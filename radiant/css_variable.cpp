#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lib/str.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// substitution is shared by layout, CSSOM and animation; allocations follow the caller's pool (D4.5.1v4).
// inherited custom properties resolve in the environment where they were specified.
static const CssValue* lookup_css_variable_from(DomElement* element,
                                                  const char* var_name) {
    if (!element || !var_name) return nullptr;
    CssCustomProp* winner = nullptr;
    for (CssCustomProp* var = element->css_variables; var; var = var->next) {
        if (!css_custom_property_name_matches(var->name, var_name)) continue;
        if (!winner || !winner->declaration || !var->declaration ||
            css_declaration_cascade_compare(var->declaration, winner->declaration) > 0) {
            winner = var;
        }
    }
    return winner ? winner->value : nullptr;
}

struct CssVarStack {
    const char* name;
    DomElement* element;
    const CssVarStack* parent;
    bool* invalid;
    CssPropertyCode property;
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

static bool css_var_stack_contains(const CssVarStack* stack, DomElement* element, const char* name) {
    for (const CssVarStack* current = stack; current; current = current->parent) {
        if (current->element != element || !css_custom_property_name_matches(current->name, name)) continue;
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
    const char* name, const CssVarStack* stack, size_t name_length = (size_t)-1) {
    if (!element) return nullptr;
    if (name_length == (size_t)-1) name_length = strlen(name);
    const CssPropertyRegistration* registration = element->doc
        ? css_find_document_property_registration(element->doc, name, name_length) : nullptr;
    const CssValue* initial = registration ? registration->initial_value : nullptr;
    // Script names can contain NUL; CSS identifiers cannot match their truncated prefix.
    bool token_name = !memchr(name, '\0', name_length);
    if (token_name && css_var_stack_contains(stack, element, name)) return nullptr;
    const CssValue* value = token_name ? lookup_css_variable_from(element, name) : nullptr;
    bool inherit = registration ? registration->inherits : true;
    if (value && css_value_is_global_keyword(value)) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INITIAL) {value = nullptr; inherit = false;}
        else if (keyword == CSS_VALUE_INHERIT) {value = nullptr; inherit = true;}
        else {value = nullptr;}
    }
    if (value) {
        bool invalid = false;
        CssVarStack current = {name, element, stack, &invalid, CSS_PROPERTY_UNKNOWN};
        CssVarResolutionScope scope(&current);
        const CssValue* resolved = resolve_var_function_inner(pool, value, element, nullptr, nullptr, &current);
        if (invalid) resolved = nullptr;
        if (resolved && (!registration || registration->universal)) return resolved;
        const CssPropertySyntaxComponent* matched = registration
            ? css_match_property_syntax(registration, resolved) : nullptr;
        if (matched) {
            const CssValue* computed = css_compute_registered_atom(pool, element, matched, resolved);
            if (computed && !invalid) return computed;
        }
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

static bool css_value_is_var_reference(const CssValue* value) {
    return value && (value->type == CSS_VALUE_TYPE_VAR ||
        (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
         value->data.function->name &&
         strcmp(value->data.function->name, "var") == 0));
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
        int expanded_count = count;
        if (count > 0 && !value->data.list.values) return nullptr;
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
            // A var() replaces tokens in its surrounding list. Keep a list
            // nested only when its separator differs from the outer list.
            if (css_value_is_var_reference(item) && replacement &&
                replacement->type == CSS_VALUE_TYPE_LIST &&
                replacement->data.list.comma_separated ==
                    value->data.list.comma_separated) {
                int replacement_count = replacement->data.list.count;
                if (replacement_count < 0 ||
                    replacement_count > INT_MAX - expanded_count + 1)
                    return nullptr;
                expanded_count += replacement_count - 1;
            }
        }
        if (!substituted) return value;
        if (expanded_count != count) {
            CssValue** expanded = (CssValue**)pool_alloc(pool,
                (size_t)expanded_count * sizeof(CssValue*));
            if (!expanded) return nullptr;
            int next = 0;
            for (int i = 0; i < count; i++) {
                const CssValue* item = value->data.list.values[i];
                const CssValue* replacement = substituted[i];
                if (css_value_is_var_reference(item) && replacement &&
                    replacement->type == CSS_VALUE_TYPE_LIST &&
                    replacement->data.list.comma_separated ==
                        value->data.list.comma_separated) {
                    for (int j = 0; j < replacement->data.list.count; j++)
                        expanded[next++] = replacement->data.list.values[j];
                } else {
                    expanded[next++] = (CssValue*)replacement;
                }
            }
            substituted = expanded;
        }
        CssValue* result = (CssValue*)pool_alloc(pool, sizeof(CssValue));
        if (!result) return nullptr;
        *result = *value;
        result->data.list.values = substituted;
        result->data.list.count = expanded_count;
        return result;
    }
    const CssFunction* func = value->type == CSS_VALUE_TYPE_FUNCTION
        ? value->data.function : nullptr;
    const CSSVarRef* var_ref = value->type == CSS_VALUE_TYPE_VAR
        ? value->data.var_ref : nullptr;
    if (!func && !var_ref) return value;
    if (func && !func->name) return value;
    if (func && func->arg_count > 0 && !func->args) return nullptr;
    if (func && strcmp(func->name, "var") != 0) {
        CssValue** substituted = nullptr;
        for (int i = 0; i < func->arg_count; i++) {
            const CssValue* arg = func->args[i];
            const CssValue* replacement = resolve_var_function_inner(
                pool, arg, context_element, lookup, lookup_context, stack);
            if (arg && !replacement) return nullptr;
            if (replacement != arg && !substituted) {
                if (!pool) return nullptr;
                substituted = (CssValue**)pool_alloc(pool,
                    (size_t)func->arg_count * sizeof(CssValue*));
                if (!substituted) return nullptr;
                memcpy(substituted, func->args,
                       (size_t)func->arg_count * sizeof(CssValue*));
            }
            if (substituted) substituted[i] = (CssValue*)replacement;
        }
        if (!substituted) return value;
        return css_value_create_function(pool, func->name, substituted, func->arg_count);
    }
    const CssValue* fallback_value = var_ref
        ? (var_ref->has_fallback ? var_ref->fallback : nullptr)
        : (func->arg_count >= 2 ? func->args[1] : nullptr);
    auto resolve_fallback = [&]() -> const CssValue* {
        return fallback_value
            ? resolve_var_function_inner(pool, fallback_value, context_element,
                                         lookup, lookup_context, stack)
            : nullptr;
    };
    const char* var_name = var_ref ? var_ref->name : css_var_function_name(func);
    if (!var_name) {
        return resolve_fallback();
    }
    if (!lookup) {
        const CssValue* computed = css_compute_custom_property(pool, context_element, var_name, stack);
        return computed ? computed : resolve_fallback();
    }
    if (css_var_stack_contains(stack, nullptr, var_name)) return resolve_fallback();
    const CssValue* var_value = lookup(lookup_context, var_name);
    if (var_value) {
        bool invalid = false;
        CssVarStack current = {var_name, nullptr, stack, &invalid, CSS_PROPERTY_UNKNOWN};
        const CssValue* resolved = resolve_var_function_inner(
            pool, var_value, nullptr, lookup, lookup_context, &current);
        if (resolved && !invalid) return resolved;
    }
    return resolve_fallback();
}

const CssValue* css_resolve_var_value(Pool* pool, const CssValue* value,
                                     CssVariableLookupFn lookup, void* context) {
    return resolve_var_function_inner(pool, value, nullptr, lookup, context, nullptr);
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
