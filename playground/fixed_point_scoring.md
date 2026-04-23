# Fixed-Point Log-Add Scoring

The C MCMC path now supports three scoring backends:

- `float`: original floating-point reference path.
- `fixed-piecewise`: fixed-point LUT for `log(1 + exp(x))`, with `x < min -> 0` and `x > max -> x`.
- `fixed-clamp`: fixed-point LUT with out-of-range inputs clamped to LUT endpoints.

The insertion point is the order-score accumulation:

```text
node_score = node_score + log(1 + exp(local_score - node_score))
```

The original BDe precompute, CSV loading, proposal generation, and edge extraction remain shared.

## Build

```sh
cd playground
make
make test
```

## Run MCMC

```sh
./mcmc --mode float --iters 5000 --seed 7 --quiet
./mcmc --mode fixed-piecewise --iters 5000 --seed 7 --quiet \
  --fx-total 32 --fx-int 16 --lut-total 32 --lut-int 16 \
  --lut-min -8 --lut-max 8 --lut-entries 1024
```

Compare fixed-point against the float path on the same proposal stream:

```sh
./mcmc --compare-mode fixed-piecewise --iters 5000 --seed 7 --quiet \
  --fx-total 32 --fx-int 16 --lut-total 32 --lut-int 16 \
  --lut-min -8 --lut-max 8 --lut-entries 1024
```

## Experiments

```sh
uv run python run_fixed_point_experiments.py --mcmc-iters 5000 --seed 7
```

Outputs go to `playground/fx_results/`:

- `family_a_format_sweep.csv/png`: fixed-point width and integer/fraction split sweep.
- `family_b_lut_sweep.csv/png`: LUT range and entry-count sweep.
- `family_d_mcmc_impact.csv/png`: paired MCMC impact.
- `mcmc_x_values.csv` and `mcmc_x_distribution.png`: observed `x = local_score - node_score`.
- `pareto_frontier.csv`: non-dominated LUT/error points.

## Current Result

On the Asia dataset, observed `x` values span roughly `[-2665, 2533]`. A pure clamped LUT is therefore not appropriate: large positive `x` values must use the piecewise `output ~= x` path, and large negative values can safely use `output ~= 0`.

Current best system-level match:

```text
fixed-piecewise, Q32 with 16 integer bits and 15 fractional bits,
LUT range [-8, 8], 1024 entries.
```

For a smaller table/storage point, Q24 with 16 integer bits and 7 fractional bits, range `[-8, 8]`, and 512 or 1024 entries is the useful next RTL candidate. Q20 and Q16 run, but the reduced fractional precision starts changing MCMC behavior.
