/**
 * @file schema_builder.cpp
 * @brief Unified Schema Type Creation Functions
 * @author GitHub Copilot
 * @license MIT
 *
 * Implementation of unified type creation functions that bridge schema validation
 * and runtime type systems in Lambda Script.
 */

#include "schema_ast.hpp"
#include "../lib/hashmap.h"
#include <string.h>
#include <assert.h>

// ==================== TypeRegistry Hash Functions ====================

// Hash function for TypeRegistryEntry
static uint64_t type_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)item;
    const StrView* view = &entry->name_key;
    return hashmap_sip(view->str, view->length, seed0, seed1);
}

// Compare function for TypeRegistryEntry
static int type_entry_compare(const void *a, const void *b, void *udata) {
    const TypeRegistryEntry* entry_a = (const TypeRegistryEntry*)a;
    const TypeRegistryEntry* entry_b = (const TypeRegistryEntry*)b;

    const StrView* view_a = &entry_a->name_key;
    const StrView* view_b = &entry_b->name_key;

    if (view_a->length != view_b->length) {
        return (view_a->length < view_b->length) ? -1 : 1;
    }

    if (view_a->length == 0) return 0;
    return memcmp(view_a->str, view_b->str, view_a->length);
}

// ==================== Utility Functions ====================

void occurrence_to_counts(char modifier, int64_t* min_count, int64_t* max_count) {
    switch (modifier) {
        case '?':
            *min_count = 0;
            *max_count = 1;
            break;
        case '+':
            *min_count = 1;
            *max_count = -1; // unlimited
            break;
        case '*':
            *min_count = 0;
            *max_count = -1; // unlimited
            break;
        default:
            *min_count = 1;
            *max_count = 1;
            break;
    }
}

char counts_to_occurrence(int64_t min_count, int64_t max_count) {
    if (min_count == 0 && max_count == 1) return '?';
    if (min_count == 1 && max_count == -1) return '+';
    if (min_count == 0 && max_count == -1) return '*';
    return 0; // No modifier
}

bool validate_occurrence_counts(int64_t min_count, int64_t max_count) {
    if (min_count < 0) return false;
    if (max_count != -1 && max_count < min_count) return false;
    return true;
}

// ==================== Schema Type Creation Functions ====================

TypeSchema* unified_create_primitive_schema(TypeId primitive_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_PRIMITIVE;
    schema->is_open = false;

    SchemaPrimitive* primitive_data = (SchemaPrimitive*)pool_calloc(pool, sizeof(SchemaPrimitive));
    if (!primitive_data) return NULL;

    primitive_data->primitive_type = primitive_type;
    schema->schema_data = primitive_data;

    return schema;
}

TypeSchema* unified_create_union_schema(TypeSchema** types, int type_count, VariableMemPool* pool) {
    if (!types || type_count <= 0) return NULL;

    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_UNION;
    schema->is_open = false;

    SchemaUnion* union_data = (SchemaUnion*)pool_calloc(pool, sizeof(SchemaUnion));
    if (!union_data) return NULL;

    union_data->types = (TypeSchema**)pool_calloc(pool, sizeof(TypeSchema*) * type_count);
    if (!union_data->types) return NULL;

    for (int i = 0; i < type_count; i++) {
        union_data->types[i] = types[i];
    }
    union_data->type_count = type_count;

    schema->schema_data = union_data;
    return schema;
}

TypeSchema* unified_create_array_schema(TypeSchema* element_type, long min_count, long max_count, VariableMemPool* pool) {
    if (!element_type || !validate_occurrence_counts(min_count, max_count)) return NULL;

    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_ARRAY;
    schema->is_open = false;

    SchemaArray* array_data = (SchemaArray*)pool_calloc(pool, sizeof(SchemaArray));
    if (!array_data) return NULL;

    array_data->element_type = element_type;
    array_data->min_count = min_count;
    array_data->max_count = max_count;
    array_data->occurrence = counts_to_occurrence(min_count, max_count);

    schema->schema_data = array_data;
    return schema;
}

TypeSchema* unified_create_map_schema(TypeSchema* key_type, TypeSchema* value_type, VariableMemPool* pool) {
    if (!key_type || !value_type) return NULL;

    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_MAP;
    schema->is_open = true; // Maps are open by default

    SchemaMap* map_data = (SchemaMap*)pool_calloc(pool, sizeof(SchemaMap));
    if (!map_data) return NULL;

    map_data->fields = NULL;
    map_data->field_count = 0;
    map_data->is_open = true;

    schema->schema_data = map_data;
    return schema;
}

TypeSchema* unified_create_element_schema(StrView tag, SchemaMapField* attributes, TypeSchema** content_types, int content_count, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_ELEMENT;
    schema->is_open = true; // Elements are open by default

    SchemaElement* element_data = (SchemaElement*)pool_calloc(pool, sizeof(SchemaElement));
    if (!element_data) return NULL;

    element_data->tag = tag;
    element_data->attributes = attributes;
    element_data->content_types = content_types;
    element_data->content_count = content_count;
    element_data->is_open = true;

    schema->schema_data = element_data;
    return schema;
}

TypeSchema* unified_create_occurrence_schema(TypeSchema* base_type, char modifier, VariableMemPool* pool) {
    if (!base_type) return NULL;

    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_OCCURRENCE;
    schema->is_open = false;

    SchemaOccurrence* occurrence_data = (SchemaOccurrence*)pool_calloc(pool, sizeof(SchemaOccurrence));
    if (!occurrence_data) return NULL;

    occurrence_data->base_type = base_type;
    occurrence_data->modifier = modifier;
    occurrence_to_counts(modifier, &occurrence_data->min_count, &occurrence_data->max_count);

    schema->schema_data = occurrence_data;
    return schema;
}

TypeSchema* unified_create_reference_schema(StrView type_name, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_REFERENCE;
    schema->is_open = false;

    SchemaReference* reference_data = (SchemaReference*)pool_calloc(pool, sizeof(SchemaReference));
    if (!reference_data) return NULL;

    reference_data->type_name = type_name;
    reference_data->resolved_type = NULL; // Will be resolved later

    schema->schema_data = reference_data;
    return schema;
}

TypeSchema* unified_create_literal_schema(Item literal_value, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    if (!schema) return NULL;

    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_LITERAL;
    schema->is_open = false;

    SchemaLiteral* literal_data = (SchemaLiteral*)pool_calloc(pool, sizeof(SchemaLiteral));
    if (!literal_data) return NULL;

    literal_data->literal_value = literal_value;

    schema->schema_data = literal_data;
    return schema;
}

// ==================== Schema-Runtime Type Bridge Functions ====================

Type* schema_to_runtime_type(TypeSchema* schema, VariableMemPool* pool) {
    if (!schema) return NULL;

    switch (schema->schema_type) {
    case LMD_SCHEMA_PRIMITIVE: {
        SchemaPrimitive* primitive = (SchemaPrimitive*)schema->schema_data;
        Type* type = (Type*)pool_calloc(pool, sizeof(Type));
        if (type) {
            type->type_id = primitive->primitive_type;
        }
        return type;
    }
    case LMD_SCHEMA_ARRAY: {
        SchemaArray* array_schema = (SchemaArray*)schema->schema_data;
        TypeArray* array_type = (TypeArray*)pool_calloc(pool, sizeof(TypeArray));
        if (array_type) {
            array_type->type_id = LMD_TYPE_ARRAY;
            array_type->nested = schema_to_runtime_type(array_schema->element_type, pool);
            array_type->length = array_schema->max_count == -1 ? 0 : array_schema->max_count;
        }
        return (Type*)array_type;
    }
    case LMD_SCHEMA_MAP: {
        TypeMap* map_type = (TypeMap*)pool_calloc(pool, sizeof(TypeMap));
        if (map_type) {
            map_type->type_id = LMD_TYPE_MAP;
            // TODO: Convert schema map fields to runtime shape
        }
        return (Type*)map_type;
    }
    case LMD_SCHEMA_OCCURRENCE: {
        SchemaOccurrence* occurrence = (SchemaOccurrence*)schema->schema_data;
        // For occurrences, we return the base type for runtime
        return schema_to_runtime_type(occurrence->base_type, pool);
    }
    case LMD_SCHEMA_REFERENCE: {
        SchemaReference* reference = (SchemaReference*)schema->schema_data;
        if (reference->resolved_type) {
            return schema_to_runtime_type(reference->resolved_type, pool);
        }
        // Unresolved reference - return ANY type
        Type* type = (Type*)pool_calloc(pool, sizeof(Type));
        if (type) {
            type->type_id = LMD_TYPE_ANY;
        }
        return type;
    }
    default:
        // For unknown schema types, return ANY type
        Type* type = (Type*)pool_calloc(pool, sizeof(Type));
        if (type) {
            type->type_id = LMD_TYPE_ANY;
        }
        return type;
    }
}

TypeSchema* runtime_to_schema_type(Type* runtime_type, VariableMemPool* pool) {
    if (!runtime_type) return NULL;

    switch (runtime_type->type_id) {
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_STRING:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_NULL:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_DTIME:
        case LMD_TYPE_DECIMAL:
        case LMD_TYPE_BINARY:
            return unified_create_primitive_schema(runtime_type->type_id, pool);

        case LMD_TYPE_ARRAY: {
            TypeArray* array_type = (TypeArray*)runtime_type;
            TypeSchema* element_schema = runtime_to_schema_type(array_type->nested, pool);
            return unified_create_array_schema(element_schema, 0, -1, pool);
        }

        case LMD_TYPE_MAP: {
            // Create a generic map schema
            TypeSchema* key_schema = unified_create_primitive_schema(LMD_TYPE_STRING, pool);
            TypeSchema* value_schema = unified_create_primitive_schema(LMD_TYPE_ANY, pool);
            return unified_create_map_schema(key_schema, value_schema, pool);
        }

        default:
            return unified_create_primitive_schema(LMD_TYPE_ANY, pool);
    }
}

// ==================== Type Registry Implementation ====================

TypeRegistry* type_registry_create(VariableMemPool* pool) {
    TypeRegistry* registry = (TypeRegistry*)pool_calloc(pool, sizeof(TypeRegistry));
    if (!registry) return NULL;

    registry->type_map = hashmap_new(sizeof(TypeRegistryEntry), 0, 0, 0,
                                     type_entry_hash, type_entry_compare, NULL, NULL);
    registry->type_list = arraylist_new(16);
    registry->pool = pool;

    return registry;
}

void type_registry_destroy(TypeRegistry* registry) {
    if (!registry) return;

    if (registry->type_map) {
        hashmap_free(registry->type_map);
    }

    if (registry->type_list) {
        arraylist_free(registry->type_list);
    }

    // Note: memory pool cleanup handled by caller
}

bool type_registry_add(TypeRegistry* registry, StrView name, TypeSchema* schema_type, Type* runtime_type) {
    if (!registry || !schema_type) return false;

    TypeDefinition* def = (TypeDefinition*)pool_calloc(registry->pool, sizeof(TypeDefinition));
    if (!def) return false;

    def->name = name;
    def->schema_type = schema_type;
    def->runtime_type = runtime_type;
    def->is_exported = true;

    // Create a registry entry for hashmap
    TypeRegistryEntry entry;
    entry.definition = def;
    entry.name_key = name;

    // Add to hashmap
    if (hashmap_set(registry->type_map, &entry) != NULL) {
        return false; // Type already exists
    }

    // Add to ordered list
    arraylist_append(registry->type_list, def);

    return true;
}

TypeDefinition* type_registry_lookup(TypeRegistry* registry, StrView name) {
    if (!registry) return NULL;

    // Create a temporary entry for lookup
    TypeRegistryEntry lookup_entry;
    lookup_entry.name_key = name;
    lookup_entry.definition = NULL;

    TypeRegistryEntry* found = (TypeRegistryEntry*)hashmap_get(registry->type_map, &lookup_entry);
    return found ? found->definition : NULL;
}

TypeSchema* type_registry_resolve_reference(TypeRegistry* registry, StrView type_name) {
    TypeDefinition* def = type_registry_lookup(registry, type_name);
    return def ? def->schema_type : NULL;
}

// ==================== Enhanced Transpiler Functions ====================

SchemaTranspiler* schema_transpiler_create(VariableMemPool* pool) {
    SchemaTranspiler* transpiler = (SchemaTranspiler*)pool_calloc(pool, sizeof(SchemaTranspiler));
    if (!transpiler) return NULL;

    // Initialize base transpiler fields
    transpiler->ast_pool = pool;
    transpiler->type_list = arraylist_new(16);

    // Initialize schema-specific fields
    transpiler->type_registry = type_registry_create(pool);
    transpiler->schema_mode = false;
    transpiler->pending_references = arraylist_new(8);

    return transpiler;
}

void schema_transpiler_destroy(SchemaTranspiler* transpiler) {
    if (!transpiler) return;

    if (transpiler->type_registry) {
        type_registry_destroy(transpiler->type_registry);
    }

    if (transpiler->pending_references) {
        arraylist_free(transpiler->pending_references);
    }

    if (transpiler->type_list) {
        arraylist_free(transpiler->type_list);
    }

    // Note: memory pool cleanup handled by caller
}

void schema_transpiler_enable_validation(SchemaTranspiler* transpiler) {
    if (transpiler) {
        transpiler->schema_mode = true;
    }
}

void schema_transpiler_add_type_definition(SchemaTranspiler* transpiler, StrView name, TypeSchema* schema) {
    if (!transpiler || !schema) return;

    Type* runtime_type = schema_to_runtime_type(schema, transpiler->ast_pool);
    type_registry_add(transpiler->type_registry, name, schema, runtime_type);
}
