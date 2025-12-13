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
const char* child_kernel_file = "child_kernel.vxbin";
uint32_t tree_depth = 3;    // Depth of binary tree
uint32_t vec_len = 4;       // Length of vectors

vx_device_h device = nullptr;
vx_buffer_h query_buffer = nullptr;
vx_buffer_h result_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h child_krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
std::vector<vx_buffer_h> node_vec_buffers;
std::vector<vx_buffer_h> node_params_buffers;

static void show_usage() {
   std::cout << "Vortex Dynamic Launch Vector Tree Search Test." << std::endl;
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
    vx_mem_free(child_krnl_buffer);
    vx_mem_free(args_buffer);
    for (auto& buf : node_vec_buffers) {
      vx_mem_free(buf);
    }
    for (auto& buf : node_params_buffers) {
      vx_mem_free(buf);
    }
    vx_dev_close(device);
  }
}

// Host-side node structure
struct HostNode {
  std::vector<TYPE> vec;
  int left_child;   // -1 if leaf
  int right_child;  // -1 if leaf
  int node_id;
  uint64_t vec_addr;      // Device address of vector
  uint64_t params_addr;   // Device address of child_params_t
};

void build_tree(std::vector<HostNode>& nodes, uint32_t depth, uint32_t n) {
  uint32_t num_nodes = (1 << depth) - 1;
  nodes.resize(num_nodes);
  
  for (uint32_t i = 0; i < num_nodes; ++i) {
    nodes[i].node_id = i;
    nodes[i].vec.resize(n);
    
    for (uint32_t j = 0; j < n; ++j) {
      nodes[i].vec[j] = static_cast<TYPE>(std::rand()) / RAND_MAX * 2.0f - 1.0f;
    }
    
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

int compute_expected_leaf(const std::vector<HostNode>& nodes, const std::vector<TYPE>& query) {
  int current = 0;
  
  while (nodes[current].left_child != -1) {
    TYPE dot = 0;
    for (size_t i = 0; i < query.size(); ++i) {
      dot += query[i] * nodes[current].vec[i];
    }
    
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
  if (tree_depth > 10) tree_depth = 10;
  if (vec_len < 1) vec_len = 4;

  std::srand(42);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t num_nodes = (1 << tree_depth) - 1;
  uint32_t vec_size = vec_len * sizeof(TYPE);

  std::cout << "tree depth: " << tree_depth << std::endl;
  std::cout << "num nodes: " << num_nodes << std::endl;
  std::cout << "vector length: " << vec_len << std::endl;

  // Build tree
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

  int expected_leaf = compute_expected_leaf(nodes, query);
  std::cout << "expected leaf node: " << expected_leaf << std::endl;

  // Allocate device memory
  std::cout << "allocate device memory" << std::endl;
  
  // Query buffer
  uint64_t query_addr;
  RT_CHECK(vx_mem_alloc(device, vec_size, VX_MEM_READ, &query_buffer));
  RT_CHECK(vx_mem_address(query_buffer, &query_addr));

  // Result buffer (single uint32 for leaf node ID)
  uint64_t result_addr;
  RT_CHECK(vx_mem_alloc(device, sizeof(uint32_t), VX_MEM_READ_WRITE, &result_buffer));
  RT_CHECK(vx_mem_address(result_buffer, &result_addr));

  // Upload node vectors
  node_vec_buffers.resize(num_nodes);
  for (uint32_t i = 0; i < num_nodes; ++i) {
    RT_CHECK(vx_mem_alloc(device, vec_size, VX_MEM_READ, &node_vec_buffers[i]));
    RT_CHECK(vx_mem_address(node_vec_buffers[i], &nodes[i].vec_addr));
    RT_CHECK(vx_copy_to_dev(node_vec_buffers[i], nodes[i].vec.data(), 0, vec_size));
  }

  // Upload parent kernel
  std::cout << "upload parent kernel" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  // Upload child kernel
  std::cout << "upload child kernel" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, child_kernel_file, &child_krnl_buffer));

  uint64_t child_kernel_pc;
  RT_CHECK(vx_mem_address(child_krnl_buffer, &child_kernel_pc));
  std::cout << "child_kernel_pc=0x" << std::hex << child_kernel_pc << std::dec << std::endl;

  // Compute grid/block dimensions for child kernels
  // Use vec_len threads total, spread across warps
  uint32_t threadsPerBlock = vec_len;
  if (threadsPerBlock < 4) threadsPerBlock = 4;  // Minimum threads
  uint32_t blocksPerGrid = 1;

  std::cout << "child kernel: blocksPerGrid=" << blocksPerGrid 
            << ", threadsPerBlock=" << threadsPerBlock << std::endl;

  // Allocate and set up child_params_t for each node in device memory
  std::cout << "setting up node params in device memory" << std::endl;
  node_params_buffers.resize(num_nodes);
  
  // First pass: allocate params buffers and get addresses
  for (uint32_t i = 0; i < num_nodes; ++i) {
    RT_CHECK(vx_mem_alloc(device, sizeof(child_params_t), VX_MEM_READ, &node_params_buffers[i]));
    RT_CHECK(vx_mem_address(node_params_buffers[i], &nodes[i].params_addr));
  }

  // Second pass: fill in params with child addresses
  for (uint32_t i = 0; i < num_nodes; ++i) {
    child_params_t cparams;
    // Set grid and block dimensions for child kernel launches
    cparams.grid_dim[0] = blocksPerGrid;
    cparams.grid_dim[1] = 1;
    cparams.grid_dim[2] = 1;
    cparams.block_dim[0] = threadsPerBlock;
    cparams.block_dim[1] = 1;
    cparams.block_dim[2] = 1;
    
    cparams.vec_len = vec_len;
    cparams.query_addr = query_addr;
    cparams.node_vec_addr = nodes[i].vec_addr;
    cparams.kernel_pc = child_kernel_pc;
    cparams.result_addr = result_addr;
    cparams.node_id = nodes[i].node_id;
    cparams.padding = 0;

    if (nodes[i].left_child != -1) {
      cparams.left_child_params = nodes[nodes[i].left_child].params_addr;
      cparams.right_child_params = nodes[nodes[i].right_child].params_addr;
    } else {
      cparams.left_child_params = 0;
      cparams.right_child_params = 0;
    }

    RT_CHECK(vx_copy_to_dev(node_params_buffers[i], &cparams, 0, sizeof(child_params_t)));
  }

  // Upload query
  std::cout << "upload query vector" << std::endl;
  RT_CHECK(vx_copy_to_dev(query_buffer, query.data(), 0, vec_size));

  // Initialize result to invalid value
  uint32_t invalid_result = 0xFFFFFFFF;
  RT_CHECK(vx_copy_to_dev(result_buffer, &invalid_result, 0, sizeof(uint32_t)));

  // Set up parent kernel arguments
  kernel_arg_t kernel_arg;
  kernel_arg.vec_len = vec_len;
  kernel_arg.query_addr = query_addr;
  kernel_arg.root_params_addr = nodes[0].params_addr;  // Root node params
  kernel_arg.child_kernel_pc = child_kernel_pc;

  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  // Launch parent kernel - it will launch the first child kernel
  std::cout << "launching parent kernel (dynamic traversal begins)" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  // Read result
  uint32_t actual_leaf;
  RT_CHECK(vx_copy_from_dev(&actual_leaf, result_buffer, 0, sizeof(uint32_t)));

  std::cout << "reached leaf node: " << actual_leaf << std::endl;

  // Cleanup
  std::cout << "cleanup" << std::endl;
  cleanup();

  // Verify
  if (actual_leaf != (uint32_t)expected_leaf) {
    std::cout << "*** error: expected leaf " << expected_leaf << ", got " << actual_leaf << std::endl;
    std::cout << "FAILED!" << std::endl;
    return 1;
  }

  std::cout << "PASSED!" << std::endl;
  return 0;
}
