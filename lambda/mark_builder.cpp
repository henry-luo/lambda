// MarkBuilder - Fluent API for constructing Lambda document structures
//
// String Management Strategy (unified name/symbol system):
//   - createName(): Always pooled via NamePool (string interning)
//       Use for: map keys, element tags, attribute names - structural identifiers
//       Same name returns same pointer (enables identity comparison, memory sharing)
//
//   - createString(): Never pooled (arena allocated)
//       Use for: user content, text data, string values - non-structural content
//       Fast allocation, no hash lookup overhead
//
//   - createSymbol(): Conditionally pooled (only if ≤32 chars, otherwise arena)
//       Use for: symbol literals ('mySymbol), short enum-like values
//
// Memory Benefits:
//   - Structural names deduplicated across entire document hierarchy
//   - Parent NamePool inheritance (schemas share names with instances)
//   - Content strings remain fast with arena allocation

#include "mark_builder.hpp"
#include "lambda-data.hpp"
#include "input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"
#include <cstring>
#include <cassert>

// Forward declarations for internal functions in input.cpp
extern Element* input_create_element_internal(Input *input, const char* tag_name);
extern void elmt_put(Element* elmt, String* key, Item value, Pool* pool);
extern void map_put(Map* mp, String* key, Item value, Input *input);

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
    , auto_string_merge_(false)
{
    assert(input != nullptr);
    assert(pool_ != nullptr);
    assert(arena_ != nullptr);
    assert(name_pool_ != nullptr);
    assert(type_list_ != nullptr);
}

MarkBuilder::~MarkBuilder() {
    // RAII cleanup - nothing to do since we don't own any resources
    // Arena-allocated data lives until Input's arena is reset/destroyed
}

//------------------------------------------------------------------------------
// Name Creation Methods (always use name_pool)
//------------------------------------------------------------------------------

String* MarkBuilder::createName(const char* name) {
    if (!name) return &EMPTY_STRING;
    return createName(name, strlen(name));
}

String* MarkBuilder::createName(const char* name, size_t len) {
    if (!name || len == 0) return &EMPTY_STRING;
    return name_pool_create_len(name_pool_, name, len);
}

String* MarkBuilder::createNameFromStrView(StrView name) {
    if (!name.str || name.length == 0) return &EMPTY_STRING;
    return name_pool_create_strview(name_pool_, name);
}

//------------------------------------------------------------------------------
// Symbol Creation Methods (use name_pool for short symbols)
//------------------------------------------------------------------------------

String* MarkBuilder::createSymbol(const char* symbol) {
    if (!symbol) return &EMPTY_STRING;
    return createSymbol(symbol, strlen(symbol));
}

String* MarkBuilder::createSymbol(const char* symbol, size_t len) {
    if (!symbol || len == 0) return &EMPTY_STRING;
    return name_pool_create_symbol_len(name_pool_, symbol, len);
}

String* MarkBuilder::createSymbolFromStrView(StrView symbol) {
    return name_pool_create_symbol_strview(name_pool_, symbol);
}

//------------------------------------------------------------------------------
// String Creation Methods (arena allocation, no pooling)
//------------------------------------------------------------------------------

String* MarkBuilder::createString(const char* str) {
    if (!str) return &EMPTY_STRING;
    return createString(str, strlen(str));
}

String* MarkBuilder::createString(const char* str, size_t len) {
    if (!str || len == 0) return &EMPTY_STRING;

    // Allocate from arena (fast sequential allocation, no deduplication)
    String* s = (String*)arena_alloc(arena_, sizeof(String) + len + 1);
    s->ref_cnt = 1;
    s->len = len;
    memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

String* MarkBuilder::createStringFromBuf(StringBuf* sb) {
    if (!sb || sb->length == 0) return &EMPTY_STRING;
    return createString(sb->str->chars, sb->length);
}

String* MarkBuilder::emptyString() {
    return &EMPTY_STRING;
}

//------------------------------------------------------------------------------
// Item Creation Helpers
//------------------------------------------------------------------------------

Item MarkBuilder::createNameItem(const char* name) {
    return (Item){.item = y2it(createName(name))};  // use symbol encoding for names
}

Item MarkBuilder::createSymbolItem(const char* symbol) {
    return (Item){.item = y2it(createSymbol(symbol))};
}

Item MarkBuilder::createStringItem(const char* str) {
    return (Item){.item = s2it(createString(str))};
}

Item MarkBuilder::createStringItem(const char* str, size_t len) {
    return (Item){.item = s2it(createString(str, len))};
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

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

void MarkBuilder::putToElement(Element* elmt, String* key, Item value) {
    elmt_put(elmt, key, value, pool_);
}

void MarkBuilder::putToMap(Map* map, String* key, Item value) {
    map_put(map, key, value, input_);
}

//==============================================================================
// ElementBuilder Implementation
//==============================================================================

ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name)
    : builder_(builder)
    , tag_name_(builder->createName(tag_name))  // element names are structural identifiers - always pooled
    , elmt_(nullptr)
    , parent_(nullptr)
{
    Input* input = builder_->input();
    Element* element = elmt_pooled(input->pool);
    if (element) {
        TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        if (element_type) {
            element->type = element_type;
            arraylist_append(input->type_list, element_type);
            element_type->type_index = input->type_list->length - 1;
            // initialize with no attributes

            // set element name (use name pool for structural identifier)
            String* name_str = builder->createName(tag_name);
            if (name_str) {
                element_type->name.str = name_str->chars;
                element_type->name.length = name_str->len;
            }
            elmt_ = (element);
        }
    }
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
    String* key_str = builder_->createName(key);  // attribute names are structural identifiers - always pooled
    elmt_put(elmt_, key_str, value, builder_->pool());
    return *this;
}

ElementBuilder& ElementBuilder::attr(const char* key, const char* value) {
    return attr(key, builder_->createStringItem(value));  // note: value content uses createString (NOT pooled)
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

// String* key overloads
ElementBuilder& ElementBuilder::attr(String* key, Item value) {
    if (!key) return *this;
    builder_->putToElement(elmt_, key, value);
    return *this;
}

ElementBuilder& ElementBuilder::attr(String* key, const char* value) {
    return attr(key, builder_->createStringItem(value));
}

ElementBuilder& ElementBuilder::attr(String* key, int64_t value) {
    return attr(key, builder_->createInt(value));
}

ElementBuilder& ElementBuilder::attr(String* key, double value) {
    return attr(key, builder_->createFloat(value));
}

ElementBuilder& ElementBuilder::attr(String* key, bool value) {
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
        child(builder_->createStringItem(text));  // text content is user data - NOT pooled
    }
    return *this;
}

ElementBuilder& ElementBuilder::text(const char* text, size_t len) {
    if (text && len > 0) {
        child(builder_->createStringItem(text, len));  // text content is user data - NOT pooled
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

    String* key_str = builder_->createName(key);  // map keys are structural identifiers - always pooled
    map_put(map_, key_str, value, builder_->input());

    // cache the type for convenience
    if (!map_type_) {
        map_type_ = (TypeMap*)map_->type;
    }

    return *this;
}

MapBuilder& MapBuilder::put(String* key, Item value) {
    if (!key) return *this;

    // use the existing string directly (caller responsible for pooling decision)
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

// String* key overloads
MapBuilder& MapBuilder::put(String* key, const char* value) {
    return put(key, builder_->createStringItem(value));
}

MapBuilder& MapBuilder::put(String* key, int64_t value) {
    return put(key, builder_->createInt(value));
}

MapBuilder& MapBuilder::put(String* key, double value) {
    return put(key, builder_->createFloat(value));
}

MapBuilder& MapBuilder::put(String* key, bool value) {
    return put(key, builder_->createBool(value));
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

// Deep copy an Item using MarkBuilder to a new Input's arena
// This recursively copies all nested structures (Arrays, Maps, Elements) and their data
static Item copy_item_deep(MarkBuilder* builder, Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("copy_item_deep: type_id=%d", type_id);
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            return builder->createNull();
            
        case LMD_TYPE_BOOL:
            return builder->createBool(it2b(item));
            
        case LMD_TYPE_INT:
            return builder->createInt(it2i(item));
            
        case LMD_TYPE_INT64:
            return builder->createLong(it2l(item));
            
        case LMD_TYPE_FLOAT:
            return builder->createFloat(it2d(item));
        
        case LMD_TYPE_SYMBOL: {
            String* sym = (String*)item.pointer;
            String* copied_sym = builder->createSymbol(sym->chars, sym->len);
            return {.item = y2it(copied_sym)};
        }
        
        case LMD_TYPE_STRING: {
            String* str = it2s(item);
            if (!str) return builder->createNull();
            return builder->createStringItem(str->chars, str->len);
        }
            
        case LMD_TYPE_BINARY: {
            String* bin = get_binary(item);
            if (!bin) return builder->createNull();
            // Binary data is stored like String but with different type_id
            String* copied = builder->createString(bin->chars, bin->len);
            if (!copied) return builder->createNull();
            Item result = {.item = x2it(copied)};
            return result;
        }
            
        case LMD_TYPE_DTIME: {
            DateTime dt = get_datetime(item);
            // DateTime is uint64_t bitfield, allocate from arena
            DateTime* dt_ptr = (DateTime*)arena_alloc(builder->input()->arena, sizeof(DateTime));
            if (!dt_ptr) return builder->createNull();
            *dt_ptr = dt;
            Item result = {.item = k2it(dt_ptr)};
            return result;
        }
            
        case LMD_TYPE_DECIMAL: {
            Decimal* src_dec = get_decimal(item);
            if (!src_dec || !src_dec->dec_val) return builder->createNull();
            
            // Allocate new Decimal structure from arena
            Decimal* new_dec = (Decimal*)arena_alloc(builder->input()->arena, sizeof(Decimal));
            if (!new_dec) return builder->createNull();
            
            // Create new mpd_t and copy the value by converting to string and back
            // This is the safest way to deep copy mpdecimal values
            char* dec_str = mpd_to_sci(src_dec->dec_val, 1);
            if (!dec_str) return builder->createNull();
            
            mpd_context_t ctx;
            mpd_maxcontext(&ctx);
            mpd_t* new_dec_val = mpd_qnew();
            if (!new_dec_val) {
                free(dec_str);
                return builder->createNull();
            }
            
            mpd_set_string(new_dec_val, dec_str, &ctx);
            free(dec_str);
            
            new_dec->ref_cnt = 1;
            new_dec->dec_val = new_dec_val;
            
            Item result = {.item = c2it(new_dec)};
            return result;
        }
            
        case LMD_TYPE_NUMBER: {
            // NUMBER type is stored as double
            double val = get_double(item);
            return builder->createFloat(val);
        }
            
        case LMD_TYPE_ARRAY_INT: {
            ArrayInt* arr = item.array_int;
            if (!arr) return builder->createArray();
            
            ArrayBuilder arr_builder = builder->array();
            for (int i = 0; i < arr->length; i++) {
                int32_t val = arr->items[i];
                arr_builder.append((int64_t)val);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY_INT64: {
            ArrayInt64* arr = item.array_int64;
            if (!arr) return builder->createArray();
            
            ArrayBuilder arr_builder = builder->array();
            for (int i = 0; i < arr->length; i++) {
                int64_t val = arr->items[i];
                arr_builder.append(val);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY_FLOAT: {
            ArrayFloat* arr = item.array_float;
            if (!arr) return builder->createArray();
            
            ArrayBuilder arr_builder = builder->array();
            for (int i = 0; i < arr->length; i++) {
                double val = arr->items[i];
                arr_builder.append(val);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_ARRAY: {
            Array* arr = item.array;
            if (!arr) return builder->createArray();
            
            ArrayBuilder arr_builder = builder->array();
            for (int i = 0; i < arr->length; i++) {
                Item child = array_get(arr, i);
                Item copied_child = copy_item_deep(builder, child);
                arr_builder.append(copied_child);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_LIST: {
            List* list = item.list;
            if (!list) return builder->createArray();
            
            ArrayBuilder arr_builder = builder->array();
            for (int i = 0; i < list->length; i++) {
                Item child = list->items[i];
                Item copied_child = copy_item_deep(builder, child);
                arr_builder.append(copied_child);
            }
            return arr_builder.final();
        }
            
        case LMD_TYPE_MAP: {
            Map* map = item.map;
            log_debug("copy_item_deep: MAP map=%p, type=%p, data=%p", map, map ? map->type : nullptr, map ? map->data : nullptr);
            if (!map || !map->type || !map->data) {
                log_debug("copy_item_deep: MAP returning empty map (null data)");
                return builder->createMap();
            }
            TypeMap* map_type = (TypeMap*)map->type;
            log_debug("copy_item_deep: MAP map_type=%p, shape=%p", map_type, map_type->shape);
            MapBuilder map_builder = builder->map();
            
            // Iterate over map fields
            ShapeEntry* field = map_type->shape;
            while (field) {
                log_debug("copy_item_deep: MAP field=%p, name=%p", field, field->name);
                if (field->name && field->name->str) {
                    log_debug("copy_item_deep: MAP field name=%.*s (len=%zu), byte_offset=%d", 
                        (int)field->name->length, field->name->str, field->name->length, field->byte_offset);
                    // Get field value - it's stored as an Item!
                    void* field_data = (char*)map->data + field->byte_offset;
                    log_debug("copy_item_deep: MAP field_data=%p, about to dereference", field_data);
                    Item field_item = *(Item*)field_data;
                    log_debug("copy_item_deep: MAP field item type=%d", get_type_id(field_item));
                    
                    // Recursively deep copy the field value
                    Item copied_field = copy_item_deep(builder, field_item);
                    
                    // Add to map - use str and length from StrView
                    String* key_name = builder->createName(field->name->str, field->name->length);
                    map_builder.put(key_name, copied_field);
                }
                field = field->next;
            }
            
            return map_builder.final();
        }
            
        case LMD_TYPE_ELEMENT: {
            Element* elem = item.element;
            if (!elem || !elem->type) return builder->createElement("div");
            
            TypeElmt* elem_type = (TypeElmt*)elem->type;
            // Use str and length from StrView
            char tag_name[256];
            size_t tag_len = elem_type->name.length < 255 ? elem_type->name.length : 255;
            memcpy(tag_name, elem_type->name.str, tag_len);
            tag_name[tag_len] = '\0';
            ElementBuilder elem_builder = builder->element(tag_name);
            
            // Copy attributes
            if (elem_type->length > 0) {
                ShapeEntry* attr = elem_type->shape;
                while (attr) {
                    if (attr->name) {
                        void* attr_data = (char*)elem->data + attr->byte_offset;
                        // Attributes are stored as Items
                        Item attr_item = *(Item*)attr_data;
                        
                        // Recursively deep copy the attribute value
                        Item copied_attr = copy_item_deep(builder, attr_item);
                        
                        // Use str and length from StrView
                        String* attr_name = builder->createName(attr->name->str, attr->name->length);
                        elem_builder.attr(attr_name, copied_attr);
                    }
                    attr = attr->next;
                }
            }
            
            // Copy children
            for (int i = 0; i < elem->length; i++) {
                Item child = elem->items[i];
                Item copied_child = copy_item_deep(builder, child);
                elem_builder.child(copied_child);
            }
            
            return elem_builder.final();
        }
            
        default:
            // For unsupported types, return null
            log_debug("copy_item_deep: unsupported type_id=%d, returning null", type_id);
            return builder->createNull();
    }
}