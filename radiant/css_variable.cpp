#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lib/str.h"
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
    bool preserve_tokens = false;
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

// CSS Syntax 3 section 9: separators preserve tokens without introducing whitespace.
static bool css_tokens_need_separator(const CssToken& left, const CssToken& right) {
    bool ident = right.type == CSS_TOKEN_IDENT || right.type == CSS_TOKEN_FUNCTION ||
        right.type == CSS_TOKEN_URL || right.type == CSS_TOKEN_BAD_URL || right.type == CSS_TOKEN_CDC;
    bool numeric = right.type == CSS_TOKEN_NUMBER || right.type == CSS_TOKEN_PERCENTAGE ||
        right.type == CSS_TOKEN_DIMENSION;
    bool dash = right.type == CSS_TOKEN_DELIM && right.data.delimiter == '-';
    if (left.type == CSS_TOKEN_IDENT)
        return ident || numeric || dash || right.type == CSS_TOKEN_LEFT_PAREN;
    if (left.type == CSS_TOKEN_AT_KEYWORD || left.type == CSS_TOKEN_HASH ||
        left.type == CSS_TOKEN_DIMENSION) return ident || numeric || dash;
    if (left.type == CSS_TOKEN_NUMBER)
        return ident || numeric || (right.type == CSS_TOKEN_DELIM && right.data.delimiter == '%');
    if (left.type != CSS_TOKEN_DELIM) return false;
    switch (left.data.delimiter) {
        case '#': case '-': return ident || numeric || dash;
        case '@': return ident || dash;
        case '.': case '+': return numeric;
        case '/': return right.type == CSS_TOKEN_DELIM && right.data.delimiter == '*';
        default: return false;
    }
}

struct CssTokenOutput {
    StringBuf* text;
    CssToken last;
    bool has_last;
};

static bool css_append_token_fragment(Pool* pool, CssTokenOutput* output, StrView text) {
    if (!text.length) return true;
    size_t count = 0;
    CssToken* tokens = css_tokenize(text.str, text.length, pool, &count);
    if (!tokens || !count) return false;
    size_t first = 0, end = count;
    // comments at substitution boundaries are not part of either adjacent token.
    while (first < end && tokens[first].type == CSS_TOKEN_COMMENT) first++;
    while (end > first && (tokens[end - 1].type == CSS_TOKEN_EOF ||
        tokens[end - 1].type == CSS_TOKEN_COMMENT)) end--;
    if (first < end) {
        if (output->has_last && css_tokens_need_separator(output->last, tokens[first]))
            stringbuf_append_str(output->text, "/**/");
        const CssToken& last = tokens[end - 1];
        stringbuf_append_str_n(output->text, tokens[first].start,
            (size_t)(last.start + last.length - tokens[first].start));
        output->last = last;
        output->has_last = true;
    }
    css_token_array_release(pool, tokens, count);
    return true;
}

static CssValue* css_parse_custom_token_text(Pool* pool, StrView text) {
    StringBuf* declaration = stringbuf_new(pool);
    if (!declaration) return nullptr;
    stringbuf_append_str(declaration, "--value:");
    stringbuf_append_str_n(declaration, text.str, text.length);
    size_t count = 0;
    CssToken* tokens = css_tokenize(declaration->str->chars, declaration->length, pool, &count);
    if (!tokens || count > INT_MAX) {stringbuf_free(declaration); return nullptr;}
    // inserted separator comments carry no typed values or whitespace tokens.
    count = css_token_array_remove_comments(pool, tokens, count);
    int position = 0;
    CssDeclaration* parsed = css_parse_declaration_from_tokens(tokens, &position,
        (int)count, pool); // INT_CAST_OK: token count is bounded by INT_MAX above.
    css_token_array_release(pool, tokens, count);
    stringbuf_free(declaration);
    if (!parsed || !parsed->value) return nullptr;
    // declaration parsing trims outer whitespace; substitution must retain it.
    return css_value_create_token_sequence(pool, parsed->value, text);
}

static const CssValue* css_resolve_token_text(Pool* pool, const CssValue* value,
    DomElement* element, CssVariableLookupFn lookup, void* context, const CssVarStack* stack) {
    const String* text = value->data.tokens.text;
    size_t count = 0;
    CssToken* tokens = css_tokenize(text->chars, text->len, pool, &count);
    if (!tokens || count > INT_MAX) return nullptr;
    count = css_token_array_remove_comments(pool, tokens, count);
    CssTokenOutput output = {nullptr, {}, false};
    size_t start = 0;
    const CssValue* result = value;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type != CSS_TOKEN_FUNCTION || !tokens[i].value ||
            !str_ieq_cstr(tokens[i].value, "var(")) continue;
        if (!output.text) output.text = stringbuf_new(pool);
        if (!output.text || !css_append_token_fragment(pool, &output,
            strview_init(text->chars + start, (size_t)(tokens[i].start - text->chars) - start))) {
            result = nullptr; break;
        }
        int position = (int)i; // INT_CAST_OK: token array index is bounded by INT_MAX above.
        CssValue* function = css_parse_function_from_tokens(tokens, &position,
            (int)count, pool); // INT_CAST_OK: token count was checked above.
        if (!function || function->type != CSS_VALUE_TYPE_FUNCTION) {result = nullptr; break;}
        // retain the entire fallback, including its commas, quotes and whitespace.
        int depth = 0;
        for (size_t j = i + 1; j < (size_t)position; j++) {
            CssTokenType type = tokens[j].type;
            if (type == CSS_TOKEN_FUNCTION || type == CSS_TOKEN_LEFT_PAREN) depth++;
            else if (type == CSS_TOKEN_RIGHT_PAREN) {if (!depth) break; depth--;}
            else if (type == CSS_TOKEN_COMMA && !depth) {
                const char* end = tokens[position - 1].start;
                if (tokens[position - 1].type != CSS_TOKEN_RIGHT_PAREN)
                    end += tokens[position - 1].length;
                const char* begin = tokens[j].start + tokens[j].length;
                CssValue* fallback = css_parse_custom_token_text(pool,
                    strview_init(begin, (size_t)(end - begin)));
                if (!fallback || function->data.function->arg_count < 2) {result = nullptr; break;}
                function->data.function->args[1] = fallback;
                function->data.function->arg_count = 2;
                break;
            }
        }
        if (!result) break;
        const CssValue* replacement = resolve_var_function_inner(pool, function,
            element, lookup, context, stack);
        if (!replacement) {result = nullptr; break;}
        CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        if (!formatter) {result = nullptr; break;}
        formatter->options.preserve_tokens = true;
        formatter->options.quote_urls = false;
        css_format_value(formatter, (CssValue*)replacement);
        bool appended = css_append_token_fragment(pool, &output,
            strview_init(formatter->output->str->chars, formatter->output->length));
        css_formatter_destroy(formatter);
        if (!appended) {result = nullptr; break;}
        const CssToken& last = tokens[position - 1];
        start = (size_t)(last.start + last.length - text->chars);
        i = (size_t)position - 1;
    }
    if (result && output.text) {
        result = css_append_token_fragment(pool, &output,
            strview_init(text->chars + start, text->len - start))
            ? css_parse_custom_token_text(pool,
                strview_init(output.text->str->chars, output.text->length)) : nullptr;
    }
    if (output.text) stringbuf_free(output.text);
    css_token_array_release(pool, tokens, count);
    return result;
}

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
    CssValue* owned = css_value_clone_owned(css_value_unwrap(value), pool);
    return owned ? css_compute_registered_tree(pool, owner, component, owned, true, 0) : nullptr;
}

static const CssValue* css_compute_custom_property(Pool* pool, DomElement* element,
    const char* name, const CssVarStack* stack, size_t name_length = (size_t)-1) {
    if (!element) return nullptr;
    if (name_length == (size_t)-1) name_length = strlen(name);
    const CssPropertyRegistration* registration = element->doc
        ? css_find_document_property_registration(element->doc, name, name_length) : nullptr;
    const CssValue* initial = registration ? registration->initial_value : nullptr;
    if (css_var_stack_contains(stack, element, name, name_length)) return nullptr;
    StrView token_text = {};
    const CssValue* value = dom_element_lookup_own_custom_property(element, name, name_length, &token_text);
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
        current.preserve_tokens = stack && stack->preserve_tokens;
        CssVarResolutionScope scope(&current);
        // raw tokens borrow the winning declaration only during this pool-owned computation (D4.5.1v4).
        if (current.preserve_tokens && pool && token_text.str && (!registration || registration->universal))
            value = css_value_create_token_sequence(pool, (CssValue*)value, token_text);
        const CssValue* resolved = resolve_var_function_inner(pool, value, element, nullptr, nullptr, &current);
        if (invalid) resolved = nullptr;
        if (resolved && (!registration || registration->universal)) return resolved;
        const CssPropertySyntaxComponent* matched = registration
            ? css_match_property_syntax(registration, css_value_unwrap(resolved)) : nullptr;
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
    if (inherit && parent) return css_compute_custom_property(pool, parent, name, stack, name_length);
    if (!initial) return nullptr;
    if (registration->universal) return initial;
    const CssPropertySyntaxComponent* matched = css_match_property_syntax(registration, initial);
    return matched ? css_compute_registered_atom(pool, element, matched, initial) : nullptr;
}

static const char* css_var_function_name(const CssFunction* func) {
    if (!func || !func->args || func->arg_count < 1 || !func->args[0]) return nullptr;
    CssValue* first_arg = func->args[0];
    if (first_arg->type == CSS_VALUE_TYPE_CUSTOM) {
        return first_arg->data.custom_property.name;
    }
    return first_arg->type == CSS_VALUE_TYPE_STRING ? first_arg->data.string : nullptr;
}

struct CssSubstitutedTokens {
    CssValue** values;
    int count;
    int capacity;
};

static bool css_append_substituted_tokens(Pool* pool, const CssValue* value,
                                          CssSubstitutedTokens* tokens) {
    value = css_value_unwrap(value);
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
    if (pool && value->type == CSS_VALUE_TYPE_TOKEN_SEQUENCE)
        return css_resolve_token_text(pool, value, context_element, lookup, lookup_context, stack);
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
    const char* var_name = var_ref ? var_ref->name : css_var_function_name(func);
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
        current.preserve_tokens = stack && stack->preserve_tokens;
        const CssValue* resolved = resolve_var_function_inner(
            pool, var_value, owner, lookup, lookup_context, &current);
        if (resolved && !invalid) return resolved;
    }
    return resolve_fallback();
}

const CssValue* css_resolve_var_value(Pool* pool, const CssValue* value,
                                     CssVariableLookupFn lookup, void* context, DomElement* element,
                                     bool preserve_tokens) {
    CssVarStack serialization = {nullptr, element, css_active_var_stack, nullptr, CSS_PROPERTY_UNKNOWN};
    serialization.preserve_tokens = true;
    const CssValue* resolved = resolve_var_function_inner(pool, value, element, lookup, context,
        preserve_tokens ? &serialization : css_active_var_stack);
    return preserve_tokens ? resolved : css_value_unwrap(resolved);
}

// Resolve in the declaration owner's environment for inherited custom properties.
const CssValue* css_resolve_element_var_value(Pool* pool, DomElement* element,
    const CssValue* value, CssPropertyCode property) {
    if (property != CSS_PROPERTY_FONT_SIZE && property != CSS_PROPERTY_LINE_HEIGHT)
        return css_value_unwrap(resolve_var_function_inner(pool, value, element, nullptr, nullptr, css_active_var_stack));
    for (const CssVarStack* current = css_active_var_stack; current; current = current->parent) {
        if (current->element == element && current->property == property) {
            css_var_stack_invalidate(css_active_var_stack, current);
            return nullptr;
        }
    }
    bool invalid = false;
    CssVarStack consumer = {nullptr, element, css_active_var_stack, &invalid, property};
    consumer.preserve_tokens = css_active_var_stack && css_active_var_stack->preserve_tokens;
    CssVarResolutionScope scope(&consumer);
    const CssValue* resolved = resolve_var_function_inner(pool, value, element, nullptr, nullptr, &consumer);
    return invalid ? nullptr : css_value_unwrap(resolved);
}

const CssValue* css_compute_element_custom_property(Pool* pool, DomElement* element,
    const char* name, size_t name_length, bool preserve_tokens) {
    // layout keeps the typed path; only CSSOM requests source-preserving substitution.
    CssVarStack serialization = {nullptr, element, css_active_var_stack, nullptr, CSS_PROPERTY_UNKNOWN};
    serialization.preserve_tokens = true;
    const CssValue* resolved = css_compute_custom_property(pool, element, name,
        preserve_tokens ? &serialization : css_active_var_stack, name_length);
    return preserve_tokens ? resolved : css_value_unwrap(resolved);
}
