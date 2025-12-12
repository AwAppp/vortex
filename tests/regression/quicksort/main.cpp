#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <vortex.h>
#include "common.h"

#define RT_CHECK(_expr)                                         \
   do {                                                         \
     int _ret = _expr;                                          \
     if (0 == _ret)                                             \
       break;                                                   \
     printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);   \
     cleanup();                                                 \
     exit(-1);                                                  \
   } while (false)

///////////////////////////////////////////////////////////////////////////////

const char* parent_kernel_file = "kernel.vxbin";
const char* child_kernel_file = "child_kernel.vxbin";
uint32_t size = 16;

vx_device_h device = nullptr;
vx_buffer_h src_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h child_params_buffer = nullptr;
vx_buffer_h parent_krnl_buffer = nullptr;
vx_buffer_h child_krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
   std::cout << "Vortex Quicksort with Dynamic Kernel Launch Test." << std::endl;
   std::cout << "Usage: [-n size] [-h: help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:h")) != -1) {
    switch (c) {
    case 'n':
      size = atoi(optarg);
      break;
    case 'h':
      show_usage();
      exit(0);
      break;
    default:
      show_usage();
      exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    vx_mem_free(src_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(child_params_buffer);
    vx_mem_free(parent_krnl_buffer);
    vx_mem_free(child_krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

void gen_src_data(std::vector<TYPE>& src_data, uint32_t n) {
  src_data.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    src_data[i] = static_cast<TYPE>(std::rand() % 1000);  // Random values 0-999
  }
}

int main(int argc, char *argv[]) {
  // parse command arguments
  parse_args(argc, argv);

  if (size == 0) {
    size = 16;
  }

  std::srand(50);

  // open device connection
  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t num_points = size;
  uint32_t buf_size = num_points * sizeof(TYPE);
  uint32_t child_params_size = sizeof(child_params_t);

  std::cout << "number of points: " << num_points << std::endl;
  std::cout << "buffer size: " << buf_size << " bytes" << std::endl;

  kernel_arg.num_points = num_points;

  // Allocate device memory
  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ, &src_buffer));
  RT_CHECK(vx_mem_address(src_buffer, &kernel_arg.src_addr));
  
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));
  
  RT_CHECK(vx_mem_alloc(device, child_params_size, VX_MEM_READ_WRITE, &child_params_buffer));
  RT_CHECK(vx_mem_address(child_params_buffer, &kernel_arg.child_params));

  std::cout << "src_addr=0x" << std::hex << kernel_arg.src_addr << std::endl;
  std::cout << "dst_addr=0x" << std::hex << kernel_arg.dst_addr << std::endl;
  std::cout << "child_params=0x" << std::hex << kernel_arg.child_params << std::dec << std::endl;

  // Allocate and initialize host buffer
  std::cout << "initialize source data" << std::endl;
  std::vector<TYPE> h_src;
  gen_src_data(h_src, num_points);

  // Print input array (for debugging)
  std::cout << "input array: ";
  for (uint32_t i = 0; i < std::min(num_points, 20u); ++i) {
    std::cout << h_src[i] << " ";
  }
  if (num_points > 20) std::cout << "...";
  std::cout << std::endl;

  // Compute expected result (sorted array)
  std::vector<TYPE> h_ref = h_src;
  std::sort(h_ref.begin(), h_ref.end());

  // Upload source buffer
  std::cout << "upload source buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, buf_size));

  // Upload parent kernel binary
  std::cout << "upload parent kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, parent_kernel_file, &parent_krnl_buffer));

  // Upload child kernel binary
  std::cout << "upload child kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, child_kernel_file, &child_krnl_buffer));

  // Get child kernel's device address and pass to parent
  RT_CHECK(vx_mem_address(child_krnl_buffer, &kernel_arg.child_pc));
  std::cout << "child_pc=0x" << std::hex << kernel_arg.child_pc << std::dec << std::endl;

  // Upload kernel arguments
  std::cout << "upload kernel arguments" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  // Start parent kernel
  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, parent_krnl_buffer, args_buffer));

  // Wait for completion (both parent and dynamically launched children)
  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  // Download result
  std::cout << "download result" << std::endl;
  std::vector<TYPE> h_dst(num_points);
  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, buf_size));

  // Print output array (for debugging)
  std::cout << "output array: ";
  for (uint32_t i = 0; i < std::min(num_points, 20u); ++i) {
    std::cout << h_dst[i] << " ";
  }
  if (num_points > 20) std::cout << "...";
  std::cout << std::endl;

  // Verify result
  std::cout << "verify result" << std::endl;
  int errors = 0;
  for (uint32_t i = 0; i < num_points; ++i) {
    if (h_dst[i] != h_ref[i]) {
      if (errors < 20) {
        std::cout << "*** error at index " << i << ": expected=" << h_ref[i] 
                  << ", actual=" << h_dst[i] << std::endl;
      }
      ++errors;
    }
  }

  // cleanup
  std::cout << "cleanup" << std::endl;
  cleanup();

  if (errors != 0) {
    std::cout << "Found " << std::dec << errors << " errors!" << std::endl;
    std::cout << "FAILED!" << std::endl;
    return 1;
  }

  std::cout << "PASSED!" << std::endl;

  return 0;
}
