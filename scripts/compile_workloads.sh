#!/bin/bash
# ==============================================================================
# COMPASS v2 / InterStellar 2.0 - EXP workload compilation pipeline
# ==============================================================================
#
# Builds all benchmark workloads as static RISC-V binaries with the project's
# custom LLVM/Clang (InterStellar pass -> paired csrrw descriptor writes).
#
# WORKLOAD PROVENANCE
# -------------------
# * PolyBench kernels (28): PolyBench/C 4.2 upstream
#   https://sourceforge.net/projects/polybench/
#   vendored here as ANNOTATED v2 sources under BMs/Polybench/<k>/src/<k>_v2.c
#   (InterStellar descriptor annotations added by the InterStellar 2.0 project;
#    some kernels such as knn*, scusum*, syn*, memcpy, dscal* are custom
#    additions following the polybench v2 layout).  DO NOT edit src/ files.
# * Custom BM kernels (3): local, under gem5/BM/{daxpy_large,kv_cache,dlrm_gather}.
# * Toolchain: custom LLVM/Clang (llvm-project/build/bin/clang), target
#   riscv64-unknown-linux-gnu, march=rv64gc, -mno-relax, -O3, static.
# * Shared headers (include root BMs/Polybench/include_exp/, safe copies):
#     - polybench.h : compiled_benchmarks/polybench.h + minimal missing pieces
#                     (POLYBENCH_1D/2D/3D/4D prototype macros, 3D_ARRAY_DECL,
#                      POLYBENCH_ARRAY, no-op timing instruments, prevent_dce)
#     - encoding.h  : stub + write_csr() csrrw macro (as in build_benchmark.sh)
#     - gem5/m5ops.h: no-op macros for m5_reset_stats/checkpoint/work_begin
#   metaisa.hpp (descriptor ABI) is intentionally NOT in the include root; it
#   must resolve to the per-kernel src/ copy.
#
# DATASET SIZES
# -------------
# The v2 kernel headers have their MINI/SMALL/MEDIUM/LARGE/EXTRALARGE tables
# commented out, so dimensions are passed EXPLICITLY with -D flags.  EXTRALARGE
# table values are used when their footprint lands in [4 MiB, 64 MiB]
# (thrash the 2 MiB L3) and the run stays within ~200M instructions; otherwise
# the dominant dimension is scaled (see per-kernel comments below).  Time-step
# counts (TSTEPS/TMAX) are hard-coded in some headers and cannot be overridden.
#
# Output: <kernel>/build/EXP/<kernel>_exp.riscv   (new dir; existing
#         build/Ex_v2_s1 artifacts are untouched).  Idempotent.
#
# Per-kernel error tolerance: a failed kernel logs and continues.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEM5_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PB_ROOT="${GEM5_ROOT}/BMs/Polybench"
INC_EXP="${PB_ROOT}/include_exp"
CLANG="/home/laiy24/interstelar/llvm-project/build/bin/clang"

MAX_BIN_BYTES=209715200   # ~200 MiB single-binary guard

if [ ! -x "${CLANG}" ]; then
    echo "ERROR: custom clang not found/executable at ${CLANG}" >&2
    exit 1
fi
if [ ! -f "${INC_EXP}/polybench.h" ] || [ ! -f "${INC_EXP}/encoding.h" ] || \
   [ ! -f "${INC_EXP}/gem5/m5ops.h" ]; then
    echo "ERROR: include_exp stub root missing under ${INC_EXP}" >&2
    exit 1
fi

BASE_FLAGS=(--target=riscv64-unknown-linux-gnu -march=rv64gc -mno-relax -O3
            -static --sysroot=/usr/riscv64-linux-gnu --gcc-toolchain=/usr)

# One entry per workload: name|source|outdir|extra -D flags
KERNELS=(
    # ---- annotated PolyBench v2 (dims from EXTRALARGE table, or scaled) ----
    "atax|${PB_ROOT}/atax/src/atax_v2.c|${PB_ROOT}/atax/build/EXP|-DM=1800 -DN=2200"
    "bicg|${PB_ROOT}/bicg/src/bicg_v2.c|${PB_ROOT}/bicg/build/EXP|-DM=1800 -DN=2200"
    "daxpy|${PB_ROOT}/daxpy/src/daxpy_v2.c|${PB_ROOT}/daxpy/build/EXP|-DN=2000000"
    "daxpys|${PB_ROOT}/daxpys/src/daxpys_v2.c|${PB_ROOT}/daxpys/build/EXP|-DSIZE=2000000 -DSTRIDE=1"
    "ddot|${PB_ROOT}/ddot/src/ddot_v2.c|${PB_ROOT}/ddot/build/EXP|-DN=2000000"
    "ddots|${PB_ROOT}/ddots/src/ddots_v2.c|${PB_ROOT}/ddots/build/EXP|-DSIZE=2000000 -DSTRIDE=1"
    "doitgen|${PB_ROOT}/doitgen/src/doitgen_v2.c|${PB_ROOT}/doitgen/build/EXP|-DNR=192 -DNQ=192 -DNP=16"
    "dscal|${PB_ROOT}/dscal/src/dscal_v2.c|${PB_ROOT}/dscal/build/EXP|-DN=2000000"
    "dscals|${PB_ROOT}/dscals/src/dscals_v2.c|${PB_ROOT}/dscals/build/EXP|-DSIZE=2000000 -DSTRIDE=1"
    "fdtd-2d|${PB_ROOT}/fdtd-2d/src/fdtd-2d_v2.c|${PB_ROOT}/fdtd-2d/build/EXP|-DNX=500 -DNY=400"
    "floyd|${PB_ROOT}/floyd/src/floyd_v2.c|${PB_ROOT}/floyd/build/EXP|-DN=260"
    # gemm_v2.c aliases NI->NP, NJ->NQ, NK->NR, so dims must be given as NP/NQ/NR
    "gemm|${PB_ROOT}/gemm/src/gemm_v2.c|${PB_ROOT}/gemm/build/EXP|-DNP=250 -DNQ=250 -DNR=250"
    "gemver|${PB_ROOT}/gemver/src/gemver_v2.c|${PB_ROOT}/gemver/build/EXP|-DN=1800"
    "gesummv|${PB_ROOT}/gesummv/src/gesummv_v2.c|${PB_ROOT}/gesummv/build/EXP|-DN=1800"
    "heat-3d|${PB_ROOT}/heat-3d/src/heat-3d_v2.c|${PB_ROOT}/heat-3d/build/EXP|-DN=64"
    "heat-3d-5s|${PB_ROOT}/heat-3d-5s/src/heat-3d-5s_v2.c|${PB_ROOT}/heat-3d-5s/build/EXP|-DN=64"
    "jacobi-1d|${PB_ROOT}/jacobi-1d/src/jacobi-1d_v2.c|${PB_ROOT}/jacobi-1d/build/EXP|-DN=300000"
    "jacobi-2d|${PB_ROOT}/jacobi-2d/src/jacobi-2d_v2.c|${PB_ROOT}/jacobi-2d/build/EXP|-DN=760"
    "knn1d|${PB_ROOT}/knn1d/src/knn1d_v2.c|${PB_ROOT}/knn1d/build/EXP|-DN=200000"
    "knn2d|${PB_ROOT}/knn2d/src/knn2d_v2.c|${PB_ROOT}/knn2d/build/EXP|-DN=100000"
    "knn3d|${PB_ROOT}/knn3d/src/knn3d_v2.c|${PB_ROOT}/knn3d/build/EXP|-DN=100000"
    "memcpy|${PB_ROOT}/memcpy/src/memcpy_v2.c|${PB_ROOT}/memcpy/build/EXP|-DN=4000000"
    "mvt|${PB_ROOT}/mvt/src/mvt_v2.c|${PB_ROOT}/mvt/build/EXP|-DN=2000"
    "scusumkbn|${PB_ROOT}/scusumkbn/src/scusumkbn_v2.c|${PB_ROOT}/scusumkbn/build/EXP|-DN=2000000"
    "scusumkbns|${PB_ROOT}/scusumkbns/src/scusumkbns_v2.c|${PB_ROOT}/scusumkbns/build/EXP|-DSIZE=2000000 -DSTRIDE=1"
    "seidel-2d|${PB_ROOT}/seidel-2d/src/seidel-2d_v2.c|${PB_ROOT}/seidel-2d/build/EXP|-DN=700"
    "syn7s|${PB_ROOT}/syn7s/src/syn7s_v2.c|${PB_ROOT}/syn7s/build/EXP|-DM=800 -DN=800"
    "synind|${PB_ROOT}/synind/src/synind_v2.c|${PB_ROOT}/synind/build/EXP|-DN=800"
    # ---- custom BM kernels (self-contained, no -D needed) ----
    "daxpy_large|${GEM5_ROOT}/BM/daxpy_large/daxpy_large.c|${GEM5_ROOT}/BM/daxpy_large/build/EXP|"
    "kv_cache|${GEM5_ROOT}/BM/kv_cache/kv_cache.c|${GEM5_ROOT}/BM/kv_cache/build/EXP|"
    "dlrm_gather|${GEM5_ROOT}/BM/dlrm_gather/dlrm_gather.c|${GEM5_ROOT}/BM/dlrm_gather/build/EXP|"
)

echo "=========================================================="
echo "COMPASS EXP workload compilation (custom LLVM)"
echo "Clang: $(${CLANG} --version | head -n 1)"
echo "=========================================================="

FAILED=0
FAILED_LIST=""
for entry in "${KERNELS[@]}"; do
    IFS='|' read -r NAME SRC OUTDIR EXTRA <<< "${entry}"
    OUT="${OUTDIR}/${NAME}_exp.riscv"
    SRC_DIR="$(dirname "${SRC}")"

    echo ""
    echo "==> [${NAME}] ${SRC}"
    if [ ! -f "${SRC}" ]; then
        echo "   FAIL ${NAME}: source not found"
        FAILED=$((FAILED + 1)); FAILED_LIST="${FAILED_LIST} ${NAME}"
        continue
    fi
    mkdir -p "${OUTDIR}"
    rm -f "${OUT}"

    read -ra EXTRA_ARGS <<< "${EXTRA}"
    LOG="${OUTDIR}/compile.log"
    if ! "${CLANG}" "${BASE_FLAGS[@]}" \
         -I"${SRC_DIR}" -I"${INC_EXP}" \
         "${EXTRA_ARGS[@]}" "${SRC}" -o "${OUT}" -lm \
         >"${LOG}" 2>&1; then
        echo "   FAIL ${NAME}: compile error (see ${LOG})"
        tail -n 15 "${LOG}" | sed 's/^/   | /'
        FAILED=$((FAILED + 1)); FAILED_LIST="${FAILED_LIST} ${NAME}"
        continue
    fi

    SIZE=$(stat -c%s "${OUT}")
    if [ "${SIZE}" -gt "${MAX_BIN_BYTES}" ]; then
        echo "   FAIL ${NAME}: binary ${SIZE} bytes exceeds ${MAX_BIN_BYTES} guard"
        FAILED=$((FAILED + 1)); FAILED_LIST="${FAILED_LIST} ${NAME}"
        continue
    fi
    printf "   OK   %-12s %10d bytes   flags: %s\n" "${NAME}" "${SIZE}" "${EXTRA:-<none>}"
done

echo ""
echo "=========================================================="
echo "Compile summary: $((${#KERNELS[@]} - FAILED))/${#KERNELS[@]} OK"
if [ "${FAILED}" -gt 0 ]; then
    echo "Failed kernels:${FAILED_LIST}"
fi
echo "=========================================================="
# Non-zero exit only if everything failed; partial success is reported so the
# loop caller can inspect per-kernel OK/FAIL lines above.
[ "${FAILED}" -eq 0 ]
