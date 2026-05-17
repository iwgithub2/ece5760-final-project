`timescale 1ns / 1ps

module tb_mcmc_system_parallel_order_update;
    reg clk;
    reg reset_n;
    reg [11:0] avs_address;
    reg avs_write;
    reg [31:0] avs_writedata;
    reg avs_read;
    wire [31:0] avs_readdata;
    reg start;
    reg pio_reset;
    reg [31:0] seed;
    reg [31:0] iterations;
    reg [31:0] active_nodes;
    reg [31:0] node_idx_mask;
    wire done;
    wire [31:0] best_score;
    wire [31:0] clk_count;

    mcmc_system_parallel #(
        .N_NODES(32),
        .N_CHAINS(2)
    ) dut (
        .clk(clk),
        .reset_n(reset_n),
        .avs_address(avs_address),
        .avs_write(avs_write),
        .avs_writedata(avs_writedata),
        .avs_read(avs_read),
        .avs_readdata(avs_readdata),
        .start(start),
        .pio_reset(pio_reset),
        .seed(seed),
        .iterations(iterations),
        .active_nodes(active_nodes),
        .node_idx_mask(node_idx_mask),
        .done(done),
        .best_score(best_score),
        .clk_count(clk_count)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task avalon_write;
        input [11:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            avs_address <= addr;
            avs_writedata <= data;
            avs_write <= 1'b1;
            @(posedge clk);
            avs_write <= 1'b0;
            avs_address <= 12'd0;
            avs_writedata <= 32'd0;
        end
    endtask

    task write_candidate;
        input [4:0] node;
        input [5:0] cand;
        input [31:0] score;
        input [31:0] mask;
        begin
            avalon_write({node, cand, 1'b0}, score);
            avalon_write({node, cand, 1'b1}, mask);
        end
    endtask

    task avalon_read;
        input [11:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            avs_address <= addr;
            avs_read <= 1'b1;
            @(posedge clk);
            avs_read <= 1'b0;
            @(negedge clk);
            data = avs_readdata;
            avs_address <= 12'd0;
        end
    endtask

    integer inactive_node;
    integer node_idx;
    integer parent_idx;
    integer cand_idx;
    integer cycle;
    reg [31:0] order_word0;
    reg [31:0] order_word1;
    reg signed [31:0] expected_best_score;
    reg [(32*5)-1:0] expected_best_order;

    initial begin
        $dumpfile("tb_mcmc_system_parallel_order_update.vcd");
        $dumpvars(0, tb_mcmc_system_parallel_order_update);

        reset_n = 1'b0;
        avs_address = 12'd0;
        avs_write = 1'b0;
        avs_writedata = 32'd0;
        avs_read = 1'b0;
        start = 1'b0;
        pio_reset = 1'b0;
        seed = 32'h0000_00B7;
        iterations = 32'd96;
        active_nodes = 32'd8;
        node_idx_mask = 32'd7;
        cycle = 0;

        repeat (4) @(posedge clk);
        reset_n = 1'b1;
        repeat (2) @(posedge clk);

        for (node_idx = 0; node_idx < 8; node_idx = node_idx + 1) begin
            cand_idx = 0;
            write_candidate(node_idx[4:0], cand_idx[5:0], 32'h0000_0000, 32'h0000_0000);
            cand_idx = cand_idx + 1;
            for (parent_idx = node_idx + 1; parent_idx < 8; parent_idx = parent_idx + 1) begin
                write_candidate(node_idx[4:0], cand_idx[5:0], 32'h0001_0000, 32'h0000_0001 << parent_idx);
                cand_idx = cand_idx + 1;
            end
            write_candidate(node_idx[4:0], cand_idx[5:0], 32'h0000_0000, 32'hFFFF_FFFF);
        end

        for (inactive_node = 8; inactive_node < 32; inactive_node = inactive_node + 1) begin
            write_candidate(inactive_node[4:0], 6'd0, 32'h0000_0000, 32'hFFFF_FFFF);
        end

        @(posedge clk);
        pio_reset <= 1'b1;
        repeat (8) @(posedge clk);
        pio_reset <= 1'b0;
        repeat (2) @(posedge clk);

        start <= 1'b1;
        while (!done && cycle < 10000) begin
            @(posedge clk);
            cycle = cycle + 1;
        end

        if (!done) begin
            $display("FAIL: parallel system timed out done_bits=%b", dut.chain_done);
            $finish;
        end

        start <= 1'b0;
        avalon_read(12'h800, order_word0);
        avalon_read(12'h801, order_word1);

        expected_best_score = dut.gen_chains[0].chain_score;
        expected_best_order = dut.gen_chains[0].chain_order;
        if (dut.gen_chains[1].chain_score > expected_best_score) begin
            expected_best_score = dut.gen_chains[1].chain_score;
            expected_best_order = dut.gen_chains[1].chain_order;
        end

        $display("FINAL chain0=%h chain1=%h selected=%h order=%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d words=%h,%h",
            dut.gen_chains[0].chain_score,
            dut.gen_chains[1].chain_score,
            best_score,
            order_word0[4:0],
            order_word0[9:5],
            order_word0[14:10],
            order_word0[19:15],
            order_word1[4:0],
            order_word1[9:5],
            order_word1[14:10],
            order_word1[19:15],
            order_word0,
            order_word1);

        if (best_score !== expected_best_score ||
            order_word0[4:0]   !== expected_best_order[4:0] ||
            order_word0[9:5]   !== expected_best_order[9:5] ||
            order_word0[14:10] !== expected_best_order[14:10] ||
            order_word0[19:15] !== expected_best_order[19:15] ||
            order_word1[4:0]   !== expected_best_order[24:20] ||
            order_word1[9:5]   !== expected_best_order[29:25] ||
            order_word1[14:10] !== expected_best_order[34:30] ||
            order_word1[19:15] !== expected_best_order[39:35]) begin
            $display("FAIL: parallel selected best score/order does not match best chain");
            $finish;
        end

        if ($signed(best_score) <= 0) begin
            $display("FAIL: parallel best score did not improve on deterministic table");
            $finish;
        end

        $display("PASS: parallel system returns best score/order across independent chains");
        $finish;
    end
endmodule
