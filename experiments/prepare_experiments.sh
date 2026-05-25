#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

set -euo pipefail

LLVM_VER="${1:-17}"

CC_C="clang-${LLVM_VER}"
CC_CPP="clang++-${LLVM_VER}"

PLUGIN="${REPO_ROOT}/build/libs/CFG/libCFGFlattening.so"

SRC_DIRS=("${REPO_ROOT}/tests/benchmarks")
OUT_DIR="${REPO_ROOT}/tests/ll_out"

OPTS=(O0 O2)

DISPATCHERS=(
    nopass
    trivial
    minimal_t
    klimov_shamir
    encrypted_table
)

export CFF_SEED="${CFF_SEED:-12345}"

R='\033[0;31m'
G='\033[0;32m'
C='\033[0;36m'
B='\033[1m'
N='\033[0m'

ok()   { echo -e "${G}[OK]${N} $*"; }
fail() { echo -e "${R}[FAIL]${N} $*"; }
info() { echo -e "${C}...${N} $*"; }


echo -e "\n${B}=== BUILD CFGFlattening ===${N}"

rm -rf "${REPO_ROOT}/build"
mkdir -p "${REPO_ROOT}/build"

pushd "${REPO_ROOT}/build" > /dev/null

cmake \
    -DOBF_LLVM_INSTALL_DIR=/usr/lib/llvm-${LLVM_VER} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
    -DCMAKE_C_COMPILER=clang-${LLVM_VER} \
    -DCMAKE_CXX_COMPILER=clang++-${LLVM_VER} \
    .. -G Ninja

cmake --build .

popd > /dev/null

if [[ ! -f "$PLUGIN" ]]; then
    fail "Plugin not found: $PLUGIN"
    exit 1
fi

ok "Plugin build complete"


SRC_FILES=()

while IFS= read -r -d $'\0' f; do
    SRC_FILES+=("$f")
done < <(
    find "${SRC_DIRS[@]}" \
        -type f \( -name "*.c" -o -name "*.cpp" \) \
        -print0 | sort -z
)

if [[ ${#SRC_FILES[@]} -eq 0 ]]; then
    fail "No source files found"
    exit 1
fi

rm -rf "$OUT_DIR"


for src in "${SRC_FILES[@]}"; do

    ext="${src##*.}"
    base="$(basename "$src" ".${ext}")"

    cc="$CC_C"
    [[ "$ext" == "cpp" ]] && cc="$CC_CPP"

    dir="${OUT_DIR}/${base}"
    mkdir -p "$dir"

    echo -e "\n${B}=== ${base} ===${N}"

    for opt in "${OPTS[@]}"; do

        echo -e "\n  ${C}[${opt}]${N}"

        for disp in "${DISPATCHERS[@]}"; do

            label="${base}_${opt}_${disp}"

            ll="${dir}/${label}.ll"
            bin="${dir}/${label}.bin"

            info "$label"

            PLUGIN_FLAGS=()
            DISP_ENV=()

            if [[ "$disp" != "nopass" ]]; then
                PLUGIN_FLAGS+=("-fpass-plugin=${PLUGIN}")
                DISP_ENV=("CFF_DISPATCHER=${disp}")
            fi


            if ! env "${DISP_ENV[@]}" \
                "$cc" \
                "-${opt}" \
                "${PLUGIN_FLAGS[@]}" \
                -S -emit-llvm \
                "$src" \
                -o "$ll"; then

                fail "IR generation failed: $label"
                continue
            fi


            if ! env "${DISP_ENV[@]}" \
                "$cc" \
                "-${opt}" \
                "${PLUGIN_FLAGS[@]}" \
                "$src" \
                -o "$bin" \
                -lm; then

                fail "Binary build failed: $label"
                continue
            fi

            ll_lines=$(wc -l < "$ll")
            bin_bytes=$(stat -c%s "$bin")

            echo "      IR: ${ll_lines} lines | bin: ${bin_bytes} bytes"

        done
    done
done

echo ""
ok "Experiment artifacts generated in ${OUT_DIR}"