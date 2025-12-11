#include <vx_spawn.h>
#include "common.h"

// Child kernel: sums all row sums from buffer B into final result C
void child_kernel_body(child_params_t* __UNIFORM__ params) {
    TYPE* B = reinterpret_cast<TYPE*>(params->buf_B);
    TYPE* C = reinterpret_cast<TYPE*>(params->buf_C);
    uint32_t num_rows = params->num_rows;

    // Single thread sums all row results
    TYPE sum = 0;
    for (uint32_t i = 0; i < num_rows; ++i) {
        sum += B[i];
    }
    C[0] = sum;
}

int main() {
    child_params_t* params = (child_params_t*)csr_read(VX_CSR_MSCRATCH);
    // Child kernel runs with single thread (grid=1, block=1)
    uint32_t grid_dim[1] = {1};
    uint32_t block_dim[1] = {1};
    return vx_spawn_threads(1, grid_dim, block_dim, (vx_kernel_func_cb)child_kernel_body, params);
}
