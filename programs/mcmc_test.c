// gcc -std=gnu99 mcmc_test.c -o mcmc_test -pg -lm -pthread

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
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
#define PIO_ITERATIONS_OFFSET   0x40 
#define PIO_ACTIVE_NODES_OFFSET 0x50 
#define PIO_NODE_MASK           0x60 
#define PIO_RESET_OFFSET        0x70 
#define PIO_CLK_COUNT_OFFSET    0x80 

#ifndef DATASET_NAME
#define DATASET_NAME "asia"
#endif
#ifndef NUM_NODES
#define NUM_NODES 8
#endif
#define ITERATIONS 1000000
#define SEED 0xDEADBEEF
#define MAX_PARENTS_PER_NODE 64 
#define HW_CANDIDATE_SLOTS_PER_NODE 64
#define HW_MAX_CANDIDATES_PER_NODE (HW_CANDIDATE_SLOTS_PER_NODE - 1)
#define DEFAULT_ML_TABLE_DIR "ml"
#define ML_DEBUG_CSV_NAME "readable_debug.csv"
#define DONE_TIMEOUT_US 10000000
#define DEBUG_BUILD_TAG "mcmc_test debug-reset-readback-v3-ml-candidates"
#define VGA_BLACK 0x00
#define VGA_WHITE 0xFF
#define VGA_BLUE  0x03
#define VGA_GREEN 0x1C
#define VGA_RED   0xE0
#define VGA_YELL  0xFC
#define INFERENCE_THREADS 4
#define INFERENCE_SAMPLES 200000
#define INFERENCE_ALPHA 1.0f

char node_names[NUM_NODES][64];
int node_cardinalities[NUM_NODES];

// --- Database Data Structures ---
typedef struct {
    unsigned int parent_bitmask;
    float local_score;           
} ParentSet;

typedef enum {
    CANDIDATE_SOURCE_FIXED,
    CANDIDATE_SOURCE_ML
} CandidateSource;

typedef struct {
    CandidateSource source;
    char ml_dir[256];
    bool dry_run_candidates;
} CandidateLoadConfig;

typedef struct {
    unsigned int parent_mask;
    int parent_count;
    int parent_state_count;
    int node_cardinality;
    float *probabilities;
} LearnedCPT;

typedef struct {
    const LearnedCPT *cpts;
    const int *order;
    int active_count;
    int target_node;
    int target_value;
    int evidence[NUM_NODES];
    int samples;
    uint32_t rng_state;
    double total_weight;
    double target_value_weight;
} InferenceWorkerArgs;

ParentSet precomputed_db[NUM_NODES][MAX_PARENTS_PER_NODE];
int num_candidates[NUM_NODES]; 

// FPGA Pointers
void *h2p_lw_virtual_base;
volatile unsigned int *pio_start=NULL;
volatile unsigned int *pio_seed=NULL;
volatile unsigned int *pio_done=NULL;
volatile unsigned int *pio_best_score=NULL;
volatile unsigned int *pio_iterations=NULL;
volatile unsigned int *pio_active_nodes=NULL;
volatile unsigned int *pio_node_mask=NULL;
volatile unsigned int *pio_reset=NULL;
volatile unsigned int *pio_clock_count=NULL;

volatile unsigned int *mcmc_system_base;
void *fpga_ram_virtual_base;
volatile unsigned int *vga_pixel_ptr=NULL;
volatile unsigned int *vga_char_ptr=NULL;
void *vga_pixel_virtual_base;
void *vga_char_virtual_base;

void VGA_text(int x, int y, const char* text_ptr);
void VGA_text_clear(void);
void VGA_box(int x1, int y1, int x2, int y2, short pixel_color);
void VGA_line(int x1, int y1, int x2, int y2, short c);
void VGA_disc(int x, int y, int r, short pixel_color);
void VGA_arrow(int x1, int y1, int x2, int y2, short c);
void VGA_label_centered(int x, int y, const char* label);
int init_vga(int fd);
void draw_learned_graph_vga(const int* order, int active_count, const unsigned int* parent_masks,
                            float order_score, float graph_score);
void print_fpga_debug_status(const char* label);
int wait_for_done_or_timeout(unsigned int timeout_us);
void print_known_order_score_check(void);
void parse_candidate_args(int argc, char** argv, CandidateLoadConfig* config);
void precompute_fixed_k(int** dataset, int num_samples);
void load_ml_candidate_table(const char* ml_dir);
void normalize_candidate_scores(void);
void validate_candidate_capacity_or_die(const char* source_name);
void build_learned_cpts(int** dataset, int num_samples,
                        const unsigned int* parent_masks, LearnedCPT* cpts);
void free_learned_cpts(LearnedCPT* cpts);
void run_inference_console(int** dataset, int num_samples, const int* order, int active_count,
                           const unsigned int* parent_masks);
void strip_newline(char* line);

// --- Precomputation Math ---
int count_set_bits(unsigned int n) {
    int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

void print_usage(const char* program_name) {
    printf("Usage:\n");
    printf("  %s                 # fixed-k parent-set precompute\n", program_name);
    printf("  %s fixed           # same as default\n", program_name);
    printf("  %s ml              # load ML candidates from ./%s\n", program_name, DEFAULT_ML_TABLE_DIR);
    printf("  %s ml --ml-dir DIR # load ML candidates from DIR/%s\n",
           program_name, ML_DEBUG_CSV_NAME);
    printf("  %s ml --ml-dir DIR --dry-run-candidates\n", program_name);
    printf("\n");
    printf("Current bitstream table capacity: %d candidates/node plus one sentinel.\n",
           HW_MAX_CANDIDATES_PER_NODE);
}

void parse_candidate_args(int argc, char** argv, CandidateLoadConfig* config) {
    config->source = CANDIDATE_SOURCE_FIXED;
    config->dry_run_candidates = false;
    strncpy(config->ml_dir, DEFAULT_ML_TABLE_DIR, sizeof(config->ml_dir) - 1);
    config->ml_dir[sizeof(config->ml_dir) - 1] = '\0';

    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "fixed") == 0 || strcmp(argv[arg], "--fixed") == 0) {
            config->source = CANDIDATE_SOURCE_FIXED;
        } else if (strcmp(argv[arg], "ml") == 0 || strcmp(argv[arg], "--ml") == 0) {
            config->source = CANDIDATE_SOURCE_ML;
        } else if (strcmp(argv[arg], "--candidate-source") == 0 && arg + 1 < argc) {
            arg++;
            if (strcmp(argv[arg], "fixed") == 0) {
                config->source = CANDIDATE_SOURCE_FIXED;
            } else if (strcmp(argv[arg], "ml") == 0) {
                config->source = CANDIDATE_SOURCE_ML;
            } else {
                printf("ERROR: unknown candidate source '%s'\n", argv[arg]);
                print_usage(argv[0]);
                exit(1);
            }
        } else if (strcmp(argv[arg], "--ml-dir") == 0 && arg + 1 < argc) {
            arg++;
            strncpy(config->ml_dir, argv[arg], sizeof(config->ml_dir) - 1);
            config->ml_dir[sizeof(config->ml_dir) - 1] = '\0';
        } else if (strcmp(argv[arg], "--dry-run-candidates") == 0 ||
                   strcmp(argv[arg], "--validate-candidates") == 0) {
            config->dry_run_candidates = true;
        } else if (strcmp(argv[arg], "--help") == 0 || strcmp(argv[arg], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            printf("ERROR: unknown argument '%s'\n", argv[arg]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

void add_candidate_or_die(int node, int* candidate_count, unsigned int parent_mask,
                          float local_score, const char* source_name) {
    if (*candidate_count >= HW_MAX_CANDIDATES_PER_NODE ||
        *candidate_count >= MAX_PARENTS_PER_NODE) {
        printf("ERROR: %s produced too many candidate parent sets for node %d (%s).\n",
               source_name, node, node_names[node]);
        printf("       C array capacity is %d; current RTL usable capacity is %d candidates plus sentinel.\n",
               MAX_PARENTS_PER_NODE, HW_MAX_CANDIDATES_PER_NODE);
        printf("       Reduce ML pruning params or use a bitstream compiled with deeper per-node RAM.\n");
        exit(1);
    }

    precomputed_db[node][*candidate_count].parent_bitmask = parent_mask;
    precomputed_db[node][*candidate_count].local_score = local_score;
    (*candidate_count)++;
}

void normalize_candidate_scores(void) {
    for (int i = 0; i < NUM_NODES; i++) {
        float max_score = -1e30f;
        for (int p = 0; p < num_candidates[i]; p++) {
            if (precomputed_db[i][p].local_score > max_score) {
                max_score = precomputed_db[i][p].local_score;
            }
        }

        for (int p = 0; p < num_candidates[i]; p++) {
            precomputed_db[i][p].local_score -= max_score;
            if (precomputed_db[i][p].local_score < -500.0f) {
                precomputed_db[i][p].local_score = -500.0f;
            }
        }
    }
}

void validate_candidate_capacity_or_die(const char* source_name) {
    bool failed = false;

    for (int node = 0; node < NUM_NODES; node++) {
        if (num_candidates[node] <= 0) {
            printf("ERROR: %s produced no candidate parent sets for node %d (%s).\n",
                   source_name, node, node_names[node]);
            failed = true;
        }

        if (num_candidates[node] > HW_MAX_CANDIDATES_PER_NODE) {
            printf("ERROR: %s candidate table for node %d (%s) has %d entries; "
                   "current RTL supports %d plus sentinel.\n",
                   source_name, node, node_names[node],
                   num_candidates[node], HW_MAX_CANDIDATES_PER_NODE);
            failed = true;
        }
    }

    if (failed) {
        printf("Hardware note: mcmc_system.v currently partitions Avalon address bits "
               "[11:7] by node and [6:1] by candidate word-pair.\n");
        printf("               That gives 64 pair slots per node, so only 63 usable "
               "candidates if one sentinel is kept.\n");
        printf("               Reduce ML --max-candidates-per-node/--max-parent-size, "
               "or change RTL RAM/address widths and recompile the FPGA bitstream.\n");
        exit(1);
    }
}

int split_csv_simple(char* line, char** fields, int max_fields) {
    int count = 0;
    char *cursor = line;

    if (max_fields <= 0) return 0;
    fields[count++] = cursor;
    while (*cursor) {
        if (*cursor == ',') {
            *cursor = '\0';
            if (count < max_fields) fields[count++] = cursor + 1;
        }
        cursor++;
    }
    return count;
}

unsigned int parse_ml_parent_mask_or_die(const char* raw_mask, int line_number) {
    char *end_ptr;
    unsigned long long mask64;

    mask64 = strtoull(raw_mask, &end_ptr, 16);
    while (*end_ptr && isspace((unsigned char)*end_ptr)) end_ptr++;
    if (*end_ptr != '\0') {
        printf("ERROR: ML table line %d has multiple parent-mask chunks; "
               "current hardware/C path supports <=32 nodes only.\n", line_number);
        exit(1);
    }
    if (mask64 > 0xFFFFFFFFULL) {
        printf("ERROR: ML table line %d parent mask 0x%llx exceeds 32 hardware nodes.\n",
               line_number, mask64);
        exit(1);
    }
    return (unsigned int)mask64;
}

void load_ml_candidate_table(const char* ml_dir) {
    char path[512];
    char line[4096];
    FILE* file;
    int line_number = 0;

    snprintf(path, sizeof(path), "%s/%s", ml_dir, ML_DEBUG_CSV_NAME);
    file = fopen(path, "r");
    if (!file) {
        printf("ERROR: failed to open ML candidate table %s\n", path);
        printf("       Generate it with preprocess_bn.py --output-dir %s --emit-hex\n", ml_dir);
        exit(1);
    }

    for (int node = 0; node < NUM_NODES; node++) num_candidates[node] = 0;

    if (!fgets(line, sizeof(line), file)) {
        printf("ERROR: ML candidate table %s is empty\n", path);
        fclose(file);
        exit(1);
    }
    line_number++;

    while (fgets(line, sizeof(line), file)) {
        char* fields[16];
        int field_count;
        int node;
        unsigned int parent_mask;
        float log_score;

        line_number++;
        strip_newline(line);
        field_count = split_csv_simple(line, fields, 16);
        if (field_count < 8) {
            printf("ERROR: malformed ML candidate CSV line %d in %s\n", line_number, path);
            fclose(file);
            exit(1);
        }

        node = atoi(fields[0]);
        if (node < 0 || node >= NUM_NODES) {
            printf("ERROR: ML candidate CSV line %d has node_id=%d, but C NUM_NODES=%d\n",
                   line_number, node, NUM_NODES);
            fclose(file);
            exit(1);
        }
        if (strcmp(fields[1], node_names[node]) != 0) {
            printf("ERROR: ML candidate CSV line %d node name mismatch: table has '%s', "
                   "dataset column %d is '%s'.\n",
                   line_number, fields[1], node, node_names[node]);
            fclose(file);
            exit(1);
        }

        parent_mask = parse_ml_parent_mask_or_die(fields[6], line_number);
        if (parent_mask & (1U << node)) {
            printf("ERROR: ML candidate CSV line %d has node %s as its own parent.\n",
                   line_number, node_names[node]);
            fclose(file);
            exit(1);
        }
        log_score = strtof(fields[7], NULL);
        add_candidate_or_die(node, &num_candidates[node], parent_mask, log_score, "ML");
    }

    fclose(file);
    normalize_candidate_scores();
    validate_candidate_capacity_or_die("ML");

    printf("Loaded ML candidate table from %s\n", path);
    for (int node = 0; node < NUM_NODES; node++) {
        printf("  node %d %-12s candidates=%d\n", node, node_names[node], num_candidates[node]);
    }
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
        size_t cfg = 0;
        int target_val = dataset[row][target_node];

        for (int p = 0; p < num_parents; p++) {
            int p_node = parent_indices[p];
            cfg = cfg * (size_t)node_cardinalities[p_node] + (size_t)dataset[row][p_node];
        }

        counts[cfg * (size_t)r + (size_t)target_val]++;
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

// Comparison function for qsort (descending order)
int cmp_candidates(const void* a, const void* b) {
    float score_a = ((ParentSet*)a)->local_score;
    float score_b = ((ParentSet*)b)->local_score;
    if (score_a < score_b) return 1;
    if (score_a > score_b) return -1;
    return 0;
}

void precompute_fixed_k(int** dataset, int num_samples) {
    for (int i = 0; i < NUM_NODES; i++) {
        int candidate_count = 0;
        unsigned int mask_k0 = 0;
        add_candidate_or_die(i, &candidate_count, mask_k0,
                             calculate_bde_score(dataset, num_samples, i, mask_k0),
                             "fixed-k");

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            unsigned int mask_k1 = (1 << p1);
            add_candidate_or_die(i, &candidate_count, mask_k1,
                                 calculate_bde_score(dataset, num_samples, i, mask_k1),
                                 "fixed-k");
        }

        for (int p1 = 0; p1 < NUM_NODES; p1++) {
            if (p1 == i) continue;
            for (int p2 = p1 + 1; p2 < NUM_NODES; p2++) {
                if (p2 == i) continue;
                unsigned int mask_k2 = (1 << p1) | (1 << p2);
                add_candidate_or_die(i, &candidate_count, mask_k2,
                                     calculate_bde_score(dataset, num_samples, i, mask_k2),
                                     "fixed-k");
            }
        }
        num_candidates[i] = candidate_count;
    }

    normalize_candidate_scores();
    validate_candidate_capacity_or_die("fixed-k");
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

int32_t float_to_q16(float val) {
    float scaled = val * 65536.0f;
    if (scaled > 2147483647.0f) return 2147483647; // 32-bit signed max
    if (scaled < -2147483648.0f) return -2147483648; // 32-bit signed min
    return (int32_t)scaled;
}

float q16_to_float(uint32_t raw) {
    return (float)((int32_t)raw) / 65536.0f;
}

float log_add_float(float a, float b) {
    if (!isfinite(a)) return b;
    if (!isfinite(b)) return a;
    float max_val = (a > b) ? a : b;
    float min_val = (a < b) ? a : b;
    return max_val + log1pf(expf(min_val - max_val));
}

unsigned int allowed_mask_for_order(const int* order, int order_pos) {
    unsigned int allowed_mask = 0;
    for (int i = 0; i < order_pos; i++) {
        allowed_mask |= (1U << order[i]);
    }
    return allowed_mask;
}

bool candidate_compatible(unsigned int parent_mask, unsigned int allowed_mask) {
    return (parent_mask & allowed_mask) == parent_mask;
}

float score_order_logsum(const int* order, int active_count) {
    float total_score = 0.0f;
    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int node = order[order_pos];
        unsigned int allowed_mask = allowed_mask_for_order(order, order_pos);
        float node_score = -INFINITY;

        for (int p = 0; p < num_candidates[node]; p++) {
            unsigned int parent_mask = precomputed_db[node][p].parent_bitmask;
            if (candidate_compatible(parent_mask, allowed_mask)) {
                node_score = log_add_float(node_score, precomputed_db[node][p].local_score);
            }
        }
        total_score += node_score;
    }
    return total_score;
}

int node_index_by_name(const char* name) {
    for (int i = 0; i < NUM_NODES; i++) {
        if (strcmp(node_names[i], name) == 0) return i;
    }
    return -1;
}

void print_known_order_score_check(void) {
    const char* pasted_names[] = {
        "either", "xray", "dysp", "bronc", "lung", "smoke", "tub", "asia"
    };
    int pasted_order[NUM_NODES];
    int initial_order[NUM_NODES];
    int pasted_count = (int)(sizeof(pasted_names) / sizeof(pasted_names[0]));

    if (NUM_NODES != pasted_count) {
        printf("DEBUG score self-check skipped: Asia reference order has %d nodes, dataset has %d\n",
               pasted_count, NUM_NODES);
        return;
    }

    for (int i = 0; i < NUM_NODES; i++) {
        pasted_order[i] = node_index_by_name(pasted_names[i]);
        initial_order[i] = i;
        if (pasted_order[i] < 0) {
            printf("DEBUG score self-check skipped: node name '%s' not found\n", pasted_names[i]);
            return;
        }
    }

    printf("DEBUG score self-check: pasted-order=%.6f initial-order=%.6f\n",
           score_order_logsum(pasted_order, NUM_NODES),
           score_order_logsum(initial_order, NUM_NODES));
}

float choose_best_graph_for_order(const int* order, int active_count, unsigned int* best_parent_masks) {
    float graph_score = 0.0f;

    for (int i = 0; i < NUM_NODES; i++) {
        best_parent_masks[i] = 0;
    }

    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int node = order[order_pos];
        unsigned int allowed_mask = allowed_mask_for_order(order, order_pos);
        float best_local_score = -INFINITY;
        unsigned int best_parent_mask = 0;

        for (int p = 0; p < num_candidates[node]; p++) {
            unsigned int parent_mask = precomputed_db[node][p].parent_bitmask;
            float local_score = precomputed_db[node][p].local_score;
            if (candidate_compatible(parent_mask, allowed_mask) && local_score > best_local_score) {
                best_local_score = local_score;
                best_parent_mask = parent_mask;
            }
        }

        best_parent_masks[node] = best_parent_mask;
        graph_score += best_local_score;
    }

    return graph_score;
}

int parent_state_from_values(unsigned int parent_mask, const int* values) {
    size_t state = 0;

    for (int node = 0; node < NUM_NODES; node++) {
        if (parent_mask & (1U << node)) {
            int value = values[node];
            int cardinality = node_cardinalities[node];
            if (value < 0 || value >= cardinality) {
                printf("ERROR: value %d for node %s outside state range 0..%d.\n",
                       value, node_names[node], cardinality - 1);
                exit(1);
            }
            state = state * (size_t)cardinality + (size_t)value;
            if (state > (size_t)INT_MAX) {
                printf("ERROR: parent-state index overflow.\n");
                exit(1);
            }
        }
    }

    return (int)state;
}

void build_learned_cpts(int** dataset, int num_samples,
                        const unsigned int* parent_masks, LearnedCPT* cpts) {
    for (int node = 0; node < NUM_NODES; node++) {
        unsigned int parent_mask = parent_masks[node];
        int parent_count = count_set_bits(parent_mask);
        size_t parent_state_count = checked_parent_config_count(parent_mask, "building learned CPTs");
        int node_cardinality = node_cardinalities[node];
        size_t entry_count = checked_cpt_entry_count(parent_state_count, node_cardinality,
                                                     "building learned CPTs");
        int *counts = (int*)calloc(entry_count, sizeof(int));
        float alpha_ijk;

        if (parent_state_count > (size_t)INT_MAX) {
            printf("ERROR: parent-state count too large for node %s.\n", node_names[node]);
            exit(1);
        }
        if (!counts) {
            printf("ERROR: failed to allocate CPT counts\n");
            exit(1);
        }

        cpts[node].parent_mask = parent_mask;
        cpts[node].parent_count = parent_count;
        cpts[node].parent_state_count = (int)parent_state_count;
        cpts[node].node_cardinality = node_cardinality;
        cpts[node].probabilities = (float*)calloc(entry_count, sizeof(float));
        if (!cpts[node].probabilities) {
            printf("ERROR: failed to allocate CPT probabilities\n");
            exit(1);
        }

        for (int row = 0; row < num_samples; row++) {
            int state = parent_state_from_values(parent_mask, dataset[row]);
            int value = dataset[row][node];
            counts[(size_t)state * (size_t)node_cardinality + (size_t)value]++;
        }

        alpha_ijk = INFERENCE_ALPHA / (float)entry_count;
        for (size_t state = 0; state < parent_state_count; state++) {
            float total = 0.0f;

            for (int value = 0; value < node_cardinality; value++) {
                total += (float)counts[state * (size_t)node_cardinality + (size_t)value];
            }

            for (int value = 0; value < node_cardinality; value++) {
                size_t idx = state * (size_t)node_cardinality + (size_t)value;
                cpts[node].probabilities[idx] =
                    ((float)counts[idx] + alpha_ijk) /
                    (total + (float)node_cardinality * alpha_ijk);
            }
        }

        free(counts);
    }
}

void free_learned_cpts(LearnedCPT* cpts) {
    for (int node = 0; node < NUM_NODES; node++) {
        free(cpts[node].probabilities);
        cpts[node].probabilities = NULL;
    }
}

uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    if (x == 0) x = 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

float random_unit_float(uint32_t *state) {
    return (float)(xorshift32(state) & 0x00FFFFFFu) / 16777216.0f;
}

int sample_categorical(const float* probabilities, int cardinality, uint32_t *rng_state) {
    float u = random_unit_float(rng_state);
    float cumulative = 0.0f;

    for (int value = 0; value < cardinality - 1; value++) {
        cumulative += probabilities[value];
        if (u < cumulative) return value;
    }

    return cardinality - 1;
}

void* inference_worker(void* arg_ptr) {
    InferenceWorkerArgs *args = (InferenceWorkerArgs*)arg_ptr;
    int values[NUM_NODES];
    double total_weight = 0.0;
    double target_value_weight = 0.0;
    uint32_t rng_state = args->rng_state;

    for (int sample = 0; sample < args->samples; sample++) {
        double weight = 1.0;

        for (int node = 0; node < NUM_NODES; node++) values[node] = 0;

        for (int order_pos = 0; order_pos < args->active_count; order_pos++) {
            int node = args->order[order_pos];
            int parent_state = parent_state_from_values(args->cpts[node].parent_mask, values);
            int cardinality = args->cpts[node].node_cardinality;
            const float *probabilities =
                &args->cpts[node].probabilities[(size_t)parent_state * (size_t)cardinality];
            int observed = args->evidence[node];

            if (observed >= 0) {
                values[node] = observed;
                weight *= (double)probabilities[observed];
            } else {
                values[node] = sample_categorical(probabilities, cardinality, &rng_state);
            }
        }

        total_weight += weight;
        if (values[args->target_node] == args->target_value) {
            target_value_weight += weight;
        }
    }

    args->rng_state = rng_state;
    args->total_weight = total_weight;
    args->target_value_weight = target_value_weight;
    return NULL;
}

double infer_probability_threaded(const LearnedCPT* cpts, const int* order, int active_count,
                                  int target_node, int target_value, const int* evidence,
                                  int samples, int thread_count) {
    pthread_t threads[INFERENCE_THREADS];
    InferenceWorkerArgs args[INFERENCE_THREADS];
    bool thread_started[INFERENCE_THREADS];
    double total_weight = 0.0;
    double target_value_weight = 0.0;

    if (thread_count > INFERENCE_THREADS) thread_count = INFERENCE_THREADS;
    if (thread_count < 1) thread_count = 1;

    for (int t = 0; t < thread_count; t++) {
        int thread_samples = samples / thread_count;
        if (t < (samples % thread_count)) thread_samples++;

        args[t].cpts = cpts;
        args[t].order = order;
        args[t].active_count = active_count;
        args[t].target_node = target_node;
        args[t].target_value = target_value;
        args[t].samples = thread_samples;
        args[t].rng_state = 0xA5A50000u ^ (uint32_t)(SEED + 0x9E3779B9u * (t + 1));
        args[t].total_weight = 0.0;
        args[t].target_value_weight = 0.0;
        thread_started[t] = false;
        for (int node = 0; node < NUM_NODES; node++) args[t].evidence[node] = evidence[node];

        if (pthread_create(&threads[t], NULL, inference_worker, &args[t]) == 0) {
            thread_started[t] = true;
        } else {
            inference_worker(&args[t]);
        }
    }

    for (int t = 0; t < thread_count; t++) {
        if (thread_started[t]) pthread_join(threads[t], NULL);
    }

    for (int t = 0; t < thread_count; t++) {
        total_weight += args[t].total_weight;
        target_value_weight += args[t].target_value_weight;
    }

    if (total_weight <= 0.0) return NAN;
    return target_value_weight / total_weight;
}

void print_inference_nodes(void) {
    printf("\nQueryable discrete nodes:\n");
    for (int node = 0; node < NUM_NODES; node++) {
        printf("  %d: %s states=0..%d\n",
               node, node_names[node], node_cardinalities[node] - 1);
    }
}

void strip_newline(char* line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

int parse_node_token(const char* token) {
    char *end_ptr;
    long numeric_id;

    if (!token || token[0] == '\0') return -1;

    numeric_id = strtol(token, &end_ptr, 10);
    if (*end_ptr == '\0' && numeric_id >= 0 && numeric_id < NUM_NODES) {
        return (int)numeric_id;
    }

    return node_index_by_name(token);
}

int parse_state_value(int node, const char* token) {
    char lower[16];
    char *end_ptr;
    long numeric_value;
    int i;

    if (!token || token[0] == '\0') return -1;

    numeric_value = strtol(token, &end_ptr, 10);
    while (*end_ptr && isspace((unsigned char)*end_ptr)) end_ptr++;
    if (*end_ptr == '\0') {
        if (numeric_value >= 0 && numeric_value < node_cardinalities[node]) {
            return (int)numeric_value;
        }
        return -1;
    }

    for (i = 0; token[i] && i < (int)sizeof(lower) - 1; i++) {
        lower[i] = (char)tolower((unsigned char)token[i]);
    }
    lower[i] = '\0';

    if (node_cardinalities[node] == 2) {
        if (strcmp(lower, "false") == 0 || strcmp(lower, "no") == 0) return 0;
        if (strcmp(lower, "true") == 0 || strcmp(lower, "yes") == 0) return 1;
    }

    return -1;
}

bool parse_evidence_line(char* line, int* evidence) {
    char *token;

    for (int node = 0; node < NUM_NODES; node++) evidence[node] = -1;
    strip_newline(line);

    if (line[0] == '\0' || strcmp(line, "none") == 0 || strcmp(line, "n") == 0) {
        return true;
    }
    if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
        return false;
    }

    token = strtok(line, " ,\t");
    while (token) {
        char *equals = strchr(token, '=');
        int node;
        int value;

        if (!equals) {
            printf("Ignoring evidence token '%s' (use name=value)\n", token);
            token = strtok(NULL, " ,\t");
            continue;
        }

        *equals = '\0';
        node = parse_node_token(token);
        if (node < 0) {
            printf("Ignoring evidence token '%s=%s' (unknown node)\n", token, equals + 1);
        } else {
            value = parse_state_value(node, equals + 1);
            if (value < 0) {
                printf("Ignoring evidence token '%s=%s' (value must be 0..%d)\n",
                       token, equals + 1, node_cardinalities[node] - 1);
            } else {
                evidence[node] = value;
            }
        }

        token = strtok(NULL, " ,\t");
    }

    return true;
}

int prompt_for_state_value(int node, const char* line) {
    int value = parse_state_value(node, line);
    if (value < 0) {
        printf("Unknown state '%s' for %s. Use 0..%d.\n",
               line, node_names[node], node_cardinalities[node] - 1);
    }
    return value;
}

void print_evidence_summary(const int* evidence) {
    bool first = true;

    for (int node = 0; node < NUM_NODES; node++) {
        if (evidence[node] >= 0) {
            printf("%s%s=%d", first ? "" : ", ", node_names[node], evidence[node]);
            first = false;
        }
    }

    if (first) printf("none");
}

void run_inference_console(int** dataset, int num_samples, const int* order, int active_count,
                           const unsigned int* parent_masks) {
    LearnedCPT cpts[NUM_NODES];
    char line[256];

    for (int node = 0; node < NUM_NODES; node++) cpts[node].probabilities = NULL;
    build_learned_cpts(dataset, num_samples, parent_masks, cpts);

    printf("\n=== Interactive Probability Demo ===\n");
    printf("Model: learned DAG + dataset-estimated discrete CPTs\n");
    printf("Inference: %d-sample likelihood weighting across %d pthread workers\n",
           INFERENCE_SAMPLES, INFERENCE_THREADS);
    printf("Enter q at the target prompt to exit.\n");
    print_inference_nodes();

    while (1) {
        int target_node;
        int target_value;
        int evidence[NUM_NODES];
        double probability;

        printf("\nTarget node for P(node=value | evidence): ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        strip_newline(line);
        if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) break;

        target_node = parse_node_token(line);
        if (target_node < 0) {
            printf("Unknown node '%s'. Use a node number or name.\n", line);
            continue;
        }

        printf("Target value for %s (0..%d): ",
               node_names[target_node], node_cardinalities[target_node] - 1);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        strip_newline(line);
        target_value = prompt_for_state_value(target_node, line);
        if (target_value < 0) continue;

        printf("Evidence pairs, e.g. smoke=1 age=2; blank/none for no evidence: ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (!parse_evidence_line(line, evidence)) break;

        probability = infer_probability_threaded(cpts, order, active_count, target_node,
                                                 target_value, evidence, INFERENCE_SAMPLES,
                                                 INFERENCE_THREADS);

        printf("P(%s=%d | ", node_names[target_node], target_value);
        print_evidence_summary(evidence);
        if (isnan(probability)) {
            printf(") could not be estimated; evidence has near-zero support.\n");
        } else {
            printf(") = %.4f\n", probability);
        }
    }

    free_learned_cpts(cpts);
}

void print_learned_graph(const int* order, int active_count) {
    unsigned int best_parent_masks[NUM_NODES];
    int initial_order[NUM_NODES];
    float graph_score = choose_best_graph_for_order(order, active_count, best_parent_masks);
    float order_score = score_order_logsum(order, active_count);
    float initial_order_score;
    bool printed_edge = false;

    for (int i = 0; i < active_count; i++) {
        initial_order[i] = i;
    }
    initial_order_score = score_order_logsum(initial_order, active_count);

    printf("Order log-sum score: %.6f\n", order_score);
    printf("Initial-order log-sum score: %.6f\n", initial_order_score);
    if (order_score + 0.001f < initial_order_score) {
        printf("WARNING: returned order scores below initial order in the C model.\n");
    }
    printf("Best compatible graph score: %.6f\n", graph_score);
    printf("Learned DAG edges:\n");

    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int child = order[order_pos];
        unsigned int parent_mask = best_parent_masks[child];
        for (int parent = 0; parent < active_count; parent++) {
            if (parent_mask & (1U << parent)) {
                printf("  %s -> %s\n", node_names[parent], node_names[child]);
                printed_edge = true;
            }
        }
    }

    if (!printed_edge) {
        printf("  (none)\n");
    }

    printf("Parent sets by node:\n");
    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int child = order[order_pos];
        unsigned int parent_mask = best_parent_masks[child];
        bool first_parent = true;
        printf("  %s <- ", node_names[child]);
        if (parent_mask == 0) {
            printf("(none)");
        } else {
            for (int parent = 0; parent < active_count; parent++) {
                if (parent_mask & (1U << parent)) {
                    printf("%s%s", first_parent ? "" : ", ", node_names[parent]);
                    first_parent = false;
                }
            }
        }
        printf("\n");
    }
    draw_learned_graph_vga(order, active_count, best_parent_masks, order_score, graph_score);
}

int init_vga(int fd) {
    vga_char_virtual_base = mmap(NULL, FPGA_CHAR_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_CHAR_BASE);
    if (vga_char_virtual_base == MAP_FAILED) {
        printf("WARNING: VGA char mmap failed; skipping VGA graph\n");
        vga_char_ptr = NULL;
        return -1;
    }
    vga_char_ptr = (unsigned int*)vga_char_virtual_base;

    vga_pixel_virtual_base = mmap(NULL, FPGA_ONCHIP_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SDRAM_BASE);
    if (vga_pixel_virtual_base == MAP_FAILED) {
        printf("WARNING: VGA pixel mmap failed; skipping VGA graph\n");
        vga_pixel_ptr = NULL;
        return -1;
    }
    vga_pixel_ptr = (unsigned int*)vga_pixel_virtual_base;
    return 0;
}

void VGA_text(int x, int y, const char* text_ptr) {
    volatile char* character_buffer = (char*)vga_char_ptr;
    int offset = (y << 7) + x;
    if (!vga_char_ptr) return;
    while (*text_ptr) {
        *(character_buffer + offset) = *text_ptr;
        text_ptr++;
        offset++;
    }
}

void VGA_text_clear(void) {
    volatile char* character_buffer = (char*)vga_char_ptr;
    if (!vga_char_ptr) return;
    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 80; x++) {
            *(character_buffer + (y << 7) + x) = ' ';
        }
    }
}

#define CLAMP_COORD(v, lo, hi) do { if ((v) < (lo)) (v) = (lo); if ((v) > (hi)) (v) = (hi); } while (0)
#define SWAP_INT(a,b) do { int tmp = (a); (a) = (b); (b) = tmp; } while (0)

void VGA_box(int x1, int y1, int x2, int y2, short pixel_color) {
    if (!vga_pixel_ptr) return;
    CLAMP_COORD(x1, 0, 639); CLAMP_COORD(x2, 0, 639);
    CLAMP_COORD(y1, 0, 479); CLAMP_COORD(y2, 0, 479);
    if (x1 > x2) SWAP_INT(x1, x2);
    if (y1 > y2) SWAP_INT(y1, y2);
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            *((char*)vga_pixel_ptr + (y << 10) + x) = pixel_color;
        }
    }
}

void VGA_disc(int x, int y, int r, short pixel_color) {
    if (!vga_pixel_ptr) return;
    int rr = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= rr + r) {
                int px = x + dx, py = y + dy;
                CLAMP_COORD(px, 0, 639); CLAMP_COORD(py, 0, 479);
                *((char*)vga_pixel_ptr + (py << 10) + px) = pixel_color;
            }
        }
    }
}

void VGA_line(int x1, int y1, int x2, int y2, short c) {
    if (!vga_pixel_ptr) return;
    CLAMP_COORD(x1, 0, 639); CLAMP_COORD(x2, 0, 639);
    CLAMP_COORD(y1, 0, 479); CLAMP_COORD(y2, 0, 479);
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        *((char*)vga_pixel_ptr + (y1 << 10) + x1) = c;
        if (x1 == x2 && y1 == y2) break;
        int e2 = err << 1;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void VGA_arrow(int x1, int y1, int x2, int y2, short c) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    int radius = 23;
    int sx, sy, ex, ey;
    int ax1, ay1, ax2, ay2;
    int arrow_len = 13;
    float angle = atan2f((float)(y2 - y1), (float)(x2 - x1));

    if (len < 1.0f) return;

    sx = x1 + (int)((dx / len) * (float)radius);
    sy = y1 + (int)((dy / len) * (float)radius);
    ex = x2 - (int)((dx / len) * (float)radius);
    ey = y2 - (int)((dy / len) * (float)radius);

    ax1 = ex - (int)((float)arrow_len * cosf(angle - 0.55f));
    ay1 = ey - (int)((float)arrow_len * sinf(angle - 0.55f));
    ax2 = ex - (int)((float)arrow_len * cosf(angle + 0.55f));
    ay2 = ey - (int)((float)arrow_len * sinf(angle + 0.55f));

    VGA_line(sx, sy, ex, ey, c);
    VGA_line(ex, ey, ax1, ay1, c);
    VGA_line(ex, ey, ax2, ay2, c);
}

void VGA_label_centered(int x, int y, const char* label) {
    int len = (int)strlen(label);
    int text_x = (x / 8) - (len / 2) + 1;
    int text_y = y / 8;
    CLAMP_COORD(text_x, 0, 79);
    CLAMP_COORD(text_y, 0, 59);
    VGA_text(text_x, text_y, label);
}

void draw_learned_graph_vga(const int* order, int active_count, const unsigned int* parent_masks,
                            float order_score, float graph_score) {
    if (!vga_pixel_ptr || !vga_char_ptr) return;

    int x[NUM_NODES];
    int y[NUM_NODES];
    int layer[NUM_NODES];
    int layer_counts[NUM_NODES + 1];
    int layer_seen[NUM_NODES + 1];
    int max_layer = 0;
    int left = 70;
    int right = 565;
    int top = 82;
    int bottom = 420;
    char line[80];

    for (int i = 0; i < NUM_NODES; i++) {
        x[i] = 0;
        y[i] = 0;
        layer[i] = 0;
        layer_counts[i] = 0;
        layer_seen[i] = 0;
    }
    layer_counts[NUM_NODES] = 0;
    layer_seen[NUM_NODES] = 0;

    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int child = order[order_pos];
        unsigned int parent_mask = parent_masks[child];
        int child_layer = 0;

        for (int prior = 0; prior < order_pos; prior++) {
            int parent = order[prior];
            if (parent_mask & (1U << parent)) {
                int candidate_layer = layer[parent] + 1;
                if (candidate_layer > child_layer) child_layer = candidate_layer;
            }
        }

        if (child_layer > NUM_NODES) child_layer = NUM_NODES;
        layer[child] = child_layer;
        if (child_layer > max_layer) max_layer = child_layer;
    }

    if (max_layer < 1) max_layer = 1;

    for (int i = 0; i < active_count; i++) {
        int node = order[i];
        layer_counts[layer[node]]++;
    }

    for (int i = 0; i < active_count; i++) {
        int node = order[i];
        int node_layer = layer[node];
        int idx_in_layer = layer_seen[node_layer]++;
        int count_in_layer = layer_counts[node_layer];
        int x_span = right - left;
        int y_span = bottom - top;

        x[node] = left + (x_span * node_layer) / max_layer;
        y[node] = top + (y_span * (idx_in_layer + 1)) / (count_in_layer + 1);
    }

    VGA_box(0, 0, 639, 479, VGA_BLACK);
    VGA_text_clear();
    VGA_text(1, 1, "Bayesian Network Learned DAG");
    snprintf(line, sizeof(line), "order %.2f  graph %.2f", order_score, graph_score);
    VGA_text(1, 2, line);

    for (int order_pos = 0; order_pos < active_count; order_pos++) {
        int child = order[order_pos];
        unsigned int parent_mask = parent_masks[child];
        for (int parent = 0; parent < active_count; parent++) {
            if (parent_mask & (1U << parent)) {
                VGA_arrow(x[parent], y[parent], x[child], y[child], VGA_WHITE);
            }
        }
    }

    for (int i = 0; i < active_count; i++) {
        int node = order[i];
        VGA_disc(x[node], y[node], 21, VGA_BLUE);
        VGA_disc(x[node], y[node], 17, VGA_GREEN);
        VGA_label_centered(x[node], y[node], node_names[node]);
    }
}

void print_fpga_debug_status(const char* label) {
    uint32_t raw_order0 = 0;
    uint32_t raw_order1 = 0;
    uint32_t best_score_raw = pio_best_score ? *pio_best_score : 0;

    if (mcmc_system_base) {
        raw_order0 = *(mcmc_system_base + 2048);
        raw_order1 = *(mcmc_system_base + 2049);
    }

    printf("[FPGA DEBUG] %s\n", label);
    printf("  start=%u reset=%u done=%u clk_count=%u\n",
           pio_start ? *pio_start : 0,
           pio_reset ? *pio_reset : 0,
           pio_done ? *pio_done : 0,
           pio_clock_count ? *pio_clock_count : 0);
    printf("  iterations=%u active_nodes=%u node_mask=0x%08x seed=0x%08x\n",
           pio_iterations ? *pio_iterations : 0,
           pio_active_nodes ? *pio_active_nodes : 0,
           pio_node_mask ? *pio_node_mask : 0,
           pio_seed ? *pio_seed : 0);
    printf("  best_score_raw=0x%08x best_score_q16=%.6f raw_order_words=0x%08x 0x%08x\n",
           best_score_raw,
           q16_to_float(best_score_raw),
           raw_order0,
           raw_order1);
    printf("  decoded_order_first8=%u,%u,%u,%u,%u,%u,%u,%u\n",
           raw_order0 & 0x1F,
           (raw_order0 >> 5) & 0x1F,
           (raw_order0 >> 10) & 0x1F,
           (raw_order0 >> 15) & 0x1F,
           raw_order1 & 0x1F,
           (raw_order1 >> 5) & 0x1F,
           (raw_order1 >> 10) & 0x1F,
           (raw_order1 >> 15) & 0x1F);
}

int wait_for_done_or_timeout(unsigned int timeout_us) {
    struct timeval start_time, now;
    
    gettimeofday(&start_time, NULL);
    while (*pio_done == 0) {
        unsigned int elapsed_us;
        usleep(1000); 
        gettimeofday(&now, NULL);
        elapsed_us = (unsigned int)((now.tv_sec - start_time.tv_sec) * 1000000u +
                                    (now.tv_usec - start_time.tv_usec));

        if (elapsed_us >= timeout_us) {
            printf("\n[ERROR] FPGA hardware timed out after %.3f seconds.\n", 
                   (double)elapsed_us / 1000000.0);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    CandidateLoadConfig candidate_config;
    parse_candidate_args(argc, argv, &candidate_config);

    printf("Build ID: %s | %s %s\n", DEBUG_BUILD_TAG, __DATE__, __TIME__);
    printf("Candidate source: %s%s%s\n",
           candidate_config.source == CANDIDATE_SOURCE_FIXED ? "fixed-k" : "ML table",
           candidate_config.source == CANDIDATE_SOURCE_ML ? " from " : "",
           candidate_config.source == CANDIDATE_SOURCE_ML ? candidate_config.ml_dir : "");

    // Load Dataset & Precompute/Load candidate tables (ARM)
    printf("Loading dataset and candidate tables...\n");
    char samples_path[256];
    sprintf(samples_path, "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    int num_samples;
    int** dataset = load_csv(samples_path, &num_samples, NUM_NODES);
    if (candidate_config.source == CANDIDATE_SOURCE_ML) {
        load_ml_candidate_table(candidate_config.ml_dir);
    } else {
        precompute_fixed_k(dataset, num_samples);
    }
    print_known_order_score_check();

    if (candidate_config.dry_run_candidates) {
        printf("Dry run complete: candidate tables fit this C build and current RTL capacity.\n");
        return 0;
    }

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
    pio_iterations    = (unsigned int *)(h2p_lw_virtual_base + PIO_ITERATIONS_OFFSET);
    pio_active_nodes  = (unsigned int *)(h2p_lw_virtual_base + PIO_ACTIVE_NODES_OFFSET);
    pio_node_mask     = (unsigned int *)(h2p_lw_virtual_base + PIO_NODE_MASK);
    pio_reset         = (unsigned int *)(h2p_lw_virtual_base + PIO_RESET_OFFSET);
    pio_clock_count   = (unsigned int *)(h2p_lw_virtual_base + PIO_CLK_COUNT_OFFSET);

    init_vga(fd);
    
    fpga_ram_virtual_base = mmap( NULL, FPGA_ONCHIP_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_ONCHIP_BASE); 
    if( fpga_ram_virtual_base == MAP_FAILED ) { printf( "ERROR: mmap3() failed...\n" ); close( fd ); return(1); }
    mcmc_system_base = (unsigned int *)fpga_ram_virtual_base;
    print_fpga_debug_status("after mmap before table write");

    printf("Writing precomputed scores to FPGA broadcast memory...\n");
    for (int i = 0; i < NUM_NODES; i++) {
        for (int p = 0; p < num_candidates[i]; p++) {
            uint32_t mask = precomputed_db[i][p].parent_bitmask;
            int32_t q16_score = float_to_q16(precomputed_db[i][p].local_score);
            int base_offset = (i << 7) | (p << 1); 
            
            *(mcmc_system_base + base_offset)     = (uint32_t)q16_score; 
            *(mcmc_system_base + base_offset + 1) = mask; 
        }
        
        int sentinel_offset = (i << 7) | (num_candidates[i] << 1); 
        *(mcmc_system_base + sentinel_offset)     = 0x00000000;
        *(mcmc_system_base + sentinel_offset + 1) = 0xFFFFFFFF;
    }
    
    for (int i = NUM_NODES; i < 32; i++) {
        int sentinel_offset = (i << 7) | (0 << 1); 
        *(mcmc_system_base + sentinel_offset)     = 0x00000000; 
        *(mcmc_system_base + sentinel_offset + 1) = 0xFFFFFFFF; 
    }

    *pio_start = 0;
    *pio_reset = 1;      
    usleep(10);           
    *pio_reset = 0;      
    usleep(10);

    *pio_iterations = ITERATIONS;
    *pio_active_nodes = NUM_NODES;
    *pio_node_mask = NUM_NODES - 1;
    *pio_seed = SEED;

    printf("Starting 32-Node Engine...\n");
    
    *pio_start = 1;
    usleep(10);

    if (wait_for_done_or_timeout(DONE_TIMEOUT_US) != 0) {
        *pio_start = 0;
        return 1;
    }
    
    *pio_start = 0;            
    
    uint32_t clocks = *pio_clock_count;
    float time_seconds = (float)clocks / 50000000.0f; 
    
    printf("\n=== MCMC Hardware Complete ===\n");
    printf("Hardware Iterations: %d\n", *pio_iterations);
    printf("Cycle Count:         %u clocks\n", clocks);
    printf("Execution Time:      %f seconds\n\n", time_seconds);
    
    int best_order[32];
    int active_count = (int)(*pio_active_nodes);
    
    for (int i = 0; i < active_count; i++) {
        uint32_t word = *(mcmc_system_base + 2048 + (i / 4));
        best_order[i] = (word >> ((i % 4) * 5)) & 0x1F;
    }
    
    print_learned_graph(best_order, active_count);
    unsigned int learned_parent_masks[NUM_NODES];
    choose_best_graph_for_order(best_order, active_count, learned_parent_masks);
    // run_inference_console(dataset, num_samples, best_order, active_count, learned_parent_masks);

    return 0;
}
