`include "VX_define.vh"


module VX_kmu_refactored_dcr_kd import VX_gpu_pkg::*; (
    input wire clk,
    input wire reset,

    input kmu_data_t hwq_data,
    input wire hwq_data_valid,
    output wire kmu_kd_ready,
    VX_kmu_bus_if.master                 kmu_bus_out[1],
    output                                    reg start //TODO: NOT USED, REMOVE IF NEEDED
);

    kmu_data_t kmu_data;
    assign kmu_data = hwq_data;
    /* verilator lint_off PROCASSINIT */
    logic[31:0] smem_size = 0;
    /* verilator lint_on PROCASSINIT */
    `UNUSED_VAR(smem_size);

    // Internal counters for CTA distribution
    logic[31:0] counter_x, counter_y, counter_z, counter_id;

    // Thread and warp calculation
    logic[31:0] total_threads;
    logic[31:0] total_warps;

    // State for kmu bus handshake
    logic all_cta_sent;

    // the remaining mask
    logic[`NUM_THREADS-1:0] remain_mask;

    // Calculate total threads and warps
    always_comb begin
        total_threads = `MAX(kmu_data.block_dim[0], 1)
                      * `MAX(kmu_data.block_dim[1], 1)
                      * `MAX(kmu_data.block_dim[2], 1);

        total_warps = total_threads / `NUM_THREADS;
        if (total_warps * `NUM_THREADS < total_threads) begin
            total_warps++;
        end

        if (total_warps * `NUM_THREADS == total_threads) begin
            remain_mask = {`NUM_THREADS{1'b1}};
        end else begin
            remain_mask = {`NUM_THREADS{1'b1}};
            remain_mask = remain_mask >> (total_warps * `NUM_THREADS - total_threads);
        end
    end

    // CTA distribution state machine
    always_ff @(posedge clk) begin
        // if reset, reset all the counters
        // not all_cta_sent
        // bus out is not valid
        // kmu_kd is ready for next data
        if (reset) begin
            counter_x    <= 0;
            counter_y    <= 0;
            counter_z    <= 0;
            counter_id   <= 0;
            all_cta_sent <= 0;
            kmu_bus_out[0].req_valid <= 0;
            kmu_kd_ready <= 1'b1;
        end else if (all_cta_sent) begin
            kmu_kd_ready <= 1'b1; // ready for next data, skip current cycle
        end else begin
            // If all CTAs sent, keep valid low
            if (all_cta_sent) begin
                kmu_bus_out[0].req_valid <= 0;
            end else begin
                // If not currently valid, prepare next CTA
                if (!kmu_bus_out[0].req_valid) begin
                    // Prepare and send one CTA block
                    kmu_bus_out[0].req_data.num_warps    <= total_warps;
                    kmu_bus_out[0].req_data.start_pc     <= kmu_data.pc;
                    kmu_bus_out[0].req_data.param        <= kmu_data.param;
                    kmu_bus_out[0].req_data.cta_x        <= counter_x;
                    kmu_bus_out[0].req_data.cta_y        <= counter_y;
                    kmu_bus_out[0].req_data.cta_z        <= counter_z;
                    kmu_bus_out[0].req_data.cta_id       <= counter_id;
                    kmu_bus_out[0].req_data.remain_mask  <= remain_mask;
                    kmu_bus_out[0].req_valid             <= 1;
                end
                // Advance to next CTA block only after handshake
                if (kmu_bus_out[0].req_valid && kmu_bus_out[0].req_ready) begin
                    // Advance counters
                    counter_z  <= counter_z + 1;
                    counter_id <= counter_id + 1;
                    if (counter_z + 1 >= kmu_data.grid_dim[2]) begin
                        counter_z <= 0;
                        counter_y <= counter_y + 1;
                        if (counter_y + 1 >= kmu_data.grid_dim[1]) begin
                            counter_y <= 0;
                            counter_x <= counter_x + 1;
                            if (counter_x + 1 >= kmu_data.grid_dim[0]) begin
                                all_cta_sent <= 1;
                            end
                        end
                    end
                    // Deassert valid until next CTA is prepared
                    kmu_bus_out[0].req_valid <= 0;
                end
            end
        end

        // if ready for next data and next data is valid, reset all the counters
        // and prepare for next data
        // TODO: this logic can be a lot cleaner
        if (hwq_data_valid && kmu_kd_ready) begin
            counter_x <= '0;
            counter_y <= '0;
            counter_z <= '0;
            counter_id <= '0;
            kmu_kd_ready <= 1'b0;
            kmu_bus_out[0].req_valid <= 0;
            all_cta_sent <= 0;
        end
    end

    // Start signal logic (optional, can be customized)
    always_ff @(posedge clk) begin
        // if (start) begin
        //     start <= '0;
        // end
        if (reset) begin
            start <= '1;
        end else begin
            start <= '0;
        end
    end
    // always @ (posedge clk) begin
    //     if (reset) begin
    //         counter_x <= '0;
    //         counter_y <= '0;
    //         counter_z <= '0;
    //         counter_id <= '0;
    //         kmu_kd_ready <= 1'b1;
    //     end else if (cur_x == -1) begin
    //         kmu_kd_ready <= 1'b1;
    //     end else begin
    //         counter_x <= cur_x;
    //         counter_y <= cur_y;
    //         counter_z <= cur_z;
    //         counter_id <= cur_id;
    //     end

    //     if (hwq_data_valid && kmu_kd_ready) begin
    //         counter_x <= '0;
    //         counter_y <= '0;
    //         counter_z <= '0;
    //         counter_id <= '0;
    //         kmu_kd_ready <= 1'b1;
    //     end
    // end
endmodule



// module VX_kmu_refactored_dcr_kd import VX_gpu_pkg::*; (
//     input wire clk,
//     input wire reset,

//     input kmu_data_t hwq_data,
//     input wire hwq_data_valid,
//     output wire kmu_kd_ready,
//     VX_kmu_bus_if.master                 kmu_bus_out[1]
// );

//     kmu_data_t kmu_data;
//     assign kmu_data = hwq_data;
//     /* verilator lint_off PROCASSINIT */
//     logic[31:0] smem_size = 0;
//     /* verilator lint_on PROCASSINIT */
//     `UNUSED_VAR(smem_size);

//     logic[31:0] cta_x[`NUM_CLUSTERS * `NUM_CORES];
//     logic[31:0] cta_y[`NUM_CLUSTERS * `NUM_CORES];
//     logic[31:0] cta_z[`NUM_CLUSTERS * `NUM_CORES];
//     logic[31:0] cta_id[`NUM_CLUSTERS * `NUM_CORES];
//     logic core_ready[`NUM_CLUSTERS * `NUM_CORES];

//     logic[31:0] total_threads;

//     logic[31:0] total_warps;

//     for(genvar i  = 0; i < `NUM_CLUSTERS * `NUM_CORES; ++i) begin : g_task_distr
//         assign task_interface[i].task_data.num_warps = total_warps;

//         assign task_interface[i].task_data.start_pc = kmu_data.pc;
//         assign task_interface[i].task_data.param = kmu_data.param;
//         assign task_interface[i].task_data.cta_x = cta_x[i];
//         assign task_interface[i].task_data.cta_y = cta_y[i];
//         assign task_interface[i].task_data.cta_z = cta_z[i];
//         assign task_interface[i].task_data.cta_id = cta_id[i];
//         assign core_ready[i] = task_interface[i].core_ready;
//     end

//     logic[31:0] counter_x;
//     logic[31:0] counter_y;
//     logic[31:0] counter_z;
//     logic[31:0] counter_id;

//     logic[31:0] cur_x;
//     logic[31:0] cur_y;
//     logic[31:0] cur_z;
//     logic[31:0] cur_id;

//     logic[`NUM_THREADS-1:0] tmask_v;

//     always_comb begin
//         total_threads = `MAX(kmu_data.block_dim[0], 1) 
//                         * `MAX(kmu_data.block_dim[1], 1) 
//                         * `MAX(kmu_data.block_dim[2], 1);

//         total_warps = total_threads / `NUM_THREADS;

//         if (total_warps * `NUM_THREADS < total_threads) begin
//             total_warps++;
//         end

//         if (total_warps * `NUM_THREADS == total_threads) begin
//             tmask_v = {`NUM_THREADS{1'b1}};
//         end else begin
//             tmask_v = {`NUM_THREADS{1'b1}};
//             tmask_v = tmask_v << (total_warps * `NUM_THREADS - total_threads);
//         end


//         cur_x = counter_x;
//         cur_y = counter_y;
//         cur_z = counter_z;
//         cur_id = counter_id;

//         for (int i = 0; i < `NUM_CLUSTERS * `NUM_CORES; ++i) begin
//             if (core_ready[i]) begin
//                 cta_x[i] = cur_x;
//                 cta_y[i] = cur_y;
//                 cta_z[i] = cur_z;
//                 cta_id[i] = cur_id;

//                 cur_z++;
//                 cur_id++;

//                 if (cur_z >= kmu_data.grid_dim[2]) begin
//                     cur_z = 0;
//                     cur_y++;
//                 end

//                 if (cur_y >= kmu_data.grid_dim[1]) begin
//                     cur_y = 0;
//                     cur_x++;
//                 end

//                 if (cur_x >= kmu_data.grid_dim[0]) begin
//                     cur_x = -1;
//                     // When in this conditional branch,
//                     // it means that kmu has distributed all
//                     // the task.
//                 end
//             end else begin
//                 // Default assignment to avoid latch inference
//                 cta_x[i] = cta_x[i];
//                 cta_y[i] = cta_y[i];
//                 cta_z[i] = cta_z[i];
//                 cta_id[i] = cta_id[i];
//             end
//         end
//     end


//     always @ (posedge clk) begin
//         if (reset) begin
//             counter_x <= '0;
//             counter_y <= '0;
//             counter_z <= '0;
//             counter_id <= '0;
//             kmu_kd_ready <= 1'b1;
//         end else if (cur_x == -1) begin
//             kmu_kd_ready <= 1'b1;
//         end else begin
//             counter_x <= cur_x;
//             counter_y <= cur_y;
//             counter_z <= cur_z;
//             counter_id <= cur_id;
//         end

//         if (hwq_data_valid && kmu_kd_ready) begin
//             counter_x <= '0;
//             counter_y <= '0;
//             counter_z <= '0;
//             counter_id <= '0;
//             kmu_kd_ready <= 1'b1;
//         end
        

//         if (smem_size != 0) begin
//             if (total_threads <= `NUM_CLUSTERS * `NUM_CORES * `NUM_THREADS) begin
//                 `ASSERT(0, ("cannot use shared memory since the cta size is smaller than total hardware threads\n"));
//             end
//         end
//     end

// endmodule

