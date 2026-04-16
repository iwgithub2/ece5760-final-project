`timescale 1ns / 1ps

module lfsr_32bit (
    input clk,
    input rst_n,        // Active low reset
    input en,           // Enable shifting
    input [31:0] seed,  // Initial seed value
    input load_seed,    // Signal to load the seed
    output reg [31:0] rand_out
);

    // Polynomial: x^32 + x^22 + x^2 + x^1 + 1
    // Represents the tap positions for maximal length sequence
    localparam POLY = 32'h80200003; 

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rand_out <= 32'hDEADBEEF; // Default non-zero seed
        end else if (load_seed) begin
            rand_out <= seed;
        end else if (en) begin
            if (rand_out[0] == 1'b1) begin
                rand_out <= (rand_out >> 1) ^ POLY;
            end else begin
                rand_out <= rand_out >> 1;
            end
        end
    end
endmodule

// ==========================================
// M10K BRAM Inference Module
// ==========================================
module log_add_rom (
    input wire clk,
    input wire [9:0] addr,
    output reg [31:0] data_out
);
    // Declare 1024 words of 32-bit memory
    reg [31:0] mem [0:1023];

    // Load the hex file. Both iverilog and Quartus support this.
    initial begin
        $readmemh("log_lut.hex", mem);
    end

    // Synchronous read (CRITICAL for Quartus M10K block inference)
    always @(posedge clk) begin
        data_out <= mem[addr];
    end
endmodule

// ==========================================
// Pipelined Log-Add Arithmetic Module
// ==========================================
module log_add (
    input wire clk,
    input wire rst_n,
    input wire signed [31:0] a, // Q16.16 format
    input wire signed [31:0] b, // Q16.16 format
    output reg signed [31:0] result
);

    // --- Stage 1 Registers ---
    reg signed [31:0] max_val_s1;
    reg [31:0] abs_diff_s1;

    // --- Stage 2 Registers ---
    reg signed [31:0] max_val_s2;
    wire [31:0] lut_val_s2;
    reg force_zero_s2; // Flag if diff is too large for LUT

    // Instantiate the ROM
    // The ROM has a 1-cycle latency. Address goes in at S1, data available at S2.
    log_add_rom lut_inst (
        .clk(clk),
        .addr(abs_diff_s1[19:10]), // Map diff to 10-bit address (diff / step_size)
        .data_out(lut_val_s2)
    );

    // PIPELINE LOGIC
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            max_val_s1  <= 0;
            abs_diff_s1 <= 0;
            max_val_s2  <= 0;
            force_zero_s2 <= 0;
            result      <= 0;
        end else begin
            // ==========================================
            // STAGE 1: Calculate Diff, Max, and Abs
            // ==========================================
            if (a > b) begin
                max_val_s1  <= a;
                abs_diff_s1 <= a - b;
            end else begin
                max_val_s1  <= b;
                abs_diff_s1 <= b - a;
            end

            // ==========================================
            // STAGE 2: Memory Read & Delay Matching
            // ==========================================
            // Pass max_val forward to align with the ROM read latency
            max_val_s2 <= max_val_s1;
            
            // If the absolute difference is >= 16.0 (0x00100000 in Q16.16)
            // The log(1+e^-x) is effectively 0. Clamp it to avoid memory overflow.
            if (abs_diff_s1 >= 32'h0010_0000) begin
                force_zero_s2 <= 1'b1;
            end else begin
                force_zero_s2 <= 1'b0;
            end

            // ==========================================
            // STAGE 3: Final Addition
            // ==========================================
            if (force_zero_s2) begin
                result <= max_val_s2; // Add 0
            end else begin
                result <= max_val_s2 + lut_val_s2;
            end
        end
    end
endmodule
