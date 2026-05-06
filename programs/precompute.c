// Compile with: gcc -std=gnu99 pc_precompute.c -o pc_precompute -lm -O3

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define DATASET_NAME "insurance"
#define NUM_NODES 27
#define MAX_PARENTS_PER_NODE 256 

typedef struct {
    unsigned int parent_bitmask;
    float local_score;           
} ParentSet;

char node_names[NUM_NODES][64];
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

// OPTIMIZED BDeu Score Calculator (Single Pass)
float calculate_bde_score(int** dataset, int num_samples, int target_node, unsigned int parent_mask) {
    float alpha = 1.0f; 
    int num_parents = count_set_bits(parent_mask);
    
    int parent_indices[32]; 
    int p_idx = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (parent_mask & (1 << i)) parent_indices[p_idx++] = i;
    }
    
    int q = 1 << num_parents; 
    float alpha_ij = alpha / (float)q;
    float alpha_ijk = alpha_ij / 2.0f; 
    float final_log_score = 0.0f;

    // VLA for stack allocation
    int N_ijk[q][2];
    memset(N_ijk, 0, sizeof(N_ijk)); 

    // Single pass over the dataset
    for (int row = 0; row < num_samples; row++) {
        int state = 0;
        for (int p = 0; p < num_parents; p++) {
            int p_node = parent_indices[p];
            if (dataset[row][p_node]) {
                state |= (1 << p);
            }
        }
        int target_val = dataset[row][target_node];
        N_ijk[state][target_val]++;
    }

    // Calculate score
    for (int state = 0; state < q; state++) {
        int n0 = N_ijk[state][0];
        int n1 = N_ijk[state][1];
        int N_ij = n0 + n1; 
        
        final_log_score += lgammaf(alpha_ij) - lgammaf(alpha_ij + N_ij);
        final_log_score += lgammaf(alpha_ijk + n0) - lgammaf(alpha_ijk);
        final_log_score += lgammaf(alpha_ijk + n1) - lgammaf(alpha_ijk);
    }
    
    return final_log_score;
}

int cmp_candidates(const void* a, const void* b) {
    float score_a = ((ParentSet*)a)->local_score;
    float score_b = ((ParentSet*)b)->local_score;
    if (score_a < score_b) return 1;
    if (score_a > score_b) return -1;
    return 0;
}

// We no longer need a separate prune function, we will do it inline.

void precompute_fixed_k(int** dataset, int num_samples) {
    // Temporary buffer to hold all possible combinations before pruning.
    // 27 nodes max k=3 is 2952 combinations. 3500 is a safe buffer.
    ParentSet temp_candidates[3500]; 

    for (int i = 0; i < NUM_NODES; i++) {
        int candidate_count = 0;
        unsigned int mask_k0 = 0;
        
        // k = 0
        temp_candidates[candidate_count].parent_bitmask = mask_k0;
        temp_candidates[candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k0);
        candidate_count++;

        // k = 1
        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            unsigned int mask_k1 = (1 << p1);
            temp_candidates[candidate_count].parent_bitmask = mask_k1;
            temp_candidates[candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k1);
            candidate_count++;
        }

        // k = 2
        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                unsigned int mask_k2 = (1 << p1) | (1 << p2);
                temp_candidates[candidate_count].parent_bitmask = mask_k2;
                temp_candidates[candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k2);
                candidate_count++;
            }
        }

        // k = 3
        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                for (int p3 = p2 + 1; p3 < NUM_NODES; p3++) {
                    if (p3 == i) continue;
                    unsigned int mask_k3 = (1 << p1) | (1 << p2) | (1 << p3);
                    temp_candidates[candidate_count].parent_bitmask = mask_k3;
                    temp_candidates[candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k3);
                    candidate_count++;
                }
            }
        }

        // Sort the temporary array in descending order of local_score
        qsort(temp_candidates, candidate_count, sizeof(ParentSet), cmp_candidates);

        // Take only the top K (up to our max of 255) to fit in the hardware array
        int keep_count = (candidate_count > 255) ? 255 : candidate_count;
        
        for (int j = 0; j < keep_count; j++) {
            precomputed_db[i][j] = temp_candidates[j];
        }
        num_candidates[i] = keep_count;
    }
}

int** load_csv(const char* filename, int* out_num_samples, int num_nodes) {
    FILE* file = fopen(filename, "r");
    if (!file) { printf("Failed to open %s\n", filename); exit(1); }
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
    while (fgets(buffer, sizeof(buffer), file)) lines++;
    *out_num_samples = lines; 

    int** dataset = (int**)malloc((*out_num_samples) * sizeof(int*));
    for (int i = 0; i < *out_num_samples; i++) dataset[i] = (int*)malloc(num_nodes * sizeof(int));
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

void save_precomputed_data(const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("ERROR: Could not open %s for writing.\n", filename);
        exit(1);
    }
    
    // Write metadata/arrays
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
    
    printf("Done.\n");
    return 0;
}