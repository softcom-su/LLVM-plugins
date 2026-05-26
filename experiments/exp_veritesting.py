#!/usr/bin/env python3
import time
import logging
import sys
import resource
from pathlib import Path
from collections import defaultdict

try:
    import angr
    import claripy
except ImportError:
    print("angr не найден. Установите: pip install angr")
    sys.exit(1)

logging.getLogger("angr").setLevel(logging.ERROR)
logging.getLogger("claripy").setLevel(logging.ERROR)
logging.getLogger("cle").setLevel(logging.ERROR)

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "conditions": {
        "function": "foo",
        "opt_levels": ["O0"],
        "sym_args": [("a", 32), ("b", 32), ("c", 32)],
        "constraints": lambda syms: [syms[2] != 0],
    },
    "nested_loops": {
        "function": "accumulate",
        "opt_levels": ["O0"],
        "sym_args": [("n", 32), ("seed", 32), ("step", 32)],
        "constraints": lambda syms: [syms[2] != 0],
    },
    "switch_case": {
        "function": "classify",
        "opt_levels": ["O0"],
        "sym_args": [("op", 32), ("a", 32), ("b", 32)],
        "constraints": lambda syms: [syms[2] != 0],
    },
}

TIMEOUT = 30
STEP_LIMIT = 300
MEM_LIMIT_GB = 4


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


def find_func_addr(bpath, func_name):
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
    with open(bpath, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if not isinstance(sec, SymbolTableSection):
                continue
            for sym in sec.iter_symbols():
                if sym.name == func_name and sym.entry["st_value"] != 0:
                    return sym.entry["st_value"]
    return None


def count_basic_blocks(proj, func_addr):
    try:
        cfg = proj.analyses.CFGFast(
            regions=[(func_addr, func_addr + 0x10000)],
            normalize=True)
        func = cfg.kb.functions.get(func_addr)
        if func:
            return len(list(func.blocks))
    except Exception:
        pass
    return None


def run_symbolic(bpath, func_name, sym_arg_defs, constraints_fn, use_veritesting):
    result = {
        "binary": Path(bpath).name,
        "veritesting": use_veritesting,
        "explored": 0, "deadended": 0, "active_at_end": 0,
        "errored": 0, "blocks_seen": 0, "total_blocks": None,
        "coverage_pct": None, "time_sec": None, "steps": 0,
        "timed_out": False, "error": None, "ms_per_step": None,
    }

    func_addr = find_func_addr(bpath, func_name)
    if func_addr is None:
        result["error"] = "Function not found"
        return result

    try:
        proj = angr.Project(bpath, auto_load_libs=False, main_opts={"base_addr": 0})
    except Exception as e:
        result["error"] = f"Load error: {e}"
        return result

    sym_vars = []
    for name, bits in sym_arg_defs:
        sym_vars.append(claripy.BVS(name, bits))

    ret_addr = proj.loader.extern_object.allocate()
    state = proj.factory.call_state(func_addr, *sym_vars, ret_addr=ret_addr)

    if constraints_fn:
        for c in constraints_fn(sym_vars):
            state.add_constraints(c)

    simgr = proj.factory.simulation_manager(state)
    if use_veritesting:
        simgr.use_technique(angr.exploration_techniques.Veritesting())

    blocks_visited = set()
    total_explored = 0

    start = time.time()
    timed_out = False

    try:
        while simgr.active:
            if time.time() - start > TIMEOUT:
                timed_out = True
                break
            if total_explored >= STEP_LIMIT:
                timed_out = True
                break

            for s in simgr.active:
                blocks_visited.add(s.addr)

            simgr.step()
            total_explored += 1

            simgr.move(from_stash="active", to_stash="deadended",
                       filter_func=lambda s: s.addr == ret_addr)
    except MemoryError:
        result["error"] = "OOM"
    except Exception as e:
        result["error"] = f"Error: {e}"

    elapsed = time.time() - start
    total_blocks = count_basic_blocks(proj, func_addr)

    result["time_sec"] = round(elapsed, 3)
    result["explored"] = total_explored
    result["deadended"] = len(simgr.deadended)
    result["active_at_end"] = len(simgr.active)
    result["errored"] = len(simgr.errored) if hasattr(simgr, 'errored') else 0
    result["blocks_seen"] = len(blocks_visited)
    result["total_blocks"] = total_blocks
    result["timed_out"] = timed_out
    result["ms_per_step"] = round(1000 * elapsed / total_explored, 1) if total_explored else None

    if total_blocks and total_blocks > 0:
        result["coverage_pct"] = round(100.0 * len(blocks_visited) / total_blocks, 1)

    return result


def run_test_set(test_name, cfg):
    func_name = cfg["function"]
    sym_args = cfg["sym_args"]
    constraints_fn = cfg.get("constraints")

    for opt in cfg["opt_levels"]:
        available = [(d, bin_path(test_name, opt, d))
                     for d in DISPATCHERS if Path(bin_path(test_name, opt, d)).exists()]
        if not available:
            print(f"\n  [SKIP] {test_name}/{opt}: binaries not found")
            continue

        print(f"\n{'─'*78}")
        print(f"  {test_name} / -{opt} / function: {func_name}()")
        print(f"{'─'*78}")

        all_results = []

        for disp, bp in available:
            name = disp
            print(f"\n  >>> {name}")
            for veri in [False, True]:
                label = "veritesting" if veri else "standard  "
                print(f"    {label}: ", end="", flush=True)

                r = run_symbolic(bp, func_name, sym_args, constraints_fn, veri)
                r["dispatcher"] = name
                all_results.append(r)

                if r["error"]:
                    print(f"ERROR: {r['error']}")
                    continue

                status = "TIMEOUT" if r["timed_out"] else "done"
                cov = f"{r['coverage_pct']}%" if r["coverage_pct"] is not None else "?"
                print(f"{r['time_sec']:>6}s | "
                      f"steps: {r['explored']:>5} | "
                      f"paths: {r['deadended']:>3} | "
                      f"active: {r['active_at_end']:>4} | "
                      f"blocks: {r['blocks_seen']:>3} | "
                      f"coverage: {cov:>5} | {status}")

        if all_results:
            print(f"\n  {'─'*82}")
            print(f"  {'Summary':^82}")
            print(f"  {'─'*82}")
            print(f"  {'Dispatcher':<20} {'Mode':<14} {'Time':>7} {'Steps':>6} "
                  f"{'Paths':>6} {'Active':>6} {'Blocks':>7} {'ms/step':>8}")
            print(f"  {'─'*82}")

            for r in all_results:
                if r.get("error"):
                    continue
                mode = "veritesting" if r["veritesting"] else "standard"
                t = f">{TIMEOUT}s" if r["timed_out"] else f"{r['time_sec']}s"
                ms = f"{r['ms_per_step']}ms" if r.get("ms_per_step") is not None else "—"
                print(f"  {r['dispatcher']:<20} {mode:<14} {t:>7} "
                      f"{r['explored']:>6} {r['deadended']:>6} "
                      f"{r['active_at_end']:>6} {r['blocks_seen']:>7} {ms:>8}")


def main():
    soft, hard = resource.getrlimit(resource.RLIMIT_AS)
    resource.setrlimit(resource.RLIMIT_AS,
                       (MEM_LIMIT_GB * 1024**3, hard))

    print(f"\n{'='*78}")
    print(f"{'Эксперимент 2: Символическое покрытие с veritesting (angr)':^78}")
    print(f"{'='*78}")

    for test_name, cfg in TEST_SETS.items():
        run_test_set(test_name, cfg)


if __name__ == "__main__":
    main()
