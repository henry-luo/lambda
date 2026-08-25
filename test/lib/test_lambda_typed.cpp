#include <gtest/gtest.h>

#include "../../lambda/core/lambda_typed.hpp"
#include "../../lambda/js/js_props.h"

namespace {

template<class T>
T&& test_declval();

template<TypeId T>
class HasItemTag {
    template<TypeId U>
    static char test(typename lam::ItemTagToType<U>::type*);

    template<TypeId>
    static long test(...);

public:
    enum { value = sizeof(test<T>(nullptr)) == sizeof(char) };
};

template<class Witness>
class CanAsArray {
    template<class W>
    static char test(int, decltype(lam::as_array(test_declval<W>()))* = 0);

    template<class>
    static long test(...);

public:
    enum { value = sizeof(test<Witness>(0)) == sizeof(char) };
};

struct LambdaVisitKind {
    int operator()(lam::ItemOf<LMD_TYPE_ARRAY>) { return 1; }
    int operator()(lam::ItemOf<LMD_TYPE_MAP>) { return 2; }
    int operator()(lam::ItemOf<LMD_TYPE_BOOL>) { return 3; }
    int operator()(Item) { return 0; }

    template<TypeId T>
    int operator()(lam::ItemOf<T>) { return 9; }
};

struct LambdaVisitTag {
    template<TypeId Tag>
    TypeId operator()(lam::ItemOf<Tag>) { return Tag; }

    TypeId operator()(Item) { return LMD_TYPE_COUNT; }
};

static void set_test_shape_name(char* out, int index) {
    out[0] = 'k';
    out[1] = (char)('0' + ((index / 10) % 10));
    out[2] = (char)('0' + (index % 10));
    out[3] = '\0';
}

} // namespace

TEST(LambdaTypedItem, PreservesAbiSize) {
    static_assert(sizeof(lam::ItemOf<LMD_TYPE_ARRAY>) == sizeof(Item),
                  "ItemOf must remain ABI-equivalent to raw Item");
    static_assert(sizeof(lam::ItemOf<LMD_TYPE_STRING>) == sizeof(Item),
                  "tagged scalar witnesses must remain raw Item sized");
    static_assert(sizeof(lam::GcPtr<Array>) == sizeof(Array*),
                  "borrowed GC pointers must remain pointer sized");
    static_assert(sizeof(lam::ShapeRef) == sizeof(ShapeEntry*),
                  "shape references must remain raw pointer sized");
    static_assert(sizeof(lam::HoleSentinel) == sizeof(Item),
                  "hole sentinels must remain raw Item sized");

    SUCCEED();
}

TEST(LambdaTypedItem, TagSpecializationsAreExplicit) {
    static_assert(HasItemTag<LMD_TYPE_ARRAY>::value,
                  "known Lambda tags should be mapped");
    static_assert(HasItemTag<LMD_TYPE_STRING>::value,
                  "known scalar tags should be mapped");
    static_assert(!HasItemTag<LMD_TYPE_COUNT>::value,
                  "sentinel tags should not be mapped");

    SUCCEED();
}

TEST(LambdaTypedItem, CanonicalTypeTagsHaveTypedWitnesses) {
#define ASSERT_TYPED_ITEM_TAG(tag) \
    static_assert(HasItemTag<tag>::value, "canonical runtime tags need typed witnesses")
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_RAW_POINTER);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_NULL);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_UNDEFINED);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_BOOL);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_NUM_SIZED);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_INT);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_INT64);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_UINT64);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_FLOAT);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_DECIMAL);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_DTIME);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_SYMBOL);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_STRING);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_BINARY);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_PATH);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_RANGE);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_ARRAY_NUM);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_ARRAY);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_MAP);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_VMAP);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_ELEMENT);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_OBJECT);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_TYPE);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_FUNC);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_ANY);
    ASSERT_TYPED_ITEM_TAG(LMD_TYPE_ERROR);
#undef ASSERT_TYPED_ITEM_TAG

    SUCCEED();
}

TEST(LambdaTypedItem, AsMatchesOnlyTheRuntimeTag) {
    Array arr;
    arr.type_id = LMD_TYPE_ARRAY;
    arr.length = 0;
    arr.items = nullptr;

    Map map;
    map.type_id = LMD_TYPE_MAP;
    map.type = nullptr;
    map.data = nullptr;

    Item arr_item;
    arr_item.array = &arr;
    Item map_item;
    map_item.map = &map;

    auto arr_match = lam::as<LMD_TYPE_ARRAY>(arr_item);
    ASSERT_TRUE((bool)arr_match);
    EXPECT_EQ(arr_match.ptr(), &arr);
    EXPECT_FALSE((bool)lam::as<LMD_TYPE_ARRAY>(map_item));
    EXPECT_TRUE((bool)lam::as<LMD_TYPE_MAP>(map_item));
}

TEST(LambdaTypedItem, ReadsInlineAndTaggedScalars) {
    Item bool_item;
    bool_item.item = b2it(BOOL_TRUE);
    auto bool_match = lam::as<LMD_TYPE_BOOL>(bool_item);
    ASSERT_TRUE((bool)bool_match);
    EXPECT_TRUE(bool_match.value());

    Item int_item;
    int_item.item = i2it(-37);
    auto int_match = lam::as<LMD_TYPE_INT>(int_item);
    ASSERT_TRUE((bool)int_match);
    EXPECT_EQ(int_match.value(), -37);

    int64_t boxed = 42;
    Item int64_item;
    int64_item.item = l2it(&boxed);
    auto int64_match = lam::as<LMD_TYPE_INT64>(int64_item);
    ASSERT_TRUE((bool)int64_match);
    EXPECT_EQ(int64_match.value(), 42);
}

TEST(LambdaTypedItem, PointerFactoriesPreserveScalarAndContainerEncodings) {
    Array arr = {};
    arr.type_id = LMD_TYPE_ARRAY;
    uint64_t wide_unsigned = UINT64_MAX;
    double wide_float = 3.25;

    auto array_witness = lam::ItemOf<LMD_TYPE_ARRAY>::from_ptr(&arr);
    EXPECT_EQ(array_witness.raw().item, (uint64_t)(uintptr_t)&arr);
    EXPECT_EQ(array_witness.ptr(), &arr);
    EXPECT_EQ(lam::as<LMD_TYPE_ARRAY>(array_witness.raw()).ptr(), &arr);

    auto uint_witness = lam::ItemOf<LMD_TYPE_UINT64>::from_ptr(&wide_unsigned);
    EXPECT_EQ(get_type_id(uint_witness.raw()), LMD_TYPE_UINT64);
    EXPECT_EQ(uint_witness.ptr(), &wide_unsigned);
    EXPECT_EQ(lam::as<LMD_TYPE_UINT64>(uint_witness.raw()).ptr(), &wide_unsigned);

    auto float_witness = lam::ItemOf<LMD_TYPE_FLOAT>::from_ptr(&wide_float);
    EXPECT_EQ(get_type_id(float_witness.raw()), LMD_TYPE_FLOAT);
    EXPECT_EQ(float_witness.ptr(), &wide_float);
    EXPECT_DOUBLE_EQ(lam::as<LMD_TYPE_FLOAT>(float_witness.raw()).value(), 3.25);
}

TEST(LambdaTypedItem, FloatAndSizedNumericViewsPreserveCanonicalEncodings) {
    double positive_zero = 0.0;
    double negative_zero = -0.0;
    double ordinary = 3.25;
    Item plus_zero = lambda_float_ptr_to_item(&positive_zero);
    Item minus_zero = lambda_float_ptr_to_item(&negative_zero);
    Item ordinary_float = lambda_float_ptr_to_item(&ordinary);

    EXPECT_EQ(plus_zero.item, ITEM_FLOAT_P0);
    EXPECT_EQ(minus_zero.item, ITEM_FLOAT_N0);
    EXPECT_DOUBLE_EQ(lam::as<LMD_TYPE_FLOAT>(plus_zero).value(), 0.0);
    EXPECT_DOUBLE_EQ(lam::as<LMD_TYPE_FLOAT>(minus_zero).value(), -0.0);
    EXPECT_DOUBLE_EQ(lam::as<LMD_TYPE_FLOAT>(ordinary_float).value(), 3.25);

    Item signed_sized = {.item = i8_to_item(-7)};
    Item unsigned_sized = {.item = u32_to_item(UINT32_MAX)};
    auto signed_match = lam::as<LMD_TYPE_NUM_SIZED>(signed_sized);
    auto unsigned_match = lam::as<LMD_TYPE_NUM_SIZED>(unsigned_sized);
    ASSERT_TRUE((bool)signed_match);
    ASSERT_TRUE((bool)unsigned_match);
    EXPECT_EQ(signed_match.value().get_num_type(), NUM_INT8);
    EXPECT_EQ(signed_match.value().get_num_sized_as_int64(), -7);
    EXPECT_EQ(unsigned_match.value().get_num_type(), NUM_UINT32);
    EXPECT_EQ(unsigned_match.value().get_u32(), UINT32_MAX);
}

TEST(LambdaTypedItem, GroupAccessorsRejectWrongStorageAtCompileTime) {
    typedef lam::ItemOf<LMD_TYPE_ARRAY> ArrayWitness;
    typedef lam::ItemOf<LMD_TYPE_ELEMENT> ElementWitness;
    typedef lam::ItemOf<LMD_TYPE_MAP> MapWitness;
    typedef lam::ItemOf<LMD_TYPE_ARRAY_NUM> ArrayNumWitness;

    static_assert(lam::IsArrayLike<LMD_TYPE_ARRAY>::value,
                  "array should be array-like");
    static_assert(lam::IsArrayLike<LMD_TYPE_ARRAY_NUM>::value,
                  "numeric arrays should be array-like");
    static_assert(lam::IsArrayLike<LMD_TYPE_ELEMENT>::value,
                  "elements should be array-like through their List prefix");
    static_assert(lam::IsMapLike<LMD_TYPE_ELEMENT>::value,
                  "elements should still be treated as map-like for attributes");
    static_assert(CanAsArray<ArrayWitness>::value,
                  "arrays have Array storage");
    static_assert(CanAsArray<ElementWitness>::value,
                  "elements have Array/List prefix storage");
    static_assert(!CanAsArray<MapWitness>::value,
                  "maps are not array storage");
    static_assert(!CanAsArray<ArrayNumWitness>::value,
                  "numeric arrays use ArrayNum storage, not Array storage");

    SUCCEED();
}

TEST(LambdaTypedItem, VisitDispatchesTypedWitnesses) {
    Array arr;
    arr.type_id = LMD_TYPE_ARRAY;
    arr.length = 0;
    arr.items = nullptr;

    Map map;
    map.type_id = LMD_TYPE_MAP;
    map.type = nullptr;
    map.data = nullptr;

    Item arr_item;
    arr_item.array = &arr;
    Item map_item;
    map_item.map = &map;
    Item bool_item;
    bool_item.item = b2it(BOOL_TRUE);

    EXPECT_EQ(lam::visit(arr_item, LambdaVisitKind()), 1);
    EXPECT_EQ(lam::visit(map_item, LambdaVisitKind()), 2);
    EXPECT_EQ(lam::visit(bool_item, LambdaVisitKind()), 3);
}

TEST(LambdaTypedItem, VisitDispatchesCanonicalScalarAndContainerTags) {
    int64_t signed_value = INT64_MAX;
    uint64_t unsigned_value = UINT64_MAX;
    double float_value = 3.25;
    Array arr = {};
    arr.type_id = LMD_TYPE_ARRAY;
    Item signed_item = {.item = l2it(&signed_value)};
    Item unsigned_item = {.item = u64_to_item(&unsigned_value)};
    Item float_item = lambda_float_ptr_to_item(&float_value);
    Item array_item = {.array = &arr};
    Item undefined_item = {.item = ITEM_JS_UNDEFINED};

    EXPECT_EQ(lam::visit(signed_item, LambdaVisitTag()), LMD_TYPE_INT64);
    EXPECT_EQ(lam::visit(unsigned_item, LambdaVisitTag()), LMD_TYPE_UINT64);
    EXPECT_EQ(lam::visit(float_item, LambdaVisitTag()), LMD_TYPE_FLOAT);
    EXPECT_EQ(lam::visit(array_item, LambdaVisitTag()), LMD_TYPE_ARRAY);
    EXPECT_EQ(lam::visit(undefined_item, LambdaVisitTag()), LMD_TYPE_UNDEFINED);
}

TEST(LambdaTypedItem, ShapeRefBorrowsAndAdvancesShapeEntries) {
    ShapeEntry first = {};
    ShapeEntry second = {};
    first.next = &second;

    lam::ShapeRef shape = lam::shape_borrow(&first);
    ASSERT_TRUE((bool)shape);
    EXPECT_EQ(shape.get(), &first);

    shape = lam::shape_next(shape);
    ASSERT_TRUE((bool)shape);
    EXPECT_EQ(shape.get(), &second);

    shape = lam::shape_next(shape);
    EXPECT_FALSE((bool)shape);
}

TEST(LambdaTypedItem, TypeMapHashLookupFindsOverflowShapeEntries) {
    TypeMap tm = {};
    ShapeEntry entries[TYPEMAP_HASH_CAPACITY + 1] = {};
    StrView names[TYPEMAP_HASH_CAPACITY + 1] = {};
    char key_storage[TYPEMAP_HASH_CAPACITY + 1][4] = {};

    for (int i = 0; i <= TYPEMAP_HASH_CAPACITY; i++) {
        set_test_shape_name(key_storage[i], i);
        names[i].str = key_storage[i];
        names[i].length = 3;
        entries[i].name = &names[i];
        entries[i].byte_offset = i;
        if (i > 0) entries[i - 1].next = &entries[i];
        typemap_hash_insert(&tm, &entries[i]);
    }
    tm.shape = &entries[0];
    tm.last = &entries[TYPEMAP_HASH_CAPACITY];

    EXPECT_EQ(tm.field_count, TYPEMAP_HASH_CAPACITY);
    EXPECT_EQ(typemap_hash_lookup(&tm, key_storage[TYPEMAP_HASH_CAPACITY], 3),
              &entries[TYPEMAP_HASH_CAPACITY]);
}

TEST(LambdaTypedItem, TypeMapHashLookupByNameIdUsesCachedHash) {
    TypeMap tm = {};
    ShapeEntry entries[4] = {};
    StrView names[4] = {};
    char key_storage[4][4] = {};

    for (int i = 0; i < 4; i++) {
        set_test_shape_name(key_storage[i], i + 10);
        names[i].str = key_storage[i];
        names[i].length = 3;
        entries[i].name = &names[i];
        entries[i].name_id = (NameId)(0x100 + i);
        entries[i].name_hash = typemap_name_hash(key_storage[i], 3);
        if (i > 0) entries[i - 1].next = &entries[i];
        if (i == 0) tm.shape = &entries[i];
        tm.last = &entries[i];
        tm.length++;
        typemap_hash_insert(&tm, &entries[i]);
    }

    EXPECT_EQ(typemap_hash_lookup_by_name_id(&tm, entries[2].name_id,
        entries[2].name_hash), &entries[2]);
}

TEST(LambdaTypedItem, TypeMapHashLookupByNameIdFallsBackWhenTableIsFull) {
    TypeMap tm = {};
    ShapeEntry entries[TYPEMAP_HASH_CAPACITY + 1] = {};
    StrView names[TYPEMAP_HASH_CAPACITY + 1] = {};
    char key_storage[TYPEMAP_HASH_CAPACITY + 1][4] = {};

    for (int i = 0; i <= TYPEMAP_HASH_CAPACITY; i++) {
        set_test_shape_name(key_storage[i], i);
        names[i].str = key_storage[i];
        names[i].length = 3;
        entries[i].name = &names[i];
        entries[i].name_id = (NameId)(0x200 + i);
        entries[i].name_hash = typemap_name_hash(key_storage[i], 3);
        if (i > 0) entries[i - 1].next = &entries[i];
        if (i == 0) tm.shape = &entries[i];
        tm.last = &entries[i];
        typemap_hash_insert(&tm, &entries[i]);
    }

    EXPECT_EQ(typemap_hash_lookup_by_name_id(&tm,
        entries[TYPEMAP_HASH_CAPACITY].name_id,
        entries[TYPEMAP_HASH_CAPACITY].name_hash),
        &entries[TYPEMAP_HASH_CAPACITY]);
}

TEST(LambdaTypedItem, TypeMapHashOwnedInsertGrowsPastInlineTable) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    enum { count = TYPEMAP_HASH_CAPACITY + 1 };
    TypeMap tm = {};
    ShapeEntry entries[count] = {};
    StrView names[count] = {};
    char key_storage[count][4] = {};

    for (int i = 0; i < count; i++) {
        set_test_shape_name(key_storage[i], i);
        names[i].str = key_storage[i];
        names[i].length = 3;
        entries[i].name = &names[i];
        entries[i].byte_offset = i;
        if (i > 0) entries[i - 1].next = &entries[i];
        if (!tm.shape) tm.shape = &entries[i];
        tm.last = &entries[i];
        tm.length++;
        typemap_hash_insert_owned(&tm, &entries[i], pool);
    }

    EXPECT_NE(tm.field_index_dynamic, nullptr);
    EXPECT_GT(typemap_hash_capacity(&tm), TYPEMAP_HASH_CAPACITY);
    EXPECT_EQ(tm.field_count, count);
    EXPECT_EQ(typemap_hash_lookup(&tm, key_storage[count - 1], 3),
              &entries[count - 1]);

    pool_destroy(pool);
}

TEST(LambdaTypedItem, TypeMapHashLookupFallsBackToLastShapeMatch) {
    TypeMap tm = {};
    ShapeEntry first = {};
    ShapeEntry second = {};
    StrView first_name = {};
    StrView second_name = {};
    first_name.str = "dup";
    first_name.length = 3;
    second_name.str = "dup";
    second_name.length = 3;

    first.name = &first_name;
    first.byte_offset = 8;
    first.next = &second;
    second.name = &second_name;
    second.byte_offset = 16;
    tm.shape = &first;
    tm.last = &second;

    EXPECT_EQ(typemap_hash_lookup(&tm, "dup", 3), &second);
}

TEST(LambdaTypedItem, JsAccessorSetOutcomeSeparatesDispatch) {
    static_assert(JS_SET_NOT_FOUND == 0,
                  "no-accessor status stays ABI-stable for existing callers");
    // Data writes now return their Item directly; the only setter status is
    // the accessor dispatch outcome, which avoids a parallel error channel.
    static_assert(JS_SET_DISPATCHED != JS_SET_NOT_FOUND,
                  "a dispatched setter must not look like a missing accessor");
    EXPECT_NE(JS_SET_DISPATCHED, JS_SET_NOT_FOUND);
}

TEST(LambdaTypedItem, HoleSentinelWrapsDeletedSlotPayload) {
    Item hole = lam::hole_sentinel_item();

    EXPECT_EQ(hole.item, lam::HoleSentinel::raw_value());
    EXPECT_TRUE(lam::is_hole_sentinel(hole));

    Item ordinary_int;
    ordinary_int.item = i2it(42);
    EXPECT_FALSE(lam::is_hole_sentinel(ordinary_int));

    lam::HoleSentinel witness = lam::HoleSentinel::from_raw(hole);
    EXPECT_EQ(witness.raw().item, hole.item);
}

TEST(LambdaTypedItem, ItemOrErrorWrapsRetStructs) {
    Map map;
    map.type_id = LMD_TYPE_MAP;
    map.type = nullptr;
    map.data = nullptr;

    RetMap map_ret = rm_ok(&map);
    auto map_result = lam::item_or_error(map_ret);
    static_assert(sizeof(map_result) == sizeof(RetMap),
                  "ItemOrError must remain Ret* ABI sized");
    EXPECT_TRUE(map_result.ok());
    EXPECT_FALSE(map_result.has_error());
    EXPECT_EQ(map_result.error(), nullptr);
    EXPECT_EQ(map_result.value(), &map);
    EXPECT_EQ(map_result.raw().value, &map);

    LambdaError* err = (LambdaError*)0x1234;
    RetArray array_ret = ra_err(err);
    auto array_result = lam::item_or_error(array_ret);
    EXPECT_FALSE(array_result.ok());
    EXPECT_TRUE(array_result.has_error());
    EXPECT_EQ(array_result.error(), err);
    EXPECT_EQ(array_result.raw().value, nullptr);
}

TEST(LambdaTypedItem, Uint8ClampedArrayNumUsesReservedNibbleSlot) {
    EXPECT_EQ(ELEM_UINT8_CLAMPED, 0xE0);
    EXPECT_EQ(ELEM_NUM_COUNT, 14);
    EXPECT_EQ(ELEM_TYPE_SIZE[ELEM_UINT8_CLAMPED >> 4], 1);
}

TEST(LambdaTypedItem, ExternalArrayNumViewAliasesRawBufferWithRawBase) {
    Map base = {};
    base.type_id = LMD_TYPE_MAP;
    base.map_kind = MAP_KIND_ARRAYBUFFER;

    uint8_t raw[8] = {};
    ArrayNum view = {};
    uint8_t shape_storage[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    ArrayNumShape* shape = (ArrayNumShape*)shape_storage;

    ASSERT_TRUE(array_num_init_external_view(&view, shape, &base, raw,
                                             ELEM_UINT16, 2, 3, true));
    EXPECT_EQ(view.type_id, LMD_TYPE_ARRAY_NUM);
    EXPECT_EQ(view.get_elem_type(), ELEM_UINT16);
    EXPECT_TRUE(view.is_ndim);
    EXPECT_TRUE(view.is_view);
    EXPECT_TRUE(view.is_mutable_view);
    EXPECT_EQ(view.data, raw + 2);
    EXPECT_EQ(view.length, 3);
    EXPECT_EQ(view.capacity, 3);
    EXPECT_EQ(view.extra, (int64_t)(uintptr_t)shape);
    EXPECT_EQ(shape->ndim, 1);
    EXPECT_TRUE(shape->is_c_contig);
    EXPECT_TRUE(shape->is_f_contig);
    EXPECT_EQ(shape->offset, 1);
    EXPECT_EQ(shape->base, (void*)&base);
    EXPECT_EQ(array_num_shape_dims(shape)[0], 3);
    EXPECT_EQ(array_num_shape_strides(shape)[0], 1);

    ArrayNum bad_view = {};
    uint8_t bad_shape_storage[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    ArrayNumShape* bad_shape = (ArrayNumShape*)bad_shape_storage;
    EXPECT_FALSE(array_num_init_external_view(&bad_view, bad_shape, &base, raw,
                                              ELEM_UINT16, 1, 1, true));
}
