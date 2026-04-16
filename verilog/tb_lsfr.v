`include "components.v"
`timescale 1ns / 1ps // Defines time units for simulation

module tb_lfsr_32bit();

    // 1. Declare testbench signals (regs for inputs, wires for outputs)
    reg clk;
    reg rst_n;
    reg en;
    reg [31:0] seed;
    reg load_seed;
    wire [31:0] rand_out;

    // 2. Instantiate the module under test (DUT - Device Under Test)
    lfsr_32bit uut (
        .clk(clk),
        .rst_n(rst_n),
        .en(en),
        .seed(seed),
        .load_seed(load_seed),
        .rand_out(rand_out)
    );

    // 3. Generate a clock (50MHz = 20ns period -> toggles every 10ns)
    always #10 clk = ~clk;

    // 4. Test Sequence
    initial begin
        // Initialize inputs
        clk = 0;
        rst_n = 0; // Assert reset (active low)
        en = 0;
        load_seed = 0;
        seed = 32'h12345678; // An arbitrary seed

        $dumpfile("tb_lsfr.vcd"); // Give the file a specific name
        $dumpvars(0, tb_lfsr_32bit);    // Dump everything in the module 'testbench'

        // Wait a bit, then release reset
        #50;
        rst_n = 1;

        // Load our custom seed
        #20;
        load_seed = 1;
        #20;
        load_seed = 0; // Stop loading

        // Enable shifting to generate numbers
        #20;
        en = 1;

        // Let it run for 20 clock cycles to watch the numbers change
        #4000;

        // Pause it
        en = 0;
        #40;

        // End simulation
        $finish;
    end

    // 5. Print the values to the console automatically
    always @(posedge clk) begin
        if (en) begin
            $display("Time: %0t | Random Value: %h", $time, rand_out);
        end
    end

endmodule