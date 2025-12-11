#include <vx_spawn.h>
#include <vx_intrinsics.h>
#include "common.h"

// Parent kernel: each thread sums one row of matrix A into buffer B
// Thread 0 of block 0 then launches the child kernel
void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
    TYPE* A = reinterpret_cast<TYPE*>(arg->buf_A);
    TYPE* B = reinterpret_cast<TYPE*>(arg->buf_B);
    uint32_t rows = arg->rows;
    uint32_t cols = arg->cols;

    int row = blockIdx.x * blockDim.x + threadIdx.x;

    // Each thread sums one row
    if ((uint32_t)row < rows) {
        TYPE sum = 0;
        for (uint32_t c = 0; c < cols; ++c) {
            sum += A[row * cols + c];
        }
        B[row] = sum;
    }

    // Barrier to ensure all row sums are complete
    __syncthreads();

    // Only thread 0 of block 0 launches the child kernel
    // This ensures dynamic_kernel_launch is called once per threadblock
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        // Set up child kernel parameters
        child_params_t* cp = reinterpret_cast<child_params_t*>(arg->child_params);
        cp->buf_B = arg->buf_B;
        cp->buf_C = arg->buf_C;
        cp->num_rows = rows;

        // Child kernel dimensions: single thread
        uint32_t grid_dim[3] = {1, 1, 1};
        uint32_t block_dim[3] = {1, 1, 1};

        // Launch child kernel dynamically
        dynamic_kernel_launch(arg->child_pc, grid_dim, block_dim, arg->child_params);
    }
}

int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
    return vx_spawn_threads(1, arg->grid_dim, arg->block_dim, (vx_kernel_func_cb)kernel_body, arg);
}
