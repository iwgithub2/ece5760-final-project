//in the top-level module ///////////////////


// clock divider to slow system down for testing
reg [4:0] count;
// analog update divided clock
always @ (posedge CLOCK_50) 
begin
        count <= count + 1; 
end	
assign AnalogClock = (count==0);	
