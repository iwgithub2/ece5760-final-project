// Compile with: gcc -std=gnu99 pc_precompute.c -o pc_precompute -lm -O3

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

#ifndef DATASET_NAME
#define DATASET_NAME "insurance"
#endif
#ifndef NUM_NODES
#define NUM_NODES 27
#endif
#define MAX_PARENTS_PER_NODE 256 

typedef struct {
    unsigned int parent_bitmask;
    float local_score;           
} ParentSet;

char node_names[NUM_NODES][64];
int node_cardinalities[NUM_NODES];
ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES]; 

// --- Helper Functions ---

int count_set_bits(unsigned int n) {
    int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

size_t checked_parent_config_count(unsigned int parent_mask, const char* context) {
    size_t config_count = 1;

    for (int node = 0; node < NUM_NODES; node++) {
        if (parent_mask & (1U << node)) {
            int cardinality = node_cardinalities[node];
            if (cardinality <= 0) {
                printf("ERROR: node %d (%s) has invalid cardinality %d while %s.\n",
                       node, node_names[node], cardinality, context);
                exit(1);
            }
            if (config_count > SIZE_MAX / (size_t)cardinality) {
                printf("ERROR: parent-state count overflow while %s.\n", context);
                exit(1);
            }
            config_count *= (size_t)cardinality;
        }
    }

    return config_count;
}

size_t checked_cpt_entry_count(size_t parent_config_count, int node_cardinality,
                               const char* context) {
    if (node_cardinality <= 0) {
        printf("ERROR: invalid target cardinality %d while %s.\n",
               node_cardinality, context);
        exit(1);
    }
    if (parent_config_count > SIZE_MAX / (size_t)node_cardinality) {
        printf("ERROR: CPT table size overflow while %s.\n", context);
        exit(1);
    }
    return parent_config_count * (size_t)node_cardinality;
}

// OPTIMIZED BDeu Score Calculator (Single Pass)
float calculate_bde_score(int** dataset, int num_samples, int target_node, unsigned int parent_mask) {
    double alpha = 1.0;
    int num_parents = count_set_bits(parent_mask);
    int parent_indices[NUM_NODES];
    int p_idx = 0;
    int r = node_cardinalities[target_node];
    size_t q;
    size_t entry_count;
    int *counts;
    double alpha_ij;
    double alpha_ijk;
    double final_log_score = 0.0;

    for (int i = 0; i < NUM_NODES; i++) {
        if (parent_mask & (1U << i)) parent_indices[p_idx++] = i;
    }

    q = checked_parent_config_count(parent_mask, "scoring a parent set");
    entry_count = checked_cpt_entry_count(q, r, "scoring a parent set");
    counts = (int*)calloc(entry_count, sizeof(int));
    if (!counts) {
        printf("ERROR: failed to allocate %zu score-count entries for node %s.\n",
               entry_count, node_names[target_node]);
        exit(1);
    }

    for (int row = 0; row < num_samples; row++) {
        size_t state = 0;
        int target_val = dataset[row][target_node];

        for (int p = 0; p < num_parents; p++) {
            int p_node = parent_indices[p];
            state = state * (size_t)node_cardinalities[p_node] + (size_t)dataset[row][p_node];
        }

        counts[state * (size_t)r + (size_t)target_val]++;
    }

    alpha_ij = alpha / (double)q;
    alpha_ijk = alpha_ij / (double)r;

    for (size_t state = 0; state < q; state++) {
        int N_ij = 0;

        for (int target_state = 0; target_state < r; target_state++) {
            N_ij += counts[state * (size_t)r + (size_t)target_state];
        }

        final_log_score += lgamma(alpha_ij) - lgamma(alpha_ij + (double)N_ij);
        for (int target_state = 0; target_state < r; target_state++) {
            int count = counts[state * (size_t)r + (size_t)target_state];
            final_log_score += lgamma(alpha_ijk + (double)count) - lgamma(alpha_ijk);
        }
    }

    free(counts);
    return (float)final_log_score;
}

int cmp_candidates(const void* a, const void* b) {
    float score_a = ((ParentSet*)a)->local_score;
    float score_b = ((ParentSet*)b)->local_score;
    if (score_a < score_b) return 1;
    if (score_a > score_b) return -1;
    return 0;
}

void precompute_fixed_k(int** dataset, int num_samples) {
    for (int i = 0; i < NUM_NODES; i++) {
        ParentSet k0[1], k1[32], k2[400], k3[3000];
        int c0 = 0, c1 = 0, c2 = 0, c3 = 0;

        // Generate k = 0
        k0[c0++] = (ParentSet){0, calculate_bde_score(dataset, num_samples, i, 0)};

        // Generate k = 1, 2, 3
        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            k1[c1++] = (ParentSet){1<<p1, calculate_bde_score(dataset, num_samples, i, 1<<p1)};

            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                k2[c2++] = (ParentSet){(1<<p1)|(1<<p2), calculate_bde_score(dataset, num_samples, i, (1<<p1)|(1<<p2))};

                for (int p3 = p2 + 1; p3 < NUM_NODES; p3++) {
                    if (p3 == i) continue;
                    k3[c3++] = (ParentSet){(1<<p1)|(1<<p2)|(1<<p3), calculate_bde_score(dataset, num_samples, i, (1<<p1)|(1<<p2)|(1<<p3))};
                }
            }
        }

        // Sort k-sets individually by local score descending
        qsort(k1, c1, sizeof(ParentSet), cmp_candidates);
        qsort(k2, c2, sizeof(ParentSet), cmp_candidates);
        qsort(k3, c3, sizeof(ParentSet), cmp_candidates);

        // --- Stratified Selection (Max 255) ---
        int out_idx = 0;
        
        // 1. ALWAYS keep k=0 (Crucial for nodes at the start of the ordering)
        precomputed_db[i][out_idx++] = k0[0];

        // 2. Keep ALL k=1 candidates (Max 26)
        for (int j = 0; j < c1 && out_idx < 255; j++) {
            precomputed_db[i][out_idx++] = k1[j];
        }

        // 3. Keep the top 100 k=2 candidates
        for (int j = 0; j < c2 && j < 100 && out_idx < 255; j++) {
            precomputed_db[i][out_idx++] = k2[j];
        }

        // 4. Fill the remaining slots with the best k=3 candidates
        for (int j = 0; j < c3 && out_idx < 255; j++) {
            precomputed_db[i][out_idx++] = k3[j];
        }

        num_candidates[i] = out_idx;
    }
}

int parse_csv_state_or_die(const char* token, const char* filename, int row_number, int col) {
    char *end_ptr;
    long value;

    if (!token || token[0] == '\0') {
        printf("ERROR: missing value in %s row %d column %d (%s).\n",
               filename, row_number, col, node_names[col]);
        exit(1);
    }

    value = strtol(token, &end_ptr, 10);
    while (*end_ptr && isspace((unsigned char)*end_ptr)) end_ptr++;
    if (*end_ptr != '\0' || value < 0 || value >= INT_MAX) {
        printf("ERROR: expected nonnegative integer state in %s row %d column %d (%s), got '%s'.\n",
               filename, row_number, col, node_names[col], token);
        exit(1);
    }

    return (int)value;
}

int** load_csv(const char* filename, int* out_num_samples, int num_nodes) {
    FILE* file = fopen(filename, "r");
    if (!file) { printf("Failed to open %s\n", filename); exit(1); }
    char buffer[2048];
    if (fgets(buffer, sizeof(buffer), file)) {
        char* token = strtok(buffer, ",\n\r");
        int i = 0;
        while (token) {
            if (i < num_nodes) {
                strncpy(node_names[i], token, 63);
                node_names[i][63] = '\0';
            }
            token = strtok(NULL, ",\n\r");
            i++;
        }
        if (i != num_nodes) {
            printf("ERROR: %s has %d columns, but NUM_NODES=%d.\n",
                   filename, i, num_nodes);
            exit(1);
        }
    } else {
        printf("ERROR: %s is empty.\n", filename);
        exit(1);
    }
    int lines = 0;
    long data_start_pos = ftell(file); 
    while (fgets(buffer, sizeof(buffer), file)) lines++;
    if (lines <= 0) {
        printf("ERROR: %s has no data rows.\n", filename);
        exit(1);
    }
    *out_num_samples = lines; 

    int** dataset = (int**)malloc((*out_num_samples) * sizeof(int*));
    if (!dataset) {
        printf("ERROR: failed to allocate dataset rows.\n");
        exit(1);
    }
    for (int i = 0; i < *out_num_samples; i++) {
        dataset[i] = (int*)malloc((size_t)num_nodes * sizeof(int));
        if (!dataset[i]) {
            printf("ERROR: failed to allocate dataset row %d.\n", i);
            exit(1);
        }
    }
    for (int col = 0; col < num_nodes; col++) node_cardinalities[col] = 0;
    fseek(file, data_start_pos, SEEK_SET);

    int row = 0;
    while (fgets(buffer, sizeof(buffer), file) && row < *out_num_samples) {
        char* token = strtok(buffer, ",\n\r");
        int col = 0;
        while (token) {
            int value;
            if (col >= num_nodes) {
                printf("ERROR: %s row %d has more than %d columns.\n",
                       filename, row + 2, num_nodes);
                exit(1);
            }
            value = parse_csv_state_or_die(token, filename, row + 2, col);
            dataset[row][col] = value;
            if (value + 1 > node_cardinalities[col]) {
                node_cardinalities[col] = value + 1;
            }
            token = strtok(NULL, ",\n\r");
            col++;
        }
        if (col != num_nodes) {
            printf("ERROR: %s row %d has %d columns, expected %d.\n",
                   filename, row + 2, col, num_nodes);
            exit(1);
        }
        row++;
    }
    fclose(file);

    printf("Loaded %d samples with node cardinalities:\n", *out_num_samples);
    for (int col = 0; col < num_nodes; col++) {
        if (node_cardinalities[col] <= 0) {
            printf("ERROR: node %d (%s) has no observed states.\n", col, node_names[col]);
            exit(1);
        }
        printf("  node %d %-12s states=%d\n", col, node_names[col], node_cardinalities[col]);
    }

    return dataset;
}

void save_precomputed_data(const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("ERROR: Could not open %s for writing.\n", filename);
        exit(1);
    }
    
    // Write the string names, then metadata, then the database
    fwrite(node_names, sizeof(char), NUM_NODES * 64, f);
    fwrite(num_candidates, sizeof(int), NUM_NODES, f);
    fwrite(precomputed_db, sizeof(ParentSet), NUM_NODES * MAX_PARENTS_PER_NODE, f);
    
    fclose(f);
    printf("Successfully saved precomputed data to %s\n", filename);
}

int main(void) {
    printf("=== PC-Side Precomputation Engine ===\n");
    
    char samples_path[256];
    sprintf(samples_path, "../cleaned-datasets/%s_samples.csv", DATASET_NAME);
    
    printf("1. Loading dataset: %s\n", samples_path);
    int num_samples;
    int** dataset = load_csv(samples_path, &num_samples, NUM_NODES);
    printf("   Loaded %d samples.\n", num_samples);
    
    printf("2. Precomputing scores (k=3)... This will take a few moments.\n");
    precompute_fixed_k(dataset, num_samples);

    
    char out_path[256];
    sprintf(out_path, "%s_precomputed.bin", DATASET_NAME);
    printf("3. Saving binary dump...\n");
    
    save_precomputed_data(out_path);
    
    // Cleanup
    for (int i = 0; i < num_samples; i++) free(dataset[i]);
    free(dataset);

    // --- SCORE DEBUGGING ---
    float max_score = -INFINITY;
    float min_score = INFINITY;
    
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = 0; j < num_candidates[i]; j++) {
            float s = precomputed_db[i][j].local_score;
            if (s > max_score) max_score = s;
            if (s < min_score) min_score = s;
        }
    }
    
    printf("\n--- BDeu Score Diagnostics ---\n");
    printf("Global Max Score: %f\n", max_score);
    printf("Global Min Score: %f\n", min_score);
    
    // Look at the score landscape for the first node to see the "gaps"
    printf("Top 10 candidates for Node 0 (%s):\n", node_names[0]);
    for(int j = 0; j < 10 && j < num_candidates[0]; j++) {
        printf("  Rank %d: Score = %f, Mask = 0x%08x\n", 
               j, precomputed_db[0][j].local_score, precomputed_db[0][j].parent_bitmask);
    }
    printf("------------------------------\n\n");
    
    printf("Done.\n");
    return 0;
}
