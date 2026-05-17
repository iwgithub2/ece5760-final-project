`timescale 1ns / 1ps
`include "mcmc_controller.v"

// ==========================================
// MOCK NODE SCORER
// Responds 2 clock cycles after 'start' is asserted.
// Generates a fake score based on the allowed_parents mask 
// so that different order proposals yield different scores.
// ==========================================
module mcmc_node_scoring #(
    parameter N_NODES = 32,
    parameter ADDR_W = 10,
    parameter DATA_W = 32
)(
    input  wire clk,
    input  wire rst_n,
    input  wire start,
    input  wire [N_NODES-1:0] allowed_parents,
    input  wire [9:0] num_candidates,
    output reg  done,
    output reg signed [DATA_W-1:0] final_score
);
    reg [1:0] delay_cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done <= 1'b0;
            final_score <= 0;
            delay_cnt <= 0;
        end else begin
            if (start) begin
                done <= 1'b0;
                delay_cnt <= 1; // Begin 2-cycle delay
            end else if (delay_cnt > 0) begin
                if (delay_cnt == 2) begin
                    done <= 1'b1;
                    // Use the parent mask as a dummy score (shifted into Q16.16)
                    // This guarantees that changing the order changes the score
                    final_score <= {16'h0000, allowed_parents, {(16-N_NODES){1'b0}}};
                    delay_cnt <= 0;
                end else begin
                    delay_cnt <= delay_cnt + 1;
                end
            end else begin
                done <= 1'b0;
            end
        end
    end
endmodule


// ==========================================
// MCMC CONTROLLER TESTBENCH
// ==========================================
module tb_mcmc_controller;

    // Testbench signals
    reg clk;
    reg rst_n;
    reg start;
    reg [31:0] seed;
    
    wire done;
    wire signed [31:0] best_score;

    // Instantiate DUT with scaled-down parameters for simulation
    // 4 Nodes, 10 Iterations
    mcmc_controller #(
        .N_NODES(4),
        .ADDR_W(10),
        .DATA_W(32),
        .ITERATIONS(10) 
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .done(done),
        .best_score(best_score),
        .seed(seed)
    );

    // Clock Generation (100MHz)
    initial begin
        clk = 0;
        forever #5 clk = ~clk; 
    end

    // Test Sequence
    initial begin
        $dumpfile("tb_mcmc_controller.vcd");
        $dumpvars(0, tb_mcmc_controller);

        // Initialize signals
        rst_n = 0;
        start = 0;
        seed = 32'hABCD_1234; // Arbitrary seed

        #15;
        rst_n = 1;
        #10;

        // Kick off the MCMC Process
        $display("Starting MCMC Controller (4 Nodes, 10 Iterations)...");
        @(posedge clk);
        start = 1;
        @(posedge clk);
        start = 0;

        // Wait for the controller to finish
        @(posedge done);
        
        $display("MCMC Finished!");
        $display("Final Best Score: %08x", best_score);
        
        // Let it idle for a moment before ending simulation
        repeat(5) @(posedge clk);
        $finish;
    end

endmodule