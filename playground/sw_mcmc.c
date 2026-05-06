///////////////////////////////////////
// compile with
// gcc -std=gnu99 mcmc.c -o mcmc -pg -lm -lrt
///////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define DATASET_NAME "asia"
#define NUM_NODES 8 // Example: 8 nodes for the Asia network, cancer: 5, earthquake: 5, sachs: 11, survey: 6
#define MAX_PARENTS_PER_NODE 64 // Depends on your pre-computation
#define ITERATIONS 100000

char node_names[NUM_NODES][64]; // Stores up to 8 names, 63 chars each

struct timespec start, end;
double time_spent;

// 1. Data Structures for the Database
typedef struct {
    unsigned int parent_bitmask; // One-hot encoded parent set
    float local_score;           // Pre-computed BDe score
} ParentSet;

ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES]; // How many candidate sets each node actually has

// 2. Pre-computation Phase (Run once on ARM)
// V1: Fixed K (max parents per node)

// Helper to count how many bits are set to 1 in the mask (number of parents)
int count_set_bits(unsigned int n) {
    int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

// Computes the BDeu score for a single node given a specific parent set
float calculate_bde_score(int** dataset, int num_samples, int target_node, unsigned int parent_mask) {
    // Equivalent Sample Size (Hyperparameter, usually 1.0)
    float alpha = 1.0f; 
    
    int num_parents = count_set_bits(parent_mask);
    
    // Extract which specific nodes are the parents into an array for easy looping
    int parent_indices[32]; 
    int p_idx = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        if (parent_mask & (1 << i)) {
            parent_indices[p_idx++] = i;
        }
    }

    // q = Number of possible states the parent group can be in (2^num_parents for binary)
    int q = 1 << num_parents; 
    
    // The Dirichlet priors
    float alpha_ij = alpha / (float)q;
    float alpha_ijk = alpha_ij / 2.0f; // 2 because the target node has 2 states (0 or 1)

    float final_log_score = 0.0f;

    // Iterate through every possible state combination the parents could take (e.g., 00, 01, 10, 11)
    for (int state = 0; state < q; state++) {
        int N_ijk[2] = {0, 0}; // N_ijk[0] counts when target=0, N_ijk[1] counts when target=1

        // Scan the entire dataset to count frequencies
        for (int row = 0; row < num_samples; row++) {
            bool parents_match_state = true;
            
            // Check if this row's parent values match the current 'state' we are testing
            for (int p = 0; p < num_parents; p++) {
                int p_node = parent_indices[p];
                int expected_val = (state >> p) & 1; // Extract the p-th bit of 'state'
                
                if (dataset[row][p_node] != expected_val) {
                    parents_match_state = false;
                    break;
                }
            }

            // If the parents are in the state we are looking for, check the target node!
            if (parents_match_state) {
                int target_val = dataset[row][target_node];
                N_ijk[target_val]++;
            }
        }

        int N_ij = N_ijk[0] + N_ijk[1]; // Total times this parent state occurred

        // Apply the BDe log-gamma formula
        // Use lgammaf() from <math.h> which computes the natural logarithm of the gamma function
        final_log_score += lgammaf(alpha_ij) - lgammaf(alpha_ij + N_ij);
        final_log_score += lgammaf(alpha_ijk + N_ijk[0]) - lgammaf(alpha_ijk);
        final_log_score += lgammaf(alpha_ijk + N_ijk[1]) - lgammaf(alpha_ijk);
    }

    return final_log_score;
}

void precompute_fixed_k(int** dataset, int num_samples) {
    for (int i = 0; i < NUM_NODES; i++) {
        int candidate_count = 0;

        // 1. K = 0 (No parents)
        unsigned int mask_k0 = 0;
        precomputed_db[i][candidate_count].parent_bitmask = mask_k0;
        precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k0);
        candidate_count++;

        // 2. K = 1 (One parent)
        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue; // Node cannot be its own parent
            
            unsigned int mask_k1 = (1 << p1);
            precomputed_db[i][candidate_count].parent_bitmask = mask_k1;
            precomputed_db[i][candidate_count].local_score = calculate_bde_score(dataset, num_samples, i, mask_k1);
            candidate_count++;
        }

        // 3. K = 2 (Two parents)
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
        
        num_candidates[i] = candidate_count;
    }

}

// --- Scoring Logic ---
bool check_compatibility(unsigned int parent_mask, int* order, int current_node_index) {
    // In an order, a node can only have parents that appear BEFORE it.
    unsigned int allowed_mask = 0;
    for (int i = 0; i < current_node_index; i++) {
        allowed_mask |= (1 << order[i]);
    }
    // If the required parents are a subset of the allowed parents, it's compatible
    return (parent_mask & allowed_mask) == parent_mask;
}

// --- Core Math (This maps to the FPGA LUT) ---
// Calculates log(e^a + e^b) safely to avoid underflow/overflow
float log_add(float log_a, float log_b) {
    if (log_a <= -999999.0f) return log_b;
    if (log_b <= -999999.0f) return log_a;
    float max_val = (log_a > log_b) ? log_a : log_b;
    float min_val = (log_a < log_b) ? log_a : log_b;
    return max_val + logf(1.0f + expf(min_val - max_val));
}

// 3. The Software MCMC Loop (This is what the FPGA will eventually replace)
float score_order(int* order) {
    float total_score = 0.0;
    
    // For each node...
    for (int i = 0; i < NUM_NODES; i++) {
        int current_node = order[i];
        float node_score = -INFINITY; // Start in log space
        
        // ...scan its precomputed database
        for (int p = 0; p < num_candidates[current_node]; p++) {
            unsigned int candidate_mask = precomputed_db[current_node][p].parent_bitmask;
            
            // Check if the candidate parent set is valid given the current order
            // (In software, this is a loop checking if parents come before 'i' in the order)
            bool is_compatible = check_compatibility(candidate_mask, order, i);
            
            if (is_compatible) {
                // Log-space addition: log(exp(node_score) + exp(local_score))
                // In hardware, this uses the log(1+exp(x)) LUT
                node_score = log_add(node_score, precomputed_db[current_node][p].local_score);
            }
        }
        total_score += node_score;
    }
    return total_score;
}

int** load_csv(const char* filename, int* out_num_samples, int num_nodes) {
    FILE* file = fopen(filename, "r");

    char buffer[2048];
    
    // Read Header and Store Names
    if (fgets(buffer, sizeof(buffer), file)) {
        char* token = strtok(buffer, ",\n\r"); // Split by comma or newline
        int i = 0;
        while (token && i < num_nodes) {
            strncpy(node_names[i], token, 63);
            node_names[i][63] = '\0'; // Ensure null-termination
            token = strtok(NULL, ",\n\r");
            i++;
        }
    }

    // counting logic
    int lines = 0;
    long data_start_pos = ftell(file); // Save position after header
    while (fgets(buffer, sizeof(buffer), file)) {
        lines++;
    }
    *out_num_samples = lines; 

    int** dataset = (int**)malloc((*out_num_samples) * sizeof(int*));
    for (int i = 0; i < *out_num_samples; i++) {
        dataset[i] = (int*)malloc(num_nodes * sizeof(int));
    }

    // Rewind to the start of the DATA (just after header)
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

// helper for edge validation
int find_node_index(char* name) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (strcmp(node_names[i], name) == 0) return i;
    }
    return -1;
}


int main() {
    // 1. Load dataset

    // Construct dynamic file paths based on DATASET_NAME
    char samples_path[256], edges_path[256];
    sprintf(samples_path, "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    sprintf(edges_path, "cleaned-datasets/%s_edges.csv", DATASET_NAME);

    int num_samples;
    int** dataset = load_csv(samples_path, &num_samples, NUM_NODES);

    // 2. Run Pre-computation
    clock_gettime(CLOCK_MONOTONIC, &start);

    precompute_fixed_k(dataset, num_samples);

    clock_gettime(CLOCK_MONOTONIC, &end);
    time_spent = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Pre-computation took %f seconds\n", time_spent);
    
    // 4. Software MCMC (The Golden Model)
    // Initialize random order
    int current_order[NUM_NODES];
    for (int i = 0; i < NUM_NODES; i++) {
        current_order[i] = i;
    }

    // Fisher-Yates shuffle
    for (int i = NUM_NODES - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = current_order[i];
        current_order[i] = current_order[j];
        current_order[j] = temp;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    float current_score = score_order(current_order);
    
    // Simple Metropolis-Hastings Loop
    for (int step = 0; step < ITERATIONS; step++) {
        int proposed_order[NUM_NODES];
        
        // 1. Copy current to proposed
        for (int i = 0; i < NUM_NODES; i++) {
            proposed_order[i] = current_order[i];
        }
        
        // 2. Randomly swap two elements to create a new proposal
        int idx1 = rand() % NUM_NODES;
        int idx2 = rand() % NUM_NODES;
        // int idx1 = rand() % (NUM_NODES - 1); 
        // int idx2 = idx1 + 1;

        int temp = proposed_order[idx1];
        proposed_order[idx1] = proposed_order[idx2];
        proposed_order[idx2] = temp;
        
        float proposed_score = score_order(proposed_order);
        
        // Accept/Reject
        float acceptance_prob = expf(proposed_score - current_score);
        float u = (float)rand() / RAND_MAX;
        
        if (u < acceptance_prob) {
            // 3. Accept the new order by copying proposed back to current
            for (int i = 0; i < NUM_NODES; i++) {
                current_order[i] = proposed_order[i];
            }
            current_score = proposed_score;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    time_spent = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("MCMC Loop took %f seconds\n", time_spent);
    
    printf("Final order: ");
    for (int i = 0; i < NUM_NODES; i++) {
        int node_idx = current_order[i];
        printf("%s ", node_names[node_idx]); 
        
        // Optional: Add an arrow for clarity
        if (i < NUM_NODES - 1) printf("-> ");
    }
    printf("\n");

    // 1. Store Ground Truth Edges in a matrix
    bool ground_truth[NUM_NODES][NUM_NODES] = {false};
    FILE* edge_file = fopen(edges_path, "r");
    if (edge_file) {
        char line[128];
        fgets(line, sizeof(line), edge_file); // Skip header
        while (fgets(line, sizeof(line), edge_file)) {
            char src_name[64], dst_name[64];
            sscanf(line, "%[^,],%s", src_name, dst_name);
            int u = find_node_index(src_name);
            int v = find_node_index(dst_name);
            if (u != -1 && v != -1) ground_truth[u][v] = true;
        }
        fclose(edge_file);
    }

    // 2. Extract Learned Edges and Compare
    int true_positives = 0, false_positives = 0, total_ground_truth = 0;
    float best_dag_score = 0.0f; // Track the absolute DAG score
    
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = 0; j < NUM_NODES; j++) if (ground_truth[i][j]) total_ground_truth++;

        int current_node = current_order[i];
        float best_score = -INFINITY;
        unsigned int best_mask = 0;

        // Find the single best compatible parent set for this node
        for (int p = 0; p < num_candidates[current_node]; p++) {
            if (check_compatibility(precomputed_db[current_node][p].parent_bitmask, current_order, i)) {
                if (precomputed_db[current_node][p].local_score > best_score) {
                    best_score = precomputed_db[current_node][p].local_score;
                    best_mask = precomputed_db[current_node][p].parent_bitmask;
                }
            }
        }

        best_dag_score += best_score; // Accumulate the score for the final graph

        for (int p_idx = 0; p_idx < NUM_NODES; p_idx++) {
            if (best_mask & (1 << p_idx)) {
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

    // 3. Print Metrics and BDeu Scores
    float precision = (float)true_positives / (true_positives + false_positives);
    float recall = (float)true_positives / total_ground_truth;
    float f1 = 2 * (precision * recall) / (precision + recall);

    printf("\n=== Final Scores ===\n");
    printf("Order log-sum BDeu score:       %f\n", current_score);
    printf("Best compatible DAG BDeu score: %f\n", best_dag_score);

    printf("\n=== Final Metrics ===\n");
    printf("Precision: %.2f (How many learned edges were real?)\n", precision);
    printf("Recall:    %.2f (How many real edges did we find?)\n", recall);
    printf("F1 Score:  %.2f\n", f1);

    return 0;
}