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
//       re-replays that DisplayList; SVG/PDF lowerings (later phases) consume
//       PaintIR directly.
//
// Migration status (Phase C step 1):
//   - The vector primitive ops below mirror the rc_* painter gateway 1:1 and
//     lower to the matching dl_* command, so raster output stays byte-for-byte
//     identical. This is the "thin layer above DisplayList" the design doc's
//     pragmatic migration note describes.
//   - The higher-level semantic ops (glyph runs, effect groups, SVG subscene)
//     are DECLARED here as the canonical contract but are lowered in later
//     phases (E: effects, F: inline SVG). They are intentionally not yet
//     emitted by the live render path.
// ==========================================================================

#include "display_list.h"   // DisplayList + all rdt_* / Color / Bound / ClipShape types

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
    PAINT_DRAW_PICTURE,
    PAINT_PUSH_CLIP,
    PAINT_POP_CLIP,

    // ── Higher-level semantic ops (contract only; lowered in later phases) ──
    // A run of glyphs sharing a font/colour — NOT rasterised bitmaps. Raster
    // lowering rasterises each glyph to a DL_DRAW_GLYPH; SVG lowering emits a
    // native <text> run. (Phase E/F.)
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
    RdtPath* path;          // borrowed; raster lowering clones into the DisplayList
    Color color;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} PaintFillPath;

typedef struct {
    RdtPath* path;          // borrowed
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
    RdtPath* path;          // borrowed
    float x1, y1, x2, y2;
    const RdtGradientStop* stops;  // borrowed
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} PaintFillLinearGradient;

typedef struct {
    RdtPath* path;          // borrowed
    float cx, cy, r;
    const RdtGradientStop* stops;  // borrowed
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
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
    RdtPicture* picture;    // borrowed
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} PaintDrawPicture;

typedef struct {
    RdtPath* clip_path;     // borrowed
    bool has_transform;
    RdtMatrix transform;
} PaintPushClip;

// ── Higher-level semantic payloads (contract; not yet lowered) ─────────────

// Effect group descriptor. Mirrors the CSS stacking effect inputs so every
// backend can decide native-vs-fallback (see design doc §6).
typedef struct {
    Bound bounds;            // visual bounds of the grouped subtree
    bool has_clip;
    bool has_transform;
    RdtMatrix transform;
    float opacity;           // 1.0 = none
    int blend_mode;          // CssEnum; 0 = normal
    void* filter;            // FilterProp*; null = none
    bool backdrop;           // backdrop-filter present
    bool isolation;          // forced isolation
} PaintEffectGroup;

// A nested SVG subscene (Phase F). Carries the inheritance + geometry that
// must survive lowering so inline SVG renders identically on every target.
typedef struct {
    void* svg_root;          // Element* SVG DOM root (inline native or picture)
    bool has_color;          // inherited currentColor present
    Color color;             // inherited currentColor
    bool has_fill;           // cascaded fill present
    Color fill;
    bool has_stroke;
    Color stroke;
    float stroke_width;
    RdtMatrix transform;     // viewBox / preserveAspectRatio composed transform
    Bound content_clip;      // clip established for the SVG box
    const char* source_path; // for resolving nested refs + recursion guard
    uint64_t resource_generation; // immutable parsed DOM generation (retain-safe)
} PaintSvgSubscene;

// A semantic glyph run (Phase E). Positions/text/font, not rasterised bitmaps.
typedef struct {
    void* font;              // FontBox* / font handle
    Color color;
    const uint32_t* glyph_ids;   // borrowed
    const float* xs;             // borrowed pen positions
    const float* ys;
    int count;
    bool has_transform;
    RdtMatrix transform;
    Bound clip;
} PaintGlyphRun;

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
        PaintDrawPicture        draw_picture;
        PaintPushClip           push_clip;
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

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void paint_list_init(PaintList* pl, Arena* backing_arena);
void paint_list_clear(PaintList* pl);   // rewind count (arena owned by caller)
void paint_list_destroy(PaintList* pl);
int  paint_list_count(const PaintList* pl);

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
                                RdtFillRule rule, const RdtMatrix* transform);
void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform);
void paint_draw_image(PaintList* pl, const uint32_t* pixels,
                      int src_w, int src_h, int src_stride,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      uint8_t opacity, const RdtMatrix* transform,
                      void* resource_owner);
void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform);
void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform);
void paint_pop_clip(PaintList* pl);

// ---------------------------------------------------------------------------
// Raster lowering: PaintIR -> DisplayList
//
// Lowers the vector primitive ops 1:1 onto the matching dl_* commands. The
// resulting DisplayList is identical to recording those dl_* calls directly,
// which keeps raster output byte-for-byte unchanged. Higher-level semantic ops
// (glyph run, effect group, SVG subscene) are not yet lowered here.
// ---------------------------------------------------------------------------

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl);

#ifdef __cplusplus
}
#endif
