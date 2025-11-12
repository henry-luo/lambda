
#include "lambda.h"

typedef struct ConstItem ConstItem;

typedef struct Item {
    union {
        // packed values with type_id tagging
        struct {
            int int_val: 32;
            uint32_t _24: 24;
            uint32_t _type: 8;
        };
        struct {
            uint64_t bool_val: 8;
            uint64_t _56: 56;
        };
        // uses the high byte to tag the item/pointer, defined for little-endian
        struct {
            uint64_t pointer : 56;  // tagged pointer for long, double, string, symbol, dtime, binary
            uint64_t _type_id : 8;
        };
        // raw 64-bit value
        uint64_t item;

        // direct pointers to the container types
        Container* container;
        Range* range;
        List* list;
        Array* array;
        ArrayInt* array_int;      // Renamed from array_long
        ArrayInt64* array_int64;  // New: 64-bit integer arrays
        ArrayFloat* array_float;
        Map* map;
        Element* element;
        Type* type;
        Function* function;
    };

    inline TypeId type_id() {
        if (this->_type_id) {
            return this->_type_id;
        }
        if (this->item) {
            return *((TypeId*)this->item);
        }
        return LMD_TYPE_NULL; // fallback for null items
    }

    inline ConstItem to_const() const;
} Item;

// const read-only item
// ConstItem, instead of const Item, to hide fields from Item
struct ConstItem {
    union {
        // raw 64-bit value
        const uint64_t item;

        // direct pointers to the container types
        const Container* container;
        const Range* range;
        const List* list;
        const Array* array;
        const ArrayInt* array_int;      // Renamed from array_long
        const ArrayInt64* array_int64;  // New: 64-bit integer arrays
        const ArrayFloat* array_float;
        const Map* map;
        const Element* element;
        const Type* type;
        const Function* function;
    };

    explicit ConstItem(): item(0) {}
    // ConstItem& operator=(ConstItem&&) = default;
    ConstItem& operator=(const ConstItem &) = default;

    inline TypeId type_id() const {
        return ((Item*)this)->type_id();
    }

    inline String* string() const {
        Item* itm = (Item*)this;
        return (itm->_type_id == LMD_TYPE_STRING) ? (String*)itm->pointer : nullptr;
    }

    // inline Item item() { return *(Item*)this; }
};

// define Item::to_const() after ConstItem is complete
inline ConstItem Item::to_const() const {
    return *(ConstItem*)this;
}

// get type_id from an Item
static inline TypeId get_type_id(Item value) { return value.type_id(); }

extern Item ItemNull;
extern Item ItemError;

struct Range : Container {
    int64_t start;  // inclusive start
    int64_t end;    // inclusive end
    int64_t length;
};

struct List : Container {
    Item* items;
    int64_t length;
    int64_t extra;  // count of extra items stored at the end of the list
    int64_t capacity;

    ConstItem get(int index) const;
};

struct ArrayInt : Container {
    int* items;  // 32-bit integer items
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
};

struct ArrayInt64 : Container {
    int64_t* items;  // 64-bit integer items
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
};

struct ArrayFloat : Container {
    double* items;
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
};

struct Map : Container {
    void* type;  // map type/shape
    void* data;  // packed data struct of the map
    int data_cap;  // capacity of the data struct

    ConstItem get(const Item key) const;

    inline ConstItem get(const char* key_str) const {
        return get((Item){.item = s2it(key_str)});
    }
};

struct Element : List {
    // attributes map
    void* type;  // attr type/shape
    void* data;  // packed data struct of the attrs
    int data_cap;  // capacity of the data struct
    // member functions
    bool has_attr(const char* attr_name);

    ConstItem get_attr(const Item attr_name) const;

    ConstItem get_attr(const char* attr_name) const {
        return get_attr((Item){.item = s2it(attr_name)});
    }
};
