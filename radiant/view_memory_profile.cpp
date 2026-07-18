#include "layout.hpp"

#include "event.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lib/log.h"
#include "../lib/mem_context.h"

#include <stdio.h>
#include <string.h>

typedef struct ViewMemoryDomain {
    bool present;
    uint32_t instance_count;
    uint64_t bytes_reserved;
    uint64_t bytes_in_use;
    uint64_t backing_bytes;
    uint64_t direct_bytes;
    uint64_t committed_bytes;
    uint64_t recyclable_bytes;
    uint64_t waste_bytes;
    uint64_t overhead_bytes;
    uint64_t high_water_bytes;
    uint64_t cumulative_bytes;
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t reuse_hits;
    uint64_t reuse_misses;
    uint64_t split_count;
    uint64_t coalesce_count;
    uint64_t bump_back_count;
    uint64_t fresh_chunk_count;
    uint64_t fresh_growth_bytes;
    uint64_t reset_count;
    uint64_t clear_count;
    uint64_t active_scope_count;
    uint64_t chunk_count;
} ViewMemoryDomain;

static void view_memory_domain_add(ViewMemoryDomain* domain,
                                   const MemStatSample* sample) {
    if (!domain || !sample) return;
    domain->present = true;
    domain->instance_count++;
#define ADD_FIELD(field) domain->field += sample->field
    ADD_FIELD(bytes_reserved);
    ADD_FIELD(bytes_in_use);
    ADD_FIELD(backing_bytes);
    ADD_FIELD(direct_bytes);
    ADD_FIELD(committed_bytes);
    ADD_FIELD(recyclable_bytes);
    ADD_FIELD(waste_bytes);
    ADD_FIELD(overhead_bytes);
    ADD_FIELD(high_water_bytes);
    ADD_FIELD(cumulative_bytes);
    ADD_FIELD(alloc_count);
    ADD_FIELD(free_count);
    ADD_FIELD(reuse_hits);
    ADD_FIELD(reuse_misses);
    ADD_FIELD(split_count);
    ADD_FIELD(coalesce_count);
    ADD_FIELD(bump_back_count);
    ADD_FIELD(fresh_chunk_count);
    ADD_FIELD(fresh_growth_bytes);
    ADD_FIELD(reset_count);
    ADD_FIELD(clear_count);
    ADD_FIELD(active_scope_count);
    ADD_FIELD(chunk_count);
#undef ADD_FIELD
}

static void view_memory_domain_json(JsonWriter* writer, const char* name,
                                    const ViewMemoryDomain* domain) {
    jw_key(writer, name);
    jw_obj_begin(writer);
        jw_kv_bool(writer, "present", domain->present);
        jw_kv_uint(writer, "instance_count", domain->instance_count);
        jw_kv_uint(writer, "reserved_bytes", domain->bytes_reserved);
        jw_kv_uint(writer, "live_or_active_bytes", domain->bytes_in_use);
        jw_kv_uint(writer, "backing_bytes", domain->backing_bytes);
        jw_kv_uint(writer, "direct_live_bytes", domain->direct_bytes);
        jw_kv_uint(writer, "committed_bytes", domain->committed_bytes);
        jw_kv_uint(writer, "recyclable_bytes", domain->recyclable_bytes);
        jw_kv_uint(writer, "waste_bytes", domain->waste_bytes);
        jw_kv_uint(writer, "overhead_bytes", domain->overhead_bytes);
        jw_kv_uint(writer, "high_water_bytes", domain->high_water_bytes);
        jw_kv_uint(writer, "cumulative_bytes", domain->cumulative_bytes);
        jw_kv_uint(writer, "allocation_count", domain->alloc_count);
        jw_kv_uint(writer, "free_count", domain->free_count);
        jw_kv_uint(writer, "reuse_hits", domain->reuse_hits);
        jw_kv_uint(writer, "reuse_misses", domain->reuse_misses);
        jw_kv_uint(writer, "split_count", domain->split_count);
        jw_kv_uint(writer, "coalesce_count", domain->coalesce_count);
        jw_kv_uint(writer, "bump_back_count", domain->bump_back_count);
        jw_kv_uint(writer, "fresh_chunk_count", domain->fresh_chunk_count);
        jw_kv_uint(writer, "fresh_growth_bytes", domain->fresh_growth_bytes);
        jw_kv_uint(writer, "reset_count", domain->reset_count);
        jw_kv_uint(writer, "clear_count", domain->clear_count);
        jw_kv_uint(writer, "active_scope_count", domain->active_scope_count);
        jw_kv_uint(writer, "chunk_count", domain->chunk_count);
    jw_obj_end(writer);
}

static bool view_memory_label_is(const MemStatSample* sample,
                                 const char* label) {
    return sample && label && strcmp(sample->label, label) == 0;
}

bool view_memory_profile_write(DomDocument* doc, const char* input_file,
                               const char* output_path) {
    if (!doc || !doc->view_tree || !output_path) return false;
    MemSnapshot* snapshot = mem_snapshot_capture(NULL);
    if (!snapshot) return false;

    uint32_t doc_id = doc->services.mem_ctx
        ? mem_context_doc_id((MemContext*)doc->services.mem_ctx) : 0;
    ViewMemoryDomain document_pool = {};
    ViewMemoryDomain node_arena = {};
    ViewMemoryDomain prop_pool = {};
    ViewMemoryDomain canonical_prop_arena = {};
    ViewMemoryDomain scratch_arena = {};
    ViewMemoryDomain style_epoch_pool = {};
    uint64_t attribution_errors = 0;

    for (uint32_t i = 0; i < snapshot->count; i++) {
        const MemStatSample* sample = &snapshot->samples[i];
        if (sample->doc_id != doc_id) continue;
        if (sample->flags & MEM_FLAG_ATTRIBUTION_ERROR) attribution_errors++;
        if (view_memory_label_is(sample, "dom.document.pool")) {
            view_memory_domain_add(&document_pool, sample);
        } else if (view_memory_label_is(sample, "dom.node.arena")) {
            view_memory_domain_add(&node_arena, sample);
        } else if (view_memory_label_is(sample, "view_tree.prop_pool")) {
            view_memory_domain_add(&prop_pool, sample);
        } else if (view_memory_label_is(sample, "view_tree.canonical_prop_arena")) {
            view_memory_domain_add(&canonical_prop_arena, sample);
        } else if (view_memory_label_is(sample, "view_tree.scratch_arena")) {
            view_memory_domain_add(&scratch_arena, sample);
        } else if (view_memory_label_is(sample, "style.canonical.epoch.pool")) {
            view_memory_domain_add(&style_epoch_pool, sample);
        }
    }

    StyleEpochStats style_stats = {};
    style_epoch_get_stats(doc, &style_stats);
    const CanonicalPropStats* prop_stats = &doc->view_tree->canonical_stats;
    uint64_t physical_reserved = document_pool.bytes_reserved +
        prop_pool.bytes_reserved + style_epoch_pool.bytes_reserved;
    uint64_t physical_live = document_pool.bytes_in_use +
        prop_pool.bytes_in_use + style_epoch_pool.bytes_in_use;

    char buffer[16384];
    JsonWriter writer;
    jw_init(&writer, buffer, sizeof(buffer));
    jw_obj_begin(&writer);
        jw_kv_uint(&writer, "schema_version", 1);
        jw_kv_str(&writer, "sample_point", "post_layout_pre_output");
        jw_kv_str(&writer, "file", input_file ? input_file : "");
        jw_kv_uint(&writer, "doc_id", doc_id);
        jw_kv_uint(&writer, "layout_generation", doc->view_tree->layout_generation);
        jw_key(&writer, "physical_total");
        jw_obj_begin(&writer);
            jw_kv_uint(&writer, "reserved_bytes", physical_reserved);
            jw_kv_uint(&writer, "live_bytes", physical_live);
        jw_obj_end(&writer);
        jw_key(&writer, "domains");
        jw_obj_begin(&writer);
            view_memory_domain_json(&writer, "dom.document.pool", &document_pool);
            view_memory_domain_json(&writer, "dom.node.arena", &node_arena);
            view_memory_domain_json(&writer, "view_tree.prop_pool", &prop_pool);
            view_memory_domain_json(&writer, "view_tree.canonical_prop_arena",
                                    &canonical_prop_arena);
            view_memory_domain_json(&writer, "view_tree.scratch_arena", &scratch_arena);
            view_memory_domain_json(&writer, "style.canonical.epoch.pool",
                                    &style_epoch_pool);
        jw_obj_end(&writer);
        jw_key(&writer, "logical_composites");
        jw_obj_begin(&writer);
            jw_kv_uint(&writer, "dom_storage_active_bytes",
                       document_pool.direct_bytes + node_arena.bytes_in_use);
            jw_kv_uint(&writer, "view_prop_storage_active_bytes",
                       prop_pool.direct_bytes + canonical_prop_arena.bytes_in_use +
                       scratch_arena.bytes_in_use);
            jw_kv_uint(&writer, "canonical_style_live_bytes",
                       style_epoch_pool.bytes_in_use);
        jw_obj_end(&writer);
        jw_key(&writer, "canonical_props");
        jw_obj_begin(&writer);
            jw_kv_uint(&writer, "inline_canonical_count",
                       doc->view_tree->inline_canonical_count);
            jw_kv_uint(&writer, "inline_lookups", prop_stats->inline_lookups);
            jw_kv_uint(&writer, "inline_hits", prop_stats->inline_hits);
            jw_kv_uint(&writer, "inline_misses", prop_stats->inline_misses);
            jw_kv_uint(&writer, "inline_exact_compares",
                       prop_stats->inline_exact_compares);
            jw_kv_uint(&writer, "inline_collisions", prop_stats->inline_collisions);
            jw_kv_uint(&writer, "inline_promotions", prop_stats->inline_promotions);
            jw_kv_uint(&writer, "inline_cows", prop_stats->inline_cows);
            jw_kv_uint(&writer, "cap_fallbacks", prop_stats->cap_fallbacks);
            jw_kv_uint(&writer, "index_bytes", prop_stats->index_bytes);
        jw_obj_end(&writer);
        jw_key(&writer, "style_epochs");
        jw_obj_begin(&writer);
            jw_kv_uint(&writer, "current_epoch_id", style_stats.current_epoch_id);
            jw_kv_uint(&writer, "epoch_count", style_stats.epoch_count);
            jw_kv_uint(&writer, "retired_epoch_count", style_stats.retired_epoch_count);
            jw_kv_uint(&writer, "released_epoch_count", style_stats.released_epoch_count);
            jw_kv_uint(&writer, "canonical_tree_count", style_stats.canonical_tree_count);
            jw_kv_uint(&writer, "canonical_entry_count", style_stats.canonical_entry_count);
            jw_kv_uint(&writer, "recipe_entry_count", style_stats.recipe_entry_count);
            jw_kv_uint(&writer, "bound_element_refs", style_stats.bound_element_refs);
            jw_kv_uint(&writer, "lookup_count", style_stats.lookup_count);
            jw_kv_uint(&writer, "hit_count", style_stats.hit_count);
            jw_kv_uint(&writer, "miss_count", style_stats.miss_count);
            jw_kv_uint(&writer, "exact_compare_count", style_stats.exact_compare_count);
            jw_kv_uint(&writer, "collision_count", style_stats.collision_count);
            jw_kv_uint(&writer, "cow_count", style_stats.cow_count);
            jw_kv_uint(&writer, "current_reserved_bytes",
                       style_stats.current_reserved_bytes);
            jw_kv_uint(&writer, "current_live_bytes", style_stats.current_live_bytes);
            jw_kv_uint(&writer, "retired_referenced_reserved_bytes",
                       style_stats.retired_referenced_reserved_bytes);
        jw_obj_end(&writer);
        jw_key(&writer, "comparability");
        jw_obj_begin(&writer);
            jw_kv_bool(&writer, "all_six_domains_present",
                       document_pool.present && node_arena.present && prop_pool.present &&
                       canonical_prop_arena.present && scratch_arena.present &&
                       style_epoch_pool.present);
            jw_kv_uint(&writer, "attribution_errors", attribution_errors);
        jw_obj_end(&writer);
    jw_obj_end(&writer);

    const char* json = jw_finish(&writer);
    bool success = false;
    if (!json) {
        log_error("VIEW_MEMORY_PROFILE: JSON buffer overflow for %s",
                  input_file ? input_file : "(unknown)");
    } else {
        FILE* file = fopen(output_path, "w");
        if (!file) {
            log_error("VIEW_MEMORY_PROFILE: could not open %s", output_path);
        } else {
            size_t length = strlen(json);
            success = fwrite(json, 1, length, file) == length;
            fclose(file);
            if (success) {
                log_info("VIEW_MEMORY_PROFILE: captured post-layout domains in %s",
                         output_path);
            }
        }
    }
    mem_snapshot_free(snapshot);
    return success;
}
