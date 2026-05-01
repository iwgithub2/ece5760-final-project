// gcc -std=gnu99 mcmc_test.c -o mcmc_test -pg -lm

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/time.h> 

#include "address_map_arm_brl4.h"

#define PIO_START_OFFSET        0x00
#define PIO_SEED_OFFSET         0x10
#define PIO_DONE_OFFSET         0x20
#define PIO_BEST_SCORE_OFFSET   0x30
#define PIO_NUM_CANDS_OFFSET    0x40 
#define PIO_BEST_ORDER_OFFSET   0x50 

#define DATASET_NAME "asia"
#define NUM_NODES 4 // Locked to 4 to match your current Verilog
#define MAX_PARENTS_PER_NODE 64 

char node_names[NUM_NODES][64];

// --- Database Data Structures ---
typedef struct {
    unsigned int parent_bitmask;
    float local_score;           
} ParentSet;

ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES]; 

// FPGA Pointers
void *h2p_lw_virtual_base;
volatile unsigned int *pio_start=NULL;
volatile unsigned int *pio_seed=NULL;
volatile unsigned int *pio_done=NULL;
volatile unsigned int *pio_best_score=NULL;
volatile unsigned int *pio_num_cands=NULL;
volatile unsigned int *pio_best_order=NULL;

volatile uint64_t *mcmc_system_base;
void *fpga_ram_virtual_base;

// --- Precomputation Math ---
int count_set_bits(unsigned int n) {
    int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

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
                int target_val = dataset[row][target_node];
                N_ijk[target_val]++;
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
        num_candidates[i] = candidate_count;
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

int32_t float_to_q16(float val) {
    float scaled = val * 65536.0f;
    if (scaled > 2147483647.0f) return 2147483647; // 32-bit signed max
    if (scaled < -2147483648.0f) return -2147483648; // 32-bit signed min
    return (int32_t)scaled;
}

int main(void)
{
    // === Get FPGA addresses ===
    int fd;
    if( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
        printf( "ERROR: could not open \"/dev/mem\"...\n" ); return( 1 );
    }
    
    h2p_lw_virtual_base = mmap( NULL, HW_REGS_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, HW_REGS_BASE );    
    if( h2p_lw_virtual_base == MAP_FAILED ) { printf( "ERROR: mmap1() failed...\n" ); close( fd ); return(1); }

    pio_start         = (unsigned int *)(h2p_lw_virtual_base + PIO_START_OFFSET);
    pio_seed          = (unsigned int *)(h2p_lw_virtual_base + PIO_SEED_OFFSET);
    pio_done          = (unsigned int *)(h2p_lw_virtual_base + PIO_DONE_OFFSET);
    pio_best_score    = (unsigned int *)(h2p_lw_virtual_base + PIO_BEST_SCORE_OFFSET);
    pio_num_cands     = (unsigned int *)(h2p_lw_virtual_base + PIO_NUM_CANDS_OFFSET);
    pio_best_order    = (unsigned int *)(h2p_lw_virtual_base + PIO_BEST_ORDER_OFFSET);
    
    fpga_ram_virtual_base = mmap( NULL, FPGA_ONCHIP_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_ONCHIP_BASE); 
    if( fpga_ram_virtual_base == MAP_FAILED ) { printf( "ERROR: mmap3() failed...\n" ); close( fd ); return(1); }
    
    mcmc_system_base = (uint64_t *)fpga_ram_virtual_base;

    // === 1. Load Dataset & Precompute (ARM) ===
    printf("Loading dataset and precomputing...\n");
    char samples_path[256];
    sprintf(samples_path, "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    int num_samples;
    int** dataset = load_csv(samples_path, &num_samples, NUM_NODES);
    precompute_fixed_k(dataset, num_samples);

    // === 2. Transfer Data to FPGA ===
    printf("Writing databases to FPGA Broadcast Memory...\n");
    uint32_t num_cands_packed = 0;

    for (int i = 0; i < NUM_NODES; i++) {
        // Pack the candidate count for node `i` into the correct byte position
        num_cands_packed |= (num_candidates[i] & 0xFF) << (i * 8);

        for (int p = 0; p < num_candidates[i]; p++) {
            uint64_t mask = (uint64_t)precomputed_db[i][p].parent_bitmask;
            int32_t q16_score = float_to_q16(precomputed_db[i][p].local_score);
            
            // Pack: [63:32] Mask, [31:0] Score
            uint64_t packed_word = (mask << 32) | (uint32_t)q16_score;
            
            // Address calculation: Node is top 2 bits (i << 6), Candidate is bottom 6 bits (p)
            int offset = (i << 6) | p; 
            
            *(mcmc_system_base + offset) = packed_word;
        }
    }
    
    *pio_num_cands = num_cands_packed; // Tell the FPGA how many to process
    *pio_seed = 0xABCD1234;            // Seed the LFSR

    // === 3. Run Hardware MCMC ===
    printf("Starting FPGA Acceleration...\n");
    *pio_start = 1;
    
    while (*pio_done == 0) { } // Wait for FPGA 
    *pio_start = 0; // Acknowledge completion
    
    int32_t hw_best_score = (int32_t)(*pio_best_score);
    float float_score = (float)hw_best_score / 65536.0f;
    printf("MCMC Best Score: %f (Hex: %08X)\n", float_score, hw_best_score);

    // === 4. Unpack Best Order ===
    int best_order[NUM_NODES];
    uint32_t packed_order = *pio_best_order;
    
    printf("Hardware Final Order: ");
    for (int i = 0; i < NUM_NODES; i++) {
        best_order[i] = (packed_order >> (i * 8)) & 0xFF;
        printf("%s ", node_names[best_order[i]]);
        if (i < NUM_NODES - 1) printf("-> ");
    }
    printf("\n");

    return 0;
}