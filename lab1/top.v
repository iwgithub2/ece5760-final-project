`include "components.v"

module XYZIntegrator (
	clock, 
	reset,
	x    ,
	y    ,
	z    ,
	p    ,
	b    ,
	s    ,
	dt   ,
	x_out,
	y_out,
	z_out
);

  input clock, reset;

  // declare inputs

  input signed [26:0] x;
  input signed [26:0] y;
  input signed [26:0] z;

  input signed [26:0] p;
  input signed [26:0] b;
  input signed [26:0] s;
  input signed [26:0] dt;

  // declare outputs

  output wire  signed [26:0] x_out;
  output wire  signed [26:0] y_out;
  output wire  signed [26:0] z_out;

  // // clock divider to slow system down for testing
  // reg [4:0] count;
  // // analog update divided clock
  // always @ (posedge clock) begin
  //         count <= count + 1; 
  // end	
  // wire AnalogClock;
  // assign AnalogClock = (count==0);

  //-----------------------------------------------------
  // X out
  //-----------------------------------------------------

  wire [26:0] y_minus_x;

  subtractor y_minus_x_adder 
  (
    .a    (y_out),
    .b    (x_out),
    .sum  (y_minus_x)
  );
  
  wire [26:0] sigma_mul;

  signed_mult sig_mul 
  (
    .a   (y_minus_x),
    .b   (s),
    .out (sigma_mul)
  );

  wire [26:0] x_final_mul_out;

  signed_mult x_mul 
  (
    .a   (sigma_mul),
    .b   (dt),
    .out (x_final_mul_out)
  );
  
  integrator x_integrator 
  (
    .clk        (clock),
    .reset      (reset), 
    .funct      (x_final_mul_out),
    .out        (x_out),
    .InitialOut (x)
  );

  //-----------------------------------------------------
  // Y out
  //-----------------------------------------------------

  wire [26:0] p_minus_z;

  subtractor p_minus_z_adder 
  (
    .a    (p),
    .b    (z_out),
    .sum  (p_minus_z)
  );

  wire [26:0] zp_times_x;

  signed_mult z_p 
  (
    .a   (p_minus_z),
    .b   (x_out),
    .out (zp_times_x)
  );
  
  wire [26:0] x_minus_y;

  subtractor x_minus_y_adder 
  (
    .a    (zp_times_x),
    .b    (y_out),
    .sum  (x_minus_y)
  );
  
  wire [26:0] y_final_mul_out;

  signed_mult y_mul 
  (
    .a   (x_minus_y),
    .b   (dt),
    .out (y_final_mul_out)
  );

  integrator y_integrator 
  (
    .clk        (clock),
    .reset      (reset), 
    .funct      (y_final_mul_out),
    .out        (y_out),
    .InitialOut (y)
  );

  //-----------------------------------------------------
  // Z out
  //-----------------------------------------------------

  wire [26:0] x_times_y;

  signed_mult x_y 
  (
    .a   (x_out),
    .b   (y_out),
    .out (x_times_y)
  );
  
  wire [26:0] b_times_z;

  signed_mult b_z
  (
    .a   (b),
    .b   (z_out),
    .out (b_times_z)
  );

  wire [26:0] xy_minus_zb;

  subtractor xy_minus_ab_adder 
  (
    .a    (x_times_y),
    .b    (b_times_z),
    .sum  (xy_minus_zb)
  );

  wire [26:0] z_final_mul_out;

  signed_mult z_mul 
  (
    .a   (xy_minus_zb),
    .b   (dt),
    .out (z_final_mul_out)
  );

  integrator z_integrator 
  (
    .clk        (clock),
    .reset      (reset), 
    .funct      (z_final_mul_out),
    .out        (z_out),
    .InitialOut (z)
  );

endmodule

