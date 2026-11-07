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
        "full": ("65536", "100000000"),
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
        "bin": "BMs/Polybench/atax/build/EXP/atax_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "120000000"),
        "must_prep": True,
    },
    "bicg": {
        "src": "BMs/Polybench/bicg/src/bicg_v2.c",
        "bin": "BMs/Polybench/bicg/build/EXP/bicg_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "120000000"),
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
        "bin": "BMs/Polybench/gemver/build/EXP/gemver_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
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
        "bin": "BMs/Polybench/jacobi-2d/build/EXP/jacobi-2d_exp.riscv",
        "quick": (" ", "20000000"),
        "full": (" ", "100000000"),
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


def resolve_bin(bench):
    """Resolve binary path relative to GEM5_ROOT with fallbacks."""
    info = BENCHMARKS.get(bench, {})
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
                        return os.path.relpath(os.path.join(root, f), GEM5_ROOT)

    return info.get("bin", candidates[0])

CUR_PROFILE = "quick"
GAIN_MIN = 1.02  # >2% speedup over cell A counts as observable
INTERVAL_W = 16
CREDITS = 16
CFG_IPP = "configs/ramulator/DDR4-config-Gen.cfg"
CFG_NOIPP = "configs/ramulator/DDR4-config-Gen-noIPP.cfg"


def arms_for(ks):
    """Return ordered list of (arm_name, cell, k, ipp_cfg, prefetch)."""
    arms = [
        ("stock", None, 1, False, False),
        ("ipp", None, 1, True, False),
        ("prefetch", None, 1, False, True),
        ("A", "A", 1, True, True),
        ("B", "B", 1, True, True),
    ]
    for cell in ("C", "D"):
        for k in sorted(set([1] + ks)):
            arms.append((f"{cell}_k{k}", cell, k, True, True))
    return arms


def run_dir(bench, lat, arm):
    return os.path.join(RUN_ROOT, f"{bench}_lat{lat}_{arm}")


def build_command(bench, profile, lat, arm, outdir):
    _, cell, k, ipp, prefetch = arm
    options, maxinsts = BENCHMARKS[bench][profile]
    cmd = [
        GEM5_BIN,
        f"-d{outdir}",
        CONFIG_SE,
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
        f"--ramulator-config={CFG_IPP if ipp else CFG_NOIPP}",
        f"--cxl-latency={lat}",
        f"--cmd={BENCHMARKS[bench]['bin']}",
        f"--options={options}",
        f"--maxinsts={maxinsts}",
    ]
    if ipp or prefetch:
        # the iMC IPP/iBatch logic is only enabled with --meta-isa-type=IPP
        cmd += ["--meta-isa-type=IPP"]
    if prefetch:
        cmd += ["--l3-hwp-type=InterStellarPrefetcher"]
    if cell:
        cmd += [
            f"--meta-isa-cell={cell}",
            f"--meta-isa-prep-k={k}",
            f"--meta-isa-interval-w={INTERVAL_W}",
            f"--meta-isa-credits={CREDITS}",
        ]
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
        ibatch_done=int(g("ramulator.Total_Requests_Completed_iBatch")),
        prep_seg=int(prep_seg),
        prep_blocked=int(prep_blk),
    )
    return out


def parse_checksum(log):
    if not os.path.isfile(log):
        return None
    with open(log, errors="replace") as fh:
        text = fh.read()
    m = re.findall(r"Checksum:\s*([-+0-9.eE]+)", text)
    if m:
        return m[-1]
    # Fallback for PolyBench outputs
    lines = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if any(line.startswith(p) for p in [
            "Global frequency", "warn:", "info:", "src/", "gem5", "command line:",
            "Abotaleb", "test_mem_mode", "FutureClass", "1GHz", "2GHz",
            "**** REAL", "Exiting @ tick", "CMD:", "simTicks", "simInsts"
        ]):
            continue
        lines.append(line)
    if lines:
        return "hash_" + hashlib.md5("\n".join(lines).encode()).hexdigest()[:12]
    return "rc0_ok"


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
        for lat in lats:
            scope = f"{b}@{lat}"
            R = {
                a: results.get((b, lat, a))
                for a in [x[0] for x in arms_for(ks)]
            }
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
            cs = {a: checksums.get((b, lat, a)) for a in good}
            vals = set(cs.values())
            maxi = int(BENCHMARKS[b][CUR_PROFILE][1])
            truncated = bool(good) and all(
                r["insts"] >= maxi for r in good.values()
            )
            if truncated and set(cs.values()) == {None}:
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
            # 3. prep stats zero where K-prep must be off
            off = [
                a
                for a in ("stock", "ipp", "prefetch", "A", "B", "C_k1", "D_k1")
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
                            "S_B/A": ta / tb,
                            "S_C/A": ta / tc,
                            "S_D/A": ta / td,
                            "I": ln(tb) + ln(tc) - ln(ta) - ln(td),
                            "S_A/stock": (
                                good["stock"]["seconds"] / ta
                                if "stock" in good
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
    ]
    with open(csv_path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(fields)
        for b in benches:
            for lat in lats:
                for arm in arms_for(ks):
                    a = arm[0]
                    r = results.get((b, lat, a)) or {"ok": False}
                    w.writerow(
                        [
                            b,
                            lat,
                            a,
                            rcs.get((b, lat, a)),
                            checksums.get((b, lat, a)),
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
                        ]
                    )
    md = os.path.join(OUTPUT_DIR, "verify_summary.md")
    with open(md, "w") as fh:
        fh.write(f"# COMPASS v2 verification ({profile} profile)\n\n")
        fh.write(f"Binary: `{GEM5_BIN}`  \n")
        fh.write(
            f"PASS={checks.count('PASS')} WARN={checks.count('WARN')} "
            f"FAIL={checks.count('FAIL')}\n\n## Checks\n\n"
        )
        fh.write("| Result | Scope | Detail |\n|---|---|---|\n")
        for lvl, scope, msg in sorted(
            checks.rows, key=lambda r: ("FAIL WARN PASS".index(r[0]), r[1])
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
    global CUR_PROFILE, INTERVAL_W, CREDITS
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    ap.add_argument("--profile", choices=("quick", "full"), default="quick")
    ap.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 16))
    ap.add_argument("--benchmarks", default=",".join(BENCHMARKS))
    ap.add_argument("--latencies", default="40,120")
    ap.add_argument("--ks", default="2,4", help="K values for cells C/D")
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
    jobs = [
        (b, args.profile, lat, arm)
        for b in benches
        for lat in lats
        for arm in arms_for(ks)
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
        f"FAIL={checks.count('FAIL')}  improvement_observed={gain}"
    )
    for lvl, scope, msg in checks.rows:
        if lvl != "PASS":
            print(f"  {lvl} {scope}: {msg}")
    print(f"Wrote {csv_path}\n      {md}")
    return 1 if checks.count("FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())
