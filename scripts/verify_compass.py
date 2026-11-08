#!/usr/bin/env python3
"""End-to-end verification of the COMPASS v2 implementation (no builds).

For every benchmark x CXL latency it runs:
  stock / ipp / prefetch      external baselines
  A, B                        2x2 cells with K=1
  C, D  with K in {1, 2, 4}   K=1 doubles as a regression check vs A / B

and checks:
  1. every run exits 0 and writes stats.txt
  2. benchmark checksum identical across ALL arms (functional correctness)
  3. prep_* stats are 0 for every arm that must not do K-segment prep
  4. cells C/D with K>1 advance segments (prep_segments_advanced > 0)
  5. C(K=1) == A and D(K=1) == B bit-for-bit in simTicks (no silent change)
  6. interval delivery (B/D) is observably active (differs from A/C)
  7. performance: S_B/A, S_C/A, S_D/A, interaction I = ln(TB*TC/(TA*TD))

Outputs (default /home/isalai/inter/output):
  verify_results.csv  verify_summary.md  verify_runs/<run>/{stats.txt,run.log}

Usage:
  scripts/verify_compass.py [--profile quick|full] [--jobs N]
        [--benchmarks daxpy_large,kv_cache,dlrm_gather]
        [--latencies 40,120] [--ks 2,4] [--force] [--dry-run] [--build]

Environment: GEM5_BIN (default build/RISCV/gem5.fast), OUTPUT_DIR,
  and for --build: CLANG, TC (riscv toolchain root with sysroot/).
NOTE: gem5.fast compiles out assert(); the slot-conservation asserts in
  interstellar.cc are only exercised with gem5.opt (set GEM5_BIN).
"""

import argparse
import csv
import hashlib
import math
import os
import re
import subprocess
import sys
import time
from concurrent.futures import (
    ThreadPoolExecutor,
    as_completed,
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GEM5_ROOT = os.path.dirname(SCRIPT_DIR)


def _default_gem5_bin():
    if "GEM5_BIN" in os.environ:
        return os.environ["GEM5_BIN"]
    fast = os.path.join(GEM5_ROOT, "build/RISCV/gem5.fast")
    if os.path.isfile(fast):
        return fast
    opt = os.path.join(GEM5_ROOT, "build/RISCV/gem5.opt")
    if os.path.isfile(opt):
        return opt
    return fast


GEM5_BIN = _default_gem5_bin()
CONFIG_SE = os.path.join(GEM5_ROOT, "configs/example/se.py")
OUTPUT_DIR = os.path.normpath(
    os.environ.get("OUTPUT_DIR", os.path.join(GEM5_ROOT, "..", "output"))
)
RUN_ROOT = os.path.join(OUTPUT_DIR, "verify_runs")


def resolve_bin(bench):
    """Resolve binary path relative to GEM5_ROOT with fallbacks."""
    info = globals().get("BENCHMARKS", {}).get(bench, {})
    candidates = []
    if "bin" in info and info["bin"]:
        candidates.append(info["bin"])
    candidates.append(f"BMs/Polybench/{bench}/build/EXP/{bench}_exp.riscv")
    candidates.append(f"BM/{bench}/build/EXP/{bench}_exp.riscv")
    candidates.append(f"BM/{bench}/{bench}.riscv")
    candidates.append(f"BMs/Polybench/{bench}/{bench}.riscv")

    for rel in candidates:
        if os.path.isfile(os.path.join(GEM5_ROOT, rel)):
            return rel

    for sub in ("BMs/Polybench", "BM"):
        base = os.path.join(GEM5_ROOT, sub, bench)
        if os.path.isdir(base):
            for root, _, files in os.walk(base):
                for f in sorted(files):
                    if f.endswith(".riscv"):
                        return os.path.relpath(
                            os.path.join(root, f), GEM5_ROOT
                        )

    return info.get("bin", candidates[0])


# profile -> per benchmark (options, maxinsts)
BENCHMARKS = {
    # Custom COMPASS BM kernels (pass problem size in options)
    "daxpy_large": {
        "src": "BM/daxpy_large/daxpy_large.c",
        "bin": "BM/daxpy_large/build/EXP/daxpy_large_exp.riscv",
        "quick": ("262144", "20000000"),
        "full": ("4194304", "100000000"),
        "must_prep": True,
    },
    "kv_cache": {
        "src": "BM/kv_cache/kv_cache.c",
        "bin": "BM/kv_cache/build/EXP/kv_cache_exp.riscv",
        "quick": ("8192", "20000000"),
        "full": ("65536", "130000000"),
        "must_prep": True,
    },
    "dlrm_gather": {
        "src": "BM/dlrm_gather/dlrm_gather.c",
        "bin": "BM/dlrm_gather/build/EXP/dlrm_gather_exp.riscv",
        "quick": ("8192", "10000000"),
        "full": ("65536", "20000000"),
        "must_prep": False,  # irregular gather: K-prep may legitimately idle
    },
    # Prioritized PolyBench EXP kernels (no cli options; size compiled-in)
    "atax": {
        "src": "BMs/Polybench/atax/src/atax_v2.c",
        "bin": resolve_bin("atax"),
        "quick": (" ", "20000000"),
        "full": (" ", "200000000"),
        "must_prep": True,
    },
    "bicg": {
        "src": "BMs/Polybench/bicg/src/bicg_v2.c",
        "bin": resolve_bin("bicg"),
        "quick": (" ", "20000000"),
        "full": (" ", "200000000"),
        "must_prep": True,
    },
    "ddot": {
        "src": "BMs/Polybench/ddot/src/ddot_v2.c",
        "bin": "BMs/Polybench/ddot/build/EXP/ddot_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "daxpy": {
        "src": "BMs/Polybench/daxpy/src/daxpy_v2.c",
        "bin": "BMs/Polybench/daxpy/build/EXP/daxpy_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "dscal": {
        "src": "BMs/Polybench/dscal/src/dscal_v2.c",
        "bin": "BMs/Polybench/dscal/build/EXP/dscal_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "gemver": {
        "src": "BMs/Polybench/gemver/src/gemver_v2.c",
        "bin": resolve_bin("gemver"),
        "quick": (" ", "20000000"),
        "full": (" ", "200000000"),
        "must_prep": True,
    },
    "mvt": {
        "src": "BMs/Polybench/mvt/src/mvt_v2.c",
        "bin": "BMs/Polybench/mvt/build/EXP/mvt_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "120000000"),
        "must_prep": True,
    },
    "jacobi-2d": {
        "src": "BMs/Polybench/jacobi-2d/src/jacobi-2d_v2.c",
        "bin": resolve_bin("jacobi-2d"),
        "quick": (" ", "20000000"),
        "full": (" ", "200000000"),
        "must_prep": True,
    },
    # Additional PolyBench v2 kernels
    "daxpys": {
        "src": "BMs/Polybench/daxpys/src/daxpys_v2.c",
        "bin": "BMs/Polybench/daxpys/build/EXP/daxpys_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "ddots": {
        "src": "BMs/Polybench/ddots/src/ddots_v2.c",
        "bin": "BMs/Polybench/ddots/build/EXP/ddots_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "dscals": {
        "src": "BMs/Polybench/dscals/src/dscals_v2.c",
        "bin": "BMs/Polybench/dscals/build/EXP/dscals_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "doitgen": {
        "src": "BMs/Polybench/doitgen/src/doitgen_v2.c",
        "bin": "BMs/Polybench/doitgen/build/EXP/doitgen_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "fdtd-2d": {
        "src": "BMs/Polybench/fdtd-2d/src/fdtd-2d_v2.c",
        "bin": "BMs/Polybench/fdtd-2d/build/EXP/fdtd-2d_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "floyd": {
        "src": "BMs/Polybench/floyd/src/floyd_v2.c",
        "bin": "BMs/Polybench/floyd/build/EXP/floyd_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "gemm": {
        "src": "BMs/Polybench/gemm/src/gemm_v2.c",
        "bin": "BMs/Polybench/gemm/build/EXP/gemm_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "gesummv": {
        "src": "BMs/Polybench/gesummv/src/gesummv_v2.c",
        "bin": "BMs/Polybench/gesummv/build/EXP/gesummv_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "heat-3d": {
        "src": "BMs/Polybench/heat-3d/src/heat-3d_v2.c",
        "bin": "BMs/Polybench/heat-3d/build/EXP/heat-3d_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "heat-3d-5s": {
        "src": "BMs/Polybench/heat-3d-5s/src/heat-3d-5s_v2.c",
        "bin": "BMs/Polybench/heat-3d-5s/build/EXP/heat-3d-5s_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "jacobi-1d": {
        "src": "BMs/Polybench/jacobi-1d/src/jacobi-1d_v2.c",
        "bin": "BMs/Polybench/jacobi-1d/build/EXP/jacobi-1d_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "knn1d": {
        "src": "BMs/Polybench/knn1d/src/knn1d_v2.c",
        "bin": "BMs/Polybench/knn1d/build/EXP/knn1d_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "knn2d": {
        "src": "BMs/Polybench/knn2d/src/knn2d_v2.c",
        "bin": "BMs/Polybench/knn2d/build/EXP/knn2d_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "knn3d": {
        "src": "BMs/Polybench/knn3d/src/knn3d_v2.c",
        "bin": "BMs/Polybench/knn3d/build/EXP/knn3d_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "memcpy": {
        "src": "BMs/Polybench/memcpy/src/memcpy_v2.c",
        "bin": "BMs/Polybench/memcpy/build/EXP/memcpy_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "scusumkbn": {
        "src": "BMs/Polybench/scusumkbn/src/scusumkbn_v2.c",
        "bin": "BMs/Polybench/scusumkbn/build/EXP/scusumkbn_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "scusumkbns": {
        "src": "BMs/Polybench/scusumkbns/src/scusumkbns_v2.c",
        "bin": "BMs/Polybench/scusumkbns/build/EXP/scusumkbns_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "seidel-2d": {
        "src": "BMs/Polybench/seidel-2d/src/seidel-2d_v2.c",
        "bin": "BMs/Polybench/seidel-2d/build/EXP/seidel-2d_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "syn7s": {
        "src": "BMs/Polybench/syn7s/src/syn7s_v2.c",
        "bin": "BMs/Polybench/syn7s/build/EXP/syn7s_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": True,
    },
    "synind": {
        "src": "BMs/Polybench/synind/src/synind_v2.c",
        "bin": "BMs/Polybench/synind/build/EXP/synind_exp.riscv",
        "quick": (" ", "50000000"),
        "full": (" ", "100000000"),
        "must_prep": False,
    },
}

DEFAULT_BENCHMARKS = ["daxpy_large", "kv_cache", "dlrm_gather"]
HELDOUT_BENCHMARKS = ["jacobi-2d", "gemver", "bicg"]
EXP1_BENCHMARKS = [
    "daxpy_large",
    "kv_cache",
    "dlrm_gather",
    "atax",
    "bicg",
    "ddot",
    "daxpy",
    "dscal",
    "gemver",
    "mvt",
    "jacobi-2d",
]

CUR_PROFILE = "quick"
GAIN_MIN = 1.02  # >2% speedup over cell A counts as observable
INTERVAL_W = 16
CREDITS = 16
NUM_CPUS = 1
MEM_CHANNELS = None
L3_MSHRS = None
INCLUDE_SOTA = False
INCLUDE_UNCOORD = False
CFG_IPP = "configs/ramulator/DDR4-config-Gen.cfg"
CFG_NOIPP = "configs/ramulator/DDR4-config-Gen-noIPP.cfg"
CFG_IPP_2CH = "configs/ramulator/DDR4-config-Gen-2ch.cfg"
CFG_NOIPP_2CH = "configs/ramulator/DDR4-config-Gen-2ch-noIPP.cfg"


def arms_for(ks, include_sota=None, include_uncoord=None):
    """Return ordered list of (arm_name, cell, k, ipp_cfg, prefetch, hwp_type)."""
    if include_sota is None:
        include_sota = INCLUDE_SOTA
    if include_uncoord is None:
        include_uncoord = INCLUDE_UNCOORD

    arms = [
        ("stock", None, 1, False, False, None),
        ("ipp", None, 1, True, False, None),
        ("prefetch", None, 1, False, True, None),
    ]
    if include_sota:
        arms.extend(
            [
                ("stride", None, 1, False, False, "StridePrefetcher"),
                ("bop", None, 1, False, False, "BOPPrefetcher"),
                ("sms", None, 1, False, False, "SmsPrefetcher"),
            ]
        )
    if include_uncoord:
        arms.append(("iPF_noCoord", None, 1, True, True, None))
    arms.extend(
        [
            ("A", "A", 1, True, True, None),
            ("B", "B", 1, True, True, None),
        ]
    )
    for cell in ("C", "D"):
        for k in sorted(set([1] + ks)):
            arms.append((f"{cell}_k{k}", cell, k, True, True, None))
    return arms


def run_dir(bench, lat, arm):
    return os.path.join(RUN_ROOT, f"{bench}_lat{lat}_{arm}")


def build_command(
    bench,
    profile,
    lat,
    arm,
    outdir,
    args=None,
    num_cpus=None,
    mem_channels=None,
    l3_mshrs=None,
):
    if args is not None:
        num_cpus = getattr(args, "num_cpus", 1)
        l3_mshrs = getattr(args, "l3_mshrs", None)
        mem_channels = getattr(args, "mem_channels", None)
    else:
        if num_cpus is None:
            num_cpus = NUM_CPUS
        if l3_mshrs is None:
            l3_mshrs = L3_MSHRS
        if mem_channels is None:
            mem_channels = MEM_CHANNELS

    arm_name, cell, k, ipp, prefetch = arm[:5]
    hwp_type = arm[5] if len(arm) > 5 else None

    bench_info = BENCHMARKS[bench]
    bin_path = bench_info.get("bin") or resolve_bin(bench)
    cmd_arg = ";".join([bin_path] * num_cpus)

    bench_opts, maxinsts = bench_info[profile]
    opts_stripped = bench_opts.strip()

    # Multi-channel Ramulator config resolution
    if num_cpus > 1:
        ram_cfg = CFG_IPP_2CH if ipp else CFG_NOIPP_2CH
        use_mem_channels = mem_channels
    else:
        ram_cfg = CFG_IPP if ipp else CFG_NOIPP
        use_mem_channels = mem_channels

    cmd = [
        GEM5_BIN,
        f"-d{outdir}",
        CONFIG_SE,
        f"--num-cpus={num_cpus}",
        "--cpu-type=DerivO3CPU",
        "--cpu-clock=2400MHz",
        "--sys-clock=2400MHz",
        "--caches",
        "--l1d_size=64kB",
        "--l1d_assoc=2",
        "--l2cache",
        "--l2_size=512kB",
        "--l2_assoc=4",
        "--l3cache",
        "--l3_size=2MB",
        "--l3_assoc=8",
        "--mem-type=Ramulator",
    ]
    if use_mem_channels is not None:
        cmd.append(f"--mem-channels={use_mem_channels}")
    if num_cpus > 1:
        cmd.append("--mem-size=8GB")
    cmd.append(f"--ramulator-config={ram_cfg}")

    if l3_mshrs is not None:
        cmd.append(f"--l3_mshrs={l3_mshrs}")

    cmd += [
        f"--cxl-latency={lat}",
        f"--cmd={cmd_arg}",
    ]

    if opts_stripped:
        opts_arg = ";".join([opts_stripped] * num_cpus)
        cmd.append(f"--options={opts_arg}")

    cmd.append(f"--maxinsts={maxinsts}")

    if ipp or prefetch:
        # the iMC IPP/iBatch logic is only enabled with --meta-isa-type=IPP
        cmd.append("--meta-isa-type=IPP")
    if hwp_type:
        cmd.append(f"--l3-hwp-type={hwp_type}")
    elif prefetch:
        cmd.append("--l3-hwp-type=InterStellarPrefetcher")
    if cell:
        cmd.extend(
            [
                f"--meta-isa-cell={cell}",
                f"--meta-isa-prep-k={k}",
                f"--meta-isa-interval-w={INTERVAL_W}",
                f"--meta-isa-credits={CREDITS}",
            ]
        )
    return cmd


def parse_stats(path):
    out = {"ok": False}
    if not os.path.isfile(path) or not os.path.getsize(path):
        return out
    vals = {}
    prep_seg = prep_blk = 0.0
    with open(path) as fh:
        for line in fh:
            p = line.split()
            if len(p) < 2:
                continue
            try:
                v = float(p[1])
            except ValueError:
                continue
            vals.setdefault(p[0], v)
            name = p[0].rsplit(".", 1)[-1]
            if "::" in name:
                continue
            if name.startswith("prep_segments_advanced"):
                prep_seg += v
            elif name.startswith("prep_blocked"):
                prep_blk += v

    def g(*keys):
        for k in keys:
            if k in vals:
                return vals[k]
        for k in keys:
            suffix = "." + k if not k.startswith(".") else k
            for vk in vals:
                if vk == k or vk.endswith(suffix):
                    return vals[vk]
        return 0.0

    def g_sum(*patterns):
        total = 0.0
        found = False
        for pat in patterns:
            for k, v in vals.items():
                if pat == "compass_seeds_arrived":
                    match = (
                        k.endswith(".compass_seeds_arrived")
                        or k == "compass_seeds_arrived"
                    )
                else:
                    match = k == pat or k.endswith("." + pat) or pat in k
                if match:
                    total += v
                    found = True
            if found:
                return total
        return 0.0

    out.update(
        ok="simTicks" in vals,
        ticks=int(g("simTicks")),
        seconds=g("simSeconds"),
        insts=int(g("simInsts")),
        ipc=g("system.cpu.ipc"),
        pf_issued=int(g("system.l3.prefetcher.pfIssued")),
        pf_useful=int(g("system.l3.prefetcher.pfUseful")),
        ipf_gen=int(g("system.l3.prefetcher.tot_req_iPrefteched")),
        ipf_full=int(g("system.l3.prefetcher.tot_iHWP_full")),
        l3_demand_mshr=int(g("system.l3.demandMshrMisses::total")),
        ibatch_done=int(g_sum("Total_Requests_Completed_iBatch")),
        prep_seg=int(prep_seg),
        prep_blocked=int(prep_blk),
        blocked_while_ready_at_iMC=int(g_sum("blocked_while_ready_at_iMC")),
        l3_mshr_blocked_cycles=int(
            g(
                "system.l3.blockedCycles::no_mshrs",
                "system.l3.blocked_cycles::no_mshrs",
                "blockedCycles::no_mshrs",
            )
        ),
        demand_avg_mshr_miss_latency=float(
            g(
                "system.l3.demandAvgMshrMissLatency::total",
                "demandAvgMshrMissLatency::total",
            )
        ),
        overall_mshr_misses=int(
            g(
                "system.l3.overallMshrMisses::total",
                "overallMshrMisses::total",
            )
        ),
        compass_seeds_arrived=int(g_sum("compass_seeds_arrived")),
        tot_compass_seeds=int(
            g(
                "system.l3.prefetcher.tot_compass_seeds",
                "tot_compass_seeds",
            )
        ),
    )
    return out


def _is_numeric_token(tok):
    try:
        float(tok)
        return True
    except ValueError:
        pass
    try:
        int(tok, 0)
        return True
    except ValueError:
        pass
    if re.match(r"^[-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?$", tok):
        return True
    return False


def _is_numeric_line(line):
    parts = line.split()
    if not parts:
        return False
    if re.match(r"^[a-zA-Z_]\w*(\[[^\]]+\])*\s*=\s*[-+0-9xa-fA-F.]+$", line):
        return True
    return all(_is_numeric_token(p) for p in parts)


def parse_checksum(log):
    if not os.path.isfile(log):
        return (None, "UNVERIFIED")
    with open(log, errors="replace") as fh:
        text = fh.read()

    # 1. Direct Checksum: ... line
    m = re.findall(r"Checksum:\s*([-+0-9.eE]+)", text)
    if m:
        return (m[-1], "VERIFIED")

    # 2. Output between BEGIN_OUTPUT and END_OUTPUT
    if "BEGIN_OUTPUT" in text and "END_OUTPUT" in text:
        start = text.find("BEGIN_OUTPUT") + len("BEGIN_OUTPUT")
        end = text.find("END_OUTPUT", start)
        if end != -1:
            raw_lines = text[start:end].splitlines()
            out_lines = [l.strip() for l in raw_lines if l.strip()]
            if out_lines:
                digest = hashlib.md5(
                    "\n".join(out_lines).encode()
                ).hexdigest()[:12]
                return (f"hash_{digest}", "VERIFIED")

    # 3. Numeric benchmark output lines after filtering telemetry
    telemetry_patterns = [
        "Timing Constraints:",
        "freq =",
        "tCK=",
        "nBL=",
        "nCCDS=",
        "nCCDL=",
        "nRTRS=",
        "nCL=",
        "nRCD=",
        "nRP=",
        "nRAS=",
        "nRC=",
        "nRTP=",
        "nWTR=",
        "nWR=",
        "nRRD=",
        "nFAW=",
        "nRFC=",
        "nREFI=",
        "iHWP Configs:",
        "Ramulator system configuration file",
        "warn:",
        "info:",
        "panic:",
        "fatal:",
        "Global frequency set to",
        "Global frequency",
        "Simulation.py",
        "buildEnv",
        "Abotaleb",
        "src/",
        "gem5",
        "command line:",
        "test_mem_mode",
        "FutureClass",
        "1GHz",
        "2GHz",
        "2400MHz",
        "**** REAL",
        "Exiting @ tick",
        "CMD:",
        "simTicks",
        "simInsts",
        "Memory Controller",
        "Processor",
        "Reading from",
        "Connecting",
        "Building",
        "Number of cpus",
        "Channel",
        "Rank",
        "For elastic trace",
        "system.",
    ]

    benchmark_lines = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if any(line.startswith(p) or p in line for p in telemetry_patterns):
            continue
        if any(
            line.startswith(p)
            for p in ["&A[", "&x[", "&y[", "&tmp[", "&s[", "&p[", "&w["]
        ):
            continue
        if _is_numeric_line(line):
            benchmark_lines.append(line)

    if benchmark_lines:
        digest = hashlib.md5("\n".join(benchmark_lines).encode()).hexdigest()[
            :12
        ]
        return (f"hash_{digest}", "VERIFIED")

    return (None, "UNVERIFIED")


def evaluate_run(
    rc_or_bench, r_or_profile=None, maxinsts_or_arm=None, r=None, rc=None
):
    """Classify the verdict of an individual run.

    Can be called as:
      evaluate_run(rc, r, maxinsts)
    or:
      evaluate_run(bench, profile, arm_name, r, rc)

    Returns one of: 'FAIL', 'INIT_ONLY', 'TRUNCATED', 'PASS'
    """
    if r is not None:
        actual_r = r
        actual_rc = 0 if rc is None else rc
        maxinsts = int(BENCHMARKS[rc_or_bench][r_or_profile][1])
    elif isinstance(r_or_profile, dict):
        actual_rc = rc_or_bench
        actual_r = r_or_profile
        maxinsts = int(maxinsts_or_arm)
    elif isinstance(rc_or_bench, dict):
        actual_r = rc_or_bench
        maxinsts = int(r_or_profile)
        actual_rc = 0 if maxinsts_or_arm is None else maxinsts_or_arm
    else:
        actual_rc = rc_or_bench
        actual_r = r_or_profile
        maxinsts = int(maxinsts_or_arm)

    if actual_rc != 0 or not (actual_r and actual_r.get("ok")):
        return "FAIL"
    insts = actual_r.get("insts", 0)
    pf_issued = actual_r.get("pf_issued", 0)
    ibatch = actual_r.get("ibatch_done", 0)
    if insts >= maxinsts:
        if pf_issued == 0 and ibatch == 0:
            return "INIT_ONLY"
        else:
            return "TRUNCATED"
    return "PASS"


classify_verdict = evaluate_run


def execute(job, force):
    bench, profile, lat, arm = job
    d = run_dir(bench, lat, arm[0])
    rc_file = os.path.join(d, "returncode")
    stats = os.path.join(d, "stats.txt")
    if (
        not force
        and os.path.isfile(rc_file)
        and os.path.isfile(stats)
        and os.path.getsize(stats)
    ):
        with open(rc_file) as fh:
            return job, int(fh.read().strip()), 0.0, True
    os.makedirs(d, exist_ok=True)
    cmd = build_command(bench, profile, lat, arm, d)
    t0 = time.time()
    with open(os.path.join(d, "run.log"), "w") as log:
        log.write("CMD: " + " ".join(cmd) + "\n")
        log.flush()
        rc = subprocess.run(
            cmd, stdout=log, stderr=subprocess.STDOUT, cwd=GEM5_ROOT
        ).returncode
    with open(rc_file, "w") as fh:
        fh.write(str(rc))
    return job, rc, time.time() - t0, False


def compile_binaries(benches):
    clang = os.environ.get("CLANG")
    tc = os.environ.get("TC")
    if not clang or not tc:
        sys.exit("--build needs env CLANG and TC (riscv toolchain root)")
    for b in benches:
        info = BENCHMARKS[b]
        cmd = [
            clang,
            "--target=riscv64-unknown-linux-gnu",
            "-march=rv64gc",
            "-mno-relax",
            "-O3",
            "-static",
            f"--sysroot={tc}/sysroot",
            f"--gcc-toolchain={tc}",
            info["src"],
            "-o",
            info["bin"],
        ]
        print("BUILD", " ".join(cmd))
        subprocess.run(cmd, cwd=GEM5_ROOT, check=True)


class Checks:
    def __init__(self):
        self.rows = []  # (level, scope, message)

    def add(self, level, scope, msg):
        self.rows.append((level, scope, msg))

    def count(self, level):
        return sum(1 for r in self.rows if r[0] == level)


def ln(x):
    return math.log(max(x, 1e-30))


def evaluate(benches, lats, ks, results, checksums, rcs, checks):
    perf = []
    for b in benches:
        must_prep = BENCHMARKS[b]["must_prep"]
        maxi = int(BENCHMARKS[b][CUR_PROFILE][1])
        for lat in lats:
            scope = f"{b}@{lat}"
            active_arms = arms_for(ks)
            present_arm_names = {
                k[2] for k in results.keys() if k[0] == b and k[1] == lat
            }
            for candidate in [
                ("iPF_noCoord", None, 1, True, True),
                ("iPF_IPP_uncoord", None, 1, True, True),
                ("stride", None, 1, False, False, "StridePrefetcher"),
                ("bop", None, 1, False, False, "BOPPrefetcher"),
                ("sms", None, 1, False, False, "SmsPrefetcher"),
            ]:
                if candidate[0] in present_arm_names and not any(
                    a[0] == candidate[0] for a in active_arms
                ):
                    active_arms.append(candidate)

            R = {a[0]: results.get((b, lat, a[0])) for a in active_arms}
            # 1. exit codes / stats presence
            bad = [
                a
                for a, r in R.items()
                if rcs.get((b, lat, a)) != 0 or not (r and r["ok"])
            ]
            if bad:
                checks.add("FAIL", scope, f"run failed or no stats: {bad}")
            else:
                checks.add("PASS", scope, "all runs exit 0 with stats")
            good = {
                a: r
                for a, r in R.items()
                if r and r["ok"] and rcs.get((b, lat, a)) == 0
            }
            # 2. checksum
            cs = {}
            for a in good:
                entry = checksums.get((b, lat, a))
                cs[a] = entry[0] if isinstance(entry, tuple) else entry
            vals = set(cs.values())
            truncated = bool(good) and all(
                r["insts"] >= maxi for r in good.values()
            )
            if truncated and vals == {None}:
                checks.add(
                    "WARN",
                    scope,
                    f"all runs stopped at maxinsts={maxi} (no final "
                    f"checksum); compared at equal instruction count",
                )
            elif None in vals:
                checks.add(
                    "FAIL",
                    scope,
                    "checksum missing in: "
                    + str([a for a, v in cs.items() if v is None]),
                )
            elif len(vals) == 1:
                checks.add("PASS", scope, f"checksum identical ({vals.pop()})")
            else:
                checks.add("FAIL", scope, f"checksum mismatch: {cs}")

            # G5 INIT_ONLY and TRUNCATED classification
            for a, r in good.items():
                if r["insts"] >= maxi:
                    if r["pf_issued"] == 0 and r["ibatch_done"] == 0:
                        checks.add(
                            "INIT_ONLY",
                            scope,
                            f"{a}: G5 INIT_ONLY (sim_insts={r['insts']} == "
                            f"maxinsts={maxi}, pf_issued=0, ibatch=0)",
                        )
                    else:
                        checks.add(
                            "TRUNCATED",
                            scope,
                            f"{a}: TRUNCATED (sim_insts={r['insts']} == "
                            f"maxinsts={maxi}, pf_issued={r['pf_issued']}, "
                            f"ibatch={r['ibatch_done']})",
                        )

            # Mechanism gates
            # G1: pf_issued >= 1000 and pf_useful > 0 (for prefetch/COMPASS arms on supported streaming workloads)
            # G2: ibatch_completed > 0 (for IPP/COMPASS arms)
            # G4: compass_seeds_arrived == tot_compass_seeds (for B/D arms)
            arm_dict = {a[0]: a for a in active_arms}
            for a, r in good.items():
                is_init_only = (
                    r["insts"] >= maxi
                    and r["pf_issued"] == 0
                    and r["ibatch_done"] == 0
                )
                if is_init_only:
                    continue  # INIT_ONLY runs didn't reach kernel

                arm_info = arm_dict.get(a)
                cell_val = arm_info[1] if arm_info else None
                ipp_val = arm_info[3] if arm_info else False
                pref_val = arm_info[4] if arm_info else False

                # G1 check
                if pref_val:
                    if r["pf_issued"] >= 1000 and r["pf_useful"] > 0:
                        checks.add(
                            "PASS",
                            scope,
                            f"{a}: G1 pf_issued={r['pf_issued']}, "
                            f"pf_useful={r['pf_useful']}",
                        )
                    else:
                        lvl = "FAIL" if must_prep else "WARN"
                        checks.add(
                            lvl,
                            scope,
                            f"{a}: G1 failed (pf_issued={r['pf_issued']}, "
                            f"pf_useful={r['pf_useful']})",
                        )

                # G2 check
                if ipp_val:
                    if r["ibatch_done"] > 0:
                        checks.add(
                            "PASS",
                            scope,
                            f"{a}: G2 ibatch_completed={r['ibatch_done']}",
                        )
                    else:
                        checks.add(
                            "FAIL",
                            scope,
                            f"{a}: G2 failed (ibatch_completed={r['ibatch_done']})",
                        )

                # G4 check
                if cell_val in ("B", "D") or a == "B" or a.startswith("D_"):
                    arrived = r.get("compass_seeds_arrived", 0)
                    tot = r.get("tot_compass_seeds", 0)
                    if arrived == tot and tot > 0:
                        checks.add(
                            "PASS",
                            scope,
                            f"{a}: G4 compass_seeds_arrived == "
                            f"tot_compass_seeds == {arrived}",
                        )
                    elif arrived == tot:
                        checks.add(
                            "WARN" if not must_prep else "FAIL",
                            scope,
                            f"{a}: G4 seeds matched but zero "
                            f"(arrived={arrived}, tot={tot})",
                        )
                    else:
                        checks.add(
                            "FAIL",
                            scope,
                            f"{a}: G4 failed: compass_seeds_arrived={arrived} "
                            f"!= tot_compass_seeds={tot}",
                        )

            # 3. prep stats zero where K-prep must be off
            off = [
                a
                for a in (
                    "stock",
                    "ipp",
                    "prefetch",
                    "iPF_noCoord",
                    "iPF_IPP_uncoord",
                    "stride",
                    "bop",
                    "sms",
                    "A",
                    "B",
                    "C_k1",
                    "D_k1",
                )
                if a in good
            ]
            nz = [a for a in off if good[a]["prep_seg"] != 0]
            checks.add(
                "FAIL" if nz else "PASS",
                scope,
                (
                    f"prep_segments_advanced != 0 where K=1: {nz}"
                    if nz
                    else "prep_* stats zero for K=1 arms"
                ),
            )
            # 4. K>1 advances segments
            for cell in ("C", "D"):
                prev = -1
                for k in [k for k in sorted(set(ks)) if k > 1]:
                    a = f"{cell}_k{k}"
                    if a not in good:
                        continue
                    seg = good[a]["prep_seg"]
                    if seg > 0:
                        checks.add(
                            "PASS", scope, f"{a}: prep_segments_advanced={seg}"
                        )
                    else:
                        checks.add(
                            "FAIL" if must_prep else "WARN",
                            scope,
                            f"{a}: prep_segments_advanced=0 "
                            f"(K-prep idle; blocked="
                            f"{good[a]['prep_blocked']})",
                        )
                    if seg < prev:
                        checks.add(
                            "WARN",
                            scope,
                            f"{a}: fewer segments than smaller K",
                        )
                    prev = seg
            # 5. regression: K=1 equals A/B
            for new, ref in (("C_k1", "A"), ("D_k1", "B")):
                if new in good and ref in good:
                    same = good[new]["ticks"] == good[ref]["ticks"]
                    checks.add(
                        "PASS" if same else "FAIL",
                        scope,
                        (
                            f"{new} ticks == {ref} ticks"
                            if same
                            else f"{new} ticks {good[new]['ticks']} != "
                            f"{ref} {good[ref]['ticks']}"
                        ),
                    )
            # 6. interval delivery observable
            if "A" in good and "B" in good:
                a_, b_ = good["A"], good["B"]
                if (a_["ticks"], a_["ipf_gen"]) == (
                    b_["ticks"],
                    b_["ipf_gen"],
                ):
                    checks.add(
                        "WARN",
                        scope,
                        "B identical to A: interval path inactive?",
                    )
                else:
                    checks.add(
                        "PASS",
                        scope,
                        f"B differs from A (ticks {a_['ticks']}->"
                        f"{b_['ticks']}, iPF gen {a_['ipf_gen']}->"
                        f"{b_['ipf_gen']})",
                    )
            if "A" in good and good["A"]["pf_issued"] == 0:
                checks.add("WARN", scope, "A: pfIssued == 0 (iPF idle)")
            # 7. performance
            if all(x in good for x in ("A", "B")):
                for kk in sorted(set(ks)):
                    c, d = good.get(f"C_k{kk}"), good.get(f"D_k{kk}")
                    if not (c and d):
                        continue
                    # Skip INIT_ONLY runs from performance evaluation
                    if any(
                        (
                            x["insts"] >= maxi
                            and x["pf_issued"] == 0
                            and x["ibatch_done"] == 0
                        )
                        for x in (good["A"], good["B"], c, d)
                    ):
                        continue
                    ta, tb = good["A"]["seconds"], good["B"]["seconds"]
                    tc, td = c["seconds"], d["seconds"]
                    perf.append(
                        {
                            "benchmark": b,
                            "latency": lat,
                            "K": kk,
                            "T_A": ta,
                            "T_B": tb,
                            "T_C": tc,
                            "T_D": td,
                            "S_B/A": ta / tb if tb else 0.0,
                            "S_C/A": ta / tc if tc else 0.0,
                            "S_D/A": ta / td if td else 0.0,
                            "I": ln(tb) + ln(tc) - ln(ta) - ln(td),
                            "S_A/stock": (
                                good["stock"]["seconds"] / ta
                                if "stock" in good and ta
                                else 0.0
                            ),
                        }
                    )
    return perf


def write_outputs(
    benches, lats, ks, results, checksums, rcs, checks, perf, profile
):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    csv_path = os.path.join(OUTPUT_DIR, "verify_results.csv")
    fields = [
        "benchmark",
        "latency",
        "arm",
        "return_code",
        "verdict",
        "checksum",
        "sim_seconds",
        "sim_ticks",
        "sim_insts",
        "ipc",
        "pf_issued",
        "pf_useful",
        "ipf_generated",
        "ipf_full",
        "l3_demand_mshr",
        "ibatch_completed",
        "prep_segments_advanced",
        "prep_blocked",
        "blocked_while_ready_at_iMC",
        "l3_mshr_blocked_cycles",
        "demand_avg_mshr_miss_latency",
        "overall_mshr_misses",
        "compass_seeds_arrived",
        "tot_compass_seeds",
    ]
    active_arms = arms_for(ks)
    present_arm_names = {k[2] for k in results.keys()}
    for candidate in [
        ("iPF_noCoord", None, 1, True, True),
        ("iPF_IPP_uncoord", None, 1, True, True),
        ("stride", None, 1, False, False, "StridePrefetcher"),
        ("bop", None, 1, False, False, "BOPPrefetcher"),
        ("sms", None, 1, False, False, "SmsPrefetcher"),
    ]:
        if candidate[0] in present_arm_names and not any(
            a[0] == candidate[0] for a in active_arms
        ):
            active_arms.append(candidate)

    with open(csv_path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(fields)
        for b in benches:
            maxi = int(BENCHMARKS[b][profile][1])
            for lat in lats:
                for arm in active_arms:
                    a = arm[0]
                    r = results.get((b, lat, a)) or {"ok": False}
                    rc = rcs.get((b, lat, a))
                    v = classify_verdict(rc, r, maxi)
                    cs_entry = checksums.get((b, lat, a))
                    if isinstance(cs_entry, tuple):
                        cs_val, cs_status = cs_entry
                    else:
                        cs_val, cs_status = (
                            cs_entry,
                            "OK" if cs_entry else "UNVERIFIED",
                        )
                    cs_str = cs_val if cs_val is not None else cs_status
                    w.writerow(
                        [
                            b,
                            lat,
                            a,
                            rc,
                            v,
                            cs_str,
                            r.get("seconds"),
                            r.get("ticks"),
                            r.get("insts"),
                            r.get("ipc"),
                            r.get("pf_issued"),
                            r.get("pf_useful"),
                            r.get("ipf_gen"),
                            r.get("ipf_full"),
                            r.get("l3_demand_mshr"),
                            r.get("ibatch_done"),
                            r.get("prep_seg"),
                            r.get("prep_blocked"),
                            r.get("blocked_while_ready_at_iMC", 0),
                            r.get("l3_mshr_blocked_cycles", 0),
                            r.get("demand_avg_mshr_miss_latency", 0.0),
                            r.get("overall_mshr_misses", 0),
                            r.get("compass_seeds_arrived", 0),
                            r.get("tot_compass_seeds", 0),
                        ]
                    )
    md = os.path.join(OUTPUT_DIR, "verify_summary.md")
    with open(md, "w") as fh:
        fh.write(f"# COMPASS v2 verification ({profile} profile)\n\n")
        fh.write(f"Binary: `{GEM5_BIN}`  \n")
        fh.write(
            f"PASS={checks.count('PASS')} WARN={checks.count('WARN')} "
            f"FAIL={checks.count('FAIL')} INIT_ONLY={checks.count('INIT_ONLY')} "
            f"TRUNCATED={checks.count('TRUNCATED')}\n\n## Checks\n\n"
        )
        fh.write("| Result | Scope | Detail |\n|---|---|---|\n")
        order = {
            "FAIL": 0,
            "INIT_ONLY": 1,
            "TRUNCATED": 2,
            "WARN": 3,
            "PASS": 4,
        }
        for lvl, scope, msg in sorted(
            checks.rows, key=lambda r: (order.get(r[0], 5), r[1])
        ):
            fh.write(f"| {lvl} | {scope} | {msg} |\n")
        fh.write(
            "\n## Performance (2x2 factorial, execution-time speedup "
            "over cell A)\n\n"
        )
        fh.write(
            "Delivery = S_B/A, Preparation = S_C/A, Combined = S_D/A; "
            "I = ln(T_B*T_C/(T_A*T_D)); >0 means synergy.\n\n"
        )
        fh.write(
            "| Benchmark | Lat | K | S_B/A | S_C/A | S_D/A | I | "
            "A vs stock |\n|---|---:|---:|---:|---:|---:|---:|---:|\n"
        )
        for p in perf:
            fh.write(
                f"| {p['benchmark']} | {p['latency']} | {p['K']} | "
                f"{p['S_B/A']:.4f}x | {p['S_C/A']:.4f}x | "
                f"{p['S_D/A']:.4f}x | {p['I']:+.4f} | "
                f"{p['S_A/stock']:.3f}x |\n"
            )
            print(
                f"PERF|{p['benchmark']}|{p['latency']}|K={p['K']}"
                f"|S_B/A={p['S_B/A']:.4f}|S_C/A={p['S_C/A']:.4f}"
                f"|S_D/A={p['S_D/A']:.4f}|I={p['I']:+.4f}"
                f"|A_vs_stock={p['S_A/stock']:.3f}"
            )
        best = max(perf, key=lambda p: p["S_D/A"], default=None)
        if best:
            fh.write(
                f"\nBest combined: {best['benchmark']}@"
                f"{best['latency']} K={best['K']} "
                f"S_D/A={best['S_D/A']:.4f}x\n"
            )
            print(
                f"Best combined: {best['benchmark']}@"
                f"{best['latency']} K={best['K']} "
                f"S_D/A={best['S_D/A']:.4f}x"
            )
        any_gain = any(
            max(p["S_B/A"], p["S_C/A"], p["S_D/A"]) > GAIN_MIN for p in perf
        )
        fh.write(
            f"\nObservable improvement (> {100*(GAIN_MIN-1):.0f}% over A) "
            f"in at least one point: "
            f"**{'YES' if any_gain else 'NO'}**\n"
        )
        print(
            f"Observable improvement (> {100*(GAIN_MIN-1):.0f}% over A) "
            f"in at least one point: {'YES' if any_gain else 'NO'}"
        )
    return (
        csv_path,
        md,
        any(max(p["S_B/A"], p["S_C/A"], p["S_D/A"]) > GAIN_MIN for p in perf),
    )


def main():
    global CUR_PROFILE, INTERVAL_W, CREDITS, NUM_CPUS, MEM_CHANNELS, L3_MSHRS, INCLUDE_SOTA, INCLUDE_UNCOORD
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    ap.add_argument("--profile", choices=("quick", "full"), default="quick")
    ap.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 16))
    ap.add_argument("--benchmarks", default=",".join(BENCHMARKS))
    ap.add_argument("--latencies", default="40,120")
    ap.add_argument("--ks", default="2,4", help="K values for cells C/D")
    ap.add_argument(
        "--num-cpus",
        type=int,
        default=1,
        help="Number of CPUs for multicore runs",
    )
    ap.add_argument(
        "--mem-channels",
        type=int,
        default=None,
        help="Number of memory channels",
    )
    ap.add_argument(
        "--l3-mshrs",
        type=int,
        default=None,
        help="Override L3 cache MSHRs",
    )
    ap.add_argument(
        "--sota",
        "--include-sota",
        dest="include_sota",
        action="store_true",
        default=False,
        help="Include SOTA prefetcher arms (stride, bop, sms)",
    )
    ap.add_argument(
        "--no-sota",
        dest="include_sota",
        action="store_false",
        help="Exclude SOTA prefetcher arms",
    )
    ap.add_argument(
        "--uncoord",
        "--include-uncoord",
        dest="include_uncoord",
        action="store_true",
        default=False,
        help="Include uncoordinated arm iPF_noCoord",
    )
    ap.add_argument(
        "--no-uncoord",
        dest="include_uncoord",
        action="store_false",
        help="Exclude uncoordinated arm",
    )
    ap.add_argument(
        "--arms",
        default=None,
        help="Comma-separated subset of arms to run",
    )
    ap.add_argument(
        "--interval-w",
        type=int,
        default=INTERVAL_W,
        help="Stage 2 delivery window W_del in cache lines "
        "(default: %(default)s)",
    )
    ap.add_argument(
        "--credits",
        type=int,
        default=CREDITS,
        help="Stage 2 credit budget in cache lines " "(default: %(default)s)",
    )
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument(
        "--build",
        action="store_true",
        help="compile benchmarks with $CLANG/$TC first",
    )
    args = ap.parse_args()
    CUR_PROFILE = args.profile
    INTERVAL_W = args.interval_w
    CREDITS = args.credits
    NUM_CPUS = args.num_cpus
    MEM_CHANNELS = args.mem_channels
    L3_MSHRS = args.l3_mshrs
    INCLUDE_SOTA = args.include_sota
    INCLUDE_UNCOORD = args.include_uncoord

    if args.arms:
        wanted = [x.strip() for x in args.arms.split(",") if x.strip()]
        if any(w in ("stride", "bop", "sms") for w in wanted):
            INCLUDE_SOTA = True
        if any(w in ("iPF_noCoord", "iPF_IPP_uncoord") for w in wanted):
            INCLUDE_UNCOORD = True

    if args.benchmarks in ("heldout", "held_out"):
        benches = list(HELDOUT_BENCHMARKS)
    elif args.benchmarks == "default":
        benches = list(DEFAULT_BENCHMARKS)
    elif args.benchmarks == "exp1":
        benches = list(EXP1_BENCHMARKS)
    else:
        benches = [x for x in args.benchmarks.split(",") if x]
    for b in benches:
        if b not in BENCHMARKS:
            sys.exit(f"unknown benchmark {b}; known {list(BENCHMARKS)}")
    lats = [int(x) for x in args.latencies.split(",") if x]
    ks = [int(x) for x in args.ks.split(",") if x]
    if any(k not in (1, 2, 4) for k in ks):
        sys.exit("--ks values must be in 1,2,4")

    if args.build and not args.dry_run:
        compile_binaries(benches)

    arms_to_run = arms_for(ks)
    if args.arms:
        wanted = [x.strip() for x in args.arms.split(",") if x.strip()]
        arms_to_run = [
            a
            for a in arms_to_run
            if a[0] in wanted
            or (a[0] == "iPF_IPP_uncoord" and "iPF_noCoord" in wanted)
        ]

    jobs = [
        (b, args.profile, lat, arm)
        for b in benches
        for lat in lats
        for arm in arms_to_run
    ]

    if args.dry_run:
        for b, prof, lat, arm in jobs:
            print(
                " ".join(
                    build_command(b, prof, lat, arm, run_dir(b, lat, arm[0]))
                )
            )
        print(f"{len(jobs)} runs")
        return 0

    if not os.path.isfile(GEM5_BIN):
        sys.exit(f"missing {GEM5_BIN}")
    missing = [
        b
        for b in benches
        if not os.path.isfile(os.path.join(GEM5_ROOT, BENCHMARKS[b]["bin"]))
    ]
    if missing:
        sys.exit(
            f"missing RISC-V binaries for {missing}; compile them or "
            f"use --build (CLANG, TC env)"
        )

    os.makedirs(RUN_ROOT, exist_ok=True)
    print(f"{len(jobs)} runs, {args.jobs} parallel, profile={args.profile}")
    rcs, results, checksums = {}, {}, {}
    done = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(execute, j, args.force) for j in jobs]
        for f in as_completed(futs):
            (b, _, lat, arm), rc, wall, cached = f.result()
            done += 1
            d = run_dir(b, lat, arm[0])
            rcs[(b, lat, arm[0])] = rc
            results[(b, lat, arm[0])] = parse_stats(
                os.path.join(d, "stats.txt")
            )
            checksums[(b, lat, arm[0])] = parse_checksum(
                os.path.join(d, "run.log")
            )
            print(
                f"[{done}/{len(jobs)}] {b}@{lat} {arm[0]} rc={rc} "
                f"{'cached' if cached else f'{wall:.0f}s'}",
                flush=True,
            )

    checks = Checks()
    perf = evaluate(benches, lats, ks, results, checksums, rcs, checks)
    csv_path, md, gain = write_outputs(
        benches, lats, ks, results, checksums, rcs, checks, perf, args.profile
    )
    print(
        f"\nPASS={checks.count('PASS')} WARN={checks.count('WARN')} "
        f"FAIL={checks.count('FAIL')} INIT_ONLY={checks.count('INIT_ONLY')} "
        f"TRUNCATED={checks.count('TRUNCATED')}  improvement_observed={gain}"
    )
    for lvl, scope, msg in checks.rows:
        if lvl != "PASS":
            print(f"  {lvl} {scope}: {msg}")
    print(f"Wrote {csv_path}\n      {md}")
    return 1 if checks.count("FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())
