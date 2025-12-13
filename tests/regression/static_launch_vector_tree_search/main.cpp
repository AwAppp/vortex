#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <cmath>
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
uint32_t tree_depth = 3;    // Depth of binary tree
uint32_t vec_len = 4;       // Length of vectors

vx_device_h device = nullptr;
vx_buffer_h query_buffer = nullptr;
vx_buffer_h result_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
std::vector<vx_buffer_h> node_vec_buffers;

static void show_usage() {
   std::cout << "Vortex Static Launch Vector Tree Search Test." << std::endl;
   std::cout << "Usage: [-d depth] [-n vec_len] [-h: help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "d:n:h")) != -1) {
    switch (c) {
    case 'd':
      tree_depth = atoi(optarg);
      break;
    case 'n':
      vec_len = atoi(optarg);
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
    vx_mem_free(query_buffer);
    vx_mem_free(result_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    for (auto& buf : node_vec_buffers) {
      vx_mem_free(buf);
    }
    vx_dev_close(device);
  }
}

// Build a complete binary tree structure
// Returns vector of nodes, index 0 is root
// For a tree of depth d, we have 2^d - 1 nodes
struct HostNode {
  std::vector<TYPE> vec;
  int left_child;   // -1 if leaf
  int right_child;  // -1 if leaf
  int node_id;
  uint64_t vec_addr;  // Device address
};

void build_tree(std::vector<HostNode>& nodes, uint32_t depth, uint32_t n) {
  uint32_t num_nodes = (1 << depth) - 1;
  nodes.resize(num_nodes);
  
  for (uint32_t i = 0; i < num_nodes; ++i) {
    nodes[i].node_id = i;
    nodes[i].vec.resize(n);
    
    // Generate random vector for this node
    for (uint32_t j = 0; j < n; ++j) {
      // Use values between -1 and 1
      nodes[i].vec[j] = static_cast<TYPE>(std::rand()) / RAND_MAX * 2.0f - 1.0f;
    }
    
    // Calculate children indices (complete binary tree)
    uint32_t left = 2 * i + 1;
    uint32_t right = 2 * i + 2;
    
    if (left < num_nodes) {
      nodes[i].left_child = left;
      nodes[i].right_child = right;
    } else {
      nodes[i].left_child = -1;
      nodes[i].right_child = -1;
    }
  }
}

// Compute expected traversal path on host
int compute_expected_leaf(const std::vector<HostNode>& nodes, const std::vector<TYPE>& query) {
  int current = 0;  // Start at root
  
  while (nodes[current].left_child != -1) {
    // Compute dot product
    TYPE dot = 0;
    for (size_t i = 0; i < query.size(); ++i) {
      dot += query[i] * nodes[current].vec[i];
    }
    
    // Traverse based on sign
    if (dot >= 0) {
      current = nodes[current].left_child;
    } else {
      current = nodes[current].right_child;
    }
  }
  
  return nodes[current].node_id;
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);

  if (tree_depth < 1) tree_depth = 1;
  if (tree_depth > 10) tree_depth = 10;  // Limit depth
  if (vec_len < 1) vec_len = 4;

  std::srand(42);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t num_nodes = (1 << tree_depth) - 1;
  uint32_t vec_size = vec_len * sizeof(TYPE);

  std::cout << "tree depth: " << tree_depth << std::endl;
  std::cout << "num nodes: " << num_nodes << std::endl;
  std::cout << "vector length: " << vec_len << std::endl;

  // Build tree on host
  std::vector<HostNode> nodes;
  build_tree(nodes, tree_depth, vec_len);

  // Generate query vector
  std::vector<TYPE> query(vec_len);
  for (uint32_t i = 0; i < vec_len; ++i) {
    query[i] = static_cast<TYPE>(std::rand()) / RAND_MAX * 2.0f - 1.0f;
  }

  std::cout << "query vector: ";
  for (uint32_t i = 0; i < vec_len; ++i) {
    std::cout << query[i] << " ";
  }
  std::cout << std::endl;

  // Compute expected result
  int expected_leaf = compute_expected_leaf(nodes, query);
  std::cout << "expected leaf node: " << expected_leaf << std::endl;

  // Allocate device memory
  std::cout << "allocate device memory" << std::endl;
  
  // Query buffer
  uint64_t query_addr;
  RT_CHECK(vx_mem_alloc(device, vec_size, VX_MEM_READ, &query_buffer));
  RT_CHECK(vx_mem_address(query_buffer, &query_addr));

  // Result buffer (single float)
  uint64_t result_addr;
  RT_CHECK(vx_mem_alloc(device, sizeof(TYPE), VX_MEM_READ_WRITE, &result_buffer));
  RT_CHECK(vx_mem_address(result_buffer, &result_addr));

  // Upload node vectors to device
  node_vec_buffers.resize(num_nodes);
  for (uint32_t i = 0; i < num_nodes; ++i) {
    RT_CHECK(vx_mem_alloc(device, vec_size, VX_MEM_READ, &node_vec_buffers[i]));
    RT_CHECK(vx_mem_address(node_vec_buffers[i], &nodes[i].vec_addr));
    RT_CHECK(vx_copy_to_dev(node_vec_buffers[i], nodes[i].vec.data(), 0, vec_size));
  }

  // Upload query
  std::cout << "upload query vector" << std::endl;
  RT_CHECK(vx_copy_to_dev(query_buffer, query.data(), 0, vec_size));

  // Upload kernel
  std::cout << "upload kernel" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  // Traverse tree using static kernel launches
  std::cout << "starting tree traversal with static kernel launches" << std::endl;
  
  int current_node = 0;  // Start at root
  int traversal_steps = 0;

  while (nodes[current_node].left_child != -1) {
    // Set up kernel args for current node
    kernel_arg_t kernel_arg;
    kernel_arg.block_dim = 16;  // Match runtime's VX_DCR_BASE_BLOCK_DIM2 value
    kernel_arg.vec_len = vec_len;
    kernel_arg.query_addr = query_addr;
    kernel_arg.node_vec_addr = nodes[current_node].vec_addr;
    kernel_arg.result_addr = result_addr;

    // Upload kernel args
    if (args_buffer) {
      vx_mem_free(args_buffer);
    }
    RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

    // Launch kernel
    std::cout << "  step " << traversal_steps << ": launching kernel for node " << current_node << std::endl;
    RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
    RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

    // Read result
    TYPE dot_result;
    RT_CHECK(vx_copy_from_dev(&dot_result, result_buffer, 0, sizeof(TYPE)));

    std::cout << "    dot product = " << dot_result << std::endl;

    // Decide next node
    if (dot_result >= 0) {
      current_node = nodes[current_node].left_child;
      std::cout << "    going LEFT to node " << current_node << std::endl;
    } else {
      current_node = nodes[current_node].right_child;
      std::cout << "    going RIGHT to node " << current_node << std::endl;
    }

    ++traversal_steps;
  }

  int actual_leaf = nodes[current_node].node_id;
  std::cout << "reached leaf node: " << actual_leaf << " in " << traversal_steps << " steps" << std::endl;

  // Cleanup
  std::cout << "cleanup" << std::endl;
  cleanup();

  // Verify
  if (actual_leaf != expected_leaf) {
    std::cout << "*** error: expected leaf " << expected_leaf << ", got " << actual_leaf << std::endl;
    std::cout << "FAILED!" << std::endl;
    return 1;
  }

  std::cout << "PASSED!" << std::endl;
  return 0;
}
