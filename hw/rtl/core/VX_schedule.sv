// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module VX_schedule import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter CORE_ID = 0
) (
    input wire              clk,
    input wire              reset,

`ifdef PERF_ENABLE
    output sched_perf_t     sched_perf,
`endif

    // configuration
    input base_dcrs_t       base_dcrs,

    // inputsdecode_if
    VX_warp_ctl_if.slave    warp_ctl_if,
    VX_branch_ctl_if.slave  branch_ctl_if [`NUM_ALU_BLOCKS],
    VX_decode_sched_if.slave decode_sched_if,
    VX_issue_sched_if.slave issue_sched_if[`ISSUE_WIDTH],
    VX_commit_sched_if.slave commit_sched_if,

    // outputs
    VX_schedule_if.master   schedule_if,
`ifdef GBAR_ENABLE
    VX_gbar_bus_if.master   gbar_bus_if,
`endif
    VX_sched_csr_if.master  sched_csr_if,

    // status
    output wire             busy,

    // Distributed task
    VX_kmu_bus_if.slave     task_in
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_PARAM (CORE_ID)

    reg [`NUM_WARPS-1:0] active_warps, active_warps_n; // updated when a warp is activated or disabled
    reg [`NUM_WARPS-1:0] stalled_warps, stalled_warps_n;  // set when branch/gpgpu instructions are issued

    reg [`NUM_WARPS-1:0][`NUM_THREADS-1:0] thread_masks, thread_masks_n;
    reg [`NUM_WARPS-1:0][PC_BITS-1:0] warp_pcs, warp_pcs_n;

    wire [NW_WIDTH-1:0]     schedule_wid;
    wire [`NUM_THREADS-1:0] schedule_tmask;
    wire [PC_BITS-1:0]      schedule_pc;
    wire                    schedule_valid;
    wire                    schedule_ready;

    // split/join
    wire                    join_valid;
    wire                    join_is_dvg;
    wire                    join_is_else;
    wire [NW_WIDTH-1:0]     join_wid;
    wire [`NUM_THREADS-1:0] join_tmask;
    wire [PC_BITS-1:0]      join_pc;

    reg [PERF_CTR_BITS-1:0] cycles;

    wire schedule_fire = schedule_valid && schedule_ready;
    wire schedule_if_fire = schedule_if.valid && schedule_if.ready;

    VX_cta_dispatch cta_dispatcher(
        .clk    (clk),
        .reset  (reset),
        .task_in        (task_in),
        .active_warps   ('0),
        .num_warps      ('0),
        .sched_warps    ('x),
        .cta_x          (sched_csr_if.cta_x),
        .cta_y          (sched_csr_if.cta_y),
        .cta_z          (sched_csr_if.cta_z),
        .cta_id         (sched_csr_if.cta_id),
        .pc             ('x),
        .param          ('x),
        .remain_masks   ('x)
    );

    // branch
    wire [`NUM_ALU_BLOCKS-1:0]               branch_valid;
    wire [`NUM_ALU_BLOCKS-1:0][NW_WIDTH-1:0] branch_wid;
    wire [`NUM_ALU_BLOCKS-1:0]               branch_taken;
    wire [`NUM_ALU_BLOCKS-1:0][PC_BITS-1:0]  branch_dest;
    for (genvar i = 0; i < `NUM_ALU_BLOCKS; ++i) begin : g_branch_init
        assign branch_valid[i] = branch_ctl_if[i].valid;
        assign branch_wid[i]   = branch_ctl_if[i].wid;
        assign branch_taken[i] = branch_ctl_if[i].taken;
        assign branch_dest[i]  = branch_ctl_if[i].dest;
    end

    // barriers
    reg [`NUM_BARRIERS-1:0][`NUM_WARPS-1:0] barrier_masks, barrier_masks_n;
    reg [`NUM_BARRIERS-1:0][NW_WIDTH-1:0] barrier_ctrs, barrier_ctrs_n;
    reg [`NUM_WARPS-1:0] barrier_stalls, barrier_stalls_n;
    reg [`NUM_WARPS-1:0] curr_barrier_mask_p1;
`ifdef GBAR_ENABLE
    reg gbar_req_valid;
    reg [NB_WIDTH-1:0] gbar_req_id;
    reg [NC_WIDTH-1:0] gbar_req_size_m1;
`endif

    // wspawn
    wspawn_t wspawn;
    reg [NW_WIDTH-1:0] wspawn_wid;
    reg is_single_warp;

    wire [`CLOG2(`NUM_WARPS+1)-1:0] active_warps_cnt;
    `POP_COUNT(active_warps_cnt, active_warps);

    always @(*) begin
        active_warps_n  = active_warps;
        stalled_warps_n = stalled_warps;
        thread_masks_n  = thread_masks;
        barrier_masks_n = barrier_masks;
        barrier_ctrs_n  = barrier_ctrs;
        barrier_stalls_n= barrier_stalls;
        warp_pcs_n      = warp_pcs;

        // decode unlock
        if (decode_sched_if.valid && decode_sched_if.unlock) begin
            stalled_warps_n[decode_sched_if.wid] = 0;
        end

        // CSR unlock
        if (sched_csr_if.unlock_warp) begin
            stalled_warps_n[sched_csr_if.unlock_wid] = 0;
        end

        // wspawn handling
        if (wspawn.valid && is_single_warp) begin
            active_warps_n |= wspawn.wmask;
            for (integer i = 0; i < `NUM_WARPS; ++i) begin
                if (wspawn.wmask[i]) begin
                    thread_masks_n[i][0] = 1;
                    warp_pcs_n[i] = wspawn.pc;
                end
            end
            stalled_warps_n[wspawn_wid] = 0; // unlock warp
        end

        // TMC handling
        if (warp_ctl_if.valid && warp_ctl_if.tmc.valid) begin
            active_warps_n[warp_ctl_if.wid]  = (warp_ctl_if.tmc.tmask != 0);
            thread_masks_n[warp_ctl_if.wid]  = warp_ctl_if.tmc.tmask;
            stalled_warps_n[warp_ctl_if.wid] = 0; // unlock warp
        end

        // split handling
        if (warp_ctl_if.valid && warp_ctl_if.split.valid) begin
            if (warp_ctl_if.split.is_dvg) begin
                thread_masks_n[warp_ctl_if.wid] = warp_ctl_if.split.then_tmask;
            end
            stalled_warps_n[warp_ctl_if.wid] = 0; // unlock warp
        end

        // join handling
        if (join_valid) begin
            if (join_is_dvg) begin
                if (join_is_else) begin
                    warp_pcs_n[join_wid] = join_pc;
                end
                thread_masks_n[join_wid] = join_tmask;
            end
            stalled_warps_n[join_wid] = 0; // unlock warp
        end

        // barrier handling
        curr_barrier_mask_p1 = barrier_masks[warp_ctl_if.barrier.id];
        curr_barrier_mask_p1[warp_ctl_if.wid] = 1;
        if (warp_ctl_if.valid && warp_ctl_if.barrier.valid) begin
            if (~warp_ctl_if.barrier.is_noop) begin
                if (~warp_ctl_if.barrier.is_global
                 && (barrier_ctrs[warp_ctl_if.barrier.id] == NW_WIDTH'(warp_ctl_if.barrier.size_m1))) begin
                    barrier_ctrs_n[warp_ctl_if.barrier.id] = '0; // reset barrier counter
                    barrier_masks_n[warp_ctl_if.barrier.id] = '0; // reset barrier mask
                    stalled_warps_n &= ~barrier_masks[warp_ctl_if.barrier.id]; // unlock warps
                    stalled_warps_n[warp_ctl_if.wid] = 0; // unlock warp
                end else begin
                    barrier_ctrs_n[warp_ctl_if.barrier.id] = barrier_ctrs[warp_ctl_if.barrier.id] + NW_WIDTH'(1);
                    barrier_masks_n[warp_ctl_if.barrier.id] = curr_barrier_mask_p1;
                end
            end else begin
                stalled_warps_n[warp_ctl_if.wid] = 0; // unlock warp
            end
        end

    `ifdef GBAR_ENABLE
        if (gbar_bus_if.rsp_valid && (gbar_req_id == gbar_bus_if.rsp_data.id)) begin
            barrier_ctrs_n[warp_ctl_if.barrier.id] = '0; // reset barrier counter
            barrier_masks_n[gbar_bus_if.rsp_data.id] = '0; // reset barrier mask
            stalled_warps_n = '0; // unlock all warps
        end
    `endif

        // Branch handling
        for (integer i = 0; i < `NUM_ALU_BLOCKS; ++i) begin
            if (branch_valid[i]) begin
                if (branch_taken[i]) begin
                    warp_pcs_n[branch_wid[i]] = branch_dest[i];
                end
                stalled_warps_n[branch_wid[i]] = 0; // unlock warp
            end
        end

        // stall the warp until decode stage
        if (schedule_fire) begin
            stalled_warps_n[schedule_wid] = 1;
        end

        // advance PC
        if (schedule_if_fire) begin
            warp_pcs_n[schedule_if.data.wid] = schedule_if.data.PC + from_fullPC(`XLEN'(4));
        end
    end

    `UNUSED_VAR (base_dcrs)

    always @(posedge clk) begin
        if (reset) begin
            barrier_masks   <= '0;
            barrier_ctrs    <= '0;
        `ifdef GBAR_ENABLE
            gbar_req_valid  <= 0;
        `endif
            stalled_warps   <= '0;
            warp_pcs        <= '0;
            active_warps    <= '0;
            thread_masks    <= '0;
            barrier_stalls  <= '0;
            cycles          <= '0;
            wspawn.valid    <=  0;

            // activate first warp
            warp_pcs[0]     <= from_fullPC(base_dcrs.startup_addr);
            warp_pcs[1]     <= from_fullPC(base_dcrs.startup_addr);
            warp_pcs[2]     <= from_fullPC(base_dcrs.startup_addr);
            warp_pcs[3]     <= from_fullPC(base_dcrs.startup_addr);
            active_warps[0] <= 1;
            active_warps[1] <= 1;
            active_warps[2] <= 1;
            active_warps[3] <= 1;
            thread_masks[0][0] <= 1;
            thread_masks[0][1] <= 1;
            thread_masks[1][0] <= 1;
            thread_masks[2][0] <= 1;
            thread_masks[3][0] <= 1;
            is_single_warp  <= 1;
        end else begin
            active_warps   <= active_warps_n;
            stalled_warps  <= stalled_warps_n;
            thread_masks   <= thread_masks_n;
            warp_pcs       <= warp_pcs_n;
            barrier_masks  <= barrier_masks_n;
            barrier_ctrs   <= barrier_ctrs_n;
            barrier_stalls <= barrier_stalls_n;
            is_single_warp <= (active_warps_cnt == $bits(active_warps_cnt)'(1));

            // wspawn handling
            if (warp_ctl_if.valid && warp_ctl_if.wspawn.valid) begin
                wspawn.valid <= 1;
                wspawn.wmask <= warp_ctl_if.wspawn.wmask;
                wspawn.pc    <= warp_ctl_if.wspawn.pc;
                wspawn_wid   <= warp_ctl_if.wid;
            end
            if (wspawn.valid && is_single_warp) begin
                wspawn.valid <= 0;
            end

            // global barrier scheduling
        `ifdef GBAR_ENABLE
            if (warp_ctl_if.valid && warp_ctl_if.barrier.valid
             && warp_ctl_if.barrier.is_global
             && !warp_ctl_if.barrier.is_noop
             && (curr_barrier_mask_p1 == active_warps)) begin
                gbar_req_valid <= 1;
                gbar_req_id <= warp_ctl_if.barrier.id;
                gbar_req_size_m1 <= NC_WIDTH'(warp_ctl_if.barrier.size_m1);
            end
            if (gbar_bus_if.req_valid && gbar_bus_if.req_ready) begin
                gbar_req_valid <= 0;
            end
        `endif

            if (busy) begin
                cycles <= cycles + 1;
            end
        end
    end

    // barrier handling

`ifdef GBAR_ENABLE
    assign gbar_bus_if.req_valid        = gbar_req_valid;
    assign gbar_bus_if.req_data.id      = gbar_req_id;
    assign gbar_bus_if.req_data.size_m1 = gbar_req_size_m1;
    assign gbar_bus_if.req_data.core_id = NC_WIDTH'(CORE_ID % `NUM_CORES);
`endif

    // split/join handling

    VX_split_join #(
        .INSTANCE_ID (`SFORMATF(("%s-splitjoin", INSTANCE_ID))),
        .OUT_REG     (1)
    ) split_join (
        .clk        (clk),
        .reset      (reset),
        .valid      (warp_ctl_if.valid),
        .wid        (warp_ctl_if.wid),
        .split      (warp_ctl_if.split),
        .sjoin      (warp_ctl_if.sjoin),
        .join_valid (join_valid),
        .join_is_dvg(join_is_dvg),
        .join_is_else(join_is_else),
        .join_wid   (join_wid),
        .join_tmask (join_tmask),
        .join_pc    (join_pc),
        .stack_wid  (warp_ctl_if.dvstack_wid),
        .stack_ptr  (warp_ctl_if.dvstack_ptr)
    );

    // schedule the next ready warp

    wire [`NUM_WARPS-1:0] ready_warps = active_warps & ~stalled_warps;

    VX_priority_encoder #(
        .N (`NUM_WARPS)
    ) wid_select (
        .data_in   (ready_warps),
        .index_out (schedule_wid),
        .valid_out (schedule_valid),
        `UNUSED_PIN (onehot_out)
    );

    wire [`NUM_WARPS-1:0][(`NUM_THREADS + PC_BITS)-1:0] schedule_data;
    for (genvar i = 0; i < `NUM_WARPS; ++i) begin : g_schedule_data
        assign schedule_data[i] = {thread_masks[i], warp_pcs[i]};
    end

    assign {schedule_tmask, schedule_pc} = {
        schedule_data[schedule_wid][(`NUM_THREADS + PC_BITS)-1:(`NUM_THREADS + PC_BITS)-4],
        schedule_data[schedule_wid][(`NUM_THREADS + PC_BITS)-5:0]
    };

    wire [UUID_WIDTH-1:0] instr_uuid;
`ifdef UUID_ENABLE
    VX_uuid_gen #(
        .CORE_ID (CORE_ID)
    ) uuid_gen (
        .clk   (clk),
        .reset (reset),
        .incr  (schedule_fire),
        .wid   (schedule_wid),
        .uuid  (instr_uuid)
    );
`else
    assign instr_uuid = '0;
`endif

    VX_elastic_buffer #(
        .DATAW (`NUM_THREADS + PC_BITS + NW_WIDTH + UUID_WIDTH),
        .SIZE  (2),  // need to buffer out ready_in
        .OUT_REG (1) // should be registered for BRAM acces in fetch unit
    ) out_buf (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (schedule_valid),
        .ready_in  (schedule_ready),
        .data_in   ({schedule_tmask, schedule_pc, schedule_wid, instr_uuid}),
        .data_out  ({schedule_if.data.tmask, schedule_if.data.PC, schedule_if.data.wid, schedule_if.data.uuid}),
        .valid_out (schedule_if.valid),
        .ready_out (schedule_if.ready)
    );

    // Track pending instructions per warp

    wire [`NUM_WARPS-1:0] pending_warp_empty;
    wire [`NUM_WARPS-1:0] pending_warp_alm_empty;

    for (genvar i = 0; i < `NUM_WARPS; ++i) begin : g_pending_sizes

        localparam isw = wid_to_isw(i);
        localparam wis = wid_to_wis(i);

        VX_pending_size #(
            .SIZE      (4096),
            .ALM_EMPTY (1)
        ) counter (
            .clk       (clk),
            .reset     (reset),
            .incr      (issue_sched_if[isw].valid && (issue_sched_if[isw].wis == wis)),
            .decr      (commit_sched_if.committed_warps[i]),
            .empty     (pending_warp_empty[i]),
            .alm_empty (pending_warp_alm_empty[i]),
            `UNUSED_PIN (full),
            `UNUSED_PIN (alm_full),
            `UNUSED_PIN (size)
        );
	end

    assign sched_csr_if.alm_empty = pending_warp_alm_empty[sched_csr_if.alm_empty_wid];

    wire no_pending_instr = (& pending_warp_empty);

    `BUFFER_EX(busy, (active_warps != 0 || ~no_pending_instr), 1'b1, 1, 1);

    // export CSRs
    assign sched_csr_if.cycles = cycles;
    assign sched_csr_if.active_warps = active_warps;
    assign sched_csr_if.thread_masks = thread_masks;

   // timeout handling
    reg [31:0] timeout_ctr;
    reg timeout_enable;
    always @(posedge clk) begin
        if (reset) begin
            timeout_ctr    <= '0;
            timeout_enable <= 0;
        end else begin
            if (decode_sched_if.valid && decode_sched_if.unlock) begin
                timeout_enable <= 1;
            end
            if (timeout_enable && active_warps !=0 && active_warps == stalled_warps) begin
                timeout_ctr <= timeout_ctr + 1;
            end else if (active_warps == 0 || active_warps != stalled_warps) begin
                timeout_ctr <= '0;
            end
        end
    end
    `RUNTIME_ASSERT(timeout_ctr < STALL_TIMEOUT, ("%t: *** %s timeout: stalled_warps=%b", $time, INSTANCE_ID, stalled_warps))

`ifdef PERF_ENABLE
    reg [PERF_CTR_BITS-1:0] perf_sched_idles;
    reg [PERF_CTR_BITS-1:0] perf_sched_stalls;

    wire schedule_idle = ~schedule_valid;
    wire schedule_stall = schedule_if.valid && ~schedule_if.ready;

    always @(posedge clk) begin
        if (reset) begin
            perf_sched_idles  <= '0;
            perf_sched_stalls <= '0;
        end else begin
            perf_sched_idles  <= perf_sched_idles + PERF_CTR_BITS'(schedule_idle);
            perf_sched_stalls <= perf_sched_stalls + PERF_CTR_BITS'(schedule_stall);
        end
    end

    assign sched_perf.idles = perf_sched_idles;
    assign sched_perf.stalls = perf_sched_stalls;
`endif

`ifdef DBG_TRACE_PIPELINE
    always @(posedge clk) begin
        if (schedule_fire) begin
            `TRACE(1, ("%t: %s: wid=%0d, PC=0x%0h, tmask=%b (#%0d)\n", $time, INSTANCE_ID, schedule_wid, to_fullPC(schedule_pc), schedule_tmask, instr_uuid))
        end
    end
`endif

endmodule
