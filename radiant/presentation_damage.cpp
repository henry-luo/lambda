// Presentation damage (vibe/radiant/Radiant_Design_Animation_Repaint.md §3, stage A).
// A paint-only commit (event.cpp) changes only presentation transform/opacity, so the
// pixels that change are the written elements' old and new visual bounds. Old bounds
// come from the last recording; new bounds from the frame's own recording, which the
// raster driver makes before it clears (render_output.cpp). Every painting display-list
// op carries trusted device-space bounds (dirty replay already skips by them), so an
// element's recorded items bound everything it paints, transformed or projected.
#include "render.hpp"
#include "../lib/log.h"
#include "../lib/shell.h"

#include <math.h>
#include <string.h>

// Entries not written for this many recordings are dropped: their element stopped animating.
static const uint64_t PRESENTATION_DAMAGE_IDLE_FRAMES = 600;

bool presentation_damage_enabled() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = shell_getenv("RADIANT_PRESENTATION_DAMAGE");
        enabled = !(value && strcmp(value, "off") == 0);
    }
    return enabled == 1;
}

static PresentationDamageEntry* damage_find(PresentationDamage* damage, uint32_t id) {
    for (int i = 0; i < damage->count; i++) {
        if (damage->entries[i].node.expected_id == id) return &damage->entries[i];
    }
    return nullptr;
}

static PresentationDamageEntry* damage_track(PresentationDamage* damage, DomElement* element) {
    DomNode* node = static_cast<DomNode*>(element);
    PresentationDamageEntry* entry = damage_find(damage, node->id);
    if (entry && entry->node.address != node) {
        // a recycled id at a different address is a different element
        *entry = {};
        entry->node = dom_node_ref(node);
        return entry;
    }
    if (entry) return entry;
    if (damage->count == PRESENTATION_DAMAGE_CAPACITY) {
        // replace the least recently written entry
        int oldest = 0;
        for (int i = 1; i < damage->count; i++) {
            if (damage->entries[i].last_write < damage->entries[oldest].last_write) oldest = i;
        }
        entry = &damage->entries[oldest];
    } else {
        entry = &damage->entries[damage->count++];
    }
    *entry = {};
    entry->node = dom_node_ref(node);
    return entry;
}

// The element establishing the preserve-3d context `element` is a plane of, or null for flat
// content. Composition orders and splits every plane of a context against the others
// (render_css3d.cpp), so a write to one plane can change any other plane's pixels: a write
// inside a context damages the whole context.
static DomElement* damage_context_root(DomElement* element) {
    DomElement* root = nullptr;
    for (DomNode* node = static_cast<DomNode*>(element)->parent; node && node->is_element(); node = node->parent) {
        DomElement* ancestor = node->as_element();
        if (!radiant::transform_preserves_3d(ancestor)) break;
        root = ancestor;
    }
    return root;
}

static bool damage_is_context_root(DomElement* element) {
    DomNode* parent = static_cast<DomNode*>(element)->parent;
    return radiant::transform_preserves_3d(element) &&
        !(parent && parent->is_element() && radiant::transform_preserves_3d(parent->as_element()));
}

// An element carrying presentation transform or opacity is the kind presentation writes move:
// track it from its first recording, so the write that first moves it (an animation's first
// frame) already knows its old bounds instead of forcing a full repaint. Writes alone evict
// entries; a full table skips candidates.
void presentation_damage_note_candidate(PresentationDamage* damage, DomElement* element) {
    if (!damage || !element || !element->specified_style || !presentation_damage_enabled()) return;
    // a 3D context root is damaged with any write inside it, so it needs bounds too
    if (!style_tree_get_presentation_declaration(element->specified_style, CSS_PROPERTY_TRANSFORM) &&
        !style_tree_get_presentation_declaration(element->specified_style, CSS_PROPERTY_OPACITY) &&
        !damage_is_context_root(element)) return;
    DomNode* node = static_cast<DomNode*>(element);
    PresentationDamageEntry* entry = damage_find(damage, node->id);
    if (entry && entry->node.address == node) return;
    if (!entry && damage->count == PRESENTATION_DAMAGE_CAPACITY) return;
    entry = damage_track(damage, element);
    entry->last_write = damage->frame;
}

// Marks a damage rect rounded out to whole device pixels plus one for antialiasing, so the
// selective clear and the replay clip never cut a partially covered edge pixel.
void presentation_damage_mark(DocState* state, const Bound& b) {
    float s = state->presentation_damage.scale > 0.0f ? state->presentation_damage.scale : 1.0f;
    float left = floorf(b.left * s - 1.0f) / s, top = floorf(b.top * s - 1.0f) / s;
    float right = ceilf(b.right * s + 1.0f) / s, bottom = ceilf(b.bottom * s + 1.0f) / s;
    dirty_mark_surface_rect(&state->dirty_tracker, left, top, right - left, bottom - top);
}

bool presentation_damage_commit(DocState* state, DomDocument* doc, DomElement* const* written, int written_count) {
    if (!state || !doc || !written || written_count <= 0 || !presentation_damage_enabled()) return false;
    PresentationDamage* damage = &state->presentation_damage;
    // the written elements, plus the root of each 3D context they are planes of
    DomElement* targets[PRESENTATION_DAMAGE_CAPACITY * 2];
    int count = 0;
    for (int i = 0; i < written_count; i++) {
        // a missing written element stays in the list: it forces the full repaint below
        DomElement* add[2] = {written[i], written[i] ? damage_context_root(written[i]) : nullptr};
        for (int k = 0; k < 2; k++) {
            DomElement* element = add[k];
            if (k == 1 && !element) continue;
            bool seen = false;
            for (int j = 0; j < count && !seen; j++) seen = targets[j] == element;
            if (seen) continue;
            if (count == PRESENTATION_DAMAGE_CAPACITY * 2) return false;
            targets[count++] = element;
        }
    }
    bool known = true;
    for (int i = 0; i < count; i++) {
        if (!targets[i]) { known = false; continue; }
        PresentationDamageEntry* entry = damage_track(damage, targets[i]);
        entry->last_write = damage->frame;
        // an element never recorded has no old bounds to repaint
        if (!entry->has_painted) {
            known = false;
            log_debug("presentation damage: element %u has no painted bounds yet", entry->node.expected_id);
        }
    }
    // a pending full repaint already covers everything; region marking would be lost in it
    if (!known || state->is_dirty || state->dirty_tracker.full_repaint) {
        log_debug("presentation damage: full repaint (targets=%d known=%d dirty=%d full=%d tracked=%d)",
                  count, known, state->is_dirty, state->dirty_tracker.full_repaint, damage->count);
        return false;
    }
    for (int i = 0; i < count; i++) {
        PresentationDamageEntry* entry = damage_find(damage, static_cast<DomNode*>(targets[i])->id);
        entry->damaged = true;
        if (entry->painted.right > entry->painted.left) presentation_damage_mark(state, entry->painted);
    }
    damage->pending = true;
    log_debug("presentation damage: regions for %d element(s)", count);
    return true;
}

void presentation_damage_frame_begin(PresentationDamage* damage) {
    if (!damage) return;
    for (int i = 0; i < damage->count; i++) {
        damage->entries[i].recorded = false;
        damage->entries[i].recording = {};
    }
}

void presentation_damage_note_range(PresentationDamage* damage, const DisplayList* dl,
                                    uint32_t view_id, int begin_index, int end_index) {
    if (!damage || !dl || damage->count == 0 || begin_index < 0) return;
    PresentationDamageEntry* entry = damage_find(damage, view_id);
    if (!entry) return;
    // the element's items only: its begin marker starts from the untransformed layout box,
    // which a turned or moved element no longer paints
    Bound bounds = {};
    bool valid = false;
    int end = end_index < dl->item_count() ? end_index : dl->item_count();
    for (int i = begin_index + 1; i < end; i++) {
        const DisplayItem* item = &dl->data()[i];
        if (dl_op_has_flags(item->op, DL_OP_FLAG_PRESERVES_REPLAY_STATE)) continue;
        float w = item->bounds[2], h = item->bounds[3];
        if (!(w > 0.0f) || !(h > 0.0f)) {
            // dirty replay skips a zero-sized skippable op too; any other op has unknown extent
            if (dl_op_has_flags(item->op, DL_OP_FLAG_DIRTY_SKIPPABLE)) continue;
            w = h = DL_UNBOUNDED_EXTENT;
        }
        float l = item->bounds[0], t = item->bounds[1];
        if (!valid) { bounds = {l, t, l + w, t + h}; valid = true; continue; }
        bounds.left = fminf(bounds.left, l); bounds.top = fminf(bounds.top, t);
        bounds.right = fmaxf(bounds.right, l + w); bounds.bottom = fmaxf(bounds.bottom, t + h);
    }
    if (!valid) { entry->recorded = true; return; }  // painted nothing this frame
    if (!entry->recorded || entry->recording.right <= entry->recording.left) entry->recording = bounds;
    else {
        entry->recording.left = fminf(entry->recording.left, bounds.left);
        entry->recording.top = fminf(entry->recording.top, bounds.top);
        entry->recording.right = fmaxf(entry->recording.right, bounds.right);
        entry->recording.bottom = fmaxf(entry->recording.bottom, bounds.bottom);
    }
    entry->recorded = true;
}

bool presentation_damage_frame_end(DocState* state, DomDocument* doc, float raster_scale, bool presentation_frame) {
    if (!state) return !presentation_frame;
    PresentationDamage* damage = &state->presentation_damage;
    float scale = raster_scale > 0.0f ? raster_scale : 1.0f;
    damage->scale = scale;
    bool ok = true;
    int kept = 0;
    for (int i = 0; i < damage->count; i++) {
        PresentationDamageEntry entry = damage->entries[i];
        // a retired or replaced element leaves the registry; validation precedes any read
        DomNode* node = doc ? dom_node_ref_validate(doc, entry.node) : nullptr;
        bool live = node && node->id == entry.node.expected_id;
        if (entry.recorded) {
            Bound painted = {entry.recording.left / scale, entry.recording.top / scale,
                             entry.recording.right / scale, entry.recording.bottom / scale};
            if (presentation_frame && entry.damaged) {
                // an unbounded extent (a plane crossing the viewer) cannot be a region
                if (entry.recording.right - entry.recording.left >= DL_UNBOUNDED_EXTENT * 0.5f ||
                    entry.recording.bottom - entry.recording.top >= DL_UNBOUNDED_EXTENT * 0.5f) ok = false;
                else if (painted.right > painted.left) presentation_damage_mark(state, painted);
            }
            entry.painted = painted;
            entry.has_painted = true;
        }
        // an element not recorded this frame keeps its last bounds: a culled or reused subtree
        // did not move, and a removed one is pruned below
        entry.damaged = false;
        entry.recorded = false;
        if (!live || damage->frame - entry.last_write > PRESENTATION_DAMAGE_IDLE_FRAMES) continue;
        damage->entries[kept++] = entry;
    }
    log_debug("presentation damage: frame end tracked=%d kept=%d presentation=%d regions=%d",
              damage->count, kept, presentation_frame, ok);
    damage->count = kept;
    damage->frame++;
    if (presentation_frame) damage->pending = false;
    return ok;
}
