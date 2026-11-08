#!/usr/bin/env python3
"""Generate Normative Evaluation Report (M1-M6) and Applicability Plots.

Implements the binding specification from:
`docs/final_proposal/06_evaluation_methodology.md` Section 2.1:
  - M1: SC  (Scheduled Coverage = pfUseful / demandMshrMisses)
  - M2: DLA (Delivery Latency Avoided = (stock - arm) avg_miss_latency_ns)
  - M3: ROF (Request Overhead Factor = arm overallMshr / stock overallMshr)
  - M4: DMA (Demand Misses Avoided = stock demandMshr - arm demandMshr)
  - M5: AIR (Authorized Issue Ratio = pfUseful / pfIssued)
  - M6: DCD (Descriptor Coverage of Demand = pfIssued / demandMshrMisses)

Outputs:
  output/NORMATIVE_METRICS.csv
  output/NORMATIVE_REPORT.md
  output/graphs/normative_metrics.png
"""

import csv
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_float(val, default=0.0):
    try:
        return float(val)
    except (TypeError, ValueError):
        return default


def load_all_data(csv_paths):
    rows = []
    for p in csv_paths:
        if not os.path.isfile(p):
            continue
        with open(p, newline="") as f:
            reader = csv.DictReader(f)
            for r in reader:
                if str(r.get("return_code", "")).strip() not in ("0", "PASS"):
                    if (
                        r.get("verdict") != "PASS"
                        and r.get("return_code") != "0"
                    ):
                        continue
                rows.append(r)
    return rows


def compute_normative_metrics(rows):
    # Group by (benchmark, latency)
    groups = {}
    for r in rows:
        key = (r["benchmark"], int(float(r["latency"])))
        groups.setdefault(key, {})[r["arm"]] = r

    metrics_table = []

    for (bench, lat), arms in sorted(groups.items()):
        stock = arms.get("stock")
        if not stock:
            continue

        stock_ticks = parse_float(stock.get("sim_ticks"))
        stock_mshr_misses = parse_float(stock.get("l3_demand_mshr"))
        stock_overall_mshr = parse_float(
            stock.get("overall_mshr_misses", stock_mshr_misses)
        )
        stock_mshr_lat_ps = parse_float(
            stock.get("demand_avg_mshr_miss_latency")
        )

        for arm_name, arm_row in sorted(arms.items()):
            ticks = parse_float(arm_row.get("sim_ticks"))
            if ticks <= 0:
                continue

            pf_issued = parse_float(arm_row.get("pf_issued"))
            pf_useful = parse_float(arm_row.get("pf_useful"))
            demand_mshr = parse_float(arm_row.get("l3_demand_mshr"))
            overall_mshr = parse_float(
                arm_row.get("overall_mshr_misses", demand_mshr + pf_issued)
            )
            arm_mshr_lat_ps = parse_float(
                arm_row.get("demand_avg_mshr_miss_latency")
            )

            speedup_vs_stock = stock_ticks / ticks if ticks > 0 else 0.0

            # M1: Scheduled Coverage (SC)
            sc = (pf_useful / demand_mshr) if demand_mshr > 0 else 0.0

            # M2: Delivery Latency Avoided (DLA, in ns)
            dla_ns = (
                (stock_mshr_lat_ps - arm_mshr_lat_ps) / 1000.0
                if stock_mshr_lat_ps > 0
                else 0.0
            )

            # M3: Request Overhead Factor (ROF)
            rof = (
                (overall_mshr / stock_overall_mshr)
                if stock_overall_mshr > 0
                else 1.0
            )

            # M4: Demand Misses Avoided (DMA)
            dma = stock_mshr_misses - demand_mshr
            dma_pct = (
                (dma / stock_mshr_misses * 100.0)
                if stock_mshr_misses > 0
                else 0.0
            )

            # M5: Authorized Issue Ratio (AIR)
            air = (pf_useful / pf_issued) if pf_issued > 0 else 1.0

            # M6: Descriptor Coverage of Demand (DCD)
            dcd = (pf_issued / demand_mshr) if demand_mshr > 0 else 0.0

            metrics_table.append(
                {
                    "benchmark": bench,
                    "latency": lat,
                    "arm": arm_name,
                    "sim_ticks": int(ticks),
                    "speedup_vs_stock": speedup_vs_stock,
                    "M1_SC": sc,
                    "M2_DLA_ns": dla_ns,
                    "M3_ROF": rof,
                    "M4_DMA": int(dma),
                    "M4_DMA_pct": dma_pct,
                    "M5_AIR": air,
                    "M6_DCD": dcd,
                    "ibatch_completed": int(
                        parse_float(arm_row.get("ibatch_completed"))
                    ),
                    "prep_segments": int(
                        parse_float(arm_row.get("prep_segments_advanced"))
                    ),
                }
            )

    return metrics_table


def write_normative_csv(metrics, out_path):
    fields = [
        "benchmark",
        "latency",
        "arm",
        "sim_ticks",
        "speedup_vs_stock",
        "M1_SC",
        "M2_DLA_ns",
        "M3_ROF",
        "M4_DMA",
        "M4_DMA_pct",
        "M5_AIR",
        "M6_DCD",
        "ibatch_completed",
        "prep_segments",
    ]
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for m in metrics:
            row = dict(m)
            row["speedup_vs_stock"] = f"{m['speedup_vs_stock']:.4f}"
            row["M1_SC"] = f"{m['M1_SC']:.4f}"
            row["M2_DLA_ns"] = f"{m['M2_DLA_ns']:.2f}"
            row["M3_ROF"] = f"{m['M3_ROF']:.4f}"
            row["M4_DMA_pct"] = f"{m['M4_DMA_pct']:.4f}%"
            row["M5_AIR"] = f"{m['M5_AIR']:.4f}"
            row["M6_DCD"] = f"{m['M6_DCD']:.4f}"
            w.writerow(row)


def write_normative_markdown(metrics, out_path):
    with open(out_path, "w") as f:
        f.write("# Normative Evaluation Report (M1-M6)\n\n")
        f.write(
            "Evaluation framework adhering strictly to `docs/final_proposal/06_evaluation_methodology.md` Section 2.1.\n\n"
        )
        f.write("### Metric Definitions\n")
        f.write(
            "- **M1 (SC)**: Scheduled-Coverage (`pfUseful / demandMshrMisses`)\n"
        )
        f.write(
            "- **M2 (DLA)**: Delivery-Latency-Avoided vs stock (ns/miss)\n"
        )
        f.write(
            "- **M3 (ROF)**: Request-Overhead Factor (`arm overallMshr / stock overallMshr`)\n"
        )
        f.write(
            "- **M4 (DMA)**: Demand-Misses-Avoided (`stock demandMshr - arm demandMshr`)\n"
        )
        f.write(
            "- **M5 (AIR)**: Authorized-Issue Ratio (`pfUseful / pfIssued`)\n"
        )
        f.write(
            "- **M6 (DCD)**: Descriptor-Coverage-of-Demand (`pfIssued / demandMshrMisses`)\n\n"
        )

        f.write("## Normative Metrics Table\n\n")
        f.write(
            "| Benchmark | Lat | Arm | Speedup vs Stock | M1 (SC) | M2 (DLA ns) | M3 (ROF) | M4 (DMA) | M5 (AIR) | M6 (DCD) | iBatch Done |\n"
        )
        f.write("|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for m in metrics:
            f.write(
                f"| `{m['benchmark']}` | {m['latency']} | **{m['arm']}** | {m['speedup_vs_stock']:.3f}x | {m['M1_SC']:.3f} | {m['M2_DLA_ns']:.1f} | {m['M3_ROF']:.3f} | {m['M4_DMA']} ({m['M4_DMA_pct']:.2f}%) | {m['M5_AIR']:.4f} | {m['M6_DCD']:.3f} | {m['ibatch_completed']:,} |\n"
            )


def plot_applicability_map(metrics, out_path):
    # Scatter of SC (y) vs DCD (x) per benchmark for Cell A
    cell_a_points = [m for m in metrics if m["arm"] == "A"]
    if not cell_a_points:
        return

    fig, ax = plt.subplots(figsize=(8, 6), dpi=300)
    fig.patch.set_facecolor("#fcfcfb")
    ax.set_facecolor("#fcfcfb")

    for p in cell_a_points:
        label = f"{p['benchmark']}@{p['latency']}"
        ax.scatter(
            p["M6_DCD"],
            p["M1_SC"],
            s=100,
            color="#1baf7a",
            edgecolors="#0b0b0b",
            linewidth=1.2,
            zorder=3,
        )
        ax.annotate(
            label,
            (p["M6_DCD"], p["M1_SC"]),
            xytext=(7, 4),
            textcoords="offset points",
            fontsize=9,
            color="#0b0b0b",
        )

    ax.set_xlim(-0.05, 1.05)
    ax.set_ylim(-0.05, 1.05)
    ax.set_xlabel(
        "M6: Descriptor Coverage of Demand (DCD = pfIssued / demandMisses)",
        fontsize=11,
        color="#0b0b0b",
    )
    ax.set_ylabel(
        "M1: Scheduled Coverage (SC = pfUseful / demandMisses)",
        fontsize=11,
        color="#0b0b0b",
    )
    ax.set_title(
        "COMPASS v2 Applicability Map (Cell A: SC vs DCD)",
        fontsize=13,
        fontweight="bold",
        color="#0b0b0b",
    )
    ax.grid(True, linestyle="--", alpha=0.6, color="#e1e0d9")

    # Add diagonal line SC = DCD (ideal coverage)
    ax.plot(
        [0, 1],
        [0, 1],
        linestyle=":",
        color="#898781",
        alpha=0.8,
        label="Ideal Line (AIR = 1.0)",
    )
    ax.legend(loc="lower right", frameon=True, facecolor="#ffffff")

    plt.tight_layout()
    plt.savefig(out_path)
    plt.close()
    print(f"Wrote applicability map: {out_path}")


def main():
    base_dir = "/home/laiy24/interstelar"
    csv_paths = [
        os.path.join(base_dir, "output", "verify_results.csv"),
        os.path.join(
            base_dir, "output", "final2_quick_w4", "verify_results.csv"
        ),
        os.path.join(base_dir, "output", "final2_full", "verify_results.csv"),
    ]
    rows = load_all_data(csv_paths)
    print(f"Loaded {len(rows)} verified simulation rows.")
    metrics = compute_normative_metrics(rows)
    print(
        f"Computed normative metrics for {len(metrics)} benchmark-arm configurations."
    )

    out_csv = os.path.join(base_dir, "output", "NORMATIVE_METRICS.csv")
    out_md = os.path.join(base_dir, "output", "NORMATIVE_REPORT.md")
    out_png = os.path.join(
        base_dir, "output", "graphs", "normative_metrics.png"
    )

    write_normative_csv(metrics, out_csv)
    print(f"Wrote {out_csv}")
    write_normative_markdown(metrics, out_md)
    print(f"Wrote {out_md}")
    plot_applicability_map(metrics, out_png)


if __name__ == "__main__":
    main()
