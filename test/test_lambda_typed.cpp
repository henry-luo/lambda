#include <gtest/gtest.h>

#include "../lib/lambda_typed.hpp"

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
    EXPECT_EQ(*int64_match.ptr(), 42);
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
