#!/bin/bash
# ==============================================================================
# COMPASS v2 / InterStellar 2.0 - Workload Compilation Pipeline
# Uses custom LLVM Clang compiler to build RISC-V 64-bit static binaries.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEM5_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LLVM_BIN="/home/laiy24/interstelar/llvm-project/build/bin"
CLANG="${LLVM_BIN}/clang"

if [ ! -f "${CLANG}" ]; then
    echo "❌ Error: Custom clang not found at ${CLANG}"
    exit 1
fi

echo "=========================================================="
echo "Compiling COMPASS v2 Workloads using Custom LLVM"
echo "Clang: $(${CLANG} --version | head -n 1)"
echo "=========================================================="

CFLAGS="--target=riscv64-unknown-linux-gnu -march=rv64gc --sysroot=/usr/riscv64-linux-gnu --gcc-toolchain=/usr -O3 -static"

WORKLOADS=(
    "BM/daxpy_large/daxpy_large.c:BM/daxpy_large/daxpy_large.riscv"
    "BM/kv_cache/kv_cache.c:BM/kv_cache/kv_cache.riscv"
    "BM/dlrm_gather/dlrm_gather.c:BM/dlrm_gather/dlrm_gather.riscv"
)

for entry in "${WORKLOADS[@]}"; do
    SRC="${GEM5_ROOT}/${entry%%:*}"
    OUT="${GEM5_ROOT}/${entry##*:}"

    echo "Compiling ${SRC} -> ${OUT}..."
    ${CLANG} ${CFLAGS} "${SRC}" -o "${OUT}"

    if [ -f "${OUT}" ] && [ -s "${OUT}" ]; then
        echo "✅ Successfully built: $(basename "${OUT}") ($(stat -c%s "${OUT}") bytes)"
    else
        echo "❌ Build failed for ${OUT}"
        exit 1
    fi
done

echo "=========================================================="
echo "All workloads compiled successfully with custom LLVM!"
echo "=========================================================="
