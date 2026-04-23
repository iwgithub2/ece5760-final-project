#!/usr/bin/env python3
"""Create summary plots from fixed-point scoring experiment CSVs."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as csv_file:
        return list(csv.DictReader(csv_file))


def f(row: dict[str, str], key: str) -> float:
    return float(row[key])


def i(row: dict[str, str], key: str) -> int:
    return int(float(row[key]))


def config_label(row: dict[str, str]) -> str:
    return (
        f"{row['mode'].replace('fixed-', '')}\n"
        f"Q{i(row, 'total_bits')}.{i(row, 'frac_bits')}\n"
        f"{i(row, 'lut_entries')}e [{f(row, 'lut_min'):g},{f(row, 'lut_max'):g}]"
    )


def plot_format_precision(rows: list[dict[str, str]], out_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(10, 6))
    colors = {16: "#764c31", 20: "#426b3f", 24: "#315f72", 32: "#5e4b7e"}
    for total in sorted({i(row, "total_bits") for row in rows}):
        subset = [row for row in rows if i(row, "total_bits") == total]
        subset.sort(key=lambda row: i(row, "frac_bits"))
        ax.semilogy(
            [i(row, "frac_bits") for row in subset],
            [f(row, "rmse") for row in subset],
            marker="o",
            linewidth=2,
            color=colors.get(total),
            label=f"{total}-bit datapath",
        )
    ax.set_title("Fixed-point precision sweep")
    ax.set_xlabel("Fractional bits")
    ax.set_ylabel("RMSE vs floating log-add")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def plot_lut_tradeoff(rows: list[dict[str, str]], frontier: list[dict[str, str]], out_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(10, 6))
    for mode, marker, color in [
        ("fixed-piecewise", "o", "#315f72"),
        ("fixed-clamp", "x", "#b45d4c"),
    ]:
        subset = [row for row in rows if row["mode"] == mode]
        ax.scatter(
            [i(row, "lut_memory_bits") for row in subset],
            [f(row, "rmse") for row in subset],
            marker=marker,
            s=60,
            alpha=0.75,
            color=color,
            label=mode,
        )

    if frontier:
        frontier = sorted(frontier, key=lambda row: i(row, "lut_memory_bits"))
        ax.plot(
            [i(row, "lut_memory_bits") for row in frontier],
            [f(row, "rmse") for row in frontier],
            color="black",
            linewidth=2,
            label="Pareto frontier",
        )

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_title("LUT memory vs approximation error")
    ax.set_xlabel("LUT storage bits")
    ax.set_ylabel("RMSE vs floating log-add")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def plot_mcmc_impact(rows: list[dict[str, str]], out_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(12, 6))
    rows = sorted(rows, key=lambda row: f(row, "same_order_mae"))
    x = list(range(len(rows)))
    bars = ax.bar(x, [f(row, "same_order_mae") for row in rows], color="#426b3f")
    ax.set_yscale("log")
    ax.set_ylabel("Same-order score MAE vs float")
    ax.set_title("System-level MCMC impact")
    ax.set_xticks(x)
    ax.set_xticklabels([config_label(row) for row in rows], rotation=25, ha="right")
    ax.grid(True, axis="y", which="both", alpha=0.25)

    for bar, row in zip(bars, rows, strict=True):
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height() * 1.12,
            f"accept diff {i(row, 'accept_decision_diff')}",
            ha="center",
            va="bottom",
            fontsize=8,
            rotation=90,
        )

    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def plot_x_distribution(path: Path, out_path: Path) -> None:
    values = [float(line.strip()) for line in path.read_text().splitlines() if line.strip()]
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.hist(values, bins=220, color="#315f72", alpha=0.9)
    ax.axvspan(-8, 8, color="#d89c4a", alpha=0.25, label="recommended LUT range [-8, 8]")
    ax.axvline(-16, color="#b45d4c", linestyle="--", linewidth=1.5, label="-16")
    ax.axvline(16, color="#b45d4c", linestyle="--", linewidth=1.5, label="+16")
    ax.set_xscale("symlog", linthresh=10)
    ax.set_yscale("log")
    ax.set_title("Observed MCMC log-add input distribution")
    ax.set_xlabel("x = local_score - node_score, symlog scale")
    ax.set_ylabel("Count, log scale")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def plot_dashboard(
    format_rows: list[dict[str, str]],
    lut_rows: list[dict[str, str]],
    mcmc_rows: list[dict[str, str]],
    x_values_path: Path,
    out_path: Path,
) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(16, 11))
    fig.suptitle("Fixed-point Bayesian-network MCMC scoring summary", fontsize=16)

    ax = axes[0, 0]
    for total in sorted({i(row, "total_bits") for row in format_rows}):
        subset = [row for row in format_rows if i(row, "total_bits") == total]
        subset.sort(key=lambda row: i(row, "frac_bits"))
        ax.semilogy(
            [i(row, "frac_bits") for row in subset],
            [f(row, "rmse") for row in subset],
            marker="o",
            label=f"{total}-bit",
        )
    ax.set_title("A: format sweep")
    ax.set_xlabel("Fractional bits")
    ax.set_ylabel("RMSE")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()

    ax = axes[0, 1]
    for mode, marker in [("fixed-piecewise", "o"), ("fixed-clamp", "x")]:
        subset = [row for row in lut_rows if row["mode"] == mode]
        ax.scatter(
            [i(row, "lut_memory_bits") for row in subset],
            [f(row, "rmse") for row in subset],
            marker=marker,
            s=50,
            alpha=0.75,
            label=mode,
        )
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_title("B/C: LUT tradeoff")
    ax.set_xlabel("LUT storage bits")
    ax.set_ylabel("RMSE")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()

    ax = axes[1, 0]
    mcmc_rows = sorted(mcmc_rows, key=lambda row: f(row, "same_order_mae"))
    labels = [f"Q{i(row, 'total_bits')}.{i(row, 'frac_bits')}\n{i(row, 'lut_entries')}e" for row in mcmc_rows]
    ax.bar(range(len(mcmc_rows)), [f(row, "same_order_mae") for row in mcmc_rows], color="#426b3f")
    ax.set_yscale("log")
    ax.set_title("D: MCMC score drift")
    ax.set_ylabel("Same-order score MAE")
    ax.set_xticks(range(len(mcmc_rows)))
    ax.set_xticklabels(labels, fontsize=8)
    ax.grid(True, axis="y", which="both", alpha=0.25)

    ax = axes[1, 1]
    values = [float(line.strip()) for line in x_values_path.read_text().splitlines() if line.strip()]
    ax.hist(values, bins=220, color="#315f72")
    ax.axvspan(-8, 8, color="#d89c4a", alpha=0.25, label="LUT [-8, 8]")
    ax.set_xscale("symlog", linthresh=10)
    ax.set_yscale("log")
    ax.set_title("Observed x distribution")
    ax.set_xlabel("x = local_score - node_score")
    ax.set_ylabel("Count")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=ROOT / "fx_results")
    args = parser.parse_args()

    results = args.results_dir
    format_rows = read_csv(results / "family_a_format_sweep.csv")
    lut_rows = read_csv(results / "family_b_lut_sweep.csv")
    mcmc_rows = read_csv(results / "family_d_mcmc_impact.csv")
    frontier_rows = read_csv(results / "pareto_frontier.csv")
    x_values_path = results / "mcmc_x_values.csv"

    plot_format_precision(format_rows, results / "plot_format_precision.png")
    plot_lut_tradeoff(lut_rows, frontier_rows, results / "plot_lut_tradeoff.png")
    plot_mcmc_impact(mcmc_rows, results / "plot_mcmc_impact.png")
    plot_x_distribution(x_values_path, results / "plot_x_distribution_symlog.png")
    plot_dashboard(format_rows, lut_rows, mcmc_rows, x_values_path, results / "plot_summary_dashboard.png")

    print(f"plots written to {results}")


if __name__ == "__main__":
    main()
