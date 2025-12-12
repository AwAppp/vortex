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

`ifdef EXT_F_ENABLE
`include "VX_fpu_define.vh"
`endif

`ifdef XLEN_64
    `define CSR_READ_64(addr, dst, src) \
        addr : dst = `XLEN'(src)
`else
    `define CSR_READ_64(addr, dst, src) \
        addr : dst = src[31:0]; \
        addr+12'h80 : dst = 32'(src[$bits(src)-1:32])
`endif

module VX_csr_data
import VX_gpu_pkg::*;
`ifdef EXT_F_ENABLE
import VX_fpu_pkg::*;
`endif
#(
    parameter `STRING INSTANCE_ID = "",
    parameter CORE_ID = 0
) (
    input wire                          clk,
    input wire                          reset,

    input base_dcrs_t                   base_dcrs,

`ifdef PERF_ENABLE
    input sysmem_perf_t                 sysmem_perf,
    input pipeline_perf_t               pipeline_perf,
`endif

    VX_commit_csr_if.slave              commit_csr_if,

`ifdef EXT_F_ENABLE
    VX_fpu_csr_if.slave                 fpu_csr_if [`NUM_FPU_BLOCKS],
`endif

    input wire [PERF_CTR_BITS-1:0]      cycles,
    input wire [`NUM_WARPS-1:0]         active_warps,
    input wire [`NUM_WARPS-1:0][`NUM_THREADS-1:0] thread_masks,

    VX_cta_csr_if.slave                 cta_csr_if,

    input wire                          read_enable,
    input wire [UUID_WIDTH-1:0]         read_uuid,
    input wire [NW_WIDTH-1:0]           read_wid,
    input wire [`VX_CSR_ADDR_BITS-1:0]  read_addr,
    output wire [`XLEN-1:0]             read_data_ro,
    output wire [`XLEN-1:0]             read_data_rw,

    input wire                          write_enable,
    input wire [UUID_WIDTH-1:0]         write_uuid,
    input wire [NW_WIDTH-1:0]           write_wid,
    input wire [`VX_CSR_ADDR_BITS-1:0]  write_addr,
    input wire [`XLEN-1:0]              write_data,

    input wire [`NUM_WARPS-1:0]         dkl_csr_to_core_arb_ready,
    output wire [`NUM_WARPS-1:0]        dkl_csr_level_entry_valid,
    output kmu_data_t [`NUM_WARPS-1:0]  dkl_csr_level_entry_data
);

    `UNUSED_VAR (reset)
    `UNUSED_VAR (write_data)

    // CSRs Write /////////////////////////////////////////////////////////////

    reg [`NUM_WARPS-1:0][`XLEN-1:0] mscratch;  // per-warp mscratch for kernel boot argument
    csr_cta_data_t [`NUM_WARPS-1:0] csr_ctas;
    kmu_data_t [`NUM_WARPS-1:0] csr_dkl_kde;
    reg [`NUM_WARPS-1:0] dkl_csr_level_entry_valid_reg;

    assign dkl_csr_level_entry_valid = dkl_csr_level_entry_valid_reg;
    assign dkl_csr_level_entry_data = csr_dkl_kde;


`ifdef EXT_F_ENABLE
    reg [`NUM_WARPS-1:0][INST_FRM_BITS+`FP_FLAGS_BITS-1:0] fcsr, fcsr_n;
    wire [`NUM_FPU_BLOCKS-1:0]              fpu_write_enable;
    wire [`NUM_FPU_BLOCKS-1:0][NW_WIDTH-1:0] fpu_write_wid;
    fflags_t [`NUM_FPU_BLOCKS-1:0]          fpu_write_fflags;

    for (genvar i = 0; i < `NUM_FPU_BLOCKS; ++i) begin : g_fpu_write
        assign fpu_write_enable[i] = fpu_csr_if[i].write_enable;
        assign fpu_write_wid[i]    = fpu_csr_if[i].write_wid;
        assign fpu_write_fflags[i] = fpu_csr_if[i].write_fflags;
    end

    always @(*) begin
        fcsr_n = fcsr;
        for (integer i = 0; i < `NUM_FPU_BLOCKS; ++i) begin
            if (fpu_write_enable[i]) begin
                fcsr_n[fpu_write_wid[i]][`FP_FLAGS_BITS-1:0] = fcsr[fpu_write_wid[i]][`FP_FLAGS_BITS-1:0]
                                                             | fpu_write_fflags[i];
            end
        end
        if (write_enable) begin
            case (write_addr)
                `VX_CSR_FFLAGS: fcsr_n[write_wid][`FP_FLAGS_BITS-1:0] = write_data[`FP_FLAGS_BITS-1:0];
                `VX_CSR_FRM:    fcsr_n[write_wid][INST_FRM_BITS+`FP_FLAGS_BITS-1:`FP_FLAGS_BITS] = write_data[INST_FRM_BITS-1:0];
                `VX_CSR_FCSR:   fcsr_n[write_wid] = write_data[`FP_FLAGS_BITS+INST_FRM_BITS-1:0];
            default:;
            endcase
        end
    end

    for (genvar i = 0; i < `NUM_FPU_BLOCKS; ++i) begin : g_fpu_csr_read_frm
        assign fpu_csr_if[i].read_frm = fcsr[fpu_csr_if[i].read_wid][INST_FRM_BITS+`FP_FLAGS_BITS-1:`FP_FLAGS_BITS];
    end

    always @(posedge clk) begin
        if (reset) begin
            fcsr <= '0;
        end else begin
            fcsr <= fcsr_n;
        end
    end
`endif

    initial begin
        dkl_csr_level_entry_valid_reg = '0;
    end

    always @(posedge clk) begin
        if (reset) begin
            // Initialize all per-warp mscratch values to the host kernel's startup arg
            for (integer i = 0; i < `NUM_WARPS; i++) begin
                mscratch[i] <= base_dcrs.startup_arg;
            end
            csr_ctas <= '0;
            dkl_csr_level_entry_valid_reg <= '0;
            csr_dkl_kde <= '0;
        end else begin
            // Clear valid bit if arbiter is ready (handshake complete)
            // This is placed BEFORE the write logic so that if a write happens in the same cycle,
            // the valid bit is re-asserted (last assignment wins).
            dkl_csr_level_entry_valid_reg <= dkl_csr_level_entry_valid_reg & ~dkl_csr_to_core_arb_ready;
            
            if (write_enable) begin
                `TRACE(1, ("%t: CSR_WRITE: write_enable, write_addr: %h, write_data: %h\n", $time, write_addr, write_data))
                `TRACE(1, ("%t: CSR_WRITE: dkl_csr_level_entry_valid_reg: %b\n", $time, dkl_csr_level_entry_valid_reg))
                case (write_addr)
                `ifdef EXT_F_ENABLE
                    `VX_CSR_FFLAGS,
                    `VX_CSR_FRM,
                    `VX_CSR_FCSR,
                `endif
                    `VX_CSR_SATP,
                    `VX_CSR_MSTATUS,
                    `VX_CSR_MNSTATUS,
                    `VX_CSR_MEDELEG,
                    `VX_CSR_MIDELEG,
                    `VX_CSR_MIE,
                    `VX_CSR_MTVEC,
                    `VX_CSR_MEPC,
                    `VX_CSR_PMPCFG0,
                    `VX_CSR_PMPADDR0: begin
                        // do nothing!
                    end
                    `VX_CSR_MSCRATCH: begin
                        mscratch[write_wid] <= write_data;  // write to per-warp mscratch
                    end

                    // TODO: need to the the new writes to STALL until the kernel has been sent to the arbiter
                    // Need elastic handshaking interface between the CSR and the arbiter
                    // CSR ready is dkl_ready (tied to dkl_csr_level_entry_valid_reg), need to pass in the arbiter ready signal
                    // need to decide if this can happen in the same cycle (check if the arbiter is ready and just bypass)
                    // or need to wait for new cycle and check if the arbiter is ready
                    // STATUS: STALLING THE CSR WRITE IMPLEMENTATION DEFERRED (OUT OF SCOPE FOR NOW, DON'T WORRY UNTIL EVERYTHING ELSE IS DONE)

                    // TODO: let's remove the ready bit and just assume only one kernel can be created per sm
                    // STATUS: DONE
                    // TODO: NEED TO ADD PATH THROUGH THE CSR UNIT TO THE ARBITER AND IMPLEMENT ELASTIC HANDSHAKING
                    // STATUS: DONE
                    // TODO: ADD THE SENDING OF THE KERNEL TO THE ARBITER
                    // STATUS: Need to implement logic to reset the ready signal after successful transaction.
                    `VX_CSR_DKL_PC: begin
                        csr_dkl_kde[write_wid].pc <= write_data[`XLEN-1:0];
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO PC\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_GRID_DIM_0: begin
                        csr_dkl_kde[write_wid].grid_dim[0] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO GRID DIM 0\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_GRID_DIM_1: begin
                        csr_dkl_kde[write_wid].grid_dim[1] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO GRID DIM 1\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_GRID_DIM_2: begin
                        csr_dkl_kde[write_wid].grid_dim[2] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO GRID DIM 2\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_BLOCK_DIM_0: begin
                        csr_dkl_kde[write_wid].block_dim[0] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO BLOCK DIM 0\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_BLOCK_DIM_1: begin
                        csr_dkl_kde[write_wid].block_dim[1] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO BLOCK DIM 1\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_BLOCK_DIM_2: begin
                        csr_dkl_kde[write_wid].block_dim[2] <= write_data;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO BLOCK DIM 2\n", $time, write_wid))
                    end
                    `VX_CSR_DKL_PARAM: begin
                        csr_dkl_kde[write_wid].param <= write_data[`XLEN-1:0];
                        dkl_csr_level_entry_valid_reg[write_wid] <= 1;
                        `TRACE(1, ("%t: WARP %d CSR_WRITE: WROTE TO PARAM\n", $time, write_wid))
                    end
                    // `VX_CSR_DKL_READY: begin
                    //     csr_dkl_kde[write_wid].ready <= write_data[0];
                    //     dkl_csr_level_entry_valid_reg <= write_data[0];
                    // end
                    default: begin
                        `ASSERT(0, ("%t: *** %s invalid CSR write address: %0h (#%0d)", $time, INSTANCE_ID, write_addr, write_uuid));
                    end
                endcase
            end
            if (cta_csr_if.valid) begin
                csr_ctas[cta_csr_if.wid] <= cta_csr_if.data;
                // Update per-warp mscratch with the kernel's param from CTA dispatch
                mscratch[cta_csr_if.wid] <= cta_csr_if.data.param;
                `TRACE(1, ("%t: CTA_CSR: wid=%0d, setting mscratch=0x%0h\n", $time, cta_csr_if.wid, cta_csr_if.data.param))
            end
        end
    end

    // CSRs read //////////////////////////////////////////////////////////////

    reg [`XLEN-1:0] read_data_ro_w;
    reg [`XLEN-1:0] read_data_rw_w;
    reg read_addr_valid_w;

    always @(*) begin
        read_data_ro_w    = '0;
        read_data_rw_w    = '0;
        read_addr_valid_w = 1;
        case (read_addr)
            `VX_CSR_MVENDORID  : read_data_ro_w = `XLEN'(`VENDOR_ID);
            `VX_CSR_MARCHID    : read_data_ro_w = `XLEN'(`ARCHITECTURE_ID);
            `VX_CSR_MIMPID     : read_data_ro_w = `XLEN'(`IMPLEMENTATION_ID);
            `VX_CSR_MISA       : read_data_ro_w = `XLEN'({2'(`CLOG2(`XLEN/16)), 30'(`MISA_STD)});
        `ifdef EXT_F_ENABLE
            `VX_CSR_FFLAGS     : read_data_rw_w = `XLEN'(fcsr[read_wid][`FP_FLAGS_BITS-1:0]);
            `VX_CSR_FRM        : read_data_rw_w = `XLEN'(fcsr[read_wid][INST_FRM_BITS+`FP_FLAGS_BITS-1:`FP_FLAGS_BITS]);
            `VX_CSR_FCSR       : read_data_rw_w = `XLEN'(fcsr[read_wid]);
        `endif
            `VX_CSR_MSCRATCH   : read_data_rw_w = mscratch[read_wid];  // read from per-warp mscratch

            `VX_CSR_CTA_X      : read_data_rw_w = csr_ctas[read_wid].cta_x;
            `VX_CSR_CTA_Y      : read_data_rw_w = csr_ctas[read_wid].cta_y;
            `VX_CSR_CTA_Z      : read_data_rw_w = csr_ctas[read_wid].cta_z;
            `VX_CSR_CTA_ID     : read_data_rw_w = csr_ctas[read_wid].cta_id;            

            `VX_CSR_WARP_ID    : read_data_ro_w = `XLEN'(read_wid);
            `VX_CSR_CORE_ID    : read_data_ro_w = `XLEN'(CORE_ID);
            `VX_CSR_ACTIVE_THREADS: read_data_ro_w = `XLEN'(thread_masks[read_wid]);
            `VX_CSR_ACTIVE_WARPS: read_data_ro_w = `XLEN'(active_warps);
            `VX_CSR_NUM_THREADS: read_data_ro_w = `XLEN'(`NUM_THREADS);
            `VX_CSR_NUM_WARPS  : read_data_ro_w = `XLEN'(`NUM_WARPS);
            `VX_CSR_NUM_CORES  : read_data_ro_w = `XLEN'(`NUM_CORES * `NUM_CLUSTERS);
            `VX_CSR_LOCAL_MEM_BASE: read_data_ro_w = `XLEN'(`LMEM_BASE_ADDR);

            `CSR_READ_64(`VX_CSR_MCYCLE, read_data_ro_w, cycles);

            `VX_CSR_MPM_RESERVED : read_data_ro_w = 'x;
            `VX_CSR_MPM_RESERVED_H : read_data_ro_w = 'x;

            `CSR_READ_64(`VX_CSR_MINSTRET, read_data_ro_w, commit_csr_if.instret);

            `VX_CSR_SATP,
            `VX_CSR_MSTATUS,
            `VX_CSR_MNSTATUS,
            `VX_CSR_MEDELEG,
            `VX_CSR_MIDELEG,
            `VX_CSR_MIE,
            `VX_CSR_MTVEC,
            `VX_CSR_MEPC,
            `VX_CSR_PMPCFG0,
            `VX_CSR_PMPADDR0 : read_data_ro_w = `XLEN'(0);

            // DKL CSR reads
            `VX_CSR_DKL_PC         : read_data_rw_w = csr_dkl_kde[read_wid].pc;
            `VX_CSR_DKL_GRID_DIM_0 : read_data_rw_w = csr_dkl_kde[read_wid].grid_dim[0];
            `VX_CSR_DKL_GRID_DIM_1 : read_data_rw_w = csr_dkl_kde[read_wid].grid_dim[1];
            `VX_CSR_DKL_GRID_DIM_2 : read_data_rw_w = csr_dkl_kde[read_wid].grid_dim[2];
            `VX_CSR_DKL_BLOCK_DIM_0: read_data_rw_w = csr_dkl_kde[read_wid].block_dim[0];
            `VX_CSR_DKL_BLOCK_DIM_1: read_data_rw_w = csr_dkl_kde[read_wid].block_dim[1];
            `VX_CSR_DKL_BLOCK_DIM_2: read_data_rw_w = csr_dkl_kde[read_wid].block_dim[2];
            `VX_CSR_DKL_PARAM      : read_data_rw_w = csr_dkl_kde[read_wid].param;
            `VX_CSR_DKL_READY      : read_data_rw_w = `XLEN'(dkl_csr_level_entry_valid_reg[read_wid]);

            default: begin
                read_addr_valid_w = 0;
                if ((read_addr >= `VX_CSR_MPM_USER   && read_addr < (`VX_CSR_MPM_USER + 32))
                 || (read_addr >= `VX_CSR_MPM_USER_H && read_addr < (`VX_CSR_MPM_USER_H + 32))) begin
                    read_addr_valid_w = 1;
                `ifdef PERF_ENABLE
                    case (base_dcrs.mpm_class)
                    `VX_DCR_MPM_CLASS_CORE: begin
                        case (read_addr)
                        // PERF: pipeline
                        `CSR_READ_64(`VX_CSR_MPM_SCHED_ID, read_data_ro_w, pipeline_perf.sched.idles);
                        `CSR_READ_64(`VX_CSR_MPM_SCHED_ST, read_data_ro_w, pipeline_perf.sched.stalls);
                        `CSR_READ_64(`VX_CSR_MPM_IBUF_ST, read_data_ro_w, pipeline_perf.issue.ibf_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_ST, read_data_ro_w, pipeline_perf.issue.scb_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_OPDS_ST, read_data_ro_w, pipeline_perf.issue.opd_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_ALU, read_data_ro_w, pipeline_perf.issue.units_uses[EX_ALU]);
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_LSU, read_data_ro_w, pipeline_perf.issue.units_uses[EX_LSU]);
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_SFU, read_data_ro_w, pipeline_perf.issue.units_uses[EX_SFU]);
                    `ifdef EXT_F_ENABLE
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_FPU, read_data_ro_w, pipeline_perf.issue.units_uses[EX_FPU]);
                    `endif
                    `ifdef EXT_TCU_ENABLE
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_TCU, read_data_ro_w, pipeline_perf.issue.units_uses[EX_TCU]);
                    `endif
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_CSRS, read_data_ro_w, pipeline_perf.issue.sfu_uses[SFU_CSRS]);
                        `CSR_READ_64(`VX_CSR_MPM_SCRB_WCTL, read_data_ro_w, pipeline_perf.issue.sfu_uses[SFU_WCTL]);
                        // PERF: memory
                        `CSR_READ_64(`VX_CSR_MPM_IFETCHES, read_data_ro_w, pipeline_perf.ifetches);
                        `CSR_READ_64(`VX_CSR_MPM_LOADS, read_data_ro_w, pipeline_perf.loads);
                        `CSR_READ_64(`VX_CSR_MPM_STORES, read_data_ro_w, pipeline_perf.stores);
                        `CSR_READ_64(`VX_CSR_MPM_IFETCH_LT, read_data_ro_w, pipeline_perf.ifetch_latency);
                        `CSR_READ_64(`VX_CSR_MPM_LOAD_LT, read_data_ro_w, pipeline_perf.load_latency);
                        default:;
                        endcase
                    end
                    `VX_DCR_MPM_CLASS_MEM: begin
                        case (read_addr)
                        // PERF: icache
                        `CSR_READ_64(`VX_CSR_MPM_ICACHE_READS, read_data_ro_w, sysmem_perf.icache.reads);
                        `CSR_READ_64(`VX_CSR_MPM_ICACHE_MISS_R, read_data_ro_w, sysmem_perf.icache.read_misses);
                        `CSR_READ_64(`VX_CSR_MPM_ICACHE_MSHR_ST, read_data_ro_w, sysmem_perf.icache.mshr_stalls);
                        // PERF: dcache
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_READS, read_data_ro_w, sysmem_perf.dcache.reads);
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_WRITES, read_data_ro_w, sysmem_perf.dcache.writes);
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_MISS_R, read_data_ro_w, sysmem_perf.dcache.read_misses);
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_MISS_W, read_data_ro_w, sysmem_perf.dcache.write_misses);
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_BANK_ST, read_data_ro_w, sysmem_perf.dcache.bank_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_DCACHE_MSHR_ST, read_data_ro_w, sysmem_perf.dcache.mshr_stalls);
                        // PERF: lmem
                        `CSR_READ_64(`VX_CSR_MPM_LMEM_READS, read_data_ro_w, sysmem_perf.lmem.reads);
                        `CSR_READ_64(`VX_CSR_MPM_LMEM_WRITES, read_data_ro_w, sysmem_perf.lmem.writes);
                        `CSR_READ_64(`VX_CSR_MPM_LMEM_BANK_ST, read_data_ro_w, sysmem_perf.lmem.bank_stalls);
                        // PERF: l2cache
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_READS, read_data_ro_w, sysmem_perf.l2cache.reads);
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_WRITES, read_data_ro_w, sysmem_perf.l2cache.writes);
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_MISS_R, read_data_ro_w, sysmem_perf.l2cache.read_misses);
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_MISS_W, read_data_ro_w, sysmem_perf.l2cache.write_misses);
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_BANK_ST, read_data_ro_w, sysmem_perf.l2cache.bank_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_L2CACHE_MSHR_ST, read_data_ro_w, sysmem_perf.l2cache.mshr_stalls);
                        // PERF: l3cache
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_READS, read_data_ro_w, sysmem_perf.l3cache.reads);
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_WRITES, read_data_ro_w, sysmem_perf.l3cache.writes);
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_MISS_R, read_data_ro_w, sysmem_perf.l3cache.read_misses);
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_MISS_W, read_data_ro_w, sysmem_perf.l3cache.write_misses);
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_BANK_ST, read_data_ro_w, sysmem_perf.l3cache.bank_stalls);
                        `CSR_READ_64(`VX_CSR_MPM_L3CACHE_MSHR_ST, read_data_ro_w, sysmem_perf.l3cache.mshr_stalls);
                        // PERF: memory
                        `CSR_READ_64(`VX_CSR_MPM_MEM_READS, read_data_ro_w, sysmem_perf.mem.reads);
                        `CSR_READ_64(`VX_CSR_MPM_MEM_WRITES, read_data_ro_w, sysmem_perf.mem.writes);
                        `CSR_READ_64(`VX_CSR_MPM_MEM_LT, read_data_ro_w, sysmem_perf.mem.latency);
                        // PERF: coalescer
                        `CSR_READ_64(`VX_CSR_MPM_COALESCER_MISS, read_data_ro_w, sysmem_perf.coalescer.misses);
                        default:;
                        endcase
                    end
                    default:;
                    endcase
                `endif
                end
            end
        endcase
    end

    assign read_data_ro = read_data_ro_w;
    assign read_data_rw = read_data_rw_w;

    `UNUSED_VAR (base_dcrs)

    `RUNTIME_ASSERT(~read_enable || read_addr_valid_w, ("%t: *** invalid CSR read address: 0x%0h (#%0d)", $time, read_addr, read_uuid))

`ifdef PERF_ENABLE
    `UNUSED_VAR (sysmem_perf.icache);
    `UNUSED_VAR (sysmem_perf.lmem);
`endif

endmodule
