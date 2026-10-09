#pragma once
#include "typeset.hpp"
#include "../lib/mempool.h"

struct MemContext;
struct TypesetRegionConstraints {
    float inline_size, available_height;
    uint32_t page_number;
    lam::Up<const TypesetRecord> exact;
    bool minimum; // request the smallest legal prefix before sharing the remaining region
    bool defer_anchors, from_start;
    float reference_height; // containing region height remains stable as the reservation budget shrinks
    bool occupied; // body material outside this region may leave it with a zero budget
    float minimum_height; // reserve this extent only when material actually occupies the region
    bool retain_tail; // leave real material for a later region; producers choose a legal nonterminal prefix
};
struct TypesetRegionSlice {
    TypesetResume end;
    TypesetMetrics metrics;
    lam::Up<const TypesetRecord> paint;
    bool complete;
};
struct TypesetRegionMaterial {
    uint64_t identity;
    TypesetSource source;
    TypesetResume start;
    void* context;
    // Measurement is replayable; all trial-owned payloads live in scratch.
    TypesetStatus (*measure)(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice);
    bool split;
    uint32_t delay_pages;
    bool clear_before; // begin on a fresh region after preceding material has been placed
    bool defer_anchor; // an insertion may follow its reference when the producing policy permits it
};
struct TypesetRegionPending {
    const TypesetRegionMaterial* material;
    TypesetResume cursor;
    uint32_t earliest_page;
};
struct TypesetRegionQueue {
    Pool* pool;
    MemContext* memory;
    uint64_t version;
    TypesetRegionPending* entries;
    size_t count, capacity, limit;
};
struct TypesetRegionPlacement {
    const TypesetRegionMaterial* material;
    TypesetResume start;
    TypesetRegionSlice slice;
    RdtLogicalRect rect;
};
struct TypesetRegionPlan {
    Pool* scratch;
    size_t* leases;
    const TypesetRegionQueue* queue;
    uint64_t version;
    TypesetRegionPlacement* placements;
    TypesetRegionPending* pending;
    size_t count, pending_count;
    float reserved_height;
};
struct TypesetRegionCheckpoint {
    TypesetRegionQueue* queue;
    uint64_t version;
    TypesetRegionPending* entries;
    size_t count;
};

// Required anchors receive a first slice; deferrable insertions preserve their cursor in the queue.
// A signed separator permits producer-owned margins without changing content metrics.
TypesetStatus typeset_region_plan(const TypesetRegionQueue* queue,
    const TypesetRegionMaterial* const* anchors, size_t anchor_count,
    const TypesetRegionConstraints* constraints, float body_height, float separator_height,
    TypesetRegionPlan* result);
TypesetStatus typeset_region_commit(TypesetRegionQueue* queue, const TypesetRegionPlan* plan);
// retained plans keep exact scratch payloads alive across abandoned page trials.
TypesetStatus typeset_region_plan_retain(const TypesetRegionPlan* plan, TypesetRegionPlan* retained);
TypesetStatus typeset_region_checkpoint(TypesetRegionQueue* queue, Pool* scratch, TypesetRegionCheckpoint* checkpoint);
TypesetStatus typeset_region_restore(TypesetRegionCheckpoint* checkpoint, TypesetRegionPlan* retained = nullptr);
void typeset_region_plan_dispose(TypesetRegionPlan* plan);
