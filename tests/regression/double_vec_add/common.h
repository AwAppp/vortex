#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE int
#endif

// Parent kernel arguments
typedef struct {
  uint64_t buf_A;           // Input vector A
  uint64_t buf_B;           // Input vector B
  uint64_t buf_C;           // Output vector C (result)
  uint64_t child_params;    // Child kernel params buffer
  uint64_t child_pc;        // Child kernel entry point address
  uint32_t size;            // Number of elements
} kernel_arg_t;

// Child kernel arguments (passed via dynamic_kernel_launch param)
typedef struct {
  uint64_t buf_B;           // Input vector B
  uint64_t buf_C;           // Output vector C (in-place update)
  uint32_t size;            // Number of elements
  uint32_t padding;         // For alignment
} child_params_t;

#endif
