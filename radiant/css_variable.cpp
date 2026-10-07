#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include "../lib/mem_grow.hpp"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// substitution is shared by layout, CSSOM and animation; allocations follow the caller's pool (D4.5.1v4).
// inherited custom properties resolve in the environment where they were specified.
struct CssVarStack {
    const char* name;
    DomElement* element;
    const CssVarStack* parent;
    bool* invalid;
    CssPropertyCode property;
    size_t name_length = (size_t)-1;
};

static thread_local const CssVarStack* css_active_var_stack = nullptr;

struct CssVarResolutionScope {
    const CssVarStack* previous;
    CssVarResolutionScope(const CssVarStack* current) : previous(css_active_var_stack) {
        css_active_var_stack = current;
    }
    ~CssVarResolutionScope() {css_active_var_stack = previous;}
};

static void css_var_stack_invalidate(const CssVarStack* stack, const CssVarStack* through) {
    for (const CssVarStack* invalid = stack; invalid; invalid = invalid->parent) {
        if (invalid->invalid) *invalid->invalid = true;
        if (invalid == through) break;
    }
}

static bool css_var_stack_contains(const CssVarStack* stack, DomElement* element,
    const char* name, size_t name_length = (size_t)-1) {
    for (const CssVarStack* current = stack; current; current = current->parent) {
        if (current->element != element || !css_custom_property_name_matches(current->name,
            name, current->name_length, name_length)) continue;
        // A fallback inside the dependency cycle cannot make its declarations valid.
        css_var_stack_invalidate(stack, current);
        return true;
    }
    return false;
}

static const CssValue* resolve_var_function_inner(Pool* pool, const CssValue* value,
    DomElement* element, CssVariableLookupFn lookup, void* context, const CssVarStack* stack);

static CssValue* css_compute_registered_dimension(Pool* pool, DomElement* owner, const CssValue* value) {
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_ANGLE ||
        value->type == CSS_VALUE_TYPE_TIME) {
        double pixels = 0.0;
        CssUnit unit = value->data.length.unit;
        double amount = value->data.length.value;
        CssUnit canonical = CSS_UNIT_PX;
        bool resolved = css_dimension_to_canonical(unit, amount, &canonical, &pixels);
        if (!resolved && owner && owner->doc) {
            resolved = css_viewport_length_to_px(unit, amount, owner->doc->viewport.width,
                owner->doc->viewport.height, owner->blk && owner->block()->writing_mode != WM_HORIZONTAL_TB,
                &pixels);
        }
        if (!resolved && (unit == CSS_UNIT_EM || unit == CSS_UNIT_REM) && owner) {
            DomElement* basis = owner;
            if (unit == CSS_UNIT_REM) {
                while (dom_parent_element(basis) && basis != basis->doc->root)
                    basis = dom_parent_element(basis);
            }
            float font_size = 0.0f;
            if (css_compute_cascaded_font_size(basis, &font_size)) {
                resolved = true;
                pixels = amount * font_size;
            }
        }
        return resolved ? css_value_create_length(pool, pixels, canonical) : nullptr;
    }
    return nullptr;
}

static CssValue* css_registered_math_value(Pool* pool, const CssMathResult& result,
    bool integer) {
    switch (result.type) {
        case CSS_MATH_NUMBER: return css_value_create_number(pool, integer ? floor(result.value + 0.5) : result.value);
        case CSS_MATH_PERCENT: return css_value_create_percentage(pool, result.percentage);
        case CSS_MATH_LENGTH: return css_value_create_length(pool, result.value, CSS_UNIT_PX);
        case CSS_MATH_ANGLE: return css_value_create_length(pool, result.value, CSS_UNIT_DEG);
        case CSS_MATH_TIME: return css_value_create_length(pool, result.value, CSS_UNIT_S);
        case CSS_MATH_RESOLUTION: return css_value_create_length(pool, result.value, CSS_UNIT_DPPX);
        case CSS_MATH_LENGTH_PERCENT: {
            // Preserve the percentage term, including zero, until a consumer supplies its basis.
            CssValue** terms = (CssValue**)pool_alloc(pool, sizeof(CssValue*) * 3);
            CssValue** args = (CssValue**)pool_alloc(pool, sizeof(CssValue*));
            CssValue* sum = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!terms || !args || !sum) return nullptr;
            terms[0] = css_value_create_percentage(pool, result.percentage);
            terms[1] = css_value_create_keyword(pool, result.value < 0.0 ? "-" : "+");
            terms[2] = css_value_create_length(pool, fabs(result.value), CSS_UNIT_PX);
            if (!terms[0] || !terms[1] || !terms[2]) return nullptr;
            sum->type = CSS_VALUE_TYPE_LIST;
            sum->data.list.values = terms;
            sum->data.list.count = 3;
            args[0] = sum;
            return css_value_create_function(pool, "calc", args, 1);
        }
        default: return nullptr;
    }
}

static CssValue* css_compute_registered_tree(Pool* pool, DomElement* owner,
    const CssPropertySyntaxComponent* component, CssValue* value, bool atom, int depth) {
    if (!value || depth > 64) return nullptr;
    if (component->type == CSS_SYNTAX_IDENT || component->type == CSS_SYNTAX_CUSTOM_IDENT ||
        component->type == CSS_SYNTAX_STRING) return value;
    if (atom && component->type == CSS_SYNTAX_COLOR && value->type != CSS_VALUE_TYPE_LIST) {
        CssComputedColor color;
        return css_color_compute(value, &color) ? css_value_create_computed_color(pool, &color) : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_ANGLE ||
        value->type == CSS_VALUE_TYPE_TIME) return css_compute_registered_dimension(pool, owner, value);
    if (atom && value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0.0 &&
        (component->type == CSS_SYNTAX_LENGTH || component->type == CSS_SYNTAX_LENGTH_PERCENTAGE))
        return css_value_create_length(pool, 0.0, CSS_UNIT_PX);
    CssValue** children = nullptr;
    int count = 0;
    bool child_atom = false;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        children = value->data.list.values;
        count = value->data.list.count;
        child_atom = atom && (component->multiplier || component->type == CSS_SYNTAX_TRANSFORM_LIST);
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        children = value->data.function->args;
        count = value->data.function->arg_count;
    }
    for (int i = 0; i < count; i++) {
        children[i] = css_compute_registered_tree(pool, owner, component, children[i], child_atom, depth + 1);
        if (!children[i]) return nullptr;
    }
    CssMathEvaluationContext context = {nullptr, nullptr, 1.0, true};
    CssMathResult computed = css_math_evaluate(value, &context);
    if (computed.resolved && computed.type != CSS_MATH_INVALID && computed.type != CSS_MATH_DEFERRED)
        return css_registered_math_value(pool, computed, atom && component->type == CSS_SYNTAX_INTEGER);
    return value;
}

static const CssValue* css_compute_registered_atom(Pool* pool, DomElement* owner,
    const CssPropertySyntaxComponent* component, const CssValue* value) {
    // Registered inheritance retains computed values in the declaration owner's environment.
    CssValue* owned = css_value_clone_owned(value, pool);
    return owned ? css_compute_registered_tree(pool, owner, component, owned, true, 0) : nullptr;
}

static const CssValue* css_compute_custom_property(Pool* pool, DomElement* element,
    const char* name, const CssVarStack* stack, size_t name_length = (size_t)-1,
    StrView* text = nullptr);

static StrView css_substitute_custom_text(Pool* pool, DomElement* element,
    StrView source, const CssVarStack* stack);

static const CssValue* css_compute_custom_property(Pool* pool, DomElement* element,
    const char* name, const CssVarStack* stack, size_t name_length, StrView* text) {
    if (!element) return nullptr;
    if (name_length == (size_t)-1) name_length = strlen(name);
    const CssPropertyRegistration* registration = element->doc
        ? css_find_document_property_registration(element->doc, name, name_length) : nullptr;
    const CssValue* initial = registration ? registration->initial_value : nullptr;
    if (css_var_stack_contains(stack, element, name, name_length)) return nullptr;
    const CssCustomProp* entry = dom_element_lookup_own_custom_property_entry(element, name, name_length);
    const CssValue* value = entry ? entry->value : nullptr;
    bool inherit = registration ? registration->inherits : true;
    if (value && css_value_is_global_keyword(value)) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INITIAL) {value = nullptr; inherit = false;}
        else if (keyword == CSS_VALUE_INHERIT) {value = nullptr; inherit = true;}
        else {value = nullptr;}
    }
    if (value) {
        bool invalid = false;
        CssVarStack current = {name, element, stack, &invalid, CSS_PROPERTY_UNKNOWN, name_length};
        CssVarResolutionScope scope(&current);
        const CssValue* resolved = resolve_var_function_inner(pool, value, element, nullptr, nullptr, &current);
        if (invalid) resolved = nullptr;
        if (resolved && (!registration || registration->universal)) {
            if (text && entry && entry->value_text) {
                *text = css_substitute_custom_text(pool, element,
                    {entry->value_text, entry->value_text_len}, &current);
                if (!text->str || invalid) return nullptr;
            }
            return resolved;
        }
        const CssPropertySyntaxComponent* matched = registration
            ? css_match_property_syntax(registration, resolved) : nullptr;
        if (matched) {
            const CssValue* computed = css_compute_registered_atom(pool, element, matched, resolved);
            if (computed && !invalid) return computed;
        }
        // an invalid unregistered declaration stays invalid instead of inheriting a parent value.
        if (!registration) return nullptr;
        // Invalid computed values use the registered default; the losing declaration stays discarded.
    }
    DomElement* parent = element->doc && element == element->doc->root
        ? nullptr : dom_parent_element(element);
    if (inherit && parent) return css_compute_custom_property(pool, parent, name, stack, name_length, text);
    if (!initial) return nullptr;
    if (registration->universal) {
        if (text && registration->initial_text)
            *text = {registration->initial_text, registration->initial_text_length};
        return initial;
    }
    const CssPropertySyntaxComponent* matched = css_match_property_syntax(registration, initial);
    return matched ? css_compute_registered_atom(pool, element, matched, initial) : nullptr;
}

// CSS Syntax 3 §9: only newly adjacent tokens need an inserted empty comment.
static bool css_custom_tokens_need_separator(const CssToken& left, const CssToken& right) {
    bool ident = right.type == CSS_TOKEN_IDENT || right.type == CSS_TOKEN_CUSTOM_PROPERTY;
    bool name = ident || right.type == CSS_TOKEN_FUNCTION || right.type == CSS_TOKEN_URL ||
        right.type == CSS_TOKEN_BAD_URL;
    bool number = right.type == CSS_TOKEN_NUMBER || right.type == CSS_TOKEN_PERCENTAGE ||
        right.type == CSS_TOKEN_DIMENSION;
    bool dash = right.type == CSS_TOKEN_DELIM && right.data.delimiter == '-';
    bool cdc = right.type == CSS_TOKEN_CDC;
    switch (left.type) {
    case CSS_TOKEN_IDENT:
    case CSS_TOKEN_CUSTOM_PROPERTY:
        return name || number || dash || cdc || right.type == CSS_TOKEN_LEFT_PAREN;
    case CSS_TOKEN_AT_KEYWORD:
    case CSS_TOKEN_HASH:
    case CSS_TOKEN_DIMENSION:
        return name || number || dash || cdc;
    case CSS_TOKEN_NUMBER:
        return name || number || cdc ||
            (right.type == CSS_TOKEN_DELIM && right.data.delimiter == '%');
    case CSS_TOKEN_DELIM:
        switch (left.data.delimiter) {
        case '#': case '-': return name || number || dash || cdc;
        case '@': return name || dash || cdc;
        case '.': case '+': return number;
        case '/': return right.type == CSS_TOKEN_DELIM && right.data.delimiter == '*';
        default: return false;
        }
    default: return false;
    }
}

struct CssCustomTextOutput {
    StrBuf* buffer;
    CssToken last;
    bool adjacent;
};

static bool css_append_custom_token(Pool* pool, CssCustomTextOutput* output, const CssToken& token) {
    StrView text = css_token_source_text(&token, pool);
    if (!text.str) return false;
    // contiguous authored spans already round-trip (for example UUID number/dimension runs).
    bool authored_neighbors = output->last.start && output->last.start + output->last.length == token.start;
    if (output->adjacent && !authored_neighbors && css_custom_tokens_need_separator(output->last, token))
        strbuf_append_str(output->buffer, "/**/");
    strbuf_append_str_n(output->buffer, text.str, text.length);
    output->last = token;
    output->adjacent = token.type != CSS_TOKEN_WHITESPACE && token.type != CSS_TOKEN_COMMENT;
    return true;
}

static const char* css_var_name_from_text(Pool* pool, StrView source) {
    if (!source.str) return nullptr;
    size_t count = 0;
    CssToken* tokens = css_tokenize(source.str, source.length, pool, &count);
    if (!tokens) return nullptr;
    const char* name = nullptr;
    for (size_t index = 0; index < count && tokens[index].type != CSS_TOKEN_EOF; index++) {
        const CssToken& token = tokens[index];
        if (token.type == CSS_TOKEN_WHITESPACE || token.type == CSS_TOKEN_COMMENT) continue;
        // a computed name is one identifier token, including decoded CSS escapes.
        if (name || token.type != CSS_TOKEN_CUSTOM_PROPERTY || !token.value ||
            strlen(token.value) <= 2) return nullptr;
        name = token.value;
    }
    return name;
}

static bool css_append_custom_text(Pool* pool, DomElement* element, StrView source,
    const CssVarStack* stack, CssCustomTextOutput* output) {
    size_t count = 0;
    CssToken* tokens = css_tokenize(source.str, source.length, pool, &count);
    if (!tokens) return false;
    for (size_t index = 0; index < count && tokens[index].type != CSS_TOKEN_EOF; index++) {
        const CssToken& token = tokens[index];
        if (token.type != CSS_TOKEN_FUNCTION || !token.value ||
            !str_ieq_const(token.value, strlen(token.value), "var(")) {
            if (!css_append_custom_token(pool, output, token)) return false;
            continue;
        }
        size_t end = index + 1, comma = count;
        int depth = 1;
        for (; end < count && tokens[end].type != CSS_TOKEN_EOF; end++) {
            CssTokenType type = tokens[end].type;
            if (css_token_block_closer(type) != CSS_TOKEN_EOF) depth++;
            else if (css_token_is_block_end(type)) {
                if (--depth == 0) break;
            } else if (type == CSS_TOKEN_COMMA && depth == 1 && comma == count) comma = end;
        }
        const char* begin = token.start + token.length;
        const char* finish = comma < end ? tokens[comma].start
            : end < count ? tokens[end].start : source.str + source.length;
        // CSS Variables 1 §3 substitutes the complete first argument before parsing its name.
        StrView computed_name = css_substitute_custom_text(pool, element,
            {begin, (size_t)(finish - begin)}, stack);
        const char* name = css_var_name_from_text(pool, computed_name);
        StrView replacement = {};
        const CssValue* value = name ? css_compute_custom_property(pool, element, name, stack,
            strlen(name), &replacement) : nullptr;
        if (value) {
            if (!replacement.str) {
                CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
                if (!formatter) return false;
                // registered values substitute their computed spelling, including color serialization.
                formatter->options.computed_colors = true;
                css_format_value(formatter, (CssValue*)value);
                String* text = stringbuf_to_string(formatter->output);
                if (!text) return false;
                replacement = {text->chars, text->len};
            }
        } else {
            if (comma >= end) return false;
            const char* begin = tokens[comma].start + tokens[comma].length;
            const char* finish = end < count ? tokens[end].start : source.str + source.length;
            replacement = {begin, (size_t)(finish - begin)};
        }
        // empty replacement leaves its neighboring tokens adjacent.
        if (!css_append_custom_text(pool, element, replacement, stack, output)) return false;
        index = end;
    }
    return true;
}

static StrView css_substitute_custom_text(Pool* pool, DomElement* element,
    StrView source, const CssVarStack* stack) {
    StrBuf* buffer = strbuf_new();
    if (!buffer) return {};
    CssCustomTextOutput output = {buffer, {}, false};
    bool valid = css_append_custom_text(pool, element, source, stack, &output);
    // D4.5.1v4: only the caller's pool retains output; the builder dies at return.
    StrView result = valid ? StrView{pool_dup_n(pool, buffer->str, buffer->length), buffer->length}
        : StrView{};
    strbuf_free(buffer);
    return result;
}

static const char* css_var_value_name(const CssValue* value) {
    while (value && value->type == CSS_VALUE_TYPE_LIST &&
        !value->data.list.comma_separated && value->data.list.count == 1 && value->data.list.values)
        value = value->data.list.values[0];
    if (!value || value->type != CSS_VALUE_TYPE_CUSTOM) return nullptr;
    const char* name = value->data.custom_property.name;
    // strings and multiple component values cannot name a custom property.
    return name && name[0] == '-' && name[1] == '-' && name[2] ? name : nullptr;
}

struct CssSubstitutedTokens {
    CssValue** values;
    int count;
    int capacity;
};

static bool css_append_substituted_tokens(Pool* pool, const CssValue* value,
                                          CssSubstitutedTokens* tokens) {
    if (value && value->type == CSS_VALUE_TYPE_LIST) {
        if (value->data.list.count < 0 ||
            (value->data.list.count && !value->data.list.values)) return false;
        for (int i = 0; i < value->data.list.count; i++) {
            if (i && value->data.list.comma_separated &&
                !css_append_substituted_tokens(pool, nullptr, tokens)) return false;
            if (!value->data.list.values[i] ||
                !css_append_substituted_tokens(pool, value->data.list.values[i], tokens))
                return false;
        }
        return true;
    }
    if (tokens->count == INT_MAX || !lam::pool_copy_grow_array(pool,
            &tokens->values, &tokens->capacity, tokens->count,
            tokens->count + 1, 4, false)) return false;
    // null is a comma boundary here; empty lists contribute no tokens.
    tokens->values[tokens->count++] = (CssValue*)value;
    return true;
}

static CssValue* css_substituted_token_group(Pool* pool, CssValue** values, int count) {
    return count == 1 ? values[0] : css_value_create_list(pool, values, (size_t)count);
}

static const CssValue* css_normalize_substituted_list(Pool* pool, const CssValue* value) {
    CssSubstitutedTokens tokens = {};
    if (!css_append_substituted_tokens(pool, value, &tokens)) return nullptr;
    int groups = 1;
    for (int i = 0; i < tokens.count; i++) {
        if (!tokens.values[i]) groups++;
    }
    if (groups == 1) return css_substituted_token_group(pool, tokens.values, tokens.count);
    CssValue** values = (CssValue**)pool_alloc(pool, (size_t)groups * sizeof(CssValue*));
    if (!values) return nullptr;
    int start = 0, next = 0;
    for (int i = 0; i <= tokens.count; i++) {
        if (i < tokens.count && tokens.values[i]) continue;
        values[next] = css_substituted_token_group(pool, tokens.values + start, i - start);
        if (!values[next++]) return nullptr;
        start = i + 1;
    }
    CssValue* result = css_value_create_list(pool, values, (size_t)groups);
    if (result) result->data.list.comma_separated = true;
    return result;
}

static const CssValue* resolve_var_function_inner(Pool* pool, const CssValue* value,
                                                  DomElement* context_element,
                                                  CssVariableLookupFn lookup,
                                                  void* lookup_context,
                                                  const CssVarStack* stack) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        CssValue** substituted = nullptr;
        int count = value->data.list.count;
        if (count < 0 || (count > 0 && !value->data.list.values)) return nullptr;
        for (int i = 0; i < count; i++) {
            const CssValue* item = value->data.list.values[i];
            const CssValue* replacement = resolve_var_function_inner(
                pool, item, context_element, lookup, lookup_context, stack);
            if (item && !replacement) return nullptr;
            if (replacement != item && !substituted) {
                if (!pool) return nullptr;
                substituted = (CssValue**)pool_alloc(pool,
                    (size_t)count * sizeof(CssValue*));
                if (!substituted) return nullptr;
                memcpy(substituted, value->data.list.values,
                       (size_t)count * sizeof(CssValue*));
            }
            if (substituted) substituted[i] = (CssValue*)replacement;
        }
        if (!substituted) return value;
        CssValue result = *value;
        result.data.list.values = substituted;
        // substitution joins adjacent tokens across comma and space boundaries.
        return css_normalize_substituted_list(pool, &result);
    }
    const CssFunction* func = value->type == CSS_VALUE_TYPE_FUNCTION
        ? value->data.function : nullptr;
    const CSSVarRef* var_ref = value->type == CSS_VALUE_TYPE_VAR
        ? value->data.var_ref : nullptr;
    if (!func && !var_ref) return value;
    if (func && !func->name) return value;
    if (func && func->arg_count > 0 && !func->args) return nullptr;
    if (func && strcmp(func->name, "var") != 0) {
        CssValue arguments = {};
        arguments.type = CSS_VALUE_TYPE_LIST;
        arguments.data.list.values = func->args;
        arguments.data.list.count = func->arg_count;
        arguments.data.list.comma_separated = true;
        const CssValue* substituted = resolve_var_function_inner(pool, &arguments,
            context_element, lookup, lookup_context, stack);
        if (!substituted) return nullptr;
        if (substituted == &arguments) return value;
        CssFunction* new_func = (CssFunction*)pool_alloc(pool,
                                                        sizeof(CssFunction));
        CssValue* result = (CssValue*)pool_alloc(pool, sizeof(CssValue));
        if (!new_func || !result) return nullptr;
        *new_func = *func;
        if (substituted->type == CSS_VALUE_TYPE_LIST &&
            (substituted->data.list.comma_separated || substituted->data.list.count == 0)) {
            new_func->args = substituted->data.list.values;
            new_func->arg_count = substituted->data.list.count;
        } else {
            new_func->args = (CssValue**)pool_alloc(pool, sizeof(CssValue*));
            if (!new_func->args) return nullptr;
            new_func->args[0] = (CssValue*)substituted;
            new_func->arg_count = 1;
        }
        *result = *value;
        result->data.function = new_func;
        return result;
    }
    CssValue fallback_tokens = {};
    fallback_tokens.type = CSS_VALUE_TYPE_LIST;
    if (func && func->arg_count >= 2) {
        // every comma after the first belongs to the fallback token sequence.
        fallback_tokens.data.list.values = func->args + 1;
        fallback_tokens.data.list.count = func->arg_count - 1;
        fallback_tokens.data.list.comma_separated = true;
    }
    const CssValue* fallback_value = var_ref
        ? (var_ref->has_fallback ? var_ref->fallback : nullptr)
        : (func->arg_count >= 2 ? (func->arg_count == 2 ? func->args[1] : &fallback_tokens)
            : nullptr);
    auto resolve_fallback = [&]() -> const CssValue* {
        if (!fallback_value) return nullptr;
        const CssValue* resolved = resolve_var_function_inner(pool, fallback_value,
            context_element, lookup, lookup_context, stack);
        // synthetic fallback lists cannot escape their stack frame.
        return resolved == &fallback_tokens
            ? (pool ? css_normalize_substituted_list(pool, resolved) : nullptr) : resolved;
    };
    const CssValue* first_arg = func && func->arg_count > 0
        ? resolve_var_function_inner(pool, func->args[0], context_element, lookup, lookup_context, stack)
        : nullptr;
    const char* var_name = var_ref ? var_ref->name : css_var_value_name(first_arg);
    if (!var_name) {
        return resolve_fallback();
    }
    if (!lookup) {
        const CssValue* computed = css_compute_custom_property(pool, context_element, var_name, stack);
        return computed ? computed : resolve_fallback();
    }
    DomElement* owner = nullptr;
    const CssValue* var_value = lookup(lookup_context, context_element, var_name, &owner);
    if (css_var_stack_contains(stack, owner, var_name)) return nullptr;
    if (var_value) {
        bool invalid = false;
        CssVarStack current = {var_name, owner, stack, &invalid, CSS_PROPERTY_UNKNOWN};
        const CssValue* resolved = resolve_var_function_inner(
            pool, var_value, owner, lookup, lookup_context, &current);
        if (resolved && !invalid) return resolved;
    }
    return resolve_fallback();
}

const CssValue* css_resolve_var_value(Pool* pool, const CssValue* value,
                                     CssVariableLookupFn lookup, void* context, DomElement* element) {
    return resolve_var_function_inner(pool, value, element, lookup, context, css_active_var_stack);
}

// Resolve in the declaration owner's environment for inherited custom properties.
const CssValue* css_resolve_element_var_value(Pool* pool, DomElement* element,
    const CssValue* value, CssPropertyCode property) {
    if (property != CSS_PROPERTY_FONT_SIZE && property != CSS_PROPERTY_LINE_HEIGHT)
        return resolve_var_function_inner(pool, value, element, nullptr, nullptr, css_active_var_stack);
    for (const CssVarStack* current = css_active_var_stack; current; current = current->parent) {
        if (current->element == element && current->property == property) {
            css_var_stack_invalidate(css_active_var_stack, current);
            return nullptr;
        }
    }
    bool invalid = false;
    CssVarStack consumer = {nullptr, element, css_active_var_stack, &invalid, property};
    CssVarResolutionScope scope(&consumer);
    const CssValue* resolved = resolve_var_function_inner(pool, value, element, nullptr, nullptr, &consumer);
    return invalid ? nullptr : resolved;
}

const CssValue* css_compute_element_custom_property(Pool* pool, DomElement* element,
    const char* name, size_t name_length) {
    return css_compute_custom_property(pool, element, name, css_active_var_stack, name_length);
}

const CssValue* css_compute_element_custom_property_text(Pool* pool, DomElement* element,
    const char* name, size_t name_length, StrView* text) {
    if (text) *text = {};
    return css_compute_custom_property(pool, element, name, css_active_var_stack, name_length, text);
}
