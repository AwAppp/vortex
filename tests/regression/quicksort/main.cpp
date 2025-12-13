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

const char* kernel_file = "kernel.vxbin";
uint32_t size = 16;

vx_device_h device = nullptr;
vx_buffer_h src_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
   std::cout << "Vortex Quicksort Test." << std::endl;
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
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

void gen_src_data(std::vector<TYPE>& src_data, uint32_t n) {
  src_data.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    src_data[i] = static_cast<TYPE>(std::rand() % 1000);
  }
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);

  if (size == 0) {
    size = 16;
  }

  std::srand(50);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t num_points = size;
  uint32_t buf_size = num_points * sizeof(TYPE);

  std::cout << "number of points: " << num_points << std::endl;
  std::cout << "buffer size: " << buf_size << " bytes" << std::endl;

  kernel_arg.num_points = num_points;

  // Allocate device memory
  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ, &src_buffer));
  RT_CHECK(vx_mem_address(src_buffer, &kernel_arg.src_addr));
  
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));

  std::cout << "src_addr=0x" << std::hex << kernel_arg.src_addr << std::endl;
  std::cout << "dst_addr=0x" << std::hex << kernel_arg.dst_addr << std::dec << std::endl;

  // Initialize host buffer
  std::cout << "initialize source data" << std::endl;
  std::vector<TYPE> h_src;
  gen_src_data(h_src, num_points);

  std::cout << "input array: ";
  for (uint32_t i = 0; i < std::min(num_points, 20u); ++i) {
    std::cout << h_src[i] << " ";
  }
  if (num_points > 20) std::cout << "...";
  std::cout << std::endl;

  // Compute expected result
  std::vector<TYPE> h_ref = h_src;
  std::sort(h_ref.begin(), h_ref.end());

  // Upload source buffer
  std::cout << "upload source buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, buf_size));

  // Upload kernel binary
  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  // Upload kernel arguments
  std::cout << "upload kernel arguments" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  // Start kernel
  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));

  // Wait for completion
  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  // Download result
  std::cout << "download result" << std::endl;
  std::vector<TYPE> h_dst(num_points);
  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, buf_size));

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
