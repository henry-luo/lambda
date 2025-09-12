/**
 * @file test_lambda_patterns.c
 * @brief KLEE test harness for Lambda Script specific null pointer patterns
 * @author Henry Luo
 * 
 * This test harness targets specific patterns used in Lambda Script that
 * are prone to null pointer vulnerabilities, based on the codebase structure.
 */

#include <klee/klee.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Simulate Lambda Script data structures (simplified versions)
typedef enum {
    LMD_TYPE_NONE = 0,
    LMD_TYPE_INT,
    LMD_TYPE_STRING,
    LMD_TYPE_ARRAY,
    LMD_TYPE_ERROR
} ItemType;

typedef struct {
    ItemType type_id;
    union {
        int int_val;
        char* str_val;
        struct {
            void** items;
            size_t count;
            size_t capacity;
        } array_val;
    } data;
} Item;

typedef struct {
    char* ptr;
    size_t size;
    size_t capacity;
} String;

typedef struct {
    Item* items;
    size_t count;
    size_t capacity;
} Pool;

// Test 1: Item creation and access patterns (common in Lambda Script)
Item* item_create_int(int value) {
    Item* item = malloc(sizeof(Item));
    if (!item) return NULL;
    
    item->type_id = LMD_TYPE_INT;
    item->data.int_val = value;
    return item;
}

Item* item_create_string(const char* str) {
    if (!str) return NULL; // Good null check
    
    Item* item = malloc(sizeof(Item));
    if (!item) return NULL;
    
    item->type_id = LMD_TYPE_STRING;
    item->data.str_val = malloc(strlen(str) + 1);
    if (!item->data.str_val) {
        free(item);
        return NULL;
    }
    
    strcpy(item->data.str_val, str);
    return item;
}

Item* item_create_array(size_t capacity) {
    Item* item = malloc(sizeof(Item));
    if (!item) return NULL;
    
    item->type_id = LMD_TYPE_ARRAY;
    item->data.array_val.items = malloc(capacity * sizeof(void*));
    if (!item->data.array_val.items) {
        free(item);
        return NULL;
    }
    
    item->data.array_val.count = 0;
    item->data.array_val.capacity = capacity;
    return item;
}

void item_free(Item* item) {
    if (!item) return;
    
    switch (item->type_id) {
        case LMD_TYPE_STRING:
            free(item->data.str_val); // Potential double-free if already freed
            break;
        case LMD_TYPE_ARRAY:
            // Missing: freeing individual array items
            free(item->data.array_val.items);
            break;
        default:
            break;
    }
    
    free(item);
}

// Dangerous accessor without null checks (common pattern)
int item_get_int(Item* item) {
    // Missing null check for item
    if (item->type_id != LMD_TYPE_INT) return 0;
    return item->data.int_val;
}

const char* item_get_string(Item* item) {
    // Multiple null pointer risks
    if (item->type_id != LMD_TYPE_STRING) return NULL;
    return item->data.str_val; // What if str_val is null?
}

// Test 2: Pool management patterns (memory pool common in Lambda Script)
Pool* pool_create(size_t initial_capacity) {
    Pool* pool = malloc(sizeof(Pool));
    if (!pool) return NULL;
    
    pool->items = malloc(initial_capacity * sizeof(Item));
    if (!pool->items) {
        free(pool);
        return NULL;
    }
    
    pool->count = 0;
    pool->capacity = initial_capacity;
    return pool;
}

int pool_add_item(Pool* pool, Item* item) {
    if (!pool) return -1; // Good null check
    // Missing null check for item parameter
    
    if (pool->count >= pool->capacity) {
        // Resize pool
        size_t new_capacity = pool->capacity * 2;
        Item* new_items = realloc(pool->items, new_capacity * sizeof(Item));
        if (!new_items) return -2;
        
        pool->items = new_items;
        pool->capacity = new_capacity;
    }
    
    // Potential null dereference if item is null
    pool->items[pool->count] = *item;
    pool->count++;
    return 0;
}

Item* pool_get_item(Pool* pool, size_t index) {
    // Multiple null pointer risks
    if (index >= pool->count) return NULL; // Missing pool null check
    return &pool->items[index]; // What if pool->items is null?
}

void pool_free(Pool* pool) {
    if (!pool) return;
    
    // Free all items in pool
    for (size_t i = 0; i < pool->count; i++) {
        // Potential issue: items might contain pointers that need freeing
        if (pool->items[i].type_id == LMD_TYPE_STRING) {
            free(pool->items[i].data.str_val);
        }
    }
    
    free(pool->items);
    free(pool);
}

// Test 3: String operations (common in parsing)
String* string_create(const char* initial) {
    String* str = malloc(sizeof(String));
    if (!str) return NULL;
    
    if (initial) {
        size_t len = strlen(initial);
        str->capacity = len + 16; // Some extra space
        str->ptr = malloc(str->capacity);
        if (!str->ptr) {
            free(str);
            return NULL;
        }
        strcpy(str->ptr, initial);
        str->size = len;
    } else {
        str->capacity = 16;
        str->ptr = malloc(str->capacity);
        if (!str->ptr) {
            free(str);
            return NULL;
        }
        str->ptr[0] = '\0';
        str->size = 0;
    }
    
    return str;
}

int string_append(String* str, const char* text) {
    // Missing null checks
    size_t text_len = strlen(text); // Potential null dereference
    
    if (str->size + text_len >= str->capacity) {
        size_t new_capacity = (str->size + text_len + 1) * 2;
        char* new_ptr = realloc(str->ptr, new_capacity);
        if (!new_ptr) return -1;
        
        str->ptr = new_ptr;
        str->capacity = new_capacity;
    }
    
    strcpy(str->ptr + str->size, text); // Multiple null risks
    str->size += text_len;
    return 0;
}

const char* string_get(String* str) {
    if (!str) return NULL;
    return str->ptr; // What if ptr is null?
}

void string_free(String* str) {
    if (str) {
        free(str->ptr); // What if ptr is already freed?
        free(str);
    }
}

// Test 4: Parser-like operations (common in Lambda Script)
typedef struct {
    const char* input;
    size_t pos;
    size_t length;
    Item* current_item;
} Parser;

Parser* parser_create(const char* input) {
    if (!input) return NULL;
    
    Parser* parser = malloc(sizeof(Parser));
    if (!parser) return NULL;
    
    parser->input = input; // Storing pointer - what if input is freed later?
    parser->pos = 0;
    parser->length = strlen(input);
    parser->current_item = NULL;
    
    return parser;
}

Item* parser_next_item(Parser* parser) {
    if (!parser) return NULL;
    if (parser->pos >= parser->length) return NULL;
    
    // Simplified parsing logic
    char c = parser->input[parser->pos]; // Potential null dereference
    
    if (c >= '0' && c <= '9') {
        // Parse integer
        int value = c - '0';
        parser->pos++;
        
        Item* item = item_create_int(value);
        parser->current_item = item; // Storing pointer without ownership management
        return item;
    } else if (c == '"') {
        // Parse string (simplified)
        parser->pos++; // Skip opening quote
        size_t start = parser->pos;
        
        while (parser->pos < parser->length && parser->input[parser->pos] != '"') {
            parser->pos++;
        }
        
        if (parser->pos >= parser->length) return NULL; // Unterminated string
        
        size_t len = parser->pos - start;
        char* str_value = malloc(len + 1);
        if (!str_value) return NULL;
        
        memcpy(str_value, parser->input + start, len);
        str_value[len] = '\0';
        
        parser->pos++; // Skip closing quote
        
        Item* item = item_create_string(str_value);
        free(str_value); // We copied it into the item
        
        parser->current_item = item;
        return item;
    }
    
    return NULL;
}

void parser_free(Parser* parser) {
    if (parser) {
        // Should we free current_item? Unclear ownership
        free(parser);
    }
}

// Main test function
int main() {
    // Create symbolic inputs
    int test_scenario;
    size_t size_param;
    int int_value;
    char* str_input;
    
    klee_make_symbolic(&test_scenario, sizeof(test_scenario), "test_scenario");
    klee_make_symbolic(&size_param, sizeof(size_param), "size_param");
    klee_make_symbolic(&int_value, sizeof(int_value), "int_value");
    klee_make_symbolic(&str_input, sizeof(str_input), "str_input");
    
    // Constrain inputs
    klee_assume(test_scenario >= 0 && test_scenario < 8);
    klee_assume(size_param > 0 && size_param <= 32);
    
    // Test 1: Item operations with null pointers
    if (test_scenario == 0) {
        Item* item1 = item_create_int(int_value);
        Item* item2 = item_create_string(NULL); // Should handle null gracefully
        
        if (item1) {
            int val = item_get_int(item1); // Should work
            assert(val == int_value);
            item_free(item1);
        }
        
        if (item2) {
            const char* str = item_get_string(item2); // Potential null dereference
            item_free(item2);
        }
    }
    
    // Test 2: Pool operations with null items
    if (test_scenario == 1) {
        Pool* pool = pool_create(size_param);
        if (pool) {
            Item* valid_item = item_create_int(42);
            Item* null_item = NULL;
            
            // Add valid item
            if (valid_item) {
                int result1 = pool_add_item(pool, valid_item);
                assert(result1 == 0);
            }
            
            // Add null item - should be caught
            int result2 = pool_add_item(pool, null_item);
            // This should fail gracefully, but might crash
            
            // Access items
            Item* retrieved = pool_get_item(pool, 0);
            if (retrieved) {
                int val = item_get_int(retrieved);
            }
            
            // Cleanup
            if (valid_item) item_free(valid_item);
            pool_free(pool);
        }
    }
    
    // Test 3: String operations with null inputs
    if (test_scenario == 2) {
        String* str1 = string_create("Hello");
        String* str2 = string_create(NULL);
        
        if (str1) {
            int result = string_append(str1, NULL); // Should handle null
            const char* content = string_get(str1);
            if (content) {
                assert(strlen(content) >= 5); // "Hello"
            }
            string_free(str1);
        }
        
        if (str2) {
            const char* content = string_get(str2);
            string_free(str2);
        }
    }
    
    // Test 4: Parser with potentially invalid input
    if (test_scenario == 3) {
        // Test with potentially null input
        Parser* parser = parser_create((const char*)str_input);
        
        if (parser) {
            Item* item = parser_next_item(parser);
            if (item) {
                // Try to access the item
                if (item->type_id == LMD_TYPE_INT) {
                    int val = item_get_int(item);
                } else if (item->type_id == LMD_TYPE_STRING) {
                    const char* str = item_get_string(item);
                }
                item_free(item);
            }
            parser_free(parser);
        }
    }
    
    // Test 5: Chain operations that can fail
    if (test_scenario == 4) {
        Pool* pool = pool_create(4);
        if (pool) {
            for (size_t i = 0; i < size_param && i < 8; i++) {
                Item* item = item_create_int(i);
                if (item) {
                    pool_add_item(pool, item);
                    item_free(item); // Note: pool has a copy, this should be safe
                }
            }
            
            // Access all items
            for (size_t i = 0; i < pool->count; i++) {
                Item* item = pool_get_item(pool, i);
                if (item) {
                    int val = item_get_int(item);
                }
            }
            
            pool_free(pool);
        }
    }
    
    return 0;
}
