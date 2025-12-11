#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE float
#endif

// Parent kernel arguments
typedef struct {
  uint64_t buf_A;           // Input matrix (rows x cols)
  uint64_t buf_B;           // Row sums buffer (rows)
  uint64_t buf_C;           // Final sum output (1 element)
  uint64_t child_params;    // Child kernel params buffer
  uint64_t child_pc;        // Child kernel entry point address
  uint32_t rows;
  uint32_t cols;
  uint32_t grid_dim[1];
  uint32_t block_dim[1];
} kernel_arg_t;

// Child kernel arguments (passed via dynamic_kernel_launch param)
typedef struct {
  uint64_t buf_B;
  uint64_t buf_C;
  uint32_t num_rows;
  uint32_t padding;         // For alignment
} child_params_t;

#endif
