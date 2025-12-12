// #include <vx_spawn.h>
#include <vx_intrinsics.h>
#include "common.h"

// Child kernel: Computes C = B + C (adds B to the result from parent kernel)
// This kernel is launched dynamically by the parent kernel
int main() {
    child_params_t* params = (child_params_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = static_cast<int>(csr_read(VX_CSR_CTA_ID));
    int threadId = vx_thread_id();

    // Calculate global thread ID
    int idx = warpId + threadId;

    TYPE* B = reinterpret_cast<TYPE*>(params->buf_B);
    TYPE* C = reinterpret_cast<TYPE*>(params->buf_C);
    uint32_t size = params->size;

    // Each thread computes C[idx] = B[idx] + C[idx]
    if ((uint32_t)idx < size) {
        C[idx] = B[idx] + C[idx];
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}
