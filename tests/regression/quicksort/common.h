#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE int
#endif

// Kernel arguments (no child kernel needed)
typedef struct {
  uint32_t num_points;      // Number of elements to sort
  uint64_t src_addr;        // Input array (unsorted)
  uint64_t dst_addr;        // Output array (sorted)
} kernel_arg_t;

#endif

