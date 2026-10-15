#pragma once

#include "../lambda-data.hpp"

// Physical layouts are independent of the Velmt carrier (D7.4.5v2).
enum BinaryFieldKind : uint8_t {
    BINARY_FIELD_BOOL, BINARY_FIELD_I8, BINARY_FIELD_U8,
    BINARY_FIELD_I16, BINARY_FIELD_U16, BINARY_FIELD_I32, BINARY_FIELD_U32,
    BINARY_FIELD_I64, BINARY_FIELD_U64, BINARY_FIELD_F32, BINARY_FIELD_F64,
    BINARY_FIELD_RECORD, BINARY_FIELD_REFERENCE, BINARY_FIELD_ITEM,
    BINARY_FIELD_SPAN, BINARY_FIELD_BYTES,
};

enum BinarySchemaEndian : uint8_t {
    BINARY_SCHEMA_NATIVE, BINARY_SCHEMA_LITTLE, BINARY_SCHEMA_BIG, BINARY_SCHEMA_INHERIT,
};

struct BinarySchema;
struct BinarySchemaOwner;
inline constexpr uint32_t BINARY_SCHEMA_BINDING_ABI = 1;

struct BinarySchemaField {
    const char* name;
    BinaryFieldKind kind;
    size_t offset;
    size_t storage_size;
    const BinarySchema* target;
    bool nullable;
    // Compiled locators handle inherited members without invalid offsetof.
    const void* (*locate)(const void* record);
    const void* (*dereference)(const void* slot);
    Type* logical_type = nullptr;
    BinaryFieldKind element_kind = BINARY_FIELD_U8;
    const char* count_field = nullptr;
    uint64_t fixed_count = 0;
    bool has_fixed_count = false;
    size_t stride = 0;
    const BinarySchemaField* count_binding = nullptr;
    // the host admits an allocation extent independently of the script's count.
    bool (*extent)(const void* record, uint64_t* capacity) = nullptr;
};

struct BinarySchema {
    const char* tag;
    size_t byte_size;
    const BinarySchemaField* fields;
    size_t field_count;
    BinarySchemaEndian endian;
    const char* binding = nullptr;
    const BinarySchema* identity = nullptr;
    uint32_t binding_abi = BINARY_SCHEMA_BINDING_ABI;
    const char* binding_version = nullptr;
};

// The shared declaration prefix retains module-local type indexing.
struct TypeBinarySchema : TypeObject {
    BinarySchema layout;
};

const BinarySchema* binary_schema_from_type(Type* type);
bool binary_schema_matches(Item value, const BinarySchema* schema);
bool binary_schema_parse_count(StrView brackets, uint64_t* fixed, StrView* field);
extern "C" Item fn_binary_decode(Item binary, Item schema);

// A wrapper retains the control object, never ownership of an arbitrary pointee.
BinarySchemaOwner* binary_schema_owner_create();
void binary_schema_owner_invalidate(BinarySchemaOwner* owner);
void binary_schema_owner_release(BinarySchemaOwner* owner);
const BinarySchema* binary_schema_bind_native(BinarySchemaOwner* owner,
    const BinarySchema* declared, const BinarySchema* native, Item* error);
Item binary_schema_native_view(const BinarySchema* schema, const void* record,
                              BinarySchemaOwner* owner);
Item binary_schema_decode(const BinarySchema* schema, Item binary);
// Capability-checked native helper access; no address crosses into script.
const void* binary_schema_native_record(Item view, const char* binding);

template<class Record, auto Member>
const void* binary_schema_member_location(const void* record) {
    return &(((const Record*)record)->*Member);
}

template<class T> struct BinarySchemaRemoveRef { using Type = T; };
template<class T> struct BinarySchemaRemoveRef<T&> { using Type = T; };
template<class A, class B> struct BinarySchemaSameType { static constexpr bool value = false; };
template<class T> struct BinarySchemaSameType<T, T> { static constexpr bool value = true; };
template<class T> struct BinarySchemaSameType<const T, T> { static constexpr bool value = true; };
template<class Pointer> struct BinarySchemaPointer {
    using Target = typename BinarySchemaRemoveRef<decltype(*((const Pointer*)nullptr)->get())>::Type;
    static const void* read(const void* slot) {
        return ((const Pointer*)slot)->get();
    }
};

template<class T> struct BinarySchemaPointer<T*> {
    using Target = T;
    static const void* read(const void* slot) {
        T* pointer;
        memcpy(&pointer, slot, sizeof(pointer));
        return pointer;
    }
};

template<class T> struct BinarySchemaScalarKind;
template<class T> struct BinarySchemaScalarKind<const T> : BinarySchemaScalarKind<T> {};
// native size_t and fixed-width typedefs may name different fundamental types.
template<class T> struct BinarySchemaIntegerKind {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "native binary integer has unsupported width");
    static constexpr bool signed_value = T(-1) < T(0);
    static constexpr BinaryFieldKind kind = sizeof(T) == 1 ? (signed_value ? BINARY_FIELD_I8 : BINARY_FIELD_U8) :
        sizeof(T) == 2 ? (signed_value ? BINARY_FIELD_I16 : BINARY_FIELD_U16) :
        sizeof(T) == 4 ? (signed_value ? BINARY_FIELD_I32 : BINARY_FIELD_U32) :
        (signed_value ? BINARY_FIELD_I64 : BINARY_FIELD_U64);
};
#define BINARY_SCHEMA_INTEGER_KIND(T) \
    template<> struct BinarySchemaScalarKind<T> : BinarySchemaIntegerKind<T> {}
BINARY_SCHEMA_INTEGER_KIND(char);
BINARY_SCHEMA_INTEGER_KIND(signed char);
BINARY_SCHEMA_INTEGER_KIND(unsigned char);
BINARY_SCHEMA_INTEGER_KIND(short);
BINARY_SCHEMA_INTEGER_KIND(unsigned short);
BINARY_SCHEMA_INTEGER_KIND(int);
BINARY_SCHEMA_INTEGER_KIND(unsigned int);
BINARY_SCHEMA_INTEGER_KIND(long);
BINARY_SCHEMA_INTEGER_KIND(unsigned long);
BINARY_SCHEMA_INTEGER_KIND(long long);
BINARY_SCHEMA_INTEGER_KIND(unsigned long long);
#undef BINARY_SCHEMA_INTEGER_KIND
#define BINARY_SCHEMA_SCALAR_KIND(T, Kind) \
    template<> struct BinarySchemaScalarKind<T> { static constexpr BinaryFieldKind kind = Kind; }
BINARY_SCHEMA_SCALAR_KIND(bool, BINARY_FIELD_BOOL);
BINARY_SCHEMA_SCALAR_KIND(float, BINARY_FIELD_F32);
BINARY_SCHEMA_SCALAR_KIND(double, BINARY_FIELD_F64);
#undef BINARY_SCHEMA_SCALAR_KIND

template<class Record, auto Capacity>
bool binary_schema_span_capacity(const void* record, uint64_t* out) {
    auto value = ((const Record*)record)->*Capacity;
    using Count = decltype(value);
    constexpr BinaryFieldKind kind = BinarySchemaScalarKind<Count>::kind;
    static_assert(kind >= BINARY_FIELD_I8 && kind <= BINARY_FIELD_U64,
        "native span capacity must be an integer field");
    if constexpr (Count(-1) < Count(0)) { if (value < 0) return false; }
    *out = (uint64_t)value;
    return true;
}

template<class Record, auto Member, auto Capacity>
BinarySchemaField binary_schema_span_binding(const char* name, const char* count,
        BinaryFieldKind kind, BinaryFieldKind element_kind, const BinarySchema* target,
        bool nullable, Type* logical = nullptr) {
    using Pointer = decltype(((Record*)nullptr)->*Member);
    using Slot = typename BinarySchemaRemoveRef<Pointer>::Type;
    BinarySchemaField field = {name, kind, 0, sizeof(Slot), target, nullable,
        binary_schema_member_location<Record, Member>, BinarySchemaPointer<Slot>::read, logical};
    field.element_kind = element_kind;
    field.count_field = count;
    field.stride = sizeof(typename BinarySchemaPointer<Slot>::Target);
    field.extent = binary_schema_span_capacity<Record, Capacity>;
    return field;
}

// These bindings derive sizes and member access from the actual host compiler.
#define BINARY_SCHEMA_MEMBER(Record, member, kind) \
    {#member, kind, 0, sizeof(((Record*)nullptr)->member), nullptr, false, \
     binary_schema_member_location<Record, &Record::member>, nullptr}
#define BINARY_SCHEMA_REFERENCE(Record, member, schema, nullable) \
    {#member, BINARY_FIELD_REFERENCE, 0, sizeof(((Record*)nullptr)->member), \
     schema, nullable, binary_schema_member_location<Record, &Record::member>, \
     BinarySchemaPointer<decltype(((Record*)nullptr)->member)>::read}
#define BINARY_SCHEMA_RECORD(Record, member, schema) \
    {#member, BINARY_FIELD_RECORD, 0, sizeof(((Record*)nullptr)->member), schema, \
     false, binary_schema_member_location<Record, &Record::member>, nullptr}
#define BINARY_SCHEMA_ITEM(Record, member, logical, nullable) \
    {#member, BINARY_FIELD_ITEM, 0, sizeof(((Record*)nullptr)->member), nullptr, \
     nullable, binary_schema_member_location<Record, &Record::member>, nullptr, logical}
#define BINARY_SCHEMA_SPAN(Record, member, count, capacity, kind, target, nullable) \
    binary_schema_span_binding<Record, &Record::member, &Record::capacity>( \
        #member, #count, BINARY_FIELD_SPAN, kind, target, nullable)
#define BINARY_SCHEMA_BYTES(Record, member, count, capacity, nullable) \
    binary_schema_span_binding<Record, &Record::member, &Record::capacity>( \
        #member, #count, BINARY_FIELD_BYTES, BINARY_FIELD_U8, nullptr, nullable)

