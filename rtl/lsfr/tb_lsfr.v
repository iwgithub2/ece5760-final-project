`include "components.v"
`timescale 1ns / 1ps // Defines time units for simulation

module tb_lfsr_32bit();

    parameter integer NUM_SAMPLES = 65536;
    parameter [31:0] INITIAL_SEED = 32'h12345678;
    parameter DUMP_VCD = 0;
    parameter SAMPLE_FILE = "lfsr_samples.csv";

    reg clk;
    reg rst_n;
    reg en;
    reg [31:0] seed;
    reg load_seed;
    wire [31:0] rand_out;

    integer sample_fd;
    integer sample_idx;
    integer bit_idx;

    lfsr_32bit uut (
        .clk(clk),
        .rst_n(rst_n),
        .en(en),
        .seed(seed),
        .load_seed(load_seed),
        .rand_out(rand_out)
    );

    always #10 clk = ~clk;

    initial begin
        clk = 0;
        rst_n = 0;
        en = 0;
        load_seed = 0;
        seed = INITIAL_SEED;

        if (DUMP_VCD) begin
            $dumpfile("tb_lsfr.vcd");
            $dumpvars(0, tb_lfsr_32bit);
        end

        sample_fd = $fopen(SAMPLE_FILE, "w");
        if (sample_fd == 0) begin
            $display("ERROR: could not open %s", SAMPLE_FILE);
            $finish;
        end

        $fwrite(sample_fd, "cycle,value_hex");
        for (bit_idx = 0; bit_idx < 32; bit_idx = bit_idx + 1) begin
            $fwrite(sample_fd, ",bit%0d", bit_idx);
        end
        $fwrite(sample_fd, "\n");

        repeat (3) @(negedge clk);
        rst_n = 1;

        @(negedge clk);
        load_seed = 1;

        @(negedge clk);
        load_seed = 0;
        en = 1;

        for (sample_idx = 0; sample_idx < NUM_SAMPLES; sample_idx = sample_idx + 1) begin
            @(posedge clk);
            #1; // Sample after the DUT nonblocking assignment updates rand_out.
            $fwrite(sample_fd, "%0d,0x%08h", sample_idx, rand_out);
            for (bit_idx = 0; bit_idx < 32; bit_idx = bit_idx + 1) begin
                $fwrite(sample_fd, ",%0d", rand_out[bit_idx]);
            end
            $fwrite(sample_fd, "\n");
        end

        en = 0;
        $fclose(sample_fd);
        $display("Wrote %0d LFSR samples to %s", NUM_SAMPLES, SAMPLE_FILE);
        $finish;
    end

endmodule
