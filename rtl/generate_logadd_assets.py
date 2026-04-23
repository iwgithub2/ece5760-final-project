#!/usr/bin/env python3
"""Generate LUT contents and self-checking vectors for the piecewise log-add RTL."""

from __future__ import annotations

import math
import random
from pathlib import Path


DATA_W = 24
INT_BITS = 16
FRAC_BITS = DATA_W - INT_BITS - 1
ENTRIES = 1024
LUT_MIN = -8.0
LUT_MAX = 8.0
LUT_CASES = 64
UPDATE_CASES = 48
CHAIN_CASES = 40
CHAIN_INPUTS = 4
ROOT = Path(__file__).resolve().parent
BANK_SIZE = 256

MIN_RAW = -(1 << (DATA_W - 1))
MAX_RAW = (1 << (DATA_W - 1)) - 1
NEG_BOUND_RAW = int(LUT_MIN * (1 << FRAC_BITS))
POS_BOUND_RAW = int(LUT_MAX * (1 << FRAC_BITS))
STEP_SHIFT = FRAC_BITS + 4 - int(math.log2(ENTRIES))


def sat(raw: int) -> int:
    return max(MIN_RAW, min(MAX_RAW, raw))


def real_to_raw(value: float) -> int:
    return sat(round(value * (1 << FRAC_BITS)))


def raw_to_real(raw: int) -> float:
    return raw / float(1 << FRAC_BITS)


def twos(raw: int, width: int = DATA_W) -> str:
    return f"{raw & ((1 << width) - 1):0{(width + 3) // 4}x}"


def log1pexp(x: float) -> float:
    if x > 0.0:
        return x + math.log1p(math.exp(-x))
    return math.log1p(math.exp(x))


def build_lut_values() -> list[int]:
    return [
        real_to_raw(log1pexp(LUT_MIN + (LUT_MAX - LUT_MIN) * idx / ENTRIES))
        for idx in range(ENTRIES)
    ]


def softplus_piecewise(x_raw: int, lut: list[int]) -> int:
    if x_raw <= NEG_BOUND_RAW:
        return 0
    if x_raw >= POS_BOUND_RAW:
        return x_raw
    addr = (x_raw - NEG_BOUND_RAW) >> STEP_SHIFT
    return lut[addr]


def score_update(score_cur: int, ls_next: int, lut: list[int]) -> int:
    x_raw = sat(ls_next - score_cur)
    return sat(score_cur + softplus_piecewise(x_raw, lut))


def directed_lut_cases() -> list[int]:
    reals = [
        -200.0,
        -20.0,
        -8.5,
        -8.0,
        -7.9921875,
        -7.5,
        -1.0,
        -0.5,
        0.0,
        0.5,
        1.0,
        7.5,
        7.984375,
        8.0,
        8.5,
        200.0,
    ]
    return [real_to_raw(v) for v in reals]


def directed_update_cases() -> list[tuple[int, int]]:
    reals = [
        (-22000.0, -22001.0),
        (-22000.0, -21999.5),
        (-22000.0, -22008.0),
        (-22000.0, -21992.0),
        (-120.0, -136.0),
        (-120.0, -128.0),
        (-120.0, -120.0),
        (-120.0, -112.0),
        (-120.0, -104.0),
        (32760.0, 32767.0),
        (-32768.0, -32760.0),
        (100.0, -4000.0),
        (-4000.0, 100.0),
        (-40.0, -41.0),
        (-40.0, -32.0),
        (-40.0, -24.0),
    ]
    return [(real_to_raw(a), real_to_raw(b)) for a, b in reals]


def directed_chain_cases() -> list[list[int]]:
    real_cases = [
        [-22000.0, -22001.0, -21999.5, -22002.0],
        [-120.0, -128.0, -112.0, -104.0],
        [-512.0, -520.0, -540.0, -400.0],
        [120.0, -4000.0, -3992.0, -3984.0],
        [-32768.0, -32760.0, -32700.0, -32650.0],
        [32000.0, 32760.0, 32767.0, 32000.0],
        [-8.0, -7.0, 0.0, 8.0],
        [-50.0, 100.0, -200.0, 300.0],
    ]
    return [[real_to_raw(v) for v in case] for case in real_cases]


def random_lut_cases(rng: random.Random, count: int) -> list[int]:
    cases: list[int] = []
    for _ in range(count):
        cases.append(real_to_raw(rng.choice([
            rng.uniform(-32.0, 32.0),
            rng.uniform(-4000.0, 4000.0),
            rng.uniform(-32768.0, 32767.0),
        ])))
    return cases


def random_update_cases(rng: random.Random, count: int) -> list[tuple[int, int]]:
    cases: list[tuple[int, int]] = []
    for _ in range(count):
        score_cur = real_to_raw(rng.uniform(-24000.0, 24000.0))
        delta = rng.choice([
            rng.uniform(-40.0, 40.0),
            rng.uniform(-8.5, 8.5),
            rng.uniform(-3000.0, 3000.0),
        ])
        ls_next = real_to_raw(raw_to_real(score_cur) + delta)
        cases.append((score_cur, ls_next))
    return cases


def random_chain_cases(rng: random.Random, count: int) -> list[list[int]]:
    cases: list[list[int]] = []
    for _ in range(count):
        base = rng.uniform(-24000.0, 24000.0)
        values = [
            real_to_raw(base + rng.uniform(-16.0, 16.0)),
            real_to_raw(base + rng.uniform(-8.0, 8.0)),
            real_to_raw(base + rng.uniform(-4.0, 4.0)),
            real_to_raw(base + rng.uniform(-32.0, 32.0)),
        ]
        cases.append(values)
    return cases


def write_lut(path: Path, lut_values: list[int]) -> None:
    with path.open("w") as hex_file:
        for raw in lut_values:
            hex_file.write(f"{twos(raw)}\n")


def write_case_bank(path: Path, lut_values: list[int], bank_idx: int) -> None:
    start = bank_idx * BANK_SIZE
    stop = start + BANK_SIZE
    with path.open("w") as svh_file:
        for local_idx, raw in enumerate(lut_values[start:stop]):
            svh_file.write(f"                    8'd{local_idx}: lut_data = $signed(24'h{twos(raw)});\n")


def write_lut_vectors(path: Path, lut_values: list[int], cases: list[int]) -> None:
    with path.open("w") as hex_file:
        for x_raw in cases:
            expected = softplus_piecewise(x_raw, lut_values)
            hex_file.write(f"{twos(x_raw)}{twos(expected)}\n")


def write_update_vectors(path: Path, lut_values: list[int], cases: list[tuple[int, int]]) -> None:
    with path.open("w") as hex_file:
        for score_cur, ls_next in cases:
            expected = score_update(score_cur, ls_next, lut_values)
            hex_file.write(f"{twos(score_cur)}{twos(ls_next)}{twos(expected)}\n")


def write_chain_vectors(path: Path, lut_values: list[int], cases: list[list[int]]) -> None:
    with path.open("w") as hex_file:
        for values in cases:
            acc = values[0]
            for nxt in values[1:]:
                acc = score_update(acc, nxt, lut_values)
            hex_file.write(f"{''.join(twos(v) for v in values)}{twos(acc)}\n")


def main() -> None:
    rng = random.Random(7)
    lut_values = build_lut_values()
    lut_cases = directed_lut_cases() + random_lut_cases(rng, LUT_CASES - len(directed_lut_cases()))
    update_cases = directed_update_cases() + random_update_cases(rng, UPDATE_CASES - len(directed_update_cases()))
    chain_cases = directed_chain_cases() + random_chain_cases(rng, CHAIN_CASES - len(directed_chain_cases()))

    write_lut(ROOT / "logadd_q24_i16_1024_m8_8.hex", lut_values)
    for bank_idx in range(ENTRIES // BANK_SIZE):
        write_case_bank(ROOT / f"logadd_q24_i16_1024_m8_8_bank{bank_idx}.svh", lut_values, bank_idx)
    write_lut_vectors(ROOT / "logadd_lut_vectors.hex", lut_values, lut_cases)
    write_update_vectors(ROOT / "logadd_update_vectors.hex", lut_values, update_cases)
    write_chain_vectors(ROOT / "logadd_chain_vectors.hex", lut_values, chain_cases)
    print("Generated LUT contents and test vectors for log-add RTL.")


if __name__ == "__main__":
    main()
