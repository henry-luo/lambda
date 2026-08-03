#include <gtest/gtest.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_runtime.h"

extern "C" {
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lib/shell.h"
}

namespace {

static void expect_raw_item_header(void* ptr, TypeId expected_type) {
    // Constructors must write byte-zero TypeId before exposing raw-pointer Items.
    EXPECT_EQ(*(TypeId*)ptr, expected_type);
    Item item = p2it(ptr);
    uint64_t bits = item.item;

    EXPECT_EQ(bits, (uint64_t)(uintptr_t)ptr);
    EXPECT_EQ(bits & ITEM_HIGH_BYTE_MASK, UINT64_C(0));
    EXPECT_EQ(bits & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_EQ(get_type_id(item), expected_type);
    EXPECT_EQ(it2p(item), ptr);
}

static void expect_array_like_header(List* ptr, TypeId expected_type) {
    expect_raw_item_header(ptr, expected_type);
    Item item = p2it(ptr);
    EXPECT_EQ(it2list(item), ptr);
    EXPECT_EQ(item.array, ptr);
}

static void item_repr_noop_fn() {}

class RuntimeItemRepresentation : public ::testing::Test {
protected:
    gc_heap_t* gc = nullptr;

    void SetUp() override {
        gc = gc_heap_create();
        ASSERT_NE(gc, nullptr);
    }

    void TearDown() override {
        if (gc) {
            gc_heap_destroy(gc);
            gc = nullptr;
        }
    }
};

} // namespace

TEST(ItemRepresentation, SharedMasksKeepSentinelsOutOfDoubleSpace) {
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_NULL >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_UNDEFINED >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_JS_TDZ >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_INT >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_TRUE >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_FALSE >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_ERROR >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(ITEM_NULL_SPREADABLE >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_DELETED_SENTINEL_VAL >> 56)));
    EXPECT_TRUE(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_ITER_DONE_SENTINEL >> 56)));

    EXPECT_EQ(ITEM_FLOAT_P0 & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_EQ(ITEM_FLOAT_N0 & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_EQ((uint8_t)(ITEM_FLOAT_P0 >> 56), LMD_TYPE_FLOAT);
    EXPECT_EQ((uint8_t)(ITEM_FLOAT_N0 >> 56), LMD_TYPE_FLOAT);
}

TEST(ItemRepresentation, ContainerHeaderNamedStatePreservesRawAbi) {
    Container container = {};

    EXPECT_EQ(sizeof(Container), 8u);
    EXPECT_EQ(__builtin_offsetof(Container, cow_state), 4u);
    EXPECT_EQ(__builtin_offsetof(Container, ctor_reserved_mask_lo), 5u);
    EXPECT_EQ(__builtin_offsetof(Container, ctor_reserved_mask_hi), 6u);
    EXPECT_EQ(__builtin_offsetof(Container, reserved_state), 7u);

    // named access and detached raw-mask access must describe the same ABI byte.
    container.has_js_props = 1;
    EXPECT_EQ(container.flags, CONTAINER_FLAG_JS_PROPS);
    container.has_ctor_reserved = 1;
    EXPECT_EQ(container.flags,
              CONTAINER_FLAG_JS_PROPS | CONTAINER_FLAG_CTOR_RESERVED);
    container.has_js_props = 0;
    EXPECT_EQ(container.flags, CONTAINER_FLAG_CTOR_RESERVED);
    container.flags = CONTAINER_FLAG_JS_PROPS;
    EXPECT_TRUE(container.has_js_props);
    EXPECT_FALSE(container.has_ctor_reserved);

    container.cow_state = 0xa5;
    container.ctor_reserved_mask_lo = 0xcd;
    container.ctor_reserved_mask_hi = 0xab;
    container.reserved_state = 0x5a;
    EXPECT_EQ(container.cow_state, 0xa5);
    EXPECT_EQ(container.ctor_reserved_mask_lo, 0xcd);
    EXPECT_EQ(container.ctor_reserved_mask_hi, 0xab);
    EXPECT_EQ(container.reserved_state, 0x5a);

    Map map = {};
    map_ctor_set_reserved_mask(&map, UINT16_C(0xabcd));
    EXPECT_TRUE(map.has_ctor_reserved);
    EXPECT_EQ(map.flags & CONTAINER_FLAG_CTOR_RESERVED,
              CONTAINER_FLAG_CTOR_RESERVED);
    EXPECT_EQ(map_ctor_reserved_mask(&map), UINT16_C(0xabcd));
    map_ctor_set_reserved_mask(&map, 0);
    EXPECT_FALSE(map.has_ctor_reserved);
    EXPECT_EQ(map_ctor_reserved_mask(&map), 0);
}

TEST(ItemRepresentation, Int64AlwaysUsesPointerBackedPayload) {
    const int64_t values[] = {
        INT64_MIN + 1, INT53_MIN, -1, 0, 1, INT53_MAX, INT64_MAX,
    };
    for (const int64_t& value : values) {
        Item item = {.item = l2it(&value)};
        EXPECT_EQ(get_type_id(item), LMD_TYPE_INT64);
        EXPECT_EQ((const int64_t*)(uintptr_t)item.int64_ptr, &value);
        EXPECT_EQ(item.get_int64(), value);
        EXPECT_EQ(it2l(item), value);
    }
}

TEST(ItemRepresentation, Uint64AlwaysUsesPointerBackedPayload) {
    const uint64_t values[] = {0, 1, (uint64_t)INT64_MAX, UINT64_MAX};
    for (const uint64_t& value : values) {
        Item item = {.item = u2it(&value)};
        EXPECT_EQ(get_type_id(item), LMD_TYPE_UINT64);
        EXPECT_EQ((const uint64_t*)(uintptr_t)item.uint64_ptr, &value);
        EXPECT_EQ(item.get_uint64(), value);
    }
}

TEST(ItemRepresentation, Uint64DeepEqualityUsesPayloadValue) {
    uint64_t same_left = UINT64_MAX;
    uint64_t same_right = UINT64_MAX;
    uint64_t different = UINT64_MAX - 1;

    Item left = {.item = u2it(&same_left)};
    Item right = {.item = u2it(&same_right)};
    Item other = {.item = u2it(&different)};

    EXPECT_TRUE(item_deep_equal(left, right));
    EXPECT_FALSE(item_deep_equal(left, other));
}

TEST(ItemRepresentation, MapOwnedWideScalarsPreserveOwnerStorage) {
    int64_t int64_field = INT64_MIN;
    uint64_t uint64_field = UINT64_MAX;
    Item int64_item = map_field_to_item(&int64_field, LMD_TYPE_INT64);
    Item uint64_item = map_field_to_item(&uint64_field, LMD_TYPE_UINT64);

    EXPECT_EQ((int64_t*)(uintptr_t)int64_item.int64_ptr, &int64_field);
    EXPECT_EQ((uint64_t*)(uintptr_t)uint64_item.uint64_ptr, &uint64_field);

    TypedItem typed_int64 = {};
    typed_int64.type_id = LMD_TYPE_INT64;
    typed_int64.long_val = INT64_MAX;
    TypedItem typed_uint64 = {};
    typed_uint64.type_id = LMD_TYPE_UINT64;
    typed_uint64.uint64_val = UINT64_MAX;
    int64_item = map_field_to_item(&typed_int64, LMD_TYPE_ANY);
    uint64_item = map_field_to_item(&typed_uint64, LMD_TYPE_ANY);

    EXPECT_EQ((int64_t*)(uintptr_t)int64_item.int64_ptr, &typed_int64.long_val);
    EXPECT_EQ((uint64_t*)(uintptr_t)uint64_item.uint64_ptr,
              &typed_uint64.uint64_val);
}

TEST(ItemRepresentation, ArrayOwnedCopyRebasesWideScalarPayloads) {
    Item source_storage[6] = {};
    Array source = {};
    source.type_id = LMD_TYPE_ARRAY;
    source.items = source_storage;
    source.length = 3;
    source.capacity = 6;
    source.extra = 3;
    int64_t* source_long = (int64_t*)&source_storage[5];
    double* source_double = (double*)&source_storage[4];
    uint64_t* source_uint = (uint64_t*)&source_storage[3];
    *source_long = INT64_MAX - 1;
    *source_double = ldexp(1.0, -1074);
    *source_uint = UINT64_MAX;
    source_storage[0] = {.item = l2it(source_long)};
    source_storage[1] = {.item = d2it(source_double)};
    source_storage[2] = {.item = u2it(source_uint)};

    Item destination_storage[6] = {};
    Array destination = {};
    destination.type_id = LMD_TYPE_ARRAY;
    destination.items = destination_storage;
    destination.length = 3;
    destination.capacity = 6;

    array_copy_owned_items(&destination, 0, source.items, source.length);

    EXPECT_EQ(destination.extra, 3);
    EXPECT_EQ(destination.items[0].get_int64(), *source_long);
    EXPECT_EQ(destination.items[1].get_double(), *source_double);
    EXPECT_EQ(destination.items[2].get_uint64(), *source_uint);
    EXPECT_NE(destination.items[0].int64_ptr, source.items[0].int64_ptr);
    EXPECT_NE(destination.items[1].double_ptr, source.items[1].double_ptr);
    EXPECT_NE(destination.items[2].uint64_ptr, source.items[2].uint64_ptr);
    EXPECT_EQ((int64_t*)destination.items[0].int64_ptr,
              (int64_t*)&destination_storage[5]);
    EXPECT_EQ((double*)destination.items[1].double_ptr,
              (double*)&destination_storage[4]);
    EXPECT_EQ((uint64_t*)destination.items[2].uint64_ptr,
              (uint64_t*)&destination_storage[3]);
}

TEST(ItemRepresentation, ContainerPointersAreBitIdenticalItems) {
    Range range = {};
    range.type_id = LMD_TYPE_RANGE;
    range.start = 1;
    expect_raw_item_header(&range, LMD_TYPE_RANGE);
    EXPECT_EQ(it2range(p2it(&range))->start, 1);

    List array = {};
    array.type_id = LMD_TYPE_ARRAY;
    array.length = 2;
    expect_array_like_header(&array, LMD_TYPE_ARRAY);
    EXPECT_EQ(it2arr(p2it(&array))->length, 2);

    ArrayNum array_num = {};
    array_num.type_id = LMD_TYPE_ARRAY_NUM;
    array_num.length = 3;
    expect_raw_item_header(&array_num, LMD_TYPE_ARRAY_NUM);
    EXPECT_EQ(p2it(&array_num).array_num, &array_num);
    EXPECT_EQ(p2it(&array_num).array_num->length, 3);

    Map map = {};
    map.type_id = LMD_TYPE_MAP;
    map.data_cap = 4;
    expect_raw_item_header(&map, LMD_TYPE_MAP);
    EXPECT_EQ(it2map(p2it(&map))->data_cap, 4);

    VMap vmap = {};
    vmap.type_id = LMD_TYPE_VMAP;
    vmap.host_data = &array;
    expect_raw_item_header(&vmap, LMD_TYPE_VMAP);
    EXPECT_EQ(p2it(&vmap).vmap->host_data, &array);

    Element element = {};
    element.type_id = LMD_TYPE_ELEMENT;
    element.length = 5;
    expect_array_like_header(&element, LMD_TYPE_ELEMENT);
    EXPECT_EQ(it2elmt(p2it(&element))->length, 5);

    Object object = {};
    object.type_id = LMD_TYPE_OBJECT;
    object.data_cap = 6;
    expect_raw_item_header(&object, LMD_TYPE_OBJECT);
    EXPECT_EQ(it2obj(p2it(&object))->data_cap, 6);

    Type type = {};
    type.type_id = LMD_TYPE_TYPE;
    type.kind = 7;
    expect_raw_item_header(&type, LMD_TYPE_TYPE);
    EXPECT_EQ(p2it(&type).type->kind, 7);

    Function function = {};
    function.type_id = LMD_TYPE_FUNC;
    function.arity = 8;
    expect_raw_item_header(&function, LMD_TYPE_FUNC);
    EXPECT_EQ(p2it(&function).function->arity, 8);

    Path path = {};
    path.type_id = LMD_TYPE_PATH;
    path.flags = PATH_FLAG_META_LOADED;
    expect_raw_item_header(&path, LMD_TYPE_PATH);
    EXPECT_EQ(it2path(p2it(&path))->flags, PATH_FLAG_META_LOADED);
}

TEST_F(RuntimeItemRepresentation, HeapAllocatedHeadersAreBitIdenticalItems) {
    Range* range = (Range*)gc_heap_alloc(gc, sizeof(Range), LMD_TYPE_RANGE);
    ASSERT_NE(range, nullptr);
    range->type_id = LMD_TYPE_RANGE;
    range->start = 1;
    expect_raw_item_header(range, LMD_TYPE_RANGE);
    EXPECT_EQ(it2range(p2it(range))->start, 1);

    Array* array = (Array*)gc_heap_calloc(gc, sizeof(Array), LMD_TYPE_ARRAY);
    ASSERT_NE(array, nullptr);
    array->type_id = LMD_TYPE_ARRAY;
    array->length = 2;
    expect_array_like_header(array, LMD_TYPE_ARRAY);
    EXPECT_EQ(it2arr(p2it(array))->length, 2);

    ArrayNum* array_num = (ArrayNum*)gc_heap_calloc(gc, sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    ASSERT_NE(array_num, nullptr);
    array_num->type_id = LMD_TYPE_ARRAY_NUM;
    array_num->length = 3;
    expect_raw_item_header(array_num, LMD_TYPE_ARRAY_NUM);
    EXPECT_EQ(p2it(array_num).array_num->length, 3);

    Map* map = (Map*)gc_heap_calloc(gc, sizeof(Map), LMD_TYPE_MAP);
    ASSERT_NE(map, nullptr);
    map->type_id = LMD_TYPE_MAP;
    map->data_cap = 4;
    expect_raw_item_header(map, LMD_TYPE_MAP);
    EXPECT_EQ(it2map(p2it(map))->data_cap, 4);

    VMap* vmap = (VMap*)gc_heap_calloc(gc, sizeof(VMap), LMD_TYPE_VMAP);
    ASSERT_NE(vmap, nullptr);
    vmap->type_id = LMD_TYPE_VMAP;
    vmap->host_data = array;
    expect_raw_item_header(vmap, LMD_TYPE_VMAP);
    EXPECT_EQ(p2it(vmap).vmap->host_data, array);

    Element* element = (Element*)gc_heap_calloc(gc, sizeof(Element), LMD_TYPE_ELEMENT);
    ASSERT_NE(element, nullptr);
    element->type_id = LMD_TYPE_ELEMENT;
    element->length = 5;
    expect_array_like_header(element, LMD_TYPE_ELEMENT);
    EXPECT_EQ(it2elmt(p2it(element))->length, 5);

    Object* object = (Object*)gc_heap_calloc(gc, sizeof(Object), LMD_TYPE_OBJECT);
    ASSERT_NE(object, nullptr);
    object->type_id = LMD_TYPE_OBJECT;
    object->data_cap = 6;
    expect_raw_item_header(object, LMD_TYPE_OBJECT);
    EXPECT_EQ(it2obj(p2it(object))->data_cap, 6);

    Type* type = (Type*)gc_heap_calloc(gc, sizeof(Type), LMD_TYPE_TYPE);
    ASSERT_NE(type, nullptr);
    type->type_id = LMD_TYPE_TYPE;
    type->kind = TYPE_KIND_SIMPLE;
    expect_raw_item_header(type, LMD_TYPE_TYPE);
    EXPECT_EQ(p2it(type).type->kind, TYPE_KIND_SIMPLE);

    Function* function = (Function*)gc_heap_calloc(gc, sizeof(Function), LMD_TYPE_FUNC);
    ASSERT_NE(function, nullptr);
    function->type_id = LMD_TYPE_FUNC;
    function->arity = 8;
    function->ptr = (fn_ptr)item_repr_noop_fn;
    expect_raw_item_header(function, LMD_TYPE_FUNC);
    EXPECT_EQ(p2it(function).function->arity, 8);

    Path* path = (Path*)gc_heap_calloc(gc, sizeof(Path), LMD_TYPE_PATH);
    ASSERT_NE(path, nullptr);
    path->type_id = LMD_TYPE_PATH;
    path->flags = PATH_FLAG_META_LOADED;
    expect_raw_item_header(path, LMD_TYPE_PATH);
    EXPECT_EQ(it2path(p2it(path))->flags, PATH_FLAG_META_LOADED);
}

TEST(ItemRepresentation, NonPointerDiscriminatorWordsDoNotReadHeaders) {
    Item synthetic = {.item = ITEM_DBL_MASK | UINT64_C(0x0000000000001234)};

    EXPECT_NE(synthetic.item & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_NE(get_type_id(synthetic), LMD_TYPE_RAW_POINTER);
}

// ---------------------------------------------------------------------------
// Boxed `int` representation.
//
// These are written against the canonical encoder/accessors rather than raw
// bits, so they hold before and after the rotated inline-int cutover
// (`vibe/Lambda_Type_Int_Boxing.md`). A bit-level expectation here would have
// to be rewritten by the very change it is meant to protect.
// ---------------------------------------------------------------------------

namespace {
// Values every int representation must carry exactly. Band edges included
// because they are where a carrier-capacity bug shows up first.
const int64_t kIntRoundTripValues[] = {
    0, 1, -1, 2, -2, 3, -3, 42, -42, 255, 256, -256,
    32767, -32768, 2147483647, -2147483648LL,
    4503599627370496LL,   // 2^52
    -4503599627370496LL,
    9007199254740990LL,   // 2^53 - 2
    INT53_MAX, INT53_MIN,
    // Past the old carrier band: these are the values the retired compact
    // encoding could not hold at all (it returned ITEM_ERROR — the O1 defect).
    9007199254740992LL,   // 2^53
    -9007199254740992LL,
    18014398509481984LL,  // 2^54
    4611686018427387904LL,  // 2^62
    -4611686018427387904LL,
};
}  // namespace

TEST(ItemRepresentation, IntBoxRoundTripsThroughBothAccessors) {
    for (int64_t value : kIntRoundTripValues) {
        Item boxed = {.item = i2it(value)};
        EXPECT_EQ(get_type_id(boxed), LMD_TYPE_INT) << "value " << value;
        EXPECT_EQ(lambda_int_item_to_i64(boxed), value);
        EXPECT_EQ(lambda_int_item_value(boxed), (double)value);
        EXPECT_EQ(it2i(boxed), value);
    }
}

TEST(ItemRepresentation, IntEncodingIsCanonical) {
    // One value, one encoding: equal values must produce bit-identical Items,
    // and distinct values must never collide.
    for (int64_t value : kIntRoundTripValues) {
        EXPECT_EQ(i2it(value), i2it(value)) << "value " << value;
        for (int64_t other : kIntRoundTripValues) {
            if (other == value) continue;
            EXPECT_NE(i2it(value), i2it(other)) << value << " vs " << other;
        }
    }
}

TEST(ItemRepresentation, IntItemsStayOutOfDoubleSpace) {
    for (int64_t value : kIntRoundTripValues) {
        Item boxed = {.item = i2it(value)};
        EXPECT_EQ(boxed.item & ITEM_DBL_MASK, UINT64_C(0)) << "value " << value;
    }
}

TEST(ItemRepresentation, IntItemsNeverCollideWithInternalSentinels) {
    // The sentinel tag is reserved precisely so no value can alias it.
    for (int64_t value : kIntRoundTripValues) {
        Item boxed = {.item = i2it(value)};
        EXPECT_NE(boxed.item, ITEM_JS_DELETED_SENTINEL) << "value " << value;
        EXPECT_NE(boxed.item, ITEM_JS_ITER_DONE_SENTINEL) << "value " << value;
        EXPECT_NE(boxed.item, (uint64_t)ITEM_NULL) << "value " << value;
        EXPECT_NE(boxed.item, (uint64_t)ITEM_ERROR) << "value " << value;
    }
}

TEST(ItemRepresentation, IntPoisonIsSharedWithFloatAndClassifies) {
    // `int` and `float` share ONE representation for inf/nan: the ordinary
    // inline IEEE bits, not an int-tagged sentinel. So boxing the poison as an
    // int and boxing it as a float must land on the identical Item.
    Item inf = {.item = lambda_int_box_double(INFINITY)};
    Item neg_inf = {.item = lambda_int_box_double(-INFINITY)};
    Item nan_item = {.item = lambda_int_box_double(NAN)};

    auto raw_bits = [](double d) { uint64_t b; memcpy(&b, &d, sizeof(b)); return b; };
    EXPECT_EQ(inf.item, raw_bits(INFINITY));
    EXPECT_EQ(neg_inf.item, raw_bits(-INFINITY));
    EXPECT_EQ(nan_item.item, raw_bits(NAN));

    for (Item poison : {inf, neg_inf, nan_item}) {
        EXPECT_TRUE(lambda_item_is_merged_poison(poison.item));
        // Physically a double, so the DECODER reports float; `type()` surfaces
        // int separately (fn_type), which is what keeps integer-lowering sites
        // from ever receiving one.
        EXPECT_EQ(get_type_id(poison), LMD_TYPE_FLOAT);
        EXPECT_NE(poison.item & ITEM_DBL_MASK, UINT64_C(0));
        // Poison must never alias a finite value.
        for (int64_t value : kIntRoundTripValues) {
            EXPECT_NE(poison.item, i2it(value)) << "aliases " << value;
        }
    }
    EXPECT_NE(inf.item, neg_inf.item);
    EXPECT_NE(inf.item, nan_item.item);
    EXPECT_NE(neg_inf.item, nan_item.item);

    // Unboxing through the int lane preserves nan-ness -- the property the
    // retired sentinel could not hold.
    EXPECT_TRUE(std::isnan(lambda_int_unbox_double(nan_item.item)));
    EXPECT_EQ(lambda_int_unbox_double(inf.item), INFINITY);
    EXPECT_EQ(lambda_int_unbox_double(neg_inf.item), -INFINITY);
}

TEST(ItemRepresentation, IntStoresIntoContainerWithoutBorrowedStorage) {
    // The escape path that exposed the number-stack carrier defect: an int
    // stored into a container must own its value outright. A representation
    // that borrows frame-scoped storage would need `extra` tail words here and
    // would dangle once the producing frame unwound.
    const size_t count = sizeof(kIntRoundTripValues) / sizeof(kIntRoundTripValues[0]);
    Item storage[2 * sizeof(kIntRoundTripValues) / sizeof(kIntRoundTripValues[0])] = {};
    Array arr = {};
    arr.type_id = LMD_TYPE_ARRAY;
    arr.items = storage;
    arr.length = (int64_t)count;
    arr.capacity = (int64_t)(sizeof(storage) / sizeof(storage[0]));

    for (size_t i = 0; i < count; i++) {
        array_set(&arr, (int64_t)i, (Item){.item = i2it(kIntRoundTripValues[i])});
    }

    EXPECT_EQ(arr.extra, 0) << "int must not consume owned tail storage";
    for (size_t i = 0; i < count; i++) {
        Item read_back = arr.items[i];
        EXPECT_EQ(get_type_id(read_back), LMD_TYPE_INT);
        EXPECT_EQ(lambda_int_item_to_i64(read_back), kIntRoundTripValues[i]);
        // Stored form must be the boxed form unchanged — no rehoming happened.
        EXPECT_EQ(read_back.item, i2it(kIntRoundTripValues[i]));
    }
}

TEST(ItemRepresentation, SelfTaggedFloatEncoderKeepsInBandBitsImmediate) {
    double value = 1.5;
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));

    Item encoded = lambda_float_ptr_to_item(&value);

    EXPECT_EQ(encoded.item, bits);
    EXPECT_NE(encoded.item & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_EQ(get_type_id(encoded), LMD_TYPE_FLOAT);
    EXPECT_EQ(encoded.get_double(), value);
    EXPECT_EQ(it2d(encoded), value);
}

TEST(ItemRepresentation, SelfTaggedFloatEncoderPacksSignedZero) {
    double pos_value = 0.0;
    double neg_value = -0.0;
    Item pos_zero = lambda_float_ptr_to_item(&pos_value);
    Item neg_zero = lambda_float_ptr_to_item(&neg_value);

    EXPECT_EQ(pos_zero.item, ITEM_FLOAT_P0);
    EXPECT_EQ(neg_zero.item, ITEM_FLOAT_N0);
    EXPECT_EQ(get_type_id(pos_zero), LMD_TYPE_FLOAT);
    EXPECT_EQ(get_type_id(neg_zero), LMD_TYPE_FLOAT);
    EXPECT_FALSE(signbit(it2d(pos_zero)));
    EXPECT_TRUE(signbit(it2d(neg_zero)));
    EXPECT_TRUE(isinf(1.0 / it2d(pos_zero)));
    EXPECT_TRUE(isinf(1.0 / it2d(neg_zero)));
    EXPECT_GT(1.0 / it2d(pos_zero), 0.0);
    EXPECT_LT(1.0 / it2d(neg_zero), 0.0);
}

TEST(ItemRepresentation, SelfTaggedFloatEncoderPreservesInfAndNanPayloadBits) {
    double inf = INFINITY;
    uint64_t inf_bits = 0;
    memcpy(&inf_bits, &inf, sizeof(inf_bits));
    Item inf_item = lambda_float_ptr_to_item(&inf);
    EXPECT_EQ(inf_item.item, inf_bits);
    EXPECT_TRUE(isinf(it2d(inf_item)));

    uint64_t nan_bits = UINT64_C(0x7ff8000000001234);
    double nan_value = 0.0;
    memcpy(&nan_value, &nan_bits, sizeof(nan_value));
    Item nan_item = lambda_float_ptr_to_item(&nan_value);
    EXPECT_EQ(nan_item.item, nan_bits);
    EXPECT_TRUE(isnan(it2d(nan_item)));
}

TEST(ItemRepresentation, SelfTaggedFloatHelperBoxesOutOfBandPayloads) {
    double tiny = ldexp(1.0, -1074);
    uint64_t bits = 0;
    memcpy(&bits, &tiny, sizeof(bits));
    ASSERT_EQ(bits & ITEM_DBL_MASK, UINT64_C(0));

    Item encoded = lambda_float_ptr_to_item(&tiny);

    EXPECT_EQ(encoded.item, d2it(&tiny));
    EXPECT_EQ(get_type_id(encoded), LMD_TYPE_FLOAT);
    EXPECT_EQ(it2d(encoded), tiny);
}

TEST(ItemRepresentation, MirMemberAccessKeepsContainerItemUnmodified) {
    // use a test-private dump path: the default temp/mir_dump.txt is truncated and
    // rewritten by every concurrent debug lambda.exe run (e.g. test_lambda_gtest in
    // the parallel harness), which raced this test and made member_calls == 0 flaky.
    const char* dump_path = "temp/item_repr_mir_dump.txt";
    remove(dump_path);
    const ShellEnvEntry env[] = {
        {"LAMBDA_MIR_DUMP_PATH", dump_path},
        {NULL, NULL},
    };
    const char* args[] = {
        "./lambda.exe", "test/lambda/item_repr_container_member_load.ls", NULL,
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    // A child-only dump path prevents parallel lambda.exe launches from racing this assertion.
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(shell_result.exit_code, 0)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    FILE* f = fopen(dump_path, "r");
    // LAMBDA_MIR_DUMP_PATH is honored in every build now (MT2 of
    // vibe/Lambda_Design_MIR_Emission_Test.md), so a missing artifact means the
    // dump contract broke rather than that this build cannot produce one.
    ASSERT_NE(f, nullptr) << "no MIR artifact written to " << dump_path;

    char window[12][512] = {};
    int line_index = 0;
    int member_calls = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Member sites use fn_member and must receive the raw container Item.
        if (strstr(line, "call\tfn_member_p, fn_member,")) {
            member_calls++;
            for (int i = 0; i < 12; i++) {
                const char* prev = window[(line_index + i) % 12];
                // Raw container Items must flow into member lookup without pointer reconstruction.
                EXPECT_EQ(strstr(prev, "\tand\t"), nullptr) << prev;
                EXPECT_EQ(strstr(prev, "\txor\t"), nullptr) << prev;
                EXPECT_EQ(strstr(prev, "72057594037927935"), nullptr) << prev;
                EXPECT_EQ(strstr(prev, "ITEM_DBL_MASK"), nullptr) << prev;
            }
        }
        snprintf(window[line_index % 12], sizeof(window[0]), "%s", line);
        line_index++;
    }
    fclose(f);

    ASSERT_GT(member_calls, 0);
}
