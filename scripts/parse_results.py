#!/usr/bin/env python3
"""Parse gem5 verify_compass sweep CSVs into one tidy results table + summary.

Usage:
    python3 scripts/parse_results.py --inputs DIR1[,DIR2...] --out OUTDIR

Reads verify_results.csv from each input dir (as written by
scripts/verify_compass.py), keeps only return_code==0 rows, computes
speedup_vs_stock = stock_ticks / ticks and ticks_ratio_vs_A = A_ticks / ticks
within each (sweep_dir, benchmark, latency) group (both >1 means faster than
the comparator), and writes:

    OUTDIR/RESULTS_TIDY.csv
    OUTDIR/SUMMARY.md

Pure python (stdlib only). Deterministic ordering throughout. No hardcoded
benchmark/arm lists: anything present in the CSVs flows through.
"""

import argparse
import csv
import math
import os
import sys

# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

INPUT_CSV = "verify_results.csv"

# Columns of the tidy output (exact order required by the deliverable spec).
TIDY_COLS = [
    "sweep_dir",
    "benchmark",
    "latency",
    "arm",
    "sim_ticks",
    "sim_seconds",
    "sim_insts",
    "ipc",
    "l3_demand_mshr",
    "pf_issued",
    "pf_useful",
    "ibatch_completed",
    "prep_segments_advanced",
    "speedup_vs_stock",
    "ticks_ratio_vs_A",
]

# Numeric columns carried through from the sweep CSV.
NUMERIC_COLS = [
    "sim_ticks",
    "sim_seconds",
    "sim_insts",
    "ipc",
    "l3_demand_mshr",
    "pf_issued",
    "pf_useful",
    "ibatch_completed",
    "prep_segments_advanced",
]

# Canonical arm ordering (controls first, then cells by letter then k).
# Unknown arms sort after these, alphabetically.
_BASE_ARM_ORDER = {"stock": 0, "ipp": 1, "prefetch": 2, "A": 3, "B": 4}


def arm_sort_key(arm):
    """Deterministic sort key for arm names, no hardcoded benchmark lists."""
    if arm in _BASE_ARM_ORDER:
        return (0, _BASE_ARM_ORDER[arm], 0, "")
    if "_" in arm:
        cell, _, kpart = arm.rpartition("_k")
        try:
            k = int(kpart)
        except ValueError:
            return (2, 0, 0, arm)
        return (
            1,
            {"C": 0}.get(cell, 1 if cell else 2),
            0,
            "%s_%010d" % (cell, k),
        )
    return (2, 0, 0, arm)


def parse_float(tok):
    """Best-effort numeric parse; None if blank/unparseable."""
    try:
        return float(tok)
    except (TypeError, ValueError):
        return None


def load_sweep(indir):
    """Load one sweep dir.

    Returns (records, stats) where records is a list of dicts with keys
    sweep_dir/benchmark/latency/arm/<numeric cols> and stats is a dict with
    keys total / ok / skipped / skipped_detail / missing_file.
    """
    path = os.path.join(indir, INPUT_CSV)
    stats = {
        "sweep_dir": os.path.basename(os.path.abspath(indir)),
        "path": indir,
        "total": 0,
        "ok": 0,
        "skipped": 0,
        "skipped_detail": {},
        "missing_file": False,
    }
    records = []
    if not os.path.isfile(path):
        stats["missing_file"] = True
        return records, stats
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            stats["total"] += 1
            bench = (row.get("benchmark") or "").strip()
            arm = (row.get("arm") or "").strip()
            try:
                lat = int(float((row.get("latency") or "").strip()))
            except (TypeError, ValueError):
                lat = None
            rc = parse_float(row.get("return_code"))
            ticks = parse_float(row.get("sim_ticks"))
            reason = None
            if rc is None or int(rc) != 0:
                reason = "rc={}".format(row.get("return_code"))
            elif ticks is None:
                reason = "sim_ticks unparseable"
            elif bench == "" or arm == "" or lat is None:
                reason = "missing benchmark/latency/arm"
            if reason is not None:
                stats["skipped"] += 1
                stats["skipped_detail"][reason] = (
                    stats["skipped_detail"].get(reason, 0) + 1
                )
                continue
            rec = {
                "sweep_dir": stats["sweep_dir"],
                "benchmark": bench,
                "latency": lat,
                "arm": arm,
            }
            for col in NUMERIC_COLS:
                rec[col] = parse_float(row.get(col))
            records.append(rec)
            stats["ok"] += 1
    return records, stats


def load_sweeps(indirs):
    """Load all sweep dirs -> (records, stats list)."""
    records, stats_list = [], []
    for d in indirs:
        recs, st = load_sweep(d)
        records.extend(recs)
        stats_list.append(st)
        if st["missing_file"]:
            sys.stderr.write(
                "parse_results: WARNING: no {} in {}\n".format(INPUT_CSV, d)
            )
    return records, stats_list


def group_key(rec):
    return (rec["sweep_dir"], rec["benchmark"], rec["latency"])


def compute_ratios(records):
    """Annotate each record with speedup_vs_stock / ticks_ratio_vs_A.

    speedup_vs_stock  = stock_ticks / ticks   (>1 => faster than stock)
    ticks_ratio_vs_A  = A_ticks    / ticks    (>1 => faster than cell A)
    Comparators are looked up within the same (sweep_dir, benchmark, latency).
    Missing comparator -> None.
    """
    ticks = {}
    for rec in records:
        if rec["sim_ticks"] is not None and rec["sim_ticks"] > 0:
            ticks.setdefault(group_key(rec), {})[rec["arm"]] = rec["sim_ticks"]
    for rec in records:
        rec["speedup_vs_stock"] = None
        rec["ticks_ratio_vs_A"] = None
        arms = ticks.get(group_key(rec), {})
        t = rec["sim_ticks"]
        if t is None or t <= 0:
            continue
        stock = arms.get("stock")
        if stock:
            rec["speedup_vs_stock"] = stock / t
        a = arms.get("A")
        if a:
            rec["ticks_ratio_vs_A"] = a / t
    return records


def sorted_records(records):
    """Deterministic global ordering: sweep, benchmark, latency, arm."""
    return sorted(
        records,
        key=lambda r: (
            r["sweep_dir"],
            r["benchmark"],
            r["latency"],
            arm_sort_key(r["arm"]),
        ),
    )


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------


def write_tidy(records, outdir):
    path = os.path.join(outdir, "RESULTS_TIDY.csv")
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(TIDY_COLS)
        for rec in sorted_records(records):
            row = []
            for col in TIDY_COLS:
                v = rec.get(col)
                if col in NUMERIC_COLS:
                    row.append(
                        ""
                        if v is None
                        else ("%d" % v if float(v).is_integer() else "%g" % v)
                    )
                elif col in ("speedup_vs_stock", "ticks_ratio_vs_A"):
                    row.append("" if v is None else "%.6f" % v)
                else:
                    row.append(v)
            w.writerow(row)
    return path


def _fmt(v, nd=6):
    return "n/a" if v is None else ("%.*f" % (nd, v))


def write_summary(records, stats_list, outdir):
    path = os.path.join(outdir, "SUMMARY.md")
    lines = ["# Sweep summary", ""]
    lines.append(
        "Ratios: `ticks_ratio_vs_A = ticks(A) / ticks(arm)` and "
        "`speedup_vs_stock = ticks(stock) / ticks(arm)` — both "
        "**>1 means the arm is faster** than the comparator. Rows "
        "with `return_code != 0` are excluded everywhere."
    )
    lines.append("")

    # Per-sweep row counts.
    lines.append("## Per-sweep row counts")
    lines.append("")
    lines.append("| sweep | ok | skipped (rc!=0/bad) | total |")
    lines.append("|---|---:|---:|---:|")
    for st in stats_list:
        det = "; ".join(
            "%s x%d" % (k, v) for k, v in sorted(st["skipped_detail"].items())
        )
        lines.append(
            "| %s | %d | %d%s | %d |"
            % (
                st["sweep_dir"],
                st["ok"],
                st["skipped"],
                (" (%s)" % det) if det else "",
                st["total"],
            )
        )
    lines.append("")

    # Per (sweep, benchmark, latency) ratio table.
    lines.append("## Ratios per benchmark / latency")
    lines.append("")
    groups = {}
    for rec in records:
        groups.setdefault(group_key(rec), {})[rec["arm"]] = rec
    for key in sorted(groups, key=lambda k: (k[0], k[1], k[2])):
        sweep, bench, lat = key
        arms = groups[key]
        arm_names = sorted(arms, key=arm_sort_key)
        lines.append("### {} — {} @ latency {}".format(sweep, bench, lat))
        lines.append("")
        lines.append(
            "| arm | sim_ticks | speedup_vs_stock | ticks_ratio_vs_A |"
        )
        lines.append("|---|---:|---:|---:|")
        for arm in arm_names:
            rec = arms[arm]
            t = rec["sim_ticks"]
            tstr = (
                "n/a"
                if t is None
                else ("%d" % t if float(t).is_integer() else "%g" % t)
            )
            lines.append(
                "| %s | %s | %s | %s |"
                % (
                    arm,
                    tstr,
                    _fmt(rec["speedup_vs_stock"]),
                    _fmt(rec["ticks_ratio_vs_A"]),
                )
            )
        # Geomean of ticks_ratio_vs_A across cell arms (informational).
        cell = [
            arms[a]["ticks_ratio_vs_A"]
            for a in arm_names
            if arms[a]["ticks_ratio_vs_A"] is not None and a not in ("stock",)
        ]
        if len(cell) > 1:
            gm = math.exp(sum(math.log(v) for v in cell) / len(cell))
            lines.append("")
            lines.append(
                "Geomean of ticks_ratio_vs_A over all non-stock "
                "arms: **%.4f**" % gm
            )
        lines.append("")
    with open(path, "w") as fh:
        fh.write("\n".join(lines))
    return path


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Parse verify_compass sweep CSVs into a tidy table."
    )
    ap.add_argument(
        "--inputs",
        required=True,
        help="comma-separated sweep output dirs, each containing "
        "verify_results.csv",
    )
    ap.add_argument("--out", required=True, help="output directory")
    args = ap.parse_args(argv)

    indirs = [d for d in (s.strip() for s in args.inputs.split(",")) if d]
    if not indirs:
        sys.stderr.write("parse_results: no input dirs given\n")
        return 2
    os.makedirs(args.out, exist_ok=True)

    records, stats_list = load_sweeps(indirs)
    if not records:
        sys.stderr.write("parse_results: ERROR: no usable rows in any input\n")
        return 1
    compute_ratios(records)

    tidy = write_tidy(records, args.out)
    summ = write_summary(records, stats_list, args.out)
    tot_ok = sum(s["ok"] for s in stats_list)
    tot_skip = sum(s["skipped"] for s in stats_list)
    print(
        "wrote %s (%d rows, %d ok, %d skipped)"
        % (tidy, len(records), tot_ok, tot_skip)
    )
    print("wrote %s (%d sweeps)" % (summ, len(stats_list)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
