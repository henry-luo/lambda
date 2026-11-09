#pragma once
#include "lib/process_sandbox.h"
#include <crt_externs.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct {
    bool requested;
    bool sandboxed;
    int open_error;
    int map_error;
    char* memory;
    bool available;
    int external_pager;
} safety_constructor;

__attribute__((constructor)) static void safety_constructor_acquire(void) {
    char** argv = *_NSGetArgv();
#ifdef SAFETY_CONSTRUCTOR_MODULE
    if (*_NSGetArgc() != 4) return;
    const char* path = argv[3];
    bool shared = SAFETY_CONSTRUCTOR_MODULE == 1;
#else
    if (*_NSGetArgc() != 9 || (strcmp(argv[8], "constructor-shared") && strcmp(argv[8], "constructor-private"))) return;
    const char* path = argv[4];
    bool shared = !strcmp(argv[8], "constructor-shared");
#endif
    safety_constructor.requested = true;
    safety_constructor.sandboxed = process_sandbox_is_applied(getpid());
    int fd = open(path, O_RDWR | O_NOFOLLOW);
    if (fd < 0) { safety_constructor.open_error = errno; return; }
    void* memory = mmap(NULL, vm_page_size, PROT_READ | PROT_WRITE, shared ? MAP_SHARED : MAP_PRIVATE, fd, 0);
    safety_constructor.map_error = memory == MAP_FAILED ? errno : 0;
    close(fd);
    if (memory == MAP_FAILED) return;
    safety_constructor.memory = (char*)memory;
    // dirty the private page without changing the fixture's visible contents.
    if (!shared) *(volatile char*)memory = *(volatile char*)memory;
}
static int safety_constructor_report(char* text, size_t capacity) {
    if (safety_constructor.memory) {
        safety_constructor.available = *(volatile char*)safety_constructor.memory == 'h';
        mach_vm_address_t address = (mach_vm_address_t)safety_constructor.memory;
        mach_vm_size_t length = 0; natural_t depth = 0;
        vm_region_submap_info_data_64_t region = {0};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (mach_vm_region_recurse(mach_task_self(), &address, &length, &depth,
                (vm_region_recurse_info_t)&region, &count) != KERN_SUCCESS) return -1;
        safety_constructor.external_pager = region.external_pager;
    }
    return snprintf(text, capacity,
        "{\"sandboxed\":%s,\"open_errno\":%d,\"map_errno\":%d,\"available\":%s,\"external_pager\":%d}",
        safety_constructor.sandboxed ? "true" : "false", safety_constructor.open_error,
        safety_constructor.map_error, safety_constructor.available ? "true" : "false", safety_constructor.external_pager);
}
static void safety_constructor_close(void) {
    if (safety_constructor.memory) munmap(safety_constructor.memory, vm_page_size);
}
