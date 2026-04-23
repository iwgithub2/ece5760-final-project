#include "mcmc_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char node_names[NUM_NODES][64];
ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES];

static int count_set_bits(unsigned int n) {
    int count = 0;
    while (n) {
        count += n & 1U;
        n >>= 1;
    }
    return count;
}

static float calculate_bde_score(int** dataset, int num_samples, int target_node, unsigned int parent_mask) {
    float alpha = 1.0f;
    int num_parents = count_set_bits(parent_mask);
    int parent_indices[32];
    int p_idx = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (parent_mask & (1U << i)) {
            parent_indices[p_idx++] = i;
        }
    }

    int q = 1 << num_parents;
    float alpha_ij = alpha / (float)q;
    float alpha_ijk = alpha_ij / 2.0f;
    float final_log_score = 0.0f;

    for (int state = 0; state < q; state++) {
        int N_ijk[2] = {0, 0};
        for (int row = 0; row < num_samples; row++) {
            bool parents_match_state = true;
            for (int p = 0; p < num_parents; p++) {
                int p_node = parent_indices[p];
                int expected_val = (state >> p) & 1;
                if (dataset[row][p_node] != expected_val) {
                    parents_match_state = false;
                    break;
                }
            }
            if (parents_match_state) {
                N_ijk[dataset[row][target_node]]++;
            }
        }

        int N_ij = N_ijk[0] + N_ijk[1];
        final_log_score += lgammaf(alpha_ij) - lgammaf(alpha_ij + N_ij);
        final_log_score += lgammaf(alpha_ijk + N_ijk[0]) - lgammaf(alpha_ijk);
        final_log_score += lgammaf(alpha_ijk + N_ijk[1]) - lgammaf(alpha_ijk);
    }
    return final_log_score;
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
            unsigned int mask_k1 = (1U << p1);
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
                unsigned int mask_k2 = (1U << p1) | (1U << p2);
                precomputed_db[i][candidate_count].parent_bitmask = mask_k2;
                precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k2);
                candidate_count++;
            }
        }
        num_candidates[i] = candidate_count;
    }
}

bool check_compatibility(unsigned int parent_mask, const int* order, int current_node_index) {
    unsigned int allowed_mask = 0;
    for (int i = 0; i < current_node_index; i++) {
        allowed_mask |= (1U << order[i]);
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
    while (fgets(buffer, sizeof(buffer), file) && row < *out_num_samples) {
        char* token = strtok(buffer, ",");
        int col = 0;
        while (token && col < num_nodes) {
            dataset[row][col] = atoi(token);
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
        unsigned int best_mask = 0;
        for (int p = 0; p < num_candidates[current_node]; p++) {
            if (check_compatibility(precomputed_db[current_node][p].parent_bitmask, current_order, i) &&
                precomputed_db[current_node][p].local_score > best_score) {
                best_score = precomputed_db[current_node][p].local_score;
                best_mask = precomputed_db[current_node][p].parent_bitmask;
            }
        }

        for (int p_idx = 0; p_idx < NUM_NODES; p_idx++) {
            if (best_mask & (1U << p_idx)) {
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
    printf("\nFinal Metrics:\n");
    printf("Precision: %.2f\n", precision);
    printf("Recall:    %.2f\n", recall);
    printf("F1 Score:  %.2f\n", f1);
}
