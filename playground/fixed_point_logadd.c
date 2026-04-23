#include "fixed_point_logadd.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int ceil_log2_int(int value) {
    int width = 0;
    int limit = 1;
    while (limit < value) {
        limit <<= 1;
        width++;
    }
    return width;
}

static fx_raw_t saturate_i128(const FxFormat* fmt, __int128 value, FxStats* stats) {
    if (value > fmt->max_raw) {
        if (stats) {
            stats->overflow_count++;
            stats->saturation_count++;
        }
        return fmt->max_raw;
    }
    if (value < fmt->min_raw) {
        if (stats) {
            stats->overflow_count++;
            stats->saturation_count++;
        }
        return fmt->min_raw;
    }
    return (fx_raw_t)value;
}

int fx_format_init(FxFormat* fmt, int total_bits, int int_bits) {
    if (!fmt || total_bits < 2 || total_bits > 62 || int_bits < 0 || int_bits >= total_bits) {
        return -1;
    }

    int frac_bits = total_bits - int_bits - 1;
    if (frac_bits < 0) {
        return -1;
    }

    fmt->total_bits = total_bits;
    fmt->int_bits = int_bits;
    fmt->frac_bits = frac_bits;
    fmt->min_raw = -(1LL << (total_bits - 1));
    fmt->max_raw = (1LL << (total_bits - 1)) - 1;
    return 0;
}

fx_raw_t fx_from_double(const FxFormat* fmt, double value, FxStats* stats) {
    double scaled = ldexp(value, fmt->frac_bits);
    if (scaled > (double)fmt->max_raw) {
        if (stats) {
            stats->overflow_count++;
            stats->saturation_count++;
        }
        return fmt->max_raw;
    }
    if (scaled < (double)fmt->min_raw) {
        if (stats) {
            stats->overflow_count++;
            stats->saturation_count++;
        }
        return fmt->min_raw;
    }
    return (fx_raw_t)llround(scaled);
}

double fx_to_double(const FxFormat* fmt, fx_raw_t raw) {
    return ldexp((double)raw, -fmt->frac_bits);
}

fx_raw_t fx_convert_raw(const FxFormat* src, const FxFormat* dst, fx_raw_t raw, FxStats* stats) {
    __int128 shifted = raw;
    int shift = dst->frac_bits - src->frac_bits;
    if (shift > 0) {
        shifted <<= shift;
    } else if (shift < 0) {
        int rshift = -shift;
        __int128 half = ((__int128)1) << (rshift - 1);
        if (shifted >= 0) {
            shifted = (shifted + half) >> rshift;
        } else {
            shifted = -(((-shifted) + half) >> rshift);
        }
    }
    return saturate_i128(dst, shifted, stats);
}

fx_raw_t fx_add_raw(const FxFormat* fmt, fx_raw_t a, fx_raw_t b, FxStats* stats) {
    return saturate_i128(fmt, (__int128)a + (__int128)b, stats);
}

fx_raw_t fx_sub_raw(const FxFormat* fmt, fx_raw_t a, fx_raw_t b, FxStats* stats) {
    return saturate_i128(fmt, (__int128)a - (__int128)b, stats);
}

double fx_log1pexp_reference(double x) {
    if (x > 0.0) {
        return x + log1p(exp(-x));
    }
    return log1p(exp(x));
}

int fx_logadd_init(
    FxLogAdd* ctx,
    int value_total_bits,
    int value_int_bits,
    int lut_total_bits,
    int lut_int_bits,
    double lut_min,
    double lut_max,
    int entries,
    FxLutMode mode
) {
    if (!ctx || entries < 2 || !(lut_min < lut_max)) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (fx_format_init(&ctx->value_fmt, value_total_bits, value_int_bits) != 0) {
        return -1;
    }
    if (fx_format_init(&ctx->lut_fmt, lut_total_bits, lut_int_bits) != 0) {
        return -1;
    }

    ctx->lut_min = lut_min;
    ctx->lut_max = lut_max;
    ctx->entries = entries;
    ctx->address_width = ceil_log2_int(entries);
    ctx->mode = mode;
    ctx->lut_min_raw = fx_from_double(&ctx->value_fmt, lut_min, &ctx->stats);
    ctx->lut_max_raw = fx_from_double(&ctx->value_fmt, lut_max, &ctx->stats);
    ctx->table = (fx_raw_t*)calloc((size_t)entries, sizeof(fx_raw_t));
    if (!ctx->table) {
        return -1;
    }

    FxStats table_stats = {0};
    double denom = (double)(entries - 1);
    for (int i = 0; i < entries; i++) {
        double x = lut_min + (lut_max - lut_min) * ((double)i / denom);
        ctx->table[i] = fx_from_double(&ctx->lut_fmt, fx_log1pexp_reference(x), &table_stats);
    }
    ctx->table_saturation_count = table_stats.saturation_count;
    fx_logadd_reset_stats(ctx);
    return 0;
}

void fx_logadd_free(FxLogAdd* ctx) {
    if (!ctx) {
        return;
    }
    free(ctx->table);
    ctx->table = NULL;
}

void fx_logadd_reset_stats(FxLogAdd* ctx) {
    if (ctx) {
        memset(&ctx->stats, 0, sizeof(ctx->stats));
    }
}

fx_raw_t fx_log1pexp_approx_raw(FxLogAdd* ctx, fx_raw_t x_raw) {
    if (ctx->mode == FX_LUT_PIECEWISE) {
        if (x_raw < ctx->lut_min_raw) {
            ctx->stats.lut_low_count++;
            return 0;
        }
        if (x_raw > ctx->lut_max_raw) {
            ctx->stats.lut_high_count++;
            return x_raw;
        }
    } else {
        if (x_raw < ctx->lut_min_raw) {
            x_raw = ctx->lut_min_raw;
            ctx->stats.lut_low_count++;
            ctx->stats.saturation_count++;
        } else if (x_raw > ctx->lut_max_raw) {
            x_raw = ctx->lut_max_raw;
            ctx->stats.lut_high_count++;
            ctx->stats.saturation_count++;
        }
    }

    __int128 range = (__int128)ctx->lut_max_raw - (__int128)ctx->lut_min_raw;
    __int128 offset = (__int128)x_raw - (__int128)ctx->lut_min_raw;
    int addr = 0;
    if (range > 0) {
        addr = (int)((offset * (ctx->entries - 1)) / range);
    }
    if (addr < 0) {
        addr = 0;
    } else if (addr >= ctx->entries) {
        addr = ctx->entries - 1;
    }
    return fx_convert_raw(&ctx->lut_fmt, &ctx->value_fmt, ctx->table[addr], &ctx->stats);
}

fx_raw_t fx_logadd_update_raw(FxLogAdd* ctx, fx_raw_t score_cur, fx_raw_t ls_next) {
    fx_raw_t x_raw = fx_sub_raw(&ctx->value_fmt, ls_next, score_cur, &ctx->stats);
    fx_raw_t nonlinear = fx_log1pexp_approx_raw(ctx, x_raw);
    return fx_add_raw(&ctx->value_fmt, score_cur, nonlinear, &ctx->stats);
}

double fx_logadd_update_double(FxLogAdd* ctx, double score_cur, double ls_next) {
    fx_raw_t cur_raw = fx_from_double(&ctx->value_fmt, score_cur, &ctx->stats);
    fx_raw_t next_raw = fx_from_double(&ctx->value_fmt, ls_next, &ctx->stats);
    return fx_to_double(&ctx->value_fmt, fx_logadd_update_raw(ctx, cur_raw, next_raw));
}

uint64_t fx_lut_memory_bits(const FxLogAdd* ctx) {
    return (uint64_t)ctx->entries * (uint64_t)ctx->lut_fmt.total_bits;
}

const char* fx_lut_mode_name(FxLutMode mode) {
    return mode == FX_LUT_PIECEWISE ? "fixed-piecewise" : "fixed-clamp";
}

int fx_lut_mode_from_string(const char* text, FxLutMode* out_mode) {
    if (!text || !out_mode) {
        return -1;
    }
    if (strcmp(text, "fixed-clamp") == 0 || strcmp(text, "clamp") == 0) {
        *out_mode = FX_LUT_CLAMP;
        return 0;
    }
    if (strcmp(text, "fixed-piecewise") == 0 || strcmp(text, "piecewise") == 0) {
        *out_mode = FX_LUT_PIECEWISE;
        return 0;
    }
    return -1;
}
