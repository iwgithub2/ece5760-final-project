`timescale 1ns / 1ps

module tb_mcmc_system_asia;
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

    reg [31:0] scores [0:511];
    reg [31:0] masks [0:511];
    reg [31:0] order_word0;
    reg [31:0] order_word1;
    integer node;
    integer cand;
    integer cycle;
    localparam signed [31:0] ASIA_INITIAL_SCORE = 32'hfaff_42e1;

    mcmc_system dut (
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
            avs_address = addr;
            avs_writedata = data;
            avs_write = 1'b1;
            @(posedge clk);
            avs_write = 1'b0;
            avs_address = 12'd0;
            avs_writedata = 32'd0;
        end
    endtask

    task write_candidate;
        input [4:0] write_node;
        input [5:0] write_cand;
        input [31:0] score;
        input [31:0] mask;
        begin
            avalon_write({write_node, write_cand, 1'b0}, score);
            avalon_write({write_node, write_cand, 1'b1}, mask);
        end
    endtask

    task avalon_read;
        input [11:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            avs_address = addr;
            avs_read = 1'b1;
            @(posedge clk);
            avs_read = 1'b0;
            @(negedge clk);
            data = avs_readdata;
            avs_address = 12'd0;
        end
    endtask

    initial begin
        $readmemh("asia_scores.hex", scores);
        $readmemh("asia_masks.hex", masks);

        reset_n = 1'b0;
        avs_address = 12'd0;
        avs_write = 1'b0;
        avs_writedata = 32'd0;
        avs_read = 1'b0;
        start = 1'b0;
        pio_reset = 1'b0;
        seed = 32'hDEAD_BEEF;
        iterations = 32'd1000;
        active_nodes = 32'd8;
        node_idx_mask = 32'd7;
        order_word0 = 32'd0;
        order_word1 = 32'd0;
        cycle = 0;

        repeat (4) @(posedge clk);
        reset_n = 1'b1;
        repeat (4) @(posedge clk);

        for (node = 0; node < 8; node = node + 1) begin
            for (cand = 0; cand < 64; cand = cand + 1) begin
                write_candidate(node[4:0], cand[5:0],
                    scores[{node[2:0], 6'd0} + cand],
                    masks[{node[2:0], 6'd0} + cand]);
                if (masks[{node[2:0], 6'd0} + cand] == 32'hFFFF_FFFF) cand = 64;
            end
        end

        for (node = 8; node < 32; node = node + 1) begin
            write_candidate(node[4:0], 6'd0, 32'h0000_0000, 32'hFFFF_FFFF);
        end

        @(posedge clk);
        pio_reset = 1'b1;
        repeat (8) @(posedge clk);
        pio_reset = 1'b0;
        repeat (4) @(posedge clk);

        start = 1'b1;
        while (!done && cycle < 200000) begin
            @(posedge clk);
            cycle = cycle + 1;
        end

        if (!done) begin
            $display("FAIL: timed out cycle=%0d state=%0d iter=%0d score_start=%0d done_bits=%h mem_addr0=%0d scorer0_state=%0d mask0=%h score0=%h ram0_mask29=%h",
                cycle,
                dut.core.state,
                dut.core.iter_count,
                dut.core.score_start,
                dut.core.score_done,
                dut.core.gen_scorers[0].local_ram_addr,
                dut.core.gen_scorers[0].scorer_inst.state,
                dut.core.gen_scorers[0].scorer_inst.mem_parent_mask,
                dut.core.gen_scorers[0].scorer_inst.mem_local_score,
                dut.gen_rams[0].ram_inst.ram_upper[29]);
            $finish;
        end

        start = 1'b0;
        avalon_read(12'h800, order_word0);
        avalon_read(12'h801, order_word1);
        $display("FINAL cycles=%0d best_score=%h order=%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d words=%h,%h",
            cycle,
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

        if ($signed(best_score) <= ASIA_INITIAL_SCORE) begin
            $display("FAIL: Asia top-level best score did not improve over initial score");
            $finish;
        end

        if (order_word0[4:0]   !== dut.core.best_order_internal[0] ||
            order_word0[9:5]   !== dut.core.best_order_internal[1] ||
            order_word0[14:10] !== dut.core.best_order_internal[2] ||
            order_word0[19:15] !== dut.core.best_order_internal[3] ||
            order_word1[4:0]   !== dut.core.best_order_internal[4] ||
            order_word1[9:5]   !== dut.core.best_order_internal[5] ||
            order_word1[14:10] !== dut.core.best_order_internal[6] ||
            order_word1[19:15] !== dut.core.best_order_internal[7]) begin
            $display("FAIL: Asia top-level readback does not match internal best order");
            $finish;
        end

        $display("PASS: Asia top-level MCMC improved score and readback matches internal order");
        $finish;
    end
endmodule
