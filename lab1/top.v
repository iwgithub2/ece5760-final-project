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

  // MIGHT NEED ADDITIONAL SIGNALS FOR INITIAL CONDITIONS
  // X Y Z MAY NOT BE INITIAL CONDITIONS?
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

  // clock divider to slow system down for testing
  reg [4:0] count;
  // analog update divided clock
  always @ (posedge clock) begin
          count <= count + 1; 
  end	
  assign AnalogClock = (count==0);

  //-----------------------------------------------------
  // X out
  //-----------------------------------------------------

  wire [26:0] y_minus_x;

  adder y_minus_x 
  (
    .a    (~x),
    .b    (y),
    .cin  (1'b1),
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
    .clk        (AnalogClock),
    .reset      (reset), 
    .funct      (x_final_mul_out),
    .out        (x_out),
    .InitialOut ()
  );

  //-----------------------------------------------------
  // Y out
  //-----------------------------------------------------

  wire [26:0] p_minus_z;

  adder x_minus_y 
  (
    .a    (p),
    .b    (~z),
    .cin  (1'b1),
    .sum  (p_minus_z)
  );

  wire [26:0] zp_times_x;

  signed_mult z_p 
  (
    .a   (p_minus_z),
    .b   (x),
    .out (zp_times_x)
  );
  
  wire [26:0] x_minus_y;

  adder x_minus_y 
  (
    .a    (zp_times_x),
    .b    (~y),
    .cin  (1'b1),
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
    .clk        (AnalogClock),
    .reset      (reset), 
    .funct      (y_final_mul_out),
    .out        (y_out),
    .InitialOut ()
  );

  //-----------------------------------------------------
  // Z out
  //-----------------------------------------------------

  wire [26:0] x_times_y;

  signed_mult z_p 
  (
    .a   (x),
    .b   (y),
    .out (x_times_y)
  );
  
  wire [26:0] b_times_z;

  signed_mult z_p 
  (
    .a   (b),
    .b   (z),
    .out (b_times_z)
  );

  wire [26:0] xy_minus_zb;

  adder x_minus_y 
  (
    .a    (x_times_y),
    .b    (~b_times_z),
    .cin  (1'b1),
    .sum  (xy_minus_zb)
  );

  wire [26:0] z_final_mul_out;

  signed_mult y_mul 
  (
    .a   (xy_minus_zb),
    .b   (dt),
    .out (z_final_mul_out)
  );

  integrator z_integrator 
  (
    .clk        (AnalogClock),
    .reset      (reset), 
    .funct      (z_final_mul_out),
    .out        (z_out),
    .InitialOut ()
  );

endmodule

