`timescale 1ns / 1ps
`include "components.v"

module node_scorer (
    input wire clk,
    input wire rst_n,
    
    // Control signals from MCMC Controller
    input wire start,
    input wire [31:0] allowed_parents, // Bitmask of nodes that appear BEFORE this node in the order
    input wire [9:0] start_addr,       // Base address in M10K for this node's candidates
    input wire [9:0] num_cands,        // How many candidates to check for this node
    output reg done,
    output reg signed [31:0] final_score, // Q16.16 format
    
    // Memory Interface (To M10K Blocks) 
    // Assuming 1-cycle read latency (Standard for Quartus M10K)
    output reg [9:0] mem_addr,
    input wire signed [31:0] mem_local_score, // Q16.16 format
    input wire [31:0] mem_parent_mask
);

    // smallest possible value in 16.16
    localparam signed [31:0] MIN_SCORE = 32'h8000_0000;

    // FSM States
    localparam IDLE          = 3'd0;
    localparam FETCH_ADDR    = 3'd1;
    localparam FETCH_DATA    = 3'd2;
    localparam WAIT_LOGADD_1 = 3'd3;
    localparam WAIT_LOGADD_2 = 3'd4;
    localparam WAIT_LOGADD_3 = 3'd5;
    localparam DONE          = 3'd6;

    reg [2:0] state;
    reg [9:0] cands_checked;
    reg first_valid_found;
    
    // Log-Add Signals 
    reg signed [31:0] accum_score;
    wire signed [31:0] log_add_out;
    
    // Pipelined Log-Add module (3 cycles latency)
    log_add log_add_inst (
        .clk(clk),
        .rst_n(rst_n),
        .a(accum_score),         
        .b(mem_local_score),
        .result(log_add_out)
    );

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            done <= 1'b0;
            final_score <= MIN_SCORE;
            mem_addr <= 0;
            cands_checked <= 0;
            accum_score <= MIN_SCORE;
            first_valid_found <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        if (num_cands == 0) begin
                            // Edge case: Node has no candidates (shouldn't happen, but safe)
                            final_score <= MIN_SCORE;
                            done <= 1'b1;
                        end else begin
                            mem_addr <= start_addr;
                            cands_checked <= 0;
                            accum_score <= MIN_SCORE;
                            first_valid_found <= 1'b0;
                            state <= FETCH_ADDR;
                        end
                    end
                end

                FETCH_ADDR: begin
                    // Address is sent to M10K this cycle. Data will arrive next cycle.
                    state <= FETCH_DATA;
                end

                FETCH_DATA: begin
                    // Figure 3 Enable Logic: Check compatibility
                    // C-code equivalent: check_compatibility(parent_bitmask, order, i)
                    if ((mem_parent_mask & allowed_parents) == mem_parent_mask) begin
                        // Candidate is VALID
                        if (!first_valid_found) begin
                            // First valid candidate bypasses log_add to avoid subtracting from -INF
                            accum_score <= mem_local_score;
                            first_valid_found <= 1'b1;
                            
                            // Check if we are done
                            if (cands_checked + 1 == num_cands) begin
                                state <= DONE;
                            end else begin
                                mem_addr <= mem_addr + 1;
                                cands_checked <= cands_checked + 1;
                                state <= FETCH_ADDR;
                            end
                        end else begin
                            // Not the first valid candidate, feed into pipelined log_add
                            // Data is currently on log_add_inst inputs. Wait 3 cycles.
                            state <= WAIT_LOGADD_1;
                        end
                    end else begin
                        // Candidate is INVALID, move to next
                        if (cands_checked + 1 == num_cands) begin
                            state <= DONE;
                        end else begin
                            mem_addr <= mem_addr + 1;
                            cands_checked <= cands_checked + 1;
                            state <= FETCH_ADDR;
                        end
                    end
                end

                // Pipeline Delay States for Log-Add
                WAIT_LOGADD_1: state <= WAIT_LOGADD_2;
                WAIT_LOGADD_2: state <= WAIT_LOGADD_3;
                WAIT_LOGADD_3: begin
                    // 3 cycles have passed, log_add_out is valid
                    accum_score <= log_add_out;
                    
                    if (cands_checked + 1 == num_cands) begin
                        state <= DONE;
                    end else begin
                        mem_addr <= mem_addr + 1;
                        cands_checked <= cands_checked + 1;
                        state <= FETCH_ADDR;
                    end
                end

                DONE: begin
                    final_score <= accum_score;
                    done <= 1'b1;
                    if (!start) begin // Wait for controller to de-assert start
                        state <= IDLE;
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end
endmodule