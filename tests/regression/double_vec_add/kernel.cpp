#include <vx_spawn.h>
#include <vx_intrinsics.h>
#include <vx_print.h>
#include "common.h"

// Parent kernel: Computes C = A + B
// Thread 0, warp 0 then launches child kernel to compute C = B + C
int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = static_cast<int>(csr_read(VX_CSR_CTA_ID));
    int threadId = vx_thread_id();

    // Calculate global thread ID
    int idx = warpId + threadId;

    TYPE* A = reinterpret_cast<TYPE*>(arg->buf_A);
    TYPE* B = reinterpret_cast<TYPE*>(arg->buf_B);
    TYPE* C = reinterpret_cast<TYPE*>(arg->buf_C);
    uint32_t size = arg->size;

    // Each thread computes C[idx] = A[idx] + B[idx]
    if ((uint32_t)idx < size) {
        C[idx] = A[idx] + B[idx];
    }

    // Spin wait to ensure all additions are complete (placeholder for barrier)
    // int spin = 0;
    // for (int i = 0; i < 1000; i++) {
    //     spin++;
    // }
    __syncthreads();
    // Only warp 0, thread 0 launches the child kernel
    if (warpId == 0 && threadId == 0) {
        // Set up child kernel parameters
        child_params_t* cp = reinterpret_cast<child_params_t*>(arg->child_params);
        cp->buf_B = arg->buf_B;
        cp->buf_C = arg->buf_C;
        cp->size = size;

        // Child kernel dimensions: match parent dimensions
        uint32_t grid_dim[3] = {size, 1, 1};
        uint32_t block_dim[3] = {1, 1, 1};

        // Launch child kernel dynamically
        dynamic_kernel_launch(arg->child_pc, grid_dim, block_dim, arg->child_params);
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
