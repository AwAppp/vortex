`include "VX_define.vh"

// Test wrapper for VX_kmu_refactored_dcr_kd
// Exposes internal signals for Verilator testbench

module VX_kmu_refactored_dcr_kd_top import VX_gpu_pkg::*;
(
    input wire              clk,
    input wire              reset,

    // kmu_data_t input fields (flattened for Verilator)
    input wire [`XLEN-1:0]      hwq_pc,
    input wire [31:0]           hwq_grid_dim_x,
    input wire [31:0]           hwq_grid_dim_y,
    input wire [31:0]           hwq_grid_dim_z,
    input wire [31:0]           hwq_block_dim_x,
    input wire [31:0]           hwq_block_dim_y,
    input wire [31:0]           hwq_block_dim_z,
    input wire [`XLEN-1:0]      hwq_param,

    // Handshake signals
    input wire                  hwq_data_valid,
    output wire                 kmu_kd_ready,

    // Downstream ready signal (simulating consumer)
    input wire                  req_ready,

    // Output data for verification
    output wire                 req_valid,
    output wire [31:0]          req_num_warps,
    output wire [`XLEN-1:0]     req_start_pc,
    output wire [`XLEN-1:0]     req_param,
    output wire [31:0]          req_cta_x,
    output wire [31:0]          req_cta_y,
    output wire [31:0]          req_cta_z,
    output wire [31:0]          req_cta_id,
    output wire [`NUM_THREADS-1:0] req_remain_mask,

    output wire                 start
);

    // Pack input into kmu_data_t struct
    kmu_data_t hwq_data;
    assign hwq_data.pc = hwq_pc;
    assign hwq_data.grid_dim[0] = hwq_grid_dim_x;
    assign hwq_data.grid_dim[1] = hwq_grid_dim_y;
    assign hwq_data.grid_dim[2] = hwq_grid_dim_z;
    assign hwq_data.block_dim[0] = hwq_block_dim_x;
    assign hwq_data.block_dim[1] = hwq_block_dim_y;
    assign hwq_data.block_dim[2] = hwq_block_dim_z;
    assign hwq_data.param = hwq_param;

    // KMU bus interface
    VX_kmu_bus_if kmu_bus_out[1]();

    // Connect downstream ready
    assign kmu_bus_out[0].req_ready = req_ready;

    // Instantiate the DUT
    VX_kmu_refactored_dcr_kd dut (
        .clk            (clk),
        .reset          (reset),
        .hwq_data       (hwq_data),
        .hwq_data_valid (hwq_data_valid),
        .kmu_kd_ready   (kmu_kd_ready),
        .kmu_bus_out    (kmu_bus_out),
        .start          (start)
    );

    // Expose output signals for verification
    assign req_valid        = kmu_bus_out[0].req_valid;
    assign req_num_warps    = kmu_bus_out[0].req_data.num_warps;
    assign req_start_pc     = kmu_bus_out[0].req_data.start_pc;
    assign req_param        = kmu_bus_out[0].req_data.param;
    assign req_cta_x        = kmu_bus_out[0].req_data.cta_x;
    assign req_cta_y        = kmu_bus_out[0].req_data.cta_y;
    assign req_cta_z        = kmu_bus_out[0].req_data.cta_z;
    assign req_cta_id       = kmu_bus_out[0].req_data.cta_id;
    assign req_remain_mask  = kmu_bus_out[0].req_data.remain_mask;

endmodule
