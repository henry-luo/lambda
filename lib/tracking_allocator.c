// tracking_allocator.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <unistd.h>

#define MAX_BACKTRACE_DEPTH 16

typedef struct AllocationInfo {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    void *backtrace_array[MAX_BACKTRACE_DEPTH];
    int backtrace_size;
    struct AllocationInfo *next;
} AllocationInfo;

static AllocationInfo *allocations = NULL;

// Helper: Print backtrace
void print_backtrace(void **array, int size) {
    char **symbols = backtrace_symbols(array, size);
    if (symbols == NULL) {
        perror("backtrace_symbols");
        return;
    }

    for (int i = 0; i < size; ++i) {
        printf("    [%d] %s\n", i, symbols[i]);
    }
    free(symbols);
}

// Track new allocation
void track_allocation(void *ptr, size_t size, const char *file, int line) {
    AllocationInfo *info = malloc(sizeof(AllocationInfo));
    if (!info) return;

    info->ptr = ptr;
    info->size = size;
    info->file = file;
    info->line = line;
    info->backtrace_size = backtrace(info->backtrace_array, MAX_BACKTRACE_DEPTH);

    info->next = allocations;
    allocations = info;
}

// Untrack existing allocation
void untrack_allocation(void *ptr) {
    AllocationInfo **curr = &allocations;
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            AllocationInfo *to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            return;
        }
        curr = &((*curr)->next);
    }
}

// Check if pointer is already freed
int is_tracked(void *ptr) {
    AllocationInfo *curr = allocations;
    while (curr) {
        if (curr->ptr == ptr)
            return 1;
        curr = curr->next;
    }
    return 0;
}

// Dump unfreed allocations
void dump_leaks(void) {
    printf("\n=== MEMORY LEAK REPORT ===\n");
    AllocationInfo *curr = allocations;
    int count = 0;

    while (curr) {
        printf("Leak: %p (%zu bytes) from %s:%d\n", curr->ptr, curr->size, curr->file, curr->line);
        print_backtrace(curr->backtrace_array, curr->backtrace_size);
        curr = curr->next;
        count++;
    }

    if (count == 0)
        printf("No memory leaks detected.\n");
    else
        printf("%d leaks detected.\n", count);
}

// Tracking malloc
void *tracking_malloc(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (ptr) {
        track_allocation(ptr, size, file, line);
    }
    return ptr;
}

// Tracking free with double-free detection
void tracking_free(void *ptr, const char *file, int line) {
    if (!ptr) return;

    if (!is_tracked(ptr)) {
        fprintf(stderr, "Double free or invalid free at %s:%d for %p\n", file, line, ptr);
        return;
    }

    untrack_allocation(ptr);
    free(ptr);
}

// Tracking realloc
void *tracking_realloc(void *ptr, size_t size, const char *file, int line) {
    if (ptr && !is_tracked(ptr)) {
        fprintf(stderr, "Realloc of untracked memory at %s:%d for %p\n", file, line, ptr);
        return NULL;
    }

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) return NULL;

    if (ptr) untrack_allocation(ptr);
    track_allocation(new_ptr, size, file, line);
    return new_ptr;
}

// Macros for easier use
#define t_malloc(sz) tracking_malloc(sz, __FILE__, __LINE__)
#define t_free(p) tracking_free(p, __FILE__, __LINE__)
#define t_realloc(p, sz) tracking_realloc(p, sz, __FILE__, __LINE__)

// Register leak dump at exit
__attribute__((constructor))
static void setup_leak_tracker(void) {
    atexit(dump_leaks);
}

// Test (you can remove main if embedding into a larger project)
int main(void) {
    char *a = t_malloc(128);
    char *b = t_malloc(64);
    a = t_realloc(a, 256);
    t_free(b);
    t_free(b); // Intentional double-free

    // t_free(a); // Uncomment to fix leak

    return 0;
}
