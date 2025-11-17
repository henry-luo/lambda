#include "mark_builder.hpp"
#include "lambda-data.hpp"
#include "name_pool.h"
#include "input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"
#include <cstring>
#include <cassert>

extern TypeMap EmptyMap;

//==============================================================================
// MarkBuilder Implementation
//==============================================================================

MarkBuilder::MarkBuilder(Input* input)
    : input_(input)
    , pool_(input->pool)
    , arena_(input->arena)
    , name_pool_(input->name_pool)
    , type_list_(input->type_list)
    , sb_(input->sb)
    , auto_string_merge_(false)
    , intern_strings_(true)
{
    assert(input != nullptr);
    assert(pool_ != nullptr);
    assert(arena_ != nullptr);
    assert(type_list_ != nullptr);
}

MarkBuilder::~MarkBuilder() {
    // RAII cleanup - nothing to do since we don't own any resources
    // Arena-allocated data lives until Input's arena is reset/destroyed
}

//------------------------------------------------------------------------------
// String Creation Methods
//------------------------------------------------------------------------------

String* MarkBuilder::createString(const char* str) {
    if (!str) return &EMPTY_STRING;
    return createString(str, strlen(str));
}

String* MarkBuilder::createString(const char* str, size_t len) {
    if (!str || len == 0) return &EMPTY_STRING;

    if (intern_strings_ && name_pool_) {
        // use string interning (name_pool manages its own memory)
        return name_pool_create_len(name_pool_, str, len);
    } else {
        // allocate from arena (fast sequential allocation)
        String* s = (String*)arena_alloc(arena_, sizeof(String) + len + 1);
        s->ref_cnt = 1;
        s->len = len;
        memcpy(s->chars, str, len);
        s->chars[len] = '\0';
        return s;
    }
}

String* MarkBuilder::createStringFromBuf(StringBuf* sb) {
    if (!sb || sb->length == 0) return &EMPTY_STRING;
    return createString(sb->str->chars, sb->length);
}

String* MarkBuilder::emptyString() {
    return &EMPTY_STRING;
}

//------------------------------------------------------------------------------
// Builder Creation Methods
//------------------------------------------------------------------------------

ElementBuilder MarkBuilder::element(const char* tag_name) {
    return ElementBuilder(this, tag_name);
}

MapBuilder MarkBuilder::map() {
    return MapBuilder(this);
}

ArrayBuilder MarkBuilder::array() {
    return ArrayBuilder(this);
}

//------------------------------------------------------------------------------
// Direct Item Creation
//------------------------------------------------------------------------------

Item MarkBuilder::createElement(const char* tag_name) {
    return element(tag_name).final();
}

Item MarkBuilder::createMap() {
    return map().final();
}

Item MarkBuilder::createArray() {
    return array().final();
}

Item MarkBuilder::createStringItem(const char* str) {
    return (Item){.item = s2it(createString(str))};
}

Item MarkBuilder::createStringItem(const char* str, size_t len) {
    return (Item){.item = s2it(createString(str, len))};
}

Item MarkBuilder::createInt(int32_t value) {
    Item item = {.item = i2it(value)};
    return item;
}

Item MarkBuilder::createLong(int64_t value) {
    // allocate long from arena
    int64_t* long_ptr = (int64_t*)arena_alloc(arena_, sizeof(int64_t));
    *long_ptr = value;
    Item item = {.item = l2it(long_ptr)};
    return item;
}

Item MarkBuilder::createFloat(double value) {
    // allocate double from arena
    double* float_ptr = (double*)arena_alloc(arena_, sizeof(double));
    *float_ptr = value;
    Item item = {.item = d2it(float_ptr)};
    return item;
}

Item MarkBuilder::createBool(bool value) {
    return {.item = b2it(value)};
}

Item MarkBuilder::createNull() {
    return ItemNull;
}

//==============================================================================
// ElementBuilder Implementation
//==============================================================================

ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name)
    : builder_(builder)
    , tag_name_(builder->createString(tag_name))
    , elmt_(nullptr)
    , parent_(nullptr)
{
    // allocate Array directly from pool for children
    elmt_ = input_create_element(builder_->input(), tag_name);
}

ElementBuilder::~ElementBuilder() {
    // children_ is pool-allocated, no cleanup needed
    // attributes_ is pool-allocated, no cleanup needed
}

//------------------------------------------------------------------------------
// Attribute Setters
//------------------------------------------------------------------------------

ElementBuilder& ElementBuilder::attr(const char* key, Item value) {
    if (!key) return *this;
    // use elmt_put to add the attribute to the element
    String* key_str = builder_->createString(key);
    elmt_put(elmt_, key_str, value, builder_->pool());
    return *this;
}

ElementBuilder& ElementBuilder::attr(const char* key, const char* value) {
    return attr(key, builder_->createStringItem(value));
}

ElementBuilder& ElementBuilder::attr(const char* key, int64_t value) {
    return attr(key, builder_->createInt(value));
}

ElementBuilder& ElementBuilder::attr(const char* key, double value) {
    return attr(key, builder_->createFloat(value));
}

ElementBuilder& ElementBuilder::attr(const char* key, bool value) {
    return attr(key, builder_->createBool(value));
}

//------------------------------------------------------------------------------
// Child Management
//------------------------------------------------------------------------------

ElementBuilder& ElementBuilder::child(Item item) {
    array_append((Array*)elmt_, item, builder_->pool());
    return *this;
}

ElementBuilder& ElementBuilder::text(const char* text) {
    if (text) {
        child(builder_->createStringItem(text));
    }
    return *this;
}

ElementBuilder& ElementBuilder::text(const char* text, size_t len) {
    if (text && len > 0) {
        child(builder_->createStringItem(text, len));
    }
    return *this;
}

ElementBuilder& ElementBuilder::children(std::initializer_list<Item> items) {
    for (const Item& item : items) {
        child(item);
    }
    return *this;
}

//------------------------------------------------------------------------------
// Nested Element Building
//------------------------------------------------------------------------------

ElementBuilder ElementBuilder::beginChild(const char* tag_name) {
    ElementBuilder child_builder = builder_->element(tag_name);
    child_builder.parent_ = this;
    return child_builder;
}

ElementBuilder& ElementBuilder::end() {
    if (parent_) {
        // add this element as child to parent
        parent_->child(final());
        return *parent_;
    }
    return *this;
}

Item ElementBuilder::final() {
    // Set content_length to match the number of children in the element
    // This is required for the formatter to properly access children
    if (elmt_ && elmt_->type) {
        TypeElmt* elmt_type = (TypeElmt*)elmt_->type;
        List* list = (List*)elmt_;
        elmt_type->content_length = list->length;
    }
    return (Item){.element = elmt_};
}

//==============================================================================
// MapBuilder Implementation
//==============================================================================

MapBuilder::MapBuilder(MarkBuilder* builder)
    : builder_(builder)
    , map_(nullptr)
    , map_type_(nullptr)
{
    // allocate map structure from arena (fast sequential allocation)
    // Note: map_put() uses pool for internal data buffers (needs pool_free for resizing)
    map_ = (Map*)arena_calloc(builder_->arena(), sizeof(Map));
    map_->type_id = LMD_TYPE_MAP;
    map_->type = &EmptyMap;  // Will be replaced by map_put on first insert
    map_->data = nullptr;
    map_->data_cap = 0;
}

MapBuilder::~MapBuilder() {
    // map_ is arena-allocated, no cleanup needed
}

MapBuilder& MapBuilder::put(const char* key, Item value) {
    if (!key) return *this;

    String* key_str = builder_->createString(key);
    map_put(map_, key_str, value, builder_->input());

    // cache the type for convenience
    if (!map_type_) {
        map_type_ = (TypeMap*)map_->type;
    }

    return *this;
}

MapBuilder& MapBuilder::put(String* key, Item value) {
    if (!key) return *this;

    // Use the existing string directly
    map_put(map_, key, value, builder_->input());

    // cache the type for convenience
    if (!map_type_) {
        map_type_ = (TypeMap*)map_->type;
    }

    return *this;
}

MapBuilder& MapBuilder::put(const char* key, const char* value) {
    return put(key, builder_->createStringItem(value));
}

MapBuilder& MapBuilder::put(const char* key, int64_t value) {
    return put(key, builder_->createInt(value));
}

MapBuilder& MapBuilder::put(const char* key, double value) {
    return put(key, builder_->createFloat(value));
}

MapBuilder& MapBuilder::put(const char* key, bool value) {
    return put(key, builder_->createBool(value));
}

MapBuilder& MapBuilder::putNull(const char* key) {
    return put(key, builder_->createNull());
}

Item MapBuilder::final() {
    return (Item){.map = map_};
}

//==============================================================================
// ArrayBuilder Implementation
//==============================================================================

ArrayBuilder::ArrayBuilder(MarkBuilder* builder)
    : builder_(builder)
    , array_(nullptr)
{
    // allocate Array directly from pool
    array_ = array_pooled(builder_->pool());
}

ArrayBuilder::~ArrayBuilder() {
    // array_ is pool-allocated, no cleanup needed
}

ArrayBuilder& ArrayBuilder::append(Item item) {
    if (array_) {
        array_append(array_, item, builder_->pool());
    }
    return *this;
}

ArrayBuilder& ArrayBuilder::append(const char* str) {
    return append(builder_->createStringItem(str));
}

ArrayBuilder& ArrayBuilder::append(int64_t value) {
    return append(builder_->createInt(value));
}

ArrayBuilder& ArrayBuilder::append(double value) {
    return append(builder_->createFloat(value));
}

ArrayBuilder& ArrayBuilder::append(bool value) {
    return append(builder_->createBool(value));
}

ArrayBuilder& ArrayBuilder::appendItems(std::initializer_list<Item> items) {
    for (const Item& item : items) {
        append(item);
    }
    return *this;
}

Item ArrayBuilder::final() {
    // Array is already built - just wrap it in an Item
    return (Item){.array = array_};
}
