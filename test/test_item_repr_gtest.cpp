#include <gtest/gtest.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_property_attrs.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/runtime/side_stack.h"
#include "../lib/memtrack.h"

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

static String* shape_transition_test_name(Pool* pool, const char* text) {
    size_t length = strlen(text);
    String* name = (String*)pool_calloc(pool, sizeof(String) + length + 1);
    if (!name) return nullptr;
    name->len = (uint32_t)length;
    name->is_ascii = 1;
    memcpy(name->chars, text, length + 1);
    return name;
}

static ShapeEntry* shape_transition_find_field_in_type(TypeMap* type, String* name) {
    if (!type || !name) return nullptr;
    ShapeEntry* field = typemap_hash_lookup(type, name->chars, (int)name->len);
    if (field) return field;
    // freshly assembled Element shapes do not publish a hash table until a
    // runtime shape rebuild; the linked chain is still the authoritative data.
    for (field = type->shape; field; field = field->next) {
        if (field->name && field->name->length == name->len &&
                memcmp(field->name->str, name->chars, name->len) == 0) {
            return field;
        }
    }
    return nullptr;
}

static ShapeEntry* shape_transition_find_field(Map* map, String* name) {
    if (!map || !map->type) return nullptr;
    return shape_transition_find_field_in_type((TypeMap*)map->type, name);
}

static Item shape_transition_read_field(Map* map, String* name) {
    ShapeEntry* field = shape_transition_find_field(map, name);
    return field ? map_shape_field_to_item(map->data, field) : ItemError;
}

static Item shape_transition_read_typed_field(TypeMap* type, void* data, String* name) {
    ShapeEntry* field = shape_transition_find_field_in_type(type, name);
    return field ? map_field_to_item((char*)data + field->byte_offset,
        shape_entry_storage_type_id(field)) : ItemError;
}

class RuntimeShapeTransition : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Input input = {};
    EvalContext eval = {};
    String* fixed_left = nullptr;
    String* fixed_right = nullptr;
    String* late_flag = nullptr;
    String* after_flag = nullptr;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        input.pool = pool;
        input.type_list = arraylist_new(8);
        ASSERT_NE(input.type_list, nullptr);
        eval.pool = pool;
        eval.type_list = input.type_list;
        ASSERT_TRUE(eval_context_init(&eval));
        heap_init();
        ASSERT_NE(eval.heap, nullptr);
        ASSERT_TRUE(js_runtime_state_init(&eval));
        lambda_stack_init();
        eval.stack_limit = _lambda_stack_limit;
        ASSERT_TRUE(lambda_side_stack_bind());
        js_runtime_set_input(&input);

        fixed_left = shape_transition_test_name(pool, "fixedLeft");
        fixed_right = shape_transition_test_name(pool, "fixedRight");
        late_flag = shape_transition_test_name(pool, "lateFlag");
        after_flag = shape_transition_test_name(pool, "afterFlag");
        ASSERT_NE(fixed_left, nullptr);
        ASSERT_NE(fixed_right, nullptr);
        ASSERT_NE(late_flag, nullptr);
        ASSERT_NE(after_flag, nullptr);
    }

    void TearDown() override {
        js_runtime_set_input(nullptr);
        js_runtime_state_destroy_context();
        lambda_stack_cleanup();
        if (eval.heap) {
            heap_destroy();
            eval.heap = nullptr;
        }
        EXPECT_TRUE(eval_context_shutdown(&eval));
        arraylist_free(input.type_list);
        input.type_list = nullptr;
        pool_destroy(pool);
        pool = nullptr;
    }

    Map* make_transition_map(void) {
        Map* map = map_pooled(pool);
        if (!map) return nullptr;
        map->map_kind = MAP_KIND_PLAIN;

        map_put(map, fixed_left, {.item = i2it(42)}, &input);
        map_put(map, fixed_right, {.item = i2it(7)}, &input);
        TypeMap* constructor_shape = (TypeMap*)map->type;
        if (!constructor_shape || !constructor_shape->shape ||
                !constructor_shape->shape->next) {
            return nullptr;
        }

        ShapeEntry** slots = (ShapeEntry**)pool_calloc(pool, 2 * sizeof(ShapeEntry*));
        if (!slots) return nullptr;
        slots[0] = constructor_shape->shape;
        slots[1] = constructor_shape->shape->next;
        constructor_shape->slot_entries = slots;
        constructor_shape->slot_count = 2;
        constructor_shape->is_shared_constructor_shape = true;

        map_put(map, late_flag, {.item = b2it(BOOL_FALSE)}, &input);
        map_put(map, after_flag, {.item = i2it(123456)}, &input);
        return map;
    }

    Element* make_transition_element(void) {
        Element* element = elmt_pooled(pool);
        if (!element) return nullptr;

        TypeElmt* element_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
        if (!element_type) return nullptr;
        element_type->type_id = LMD_TYPE_ELEMENT;
        element_type->type_index = -1;
        element_type->name.str = "repr-element";
        element_type->name.length = 12;
        element->type = element_type;

        element->items = (Item*)pool_calloc(pool, 6 * sizeof(Item));
        if (!element->items) return nullptr;
        element->length = 2;
        element->capacity = 6;

        elmt_put(element, fixed_left, {.item = i2it(42)}, pool);
        elmt_put(element, fixed_right, {.item = i2it(7)}, pool);
        elmt_put(element, late_flag, {.item = b2it(BOOL_FALSE)}, pool);
        elmt_put(element, after_flag, {.item = i2it(123456)}, pool);
        return element;
    }

    void expect_fixed_prefix(Map* map) {
        TypeMap* shape = (TypeMap*)map->type;
        ASSERT_NE(shape, nullptr);
        EXPECT_EQ(typemap_fixed_slot_prefix_count(shape), 2);

        ShapeEntry* left = shape_transition_find_field(map, fixed_left);
        ShapeEntry* right = shape_transition_find_field(map, fixed_right);
        ASSERT_NE(left, nullptr);
        ASSERT_NE(right, nullptr);
        EXPECT_TRUE(typemap_entry_uses_fixed_slot(shape, left));
        EXPECT_TRUE(typemap_entry_uses_fixed_slot(shape, right));
        EXPECT_EQ(left->byte_offset, 0);
        EXPECT_EQ(right->byte_offset, (int64_t)sizeof(void*));
        EXPECT_EQ(lambda_int_item_value(shape_transition_read_field(map, fixed_right)), 7);
    }

    void expect_packed_sibling(Map* map, TypeId late_type, int64_t late_offset,
                               int64_t after_offset) {
        TypeMap* shape = (TypeMap*)map->type;
        ShapeEntry* late = shape_transition_find_field(map, late_flag);
        ShapeEntry* after = shape_transition_find_field(map, after_flag);
        ASSERT_NE(late, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(late->type->type_id, late_type);
        EXPECT_EQ(late->byte_offset, late_offset);
        EXPECT_EQ(after->byte_offset, after_offset);
        EXPECT_FALSE(typemap_entry_uses_fixed_slot(shape, late));
        EXPECT_FALSE(typemap_entry_uses_fixed_slot(shape, after));
        EXPECT_EQ(lambda_int_item_value(shape_transition_read_field(map, after_flag)), 123456);
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
    container.nominal_reserved = 1;
    EXPECT_EQ(container.flags, CONTAINER_FLAG_NOMINAL_RESERVED);
    container.has_ctor_reserved = 1;
    EXPECT_EQ(container.flags,
              CONTAINER_FLAG_NOMINAL_RESERVED | CONTAINER_FLAG_CTOR_RESERVED);
    container.nominal_reserved = 0;
    EXPECT_EQ(container.flags, CONTAINER_FLAG_CTOR_RESERVED);
    container.flags = CONTAINER_FLAG_NOMINAL_RESERVED;
    EXPECT_TRUE(container.nominal_reserved);
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

TEST_F(RuntimeShapeTransition, PackedAssignmentsRebuildWithoutMovingFixedSlots) {
    Map* map = make_transition_map();
    ASSERT_NE(map, nullptr);
    Item map_item = {.map = map};

    TypeMap* initial_shape = (TypeMap*)map->type;
    ASSERT_NE(initial_shape, nullptr);
    EXPECT_TRUE(typemap_is_shared_shape(initial_shape));
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_BOOL, 2 * (int64_t)sizeof(void*),
                           2 * (int64_t)sizeof(void*) + 1);
    EXPECT_EQ(initial_shape->byte_size, 2 * (int64_t)sizeof(void*) + 1 +
        (int64_t)sizeof(void*));

    // Fixed constructor slots may use a cached type transition because their
    // addresses stay slot-indexed even when the assigned value's tag changes.
    Map fixed_value = {};
    fixed_value.type_id = LMD_TYPE_MAP;
    fn_map_set(map_item, {.item = s2it(fixed_left)}, {.map = &fixed_value});
    EXPECT_TRUE(((TypeMap*)map->type)->is_transition_shared_shape);
    expect_fixed_prefix(map);
    EXPECT_EQ(it2map(shape_transition_read_field(map, fixed_left)), &fixed_value);
    expect_packed_sibling(map, LMD_TYPE_BOOL, 2 * (int64_t)sizeof(void*),
                           2 * (int64_t)sizeof(void*) + 1);

    TypeMap* packed_shape = (TypeMap*)map->type;
    ShapeEntry* packed_entry = shape_transition_find_field(map, late_flag);
    ASSERT_NE(packed_entry, nullptr);
    // `lateFlag` is followed by `afterFlag`; retaining its old one-byte offset
    // for a Map write would overwrite the packed sibling. The cache must defer
    // to the rebuild path instead of publishing a retag-only transition.
    EXPECT_EQ(js_typemap_transition_for_type(map_item, packed_entry,
        packed_entry->name_id, LMD_TYPE_MAP), nullptr);
    EXPECT_EQ(map->type, packed_shape);

    Map replacement = {};
    replacement.type_id = LMD_TYPE_MAP;
    fn_map_set(map_item, {.item = s2it(late_flag)}, {.map = &replacement});
    EXPECT_FALSE(typemap_is_shared_shape((TypeMap*)map->type));
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_MAP, 2 * (int64_t)sizeof(void*),
                           3 * (int64_t)sizeof(void*));
    EXPECT_EQ(((TypeMap*)map->type)->byte_size, 4 * (int64_t)sizeof(void*));
    EXPECT_EQ(it2map(shape_transition_read_field(map, late_flag)), &replacement);

    fn_map_set(map_item, {.item = s2it(late_flag)}, {.item = b2it(BOOL_TRUE)});
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_BOOL, 2 * (int64_t)sizeof(void*),
                           2 * (int64_t)sizeof(void*) + 1);
    EXPECT_EQ(((TypeMap*)map->type)->byte_size, 2 * (int64_t)sizeof(void*) + 1 +
        (int64_t)sizeof(void*));
    EXPECT_EQ(shape_transition_read_field(map, late_flag).item, b2it(BOOL_TRUE));

    fn_map_set(map_item, {.item = s2it(late_flag)}, {.item = i2it(77)});
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_INT, 2 * (int64_t)sizeof(void*),
                           3 * (int64_t)sizeof(void*));
    EXPECT_EQ(lambda_int_item_value(shape_transition_read_field(map, late_flag)), 77);

    int64_t wide_value = INT64_C(9007199254740991);
    fn_map_set(map_item, {.item = s2it(late_flag)}, {.item = l2it(&wide_value)});
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_INT64, 2 * (int64_t)sizeof(void*),
                           3 * (int64_t)sizeof(void*));
    EXPECT_EQ(shape_transition_read_field(map, late_flag).get_int64(), wide_value);

    double float_value = 3.5;
    fn_map_set(map_item, {.item = s2it(late_flag)}, {.item = d2it(&float_value)});
    expect_fixed_prefix(map);
    expect_packed_sibling(map, LMD_TYPE_FLOAT, 2 * (int64_t)sizeof(void*),
                           3 * (int64_t)sizeof(void*));
    EXPECT_DOUBLE_EQ(shape_transition_read_field(map, late_flag).get_double(), 3.5);
}

TEST_F(RuntimeShapeTransition, ArrayNumIntLaneRoundTripsAndWidensSafely) {
    ArrayNum* array = array_int_new(4);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->type_id, LMD_TYPE_ARRAY_NUM);
    EXPECT_EQ(array->get_elem_type(), ELEM_INT);

    array_int_set(array, 0, INT53_MIN);
    array_num_set_item(array, 1, {.item = i2it(INT53_MAX)});
    int64_t in_band = -123456789;
    array_num_set_item(array, 2, {.item = l2it(&in_band)});
    Item positive_infinity = {.item = lambda_int_box_double(INFINITY)};
    array_num_set_item(array, 3, positive_infinity);

    // ELEM_INT is an i64 lane now; it must not reinterpret the storage as
    // float64 while boxing or while preserving the shared poison values.
    EXPECT_EQ(array->items[0], INT53_MIN);
    EXPECT_EQ(array->items[1], INT53_MAX);
    EXPECT_EQ(array->items[2], in_band);
    EXPECT_EQ(array->items[3], INT_LANE_INF);

    Item first = array_num_read_item(array, 0);
    Item second = array_num_read_item(array, 1);
    Item third = array_num_read_item(array, 2);
    Item fourth = array_num_read_item(array, 3);
    EXPECT_EQ(get_type_id(first), LMD_TYPE_INT);
    EXPECT_EQ(lambda_int_item_to_i64(first), INT53_MIN);
    EXPECT_EQ(lambda_int_item_to_i64(second), INT53_MAX);
    EXPECT_EQ(lambda_int_item_to_i64(third), in_band);
    EXPECT_EQ(get_type_id(fourth), LMD_TYPE_FLOAT);
    EXPECT_EQ(fourth.get_double(), INFINITY);
    EXPECT_EQ(array_num_read_double(array, 0), (double)INT53_MIN);
    EXPECT_EQ(array_num_read_double(array, 1), (double)INT53_MAX);
    EXPECT_EQ(array_num_read_double(array, 3), INFINITY);

    EXPECT_EQ(fn_array_set((Array*)array, 0, {.item = i2it(77)}).item,
              ItemNull.item);
    EXPECT_EQ(array->items[0], 77);

    // An out-of-band int64 cannot remain in the compact int lane; widening
    // must preserve all existing lane values as ordinary generic Items.
    int64_t out_of_band = INT64_MAX;
    EXPECT_EQ(fn_array_set((Array*)array, 1, {.item = l2it(&out_of_band)}).item,
              ItemNull.item);
    EXPECT_EQ(array->type_id, LMD_TYPE_ARRAY);
    Array* generic = (Array*)array;
    EXPECT_EQ(get_type_id(generic->items[0]), LMD_TYPE_INT);
    EXPECT_EQ(lambda_int_item_to_i64(generic->items[0]), 77);
    EXPECT_EQ(get_type_id(generic->items[3]), LMD_TYPE_FLOAT);
    EXPECT_EQ(generic->items[1].get_int64(), out_of_band);
    EXPECT_EQ(array->extra, 1);

    ArrayNum* float_array = array_float_new(3);
    ASSERT_NE(float_array, nullptr);
    EXPECT_EQ(float_array->get_elem_type(), ELEM_FLOAT64);
    double tiny = ldexp(1.0, -1074);
    array_float_set(float_array, 0, tiny);
    array_num_set_item(float_array, 1, {.item = i2it(11)});
    array_num_set_item(float_array, 2, {.item = lambda_int_box_double(INFINITY)});
    EXPECT_DOUBLE_EQ(float_array->float_items[0], tiny);
    EXPECT_DOUBLE_EQ(float_array->float_items[1], 11.0);
    EXPECT_EQ(array_num_read_item(float_array, 0).get_double(), tiny);
    EXPECT_EQ(array_num_read_double(float_array, 2), INFINITY);

    EXPECT_EQ(fn_array_set((Array*)float_array, 0, {.item = i2it(13)}).item,
              ItemNull.item);
    EXPECT_DOUBLE_EQ(float_array->float_items[0], 13.0);

    // A non-numeric write widens a float lane and must preserve the exact
    // subnormal and infinity values while moving them into owned Item storage.
    EXPECT_EQ(fn_array_set((Array*)float_array, 2,
        {.item = b2it(BOOL_TRUE)}).item, ItemNull.item);
    Array* generic_float = (Array*)float_array;
    EXPECT_EQ(float_array->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(generic_float->items[0].get_double(), 13.0);
    EXPECT_EQ(generic_float->items[1].get_double(), 11.0);
    EXPECT_EQ(generic_float->items[2].item, b2it(BOOL_TRUE));
}

TEST_F(RuntimeShapeTransition, LabelStackAllocationFailureReturnsError) {
    int64_t shape[2] = {1, 1};
    ArrayNum* mask = array_num_new_ndim(ELEM_INT, 1, 2, shape);
    ASSERT_NE(mask, nullptr);
    array_int_set(mask, 0, 1);

    memtrack_fault_inject(0);
    Item result = fn_label({.array_num = mask});
    memtrack_fault_clear();

    EXPECT_TRUE(item_is_error(result));
}

TEST(ItemRepresentation, OwnedTailRelocationRebasesWideScalarItems) {
    Item old_items[6] = {};
    Item new_items[8] = {};
    List list = {};
    list.type_id = LMD_TYPE_ARRAY;
    list.items = old_items;
    list.length = 3;
    list.extra = 3;
    list.capacity = 6;

    *(int64_t*)&old_items[5] = INT64_MAX - 1;
    *(double*)&old_items[4] = ldexp(1.0, -1074);
    *(uint64_t*)&old_items[3] = UINT64_MAX;
    old_items[0] = {.item = l2it(&old_items[5])};
    old_items[1] = {.item = d2it(&old_items[4])};
    old_items[2] = {.item = u2it(&old_items[3])};
    // Growth has already copied the old allocation before this helper moves
    // the owned tail to its new end.
    memcpy(new_items, old_items, sizeof(old_items));

    list_relocate_owned_tail(&list, old_items, 6, new_items, 8);

    // The tail moves from [3, 6) to [5, 8); dense Items must follow the
    // owned payload rather than retain pointers into the abandoned buffer.
    EXPECT_EQ(new_items[0].get_int64(), INT64_MAX - 1);
    EXPECT_EQ(new_items[1].get_double(), ldexp(1.0, -1074));
    EXPECT_EQ(new_items[2].get_uint64(), UINT64_MAX);
    EXPECT_EQ((int64_t*)new_items[0].int64_ptr, (int64_t*)&new_items[7]);
    EXPECT_EQ((double*)new_items[1].double_ptr, (double*)&new_items[6]);
    EXPECT_EQ((uint64_t*)new_items[2].uint64_ptr, (uint64_t*)&new_items[5]);
    EXPECT_EQ(new_items[3].item, ItemNull.item);
    EXPECT_EQ(new_items[4].item, ItemNull.item);

    Item old_props[4] = {};
    Item new_props[6] = {};
    List js_list = {};
    js_list.type_id = LMD_TYPE_ARRAY;
    js_list.items = old_props;
    js_list.length = 2;
    js_list.extra = 1;
    js_list.capacity = 4;
    // D2.6.6v2: "has JS properties" is now the array's attribute face.
    Map props_map = {};
    props_map.type_id = LMD_TYPE_MAP;
    Item props_slot = {.map = &props_map};
    js_list.type = &ArrayPropsShape;
    js_list.data = &props_slot;
    js_list.data_cap = (int)sizeof(Item);
    old_props[3] = {.item = ITEM_JS_DELETED_SENTINEL};
    memcpy(new_props, old_props, sizeof(old_props));

    list_relocate_owned_tail(&js_list, old_props, 4, new_props, 6);
    EXPECT_EQ(new_props[5].item, ITEM_JS_DELETED_SENTINEL);
    EXPECT_EQ(new_props[3].item, ITEM_JS_DELETED_SENTINEL);
    EXPECT_EQ(new_props[4].item, ITEM_JS_DELETED_SENTINEL);
}

TEST_F(RuntimeShapeTransition, ElementContentAndAttributesKeepSeparateStorage) {
    Element* element = make_transition_element();
    ASSERT_NE(element, nullptr);
    Item element_item = {.element = element};

    int64_t content_int64 = INT64_MIN + 7;
    double content_float = ldexp(1.0, -1074);
    EXPECT_EQ(fn_array_set((Array*)element, 0, {.item = l2it(&content_int64)}).item,
              ItemNull.item);
    EXPECT_EQ(fn_array_set((Array*)element, 1, {.item = d2it(&content_float)}).item,
              ItemNull.item);
    EXPECT_EQ(element->extra, 2);
    EXPECT_EQ(element->items[0].get_int64(), content_int64);
    EXPECT_EQ(element->items[1].get_double(), content_float);

    Item initial_attr = shape_transition_read_typed_field((TypeMap*)element->type,
        element->data, late_flag);
    EXPECT_EQ(get_type_id(initial_attr), LMD_TYPE_BOOL);
    EXPECT_EQ(initial_attr.item, b2it(BOOL_FALSE));

    Map replacement = {};
    replacement.type_id = LMD_TYPE_MAP;
    fn_map_set(element_item, {.item = s2it(late_flag)}, {.map = &replacement});
    TypeMap* map_type = (TypeMap*)element->type;
    ShapeEntry* after = shape_transition_find_field_in_type(map_type, after_flag);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(((ShapeEntry*)shape_transition_find_field_in_type(map_type, late_flag))->type->type_id,
              LMD_TYPE_MAP);
    EXPECT_EQ(after->byte_offset, 3 * (int64_t)sizeof(void*));
    EXPECT_EQ(it2map(shape_transition_read_typed_field(map_type, element->data,
        late_flag)), &replacement);

    // Rebuild back to the one-byte boolean carrier while retaining the sibling
    // field and the independently owned content tail.
    fn_map_set(element_item, {.item = s2it(late_flag)}, {.item = b2it(BOOL_TRUE)});
    map_type = (TypeMap*)element->type;
    after = shape_transition_find_field_in_type(map_type, after_flag);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->byte_offset, 2 * (int64_t)sizeof(void*) + 1);
    EXPECT_EQ(shape_transition_read_typed_field(map_type, element->data,
        late_flag).item, b2it(BOOL_TRUE));
    EXPECT_EQ(element->items[0].get_int64(), content_int64);
    EXPECT_EQ(element->items[1].get_double(), content_float);
}

TEST_F(RuntimeShapeTransition, ObjectShapeMutationRebuildsPackedFields) {
    Map* seed = make_transition_map();
    ASSERT_NE(seed, nullptr);

    // The object branch shares the same packed shape machinery, but its
    // derived header must remain an OBJECT Item throughout the rebuild.
    Object object = {};
    object.type_id = LMD_TYPE_ELEMENT;
    object.type = seed->type;
    object.data = seed->data;
    object.data_cap = seed->data_cap;
    Item object_item = {.object = &object};
    EXPECT_EQ(get_type_id(object_item), LMD_TYPE_ELEMENT);
    EXPECT_EQ(object_get(&object, {.item = s2it(late_flag)}).item,
              b2it(BOOL_FALSE));

    Map replacement = {};
    replacement.type_id = LMD_TYPE_MAP;
    fn_map_set(object_item, {.item = s2it(late_flag)}, {.map = &replacement});

    TypeMap* object_type = (TypeMap*)object.type;
    ShapeEntry* late = shape_transition_find_field_in_type(object_type, late_flag);
    ShapeEntry* after = shape_transition_find_field_in_type(object_type, after_flag);
    ASSERT_NE(late, nullptr);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(late->type->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(late->byte_offset, 2 * (int64_t)sizeof(void*));
    EXPECT_EQ(after->byte_offset, 3 * (int64_t)sizeof(void*));
    EXPECT_EQ(it2map(object_get(&object, {.item = s2it(late_flag)})), &replacement);
    EXPECT_EQ(lambda_int_item_value(object_get(&object,
        {.item = s2it(after_flag)})), 123456);

    fn_map_set(object_item, {.item = s2it(late_flag)}, {.item = b2it(BOOL_TRUE)});
    EXPECT_EQ(object_get(&object, {.item = s2it(late_flag)}).item,
              b2it(BOOL_TRUE));
    EXPECT_EQ(lambda_int_item_value(object_get(&object,
        {.item = s2it(after_flag)})), 123456);
}

TEST_F(RuntimeShapeTransition, VMapMutationStabilizesWideValues) {
    Item vmap_item = vmap_new();
    ASSERT_EQ(get_type_id(vmap_item), LMD_TYPE_VMAP);
    ASSERT_NE(vmap_item.vmap, nullptr);

    int64_t wide_value = INT64_MAX;
    vmap_set(vmap_item, {.item = s2it(late_flag)}, {.item = l2it(&wide_value)});
    ASSERT_NE(vmap_item.vmap->data, nullptr);
    Item stored = vmap_item.vmap->vtable->get(vmap_item.vmap->data,
        {.item = s2it(late_flag)});
    EXPECT_EQ(get_type_id(stored), LMD_TYPE_INT64);
    EXPECT_EQ(stored.get_int64(), wide_value);
    EXPECT_NE((int64_t*)(uintptr_t)stored.int64_ptr, &wide_value);

    double float_value = ldexp(1.0, -1074);
    fn_map_set(vmap_item, {.item = s2it(late_flag)}, {.item = d2it(&float_value)});
    stored = vmap_item.vmap->vtable->get(vmap_item.vmap->data,
        {.item = s2it(late_flag)});
    EXPECT_EQ(get_type_id(stored), LMD_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(stored.get_double(), float_value);
    EXPECT_NE((double*)(uintptr_t)stored.double_ptr, &float_value);
    EXPECT_EQ(vmap_item.vmap->vtable->count(vmap_item.vmap->data), 1);
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
    object.type_id = LMD_TYPE_ELEMENT;
    object.data_cap = 6;
    expect_raw_item_header(&object, LMD_TYPE_ELEMENT);
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

    Object* object = (Object*)gc_heap_calloc(gc, sizeof(Object), LMD_TYPE_ELEMENT);
    ASSERT_NE(object, nullptr);
    object->type_id = LMD_TYPE_ELEMENT;
    object->data_cap = 6;
    expect_raw_item_header(object, LMD_TYPE_ELEMENT);
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
// These are written against the v5 finite payload contract rather than the
// retired rotation encoding. Poison has its own shared IEEE representation.
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

TEST(ItemRepresentation, IntBoxSaturatesOutOfBandValuesToSharedPoison) {
    Item positive = {.item = i2it(INT53_MAX + 1)};
    Item negative = {.item = i2it(INT53_MIN - 1)};

    // v5 reserves packed int Items for finite int53 values; saturation must
    // use the shared IEEE payload instead of retaining a sparse fake int.
    EXPECT_TRUE(lambda_item_is_merged_poison(positive.item));
    EXPECT_TRUE(lambda_item_is_merged_poison(negative.item));
    EXPECT_EQ(get_type_id(positive), LMD_TYPE_FLOAT);
    EXPECT_EQ(get_type_id(negative), LMD_TYPE_FLOAT);
    EXPECT_EQ(lambda_int_item_to_i64(positive), INT_LANE_INF);
    EXPECT_EQ(lambda_int_item_to_i64(negative), INT_LANE_NEG_INF);
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

TEST(ItemRepresentation, MirMemberAccessUsesPackedFieldWithoutReconstruction) {
    // use a test-private dump path: the default temp/mir_dump.txt is truncated and
    // rewritten by every concurrent debug lambda.exe run (e.g. test_lambda_gtest in
    // the parallel harness), which raced this test and made direct_loads == 0 flaky.
    const char* dump_path = "temp/item_repr_mir_dump.txt";
    remove(dump_path);
    const ShellEnvEntry env[] = {
        {"LAMBDA_MIR_DUMP_PATH", dump_path},
        {NULL, NULL},
    };
    const char* args[] = {
        // This test asserts the SHAPE of emitted MIR, so it must pin the JIT
        // tier: under the default LAMBDA_TIER_AUTO an eligible script is
        // planned for the T0 interpreter and no MIR is emitted at all.
        "./lambda.exe", "--tier=jit",
        "test/lambda/item_repr_container_member_load.ls", NULL,
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

    int member_calls = 0;
    int direct_loads = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // The typed Point fixture is lowered through its packed ShapeEntry now;
        // direct field loads replace generic fn_member without reconstructing a
        // different container Item for the member call.
        if (strstr(line, "call\tfn_member_p, fn_member,")) {
            member_calls++;
        }
        if (strstr(line, "\tmov\t%") && strstr(line, "i64:") &&
                strstr(line, "(%r")) {
            direct_loads++;
        }
    }
    fclose(f);

    EXPECT_EQ(member_calls, 0);
    ASSERT_GE(direct_loads, 2);
}
