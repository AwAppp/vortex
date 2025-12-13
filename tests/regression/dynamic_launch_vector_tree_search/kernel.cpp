#include <vx_intrinsics.h>
#include <vx_print.h>
#include "common.h"

// Parent kernel: Launched by host with single thread
// Simply launches the root child kernel with the root node parameters
int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = vx_warp_id();
    int threadId = vx_thread_id();

    // Only warp 0, thread 0 does the launch
    if (warpId == 0 && threadId == 0) {
        child_params_t* root_params = reinterpret_cast<child_params_t*>(arg->root_params_addr);
        
        // Launch child kernel with the root node's parameters
        // Grid/block dims are stored in the root_params structure
        dynamic_kernel_launch(arg->child_kernel_pc, 
                              root_params->grid_dim, 
                              root_params->block_dim, 
                              arg->root_params_addr);
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
