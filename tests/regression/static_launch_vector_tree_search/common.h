#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE float
#endif

// Kernel arguments for dotproduct computation on a single tree node
typedef struct {
  uint32_t block_dim;       // Number of threads for parallelism (from host)
  uint32_t vec_len;         // Length of vectors (n)
  uint64_t query_addr;      // Query vector address
  uint64_t node_vec_addr;   // Current node's vector address
  uint64_t result_addr;     // Single float result (dot product)
} kernel_arg_t;

// Tree node structure (used by host for tree building)
typedef struct {
  uint64_t vec_addr;        // Address of this node's vector in device memory
  uint64_t left_child;      // Address of left child node params (0 if leaf)
  uint64_t right_child;     // Address of right child node params (0 if leaf)
  uint32_t node_id;         // Node identifier
  uint32_t is_leaf;         // 1 if leaf node, 0 otherwise
} tree_node_t;

#endif
