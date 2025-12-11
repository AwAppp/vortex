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
#include "VVX_kmu_refactored_dcr_kd_top.h"
#include <iostream>
#include <cassert>
#include <cstdint>

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

// Configure kernel parameters
template <typename T>
void config_kernel(vl_simulator<T>& sim,
                   uint32_t pc,
                   uint32_t param,
                   uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                   uint32_t block_x, uint32_t block_y, uint32_t block_z) {
    sim->hwq_pc = pc;
    sim->hwq_param = param;
    sim->hwq_grid_dim_x = grid_x;
    sim->hwq_grid_dim_y = grid_y;
    sim->hwq_grid_dim_z = grid_z;
    sim->hwq_block_dim_x = block_x;
    sim->hwq_block_dim_y = block_y;
    sim->hwq_block_dim_z = block_z;
}

// Run CTA distribution test
template <typename T>
bool test_cta_distribution(vl_simulator<T>& sim, uint64_t& tick,
                           uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                           uint32_t block_x, uint32_t block_y, uint32_t block_z) {
    
    uint32_t expected_ctas = grid_x * grid_y * grid_z;
    uint32_t received_ctas = 0;
    uint32_t expected_id = 0;

    std::cout << "Testing grid(" << grid_x << "x" << grid_y << "x" << grid_z 
              << ") block(" << block_x << "x" << block_y << "x" << block_z << ")" << std::endl;
    std::cout << "Expected CTAs: " << expected_ctas << std::endl;

    // Configure kernel
    config_kernel(sim, 0x1000, 0xDEADBEEF,
                  grid_x, grid_y, grid_z,
                  block_x, block_y, block_z);

    // Wait for kmu_kd_ready
    int timeout = 100;
    while (!sim->kmu_kd_ready && timeout-- > 0) {
        tick = sim.step(tick, 2);
    }
    if (!sim->kmu_kd_ready) {
        std::cerr << "ERROR: kmu_kd_ready not high after reset" << std::endl;
        return false;
    }

    // Enable downstream ready
    sim->req_ready = 1;

    // Trigger kernel dispatch
    sim->hwq_data_valid = 1;
    tick = sim.step(tick, 2);
    sim->hwq_data_valid = 0;

    // Collect CTAs
    int max_cycles = 1000;
    while (received_ctas < expected_ctas && max_cycles-- > 0) {
        tick = sim.step(tick, 2);

        if (sim->req_valid && sim->req_ready) {
            // Verify CTA data
            uint32_t cta_x = sim->req_cta_x;
            uint32_t cta_y = sim->req_cta_y;
            uint32_t cta_z = sim->req_cta_z;
            uint32_t cta_id = sim->req_cta_id;

            // Compute expected coordinates
            uint32_t exp_z = expected_id % grid_z;
            uint32_t exp_y = (expected_id / grid_z) % grid_y;
            uint32_t exp_x = expected_id / (grid_y * grid_z);

            if (cta_x != exp_x || cta_y != exp_y || cta_z != exp_z) {
                std::cerr << "ERROR: CTA coordinate mismatch at id=" << cta_id << std::endl;
                std::cerr << "  Expected: (" << exp_x << ", " << exp_y << ", " << exp_z << ")" << std::endl;
                std::cerr << "  Got:      (" << cta_x << ", " << cta_y << ", " << cta_z << ")" << std::endl;
                return false;
            }

            if (cta_id != expected_id) {
                std::cerr << "ERROR: CTA id mismatch. Expected " << expected_id << ", got " << cta_id << std::endl;
                return false;
            }

            // Verify PC and param are passed through
            if (sim->req_start_pc != 0x1000) {
                std::cerr << "ERROR: PC mismatch. Expected 0x1000, got 0x" << std::hex << sim->req_start_pc << std::dec << std::endl;
                return false;
            }

            if (sim->req_param != 0xDEADBEEF) {
                std::cerr << "ERROR: Param mismatch. Expected 0xDEADBEEF, got 0x" << std::hex << sim->req_param << std::dec << std::endl;
                return false;
            }

            received_ctas++;
            expected_id++;
        }
    }

    if (received_ctas != expected_ctas) {
        std::cerr << "ERROR: Expected " << expected_ctas << " CTAs, received " << received_ctas << std::endl;
        return false;
    }

    std::cout << "  PASSED: Received " << received_ctas << " CTAs correctly" << std::endl;

    // Wait for ready to go high again
    timeout = 100;
    while (!sim->kmu_kd_ready && timeout-- > 0) {
        tick = sim.step(tick, 2);
    }

    return true;
}

// Test backpressure handling
template <typename T>
bool test_backpressure(vl_simulator<T>& sim, uint64_t& tick) {
    std::cout << "Testing backpressure handling..." << std::endl;

    // Reset
    tick = sim.reset(tick);

    // Configure small grid
    config_kernel(sim, 0x2000, 0x12345678, 2, 2, 1, 4, 4, 4);

    // Wait for ready
    int timeout = 100;
    while (!sim->kmu_kd_ready && timeout-- > 0) {
        tick = sim.step(tick, 2);
    }

    // Start with ready=0
    sim->req_ready = 0;
    sim->hwq_data_valid = 1;
    tick = sim.step(tick, 2);
    sim->hwq_data_valid = 0;

    // Step a few cycles, req_valid should stay high with same data
    for (int i = 0; i < 5; i++) {
        tick = sim.step(tick, 2);
        if (!sim->req_valid) {
            std::cerr << "ERROR: req_valid dropped without handshake" << std::endl;
            return false;
        }
    }

    // Now accept
    sim->req_ready = 1;
    uint32_t ctas = 0;
    int max_cycles = 100;
    while (ctas < 4 && max_cycles-- > 0) {
        tick = sim.step(tick, 2);
        if (sim->req_valid && sim->req_ready) {
            ctas++;
        }
    }

    if (ctas != 4) {
        std::cerr << "ERROR: Expected 4 CTAs after backpressure, got " << ctas << std::endl;
        return false;
    }

    std::cout << "  PASSED: Backpressure handling correct" << std::endl;
    return true;
}

int main(int argc, char **argv) {
    // Initialize Verilator
    Verilated::commandArgs(argc, argv);

    vl_simulator<VVX_kmu_refactored_dcr_kd_top> sim;
    uint64_t tick = 0;

    // Initialize signals
    sim->hwq_data_valid = 0;
    sim->req_ready = 0;
    sim->hwq_pc = 0;
    sim->hwq_param = 0;
    sim->hwq_grid_dim_x = 1;
    sim->hwq_grid_dim_y = 1;
    sim->hwq_grid_dim_z = 1;
    sim->hwq_block_dim_x = 1;
    sim->hwq_block_dim_y = 1;
    sim->hwq_block_dim_z = 1;

    // Reset
    tick = sim.reset(tick);

    std::cout << "=== VX_kmu_refactored_dcr_kd Unit Test ===" << std::endl;

    // Test 1: Simple 2x2x2 grid
    tick = sim.reset(tick);
    if (!test_cta_distribution(sim, tick, 2, 2, 2, 4, 4, 4)) {
        std::cerr << "Test 1 FAILED" << std::endl;
        return 1;
    }

    // Test 2: Non-uniform 3x2x1 grid
    tick = sim.reset(tick);
    if (!test_cta_distribution(sim, tick, 3, 2, 1, 8, 8, 1)) {
        std::cerr << "Test 2 FAILED" << std::endl;
        return 1;
    }

    // Test 3: Single CTA
    tick = sim.reset(tick);
    if (!test_cta_distribution(sim, tick, 1, 1, 1, 16, 16, 1)) {
        std::cerr << "Test 3 FAILED" << std::endl;
        return 1;
    }

    // Test 4: Backpressure test
    if (!test_backpressure(sim, tick)) {
        std::cerr << "Test 4 FAILED" << std::endl;
        return 1;
    }

    std::cout << std::endl << "=== ALL TESTS PASSED! ===" << std::endl;
    return 0;
}
