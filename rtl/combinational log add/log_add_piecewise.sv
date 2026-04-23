`timescale 1ns / 1ps
/* verilator lint_off DECLFILENAME */

module logadd_lut #(
    parameter int DATA_W = 24,
    parameter int FRAC_BITS = 7,
    parameter int ADDR_W = 10,
    parameter int LUT_NEG_INT = -8,
    parameter int LUT_POS_INT = 8
) (
    input  logic signed [DATA_W-1:0] x_in,
    output logic signed [DATA_W-1:0] fx_out
);
    localparam int RANGE_INT = LUT_POS_INT - LUT_NEG_INT;
    localparam int STEP_SHIFT = FRAC_BITS + $clog2(RANGE_INT) - ADDR_W;
    localparam int signed LUT_NEG_BOUND_INT = LUT_NEG_INT <<< FRAC_BITS;
    localparam int signed LUT_POS_BOUND_INT = LUT_POS_INT <<< FRAC_BITS;
    localparam logic signed [DATA_W-1:0] LUT_NEG_BOUND = LUT_NEG_BOUND_INT[DATA_W-1:0];
    localparam logic signed [DATA_W-1:0] LUT_POS_BOUND = LUT_POS_BOUND_INT[DATA_W-1:0];

    logic [ADDR_W-1:0] lut_addr;
    logic signed [DATA_W-1:0] lut_data;

    /* verilator lint_off UNUSEDSIGNAL */
    function automatic logic [ADDR_W-1:0] trunc_addr(
        input logic signed [DATA_W-1:0] value
    );
        trunc_addr = value[ADDR_W-1:0];
    endfunction
    /* verilator lint_on UNUSEDSIGNAL */

    function automatic logic [ADDR_W-1:0] calc_addr(
        input logic signed [DATA_W-1:0] x_value
    );
        logic signed [DATA_W-1:0] x_offset;
        begin
            x_offset = x_value - LUT_NEG_BOUND;
            if (STEP_SHIFT > 0) begin
                calc_addr = trunc_addr(x_offset >>> STEP_SHIFT);
            end else begin
                calc_addr = trunc_addr(x_offset);
            end
        end
    endfunction

    assign lut_addr = ((x_in > LUT_NEG_BOUND) && (x_in < LUT_POS_BOUND)) ? calc_addr(x_in) : '0;

    always @* begin
        lut_data = '0;
        case (lut_addr[9:8])
            2'd0: begin
                case (lut_addr[7:0])
`include "rtl/logadd_q24_i16_1024_m8_8_bank0.svh"
                    default: lut_data = '0;
                endcase
            end
            2'd1: begin
                case (lut_addr[7:0])
`include "rtl/logadd_q24_i16_1024_m8_8_bank1.svh"
                    default: lut_data = '0;
                endcase
            end
            2'd2: begin
                case (lut_addr[7:0])
`include "rtl/logadd_q24_i16_1024_m8_8_bank2.svh"
                    default: lut_data = '0;
                endcase
            end
            2'd3: begin
                case (lut_addr[7:0])
`include "rtl/logadd_q24_i16_1024_m8_8_bank3.svh"
                    default: lut_data = '0;
                endcase
            end
            default: lut_data = '0;
        endcase

        if (x_in <= LUT_NEG_BOUND) begin
            fx_out = '0;
        end else if (x_in >= LUT_POS_BOUND) begin
            fx_out = x_in;
        end else begin
            fx_out = lut_data;
        end
    end
endmodule

module log_add_update #(
    parameter int DATA_W = 24,
    parameter int FRAC_BITS = 7,
    parameter int ADDR_W = 10,
    parameter int LUT_NEG_INT = -8,
    parameter int LUT_POS_INT = 8
) (
    input  logic signed [DATA_W-1:0] score_cur,
    input  logic signed [DATA_W-1:0] ls_next,
    output logic signed [DATA_W-1:0] result
);
    localparam logic signed [DATA_W:0] MAX_WIDE = (1 <<< (DATA_W - 1)) - 1;
    localparam logic signed [DATA_W:0] MIN_WIDE = -(1 <<< (DATA_W - 1));

    logic signed [DATA_W-1:0] x_value;
    logic signed [DATA_W-1:0] fx_value;

    function automatic logic signed [DATA_W-1:0] sat_from_wide(
        input logic signed [DATA_W:0] wide
    );
        if (wide > MAX_WIDE) begin
            sat_from_wide = MAX_WIDE[DATA_W-1:0];
        end else if (wide < MIN_WIDE) begin
            sat_from_wide = MIN_WIDE[DATA_W-1:0];
        end else begin
            sat_from_wide = wide[DATA_W-1:0];
        end
    endfunction

    function automatic logic signed [DATA_W-1:0] sat_add(
        input logic signed [DATA_W-1:0] a,
        input logic signed [DATA_W-1:0] b
    );
        logic signed [DATA_W:0] wide_sum;
        begin
            wide_sum = $signed({a[DATA_W-1], a}) + $signed({b[DATA_W-1], b});
            sat_add = sat_from_wide(wide_sum);
        end
    endfunction

    function automatic logic signed [DATA_W-1:0] sat_sub(
        input logic signed [DATA_W-1:0] a,
        input logic signed [DATA_W-1:0] b
    );
        logic signed [DATA_W:0] wide_diff;
        begin
            wide_diff = $signed({a[DATA_W-1], a}) - $signed({b[DATA_W-1], b});
            sat_sub = sat_from_wide(wide_diff);
        end
    endfunction

    assign x_value = sat_sub(ls_next, score_cur);

    logadd_lut #(
        .DATA_W(DATA_W),
        .FRAC_BITS(FRAC_BITS),
        .ADDR_W(ADDR_W),
        .LUT_NEG_INT(LUT_NEG_INT),
        .LUT_POS_INT(LUT_POS_INT)
    ) logadd_lut_inst (
        .x_in(x_value),
        .fx_out(fx_value)
    );

    assign result = sat_add(score_cur, fx_value);
endmodule

module log_add_chain #(
    parameter int DATA_W = 24,
    parameter int FRAC_BITS = 7,
    parameter int ADDR_W = 10,
    parameter int N_INPUTS = 4,
    parameter int LUT_NEG_INT = -8,
    parameter int LUT_POS_INT = 8
) (
    input  logic signed [DATA_W-1:0] in_values [0:N_INPUTS-1],
    output logic signed [DATA_W-1:0] result
);
    logic signed [DATA_W-1:0] stage_score [0:N_INPUTS-1];

    assign stage_score[0] = in_values[0];

    genvar g;
    generate
        for (g = 0; g < N_INPUTS - 1; g = g + 1) begin : gen_chain
            log_add_update #(
                .DATA_W(DATA_W),
                .FRAC_BITS(FRAC_BITS),
                .ADDR_W(ADDR_W),
                .LUT_NEG_INT(LUT_NEG_INT),
                .LUT_POS_INT(LUT_POS_INT)
            ) update_inst (
                .score_cur(stage_score[g]),
                .ls_next(in_values[g + 1]),
                .result(stage_score[g + 1])
            );
        end
    endgenerate

    assign result = stage_score[N_INPUTS - 1];
endmodule
/* verilator lint_on DECLFILENAME */
