#!/usr/bin/env python3
"""Run the COMPASS benchmark matrix and generate self-contained reports."""

import csv
import html
import math
import os
import subprocess
import sys
import time

GEM5_ROOT = "/home/laiy24/interstelar/gem5"
GEM5_OPT = os.path.join(GEM5_ROOT, "build/RISCV/gem5.opt")
CONFIG_SE = os.path.join(GEM5_ROOT, "configs/example/se.py")
OUTPUT_DIR = "/home/laiy24/interstelar/output"
M5OUT_BASE = os.path.join(GEM5_ROOT, "m5out_eval")

# These values deliberately exceed the 2 MiB L3 and 512 KiB L2.  Keep the
# values in sync with the defaults in the benchmark sources.
BENCHMARKS = [
    {
        "name": "daxpy_large",
        "bin": "BM/daxpy_large/daxpy_large.riscv",
        "options": "4194304",
        "maxinsts": "100000000",
    },
    {
        "name": "kv_cache",
        "bin": "BM/kv_cache/kv_cache.riscv",
        "options": "65536",
        "maxinsts": "100000000",
    },
    {
        "name": "dlrm_gather",
        "bin": "BM/dlrm_gather/dlrm_gather.riscv",
        "options": "65536",
        "maxinsts": "20000000",
    },
]

LATENCIES = [40, 120, 300, 450]
ARMS = {
    "stock": {
        "label": "Stock baseline",
        "ipp": False,
        "prefetch": False,
    },
    "ipp": {
        "label": "Pure IPP",
        "ipp": True,
        "prefetch": False,
    },
    "prefetch": {
        "label": "Pure prefetcher",
        "ipp": False,
        "prefetch": True,
    },
    "full": {
        "label": "Full co-design",
        "ipp": True,
        "prefetch": True,
    },
}


def parse_stats(stats_path):
    """Read scalar and ``::total`` gem5 statistics without assuming either."""
    stats = {
        "simInsts": 0,
        "simTicks": 0,
        "simSeconds": 0.0,
        "ipc": 0.0,
        "pfIssued": 0,
        "pfUseful": 0,
        "accuracy": 0.0,
        "coverage": 0.0,
        "demandMshrMisses": 0,
        "overallMisses": 0,
    }
    if not os.path.isfile(stats_path):
        return stats

    values = {}
    with open(stats_path) as stats_file:
        for line in stats_file:
            parts = line.split()
            if len(parts) < 2:
                continue
            key = parts[0]
            try:
                values[key] = float(parts[1])
            except ValueError:
                continue

    def value(*keys):
        for key in keys:
            if key in values:
                return values[key]
        return 0

    stats["simInsts"] = int(value("simInsts"))
    stats["simTicks"] = int(value("simTicks"))
    stats["simSeconds"] = value("simSeconds")
    stats["ipc"] = value("system.cpu.ipc")
    stats["pfIssued"] = int(value("system.l3.prefetcher.pfIssued"))
    stats["pfUseful"] = int(value("system.l3.prefetcher.pfUseful"))
    stats["accuracy"] = value("system.l3.prefetcher.accuracy")
    stats["coverage"] = value("system.l3.prefetcher.coverage")
    stats["demandMshrMisses"] = int(
        value(
            "system.l3.demandMshrMisses::total",
            "system.l3.demandMshrMisses",
        )
    )
    stats["overallMisses"] = int(
        value("system.l3.overallMisses::total", "system.l3.overallMisses")
    )
    return stats


def run_command(cmd, log_path):
    print("Executing:", " ".join(cmd))
    with open(log_path, "w") as log_file:
        process = subprocess.run(
            cmd,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            cwd=GEM5_ROOT,
        )
    return process.returncode


def command_for(bench, arm, latency, output_dir):
    config = (
        "configs/ramulator/DDR4-config-Gen.cfg"
        if arm["ipp"]
        else "configs/ramulator/DDR4-config-Gen-noIPP.cfg"
    )
    command = [
        GEM5_OPT,
        f"-d{output_dir}",
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
        f"--ramulator-config={config}",
        f"--cxl-latency={latency}",
        f"--cmd={bench['bin']}",
        f"--options={bench['options']}",
        f"--maxinsts={bench['maxinsts']}",
    ]
    if arm["prefetch"]:
        command.extend(
            [
                "--meta-isa-type=IPP",
                "--l3-hwp-type=InterStellarPrefetcher",
            ]
        )
    return command


def svg_chart(path, title, series, y_label):
    """Write a dependency-free line chart suitable for reports and papers."""
    width, height = 760, 420
    left, top, right, bottom = 70, 55, 25, 55
    plot_width = width - left - right
    plot_height = height - top - bottom
    all_values = [value for points in series.values() for _, value in points]
    maximum = max(all_values + [1.0])
    minimum = min(all_values + [0.0])
    if maximum == minimum:
        maximum += 1
    colors = ["#2563eb", "#dc2626", "#16a34a", "#9333ea"]
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}">',
        "<style>text{font:12px sans-serif} .axis{stroke:#444} "
        ".grid{stroke:#ddd} </style>",
        f'<text x="{width / 2}" y="24" text-anchor="middle" '
        f'font-size="16">{html.escape(title)}</text>',
    ]
    for index in range(5):
        y = top + plot_height * index / 4
        value = maximum - (maximum - minimum) * index / 4
        lines.append(
            f'<line class="grid" x1="{left}" y1="{y:.1f}" '
            f'x2="{width - right}" y2="{y:.1f}"/>'
        )
        lines.append(
            f'<text x="{left - 8}" y="{y + 4:.1f}" '
            f'text-anchor="end">{value:.2f}</text>'
        )
    lines.extend(
        [
            f'<line class="axis" x1="{left}" y1="{top + plot_height}" '
            f'x2="{width - right}" y2="{top + plot_height}"/>',
            f'<line class="axis" x1="{left}" y1="{top}" '
            f'x2="{left}" y2="{top + plot_height}"/>',
            f'<text x="15" y="{top + plot_height / 2}" '
            f'transform="rotate(-90 15 {top + plot_height / 2})" '
            f'text-anchor="middle">{html.escape(y_label)}</text>',
        ]
    )
    x_values = sorted({x for points in series.values() for x, _ in points})
    for index, x_value in enumerate(x_values):
        x = left + plot_width * index / max(len(x_values) - 1, 1)
        lines.append(
            f'<text x="{x:.1f}" y="{height - 30}" text-anchor="middle">'
            f"{x_value}</text>"
        )
    for index, (name, points) in enumerate(series.items()):
        coords = []
        for x_value, value in points:
            x_index = x_values.index(x_value)
            x = left + plot_width * x_index / max(len(x_values) - 1, 1)
            y = top + plot_height * (maximum - value) / (maximum - minimum)
            coords.append(f"{x:.1f},{y:.1f}")
        color = colors[index % len(colors)]
        lines.append(
            f'<polyline points="{" ".join(coords)}" fill="none" '
            f'stroke="{color}" stroke-width="2"/>'
        )
        legend_x = left + index * 160
        lines.append(
            f'<line x1="{legend_x}" y1="{height - 12}" '
            f'x2="{legend_x + 20}" y2="{height - 12}" '
            f'stroke="{color}" stroke-width="3"/>'
        )
        lines.append(
            f'<text x="{legend_x + 25}" y="{height - 8}">'
            f"{html.escape(name)}</text>"
        )
    lines.append("</svg>")
    with open(path, "w") as chart_file:
        chart_file.write("\n".join(lines))


def write_reports(results):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    csv_path = os.path.join(OUTPUT_DIR, "results.csv")
    fields = [
        "benchmark",
        "arm",
        "arm_label",
        "cxl_latency_cycles",
        "ipc",
        "sim_seconds",
        "sim_insts",
        "pf_issued",
        "pf_useful",
        "prefetch_accuracy",
        "prefetch_coverage",
        "l3_demand_mshr_misses",
        "l3_overall_misses",
        "return_code",
    ]
    with open(csv_path, "w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(results)

    by_benchmark = {}
    for result in results:
        by_benchmark.setdefault(result["benchmark"], []).append(result)
    for benchmark, benchmark_results in by_benchmark.items():
        series = {}
        stock_ipc = {
            r["cxl_latency_cycles"]: r["ipc"]
            for r in benchmark_results
            if r["arm"] == "stock"
        }
        for arm in ARMS:
            series[ARMS[arm]["label"]] = [
                (
                    r["cxl_latency_cycles"],
                    (
                        r["ipc"] / stock_ipc.get(r["cxl_latency_cycles"], 1.0)
                        if stock_ipc.get(r["cxl_latency_cycles"], 0.0) > 0
                        else 0.0
                    ),
                )
                for r in benchmark_results
                if r["arm"] == arm
            ]
        svg_chart(
            os.path.join(OUTPUT_DIR, f"{benchmark}_speedup_scaling.svg"),
            f"{benchmark}: speedup across CXL latency",
            series,
            "speedup vs stock",
        )

    prefetch_series = {}
    for arm in ("prefetch", "full"):
        prefetch_series[f"{arm} pfIssued"] = [
            (r["cxl_latency_cycles"], r["pf_issued"])
            for r in results
            if r["arm"] == arm
        ]
        prefetch_series[f"{arm} pfUseful"] = [
            (r["cxl_latency_cycles"], r["pf_useful"])
            for r in results
            if r["arm"] == arm
        ]
        prefetch_series[f"{arm} accuracy"] = [
            (r["cxl_latency_cycles"], r["prefetch_accuracy"])
            for r in results
            if r["arm"] == arm
        ]
        prefetch_series[f"{arm} coverage"] = [
            (r["cxl_latency_cycles"], r["prefetch_coverage"])
            for r in results
            if r["arm"] == arm
        ]
    svg_chart(
        os.path.join(OUTPUT_DIR, "prefetch_activity.svg"),
        "Prefetch utility and accuracy",
        prefetch_series,
        "count / ratio",
    )

    quadrant_series = {}
    for arm in ARMS:
        quadrant_series[ARMS[arm]["label"]] = [
            (
                latency,
                next(
                    (
                        r["ipc"]
                        for r in results
                        if r["arm"] == arm
                        and r["benchmark"] == BENCHMARKS[0]["name"]
                        and r["cxl_latency_cycles"] == latency
                    ),
                    0.0,
                ),
            )
            for latency in LATENCIES
        ]
    svg_chart(
        os.path.join(OUTPUT_DIR, "quadrant_breakdown.svg"),
        "Four-quadrant ablation (daxpy_large)",
        quadrant_series,
        "IPC",
    )

    summary_path = os.path.join(OUTPUT_DIR, "summary.md")
    with open(summary_path, "w") as summary:
        summary.write("# COMPASS four-quadrant evaluation\n\n")
        summary.write(
            "Benchmark inputs are scaled beyond the 2 MiB L3: daxpy_large "
            "uses 4,194,304 doubles, kv_cache uses 65,536 tokens, and "
            "dlrm_gather uses 65,536 lookups over a 64 MiB table.\n\n"
        )
        summary.write(
            "| Arm | Preparation | Delivery/row policy | Prefetcher |\n"
            "|---|---|---|---|\n"
        )
        summary.write(
            "| Stock baseline | none | Opened (no IPP) | none |\n"
            "| Pure IPP | none | IPP | none |\n"
            "| Pure prefetcher | iPF | Opened (no IPP) | InterStellar |\n"
            "| Full co-design | iPF | IPP | InterStellar |\n\n"
        )
        summary.write(
            "The CSV reports L3 `demandMshrMisses` and `overallMisses` "
            "from `::total` statistics for every arm, plus `pfUseful`, "
            "accuracy, and coverage where a prefetcher is instantiated.\n"
        )
        summary.write(
            "\n## Factorial attribution\n\n"
            "For each benchmark and latency, the following table reports "
            "execution-time speedups and the additive interaction in log "
            "time. Delivery is `IPP / stock`, preparation is "
            "`prefetch / stock`, and interaction is "
            "`log(stock/full) - log(stock/IPP) - log(stock/prefetch)`.\n\n"
            "| Benchmark | Latency | Delivery | Preparation | Full | "
            "Interaction |\n|---|---:|---:|---:|---:|---:|\n"
        )
        for benchmark in BENCHMARKS:
            for latency in LATENCIES:
                row = {
                    r["arm"]: r
                    for r in results
                    if r["benchmark"] == benchmark["name"]
                    and r["cxl_latency_cycles"] == latency
                }
                if not all(arm in row for arm in ARMS):
                    continue
                times = {
                    arm: max(row[arm]["sim_seconds"], 1e-30) for arm in ARMS
                }
                delivery = times["stock"] / times["ipp"]
                preparation = times["stock"] / times["prefetch"]
                full = times["stock"] / times["full"]
                interaction = (
                    math.log(times["stock"] / times["full"])
                    - math.log(delivery)
                    - math.log(preparation)
                )
                summary.write(
                    f"| `{benchmark['name']}` | {latency} | "
                    f"{delivery:.3f}x | {preparation:.3f}x | "
                    f"{full:.3f}x | {interaction:+.4f} |\n"
                )

    report_path = os.path.join(OUTPUT_DIR, "report.html")
    with open(report_path, "w") as report:
        report.write(
            "<!doctype html><meta charset='utf-8'>"
            "<title>COMPASS evaluation</title><h1>COMPASS evaluation</h1>"
            "<p>Four-quadrant ablation across scaled working sets and CXL "
            "latencies.</p>"
        )
        for chart in (
            "quadrant_breakdown.svg",
            "prefetch_activity.svg",
        ):
            report.write(f"<h2>{html.escape(chart)}</h2><img src='{chart}'>")
        report.write("<h2>Speedup/latency charts</h2>")
        for benchmark in BENCHMARKS:
            chart = f"{benchmark['name']}_speedup_scaling.svg"
            report.write(f"<h3>{html.escape(benchmark['name'])}</h3>")
            report.write(f"<img src='{chart}'>")


def main():
    force = "--force" in sys.argv
    os.makedirs(M5OUT_BASE, exist_ok=True)
    results = []
    for benchmark in BENCHMARKS:
        for arm_name, arm in ARMS.items():
            for latency in LATENCIES:
                run_name = f"{benchmark['name']}_{arm_name}_lat{latency}"
                run_dir = os.path.join(M5OUT_BASE, run_name)
                stats_path = os.path.join(run_dir, "stats.txt")
                os.makedirs(run_dir, exist_ok=True)
                return_code = 0
                stats = parse_stats(stats_path)
                stats_missing = not os.path.isfile(
                    stats_path
                ) or not os.path.getsize(stats_path)
                if force or stats_missing:
                    started = time.time()
                    return_code = run_command(
                        command_for(benchmark, arm, latency, run_dir),
                        os.path.join(run_dir, "run.log"),
                    )
                    print(
                        f"[{run_name}] return={return_code} "
                        f"wall={time.time() - started:.1f}s"
                    )
                    stats = parse_stats(stats_path)
                results.append(
                    {
                        "benchmark": benchmark["name"],
                        "arm": arm_name,
                        "arm_label": arm["label"],
                        "cxl_latency_cycles": latency,
                        "ipc": stats["ipc"],
                        "sim_seconds": stats["simSeconds"],
                        "sim_insts": stats["simInsts"],
                        "pf_issued": stats["pfIssued"],
                        "pf_useful": stats["pfUseful"],
                        "prefetch_accuracy": stats["accuracy"],
                        "prefetch_coverage": stats["coverage"],
                        "l3_demand_mshr_misses": stats["demandMshrMisses"],
                        "l3_overall_misses": stats["overallMisses"],
                        "return_code": return_code,
                    }
                )
    write_reports(results)
    print(f"Saved evaluation outputs to {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
