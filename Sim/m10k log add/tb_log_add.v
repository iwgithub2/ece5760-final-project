`timescale 1ns / 1ps
`include "components.v"

module tb_log_add;

    // Testbench signals
    reg clk;
    reg rst_n;
    reg signed [31:0] a;
    reg signed [31:0] b;
    wire signed [31:0] result;

    // Instantiate the Device Under Test (DUT)
    log_add dut (
        .clk(clk),
        .rst_n(rst_n),
        .a(a),
        .b(b),
        .result(result)
    );

    // 10ns Clock Generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk; 
    end

    // Test Sequence
    initial begin
        // Generate a VCD file to view waveforms in GTKWave
        $dumpfile("tb_log_add.vcd");
        $dumpvars(0, tb_log_add);

        // Initialize signals
        rst_n = 0;
        a = 0;
        b = 0;
        
        #15;          // Hold reset for a bit
        rst_n = 1;    // Release reset

        // ----------------------------------------------------
        // Test Case 1: a = 0.0, b = 0.0
        // ----------------------------------------------------
        // Diff = 0. Max = 0. 
        // LUT should add log(1+e^0) = log(2) ≈ 0.69314
        // In Q16.16: 0.69314 * 65536 ≈ 45425 = 0x0000B171
        @(posedge clk);
        a = 32'h0000_0000; 
        b = 32'h0000_0000;
        
        // Wait 3 clock cycles for the pipeline to flush
        repeat(3) @(posedge clk);
        #1; // Slight delay to read after the edge
        $display("Test 1 (A=0, B=0): Result = %08x (Expected ~0000B171)", result);

        // ----------------------------------------------------
        // Test Case 2: a = 20.0, b = 0.0
        // ----------------------------------------------------
        // Diff = 20.0 (Greater than MAX_X of 16.0). Max = 20.0.
        // The force_zero_s2 flag should trigger.
        // Result should exactly equal max(a,b) = 20.0
        @(posedge clk);
        a = 32'h0014_0000; // 20.0 in Q16.16
        b = 32'h0000_0000;
        
        repeat(3) @(posedge clk);
        #1;
        $display("Test 2 (A=20.0, B=0): Result = %08x (Expected 00140000)", result);

        // ----------------------------------------------------
        // Test Case 3: a = -2.0, b = -2.5 (Negative Numbers)
        // ----------------------------------------------------
        // A = 0xFFFE_0000, B = 0xFFFD_8000
        // Max = -2.0. Diff = 0.5. 
        // log(1+e^-0.5) ≈ 0.47407. In Q16.16 ≈ 31068 = 0x0000795C
        // Expected Result: -2.0 + 0.47407 = -1.5259
        // -1.5259 in Q16.16 ≈ 0xFFFE0000 + 0x0000795C = 0xFFFE795C
        @(posedge clk);
        a = 32'hFFFE_0000; 
        b = 32'hFFFD_8000;
        
        repeat(3) @(posedge clk);
        #1;
        $display("Test 3 (A=-2.0, B=-2.5): Result = %08x (Expected ~FFFE795C)", result);

        #20;
        $display("log_add verification complete.");
        $finish;
    end

endmodule