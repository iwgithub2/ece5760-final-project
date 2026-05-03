# ECE 5760

## Lab 1

## Lab 2

## Lab 3

## Final Project

### BN FPGA Preprocessing

Build deterministic parent-set/local-score tables for the FPGA order/MCMC sampler:

```sh
uv run python preprocess_bn.py \
  --data cleaned-datasets/asia_samples.csv \
  --output-dir out_bn_tables \
  --target-type discrete \
  --max-parent-size 4 \
  --max-candidates-per-node 10 \
  --bootstrap-iters 20 \
  --min-stability-frequency 0.3 \
  --score bdeu \
  --equivalent-sample-size 1.0 \
  --fixed-point q16.16 \
  --emit-hex
```

Outputs:

- `parent_sets.bin`: packed parent-set masks, one `uint64` chunk at a time, little-endian.
- `local_scores.bin`: signed fixed-point log local scores, aligned 1:1 with parent sets.
- `node_offsets.json`: node offsets/counts, variable names, candidate names, mask layout, score scaling.
- `config_used.json`: reproducibility config.
- `readable_debug.csv`: human-readable table dump.
- `parent_sets.hex` and `local_scores.hex`: optional FPGA memory-init text.

This is only the software preprocessing side. It does not implement the FPGA MCMC/order sampler.

The C playground MCMC path can also load `readable_debug.csv` tables:

```sh
cd playground
./mcmc --score-table ../out_bn_tables/readable_debug.csv --iters 50000 --seed 1
```

Candidate-table vs fixed-k experiments:

```sh
uv run python playground/run_candidate_table_experiments.py
```

Results are written to `playground/candidate_table_results/candidate_table_summary.csv`.
