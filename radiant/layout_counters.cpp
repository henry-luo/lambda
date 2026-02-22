#include "layout_counters.hpp"
#include "../lib/arena.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_value.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

// ============================================================================
// Counter HashMap Helpers
// ============================================================================

// Hash function for counter names (strings)
static uint64_t counter_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const CounterValue* val = (const CounterValue*)item;
    return hashmap_sip(val->name, strlen(val->name), seed0, seed1);
}

// Compare function for counter names
static int counter_compare(const void* a, const void* b, void* udata) {
    const CounterValue* val_a = (const CounterValue*)a;
    const CounterValue* val_b = (const CounterValue*)b;
    return strcmp(val_a->name, val_b->name);
}

// ============================================================================
// Counter Context Management
// ============================================================================

CounterContext* counter_context_create(Arena* arena) {
    if (!arena) return nullptr;

    CounterContext* ctx = (CounterContext*)arena_alloc(arena, sizeof(CounterContext));
    if (!ctx) return nullptr;

    ctx->arena = arena;
    ctx->current_scope = nullptr;
    ctx->scope_stack = arraylist_new(16);

    // Create root scope
    counter_push_scope(ctx);

    log_debug("[Counters] Created counter context");
    return ctx;
}

void counter_context_destroy(CounterContext* ctx) {
    if (!ctx) return;

    // Free hash maps in each scope
    if (ctx->scope_stack) {
        for (int i = 0; i < ctx->scope_stack->length; i++) {
            CounterScope* scope = (CounterScope*)ctx->scope_stack->data[i];
            if (scope && scope->counters) {
                hashmap_free(scope->counters);
            }
        }
        arraylist_free(ctx->scope_stack);
    }

    log_debug("[Counters] Destroyed counter context");
}

void counter_push_scope(CounterContext* ctx) {
    if (!ctx) return;

    // Allocate new scope
    CounterScope* scope = (CounterScope*)arena_alloc(ctx->arena, sizeof(CounterScope));
    if (!scope) return;

    // Create hash map for counters in this scope
    scope->counters = hashmap_new(sizeof(CounterValue), 16, 0, 0,
                                  counter_hash, counter_compare, NULL, NULL);
    scope->parent = ctx->current_scope;

    // Push onto stack
    if (ctx->scope_stack) {
        arraylist_append(ctx->scope_stack, scope);
    }

    ctx->current_scope = scope;
}

void counter_pop_scope(CounterContext* ctx) {
    if (!ctx || !ctx->scope_stack) return;

    int size = ctx->scope_stack->length;
    if (size <= 1) {
        // Don't pop root scope
        return;
    }

    // Free the hash map before removing
    CounterScope* scope = (CounterScope*)ctx->scope_stack->data[size - 1];
    if (scope && scope->counters) {
        hashmap_free(scope->counters);
    }

    // Pop from stack
    arraylist_remove(ctx->scope_stack, size - 1);

    // Update current scope to parent
    if (size > 1) {
        ctx->current_scope = (CounterScope*)ctx->scope_stack->data[size - 2];
    } else {
        ctx->current_scope = nullptr;
    }
}

// ============================================================================
// Counter Parsing Helpers
// ============================================================================

/**
 * Parse counter specification string like "chapter 0 section 1"
 * Returns array of name-value pairs
 */
static void parse_counter_spec(const char* spec,
                               char*** names_out, int** values_out, int* count_out,
                               Arena* arena) {
    if (!spec || !names_out || !values_out || !count_out) return;

    *names_out = nullptr;
    *values_out = nullptr;
    *count_out = 0;

    // Check for "none"
    if (strcmp(spec, "none") == 0) {
        return;
    }

    // Count tokens
    int token_count = 0;
    const char* p = spec;
    bool in_token = false;
    while (*p) {
        if (isspace(*p)) {
            in_token = false;
        } else if (!in_token) {
            token_count++;
            in_token = true;
        }
        p++;
    }

    if (token_count == 0) return;

    // Allocate arrays (max possible pairs)
    int max_pairs = (token_count + 1) / 2;
    char** names = (char**)arena_alloc(arena, sizeof(char*) * max_pairs);
    int* values = (int*)arena_alloc(arena, sizeof(int) * max_pairs);

    if (!names || !values) return;

    // Parse name-value pairs
    int pair_count = 0;
    p = spec;

    while (*p && pair_count < max_pairs) {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        // Parse name
        const char* name_start = p;
        while (*p && !isspace(*p) && !isdigit(*p) && *p != '-' && *p != '+') p++;

        if (p == name_start) break;

        size_t name_len = p - name_start;
        char* name = (char*)arena_alloc(arena, name_len + 1);
        if (!name) break;

        memcpy(name, name_start, name_len);
        name[name_len] = '\0';

        // Skip whitespace
        while (*p && isspace(*p)) p++;

        // Parse value (optional, defaults to 0 for reset, 1 for increment)
        int value = 0;
        if (*p && (isdigit(*p) || *p == '-' || *p == '+')) {
            char* endptr = nullptr;
            long long_value = strtol(p, &endptr, 10);

            // Check for overflow/underflow
            if (long_value > INT_MAX) {
                value = INT_MAX;
            } else if (long_value < INT_MIN) {
                value = INT_MIN;
            } else {
                value = (int)long_value;
            }

            // Move pointer past the parsed number
            if (endptr > p) {
                p = endptr;
            } else {
                // Skip manually if strtol failed
                if (*p == '-' || *p == '+') p++;
                while (*p && isdigit(*p)) p++;
            }
        }

        names[pair_count] = name;
        values[pair_count] = value;
        pair_count++;
    }

    *names_out = names;
    *values_out = values;
    *count_out = pair_count;
}

// ============================================================================
// Counter Operations
// ============================================================================

void counter_reset(CounterContext* ctx, const char* counter_spec) {
    if (!ctx || !ctx->current_scope || !counter_spec) return;

    log_debug("[Counters] counter-reset: %s", counter_spec);

    char** names = nullptr;
    int* values = nullptr;
    int count = 0;

    parse_counter_spec(counter_spec, &names, &values, &count, ctx->arena);

    for (int i = 0; i < count; i++) {
        // Create or update counter in current scope
        CounterValue search_key = {names[i], 0};
        CounterValue* existing = (CounterValue*)hashmap_get(ctx->current_scope->counters, &search_key);

        if (!existing) {
            // Create new counter
            CounterValue new_counter;
            new_counter.name = names[i];
            new_counter.value = values[i];

            hashmap_set(ctx->current_scope->counters, &new_counter);
            log_debug("[Counters]   Reset '%s' = %d (new)", names[i], values[i]);
        } else {
            // Update existing counter value
            existing->value = values[i];
            log_debug("[Counters]   Reset '%s' = %d (existing)", names[i], values[i]);
        }
    }
}

void counter_increment(CounterContext* ctx, const char* counter_spec) {
    if (!ctx || !ctx->current_scope || !counter_spec) return;

    log_debug("[Counters] counter-increment: %s", counter_spec);

    char** names = nullptr;
    int* values = nullptr;
    int count = 0;

    parse_counter_spec(counter_spec, &names, &values, &count, ctx->arena);

    log_debug("[Counters] counter-increment parsed: count=%d", count);

    for (int i = 0; i < count; i++) {
        log_debug("[Counters]   Processing counter[%d]: name=%s, value=%d", i, names[i], values[i]);

        int increment = (values[i] != 0) ? values[i] : 1;  // Default increment is 1

        // Search for counter in current and parent scopes
        CounterValue search_key = {names[i], 0};
        CounterValue* cv = nullptr;
        CounterScope* scope = ctx->current_scope;

        while (scope && !cv) {
            cv = (CounterValue*)hashmap_get(scope->counters, &search_key);
            if (cv) break;
            scope = scope->parent;
        }

        if (!cv) {
            // Counter doesn't exist - create it in current scope with value 0 + increment
            CounterValue new_counter;
            new_counter.name = names[i];
            new_counter.value = increment;

            hashmap_set(ctx->current_scope->counters, &new_counter);
            log_debug("[Counters]   Increment '%s' by %d = %d (new)", names[i], increment, increment);
        } else {
            cv->value += increment;
            log_debug("[Counters]   Increment '%s' by %d = %d", names[i], increment, cv->value);
        }
    }
}

int counter_get_value(CounterContext* ctx, const char* name) {
    if (!ctx || !ctx->current_scope || !name) return 0;

    // Search for counter in current and parent scopes
    CounterScope* scope = ctx->current_scope;
    CounterValue search_key = {name, 0};

    while (scope) {
        CounterValue* cv = (CounterValue*)hashmap_get(scope->counters, &search_key);
        if (cv) {
            return cv->value;
        }
        scope = scope->parent;
    }

    return 0;  // Counter not found, return 0
}

void counter_get_all_values(CounterContext* ctx, const char* name, int** values, int* count) {
    if (!ctx || !ctx->current_scope || !name || !values || !count) return;

    *values = nullptr;
    *count = 0;

    CounterValue search_key = {name, 0};

    // Count how many counters with this name exist in the scope chain
    int counter_count = 0;
    CounterScope* scope = ctx->current_scope;
    while (scope) {
        if (hashmap_get(scope->counters, &search_key)) {
            counter_count++;
        }
        scope = scope->parent;
    }

    if (counter_count == 0) return;

    // Allocate array (from innermost to innermost)
    *values = (int*)arena_alloc(ctx->arena, sizeof(int) * counter_count);
    if (!*values) return;

    // Collect values from outermost to innermost
    int* temp = (int*)mem_alloc(sizeof(int) * counter_count, MEM_CAT_LAYOUT);
    int idx = 0;

    scope = ctx->current_scope;
    while (scope && idx < counter_count) {
        CounterValue* cv = (CounterValue*)hashmap_get(scope->counters, &search_key);
        if (cv) {
            temp[counter_count - 1 - idx] = cv->value;
            idx++;
        }
        scope = scope->parent;
    }

    // Copy to output array
    memcpy(*values, temp, sizeof(int) * counter_count);
    mem_free(temp);

    *count = counter_count;
}

// ============================================================================
// Counter Formatting
// ============================================================================

/**
 * Convert integer to lowercase roman numerals
 */
static int int_to_lower_roman(int value, char* buffer, size_t buffer_size) {
    if (value <= 0 || value >= 4000 || buffer_size < 20) {
        return snprintf(buffer, buffer_size, "%d", value);
    }

    const char* ones[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"};
    const char* tens[] = {"", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"};
    const char* hundreds[] = {"", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"};
    const char* thousands[] = {"", "m", "mm", "mmm"};

    int len = 0;
    len += snprintf(buffer + len, buffer_size - len, "%s", thousands[value / 1000]);
    len += snprintf(buffer + len, buffer_size - len, "%s", hundreds[(value % 1000) / 100]);
    len += snprintf(buffer + len, buffer_size - len, "%s", tens[(value % 100) / 10]);
    len += snprintf(buffer + len, buffer_size - len, "%s", ones[value % 10]);

    return len;
}

/**
 * Convert integer to uppercase roman numerals
 */
static int int_to_upper_roman(int value, char* buffer, size_t buffer_size) {
    int len = int_to_lower_roman(value, buffer, buffer_size);

    // Convert to uppercase
    str_upper_inplace(buffer, len);

    return len;
}

/**
 * Convert integer to lowercase latin letters (a, b, c, ..., z, aa, ab, ...)
 */
static int int_to_lower_latin(int value, char* buffer, size_t buffer_size) {
    if (value <= 0 || buffer_size < 10) {
        return snprintf(buffer, buffer_size, "%d", value);
    }

    int len = 0;
    value--;  // Convert to 0-based

    do {
        buffer[len++] = 'a' + (value % 26);
        value = value / 26 - 1;
    } while (value >= 0 && len < (int)buffer_size - 1);

    // Reverse the string
    for (int i = 0; i < len / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[len - 1 - i];
        buffer[len - 1 - i] = temp;
    }

    buffer[len] = '\0';
    return len;
}

/**
 * Convert integer to uppercase latin letters
 */
static int int_to_upper_latin(int value, char* buffer, size_t buffer_size) {
    int len = int_to_lower_latin(value, buffer, buffer_size);

    // Convert to uppercase
    str_upper_inplace(buffer, len);

    return len;
}

/**
 * Convert integer to lower-greek letters (α, β, γ, δ, ε, ζ, η, θ, ι, κ, λ, μ, ν, ξ, ο, π, ρ, σ, τ, υ, φ, χ, ψ, ω)
 * CSS 2.1: alphabetic system using Greek lowercase letters
 */
static int int_to_lower_greek(int value, char* buffer, size_t buffer_size) {
    // Greek lowercase alpha=U+03B1 through omega=U+03C9 (24 letters, skipping U+03C2 final sigma)
    static const int greek_letters[] = {
        0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7, 0x03B8,
        0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BE, 0x03BF, 0x03C0,
        0x03C1, 0x03C3, 0x03C4, 0x03C5, 0x03C6, 0x03C7, 0x03C8, 0x03C9
    };
    const int count = 24;

    if (value <= 0 || buffer_size < 10) {
        return snprintf(buffer, buffer_size, "%d", value);
    }

    // Alphabetic numbering: 1=α, 2=β, ..., 24=ω, 25=αα, ...
    char temp[64];
    int temp_len = 0;
    value--;
    do {
        int idx = value % count;
        // Encode greek letter as UTF-8 (2 bytes for U+03xx)
        temp[temp_len++] = (char)(0xCE + (greek_letters[idx] >= 0x03C0 ? 1 : 0));
        temp[temp_len++] = (char)(0x80 + (greek_letters[idx] & 0x3F));
        value = value / count - 1;
    } while (value >= 0 && temp_len < (int)sizeof(temp) - 2);

    // Reverse pairs
    int len = 0;
    for (int i = temp_len - 2; i >= 0 && len < (int)buffer_size - 2; i -= 2) {
        buffer[len++] = temp[i];
        buffer[len++] = temp[i + 1];
    }
    buffer[len] = '\0';
    return len;
}

/**
 * Convert integer to Armenian traditional numbering.
 * CSS 2.1: Armenian additive system for 1-9999.
 */
static int int_to_armenian(int value, char* buffer, size_t buffer_size) {
    if (value <= 0 || value > 9999 || buffer_size < 20) {
        return snprintf(buffer, buffer_size, "%d", value);
    }
    // Armenian uppercase letters for thousands, hundreds, tens, ones
    // U+0531-U+0554 (Ա-Ք)
    static const int thousands[] = {0, 0x0531+35, 0x0531+36, 0x0531+37, 0x0531+38, 0x0531+39, 0x0531+40, 0x0531+41, 0x0531+42};
    // 1000=Ռ(0x0550), 2000=Ս(0x0551), 3000=Վ(0x0552), 4000=Տ(0x0553), 5000=Ր(0x0550+4), 6000=Ց(0x0551+4), 7000=Ւ(0x0552+4), 8000=Փ(0x0553+4), 9000=Ք(0x0554)
    static const int armenian_ones[] = {0, 0x0531, 0x0532, 0x0533, 0x0534, 0x0535, 0x0536, 0x0537, 0x0538, 0x0539};
    static const int armenian_tens[] = {0, 0x053A, 0x053B, 0x053C, 0x053D, 0x053E, 0x053F, 0x0540, 0x0541, 0x0542};
    static const int armenian_hundreds[] = {0, 0x0543, 0x0544, 0x0545, 0x0546, 0x0547, 0x0548, 0x0549, 0x054A, 0x054B};
    static const int armenian_thousands[] = {0, 0x054C, 0x054D, 0x054E, 0x054F, 0x0550, 0x0551, 0x0552, 0x0553, 0x0554};

    int len = 0;
    int digits[] = { value / 1000, (value / 100) % 10, (value / 10) % 10, value % 10 };
    const int* tables[] = { armenian_thousands, armenian_hundreds, armenian_tens, armenian_ones };
    (void)thousands; (void)armenian_ones; // suppress unused warnings done through tables

    for (int i = 0; i < 4; i++) {
        if (digits[i] > 0 && digits[i] <= 9) {
            int cp = tables[i][digits[i]];
            // encode as UTF-8 (2 bytes for U+05xx range)
            if (len < (int)buffer_size - 2) {
                buffer[len++] = (char)(0xD4 + (cp >= 0x0540 ? 1 : 0));
                buffer[len++] = (char)(0x80 + (cp & 0x3F));
            }
        }
    }
    buffer[len] = '\0';
    return len;
}

/**
 * Convert integer to Georgian traditional numbering.
 * CSS 2.1: Georgian additive system for 1-19999.
 */
static int int_to_georgian(int value, char* buffer, size_t buffer_size) {
    if (value <= 0 || value > 19999 || buffer_size < 20) {
        return snprintf(buffer, buffer_size, "%d", value);
    }
    // Georgian Mkhedruli letters for additive system
    static const int geo_ones[] = {0, 0x10D0, 0x10D1, 0x10D2, 0x10D3, 0x10D4, 0x10D5, 0x10D6, 0x10D7, 0x10D8};
    static const int geo_tens[] = {0, 0x10D9, 0x10DA, 0x10DB, 0x10DC, 0x10DD, 0x10DE, 0x10DF, 0x10E0, 0x10E1};
    static const int geo_hundreds[] = {0, 0x10E2, 0x10E3, 0x10E4, 0x10E5, 0x10E6, 0x10E7, 0x10E8, 0x10E9, 0x10EA};
    static const int geo_thousands[] = {0, 0x10EB, 0x10EC, 0x10ED, 0x10EE, 0x10EF, 0x10F0, 0x10F1, 0x10F2, 0x10F3};

    int len = 0;
    int th = value / 1000;
    int h = (value / 100) % 10;
    int t = (value / 10) % 10;
    int o = value % 10;

    int parts[] = { th, h, t, o };
    const int* tables[] = { geo_thousands, geo_hundreds, geo_tens, geo_ones };

    for (int i = 0; i < 4; i++) {
        if (parts[i] > 0 && parts[i] <= 9) {
            int cp = tables[i][parts[i]];
            // encode as UTF-8 (3 bytes for U+10xx range)
            if (len < (int)buffer_size - 3) {
                buffer[len++] = (char)(0xE1);
                buffer[len++] = (char)(0x80 + ((cp >> 6) & 0x3F));
                buffer[len++] = (char)(0x80 + (cp & 0x3F));
            }
        }
    }
    buffer[len] = '\0';
    return len;
}

int counter_format_value(int value, uint32_t style, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return 0;

    switch (style) {
        case CSS_VALUE_NONE:
            buffer[0] = '\0';
            return 0;

        case CSS_VALUE_DISC: // bullet point "•"
            if (buffer_size >= 4) {
                buffer[0] = '\xE2';
                buffer[1] = '\x80';
                buffer[2] = '\xA2';
                buffer[3] = '\0';
                return 3;
            }
            return 0;

        case CSS_VALUE_CIRCLE: // white circle "◦"
            if (buffer_size >= 4) {
                buffer[0] = '\xE2';
                buffer[1] = '\x97';
                buffer[2] = '\xA6';
                buffer[3] = '\0';
                return 3;
            }
            return 0;

        case CSS_VALUE_SQUARE: // black square "▪"
            if (buffer_size >= 4) {
                buffer[0] = '\xE2';
                buffer[1] = '\x96';
                buffer[2] = '\xAA';
                buffer[3] = '\0';
                return 3;
            }
            return 0;

        case CSS_VALUE_LOWER_ROMAN:
            return int_to_lower_roman(value, buffer, buffer_size);

        case CSS_VALUE_UPPER_ROMAN:
            return int_to_upper_roman(value, buffer, buffer_size);

        case CSS_VALUE_LOWER_ALPHA:
        case CSS_VALUE_LOWER_LATIN:
            return int_to_lower_latin(value, buffer, buffer_size);

        case CSS_VALUE_UPPER_ALPHA:
        case CSS_VALUE_UPPER_LATIN:
            return int_to_upper_latin(value, buffer, buffer_size);

        case CSS_VALUE_DECIMAL_LEADING_ZERO:
            return snprintf(buffer, buffer_size, "%02d", value);

        case CSS_VALUE_LOWER_GREEK:
            return int_to_lower_greek(value, buffer, buffer_size);

        case CSS_VALUE_ARMENIAN:
            return int_to_armenian(value, buffer, buffer_size);

        case CSS_VALUE_GEORGIAN:
            return int_to_georgian(value, buffer, buffer_size);

        case CSS_VALUE_DECIMAL:
        default:
            return snprintf(buffer, buffer_size, "%d", value);
    }
}

int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                   char* buffer, size_t buffer_size) {
    if (!ctx || !name || !buffer || buffer_size == 0) return 0;

    int value = counter_get_value(ctx, name);
    return counter_format_value(value, style, buffer, buffer_size);
}

int counters_format(CounterContext* ctx, const char* name, const char* separator,
                    uint32_t style, char* buffer, size_t buffer_size) {
    if (!ctx || !name || !buffer || buffer_size == 0) return 0;

    int* values = nullptr;
    int count = 0;

    counter_get_all_values(ctx, name, &values, &count);

    if (count == 0) {
        // No counters found, return "0"
        return snprintf(buffer, buffer_size, "0");
    }

    const char* sep = separator ? separator : ".";
    int len = 0;

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            len += snprintf(buffer + len, buffer_size - len, "%s", sep);
        }
        len += counter_format_value(values[i], style, buffer + len, buffer_size - len);
    }

    return len;
}
