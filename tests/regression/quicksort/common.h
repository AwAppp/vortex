#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE int
#endif

// Maximum recursion depth to prevent stack overflow
#define MAX_RECURSION_DEPTH 16

// Parent kernel arguments
typedef struct {
  uint64_t src_addr;        // Input array (unsorted)
  uint64_t dst_addr;        // Output array (sorted in-place, same as src)
  uint64_t child_params;    // Child kernel params buffer
  uint64_t child_pc;        // Child kernel entry point address
  uint32_t num_points;      // Number of elements to sort
  uint32_t padding;         // Alignment
} kernel_arg_t;

// Child kernel arguments (for recursive quicksort)
typedef struct {
  uint64_t arr_addr;        // Array buffer address
  uint64_t child_params;    // Next child params buffer for recursion
  uint64_t child_pc;        // Child kernel PC for recursive launch
  uint32_t left;            // Left bound of partition (inclusive)
  uint32_t right;           // Right bound of partition (inclusive)
  uint32_t depth;           // Current recursion depth
  uint32_t padding;         // Alignment
} child_params_t;

#endif
