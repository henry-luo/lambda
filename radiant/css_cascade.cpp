#include "view.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

static void apply_rule_to_element(DomElement* element, CssRule* rule,
                                  SelectorMatcher* matcher, Pool* pool,
                                  CssEngine* engine) {
    if (!element || !rule || !matcher || !pool) return;

    if (rule->type == CSS_RULE_MEDIA) {
        if (css_evaluate_media_query(engine, rule->data.conditional_rule.condition)) {
            for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
                CssRule* nested = rule->data.conditional_rule.rules[i];
                if (nested) apply_rule_to_element(element, nested, matcher, pool, engine);
            }
        }
        return;
    }
    if (rule->type == CSS_RULE_SUPPORTS || rule->type != CSS_RULE_STYLE) return;

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

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets[i];
        if (!stylesheet) continue;
        for (size_t j = 0; j < stylesheet->rule_count; j++) {
            CssRule* rule = stylesheet->rules[j];
            if (rule) apply_rule_to_element(element, rule, matcher, pool, engine);
        }
    }
    selector_matcher_destroy(matcher);
}
