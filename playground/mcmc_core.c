#include "mcmc_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char node_names[NUM_NODES][64];
int node_cardinalities[NUM_NODES];
ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES];

static int count_set_bits(uint64_t n) {
    int count = 0;
    while (n) {
        count += n & 1U;
        n >>= 1;
    }
    return count;
}

static float calculate_bde_score(int** dataset, int num_samples, int target_node, uint64_t parent_mask) {
    float alpha = 1.0f;
    int num_parents = count_set_bits(parent_mask);
    int parent_indices[NUM_NODES];
    int p_idx = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (parent_mask & (UINT64_C(1) << i)) {
            parent_indices[p_idx++] = i;
        }
    }

    int r = node_cardinalities[target_node];
    int q = 1;
    for (int p = 0; p < num_parents; p++) {
        q *= node_cardinalities[parent_indices[p]];
    }

    int* counts = (int*)calloc((size_t)q * (size_t)r, sizeof(int));
    if (!counts) {
        fprintf(stderr, "ERROR: could not allocate score counts\n");
        exit(1);
    }

    for (int row = 0; row < num_samples; row++) {
        int cfg = 0;
        for (int p = 0; p < num_parents; p++) {
            int p_node = parent_indices[p];
            cfg = cfg * node_cardinalities[p_node] + dataset[row][p_node];
        }
        counts[cfg * r + dataset[row][target_node]]++;
    }

    double alpha_ij = (double)alpha / (double)q;
    double alpha_ijk = alpha_ij / (double)r;
    double final_log_score = 0.0;

    for (int state = 0; state < q; state++) {
        int N_ij = 0;
        for (int target_state = 0; target_state < r; target_state++) {
            N_ij += counts[state * r + target_state];
        }
        final_log_score += lgamma(alpha_ij) - lgamma(alpha_ij + N_ij);
        for (int target_state = 0; target_state < r; target_state++) {
            final_log_score += lgamma(alpha_ijk + counts[state * r + target_state]) - lgamma(alpha_ijk);
        }
    }
    free(counts);
    return (float)final_log_score;
}

void precompute_fixed_k(int** dataset, int num_samples) {
    for (int i = 0; i < NUM_NODES; i++) {
        int candidate_count = 0;
        precomputed_db[i][candidate_count].parent_bitmask = 0;
        precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, 0);
        candidate_count++;

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) {
                continue;
            }
            uint64_t mask_k1 = (UINT64_C(1) << p1);
            precomputed_db[i][candidate_count].parent_bitmask = mask_k1;
            precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k1);
            candidate_count++;
        }

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) {
                continue;
            }
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) {
                    continue;
                }
                uint64_t mask_k2 = (UINT64_C(1) << p1) | (UINT64_C(1) << p2);
                precomputed_db[i][candidate_count].parent_bitmask = mask_k2;
                precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k2);
                candidate_count++;
            }
        }
        num_candidates[i] = candidate_count;
    }
}

static int split_csv_simple(char* line, char** fields, int max_fields) {
    int count = 0;
    char* start = line;
    for (char* p = line; ; p++) {
        char ch = *p;
        if (ch == ',' || ch == '\n' || ch == '\r' || ch == '\0') {
            if (count < max_fields) {
                fields[count++] = start;
            }
            if (ch == '\0' || ch == '\n' || ch == '\r') {
                break;
            }
            *p = '\0';
            start = p + 1;
        }
    }
    return count;
}

int load_precomputed_table(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: could not open score table %s\n", filename);
        return -1;
    }

    for (int i = 0; i < NUM_NODES; i++) {
        num_candidates[i] = 0;
    }

    char line[4096];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        fprintf(stderr, "ERROR: score table is empty: %s\n", filename);
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char* fields[16] = {0};
        int nfields = split_csv_simple(line, fields, 16);
        if (nfields < 8) {
            continue;
        }

        int node = atoi(fields[0]);
        if (node < 0 || node >= NUM_NODES) {
            fprintf(stderr, "ERROR: score table node out of range: %d\n", node);
            fclose(file);
            return -1;
        }
        int cand = num_candidates[node];
        if (cand >= MAX_PARENTS_PER_NODE) {
            fprintf(stderr, "ERROR: too many candidates for node %d, max %d\n",
                    node, MAX_PARENTS_PER_NODE);
            fclose(file);
            return -1;
        }

        unsigned long long mask = strtoull(fields[6], NULL, 16);
        if (NUM_NODES < 64 && mask >= (UINT64_C(1) << NUM_NODES)) {
            fprintf(stderr, "ERROR: parent mask 0x%llx does not fit NUM_NODES=%d\n",
                    mask, NUM_NODES);
            fclose(file);
            return -1;
        }
        precomputed_db[node][cand].parent_bitmask = (uint64_t)mask;
        precomputed_db[node][cand].local_score = (float)atof(fields[7]);
        precomputed_db[node][cand].local_score_fx = 0;
        num_candidates[node]++;
    }
    fclose(file);

    for (int i = 0; i < NUM_NODES; i++) {
        if (num_candidates[i] == 0) {
            fprintf(stderr, "ERROR: no candidates loaded for node %d\n", i);
            return -1;
        }
    }
    return 0;
}

bool check_compatibility(uint64_t parent_mask, const int* order, int current_node_index) {
    uint64_t allowed_mask = 0;
    for (int i = 0; i < current_node_index; i++) {
        allowed_mask |= (UINT64_C(1) << order[i]);
    }
    return (parent_mask & allowed_mask) == parent_mask;
}

int** load_csv(const char* filename, int* out_num_samples, int num_nodes) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: could not open samples file %s\n", filename);
        return NULL;
    }

    char buffer[2048];
    if (fgets(buffer, sizeof(buffer), file)) {
        char* token = strtok(buffer, ",\n\r");
        int i = 0;
        while (token && i < num_nodes) {
            strncpy(node_names[i], token, 63);
            node_names[i][63] = '\0';
            token = strtok(NULL, ",\n\r");
            i++;
        }
    }

    int lines = 0;
    long data_start_pos = ftell(file);
    while (fgets(buffer, sizeof(buffer), file)) {
        lines++;
    }
    *out_num_samples = lines;

    int** dataset = (int**)malloc((size_t)(*out_num_samples) * sizeof(int*));
    if (!dataset) {
        fclose(file);
        return NULL;
    }
    for (int i = 0; i < *out_num_samples; i++) {
        dataset[i] = (int*)malloc((size_t)num_nodes * sizeof(int));
    }

    fseek(file, data_start_pos, SEEK_SET);
    int row = 0;
    for (int col = 0; col < num_nodes; col++) {
        node_cardinalities[col] = 0;
    }
    while (fgets(buffer, sizeof(buffer), file) && row < *out_num_samples) {
        char* token = strtok(buffer, ",");
        int col = 0;
        while (token && col < num_nodes) {
            dataset[row][col] = atoi(token);
            if (dataset[row][col] + 1 > node_cardinalities[col]) {
                node_cardinalities[col] = dataset[row][col] + 1;
            }
            token = strtok(NULL, ",");
            col++;
        }
        row++;
    }

    fclose(file);
    return dataset;
}

void free_dataset(int** dataset, int num_samples) {
    if (!dataset) {
        return;
    }
    for (int i = 0; i < num_samples; i++) {
        free(dataset[i]);
    }
    free(dataset);
}

static int find_node_index(const char* name) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (strcmp(node_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void print_order(const int* order) {
    printf("Final order: ");
    for (int i = 0; i < NUM_NODES; i++) {
        printf("%s", node_names[order[i]]);
        if (i < NUM_NODES - 1) {
            printf(" -> ");
        }
    }
    printf("\n");
}

void print_edge_metrics(const int* current_order, const char* edges_path) {
    bool ground_truth[NUM_NODES][NUM_NODES] = {{false}};
    FILE* edge_file = fopen(edges_path, "r");
    if (edge_file) {
        char line[128];
        fgets(line, sizeof(line), edge_file);
        while (fgets(line, sizeof(line), edge_file)) {
            char src_name[64], dst_name[64];
            if (sscanf(line, "%63[^,],%63s", src_name, dst_name) == 2) {
                int u = find_node_index(src_name);
                int v = find_node_index(dst_name);
                if (u != -1 && v != -1) {
                    ground_truth[u][v] = true;
                }
            }
        }
        fclose(edge_file);
    }

    int true_positives = 0;
    int false_positives = 0;
    int total_ground_truth = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = 0; j < NUM_NODES; j++) {
            if (ground_truth[i][j]) {
                total_ground_truth++;
            }
        }

        int current_node = current_order[i];
        float best_score = -INFINITY;
        uint64_t best_mask = 0;
        for (int p = 0; p < num_candidates[current_node]; p++) {
            if (check_compatibility(precomputed_db[current_node][p].parent_bitmask, current_order, i) &&
                precomputed_db[current_node][p].local_score > best_score) {
                best_score = precomputed_db[current_node][p].local_score;
                best_mask = precomputed_db[current_node][p].parent_bitmask;
            }
        }

        for (int p_idx = 0; p_idx < NUM_NODES; p_idx++) {
            if (best_mask & (UINT64_C(1) << p_idx)) {
                if (ground_truth[p_idx][current_node]) {
                    printf("[CORRECT] %s -> %s\n", node_names[p_idx], node_names[current_node]);
                    true_positives++;
                } else {
                    printf("[EXTRA  ] %s -> %s\n", node_names[p_idx], node_names[current_node]);
                    false_positives++;
                }
            }
        }
    }

    float precision = (true_positives + false_positives) == 0 ? 0.0f :
        (float)true_positives / (float)(true_positives + false_positives);
    float recall = total_ground_truth == 0 ? 0.0f : (float)true_positives / (float)total_ground_truth;
    float f1 = (precision + recall) == 0.0f ? 0.0f : 2.0f * (precision * recall) / (precision + recall);
    int false_negatives = total_ground_truth - true_positives;
    int learned_edges = true_positives + false_positives;
    int shd = false_positives + false_negatives;
    printf("\nFinal Metrics:\n");
    printf("Precision: %.2f\n", precision);
    printf("Recall:    %.2f\n", recall);
    printf("F1 Score:  %.2f\n", f1);
    printf("METRICS precision=%.6f recall=%.6f f1=%.6f tp=%d fp=%d fn=%d learned_edges=%d ground_truth_edges=%d shd=%d\n",
           precision, recall, f1, true_positives, false_positives, false_negatives,
           learned_edges, total_ground_truth, shd);
}
