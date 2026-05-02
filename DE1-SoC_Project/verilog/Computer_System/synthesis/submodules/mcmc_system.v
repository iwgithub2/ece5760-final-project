// ============================================================================
// Single-Core MCMC System Wrapper (Parameterizable)
// ============================================================================
module mcmc_system #(
    parameter N_NODES = 32 // Max number of nodes
)(
    input  wire        clk,
    input  wire        reset_n,

    // Avalon-MM Slave Interface
    input  wire [11:0] avs_address,   // 12 bits to address 4096 32-bit words
    input  wire        avs_write,
    input  wire [31:0] avs_writedata,
    input  wire        avs_read, 
    output reg  [31:0] avs_readdata,

    // Conduits
    input  wire        start,
	 input  wire		  pio_reset,
    input  wire [31:0] seed,
    input  wire [31:0] iterations,  
    input  wire [31:0] active_nodes, 
    input  wire [31:0] node_idx_mask,
    output wire        done,
    output wire [31:0] best_score,
	 output reg  [31:0] clk_count
);
	 wire global_reset_n;
	 assign global_reset_n = reset_n & (~pio_reset);
	 
    // Clock Counter Logic
    always @(posedge clk or negedge global_reset_n) begin
        if (!global_reset_n) begin
            clk_count <= 32'd0;
        end else begin
            // Count up only while the engine is actively running
            if (start && !done) begin
                clk_count <= clk_count + 1;
            end
        end
    end

    wire [(N_NODES*10)-1:0] packed_addrs;
    wire [(N_NODES*64)-1:0] packed_datas;
    wire [(N_NODES*5)-1:0]  best_order_packed; // 32 nodes * 5 bits = 160 bits

    // Avalon Read Logic (Mapped to address 0x800 / 2048)
    always @(posedge clk) begin
        if (avs_read && avs_address[11] == 1'b1) begin
            // Pack four 5-bit nodes into a 32-bit word, padded with zeros
            avs_readdata <= { 12'd0, 
                              best_order_packed[ (avs_address[2:0]*20 + 15) +: 5 ],
                              best_order_packed[ (avs_address[2:0]*20 + 10) +: 5 ],
                              best_order_packed[ (avs_address[2:0]*20 + 5)  +: 5 ],
                              best_order_packed[ (avs_address[2:0]*20)      +: 5 ] };
        end
    end

    // Generate 32 RAM blocks
    genvar i;
    generate
        for (i = 0; i < N_NODES; i = i + 1) begin : gen_rams
            // Top 5 bits [11:7] select the Node (0 to 31)
            wire local_we = avs_write && (avs_address[11:7] == i);
            
            wire [9:0]  read_addr = packed_addrs[(i*10)+:10];
            wire [63:0] read_data;

            mcmc_node_ram ram_inst (
                .clk(clk),
                .we(local_we),
                .write_addr(avs_address[6:0]), // Pass [6:0] to handle the word split
                .write_data(avs_writedata),
                .read_addr(read_addr[5:0]),
                .read_data(read_data)
            );
            assign packed_datas[(i*64)+:64] = read_data;
        end
    endgenerate

    mcmc_controller #(.N_NODES(N_NODES)) core (
        .clk(clk), 
        .rst_n(global_reset_n), 
        .start(start), 
        .seed(seed),
        .iterations(iterations), 
        .active_nodes(active_nodes), 
        .node_idx_mask(node_idx_mask),
        .done(done), 
        .best_score(best_score), 
        .best_order_packed(best_order_packed),
        .mem_addrs_packed(packed_addrs), 
        .mem_datas_packed(packed_datas)
    );
endmodule

module mcmc_controller #(
    parameter N_NODES = 32,
    parameter ADDR_W = 10,
    parameter DATA_W = 32  
)(
    input  wire clk,
    input  wire rst_n,

    // Control Interface
    input  wire start,
    input  wire [31:0] seed,
    input  wire [31:0] iterations,
    input  wire [31:0] active_nodes,
    input  wire [31:0] node_idx_mask,
    output reg  done,
    output reg signed [DATA_W-1:0] best_score,
    output wire [(N_NODES*5)-1:0] best_order_packed,
    
    // Dynamic packed memory interfaces
    output wire [(N_NODES*10)-1:0] mem_addrs_packed,
    input  wire [(N_NODES*64)-1:0] mem_datas_packed
);

    // FSM States
    localparam S_IDLE            = 3'd0;
    localparam S_INIT_SCORE      = 3'd1; 
    localparam S_WAIT_INIT_SCORE = 3'd2;
    localparam S_PROPOSE         = 3'd3;
    localparam S_WAIT_SCORE      = 3'd4; 
    localparam S_DECIDE          = 3'd5;
    localparam S_DONE            = 3'd6;

    reg [2:0] state;
    reg [31:0] iter_count;

    // LFSR
    wire [31:0] lfsr_out;
    reg lfsr_en;
    reg load_seed;

    lfsr_32bit rng_inst (
        .clk(clk), .rst_n(rst_n), .en(lfsr_en),
        .seed(seed), .load_seed(load_seed), .rand_out(lfsr_out)
    );

    // Fast Bitmask Randomization
    reg [4:0] saved_rand_i;
    reg [4:0] saved_rand_j;
    reg [15:0] saved_rand_u;
    wire [4:0] rand_i = lfsr_out[4:0] & node_idx_mask[4:0];
    wire [4:0] rand_j = lfsr_out[9:5] & node_idx_mask[4:0];
    wire [15:0] rand_u = lfsr_out[31:16];

    // Order Matrix Management
    reg [4:0] current_order  [0:N_NODES-1];
    reg [4:0] proposed_order [0:N_NODES-1];
    reg [4:0] current_pos    [0:N_NODES-1];
    reg [4:0] proposed_pos   [0:N_NODES-1]; 

    reg signed [DATA_W-1:0] current_score;
    wire signed [DATA_W-1:0] proposed_score;

    wire [N_NODES-1:0] proposed_masks [0:N_NODES-1];
    integer i_node, j_pos;

    genvar n_row, n_col;
    generate
        for (n_row = 0; n_row < N_NODES; n_row = n_row + 1) begin : gen_rows
            for (n_col = 0; n_col < N_NODES; n_col = n_col + 1) begin : gen_cols
                assign proposed_masks[n_row][n_col] = (proposed_pos[n_col] < proposed_pos[n_row]) ? 1'b1 : 1'b0;
            end
        end
    endgenerate

    // Parallel Node Scorers
    reg score_start;
    wire [N_NODES-1:0] score_done;
    wire signed [DATA_W-1:0] node_scores [0:N_NODES-1];

    genvar n;
    generate
        for (n = 0; n < N_NODES; n = n + 1) begin : gen_scorers
            wire [63:0] local_ram_data = mem_datas_packed[(n*64)+:64];
            wire [9:0]  local_ram_addr;
            
            assign mem_addrs_packed[(n*10)+:10] = local_ram_addr;

            node_scorer scorer_inst (
                .clk(clk),
                .rst_n(rst_n),
                .start(score_start),
                .allowed_parents(proposed_masks[n]), 
                
                .done(score_done[n]),
                .final_score(node_scores[n]),
                
                .mem_addr(local_ram_addr),
                .mem_local_score(local_ram_data[31:0]),
                .mem_parent_mask(local_ram_data[63:32])
            );
        end
    endgenerate

    wire all_scores_done = &score_done;

    // Masked Adder Tree
    reg signed [DATA_W-1:0] score_sum;
    integer k;
    always @(*) begin
        score_sum = 0;
        for (k = 0; k < N_NODES; k = k + 1) begin
            if (k < active_nodes) begin 
                score_sum = score_sum + node_scores[k];
            end
        end
    end
    assign proposed_score = score_sum;

    wire signed [DATA_W-1:0] score_diff = proposed_score - current_score;
    wire accept_move;
     
    // Best Order Packing (5 bits per node)
    reg [4:0] best_order_internal [0:N_NODES-1];
    genvar b;
    generate
        for (b = 0; b < N_NODES; b = b + 1) begin : pack_order
            assign best_order_packed[(b*5)+:5] = best_order_internal[b];
        end
    endgenerate

    // ==============================================================
    // Metropolis-Hastings Acceptance Logic
    // ==============================================================
    wire [31:0] abs_diff = -score_diff;
    
    // Approximate P = exp(-abs_diff) using a bit-shift.
    // We shift right by the Integer part (bits 20:16), plus the 
    // 0.5 fractional bit (bit 15) to round to the nearest power of 2.
    wire [4:0] shift_amt = abs_diff[20:16] + abs_diff[15];
    
    // If the move is worse by more than 15.0 log-likelihood, probability is 0%
    wire [15:0] dynamic_prob = (abs_diff >= 32'h000F_0000) ? 16'd0 : (16'hFFFF >> shift_amt);

    // Accept if better, OR randomly accept based on how bad the move is
    assign accept_move = (score_diff >= 0) ? 1'b1 : (saved_rand_u < dynamic_prob);
    
    // MCMC State Machine
    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= S_IDLE;
            done        <= 1'b0;
            score_start <= 1'b0;
            lfsr_en     <= 1'b0;
            load_seed   <= 1'b0;
            iter_count  <= 0;
            current_score <= 32'h8000_0000;
            best_score  <= 32'h8000_0000;
            for (i = 0; i < N_NODES; i = i + 1) begin
                current_order[i]  <= i;
                proposed_order[i] <= i;
                current_pos[i]    <= i;
                proposed_pos[i]   <= i;
            end
        end else begin
            lfsr_en     <= 1'b1;
            load_seed   <= 1'b0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        done        <= 1'b0;
                        iter_count  <= 0;
                        load_seed   <= 1'b1;
                        state       <= S_INIT_SCORE;
                    end
                end

                S_INIT_SCORE: begin
                    score_start <= 1'b1;
                    state       <= S_WAIT_INIT_SCORE;
                end

                S_WAIT_INIT_SCORE: begin
                    if (all_scores_done) begin
                        score_start   <= 1'b0;
                        current_score <= proposed_score;
                        best_score    <= proposed_score; 
                        
                        // initialize order
                        for (i = 0; i < N_NODES; i = i + 1) begin
                            best_order_internal[i] <= proposed_order[i];
                        end
                        
                        state <= S_PROPOSE;
                    end
                end

                S_PROPOSE: begin
                    if (iter_count < iterations) begin
                        // 1. Snapshot the random variables
                        saved_rand_i <= rand_i;
                        saved_rand_j <= rand_j;
                        saved_rand_u <= rand_u;

                        // 2. Make the proposal using the live wires
                        proposed_order[rand_i] <= current_order[rand_j];
                        proposed_order[rand_j] <= current_order[rand_i];
                        proposed_pos[current_order[rand_i]] <= rand_j;
                        proposed_pos[current_order[rand_j]] <= rand_i;
                        
                        score_start <= 1'b1;
                        state       <= S_WAIT_SCORE;
                    end else begin
                        state <= S_DONE;
                    end
                end

                S_WAIT_SCORE: begin
                    if (all_scores_done) begin
                        score_start <= 1'b0;
                        state       <= S_DECIDE;
                    end
                end

                S_DECIDE: begin
                    if (accept_move) begin
                        current_score <= proposed_score;
                        
                        // COMMIT the swap using the SAVED indices
                        current_order[saved_rand_i] <= proposed_order[saved_rand_i];
                        current_order[saved_rand_j] <= proposed_order[saved_rand_j];
                        current_pos[proposed_order[saved_rand_i]] <= proposed_pos[proposed_order[saved_rand_i]];
                        current_pos[proposed_order[saved_rand_j]] <= proposed_pos[proposed_order[saved_rand_j]];
                        
                        if (proposed_score > best_score) begin
                            best_score <= proposed_score;
                            for (i = 0; i < N_NODES; i = i + 1) begin
                                best_order_internal[i] <= proposed_order[i];
                            end
                        end
                    end else begin
                        // REVERT the swap using the SAVED indices
                        proposed_order[saved_rand_i] <= current_order[saved_rand_i];
                        proposed_order[saved_rand_j] <= current_order[saved_rand_j];
                        proposed_pos[current_order[saved_rand_i]] <= current_pos[current_order[saved_rand_i]];
                        proposed_pos[current_order[saved_rand_j]] <= current_pos[current_order[saved_rand_j]];
                    end
                    
                    iter_count <= iter_count + 1;
                    state      <= S_PROPOSE;
                end

                S_DONE: begin
                    done  <= 1'b1;
                    if (!start) state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule

// ==========================================
// Node Scorer (Sentinel Version)
// ==========================================
module node_scorer (
    input wire clk,
    input wire rst_n,
    input wire start,
    input wire [31:0] allowed_parents, 
    
    output reg done,
    output reg signed [31:0] final_score, 
    
    output reg [9:0] mem_addr,
    input wire signed [31:0] mem_local_score, 
    input wire [31:0] mem_parent_mask
);
    localparam signed [31:0] MIN_SCORE = 32'h8000_0000;
    localparam IDLE          = 3'd0;
    localparam FETCH_ADDR    = 3'd1;
    localparam FETCH_DATA    = 3'd2;
    localparam WAIT_LOGADD_1 = 3'd3;
    localparam WAIT_LOGADD_2 = 3'd4;
    localparam DONE          = 3'd5;

    reg [2:0] state;
    reg first_valid_found;
    reg signed [31:0] accum_score;
    wire signed [31:0] log_add_out;

    log_add log_add_inst (
        .clk(clk), .rst_n(rst_n),
        .a(accum_score), .b(mem_local_score), .result(log_add_out)
    );

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            done <= 1'b0;
            final_score <= MIN_SCORE;
            mem_addr <= 0;
            accum_score <= MIN_SCORE;
            first_valid_found <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        mem_addr <= 0;
                        accum_score <= MIN_SCORE;
                        first_valid_found <= 1'b0;
                        state <= FETCH_ADDR;
                    end
                end

                FETCH_ADDR: state <= FETCH_DATA;

                FETCH_DATA: begin
                    // Check for the 0xFFFFFFFF Sentinel mask!
                    if (mem_parent_mask == 32'hFFFF_FFFF) begin 
                        state <= DONE;
                    end 
                    else if ((mem_parent_mask & allowed_parents) == mem_parent_mask) begin
                        if (!first_valid_found) begin
                            accum_score <= mem_local_score;
                            first_valid_found <= 1'b1;
                            mem_addr <= mem_addr + 1;
                            state <= FETCH_ADDR;
                        end else begin
                            state <= WAIT_LOGADD_1;
                        end
                    end 
                    else begin
                        // Skip invalid candidates
                        mem_addr <= mem_addr + 1;
                        state <= FETCH_ADDR;
                    end
                end

                WAIT_LOGADD_1: state <= WAIT_LOGADD_2;
                WAIT_LOGADD_2: begin
                    accum_score <= log_add_out;
                    mem_addr <= mem_addr + 1;
                    state <= FETCH_ADDR;
                end

                DONE: begin
                    final_score <= accum_score;
                    // Instantly drop 'done' the moment the main FSM drops 'start'
                    if (!start) begin
                        done <= 1'b0;
                        state <= IDLE;
                    end else begin
                        done <= 1'b1;
                    end
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule

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
        $readmemh("log_lut.txt", mem);
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
    output wire signed [31:0] result
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

    assign result = force_zero_s2 ? max_val_s2 : max_val_s2 + lut_val_s2; // If diff is large, log(1+e^-x) ~ 0, so result ~ max_val

    // PIPELINE LOGIC
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            max_val_s1  <= 0;
            abs_diff_s1 <= 0;
            max_val_s2  <= 0;
            force_zero_s2 <= 0;
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

        end
    end
endmodule

// ==========================================
// M10K RAM Block (Mixed-Width)
// ==========================================
module mcmc_node_ram (
    input  wire        clk,
    input  wire        we,
    input  wire [6:0]  write_addr, 
    input  wire [31:0] write_data,
    input  wire [5:0]  read_addr,
    output reg  [63:0] read_data
);

    // Splitting into two 32-bit arrays
    reg [31:0] ram_lower [0:63];
    reg [31:0] ram_upper [0:63];

    always @(posedge clk) begin
        if (we) begin
            if (write_addr[0] == 1'b0)
                ram_lower[write_addr[6:1]] <= write_data;
            else
                ram_upper[write_addr[6:1]] <= write_data;
        end
    end

    // Synchronous Read stitches them back for the 64-bit hardware
    always @(posedge clk) begin
        read_data <= {ram_upper[read_addr], ram_lower[read_addr]};
    end
endmodule