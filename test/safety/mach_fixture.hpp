#pragma once
#include "../../lib/process_sandbox.h"
#include <mach/mach.h>
#include <mach/exception_types.h>
#include <mach/mach_vm.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <errno.h>

// Real inherited capabilities, not claimed port names in a worker message.
struct SafetyMachFixture {
    mach_port_t marker = MACH_PORT_NULL;
    mach_port_t memory_entry = MACH_PORT_NULL;
    mach_vm_address_t memory = 0;
    mach_port_array_t saved = nullptr;
    mach_msg_type_number_t saved_count = 0;
    exception_mask_t masks[EXC_TYPES_COUNT] = {};
    mach_port_t exceptions[EXC_TYPES_COUNT] = {};
    exception_behavior_t behaviors[EXC_TYPES_COUNT] = {};
    thread_state_flavor_t flavors[EXC_TYPES_COUNT] = {};
    mach_msg_type_number_t count = EXC_TYPES_COUNT;
    bool registered = false;
    bool changed_exception = false;
    SafetyMachFixture() = default;
    SafetyMachFixture(const SafetyMachFixture&) = delete;
    SafetyMachFixture& operator=(const SafetyMachFixture&) = delete;
    bool open() {
        task_t task = mach_task_self();
        if (mach_ports_lookup(task, &saved, &saved_count) != KERN_SUCCESS ||
                task_get_exception_ports(task, EXC_MASK_BREAKPOINT, masks, &count,
                    exceptions, behaviors, flavors) != KERN_SUCCESS ||
                mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &marker) != KERN_SUCCESS ||
                mach_port_insert_right(task, marker, marker, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) return false;
        mach_port_limits_t limits = {16};
        if (mach_port_set_attributes(task, marker, MACH_PORT_LIMITS_INFO,
                (mach_port_info_t)&limits, MACH_PORT_LIMITS_INFO_COUNT) != KERN_SUCCESS) return false;
        memory_object_size_t size = vm_page_size;
        if (mach_vm_allocate(task, &memory, size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) return false;
        *(volatile unsigned char*)memory = 0x4c;
        if (mach_make_memory_entry_64(task, &size, memory, VM_PROT_READ | VM_PROT_WRITE,
                &memory_entry, MACH_PORT_NULL) != KERN_SUCCESS) return false;
        mach_port_t ports[] = {marker, memory_entry, marker};
        registered = mach_ports_register(task, ports, 3) == KERN_SUCCESS;
        changed_exception = registered && task_set_exception_ports(task, EXC_MASK_BREAKPOINT,
            marker, EXCEPTION_DEFAULT, THREAD_STATE_NONE) == KERN_SUCCESS;
        return changed_exception;
    }
    unsigned received() {
        unsigned received = 0;
        while (marker) {
            struct { mach_msg_header_t header; mach_msg_trailer_t trailer; } message = {};
            kern_return_t status = mach_msg(&message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0, sizeof(message), marker, 0, MACH_PORT_NULL);
            if (status != MACH_MSG_SUCCESS) break;
            received++; mach_msg_destroy(&message.header);
        }
        return received;
    }
    bool memory_changed() { return memory && *(volatile unsigned char*)memory != 0x4c; }
    ~SafetyMachFixture() {
        task_t task = mach_task_self();
        if (changed_exception) for (unsigned i = 0; i < count; i++)
            (void)task_set_exception_ports(task, masks[i], exceptions[i], behaviors[i], flavors[i]);
        if (registered) (void)mach_ports_register(task, saved, saved_count);
        for (unsigned i = 0; i < count; i++) if (exceptions[i]) mach_port_deallocate(task, exceptions[i]);
        if (saved) {
            for (unsigned i = 0; i < saved_count; i++) if (saved[i]) mach_port_deallocate(task, saved[i]);
            vm_deallocate(task, (vm_address_t)saved, saved_count * sizeof(*saved));
        }
        if (marker) mach_port_destruct(task, marker, -1, 0);
        if (memory_entry) mach_port_deallocate(task, memory_entry);
        if (memory) mach_vm_deallocate(task, memory, vm_page_size);
    }
};

struct SafetyMachState {
    int registered_status;
    int exception_status;
    int bootstrap_status;
    int access_status;
    unsigned registered;
    unsigned exceptions;
    unsigned bootstrap;
    unsigned access;
    unsigned sent;
    int memory_map_status;
    bool memory_written;
    int self_control_status;
    int parent_name_status;
    int parent_control_status;
    int trace_error;
};
static bool safety_mach_send_right(mach_port_t port) {
    mach_port_type_t type = 0;
    return MACH_PORT_VALID(port) && mach_port_type(mach_task_self(), port, &type) == KERN_SUCCESS &&
        (type & MACH_PORT_TYPE_SEND);
}
static bool safety_mach_send_marker(mach_port_t port) {
    mach_msg_header_t message = {};
    message.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    message.msgh_size = sizeof(message); message.msgh_remote_port = port; message.msgh_id = 7;
    return mach_msg(&message, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message), 0,
        MACH_PORT_NULL, 0, MACH_PORT_NULL) == MACH_MSG_SUCCESS;
}
static int safety_mach_pid_port(bool control, pid_t pid) {
    mach_port_t port = MACH_PORT_NULL;
    int status = control ? task_for_pid(mach_task_self(), pid, &port) :
        task_name_for_pid(mach_task_self(), pid, &port);
    if (MACH_PORT_VALID(port)) mach_port_deallocate(mach_task_self(), port);
    return status;
}
static SafetyMachState safety_mach_state(bool send) {
    SafetyMachState state = {};
    state.memory_map_status = KERN_INVALID_NAME;
    task_t task = mach_task_self();
    mach_port_array_t ports = nullptr; mach_msg_type_number_t count = 0;
    state.registered_status = mach_ports_lookup(task, &ports, &count);
    if (!state.registered_status) for (unsigned i = 0; i < count; i++) if (ports[i]) {
        state.registered++;
        if (send) {
            if (i == 1) {
                mach_vm_address_t address = 0;
                state.memory_map_status = mach_vm_map(task, &address, vm_page_size, 0,
                    VM_FLAGS_ANYWHERE, ports[i], 0, false, VM_PROT_READ | VM_PROT_WRITE,
                    VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
                if (!state.memory_map_status) {
                    *(volatile unsigned char*)address = 0x57;
                    state.memory_written = true;
                    mach_vm_deallocate(task, address, vm_page_size);
                }
            } else if (safety_mach_send_marker(ports[i])) state.sent++;
        }
        mach_port_deallocate(task, ports[i]);
    }
    if (ports) vm_deallocate(task, (vm_address_t)ports, count * sizeof(*ports));
    exception_mask_t masks[EXC_TYPES_COUNT] = {};
    mach_port_t exceptions[EXC_TYPES_COUNT] = {};
    exception_behavior_t behaviors[EXC_TYPES_COUNT] = {};
    thread_state_flavor_t flavors[EXC_TYPES_COUNT] = {};
    count = EXC_TYPES_COUNT;
    state.exception_status = task_get_exception_ports(task, EXC_MASK_BREAKPOINT, masks,
        &count, exceptions, behaviors, flavors);
    if (!state.exception_status) for (unsigned i = 0; i < count; i++) if (exceptions[i]) {
        state.exceptions++;
        if (send && safety_mach_send_marker(exceptions[i])) state.sent++;
        mach_port_deallocate(task, exceptions[i]);
    }
    mach_port_t port = MACH_PORT_NULL;
    state.bootstrap_status = task_get_bootstrap_port(task, &port);
    state.bootstrap = safety_mach_send_right(port);
    if (port) mach_port_deallocate(task, port);
    port = MACH_PORT_NULL;
    state.access_status = task_get_special_port(task, TASK_ACCESS_PORT, &port);
    state.access = safety_mach_send_right(port);
    if (port) mach_port_deallocate(task, port);
    if (send) {
        state.self_control_status = safety_mach_pid_port(true, getpid());
        state.parent_name_status = safety_mach_pid_port(false, getppid());
        state.parent_control_status = safety_mach_pid_port(true, getppid());
        state.trace_error = ptrace(PT_TRACE_ME, 0, nullptr, 0) ? errno : 0;
    }
    return state;
}
static int safety_mach_report(bool confined) {
    SafetyMachState before = safety_mach_state(false);
    if (confined && !process_sandbox_enter_offline()) return 2;
    SafetyMachState after = safety_mach_state(true);
    char report[768];
    int length = snprintf(report, sizeof(report),
        "{\"before_registered\":%u,\"before_exceptions\":%u,\"before_bootstrap\":%u,\"before_access\":%u,"
        "\"registered\":%u,\"exceptions\":%u,\"bootstrap\":%u,\"access\":%u,\"sent\":%u,"
        "\"registered_status\":%d,\"exception_status\":%d,\"bootstrap_status\":%d,\"access_status\":%d,"
        "\"memory_map_status\":%d,\"memory_written\":%s,\"sandboxed\":%s,"
        "\"self_control_status\":%d,\"parent_name_status\":%d,\"parent_control_status\":%d,\"trace_errno\":%d}\n",
        before.registered, before.exceptions, before.bootstrap, before.access,
        after.registered, after.exceptions, after.bootstrap, after.access, after.sent,
        after.registered_status, after.exception_status, after.bootstrap_status, after.access_status,
        after.memory_map_status, after.memory_written ? "true" : "false",
        process_sandbox_is_applied(getpid()) ? "true" : "false",
        after.self_control_status, after.parent_name_status, after.parent_control_status, after.trace_error);
    return length > 0 && (size_t)length < sizeof(report) && write(1, report, length) == length ? 0 : 2;
}
