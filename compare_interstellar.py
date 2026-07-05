#!/usr/bin/env python3
import os
import subprocess
import sys

# Default benchmark
BENCHMARK_CMD = "./BMs/Polybench/atax/build/Ex_v2_s1/O3/P0/atax_v2_s1_i0_O3_M724kB_SdoubleB.riscv"
GEM5_BIN = "./build/RISCV/gem5.opt"
SE_CONFIG = "configs/deprecated/example/se.py"
RAMULATOR_CFG = "configs/ramulator/DDR4-config-Gen.cfg"


def run_cmd(cmd, log_file):
    print(f"Running: {' '.join(cmd)}")
    with open(log_file, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, check=True)


def parse_stats(stats_path):
    stats = {}
    if not os.path.exists(stats_path):
        return stats
    with open(stats_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 2:
                key = parts[0]
                try:
                    # Some stats might be floats, ints or strings
                    val = float(parts[1])
                except ValueError:
                    val = parts[1]
                stats[key] = val
    return stats


def main():
    out_with = "out_with_interstellar"
    out_without = "out_without_interstellar"

    # 1. Run WITH InterStellar
    cmd_with = [
        GEM5_BIN,
        f"--outdir={out_with}",
        SE_CONFIG,
        "-n",
        "1",
        "--cpu-type=DerivO3CPU",
        "--cpu-clock=2.4GHz",
        "--sys-clock=2.4GHz",
        "--caches",
        "--l1d_size=64kB",
        "--l1d_assoc=2",
        "--l1i_size=64kB",
        "--l1i_assoc=2",
        "--l2cache",
        "--l2_size=512kB",
        "--l2_assoc=4",
        "--l2-hwp-type=InterStellarPrefetcher",
        "--mem-type=Ramulator",
        f"--ramulator-config={RAMULATOR_CFG}",
        "--meta-isa-type=IPP",
        "--cmd",
        BENCHMARK_CMD,
    ]
    print("--- Running WITH InterStellar ---")
    run_cmd(cmd_with, "run_with.log")

    # 2. Run WITHOUT InterStellar (Stock DDR4, no custom prefetcher)
    cmd_without = [
        GEM5_BIN,
        f"--outdir={out_without}",
        SE_CONFIG,
        "-n",
        "1",
        "--cpu-type=DerivO3CPU",
        "--cpu-clock=2.4GHz",
        "--sys-clock=2.4GHz",
        "--caches",
        "--l1d_size=64kB",
        "--l1d_assoc=2",
        "--l1i_size=64kB",
        "--l1i_assoc=2",
        "--l2cache",
        "--l2_size=512kB",
        "--l2_assoc=4",
        "--mem-type=DDR4_2400_8x8",
        "--meta-isa-type=None",
        "--cmd",
        BENCHMARK_CMD,
    ]
    print("--- Running WITHOUT InterStellar ---")
    run_cmd(cmd_without, "run_without.log")

    # 3. Parse stats
    stats_with = parse_stats(os.path.join(out_with, "stats.txt"))
    stats_without = parse_stats(os.path.join(out_without, "stats.txt"))

    # 4. Compare and display results
    metrics = [
        ("simInsts", "Instructions Simulated", "{:.0f}"),
        ("simSeconds", "Simulation Time (sec)", "{:.6f}"),
        ("system.cpu.ipc", "IPC", "{:.4f}"),
        (
            "system.l2.prefetcher.pfiHitsPerStreamPerCore::100",
            "iHWP hits (Core 1, Stream 0)",
            "{:.0f}",
        ),
        (
            "ramulator.Total_Requests_Completed_iBatch",
            "Ramulator iBatch Requests",
            "{:.0f}",
        ),
    ]

    print("\n" + "=" * 80)
    print(
        f"{'Metric':<35} | {'With InterStellar':<20} | {'Without InterStellar':<20}"
    )
    print("-" * 80)
    for key, label, fmt in metrics:
        val_with = stats_with.get(key, "N/A")
        val_without = stats_without.get(key, "N/A")

        str_with = (
            fmt.format(val_with)
            if isinstance(val_with, (int, float))
            else str(val_with)
        )
        str_without = (
            fmt.format(val_without)
            if isinstance(val_without, (int, float))
            else str(val_without)
        )

        print(f"{label:<35} | {str_with:<20} | {str_without:<20}")
    print("=" * 80 + "\n")


if __name__ == "__main__":
    main()
