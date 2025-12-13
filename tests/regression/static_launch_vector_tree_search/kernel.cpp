#include <vx_intrinsics.h>
#include <vx_spawn.h>
#include <vx_print.h>
#include "common.h"

// Parallel dot product kernel using all available threads
// Uses shared memory for reduction
int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

    TYPE* query = reinterpret_cast<TYPE*>(arg->query_addr);
    TYPE* node_vec = reinterpret_cast<TYPE*>(arg->node_vec_addr);
    TYPE* result = reinterpret_cast<TYPE*>(arg->result_addr);
    uint32_t n = arg->vec_len;

    // Get thread info using vx_warp_id (matches dynamic version)
    int warpId = vx_warp_id();
    int threadId = vx_thread_id();
    int numThreads = vx_num_threads();
    
    // Calculate number of warps from block_dim / num_threads (matches dynamic version)
    int numWarps = arg->block_dim / numThreads;
    if (numWarps < 1) numWarps = 1;
    
    // Global thread ID and total threads
    int tid = warpId * numThreads + threadId;
    int totalThreads = numWarps * numThreads;
    int localThreadId = threadId;  // Within warp

    // Allocate shared memory for reduction using __local_mem
    TYPE* cache = reinterpret_cast<TYPE*>(__local_mem(numThreads * sizeof(TYPE)));

    // Each thread computes partial sum
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

    // Warp 0, thread 0 writes final result
    if (warpId == 0 && threadId == 0) {
        *result = cache[0];
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
