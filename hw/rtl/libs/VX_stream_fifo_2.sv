module VX_stream_fifo_2 #(
    parameter DATAW   = 32,
    parameter DEPTH   = 32,
    parameter OUT_REG = 1,  // 1 = fully registered output, 0 = skid buffer only
    parameter LUTRAM  = 0
) (
    input  wire             clk,
    input  wire             reset,

    // Input Handshake
    input  wire             valid_in,
    output wire             ready_in,
    input  wire [DATAW-1:0] data_in,

    // Output Handshake
    output wire             valid_out,
    input  wire             ready_out,
    output wire [DATAW-1:0] data_out
);

    // Internal Handshake Signals (Between FIFO and Buffer)
    wire             fifo_valid_out;
    wire             fifo_ready_in; // Driven by the Stream Buffer
    wire [DATAW-1:0] fifo_data_out;
    
    // Internal FIFO status (needed to generate fifo_valid_out)
    wire             fifo_empty;
    wire             fifo_full;

    // ============================================================
    // 1. The Storage (RAM)
    // ============================================================
    // CRITICAL: OUT_REG must be 0 here. We want the data to appear 
    // on the wires immediately so the Stream Buffer can latch it.
    
    // Handshake Translation:
    // We push if the input is valid AND the FIFO isn't full.
    wire push = valid_in && ~fifo_full;
    
    // We pop if the FIFO has data (valid) AND the Stream Buffer is ready to take it.
    wire pop  = ~fifo_empty && fifo_ready_in;

    VX_fifo_queue #(
        .DATAW   (DATAW),
        .DEPTH   (DEPTH),
        .OUT_REG (0),     // <--- MUST BE 0
        .LUTRAM  (LUTRAM)
    ) queue (
        .clk      (clk),
        .reset    (reset),
        .push     (push),
        .pop      (pop),
        .data_in  (data_in),
        .data_out (fifo_data_out),
        .empty    (fifo_empty),
        .full     (fifo_full),
        .alm_empty(), // Unused
        .alm_full (), // Unused
        .size     ()  // Unused
    );

    // The FIFO is "valid" whenever it is not empty.
    assign fifo_valid_out = ~fifo_empty;

    // The FIFO is "ready" for new input whenever it is not full.
    assign ready_in = ~fifo_full;

    // ============================================================
    // 2. The Output Register (Skid Buffer)
    // ============================================================
    // This decouples the RAM read path from the external destination.
    
    VX_stream_buffer #(
        .DATAW    (DATAW),
        .OUT_REG  (OUT_REG) // Set to 1 for fully registered output
    ) out_buffer (
        .clk      (clk),
        .reset    (reset),
        
        // Connects to FIFO output
        .valid_in (fifo_valid_out),
        .ready_in (fifo_ready_in),
        .data_in  (fifo_data_out),
        
        // Connects to External World
        .valid_out(valid_out),
        .ready_out(ready_out),
        .data_out (data_out)
    );

endmodule