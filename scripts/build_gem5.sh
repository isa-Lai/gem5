#!/usr/bin/env bash
# Build the InterStellar-enabled gem5 (RISC-V target).
#
# Usage:
#   scripts/build_gem5.sh            # optimized build (build/RISCV/gem5.opt)
#   scripts/build_gem5.sh debug      # debug build  (build/RISCV/gem5.debug)
#   JOBS=8 scripts/build_gem5.sh     # override parallelism
#
# The debug build is required for --debug-flags (InterStellar custom flags).
set -euo pipefail

# Always run from the gem5 repo root, regardless of the caller's CWD.
cd "$(dirname "$0")/.."

JOBS="${JOBS:-$(nproc)}"
MODE="${1:-opt}"

case "$MODE" in
    opt)   target="build/RISCV/gem5.opt" ;;
    debug) target="build/RISCV/gem5.debug" ;;
    *) echo "usage: $0 [opt|debug]" >&2; exit 1 ;;
esac

if ! command -v clang >/dev/null 2>&1; then
    echo "ERROR: clang is required by gem5's SCons. Install: sudo apt install clang lld" >&2
    exit 1
fi

echo ">> Building $target with -j${JOBS} ..."
# Fall back to invoking scons through python3 in case 'scons' picks the wrong interpreter.
if scons --version >/dev/null 2>&1; then
    scons "$target" -j"${JOBS}"
else
    /usr/bin/env python3 "$(command -v scons)" "$target" -j"${JOBS}"
fi
echo ">> Done: $target"
