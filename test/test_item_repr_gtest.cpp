#include <gtest/gtest.h>

#include "../lambda/lambda.hpp"
#include "../lambda/js/js_runtime.h"

namespace {

static void expect_raw_item_header(void* ptr, TypeId expected_type) {
    assert_raw_item_pointer(ptr);
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

TEST(ItemRepresentation, NonPointerDiscriminatorWordsDoNotReadHeaders) {
    Item synthetic = {.item = ITEM_DBL_MASK | UINT64_C(0x0000000000001234)};

    EXPECT_NE(synthetic.item & ITEM_DBL_MASK, UINT64_C(0));
    EXPECT_NE(get_type_id(synthetic), LMD_TYPE_RAW_POINTER);
}
