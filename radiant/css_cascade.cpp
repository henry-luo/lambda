#include "view.hpp"
#include "layout.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include <string.h>

// CSS-only targets do not link StateStore; their matcher keeps default state.
__attribute__((weak)) void state_configure_selector_matcher(
        DocState* /*state*/, SelectorMatcher* /*matcher*/) {}

typedef struct CssLayerNode {
    const char* name;
    struct CssLayerNode* first_child;
    struct CssLayerNode* last_child;
    struct CssLayerNode* next_sibling;
    uint32_t order;
} CssLayerNode;

typedef struct CssLayerRuleRef {
    CssRule* rule;
    CssLayerNode* layer;
    struct CssLayerRuleRef* next;
} CssLayerRuleRef;

typedef struct CssLayerRegistry {
    Arena* scratch;
    CssLayerNode roots[CSS_ORIGIN_TRANSITION + 1];
    CssLayerRuleRef* rules;
    bool valid;
} CssLayerRegistry;

static CssLayerNode* css_layer_child(CssLayerRegistry* registry,
                                     CssLayerNode* parent, const char* name) {
    if (!registry || !parent) return nullptr;
    if (name) {
        for (CssLayerNode* child = parent->first_child; child;
             child = child->next_sibling) {
            if (child->name && strcmp(child->name, name) == 0) return child;
        }
    }
    CssLayerNode* node = (CssLayerNode*)arena_alloc(
        registry->scratch, sizeof(CssLayerNode));
    if (!node) {
        registry->valid = false;
        return nullptr;
    }
    *node = {};
    node->name = name;
    if (parent->last_child) parent->last_child->next_sibling = node;
    else parent->first_child = node;
    parent->last_child = node;
    return node;
}

static CssLayerNode* css_layer_name_node(CssLayerRegistry* registry,
                                         CssLayerNode* parent,
                                         const CssLayerName* name) {
    if (!registry || !parent || !name) return nullptr;
    for (size_t i = 0; i < name->part_count; i++) {
        parent = css_layer_child(registry, parent, name->parts[i]);
        if (!parent) return nullptr;
    }
    return parent;
}

static void css_layer_collect_rule(CssLayerRegistry* registry,
                                   CssRule* rule, CssLayerNode* layer,
                                   CssEngine* engine) {
    if (!registry || !rule || !registry->valid) return;
    if (rule->type == CSS_RULE_STYLE ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        CssLayerRuleRef* ref = (CssLayerRuleRef*)arena_alloc(
            registry->scratch, sizeof(CssLayerRuleRef));
        if (!ref) {
            registry->valid = false;
            return;
        }
        ref->rule = rule;
        ref->layer = layer;
        ref->next = registry->rules;
        registry->rules = ref;
        if (rule->type == CSS_RULE_STYLE) {
            for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
                css_layer_collect_rule(registry,
                    rule->data.style_rule.nested_rules[i], layer, engine);
            }
        }
        return;
    }
    if (rule->type == CSS_RULE_IMPORT) {
        if (!css_import_rule_is_active(rule, engine)) return;
        if (rule->data.import_rule.has_layer) {
            CssLayerNode* parent = layer ? layer :
                &registry->roots[rule->origin];
            layer = rule->data.import_rule.anonymous_layer
                ? css_layer_child(registry, parent, nullptr)
                : css_layer_name_node(registry, parent,
                    &rule->data.import_rule.layer_name);
            if (!layer) return;
        }
        CssStylesheet* imported = rule->data.import_rule.stylesheet;
        if (imported && !imported->disabled) {
            for (size_t i = 0; i < imported->rule_count; i++) {
                css_layer_collect_rule(registry, imported->rules[i], layer, engine);
            }
        }
        return;
    }
    if (rule->type == CSS_RULE_LAYER) {
        if (rule->data.conditional_rule.invalid_layer) return;
        CssLayerNode* parent = layer ? layer :
            &registry->roots[rule->origin];
        if (rule->data.conditional_rule.layer_statement) {
            for (size_t i = 0;
                 i < rule->data.conditional_rule.layer_name_count; i++) {
                css_layer_name_node(registry, parent,
                    &rule->data.conditional_rule.layer_names[i]);
            }
            return;
        }
        layer = rule->data.conditional_rule.layer_name_count
            ? css_layer_name_node(registry, parent,
                &rule->data.conditional_rule.layer_names[0])
            : css_layer_child(registry, parent, nullptr);
        if (!layer) return;
    } else if (rule->type == CSS_RULE_MEDIA) {
        if (!css_evaluate_media_query(engine,
                rule->data.conditional_rule.condition)) return;
    } else if (rule->type == CSS_RULE_SUPPORTS) {
        if (!css_evaluate_supports_condition(engine,
                rule->data.conditional_rule.condition)) return;
    } else if (rule->type == CSS_RULE_SCOPE) {
        if (rule->data.conditional_rule.invalid_scope) return;
    } else {
        return;
    }
    for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
        css_layer_collect_rule(registry, rule->data.conditional_rule.rules[i],
            layer, engine);
    }
}

static void css_layer_number_children(CssLayerNode* parent, uint32_t* next) {
    for (CssLayerNode* child = parent->first_child; child;
         child = child->next_sibling) {
        css_layer_number_children(child, next);
        child->order = ++*next;
    }
}

static void css_layer_rank_stylesheets(DomDocument* doc,
                                       CssStylesheet** stylesheets, int count,
                                       CssEngine* engine) {
    if (!doc || !stylesheets || count <= 0 || !engine) return;
    Arena* scratch = mem_arena_create((MemContext*)doc->services.mem_ctx,
        MEM_ROLE_TEMP, "css.layer.registry");
    if (!scratch) return;
    CssLayerRegistry registry = {};
    registry.scratch = scratch;
    registry.valid = true;
    for (int i = 0; i < count; i++) {
        CssStylesheet* sheet = stylesheets[i];
        if (!sheet || sheet->disabled || sheet->is_import_child) continue;
        for (size_t j = 0; j < sheet->rule_count; j++) {
            css_layer_collect_rule(&registry, sheet->rules[j], nullptr, engine);
        }
    }
    if (registry.valid) {
        for (int origin = CSS_ORIGIN_USER_AGENT;
             origin <= CSS_ORIGIN_TRANSITION; origin++) {
            uint32_t next = 0;
            css_layer_number_children(&registry.roots[origin], &next);
        }
        for (CssLayerRuleRef* ref = registry.rules; ref; ref = ref->next) {
            uint32_t order = ref->layer ? ref->layer->order : 0;
            for (size_t d = 0; d < ref->rule->data.style_rule.declaration_count; d++) {
                CssDeclaration* declaration = ref->rule->data.style_rule.declarations[d];
                if (declaration) declaration->layer_order = order;
            }
        }
    }
    mem_arena_destroy(scratch);
}

static void apply_rule_to_element_with_nested(DomElement* element, CssRule* rule,
                                              SelectorMatcher* matcher, Pool* pool,
                                              CssEngine* engine, int depth,
                                              uint32_t scope_proximity = 0);

struct CssScopeApplyContext {
    DomElement* element;
    CssRule* rule;
    SelectorMatcher* matcher;
    Pool* pool;
    CssEngine* engine;
};

static void apply_scope_root(void* data, uint32_t scope_proximity) {
    CssScopeApplyContext* context = (CssScopeApplyContext*)data;
    CssRuleChildList children = css_rule_child_list(context->rule);
    for (size_t i = 0; children.count && i < *children.count; i++)
        apply_rule_to_element_with_nested(context->element, (*children.rules)[i],
            context->matcher, context->pool, context->engine, 0, scope_proximity);
}

static void apply_rule_to_element(DomElement* element, CssRule* rule,
                                  SelectorMatcher* matcher, Pool* pool,
                                  CssEngine* engine, uint32_t scope_proximity = 0) {
    if (!element || !rule || !matcher || !pool) return;
    if (rule->type == CSS_RULE_SCOPE) {
        CssScopeApplyContext context = {element, rule, matcher, pool, engine};
        css_scope_visit_roots(rule, element, matcher, apply_scope_root, &context);
        return;
    }

    if (rule->type == CSS_RULE_IMPORT) {
        if (!css_import_rule_is_active(rule, engine)) return;
        CssStylesheet* imported = rule->data.import_rule.stylesheet;
        if (imported && !imported->disabled) {
            for (size_t i = 0; i < imported->rule_count; i++) {
                apply_rule_to_element_with_nested(element, imported->rules[i],
                    matcher, pool, engine, 0);
            }
        }
        return;
    }

    bool nested_rule = rule->type == CSS_RULE_MEDIA ||
        rule->type == CSS_RULE_SUPPORTS || rule->type == CSS_RULE_LAYER;
    if (nested_rule) {
        // Conditional and layer blocks preserve source order while applying
        // their nested selector rules.
        bool enabled = (rule->type == CSS_RULE_LAYER &&
                !rule->data.conditional_rule.invalid_layer) ||
            (rule->type == CSS_RULE_MEDIA
                ? css_evaluate_media_query(engine, rule->data.conditional_rule.condition)
                : css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition));
        if (enabled) {
            for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
                CssRule* nested = rule->data.conditional_rule.rules[i];
                if (nested) apply_rule_to_element_with_nested(element, nested,
                    matcher, pool, engine, 0, scope_proximity);
            }
        }
        return;
    }
    if (rule->type != CSS_RULE_STYLE &&
        rule->type != CSS_RULE_NESTED_DECLARATIONS) return;

    CssSelector* selector = rule->data.style_rule.selector;
    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    if (group && group->selector_count > 0) {
        bool matched_selector = false;
        CssSpecificity best_specificity = {0, 0, 0, 0, false};
        for (size_t i = 0; i < group->selector_count; i++) {
            CssSelector* candidate = group->selectors[i];
            if (!candidate) continue;
            if (candidate->specificity.inline_style == 0 &&
                candidate->specificity.ids == 0 &&
                candidate->specificity.classes == 0 &&
                candidate->specificity.elements == 0) {
                candidate->specificity = selector_matcher_calculate_specificity(
                    matcher, candidate);
            }
            MatchResult result;
            if (!selector_matcher_matches(matcher, candidate, element, &result)) continue;
            if (result.pseudo_element != PSEUDO_ELEMENT_NONE) {
                if (rule->data.style_rule.declaration_count > 0) {
                    dom_element_apply_pseudo_element_rule(element, rule,
                        result.specificity, (int)result.pseudo_element, scope_proximity);
                }
            } else if (!matched_selector ||
                       css_specificity_compare(result.specificity, best_specificity) > 0) {
                // a selector list contributes its most specific matching branch.
                matched_selector = true;
                best_specificity = result.specificity;
            }
        }
        if (matched_selector && rule->data.style_rule.declaration_count > 0) {
            dom_element_apply_rule(element, rule, best_specificity, scope_proximity);
        }
        return;
    }

    if (!selector) return;
    if (selector->specificity.inline_style == 0 &&
        selector->specificity.ids == 0 &&
        selector->specificity.classes == 0 &&
        selector->specificity.elements == 0) {
        selector->specificity = selector_matcher_calculate_specificity(matcher, selector);
    }
    MatchResult result;
    if (!selector_matcher_matches(matcher, selector, element, &result)) return;
    if (rule->data.style_rule.declaration_count == 0) return;
    if (result.pseudo_element != PSEUDO_ELEMENT_NONE) {
        dom_element_apply_pseudo_element_rule(element, rule,
                                              result.specificity,
                                              (int)result.pseudo_element, scope_proximity);
    } else {
        dom_element_apply_rule(element, rule, result.specificity, scope_proximity);
    }
}

static void apply_rule_to_element_with_nested(DomElement* element, CssRule* rule,
                                              SelectorMatcher* matcher, Pool* pool,
                                              CssEngine* engine, int depth, uint32_t scope_proximity) {
    if (!rule || depth > 128) return;
    apply_rule_to_element(element, rule, matcher, pool, engine, scope_proximity);
    if (rule->type == CSS_RULE_STYLE) {
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            apply_rule_to_element_with_nested(element,
                rule->data.style_rule.nested_rules[i], matcher, pool, engine,
                depth + 1, scope_proximity);
        }
    }
}

void radiant_apply_css_rule_to_element(DomElement* element, CssRule* rule,
                                       SelectorMatcher* matcher, Pool* pool,
                                       CssEngine* engine) {
    apply_rule_to_element_with_nested(element, rule, matcher, pool, engine, 0);
}

static bool conditional_rule_is_active(CssRule* rule, CssEngine* engine) {
    if (!rule) return false;
    if (rule->type == CSS_RULE_LAYER)
        return !rule->data.conditional_rule.invalid_layer;
    if (rule->type == CSS_RULE_MEDIA) {
        return css_evaluate_media_query(engine, rule->data.conditional_rule.condition);
    }
    if (rule->type == CSS_RULE_SUPPORTS) {
        return css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition);
    }
    return false;
}

static size_t active_rule_count(CssRule* rule, CssEngine* engine) {
    if (!rule) return 0;
    // scoped groups retain target-dependent root matching in the rule program.
    if (rule->type == CSS_RULE_SCOPE) return !rule->data.conditional_rule.invalid_scope;
    if (rule->type == CSS_RULE_NESTED_DECLARATIONS) return 1;
    if (rule->type == CSS_RULE_STYLE) {
        size_t count = 1;
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            count += active_rule_count(rule->data.style_rule.nested_rules[i], engine);
        }
        return count;
    }
    if (rule->type == CSS_RULE_IMPORT) {
        if (!css_import_rule_is_active(rule, engine)) return 0;
        CssStylesheet* imported = rule->data.import_rule.stylesheet;
        if (!imported || imported->disabled) return 0;
        size_t count = 0;
        for (size_t i = 0; i < imported->rule_count; i++) {
            count += active_rule_count(imported->rules[i], engine);
        }
        return count;
    }
    if (!conditional_rule_is_active(rule, engine)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
        count += active_rule_count(rule->data.conditional_rule.rules[i], engine);
    }
    return count;
}

static void active_rule_collect(CssRule* rule, CssEngine* engine,
                                CssRule** rules, size_t capacity, size_t* count) {
    if (!rule || !rules || !count) return;
    if (rule->type == CSS_RULE_SCOPE) {
        if (!rule->data.conditional_rule.invalid_scope && *count < capacity)
            rules[(*count)++] = rule;
        return;
    }
    if (rule->type == CSS_RULE_STYLE ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        if (*count < capacity) rules[(*count)++] = rule;
        if (rule->type == CSS_RULE_STYLE) {
            for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
                active_rule_collect(rule->data.style_rule.nested_rules[i],
                                    engine, rules, capacity, count);
            }
        }
        return;
    }
    if (rule->type == CSS_RULE_IMPORT) {
        if (!css_import_rule_is_active(rule, engine)) return;
        CssStylesheet* imported = rule->data.import_rule.stylesheet;
        if (imported && !imported->disabled) {
            for (size_t i = 0; i < imported->rule_count; i++) {
                active_rule_collect(imported->rules[i], engine,
                    rules, capacity, count);
            }
        }
        return;
    }
    if (!conditional_rule_is_active(rule, engine)) return;
    for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
        active_rule_collect(rule->data.conditional_rule.rules[i], engine,
                            rules, capacity, count);
    }
}

static void apply_active_rules_to_tree(DomElement* root, CssRule** rules,
                                       size_t rule_count, SelectorMatcher* matcher,
                                       Pool* pool, CssEngine* engine, int depth) {
    if (!root || !rules || !matcher || !pool || depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    // css_cascade is also linked without table layout by animation tests, so
    // query the persistent DOM flag directly instead of taking a layout symbol.
    if (!root->is_table_fixup()) {
        for (size_t i = 0; i < rule_count; i++) {
            apply_rule_to_element(root, rules[i], matcher, pool, engine);
        }
    }

    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            apply_active_rules_to_tree(lam::dom_require_element(child), rules,
                                       rule_count, matcher, pool, engine, depth + 1);
        }
    }
}

static void apply_stylesheet_reference_to_tree(DomElement* root,
                                               CssStylesheet* stylesheet,
                                               SelectorMatcher* matcher,
                                               Pool* pool, CssEngine* engine,
                                               int depth) {
    if (!root || !stylesheet || !matcher || !pool ||
        depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    if (!root->is_table_fixup()) {
        for (size_t i = 0; i < stylesheet->rule_count; i++) {
            apply_rule_to_element_with_nested(root, stylesheet->rules[i], matcher,
                                               pool, engine, 0);
        }
    }
    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            apply_stylesheet_reference_to_tree(lam::dom_require_element(child),
                                               stylesheet, matcher, pool, engine,
                                               depth + 1);
        }
    }
}

static void apply_stylesheet_to_tree(DomElement* root, CssStylesheet* stylesheet,
                                     SelectorMatcher* matcher, Pool* pool,
                                     CssEngine* engine, int depth) {
    if (!root || !stylesheet || !matcher || !pool || !engine) return;
    size_t count = 0;
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        count += active_rule_count(stylesheet->rules[i], engine);
    }
    if (count == 0) return;
    MemContext* context = root->doc ? (MemContext*)root->doc->services.mem_ctx : nullptr;
    Arena* scratch = mem_arena_create(context, MEM_ROLE_TEMP, "css.active_rule_program");
    if (!scratch) {
        // Allocation pressure must preserve the reference cascade semantics.
        apply_stylesheet_reference_to_tree(root, stylesheet, matcher, pool, engine, depth);
        return;
    }
    CssRule** rules = (CssRule**)arena_alloc(scratch, count * sizeof(CssRule*));
    if (!rules) {
        mem_arena_destroy(scratch);
        // The optimized rule program is optional scratch storage.
        apply_stylesheet_reference_to_tree(root, stylesheet, matcher, pool, engine, depth);
        return;
    }
    size_t written = 0;
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        active_rule_collect(stylesheet->rules[i], engine, rules, count, &written);
    }
    apply_active_rules_to_tree(root, rules, written, matcher, pool, engine, depth);
    mem_arena_destroy(scratch);
}

void radiant_apply_css_stylesheet_to_tree(DomElement* root,
                                          CssStylesheet* stylesheet,
                                          SelectorMatcher* matcher, Pool* pool,
                                          CssEngine* engine) {
    if (!root || !stylesheet || stylesheet->disabled || stylesheet->rule_count == 0 ||
        !matcher || !pool || !engine) {
        return;
    }

    css_layer_rank_stylesheets(root->doc, &stylesheet, 1, engine);
    bool epoch_scope = style_epoch_cascade_begin_extend(root->doc, root, engine);
    apply_stylesheet_to_tree(root, stylesheet, matcher, pool, engine, 0);
    if (epoch_scope) style_epoch_cascade_end(root->doc);
}

void radiant_apply_css_stylesheets_to_tree(DomDocument* doc, DomElement* root,
                                           CssStylesheet** stylesheets, int count,
                                           Pool* pool, CssEngine* engine,
                                           SelectorMatcher* matcher) {
    if (!doc || !root || !stylesheets || count <= 0 || !pool || !engine) return;
    if (!matcher) matcher = selector_matcher_create(pool);
    if (!matcher) return;

    css_layer_rank_stylesheets(doc, stylesheets, count, engine);
    bool epoch_scope = style_epoch_cascade_begin_replace(doc, root, engine);
    for (int i = 0; i < count; i++) {
        CssStylesheet* stylesheet = stylesheets[i];
        if (stylesheet && !stylesheet->disabled && !stylesheet->is_import_child &&
            stylesheet->rule_count > 0) {
            apply_stylesheet_to_tree(root, stylesheet, matcher, pool, engine, 0);
        }
    }
    if (epoch_scope) style_epoch_cascade_end(doc);
}

void radiant_cascade_styles_for_element_with_matcher(DomElement* element,
                                                      SelectorMatcher* matcher) {
    if (!element || !element->doc || !element->doc->document_pool || !matcher) return;

    DomDocument* doc = element->doc;
    Pool* pool = doc->document_pool;
    // Dynamic CSSOM nodes may never have entered layout, so their specified tree
    // still lacks stylesheet declarations when a transition samples the old value.
    dom_element_clear_cascaded_styles(element);

    CssEngine* engine = (CssEngine*)doc->services.cached_css_engine;
    if (!engine || doc->stylesheet_count <= 0) return;
    css_layer_rank_stylesheets(doc, doc->stylesheets, doc->stylesheet_count, engine);
    // CSSOM reads must see the same live form and interaction state as layout.
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets[i];
        if (!stylesheet || stylesheet->disabled || stylesheet->is_import_child) continue;
        for (size_t j = 0; j < stylesheet->rule_count; j++) {
            CssRule* rule = stylesheet->rules[j];
            if (rule) apply_rule_to_element(element, rule, matcher, pool, engine);
        }
    }
}

void radiant_cascade_styles_for_element(DomElement* element) {
    if (!element || !element->doc || !element->doc->document_pool) return;

    SelectorMatcher* matcher = selector_matcher_create(element->doc->document_pool);
    if (!matcher) return;
    radiant_cascade_styles_for_element_with_matcher(element, matcher);
    selector_matcher_destroy(matcher);
}
