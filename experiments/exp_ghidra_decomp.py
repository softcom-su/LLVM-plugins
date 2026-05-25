#!/usr/bin/env python3
import os
import subprocess
import tempfile
import shutil
import re
from pathlib import Path

DISPATCHERS = ["nopass", "trivial", "minimal_t", "klimov_shamir", "encrypted_table"]
BIN_DIR = str(Path(__file__).resolve().parent.parent / "tests" / "ll_out")

TEST_SETS = {
    "large_cfg":     {"function": "compute",    "opt_levels": ["O0", "O2"]},
    "conditions":    {"function": "foo",        "opt_levels": ["O0", "O2"]},
    "nested_loops":  {"function": "accumulate", "opt_levels": ["O0", "O2"]},
    "switch_case":   {"function": "classify",   "opt_levels": ["O0", "O2"]},
}


def _find_ghidra_headless():
    direct = [
        shutil.which("analyzeHeadless"),
        os.path.expanduser("~/ghidra_12.0.4_PUBLIC/support/analyzeHeadless"),
        "/opt/ghidra/support/analyzeHeadless",
    ]
    for path in direct:
        if path and os.path.exists(path):
            return path
    for base in (os.path.expanduser("~"), "/opt"):
        try:
            entries = os.listdir(base)
        except FileNotFoundError:
            continue
        for entry in entries:
            if entry.startswith("ghidra_") and entry.endswith("_PUBLIC"):
                p = os.path.join(base, entry, "support", "analyzeHeadless")
                if os.path.exists(p):
                    return p
    return os.path.expanduser("~/ghidra_12.0.4_PUBLIC/support/analyzeHeadless")


GHIDRA_HEADLESS = os.environ.get("GHIDRA_HEADLESS", _find_ghidra_headless())

GHIDRA_SCRIPT = '''// Decompile.java
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String funcName = System.getenv("GHIDRA_FUNC");
        if (funcName == null) funcName = "main";
        String outPath = System.getenv("GHIDRA_OUT");
        if (outPath == null) outPath = "/tmp/ghidra_decomp_out.txt";

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            Function func = null;
            SymbolTable st = currentProgram.getSymbolTable();
            for (Symbol s : st.getSymbols(funcName)) {
                func = getFunctionAt(s.getAddress());
                if (func != null) break;
            }
            if (func == null) {
                for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                    if (f.getName().equals(funcName)) { func = f; break; }
                }
            }
            if (func == null) {
                out.println("FUNCTION_NOT_FOUND:" + funcName);
                println("FUNCTION_NOT_FOUND:" + funcName);
                return;
            }

            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            DecompileResults res = decomp.decompileFunction(func, 60, monitor);
            if (res == null || res.getDecompiledFunction() == null) {
                String err = (res != null) ? res.getErrorMessage() : "no result";
                out.println("DECOMP_FAILED:" + funcName + ":" + err);
                println("DECOMP_FAILED:" + funcName + ":" + err);
                return;
            }
            out.println("=== BEGIN DECOMPILED ===");
            out.print(res.getDecompiledFunction().getC());
            out.println("=== END DECOMPILED ===");
            println("DECOMP_OK: wrote " + outPath);
        } finally {
            out.close();
        }
    }
}
'''


def bin_path(test, opt, disp):
    return f"{BIN_DIR}/{test}/{test}_{opt}_{disp}.bin"


DEBUG_DIR = Path("/tmp/ghidra_decomp_debug")


def _save_debug(bpath, proc):
    DEBUG_DIR.mkdir(parents=True, exist_ok=True)
    base = Path(bpath).stem
    (DEBUG_DIR / f"{base}.stdout.log").write_text(proc.stdout or "")
    (DEBUG_DIR / f"{base}.stderr.log").write_text(proc.stderr or "")
    return DEBUG_DIR / f"{base}.stdout.log"


def run_ghidra(bpath, func_name, scripts_dir):
    with tempfile.TemporaryDirectory() as proj_dir:
        out_file = tempfile.NamedTemporaryFile(
            mode="w", suffix=".decomp.txt", delete=False)
        out_file.close()
        out_path = out_file.name
        try:
            env = os.environ.copy()
            env["GHIDRA_FUNC"] = func_name
            env["GHIDRA_OUT"] = out_path
            cmd = [
                GHIDRA_HEADLESS, proj_dir, "TempProj",
                "-import", bpath,
                "-scriptPath", scripts_dir,
                "-postScript", "Decompile.java",
                "-deleteProject",
            ]
            try:
                proc = subprocess.run(cmd, env=env, capture_output=True,
                                      text=True, timeout=300)
            except subprocess.TimeoutExpired:
                return None, "TIMEOUT"

            content = ""
            try:
                content = Path(out_path).read_text()
            except Exception:
                pass

            m = re.search(
                r"=== BEGIN DECOMPILED ===\s*\n(.*?)\n=== END DECOMPILED ===",
                content, re.DOTALL)
            if m:
                return m.group(1), None

            combined_log = (proc.stdout or "") + "\n" + (proc.stderr or "")
            if "FUNCTION_NOT_FOUND" in content or "FUNCTION_NOT_FOUND" in combined_log:
                return None, "FUNC_NOT_FOUND"
            if "DECOMP_FAILED" in content or "DECOMP_FAILED" in combined_log:
                src = content if "DECOMP_FAILED" in content else combined_log
                mm = re.search(r"DECOMP_FAILED:[^\n]*", src)
                return None, mm.group(0) if mm else "DECOMP_FAILED"

            log_path = _save_debug(bpath, proc)
            if proc.returncode != 0:
                return None, f"EXIT {proc.returncode} (log: {log_path})"
            return None, f"NO_DECOMP_OUTPUT (log: {log_path})"
        finally:
            try:
                os.unlink(out_path)
            except FileNotFoundError:
                pass


def analyze_decomp(code):
    lines = code.split("\n")
    non_empty = [ln for ln in lines if ln.strip() and not ln.strip().startswith("//")]
    return {
        "lines_total":  len(lines),
        "lines_code":   len(non_empty),
        "gotos":        len(re.findall(r"\bgoto\s+\w+", code)),
        "labels":       len(re.findall(r"^\s*\w+:\s*$", code, re.M)),
        "switches":     len(re.findall(r"\bswitch\s*\(", code)),
        "while_true":   len(re.findall(r"while\s*\(\s*true\s*\)", code))
                      + len(re.findall(r"while\s*\(\s*1\s*\)", code))
                      + len(re.findall(r"for\s*\(\s*;\s*;\s*\)", code)),
        "local_vars":   len(re.findall(r"^\s*(?:u?int\d+_t|long|ulong|int|uint)\s+\w+",
                                       code, re.M)),
        "calls":        len(re.findall(r"\w+\s*\([^)]*\)\s*;", code)),
    }


def run_test_set(test_name, func_name, opt_levels, scripts_dir):
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
            print(f"  {disp}...", end="", flush=True)
            code, err = run_ghidra(bp, func_name, scripts_dir)
            if err:
                print(f" ERROR: {err}")
                results.append({"name": disp, "error": err})
                continue
            r = analyze_decomp(code)
            r["name"] = disp
            r["code"] = code
            results.append(r)
            print(f" {r['lines_code']} lines, {r['gotos']} gotos, "
                  f"{r['local_vars']} vars, {r['switches']} switch")

            out_dir = Path(BIN_DIR) / test_name
            out_dir.mkdir(parents=True, exist_ok=True)
            (out_dir / f"{test_name}_{opt}_{disp}.decomp.c").write_text(code)

        valid = [r for r in results if "error" not in r]
        if not valid:
            continue

        print(f"\n  {'Таблица: метрики декомпиляции Ghidra':^72}")
        print(f"  {'─'*72}")
        print(f"  {'Диспетчер':<20} {'Lines':>6} {'goto':>5} {'switch':>7} "
              f"{'while(1)':>9} {'vars':>5} {'calls':>6}")
        print(f"  {'─'*72}")
        for r in valid:
            print(f"  {r['name']:<20} {r['lines_code']:>6} {r['gotos']:>5} "
                  f"{r['switches']:>7} {r['while_true']:>9} "
                  f"{r['local_vars']:>5} {r['calls']:>6}")

        base = next((r for r in valid if r["name"] == "nopass"), None)
        if base:
            print(f"\n  {'Blow-up vs nopass':^72}")
            print(f"  {'─'*72}")
            for r in valid:
                if r["name"] == "nopass":
                    continue
                ratio = r["lines_code"] / base["lines_code"] if base["lines_code"] else 0
                print(f"  {r['name']:<20} {ratio:>.2f}x lines, "
                      f"+{r['gotos']-base['gotos']} goto, "
                      f"+{r['local_vars']-base['local_vars']} vars")


def main():
    print(f"\n{'='*78}")
    print(f"{'Эксперимент 6: Декомпиляция Ghidra':^78}")
    print(f"{'='*78}")

    if not os.path.exists(GHIDRA_HEADLESS):
        print(f"\n  ERROR: Ghidra analyzeHeadless not found at {GHIDRA_HEADLESS}")
        return

    with tempfile.TemporaryDirectory() as scripts_dir:
        (Path(scripts_dir) / "Decompile.java").write_text(GHIDRA_SCRIPT)

        for test_name, cfg in TEST_SETS.items():
            run_test_set(test_name, cfg["function"], cfg["opt_levels"], scripts_dir)


if __name__ == "__main__":
    main()
