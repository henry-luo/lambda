#include "typeset_regions.hpp"
#include "../lib/mem_factory.h"
#include "../lib/mem_grow.hpp"
#include <math.h>
#include <string.h>

static bool region_cursor_valid(const TypesetRegionMaterial* material, const TypesetResume& cursor) {
    return material && material->identity && material->measure &&
        material->source.provider == cursor.provider && material->source.generation == cursor.generation;
}

static TypesetStatus region_slice_valid(const TypesetRegionMaterial* material,
        const TypesetResume& cursor, const TypesetRegionSlice& slice, float available) {
    float height = slice.metrics.height + slice.metrics.depth;
    if (!region_cursor_valid(material, slice.end)) return TYPESET_STALE;
    return slice.end.serial <= cursor.serial || !isfinite(height) || height <= 0.0f ||
        height > available || (!material->split && !slice.complete) ? TYPESET_NO_PROGRESS : TYPESET_OK;
}

void typeset_region_plan_dispose(TypesetRegionPlan* plan) {
    if (!plan) return;
    if (plan->scratch && (!plan->leases || --*plan->leases == 0)) mem_pool_destroy(plan->scratch);
    *plan = {};
}

TypesetStatus typeset_region_plan_retain(const TypesetRegionPlan* plan, TypesetRegionPlan* retained) {
    if (!plan || !retained || retained->scratch) return TYPESET_INVALID;
    if (plan->scratch) {
        if (!plan->leases || *plan->leases == SIZE_MAX) return TYPESET_BUDGET_EXHAUSTED;
        ++*plan->leases;
    }
    *retained = *plan;
    return TYPESET_OK;
}

TypesetStatus typeset_region_checkpoint(TypesetRegionQueue* queue, Pool* scratch, TypesetRegionCheckpoint* checkpoint) {
    if (!queue || !scratch || !checkpoint || checkpoint->queue || (queue->count && !queue->entries)) return TYPESET_INVALID;
    if (queue->count > SIZE_MAX / sizeof(TypesetRegionPending)) return TYPESET_BUDGET_EXHAUSTED;
    TypesetRegionPending* entries = queue->count ? (TypesetRegionPending*)pool_alloc(scratch, queue->count * sizeof(TypesetRegionPending)) : nullptr;
    if (queue->count && !entries) return TYPESET_OUT_OF_MEMORY;
    if (queue->count) memcpy(entries, queue->entries, queue->count * sizeof(TypesetRegionPending));
    *checkpoint = {queue, queue->version, entries, queue->count};
    return TYPESET_OK;
}

TypesetStatus typeset_region_restore(TypesetRegionCheckpoint* checkpoint, TypesetRegionPlan* retained) {
    if (!checkpoint || !checkpoint->queue) return TYPESET_INVALID;
    TypesetRegionQueue* queue = checkpoint->queue;
    if (queue->version < checkpoint->version || (retained && retained->scratch &&
        (retained->queue != queue || retained->version != checkpoint->version))) return TYPESET_STALE;
    if (queue->version == UINT64_MAX) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(queue->pool, &queue->entries, &queue->capacity, checkpoint->count, 16)) return TYPESET_OUT_OF_MEMORY;
    if (checkpoint->count) memcpy(queue->entries, checkpoint->entries, checkpoint->count * sizeof(TypesetRegionPending));
    queue->count = checkpoint->count;
    // rollback changes the queue incarnation; rejected plans must stay stale.
    queue->version++;
    if (retained && retained->scratch) retained->version = queue->version;
    *checkpoint = {};
    return TYPESET_OK;
}

TypesetStatus typeset_region_plan(const TypesetRegionQueue* queue,
        const TypesetRegionMaterial* const* anchors, size_t anchor_count,
        const TypesetRegionConstraints* constraints, float body_height, float separator_height,
        TypesetRegionPlan* result) {
    if (!queue || !queue->pool || !result || result->scratch || !constraints ||
        (anchor_count && !anchors) || (queue->count && !queue->entries) ||
        !isfinite(constraints->inline_size) || constraints->inline_size <= 0.0f ||
        !isfinite(constraints->available_height) || constraints->available_height < 0.0f ||
        !isfinite(constraints->reference_height) || constraints->reference_height < 0.0f ||
        !isfinite(body_height) || body_height < 0.0f || !isfinite(separator_height) ||
        !isfinite(constraints->minimum_height) || constraints->minimum_height < 0.0f)
        return TYPESET_INVALID;
    if (body_height > constraints->available_height) return TYPESET_UNPLACEABLE;
    if (queue->count > queue->limit || anchor_count > queue->limit - queue->count ||
        queue->limit > SIZE_MAX / sizeof(TypesetRegionPlacement)) return TYPESET_BUDGET_EXHAUSTED;
    size_t total = queue->count + anchor_count;
    TypesetRegionPlan plan = {};
    plan.queue = queue; plan.version = queue->version;
    plan.scratch = mem_pool_create(queue->memory, MEM_ROLE_LAYOUT, "typeset.region.trial");
    if (!plan.scratch) return TYPESET_OUT_OF_MEMORY;
    plan.leases = (size_t*)pool_alloc(plan.scratch, sizeof(size_t));
    if (!plan.leases) { typeset_region_plan_dispose(&plan); return TYPESET_OUT_OF_MEMORY; }
    *plan.leases = 1;
    if (total) {
        plan.placements = (TypesetRegionPlacement*)pool_calloc(plan.scratch, total * sizeof(TypesetRegionPlacement));
        plan.pending = (TypesetRegionPending*)pool_calloc(plan.scratch, total * sizeof(TypesetRegionPending));
        if (!plan.placements || !plan.pending) { typeset_region_plan_dispose(&plan); return TYPESET_OUT_OF_MEMORY; }
    }
    TypesetRegionSlice* minimum = total ? (TypesetRegionSlice*)pool_calloc(plan.scratch, total * sizeof(TypesetRegionSlice)) : nullptr;
    if (total && !minimum) { typeset_region_plan_dispose(&plan); return TYPESET_OUT_OF_MEMORY; }
    TypesetStatus status = TYPESET_OK;
    float required = 0.0f;
    size_t admitted = 0;
    for (size_t i = 0; i < total; i++) {
        bool incoming = i >= queue->count;
        const TypesetRegionMaterial* material = incoming ? anchors[i - queue->count] : queue->entries[i].material;
        TypesetResume cursor = incoming && material ? material->start : incoming ? TypesetResume{} : queue->entries[i].cursor;
        if (!region_cursor_valid(material, cursor)) { status = TYPESET_STALE; break; }
        if (incoming && material->delay_pages > UINT32_MAX - constraints->page_number) { status = TYPESET_BUDGET_EXHAUSTED; break; }
        uint32_t earliest = incoming ? constraints->page_number + material->delay_pages : queue->entries[i].earliest_page;
        bool eligible = constraints->page_number >= earliest;
        for (size_t j = 0; j < i; j++) {
            const TypesetRegionMaterial* prior = j < queue->count ? queue->entries[j].material : anchors[j - queue->count];
            if (prior->identity == material->identity && prior->source.provider == material->source.provider &&
                prior->source.generation == material->source.generation) { status = TYPESET_INVALID; break; }
        }
        if (status != TYPESET_OK) break;
        float available = constraints->minimum_height > constraints->available_height - body_height ? 0.0f :
            constraints->available_height - body_height - separator_height - required;
        TypesetRegionConstraints region = *constraints; region.available_height = fmaxf(0.0f, available);
        region.minimum = true;
        region.retain_tail = false;
        TypesetStatus measured = !eligible || admitted != i || (material->clear_before && i) || available <= 0.0f ? TYPESET_UNPLACEABLE :
            material->measure(material->context, &cursor, &region, material->split, plan.scratch, &minimum[i]);
        if (measured == TYPESET_UNPLACEABLE) {
            // An anchor can wait only when its producer permits deferred placement.
            if ((incoming && !constraints->defer_anchors && !material->defer_anchor) ||
                (!body_height && !constraints->occupied && !admitted && eligible)) {
                status = TYPESET_UNPLACEABLE; break;
            }
            continue;
        }
        if (measured != TYPESET_OK) { status = measured; break; }
        status = region_slice_valid(material, cursor, minimum[i], available);
        if (status != TYPESET_OK) break;
        required += minimum[i].metrics.height + minimum[i].metrics.depth; admitted++;
    }
    float used = 0.0f;
    for (size_t i = 0; status == TYPESET_OK && i < total; i++) {
        bool incoming = i >= queue->count;
        const TypesetRegionMaterial* material = i < queue->count ? queue->entries[i].material : anchors[i - queue->count];
        TypesetResume cursor = i < queue->count ? queue->entries[i].cursor : material->start;
        uint32_t earliest = i < queue->count ? queue->entries[i].earliest_page : constraints->page_number + material->delay_pages;
        if (i >= admitted) { plan.pending[plan.pending_count++] = {material, cursor, earliest}; continue; }
        required -= minimum[i].metrics.height + minimum[i].metrics.depth;
        float available = constraints->available_height - body_height - separator_height - used - required;
        TypesetRegionConstraints region = *constraints; region.available_height = available; region.minimum = false;
        region.retain_tail = constraints->retain_tail && i + 1 == total && !plan.pending_count;
        TypesetRegionSlice slice = {};
        status = material->measure(material->context, &cursor, &region, material->split, plan.scratch, &slice);
        bool can_defer = !incoming || constraints->defer_anchors || material->defer_anchor;
        if (status == TYPESET_UNPLACEABLE && region.retain_tail &&
            (plan.count || ((body_height > 0.0f || constraints->occupied) && can_defer))) {
            // an indivisible tail can wait after an already progressing prefix, preserving its exact cursor.
            plan.pending[plan.pending_count++] = {material, cursor, earliest};
            status = TYPESET_OK; break;
        }
        if (status == TYPESET_OK && region.retain_tail && slice.complete) status = TYPESET_NO_PROGRESS;
        if (status == TYPESET_OK) status = region_slice_valid(material, cursor, slice, available);
        if (status != TYPESET_OK) break;
        float height = slice.metrics.height + slice.metrics.depth;
        plan.placements[plan.count++] = {material, cursor, slice, {0.0f, used, constraints->inline_size, height}};
        used += height;
        if (!slice.complete) plan.pending[plan.pending_count++] = {material, slice.end, earliest};
    }
    if (status == TYPESET_OK) {
        // A region floor is empty space after its content, never a fabricated material slice.
        plan.reserved_height = plan.count ? fmaxf(constraints->minimum_height, separator_height + used) : 0.0f;
        float top = constraints->from_start ? separator_height : constraints->available_height - plan.reserved_height + separator_height;
        for (size_t i = 0; i < plan.count; i++) plan.placements[i].rect.y += top;
        *result = plan;
    } else typeset_region_plan_dispose(&plan);
    return status;
}

TypesetStatus typeset_region_commit(TypesetRegionQueue* queue, const TypesetRegionPlan* plan) {
    if (!queue || !plan || !plan->scratch || plan->queue != queue) return TYPESET_INVALID;
    if (plan->version != queue->version) return TYPESET_STALE;
    if (queue->version == UINT64_MAX) return TYPESET_BUDGET_EXHAUSTED;
    if (!lam::pool_grow_array(queue->pool, &queue->entries, &queue->capacity, plan->pending_count, 16))
        return TYPESET_OUT_OF_MEMORY;
    if (plan->pending_count) memcpy(queue->entries, plan->pending, plan->pending_count * sizeof(TypesetRegionPending));
    queue->count = plan->pending_count; queue->version++;
    return TYPESET_OK;
}
