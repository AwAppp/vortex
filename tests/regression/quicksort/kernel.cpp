#include <vx_intrinsics.h>
#include <vx_print.h>
#include "common.h"

// Parent kernel: Sets up initial quicksort parameters and launches child kernel
int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = static_cast<int>(csr_read(VX_CSR_CTA_ID));
    int threadId = vx_thread_id();

    // Only warp 0, thread 0 does the setup and launch
    if (warpId == 0 && threadId == 0) {
        // Copy input array to output (sort in-place on dst)
        TYPE* src = reinterpret_cast<TYPE*>(arg->src_addr);
        TYPE* dst = reinterpret_cast<TYPE*>(arg->dst_addr);
        uint32_t n = arg->num_points;

        // Copy src to dst first (we'll sort dst in-place)
        for (uint32_t i = 0; i < n; ++i) {
            dst[i] = src[i];
        }

        // Set up child kernel parameters for full array
        child_params_t* cp = reinterpret_cast<child_params_t*>(arg->child_params);
        cp->arr_addr = arg->dst_addr;
        cp->child_params = arg->child_params;  // Same buffer for recursion
        cp->child_pc = arg->child_pc;
        cp->left = 0;
        cp->right = n > 0 ? n - 1 : 0;
        cp->depth = 0;

        // Only launch if we have elements to sort
        if (n > 1) {
            // Child kernel dimensions: single thread for now
            uint32_t grid_dim[3] = {1, 1, 1};
            uint32_t block_dim[3] = {1, 1, 1};

            // Launch child kernel dynamically
            dynamic_kernel_launch(arg->child_pc, grid_dim, block_dim, arg->child_params);
        }
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
