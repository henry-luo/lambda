#ifndef LAYOUT_COUNTERS_HPP
#define LAYOUT_COUNTERS_HPP

#include <stdint.h>
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"

/**
 * CSS Counter System for CSS 2.1 Section 12.4
 *
 * Implements automatic counters and numbering for generated content.
 * Counters are inherited through the document tree with proper scoping.
 */

// Forward declarations
typedef struct Arena Arena;
typedef struct DomElement DomElement;

// Counter value entry - represents one counter value in a scope
typedef struct CounterValue {
    const char* name;     // Counter name (e.g., "chapter", "section")
    int value;            // Current counter value
} CounterValue;

// Counter scope - represents counters at one element in the tree
typedef struct CounterScope {
    HashMap* counters;    // name -> CounterValue*
    CounterScope* parent; // Parent scope (for inheritance)
} CounterScope;

// Counter context - tracks counter state during layout traversal
typedef struct CounterContext {
    Arena* arena;         // Memory arena for allocations
    CounterScope* current_scope;  // Current counter scope
    ArrayList* scope_stack;       // Stack of scopes for tree traversal
} CounterContext;

// ============================================================================
// Counter Context Management
// ============================================================================

/**
 * Create a new counter context for layout traversal
 */
CounterContext* counter_context_create(Arena* arena);

/**
 * Destroy counter context and free resources
 */
void counter_context_destroy(CounterContext* ctx);

/**
 * Push a new counter scope for an element
 * Call when entering an element during layout
 */
void counter_push_scope(CounterContext* ctx);

/**
 * Pop the current counter scope
 * Call when leaving an element during layout
 */
void counter_pop_scope(CounterContext* ctx);

// ============================================================================
// Counter Operations
// ============================================================================

/**
 * Reset counter(s) - implements counter-reset property
 * @param ctx Counter context
 * @param counter_spec String like "chapter 0 section 1" or "none"
 */
void counter_reset(CounterContext* ctx, const char* counter_spec);

/**
 * Increment counter(s) - implements counter-increment property
 * @param ctx Counter context
 * @param counter_spec String like "chapter 1 section 2" or "none"
 */
void counter_increment(CounterContext* ctx, const char* counter_spec);

/**
 * Get current value of a counter
 * @param ctx Counter context
 * @param name Counter name
 * @return Current counter value, or 0 if counter doesn't exist
 */
int counter_get_value(CounterContext* ctx, const char* name);

/**
 * Get all values of a counter in nested scopes (for counters() function)
 * @param ctx Counter context
 * @param name Counter name
 * @param values Output array for counter values (caller must free)
 * @param count Output count of values
 */
void counter_get_all_values(CounterContext* ctx, const char* name, int** values, int* count);

// ============================================================================
// Counter Formatting
// ============================================================================

/**
 * Format counter value according to list-style-type
 * @param value Counter value
 * @param style List style (CSS_VALUE_DECIMAL, CSS_VALUE_LOWER_ROMAN, etc.)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int counter_format_value(int value, uint32_t style, char* buffer, size_t buffer_size);

/**
 * Format counter() function result
 * @param ctx Counter context
 * @param name Counter name
 * @param style List style
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                   char* buffer, size_t buffer_size);

/**
 * Format counters() function result (all nested values with separator)
 * @param ctx Counter context
 * @param name Counter name
 * @param separator Separator string (e.g., ".")
 * @param style List style
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int counters_format(CounterContext* ctx, const char* name, const char* separator,
                    uint32_t style, char* buffer, size_t buffer_size);

#endif // LAYOUT_COUNTERS_HPP
