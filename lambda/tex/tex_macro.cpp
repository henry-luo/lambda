// tex_macro.cpp - TeX Macro Processor Implementation
//
// Implements macro expansion following TeXBook Chapter 20.
//
// Reference: TeXBook Chapter 20

#include "tex_macro.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>

namespace tex {

// Hash entry for storing macro definitions by name
struct MacroHashEntry {
    const char* name;
    MacroDef* def;
};

static uint64_t macro_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const MacroHashEntry* entry = (const MacroHashEntry*)item;
    return hashmap_sip(entry->name, strlen(entry->name), seed0, seed1);
}

static int macro_entry_compare(const void* a, const void* b, void* udata) {
    const MacroHashEntry* ea = (const MacroHashEntry*)a;
    const MacroHashEntry* eb = (const MacroHashEntry*)b;
    return strcmp(ea->name, eb->name);
}

// ============================================================================
// MacroProcessor Implementation
// ============================================================================

MacroProcessor::MacroProcessor(Arena* arena)
    : arena(arena)
    , macros(nullptr)
    , saved_macros(nullptr)
    , expansion_depth(0)
    , expansion_limit(1000)
{
    macros = hashmap_new(sizeof(MacroHashEntry), 32, 0, 0,
                         macro_entry_hash, macro_entry_compare,
                         nullptr, nullptr);
}

MacroProcessor::~MacroProcessor() {
    if (macros) {
        hashmap_free(macros);
    }
    if (saved_macros) {
        hashmap_free(saved_macros);
    }
}

// ============================================================================
// Macro Definition
// ============================================================================

MacroParam* MacroProcessor::parse_param_text(const char* param_text, size_t len, int* param_count) {
    // Count parameters (each #n)
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (param_text[i] == '#' && i + 1 < len && param_text[i + 1] >= '1' && param_text[i + 1] <= '9') {
            int num = param_text[i + 1] - '0';
            if (num > count) count = num;
            i++;  // skip the digit
        }
    }

    *param_count = count;
    if (count == 0) return nullptr;

    MacroParam* params = (MacroParam*)arena_alloc(arena, count * sizeof(MacroParam));

    // Initialize all as undelimited
    for (int p = 0; p < count; p++) {
        params[p].type = MacroParamType::Undelimited;
        params[p].delimiter = nullptr;
        params[p].delimiter_len = 0;
        params[p].default_value = nullptr;
        params[p].default_len = 0;
    }

    // Parse for delimiters
    // In "#1.#2" the first parameter is delimited by "."
    size_t i = 0;
    while (i < len) {
        if (param_text[i] == '#' && i + 1 < len && param_text[i + 1] >= '1' && param_text[i + 1] <= '9') {
            int param_num = param_text[i + 1] - '0' - 1;  // 0-indexed
            i += 2;

            // Check if there's a delimiter before next parameter or end
            size_t delim_start = i;
            while (i < len) {
                if (param_text[i] == '#' && i + 1 < len && param_text[i + 1] >= '1' && param_text[i + 1] <= '9') {
                    break;
                }
                i++;
            }

            if (i > delim_start && param_num >= 0 && param_num < count) {
                params[param_num].type = MacroParamType::Delimited;
                params[param_num].delimiter = param_text + delim_start;
                params[param_num].delimiter_len = i - delim_start;
            }
        } else {
            i++;
        }
    }

    return params;
}

bool MacroProcessor::define(const char* name, size_t name_len,
                            const char* param_text, size_t param_len,
                            const char* replacement, size_t repl_len) {
    MacroDef* def = (MacroDef*)arena_alloc(arena, sizeof(MacroDef));
    *def = MacroDef();

    // Copy name
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    def->name = name_copy;
    def->name_len = name_len;

    // Parse parameters
    def->params = parse_param_text(param_text, param_len, &def->param_count);

    // Copy replacement text
    char* repl_copy = (char*)arena_alloc(arena, repl_len + 1);
    memcpy(repl_copy, replacement, repl_len);
    repl_copy[repl_len] = '\0';
    def->replacement = repl_copy;
    def->replacement_len = repl_len;

    // Store in hash map
    MacroHashEntry entry = {name_copy, def};
    hashmap_set(macros, &entry);

    log_debug("macro: defined \\%s with %d params", name_copy, def->param_count);
    return true;
}

bool MacroProcessor::define_full(const MacroDef& def) {
    MacroDef* copy = (MacroDef*)arena_alloc(arena, sizeof(MacroDef));
    *copy = def;
    MacroHashEntry entry = {def.name, copy};
    hashmap_set(macros, &entry);
    return true;
}

bool MacroProcessor::newcommand(const char* name, size_t name_len,
                                int nargs,
                                const char* default_arg, size_t default_len,
                                const char* definition, size_t def_len) {
    // Check if already defined
    if (is_defined(name, name_len)) {
        log_error("macro: \\newcommand: \\%.*s already defined", (int)name_len, name);
        return false;
    }

    MacroDef* def = (MacroDef*)arena_alloc(arena, sizeof(MacroDef));
    *def = MacroDef();

    // Copy name
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    def->name = name_copy;
    def->name_len = name_len;

    // Set up parameters
    def->param_count = nargs;
    if (nargs > 0) {
        def->params = (MacroParam*)arena_alloc(arena, nargs * sizeof(MacroParam));
        for (int p = 0; p < nargs; p++) {
            def->params[p].type = MacroParamType::Undelimited;
            def->params[p].delimiter = nullptr;
            def->params[p].delimiter_len = 0;
            def->params[p].default_value = nullptr;
            def->params[p].default_len = 0;
        }

        // First parameter can have a default value
        if (default_arg && default_len > 0 && nargs >= 1) {
            def->params[0].type = MacroParamType::Optional;
            char* def_copy = (char*)arena_alloc(arena, default_len + 1);
            memcpy(def_copy, default_arg, default_len);
            def_copy[default_len] = '\0';
            def->params[0].default_value = def_copy;
            def->params[0].default_len = default_len;
        }
    }

    // Copy definition
    char* def_copy = (char*)arena_alloc(arena, def_len + 1);
    memcpy(def_copy, definition, def_len);
    def_copy[def_len] = '\0';
    def->replacement = def_copy;
    def->replacement_len = def_len;

    MacroHashEntry entry = {name_copy, def};
    hashmap_set(macros, &entry);
    log_debug("macro: \\newcommand{\\%s}[%d] defined", name_copy, nargs);
    return true;
}

bool MacroProcessor::renewcommand(const char* name, size_t name_len,
                                  int nargs,
                                  const char* default_arg, size_t default_len,
                                  const char* definition, size_t def_len) {
    // Remove existing definition
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    MacroHashEntry entry = {name_copy, nullptr};
    hashmap_delete(macros, &entry);

    // Define new
    return newcommand(name, name_len, nargs, default_arg, default_len, definition, def_len);
}

bool MacroProcessor::providecommand(const char* name, size_t name_len,
                                    int nargs,
                                    const char* default_arg, size_t default_len,
                                    const char* definition, size_t def_len) {
    // Only define if not already defined
    if (is_defined(name, name_len)) {
        return true;  // Silently succeed
    }
    return newcommand(name, name_len, nargs, default_arg, default_len, definition, def_len);
}

// ============================================================================
// Macro Lookup
// ============================================================================

bool MacroProcessor::is_defined(const char* name, size_t len) const {
    char* name_copy = (char*)alloca(len + 1);
    memcpy(name_copy, name, len);
    name_copy[len] = '\0';
    MacroHashEntry entry = {name_copy, nullptr};
    return hashmap_get(macros, &entry) != nullptr;
}

const MacroDef* MacroProcessor::get_macro(const char* name, size_t len) const {
    char* name_copy = (char*)alloca(len + 1);
    memcpy(name_copy, name, len);
    name_copy[len] = '\0';
    MacroHashEntry entry = {name_copy, nullptr};
    const MacroHashEntry* found = (const MacroHashEntry*)hashmap_get(macros, &entry);
    return found ? found->def : nullptr;
}

// ============================================================================
// Argument Matching
// ============================================================================

size_t MacroProcessor::match_argument(const char* input, size_t pos, size_t len,
                                      const MacroParam* param,
                                      const char** arg, size_t* arg_len) {
    // Skip leading whitespace
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n')) {
        pos++;
    }

    if (param->type == MacroParamType::Optional) {
        // Check for [optional]
        if (pos < len && input[pos] == '[') {
            pos++;  // skip '['
            size_t start = pos;
            int depth = 1;
            while (pos < len && depth > 0) {
                if (input[pos] == '[') depth++;
                else if (input[pos] == ']') depth--;
                if (depth > 0) pos++;
            }
            *arg = input + start;
            *arg_len = pos - start;
            if (pos < len) pos++;  // skip ']'
            return pos;
        } else {
            // Use default value
            *arg = param->default_value;
            *arg_len = param->default_len;
            return pos;
        }
    }

    if (param->type == MacroParamType::Undelimited) {
        // Single token or braced group
        if (pos < len && input[pos] == '{') {
            pos++;  // skip '{'
            size_t start = pos;
            int depth = 1;
            while (pos < len && depth > 0) {
                if (input[pos] == '{') depth++;
                else if (input[pos] == '}') depth--;
                if (depth > 0) pos++;
            }
            *arg = input + start;
            *arg_len = pos - start;
            if (pos < len) pos++;  // skip '}'
            return pos;
        } else {
            // Single token (non-space character)
            size_t start = pos;
            if (pos < len && input[pos] == '\\') {
                // Command name
                pos++;
                while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                     (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                    pos++;
                }
            } else if (pos < len) {
                pos++;  // single character
            }
            *arg = input + start;
            *arg_len = pos - start;
            return pos;
        }
    }

    if (param->type == MacroParamType::Delimited) {
        // Match up to delimiter
        size_t start = pos;
        while (pos + param->delimiter_len <= len) {
            if (strncmp(input + pos, param->delimiter, param->delimiter_len) == 0) {
                break;
            }
            // Skip braced groups
            if (input[pos] == '{') {
                int depth = 1;
                pos++;
                while (pos < len && depth > 0) {
                    if (input[pos] == '{') depth++;
                    else if (input[pos] == '}') depth--;
                    pos++;
                }
            } else {
                pos++;
            }
        }
        *arg = input + start;
        *arg_len = pos - start;
        // Skip the delimiter
        pos += param->delimiter_len;
        return pos;
    }

    return pos;
}

// ============================================================================
// Parameter Substitution
// ============================================================================

char* MacroProcessor::substitute_params(const MacroDef* macro,
                                        const char** args, size_t* arg_lens,
                                        size_t* out_len) {
    // Calculate output size
    size_t size = 0;
    for (size_t i = 0; i < macro->replacement_len; i++) {
        if (macro->replacement[i] == '#' && i + 1 < macro->replacement_len) {
            char next = macro->replacement[i + 1];
            if (next >= '1' && next <= '9') {
                int param_num = next - '1';
                if (param_num < macro->param_count) {
                    size += arg_lens[param_num];
                }
                i++;  // skip the digit
            } else if (next == '#') {
                size++;  // ## -> #
                i++;
            } else {
                size++;
            }
        } else {
            size++;
        }
    }

    char* result = (char*)arena_alloc(arena, size + 1);
    size_t pos = 0;

    // Do substitution
    for (size_t i = 0; i < macro->replacement_len; i++) {
        if (macro->replacement[i] == '#' && i + 1 < macro->replacement_len) {
            char next = macro->replacement[i + 1];
            if (next >= '1' && next <= '9') {
                int param_num = next - '1';
                if (param_num < macro->param_count) {
                    memcpy(result + pos, args[param_num], arg_lens[param_num]);
                    pos += arg_lens[param_num];
                }
                i++;
            } else if (next == '#') {
                result[pos++] = '#';
                i++;
            } else {
                result[pos++] = macro->replacement[i];
            }
        } else {
            result[pos++] = macro->replacement[i];
        }
    }

    result[pos] = '\0';
    *out_len = pos;
    return result;
}

// ============================================================================
// Expansion
// ============================================================================

size_t MacroProcessor::expand_one(const char* input, size_t pos, size_t len,
                                  char** out_result, size_t* out_result_len) {
    // Check for backslash
    if (pos >= len || input[pos] != '\\') {
        return 0;
    }

    size_t cmd_start = pos + 1;

    // Parse command name
    size_t cmd_end = cmd_start;
    while (cmd_end < len && ((input[cmd_end] >= 'a' && input[cmd_end] <= 'z') ||
                             (input[cmd_end] >= 'A' && input[cmd_end] <= 'Z'))) {
        cmd_end++;
    }

    if (cmd_end == cmd_start) {
        // Single-character command
        cmd_end = cmd_start + 1;
    }

    size_t cmd_len = cmd_end - cmd_start;

    // Look up macro
    const MacroDef* macro = get_macro(input + cmd_start, cmd_len);
    if (!macro) {
        return 0;  // Not a defined macro
    }

    // Parse arguments
    const char** args = nullptr;
    size_t* arg_lens = nullptr;
    size_t after_args = cmd_end;

    if (macro->param_count > 0) {
        args = (const char**)alloca(macro->param_count * sizeof(char*));
        arg_lens = (size_t*)alloca(macro->param_count * sizeof(size_t));

        for (int p = 0; p < macro->param_count; p++) {
            after_args = match_argument(input, after_args, len,
                                        &macro->params[p],
                                        &args[p], &arg_lens[p]);
        }
    }

    // Substitute parameters
    *out_result = substitute_params(macro, args, arg_lens, out_result_len);

    log_debug("macro: expanded \\%.*s to %zu chars", (int)cmd_len, input + cmd_start, *out_result_len);
    return after_args - pos;
}

// Internal recursive expansion helper that preserves depth
char* MacroProcessor::expand_recursive(const char* input, size_t len, size_t* out_len) {
    // Check depth limit at entry
    if (expansion_depth >= expansion_limit) {
        log_error("macro: expansion depth limit (%d) reached", expansion_limit);
        // Return copy of input without further expansion
        char* output = (char*)arena_alloc(arena, len + 1);
        memcpy(output, input, len);
        output[len] = '\0';
        *out_len = len;
        return output;
    }

    // Use a string buffer for growing result
    StrBuf* result = strbuf_new();

    size_t pos = 0;

    while (pos < len) {
        if (input[pos] == '\\') {
            char* expanded;
            size_t expanded_len;
            size_t consumed = expand_one(input, pos, len, &expanded, &expanded_len);

            if (consumed > 0) {
                // Recursively expand the result
                expansion_depth++;
                size_t re_expanded_len;
                char* re_expanded = expand_recursive(expanded, expanded_len, &re_expanded_len);
                strbuf_append_str_n(result, re_expanded, re_expanded_len);
                expansion_depth--;
                pos += consumed;
            } else {
                // Not a macro, copy literally
                strbuf_append_char(result, input[pos]);
                pos++;
            }
        } else {
            strbuf_append_char(result, input[pos]);
            pos++;
        }
    }

    // Copy result to arena
    size_t result_len = result->length;
    char* output = (char*)arena_alloc(arena, result_len + 1);
    memcpy(output, result->str, result_len);
    output[result_len] = '\0';

    strbuf_free(result);

    *out_len = result_len;
    return output;
}

char* MacroProcessor::expand(const char* input, size_t len, size_t* out_len) {
    // Reset depth for top-level calls
    expansion_depth = 0;
    return expand_recursive(input, len, out_len);
}

// ============================================================================
// Grouping
// ============================================================================

void MacroProcessor::begin_group() {
    // Save current macro definitions
    // For simplicity, just mark the depth
    // A full implementation would clone the hashmap
    log_debug("macro: begin group");
}

void MacroProcessor::end_group() {
    // Restore saved definitions
    log_debug("macro: end group");
}

// ============================================================================
// Debugging
// ============================================================================

void MacroProcessor::dump_macros() const {
    log_debug("macro: dumping all macros:");
    // Iterate hashmap and log each macro
    // (requires hashmap iteration support)
}

// ============================================================================
// Utility Functions
// ============================================================================

size_t parse_braced_argument(
    const char* input, size_t pos, size_t len,
    const char** content, size_t* content_len
) {
    // Skip whitespace
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n')) {
        pos++;
    }

    if (pos >= len || input[pos] != '{') {
        *content = nullptr;
        *content_len = 0;
        return pos;
    }

    pos++;  // skip '{'
    size_t start = pos;
    int depth = 1;

    while (pos < len && depth > 0) {
        if (input[pos] == '{') depth++;
        else if (input[pos] == '}') depth--;
        if (depth > 0) pos++;
    }

    *content = input + start;
    *content_len = pos - start;

    if (pos < len) pos++;  // skip '}'
    return pos;
}

size_t parse_optional_argument(
    const char* input, size_t pos, size_t len,
    const char** content, size_t* content_len
) {
    // Skip whitespace
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n')) {
        pos++;
    }

    if (pos >= len || input[pos] != '[') {
        *content = nullptr;
        *content_len = 0;
        return pos;
    }

    pos++;  // skip '['
    size_t start = pos;
    int depth = 1;

    while (pos < len && depth > 0) {
        if (input[pos] == '[') depth++;
        else if (input[pos] == ']') depth--;
        if (depth > 0) pos++;
    }

    *content = input + start;
    *content_len = pos - start;

    if (pos < len) pos++;  // skip ']'
    return pos;
}

size_t parse_macro_definition(
    const char* input, size_t pos, size_t len,
    MacroProcessor* processor
) {
    // Skip \def or \newcommand
    if (pos >= len || input[pos] != '\\') return pos;
    pos++;

    bool is_newcommand = false;
    bool is_renewcommand = false;
    bool is_providecommand = false;

    if (pos + 2 < len && strncmp(input + pos, "def", 3) == 0) {
        pos += 3;
    } else if (pos + 9 < len && strncmp(input + pos, "newcommand", 10) == 0) {
        pos += 10;
        is_newcommand = true;
    } else if (pos + 11 < len && strncmp(input + pos, "renewcommand", 12) == 0) {
        pos += 12;
        is_renewcommand = true;
    } else if (pos + 13 < len && strncmp(input + pos, "providecommand", 14) == 0) {
        pos += 14;
        is_providecommand = true;
    } else {
        return pos;  // Not a definition command
    }

    // Skip whitespace
    while (pos < len && input[pos] == ' ') pos++;

    if (is_newcommand || is_renewcommand || is_providecommand) {
        // LaTeX style: \newcommand{\name}[nargs][default]{def}

        // Parse {\name} or \name
        const char* name;
        size_t name_len;
        if (pos < len && input[pos] == '{') {
            pos++;
            if (pos < len && input[pos] == '\\') pos++;  // skip backslash in name
            size_t name_start = pos;
            while (pos < len && input[pos] != '}') pos++;
            name = input + name_start;
            name_len = pos - name_start;
            if (pos < len) pos++;  // skip '}'
        } else if (pos < len && input[pos] == '\\') {
            pos++;  // skip backslash
            size_t name_start = pos;
            while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                 (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                pos++;
            }
            name = input + name_start;
            name_len = pos - name_start;
        } else {
            return pos;
        }

        // Skip whitespace
        while (pos < len && input[pos] == ' ') pos++;

        // Parse [nargs]
        int nargs = 0;
        if (pos < len && input[pos] == '[') {
            pos++;
            nargs = 0;
            while (pos < len && input[pos] >= '0' && input[pos] <= '9') {
                nargs = nargs * 10 + (input[pos] - '0');
                pos++;
            }
            while (pos < len && input[pos] != ']') pos++;
            if (pos < len) pos++;  // skip ']'
        }

        // Skip whitespace
        while (pos < len && input[pos] == ' ') pos++;

        // Parse [default]
        const char* default_arg = nullptr;
        size_t default_len = 0;
        if (pos < len && input[pos] == '[') {
            pos = parse_optional_argument(input, pos, len, &default_arg, &default_len);
        }

        // Skip whitespace
        while (pos < len && input[pos] == ' ') pos++;

        // Parse {definition}
        const char* definition;
        size_t def_len;
        pos = parse_braced_argument(input, pos, len, &definition, &def_len);

        if (is_renewcommand) {
            processor->renewcommand(name, name_len, nargs, default_arg, default_len, definition, def_len);
        } else if (is_providecommand) {
            processor->providecommand(name, name_len, nargs, default_arg, default_len, definition, def_len);
        } else {
            processor->newcommand(name, name_len, nargs, default_arg, default_len, definition, def_len);
        }

    } else {
        // TeX style: \def\name#1#2{replacement}

        // Parse \name
        if (pos >= len || input[pos] != '\\') return pos;
        pos++;
        size_t name_start = pos;
        while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                             (input[pos] >= 'A' && input[pos] <= 'Z'))) {
            pos++;
        }
        const char* name = input + name_start;
        size_t name_len = pos - name_start;

        // Parse parameter text (everything up to {)
        size_t param_start = pos;
        while (pos < len && input[pos] != '{') {
            pos++;
        }
        const char* param_text = input + param_start;
        size_t param_len = pos - param_start;

        // Parse {replacement}
        const char* replacement;
        size_t repl_len;
        pos = parse_braced_argument(input, pos, len, &replacement, &repl_len);

        processor->define(name, name_len, param_text, param_len, replacement, repl_len);
    }

    return pos;
}

} // namespace tex
