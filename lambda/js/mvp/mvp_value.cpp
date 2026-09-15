#include "mvp.h"

#include "../../../lib/mem.h"

#include <math.h>
#include <string.h>

static const uint64_t MVP_NUMBER_NAN = UINT64_C(0x7ff8000000000000);
static const uint64_t MVP_TAG_MASK = UINT64_C(0xffff000000000000);
static const uint64_t MVP_PAYLOAD_MASK = UINT64_C(0x0000ffffffffffff);
static const uint64_t MVP_TAG_UNDEFINED = UINT64_C(0xfff9000000000000);
static const uint64_t MVP_TAG_NULL = UINT64_C(0xfffa000000000000);
static const uint64_t MVP_TAG_FALSE = UINT64_C(0xfffb000000000000);
static const uint64_t MVP_TAG_TRUE = UINT64_C(0xfffc000000000000);
static const uint64_t MVP_TAG_REFERENCE = UINT64_C(0xfffd000000000000);
static const uint64_t MVP_TAG_COMPLETION = UINT64_C(0xffff000000000000);

static_assert(sizeof(MvpValue) == sizeof(uint64_t), "MvpValue must remain one word");

static int mvp_pointer_payload_fits(const MvpHeapObject* object) {
    return ((uint64_t)(uintptr_t)object & ~MVP_PAYLOAD_MASK) == 0;
}

static uint64_t mvp_number_bits(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double mvp_number_from_bits(uint64_t bits) {
    double value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int mvp_tagged_as(uint64_t bits, uint64_t tag) {
    return (bits & MVP_TAG_MASK) == tag;
}

static int mvp_is_tagged(uint64_t bits) {
    return (bits & MVP_TAG_MASK) >= MVP_TAG_UNDEFINED;
}

MvpValue mvp_value_from_number(double value) {
    MvpValue result = {mvp_number_bits(value)};
    // All NaNs share a numerical encoding outside the boxed-value range.
    if (isnan(value)) result.bits = MVP_NUMBER_NAN;
    return result;
}

double mvp_value_to_number(MvpValue value) {
    return mvp_number_from_bits(value.bits);
}

int mvp_value_is_number(MvpValue value) {
    return !mvp_is_tagged(value.bits);
}

int mvp_value_is_nan(MvpValue value) {
    return mvp_value_is_number(value) && isnan(mvp_value_to_number(value));
}

int mvp_value_is_undefined(MvpValue value) {
    return value.bits == MVP_TAG_UNDEFINED;
}

int mvp_value_is_null(MvpValue value) {
    return value.bits == MVP_TAG_NULL;
}

int mvp_value_is_boolean(MvpValue value) {
    return value.bits == MVP_TAG_FALSE || value.bits == MVP_TAG_TRUE;
}

int mvp_value_is_reference(MvpValue value) {
    return mvp_tagged_as(value.bits, MVP_TAG_REFERENCE);
}

int mvp_value_is_completion(MvpValue value) {
    return mvp_tagged_as(value.bits, MVP_TAG_COMPLETION);
}

static int mvp_value_is_heap_kind(MvpValue value, MvpHeapKind kind) {
    MvpHeapObject* object = mvp_value_reference_object(value);
    return object && object->kind == kind;
}

int mvp_value_is_string(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_STRING);
}

int mvp_value_is_bigint(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_BIGINT);
}

int mvp_value_is_array(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_ARRAY);
}

int mvp_value_is_object(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_OBJECT);
}

int mvp_value_is_function(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_FUNCTION);
}

int mvp_value_is_map(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_MAP);
}

int mvp_value_is_regex(MvpValue value) {
    return mvp_value_is_heap_kind(value, MVP_HEAP_REGEX);
}

int mvp_value_truthy(MvpValue value) {
    if (mvp_value_is_undefined(value) || mvp_value_is_null(value)) return 0;
    if (mvp_value_is_boolean(value)) return mvp_value_boolean(value);
    if (mvp_value_is_number(value)) {
        double number = mvp_value_to_number(value);
        return number != 0.0 && !isnan(number);
    }
    if (mvp_value_is_string(value)) {
        size_t length = 0;
        return mvp_string_bytes(value, &length) != NULL && length != 0;
    }
    return 1;
}

int mvp_value_strict_equal(MvpValue left, MvpValue right) {
    if (mvp_value_is_number(left) && mvp_value_is_number(right)) {
        double left_number = mvp_value_to_number(left);
        double right_number = mvp_value_to_number(right);
        return !isnan(left_number) && !isnan(right_number) && left_number == right_number;
    }
    if (mvp_value_is_string(left) && mvp_value_is_string(right)) {
        return mvp_string_equal(left, right);
    }
    if (mvp_value_is_bigint(left) && mvp_value_is_bigint(right)) {
        return mvp_bigint_compare(left, right) == 0;
    }
    return left.bits == right.bits;
}

int mvp_value_boolean(MvpValue value) {
    return value.bits == MVP_TAG_TRUE;
}

MvpValue mvp_value_undefined(void) {
    MvpValue value = {MVP_TAG_UNDEFINED};
    return value;
}

MvpValue mvp_value_null(void) {
    MvpValue value = {MVP_TAG_NULL};
    return value;
}

MvpValue mvp_value_bool(int value) {
    MvpValue result = {value ? MVP_TAG_TRUE : MVP_TAG_FALSE};
    return result;
}

MvpValue mvp_value_reference(MvpHeapObject* object) {
    if (!mvp_pointer_payload_fits(object)) return mvp_value_undefined();
    uintptr_t address = (uintptr_t)object;
    MvpValue result = {MVP_TAG_REFERENCE | ((uint64_t)address & MVP_PAYLOAD_MASK)};
    return result;
}

MvpHeapObject* mvp_value_reference_object(MvpValue value) {
    if (!mvp_value_is_reference(value)) return NULL;
    return (MvpHeapObject*)(uintptr_t)(value.bits & MVP_PAYLOAD_MASK);
}

MvpValue mvp_value_completion(MvpCompletionKind kind, MvpHeapObject* payload) {
    if (!mvp_pointer_payload_fits(payload)) return mvp_value_undefined();
    uintptr_t address = (uintptr_t)payload;
    uint64_t payload_bits = ((uint64_t)address & MVP_PAYLOAD_MASK);
    // The low two bits reserve the completion kind and require heap alignment.
    MvpValue result = {MVP_TAG_COMPLETION | (payload_bits & ~UINT64_C(3)) |
        (uint64_t)kind};
    return result;
}

MvpCompletionKind mvp_value_completion_kind(MvpValue value) {
    if (!mvp_value_is_completion(value)) return (MvpCompletionKind)0;
    return (MvpCompletionKind)(value.bits & UINT64_C(3));
}

MvpHeapObject* mvp_value_completion_payload(MvpValue value) {
    if (!mvp_value_is_completion(value)) return NULL;
    return (MvpHeapObject*)(uintptr_t)(value.bits & MVP_PAYLOAD_MASK & ~UINT64_C(3));
}
