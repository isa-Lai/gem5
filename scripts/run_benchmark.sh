#!/usr/bin/env bash
# Run an InterStellar 2.0 benchmark through the migrated gem5 + Ramulator.
#
# Usage:
#   scripts/run_benchmark.sh <bench> [N] [variant]
#     bench   : Polybench kernel (atax, bicg, daxpy, ddot, gemm, ...)
#     N       : number of CPUs / PEs (default 8; the paper also uses 1)
#     variant : build-variant subdir under <bench>/build/ (default Ex_v2_s1/O3/P0)
#
# Examples:
#   scripts/run_benchmark.sh atax        # 8-core atax (N=724 working set)
#   scripts/run_benchmark.sh atax 1      # single-core
#   scripts/run_benchmark.sh bicg
#
# Environment overrides:
#   GEM5_OPT  path to gem5.opt          (default build/RISCV/gem5.opt)
#   BMS_ROOT  benchmarks tree            (default ../fanosgem5/BMs)
#   CFG       Ramulator .cfg             (default configs/ramulator/DDR4-config-Gen.cfg)
#   OUTDIR    m5out directory            (default BM/<bench>/out/map2/N<N>)
#
# NOTE: requires the migrated InterStellar engine + Ramulator + configs
# (Phases 2-5). Until then se.py rejects --mem-type=Ramulator / --meta-isa-type.
set -euo pipefail

cd "$(dirname "$0")/.."

BENCH="${1:?usage: $0 <bench> [N] [variant]}"
N="${2:-8}"
VARIANT="${3:-Ex_v2_s1/O3/P0}"

GEM5_OPT="${GEM5_OPT:-build/RISCV/gem5.opt}"
BMS_ROOT="${BMS_ROOT:-../fanosgem5/BMs}"
CFG="${CFG:-configs/ramulator/DDR4-config-Gen.cfg}"
OUTDIR="${OUTDIR:-BM/${BENCH}/out/map2/N${N}}"

[ -x "$GEM5_OPT" ] || { echo "ERROR: $GEM5_OPT not built. Run scripts/build_gem5.sh first." >&2; exit 1; }
[ -f "$CFG" ]      || { echo "ERROR: $CFG not found." >&2; exit 1; }

# Locate the prebuilt .riscv binary for this kernel/variant.
SEARCH="${BMS_ROOT}/Polybench/${BENCH}/build/${VARIANT}"
BIN="$(find "$SEARCH" -name "${BENCH}*.riscv" 2>/dev/null | head -1)"
if [ -z "$BIN" ]; then
    echo "ERROR: no ${BENCH}*.riscv under $SEARCH" >&2
    echo "       Set BMS_ROOT or pass a different variant (3rd arg)." >&2
    exit 1
fi
echo ">> binary: $BIN"

# --cmd must list exactly N binaries (one per -n CPU), semicolon-separated.
CMD=""
i=0
while [ "$i" -lt "$N" ]; do CMD+="${BIN};"; i=$((i+1)); done
CMD="${CMD%;}"

echo ">> outdir: $OUTDIR  (cpus=${N})"
mkdir -p "$OUTDIR"

# Paper config: RISC-V OoO @ 2.4 GHz; L1 64KiB/2; L2 512KiB/4; L3 2MiB/8; DDR4 via Ramulator.
"$GEM5_OPT" --outdir="$OUTDIR" configs/example/se.py \
    -n "$N" \
    --cpu-type=DerivO3CPU --cpu-clock=2400MHz --sys-clock=2400MHz \
    --caches \
        --l1d_size=64kB --l1d_assoc=2 --l1d_mshrs=16 \
        --l1i_size=64kB --l1i_assoc=2 --l1i_mshrs=16 \
    --l2cache --l2_size=512kB --l2_assoc=4 --l2_mshrs=32 \
    --l3cache --l3_size=2MB  --l3_assoc=8 --l3_mshrs=64 \
    --mem-type=Ramulator \
    --ramulator-config="$CFG" \
    --meta-isa-type=IPP \
    --cmd "$CMD"

echo ">> Done. Stats: ${OUTDIR}/stats.txt"
