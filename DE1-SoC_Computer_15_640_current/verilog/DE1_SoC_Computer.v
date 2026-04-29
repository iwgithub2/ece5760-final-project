

module DE1_SoC_Computer (
	////////////////////////////////////
	// FPGA Pins
	////////////////////////////////////

	// Clock pins
	CLOCK_50,
	CLOCK2_50,
	CLOCK3_50,
	CLOCK4_50,

	// ADC
	ADC_CS_N,
	ADC_DIN,
	ADC_DOUT,
	ADC_SCLK,

	// Audio
	AUD_ADCDAT,
	AUD_ADCLRCK,
	AUD_BCLK,
	AUD_DACDAT,
	AUD_DACLRCK,
	AUD_XCK,

	// SDRAM
	DRAM_ADDR,
	DRAM_BA,
	DRAM_CAS_N,
	DRAM_CKE,
	DRAM_CLK,
	DRAM_CS_N,
	DRAM_DQ,
	DRAM_LDQM,
	DRAM_RAS_N,
	DRAM_UDQM,
	DRAM_WE_N,

	// I2C Bus for Configuration of the Audio and Video-In Chips
	FPGA_I2C_SCLK,
	FPGA_I2C_SDAT,

	// 40-Pin Headers
	GPIO_0,
	GPIO_1,
	
	// Seven Segment Displays
	HEX0,
	HEX1,
	HEX2,
	HEX3,
	HEX4,
	HEX5,

	// IR
	IRDA_RXD,
	IRDA_TXD,

	// Pushbuttons
	KEY,

	// LEDs
	LEDR,

	// PS2 Ports
	PS2_CLK,
	PS2_DAT,
	
	PS2_CLK2,
	PS2_DAT2,

	// Slider Switches
	SW,

	// Video-In
	TD_CLK27,
	TD_DATA,
	TD_HS,
	TD_RESET_N,
	TD_VS,

	// VGA
	VGA_B,
	VGA_BLANK_N,
	VGA_CLK,
	VGA_G,
	VGA_HS,
	VGA_R,
	VGA_SYNC_N,
	VGA_VS,

	////////////////////////////////////
	// HPS Pins
	////////////////////////////////////
	
	// DDR3 SDRAM
	HPS_DDR3_ADDR,
	HPS_DDR3_BA,
	HPS_DDR3_CAS_N,
	HPS_DDR3_CKE,
	HPS_DDR3_CK_N,
	HPS_DDR3_CK_P,
	HPS_DDR3_CS_N,
	HPS_DDR3_DM,
	HPS_DDR3_DQ,
	HPS_DDR3_DQS_N,
	HPS_DDR3_DQS_P,
	HPS_DDR3_ODT,
	HPS_DDR3_RAS_N,
	HPS_DDR3_RESET_N,
	HPS_DDR3_RZQ,
	HPS_DDR3_WE_N,

	// Ethernet
	HPS_ENET_GTX_CLK,
	HPS_ENET_INT_N,
	HPS_ENET_MDC,
	HPS_ENET_MDIO,
	HPS_ENET_RX_CLK,
	HPS_ENET_RX_DATA,
	HPS_ENET_RX_DV,
	HPS_ENET_TX_DATA,
	HPS_ENET_TX_EN,

	// Flash
	HPS_FLASH_DATA,
	HPS_FLASH_DCLK,
	HPS_FLASH_NCSO,

	// Accelerometer
	HPS_GSENSOR_INT,
		
	// General Purpose I/O
	HPS_GPIO,
		
	// I2C
	HPS_I2C_CONTROL,
	HPS_I2C1_SCLK,
	HPS_I2C1_SDAT,
	HPS_I2C2_SCLK,
	HPS_I2C2_SDAT,

	// Pushbutton
	HPS_KEY,

	// LED
	HPS_LED,
		
	// SD Card
	HPS_SD_CLK,
	HPS_SD_CMD,
	HPS_SD_DATA,

	// SPI
	HPS_SPIM_CLK,
	HPS_SPIM_MISO,
	HPS_SPIM_MOSI,
	HPS_SPIM_SS,

	// UART
	HPS_UART_RX,
	HPS_UART_TX,

	// USB
	HPS_CONV_USB_N,
	HPS_USB_CLKOUT,
	HPS_USB_DATA,
	HPS_USB_DIR,
	HPS_USB_NXT,
	HPS_USB_STP
);

//=======================================================
//  PARAMETER declarations
//=======================================================


//=======================================================
//  PORT declarations
//=======================================================

////////////////////////////////////
// FPGA Pins
////////////////////////////////////

// Clock pins
input						CLOCK_50;
input						CLOCK2_50;
input						CLOCK3_50;
input						CLOCK4_50;

// ADC
inout						ADC_CS_N;
output					ADC_DIN;
input						ADC_DOUT;
output					ADC_SCLK;

// Audio
input						AUD_ADCDAT;
inout						AUD_ADCLRCK;
inout						AUD_BCLK;
output					AUD_DACDAT;
inout						AUD_DACLRCK;
output					AUD_XCK;

// SDRAM
output 		[12: 0]	DRAM_ADDR;
output		[ 1: 0]	DRAM_BA;
output					DRAM_CAS_N;
output					DRAM_CKE;
output					DRAM_CLK;
output					DRAM_CS_N;
inout			[15: 0]	DRAM_DQ;
output					DRAM_LDQM;
output					DRAM_RAS_N;
output					DRAM_UDQM;
output					DRAM_WE_N;

// I2C Bus for Configuration of the Audio and Video-In Chips
output					FPGA_I2C_SCLK;
inout						FPGA_I2C_SDAT;

// 40-pin headers
inout			[35: 0]	GPIO_0;
inout			[35: 0]	GPIO_1;

// Seven Segment Displays
output		[ 6: 0]	HEX0;
output		[ 6: 0]	HEX1;
output		[ 6: 0]	HEX2;
output		[ 6: 0]	HEX3;
output		[ 6: 0]	HEX4;
output		[ 6: 0]	HEX5;

// IR
input						IRDA_RXD;
output					IRDA_TXD;

// Pushbuttons
input			[ 3: 0]	KEY;

// LEDs
output		[ 9: 0]	LEDR;

// PS2 Ports
inout						PS2_CLK;
inout						PS2_DAT;

inout						PS2_CLK2;
inout						PS2_DAT2;

// Slider itches
input			[ 9: 0]	SW;

// Video-In
input						TD_CLK27;
input			[ 7: 0]	TD_DATA;
input						TD_HS;
output					TD_RESET_N;
input						TD_VS;

// VGA
output		[ 7: 0]	VGA_B;
output					VGA_BLANK_N;
output					VGA_CLK;
output		[ 7: 0]	VGA_G;
output					VGA_HS;
output		[ 7: 0]	VGA_R;
output					VGA_SYNC_N;
output					VGA_VS;



////////////////////////////////////
// HPS Pins
////////////////////////////////////
	
// DDR3 SDRAM
output		[14: 0]	HPS_DDR3_ADDR;
output		[ 2: 0]  HPS_DDR3_BA;
output					HPS_DDR3_CAS_N;
output					HPS_DDR3_CKE;
output					HPS_DDR3_CK_N;
output					HPS_DDR3_CK_P;
output					HPS_DDR3_CS_N;
output		[ 3: 0]	HPS_DDR3_DM;
inout			[31: 0]	HPS_DDR3_DQ;
inout			[ 3: 0]	HPS_DDR3_DQS_N;
inout			[ 3: 0]	HPS_DDR3_DQS_P;
output					HPS_DDR3_ODT;
output					HPS_DDR3_RAS_N;
output					HPS_DDR3_RESET_N;
input						HPS_DDR3_RZQ;
output					HPS_DDR3_WE_N;

// Ethernet
output					HPS_ENET_GTX_CLK;
inout						HPS_ENET_INT_N;
output					HPS_ENET_MDC;
inout						HPS_ENET_MDIO;
input						HPS_ENET_RX_CLK;
input			[ 3: 0]	HPS_ENET_RX_DATA;
input						HPS_ENET_RX_DV;
output		[ 3: 0]	HPS_ENET_TX_DATA;
output					HPS_ENET_TX_EN;

// Flash
inout			[ 3: 0]	HPS_FLASH_DATA;
output					HPS_FLASH_DCLK;
output					HPS_FLASH_NCSO;

// Accelerometer
inout						HPS_GSENSOR_INT;

// General Purpose I/O
inout			[ 1: 0]	HPS_GPIO;

// I2C
inout						HPS_I2C_CONTROL;
inout						HPS_I2C1_SCLK;
inout						HPS_I2C1_SDAT;
inout						HPS_I2C2_SCLK;
inout						HPS_I2C2_SDAT;

// Pushbutton
inout						HPS_KEY;

// LED
inout						HPS_LED;

// SD Card
output					HPS_SD_CLK;
inout						HPS_SD_CMD;
inout			[ 3: 0]	HPS_SD_DATA;

// SPI
output					HPS_SPIM_CLK;
input						HPS_SPIM_MISO;
output					HPS_SPIM_MOSI;
inout						HPS_SPIM_SS;

// UART
input						HPS_UART_RX;
output					HPS_UART_TX;

// USB
inout						HPS_CONV_USB_N;
input						HPS_USB_CLKOUT;
inout			[ 7: 0]	HPS_USB_DATA;
input						HPS_USB_DIR;
input						HPS_USB_NXT;
output					HPS_USB_STP;

//=======================================================
//  REG/WIRE declarations
//=======================================================


//=======================================================
// SRAM state machine
//=======================================================

// PIO wires
wire [31:0] pio_start;
wire [31:0] pio_num_cands;
wire [31:0] pio_allowed_parents;
wire [31:0] pio_done;
wire [31:0] pio_final_score;

// SRAM Wires
wire [9:0]  sram_address;
wire [63:0] sram_readdata;
wire        sram_clken;
wire        sram_chipselect;

assign sram_clken = 1'b1;
assign sram_chipselect = 1'b1;

// Assuming HPS packs them: [63:32 = Mask] | [31:0 = Score]
wire signed [31:0] mem_local_score = sram_readdata[31:0];
wire [31:0]        mem_parent_mask = sram_readdata[63:32];

 // ---------------------------------------------------
 // Instantiate the Node Scorer
 // ---------------------------------------------------
 node_scorer scorer_inst (
	  .clk(CLOCK_50),
	  .rst_n(~KEY[0]), // Press KEY0 to reset
	  
	  .start(pio_start[0]),
	  .allowed_parents(pio_allowed_parents),
	  .start_addr(10'd0), // Always start at index 0 for this test
	  .num_cands(pio_num_cands[9:0]),
	  
	  .done(pio_done[0]),
	  .final_score(pio_final_score),
	  
	  .mem_addr(sram_address),
	  .mem_local_score(mem_local_score),
	  .mem_parent_mask(mem_parent_mask)
 );


//=======================================================
//  Structural coding
//=======================================================

Computer_System The_System (
	////////////////////////////////////
	// FPGA Side
	////////////////////////////////////

	// Global signals
	.system_pll_ref_clk_clk					(CLOCK_50),
	.system_pll_ref_reset_reset			(1'b0),
	
	// SRAM shared block with HPS
	.onchip_sram_s1_address               (sram_address),               
	.onchip_sram_s1_clken                 (sram_clken),                 
	.onchip_sram_s1_chipselect            (sram_chipselect),            
	.onchip_sram_s1_write                 (1'b0),                 
	.onchip_sram_s1_readdata              (sram_readdata),              
	.onchip_sram_s1_writedata             (64'd0),             
	.onchip_sram_s1_byteenable            (8'hFF), 

	// AV Config
	.av_config_SCLK							(FPGA_I2C_SCLK),
	.av_config_SDAT							(FPGA_I2C_SDAT),
	
	// PIOs
	.pio_allowed_parents_external_connection_export (pio_allowed_parents),
	.pio_done_external_connection_export(pio_done),
	.pio_final_score_external_connection_export(pio_final_score),
	.pio_num_cands_external_connection_export(pio_num_cands),
	.pio_start_external_connection_export(pio_start),

	// Slider Switches
	//.slider_switches_export					(SW),

	// Pushbuttons (~KEY[3:0]),
	//.pushbuttons_export						(~KEY[3:0]),

	.clock_bridge_0_in_clk_clk            (CLOCK_50), 
	// LEDs
	//.leds_export								( ),
	
	// Seven Segs
	//.hex3_hex0_export							(hex3_hex0),
	//.hex5_hex4_export							(hex5_hex4),
	
	// VGA Subsystem
	.vga_pll_ref_clk_clk 					(CLOCK2_50),
	.vga_pll_ref_reset_reset				(1'b0),
	.vga_CLK										(VGA_CLK),
	.vga_BLANK									(VGA_BLANK_N),
	.vga_SYNC									(VGA_SYNC_N),
	.vga_HS										(VGA_HS),
	.vga_VS										(VGA_VS),
	.vga_R										(VGA_R),
	.vga_G										(VGA_G),
	.vga_B										(VGA_B),
		
	// SDRAM
	.sdram_clk_clk								(DRAM_CLK),
   .sdram_addr									(DRAM_ADDR),
	.sdram_ba									(DRAM_BA),
	.sdram_cas_n								(DRAM_CAS_N),
	.sdram_cke									(DRAM_CKE),
	.sdram_cs_n									(DRAM_CS_N),
	.sdram_dq									(DRAM_DQ),
	.sdram_dqm									({DRAM_UDQM,DRAM_LDQM}),
	.sdram_ras_n								(DRAM_RAS_N),
	.sdram_we_n									(DRAM_WE_N),
	
	////////////////////////////////////
	// HPS Side
	////////////////////////////////////
	// DDR3 SDRAM
	.memory_mem_a			(HPS_DDR3_ADDR),
	.memory_mem_ba			(HPS_DDR3_BA),
	.memory_mem_ck			(HPS_DDR3_CK_P),
	.memory_mem_ck_n		(HPS_DDR3_CK_N),
	.memory_mem_cke		(HPS_DDR3_CKE),
	.memory_mem_cs_n		(HPS_DDR3_CS_N),
	.memory_mem_ras_n		(HPS_DDR3_RAS_N),
	.memory_mem_cas_n		(HPS_DDR3_CAS_N),
	.memory_mem_we_n		(HPS_DDR3_WE_N),
	.memory_mem_reset_n	(HPS_DDR3_RESET_N),
	.memory_mem_dq			(HPS_DDR3_DQ),
	.memory_mem_dqs		(HPS_DDR3_DQS_P),
	.memory_mem_dqs_n		(HPS_DDR3_DQS_N),
	.memory_mem_odt		(HPS_DDR3_ODT),
	.memory_mem_dm			(HPS_DDR3_DM),
	.memory_oct_rzqin		(HPS_DDR3_RZQ),
		  
	// Ethernet
	.hps_io_hps_io_gpio_inst_GPIO35	(HPS_ENET_INT_N),
	.hps_io_hps_io_emac1_inst_TX_CLK	(HPS_ENET_GTX_CLK),
	.hps_io_hps_io_emac1_inst_TXD0	(HPS_ENET_TX_DATA[0]),
	.hps_io_hps_io_emac1_inst_TXD1	(HPS_ENET_TX_DATA[1]),
	.hps_io_hps_io_emac1_inst_TXD2	(HPS_ENET_TX_DATA[2]),
	.hps_io_hps_io_emac1_inst_TXD3	(HPS_ENET_TX_DATA[3]),
	.hps_io_hps_io_emac1_inst_RXD0	(HPS_ENET_RX_DATA[0]),
	.hps_io_hps_io_emac1_inst_MDIO	(HPS_ENET_MDIO),
	.hps_io_hps_io_emac1_inst_MDC		(HPS_ENET_MDC),
	.hps_io_hps_io_emac1_inst_RX_CTL	(HPS_ENET_RX_DV),
	.hps_io_hps_io_emac1_inst_TX_CTL	(HPS_ENET_TX_EN),
	.hps_io_hps_io_emac1_inst_RX_CLK	(HPS_ENET_RX_CLK),
	.hps_io_hps_io_emac1_inst_RXD1	(HPS_ENET_RX_DATA[1]),
	.hps_io_hps_io_emac1_inst_RXD2	(HPS_ENET_RX_DATA[2]),
	.hps_io_hps_io_emac1_inst_RXD3	(HPS_ENET_RX_DATA[3]),

	// Flash
	.hps_io_hps_io_qspi_inst_IO0	(HPS_FLASH_DATA[0]),
	.hps_io_hps_io_qspi_inst_IO1	(HPS_FLASH_DATA[1]),
	.hps_io_hps_io_qspi_inst_IO2	(HPS_FLASH_DATA[2]),
	.hps_io_hps_io_qspi_inst_IO3	(HPS_FLASH_DATA[3]),
	.hps_io_hps_io_qspi_inst_SS0	(HPS_FLASH_NCSO),
	.hps_io_hps_io_qspi_inst_CLK	(HPS_FLASH_DCLK),

	// Accelerometer
	.hps_io_hps_io_gpio_inst_GPIO61	(HPS_GSENSOR_INT),

	//.adc_sclk                        (ADC_SCLK),
	//.adc_cs_n                        (ADC_CS_N),
	//.adc_dout                        (ADC_DOUT),
	//.adc_din                         (ADC_DIN),

	// General Purpose I/O
	.hps_io_hps_io_gpio_inst_GPIO40	(HPS_GPIO[0]),
	.hps_io_hps_io_gpio_inst_GPIO41	(HPS_GPIO[1]),

	// I2C
	.hps_io_hps_io_gpio_inst_GPIO48	(HPS_I2C_CONTROL),
	.hps_io_hps_io_i2c0_inst_SDA		(HPS_I2C1_SDAT),
	.hps_io_hps_io_i2c0_inst_SCL		(HPS_I2C1_SCLK),
	.hps_io_hps_io_i2c1_inst_SDA		(HPS_I2C2_SDAT),
	.hps_io_hps_io_i2c1_inst_SCL		(HPS_I2C2_SCLK),

	// Pushbutton
	.hps_io_hps_io_gpio_inst_GPIO54	(HPS_KEY),

	// LED
	.hps_io_hps_io_gpio_inst_GPIO53	(HPS_LED),

	// SD Card
	.hps_io_hps_io_sdio_inst_CMD	(HPS_SD_CMD),
	.hps_io_hps_io_sdio_inst_D0	(HPS_SD_DATA[0]),
	.hps_io_hps_io_sdio_inst_D1	(HPS_SD_DATA[1]),
	.hps_io_hps_io_sdio_inst_CLK	(HPS_SD_CLK),
	.hps_io_hps_io_sdio_inst_D2	(HPS_SD_DATA[2]),
	.hps_io_hps_io_sdio_inst_D3	(HPS_SD_DATA[3]),

	// SPI
	.hps_io_hps_io_spim1_inst_CLK		(HPS_SPIM_CLK),
	.hps_io_hps_io_spim1_inst_MOSI	(HPS_SPIM_MOSI),
	.hps_io_hps_io_spim1_inst_MISO	(HPS_SPIM_MISO),
	.hps_io_hps_io_spim1_inst_SS0		(HPS_SPIM_SS),

	// UART
	.hps_io_hps_io_uart0_inst_RX	(HPS_UART_RX),
	.hps_io_hps_io_uart0_inst_TX	(HPS_UART_TX),

	// USB
	.hps_io_hps_io_gpio_inst_GPIO09	(HPS_CONV_USB_N),
	.hps_io_hps_io_usb1_inst_D0		(HPS_USB_DATA[0]),
	.hps_io_hps_io_usb1_inst_D1		(HPS_USB_DATA[1]),
	.hps_io_hps_io_usb1_inst_D2		(HPS_USB_DATA[2]),
	.hps_io_hps_io_usb1_inst_D3		(HPS_USB_DATA[3]),
	.hps_io_hps_io_usb1_inst_D4		(HPS_USB_DATA[4]),
	.hps_io_hps_io_usb1_inst_D5		(HPS_USB_DATA[5]),
	.hps_io_hps_io_usb1_inst_D6		(HPS_USB_DATA[6]),
	.hps_io_hps_io_usb1_inst_D7		(HPS_USB_DATA[7]),
	.hps_io_hps_io_usb1_inst_CLK		(HPS_USB_CLKOUT),
	.hps_io_hps_io_usb1_inst_STP		(HPS_USB_STP),
	.hps_io_hps_io_usb1_inst_DIR		(HPS_USB_DIR),
	.hps_io_hps_io_usb1_inst_NXT		(HPS_USB_NXT)
);
endmodule // end top level

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

module node_scorer (
    input wire clk,
    input wire rst_n,
    
    // Control signals from MCMC Controller
    input wire start,
    input wire [31:0] allowed_parents, // Bitmask of nodes that appear BEFORE this node in the order
    input wire [9:0] start_addr,       // Base address in M10K for this node's candidates
    input wire [9:0] num_cands,        // How many candidates to check for this node
    output reg done,
    output reg signed [31:0] final_score, // Q16.16 format
    
    // Memory Interface (To M10K Blocks) 
    // Assuming 1-cycle read latency (Standard for Quartus M10K)
    output reg [9:0] mem_addr,
    input wire signed [31:0] mem_local_score, // Q16.16 format
    input wire [31:0] mem_parent_mask
);

    // smallest possible value in 16.16
    localparam signed [31:0] MIN_SCORE = 32'h8000_0000;

    // FSM States
    localparam IDLE          = 3'd0;
    localparam FETCH_ADDR    = 3'd1;
    localparam FETCH_DATA    = 3'd2;
    localparam WAIT_LOGADD_1 = 3'd3;
    localparam WAIT_LOGADD_2 = 3'd4;
    localparam WAIT_LOGADD_3 = 3'd5;
    localparam DONE          = 3'd6;

    reg [2:0] state;
    reg [9:0] cands_checked;
    reg first_valid_found;
    
    // Log-Add Signals 
    reg signed [31:0] accum_score;
    wire signed [31:0] log_add_out;
    
    // Pipelined Log-Add module (3 cycles latency)
    log_add log_add_inst (
        .clk(clk),
        .rst_n(rst_n),
        .a(accum_score),         
        .b(mem_local_score),
        .result(log_add_out)
    );

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            done <= 1'b0;
            final_score <= MIN_SCORE;
            mem_addr <= 0;
            cands_checked <= 0;
            accum_score <= MIN_SCORE;
            first_valid_found <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        if (num_cands == 0) begin
                            // Edge case: Node has no candidates (shouldn't happen, but safe)
                            final_score <= MIN_SCORE;
                            done <= 1'b1;
                        end else begin
                            mem_addr <= start_addr;
                            cands_checked <= 0;
                            accum_score <= MIN_SCORE;
                            first_valid_found <= 1'b0;
                            state <= FETCH_ADDR;
                        end
                    end
                end

                FETCH_ADDR: begin
                    // Address is sent to M10K this cycle. Data will arrive next cycle.
                    state <= FETCH_DATA;
                end

                FETCH_DATA: begin
                    // Figure 3 Enable Logic: Check compatibility
                    // C-code equivalent: check_compatibility(parent_bitmask, order, i)
                    if ((mem_parent_mask & allowed_parents) == mem_parent_mask) begin
                        // Candidate is VALID
                        if (!first_valid_found) begin
                            // First valid candidate bypasses log_add to avoid subtracting from -INF
                            accum_score <= mem_local_score;
                            first_valid_found <= 1'b1;
                            
                            // Check if we are done
                            if (cands_checked + 1 == num_cands) begin
                                state <= DONE;
                            end else begin
                                mem_addr <= mem_addr + 1;
                                cands_checked <= cands_checked + 1;
                                state <= FETCH_ADDR;
                            end
                        end else begin
                            // Not the first valid candidate, feed into pipelined log_add
                            // Data is currently on log_add_inst inputs. Wait 3 cycles.
                            state <= WAIT_LOGADD_1;
                        end
                    end else begin
                        // Candidate is INVALID, move to next
                        if (cands_checked + 1 == num_cands) begin
                            state <= DONE;
                        end else begin
                            mem_addr <= mem_addr + 1;
                            cands_checked <= cands_checked + 1;
                            state <= FETCH_ADDR;
                        end
                    end
                end

                // Pipeline Delay States for Log-Add
                WAIT_LOGADD_1: state <= WAIT_LOGADD_2;
                WAIT_LOGADD_2: state <= WAIT_LOGADD_3;
                WAIT_LOGADD_3: begin
                    // 3 cycles have passed, log_add_out is valid
                    accum_score <= log_add_out;
                    
                    if (cands_checked + 1 == num_cands) begin
                        state <= DONE;
                    end else begin
                        mem_addr <= mem_addr + 1;
                        cands_checked <= cands_checked + 1;
                        state <= FETCH_ADDR;
                    end
                end

                DONE: begin
                    final_score <= accum_score;
                    done <= 1'b1;
                    if (!start) begin // Wait for controller to de-assert start
                        state <= IDLE;
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
    output reg signed [31:0] result
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

    // PIPELINE LOGIC
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            max_val_s1  <= 0;
            abs_diff_s1 <= 0;
            max_val_s2  <= 0;
            force_zero_s2 <= 0;
            result      <= 0;
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

            // ==========================================
            // STAGE 3: Final Addition
            // ==========================================
            if (force_zero_s2) begin
                result <= max_val_s2; // Add 0
            end else begin
                result <= max_val_s2 + lut_val_s2;
            end
        end
    end
endmodule


/// end /////////////////////////////////////////////////////////////////////