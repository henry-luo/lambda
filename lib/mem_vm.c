#include "mem_vm.h"
#include "memtrack.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#define MEM_VM_MAGIC 0x4D56524Eu

struct MemVmRegion {
    uint32_t magic;
    uint32_t _pad;
    MemContext* context;
    MemNode* owner;
    MemRole role;
    void* base;
    size_t reserved;
    size_t committed;
    size_t alignment;
    uint8_t* committed_pages;
    size_t page_count;
};

static bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static bool checked_add_size(size_t left, size_t right, size_t* out) {
    if (right > SIZE_MAX - left) return false;
    *out = left + right;
    return true;
}

static bool round_up_size(size_t value, size_t quantum, size_t* out) {
    if (quantum == 0) return false;
    size_t remainder = value % quantum;
    return remainder == 0
        ? (*out = value, true)
        : checked_add_size(value, quantum - remainder, out);
}

size_t mem_vm_page_size(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (size_t)info.dwPageSize;
#else
    long page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0 ? (size_t)page_size : 4096u;
#endif
}

static void* vm_reserve(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* base = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return base == MAP_FAILED ? NULL : base;
#endif
}

static bool vm_commit(void* base, size_t offset, size_t size) {
#ifdef _WIN32
    return VirtualAlloc((uint8_t*)base + offset, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
    return mprotect((uint8_t*)base + offset, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

static bool vm_decommit(void* base, size_t offset, size_t size) {
#ifdef _WIN32
    return VirtualFree((uint8_t*)base + offset, size, MEM_DECOMMIT) != 0;
#else
    bool protected = mprotect((uint8_t*)base + offset, size, PROT_NONE) == 0;
#ifdef MADV_DONTNEED
    if (protected) (void)madvise((uint8_t*)base + offset, size, MADV_DONTNEED);
#endif
    return protected;
#endif
}

static void vm_release(void* base, size_t size) {
#ifdef _WIN32
    (void)size;
    (void)VirtualFree(base, 0, MEM_RELEASE);
#else
    (void)munmap(base, size);
#endif
}

static void* vm_reserve_after_reclaim(MemContext* context, size_t size) {
    void* base = vm_reserve(size);
    if (base) return base;
    // OS reservation failure is the page-provider boundary: let the single
    // MemContext coordinator reclaim evictable memory before one retry.
    mem_context_request_reclaim(context, MEM_PRESSURE_HIGH, size);
    return vm_reserve(size);
}

static bool vm_commit_after_reclaim(MemContext* context, void* base,
                                    size_t offset, size_t size) {
    if (vm_commit(base, offset, size)) return true;
    mem_context_request_reclaim(context, MEM_PRESSURE_HIGH, size);
    return vm_commit(base, offset, size);
}

MemVmRegion* mem_vm_region_reserve(MemContext* context, MemNode* owner,
                                   MemRole role, size_t size, size_t alignment) {
    size_t page_size = mem_vm_page_size();
    if (size == 0 || page_size == 0) return NULL;
    if (alignment < page_size) alignment = page_size;
    // v1 maps one page-aligned extent; larger alignment requires a dedicated
    // over-reservation/trim protocol and is rejected instead of being false.
    if (!is_power_of_two(alignment) || alignment > page_size) return NULL;

    size_t reserved = 0;
    if (!round_up_size(size, page_size, &reserved)) return NULL;
    MemVmRegion* region = (MemVmRegion*)mem_alloc(sizeof(MemVmRegion), MEM_CAT_SYSTEM);
    if (!region) return NULL;

    size_t page_count = reserved / page_size;
    uint8_t* committed_pages = (uint8_t*)mem_calloc(
        page_count, sizeof(uint8_t), MEM_CAT_SYSTEM);
    if (!committed_pages) {
        mem_free(region);
        return NULL;
    }

    if (memtrack_fault_should_fail()) {
        mem_free(committed_pages);
        mem_free(region);
        return NULL;
    }

    void* base = vm_reserve_after_reclaim(context, reserved);
    if (!base) {
        mem_free(committed_pages);
        mem_free(region);
        return NULL;
    }

    memset(region, 0, sizeof(*region));
    region->magic = MEM_VM_MAGIC;
    region->context = context;
    region->owner = owner;
    region->role = role;
    region->base = base;
    region->reserved = reserved;
    region->alignment = alignment;
    region->committed_pages = committed_pages;
    region->page_count = page_count;
    return region;
}

static bool region_page_range_valid(const MemVmRegion* region, size_t offset,
                                    size_t size, size_t* first_page,
                                    size_t* page_count) {
    if (!region || region->magic != MEM_VM_MAGIC || !region->base) return false;
    size_t page_size = mem_vm_page_size();
    if (page_size == 0 || offset % page_size != 0 || size == 0 ||
        size % page_size != 0 || offset > region->reserved ||
        size > region->reserved - offset) return false;
    size_t first = offset / page_size;
    size_t count = size / page_size;
    if (first > region->page_count || count > region->page_count - first) return false;
    if (first_page) *first_page = first;
    if (page_count) *page_count = count;
    return true;
}

bool mem_vm_region_commit(MemVmRegion* region, size_t offset, size_t size) {
    size_t first_page = 0;
    size_t page_count = 0;
    if (!region_page_range_valid(region, offset, size, &first_page, &page_count)) {
        return false;
    }
    for (size_t i = 0; i < page_count; i++) {
        if (region->committed_pages[first_page + i]) return false;
    }
    if (memtrack_fault_should_fail()) return false;
    if (!vm_commit_after_reclaim(region->context, region->base, offset, size)) {
        return false;
    }
    memset(region->committed_pages + first_page, 1, page_count);
    region->committed += size;
    return true;
}

bool mem_vm_region_decommit(MemVmRegion* region, size_t offset, size_t size) {
    size_t first_page = 0;
    size_t page_count = 0;
    if (!region_page_range_valid(region, offset, size, &first_page, &page_count)) {
        return false;
    }
    for (size_t i = 0; i < page_count; i++) {
        if (!region->committed_pages[first_page + i]) return false;
    }
    if (!vm_decommit(region->base, offset, size)) return false;
    memset(region->committed_pages + first_page, 0, page_count);
    region->committed = region->committed >= size ? region->committed - size : 0;
    return true;
}

void mem_vm_region_release(MemVmRegion* region) {
    if (!region || region->magic != MEM_VM_MAGIC) return;
    void* base = region->base;
    size_t reserved = region->reserved;
    region->magic = 0;
    region->base = NULL;
    region->reserved = 0;
    region->committed = 0;
    uint8_t* committed_pages = region->committed_pages;
    region->committed_pages = NULL;
    region->page_count = 0;
    vm_release(base, reserved);
    mem_free(committed_pages);
    mem_free(region);
}

void* mem_vm_region_base(const MemVmRegion* region) {
    return region && region->magic == MEM_VM_MAGIC ? region->base : NULL;
}

size_t mem_vm_region_reserved_bytes(const MemVmRegion* region) {
    return region && region->magic == MEM_VM_MAGIC ? region->reserved : 0;
}

size_t mem_vm_region_committed_bytes(const MemVmRegion* region) {
    return region && region->magic == MEM_VM_MAGIC ? region->committed : 0;
}

MemRole mem_vm_region_role(const MemVmRegion* region) {
    return region && region->magic == MEM_VM_MAGIC ? region->role : MEM_ROLE_UNKNOWN;
}

MemNode* mem_vm_region_owner(const MemVmRegion* region) {
    return region && region->magic == MEM_VM_MAGIC ? region->owner : NULL;
}
