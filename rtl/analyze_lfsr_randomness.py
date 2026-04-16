#!/usr/bin/env python3
"""Quick randomness report for the 32-bit LFSR testbench output.

This is a sanity benchmark, not a replacement for a full NIST/Dieharder test
suite. It catches common hardware RNG mistakes: stuck bits, strong output bias,
obvious low-byte imbalance, repeated short runs, and short-lag correlation.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np


def load_values(csv_path: Path) -> np.ndarray:
    """Load the hex output words written by tb_lsfr.v."""

    values: list[int] = []
    with csv_path.open(newline="") as csv_file:
        reader = csv.DictReader(csv_file)
        if reader.fieldnames is None or "value_hex" not in reader.fieldnames:
            raise ValueError(f"{csv_path} must contain a value_hex column")

        for row in reader:
            values.append(int(row["value_hex"], 16))

    if not values:
        raise ValueError(f"{csv_path} did not contain any LFSR samples")

    return np.asarray(values, dtype=np.uint64)


def unpack_bits(values: np.ndarray) -> np.ndarray:
    """Return shape (num_samples, 32), least-significant bit first."""

    shifts = np.arange(32, dtype=np.uint64)
    return ((values[:, None] >> shifts) & 1).astype(np.uint8)


def lag_autocorrelation(bit_stream: np.ndarray, max_lag: int) -> np.ndarray:
    """Autocorrelation of a 0/1 stream after mapping it to -1/+1."""

    signed = bit_stream.astype(np.float64) * 2.0 - 1.0
    max_lag = min(max_lag, signed.size - 1)
    return np.asarray(
        [np.mean(signed[:-lag] * signed[lag:]) for lag in range(1, max_lag + 1)]
    )


def run_lengths(bit_stream: np.ndarray) -> np.ndarray:
    """Lengths of consecutive equal-bit runs in a flat bit stream."""

    change_points = np.flatnonzero(bit_stream[1:] != bit_stream[:-1]) + 1
    boundaries = np.concatenate(([0], change_points, [bit_stream.size]))
    return np.diff(boundaries)


def compute_stats(values: np.ndarray, max_lag: int, max_run_len: int) -> dict[str, np.ndarray | float]:
    bits = unpack_bits(values)
    flat_bits = bits.reshape(-1)
    low_byte = (values & 0xFF).astype(np.int64)
    low_byte_counts = np.bincount(low_byte, minlength=256)
    expected_low_byte = values.size / 256.0
    chi_square = np.sum((low_byte_counts - expected_low_byte) ** 2 / expected_low_byte)
    runs = run_lengths(flat_bits)

    run_bins = np.arange(1, max_run_len + 1)
    observed_runs = np.asarray([np.sum(runs == length) for length in run_bins])
    expected_runs = runs.size * (0.5 ** run_bins)

    return {
        "bit_fraction": bits.mean(axis=0),
        "overall_fraction": float(flat_bits.mean()),
        "low_byte_counts": low_byte_counts,
        "low_byte_chi_square": float(chi_square),
        "autocorrelation": lag_autocorrelation(flat_bits, max_lag),
        "run_bins": run_bins,
        "observed_runs": observed_runs,
        "expected_runs": expected_runs,
        "longest_run": float(runs.max()),
    }


def plot_report(values: np.ndarray, stats: dict[str, np.ndarray | float], out_path: Path) -> None:
    sample_count = values.size
    bit_count = sample_count * 32
    bit_fraction = stats["bit_fraction"]
    low_byte_counts = stats["low_byte_counts"]
    autocorr = stats["autocorrelation"]
    run_bins = stats["run_bins"]
    observed_runs = stats["observed_runs"]
    expected_runs = stats["expected_runs"]

    fig, axes = plt.subplots(2, 2, figsize=(15, 10))
    fig.suptitle(f"32-bit LFSR randomness quick report ({sample_count:,} words)")

    ax = axes[0, 0]
    sigma = math.sqrt(0.25 / sample_count)
    x_bits = np.arange(32)
    ax.bar(x_bits, bit_fraction, color="#315f72")
    ax.axhline(0.5, color="black", linewidth=1, label="ideal 0.5")
    ax.axhspan(0.5 - 3 * sigma, 0.5 + 3 * sigma, color="#d89c4a", alpha=0.25, label="+/- 3 sigma")
    ymin = min(float(np.min(bit_fraction)), 0.5 - 4 * sigma)
    ymax = max(float(np.max(bit_fraction)), 0.5 + 4 * sigma)
    pad = max(0.005, (ymax - ymin) * 0.25)
    ax.set_ylim(max(0.0, ymin - pad), min(1.0, ymax + pad))
    ax.set_title("Per-bit fraction of ones")
    ax.set_xlabel("Bit index")
    ax.set_ylabel("Fraction ones")
    ax.legend(loc="best")

    ax = axes[0, 1]
    x_byte = np.arange(256)
    expected_byte = sample_count / 256.0
    ax.bar(x_byte, low_byte_counts, width=1.0, color="#426b3f")
    ax.axhline(expected_byte, color="black", linewidth=1, label="uniform expectation")
    ax.set_title("Low-byte distribution")
    ax.set_xlabel("rand_out[7:0]")
    ax.set_ylabel("Count")
    ax.legend(loc="best")

    ax = axes[1, 0]
    lags = np.arange(1, autocorr.size + 1)
    corr_limit = 3.0 / math.sqrt(bit_count)
    ax.axhline(0.0, color="black", linewidth=1)
    ax.axhline(corr_limit, color="#b45d4c", linestyle="--", linewidth=1, label="+/- 3/sqrt(N)")
    ax.axhline(-corr_limit, color="#b45d4c", linestyle="--", linewidth=1)
    ax.vlines(lags, 0.0, autocorr, color="#5e4b7e", linewidth=1.2)
    ax.set_title("Flat bit-stream autocorrelation")
    ax.set_xlabel("Lag in bits")
    ax.set_ylabel("Correlation")
    ax.legend(loc="best")

    ax = axes[1, 1]
    ax.semilogy(run_bins, observed_runs + 1, marker="o", label="observed + 1", color="#764c31")
    ax.semilogy(run_bins, expected_runs + 1, linestyle="--", label="ideal Bernoulli + 1", color="black")
    ax.set_title("Run length distribution")
    ax.set_xlabel("Run length in bits")
    ax.set_ylabel("Run count, log scale")
    ax.legend(loc="best")

    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


def print_summary(values: np.ndarray, stats: dict[str, np.ndarray | float], out_path: Path) -> None:
    bit_fraction = stats["bit_fraction"]
    autocorr = stats["autocorrelation"]
    worst_bit = int(np.argmax(np.abs(bit_fraction - 0.5)))
    worst_lag = int(np.argmax(np.abs(autocorr))) + 1
    reduced_chi = stats["low_byte_chi_square"] / 255.0

    print(f"samples: {values.size}")
    print(f"overall ones fraction: {stats['overall_fraction']:.6f}")
    print(f"worst bit bias: bit{worst_bit} ones={bit_fraction[worst_bit]:.6f}")
    print(f"low-byte chi-square: {stats['low_byte_chi_square']:.2f} (reduced {reduced_chi:.3f})")
    print(f"max abs autocorrelation: {np.max(np.abs(autocorr)):.6f} at lag {worst_lag} bits")
    print(f"longest flat bit-stream run: {int(stats['longest_run'])} bits")
    print(f"plot: {out_path}")


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=script_dir / "lfsr_samples.csv")
    parser.add_argument("--out", type=Path, default=script_dir / "lfsr_randomness_report.png")
    parser.add_argument("--max-lag", type=int, default=64)
    parser.add_argument("--max-run-len", type=int, default=16)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    values = load_values(args.csv)
    stats = compute_stats(values, args.max_lag, args.max_run_len)
    plot_report(values, stats, args.out)
    print_summary(values, stats, args.out)


if __name__ == "__main__":
    main()
