#!/usr/bin/env python3
import subprocess
import statistics
import time
import os
import re
import sys
from pathlib import Path

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "large_cfg":     {"function": "compute",          "opt_levels": ["O0", "O2"]},
    "conditions":    {"function": "foo",              "opt_levels": ["O0", "O2"]},
    "nested_loops":  {"function": "accumulate",       "opt_levels": ["O0", "O2"]},
    "switch_case":   {"function": "classify",         "opt_levels": ["O0", "O2"]},
    "heavy_arith":   {"function": "heavy_compute",    "opt_levels": ["O0", "O2"]},
    "mini_aes":      {"function": "mini_aes_encrypt", "opt_levels": ["O0", "O2"]},
}

N_RUNS = 30
CALLGRIND_RUNS = 1


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


def get_func_text_size(bpath, func_name):
    result = subprocess.run(["objdump", "-t", bpath],
                            capture_output=True, text=True)
    for line in result.stdout.split("\n"):
        parts = line.split()
        if len(parts) >= 6 and parts[-1] == func_name:
            try:
                return int(parts[4], 16)
            except (ValueError, IndexError):
                pass
    return None


def get_total_text_size(bpath):
    result = subprocess.run(["size", "-A", bpath],
                            capture_output=True, text=True)
    for line in result.stdout.split("\n"):
        parts = line.split()
        if len(parts) >= 2 and parts[0] == ".text":
            try:
                return int(parts[1])
            except ValueError:
                pass
    return None


def measure_wallclock(bpath, n_runs):
    times = []
    for _ in range(n_runs):
        start = time.perf_counter_ns()
        try:
            proc = subprocess.run([bpath], capture_output=True, timeout=10)
            elapsed_ns = time.perf_counter_ns() - start
            if proc.returncode == 0:
                times.append(elapsed_ns / 1e6)
        except (subprocess.TimeoutExpired, Exception):
            pass
    if not times:
        return None, None, None
    return (round(statistics.median(times), 3),
            round(statistics.stdev(times), 3) if len(times) > 1 else 0,
            round(min(times), 3))


def measure_instructions_once(bpath, func_name=None):
    outfile = f"/tmp/callgrind_exp_{os.getpid()}"
    try:
        os.unlink(outfile)
    except FileNotFoundError:
        pass
    try:
        proc = subprocess.run(
            ["valgrind", "--tool=callgrind",
             "--collect-jumps=no",
             "--cache-sim=no",
             f"--callgrind-out-file={outfile}", bpath],
            capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return None, None

    total_ir = None
    for line in proc.stderr.split("\n"):
        m = re.search(r"I\s+refs:\s+([\d,]+)", line)
        if m:
            total_ir = int(m.group(1).replace(",", ""))
            break

    func_ir = None
    if func_name:
        try:
            with open(outfile, "r") as f:
                lines = f.read().split("\n")
            id_to_name = {}
            for line in lines:
                for prefix in ("fn=", "cfn="):
                    if line.startswith(prefix):
                        rest = line[len(prefix):]
                        if rest.startswith("("):
                            paren_end = rest.find(")")
                            if paren_end != -1:
                                fid = rest[1:paren_end]
                                name_part = rest[paren_end + 1:].strip()
                                if name_part:
                                    id_to_name[fid] = name_part
            target_ids = {fid for fid, name in id_to_name.items()
                          if name == func_name}
            ir_sum = 0
            in_func = False
            for line in lines:
                if line.startswith("fn="):
                    rest = line[3:]
                    if rest.startswith("("):
                        paren_end = rest.find(")")
                        fid = rest[1:paren_end] if paren_end != -1 else ""
                        in_func = fid in target_ids
                    else:
                        in_func = (rest.strip() == func_name)
                    continue
                if not in_func:
                    continue
                if line.startswith(("fl=", "ob=", "cfn=", "cfi=", "cob=",
                                    "calls=", "fe=", "fi=", "#",
                                    "totals:", "summary:")):
                    continue
                if not line.strip():
                    continue
                parts = line.split()
                if len(parts) >= 2:
                    try:
                        ir_sum += int(parts[-1])
                    except ValueError:
                        pass
            if ir_sum > 0:
                if total_ir and ir_sum > total_ir:
                    pass
                else:
                    func_ir = ir_sum
        except FileNotFoundError:
            pass

    try:
        os.unlink(outfile)
    except FileNotFoundError:
        pass
    return total_ir, func_ir


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
        baseline_ir = None
        baseline_func_ir = None

        for disp, bp in available:
            print(f"    {disp}...", end="", flush=True)
            r = {"name": disp}
            r["func_size"] = get_func_text_size(bp, func_name)
            r["text_size"] = get_total_text_size(bp)
            median, std, mintime = measure_wallclock(bp, N_RUNS)
            r["median_ms"] = median
            r["std_ms"] = std
            r["min_ms"] = mintime
            total_ir, func_ir = measure_instructions_once(bp, func_name)
            r["total_ir"] = total_ir
            r["func_ir"] = func_ir

            if disp == "nopass":
                baseline_ir = r["total_ir"]
                baseline_func_ir = r["func_ir"]

            results.append(r)
            tot_str = f"{r['total_ir']:,}" if r["total_ir"] else "?"
            fn_str  = f"{r['func_ir']:,}" if r["func_ir"] else "—"
            print(f" {median}ms, {tot_str} total, {fn_str} in {func_name}()")

        print(f"\n  {'Таблица 1: Wall-clock time':^70}")
        print(f"  {'─'*70}")
        print(f"  {'Диспетчер':<20} {'Медиана':>10} {'+-std':>8} {'Min':>8} {'Slowdown':>10}")
        print(f"  {'─'*70}")
        base_time = next((r["median_ms"] for r in results if r["name"] == "nopass"), None)
        for r in results:
            med = f"{r['median_ms']}ms" if r["median_ms"] else "—"
            std = f"+-{r['std_ms']}ms" if r["std_ms"] else "—"
            mn = f"{r['min_ms']}ms" if r["min_ms"] else "—"
            sl = f"{round(r['median_ms']/base_time, 2)}x" if base_time and r["median_ms"] else "—"
            print(f"  {r['name']:<20} {med:>10} {std:>8} {mn:>8} {sl:>10}")

        print(f"\n  {'Таблица 2: Инструкции внутри функции (callgrind)':^70}")
        print(f"  {'─'*70}")
        print(f"  {'Диспетчер':<20} {'Func instr':>14} {'Slowdown':>10} {'func size':>10} {'.text':>8}")
        print(f"  {'─'*70}")
        for r in results:
            fir = f"{r['func_ir']:>14,}" if r["func_ir"] else f"{'—':>14}"
            sl = (f"{round(r['func_ir']/baseline_func_ir, 2)}x"
                  if baseline_func_ir and r["func_ir"] else "—")
            fs = f"{r['func_size']}B" if r["func_size"] else "—"
            ts = f"{r['text_size']}B" if r["text_size"] else "—"
            print(f"  {r['name']:<20} {fir} {sl:>10} {fs:>10} {ts:>8}")

        if baseline_func_ir:
            base_fs = next((r.get("func_size", 0) or 0 for r in results if r["name"] == "nopass"), 0)
            print(f"\n  {'Таблица 3: Overhead функции vs nopass':^70}")
            print(f"  {'─'*70}")
            print(f"  {'Диспетчер':<20} {'Доп. инстр.':>16} {'Доп. код':>12} {'Overhead %':>12}")
            print(f"  {'─'*70}")
            for r in results:
                if r["name"] == "nopass":
                    continue
                if r["func_ir"]:
                    extra_ir = r["func_ir"] - baseline_func_ir
                    overhead_pct = round(100 * extra_ir / baseline_func_ir, 1)
                    eir = f"+{extra_ir:,}"
                    op = f"+{overhead_pct}%"
                else:
                    eir = "—"
                    op = "—"
                extra_code = (r.get("func_size", 0) or 0) - base_fs
                ec = f"+{extra_code}B" if extra_code else "—"
                print(f"  {r['name']:<20} {eir:>16} {ec:>12} {op:>12}")

        if baseline_ir:
            print(f"\n  {'Таблица 4: Overhead всего процесса (для справки)':^70}")
            print(f"  {'─'*70}")
            print(f"  {'Диспетчер':<20} {'Total instr':>16} {'Slowdown':>12}")
            print(f"  {'─'*70}")
            for r in results:
                ir = f"{r['total_ir']:,}" if r["total_ir"] else "—"
                sl = (f"{round(r['total_ir']/baseline_ir, 3)}x"
                      if baseline_ir and r["total_ir"] else "—")
                print(f"  {r['name']:<20} {ir:>16} {sl:>12}")


def main():
    print(f"\n{'='*78}")
    print(f"{'Эксперимент 1: Runtime performance overhead':^78}")
    print(f"{'='*78}")

    for test_name, cfg in TEST_SETS.items():
        run_test_set(test_name, cfg["function"], cfg["opt_levels"])


if __name__ == "__main__":
    main()
