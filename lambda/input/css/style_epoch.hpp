#ifndef STYLE_EPOCH_HPP
#define STYLE_EPOCH_HPP

#include <stddef.h>
#include <stdint.h>

#include "css_style.hpp"

struct CssEngine;
struct CssRule;
struct DomDocument;
struct DomElement;

typedef struct StyleEpochStats {
    uint64_t current_epoch_id;
    uint64_t epoch_count;
    uint64_t retired_epoch_count;
    uint64_t canonical_entry_count;
    uint64_t canonical_tree_count;
    uint64_t recipe_entry_count;
    uint64_t bound_element_refs;
    uint64_t lookup_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t exact_compare_count;
    uint64_t collision_count;
    uint64_t cow_count;
    uint64_t released_epoch_count;
    size_t current_reserved_bytes;
    size_t current_live_bytes;
    size_t retired_referenced_reserved_bytes;
} StyleEpochStats;

bool style_epoch_manager_init(DomDocument* doc);
void style_epoch_manager_destroy(DomDocument* doc);

// Returns true only for the caller that opened the outer cascade batch.
bool style_epoch_cascade_begin(DomDocument* doc, DomElement* root,
                               CssEngine* engine, bool global_change);
void style_epoch_cascade_end(DomDocument* doc);

// Returns true when the rule was recorded and must not be cloned per element.
bool style_epoch_record_rule(DomElement* element, CssRule* rule,
                             CssSpecificity specificity);

bool style_epoch_ensure_owned(DomElement* element);
void style_epoch_unbind_element(DomElement* element);
void style_epoch_mark_global_change(DomDocument* doc);
uint64_t style_epoch_current_id(DomDocument* doc);
void style_epoch_get_stats(DomDocument* doc, StyleEpochStats* out);

// Deterministic collision coverage; never enabled by production paths.
void style_epoch_debug_force_hash_collision(DomDocument* doc, bool enabled);

#endif
