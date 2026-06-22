#pragma once
// ==========================================================================
// PaintIR — Semantic paint intermediate representation.
//
// Phase C of the render-paths unification (see vibe/radiant/
// Radiant_Design_Render_Paths.md). PaintIR is the *target-neutral* paint
// layer that sits ABOVE the raster DisplayList. Today's DisplayList is a
// raster lowering (pixel-domain ops, rasterised glyph bitmaps, premultiplied
// compositing); PaintIR is the higher, shared layer that every backend
// (raster, SVG, PDF) is meant to consume so the per-element paint algorithm
// lives in exactly one place.
//
// Two-level model:
//   - PaintIR (this file): semantic, target-neutral commands.
//   - DisplayList (display_list.h): ONE lowering of PaintIR for raster.
//       paint_ir_lower_raster() turns PaintIR -> DisplayList. Tiled replay
//       re-replays that DisplayList; SVG/PDF lowerings consume PaintIR directly
//       as command families are migrated.
//
// Migration status (Phase C step 1):
//   - The vector primitive ops below mirror the rc_* painter gateway 1:1 and
//     lower to the matching dl_* command, so raster output stays byte-for-byte
//     identical. This is the "thin layer above DisplayList" the design doc's
//     pragmatic migration note describes.
//   - The higher-level semantic ops (glyph runs, effect groups, SVG subscene)
//     are the canonical contract. Lowering support is deliberately incremental:
//     SVG already handles text runs and opacity-only groups; richer effects and
//     nested SVG subscenes still expand in later phases.
// ==========================================================================

#include "display_list.h"   // DisplayList + all rdt_* / Color / Bound / ClipShape types
#include "render_backend_caps.hpp"
#include "../lib/strbuf.h"  // StrBuf for vector lowerings

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// PaintIR op codes
// ---------------------------------------------------------------------------

typedef enum {
    // ── Vector primitives (lowered 1:1 to DisplayList; byte-identical) ──────
    PAINT_FILL_RECT,
    PAINT_FILL_ROUNDED_RECT,
    PAINT_FILL_PATH,
    PAINT_STROKE_PATH,
    PAINT_FILL_LINEAR_GRADIENT,
    PAINT_FILL_RADIAL_GRADIENT,
    PAINT_DRAW_IMAGE,
    PAINT_DRAW_IMAGE_RESOURCE,
    PAINT_DRAW_GLYPH,
    PAINT_DRAW_PICTURE,
    PAINT_VIDEO_PLACEHOLDER,
    PAINT_WEBVIEW_LAYER_PLACEHOLDER,
    PAINT_PUSH_CLIP,
    PAINT_POP_CLIP,
    PAINT_PUSH_TRANSFORM,
    PAINT_POP_TRANSFORM,

    // ── Raster-lowering tier (pixel-domain ops) ────────────────────────────
    // These mirror the pixel-domain DisplayList commands 1:1. Per the design
    // doc §1 they are *not* the semantic contract — they are the raster
    // lowering of the higher-level PAINT_*_EFFECT_GROUP op below. Routing them
    // through the PaintBuilder during migration gives the raster path a single
    // emission gateway (Phase C) while the effect-group op is fleshed out; a
    // vector backend consumes the effect group, not these.
    PAINT_SAVE_BACKDROP,
    PAINT_COMPOSITE_OPACITY,
    PAINT_APPLY_BLEND_MODE,
    PAINT_APPLY_FILTER,
    PAINT_BOX_BLUR_REGION,
    PAINT_BOX_BLUR_INSET,
    PAINT_SHADOW_CLIP_SAVE,
    PAINT_SHADOW_CLIP_RESTORE,
    PAINT_OUTER_SHADOW,
    PAINT_FILL_SURFACE_RECT,
    PAINT_BLIT_SURFACE_SCALED,

    // ── Higher-level semantic ops (incrementally lowered by target) ─────────
    // A run of glyphs sharing a font/colour — NOT rasterised bitmaps. Raster
    // lowering still ignores semantic text runs; SVG lowering emits native
    // <text> when the run carries a UTF-8 text payload.
    PAINT_GLYPH_RUN,
    // CSS stacking effect (opacity, blend, filter, backdrop, clip, transform,
    // isolation). Raster lowering becomes save-backdrop / composite / blur
    // sequences; vector lowerings use native constructs or raster fallback.
    PAINT_BEGIN_EFFECT_GROUP,
    PAINT_END_EFFECT_GROUP,
    // A nested SVG subscene (inline <svg> or external .svg picture) carrying
    // the inherited paint + viewBox transform needed by every backend. (Phase F.)
    PAINT_SVG_SUBSCENE,
} PaintOp;

// ---------------------------------------------------------------------------
// Per-op payloads (vector primitives mirror the dl_* parameter shapes)
// ---------------------------------------------------------------------------

typedef struct {
    float x, y, w, h;
    Color color;
} PaintFillRect;

typedef struct {
    float x, y, w, h;
    float rx, ry;
    Color color;
} PaintFillRoundedRect;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    Color color;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} PaintFillPath;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    Color color;
    float width;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
    const float* dash_array; // borrowed
    int dash_count;
    float dash_phase;
    bool has_transform;
    RdtMatrix transform;
} PaintStrokePath;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    float x1, y1, x2, y2;
    const RdtGradientStop* stops;  // borrowed unless owns_stops is set
    bool owns_stops;        // stops must be freed by the owning PaintList cleanup
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} PaintFillLinearGradient;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    float cx, cy, r;
    const RdtGradientStop* stops;  // borrowed unless owns_stops is set
    bool owns_stops;        // stops must be freed by the owning PaintList cleanup
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} PaintFillRadialGradient;

typedef struct {
    const uint32_t* pixels; // borrowed
    int src_w, src_h, src_stride;
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
    void* resource_owner;   // optional ImageSurface* owner for generation checks
} PaintDrawImage;

typedef struct {
    ImageSurface* image;     // borrowed; lowerers decode or reference URL as needed
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} PaintDrawImageResource;

typedef struct {
    GlyphBitmap bitmap;      // descriptor copy; bitmap buffer borrowed
    int x, y;
    Color color;
    bool is_color_emoji;
    bool has_clip;
    Bound clip;
    bool has_transform;
    RdtMatrix transform;
    uint64_t resource_generation;
} PaintDrawGlyph;

typedef struct {
    RdtPicture* picture;    // borrowed
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} PaintDrawPicture;

typedef struct {
    void* video;             // borrowed video handle
    float dst_x, dst_y, dst_w, dst_h;
    int object_fit;
    bool has_clip;
    Bound clip;
    uint64_t video_generation;
} PaintVideoPlaceholder;

typedef struct {
    void* surface;           // ImageSurface* — borrowed
    float dst_x, dst_y, dst_w, dst_h;
    bool has_clip;
    Bound clip;
    uint64_t surface_generation;
} PaintWebviewLayerPlaceholder;

typedef struct {
    RdtPath* clip_path;     // borrowed
    bool has_transform;
    RdtMatrix transform;
} PaintPushClip;

typedef struct {
    RdtMatrix transform;
} PaintPushTransform;

// ── Raster-lowering tier payloads (mirror the pixel-domain Dl* structs) ────

typedef struct {
    int x0, y0, w, h;       // physical pixel region
} PaintSaveBackdrop;

typedef struct {
    int x0, y0, w, h;       // physical pixel region
    float opacity;
    bool premultiplied_source;
} PaintCompositeOpacity;

typedef struct {
    int x0, y0, w, h;       // physical pixel region
    int blend_mode;          // CssEnum
} PaintApplyBlendMode;

typedef struct {
    float x, y, w, h;
    void* filter;            // FilterProp* — borrowed
    bool has_clip;
    Bound clip;              // rectangular clip bounds at recording time
} PaintApplyFilter;

typedef struct {
    int rx, ry, rw, rh;
    float blur_radius;
    int clip_type;
    float clip_params[8];
    int exclude_type;
    float exclude_params[8];
    bool premultiply_source;
    bool tint_source;
    Color tint_color;
} PaintBoxBlurRegion;

typedef struct {
    int rx, ry, rw, rh;
    int pad;
    float blur_radius;
    uint32_t bg_color;
} PaintBoxBlurInset;

typedef struct {
    int rx, ry, rw, rh;
} PaintShadowClipSave;

typedef struct {
    int exclude_type;
    float exclude_params[8];
    int save_rx, save_ry, save_rw, save_rh;
    int restore_inside;
} PaintShadowClipRestore;

typedef struct {
    float shadow_x, shadow_y, shadow_w, shadow_h;
    float sr_tl, sr_tr, sr_br, sr_bl;
    Color color;
    float blur_radius;
    int exclude_type;
    float exclude_params[8];
    int clip_type;
    float clip_params[8];
} PaintOuterShadow;

typedef struct {
    float x, y, w, h;
    uint32_t color;          // ABGR8888
    bool has_clip;
    Bound clip;
    ClipShape** clip_shapes; // borrowed; raster lowering clones into DisplayList
    int clip_depth;
} PaintFillSurfaceRect;

typedef struct {
    void* src_surface;       // ImageSurface* — borrowed
    uint64_t src_generation;
    float dst_x, dst_y, dst_w, dst_h;
    int scale_mode;
    uint8_t opacity;
    bool has_clip;
    Bound clip;
    ClipShape** clip_shapes; // borrowed; raster lowering clones into DisplayList
    int clip_depth;
} PaintBlitSurfaceScaled;

// ── Higher-level semantic payloads (incrementally lowered by target) ────────

// Effect group descriptor. Mirrors the CSS stacking effect inputs so every
// backend can decide native-vs-fallback (see design doc §6).
typedef struct PaintEffectGroup {
    Bound bounds;            // visual bounds of the grouped subtree
    bool has_clip;
    bool has_transform;
    RdtMatrix transform;
    float opacity;           // 1.0 = none
    int blend_mode;          // CssEnum; 0 = normal
    void* filter;            // FilterProp*; null = none
    bool backdrop;           // backdrop-filter present
    void* backdrop_filter;   // FilterProp* for backdrop-filter; null = none
    bool shadow;             // box-shadow present
    bool isolation;          // forced isolation
} PaintEffectGroup;

// A nested SVG subscene (Phase F). Carries the inheritance + geometry that
// must survive lowering so inline SVG renders identically on every target.
typedef struct PaintSvgSubscene {
    void* svg_root;          // Element* SVG DOM root (inline native or picture)
    void* pool;              // Pool* used by the owning document when available
    void* font_context;      // FontContext* for SVG text resolution
    float viewport_width;
    float viewport_height;
    float pixel_ratio;
    float opacity;
    bool has_color;          // inherited currentColor present
    Color color;             // inherited currentColor
    bool has_fill;           // cascaded fill present
    Color fill;
    bool fill_none;
    bool has_stroke;
    Color stroke;
    bool stroke_none;
    float stroke_width;
    RdtMatrix transform;     // base transform; SVG lowering composes viewBox/PAR
    Bound content_clip;      // clip established for the SVG box
    const char* source_path; // for resolving nested refs + recursion guard
    uint64_t resource_generation; // immutable parsed DOM generation (retain-safe)
} PaintSvgSubscene;

typedef void (*PaintSvgSubsceneRasterLowerFn)(const PaintSvgSubscene* subscene,
                                              DisplayList* dl);
typedef bool (*PaintSvgSubsceneSvgLowerFn)(const PaintSvgSubscene* subscene,
                                           StrBuf* out, int indent_level);

void paint_ir_register_svg_subscene_lowerers(PaintSvgSubsceneRasterLowerFn raster_lower,
                                             PaintSvgSubsceneSvgLowerFn svg_lower);

// A semantic glyph run. Positions/text/font, not rasterised bitmaps.
typedef struct {
    void* font;              // FontBox* / font handle
    Color color;
    const char* text;        // optional native text payload; UTF-8, borrowed unless owns_text
    int text_len;            // bytes; 0 means empty, negative means strlen(text)
    bool owns_text;          // text must be freed by the owning PaintList cleanup
    const char* font_family; // borrowed; optional for vector text lowering
    float font_size;
    float x, baseline_y;
    float word_spacing;
    int font_weight;         // CSS numeric weight; 0 = omit
    bool italic;
    const uint32_t* glyph_ids;   // borrowed
    const float* xs;             // borrowed pen positions
    const float* ys;
    int count;
    bool has_transform;
    RdtMatrix transform;
    Bound clip;
} PaintGlyphRun;

typedef void (*PaintGlyphRunRasterLowerFn)(const PaintGlyphRun* run,
                                           DisplayList* dl);
void paint_ir_register_glyph_run_raster_lowerer(PaintGlyphRunRasterLowerFn lowerer);

// ---------------------------------------------------------------------------
// PaintCmd — tagged union of all paint commands
// ---------------------------------------------------------------------------

typedef struct PaintCmd {
    PaintOp op;
    union {
        PaintFillRect           fill_rect;
        PaintFillRoundedRect    fill_rounded_rect;
        PaintFillPath           fill_path;
        PaintStrokePath         stroke_path;
        PaintFillLinearGradient fill_linear_gradient;
        PaintFillRadialGradient fill_radial_gradient;
        PaintDrawImage          draw_image;
        PaintDrawImageResource  draw_image_resource;
        PaintDrawGlyph          draw_glyph;
        PaintDrawPicture        draw_picture;
        PaintVideoPlaceholder   video_placeholder;
        PaintWebviewLayerPlaceholder webview_layer_placeholder;
        PaintPushClip           push_clip;
        PaintPushTransform      push_transform;
        PaintSaveBackdrop       save_backdrop;
        PaintCompositeOpacity   composite_opacity;
        PaintApplyBlendMode     apply_blend_mode;
        PaintApplyFilter        apply_filter;
        PaintBoxBlurRegion      box_blur_region;
        PaintBoxBlurInset       box_blur_inset;
        PaintShadowClipSave     shadow_clip_save;
        PaintShadowClipRestore  shadow_clip_restore;
        PaintOuterShadow        outer_shadow;
        PaintFillSurfaceRect    fill_surface_rect;
        PaintBlitSurfaceScaled  blit_surface_scaled;
        PaintEffectGroup        effect_group;
        PaintSvgSubscene        svg_subscene;
        PaintGlyphRun           glyph_run;
    };
} PaintCmd;

// ---------------------------------------------------------------------------
// PaintList — growable array of PaintCmd (the recorded semantic IR)
// ---------------------------------------------------------------------------

typedef struct PaintList {
    PaintCmd* cmds;
    int count;
    int capacity;
    Arena* arena;            // backing arena for the cmds array
} PaintList;

typedef struct PaintIrValidationResult {
    bool valid;
    int first_error_index;
    const char* message;
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int effect_depth;
} PaintIrValidationResult;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void paint_list_init(PaintList* pl, Arena* backing_arena);
void paint_list_clear(PaintList* pl);   // rewind count (arena owned by caller)
void paint_list_destroy(PaintList* pl);
int  paint_list_count(const PaintList* pl);
bool paint_ir_validate(const PaintList* pl, PaintIrValidationResult* result);
bool paint_ir_validate_or_log(const PaintList* pl, const char* context);

// ---------------------------------------------------------------------------
// PaintBuilder — recording API (mirrors the rc_* painter gateway)
// ---------------------------------------------------------------------------

void paint_fill_rect(PaintList* pl, float x, float y, float w, float h, Color color);
void paint_fill_rounded_rect(PaintList* pl, float x, float y, float w, float h,
                             float rx, float ry, Color color);
void paint_fill_path(PaintList* pl, RdtPath* path, Color color,
                     RdtFillRule rule, const RdtMatrix* transform);
void paint_stroke_path(PaintList* pl, RdtPath* path, Color color, float width,
                       RdtStrokeCap cap, RdtStrokeJoin join,
                       const float* dash_array, int dash_count, float dash_phase,
                       const RdtMatrix* transform);
void paint_fill_linear_gradient(PaintList* pl, RdtPath* path,
                                float x1, float y1, float x2, float y2,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform);
void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform);
void paint_draw_image(PaintList* pl, const uint32_t* pixels,
                      int src_w, int src_h, int src_stride,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      uint8_t opacity, const RdtMatrix* transform,
                      void* resource_owner);
void paint_draw_image_resource(PaintList* pl, ImageSurface* image,
                               float dst_x, float dst_y,
                               float dst_w, float dst_h,
                               uint8_t opacity,
                               const RdtMatrix* transform);
void paint_draw_glyph(PaintList* pl, GlyphBitmap* bitmap, int x, int y,
                      Color color, bool is_color_emoji, const Bound* clip,
                      const RdtMatrix* transform, uint64_t resource_generation);
void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform);
void paint_video_placeholder(PaintList* pl, void* video,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             int object_fit, const Bound* clip,
                             uint64_t video_generation);
void paint_webview_layer_placeholder(PaintList* pl, void* surface,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     const Bound* clip,
                                     uint64_t surface_generation);
void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform);
void paint_pop_clip(PaintList* pl);
void paint_push_transform(PaintList* pl, const RdtMatrix* transform);
void paint_pop_transform(PaintList* pl);

// Raster-lowering tier (pixel-domain effect ops; see enum comment).
void paint_save_backdrop(PaintList* pl, int x0, int y0, int w, int h);
void paint_composite_opacity(PaintList* pl, int x0, int y0, int w, int h,
                             float opacity, bool premultiplied_source);
void paint_apply_blend_mode(PaintList* pl, int x0, int y0, int w, int h, int blend_mode);
void paint_apply_filter(PaintList* pl, float x, float y, float w, float h,
                        void* filter, const Bound* clip);
void paint_box_blur_region(PaintList* pl, int rx, int ry, int rw, int rh,
                           float blur_radius, int clip_type, const float* clip_params,
                           int exclude_type, const float* exclude_params,
                           bool premultiply_source, bool tint_source, Color tint_color);
void paint_box_blur_inset(PaintList* pl, int rx, int ry, int rw, int rh,
                          int pad, float blur_radius, uint32_t bg_color);
void paint_shadow_clip_save(PaintList* pl, int rx, int ry, int rw, int rh);
void paint_shadow_clip_restore(PaintList* pl, int exclude_type, const float* exclude_params,
                               int save_rx, int save_ry, int save_rw, int save_rh,
                               int restore_inside);
void paint_outer_shadow(PaintList* pl,
                        float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                        float sr_tl, float sr_tr, float sr_br, float sr_bl,
                        Color color, float blur_radius,
                        int exclude_type, const float* exclude_params,
                        int clip_type, const float* clip_params);
void paint_fill_surface_rect(PaintList* pl, float x, float y, float w, float h,
                             uint32_t color, const Bound* clip,
                             ClipShape** clip_shapes, int clip_depth);
void paint_blit_surface_scaled(PaintList* pl, void* src_surface,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               int scale_mode, const Bound* clip,
                               ClipShape** clip_shapes, int clip_depth,
                               uint8_t opacity, uint64_t src_generation);

// Higher-level semantic ops (target lowerings are intentionally incremental).
void paint_begin_effect_group(PaintList* pl, const PaintEffectGroup* group);
void paint_end_effect_group(PaintList* pl);
void paint_svg_subscene(PaintList* pl, const PaintSvgSubscene* subscene);
void paint_glyph_run(PaintList* pl, const PaintGlyphRun* glyph_run);

// ---------------------------------------------------------------------------
// Raster lowering: PaintIR -> DisplayList
//
// Lowers the vector primitive ops 1:1 onto the matching dl_* commands. The
// resulting DisplayList is identical to recording those dl_* calls directly,
// which keeps raster output byte-for-byte unchanged. Higher-level semantic ops
// that lack raster expansion here are ignored by this lowering.
// ---------------------------------------------------------------------------

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl);
void paint_ir_lower_raster_fragment(const PaintList* pl, DisplayList* dl);

// ---------------------------------------------------------------------------
// SVG lowering: PaintIR -> SVG fragment
//
// Phase D foothold. This lowering intentionally starts with the primitive
// commands whose SVG representation is exact and target-neutral. Unsupported
// commands are counted explicitly so callers can choose native support,
// raster fallback, or diagnostic handling as the capability table grows.
// ---------------------------------------------------------------------------

typedef struct {
    int indent_level;
    bool emit_unsupported_comments;
    const RenderExportTargetCaps* caps;
    int resource_id_base;
} PaintSvgLoweringOptions;

typedef struct {
    int command_count;
    int emitted_count;
    int fallback_count;
    int unsupported_count;
} PaintSvgLoweringStats;

typedef struct {
    int indent_level;
    int open_clip_depth;
    int skipped_clip_depth;
    int open_transform_depth;
    int skipped_transform_depth;
    int open_effect_depth;
    int skipped_effect_depth;
} PaintSvgLoweringState;

void paint_svg_lowering_state_init(PaintSvgLoweringState* state, int indent_level);

void paint_ir_lower_svg(const PaintList* pl, StrBuf* out,
                        const PaintSvgLoweringOptions* options,
                        PaintSvgLoweringStats* stats);

void paint_ir_lower_svg_stream(const PaintList* pl, StrBuf* out,
                               const PaintSvgLoweringOptions* options,
                               PaintSvgLoweringState* state,
                               PaintSvgLoweringStats* stats);

#ifdef __cplusplus
}
#endif
