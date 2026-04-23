#!/usr/bin/env python3
"""Generate fixed-point log(1+exp(x)) LUT contents for RTL."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def log1pexp(x: float) -> float:
    if x > 0.0:
        return x + math.log1p(math.exp(-x))
    return math.log1p(math.exp(x))


def quantize_signed(value: float, total_bits: int, int_bits: int) -> int:
    frac_bits = total_bits - int_bits - 1
    if frac_bits < 0:
        raise ValueError("int_bits must leave room for sign bit")
    raw = round(value * (1 << frac_bits))
    min_raw = -(1 << (total_bits - 1))
    max_raw = (1 << (total_bits - 1)) - 1
    return max(min(raw, max_raw), min_raw)


def twos_complement(raw: int, total_bits: int) -> int:
    return raw & ((1 << total_bits) - 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entries", type=int, default=1024)
    parser.add_argument("--lut-min", type=float, default=-8.0)
    parser.add_argument("--lut-max", type=float, default=8.0)
    parser.add_argument("--total", type=int, default=24)
    parser.add_argument("--int", dest="int_bits", type=int, default=16)
    parser.add_argument("--hex-out", type=Path, default=Path("logadd_lut.hex"))
    parser.add_argument("--csv-out", type=Path)
    args = parser.parse_args()

    width_hex_digits = (args.total + 3) // 4
    rows: list[tuple[int, float, float, int, int]] = []
    with args.hex_out.open("w") as hex_file:
        for addr in range(args.entries):
            x = args.lut_min + (args.lut_max - args.lut_min) * addr / (args.entries - 1)
            y = log1pexp(x)
            raw = quantize_signed(y, args.total, args.int_bits)
            encoded = twos_complement(raw, args.total)
            hex_file.write(f"{encoded:0{width_hex_digits}x}\n")
            rows.append((addr, x, y, raw, encoded))

    if args.csv_out:
        with args.csv_out.open("w", newline="") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(["addr", "x", "log1pexp", "signed_raw", "twos_complement"])
            writer.writerows(rows)

    print(
        f"wrote {args.entries} entries to {args.hex_out} "
        f"(Q{args.total}.{args.total - args.int_bits - 1}, range=[{args.lut_min},{args.lut_max}])"
    )


if __name__ == "__main__":
    main()
