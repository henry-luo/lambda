#include <gtest/gtest.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda.hpp"
#include "../lambda/js/js_runtime.h"

extern "C" {
#include "../lib/gc/gc_heap.h"
}

namespace {

static void expect_raw_item_header(void* ptr, TypeId expected_type) {
    assert_raw_item_header(ptr, expected_type);
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

#ifdef LAMBDA_SELF_TAG_FLOAT
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
#endif

TEST(ItemRepresentation, MirMemberAccessKeepsContainerItemUnmodified) {
    int rc = system("./lambda.exe test/lambda/item_repr_container_member_load.ls > temp/item_repr_mir_stdout.txt");
    ASSERT_EQ(rc, 0);

    FILE* f = fopen("temp/mir_dump.txt", "r");
    ASSERT_NE(f, nullptr);

    char window[12][512] = {};
    int line_index = 0;
    int member_calls = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\tcall\tfn_member_p, fn_member,")) {
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
