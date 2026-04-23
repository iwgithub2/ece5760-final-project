#!/usr/bin/env python3
"""Run fixed-point/LUT experiments for the C MCMC scoring path."""

from __future__ import annotations

import argparse
import csv
import math
import subprocess
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent


def run_cmd(args: list[str]) -> str:
    completed = subprocess.run(args, cwd=ROOT, check=True, text=True, capture_output=True)
    return completed.stdout.strip()


def parse_kv_line(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for token in line.split():
        if "=" in token:
            key, value = token.split("=", 1)
            out[key] = value
    return out


def run_block_config(
    mode: str,
    total: int,
    int_bits: int,
    entries: int,
    lut_min: float,
    lut_max: float,
    csv_header: bool = False,
) -> dict[str, str]:
    args = [
        "./fixed_point_experiment",
        "--mode",
        mode,
        "--total",
        str(total),
        "--int",
        str(int_bits),
        "--lut-total",
        str(total),
        "--lut-int",
        str(int_bits),
        "--entries",
        str(entries),
        "--lut-min",
        str(lut_min),
        "--lut-max",
        str(lut_max),
    ]
    if csv_header:
        args.append("--csv-header")

    lines = run_cmd(args).splitlines()
    if csv_header:
        reader = csv.DictReader(lines)
        return next(reader)
    header = [
        "mode",
        "total_bits",
        "int_bits",
        "frac_bits",
        "lut_total_bits",
        "lut_int_bits",
        "lut_frac_bits",
        "lut_min",
        "lut_max",
        "lut_entries",
        "lut_address_width",
        "lut_memory_bits",
        "max_abs_error",
        "mean_abs_error",
        "rmse",
        "max_relative_error",
        "overflow_count",
        "saturation_count",
        "lut_low_count",
        "lut_high_count",
        "table_saturation_count",
        "monotonic_issues",
        "repeated_accum_abs_error",
    ]
    return dict(zip(header, lines[-1].split(","), strict=True))


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    if not rows:
        return
    with path.open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def as_float(row: dict[str, str], key: str) -> float:
    return float(row[key])


def family_a(out_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    splits = {
        32: [8, 12, 16, 20, 24],
        24: [8, 12, 14, 16, 18, 20],
        20: [8, 10, 12, 14, 16, 18],
        16: [6, 8, 10, 12, 14, 15],
    }
    for total, int_options in splits.items():
        for int_bits in int_options:
            if int_bits >= total:
                continue
            rows.append(run_block_config("fixed-piecewise", total, int_bits, 512, -16.0, 8.0))

    write_rows(out_dir / "family_a_format_sweep.csv", rows)
    fig, ax = plt.subplots(figsize=(10, 6))
    for total in sorted({int(row["total_bits"]) for row in rows}):
        subset = [row for row in rows if int(row["total_bits"]) == total]
        subset.sort(key=lambda row: int(row["frac_bits"]))
        ax.semilogy(
            [int(row["frac_bits"]) for row in subset],
            [as_float(row, "rmse") for row in subset],
            marker="o",
            label=f"{total}-bit",
        )
    ax.set_xlabel("Fractional bits")
    ax.set_ylabel("RMSE vs floating reference")
    ax.set_title("Family A: fixed-point format sweep, piecewise LUT [-16, 8], 512 entries")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "family_a_format_sweep.png", dpi=160)
    plt.close(fig)
    return rows


def family_b(out_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    ranges = [(-24.0, 8.0), (-16.0, 8.0), (-16.0, 0.0), (-12.0, 8.0), (-8.0, 8.0)]
    entries_list = [64, 128, 256, 512, 1024]
    for mode in ["fixed-piecewise", "fixed-clamp"]:
        for lut_min, lut_max in ranges:
            for entries in entries_list:
                rows.append(run_block_config(mode, 24, 16, entries, lut_min, lut_max))

    write_rows(out_dir / "family_b_lut_sweep.csv", rows)
    fig, ax = plt.subplots(figsize=(10, 6))
    for mode in ["fixed-piecewise", "fixed-clamp"]:
        for lut_min, lut_max in [(-16.0, 8.0), (-16.0, 0.0)]:
            subset = [
                row
                for row in rows
                if row["mode"] == mode and float(row["lut_min"]) == lut_min and float(row["lut_max"]) == lut_max
            ]
            subset.sort(key=lambda row: int(row["lut_entries"]))
            ax.loglog(
                [int(row["lut_memory_bits"]) for row in subset],
                [as_float(row, "rmse") for row in subset],
                marker="o",
                label=f"{mode} [{lut_min:g},{lut_max:g}]",
            )
    ax.set_xlabel("LUT storage bits")
    ax.set_ylabel("RMSE vs floating reference")
    ax.set_title("Family B: LUT size/range sweep, Q24.7")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_dir / "family_b_lut_sweep.png", dpi=160)
    plt.close(fig)
    return rows


def pareto_frontier(rows: list[dict[str, str]], out_dir: Path) -> list[dict[str, str]]:
    candidates = [
        row
        for row in rows
        if int(row["overflow_count"]) == 0
        and int(row["monotonic_issues"]) == 0
        and int(row["table_saturation_count"]) == 0
    ]
    frontier: list[dict[str, str]] = []
    for row in candidates:
        memory = int(row["lut_memory_bits"])
        error = as_float(row, "rmse")
        dominated = False
        for other in candidates:
            if other is row:
                continue
            other_memory = int(other["lut_memory_bits"])
            other_error = as_float(other, "rmse")
            if other_memory <= memory and other_error <= error and (other_memory < memory or other_error < error):
                dominated = True
                break
        if not dominated:
            frontier.append(row)

    frontier.sort(key=lambda row: (int(row["lut_memory_bits"]), as_float(row, "rmse")))
    write_rows(out_dir / "pareto_frontier.csv", frontier)
    return frontier


def log_x_distribution(out_dir: Path, iters: int, seed: int) -> dict[str, float]:
    path = out_dir / "mcmc_x_values.csv"
    run_cmd(
        [
            "./mcmc",
            "--mode",
            "float",
            "--iters",
            str(iters),
            "--seed",
            str(seed),
            "--quiet",
            "--x-log",
            str(path),
            "--x-log-limit",
            "250000",
        ]
    )
    values = [float(line.strip()) for line in path.read_text().splitlines() if line.strip()]
    values.sort()
    stats = {
        "count": float(len(values)),
        "min": values[0],
        "p01": values[int(0.01 * (len(values) - 1))],
        "p50": values[int(0.50 * (len(values) - 1))],
        "p99": values[int(0.99 * (len(values) - 1))],
        "max": values[-1],
    }
    with (out_dir / "mcmc_x_distribution_summary.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=list(stats.keys()))
        writer.writeheader()
        writer.writerow(stats)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.hist(values, bins=120, color="#315f72")
    ax.axvline(-16.0, color="#b45d4c", linestyle="--", label="-16 LUT min")
    ax.axvline(8.0, color="#426b3f", linestyle="--", label="8 LUT max")
    ax.set_xlabel("x = ls_next - score_cur")
    ax.set_ylabel("Count")
    ax.set_title("Observed MCMC log-add x distribution")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "mcmc_x_distribution.png", dpi=160)
    plt.close(fig)
    return stats


def family_d(out_dir: Path, iters: int, seed: int) -> list[dict[str, str]]:
    configs = [
        ("fixed-piecewise", 32, 16, 1024, -16.0, 8.0),
        ("fixed-piecewise", 32, 16, 1024, -8.0, 8.0),
        ("fixed-piecewise", 24, 16, 512, -16.0, 8.0),
        ("fixed-piecewise", 24, 16, 512, -8.0, 8.0),
        ("fixed-piecewise", 24, 16, 1024, -8.0, 8.0),
        ("fixed-piecewise", 20, 16, 512, -16.0, 8.0),
        ("fixed-piecewise", 16, 15, 512, -16.0, 8.0),
        ("fixed-clamp", 24, 16, 512, -16.0, 8.0),
    ]
    rows: list[dict[str, str]] = []
    for mode, total, int_bits, entries, lut_min, lut_max in configs:
        output = run_cmd(
            [
                "./mcmc",
                "--compare-mode",
                mode,
                "--iters",
                str(iters),
                "--seed",
                str(seed),
                "--quiet",
                "--fx-total",
                str(total),
                "--fx-int",
                str(int_bits),
                "--lut-total",
                str(total),
                "--lut-int",
                str(int_bits),
                "--lut-min",
                str(lut_min),
                "--lut-max",
                str(lut_max),
                "--lut-entries",
                str(entries),
            ]
        )
        row = parse_kv_line(output)
        row.update(
            {
                "total_bits": str(total),
                "int_bits": str(int_bits),
                "frac_bits": str(total - int_bits - 1),
                "lut_entries": str(entries),
                "lut_min": str(lut_min),
                "lut_max": str(lut_max),
            }
        )
        rows.append(row)

    write_rows(out_dir / "family_d_mcmc_impact.csv", rows)
    fig, ax = plt.subplots(figsize=(10, 6))
    labels = [f"{row['mode']} Q{row['total_bits']}.{row['frac_bits']}" for row in rows]
    ax.bar(labels, [float(row["same_order_mae"]) for row in rows], color="#764c31")
    ax.set_yscale("log")
    ax.set_ylabel("Same-order score MAE vs float")
    ax.set_title(f"Family D: system-level score drift over {iters} MCMC steps")
    ax.tick_params(axis="x", labelrotation=25)
    fig.tight_layout()
    fig.savefig(out_dir / "family_d_mcmc_impact.png", dpi=160)
    plt.close(fig)
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "fx_results")
    parser.add_argument("--mcmc-iters", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    run_cmd(["make"])
    run_cmd(["make", "test"])

    a_rows = family_a(args.out_dir)
    b_rows = family_b(args.out_dir)
    frontier = pareto_frontier(a_rows + b_rows, args.out_dir)
    x_stats = log_x_distribution(args.out_dir, min(args.mcmc_iters, 2000), args.seed)
    d_rows = family_d(args.out_dir, args.mcmc_iters, args.seed)

    print(f"wrote: {args.out_dir}")
    print(f"x distribution count={int(x_stats['count'])} p01={x_stats['p01']:.3f} p99={x_stats['p99']:.3f}")
    if frontier:
        best = min(frontier, key=lambda row: (float(row["rmse"]), int(row["lut_memory_bits"])))
        print(
            "best block RMSE frontier: "
            f"{best['mode']} total={best['total_bits']} int={best['int_bits']} "
            f"frac={best['frac_bits']} entries={best['lut_entries']} "
            f"range=[{best['lut_min']},{best['lut_max']}] rmse={float(best['rmse']):.6g}"
        )
    best_mcmc = min(d_rows, key=lambda row: float(row["same_order_mae"]))
    print(
        "best MCMC score match: "
        f"{best_mcmc['mode']} total={best_mcmc['total_bits']} int={best_mcmc['int_bits']} "
        f"frac={best_mcmc['frac_bits']} same_order_mae={float(best_mcmc['same_order_mae']):.6g} "
        f"same_state_accept_diff={best_mcmc['accept_decision_diff']}"
    )


if __name__ == "__main__":
    main()
