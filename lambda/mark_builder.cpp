#include "mark_builder.hpp"
#include "lambda-data.hpp"
#include "name_pool.h"
#include "input/input.h"
#include "../lib/mempool.h"
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
    , name_pool_(input->name_pool)
    , type_list_(input->type_list)
    , sb_(input->sb)
    , auto_string_merge_(false)
    , intern_strings_(true)
{
    assert(input != nullptr);
    assert(pool_ != nullptr);
    assert(type_list_ != nullptr);
}

MarkBuilder::~MarkBuilder() {
    // RAII cleanup - nothing to do since we don't own any resources
    // All temporary data was stack-allocated or pool-allocated
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
        // use string interning
        return name_pool_create_len(name_pool_, str, len);
    } else {
        // allocate from pool (use flexible array member layout)
        String* s = (String*)pool_alloc(pool_, sizeof(String) + len + 1);
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
    return element(tag_name).build();
}

Item MarkBuilder::createMap() {
    return map().build();
}

Item MarkBuilder::createArray() {
    return array().build();
}

Item MarkBuilder::createStringItem(const char* str) {
    return (Item){.item = s2it(createString(str))};
}

Item MarkBuilder::createStringItem(const char* str, size_t len) {
    return (Item){.item = s2it(createString(str, len))};
}

Item MarkBuilder::createInt(int64_t value) {
    Item item = {0};
    item.int_val = value;
    item.type_id = LMD_TYPE_INT;
    return item;
}

Item MarkBuilder::createFloat(double value) {
    // allocate double from pool
    double* float_ptr = (double*)pool_alloc(pool_, sizeof(double));
    *float_ptr = value;
    Item item = {0};
    item.pointer = (uint64_t)float_ptr;
    item.type_id = LMD_TYPE_FLOAT;
    return item;
}

Item MarkBuilder::createBool(bool value) {
    Item item = {0};
    item.bool_val = value ? 1 : 0;
    item.type_id = LMD_TYPE_BOOL;
    return item;
}

Item MarkBuilder::createNull() {
    return (Item){.item = ITEM_NULL};
}

//==============================================================================
// ElementBuilder Implementation
//==============================================================================

ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name)
    : builder_(builder)
    , tag_name_(builder->createString(tag_name))
    , children_(arraylist_new(16))
    , attributes_(nullptr)
    , attr_type_(nullptr)
    , parent_(nullptr)
{
}

ElementBuilder::~ElementBuilder() {
    // cleanup temporary children list
    if (children_) {
        arraylist_free(children_);
        children_ = nullptr;
    }
    // attributes_ is pool-allocated, no cleanup needed
}

//------------------------------------------------------------------------------
// Attribute Setters
//------------------------------------------------------------------------------

ElementBuilder& ElementBuilder::attr(const char* key, Item value) {
    if (!key) return *this;
    
    // lazy initialization of element with TypeElmt
    if (!attributes_) {
        // allocate element from pool
        Pool* pool = builder_->pool();
        attributes_ = elmt_pooled(pool);
        
        // create TypeElmt descriptor
        TypeElmt* element_type = (TypeElmt*)alloc_type(pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        attributes_->type = element_type;
        
        // register type in type list
        arraylist_append(builder_->typeList(), element_type);
        element_type->type_index = builder_->typeList()->length - 1;
        
        // set element name (tag name)
        element_type->name.str = tag_name_->chars;
        element_type->name.length = tag_name_->len;
        
        // cache the type for convenience
        attr_type_ = (TypeMap*)element_type;
    }
    
    // use elmt_put to add the attribute to the element
    String* key_str = builder_->createString(key);
    elmt_put((Element*)attributes_, key_str, value, builder_->pool());
    
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
    if (children_) {
        arraylist_append(children_, (void*)item.item);
    }
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
        parent_->child(build());
        return *parent_;
    }
    return *this;
}

//------------------------------------------------------------------------------
// Finalization
//------------------------------------------------------------------------------

Item ElementBuilder::build() {
    Pool* pool = builder_->pool();
    Element* element;
    
    // if we have attributes, the element was already created in attr()
    if (attributes_) {
        element = (Element*)attributes_;
    } else {
        // no attributes, create element now
        element = elmt_pooled(pool);
        
        // create TypeElmt descriptor
        TypeElmt* element_type = (TypeElmt*)alloc_type(pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        element->type = element_type;
        
        // register type in type list
        arraylist_append(builder_->typeList(), element_type);
        element_type->type_index = builder_->typeList()->length - 1;
        
        // set element name (tag name)
        element_type->name.str = tag_name_->chars;
        element_type->name.length = tag_name_->len;
    }
    
    // convert children arraylist to array
    if (children_ && children_->length > 0) {
        element->length = children_->length;
        element->capacity = children_->length;
        
        // allocate items array from pool
        element->items = (Item*)pool_alloc(
            pool, 
            sizeof(Item) * element->length
        );
        
        // copy items
        for (int i = 0; i < children_->length; i++) {
            element->items[i] = (Item){.item = (uint64_t)children_->data[i]};
        }
    } else {
        element->length = 0;
        element->capacity = 0;
        element->items = nullptr;
    }
    
    // return as Item
    Item result = {0};
    result.pointer = (uint64_t)element;
    result.type_id = LMD_TYPE_ELEMENT;
    return result;
}

//==============================================================================
// MapBuilder Implementation
//==============================================================================

MapBuilder::MapBuilder(MarkBuilder* builder)
    : builder_(builder)
    , map_(nullptr)
    , map_type_(nullptr)
{
    // allocate map from pool
    map_ = (Map*)pool_calloc(builder_->pool(), sizeof(Map));
    map_->type = &EmptyMap;  // Will be replaced by map_put on first insert
    map_->data = nullptr;
    map_->data_cap = 0;
}

MapBuilder::~MapBuilder() {
    // map_ is pool-allocated, no cleanup needed
}

//------------------------------------------------------------------------------
// Key-Value Setters
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// Finalization
//------------------------------------------------------------------------------

Item MapBuilder::build() {
    Item result = {0};
    result.pointer = (uint64_t)map_;
    result.type_id = LMD_TYPE_MAP;
    return result;
}

//==============================================================================
// ArrayBuilder Implementation
//==============================================================================

ArrayBuilder::ArrayBuilder(MarkBuilder* builder)
    : builder_(builder)
    , items_(arraylist_new(16))
{
}

ArrayBuilder::~ArrayBuilder() {
    // cleanup temporary items list
    if (items_) {
        arraylist_free(items_);
        items_ = nullptr;
    }
}

//------------------------------------------------------------------------------
// Append Operations
//------------------------------------------------------------------------------

ArrayBuilder& ArrayBuilder::append(Item item) {
    if (items_) {
        arraylist_append(items_, (void*)item.item);
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

//------------------------------------------------------------------------------
// Finalization
//------------------------------------------------------------------------------

Item ArrayBuilder::build() {
    // allocate Array from pool
    Pool* pool = builder_->pool();
    Array* array = (Array*)pool_calloc(pool, sizeof(Array));
    
    // Array is just a typedef for List in C++ mode, which inherits from Container
    // Container has type_id, flags, ref_cnt fields, but no separate type pointer
    array->type_id = LMD_TYPE_ARRAY;
    array->ref_cnt = 1;
    array->flags = 0;
    
    // convert items arraylist to array
    if (items_ && items_->length > 0) {
        array->length = items_->length;
        array->capacity = items_->length;
        array->extra = 0;
        
        // allocate items array from pool
        array->items = (Item*)pool_alloc(
            pool, 
            sizeof(Item) * array->length
        );
        
        // copy items
        for (int i = 0; i < items_->length; i++) {
            array->items[i] = (Item){.item = (uint64_t)items_->data[i]};
        }
    } else {
        array->length = 0;
        array->capacity = 0;
        array->extra = 0;
        array->items = nullptr;
    }
    
    // return as Item
    Item result = {0};
    result.pointer = (uint64_t)array;
    result.type_id = LMD_TYPE_ARRAY;
    return result;
}
