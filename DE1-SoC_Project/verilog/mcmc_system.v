`timescale 1ns / 1ps

// ============================================================================
// Parallel MCMC System Wrapper
// ============================================================================
// Same external interface as mcmc_system.v. This top runs several independent
// MCMC chains with different seeds and returns the best score/order across
// chains. Candidate RAMs are replicated per chain and HPS writes are broadcast
// to all replicas. This avoids a read-port arbiter on the scorer hot path.
//
// Compile this file together with rtl/mcmc_system.v, which provides
// mcmc_controller and mcmc_node_ram.
module mcmc_system_parallel #(
    parameter N_NODES = 32,
    parameter N_CHAINS = 2
)(
    input  wire        clk,
    input  wire        reset_n,

    // Avalon-MM Slave Interface
    input  wire [13:0] avs_address,
    input  wire        avs_write,
    input  wire [31:0] avs_writedata,
    input  wire        avs_read,
    output reg  [31:0] avs_readdata,

    // Conduits
    input  wire        start,
    input  wire        pio_reset,
    input  wire [31:0] seed,
    input  wire [31:0] iterations,
    input  wire [31:0] active_nodes,
    input  wire [31:0] node_idx_mask,
    output wire        done,
    output wire [31:0] best_score,
    output reg  [31:0] clk_count
);
    localparam ORDER_PACK_W = N_CHAINS * N_NODES * 5;

    wire async_reset_n;
    reg [1:0] reset_sync;
    wire global_reset_n;

    assign async_reset_n = reset_n & (~pio_reset);

    always @(posedge clk or negedge async_reset_n) begin
        if (!async_reset_n) begin
            reset_sync <= 2'b00;
        end else begin
            reset_sync <= {reset_sync[0], 1'b1};
        end
    end

    assign global_reset_n = reset_sync[1];

    wire [N_CHAINS-1:0] chain_done;
    wire [(N_CHAINS*32)-1:0] chain_scores_packed;
    wire [ORDER_PACK_W-1:0] chain_orders_packed;

    reg signed [31:0] selected_best_score;
    reg [(N_NODES*5)-1:0] selected_best_order;
    integer select_idx;

    assign done = &chain_done;
    assign best_score = selected_best_score;

    always @(posedge clk or negedge global_reset_n) begin
        if (!global_reset_n) begin
            clk_count <= 32'd0;
        end else if (start && !done) begin
            clk_count <= clk_count + 1;
        end
    end

    always @(*) begin
        selected_best_score = chain_scores_packed[31:0];
        selected_best_order = chain_orders_packed[(N_NODES*5)-1:0];
        for (select_idx = 1; select_idx < N_CHAINS; select_idx = select_idx + 1) begin
            if ($signed(chain_scores_packed[(select_idx*32)+:32]) > selected_best_score) begin
                selected_best_score = chain_scores_packed[(select_idx*32)+:32];
                selected_best_order = chain_orders_packed[(select_idx*N_NODES*5)+:(N_NODES*5)];
            end
        end
    end

    always @(posedge clk) begin
        if (avs_read && avs_address[11] == 1'b1) begin
            avs_readdata <= { 12'd0,
                              selected_best_order[(avs_address[2:0]*20 + 15) +: 5],
                              selected_best_order[(avs_address[2:0]*20 + 10) +: 5],
                              selected_best_order[(avs_address[2:0]*20 + 5)  +: 5],
                              selected_best_order[(avs_address[2:0]*20)      +: 5] };
        end
    end

    genvar c, n;
    generate
        for (c = 0; c < N_CHAINS; c = c + 1) begin : gen_chains
            wire [(N_NODES*10)-1:0] chain_addrs;
            wire [(N_NODES*64)-1:0] chain_datas;
            wire [(N_NODES*5)-1:0] chain_order;
            wire signed [31:0] chain_score;
            wire [31:0] chain_seed = seed ^ (32'h9E37_79B9 * (c + 1));

            assign chain_orders_packed[(c*N_NODES*5)+:(N_NODES*5)] = chain_order;
            assign chain_scores_packed[(c*32)+:32] = chain_score;

            for (n = 0; n < N_NODES; n = n + 1) begin : gen_rams
                wire local_we = avs_write && (avs_address[13:9] == n);
                wire [7:0] read_addr = chain_addrs[(n*10)+:8];
                wire [63:0] read_data;

                mcmc_node_ram ram_inst (
                    .clk(clk),
                    .we(local_we),
                    .write_addr(avs_address[8:0]),
                    .write_data(avs_writedata),
                    .read_addr(read_addr),
                    .read_data(read_data)
                );

                assign chain_datas[(n*64)+:64] = read_data;
            end

            mcmc_controller #(.N_NODES(N_NODES)) core (
                .clk(clk),
                .rst_n(global_reset_n),
                .start(start),
                .seed(chain_seed),
                .iterations(iterations),
                .active_nodes(active_nodes),
                .node_idx_mask(node_idx_mask),
                .done(chain_done[c]),
                .best_score(chain_score),
                .best_order_packed(chain_order),
                .mem_addrs_packed(chain_addrs),
                .mem_datas_packed(chain_datas)
            );
        end
    endgenerate
endmodule

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
    wire async_reset_n;
    reg  [1:0] reset_sync;
    wire global_reset_n;

    assign async_reset_n = reset_n & (~pio_reset);

    // PIO reset can be released at an arbitrary time relative to clk. Assert
    // reset immediately, but release it synchronously so controller/scorer
    // state cannot split across cycles after a software reset.
    always @(posedge clk or negedge async_reset_n) begin
        if (!async_reset_n) begin
            reset_sync <= 2'b00;
        end else begin
            reset_sync <= {reset_sync[0], 1'b1};
        end
    end

    assign global_reset_n = reset_sync[1];
	 
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
                .write_addr(avs_address[8:0]), // Pass [6:0] to handle the word split
                .write_data(avs_writedata),
                .read_addr(read_addr[7:0]),
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
    localparam S_IDLE                = 4'd0;
    localparam S_INIT_SCORE          = 4'd1;
    localparam S_WAIT_INIT_SCORE     = 4'd2;
    localparam S_SUM_INIT_SCORE      = 4'd3;
    localparam S_WAIT_SUM_INIT_SCORE = 4'd4;
    localparam S_PROPOSE             = 4'd5;
    localparam S_WAIT_SCORE          = 4'd6;
    localparam S_SUM_SCORE           = 4'd7;
    localparam S_WAIT_SUM_SCORE      = 4'd8;
    localparam S_DECIDE              = 4'd9;
    localparam S_DONE                = 4'd10;

    reg [3:0] state;
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
    localparam [5:0] MAX_ACTIVE_NODES = N_NODES;

    wire [5:0] clamped_active_nodes =
        (active_nodes == 32'd0) ? 6'd1 :
        (active_nodes > MAX_ACTIVE_NODES) ? MAX_ACTIVE_NODES :
        active_nodes[5:0];
    wire [4:0] active_idx_mask = clamped_active_nodes[4:0] - 5'd1;
    wire [4:0] configured_idx_mask = node_idx_mask[4:0];
    wire [4:0] power2_idx_mask =
        (configured_idx_mask == active_idx_mask) ? configured_idx_mask : active_idx_mask;
    wire active_count_power2 = (clamped_active_nodes & (clamped_active_nodes - 6'd1)) == 6'd0;

    function [4:0] bounded_random_index;
        input [4:0] raw_index;
        input [5:0] count;
        reg [5:0] raw_ext;
        reg [5:0] reduced;
        begin
            raw_ext = {1'b0, raw_index};
            if (count <= 6'd1)
                bounded_random_index = 5'd0;
            else if (count >= 6'd32)
                bounded_random_index = raw_index;
            else begin
                reduced = raw_ext % count;
                bounded_random_index = reduced[4:0];
            end
        end
    endfunction

    wire [4:0] rand_i = active_count_power2 ?
        (lfsr_out[4:0] & power2_idx_mask) :
        bounded_random_index(lfsr_out[4:0], clamped_active_nodes);

    // ADJACENT SWAP
    // add 1 and wrap around to 0 if it exceeds the active node count.
    wire [5:0] next_j = {1'b0, rand_i} + 6'd1;
    wire [4:0] rand_j = (next_j >= clamped_active_nodes) ? 5'd0 : next_j[4:0];
    wire [15:0] rand_u = lfsr_out[31:16];

    // Order Matrix Management
    reg [4:0] current_order  [0:N_NODES-1];
    reg [4:0] proposed_order [0:N_NODES-1];
    reg [4:0] current_pos    [0:N_NODES-1];
    reg [4:0] proposed_pos   [0:N_NODES-1]; 

    reg signed [DATA_W-1:0] current_score;
    reg signed [DATA_W-1:0] proposed_score;

    wire [N_NODES-1:0] proposed_masks [0:N_NODES-1];

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
    reg sum_start;
    wire [N_NODES-1:0] score_done;
    wire signed [DATA_W-1:0] node_scores [0:N_NODES-1];
    wire [(N_NODES*DATA_W)-1:0] node_scores_packed;
    wire sum_done;
    wire signed [DATA_W-1:0] sum_result;

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

            assign node_scores_packed[(n*DATA_W)+:DATA_W] = node_scores[n];
        end
    endgenerate

    wire all_scores_done = &score_done;

    // Multi-cycle score sum. This replaces the old wide combinational adder
    // tree with one registered mux stage and one 32-bit add stage.
    score_accumulator #(
        .N_NODES(N_NODES),
        .DATA_W(DATA_W)
    ) score_sum_inst (
        .clk(clk),
        .rst_n(rst_n),
        .start(sum_start),
        .active_count(clamped_active_nodes),
        .scores_packed(node_scores_packed),
        .done(sum_done),
        .sum(sum_result)
    );

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
	 // ==============================================================
    // SIMULATED ANNEALING TEMPERATURE SCHEDULE
    // ==============================================================
    // Phase 1 (0% - 25%): Boiling hot. Accepts almost anything. 
    //                     Completely shuffles the hardcoded initial order.
    // Phase 2 (25% - 50%): Warm. Begins to favor better moves.
    // Phase 3 (50% - 75%): Cool. Actively climbs the hill.
    // Phase 4 (75% - 100%): Freezing. Greedy search to lock onto the exact peak.
    
    wire [4:0] TEMP_SHIFT = (iter_count < (iterations >> 2)) ? 5'd4 :
                            (iter_count < (iterations >> 1)) ? 5'd2 :
                            (iter_count < (iterations - (iterations >> 2))) ? 5'd1 : 
                            5'd0;

    wire [31:0] abs_diff = -score_diff;
    
    // Calculate raw shift, then subtract the temperature shift (clamped at 0 to prevent underflow)
    wire [4:0] raw_shift_amt = abs_diff[20:16] + {4'd0, abs_diff[15]};
    wire [4:0] shift_amt = (raw_shift_amt > TEMP_SHIFT) ? (raw_shift_amt - TEMP_SHIFT) : 5'd0;
    
    // If the move is worse by more than 15.0 log-likelihood, probability is 0%
    wire [15:0] dynamic_prob = (abs_diff >= 32'h000F_0000) ?
        16'd0 : (16'hFFFF >> shift_amt);

    // Accept if better, OR randomly accept based on how bad the move is
    assign accept_move = (score_diff >= 0) ? 1'b1 : (saved_rand_u < dynamic_prob);
    
    // MCMC State Machine
    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= S_IDLE;
            done        <= 1'b0;
            score_start <= 1'b0;
            sum_start   <= 1'b0;
            lfsr_en     <= 1'b0;
            load_seed   <= 1'b0;
            iter_count  <= 0;
            current_score <= 32'h8000_0000;
            proposed_score <= 32'h8000_0000;
            best_score  <= 32'h8000_0000;
            for (i = 0; i < N_NODES; i = i + 1) begin
                current_order[i]  <= i[4:0];
                proposed_order[i] <= i[4:0];
                current_pos[i]    <= i[4:0];
                proposed_pos[i]   <= i[4:0];
            end
        end else begin
            lfsr_en     <= 1'b1;
            load_seed   <= 1'b0;
            sum_start   <= 1'b0;

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
                        score_start <= 1'b0;
                        state       <= S_SUM_INIT_SCORE;
                    end
                end

                S_SUM_INIT_SCORE: begin
                    sum_start <= 1'b1;
                    state     <= S_WAIT_SUM_INIT_SCORE;
                end

                S_WAIT_SUM_INIT_SCORE: begin
                    if (sum_done) begin
                        proposed_score <= sum_result;
                        current_score  <= sum_result;
                        best_score     <= sum_result;

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

                        // 2. Rebuild proposal from current state, then apply one swap.
                        for (i = 0; i < N_NODES; i = i + 1) begin
                            if (i[4:0] == rand_i)
                                proposed_order[i] <= current_order[rand_j];
                            else if (i[4:0] == rand_j)
                                proposed_order[i] <= current_order[rand_i];
                            else
                                proposed_order[i] <= current_order[i];

                            if (i[4:0] == current_order[rand_i])
                                proposed_pos[i] <= rand_j;
                            else if (i[4:0] == current_order[rand_j])
                                proposed_pos[i] <= rand_i;
                            else
                                proposed_pos[i] <= current_pos[i];
                        end
                        
                        score_start <= 1'b1;
                        state       <= S_WAIT_SCORE;
                    end else begin
                        state <= S_DONE;
                    end
                end

                S_WAIT_SCORE: begin
                    if (all_scores_done) begin
                        score_start <= 1'b0;
                        state       <= S_SUM_SCORE;
                    end
                end

                S_SUM_SCORE: begin
                    sum_start <= 1'b1;
                    state     <= S_WAIT_SUM_SCORE;
                end

                S_WAIT_SUM_SCORE: begin
                    if (sum_done) begin
                        proposed_score <= sum_result;
                        state          <= S_DECIDE;
                    end
                end

                S_DECIDE: begin
                    if (accept_move) begin
                        current_score <= proposed_score;
                        
                        // Commit the full proposed state. This keeps order and inverse
                        // position maps synchronized even after rejected proposals.
                        for (i = 0; i < N_NODES; i = i + 1) begin
                            current_order[i] <= proposed_order[i];
                            current_pos[i]   <= proposed_pos[i];
                        end
                        
                        if (proposed_score > best_score) begin
                            best_score <= proposed_score;
                            for (i = 0; i < N_NODES; i = i + 1) begin
                                best_order_internal[i] <= proposed_order[i];
                            end
                        end
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
// Multi-Cycle Score Accumulator
// ==========================================
module score_accumulator #(
    parameter N_NODES = 32,
    parameter DATA_W = 32
)(
    input wire clk,
    input wire rst_n,
    input wire start,
    input wire [5:0] active_count,
    input wire [(N_NODES*DATA_W)-1:0] scores_packed,
    output reg done,
    output reg signed [DATA_W-1:0] sum
);
    localparam ACC_IDLE = 2'd0;
    localparam ACC_LOAD = 2'd1;
    localparam ACC_ADD  = 2'd2;

    localparam [5:0] MAX_COUNT = N_NODES;

    reg [1:0] state;
    reg [5:0] idx;
    reg signed [DATA_W-1:0] selected_score;
    wire [5:0] safe_count = (active_count > MAX_COUNT) ? MAX_COUNT : active_count;
    wire signed [DATA_W-1:0] score_at_idx = scores_packed[(idx*DATA_W)+:DATA_W];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= ACC_IDLE;
            idx <= 6'd0;
            selected_score <= {DATA_W{1'b0}};
            sum <= {DATA_W{1'b0}};
            done <= 1'b0;
        end else begin
            case (state)
                ACC_IDLE: begin
                    if (start) begin
                        idx <= 6'd0;
                        sum <= {DATA_W{1'b0}};
                        done <= 1'b0;
                        if (safe_count == 6'd0) begin
                            done <= 1'b1;
                        end else begin
                            state <= ACC_LOAD;
                        end
                    end else begin
                        done <= 1'b0;
                    end
                end

                ACC_LOAD: begin
                    selected_score <= score_at_idx;
                    state <= ACC_ADD;
                end

                ACC_ADD: begin
                    sum <= sum + selected_score;
                    if ((idx + 6'd1) >= safe_count) begin
                        done <= 1'b1;
                        state <= ACC_IDLE;
                    end else begin
                        idx <= idx + 6'd1;
                        state <= ACC_LOAD;
                    end
                end

                default: begin
                    state <= ACC_IDLE;
                    done <= 1'b0;
                end
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
    input  wire [8:0]  write_addr, 
    input  wire [31:0] write_data,
    input  wire [7:0]  read_addr,
    output reg  [63:0] read_data
);

    // Splitting into two 32-bit arrays
    reg [31:0] ram_lower [0:255];
    reg [31:0] ram_upper [0:255];

    always @(posedge clk) begin
        if (we) begin
            if (write_addr[0] == 1'b0)
                ram_lower[write_addr[8:1]] <= write_data;
            else
                ram_upper[write_addr[8:1]] <= write_data;
        end
    end

    // Synchronous Read stitches them back for the 64-bit hardware
    always @(posedge clk) begin
        read_data <= {ram_upper[read_addr], ram_lower[read_addr]};
    end
endmodule
