#!/usr/bin/env python3
import subprocess
import time
import re
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from triton import TritonContext, ARCH, MemoryAccess, Instruction, MODE
from triton import EXCEPTION

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "large_cfg": {
        "function": "compute",
        "opt_levels": ["O0"],
        "args": [5, 3, 4, 7],
    },
    "conditions": {
        "function": "foo",
        "opt_levels": ["O0"],
        "args": [1, 2, 3],
    },
    "nested_loops": {
        "function": "accumulate",
        "opt_levels": ["O0"],
        "args": [8, 42, 7],
    },
    "switch_case": {
        "function": "classify",
        "opt_levels": ["O0"],
        "args": [5, 42, 17],
    },
}

STACK_BASE  = 0x7fff0000
RETURN_ADDR = 0xdeadbeef
MAX_STEPS   = 2_000_000
MAX_AST_LEN = 10_000_000


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


def load_binary(ctx, bpath):
    code_lo, code_hi = 0xffffffffffffffff, 0
    with open(bpath, "rb") as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg.header.p_type == "PT_LOAD":
                data = seg.data()
                vaddr = seg.header.p_vaddr
                if data:
                    ctx.setConcreteMemoryAreaValue(vaddr, list(data))
                    if seg.header.p_flags & 0x1:
                        code_lo = min(code_lo, vaddr)
                        code_hi = max(code_hi, vaddr + len(data))
    return code_lo, code_hi


def find_func_addr(bpath, func_name):
    with open(bpath, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if not isinstance(sec, SymbolTableSection):
                continue
            for sym in sec.iter_symbols():
                if sym.name == func_name:
                    a = sym.entry["st_value"]
                    if a:
                        return a
    return None


def setup_stack(ctx, func_addr, args):
    for off in range(0, 0x2000, 8):
        ctx.setConcreteMemoryValue(MemoryAccess(STACK_BASE - off, 8), 0)
    rsp = STACK_BASE - 8
    ctx.setConcreteMemoryValue(MemoryAccess(rsp, 8), RETURN_ADDR)
    ctx.setConcreteRegisterValue(ctx.registers.rsp, rsp)
    ctx.setConcreteRegisterValue(ctx.registers.rbp, rsp)
    ctx.setConcreteRegisterValue(ctx.registers.rip, func_addr)
    arg_regs = [ctx.registers.rdi, ctx.registers.rsi, ctx.registers.rdx,
                ctx.registers.rcx, ctx.registers.r8, ctx.registers.r9]
    for i, val in enumerate(args):
        if i < len(arg_regs):
            ctx.setConcreteRegisterValue(arg_regs[i], val)


def find_hook_and_measure(bpath, func_addr, func_name):
    out = subprocess.run(["objdump", "-d", bpath],
                         capture_output=True, text=True).stdout
    in_func = False
    hook = measure = None
    movq_n = 0
    movq_offsets = []

    rbp_off_re = re.compile(r'-0x([0-9a-fA-F]+)\(%rbp\)')

    for line in out.split("\n"):
        s = line.strip()
        if not s:
            continue
        if f"{func_addr:x} <{func_name}>" in s:
            in_func = True
            continue
        if not in_func:
            continue
        if s and s[0].isdigit() and ">:" in s and func_name not in s and "#" not in s.split("<")[0]:
            break

        addr = None
        if s[0].isdigit() and ":" in s:
            try:
                addr = int(s.split(":")[0].strip(), 16)
            except ValueError:
                pass

        is_real_instr = addr is not None and "\t" in line and any(
            c.isalpha() and c not in "abcdef"
            for c in line.split("\t")[-1][:4])

        if hook is None:
            if is_real_instr and "movq" in s and "$0x1," in s:
                movq_n += 1
                m = rbp_off_re.search(s)
                if m:
                    movq_offsets.append(-int(m.group(1), 16))
            elif movq_n >= 2 and is_real_instr:
                hook = addr

        if hook and not measure and is_real_instr:
            if "jmp" in s and "*%" in s:
                measure = addr

    q1_off = movq_offsets[0] if len(movq_offsets) >= 1 else None
    q2_off = movq_offsets[1] if len(movq_offsets) >= 2 else None

    return hook, measure, q1_off, q2_off


def emulate(bpath, func_name, args):
    result = {
        "binary": Path(bpath).name,
        "dispatcher_visits": 0,
        "ast_sizes": [],
        "time_sec": None,
        "error": None,
    }

    func_addr = find_func_addr(bpath, func_name)
    if not func_addr:
        result["error"] = "Function not found"
        return result

    hook_addr, measure_addr, q1_off, q2_off = find_hook_and_measure(
        bpath, func_addr, func_name)

    if not hook_addr:
        result["error"] = "No q initialization (no T-function dispatcher)"
        return result
    if not measure_addr:
        result["error"] = "No BRANCHIND found"
        return result
    if q1_off is None or q2_off is None:
        result["error"] = f"Cannot detect q1/q2 stack offsets (found {len([x for x in [q1_off, q2_off] if x is not None])} of 2)"
        return result

    ctx = TritonContext(ARCH.X86_64)
    ctx.setMode(MODE.ALIGNED_MEMORY, True)
    ctx.setMode(MODE.ONLY_ON_SYMBOLIZED, False)

    code_lo, code_hi = load_binary(ctx, bpath)
    setup_stack(ctx, func_addr, args)

    symbolized = False
    q1_stack = q2_stack = None
    start = time.time()

    for step in range(MAX_STEPS):
        pc = ctx.getConcreteRegisterValue(ctx.registers.rip)

        if pc == RETURN_ADDR:
            break

        if pc < code_lo or pc >= code_hi:
            rsp = ctx.getConcreteRegisterValue(ctx.registers.rsp)
            ret = ctx.getConcreteMemoryValue(MemoryAccess(rsp, 8))
            ctx.setConcreteRegisterValue(ctx.registers.rsp, rsp + 8)
            ctx.setConcreteRegisterValue(ctx.registers.rip, ret)
            ctx.setConcreteRegisterValue(ctx.registers.rax, 0)
            if ret == RETURN_ADDR:
                break
            continue

        if pc == hook_addr and not symbolized:
            rbp = ctx.getConcreteRegisterValue(ctx.registers.rbp)
            q1_stack = (rbp + q1_off) & 0xffffffffffffffff
            q2_stack = (rbp + q2_off) & 0xffffffffffffffff
            ctx.symbolizeMemory(MemoryAccess(q1_stack, 8), "q1")
            ctx.symbolizeMemory(MemoryAccess(q2_stack, 8), "q2")
            symbolized = True

        if pc == measure_addr and symbolized:
            q1_ast = ctx.getMemoryAst(MemoryAccess(q1_stack, 8))
            astCtx = ctx.getAstContext()
            q1_ast = astCtx.unroll(q1_ast)
            sz = len(str(q1_ast))
            result["ast_sizes"].append(sz)
            result["dispatcher_visits"] += 1
            if sz > MAX_AST_LEN:
                result["error"] = f"AST > {MAX_AST_LEN:,} at iteration {result['dispatcher_visits']}"
                break

        opcodes = ctx.getConcreteMemoryAreaValue(pc, 16)
        inst = Instruction(pc, bytes(opcodes))
        ret = ctx.processing(inst)
        if ret != EXCEPTION.NO_FAULT:
            result["error"] = f"Emulation error @ 0x{pc:x}: {inst} (code={ret})"
            break

    result["time_sec"] = round(time.time() - start, 3)
    return result


def run_test_set(test_name, cfg):
    func_name = cfg["function"]
    args = cfg["args"]

    for opt in cfg["opt_levels"]:
        available = [(d, bin_path(test_name, opt, d))
                     for d in DISPATCHERS if Path(bin_path(test_name, opt, d)).exists()]
        if not available:
            print(f"\n  [SKIP] {test_name}/{opt}: binaries not found")
            continue

        print(f"\n{'─'*78}")
        print(f"  {test_name} / -{opt} / function: {func_name}({', '.join(str(a) for a in args)})")
        print(f"{'─'*78}")

        results = []
        for disp, bp in available:
            name = disp
            print(f"  {name}...", end="", flush=True)
            r = emulate(bp, func_name, args)
            r["dispatcher_name"] = name
            results.append(r)
            err = f"  ({r['error']})" if r["error"] else ""
            print(f" {r['dispatcher_visits']} iterations, {r['time_sec']}s{err}")

        has_data = [r for r in results if r["ast_sizes"]]
        if not has_data:
            print("\n  No AST data collected.")
            for r in results:
                print(f"    {r['dispatcher_name']}: {r.get('error', 'no data')}")
            continue

        names = [r["dispatcher_name"] for r in has_data]
        max_i = max(len(r["ast_sizes"]) for r in has_data)

        print(f"\n  {'AST Growth Table':^72}")
        print(f"  {'─'*72}")
        hdr = f"  {'Iter':<6}" + "".join(f"{n:>17}" for n in names)
        print(hdr)
        print("  " + "─" * (6 + 17 * len(names)))
        for i in range(min(max_i, 30)):
            row = f"  {i+1:<6}"
            for r in has_data:
                v = r["ast_sizes"][i] if i < len(r["ast_sizes"]) else None
                row += f"{'> limit':>17}" if v is None else f"{v:>17,}"
            print(row)
        if max_i > 30:
            print(f"  ... ({max_i - 30} more rows)")

        print(f"\n  {'Growth Analysis':^72}")
        print(f"  {'─'*72}")
        for r in has_data:
            sizes = r["ast_sizes"]
            name = r["dispatcher_name"]
            if len(sizes) >= 2:
                ratios = [sizes[j] / sizes[j-1]
                          for j in range(1, len(sizes)) if sizes[j-1]]
                avg = sum(ratios) / len(ratios) if ratios else 1.0
                rmin = min(ratios) if ratios else 1.0
                rmax = max(ratios) if ratios else 1.0
                print(f"    {name:<22}: x{avg:.2f}/iter (x{rmin:.2f}–x{rmax:.2f}), "
                      f"iters={len(sizes)}, final={sizes[-1]:,}, {r['time_sec']}s")
            elif r.get("error"):
                print(f"    {name:<22}: {r['error']}")
            else:
                print(f"    {name:<22}: only 1 iteration")


def main():
    print(f"\n{'='*78}")
    print(f"{'Эксперимент 4: Рост AST (Triton, конкретно-символическая эмуляция)':^78}")
    print(f"{'='*78}")

    for test_name, cfg in TEST_SETS.items():
        run_test_set(test_name, cfg)


if __name__ == "__main__":
    main()
