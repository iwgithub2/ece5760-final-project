# ECE 5760

## Lab 1

## Lab 2

## Lab 3

## Final Project

### Bayesian Network FPGA MCMC Flow

The hardware path is:

1. HPS/C loads candidate parent-set score tables into FPGA RAM.
2. FPGA runs order-MCMC over node orderings.
3. HPS reads back the best order.
4. C reconstructs the best compatible graph, draws it to VGA, and starts the interactive probability demo.

There are two candidate-table modes:

- `fixed`: original C path. Enumerates hardcoded parent sets in `programs/mcmc_test.c`: empty set, all 1-parent sets, all 2-parent sets.
- `ml`: Python-generated candidate table. ML ranks likely parents, enumerates a pruned set of parent sets, and C loads `readable_debug.csv`.

### Local/Laptop: Generate ML Candidate Data

Run this on the laptop/local machine from the repo root:

```sh
uv run python preprocess_bn.py \
  --data cleaned-datasets/asia_samples.csv \
  --output-dir ml \
  --target-type discrete \
  --max-parent-size 4 \
  --max-candidates-per-node 5 \
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

For the current bitstream, keep every node at or below `63` candidate parent sets plus one sentinel. Good current settings:

```text
--max-candidates-per-node 5 --max-parent-size 4  # 31 parent sets/node
--max-candidates-per-node 6 --max-parent-size 4  # 57 parent sets/node
```

This does not fit the current bitstream:

```text
--max-candidates-per-node 7 --max-parent-size 4  # 99 parent sets/node
```

Copy the whole `ml/` output folder to the HPS repo checkout. The current C loader reads `ml/readable_debug.csv`.

### HPS: Configure Dataset

Edit the dataset constants near the top of `programs/mcmc_test.c`:

```c
#define DATASET_NAME "asia"
#define NUM_NODES 8
```

`DATASET_NAME` selects:

```text
cleaned-datasets/<DATASET_NAME>_samples.csv
```

`NUM_NODES` must match the CSV column count and the ML table. The CSV column order must match the ML table `node_id`/`node_name` order.

### HPS: Build

Run on the DE1-SoC HPS from the repo root:

```sh
gcc -std=gnu99 programs/mcmc_test.c -o mcmc_test -lm -pthread
```

### HPS: Run Normally With Hardcoded Fixed Parent Sets

This is the original path. It does not need Python-generated ML files:

```sh
./mcmc_test fixed
```

Equivalent default:

```sh
./mcmc_test
```

### HPS: Validate ML Candidate Table Before FPGA Run

This only loads the CSV/dataset and checks table capacity. It does not touch `/dev/mem`:

```sh
./mcmc_test ml --ml-dir ml --dry-run-candidates
```

If this passes, the table fits the current C build and current FPGA bitstream capacity.

### HPS: Run With ML-Pruned Candidate Table

```sh
./mcmc_test ml --ml-dir ml
```

After either `fixed` or `ml`, the program prints the best order, reconstructs the learned DAG, draws it to VGA, and starts the interactive probability query prompt.

### HPS: Run Larger Networks With Software Partitioning

For datasets larger than the current 32-node bitstream, use ML candidate tables and let C partition the global candidate graph into core/context subproblems. Each subproblem is remapped to local 0..31 node ids, sent to the existing solver, then merged back into a global DAG. Merge skips any edge that would close a cycle.

Example for hepar2:

```sh
uv run python preprocess_bn.py \
  --data cleaned-datasets/hepar2_samples.csv \
  --output-dir ml_hepar2 \
  --target-type discrete \
  --max-parent-size 4 \
  --max-candidates-per-node 5 \
  --bootstrap-iters 20 \
  --min-stability-frequency 0.3 \
  --score bdeu \
  --equivalent-sample-size 1.0 \
  --fixed-point q16.16 \
  --emit-hex

gcc -std=gnu99 -DDATASET_NAME=\"hepar2\" -DNUM_NODES=70 \
  programs/mcmc_partition_demo.c -o mcmc_partition_demo -lm -pthread

./mcmc_partition_demo ml --ml-dir ml_hepar2 --partition --dry-run-candidates
./mcmc_partition_demo ml --ml-dir ml_hepar2 --partition
```

This is intentionally separate from `programs/mcmc_test.c`, which remains the normal data-collection solver. In the demo build, `--partition` is automatic when `NUM_NODES > 32`. Useful tuning knobs:

```text
--partition-size 32       # max active nodes per solver run
--partition-overlap 8     # context nodes added around each core partition
```

### Hardware Capacity And Recompile Rule

Current `rtl/mcmc_system.v` memory map:

```text
32 nodes
64 score/mask pair slots per node
63 usable candidate parent sets per node
1 sentinel entry per node
```

Changing datasets with `NUM_NODES <= 32` and `candidate_count <= 63` only needs a C rebuild.

Changing ML parameters only needs a new FPGA compile if any generated node exceeds `63` candidate parent sets. To support more entries, change the per-node RAM depth/address layout in `rtl/mcmc_hw_config.vh`, mirror it in `programs/mcmc_hw_config.h`, and recompile the FPGA bitstream.

### Experiment Scripts

Candidate-table vs fixed-k experiments:

```sh
uv run python ml_experiments/run_candidate_table_experiments.py
```

Results are written to `ml_experiments/candidate_table_results/candidate_table_summary.csv`.

Multi-dataset ML-pruning experiments and PNG plots live under `ml_experiments/`:

```sh
uv run python ml_experiments/run_multi_dataset_candidate_experiments.py
uv run python ml_experiments/plot_multi_dataset_pngs.py
```
