# Radiant Multi-Threaded Rendering & Unified Animation Pipeline

**Status**: In Progress — Phase 2 Complete  
**Date**: April 2026  

---

## 1. Motivation

Radiant currently operates as a **single-threaded, immediate-mode renderer**. The entire pipeline — event handling, layout, style resolution, painting, and rasterization — runs sequentially on one thread. This limits performance on multi-core hardware and blocks the path to smooth animations.

This proposal addresses two goals:

1. **Multi-threaded CPU rendering** — parallelize rasterization to exploit multi-core CPUs (defer GPU to future).
2. **Unified animation pipeline** — support CSS animations/transitions, animated GIFs, and Lottie under one frame-scheduled, config-driven architecture.

### Scope

**In scope (this stage)**:
- Display list with retained sub-trees (flat, element-grouped)
- Tile-based parallel CPU rasterization
- Animation scheduler for CSS, GIF, and Lottie (config-driven, no JS)
- Transform/opacity compositing fast path

**Deferred to future stages**:
- GPU rendering backends (Metal/Vulkan/WebGPU)
- Parallelized layout / style resolution
- JS-driven animation (`requestAnimationFrame`, Web Animations API, `element.animate()`)
- Compositor thread

---

## 2. Current Architecture

### 2.1 Rendering Pipeline (Single-Threaded)

```
GLFW Event Loop (window.cpp, 60 FPS cap)
│
├─ glfwPollEvents()          ← input handling
├─ state change detection    ← dirty flags, reflow scheduler
├─ layout_html_doc()         ← style + layout (single pass)
└─ render_html_doc()         ← view tree traversal + rasterization
   │
   ├─ render_block_view()    ← backgrounds, borders, shadows
   ├─ render_inline_view()   ← text spans, decorations
   ├─ render_text_view()     ← glyph rendering
   ├─ render_inline_svg()    ← ThorVG scene rasterization
   └─ render_ui_overlays()   ← caret, selection, focus
```

### 2.2 Key Constraints

| Aspect | Current State |
|--------|---------------|
| Threading | All layout + render on main thread |
| Rasterizer | ThorVG software canvas, 1 thread (`tvg_engine_init(1)`) |
| Abstraction | `rdt_vector.hpp` API wraps ThorVG — never calls `tvg_*` directly |
| Memory | Arena allocators — single-threaded, no concurrent access |
| Animation | CSS properties parsed but never executed; `requestAnimationFrame` stubbed |
| GIF | First frame only via stb_image; no frame iteration |
| Lottie | ThorVG has full Lottie support, but intentionally excluded from Radiant build |
| Dirty tracking | `DirtyTracker` + `ReflowScheduler` exist for incremental repaint |
| JS timers | `setTimeout`/`setInterval` via libuv in `js_event_loop.cpp` |

---

## 3. Design: Multi-Threaded CPU Rendering

### 3.1 Architecture: Tile-Based Parallel Rasterization

Adopt the **tile-based** approach used by Chrome's raster worker pool. The page surface is divided into rectangular tiles, and each tile is rasterized independently by a worker thread.

```
Main Thread                    Worker Pool (N threads)
───────────                    ──────────────────────
Layout → Paint                 
  │                            
  ├─ Build display list        
  │  (draw commands per tile)  
  │                            
  ├─ Submit tile jobs ───────→ Worker 0: rasterize tile (0,0)
  │                            Worker 1: rasterize tile (1,0)
  │                            Worker 2: rasterize tile (0,1)
  │                            Worker 3: rasterize tile (1,1)
  │                            ...
  ├─ Wait for completion ←───  All tiles done
  │                            
  └─ Composite tiles to        
     final surface + swap      
```

### 3.2 Display List (Paint Record)

The key prerequisite for threading is **separating paint from rasterization**. Currently, `render_block_view()` calls `rdt_fill_rect()` which immediately rasterizes to the pixel buffer. We need an intermediate **display list** — a serialized sequence of draw commands.

```c
// Proposed: radiant/display_list.h

typedef enum {
    DL_FILL_RECT,
    DL_FILL_ROUNDED_RECT,
    DL_FILL_PATH,
    DL_STROKE_PATH,
    DL_FILL_LINEAR_GRADIENT,
    DL_FILL_RADIAL_GRADIENT,
    DL_DRAW_IMAGE,
    DL_DRAW_GLYPH,
    DL_DRAW_SVG_SCENE,
    DL_PUSH_CLIP,
    DL_POP_CLIP,
    DL_PUSH_OPACITY_LAYER,
    DL_POP_OPACITY_LAYER,
    DL_PUSH_BLEND_MODE,
    DL_POP_BLEND_MODE,
    DL_PUSH_FILTER,
    DL_POP_FILTER,
} DisplayOp;

typedef struct DisplayItem {
    DisplayOp op;
    float bounds[4];            // x, y, w, h — for tile culling
    union { ... };              // op-specific data
} DisplayItem;

typedef struct DisplayList {
    DisplayItem* items;
    int count;
    int capacity;
    Arena* arena;               // all item data lifetime-bound to the list
} DisplayList;
```

**Decision: Flat list with element groups.** A single flat array is cache-friendly and simple to cull per-tile. Each element's draw commands are bracketed by `DL_BEGIN_ELEMENT` / `DL_END_ELEMENT` markers that record the element pointer, bounding box, and current transform/opacity. This enables retained sub-tree replay (see §3.11).

### 3.3 Recording Phase (Main Thread)

Replace direct `rdt_*` calls with display list recording:

```c
// Before (immediate mode):
rdt_fill_rect(&rdcon->vec, x, y, w, h, bg_color);

// After (display list recording):
dl_fill_rect(rdcon->display_list, x, y, w, h, bg_color);
```

The `render_*` functions remain structurally identical — they just record instead of draw. This is the highest-effort change but is purely mechanical: every `rdt_*` call in `render.cpp`, `render_background.cpp`, `render_border.cpp`, `render_filter.cpp`, `render_img.cpp`, `render_svg_inline.cpp`, `render_texnode.cpp`, `render_form.cpp`, and `render_walk.cpp` becomes a `dl_*` call.

### 3.4 Tile Partitioning

```c
// Proposed: radiant/tile_pool.h

#define TILE_SIZE 256  // 256x256 CSS pixels (512x512 at 2x)

typedef struct Tile {
    int col, row;               // tile grid position
    float x, y, w, h;          // bounds in CSS pixels
    uint32_t* pixels;           // tile pixel buffer (owned)
    int pixel_w, pixel_h;      // physical pixel dimensions
} Tile;

typedef struct TileGrid {
    Tile* tiles;
    int cols, rows;
    int total;
    float scale;                // pixel_ratio
} TileGrid;
```

Tile size tradeoffs:
- **256px**: Good parallelism, low overhead. Chrome uses 256x256.
- Smaller tiles (128px) = more parallelism but higher merge overhead.
- Larger tiles (512px) = less parallelism but simpler.

### 3.5 Worker Thread Pool

```c
// Proposed: radiant/render_pool.h

typedef struct RenderPool {
    pthread_t* threads;
    int thread_count;
    
    // Job queue (lock-free or mutex-guarded ring buffer)
    TileJob* jobs;
    _Atomic int job_head;
    _Atomic int job_tail;
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t  work_available;
    pthread_cond_t  all_done;
    _Atomic int     active_jobs;
    bool            shutdown;
} RenderPool;

typedef struct TileJob {
    Tile* tile;
    DisplayList* display_list;  // shared, read-only
    float scale;
} TileJob;
```

Each worker thread:
1. Dequeues a `TileJob`
2. Creates a **thread-local** ThorVG canvas bound to the tile's pixel buffer
3. Iterates the display list, **culling** items whose bounds don't intersect the tile
4. Replays matching draw commands via `rdt_*` calls to the local canvas
5. Signals completion

### 3.6 Per-Thread ThorVG Canvas

ThorVG's software canvas is **not thread-safe** for concurrent writes to the same canvas. However, **separate canvases with separate pixel buffers are fully independent**. Each worker creates its own canvas:

```c
void tile_worker_fn(TileJob* job) {
    // Thread-local ThorVG canvas (created once, reused across frames)
    static thread_local Tvg_Canvas canvas = NULL;
    if (!canvas) {
        canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    }
    
    Tile* tile = job->tile;
    tvg_swcanvas_set_target(canvas, tile->pixels, tile->pixel_w,
                            tile->pixel_w, tile->pixel_h,
                            TVG_COLORSPACE_ABGR8888);
    
    // Replay display list items that intersect this tile
    DisplayList* dl = job->display_list;
    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];
        if (!bounds_intersect(item->bounds, tile->x, tile->y, tile->w, tile->h)) {
            continue;  // cull: item doesn't touch this tile
        }
        // Translate coordinates: page-absolute → tile-local
        replay_item(canvas, item, -tile->x, -tile->y, job->scale);
    }
}
```

### 3.7 Compositing (Main Thread)

After all tiles complete, the main thread copies tile buffers into the final surface:

```c
void composite_tiles(uint32_t* surface, int surface_stride,
                     TileGrid* grid) {
    for (int i = 0; i < grid->total; i++) {
        Tile* tile = &grid->tiles[i];
        blit_tile(surface, surface_stride, tile);
    }
}
```

This is a simple memcpy per tile row — very fast.

### 3.8 Thread-Safety Considerations

| Resource | Strategy |
|----------|----------|
| Display list | Built on main thread, then **read-only** shared with workers |
| Tile pixel buffers | Each tile owns its buffer; no concurrent writes |
| ThorVG canvas | Thread-local; one canvas per worker |
| Arena allocators | Display list arena is main-thread only; workers don't allocate from it |
| Font glyph cache | Need thread-safe read access; pre-rasterize glyphs before dispatch or use read-write lock |
| Image pixels | Read-only shared (images already decoded before render) |
| Clip stack | Currently `thread_local` in `rdt_vector_tvg.cpp` — already safe |

### 3.9 Font Glyph Cache Thread Safety

The font glyph cache (`font.cpp`) is the main shared mutable state during rendering. Options:

**Option A (Recommended): Pre-rasterize before dispatch**  
During display list recording on the main thread, ensure all referenced glyphs are already cached. The glyph cache becomes read-only during tile rasterization.

**Option B: Read-write lock on glyph cache**  
Workers take read locks; cache miss triggers write lock + rasterize. Adds contention.

Option A is cleaner — the recording pass already touches every glyph, so the cache is warm.

### 3.10 Incremental / Dirty Tile Tracking

Extend `DirtyTracker` to track dirty tiles:

```c
typedef struct DirtyTileTracker {
    bool* dirty;       // one flag per tile
    int tile_count;
} DirtyTileTracker;
```

On incremental updates (e.g., caret blink, hover state change), only dirty tiles are re-rasterized. This combines well with the existing dirty rectangle tracking.

### 3.11 Retained Sub-Trees for Transform/Opacity Fast Path

CSS `transform` and `opacity` animations should **not** trigger re-layout or full re-record. The display list supports this via element groups:

```c
// Element group markers in the flat display list
typedef struct DlElementGroup {
    View* element;              // owning view node
    int begin_index;            // first DisplayItem index (inclusive)
    int end_index;              // last DisplayItem index (exclusive)
    float bounds[4];            // union of all child item bounds
    RdtMatrix transform;        // element's current CSS transform
    float opacity;              // element's current opacity [0,1]
    bool compositable;          // true if only transform/opacity differ from last frame
} DlElementGroup;

typedef struct DisplayList {
    DisplayItem* items;
    int count;
    int capacity;

    DlElementGroup* groups;     // parallel array of element groups
    int group_count;
    int group_capacity;

    Arena* arena;
} DisplayList;
```

**Recording**: When `render_block_view()` begins, it pushes a `DL_BEGIN_ELEMENT` and records the group start index. When it ends, it records the group end index and bounding box.

**Animation fast path**: When an animation tick changes only `transform` or `opacity` on an element:

1. Look up the element's `DlElementGroup`
2. Update `group->transform` or `group->opacity` in-place (no re-record)
3. Mark the old bounding tiles + new bounding tiles dirty
4. During tile replay, apply the updated transform/opacity when replaying items in `[begin_index, end_index)`

```c
void dl_update_element_transform(DisplayList* dl, View* element, RdtMatrix* new_transform) {
    DlElementGroup* group = dl_find_group(dl, element);
    if (!group) return;
    
    // Mark old and new tile regions dirty
    dirty_tracker_mark_bounds(tracker, group->bounds);   // old position
    group->transform = *new_transform;
    dl_recompute_group_bounds(group);                    // update bounds
    dirty_tracker_mark_bounds(tracker, group->bounds);   // new position
    group->compositable = true;
}

void dl_update_element_opacity(DisplayList* dl, View* element, float new_opacity) {
    DlElementGroup* group = dl_find_group(dl, element);
    if (!group) return;
    
    group->opacity = new_opacity;
    dirty_tracker_mark_bounds(tracker, group->bounds);
    group->compositable = true;
}
```

**Compositing**: During tile replay, compositable groups are rasterized to a **temporary tile-local buffer** with the updated transform, then alpha-blended onto the tile at the group's opacity. This avoids re-recording the entire display list for a single animated element.

**Fallback**: If a property change requires re-layout (e.g., `width`, `margin`), the group is invalidated and re-recorded from scratch on the next frame.

---

## 4. Design: Unified Animation Pipeline

### 4.1 Architecture Overview

```
                    ┌─────────────────────────────────┐
                    │       AnimationScheduler         │
                    │  (frame-driven, vsync-aligned)   │
                    └────────┬────────────────────────┘
                             │ tick(timestamp)
              ┌──────────────┼──────────────┐
              │              │              │
     ┌────────▼───────┐ ┌───▼──────┐ ┌────▼────────┐
     │ CssAnimTimeline│ │GifPlayer │ │LottiePlayer │
     │ @keyframes     │ │ frame    │ │ frame       │
     │ transitions    │ │ sequence │ │ interpolator│
     └────────┬───────┘ └───┬──────┘ └────┬────────┘
              │              │              │
              ▼              ▼              ▼
         style mutation   image swap   scene rebuild
              │              │              │
              └──────────────┼──────────────┘
                             │
                    ┌────────▼────────┐
                    │  DirtyTracker   │
                    │  mark affected  │
                    │  tiles dirty    │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │ re-record + re- │
                    │ rasterize dirty │
                    │ tiles only      │
                    └─────────────────┘
```

### 4.2 AnimationScheduler

The central coordinator, integrated into the GLFW event loop:

```c
// Proposed: radiant/animation.h

typedef struct AnimationScheduler {
    // Active animation instances (intrusive linked list for O(1) add/remove)
    AnimationInstance* first;
    AnimationInstance* last;
    int count;
    
    // Timing
    double current_time;        // monotonic time in seconds
    bool has_active_animations; // controls frame scheduling
    
    // Pool for allocation
    Pool* pool;
} AnimationScheduler;

typedef struct AnimationInstance {
    AnimationInstance* next;
    AnimationInstance* prev;
    
    AnimationType type;         // CSS_ANIMATION, CSS_TRANSITION, GIF, LOTTIE
    void* target;               // ViewBlock*, EmbedProp* (image), etc.
    void* state;                // type-specific state
    
    double start_time;
    double duration;
    double delay;
    int iteration_count;        // -1 = infinite
    AnimationDirection direction;
    AnimationFillMode fill_mode;
    AnimationPlayState play_state;
    
    // Callback to apply the current frame
    void (*tick)(AnimationInstance* anim, double t);  // t = normalized progress [0,1]
    void (*on_finish)(AnimationInstance* anim);
} AnimationInstance;
```

Integration with the event loop:

```c
// In window.cpp main loop:
while (!glfwWindowShouldClose(window)) {
    double now = glfwGetTime();
    glfwPollEvents();
    
    // Tick all active animations
    if (scheduler->has_active_animations) {
        animation_scheduler_tick(scheduler, now);  // updates properties, marks dirty
        do_redraw = true;
    }
    
    if (do_redraw) {
        // Incremental: re-layout if needed, re-record dirty display list, re-rasterize dirty tiles
        window_refresh_callback(window);
    }
    
    // If animations are active, don't wait for events — run at vsync rate
    if (scheduler->has_active_animations) {
        glfwSwapInterval(1);  // vsync
    } else {
        glfwWaitEventsTimeout(1.0 / 60.0 - delta);  // idle: event-driven
    }
}
```

### 4.3 CSS Animations

CSS animation properties are **already parsed** (`resolve_css_style.cpp`). We need:

1. **Keyframe storage**: Store parsed `@keyframes` rules indexed by name.
2. **Property interpolation engine**: Interpolate CSS values between keyframes.
3. **Animation instance creation**: When an element's computed `animation-name` is non-`none`, create an `AnimationInstance`.

```c
// Proposed: radiant/css_animation.h

typedef struct CssKeyframeRule {
    float offset;               // 0.0 = from, 1.0 = to, or percentage
    CssPropertyValue* properties;
    int property_count;
} CssKeyframeRule;

typedef struct CssKeyframes {
    const char* name;
    CssKeyframeRule* rules;
    int rule_count;
} CssKeyframes;

typedef struct CssAnimState {
    CssKeyframes* keyframes;
    CssTimingFunction timing;   // ease, linear, cubic-bezier, steps
    float current_iteration;
    bool forwards;              // current direction (for alternate)
} CssAnimState;
```

**Interpolation** for CSS property types:

| CSS Type | Interpolation |
|----------|---------------|
| `<length>` | Linear: `a + (b - a) * t` |
| `<color>` | Per-channel linear in sRGB (or oklab for perceptual) |
| `<percentage>` | Linear |
| `transform` | Decompose → interpolate translate/rotate/scale/skew individually |
| `opacity` | Linear [0, 1] |
| `visibility` | Discrete: flip at t=0.5 |
| `box-shadow` | Interpolate each component (offsets, blur, spread, color) |
| Discrete properties (`display`, `font-family`) | No interpolation; switch at endpoint |

**Timing functions** — reuse ThorVG's cubic-bezier solver (already in the codebase at `tvgLottieInterpolator.cpp`) or port a standalone version:

```c
// Easing: same algorithm as CSS cubic-bezier()
// Already proven in tvgLottieInterpolator.cpp — extract and reuse
float cubic_bezier_ease(float t, float x1, float y1, float x2, float y2);

// Built-in keywords
// ease:        cubic_bezier(0.25, 0.1, 0.25, 1.0)
// ease-in:     cubic_bezier(0.42, 0.0, 1.0, 1.0)
// ease-out:    cubic_bezier(0.0, 0.0, 0.58, 1.0)
// ease-in-out: cubic_bezier(0.42, 0.0, 0.58, 1.0)
// linear:      identity (no transform)
// steps(n, jump-start|jump-end|...): discrete stepping
```

**CSS Transitions**: Create an `AnimationInstance` with `type = CSS_TRANSITION` whenever a computed style property changes on an element that has `transition-property` covering that property. The transition interpolates from old value to new value.

### 4.4 Animated GIF

Add a GIF frame decoder and integrate with the animation scheduler.

**Decoder**: Use `stb_image`'s GIF support (already bundled) which provides `stbi_load_gif_from_memory()` to extract all frames and delays.

```c
// Proposed: radiant/gif_player.h

typedef struct GifFrame {
    uint32_t* pixels;           // ABGR8888
    int delay_ms;               // frame display duration (from GIF metadata)
} GifFrame;

typedef struct GifAnimation {
    GifFrame* frames;
    int frame_count;
    int width, height;
    int current_frame;
    double frame_end_time;      // when to advance to next frame
    int loop_count;             // 0 = infinite (GIF spec default)
    int loops_completed;
} GifAnimation;
```

**Integration**: When `<img>` loads a GIF with multiple frames:
1. Decode all frames at load time into `GifAnimation`
2. Register an `AnimationInstance` with the scheduler
3. On each tick: check if `current_time >= frame_end_time`, advance frame if so
4. Swap the `ImageSurface` pixel pointer to the new frame's buffer
5. Mark the image's tile(s) dirty

### 4.5 Lottie Animation

ThorVG already includes a complete Lottie engine. Enable it in the Radiant build and wrap it:

```c
// Proposed: radiant/lottie_player.h

typedef struct LottiePlayer {
    // ThorVG animation handle
    Tvg_Animation animation;
    Tvg_Paint picture;
    
    float total_frames;
    float frame_rate;
    float duration;             // seconds
    
    // Playback state
    float current_frame;
    bool loop;
    bool playing;
    
    // Rendering target
    uint32_t* pixels;
    int width, height;
} LottiePlayer;
```

**Integration** (config-driven, no JS required):
- `<img src="animation.json">` — auto-detected by extension/MIME
- `<lottie-player>` custom element (used by LottieFiles ecosystem)
- CSS `background-image: url(animation.json)` — future extension

On each animation tick:
1. Compute `progress = elapsed / duration`
2. Call `tvg_animation_set_frame(anim, totalFrames * progress)`
3. Update and re-rasterize the ThorVG canvas to the element's pixel buffer
4. Mark dirty

### 4.6 JS-Driven Animation (Deferred)

`requestAnimationFrame`, `cancelAnimationFrame`, and the Web Animations API (`element.animate()`) are **deferred to a future JS integration stage**. These require:
- Keeping the JS execution context alive across frames (currently run-once)
- GC root management for callback references
- Microtask queue draining per frame

The animation scheduler is designed to accommodate these later — they will register as additional `AnimationInstance` types (`JS_RAF`, `JS_WEB_ANIM`) when JS integration lands.

---

## 5. Implementation Plan

### Phase 1: Display List Foundation ✅ Complete

**Goal**: Decouple paint from rasterization without changing visible behavior.

1. ✅ Define `DisplayList` and `DisplayItem` structures
2. ✅ Create `dl_*` recording functions mirroring every `rdt_*` call
3. ✅ Create `dl_replay()` that replays a display list through `rdt_*` calls
4. ✅ Wire `render_html_doc()` to: record → replay → present
5. ✅ **Validation**: Pixel-identical output — 5237/5237 radiant baseline tests pass

**Implementation details** (April 16, 2026):

| File | Role |
|------|------|
| `radiant/display_list.h` | Header: 22 opcodes, all payload structs, `DisplayList` struct, full API |
| `radiant/display_list.cpp` | Recording functions + `dl_replay()` with backdrop stack |
| `radiant/render.hpp` | 15 `rc_*` static inline wrappers dispatching to dl or rdt based on `rdcon->dl` |
| `radiant/rdt_vector.hpp` / `rdt_vector_tvg.cpp` | Added `rdt_path_clone()` for display list path ownership |
| `radiant/render_svg_inline.hpp` / `.cpp` | 7 `svg_*` wrappers + `DisplayList* dl` in `SvgRenderContext` |

Draw call sites converted (all `rdt_*` → `rc_*` / `svg_*` wrappers):
- `render.cpp` (~35 sites + `draw_glyph`, `apply_css_filters`, opacity, blend mode)
- `render_background.cpp`, `render_border.cpp`, `render_form.cpp`, `render_svg_inline.cpp`

Display list opcodes: `DL_FILL_RECT`, `DL_FILL_ROUNDED_RECT`, `DL_FILL_PATH`, `DL_STROKE_PATH`, `DL_FILL_LINEAR_GRADIENT`, `DL_FILL_RADIAL_GRADIENT`, `DL_DRAW_IMAGE`, `DL_DRAW_GLYPH`, `DL_DRAW_PICTURE`, `DL_PUSH_CLIP`, `DL_POP_CLIP`, `DL_SAVE_CLIP_DEPTH`, `DL_RESTORE_CLIP_DEPTH`, `DL_FILL_SURFACE_RECT`, `DL_BLIT_SURFACE_SCALED`, `DL_APPLY_OPACITY`, `DL_SAVE_BACKDROP`, `DL_APPLY_BLEND_MODE`, `DL_APPLY_FILTER`, `DL_BEGIN_ELEMENT`, `DL_END_ELEMENT`

Key design choices:
- Flat growable array (start 2048, double on overflow) — cache-friendly
- `ScratchArena` for variable-length data (gradient stops, dash arrays)
- `RdtPath*` cloned into display list (owned by DL, freed on destroy)
- Backdrop stack during replay (depth 16) for mix-blend-mode save/restore
- Canvas clear happens *before* display list recording (not recorded)
- Tiled export path (`render_html_doc_tiled`) unaffected — `dl` stays NULL

### Phase 2: Tile-Based Rasterization ✅ Complete

**Goal**: Parallel rasterization on worker threads.

1. ✅ Implement `TileGrid` partitioning
2. ✅ Implement `RenderPool` (pthread-based worker pool)
3. ✅ Per-worker thread-local ThorVG canvas
4. ✅ Display list replay with tile-bounds culling
5. ✅ Tile compositing to final surface
6. ✅ **Validation**: 5237/5237 radiant baseline tests pass (8 threads, fully parallel)

**Implementation details** (April 16, 2026):

| File | Role |
|------|------|
| `radiant/tile_pool.h` | Header: `TileGrid`, `Tile`, `RenderPool`, `TileJob`, `WorkerState` structs |
| `radiant/tile_pool.cpp` | Full implementation: tile grid init/destroy/clear/composite, pthread worker pool, `dl_replay_tile()` with bounds culling + coordinate translation |
| `radiant/render.cpp` | Wired tiled replay into `render_html_doc()`: grid init → pool dispatch → composite → present; `render_pool_shutdown()` for cleanup |
| `radiant/render.hpp` | Added `render_pool_shutdown()` declaration |
| `radiant/rdt_vector_tvg.cpp` | `tile_offset_x`/`tile_offset_y` for ThorVG scene wrapper offset; `rdt_picture_draw_dup()` with mutex for thread-safe `tvg_paint_duplicate`; `rdt_vector_set_target()` for tile rebinding |
| `radiant/rdt_vector.hpp` | Added `rdt_vector_set_tile_offset_x/y`, `rdt_vector_set_target`, `rdt_picture_draw_dup` |
| `radiant/ui_context.cpp` | Calls `render_pool_shutdown()` before `rdt_engine_term()` for proper cleanup ordering |

Key design choices:
- 256 CSS px tiles (512 physical at 2x) — matches Chrome's tile size
- `pthread` worker pool with `mutex` + `condvar` synchronization (no atomics needed)
- `thread_local WorkerState` per worker: own ThorVG `SwCanvas`, `Pool`, `Arena`, `ScratchArena`
- Two coordinate translation strategies:
  - **ThorVG vector ops** (fill_rect, paths, gradients, images): scene wrapper in `tvg_push_draw_remove` translates by `(-tile_offset_x, -tile_offset_y)` automatically
  - **Direct-pixel ops** (glyphs, fill_surface_rect, blit_surface_scaled, opacity, blend, filter): manual tile-local coordinate translation with bounds clamped to `[0, tile_w/h]`
- `rdt_picture_draw_dup()`: duplicates ThorVG paint via `tvg_paint_duplicate()` so the original stays intact for other tiles; protected by `g_tvg_dup_mutex` (ThorVG's `Picture::duplicate()` mutates shared non-atomic state)
- `RADIANT_RENDER_THREADS` env var controls thread count (default: hardware threads; `1` = single-threaded `dl_replay()` fallback)
- Per-tile clip depth save stack (avoids writing to shared display list)
- Per-tile backdrop stack for `mix-blend-mode` save/restore
- `render_pool_shutdown()` joins worker threads and destroys their ThorVG canvases before `tvg_engine_term()`

Bugs found and fixed during implementation:
- **Unclamped tile-local clip bounds** (root cause of SIGBUS/SIGSEGV): direct-pixel ops passed translated clip bounds without clamping to tile dimensions, causing out-of-bounds pixel writes in `fill_surface_rect`, `blit_surface_scaled`, `apply_css_filters`
- **`tvg_paint_duplicate` race condition**: ThorVG's `Picture::Impl::duplicate()` increments a non-atomic `sharing` counter and calls `load()` which mutates the source loader — serialized with a dedicated mutex

### Phase 3: Animation Scheduler

**Goal**: Frame-driven animation loop integrated with event loop (config-driven, no JS).

1. Implement `AnimationScheduler` — tick dispatch, lifetime management
2. Implement timing functions (cubic-bezier, steps) — port from ThorVG's interpolator
3. Integrate with GLFW event loop (vsync when active, idle otherwise)
4. Animation auto-detection: CSS `animation-*` properties trigger CSS animations; multi-frame GIFs trigger GifPlayer; `.json` Lottie files trigger LottiePlayer
5. **Validation**: Animations start/stop/loop based on CSS and content properties

### Phase 4: CSS Animations & Transitions

**Goal**: Animate CSS properties via @keyframes and transitions.

1. Store parsed `@keyframes` in stylesheet (currently parsed, discarded)
2. Property interpolation engine for numeric, color, transform types
3. CSS Animation instances: create on style resolution, manage in scheduler
4. CSS Transition instances: detect computed value changes on transitionable properties
5. **Validation**: CSS animation test suite (transform, opacity, color, position)

### Phase 5: GIF Animation

**Goal**: Animated GIF playback.

1. Multi-frame GIF decoder via `stbi_load_gif_from_memory()`
2. `GifAnimation` lifecycle (decode on load, register with scheduler)
3. Frame advancement + dirty marking on tick
4. **Validation**: Animated GIF renders with correct timing

### Phase 6: Lottie Animation

**Goal**: Lottie playback in `<img>` and custom elements.

1. Enable Lottie loader in ThorVG build configuration
2. `LottiePlayer` wrapper + scheduler integration
3. Auto-detect `.json` / `application/json` Lottie files in `<img>`
4. Re-rasterize Lottie frame on each tick into element's surface
5. **Validation**: Lottie animations play smoothly at target framerate

---

## 6. Performance Projections

### Tile-Based Rendering Speedup

Current bottleneck: ThorVG software rasterization is single-threaded. A page with complex borders, gradients, shadows, and SVG renders sequentially.

Expected speedup with tile-parallel rasterization:

| Cores | Theoretical | Realistic (overhead) |
|-------|-------------|---------------------|
| 2     | 2.0x        | 1.6–1.8x           |
| 4     | 4.0x        | 2.5–3.2x           |
| 8     | 8.0x        | 3.5–5.0x           |

Sub-linear scaling due to: tile boundary overhead, display list traversal per tile, load imbalance (some tiles are empty background, some are text-heavy), compositing cost, and memory bandwidth.

### Animation Frame Budget

At 60 FPS, the frame budget is **16.6ms**. Budget allocation:

| Stage | Budget | Notes |
|-------|--------|-------|
| Animation tick | <0.5ms | Property interpolation, dirty marking |
| Incremental layout | 0–4ms | Only if animation triggers reflow (e.g., width change) |
| Display list record | 1–3ms | Only dirty subtrees |
| Tile rasterization | 4–8ms | Parallel; only dirty tiles |
| Compositing | 0.5–1ms | Blit tiles to surface |
| Buffer swap | 0–1ms | Platform-dependent |
| **Total** | **6–17ms** | Fits in 60 FPS budget for most pages |

For animations that only affect `transform` and `opacity` (the fast path), layout can be skipped entirely — only re-record and re-rasterize the affected tiles.

---

## 7. Open Questions & Suggestions

### Q1: Memory Budget for Tile Buffers

Each tile at 256x256 @ 2x = 512x512 pixels × 4 bytes = **1 MB**. A 1920x1080 @ 2x page needs 8×5 = 40 tiles = **40 MB** of tile buffers. Plus the final surface (32 MB for 3840x2160).

*Recommendation*: Pre-allocate a tile buffer pool sized for the viewport + margin. Reuse buffers across frames. For large scrollable pages, only tile the visible viewport + scroll-ahead margin (Chrome tiles ±1 screen in scroll direction).

### Q2: GIF Memory Usage

A 100-frame GIF at 500×500 = 100 MB decoded. Options:
- **Decode all upfront** (current `stbi` approach) — simple, high memory
- **Decode on demand** — keep compressed data, decode current + next frame
- **Ring buffer** — keep N decoded frames, evict oldest

*Recommendation*: Decode-on-demand with a 2-frame ring buffer (current + next). Keeps memory bounded while avoiding decode latency on frame advance.

### Q3: Lottie Build Size Impact

ThorVG's Lottie loader + JerryScript expressions add ~150KB+ to the binary. This is acceptable for the desktop build but may matter for embedded/WASM targets.

*Recommendation*: Make Lottie support a build-time option via `build_lambda_config.json` (enabled by default, can be disabled for minimal builds).

---

## 8. Summary

| Component | Approach | Key Decision |
|-----------|----------|--------------|
| Rendering parallelism | Tile-based worker pool | N worker threads, each with own ThorVG canvas |
| Paint/raster decoupling | Display list | Flat command buffer, recorded on main thread |
| Animation scheduling | Central `AnimationScheduler` | Frame-driven tick, integrated with GLFW loop |
| CSS animation | Keyframe interpolation engine | Reuse ThorVG's cubic-bezier solver |
| CSS transitions | Computed-value-change detection | Auto-create transition instances |
| GIF animation | Multi-frame decoder + frame swap | Decode-on-demand, 2-frame ring buffer |
| Lottie animation | ThorVG `Animation` API wrapper | Enable in build, auto-detect format |
| Transform/opacity fast path | Retained display sub-trees | Element groups in flat list; in-place transform/opacity update |
| JS animation | **Deferred** | rAF, Web Animations API deferred to JS integration stage |
| Parallelized layout | **Deferred** | Layout is inherently sequential; keep single-threaded |
| Compositor thread | **Deferred** | Premature without GPU compositing |
| GPU rendering | **Deferred** | Future phase: Metal/Vulkan/WebGPU backends |
