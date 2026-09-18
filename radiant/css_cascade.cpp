#include "view.hpp"
#include "layout.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"

// CSS-only targets do not link StateStore; their matcher keeps default state.
__attribute__((weak)) void state_configure_selector_matcher(
        DocState* /*state*/, SelectorMatcher* /*matcher*/) {}

static void apply_rule_to_element(DomElement* element, CssRule* rule,
                                  SelectorMatcher* matcher, Pool* pool,
                                  CssEngine* engine) {
    if (!element || !rule || !matcher || !pool) return;

    bool nested_rule = rule->type == CSS_RULE_MEDIA ||
        rule->type == CSS_RULE_SUPPORTS || rule->type == CSS_RULE_LAYER;
    if (nested_rule) {
        // Conditional and layer blocks preserve source order while applying
        // their nested selector rules.
        bool enabled = rule->type == CSS_RULE_LAYER ||
            (rule->type == CSS_RULE_MEDIA
                ? css_evaluate_media_query(engine, rule->data.conditional_rule.condition)
                : css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition));
        if (enabled) {
            for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
                CssRule* nested = rule->data.conditional_rule.rules[i];
                if (nested) apply_rule_to_element(element, nested, matcher, pool, engine);
            }
        }
        return;
    }
    if (rule->type != CSS_RULE_STYLE) return;

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
                        result.specificity, (int)result.pseudo_element);
                }
            } else if (!matched_selector) {
                matched_selector = true;
                best_specificity = result.specificity;
            }
        }
        if (matched_selector && rule->data.style_rule.declaration_count > 0) {
            dom_element_apply_rule(element, rule, best_specificity);
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
                                              (int)result.pseudo_element);
    } else {
        dom_element_apply_rule(element, rule, result.specificity);
    }
}

void radiant_apply_css_rule_to_element(DomElement* element, CssRule* rule,
                                       SelectorMatcher* matcher, Pool* pool,
                                       CssEngine* engine) {
    apply_rule_to_element(element, rule, matcher, pool, engine);
}

static bool conditional_rule_is_active(CssRule* rule, CssEngine* engine) {
    if (!rule) return false;
    if (rule->type == CSS_RULE_LAYER) return true;
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
    if (rule->type == CSS_RULE_STYLE) return 1;
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
    if (rule->type == CSS_RULE_STYLE) {
        if (*count < capacity) rules[(*count)++] = rule;
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
            apply_rule_to_element(root, stylesheet->rules[i], matcher, pool, engine);
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

    bool epoch_scope = style_epoch_cascade_begin_replace(doc, root, engine);
    for (int i = 0; i < count; i++) {
        CssStylesheet* stylesheet = stylesheets[i];
        if (stylesheet && !stylesheet->disabled && stylesheet->rule_count > 0) {
            apply_stylesheet_to_tree(root, stylesheet, matcher, pool, engine, 0);
        }
    }
    if (epoch_scope) style_epoch_cascade_end(doc);
}

void radiant_cascade_styles_for_element(DomElement* element) {
    if (!element || !element->doc || !element->doc->document_pool) return;

    DomDocument* doc = element->doc;
    Pool* pool = doc->document_pool;
    // Dynamic CSSOM nodes may never have entered layout, so their specified tree
    // still lacks stylesheet declarations when a transition samples the old value.
    dom_element_clear_cascaded_styles(element);

    CssEngine* engine = (CssEngine*)doc->services.cached_css_engine;
    if (!engine || doc->stylesheet_count <= 0) return;
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return;
    // CSSOM reads must see the same live form and interaction state as layout.
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets[i];
        if (!stylesheet || stylesheet->disabled) continue;
        for (size_t j = 0; j < stylesheet->rule_count; j++) {
            CssRule* rule = stylesheet->rules[j];
            if (rule) apply_rule_to_element(element, rule, matcher, pool, engine);
        }
    }
    selector_matcher_destroy(matcher);
}
