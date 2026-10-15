#include "typeset.hpp"
#include "../lib/mem_grow.hpp"
#include "../lib/hashmap_helpers.h"
#include <math.h>
#include <float.h>

struct TypesetTargetIndex { const char* name; size_t offset; };
HASHMAP_DEFINE_STRKEY(typeset_target_index, TypesetTargetIndex, name)

bool typeset_source_in_scope(const TypesetSource& source, uint64_t provider, uint64_t generation,
        const TypesetSourceScope& scope) {
    if (source.provider == provider && source.generation == generation) return true;
    for (size_t i = 0; scope.namespaces && i < scope.count; i++)
        if (source.provider == scope.namespaces[i].provider && source.generation == scope.namespaces[i].generation)
            return true;
    return false;
}

bool typeset_source_same_identity(const TypesetSource& left, const TypesetSource& right) {
    return left.provider == right.provider && left.generation == right.generation && left.node == right.node &&
        left.offset_unit == right.offset_unit;
}

TypesetStatus typeset_target_append(TypesetTargetStore* store, const TypesetTarget* target) {
    if (!store || !store->pool || !store->provider || !store->generation || !target ||
        !target->name || !*target->name || !store->limit) return TYPESET_INVALID;
    if (!typeset_source_in_scope(target->source, store->provider, store->generation, store->sources))
        return TYPESET_STALE;
    if (store->count >= store->limit) return TYPESET_BUDGET_EXHAUSTED;
    if (!store->index) store->index = typeset_target_index_new(16);
    if (!store->index) return TYPESET_OUT_OF_MEMORY;
    TypesetTargetIndex key = {target->name, store->count};
    if (hashmap_get(store->index, &key)) return TYPESET_INVALID;
    if (!lam::pool_grow_array(store->pool, &store->entries, &store->capacity, store->count + 1, 16))
        return TYPESET_OUT_OF_MEMORY;
    key.name = pool_strdup(store->pool, target->name);
    if (!key.name) return TYPESET_OUT_OF_MEMORY;
    hashmap_set(store->index, &key);
    if (hashmap_oom(store->index)) return TYPESET_OUT_OF_MEMORY;
    store->entries[store->count] = *target;
    store->entries[store->count++].name = key.name;
    return TYPESET_OK;
}

const TypesetTarget* typeset_target_find(const TypesetTargetStore* store, const char* name) {
    if (!store || !store->index || !name) return nullptr;
    TypesetTargetIndex key = {name, 0};
    const TypesetTargetIndex* found = (const TypesetTargetIndex*)hashmap_get(store->index, &key);
    return found ? &store->entries[found->offset] : nullptr;
}

void typeset_targets_dispose(TypesetTargetStore* store) {
    if (!store) return;
    if (store->index) hashmap_free(store->index);
    *store = {};
}

TypesetStatus typeset_convergence_observe(TypesetConvergence* state,
        const char* bytes, size_t length, bool* settled) {
    if (!state || !state->pool || !state->limit || !settled || (!bytes && length)) return TYPESET_INVALID;
    *settled = false;
    for (size_t i = state->count; i > 0; i--) {
        const TypesetPassSignature& previous = state->history[i - 1];
        if (previous.length != length || (length && memcmp(previous.bytes, bytes, length) != 0)) continue;
        // Exact signatures distinguish a fixed point from a repeated nonconverged cycle.
        *settled = i == state->count;
        return *settled ? TYPESET_OK : TYPESET_NO_PROGRESS;
    }
    if (state->count >= state->limit) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(state->pool, &state->history, &state->capacity, state->count + 1, 8))
        return TYPESET_OUT_OF_MEMORY;
    char* copy = pool_dup_n(state->pool, bytes ? bytes : "", length);
    if (!copy) return TYPESET_OUT_OF_MEMORY;
    state->history[state->count++] = {copy, length};
    return state->count >= state->limit ? TYPESET_BUDGET_EXHAUSTED : TYPESET_OK;
}

TypesetPacking typeset_pack_glue(const TypesetItem* items, size_t first, size_t end,
                                 float natural, float target) {
    TypesetPacking packing = {};
    packing.shrinking = target < natural;
    float capacity = 0.0f;
    for (size_t i = first; items && i < end; i++) {
        if (items[i].kind != TYPESET_GLUE) continue;
        const TypesetGlue& glue = items[i].glue;
        float amount = packing.shrinking ? glue.shrink : glue.stretch;
        uint8_t order = packing.shrinking ? glue.shrink_order : glue.stretch_order;
        if (amount <= 0.0f) continue;
        if (order > packing.order) { packing.order = order; capacity = 0.0f; }
        if (order == packing.order) capacity += amount;
    }
    float difference = target - natural;
    if (capacity > 0.0f) {
        packing.ratio = difference / capacity;
        if (packing.shrinking && !packing.order && packing.ratio < -1.0f) packing.ratio = -1.0f;
        packing.residual = difference - packing.ratio * capacity;
    } else packing.residual = difference;
    return packing;
}

float typeset_glue_advance(const TypesetGlue* glue, const TypesetPacking* packing) {
    if (!glue || !packing) return 0.0f;
    float amount = packing->shrinking ? glue->shrink : glue->stretch;
    uint8_t order = packing->shrinking ? glue->shrink_order : glue->stretch_order;
    return glue->natural + (order == packing->order ? amount * packing->ratio : 0.0f);
}

size_t typeset_line_alternatives(const TypesetParagraph* paragraph, size_t first,
        float width, TypesetLineCandidate* candidates, size_t capacity, void*) {
    if (!paragraph || !paragraph->items || !candidates || !capacity || first >= paragraph->count) return 0;
    size_t count = 0, paint_first = first;
    while (paint_first < paragraph->count && paragraph->items[paint_first].kind == TYPESET_GLUE &&
           paragraph->items[paint_first].glue.discard_start) paint_first++;
    if (paint_first == paragraph->count) {
        candidates[0] = {};
        candidates[0].first = first; candidates[0].next = paint_first;
        candidates[0].paint_first = candidates[0].paint_end = paint_first;
        return 1;
    }
    float advance = 0.0f, height = paragraph->minimum_line_height, depth = 0.0f;
    float ascent = paragraph->minimum_baseline;
    float descent = fmaxf(0.0f, paragraph->minimum_line_height - ascent);
    bool baseline_aware = paragraph->baseline_aware || paragraph->minimum_baseline > 0.0f;
    size_t last_next = SIZE_MAX;
    auto append_candidate = [&](size_t next, size_t paint_end, float natural, const TypesetBreak& boundary) {
        // trim the complete conditional run, including whitespace before an unpainted forced break.
        while (paint_end > paint_first && paragraph->items[paint_end - 1].kind == TYPESET_GLUE &&
               paragraph->items[paint_end - 1].glue.discard_end)
            natural -= paragraph->items[--paint_end].glue.natural;
        TypesetLineCandidate candidate = {};
        candidate.first = first; candidate.next = next;
        candidate.paint_first = paint_first; candidate.paint_end = paint_end;
        candidate.width = natural; candidate.height = height;
        candidate.depth = baseline_aware ? descent : depth;
        candidate.baseline = baseline_aware ? ascent : 0.0f;
        candidate.penalty = boundary.penalty;
        candidate.forced = boundary.legality == TYPESET_BREAK_FORCED && boundary.scope == TYPESET_BREAK_LINE;
        candidate.overflow = natural > width;
        candidate.packing = typeset_pack_glue(paragraph->items, paint_first, paint_end, natural, width);
        double ratio = candidate.packing.ratio;
        candidate.cost = fmin(10000.0, 100.0 * fabs(ratio * ratio * ratio)) + candidate.penalty;
        if (!candidate.packing.order && fabsf(candidate.packing.residual) > 0.01f) candidate.cost += 10000.0;
        // one continuation has one candidate; a forced before-boundary overrides its soft predecessor.
        if (next == last_next) {
            if (candidate.forced && count <= capacity) candidates[count - 1] = candidate;
        } else {
            if (count < capacity) candidates[count] = candidate;
            count++; last_next = next;
        }
        return candidate.forced || candidate.overflow;
    };
    for (size_t i = paint_first; i < paragraph->count; i++) {
        const TypesetItem& item = paragraph->items[i];
        if (i > paint_first && item.has_before && item.before.legality != TYPESET_BREAK_FORBIDDEN && item.before.scope == TYPESET_BREAK_LINE) {
            // an unconsumed atomic item must not contribute height to the preceding line.
            size_t paint_end = i;
            float natural = advance;
            if (append_candidate(i, paint_end, natural, item.before)) break;
        }
        TypesetMetrics metrics = item.metrics;
        if (paragraph->measure && !paragraph->measure(paragraph, i, width, &metrics, paragraph->context)) return 0;
        if (!isfinite(metrics.advance) || !isfinite(metrics.height) || !isfinite(metrics.depth)) return 0;
        // baseline-aware producers retain the tallest ascent and descent independently.
        ascent = fmaxf(ascent, metrics.height); descent = fmaxf(descent, metrics.depth);
        height = baseline_aware ? ascent + descent
            : fmaxf(height, metrics.height + metrics.depth);
        depth = fmaxf(depth, metrics.depth);
        if (item.kind == TYPESET_BOX) {
            advance += metrics.advance;
        } else if (item.kind == TYPESET_GLUE) advance += item.glue.natural;
        bool ending = i + 1 == paragraph->count;
        bool forced = item.boundary.legality == TYPESET_BREAK_FORCED && item.boundary.scope == TYPESET_BREAK_LINE;
        bool allowed = (item.kind == TYPESET_GLUE || item.kind == TYPESET_PENALTY) &&
            item.boundary.legality != TYPESET_BREAK_FORBIDDEN && item.boundary.scope == TYPESET_BREAK_LINE;
        if (!ending && !allowed && !forced) continue;
        size_t paint_end = item.kind == TYPESET_PENALTY && !item.paint ? i : i + 1;
        if (append_candidate(i + 1, paint_end, advance, item.boundary)) break;
    }
    return count;
}

size_t typeset_choose_furthest_line(const TypesetLineCandidate* candidates, size_t count, void*) {
    if (!count) return SIZE_MAX;
    size_t selected = 0;
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].forced && !candidates[i].overflow) return i;
        if (!candidates[i].overflow && candidates[i].next >= candidates[selected].next) selected = i;
    }
    return selected;
}

size_t typeset_choose_lowest_cost(const TypesetLineCandidate* candidates, size_t count, void*) {
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < count; i++) {
        if (candidates[i].overflow) continue;
        if (selected == SIZE_MAX || candidates[i].cost < candidates[selected].cost) selected = i;
    }
    return selected;
}

TypesetStatus typeset_next_line(const TypesetParagraph* paragraph, size_t first,
        float width, TypesetLineCandidate* scratch, size_t capacity, TypesetLineCandidate* result) {
    if (!paragraph || !scratch || !capacity || !result || first >= paragraph->count ||
        !isfinite(width) || width <= 0.0f) return TYPESET_INVALID;
    TypesetLineAlternativesFn alternatives = paragraph->alternatives ? paragraph->alternatives : typeset_line_alternatives;
    size_t count = alternatives(paragraph, first, width, scratch, capacity, paragraph->context);
    if (!count) return TYPESET_UNPLACEABLE;
    if (count > capacity) return TYPESET_BUDGET_EXHAUSTED;
    TypesetLineChooseFn choose = paragraph->choose ? paragraph->choose : typeset_choose_furthest_line;
    size_t selected = choose(scratch, count, paragraph->context);
    if (selected >= count) return TYPESET_UNPLACEABLE;
    const TypesetLineCandidate& line = scratch[selected];
    if (line.first != first || line.next <= first || line.next > paragraph->count ||
        line.paint_first < first || line.paint_first > line.paint_end || line.paint_end > line.next ||
        !isfinite(line.width) || line.width < 0.0f || !isfinite(line.height) || line.height < 0.0f ||
        !isfinite(line.depth) || line.depth < 0.0f) return TYPESET_NO_PROGRESS;
    *result = line;
    return TYPESET_OK;
}

static bool typeset_resume_valid(const TypesetFlowProvider* provider, const TypesetResume* cursor) {
    return provider && cursor && provider->identity == cursor->provider && provider->generation == cursor->generation;
}

TypesetStatus typeset_flow_checkpoint(const TypesetFlowProvider* provider,
                                      const TypesetResume* cursor, TypesetResume* saved) {
    if (!provider || !cursor || !saved || !provider->checkpoint) return TYPESET_INVALID;
    if (!typeset_resume_valid(provider, cursor)) return TYPESET_STALE;
    TypesetStatus status = provider->checkpoint(provider->context, cursor, saved);
    if (status != TYPESET_OK) return status;
    if (!typeset_resume_valid(provider, saved)) return TYPESET_STALE;
    return saved->serial == cursor->serial ? TYPESET_OK : TYPESET_NO_PROGRESS;
}

TypesetStatus typeset_flow_restore(const TypesetFlowProvider* provider,
                                   const TypesetResume* saved, TypesetResume* restored) {
    if (!provider || !saved || !restored || !provider->restore) return TYPESET_INVALID;
    if (!typeset_resume_valid(provider, saved)) return TYPESET_STALE;
    TypesetStatus status = provider->restore(provider->context, saved, restored);
    if (status != TYPESET_OK) return status;
    if (!typeset_resume_valid(provider, restored)) return TYPESET_STALE;
    return restored->serial == saved->serial ? TYPESET_OK : TYPESET_NO_PROGRESS;
}

TypesetStatus typeset_flow_next(const TypesetFlowProvider* provider, const TypesetResume* cursor,
                                TypesetContribution* contribution, TypesetResume* next) {
    if (!provider || !cursor || !contribution || !next || !provider->next) return TYPESET_INVALID;
    if (!typeset_resume_valid(provider, cursor)) return TYPESET_STALE;
    TypesetStatus status = provider->next(provider->context, cursor, contribution, next);
    if (status != TYPESET_OK) return status;
    if (!typeset_resume_valid(provider, next)) return TYPESET_STALE;
    return next->serial > cursor->serial ? TYPESET_OK : TYPESET_NO_PROGRESS;
}
