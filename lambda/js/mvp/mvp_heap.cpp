#include "mvp.h"

#include "../../../lib/mem.h"

#include <re2/re2.h>

#include <math.h>
#include <string.h>

static const size_t MVP_INITIAL_COLLECTION_BYTES = 64 * 1024;
static const uint64_t MVP_BIGINT_BASE = UINT64_C(1000000000);

static MvpHeapObject* mvp_heap_alloc_raw(MvpHeap* heap, MvpHeapKind kind,
        size_t bytes) {
    if (!heap || bytes < sizeof(MvpHeapObject)) return NULL;
    if (heap->force_every_allocation || heap->allocated_bytes + bytes >
            heap->next_collection_bytes) {
        mvp_heap_collect(heap);
    }
    MvpHeapObject* object = (MvpHeapObject*)mem_calloc(1, bytes, MEM_CAT_JS_RUNTIME);
    if (!object) return NULL;
    if (((uint64_t)(uintptr_t)object >> 48) != 0) {
        // The MvpValue ABI reserves 48 payload bits; reject an incompatible
        // allocator address instead of truncating a live heap reference.
        mem_free(object);
        return NULL;
    }
    object->allocation_size = bytes;
    object->kind = kind;
    object->next = heap->objects;
    heap->objects = object;
    heap->allocated_bytes += bytes;
    heap->live_bytes += bytes;
    heap->object_count++;
    return object;
}

static void mvp_heap_mark_value(MvpHeapObject** worklist, MvpValue value) {
    MvpHeapObject* object = NULL;
    if (mvp_value_is_reference(value)) {
        object = mvp_value_reference_object(value);
    } else if (mvp_value_is_completion(value)) {
        object = mvp_value_completion_payload(value);
    }
    if (!object || object->marked) return;
    object->marked = 1;
    object->mark_next = *worklist;
    *worklist = object;
}

static void mvp_heap_trace_object(MvpHeapObject** worklist, MvpHeapObject* object) {
    if (!object) return;
    if (object->kind == MVP_HEAP_VALUES) {
        MvpValue* values = mvp_heap_object_values(object);
        size_t count = mvp_heap_object_value_count(object);
        for (size_t i = 0; i < count; i++) {
            mvp_heap_mark_value(worklist, values[i]);
        }
    } else if (object->kind == MVP_HEAP_ARRAY) {
        MvpArray* array = (MvpArray*)object;
        mvp_heap_mark_value(worklist, array->storage);
        mvp_heap_mark_value(worklist, array->properties);
    } else if (object->kind == MVP_HEAP_OBJECT) {
        MvpObject* map = (MvpObject*)object;
        mvp_heap_mark_value(worklist, map->storage);
        mvp_heap_mark_value(worklist, map->prototype);
        mvp_heap_mark_value(worklist, map->index_storage);
    } else if (object->kind == MVP_HEAP_MAP) {
        MvpMap* map = (MvpMap*)object;
        mvp_heap_mark_value(worklist, map->storage);
        mvp_heap_mark_value(worklist, map->index_storage);
    } else if (object->kind == MVP_HEAP_REGEX) {
        MvpRegex* regex = (MvpRegex*)object;
        mvp_heap_mark_value(worklist, regex->pattern);
        mvp_heap_mark_value(worklist, regex->flags);
    } else if (object->kind == MVP_HEAP_ACCESSOR) {
        MvpAccessor* accessor = (MvpAccessor*)object;
        mvp_heap_mark_value(worklist, accessor->getter);
        mvp_heap_mark_value(worklist, accessor->setter);
    } else if (object->kind == MVP_HEAP_FUNCTION) {
        MvpFunction* function = (MvpFunction*)object;
        mvp_heap_mark_value(worklist, function->captures);
        mvp_heap_mark_value(worklist, function->properties);
        mvp_heap_mark_value(worklist, function->instance_prototype);
    }
}

void mvp_heap_init(MvpHeap* heap) {
    if (!heap) return;
    memset(heap, 0, sizeof(*heap));
    heap->next_collection_bytes = MVP_INITIAL_COLLECTION_BYTES;
}

void mvp_heap_destroy(MvpHeap* heap) {
    if (!heap) return;
    MvpHeapObject* object = heap->objects;
    while (object) {
        MvpHeapObject* next = object->next;
        if (object->kind == MVP_HEAP_REGEX) {
            MvpRegex* regex = (MvpRegex*)object;
            delete (re2::RE2*)regex->compiled; // NEW_DELETE_OK: private regex payload owner.
        }
        mem_free(object);
        object = next;
    }
    memset(heap, 0, sizeof(*heap));
}

void mvp_root_frame_push(MvpHeap* heap, MvpRootFrame* frame,
        MvpValue* values, size_t count) {
    if (!heap || !frame) return;
    frame->previous = heap->roots;
    frame->values = values;
    frame->count = count;
    heap->roots = frame;
}

void mvp_root_frame_pop(MvpHeap* heap, MvpRootFrame* frame) {
    if (!heap || !frame || heap->roots != frame) return;
    heap->roots = frame->previous;
    frame->previous = NULL;
    frame->values = NULL;
    frame->count = 0;
}

MvpHeapObject* mvp_heap_alloc_values(MvpHeap* heap, size_t value_count) {
    if (!heap || value_count > (SIZE_MAX - sizeof(MvpHeapObject)) / sizeof(MvpValue)) {
        return NULL;
    }
    size_t bytes = sizeof(MvpHeapObject) + value_count * sizeof(MvpValue);
    return mvp_heap_alloc_raw(heap, MVP_HEAP_VALUES, bytes);
}

MvpValue* mvp_heap_object_values(MvpHeapObject* object) {
    if (!object || object->kind != MVP_HEAP_VALUES) return NULL;
    return (MvpValue*)(object + 1);
}

size_t mvp_heap_object_value_count(const MvpHeapObject* object) {
    if (!object || object->kind != MVP_HEAP_VALUES || object->allocation_size < sizeof(*object)) return 0;
    return (object->allocation_size - sizeof(*object)) / sizeof(MvpValue);
}

void mvp_heap_collect(MvpHeap* heap) {
    if (!heap) return;
    MvpHeapObject* worklist = NULL;
    for (MvpRootFrame* frame = heap->roots; frame; frame = frame->previous) {
        for (size_t i = 0; i < frame->count; i++) {
            mvp_heap_mark_value(&worklist, frame->values[i]);
        }
    }
    while (worklist) {
        MvpHeapObject* object = worklist;
        worklist = object->mark_next;
        object->mark_next = NULL;
        mvp_heap_trace_object(&worklist, object);
    }

    size_t live_bytes = 0;
    size_t object_count = 0;
    MvpHeapObject** previous_next = &heap->objects;
    while (*previous_next) {
        MvpHeapObject* object = *previous_next;
        if (!object->marked) {
            *previous_next = object->next;
            if (object->kind == MVP_HEAP_REGEX) {
                MvpRegex* regex = (MvpRegex*)object;
                delete (re2::RE2*)regex->compiled; // NEW_DELETE_OK: private regex payload owner.
            }
            mem_free(object);
            continue;
        }
        object->marked = 0;
        live_bytes += object->allocation_size;
        object_count++;
        previous_next = &object->next;
    }
    heap->allocated_bytes = live_bytes;
    heap->live_bytes = live_bytes;
    heap->object_count = object_count;
    heap->collection_count++;
    heap->next_collection_bytes = live_bytes > MVP_INITIAL_COLLECTION_BYTES
        ? live_bytes * 2 : MVP_INITIAL_COLLECTION_BYTES;
}

MvpValue mvp_string_new(MvpHeap* heap, const char* bytes, size_t byte_length) {
    if (!heap || byte_length > SIZE_MAX - sizeof(MvpString)) return mvp_value_undefined();
    MvpString* string = (MvpString*)mvp_heap_alloc_raw(heap, MVP_HEAP_STRING,
        sizeof(MvpString) + byte_length);
    if (!string) return mvp_value_undefined();
    string->byte_length = byte_length;
    if (byte_length && bytes) memcpy(string->bytes, bytes, byte_length);
    string->bytes[byte_length] = '\0';
    return mvp_value_reference(&string->header);
}

const char* mvp_string_bytes(MvpValue value, size_t* out_length) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_STRING) return NULL;
    MvpString* string = (MvpString*)object;
    if (out_length) *out_length = string->byte_length;
    return string->bytes;
}

uint64_t mvp_string_hash_bytes(const char* bytes, size_t byte_length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0; bytes && index < byte_length; index++) {
        hash ^= (unsigned char)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    hash ^= (uint64_t)byte_length;
    return hash ? hash : 1;
}

static uint64_t mvp_string_lookup_hash(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_STRING) return 0;
    MvpString* string = (MvpString*)object;
    if (string->lookup_hash) return string->lookup_hash;
    uint64_t hash = mvp_string_hash_bytes(string->bytes, string->byte_length);
    string->lookup_hash = hash ? hash : 1;
    return string->lookup_hash;
}

int mvp_string_equal(MvpValue left, MvpValue right) {
    if (left.bits == right.bits) return 1;
    size_t left_length = 0;
    size_t right_length = 0;
    const char* left_bytes = mvp_string_bytes(left, &left_length);
    const char* right_bytes = mvp_string_bytes(right, &right_length);
    return left_bytes && right_bytes && left_length == right_length &&
        mvp_string_lookup_hash(left) == mvp_string_lookup_hash(right) &&
        memcmp(left_bytes, right_bytes, left_length) == 0;
}

MvpValue mvp_string_concat(MvpHeap* heap, MvpValue left, MvpValue right) {
    size_t left_length = 0;
    size_t right_length = 0;
    const char* left_bytes = mvp_string_bytes(left, &left_length);
    const char* right_bytes = mvp_string_bytes(right, &right_length);
    if (!left_bytes || !right_bytes || left_length > SIZE_MAX - right_length) {
        return mvp_value_undefined();
    }
    size_t length = left_length + right_length;
    MvpValue roots[2] = {left, right};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    MvpValue result = mvp_string_new(heap, NULL, length);
    MvpHeapObject* object = mvp_value_reference_object(result);
    if (object && object->kind == MVP_HEAP_STRING) {
        MvpString* joined = (MvpString*)object;
        memcpy(joined->bytes, left_bytes, left_length);
        memcpy(joined->bytes + left_length, right_bytes, right_length);
    } else {
        result = mvp_value_undefined();
    }
    mvp_root_frame_pop(heap, &frame);
    return result;
}

static MvpBigInt* mvp_bigint_object(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_BIGINT ? (MvpBigInt*)object : NULL;
}

static MvpValue mvp_bigint_new_raw(MvpHeap* heap, size_t limb_count, int sign) {
    if (!heap || limb_count > (SIZE_MAX - sizeof(MvpBigInt)) / sizeof(uint32_t) + 1) {
        return mvp_value_undefined();
    }
    size_t bytes = sizeof(MvpBigInt);
    if (limb_count > 1) bytes += (limb_count - 1) * sizeof(uint32_t);
    MvpBigInt* bigint = (MvpBigInt*)mvp_heap_alloc_raw(heap, MVP_HEAP_BIGINT, bytes);
    if (!bigint) return mvp_value_undefined();
    bigint->limb_count = limb_count;
    bigint->sign = limb_count ? (sign < 0 ? -1 : 1) : 0;
    return mvp_value_reference(&bigint->header);
}

static size_t mvp_bigint_trimmed_count(const uint32_t* limbs, size_t count) {
    while (count && !limbs[count - 1]) count--;
    return count;
}

MvpValue mvp_bigint_from_uint64(MvpHeap* heap, uint64_t value) {
    if (!value) return mvp_bigint_new_raw(heap, 0, 0);
    size_t count = 0;
    for (uint64_t remaining = value; remaining; remaining /= MVP_BIGINT_BASE) count++;
    MvpValue result = mvp_bigint_new_raw(heap, count, 1);
    MvpBigInt* bigint = mvp_bigint_object(result);
    if (!bigint) return mvp_value_undefined();
    for (size_t index = 0; index < count; index++) {
        bigint->limbs[index] = (uint32_t)(value % MVP_BIGINT_BASE);
        value /= MVP_BIGINT_BASE;
    }
    return result;
}

MvpValue mvp_bigint_from_decimal(MvpHeap* heap, MvpValue text) {
    size_t length = 0;
    const char* bytes = mvp_string_bytes(text, &length);
    if (!heap || !bytes || !length) return mvp_value_undefined();
    size_t first = 0;
    int sign = 1;
    if (bytes[first] == '-' || bytes[first] == '+') {
        sign = bytes[first] == '-' ? -1 : 1;
        first++;
    }
    if (first == length) return mvp_value_undefined();
    for (size_t index = first; index < length; index++) {
        if (bytes[index] < '0' || bytes[index] > '9') return mvp_value_undefined();
    }
    size_t count = (length - first + 8) / 9;
    MvpValue roots[1] = {text};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpValue result = mvp_bigint_new_raw(heap, count, sign);
    MvpBigInt* bigint = mvp_bigint_object(result);
    if (!bigint) {
        mvp_root_frame_pop(heap, &frame);
        return mvp_value_undefined();
    }
    size_t end = length;
    for (size_t index = 0; index < count; index++) {
        size_t begin = end >= first + 9 ? end - 9 : first;
        uint32_t limb = 0;
        for (size_t digit = begin; digit < end; digit++) limb = limb * 10 +
            (uint32_t)(bytes[digit] - '0');
        bigint->limbs[index] = limb;
        end = begin;
    }
    bigint->limb_count = mvp_bigint_trimmed_count(bigint->limbs, count);
    if (!bigint->limb_count) bigint->sign = 0;
    mvp_root_frame_pop(heap, &frame);
    return result;
}

double mvp_bigint_to_number(MvpValue value) {
    MvpBigInt* bigint = mvp_bigint_object(value);
    if (!bigint) return NAN;
    double result = 0;
    for (size_t index = bigint->limb_count; index > 0; index--) {
        result = result * (double)MVP_BIGINT_BASE + (double)bigint->limbs[index - 1];
    }
    return bigint->sign < 0 ? -result : result;
}

MvpValue mvp_bigint_to_string(MvpHeap* heap, MvpValue value) {
    MvpBigInt* bigint = mvp_bigint_object(value);
    if (!heap || !bigint) return mvp_value_undefined();
    if (!bigint->limb_count) return mvp_string_new(heap, "0", 1);
    size_t capacity = (bigint->sign < 0 ? 1 : 0) + bigint->limb_count * 9;
    char* text = (char*)mem_alloc(capacity + 1, MEM_CAT_JS_RUNTIME);
    if (!text) return mvp_value_undefined();
    size_t at = 0;
    if (bigint->sign < 0) text[at++] = '-';
    int first = snprintf(text + at, capacity + 1 - at, "%u",
        bigint->limbs[bigint->limb_count - 1]);
    if (first <= 0 || (size_t)first >= capacity + 1 - at) {
        mem_free(text);
        return mvp_value_undefined();
    }
    at += (size_t)first;
    for (size_t index = bigint->limb_count - 1; index > 0; index--) {
        int written = snprintf(text + at, capacity + 1 - at, "%09u", bigint->limbs[index - 1]);
        if (written != 9) {
            mem_free(text);
            return mvp_value_undefined();
        }
        at += 9;
    }
    MvpValue roots[1] = {value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpValue result = mvp_string_new(heap, text, at);
    mvp_root_frame_pop(heap, &frame);
    mem_free(text);
    return result;
}

static int mvp_bigint_abs_compare(const MvpBigInt* left, const MvpBigInt* right) {
    if (left->limb_count != right->limb_count) return left->limb_count < right->limb_count
        ? -1 : 1;
    for (size_t index = left->limb_count; index > 0; index--) {
        if (left->limbs[index - 1] != right->limbs[index - 1]) {
            return left->limbs[index - 1] < right->limbs[index - 1] ? -1 : 1;
        }
    }
    return 0;
}

int mvp_bigint_compare(MvpValue left_value, MvpValue right_value) {
    MvpBigInt* left = mvp_bigint_object(left_value);
    MvpBigInt* right = mvp_bigint_object(right_value);
    if (!left || !right) return 0;
    if (left->sign != right->sign) return left->sign < right->sign ? -1 : 1;
    int comparison = mvp_bigint_abs_compare(left, right);
    return left->sign < 0 ? -comparison : comparison;
}

static MvpValue mvp_bigint_abs_add(MvpHeap* heap, const MvpBigInt* left,
        const MvpBigInt* right, int sign) {
    size_t count = (left->limb_count > right->limb_count ? left->limb_count :
        right->limb_count) + 1;
    MvpValue result = mvp_bigint_new_raw(heap, count, sign);
    MvpBigInt* out = mvp_bigint_object(result);
    if (!out) return mvp_value_undefined();
    uint64_t carry = 0;
    for (size_t index = 0; index < count - 1; index++) {
        uint64_t sum = carry + (index < left->limb_count ? left->limbs[index] : 0) +
            (index < right->limb_count ? right->limbs[index] : 0);
        out->limbs[index] = (uint32_t)(sum % MVP_BIGINT_BASE);
        carry = sum / MVP_BIGINT_BASE;
    }
    out->limbs[count - 1] = (uint32_t)carry;
    out->limb_count = mvp_bigint_trimmed_count(out->limbs, count);
    if (!out->limb_count) out->sign = 0;
    return result;
}

static MvpValue mvp_bigint_abs_sub(MvpHeap* heap, const MvpBigInt* larger,
        const MvpBigInt* smaller, int sign) {
    MvpValue result = mvp_bigint_new_raw(heap, larger->limb_count, sign);
    MvpBigInt* out = mvp_bigint_object(result);
    if (!out) return mvp_value_undefined();
    int64_t borrow = 0;
    for (size_t index = 0; index < larger->limb_count; index++) {
        int64_t value = (int64_t)larger->limbs[index] - (index < smaller->limb_count
            ? (int64_t)smaller->limbs[index] : 0) - borrow;
        if (value < 0) {
            value += (int64_t)MVP_BIGINT_BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        out->limbs[index] = (uint32_t)value;
    }
    out->limb_count = mvp_bigint_trimmed_count(out->limbs, larger->limb_count);
    if (!out->limb_count) out->sign = 0;
    return result;
}

MvpValue mvp_bigint_add(MvpHeap* heap, MvpValue left_value, MvpValue right_value) {
    MvpBigInt* left = mvp_bigint_object(left_value);
    MvpBigInt* right = mvp_bigint_object(right_value);
    if (!heap || !left || !right) return mvp_value_undefined();
    MvpValue roots[2] = {left_value, right_value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    MvpValue result = mvp_value_undefined();
    if (left->sign == right->sign) result = mvp_bigint_abs_add(heap, left, right, left->sign);
    else {
        int comparison = mvp_bigint_abs_compare(left, right);
        result = comparison >= 0 ? mvp_bigint_abs_sub(heap, left, right, left->sign)
            : mvp_bigint_abs_sub(heap, right, left, right->sign);
    }
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_bigint_sub(MvpHeap* heap, MvpValue left_value, MvpValue right_value) {
    MvpBigInt* left = mvp_bigint_object(left_value);
    MvpBigInt* right = mvp_bigint_object(right_value);
    if (!heap || !left || !right) return mvp_value_undefined();
    MvpValue roots[2] = {left_value, right_value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    MvpValue result = mvp_value_undefined();
    if (left->sign != right->sign) result = mvp_bigint_abs_add(heap, left, right, left->sign);
    else {
        int comparison = mvp_bigint_abs_compare(left, right);
        result = comparison >= 0 ? mvp_bigint_abs_sub(heap, left, right, left->sign)
            : mvp_bigint_abs_sub(heap, right, left, -left->sign);
    }
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_bigint_mul(MvpHeap* heap, MvpValue left_value, MvpValue right_value) {
    MvpBigInt* left = mvp_bigint_object(left_value);
    MvpBigInt* right = mvp_bigint_object(right_value);
    if (!heap || !left || !right) return mvp_value_undefined();
    if (!left->limb_count || !right->limb_count) return mvp_bigint_new_raw(heap, 0, 0);
    MvpValue roots[2] = {left_value, right_value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    size_t count = left->limb_count + right->limb_count;
    MvpValue result = mvp_bigint_new_raw(heap, count, left->sign * right->sign);
    MvpBigInt* out = mvp_bigint_object(result);
    if (out) {
        for (size_t left_index = 0; left_index < left->limb_count; left_index++) {
            uint64_t carry = 0;
            for (size_t right_index = 0; right_index < right->limb_count; right_index++) {
                size_t index = left_index + right_index;
                uint64_t value = (uint64_t)out->limbs[index] +
                    (uint64_t)left->limbs[left_index] * right->limbs[right_index] + carry;
                out->limbs[index] = (uint32_t)(value % MVP_BIGINT_BASE);
                carry = value / MVP_BIGINT_BASE;
            }
            size_t carry_index = left_index + right->limb_count;
            while (carry && carry_index < count) {
                uint64_t value = (uint64_t)out->limbs[carry_index] + carry;
                out->limbs[carry_index] = (uint32_t)(value % MVP_BIGINT_BASE);
                carry = value / MVP_BIGINT_BASE;
                carry_index++;
            }
        }
        out->limb_count = mvp_bigint_trimmed_count(out->limbs, count);
        if (!out->limb_count) out->sign = 0;
    }
    mvp_root_frame_pop(heap, &frame);
    return out ? result : mvp_value_undefined();
}

static int mvp_bigint_word_compare(const uint32_t* left, size_t left_count,
        const uint32_t* right, size_t right_count) {
    left_count = mvp_bigint_trimmed_count(left, left_count);
    right_count = mvp_bigint_trimmed_count(right, right_count);
    if (left_count != right_count) return left_count < right_count ? -1 : 1;
    for (size_t index = left_count; index > 0; index--) {
        if (left[index - 1] != right[index - 1]) return left[index - 1] <
            right[index - 1] ? -1 : 1;
    }
    return 0;
}

static size_t mvp_bigint_scratch_mul_small(const MvpBigInt* value, uint32_t factor,
        uint32_t* out) {
    uint64_t carry = 0;
    for (size_t index = 0; index < value->limb_count; index++) {
        uint64_t product = (uint64_t)value->limbs[index] * factor + carry;
        out[index] = (uint32_t)(product % MVP_BIGINT_BASE);
        carry = product / MVP_BIGINT_BASE;
    }
    size_t count = value->limb_count;
    if (carry) out[count++] = (uint32_t)carry;
    return count;
}

MvpValue mvp_bigint_div(MvpHeap* heap, MvpValue left_value, MvpValue right_value) {
    MvpBigInt* left = mvp_bigint_object(left_value);
    MvpBigInt* right = mvp_bigint_object(right_value);
    if (!heap || !left || !right || !right->limb_count) return mvp_value_undefined();
    if (!left->limb_count || mvp_bigint_abs_compare(left, right) < 0) {
        return mvp_bigint_new_raw(heap, 0, 0);
    }
    MvpValue roots[2] = {left_value, right_value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    size_t scratch_count = right->limb_count + 2;
    uint32_t* remainder = (uint32_t*)mem_calloc(scratch_count, sizeof(uint32_t),
        MEM_CAT_JS_RUNTIME);
    uint32_t* product = (uint32_t*)mem_calloc(scratch_count, sizeof(uint32_t),
        MEM_CAT_JS_RUNTIME);
    MvpValue result = mvp_bigint_new_raw(heap, left->limb_count, left->sign * right->sign);
    MvpBigInt* out = mvp_bigint_object(result);
    if (!remainder || !product || !out) {
        mem_free(remainder);
        mem_free(product);
        mvp_root_frame_pop(heap, &frame);
        return mvp_value_undefined();
    }
    size_t remainder_count = 0;
    for (size_t offset = left->limb_count; offset > 0; offset--) {
        if (remainder_count) memmove(remainder + 1, remainder,
            remainder_count * sizeof(uint32_t));
        remainder[0] = left->limbs[offset - 1];
        if (remainder_count < scratch_count - 1) remainder_count++;
        remainder_count = mvp_bigint_trimmed_count(remainder, remainder_count);
        uint32_t low = 0;
        uint32_t high = (uint32_t)MVP_BIGINT_BASE - 1;
        while (low < high) {
            uint32_t middle = low + (uint32_t)(((uint64_t)high - low + 1) / 2);
            size_t product_count = mvp_bigint_scratch_mul_small(right, middle, product);
            if (mvp_bigint_word_compare(remainder, remainder_count, product,
                    product_count) < 0) {
                high = middle - 1;
            } else {
                low = middle;
            }
        }
        uint32_t quotient = low;
        size_t product_count = mvp_bigint_scratch_mul_small(right, quotient, product);
        if (quotient) {
            int64_t borrow = 0;
            for (size_t index = 0; index < remainder_count; index++) {
                int64_t difference = (int64_t)remainder[index] - (index < product_count
                    ? (int64_t)product[index] : 0) - borrow;
                if (difference < 0) {
                    difference += (int64_t)MVP_BIGINT_BASE;
                    borrow = 1;
                } else {
                    borrow = 0;
                }
                remainder[index] = (uint32_t)difference;
            }
            remainder_count = mvp_bigint_trimmed_count(remainder, remainder_count);
        }
        out->limbs[offset - 1] = quotient;
    }
    out->limb_count = mvp_bigint_trimmed_count(out->limbs, left->limb_count);
    if (!out->limb_count) out->sign = 0;
    mem_free(remainder);
    mem_free(product);
    mvp_root_frame_pop(heap, &frame);
    return result;
}

static MvpHeapObject* mvp_value_storage(MvpValue value) {
    MvpHeapObject* storage = mvp_value_reference_object(value);
    return storage && storage->kind == MVP_HEAP_VALUES ? storage : NULL;
}

static int mvp_array_grow(MvpHeap* heap, MvpArray* array, size_t needed) {
    if (!heap || !array) return 0;
    if (needed <= array->capacity) return 1;
    size_t capacity = array->capacity ? array->capacity : 4;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    MvpValue roots[1] = {mvp_value_reference(&array->header)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpHeapObject* old_storage = mvp_value_storage(array->storage);
    MvpHeapObject* new_storage = mvp_heap_alloc_values(heap, capacity);
    if (!new_storage) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* new_values = mvp_heap_object_values(new_storage);
    MvpValue* old_values = mvp_heap_object_values(old_storage);
    if (old_values && array->length) memcpy(new_values, old_values,
        array->length * sizeof(MvpValue));
    array->storage = mvp_value_reference(new_storage);
    array->capacity = capacity;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

MvpValue mvp_array_new(MvpHeap* heap, size_t initial_capacity) {
    if (!heap) return mvp_value_undefined();
    MvpArray* array = (MvpArray*)mvp_heap_alloc_raw(heap, MVP_HEAP_ARRAY, sizeof(MvpArray));
    if (!array) return mvp_value_undefined();
    array->properties = mvp_value_undefined();
    if (!mvp_array_grow(heap, array, initial_capacity)) return mvp_value_undefined();
    return mvp_value_reference(&array->header);
}

size_t mvp_array_length(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_ARRAY ? ((MvpArray*)object)->length : 0;
}

MvpValue mvp_array_get(MvpValue value, size_t index) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_ARRAY) return mvp_value_undefined();
    MvpArray* array = (MvpArray*)object;
    if (index >= array->length) return mvp_value_undefined();
    MvpHeapObject* storage = mvp_value_storage(array->storage);
    MvpValue* elements = mvp_heap_object_values(storage);
    return elements ? elements[index] : mvp_value_undefined();
}

int mvp_array_set(MvpHeap* heap, MvpValue value, size_t index, MvpValue element) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_ARRAY || index == SIZE_MAX) return 0;
    MvpArray* array = (MvpArray*)object;
    MvpValue roots[2] = {value, element};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    if (!mvp_array_grow(heap, array, index + 1)) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpHeapObject* storage = mvp_value_storage(array->storage);
    MvpValue* elements = mvp_heap_object_values(storage);
    if (!elements) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    while (array->length < index) elements[array->length++] = mvp_value_undefined();
    elements[index] = element;
    if (array->length == index) array->length++;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

int mvp_array_push(MvpHeap* heap, MvpValue value, MvpValue element) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_ARRAY) return 0;
    return mvp_array_set(heap, value, ((MvpArray*)object)->length, element);
}

MvpValue mvp_array_pop(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_ARRAY) return mvp_value_undefined();
    MvpArray* array = (MvpArray*)object;
    if (!array->length) return mvp_value_undefined();
    MvpValue* elements = mvp_heap_object_values(mvp_value_storage(array->storage));
    if (!elements) return mvp_value_undefined();
    MvpValue result = elements[array->length - 1];
    elements[--array->length] = mvp_value_undefined();
    return result;
}

int mvp_array_set_length(MvpHeap* heap, MvpValue value, size_t length) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!heap || !object || object->kind != MVP_HEAP_ARRAY) return 0;
    MvpValue roots[1] = {value};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpArray* array = (MvpArray*)object;
    if (!mvp_array_grow(heap, array, length)) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* elements = mvp_heap_object_values(mvp_value_storage(array->storage));
    if (!elements && length) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    size_t prior_length = array->length;
    if (length > prior_length) {
        for (size_t index = prior_length; index < length; index++) {
            elements[index] = mvp_value_undefined();
        }
    } else {
        for (size_t index = length; index < prior_length; index++) {
            elements[index] = mvp_value_undefined();
        }
    }
    array->length = length;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

MvpValue mvp_array_get_property(MvpValue value, MvpValue key) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_ARRAY) return mvp_value_undefined();
    return mvp_object_get(((MvpArray*)object)->properties, key);
}

int mvp_array_set_property(MvpHeap* heap, MvpValue value, MvpValue key, MvpValue element) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!heap || !object || object->kind != MVP_HEAP_ARRAY || !mvp_value_is_string(key)) return 0;
    MvpArray* array = (MvpArray*)object;
    MvpValue roots[3] = {value, key, element};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 3);
    if (!mvp_value_is_object(array->properties)) {
        array->properties = mvp_object_new(heap, mvp_value_null());
    }
    int written = mvp_value_is_object(array->properties) && mvp_object_set(heap,
        array->properties, roots[1], roots[2]);
    mvp_root_frame_pop(heap, &frame);
    return written;
}

MvpValue mvp_object_new(MvpHeap* heap, MvpValue prototype) {
    if (!heap) return mvp_value_undefined();
    MvpValue roots[1] = {prototype};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpObject* object = (MvpObject*)mvp_heap_alloc_raw(heap, MVP_HEAP_OBJECT, sizeof(MvpObject));
    if (!object) {
        mvp_root_frame_pop(heap, &frame);
        return mvp_value_undefined();
    }
    object->prototype = prototype;
    MvpValue result = mvp_value_reference(&object->header);
    mvp_root_frame_pop(heap, &frame);
    return result;
}

static MvpValue* mvp_object_properties(MvpObject* object) {
    return object ? mvp_heap_object_values(mvp_value_storage(object->storage)) : NULL;
}

static MvpValue* mvp_object_indexes(MvpObject* object) {
    return object ? mvp_heap_object_values(mvp_value_storage(object->index_storage)) : NULL;
}

static size_t mvp_index_find_entry(MvpValue* entries, size_t entry_count,
        MvpValue* indexes, size_t index_capacity, MvpValue key);
static size_t mvp_index_find_slot(MvpValue* entries, size_t entry_count,
        MvpValue* indexes, size_t index_capacity, MvpValue key, int* found);
static int mvp_index_ensure(MvpHeap* heap, MvpValue owner, MvpValue entry_storage,
        size_t entry_count, MvpValue* index_storage, size_t* index_capacity, size_t needed);

static size_t mvp_object_find_property(MvpObject* object, MvpValue key) {
    return object ? mvp_index_find_entry(mvp_object_properties(object), object->property_count,
        mvp_object_indexes(object), object->index_capacity, key) : SIZE_MAX;
}

static int mvp_object_grow(MvpHeap* heap, MvpObject* object, size_t needed) {
    if (!heap || !object) return 0;
    if (needed <= object->property_capacity) return 1;
    size_t capacity = object->property_capacity ? object->property_capacity : 4;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / 2) return 0;
    MvpValue roots[1] = {mvp_value_reference(&object->header)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpHeapObject* old_storage = mvp_value_storage(object->storage);
    MvpHeapObject* new_storage = mvp_heap_alloc_values(heap, capacity * 2);
    if (!new_storage) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* old_values = mvp_heap_object_values(old_storage);
    MvpValue* new_values = mvp_heap_object_values(new_storage);
    if (old_values && object->property_count) memcpy(new_values, old_values,
        object->property_count * 2 * sizeof(MvpValue));
    object->storage = mvp_value_reference(new_storage);
    object->property_capacity = capacity;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

MvpValue mvp_object_get(MvpValue value, MvpValue key) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_OBJECT) return mvp_value_undefined();
    for (MvpObject* current = (MvpObject*)object; current; ) {
        size_t index = mvp_object_find_property(current, key);
        if (index != SIZE_MAX) return mvp_object_properties(current)[index * 2 + 1];
        MvpHeapObject* prototype = mvp_value_reference_object(current->prototype);
        current = prototype && prototype->kind == MVP_HEAP_OBJECT ? (MvpObject*)prototype : NULL;
    }
    return mvp_value_undefined();
}

int mvp_object_has_own(MvpValue value, MvpValue key) {
    MvpHeapObject* header = mvp_value_reference_object(value);
    if (!header || header->kind != MVP_HEAP_OBJECT || !mvp_value_is_string(key)) return 0;
    return mvp_object_find_property((MvpObject*)header, key) != SIZE_MAX;
}

int mvp_object_set(MvpHeap* heap, MvpValue value, MvpValue key, MvpValue element) {
    MvpHeapObject* header = mvp_value_reference_object(value);
    if (!header || header->kind != MVP_HEAP_OBJECT || !mvp_value_is_string(key)) return 0;
    MvpObject* object = (MvpObject*)header;
    size_t index = mvp_object_find_property(object, key);
    if (index != SIZE_MAX) {
        mvp_object_properties(object)[index * 2 + 1] = element;
        return 1;
    }
    MvpValue roots[3] = {value, key, element};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 3);
    size_t needed = object->property_count + 1;
    if (!mvp_object_grow(heap, object, needed) || !mvp_index_ensure(heap,
            mvp_value_reference(&object->header), object->storage, object->property_count,
            &object->index_storage, &object->index_capacity, needed)) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* properties = mvp_object_properties(object);
    MvpValue* indexes = mvp_object_indexes(object);
    int found = 0;
    size_t slot = mvp_index_find_slot(properties, object->property_count, indexes,
        object->index_capacity, key, &found);
    if (!properties || !indexes || slot == SIZE_MAX || found) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    properties[object->property_count * 2] = key;
    properties[object->property_count * 2 + 1] = element;
    indexes[slot] = mvp_value_from_number((double)(object->property_count + 1));
    object->property_count++;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

static MvpValue* mvp_map_values(MvpMap* map) {
    return map ? mvp_heap_object_values(mvp_value_storage(map->storage)) : NULL;
}

static uint64_t mvp_index_mix_hash(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static uint64_t mvp_index_hash_key(MvpValue key) {
    if (mvp_value_is_string(key)) {
        return mvp_index_mix_hash(mvp_string_lookup_hash(key));
    }
    uint64_t bits = key.bits;
    if (mvp_value_is_number(key) && mvp_value_to_number(key) == 0.0) {
        bits = mvp_value_from_number(0.0).bits;
    }
    return mvp_index_mix_hash(bits);
}

static int mvp_index_key_equal(MvpValue left, MvpValue right) {
    return mvp_value_strict_equal(left, right) ||
        (mvp_value_is_nan(left) && mvp_value_is_nan(right));
}

static size_t mvp_index_find_slot(MvpValue* entries, size_t entry_count,
        MvpValue* indexes, size_t index_capacity, MvpValue key, int* found) {
    if (found) *found = 0;
    if (!indexes || !entries || !index_capacity) return SIZE_MAX;
    size_t mask = index_capacity - 1;
    size_t slot = (size_t)(mvp_index_hash_key(key) & mask);
    for (;;) {
        MvpValue stored = indexes[slot];
        if (mvp_value_is_number(stored) && mvp_value_to_number(stored) == 0.0) return slot;
        size_t entry = (size_t)mvp_value_to_number(stored) - 1;
        if (entry < entry_count && mvp_index_key_equal(entries[entry * 2], key)) {
            if (found) *found = 1;
            return slot;
        }
        slot = (slot + 1) & mask;
    }
}

static size_t mvp_index_find_entry(MvpValue* entries, size_t entry_count,
        MvpValue* indexes, size_t index_capacity, MvpValue key) {
    int found = 0;
    size_t slot = mvp_index_find_slot(entries, entry_count, indexes, index_capacity, key, &found);
    if (!found || slot == SIZE_MAX) return SIZE_MAX;
    return (size_t)mvp_value_to_number(indexes[slot]) - 1;
}

static size_t mvp_index_find_named_entry(MvpValue* entries, size_t entry_count,
        MvpValue* indexes, size_t index_capacity, const char* key, size_t key_length,
        uint64_t hash) {
    if (!entries || !indexes || !index_capacity || !key) return SIZE_MAX;
    size_t mask = index_capacity - 1;
    size_t slot = (size_t)(mvp_index_mix_hash(hash) & mask);
    for (;;) {
        MvpValue stored = indexes[slot];
        if (mvp_value_is_number(stored) && mvp_value_to_number(stored) == 0.0) return SIZE_MAX;
        size_t entry = (size_t)mvp_value_to_number(stored) - 1;
        if (entry < entry_count && mvp_value_is_string(entries[entry * 2])) {
            MvpString* stored_string = (MvpString*)mvp_value_reference_object(entries[entry * 2]);
            size_t stored_length = 0;
            const char* stored_key = mvp_string_bytes(entries[entry * 2], &stored_length);
            if (stored_string && stored_string->source_address == key &&
                    stored_length == key_length) {
                return entry;
            }
            if (stored_key && stored_length == key_length &&
                    mvp_string_lookup_hash(entries[entry * 2]) == hash &&
                    memcmp(stored_key, key, key_length) == 0) {
                return entry;
            }
        }
        slot = (slot + 1) & mask;
    }
}

int mvp_object_get_named(MvpValue value, const char* key, size_t key_length,
        MvpValue* out_value) {
    return mvp_object_get_named_hashed(value, key, key_length,
        mvp_string_hash_bytes(key, key_length), out_value);
}

int mvp_object_get_named_hashed(MvpValue value, const char* key, size_t key_length,
        uint64_t key_hash, MvpValue* out_value) {
    if (out_value) *out_value = mvp_value_undefined();
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_OBJECT || !key || !out_value) return 0;
    for (MvpObject* current = (MvpObject*)object; current; ) {
        size_t index = mvp_index_find_named_entry(mvp_object_properties(current),
            current->property_count, mvp_object_indexes(current), current->index_capacity,
            key, key_length, key_hash);
        if (index != SIZE_MAX) {
            *out_value = mvp_object_properties(current)[index * 2 + 1];
            return 1;
        }
        MvpHeapObject* prototype = mvp_value_reference_object(current->prototype);
        current = prototype && prototype->kind == MVP_HEAP_OBJECT ? (MvpObject*)prototype : NULL;
    }
    return 0;
}

int mvp_object_get_own_named_hashed(MvpValue value, const char* key, size_t key_length,
        uint64_t key_hash, MvpValue* out_value) {
    if (out_value) *out_value = mvp_value_undefined();
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_OBJECT || !key || !out_value) return 0;
    MvpObject* target = (MvpObject*)object;
    size_t index = mvp_index_find_named_entry(mvp_object_properties(target),
        target->property_count, mvp_object_indexes(target), target->index_capacity, key,
        key_length, key_hash);
    if (index == SIZE_MAX) return 0;
    *out_value = mvp_object_properties(target)[index * 2 + 1];
    return 1;
}

int mvp_object_set_named_existing(MvpValue value, const char* key, size_t key_length,
        MvpValue element) {
    return mvp_object_set_named_existing_hashed(value, key, key_length,
        mvp_string_hash_bytes(key, key_length), element);
}

int mvp_object_set_named_existing_hashed(MvpValue value, const char* key, size_t key_length,
        uint64_t key_hash, MvpValue element) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_OBJECT || !key) return 0;
    MvpObject* target = (MvpObject*)object;
    size_t index = mvp_index_find_named_entry(mvp_object_properties(target),
        target->property_count, mvp_object_indexes(target), target->index_capacity, key,
        key_length, key_hash);
    if (index == SIZE_MAX) return 0;
    MvpValue* properties = mvp_object_properties(target);
    MvpHeapObject* prior = properties ? mvp_value_reference_object(properties[index * 2 + 1])
        : NULL;
    if (!properties || (prior && prior->kind == MVP_HEAP_ACCESSOR)) {
        return 0;
    }
    properties[index * 2 + 1] = element;
    return 1;
}

static int mvp_map_grow(MvpHeap* heap, MvpMap* map, size_t needed) {
    if (!heap || !map) return 0;
    if (needed <= map->entry_capacity) return 1;
    size_t capacity = map->entry_capacity ? map->entry_capacity : 8;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / 2) return 0;
    MvpValue roots[1] = {mvp_value_reference(&map->header)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpHeapObject* old_storage = mvp_value_storage(map->storage);
    MvpHeapObject* new_storage = mvp_heap_alloc_values(heap, capacity * 2);
    if (!new_storage) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* old_values = mvp_heap_object_values(old_storage);
    MvpValue* new_values = mvp_heap_object_values(new_storage);
    if (old_values && map->entry_count) memcpy(new_values, old_values,
        map->entry_count * 2 * sizeof(MvpValue));
    map->storage = mvp_value_reference(new_storage);
    map->entry_capacity = capacity;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

static int mvp_index_ensure(MvpHeap* heap, MvpValue owner, MvpValue entry_storage,
        size_t entry_count, MvpValue* index_storage, size_t* index_capacity, size_t needed) {
    if (!heap || !index_storage || !index_capacity || needed > SIZE_MAX / 2) return 0;
    size_t capacity = *index_capacity ? *index_capacity : 16;
    while (capacity / 2 < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    if (capacity == *index_capacity) return 1;
    MvpValue roots[1] = {owner};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpHeapObject* storage = mvp_heap_alloc_values(heap, capacity);
    if (!storage) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    *index_storage = mvp_value_reference(storage);
    *index_capacity = capacity;
    MvpValue* indexes = mvp_heap_object_values(storage);
    MvpValue* entries = mvp_heap_object_values(mvp_value_storage(entry_storage));
    if (!indexes || (entry_count && !entries)) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    for (size_t entry = 0; entry < entry_count; entry++) {
        int found = 0;
        size_t slot = mvp_index_find_slot(entries, entry_count, indexes, capacity,
            entries[entry * 2], &found);
        if (slot == SIZE_MAX || found) {
            mvp_root_frame_pop(heap, &frame);
            return 0;
        }
        indexes[slot] = mvp_value_from_number((double)(entry + 1));
    }
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

static MvpValue* mvp_map_indexes(MvpMap* map) {
    return map ? mvp_heap_object_values(mvp_value_storage(map->index_storage)) : NULL;
}

static size_t mvp_map_find_entry(MvpMap* map, MvpValue key) {
    return map ? mvp_index_find_entry(mvp_map_values(map), map->entry_count,
        mvp_map_indexes(map), map->index_capacity, key) : SIZE_MAX;
}

MvpValue mvp_map_new(MvpHeap* heap) {
    if (!heap) return mvp_value_undefined();
    MvpMap* map = (MvpMap*)mvp_heap_alloc_raw(heap, MVP_HEAP_MAP, sizeof(MvpMap));
    return map ? mvp_value_reference(&map->header) : mvp_value_undefined();
}

size_t mvp_map_size(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_MAP ? ((MvpMap*)object)->entry_count : 0;
}

MvpValue mvp_map_get(MvpValue value, MvpValue key) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_MAP) return mvp_value_undefined();
    MvpMap* map = (MvpMap*)object;
    size_t index = mvp_map_find_entry(map, key);
    return index == SIZE_MAX ? mvp_value_undefined() : mvp_map_values(map)[index * 2 + 1];
}

int mvp_map_set(MvpHeap* heap, MvpValue value, MvpValue key, MvpValue element) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!heap || !object || object->kind != MVP_HEAP_MAP) return 0;
    MvpMap* map = (MvpMap*)object;
    int found = 0;
    size_t slot = mvp_index_find_slot(mvp_map_values(map), map->entry_count,
        mvp_map_indexes(map), map->index_capacity, key, &found);
    if (found) {
        MvpValue* indexes = mvp_map_indexes(map);
        size_t index = indexes ? (size_t)mvp_value_to_number(indexes[slot]) - 1 : SIZE_MAX;
        if (index == SIZE_MAX || index >= map->entry_count) return 0;
        mvp_map_values(map)[index * 2 + 1] = element;
        return 1;
    }
    MvpValue roots[3] = {value, key, element};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 3);
    if (!mvp_map_grow(heap, map, map->entry_count + 1) || !mvp_index_ensure(heap,
            mvp_value_reference(&map->header), map->storage, map->entry_count,
            &map->index_storage, &map->index_capacity, map->entry_count + 1)) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    MvpValue* entries = mvp_map_values(map);
    entries[map->entry_count * 2] = key;
    entries[map->entry_count * 2 + 1] = element;
    slot = mvp_index_find_slot(entries, map->entry_count, mvp_map_indexes(map),
        map->index_capacity, key, &found);
    MvpValue* indexes = mvp_map_indexes(map);
    if (slot == SIZE_MAX || found || !indexes) {
        mvp_root_frame_pop(heap, &frame);
        return 0;
    }
    indexes[slot] = mvp_value_from_number((double)(map->entry_count + 1));
    map->entry_count++;
    mvp_root_frame_pop(heap, &frame);
    return 1;
}

MvpValue mvp_map_entries(MvpHeap* heap, MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!heap || !object || object->kind != MVP_HEAP_MAP) return mvp_value_undefined();
    MvpMap* map = (MvpMap*)object;
    MvpValue roots[4] = {value, mvp_value_undefined(), mvp_value_undefined(),
        mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 4);
    roots[1] = mvp_array_new(heap, map->entry_count);
    for (size_t index = 0; index < map->entry_count; index++) {
        roots[2] = mvp_array_new(heap, 2);
        MvpValue* entries = mvp_map_values((MvpMap*)mvp_value_reference_object(roots[0]));
        if (!entries || !mvp_array_push(heap, roots[2], entries[index * 2]) ||
                !mvp_array_push(heap, roots[2], entries[index * 2 + 1]) ||
                !mvp_array_push(heap, roots[1], roots[2])) {
            roots[1] = mvp_value_undefined();
            break;
        }
    }
    MvpValue result = roots[1];
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_regex_new(MvpHeap* heap, MvpValue pattern, MvpValue flags) {
    if (!heap || !mvp_value_is_string(pattern) || !mvp_value_is_string(flags)) {
        return mvp_value_undefined();
    }
    MvpValue roots[2] = {pattern, flags};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    MvpRegex* regex = (MvpRegex*)mvp_heap_alloc_raw(heap, MVP_HEAP_REGEX, sizeof(MvpRegex));
    if (!regex) {
        mvp_root_frame_pop(heap, &frame);
        return mvp_value_undefined();
    }
    regex->pattern = roots[0];
    regex->flags = roots[1];
    size_t pattern_length = 0;
    size_t flags_length = 0;
    const char* pattern_bytes = mvp_string_bytes(regex->pattern, &pattern_length);
    const char* flags_bytes = mvp_string_bytes(regex->flags, &flags_length);
    re2::RE2::Options options;
    options.set_log_errors(false);
    for (size_t index = 0; flags_bytes && index < flags_length; index++) {
        if (flags_bytes[index] == 'i') options.set_case_sensitive(false);
        if (flags_bytes[index] == 's') options.set_dot_nl(true);
        if (flags_bytes[index] == 'm') options.set_one_line(false);
        if (flags_bytes[index] == 'g') regex->global = 1;
    }
    re2::RE2* compiled = pattern_bytes ? new re2::RE2(re2::StringPiece(pattern_bytes,
        pattern_length), options) : NULL; // NEW_DELETE_OK: released by private MVP heap.
    if (!compiled || !compiled->ok()) {
        delete compiled; // NEW_DELETE_OK: paired with private MVP regex construction.
        compiled = NULL;
    }
    regex->compiled = compiled;
    MvpValue result = mvp_value_reference(&regex->header);
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_regex_pattern(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_REGEX ? ((MvpRegex*)object)->pattern
        : mvp_value_undefined();
}

int mvp_regex_global(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_REGEX && ((MvpRegex*)object)->global;
}

void* mvp_regex_compiled(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_REGEX ? ((MvpRegex*)object)->compiled : NULL;
}

MvpValue mvp_accessor_new(MvpHeap* heap, MvpValue getter, MvpValue setter) {
    if (!heap || (!mvp_value_is_function(getter) && !mvp_value_is_undefined(getter)) ||
            (!mvp_value_is_function(setter) && !mvp_value_is_undefined(setter))) {
        return mvp_value_undefined();
    }
    MvpValue roots[2] = {getter, setter};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 2);
    MvpAccessor* accessor = (MvpAccessor*)mvp_heap_alloc_raw(heap, MVP_HEAP_ACCESSOR,
        sizeof(MvpAccessor));
    if (accessor) {
        accessor->getter = roots[0];
        accessor->setter = roots[1];
    }
    MvpValue result = accessor ? mvp_value_reference(&accessor->header)
        : mvp_value_undefined();
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_accessor_getter(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_ACCESSOR ? ((MvpAccessor*)object)->getter
        : mvp_value_undefined();
}

MvpValue mvp_accessor_setter(MvpValue value) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == MVP_HEAP_ACCESSOR ? ((MvpAccessor*)object)->setter
        : mvp_value_undefined();
}

MvpValue mvp_function_new(MvpHeap* heap, MvpNativeFunction entry, void* closure,
        size_t formal_count, MvpValue captures) {
    if (!heap || !entry) return mvp_value_undefined();
    MvpValue roots[1] = {captures};
    MvpRootFrame frame = {};
    mvp_root_frame_push(heap, &frame, roots, 1);
    MvpFunction* function = (MvpFunction*)mvp_heap_alloc_raw(heap, MVP_HEAP_FUNCTION,
        sizeof(MvpFunction));
    if (!function) {
        mvp_root_frame_pop(heap, &frame);
        return mvp_value_undefined();
    }
    function->entry = entry;
    function->closure = closure;
    function->formal_count = formal_count;
    function->captures = captures;
    function->properties = mvp_value_undefined();
    function->instance_prototype = mvp_value_undefined();
    MvpValue result = mvp_value_reference(&function->header);
    mvp_root_frame_pop(heap, &frame);
    return result;
}

MvpValue mvp_function_call(MvpValue value, void* execution, MvpValue receiver,
        uint64_t* arguments, size_t argument_count) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    if (!object || object->kind != MVP_HEAP_FUNCTION) return mvp_value_undefined();
    MvpFunction* function = (MvpFunction*)object;
    return (MvpValue){function->entry(execution, function, receiver.bits,
        arguments, argument_count)};
}
