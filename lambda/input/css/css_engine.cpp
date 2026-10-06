#include "css_engine.hpp"
#include "css_value_parser.hpp"
#include "css_parser.hpp"
#include "css_style_node.hpp"
#include "selector_matcher.hpp"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include "../../../lib/mem_grow.hpp"
#include "../../../lib/mem_factory.h"
#include "../../../lib/str.h"
#include "../../../lib/hash.h"

static uint64_t css_condition_hash_bytes(const char* text, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

bool css_import_rule_is_active(CssRule* rule, CssEngine* engine) {
    if (!rule || rule->type != CSS_RULE_IMPORT || rule->data.import_rule.invalid) return false;
    if (rule->data.import_rule.supports &&
        !css_evaluate_supports_condition(engine, rule->data.import_rule.supports)) return false;
    return !rule->data.import_rule.media ||
        css_evaluate_media_query(engine, rule->data.import_rule.media);
}

typedef bool (*CssRegistrationVisitor)(void*, const CssPropertyRegistration*);

static bool css_visit_registrations_in_sheet(CssEngine* engine, CssStylesheet* sheet,
    CssRegistrationVisitor visitor, void* context, size_t depth);

static bool css_visit_registrations_in_rule(CssEngine* engine, CssRule* rule,
    CssRegistrationVisitor visitor, void* context, size_t depth) {
    if (!rule || depth > 512) return true;
    if (rule->type == CSS_RULE_PROPERTY)
        return visitor(context, &rule->data.property_rule);
    if (rule->type == CSS_RULE_IMPORT) {
        return !css_import_rule_is_active(rule, engine) ||
            css_visit_registrations_in_sheet(engine, rule->data.import_rule.stylesheet,
                visitor, context, depth + 1);
    }
    // Registrations are global within the document, including within @scope.
    bool active = rule->type == CSS_RULE_SCOPE || rule->type == CSS_RULE_LAYER ||
        (rule->type == CSS_RULE_MEDIA && css_evaluate_media_query(engine, rule->data.conditional_rule.condition)) ||
        (rule->type == CSS_RULE_SUPPORTS && css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition));
    if (!active) return true;
    CssRuleChildList children = css_rule_child_list(rule);
    for (size_t i = 0; children.count && i < *children.count; i++)
        if (!css_visit_registrations_in_rule(engine, (*children.rules)[i], visitor, context, depth + 1))
            return false;
    return true;
}

static bool css_visit_registrations_in_sheet(CssEngine* engine, CssStylesheet* sheet,
    CssRegistrationVisitor visitor, void* context, size_t depth) {
    if (!sheet || sheet->disabled || depth > 512 ||
        (sheet->media && !css_evaluate_media_query(engine, sheet->media))) return true;
    for (size_t i = 0; i < sheet->rule_count; i++)
        if (!css_visit_registrations_in_rule(engine, sheet->rules[i], visitor, context, depth + 1))
            return false;
    return true;
}

static bool css_index_property_registration(void* context, const CssPropertyRegistration* registration) {
    CssEngine* engine = (CssEngine*)context;
    size_t count = engine->property_registration_count;
    if (!lam::pool_grow_array(engine->pool, &engine->property_registrations,
        &engine->property_registration_capacity, count + 1, (size_t)8)) return false;
    engine->property_registrations[count] = {registration, count};
    engine->property_registration_count++;
    return true;
}

static int css_compare_property_registrations(const void* left, const void* right) {
    const CssPropertyRegistrationEntry* a = (const CssPropertyRegistrationEntry*)left;
    const CssPropertyRegistrationEntry* b = (const CssPropertyRegistrationEntry*)right;
    int names = strcmp(a->registration->name, b->registration->name);
    return names ? names : (a->source_order > b->source_order) - (a->source_order < b->source_order);
}

struct CssRegistrationLookup {const char* name; const CssPropertyRegistration* result;};

static bool css_lookup_property_registration(void* context, const CssPropertyRegistration* registration) {
    CssRegistrationLookup* lookup = (CssRegistrationLookup*)context;
    if (css_custom_property_name_matches(registration->name, lookup->name)) lookup->result = registration;
    return true;
}

void css_stylesheet_mark_changed(CssStylesheet* stylesheet) {
    // Imported edits invalidate the root sheet's index without scanning its rule tree.
    for (size_t depth = 0; stylesheet && depth <= 512; depth++, stylesheet = stylesheet->parent_stylesheet)
        stylesheet->mutation_generation++;
}

struct CssElementDeclarationQuery {
    CssEngine* engine;
    SelectorMatcher* matcher;
    DomElement* element;
    const char* property;
    CssDeclaration best;
    uint32_t order;
    uint32_t scope_proximity;
    bool found;
};

static void css_query_consider_declaration(CssElementDeclarationQuery* query,
    const CssDeclaration* declaration, CssSpecificity specificity, CssOrigin origin) {
    if (!declaration || !declaration->valid || !declaration->property_name) return;
    // SVG declaration queries need shorthand priority before projecting their resolved longhand value.
    bool marker_shorthand = str_icmp_cstr(declaration->property_name, "marker") == 0 &&
        (strcmp(query->property, "marker-start") == 0 || strcmp(query->property, "marker-mid") == 0 ||
         strcmp(query->property, "marker-end") == 0);
    bool font_shorthand = str_icmp_cstr(declaration->property_name, "font") == 0 &&
        css_font_shorthand_contains_property(query->property);
    if (!marker_shorthand && !font_shorthand && str_icmp_cstr(declaration->property_name, query->property) != 0) return;
    CssDeclaration candidate = *declaration;
    candidate.specificity = specificity;
    candidate.specificity.important = declaration->important;
    candidate.origin = origin;
    candidate.source_order = query->order++;
    candidate.scope_proximity = query->scope_proximity;
    if (!query->found || css_declaration_cascade_compare(&candidate, &query->best) >= 0) {
        query->best = candidate;
        query->found = true;
    }
}

static void css_query_element_rule(CssElementDeclarationQuery* query, CssRule* rule, size_t depth);

struct CssScopeQueryContext {CssElementDeclarationQuery* query; CssRule* rule; size_t depth;};

static void css_query_scope_root(void* data, uint32_t scope_proximity) {
    CssScopeQueryContext* context = (CssScopeQueryContext*)data;
    uint32_t saved = context->query->scope_proximity;
    context->query->scope_proximity = scope_proximity;
    CssRuleChildList children = css_rule_child_list(context->rule);
    for (size_t i = 0; children.count && i < *children.count; i++)
        css_query_element_rule(context->query, (*children.rules)[i], context->depth + 1);
    context->query->scope_proximity = saved;
}

static void css_query_element_rule(CssElementDeclarationQuery* query, CssRule* rule,
                                    size_t depth) {
    if (!rule || depth > 512) return;
    if (rule->type == CSS_RULE_SCOPE) {
        CssScopeQueryContext context = {query, rule, depth};
        css_scope_visit_roots(rule, query->element, query->matcher, css_query_scope_root, &context);
        return;
    }
    if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
        rule->type == CSS_RULE_LAYER) {
        bool active = rule->type == CSS_RULE_LAYER || (query->engine &&
            (rule->type == CSS_RULE_MEDIA
                ? css_evaluate_media_query(query->engine, rule->data.conditional_rule.condition)
                : css_evaluate_supports_condition(query->engine, rule->data.conditional_rule.condition)));
        if (active) for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            css_query_element_rule(query, rule->data.conditional_rule.rules[i], depth + 1);
        }
        return;
    }
    if (rule->type != CSS_RULE_STYLE && rule->type != CSS_RULE_NESTED_DECLARATIONS) return;
    CssSpecificity specificity = {};
    bool matched = false;
    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    size_t count = group ? group->selector_count : 1;
    for (size_t i = 0; i < count; i++) {
        CssSelector* selector = group ? group->selectors[i] : rule->data.style_rule.selector;
        MatchResult match = {};
        if (selector && !selector->specificity.inline_style && !selector->specificity.ids &&
            !selector->specificity.classes && !selector->specificity.elements) {
            selector->specificity = selector_matcher_calculate_specificity(query->matcher, selector);
        }
        if (selector && selector_matcher_matches(query->matcher, selector, query->element, &match) &&
            match.pseudo_element == PSEUDO_ELEMENT_NONE &&
            (!matched || css_specificity_compare(match.specificity, specificity) > 0)) {
            specificity = match.specificity;
            matched = true;
        }
    }
    if (matched) for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        css_query_consider_declaration(query, rule->data.style_rule.declarations[i],
                                       specificity, rule->origin);
    }
    CssRuleChildList children = css_rule_child_list(rule);
    for (size_t i = 0; children.count && i < *children.count; i++)
        css_query_element_rule(query, (*children.rules)[i], depth + 1);
}

static void css_query_element_sheet(CssElementDeclarationQuery* query, CssStylesheet* sheet,
                                     size_t depth) {
    if (!sheet || sheet->disabled || depth > 512) return;
    for (size_t i = 0; i < sheet->imported_count; i++) {
        css_query_element_sheet(query, sheet->imported_stylesheets[i], depth + 1);
    }
    for (size_t i = 0; i < sheet->rule_count; i++) {
        css_query_element_rule(query, sheet->rules[i], 0);
    }
}

bool css_select_element_declaration(CssEngine* engine, SelectorMatcher* matcher,
    DomElement* element, CssStylesheet** sheets, size_t sheet_count,
    CssDeclaration** inline_declarations, size_t inline_count,
    const char* property_name, CssDeclaration* result) {
    if (!matcher || !element || !property_name || !result) return false;
    CssElementDeclarationQuery query = {};
    query.engine = engine;
    query.matcher = matcher;
    query.element = element;
    query.property = property_name;
    for (size_t i = 0; sheets && i < sheet_count; i++) {
        css_query_element_sheet(&query, sheets[i], 0);
    }
    CssSpecificity inline_specificity = {1, 0, 0, 0, false};
    for (size_t i = 0; inline_declarations && i < inline_count; i++) {
        css_query_consider_declaration(&query, inline_declarations[i],
                                       inline_specificity, CSS_ORIGIN_AUTHOR);
    }
    if (query.found) *result = query.best;
    return query.found;
}

static uint64_t css_condition_environment_key(const CssEngine* engine,
                                              CssConditionKind kind) {
    if (!engine) return 0;
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t ratio = 0;
    memcpy(&width, &engine->context.viewport_width, sizeof(width));
    memcpy(&height, &engine->context.viewport_height, sizeof(height));
    memcpy(&ratio, &engine->context.device_pixel_ratio, sizeof(ratio));
    uint64_t key = width ^ (height << 1u) ^ (ratio << 7u);
    key ^= engine->context.print_media ? UINT64_C(0x6a09e667f3bcc909) : 0;
    key ^= engine->context.reduced_motion ? UINT64_C(0x9e3779b97f4a7c15) : 0;
    key ^= engine->context.high_contrast ? UINT64_C(0xbf58476d1ce4e5b9) : 0;
    const char* scheme = engine->context.color_scheme ? engine->context.color_scheme : "";
    key ^= css_condition_hash_bytes(scheme, strlen(scheme));
    if (kind == CSS_CONDITION_SUPPORTS) {
        key ^= engine->supports_css3 ? UINT64_C(0x94d049bb133111eb) : 0;
        key ^= engine->features.css_color_4 ? UINT64_C(0x2545f4914f6cdd1d) : 0;
        key ^= engine->features.css_logical_properties ? UINT64_C(0xd6e8feb86659fd93) : 0;
    }
    return key ? key : 1;
}

const CssPropertyRegistration* css_find_property_registration(CssEngine* engine,
    CssStylesheet** sheets, size_t sheet_count, const char* name) {
    if (!name || !sheets) return nullptr;
    uint64_t key = css_condition_environment_key(engine, CSS_CONDITION_SUPPORTS);
    key = hash_combine_u64(key, sheet_count);
    for (size_t i = 0; i < sheet_count; i++) {
        CssStylesheet* sheet = sheets[i];
        key = hash_combine_u64(key, (uintptr_t)sheet);
        if (!sheet) continue;
        key = hash_combine_u64(key, sheet->mutation_generation);
        key = hash_combine_u64(key, sheet->rule_count);
        key = hash_combine_u64(key, sheet->disabled | (sheet->is_import_child << 1));
        if (sheet->media) key = hash_combine_u64(key, css_condition_hash_bytes(sheet->media, strlen(sheet->media)));
    }
    if (engine && (!engine->property_registration_index_valid || engine->property_registration_key != key)) {
        engine->property_registration_index_valid = false;
        engine->property_registration_count = 0;
        bool complete = true;
        for (size_t i = 0; i < sheet_count && complete; i++) {
            if (!sheets[i] || sheets[i]->is_import_child) continue;
            complete = css_visit_registrations_in_sheet(engine, sheets[i], css_index_property_registration, engine, 0);
        }
        if (complete) {
            if (engine->property_registration_count > 1)
                qsort(engine->property_registrations, engine->property_registration_count,
                    sizeof(CssPropertyRegistrationEntry), css_compare_property_registrations);
            engine->property_registration_key = key;
            engine->property_registration_index_valid = true;
            engine->property_registration_rebuilds++;
        }
    }
    if (!engine || !engine->property_registration_index_valid) {
        CssRegistrationLookup lookup = {name, nullptr};
        for (size_t i = 0; i < sheet_count; i++) {
            if (!sheets[i] || sheets[i]->is_import_child) continue;
            css_visit_registrations_in_sheet(engine, sheets[i], css_lookup_property_registration, &lookup, 0);
        }
        return lookup.result;
    }
    // The last equal entry wins. Names retain their authored case.
    const char* body = strncmp(name, "--", 2) == 0 ? name + 2 : name;
    size_t low = 0, high = engine->property_registration_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const char* registered = engine->property_registrations[mid].registration->name;
        if (strncmp(registered, "--", 2) == 0) registered += 2;
        if (strcmp(registered, body) <= 0) low = mid + 1;
        else high = mid;
    }
    if (!low) return nullptr;
    const CssPropertyRegistration* result = engine->property_registrations[low - 1].registration;
    return css_custom_property_name_matches(result->name, name) ? result : nullptr;
}

struct CssScriptPropertyRegistration {
    CssPropertyRegistration registration;
    size_t name_length;
    CssScriptPropertyRegistration* next;
};

const CssPropertyRegistration* css_find_script_property_registration(DomDocument* doc,
    const char* name, size_t name_length) {
    if (!doc || !name) return nullptr;
    if (name_length >= 2 && name[0] == '-' && name[1] == '-') {
        name += 2;
        name_length -= 2;
    }
    for (CssScriptPropertyRegistration* entry = (CssScriptPropertyRegistration*)doc->services.registered_property_set;
         entry; entry = entry->next) {
        if (entry->name_length - 2 == name_length &&
            memcmp(entry->registration.name + 2, name, name_length) == 0) return &entry->registration;
    }
    return nullptr;
}

const CssPropertyRegistration* css_find_document_property_registration(DomDocument* doc,
    const char* name, size_t name_length) {
    if (!doc || !name) return nullptr;
    if (name_length == (size_t)-1) name_length = strlen(name);
    const CssPropertyRegistration* script = css_find_script_property_registration(doc, name, name_length);
    // Script registrations override every active stylesheet registration (Properties and Values API 1 §2.1).
    if (script || memchr(name, '\0', name_length)) return script;
    return css_find_property_registration((CssEngine*)doc->services.cached_css_engine,
        doc->stylesheets, (size_t)doc->stylesheet_count, name);
}

bool css_register_document_property(DomDocument* doc,
    const CssPropertyRegistration* source, size_t name_length) {
    if (!doc || !doc->document_pool || !source || !source->name || name_length <= 2 ||
        source->name[0] != '-' || source->name[1] != '-' ||
        css_find_script_property_registration(doc, source->name, name_length) ||
        !css_property_registration_is_valid(source)) return false;
    Pool* pool = doc->document_pool;
    CssScriptPropertyRegistration* entry = (CssScriptPropertyRegistration*)pool_calloc(pool, sizeof(CssScriptPropertyRegistration));
    if (!entry) return false;
    entry->registration = *source;
    CssPropertyRegistration* target = &entry->registration;
    // The document retains an owned definition after argument conversion and parser scratch are released (D4.5.1v4).
    target->name = pool_dup_n(pool, source->name, name_length);
    target->syntax = pool_strdup(pool, source->syntax);
    target->initial_text_length = source->initial_text_length ? source->initial_text_length :
        source->initial_text ? strlen(source->initial_text) : 0;
    target->initial_text = source->initial_text ? pool_dup_n(pool, source->initial_text,
        target->initial_text_length) : nullptr;
    target->initial_value = source->initial_value ? css_value_clone_owned(source->initial_value, pool) : nullptr;
    if (!target->name || !target->syntax || (source->initial_text && !target->initial_text) ||
        (source->initial_value && !target->initial_value)) return false;
    if (source->component_count) {
        target->components = (CssPropertySyntaxComponent*)pool_alloc(pool,
            source->component_count * sizeof(CssPropertySyntaxComponent));
        if (!target->components) return false;
        memcpy(target->components, source->components, source->component_count * sizeof(CssPropertySyntaxComponent));
        for (size_t i = 0; i < source->component_count; i++) {
            if (!source->components[i].identifier) continue;
            target->components[i].identifier = pool_strdup(pool, source->components[i].identifier);
            if (!target->components[i].identifier) return false;
        }
    }
    entry->name_length = name_length;
    entry->next = (CssScriptPropertyRegistration*)doc->services.registered_property_set;
    doc->services.registered_property_set = entry;
    return true;
}

static bool css_condition_cache_lookup(CssEngine* engine, CssConditionKind kind,
                                       const char* condition, bool* result) {
    if (!engine || !condition || !result) return false;
    size_t length = strlen(condition);
    uint64_t hash = css_condition_hash_bytes(condition, length);
    size_t slot = (size_t)(hash & (CSS_CONDITION_CACHE_CAPACITY - 1u));
    CssConditionCacheEntry* entry = &engine->condition_cache[slot];
    if (entry->kind != (uint8_t)kind || entry->condition_length != length ||
        entry->environment_key != css_condition_environment_key(engine, kind) ||
        !entry->condition || memcmp(entry->condition, condition, length) != 0) return false;
    *result = entry->result != 0;
    engine->condition_cache_hits++;
    return true;
}

static void css_condition_cache_store(CssEngine* engine, CssConditionKind kind,
                                      const char* condition, bool result) {
    if (!engine || !condition) return;
    size_t length = strlen(condition);
    if (length > CSS_CONDITION_CACHE_MAX_TEXT_BYTES) return;
    uint64_t hash = css_condition_hash_bytes(condition, length);
    CssConditionCacheEntry* entry = &engine->condition_cache[
        (size_t)(hash & (CSS_CONDITION_CACHE_CAPACITY - 1u))];
    char* copy = pool_dup_n(engine->pool, condition, length);
    if (!copy) return;
    pool_free(engine->pool, (void*)entry->condition);
    entry->condition = copy;
    entry->condition_length = (uint32_t)length;
    entry->environment_key = css_condition_environment_key(engine, kind);
    entry->kind = (uint8_t)kind;
    entry->result = result ? 1 : 0;
}

// Enhanced CSS Engine creation
CssEngine* css_engine_create(Pool* pool) {
    // Note: CssValue size is 24 bytes on 64-bit systems (1 byte enum + 7 padding + 16 byte union)
    // The union size is determined by the largest member (16 bytes - either pointer+int for list, or color struct)
    // Color components (HSLA/HWBA/LABA/LCHA) use a pointer to reduce union size from 40 to 16 bytes
    static_assert(sizeof(CssValue) == 24, "Expected CssValue to be 24 bytes on 64-bit systems");

    if (!pool) return NULL;

    // Initialize CSS property system FIRST (required for property lookups)
    if (!css_property_system_init(pool)) {
        log_error("Failed to initialize CSS property system");
        return NULL;
    }

    CssEngine* engine = (CssEngine*)pool_calloc(pool, sizeof(CssEngine));
    if (!engine) return NULL;

    engine->pool = pool;

    // Initialize core components
    // Removed: engine->selector_parser (legacy linked-list parser removed)
    engine->value_parser = css_property_value_parser_create(pool);

    // Initialize style storage
    engine->style_tree = avl_tree_create(pool);
    engine->style_engine = css_style_engine_create(pool);

    // Enable all CSS3+ features by default
    engine->features.css_nesting = true;
    engine->features.css_cascade_layers = true;
    engine->features.css_container_queries = true;
    engine->features.css_scope = true;
    engine->features.css_custom_selectors = true;
    engine->features.css_mixins = false;  // Experimental
    engine->features.css_color_4 = true;
    engine->features.css_logical_properties = true;
    engine->features.css_subgrid = true;
    engine->features.css_anchor_positioning = false; // Experimental

    // Configure performance options
    engine->performance.cache_parsed_selectors = true;
    engine->performance.cache_computed_values = true;
    engine->performance.optimize_specificity = true;
    engine->performance.parallel_parsing = false; // Not implemented yet
    engine->performance.max_cache_size = 1000;

    // Set default document context
    engine->context.base_url = "";
    engine->context.document_charset = "UTF-8";
    engine->context.color_scheme = "auto";
    engine->context.viewport_width = 1920.0;
    engine->context.viewport_height = 1080.0;
    engine->context.device_pixel_ratio = 1.0;
    engine->context.root_font_size = 16.0;
    engine->context.print_media = false;
    engine->context.quirks_mode = false;
    engine->context.reduced_motion = false;
    engine->context.high_contrast = false;

    // Initialize statistics
    memset(&engine->stats, 0, sizeof(engine->stats));

    return engine;
}

void css_engine_destroy(CssEngine* engine) {
    if (!engine) return;

    for (size_t i = 0; i < CSS_CONDITION_CACHE_CAPACITY; i++) {
        pool_free(engine->pool, (void*)engine->condition_cache[i].condition);
        engine->condition_cache[i].condition = nullptr;
    }

    pool_free(engine->pool, engine->property_registrations);
    engine->property_registrations = nullptr;
    engine->property_registration_count = engine->property_registration_capacity = 0;
    engine->property_registration_index_valid = false;

    // Cleanup components
    // Removed: css_selector_parser_destroy (legacy parser removed)
    css_property_value_parser_destroy(engine->value_parser);

    if (engine->style_tree) {
        avl_tree_destroy(engine->style_tree);
    }

    if (engine->style_engine) {
        css_style_engine_destroy(engine->style_engine);
    }

    // Engine itself is pool-allocated, so it will be cleaned up with the pool
}

// Configuration functions
void css_engine_enable_feature(CssEngine* engine, const char* feature_name, bool enabled) {
    if (!engine || !feature_name) return;

    if (strcmp(feature_name, "css-nesting") == 0) {
        engine->features.css_nesting = enabled;
    } else if (strcmp(feature_name, "cascade-layers") == 0) {
        engine->features.css_cascade_layers = enabled;
    } else if (strcmp(feature_name, "container-queries") == 0) {
        engine->features.css_container_queries = enabled;
    } else if (strcmp(feature_name, "css-scope") == 0) {
        engine->features.css_scope = enabled;
    } else if (strcmp(feature_name, "custom-selectors") == 0) {
        engine->features.css_custom_selectors = enabled;
    } else if (strcmp(feature_name, "css-mixins") == 0) {
        engine->features.css_mixins = enabled;
    } else if (strcmp(feature_name, "css-color-4") == 0) {
        engine->features.css_color_4 = enabled;
    } else if (strcmp(feature_name, "logical-properties") == 0) {
        engine->features.css_logical_properties = enabled;
    } else if (strcmp(feature_name, "css-subgrid") == 0) {
        engine->features.css_subgrid = enabled;
    } else if (strcmp(feature_name, "anchor-positioning") == 0) {
        engine->features.css_anchor_positioning = enabled;
    }
}

void css_engine_set_viewport(CssEngine* engine, double width, double height) {
    if (!engine) return;

    engine->context.viewport_width = width;
    engine->context.viewport_height = height;
}

void css_engine_set_color_scheme(CssEngine* engine, const char* scheme) {
    if (!engine || !scheme) return;

    // retain the scheme in the engine-owned pool
    char* scheme_copy = pool_strdup(engine->pool, scheme);
    if (scheme_copy) {
        engine->context.color_scheme = scheme_copy;
    }
}

void css_engine_set_root_font_size(CssEngine* engine, double size) {
    if (!engine || size <= 0) return;

    engine->context.root_font_size = size;
}

// Enhanced CSS parsing
static const char* css_stylesheet_namespace_lookup(void* context,
                                                    const char* prefix) {
    CssStylesheet* stylesheet = (CssStylesheet*)context;
    if (!stylesheet) return NULL;
    for (size_t i = stylesheet->namespace_count; i > 0; i--) {
        const char* declared = stylesheet->namespaces[i - 1].prefix;
        if ((!prefix && !declared) ||
            (prefix && declared && strcmp(prefix, declared) == 0)) {
            return stylesheet->namespaces[i - 1].url;
        }
    }
    return NULL;
}

static bool css_bind_selector_group_namespaces(CssSelectorGroup* group, CssStylesheet* stylesheet) {
    for (size_t i = 0; group && i < group->selector_count; i++)
        if (!css_resolve_selector_namespaces(group->selectors[i],
                css_stylesheet_namespace_lookup, stylesheet)) return false;
    return true;
}

bool css_bind_rule_namespaces(CssRule* rule, CssStylesheet* stylesheet) {
    if (!rule) return true;
    if (rule->type == CSS_RULE_STYLE || rule->type == CSS_RULE_NESTING ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        CssSelectorGroup* group = rule->data.style_rule.selector_group;
        if (group) {
            if (!css_bind_selector_group_namespaces(group, stylesheet)) return false;
        } else if (!css_resolve_selector_namespaces(rule->data.style_rule.selector,
                       css_stylesheet_namespace_lookup, stylesheet)) {
            return false;
        }
    }
    if (rule->type == CSS_RULE_SCOPE) {
        CssSelectorGroup* groups[] = {rule->data.conditional_rule.scope_start,
            rule->data.conditional_rule.scope_end};
        for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++)
            if (!css_bind_selector_group_namespaces(groups[g], stylesheet)) return false;
    }
    CssRuleChildList children = css_rule_child_list(rule);
    if (children.count) {
        size_t write = 0;
        for (size_t i = 0; i < *children.count; i++) {
            CssRule* nested = (*children.rules)[i];
            if (css_bind_rule_namespaces(nested, stylesheet)) {
                (*children.rules)[write++] = nested;
            }
        }
        *children.count = write;
    }
    return true;
}

CssStylesheet* css_enhanced_parse_stylesheet(CssEngine* engine,
    const char* css_text, const char* base_url) {
    if (!engine || !css_text) return NULL;

    clock_t start_time = clock();

    log_debug("Starting enhanced CSS parsing: %zu chars, base_url=%s", strlen(css_text), base_url ? base_url : "(none)");

    CssStylesheet* stylesheet = (CssStylesheet*)pool_calloc(engine->pool, sizeof(CssStylesheet));
    if (!stylesheet) {
        log_error("Failed to allocate CSS stylesheet");
        return NULL;
    }

    // Set stylesheet metadata
    if (base_url) {
        char* url_copy = pool_strdup(engine->pool, base_url);
        if (url_copy) {
            stylesheet->origin_url = url_copy;
        }
    }

    // Initialize rule storage
    stylesheet->rule_capacity = 64;
    stylesheet->rules = (CssRule**)pool_alloc(engine->pool, stylesheet->rule_capacity * sizeof(CssRule*));

    // Token records and lexemes have no stylesheet lifetime. A dedicated pool
    // returns its VM extents after the semantic parser has copied retained data.
    Pool* token_pool = pool_create();
    if (!token_pool) {
        log_error("CSS tokenizer scratch pool allocation failed");
        return stylesheet;
    }
    CssTokenizer* tokenizer = css_tokenizer_create(token_pool);
    if (!tokenizer) {
        log_error("CSS tokenizer creation failed");
        pool_destroy(token_pool);
        return stylesheet;
    }

    // Tokenize the CSS.
    CssToken* tokens;
    int token_count = css_tokenizer_tokenize(tokenizer, css_text, strlen(css_text), &tokens);

    if (token_count <= 0) {
        log_debug("CSS tokenization returned %d tokens", token_count);
        clock_t end_time = clock();
        stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        engine->stats.stylesheets_parsed++;
        pool_destroy(token_pool);
        return stylesheet;
    }

    // Parse rules from tokens
    log_debug("Parsing CSS rules from %d tokens", token_count);

    int token_index = 0;
    int namespace_capacity = 0;
    bool namespace_allowed = true;
    bool import_allowed = true;
    bool saw_import = false;
    while (token_index < token_count) {
        // Skip whitespace between rules
        while (token_index < token_count &&
               (tokens[token_index].type == CSS_TOKEN_WHITESPACE ||
                tokens[token_index].type == CSS_TOKEN_COMMENT ||
                tokens[token_index].type == CSS_TOKEN_CDC ||
                tokens[token_index].type == CSS_TOKEN_CDO)) {
            token_index++;
        }

        if (token_index >= token_count) break;

        // Skip EOF token - nothing left to parse
        if (tokens[token_index].type == CSS_TOKEN_EOF) {
            break;
        }

        // Per CSS syntax spec: discard rules whose prelude starts with <dashed-ident><colon>
        // These look like custom property declarations and should be ignored at top level
        if (tokens[token_index].type == CSS_TOKEN_CUSTOM_PROPERTY) {
            int next = token_index + 1;
            while (next < (int)token_count && tokens[next].type == CSS_TOKEN_WHITESPACE) next++;
            if (next < (int)token_count && tokens[next].type == CSS_TOKEN_COLON) {
                // skip to end of block or semicolon
                int brace_depth = 0;
                token_index = next + 1;
                while (token_index < (int)token_count) {
                    if (tokens[token_index].type == CSS_TOKEN_LEFT_BRACE) {
                        brace_depth++;
                    } else if (tokens[token_index].type == CSS_TOKEN_RIGHT_BRACE) {
                        if (brace_depth > 0) { brace_depth--; if (brace_depth == 0) { token_index++; break; } }
                        else break;
                    } else if (brace_depth == 0 && tokens[token_index].type == CSS_TOKEN_SEMICOLON) {
                        token_index++;
                        break;
                    }
                    token_index++;
                }
                continue;
            }
        }

        // Parse a rule
        CssRule* rule = NULL;
        int tokens_consumed = css_parse_rule_from_tokens_internal_mode(
            tokens + token_index, token_count - token_index, engine->pool,
            &rule, engine->context.quirks_mode);

        if (tokens_consumed > 0) {
            token_index += tokens_consumed;

            if (rule) {
                if (rule->type == CSS_RULE_LAYER &&
                    rule->data.conditional_rule.invalid_layer) continue;
                if (rule->type == CSS_RULE_IMPORT) {
                    if (!import_allowed || rule->data.import_rule.invalid) continue;
                    saw_import = true;
                } else if (rule->type == CSS_RULE_LAYER &&
                           rule->data.conditional_rule.layer_statement &&
                           !rule->data.conditional_rule.invalid_layer) {
                    // A layer order statement may precede imports, but one
                    // between imports ends their consecutive prelude.
                    if (saw_import) {
                        import_allowed = false;
                        namespace_allowed = false;
                    }
                } else if (rule->type != CSS_RULE_CHARSET) {
                    import_allowed = false;
                }
                if (rule->type == CSS_RULE_NAMESPACE) {
                    if (!namespace_allowed) continue;
                    if (stylesheet->namespace_count >= (size_t)namespace_capacity &&
                        !lam::pool_copy_grow_array(engine->pool,
                            &stylesheet->namespaces, &namespace_capacity,
                            (int)stylesheet->namespace_count,
                            (int)stylesheet->namespace_count + 1, 4, false)) {
                        continue;
                    }
                    stylesheet->namespaces[stylesheet->namespace_count].prefix =
                        rule->data.namespace_rule.prefix;
                    stylesheet->namespaces[stylesheet->namespace_count].url =
                        rule->data.namespace_rule.namespace_url;
                    stylesheet->namespace_count++;
                } else {
                    if (rule->type != CSS_RULE_IMPORT &&
                        rule->type != CSS_RULE_CHARSET &&
                        !(rule->type == CSS_RULE_LAYER &&
                          rule->data.conditional_rule.layer_statement &&
                          !rule->data.conditional_rule.invalid_layer &&
                          !saw_import)) namespace_allowed = false;
                    if (!css_bind_rule_namespaces(rule, stylesheet)) continue;
                }
                // Add rule to stylesheet
                if (stylesheet->rule_count >= stylesheet->rule_capacity) {
                    (void)lam::pool_copy_grow_array(engine->pool, &stylesheet->rules,
                        &stylesheet->rule_capacity, stylesheet->rule_count,
                        stylesheet->rule_count + 1, 64, false);
                }

                if (stylesheet->rule_count < stylesheet->rule_capacity) {
                    css_rule_attach(rule, NULL, stylesheet);
                    stylesheet->rules[stylesheet->rule_count++] = rule;

                    // Update feature usage flags
                    css_enhanced_detect_features_in_rule(stylesheet, rule);
                }
            }
        } else {
            // Failed to parse, skip to next rule
            log_debug("CSS: Failed to parse rule at token %d, skipping", token_index);

            // We need to skip to the end of this failed rule's declaration block
            // Look for the opening brace first (in case we failed before it)
            int brace_depth = 0;
            bool found_open_brace = false;

            // Search for opening brace
            while (token_index < token_count && !found_open_brace) {
                if (tokens[token_index].type == CSS_TOKEN_LEFT_BRACE) {
                    found_open_brace = true;
                    brace_depth = 1;
                    token_index++;
                    break;
                } else if (tokens[token_index].type == CSS_TOKEN_SEMICOLON) {
                    // Hit a semicolon before opening brace, might be @-rule
                    token_index++;
                    break;
                }
                token_index++;
            }

            // If we found an opening brace, skip to matching closing brace
            if (found_open_brace) {
                while (token_index < token_count && brace_depth > 0) {
                    if (tokens[token_index].type == CSS_TOKEN_LEFT_BRACE) {
                        brace_depth++;
                    } else if (tokens[token_index].type == CSS_TOKEN_RIGHT_BRACE) {
                        brace_depth--;
                    }
                    token_index++;
                }
            }
        }
    }

    log_debug("Parsed %zu CSS rules", stylesheet->rule_count);

    clock_t end_time = clock();
    stylesheet->parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    // Update engine statistics
    engine->stats.rules_parsed += stylesheet->rule_count;
    engine->stats.stylesheets_parsed++;
    engine->stats.parse_time += stylesheet->parse_time;
    // Rules retain copied semantic values; release parser scratch and its VM extents.
    css_token_array_release(token_pool, tokens, (size_t)token_count);
    pool_destroy(token_pool);
    log_debug("Finished enhanced CSS parsing");

    return stylesheet;
}

// Feature detection in rules
void css_enhanced_detect_features_in_rule(CssStylesheet* stylesheet, CssRule* rule) {
    if (!stylesheet || !rule) return;

    // Legacy selector_list field removed - nesting detection moved to modern selector_group
    // TODO: Implement nesting detection using CssSelectorGroup* format

    // Check for custom properties and other features
    for (int i = 0; i < (int)rule->property_count; i++) {
        CssValue* value = rule->property_values[i];
        if (value) {

        }
    }
}

void css_engine_print_stats(CssEngine* engine) {
    if (!engine) return;

    log_info("css engine stats: rules_parsed=%zu selectors_cached=%zu values_computed=%zu cascade_calcs=%zu",
             engine->stats.rules_parsed, engine->stats.selectors_cached,
             engine->stats.values_computed, engine->stats.cascade_calculations);
    log_info("css engine timing: parse=%.4fs cascade=%.4fs memory=%zu bytes",
             engine->stats.parse_time, engine->stats.cascade_time, engine->stats.memory_usage);
    log_info("css features: nesting=%d layers=%d containers=%d scope=%d color4=%d",
             engine->features.css_nesting, engine->features.css_cascade_layers,
             engine->features.css_container_queries, engine->features.css_scope,
             engine->features.css_color_4);
}

double css_engine_get_parse_time(CssEngine* engine) {
    return engine ? engine->stats.parse_time : 0.0;
}

size_t css_engine_get_memory_usage(CssEngine* engine) {
    return engine ? engine->stats.memory_usage : 0;
}

CssStyleEngine* css_style_engine_create(Pool* pool) {
    if (!pool) return NULL;

    CssStyleEngine* engine = (CssStyleEngine*)pool_calloc(pool, sizeof(CssStyleEngine));
    if (!engine) return NULL;

    engine->pool = pool;
    engine->version = 1;

    return engine;
}

void css_style_engine_destroy(CssStyleEngine* engine) {
    (void)engine;
    // Memory managed by pool
}

// ============================================================================
// Media Query Evaluation
// ============================================================================

/**
 * Parse a length value from a media query condition.
 * Returns the value in pixels, or -1 on error.
 */
static double parse_media_length(const char* value) {
    if (!value) return -1;

    // Skip whitespace
    while (*value && (*value == ' ' || *value == '\t')) value++;

    char* end;
    double num = strtod(value, &end);
    if (end == value) return -1;  // No number found

    // Skip whitespace before unit
    while (*end && (*end == ' ' || *end == '\t')) end++;

    double pixels = -1;
    if (str_ieq_cstr(end, "px")) pixels = num;
    else if (str_ieq_cstr(end, "em") || str_ieq_cstr(end, "rem"))
        pixels = num * 16.0;  // media relative units use the initial font size.
    else if (*end == '\0' && num == 0.0) pixels = 0.0;
    return pixels >= 0.0 && isfinite(pixels) ? pixels : -1;
}

typedef enum CssMediaNumericKind {
    CSS_MEDIA_NUMERIC_UNKNOWN = 0,
    CSS_MEDIA_NUMERIC_LENGTH,
    CSS_MEDIA_NUMERIC_RATIO,
    CSS_MEDIA_NUMERIC_RESOLUTION
} CssMediaNumericKind;

static bool css_media_numeric_feature(CssEngine* engine, const char* name,
                                      CssMediaNumericKind* kind, double* value) {
    if (strcmp(name, "width") == 0 || strcmp(name, "height") == 0) {
        *kind = CSS_MEDIA_NUMERIC_LENGTH;
        *value = strcmp(name, "width") == 0
            ? engine->context.viewport_width : engine->context.viewport_height;
        return true;
    }
    if (strcmp(name, "aspect-ratio") == 0) {
        *kind = CSS_MEDIA_NUMERIC_RATIO;
        *value = engine->context.viewport_height > 0.0
            ? engine->context.viewport_width / engine->context.viewport_height : 0.0;
        return true;
    }
    if (strcmp(name, "resolution") == 0) {
        *kind = CSS_MEDIA_NUMERIC_RESOLUTION;
        *value = engine->context.device_pixel_ratio;
        return true;
    }
    return false;
}

static double css_media_numeric_value(CssMediaNumericKind kind, const char* text) {
    if (kind == CSS_MEDIA_NUMERIC_LENGTH) return parse_media_length(text);
    if (!text || !*text) return -1.0;
    char* end = nullptr;
    double numerator = strtod(text, &end);
    if (end == text || numerator < 0.0 || !isfinite(numerator)) return -1.0;
    if (kind == CSS_MEDIA_NUMERIC_RATIO) {
        while (*end == ' ' || *end == '\t') end++;
        if (*end == '\0') return numerator;
        if (*end != '/') return -1.0;
        char* denominator_end = nullptr;
        double denominator = strtod(end + 1, &denominator_end);
        while (*denominator_end == ' ' || *denominator_end == '\t')
            denominator_end++;
        return denominator_end != end + 1 && *denominator_end == '\0' &&
            denominator > 0.0 && isfinite(denominator)
            ? numerator / denominator : -1.0;
    }
    if (kind == CSS_MEDIA_NUMERIC_RESOLUTION && numerator > 0.0) {
        if (strcmp(end, "dppx") == 0 || strcmp(end, "x") == 0)
            return numerator;
        if (strcmp(end, "dpi") == 0) return numerator / 96.0;
        if (strcmp(end, "dpcm") == 0) return numerator * 2.54 / 96.0;
    }
    return -1.0;
}

/**
 * Evaluate a single media feature condition like "(min-width: 768px)"
 */
static bool evaluate_media_feature(CssEngine* engine, const char* feature, const char* value) {
    if (!engine || !feature) return false;

#ifdef RADIANT_TRACE_MEDIA_QUERY
    // Media query feature tracing is opt-in; cascade evaluates these for many
    // element/rule pairs and can otherwise dominate page-load time.
    log_debug("[Media Query] Evaluating feature: %s = %s", feature, value ? value : "(no value)");
#endif

    double viewport_width = engine->context.viewport_width;
    double viewport_height = engine->context.viewport_height;

    const char* numeric_name = feature;
    int numeric_comparison = 0;
    if (strncmp(feature, "min-", 4) == 0) {
        numeric_name += 4;
        numeric_comparison = 1;
    } else if (strncmp(feature, "max-", 4) == 0) {
        numeric_name += 4;
        numeric_comparison = -1;
    }
    CssMediaNumericKind numeric_kind = CSS_MEDIA_NUMERIC_UNKNOWN;
    double actual = 0.0;
    if (css_media_numeric_feature(engine, numeric_name, &numeric_kind, &actual)) {
        double requested = css_media_numeric_value(numeric_kind, value);
        if (requested < 0.0 || !isfinite(requested)) return false;
        if (numeric_comparison > 0) return actual >= requested;
        if (numeric_comparison < 0) return actual <= requested;
        return actual == requested;
    }

    // orientation
    if (strcmp(feature, "orientation") == 0) {
        if (!value) return false;
        if (strcmp(value, "portrait") == 0) {
            return viewport_height >= viewport_width;
        } else if (strcmp(value, "landscape") == 0) {
            return viewport_width > viewport_height;
        }
        return false;
    }

    // prefers-color-scheme
    if (strcmp(feature, "prefers-color-scheme") == 0) {
        if (!value || !engine->context.color_scheme) return false;
        // Treat "auto" as "light" (standard default for web content)
        const char* effective_scheme = engine->context.color_scheme;
        if (strcmp(effective_scheme, "auto") == 0) {
            effective_scheme = "light";
        }
        return strcmp(effective_scheme, value) == 0;
    }

    // prefers-reduced-motion
    if (strcmp(feature, "prefers-reduced-motion") == 0) {
        if (!value) return false;
        if (strcmp(value, "reduce") == 0) {
            return engine->context.reduced_motion;
        } else if (strcmp(value, "no-preference") == 0) {
            return !engine->context.reduced_motion;
        }
        return false;
    }

    // Unknown feature - assume it doesn't match
    log_debug("[Media Query] Unknown feature: %s", feature);
    return false;
}

/**
 * Evaluate a media type like "screen", "print", "all"
 */
static bool evaluate_media_type(const CssEngine* engine, const char* type) {
    if (!type) return true;  // No type specified = matches all

    // Skip leading whitespace
    while (*type && (*type == ' ' || *type == '\t')) type++;

    // Check known media types
    if (str_ieq_cstr(type, "all")) return true;
    if (str_ieq_cstr(type, "screen")) return !engine->context.print_media;
    if (str_ieq_cstr(type, "print")) return engine->context.print_media;
    if (str_ieq_cstr(type, "speech")) return false;

    // Unknown media types cannot match a screen device.
    return false;
}

/**
 * Evaluate a complete media query string.
 *
 * Supports:
 * - Media types: screen, print, all
 * - Features: min-width, max-width, min-height, max-height, orientation
 * - Logical operators: and, not, or (via comma)
 * - Parenthesized feature conditions
 *
 * Examples:
 * - "screen"
 * - "screen and (min-width: 768px)"
 * - "(min-width: 768px) and (max-width: 1024px)"
 * - "screen, print"
 */
typedef struct CssConditionSpan {
    const char* start;
    size_t length;
} CssConditionSpan;

static CssConditionSpan css_condition_trim(CssConditionSpan span) {
    while (span.length > 0 &&
           (span.start[0] == ' ' || span.start[0] == '\t' ||
            span.start[0] == '\n' || span.start[0] == '\r')) {
        span.start++;
        span.length--;
    }
    while (span.length > 0) {
        char last = span.start[span.length - 1];
        if (last != ' ' && last != '\t' && last != '\n' && last != '\r') break;
        span.length--;
    }
    return span;
}

static bool css_condition_outer_parens(CssConditionSpan* span) {
    if (!span || span->length < 2 || span->start[0] != '(' ||
        span->start[span->length - 1] != ')') return false;
    int depth = 0;
    for (size_t i = 0; i < span->length; i++) {
        char c = span->start[i];
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0 && i != span->length - 1) return false;
            if (depth < 0) return false;
        }
    }
    if (depth != 0) return false;
    span->start++;
    span->length -= 2;
    *span = css_condition_trim(*span);
    return true;
}

static bool css_condition_find_operator(CssConditionSpan span, const char* op,
                                        size_t* out_pos, size_t* out_len) {
    if (!op || !out_pos || !out_len) return false;
    size_t op_len = strlen(op);
    int depth = 0;
    for (size_t i = 0; i + op_len <= span.length; i++) {
        char c = span.start[i];
        if (c == '(') {
            depth++;
            continue;
        }
        if (c == ')') {
            if (depth > 0) depth--;
            continue;
        }
        if (depth != 0 || !str_ieq(span.start + i, op_len, op, op_len)) continue;
        bool left_space = i == 0 || span.start[i - 1] == ' ' ||
            span.start[i - 1] == '\t' || span.start[i - 1] == '\n' || span.start[i - 1] == '\r';
        size_t end = i + op_len;
        bool right_space = end == span.length || span.start[end] == ' ' ||
            span.start[end] == '\t' || span.start[end] == '\n' || span.start[end] == '\r';
        if (left_space && right_space) {
            *out_pos = i;
            *out_len = op_len;
            return true;
        }
    }
    return false;
}

static bool css_evaluate_supports_span(CssEngine* engine, CssConditionSpan span,
                                       Pool* scratch) {
    if (!engine || !scratch) return false;
    span = css_condition_trim(span);
    if (span.length == 0) return false;

    if (str_istarts_with(span.start, span.length, "not ", 4)) {
        CssConditionSpan operand = {span.start + 4, span.length - 4};
        return !css_evaluate_supports_span(engine, operand, scratch);
    }

    size_t op_pos = 0;
    size_t op_len = 0;
    if (css_condition_find_operator(span, "or", &op_pos, &op_len)) {
        CssConditionSpan left = {span.start, op_pos};
        CssConditionSpan right = {span.start + op_pos + op_len,
                                  span.length - op_pos - op_len};
        return css_evaluate_supports_span(engine, left, scratch) ||
            css_evaluate_supports_span(engine, right, scratch);
    }
    if (css_condition_find_operator(span, "and", &op_pos, &op_len)) {
        CssConditionSpan left = {span.start, op_pos};
        CssConditionSpan right = {span.start + op_pos + op_len,
                                  span.length - op_pos - op_len};
        return css_evaluate_supports_span(engine, left, scratch) &&
            css_evaluate_supports_span(engine, right, scratch);
    }

    css_condition_outer_parens(&span);
    span = css_condition_trim(span);
    if (span.length == 0) return false;
    if (span.length >= 10 &&
        str_istarts_with(span.start, span.length, "selector(", 9)) {
        CssConditionSpan argument = {span.start + 8, span.length - 8};
        if (!css_condition_outer_parens(&argument) || argument.length == 0) {
            return false;
        }
        char* selector_text = pool_dup_n(scratch, argument.start,
                                         argument.length);
        return selector_text && css_parse_selector_group_text(
            selector_text, argument.length, scratch) != nullptr;
    }
    char* declaration = pool_dup_n(scratch, span.start, span.length);
    if (!declaration) return false;
    CssDeclaration* parsed = css_parse_declaration_text(
        declaration, span.length, scratch);
    return css_declaration_is_supported(parsed);
}

static bool css_evaluate_supports_condition_uncached(CssEngine* engine,
                                                      const char* condition) {
    if (!engine || !condition) return false;
    Pool* scratch = mem_pool_create(NULL, MEM_ROLE_TEMP,
                                    "css.supports_condition.scratch");
    if (!scratch) return false;
    CssConditionSpan span = {condition, strlen(condition)};
    bool result = css_evaluate_supports_span(engine, span, scratch);
    mem_pool_destroy(scratch);
    return result;
}

static char* css_media_trim_mutable(char* text) {
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;
    size_t length = strlen(text);
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
    return text;
}

static bool css_media_range_operand(CssEngine* engine, char* text,
                                    double* value, bool* feature,
                                    CssMediaNumericKind* kind) {
    text = css_media_trim_mutable(text);
    *feature = css_media_numeric_feature(engine, text, kind, value);
    return *feature || *text != '\0';
}

static bool css_media_compare(double left, double right, const char* op) {
    if (strcmp(op, "<") == 0) return left < right;
    if (strcmp(op, "<=") == 0) return left <= right;
    if (strcmp(op, ">") == 0) return left > right;
    if (strcmp(op, ">=") == 0) return left >= right;
    return strcmp(op, "=") == 0 && left == right;
}

static bool css_media_evaluate_range(CssEngine* engine, char* text) {
    char* operands[3] = {text, nullptr, nullptr};
    char operators[2][3] = {};
    int operator_count = 0;
    for (char* cursor = text; *cursor; cursor++) {
        if (*cursor != '<' && *cursor != '>' && *cursor != '=') continue;
        if (operator_count >= 2) return false;
        char op = *cursor;
        *cursor = '\0';
        operators[operator_count][0] = op;
        if (cursor[1] == '=') {
            if (op == '=') return false;
            operators[operator_count][1] = '=';
            cursor++;
        } else if (op == '=' && (cursor[1] == '<' || cursor[1] == '>')) {
            return false;
        }
        operands[++operator_count] = cursor + 1;
    }
    if (operator_count == 0) return false;
    double values[3] = {};
    bool features[3] = {};
    CssMediaNumericKind kind = CSS_MEDIA_NUMERIC_UNKNOWN;
    for (int i = 0; i <= operator_count; i++) {
        CssMediaNumericKind operand_kind = CSS_MEDIA_NUMERIC_UNKNOWN;
        if (!css_media_range_operand(engine, operands[i], &values[i],
                                     &features[i], &operand_kind))
            return false;
        if (features[i]) {
            if (kind != CSS_MEDIA_NUMERIC_UNKNOWN) return false;
            kind = operand_kind;
        }
    }
    if (kind == CSS_MEDIA_NUMERIC_UNKNOWN) return false;
    for (int i = 0; i <= operator_count; i++) {
        if (features[i]) continue;
        values[i] = css_media_numeric_value(
            kind, css_media_trim_mutable(operands[i]));
        if (values[i] < 0.0 || !isfinite(values[i])) return false;
    }
    if (operator_count == 1) {
        if (features[0] == features[1]) return false;
    } else if (features[0] || !features[1] || features[2]) {
        return false;
    }
    for (int i = 0; i < operator_count; i++) {
        if (!css_media_compare(values[i], values[i + 1], operators[i]))
            return false;
    }
    return true;
}

static bool css_media_evaluate_atom(CssEngine* engine, CssConditionSpan span,
                                    Pool* scratch) {
    char* content = pool_dup_n(scratch, span.start, span.length);
    if (!content) return false;
    // Media feature names and their keywords are ASCII case-insensitive.
    for (char* cursor = content; *cursor; cursor++) {
        if (*cursor >= 'A' && *cursor <= 'Z') *cursor += 'a' - 'A';
    }
    char* colon = strchr(content, ':');
    if (colon) {
        *colon = '\0';
        char* name = css_media_trim_mutable(content);
        char* value = css_media_trim_mutable(colon + 1);
        return *name && *value && evaluate_media_feature(engine, name, value);
    }
    if (strpbrk(content, "<=>")) return css_media_evaluate_range(engine, content);
    char* name = css_media_trim_mutable(content);
    if (strcmp(name, "width") == 0) return engine->context.viewport_width > 0.0;
    if (strcmp(name, "height") == 0) return engine->context.viewport_height > 0.0;
    if (strcmp(name, "aspect-ratio") == 0)
        return engine->context.viewport_width > 0.0 &&
            engine->context.viewport_height > 0.0;
    if (strcmp(name, "resolution") == 0)
        return engine->context.device_pixel_ratio > 0.0;
    if (strcmp(name, "color") == 0) return true;
    if (strcmp(name, "monochrome") == 0) return false;
    if (strcmp(name, "orientation") == 0) return true;
    return false;
}

static bool css_evaluate_media_span(CssEngine* engine, CssConditionSpan span,
                                    Pool* scratch, int depth) {
    if (!engine || !scratch || depth > 32) return false;
    span = css_condition_trim(span);
    if (span.length == 0) return false;
    if (str_istarts_with(span.start, span.length, "not ", 4)) {
        CssConditionSpan operand = {span.start + 4, span.length - 4};
        return !css_evaluate_media_span(engine, operand, scratch, depth + 1);
    }
    if (str_istarts_with(span.start, span.length, "only ", 5)) {
        CssConditionSpan operand = {span.start + 5, span.length - 5};
        return css_evaluate_media_span(engine, operand, scratch, depth + 1);
    }
    size_t op_pos = 0, op_len = 0;
    if (css_condition_find_operator(span, "or", &op_pos, &op_len)) {
        CssConditionSpan left = {span.start, op_pos};
        CssConditionSpan right = {span.start + op_pos + op_len,
                                  span.length - op_pos - op_len};
        return css_evaluate_media_span(engine, left, scratch, depth + 1) ||
            css_evaluate_media_span(engine, right, scratch, depth + 1);
    }
    if (css_condition_find_operator(span, "and", &op_pos, &op_len)) {
        CssConditionSpan left = {span.start, op_pos};
        CssConditionSpan right = {span.start + op_pos + op_len,
                                  span.length - op_pos - op_len};
        return css_evaluate_media_span(engine, left, scratch, depth + 1) &&
            css_evaluate_media_span(engine, right, scratch, depth + 1);
    }
    if (css_condition_outer_parens(&span)) {
        if (span.length == 0) return false;
        if (span.start[0] == '(' ||
            str_istarts_with(span.start, span.length, "not ", 4) ||
            css_condition_find_operator(span, "or", &op_pos, &op_len) ||
            css_condition_find_operator(span, "and", &op_pos, &op_len)) {
            return css_evaluate_media_span(engine, span, scratch, depth + 1);
        }
        return css_media_evaluate_atom(engine, span, scratch);
    }
    if (span.start[0] == '(' || span.start[0] == ')') return false;
    char* type = pool_dup_n(scratch, span.start, span.length);
    return type && evaluate_media_type(engine, css_media_trim_mutable(type));
}

static bool css_evaluate_media_query_uncached(CssEngine* engine,
                                              const char* media_query) {
    if (!engine || !media_query || !*media_query) return true;
    Pool* scratch = mem_pool_create(NULL, MEM_ROLE_TEMP,
                                    "css.media_query.scratch");
    if (!scratch) return false;
    CssConditionSpan whole = {media_query, strlen(media_query)};
    size_t start = 0;
    int depth = 0;
    bool result = false;
    for (size_t i = 0; i <= whole.length; i++) {
        char c = i < whole.length ? whole.start[i] : ',';
        if (c == '(') depth++;
        else if (c == ')') {
            if (--depth < 0) break;
        }
        if (c != ',' || depth != 0) continue;
        CssConditionSpan part = {whole.start + start, i - start};
        if (css_evaluate_media_span(engine, part, scratch, 0)) {
            result = true;
            break;
        }
        start = i + 1;
    }
    mem_pool_destroy(scratch);
    return result;
}

bool css_evaluate_supports_condition(CssEngine* engine, const char* condition) {
    if (!engine || !condition) return false;
    bool result = false;
    if (css_condition_cache_lookup(engine, CSS_CONDITION_SUPPORTS,
                                   condition, &result)) return result;
    engine->condition_evaluations++;
    result = css_evaluate_supports_condition_uncached(engine, condition);
    css_condition_cache_store(engine, CSS_CONDITION_SUPPORTS, condition, result);
    return result;
}

bool css_evaluate_media_query(CssEngine* engine, const char* media_query) {
    if (!engine || !media_query) return true;
    bool result = false;
    if (css_condition_cache_lookup(engine, CSS_CONDITION_MEDIA,
                                   media_query, &result)) return result;
    engine->condition_evaluations++;
    result = css_evaluate_media_query_uncached(engine, media_query);
    css_condition_cache_store(engine, CSS_CONDITION_MEDIA, media_query, result);
    return result;
}

CssEngineStats css_engine_get_stats(const CssEngine* engine) {
    CssEngineStats stats = {0};
    if (!engine) return stats;

    stats.rules_processed = engine->stats.rules_processed;
    stats.selectors_processed = engine->selectors_processed; // Top-level field
    stats.properties_processed = engine->stats.properties_computed;
    stats.parse_errors = engine->parse_errors; // Top-level field
    stats.validation_errors = engine->validation_errors; // Top-level field
    stats.parse_time = engine->stats.parse_time;
    stats.cascade_time = engine->stats.cascade_time;
    stats.memory_usage = engine->stats.memory_usage;
    return stats;
}

CssStylesheet* css_parse_stylesheet(CssEngine* engine, const char* css_text, const char* source_url) {
    return css_enhanced_parse_stylesheet(engine, css_text, source_url);
}
