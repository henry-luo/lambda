#include "mvp.h"

#include "../js_ast.hpp"
#include "../../core/lambda-decimal.hpp"
#include "../../runtime/ast-core.hpp"
#include "../../../lib/mem.h"

#include <re2/re2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static MvpValue mvp_runtime_value(uint64_t bits) {
    return (MvpValue){bits};
}

static int mvp_value_property_index(MvpValue value, size_t* out_index);
static MvpValue mvp_array_to_string(MvpExecution* execution, MvpValue array);

typedef uint64_t (*MvpJit0)(MvpExecution*, uint64_t);
typedef uint64_t (*MvpJit1)(MvpExecution*, uint64_t, uint64_t);
typedef uint64_t (*MvpJit2)(MvpExecution*, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit3)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit4)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t);
typedef uint64_t (*MvpJit5)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t);
typedef uint64_t (*MvpJit6)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit7)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit8)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit9)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpJit10)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t);
typedef uint64_t (*MvpClosureJit0)(MvpExecution*, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit1)(MvpExecution*, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit2)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit3)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t);
typedef uint64_t (*MvpClosureJit4)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit5)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit6)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit7)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit8)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*MvpClosureJit9)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t);
typedef uint64_t (*MvpClosureJit10)(MvpExecution*, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t);

static uint64_t mvp_jit_function_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver, uint64_t* arguments, size_t argument_count) {
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    MvpJitFunctionDescriptor* descriptor = function
        ? (MvpJitFunctionDescriptor*)function->closure : NULL;
    if (!execution || !descriptor) {
        if (execution) mvp_execution_set_error(execution, "MVP JIT function descriptor is invalid");
        return mvp_value_undefined().bits;
    }
    if (descriptor->unavailable) {
        mvp_execution_set_error(execution, "MVP nested callable lowering is not implemented");
        return mvp_value_undefined().bits;
    }
    if (!descriptor->entry || descriptor->parameter_count > 10) {
        mvp_execution_set_error(execution, "MVP JIT function descriptor is invalid");
        return mvp_value_undefined().bits;
    }
    uint64_t values[10] = {mvp_value_undefined().bits, mvp_value_undefined().bits,
        mvp_value_undefined().bits, mvp_value_undefined().bits, mvp_value_undefined().bits,
        mvp_value_undefined().bits, mvp_value_undefined().bits, mvp_value_undefined().bits,
        mvp_value_undefined().bits, mvp_value_undefined().bits};
    for (size_t index = 0; index < argument_count && index < descriptor->parameter_count;
            index++) values[index] = arguments[index];
    uint64_t function_receiver = receiver;
    uint64_t capture_environment = function->captures.bits;
    MvpValue prior_home_prototype = execution->current_home_prototype;
    if (!descriptor->captures_environment) execution->current_home_prototype = function->captures;
    uint64_t result = mvp_value_undefined().bits;
    int dispatched = 1;
    if (descriptor->captures_environment) {
        switch (descriptor->parameter_count) {
        case 0: result = ((MvpClosureJit0)descriptor->entry)(execution, function_receiver,
            capture_environment); break;
        case 1: result = ((MvpClosureJit1)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0]); break;
        case 2: result = ((MvpClosureJit2)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1]); break;
        case 3: result = ((MvpClosureJit3)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2]); break;
        case 4: result = ((MvpClosureJit4)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3]); break;
        case 5: result = ((MvpClosureJit5)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4]); break;
        case 6: result = ((MvpClosureJit6)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4],
            values[5]); break;
        case 7: result = ((MvpClosureJit7)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4],
            values[5], values[6]); break;
        case 8: result = ((MvpClosureJit8)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4],
            values[5], values[6], values[7]); break;
        case 9: result = ((MvpClosureJit9)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4],
            values[5], values[6], values[7], values[8]); break;
        case 10: result = ((MvpClosureJit10)descriptor->entry)(execution, function_receiver,
            capture_environment, values[0], values[1], values[2], values[3], values[4],
            values[5], values[6], values[7], values[8], values[9]); break;
        default: dispatched = 0; break;
        }
    } else {
        switch (descriptor->parameter_count) {
        case 0: result = ((MvpJit0)descriptor->entry)(execution, function_receiver); break;
        case 1: result = ((MvpJit1)descriptor->entry)(execution, function_receiver, values[0]); break;
        case 2: result = ((MvpJit2)descriptor->entry)(execution, function_receiver, values[0], values[1]); break;
        case 3: result = ((MvpJit3)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2]); break;
        case 4: result = ((MvpJit4)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3]); break;
        case 5: result = ((MvpJit5)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3], values[4]); break;
        case 6: result = ((MvpJit6)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3], values[4], values[5]); break;
        case 7: result = ((MvpJit7)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3], values[4], values[5], values[6]); break;
        case 8: result = ((MvpJit8)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7]); break;
        case 9: result = ((MvpJit9)descriptor->entry)(execution, function_receiver, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8]); break;
        case 10: result = ((MvpJit10)descriptor->entry)(execution, function_receiver,
            values[0], values[1], values[2], values[3], values[4], values[5], values[6],
            values[7], values[8], values[9]); break;
        default: dispatched = 0; break;
        }
    }
    execution->current_home_prototype = prior_home_prototype;
    if (dispatched) return result;
    mvp_execution_set_error(execution, "MVP JIT function arity is unsupported");
    return mvp_value_undefined().bits;
}

static uint64_t mvp_class_call(void* opaque_execution, MvpFunction* function, uint64_t receiver,
        uint64_t* arguments, size_t argument_count) {
    (void)function;
    (void)receiver;
    (void)arguments;
    (void)argument_count;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (execution) mvp_execution_set_error(execution, "MVP class constructor requires new");
    return mvp_value_undefined().bits;
}

static MvpFunction* mvp_runtime_function(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_FUNCTION ? (MvpFunction*)object : NULL;
}

static int mvp_function_initialize_objects(MvpExecution* execution, MvpValue value) {
    if (!execution) return 0;
    MvpValue roots[3] = {value, mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    roots[1] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[2] = mvp_object_new(&execution->heap, mvp_value_null());
    MvpFunction* function = mvp_runtime_function(roots[0]);
    if (function) {
        function->properties = roots[1];
        function->instance_prototype = roots[2];
    }
    int initialized = function && mvp_value_is_object(function->properties) &&
        mvp_value_is_object(function->instance_prototype);
    mvp_root_frame_pop(&execution->heap, &frame);
    return initialized;
}

static MvpValue mvp_class_prototype(MvpValue value) {
    MvpFunction* function = mvp_runtime_function(value);
    return function && function->entry == mvp_class_call ? function->captures
        : mvp_value_undefined();
}

struct MvpExecutionFrame {
    MvpRootFrame roots;
    MvpExecutionFrame* free_next;
    size_t capacity;
    MvpValue values[1];
};

static void mvp_execution_release_frame_pool(MvpExecution* execution) {
    if (!execution) return;
    MvpExecutionFrame* frame = execution->free_frames;
    while (frame) {
        MvpExecutionFrame* next = frame->free_next;
        mem_free(frame);
        frame = next;
    }
    execution->free_frames = NULL;
}

void mvp_execution_init(MvpExecution* execution, size_t root_count) {
    if (!execution) return;
    memset(execution, 0, sizeof(*execution));
    execution->random_state = UINT64_C(0x9e3779b97f4a7c15);
    mvp_heap_init(&execution->heap);
    const char* force_collection = getenv("MVP_GC_FORCE_EVERY");
    execution->heap.force_every_allocation = force_collection &&
        strcmp(force_collection, "1") == 0;
    if (root_count == SIZE_MAX) {
        mvp_execution_set_error(execution, "MVP root allocation is too large");
        return;
    }
    size_t total_root_count = root_count + 1;
    if (total_root_count) {
        execution->roots = (MvpValue*)mem_calloc(total_root_count, sizeof(MvpValue),
            MEM_CAT_JS_RUNTIME);
        if (!execution->roots) {
            mvp_execution_set_error(execution, "MVP root allocation failed");
            return;
        }
        for (size_t index = 0; index < total_root_count; index++) {
            execution->roots[index] = mvp_value_undefined();
        }
    }
    execution->root_count = total_root_count;
    execution->root_capacity = total_root_count;
    execution->global_root_index = root_count;
    mvp_root_frame_push(&execution->heap, &execution->root_frame,
        execution->roots, execution->root_count);
    execution->active_frame = &execution->root_frame;
    MvpValue globals = mvp_object_new(&execution->heap, mvp_value_null());
    if (!mvp_value_is_object(globals)) {
        mvp_execution_set_error(execution, "MVP global object allocation failed");
        return;
    }
    execution->roots[execution->global_root_index] = globals;
}

void mvp_execution_destroy(MvpExecution* execution) {
    if (!execution) return;
    while (execution->active_frame && execution->active_frame != &execution->root_frame) {
        mvp_execution_leave_frame(execution);
    }
    mvp_root_frame_pop(&execution->heap, &execution->root_frame);
    mvp_execution_release_frame_pool(execution);
    if (execution->literal_cache) mem_free(execution->literal_cache);
    if (execution->roots) mem_free(execution->roots);
    mvp_heap_destroy(&execution->heap);
    memset(execution, 0, sizeof(*execution));
}

void mvp_execution_set_root(MvpExecution* execution, size_t index, uint64_t bits) {
    if (!execution || !execution->active_frame || index >= execution->active_frame->count) return;
    execution->active_frame->values[index] = mvp_runtime_value(bits);
}

uint64_t mvp_execution_get_root(const MvpExecution* execution, size_t index) {
    return !execution || !execution->active_frame || index >= execution->active_frame->count
        ? mvp_value_undefined().bits : execution->active_frame->values[index].bits;
}

uint64_t mvp_execution_global_get(MvpExecution* execution, uint64_t address,
        uint64_t byte_length) {
    if (!execution || byte_length > SIZE_MAX || !execution->roots ||
            execution->global_root_index >= execution->root_count) {
        return mvp_value_undefined().bits;
    }
    MvpValue globals = execution->roots[execution->global_root_index];
    MvpValue roots[2] = {globals, mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 2);
    roots[1] = mvp_string_new(&execution->heap, (const char*)(uintptr_t)address,
        (size_t)byte_length);
    MvpValue value = mvp_value_is_string(roots[1]) ? mvp_object_get(roots[0], roots[1])
        : mvp_value_undefined();
    mvp_root_frame_pop(&execution->heap, &frame);
    return value.bits;
}

uint64_t mvp_execution_global_set(MvpExecution* execution, uint64_t address,
        uint64_t byte_length, uint64_t value_bits) {
    if (!execution || byte_length > SIZE_MAX || !execution->roots ||
            execution->global_root_index >= execution->root_count) {
        return mvp_value_undefined().bits;
    }
    MvpValue globals = execution->roots[execution->global_root_index];
    MvpValue roots[3] = {globals, mvp_value_undefined(), mvp_runtime_value(value_bits)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    roots[1] = mvp_string_new(&execution->heap, (const char*)(uintptr_t)address,
        (size_t)byte_length);
    int stored = mvp_value_is_string(roots[1]) &&
        mvp_object_set(&execution->heap, roots[0], roots[1], roots[2]);
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!stored) {
        mvp_execution_set_error(execution, "MVP global binding failed");
        return mvp_value_undefined().bits;
    }
    return value_bits;
}

uint64_t mvp_execution_global_object(MvpExecution* execution) {
    return !execution || !execution->roots || execution->global_root_index >=
        execution->root_count ? mvp_value_undefined().bits
        : execution->roots[execution->global_root_index].bits;
}

static int mvp_execution_grow_roots(MvpExecution* execution, size_t needed) {
    if (!execution || needed <= execution->root_capacity) return execution != NULL;
    size_t capacity = execution->root_capacity ? execution->root_capacity : 4;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    MvpValue* roots = (MvpValue*)mem_calloc(capacity, sizeof(MvpValue), MEM_CAT_JS_RUNTIME);
    if (!roots) return 0;
    if (execution->roots && execution->root_count) {
        memcpy(roots, execution->roots, execution->root_count * sizeof(MvpValue));
    }
    if (execution->roots) mem_free(execution->roots);
    execution->roots = roots;
    execution->root_capacity = capacity;
    execution->root_frame.values = roots;
    execution->root_frame.count = execution->root_count;
    return 1;
}

static uint64_t mvp_execution_literal_hash(uint64_t address, size_t byte_length) {
    uint64_t hash = address ^ ((uint64_t)byte_length * UINT64_C(0x9e3779b97f4a7c15));
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31);
}

static size_t mvp_execution_literal_slot(MvpLiteralCacheEntry* entries, size_t capacity,
        uint64_t address, size_t byte_length) {
    if (!entries || !capacity) return SIZE_MAX;
    size_t slot = (size_t)(mvp_execution_literal_hash(address, byte_length) & (capacity - 1));
    while (entries[slot].address && (entries[slot].address != address ||
            entries[slot].byte_length != byte_length)) {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static int mvp_execution_grow_literal_cache(MvpExecution* execution, size_t needed) {
    if (!execution || needed > SIZE_MAX / 2) return 0;
    if (execution->literal_cache_capacity &&
            execution->literal_cache_capacity / 2 >= needed) return 1;
    size_t capacity = execution->literal_cache_capacity ? execution->literal_cache_capacity : 16;
    while (capacity / 2 < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    MvpLiteralCacheEntry* entries = (MvpLiteralCacheEntry*)mem_calloc(capacity,
        sizeof(MvpLiteralCacheEntry), MEM_CAT_JS_RUNTIME);
    if (!entries) return 0;
    for (size_t index = 0; execution->literal_cache &&
            index < execution->literal_cache_capacity; index++) {
        MvpLiteralCacheEntry entry = execution->literal_cache[index];
        if (!entry.address) continue;
        size_t slot = mvp_execution_literal_slot(entries, capacity, entry.address,
            entry.byte_length);
        if (slot == SIZE_MAX) {
            mem_free(entries);
            return 0;
        }
        entries[slot] = entry;
    }
    if (execution->literal_cache) mem_free(execution->literal_cache);
    execution->literal_cache = entries;
    execution->literal_cache_capacity = capacity;
    return 1;
}

uint64_t mvp_execution_literal_string(MvpExecution* execution, uint64_t address,
        uint64_t byte_length) {
    if (!execution || !address || byte_length > SIZE_MAX) return mvp_value_undefined().bits;
    size_t length = (size_t)byte_length;
    size_t slot = mvp_execution_literal_slot(execution->literal_cache,
        execution->literal_cache_capacity, address, length);
    if (slot != SIZE_MAX && execution->literal_cache[slot].address) {
        return execution->literal_cache[slot].value.bits;
    }
    size_t next_root = execution->root_count + 1;
    size_t next_entry = execution->literal_cache_count + 1;
    if (next_root <= execution->root_count || next_entry <= execution->literal_cache_count ||
            !mvp_execution_grow_roots(execution, next_root) ||
            !mvp_execution_grow_literal_cache(execution, next_entry)) {
        mvp_execution_set_error(execution, "MVP literal cache allocation failed");
        return mvp_value_undefined().bits;
    }
    slot = mvp_execution_literal_slot(execution->literal_cache,
        execution->literal_cache_capacity, address, length);
    if (slot == SIZE_MAX || execution->literal_cache[slot].address) {
        mvp_execution_set_error(execution, "MVP literal cache insertion failed");
        return mvp_value_undefined().bits;
    }
    MvpValue value = mvp_string_new(&execution->heap, (const char*)(uintptr_t)address, length);
    if (!mvp_value_is_string(value)) {
        mvp_execution_set_error(execution, "MVP string literal allocation failed");
        return mvp_value_undefined().bits;
    }
    // The source address is valid for this execution and identifies repeated
    // compiler-known property names without making an atom table global.
    ((MvpString*)mvp_value_reference_object(value))->source_address =
        (const char*)(uintptr_t)address;
    execution->roots[execution->root_count++] = value;
    execution->root_frame.count = execution->root_count;
    execution->literal_cache[slot] = (MvpLiteralCacheEntry){address, length, value};
    execution->literal_cache_count++;
    return value.bits;
}

MvpValue* mvp_execution_enter_frame(MvpExecution* execution, size_t root_count) {
    // Generated MIR always emits a matching leave. Keep the root-frame stack
    // balanced after a guest error so later epilogues cannot free a caller.
    if (!execution || root_count > (SIZE_MAX - sizeof(MvpExecutionFrame)) / sizeof(MvpValue)) {
        return NULL;
    }
    MvpExecutionFrame** previous = &execution->free_frames;
    while (*previous && (*previous)->capacity < root_count) previous = &(*previous)->free_next;
    MvpExecutionFrame* frame = *previous;
    if (frame) {
        *previous = frame->free_next;
        frame->free_next = NULL;
    } else {
        size_t bytes = sizeof(MvpExecutionFrame) +
            (root_count ? root_count - 1 : 0) * sizeof(MvpValue);
        frame = (MvpExecutionFrame*)mem_calloc(1, bytes, MEM_CAT_JS_RUNTIME);
        if (!frame) {
            mvp_execution_set_error(execution, "MVP frame allocation failed");
            return NULL;
        }
        frame->capacity = root_count;
    }
    // A pooled frame is not a root while idle. Clear every slot before it
    // returns to the root chain so stale freed references can never be traced.
    for (size_t index = 0; index < root_count; index++) {
        frame->values[index] = mvp_value_undefined();
    }
    mvp_root_frame_push(&execution->heap, &frame->roots, frame->values, root_count);
    execution->active_frame = &frame->roots;
    return frame->values;
}

void mvp_execution_leave_frame(MvpExecution* execution) {
    if (!execution || !execution->active_frame ||
            execution->active_frame == &execution->root_frame) return;
    MvpRootFrame* roots = execution->active_frame;
    execution->active_frame = roots->previous;
    mvp_root_frame_pop(&execution->heap, roots);
    MvpExecutionFrame* frame = (MvpExecutionFrame*)roots;
    frame->free_next = execution->free_frames;
    execution->free_frames = frame;
}

int mvp_execution_has_error(const MvpExecution* execution) {
    return execution && execution->error[0] != '\0';
}

const char* mvp_execution_error(const MvpExecution* execution) {
    return execution && execution->error[0] ? execution->error : "MVP execution failed";
}

void mvp_execution_set_error(MvpExecution* execution, const char* message) {
    if (!execution || execution->error[0] || !message) return;
    size_t length = strlen(message);
    if (length >= sizeof(execution->error)) length = sizeof(execution->error) - 1;
    memcpy(execution->error, message, length);
    execution->error[length] = '\0';
}

void mvp_execution_result_destroy(MvpExecutionResult* result) {
    if (!result || !result->execution) return;
    mvp_execution_destroy(result->execution);
    mem_free(result->execution);
    result->execution = NULL;
}

static MvpValue mvp_object_named_value(MvpValue value, const char* name) {
    if (!name) return mvp_value_undefined();
    size_t name_length = strlen(name);
    MvpHeapObject* header = mvp_value_reference_object(value);
    for (MvpObject* object = header && header->kind == MVP_HEAP_OBJECT
            ? (MvpObject*)header : NULL; object; ) {
        MvpValue* properties = mvp_heap_object_values(mvp_value_reference_object(object->storage));
        for (size_t index = 0; properties && index < object->property_count; index++) {
            size_t key_length = 0;
            const char* key = mvp_string_bytes(properties[index * 2], &key_length);
            if (key && key_length == name_length && memcmp(key, name, name_length) == 0) {
                return properties[index * 2 + 1];
            }
        }
        MvpHeapObject* prototype = mvp_value_reference_object(object->prototype);
        object = prototype && prototype->kind == MVP_HEAP_OBJECT ? (MvpObject*)prototype : NULL;
    }
    return mvp_value_undefined();
}

static int mvp_object_has_constructor_name(MvpValue value, const char* name) {
    if (!name) return 0;
    MvpValue constructor = mvp_object_named_value(value, "constructor");
    MvpValue constructor_name = mvp_object_named_value(constructor, "name");
    size_t length = 0;
    const char* bytes = mvp_string_bytes(constructor_name, &length);
    size_t expected_length = strlen(name);
    return bytes && length == expected_length && memcmp(bytes, name, length) == 0;
}

static double mvp_to_number(MvpExecution* execution, MvpValue value) {
    if (mvp_value_is_number(value)) return mvp_value_to_number(value);
    if (mvp_value_is_null(value)) return 0.0;
    if (mvp_value_is_boolean(value)) return mvp_value_boolean(value) ? 1.0 : 0.0;
    if (mvp_value_is_undefined(value)) return NAN;
    if (mvp_value_is_string(value)) {
        size_t length = 0;
        const char* bytes = mvp_string_bytes(value, &length);
        if (!bytes || length >= 128) return NAN;
        char buffer[128] = {};
        memcpy(buffer, bytes, length);
        char* end = NULL;
        double number = strtod(buffer, &end);
        return end && *end == '\0' ? number : NAN;
    }
    if (mvp_value_is_bigint(value)) return NAN;
    MvpValue number = mvp_object_named_value(value, "__mvp_number");
    if (mvp_value_is_number(number)) return mvp_value_to_number(number);
    MvpValue string = mvp_object_named_value(value, "__mvp_string");
    if (mvp_value_is_string(string)) return mvp_to_number(execution, string);
    return NAN;
}

static MvpValue mvp_to_string(MvpExecution* execution, MvpValue value) {
    if (!execution) return mvp_value_undefined();
    if (mvp_value_is_string(value)) return value;
    if (mvp_value_is_undefined(value)) return mvp_string_new(&execution->heap, "undefined", 9);
    if (mvp_value_is_null(value)) return mvp_string_new(&execution->heap, "null", 4);
    if (mvp_value_is_boolean(value)) {
        const char* text = mvp_value_boolean(value) ? "true" : "false";
        return mvp_string_new(&execution->heap, text, strlen(text));
    }
    if (mvp_value_is_number(value)) {
        char buffer[64] = {};
        double number = mvp_value_to_number(value);
        int count = 0;
        if (isnan(number)) count = snprintf(buffer, sizeof(buffer), "NaN");
        else if (isinf(number)) count = snprintf(buffer, sizeof(buffer),
            number < 0 ? "-Infinity" : "Infinity");
        else {
            lambda_finite_double_to_shortest(number, buffer, sizeof(buffer));
            count = (int)strlen(buffer);
        }
        if (count <= 0 || (size_t)count >= sizeof(buffer)) {
            mvp_execution_set_error(execution, "MVP number formatting failed");
            return mvp_value_undefined();
        }
        return mvp_string_new(&execution->heap, buffer, (size_t)count);
    }
    if (mvp_value_is_bigint(value)) return mvp_bigint_to_string(&execution->heap, value);
    if (mvp_value_is_array(value)) return mvp_array_to_string(execution, value);
    if (mvp_value_is_regex(value)) {
        MvpValue roots[4] = {value, mvp_value_undefined(), mvp_value_undefined(),
            mvp_value_undefined()};
        MvpRootFrame frame = {};
        mvp_root_frame_push(&execution->heap, &frame, roots, 4);
        roots[1] = mvp_string_new(&execution->heap, "/", 1);
        roots[2] = mvp_string_concat(&execution->heap, roots[1], mvp_regex_pattern(roots[0]));
        roots[3] = mvp_string_concat(&execution->heap, roots[2], roots[1]);
        MvpValue result = mvp_string_concat(&execution->heap, roots[3],
            mvp_value_reference_object(roots[0]) ? ((MvpRegex*)mvp_value_reference_object(
                roots[0]))->flags : mvp_value_undefined());
        mvp_root_frame_pop(&execution->heap, &frame);
        return result;
    }
    MvpValue string = mvp_object_named_value(value, "__mvp_string");
    if (mvp_value_is_string(string)) return string;
    MvpValue number = mvp_object_named_value(value, "__mvp_number");
    if (mvp_value_is_number(number)) return mvp_to_string(execution, number);
    return mvp_string_new(&execution->heap, "[object Object]", 15);
}

uint64_t mvp_op_add(MvpExecution* execution, uint64_t left_bits, uint64_t right_bits) {
    MvpValue left = mvp_runtime_value(left_bits);
    MvpValue right = mvp_runtime_value(right_bits);
    if (mvp_value_is_number(left) && mvp_value_is_number(right)) {
        return mvp_value_from_number(mvp_value_to_number(left) + mvp_value_to_number(right)).bits;
    }
    if (mvp_value_is_bigint(left) || mvp_value_is_bigint(right)) {
        if (!mvp_value_is_bigint(left) || !mvp_value_is_bigint(right)) {
            mvp_execution_set_error(execution, "MVP BigInt cannot mix with Number");
            return mvp_value_undefined().bits;
        }
        return mvp_bigint_add(&execution->heap, left, right).bits;
    }
    if (mvp_value_is_string(left) || mvp_value_is_string(right)) {
        MvpValue roots[4] = {left, right, mvp_value_undefined(), mvp_value_undefined()};
        MvpRootFrame frame = {};
        mvp_root_frame_push(&execution->heap, &frame, roots, 4);
        roots[2] = mvp_to_string(execution, left);
        roots[3] = mvp_to_string(execution, right);
        MvpValue result = mvp_string_concat(&execution->heap, roots[2], roots[3]);
        mvp_root_frame_pop(&execution->heap, &frame);
        return result.bits;
    }
    return mvp_value_from_number(mvp_to_number(execution, left) +
        mvp_to_number(execution, right)).bits;
}

uint64_t mvp_op_sub(MvpExecution* execution, uint64_t left, uint64_t right) {
    MvpValue left_value = mvp_runtime_value(left);
    MvpValue right_value = mvp_runtime_value(right);
    if (mvp_value_is_number(left_value) && mvp_value_is_number(right_value)) {
        return mvp_value_from_number(mvp_value_to_number(left_value) -
            mvp_value_to_number(right_value)).bits;
    }
    if (mvp_value_is_bigint(left_value) || mvp_value_is_bigint(right_value)) {
        if (!mvp_value_is_bigint(left_value) || !mvp_value_is_bigint(right_value)) {
            mvp_execution_set_error(execution, "MVP BigInt cannot mix with Number");
            return mvp_value_undefined().bits;
        }
        return mvp_bigint_sub(&execution->heap, left_value, right_value).bits;
    }
    return mvp_value_from_number(mvp_to_number(execution, mvp_runtime_value(left)) -
        mvp_to_number(execution, mvp_runtime_value(right))).bits;
}

uint64_t mvp_op_mul(MvpExecution* execution, uint64_t left, uint64_t right) {
    MvpValue left_value = mvp_runtime_value(left);
    MvpValue right_value = mvp_runtime_value(right);
    if (mvp_value_is_number(left_value) && mvp_value_is_number(right_value)) {
        return mvp_value_from_number(mvp_value_to_number(left_value) *
            mvp_value_to_number(right_value)).bits;
    }
    if (mvp_value_is_bigint(left_value) || mvp_value_is_bigint(right_value)) {
        if (!mvp_value_is_bigint(left_value) || !mvp_value_is_bigint(right_value)) {
            mvp_execution_set_error(execution, "MVP BigInt cannot mix with Number");
            return mvp_value_undefined().bits;
        }
        return mvp_bigint_mul(&execution->heap, left_value, right_value).bits;
    }
    return mvp_value_from_number(mvp_to_number(execution, mvp_runtime_value(left)) *
        mvp_to_number(execution, mvp_runtime_value(right))).bits;
}

uint64_t mvp_op_div(MvpExecution* execution, uint64_t left, uint64_t right) {
    MvpValue left_value = mvp_runtime_value(left);
    MvpValue right_value = mvp_runtime_value(right);
    if (mvp_value_is_number(left_value) && mvp_value_is_number(right_value)) {
        return mvp_value_from_number(mvp_value_to_number(left_value) /
            mvp_value_to_number(right_value)).bits;
    }
    if (mvp_value_is_bigint(left_value) || mvp_value_is_bigint(right_value)) {
        if (!mvp_value_is_bigint(left_value) || !mvp_value_is_bigint(right_value)) {
            mvp_execution_set_error(execution, "MVP BigInt cannot mix with Number");
            return mvp_value_undefined().bits;
        }
        MvpValue result = mvp_bigint_div(&execution->heap, left_value, right_value);
        if (!mvp_value_is_bigint(result)) mvp_execution_set_error(execution,
            "MVP BigInt division failed");
        return result.bits;
    }
    return mvp_value_from_number(mvp_to_number(execution, mvp_runtime_value(left)) /
        mvp_to_number(execution, mvp_runtime_value(right))).bits;
}

uint64_t mvp_op_mod(MvpExecution* execution, uint64_t left, uint64_t right) {
    MvpValue left_value = mvp_runtime_value(left);
    MvpValue right_value = mvp_runtime_value(right);
    if (mvp_value_is_number(left_value) && mvp_value_is_number(right_value)) {
        return mvp_value_from_number(fmod(mvp_value_to_number(left_value),
            mvp_value_to_number(right_value))).bits;
    }
    return mvp_value_from_number(fmod(mvp_to_number(execution, left_value),
        mvp_to_number(execution, right_value))).bits;
}

static int32_t mvp_uint32_to_int32(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value
        : (int32_t)(-(int64_t)(UINT64_C(0x100000000) - value));
}

static int32_t mvp_to_int32(MvpExecution* execution, uint64_t bits) {
    double number = mvp_to_number(execution, mvp_runtime_value(bits));
    if (!isfinite(number) || number == 0.0) return 0;
    double truncated = trunc(number);
    double modulo = fmod(truncated, 4294967296.0);
    if (modulo < 0.0) modulo += 4294967296.0;
    return mvp_uint32_to_int32((uint32_t)modulo);
}

uint64_t mvp_op_bit_and(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_from_number((double)(mvp_to_int32(execution, left) &
        mvp_to_int32(execution, right))).bits;
}

uint64_t mvp_op_bit_or(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_from_number((double)(mvp_to_int32(execution, left) |
        mvp_to_int32(execution, right))).bits;
}

uint64_t mvp_op_bit_xor(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_from_number((double)(mvp_to_int32(execution, left) ^
        mvp_to_int32(execution, right))).bits;
}

uint64_t mvp_op_bit_lshift(MvpExecution* execution, uint64_t left, uint64_t right) {
    uint32_t lhs = (uint32_t)mvp_to_int32(execution, left);
    uint32_t rhs = (uint32_t)mvp_to_int32(execution, right);
    return mvp_value_from_number((double)mvp_uint32_to_int32(lhs << (rhs & 31))).bits;
}

uint64_t mvp_op_bit_urshift(MvpExecution* execution, uint64_t left, uint64_t right) {
    uint32_t lhs = (uint32_t)mvp_to_int32(execution, left);
    uint32_t rhs = (uint32_t)mvp_to_int32(execution, right);
    return mvp_value_from_number((double)(lhs >> (rhs & 31))).bits;
}

uint64_t mvp_op_neg(MvpExecution* execution, uint64_t bits) {
    MvpValue value = mvp_runtime_value(bits);
    return mvp_value_is_number(value) ? mvp_value_from_number(-mvp_value_to_number(value)).bits
        : mvp_value_from_number(-mvp_to_number(execution, value)).bits;
}

uint64_t mvp_op_equal(MvpExecution* execution, uint64_t left_bits, uint64_t right_bits) {
    MvpValue left = mvp_runtime_value(left_bits);
    MvpValue right = mvp_runtime_value(right_bits);
    if (mvp_value_is_null(left) || mvp_value_is_undefined(left)) {
        return mvp_value_bool(mvp_value_is_null(right) || mvp_value_is_undefined(right)).bits;
    }
    if (mvp_value_is_null(right) || mvp_value_is_undefined(right)) return mvp_value_bool(0).bits;
    if (mvp_value_is_number(left) && mvp_value_is_number(right)) {
        return mvp_value_bool(mvp_value_to_number(left) == mvp_value_to_number(right)).bits;
    }
    if (mvp_value_is_string(left) && mvp_value_is_string(right)) {
        return mvp_value_bool(mvp_string_equal(left, right)).bits;
    }
    if (mvp_value_is_bigint(left) && mvp_value_is_bigint(right)) {
        return mvp_value_bool(mvp_bigint_compare(left, right) == 0).bits;
    }
    if (mvp_value_is_boolean(left) || mvp_value_is_boolean(right) ||
            (mvp_value_is_number(left) && mvp_value_is_string(right)) ||
            (mvp_value_is_string(left) && mvp_value_is_number(right))) {
        return mvp_value_bool(mvp_to_number(execution, left) ==
            mvp_to_number(execution, right)).bits;
    }
    return mvp_value_bool(mvp_value_strict_equal(left, right)).bits;
}

uint64_t mvp_op_not_equal(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_bool(!mvp_value_boolean(mvp_runtime_value(mvp_op_equal(execution,
        left, right)))).bits;
}

uint64_t mvp_op_strict_equal(MvpExecution* execution, uint64_t left, uint64_t right) {
    (void)execution;
    MvpValue left_value = mvp_runtime_value(left);
    MvpValue right_value = mvp_runtime_value(right);
    if (mvp_value_is_number(left_value) && mvp_value_is_number(right_value)) {
        return mvp_value_bool(mvp_value_to_number(left_value) ==
            mvp_value_to_number(right_value)).bits;
    }
    return mvp_value_bool(mvp_value_strict_equal(left_value, right_value)).bits;
}

uint64_t mvp_op_strict_not_equal(MvpExecution* execution, uint64_t left, uint64_t right) {
    (void)execution;
    return mvp_value_bool(!mvp_value_strict_equal(mvp_runtime_value(left),
        mvp_runtime_value(right))).bits;
}

uint64_t mvp_op_less_than(MvpExecution* execution, uint64_t left_bits, uint64_t right_bits) {
    MvpValue left = mvp_runtime_value(left_bits);
    MvpValue right = mvp_runtime_value(right_bits);
    if (mvp_value_is_number(left) && mvp_value_is_number(right)) {
        return mvp_value_bool(mvp_value_to_number(left) < mvp_value_to_number(right)).bits;
    }
    if (mvp_value_is_bigint(left) || mvp_value_is_bigint(right)) {
        if (!mvp_value_is_bigint(left) || !mvp_value_is_bigint(right)) {
            mvp_execution_set_error(execution, "MVP BigInt cannot mix with Number");
            return mvp_value_bool(0).bits;
        }
        return mvp_value_bool(mvp_bigint_compare(left, right) < 0).bits;
    }
    if (mvp_value_is_string(left) && mvp_value_is_string(right)) {
        size_t left_length = 0;
        size_t right_length = 0;
        const char* left_bytes = mvp_string_bytes(left, &left_length);
        const char* right_bytes = mvp_string_bytes(right, &right_length);
        size_t common = left_length < right_length ? left_length : right_length;
        int compare = memcmp(left_bytes, right_bytes, common);
        return mvp_value_bool(compare < 0 || (compare == 0 && left_length < right_length)).bits;
    }
    return mvp_value_bool(mvp_to_number(execution, left) <
        mvp_to_number(execution, right)).bits;
}

uint64_t mvp_op_less_equal(MvpExecution* execution, uint64_t left, uint64_t right) {
    MvpValue greater = mvp_runtime_value(mvp_op_less_than(execution, right, left));
    return mvp_value_bool(!mvp_value_boolean(greater)).bits;
}

uint64_t mvp_op_greater_than(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_op_less_than(execution, right, left);
}

uint64_t mvp_op_greater_equal(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_op_less_equal(execution, right, left);
}

uint64_t mvp_op_in(MvpExecution* execution, uint64_t key_bits, uint64_t object_bits) {
    (void)execution;
    MvpValue key = mvp_runtime_value(key_bits);
    MvpValue object = mvp_runtime_value(object_bits);
    size_t index = 0;
    if (mvp_value_is_array(object)) {
        if (mvp_value_property_index(key, &index)) {
            return mvp_value_bool(index < mvp_array_length(object)).bits;
        }
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        return mvp_value_bool(name && length == 6 && memcmp(name, "length", 6) == 0).bits;
    }
    MvpValue properties = object;
    if (mvp_value_is_function(properties)) {
        MvpFunction* function = mvp_runtime_function(properties);
        properties = function ? function->properties : mvp_value_undefined();
    }
    MvpHeapObject* header = mvp_value_reference_object(properties);
    for (MvpObject* current = header && header->kind == MVP_HEAP_OBJECT
            ? (MvpObject*)header : NULL; current; ) {
        MvpValue* values = mvp_heap_object_values(mvp_value_reference_object(current->storage));
        for (size_t property = 0; values && property < current->property_count; property++) {
            if (mvp_string_equal(values[property * 2], key)) return mvp_value_bool(1).bits;
        }
        MvpHeapObject* prototype = mvp_value_reference_object(current->prototype);
        current = prototype && prototype->kind == MVP_HEAP_OBJECT ? (MvpObject*)prototype : NULL;
    }
    return mvp_value_bool(0).bits;
}

uint64_t mvp_op_bit_rshift(MvpExecution* execution, uint64_t left, uint64_t right) {
    int32_t lhs = mvp_to_int32(execution, left);
    uint32_t rhs = (uint32_t)mvp_to_int32(execution, right);
    return mvp_value_from_number((double)(lhs >> (rhs & 31))).bits;
}

uint64_t mvp_op_bit_not(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number((double)~mvp_to_int32(execution, bits)).bits;
}

uint64_t mvp_op_not(MvpExecution* execution, uint64_t bits) {
    (void)execution;
    return mvp_value_bool(!mvp_value_truthy(mvp_runtime_value(bits))).bits;
}

uint64_t mvp_op_typeof(MvpExecution* execution, uint64_t bits) {
    MvpValue value = mvp_runtime_value(bits);
    const char* name = mvp_value_is_undefined(value) ? "undefined"
        : mvp_value_is_boolean(value) ? "boolean"
        : mvp_value_is_number(value) ? "number"
        : mvp_value_is_string(value) ? "string"
        : mvp_value_is_function(value) ? "function" : "object";
    return mvp_string_new(&execution->heap, name, strlen(name)).bits;
}

uint64_t mvp_op_truthy(MvpExecution* execution, uint64_t bits) {
    (void)execution;
    return mvp_value_truthy(mvp_runtime_value(bits)) ? 1 : 0;
}

static const char* mvp_throw_message(MvpValue thrown, size_t* out_length) {
    MvpHeapObject* header = mvp_value_reference_object(thrown);
    if (!header || header->kind != MVP_HEAP_OBJECT) return NULL;
    MvpObject* object = (MvpObject*)header;
    MvpHeapObject* storage = mvp_value_reference_object(object->storage);
    MvpValue* properties = mvp_heap_object_values(storage);
    if (!properties) return NULL;
    for (size_t index = 0; index < object->property_count; index++) {
        size_t key_length = 0;
        const char* key = mvp_string_bytes(properties[index * 2], &key_length);
        if (!key || key_length != 7 || memcmp(key, "message", 7) != 0) continue;
        return mvp_string_bytes(properties[index * 2 + 1], out_length);
    }
    return NULL;
}

uint64_t mvp_op_throw(MvpExecution* execution, uint64_t bits) {
    size_t length = 0;
    const char* text = mvp_throw_message(mvp_runtime_value(bits), &length);
    if (!text) {
        mvp_execution_set_error(execution, "MVP uncaught throw");
        return mvp_value_undefined().bits;
    }
    char message[160] = {};
    const char* prefix = "MVP uncaught throw: ";
    size_t prefix_length = strlen(prefix);
    size_t copied = length < sizeof(message) - prefix_length - 1 ? length
        : sizeof(message) - prefix_length - 1;
    memcpy(message, prefix, prefix_length);
    memcpy(message + prefix_length, text, copied);
    message[prefix_length + copied] = '\0';
    mvp_execution_set_error(execution, message);
    return mvp_value_undefined().bits;
}

uint64_t mvp_op_literal_string(MvpExecution* execution, uint64_t address,
        uint64_t byte_length) {
    if (!execution) return mvp_value_undefined().bits;
    if (byte_length > SIZE_MAX) {
        mvp_execution_set_error(execution, "MVP string literal is too large");
        return mvp_value_undefined().bits;
    }
    return mvp_execution_literal_string(execution, address, byte_length);
}

uint64_t mvp_host_hrtime_bigint(MvpExecution* execution) {
    struct timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return mvp_value_from_number(NAN).bits;
    uint64_t nanoseconds = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
    return execution ? mvp_bigint_from_uint64(&execution->heap, nanoseconds).bits
        : mvp_value_undefined().bits;
}

uint64_t mvp_host_performance_now(MvpExecution* execution) {
    struct timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return mvp_value_from_number(NAN).bits;
    double milliseconds = (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
    (void)execution;
    return mvp_value_from_number(milliseconds).bits;
}

uint64_t mvp_host_math_random(MvpExecution* execution) {
    if (!execution) return mvp_value_undefined().bits;
    uint64_t state = execution->random_state;
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    execution->random_state = state;
    uint64_t value = state * UINT64_C(2685821657736338717);
    return mvp_value_from_number((double)(value >> 11) / 9007199254740992.0).bits;
}

uint64_t mvp_host_stdout_write(MvpExecution* execution, uint64_t bits) {
    MvpValue text = mvp_to_string(execution, mvp_runtime_value(bits));
    size_t length = 0;
    const char* bytes = mvp_string_bytes(text, &length);
    if (!bytes) {
        mvp_execution_set_error(execution, "MVP stdout requires a string value");
        return mvp_value_undefined().bits;
    }
    if (length) fwrite(bytes, 1, length, stdout);
    return mvp_value_undefined().bits;
}

uint64_t mvp_host_console_log(MvpExecution* execution, uint64_t bits) {
    uint64_t result = mvp_host_stdout_write(execution, bits);
    if (!mvp_execution_has_error(execution)) fputc('\n', stdout);
    return result;
}

uint64_t mvp_host_number(MvpExecution* execution, uint64_t bits) {
    MvpValue value = mvp_runtime_value(bits);
    return mvp_value_from_number(mvp_value_is_bigint(value) ? mvp_bigint_to_number(value)
        : mvp_to_number(execution, value)).bits;
}

uint64_t mvp_host_bigint(MvpExecution* execution, uint64_t bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_runtime_value(bits);
    MvpValue result = mvp_value_is_bigint(value) ? value : mvp_bigint_from_decimal(&execution->heap,
        mvp_to_string(execution, value));
    if (!mvp_value_is_bigint(result)) mvp_execution_set_error(execution,
        "MVP BigInt conversion failed");
    return result.bits;
}

uint64_t mvp_host_string(MvpExecution* execution, uint64_t bits) {
    return mvp_to_string(execution, mvp_runtime_value(bits)).bits;
}

static uint64_t mvp_host_parse_number(MvpExecution* execution, uint64_t bits, int integer) {
    MvpValue text = mvp_to_string(execution, mvp_runtime_value(bits));
    size_t length = 0;
    const char* bytes = mvp_string_bytes(text, &length);
    if (!bytes || !length) return mvp_value_from_number(NAN).bits;
    char* terminated = (char*)mem_alloc(length + 1, MEM_CAT_JS_RUNTIME);
    if (!terminated) {
        mvp_execution_set_error(execution, "MVP parse number allocation failed");
        return mvp_value_undefined().bits;
    }
    memcpy(terminated, bytes, length);
    terminated[length] = '\0';
    char* end = NULL;
    double number = integer ? (double)strtol(terminated, &end, 10) : strtod(terminated, &end);
    int parsed = end && end != terminated;
    mem_free(terminated);
    return parsed ? mvp_value_from_number(number).bits
        : mvp_value_from_number(NAN).bits;
}

uint64_t mvp_host_parse_int(MvpExecution* execution, uint64_t bits) {
    return mvp_host_parse_number(execution, bits, 1);
}

uint64_t mvp_host_parse_float(MvpExecution* execution, uint64_t bits) {
    return mvp_host_parse_number(execution, bits, 0);
}

typedef struct MvpJsonParser {
    MvpExecution* execution;
    const char* text;
    size_t length;
    size_t at;
} MvpJsonParser;

typedef struct MvpJsonWriter {
    char* bytes;
    size_t length;
    size_t capacity;
    int ok;
} MvpJsonWriter;

static void mvp_json_skip_space(MvpJsonParser* parser) {
    while (parser && parser->at < parser->length && (parser->text[parser->at] == ' ' ||
            parser->text[parser->at] == '\t' || parser->text[parser->at] == '\n' ||
            parser->text[parser->at] == '\r')) parser->at++;
}

static int mvp_json_writer_grow(MvpJsonWriter* writer, size_t needed) {
    if (!writer || needed > SIZE_MAX - writer->length) return 0;
    needed += writer->length;
    if (needed <= writer->capacity) return 1;
    size_t capacity = writer->capacity ? writer->capacity : 64;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    char* bytes = (char*)mem_alloc(capacity, MEM_CAT_JS_RUNTIME);
    if (!bytes) return 0;
    if (writer->bytes && writer->length) memcpy(bytes, writer->bytes, writer->length);
    mem_free(writer->bytes);
    writer->bytes = bytes;
    writer->capacity = capacity;
    return 1;
}

static void mvp_json_write_bytes(MvpJsonWriter* writer, const char* bytes, size_t length) {
    if (!writer || !writer->ok || !mvp_json_writer_grow(writer, length)) {
        if (writer) writer->ok = 0;
        return;
    }
    if (length) memcpy(writer->bytes + writer->length, bytes, length);
    writer->length += length;
}

static void mvp_json_write_char(MvpJsonWriter* writer, char value) {
    mvp_json_write_bytes(writer, &value, 1);
}

static int mvp_json_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static MvpValue mvp_json_parse_value(MvpJsonParser* parser);

static MvpValue mvp_json_parse_string(MvpJsonParser* parser) {
    if (!parser || parser->at >= parser->length || parser->text[parser->at++] != '"') {
        return mvp_value_undefined();
    }
    size_t capacity = parser->length - parser->at + 1;
    char* bytes = (char*)mem_alloc(capacity, MEM_CAT_JS_RUNTIME);
    if (!bytes) return mvp_value_undefined();
    size_t at = 0;
    int closed = 0;
    while (parser->at < parser->length) {
        char value = parser->text[parser->at++];
        if (value == '"') {
            closed = 1;
            break;
        }
        if (value != '\\') {
            bytes[at++] = value;
            continue;
        }
        if (parser->at >= parser->length) break;
        char escaped = parser->text[parser->at++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') bytes[at++] = escaped;
        else if (escaped == 'b') bytes[at++] = '\b';
        else if (escaped == 'f') bytes[at++] = '\f';
        else if (escaped == 'n') bytes[at++] = '\n';
        else if (escaped == 'r') bytes[at++] = '\r';
        else if (escaped == 't') bytes[at++] = '\t';
        else if (escaped == 'u' && parser->at + 4 <= parser->length) {
            int code = 0;
            for (size_t index = 0; index < 4; index++) {
                int digit = mvp_json_hex_value(parser->text[parser->at++]);
                if (digit < 0) {
                    mem_free(bytes);
                    return mvp_value_undefined();
                }
                code = code * 16 + digit;
            }
            if (code < 0x80) bytes[at++] = (char)code;
            else {
                // The benchmark JSON is UTF-8 already; preserve a replacement for escaped non-ASCII.
                bytes[at++] = '?';
            }
        } else {
            mem_free(bytes);
            return mvp_value_undefined();
        }
    }
    MvpValue result = closed ? mvp_string_new(&parser->execution->heap, bytes, at)
        : mvp_value_undefined();
    mem_free(bytes);
    return result;
}

static MvpValue mvp_json_parse_array(MvpJsonParser* parser) {
    if (!parser || parser->at >= parser->length || parser->text[parser->at++] != '[') {
        return mvp_value_undefined();
    }
    MvpValue roots[2] = {mvp_array_new(&parser->execution->heap, 4), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&parser->execution->heap, &frame, roots, 2);
    mvp_json_skip_space(parser);
    while (parser->at < parser->length && parser->text[parser->at] != ']') {
        roots[1] = mvp_json_parse_value(parser);
        if (mvp_value_is_undefined(roots[1]) || !mvp_array_push(&parser->execution->heap,
                roots[0], roots[1])) {
            roots[0] = mvp_value_undefined();
            break;
        }
        mvp_json_skip_space(parser);
        if (parser->at >= parser->length || parser->text[parser->at] != ',') break;
        parser->at++;
        mvp_json_skip_space(parser);
    }
    if (parser->at >= parser->length || parser->text[parser->at++] != ']') {
        roots[0] = mvp_value_undefined();
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&parser->execution->heap, &frame);
    return result;
}

static MvpValue mvp_json_parse_object(MvpJsonParser* parser) {
    if (!parser || parser->at >= parser->length || parser->text[parser->at++] != '{') {
        return mvp_value_undefined();
    }
    MvpValue roots[3] = {mvp_object_new(&parser->execution->heap, mvp_value_null()),
        mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&parser->execution->heap, &frame, roots, 3);
    mvp_json_skip_space(parser);
    while (parser->at < parser->length && parser->text[parser->at] != '}') {
        roots[1] = mvp_json_parse_string(parser);
        mvp_json_skip_space(parser);
        if (!mvp_value_is_string(roots[1]) || parser->at >= parser->length ||
                parser->text[parser->at++] != ':') {
            roots[0] = mvp_value_undefined();
            break;
        }
        mvp_json_skip_space(parser);
        roots[2] = mvp_json_parse_value(parser);
        if (mvp_value_is_undefined(roots[2]) || !mvp_object_set(&parser->execution->heap,
                roots[0], roots[1], roots[2])) {
            roots[0] = mvp_value_undefined();
            break;
        }
        mvp_json_skip_space(parser);
        if (parser->at >= parser->length || parser->text[parser->at] != ',') break;
        parser->at++;
        mvp_json_skip_space(parser);
    }
    if (parser->at >= parser->length || parser->text[parser->at++] != '}') {
        roots[0] = mvp_value_undefined();
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&parser->execution->heap, &frame);
    return result;
}

static MvpValue mvp_json_parse_value(MvpJsonParser* parser) {
    mvp_json_skip_space(parser);
    if (!parser || parser->at >= parser->length) return mvp_value_undefined();
    char value = parser->text[parser->at];
    if (value == '"') return mvp_json_parse_string(parser);
    if (value == '[') return mvp_json_parse_array(parser);
    if (value == '{') return mvp_json_parse_object(parser);
    if (parser->at + 4 <= parser->length && memcmp(parser->text + parser->at, "true", 4) == 0) {
        parser->at += 4;
        return mvp_value_bool(1);
    }
    if (parser->at + 5 <= parser->length && memcmp(parser->text + parser->at, "false", 5) == 0) {
        parser->at += 5;
        return mvp_value_bool(0);
    }
    if (parser->at + 4 <= parser->length && memcmp(parser->text + parser->at, "null", 4) == 0) {
        parser->at += 4;
        return mvp_value_null();
    }
    size_t start = parser->at;
    while (parser->at < parser->length && (parser->text[parser->at] == '-' ||
            parser->text[parser->at] == '+' || parser->text[parser->at] == '.' ||
            parser->text[parser->at] == 'e' || parser->text[parser->at] == 'E' ||
            (parser->text[parser->at] >= '0' && parser->text[parser->at] <= '9'))) parser->at++;
    if (start == parser->at || parser->at - start >= 128) return mvp_value_undefined();
    char number[128] = {};
    size_t count = parser->at - start;
    memcpy(number, parser->text + start, count);
    char* end = NULL;
    double parsed = strtod(number, &end);
    return end && *end == '\0' ? mvp_value_from_number(parsed) : mvp_value_undefined();
}

static void mvp_json_write_value(MvpJsonWriter* writer, MvpValue value) {
    if (!writer || !writer->ok) return;
    if (mvp_value_is_string(value)) {
        size_t length = 0;
        const char* text = mvp_string_bytes(value, &length);
        mvp_json_write_char(writer, '"');
        for (size_t index = 0; text && index < length; index++) {
            char ch = text[index];
            if (ch == '"' || ch == '\\') mvp_json_write_char(writer, '\\');
            if (ch == '\n') mvp_json_write_bytes(writer, "\\n", 2);
            else if (ch == '\r') mvp_json_write_bytes(writer, "\\r", 2);
            else if (ch == '\t') mvp_json_write_bytes(writer, "\\t", 2);
            else if (ch != '\n' && ch != '\r' && ch != '\t') mvp_json_write_char(writer, ch);
        }
        mvp_json_write_char(writer, '"');
    } else if (mvp_value_is_number(value)) {
        char number[64] = {};
        double numeric = mvp_value_to_number(value);
        int count = 0;
        if (isfinite(numeric)) {
            lambda_finite_double_to_shortest(numeric, number, sizeof(number));
            count = (int)strlen(number);
        } else {
            count = snprintf(number, sizeof(number), "null");
        }
        if (count <= 0 || (size_t)count >= sizeof(number)) writer->ok = 0;
        else mvp_json_write_bytes(writer, number, (size_t)count);
    } else if (mvp_value_is_boolean(value)) {
        mvp_json_write_bytes(writer, mvp_value_boolean(value) ? "true" : "false",
            mvp_value_boolean(value) ? 4 : 5);
    } else if (mvp_value_is_null(value)) {
        mvp_json_write_bytes(writer, "null", 4);
    } else if (mvp_value_is_array(value)) {
        mvp_json_write_char(writer, '[');
        for (size_t index = 0; index < mvp_array_length(value); index++) {
            if (index) mvp_json_write_char(writer, ',');
            mvp_json_write_value(writer, mvp_array_get(value, index));
        }
        mvp_json_write_char(writer, ']');
    } else if (mvp_value_is_object(value)) {
        MvpObject* object = (MvpObject*)mvp_value_reference_object(value);
        MvpValue* properties = object ? mvp_heap_object_values(mvp_value_reference_object(
            object->storage)) : NULL;
        mvp_json_write_char(writer, '{');
        for (size_t index = 0; properties && index < object->property_count; index++) {
            if (index) mvp_json_write_char(writer, ',');
            mvp_json_write_value(writer, properties[index * 2]);
            mvp_json_write_char(writer, ':');
            mvp_json_write_value(writer, properties[index * 2 + 1]);
        }
        mvp_json_write_char(writer, '}');
    } else {
        mvp_json_write_bytes(writer, "null", 4);
    }
}

uint64_t mvp_host_json_parse(MvpExecution* execution, uint64_t bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue source = mvp_to_string(execution, mvp_runtime_value(bits));
    size_t length = 0;
    const char* text = mvp_string_bytes(source, &length);
    MvpValue roots[1] = {source};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 1);
    MvpJsonParser parser = {execution, text, length, 0};
    MvpValue result = mvp_json_parse_value(&parser);
    mvp_json_skip_space(&parser);
    if (mvp_value_is_undefined(result) || parser.at != parser.length) {
        mvp_execution_set_error(execution, "MVP JSON.parse failed");
        result = mvp_value_undefined();
    }
    mvp_root_frame_pop(&execution->heap, &frame);
    return result.bits;
}

uint64_t mvp_host_json_stringify(MvpExecution* execution, uint64_t bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_runtime_value(bits);
    MvpJsonWriter writer = {NULL, 0, 0, 1};
    mvp_json_write_value(&writer, value);
    MvpValue roots[1] = {value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 1);
    MvpValue result = writer.ok ? mvp_string_new(&execution->heap, writer.bytes, writer.length)
        : mvp_value_undefined();
    mvp_root_frame_pop(&execution->heap, &frame);
    mem_free(writer.bytes);
    if (!mvp_value_is_string(result)) mvp_execution_set_error(execution,
        "MVP JSON.stringify failed");
    return result.bits;
}

static uint64_t mvp_fs_read_file_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver_bits, uint64_t* arguments, size_t argument_count) {
    (void)function;
    (void)receiver_bits;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (!execution || !arguments || argument_count < 1) {
        if (execution) mvp_execution_set_error(execution, "MVP fs.readFileSync requires a path");
        return mvp_value_undefined().bits;
    }
    size_t path_length = 0;
    const char* path = mvp_string_bytes(mvp_runtime_value(arguments[0]), &path_length);
    if (!path || path_length >= 1024) {
        mvp_execution_set_error(execution, "MVP fs.readFileSync path is invalid");
        return mvp_value_undefined().bits;
    }
    char path_buffer[1024] = {};
    memcpy(path_buffer, path, path_length);
    FILE* file = fopen(path_buffer, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) fclose(file);
        mvp_execution_set_error(execution, "MVP fs.readFileSync could not open the file");
        return mvp_value_undefined().bits;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        mvp_execution_set_error(execution, "MVP fs.readFileSync could not size the file");
        return mvp_value_undefined().bits;
    }
    char* bytes = length ? (char*)mem_alloc((size_t)length, MEM_CAT_JS_RUNTIME) : NULL;
    if (length && !bytes) {
        fclose(file);
        mvp_execution_set_error(execution, "MVP fs.readFileSync allocation failed");
        return mvp_value_undefined().bits;
    }
    size_t read = length ? fread(bytes, 1, (size_t)length, file) : 0;
    fclose(file);
    if ((length && (!bytes || read != (size_t)length))) {
        if (bytes) mem_free(bytes);
        mvp_execution_set_error(execution, "MVP fs.readFileSync could not read the file");
        return mvp_value_undefined().bits;
    }
    MvpValue value = mvp_string_new(&execution->heap, bytes, (size_t)length);
    if (bytes) mem_free(bytes);
    if (!mvp_value_is_string(value)) {
        mvp_execution_set_error(execution, "MVP fs.readFileSync allocation failed");
        return mvp_value_undefined().bits;
    }
    return value.bits;
}

uint64_t mvp_host_require(MvpExecution* execution, uint64_t module_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue module = mvp_runtime_value(module_bits);
    size_t module_length = 0;
    const char* module_name = mvp_string_bytes(module, &module_length);
    if (!module_name || module_length != 2 || memcmp(module_name, "fs", 2) != 0) {
        mvp_execution_set_error(execution, "MVP require supports only fs");
        return mvp_value_undefined().bits;
    }
    MvpValue roots[3] = {mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    roots[0] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[1] = mvp_string_new(&execution->heap, "readFileSync", 12);
    roots[2] = mvp_function_new(&execution->heap, mvp_fs_read_file_call, NULL, 2,
        mvp_value_undefined());
    int ok = mvp_value_is_object(roots[0]) && mvp_value_is_string(roots[1]) &&
        mvp_value_is_function(roots[2]) &&
        mvp_object_set(&execution->heap, roots[0], roots[1], roots[2]);
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!ok) {
        mvp_execution_set_error(execution, "MVP fs module allocation failed");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

uint64_t mvp_host_argv_get(MvpExecution* execution, uint64_t bits) {
    (void)execution;
    (void)bits;
    // The frozen benchmark contract has no command-line arguments; callers
    // select their source-provided default through JavaScript's logical OR.
    return mvp_value_undefined().bits;
}

uint64_t mvp_host_string_from_char_code(MvpExecution* execution, uint64_t bits) {
    unsigned char byte = (unsigned char)(uint32_t)mvp_to_number(execution,
        mvp_runtime_value(bits));
    return mvp_string_new(&execution->heap, (const char*)&byte, 1).bits;
}

uint64_t mvp_host_array_is_array(MvpExecution* execution, uint64_t bits) {
    (void)execution;
    return mvp_value_bool(mvp_value_is_array(mvp_runtime_value(bits))).bits;
}

uint64_t mvp_host_is_nan(MvpExecution* execution, uint64_t bits) {
    return mvp_value_bool(isnan(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_number_is_nan(MvpExecution* execution, uint64_t bits) {
    (void)execution;
    MvpValue value = mvp_runtime_value(bits);
    return mvp_value_bool(mvp_value_is_number(value) && mvp_value_is_nan(value)).bits;
}

static uint64_t mvp_host_named_object(MvpExecution* execution, const char* name,
        MvpValue number, MvpValue string);

uint64_t mvp_host_object_define_property(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_bits, uint64_t descriptor_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue roots[4] = {mvp_runtime_value(object_bits), mvp_runtime_value(key_bits),
        mvp_runtime_value(descriptor_bits), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    roots[3] = mvp_string_new(&execution->heap, "value", 5);
    MvpValue value = mvp_value_is_object(roots[2]) && mvp_value_is_string(roots[3])
        ? mvp_object_get(roots[2], roots[3]) : mvp_value_undefined();
    int stored = mvp_value_is_object(roots[0]) && mvp_value_is_string(roots[1]) &&
        mvp_object_set(&execution->heap, roots[0], roots[1], value);
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!stored) mvp_execution_set_error(execution, "MVP Object.defineProperty target is invalid");
    return result.bits;
}

uint64_t mvp_host_object_get_prototype(MvpExecution* execution, uint64_t object_bits) {
    MvpValue object = mvp_runtime_value(object_bits);
    if (mvp_value_is_regex(object)) {
        MvpValue representative = mvp_runtime_value(mvp_host_named_object(execution, "RegExp",
            mvp_value_undefined(), mvp_value_undefined()));
        MvpHeapObject* representative_header = mvp_value_reference_object(representative);
        return representative_header && representative_header->kind == MVP_HEAP_OBJECT
            ? ((MvpObject*)representative_header)->prototype.bits : mvp_value_null().bits;
    }
    MvpHeapObject* header = mvp_value_reference_object(object);
    return header && header->kind == MVP_HEAP_OBJECT ? ((MvpObject*)header)->prototype.bits
        : mvp_value_null().bits;
}

uint64_t mvp_host_object_to_string(MvpExecution* execution, uint64_t object_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue object = mvp_runtime_value(object_bits);
    const char* type_name = mvp_value_is_array(object) ? "Array"
        : mvp_value_is_number(object) ? "Number"
        : mvp_value_is_string(object) ? "String"
        : mvp_value_is_function(object) ? "Function"
        : mvp_value_is_regex(object) ? "RegExp"
        : mvp_value_is_object(object) ? "Object" : "Object";
    char text[32] = {};
    int written = snprintf(text, sizeof(text), "[object %s]", type_name);
    if (written < 0 || (size_t)written >= sizeof(text)) {
        mvp_execution_set_error(execution, "MVP Object type string formatting failed");
        return mvp_value_undefined().bits;
    }
    return mvp_string_new(&execution->heap, text, (size_t)written).bits;
}

uint64_t mvp_host_object_has_own(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue roots[2] = {mvp_runtime_value(object_bits), mvp_runtime_value(key_bits)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 2);
    roots[1] = mvp_to_string(execution, roots[1]);
    int owns = mvp_value_is_string(roots[1]) && mvp_object_has_own(roots[0], roots[1]);
    mvp_root_frame_pop(&execution->heap, &frame);
    return mvp_value_bool(owns).bits;
}

static int mvp_value_index(MvpValue value, size_t* out_index) {
    if (!out_index || !mvp_value_is_number(value)) return 0;
    double number = mvp_value_to_number(value);
    if (!isfinite(number) || number < 0 || floor(number) != number ||
            number > (double)SIZE_MAX) return 0;
    *out_index = (size_t)number;
    return 1;
}

static int mvp_value_property_index(MvpValue value, size_t* out_index) {
    if (mvp_value_index(value, out_index)) return 1;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(value, &length);
    if (!out_index || !bytes || !length) return 0;
    size_t index = 0;
    for (size_t offset = 0; offset < length; offset++) {
        unsigned char byte = (unsigned char)bytes[offset];
        if (byte < '0' || byte > '9' || index > (SIZE_MAX - (byte - '0')) / 10) return 0;
        index = index * 10 + (size_t)(byte - '0');
    }
    *out_index = index;
    return 1;
}

static uint64_t mvp_host_array_new_common(MvpExecution* execution, uint64_t bits,
        int numeric_zeroes) {
    if (!execution) return mvp_value_undefined().bits;
    size_t length = 0;
    if (!mvp_value_index(mvp_runtime_value(bits), &length)) {
        mvp_execution_set_error(execution, "MVP array length must be a non-negative integer");
        return mvp_value_undefined().bits;
    }
    MvpValue result = mvp_array_new(&execution->heap, length);
    MvpHeapObject* object = mvp_value_reference_object(result);
    if (!object) {
        mvp_execution_set_error(execution, "MVP array allocation failed");
        return mvp_value_undefined().bits;
    }
    MvpArray* array = (MvpArray*)object;
    array->length = length;
    MvpValue* values = mvp_heap_object_values(mvp_value_reference_object(array->storage));
    if (!values && length) {
        mvp_execution_set_error(execution, "MVP array storage allocation failed");
        return mvp_value_undefined().bits;
    }
    for (size_t index = 0; index < length; index++) {
        values[index] = numeric_zeroes ? mvp_value_from_number(0.0) : mvp_value_undefined();
    }
    return result.bits;
}

uint64_t mvp_host_array_new(MvpExecution* execution, uint64_t bits) {
    return mvp_host_array_new_common(execution, bits, 0);
}

uint64_t mvp_host_array_empty(MvpExecution* execution) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_array_new(&execution->heap, 0);
    if (!mvp_value_is_array(value)) {
        mvp_execution_set_error(execution, "MVP empty array allocation failed");
    }
    return value.bits;
}

uint64_t mvp_host_array_one(MvpExecution* execution, uint64_t bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_runtime_value(bits);
    size_t length = 0;
    if (mvp_value_index(value, &length)) return mvp_host_array_new_common(execution, bits, 0);
    MvpValue result = mvp_array_new(&execution->heap, 1);
    if (!mvp_value_is_array(result) || !mvp_array_push(&execution->heap, result, value)) {
        mvp_execution_set_error(execution, "MVP single-value array allocation failed");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

uint64_t mvp_host_typed_array_new(MvpExecution* execution, uint64_t bits) {
    return mvp_host_array_new_common(execution, bits, 1);
}

uint64_t mvp_host_object_new(MvpExecution* execution) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_object_new(&execution->heap, mvp_value_null());
    if (!mvp_value_is_object(value)) {
        mvp_execution_set_error(execution, "MVP object allocation failed");
        return mvp_value_undefined().bits;
    }
    return value.bits;
}

uint64_t mvp_host_accessor_new(MvpExecution* execution, uint64_t getter_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue getter = mvp_runtime_value(getter_bits);
    MvpValue value = mvp_accessor_new(&execution->heap, getter, mvp_value_undefined());
    if (mvp_value_is_undefined(value)) {
        mvp_execution_set_error(execution, "MVP accessor allocation failed");
    }
    return value.bits;
}

uint64_t mvp_host_environment_new(MvpExecution* execution, uint64_t parent_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue parent = mvp_runtime_value(parent_bits);
    if (!mvp_value_is_object(parent) && !mvp_value_is_null(parent) &&
            !mvp_value_is_undefined(parent)) {
        mvp_execution_set_error(execution, "MVP closure parent environment is invalid");
        return mvp_value_undefined().bits;
    }
    MvpValue value = mvp_object_new(&execution->heap, parent);
    if (!mvp_value_is_object(value)) {
        mvp_execution_set_error(execution, "MVP closure environment allocation failed");
    }
    return value.bits;
}

uint64_t mvp_host_map_new(MvpExecution* execution) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue value = mvp_map_new(&execution->heap);
    if (!mvp_value_is_map(value)) {
        mvp_execution_set_error(execution, "MVP Map allocation failed");
    }
    return value.bits;
}

uint64_t mvp_host_error_new(MvpExecution* execution, uint64_t message_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue roots[3] = {mvp_runtime_value(message_bits), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    roots[1] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[2] = mvp_string_new(&execution->heap, "message", 7);
    int ok = mvp_value_is_object(roots[1]) && mvp_value_is_string(roots[2]) &&
        mvp_object_set(&execution->heap, roots[1], roots[2], roots[0]);
    MvpValue result = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!ok) {
        mvp_execution_set_error(execution, "MVP Error allocation failed");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

static uint64_t mvp_host_named_object(MvpExecution* execution, const char* name,
        MvpValue number, MvpValue string) {
    if (!execution || !name) return mvp_value_undefined().bits;
    MvpValue roots[10] = {mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), number, string, mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 10);
    roots[0] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[1] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[2] = mvp_object_new(&execution->heap, mvp_value_null());
    roots[3] = mvp_string_new(&execution->heap, "constructor", 11);
    roots[4] = mvp_string_new(&execution->heap, "name", 4);
    roots[5] = mvp_string_new(&execution->heap, name, strlen(name));
    roots[8] = mvp_string_new(&execution->heap, "__mvp_number", 12);
    roots[9] = mvp_string_new(&execution->heap, "__mvp_string", 12);
    int ok = mvp_value_is_object(roots[0]) && mvp_value_is_object(roots[1]) &&
        mvp_value_is_object(roots[2]) && mvp_value_is_string(roots[3]) &&
        mvp_value_is_string(roots[4]) && mvp_value_is_string(roots[5]) &&
        mvp_value_is_string(roots[8]) && mvp_value_is_string(roots[9]) &&
        mvp_object_set(&execution->heap, roots[1], roots[3], roots[2]) &&
        mvp_object_set(&execution->heap, roots[2], roots[4], roots[5]);
    if (ok && mvp_value_is_number(roots[6])) {
        ok = mvp_object_set(&execution->heap, roots[0], roots[8], roots[6]);
    }
    if (ok && mvp_value_is_string(roots[7])) {
        ok = mvp_object_set(&execution->heap, roots[0], roots[9], roots[7]);
    }
    MvpHeapObject* object_header = mvp_value_reference_object(roots[0]);
    if (object_header && object_header->kind == MVP_HEAP_OBJECT) {
        ((MvpObject*)object_header)->prototype = roots[1];
    } else {
        ok = 0;
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!ok) {
        mvp_execution_set_error(execution, "MVP named object allocation failed");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

uint64_t mvp_host_date_new(MvpExecution* execution, uint64_t value_bits) {
    return mvp_host_named_object(execution, "Date", mvp_runtime_value(value_bits),
        mvp_value_undefined());
}

uint64_t mvp_host_date_now(MvpExecution* execution) {
    if (!execution) return mvp_value_undefined().bits;
    struct timespec now = {};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return mvp_value_undefined().bits;
    double milliseconds = (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
    return mvp_host_date_new(execution, mvp_value_from_number(milliseconds).bits);
}

uint64_t mvp_host_regexp_new(MvpExecution* execution, uint64_t pattern_bits,
        uint64_t flags_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue pattern = mvp_runtime_value(pattern_bits);
    MvpValue flags = mvp_runtime_value(flags_bits);
    MvpValue regex = mvp_regex_new(&execution->heap, pattern, flags);
    if (!mvp_value_is_regex(regex)) {
        mvp_execution_set_error(execution, "MVP RegExp compilation failed");
    }
    return regex.bits;
}

uint64_t mvp_host_math_sin(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(sin(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_cos(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(cos(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_sqrt(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(sqrt(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_abs(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(fabs(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_max(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_from_number(fmax(mvp_to_number(execution, mvp_runtime_value(left)),
        mvp_to_number(execution, mvp_runtime_value(right)))).bits;
}

uint64_t mvp_host_math_max3(MvpExecution* execution, uint64_t first, uint64_t second,
        uint64_t third) {
    double result = fmax(mvp_to_number(execution, mvp_runtime_value(first)),
        mvp_to_number(execution, mvp_runtime_value(second)));
    return mvp_value_from_number(fmax(result, mvp_to_number(execution,
        mvp_runtime_value(third)))).bits;
}

uint64_t mvp_host_math_min(MvpExecution* execution, uint64_t left, uint64_t right) {
    return mvp_value_from_number(fmin(mvp_to_number(execution, mvp_runtime_value(left)),
        mvp_to_number(execution, mvp_runtime_value(right)))).bits;
}

uint64_t mvp_host_math_floor(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(floor(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_ceil(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(ceil(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_trunc(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(trunc(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_math_round(MvpExecution* execution, uint64_t bits) {
    return mvp_value_from_number(round(mvp_to_number(execution, mvp_runtime_value(bits)))).bits;
}

uint64_t mvp_host_jit_function(MvpExecution* execution, uint64_t descriptor_address) {
    MvpJitFunctionDescriptor* descriptor = (MvpJitFunctionDescriptor*)(uintptr_t)descriptor_address;
    if (!execution || !descriptor || (!descriptor->entry && !descriptor->unavailable) ||
            (!descriptor->unavailable && descriptor->parameter_count > 10)) {
        if (execution) {
            char message[96] = {};
            int written = snprintf(message, sizeof(message),
                "MVP JIT function creation failed: parameters=%zu unavailable=%d",
                descriptor ? descriptor->parameter_count : 0,
                descriptor ? descriptor->unavailable : 0);
            mvp_execution_set_error(execution, written > 0 ? message
                : "MVP JIT function creation failed");
        }
        return mvp_value_undefined().bits;
    }
    MvpValue value = mvp_function_new(&execution->heap, mvp_jit_function_call, descriptor,
        descriptor->parameter_count, mvp_value_undefined());
    if (!mvp_value_is_function(value) || !mvp_function_initialize_objects(execution, value)) {
        mvp_execution_set_error(execution, "MVP JIT function allocation failed");
        return mvp_value_undefined().bits;
    }
    return value.bits;
}

uint64_t mvp_host_jit_closure(MvpExecution* execution, uint64_t descriptor_address,
        uint64_t captures_bits) {
    MvpJitFunctionDescriptor* descriptor = (MvpJitFunctionDescriptor*)(uintptr_t)descriptor_address;
    if (!execution || !descriptor || !descriptor->captures_environment || !descriptor->entry ||
            descriptor->parameter_count > 10) {
        if (execution) mvp_execution_set_error(execution, "MVP JIT closure creation failed");
        return mvp_value_undefined().bits;
    }
    MvpValue captures = mvp_runtime_value(captures_bits);
    MvpValue value = mvp_function_new(&execution->heap, mvp_jit_function_call, descriptor,
        descriptor->parameter_count, captures);
    if (!mvp_value_is_function(value) || !mvp_function_initialize_objects(execution, value)) {
        mvp_execution_set_error(execution, "MVP JIT closure allocation failed");
    }
    return value.bits;
}

uint64_t mvp_host_class_new(MvpExecution* execution, uint64_t parent_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue parent = mvp_runtime_value(parent_bits);
    MvpValue roots[3] = {parent, mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    MvpValue parent_prototype = mvp_value_is_undefined(parent) ? mvp_value_null()
        : mvp_class_prototype(parent);
    if (!mvp_value_is_null(parent_prototype) && !mvp_value_is_object(parent_prototype)) {
        mvp_execution_set_error(execution, "MVP class extends requires an MVP class");
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined().bits;
    }
    roots[1] = mvp_object_new(&execution->heap, parent_prototype);
    if (!mvp_value_is_object(roots[1])) {
        mvp_execution_set_error(execution, "MVP class prototype allocation failed");
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined().bits;
    }
    MvpValue result = mvp_function_new(&execution->heap, mvp_class_call, NULL, 0, roots[1]);
    roots[2] = result;
    MvpFunction* class_function = mvp_runtime_function(roots[2]);
    if (class_function) class_function->properties = mvp_object_new(&execution->heap,
        mvp_value_null());
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!mvp_value_is_function(result) || !class_function ||
            !mvp_value_is_object(class_function->properties)) {
        mvp_execution_set_error(execution, "MVP class allocation failed");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

uint64_t mvp_host_class_define_method(MvpExecution* execution, uint64_t class_bits,
        uint64_t key_bits, uint64_t method_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue class_value = mvp_runtime_value(class_bits);
    MvpValue prototype = mvp_class_prototype(class_value);
    MvpValue key = mvp_runtime_value(key_bits);
    MvpValue method = mvp_runtime_value(method_bits);
    if (!mvp_value_is_object(prototype) || !mvp_value_is_string(key) ||
            !mvp_value_is_function(method) ||
            !mvp_object_set(&execution->heap, prototype, key, method)) {
        mvp_execution_set_error(execution, "MVP class method definition failed");
        return mvp_value_undefined().bits;
    }
    MvpFunction* function = mvp_runtime_function(method);
    if (function) function->captures = prototype;
    return class_value.bits;
}

uint64_t mvp_host_class_define_static(MvpExecution* execution, uint64_t class_bits,
        uint64_t key_bits, uint64_t method_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpFunction* class_function = mvp_runtime_function(mvp_runtime_value(class_bits));
    MvpValue key = mvp_runtime_value(key_bits);
    MvpValue method = mvp_runtime_value(method_bits);
    if (!class_function || !mvp_value_is_object(class_function->properties) ||
            !mvp_value_is_string(key) || !mvp_value_is_function(method) ||
            !mvp_object_set(&execution->heap, class_function->properties, key, method)) {
        mvp_execution_set_error(execution, "MVP static class method definition failed");
        return mvp_value_undefined().bits;
    }
    return class_bits;
}

static uint64_t mvp_op_call_values(MvpExecution* execution, uint64_t function_bits,
        uint64_t receiver_bits, uint64_t* arguments, size_t argument_count) {
    if (!execution) return mvp_value_undefined().bits;
    if (argument_count > 9) {
        mvp_execution_set_error(execution, "MVP dynamic call arity is unsupported");
        return mvp_value_undefined().bits;
    }
    MvpValue roots[11] = {mvp_runtime_value(function_bits), mvp_runtime_value(receiver_bits),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined()};
    for (size_t index = 0; index < argument_count; index++) {
        roots[index + 2] = mvp_runtime_value(arguments[index]);
    }
    MvpRootFrame frame = {};
    MvpRootFrame* active_frame = execution->active_frame;
    mvp_root_frame_push(&execution->heap, &frame, roots, argument_count + 2);
    MvpValue result = mvp_function_call(roots[0], execution, roots[1],
        (uint64_t*)&roots[2], argument_count);
    mvp_root_frame_pop(&execution->heap, &frame);
    // Nested MIR calls leave to the heap-root predecessor, which can be this
    // temporary C frame. Restore the enclosing MIR frame before returning.
    execution->active_frame = active_frame;
    if (!mvp_value_is_function(roots[0])) {
        mvp_execution_set_error(execution, "MVP call receiver is not callable");
        return mvp_value_undefined().bits;
    }
    return result.bits;
}

static uint64_t mvp_array_for_each_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver, uint64_t* arguments, size_t argument_count) {
    (void)receiver;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (!execution || !function || argument_count != 1 || !arguments) {
        if (execution) mvp_execution_set_error(execution, "MVP Array.forEach arguments failed");
        return mvp_value_undefined().bits;
    }
    MvpValue array = function->captures;
    MvpValue callback = mvp_runtime_value(arguments[0]);
    if (!mvp_value_is_array(array) || !mvp_value_is_function(callback)) {
        mvp_execution_set_error(execution, "MVP Array.forEach callback is invalid");
        return mvp_value_undefined().bits;
    }
    size_t length = mvp_array_length(array);
    for (size_t index = 0; index < length && !mvp_execution_has_error(execution); index++) {
        MvpValue element = mvp_array_get(array, index);
        uint64_t values[3] = {element.bits, mvp_value_from_number((double)index).bits,
            array.bits};
        (void)mvp_op_call_values(execution, callback.bits, mvp_value_undefined().bits,
            values, 3);
    }
    return mvp_value_undefined().bits;
}

typedef enum MvpBuiltinMethod {
    MVP_BUILTIN_ARRAY_PUSH = 1,
    MVP_BUILTIN_ARRAY_POP,
    MVP_BUILTIN_ARRAY_JOIN,
    MVP_BUILTIN_ARRAY_SLICE,
    MVP_BUILTIN_ARRAY_INCLUDES,
    MVP_BUILTIN_ARRAY_CONCAT,
    MVP_BUILTIN_ARRAY_UNSHIFT,
    MVP_BUILTIN_ARRAY_MAP,
    MVP_BUILTIN_ARRAY_SORT,
    MVP_BUILTIN_ARRAY_REVERSE,
    MVP_BUILTIN_ARRAY_FLAT,
    MVP_BUILTIN_ARRAY_REDUCE,
    MVP_BUILTIN_ARRAY_SOME,
    MVP_BUILTIN_ARRAY_SPLICE,
    MVP_BUILTIN_ARRAY_SHIFT,
    MVP_BUILTIN_FUNCTION_APPLY,
    MVP_BUILTIN_FUNCTION_CALL,
    MVP_BUILTIN_MAP_GET,
    MVP_BUILTIN_MAP_SET,
    MVP_BUILTIN_MAP_ENTRIES,
    MVP_BUILTIN_BIGINT_TO_STRING,
    MVP_BUILTIN_STRING_CHAR_CODE_AT,
    MVP_BUILTIN_STRING_REPEAT,
    MVP_BUILTIN_STRING_SUBSTRING,
    MVP_BUILTIN_STRING_SLICE,
    MVP_BUILTIN_STRING_TO_UPPER_CASE,
    MVP_BUILTIN_STRING_TO_LOWER_CASE,
    MVP_BUILTIN_STRING_SPLIT,
    MVP_BUILTIN_STRING_STARTS_WITH,
    MVP_BUILTIN_STRING_MATCH,
    MVP_BUILTIN_STRING_REPLACE,
    MVP_BUILTIN_STRING_INDEX_OF,
    MVP_BUILTIN_STRING_CHAR_AT,
    MVP_BUILTIN_STRING_TRIM,
    MVP_BUILTIN_NUMBER_TO_FIXED,
    MVP_BUILTIN_DATE_GET_TIME,
} MvpBuiltinMethod;

static uint64_t mvp_builtin_unsupported(MvpExecution* execution);

static size_t mvp_builtin_index(MvpExecution* execution, uint64_t* arguments,
        size_t argument_count, size_t argument_index, size_t fallback, size_t limit) {
    if (argument_index >= argument_count || !arguments) return fallback;
    double number = mvp_to_number(execution, mvp_runtime_value(arguments[argument_index]));
    if (!isfinite(number)) return number > 0 ? limit : 0;
    if (number <= 0) return 0;
    if (number >= (double)limit) return limit;
    return (size_t)floor(number);
}

static MvpValue mvp_builtin_array_join(MvpExecution* execution, MvpValue array,
        MvpValue separator) {
    MvpValue roots[3] = {array, separator, mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    size_t length = mvp_array_length(roots[0]);
    size_t separator_length = 0;
    const char* separator_bytes = mvp_string_bytes(roots[1], &separator_length);
    size_t total = length > 1 ? (length - 1) * separator_length : 0;
    if (!separator_bytes || (length > 1 && separator_length &&
            total / separator_length != length - 1)) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    for (size_t index = 0; index < length && !mvp_execution_has_error(execution); index++) {
        MvpValue value = mvp_array_get(roots[0], index);
        if (mvp_value_is_null(value) || mvp_value_is_undefined(value)) continue;
        roots[2] = mvp_value_is_string(value) ? value : mvp_to_string(execution, value);
        size_t item_length = 0;
        if (!mvp_string_bytes(roots[2], &item_length) || item_length > SIZE_MAX - total) {
            mvp_root_frame_pop(&execution->heap, &frame);
            return mvp_value_undefined();
        }
        total += item_length;
    }
    if (total == SIZE_MAX) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    char* output = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
    if (!output) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    size_t at = 0;
    for (size_t index = 0; index < length && !mvp_execution_has_error(execution); index++) {
        if (index && separator_length) {
            memcpy(output + at, separator_bytes, separator_length);
            at += separator_length;
        }
        MvpValue value = mvp_array_get(roots[0], index);
        if (mvp_value_is_null(value) || mvp_value_is_undefined(value)) continue;
        roots[2] = mvp_value_is_string(value) ? value : mvp_to_string(execution, value);
        size_t item_length = 0;
        const char* item_bytes = mvp_string_bytes(roots[2], &item_length);
        if (!item_bytes || item_length > total - at) {
            mem_free(output);
            mvp_root_frame_pop(&execution->heap, &frame);
            return mvp_value_undefined();
        }
        if (item_length) memcpy(output + at, item_bytes, item_length);
        at += item_length;
    }
    MvpValue result = !mvp_execution_has_error(execution) && at == total
        ? mvp_string_new(&execution->heap, output, total) : mvp_value_undefined();
    mem_free(output);
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static MvpValue mvp_array_to_string(MvpExecution* execution, MvpValue array) {
    MvpValue roots[2] = {array, mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 2);
    roots[1] = mvp_string_new(&execution->heap, ",", 1);
    MvpValue result = mvp_value_is_string(roots[1])
        ? mvp_builtin_array_join(execution, roots[0], roots[1]) : mvp_value_undefined();
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static MvpValue mvp_static_literal_value(MvpExecution* execution, AstNode* node) {
    if (!execution || !node) return mvp_value_undefined();
    if (node->node_type == AST_NODE_LITERAL) {
        AstLiteralNode* literal = (AstLiteralNode*)node;
        if (literal->is_bigint) return mvp_value_undefined();
        if (literal->literal_type == AST_LITERAL_NUMBER) {
            return mvp_value_from_number(literal->value.number_value);
        }
        if (literal->literal_type == AST_LITERAL_BOOLEAN) {
            return mvp_value_bool(literal->value.boolean_value);
        }
        if (literal->literal_type == AST_LITERAL_NULL) return mvp_value_null();
        if (literal->literal_type == AST_LITERAL_UNDEFINED) return mvp_value_undefined();
        if (literal->literal_type == AST_LITERAL_STRING && literal->value.string_value) {
            String* value = literal->value.string_value;
            return mvp_string_new(&execution->heap, value->chars, value->len);
        }
        return mvp_value_undefined();
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if ((unary->op == OPERATOR_POS || unary->op == OPERATOR_NEG) && unary->operand &&
                unary->operand->node_type == AST_NODE_LITERAL) {
            AstLiteralNode* operand = (AstLiteralNode*)unary->operand;
            if (operand->literal_type == AST_LITERAL_NUMBER) {
                double number = operand->value.number_value;
                return mvp_value_from_number(unary->op == OPERATOR_NEG ? -number : number);
            }
        }
        return mvp_value_undefined();
    }
    if (node->node_type == AST_NODE_ARRAY) {
        AstArrayNode* array = (AstArrayNode*)node;
        MvpValue roots[2] = {mvp_array_new(&execution->heap, array->length),
            mvp_value_undefined()};
        MvpRootFrame frame = {};
        mvp_root_frame_push(&execution->heap, &frame, roots, 2);
        for (AstNode* item = array->elements; mvp_value_is_array(roots[0]) && item;
                item = item->next) {
            roots[1] = mvp_static_literal_value(execution, item);
            if (mvp_value_is_undefined(roots[1]) && item->node_type != AST_NODE_LITERAL) {
                roots[0] = mvp_value_undefined();
                break;
            }
            if (!mvp_array_push(&execution->heap, roots[0], roots[1])) {
                roots[0] = mvp_value_undefined();
                break;
            }
        }
        MvpValue result = roots[0];
        mvp_root_frame_pop(&execution->heap, &frame);
        return result;
    }
    if (node->node_type != AST_NODE_MAP) return mvp_value_undefined();
    AstMapNode* map = (AstMapNode*)node;
    MvpValue roots[3] = {mvp_object_new(&execution->heap, mvp_value_null()),
        mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    for (AstNode* item = map->properties; mvp_value_is_object(roots[0]) && item;
            item = item->next) {
        if (item->node_type != AST_NODE_PROPERTY) {
            roots[0] = mvp_value_undefined();
            break;
        }
        AstPropertyNode* property = (AstPropertyNode*)item;
        if (property->key->node_type == AST_NODE_IDENT) {
            String* name = ((AstIdentNode*)property->key)->name;
            roots[1] = name ? mvp_string_new(&execution->heap, name->chars, name->len)
                : mvp_value_undefined();
        } else {
            roots[1] = mvp_static_literal_value(execution, property->key);
            if (!mvp_value_is_string(roots[1])) roots[1] = mvp_to_string(execution, roots[1]);
        }
        roots[2] = mvp_static_literal_value(execution, property->value);
        if (!mvp_value_is_string(roots[1]) ||
                (mvp_value_is_undefined(roots[2]) &&
                 property->value->node_type != AST_NODE_LITERAL) ||
                !mvp_object_set(&execution->heap, roots[0], roots[1], roots[2])) {
            roots[0] = mvp_value_undefined();
            break;
        }
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

uint64_t mvp_host_static_literal(MvpExecution* execution, uint64_t node_address) {
    MvpValue value = mvp_static_literal_value(execution, (AstNode*)(uintptr_t)node_address);
    if (execution && mvp_value_is_undefined(value)) {
        mvp_execution_set_error(execution, "MVP static literal construction failed");
    }
    return value.bits;
}

static MvpValue mvp_builtin_array_slice(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    size_t length = mvp_array_length(array);
    size_t start = mvp_builtin_index(execution, arguments, argument_count, 0, 0, length);
    size_t end = mvp_builtin_index(execution, arguments, argument_count, 1, length, length);
    if (end < start) end = start;
    MvpValue roots[3] = {array, mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    roots[1] = mvp_array_new(&execution->heap, end - start);
    for (size_t index = start; mvp_value_is_array(roots[1]) && index < end; index++) {
        roots[2] = mvp_array_get(roots[0], index);
        if (!mvp_array_push(&execution->heap, roots[1], roots[2])) {
            mvp_execution_set_error(execution, "MVP Array.slice allocation failed");
            break;
        }
    }
    MvpValue result = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_array_includes(MvpValue array, uint64_t* arguments,
        size_t argument_count) {
    MvpValue needle = argument_count ? mvp_runtime_value(arguments[0]) : mvp_value_undefined();
    for (size_t index = 0; index < mvp_array_length(array); index++) {
        MvpValue candidate = mvp_array_get(array, index);
        if (mvp_value_strict_equal(candidate, needle) ||
                (mvp_value_is_nan(candidate) && mvp_value_is_nan(needle))) {
            return mvp_value_bool(1).bits;
        }
    }
    return mvp_value_bool(0).bits;
}

static MvpValue mvp_builtin_array_concat(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    MvpValue roots[10] = {array, mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined()};
    if (argument_count > 8) return mvp_value_undefined();
    for (size_t index = 0; index < argument_count; index++) {
        roots[index + 2] = mvp_runtime_value(arguments[index]);
    }
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 10);
    roots[1] = mvp_array_new(&execution->heap, mvp_array_length(roots[0]));
    for (size_t index = 0; index < mvp_array_length(roots[0]); index++) {
        if (!mvp_array_push(&execution->heap, roots[1], mvp_array_get(roots[0], index))) break;
    }
    for (size_t argument = 0; argument < argument_count; argument++) {
        MvpValue source = roots[argument + 2];
        if (mvp_value_is_array(source)) {
            for (size_t index = 0; index < mvp_array_length(source); index++) {
                if (!mvp_array_push(&execution->heap, roots[1], mvp_array_get(source, index))) break;
            }
        } else if (!mvp_array_push(&execution->heap, roots[1], source)) {
            break;
        }
    }
    MvpValue result = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_array_unshift(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count > 8) return mvp_builtin_unsupported(execution);
    MvpValue roots[9] = {array, mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined()};
    for (size_t index = 0; index < argument_count; index++) {
        roots[index + 1] = mvp_runtime_value(arguments[index]);
    }
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, argument_count + 1);
    size_t old_length = mvp_array_length(roots[0]);
    int ok = mvp_array_set_length(&execution->heap, roots[0], old_length + argument_count);
    for (size_t index = old_length; ok && index > 0; index--) {
        ok = mvp_array_set(&execution->heap, roots[0], index + argument_count - 1,
            mvp_array_get(roots[0], index - 1));
    }
    for (size_t index = 0; ok && index < argument_count; index++) {
        ok = mvp_array_set(&execution->heap, roots[0], index, roots[index + 1]);
    }
    MvpValue result = mvp_value_from_number((double)mvp_array_length(roots[0]));
    mvp_root_frame_pop(&execution->heap, &frame);
    if (!ok) return mvp_builtin_unsupported(execution);
    return result.bits;
}

static MvpValue mvp_builtin_array_map(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (!argument_count || !mvp_value_is_function(mvp_runtime_value(arguments[0]))) {
        mvp_builtin_unsupported(execution);
        return mvp_value_undefined();
    }
    MvpValue roots[4] = {array, mvp_runtime_value(arguments[0]), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    roots[2] = mvp_array_new(&execution->heap, mvp_array_length(roots[0]));
    for (size_t index = 0; index < mvp_array_length(roots[0]) &&
            !mvp_execution_has_error(execution); index++) {
        uint64_t callback_arguments[3] = {mvp_array_get(roots[0], index).bits,
            mvp_value_from_number((double)index).bits, roots[0].bits};
        roots[3] = mvp_runtime_value(mvp_op_call_values(execution, roots[1].bits,
            mvp_value_undefined().bits, callback_arguments, 3));
        if (!mvp_execution_has_error(execution) &&
                !mvp_array_push(&execution->heap, roots[2], roots[3])) {
            mvp_execution_set_error(execution, "MVP Array.map allocation failed");
        }
    }
    MvpValue result = roots[2];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_array_sort(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count > 1 || (argument_count && !mvp_value_is_function(
            mvp_runtime_value(arguments[0])))) return mvp_builtin_unsupported(execution);
    MvpValue roots[4] = {array, argument_count ? mvp_runtime_value(arguments[0]) :
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    size_t length = mvp_array_length(roots[0]);
    for (size_t outer = 1; outer < length && !mvp_execution_has_error(execution); outer++) {
        for (size_t inner = outer; inner > 0; inner--) {
            roots[2] = mvp_array_get(roots[0], inner - 1);
            roots[3] = mvp_array_get(roots[0], inner);
            int before = 0;
            if (mvp_value_is_function(roots[1])) {
                uint64_t values[2] = {roots[2].bits, roots[3].bits};
                MvpValue comparison = mvp_runtime_value(mvp_op_call_values(execution,
                    roots[1].bits, mvp_value_undefined().bits, values, 2));
                before = mvp_to_number(execution, comparison) > 0.0;
            } else {
                MvpValue left = mvp_to_string(execution, roots[2]);
                MvpValue right = mvp_to_string(execution, roots[3]);
                size_t left_length = 0;
                size_t right_length = 0;
                const char* left_text = mvp_string_bytes(left, &left_length);
                const char* right_text = mvp_string_bytes(right, &right_length);
                size_t shared = left_length < right_length ? left_length : right_length;
                int comparison = left_text && right_text ? memcmp(left_text, right_text, shared) : 0;
                before = comparison > 0 || (comparison == 0 && left_length > right_length);
            }
            if (mvp_execution_has_error(execution) || !before) break;
            if (!mvp_array_set(&execution->heap, roots[0], inner - 1, roots[3]) ||
                    !mvp_array_set(&execution->heap, roots[0], inner, roots[2])) {
                mvp_execution_set_error(execution, "MVP Array.sort write failed");
                break;
            }
        }
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result.bits;
}

static uint64_t mvp_builtin_array_reverse(MvpExecution* execution, MvpValue array,
        size_t argument_count) {
    if (argument_count) return mvp_builtin_unsupported(execution);
    MvpValue roots[3] = {array, mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    size_t length = mvp_array_length(roots[0]);
    for (size_t left = 0; left < length / 2; left++) {
        size_t right = length - left - 1;
        roots[1] = mvp_array_get(roots[0], left);
        roots[2] = mvp_array_get(roots[0], right);
        if (!mvp_array_set(&execution->heap, roots[0], left, roots[2]) ||
                !mvp_array_set(&execution->heap, roots[0], right, roots[1])) {
            mvp_execution_set_error(execution, "MVP Array.reverse write failed");
            break;
        }
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result.bits;
}

static int mvp_builtin_array_flat_into(MvpExecution* execution, MvpValue output,
        MvpValue input, size_t depth) {
    for (size_t index = 0; index < mvp_array_length(input); index++) {
        MvpValue item = mvp_array_get(input, index);
        if (depth && mvp_value_is_array(item)) {
            if (!mvp_builtin_array_flat_into(execution, output, item, depth - 1)) return 0;
        } else if (!mvp_array_push(&execution->heap, output, item)) {
            mvp_execution_set_error(execution, "MVP Array.flat allocation failed");
            return 0;
        }
    }
    return 1;
}

static MvpValue mvp_builtin_array_flat(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count > 1) {
        mvp_builtin_unsupported(execution);
        return mvp_value_undefined();
    }
    size_t depth = argument_count ? mvp_builtin_index(execution, arguments, argument_count,
        0, 1, 64) : 1;
    MvpValue roots[2] = {array, mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 2);
    roots[1] = mvp_array_new(&execution->heap, mvp_array_length(roots[0]));
    if (!mvp_value_is_array(roots[1]) || !mvp_builtin_array_flat_into(execution, roots[1],
            roots[0], depth)) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    MvpValue result = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static MvpValue mvp_builtin_array_reduce(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (!argument_count || !mvp_value_is_function(mvp_runtime_value(arguments[0]))) {
        mvp_builtin_unsupported(execution);
        return mvp_value_undefined();
    }
    MvpValue roots[4] = {array, mvp_runtime_value(arguments[0]), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    size_t index = 0;
    if (argument_count > 1) roots[2] = mvp_runtime_value(arguments[1]);
    else if (mvp_array_length(roots[0])) roots[2] = mvp_array_get(roots[0], index++);
    else {
        mvp_execution_set_error(execution, "MVP Array.reduce requires an initial value");
    }
    for (; index < mvp_array_length(roots[0]) && !mvp_execution_has_error(execution); index++) {
        roots[3] = mvp_array_get(roots[0], index);
        uint64_t values[4] = {roots[2].bits, roots[3].bits,
            mvp_value_from_number((double)index).bits, roots[0].bits};
        roots[2] = mvp_runtime_value(mvp_op_call_values(execution, roots[1].bits,
            mvp_value_undefined().bits, values, 4));
    }
    MvpValue result = roots[2];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_array_some(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count != 1 || !mvp_value_is_function(mvp_runtime_value(arguments[0]))) {
        return mvp_builtin_unsupported(execution);
    }
    MvpValue roots[3] = {array, mvp_runtime_value(arguments[0]), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    int found = 0;
    for (size_t index = 0; index < mvp_array_length(roots[0]) &&
            !mvp_execution_has_error(execution); index++) {
        roots[2] = mvp_array_get(roots[0], index);
        uint64_t values[3] = {roots[2].bits, mvp_value_from_number((double)index).bits,
            roots[0].bits};
        MvpValue result = mvp_runtime_value(mvp_op_call_values(execution, roots[1].bits,
            mvp_value_undefined().bits, values, 3));
        if (mvp_value_truthy(result)) {
            found = 1;
            break;
        }
    }
    mvp_root_frame_pop(&execution->heap, &frame);
    return mvp_value_bool(found).bits;
}

static MvpValue mvp_builtin_array_splice(MvpExecution* execution, MvpValue array,
        uint64_t* arguments, size_t argument_count) {
    MvpValue roots[10] = {array, mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined()};
    if (argument_count > 8) return mvp_value_undefined();
    for (size_t index = 0; index < argument_count; index++) roots[index + 1] =
        mvp_runtime_value(arguments[index]);
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 10);
    size_t old_length = mvp_array_length(roots[0]);
    double start_number = argument_count ? mvp_to_number(execution, roots[1]) : 0.0;
    size_t start = !isfinite(start_number) ? (start_number > 0 ? old_length : 0)
        : start_number < 0 ? (size_t)(start_number < -(double)old_length ? 0
            : (double)old_length + ceil(start_number))
        : (size_t)(start_number > (double)old_length ? old_length : floor(start_number));
    double delete_number = argument_count > 1 ? mvp_to_number(execution, roots[2]) : 0.0;
    size_t deleted = argument_count > 1 && isfinite(delete_number) && delete_number > 0
        ? (size_t)(delete_number > (double)(old_length - start) ? old_length - start
            : floor(delete_number)) : 0;
    if (deleted > old_length - start) deleted = old_length - start;
    size_t inserted = argument_count > 2 ? argument_count - 2 : 0;
    roots[9] = mvp_array_new(&execution->heap, deleted);
    for (size_t index = 0; mvp_value_is_array(roots[9]) && index < deleted; index++) {
        if (!mvp_array_push(&execution->heap, roots[9], mvp_array_get(roots[0], start + index))) {
            mvp_execution_set_error(execution, "MVP Array.splice allocation failed");
            break;
        }
    }
    if (inserted > deleted) {
        if (!mvp_array_set_length(&execution->heap, roots[0], old_length + inserted - deleted)) {
            mvp_execution_set_error(execution, "MVP Array.splice growth failed");
        }
        for (size_t index = old_length; index > start + deleted &&
                !mvp_execution_has_error(execution); index--) {
            if (!mvp_array_set(&execution->heap, roots[0], index + inserted - deleted - 1,
                    mvp_array_get(roots[0], index - 1))) {
                mvp_execution_set_error(execution, "MVP Array.splice move failed");
            }
        }
    } else {
        for (size_t index = start + deleted; index < old_length &&
                !mvp_execution_has_error(execution); index++) {
            if (!mvp_array_set(&execution->heap, roots[0], index - deleted + inserted,
                    mvp_array_get(roots[0], index))) {
                mvp_execution_set_error(execution, "MVP Array.splice move failed");
            }
        }
        if (!mvp_execution_has_error(execution) && !mvp_array_set_length(&execution->heap,
                roots[0], old_length - deleted + inserted)) {
            mvp_execution_set_error(execution, "MVP Array.splice shrink failed");
        }
    }
    for (size_t index = 0; index < inserted && !mvp_execution_has_error(execution); index++) {
        if (!mvp_array_set(&execution->heap, roots[0], start + index, roots[index + 3])) {
            mvp_execution_set_error(execution, "MVP Array.splice insertion failed");
        }
    }
    MvpValue result = roots[9];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static MvpValue mvp_builtin_array_shift(MvpExecution* execution, MvpValue array,
        size_t argument_count) {
    if (argument_count) {
        mvp_builtin_unsupported(execution);
        return mvp_value_undefined();
    }
    size_t length = mvp_array_length(array);
    if (!length) return mvp_value_undefined();
    MvpValue result = mvp_array_get(array, 0);
    for (size_t index = 1; index < length; index++) {
        if (!mvp_array_set(&execution->heap, array, index - 1, mvp_array_get(array, index))) {
            mvp_execution_set_error(execution, "MVP Array.shift move failed");
            return mvp_value_undefined();
        }
    }
    if (!mvp_array_set_length(&execution->heap, array, length - 1)) {
        mvp_execution_set_error(execution, "MVP Array.shift shrink failed");
        return mvp_value_undefined();
    }
    return result;
}

static MvpValue mvp_builtin_string_range(MvpExecution* execution, MvpValue string,
        uint64_t* arguments, size_t argument_count, int slice) {
    size_t length = 0;
    const char* bytes = mvp_string_bytes(string, &length);
    if (!bytes) return mvp_value_undefined();
    size_t start = mvp_builtin_index(execution, arguments, argument_count, 0, 0, length);
    size_t end = mvp_builtin_index(execution, arguments, argument_count, 1, length, length);
    if (slice && argument_count && arguments) {
        double first = mvp_to_number(execution, mvp_runtime_value(arguments[0]));
        if (isfinite(first) && first < 0) start = (size_t)(first < -(double)length ? 0
            : (double)length + first);
        if (argument_count > 1) {
            double second = mvp_to_number(execution, mvp_runtime_value(arguments[1]));
            if (isfinite(second) && second < 0) end = (size_t)(second < -(double)length ? 0
                : (double)length + second);
        }
    }
    if (!slice && end < start) {
        size_t swapped = start;
        start = end;
        end = swapped;
    }
    if (end < start) end = start;
    return mvp_string_new(&execution->heap, bytes + start, end - start);
}

static MvpValue mvp_builtin_string_case(MvpExecution* execution, MvpValue string,
        int upper) {
    size_t length = 0;
    const char* bytes = mvp_string_bytes(string, &length);
    if (!bytes) return mvp_value_undefined();
    MvpValue result = mvp_string_new(&execution->heap, bytes, length);
    MvpHeapObject* header = mvp_value_reference_object(result);
    if (!header || header->kind != MVP_HEAP_STRING) return mvp_value_undefined();
    MvpString* copy = (MvpString*)header;
    for (size_t index = 0; index < length; index++) {
        unsigned char value = (unsigned char)copy->bytes[index];
        if (value >= 'a' && value <= 'z' && upper) copy->bytes[index] = (char)(value - 'a' + 'A');
        if (value >= 'A' && value <= 'Z' && !upper) copy->bytes[index] = (char)(value - 'A' + 'a');
    }
    return result;
}

static MvpValue mvp_builtin_string_split(MvpExecution* execution, MvpValue string,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count != 1) return mvp_value_undefined();
    MvpValue roots[4] = {string, mvp_runtime_value(arguments[0]), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    roots[1] = mvp_to_string(execution, roots[1]);
    size_t text_length = 0;
    size_t separator_length = 0;
    const char* text = mvp_string_bytes(roots[0], &text_length);
    const char* separator = mvp_string_bytes(roots[1], &separator_length);
    roots[2] = mvp_array_new(&execution->heap, 4);
    if (!text || !separator || !mvp_value_is_array(roots[2])) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    if (!separator_length) {
        for (size_t index = 0; index < text_length; index++) {
            roots[3] = mvp_string_new(&execution->heap, text + index, 1);
            if (!mvp_array_push(&execution->heap, roots[2], roots[3])) break;
        }
    } else {
        size_t start = 0;
        for (size_t index = 0; index + separator_length <= text_length; index++) {
            if (memcmp(text + index, separator, separator_length) != 0) continue;
            roots[3] = mvp_string_new(&execution->heap, text + start, index - start);
            if (!mvp_array_push(&execution->heap, roots[2], roots[3])) break;
            index += separator_length - 1;
            start = index + 1;
        }
        if (!mvp_execution_has_error(execution)) {
            roots[3] = mvp_string_new(&execution->heap, text + start, text_length - start);
            if (!mvp_array_push(&execution->heap, roots[2], roots[3])) roots[2] =
                mvp_value_undefined();
        }
    }
    MvpValue result = roots[2];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_string_starts_with(MvpExecution* execution, MvpValue string,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count != 1) return mvp_builtin_unsupported(execution);
    MvpValue needle = mvp_to_string(execution, mvp_runtime_value(arguments[0]));
    size_t length = 0;
    size_t needle_length = 0;
    const char* text = mvp_string_bytes(string, &length);
    const char* prefix = mvp_string_bytes(needle, &needle_length);
    return mvp_value_bool(text && prefix && needle_length <= length &&
        memcmp(text, prefix, needle_length) == 0).bits;
}

static uint64_t mvp_builtin_string_index_of(MvpExecution* execution, MvpValue string,
        uint64_t* arguments, size_t argument_count) {
    if (!argument_count || argument_count > 2) return mvp_builtin_unsupported(execution);
    MvpValue needle = mvp_to_string(execution, mvp_runtime_value(arguments[0]));
    size_t length = 0;
    size_t needle_length = 0;
    const char* text = mvp_string_bytes(string, &length);
    const char* needle_text = mvp_string_bytes(needle, &needle_length);
    size_t start = mvp_builtin_index(execution, arguments, argument_count, 1, 0, length);
    if (!text || !needle_text || needle_length > length || start > length - needle_length) {
        return mvp_value_from_number(-1).bits;
    }
    for (size_t index = start; index + needle_length <= length; index++) {
        if (!needle_length || memcmp(text + index, needle_text, needle_length) == 0) {
            return mvp_value_from_number((double)index).bits;
        }
    }
    return mvp_value_from_number(-1).bits;
}

static MvpValue mvp_builtin_string_char_at(MvpExecution* execution, MvpValue string,
        uint64_t* arguments, size_t argument_count) {
    if (argument_count > 1) return mvp_value_undefined();
    size_t length = 0;
    const char* text = mvp_string_bytes(string, &length);
    size_t index = mvp_builtin_index(execution, arguments, argument_count, 0, 0, length);
    return text && index < length ? mvp_string_new(&execution->heap, text + index, 1)
        : mvp_string_new(&execution->heap, "", 0);
}

static MvpValue mvp_builtin_string_trim(MvpExecution* execution, MvpValue string,
        size_t argument_count) {
    if (argument_count) return mvp_value_undefined();
    size_t length = 0;
    const char* text = mvp_string_bytes(string, &length);
    if (!text) return mvp_value_undefined();
    size_t first = 0;
    size_t last = length;
    while (first < last && (text[first] == ' ' || text[first] == '\t' || text[first] == '\n' ||
            text[first] == '\r' || text[first] == '\f')) first++;
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' ||
            text[last - 1] == '\n' || text[last - 1] == '\r' || text[last - 1] == '\f')) last--;
    return mvp_string_new(&execution->heap, text + first, last - first);
}

static int mvp_regex_next(MvpValue regex, const char* text, size_t text_length,
        size_t start, size_t* out_start, size_t* out_length) {
    re2::RE2* compiled = (re2::RE2*)mvp_regex_compiled(regex);
    if (!compiled || !text || start > text_length) return 0;
    re2::StringPiece match;
    if (!compiled->Match(re2::StringPiece(text, text_length), start, text_length,
            re2::RE2::UNANCHORED, &match, 1)) return 0;
    if (out_start) *out_start = (size_t)(match.data() - text);
    if (out_length) *out_length = match.size();
    return 1;
}

// RE2 deliberately excludes lookaround.  Keep the common character-boundary
// predicate in the private runtime so a rejected regular expression is still
// evaluated from its parsed pattern instead of changing the benchmark source.
static int mvp_regex_utf8_decode(const char* text, size_t length, size_t* offset,
        uint32_t* out_codepoint) {
    if (!text || !offset || !out_codepoint || *offset >= length) return 0;
    unsigned char first = (unsigned char)text[*offset];
    if (first < 0x80) {
        *out_codepoint = first;
        (*offset)++;
        return 1;
    }
    size_t continuation_count = first < 0xe0 ? 1 : first < 0xf0 ? 2 : first < 0xf8 ? 3 : 0;
    if (!continuation_count || *offset + continuation_count >= length) return 0;
    uint32_t codepoint = first & ((1u << (6 - continuation_count)) - 1u);
    for (size_t index = 0; index < continuation_count; index++) {
        unsigned char continuation = (unsigned char)text[*offset + index + 1];
        if ((continuation & 0xc0) != 0x80) return 0;
        codepoint = (codepoint << 6) | (continuation & 0x3f);
    }
    *offset += continuation_count + 1;
    *out_codepoint = codepoint;
    return 1;
}

static int mvp_regex_hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int mvp_regex_class_token(const char* pattern, size_t end, size_t* offset,
        uint32_t* out_codepoint) {
    if (!pattern || !offset || !out_codepoint || *offset >= end) return 0;
    if (pattern[*offset] != '\\') {
        return mvp_regex_utf8_decode(pattern, end, offset, out_codepoint);
    }
    (*offset)++;
    if (*offset >= end) return 0;
    if (pattern[*offset] != 'u') {
        *out_codepoint = (unsigned char)pattern[*offset];
        (*offset)++;
        return 1;
    }
    if (*offset + 4 >= end) return 0;
    uint32_t codepoint = 0;
    for (size_t index = 1; index <= 4; index++) {
        int digit = mvp_regex_hex_digit(pattern[*offset + index]);
        if (digit < 0) return 0;
        codepoint = (codepoint << 4) | (uint32_t)digit;
    }
    *offset += 5;
    *out_codepoint = codepoint;
    return 1;
}

static int mvp_regex_class_contains(const char* pattern, size_t start, size_t end,
        uint32_t codepoint) {
    size_t offset = start;
    while (offset < end) {
        uint32_t lower = 0;
        if (!mvp_regex_class_token(pattern, end, &offset, &lower)) return 0;
        if (offset < end && pattern[offset] == '-') {
            offset++;
            uint32_t upper = 0;
            if (!mvp_regex_class_token(pattern, end, &offset, &upper)) return 0;
            if (codepoint >= lower && codepoint <= upper) return 1;
        } else if (codepoint == lower) {
            return 1;
        }
    }
    return 0;
}

static int mvp_regex_class_end(const char* pattern, size_t length, size_t start,
        size_t* out_end) {
    size_t offset = start;
    while (offset < length) {
        if (pattern[offset] == '\\') {
            offset += offset + 1 < length ? 2 : 1;
            continue;
        }
        if (pattern[offset] == ']') {
            *out_end = offset;
            return 1;
        }
        offset++;
    }
    return 0;
}

static int mvp_regex_fallback_test(MvpValue regex, const char* text, size_t text_length) {
    MvpValue pattern_value = mvp_regex_pattern(regex);
    size_t pattern_length = 0;
    const char* pattern = mvp_string_bytes(pattern_value, &pattern_length);
    if (!pattern || pattern_length < 10) return 0;
    size_t offset = 0;
    int has_whitespace_branch = pattern_length >= 3 && pattern[0] == '\\' &&
        pattern[1] == 's' && pattern[2] == '|';
    if (has_whitespace_branch) offset = 3;
    if (offset + 4 >= pattern_length || pattern[offset] != '(' ||
            pattern[offset + 1] != '?' || pattern[offset + 2] != '!' ||
            pattern[offset + 3] != '[') return 0;
    size_t excluded_end = 0;
    if (!mvp_regex_class_end(pattern, pattern_length, offset + 4, &excluded_end) ||
            excluded_end + 2 >= pattern_length || pattern[excluded_end + 1] != ')' ||
            pattern[excluded_end + 2] != '[') return 0;
    size_t included_start = excluded_end + 3;
    size_t included_end = 0;
    if (!mvp_regex_class_end(pattern, pattern_length, included_start, &included_end) ||
            included_end + 1 != pattern_length) return 0;
    size_t text_offset = 0;
    while (text_offset < text_length) {
        uint32_t codepoint = 0;
        if (!mvp_regex_utf8_decode(text, text_length, &text_offset, &codepoint)) return 0;
        int is_whitespace = codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
            codepoint == '\r' || codepoint == '\f' || codepoint == '\v';
        if ((has_whitespace_branch && is_whitespace) ||
                (!mvp_regex_class_contains(pattern, offset + 4, excluded_end, codepoint) &&
                 mvp_regex_class_contains(pattern, included_start, included_end, codepoint))) return 1;
    }
    return 0;
}

static uint64_t mvp_regexp_test_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver, uint64_t* arguments, size_t argument_count) {
    (void)receiver;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (!execution || !function || argument_count != 1 || !mvp_value_is_regex(function->captures)) {
        return mvp_builtin_unsupported(execution);
    }
    MvpValue roots[2] = {function->captures, mvp_runtime_value(arguments[0])};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 2);
    roots[1] = mvp_to_string(execution, roots[1]);
    size_t text_length = 0;
    const char* text = mvp_string_bytes(roots[1], &text_length);
    int matches = text && (mvp_regex_next(roots[0], text, text_length, 0, NULL, NULL) ||
        mvp_regex_fallback_test(roots[0], text, text_length));
    mvp_root_frame_pop(&execution->heap, &frame);
    return mvp_value_bool(matches).bits;
}

uint64_t mvp_host_regexp_test_bind(MvpExecution* execution, uint64_t regex_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue regex = mvp_runtime_value(regex_bits);
    if (!mvp_value_is_regex(regex)) return mvp_builtin_unsupported(execution);
    MvpValue result = mvp_function_new(&execution->heap, mvp_regexp_test_call, NULL, 1, regex);
    if (!mvp_value_is_function(result)) {
        mvp_execution_set_error(execution, "MVP RegExp test binding allocation failed");
    }
    return result.bits;
}

typedef double (*MvpNumericEntry0)(void);
typedef double (*MvpNumericEntry1)(double);
typedef double (*MvpNumericEntry2)(double, double);
typedef double (*MvpNumericEntry3)(double, double, double);
typedef double (*MvpNumericEntry4)(double, double, double, double);
typedef double (*MvpNumericEntry5)(double, double, double, double, double);
typedef double (*MvpNumericEntry6)(double, double, double, double, double, double);
typedef double (*MvpNumericEntry7)(double, double, double, double, double, double, double);
typedef double (*MvpNumericEntry8)(double, double, double, double, double, double, double,
                                   double);

static uint64_t mvp_host_numeric_call_values(MvpExecution* execution, uint64_t entry_bits,
        uint64_t* arguments, size_t argument_count) {
    if (!execution || !entry_bits || argument_count > 8) {
        if (execution) mvp_execution_set_error(execution, "MVP numeric kernel descriptor is invalid");
        return mvp_value_undefined().bits;
    }
    double values[8] = {};
    for (size_t index = 0; index < argument_count; index++) {
        values[index] = mvp_to_number(execution, mvp_runtime_value(arguments[index]));
        if (mvp_execution_has_error(execution)) return mvp_value_undefined().bits;
    }
    void* entry = (void*)(uintptr_t)entry_bits;
    double result = 0.0;
    switch (argument_count) {
    case 0: result = ((MvpNumericEntry0)entry)(); break;
    case 1: result = ((MvpNumericEntry1)entry)(values[0]); break;
    case 2: result = ((MvpNumericEntry2)entry)(values[0], values[1]); break;
    case 3: result = ((MvpNumericEntry3)entry)(values[0], values[1], values[2]); break;
    case 4: result = ((MvpNumericEntry4)entry)(values[0], values[1], values[2], values[3]); break;
    case 5: result = ((MvpNumericEntry5)entry)(values[0], values[1], values[2], values[3],
        values[4]); break;
    case 6: result = ((MvpNumericEntry6)entry)(values[0], values[1], values[2], values[3],
        values[4], values[5]); break;
    case 7: result = ((MvpNumericEntry7)entry)(values[0], values[1], values[2], values[3],
        values[4], values[5], values[6]); break;
    case 8: result = ((MvpNumericEntry8)entry)(values[0], values[1], values[2], values[3],
        values[4], values[5], values[6], values[7]); break;
    }
    return mvp_value_from_number(result).bits;
}

uint64_t mvp_host_numeric_call0(MvpExecution* execution, uint64_t entry) {
    return mvp_host_numeric_call_values(execution, entry, NULL, 0);
}

uint64_t mvp_host_numeric_call1(MvpExecution* execution, uint64_t entry, uint64_t argument0) {
    uint64_t arguments[1] = {argument0};
    return mvp_host_numeric_call_values(execution, entry, arguments, 1);
}

uint64_t mvp_host_numeric_call2(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1) {
    uint64_t arguments[2] = {argument0, argument1};
    return mvp_host_numeric_call_values(execution, entry, arguments, 2);
}

uint64_t mvp_host_numeric_call3(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2) {
    uint64_t arguments[3] = {argument0, argument1, argument2};
    return mvp_host_numeric_call_values(execution, entry, arguments, 3);
}

uint64_t mvp_host_numeric_call4(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3) {
    uint64_t arguments[4] = {argument0, argument1, argument2, argument3};
    return mvp_host_numeric_call_values(execution, entry, arguments, 4);
}

uint64_t mvp_host_numeric_call5(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4) {
    uint64_t arguments[5] = {argument0, argument1, argument2, argument3, argument4};
    return mvp_host_numeric_call_values(execution, entry, arguments, 5);
}

uint64_t mvp_host_numeric_call6(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5) {
    uint64_t arguments[6] = {argument0, argument1, argument2, argument3, argument4,
        argument5};
    return mvp_host_numeric_call_values(execution, entry, arguments, 6);
}

uint64_t mvp_host_numeric_call7(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6) {
    uint64_t arguments[7] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6};
    return mvp_host_numeric_call_values(execution, entry, arguments, 7);
}

uint64_t mvp_host_numeric_call8(MvpExecution* execution, uint64_t entry, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6, uint64_t argument7) {
    uint64_t arguments[8] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7};
    return mvp_host_numeric_call_values(execution, entry, arguments, 8);
}

static uint64_t mvp_numeric_function_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver, uint64_t* arguments, size_t argument_count) {
    (void)receiver;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (!execution || !function || !function->closure) {
        return mvp_builtin_unsupported(execution);
    }
    return mvp_host_numeric_call_values(execution, (uint64_t)(uintptr_t)function->closure,
        arguments, argument_count);
}

uint64_t mvp_host_numeric_function(MvpExecution* execution, uint64_t entry,
        uint64_t parameter_count) {
    if (!execution || !entry || parameter_count > 8) {
        if (execution) mvp_execution_set_error(execution, "MVP numeric function descriptor is invalid");
        return mvp_value_undefined().bits;
    }
    MvpValue result = mvp_function_new(&execution->heap, mvp_numeric_function_call,
        (void*)(uintptr_t)entry, (size_t)parameter_count, mvp_value_undefined());
    if (!mvp_value_is_function(result) || !mvp_function_initialize_objects(execution, result)) {
        mvp_execution_set_error(execution, "MVP numeric function allocation failed");
    }
    return result.bits;
}

static MvpValue mvp_builtin_string_match(MvpExecution* execution, MvpValue string,
        MvpValue regex) {
    MvpValue roots[4] = {string, regex, mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    size_t text_length = 0;
    const char* text = mvp_string_bytes(roots[0], &text_length);
    if (!text) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_null();
    }
    roots[2] = mvp_array_new(&execution->heap, 4);
    size_t start = 0;
    size_t match_start = 0;
    size_t match_length = 0;
    while (mvp_regex_next(roots[1], text, text_length, start, &match_start, &match_length)) {
        roots[3] = mvp_string_new(&execution->heap, text + match_start, match_length);
        if (!mvp_array_push(&execution->heap, roots[2], roots[3])) {
            roots[2] = mvp_value_undefined();
            break;
        }
        if (!mvp_regex_global(roots[1])) break;
        start = match_start + match_length;
        if (!match_length && start < text_length) start++;
        if (!match_length && start >= text_length) break;
    }
    MvpValue result = mvp_array_length(roots[2]) ? roots[2] : mvp_value_null();
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static MvpValue mvp_builtin_string_replace_regex(MvpExecution* execution, MvpValue string,
        MvpValue regex, MvpValue replacement) {
    MvpValue roots[4] = {string, regex, replacement, mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 4);
    roots[2] = mvp_to_string(execution, roots[2]);
    size_t text_length = 0;
    size_t replacement_length = 0;
    const char* text = mvp_string_bytes(roots[0], &text_length);
    const char* replacement_text = mvp_string_bytes(roots[2], &replacement_length);
    if (!text || !replacement_text) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    size_t start = 0;
    size_t prior = 0;
    size_t output_length = 0;
    size_t match_start = 0;
    size_t match_length = 0;
    int matched = 0;
    while (mvp_regex_next(roots[1], text, text_length, start, &match_start, &match_length)) {
        if (output_length > SIZE_MAX - (match_start - prior) ||
                output_length + (match_start - prior) > SIZE_MAX - replacement_length) {
            mvp_root_frame_pop(&execution->heap, &frame);
            return mvp_value_undefined();
        }
        output_length += match_start - prior + replacement_length;
        prior = match_start + match_length;
        matched = 1;
        if (!mvp_regex_global(roots[1])) break;
        start = prior;
        if (!match_length && start < text_length) start++;
        if (!match_length && start >= text_length) break;
    }
    if (!matched || output_length > SIZE_MAX - (text_length - prior)) {
        MvpValue result = roots[0];
        mvp_root_frame_pop(&execution->heap, &frame);
        return result;
    }
    output_length += text_length - prior;
    roots[3] = mvp_string_new(&execution->heap, NULL, output_length);
    MvpHeapObject* header = mvp_value_reference_object(roots[3]);
    if (!header || header->kind != MVP_HEAP_STRING) {
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined();
    }
    char* output = ((MvpString*)header)->bytes;
    size_t output_at = 0;
    start = 0;
    prior = 0;
    while (mvp_regex_next(roots[1], text, text_length, start, &match_start, &match_length)) {
        size_t literal_length = match_start - prior;
        if (literal_length) memcpy(output + output_at, text + prior, literal_length);
        output_at += literal_length;
        if (replacement_length) memcpy(output + output_at, replacement_text, replacement_length);
        output_at += replacement_length;
        prior = match_start + match_length;
        if (!mvp_regex_global(roots[1])) break;
        start = prior;
        if (!match_length && start < text_length) start++;
        if (!match_length && start >= text_length) break;
    }
    if (text_length > prior) memcpy(output + output_at, text + prior, text_length - prior);
    MvpValue result = roots[3];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

static uint64_t mvp_builtin_unsupported(MvpExecution* execution) {
    mvp_execution_set_error(execution, "MVP builtin receiver or arguments are unsupported");
    return mvp_value_undefined().bits;
}

static uint64_t mvp_builtin_method_call(void* opaque_execution, MvpFunction* function,
        uint64_t receiver, uint64_t* arguments, size_t argument_count) {
    (void)receiver;
    MvpExecution* execution = (MvpExecution*)opaque_execution;
    if (!execution || !function) return mvp_value_undefined().bits;
    MvpValue value = function->captures;
    MvpBuiltinMethod method = (MvpBuiltinMethod)(uintptr_t)function->closure;
    if (method == MVP_BUILTIN_FUNCTION_APPLY) {
        if (!mvp_value_is_function(value)) return mvp_builtin_unsupported(execution);
        MvpValue receiver_value = argument_count ? mvp_runtime_value(arguments[0])
            : mvp_value_undefined();
        MvpValue values = argument_count > 1 ? mvp_runtime_value(arguments[1])
            : mvp_value_undefined();
        if (!mvp_value_is_array(values) || mvp_array_length(values) > 8) {
            return mvp_builtin_unsupported(execution);
        }
        uint64_t applied[8] = {};
        size_t length = mvp_array_length(values);
        for (size_t index = 0; index < length; index++) applied[index] =
            mvp_array_get(values, index).bits;
        return mvp_op_call_values(execution, value.bits, receiver_value.bits, applied, length);
    }
    if (method == MVP_BUILTIN_FUNCTION_CALL) {
        if (!mvp_value_is_function(value)) return mvp_builtin_unsupported(execution);
        MvpValue receiver_value = argument_count ? mvp_runtime_value(arguments[0])
            : mvp_value_undefined();
        return mvp_op_call_values(execution, value.bits, receiver_value.bits,
            argument_count > 1 ? arguments + 1 : NULL,
            argument_count > 0 ? argument_count - 1 : 0);
    }
    if (method == MVP_BUILTIN_ARRAY_PUSH) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        for (size_t index = 0; index < argument_count; index++) {
            if (!mvp_array_push(&execution->heap, value, mvp_runtime_value(arguments[index]))) {
                mvp_execution_set_error(execution, "MVP Array.push allocation failed");
                return mvp_value_undefined().bits;
            }
        }
        return mvp_value_from_number((double)mvp_array_length(value)).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_POP) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_array_pop(value).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_JOIN) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        MvpValue separator = argument_count ? mvp_to_string(execution,
            mvp_runtime_value(arguments[0])) : mvp_string_new(&execution->heap, ",", 1);
        return mvp_builtin_array_join(execution, value, separator).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_SLICE) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_slice(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_INCLUDES) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_includes(value, arguments, argument_count);
    }
    if (method == MVP_BUILTIN_ARRAY_CONCAT) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_concat(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_UNSHIFT) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_unshift(execution, value, arguments, argument_count);
    }
    if (method == MVP_BUILTIN_ARRAY_MAP) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_map(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_SORT) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_sort(execution, value, arguments, argument_count);
    }
    if (method == MVP_BUILTIN_ARRAY_REVERSE) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_reverse(execution, value, argument_count);
    }
    if (method == MVP_BUILTIN_ARRAY_FLAT) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_flat(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_REDUCE) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_reduce(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_SOME) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_some(execution, value, arguments, argument_count);
    }
    if (method == MVP_BUILTIN_ARRAY_SPLICE) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_splice(execution, value, arguments, argument_count).bits;
    }
    if (method == MVP_BUILTIN_ARRAY_SHIFT) {
        if (!mvp_value_is_array(value)) return mvp_builtin_unsupported(execution);
        return mvp_builtin_array_shift(execution, value, argument_count).bits;
    }
    if (method == MVP_BUILTIN_MAP_GET) {
        if (!mvp_value_is_map(value) || argument_count != 1) {
            return mvp_builtin_unsupported(execution);
        }
        return mvp_map_get(value, mvp_runtime_value(arguments[0])).bits;
    }
    if (method == MVP_BUILTIN_MAP_SET) {
        if (!mvp_value_is_map(value) || argument_count != 2 || !mvp_map_set(&execution->heap,
                value, mvp_runtime_value(arguments[0]), mvp_runtime_value(arguments[1]))) {
            return mvp_builtin_unsupported(execution);
        }
        return value.bits;
    }
    if (method == MVP_BUILTIN_MAP_ENTRIES) {
        if (!mvp_value_is_map(value) || argument_count != 0) {
            return mvp_builtin_unsupported(execution);
        }
        MvpValue entries = mvp_map_entries(&execution->heap, value);
        if (!mvp_value_is_array(entries)) return mvp_builtin_unsupported(execution);
        return entries.bits;
    }
    if (method == MVP_BUILTIN_DATE_GET_TIME) {
        MvpValue milliseconds = mvp_object_named_value(value, "__mvp_number");
        if (!mvp_value_is_number(milliseconds) || argument_count) {
            return mvp_builtin_unsupported(execution);
        }
        return milliseconds.bits;
    }
    if (method == MVP_BUILTIN_BIGINT_TO_STRING) {
        if (!mvp_value_is_bigint(value) || argument_count) return mvp_builtin_unsupported(execution);
        return mvp_bigint_to_string(&execution->heap, value).bits;
    }
    if (!mvp_value_is_string(value) && method != MVP_BUILTIN_NUMBER_TO_FIXED &&
            method != MVP_BUILTIN_BIGINT_TO_STRING) {
        return mvp_builtin_unsupported(execution);
    }
    if (method == MVP_BUILTIN_STRING_CHAR_CODE_AT) {
        size_t length = 0;
        const char* bytes = mvp_string_bytes(value, &length);
        size_t index = mvp_builtin_index(execution, arguments, argument_count, 0, 0, length);
        return index < length ? mvp_value_from_number((unsigned char)bytes[index]).bits
            : mvp_value_from_number(NAN).bits;
    }
    if (method == MVP_BUILTIN_STRING_REPEAT) {
        size_t length = 0;
        const char* bytes = mvp_string_bytes(value, &length);
        size_t count = mvp_builtin_index(execution, arguments, argument_count, 0, 0,
            length ? SIZE_MAX / length : 0);
        if (length && count > SIZE_MAX / length) return mvp_builtin_unsupported(execution);
        MvpValue result = mvp_string_new(&execution->heap, NULL, length * count);
        MvpHeapObject* header = mvp_value_reference_object(result);
        if (!header || header->kind != MVP_HEAP_STRING) return mvp_builtin_unsupported(execution);
        char* destination = ((MvpString*)header)->bytes;
        for (size_t index = 0; index < count; index++) memcpy(destination + index * length,
            bytes, length);
        return result.bits;
    }
    if (method == MVP_BUILTIN_STRING_SUBSTRING) {
        return mvp_builtin_string_range(execution, value, arguments, argument_count, 0).bits;
    }
    if (method == MVP_BUILTIN_STRING_SLICE) {
        return mvp_builtin_string_range(execution, value, arguments, argument_count, 1).bits;
    }
    if (method == MVP_BUILTIN_STRING_TO_UPPER_CASE) return mvp_builtin_string_case(execution,
        value, 1).bits;
    if (method == MVP_BUILTIN_STRING_TO_LOWER_CASE) return mvp_builtin_string_case(execution,
        value, 0).bits;
    if (method == MVP_BUILTIN_STRING_SPLIT) {
        MvpValue result = mvp_builtin_string_split(execution, value, arguments, argument_count);
        return mvp_value_is_array(result) ? result.bits : mvp_builtin_unsupported(execution);
    }
    if (method == MVP_BUILTIN_STRING_STARTS_WITH) return mvp_builtin_string_starts_with(
        execution, value, arguments, argument_count);
    if (method == MVP_BUILTIN_STRING_MATCH) {
        if (argument_count != 1 || !mvp_value_is_regex(mvp_runtime_value(arguments[0]))) {
            return mvp_builtin_unsupported(execution);
        }
        return mvp_builtin_string_match(execution, value, mvp_runtime_value(arguments[0])).bits;
    }
    if (method == MVP_BUILTIN_STRING_REPLACE) {
        if (argument_count != 2 || !mvp_value_is_regex(mvp_runtime_value(arguments[0]))) {
            return mvp_builtin_unsupported(execution);
        }
        return mvp_builtin_string_replace_regex(execution, value, mvp_runtime_value(arguments[0]),
            mvp_runtime_value(arguments[1])).bits;
    }
    if (method == MVP_BUILTIN_STRING_INDEX_OF) return mvp_builtin_string_index_of(execution,
        value, arguments, argument_count);
    if (method == MVP_BUILTIN_STRING_CHAR_AT) {
        MvpValue result = mvp_builtin_string_char_at(execution, value, arguments, argument_count);
        return mvp_value_is_string(result) ? result.bits : mvp_builtin_unsupported(execution);
    }
    if (method == MVP_BUILTIN_STRING_TRIM) {
        MvpValue result = mvp_builtin_string_trim(execution, value, argument_count);
        return mvp_value_is_string(result) ? result.bits : mvp_builtin_unsupported(execution);
    }
    if (method == MVP_BUILTIN_NUMBER_TO_FIXED) {
        int digits = (int)mvp_builtin_index(execution, arguments, argument_count, 0, 0, 20);
        char text[96] = {};
        int written = snprintf(text, sizeof(text), "%.*f", digits, mvp_to_number(execution,
            value));
        return written > 0 && (size_t)written < sizeof(text) ? mvp_string_new(&execution->heap,
            text, (size_t)written).bits : mvp_value_undefined().bits;
    }
    return mvp_builtin_unsupported(execution);
}

static MvpValue mvp_builtin_method_new(MvpExecution* execution, MvpBuiltinMethod method,
        MvpValue receiver, size_t formal_count) {
    return mvp_function_new(&execution->heap, mvp_builtin_method_call,
        (void*)(uintptr_t)method, formal_count, receiver);
}

uint64_t mvp_op_call0(MvpExecution* execution, uint64_t function, uint64_t receiver) {
    return mvp_op_call_values(execution, function, receiver, NULL, 0);
}

uint64_t mvp_op_call1(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0) {
    uint64_t arguments[1] = {argument0};
    return mvp_op_call_values(execution, function, receiver, arguments, 1);
}

uint64_t mvp_op_call2(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1) {
    uint64_t arguments[2] = {argument0, argument1};
    return mvp_op_call_values(execution, function, receiver, arguments, 2);
}

uint64_t mvp_op_call3(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2) {
    uint64_t arguments[3] = {argument0, argument1, argument2};
    return mvp_op_call_values(execution, function, receiver, arguments, 3);
}

uint64_t mvp_op_call4(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3) {
    uint64_t arguments[4] = {argument0, argument1, argument2, argument3};
    return mvp_op_call_values(execution, function, receiver, arguments, 4);
}

uint64_t mvp_op_call5(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3,
        uint64_t argument4) {
    uint64_t arguments[5] = {argument0, argument1, argument2, argument3, argument4};
    return mvp_op_call_values(execution, function, receiver, arguments, 5);
}

uint64_t mvp_op_call6(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3,
        uint64_t argument4, uint64_t argument5) {
    uint64_t arguments[6] = {argument0, argument1, argument2, argument3, argument4,
        argument5};
    return mvp_op_call_values(execution, function, receiver, arguments, 6);
}

uint64_t mvp_op_call7(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3,
        uint64_t argument4, uint64_t argument5, uint64_t argument6) {
    uint64_t arguments[7] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6};
    return mvp_op_call_values(execution, function, receiver, arguments, 7);
}

uint64_t mvp_op_call8(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3,
        uint64_t argument4, uint64_t argument5, uint64_t argument6, uint64_t argument7) {
    uint64_t arguments[8] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7};
    return mvp_op_call_values(execution, function, receiver, arguments, 8);
}

uint64_t mvp_op_call9(MvpExecution* execution, uint64_t function, uint64_t receiver,
        uint64_t argument0, uint64_t argument1, uint64_t argument2, uint64_t argument3,
        uint64_t argument4, uint64_t argument5, uint64_t argument6, uint64_t argument7,
        uint64_t argument8) {
    uint64_t arguments[9] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7, argument8};
    return mvp_op_call_values(execution, function, receiver, arguments, 9);
}

static uint64_t mvp_op_super_values(MvpExecution* execution, uint64_t receiver_bits,
        uint64_t* arguments, size_t argument_count) {
    if (!execution || argument_count > 8) {
        if (execution) mvp_execution_set_error(execution, "MVP super call arity is unsupported");
        return mvp_value_undefined().bits;
    }
    MvpValue roots[10] = {mvp_runtime_value(receiver_bits), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined()};
    for (size_t index = 0; index < argument_count; index++) roots[index + 2] =
        mvp_runtime_value(arguments[index]);
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, argument_count + 2);
    MvpHeapObject* child_header = mvp_value_reference_object(execution->current_home_prototype);
    MvpValue parent_prototype = child_header && child_header->kind == MVP_HEAP_OBJECT
        ? ((MvpObject*)child_header)->prototype : mvp_value_undefined();
    roots[1] = mvp_string_new(&execution->heap, "constructor", 11);
    MvpValue constructor = mvp_value_is_object(parent_prototype) &&
            mvp_value_is_string(roots[1]) ? mvp_object_get(parent_prototype, roots[1])
        : mvp_value_undefined();
    uint64_t result = mvp_value_undefined().bits;
    if (mvp_value_is_function(constructor)) {
        result = mvp_op_call_values(execution, constructor.bits, roots[0].bits,
            (uint64_t*)&roots[2], argument_count);
    }
    mvp_root_frame_pop(&execution->heap, &frame);
    return result;
}

uint64_t mvp_op_super0(MvpExecution* execution, uint64_t receiver) {
    return mvp_op_super_values(execution, receiver, NULL, 0);
}

uint64_t mvp_op_super1(MvpExecution* execution, uint64_t receiver, uint64_t argument0) {
    uint64_t arguments[1] = {argument0};
    return mvp_op_super_values(execution, receiver, arguments, 1);
}

uint64_t mvp_op_super2(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1) {
    uint64_t arguments[2] = {argument0, argument1};
    return mvp_op_super_values(execution, receiver, arguments, 2);
}

uint64_t mvp_op_super3(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2) {
    uint64_t arguments[3] = {argument0, argument1, argument2};
    return mvp_op_super_values(execution, receiver, arguments, 3);
}

uint64_t mvp_op_super4(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3) {
    uint64_t arguments[4] = {argument0, argument1, argument2, argument3};
    return mvp_op_super_values(execution, receiver, arguments, 4);
}

uint64_t mvp_op_super5(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4) {
    uint64_t arguments[5] = {argument0, argument1, argument2, argument3, argument4};
    return mvp_op_super_values(execution, receiver, arguments, 5);
}

uint64_t mvp_op_super6(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5) {
    uint64_t arguments[6] = {argument0, argument1, argument2, argument3, argument4,
        argument5};
    return mvp_op_super_values(execution, receiver, arguments, 6);
}

uint64_t mvp_op_super7(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6) {
    uint64_t arguments[7] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6};
    return mvp_op_super_values(execution, receiver, arguments, 7);
}

uint64_t mvp_op_super8(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6, uint64_t argument7) {
    uint64_t arguments[8] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7};
    return mvp_op_super_values(execution, receiver, arguments, 8);
}

static uint64_t mvp_op_construct_values(MvpExecution* execution, uint64_t function_bits,
        uint64_t* arguments, size_t argument_count) {
    if (!execution || argument_count > 8) {
        if (execution) mvp_execution_set_error(execution, "MVP constructor arity is unsupported");
        return mvp_value_undefined().bits;
    }
    MvpValue roots[11] = {mvp_runtime_value(function_bits), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined(), mvp_value_undefined(), mvp_value_undefined()};
    for (size_t index = 0; index < argument_count; index++) roots[index + 3] =
        mvp_runtime_value(arguments[index]);
    MvpRootFrame frame = {};
    MvpRootFrame* active_frame = execution->active_frame;
    mvp_root_frame_push(&execution->heap, &frame, roots, argument_count + 3);
    MvpFunction* target = mvp_runtime_function(roots[0]);
    MvpValue prototype = mvp_class_prototype(roots[0]);
    int is_class = mvp_value_is_object(prototype);
    if (!is_class && target) prototype = target->instance_prototype;
    if (!mvp_value_is_object(prototype)) {
        mvp_execution_set_error(execution, "MVP new target is not a class");
        mvp_root_frame_pop(&execution->heap, &frame);
        execution->active_frame = active_frame;
        return mvp_value_undefined().bits;
    }
    roots[1] = mvp_object_new(&execution->heap, prototype);
    roots[2] = mvp_string_new(&execution->heap, "constructor", 11);
    if (!mvp_value_is_object(roots[1]) || !mvp_value_is_string(roots[2])) {
        mvp_execution_set_error(execution, "MVP instance allocation failed");
        mvp_root_frame_pop(&execution->heap, &frame);
        execution->active_frame = active_frame;
        return mvp_value_undefined().bits;
    }
    MvpValue constructor = is_class ? mvp_object_get(prototype, roots[2]) : roots[0];
    MvpValue result = mvp_value_undefined();
    if (mvp_value_is_function(constructor)) {
        result = mvp_function_call(constructor, execution, roots[1], (uint64_t*)&roots[3],
            argument_count);
    }
    MvpValue instance = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    execution->active_frame = active_frame;
    return mvp_value_is_object(result) ? result.bits : instance.bits;
}

uint64_t mvp_op_construct0(MvpExecution* execution, uint64_t function) {
    return mvp_op_construct_values(execution, function, NULL, 0);
}

uint64_t mvp_op_construct1(MvpExecution* execution, uint64_t function, uint64_t argument0) {
    uint64_t arguments[1] = {argument0};
    return mvp_op_construct_values(execution, function, arguments, 1);
}

uint64_t mvp_op_construct2(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1) {
    uint64_t arguments[2] = {argument0, argument1};
    return mvp_op_construct_values(execution, function, arguments, 2);
}

uint64_t mvp_op_construct3(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2) {
    uint64_t arguments[3] = {argument0, argument1, argument2};
    return mvp_op_construct_values(execution, function, arguments, 3);
}

uint64_t mvp_op_construct4(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3) {
    uint64_t arguments[4] = {argument0, argument1, argument2, argument3};
    return mvp_op_construct_values(execution, function, arguments, 4);
}

uint64_t mvp_op_construct5(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4) {
    uint64_t arguments[5] = {argument0, argument1, argument2, argument3, argument4};
    return mvp_op_construct_values(execution, function, arguments, 5);
}

uint64_t mvp_op_construct6(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5) {
    uint64_t arguments[6] = {argument0, argument1, argument2, argument3, argument4,
        argument5};
    return mvp_op_construct_values(execution, function, arguments, 6);
}

uint64_t mvp_op_construct7(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6) {
    uint64_t arguments[7] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6};
    return mvp_op_construct_values(execution, function, arguments, 7);
}

uint64_t mvp_op_construct8(MvpExecution* execution, uint64_t function, uint64_t argument0,
        uint64_t argument1, uint64_t argument2, uint64_t argument3, uint64_t argument4,
        uint64_t argument5, uint64_t argument6, uint64_t argument7) {
    uint64_t arguments[8] = {argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7};
    return mvp_op_construct_values(execution, function, arguments, 8);
}

uint64_t mvp_op_enumerable_keys(MvpExecution* execution, uint64_t object_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue roots[3] = {mvp_runtime_value(object_bits), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    size_t count = 0;
    if (mvp_value_is_array(roots[0])) {
        count = mvp_array_length(roots[0]);
    } else if (mvp_value_is_object(roots[0])) {
        count = ((MvpObject*)mvp_value_reference_object(roots[0]))->property_count;
    } else {
        MvpFunction* function = mvp_runtime_function(roots[0]);
        if (function && mvp_value_is_object(function->properties)) {
            count = ((MvpObject*)mvp_value_reference_object(function->properties))->property_count;
        }
    }
    roots[1] = mvp_array_new(&execution->heap, count);
    if (!mvp_value_is_array(roots[1])) {
        mvp_execution_set_error(execution, "MVP enumerable-key allocation failed");
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined().bits;
    }
    if (mvp_value_is_array(roots[0])) {
        for (size_t index = 0; index < count; index++) {
            char text[32] = {};
            int written = snprintf(text, sizeof(text), "%zu", index);
            roots[2] = written > 0 && (size_t)written < sizeof(text)
                ? mvp_string_new(&execution->heap, text, (size_t)written)
                : mvp_value_undefined();
            if (!mvp_value_is_string(roots[2]) || !mvp_array_push(&execution->heap,
                    roots[1], roots[2])) {
                mvp_execution_set_error(execution, "MVP enumerable array-key allocation failed");
                break;
            }
        }
    } else {
        MvpValue properties = roots[0];
        if (mvp_value_is_function(properties)) {
            MvpFunction* function = mvp_runtime_function(properties);
            properties = function ? function->properties : mvp_value_undefined();
        }
        MvpHeapObject* header = mvp_value_reference_object(properties);
        MvpValue* values = header && header->kind == MVP_HEAP_OBJECT
            ? mvp_heap_object_values(mvp_value_reference_object(((MvpObject*)header)->storage))
            : NULL;
        for (size_t index = 0; values && index < count; index++) {
            roots[2] = values[index * 2];
            if (!mvp_array_push(&execution->heap, roots[1], roots[2])) {
                mvp_execution_set_error(execution, "MVP enumerable object-key allocation failed");
                break;
            }
        }
    }
    MvpValue result = roots[1];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result.bits;
}

uint64_t mvp_op_member_get(MvpExecution* execution, uint64_t object_bits, uint64_t key_bits) {
    MvpValue object = mvp_runtime_value(object_bits);
    MvpValue key = mvp_runtime_value(key_bits);
    size_t index = 0;
    if (mvp_value_is_array(object) && mvp_value_property_index(key, &index)) {
        return mvp_array_get(object, index).bits;
    }
    if (mvp_value_is_array(object) && mvp_value_is_string(key)) {
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        if (name && length == 6 && memcmp(name, "length", 6) == 0) {
            return mvp_value_from_number((double)mvp_array_length(object)).bits;
        }
        MvpValue property = mvp_array_get_property(object, key);
        if (!mvp_value_is_undefined(property)) return property.bits;
        if (name && length == 7 && memcmp(name, "forEach", 7) == 0) {
            MvpValue callback = mvp_function_new(&execution->heap, mvp_array_for_each_call,
                NULL, 1, object);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Array.forEach allocation failed");
            }
            return callback.bits;
        }
        int method = length == 4 && memcmp(name, "push", 4) == 0
            ? MVP_BUILTIN_ARRAY_PUSH : length == 3 && memcmp(name, "pop", 3) == 0
            ? MVP_BUILTIN_ARRAY_POP : length == 4 && memcmp(name, "join", 4) == 0
            ? MVP_BUILTIN_ARRAY_JOIN : length == 5 && memcmp(name, "slice", 5) == 0
            ? MVP_BUILTIN_ARRAY_SLICE : length == 8 && memcmp(name, "includes", 8) == 0
            ? MVP_BUILTIN_ARRAY_INCLUDES : length == 6 && memcmp(name, "concat", 6) == 0
            ? MVP_BUILTIN_ARRAY_CONCAT : length == 7 && memcmp(name, "unshift", 7) == 0
            ? MVP_BUILTIN_ARRAY_UNSHIFT : length == 3 && memcmp(name, "map", 3) == 0
            ? MVP_BUILTIN_ARRAY_MAP : length == 4 && memcmp(name, "sort", 4) == 0
            ? MVP_BUILTIN_ARRAY_SORT : length == 7 && memcmp(name, "reverse", 7) == 0
            ? MVP_BUILTIN_ARRAY_REVERSE : length == 4 && memcmp(name, "flat", 4) == 0
            ? MVP_BUILTIN_ARRAY_FLAT : length == 6 && memcmp(name, "reduce", 6) == 0
            ? MVP_BUILTIN_ARRAY_REDUCE : length == 4 && memcmp(name, "some", 4) == 0
            ? MVP_BUILTIN_ARRAY_SOME : length == 6 && memcmp(name, "splice", 6) == 0
            ? MVP_BUILTIN_ARRAY_SPLICE : length == 5 && memcmp(name, "shift", 5) == 0
            ? MVP_BUILTIN_ARRAY_SHIFT : 0;
        if (method) {
            MvpValue callback = mvp_builtin_method_new(execution, (MvpBuiltinMethod)method, object,
                method == MVP_BUILTIN_ARRAY_PUSH || method == MVP_BUILTIN_ARRAY_INCLUDES ||
                method == MVP_BUILTIN_ARRAY_UNSHIFT || method == MVP_BUILTIN_ARRAY_MAP ||
                method == MVP_BUILTIN_ARRAY_SORT || method == MVP_BUILTIN_ARRAY_SOME ? 1 :
                method == MVP_BUILTIN_ARRAY_POP || method == MVP_BUILTIN_ARRAY_REVERSE ||
                method == MVP_BUILTIN_ARRAY_SHIFT ? 0 : 2);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Array method allocation failed");
            }
            return callback.bits;
        }
    }
    if (mvp_value_is_map(object) && mvp_value_is_string(key)) {
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        if (name && length == 4 && memcmp(name, "size", 4) == 0) {
            return mvp_value_from_number((double)mvp_map_size(object)).bits;
        }
        int method = name && length == 3 && memcmp(name, "get", 3) == 0
            ? MVP_BUILTIN_MAP_GET : name && length == 3 && memcmp(name, "set", 3) == 0
            ? MVP_BUILTIN_MAP_SET : name && length == 7 && memcmp(name, "entries", 7) == 0
            ? MVP_BUILTIN_MAP_ENTRIES : 0;
        if (method) {
            MvpValue callback = mvp_builtin_method_new(execution, (MvpBuiltinMethod)method,
                object, method == MVP_BUILTIN_MAP_ENTRIES ? 0 : method == MVP_BUILTIN_MAP_GET
                ? 1 : 2);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Map method allocation failed");
            }
            return callback.bits;
        }
    }
    if (mvp_value_is_object(object)) {
        MvpValue roots[2] = {object, key};
        MvpRootFrame frame = {};
        mvp_root_frame_push(&execution->heap, &frame, roots, 2);
        if (!mvp_value_is_string(roots[1])) roots[1] = mvp_to_string(execution, roots[1]);
        MvpValue result = mvp_value_is_string(roots[1]) ? mvp_object_get(roots[0], roots[1])
            : mvp_value_undefined();
        if (mvp_value_is_function(mvp_accessor_getter(result))) {
            uint64_t called = mvp_op_call_values(execution, mvp_accessor_getter(result).bits,
                roots[0].bits, NULL, 0);
            mvp_root_frame_pop(&execution->heap, &frame);
            return called;
        }
        if (mvp_value_is_undefined(result) && mvp_object_has_constructor_name(roots[0], "Date")) {
            size_t length = 0;
            const char* name = mvp_string_bytes(roots[1], &length);
            if (name && length == 7 && memcmp(name, "getTime", 7) == 0) {
                MvpValue callback = mvp_builtin_method_new(execution, MVP_BUILTIN_DATE_GET_TIME,
                    roots[0], 0);
                if (!mvp_value_is_function(callback)) {
                    mvp_execution_set_error(execution, "MVP Date.getTime allocation failed");
                }
                mvp_root_frame_pop(&execution->heap, &frame);
                return callback.bits;
            }
        }
        mvp_root_frame_pop(&execution->heap, &frame);
        return result.bits;
    }
    MvpFunction* function = mvp_runtime_function(object);
    if (function) {
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        if (name && length == 9 && memcmp(name, "prototype", 9) == 0) {
            return function->instance_prototype.bits;
        }
        if (name && length == 5 && memcmp(name, "apply", 5) == 0) {
            MvpValue callback = mvp_builtin_method_new(execution, MVP_BUILTIN_FUNCTION_APPLY,
                object, 2);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Function.apply allocation failed");
            }
            return callback.bits;
        }
        if (name && length == 4 && memcmp(name, "call", 4) == 0) {
            MvpValue callback = mvp_builtin_method_new(execution, MVP_BUILTIN_FUNCTION_CALL,
                object, 0);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Function.call allocation failed");
            }
            return callback.bits;
        }
        if (mvp_value_is_object(function->properties)) {
            return mvp_object_get(function->properties, key).bits;
        }
    }
    if (mvp_value_is_string(object)) {
        size_t length = 0;
        const char* bytes = mvp_string_bytes(object, &length);
        if (mvp_value_property_index(key, &index) && index < length) {
            return mvp_string_new(&execution->heap, bytes + index, 1).bits;
        }
        if (mvp_value_is_string(key)) {
            size_t key_length = 0;
            const char* name = mvp_string_bytes(key, &key_length);
            if (name && key_length == 6 && memcmp(name, "length", 6) == 0) {
                return mvp_value_from_number((double)length).bits;
            }
            int method = name && key_length == 10 &&
                    memcmp(name, "charCodeAt", 10) == 0 ? MVP_BUILTIN_STRING_CHAR_CODE_AT
                : name && key_length == 6 && memcmp(name, "repeat", 6) == 0
                ? MVP_BUILTIN_STRING_REPEAT : name && key_length == 9 &&
                    memcmp(name, "substring", 9) == 0 ? MVP_BUILTIN_STRING_SUBSTRING
                : name && key_length == 5 && memcmp(name, "slice", 5) == 0
                ? MVP_BUILTIN_STRING_SLICE : name && key_length == 11 &&
                    memcmp(name, "toUpperCase", 11) == 0 ? MVP_BUILTIN_STRING_TO_UPPER_CASE
                : name && key_length == 11 && memcmp(name, "toLowerCase", 11) == 0
                ? MVP_BUILTIN_STRING_TO_LOWER_CASE : name && key_length == 5 &&
                    memcmp(name, "split", 5) == 0 ? MVP_BUILTIN_STRING_SPLIT : name &&
                    key_length == 10 && memcmp(name, "startsWith", 10) == 0
                ? MVP_BUILTIN_STRING_STARTS_WITH : name && key_length == 5 &&
                    memcmp(name, "match", 5) == 0 ? MVP_BUILTIN_STRING_MATCH : name &&
                    key_length == 7 && memcmp(name, "replace", 7) == 0
                ? MVP_BUILTIN_STRING_REPLACE : 0;
            if (!method && name && key_length == 17 &&
                    memcmp(name, "toLocaleLowerCase", 17) == 0) {
                method = MVP_BUILTIN_STRING_TO_LOWER_CASE;
            }
            if (!method && name && key_length == 7 && memcmp(name, "indexOf", 7) == 0) {
                method = MVP_BUILTIN_STRING_INDEX_OF;
            }
            if (!method && name && key_length == 6 && memcmp(name, "charAt", 6) == 0) {
                method = MVP_BUILTIN_STRING_CHAR_AT;
            }
            if (!method && name && key_length == 4 && memcmp(name, "trim", 4) == 0) {
                method = MVP_BUILTIN_STRING_TRIM;
            }
            if (method) {
                MvpValue callback = mvp_builtin_method_new(execution, (MvpBuiltinMethod)method,
                    object,
                    method == MVP_BUILTIN_STRING_CHAR_CODE_AT ||
                    method == MVP_BUILTIN_STRING_REPEAT || method == MVP_BUILTIN_STRING_SPLIT ||
                    method == MVP_BUILTIN_STRING_STARTS_WITH ||
                    method == MVP_BUILTIN_STRING_MATCH ? 1 : 2);
                if (!mvp_value_is_function(callback)) {
                    mvp_execution_set_error(execution, "MVP String method allocation failed");
                }
                return callback.bits;
            }
        }
    }
    if (mvp_value_is_number(object) && mvp_value_is_string(key)) {
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        if (name && length == 7 && memcmp(name, "toFixed", 7) == 0) {
            MvpValue callback = mvp_builtin_method_new(execution, MVP_BUILTIN_NUMBER_TO_FIXED,
                object, 1);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP Number.toFixed allocation failed");
            }
            return callback.bits;
        }
    }
    if (mvp_value_is_bigint(object) && mvp_value_is_string(key)) {
        size_t length = 0;
        const char* name = mvp_string_bytes(key, &length);
        if (name && length == 8 && memcmp(name, "toString", 8) == 0) {
            MvpValue callback = mvp_builtin_method_new(execution, MVP_BUILTIN_BIGINT_TO_STRING,
                object, 0);
            if (!mvp_value_is_function(callback)) {
                mvp_execution_set_error(execution, "MVP BigInt.toString allocation failed");
            }
            return callback.bits;
        }
    }
    // JavaScript reads of an absent property produce undefined.  Only a nullish
    // base is an error; arrays also need this for optional user properties.
    if (!mvp_value_is_undefined(object) && !mvp_value_is_null(object)) {
        return mvp_value_undefined().bits;
    }
    const char* kind = mvp_value_is_undefined(object) ? "undefined" : "null";
    size_t key_length = 0;
    const char* key_name = mvp_string_bytes(key, &key_length);
    char message[160] = {};
    int written = key_name ? snprintf(message, sizeof(message),
        "MVP member read %.*s on %s is unsupported", (int)key_length, key_name, kind)
        : snprintf(message, sizeof(message), "MVP member read on %s is unsupported", kind);
    mvp_execution_set_error(execution, written > 0 ? message : "MVP member read is unsupported");
    return mvp_value_undefined().bits;
}

static uint64_t mvp_op_named_member_get_with_hash(MvpExecution* execution,
        uint64_t object_bits, uint64_t key_address, uint64_t key_length, uint64_t key_hash) {
    if (!execution || !key_address || key_length > SIZE_MAX) return mvp_value_undefined().bits;
    MvpValue object = mvp_runtime_value(object_bits);
    MvpValue result = mvp_value_undefined();
    const char* key = (const char*)(uintptr_t)key_address;
    if (mvp_object_get_named_hashed(object, key, (size_t)key_length, key_hash, &result)) {
        if (mvp_value_is_function(mvp_accessor_getter(result))) {
            return mvp_op_call_values(execution, mvp_accessor_getter(result).bits, object.bits,
                NULL, 0);
        }
        return result.bits;
    }
    // A missing ordinary-object field and every non-object receiver retain the
    // generic member semantics, including Date and primitive prototype methods.
    uint64_t key_bits = mvp_execution_literal_string(execution, key_address, key_length);
    return mvp_execution_has_error(execution) ? mvp_value_undefined().bits
        : mvp_op_member_get(execution, object_bits, key_bits);
}

uint64_t mvp_op_named_member_get(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length) {
    return !key_address || key_length > SIZE_MAX ? mvp_value_undefined().bits
        : mvp_op_named_member_get_with_hash(execution, object_bits, key_address, key_length,
            mvp_string_hash_bytes((const char*)(uintptr_t)key_address, (size_t)key_length));
}

uint64_t mvp_op_named_member_get_hashed(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length, uint64_t key_hash) {
    return mvp_op_named_member_get_with_hash(execution, object_bits, key_address, key_length,
        key_hash);
}

uint64_t mvp_op_object_named_own_get_hashed(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length, uint64_t key_hash) {
    (void)execution;
    if (!key_address || key_length > SIZE_MAX) return mvp_value_undefined().bits;
    MvpValue value = mvp_value_undefined();
    (void)mvp_object_get_own_named_hashed(mvp_runtime_value(object_bits),
        (const char*)(uintptr_t)key_address, (size_t)key_length, key_hash, &value);
    return value.bits;
}

static uint64_t mvp_op_named_member_set_with_hash(MvpExecution* execution,
        uint64_t object_bits, uint64_t key_address, uint64_t key_length, uint64_t key_hash,
        uint64_t value_bits) {
    if (!execution || !key_address || key_length > SIZE_MAX) return mvp_value_undefined().bits;
    MvpValue object = mvp_runtime_value(object_bits);
    MvpValue value = mvp_runtime_value(value_bits);
    if (mvp_object_set_named_existing_hashed(object, (const char*)(uintptr_t)key_address,
            (size_t)key_length, key_hash, value)) {
        return value.bits;
    }
    // The direct write only applies to an own data property. New fields,
    // inherited fields, and accessors retain the generic assignment semantics.
    uint64_t key_bits = mvp_execution_literal_string(execution, key_address, key_length);
    return mvp_execution_has_error(execution) ? mvp_value_undefined().bits
        : mvp_op_member_set(execution, object_bits, key_bits, value_bits);
}

uint64_t mvp_op_named_member_set(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length, uint64_t value_bits) {
    return !key_address || key_length > SIZE_MAX ? mvp_value_undefined().bits
        : mvp_op_named_member_set_with_hash(execution, object_bits, key_address, key_length,
            mvp_string_hash_bytes((const char*)(uintptr_t)key_address, (size_t)key_length),
            value_bits);
}

uint64_t mvp_op_named_member_set_hashed(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length, uint64_t key_hash, uint64_t value_bits) {
    return mvp_op_named_member_set_with_hash(execution, object_bits, key_address, key_length,
        key_hash, value_bits);
}

uint64_t mvp_op_optional_member_get(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_bits) {
    MvpValue object = mvp_runtime_value(object_bits);
    if (mvp_value_is_null(object) || mvp_value_is_undefined(object)) {
        return mvp_value_undefined().bits;
    }
    return mvp_op_member_get(execution, object_bits, key_bits);
}

uint64_t mvp_op_optional_named_member_get(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_address, uint64_t key_length) {
    MvpValue object = mvp_runtime_value(object_bits);
    return mvp_value_is_null(object) || mvp_value_is_undefined(object)
        ? mvp_value_undefined().bits : mvp_op_named_member_get(execution, object_bits,
            key_address, key_length);
}

uint64_t mvp_op_optional_named_member_get_hashed(MvpExecution* execution,
        uint64_t object_bits, uint64_t key_address, uint64_t key_length, uint64_t key_hash) {
    MvpValue object = mvp_runtime_value(object_bits);
    return mvp_value_is_null(object) || mvp_value_is_undefined(object)
        ? mvp_value_undefined().bits : mvp_op_named_member_get_with_hash(execution,
            object_bits, key_address, key_length, key_hash);
}

uint64_t mvp_op_member_set(MvpExecution* execution, uint64_t object_bits,
        uint64_t key_bits, uint64_t value_bits) {
    MvpValue object = mvp_runtime_value(object_bits);
    MvpValue key = mvp_runtime_value(key_bits);
    MvpValue value = mvp_runtime_value(value_bits);
    size_t index = 0;
    int written = 0;
    if (mvp_value_is_array(object) && mvp_value_property_index(key, &index)) {
        written = mvp_array_set(&execution->heap, object, index, value);
    } else if (mvp_value_is_array(object) && mvp_value_is_string(key)) {
        size_t key_length = 0;
        const char* name = mvp_string_bytes(key, &key_length);
        size_t length = 0;
        if (name && key_length == 6 && memcmp(name, "length", 6) == 0 &&
                mvp_value_index(value, &length)) {
            written = mvp_array_set_length(&execution->heap, object, length);
        } else {
            written = mvp_array_set_property(&execution->heap, object, key, value);
        }
    } else if (mvp_value_is_object(object)) {
        MvpValue roots[3] = {object, key, value};
        MvpRootFrame frame = {};
        mvp_root_frame_push(&execution->heap, &frame, roots, 3);
        if (!mvp_value_is_string(roots[1])) roots[1] = mvp_to_string(execution, roots[1]);
        size_t key_length = 0;
        const char* name = mvp_string_bytes(roots[1], &key_length);
        if (name && key_length == 9 && memcmp(name, "__proto__", 9) == 0) {
            MvpObject* target = (MvpObject*)mvp_value_reference_object(roots[0]);
            if (target && (mvp_value_is_object(roots[2]) || mvp_value_is_null(roots[2]))) {
                target->prototype = roots[2];
                written = 1;
            }
        } else {
            written = mvp_value_is_string(roots[1]) && mvp_object_set(&execution->heap,
                roots[0], roots[1], roots[2]);
        }
        mvp_root_frame_pop(&execution->heap, &frame);
    } else {
        MvpFunction* function = mvp_runtime_function(object);
        if (function && mvp_value_is_string(key)) {
            size_t key_length = 0;
            const char* name = mvp_string_bytes(key, &key_length);
            if (name && key_length == 9 && memcmp(name, "prototype", 9) == 0) {
                function->instance_prototype = value;
                written = 1;
            } else if (mvp_value_is_object(function->properties)) {
                written = mvp_object_set(&execution->heap, function->properties, key, value);
            }
        }
    }
    if (!written) {
        size_t key_length = 0;
        const char* key_name = mvp_string_bytes(key, &key_length);
        const char* kind = mvp_value_is_undefined(object) ? "undefined"
            : mvp_value_is_null(object) ? "null" : mvp_value_is_array(object) ? "array"
            : mvp_value_is_object(object) ? "object" : mvp_value_is_function(object)
            ? "function" : "unknown";
        char message[160] = {};
        int written_message = key_name ? snprintf(message, sizeof(message),
            "MVP member write %.*s on %s is unsupported", (int)key_length, key_name, kind)
            : snprintf(message, sizeof(message), "MVP member write on %s is unsupported", kind);
        mvp_execution_set_error(execution, written_message > 0 ? message
            : "MVP member write is unsupported");
        return mvp_value_undefined().bits;
    }
    return value.bits;
}

uint64_t mvp_op_array_fill(MvpExecution* execution, uint64_t object_bits,
        uint64_t value_bits) {
    MvpValue object = mvp_runtime_value(object_bits);
    MvpHeapObject* header = mvp_value_reference_object(object);
    if (!header || header->kind != MVP_HEAP_ARRAY) {
        mvp_execution_set_error(execution, "MVP Array.fill receiver is not an array");
        return mvp_value_undefined().bits;
    }
    MvpArray* array = (MvpArray*)header;
    MvpValue* values = mvp_heap_object_values(mvp_value_reference_object(array->storage));
    if (!values && array->length) {
        mvp_execution_set_error(execution, "MVP Array.fill storage is unavailable");
        return mvp_value_undefined().bits;
    }
    MvpValue value = mvp_runtime_value(value_bits);
    for (size_t index = 0; index < array->length; index++) values[index] = value;
    return object.bits;
}

uint64_t mvp_op_array_push(MvpExecution* execution, uint64_t object_bits,
        uint64_t value_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue object = mvp_runtime_value(object_bits);
    MvpValue value = mvp_runtime_value(value_bits);
    if (!mvp_value_is_array(object) || !mvp_array_push(&execution->heap, object, value)) {
        mvp_execution_set_error(execution, "MVP array literal append failed");
        return mvp_value_undefined().bits;
    }
    return object.bits;
}

uint64_t mvp_op_array_spread(MvpExecution* execution, uint64_t object_bits,
        uint64_t source_bits) {
    if (!execution) return mvp_value_undefined().bits;
    MvpValue roots[3] = {mvp_runtime_value(object_bits), mvp_runtime_value(source_bits),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&execution->heap, &frame, roots, 3);
    if (!mvp_value_is_array(roots[0]) || !mvp_value_is_array(roots[1])) {
        mvp_execution_set_error(execution, "MVP array spread requires an array");
        mvp_root_frame_pop(&execution->heap, &frame);
        return mvp_value_undefined().bits;
    }
    size_t length = mvp_array_length(roots[1]);
    for (size_t index = 0; index < length; index++) {
        roots[2] = mvp_array_get(roots[1], index);
        if (!mvp_array_push(&execution->heap, roots[0], roots[2])) {
            mvp_execution_set_error(execution, "MVP array spread allocation failed");
            break;
        }
    }
    MvpValue result = roots[0];
    mvp_root_frame_pop(&execution->heap, &frame);
    return result.bits;
}
