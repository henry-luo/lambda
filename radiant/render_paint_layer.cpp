// Paint layers (vibe/radiant/Radiant_Design_Animation_Repaint.md §4, stage B).
// A subtree that a presentation transform moves (the element's own, or an ancestor's
// within a preserve-3d context, where each flat plane is the unit) is rasterised once into
// a document-owned surface and painted as one projected image on later frames, instead of
// replaying its items under the 3D matrix every frame. The surface is captured in the
// subtree's own plane: the element's transform is its placement, applied at paint time.
// Modelled on the inline-SVG raster layer (render_svg_inline.cpp, RAD_14 §4.4): a content
// stamp of generations, capture on the second identical paint, the shared two-pass raster.
#include "render.hpp"
#include "render_css3d.hpp"
#include "render_metrics.hpp"
#include "layout.hpp"
#include "event.hpp"
#include "../lib/log.h"
#include "../lib/shell.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"

#include <math.h>
#include <string.h>

static const size_t PAINT_LAYER_MAX_BYTES = (size_t)48 << 20;         // one layer
static const size_t PAINT_LAYER_TOTAL_MAX_BYTES = (size_t)256 << 20;  // per document
// captures cost about two live paints each; spreading them keeps one frame from stalling
static const int PAINT_LAYER_CAPTURES_PER_FRAME = 2;
// a subtree painting no more than this many cheap items (a shade's one two-stop gradient)
// is cheaper to paint live than to resample as a full-size projected bitmap every frame;
// an image or a many-stop gradient (a repeating texture) is never cheap
static const int PAINT_LAYER_TRIVIAL_ITEMS = 2;
static const int PAINT_LAYER_TRIVIAL_STOPS = 4;
// a revalidation only records; a document-wide change (an inline style or layout anywhere)
// revalidates every layer at once
static const int PAINT_LAYER_REVALIDATIONS_PER_FRAME = 16;
// a surface not painted for this many frames is released when room is needed
static const uint64_t PAINT_LAYER_IDLE_FRAMES = 600;

enum PaintLayerMode { PAINT_LAYER_MODE_ON = 0, PAINT_LAYER_MODE_OFF, PAINT_LAYER_MODE_EAGER };

// RADIANT_PAINT_LAYER=off disables layers; =eager captures on the first paint, so a single
// render exercises the layer path and can be diffed against `off`.
static PaintLayerMode paint_layer_mode() {
    static int mode = -1;
    if (mode < 0) {
        const char* env = shell_getenv("RADIANT_PAINT_LAYER");
        mode = PAINT_LAYER_MODE_ON;
        if (env && strcmp(env, "off") == 0) mode = PAINT_LAYER_MODE_OFF;
        else if (env && strcmp(env, "eager") == 0) mode = PAINT_LAYER_MODE_EAGER;
    }
    return (PaintLayerMode)mode;
}

// parity checks (event_sim) render a frame again with layers held or suspended
static PaintLayerOverride paint_layer_override = PAINT_LAYER_NORMAL;
static PaintLayerFrameStats paint_layer_stats = {};
void render_paint_layer_override(PaintLayerOverride value) { paint_layer_override = value; }
PaintLayerOverride render_paint_layer_current_override() { return paint_layer_override; }
PaintLayerFrameStats render_paint_layer_take_stats() {
    PaintLayerFrameStats stats = paint_layer_stats;
    paint_layer_stats = {};
    return stats;
}

// A translation placement (a leaf at rest: identity on the right, a page width on the left)
// can land the layer's samples 1:1 when the capture origin absorbs the translation's
// fraction; any other placement resamples them (B5: soft while moving, sharp at rest).
static bool paint_layer_translation(const RasterRenderContext* rdcon, float* tx, float* ty) {
    *tx = *ty = 0.0f;
    if (!rdcon->has_transform) return true;
    const RdtMatrix& m = rdcon->transform;
    const float e = 0.0005f;
    if (!(fabsf(m.e11 - 1.0f) < e && fabsf(m.e22 - 1.0f) < e && fabsf(m.e12) < e && fabsf(m.e21) < e &&
          fabsf(m.e31) < e * 0.01f && fabsf(m.e32) < e * 0.01f && fabsf(m.e33 - 1.0f) < e)) return false;
    *tx = m.e13; *ty = m.e23;
    return true;
}

static bool paint_layer_aligned(float origin, float translation) {
    float device = origin + translation;
    return fabsf(device - roundf(device)) < 0.002f;
}

struct PaintLayerStamp {
    uint32_t generation;            // layer_generation of the root
    uint64_t document_epoch;        // inherited styles and stylesheets
    uint64_t content_version;       // interaction, form and scroll state
    uint64_t layout_count;          // completed document flows
    uint64_t font_generation;
    uint64_t font_resource_generation;
    uint64_t image_resource_generation; // decoded pixels, GIF frames
    float scale;
    Rect extent;                    // the plane's painted extent (layout px), so its geometry
};

struct PaintLayerEntry : PaintLayerStamp {
    DomNodeRef element;             // the node id guards against a recycled element address
    ImageSurface* surface;          // null until the content was painted twice unchanged
    float left, top;                // the capture origin, in device paint coordinates
    float last_tx, last_ty;         // the last translation placement, to tell rest from motion
    bool last_translation;
    bool live_content;              // the stamped subtree holds media, animation or blending
    bool from_layer;                // the last paint drew the layer, rather than the subtree
    uint64_t fingerprint;           // the captured recording's content (dl_content_fingerprint)
    bool fingerprinted;
    uint64_t seen_frame;            // the last recording that reached this root
    bool misaligned;                // last drawn resampled under a translation (not at rest yet)
    uint64_t last_frame;
    lam::Own<PaintLayerEntry> next;
};

// The registry and its entries live in one pool under the document's memory context;
// entries of removed elements are reused, so the list stays bounded by live roots.
struct PaintLayerRegistry : DomDocumentResourceData {
    lam::Own<PaintLayerEntry> entries;
    size_t cached_bytes;
    int idle_attempts;              // idle capture frames since the last capture made progress
    lam::Own<Pool> pool;
    lam::Up<DomDocument> document;
};

static void paint_layer_release_surface(PaintLayerRegistry* registry, PaintLayerEntry* entry) {
    if (!entry->surface) return;
    registry->cached_bytes -= (size_t)entry->surface->pitch * (size_t)entry->surface->height;
    // a display list still holding it reads through its handle; destroy defers while read
    image_surface_destroy(entry->surface);
    entry->surface = nullptr;
}

static void paint_layer_registry_destroy(DomDocumentResourceData* data) {
    PaintLayerRegistry* registry = (PaintLayerRegistry*)data;
    if (!registry) return;
    for (PaintLayerEntry* entry = registry->entries; entry; entry = entry->next) {
        paint_layer_release_surface(registry, entry);
    }
    registry->document->services.paint_layer_registry = nullptr;
    mem_pool_destroy(registry->pool);
}

static PaintLayerRegistry* paint_layer_registry_for_document(DomDocument* document) {
    PaintLayerRegistry* registry = (PaintLayerRegistry*)document->services.paint_layer_registry;
    if (registry) return registry;
    MemContext* context = (MemContext*)document->services.mem_ctx;
    Pool* pool = mem_pool_create(context ? context : mem_context_process(MEM_ROLE_RENDER),
                                 MEM_ROLE_RENDER, "paint_layer.registry");
    if (!pool) return nullptr;
    registry = (PaintLayerRegistry*)pool_calloc(pool, sizeof(PaintLayerRegistry));
    if (!registry || !dom_document_add_resource(document, registry, paint_layer_registry_destroy)) {
        mem_pool_destroy(pool);
        return nullptr;
    }
    registry->pool = lam::own(pool);
    registry->document = lam::up(document);
    document->services.paint_layer_registry = registry;
    return registry;
}

static PaintLayerEntry* paint_layer_entry_for_element(PaintLayerRegistry* registry, DomElement* element) {
    DomNode* node = static_cast<DomNode*>(element);
    PaintLayerEntry* reusable = nullptr;
    for (PaintLayerEntry* entry = registry->entries; entry; entry = entry->next) {
        if (entry->element.address == node && entry->element.expected_id == node->id) return entry;
        if (!reusable && !dom_node_ref_validate(registry->document, entry->element)) reusable = entry;
    }
    PaintLayerEntry* entry = reusable;
    if (entry) {
        paint_layer_release_surface(registry, entry);
        lam::Own<PaintLayerEntry> next = entry->next;
        *static_cast<PaintLayerStamp*>(entry) = {};
        entry->live_content = false;
        entry->next = next;
    } else {
        entry = (PaintLayerEntry*)pool_calloc(registry->pool, sizeof(PaintLayerEntry));
        if (!entry) return nullptr;
        entry->next = registry->entries;
        registry->entries = lam::own(entry);
    }
    entry->element = dom_node_ref(node);
    return entry;
}

// Frees idle surfaces until `bytes` fits the document budget.
static bool paint_layer_make_room(PaintLayerRegistry* registry, size_t bytes, uint64_t frame) {
    for (PaintLayerEntry* entry = registry->entries;
         entry && registry->cached_bytes + bytes > PAINT_LAYER_TOTAL_MAX_BYTES; entry = entry->next) {
        if (entry->surface && frame - entry->last_frame > PAINT_LAYER_IDLE_FRAMES)
            paint_layer_release_surface(registry, entry);
    }
    return registry->cached_bytes + bytes <= PAINT_LAYER_TOTAL_MAX_BYTES;
}

static bool paint_layer_has_presentation_transform(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* element = static_cast<DomNode*>(view)->as_element();
    return element->specified_style &&
        style_tree_get_presentation_declaration(element->specified_style, CSS_PROPERTY_TRANSFORM);
}

// A layer root is a flat element moved by a presentation transform: its own, or, inside a
// preserve-3d context, any ancestor's up to the context root (the plane moves with it). Its
// own opacity is drawn with the layer; a presentation filter would change the captured pixels.
static bool paint_layer_root(ViewBlock* block, Css3dPaintContext* context) {
    if (!transform_has_functions(block->transform) && !context) return false;
    if (radiant::transform_preserves_3d(block)) return false;
    DomElement* self = static_cast<DomNode*>(static_cast<View*>(block))->as_element();
    if (self->specified_style && style_tree_get_presentation_declaration(self->specified_style, CSS_PROPERTY_FILTER))
        return false;
    for (View* view = static_cast<View*>(block); view; view = view->parent) {
        if (paint_layer_has_presentation_transform(view)) return true;
        if (!context || view == static_cast<View*>(context->root.get())) break;
    }
    return false;
}

// Media, canvases and embedded documents change without DOM mutations, animations sample
// without them, and blending reads the real backdrop: none can be held in an isolated raster.
static bool paint_layer_subtree_static(DomElement* root) {
    DomNode* node = static_cast<DomNode*>(root);
    while (node) {
        if (node->is_element()) {
            DomElement* element = node->as_element();
            switch (element->tag()) {
                case MARKUP_NAME_VIDEO: case MARKUP_NAME_AUDIO: case MARKUP_NAME_CANVAS:
                case MARKUP_NAME_IFRAME: case MARKUP_NAME_EMBED: case MARKUP_NAME_OBJECT:
                case MARKUP_NAME_SCENE3D: case MARKUP_NAME_WEBVIEW:
                    return false;
                default: break;
            }
            StyleTree* style = element->specified_style;
            if (element->web_animation_state() || (style &&
                (style_tree_get_declaration(style, CSS_PROPERTY_ANIMATION) ||
                 style_tree_get_declaration(style, CSS_PROPERTY_ANIMATION_NAME) ||
                 style_tree_get_declaration(style, CSS_PROPERTY_TRANSITION) ||
                 style_tree_get_declaration(style, CSS_PROPERTY_TRANSITION_PROPERTY)))) return false;
            if (element->in_line && element->inl()->mix_blend_mode &&
                element->inl()->mix_blend_mode != CSS_VALUE_NORMAL) return false;
            if (element->backdrop_filter_prop() && element->backdrop_filter_prop()->functions) return false;
        }
        // depth-first, staying inside the root's subtree
        if (node->is_element() && node->as_element()->first_child) { node = node->as_element()->first_child; continue; }
        while (node && node != static_cast<DomNode*>(root) && !node->next_sibling) node = node->parent;
        if (!node || node == static_cast<DomNode*>(root)) break;
        node = node->next_sibling;
    }
    return true;
}

static PaintLayerStamp paint_layer_stamp(RasterRenderContext* rdcon, DomElement* element, Rect extent) {
    UiContext* ui = rdcon->ui_context;
    DomDocument* document = ui->document;
    ViewTree* tree = document->view_tree;
    return {element->layer_generation, document->style_content_epoch,
        doc_state_content_version(document->state), tree ? tree->document_layout_count : 0,
        font_context_glyph_cache_generation(ui->font_ctx), font_context_resource_generation(ui->font_ctx),
        image_cache_resource_generation(ui), rdcon->raster_scale, extent};
}

static bool paint_layer_stamp_equal(const PaintLayerStamp& a, const PaintLayerStamp& b) {
    return a.generation == b.generation && a.document_epoch == b.document_epoch &&
        a.content_version == b.content_version && a.layout_count == b.layout_count &&
        a.font_generation == b.font_generation && a.font_resource_generation == b.font_resource_generation &&
        a.image_resource_generation == b.image_resource_generation && a.scale == b.scale &&
        a.extent.x == b.extent.x && a.extent.y == b.extent.y &&
        a.extent.width == b.extent.width && a.extent.height == b.extent.height;
}

// Records the root's subtree into an offscreen list in the root's own plane, offset to the
// layer origin, and rasterises it into `surface`. The root's transform is cancelled by
// starting from its inverse, so every descendant composes exactly as it does on screen.
// With a null `surface` it only records and fingerprints (revalidation). `trivial` reports a
// recording too small to be worth a layer; the surface is then left unrasterised.
static bool paint_layer_capture(RasterRenderContext* rdcon, ViewBlock* block,
                                void (*content)(RasterRenderContext*, ViewBlock*),
                                RenderBlockRecordFn record, float left, float top, int width, int height,
                                ImageSurface* surface, uint64_t* fingerprint, bool* fingerprinted,
                                bool* trivial = nullptr) {
    float scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
    float elem_x = rdcon->block.x / scale + block->x;
    float elem_y = rdcon->block.y / scale + block->y;
    RdtLogicalPoint origin = radiant::transform_origin(block->transformp(), elem_x, elem_y,
        block->width, block->height);
    RdtMatrix4 local = radiant::compute_transform_matrix_3d(block->transformp(), block->width,
        block->height, origin.x, origin.y, block->transform ? block->transformp()->origin_z : 0.0f);
    RdtMatrix4 inverse;
    if (!rdt_matrix4_invert(&local, &inverse)) return false;
    RdtMatrix4 offset = rdt_matrix4_translate(-left / scale, -top / scale, 0.0f);
    RdtMatrix4 basis = rdt_matrix4_multiply(&offset, &inverse);

    OffscreenRenderArenas arenas;
    if (!arenas.init("render.paint_layer", "render.paint_layer.list_arena",
                     "render.paint_layer.scratch_arena")) return false;
    DisplayList dl = {};
    PaintList paint_list = {};
    ScratchArena scratch = {};
    dl_init(&dl, arenas.list_arena);
    paint_list_init(&paint_list, nullptr);
    mem_scratch_init(NULL, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "render.paint_layer.scratch");

    // the capture is an isolated recording: no screen clips, damage, culling or 3D context
    DisplayList* saved_dl = rdcon->dl;
    PaintList* saved_paint_list = rdcon->paint_list;
    Bound saved_clip = rdcon->block.clip;
    int saved_vector_clip_depth = rdcon->vector_clip_depth;
    int saved_clip_shape_depth = rdcon->clip_shape_depth;
    Bound saved_vector_clip_bounds[RDT_MAX_CLIP_SHAPES];
    memcpy(saved_vector_clip_bounds, rdcon->vector_clip_bounds, sizeof(saved_vector_clip_bounds));
    RdtMatrix saved_transform = rdcon->transform;
    bool saved_has_transform = rdcon->has_transform;
    RdtMatrix4 saved_transform_3d = rdcon->transform_3d;
    RdtMatrix4 saved_children_3d = rdcon->children_transform_3d;
    bool saved_has_transform_3d = rdcon->has_transform_3d;
    bool saved_overflow_clip_transform = rdcon->has_overflow_clip_transform;
    DirtyTracker* saved_dirty_tracker = rdcon->dirty_tracker;
    bool saved_has_dirty_union = rdcon->has_dirty_union;
    Css3dPaintContext* saved_css3d = rdcon->css3d_context;
    PresentationDamage* saved_damage = rdcon->presentation_damage;
    const DisplayList* saved_damage_list = rdcon->presentation_damage_list;
    bool saved_paint_failed = rdcon->paint_failed;

    rdcon->dl = lam::up(&dl);
    rdcon->paint_list = lam::up(&paint_list);
    rdcon->block.clip = {0.0f, 0.0f, (float)width, (float)height};
    rdcon->vector_clip_depth = 0;
    rdcon->clip_shape_depth = 0;
    rdcon->has_overflow_clip_transform = false;
    rdcon->dirty_tracker = nullptr;
    rdcon->has_dirty_union = false;
    rdcon->css3d_context = nullptr;
    rdcon->presentation_damage = nullptr;
    rdcon->presentation_damage_list = nullptr;
    rdcon->paint_failed = false;
    rdcon->paint_layer_capturing = true;
    rdcon->paint_layer_root = lam::up(block);
    rdcon->transform_3d = basis;
    rdcon->children_transform_3d = basis;
    rdcon->has_transform_3d = true;
    RdtMatrix projected = radiant::matrix4_project_to_2d(&basis);
    projected.e13 *= scale; projected.e23 *= scale;
    projected.e31 /= scale; projected.e32 /= scale;
    rdcon->transform = projected;
    rdcon->has_transform = true;

    record(rdcon, block, content);
    bool ok = !rdcon->paint_failed;

    rdcon->paint_layer_capturing = false;
    rdcon->paint_layer_root = nullptr;
    rdcon->paint_failed = saved_paint_failed;
    rdcon->dl = lam::up(saved_dl);
    rdcon->paint_list = lam::up(saved_paint_list);
    rdcon->block.clip = saved_clip;
    rdcon->vector_clip_depth = saved_vector_clip_depth;
    rdcon->clip_shape_depth = saved_clip_shape_depth;
    memcpy(rdcon->vector_clip_bounds, saved_vector_clip_bounds, sizeof(saved_vector_clip_bounds));
    rdcon->transform = saved_transform;
    rdcon->has_transform = saved_has_transform;
    rdcon->transform_3d = saved_transform_3d;
    rdcon->children_transform_3d = saved_children_3d;
    rdcon->has_transform_3d = saved_has_transform_3d;
    rdcon->has_overflow_clip_transform = saved_overflow_clip_transform;
    rdcon->dirty_tracker = lam::up(saved_dirty_tracker);
    rdcon->has_dirty_union = saved_has_dirty_union;
    rdcon->css3d_context = lam::up(saved_css3d);
    rdcon->presentation_damage = lam::up(saved_damage);
    rdcon->presentation_damage_list = lam::up(saved_damage_list);

    *fingerprinted = ok && dl_content_fingerprint(&dl, fingerprint);
    if (trivial) {
        int painting = 0;
        bool cheap = true;
        for (int i = 0; i < dl.item_count(); i++) {
            const DisplayItem& item = dl.data()[i];
            if (!dl_op_has_flags(item.op, DL_OP_FLAG_DIRTY_SKIPPABLE)) continue;
            painting++;
            if (item.op == DL_DRAW_IMAGE || item.op == DL_DRAW_PICTURE ||
                (item.op == DL_FILL_LINEAR_GRADIENT && item.fill_linear_gradient.stop_count > PAINT_LAYER_TRIVIAL_STOPS) ||
                (item.op == DL_FILL_RADIAL_GRADIENT && item.fill_radial_gradient.stop_count > PAINT_LAYER_TRIVIAL_STOPS))
                cheap = false;
        }
        *trivial = ok && cheap && painting <= PAINT_LAYER_TRIVIAL_ITEMS;
        if (*trivial) surface = nullptr;
    }
    if (surface) ok = ok && render_layer_rasterize(&dl, &scratch, scale, surface, "render_paint_layer_capture");
    scratch_release(&scratch);
    paint_list_destroy(&paint_list);
    dl_destroy(&dl);
    arenas.destroy();
    return ok;
}

static bool paint_layer_paint(RasterRenderContext* rdcon, ViewBlock* block,
                              void (*content)(RasterRenderContext*, ViewBlock*),
                              RenderBlockRecordFn record, Css3dPaintContext* context,
                              PaintLayerEntry** entry_out, bool* captured) {
    if (!rdcon || !block || !record || !rdcon->dl || rdcon->paint_layer_capturing ||
        paint_layer_override == PAINT_LAYER_SUSPENDED || rdcon->paint_phase != RADIANT_PAINT_ALL ||
        rdcon->element_marker_suppression_depth > 0 || !block->is_element() ||
        !rdcon->ui_context || !rdcon->ui_context->document || !rdcon->ui_context->document->state) return false;
    PaintLayerMode mode = paint_layer_mode();
    if (mode == PAINT_LAYER_MODE_OFF || !paint_layer_root(block, context)) return false;
    DomDocument* document = rdcon->ui_context->document;
    PaintLayerRegistry* registry = paint_layer_registry_for_document(document);
    if (!registry) return false;
    if (memtrack_get_pressure_level() >= MEM_PRESSURE_HIGH) {
        for (PaintLayerEntry* entry = registry->entries; entry; entry = entry->next)
            paint_layer_release_surface(registry, entry);
        return false;
    }
    DomElement* element = static_cast<DomNode*>(static_cast<View*>(block))->as_element();
    PaintLayerEntry* entry = paint_layer_entry_for_element(registry, element);
    if (!entry) return false;
    *entry_out = entry;
    entry->seen_frame = document->state->presentation_damage.frame;

    float scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
    Rect extent = css3d_block_paint_rect(rdcon, block);
    uint64_t frame = document->state->presentation_damage.frame;
    PaintLayerStamp stamp = paint_layer_stamp(rdcon, element, extent);
    if (!paint_layer_stamp_equal(*entry, stamp)) {
        bool live = !paint_layer_subtree_static(element);
        // A document-wide change (an inline style, a text or a layout anywhere) usually leaves
        // this subtree as it was: re-record it, and keep the surface when the recording has
        // the captured content, instead of rasterising it again. Glyph buffers are compared
        // by identity, so a glyph-cache change always recaptures.
        bool revalidated = false;
        if (entry->surface && entry->fingerprinted && !live && paint_layer_override == PAINT_LAYER_NORMAL &&
            rdcon->paint_layer_revalidations < PAINT_LAYER_REVALIDATIONS_PER_FRAME &&
            entry->scale == stamp.scale && entry->font_generation == stamp.font_generation &&
            entry->font_resource_generation == stamp.font_resource_generation &&
            entry->extent.x == extent.x && entry->extent.y == extent.y &&
            entry->extent.width == extent.width && entry->extent.height == extent.height) {
            rdcon->paint_layer_revalidations++;
            uint64_t fingerprint = 0;
            bool fingerprinted = false;
            revalidated = paint_layer_capture(rdcon, block, content, record, entry->left, entry->top,
                entry->surface->width, entry->surface->height, nullptr, &fingerprint, &fingerprinted) &&
                fingerprinted && fingerprint == entry->fingerprint;
            log_debug("paint-layer: revalidated element %u: %s", element->DomNode::id, revalidated ? "kept" : "changed");
        }
        if (!revalidated) paint_layer_release_surface(registry, entry);
        *static_cast<PaintLayerStamp*>(entry) = stamp;
        entry->live_content = live;
        // the first paint of new content only arms the capture
        if (!revalidated && mode != PAINT_LAYER_MODE_EAGER) return false;
    }
    if (entry->live_content) return false;

    // the root's marker, transform and plane placement as in a live paint; one image for its subtree
    RenderElementMarkerScope marker = render_element_marker_begin(rdcon, block);
    BlockBlot parent = rdcon->block;
    RenderTransformScope transform_scope = render_state_push_transform(rdcon, block, &parent);
    auto finish = [&](bool painted) {
        render_state_pop_transform(&transform_scope);
        rdcon->block = parent;
        render_element_marker_end(rdcon, &marker);
        return painted;
    };
    // nothing was painted: drop the opened marker, so the live paint records its own
    auto abandon = [&]() {
        render_state_pop_transform(&transform_scope);
        rdcon->block = parent;
        if (marker.begin_index >= 0) dl_truncate(rdcon->dl, marker.begin_index);
        return false;
    };
    float tx, ty;
    bool translation = paint_layer_translation(rdcon, &tx, &ty);
    // a translated layer at rest is captured so its pixels land on the device grid; while
    // it moves, or under a projection, any existing capture serves, resampled (B5)
    // a parity re-render of the same frame (held captures) must leave the motion state as it was
    bool held = paint_layer_override == PAINT_LAYER_HOLD_CAPTURES;
    bool resting = translation && entry->last_translation && entry->last_tx == tx && entry->last_ty == ty;
    if (!held) {
        entry->last_translation = translation;
        entry->last_tx = tx; entry->last_ty = ty;
    }
    // realign only when this frame can recapture: a released surface would leave the layer
    // waiting, live, until another capture slot
    if (!held && entry->surface && resting && rdcon->paint_layer_captures < PAINT_LAYER_CAPTURES_PER_FRAME &&
        !(paint_layer_aligned(entry->left, tx) && paint_layer_aligned(entry->top, ty)))
        paint_layer_release_surface(registry, entry);
    if (!entry->surface) {
        float left = floorf(extent.x * scale + tx) - tx, top = floorf(extent.y * scale + ty) - ty;
        int width = (int)ceilf((extent.x + extent.width) * scale - left);   // INT_CAST_OK: layer pixel size
        int height = (int)ceilf((extent.y + extent.height) * scale - top);  // INT_CAST_OK: layer pixel size
        size_t bytes = (size_t)(width > 0 ? width : 0) * (size_t)(height > 0 ? height : 0) * 4u;
        if (width <= 0 || height <= 0 || bytes > PAINT_LAYER_MAX_BYTES ||
            paint_layer_override == PAINT_LAYER_HOLD_CAPTURES ||
            rdcon->paint_layer_captures >= PAINT_LAYER_CAPTURES_PER_FRAME ||
            !paint_layer_make_room(registry, bytes, frame)) return abandon();
        ImageSurface* surface = image_surface_create(width, height);
        if (!surface) return abandon();
        rdcon->paint_layer_captures++;
        RENDER_METRIC_ADD(layer_recordings, 1);
        // the capture replaces and then restores the pushed transform state
        bool trivial = false;
        bool captured_ok = paint_layer_capture(rdcon, block, content, record, left, top, width, height,
            surface, &entry->fingerprint, &entry->fingerprinted, &trivial);
        if (trivial) {
            // stays live until its content changes, like content a layer cannot hold
            image_surface_destroy(surface);
            entry->live_content = true;
            return abandon();
        }
        if (!captured_ok) {
            image_surface_destroy(surface);
            // content the capture cannot hold stays live until it changes
            entry->live_content = true;
            return abandon();
        }
        entry->surface = surface;
        *captured = true;
        registry->idle_attempts = 0;
        entry->left = left;
        entry->top = top;
        registry->cached_bytes += bytes;
        // capture can resolve a font source or decode an image; key the layer by the result
        *static_cast<PaintLayerStamp*>(entry) = paint_layer_stamp(rdcon, element, extent);
        log_debug("paint-layer: captured %dx%d layer for element %u", width, height, element->DomNode::id);
    }
    entry->last_frame = frame;
    bool exact = translation && paint_layer_aligned(entry->left, tx) && paint_layer_aligned(entry->top, ty);
    entry->misaligned = translation && !exact;
    ImageSurface* surface = entry->surface;
    Rect dst = {entry->left, entry->top, (float)surface->width, (float)surface->height};
    // the root's own opacity groups its whole subtree, which the layer composites as one image
    float opacity = block->in_line && block->inl()->opacity >= 0.0f && block->inl()->opacity < 1.0f
        ? block->inl()->opacity : 1.0f;
    bool painted = finish(render_painter_draw_pixels_rect(rdcon, (const uint32_t*)surface->pixels,
        surface->width, surface->height, surface->pitch / 4, &dst, nullptr,
        (uint8_t)lroundf(opacity * 255.0f), surface, exact ? SCALE_MODE_NEAREST : SCALE_MODE_LINEAR_EDGE));
    if (painted) {
        rdcon->paint_layer_hits++;
        paint_layer_stats.hits++;
        if (!exact) paint_layer_stats.resampled++;
    }
    return painted;
}

// The live subtree and its layer differ by resampling and alpha rounding, so a switch between
// them (or a new capture) changes pixels where nothing moved: damage the root's projected
// extent, or a selective frame would keep the other representation's pixels.
static void paint_layer_damage(RasterRenderContext* rdcon, ViewBlock* block, Rect extent) {
    DocState* state = rdcon->ui_context->document->state;
    float scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
    BlockBlot parent = rdcon->block;
    RenderTransformScope transform_scope = render_state_push_transform(rdcon, block, &parent);
    float left = extent.x * scale, top = extent.y * scale;
    float right = (extent.x + extent.width) * scale, bottom = (extent.y + extent.height) * scale;
    bool projected = !rdcon->has_transform || rdt_matrix_project_rect_bounds(&rdcon->transform,
        left, top, right, bottom, &left, &top, &right, &bottom);
    render_state_pop_transform(&transform_scope);
    rdcon->block = parent;
    if (projected) presentation_damage_mark(state, {left / scale, top / scale, right / scale, bottom / scale});
    else state->dirty_tracker.full_repaint = true;
}

bool render_paint_layer_try(RasterRenderContext* rdcon, ViewBlock* block,
                            void (*content)(RasterRenderContext*, ViewBlock*),
                            RenderBlockRecordFn record, Css3dPaintContext* context) {
    PaintLayerEntry* entry = nullptr;
    bool captured = false;
    bool painted = paint_layer_paint(rdcon, block, content, record, context, &entry, &captured);
    if (!entry) return painted;
    if (render_paint_layer_current_override() != PAINT_LAYER_NORMAL) return painted;
    if (painted != entry->from_layer || captured) paint_layer_damage(rdcon, block, entry->extent);
    entry->from_layer = painted;
    return painted;
}

// Layers the last recording reached that still wait for a capture (armed, or resting
// misaligned). The window gives them an idle frame, so captures land between interactions
// rather than on the first frames of the next one. A capture that keeps failing (budget,
// size) stops after a few idle frames.
bool render_paint_layers_pending(DomDocument* document) {
    PaintLayerRegistry* registry = document ? (PaintLayerRegistry*)document->services.paint_layer_registry : nullptr;
    if (!registry || !document->state || paint_layer_mode() == PAINT_LAYER_MODE_OFF || registry->idle_attempts >= 3) return false;
    uint64_t frame = document->state->presentation_damage.frame;
    for (PaintLayerEntry* entry = registry->entries; entry; entry = entry->next) {
        if (entry->live_content || entry->seen_frame + 1 < frame) continue;
        if (!entry->surface || entry->misaligned) return true;
    }
    return false;
}

void render_paint_layers_idle_frame(DomDocument* document) {
    PaintLayerRegistry* registry = document ? (PaintLayerRegistry*)document->services.paint_layer_registry : nullptr;
    if (!registry || !document->state) return;
    registry->idle_attempts++;
    // a presentation frame with no damage records (and so captures) but replays only the
    // regions whose layers switch source
    document->state->presentation_damage.pending = true;
    doc_state_request_region_repaint(document->state);
}
