#include "layout.hpp"
#include "../lambda/input/css/css_counter_hook.h"
#include "../lib/arena.h"
#include "../lib/hashmap.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_value.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
// Counter HashMap Helpers

HASHMAP_DEFINE_STRKEY(counter, CounterValue, name)
// Counter Context Management

CounterContext* counter_context_create(Arena* arena) {
    if (!arena) return nullptr;

    CounterContext* ctx = (CounterContext*)arena_alloc(arena, sizeof(CounterContext));
    if (!ctx) return nullptr;

    if (!ctx->init(arena)) {
        return nullptr;
    }

    return ctx;
}

bool CounterContext::init(Arena* backing_arena) {
    if (!backing_arena) return false;

    arena = backing_arena;
    current_scope = nullptr;
    scope_stack = nullptr;
    frame_stack = nullptr;
    void* stack_mem = mem_alloc(sizeof(lam::ArrayList<CounterScope*>), MEM_CAT_LAYOUT);
    if (!stack_mem) return false;
    scope_stack = new (stack_mem) lam::ArrayList<CounterScope*>(MEM_CAT_LAYOUT, 16); // NEW_DELETE_OK: single audited construction of scope_stack inside CounterContext::init.
    void* frame_mem = mem_alloc(sizeof(lam::ArrayList<CounterFrame>), MEM_CAT_LAYOUT);
    if (!frame_mem) {
        scope_stack->~ArrayList<CounterScope*>();
        mem_free(scope_stack);
        scope_stack = nullptr;
        return false;
    }
    frame_stack = new (frame_mem) lam::ArrayList<CounterFrame>(MEM_CAT_LAYOUT, 16); // NEW_DELETE_OK: single audited construction of frame_stack inside CounterContext::init.
    // Create root scope
    push_scope();

    if (!current_scope) {
        destroy();
        return false;
    }
    return true;
}

void counter_context_destroy(CounterContext* ctx) {
    if (!ctx) return;
    ctx->destroy();
}

void CounterContext::destroy() {
    // Free hash maps in each scope
    if (scope_stack) {
        for (size_t i = 0; i < scope_stack->size(); i++) {
            CounterScope* scope = (*scope_stack)[i];
            if (scope && scope->counters) {
                hashmap_free(scope->counters);
            }
        }
        scope_stack->~ArrayList<CounterScope*>();
        mem_free(scope_stack);
        scope_stack = nullptr;
    }
    if (frame_stack) {
        frame_stack->~ArrayList<CounterFrame>();
        mem_free(frame_stack);
        frame_stack = nullptr;
    }
    current_scope = nullptr;

}

void counter_push_scope(CounterContext* ctx, bool pseudo_scope) {
    if (!ctx) return;
    ctx->push_scope(pseudo_scope);
}

void CounterContext::push_scope(bool pseudo_scope) {
    // Allocate new scope
    CounterScope* scope = (CounterScope*)arena_alloc(arena, sizeof(CounterScope));
    if (!scope) return;
    // Create hash map for counters in this scope
    scope->counters = counter_new(16);
    scope->parent = current_scope;
    scope->owner_depth = current_scope && frame_stack ? (int)frame_stack->size() : -1;
    scope->pseudo_scope = pseudo_scope;
    scope->reset_replaces_sibling = false;
    scope->pseudo_reset_for_descendants = false;
    // Keep all allocated scopes for destruction; frame_stack owns nesting boundaries.
    if (scope_stack) {
        scope_stack->append(scope);
    }

    if (!current_scope) {
        current_scope = scope;
        return;
    }

    if (frame_stack) {
        CounterFrame frame = {current_scope, scope};
        frame_stack->append(frame);
    }
    current_scope = scope;
}

void CounterContext::pop_scope() {
    if (!frame_stack || frame_stack->size() == 0) return;
    size_t index = frame_stack->size() - 1;
    CounterFrame frame = (*frame_stack)[index];
    frame_stack->remove(index);
    current_scope = frame.entry_scope;
}

void counter_pop_scope_propagate(CounterContext* ctx, bool propagate_resets,
                                 bool preserve_reset_scope) {
    if (!ctx || !ctx->scope_stack) return;
    ctx->pop_scope_propagate(propagate_resets, preserve_reset_scope);
}

void CounterContext::pop_scope_propagate(bool propagate_resets, bool preserve_reset_scope) {
    if (!frame_stack || frame_stack->size() == 0) return;

    size_t index = frame_stack->size() - 1;
    CounterFrame frame = (*frame_stack)[index];
    CounterScope* scope = frame.element_scope;
    CounterScope* entry = frame.entry_scope;
    bool has_reset = false;

    if (scope && scope->counters) {
        size_t iter = 0;
        void* item;
        while (hashmap_iter(scope->counters, &iter, &item)) {
            CounterValue* cv = (CounterValue*)item;
            if (cv->created_by_reset) {
                has_reset = true;
            }
        }

        // A counter created by increment/set without an inherited instance belongs
        // to this element's temporary scope; expose it to following siblings when
        // the element did not create a nested reset scope of its own.
        if (propagate_resets && !has_reset && entry && entry->counters) {
            iter = 0;
            while (hashmap_iter(scope->counters, &iter, &item)) {
                CounterValue* cv = (CounterValue*)item;
                if (cv->created_by_reset) continue;
                CounterValue search_key = {cv->name, 0, false, false};
                CounterValue* entry_cv = (CounterValue*)hashmap_get(entry->counters, &search_key);
                if (entry_cv) {
                    entry_cv->value = cv->value;
                    entry_cv->propagated = true;
                } else {
                    CounterValue propagated = {cv->name, cv->value, true, false};
                    hashmap_set(entry->counters, &propagated);
                }
            }
        }
    }

    // CSS Lists 3 §4.4.1: retain a generated counter scope only while it is the
    // preceding flattened-tree source for the originating element's children.
    bool preserve_scope = preserve_reset_scope ||
        (scope && (scope->reset_replaces_sibling ||
                   scope->pseudo_reset_for_descendants));
    current_scope = (propagate_resets && has_reset && preserve_scope) ? scope : entry;
    frame_stack->remove(index);
}
// Counter Parsing Helpers

/**
 * Parse counter specification string like "chapter 0 section 1"
 * Returns array of name-value pairs
 */
static void parse_counter_spec(const char* spec,
                               char*** names_out, int** values_out, int* count_out,
                               Arena* arena, int default_value = 0) {
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
        if (str_char_is_ascii_space(*p)) {
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
    int* values = (int*)arena_alloc(arena, sizeof(int) * max_pairs); // INT_CAST_OK: pointer cast

    if (!names || !values) return;
    // Parse name-value pairs
    int pair_count = 0;
    p = spec;

    while (*p && pair_count < max_pairs) {
        // Skip whitespace
        while (*p && str_char_is_ascii_space(*p)) p++;
        if (!*p) break;
        // Parse name: read until whitespace (CSS <custom-ident> can contain hyphens, underscores, digits)
        const char* name_start = p;
        while (*p && !str_char_is_ascii_space(*p)) p++;

        if (p == name_start) break;
        // verify it looks like a CSS identifier (starts with letter, underscore, or hyphen)
        if (!str_char_is_alpha(name_start[0]) && name_start[0] != '_' && name_start[0] != '-') break;

        size_t name_len = p - name_start;
        char* name = (char*)arena_alloc(arena, name_len + 1);
        if (!name) break;

        memcpy(name, name_start, name_len);
        name[name_len] = '\0';
        // Skip whitespace
        while (*p && str_char_is_ascii_space(*p)) p++;
        // Parse optional integer value (sign must be followed by digit)
        int value = default_value;
        if (*p && (str_char_is_digit(*p) || ((*p == '-' || *p == '+') && *(p+1) && str_char_is_digit(*(p+1))))) {
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
// Counter Operations

static CounterValue* counter_find(CounterScope* scope, CounterValue* search_key) {
    while (scope) {
        CounterValue* counter = (CounterValue*)hashmap_get(scope->counters, search_key);
        if (counter) return counter;
        scope = scope->parent;
    }
    return nullptr;
}

static CounterScope* counter_find_scope(CounterScope* scope,
                                        CounterValue* search_key) {
    while (scope) {
        if (hashmap_get(scope->counters, search_key)) return scope;
        scope = scope->parent;
    }
    return nullptr;
}

static void counter_create(CounterScope* scope, char* name, int value,
                           bool created_by_reset) {
    CounterValue counter = {name, value, false, created_by_reset};
    hashmap_set(scope->counters, &counter);
}

struct ParsedCounterSpec {
    char** names;
    int* values;
    int count;
};

static ParsedCounterSpec counter_parse(CounterContext* ctx, const char* spec,
                                       int default_value = 0) {
    ParsedCounterSpec parsed = {nullptr, nullptr, 0};
    parse_counter_spec(spec, &parsed.names, &parsed.values, &parsed.count,
                       ctx->arena, default_value);
    return parsed;
}

void counter_reset(CounterContext* ctx, const char* counter_spec) {
    if (!ctx || !ctx->current_scope || !counter_spec) return;

    ParsedCounterSpec parsed = counter_parse(ctx, counter_spec);

    for (int i = 0; i < parsed.count; i++) {
        // Create or update counter in current scope
        CounterValue search_key = {parsed.names[i], 0, false, false};
        CounterValue* existing = (CounterValue*)hashmap_get(ctx->current_scope->counters, &search_key);

        if (!existing) {
            CounterScope* inherited_scope = nullptr;
            if (ctx->current_scope->pseudo_scope) {
                inherited_scope = counter_find_scope(ctx->current_scope->parent,
                                                     &search_key);
            }
            CounterScope* previous_sibling = ctx->current_scope->parent;
            CounterValue* previous_value = previous_sibling && previous_sibling->counters
                ? (CounterValue*)hashmap_get(previous_sibling->counters, &search_key)
                : nullptr;
            if (previous_value &&
                (previous_value->propagated ||
                 previous_sibling->owner_depth == ctx->current_scope->owner_depth)) {
                // CSS Lists 3 §4.4.2: a reset replaces a preceding-sibling
                // instance, but must leave an ancestor-created instance intact.
                hashmap_delete(previous_sibling->counters, &search_key);
                ctx->current_scope->reset_replaces_sibling = true;
            }
            // Create new counter
            counter_create(ctx->current_scope, parsed.names[i], parsed.values[i], true);
            // CSS Lists 3 §4.4.1: a pseudo reset remains the value source for
            // descendants only when its originating element inherited that
            // counter from the preceding flattened-tree sibling.
            // the active parent scope is the preceding sibling when the lookup
            // lands exactly one scope below the pseudo's originating element.
            bool retain_pseudo_reset = ctx->current_scope->pseudo_scope &&
                inherited_scope && ctx->current_scope->parent &&
                inherited_scope == ctx->current_scope->parent->parent;
            if (retain_pseudo_reset) {
                ctx->current_scope->pseudo_reset_for_descendants = true;
            }
        } else {
            // Update existing counter value
            existing->value = parsed.values[i];
        }
    }
}

void counter_increment(CounterContext* ctx, const char* counter_spec) {
    if (!ctx || !ctx->current_scope || !counter_spec) return;

    ParsedCounterSpec parsed = counter_parse(ctx, counter_spec, 1);

    for (int i = 0; i < parsed.count; i++) {

        int increment = parsed.values[i];
        // CSS Lists 3 §4.2/§4.4.1: use the inherited counter instance when the
        // element has not created a nearer reset scope.
        CounterValue search_key = {parsed.names[i], 0, false, false};
        CounterValue* cv = counter_find(ctx->current_scope, &search_key);

        if (!cv) {
            counter_create(ctx->current_scope, parsed.names[i], increment, false);
        } else {
            cv->value += increment;
        }
    }
}

static void counter_set_parsed(CounterContext* ctx, ParsedCounterSpec parsed) {
    for (int i = 0; i < parsed.count; i++) {
        // CSS Lists 3 §5.2: counter-set sets the value of the innermost counter
        // of the given name. If no counter of the given name exists on the element,
        // a new counter is created with the specified value.
        // Unlike counter-reset, this does NOT create a new scope.
        CounterValue search_key = {parsed.names[i], 0, false, false};
        CounterValue* cv = counter_find(ctx->current_scope, &search_key);

        if (cv) {
            // set existing counter to specified value
            cv->value = parsed.values[i];
        } else {
            // create new counter in current scope with specified value
            counter_create(ctx->current_scope, parsed.names[i], parsed.values[i], false);
        }
    }
}

void counter_set(CounterContext* ctx, const char* counter_spec) {
    if (!ctx || !ctx->current_scope || !counter_spec) return;
    counter_set_parsed(ctx, counter_parse(ctx, counter_spec));
}

int counter_get_value(CounterContext* ctx, const char* name) {
    if (!ctx || !ctx->current_scope || !name) return 0;
    CounterValue search_key = {name, 0, false, false};
    CounterValue* cv = counter_find(ctx->current_scope, &search_key);
    return cv ? cv->value : 0;
}

void counter_get_all_values(CounterContext* ctx, const char* name, int** values, int* count) {
    if (!ctx || !ctx->current_scope || !name || !values || !count) return;

    *values = nullptr;
    *count = 0;

    CounterValue search_key = {name, 0, false, false};
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
    *values = (int*)arena_alloc(ctx->arena, sizeof(int) * counter_count); // INT_CAST_OK: pointer cast
    if (!*values) return;
    // Collect values from outermost to innermost
    int* temp = (int*)mem_alloc(sizeof(int) * counter_count, MEM_CAT_LAYOUT); // INT_CAST_OK: pointer cast
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
    memcpy(*values, temp, sizeof(int) * counter_count); // INT_CAST_OK: size comparison
    mem_free(temp);

    *count = counter_count;
}
// Counter Formatting

static int format_roman_counter(int value, char* buffer, size_t buffer_size,
                                bool uppercase) {
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

    if (uppercase) str_upper_inplace(buffer, len);
    return len;
}

static int format_latin_counter(int value, char* buffer, size_t buffer_size,
                                bool uppercase) {
    if (value <= 0 || buffer_size < 10) {
        return snprintf(buffer, buffer_size, "%d", value);
    }

    int len = 0;
    value--;  // Convert to 0-based

    do {
        buffer[len++] = 'a' + (value % 26);
        value = value / 26 - 1;
    } while (value >= 0 && len < (int)buffer_size - 1); // INT_CAST_OK: size comparison
    // Reverse the string
    for (int i = 0; i < len / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[len - 1 - i];
        buffer[len - 1 - i] = temp;
    }

    buffer[len] = '\0';
    if (uppercase) str_upper_inplace(buffer, len);
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
    for (int i = temp_len - 2; i >= 0 && len < (int)buffer_size - 2; i -= 2) { // INT_CAST_OK: size comparison
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
            if (len < (int)buffer_size - 2) { // INT_CAST_OK: size comparison
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
    // Georgian Mkhedruli letters for additive numeral system (non-sequential codepoints)
    //                          0       1       2       3       4       5       6       7       8       9
    static const int geo_ones[]      = {0, 0x10D0, 0x10D1, 0x10D2, 0x10D3, 0x10D4, 0x10D5, 0x10D6, 0x10F1, 0x10D7};
    static const int geo_tens[]      = {0, 0x10D8, 0x10D9, 0x10DA, 0x10DB, 0x10DC, 0x10F2, 0x10DD, 0x10DE, 0x10DF};
    static const int geo_hundreds[]  = {0, 0x10E0, 0x10E1, 0x10E2, 0x10F3, 0x10E4, 0x10E5, 0x10E6, 0x10E7, 0x10E8};
    static const int geo_thousands[] = {0, 0x10E9, 0x10EA, 0x10EB, 0x10EC, 0x10ED, 0x10EE, 0x10F4, 0x10EF, 0x10F0};

    int len = 0;
    // handle 10000 prefix (ჵ = U+10F5)
    if (value >= 10000) {
        if (len < (int)buffer_size - 3) { // INT_CAST_OK: size comparison
            buffer[len++] = (char)(0xE1);
            buffer[len++] = (char)(0x80 + ((0x10F5 >> 6) & 0x3F));
            buffer[len++] = (char)(0x80 + (0x10F5 & 0x3F));
        }
        value -= 10000;
    }

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
            if (len < (int)buffer_size - 3) { // INT_CAST_OK: size comparison
                buffer[len++] = (char)(0xE1);
                buffer[len++] = (char)(0x80 + ((cp >> 6) & 0x3F));
                buffer[len++] = (char)(0x80 + (cp & 0x3F));
            }
        }
    }
    buffer[len] = '\0';
    return len;
}

static int format_bullet_counter(uint32_t style, char* buffer, size_t buffer_size) {
    static const unsigned char bullets[][3] = {
        {0xE2, 0x80, 0xA2}, // disc
        {0xE2, 0x97, 0xA6}, // circle
        {0xE2, 0x96, 0xA0}  // square
    };
    int index = style == CSS_VALUE_DISC ? 0
        : style == CSS_VALUE_CIRCLE ? 1
        : style == CSS_VALUE_SQUARE ? 2 : -1;
    if (index < 0 || buffer_size < 4) return 0;
    memcpy(buffer, bullets[index], 3);
    buffer[3] = '\0';
    return 3;
}

int counter_format_value(int value, uint32_t style, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return 0;

    switch (style) {
        case CSS_VALUE_NONE:
            buffer[0] = '\0';
            return 0;

        case CSS_VALUE_DISC:
        case CSS_VALUE_CIRCLE:
        case CSS_VALUE_SQUARE:
            return format_bullet_counter(style, buffer, buffer_size);

        case CSS_VALUE_LOWER_ROMAN:
            return format_roman_counter(value, buffer, buffer_size, false);

        case CSS_VALUE_UPPER_ROMAN:
            return format_roman_counter(value, buffer, buffer_size, true);

        case CSS_VALUE_LOWER_ALPHA:
        case CSS_VALUE_LOWER_LATIN:
            return format_latin_counter(value, buffer, buffer_size, false);

        case CSS_VALUE_UPPER_ALPHA:
        case CSS_VALUE_UPPER_LATIN:
            return format_latin_counter(value, buffer, buffer_size, true);

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

static int radiant_css_counter_format(void* counter_context, const char* name,
                                      uint32_t style, char* buffer, size_t buffer_size) {
    return counter_format((CounterContext*)counter_context, name, style, buffer, buffer_size);
}

static int radiant_css_counters_format(void* counter_context, const char* name,
                                       const char* separator, uint32_t style,
                                       char* buffer, size_t buffer_size) {
    return counters_format((CounterContext*)counter_context, name, separator, style,
                           buffer, buffer_size);
}

void radiant_register_css_counter_hooks() {
    css_counter_format_register(radiant_css_counter_format, radiant_css_counters_format);
}
