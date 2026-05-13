// Standalone HPS demo: partition a >32-node ML candidate table into 32-node
// solver runs, merge the learned graph, and draw a compact VGA view.
// gcc -std=gnu99 -DDATASET_NAME=\"hepar2\" -DNUM_NODES=70 programs/mcmc_partition_demo.c -o mcmc_partition_demo -lm -pthread

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "address_map_arm_brl4.h"

#ifndef DATASET_NAME
#define DATASET_NAME "hepar2"
#endif
#ifndef NUM_NODES
#define NUM_NODES 70
#endif

#define PIO_START_OFFSET        0x00
#define PIO_SEED_OFFSET         0x10
#define PIO_DONE_OFFSET         0x20
#define PIO_BEST_SCORE_OFFSET   0x30
#define PIO_ITERATIONS_OFFSET   0x40
#define PIO_ACTIVE_NODES_OFFSET 0x50
#define PIO_NODE_MASK           0x60
#define PIO_RESET_OFFSET        0x70
#define PIO_CLK_COUNT_OFFSET    0x80

#define HW_MAX_NODES 32
#define HW_SLOTS_PER_NODE 64
#define HW_USABLE_CANDIDATES 63
#define ITERATIONS 1000000
#define SEED 0xDEADBEEF
#define DONE_TIMEOUT_US 10000000
#define DEFAULT_ML_TABLE_DIR "ml_hepar2"
#define DEFAULT_PARTITION_OVERLAP 8
#define VGA_BLACK 0x00
#define VGA_WHITE 0xFF
#define VGA_BLUE  0x03
#define VGA_GREEN 0x1C
#define VGA_RED   0xE0
#define VGA_YELL  0xFC

typedef struct {
    unsigned int parent_mask;
    float local_score;
    int parent_count;
    int parents[HW_MAX_NODES];
} ParentSet;

typedef struct {
    char ml_dir[256];
    bool dry_run;
    int partition_size;
    int partition_overlap;
} DemoConfig;

typedef struct {
    int id;
    int core_count;
    int total_count;
    int global_nodes[HW_MAX_NODES];
    int local_index[NUM_NODES];
} NodePartition;

typedef struct {
    bool learned;
    int parent_count;
    int parents[HW_MAX_NODES];
    int partition_id;
} LearnedSet;

static char node_names[NUM_NODES][64];
static int node_cardinalities[NUM_NODES];
static ParentSet db[NUM_NODES][HW_SLOTS_PER_NODE];
static int num_candidates[NUM_NODES];

static void *h2p_lw_virtual_base;
static volatile unsigned int *pio_start, *pio_seed, *pio_done, *pio_best_score;
static volatile unsigned int *pio_iterations, *pio_active_nodes, *pio_node_mask;
static volatile unsigned int *pio_reset, *pio_clock_count;
static volatile unsigned int *mcmc_base;
static void *fpga_ram_virtual_base;
static volatile unsigned int *vga_pixel_ptr, *vga_char_ptr;
static void *vga_pixel_virtual_base, *vga_char_virtual_base;

static void strip_newline(char* s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static int split_csv_simple(char* line, char** fields, int max_fields)
{
    int count = 0;
    char* p = line;
    if (max_fields <= 0) return 0;
    fields[count++] = p;
    while (*p) {
        if (*p == ',') {
            *p = 0;
            if (count < max_fields) fields[count++] = p + 1;
        }
        p++;
    }
    return count;
}

static void parse_args(int argc, char** argv, DemoConfig* cfg)
{
    strcpy(cfg->ml_dir, DEFAULT_ML_TABLE_DIR);
    cfg->dry_run = false;
    cfg->partition_size = HW_MAX_NODES;
    cfg->partition_overlap = DEFAULT_PARTITION_OVERLAP;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "ml") || !strcmp(argv[i], "--ml") || !strcmp(argv[i], "--partition")) {
            continue;
        } else if (!strcmp(argv[i], "--ml-dir") && i + 1 < argc) {
            strncpy(cfg->ml_dir, argv[++i], sizeof(cfg->ml_dir) - 1);
            cfg->ml_dir[sizeof(cfg->ml_dir) - 1] = 0;
        } else if (!strcmp(argv[i], "--dry-run-candidates") || !strcmp(argv[i], "--validate-candidates")) {
            cfg->dry_run = true;
        } else if (!strcmp(argv[i], "--partition-size") && i + 1 < argc) {
            cfg->partition_size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--partition-overlap") && i + 1 < argc) {
            cfg->partition_overlap = atoi(argv[++i]);
        } else {
            printf("Usage: %s ml --ml-dir DIR --partition [--dry-run-candidates]\n", argv[0]);
            exit(1);
        }
    }
    if (cfg->partition_size <= 0 || cfg->partition_size > HW_MAX_NODES ||
        cfg->partition_overlap < 0 || cfg->partition_overlap >= cfg->partition_size) {
        printf("ERROR: invalid partition size/overlap\n");
        exit(1);
    }
}

static void load_csv_names_and_cardinalities(const char* path)
{
    FILE* f = fopen(path, "r");
    char line[8192];
    if (!f) { printf("ERROR: failed to open %s\n", path); exit(1); }
    if (!fgets(line, sizeof(line), f)) { printf("ERROR: empty CSV\n"); exit(1); }
    strip_newline(line);
    char* tok = strtok(line, ",");
    int col = 0;
    while (tok) {
        if (col < NUM_NODES) {
            strncpy(node_names[col], tok, 63);
            node_names[col][63] = 0;
        }
        col++;
        tok = strtok(NULL, ",");
    }
    if (col != NUM_NODES) {
        printf("ERROR: CSV has %d columns, compiled NUM_NODES=%d\n", col, NUM_NODES);
        exit(1);
    }
    for (int i = 0; i < NUM_NODES; i++) node_cardinalities[i] = 0;
    while (fgets(line, sizeof(line), f)) {
        tok = strtok(line, ",\n\r");
        for (col = 0; tok && col < NUM_NODES; col++) {
            int v = atoi(tok);
            if (v + 1 > node_cardinalities[col]) node_cardinalities[col] = v + 1;
            tok = strtok(NULL, ",\n\r");
        }
    }
    fclose(f);
    printf("Loaded %s with %d nodes\n", path, NUM_NODES);
}

static int parse_parent_indices(const char* raw, int expected, int node, int line_no, int* out)
{
    char buf[1024];
    int count = 0;
    strncpy(buf, raw ? raw : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char* tok = strtok(buf, " \t");
    while (tok) {
        long parent = strtol(tok, NULL, 10);
        if (parent < 0 || parent >= NUM_NODES || parent == node || count >= HW_MAX_NODES) {
            printf("ERROR: bad parent index on ML line %d\n", line_no);
            exit(1);
        }
        out[count++] = (int)parent;
        tok = strtok(NULL, " \t");
    }
    if (count != expected) {
        printf("ERROR: ML line %d parent_count=%d but parsed %d\n", line_no, expected, count);
        exit(1);
    }
    return count;
}

static void add_candidate(int node, int parent_count, const int* parents, float score)
{
    int idx = num_candidates[node];
    if (idx >= HW_USABLE_CANDIDATES) {
        printf("ERROR: node %d has more than %d candidates\n", node, HW_USABLE_CANDIDATES);
        exit(1);
    }
    db[node][idx].parent_mask = 0;
    db[node][idx].local_score = score;
    db[node][idx].parent_count = parent_count;
    for (int i = 0; i < parent_count; i++) db[node][idx].parents[i] = parents[i];
    num_candidates[node]++;
}

static void load_ml_table(const char* dir)
{
    char path[512], line[4096];
    snprintf(path, sizeof(path), "%s/readable_debug.csv", dir);
    FILE* f = fopen(path, "r");
    int line_no = 0;
    if (!f) { printf("ERROR: failed to open %s\n", path); exit(1); }
    for (int i = 0; i < NUM_NODES; i++) num_candidates[i] = 0;
    if (!fgets(line, sizeof(line), f)) { printf("ERROR: empty ML table\n"); exit(1); }
    line_no++;
    while (fgets(line, sizeof(line), f)) {
        char* fields[16];
        int parents[HW_MAX_NODES];
        line_no++;
        strip_newline(line);
        if (split_csv_simple(line, fields, 16) < 8) {
            printf("ERROR: malformed ML line %d\n", line_no);
            exit(1);
        }
        int node = atoi(fields[0]);
        if (node < 0 || node >= NUM_NODES || strcmp(fields[1], node_names[node])) {
            printf("ERROR: ML line %d node mismatch\n", line_no);
            exit(1);
        }
        int parent_count = parse_parent_indices(fields[4], atoi(fields[3]), node, line_no, parents);
        add_candidate(node, parent_count, parents, strtof(fields[7], NULL));
    }
    fclose(f);
    for (int node = 0; node < NUM_NODES; node++) {
        float max_score = -1e30f;
        if (num_candidates[node] <= 0) { printf("ERROR: node %d has no candidates\n", node); exit(1); }
        for (int c = 0; c < num_candidates[node]; c++)
            if (db[node][c].local_score > max_score) max_score = db[node][c].local_score;
        for (int c = 0; c < num_candidates[node]; c++) {
            db[node][c].local_score -= max_score;
            if (db[node][c].local_score < -500.0f) db[node][c].local_score = -500.0f;
        }
    }
    printf("Loaded ML candidate table from %s\n", path);
}

static int contains_node(const NodePartition* part, int node)
{
    return node >= 0 && node < NUM_NODES && part->local_index[node] >= 0;
}

static void init_partition(NodePartition* part, int id)
{
    part->id = id;
    part->core_count = 0;
    part->total_count = 0;
    for (int i = 0; i < NUM_NODES; i++) part->local_index[i] = -1;
}

static void add_partition_node(NodePartition* part, int node, bool core)
{
    if (contains_node(part, node)) return;
    if (part->total_count >= HW_MAX_NODES) { printf("ERROR: partition overflow\n"); exit(1); }
    part->local_index[node] = part->total_count;
    part->global_nodes[part->total_count++] = node;
    if (core) part->core_count++;
}

static int connection_score(const int* weights, const NodePartition* part, int node)
{
    int score = 0;
    for (int i = 0; i < part->core_count; i++) score += weights[node * NUM_NODES + part->global_nodes[i]];
    return score;
}

static int choose_seed(const bool* assigned, const int* degree)
{
    int best = -1, best_degree = -1;
    for (int node = 0; node < NUM_NODES; node++)
        if (!assigned[node] && degree[node] > best_degree) { best = node; best_degree = degree[node]; }
    return best;
}

static int choose_next(const bool* assigned, const int* weights, const int* degree, const NodePartition* part)
{
    int best = -1, best_score = -1, best_degree = -1;
    for (int node = 0; node < NUM_NODES; node++) {
        int score;
        if (assigned[node]) continue;
        score = connection_score(weights, part, node);
        if (score > best_score || (score == best_score && degree[node] > best_degree)) {
            best = node; best_score = score; best_degree = degree[node];
        }
    }
    return best;
}

static int choose_context(const NodePartition* part, const int* weights, const int* degree)
{
    int best = -1, best_score = 0, best_degree = -1;
    for (int node = 0; node < NUM_NODES; node++) {
        int score;
        if (contains_node(part, node)) continue;
        score = connection_score(weights, part, node);
        if (score > best_score || (score == best_score && degree[node] > best_degree)) {
            best = node; best_score = score; best_degree = degree[node];
        }
    }
    return best;
}

static NodePartition* build_partitions(const DemoConfig* cfg, int* out_count)
{
    int* weights = (int*)calloc((size_t)NUM_NODES * NUM_NODES, sizeof(int));
    int* degree = (int*)calloc(NUM_NODES, sizeof(int));
    bool* assigned = (bool*)calloc(NUM_NODES, sizeof(bool));
    NodePartition* parts = (NodePartition*)calloc(NUM_NODES, sizeof(NodePartition));
    int assigned_count = 0, part_count = 0, core_limit = cfg->partition_size - cfg->partition_overlap;
    if (!weights || !degree || !assigned || !parts) { printf("ERROR: partition alloc failed\n"); exit(1); }
    if (core_limit < 1) core_limit = 1;
    for (int child = 0; child < NUM_NODES; child++) {
        for (int c = 0; c < num_candidates[child]; c++) {
            for (int p = 0; p < db[child][c].parent_count; p++) {
                int parent = db[child][c].parents[p];
                weights[child * NUM_NODES + parent]++;
                weights[parent * NUM_NODES + child]++;
            }
        }
    }
    for (int n = 0; n < NUM_NODES; n++)
        for (int o = 0; o < NUM_NODES; o++) degree[n] += weights[n * NUM_NODES + o];
    while (assigned_count < NUM_NODES) {
        NodePartition* part = &parts[part_count];
        int seed;
        init_partition(part, part_count);
        seed = choose_seed(assigned, degree);
        if (seed < 0) break;
        add_partition_node(part, seed, true);
        assigned[seed] = true; assigned_count++;
        while (part->core_count < core_limit && assigned_count < NUM_NODES) {
            int next = choose_next(assigned, weights, degree, part);
            if (next < 0) break;
            add_partition_node(part, next, true);
            assigned[next] = true; assigned_count++;
        }
        while (part->total_count < cfg->partition_size) {
            int context = choose_context(part, weights, degree);
            if (context < 0) break;
            add_partition_node(part, context, false);
        }
        part_count++;
    }
    free(weights); free(degree); free(assigned);
    *out_count = part_count;
    return parts;
}

static void stage_partition(const NodePartition* part, ParentSet local_db[HW_MAX_NODES][HW_SLOTS_PER_NODE],
                            int* local_counts)
{
    for (int ln = 0; ln < part->total_count; ln++) {
        int gn = part->global_nodes[ln], out = 0;
        for (int c = 0; c < num_candidates[gn]; c++) {
            unsigned int mask = 0;
            bool ok = true;
            for (int p = 0; p < db[gn][c].parent_count; p++) {
                int lp = part->local_index[db[gn][c].parents[p]];
                if (lp < 0) { ok = false; break; }
                mask |= (1U << lp);
            }
            if (!ok) continue;
            if (out >= HW_USABLE_CANDIDATES) { printf("ERROR: local candidate overflow\n"); exit(1); }
            local_db[ln][out] = db[gn][c];
            local_db[ln][out].parent_mask = mask;
            out++;
        }
        if (out <= 0) { printf("ERROR: no local candidates for node %d\n", gn); exit(1); }
        local_counts[ln] = out;
    }
}

static int32_t float_to_q16(float val)
{
    float scaled = val * 65536.0f;
    if (scaled > 2147483647.0f) return 2147483647;
    if (scaled < -2147483648.0f) return (int32_t)0x80000000;
    return (int32_t)scaled;
}

static void write_hw_table(ParentSet table[HW_MAX_NODES][HW_SLOTS_PER_NODE], const int* counts, int active)
{
    for (int n = 0; n < active; n++) {
        for (int c = 0; c < counts[n]; c++) {
            int off = (n << 7) | (c << 1);
            *(mcmc_base + off) = (uint32_t)float_to_q16(table[n][c].local_score);
            *(mcmc_base + off + 1) = table[n][c].parent_mask;
        }
        int sentinel = (n << 7) | (counts[n] << 1);
        *(mcmc_base + sentinel) = 0;
        *(mcmc_base + sentinel + 1) = 0xFFFFFFFF;
    }
    for (int n = active; n < HW_MAX_NODES; n++) {
        int sentinel = n << 7;
        *(mcmc_base + sentinel) = 0;
        *(mcmc_base + sentinel + 1) = 0xFFFFFFFF;
    }
}

static int wait_done(unsigned int timeout_us)
{
    struct timeval start, now;
    gettimeofday(&start, NULL);
    while (*pio_done == 0) {
        unsigned int elapsed;
        usleep(1000);
        gettimeofday(&now, NULL);
        elapsed = (unsigned int)((now.tv_sec - start.tv_sec) * 1000000u + (now.tv_usec - start.tv_usec));
        if (elapsed >= timeout_us) { printf("ERROR: FPGA timeout\n"); return -1; }
    }
    return 0;
}

static int run_hw(ParentSet table[HW_MAX_NODES][HW_SLOTS_PER_NODE], const int* counts,
                  int active, unsigned int seed, int* best_order)
{
    write_hw_table(table, counts, active);
    *pio_start = 0; *pio_reset = 1; usleep(10); *pio_reset = 0; usleep(10);
    *pio_iterations = ITERATIONS;
    *pio_active_nodes = (unsigned int)active;
    *pio_node_mask = (unsigned int)(active - 1);
    *pio_seed = seed;
    *pio_start = 1; usleep(10);
    if (wait_done(DONE_TIMEOUT_US) != 0) { *pio_start = 0; return 1; }
    *pio_start = 0;
    for (int i = 0; i < active; i++) {
        uint32_t word = *(mcmc_base + 2048 + (i / 4));
        best_order[i] = (word >> ((i % 4) * 5)) & 0x1F;
    }
    return 0;
}

static unsigned int allowed_mask_for_order(const int* order, int pos)
{
    unsigned int mask = 0;
    for (int i = 0; i < pos; i++) mask |= (1U << order[i]);
    return mask;
}

static void choose_best_graph(ParentSet table[HW_MAX_NODES][HW_SLOTS_PER_NODE], const int* counts,
                              const int* order, int active, unsigned int* parent_masks)
{
    for (int i = 0; i < HW_MAX_NODES; i++) parent_masks[i] = 0;
    for (int pos = 0; pos < active; pos++) {
        int node = order[pos];
        unsigned int allowed = allowed_mask_for_order(order, pos), best_mask = 0;
        float best = -1e30f;
        for (int c = 0; c < counts[node]; c++) {
            unsigned int mask = table[node][c].parent_mask;
            if ((mask & allowed) == mask && table[node][c].local_score > best) {
                best = table[node][c].local_score; best_mask = mask;
            }
        }
        parent_masks[node] = best_mask;
    }
}

static bool parent_contains(const LearnedSet* learned, int child, int parent)
{
    for (int i = 0; i < learned[child].parent_count; i++) if (learned[child].parents[i] == parent) return true;
    return false;
}

static bool has_path(const LearnedSet* learned, int current, int target, bool* visited)
{
    if (current == target) return true;
    if (visited[current]) return false;
    visited[current] = true;
    for (int child = 0; child < NUM_NODES; child++)
        if (learned[child].learned && parent_contains(learned, child, current) &&
            has_path(learned, child, target, visited)) return true;
    return false;
}

static bool creates_cycle(const LearnedSet* learned, int parent, int child)
{
    bool visited[NUM_NODES];
    for (int i = 0; i < NUM_NODES; i++) visited[i] = false;
    return has_path(learned, child, parent, visited);
}

static int merge_graph(const NodePartition* part, const unsigned int* local_masks, LearnedSet* learned)
{
    int skipped = 0;
    for (int lc = 0; lc < part->core_count; lc++) {
        int gc = part->global_nodes[lc];
        learned[gc].learned = true;
        learned[gc].parent_count = 0;
        learned[gc].partition_id = part->id;
        for (int lp = 0; lp < part->total_count; lp++) {
            if ((local_masks[lc] & (1U << lp)) == 0) continue;
            int gp = part->global_nodes[lp];
            if (creates_cycle(learned, gp, gc)) { skipped++; continue; }
            learned[gc].parents[learned[gc].parent_count++] = gp;
        }
    }
    return skipped;
}

static void VGA_text(int x, int y, const char* s)
{
    volatile char* cb = (char*)vga_char_ptr;
    int off = (y << 7) + x;
    if (!vga_char_ptr) return;
    while (*s) cb[off++] = *s++;
}

static void VGA_text_clear(void)
{
    volatile char* cb = (char*)vga_char_ptr;
    if (!vga_char_ptr) return;
    for (int y = 0; y < 60; y++) for (int x = 0; x < 80; x++) cb[(y << 7) + x] = ' ';
}

#define CLAMP(v, lo, hi) do { if ((v) < (lo)) (v) = (lo); if ((v) > (hi)) (v) = (hi); } while (0)

static void VGA_box(int x1, int y1, int x2, int y2, short color)
{
    if (!vga_pixel_ptr) return;
    CLAMP(x1, 0, 639); CLAMP(x2, 0, 639); CLAMP(y1, 0, 479); CLAMP(y2, 0, 479);
    for (int y = y1; y <= y2; y++) for (int x = x1; x <= x2; x++) *((char*)vga_pixel_ptr + (y << 10) + x) = color;
}

static void VGA_disc(int x, int y, int r, short color)
{
    if (!vga_pixel_ptr) return;
    int rr = r * r;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dy * dy <= rr) {
            int px = x + dx, py = y + dy;
            CLAMP(px, 0, 639); CLAMP(py, 0, 479);
            *((char*)vga_pixel_ptr + (py << 10) + px) = color;
        }
}

static void VGA_line(int x1, int y1, int x2, int y2, short color)
{
    if (!vga_pixel_ptr) return;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        *((char*)vga_pixel_ptr + (y1 << 10) + x1) = color;
        if (x1 == x2 && y1 == y2) break;
        int e2 = err << 1;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static int init_vga(int fd)
{
    vga_char_virtual_base = mmap(NULL, FPGA_CHAR_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_CHAR_BASE);
    if (vga_char_virtual_base == MAP_FAILED) { printf("WARNING: VGA char mmap failed\n"); return -1; }
    vga_char_ptr = (unsigned int*)vga_char_virtual_base;
    vga_pixel_virtual_base = mmap(NULL, FPGA_ONCHIP_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SDRAM_BASE);
    if (vga_pixel_virtual_base == MAP_FAILED) { printf("WARNING: VGA pixel mmap failed\n"); return -1; }
    vga_pixel_ptr = (unsigned int*)vga_pixel_virtual_base;
    return 0;
}

static short part_color(int id)
{
    static const short colors[] = { VGA_GREEN, VGA_BLUE, VGA_YELL, VGA_RED, 0x1F, 0xE3, 0x9C, 0x7F };
    return id < 0 ? VGA_WHITE : colors[id % 8];
}

static int edge_count(const LearnedSet* learned)
{
    int n = 0;
    for (int child = 0; child < NUM_NODES; child++) if (learned[child].learned) n += learned[child].parent_count;
    return n;
}

static void draw_vga(const LearnedSet* learned, const NodePartition* parts, int part_count, int skipped)
{
    int x[NUM_NODES], y[NUM_NODES], cols = 10, rows = (NUM_NODES + 9) / 10;
    char line[80], label[4];
    if (!vga_pixel_ptr || !vga_char_ptr) return;
    for (int node = 0; node < NUM_NODES; node++) {
        int col = node % cols, row = node / cols;
        x[node] = 34 + (col * (604 - 34)) / (cols - 1);
        y[node] = 82 + (row * (408 - 82)) / (rows - 1);
    }
    VGA_box(0, 0, 639, 479, VGA_BLACK);
    VGA_text_clear();
    VGA_text(1, 1, "Hepar2 partition demo: merged learned DAG");
    snprintf(line, sizeof(line), "nodes=%d partitions=%d edges=%d skipped_cycles=%d",
             NUM_NODES, part_count, edge_count(learned), skipped);
    VGA_text(1, 2, line);
    VGA_text(1, 4, "Node color = owning partition. Label = node id.");
    for (int child = 0; child < NUM_NODES; child++) {
        if (!learned[child].learned) continue;
        for (int p = 0; p < learned[child].parent_count; p++) {
            int parent = learned[child].parents[p];
            VGA_line(x[parent], y[parent], x[child], y[child], VGA_WHITE);
        }
    }
    for (int node = 0; node < NUM_NODES; node++) {
        VGA_disc(x[node], y[node], 7, part_color(learned[node].partition_id));
        VGA_disc(x[node], y[node], 3, VGA_BLACK);
        snprintf(label, sizeof(label), "%02d", node);
        VGA_text((x[node] / 8) - 1, y[node] / 8, label);
    }
    for (int p = 0; p < part_count && p < 8; p++) {
        snprintf(line, sizeof(line), "P%d core=%d active=%d", parts[p].id, parts[p].core_count, parts[p].total_count);
        VGA_text(1, 51 + p, line);
        VGA_box(150, 408 + p * 8, 159, 414 + p * 8, part_color(parts[p].id));
    }
}

static void print_partitions(const NodePartition* parts, int count)
{
    printf("\n=== Software Partitions ===\n");
    printf("NUM_NODES=%d hardware_active_limit=%d partitions=%d\n", NUM_NODES, HW_MAX_NODES, count);
    for (int p = 0; p < count; p++) printf("Partition %d: core=%d active=%d\n", parts[p].id, parts[p].core_count, parts[p].total_count);
}

static void print_graph(const LearnedSet* learned, int skipped)
{
    printf("\n=== Partitioned Learned DAG ===\nSkipped cycle-closing merged edges: %d\n", skipped);
    for (int child = 0; child < NUM_NODES; child++) {
        if (!learned[child].learned) continue;
        for (int p = 0; p < learned[child].parent_count; p++)
            printf("  %s -> %s\n", node_names[learned[child].parents[p]], node_names[child]);
    }
}

static int run_partitions(const DemoConfig* cfg, bool dry_run)
{
    int part_count = 0, skipped = 0;
    NodePartition* parts = build_partitions(cfg, &part_count);
    LearnedSet learned[NUM_NODES];
    static ParentSet local_db[HW_MAX_NODES][HW_SLOTS_PER_NODE];
    int local_counts[HW_MAX_NODES], best_order[HW_MAX_NODES];
    unsigned int local_masks[HW_MAX_NODES];
    print_partitions(parts, part_count);
    for (int n = 0; n < NUM_NODES; n++) { learned[n].learned = false; learned[n].parent_count = 0; learned[n].partition_id = -1; }
    for (int p = 0; p < part_count; p++) {
        for (int i = 0; i < HW_MAX_NODES; i++) { local_counts[i] = 0; local_masks[i] = 0; }
        stage_partition(&parts[p], local_db, local_counts);
        printf("Partition %d candidate counts:", parts[p].id);
        for (int i = 0; i < parts[p].total_count; i++) printf(" %d:%d", i, local_counts[i]);
        printf("\n");
        if (dry_run) continue;
        if (run_hw(local_db, local_counts, parts[p].total_count, SEED ^ (0x9E3779B9u * (unsigned int)(p + 1)), best_order)) {
            free(parts); return 1;
        }
        choose_best_graph(local_db, local_counts, best_order, parts[p].total_count, local_masks);
        skipped += merge_graph(&parts[p], local_masks, learned);
    }
    if (dry_run) printf("\nDry run complete: partitioned candidate tables fit current solver capacity.\n");
    else { print_graph(learned, skipped); draw_vga(learned, parts, part_count, skipped); }
    free(parts);
    return 0;
}

static int init_hw_and_vga(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) { printf("ERROR: could not open /dev/mem\n"); return 1; }
    h2p_lw_virtual_base = mmap(NULL, HW_REGS_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, HW_REGS_BASE);
    if (h2p_lw_virtual_base == MAP_FAILED) { printf("ERROR: mmap regs failed\n"); close(fd); return 1; }
    pio_start = (unsigned int*)(h2p_lw_virtual_base + PIO_START_OFFSET);
    pio_seed = (unsigned int*)(h2p_lw_virtual_base + PIO_SEED_OFFSET);
    pio_done = (unsigned int*)(h2p_lw_virtual_base + PIO_DONE_OFFSET);
    pio_best_score = (unsigned int*)(h2p_lw_virtual_base + PIO_BEST_SCORE_OFFSET);
    pio_iterations = (unsigned int*)(h2p_lw_virtual_base + PIO_ITERATIONS_OFFSET);
    pio_active_nodes = (unsigned int*)(h2p_lw_virtual_base + PIO_ACTIVE_NODES_OFFSET);
    pio_node_mask = (unsigned int*)(h2p_lw_virtual_base + PIO_NODE_MASK);
    pio_reset = (unsigned int*)(h2p_lw_virtual_base + PIO_RESET_OFFSET);
    pio_clock_count = (unsigned int*)(h2p_lw_virtual_base + PIO_CLK_COUNT_OFFSET);
    init_vga(fd);
    fpga_ram_virtual_base = mmap(NULL, FPGA_ONCHIP_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_ONCHIP_BASE);
    if (fpga_ram_virtual_base == MAP_FAILED) { printf("ERROR: mmap RAM failed\n"); close(fd); return 1; }
    mcmc_base = (unsigned int*)fpga_ram_virtual_base;
    return 0;
}

int main(int argc, char** argv)
{
    DemoConfig cfg;
    char samples_path[256];
    parse_args(argc, argv, &cfg);
    printf("Build ID: standalone partition VGA demo | %s %s\n", __DATE__, __TIME__);
    printf("Solver mode: software-partitioned VGA demo\n");
    snprintf(samples_path, sizeof(samples_path), "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    load_csv_names_and_cardinalities(samples_path);
    load_ml_table(cfg.ml_dir);
    if (cfg.dry_run) return run_partitions(&cfg, true);
    if (init_hw_and_vga()) return 1;
    return run_partitions(&cfg, false);
}
