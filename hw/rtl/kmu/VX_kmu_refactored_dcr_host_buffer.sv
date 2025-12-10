`include "VX_define.vh"

module VX_kmu_refactored_dcr_host_buffer import VX_gpu_pkg::*; (
    input wire clk,
    input wire reset,

    // DCR write request
    input  wire                             dcr_wr_valid,
    input  wire [VX_DCR_ADDR_WIDTH-1:0]     dcr_wr_addr,
    input  wire [VX_DCR_DATA_WIDTH-1:0]     dcr_wr_data,
    
    output kmu_data_t                       dcr_kmu_data,
    input wire                              hwq_in_ready,
    output wire                             dcr_out_valid,

    
   );

    kmu_data_t kmu_data;
    reg [7:0] fields_ready;
    
    assign dcr_out_valid = &fields_ready;
    assign dcr_kmu_data = kmu_data;

    always @ (posedge clk) begin
        if (reset) begin
            kmu_data <= '0;
            fields_ready <= '0;
        end 
        if (dcr_out_valid && hwq_in_ready) begin
            fields_ready <= 0;
        end
        if (dcr_wr_valid) begin
            case(dcr_wr_addr)
                // PC
                `VX_DCR_BASE_STARTUP_ADDR0: begin
                    kmu_data.pc <= dcr_wr_data;
                    fields_ready[0] <= 1'b1;
                end

                // PARAM
                `VX_DCR_BASE_STARTUP_ARG0: begin
                    kmu_data.param <= dcr_wr_data;
                    fields_ready[1] <= 1'b1;
                end

                // Grid_dim
                `VX_DCR_BASE_GRID_DIM0: begin
                    kmu_data.grid_dim[0] <= dcr_wr_data;
                    fields_ready[2] <= 1'b1;
                end
                `VX_DCR_BASE_GRID_DIM1: begin
                    kmu_data.grid_dim[1] <= dcr_wr_data;
                    fields_ready[3] <= 1'b1;
                end
                `VX_DCR_BASE_GRID_DIM2: begin
                    kmu_data.grid_dim[2] <= dcr_wr_data;
                    fields_ready[4] <= 1'b1;
                end

                // Block_dim
                `VX_DCR_BASE_BLOCK_DIM0: begin
                    kmu_data.block_dim[0] <= dcr_wr_data;
                    fields_ready[5] <= 1'b1;
                end
                `VX_DCR_BASE_BLOCK_DIM1: begin
                    kmu_data.block_dim[1] <= dcr_wr_data;
                    fields_ready[6] <= 1'b1;
                end
                `VX_DCR_BASE_BLOCK_DIM2: begin
                    kmu_data.block_dim[2] <= dcr_wr_data;
                    fields_ready[7] <= 1'b1;
                end
                // `VX_DCR_BASE_SMEM_SIZE: begin
                //     smem_size <= dcr_wr_data;
                // end

                default: begin
                    // `ASSERT(0, ("%t: invalid DCR write address: %0h", $time, dcr_bus_if.write_addr));
                end
            endcase
            
        end



        
    end

endmodule

