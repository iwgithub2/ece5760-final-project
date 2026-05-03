#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mcmc_core.h"

#define MAX_PATH_LEN 256

typedef enum {
    SCORE_BACKEND_FLOAT = 0,
    SCORE_BACKEND_FIXED = 1
} ScoreBackendKind;

typedef struct {
    ScoreBackendKind kind;
    FxLogAdd fx;
    FILE* x_log;
    long x_log_limit;
    long x_log_count;
} ScoreBackend;

typedef struct {
    char samples_path[MAX_PATH_LEN];
    char edges_path[MAX_PATH_LEN];
    char score_table_path[MAX_PATH_LEN];
    char x_log_path[MAX_PATH_LEN];
    char trace_path[MAX_PATH_LEN];
    int iterations;
    unsigned int seed;
    bool quiet;
    bool compare;
    ScoreBackendKind backend_kind;
    FxLutMode fx_mode;
    FxLutMode compare_mode;
    int fx_total_bits;
    int fx_int_bits;
    int lut_total_bits;
    int lut_int_bits;
    int lut_entries;
    double lut_min;
    double lut_max;
    long x_log_limit;
} AppConfig;

typedef struct {
    int accepted;
    double final_score;
    double elapsed_sec;
} RunStats;

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

static double log_add_reference(double score_cur, double ls_next) {
    if (!isfinite(score_cur)) {
        return ls_next;
    }
    if (!isfinite(ls_next)) {
        return score_cur;
    }
    return score_cur + fx_log1pexp_reference(ls_next - score_cur);
}

static void log_x_value(ScoreBackend* backend, double x) {
    if (!backend->x_log) {
        return;
    }
    if (backend->x_log_limit > 0 && backend->x_log_count >= backend->x_log_limit) {
        return;
    }
    fprintf(backend->x_log, "%.9f\n", x);
    backend->x_log_count++;
}

static int init_backend(ScoreBackend* backend, const AppConfig* cfg, ScoreBackendKind kind, FxLutMode fx_mode) {
    memset(backend, 0, sizeof(*backend));
    backend->kind = kind;
    backend->x_log_limit = cfg->x_log_limit;

    if (kind == SCORE_BACKEND_FIXED &&
        fx_logadd_init(&backend->fx, cfg->fx_total_bits, cfg->fx_int_bits,
                       cfg->lut_total_bits, cfg->lut_int_bits, cfg->lut_min,
                       cfg->lut_max, cfg->lut_entries, fx_mode) != 0) {
        return -1;
    }

    if (cfg->x_log_path[0] != '\0') {
        backend->x_log = fopen(cfg->x_log_path, "w");
        if (!backend->x_log) {
            return -1;
        }
    }
    return 0;
}

static void close_backend(ScoreBackend* backend) {
    if (backend->x_log) {
        fclose(backend->x_log);
        backend->x_log = NULL;
    }
    if (backend->kind == SCORE_BACKEND_FIXED) {
        fx_logadd_free(&backend->fx);
    }
}

static void prepare_fixed_scores(ScoreBackend* backend) {
    if (backend->kind != SCORE_BACKEND_FIXED) {
        return;
    }
    for (int i = 0; i < NUM_NODES; i++) {
        for (int p = 0; p < num_candidates[i]; p++) {
            precomputed_db[i][p].local_score_fx = fx_from_double(
                &backend->fx.value_fmt, precomputed_db[i][p].local_score, &backend->fx.stats);
        }
    }
}

static double score_order_backend(const int* order, ScoreBackend* backend) {
    if (backend->kind == SCORE_BACKEND_FLOAT) {
        double total_score = 0.0;
        for (int i = 0; i < NUM_NODES; i++) {
            int current_node = order[i];
            double node_score = -INFINITY;
            for (int p = 0; p < num_candidates[current_node]; p++) {
                if (check_compatibility(precomputed_db[current_node][p].parent_bitmask, order, i)) {
                    double ls = precomputed_db[current_node][p].local_score;
                    if (isfinite(node_score)) {
                        log_x_value(backend, ls - node_score);
                    }
                    node_score = log_add_reference(node_score, ls);
                }
            }
            total_score += node_score;
        }
        return total_score;
    }

    fx_raw_t total_fx = fx_from_double(&backend->fx.value_fmt, 0.0, &backend->fx.stats);
    for (int i = 0; i < NUM_NODES; i++) {
        int current_node = order[i];
        bool have_score = false;
        fx_raw_t node_fx = 0;
        for (int p = 0; p < num_candidates[current_node]; p++) {
            if (check_compatibility(precomputed_db[current_node][p].parent_bitmask, order, i)) {
                fx_raw_t ls_fx = precomputed_db[current_node][p].local_score_fx;
                if (!have_score) {
                    node_fx = ls_fx;
                    have_score = true;
                } else {
                    fx_raw_t x_fx = fx_sub_raw(&backend->fx.value_fmt, ls_fx, node_fx, &backend->fx.stats);
                    log_x_value(backend, fx_to_double(&backend->fx.value_fmt, x_fx));
                    fx_raw_t nonlinear = fx_log1pexp_approx_raw(&backend->fx, x_fx);
                    node_fx = fx_add_raw(&backend->fx.value_fmt, node_fx, nonlinear, &backend->fx.stats);
                }
            }
        }
        total_fx = fx_add_raw(&backend->fx.value_fmt, total_fx, node_fx, &backend->fx.stats);
    }
    return fx_to_double(&backend->fx.value_fmt, total_fx);
}

static void init_order(int* order) {
    for (int i = 0; i < NUM_NODES; i++) {
        order[i] = i;
    }
    for (int i = NUM_NODES - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = order[i];
        order[i] = order[j];
        order[j] = temp;
    }
}

static void copy_order(int* dst, const int* src) {
    for (int i = 0; i < NUM_NODES; i++) {
        dst[i] = src[i];
    }
}

static void swap_order_indices(int* order, int idx1, int idx2) {
    int temp = order[idx1];
    order[idx1] = order[idx2];
    order[idx2] = temp;
}

static bool accept_move(double proposed_score, double current_score, double u) {
    double diff = proposed_score - current_score;
    return diff >= 0.0 || u < exp(diff);
}

static RunStats run_mcmc_single(const AppConfig* cfg, ScoreBackend* backend, int* final_order, FILE* trace) {
    RunStats stats = {0};
    struct timespec start, end;
    int current_order[NUM_NODES];
    init_order(current_order);
    double current_score = score_order_backend(current_order, backend);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int step = 0; step < cfg->iterations; step++) {
        int proposed_order[NUM_NODES];
        copy_order(proposed_order, current_order);
        swap_order_indices(proposed_order, rand() % NUM_NODES, rand() % NUM_NODES);
        double proposed_score = score_order_backend(proposed_order, backend);
        bool accepted = accept_move(proposed_score, current_score, (double)rand() / (double)RAND_MAX);
        if (accepted) {
            copy_order(current_order, proposed_order);
            current_score = proposed_score;
            stats.accepted++;
        }
        if (trace) {
            fprintf(trace, "%d,%.9f,%d\n", step, current_score, accepted ? 1 : 0);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    copy_order(final_order, current_order);
    stats.final_score = current_score;
    stats.elapsed_sec = elapsed_seconds(start, end);
    return stats;
}

static void run_mcmc_compare(const AppConfig* cfg) {
    ScoreBackend float_backend;
    ScoreBackend approx_backend;
    AppConfig no_xlog = *cfg;
    no_xlog.x_log_path[0] = '\0';

    if (init_backend(&float_backend, &no_xlog, SCORE_BACKEND_FLOAT, FX_LUT_CLAMP) != 0 ||
        init_backend(&approx_backend, &no_xlog, SCORE_BACKEND_FIXED, cfg->compare_mode) != 0) {
        fprintf(stderr, "ERROR: could not initialize compare backends\n");
        exit(1);
    }
    prepare_fixed_scores(&approx_backend);

    FILE* trace = NULL;
    if (cfg->trace_path[0] != '\0') {
        trace = fopen(cfg->trace_path, "w");
        if (trace) {
            fprintf(trace, "step,float_score,approx_score,float_accept,approx_accept,same_order_score_diff\n");
        }
    }

    int initial_order[NUM_NODES], float_order[NUM_NODES], approx_order[NUM_NODES];
    init_order(initial_order);
    copy_order(float_order, initial_order);
    copy_order(approx_order, initial_order);
    double float_score = score_order_backend(float_order, &float_backend);
    double approx_score = score_order_backend(approx_order, &approx_backend);
    int float_accepted = 0, approx_accepted = 0;
    int chain_decision_diff = 0, same_state_decision_diff = 0;
    double same_order_abs_sum = 0.0, same_order_abs_max = 0.0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int step = 0; step < cfg->iterations; step++) {
        int idx1 = rand() % NUM_NODES;
        int idx2 = rand() % NUM_NODES;
        double u = (double)rand() / (double)RAND_MAX;
        int float_prop[NUM_NODES], approx_prop[NUM_NODES];
        copy_order(float_prop, float_order);
        copy_order(approx_prop, approx_order);
        swap_order_indices(float_prop, idx1, idx2);
        swap_order_indices(approx_prop, idx1, idx2);

        double float_prop_score = score_order_backend(float_prop, &float_backend);
        double approx_prop_score = score_order_backend(approx_prop, &approx_backend);
        bool float_accept = accept_move(float_prop_score, float_score, u);
        bool approx_accept = accept_move(approx_prop_score, approx_score, u);
        double approx_same_current = score_order_backend(float_order, &approx_backend);
        double approx_same_proposed = score_order_backend(float_prop, &approx_backend);
        bool approx_same_accept = accept_move(approx_same_proposed, approx_same_current, u);
        if (float_accept) {
            copy_order(float_order, float_prop);
            float_score = float_prop_score;
            float_accepted++;
        }
        if (approx_accept) {
            copy_order(approx_order, approx_prop);
            approx_score = approx_prop_score;
            approx_accepted++;
        }
        chain_decision_diff += float_accept != approx_accept;
        same_state_decision_diff += float_accept != approx_same_accept;

        double approx_same_order = score_order_backend(float_order, &approx_backend);
        double same_order_abs = fabs(approx_same_order - float_score);
        same_order_abs_sum += same_order_abs;
        if (same_order_abs > same_order_abs_max) {
            same_order_abs_max = same_order_abs;
        }
        if (trace) {
            fprintf(trace, "%d,%.9f,%.9f,%d,%d,%.9f\n",
                    step, float_score, approx_score, float_accept ? 1 : 0, approx_accept ? 1 : 0, same_order_abs);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    int order_match_positions = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        order_match_positions += float_order[i] == approx_order[i];
    }

    printf("COMPARE_RESULT mode=%s iterations=%d float_acceptance=%.6f approx_acceptance=%.6f "
           "accept_decision_diff=%d chain_accept_decision_diff=%d same_order_mae=%.9f same_order_max=%.9f "
           "final_float_score=%.9f final_approx_score=%.9f final_score_drift=%.9f "
           "order_match_positions=%d overflow=%llu saturation=%llu lut_low=%llu lut_high=%llu elapsed=%.6f\n",
           fx_lut_mode_name(cfg->compare_mode), cfg->iterations,
           (double)float_accepted / (double)cfg->iterations,
           (double)approx_accepted / (double)cfg->iterations, same_state_decision_diff, chain_decision_diff,
           same_order_abs_sum / (double)cfg->iterations, same_order_abs_max,
           float_score, approx_score, approx_score - float_score, order_match_positions,
           (unsigned long long)approx_backend.fx.stats.overflow_count,
           (unsigned long long)approx_backend.fx.stats.saturation_count,
           (unsigned long long)approx_backend.fx.stats.lut_low_count,
           (unsigned long long)approx_backend.fx.stats.lut_high_count,
           elapsed_seconds(start, end));

    if (trace) {
        fclose(trace);
    }
    close_backend(&float_backend);
    close_backend(&approx_backend);
}

static void init_config(AppConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->samples_path, sizeof(cfg->samples_path), "../cleaned-datasets/%s_samples.csv", DATASET_NAME);
    snprintf(cfg->edges_path, sizeof(cfg->edges_path), "../cleaned-datasets/%s_edges.csv", DATASET_NAME);
    cfg->iterations = 100000;
    cfg->seed = 1;
    cfg->backend_kind = SCORE_BACKEND_FLOAT;
    cfg->fx_mode = FX_LUT_PIECEWISE;
    cfg->compare_mode = FX_LUT_PIECEWISE;
    cfg->fx_total_bits = 32;
    cfg->fx_int_bits = 16;
    cfg->lut_total_bits = 32;
    cfg->lut_int_bits = 16;
    cfg->lut_entries = 1024;
    cfg->lut_min = -16.0;
    cfg->lut_max = 8.0;
}

static void print_usage(const char* argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("  --mode float|fixed-clamp|fixed-piecewise\n");
    printf("  --compare-mode fixed-clamp|fixed-piecewise\n");
    printf("  --iters N --seed N --samples path --edges path --score-table path --quiet\n");
    printf("  --fx-total N --fx-int N --lut-total N --lut-int N\n");
    printf("  --lut-min X --lut-max X --lut-entries N\n");
    printf("  --x-log path --x-log-limit N --trace path\n");
}

static int parse_args(AppConfig* cfg, int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "float") == 0) {
                cfg->backend_kind = SCORE_BACKEND_FLOAT;
            } else if (fx_lut_mode_from_string(argv[i], &cfg->fx_mode) == 0) {
                cfg->backend_kind = SCORE_BACKEND_FIXED;
            } else {
                return -1;
            }
        } else if (strcmp(argv[i], "--compare-mode") == 0 && i + 1 < argc) {
            cfg->compare = true;
            if (fx_lut_mode_from_string(argv[++i], &cfg->compare_mode) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            cfg->iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg->seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
            snprintf(cfg->samples_path, sizeof(cfg->samples_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--edges") == 0 && i + 1 < argc) {
            snprintf(cfg->edges_path, sizeof(cfg->edges_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--score-table") == 0 && i + 1 < argc) {
            snprintf(cfg->score_table_path, sizeof(cfg->score_table_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            cfg->quiet = true;
        } else if (strcmp(argv[i], "--fx-total") == 0 && i + 1 < argc) {
            cfg->fx_total_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fx-int") == 0 && i + 1 < argc) {
            cfg->fx_int_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-total") == 0 && i + 1 < argc) {
            cfg->lut_total_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-int") == 0 && i + 1 < argc) {
            cfg->lut_int_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-min") == 0 && i + 1 < argc) {
            cfg->lut_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--lut-max") == 0 && i + 1 < argc) {
            cfg->lut_max = atof(argv[++i]);
        } else if (strcmp(argv[i], "--lut-entries") == 0 && i + 1 < argc) {
            cfg->lut_entries = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--x-log") == 0 && i + 1 < argc) {
            snprintf(cfg->x_log_path, sizeof(cfg->x_log_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--x-log-limit") == 0 && i + 1 < argc) {
            cfg->x_log_limit = atol(argv[++i]);
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            snprintf(cfg->trace_path, sizeof(cfg->trace_path), "%s", argv[++i]);
        } else {
            return -1;
        }
    }
    return cfg->iterations > 0 ? 0 : -1;
}

int main(int argc, char** argv) {
    AppConfig cfg;
    init_config(&cfg);
    if (parse_args(&cfg, argc, argv) != 0) {
        print_usage(argv[0]);
        return 2;
    }
    srand(cfg.seed);

    int num_samples = 0;
    int** dataset = load_csv(cfg.samples_path, &num_samples, NUM_NODES);
    if (!dataset) {
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    const char* table_source = "fixed-k";
    if (cfg.score_table_path[0] != '\0') {
        if (load_precomputed_table(cfg.score_table_path) != 0) {
            free_dataset(dataset, num_samples);
            return 1;
        }
        table_source = "score-table";
    } else {
        precompute_fixed_k(dataset, num_samples);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double prep_elapsed = elapsed_seconds(start, end);
    if (!cfg.quiet) {
        printf("Table preparation took %f seconds\n", prep_elapsed);
    }

    if (cfg.compare) {
        run_mcmc_compare(&cfg);
        free_dataset(dataset, num_samples);
        return 0;
    }

    ScoreBackend backend;
    if (init_backend(&backend, &cfg, cfg.backend_kind, cfg.fx_mode) != 0) {
        fprintf(stderr, "ERROR: could not initialize score backend\n");
        free_dataset(dataset, num_samples);
        return 1;
    }
    prepare_fixed_scores(&backend);

    FILE* trace = NULL;
    if (cfg.trace_path[0] != '\0') {
        trace = fopen(cfg.trace_path, "w");
        if (trace) {
            fprintf(trace, "step,score,accepted\n");
        }
    }

    int final_order[NUM_NODES];
    RunStats run_stats = run_mcmc_single(&cfg, &backend, final_order, trace);
    if (trace) {
        fclose(trace);
    }

    const char* mode_name = backend.kind == SCORE_BACKEND_FLOAT ? "float" : fx_lut_mode_name(cfg.fx_mode);
    printf("RESULT table_source=%s mode=%s prep_elapsed=%.6f iterations=%d final_score=%.9f accepted=%d acceptance_rate=%.6f "
           "overflow=%llu saturation=%llu lut_low=%llu lut_high=%llu elapsed=%.6f\n",
           table_source, mode_name, prep_elapsed, cfg.iterations, run_stats.final_score, run_stats.accepted,
           (double)run_stats.accepted / (double)cfg.iterations,
           backend.kind == SCORE_BACKEND_FIXED ? (unsigned long long)backend.fx.stats.overflow_count : 0ULL,
           backend.kind == SCORE_BACKEND_FIXED ? (unsigned long long)backend.fx.stats.saturation_count : 0ULL,
           backend.kind == SCORE_BACKEND_FIXED ? (unsigned long long)backend.fx.stats.lut_low_count : 0ULL,
           backend.kind == SCORE_BACKEND_FIXED ? (unsigned long long)backend.fx.stats.lut_high_count : 0ULL,
           run_stats.elapsed_sec);

    if (!cfg.quiet) {
        print_order(final_order);
        print_edge_metrics(final_order, cfg.edges_path);
    }

    close_backend(&backend);
    free_dataset(dataset, num_samples);
    return 0;
}
