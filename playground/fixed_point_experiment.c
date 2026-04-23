#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_point_logadd.h"

typedef struct {
    FxLutMode mode;
    int total_bits;
    int int_bits;
    int lut_total_bits;
    int lut_int_bits;
    int entries;
    double lut_min;
    double lut_max;
    bool csv_header;
} Config;

static double approx_value(FxLogAdd* ctx, double x) {
    fx_raw_t x_raw = fx_from_double(&ctx->value_fmt, x, &ctx->stats);
    fx_raw_t y_raw = fx_log1pexp_approx_raw(ctx, x_raw);
    return fx_to_double(&ctx->value_fmt, y_raw);
}

static void update_error(double approx, double ref, double* max_abs, double* sum_abs, double* sum_sq,
                         double* max_rel, int* count) {
    double err = fabs(approx - ref);
    *sum_abs += err;
    *sum_sq += err * err;
    if (err > *max_abs) {
        *max_abs = err;
    }
    if (fabs(ref) > 1e-6) {
        double rel = err / fabs(ref);
        if (rel > *max_rel) {
            *max_rel = rel;
        }
    }
    (*count)++;
}

static double repeated_accum_error(FxLogAdd* ctx) {
    double golden = -INFINITY;
    fx_raw_t fixed = 0;
    bool have_fixed = false;

    for (int i = 0; i < 512; i++) {
        double ls = -250.0 + 0.45 * (double)i + 0.35 * sin((double)i * 0.37);
        golden = isfinite(golden) ? golden + fx_log1pexp_reference(ls - golden) : ls;
        fx_raw_t ls_raw = fx_from_double(&ctx->value_fmt, ls, &ctx->stats);
        fixed = have_fixed ? fx_logadd_update_raw(ctx, fixed, ls_raw) : ls_raw;
        have_fixed = true;
    }

    return fabs(fx_to_double(&ctx->value_fmt, fixed) - golden);
}

static int monotonic_issues(FxLogAdd* ctx) {
    int issues = 0;
    double prev = -INFINITY;
    for (int i = 0; i < 4096; i++) {
        double x = -64.0 + 128.0 * ((double)i / 4095.0);
        double y = approx_value(ctx, x);
        if (i > 0 && y + 1e-12 < prev) {
            issues++;
        }
        prev = y;
    }
    return issues;
}

static int run_config(const Config* cfg) {
    FxLogAdd ctx;
    if (fx_logadd_init(&ctx, cfg->total_bits, cfg->int_bits, cfg->lut_total_bits, cfg->lut_int_bits,
                       cfg->lut_min, cfg->lut_max, cfg->entries, cfg->mode) != 0) {
        fprintf(stderr, "bad fixed-point/LUT config\n");
        return 2;
    }

    double max_abs = 0.0, sum_abs = 0.0, sum_sq = 0.0, max_rel = 0.0;
    int count = 0;
    for (int i = 0; i < 20001; i++) {
        double x = -64.0 + 128.0 * ((double)i / 20000.0);
        update_error(approx_value(&ctx, x), fx_log1pexp_reference(x),
                     &max_abs, &sum_abs, &sum_sq, &max_rel, &count);
    }

    double probes[] = {
        cfg->lut_min - 1.0, cfg->lut_min, cfg->lut_min + 1e-6,
        -1e-6, 0.0, 1e-6,
        cfg->lut_max - 1e-6, cfg->lut_max, cfg->lut_max + 1.0
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        update_error(approx_value(&ctx, probes[i]), fx_log1pexp_reference(probes[i]),
                     &max_abs, &sum_abs, &sum_sq, &max_rel, &count);
    }

    int mono = monotonic_issues(&ctx);
    double repeat_err = repeated_accum_error(&ctx);

    if (cfg->csv_header) {
        printf("mode,total_bits,int_bits,frac_bits,lut_total_bits,lut_int_bits,lut_frac_bits,"
               "lut_min,lut_max,lut_entries,lut_address_width,lut_memory_bits,max_abs_error,"
               "mean_abs_error,rmse,max_relative_error,overflow_count,saturation_count,"
               "lut_low_count,lut_high_count,table_saturation_count,monotonic_issues,"
               "repeated_accum_abs_error\n");
    }

    printf("%s,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%d,%d,%llu,%.12g,%.12g,%.12g,%.12g,"
           "%llu,%llu,%llu,%llu,%llu,%d,%.12g\n",
           fx_lut_mode_name(cfg->mode),
           ctx.value_fmt.total_bits,
           ctx.value_fmt.int_bits,
           ctx.value_fmt.frac_bits,
           ctx.lut_fmt.total_bits,
           ctx.lut_fmt.int_bits,
           ctx.lut_fmt.frac_bits,
           ctx.lut_min,
           ctx.lut_max,
           ctx.entries,
           ctx.address_width,
           (unsigned long long)fx_lut_memory_bits(&ctx),
           max_abs,
           sum_abs / (double)count,
           sqrt(sum_sq / (double)count),
           max_rel,
           (unsigned long long)ctx.stats.overflow_count,
           (unsigned long long)ctx.stats.saturation_count,
           (unsigned long long)ctx.stats.lut_low_count,
           (unsigned long long)ctx.stats.lut_high_count,
           (unsigned long long)ctx.table_saturation_count,
           mono,
           repeat_err);

    fx_logadd_free(&ctx);
    return 0;
}

static void init_config(Config* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = FX_LUT_PIECEWISE;
    cfg->total_bits = 24;
    cfg->int_bits = 12;
    cfg->lut_total_bits = 24;
    cfg->lut_int_bits = 12;
    cfg->entries = 1024;
    cfg->lut_min = -16.0;
    cfg->lut_max = 8.0;
}

static int parse_args(Config* cfg, int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (fx_lut_mode_from_string(argv[++i], &cfg->mode) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--total") == 0 && i + 1 < argc) {
            cfg->total_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--int") == 0 && i + 1 < argc) {
            cfg->int_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-total") == 0 && i + 1 < argc) {
            cfg->lut_total_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-int") == 0 && i + 1 < argc) {
            cfg->lut_int_bits = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) {
            cfg->entries = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lut-min") == 0 && i + 1 < argc) {
            cfg->lut_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--lut-max") == 0 && i + 1 < argc) {
            cfg->lut_max = atof(argv[++i]);
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            cfg->csv_header = true;
        } else {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    Config cfg;
    init_config(&cfg);
    if (parse_args(&cfg, argc, argv) != 0) {
        fprintf(stderr, "usage: %s --mode fixed-piecewise|fixed-clamp --total N --int N "
                "--lut-total N --lut-int N --entries N --lut-min X --lut-max X [--csv-header]\n", argv[0]);
        return 2;
    }
    return run_config(&cfg);
}
