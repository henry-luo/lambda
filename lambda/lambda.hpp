
#include "lambda.h"

typedef union Item {
    struct {
        union {
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
                uint64_t type_id : 8;
            };
        };
    };
    uint64_t item;
    void* raw_pointer;

    // pointers to the container types
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
} Item;

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
