`timescale 1ns / 1ps
`include "components.v"

module mcmc_controller #(
    parameter N_NODES = 32,
    parameter ADDR_W = 10,
    parameter DATA_W = 32,        // Q16.16 format
    parameter ITERATIONS = 100000
)(
    input  wire clk,
    input  wire rst_n,

    // Control Interface
    input  wire start,
    output reg  done,
    output reg signed [DATA_W-1:0] best_score,
    
    // Config
    input  wire [31:0] seed
);

    // ==========================================
    // FSM States
    // ==========================================
    localparam S_IDLE            = 3'd0;
    localparam S_INIT_SCORE      = 3'd1; 
    localparam S_WAIT_INIT_SCORE = 3'd2;
    localparam S_PROPOSE         = 3'd3; 
    localparam S_WAIT_SCORE      = 3'd4; 
    localparam S_DECIDE          = 3'd5; 
    localparam S_DONE            = 3'd6;

    reg [2:0] state;
    reg [31:0] iter_count;

    // ==========================================
    // Random Number Generation (LFSR)
    // ==========================================
    wire [31:0] lfsr_out;
    reg lfsr_en;
    reg load_seed;

    lfsr_32bit rng_inst (
        .clk(clk),
        .rst_n(rst_n),
        .en(lfsr_en),
        .seed(seed),
        .load_seed(load_seed),
        .rand_out(lfsr_out)
    );

    // Slice the 32-bit LFSR to get I, J, and U simultaneously
    // Assumes N_NODES <= 32, so we need 5 bits for indices.
    // Slice the 32-bit LFSR to get I, J, and U simultaneously
    wire [4:0] rand_i = lfsr_out[4:0] % N_NODES;
    wire [4:0] rand_j = lfsr_out[9:5] % N_NODES;
    wire [15:0] rand_u = lfsr_out[31:16]; 

    // ==========================================
    // Order Matrix Management (1D to 2D One-Hot)
    // ==========================================
    // We maintain a simple 1D array of the order to easily swap indices.
    // We dynamically generate the 2D "allowed_parents" mask from this 1D array.
    reg [4:0] current_order  [0:N_NODES-1];
    reg [4:0] proposed_order [0:N_NODES-1];

    reg signed [DATA_W-1:0] current_score;
    wire signed [DATA_W-1:0] proposed_score;

    // Generate the 2D One-Hot masks for the Scoring Modules
    wire [N_NODES-1:0] current_masks  [0:N_NODES-1];
    wire [N_NODES-1:0] proposed_masks [0:N_NODES-1];
    
    // ==========================================
    // Order Matrix Management
    // ==========================================
    
    // 1. Create a combinational inverse mapping to find the position of each node
    reg [4:0] proposed_pos [0:N_NODES-1];
    integer i_node, j_pos;
    
    always @(*) begin
        for (i_node = 0; i_node < N_NODES; i_node = i_node + 1) begin
            proposed_pos[i_node] = 5'd0; // Default to prevent latches
            for (j_pos = 0; j_pos < N_NODES; j_pos = j_pos + 1) begin
                if (proposed_order[j_pos] == i_node) begin
                    proposed_pos[i_node] = j_pos;
                end
            end
        end
    end

    // 2. Statically assign the masks by comparing positions
    genvar n_row, n_col;
    generate
        for (n_row = 0; n_row < N_NODES; n_row = n_row + 1) begin : gen_rows
            for (n_col = 0; n_col < N_NODES; n_col = n_col + 1) begin : gen_cols
                // Node n_col is an allowed parent of n_row IF its position comes earlier
                assign proposed_masks[n_row][n_col] = (proposed_pos[n_col] < proposed_pos[n_row]) ? 1'b1 : 1'b0;
            end
        end
    endgenerate

    // ==========================================
    // Parallel Node Scoring Modules
    // ==========================================
    reg score_start;
    wire [N_NODES-1:0] score_done;
    wire signed [DATA_W-1:0] node_scores [0:N_NODES-1];

    genvar n;
    generate
        for (n = 0; n < N_NODES; n = n + 1) begin : gen_scorers
            mcmc_node_scoring #(
                .N_NODES(N_NODES),
                .ADDR_W(ADDR_W),
                .DATA_W(DATA_W)
            ) scorer_inst (
                .clk(clk),
                .rst_n(rst_n),
                .start(score_start),
                .allowed_parents(proposed_masks[n]), // Pass the specific row mask to each node
                .num_candidates(10'd1023),           // Assume full BRAM usage or pass real config
                .done(score_done[n]),
                .final_score(node_scores[n])
                // BRAM interface wires would connect here to individual memory blocks
            );
        end
    endgenerate

    wire all_scores_done = &score_done;

    // Combinational Adder Tree to sum all parallel node scores
    // (Simplified for brevity: in reality, for N=32, you'd want to pipeline this tree to save Fmax)
    reg signed [DATA_W-1:0] score_sum;
    integer k;
    always @(*) begin
        score_sum = 0;
        for (k = 0; k < N_NODES; k = k + 1) begin
            score_sum = score_sum + node_scores[k];
        end
    end
    assign proposed_score = score_sum;

    // ==========================================
    // Acceptance Logic
    // ==========================================
    wire signed [DATA_W-1:0] score_diff = proposed_score - current_score;
    wire accept_move;
    
    // If proposed is better or equal, accept immediately.
    // Otherwise, we need `u < exp(diff)`.
    // In hardware, we'd map `score_diff` to an exp() ROM output and compare with `rand_u`.
    // Placeholder logic for the ROM comparison:
    wire [15:0] exp_threshold; 
    
    // assign exp_threshold = my_exp_rom(score_diff); // Implement ROM separately
    assign exp_threshold = 16'h4000; // Fake 25% acceptance probability threshold
    
    assign accept_move = (score_diff >= 0) ? 1'b1 : (rand_u < exp_threshold);

    // ==========================================
    // MCMC State Machine
    // ==========================================
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
            end
        end else begin
            // Defaults
            score_start <= 1'b0;
            lfsr_en     <= 1'b0;
            load_seed   <= 1'b0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        load_seed <= 1'b1; // Load LFSR initial state
                        state     <= S_INIT_SCORE;
                    end
                end

                S_INIT_SCORE: begin
                    score_start <= 1'b1;
                    state       <= S_WAIT_INIT_SCORE;
                end

                S_WAIT_INIT_SCORE: begin
                    if (all_scores_done) begin
                        // Initialize the current_score once, then start proposing
                        current_score <= proposed_score;
                        state         <= S_PROPOSE;
                    end
                end

                S_PROPOSE: begin
                    if (iter_count < ITERATIONS) begin
                        lfsr_en <= 1'b1;
                        
                        proposed_order[rand_i] <= current_order[rand_j];
                        proposed_order[rand_j] <= current_order[rand_i];
                        
                        score_start <= 1'b1;
                        state       <= S_WAIT_SCORE;
                    end else begin
                        state <= S_DONE;
                    end
                end

                S_WAIT_SCORE: begin
                    if (all_scores_done) begin
                        // Directly to DECIDE since INIT is handled elsewhere
                        state <= S_DECIDE;
                    end
                end

                S_DECIDE: begin
                    if (accept_move) begin
                        // Accept: Update current score and apply the swap to current_order
                        current_score <= proposed_score;
                        current_order[rand_i] <= proposed_order[rand_i];
                        current_order[rand_j] <= proposed_order[rand_j];
                        
                        if (proposed_score > best_score) begin
                            best_score <= proposed_score;
                        end
                    end else begin
                        // Reject: Revert proposed order back to current
                        proposed_order[rand_i] <= current_order[rand_i];
                        proposed_order[rand_j] <= current_order[rand_j];
                    end
                    
                    iter_count <= iter_count + 1;
                    state      <= S_PROPOSE;
                end

                S_DONE: begin
                    done  <= 1'b1;
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end
endmodule