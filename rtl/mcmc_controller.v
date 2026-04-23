`timescale 1ns / 1ps

module mcmc_controller #(
    parameter int N_NODES = 32,
    parameter int ADDR_W = 10,
    parameter int DATA_W = 32,        // Q16.16 format
    parameter int ITERATIONS = 100000
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
    localparam S_IDLE       = 3'd0;
    localparam S_INIT_SCORE = 3'd1; // Score initial order
    localparam S_PROPOSE    = 3'd2; // Swap nodes
    localparam S_WAIT_SCORE = 3'd3; // Wait for parallel modules to finish
    localparam S_DECIDE     = 3'd4; // Accept or reject
    localparam S_DONE       = 3'd5;

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
    wire [4:0] rand_i = lfsr_out[4:0];
    wire [4:0] rand_j = lfsr_out[9:5];
    wire [15:0] rand_u = lfsr_out[31:16]; // Used for acceptance probability

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
    
    genvar row, col;
    generate
        for (row = 0; row < N_NODES; row = row + 1) begin : gen_rows
            for (col = 0; col < N_NODES; col = col + 1) begin : gen_cols
                // Node at 'col' is an allowed parent of node at 'row' IF 'col' comes before 'row'
                assign proposed_masks[ proposed_order[row] ][ proposed_order[col] ] = (col < row) ? 1'b1 : 1'b0;
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
                .num_candidates(10'd1024),           // Assume full BRAM usage or pass real config
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
                    state       <= S_WAIT_SCORE;
                end

                S_WAIT_SCORE: begin
                    if (all_scores_done) begin
                        if (iter_count == 0) begin
                            // First iteration: initialize the current_score
                            current_score <= proposed_score;
                            state         <= S_PROPOSE;
                        end else begin
                            state <= S_DECIDE;
                        end
                    end
                end

                S_PROPOSE: begin
                    if (iter_count < ITERATIONS) begin
                        // 1. Shift LFSR to get new i, j, u
                        lfsr_en <= 1'b1;
                        
                        // 2. Modulo nodes to prevent out of bounds
                        // If N_NODES is not a power of 2, this requires a modulo op or limits.
                        // Assuming N_NODES=32 for clean bit slicing.
                        
                        // 3. Swap indices in the proposed order
                        proposed_order[rand_i] <= current_order[rand_j];
                        proposed_order[rand_j] <= current_order[rand_i];
                        
                        // 4. Fire off the scorers for the next cycle
                        score_start <= 1'b1;
                        state       <= S_WAIT_SCORE;
                    end else begin
                        state <= S_DONE;
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