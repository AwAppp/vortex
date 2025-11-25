// Copyright 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A stream elastic buffer_r operates at full-bandwidth where fire_in and fire_out can happen simultaneously
// It has the following benefits:
// + full-bandwidth throughput
// + ready_in and ready_out are decoupled
// + data_out can be fully registered
// It has the following limitations:
// - requires two registers for storage

`include "VX_platform.vh"

`TRACING_OFF
module VX_stream_fifo #(
    // parameter DATAW    = 1,
	// parameter OUT_REG  = 0,
    // parameter PASSTHRU = 0
	parameter DATAW     = 32,
    parameter DEPTH     = 32,
    parameter ALM_FULL  = (DEPTH - 1),
    parameter ALM_EMPTY = 1,
    parameter OUT_REG   = 0,
    parameter LUTRAM    = 0,
    parameter SIZEW     = `CLOG2(DEPTH+1)
) (
    input  wire             clk,
    input  wire             reset,

	// input handshake
    input  wire             valid_in,
    output wire             ready_in,
    input  wire [DATAW-1:0] data_in,

	// output handshake
    output wire [DATAW-1:0] data_out,
    input  wire             ready_out,
    output wire             valid_out

	// size state
	output wire             empty,
    output wire             alm_empty,
    output wire             full,
    output wire             alm_full,
    output wire [SIZEW-1:0] size
);
   
	`STATIC_ASSERT(ALM_FULL > 0, ("alm_full must be greater than 0!"))
    `STATIC_ASSERT(ALM_FULL < DEPTH, ("alm_full must be smaller than size!"))
    `STATIC_ASSERT(ALM_EMPTY > 0, ("alm_empty must be greater than 0!"))
    `STATIC_ASSERT(ALM_EMPTY < DEPTH, ("alm_empty must be smaller than size!"))
    `STATIC_ASSERT(`IS_POW2(DEPTH), ("depth must be a power of 2!"))
	`STATIC_ASSERT(OUT_REG == 0, ("OUT_REG == 1 case not implemented yet"))
	
    wire fifo_push_internal = valid_in && ready_in;
	wire fifo_pop_internal = ~empty && ready_out;

	assign ready_in = ~full;
	// assign valid_out = ~empty;


    VX_pending_size #(
        .SIZE      (DEPTH),
        .ALM_EMPTY (ALM_EMPTY),
        .ALM_FULL  (ALM_FULL)
    ) pending_size (
        .clk   (clk),
        .reset (reset),
        .incr  (fifo_push_internal),
        .decr  (fifo_pop_internal),
        .empty (empty),
        .full  (full),
        .alm_empty(alm_empty),
        .alm_full(alm_full),
        .size  (size)
    );

    if (DEPTH == 1) begin : g_depth_1
        `UNUSED_PARAM (OUT_REG)
        `UNUSED_PARAM (LUTRAM)

        reg [DATAW-1:0] head_r;

        always @(posedge clk) begin
            if (push) begin
                head_r <= data_in;
            end
        end

        assign data_out = head_r;

    end else begin : g_depth_n

        localparam ADDRW = `CLOG2(DEPTH);

        wire [DATAW-1:0] data_out_w;
        reg [ADDRW-1:0] rd_ptr_r;
        reg [ADDRW-1:0] wr_ptr_r;

        always @(posedge clk) begin
            if (reset) begin
                wr_ptr_r <= '0;
                rd_ptr_r <= (OUT_REG != 0) ? 1 : 0;
            end else begin
                wr_ptr_r <= wr_ptr_r + ADDRW'(fifo_push_internal);
                rd_ptr_r <= rd_ptr_r + ADDRW'(fifo_pop_internal);
            end
        end
        
        VX_dp_ram #(
            .DATAW (DATAW),
            .SIZE  (DEPTH),
            .LUTRAM (LUTRAM),
            .RDW_MODE ("W"),
            .RADDR_REG (1),
            .RADDR_RESET (1)
        ) dp_ram (
            .clk   (clk),
            .reset (reset),
            .read  (1'b1),
            .write (fifo_push_internal),
            .wren  (1'b1),
            .raddr (rd_ptr_r),
            .waddr (wr_ptr_r),
            .wdata (data_in),
            .rdata (data_out_w)
        );

        
        generate
            if (OUT_REG == 0) begin : unregistered_output
                assign valid_out = ~empty;
                assign data_out = data_out_w;
            end else begin: registered_output
                VX_stream_buffer #(
                    .DATAW    (DATAW),
                    .OUT_REG  (1) // Set to 1 for fully registered output
                ) out_buffer (
                    .clk      (clk),
                    .reset    (reset),
                    
                    // Connects to FIFO output
                    .valid_in (~empty),
                    .ready_in (ready_in),
                    .data_in  (data_out_w),
                    
                    // Connects to External World
                    .valid_out(valid_out),
                    .ready_out(ready_out),
                    .data_out (data_out)
                );
            end
        endgenerate
    end

endmodule
`TRACING_ON
