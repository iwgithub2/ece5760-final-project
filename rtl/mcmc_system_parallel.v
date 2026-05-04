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
    input  wire [11:0] avs_address,
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
                wire local_we = avs_write && (avs_address[11:7] == n);
                wire [5:0] read_addr = chain_addrs[(n*10)+:6];
                wire [63:0] read_data;

                mcmc_node_ram ram_inst (
                    .clk(clk),
                    .we(local_we),
                    .write_addr(avs_address[6:0]),
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
