#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE float
#endif

// Parent kernel arguments (launched by host with single thread)
typedef struct {
  uint32_t vec_len;           // Length of vectors (n)
  uint64_t query_addr;        // Query vector address
  uint64_t root_params_addr;  // Address of root node's child_params_t
  uint64_t child_kernel_pc;   // PC of child kernel binary
} kernel_arg_t;

// Child kernel arguments for tree traversal with dynamic kernel launch
// Each node's params are stored in device memory
typedef struct {
  uint32_t grid_dim[3];       // Grid dimensions for child kernel launch
  uint32_t block_dim[3];      // Block dimensions for child kernel launch
  uint32_t vec_len;           // Length of vectors (n)
  uint64_t query_addr;        // Query vector address (constant across all nodes)
  uint64_t node_vec_addr;     // This node's vector address
  uint64_t left_child_params; // Address of left child's child_params_t (0 if leaf)
  uint64_t right_child_params;// Address of right child's child_params_t (0 if leaf)
  uint64_t kernel_pc;         // Kernel PC for recursive launch
  uint64_t result_addr;       // Where to write final leaf node ID
  uint32_t node_id;           // This node's ID
  uint32_t padding;           // Alignment
} child_params_t;

#endif
