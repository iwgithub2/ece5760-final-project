#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixed_point_logadd.h"

static void require_true(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static double approx_value(FxLogAdd* ctx, double x) {
    fx_raw_t x_raw = fx_from_double(&ctx->value_fmt, x, &ctx->stats);
    fx_raw_t y_raw = fx_log1pexp_approx_raw(ctx, x_raw);
    return fx_to_double(&ctx->value_fmt, y_raw);
}

static void test_directed_edges(void) {
    FxLogAdd ctx;
    require_true(fx_logadd_init(&ctx, 24, 12, 24, 12, -16.0, 8.0, 1024, FX_LUT_PIECEWISE) == 0,
                 "init piecewise Q24");

    double xs[] = {-80.0, -16.0, -15.99, -0.001, 0.0, 0.001, 7.99, 8.0, 30.0};
    double max_err = 0.0;
    for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
        double err = fabs(approx_value(&ctx, xs[i]) - fx_log1pexp_reference(xs[i]));
        if (err > max_err) {
            max_err = err;
        }
    }
    require_true(max_err < 0.03, "piecewise LUT directed error < 0.03");
    require_true(ctx.stats.lut_low_count > 0, "piecewise low out-of-range counted");
    require_true(ctx.stats.lut_high_count > 0, "piecewise high out-of-range counted");
    fx_logadd_free(&ctx);
}

static void test_random_function_error(void) {
    FxLogAdd ctx;
    require_true(fx_logadd_init(&ctx, 24, 12, 24, 12, -16.0, 8.0, 1024, FX_LUT_PIECEWISE) == 0,
                 "init random test");

    unsigned int state = 1;
    double sum_err = 0.0;
    double max_err = 0.0;
    for (int i = 0; i < 20000; i++) {
        state = state * 1103515245U + 12345U;
        double u = (double)(state & 0x00ffffffU) / (double)0x01000000U;
        double x = -32.0 + 64.0 * u;
        double err = fabs(approx_value(&ctx, x) - fx_log1pexp_reference(x));
        sum_err += err;
        if (err > max_err) {
            max_err = err;
        }
    }
    require_true(sum_err / 20000.0 < 0.01, "random mean error < 0.01");
    require_true(max_err < 0.03, "random max error < 0.03");
    fx_logadd_free(&ctx);
}

static void test_repeated_accumulation(void) {
    FxLogAdd ctx;
    require_true(fx_logadd_init(&ctx, 32, 16, 32, 16, -16.0, 8.0, 1024, FX_LUT_PIECEWISE) == 0,
                 "init accumulation test");

    double golden = -INFINITY;
    fx_raw_t fixed = 0;
    int have_fixed = 0;
    for (int i = 0; i < 200; i++) {
        double ls = -200.0 + 0.7 * (double)i + 0.25 * sin((double)i);
        golden = isfinite(golden) ? golden + fx_log1pexp_reference(ls - golden) : ls;
        fx_raw_t ls_raw = fx_from_double(&ctx.value_fmt, ls, &ctx.stats);
        fixed = have_fixed ? fx_logadd_update_raw(&ctx, fixed, ls_raw) : ls_raw;
        have_fixed = 1;
    }

    double fixed_d = fx_to_double(&ctx.value_fmt, fixed);
    require_true(fabs(fixed_d - golden) < 0.05, "repeated accumulation error < 0.05");
    require_true(ctx.stats.overflow_count == 0, "no overflow in Q32 accumulation");
    fx_logadd_free(&ctx);
}

int main(void) {
    test_directed_edges();
    test_random_function_error();
    test_repeated_accumulation();
    printf("fixed_point_logadd tests passed\n");
    return 0;
}
