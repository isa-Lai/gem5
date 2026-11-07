#!/usr/bin/env python3
"""Generate graphs from verify_compass sweep CSVs.

Usage:
    python3 scripts/plot_results.py --inputs DIR1[,DIR2...] --out OUTDIR

Reads verify_results.csv from each dir (parsing logic imported from
parse_results.py) and writes PNGs into OUTDIR:

    speedup_vs_baselines.png  grouped bars: speedup_vs_stock per arm, per
                              benchmark@latency cluster, + GEOMEAN cluster
    factorial_cells.png       S_B/A, S_C_k2/A, S_D_k2/A bars + factorial
                              interaction I = ln(t_B*t_C/(t_A*t_D))
    latency_sensitivity.png   S_D_k2/A and S_B/A vs latency, small multiples
    ipactivity.png            pf_issued / ibatch_completed /
                              prep_segments_advanced per arm, log scale

Robust to missing arms/rows (skipped with a footnote) and to any number of
benchmarks/latencies/sweeps (nothing hardcoded). Charts with zero plottable
rows are still written, with an empty-state message. matplotlib Agg only.
"""

import argparse
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_results as pr  # noqa: E402

# ---------------------------------------------------------------------------
# Palette & chrome (dataviz-skill reference palette, light mode, surface
# #fcfcfb; family ramps validated with --ordinal, series set validated with
# the six-checks validator; contrast WARNs relieved by per-bar value labels
# + SUMMARY.md table view).
# ---------------------------------------------------------------------------

SURFACE = "#fcfcfb"
INK = "#0b0b0b"  # primary text (titles)
INK2 = "#52514e"  # secondary text (axis labels, value labels)
MUTED = "#898781"  # muted (ticks, footnotes, reference lines)
GRID = "#e1e0d9"  # hairline gridlines
BASELINE = "#c3c2b7"  # axis / baseline

# Categorical slots, fixed assignment by entity (never by rank).
SLOT = {  # reference palette slots 1..4 + baseline gray, light mode
    "ipp": "#2a78d6",  # blue
    "prefetch": "#eb6834",  # orange
    "A": "#1baf7a",  # aqua
    "B": "#eda100",  # yellow
    "stock": MUTED,  # baseline drawn in de-emphasis gray
}
# Cell families: one hue each, ordinal light->dark ramp over the K step
# (k=1,2,4,8 -> index 0..3; other k values clamp to the nearest step).
K_STEPS = (1, 2, 4, 8)
FAMILY_RAMP = {
    "C": ("#eb96b6", "#d55181", "#b83a68", "#96294f"),  # magenta ramp
    "D": ("#a9a0ee", "#9085e9", "#6250d6", "#4a3aa7"),  # violet ramp
}
RAMP_SPARE = ("#9a9a94", "#7a7a75", "#5f5e5a", "#474642")  # unseen cells

_METRIC_COLORS = {
    "pf_issued": "#2a78d6",
    "ibatch_completed": "#eb6834",
    "prep_segments_advanced": "#1baf7a",
}


def family_color(cell, k):
    """Ordinal step of the cell family's hue for K value k."""
    ramp = FAMILY_RAMP.get(cell, RAMP_SPARE)
    i = 0
    for j, step in enumerate(K_STEPS):
        if k >= step:
            i = j
    return ramp[min(i, len(ramp) - 1)]


def arm_color(arm):
    """Fixed color for an arm name (entity -> hue, never rank)."""
    if arm in SLOT:
        return SLOT[arm]
    if "_k" in arm:
        cell, _, kpart = arm.rpartition("_k")
        try:
            k = int(kpart)
        except ValueError:
            k = K_STEPS[1]
        return family_color(cell, k)
    return MUTED


# Chart-1 arm set: everything except stock and the *_k1 gating controls.
CHART1_EXCLUDE = ("stock",)


def is_k1_control(arm):
    return arm.endswith("_k1")


def shortname(sweep_dir):
    return os.path.basename(
        os.path.abspath(sweep_dir.rstrip("/")) or sweep_dir
    )


def fmt_num(v):
    """Compact value label: 3.799 / 0.900 / 62k / 1.05M / 0."""
    if v is None:
        return "n/a"
    if v == 0:
        return "0"
    if abs(v) >= 1000:
        for div, suf in ((1e6, "M"), (1e3, "k")):
            if abs(v) >= div:
                return ("%.3g" % (v / div)) + suf
    if float(v).is_integer() and abs(v) < 1000:
        return "%d" % int(v)
    return "%.3f" % v if abs(v) < 10 else "%.2f" % v


# ---------------------------------------------------------------------------
# Style helpers
# ---------------------------------------------------------------------------


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    ax.grid(axis="y", color=GRID, linewidth=0.8, linestyle="-")
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(BASELINE)
        ax.spines[side].set_linewidth(1.0)
    ax.tick_params(colors=MUTED, labelsize=8, length=3)
    for lbl in ax.get_xticklabels() + ax.get_yticklabels():
        lbl.set_color(MUTED)


def new_figure(
    nrows=1,
    ncols=1,
    figsize=None,
    wspace=0.25,
    hspace=0.35,
    bottom=0.10,
    top=0.90,
):
    fig, axes = plt.subplots(
        nrows, ncols, figsize=figsize, squeeze=False, dpi=150
    )
    fig.patch.set_facecolor(SURFACE)
    fig.subplots_adjust(
        wspace=wspace,
        hspace=hspace,
        top=top,
        bottom=bottom,
        left=0.07,
        right=0.985,
    )
    return fig, axes


def finish(fig, path, title, subtitle=None, footnote=None):
    fig.suptitle(
        title, color=INK, fontsize=12.5, x=0.01, ha="left", y=0.995, va="top"
    )
    if subtitle:
        fig.text(0.01, 0.945, subtitle, color=INK2, fontsize=9, ha="left")
    if footnote:
        fig.text(
            0.01,
            0.008,
            footnote,
            color=MUTED,
            fontsize=7,
            ha="left",
            va="bottom",
            linespacing=1.4,
        )
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)
    print("wrote %s" % path)


def empty_state(path, title, why):
    fig, axes = new_figure(figsize=(9, 4.5))
    ax = axes[0][0]
    style_axes(ax)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.text(
        0.5,
        0.5,
        "no plottable data\n(%s)" % why,
        ha="center",
        va="center",
        fontsize=11,
        color=MUTED,
        transform=ax.transAxes,
    )
    finish(fig, path, title, footnote=why)


# ---------------------------------------------------------------------------
# Data shaping
# ---------------------------------------------------------------------------


def load(indirs):
    records, stats = pr.load_sweeps(indirs)
    pr.compute_ratios(records)
    return records, stats


def clusters(records):
    """(sweep, bench, lat) groups, deterministically sorted."""
    groups = {}
    for rec in records:
        groups.setdefault(pr.group_key(rec), {})[rec["arm"]] = rec
    return [
        (k, groups[k])
        for k in sorted(groups, key=lambda k: (k[0], k[1], k[2]))
    ]


def cluster_label(key):
    sweep, bench, lat = key
    return "{}/{}@{}".format(shortname(sweep), bench, lat)


def skip_footnote(stats):
    parts = []
    for st in stats:
        det = "; ".join(
            "%s x%d" % (k, v) for k, v in sorted(st["skipped_detail"].items())
        )
        parts.append(
            "%s: %d ok / %d skipped%s"
            % (
                st["sweep_dir"],
                st["ok"],
                st["skipped"],
                (" (%s)" % det) if det else "",
            )
        )
    return "rows: " + " | ".join(parts)


def missing_arm_footnote(clst, arms_wanted):
    """Arms from arms_wanted absent in each cluster (for the footnote)."""
    missing = []
    for key, arms in clst:
        miss = [a for a in arms_wanted if a not in arms]
        if miss:
            missing.append("{}: no {}".format(cluster_label(key), ", ".join(miss)))
    return "; ".join(missing) if missing else None


# ---------------------------------------------------------------------------
# Chart 1 — speedup vs baselines
# ---------------------------------------------------------------------------


def chart_speedup(records, stats, outdir):
    path = os.path.join(outdir, "speedup_vs_baselines.png")
    title = "Speedup vs stock (sim_ticks ratio, >1 = faster than stock)"
    clst = clusters(records)
    # Series = every arm seen anywhere except stock and *_k1 controls.
    all_arms = sorted(
        {
            r["arm"]
            for r in records
            if r["arm"] not in CHART1_EXCLUDE and not is_k1_control(r["arm"])
        },
        key=pr.arm_sort_key,
    )
    if not clst or not all_arms:
        empty_state(
            path, title, "no arms left after removing stock / k1 " "controls"
        )
        return
    plot_arms = [a for a in all_arms if any(a in arms for _, arms in clst)]
    if not plot_arms:
        empty_state(path, title, "arms present only as stock/k1 controls")
        return

    geomeans = {}
    for arm in plot_arms:
        vals = [
            arms[arm]["speedup_vs_stock"]
            for _, arms in clst
            if arm in arms and arms[arm]["speedup_vs_stock"] is not None
        ]
        geomeans[arm] = (
            math.exp(sum(math.log(v) for v in vals) / len(vals))
            if vals
            else None
        )

    nclust = len(clst) + 1  # + GEOMEAN pseudo-cluster
    fig_w = max(11.0, min(26.0, 1.35 * nclust))
    fig, axes = new_figure(figsize=(fig_w, 6.0), bottom=0.26, top=0.88)
    ax = axes[0][0]
    style_axes(ax)

    n = len(plot_arms)
    slot_w = 1.0 / n
    bar_w = slot_w * 0.62  # thin marks: air between bars
    xs = range(nclust)
    vmax = 1.0
    for i, arm in enumerate(plot_arms):
        vals = []
        for key, arms in clst:
            rec = arms.get(arm)
            vals.append(
                None
                if rec is None or rec["speedup_vs_stock"] is None
                else rec["speedup_vs_stock"]
            )
        vals.append(geomeans[arm])
        offs = [x - 0.5 + slot_w * (i + 0.5) for x in xs]
        color = arm_color(arm)
        for x, v in zip(offs, vals):
            if v is None:
                continue
            vmax = max(vmax, v)
            ax.bar(
                x,
                v,
                width=bar_w,
                color=color,
                edgecolor=SURFACE,
                linewidth=0.8,
                zorder=3,
            )
            ax.annotate(
                fmt_num(v),
                (x, v),
                ha="center",
                va="bottom",
                rotation=90,
                fontsize=6.3,
                color=INK2,
                xytext=(0, 1.5),
                textcoords="offset points",
                zorder=4,
            )
    ax.axhline(1.0, color=MUTED, linewidth=1.2, linestyle="--", zorder=2)
    labels = [cluster_label(k) for k, _ in clst] + ["GEOMEAN"]
    ax.set_xticks(list(xs))
    ax.set_xticklabels(labels, rotation=30, ha="right", fontsize=8)
    ax.set_xlim(-0.6, nclust - 0.4)
    ax.set_ylim(0, vmax * 1.32)
    ax.set_ylabel("speedup vs stock  (ticks_stock / ticks_arm)")
    ax.legend(
        handles=[Patch(facecolor=arm_color(a), label=a) for a in plot_arms],
        loc="upper left",
        fontsize=8,
        ncol=min(4, len(plot_arms)),
        frameon=False,
        borderaxespad=0.2,
    )
    foot = (
        "GEOMEAN cluster = geometric mean of speedup_vs_stock over "
        "clusters where the arm has data.\nDashed line = 1.0 (parity "
        "with stock)."
    )
    miss = missing_arm_footnote(clst, plot_arms)
    if miss:
        foot += "\nmissing arms (skipped): " + miss
    foot += "\n[" + skip_footnote(stats) + "]"
    sweeps = sorted({shortname(k[0]) for k, _ in clst})
    finish(
        fig,
        path,
        title,
        subtitle="sweeps: {}".format(", ".join(sweeps)),
        footnote=foot,
    )


# ---------------------------------------------------------------------------
# Chart 2 — factorial cells vs A
# ---------------------------------------------------------------------------


def chart_factorial(records, stats, outdir):
    path = os.path.join(outdir, "factorial_cells.png")
    title = (
        "Factorial cells vs A (ticks_ratio_vs_A = ticks(A)/ticks(arm), "
        ">1 = faster than A)"
    )
    clst = clusters(records)
    if not clst:
        empty_state(path, title, "no rows")
        return
    series = ["B", "C_k2", "D_k2"]
    fig_w = max(9.0, min(24.0, 1.5 * len(clst)))
    fig, axes = new_figure(figsize=(fig_w, 6.0), bottom=0.28, top=0.88)
    ax = axes[0][0]
    style_axes(ax)

    inter = {}
    vmax, vmin = 1.0, 0.0
    for i, (key, arms) in enumerate(clst):
        need = ["A"] + series
        if all(
            a in arms and arms[a]["ticks_ratio_vs_A"] is not None for a in need
        ):
            inter[key] = math.log(
                (arms["B"]["sim_ticks"] * arms["C_k2"]["sim_ticks"])
                / (arms["A"]["sim_ticks"] * arms["D_k2"]["sim_ticks"])
            )
        else:
            inter[key] = None
        for j, arm in enumerate(series):
            rec = arms.get(arm)
            v = None if rec is None else rec["ticks_ratio_vs_A"]
            if v is None:
                continue
            vmax, vmin = max(vmax, v), min(vmin, v)
            x = i + (j - 1) * 0.26
            ax.bar(
                x,
                v,
                width=0.22,
                color=arm_color(arm),
                edgecolor=SURFACE,
                linewidth=0.8,
                zorder=3,
            )
            ax.annotate(
                fmt_num(v),
                (x, v),
                ha="center",
                va="bottom",
                rotation=90,
                fontsize=6.6,
                color=INK2,
                xytext=(0, 1.5),
                textcoords="offset points",
                zorder=4,
            )
    ax.axhline(1.0, color=MUTED, linewidth=1.2, linestyle="--", zorder=2)
    xlabels = []
    for key, _ in clst:
        lab = cluster_label(key)
        if inter[key] is not None:
            lab += "\nI_prep×del = %+.3f" % inter[key]
        else:
            lab += "\nI_prep×del = n/a"
        xlabels.append(lab)
    ax.set_xticks(range(len(clst)))
    ax.set_xticklabels(xlabels, rotation=30, ha="right", fontsize=7.5)
    ax.set_xlim(-0.6, len(clst) - 0.4)
    ax.set_ylim(vmin * 0.9, vmax * 1.30)
    ax.set_ylabel("ticks_ratio_vs_A  (ticks_A / ticks_arm)")
    ax.legend(
        handles=[
            Patch(facecolor=arm_color(a), label="S_%s/A" % a) for a in series
        ],
        loc="upper left",
        fontsize=8,
        frameon=False,
        borderaxespad=0.2,
    )
    foot = (
        "I_prep×del = ln(ticks_B × ticks_C_k2 / (ticks_A × ticks_D_k2)); "
        ">0 = synergy between K-prep and interval delivery.\n"
        "Dashed line = 1.0 (parity with A)."
    )
    miss = [
        cluster_label(k)
        for k, arms in clst
        if any(a not in arms for a in series)
    ]
    if miss:
        foot += "\nmissing cells (bar/I skipped): " + "; ".join(miss)
    foot += "\n[" + skip_footnote(stats) + "]"
    sweeps = sorted({shortname(k[0]) for k, _ in clst})
    finish(
        fig,
        path,
        title,
        subtitle="sweeps: {}".format(", ".join(sweeps)),
        footnote=foot,
    )


# ---------------------------------------------------------------------------
# Chart 3 — latency sensitivity (small multiples)
# ---------------------------------------------------------------------------


def chart_latency(records, stats, outdir):
    path = os.path.join(outdir, "latency_sensitivity.png")
    title = (
        "Latency sensitivity: ticks_ratio_vs_A vs latency (S_D_k2/A, S_B/A)"
    )
    clst = clusters(records)
    series = ["D_k2", "B"]
    data = {}  # sweep -> bench -> lat -> {arm: ratio}
    for key, arms in clst:
        sweep, bench, lat = key
        for arm in series:
            rec = arms.get(arm)
            if rec is not None and rec["ticks_ratio_vs_A"] is not None:
                data.setdefault(sweep, {}).setdefault(bench, {}).setdefault(
                    lat, {}
                )[arm] = rec["ticks_ratio_vs_A"]
    benches = sorted({b for sweep in data for b in data[sweep]})
    sweeps = sorted(data)
    if not benches:
        empty_state(path, title, "no D_k2/B rows with a computable A ratio")
        return
    ncol = max(len(sweeps), 1)
    nrow = len(benches)
    fig, axes = new_figure(
        nrow,
        ncol,
        figsize=(max(5.2, 3.6 * ncol), max(3.0, 2.5 * nrow)),
        wspace=0.28,
        hspace=0.5,
        bottom=0.14,
        top=0.86,
    )
    for r, bench in enumerate(benches):
        for c, sweep in enumerate(sweeps):
            ax = axes[r][c]
            style_axes(ax)
            pts = data.get(sweep, {}).get(bench, {})
            if not pts:
                ax.axis("off")
                ax.text(
                    0.5,
                    0.5,
                    "no data for this sweep/benchmark",
                    ha="center",
                    va="center",
                    fontsize=8,
                    color=MUTED,
                    transform=ax.transAxes,
                )
                continue
            lo, hi = 1.0, 1.0
            for si, arm in enumerate(series):
                xs = sorted(l for l in pts if arm in pts[l])
                ys = [pts[l][arm] for l in xs]
                if not xs:
                    continue
                lo, hi = min(lo, *ys), max(hi, *ys)
                ax.plot(
                    xs,
                    ys,
                    color=arm_color(arm),
                    linewidth=2,
                    solid_capstyle="round",
                    zorder=3,
                    marker="o",
                    markersize=7,
                    markeredgecolor=SURFACE,
                    markeredgewidth=2,
                )
                if len(xs) == 1:
                    # Single latency: label the point (offset per series to
                    # avoid collisions when lines overlap).
                    dy = 8 if si == 0 else -14
                    va = "bottom" if si == 0 else "top"
                    ax.annotate(
                        fmt_num(ys[0]),
                        (xs[0], ys[0]),
                        ha="center",
                        va=va,
                        fontsize=7,
                        color=INK2,
                        xytext=(0, dy),
                        textcoords="offset points",
                    )
            ax.axhline(
                1.0, color=MUTED, linewidth=1.0, linestyle="--", zorder=2
            )
            lats_sweep = sorted(
                {lat for latd in data.get(sweep, {}).values() for lat in latd}
            )
            ax.set_xscale("log")
            ax.set_xticks(lats_sweep)
            ax.set_xticklabels([str(l) for l in lats_sweep])
            ax.minorticks_off()
            ax.set_xlim(min(lats_sweep) * 0.75, max(lats_sweep) * 1.4)
            ax.set_ylim(lo * 0.92, hi * 1.12)
            ax.set_title(
                "{} — {}".format(shortname(sweep), bench), fontsize=9, color=INK2
            )
            if r == nrow - 1:
                ax.set_xlabel("latency (cycles)", fontsize=8)
            if c == 0:
                ax.set_ylabel("ticks_ratio_vs_A", fontsize=8)
    handles = [
        Line2D(
            [],
            [],
            color=arm_color(a),
            linewidth=2,
            marker="o",
            markersize=7,
            markeredgecolor=SURFACE,
            markeredgewidth=2,
            label="S_%s/A" % a,
        )
        for a in series
    ]
    fig.legend(
        handles=handles,
        loc="upper right",
        fontsize=8,
        frameon=False,
        ncol=2,
        bbox_to_anchor=(0.99, 0.99),
    )
    foot = (
        "x-axis log scale; one latency = single marker with value "
        "label. Dashed line = 1.0 (parity with A)."
    )
    miss = [
        cluster_label(k)
        for k, arms in clst
        if any(a not in arms for a in series)
    ]
    if miss:
        foot += "\nmissing (skipped): " + "; ".join(miss)
    foot += "\n[" + skip_footnote(stats) + "]"
    finish(fig, path, title, footnote=foot)


# ---------------------------------------------------------------------------
# Chart 4 — mechanism activity
# ---------------------------------------------------------------------------


def chart_ipactivity(records, stats, outdir):
    path = os.path.join(outdir, "ipactivity.png")
    title = (
        "Mechanism activity per arm (log scale): pf_issued, "
        "ibatch_completed, prep_segments_advanced"
    )
    clst = clusters(records)
    metrics = ["pf_issued", "ibatch_completed", "prep_segments_advanced"]
    groups = [
        (k, arms)
        for k, arms in clst
        if any((arms[a].get(m) or 0) > 0 for a in arms for m in metrics)
    ]
    if not groups:
        empty_state(path, title, "all metric counts are zero")
        return
    ncol = min(3, len(groups))
    nrow = math.ceil(len(groups) / float(ncol))
    pw, ph = 4.6, 3.0
    fig, axes = new_figure(
        nrow,
        ncol,
        figsize=(pw * ncol, ph * nrow),
        wspace=0.30,
        hspace=0.55,
        bottom=0.13,
        top=0.90,
    )
    for idx, (key, arms) in enumerate(groups):
        ax = axes[idx // ncol][idx % ncol]
        style_axes(ax)
        arm_names = sorted(arms, key=pr.arm_sort_key)
        m = len(metrics)
        sub = 1.0 / m  # one x-unit per arm, split by metric
        bar_w = sub * 0.62
        vals_all = []
        for i, arm in enumerate(arm_names):
            rec = arms[arm]
            nz = [
                (j, rec.get(met))
                for j, met in enumerate(metrics)
                if (rec.get(met) or 0) > 0
            ]
            for v in (v for _, v in nz):
                vals_all.append(v)
            if not nz:
                # All-zero arm: a single "0" marker in the slot, no bars.
                ax.annotate(
                    "0",
                    (i, 0.6),
                    ha="center",
                    va="bottom",
                    rotation=90,
                    fontsize=5.6,
                    color=MUTED,
                    xytext=(0, 1),
                    textcoords="offset points",
                    zorder=4,
                )
                continue
            for j, met in enumerate(metrics):
                v = rec.get(met)
                if not v:
                    continue
                x = i + (j - (m - 1) / 2.0) * sub
                ax.bar(
                    x,
                    v,
                    width=bar_w,
                    color=_METRIC_COLORS[met],
                    edgecolor=SURFACE,
                    linewidth=0.6,
                    zorder=3,
                )
                ax.annotate(
                    fmt_num(v),
                    (x, v),
                    ha="center",
                    va="bottom",
                    rotation=90,
                    fontsize=5.6,
                    color=INK2,
                    xytext=(0, 1),
                    textcoords="offset points",
                    zorder=4,
                )
        ax.set_yscale("log")
        ax.set_ylim(max(0.4, min(vals_all) * 0.35), max(vals_all) * 40)
        ax.set_xticks(range(len(arm_names)))
        ax.set_xticklabels(arm_names, rotation=60, ha="right", fontsize=6.5)
        ax.set_xlim(-0.6, len(arm_names) - 0.4)
        ax.set_title(cluster_label(key), fontsize=8.5, color=INK2)
        if idx % ncol == 0:
            ax.set_ylabel("count (log)", fontsize=8)
    for idx in range(len(groups), nrow * ncol):
        axes[idx // ncol][idx % ncol].axis("off")
    handles = [Patch(facecolor=_METRIC_COLORS[m], label=m) for m in metrics]
    fig.legend(
        handles=handles,
        loc="upper right",
        fontsize=8,
        frameon=False,
        ncol=3,
        bbox_to_anchor=(0.99, 0.975),
    )
    foot = (
        "Bars: pf_issued (blue), ibatch_completed (orange), "
        "prep_segments_advanced (aqua); only nonzero values draw a bar,\n"
        "arms with all-zero metrics are labeled 0. Log y-axis per panel. "
        "stock/prefetch arms have no iBatch/prep machinery by "
        "construction."
    )
    foot += "\n[" + skip_footnote(stats) + "]"
    finish(fig, path, title, footnote=foot)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Plot graphs from verify_compass sweep CSVs."
    )
    ap.add_argument(
        "--inputs",
        required=True,
        help="comma-separated sweep output dirs, each containing "
        "verify_results.csv",
    )
    ap.add_argument("--out", required=True, help="output directory for PNGs")
    args = ap.parse_args(argv)

    indirs = [d for d in (s.strip() for s in args.inputs.split(",")) if d]
    if not indirs:
        sys.stderr.write("plot_results: no input dirs given\n")
        return 2
    os.makedirs(args.out, exist_ok=True)

    records, stats = load(indirs)
    chart_speedup(records, stats, args.out)
    chart_factorial(records, stats, args.out)
    chart_latency(records, stats, args.out)
    chart_ipactivity(records, stats, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
