#include "view.hpp"

#include "../lib/mem_factory.h"
#include "../lib/hash.h"
#include "../lib/hashmap_typed.hpp"

#include <math.h>
#include <string.h>

struct CanonicalInlineEntry {
    uint64_t hash;
    InlineProp* value;
    lam::Own<CanonicalInlineEntry> next;
};

struct CanonicalFontFamily {
    const char* chars;
    size_t length;
};

struct CanonicalFontFamilies {
    TypedHashMap<CanonicalFontFamily, HashMapLenStrMemberKeyOps<CanonicalFontFamily,
        &CanonicalFontFamily::chars, &CanonicalFontFamily::length>> index;
};

static uint32_t inline_float_hash_bits(float value) {
    if (value == 0.0f) return 0; // CSS treats positive and negative zero as the same value.
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    if (isnan(value)) return 0x7fc00000u;
    return bits;
}

uint64_t inline_prop_hash(const InlineProp* value) {
    if (!value) return 0;
    uint64_t hash = HASH_FNV1A_64_OFFSET_BASIS;
#define INLINE_HASH_FIELD(field) hash = hash_fnv1a_64_extend_u64le(hash, (uint64_t)value->field)
    INLINE_HASH_FIELD(cursor);
    INLINE_HASH_FIELD(caret_shape);
    INLINE_HASH_FIELD(color.c);
    INLINE_HASH_FIELD(accent_color.c);
    INLINE_HASH_FIELD(caret_color.c);
    INLINE_HASH_FIELD(selection_color.c);
    INLINE_HASH_FIELD(selection_background_color.c);
    INLINE_HASH_FIELD(has_color);
    INLINE_HASH_FIELD(has_accent_color);
    INLINE_HASH_FIELD(caret_color_mode);
    INLINE_HASH_FIELD(has_selection_color);
    INLINE_HASH_FIELD(has_selection_background_color);
    INLINE_HASH_FIELD(svg_fill_color.c);
    INLINE_HASH_FIELD(svg_stroke_color.c);
    INLINE_HASH_FIELD(vertical_align);
    hash = hash_fnv1a_64_extend_u64le(hash,
        inline_float_hash_bits(value->vertical_align_offset));
    hash = hash_fnv1a_64_extend_u64le(hash, inline_float_hash_bits(value->opacity));
    INLINE_HASH_FIELD(visibility);
    INLINE_HASH_FIELD(mix_blend_mode);
    INLINE_HASH_FIELD(has_svg_fill);
    INLINE_HASH_FIELD(svg_fill_none);
    INLINE_HASH_FIELD(has_svg_stroke);
    INLINE_HASH_FIELD(svg_stroke_none);
    INLINE_HASH_FIELD(has_svg_stroke_width);
    hash = hash_fnv1a_64_extend_u64le(hash,
        inline_float_hash_bits(value->svg_stroke_width));
#undef INLINE_HASH_FIELD
    return hash;
}

bool inline_prop_equal(const InlineProp* left, const InlineProp* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    // Explicit semantic comparison keeps padding and future compiler layout out
    // of the canonicalization contract.
    return left->cursor == right->cursor &&
           left->image_rendering == right->image_rendering &&
           left->caret_shape == right->caret_shape &&
           left->color.c == right->color.c &&
           left->accent_color.c == right->accent_color.c &&
           left->caret_color.c == right->caret_color.c &&
           left->selection_color.c == right->selection_color.c &&
           left->selection_background_color.c == right->selection_background_color.c &&
           left->has_color == right->has_color &&
           left->has_accent_color == right->has_accent_color &&
           left->caret_color_mode == right->caret_color_mode &&
           left->has_selection_color == right->has_selection_color &&
           left->has_selection_background_color == right->has_selection_background_color &&
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
    lam::Own<CanonicalInlineEntry>* buckets = (lam::Own<CanonicalInlineEntry>*)pool_calloc(
        tree->prop_pool, bucket_count * sizeof(lam::Own<CanonicalInlineEntry>));
    if (!buckets) return false;
    for (size_t i = 0; i < tree->inline_canonical_bucket_count; i++) {
        lam::Own<CanonicalInlineEntry> entry = tree->inline_canonical_buckets[i];
        while (entry) {
            lam::Own<CanonicalInlineEntry> next = entry->next;
            size_t bucket = (size_t)(entry->hash & (bucket_count - 1u));
            entry->next = buckets[bucket];
            buckets[bucket] = entry;
            entry = next;
        }
    }
    if (tree->inline_canonical_buckets) {
        tree->canonical_stats.index_bytes -= pool_allocation_size(
            tree->prop_pool, tree->inline_canonical_buckets);
        lam::free_owned(tree->prop_pool, tree->inline_canonical_buckets);
    }
    tree->inline_canonical_buckets = lam::own_arr(buckets);
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

    lam::Own<CanonicalInlineEntry> entry = lam::own((CanonicalInlineEntry*)pool_calloc(
        tree->prop_pool, sizeof(CanonicalInlineEntry)));
    if (!entry) return nullptr;
    InlineProp* canonical = (InlineProp*)arena_calloc(tree->canonical_prop_arena,
                                                      sizeof(InlineProp));
    if (!canonical) {
        lam::free_owned(tree->prop_pool, entry);
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
    tree->canonical_prop_arena = lam::own(mem_arena_create(
        tree->mem_ctx, MEM_ROLE_VIEW, "view_tree.canonical_prop_arena"));
    tree->inline_canonical_buckets = nullptr;
    tree->inline_canonical_bucket_count = 0;
    tree->inline_canonical_count = 0;
    tree->canonical_prop_cap_bytes = 4u * 1024u * 1024u;
    memset(&tree->canonical_stats, 0, sizeof(tree->canonical_stats));
}

const char* view_tree_canonical_font_family(ViewTree* tree, const char* chars,
                                            size_t length) {
    if (!tree || !tree->prop_pool || !chars) return nullptr;
    tree->canonical_stats.font_family_lookups++;
    if (!tree->canonical_font_families) {
        auto* families = (CanonicalFontFamilies*)pool_calloc(
            tree->prop_pool, sizeof(CanonicalFontFamilies));
        if (!families) return nullptr;
        if (!families->index.init(0)) {
            pool_free(tree->prop_pool, families);
            return nullptr;
        }
        tree->canonical_font_families = lam::own(families);
    }
    CanonicalFontFamilies* families = tree->canonical_font_families;
    if (const CanonicalFontFamily* found = families->index.get({chars, length})) {
        return found->chars;
    }
    char* canonical = (char*)pool_alloc(tree->prop_pool, length + 1);
    if (!canonical) return nullptr;
    memcpy(canonical, chars, length);
    canonical[length] = '\0';
    families->index.set({canonical, length});
    if (families->index.oom()) {
        pool_free(tree->prop_pool, canonical);
        return nullptr;
    }
    tree->canonical_stats.font_family_misses++;
    return canonical;
}

static void canonical_font_families_destroy(ViewTree* tree) {
    CanonicalFontFamilies* families = tree->canonical_font_families;
    if (!families) return;
    size_t cursor = 0;
    CanonicalFontFamily* entry = nullptr;
    while (families->index.next(&cursor, &entry)) {
        pool_free(tree->prop_pool, (void*)entry->chars);
    }
    families->index.destroy();
    lam::free_owned(tree->prop_pool, tree->canonical_font_families);
}

void view_tree_canonical_destroy(ViewTree* tree) {
    if (!tree) return;
    canonical_font_families_destroy(tree);
    for (size_t i = 0; i < tree->inline_canonical_bucket_count; i++) {
        lam::free_owned_list(tree->prop_pool, tree->inline_canonical_buckets[i]);
    }
    lam::free_owned(tree->prop_pool, tree->inline_canonical_buckets);
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
        parent->in_line = lam::view_ref(canonical);
        parent->mark_inline_prop_shared();
        pool_free(tree->prop_pool, parent_owned);
        tree->canonical_stats.inline_promotions++;
    }

    if (element->in_line != canonical) {
        if (!element->inline_prop_shared()) {
            pool_free(tree->prop_pool, element->in_line);
        }
        element->in_line = lam::view_ref(canonical);
        element->mark_inline_prop_shared();
        tree->canonical_stats.inline_promotions++;
    }
}
