#!/usr/bin/env python3
import sys
from pathlib import Path

from miasm.analysis.binary import Container
from miasm.analysis.machine import Machine
from miasm.core.locationdb import LocationDB
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "conditions":    {"function": "foo",        "opt_levels": ["O0", "O2"]},
    "large_cfg":     {"function": "compute",    "opt_levels": ["O0", "O2"]},
    "nested_loops":  {"function": "accumulate", "opt_levels": ["O0", "O2"]},
    "switch_case":   {"function": "classify",   "opt_levels": ["O0", "O2"]},
}


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


def find_func_addr(bpath, func_name):
    with open(bpath, "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if not isinstance(section, SymbolTableSection):
                continue
            for sym in section.iter_symbols():
                if sym.name == func_name:
                    addr = sym.entry["st_value"]
                    if addr != 0:
                        return addr
    return None


def analyze_binary(bpath, func_name):
    result = {
        "binary": Path(bpath).name,
        "function": func_name,
        "func_addr": None,
        "asm_blocks": None,
        "unresolved_branches": None,
        "static_complete": False,
        "error": None,
    }

    try:
        loc_db = LocationDB()
        cont = Container.from_stream(open(bpath, "rb"), loc_db)
    except Exception as e:
        result["error"] = f"Cannot open binary: {e}"
        return result

    machine = Machine(cont.arch)

    try:
        func_addr = find_func_addr(bpath, func_name)
    except Exception as e:
        result["error"] = f"Symbol read error: {e}"
        return result

    if func_addr is None:
        result["error"] = f"Function '{func_name}' not found"
        return result

    result["func_addr"] = f"0x{func_addr:x}"

    try:
        dis = machine.dis_engine(cont.bin_stream, loc_db=loc_db)
        asm_cfg = dis.dis_multiblock(func_addr)
    except Exception as e:
        result["error"] = f"Disassembly error: {e}"
        return result

    blocks = list(asm_cfg.blocks)
    result["asm_blocks"] = len(blocks)

    unresolved = 0
    for block in blocks:
        if not block.lines:
            continue
        last_instr = block.lines[-1]
        if last_instr.name in ("RET", "RETF", "RETN"):
            continue
        successors = list(asm_cfg.successors(block.loc_key))
        if len(successors) == 0:
            unresolved += 1

    result["unresolved_branches"] = unresolved
    result["static_complete"] = (unresolved == 0)
    return result


def run_test_set(test_name, func_name, opt_levels):
    for opt in opt_levels:
        available = [(d, bin_path(test_name, opt, d))
                     for d in DISPATCHERS if Path(bin_path(test_name, opt, d)).exists()]
        if not available:
            print(f"\n  [SKIP] {test_name}/{opt}: binaries not found")
            continue

        print(f"\n{'─'*78}")
        print(f"  {test_name} / -{opt} / function: {func_name}()")
        print(f"{'─'*78}")

        results = []
        for disp, bp in available:
            name = disp
            r = analyze_binary(bp, func_name)
            r["dispatcher_name"] = name
            results.append(r)

            if r["error"]:
                print(f"  {name:<22} ERROR: {r['error']}")
            else:
                status = "COMPLETE" if r["static_complete"] else "INCOMPLETE"
                print(f"  {name:<22} blocks: {r['asm_blocks']:>4}  "
                      f"unresolved: {r['unresolved_branches']}  "
                      f"CFG: {status}")

        valid = [r for r in results if not r.get("error")]
        if valid:
            print(f"\n  {'Summary':^65}")
            print(f"  {'─'*65}")
            print(f"  {'Dispatcher':<22} {'Blocks':>7} {'Unresolved':>11} {'CFG complete':>13}")
            print(f"  {'─'*65}")
            for r in valid:
                complete = "yes" if r["static_complete"] else "no"
                print(f"  {r['dispatcher_name']:<22} {r['asm_blocks']:>7} "
                      f"{r['unresolved_branches']:>11} {complete:>13}")


def main():
    if len(sys.argv) == 3:
        r = analyze_binary(sys.argv[1], sys.argv[2])
        if r["error"]:
            print(f"ERROR: {r['error']}")
        else:
            status = "COMPLETE" if r["static_complete"] else "INCOMPLETE"
            print(f"{r['binary']}: {r['asm_blocks']} blocks, "
                  f"{r['unresolved_branches']} unresolved, CFG {status}")
        return

    print(f"\n{'='*78}")
    print(f"{'Эксперимент 5: Разрешаемость переходов (miasm)':^78}")
    print(f"{'='*78}")

    for test_name, cfg in TEST_SETS.items():
        run_test_set(test_name, cfg["function"], cfg["opt_levels"])


if __name__ == "__main__":
    main()
