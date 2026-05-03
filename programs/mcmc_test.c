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
#define PIO_ITERATIONS_OFFSET   0x40 
#define PIO_ACTIVE_NODES_OFFSET 0x50 
#define PIO_NODE_MASK           0x60 
#define PIO_RESET_OFFSET        0x70 
#define PIO_CLK_COUNT_OFFSET    0x80 

#define DATASET_NAME "asia"
#define NUM_NODES 8
#define ITERATIONS 100000
#define SEED 0xDEADBEEF
#define MAX_PARENTS_PER_NODE 64 
#define DONE_TIMEOUT_US 10000000
#define DEBUG_BUILD_TAG "mcmc_test debug-reset-readback-v1"
#define VGA_BLACK 0x00
#define VGA_WHITE 0xFF
#define VGA_BLUE  0x03
#define VGA_GREEN 0x1C
#define VGA_RED   0xE0
#define VGA_YELL  0xFC

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

    // Normalization to prevent Q16.16 FPGA Overflow
    for (int i = 0; i < NUM_NODES; i++) {
        // Find the maximum score for this specific node
        float max_score = -1e30f;
        for (int p = 0; p < num_candidates[i]; p++) {
            if (precomputed_db[i][p].local_score > max_score) {
                max_score = precomputed_db[i][p].local_score;
            }
        }
        
        // Shift scores and CLAMP them to prevent signed underflow
        for (int p = 0; p < num_candidates[i]; p++) {
            precomputed_db[i][p].local_score -= max_score;
            
            // Clamp terrible scores to -500.0f
            // -500 * 65536 * 32 nodes = -1,048,576,000 (Safely inside 32-bit limit)
            if (precomputed_db[i][p].local_score < -500.0f) {
                precomputed_db[i][p].local_score = -500.0f;
            }
        }
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
    const char* pasted_names[NUM_NODES] = {
        "either", "xray", "dysp", "bronc", "lung", "smoke", "tub", "asia"
    };
    int pasted_order[NUM_NODES];
    int initial_order[NUM_NODES];

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
    unsigned int last_report_us = 0;

    gettimeofday(&start_time, NULL);
    while (*pio_done == 0) {
        unsigned int elapsed_us;
        usleep(1000);
        gettimeofday(&now, NULL);
        elapsed_us = (unsigned int)((now.tv_sec - start_time.tv_sec) * 1000000u +
                                    (now.tv_usec - start_time.tv_usec));

        if (elapsed_us - last_report_us >= 250000u) {
            print_fpga_debug_status("waiting for done");
            last_report_us = elapsed_us;
        }

        if (elapsed_us >= timeout_us) {
            printf("ERROR: timed out waiting for FPGA done after %.3f seconds\n",
                   (double)elapsed_us / 1000000.0);
            print_fpga_debug_status("timeout");
            return -1;
        }
    }

    return 0;
}

int main(void)
{
    printf("Build ID: %s | %s %s\n", DEBUG_BUILD_TAG, __DATE__, __TIME__);

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
    pio_iterations    = (unsigned int *)(h2p_lw_virtual_base + PIO_ITERATIONS_OFFSET);
    pio_active_nodes  = (unsigned int *)(h2p_lw_virtual_base + PIO_ACTIVE_NODES_OFFSET);
    pio_node_mask     = (unsigned int *)(h2p_lw_virtual_base + PIO_NODE_MASK);
    pio_reset         = (unsigned int *)(h2p_lw_virtual_base + PIO_RESET_OFFSET);
    pio_clock_count   = (unsigned int *)(h2p_lw_virtual_base + PIO_CLK_COUNT_OFFSET);

    init_vga(fd);
    
    fpga_ram_virtual_base = mmap( NULL, FPGA_ONCHIP_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_ONCHIP_BASE); 
    if( fpga_ram_virtual_base == MAP_FAILED ) { printf( "ERROR: mmap3() failed...\n" ); close( fd ); return(1); }
    
    mcmc_system_base = (unsigned int *)fpga_ram_virtual_base;
    print_fpga_debug_status("after mmap before table load");

    // Load Dataset & Precompute (ARM)
    printf("Loading dataset and precomputing...\n");
    char samples_path[256];
    sprintf(samples_path, "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    int num_samples;
    int** dataset = load_csv(samples_path, &num_samples, NUM_NODES);
    precompute_fixed_k(dataset, num_samples);
    print_known_order_score_check();

    // Transfer Data to FPGA
    printf("Writing databases to FPGA Broadcast Memory...\n");

    for (int i = 0; i < NUM_NODES; i++) {
        for (int p = 0; p < num_candidates[i]; p++) {
            uint32_t mask = precomputed_db[i][p].parent_bitmask;
            int32_t q16_score = float_to_q16(precomputed_db[i][p].local_score);
            
            // Node is [11:7], Candidate is [6:1].
            int base_offset = (i << 7) | (p << 1); 
            
            *(mcmc_system_base + base_offset)     = (uint32_t)q16_score; 
            *(mcmc_system_base + base_offset + 1) = mask; 
        }
        
        // (Don't forget to update your sentinel write offsets the exact same way)
        int sentinel_offset = (i << 7) | (num_candidates[i] << 1); 
        *(mcmc_system_base + sentinel_offset)     = 0x00000000;
        *(mcmc_system_base + sentinel_offset + 1) = 0xFFFFFFFF;
    }
    
    // Write Sentinel at index 0 for all UNUSED hardware nodes
    // The hardware instantiates 32 nodes. We must satisfy all of them so &score_done == 1
    for (int i = NUM_NODES; i < 32; i++) {
        // Address logic for node i, candidate 0
        int sentinel_offset = (i << 7) | (0 << 1); 
        
        // Write the two 32-bit halves natively
        *(mcmc_system_base + sentinel_offset)     = 0x00000000; // local_score
        *(mcmc_system_base + sentinel_offset + 1) = 0xFFFFFFFF; // parent_mask
    }

    printf("Reset FPGA\n");
    *pio_start = 0;       // Ensure start is low
    usleep(10);
    print_fpga_debug_status("before reset assert");
    *pio_reset = 1;       // Assert soft reset
    usleep(10);           // Wait 10 microseconds (plenty of time for 50MHz clock)
    print_fpga_debug_status("during reset assert");
    *pio_reset = 0;       // De-assert soft reset
    usleep(10);
    print_fpga_debug_status("after reset deassert before config");

    *pio_iterations = ITERATIONS;
    *pio_active_nodes = NUM_NODES;
    *pio_node_mask = NUM_NODES - 1;
    *pio_seed = SEED;
    print_fpga_debug_status("after config before start");

    printf("Starting 32-Node Engine...\n");
    
    uint32_t clk_before_start = *pio_clock_count;
    uint32_t done_before_start = *pio_done;
    if (done_before_start != 0) {
        printf("WARNING: done was already high before start; readback may be stale unless reset clears it.\n");
    }

    *pio_start = 1;
    usleep(10);
    print_fpga_debug_status("after start assert");
    if (*pio_clock_count == clk_before_start && *pio_done == 0) {
        printf("DEBUG: clk_count has not advanced yet immediately after start; continuing to poll.\n");
    }

    if (wait_for_done_or_timeout(DONE_TIMEOUT_US) != 0) {
        *pio_start = 0;
        print_fpga_debug_status("after timeout start cleared");
        return 1;
    }
    print_fpga_debug_status("done observed before start clear");
    *pio_start = 0;            // Clean up
    usleep(10);
    print_fpga_debug_status("after start clear");

    // Performance Metrics
    uint32_t clocks = *pio_clock_count;
    float time_seconds = (float)clocks / 50000000.0f; // 50 MHz clock
    
    printf("\n=== MCMC Hardware Complete ===\n");
    printf("Hardware Iterations: %d\n", *pio_iterations);
    printf("Cycle Count:         %u clocks\n", clocks);
    printf("Execution Time:      %f seconds\n\n", time_seconds);
    printf("Raw HW best_score:   0x%08x (%.6f Q16.16)\n",
           *pio_best_score,
           q16_to_float(*pio_best_score));
    
    // Extract the Best Order directly from the Heavyweight Memory Bus
    
    int best_order[32];
    int active_count = (int)(*pio_active_nodes);
    uint32_t raw_order_word0 = *(mcmc_system_base + 2048);
    uint32_t raw_order_word1 = *(mcmc_system_base + 2049);
    printf("Raw best-order words: 0x%08x 0x%08x\n", raw_order_word0, raw_order_word1);
    for (int i = 0; i < active_count; i++) {
        // Read the 32-bit chunk containing this node (starts at address 2048)
        uint32_t word = *(mcmc_system_base + 2048 + (i / 4));
        
        // Shift by 0, 5, 10, or 15 bits and mask
        best_order[i] = (word >> ((i % 4) * 5)) & 0x1F;
        
        printf("%s ", node_names[best_order[i]]);
        if (i < active_count - 1) printf("-> ");
    }
    printf("\n");
    print_learned_graph(best_order, active_count);

    return 0;
}
