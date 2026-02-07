/**
 * @file suggestions.cpp
 * @brief String similarity and suggestion algorithms for validation
 * @author Lambda Validator Enhancement
 */

#include "validator.hpp"
#include "../lambda-data.hpp"
#include "../../lib/arraylist.h"
#include <string.h>
#include <stdlib.h>
#include <algorithm>

// ==================== Levenshtein Distance ====================

/**
 * Calculate Levenshtein distance (edit distance) between two strings
 * Used for typo detection and field name suggestions
 *
 * @param s1 First string
 * @param s2 Second string
 * @return Edit distance (number of single-character edits needed)
 */
static int levenshtein_distance(const char* s1, const char* s2) {
    if (!s1 || !s2) return 1000; // Large value for null strings

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    // Quick checks
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    // Use stack allocation for small strings, heap for larger ones
    const size_t max_stack_size = 64;
    int stack_matrix[(max_stack_size + 1) * (max_stack_size + 1)];
    int* heap_matrix = nullptr;
    int* matrix;

    if (len1 <= max_stack_size && len2 <= max_stack_size) {
        matrix = stack_matrix;
    } else {
        heap_matrix = (int*)malloc((len1 + 1) * (len2 + 1) * sizeof(int));
        matrix = heap_matrix;
    }

    // Initialize first column and row
    for (size_t i = 0; i <= len1; i++) {
        matrix[i * (len2 + 1)] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        matrix[j] = j;
    }

    // Calculate distances
    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;

            int deletion = matrix[(i - 1) * (len2 + 1) + j] + 1;
            int insertion = matrix[i * (len2 + 1) + (j - 1)] + 1;
            int substitution = matrix[(i - 1) * (len2 + 1) + (j - 1)] + cost;

            matrix[i * (len2 + 1) + j] = std::min({deletion, insertion, substitution});
        }
    }

    int result = matrix[len1 * (len2 + 1) + len2];

    if (heap_matrix) {
        free(heap_matrix);
    }

    return result;
}

// ==================== Field Name Suggestions ====================

/**
 * Structure to hold a suggestion with its distance score
 */
typedef struct {
    const char* name;
    int distance;
} Suggestion;

/**
 * Comparison function for sorting suggestions by distance
 */
static int compare_suggestions(const void* a, const void* b) {
    const Suggestion* sa = (const Suggestion*)a;
    const Suggestion* sb = (const Suggestion*)b;
    return sa->distance - sb->distance;
}

/**
 * Generate field name suggestions based on a typo
 *
 * @param typo_field The mistyped field name
 * @param map_type The map type containing available fields
 * @param pool Memory pool for allocations
 * @return List of String* suggestions (may be nullptr if none found)
 */
List* generate_field_suggestions(const char* typo_field, TypeMap* map_type, Pool* pool) {
    if (!typo_field || !map_type || !map_type->shape) {
        return nullptr;
    }

    // Collect all available field names with their distances
    const int max_suggestions = 10;
    Suggestion suggestions[max_suggestions];
    int suggestion_count = 0;

    ShapeEntry* entry = map_type->shape;
    while (entry && suggestion_count < max_suggestions) {
        if (entry->name) {
            const char* field_name = entry->name->str;
            int distance = levenshtein_distance(typo_field, field_name);

            // Only suggest if distance is reasonable (within 3 edits)
            if (distance <= 3 && distance > 0) {
                suggestions[suggestion_count].name = field_name;
                suggestions[suggestion_count].distance = distance;
                suggestion_count++;
            }
        }
        entry = entry->next;
    }

    // Sort by distance
    if (suggestion_count > 0) {
        qsort(suggestions, suggestion_count, sizeof(Suggestion), compare_suggestions);
    }

    // Create list of suggestions (return up to 3 best matches)
    if (suggestion_count == 0) {
        return nullptr;
    }

    List* result = (List*)pool_calloc(pool, sizeof(List));
    int count = (3 < suggestion_count) ? 3 : suggestion_count;
    for (int i = 0; i < count; i++) {
        const char* text = suggestions[i].name;
        size_t len = strlen(text);
        String* str = (String*)pool_calloc(pool, sizeof(String) + len + 1);
        str->len = len;
        str->ref_cnt = 0;
        memcpy(str->chars, text, len);
        str->chars[len] = '\0';
        Item item = {.item = s2it(str)};
        list_push(result, item);
    }

    return result;
}

// ==================== Type Mismatch Suggestions ====================

// Note: get_type_name() is now provided globally via lambda.h

/**
 * Generate type mismatch suggestions
 *
 * @param actual_type The actual type id found
 * @param expected_type The expected type
 * @param pool Memory pool for allocations
 * @return List of String* suggestions (may be nullptr)
 */
List* generate_type_suggestions(TypeId actual_type, Type* expected_type, Pool* pool) {
    if (!expected_type) return nullptr;

    List* suggestions = nullptr;

    // helper to create string from literal
    auto create_suggestion_string = [](const char* text, Pool* p) -> String* {
        size_t len = strlen(text);
        String* str = (String*)pool_calloc(p, sizeof(String) + len + 1);
        str->len = len;
        str->ref_cnt = 0;
        memcpy(str->chars, text, len);
        str->chars[len] = '\0';
        return str;
    };

    // type-specific suggestions
    if (expected_type->type_id == LMD_TYPE_STRING && actual_type == LMD_TYPE_INT) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Try wrapping the value in quotes: \"42\"", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }
    else if (expected_type->type_id == LMD_TYPE_INT && actual_type == LMD_TYPE_FLOAT) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Remove decimal part or use integer value", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }
    else if (expected_type->type_id == LMD_TYPE_INT && actual_type == LMD_TYPE_STRING) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Try removing quotes: 42 instead of \"42\"", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }
    else if (expected_type->type_id == LMD_TYPE_BOOL && actual_type == LMD_TYPE_STRING) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Use boolean value: true or false (without quotes)", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }
    else if (expected_type->type_id == LMD_TYPE_ARRAY && actual_type != LMD_TYPE_ARRAY) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Wrap value in array brackets: [value]", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }
    else if (expected_type->type_id == LMD_TYPE_MAP && actual_type != LMD_TYPE_MAP) {
        suggestions = (List*)pool_calloc(pool, sizeof(List));
        String* s = create_suggestion_string("Use map syntax: {key: value}", pool);
        list_push(suggestions, (Item){.item = s2it(s)});
    }

    return suggestions;
}

/**
 * Generate suggestions for a validation error
 * Called from error reporting to add helpful hints
 *
 * @param error The validation error
 * @param pool Memory pool for allocations
 * @return List of String* suggestions (may be nullptr)
 */
List* generate_error_suggestions(ValidationError* error, Pool* pool) {
    if (!error) return nullptr;

    switch (error->code) {
        case VALID_ERROR_TYPE_MISMATCH:
            if (error->expected && error->actual.item) {
                Type* expected = (Type*)error->expected;
                return generate_type_suggestions(error->actual.type_id(), expected, pool);
            }
            break;

        case VALID_ERROR_MISSING_FIELD:
            // Could suggest similar field names if we had the map type
            break;

        case VALID_ERROR_UNEXPECTED_FIELD:
            // Could suggest similar valid field names
            break;

        default:
            break;
    }

    return nullptr;
}
