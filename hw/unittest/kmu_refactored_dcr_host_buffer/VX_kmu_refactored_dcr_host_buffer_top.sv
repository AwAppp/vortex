`include "VX_define.vh"

// Test wrapper for VX_kmu_refactored_dcr_host_buffer
// Exposes internal signals for Verilator testbench

module VX_kmu_refactored_dcr_host_buffer_top import VX_gpu_pkg::*;
(
    input wire              clk,
    input wire              reset,

    // DCR write request
    input  wire                             dcr_wr_valid,
    input  wire [VX_DCR_ADDR_WIDTH-1:0]     dcr_wr_addr,
    input  wire [VX_DCR_DATA_WIDTH-1:0]     dcr_wr_data,

    // Downstream ready signal
    input wire                              hwq_in_ready,

    // Output valid signal
    output wire                             dcr_out_valid,

    // Output data fields (flattened for Verilator)
    output wire [`XLEN-1:0]     out_pc,
    output wire [`XLEN-1:0]     out_param,
    output wire [31:0]          out_grid_dim_x,
    output wire [31:0]          out_grid_dim_y,
    output wire [31:0]          out_grid_dim_z,
    output wire [31:0]          out_block_dim_x,
    output wire [31:0]          out_block_dim_y,
    output wire [31:0]          out_block_dim_z
);

    // Internal kmu_data_t output
    kmu_data_t dcr_kmu_data;

    // Instantiate the DUT
    VX_kmu_refactored_dcr_host_buffer dut (
        .clk            (clk),
        .reset          (reset),
        .dcr_wr_valid   (dcr_wr_valid),
        .dcr_wr_addr    (dcr_wr_addr),
        .dcr_wr_data    (dcr_wr_data),
        .dcr_kmu_data   (dcr_kmu_data),
        .hwq_in_ready   (hwq_in_ready),
        .dcr_out_valid  (dcr_out_valid)
    );

    // Expose output data fields
    assign out_pc           = dcr_kmu_data.pc;
    assign out_param        = dcr_kmu_data.param;
    assign out_grid_dim_x   = dcr_kmu_data.grid_dim[0];
    assign out_grid_dim_y   = dcr_kmu_data.grid_dim[1];
    assign out_grid_dim_z   = dcr_kmu_data.grid_dim[2];
    assign out_block_dim_x  = dcr_kmu_data.block_dim[0];
    assign out_block_dim_y  = dcr_kmu_data.block_dim[1];
    assign out_block_dim_z  = dcr_kmu_data.block_dim[2];

endmodule
