#include <vx_intrinsics.h>
#include <vx_spawn.h>
#include <vx_print.h>
#include "common.h"

// Child kernel: Performs parallel dotproduct and recursive tree traversal
// Uses multiple threads/warps for dotproduct, then thread 0 makes traversal decision
int main() {
    child_params_t* params = (child_params_t*)csr_read(VX_CSR_MSCRATCH);

    TYPE* query = reinterpret_cast<TYPE*>(params->query_addr);
    TYPE* node_vec = reinterpret_cast<TYPE*>(params->node_vec_addr);
    uint32_t n = params->vec_len;

    // Get thread info using vx_warp_id as specified
    int warpId = vx_warp_id();
    int threadId = vx_thread_id();
    int numThreads = vx_num_threads();
    
    // Calculate number of warps from block_dim / num_threads
    int numWarps = params->block_dim[0] / numThreads;
    if (numWarps < 1) numWarps = 1;
    
    // Global thread ID within the kernel
    int tid = warpId * numThreads + threadId;
    int totalThreads = numWarps * numThreads;
    int localThreadId = threadId;

    // Allocate shared memory for reduction using __local_mem
    TYPE* cache = reinterpret_cast<TYPE*>(__local_mem(numThreads * sizeof(TYPE)));

    // Parallel dot product: each thread computes partial sum
    TYPE temp = 0;
    for (int i = tid; i < (int)n; i += totalThreads) {
        temp += query[i] * node_vec[i];
    }

    // Store in shared memory
    cache[localThreadId] = temp;
    
    __syncthreads();

    // Parallel reduction within warp
    for (int stride = numThreads / 2; stride > 0; stride /= 2) {
        if (localThreadId < stride) {
            cache[localThreadId] += cache[localThreadId + stride];
        }
        __syncthreads();
    }

    // Only warp 0, thread 0 does tree traversal decision and next launch
    if (warpId == 0 && threadId == 0) {
        TYPE dot = cache[0];

        // Check if this is a leaf node
        bool is_leaf = (params->left_child_params == 0) && (params->right_child_params == 0);

        if (is_leaf) {
            // Write final result (leaf node ID)
            uint32_t* result = reinterpret_cast<uint32_t*>(params->result_addr);
            *result = params->node_id;
        } else {
            // Select next child based on dot product sign
            uint64_t next_child_params;
            if (dot >= 0) {
                next_child_params = params->left_child_params;
            } else {
                next_child_params = params->right_child_params;
            }

            // Get the child's params to access its grid/block dims
            child_params_t* child = reinterpret_cast<child_params_t*>(next_child_params);

            // Launch next child kernel with proper dimensions from child params
            dynamic_kernel_launch(params->kernel_pc, child->grid_dim, child->block_dim, next_child_params);
        }
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
