`timescale 1ns / 1ps

module tb_mcmc_system_order_update;
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
    localparam [3:0] S_DECIDE = 4'd9;

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

    integer cycle;
    integer inactive_node;
    integer node_idx;
    integer parent_idx;
    integer cand_idx;
    integer check_idx;
    integer best_updates;
    reg [31:0] order_word0;
    reg [31:0] order_word1;
    integer run_idx;

    initial begin
        $dumpfile("tb_mcmc_system_order_update.vcd");
        $dumpvars(0, tb_mcmc_system_order_update);

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
        order_word0 = 32'd0;
        order_word1 = 32'd0;

        repeat (4) @(posedge clk);
        reset_n = 1'b1;
        repeat (2) @(posedge clk);

        // mcmc_test.c contract: 8 active nodes, node_idx_mask = NUM_NODES - 1.
        // Initial order 0..7 scores 0. Any distinct swap that moves a higher
        // numbered node before a lower numbered node enables a +1.0 local score.
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

        // Inactive hardware scorers still need sentinels because the controller
        // waits for all 32 done bits.
        for (inactive_node = 8; inactive_node < 32; inactive_node = inactive_node + 1) begin
            write_candidate(inactive_node[4:0], 6'd0, 32'h0000_0000, 32'hFFFF_FFFF);
        end

        for (run_idx = 0; run_idx < 2; run_idx = run_idx + 1) begin
            if (run_idx == 1) begin
                start <= 1'b0;
                @(posedge clk);
                pio_reset <= 1'b1;
                repeat (8) @(posedge clk);
                pio_reset <= 1'b0;
                repeat (2) @(posedge clk);
            end

            cycle = 0;
            best_updates = 0;
            @(posedge clk);
            start <= 1'b1;

            while (!done && cycle < 5000) begin
                @(posedge clk);
                cycle = cycle + 1;
                if (dut.core.state == S_DECIDE) begin
                    if (dut.core.saved_rand_i >= active_nodes[4:0] ||
                        dut.core.saved_rand_j >= active_nodes[4:0]) begin
                        $display("FAIL: run %0d generated out-of-range proposal indices ri=%0d rj=%0d",
                            run_idx, dut.core.saved_rand_i, dut.core.saved_rand_j);
                        $finish;
                    end

                    for (check_idx = 0; check_idx < 8; check_idx = check_idx + 1) begin
                        if (dut.core.current_pos[dut.core.current_order[check_idx]] !== check_idx[4:0]) begin
                            $display("FAIL: run %0d current_order/current_pos inverse mismatch at pos %0d",
                                run_idx, check_idx);
                            $finish;
                        end
                        if (dut.core.proposed_pos[dut.core.proposed_order[check_idx]] !== check_idx[4:0]) begin
                            $display("FAIL: run %0d proposed_order/proposed_pos inverse mismatch at pos %0d",
                                run_idx, check_idx);
                            $finish;
                        end
                    end

                    if (dut.core.proposed_score > dut.core.best_score) begin
                        best_updates = best_updates + 1;
                    end
                end

                if (dut.core.state == S_DECIDE &&
                    (dut.core.iter_count < 4 || dut.core.proposed_score > dut.core.best_score)) begin
                    $display("RUN%0d DECIDE iter=%0d ri=%0d rj=%0d cur=%h prop=%h diff=%h accept=%0d best=%h cur_order=%0d,%0d,%0d prop_order=%0d,%0d,%0d best_order=%0d,%0d,%0d",
                        run_idx,
                        dut.core.iter_count,
                        dut.core.saved_rand_i,
                        dut.core.saved_rand_j,
                        dut.core.current_score,
                        dut.core.proposed_score,
                        dut.core.score_diff,
                        dut.core.accept_move,
                        dut.core.best_score,
                        dut.core.current_order[0],
                        dut.core.current_order[1],
                        dut.core.current_order[2],
                        dut.core.proposed_order[0],
                        dut.core.proposed_order[1],
                        dut.core.proposed_order[2],
                        dut.core.best_order_internal[0],
                        dut.core.best_order_internal[1],
                        dut.core.best_order_internal[2]);
                end
            end

            if (!done) begin
                $display("FAIL: run %0d simulation timed out", run_idx);
                $finish;
            end

            start <= 1'b0;
            avalon_read(12'h800, order_word0);
            avalon_read(12'h801, order_word1);
            $display("RUN%0d FINAL best_score=%h internal_order=%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d readback_words=%h,%h readback_order=%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",
                run_idx,
                best_score,
                dut.core.best_order_internal[0],
                dut.core.best_order_internal[1],
                dut.core.best_order_internal[2],
                dut.core.best_order_internal[3],
                dut.core.best_order_internal[4],
                dut.core.best_order_internal[5],
                dut.core.best_order_internal[6],
                dut.core.best_order_internal[7],
                order_word0,
                order_word1,
                order_word0[4:0],
                order_word0[9:5],
                order_word0[14:10],
                order_word0[19:15],
                order_word1[4:0],
                order_word1[9:5],
                order_word1[14:10],
                order_word1[19:15]);

            if (best_score <= 32'h0000_0000 ||
                best_updates == 0 ||
                order_word0[4:0]   !== dut.core.best_order_internal[0] ||
                order_word0[9:5]   !== dut.core.best_order_internal[1] ||
                order_word0[14:10] !== dut.core.best_order_internal[2] ||
                order_word0[19:15] !== dut.core.best_order_internal[3] ||
                order_word1[4:0]   !== dut.core.best_order_internal[4] ||
                order_word1[9:5]   !== dut.core.best_order_internal[5] ||
                order_word1[14:10] !== dut.core.best_order_internal[6] ||
                order_word1[19:15] !== dut.core.best_order_internal[7]) begin
                $display("FAIL: run %0d best score did not improve, best update was not observed, or readback is mispacked", run_idx);
                $finish;
            end
        end

        $display("PASS: two runs completed, best order updated, readback matches");
        $finish;
    end
endmodule
