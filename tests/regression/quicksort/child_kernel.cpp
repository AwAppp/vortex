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
// Returns the pivot index after partitioning
static int partition(TYPE* arr, int left, int right) {
    TYPE pivot = arr[right];  // Use rightmost element as pivot
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

// Insertion sort for small/medium arrays
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
// This avoids recursive kernel launches which have race conditions
static void quicksort_iterative(TYPE* arr, int left, int right) {
    // Simple stack for ranges to sort (max 32 levels should be plenty)
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
        
        // Use insertion sort for small partitions
        if (r - l < 8) {
            if (r > l) {
                insertion_sort(arr, l, r);
            }
            continue;
        }
        
        // Partition
        int pivot_idx = partition(arr, l, r);
        
        // Push right partition first (will be processed second)
        if (pivot_idx + 1 < r) {
            stack_left[top] = pivot_idx + 1;
            stack_right[top] = r;
            top++;
        }
        
        // Push left partition (will be processed first)
        if (l < pivot_idx - 1) {
            stack_left[top] = l;
            stack_right[top] = pivot_idx - 1;
            top++;
        }
    }
}

// Child kernel: Performs quicksort on the given range
int main() {
    child_params_t* params = (child_params_t*)csr_read(VX_CSR_MSCRATCH);

    int warpId = static_cast<int>(csr_read(VX_CSR_CTA_ID));
    int threadId = vx_thread_id();

    // Only warp 0, thread 0 performs the computation
    if (warpId == 0 && threadId == 0) {
        TYPE* arr = reinterpret_cast<TYPE*>(params->arr_addr);
        int left = static_cast<int>(params->left);
        int right = static_cast<int>(params->right);

        // Use iterative quicksort to avoid recursive kernel launch issues
        if (left < right) {
            quicksort_iterative(arr, left, right);
        }
    }

    // Terminate all warps except warp 0
    vx_tmc(warpId == 0);

    return 0;
}

