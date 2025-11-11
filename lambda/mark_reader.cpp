#include "mark_reader.hpp"
#include "lambda-data.hpp"
#include "element_reader.h"
#include <cstring>
#include <cstdlib>

// ==============================================================================
// MarkReader Implementation
// ==============================================================================

MarkReader::MarkReader(Item root, Pool* pool)
    : root_(root), pool_(pool) {
}

ItemReader MarkReader::getRoot() const {
    return ItemReader(root_, pool_);
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
        ElementReaderWrapper elem = root.asElement();
        while (current_index_ < elem.childCount()) {
            ItemReader child = elem.childAt(current_index_++);
            if (child.isElement()) {
                ElementReaderWrapper child_elem = child.asElement();
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
    : item_({.item = ITEM_NULL}), pool_(nullptr), cached_type_(LMD_TYPE_NULL) {
}

ItemReader::ItemReader(Item item, Pool* pool)
    : item_(item), pool_(pool), cached_type_(get_type_id(item)) {
}

bool ItemReader::isNull() const {
    return cached_type_ == LMD_TYPE_NULL;
}

bool ItemReader::isString() const {
    return cached_type_ == LMD_TYPE_STRING;
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

String* ItemReader::asString() const {
    if (isString()) {
        return get_string(item_);
    }
    return nullptr;
}

int64_t ItemReader::asInt() const {
    if (cached_type_ == LMD_TYPE_INT) {
        return item_.int_val;
    } else if (cached_type_ == LMD_TYPE_INT64) {
        return *((int64_t*)item_.pointer);
    }
    return 0;
}

int32_t ItemReader::asInt32() const {
    if (cached_type_ == LMD_TYPE_INT) {
        return item_.int_val;
    } else if (cached_type_ == LMD_TYPE_INT64) {
        int64_t val = *((int64_t*)item_.pointer);
        return (int32_t)val;  // Truncate to 32-bit
    }
    return 0;
}

double ItemReader::asFloat() const {
    if (isFloat()) {
        return *((double*)item_.pointer);
    }
    return 0.0;
}

bool ItemReader::asBool() const {
    if (isBool()) {
        return item_.bool_val;
    }
    return false;
}

ElementReaderWrapper ItemReader::asElement() const {
    if (isElement()) {
        return ElementReaderWrapper(item_.element, pool_);
    }
    return ElementReaderWrapper();  // Invalid element
}

MapReader ItemReader::asMap() const {
    if (isMap()) {
        return MapReader(item_.map, pool_);
    }
    return MapReader();  // Invalid map
}

ArrayReader ItemReader::asArray() const {
    if (isArray()) {
        return ArrayReader(item_.array, pool_);
    }
    return ArrayReader();  // Invalid array
}

const char* ItemReader::cstring() const {
    String* str = asString();
    return str ? str->chars : nullptr;
}

// ==============================================================================
// MapReader Implementation
// ==============================================================================

MapReader::MapReader()
    : map_(nullptr), map_type_(nullptr), pool_(nullptr) {
}

MapReader::MapReader(Map* map, Pool* pool)
    : map_(map), pool_(pool) {
    if (map) {
        map_type_ = (TypeMap*)map->type;
    } else {
        map_type_ = nullptr;
    }
}

MapReader MapReader::fromItem(Item item, Pool* pool) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_MAP) {
        return MapReader(item.map, pool);
    }
    return MapReader();  // Invalid
}

ItemReader MapReader::get(const char* key) const {
    if (!map_ || !map_type_) {
        return ItemReader();
    }
    
    // Walk the shape to find the field
    ShapeEntry* field = map_type_->shape;
    while (field) {
        if (strcmp(field->name->str, key) == 0) {
            // Found the field, extract value
            void* data_ptr = ((char*)map_->data) + field->byte_offset;
            
            // Unpack the value based on type
            TypeId field_type = field->type->type_id;
            Item value;
            
            if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                value.int_val = *((int64_t*)data_ptr);
                value.type_id = field_type;
            } else if (field_type == LMD_TYPE_FLOAT) {
                // Float is stored inline in the map data, but Item expects a pointer
                // So we point to the location in the map's data
                value.pointer = (uint64_t)data_ptr;
                value.type_id = LMD_TYPE_FLOAT;
            } else if (field_type == LMD_TYPE_BOOL) {
                value.bool_val = *((bool*)data_ptr);
                value.type_id = LMD_TYPE_BOOL;
            } else {
                // Pointer type - store as raw pointer
                void* ptr_val = *((void**)data_ptr);
                value.raw_pointer = ptr_val;
                value.type_id = field_type;
            }
            
            return ItemReader(value, pool_);
        }
        field = field->next;
    }
    
    return ItemReader();  // Key not found
}

bool MapReader::has(const char* key) const {
    if (!map_ || !map_type_) {
        return false;
    }
    
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
    if (!map_ || !map_type_) {
        return 0;
    }
    
    int64_t count = 0;
    ShapeEntry* field = map_type_->shape;
    while (field) {
        count++;
        field = field->next;
    }
    
    return count;
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

bool MapReader::EntryIterator::next(const char** key, ItemReader* value) {
    if (!current_field_) {
        return false;
    }
    
    *key = current_field_->name->str;
    *value = reader_->get(*key);
    
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
    : array_(nullptr), pool_(nullptr) {
}

ArrayReader::ArrayReader(Array* array, Pool* pool)
    : array_(array), pool_(pool) {
}

ArrayReader ArrayReader::fromItem(Item item, Pool* pool) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_ARRAY) {
        return ArrayReader(item.array, pool);
    }
    return ArrayReader();  // Invalid
}

ItemReader ArrayReader::get(int64_t index) const {
    if (!array_ || index < 0 || index >= array_->length) {
        return ItemReader();
    }
    
    return ItemReader(array_->items[index], pool_);
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
// ElementReaderWrapper Implementation (C++ Wrapper)
// ==============================================================================

ElementReaderWrapper::ElementReaderWrapper()
    : reader_(nullptr), pool_(nullptr), owns_reader_(false) {
}

ElementReaderWrapper::ElementReaderWrapper(const Element* element, Pool* pool)
    : pool_(pool), owns_reader_(true) {
    reader_ = element_reader_create(element, pool);
}

ElementReaderWrapper::ElementReaderWrapper(Item item, Pool* pool)
    : pool_(pool), owns_reader_(true) {
    reader_ = element_reader_from_item(item, pool);
}

ElementReaderWrapper::ElementReaderWrapper(::ElementReader* reader, Pool* pool, bool take_ownership)
    : reader_(reader), pool_(pool), owns_reader_(take_ownership) {
}

ElementReaderWrapper::~ElementReaderWrapper() {
    if (owns_reader_ && reader_) {
        element_reader_free(reader_, pool_);
        reader_ = nullptr;
    }
}

ElementReaderWrapper::ElementReaderWrapper(const ElementReaderWrapper& other)
    : pool_(other.pool_), owns_reader_(true) {
    if (other.reader_ && other.reader_->element) {
        reader_ = element_reader_create(other.reader_->element, pool_);
    } else {
        reader_ = nullptr;
    }
}

ElementReaderWrapper& ElementReaderWrapper::operator=(const ElementReaderWrapper& other) {
    if (this != &other) {
        if (owns_reader_ && reader_) {
            element_reader_free(reader_, pool_);
        }
        
        pool_ = other.pool_;
        owns_reader_ = true;
        
        if (other.reader_ && other.reader_->element) {
            reader_ = element_reader_create(other.reader_->element, pool_);
        } else {
            reader_ = nullptr;
        }
    }
    return *this;
}

ElementReaderWrapper::ElementReaderWrapper(ElementReaderWrapper&& other) noexcept
    : reader_(other.reader_), pool_(other.pool_), owns_reader_(other.owns_reader_) {
    other.reader_ = nullptr;
    other.owns_reader_ = false;
}

ElementReaderWrapper& ElementReaderWrapper::operator=(ElementReaderWrapper&& other) noexcept {
    if (this != &other) {
        if (owns_reader_ && reader_) {
            element_reader_free(reader_, pool_);
        }
        
        reader_ = other.reader_;
        pool_ = other.pool_;
        owns_reader_ = other.owns_reader_;
        
        other.reader_ = nullptr;
        other.owns_reader_ = false;
    }
    return *this;
}

const char* ElementReaderWrapper::tagName() const {
    return reader_ ? element_reader_tag_name(reader_) : nullptr;
}

int64_t ElementReaderWrapper::tagNameLen() const {
    return reader_ ? element_reader_tag_name_len(reader_) : 0;
}

bool ElementReaderWrapper::hasTag(const char* tag_name) const {
    return reader_ ? element_reader_has_tag(reader_, tag_name) : false;
}

int64_t ElementReaderWrapper::childCount() const {
    return reader_ ? element_reader_child_count(reader_) : 0;
}

int64_t ElementReaderWrapper::attrCount() const {
    return reader_ ? element_reader_attr_count(reader_) : 0;
}

bool ElementReaderWrapper::isEmpty() const {
    return reader_ ? element_reader_is_empty(reader_) : true;
}

bool ElementReaderWrapper::isTextOnly() const {
    return reader_ ? element_reader_is_text_only(reader_) : false;
}

ItemReader ElementReaderWrapper::childAt(int64_t index) const {
    if (!reader_) {
        return ItemReader();
    }
    Item child = element_reader_child_at(reader_, index);
    return ItemReader(child, pool_);
}

ItemReader ElementReaderWrapper::findChild(const char* tag_name) const {
    if (!reader_) {
        return ItemReader();
    }
    Item child = element_reader_find_child(reader_, tag_name);
    return ItemReader(child, pool_);
}

String* ElementReaderWrapper::textContent() const {
    return reader_ ? element_reader_text_content(reader_, pool_) : nullptr;
}

ElementReaderWrapper ElementReaderWrapper::findChildElement(const char* tag_name) const {
    ItemReader child = findChild(tag_name);
    if (child.isElement()) {
        return child.asElement();
    }
    return ElementReaderWrapper();
}

bool ElementReaderWrapper::hasChildElements() const {
    if (!reader_) {
        return false;
    }
    
    for (int64_t i = 0; i < childCount(); i++) {
        Item child = element_reader_child_at(reader_, i);
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            return true;
        }
    }
    
    return false;
}

String* ElementReaderWrapper::allText() const {
    return textContent();  // Alias for textContent
}

const Element* ElementReaderWrapper::element() const {
    return reader_ ? reader_->element : nullptr;
}

// ElementReaderWrapper::ChildIterator implementation
ElementReaderWrapper::ChildIterator::ChildIterator(const ElementReaderWrapper* reader)
    : reader_(reader), index_(0) {
}

bool ElementReaderWrapper::ChildIterator::next(ItemReader* item) {
    if (!reader_->reader_ || index_ >= reader_->childCount()) {
        return false;
    }
    
    *item = reader_->childAt(index_++);
    return true;
}

void ElementReaderWrapper::ChildIterator::reset() {
    index_ = 0;
}

// ElementReaderWrapper::ElementChildIterator implementation
ElementReaderWrapper::ElementChildIterator::ElementChildIterator(const ElementReaderWrapper* reader)
    : reader_(reader), index_(0) {
}

bool ElementReaderWrapper::ElementChildIterator::next(ElementReaderWrapper* elem) {
    if (!reader_->reader_) {
        return false;
    }
    
    while (index_ < reader_->childCount()) {
        ItemReader child = reader_->childAt(index_++);
        if (child.isElement()) {
            *elem = child.asElement();
            return true;
        }
    }
    
    return false;
}

void ElementReaderWrapper::ElementChildIterator::reset() {
    index_ = 0;
}

ElementReaderWrapper::ChildIterator ElementReaderWrapper::children() const {
    return ChildIterator(this);
}

ElementReaderWrapper::ElementChildIterator ElementReaderWrapper::childElements() const {
    return ElementChildIterator(this);
}

// ==============================================================================
// AttributeReaderWrapper Implementation (C++ Wrapper)
// ==============================================================================

AttributeReaderWrapper::AttributeReaderWrapper()
    : attr_reader_(nullptr), pool_(nullptr), owns_reader_(false) {
}

AttributeReaderWrapper::AttributeReaderWrapper(const ElementReaderWrapper& elem)
    : pool_(elem.pool()), owns_reader_(true) {
    if (elem.isValid()) {
        attr_reader_ = element_reader_attributes(elem.cReader(), pool_);
    } else {
        attr_reader_ = nullptr;
    }
}

AttributeReaderWrapper::AttributeReaderWrapper(::AttributeReader* attr_reader, Pool* pool, bool take_ownership)
    : attr_reader_(attr_reader), pool_(pool), owns_reader_(take_ownership) {
}

AttributeReaderWrapper::~AttributeReaderWrapper() {
    if (owns_reader_ && attr_reader_) {
        attribute_reader_free(attr_reader_, pool_);
        attr_reader_ = nullptr;
    }
}

AttributeReaderWrapper::AttributeReaderWrapper(const AttributeReaderWrapper& other)
    : pool_(other.pool_), owns_reader_(true) {
    // Cannot easily copy AttributeReader without element reference
    // For now, share the same reader (not thread-safe but matches usage pattern)
    attr_reader_ = other.attr_reader_;
    owns_reader_ = false;  // Don't own copy
}

AttributeReaderWrapper& AttributeReaderWrapper::operator=(const AttributeReaderWrapper& other) {
    if (this != &other) {
        if (owns_reader_ && attr_reader_) {
            attribute_reader_free(attr_reader_, pool_);
        }
        
        pool_ = other.pool_;
        attr_reader_ = other.attr_reader_;
        owns_reader_ = false;  // Don't own copy
    }
    return *this;
}

AttributeReaderWrapper::AttributeReaderWrapper(AttributeReaderWrapper&& other) noexcept
    : attr_reader_(other.attr_reader_), pool_(other.pool_), owns_reader_(other.owns_reader_) {
    other.attr_reader_ = nullptr;
    other.owns_reader_ = false;
}

AttributeReaderWrapper& AttributeReaderWrapper::operator=(AttributeReaderWrapper&& other) noexcept {
    if (this != &other) {
        if (owns_reader_ && attr_reader_) {
            attribute_reader_free(attr_reader_, pool_);
        }
        
        attr_reader_ = other.attr_reader_;
        pool_ = other.pool_;
        owns_reader_ = other.owns_reader_;
        
        other.attr_reader_ = nullptr;
        other.owns_reader_ = false;
    }
    return *this;
}

bool AttributeReaderWrapper::has(const char* key) const {
    return attr_reader_ ? attribute_reader_has(attr_reader_, key) : false;
}

const char* AttributeReaderWrapper::getString(const char* key) const {
    return attr_reader_ ? attribute_reader_get_cstring(attr_reader_, key) : nullptr;
}

ItemReader AttributeReaderWrapper::getItem(const char* key) const {
    if (!attr_reader_) {
        return ItemReader();
    }
    
    TypedItem typed = attribute_reader_get_typed(attr_reader_, key);
    
    // Convert TypedItem to Item
    Item value;
    if (typed.type_id == LMD_TYPE_INT) {
        value.int_val = typed.int_val;
        value.type_id = LMD_TYPE_INT;
    } else if (typed.type_id == LMD_TYPE_BOOL) {
        value.bool_val = typed.bool_val;
        value.type_id = LMD_TYPE_BOOL;
    } else {
        // Pointer type
        value.raw_pointer = typed.pointer;
        value.type_id = typed.type_id;
    }
    
    return ItemReader(value, pool_);
}

const char* AttributeReaderWrapper::getStringOr(const char* key, const char* default_value) const {
    const char* val = getString(key);
    return val ? val : default_value;
}

int64_t AttributeReaderWrapper::getIntOr(const char* key, int64_t default_value) const {
    if (!attr_reader_) {
        return default_value;
    }
    
    TypedItem typed = attribute_reader_get_typed(attr_reader_, key);
    if (typed.type_id == LMD_TYPE_INT) {
        return typed.int_val;
    } else if (typed.type_id == LMD_TYPE_INT64) {
        return typed.long_val;
    }
    
    return default_value;
}

AttributeReaderWrapper::Iterator AttributeReaderWrapper::iterator() const {
    return Iterator(this);
}

// AttributeReaderWrapper::Iterator implementation
AttributeReaderWrapper::Iterator::Iterator(const AttributeReaderWrapper* reader)
    : reader_(reader), current_field_(nullptr) {
    if (reader_->attr_reader_) {
        current_field_ = const_cast<ShapeEntry*>(reader_->attr_reader_->shape);
    }
}

bool AttributeReaderWrapper::Iterator::next(const char** key, ItemReader* value) {
    if (!current_field_) {
        return false;
    }
    
    *key = current_field_->name->str;
    *value = reader_->getItem(*key);
    
    current_field_ = current_field_->next;
    return true;
}

void AttributeReaderWrapper::Iterator::reset() {
    if (reader_->attr_reader_) {
        current_field_ = const_cast<ShapeEntry*>(reader_->attr_reader_->shape);
    }
}
