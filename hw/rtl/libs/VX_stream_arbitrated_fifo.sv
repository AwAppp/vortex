`include "VX_platform.vh"

module VX_stream_arbitrated_fifo #(
    parameter DATAW    = 32,
    parameter DEPTH    = 32,
    parameter OUT_REG  = 1,  // 1 for better timing closure
    parameter LUTRAM   = 0   // 0 = BlockRAM, 1 = LUTRAM (for small depths)
) (
    input  wire             clk,
    input  wire             reset,

    // -------------------------------------------------------
    // Source 0 Interface
    // -------------------------------------------------------
    input  wire             src0_valid,
    input  wire [DATAW-1:0] src0_data,
    output wire             src0_ready,

    // -------------------------------------------------------
    // Source 1 Interface
    // -------------------------------------------------------
    input  wire             src1_valid,
    input  wire [DATAW-1:0] src1_data,
    output wire             src1_ready,

    // -------------------------------------------------------
    // Consumer Interface
    // -------------------------------------------------------
    output wire             consumer_valid,
    output wire [DATAW-1:0] consumer_data,
    input  wire             consumer_ready
);

    // ============================================================
    // 1. Pack Inputs for the Arbiter
    // ============================================================
    // VX_stream_arb expects arrays for inputs
    
    wire [1:0]            arb_valid_in;
    wire [1:0][DATAW-1:0] arb_data_in;
    wire [1:0]            arb_ready_in;

    assign arb_valid_in = {src1_valid, src0_valid};
    assign arb_data_in  = {src1_data,  src0_data};

    assign src0_ready   = arb_ready_in[0];
    assign src1_ready   = arb_ready_in[1];

    // ============================================================
    // 2. Internal Connection (Arbiter -> Buffer)
    // ============================================================
    
    wire             merged_valid;
    wire [DATAW-1:0] merged_data;
    wire             merged_ready;

    // ============================================================
    // 3. Stream Arbiter (Merge Logic)
    // ============================================================
    
    VX_stream_arb #(
        .NUM_INPUTS  (2),
        .NUM_OUTPUTS (1),
        .DATAW       (DATAW),
        .ARBITER     ("R"), // Round-Robin for fairness
        .OUT_BUF     (0)    // We don't need buffering here, the big FIFO is next
    ) arbiter (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (arb_valid_in),
        .data_in   (arb_data_in),
        .ready_in  (arb_ready_in),
        .valid_out (merged_valid),  // 1-bit wide because NUM_OUTPUTS=1
        .data_out  (merged_data),
        .ready_out (merged_ready),
        .sel_out   ()               // Unused
    );

    // ============================================================
    // 4. Elastic Buffer (Storage Logic)
    // ============================================================
    // This replaces the manual "VX_elastic_fifo" we wrote earlier.
    
    VX_elastic_buffer #(
        .DATAW   (DATAW),
        .SIZE    (DEPTH),   // <--- This makes it a deep FIFO
        .OUT_REG (OUT_REG), // Registers the output for timing
        .LUTRAM  (LUTRAM)
    ) buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (merged_valid),
        .ready_in  (merged_ready),
        .data_in   (merged_data),
        .valid_out (consumer_valid),
        .ready_out (consumer_ready),
        .data_out  (consumer_data)
    );

endmodule