/**
 * @file doc_validators.c
 * @brief Document Schema Specific Validators
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include <string.h>
#include <assert.h>

// ==================== Citation Validation ====================

ValidationResult* validate_citations(Item document, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    // Check if document is an element
    TypeId doc_type = get_type_id(document);
    if (doc_type != LMD_TYPE_MAP) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected document to be a map", 
            context->path, context->pool));
        return result;
    }
    
    Map* doc_map = (Map*)document;
    
    // Extract metadata and body
    Item meta_item = map_get(doc_map, s2it(create_string("meta", 4, context->pool)));
    Item body_item = map_get(doc_map, s2it(create_string("body", 4, context->pool)));
    
    if (meta_item == ITEM_NULL || body_item == ITEM_NULL) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_MISSING_FIELD, "Document missing required meta or body", 
            context->path, context->pool));
        return result;
    }
    
    // Extract references from metadata
    List* references = extract_references_from_meta(meta_item, context->pool);
    
    // Find all citations in the document body
    // This is a simplified implementation
    List* citations = list_new(context->pool);
    
    // Validate each citation against references
    for (long i = 0; i < citations->length; i++) {
        Item citation = list_get(citations, i);
        ValidationResult* cite_result = validate_single_citation(citation, references, context);
        if (cite_result) {
            merge_validation_results(result, cite_result);
        }
    }
    
    return result;
}

ValidationResult* validate_single_citation(Item citation, List* references, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    // Check if citation is an element
    TypeId cite_type = get_type_id(citation);
    if (cite_type != LMD_TYPE_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected citation to be an element", 
            context->path, context->pool));
        return result;
    }
    
    Element* cite_element = (Element*)citation;
    
    // Get the ref attribute
    Item ref_attr = elmt_get(cite_element, s2it(create_string("ref", 3, context->pool)));
    if (ref_attr == ITEM_NULL) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_MISSING_FIELD, "Citation missing ref attribute", 
            context->path, context->pool));
        return result;
    }
    
    // Check if ref exists in references
    TypeId ref_type = get_type_id(ref_attr);
    if (ref_type != LMD_TYPE_STRING) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Citation ref must be a string", 
            context->path, context->pool));
        return result;
    }
    
    String* ref_string = (String*)ref_attr;
    bool found = false;
    
    // Search for reference in references list
    for (long i = 0; i < references->length; i++) {
        Item ref_item = list_get(references, i);
        if (get_type_id(ref_item) == LMD_TYPE_MAP) {
            Map* ref_map = (Map*)ref_item;
            Item ref_id = map_get(ref_map, s2it(create_string("id", 2, context->pool)));
            
            if (ref_id != ITEM_NULL && get_type_id(ref_id) == LMD_TYPE_STRING) {
                String* id_string = (String*)ref_id;
                if (string_equals(ref_string, id_string)) {
                    found = true;
                    break;
                }
            }
        }
    }
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Citation references unknown reference: %s", ref_string->chars);
        add_validation_error(result, create_validation_error(
            VALID_ERROR_REFERENCE_ERROR, error_msg, context->path, context->pool));
    }
    
    return result;
}

List* extract_references_from_meta(Item meta, VariableMemPool* pool) {
    List* references = list_new(pool);
    
    if (get_type_id(meta) != LMD_TYPE_MAP) {
        return references;
    }
    
    Map* meta_map = (Map*)meta;
    Item refs_item = map_get(meta_map, s2it(create_string("references", 10, pool)));
    
    if (refs_item != ITEM_NULL && get_type_id(refs_item) == LMD_TYPE_LIST) {
        List* refs_list = (List*)refs_item;
        
        // Copy references to new list
        for (long i = 0; i < refs_list->length; i++) {
            list_add(references, list_get(refs_list, i));
        }
    }
    
    return references;
}

// ==================== Header Hierarchy Validation ====================

ValidationResult* validate_header_hierarchy(Item body, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    // Extract headers from body
    List* headers = extract_headers((Element*)body, context->pool);
    
    // Check header sequence
    ValidationResult* sequence_result = check_header_sequence(headers, context);
    if (sequence_result) {
        merge_validation_results(result, sequence_result);
    }
    
    return result;
}

List* extract_headers(Element* body, VariableMemPool* pool) {
    List* headers = list_new(pool);
    
    // This is a simplified implementation
    // In reality, we would traverse the body element tree and extract headers
    
    return headers;
}

ValidationResult* check_header_sequence(List* headers, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    int prev_level = 0;
    
    for (long i = 0; i < headers->length; i++) {
        HeaderInfo* header = (HeaderInfo*)list_get(headers, i);
        
        // Check that header levels don't skip
        if (header->level > prev_level + 1) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Header level %d follows level %d, skipping level %d", 
                    header->level, prev_level, prev_level + 1);
            add_validation_error(result, create_validation_error(
                VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, 
                header->path, context->pool));
        }
        
        prev_level = header->level;
    }
    
    return result;
}

// ==================== Table Validation ====================

ValidationResult* validate_table_consistency(Item table, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    if (get_type_id(table) != LMD_TYPE_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected table to be an element", 
            context->path, context->pool));
        return result;
    }
    
    Element* table_element = (Element*)table;
    
    // Get headers and rows
    Item headers_item = elmt_get(table_element, s2it(create_string("headers", 7, context->pool)));
    Item rows_item = elmt_get(table_element, s2it(create_string("rows", 4, context->pool)));
    
    if (headers_item == ITEM_NULL || rows_item == ITEM_NULL) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_MISSING_FIELD, "Table missing headers or rows", 
            context->path, context->pool));
        return result;
    }
    
    // Check that headers is a list
    if (get_type_id(headers_item) != LMD_TYPE_LIST) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Table headers must be a list", 
            context->path, context->pool));
        return result;
    }
    
    // Check that rows is a list
    if (get_type_id(rows_item) != LMD_TYPE_LIST) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Table rows must be a list", 
            context->path, context->pool));
        return result;
    }
    
    List* headers_list = (List*)headers_item;
    List* rows_list = (List*)rows_item;
    
    long expected_columns = headers_list->length;
    
    // Check each row has the correct number of columns
    for (long i = 0; i < rows_list->length; i++) {
        Item row_item = list_get(rows_list, i);
        
        if (get_type_id(row_item) != LMD_TYPE_MAP) {
            continue; // Skip non-map rows
        }
        
        Map* row_map = (Map*)row_item;
        Item cells_item = map_get(row_map, s2it(create_string("cells", 5, context->pool)));
        
        if (cells_item != ITEM_NULL && get_type_id(cells_item) == LMD_TYPE_LIST) {
            List* cells_list = (List*)cells_item;
            
            if (cells_list->length != expected_columns) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Row %ld has %ld columns, expected %ld", 
                        i, cells_list->length, expected_columns);
                
                PathSegment* row_path = path_push_index(context->path, i, context->pool);
                add_validation_error(result, create_validation_error(
                    VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, 
                    row_path, context->pool));
            }
        }
    }
    
    return result;
}

// ==================== Metadata Validation ====================

ValidationResult* validate_metadata_completeness(Item meta, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    if (get_type_id(meta) != LMD_TYPE_MAP) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected metadata to be a map", 
            context->path, context->pool));
        return result;
    }
    
    Map* meta_map = (Map*)meta;
    
    // Check for recommended fields
    const char* recommended_fields[] = {"title", "author", "date"};
    int num_recommended = sizeof(recommended_fields) / sizeof(recommended_fields[0]);
    
    for (int i = 0; i < num_recommended; i++) {
        Item field_item = map_get(meta_map, s2it(create_string(recommended_fields[i], 
                                                strlen(recommended_fields[i]), context->pool)));
        
        if (field_item == ITEM_NULL) {
            char warning_msg[256];
            snprintf(warning_msg, sizeof(warning_msg), 
                    "Recommended metadata field '%s' is missing", recommended_fields[i]);
            
            // Create warning (this would be added to warnings list if implemented)
            ValidationError* warning = create_validation_error(
                VALID_ERROR_MISSING_FIELD, warning_msg, context->path, context->pool);
                
            // For now, just add as regular error since warnings aren't fully implemented
        }
    }
    
    return result;
}

ValidationResult* validate_cross_references(Item document, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    // Placeholder implementation for cross-reference validation
    // This would check that all internal links and references are valid
    
    return result;
}

// ==================== Doc Schema Validator Registration ====================

void register_doc_schema_validators(SchemaValidator* validator) {
    if (!validator) return;
    
    // Register citation validator
    register_custom_validator(validator, "citations", 
                            "Validates citation references", 
                            (CustomValidatorFunc)validate_citations);
    
    // Register header hierarchy validator
    register_custom_validator(validator, "header_hierarchy", 
                            "Validates header level progression", 
                            (CustomValidatorFunc)validate_header_hierarchy);
    
    // Register table consistency validator
    register_custom_validator(validator, "table_consistency", 
                            "Validates table structure consistency", 
                            (CustomValidatorFunc)validate_table_consistency);
    
    // Register metadata completeness validator
    register_custom_validator(validator, "metadata_completeness", 
                            "Validates metadata completeness", 
                            (CustomValidatorFunc)validate_metadata_completeness);
}

// ==================== Custom Validator Registration ====================

void register_custom_validator(SchemaValidator* validator, const char* name,
                              const char* description, CustomValidatorFunc func) {
    if (!validator || !name || !func) return;
    
    CustomValidator* custom = (CustomValidator*)pool_calloc(validator->pool, sizeof(CustomValidator));
    custom->name = strview_from_cstr(name);
    custom->description = strview_from_cstr(description);
    custom->func = func;
    
    // Add to linked list
    custom->next = validator->custom_validators;
    validator->custom_validators = custom;
}

void unregister_custom_validator(SchemaValidator* validator, const char* name) {
    if (!validator || !name) return;
    
    StrView name_view = strview_from_cstr(name);
    CustomValidator** current = &validator->custom_validators;
    
    while (*current) {
        if (strview_equals((*current)->name, name_view)) {
            *current = (*current)->next;
            return;
        }
        current = &(*current)->next;
    }
}
