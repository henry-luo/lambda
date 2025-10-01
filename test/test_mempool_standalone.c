#include "../lib/mempool.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("=== Testing Jemalloc Memory Pool ===\n");

    // Test basic allocation
    void* ptr1 = pool_alloc(1024);
    if (!ptr1) {
        printf("ERROR: Failed to allocate 1024 bytes\n");
        return 1;
    }
    printf("✅ Successfully allocated 1024 bytes\n");

    // Test calloc
    void* ptr2 = pool_calloc(1, 512);
    if (!ptr2) {
        printf("ERROR: Failed to calloc 512 bytes\n");
        return 1;
    }
    printf("✅ Successfully calloced 512 bytes\n");

    // Verify calloc zeroed memory
    char* data = (char*)ptr2;
    for (int i = 0; i < 512; i++) {
        if (data[i] != 0) {
            printf("ERROR: calloc did not zero memory at index %d\n", i);
            return 1;
        }
    }
    printf("✅ Calloc properly zeroed memory\n");

    // Test multiple allocations
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(64 * (i + 1));
        if (!ptrs[i]) {
            printf("ERROR: Failed allocation %d\n", i);
            return 1;
        }
    }
    printf("✅ Multiple allocations successful\n");

    // Free individual allocations
    pool_free(ptr1);
    pool_free(ptr2);
    for (int i = 0; i < 10; i++) {
        pool_free(ptrs[i]);
    }
    printf("✅ All individual frees completed\n");

    printf("=== All tests passed! Jemalloc is working correctly ===\n");
    return 0;
}
