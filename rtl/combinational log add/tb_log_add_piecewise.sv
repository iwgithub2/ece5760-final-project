`timescale 1ns / 1ps

module tb_log_add_piecewise;
    localparam int DATA_W = 24;
    localparam int FRAC_BITS = 7;
    localparam int ADDR_W = 10;
    localparam int LUT_CASES = 64;
    localparam int UPDATE_CASES = 48;
    localparam int CHAIN_CASES = 40;
    localparam int CHAIN_INPUTS = 4;
    localparam int LUT_VEC_W = 2 * DATA_W;
    localparam int UPDATE_VEC_W = 3 * DATA_W;
    localparam int CHAIN_VEC_W = (CHAIN_INPUTS + 1) * DATA_W;

    logic signed [DATA_W-1:0] x_lut;
    logic signed [DATA_W-1:0] fx_lut;

    logic signed [DATA_W-1:0] score_cur_update;
    logic signed [DATA_W-1:0] ls_next_update;
    logic signed [DATA_W-1:0] result_update;

    logic signed [DATA_W-1:0] chain_values [0:CHAIN_INPUTS-1];
    logic signed [DATA_W-1:0] result_chain;

    logic [LUT_VEC_W-1:0] lut_vectors [0:LUT_CASES-1];
    logic [UPDATE_VEC_W-1:0] update_vectors [0:UPDATE_CASES-1];
    logic [CHAIN_VEC_W-1:0] chain_vectors [0:CHAIN_CASES-1];

    logadd_lut #(
        .DATA_W(DATA_W),
        .FRAC_BITS(FRAC_BITS),
        .ADDR_W(ADDR_W)
    ) dut_lut (
        .x_in(x_lut),
        .fx_out(fx_lut)
    );

    log_add_update #(
        .DATA_W(DATA_W),
        .FRAC_BITS(FRAC_BITS),
        .ADDR_W(ADDR_W)
    ) dut_update (
        .score_cur(score_cur_update),
        .ls_next(ls_next_update),
        .result(result_update)
    );

    log_add_chain #(
        .DATA_W(DATA_W),
        .FRAC_BITS(FRAC_BITS),
        .ADDR_W(ADDR_W),
        .N_INPUTS(CHAIN_INPUTS)
    ) dut_chain (
        .in_values(chain_values),
        .result(result_chain)
    );

    task automatic clear_inputs;
        integer idx;
        begin
            x_lut = '0;
            score_cur_update = '0;
            ls_next_update = '0;
            for (idx = 0; idx < CHAIN_INPUTS; idx = idx + 1) begin
                chain_values[idx] = '0;
            end
        end
    endtask

    task automatic run_lut_tests;
        integer idx;
        logic signed [DATA_W-1:0] expected;
        begin
            for (idx = 0; idx < LUT_CASES; idx = idx + 1) begin
                x_lut = $signed(lut_vectors[idx][LUT_VEC_W-1 -: DATA_W]);
                #1;
                expected = $signed(lut_vectors[idx][DATA_W-1:0]);
                if (fx_lut !== expected) begin
                    $display(
                        "LUT MISMATCH case=%0d x=%0h expected=%0h got=%0h addr=%0d",
                        idx,
                        x_lut,
                        expected,
                        fx_lut,
                        dut_lut.lut_addr
                    );
                    $fatal(1);
                end
            end
        end
    endtask

    task automatic run_update_tests;
        integer idx;
        logic signed [DATA_W-1:0] expected;
        begin
            for (idx = 0; idx < UPDATE_CASES; idx = idx + 1) begin
                score_cur_update = $signed(update_vectors[idx][UPDATE_VEC_W-1 -: DATA_W]);
                ls_next_update = $signed(update_vectors[idx][UPDATE_VEC_W-DATA_W-1 -: DATA_W]);
                #1;
                expected = $signed(update_vectors[idx][DATA_W-1:0]);
                if (result_update !== expected) begin
                    $display(
                        "UPDATE MISMATCH case=%0d score=%0h next=%0h expected=%0h got=%0h x=%0h fx=%0h",
                        idx,
                        score_cur_update,
                        ls_next_update,
                        expected,
                        result_update,
                        dut_update.x_value,
                        dut_update.fx_value
                    );
                    $fatal(1);
                end
            end
        end
    endtask

    task automatic run_chain_tests;
        integer case_idx;
        integer value_idx;
        logic signed [DATA_W-1:0] expected;
        begin
            for (case_idx = 0; case_idx < CHAIN_CASES; case_idx = case_idx + 1) begin
                for (value_idx = 0; value_idx < CHAIN_INPUTS; value_idx = value_idx + 1) begin
                    chain_values[value_idx] = $signed(
                        chain_vectors[case_idx][CHAIN_VEC_W - 1 - (value_idx * DATA_W) -: DATA_W]
                    );
                end
                #1;
                expected = $signed(chain_vectors[case_idx][DATA_W-1:0]);
                if (result_chain !== expected) begin
                    $display(
                        "CHAIN MISMATCH case=%0d expected=%0h got=%0h s0=%0h s1=%0h s2=%0h",
                        case_idx,
                        expected,
                        result_chain,
                        dut_chain.stage_score[1],
                        dut_chain.stage_score[2],
                        dut_chain.stage_score[3]
                    );
                    $fatal(1);
                end
            end
        end
    endtask

    initial begin
        $readmemh("logadd_lut_vectors.hex", lut_vectors);
        $readmemh("logadd_update_vectors.hex", update_vectors);
        $readmemh("logadd_chain_vectors.hex", chain_vectors);
    end

    initial begin
        clear_inputs();
        $dumpfile("tb_log_add_piecewise.vcd");
        $dumpvars(0, tb_log_add_piecewise);

        run_lut_tests();
        clear_inputs();
        run_update_tests();
        clear_inputs();
        run_chain_tests();

        $display("log_add_piecewise RTL tests passed");
        $finish;
    end
endmodule
