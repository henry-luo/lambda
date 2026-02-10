#include "mark_reader.hpp"
#include "lambda-data.hpp"
#include "../lib/stringbuf.h"
#include <cstring>
#include <cstdlib>

// ==============================================================================
// MarkReader Implementation
// ==============================================================================

MarkReader::MarkReader(Item root)
    : root_(root) {
}

ItemReader MarkReader::getRoot() const {
    return ItemReader(root_.to_const());
}

MarkReader::ElementIterator MarkReader::findAll(const char* selector) const {
    return ElementIterator(this, selector);
}

// MarkReader::ElementIterator implementation
MarkReader::ElementIterator::ElementIterator(const MarkReader* reader, const char* selector)
    : reader_(reader), selector_(selector), current_index_(0), state_(nullptr) {
}

MarkReader::ElementIterator::~ElementIterator() {
    // Cleanup state if needed
    if (state_) {
        // TODO: Free traversal state
        state_ = nullptr;
    }
}

bool MarkReader::ElementIterator::next(ItemReader* out) {
    // Simple implementation: linear search through root's children
    // TODO: Implement proper tree traversal for nested elements
    ItemReader root = reader_->getRoot();

    if (root.isElement()) {
        ElementReader elem = root.asElement();
        while (current_index_ < elem.childCount()) {
            ItemReader child = elem.childAt(current_index_++);
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                if (selector_ == nullptr || child_elem.hasTag(selector_)) {
                    *out = child;
                    return true;
                }
            }
        }
    }

    return false;
}

void MarkReader::ElementIterator::reset() {
    current_index_ = 0;
}

// ==============================================================================
// ItemReader Implementation
// ==============================================================================

ItemReader::ItemReader()
    : item_({.item = ITEM_NULL}), cached_type_(LMD_TYPE_NULL) {
}

ItemReader::ItemReader(ConstItem item)
    : item_(*(Item*)&item), cached_type_(item.type_id()) {
}

bool ItemReader::isNull() const {
    return cached_type_ == LMD_TYPE_NULL;
}

bool ItemReader::isString() const {
    return cached_type_ == LMD_TYPE_STRING;
}

bool ItemReader::isSymbol() const {
    return cached_type_ == LMD_TYPE_SYMBOL;
}

bool ItemReader::isInt() const {
    return cached_type_ == LMD_TYPE_INT || cached_type_ == LMD_TYPE_INT64;
}

bool ItemReader::isFloat() const {
    return cached_type_ == LMD_TYPE_FLOAT;
}

bool ItemReader::isBool() const {
    return cached_type_ == LMD_TYPE_BOOL;
}

bool ItemReader::isElement() const {
    return cached_type_ == LMD_TYPE_ELEMENT;
}

bool ItemReader::isMap() const {
    return cached_type_ == LMD_TYPE_MAP;
}

bool ItemReader::isArray() const {
    return cached_type_ == LMD_TYPE_ARRAY;
}

bool ItemReader::isList() const {
    return cached_type_ == LMD_TYPE_LIST;
}

bool ItemReader::isDatetime() const {
    return cached_type_ == LMD_TYPE_DTIME;
}

String* ItemReader::asString() const {
    if (isString()) { return item_.get_string(); }
    return nullptr;
}

String* ItemReader::asSymbol() const {
    if (isSymbol()) { return item_.get_string(); }
    return nullptr;
}

int64_t ItemReader::asInt() const {
    if (cached_type_ == LMD_TYPE_INT) {
        return item_.get_int56();
    } else if (cached_type_ == LMD_TYPE_INT64) {
        return item_.get_int64();
    }
    return 0;
}

int32_t ItemReader::asInt32() const {
    if (cached_type_ == LMD_TYPE_INT) {
        return (int32_t)item_.get_int56();  // Truncate to 32-bit
    } else if (cached_type_ == LMD_TYPE_INT64) {
        int64_t val = item_.get_int64();
        return (int32_t)val;  // Truncate to 32-bit
    }
    return 0;
}

double ItemReader::asFloat() const {
    if (isFloat()) { return item_.get_double(); }
    return NAN;
}

bool ItemReader::asBool() const {
    if (isBool()) {
        return item_.bool_val;
    }
    return false;
}

DateTime ItemReader::asDatetime() const {
    if (isDatetime()) {
        return item_.get_datetime();
    }
    DateTime empty;
    memset(&empty, 0, sizeof(DateTime));
    return empty;
}

ElementReader ItemReader::asElement() const {
    if (isElement()) {
        return ElementReader(item_.element);
    }
    return ElementReader();  // Invalid element
}

MapReader ItemReader::asMap() const {
    if (isMap()) {
        return MapReader(item_.map);
    }
    return MapReader();  // Invalid map
}

ArrayReader ItemReader::asArray() const {
    if (isArray() || isList()) {
        return ArrayReader(item_.array);
    }
    return ArrayReader();  // Invalid array
}

const char* ItemReader::cstring() const {
    String* str = asString();
    if (!str) {
        str = asSymbol();  // Also check for symbol type
    }
    return str ? str->chars : nullptr;
}

// ==============================================================================
// MapReader Implementation
// ==============================================================================

MapReader::MapReader()
    : map_(nullptr), map_type_(nullptr) {
}

MapReader::MapReader(Map* map)
    : map_(map) {
    if (map) {
        map_type_ = (TypeMap*)map->type;
    } else {
        map_type_ = nullptr;
    }
}

MapReader MapReader::fromItem(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_MAP) {
        return MapReader(item.map);
    }
    return MapReader();  // Invalid
}

ItemReader MapReader::get(const char* key) const {
    if (!map_ || !map_type_) { return ItemReader(); }
    ConstItem value = map_->get(key);
    return ItemReader(value);
}

bool MapReader::has(const char* key) const {
    if (!map_ || !map_type_) { return false; }

    ShapeEntry* field = map_type_->shape;
    while (field) {
        if (strcmp(field->name->str, key) == 0) {
            return true;
        }
        field = field->next;
    }

    return false;
}

int64_t MapReader::size() const {
    if (!map_ || !map_type_) { return 0; }
    return map_type_->length;
}

MapReader::KeyIterator MapReader::keys() const {
    return KeyIterator(this);
}

MapReader::ValueIterator MapReader::values() const {
    return ValueIterator(this);
}

MapReader::EntryIterator MapReader::entries() const {
    return EntryIterator(this);
}

// MapReader::KeyIterator implementation
MapReader::KeyIterator::KeyIterator(const MapReader* reader)
    : reader_(reader), current_field_(nullptr) {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

bool MapReader::KeyIterator::next(const char** key) {
    if (!current_field_) {
        return false;
    }

    *key = current_field_->name->str;
    current_field_ = current_field_->next;
    return true;
}

void MapReader::KeyIterator::reset() {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

// MapReader::ValueIterator implementation
MapReader::ValueIterator::ValueIterator(const MapReader* reader)
    : reader_(reader), current_field_(nullptr) {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

bool MapReader::ValueIterator::next(ItemReader* value) {
    if (!current_field_) {
        return false;
    }

    // Get value using the key
    const char* key = current_field_->name->str;
    *value = reader_->get(key);

    current_field_ = current_field_->next;
    return true;
}

void MapReader::ValueIterator::reset() {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

// MapReader::EntryIterator implementation
MapReader::EntryIterator::EntryIterator(const MapReader* reader)
    : reader_(reader), current_field_(nullptr) {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

Item _map_field_to_item(void* field_ptr, TypeId type_id);

bool MapReader::EntryIterator::next(const char** key, ItemReader* value) {
    if (!current_field_) { return false; }
    *key = current_field_->name ? current_field_->name->str : nullptr;
    // get field value based on offset
    void* field_ptr = (char*)reader_->map_->data + current_field_->byte_offset;
    Item result = _map_field_to_item(field_ptr, current_field_->type->type_id);
    *value = ItemReader(result.to_const());
    current_field_ = current_field_->next;
    return true;
}

void MapReader::EntryIterator::reset() {
    if (reader_->map_type_) {
        current_field_ = reader_->map_type_->shape;
    }
}

// ==============================================================================
// ArrayReader Implementation
// ==============================================================================

ArrayReader::ArrayReader()
    : array_(nullptr) {
}

ArrayReader::ArrayReader(Array* array)
    : array_(array) {
}

ArrayReader ArrayReader::fromItem(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_ARRAY) {
        return ArrayReader(item.array);
    }
    return ArrayReader();  // Invalid
}

ItemReader ArrayReader::get(int64_t index) const {
    if (!array_ || index < 0 || index >= array_->length) {
        return ItemReader();
    }
    return ItemReader(array_->items[index].to_const());
}

int64_t ArrayReader::length() const {
    return array_ ? array_->length : 0;
}

ArrayReader::Iterator ArrayReader::items() const {
    return Iterator(this);
}

// ArrayReader::Iterator implementation
ArrayReader::Iterator::Iterator(const ArrayReader* reader)
    : reader_(reader), index_(0) {
}

bool ArrayReader::Iterator::next(ItemReader* item) {
    if (!reader_->array_ || index_ >= reader_->array_->length) {
        return false;
    }

    *item = reader_->get(index_++);
    return true;
}

void ArrayReader::Iterator::reset() {
    index_ = 0;
}

// ==============================================================================
// ElementReaderWrapper Implementation (Stack-Based, No Pool)
// ==============================================================================

ElementReader::ElementReader()
    : element_(nullptr), element_type_(nullptr), tag_name_(nullptr),
      tag_name_len_(0), child_count_(0), attr_count_(0) {
}

ElementReader::ElementReader(const Element* element)
    : element_(element) {
    if (element) {
        element_type_ = (const TypeElmt*)element->type;

        if (element_type_) {
            tag_name_ = element_type_->name.str;
            tag_name_len_ = element_type_->name.length;
        } else {
            tag_name_ = nullptr;
            tag_name_len_ = 0;
        }

        // Cache child count (Element inherits from List)
        const List* list = (const List*)element;
        child_count_ = list->length;

        // Cache attribute count from the map shape
        attr_count_ = 0;
        if (element_type_) {
            const TypeMap* map_type = (const TypeMap*)element_type_;
            attr_count_ = map_type->length;
        }
    } else {
        element_type_ = nullptr;
        tag_name_ = nullptr;
        tag_name_len_ = 0;
        child_count_ = 0;
        attr_count_ = 0;
    }
}

ElementReader::ElementReader(Item item) {
    if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        *this = ElementReader(item.element);
    } else {
        *this = ElementReader();
    }
}

bool ElementReader::hasTag(const char* tag_name) const {
    if (!tag_name_ || !tag_name) return false;
    return strcmp(tag_name_, tag_name) == 0;
}

bool ElementReader::isEmpty() const {
    if (!element_) return true;

    // Check if has no children
    if (child_count_ == 0) return true;

    // Check if all children are empty strings
    const List* list = (const List*)element_;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_ELEMENT) {
            return false; // has child elements
        } else if (type == LMD_TYPE_STRING) {
            String* str = child.get_string();
            if (str && str->len > 0) {
                return false; // has non-empty text
            }
        } else if (type != LMD_TYPE_NULL) {
            return false; // has other content
        }
    }

    return true;
}

bool ElementReader::isTextOnly() const {
    if (!element_ || child_count_ == 0) return false;

    const List* list = (const List*)element_;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_ELEMENT) {
            return false; // has child elements
        }
    }

    return true; // only text/non-element content
}

ItemReader ElementReader::childAt(int64_t index) const {
    if (!element_ || index < 0 || index >= child_count_) {
        return ItemReader();
    }
    Item child = ((const List*)element_)->items[index];
    return ItemReader(child.to_const());
}

ItemReader ElementReader::findChild(const char* tag_name) const {
    if (!element_ || !tag_name) return ItemReader();

    const List* list = (const List*)element_;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];

        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            TypeElmt* child_type = (TypeElmt*)child_elem->type;

            if (child_type && child_type->name.str &&
                strcmp(child_type->name.str, tag_name) == 0) {
                return ItemReader(child.to_const());
            }
        }
    }

    return ItemReader();
}

// Forward declaration for recursive helper
static void _extract_text_recursive_inline(const Element* element, StringBuf* sb);

void ElementReader::textContent(StringBuf* sb) const {
    if (!element_ || !sb) return;
    _extract_text_recursive_inline(element_, sb);
}

ElementReader ElementReader::findChildElement(const char* tag_name) const {
    if (!element_ || !tag_name) return ElementReader();

    const List* list = (const List*)element_;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];

        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            TypeElmt* child_type = (TypeElmt*)child_elem->type;

            if (child_type && child_type->name.str &&
                strcmp(child_type->name.str, tag_name) == 0) {
                return ElementReader(child_elem);
            }
        }
    }

    return ElementReader();
}

bool ElementReader::hasChildElements() const {
    if (!element_) return false;

    const List* list = (const List*)element_;
    for (int64_t i = 0; i < child_count_; i++) {
        Item child = list->items[i];
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            return true;
        }
    }

    return false;
}

void ElementReader::allText(StringBuf* sb) const {
    textContent(sb);  // Alias for textContent
}

// Attribute access methods (consolidated from AttributeReader)
bool ElementReader::has_attr(const char* key) const {
    if (!element_ || !element_type_ || !key) return false;

    const TypeMap* map_type = (const TypeMap*)element_type_;
    const ShapeEntry* shape = map_type->shape;
    if (!shape) return false;

    const ShapeEntry* field = shape;
    size_t key_len = strlen(key);

    while (field) {
        if (field->name && field->name->length == key_len &&
            strncmp(field->name->str, key, key_len) == 0) {
            return true;
        }
        field = field->next;
    }

    return false;
}

const char* ElementReader::get_attr_string(const char* key) const {
    if (!element_ || !element_type_ || !key) return nullptr;

    const TypeMap* map_type = (const TypeMap*)element_type_;
    const ShapeEntry* shape = map_type->shape;
    const void* attr_data = element_->data;

    if (!shape || !attr_data) {
        return nullptr;
    }

    const ShapeEntry* field = shape;
    size_t key_len = strlen(key);

    while (field) {
        if (field->name && field->name->length == key_len &&
            strncmp(field->name->str, key, key_len) == 0) {

            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                const void* data = ((const char*)attr_data) + field->byte_offset;
                const String* str = *(const String**)data;
                return str ? str->chars : nullptr;
            }
            break;
        }
        field = field->next;
    }

    return nullptr;
}

ItemReader ElementReader::get_attr(const char* key) const {
    if (!element_) return ItemReader();
    return ItemReader(element_->get_attr(key));
}

// Typed attribute accessors
String* ElementReader::get_string_attr(const char* attr_name) const {
    ItemReader attr = get_attr(attr_name);
    return attr.isString() ? attr.asString() : nullptr;
}

int64_t ElementReader::get_int_attr(const char* attr_name, int64_t default_val) const {
    ItemReader attr = get_attr(attr_name);
    return attr.isInt() ? attr.asInt() : default_val;
}

bool ElementReader::get_bool_attr(const char* attr_name, bool default_val) const {
    ItemReader attr = get_attr(attr_name);
    return attr.isBool() ? attr.asBool() : default_val;
}

// Helper function for recursive text extraction
static void _extract_text_recursive_inline(const Element* element, StringBuf* sb) {
    if (!element || !sb) return;

    const List* list = (const List*)element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            String* str = child.get_string();
            if (str && str->len > 0) {
                stringbuf_append_str_n(sb, str->chars, str->len);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            // Recursively extract text from child elements
            _extract_text_recursive_inline(child.element, sb);
        }
    }
}

// ElementReaderWrapper::ChildIterator implementation
ElementReader::ChildIterator::ChildIterator(const ElementReader* reader)
    : reader_(reader), index_(0) {
}

bool ElementReader::ChildIterator::next(ItemReader* item) {
    if (!reader_->element_ || index_ >= reader_->childCount()) {
        return false;
    }

    *item = reader_->childAt(index_++);
    return true;
}

void ElementReader::ChildIterator::reset() {
    index_ = 0;
}

// ElementReaderWrapper::ElementChildIterator implementation
ElementReader::ElementChildIterator::ElementChildIterator(const ElementReader* reader)
    : reader_(reader), index_(0) {
}

bool ElementReader::ElementChildIterator::next(ElementReader* elem) {
    if (!reader_->element_) {
        return false;
    }

    const List* list = (const List*)reader_->element_;
    while (index_ < reader_->childCount()) {
        Item child = list->items[index_++];
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            *elem = ElementReader(child.element);
            return true;
        }
    }

    return false;
}

void ElementReader::ElementChildIterator::reset() {
    index_ = 0;
}

ElementReader::ChildIterator ElementReader::children() const {
    return ChildIterator(this);
}

ElementReader::ElementChildIterator ElementReader::childElements() const {
    return ElementChildIterator(this);
}
