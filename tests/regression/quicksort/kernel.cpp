#include <vx_intrinsics.h>
#include <vx_print.h>
#include "common.h"

// Swap two elements
static inline void swap(TYPE* a, TYPE* b) {
    TYPE temp = *a;
    *a = *b;
    *b = temp;
}

// Partition function using Lomuto scheme
static int partition(TYPE* arr, int left, int right) {
    TYPE pivot = arr[right];
    int i = left - 1;

    for (int j = left; j < right; ++j) {
        if (arr[j] <= pivot) {
            ++i;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[right]);
    return i + 1;
}

// Insertion sort for small arrays
static void insertion_sort(TYPE* arr, int left, int right) {
    for (int i = left + 1; i <= right; ++i) {
        TYPE key = arr[i];
        int j = i - 1;
        while (j >= left && arr[j] > key) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

// Iterative quicksort using a simple stack
static void quicksort_iterative(TYPE* arr, int left, int right) {
    int stack_left[32];
    int stack_right[32];
    int top = 0;
    
    stack_left[top] = left;
    stack_right[top] = right;
    top++;
    
    while (top > 0) {
        top--;
        int l = stack_left[top];
        int r = stack_right[top];
        
        if (r - l < 8) {
            if (r > l) {
                insertion_sort(arr, l, r);
            }
            continue;
        }
        
        int pivot_idx = partition(arr, l, r);
        
        if (pivot_idx + 1 < r) {
            stack_left[top] = pivot_idx + 1;
            stack_right[top] = r;
            top++;
        }
        
        if (l < pivot_idx - 1) {
            stack_left[top] = l;
            stack_right[top] = pivot_idx - 1;
            top++;
        }
    }
}

// Single kernel that does quicksort - NO dynamic kernel launch
int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = static_cast<int>(csr_read(VX_CSR_CTA_ID));
    int threadId = vx_thread_id();

    // Only warp 0, thread 0 does the sorting
    if (warpId == 0 && threadId == 0) {
        TYPE* src = reinterpret_cast<TYPE*>(arg->src_addr);
        TYPE* dst = reinterpret_cast<TYPE*>(arg->dst_addr);
        uint32_t n = arg->num_points;

        // Copy src to dst first
        for (uint32_t i = 0; i < n; ++i) {
            dst[i] = src[i];
        }

        // Sort dst in-place
        if (n > 1) {
            quicksort_iterative(dst, 0, n - 1);
        }
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}

