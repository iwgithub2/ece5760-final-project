/*
 * Separate demo build for software partitioning larger Bayesian networks.
 *
 * Hardware stays unchanged. This file reuses the normal solver support code,
 * enables partition mode, and adds a VGA drawing path for the merged graph.
 */
#define MCMC_PARTITION_DEMO_BUILD 1
#define main mcmc_test_main_disabled
#define run_partitioned_solver run_partitioned_solver_console_only
#include "mcmc_test.c"
#undef run_partitioned_solver
#undef main

static short partition_color(int partition_id)
{
    static const short colors[] = {
        VGA_GREEN, VGA_BLUE, VGA_YELL, VGA_RED, 0x1F, 0xE3, 0x9C, 0x7F
    };
    if (partition_id < 0) return VGA_WHITE;
    return colors[partition_id % (int)(sizeof(colors) / sizeof(colors[0]))];
}

static int count_partitioned_edges(const LearnedGlobalParentSet* learned)
{
    int edges = 0;
    for (int child = 0; child < NUM_NODES; child++) {
        if (learned[child].learned) edges += learned[child].parent_count;
    }
    return edges;
}

static void draw_partitioned_graph_vga(const LearnedGlobalParentSet* learned,
                                       const NodePartition* partitions,
                                       int partition_count,
                                       int skipped_cycles)
{
    int x[NUM_NODES];
    int y[NUM_NODES];
    int cols = 10;
    int rows = (NUM_NODES + cols - 1) / cols;
    int left = 34;
    int right = 604;
    int top = 82;
    int bottom = 408;
    char line[80];

    if (!vga_pixel_ptr || !vga_char_ptr) return;

    if (rows < 1) rows = 1;
    for (int node = 0; node < NUM_NODES; node++) {
        int col = node % cols;
        int row = node / cols;
        x[node] = (cols <= 1) ? 320 : left + (col * (right - left)) / (cols - 1);
        y[node] = (rows <= 1) ? 240 : top + (row * (bottom - top)) / (rows - 1);
    }

    VGA_box(0, 0, 639, 479, VGA_BLACK);
    VGA_text_clear();
    VGA_text(1, 1, "Hepar2 partition demo: merged learned DAG");
    snprintf(line, sizeof(line), "nodes=%d partitions=%d edges=%d skipped_cycles=%d",
             NUM_NODES, partition_count, count_partitioned_edges(learned), skipped_cycles);
    VGA_text(1, 2, line);
    VGA_text(1, 4, "Node color = partition that owned the core node. Label = node id.");

    for (int child = 0; child < NUM_NODES; child++) {
        if (!learned[child].learned) continue;
        for (int p = 0; p < learned[child].parent_count; p++) {
            int parent = learned[child].parents[p];
            if (parent < 0 || parent >= NUM_NODES) continue;
            VGA_line(x[parent], y[parent], x[child], y[child], VGA_WHITE);
        }
    }

    for (int node = 0; node < NUM_NODES; node++) {
        short color = partition_color(learned[node].partition_id);
        char label[4];
        VGA_disc(x[node], y[node], 7, color);
        VGA_disc(x[node], y[node], 3, VGA_BLACK);
        snprintf(label, sizeof(label), "%02d", node);
        VGA_text((x[node] / 8) - 1, y[node] / 8, label);
    }

    for (int p = 0; p < partition_count && p < 8; p++) {
        snprintf(line, sizeof(line), "P%d core=%d active=%d",
                 partitions[p].id, partitions[p].core_count, partitions[p].total_count);
        VGA_text(1, 51 + p, line);
        VGA_box(150, 408 + p * 8, 159, 414 + p * 8, partition_color(partitions[p].id));
    }
}

int run_partitioned_solver(const CandidateLoadConfig* config, int dry_run)
{
    int partition_count = 0;
    int skipped_cycles = 0;
    NodePartition* partitions;
    LearnedGlobalParentSet learned[NUM_NODES];

    validate_partition_config_or_die(config);
    partitions = build_node_partitions(config->partition_size, config->partition_overlap,
                                       &partition_count);
    print_partitions(partitions, partition_count);

    for (int node = 0; node < NUM_NODES; node++) {
        learned[node].learned = false;
        learned[node].parent_count = 0;
        learned[node].local_score = 0.0f;
        learned[node].partition_id = -1;
    }

    for (int p = 0; p < partition_count; p++) {
        ParentSet local_db[MCMC_HW_MAX_NODES][MCMC_HW_CANDIDATE_SLOTS_PER_NODE];
        int local_counts[MCMC_HW_MAX_NODES];
        int best_order[MCMC_HW_MAX_NODES];
        unsigned int local_parent_masks[MCMC_HW_MAX_NODES];
        uint32_t clocks = 0;
        float order_score;
        float graph_score;

        for (int i = 0; i < MCMC_HW_MAX_NODES; i++) {
            local_counts[i] = 0;
            local_parent_masks[i] = 0;
        }

        stage_partition_candidates(&partitions[p], local_db, local_counts);

        printf("\nPartition %d candidate counts:", partitions[p].id);
        for (int i = 0; i < partitions[p].total_count; i++) {
            printf(" %d:%d", i, local_counts[i]);
        }
        printf("\n");

        if (dry_run) continue;

        printf("Starting partition %d solver: active=%d core=%d\n",
               partitions[p].id, partitions[p].total_count, partitions[p].core_count);
        if (run_hw_solver_for_table(local_db, local_counts, partitions[p].total_count,
                                    SEED ^ (0x9E3779B9u * (unsigned int)(p + 1)),
                                    best_order, &clocks) != 0) {
            free(partitions);
            return 1;
        }

        order_score = score_order_logsum_from_table(best_order, partitions[p].total_count,
                                                    local_db, local_counts);
        graph_score = choose_best_graph_for_order_from_table(best_order, partitions[p].total_count,
                                                             local_db, local_counts,
                                                             local_parent_masks);
        skipped_cycles += merge_partition_graph(&partitions[p], local_parent_masks, learned);

        printf("Partition %d complete: clocks=%u order_score=%.6f graph_score=%.6f\n",
               partitions[p].id, clocks, order_score, graph_score);
    }

    if (dry_run) {
        printf("\nDry run complete: partitioned candidate tables fit current solver capacity.\n");
    } else {
        print_partitioned_learned_graph(learned, skipped_cycles);
        draw_partitioned_graph_vga(learned, partitions, partition_count, skipped_cycles);
    }

    free(partitions);
    return 0;
}

int main(int argc, char** argv)
{
    CandidateLoadConfig candidate_config;
    bool partitioned_solver;
    char samples_path[256];
    int num_samples;
    int** dataset;
    int fd;

    parse_candidate_args(argc, argv, &candidate_config);
    partitioned_solver = should_use_partitioned_solver(&candidate_config);
    if (!partitioned_solver) {
        printf("ERROR: mcmc_partition_demo is only for the partition/VGA demo.\n");
        printf("       Use programs/mcmc_test.c for normal solver data collection.\n");
        return 1;
    }
    validate_partition_config_or_die(&candidate_config);

    printf("Build ID: partition-vga-demo | %s %s\n", __DATE__, __TIME__);
    printf("Candidate source: %s%s%s\n",
           candidate_config.source == CANDIDATE_SOURCE_FIXED ? "fixed-k" : "ML table",
           candidate_config.source == CANDIDATE_SOURCE_ML ? " from " : "",
           candidate_config.source == CANDIDATE_SOURCE_ML ? candidate_config.ml_dir : "");
    printf("Solver mode: %s\n", partitioned_solver ? "software-partitioned VGA demo" : "single hardware run");

    printf("Loading dataset and candidate tables...\n");
    sprintf(samples_path, "cleaned-datasets/%s_samples.csv", DATASET_NAME);
    dataset = load_csv(samples_path, &num_samples, NUM_NODES);
    if (candidate_config.source == CANDIDATE_SOURCE_ML) {
        load_ml_candidate_table(candidate_config.ml_dir);
    } else {
        precompute_fixed_k(dataset, num_samples);
    }
    print_known_order_score_check();

    if (candidate_config.dry_run_candidates) {
        if (partitioned_solver) return run_partitioned_solver(&candidate_config, 1);
        printf("Dry run complete: candidate tables fit this C build and current RTL capacity.\n");
        return 0;
    }

    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERROR: could not open \"/dev/mem\"...\n");
        return 1;
    }

    h2p_lw_virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE),
                               MAP_SHARED, fd, HW_REGS_BASE);
    if (h2p_lw_virtual_base == MAP_FAILED) {
        printf("ERROR: mmap1() failed...\n");
        close(fd);
        return 1;
    }

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

    fpga_ram_virtual_base = mmap(NULL, FPGA_ONCHIP_SPAN, (PROT_READ | PROT_WRITE),
                                 MAP_SHARED, fd, FPGA_ONCHIP_BASE);
    if (fpga_ram_virtual_base == MAP_FAILED) {
        printf("ERROR: mmap3() failed...\n");
        close(fd);
        return 1;
    }
    mcmc_system_base = (unsigned int *)fpga_ram_virtual_base;
    print_fpga_debug_status("after mmap before table write");

    return run_partitioned_solver(&candidate_config, 0);
}
