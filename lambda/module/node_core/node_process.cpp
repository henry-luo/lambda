#include "node_process.hpp"
#include "../../jube/jube_registry.h"

#include "../../../lib/file.h"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"

#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

#ifndef _WIN32
#include <errno.h>
#include <grp.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

static const JubeHostAPI* node_process_host = NULL;
struct NodeProcessSessionState {
    void* session;
    uint64_t uncaught_callback;
    bool uncaught_callback_rooted;
    int umask_value;
    double uptime_start_time;
    uint32_t mach_timebase_numer;
    uint32_t mach_timebase_denom;
};
static NodeProcessSessionState* node_process_state(void) {
    return (NodeProcessSessionState*)jube_node_current_module_state(
        JUBE_NODE_MODULE_STATE_PROCESS);
}
#define node_process_session (node_process_state()->session)
#define node_process_uncaught_callback (node_process_state()->uncaught_callback)
#define node_process_uncaught_callback_rooted (node_process_state()->uncaught_callback_rooted)
#define node_process_umask_value (node_process_state()->umask_value)

static bool node_process_root_frame(JubeRootFrame* frame, size_t count) {
    return node_process_host && node_process_host->node && node_process_host->node->roots &&
        node_process_host->node->roots->root_frame_begin &&
        node_process_host->node->roots->root_frame_take_slot &&
        node_process_host->node->roots->root_frame_end &&
        node_process_host->node->roots->root_frame_begin(frame, count);
}

static Item node_process_set_number_property(Item object, const char* name, double number) {
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->string_from_utf8_n || !node_process_host->value->property_set ||
            !node_process_host->script->make_number) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 1)) return ItemNull;
    uint64_t* key_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = node_process_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item result = node_process_host->value->property_set(object, (Item){.item = *key_root},
        node_process_host->script->make_number(number));
    node_process_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_process_memory_usage(void) {
    if (!node_process_host || !node_process_host->value || !node_process_host->value->new_object) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 1)) return ItemNull;
    uint64_t* result_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!result_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item result = node_process_host->value->new_object();
    *result_root = result.item;
#if defined(__APPLE__)
    struct task_basic_info info;
    mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size);
    int64_t rss = (kr == KERN_SUCCESS) ? (int64_t)info.resident_size : 0;
#elif defined(__linux__)
    int64_t rss = 0;
    FILE* file = fopen("/proc/self/statm", "r");
    if (file) {
        long pages = 0;
        if (fscanf(file, "%*ld %ld", &pages) == 1) rss = (int64_t)pages * sysconf(_SC_PAGESIZE);
        fclose(file);
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters;
    int64_t rss = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        rss = (int64_t)counters.WorkingSetSize;
    }
#else
    int64_t rss = 0;
#endif
    // The result is rooted because key construction can allocate between each
    // write; without it a forced collection can reclaim the partial object.
    result = (Item){.item = *result_root};
    node_process_set_number_property(result, "rss", (double)rss);
    node_process_set_number_property(result, "heapTotal", (double)rss);
    node_process_set_number_property(result, "heapUsed", (double)(rss / 2));
    node_process_set_number_property(result, "external", 0.0);
    node_process_set_number_property(result, "arrayBuffers", 0.0);
    result = (Item){.item = *result_root};
    node_process_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_process_cpu_usage(void) {
    if (!node_process_host || !node_process_host->value || !node_process_host->value->new_object) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 1)) return ItemNull;
    uint64_t* result_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!result_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item result = node_process_host->value->new_object();
    *result_root = result.item;
#ifndef _WIN32
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    int64_t user_us = (int64_t)usage.ru_utime.tv_sec * 1000000 + (int64_t)usage.ru_utime.tv_usec;
    int64_t system_us = (int64_t)usage.ru_stime.tv_sec * 1000000 + (int64_t)usage.ru_stime.tv_usec;
#else
    int64_t user_us = 0;
    int64_t system_us = 0;
    FILETIME create_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER user_value, kernel_value;
        user_value.LowPart = user_time.dwLowDateTime;
        user_value.HighPart = user_time.dwHighDateTime;
        kernel_value.LowPart = kernel_time.dwLowDateTime;
        kernel_value.HighPart = kernel_time.dwHighDateTime;
        user_us = (int64_t)(user_value.QuadPart / 10);
        system_us = (int64_t)(kernel_value.QuadPart / 10);
    }
#endif
    // The result remains rooted while creating both property names.
    result = (Item){.item = *result_root};
    node_process_set_number_property(result, "user", (double)user_us);
    node_process_set_number_property(result, "system", (double)system_us);
    result = (Item){.item = *result_root};
    node_process_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_process_constrained_memory(void) {
    return node_process_host->script->make_number(0.0);
}

static Item node_process_available_memory(void) {
#if defined(__APPLE__)
    vm_statistics64_data_t statistics;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&statistics, &count) == KERN_SUCCESS) {
        int64_t available = ((int64_t)statistics.free_count +
            (int64_t)statistics.inactive_count) * 4096;
        return node_process_host->script->make_number((double)available);
    }
#endif
    return node_process_host->script->make_number(0.0);
}

static Item node_process_uptime(void) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    double now = (double)timestamp.tv_sec + (double)timestamp.tv_nsec / 1e9;
    if (node_process_state()->uptime_start_time == 0.0) {
        node_process_state()->uptime_start_time = now;
    }
    return node_process_host->script->make_number(now - node_process_state()->uptime_start_time);
}

static uint64_t node_process_monotonic_nanoseconds(void) {
#ifdef __APPLE__
    NodeProcessSessionState* state = node_process_state();
    if (state->mach_timebase_denom == 0) {
        mach_timebase_info_data_t timebase = {};
        mach_timebase_info(&timebase);
        state->mach_timebase_numer = timebase.numer;
        state->mach_timebase_denom = timebase.denom;
    }
    uint64_t ticks = mach_absolute_time();
    return ticks * state->mach_timebase_numer / state->mach_timebase_denom;
#else
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000000000ULL + (uint64_t)timestamp.tv_nsec;
#endif
}

static Item node_process_hrtime_bigint(void) {
    if (!node_process_host || !node_process_host->script ||
            !node_process_host->script->bigint_from_decimal) return ItemNull;
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%llu",
        (unsigned long long)node_process_monotonic_nanoseconds());
    if (length < 0 || (size_t)length >= sizeof(buffer)) return ItemError;
    return node_process_host->script->bigint_from_decimal(buffer, (size_t)length);
}

static Item node_process_hrtime(Item previous) {
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->is_array || !node_process_host->value->array_get ||
            !node_process_host->value->array_new || !node_process_host->value->array_push ||
            !node_process_host->value->number_to_int64_exact ||
            !node_process_host->script->make_number) return ItemNull;
    double nanoseconds = (double)node_process_monotonic_nanoseconds();
    if (node_process_host->value->is_array(previous)) {
        int64_t seconds = 0;
        int64_t remainder = 0;
        Item previous_seconds = node_process_host->value->array_get(previous, 0);
        Item previous_remainder = node_process_host->value->array_get(previous, 1);
        node_process_host->value->number_to_int64_exact(previous_seconds, &seconds);
        node_process_host->value->number_to_int64_exact(previous_remainder, &remainder);
        nanoseconds -= (double)seconds * 1e9 + (double)remainder;
    }
    uint64_t total_nanoseconds = (uint64_t)nanoseconds;
    uint64_t seconds = total_nanoseconds / 1000000000ULL;
    uint64_t remainder = total_nanoseconds % 1000000000ULL;
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 1)) return ItemNull;
    uint64_t* result_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!result_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item result = node_process_host->value->array_new(0);
    *result_root = result.item;
    // The tuple remains rooted while number boxing can allocate.
    node_process_host->value->array_push((Item){.item = *result_root},
        node_process_host->script->make_number((double)seconds));
    node_process_host->value->array_push((Item){.item = *result_root},
        node_process_host->script->make_number((double)remainder));
    result = (Item){.item = *result_root};
    node_process_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_process_cwd(void) {
    if (!node_process_host || !node_process_host->value ||
            !node_process_host->value->string_from_utf8_n) return ItemNull;
    char* current_directory = file_getcwd();
    if (!current_directory) return node_process_host->value->string_from_utf8_n("", 0);
    Item result = node_process_host->value->string_from_utf8_n(current_directory,
        strlen(current_directory));
    mem_free(current_directory);
    return result;
}

static Item node_process_chdir(Item directory) {
    if (!node_process_host || !node_process_host->value ||
            !node_process_host->value->kind || !node_process_host->value->string_length ||
            !node_process_host->value->string_bytes) return ItemNull;
    if (node_process_host->value->kind(directory) != JUBE_VALUE_STRING) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    size_t length = node_process_host->value->string_length(directory);
    const uint8_t* bytes = node_process_host->value->string_bytes(directory);
    char path[2048];
    size_t copy_length = length < sizeof(path) - 1 ? length : sizeof(path) - 1;
    if (bytes && copy_length > 0) memcpy(path, bytes, copy_length);
    path[copy_length] = '\0';
#ifdef _WIN32
    int status = _chdir(path);
#else
    int status = chdir(path);
#endif
    if (status != 0) log_error("node-process: chdir failed for '%s'", path);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_throw_type_error(const char* code, const char* message) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->error ||
            !node_process_host->node->error->throw_type_error_code) return ItemNull;
    return node_process_host->node->error->throw_type_error_code(node_process_session, code, message);
}

static Item node_process_throw_range_error(const char* code, const char* message) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->error ||
            !node_process_host->node->error->throw_range_error_code) return ItemNull;
    return node_process_host->node->error->throw_range_error_code(node_process_session, code, message);
}

static Item node_process_umask(Item mask_item) {
#ifdef _WIN32
    (void)mask_item;
    return node_process_host->script->make_number(0.0);
#else
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->kind || !node_process_host->value->number_to_int64_exact ||
            !node_process_host->value->string_length || !node_process_host->value->string_bytes ||
            !node_process_host->script->make_number) return ItemNull;
    int64_t integer = 0;
    if (node_process_host->value->number_to_int64_exact(mask_item, &integer)) {
        if (integer < 0 || integer > 0777) {
            return node_process_throw_range_error("ERR_INVALID_ARG_VALUE", "The argument 'mask' is invalid");
        }
        int old_value = node_process_umask_value;
        node_process_umask_value = (int)integer;
        return node_process_host->script->make_number((double)old_value);
    }
    int value_kind = node_process_host->value->kind(mask_item);
    if (value_kind == JUBE_VALUE_STRING) {
        size_t length = node_process_host->value->string_length(mask_item);
        const uint8_t* bytes = node_process_host->value->string_bytes(mask_item);
        if (bytes && length > 0) {
            char buffer[16];
            size_t copy_length = length < 15 ? length : 15;
            memcpy(buffer, bytes, copy_length);
            buffer[copy_length] = '\0';
            char* end = NULL;
            long parsed = strtol(buffer, &end, 8);
            if (end == buffer || *end != '\0' || parsed < 0 || parsed > 0777) {
                return node_process_throw_range_error("ERR_INVALID_ARG_VALUE", "The argument 'mask' is invalid");
            }
            int old_value = node_process_umask_value;
            node_process_umask_value = (int)parsed;
            return node_process_host->script->make_number((double)old_value);
        }
    }
    if (value_kind == JUBE_VALUE_OBJECT || value_kind == JUBE_VALUE_ARRAY || value_kind == JUBE_VALUE_BOOLEAN) {
        return node_process_throw_type_error("ERR_INVALID_ARG_TYPE", "The \"mask\" argument must be of type number or string");
    }
    return node_process_host->script->make_number((double)node_process_umask_value);
#endif
}

static Item node_process_set_source_maps_enabled(Item value) {
    (void)value;
    // Source-map state is absent, so node-core preserves the observable no-op.
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_get_active_resources_info(void) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->runtime ||
            !node_process_host->node->runtime->session_active_resources_info) return ItemNull;
    return node_process_host->node->runtime->session_active_resources_info(node_process_session);
}

static Item node_process_get_active_handles(void) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->runtime ||
            !node_process_host->node->runtime->session_active_handles) return ItemNull;
    return node_process_host->node->runtime->session_active_handles(node_process_session);
}

static Item node_process_has_uncaught_exception_capture_callback(void) {
    if (!node_process_host || !node_process_host->value || !node_process_host->value->kind) return ItemNull;
    Item callback = (Item){.item = node_process_uncaught_callback};
    return (Item){.item = b2it(node_process_host->value->kind(callback) == JUBE_VALUE_FUNCTION)};
}

static Item node_process_set_uncaught_exception_capture_callback(Item callback) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->roots ||
            !node_process_host->value || !node_process_host->value->kind) return ItemNull;
    int kind = node_process_host->value->kind(callback);
    if (kind == JUBE_VALUE_FUNCTION) {
        if (!node_process_uncaught_callback_rooted) {
            if (!node_process_host->node->roots->persistent_root_register ||
                    node_process_host->node->roots->persistent_root_register(node_process_session,
                        &node_process_uncaught_callback) != 0) return ItemNull;
            node_process_uncaught_callback_rooted = true;
        }
        // The persistent slot is registered before storing callback so forced GC
        // cannot reclaim a capture handler between installation and dispatch.
        node_process_uncaught_callback = callback.item;
    } else if (kind == JUBE_VALUE_NULL) {
        node_process_uncaught_callback = ItemNull.item;
    } else {
        return node_process_throw_type_error("ERR_INVALID_ARG_TYPE",
            "The \"fn\" argument must be of type function or null.");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_abort(void) {
    abort();
    return ItemNull; // unreachable
}

#ifndef _WIN32
static Item node_process_kill(Item pid_item, Item signal_item) {
    if (!node_process_host || !node_process_host->value || !node_process_host->node ||
            !node_process_host->node->error || !node_process_host->value->kind ||
            !node_process_host->value->number_to_int64_exact ||
            !node_process_host->value->string_length || !node_process_host->value->string_bytes ||
            !node_process_host->node->error->throw_system_error) return ItemNull;
    int64_t pid_value = 0;
    node_process_host->value->number_to_int64_exact(pid_item, &pid_value);
    int signal_number = SIGTERM;
    int64_t numeric_signal = 0;
    if (node_process_host->value->number_to_int64_exact(signal_item, &numeric_signal)) {
        signal_number = (int)numeric_signal;
    } else if (node_process_host->value->kind(signal_item) == JUBE_VALUE_STRING) {
        size_t length = node_process_host->value->string_length(signal_item);
        const uint8_t* bytes = node_process_host->value->string_bytes(signal_item);
        if (bytes && length == 7 && memcmp(bytes, "SIGKILL", 7) == 0) signal_number = SIGKILL;
        else if (bytes && length == 7 && memcmp(bytes, "SIGTERM", 7) == 0) signal_number = SIGTERM;
        else if (bytes && length == 6 && memcmp(bytes, "SIGINT", 6) == 0) signal_number = SIGINT;
        // SIGHUP has six characters; matching its spelling avoids the legacy
        // length mismatch that silently converted it to SIGTERM.
        else if (bytes && length == 6 && memcmp(bytes, "SIGHUP", 6) == 0) signal_number = SIGHUP;
        else if (bytes && length == 7 && memcmp(bytes, "SIGUSR1", 7) == 0) signal_number = SIGUSR1;
        else if (bytes && length == 7 && memcmp(bytes, "SIGUSR2", 7) == 0) signal_number = SIGUSR2;
        else if (bytes && length == 7 && memcmp(bytes, "SIGCONT", 7) == 0) signal_number = SIGCONT;
        else if (bytes && length == 1 && bytes[0] == '0') signal_number = 0;
    }
    if (kill((int)pid_value, signal_number) != 0) {
        // Kernel failure shape belongs to the host so module code never builds JS Error objects.
        return node_process_host->node->error->throw_system_error(node_process_session, "kill", errno);
    }
    return (Item){.item = ITEM_TRUE};
}

static Item node_process_throw_credential_error(const char* method) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->error ||
            !node_process_host->node->error->throw_error_code) return ItemNull;
    char message[32];
    int length = snprintf(message, sizeof(message), "%s failed", method);
    if (length < 0 || (size_t)length >= sizeof(message)) return ItemNull;
    return node_process_host->node->error->throw_error_code(node_process_session,
        "ERR_UNKNOWN_CREDENTIAL", message);
}

static bool node_process_credential_id(Item value, int64_t* out_value) {
    return node_process_host && node_process_host->value &&
        node_process_host->value->number_to_int64_exact &&
        node_process_host->value->number_to_int64_exact(value, out_value);
}

static Item node_process_setuid(Item value) {
    int64_t identifier = 0;
    if (node_process_credential_id(value, &identifier) && setuid((uid_t)identifier) != 0) {
        return node_process_throw_credential_error("setuid");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_setgid(Item value) {
    int64_t identifier = 0;
    if (node_process_credential_id(value, &identifier) && setgid((gid_t)identifier) != 0) {
        return node_process_throw_credential_error("setgid");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_seteuid(Item value) {
    int64_t identifier = 0;
    if (node_process_credential_id(value, &identifier) && seteuid((uid_t)identifier) != 0) {
        return node_process_throw_credential_error("seteuid");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_setegid(Item value) {
    int64_t identifier = 0;
    if (node_process_credential_id(value, &identifier) && setegid((gid_t)identifier) != 0) {
        return node_process_throw_credential_error("setegid");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_initgroups(Item user, Item group) {
    (void)user;
    (void)group;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_process_setgroups(Item groups) {
    (void)groups;
    return (Item){.item = ITEM_JS_UNDEFINED};
}
#endif

#ifndef _WIN32
static Item node_process_getuid(void) { return node_process_host->script->make_number((double)getuid()); }
static Item node_process_getgid(void) { return node_process_host->script->make_number((double)getgid()); }
static Item node_process_geteuid(void) { return node_process_host->script->make_number((double)geteuid()); }
static Item node_process_getegid(void) { return node_process_host->script->make_number((double)getegid()); }

static Item node_process_getgroups(void) {
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->array_new || !node_process_host->value->array_set ||
            !node_process_host->script->make_number) return ItemNull;
    gid_t groups[256];
    int group_count = getgroups(256, groups);
    if (group_count < 0) group_count = 0;
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 1)) return ItemNull;
    uint64_t* groups_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!groups_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item result = node_process_host->value->array_new(group_count);
    *groups_root = result.item;
    for (int i = 0; i < group_count; i++) {
        // The result array must remain rooted because number creation may
        // allocate while populating a platform-provided group list.
        node_process_host->value->array_set((Item){.item = *groups_root}, i,
            node_process_host->script->make_number((double)groups[i]));
    }
    result = (Item){.item = *groups_root};
    node_process_host->node->roots->root_frame_end(&frame);
    return result;
}
#endif

static bool node_process_install_method(Item process, const char* name, void* function, int arity) {
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->string_from_utf8_n || !node_process_host->value->property_set ||
            !node_process_host->script->new_function) return false;
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 3)) return false;
    uint64_t* process_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!process_root || !key_root || !function_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *process_root = process.item;
    Item key = node_process_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item callback = node_process_host->script->new_function(function, arity);
    *function_root = callback.item;
    // Both key and callback allocation can move process before publication.
    node_process_host->value->property_set((Item){.item = *process_root},
        (Item){.item = *key_root}, (Item){.item = *function_root});
    node_process_host->node->roots->root_frame_end(&frame);
    return true;
}

static bool node_process_install_hrtime(Item process) {
    if (!node_process_host || !node_process_host->value || !node_process_host->script ||
            !node_process_host->value->string_from_utf8_n || !node_process_host->value->property_set ||
            !node_process_host->script->new_function) return false;
    JubeRootFrame frame = {};
    if (!node_process_root_frame(&frame, 5)) return false;
    uint64_t* process_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* hrtime_key_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* hrtime_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* bigint_key_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* bigint_root = node_process_host->node->roots->root_frame_take_slot(&frame);
    if (!process_root || !hrtime_key_root || !hrtime_root || !bigint_key_root || !bigint_root) {
        node_process_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *process_root = process.item;
    Item hrtime_key = node_process_host->value->string_from_utf8_n("hrtime", 6);
    *hrtime_key_root = hrtime_key.item;
    Item hrtime = node_process_host->script->new_function((void*)node_process_hrtime, 1);
    *hrtime_root = hrtime.item;
    Item bigint_key = node_process_host->value->string_from_utf8_n("bigint", 6);
    *bigint_key_root = bigint_key.item;
    Item bigint = node_process_host->script->new_function((void*)node_process_hrtime_bigint, 0);
    *bigint_root = bigint.item;
    // Both functions and their keys can allocate before the nested method is published.
    node_process_host->value->property_set((Item){.item = *hrtime_root},
        (Item){.item = *bigint_key_root}, (Item){.item = *bigint_root});
    node_process_host->value->property_set((Item){.item = *process_root},
        (Item){.item = *hrtime_key_root}, (Item){.item = *hrtime_root});
    node_process_host->node->roots->root_frame_end(&frame);
    return true;
}

int node_process_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->script || !host->node->runtime->session_process ||
            !host->node->runtime->session_active_resources_info ||
            !host->node->runtime->session_active_handles ||
            !host->node->roots->persistent_root_register ||
            !host->node->roots->persistent_root_unregister ||
            !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->value->is_array ||
            !host->value->array_get || !host->value->array_new || !host->value->array_push ||
            !host->script->make_number || !host->script->bigint_from_decimal ||
            !host->script->new_function || !host->node->error ||
            !host->node->error->throw_type_error_code || !host->node->error->throw_range_error_code ||
            !host->node->error->throw_system_error || !host->node->error->throw_error_code) return -1;
    node_process_host = host;
    return 0;
}

void node_process_shutdown(void) {
    node_process_host = NULL;
}

void node_process_runtime_attach(void* session) {
    if (!node_process_host || !node_process_host->node || !node_process_host->node->runtime ||
            !node_process_host->node->runtime->session_is_live ||
            !node_process_host->node->runtime->session_is_live(session)) return;
    NodeProcessSessionState* state = (NodeProcessSessionState*)
        jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_PROCESS,
            sizeof(NodeProcessSessionState));
    if (!state) return;
    if (state->umask_value == 0) state->umask_value = 0022;
    Item process = node_process_host->node->runtime->session_process(session);
    if (process.item == ItemNull.item) return;
    node_process_session = session;
    node_process_install_method(process, "memoryUsage", (void*)node_process_memory_usage, 0);
    node_process_install_method(process, "cwd", (void*)node_process_cwd, 0);
    node_process_install_method(process, "chdir", (void*)node_process_chdir, 1);
    node_process_install_method(process, "uptime", (void*)node_process_uptime, 0);
    node_process_install_hrtime(process);
    node_process_install_method(process, "cpuUsage", (void*)node_process_cpu_usage, 0);
    node_process_install_method(process, "constrainedMemory", (void*)node_process_constrained_memory, 0);
    node_process_install_method(process, "availableMemory", (void*)node_process_available_memory, 0);
    node_process_install_method(process, "umask", (void*)node_process_umask, 1);
    node_process_install_method(process, "setSourceMapsEnabled",
        (void*)node_process_set_source_maps_enabled, 1);
    node_process_install_method(process, "getActiveResourcesInfo",
        (void*)node_process_get_active_resources_info, 0);
    node_process_install_method(process, "_getActiveHandles",
        (void*)node_process_get_active_handles, 0);
    node_process_install_method(process, "hasUncaughtExceptionCaptureCallback",
        (void*)node_process_has_uncaught_exception_capture_callback, 0);
    node_process_install_method(process, "setUncaughtExceptionCaptureCallback",
        (void*)node_process_set_uncaught_exception_capture_callback, 1);
    node_process_install_method(process, "abort", (void*)node_process_abort, 0);
#ifndef _WIN32
    node_process_install_method(process, "kill", (void*)node_process_kill, 2);
    node_process_install_method(process, "setuid", (void*)node_process_setuid, 1);
    node_process_install_method(process, "setgid", (void*)node_process_setgid, 1);
    node_process_install_method(process, "seteuid", (void*)node_process_seteuid, 1);
    node_process_install_method(process, "setegid", (void*)node_process_setegid, 1);
    node_process_install_method(process, "initgroups", (void*)node_process_initgroups, 2);
    node_process_install_method(process, "setgroups", (void*)node_process_setgroups, 1);
    node_process_install_method(process, "getuid", (void*)node_process_getuid, 0);
    node_process_install_method(process, "getgid", (void*)node_process_getgid, 0);
    node_process_install_method(process, "geteuid", (void*)node_process_geteuid, 0);
    node_process_install_method(process, "getegid", (void*)node_process_getegid, 0);
    node_process_install_method(process, "getgroups", (void*)node_process_getgroups, 0);
#endif
}

void node_process_runtime_reset(void* session) {
    if (session == node_process_session) node_process_uncaught_callback = ItemNull.item;
}

void node_process_runtime_detach(void* session) {
    if (session != node_process_session) return;
    if (node_process_uncaught_callback_rooted && node_process_host && node_process_host->node &&
            node_process_host->node->roots && node_process_host->node->roots->persistent_root_unregister) {
        node_process_host->node->roots->persistent_root_unregister(session,
            &node_process_uncaught_callback);
    }
    node_process_uncaught_callback = ItemNull.item;
    node_process_uncaught_callback_rooted = false;
    node_process_session = NULL;
}
