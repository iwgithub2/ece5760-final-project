#ifndef MCMC_CORE_H
#define MCMC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "fixed_point_logadd.h"

#ifndef DATASET_NAME
#define DATASET_NAME "asia"
#endif

#ifndef NUM_NODES
#define NUM_NODES 8
#endif

#ifndef MAX_PARENTS_PER_NODE
#define MAX_PARENTS_PER_NODE 64
#endif

typedef struct {
    uint64_t parent_bitmask;
    float local_score;
    fx_raw_t local_score_fx;
} ParentSet;

extern char node_names[NUM_NODES][64];
extern ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
extern int num_candidates[NUM_NODES];

void precompute_fixed_k(int** dataset, int num_samples);
int load_precomputed_table(const char* filename);
bool check_compatibility(uint64_t parent_mask, const int* order, int current_node_index);
int** load_csv(const char* filename, int* out_num_samples, int num_nodes);
void free_dataset(int** dataset, int num_samples);
void print_order(const int* order);
void print_edge_metrics(const int* current_order, const char* edges_path);

#endif
