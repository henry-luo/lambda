#ifndef TYPEMETA_H
#define TYPEMETA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * Lambda Type Metadata System (RTTI for C/C++)
 *
 * Provides runtime type information for C structures, enabling:
 * - Runtime type checking of allocations
 * - Memory walking and inspection
 * - Automatic structure validation
 * - Debug visualization
 *
 * Usage:
 *   // Register types at startup (or use auto-generated)
 *   typemeta_init();
 *   typemeta_register_all();  // from generated/typemeta_defs.c
 *
 *   // Allocate with type info
 *   List* list = LMEM_NEW(List, LMEM_CAT_CONTAINER);
 *
 *   // Inspect at runtime
 *   const TypeMeta* type = lmem_get_type(list);
 *   lmem_dump(list, stdout);
 */

// ============================================================================
// Type Kinds
// ============================================================================

typedef enum TypeMetaKind {
    TYPE_KIND_VOID = 0,
    TYPE_KIND_BOOL,
    TYPE_KIND_CHAR,
    TYPE_KIND_INT8,
    TYPE_KIND_INT16,
    TYPE_KIND_INT32,
    TYPE_KIND_INT64,
    TYPE_KIND_UINT8,
    TYPE_KIND_UINT16,
    TYPE_KIND_UINT32,
    TYPE_KIND_UINT64,
    TYPE_KIND_FLOAT,
    TYPE_KIND_DOUBLE,
    TYPE_KIND_POINTER,      // Pointer to another type
    TYPE_KIND_ARRAY,        // Fixed-size array
    TYPE_KIND_FLEX_ARRAY,   // Flexible array member (C99)
    TYPE_KIND_STRUCT,       // Struct with fields
    TYPE_KIND_UNION,        // Union with variants
    TYPE_KIND_ENUM,         // Enumeration
    TYPE_KIND_FUNCTION,     // Function pointer
    TYPE_KIND_OPAQUE,       // Opaque type (size known, structure unknown)
} TypeMetaKind;

// ============================================================================
// Field Metadata
// ============================================================================

// Forward declaration
typedef struct TypeMeta TypeMeta;

// Field flags
#define FIELD_FLAG_CONST        0x0001  // const qualifier
#define FIELD_FLAG_VOLATILE     0x0002  // volatile qualifier
#define FIELD_FLAG_BITFIELD     0x0004  // Is a bitfield
#define FIELD_FLAG_POINTER      0x0008  // Is a pointer (for walking)
#define FIELD_FLAG_OWNED        0x0010  // Pointer owns the memory (should be freed)
#define FIELD_FLAG_NULLABLE     0x0020  // Pointer can be NULL
#define FIELD_FLAG_ARRAY        0x0040  // Is an array (inline or pointer)
#define FIELD_FLAG_FLEX         0x0080  // Flexible array member
#define FIELD_FLAG_PRIVATE      0x0100  // Private field (skip in dumps)
#define FIELD_FLAG_DEPRECATED   0x0200  // Deprecated field

/**
 * Field descriptor for structs/unions
 */
typedef struct FieldMeta {
    const char* name;           // Field name
    const TypeMeta* type;       // Field type
    size_t offset;              // Byte offset from struct start
    size_t bit_offset;          // Bit offset (for bitfields, 0 otherwise)
    size_t bit_width;           // Bit width (for bitfields, 0 otherwise)
    uint32_t flags;             // FIELD_FLAG_* flags

    // For array fields
    size_t array_count;         // Element count (0 for flex arrays)
    const char* count_field;    // Name of field holding count (for dynamic arrays)

    // For validation
    int64_t min_value;          // Minimum valid value (for integers)
    int64_t max_value;          // Maximum valid value (for integers)
    const char* description;    // Human-readable description
} FieldMeta;

// ============================================================================
// Enum Metadata
// ============================================================================

/**
 * Enum value descriptor
 */
typedef struct EnumValueMeta {
    const char* name;           // Value name
    int64_t value;              // Numeric value
} EnumValueMeta;

// ============================================================================
// Type Metadata
// ============================================================================

// Type flags
#define TYPE_FLAG_PACKED        0x0001  // __attribute__((packed))
#define TYPE_FLAG_ALIGNED       0x0002  // Has explicit alignment
#define TYPE_FLAG_ANONYMOUS     0x0004  // Anonymous struct/union
#define TYPE_FLAG_CONTAINER     0x0008  // Is a container (has items to walk)
#define TYPE_FLAG_REFCOUNTED    0x0010  // Has reference counting
#define TYPE_FLAG_POOLED        0x0020  // Allocated from pool
#define TYPE_FLAG_REGISTERED    0x0040  // Has been registered in type registry
#define TYPE_FLAG_GENERATED     0x0080  // Auto-generated (not manually defined)

/**
 * Main type metadata structure
 */
struct TypeMeta {
    const char* name;           // Type name (e.g., "List", "DomElement")
    TypeMetaKind kind;          // Type kind
    size_t size;                // sizeof(type)
    size_t alignment;           // alignof(type)
    uint32_t type_id;           // Unique type ID (hash or assigned)
    uint32_t flags;             // TYPE_FLAG_* flags

    union {
        // For TYPE_KIND_POINTER
        struct {
            const TypeMeta* target_type;
            bool is_const;      // Points to const data
        } pointer;

        // For TYPE_KIND_ARRAY / TYPE_KIND_FLEX_ARRAY
        struct {
            const TypeMeta* element_type;
            size_t count;       // Element count (0 for flex array)
        } array;

        // For TYPE_KIND_STRUCT / TYPE_KIND_UNION
        struct {
            const FieldMeta* fields;
            size_t field_count;
            const TypeMeta* base_type;  // For inheritance-like patterns
        } composite;

        // For TYPE_KIND_ENUM
        struct {
            const EnumValueMeta* values;
            size_t value_count;
            const TypeMeta* underlying_type;
        } enum_info;

        // For TYPE_KIND_FUNCTION
        struct {
            const TypeMeta* return_type;
            const TypeMeta** param_types;
            size_t param_count;
            bool is_variadic;
        } function;
    };

    // Optional callbacks for custom behavior
    void (*custom_dump)(void* obj, FILE* out, int depth);
    bool (*custom_validate)(void* obj, char* error_buf, size_t error_buf_size);
    void (*custom_walk)(void* obj, void (*visit)(void* ptr, const TypeMeta* type, void* ctx), void* ctx);
};

// ============================================================================
// Primitive Type Declarations
// ============================================================================

extern const TypeMeta TYPEMETA_void;
extern const TypeMeta TYPEMETA_bool;
extern const TypeMeta TYPEMETA_char;
extern const TypeMeta TYPEMETA_int8;
extern const TypeMeta TYPEMETA_int16;
extern const TypeMeta TYPEMETA_int32;
extern const TypeMeta TYPEMETA_int64;
extern const TypeMeta TYPEMETA_uint8;
extern const TypeMeta TYPEMETA_uint16;
extern const TypeMeta TYPEMETA_uint32;
extern const TypeMeta TYPEMETA_uint64;
extern const TypeMeta TYPEMETA_float;
extern const TypeMeta TYPEMETA_double;
extern const TypeMeta TYPEMETA_size_t;
extern const TypeMeta TYPEMETA_intptr;
extern const TypeMeta TYPEMETA_uintptr;
extern const TypeMeta TYPEMETA_cstring;  // const char*

// ============================================================================
// Registration Macros
// ============================================================================

/**
 * Begin struct definition
 */
#define TYPEMETA_STRUCT_BEGIN(type) \
    static const FieldMeta _typemeta_fields_##type[] = {

/**
 * Define a simple field
 */
#define TYPEMETA_FIELD(struct_type, field, field_typemeta) \
    { \
        .name = #field, \
        .type = (field_typemeta), \
        .offset = offsetof(struct_type, field), \
        .bit_offset = 0, \
        .bit_width = 0, \
        .flags = 0, \
        .array_count = 0, \
        .count_field = NULL, \
        .min_value = 0, \
        .max_value = 0, \
        .description = NULL, \
    },

/**
 * Define a pointer field
 */
#define TYPEMETA_FIELD_PTR(struct_type, field, target_typemeta, is_owned) \
    { \
        .name = #field, \
        .type = (target_typemeta), \
        .offset = offsetof(struct_type, field), \
        .flags = FIELD_FLAG_POINTER | ((is_owned) ? FIELD_FLAG_OWNED : 0) | FIELD_FLAG_NULLABLE, \
    },

/**
 * Define an array field with count from another field
 */
#define TYPEMETA_FIELD_ARRAY(struct_type, field, elem_typemeta, count_fld) \
    { \
        .name = #field, \
        .type = (elem_typemeta), \
        .offset = offsetof(struct_type, field), \
        .flags = FIELD_FLAG_POINTER | FIELD_FLAG_ARRAY | FIELD_FLAG_OWNED, \
        .count_field = #count_fld, \
    },

/**
 * Define a bitfield
 */
#define TYPEMETA_FIELD_BITS(struct_type, field, field_typemeta, width) \
    { \
        .name = #field, \
        .type = (field_typemeta), \
        .offset = offsetof(struct_type, field), \
        .bit_width = (width), \
        .flags = FIELD_FLAG_BITFIELD, \
    },

/**
 * Define a field with value range
 */
#define TYPEMETA_FIELD_RANGE(struct_type, field, field_typemeta, minval, maxval) \
    { \
        .name = #field, \
        .type = (field_typemeta), \
        .offset = offsetof(struct_type, field), \
        .min_value = (minval), \
        .max_value = (maxval), \
    },

/**
 * End struct definition and declare the TypeMeta
 */
#define TYPEMETA_STRUCT_END(type, type_flags) \
    }; \
    const TypeMeta TYPEMETA_##type = { \
        .name = #type, \
        .kind = TYPE_KIND_STRUCT, \
        .size = sizeof(type), \
        .alignment = _Alignof(type), \
        .type_id = 0, \
        .flags = (type_flags), \
        .composite = { \
            .fields = _typemeta_fields_##type, \
            .field_count = sizeof(_typemeta_fields_##type) / sizeof(FieldMeta), \
            .base_type = NULL, \
        }, \
    };

/**
 * End struct definition with base type
 */
#define TYPEMETA_STRUCT_END_EXTENDS(type, base_typemeta, type_flags) \
    }; \
    const TypeMeta TYPEMETA_##type = { \
        .name = #type, \
        .kind = TYPE_KIND_STRUCT, \
        .size = sizeof(type), \
        .alignment = _Alignof(type), \
        .type_id = 0, \
        .flags = (type_flags), \
        .composite = { \
            .fields = _typemeta_fields_##type, \
            .field_count = sizeof(_typemeta_fields_##type) / sizeof(FieldMeta), \
            .base_type = (base_typemeta), \
        }, \
    };

/**
 * Declare external TypeMeta (for headers)
 */
#define TYPEMETA_EXTERN(type) \
    extern const TypeMeta TYPEMETA_##type

/**
 * Define a pointer type
 */
#define TYPEMETA_POINTER(name, target_typemeta) \
    const TypeMeta TYPEMETA_ptr_##name = { \
        .name = #name "*", \
        .kind = TYPE_KIND_POINTER, \
        .size = sizeof(void*), \
        .alignment = _Alignof(void*), \
        .pointer = { .target_type = (target_typemeta) }, \
    }

/**
 * Define an enum type
 */
#define TYPEMETA_ENUM_BEGIN(type) \
    static const EnumValueMeta _typemeta_values_##type[] = {

#define TYPEMETA_ENUM_VALUE(name, val) \
    { #name, (val) },

#define TYPEMETA_ENUM_END(type, underlying) \
    }; \
    const TypeMeta TYPEMETA_##type = { \
        .name = #type, \
        .kind = TYPE_KIND_ENUM, \
        .size = sizeof(type), \
        .alignment = _Alignof(type), \
        .enum_info = { \
            .values = _typemeta_values_##type, \
            .value_count = sizeof(_typemeta_values_##type) / sizeof(EnumValueMeta), \
            .underlying_type = (underlying), \
        }, \
    };

// ============================================================================
// Type Registry API
// ============================================================================

/**
 * Initialize the type metadata system
 */
bool typemeta_init(void);

/**
 * Shutdown the type metadata system
 */
void typemeta_shutdown(void);

/**
 * Register a type in the global registry
 * @param type TypeMeta to register
 * @return true on success
 */
bool typemeta_register(const TypeMeta* type);

/**
 * Look up a type by name
 * @param name Type name
 * @return TypeMeta pointer or NULL if not found
 */
const TypeMeta* typemeta_lookup(const char* name);

/**
 * Look up a type by ID
 * @param type_id Type ID
 * @return TypeMeta pointer or NULL if not found
 */
const TypeMeta* typemeta_lookup_by_id(uint32_t type_id);

/**
 * Compute type ID from name (hash)
 */
uint32_t typemeta_compute_id(const char* name);

/**
 * Get all registered types
 * @param out_count Output: number of types
 * @return Array of TypeMeta pointers (do not free)
 */
const TypeMeta** typemeta_get_all(size_t* out_count);

// ============================================================================
// Type Introspection API
// ============================================================================

/**
 * Get field by name
 */
const FieldMeta* typemeta_get_field(const TypeMeta* type, const char* name);

/**
 * Get field by index
 */
const FieldMeta* typemeta_get_field_at(const TypeMeta* type, size_t index);

/**
 * Get pointer to field value
 */
void* typemeta_field_ptr(void* obj, const FieldMeta* field);

/**
 * Get pointer to field by name
 */
void* typemeta_field_ptr_by_name(void* obj, const TypeMeta* type, const char* field_name);

/**
 * Check if type is a specific kind
 */
bool typemeta_is_kind(const TypeMeta* type, TypeMetaKind kind);

/**
 * Check if type is a primitive (numeric, bool, char)
 */
bool typemeta_is_primitive(const TypeMeta* type);

/**
 * Check if type is a composite (struct/union)
 */
bool typemeta_is_composite(const TypeMeta* type);

/**
 * Check if type extends another (has base_type matching)
 */
bool typemeta_is_subtype(const TypeMeta* type, const TypeMeta* base);

/**
 * Get the total field count including inherited fields
 */
size_t typemeta_total_field_count(const TypeMeta* type);

// ============================================================================
// Value Formatting API
// ============================================================================

/**
 * Format a value as a string
 * @param ptr Pointer to value
 * @param type Type of value
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of characters written (excluding null terminator)
 */
int typemeta_format_value(void* ptr, const TypeMeta* type, char* buf, size_t buf_size);

/**
 * Format a field value as a string
 */
int typemeta_format_field(void* obj, const FieldMeta* field, char* buf, size_t buf_size);

/**
 * Get enum value name
 * @param type Enum TypeMeta
 * @param value Numeric value
 * @return Name string or NULL if not found
 */
const char* typemeta_enum_name(const TypeMeta* type, int64_t value);

/**
 * Get enum numeric value by name
 * @param type Enum TypeMeta
 * @param name Value name
 * @param out_value Output: numeric value
 * @return true if found
 */
bool typemeta_enum_value(const TypeMeta* type, const char* name, int64_t* out_value);

// ============================================================================
// Validation API
// ============================================================================

/**
 * Validation result
 */
typedef struct TypeMetaValidation {
    bool valid;
    const char* field_name;     // Field that failed (NULL for type-level error)
    char message[256];          // Error message
} TypeMetaValidation;

/**
 * Validate an object against its type
 * @param obj Object to validate
 * @param type Type metadata
 * @param result Output: validation result
 * @return true if valid
 */
bool typemeta_validate(void* obj, const TypeMeta* type, TypeMetaValidation* result);

/**
 * Validate all objects of a type in the memory tracker
 * @param type Type to validate
 * @return Number of invalid objects found
 */
size_t typemeta_validate_all(const TypeMeta* type);

// ============================================================================
// Memory Walking API
// ============================================================================

/**
 * Callback for walking memory
 * @param ptr Pointer to current object
 * @param type Type of current object
 * @param field Field info (NULL for root object)
 * @param depth Nesting depth (0 for root)
 * @param user_data User context
 * @return true to continue walking, false to stop
 */
typedef bool (*TypeMetaWalkCallback)(
    void* ptr,
    const TypeMeta* type,
    const FieldMeta* field,
    int depth,
    void* user_data
);

/**
 * Walk an object and all reachable objects via pointer fields
 * @param ptr Root object pointer
 * @param type Root object type
 * @param callback Callback for each object
 * @param user_data User context
 * @param max_depth Maximum depth to walk (-1 for unlimited)
 */
void typemeta_walk(void* ptr, const TypeMeta* type,
                   TypeMetaWalkCallback callback, void* user_data, int max_depth);

/**
 * Walk all objects of a specific type (requires memtrack integration)
 */
void typemeta_walk_all(const TypeMeta* type, TypeMetaWalkCallback callback, void* user_data);

// ============================================================================
// Dump/Visualization API
// ============================================================================

/**
 * Dump flags
 */
#define DUMP_FLAG_COMPACT       0x01    // Single line per object
#define DUMP_FLAG_NO_POINTERS   0x02    // Don't follow pointers
#define DUMP_FLAG_HEX_INTS      0x04    // Show integers in hex
#define DUMP_FLAG_SHOW_OFFSETS  0x08    // Show field offsets
#define DUMP_FLAG_SHOW_TYPES    0x10    // Show field types
#define DUMP_FLAG_PRIVATE       0x20    // Include private fields

/**
 * Dump an object to file
 * @param ptr Object pointer
 * @param type Object type
 * @param out Output file
 * @param flags DUMP_FLAG_* flags
 * @param max_depth Maximum depth (-1 for unlimited)
 */
void typemeta_dump(void* ptr, const TypeMeta* type, FILE* out, uint32_t flags, int max_depth);

/**
 * Dump to string buffer
 */
int typemeta_dump_to_string(void* ptr, const TypeMeta* type, char* buf, size_t buf_size,
                            uint32_t flags, int max_depth);

/**
 * Export object graph in DOT format (for Graphviz)
 */
void typemeta_export_dot(void* ptr, const TypeMeta* type, FILE* out, int max_depth);

/**
 * Export object as JSON
 */
void typemeta_export_json(void* ptr, const TypeMeta* type, FILE* out, int max_depth);

// ============================================================================
// Comparison API
// ============================================================================

/**
 * Compare two objects of the same type
 * @return 0 if equal, non-zero if different
 */
int typemeta_compare(void* a, void* b, const TypeMeta* type);

/**
 * Deep copy an object
 * @param src Source object
 * @param type Object type
 * @param allocator Function to allocate memory (or NULL for malloc)
 * @return Newly allocated copy
 */
void* typemeta_deep_copy(void* src, const TypeMeta* type, void* (*allocator)(size_t));

#ifdef __cplusplus
}
#endif

#endif // TYPEMETA_H
