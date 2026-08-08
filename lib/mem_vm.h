#ifndef MEM_VM_H
#define MEM_VM_H

#include "mem_context.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MemVmRegion MemVmRegion;

size_t mem_vm_page_size(void);

MemVmRegion* mem_vm_region_reserve(MemContext* context, MemNode* owner,
                                   MemRole role, size_t size, size_t alignment);
bool mem_vm_region_commit(MemVmRegion* region, size_t offset, size_t size);
bool mem_vm_region_decommit(MemVmRegion* region, size_t offset, size_t size);
void mem_vm_region_release(MemVmRegion* region);
void* mem_vm_region_base(const MemVmRegion* region);

size_t mem_vm_region_reserved_bytes(const MemVmRegion* region);
size_t mem_vm_region_committed_bytes(const MemVmRegion* region);
MemRole mem_vm_region_role(const MemVmRegion* region);
MemNode* mem_vm_region_owner(const MemVmRegion* region);

#ifdef __cplusplus
}
#endif

#endif // MEM_VM_H
