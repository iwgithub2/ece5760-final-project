`timescale 1ns / 1ps

module tb_score_accumulator;
    reg clk;
    reg rst_n;
    reg start;
    reg [5:0] active_count;
    reg [(8*32)-1:0] scores_packed;
    wire done;
    wire signed [31:0] sum;

    integer i;

    score_accumulator #(
        .N_NODES(8),
        .DATA_W(32)
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .active_count(active_count),
        .scores_packed(scores_packed),
        .done(done),
        .sum(sum)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task set_score;
        input integer idx;
        input signed [31:0] value;
        begin
            scores_packed[(idx*32)+:32] = value;
        end
    endtask

    task run_sum;
        input [5:0] count;
        input signed [31:0] expected;
        integer cycles;
        begin
            active_count = count;
            cycles = 0;
            @(posedge clk);
            start = 1'b1;
            @(posedge clk);
            start = 1'b0;
            while (!done && cycles < 64) begin
                @(posedge clk);
                cycles = cycles + 1;
            end
            if (!done) begin
                $display("FAIL: accumulator timed out for count=%0d", count);
                $finish;
            end
            if (sum !== expected) begin
                $display("FAIL: count=%0d sum=%h expected=%h", count, sum, expected);
                $finish;
            end
            @(posedge clk);
            if (done) begin
                $display("FAIL: done did not drop after completed sum");
                $finish;
            end
        end
    endtask

    initial begin
        $dumpfile("tb_score_accumulator.vcd");
        $dumpvars(0, tb_score_accumulator);

        rst_n = 1'b0;
        start = 1'b0;
        active_count = 6'd0;
        scores_packed = {256{1'b0}};

        repeat (4) @(posedge clk);
        rst_n = 1'b1;

        set_score(0, 32'sh0001_0000);
        set_score(1, -32'sh0002_0000);
        set_score(2, 32'sh0003_0000);
        set_score(3, 32'sh0004_0000);
        set_score(4, -32'sh0001_8000);
        set_score(5, 32'sh0000_4000);
        set_score(6, 32'sh0000_2000);
        set_score(7, -32'sh0000_1000);

        run_sum(6'd4, 32'sh0006_0000);
        run_sum(6'd8, 32'sh0004_d000);

        for (i = 0; i < 8; i = i + 1) begin
            set_score(i, 32'sh0000_0100 * i);
        end
        run_sum(6'd8, 32'sh0000_1c00);

        $display("PASS: score_accumulator sums active signed scores and restarts");
        $finish;
    end
endmodule
