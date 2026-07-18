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
#include "lambda-decimal.hpp"
#include "lambda-data.hpp"
#include "lambda.h"  // for it2l, it2s, it2b, it2i, it2d, etc.
#include "mark_reader.hpp"  // for ArrayReader
#include "../lib/lambda_typed.hpp"
#include "../lib/str.h"
#include "input/input.hpp"
#include "input/css/dom_node.hpp"      // for DomText, dom_text_to_string
#include "input/css/dom_element.hpp"   // for DomElement, dom_element_to_element
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
#include "../lib/hashmap.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <cstring>
#include <cassert>
#include <new>

// Forward declarations for internal functions in input.cpp
extern Element* input_create_element_internal(Input *input, const char* tag_name);

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
    , ui_mode_(input->ui_mode)
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
// Heap factory (audited boundary for `new MarkBuilder` / `delete builder`)
//------------------------------------------------------------------------------

MarkBuilder* mark_builder_create(Input* input) {
    if (!input) return nullptr;
    MarkBuilder* builder = (MarkBuilder*)mem_alloc(sizeof(MarkBuilder), MEM_CAT_EVAL);
    if (!builder) return nullptr;
    new (builder) MarkBuilder(input); // NEW_DELETE_OK: single audited construction boundary for MarkBuilder.
    return builder;
}

void mark_builder_destroy(MarkBuilder* builder) {
    if (!builder) return;
    builder->~MarkBuilder(); // NEW_DELETE_OK: paired with mark_builder_create.
    mem_free(builder);
}

//------------------------------------------------------------------------------
// Name Creation Methods (always use name_pool)
//------------------------------------------------------------------------------

String* MarkBuilder::createName(const char* name) {
    if (!name) return nullptr;
    return createName(name, strlen(name));
}

String* MarkBuilder::createName(const char* name, size_t len) {
    if (!name) return nullptr;
    // JSON and similar formats can carry an empty object key; preserve it as a structural name.
    return name_pool_create_len(name_pool_, name, len);
}

String* MarkBuilder::createNameFromStrView(StrView name) {
    if (!name.str) return nullptr;
    return name_pool_create_strview(name_pool_, name);
}

//------------------------------------------------------------------------------
// Symbol Creation Methods (arena-allocated Symbol structs)
//------------------------------------------------------------------------------

Symbol* MarkBuilder::createSymbol(const char* symbol) {
    if (!symbol) return nullptr;
    return createSymbol(symbol, strlen(symbol));
}

Symbol* MarkBuilder::createSymbol(const char* symbol, size_t len) {
    if (!symbol || len == 0) return nullptr;
    Symbol* sym = (Symbol*)arena_alloc(arena_, sizeof(Symbol) + len + 1);
    sym->len = len;
    sym->ns = nullptr;
    memcpy(sym->chars, symbol, len);
    sym->chars[len] = '\0';
    return sym;
}

Symbol* MarkBuilder::createSymbolFromStrView(StrView symbol) {
    return createSymbol(symbol.str, symbol.length);
}

//------------------------------------------------------------------------------
// String Creation Methods (arena allocation, no pooling)
//------------------------------------------------------------------------------

String* MarkBuilder::createString(const char* str) {
    if (!str) return nullptr;
    return createString(str, strlen(str));
}

String* MarkBuilder::createString(const char* str, size_t len) {
    if (!str) return nullptr;

    // Empty strings are values in Phase 3, so content producers must not collapse them to null.
    String* s = (String*)arena_alloc(arena_, sizeof(String) + len + 1);
    s->len = len;
    s->is_ascii = str_is_ascii(str, len) ? 1 : 0;
    memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

Binary* MarkBuilder::createBinary(const void* bytes, size_t len) {
    if ((!bytes && len != 0) || len > UINT32_MAX ||
        len > SIZE_MAX - sizeof(Binary)) return nullptr;

    // Arena objects have no individual finalizer, so their immutable byte
    // payload must remain in the arena instead of retaining external storage.
    Binary* binary = (Binary*)arena_alloc(arena_, sizeof(Binary) + len);
    if (!binary) return nullptr;
    binary->len = (uint32_t)len;
    binary->is_ascii = (len == 0 || str_is_ascii((const char*)bytes, len)) ? 1 : 0;
    binary->flags = BINARY_FLAG_INLINE;
    binary->reserved = 0;
    binary->storage = nullptr;
    binary->offset = 0;
    if (len != 0) memcpy(binary->inline_bytes, bytes, len);
    return binary;
}

String* MarkBuilder::createStringFromBuf(StringBuf* sb) {
    if (!sb) return nullptr;
    return createString(sb->str->chars, sb->length);
}

String* MarkBuilder::createDomTextString(const char* str, size_t len) {
    if (!str) return nullptr;

    // Allocate [DomText][String header][chars...\0] as one contiguous block
    // Used in ui_mode for text content that becomes DomText nodes in layout
    size_t total = sizeof(DomText) + sizeof(String) + len + 1;
    DomText* dt = (DomText*)arena_calloc(arena_, total);  // zeros DomText fields
    if (!dt) return nullptr;
    dt->node_type = DOM_NODE_TEXT;
    dt->set_symbol(false);
    String* s = dom_text_to_string(dt);
    s->len = (uint32_t)len;
    s->is_ascii = str_is_ascii(str, len) ? 1 : 0;
    memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    // Set convenience fields for backward compat with DomText consumers
    dt->native_string = s;
    dt->text = s->chars;
    dt->length = s->len;
    return s;
}

String* MarkBuilder::createDomTextStringFromBuf(StringBuf* sb) {
    if (!sb || sb->length == 0) return nullptr;
    return createDomTextString(sb->str->chars, sb->length);
}

String* MarkBuilder::emptyString() {
    return nullptr;
}

//------------------------------------------------------------------------------
// Item Creation Helpers
//------------------------------------------------------------------------------

Item MarkBuilder::createNameItem(const char* name) {
    Symbol* sym = createSymbol(name);  // create proper Symbol for correct memory layout
    if (!sym) return createNull();
    return (Item){.item = y2it(sym)};
}

Item MarkBuilder::createSymbolItem(const char* symbol) {
    Symbol* sym = createSymbol(symbol);
    // Empty symbol maps to null (createSymbol returns nullptr for empty)
    if (!sym) return createNull();
    return (Item){.item = y2it(sym)};
}

Item MarkBuilder::createStringItem(const char* str) {
    String* s = createString(str);
    // createString returns nullptr only for null input; empty strings are real values.
    if (!s) return createNull();
    return (Item){.item = s2it(s)};
}

Item MarkBuilder::createStringItem(const char* str, size_t len) {
    String* s = createString(str, len);
    // createString returns nullptr only for null input; empty strings are real values.
    if (!s) return createNull();
    return (Item){.item = s2it(s)};
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

lam::ItemOf<LMD_TYPE_ELEMENT> MarkBuilder::createElementTyped(const char* tag_name) {
    return lam::require<LMD_TYPE_ELEMENT>(createElement(tag_name));
}

Item MarkBuilder::createMap() {
    return map().final();
}

lam::ItemOf<LMD_TYPE_MAP> MarkBuilder::createMapTyped() {
    return lam::require<LMD_TYPE_MAP>(createMap());
}

Item MarkBuilder::createArray() {
    return array().final();
}

lam::ItemOf<LMD_TYPE_ARRAY> MarkBuilder::createArrayTyped() {
    return lam::require<LMD_TYPE_ARRAY>(createArray());
}

Item MarkBuilder::createInt(int64_t value) {
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
    Item item = lambda_float_ptr_to_item(float_ptr);
    return item;
}

Item MarkBuilder::createBool(bool value) {
    return {.item = b2it(value)};
}

Item MarkBuilder::createNull() {
    return ItemNull;
}

Item MarkBuilder::createRange(int64_t start, int64_t end) {
    // Allocate Range from arena
    Range* range = (Range*)arena_alloc(arena_, sizeof(Range));
    if (!range) return createNull();

    range->type_id = LMD_TYPE_RANGE;
    range->flags = CONTAINER_FLAG_IMMORTAL;
    range->start = start;
    range->end = end;
    range->length = (end >= start) ? (end - start + 1) : 0;
    return {.range = range};
}

Item MarkBuilder::createType(TypeId type_id, bool is_literal, bool is_const) {
    // Allocate Type from arena
    Type* type = (Type*)arena_alloc(arena_, sizeof(Type));
    if (!type) return {.item = ITEM_UNDEFINED};

    type->type_id = type_id;
    type->is_literal = is_literal ? 1 : 0;
    type->is_const = is_const ? 1 : 0;

    return {.type = type};
}

Item MarkBuilder::createMetaType(TypeId type_id) {
    Type* sub_type = this->createType(type_id, true, true).type;
    if (sub_type) {
        TypeType *new_type = (TypeType*)this->createType(LMD_TYPE_TYPE, true, true).type;
        new_type->type = sub_type;
        return {.type = new_type};
    } else {
        log_debug("createMetaType: failed to create sub_type for type_id=%d", type_id);
    }
    return {.item = ITEM_UNDEFINED};
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

void MarkBuilder::putToElement(lam::GcPtr<Element> elmt, String* key, Item value) {
    elmt_put(elmt.get(), key, value, pool_);
}

void MarkBuilder::putToMap(lam::GcPtr<Map> map, String* key, Item value) {
    map_put(map.get(), key, value, input_);
}

//==============================================================================
// ElementBuilder Implementation
//==============================================================================

ElementBuilder::ElementBuilder(MarkBuilder* builder, const char* tag_name)
    : builder_(builder)
    , tag_name_(builder->createName(tag_name))  // element names are structural identifiers - always pooled
    , elmt_(nullptr)
{
    Input* input = builder_->input();
    Element* element = nullptr;

    if (builder->ui_mode()) {
        // UI mode: allocate DomElement (contains DomNode + Element + CSS fields)
        DomElement* dom = (DomElement*)arena_calloc(input->arena, sizeof(DomElement));
        if (!dom) return;
        dom->node_type = DOM_NODE_ELEMENT;
        element = dom_element_to_element(dom);
        element->type_id = LMD_TYPE_ELEMENT;
        element->type = &EmptyElmt;
    } else {
        element = elmt_arena(input->arena);  // Use arena allocation for MarkBuilder
    }

    if (element) {
        TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        if (element_type) {
            element->type = element_type;
            arraylist_append(input->type_list, element_type);
            element_type->type_index = input->type_list->length - 1;
            element_type->is_private_clone = true;
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
    builder_->putToElement(lam::gc_borrow(elmt_), key_str, value);
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
    builder_->putToElement(lam::gc_borrow(elmt_), key, value);
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
    if (builder_->ui_mode()) {
        // ui_mode: convert plain String to fat [DomText][String][chars] for unified DOM tree
        auto str_item = lam::as<LMD_TYPE_STRING>(item);
        String* s = str_item ? str_item.ptr() : nullptr;
        if (s && s->len > 0) {
            String* fat_s = builder_->createDomTextString(s->chars, s->len);
            if (fat_s) {
                item = (Item){.item = s2it(fat_s)};
            }
        }
    }
    array_append((Array*)elmt_, item, builder_->pool(), builder_->arena());
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
Item ElementBuilder::final() {
    // Set content_length to match the number of children in the element
    // This is required for the formatter to properly access children
    if (elmt_ && elmt_->type) {
        TypeElmt* elmt_type = (TypeElmt*)elmt_->type;
        List* list = (List*)elmt_;
        elmt_type->content_length = list->length;

        // finalize shape before returning - deduplicate via shape pool
        if (builder_->input()) {
            elmt_finalize_shape(elmt_type, builder_->input());
        }
    }
    return (Item){.element = elmt_};
}

lam::ItemOf<LMD_TYPE_ELEMENT> ElementBuilder::finalTyped() {
    return lam::require<LMD_TYPE_ELEMENT>(final());
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
    map_->is_immortal = 1;
    map_->type = &EmptyMap;  // Will be replaced by map_put on first insert
    map_->data = nullptr;
    map_->data_cap = 0;
}

MapBuilder::~MapBuilder() {
    // map_ is arena-allocated, no cleanup needed
}

MapBuilder& MapBuilder::put(const char* key, Item value) {
    // key could be null for nested map
    String* key_str = key ? builder_->createName(key) : nullptr;  // map keys are structural identifiers - always pooled
    builder_->putToMap(lam::gc_borrow(map_), key_str, value);

    // cache the type for convenience
    if (!map_type_) { map_type_ = (TypeMap*)map_->type; }
    return *this;
}

MapBuilder& MapBuilder::put(String* key, Item value) {
    if (!key) return *this;

    // use the existing string directly (caller responsible for pooling decision)
    builder_->putToMap(lam::gc_borrow(map_), key, value);

    // cache the type for convenience
    if (!map_type_) {
        map_type_ = (TypeMap*)map_->type;
    }

    return *this;
}

MapBuilder& MapBuilder::put(const char* key, const char* value) {
    return put(key, builder_->createStringItem(value));
}

MapBuilder& MapBuilder::put(const char* key, int32_t value) {
    return put(key, {.item = i2it(value)});
}

MapBuilder& MapBuilder::put(const char* key, int64_t value) {
    return put(key, builder_->createLong(value));
}

MapBuilder& MapBuilder::put(const char* key, double value) {
    return put(key, builder_->createFloat(value));
}

MapBuilder& MapBuilder::put(const char* key, bool value) {
    return put(key, builder_->createBool(value));
}

MapBuilder& MapBuilder::putNull(const char* key) {
    return put(key, ItemNull);
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
    // finalize shape before returning - deduplicate via shape pool
    if (map_type_ && builder_->input()) {
        map_finalize_shape(map_type_, builder_->input());
    }
    return (Item){.map = map_};
}

lam::ItemOf<LMD_TYPE_MAP> MapBuilder::finalTyped() {
    return lam::require<LMD_TYPE_MAP>(final());
}

//==============================================================================
// ArrayBuilder Implementation
//==============================================================================

ArrayBuilder::ArrayBuilder(MarkBuilder* builder)
    : builder_(builder)
    , array_(nullptr)
{
    // allocate Array from arena for MarkBuilder
    array_ = array_arena(builder_->arena());
}

ArrayBuilder::~ArrayBuilder() {
    // array_ is arena-allocated, no cleanup needed
}

ArrayBuilder& ArrayBuilder::append(Item item) {
    if (array_) {
        array_append(array_, item, builder_->pool(), builder_->arena());
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

lam::ItemOf<LMD_TYPE_ARRAY> ArrayBuilder::finalTyped() {
    return lam::require<LMD_TYPE_ARRAY>(final());
}

// ============================================================================
// Deep Copy Implementation (MarkBuilder)
// ============================================================================

/**
 * Check if a pointer is owned by this Input's arena chain
 * Traverses parent Input chain to check all arenas
 */
bool MarkBuilder::is_pointer_in_arena_chain(const void* ptr) const {
    if (!ptr) return false;

    // Traverse the Input parent chain
    Input* current = input_;
    while (current) {
        if (arena_owns(current->arena, ptr)) {
            return true;
        }
        current = current->parent;
    }

    return false;
}

/**
 * Check if an Item's data is in this Input's arena chain
 * Returns true if all pointer data is owned by an arena in the chain
 */
bool MarkBuilder::is_in_arena(Item item) const {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    // Inline types - always safe to reuse
    case LMD_TYPE_NULL:  case LMD_TYPE_BOOL:  case LMD_TYPE_INT:  case LMD_TYPE_ERROR:
        return true;

    case LMD_TYPE_FLOAT:
        // Self-tagged floats do not carry an arena pointer, so ownership is implicit.
        if ((item.item & ITEM_DBL_MASK) || item.double_ptr <= 1) return true;
        return is_pointer_in_arena_chain((void*)item.double_ptr);

    // Pointer types - check arena ownership
    case LMD_TYPE_INT64:  case LMD_TYPE_DECIMAL:
    case LMD_TYPE_STRING:  case LMD_TYPE_BINARY:  case LMD_TYPE_DTIME:
        return is_pointer_in_arena_chain((void*)item.string_ptr);

    case LMD_TYPE_SYMBOL: {
        Symbol* sym = item.get_safe_symbol();
        if (!sym) return true;

        // Check if in NamePool chain by chars (Symbol is no longer String)
        StrView sv = {.str = sym->chars, .length = sym->len};
        String* pooled = name_pool_lookup_strview(name_pool_, sv);
        if (pooled && pooled->len == sym->len && memcmp(pooled->chars, sym->chars, sym->len) == 0) return true;

        // Check arena ownership
        return is_pointer_in_arena_chain(sym);
    }

    // Container types - check container and all contents
    case LMD_TYPE_RANGE:  case LMD_TYPE_TYPE:
    case LMD_TYPE_ARRAY_NUM:
        return is_pointer_in_arena_chain(item.array);

    case LMD_TYPE_ARRAY: {
        List* list = item.array;
        if (!list) return true;  // Null list

        // Phase 5a: For lists, check BOTH struct ownership AND content ownership
        // Even if List struct is in our arena, it might contain external values
        for (int i = 0; i < list->length; i++) {
            if (!is_in_arena(list->items[i])) return false;  // External item found
        }

        // All items in arena - now check if List struct itself is in arena
        return is_pointer_in_arena_chain(list);
    }

    case LMD_TYPE_MAP: {
        Map* map = item.map;
        if (!map || !map->type || !map->data) return true;  // Null/empty map

        // Phase 5a: For maps, we need to check BOTH struct ownership AND content ownership
        // Even if the Map struct is in our arena, it might contain external values
        TypeMap* map_type = (TypeMap*)map->type;
        if (!map_type->shape) return true;  // No fields

        MapReader reader(map);
        lam::ShapeRef field = lam::shape_borrow(map_type->shape);
        while (field) {
            if (field->name && field->name->str) {
                ItemReader field_reader = reader.get(field->name->str);
                Item field_item = field_reader.item();
                if (!is_in_arena(field_item)) return false;  // External value found
            }
            field = lam::shape_next(field);
        }

        // All fields are in arena - now check if Map struct itself is in arena
        return is_pointer_in_arena_chain(map);
    }

    case LMD_TYPE_ELEMENT: {
        Element* elem = item.element;
        if (!elem || !elem->type) return true;  // Null/empty element

        // for elements, check BOTH struct ownership AND content ownership
        TypeElmt* elem_type = (TypeElmt*)elem->type;

        // Check all attributes
        if (elem_type->length > 0) {
            lam::ShapeRef attr = lam::shape_borrow(elem_type->shape);
            while (attr) {
                if (attr->name) {
                    Item attr_item = map_shape_field_to_item(elem->data, attr.get());
                    if (!is_in_arena(attr_item)) return false;  // External attribute found
                }
                attr = lam::shape_next(attr);
            }
        }

        // Check all children
        for (int i = 0; i < elem->length; i++) {
            if (!is_in_arena(elem->items[i])) return false;  // External child found
        }

        // All content in arena - now check if Element struct itself is in arena
        return is_pointer_in_arena_chain(elem);
    }

    default:
        // Unknown types - assume external
        return false;
    }
}

/**
 * Deep copy with smart ownership checking
 * Only copies if data is external to arena chain
 */
Item MarkBuilder::deep_copy(Item item) {
    // Quick check: if item contains no pointers (null, bool, int), just return it
    TypeId type_id = get_type_id(item);
    if (type_id <= LMD_TYPE_INT) {
        return item;  // Inline types - always safe
    }

    // Optimization: if data already in our arena chain, return as-is
    if (is_in_arena(item)) {
        return item;
    }

    // Data is external - perform deep copy
    return deep_copy_internal(item);
}

template<TypeId Tag>
Item MarkBuilder::deep_copy_typed(lam::ItemOf<Tag> typed) {
    Item item = typed.raw();

    if constexpr (Tag == LMD_TYPE_NULL || Tag == LMD_TYPE_BOOL || Tag == LMD_TYPE_INT) {
        return item;
    } else if constexpr (Tag == LMD_TYPE_INT64) {
        return createLong(typed.value());
    } else if constexpr (Tag == LMD_TYPE_FLOAT) {
        // Float Items may be self-tagged inline values; never assume pointer storage.
        return createFloat(typed.value());
    } else if constexpr (Tag == LMD_TYPE_SYMBOL) {
        Symbol* sym = typed.ptr();
        if (!sym) return createNull();
        Symbol* copied_sym = createSymbol(sym->chars, sym->len);
        return {.item = y2it(copied_sym)};
    } else if constexpr (Tag == LMD_TYPE_STRING) {
        String* str = typed.ptr();
        if (!str) return createNull();
        return createStringItem(str->chars, str->len);
    } else if constexpr (Tag == LMD_TYPE_BINARY) {
        Binary* bin = typed.ptr();
        if (!bin) return createNull();
        Binary* copied = createBinary(binary_data(bin), binary_length(bin));
        if (!copied) return createNull();
        return {.item = x2it(copied)};
    } else if constexpr (Tag == LMD_TYPE_DTIME) {
        DateTime* dt = typed.ptr();
        // DateTime is uint64_t bitfield, allocate from arena
        DateTime* dt_ptr = (DateTime*)arena_alloc(arena_, sizeof(DateTime));
        if (!dt_ptr) return ItemNull;
        *dt_ptr = *dt;
        return {.item = k2it(dt_ptr)};
    } else if constexpr (Tag == LMD_TYPE_DECIMAL) {
        // Use centralized decimal_deep_copy function
        return decimal_deep_copy(item, arena_, false);
    } else if constexpr (Tag == LMD_TYPE_RANGE) {
        Range* src_range = typed.ptr();
        return createRange(src_range->start, src_range->end);
    } else if constexpr (Tag == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = typed.ptr();
        size_t elem_size = sizeof(int64_t);  // 8 bytes for all elem types
        size_t size = sizeof(ArrayNum) + arr->length * elem_size;
        ArrayNum* new_arr = (ArrayNum*)arena_alloc(arena_, size);
        if (!new_arr) return ItemNull;

        new_arr->type_id = LMD_TYPE_ARRAY_NUM;
        new_arr->set_elem_type(arr->get_elem_type());  // copy elem_type from map_kind byte
        new_arr->capacity = new_arr->length = arr->length;
        new_arr->items = (int64_t*)((char*)new_arr + sizeof(ArrayNum));
        memcpy(new_arr->items, arr->items, arr->length * elem_size);
        return {.array_num = new_arr};
    } else if constexpr (Tag == LMD_TYPE_ARRAY) {
        Array* arr = typed.ptr();
        int64_t length = arr->length;       // arr->length is int64_t — do not truncate to int
        int64_t capacity = arr->capacity;
        (void)capacity;
        uint8_t src_flags = arr->flags;
        ArrayBuilder arr_builder = array();
        ArrayReader reader(lam::gc_borrow(arr));
        for (int64_t i = 0; i < length; i++) {
            Item child = reader.get(i).item();
            Item copied_child = deep_copy_internal(child);
            arr_builder.append(copied_child);
        }
        Item result = arr_builder.final();
        // preserve is_content flag from source array
        if (result.array) {
            result.array->flags |= (src_flags & 0x01); // is_content bit
        }
        return result;
    } else if constexpr (Tag == LMD_TYPE_MAP) {
        Map* src_map = typed.ptr();
        TypeMap* map_type = (TypeMap*)src_map->type;
        (void)map_type;
        MapBuilder map_builder = map();
        MapReader reader(lam::gc_borrow(src_map));
        MapReader::EntryIterator iter = reader.entries();
        const char* key;  ItemReader value;
        while (iter.next(&key, &value)) {
            Item field_item = value.item();
            Item copied_field = deep_copy_internal(field_item);
            if (key) {
                String* key_name = createName(key, strlen(key));
                map_builder.put(key_name, copied_field);
            } else {
                map_builder.put(key, copied_field);
            }
        }
        return map_builder.final();
    } else if constexpr (Tag == LMD_TYPE_OBJECT) {
        Object* src_obj = typed.ptr();
        TypeObject* obj_type = (TypeObject*)src_obj->type;

        // allocate new object with same data layout using arena
        int data_size = obj_type->byte_size > 0 ? obj_type->byte_size : (int)(obj_type->length * sizeof(Item));
        Object* new_obj = (Object*)arena_calloc(arena_, sizeof(Object) + data_size);
        new_obj->type_id = LMD_TYPE_OBJECT;
        new_obj->is_immortal = 1;
        new_obj->type = (Type*)obj_type;
        new_obj->data = (char*)new_obj + sizeof(Object);
        new_obj->data_cap = data_size;

        // copy the entire data buffer first (handles primitives: bool, int, float, datetime)
        if (src_obj->data && data_size > 0) {
            memcpy(new_obj->data, src_obj->data, data_size);
        }

        // deep copy referenced types (strings, containers, etc.)
        lam::ShapeRef field = lam::shape_borrow(obj_type->shape);
        while (field) {
            void* dst_ptr = (char*)new_obj->data + field->byte_offset;
            TypeId ftype = field->type->type_id;
            switch (ftype) {
            case LMD_TYPE_STRING: case LMD_TYPE_BINARY: {
                String* str = *(String**)dst_ptr;
                if (str) {
                    Item copied = createStringItem(str->chars, str->len);
                    *(String**)dst_ptr = copied.get_safe_string();
                }
                break;
            }
            case LMD_TYPE_SYMBOL: {
                Symbol* sym = *(Symbol**)dst_ptr;
                if (sym) {
                    Symbol* copied = createSymbol(sym->chars, sym->len);
                    *(Symbol**)dst_ptr = copied;
                }
                break;
            }
            case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
            case LMD_TYPE_RANGE: case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
                Container* container = *(Container**)dst_ptr;
                if (container) {
                    Item copied = deep_copy_internal({.container = container});
                    *(Container**)dst_ptr = copied.container;
                }
                break;
            }
            default:
                // primitive types (bool, int, int64, float, datetime, decimal) are already copied via memcpy
                break;
            }
            field = lam::shape_next(field);
        }

        return {.object = new_obj};
    } else if constexpr (Tag == LMD_TYPE_ELEMENT) {
        log_enter();
        Element* elem = typed.ptr();
        ElementReader reader(lam::gc_borrow(elem));

        // Get tag name via reader (returns const char*)
        const char* tag_name = reader.tagName();
        ElementBuilder elem_builder = element(tag_name);

        // Copy attributes using ElementReader
        // ElementReader.get_attr() handles proper Item reconstruction from stored data
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        if (elem_type->length > 0) {
            lam::ShapeRef attr = lam::shape_borrow(elem_type->shape);
            while (attr) {
                Item attr_item = map_shape_field_to_item(elem->data, attr.get());
                Item copied_item = deep_copy_internal(attr_item);
                if (attr->name) {
                    // Copy attribute name bytes from external NamePool
                    String* attr_name = createName(attr->name->str, attr->name->length);
                    elem_builder.attr(attr_name, copied_item);
                } else {
                    // attr->name is null for nested map
                    elem_builder.attr((String*)nullptr, copied_item);
                }
                attr = lam::shape_next(attr);
            }
        }

        // copy children
        for (int i = 0; i < reader.childCount(); i++) {
            ItemReader child_reader = reader.childAt(i);
            Item child = child_reader.item();
            Item copied_child = deep_copy_internal(child);
            elem_builder.child(copied_child);
        }
        log_leave();
        return elem_builder.final();
    } else if constexpr (Tag == LMD_TYPE_TYPE) {
        TypeType* source_type = (TypeType*)typed.ptr();
        if (!source_type || !source_type->type) return ItemError;
        if (source_type->type == &TYPE_NUMBER) {
            // `number` is an abstract type singleton; copying by TypeId would collapse it to `type`.
            return typed.raw();
        }
        return createMetaType(source_type->type->type_id);
    } else if constexpr (Tag == LMD_TYPE_PATH) {
        // For sys:// paths, resolve and deep-copy the result
        Path* path = typed.ptr();
        if (path && path_get_scheme(path) == PATH_SCHEME_SYS) {
            // If already resolved, deep-copy the result
            if (path->result != 0) {
                return deep_copy_internal({.item = path->result});
            }
            // Note: path_resolve_for_iteration is only available in full lambda runtime,
            // not in lambda-input library. Caller is responsible for resolving paths
            // before deep_copy if needed.
        }
        // For non-sys paths, we need to deep-copy the path structure
        // For now, just return as-is (caller should be aware paths may reference external memory)
        return item;
    } else if constexpr (Tag == LMD_TYPE_ANY || Tag == LMD_TYPE_ERROR || Tag == LMD_TYPE_UNDEFINED) {
        return item;
    } else {
        log_debug("deep_copy_typed: unsupported type_id=%d, returning null", Tag);
        return ItemNull;
    }
}

Item MarkBuilder::deep_copy_unknown(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("deep_copy_unknown: unsupported type_id=%d, returning null", type_id);
    return ItemNull;
}

Item MarkBuilder::deep_copy_internal(Item item) {
    DeepCopyVisitor visitor = {this};
    return lam::visit(item, visitor);
}
