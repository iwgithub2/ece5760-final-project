`timescale 1ns/1ns
`include "top.v"    

module testbench();
	
	reg clk_50, clk_25, reset;
	
	reg [31:0] index;
	wire signed [15:0]  testbench_out;
	
	//Initialize clocks and index
	initial begin
		clk_50 = 1'b0;
		clk_25 = 1'b0;
		index  = 32'd0;
		//testbench_out = 15'd0 ;
	end
	
    initial begin
        $dumpfile("build/testbench.vcd"); // Give the file a specific name
        $dumpvars(0, testbench);    // Dump everything in the module 'testbench'
    end

	//Toggle the clocks
	always begin
		#10
		clk_50  = !clk_50;
	end
	
	always begin
		#20
		clk_25  = !clk_25;
	end
	
	//Intialize and drive signals
	initial begin
		reset  = 1'b0;
		#10 
		reset  = 1'b1;
        x      = -27'b000_0001_0000_0000_0000_0000_0000; // -1
        y      = 27'b000_0000_0001_1001_1001_1001_1010; // 0.1
        z      = 27'b001_1001_0000_0000_0000_0000_0000; // 25
        s      = 27'b000_1010_0000_0000_0000_0000_0000; // 10
        b      = 27'b000_0010_1010_1010_1010_1010_1011; // 8/3
        p      = 27'b001_1100_0000_0000_0000_0000_0000; // 28
        dt     = 27'b000_0000_0000_0001_0000_0000_0000; // 1/256
		#30
		reset  = 1'b0;

        #200000; 
        $finish;
	end
	
	//Increment index
	always @ (posedge clk_50) begin
		index  <= index + 32'd1;
	end

	//Instantiation of Device Under Test
	// Hookup all integrators
	XYZIntegrator DUT   (
		.clock(clk_50), 
        .reset(reset),
		.x (x),
		.y (y),
		.z (z),
		.p (p),
		.b (b),
		.s (s),
		.dt (dt),
		.x_out (x_out),
		.y_out (y_out),
		.z_out (z_out)
	);

    // declare inputs

    reg signed [26:0] x;
    reg signed [26:0] y;
    reg signed [26:0] z;

    reg signed [26:0] p;
    reg signed [26:0] b;
    reg signed [26:0] s;
    reg signed [26:0] dt;

    // declare outputs

    wire  signed [26:0] x_out;
    wire  signed [26:0] y_out;
    wire  signed [26:0] z_out;
	
endmodule