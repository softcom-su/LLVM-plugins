#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LLVM_VERSION="${1:-17}"
RESULTS_DIR="${REPO_ROOT}/experiment_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${RESULTS_DIR}/results_${TIMESTAMP}.txt"

VENV_ANGR="${VENV_ANGR:-${HOME}/.venv_angr}"
VENV_TRITON="${VENV_TRITON:-${HOME}/.venv_triton}"
VENV_MIASM="${VENV_MIASM:-${HOME}/.venv_miasm}"

SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_GHIDRA="${SKIP_GHIDRA:-0}"
export CFF_SEED="${CFF_SEED:-12345}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log()  { echo -e "${CYAN}[$(date +%H:%M:%S)]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; }

run_in_venv() {
    local venv="$1"; shift
    local name="$1"; shift
    if [[ ! -d "${venv}" ]]; then
        fail "${name}: venv not found at ${venv}"
        echo "  Create it:  python3 -m venv ${venv} && ${venv}/bin/pip install ${name}" | tee -a "$RESULTS_FILE"
        return 1
    fi
    (
        source "${venv}/bin/activate"
        "$@"
    )
}

run_experiment() {
    local num="$1"
    local title="$2"
    local cmd="$3"

    echo "" | tee -a "$RESULTS_FILE"
    echo "================================================================" | tee -a "$RESULTS_FILE"
    echo "  Experiment ${num}: ${title}" | tee -a "$RESULTS_FILE"
    echo "================================================================" | tee -a "$RESULTS_FILE"
    log "Starting: ${title}"

    local start_time=$SECONDS
    if eval "${cmd}" 2>&1 | tee -a "$RESULTS_FILE"; then
        local elapsed=$(( SECONDS - start_time ))
        ok "${title} (${elapsed}s)"
    else
        fail "${title}"
    fi
}

log "Preflight checks..."

missing=0
for tool in valgrind objdump readelf; do
    if ! command -v "$tool" &>/dev/null; then
        fail "Missing: $tool"
        missing=1
    fi
done

for venv_path in "$VENV_ANGR" "$VENV_TRITON" "$VENV_MIASM"; do
    if [[ ! -d "$venv_path" ]]; then
        warn "venv not found: $venv_path (experiment will be skipped)"
    fi
done

if [[ "$missing" -eq 1 ]]; then
    fail "Install missing tools and retry."
    exit 1
fi

mkdir -p "$RESULTS_DIR"

{
    echo "============================================================"
    echo "  Experiment results — $(date)"
    echo "  LLVM version: ${LLVM_VERSION}"
    echo "  CFF_SEED: ${CFF_SEED}"
    echo "============================================================"
} | tee "$RESULTS_FILE"

if [[ "$SKIP_BUILD" -eq 1 ]]; then
    log "SKIP_BUILD=1, skipping build step"
else
    log "Building plugin and test binaries (LLVM ${LLVM_VERSION})..."
    if "${SCRIPT_DIR}/prepare_experiments.sh" "$LLVM_VERSION" 2>&1 | tail -5 | tee -a "$RESULTS_FILE"; then
        ok "Build complete"
    else
        fail "Build failed — check prepare_experiments.sh output"
        exit 1
    fi
fi

run_experiment 1 "Performance (callgrind + wall-clock)" \
    "python3 '${SCRIPT_DIR}/exp_performance.py'"

run_experiment 2 "Veritesting / angr (symbolic execution)" \
    "run_in_venv '${VENV_ANGR}' angr python3 '${SCRIPT_DIR}/exp_veritesting.py'"

run_experiment 3 "Trace entropy (valgrind lackey)" \
    "python3 '${SCRIPT_DIR}/exp_trace_entropy.py'"

if [[ -n "${VENV_TRITON}" ]]; then
    run_experiment 4 "Triton AST (concolic emulation)" \
        "run_in_venv '${VENV_TRITON}' triton python3 '${SCRIPT_DIR}/exp_triton_ast.py'"
else
    run_experiment 4 "Triton AST (concolic emulation)" \
        "python3 '${SCRIPT_DIR}/exp_triton_ast.py'"
fi

run_experiment 5 "miasm (static CFG analysis)" \
    "run_in_venv '${VENV_MIASM}' miasm python3 '${SCRIPT_DIR}/exp_miasm_deflat.py'"

if [[ "$SKIP_GHIDRA" -eq 1 ]]; then
    log "SKIP_GHIDRA=1, skipping Ghidra experiment"
    echo "" >> "$RESULTS_FILE"
    echo "Experiment 6: Ghidra — SKIPPED (SKIP_GHIDRA=1)" >> "$RESULTS_FILE"
else
    run_experiment 6 "Ghidra decompilation" \
        "python3 '${SCRIPT_DIR}/exp_ghidra_decomp.py'"
fi

echo "" | tee -a "$RESULTS_FILE"
echo "================================================================" | tee -a "$RESULTS_FILE"
echo "  All experiments complete — $(date)" | tee -a "$RESULTS_FILE"
echo "  Full results: ${RESULTS_FILE}" | tee -a "$RESULTS_FILE"
echo "================================================================" | tee -a "$RESULTS_FILE"

log "Done. Results saved to ${RESULTS_FILE}"