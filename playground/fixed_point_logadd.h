#ifndef FIXED_POINT_LOGADD_H
#define FIXED_POINT_LOGADD_H

#include <stdint.h>
#include <stdio.h>

typedef int64_t fx_raw_t;

typedef enum {
    FX_LUT_CLAMP = 0,
    FX_LUT_PIECEWISE = 1
} FxLutMode;

typedef struct {
    int total_bits;
    int int_bits;
    int frac_bits;
    fx_raw_t min_raw;
    fx_raw_t max_raw;
} FxFormat;

typedef struct {
    uint64_t overflow_count;
    uint64_t saturation_count;
    uint64_t lut_low_count;
    uint64_t lut_high_count;
} FxStats;

typedef struct {
    FxFormat value_fmt;
    FxFormat lut_fmt;
    double lut_min;
    double lut_max;
    fx_raw_t lut_min_raw;
    fx_raw_t lut_max_raw;
    int entries;
    int address_width;
    FxLutMode mode;
    fx_raw_t* table;
    uint64_t table_saturation_count;
    FxStats stats;
} FxLogAdd;

int fx_format_init(FxFormat* fmt, int total_bits, int int_bits);
fx_raw_t fx_from_double(const FxFormat* fmt, double value, FxStats* stats);
double fx_to_double(const FxFormat* fmt, fx_raw_t raw);
fx_raw_t fx_convert_raw(const FxFormat* src, const FxFormat* dst, fx_raw_t raw, FxStats* stats);
fx_raw_t fx_add_raw(const FxFormat* fmt, fx_raw_t a, fx_raw_t b, FxStats* stats);
fx_raw_t fx_sub_raw(const FxFormat* fmt, fx_raw_t a, fx_raw_t b, FxStats* stats);

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
);
void fx_logadd_free(FxLogAdd* ctx);
void fx_logadd_reset_stats(FxLogAdd* ctx);
fx_raw_t fx_log1pexp_approx_raw(FxLogAdd* ctx, fx_raw_t x_raw);
fx_raw_t fx_logadd_update_raw(FxLogAdd* ctx, fx_raw_t score_cur, fx_raw_t ls_next);
double fx_logadd_update_double(FxLogAdd* ctx, double score_cur, double ls_next);
uint64_t fx_lut_memory_bits(const FxLogAdd* ctx);
const char* fx_lut_mode_name(FxLutMode mode);
int fx_lut_mode_from_string(const char* text, FxLutMode* out_mode);
double fx_log1pexp_reference(double x);

#endif
