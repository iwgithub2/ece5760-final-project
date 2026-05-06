#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/time.h> 

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

void prune_candidates_top_k(int k_limit) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (num_candidates[i] > k_limit) {
            // Sort candidates for node 'i' in descending order of local_score
            qsort(precomputed_db[i], num_candidates[i], sizeof(ParentSet), cmp_candidates);
            
            // Truncate the list
            num_candidates[i] = k_limit;
        }
    }
}

void precompute_fixed_k(int** dataset, int num_samples) {
    for (int i = 0; i < NUM_NODES; i++) {
        int candidate_count = 0;
        unsigned int mask_k0 = 0;
        precomputed_db[i][candidate_count].parent_bitmask = mask_k0;
        precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k0);
        candidate_count++;

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            unsigned int mask_k1 = (1 << p1);
            precomputed_db[i][candidate_count].parent_bitmask = mask_k1;
            precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k1);
            candidate_count++;
        }

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                unsigned int mask_k2 = (1 << p1) | (1 << p2);
                precomputed_db[i][candidate_count].parent_bitmask = mask_k2;
                precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k2);
                candidate_count++;
            }
        }

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                for (int p3 = p2 + 1; p3 < NUM_NODES; p3++) {
                    if (p3 == i) continue;
                    unsigned int mask_k3 = (1 << p1) | (1 << p2) | (1 << p3);
                    precomputed_db[i][candidate_count].parent_bitmask = mask_k3;
                    precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k3);
                    candidate_count++;
                }
            }
        }
        num_candidates[i] = candidate_count;
    }

    prune_candidates_top_k(255);
}

void save_precomputed_data(const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("ERROR: Could not open %s for writing.\n", filename);
        exit(1);
    }
    // Write the number of candidates per node
    fwrite(num_candidates, sizeof(int), NUM_NODES, f);
    // Write the actual precomputed database
    fwrite(precomputed_db, sizeof(ParentSet), NUM_NODES * MAX_PARENTS_PER_NODE, f);
    fclose(f);
    printf("Successfully saved precomputed data to %s\n", filename);
}

int main(void) {
    printf("Loading dataset on PC...\n");
    int num_samples;
    int** dataset = load_csv("../cleaned-datasets/insurance_samples.csv", &num_samples, NUM_NODES);
    
    printf("Precomputing scores (this will be fast on x86)...\n");
    precompute_fixed_k(dataset, num_samples);
    
    printf("Saving binary dump...\n");
    save_precomputed_data("insurance_precomputed.bin");
    
    return 0;
}