#!/usr/bin/env python3
"""Run the COMPASS 2x2 factorial benchmark matrix and generate reports.

Legacy external baselines: stock COTS, pure IPP (iMC only), pure prefetcher
(iPF only).  Factorial cells (all iPF+IPP co-design hardware):
  A = current prep + line pulls      (K=1)
  B = current prep + interval deliv. (K=1)
  C = K-segment prep + line pulls
  D = K-segment prep + interval deliv.

Usage:
  run_evaluation.py [--force] [--dry-run] [--latencies 40,120]
                    [--benchmarks daxpy_large,kv_cache] [--k {1,2,4}]
Environment: GEM5_OPT, OUTPUT_DIR, META_ISA_PREP_K.
"""

import argparse
import csv
import html
import math
import os
import re
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GEM5_ROOT = os.path.dirname(SCRIPT_DIR)
GEM5_OPT = os.environ.get(
    "GEM5_OPT", os.path.join(GEM5_ROOT, "build/RISCV/gem5.opt")
)
CONFIG_SE = os.path.join(GEM5_ROOT, "configs/example/se.py")
OUTPUT_DIR = os.path.normpath(
    os.environ.get("OUTPUT_DIR", os.path.join(GEM5_ROOT, "..", "output"))
)
M5OUT_BASE = os.path.join(GEM5_ROOT, "m5out_eval")

DEFAULT_PREP_K = 2
INTERVAL_W = 16
CREDITS = 16

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

LATENCIES = [40, 80, 120]

# "cell" arms use the iPF+IPP co-design command; "k" is None for cells that
# take the user-selected K (C, D) and 1 for A, B.
ARMS = {
    "stock": {
        "label": "Stock COTS",
        "ipp": False,
        "prefetch": False,
        "cell": None,
    },
    "ipp": {
        "label": "Pure IPP (iMC only)",
        "ipp": True,
        "prefetch": False,
        "cell": None,
    },
    "prefetch": {
        "label": "Pure prefetcher (iPF only)",
        "ipp": False,
        "prefetch": True,
        "cell": None,
    },
    "A": {
        "label": "A: current prep + line pulls",
        "ipp": True,
        "prefetch": True,
        "cell": "A",
        "k": 1,
    },
    "B": {
        "label": "B: current prep + interval delivery",
        "ipp": True,
        "prefetch": True,
        "cell": "B",
        "k": 1,
    },
    "C": {
        "label": "C: K-segment prep + line pulls",
        "ipp": True,
        "prefetch": True,
        "cell": "C",
        "k": None,
    },
    "D": {
        "label": "D: K-segment prep + interval delivery",
        "ipp": True,
        "prefetch": True,
        "cell": "D",
        "k": None,
    },
}
CELLS = ("A", "B", "C", "D")
EPS = 1e-30


def ratio(numerator, denominator):
    """Guarded division: 0.0 when the denominator is not positive."""
    return numerator / denominator if denominator > 0 else 0.0


def safe_ln(value):
    return math.log(max(value, EPS))


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
        "prepSegmentsAdvanced": 0,
        "prepBlocked": 0,
    }
    if not os.path.isfile(stats_path):
        return stats

    values = {}
    prep_segments = 0.0
    prep_blocked = 0.0
    with open(stats_path) as stats_file:
        for line in stats_file:
            parts = line.split()
            if len(parts) < 2:
                continue
            key = parts[0]
            try:
                number = float(parts[1])
            except ValueError:
                continue
            values[key] = number
            # Sum scalar stats and ``::total`` rollups only (no buckets).
            if "::" in key and not key.endswith("::total"):
                continue
            base = key.split("::")[0].rsplit(".", 1)[-1]
            # Controller stats carry a per-channel suffix (..._advanced_0).
            if "prep_segments_advanced" in base:
                prep_segments += number
            elif "prep_blocked" in base:
                prep_blocked += number

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
    stats["prepSegmentsAdvanced"] = int(prep_segments)
    stats["prepBlocked"] = int(prep_blocked)
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


def arm_k(arm, prep_k):
    return prep_k if arm.get("k") is None else arm["k"]


def run_name_for(bench, arm_name, arm, latency, prep_k):
    k_tag = f"_k{arm_k(arm, prep_k)}" if arm_name in ("C", "D") else ""
    return f"{bench['name']}_{arm_name}{k_tag}_lat{latency}"


def command_for(bench, arm, latency, output_dir, prep_k):
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
    if arm["cell"]:
        command.extend(
            [
                f"--meta-isa-cell={arm['cell']}",
                f"--meta-isa-prep-k={arm_k(arm, prep_k)}",
                f"--meta-isa-interval-w={INTERVAL_W}",
                f"--meta-isa-credits={CREDITS}",
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
        if not points:
            continue
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


def find_row(results, benchmark, arm, latency):
    for r in results:
        if (
            r["benchmark"] == benchmark
            and r["arm"] == arm
            and r["cxl_latency_cycles"] == latency
        ):
            return r
    return None


def factorial(results, benchmark, latency):
    """Return 2x2 attribution metrics, or None if any cell is unusable."""
    rows = {c: find_row(results, benchmark, c, latency) for c in CELLS}
    if any(row is None or row["sim_seconds"] <= 0 for row in rows.values()):
        return None
    t = {c: rows[c]["sim_seconds"] for c in CELLS}
    return {
        "S_B": ratio(t["A"], t["B"]),
        "S_C": ratio(t["A"], t["C"]),
        "S_D": ratio(t["A"], t["D"]),
        "delivery": safe_ln(ratio(t["A"], t["B"])),
        "preparation": safe_ln(ratio(t["A"], t["C"])),
        "combined": safe_ln(ratio(t["A"], t["D"])),
        "interaction": safe_ln(ratio(t["B"] * t["C"], t["A"] * t["D"])),
    }


def write_reports(results, benchmarks, latencies, prep_k):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    csv_path = os.path.join(OUTPUT_DIR, "results.csv")
    fields = [
        "benchmark",
        "arm",
        "arm_label",
        "cell",
        "prep_k",
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
        "prep_segments_advanced",
        "prep_blocked",
        "return_code",
    ]
    with open(csv_path, "w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(results)

    for benchmark in benchmarks:
        name = benchmark["name"]
        series = {}
        for arm_name, arm in ARMS.items():
            points = []
            for latency in latencies:
                row = find_row(results, name, arm_name, latency)
                stock = find_row(results, name, "stock", latency)
                if row is None or stock is None:
                    continue
                points.append(
                    (
                        latency,
                        ratio(stock["sim_seconds"], row["sim_seconds"])
                        if row["sim_seconds"] > 0
                        else 0.0,
                    )
                )
            series[arm["label"]] = points
        svg_chart(
            os.path.join(OUTPUT_DIR, f"{name}_speedup_scaling.svg"),
            f"{name}: speedup vs stock across CXL latency",
            series,
            "speedup vs stock",
        )

    prefetch_series = {}
    for arm in ("prefetch",) + CELLS:
        for label, key in (
            ("pfIssued", "pf_issued"),
            ("pfUseful", "pf_useful"),
            ("accuracy", "prefetch_accuracy"),
            ("coverage", "prefetch_coverage"),
        ):
            prefetch_series[f"{arm} {label}"] = [
                (r["cxl_latency_cycles"], r[key])
                for r in results
                if r["arm"] == arm
            ]
    svg_chart(
        os.path.join(OUTPUT_DIR, "prefetch_activity.svg"),
        "Prefetch utility and accuracy",
        prefetch_series,
        "count / ratio",
    )

    first = benchmarks[0]["name"]
    quadrant_series = {"A (baseline)": [(lat, 1.0) for lat in latencies]}
    for cell in ("B", "C", "D"):
        points = []
        for latency in latencies:
            metrics = factorial(results, first, latency)
            if metrics:
                points.append((latency, metrics[f"S_{cell}"]))
        quadrant_series[f"S_{cell}/A"] = points
    svg_chart(
        os.path.join(OUTPUT_DIR, "quadrant_breakdown.svg"),
        f"2x2 factorial speedup over cell A ({first})",
        quadrant_series,
        "speedup vs A",
    )

    summary_path = os.path.join(OUTPUT_DIR, "summary.md")
    with open(summary_path, "w") as summary:
        summary.write("# COMPASS 2x2 factorial evaluation\n\n")
        summary.write(
            "Benchmark inputs are scaled beyond the 2 MiB L3: daxpy_large "
            "uses 4,194,304 doubles, kv_cache uses 65,536 tokens, and "
            "dlrm_gather uses 65,536 lookups over a 64 MiB table.\n\n"
        )
        summary.write(
            f"K for cells C/D = {prep_k}; interval W = {INTERVAL_W}; "
            f"credits = {CREDITS}.\n\n"
        )
        summary.write(
            "| Arm | Preparation | Delivery | Hardware |\n"
            "|---|---|---|---|\n"
            "| Stock COTS | none | line pulls | baseline |\n"
            "| Pure IPP | none | IPP (iMC only) | external baseline |\n"
            "| Pure prefetcher | iPF | line pulls | external baseline |\n"
            "| A | current (K=1) | line pulls | iPF+IPP |\n"
            "| B | current (K=1) | interval | iPF+IPP |\n"
            f"| C | K-segment (K={prep_k}) | line pulls | iPF+IPP |\n"
            f"| D | K-segment (K={prep_k}) | interval | iPF+IPP |\n\n"
        )
        summary.write(
            "The CSV reports L3 `demandMshrMisses` and `overallMisses`, "
            "`pfIssued`/`pfUseful`, accuracy, coverage, "
            "`prep_segments_advanced` and summed `prep_blocked_*` "
            "(missing stats are 0).\n"
        )
        summary.write(
            "\n## Factorial attribution\n\n"
            "Log-time effects: Delivery = ln(T_A/T_B), Preparation = "
            "ln(T_A/T_C), Combined = ln(T_A/T_D), interaction "
            "I = ln(T_B*T_C/(T_A*T_D)). Speedups are S_X/A = T_A/T_X.\n\n"
            "| Benchmark | Latency | S_B/A | S_C/A | S_D/A | Delivery | "
            "Preparation | Combined | I |\n"
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n"
        )
        for benchmark in benchmarks:
            for latency in latencies:
                m = factorial(results, benchmark["name"], latency)
                if m is None:
                    continue
                summary.write(
                    f"| `{benchmark['name']}` | {latency} | "
                    f"{m['S_B']:.3f}x | {m['S_C']:.3f}x | "
                    f"{m['S_D']:.3f}x | {m['delivery']:+.4f} | "
                    f"{m['preparation']:+.4f} | {m['combined']:+.4f} | "
                    f"{m['interaction']:+.4f} |\n"
                )

    report_path = os.path.join(OUTPUT_DIR, "report.html")
    with open(report_path, "w") as report:
        report.write(
            "<!doctype html><meta charset='utf-8'>"
            "<title>COMPASS evaluation</title><h1>COMPASS evaluation</h1>"
            "<p>2x2 factorial (preparation x delivery) across scaled "
            "working sets and CXL latencies.</p>"
        )
        for chart in (
            "quadrant_breakdown.svg",
            "prefetch_activity.svg",
        ):
            report.write(f"<h2>{html.escape(chart)}</h2><img src='{chart}'>")
        report.write("<h2>Speedup/latency charts</h2>")
        for benchmark in benchmarks:
            chart = f"{benchmark['name']}_speedup_scaling.svg"
            report.write(f"<h3>{html.escape(benchmark['name'])}</h3>")
            report.write(f"<img src='{chart}'>")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--force", action="store_true",
                        help="rerun even if stats.txt exists")
    parser.add_argument("--dry-run", action="store_true",
                        help="print commands and exit; run/write nothing")
    parser.add_argument("--latencies",
                        help="comma-separated CXL latencies (default 40,80,120)")
    parser.add_argument("--benchmarks",
                        help="comma-separated benchmark names")
    parser.add_argument(
        "--k", type=int, choices=(1, 2, 4),
        default=int(os.environ.get("META_ISA_PREP_K", DEFAULT_PREP_K)),
        help="K segments for cells C/D (default 2, env META_ISA_PREP_K)",
    )
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv[1:])
    benchmarks = BENCHMARKS
    if args.benchmarks:
        wanted = [n.strip() for n in args.benchmarks.split(",") if n.strip()]
        known = {b["name"]: b for b in BENCHMARKS}
        unknown = [n for n in wanted if n not in known]
        if unknown:
            sys.exit(f"Unknown benchmarks: {unknown}; known: {list(known)}")
        benchmarks = [known[n] for n in wanted]
    latencies = LATENCIES
    if args.latencies:
        latencies = [int(x) for x in args.latencies.split(",") if x.strip()]
    prep_k = args.k

    if not args.dry_run:
        os.makedirs(M5OUT_BASE, exist_ok=True)
    results = []
    for benchmark in benchmarks:
        for arm_name, arm in ARMS.items():
            for latency in latencies:
                run_name = run_name_for(
                    benchmark, arm_name, arm, latency, prep_k
                )
                run_dir = os.path.join(M5OUT_BASE, run_name)
                stats_path = os.path.join(run_dir, "stats.txt")
                stats_missing = not os.path.isfile(
                    stats_path
                ) or not os.path.getsize(stats_path)
                command = command_for(benchmark, arm, latency, run_dir, prep_k)
                if args.dry_run:
                    tag = "run" if args.force or stats_missing else "cached"
                    print(f"[{run_name}] ({tag}) " + " ".join(command))
                    continue
                os.makedirs(run_dir, exist_ok=True)
                return_code = 0
                stats = parse_stats(stats_path)
                if args.force or stats_missing:
                    started = time.time()
                    return_code = run_command(
                        command, os.path.join(run_dir, "run.log")
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
                        "cell": arm["cell"] or "",
                        "prep_k": arm_k(arm, prep_k) if arm["cell"] else "",
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
                        "prep_segments_advanced": stats[
                            "prepSegmentsAdvanced"
                        ],
                        "prep_blocked": stats["prepBlocked"],
                        "return_code": return_code,
                    }
                )
    if args.dry_run:
        return
    write_reports(results, benchmarks, latencies, prep_k)
    print(f"Saved evaluation outputs to {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
