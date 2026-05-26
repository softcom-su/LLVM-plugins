#!/usr/bin/env python3
import subprocess
import math
import sys
import re
from pathlib import Path
from collections import Counter

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "large_cfg":     {"function": "compute",    "opt_levels": ["O0", "O2"]},
    "conditions":    {"function": "foo",        "opt_levels": ["O0", "O2"]},
    "nested_loops":  {"function": "accumulate", "opt_levels": ["O0", "O2"]},
    "switch_case":   {"function": "classify",   "opt_levels": ["O0", "O2"]},
}


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


def get_entry_point(bpath):
    result = subprocess.run(["readelf", "-h", bpath],
                            capture_output=True, text=True)
    for line in result.stdout.split("\n"):
        if "Entry point" in line:
            m = re.search(r"0x([0-9a-fA-F]+)", line)
            if m:
                return int(m.group(1), 16)
    return None


def detect_pie_offset(bpath, trace):
    entry = get_entry_point(bpath)
    if entry is None:
        return 0
    unique_addrs = set(trace)
    candidates = [addr - entry for addr in unique_addrs
                  if addr >= entry and (addr - entry) & 0xFFF == 0]
    return min(candidates) if candidates else 0


def record_trace(bpath):
    try:
        proc = subprocess.run(
            ["valgrind", "--tool=lackey", "--trace-superblocks=yes", bpath],
            capture_output=True, text=True, timeout=60)
    except FileNotFoundError:
        print("valgrind not found.")
        sys.exit(1)
    except subprocess.TimeoutExpired:
        return None, 0

    trace = []
    for line in proc.stderr.split("\n"):
        line = line.strip()
        if line.startswith("SB "):
            try:
                trace.append(int(line[3:], 16))
            except ValueError:
                pass
    return trace, detect_pie_offset(bpath, trace)


def find_func_range(bpath, func_name):
    result = subprocess.run(["objdump", "-t", bpath],
                            capture_output=True, text=True)
    for line in result.stdout.split("\n"):
        parts = line.split()
        if len(parts) >= 6 and parts[-1] == func_name and "F" in parts[2]:
            addr = int(parts[0], 16)
            size = int(parts[4], 16)
            return addr, addr + size
    return None, None


def dispatcher_fraction_from_trace(func_trace):
    if not func_trace:
        return 0.0, None
    counts = Counter(func_trace)
    avg_count = len(func_trace) / len(counts)
    threshold = avg_count * 3.0
    disp_entries = sum(cnt for cnt in counts.values() if cnt >= threshold)
    disp_addr = max(counts, key=counts.get)
    return round(disp_entries / len(func_trace), 4), disp_addr


def shannon_entropy(sequence):
    if not sequence:
        return 0.0
    counts = Counter(sequence)
    total = len(sequence)
    return -sum((c / total) * math.log2(c / total)
                for c in counts.values() if c > 0)


def max_entropy(n_unique):
    return math.log2(n_unique) if n_unique > 1 else 0.0


def normalized_entropy(sequence):
    if not sequence:
        return 0.0
    maxe = max_entropy(len(set(sequence)))
    return shannon_entropy(sequence) / maxe if maxe > 0 else 0.0


def transition_entropy(trace):
    if len(trace) < 2:
        return 0.0, 0
    transitions = [(trace[i], trace[i + 1]) for i in range(len(trace) - 1)]
    return shannon_entropy(transitions), len(set(transitions))


def autocorrelation_score(trace, lags=(1, 2, 3, 5)):
    if len(trace) < max(lags) + 1:
        return {}
    scores = {}
    for lag in lags:
        matches = sum(1 for i in range(lag, len(trace)) if trace[i] == trace[i - lag])
        scores[lag] = round(matches / (len(trace) - lag), 4)
    return scores


def analyze_binary(bpath, func_name):
    result = {"binary": Path(bpath).name, "error": None}

    func_start, func_end = find_func_range(bpath, func_name)
    if func_start is None:
        result["error"] = "Function not found"
        return result

    trace, pie_offset = record_trace(bpath)
    if trace is None:
        result["error"] = "Trace timeout"
        return result

    rt_func_start = func_start + pie_offset
    rt_func_end = func_end + pie_offset
    func_trace = [a for a in trace if rt_func_start <= a < rt_func_end]

    if not func_trace:
        result["error"] = "Empty function trace"
        return result

    disp_fraction, rt_disp_addr = dispatcher_fraction_from_trace(func_trace)
    disp_addr_static = (rt_disp_addr - pie_offset) if rt_disp_addr else None

    counts = Counter(func_trace)
    avg_count = len(func_trace) / len(counts)
    threshold = avg_count * 3.0
    code_trace = [a for a in func_trace if counts[a] < threshold]

    result["trace_len"] = len(func_trace)
    result["unique_blocks"] = len(set(func_trace))
    result["has_dispatcher"] = disp_fraction > 0.05
    result["dispatcher_addr"] = f"0x{disp_addr_static:x}" if disp_addr_static else None
    result["entropy"] = round(shannon_entropy(func_trace), 3)
    result["max_entropy"] = round(max_entropy(len(set(func_trace))), 3)
    result["norm_entropy"] = round(normalized_entropy(func_trace), 4)

    trans_ent, n_transitions = transition_entropy(func_trace)
    result["transition_entropy"] = round(trans_ent, 3)
    result["unique_transitions"] = n_transitions
    result["autocorrelation"] = autocorrelation_score(func_trace)
    result["dispatcher_fraction"] = disp_fraction

    if code_trace:
        result["code_entropy"] = round(shannon_entropy(code_trace), 3)
        result["code_norm_entropy"] = round(normalized_entropy(code_trace), 4)
    else:
        result["code_entropy"] = 0.0
        result["code_norm_entropy"] = 0.0

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
            print(f"  {name}...", end="", flush=True)
            r = analyze_binary(bp, func_name)
            r["dispatcher_name"] = name
            results.append(r)
            if r.get("error"):
                print(f" ERROR: {r['error']}")
            else:
                print(f" trace: {r['trace_len']} blocks, "
                      f"unique: {r['unique_blocks']}, "
                      f"H={r['entropy']:.2f} bit, "
                      f"H_norm={r['norm_entropy']:.3f}")

        valid = [r for r in results if not r.get("error")]
        if not valid:
            continue

        print(f"\n  {'Таблица 1: Метрики трассы':^72}")
        print(f"  {'─'*72}")
        print(f"  {'Диспетчер':<20} {'Длина':>7} {'Уник.':>6} {'H(trace)':>9} "
              f"{'H_max':>6} {'H_norm':>7} {'Disp%':>8}")
        print(f"  {'─'*72}")
        for r in valid:
            print(f"  {r['dispatcher_name']:<20} {r['trace_len']:>7} "
                  f"{r['unique_blocks']:>6} {r['entropy']:>9.3f} "
                  f"{r['max_entropy']:>6.2f} {r['norm_entropy']:>7.4f} "
                  f"{r['dispatcher_fraction']*100:>7.1f}%")

        print(f"\n  {'Таблица 2: Энтропия переходов':^72}")
        print(f"  {'─'*72}")
        print(f"  {'Диспетчер':<20} {'H(transitions)':>15} {'Unique trans.':>14}")
        print(f"  {'─'*52}")
        for r in valid:
            print(f"  {r['dispatcher_name']:<20} {r['transition_entropy']:>15.3f} "
                  f"{r['unique_transitions']:>14}")

        lags = [1, 2, 3, 5]
        print(f"\n  {'Таблица 3: Автокорреляция':^72}")
        print(f"  {'─'*72}")
        hdr = f"  {'Диспетчер':<20}"
        for lag in lags:
            hdr += f" {'lag='+str(lag):>8}"
        print(hdr)
        print(f"  {'─'*52}")
        for r in valid:
            row = f"  {r['dispatcher_name']:<20}"
            ac = r.get("autocorrelation", {})
            for lag in lags:
                row += f" {ac.get(lag, 0):>8.4f}"
            print(row)


def main():
    print(f"\n{'='*78}")
    print(f"{'Эксперимент 3: Энтропия трассы выполнения':^78}")
    print(f"{'='*78}")

    for test_name, cfg in TEST_SETS.items():
        run_test_set(test_name, cfg["function"], cfg["opt_levels"])


if __name__ == "__main__":
    main()
