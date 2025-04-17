#include "transpiler.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#define INITIAL_PACK_SIZE 64
#define VIRTUAL_PACK_THRESHOLD 4096
#define DEFAULT_PAGE_SIZE 4096

static int convert_to_virtual_impl(Pack* pack);
static void vm_grow(Pack* pack, size_t needed_size);
static void* vm_reserve(size_t size);
static int vm_commit(void* addr, size_t size);
static void vm_release(void* addr, size_t size);

// Global page size variable
static size_t g_page_size = 0;

// Get the system page size, initialize if needed
static size_t get_page_size() {
#if defined(__APPLE__) || defined(__linux__)
    if (g_page_size == 0) {
        g_page_size = (size_t)sysconf(_SC_PAGESIZE);
    }
#else
    if (g_page_size == 0) {
        g_page_size = DEFAULT_PAGE_SIZE;
    }
#endif
    return g_page_size;
}

Pack* pack_init(size_t initial_size) {
    Pack* pack = (Pack*)malloc(sizeof(Pack));
    if (!pack) return NULL;
    
    // Use the provided initial size or default if 0
    size_t actual_size = initial_size > 0 ? initial_size : INITIAL_PACK_SIZE;
    
    pack->size = 0;
    
    // Check if we should use virtual memory directly
    if (actual_size >= VIRTUAL_PACK_THRESHOLD) {
        // Get system page size
        size_t page_size = get_page_size();
        
        // Align capacity to page size
        size_t aligned_capacity = ((actual_size + page_size - 1) / page_size) * page_size;
        
        // Reserve 4x for future growth (same as in convert_to_virtual_impl)
        size_t reserve_size = aligned_capacity * 4;
        
        #if defined(__APPLE__) || defined(__linux__)
        // Reserve virtual memory
        void* mem = vm_reserve(reserve_size);
        if (!mem) {
            free(pack);
            return NULL;
        }
        
        // Commit initial portion
        if (!vm_commit(mem, aligned_capacity)) {
            vm_release(mem, reserve_size);
            free(pack);
            return NULL;
        }
        
        pack->data = mem;
        pack->capacity = reserve_size;       // Total reserved size
        pack->committed_size = aligned_capacity; // Currently committed size
        #else
        // Fallback for unsupported platforms
        pack->data = malloc(actual_size);
        if (!pack->data) {
            free(pack);
            return NULL;
        }
        pack->capacity = actual_size;
        pack->committed_size = 0; // Non-virtual
        #endif
    } else {
        // Standard allocation for smaller sizes
        pack->capacity = actual_size;
        pack->committed_size = 0; // Non-virtual
        pack->data = malloc(actual_size);
        if (!pack->data) {
            free(pack);
            return NULL;
        }
    }
    
    return pack;
}

// Update the convert_to_virtual function to use the platform-specific implementation
static int convert_to_virtual(Pack* pack) {
    return convert_to_virtual_impl(pack);
}

// Update the pack_alloc function to use vm_grow for virtual memory
void* pack_alloc(Pack* pack, size_t size) {
    // Make sure we have enough space
    if (pack->size + size > pack->capacity) {
        if (pack->committed_size == 0 && (pack->capacity >= VIRTUAL_PACK_THRESHOLD || pack->size + size >= VIRTUAL_PACK_THRESHOLD)) {
            // Convert to virtual memory pack
            if (!convert_to_virtual(pack)) {
                // If conversion fails, try to grow normally
                size_t new_capacity = pack->capacity * 2;
                while (new_capacity < pack->size + size) {
                    new_capacity *= 2;
                }
                
                void* new_data = realloc(pack->data, new_capacity);
                if (!new_data) return NULL;
                
                pack->data = new_data;
                pack->capacity = new_capacity;
            }
        } else if (pack->committed_size > 0) {
            // Grow virtual memory
            vm_grow(pack, pack->size + size);
        } else {
            // Grow regular memory
            size_t new_capacity = pack->capacity * 2;
            while (new_capacity < pack->size + size) {
                new_capacity *= 2;
            }
            
            void* new_data = realloc(pack->data, new_capacity);
            if (!new_data) return NULL;
            
            pack->data = new_data;
            pack->capacity = new_capacity;
        }
    }
    
    // Return NULL if we couldn't ensure enough capacity
    if (pack->committed_size > 0 && pack->size + size > pack->committed_size) {
        return NULL;
    }
    
    // Allocate from the pack
    void* ptr = (char*)pack->data + pack->size;
    pack->size += size;
    return ptr;
}

void* pack_calloc(Pack* pack, size_t size) {
    // Allocate memory and zero it out
    void* ptr = pack_alloc(pack, size);
    if (ptr) { memset(ptr, 0, size);}
    return ptr;
}

#if defined(__APPLE__) || defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>

static void* vm_reserve(size_t size) {
    // Reserve virtual memory without committing it
    void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
    return ptr;
}

static int vm_commit(void* addr, size_t size) {
    // Commit memory to physical pages, making it accessible
    return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
}

static int vm_decommit(void* addr, size_t size) {
    // Decommit memory but keep reservation
    return mprotect(addr, size, PROT_NONE) == 0;
}

static void vm_release(void* addr, size_t size) {
    // Release virtual memory completely
    munmap(addr, size);
}

static int convert_to_virtual_impl(Pack* pack) {
    void* old_data = pack->data;
    size_t old_size = pack->size;
    
    // Get system page size
    size_t page_size = get_page_size();
    
    // Align capacity to page size
    size_t min_capacity = ((VIRTUAL_PACK_THRESHOLD + page_size - 1) / page_size) * page_size;
    
    // Reserve virtual memory
    void* new_data = vm_reserve(min_capacity * 4); // Reserve 4x the minimum for future growth
    if (!new_data) return 0;
    
    // Commit initial portion
    if (!vm_commit(new_data, min_capacity)) {
        vm_release(new_data, min_capacity * 4);
        return 0;
    }
    
    // Copy data from old allocation
    memcpy(new_data, old_data, old_size);
    free(old_data);
    
    // Update pack
    pack->data = new_data;
    pack->capacity = min_capacity * 4;  // Total reserved size
    pack->committed_size = min_capacity; // Currently committed size
    
    return 1;
}

static void vm_grow(Pack* pack, size_t needed_size) {
    // Calculate new committed size (align to page size)
    size_t current_committed = pack->committed_size;
    size_t new_committed = current_committed;
    
    while (needed_size > new_committed) {
        new_committed *= 2;
    }
    
    // Check if we need to commit more memory
    if (new_committed > current_committed) {
        // Make sure we don't exceed the reserved size
        if (new_committed > pack->capacity) {
            // Need to re-reserve larger area
            size_t new_reserve = pack->capacity * 2;
            void* new_data = vm_reserve(new_reserve);
            if (!new_data) return;
            
            // Commit and copy data
            if (!vm_commit(new_data, new_committed)) {
                vm_release(new_data, new_reserve);
                return;
            }
            
            memcpy(new_data, pack->data, pack->size);
            vm_release(pack->data, pack->capacity);
            
            pack->data = new_data;
            pack->capacity = new_reserve;
        } else {
            // Just commit more of the existing reservation
            void* commit_start = (char*)pack->data + current_committed;
            size_t commit_size = new_committed - current_committed;
            vm_commit(commit_start, commit_size);
        }
        
        pack->committed_size = new_committed;
    }
}

void pack_free(Pack* pack) {
    if (pack->committed_size > 0) {
        vm_release(pack->data, pack->capacity);
    } else {
        free(pack->data);
    }
    free(pack);
}

#else // Windows or other platforms
// Placeholder for non-Unix platforms
static int convert_to_virtual_impl(Pack* pack) {
    // Fallback to regular memory for unsupported platforms
    return 0;
}

static void vm_grow(Pack* pack, size_t needed_size) {
    // Fallback implementation using regular malloc/realloc
    size_t new_capacity = pack->capacity * 2;
    while (new_capacity < needed_size) {
        new_capacity *= 2;
    }
    
    void* new_data = realloc(pack->data, new_capacity);
    if (new_data) {
        pack->data = new_data;
        pack->capacity = new_capacity;
        pack->committed_size = new_capacity;
    }
}

void pack_free(Pack* pack) {
    free(pack->data);
    free(pack);
}
#endif

Heap* heap_init(size_t initial_size) {
    return (Heap*)pack_init(initial_size);
}

void* heap_alloc(Heap* heap, size_t size) {
    return pack_alloc((Pack*)heap, size);
}

void* heap_calloc(Heap* heap, size_t size) {
    return heap_calloc(heap, size);
}

void heap_free(Heap* heap) {
    pack_free((Pack*)heap);
}