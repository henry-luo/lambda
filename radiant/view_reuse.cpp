#include "view.hpp"

#include "../lib/mem_factory.h"

#include <math.h>
#include <string.h>

struct CanonicalInlineEntry {
    uint64_t hash;
    InlineProp* value;
    CanonicalInlineEntry* next;
};

static uint64_t inline_hash_word(uint64_t hash, uint64_t word) {
    for (size_t i = 0; i < sizeof(word); i++) {
        hash ^= (word >> (i * 8u)) & 0xffu;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint32_t inline_float_hash_bits(float value) {
    if (value == 0.0f) return 0; // CSS treats positive and negative zero as the same value.
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    if (isnan(value)) return 0x7fc00000u;
    return bits;
}

uint64_t inline_prop_hash(const InlineProp* value) {
    if (!value) return 0;
    uint64_t hash = 1469598103934665603ULL;
#define INLINE_HASH_FIELD(field) hash = inline_hash_word(hash, (uint64_t)value->field)
    INLINE_HASH_FIELD(cursor);
    INLINE_HASH_FIELD(caret_shape);
    INLINE_HASH_FIELD(color.c);
    INLINE_HASH_FIELD(accent_color.c);
    INLINE_HASH_FIELD(has_color);
    INLINE_HASH_FIELD(has_accent_color);
    INLINE_HASH_FIELD(svg_fill_color.c);
    INLINE_HASH_FIELD(svg_stroke_color.c);
    INLINE_HASH_FIELD(vertical_align);
    hash = inline_hash_word(hash, inline_float_hash_bits(value->vertical_align_offset));
    hash = inline_hash_word(hash, inline_float_hash_bits(value->opacity));
    INLINE_HASH_FIELD(visibility);
    INLINE_HASH_FIELD(mix_blend_mode);
    INLINE_HASH_FIELD(has_svg_fill);
    INLINE_HASH_FIELD(svg_fill_none);
    INLINE_HASH_FIELD(has_svg_stroke);
    INLINE_HASH_FIELD(svg_stroke_none);
    INLINE_HASH_FIELD(has_svg_stroke_width);
    hash = inline_hash_word(hash, inline_float_hash_bits(value->svg_stroke_width));
#undef INLINE_HASH_FIELD
    return hash;
}

bool inline_prop_equal(const InlineProp* left, const InlineProp* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    // Explicit semantic comparison keeps padding and future compiler layout out
    // of the canonicalization contract.
    return left->cursor == right->cursor &&
           left->caret_shape == right->caret_shape &&
           left->color.c == right->color.c &&
           left->accent_color.c == right->accent_color.c &&
           left->has_color == right->has_color &&
           left->has_accent_color == right->has_accent_color &&
           left->svg_fill_color.c == right->svg_fill_color.c &&
           left->svg_stroke_color.c == right->svg_stroke_color.c &&
           left->vertical_align == right->vertical_align &&
           left->vertical_align_offset == right->vertical_align_offset &&
           left->opacity == right->opacity &&
           left->visibility == right->visibility &&
           left->mix_blend_mode == right->mix_blend_mode &&
           left->has_svg_fill == right->has_svg_fill &&
           left->svg_fill_none == right->svg_fill_none &&
           left->has_svg_stroke == right->has_svg_stroke &&
           left->svg_stroke_none == right->svg_stroke_none &&
           left->has_svg_stroke_width == right->has_svg_stroke_width &&
           left->svg_stroke_width == right->svg_stroke_width;
}

static bool canonical_inline_resize(ViewTree* tree, size_t bucket_count) {
    if (!tree || !tree->prop_pool || bucket_count < 16) return false;
    CanonicalInlineEntry** buckets = (CanonicalInlineEntry**)pool_calloc(
        tree->prop_pool, bucket_count * sizeof(CanonicalInlineEntry*));
    if (!buckets) return false;
    for (size_t i = 0; i < tree->inline_canonical_bucket_count; i++) {
        CanonicalInlineEntry* entry = tree->inline_canonical_buckets[i];
        while (entry) {
            CanonicalInlineEntry* next = entry->next;
            size_t bucket = (size_t)(entry->hash & (bucket_count - 1u));
            entry->next = buckets[bucket];
            buckets[bucket] = entry;
            entry = next;
        }
    }
    if (tree->inline_canonical_buckets) {
        tree->canonical_stats.index_bytes -= pool_allocation_size(
            tree->prop_pool, tree->inline_canonical_buckets);
        pool_free(tree->prop_pool, tree->inline_canonical_buckets);
    }
    tree->inline_canonical_buckets = buckets;
    tree->inline_canonical_bucket_count = bucket_count;
    tree->canonical_stats.index_bytes += pool_allocation_size(tree->prop_pool, buckets);
    return true;
}

static InlineProp* canonical_inline_find_or_create(ViewTree* tree,
                                                   const InlineProp* value) {
    if (!tree || !value || !tree->canonical_prop_arena || !tree->prop_pool) return nullptr;
    tree->canonical_stats.inline_lookups++;
    uint64_t hash = inline_prop_hash(value);
    if (tree->inline_canonical_bucket_count) {
        size_t bucket = (size_t)(hash & (tree->inline_canonical_bucket_count - 1u));
        for (CanonicalInlineEntry* entry = tree->inline_canonical_buckets[bucket];
             entry; entry = entry->next) {
            if (entry->hash != hash) continue;
            tree->canonical_stats.inline_exact_compares++;
            if (inline_prop_equal(entry->value, value)) {
                tree->canonical_stats.inline_hits++;
                return entry->value;
            }
            tree->canonical_stats.inline_collisions++;
        }
    }

    ArenaStats stats = {};
    arena_get_stats(tree->canonical_prop_arena, &stats);
    if (stats.active_bytes + sizeof(InlineProp) > tree->canonical_prop_cap_bytes) {
        tree->canonical_stats.cap_fallbacks++;
        return nullptr;
    }
    if (!tree->inline_canonical_bucket_count) {
        if (!canonical_inline_resize(tree, 64)) return nullptr;
    } else if ((tree->inline_canonical_count + 1u) * 4u >
               tree->inline_canonical_bucket_count * 3u) {
        if (!canonical_inline_resize(tree, tree->inline_canonical_bucket_count * 2u)) {
            return nullptr;
        }
    }

    CanonicalInlineEntry* entry = (CanonicalInlineEntry*)pool_calloc(
        tree->prop_pool, sizeof(CanonicalInlineEntry));
    if (!entry) return nullptr;
    InlineProp* canonical = (InlineProp*)arena_calloc(tree->canonical_prop_arena,
                                                      sizeof(InlineProp));
    if (!canonical) {
        pool_free(tree->prop_pool, entry);
        return nullptr;
    }
    memcpy(canonical, value, sizeof(InlineProp));
    entry->hash = hash;
    entry->value = canonical;
    size_t bucket = (size_t)(hash & (tree->inline_canonical_bucket_count - 1u));
    entry->next = tree->inline_canonical_buckets[bucket];
    tree->inline_canonical_buckets[bucket] = entry;
    tree->inline_canonical_count++;
    tree->canonical_stats.index_bytes += pool_allocation_size(tree->prop_pool, entry);
    tree->canonical_stats.inline_misses++;
    return canonical;
}

void view_tree_canonical_init(ViewTree* tree) {
    if (!tree || !tree->prop_pool) return;
    tree->canonical_prop_arena = mem_arena_create(
        NULL, tree->prop_pool, MEM_ROLE_VIEW, "view_tree.canonical_prop_arena");
    tree->inline_canonical_buckets = nullptr;
    tree->inline_canonical_bucket_count = 0;
    tree->inline_canonical_count = 0;
    tree->canonical_prop_cap_bytes = 4u * 1024u * 1024u;
    memset(&tree->canonical_stats, 0, sizeof(tree->canonical_stats));
}

void view_tree_canonical_destroy(ViewTree* tree) {
    if (!tree) return;
    for (size_t i = 0; i < tree->inline_canonical_bucket_count; i++) {
        CanonicalInlineEntry* entry = tree->inline_canonical_buckets[i];
        while (entry) {
            CanonicalInlineEntry* next = entry->next;
            pool_free(tree->prop_pool, entry);
            entry = next;
        }
    }
    pool_free(tree->prop_pool, tree->inline_canonical_buckets);
    tree->inline_canonical_buckets = nullptr;
    tree->inline_canonical_bucket_count = 0;
    tree->inline_canonical_count = 0;
    Arena* arena = tree->canonical_prop_arena;
    tree->canonical_prop_arena = nullptr;
    if (arena) mem_arena_destroy(arena);
}

void view_tree_commit_inline_prop(ViewTree* tree, DomElement* element,
                                  DomElement* parent) {
    if (!tree || !element || !parent || !element->in_line || !parent->in_line) return;
    tree->canonical_stats.inline_exact_compares++;
    if (!inline_prop_equal(element->in_line, parent->in_line)) return;

    InlineProp* canonical = nullptr;
    if (parent->inline_prop_shared()) {
        canonical = parent->in_line;
    } else {
        canonical = canonical_inline_find_or_create(tree, parent->in_line);
        if (!canonical) return; // Capacity/index pressure deliberately falls back to owned storage.
        InlineProp* parent_owned = parent->in_line;
        parent->in_line = canonical;
        parent->mark_inline_prop_shared();
        pool_free(tree->prop_pool, parent_owned);
        tree->canonical_stats.inline_promotions++;
    }

    if (element->in_line != canonical) {
        if (!element->inline_prop_shared()) {
            pool_free(tree->prop_pool, element->in_line);
        }
        element->in_line = canonical;
        element->mark_inline_prop_shared();
        tree->canonical_stats.inline_promotions++;
    }
}
