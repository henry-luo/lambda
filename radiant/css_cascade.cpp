#include "view.hpp"
#include "layout.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/hashmap.h"
#include <stdlib.h>
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
        rule->type == CSS_RULE_SUPPORTS || rule->type == CSS_RULE_LAYER ||
        rule->type == CSS_RULE_CONTAINER;
    if (nested_rule) {
        // Conditional and layer blocks preserve source order while applying
        // their nested selector rules.
        bool enabled = (rule->type == CSS_RULE_LAYER &&
                !rule->data.conditional_rule.invalid_layer) ||
            (rule->type == CSS_RULE_CONTAINER
                ? css_evaluate_container_query(engine, element, rule->data.conditional_rule.condition)
                : rule->type == CSS_RULE_MEDIA
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
    if (rule->type == CSS_RULE_CONTAINER) return 1;
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
    if (rule->type == CSS_RULE_SCOPE || rule->type == CSS_RULE_CONTAINER) {
        if ((rule->type == CSS_RULE_CONTAINER || !rule->data.conditional_rule.invalid_scope) && *count < capacity)
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

// ---- rule program index -------------------------------------------------
// Matching the whole rule program against every element made each cascade
// O(elements x rules). Most style rules name an id, class or tag in their
// subject compound that an element lacks, so rules are bucketed by one such key
// and an element tests only the buckets its own keys select plus the unkeyed
// rules, in program order. Keys are ASCII-lowercased, which only widens a
// bucket; selector matching still decides every candidate.

enum CascadeRuleKeyKind : uint8_t { CASCADE_KEY_ID = 1, CASCADE_KEY_CLASS, CASCADE_KEY_TAG };

struct CascadeRuleBucket {
    uint32_t* rules;            // ascending program indices
    uint32_t count;
    uint32_t capacity;
};

struct CascadeRuleKey {         // hashmap item
    const char* key;            // lowercased, arena-owned (or a lookup buffer)
    uint32_t length;
    uint8_t kind;
    CascadeRuleBucket* bucket;
};

struct CascadeRuleIndex {
    Arena* arena;
    struct hashmap* buckets;
    CascadeRuleBucket universal;
    uint32_t* candidates;       // per-element scratch, one slot per rule
    uint32_t* seen;             // per-rule stamp of the element that collected it
    uint32_t stamp;
    uint32_t rule_count;
};

static uint64_t cascade_rule_key_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const CascadeRuleKey* key = (const CascadeRuleKey*)item;
    return hashmap_xxhash3(key->key, key->length, seed0, seed1) ^ key->kind;
}

static int cascade_rule_key_compare(const void* a, const void* b, void*) {
    const CascadeRuleKey* left = (const CascadeRuleKey*)a;
    const CascadeRuleKey* right = (const CascadeRuleKey*)b;
    if (left->kind != right->kind || left->length != right->length) return 1;
    return memcmp(left->key, right->key, left->length);
}

static bool cascade_bucket_append(Arena* arena, CascadeRuleBucket* bucket, uint32_t rule) {
    // a selector list can key one rule twice under the same name
    if (bucket->count && bucket->rules[bucket->count - 1] == rule) return true;
    if (bucket->count == bucket->capacity) {
        uint32_t capacity = bucket->capacity ? bucket->capacity * 2 : 8;
        uint32_t* grown = (uint32_t*)arena_alloc(arena, capacity * sizeof(uint32_t));
        if (!grown) return false;
        if (bucket->count) memcpy(grown, bucket->rules, bucket->count * sizeof(uint32_t));
        bucket->rules = grown;
        bucket->capacity = capacity;
    }
    bucket->rules[bucket->count++] = rule;
    return true;
}

// Lowercase `text` into `buffer` when it fits, else into the arena.
static char* cascade_lower_key(Arena* arena, const char* text, size_t length,
                               char* buffer, size_t buffer_size) {
    char* out = length < buffer_size ? buffer : (char*)arena_alloc(arena, length + 1);
    if (!out) return nullptr;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[length] = '\0';
    return out;
}

// The one id/class/tag the subject compound requires of the cascaded element.
// Compounds that describe another element (::slotted's slot, :host, ::part via
// the generic pseudo kinds) or a scope root are left unkeyed.
static bool cascade_selector_subject_key(const CssSelector* selector, uint8_t* kind,
                                         const char** value) {
    if (!selector || selector->compound_selector_count == 0) return false;
    const CssCompoundSelector* subject =
        selector->compound_selectors[selector->compound_selector_count - 1];
    if (!subject) return false;
    const char* id = nullptr;
    const char* class_name = nullptr;
    const char* tag = nullptr;
    for (size_t i = 0; i < subject->simple_selector_count; i++) {
        const CssSimpleSelector* simple = subject->simple_selectors[i];
        if (!simple) return false;
        switch (simple->type) {
        case CSS_SELECTOR_PSEUDO_SLOTTED:
        case CSS_SELECTOR_PSEUDO_GENERIC:
        case CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC:
        case CSS_SELECTOR_PSEUDO_SCOPE:
            return false;
        case CSS_SELECTOR_TYPE_ID: if (!id && simple->value && *simple->value) id = simple->value; break;
        case CSS_SELECTOR_TYPE_CLASS: if (!class_name && simple->value && *simple->value) class_name = simple->value; break;
        case CSS_SELECTOR_TYPE_ELEMENT: if (!tag && simple->value && *simple->value) tag = simple->value; break;
        default: break;
        }
    }
    if (id) { *kind = CASCADE_KEY_ID; *value = id; return true; }
    if (class_name) { *kind = CASCADE_KEY_CLASS; *value = class_name; return true; }
    if (tag) { *kind = CASCADE_KEY_TAG; *value = tag; return true; }
    return false;
}

static bool cascade_index_add_keyed(CascadeRuleIndex* index, uint8_t kind,
                                    const char* value, uint32_t rule) {
    size_t length = strlen(value);
    char* key = cascade_lower_key(index->arena, value, length, nullptr, 0);
    if (!key) return false;
    CascadeRuleKey probe = {key, (uint32_t)length, kind, nullptr};  // INT_CAST_OK: selector key length
    const CascadeRuleKey* found = (const CascadeRuleKey*)hashmap_get(index->buckets, &probe);
    CascadeRuleBucket* bucket = found ? found->bucket : nullptr;
    if (!bucket) {
        bucket = (CascadeRuleBucket*)arena_calloc(index->arena, sizeof(CascadeRuleBucket));
        if (!bucket) return false;
        probe.bucket = bucket;
        hashmap_set(index->buckets, &probe);
        if (hashmap_oom(index->buckets)) return false;
    }
    return cascade_bucket_append(index->arena, bucket, rule);
}

static bool cascade_index_add_rule(CascadeRuleIndex* index, CssRule* rule, uint32_t position) {
    // only selector rules carry a subject; scope, container and nested
    // declaration rules keep their per-element evaluation unkeyed.
    if (rule && rule->type == CSS_RULE_STYLE) {
        CssSelectorGroup* group = rule->data.style_rule.selector_group;
        size_t count = group && group->selector_count > 0 ? group->selector_count : 1;
        uint8_t kinds[8];
        const char* values[8];
        bool keyed = count <= 8;
        for (size_t i = 0; keyed && i < count; i++) {
            CssSelector* selector = group && group->selector_count > 0
                ? group->selectors[i] : rule->data.style_rule.selector;
            keyed = cascade_selector_subject_key(selector, &kinds[i], &values[i]);
        }
        if (keyed) {
            for (size_t i = 0; i < count; i++) {
                if (!cascade_index_add_keyed(index, kinds[i], values[i], position)) return false;
            }
            return true;
        }
    }
    return cascade_bucket_append(index->arena, &index->universal, position);
}

static CascadeRuleIndex* cascade_rule_index_build(Arena* arena, CssRule** rules,
                                                  size_t rule_count) {
    if (!arena || !rules || rule_count == 0 || rule_count > UINT32_MAX / 2) return nullptr;
    CascadeRuleIndex* index = (CascadeRuleIndex*)arena_calloc(arena, sizeof(CascadeRuleIndex));
    if (!index) return nullptr;
    index->arena = arena;
    index->rule_count = (uint32_t)rule_count;  // INT_CAST_OK: bounded above
    index->candidates = (uint32_t*)arena_alloc(arena, rule_count * sizeof(uint32_t));
    index->seen = (uint32_t*)arena_calloc(arena, rule_count * sizeof(uint32_t));
    index->buckets = hashmap_new(sizeof(CascadeRuleKey), 64, 0, 0,
        cascade_rule_key_hash, cascade_rule_key_compare, nullptr, nullptr);
    if (!index->candidates || !index->seen || !index->buckets) {
        if (index->buckets) hashmap_free(index->buckets);
        return nullptr;
    }
    for (size_t i = 0; i < rule_count; i++) {
        if (!cascade_index_add_rule(index, rules[i], (uint32_t)i)) {  // INT_CAST_OK: bounded above
            hashmap_free(index->buckets);
            return nullptr;
        }
    }
    return index;
}

static void cascade_rule_index_free(CascadeRuleIndex* index) {
    if (index && index->buckets) hashmap_free(index->buckets);
}

static void cascade_collect_bucket(CascadeRuleIndex* index, const CascadeRuleBucket* bucket,
                                   uint32_t* count) {
    for (uint32_t i = 0; bucket && i < bucket->count; i++) {
        uint32_t rule = bucket->rules[i];
        if (index->seen[rule] == index->stamp) continue;
        index->seen[rule] = index->stamp;
        index->candidates[(*count)++] = rule;
    }
}

static void cascade_collect_key(CascadeRuleIndex* index, uint8_t kind, const char* value,
                                uint32_t* count) {
    if (!value || !*value) return;
    char buffer[128];
    size_t length = strlen(value);
    char* key = cascade_lower_key(index->arena, value, length, buffer, sizeof(buffer));
    if (!key) {
        // without the key every bucket is a candidate; correctness first.
        size_t iter = 0;
        void* item;
        while (hashmap_iter(index->buckets, &iter, &item))
            cascade_collect_bucket(index, ((CascadeRuleKey*)item)->bucket, count);
        return;
    }
    CascadeRuleKey probe = {key, (uint32_t)length, kind, nullptr};  // INT_CAST_OK: attribute length
    const CascadeRuleKey* found = (const CascadeRuleKey*)hashmap_get(index->buckets, &probe);
    if (found) cascade_collect_bucket(index, found->bucket, count);
}

static int cascade_compare_rule_position(const void* a, const void* b) {
    uint32_t left = *(const uint32_t*)a, right = *(const uint32_t*)b;
    return left < right ? -1 : left > right;
}

// Candidate rules for `element` in program order.
static uint32_t cascade_rule_index_candidates(CascadeRuleIndex* index, DomElement* element) {
    if (++index->stamp == 0) {
        memset(index->seen, 0, index->rule_count * sizeof(uint32_t));
        index->stamp = 1;
    }
    uint32_t count = 0;
    cascade_collect_bucket(index, &index->universal, &count);
    cascade_collect_key(index, CASCADE_KEY_ID, element->id, &count);
    for (int i = 0; i < element->class_count; i++)
        cascade_collect_key(index, CASCADE_KEY_CLASS, element->class_names[i], &count);
    cascade_collect_key(index, CASCADE_KEY_TAG, element->local_name(), &count);
    if (count > 1) qsort(index->candidates, count, sizeof(uint32_t), cascade_compare_rule_position);
    return count;
}

static void apply_active_rules_to_tree(DomElement* root, CssRule** rules,
                                       size_t rule_count, SelectorMatcher* matcher,
                                       Pool* pool, CssEngine* engine, int depth,
                                       CascadeRuleIndex* index) {
    if (!root || !rules || !matcher || !pool || depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    // generated boxes read their host cascade rather than matching authored selectors.
    if (!root->is_synthetic()) {
        if (index) {
            uint32_t count = cascade_rule_index_candidates(index, root);
            for (uint32_t i = 0; i < count; i++) {
                apply_rule_to_element(root, rules[index->candidates[i]], matcher, pool, engine);
            }
        } else {
            for (size_t i = 0; i < rule_count; i++) {
                apply_rule_to_element(root, rules[i], matcher, pool, engine);
            }
        }
    }

    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            apply_active_rules_to_tree(lam::dom_require_element(child), rules,
                                       rule_count, matcher, pool, engine, depth + 1, index);
        }
    }
}

static bool cascade_stylesheet_is_active(CssStylesheet* sheet, bool include_import_child) {
    return sheet && !sheet->disabled && (include_import_child || !sheet->is_import_child);
}

static void apply_stylesheets_reference_to_tree(DomElement* root,
                                               CssStylesheet** sheets, int sheet_count,
                                               SelectorMatcher* matcher,
                                               Pool* pool, CssEngine* engine,
                                               int depth, bool include_import_child) {
    if (!root || !sheets || !matcher || !pool ||
        depth > MAX_RADIANT_CSS_TREE_DEPTH) return;

    if (!root->is_synthetic()) {
        for (int sheet = 0; sheet < sheet_count; sheet++) {
            CssStylesheet* stylesheet = sheets[sheet];
            if (!cascade_stylesheet_is_active(stylesheet, include_import_child)) continue;
            for (size_t i = 0; i < stylesheet->rule_count; i++) {
                apply_rule_to_element_with_nested(root, stylesheet->rules[i], matcher,
                                                   pool, engine, 0);
            }
        }
    }
    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            apply_stylesheets_reference_to_tree(lam::dom_require_element(child),
                                               sheets, sheet_count, matcher, pool, engine,
                                               depth + 1, include_import_child);
        }
    }
}

static void apply_stylesheets_to_tree(DomElement* root, CssStylesheet** sheets, int sheet_count,
                                     SelectorMatcher* matcher, Pool* pool,
                                     CssEngine* engine, int depth, bool include_import_child = false) {
    if (!root || !sheets || !matcher || !pool || !engine) return;
    size_t count = 0;
    for (int sheet = 0; sheet < sheet_count; sheet++) {
        CssStylesheet* stylesheet = sheets[sheet];
        if (!cascade_stylesheet_is_active(stylesheet, include_import_child)) continue;
        for (size_t i = 0; i < stylesheet->rule_count; i++) {
            count += active_rule_count(stylesheet->rules[i], engine);
        }
    }
    if (count == 0) return;
    MemContext* context = root->doc ? (MemContext*)root->doc->services.mem_ctx : nullptr;
    Arena* scratch = mem_arena_create(context, MEM_ROLE_TEMP, "css.active_rule_program");
    if (!scratch) {
        // Allocation pressure must preserve the reference cascade semantics.
        apply_stylesheets_reference_to_tree(root, sheets, sheet_count, matcher, pool, engine,
                                           depth, include_import_child);
        return;
    }
    CssRule** rules = (CssRule**)arena_alloc(scratch, count * sizeof(CssRule*));
    if (!rules) {
        mem_arena_destroy(scratch);
        // The optimized rule program is optional scratch storage.
        apply_stylesheets_reference_to_tree(root, sheets, sheet_count, matcher, pool, engine,
                                           depth, include_import_child);
        return;
    }
    size_t written = 0;
    for (int sheet = 0; sheet < sheet_count; sheet++) {
        CssStylesheet* stylesheet = sheets[sheet];
        if (!cascade_stylesheet_is_active(stylesheet, include_import_child)) continue;
        for (size_t i = 0; i < stylesheet->rule_count; i++) {
            active_rule_collect(stylesheet->rules[i], engine, rules, count, &written);
        }
    }
    // an index allocation failure keeps the unindexed program walk.
    CascadeRuleIndex* index = cascade_rule_index_build(scratch, rules, written);
    // finish every ancestor's cascade before a descendant reads its query font or variables.
    apply_active_rules_to_tree(root, rules, written, matcher, pool, engine, depth, index);
    cascade_rule_index_free(index);
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
    apply_stylesheets_to_tree(root, &stylesheet, 1, matcher, pool, engine, 0, true);
    if (epoch_scope) style_epoch_cascade_end(root->doc);
}

void radiant_apply_css_stylesheets_to_tree(DomDocument* doc, DomElement* root,
                                           CssStylesheet** stylesheets, int count,
                                           Pool* pool, CssEngine* engine,
                                           SelectorMatcher* matcher) {
    if (!doc || !root || !stylesheets || count <= 0 || !pool || !engine) return;
    // a caller-owned fallback keeps each full cascade from retaining a matcher
    SelectorMatcher matcher_storage;
    if (!matcher) {
        selector_matcher_init(&matcher_storage, pool);
        matcher = &matcher_storage;
        state_configure_selector_matcher((DocState*)doc->state, matcher);
    }

    css_layer_rank_stylesheets(doc, stylesheets, count, engine);
    bool epoch_scope = style_epoch_cascade_begin_replace(doc, root, engine);
    apply_stylesheets_to_tree(root, stylesheets, count, matcher, pool, engine, 0);
    if (epoch_scope) style_epoch_cascade_end(doc);
}

bool radiant_clear_cascaded_styles_visitor(DomNode* node, void*) {
    node->layout_dirty = true;
    if (!node->is_element()) return true;
    DomElement* element = lam::dom_require_element(node);
    // generated boxes borrow their host's pseudo cascade until layout rebinds
    // them; clearing or matching them as authored DOM erases that live snapshot.
    if (!element->is_synthetic()) {
        dom_element_clear_cascaded_styles(element);
        // pseudo declarations share the base cascade epoch.
        dom_element_clear_pseudo_styles(element);
        element->set_styles_resolved(false);
    }
    return true;
}

void radiant_recascade_document(DomDocument* doc) {
    if (!doc || !doc->root || !doc->document_pool) return;
    CssEngine* engine = (CssEngine*)doc->services.cached_css_engine;
    if (!engine) return;
    SelectorMatcher matcher;
    selector_matcher_init(&matcher, doc->document_pool);
    state_configure_selector_matcher((DocState*)doc->state, &matcher);
    radiant_recascade_document_styles(doc, doc->root, &matcher);
}

void radiant_cascade_styles_for_element_with_matcher(DomElement* element,
                                                      SelectorMatcher* matcher) {
    if (!element || !element->doc || !element->doc->document_pool || !matcher ||
        element->is_synthetic()) return;

    DomDocument* doc = element->doc;
    Pool* pool = doc->document_pool;
    // Dynamic CSSOM nodes may never have entered layout, so their specified tree
    // still lacks stylesheet declarations when a transition samples the old value.
    dom_element_clear_cascaded_styles(element);
    // vanished pseudo-selector branches must not survive a live CSSOM recascade;
    // retained generated boxes keep the retired borrow until their next layout.
    dom_element_clear_pseudo_styles(element);

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

void radiant_recascade_document_styles(DomDocument* document, DomElement* root,
                                      SelectorMatcher* matcher) {
    if (!document || !root) return;
    view_geometry_walk_dom_tree(root, radiant_clear_cascaded_styles_visitor, nullptr);
    radiant_apply_css_stylesheets_to_tree(document, root, document->stylesheets,
        document->stylesheet_count, document->document_pool,
        (CssEngine*)document->services.cached_css_engine, matcher);
}
