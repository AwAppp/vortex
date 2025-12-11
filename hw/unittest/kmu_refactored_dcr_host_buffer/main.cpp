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

#include "vl_simulator.h"
#include "VVX_kmu_refactored_dcr_host_buffer_top.h"
#include <iostream>
#include <cassert>
#include <cstdint>
#include "VX_config.h"
#include "VX_types.h"

#ifndef TRACE_START_TIME
#define TRACE_START_TIME 0ull
#endif

#ifndef TRACE_STOP_TIME
#define TRACE_STOP_TIME -1ull
#endif

static uint64_t timestamp = 0;
static bool trace_enabled = false;
static uint64_t trace_start_time = TRACE_START_TIME;
static uint64_t trace_stop_time  = TRACE_STOP_TIME;

double sc_time_stamp() { 
  return timestamp;
}

bool sim_trace_enabled() {
  if (timestamp >= trace_start_time 
   && timestamp < trace_stop_time)
    return true;
  return trace_enabled;
}

void sim_trace_enable(bool enable) {
  trace_enabled = enable;
}

// Write a DCR register
template <typename T>
uint64_t write_dcr(vl_simulator<T>& sim, uint32_t addr, uint32_t value, uint64_t tick) {
    sim->dcr_wr_valid = 1;
    sim->dcr_wr_addr = addr;
    sim->dcr_wr_data = value;
    tick = sim.step(tick, 2);
    sim->dcr_wr_valid = 0;
    tick = sim.step(tick, 2);
    return tick;
}

// Write all kernel parameters via DCR
template <typename T>
uint64_t write_kernel_config(vl_simulator<T>& sim, uint64_t tick,
                             uint32_t pc, uint32_t param,
                             uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                             uint32_t block_x, uint32_t block_y, uint32_t block_z) {
    tick = write_dcr(sim, VX_DCR_BASE_STARTUP_ADDR0, pc, tick);
    tick = write_dcr(sim, VX_DCR_BASE_STARTUP_ARG0, param, tick);
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM0, grid_x, tick);
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM1, grid_y, tick);
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM2, grid_z, tick);
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM0, block_x, tick);
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM1, block_y, tick);
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM2, block_z, tick);
    return tick;
}

// Test 1: Verify output valid after all DCR writes
template <typename T>
bool test_all_fields_written(vl_simulator<T>& sim, uint64_t& tick) {
    std::cout << "Test 1: Verify dcr_out_valid after all fields written..." << std::endl;

    tick = sim.reset(tick);
    sim->hwq_in_ready = 0;

    // Verify not valid initially
    tick = sim.step(tick, 2);
    if (sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 0 after reset" << std::endl;
        return false;
    }

    // Write all fields
    uint32_t exp_pc = 0x80001000;
    uint32_t exp_param = 0xDEADBEEF;
    uint32_t exp_grid[3] = {4, 3, 2};
    uint32_t exp_block[3] = {32, 16, 8};

    tick = write_kernel_config(sim, tick, exp_pc, exp_param,
                               exp_grid[0], exp_grid[1], exp_grid[2],
                               exp_block[0], exp_block[1], exp_block[2]);

    // Check that dcr_out_valid is now high
    if (!sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 1 after all fields written" << std::endl;
        return false;
    }

    // Verify data values
    if (sim->out_pc != exp_pc) {
        std::cerr << "ERROR: PC mismatch. Expected 0x" << std::hex << exp_pc 
                  << ", got 0x" << sim->out_pc << std::dec << std::endl;
        return false;
    }
    if (sim->out_param != exp_param) {
        std::cerr << "ERROR: Param mismatch. Expected 0x" << std::hex << exp_param 
                  << ", got 0x" << sim->out_param << std::dec << std::endl;
        return false;
    }
    if (sim->out_grid_dim_x != exp_grid[0] || 
        sim->out_grid_dim_y != exp_grid[1] || 
        sim->out_grid_dim_z != exp_grid[2]) {
        std::cerr << "ERROR: Grid dim mismatch" << std::endl;
        return false;
    }
    if (sim->out_block_dim_x != exp_block[0] || 
        sim->out_block_dim_y != exp_block[1] || 
        sim->out_block_dim_z != exp_block[2]) {
        std::cerr << "ERROR: Block dim mismatch" << std::endl;
        return false;
    }

    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 2: Verify partial writes don't trigger valid
template <typename T>
bool test_partial_writes(vl_simulator<T>& sim, uint64_t& tick) {
    std::cout << "Test 2: Verify partial writes don't trigger valid..." << std::endl;

    tick = sim.reset(tick);
    sim->hwq_in_ready = 0;

    // Write only some fields (not all 8)
    tick = write_dcr(sim, VX_DCR_BASE_STARTUP_ADDR0, 0x1000, tick);
    tick = write_dcr(sim, VX_DCR_BASE_STARTUP_ARG0, 0x2000, tick);
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM0, 1, tick);

    if (sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 0 with only 3 fields written" << std::endl;
        return false;
    }

    // Write more but not all
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM1, 1, tick);
    tick = write_dcr(sim, VX_DCR_BASE_GRID_DIM2, 1, tick);
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM0, 1, tick);
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM1, 1, tick);

    if (sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 0 with 7 fields written" << std::endl;
        return false;
    }

    // Write final field
    tick = write_dcr(sim, VX_DCR_BASE_BLOCK_DIM2, 1, tick);

    if (!sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 1 after all 8 fields written" << std::endl;
        return false;
    }

    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 3: Verify handshake clears fields_ready
template <typename T>
bool test_handshake_clear(vl_simulator<T>& sim, uint64_t& tick) {
    std::cout << "Test 3: Verify handshake clears fields_ready..." << std::endl;

    tick = sim.reset(tick);
    sim->hwq_in_ready = 0;

    // Write all fields
    tick = write_kernel_config(sim, tick, 0x1000, 0x2000, 1, 1, 1, 4, 4, 4);

    if (!sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 1" << std::endl;
        return false;
    }

    // Perform handshake
    sim->hwq_in_ready = 1;
    tick = sim.step(tick, 2);
    sim->hwq_in_ready = 0;
    tick = sim.step(tick, 2);

    // dcr_out_valid should now be 0
    if (sim->dcr_out_valid) {
        std::cerr << "ERROR: dcr_out_valid should be 0 after handshake" << std::endl;
        return false;
    }

    std::cout << "  PASSED" << std::endl;
    return true;
}

// Test 4: Verify multiple kernel dispatches
template <typename T>
bool test_multiple_dispatches(vl_simulator<T>& sim, uint64_t& tick) {
    std::cout << "Test 4: Verify multiple kernel dispatches..." << std::endl;

    tick = sim.reset(tick);

    for (int i = 0; i < 3; i++) {
        sim->hwq_in_ready = 0;
        uint32_t pc = 0x1000 * (i + 1);
        uint32_t param = 0xABCD0000 | i;

        tick = write_kernel_config(sim, tick, pc, param, i+1, i+2, 1, 8, 8, 1);

        if (!sim->dcr_out_valid) {
            std::cerr << "ERROR: dcr_out_valid should be 1 for kernel " << i << std::endl;
            return false;
        }

        if (sim->out_pc != pc || sim->out_param != param) {
            std::cerr << "ERROR: Data mismatch for kernel " << i << std::endl;
            return false;
        }

        // Handshake to clear
        sim->hwq_in_ready = 1;
        tick = sim.step(tick, 2);
        sim->hwq_in_ready = 0;
        tick = sim.step(tick, 2);

        if (sim->dcr_out_valid) {
            std::cerr << "ERROR: dcr_out_valid should be 0 after handshake " << i << std::endl;
            return false;
        }
    }

    std::cout << "  PASSED" << std::endl;
    return true;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    vl_simulator<VVX_kmu_refactored_dcr_host_buffer_top> sim;
    uint64_t tick = 0;

    // Initialize signals
    sim->dcr_wr_valid = 0;
    sim->dcr_wr_addr = 0;
    sim->dcr_wr_data = 0;
    sim->hwq_in_ready = 0;

    tick = sim.reset(tick);

    std::cout << "=== VX_kmu_refactored_dcr_host_buffer Unit Test ===" << std::endl;

    if (!test_all_fields_written(sim, tick)) {
        std::cerr << "Test 1 FAILED" << std::endl;
        return 1;
    }

    if (!test_partial_writes(sim, tick)) {
        std::cerr << "Test 2 FAILED" << std::endl;
        return 1;
    }

    if (!test_handshake_clear(sim, tick)) {
        std::cerr << "Test 3 FAILED" << std::endl;
        return 1;
    }

    if (!test_multiple_dispatches(sim, tick)) {
        std::cerr << "Test 4 FAILED" << std::endl;
        return 1;
    }

    std::cout << std::endl << "=== ALL TESTS PASSED! ===" << std::endl;
    return 0;
}
