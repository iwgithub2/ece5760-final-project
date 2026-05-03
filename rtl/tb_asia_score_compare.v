`timescale 1ns / 1ps

module tb_asia_score_compare;
    reg clk;
    reg rst_n;
    reg start;
    reg [31:0] allowed_parents;
    wire done;
    wire signed [31:0] final_score;
    wire [9:0] mem_addr;
    reg signed [31:0] mem_local_score;
    reg [31:0] mem_parent_mask;

    reg signed [31:0] scores [0:511];
    reg [31:0] masks [0:511];
    reg [4:0] selected_node;

    reg [4:0] order_initial [0:7];
    reg [4:0] order_reported [0:7];
    reg [4:0] order_exhaustive [0:7];
    reg signed [31:0] score_initial;
    reg signed [31:0] score_reported;
    reg signed [31:0] score_exhaustive;

    node_scorer dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .allowed_parents(allowed_parents),
        .done(done),
        .final_score(final_score),
        .mem_addr(mem_addr),
        .mem_local_score(mem_local_score),
        .mem_parent_mask(mem_parent_mask)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    always @(posedge clk) begin
        mem_local_score <= scores[{selected_node, 6'd0} + mem_addr[5:0]];
        mem_parent_mask <= masks[{selected_node, 6'd0} + mem_addr[5:0]];
    end

    function [31:0] allowed_mask;
        input integer pos;
        input integer which_order;
        integer i;
        begin
            allowed_mask = 32'd0;
            for (i = 0; i < pos; i = i + 1) begin
                if (which_order == 0)
                    allowed_mask = allowed_mask | (32'd1 << order_initial[i]);
                else if (which_order == 1)
                    allowed_mask = allowed_mask | (32'd1 << order_reported[i]);
                else
                    allowed_mask = allowed_mask | (32'd1 << order_exhaustive[i]);
            end
        end
    endfunction

    function [4:0] order_node;
        input integer pos;
        input integer which_order;
        begin
            if (which_order == 0)
                order_node = order_initial[pos];
            else if (which_order == 1)
                order_node = order_reported[pos];
            else
                order_node = order_exhaustive[pos];
        end
    endfunction

    task score_order;
        input integer which_order;
        output signed [31:0] total;
        integer pos;
        begin
            total = 32'sd0;
            for (pos = 0; pos < 8; pos = pos + 1) begin
                selected_node = order_node(pos, which_order);
                allowed_parents = allowed_mask(pos, which_order);
                repeat (2) @(posedge clk);
                start = 1'b1;
                wait (done);
                @(negedge clk);
                $display("ORDER%0d pos=%0d node=%0d allowed=%08x node_score=%h",
                    which_order, pos, selected_node, allowed_parents, final_score);
                total = total + final_score;
                start = 1'b0;
                wait (!done);
                repeat (2) @(posedge clk);
            end
        end
    endtask

    initial begin
        $dumpfile("tb_asia_score_compare.vcd");
        $dumpvars(0, tb_asia_score_compare);
        $readmemh("asia_scores.hex", scores);
        $readmemh("asia_masks.hex", masks);

        rst_n = 1'b0;
        start = 1'b0;
        allowed_parents = 32'd0;
        selected_node = 5'd0;
        mem_local_score = 32'sd0;
        mem_parent_mask = 32'hFFFF_FFFF;

        order_initial[0] = 5'd0; order_initial[1] = 5'd1;
        order_initial[2] = 5'd2; order_initial[3] = 5'd3;
        order_initial[4] = 5'd4; order_initial[5] = 5'd5;
        order_initial[6] = 5'd6; order_initial[7] = 5'd7;

        order_reported[0] = 5'd2; order_reported[1] = 5'd3;
        order_reported[2] = 5'd5; order_reported[3] = 5'd1;
        order_reported[4] = 5'd7; order_reported[5] = 5'd6;
        order_reported[6] = 5'd4; order_reported[7] = 5'd0;

        order_exhaustive[0] = 5'd2; order_exhaustive[1] = 5'd4;
        order_exhaustive[2] = 5'd7; order_exhaustive[3] = 5'd3;
        order_exhaustive[4] = 5'd0; order_exhaustive[5] = 5'd1;
        order_exhaustive[6] = 5'd5; order_exhaustive[7] = 5'd6;

        repeat (4) @(posedge clk);
        rst_n = 1'b1;
        repeat (4) @(posedge clk);

        score_order(0, score_initial);
        score_order(1, score_reported);
        score_order(2, score_exhaustive);

        $display("SCORE initial=%h (%0d)", score_initial, score_initial);
        $display("SCORE reported=%h (%0d)", score_reported, score_reported);
        $display("SCORE exhaustive=%h (%0d)", score_exhaustive, score_exhaustive);

        if (score_reported >= score_initial) begin
            $display("FAIL: hardware scorer ranks reported order >= initial order");
            $finish;
        end
        if (score_exhaustive <= score_initial) begin
            $display("FAIL: hardware scorer does not rank exhaustive C best above initial");
            $finish;
        end

        $display("PASS: hardware scorer ranking matches C ordering sanity");
        $finish;
    end
endmodule
