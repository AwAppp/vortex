#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define FLOAT_ULP 6

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

template <typename Type>
class Comparator {};

template <>
class Comparator<int> {
public:
  static const char* type_str() {
    return "integer";
  }
  static int generate() {
    return rand() % 10;  // Small values for easier verification
  }
  static bool compare(int a, int b, int index, int errors) {
    if (a != b) {
      if (errors < 100) {
        printf("*** error: [%d] expected=%d, actual=%d\n", index, b, a);
      }
      return false;
    }
    return true;
  }
};

template <>
class Comparator<float> {
public:
  static const char* type_str() {
    return "float";
  }
  static float generate() {
    return static_cast<float>(rand() % 10);  // Small integer values as floats
  }
  static bool compare(float a, float b, int index, int errors) {
    union fi_t { float f; int32_t i; };
    fi_t fa, fb;
    fa.f = a;
    fb.f = b;
    auto d = std::abs(fa.i - fb.i);
    if (d > FLOAT_ULP) {
      if (errors < 100) {
        printf("*** error: [%d] expected=%f, actual=%f\n", index, b, a);
      }
      return false;
    }
    return true;
  }
};

///////////////////////////////////////////////////////////////////////////////

const char* parent_kernel_file = "kernel.vxbin";
const char* child_kernel_file = "child_kernel.vxbin";
uint32_t size = 8;

vx_device_h device = nullptr;
vx_buffer_h buf_A = nullptr;
vx_buffer_h buf_B = nullptr;
vx_buffer_h buf_C = nullptr;
vx_buffer_h child_params_buffer = nullptr;
vx_buffer_h parent_krnl_buffer = nullptr;
vx_buffer_h child_krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
   std::cout << "Vortex Matrix Reduction with Dynamic Kernel Launch Test." << std::endl;
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
    vx_mem_free(buf_A);
    vx_mem_free(buf_B);
    vx_mem_free(buf_C);
    vx_mem_free(child_params_buffer);
    vx_mem_free(parent_krnl_buffer);
    vx_mem_free(child_krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

int main(int argc, char *argv[]) {
  // parse command arguments
  parse_args(argc, argv);

  std::srand(50);

  // Matrix dimensions (square matrix for simplicity)
  uint32_t rows = size;
  uint32_t cols = size;
  uint32_t matrix_size = rows * cols;

  // open device connection
  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  std::cout << "matrix size: " << rows << " x " << cols << std::endl;
  std::cout << "data type: " << Comparator<TYPE>::type_str() << std::endl;

  // Kernel launch configuration: one thread per row
  const uint32_t threadsPerBlock = rows;  // All threads in one block for barrier
  const uint32_t blocksPerGrid = 1;

  kernel_arg.rows = rows;
  kernel_arg.cols = cols;
  kernel_arg.block_dim[0] = threadsPerBlock;
  kernel_arg.grid_dim[0] = blocksPerGrid;

  // Buffer sizes
  uint32_t buf_A_size = matrix_size * sizeof(TYPE);
  uint32_t buf_B_size = rows * sizeof(TYPE);
  uint32_t buf_C_size = sizeof(TYPE);
  uint32_t child_params_size = sizeof(child_params_t);

  // Allocate device memory
  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_A_size, VX_MEM_READ, &buf_A));
  RT_CHECK(vx_mem_address(buf_A, &kernel_arg.buf_A));
  
  RT_CHECK(vx_mem_alloc(device, buf_B_size, VX_MEM_READ_WRITE, &buf_B));
  RT_CHECK(vx_mem_address(buf_B, &kernel_arg.buf_B));
  
  RT_CHECK(vx_mem_alloc(device, buf_C_size, VX_MEM_WRITE, &buf_C));
  RT_CHECK(vx_mem_address(buf_C, &kernel_arg.buf_C));
  
  RT_CHECK(vx_mem_alloc(device, child_params_size, VX_MEM_READ_WRITE, &child_params_buffer));
  RT_CHECK(vx_mem_address(child_params_buffer, &kernel_arg.child_params));

  std::cout << "buf_A=0x" << std::hex << kernel_arg.buf_A << std::endl;
  std::cout << "buf_B=0x" << std::hex << kernel_arg.buf_B << std::endl;
  std::cout << "buf_C=0x" << std::hex << kernel_arg.buf_C << std::endl;
  std::cout << "child_params=0x" << std::hex << kernel_arg.child_params << std::dec << std::endl;

  // Allocate and initialize host buffer for matrix A
  std::cout << "initialize matrix A" << std::endl;
  std::vector<TYPE> h_A(matrix_size);
  TYPE expected_sum = 0;
  for (uint32_t i = 0; i < matrix_size; ++i) {
    h_A[i] = Comparator<TYPE>::generate();
    expected_sum += h_A[i];
  }
  std::cout << "expected sum: " << expected_sum << std::endl;

  // Upload matrix A to device
  std::cout << "upload matrix A" << std::endl;
  RT_CHECK(vx_copy_to_dev(buf_A, h_A.data(), 0, buf_A_size));

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

  // Wait for completion (both parent and dynamically launched child)
  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  // Download result from buffer C
  std::cout << "download result" << std::endl;
  TYPE actual_sum = 0;
  RT_CHECK(vx_copy_from_dev(&actual_sum, buf_C, 0, buf_C_size));

  // Verify result
  std::cout << "verify result" << std::endl;
  std::cout << "actual sum: " << actual_sum << std::endl;
  int errors = 0;
  if (!Comparator<TYPE>::compare(actual_sum, expected_sum, 0, errors)) {
    ++errors;
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
