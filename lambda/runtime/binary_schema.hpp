#pragma once

#include "../lambda-data.hpp"

// Physical layouts are independent of the Velmt carrier (D7.4.5v2).
enum BinaryFieldKind : uint8_t {
    BINARY_FIELD_BOOL, BINARY_FIELD_I8, BINARY_FIELD_U8,
    BINARY_FIELD_I16, BINARY_FIELD_U16, BINARY_FIELD_I32, BINARY_FIELD_U32,
    BINARY_FIELD_I64, BINARY_FIELD_U64, BINARY_FIELD_F32, BINARY_FIELD_F64,
    BINARY_FIELD_RECORD, BINARY_FIELD_REFERENCE, BINARY_FIELD_ITEM,
};

enum BinarySchemaEndian : uint8_t {
    BINARY_SCHEMA_NATIVE, BINARY_SCHEMA_LITTLE, BINARY_SCHEMA_BIG, BINARY_SCHEMA_INHERIT,
};

struct BinarySchema;
struct BinarySchemaOwner;

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
};

struct BinarySchema {
    const char* tag;
    size_t byte_size;
    const BinarySchemaField* fields;
    size_t field_count;
    BinarySchemaEndian endian;
    const char* binding = nullptr;
    const BinarySchema* identity = nullptr;
};

// The shared declaration prefix retains module-local type indexing.
struct TypeBinarySchema : TypeObject {
    BinarySchema layout;
};

const BinarySchema* binary_schema_from_type(Type* type);
bool binary_schema_matches(Item value, const BinarySchema* schema);
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

template<class Pointer> struct BinarySchemaPointer {
    static const void* read(const void* slot) {
        return ((const Pointer*)slot)->get();
    }
};

template<class Target> struct BinarySchemaPointer<Target*> {
    static const void* read(const void* slot) {
        Target* pointer;
        memcpy(&pointer, slot, sizeof(pointer));
        return pointer;
    }
};

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

