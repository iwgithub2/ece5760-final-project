`timescale 1ns / 1ps
`include "node_scorer.v"

module tb_node_scorer;

    // Control signals
    reg clk;
    reg rst_n;
    reg start;
    reg [31:0] allowed_parents;
    reg [9:0]  start_addr;
    reg [9:0]  num_cands;
    
    // Outputs from DUT
    wire done;
    wire signed [31:0] final_score;
    wire [9:0] mem_addr;
    
    // Mock Memory Output signals
    reg signed [31:0] mem_local_score;
    reg [31:0] mem_parent_mask;

    // ----------------------------------------------------
    // Mock M10K Memory (1 Cycle Latency)
    // ----------------------------------------------------
    reg signed [31:0] mock_scores [0:1023];
    reg [31:0]        mock_masks  [0:1023];

    always @(posedge clk) begin
        // The RAM reads the address on the rising edge and 
        // outputs data for the next cycle, exactly like an M10K.
        mem_local_score <= mock_scores[mem_addr];
        mem_parent_mask <= mock_masks[mem_addr];
    end

    // ----------------------------------------------------
    // Device Under Test (DUT)
    // ----------------------------------------------------
    node_scorer dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .allowed_parents(allowed_parents),
        .start_addr(start_addr),
        .num_cands(num_cands),
        .done(done),
        .final_score(final_score),
        .mem_addr(mem_addr),
        .mem_local_score(mem_local_score),
        .mem_parent_mask(mem_parent_mask)
    );

    // Clock Generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // 100MHz clock
    end

    // ----------------------------------------------------
    // Test Sequence
    // ----------------------------------------------------
    initial begin
        $dumpfile("tb_node_scorer.vcd");
        $dumpvars(0, tb_node_scorer);

        // Initialize signals
        rst_n = 0;
        start = 0;
        allowed_parents = 0;
        start_addr = 0;
        num_cands = 0;

        // Populate Mock Memory with some test candidates
        // Index 0: Score = 10.0, Mask = 0001 (Node 0)
        mock_scores[0] = 32'h000A_0000; mock_masks[0] = 32'b0001; 
        
        // Index 1: Score =  5.0, Mask = 0010 (Node 1)
        mock_scores[1] = 32'h0005_0000; mock_masks[1] = 32'b0010; 
        
        // Index 2: Score =  5.0, Mask = 0100 (Node 2)
        mock_scores[2] = 32'h0005_0000; mock_masks[2] = 32'b0100; 
        
        // Index 3: Score =  0.0, Mask = 1000 (Node 3)
        mock_scores[3] = 32'h0000_0000; mock_masks[3] = 32'b1000; 

        #15;
        rst_n = 1;
        #10;

        // ==========================================
        // Test 1: Zero Candidates Edge Case
        // ==========================================
        $display("--- Test 1: Zero Candidates ---");
        start_addr = 0;
        num_cands  = 0;
        allowed_parents = 32'hFFFF_FFFF; // Allow anything
        
        @(posedge clk) start = 1;
        @(posedge done); // Wait for FSM to assert done
        
        $display("Result = %08x (Expected 80000000 / MIN_SCORE)", final_score);
        @(posedge clk) start = 0; // Release start so FSM goes to IDLE
        repeat(3) @(posedge clk);


        // ==========================================
        // Test 2: Single Valid Candidate (Bypass Logic check)
        // ==========================================
        $display("\n--- Test 2: Single Valid Candidate ---");
        // We will check 4 candidates, but only ALLOW Node 0 (Mask 0001)
        start_addr = 0;
        num_cands  = 4;
        allowed_parents = 32'b0001; 
        
        @(posedge clk) start = 1;
        @(posedge done); 
        
        // It should skip indices 1, 2, and 3.
        // It should load index 0 (10.0) without doing a log_add subtraction from MIN_SCORE.
        $display("Result = %08x (Expected 000A0000)", final_score);
        @(posedge clk) start = 0;
        repeat(3) @(posedge clk);


        // ==========================================
        // Test 3: Multiple Valid Candidates (Pipeline check)
        // ==========================================
        $display("\n--- Test 3: Multiple Valid Candidates ---");
        // Check 3 candidates starting at index 1.
        // Allow Node 1 (0010) and Node 2 (0100).
        start_addr = 1;
        num_cands  = 3; // Checks index 1, 2, 3
        allowed_parents = 32'b0110; 
        
        @(posedge clk) start = 1;
        @(posedge done); 
        
        // Valid candidates: Index 1 (Score 5.0), Index 2 (Score 5.0)
        // Invalid candidate: Index 3 (Skipped)
        // Expected Math: log_add(5.0, 5.0)
        // Max = 5.0, Diff = 0. LUT adds ~0.69314 (0x0000B171)
        // Total = 5.69314 -> 32'h0005_B171
        $display("Result = %08x (Expected ~0005B171)", final_score);
        @(posedge clk) start = 0;
        repeat(5) @(posedge clk);

        $display("\nnode_scorer verification complete.");
        $finish;
    end

endmodule