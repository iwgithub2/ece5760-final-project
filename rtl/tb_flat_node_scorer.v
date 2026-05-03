`timescale 1ns / 1ps

module tb_flat_node_scorer;
    reg clk;
    reg rst_n;
    reg start;
    reg [31:0] allowed_parents;
    wire done;
    wire signed [31:0] final_score;
    wire [9:0] mem_addr;
    reg signed [31:0] mem_local_score;
    reg [31:0] mem_parent_mask;

    reg signed [31:0] scores [0:63];
    reg [31:0] masks [0:63];

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
        mem_local_score <= scores[mem_addr[5:0]];
        mem_parent_mask <= masks[mem_addr[5:0]];
    end

    initial begin
        $dumpfile("tb_flat_node_scorer.vcd");
        $dumpvars(0, tb_flat_node_scorer);

        rst_n = 1'b0;
        start = 1'b0;
        allowed_parents = 32'h0000_0001;
        mem_local_score = 32'd0;
        mem_parent_mask = 32'hFFFF_FFFF;

        scores[0] = 32'h0005_0000; masks[0] = 32'h0000_0000;
        scores[1] = 32'h0005_0000; masks[1] = 32'h0000_0001;
        scores[2] = 32'h0000_0000; masks[2] = 32'hFFFF_FFFF;

        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (2) @(posedge clk);

        start <= 1'b1;
        wait (done);
        @(negedge clk);

        $display("final_score=%h expected_around=0005b172", final_score);
        if (final_score < 32'h0005_A000 || final_score > 32'h0005_C000) begin
            $display("FAIL: flattened node_scorer log_add timing/result mismatch");
            $finish;
        end

        start <= 1'b0;
        repeat (4) @(posedge clk);
        $display("PASS: flattened node_scorer log_add result is in expected range");
        $finish;
    end
endmodule
