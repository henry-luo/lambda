/**
 * @file validator.cpp
 * @brief Lambda Schema Validator - Core Implementation (C++)
 * @author Henry Luo
 * @license MIT
 */

 #include "validator.hpp"
 #include <cstring>
 #include <cassert>
 #include <memory>
 #include <string>
 #include <cmath>

 // Debug flag - set to 0 to disable all SCHEMA_DEBUG output
 #define ENABLE_SCHEMA_DEBUG 0

 // External function declarations
 extern "C" {
     TSParser* lambda_parser(void);
     TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
 }

 // Runtime function declarations

 // ==================== Hashmap Entry Structures ====================

 struct SchemaEntry {
     StrView name;
     TypeSchema* schema;
 };

 struct VisitedEntry {
     StrView key;
     bool visited;
 };

 // ==================== Hashmap Helper Functions ====================

 // Hash function for StrView keys
 uint64_t strview_hash(const void *item, uint64_t seed0, uint64_t seed1) {
     const StrView* key = (const StrView*)item;
     return hashmap_sip(key->str, key->length, seed0, seed1);
 }

 // Compare function for StrView keys
 int strview_compare(const void *a, const void *b, void *udata) {
     const StrView* key_a = (const StrView*)a;
     const StrView* key_b = (const StrView*)b;

     if (key_a->length != key_b->length) {
         return (key_a->length < key_b->length) ? -1 : 1;
     }
     return memcmp(key_a->str, key_b->str, key_a->length);
 }

 // ==================== Schema Validator Creation ====================

 SchemaValidator* schema_validator_create(VariableMemPool* pool) {
     SchemaValidator* validator = (SchemaValidator*)pool_calloc(pool, sizeof(SchemaValidator));
     if (!validator) return nullptr;

     validator->pool = pool;

     // Create C hashmap for schemas
     validator->schemas = hashmap_new(
         sizeof(SchemaEntry), 16, 0, 1,
         strview_hash, strview_compare, nullptr, pool
     );

     if (!validator->schemas) {
         return nullptr;
     }

     validator->context = (ValidationContext*)pool_calloc(pool, sizeof(ValidationContext));
     if (!validator->context) {
         return nullptr;
     }

     validator->custom_validators = nullptr;

     // Initialize default options
     validator->default_options.strict_mode = false;
     validator->default_options.allow_unknown_fields = true;
     validator->default_options.allow_empty_elements = false;
     validator->default_options.max_depth = 100;
     validator->default_options.timeout_ms = 0;

     // Initialize validation context
     validator->context->pool = pool;
     validator->context->path = nullptr;
     validator->context->schema_registry = validator->schemas;

     // Create visited tracking hashmap
     validator->context->visited = hashmap_new(
         sizeof(VisitedEntry), 16, 0, 1,
         strview_hash, strview_compare, nullptr, pool
     );

     if (!validator->context->visited) {
         return nullptr;
     }

     validator->context->custom_validators = nullptr;
     validator->context->options = validator->default_options;
     validator->context->current_depth = 0;

     return validator;
 }

 void schema_validator_destroy(SchemaValidator* validator) {
     if (!validator) return;

     // Cleanup hashmaps
     if (validator->schemas) {
         hashmap_free(validator->schemas);
     }
     if (validator->context && validator->context->visited) {
         hashmap_free(validator->context->visited);
     }
     // Note: memory pool cleanup handled by caller
 }

 // ==================== Schema Loading ====================

 int schema_validator_load_schema(SchemaValidator* validator, const char* schema_source,
                                  const char* schema_name) {
     if (!validator || !schema_source || !schema_name) return -1;

     SchemaParser* parser = schema_parser_create(validator->pool);
     if (!parser) return -1;

     // Parse all types first - use the same code as parse_schema_from_source
     TSTree* tree = lambda_parse_source(parser->base.parser, schema_source);
     if (!tree) {
         schema_parser_destroy(parser);
         return -1;
     }

     TSNode root = ts_tree_root_node(tree);
     parser->current_source = schema_source;
     parser->current_tree = tree;

     // First, collect all type definitions from the source
     parse_all_type_definitions(parser, root);

     // Try to find the requested root type
     printf("[SCHEMA] DEBUG: Looking for root type '%s'\n", schema_name);
     TypeSchema* root_schema = find_type_definition(parser, schema_name);
     if (!root_schema) {
         printf("[SCHEMA] DEBUG: Root type '%s' not found, trying 'Document'\n", schema_name);
         // Fallback: if the requested type isn't found, try "Document"
         root_schema = find_type_definition(parser, "Document");
         if (!root_schema) {
             printf("[SCHEMA] DEBUG: 'Document' type also not found\n");
         } else {
             printf("[SCHEMA] DEBUG: Found 'Document' type as fallback\n");
         }
     }
     if (!root_schema) {
         // If still no root schema found, use the first available type
         if (parser->type_definitions && parser->type_definitions->length > 0) {
             TypeDefinition* first_def = (TypeDefinition*)parser->type_definitions->data[0];
             if (first_def && first_def->schema_type) {
                 root_schema = first_def->schema_type;
             }
         }
     }

     if (!root_schema) {
         schema_parser_destroy(parser);
         return -1;
     }

     // Register ALL parsed type definitions in the validator's schema registry
     if (parser->type_definitions) {
         for (long i = 0; i < parser->type_definitions->length; i++) {
             TypeDefinition* def = (TypeDefinition*)parser->type_definitions->data[i];
             if (def && def->schema_type) {
                 SchemaEntry entry = { .name = def->name, .schema = def->schema_type };
                 const void* result = hashmap_set(validator->schemas, &entry);
                 if (result == NULL && hashmap_oom(validator->schemas)) {
                     printf("[SCHEMA] WARNING: Failed to register type definition: %.*s\n",
                            (int)def->name.length, def->name.str);
                 } else {
                     printf("[SCHEMA] DEBUG: Registered type definition: %.*s\n",
                            (int)def->name.length, def->name.str);
                 }
             }
         }
     }

     // Store root schema in C hashmap registry under the requested name
     StrView name_view = strview_from_cstr(schema_name);
     SchemaEntry root_entry = { .name = name_view, .schema = root_schema };
     const void* result = hashmap_set(validator->schemas, &root_entry);
     if (result == NULL && hashmap_oom(validator->schemas)) {
         schema_parser_destroy(parser);
         return -1;
     }

     printf("[SCHEMA] DEBUG: Registered root schema as: %s\n", schema_name);

     schema_parser_destroy(parser);
     return 0;
 }

 // ==================== Validation Engine ====================

 ValidationResult* validate_item(SchemaValidator* validator, ConstItem typed_item,
                                 TypeSchema* schema, ValidationContext* context) {
     // ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: -1);

     if (!validator || !schema || !context) {
         ValidationResult* result = create_validation_result(context ? context->pool : validator->pool);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_PARSE_ERROR, "Invalid validation parameters",
             context ? context->path : nullptr,
             context ? context->pool : validator->pool));
         return result;
     }

     // TRACE: Log validation entry
     printf("[TRACE] validate_item: depth=%d, schema_type=%d, item_type=%d\n",
            context->current_depth, schema->schema_type, typed_item.type_id);
     fflush(stdout);

     // Check validation depth
     if (context->current_depth >= context->options.max_depth) {
         //printf("[TRACE] validate_item: MAX DEPTH EXCEEDED at %d\n", context->current_depth);
         //fflush(stdout);
         ValidationResult* result = create_validation_result(context->pool);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded",
             context->path, context->pool));
         return result;
     }

     context->current_depth++;

     ValidationResult* result = nullptr;

     // Dispatch to appropriate validation function based on schema type
     if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: schema_type = %d\n", schema->schema_type);
     if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: LMD_SCHEMA_PRIMITIVE=%d, LMD_SCHEMA_UNION=%d, LMD_SCHEMA_ARRAY=%d\n",
            LMD_SCHEMA_PRIMITIVE, LMD_SCHEMA_UNION, LMD_SCHEMA_ARRAY);
     if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: LMD_SCHEMA_MAP=%d, LMD_SCHEMA_ELEMENT=%d, LMD_SCHEMA_REFERENCE=%d\n",
            LMD_SCHEMA_MAP, LMD_SCHEMA_ELEMENT, LMD_SCHEMA_REFERENCE);
     printf("[EMERGENCY_DEBUG] About to switch on schema_type=%d\n", schema->schema_type); fflush(stdout);
     switch (schema->schema_type) {
         case LMD_SCHEMA_PRIMITIVE:
             printf("[EMERGENCY_DEBUG] Matched LMD_SCHEMA_PRIMITIVE case\n"); fflush(stdout);
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_primitive\n");
             result = validate_primitive(typed_item, schema, context);
             break;
         case LMD_SCHEMA_UNION:
             printf("[EMERGENCY_DEBUG] Matched LMD_SCHEMA_UNION case\n"); fflush(stdout);
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_union\n");
             result = validate_union(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_ARRAY:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_array\n");
             result = validate_array(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_MAP:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_map\n");
             result = validate_map(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_ELEMENT:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_element\n");
             result = validate_element(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_OCCURRENCE:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_occurrence\n");
             result = validate_occurrence(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_REFERENCE:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_reference\n");
             result = validate_reference(validator, typed_item, schema, context);
             break;
         case LMD_SCHEMA_LITERAL:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: Calling validate_literal\n");
             result = validate_literal(typed_item, schema, context);
             break;
         default:
             if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_item: UNKNOWN SCHEMA TYPE %d - going to default case\n", schema->schema_type);
             result = create_validation_result(context->pool);
             add_validation_error(result, create_validation_error(
                 VALID_ERROR_TYPE_MISMATCH, "Unknown schema type",
                 context->path, context->pool));
             break;
     }

     // Run custom validators if any
     CustomValidator* custom = context->custom_validators;
     while (custom && result->valid) {
         ValidationResult* custom_result = custom->func(typed_item, schema, context);
         if (custom_result) {
             merge_validation_results(result, custom_result);
         }
         custom = custom->next;
     }

     // //printf("[TRACE] validate_item: depth=%d, exiting\n", context->current_depth);
     // //fflush(stdout);

     context->current_depth--;
     return result;
 }

 // ==================== Primitive Type Validation ====================

 ValidationResult* validate_primitive(ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_primitive: Starting primitive validation\n");
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_PRIMITIVE) {
         ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Schema is not primitive type (got %d)\n", schema->schema_type);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected primitive schema",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaPrimitive* prim_schema = (SchemaPrimitive*)schema->schema_data;
     if (!prim_schema) {
         ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Schema data is null\n");
         add_validation_error(result, create_validation_error(
             VALID_ERROR_PARSE_ERROR, "Invalid primitive schema data",
             ctx->path, ctx->pool));
         return result;
     }

     TypeId expected_type = prim_schema->primitive_type;
     TypeId actual_type = typed_item.type_id;

     if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_primitive: expected_type=%d, actual_type=%d\n",
            expected_type, actual_type);

     if (!is_compatible_type(actual_type, expected_type)) {
         if (ENABLE_SCHEMA_DEBUG) if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_primitive: Types not compatible - VALIDATION FAILED\n");
         char error_msg[256];
         snprintf(error_msg, sizeof(error_msg),
                 "Type mismatch: expected type %d, got type %d",
                 expected_type, actual_type);

         ValidationError* error = create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool);
         if (error) {
             error->expected = schema;
             // Convert ConstItem back to Item for error reporting
             Item actual_item;
             actual_item.raw_pointer = typed_item.pointer;
             error->actual = actual_item;
             add_validation_error(result, error);
         }
     } else {
         // Additional validation for numeric type conversions
         if (expected_type == LMD_TYPE_INT && actual_type == LMD_TYPE_FLOAT) {
             // For float to int conversion, check if the float is actually an integer
             double float_value = *(double*)typed_item.pointer;
             if (float_value != floor(float_value)) {
                 char error_msg[256];
                 snprintf(error_msg, sizeof(error_msg),
                         "Cannot convert float %.1f to int: has fractional part",
                         float_value);

                 ValidationError* error = create_validation_error(
                     VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool);
                 if (error) {
                     error->expected = schema;
                     // Convert ConstItem back to Item for error reporting
                     Item actual_item;
                     actual_item.raw_pointer = typed_item.pointer;
                     error->actual = actual_item;
                     add_validation_error(result, error);
                 }
             }
         }
         ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Types compatible - VALIDATION PASSED\n");
     }

     return result;
 }

 // ==================== Array Validation ====================

 ValidationResult* validate_array(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_ARRAY) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected array schema",
             ctx->path, ctx->pool));
         return result;
     }

     TypeId actual_type = typed_item.type_id;
     if (actual_type != LMD_TYPE_ARRAY && actual_type != LMD_TYPE_LIST) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected array or list",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaArray* array_schema = (SchemaArray*)schema->schema_data;
     List* list = (List*)typed_item.pointer;

     // Check occurrence constraints
     if (array_schema->occurrence == '+' && list->length == 0) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_OCCURRENCE_ERROR, "Array must have at least one element (+)",
             ctx->path, ctx->pool));
         return result;
     }

     // Validate each element
     if (array_schema->element_type) {
         for (long i = 0; i < list->length; i++) {
             ConstItem element_typed = list_get_const(list, i);

             // Create path for this element
             PathSegment* element_path = path_push_index(ctx->path, i, ctx->pool);
             ValidationContext element_ctx = *ctx;
             element_ctx.path = element_path;

             ValidationResult* element_result = validate_item(
                 validator, element_typed, array_schema->element_type, &element_ctx);

             if (element_result) {
                 merge_validation_results(result, element_result);
             }
         }
     }

     return result;
 }

 // ==================== Map Validation ====================

 ValidationResult* validate_map(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     // //printf("[TRACE] validate_map: depth=%d, entering\n", ctx->current_depth);
     // //fflush(stdout);

     ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Starting map validation\n");
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_MAP) {
         //printf("[TRACE] validate_map: schema not map type, got %d\n", schema->schema_type);
         //fflush(stdout);
         ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Schema is not map type (got %d)\n", schema->schema_type);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected map schema",
             ctx->path, ctx->pool));
         return result;
     }

     TypeId actual_type = typed_item.type_id();
     //printf("[TRACE] validate_map: actual_type=%d\n", actual_type);
     //fflush(stdout);
     ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: %d\n", actual_type);

     // Accept both MAP and ELEMENT types, since Elements can also act as Maps
     if (actual_type != LMD_TYPE_MAP && actual_type != LMD_TYPE_ELEMENT) {
         //printf("[TRACE] validate_map: type mismatch, expected map/element got %d\n", actual_type);
         //fflush(stdout);
         ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Type mismatch - expected map/element, got %d\n", actual_type);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected map",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaMap* map_schema = (SchemaMap*)schema->schema_data;
     if (!map_schema) {
         ////// if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Map schema data is null\n");
         add_validation_error(result, create_validation_error(
             VALID_ERROR_PARSE_ERROR, "Invalid map schema data",
             ctx->path, ctx->pool));
         return result;
     }

     const Map* map = typed_item.map;

     // Safety check: ensure map pointer is valid
     if (!map) {
         //printf("[TRACE] validate_map: NULL map pointer\n");
         //fflush(stdout);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Null map pointer",
             ctx->path, ctx->pool));
         return result;
     }

     ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Validating map with %d fields in schema\n",
     //       map_schema->field_count);

     // NOTE: The schema parser currently has incomplete field parsing implementation
     // This is why schema validation is too permissive - the map schema doesn't
     // contain the actual field definitions from the Document type

     // Validate required fields and check types
     SchemaMapField* field = map_schema->fields;
     int field_num = 0;
     //printf("[TRACE] validate_map: starting field validation loop\n");
     //fflush(stdout);

     while (field) {
         //printf("[TRACE] validate_map: field %d, name='%.*s', required=%d\n",
         //       field_num, (int)field->name.length, field->name.str, field->required);
         //fflush(stdout);
         ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: %s)\n",
         //       field_num, (int)field->name.length, field->name.str,
         //       field->required ? "yes" : "no");

         Item field_key = {.item = s2it(string_from_strview(field->name, ctx->pool))};
         //printf("[TRACE] validate_map: about to call map_get for field '%.*s'\n",
         //       (int)field->name.length, field->name.str);
         //printf("[TRACE] validate_map: map=%p, map->type_id=%d\n",
         //       map, map ? map->type_id : -1);
         //fflush(stdout);

         // Safety check: validate map structure before calling get function
         if (!map || (map->type_id != LMD_TYPE_MAP && map->type_id != LMD_TYPE_ELEMENT)) {
             //printf("[TRACE] validate_map: INVALID MAP STRUCTURE - type_id=%d, expected=%d or %d\n",
             //       map ? map->type_id : -1, LMD_TYPE_MAP, LMD_TYPE_ELEMENT);
             //fflush(stdout);
             add_validation_error(result, create_validation_error(
                 VALID_ERROR_CONSTRAINT_VIOLATION, "Invalid map structure detected",
                 ctx->path, ctx->pool));
             break;
         }

         ConstItem field_value;
         if (map->type_id == LMD_TYPE_ELEMENT) {
             // For Elements, first try to get from attributes
             Element* element = (Element*)map;
             field_value = elmt_get_const(element, field_key);

             // If attribute not found, check if this field matches a child element with text content
             if (field_value.item == ITEM_NULL) {
                 String* field_name_str = (String*)field_key.pointer;
                 if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: Looking for child element '%s' in %ld children\n",
                        field_name_str->chars, element->length);

                 // Look through child elements to find one with matching tag name
                 for (int i = 0; i < element->length; i++) {
                     Item child_item = element->items[i];
                     TypeId child_type_id = get_type_id(child_item);
                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: Child %d has type %d (LMD_TYPE_ELEMENT=%d)\n",
                            i, child_type_id, LMD_TYPE_ELEMENT);

                     if (child_type_id == LMD_TYPE_ELEMENT) {
                         Element* child_element = (Element*)child_item.pointer;
                         if (child_element->type) {
                             TypeElmt* child_type = (TypeElmt*)child_element->type;
                             if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: Child element tag: '%.*s'\n",
                                    (int)child_type->name.length, child_type->name.str);

                             // Check if child element tag name matches field name
                             if (child_type->name.length == field_name_str->len &&
                                 memcmp(child_type->name.str, field_name_str->chars, field_name_str->len) == 0) {
                                 if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: Found matching child element '%s'\n",
                                        field_name_str->chars);

                                 // Found matching child element, check if it has text content
                                 if (child_element->length > 0) {
                                     Item first_child = child_element->items[0];
                                     TypeId first_child_type = get_type_id(first_child);
                                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: First child has type %d (LMD_TYPE_STRING=%d)\n",
                                            first_child_type, LMD_TYPE_STRING);

                                     if (first_child_type == LMD_TYPE_STRING) {
                                         // Use the text content as the field value
                                         field_value = first_child;
                                         if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: Using child element text content for field '%s'\n",
                                                field_name_str->chars);
                                         break;
                                     }
                                 }
                             }
                         }
                     }
                 }
             }
         } else {
             // For Maps, use map_get_const with safety checks
             //printf("[TRACE] validate_map: calling map_get_const for map\n");
             //fflush(stdout);

             // Safety check: ensure map and field_key are valid
             if (!map || !map->type || !map->data || field_key.item == ITEM_NULL) {
                 //printf("[TRACE] validate_map: SAFETY CHECK FAILED - invalid map or key\n");
                 //fflush(stdout);
                 field_value = *(ConstItem*)&ItemNull;
             } else {
                 field_value = map_get_const(map, field_key);
             }
         }

         //printf("[TRACE] validate_map: map_get returned, field_value.item=%llu\n", field_value.item);
         //fflush(stdout);

         // Check if this is a missing field vs a null value field
         bool field_is_missing = false;

         if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: field='%.*s', field_value.item=%llu, ITEM_NULL=%llu\n",
                (int)field->name.length, field->name.str, field_value.item, ITEM_NULL);

         if (field_value.item == ITEM_NULL) {
             if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: field_value.item == ITEM_NULL\n");
             // ITEM_NULL could mean either:
             // 1. Field exists but has null value (should be valid if schema expects null)
             // 2. Field is truly missing
             //
             // The key insight: if the schema expects null type, then ITEM_NULL is valid
             if (field->type->schema_type == LMD_SCHEMA_PRIMITIVE) {
                 SchemaPrimitive* prim = (SchemaPrimitive*)field->type->schema_data;
                 if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: primitive schema, prim=%p\n", prim);
                 if (prim) {
                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: expected type = %d, LMD_TYPE_NULL = %d\n",
                            prim->primitive_type, LMD_TYPE_NULL);
                 } else {
                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: prim is NULL\n");
                 }
                 if (prim && prim->primitive_type == LMD_TYPE_NULL) {
                     // Expected type is null, so ITEM_NULL is a valid value, field is not missing
                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: expected null, treating as valid field\n");
                     field_is_missing = false;
                 } else {
                     // Expected type is not null, so ITEM_NULL means missing field
                     if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: expected non-null, treating as missing field\n");
                     field_is_missing = true;
                 }
             } else {
                 // Non-primitive type expected, so ITEM_NULL means missing field
                 if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: non-primitive schema (type=%d), treating as missing field\n", field->type->schema_type);
                 field_is_missing = true;
             }
         } else {
             // Field has a non-null value, so it's definitely not missing
             if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: field_value.item != ITEM_NULL, field exists\n");
             field_is_missing = false;
         }

         if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_map: field_is_missing = %s\n", field_is_missing ? "true" : "false");

         if (field_is_missing) {
             // Field is truly missing
             //printf("[TRACE] validate_map: field is missing\n");
             //fflush(stdout);
             if (field->required) {
                 char error_msg[256];
                 snprintf(error_msg, sizeof(error_msg),
                         "Missing required field: %.*s",
                         (int)field->name.length, field->name.str);

                 PathSegment* field_path = path_push_field(ctx->path,
                     field->name.str, ctx->pool);
                 add_validation_error(result, create_validation_error(
                     VALID_ERROR_MISSING_FIELD, error_msg, field_path, ctx->pool));
             }
         } else {
             // Field exists - validate it (even if it has a null value)
             //printf("[TRACE] validate_map: field has value, validating\n");
             //fflush(stdout);
             // Validate field value
             PathSegment* field_path = path_push_field(ctx->path,
                 field->name.str, ctx->pool);
             ValidationContext field_ctx = *ctx;
             field_ctx.path = field_path;

             ValidationResult* field_result = validate_item(
                 validator, field_value_typed, field->type, &field_ctx);

             if (field_result) {
                 merge_validation_results(result, field_result);
             }
         }

         //printf("[TRACE] validate_map: advancing to next field\n");
         //fflush(stdout);
         field = field->next;
         field_num++;

         // Safety check: prevent infinite field loops
         if (field_num > 1000) {
             //printf("[TRACE] validate_map: FIELD LOOP SAFETY BREAK at %d fields\n", field_num);
             //fflush(stdout);
             add_validation_error(result, create_validation_error(
                 VALID_ERROR_CONSTRAINT_VIOLATION, "Too many fields in validation (safety)",
                 ctx->path, ctx->pool));
             break;
         }
     }

     // TODO: Check for unexpected fields if not open

     return result;
 }

 // ==================== Element Validation ====================

 ValidationResult* validate_element(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     printf("[TRACE] validate_element: depth=%d, entering\n", ctx->current_depth);
     fflush(stdout);

     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_ELEMENT) {
         printf("[TRACE] validate_element: ERROR - not element schema type\n");
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected element schema",
             ctx->path, ctx->pool));
         return result;
     }

     TypeId actual_type = typed_item.type_id;
     printf("[TRACE] validate_element: actual_type=%d (expecting %d=LMD_TYPE_ELEMENT)\n", actual_type, LMD_TYPE_ELEMENT);
     if (actual_type != LMD_TYPE_ELEMENT) {
         printf("[TRACE] validate_element: ERROR - not element item type\n");
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected element",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaElement* element_schema = (SchemaElement*)schema->schema_data;
     Element* element = (Element*)typed_item.pointer;

     printf("[TRACE] validate_element: element_schema=%p, element=%p\n", element_schema, element);
     printf("[TRACE] validate_element: element->length=%ld, element_schema->content_count=%d\n",
            element->length, element_schema->content_count);
     fflush(stdout);

     // Check for virtual XML document wrapper and unwrap if needed (only at depth 1, since depth is incremented before calling validate_element)
     if (ctx->current_depth == 1 && element->type) {
         TypeElmt* elmt_type = (TypeElmt*)element->type;
         if (elmt_type->name.length == 8 && memcmp(elmt_type->name.str, "document", 8) == 0) {
             printf("[TRACE] validate_element: Detected virtual <document> wrapper at root level, looking for actual XML element inside\n");

             // Find the actual XML element inside the <document> wrapper
             Element* actual_element = nullptr;
             for (size_t i = 0; i < element->length; i++) {
                 Item child_item = element->items[i];
                 if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                     Element* child_elem = (Element*)child_item.pointer;
                     if (child_elem && child_elem->type) {
                         TypeElmt* child_type = (TypeElmt*)child_elem->type;
                         // Skip processing instructions (<?xml...?>) and comments
                         if (child_type->name.length > 0 && child_type->name.str[0] != '?') {
                             actual_element = child_elem;
                             break;
                         }
                     }
                 }
             }

             if (actual_element) {
                 printf("[TRACE] validate_element: Found actual XML element, validating it instead\n");
                 // Create a new ConstItem for the actual element and validate it
                 // Temporarily increment depth to prevent recursive unwrapping
                 ctx->current_depth++;
                 ConstItem actual_typed;
                 actual_typed.type_id = LMD_TYPE_ELEMENT;
                 actual_typed.pointer = actual_element;
                 ValidationResult* unwrapped_result = validate_element(validator, actual_typed, schema, ctx);
                 ctx->current_depth--;
                 return unwrapped_result;
             } else {
                 printf("[TRACE] validate_element: No actual XML element found inside <document> wrapper\n");
                 add_validation_error(result, create_validation_error(
                     VALID_ERROR_INVALID_ELEMENT, "No XML element found inside document wrapper",
                     ctx->path, ctx->pool));
                 return result;
             }
         }
     }

     // Normal element tag validation
     if (element_schema->tag.length > 0 && element->type) {
         TypeElmt* elmt_type = (TypeElmt*)element->type;
         // Compare element tag name with schema tag
         if (elmt_type->name.length != element_schema->tag.length ||
             memcmp(elmt_type->name.str, element_schema->tag.str, element_schema->tag.length) != 0) {

             char error_msg[512];
             snprintf(error_msg, sizeof(error_msg),
                     "Element tag mismatch: expected <%.*s>, got <%.*s>",
                     (int)element_schema->tag.length, element_schema->tag.str,
                     (int)elmt_type->name.length, elmt_type->name.str);

             add_validation_error(result, create_validation_error(
                 VALID_ERROR_INVALID_ELEMENT, error_msg,
                 ctx->path, ctx->pool));
             return result;
         }
     }

     // Validate attributes
     if (element_schema->attributes) {
         SchemaMapField* required_attr = element_schema->attributes;
         while (required_attr) {
             // Check if required attribute exists
             Item attr_key = {.item = s2it(string_from_strview(required_attr->name, ctx->pool))};
             ConstItem attr_value_typed = elmt_get_const(element, attr_key);
             Item attr_value;
             attr_value.item = (uint64_t)attr_value_typed.pointer;

             if (attr_value_typed.type_id == LMD_TYPE_NULL) {
                 // Check if there's a child element with matching tag name
                 bool child_element_found = false;

                 // Iterate through child elements
                 for (size_t i = 0; i < element->length; i++) {
                     Item child_item = element->items[i];
                     if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                         Element* child_element = child_item.element;
                         if (child_element && child_element->type) {
                             TypeElmt* child_elmt_type = (TypeElmt*)child_element->type;

                             // Compare tag name with field name
                             if (child_elmt_type->name.length == required_attr->name.length &&
                                 strncmp(child_elmt_type->name.str, required_attr->name.str, required_attr->name.length) == 0) {

                                 // Found matching child element, get its text content
                                 if (child_element->length > 0 && get_type_id(child_element->items[0]) == LMD_TYPE_STRING) {
                                     child_element_found = true;
                                     attr_value = child_element->items[0];  // Use child element's text content
                                     break;
                                 }
                             }
                         }
                     }
                 }

                 if (!child_element_found && required_attr->required) {
                     char error_msg[256];
                     snprintf(error_msg, sizeof(error_msg),
                             "Missing required attribute: %.*s",
                             (int)required_attr->name.length, required_attr->name.str);

                     PathSegment* attr_path = path_push_attribute(ctx->path,
                         required_attr->name.str, ctx->pool);
                     add_validation_error(result, create_validation_error(
                         VALID_ERROR_MISSING_FIELD, error_msg, attr_path, ctx->pool));
                 }
             } else {
                 // Validate attribute type
                 PathSegment* attr_path = path_push_attribute(ctx->path,
                     required_attr->name.str, ctx->pool);
                 ValidationContext attr_ctx = *ctx;
                 attr_ctx.path = attr_path;

                 ValidationResult* attr_result = validate_item(
                     validator, attr_value_typed, required_attr->type, &attr_ctx);

                 if (attr_result) {
                     merge_validation_results(result, attr_result);
                 }
             }

             required_attr = required_attr->next;
         }
     }

     // Validate content types
     printf("[TRACE] validate_element: About to validate content - element_schema->content_count=%d\n", element_schema->content_count);
     if (element_schema->content_count > 0 && element_schema->content_types) {
         printf("[TRACE] validate_element: Validating %d content types against %ld element items\n",
                element_schema->content_count, element->length);
         for (int i = 0; i < element_schema->content_count; i++) {
             if (i < element->length) {
                 Item content_item = element->items[i];
                 printf("[TRACE] validate_element: Validating content[%d], type=%d\n", i, get_type_id(content_item));

                 PathSegment* content_path = path_push_index(ctx->path, i, ctx->pool);
                 ValidationContext content_ctx = *ctx;
                 content_ctx.path = content_path;

                 ConstItem content_typed;
                 content_typed.type_id = get_type_id(content_item);
                 content_typed.pointer = content_item.raw_pointer;
                 ValidationResult* content_result = validate_item(
                     validator, content_typed, element_schema->content_types[i], &content_ctx);

                 if (content_result) {
                     printf("[TRACE] validate_element: Content[%d] validation result: valid=%d, errors=%d\n",
                            i, content_result->valid, content_result->error_count);
                     merge_validation_results(result, content_result);
                 }
             } else {
                 printf("[TRACE] validate_element: Content[%d] missing (element has only %ld items)\n", i, element->length);

                 // Missing required content is a validation error
                 char error_msg[256];
                 snprintf(error_msg, sizeof(error_msg),
                         "Element is missing required content item %d (has %ld items, needs %d)",
                         i, element->length, element_schema->content_count);

                 PathSegment* content_path = path_push_index(ctx->path, i, ctx->pool);
                 add_validation_error(result, create_validation_error(
                     VALID_ERROR_MISSING_FIELD, error_msg, content_path, ctx->pool));
             }
         }

         // Check if element has too many content items (only if not open content model)
         if (!element_schema->is_open && element->length > element_schema->content_count) {
             printf("[TRACE] validate_element: Element has too many items (%ld > %d)\n", element->length, element_schema->content_count);
             char error_msg[256];
             snprintf(error_msg, sizeof(error_msg),
                     "Element has %ld content items, but schema allows only %d",
                     element->length, element_schema->content_count);

             add_validation_error(result, create_validation_error(
                 VALID_ERROR_CONSTRAINT_VIOLATION, error_msg,
                 ctx->path, ctx->pool));
         } else if (element_schema->is_open) {
             printf("[TRACE] validate_element: Open content model - allowing %ld items\n", element->length);
         }
     } else {
         printf("[TRACE] validate_element: No content validation (content_count=%d, content_types=%p)\n",
                element_schema->content_count, element_schema->content_types);
     }

     printf("[TRACE] validate_element: Returning result: valid=%d, errors=%d\n", result->valid, result->error_count);
     fflush(stdout);
     return result;
 }

 // ==================== Union Validation ====================

 ValidationResult* validate_union(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     //printf("[TRACE] validate_union: depth=%d, entering\n", ctx->current_depth);
     //fflush(stdout);

     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_UNION) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected union schema",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaUnion* union_schema = (SchemaUnion*)schema->schema_data;
     //printf("[TRACE] validate_union: type_count=%d\n", union_schema->type_count);
     //fflush(stdout);

     // Safety check: prevent excessive union validation attempts at deep levels
     if (ctx->current_depth > 50) {
         //printf("[TRACE] validate_union: DEPTH LIMIT EXCEEDED at %d\n", ctx->current_depth);
         //fflush(stdout);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_CONSTRAINT_VIOLATION, "Union validation depth limit exceeded (safety)",
             ctx->path, ctx->pool));
         return result;
     }

     // Try to validate against each type in the union
     for (int i = 0; i < union_schema->type_count; i++) {
         //printf("[TRACE] validate_union: trying type %d/%d\n", i+1, union_schema->type_count);
         //fflush(stdout);

         ValidationResult* type_result = validate_item(
             validator, typed_item, union_schema->types[i], ctx);

         if (type_result && type_result->valid) {
             //printf("[TRACE] validate_union: found matching type %d\n", i+1);
             //fflush(stdout);
             // Found matching type in union
             validation_result_destroy(result);
             return type_result;
         }

         if (type_result) {
             validation_result_destroy(type_result);
         }
     }

     //printf("[TRACE] validate_union: no matching type found\n");
     //fflush(stdout);

     // No types in union matched
     add_validation_error(result, create_validation_error(
         VALID_ERROR_TYPE_MISMATCH,
         "Value does not match any type in union",
         ctx->path, ctx->pool));

     return result;
 }

 // ==================== Occurrence Validation ====================

 ValidationResult* validate_occurrence(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_OCCURRENCE) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected occurrence schema",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaOccurrence* occur_schema = (SchemaOccurrence*)schema->schema_data;

     switch (occur_schema->modifier) {
         case '?': // Optional
             if (typed_item.type_id == LMD_TYPE_NULL) {
                 // Null is valid for optional types
                 return result;
             }
             // Fall through to validate the base type
             validation_result_destroy(result);
             return validate_item(validator, typed_item, occur_schema->base_type, ctx);

         case '+': // One or more
         case '*': // Zero or more
             {
                 // These need special array-like validation with occurrence constraints
                 TypeId actual_type = typed_item.type_id;
                 if (actual_type != LMD_TYPE_ARRAY && actual_type != LMD_TYPE_LIST) {
                     // Single item case - check if it satisfies the occurrence constraint
                     if (occur_schema->modifier == '+') {
                         // Single item satisfies "one or more"
                         validation_result_destroy(result);
                         return validate_item(validator, typed_item, occur_schema->base_type, ctx);
                     } else {
                         // '*' allows zero, so single item is fine too
                         validation_result_destroy(result);
                         return validate_item(validator, typed_item, occur_schema->base_type, ctx);
                     }
                 } else {
                     // Array/List case - validate each element and check count
                     List* list = (List*)typed_item.pointer;

                     // Check occurrence constraints
                     if (occur_schema->modifier == '+' && list->length == 0) {
                         add_validation_error(result, create_validation_error(
                             VALID_ERROR_OCCURRENCE_ERROR, "Must have at least one element (+)",
                             ctx->path, ctx->pool));
                         return result;
                     }

                     // Validate each element against the base type
                     for (long i = 0; i < list->length; i++) {
                         ConstItem element_typed = list_get_const(list, i);

                         // Create path for this element
                         PathSegment* element_path = path_push_index(ctx->path, i, ctx->pool);
                         ValidationContext element_ctx = *ctx;
                         element_ctx.path = element_path;

                         ValidationResult* element_result = validate_item(
                             validator, element_typed, occur_schema->base_type, &element_ctx);

                         if (element_result) {
                             merge_validation_results(result, element_result);
                         }
                     }

                     return result;
                 }
             }

         default:
             add_validation_error(result, create_validation_error(
                 VALID_ERROR_OCCURRENCE_ERROR, "Invalid occurrence modifier",
                 ctx->path, ctx->pool));
             return result;
     }
 }

 // ==================== Reference Validation ====================

 ValidationResult* validate_reference(SchemaValidator* validator, ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Starting reference validation for '%.*s'\n",
     //       (int)schema->name.length, schema->name.str);
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_REFERENCE) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected reference schema",
             ctx->path, ctx->pool));
         return result;
     }

     // Check for circular references FIRST
     VisitedEntry lookup = { .key = schema->name, .visited = false };
     const VisitedEntry* visited_entry = (const VisitedEntry*)hashmap_get(ctx->visited, &lookup);
     if (visited_entry && visited_entry->visited) {
         ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Circular reference detected for '%.*s'\n",
         //       (int)schema->name.length, schema->name.str);
         add_validation_error(result, create_validation_error(
             VALID_ERROR_CIRCULAR_REFERENCE, "Circular type reference detected",
             ctx->path, ctx->pool));
         return result;
     }

     // Resolve the reference
     TypeSchema* resolved = resolve_reference(schema, ctx->schema_registry);
     if (!resolved) {
         char error_msg[256];
         snprintf(error_msg, sizeof(error_msg),
                 "Cannot resolve type reference: %.*s",
                 (int)schema->name.length, schema->name.str);

         add_validation_error(result, create_validation_error(
             VALID_ERROR_REFERENCE_ERROR, error_msg, ctx->path, ctx->pool));
         return result;
     }

     // if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_reference: Resolved '%.*s' to schema type %d\n",
     //        (int)schema->name.length, schema->name.str, resolved->schema_type);

     // Mark as visited and validate
     VisitedEntry entry = { .key = schema->name, .visited = true };
     hashmap_set(ctx->visited, &entry);

     validation_result_destroy(result);

     // Enhanced type reference matching: if resolved type is primitive string
     // and we have a string value, allow direct validation
     if (resolved->schema_type == LMD_SCHEMA_PRIMITIVE) {
         SchemaPrimitive* prim = (SchemaPrimitive*)resolved->schema_data;
         if (prim && prim->primitive_type == LMD_TYPE_STRING &&
             typed_item.type_id == LMD_TYPE_STRING) {
             // Direct string match for type aliases like EmailAddress = string
             result = create_validation_result(ctx->pool);
             result->valid = true;
         } else {
             result = validate_item(validator, typed_item, resolved, ctx);
         }
     } else {
         result = validate_item(validator, typed_item, resolved, ctx);
     }

     // Unmark as visited
     VisitedEntry unmark_entry = { .key = schema->name, .visited = false };
     hashmap_set(ctx->visited, &unmark_entry);

     ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Finished validating '%.*s'\n",
     //       (int)schema->name.length, schema->name.str);
     return result;
 }

 // ==================== Literal Validation ====================

 ValidationResult* validate_literal(ConstItem typed_item, TypeSchema* schema, ValidationContext* ctx) {
     ValidationResult* result = create_validation_result(ctx->pool);

     if (schema->schema_type != LMD_SCHEMA_LITERAL) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Expected literal schema",
             ctx->path, ctx->pool));
         return result;
     }

     SchemaLiteral* literal_schema = (SchemaLiteral*)schema->schema_data;

     // Compare items directly - need to implement proper comparison for ConstItem
     // For now, compare pointers as a basic check
     if (typed_item.pointer != literal_schema->literal_value.raw_pointer) {
         add_validation_error(result, create_validation_error(
             VALID_ERROR_TYPE_MISMATCH, "Value does not match literal",
             ctx->path, ctx->pool));
     }

     return result;
 }

 // ==================== Validation Result Management ====================

 ValidationResult* create_validation_result(VariableMemPool* pool) {
     ValidationResult* result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
     result->valid = true;
     result->errors = NULL;
     result->warnings = NULL;
     result->error_count = 0;
     result->warning_count = 0;
     return result;
 }

 void add_validation_error(ValidationResult* result, ValidationError* error) {
     if (!result || !error) return;

     error->next = result->errors;
     result->errors = error;
     result->error_count++;
     result->valid = false;
 }

 void merge_validation_results(ValidationResult* dest, ValidationResult* src) {
     if (!dest || !src) return;

     // Merge errors
     ValidationError* error = src->errors;
     while (error) {
         ValidationError* next = error->next;
         error->next = dest->errors;
         dest->errors = error;
         dest->error_count++;
         error = next;
     }

     // Merge warnings
     ValidationWarning* warning = src->warnings;
     while (warning) {
         ValidationWarning* next = warning->next;
         warning->next = dest->warnings;
         dest->warnings = warning;
         dest->warning_count++;
         warning = next;
     }

     if (src->error_count > 0) {
         dest->valid = false;
     }

     // Clear source lists to avoid double-free
     src->errors = NULL;
     src->warnings = NULL;
 }

 // ==================== Error Creation ====================

 ValidationError* create_validation_error(ValidationErrorCode code, const char* message,
                                         PathSegment* path, VariableMemPool* pool) {
     ValidationError* error = (ValidationError*)pool_calloc(pool, sizeof(ValidationError));
     error->code = code;
     error->message = string_from_strview(strview_from_cstr(message), pool);
     error->path = path;
     error->expected = NULL;
     error->actual = (Item){.item = ITEM_NULL};
     error->suggestions = NULL;
     error->next = NULL;
     return error;
 }

 // ==================== Utility Functions ====================

 // Path management functions
 PathSegment* create_path_segment(PathSegmentType type, VariableMemPool* pool) {
     PathSegment* segment = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
     segment->type = type;
     segment->next = NULL;
     return segment;
 }

 PathSegment* create_field_path(const char* field_name, VariableMemPool* pool) {
     PathSegment* segment = create_path_segment(PATH_FIELD, pool);
     segment->data.field_name = strview_from_cstr(field_name);
     return segment;
 }

 PathSegment* create_index_path(long index, VariableMemPool* pool) {
     PathSegment* segment = create_path_segment(PATH_INDEX, pool);
     segment->data.index = index;
     return segment;
 }

 PathSegment* create_element_path(const char* tag_name, VariableMemPool* pool) {
     PathSegment* segment = create_path_segment(PATH_ELEMENT, pool);
     segment->data.element_tag = strview_from_cstr(tag_name);
     return segment;
 }

 PathSegment* push_path_segment(ValidationContext* ctx, PathSegment* segment) {
     PathSegment* old_path = ctx->path;
     segment->next = old_path;
     ctx->path = segment;
     return old_path;
 }

 void pop_path_segment(ValidationContext* ctx) {
     if (ctx->path) {
         ctx->path = ctx->path->next;
     }
 }

 PathSegment* path_push_field(PathSegment* path, const char* field_name, VariableMemPool* pool) {
     PathSegment* segment = create_field_path(field_name, pool);
     segment->next = path;
     return segment;
 }

 PathSegment* path_push_index(PathSegment* path, long index, VariableMemPool* pool) {
     PathSegment* segment = create_index_path(index, pool);
     segment->next = path;
     return segment;
 }

 PathSegment* path_push_element(PathSegment* path, const char* tag, VariableMemPool* pool) {
     PathSegment* segment = create_element_path(tag, pool);
     segment->next = path;
     return segment;
 }

 PathSegment* path_push_attribute(PathSegment* path, const char* attr_name, VariableMemPool* pool) {
     PathSegment* segment = create_path_segment(PATH_ATTRIBUTE, pool);
     segment->data.attr_name = strview_from_cstr(attr_name);
     segment->next = path;
     return segment;
 }

 // String formatting functions
 String* format_validation_path(PathSegment* path, VariableMemPool* pool) {
     if (!path) {
         return string_from_strview(strview_from_cstr(""), pool);
     }

     // Calculate total length needed
     size_t total_len = 0;
     PathSegment* current = path;
     while (current) {
         switch (current->type) {
             case PATH_FIELD:
                 total_len += 1 + current->data.field_name.length; // "." + field name
                 break;
             case PATH_INDEX:
                 total_len += 20; // "[" + number + "]" (assume max 20 chars for number)
                 break;
             case PATH_ELEMENT:
                 total_len += 2 + current->data.element_tag.length; // "<" + tag + ">"
                 break;
             case PATH_ATTRIBUTE:
                 total_len += 1 + current->data.attr_name.length; // "@" + attr name
                 break;
         }
         current = current->next;
     }

     // Build path string (in reverse order)
     char* buffer = (char*)pool_calloc(pool, total_len + 1);
     char* pos = buffer;

     // Build path segments (reverse order since path is stored backwards)
     char temp_buffer[256];
     PathSegment* segments[100]; // assume max 100 path segments
     int segment_count = 0;

     current = path;
     while (current && segment_count < 100) {
         segments[segment_count++] = current;
         current = current->next;
     }

     // Now build string in correct order
     for (int i = segment_count - 1; i >= 0; i--) {
         PathSegment* segment = segments[i];
         switch (segment->type) {
             case PATH_FIELD:
                 *pos++ = '.';
                 memcpy(pos, segment->data.field_name.str, segment->data.field_name.length);
                 pos += segment->data.field_name.length;
                 break;
             case PATH_INDEX:
                 snprintf(temp_buffer, sizeof(temp_buffer), "[%ld]", segment->data.index);
                 strcpy(pos, temp_buffer);
                 pos += strlen(temp_buffer);
                 break;
             case PATH_ELEMENT:
                 *pos++ = '<';
                 memcpy(pos, segment->data.element_tag.str, segment->data.element_tag.length);
                 pos += segment->data.element_tag.length;
                 *pos++ = '>';
                 break;
             case PATH_ATTRIBUTE:
                 *pos++ = '@';
                 memcpy(pos, segment->data.attr_name.str, segment->data.attr_name.length);
                 pos += segment->data.attr_name.length;
                 break;
         }
     }

     *pos = '\0';
     return string_from_strview(strview_from_cstr(buffer), pool);
 }

 String* format_type_name(TypeSchema* type, VariableMemPool* pool) {
     if (!type) {
         return string_from_strview(strview_from_cstr("unknown"), pool);
     }

     switch (type->schema_type) {
         case LMD_SCHEMA_PRIMITIVE: {
             SchemaPrimitive* prim = (SchemaPrimitive*)type->schema_data;
             if (prim->primitive_type <= LMD_TYPE_ERROR && prim->primitive_type >= 0) {
                 return string_from_strview(strview_from_cstr(type_info[prim->primitive_type].name), pool);
             }
             return string_from_strview(strview_from_cstr("primitive"), pool);
         }
         case LMD_SCHEMA_ARRAY:
             return string_from_strview(strview_from_cstr("array"), pool);
         case LMD_SCHEMA_MAP:
             return string_from_strview(strview_from_cstr("map"), pool);
         case LMD_SCHEMA_ELEMENT:
             return string_from_strview(strview_from_cstr("element"), pool);
         case LMD_SCHEMA_UNION:
             return string_from_strview(strview_from_cstr("union"), pool);
         case LMD_SCHEMA_OCCURRENCE:
             return string_from_strview(strview_from_cstr("occurrence"), pool);
         case LMD_SCHEMA_REFERENCE:
             return string_from_strview(strview_from_cstr("reference"), pool);
         case LMD_SCHEMA_LITERAL:
             return string_from_strview(strview_from_cstr("literal"), pool);
         default:
             return string_from_strview(strview_from_cstr("unknown"), pool);
     }
 }

 String* format_validation_error(ValidationError* error, VariableMemPool* pool) {
     if (!error) {
         return string_from_strview(strview_from_cstr(""), pool);
     }

     char buffer[1024];
     String* path_str = format_validation_path(error->path, pool);

     snprintf(buffer, sizeof(buffer), "%s%s%s",
              path_str->chars,
              (path_str->len > 0) ? ": " : "",
              error->message ? error->message->chars : "Unknown error");

     return string_from_strview(strview_from_cstr(buffer), pool);
 }

 // ==================== Public API Implementation ====================

 // Wrapper structure for public API
 struct LambdaValidator {
     SchemaValidator* internal_validator;
     VariableMemPool* pool;
 };

 LambdaValidator* lambda_validator_create(void) {
     VariableMemPool* pool = NULL;
     if (pool_variable_init(&pool, 8192, 50) != MEM_POOL_ERR_OK) {
         return NULL;
     }

     LambdaValidator* validator = (LambdaValidator*)pool_calloc(pool, sizeof(LambdaValidator));
     validator->pool = pool;
     validator->internal_validator = schema_validator_create(pool);

     if (!validator->internal_validator) {
         pool_variable_destroy(pool);
         return NULL;
     }

     return validator;
 }

 void lambda_validator_destroy(LambdaValidator* validator) {
     if (!validator) return;

     if (validator->internal_validator) {
         schema_validator_destroy(validator->internal_validator);
     }

     if (validator->pool) {
         pool_variable_destroy(validator->pool);
     }
 }

 int lambda_validator_load_schema_string(LambdaValidator* validator, const char* schema_source, const char* schema_name) {
     if (!validator || !validator->internal_validator) return -1;

     return schema_validator_load_schema(validator->internal_validator, schema_source, schema_name);
 }

 int lambda_validator_load_schema_file(LambdaValidator* validator, const char* schema_path) {
     if (!validator || !schema_path) return -1;

     FILE* file = fopen(schema_path, "r");
     if (!file) return -1;

     // Get file size
     fseek(file, 0, SEEK_END);
     long file_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     // Read file content
     char* content = (char*)pool_calloc(validator->pool, file_size + 1);
     size_t bytes_read = fread(content, 1, file_size, file);
     content[bytes_read] = '\0';
     fclose(file);

     // Extract schema name from filename
     const char* filename = strrchr(schema_path, '/');
     filename = filename ? filename + 1 : schema_path;

     // Remove extension
     char schema_name[256];
     strncpy(schema_name, filename, sizeof(schema_name) - 1);
     schema_name[sizeof(schema_name) - 1] = '\0';

     char* dot = strrchr(schema_name, '.');
     if (dot) *dot = '\0';

     return lambda_validator_load_schema_string(validator, content, schema_name);
 }

 // Helper function to convert ValidationResult to LambdaValidationResult
 LambdaValidationResult* convert_validation_result(ValidationResult* internal_result, VariableMemPool* pool) {
     if (!internal_result) {
         LambdaValidationResult* result = (LambdaValidationResult*)pool_calloc(pool, sizeof(LambdaValidationResult));
         result->valid = false;
         result->error_count = 1;
         result->errors = (char**)pool_calloc(pool, 2 * sizeof(char*));", "oldString": "         result->errors = (char**)pool_calloc(pool, 2 * sizeof(char*));
         result->errors[0] = strdup("Internal validation error");
         result->errors[1] = NULL;
         return result;
     }

     LambdaValidationResult* result = (LambdaValidationResult*)pool_calloc(pool, sizeof(LambdaValidationResult));
     result->valid = internal_result->valid;
     result->error_count = internal_result->error_count;
     result->warning_count = internal_result->warning_count;

     // Convert errors to string array
     if (result->error_count > 0) {
         result->errors = (char**)pool_calloc(pool, (result->error_count + 1) * sizeof(char*));
         ValidationError* current_error = internal_result->errors;
         int i = 0;

         while (current_error && i < result->error_count) {
             String* error_str = format_validation_error(current_error, pool);
             result->errors[i] = strdup(error_str->chars);
             current_error = current_error->next;
             i++;
         }
         result->errors[result->error_count] = NULL;
     } else {
         result->errors = NULL;
     }

     // Convert warnings to string array
     if (result->warning_count > 0) {
         result->warnings = (char**)pool_calloc(pool, (result->warning_count + 1) * sizeof(char*));
         ValidationWarning* current_warning = internal_result->warnings;
         int i = 0;

         while (current_warning && i < result->warning_count) {
             String* warning_str = format_validation_error(current_warning, pool);
             result->warnings[i] = strdup(warning_str->chars);
             current_warning = current_warning->next;
             i++;
         }
         result->warnings[result->warning_count] = NULL;
     } else {
         result->warnings = NULL;
     }

     return result;
 }

 LambdaValidationResult* lambda_validate_string(LambdaValidator* validator, const char* document_source, const char* schema_name) {
     if (!validator || !validator->internal_validator || !document_source || !schema_name) {
         LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
         result->valid = false;
         result->error_count = 1;
         result->errors = (char**)calloc(2, sizeof(char*));
         result->errors[0] = strdup("Invalid validation parameters");
         result->errors[1] = NULL;
         return result;
     }

     // Parse document (this would need to be implemented based on the document format)
     // For now, create a placeholder item
     Item document_item = {.item = ITEM_NULL};

     ValidationResult* internal_result = validate_document(validator->internal_validator, document_item, schema_name);
     return convert_validation_result(internal_result, validator->pool);
 }

 LambdaValidationResult* lambda_validate_file(LambdaValidator* validator, const char* document_file, const char* schema_name) {
     if (!validator || !document_file || !schema_name) {
         LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
         result->valid = false;
         result->error_count = 1;
         result->errors = (char**)calloc(2, sizeof(char*));
         result->errors[0] = strdup("Invalid parameters");
         result->errors[1] = NULL;
         return result;
     }

     FILE* file = fopen(document_file, "r");
     if (!file) {
         LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
         result->valid = false;
         result->error_count = 1;
         result->errors = (char**)calloc(2, sizeof(char*));
         result->errors[0] = strdup("Could not open document file");
         result->errors[1] = NULL;
         return result;
     }

     // Get file size
     fseek(file, 0, SEEK_END);
     long file_size = ftell(file);
     fseek(file, 0, SEEK_SET);

     // Read file content
     char* content = (char*)malloc(file_size + 1);
     size_t bytes_read = fread(content, 1, file_size, file);
     content[bytes_read] = '\0';
     fclose(file);

     LambdaValidationResult* result = lambda_validate_string(validator, content, schema_name);
     free(content);

     return result;
 }

 void lambda_validation_result_free(LambdaValidationResult* result) {
     if (!result) return;

     if (result->errors) {
         for (int i = 0; i < result->error_count; i++) {
             free(result->errors[i]);
         }
         free(result->errors);
     }

     if (result->warnings) {
         for (int i = 0; i < result->warning_count; i++) {
             free(result->warnings[i]);
         }
         free(result->warnings);
     }

     free(result);
 }

 void lambda_validator_set_options(LambdaValidator* validator, LambdaValidationOptions* options) {
     if (!validator || !validator->internal_validator || !options) return;

     ValidationOptions* internal_options = &validator->internal_validator->default_options;
     internal_options->strict_mode = options->strict_mode;
     internal_options->allow_unknown_fields = options->allow_unknown_fields;
     internal_options->allow_empty_elements = options->allow_empty_elements;
     internal_options->max_depth = options->max_validation_depth;

     // Update context options as well
     if (validator->internal_validator->context) {
         validator->internal_validator->context->options = *internal_options;
     }
 }

 LambdaValidationOptions* lambda_validator_get_options(LambdaValidator* validator) {
     if (!validator || !validator->internal_validator) return NULL;

     ValidationOptions* internal_options = &validator->internal_validator->default_options;
     LambdaValidationOptions* options = (LambdaValidationOptions*)pool_calloc(validator->pool, sizeof(LambdaValidationOptions));

     options->strict_mode = internal_options->strict_mode;
     options->allow_unknown_fields = internal_options->allow_unknown_fields;
     options->allow_empty_elements = internal_options->allow_empty_elements;
     options->max_validation_depth = internal_options->max_depth;
     options->enabled_custom_rules = NULL;  // Not implemented yet
     options->disabled_rules = NULL;        // Not implemented yet

     return options;
 }

 // ==================== Utility Functions ====================

 void validation_result_destroy(ValidationResult* result) {
     if (!result) return;

     // Note: We don't free individual error messages or path components
     // since they're allocated from the memory pool and will be freed
     // when the pool is destroyed

     // The ValidationResult structure itself is also allocated from
     // the memory pool, so we don't need to explicitly free it
 }

 ValidationResult* validate_document(SchemaValidator* validator, Item document, const char* schema_name) {
     if (!validator || !schema_name) {
         return nullptr;
     }

     // Look up the schema by name in the validator's C++ schema registry
     StrView lookup_name;
     lookup_name.str = schema_name;
     lookup_name.length = strlen(schema_name);

     SchemaEntry lookup = { .name = lookup_name };
     const SchemaEntry* found_entry = (const SchemaEntry*)hashmap_get(validator->schemas, &lookup);

     // Debug: Print available schemas
     printf("DEBUG: Looking for schema '%s'\n", schema_name);
     printf("DEBUG: Available schemas in registry:\n");
     // TODO: Add schema registry enumeration for debugging

     if (!found_entry) {
         // If schema not found, fall back to a basic validation
         printf("Warning: Schema '%s' not found, using basic validation\n", schema_name);
         printf("DEBUG: Schema lookup failed - using fallback\n");
         printf("DEBUG: This means the Document type was not properly parsed from the schema\n");
         TypeSchema* fallback_schema = create_primitive_schema(LMD_TYPE_ANY, validator->pool);
         if (!fallback_schema) {
             return nullptr;
         }
         ConstItem document_typed;
         document_typed.type_id = get_type_id(document);
         document_typed.pointer = document.raw_pointer;
         return validate_item(validator, document_typed, fallback_schema, validator->context);
     }

     // Use the actual loaded schema for validation
     ConstItem document_typed;
     document_typed.type_id = get_type_id(document);
     document_typed.pointer = document.raw_pointer;

     // Safety check: ensure validation context is valid
     if (!validator->context) {
         printf("ERROR: Validation context is null\n");
         return nullptr;
     }

     // Safety check: ensure schema is valid
     if (!found_entry->schema) {
         printf("ERROR: Found schema entry has null schema\n");
         return nullptr;
     }

     return validate_item(validator, document_typed, found_entry->schema, validator->context);
 }
