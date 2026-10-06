#include "render.hpp"
#include "../lib/base64.h"
#include "layout.hpp"
#include "layout_paged.hpp"
#include "view_tree_css.hpp"
#include "render_glyph_run_raster_lower.hpp"
#include "event.hpp"

#include "../lib/tagged.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lib/mem_factory.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/file.h"
#include "../lib/str.h"
#include "../lib/time_util.h"
#include <pthread.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static RenderPool* g_render_pool = nullptr;
static pthread_once_t g_render_pool_once = PTHREAD_ONCE_INIT;
static int g_render_pool_threads = 0;

typedef struct RenderOutputClearResult {
    bool selective;
    DirtyTracker* replay_dirty;
} RenderOutputClearResult;

typedef struct RenderOutputReplayResult {
    bool tiled;
    int tile_count;
    int thread_count;
} RenderOutputReplayResult;

static int render_output_render_html_file_to_target(const char* html_file,
                                                    RenderOutputTarget* target);
static int render_output_render_document_transform_to_target(const char* document_file,
    const LambdaDocumentTransformConfig* transform,
    const LambdaDocumentTransformOption* options, int option_count,
    RenderOutputTarget* target);

static bool render_export_find_page_rule(void* context, const CssRule* rule) {
    if (rule->type == CSS_RULE_PAGE && rule->page) {
        *(bool*)context = true;
        return false;
    }
    return true;
}

static bool render_export_has_page_rule(DomDocument* doc) {
    if (!doc || !doc->services.cached_css_engine) return false;
    CssEngine* engine = (CssEngine*)doc->services.cached_css_engine;
    bool found = false;
    for (int i = 0; i < doc->stylesheet_count && !found; i++) {
        css_stylesheet_visit_active_rules(engine, doc->stylesheets.get()[i],
            render_export_find_page_rule, &found);
    }
    return found;
}
static void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree,
                                          const char* output_file);
static void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                           const char* output_file,
                                           int total_width, int total_height);

typedef DomDocument* (*RenderExportDocumentLoader)(RenderExportSession* session,
    int layout_width, int layout_height, void* request);

typedef struct RenderExportHtmlRequest {
    const char* html_file;
} RenderExportHtmlRequest;

typedef struct RenderExportTransformRequest {
    const char* document_file;
    const LambdaDocumentTransformConfig* transform;
    const LambdaDocumentTransformOption* options;
    int option_count;
} RenderExportTransformRequest;

static DomDocument* render_export_load_html_document(RenderExportSession* session,
        int layout_width, int layout_height, void* request) {
    RenderExportHtmlRequest* html_request = (RenderExportHtmlRequest*)request;
    return html_request && html_request->html_file
        ? load_html_doc(session->base_url, (char*)html_request->html_file,
            layout_width, layout_height, nullptr, nullptr, false,
            session->print_media)
        : nullptr;
}

static DomDocument* render_export_load_transform_document(RenderExportSession* session,
        int layout_width, int layout_height, void* request) {
    RenderExportTransformRequest* transform_request = (RenderExportTransformRequest*)request;
    if (!transform_request || !transform_request->document_file || !transform_request->transform) {
        return nullptr;
    }
    Pool* pool = mem_pool_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_LAYOUT, "render.document_transform");
    if (!pool) return nullptr;
    Url* document_url = url_parse_with_base(transform_request->document_file, session->base_url);
    if (!document_url) {
        pool_destroy(pool);
        return nullptr;
    }
    char text_width_px[32];
    LambdaDocumentTransformOption tikz_option = {};
    const LambdaDocumentTransformOption* transform_options = transform_request->options;
    int transform_option_count = transform_request->option_count;
    if (strcmp(transform_request->transform->input_type, "tikz") == 0) {
        snprintf(text_width_px, sizeof(text_width_px), "%d", layout_width);
        tikz_option = {"text_width_px", LAMBDA_DOCUMENT_TRANSFORM_OPTION_STRING,
            text_width_px, false};
        transform_options = &tikz_option;
        transform_option_count = 1;
    }
    DomDocument* doc = load_lambda_document_transform_doc(document_url,
        transform_request->transform, transform_options, transform_option_count,
        layout_width, layout_height, pool, session->print_media);
    if (!doc) {
        url_destroy(document_url);
        pool_destroy(pool);
        return nullptr;
    }
    if (!dom_document_finalize_loader_pool(doc, pool)) {
        log_error("render document transform: could not transfer loader pool");
        free_document(doc);
        return nullptr;
    }
    return doc;
}

static void init_render_pool_once() {
    g_render_pool = (RenderPool*)mem_calloc(1, sizeof(RenderPool), MEM_CAT_RENDER); // OBJ_HEAP_OK: process render worker pool singleton.
    render_pool_init(g_render_pool, g_render_pool_threads);
}

void render_pool_shutdown() {
    if (g_render_pool) {
        render_pool_destroy(g_render_pool);
        lam::Temp<RenderPool> pool(g_render_pool);  // shutdown releases the singleton
        g_render_pool = nullptr;
    }
}

static int render_output_thread_count() {
    static int cached = -1;
    if (cached >= 0) return cached;
    const char* env = getenv("RADIANT_RENDER_THREADS");
    if (env) {
        cached = atoi(env);
        if (cached < 0) cached = 0;
    } else {
        cached = 0;
    }
    return cached;
}

static RenderOutputKind render_output_kind_from_file(const char* output_file) {
    if (!output_file) {
        return RENDER_OUTPUT_SCREEN;
    }

    const char* ext = file_path_ext(output_file);
    if (!ext) {
        return RENDER_OUTPUT_PNG;
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return RENDER_OUTPUT_JPEG;
    }
    if (strcmp(ext, ".pdf") == 0) {
        return RENDER_OUTPUT_PDF;
    }
    if (strcmp(ext, ".svg") == 0) {
        return RENDER_OUTPUT_SVG;
    }
    return RENDER_OUTPUT_PNG;
}

void render_output_target_init(RenderOutputTarget* target, RenderOutputKind kind,
                               const char* output_file) {
    if (!target) {
        return;
    }
    memset(target, 0, sizeof(RenderOutputTarget));
    target->kind = kind;
    target->output_file = lam::up(output_file);
    target->jpeg_quality = 85;
    target->output_scale = 1.0f;
    target->device_scale = 1.0f;
}

void render_output_target_apply_session(RenderOutputTarget* target,
                                        const RenderExportSession* session) {
    if (!target || !session) return;
    target->viewport_width = session->viewport_width;
    target->viewport_height = session->viewport_height;
    target->output_scale = session->output_scale;
    target->device_scale = session->device_scale;
}

static bool render_export_session_begin_internal(
        RenderExportSession* session,
        int viewport_width, int viewport_height,
        int fallback_width, int fallback_height, float output_scale,
        float device_scale, bool raster_surface, bool print_media, bool paged,
        RenderExportDocumentLoader loader, void* request) {
    if (!session || !loader) return false;
    memset(session, 0, sizeof(*session));

    bool auto_width = viewport_width == 0;
    bool auto_height = viewport_height == 0;
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    if (paged) {
        // The loader takes whole-pixel viewport extents; the secondary layout retains exact A4 geometry.
        fallback_width = (int)ceilf(environment.viewport_width); // INT_CAST_OK: legacy loader viewport argument, bounded A4 extent.
        fallback_height = (int)ceilf(environment.viewport_height); // INT_CAST_OK: legacy loader viewport argument, bounded A4 extent.
        if (viewport_width > 0) environment.viewport_width = (float)viewport_width;
        if (viewport_height > 0) environment.viewport_height = (float)viewport_height;
    }
    int layout_width = viewport_width > 0 ? viewport_width : fallback_width;
    int layout_height = viewport_height > 0 ? viewport_height : fallback_height;
    session->output_scale = output_scale > 0.0f ? output_scale : 1.0f;
    session->device_scale = device_scale > 0.0f ? device_scale : 1.0f;
    session->raster_scale = session->output_scale * session->device_scale;
    session->viewport_width = viewport_width;
    session->viewport_height = viewport_height;
    session->auto_width = auto_width;
    session->auto_height = auto_height;
    session->print_media = print_media || paged;

    session->ui_context = lam::own((UiContext*)mem_calloc(1, sizeof(UiContext), MEM_CAT_RENDER)); // OBJ_HEAP_OK: export session owns the headless UI context shell.
    if (!session->ui_context) {
        log_error("[EXPORT_SESSION] Failed to allocate headless UI context");
        return false;
    }
    if (ui_context_init(session->ui_context, true, session->device_scale) != 0) {
        log_error("[EXPORT_SESSION] Failed to initialize headless UI context");
        lam::free_owned(session->ui_context);
        return false;
    }
    int surface_width = raster_surface
        ? (int)(layout_width * session->raster_scale)
        : layout_width;
    int surface_height = raster_surface
        ? (int)(layout_height * session->raster_scale)
        : layout_height;
    ui_context_create_surface(session->ui_context, surface_width, surface_height);
    session->ui_context->window_width = surface_width;
    session->ui_context->window_height = surface_height;
    session->ui_context->viewport_width = layout_width;
    session->ui_context->viewport_height = layout_height;

    session->base_url = lam::own(get_current_dir());
    if (!session->base_url) {
        log_error("[EXPORT_SESSION] Could not resolve the current directory");
        ui_context_cleanup(session->ui_context);
        lam::free_owned(session->ui_context);
        return false;
    }

    session->document = lam::up(loader(session, layout_width, layout_height, request));
    if (!session->document) {
        log_error("[EXPORT_SESSION] Could not load export document");
        render_export_session_end(session);
        return false;
    }

    // Every file exporter must lay out and measure the same scaled document before encoding.
    session->ui_context->document = session->document;
    // exports sample the same initial CSS animation timeline as headless layout.
    if (!radiant_document_ensure_state(session->document, "render_export_session")) {
        render_export_session_end(session);
        return false;
    }
    if (session->document->services.cached_css_engine) {
        ((CssEngine*)session->document->services.cached_css_engine)->context.print_media = session->print_media;
    }
    session->document->viewport.output_scale = session->output_scale;
    ui_context_sync_document_raster_scale(session->ui_context,
                                          session->document);
    process_document_font_faces(session->ui_context, session->document);
    if (paged) {
        // The settled shared DOM owns this output view; no embedded geometry is overwritten.
        session->paged_view = lam::up(view_tree_secondary_create(session->document, &environment));
        PagedLayoutOptions options = paged_layout_options_default();
        PagedLayoutDiagnostic diagnostic = {};
        TypesetStatus status = session->paged_view
            ? layout_secondary_view(session->paged_view, &options, &diagnostic) : TYPESET_OUT_OF_MEMORY;
        if (status != TYPESET_OK) {
            const char* reason = diagnostic.reason ? diagnostic.reason : "secondary view initialization failed";
            log_error("[EXPORT_PAGED] Composition failed: status=%u source=%llu page=%u reason=%s",
                (unsigned)status, (unsigned long long)diagnostic.source.expected_id, diagnostic.page_number,
                reason);
            // Export errors are user output and remain visible when diagnostic logging is disabled.
            fputs("Error: paged layout: ", stderr); fputs(reason, stderr); fputc('\n', stderr);
            render_export_session_end(session);
            return false;
        }
        return true;
    }
    if (session->print_media && render_export_has_page_rule(session->document)) {
        // Query the existing page cascade before continuous layout so its content width
        // determines line breaking while the PDF MediaBox retains the full paper size.
        ViewTree* page_query = view_tree_secondary_create(session->document, &environment);
        ViewPageStyle page_style = {};
        ViewModelStatus status = page_query
            ? view_css_page_style(page_query, nullptr, 1, VIEW_PAGE_RIGHT, false, &page_style)
            : VIEW_MODEL_OUT_OF_MEMORY;
        if (page_query) view_tree_secondary_release(session->document, page_query);
        if (status != VIEW_MODEL_OK) {
            log_error("[EXPORT_PAGE_GEOMETRY] Could not resolve @page size and margins: status=%u",
                (unsigned)status);
            render_export_session_end(session);
            return false;
        }
        session->has_page_geometry = true;
        session->page_width = page_style.width;
        session->page_height = page_style.height;
        session->page_content_x = page_style.content_rect.x;
        session->page_content_y = page_style.content_rect.y;
        session->ui_context->viewport_width = (int)ceilf(page_style.content_rect.width); // INT_CAST_OK: legacy viewport stores integral CSS pixels.
        session->ui_context->viewport_height = (int)ceilf(page_style.content_rect.height); // INT_CAST_OK: legacy viewport stores integral CSS pixels.
        ui_context_create_surface(session->ui_context,
            session->ui_context->viewport_width, session->ui_context->viewport_height);
        session->ui_context->window_width = session->ui_context->viewport_width;
        session->ui_context->window_height = session->ui_context->viewport_height;
        CssEngine* engine = (CssEngine*)session->document->services.cached_css_engine;
        css_engine_set_viewport(engine, page_style.content_rect.width,
            page_style.content_rect.height);
    }
    layout_html_doc(session->ui_context, session->document, false);

    session->content_width = session->has_page_geometry
        ? (int)ceilf(session->page_width) : layout_width; // INT_CAST_OK: legacy export extent stores integral CSS pixels.
    session->content_height = session->has_page_geometry
        ? (int)ceilf(session->page_height) : layout_height; // INT_CAST_OK: legacy export extent stores integral CSS pixels.
    if (!session->has_page_geometry && session->document->view_tree && session->document->view_tree->root) {
        int bounds_width = 0;
        int bounds_height = 0;
        calculate_content_bounds(
            session->document->view_tree->root, &bounds_width, &bounds_height);
        bounds_width += 50;
        bounds_height += 50;
        if (auto_width || bounds_width > layout_width) session->content_width = bounds_width;
        if (auto_height || bounds_height > layout_height) session->content_height = bounds_height;
    }

    if (!session->has_page_geometry && (auto_width || auto_height)) {
        log_info("[EXPORT_SESSION] Auto-sized output to %dx%d with content padding",
                 session->content_width, session->content_height);
    } else {
    }
    return true;
}

bool render_export_session_begin(RenderExportSession* session, const char* html_file,
                                 int viewport_width, int viewport_height,
                                 int fallback_width, int fallback_height, float output_scale,
                                 bool print_media, bool paged) {
    RenderExportHtmlRequest request = {html_file};
    return render_export_session_begin_internal(session,
        viewport_width, viewport_height, fallback_width, fallback_height,
        output_scale, 1.0f, false, print_media, paged,
        render_export_load_html_document, &request);
}

bool render_export_session_begin_raster(RenderExportSession* session,
                                        const char* html_file,
                                        int viewport_width, int viewport_height,
                                        float output_scale, float device_scale) {
    RenderExportHtmlRequest request = {html_file};
    return render_export_session_begin_internal(session,
        viewport_width, viewport_height, 1200, 800, output_scale,
        device_scale, true, false, false, render_export_load_html_document, &request);
}

bool render_export_session_begin_document_transform(RenderExportSession* session,
        const char* document_file, const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        int viewport_width, int viewport_height, int fallback_width,
        int fallback_height, float output_scale, float device_scale, bool raster_surface,
        bool paged, bool print_media) {
    RenderExportTransformRequest request = {document_file, transform, options, option_count};
    return render_export_session_begin_internal(session,
        viewport_width, viewport_height, fallback_width, fallback_height, output_scale,
        device_scale, raster_surface, print_media, paged,
        render_export_load_transform_document, &request);
}

void render_export_session_end(RenderExportSession* session) {
    if (!session) return;
    if (session->base_url) {
        url_destroy(session->base_url);
        session->base_url = nullptr;
    }
    if (session->ui_context) {
        ui_context_cleanup(session->ui_context);
        lam::free_owned(session->ui_context);
    }
    session->document = nullptr;
    session->paged_view = nullptr;
}

static const char* render_output_path_trace_target(RenderOutputKind kind) {
    switch (kind) {
        case RENDER_OUTPUT_SCREEN: return "screen";
        case RENDER_OUTPUT_PNG: return "png";
        case RENDER_OUTPUT_JPEG: return "jpeg";
        case RENDER_OUTPUT_TILED_PNG: return "tiled_png";
        case RENDER_OUTPUT_PDF: return "pdf";
        case RENDER_OUTPUT_SVG: return "svg";
    }
    return "unknown";
}

static void render_output_trace_backend_caps(RenderPathTrace* trace, const RenderBackendCaps* caps) {
    if (!trace || !caps) return;
    trace->backend_name = caps->backend_name;
    trace->backend_vector_paths = caps->vector_paths;
    trace->backend_gradients = caps->gradients;
    trace->backend_nested_clips = caps->nested_clips;
    trace->backend_picture_svg = caps->picture_svg;
    trace->backend_opacity_group = caps->opacity_group;
    trace->backend_blend_modes = caps->blend_modes;
    trace->backend_gaussian_blur = caps->gaussian_blur;
    trace->backend_color_matrix_filters = caps->color_matrix_filters;
    trace->backend_native_text_runs = caps->native_text_runs;
    trace->backend_vector_batching = caps->vector_batching;
    trace->backend_tile_offsets = caps->tile_offsets;
}

static void render_output_trace_retained_stats(RenderPathTrace* trace,
                                               RetainedDisplayListCache* cache) {
    if (!trace || !cache) return;
    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    trace->retained_capture_candidates = stats.capture_candidates;
    trace->retained_captured = stats.captured;
    trace->retained_skipped_non_retainable = stats.skipped_non_retainable;
    trace->retained_copy_failed = stats.copy_failed;
    trace->retained_reuse_hits = stats.reuse_hits;
    trace->retained_reuse_misses = stats.reuse_misses;
    trace->retained_reuse_rejected_resources = stats.reuse_rejected_resources;
    trace->retained_reuse_rejected_dirty = stats.reuse_rejected_dirty;
}

static void render_output_init_context(RasterRenderContext* rdcon, UiContext* uicon, ViewTree* view_tree,
                                       RenderProfiler* profiler) {
    memset(rdcon, 0, sizeof(RasterRenderContext));
    rdcon->ui_context = lam::up(uicon);
    rdcon->profiler = lam::up(profiler);
    if (uicon && uicon->document && uicon->document->state) {
        rdcon->retained_dl_cache = lam::up(uicon->document->state->retained_dl_cache);
    }

    mem_scratch_init(NULL, &rdcon->scratch, view_tree->render_scratch_arena, MEM_ROLE_RENDER, "render.scratch");
    rdcon->content_bounds_cache = lam::up(layout_content_bounds_cache_create());
    // Semantic paint IR target: routes the rc_* primitive gateway through the
    // PaintBuilder during recording (Phase C). Reused (cleared) per primitive.
    rdcon->paint_list = lam::up((PaintList*)mem_calloc(1, sizeof(PaintList), MEM_CAT_RENDER));
    if (rdcon->paint_list) new (rdcon->paint_list) PaintList();
    paint_list_init(rdcon->paint_list, nullptr);
    rdt_vector_init(&rdcon->vec, (uint32_t*)uicon->surface->pixels,
        uicon->surface->width, uicon->surface->height, uicon->surface->width);
    rdcon->transform = rdt_matrix_identity();
    rdcon->has_transform = false;
    rdcon->raster_scale = ui_context_raster_scale(uicon);

    FontProp* default_font = view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    setup_font(uicon, &rdcon->font, default_font);
    rdcon->block.clip = {0, 0, (float)uicon->surface->width, (float)uicon->surface->height};
    rdcon->color.c = 0xFF000000;
}

static void render_output_cleanup_context(RasterRenderContext* rdcon) {
    layout_content_bounds_cache_destroy(rdcon->content_bounds_cache);
    rdcon->content_bounds_cache = nullptr;
    if (rdcon->paint_list) {
        lam::Temp<PaintList> owned(rdcon->paint_list);  // this context created its paint list
        paint_list_destroy(rdcon->paint_list);
        rdcon->paint_list->~PaintList();
        rdcon->paint_list = nullptr;
    }
#ifndef NDEBUG
    size_t live_scratch = scratch_live_count(&rdcon->scratch);
    if (live_scratch) log_error("[SCRATCH_PASS_EXIT] render pass ended with %zu live scratch blocks", live_scratch);
#endif
    scratch_release(&rdcon->scratch);
    rdt_vector_destroy(&rdcon->vec);
}

RenderFrameScope::RenderFrameScope(RasterRenderContext* r, UiContext* uicon, ViewTree* view_tree,
                                   RenderProfiler* profiler)
    : rdcon(r), display_list{}, context_active(false), display_list_active(false) {
    if (!rdcon || !view_tree) return;
    render_output_init_context(rdcon, uicon, view_tree, profiler);
    context_active = true;
    dl_init(&display_list, view_tree->display_list_arena);
    display_list_active = true;
    rdcon->dl = lam::up(&display_list);
}

RenderFrameScope::~RenderFrameScope() {
    if (display_list_active) dl_destroy(&display_list);
    if (context_active) render_output_cleanup_context(rdcon);
}

static uint32_t render_output_canvas_background(View* root_view) {
    return render_document_output_background(root_view).c;
}

static RenderOutputClearResult render_output_clear_surface(RasterRenderContext* rdcon, ViewTree* view_tree,
                                                           DocState* state, uint32_t canvas_bg) {
    RenderOutputClearResult result = {};
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->surface || !view_tree) {
        return result;
    }

    bool force_full = state && state->is_dirty;
    if (!force_full && state && !state->dirty_tracker.full_repaint &&
        dirty_has_regions(&state->dirty_tracker)) {
        DirtyRect* dr = state->dirty_tracker.dirty_list;
        float scale = rdcon->raster_scale;
        while (dr) {
            Rect dirty_rect = {dr->x * scale, dr->y * scale, dr->width * scale, dr->height * scale};
            RasterPaintContext raster = raster_paint_context(rdcon->ui_context->surface, &rdcon->block.clip, nullptr, 0);
            raster_fill_rect(&raster, &dirty_rect, canvas_bg);
            dr = dr->next;
        }

        rdcon->dirty_tracker = lam::up(&state->dirty_tracker);
        Bound dirty_bounds = {};
        dirty_tracker_bounds(&state->dirty_tracker, &dirty_bounds, 1.0f);
        rdcon->dirty_union = dirty_bounds;
        rdcon->has_dirty_union = true;
        result.selective = true;
        result.replay_dirty = &state->dirty_tracker;
        return result;
    }

    RasterPaintContext raster = raster_paint_context(rdcon->ui_context->surface, &rdcon->block.clip, nullptr, 0);
    raster_fill_rect(&raster, NULL, canvas_bg);
    return result;
}

static void render_output_render_view_tree(RasterRenderContext* rdcon, ViewTree* view_tree) {
    if (!rdcon || !view_tree) {
        return;
    }

    render_raster_view_tree(rdcon, view_tree);
}

ImageSurface* render_display_list_snapshot(DisplayList* list, MemContext* memory,
        Bound physical_bounds, float raster_scale, Color backdrop) {
    if (!list || !isfinite(raster_scale) || raster_scale <= 0.0f ||
        !isfinite(physical_bounds.left) || !isfinite(physical_bounds.top) ||
        !isfinite(physical_bounds.right) || !isfinite(physical_bounds.bottom) ||
        !dl_validate_or_log(list, "render_display_list_snapshot")) return nullptr;
    ImageSurface* surface = render_surface_create_budgeted(memory,
        physical_bounds.right - physical_bounds.left, physical_bounds.bottom - physical_bounds.top);
    if (!surface) return nullptr;
    RasterPaintContext raster = raster_paint_context(surface, nullptr, nullptr, 0);
    raster_fill_rect(&raster, nullptr, render_pixel_premultiply_abgr(backdrop.c));
    RdtVector vec = {};
    rdt_vector_init(&vec, (uint32_t*)surface->pixels, surface->width, surface->height, surface->width);
    ScratchArena scratch = {};
    mem_scratch_init(memory, &scratch, nullptr, MEM_ROLE_RENDER, "render.snapshot.replay");
    dl_replay_tile(list, &vec, surface, &scratch, physical_bounds.left, physical_bounds.top,
        (float)surface->width, (float)surface->height, raster_scale);
    rdt_vector_flush_batch(&vec); rdt_vector_destroy(&vec); scratch_release(&scratch);
    // encoders and snapshot callers consume straight channels; vector replay writes premultiplied ABGR.
    for (size_t i = 0, count = (size_t)surface->width * (size_t)surface->height; i < count; i++)
        ((uint32_t*)surface->pixels)[i] = render_pixel_unpremultiply_abgr(((uint32_t*)surface->pixels)[i]);
    surface->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    return surface;
}

static ImageSurface* render_secondary_snapshot(ViewTree* tree, const ViewPageBox* page,
        float raster_scale, Color backdrop) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed ||
        !isfinite(raster_scale) || raster_scale <= 0.0f) return nullptr;
    PaintList paint = {}; paint_list_init(&paint, nullptr);
    DisplayList list = {}; dl_init(&list, nullptr);
    ImageSurface* surface = nullptr;
    // display-list replay consumes physical coordinates; apply output density once before lowering.
    RdtMatrix density = rdt_matrix_scale(raster_scale, raster_scale);
    paint_push_transform(&paint, &density);
    bool painted = page ? layout_secondary_paint_page(tree, page, &paint) : layout_secondary_paint_root(tree, &paint);
    paint_pop_transform(&paint);
    if (painted && paint_ir_validate_or_log(&paint, "secondary snapshot")) {
        paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
        paint_ir_lower_raster(&paint, &list);
        RdtLogicalRect bounds = page ? RdtLogicalRect{0, 0, page->node.rect.width, page->node.rect.height}
                                    : tree->model->root->rect;
        MemContext* memory = (MemContext*)tree->model->document->services.mem_ctx;
        surface = render_display_list_snapshot(&list, memory,
            {bounds.x * raster_scale, bounds.y * raster_scale,
             (bounds.x + bounds.width) * raster_scale, (bounds.y + bounds.height) * raster_scale},
            raster_scale, backdrop);
    }
    dl_destroy(&list); paint_list_destroy(&paint);
    return surface;
}

ImageSurface* render_secondary_view_snapshot(ViewTree* tree, float raster_scale, Color backdrop) {
    return render_secondary_snapshot(tree, nullptr, raster_scale, backdrop);
}

static ViewPageBox* render_secondary_page(ViewTree* tree, uint32_t number) {
    if (!view_tree_model_source_valid(tree) || !tree->model->committed || !number ||
        number > tree->model->page_count) return nullptr;
    ViewPageBox* page = tree->model->pages.get()[number - 1];
    return page && page->page_number == number && view_tree_node_resolve(tree, page->node.ref) == &page->node
        ? page : nullptr;
}

ImageSurface* render_secondary_page_snapshot(ViewTree* tree, uint32_t page_number,
        float raster_scale, Color backdrop) {
    ViewPageBox* page = render_secondary_page(tree, page_number);
    return page ? render_secondary_snapshot(tree, page, raster_scale, backdrop) : nullptr;
}

struct RenderPageSnapshotEntry {
    LayoutViewRef page;
    uint64_t source_epoch, resources, fonts;
    float scale;
    Color backdrop;
    size_t bytes;
    lam::Own<ImageSurface> surface;
    lam::Up<RenderPageSnapshotEntry> previous, next;
};

struct RenderPageSnapshotCache {
    lam::Own<Pool> pool;
    lam::Up<RenderPageSnapshotEntry> entries;
    lam::Up<RenderPageSnapshotEntry> first, last;
    size_t max_entries, max_bytes;
    RenderPageSnapshotCacheStats stats;
};

RenderPageSnapshotCache* render_page_snapshot_cache_create(size_t max_entries, size_t max_bytes) {
    if (!max_entries || max_entries > SIZE_MAX / sizeof(RenderPageSnapshotEntry) || !max_bytes) return nullptr;
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "render.page_snapshot_cache");
    if (!pool) return nullptr;
    RenderPageSnapshotCache* cache = (RenderPageSnapshotCache*)pool_calloc(pool, sizeof(RenderPageSnapshotCache));
    RenderPageSnapshotEntry* entries = (RenderPageSnapshotEntry*)pool_calloc(pool,
        max_entries * sizeof(RenderPageSnapshotEntry));
    if (!cache || !entries) { mem_pool_destroy(pool); return nullptr; }
    cache->pool = lam::own(pool); cache->entries = lam::up(entries);
    cache->max_entries = max_entries; cache->max_bytes = max_bytes;
    return cache;
}

static void render_page_snapshot_unlink(RenderPageSnapshotCache* cache, RenderPageSnapshotEntry* entry) {
    if (entry->previous) entry->previous->next = entry->next; else cache->first = entry->next;
    if (entry->next) entry->next->previous = entry->previous; else cache->last = entry->previous;
    entry->previous = entry->next = nullptr;
}

static void render_page_snapshot_remove(RenderPageSnapshotCache* cache, RenderPageSnapshotEntry* entry) {
    render_page_snapshot_unlink(cache, entry);
    cache->stats.bytes -= entry->bytes; cache->stats.entries--;
    image_surface_destroy(entry->surface);
    *entry = {};
}

static void render_page_snapshot_promote(RenderPageSnapshotCache* cache, RenderPageSnapshotEntry* entry) {
    entry->previous = nullptr; entry->next = cache->first;
    if (cache->first) cache->first->previous = lam::up(entry); else cache->last = lam::up(entry);
    cache->first = lam::up(entry);
}

void render_page_snapshot_cache_clear(RenderPageSnapshotCache* cache) {
    if (cache) while (cache->last) render_page_snapshot_remove(cache, cache->last);
}

void render_page_snapshot_cache_destroy(RenderPageSnapshotCache* cache) {
    if (!cache) return;
    render_page_snapshot_cache_clear(cache);
    mem_pool_destroy(cache->pool);
}

RenderPageSnapshotCacheStats render_page_snapshot_cache_stats(const RenderPageSnapshotCache* cache) {
    return cache ? cache->stats : RenderPageSnapshotCacheStats{};
}

const ImageSurface* render_page_snapshot_cache_get(RenderPageSnapshotCache* cache, ViewTree* tree,
        uint32_t page_number, float raster_scale, Color backdrop) {
    if (!cache || !tree || !tree->model) return nullptr;
    ViewTreeModel* model = tree->model;
    uint64_t fonts = model->css ? font_context_resource_generation(model->css->fonts) : 0;
    // a presentation generation changes placement only; page content uses the layout/resource generations.
    for (size_t i = 0; i < cache->max_entries; i++) {
        RenderPageSnapshotEntry* entry = cache->entries.get() + i;
        if (entry->surface && entry->page.tree_id == model->tree_id &&
            (!view_tree_model_source_valid(tree) || !model->committed ||
             entry->page.generation != tree->layout_generation || entry->source_epoch != model->source_epoch ||
             entry->resources != model->environment.resource_generation || entry->fonts != fonts))
            render_page_snapshot_remove(cache, entry);
    }
    ViewPageBox* page = render_secondary_page(tree, page_number);
    size_t bytes = 0;
    if (!page || !isfinite(raster_scale) || raster_scale <= 0.0f ||
        !render_surface_allocation_size(page->node.rect.width * raster_scale,
            page->node.rect.height * raster_scale, &bytes) || bytes > cache->max_bytes) return nullptr;
    RenderPageSnapshotEntry* free_entry = nullptr;
    for (size_t i = 0; i < cache->max_entries; i++) {
        RenderPageSnapshotEntry* entry = cache->entries.get() + i;
        if (!entry->surface) { free_entry = entry; continue; }
        if (entry->page.tree_id == page->node.ref.tree_id && entry->page.generation == page->node.ref.generation &&
            entry->page.node_id == page->node.ref.node_id && entry->scale == raster_scale && entry->backdrop.c == backdrop.c) {
            render_page_snapshot_unlink(cache, entry); render_page_snapshot_promote(cache, entry);
            cache->stats.hits++;
            return entry->surface;
        }
    }
    while (cache->stats.entries == cache->max_entries || cache->stats.bytes > cache->max_bytes - bytes) {
        free_entry = cache->last;
        render_page_snapshot_remove(cache, free_entry); cache->stats.evictions++;
    }
    ImageSurface* surface = render_secondary_snapshot(tree, page, raster_scale, backdrop);
    if (!surface) return nullptr;
    free_entry->page = page->node.ref; free_entry->source_epoch = model->source_epoch;
    free_entry->resources = model->environment.resource_generation;
    free_entry->fonts = model->css ? font_context_resource_generation(model->css->fonts) : 0;
    free_entry->scale = raster_scale; free_entry->backdrop = backdrop; free_entry->bytes = bytes;
    free_entry->surface = lam::own(surface);
    render_page_snapshot_promote(cache, free_entry);
    cache->stats.entries++; cache->stats.bytes += bytes; cache->stats.renders++;
    return surface;
}

bool render_secondary_view_to_png(ViewTree* tree, const char* filename, float raster_scale, Color backdrop) {
    if (!filename) return false;
    ImageSurface* surface = render_secondary_view_snapshot(tree, raster_scale, backdrop);
    if (!surface) return false;
    StrBuf* png = render_encode_surface_png(surface);
    image_surface_destroy(surface);
    if (!png) return false;
    bool written = write_binary_file(filename, png->str, png->length) == 0;
    strbuf_free(png);
    return written;
}

static RenderOutputReplayResult render_output_replay_display_list(RasterRenderContext* rdcon,
                                                                  DisplayList* display_list,
                                                                  uint32_t canvas_bg,
                                                                  DirtyTracker* replay_dirty) {
    RenderOutputReplayResult result = {};
    result.thread_count = 1;
    if (!rdcon || !display_list || !rdcon->ui_context || !rdcon->ui_context->surface) {
        return result;
    }

    rdcon->dl = nullptr;

    int render_threads = render_output_thread_count();
    int item_count = dl_item_count(display_list);
    if (!dl_validate_or_log(display_list, "render_output_replay_display_list")) {
        return result;
    }
    bool has_glyphs = dl_contains_glyphs(display_list);
    if (!replay_dirty && render_threads != 1 && item_count > 0 && !has_glyphs) {
        ImageSurface* surface = rdcon->ui_context->surface;
        TileGrid grid;
        tile_grid_init(&grid, surface->width, surface->height, rdcon->raster_scale);
        if (grid.total <= 0) {
            log_error("[RENDER] tile grid initialization failed for %dx%d surface",
                      surface->width, surface->height);
            return result;
        }
        tile_grid_clear(&grid, canvas_bg);

        g_render_pool_threads = render_threads;
        pthread_once(&g_render_pool_once, init_render_pool_once);

        // Render jobs are frame-scoped; scratch allocation prevents queue storage from outliving dispatch.
        ScratchScope jobs_scope(&rdcon->scratch);
        TileJob* jobs = jobs_scope.array_zero<TileJob>((size_t)grid.total);
        if (!jobs) {
            log_error("[RENDER] failed to allocate %d tile jobs", grid.total);
            tile_grid_destroy(&grid);
            return result;
        }
        for (int i = 0; i < grid.total; i++) {
            jobs[i].tile = lam::up(&grid.tiles[i]);
            jobs[i].display_list = lam::up(display_list);
            jobs[i].raster_scale = rdcon->raster_scale;
            jobs[i].bg_color = canvas_bg;
        }

        render_pool_dispatch(g_render_pool, jobs, grid.total);
        tile_grid_composite(&grid, surface);

        result.tiled = true;
        result.tile_count = grid.total;
        result.thread_count = g_render_pool ? g_render_pool->thread_count : 1;

        jobs_scope.end();
        tile_grid_destroy(&grid);
        return result;
    }

    dl_replay(display_list, &rdcon->vec, rdcon->ui_context->surface,
              &rdcon->block.clip, &rdcon->scratch, rdcon->raster_scale, replay_dirty);
    return result;
}

static void render_output_save_surface(ImageSurface* surface, const char* output_file) {
    if (!surface || !output_file) {
        return;
    }

    const char* ext = file_path_ext(output_file);
    if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
        save_surface_to_jpeg(surface, output_file, 85);
    } else {
        save_surface_to_png(surface, output_file);
    }
}

static int render_output_render_raster_target(UiContext* uicon, ViewTree* view_tree,
                                              RenderOutputTarget* target) {
    uint64_t t_start = time_now_ns();

    if (!uicon || !view_tree || !target) {
        log_error("render_output_render_raster_target: invalid render job");
        return 1;
    }

    RenderProfiler profiler;
    render_profiler_reset(&profiler);
    RasterRenderContext rdcon;
    RenderFrameScope frame(&rdcon, uicon, view_tree, &profiler);
    DisplayList& display_list = *frame.list();

    uint32_t canvas_bg = render_output_canvas_background(view_tree->root);
    DocState* state = uicon->document ? (DocState*)uicon->document->state : nullptr;
    RenderOutputClearResult clear_result =
        render_output_clear_surface(&rdcon, view_tree, state, canvas_bg);
    bool selective = clear_result.selective;

    retained_dl_cache_begin_frame(rdcon.retained_dl_cache);

    uint64_t t_init = time_now_ns();

    render_output_render_view_tree(&rdcon, view_tree);

    uint64_t t_render = time_now_ns();
    log_info("[TIMING] render_block_view (record): %.1fms, %d display list items",
             time_elapsed_ms_f(t_init, t_render), dl_item_count(&display_list));
    render_profiler_log(rdcon.profiler);

    double render_ms = time_elapsed_ms_f(t_init, t_render);
    render_profiler_write_record_stderr(render_ms, uicon->surface->width,
        uicon->surface->height, dl_item_count(&display_list));
    render_profiler_write_counters_stderr(rdcon.profiler);

    if (uicon->document && uicon->document->state) {
        render_ui_overlays(&rdcon, uicon->document->state);
    } else {
    }
    if (!dl_validate_or_log(&display_list, "render_output_raster")) {
        return 1;
    }
    retained_dl_cache_capture(rdcon.retained_dl_cache, &display_list);

    uint64_t t_replay_start = time_now_ns();

    int item_count = dl_item_count(&display_list);
    RenderOutputReplayResult replay_result =
        render_output_replay_display_list(&rdcon, &display_list, canvas_bg, clear_result.replay_dirty);

    uint64_t t_replay_end = time_now_ns();
    double replay_ms = time_elapsed_ms_f(t_replay_start, t_replay_end);
    if (replay_result.tiled) {
        log_info("[TIMING] dl_replay_tiled: %.1fms (%d items, %d tiles, %d threads)",
                 replay_ms, item_count, replay_result.tile_count, replay_result.thread_count);
        render_profiler_write_tiled_replay_stderr(replay_ms, item_count,
            replay_result.tile_count, replay_result.thread_count);
    } else {
        log_info("[TIMING] dl_replay: %.1fms (%d items)",
                 replay_ms, item_count);
        render_profiler_write_replay_stderr(replay_ms, item_count);
    }

    DocState* rstate = uicon->document ? uicon->document->state : nullptr;
    render_video_frames(&display_list, rdcon.ui_context->surface, rstate, rdcon.ui_context);

    uint64_t t_sync = time_now_ns();
    log_info("[TIMING] render complete: %.1fms", time_elapsed_ms_f(t_render, t_sync));
    render_profiler_emit_event(rdcon.profiler, uicon, rstate, render_ms, replay_ms,
        time_elapsed_ms_f(t_start, t_sync), item_count, selective,
        replay_result.tiled, replay_result.tile_count, replay_result.thread_count);
    RenderPathTrace trace = {};
    trace.target = lam::up(render_output_path_trace_target(target->kind));
    trace.replay_mode = lam::up(replay_result.tiled ? "display_list_tiled" : "display_list_single");
    trace.display_list_recorded = true;
    trace.paint_ir_enabled = rdcon.paint_list != nullptr;
    trace.selective = selective;
    trace.tiled_replay = replay_result.tiled;
    trace.large_tiled_export = false;
    trace.display_list_items = item_count;
    trace.tile_count = replay_result.tile_count;
    trace.thread_count = replay_result.thread_count;
    trace.surface_width = uicon->surface ? uicon->surface->width : 0;
    trace.surface_height = uicon->surface ? uicon->surface->height : 0;
    render_output_trace_backend_caps(&trace, render_backend_get_caps(&rdcon.vec));
    render_output_trace_retained_stats(&trace, rdcon.retained_dl_cache);
    render_profiler_emit_path_trace(rdcon.profiler, uicon, rstate, &trace);

    if (target->kind == RENDER_OUTPUT_JPEG && target->output_file) {
        save_surface_to_jpeg(rdcon.ui_context->surface, target->output_file,
                             target->jpeg_quality > 0 ? target->jpeg_quality : 85);
    } else if (target->kind == RENDER_OUTPUT_PNG && target->output_file) {
        save_surface_to_png(rdcon.ui_context->surface, target->output_file);
    } else {
        render_output_save_surface(rdcon.ui_context->surface, target->output_file);
    }

    uint64_t t_save = time_now_ns();
    if (target->output_file) {
        log_info("[TIMING] save_to_file: %.1fms", time_elapsed_ms_f(t_sync, t_save));
    }

    if (uicon->document && uicon->document->state) {
        doc_state_clear_render_flags(uicon->document->state);
    }

    uint64_t t_end = time_now_ns();
    log_info("[TIMING] render_html_doc total: %.1fms%s",
        time_elapsed_ms_f(t_start, t_end), selective ? " (selective)" : "");
    return 0;
}

int render_output_render_view_tree_to_target(UiContext* uicon, ViewTree* view_tree,
                                             RenderOutputTarget* target) {
    if (!target) {
        log_error("render_output_render_view_tree_to_target: missing output target");
        return 1;
    }
    // a scroll since the last layout moved sticky boxes (CSS Position 3)
    layout_resolve_scrolled_sticky(view_tree);

    switch (target->kind) {
        case RENDER_OUTPUT_SCREEN:
        case RENDER_OUTPUT_PNG:
        case RENDER_OUTPUT_JPEG:
            return render_output_render_raster_target(uicon, view_tree, target);
        case RENDER_OUTPUT_TILED_PNG:
            render_output_render_tiled_png(uicon, view_tree, target->output_file,
                                           target->width, target->height);
            return 0;
        case RENDER_OUTPUT_PDF:
        case RENDER_OUTPUT_SVG:
            log_error("render_output_render_view_tree_to_target: PDF/SVG targets require file-level export");
            return 1;
    }

    log_error("render_output_render_view_tree_to_target: unknown output target kind %d", target->kind);
    return 1;
}

static int render_output_render_html_file_to_target(const char* html_file,
                                                    RenderOutputTarget* target) {
    if (!html_file || !target || !target->output_file) {
        log_error("render_output_render_html_file_to_target: invalid file render job");
        return 1;
    }
    if (target->paged && target->kind != RENDER_OUTPUT_PDF) {
        log_error("[EXPORT_PAGED] Paged file output currently requires a PDF target");
        return 1;
    }

    float output_scale = target->output_scale > 0 ? target->output_scale : 1.0f;
    float device_scale = target->device_scale > 0 ? target->device_scale : 1.0f;
    int viewport_width = target->viewport_width;
    int viewport_height = target->viewport_height;
    if (target->kind == RENDER_OUTPUT_PDF || target->kind == RENDER_OUTPUT_SVG) {
        RenderProfiler profiler;
        render_profiler_reset(&profiler);
        RenderPathTrace trace = {};
        trace.target = lam::up(render_output_path_trace_target(target->kind));
        trace.replay_mode = lam::up("file_export");
        trace.backend_name = trace.target;
        trace.display_list_recorded = false;
        trace.paint_ir_enabled = false;
        trace.surface_width = viewport_width;
        trace.surface_height = viewport_height;
        render_profiler_emit_path_trace(&profiler, nullptr, nullptr, &trace);
    }

    switch (target->kind) {
        case RENDER_OUTPUT_PDF:
            return render_html_to_pdf(html_file, target->output_file,
                                      viewport_width, viewport_height, output_scale, target->paged);
        case RENDER_OUTPUT_SVG:
            return render_html_to_svg(html_file, target->output_file,
                                      viewport_width, viewport_height, output_scale);
        case RENDER_OUTPUT_PNG:
        case RENDER_OUTPUT_TILED_PNG:
            return render_html_to_png(html_file, target->output_file,
                                      viewport_width, viewport_height, output_scale, device_scale);
        case RENDER_OUTPUT_JPEG:
            return render_html_to_jpeg(html_file, target->output_file,
                                       target->jpeg_quality > 0 ? target->jpeg_quality : 85,
                                       viewport_width, viewport_height, output_scale, device_scale);
        case RENDER_OUTPUT_SCREEN:
            log_error("render_output_render_html_file_to_target: screen target needs a UiContext");
            return 1;
    }

    log_error("render_output_render_html_file_to_target: unknown output target kind %d", target->kind);
    return 1;
}

static int render_output_render_document_transform_to_target(const char* document_file,
        const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        RenderOutputTarget* target) {
    if (!document_file || !transform || !target || !target->output_file) {
        log_error("render document transform: invalid export job");
        return 1;
    }
    if (target->paged && target->kind != RENDER_OUTPUT_PDF) {
        log_error("[EXPORT_PAGED] Paged transformed output currently requires a PDF target");
        return 1;
    }

    float output_scale = target->output_scale > 0 ? target->output_scale : 1.0f;
    float device_scale = target->device_scale > 0 ? target->device_scale : 1.0f;
    int viewport_width = target->viewport_width;
    int viewport_height = target->viewport_height;
    switch (target->kind) {
        case RENDER_OUTPUT_PDF:
            return render_document_transform_to_pdf(document_file, transform, options,
                option_count, target->output_file, viewport_width, viewport_height, output_scale, target->paged);
        case RENDER_OUTPUT_SVG:
            return render_document_transform_to_svg(document_file, transform, options,
                option_count, target->output_file, viewport_width, viewport_height, output_scale);
        case RENDER_OUTPUT_PNG:
        case RENDER_OUTPUT_TILED_PNG:
            return render_document_transform_to_png(document_file, transform, options,
                option_count, target->output_file, viewport_width, viewport_height,
                output_scale, device_scale);
        case RENDER_OUTPUT_JPEG:
            return render_document_transform_to_jpeg(document_file, transform, options,
                option_count, target->output_file,
                target->jpeg_quality > 0 ? target->jpeg_quality : 85,
                viewport_width, viewport_height, output_scale, device_scale);
        case RENDER_OUTPUT_SCREEN:
            log_error("render document transform: screen target needs a UiContext");
            return 1;
    }

    log_error("render document transform: unknown output target kind %d", target->kind);
    return 1;
}

int render_html_to_output_target(const char* html_file, const char* output_file,
                                 int viewport_width, int viewport_height,
                                 float output_scale, float device_scale,
                                 int jpeg_quality, bool paged) {
    RenderOutputTarget target;
    render_output_target_init(&target, render_output_kind_from_file(output_file), output_file);
    target.viewport_width = viewport_width;
    target.viewport_height = viewport_height;
    target.output_scale = output_scale;
    target.device_scale = device_scale;
    target.jpeg_quality = jpeg_quality > 0 ? jpeg_quality : 85;
    target.paged = paged;
    return render_output_render_html_file_to_target(html_file, &target);
}

int render_document_transform_to_output_target(const char* document_file,
        const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int option_count,
        const char* output_file, int viewport_width, int viewport_height,
        float output_scale, float device_scale, int jpeg_quality, bool paged) {
    RenderOutputTarget target;
    render_output_target_init(&target, render_output_kind_from_file(output_file), output_file);
    target.viewport_width = viewport_width;
    target.viewport_height = viewport_height;
    target.output_scale = output_scale;
    target.device_scale = device_scale;
    target.jpeg_quality = jpeg_quality > 0 ? jpeg_quality : 85;
    target.paged = paged;
    return render_output_render_document_transform_to_target(document_file, transform, options,
        option_count, &target);
}

static void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    RenderOutputTarget target;
    render_output_target_init(&target, render_output_kind_from_file(output_file), output_file);
    target.surface = lam::up(uicon ? uicon->surface : nullptr);
    if (target.kind == RENDER_OUTPUT_PDF || target.kind == RENDER_OUTPUT_SVG) {
        log_error("render_output_render_html_doc: PDF/SVG require render_output_render_html_file_to_target");
        return;
    }
    render_output_render_view_tree_to_target(uicon, view_tree, &target);
}

/**
 * Render a large output image in horizontal PNG strips.
 *
 * The caller passes the full physical output size. The view tree is walked
 * once to record a single full-page display list; each horizontal strip is then
 * produced by replaying that list through dl_replay_tile() with the strip as the
 * tile region. This is the same record-once / replay-many pipeline used by
 * normal raster output and threaded tiled replay, so strip output matches
 * screen/PNG output instead of diverging through a separate per-strip walk.
 *
 * Streaming rows into libpng keeps peak pixel memory bounded by the strip
 * height rather than the full page height.
 */
static void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                           const char* output_file,
                                           int total_width, int total_height) {
    uint64_t t_start = time_now_ns();

    // physical pixels per strip; overridable for parity testing against normal PNG.
    int TILE_H = 4096;
    if (const char* env = getenv("RADIANT_TILE_STRIP_H")) {
        int v = atoi(env);
        if (v > 0) TILE_H = v;
    }
    int tile_count = (total_height + TILE_H - 1) / TILE_H;
    log_info("render_output_render_tiled_png: %dx%d px -> %s (%d tiles of %d px)",
        total_width, total_height, output_file,
        tile_count, TILE_H);

    FILE* fp = fopen(output_file, "wb");
    if (!fp) {
        log_error("render_output_render_tiled_png: cannot open output file: %s", output_file);
        return;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return; }
    if (setjmp(png_jmpbuf(png))) {
        log_error("render_output_render_tiled_png: PNG error during write");
        png_destroy_write_struct(&png, &info); fclose(fp); return;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, total_width, total_height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint32_t canvas_bg = render_output_canvas_background(view_tree->root);

    ImageSurface* saved_surface = uicon->surface;
    int saved_window_height = uicon->window_height;

    int first_h = total_height < TILE_H ? total_height : TILE_H;

    // Recording surface: render_output_init_context() needs a surface for vector
    // backend setup, but recording never touches its pixels (rdcon.dl is set).
    // Size it to the first strip; the vector backend is rebound per strip below.
    ImageSurface* rec_surf = image_surface_create(total_width, first_h);
    if (!rec_surf) {
        log_error("render_output_render_tiled_png: failed to allocate recording surface %dx%d",
            total_width, first_h);
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    uicon->surface = lam::up(rec_surf);
    uicon->window_height = first_h;

    RenderProfiler profiler;
    render_profiler_reset(&profiler);
    RasterRenderContext rdcon;
    RenderFrameScope frame(&rdcon, uicon, view_tree, &profiler);
    DisplayList& display_list = *frame.list();

    // Record the full page once. Use the whole page as the root clip so nothing
    // is culled at record time; per-strip culling happens during replay.
    rdcon.block.clip = {0, 0, (float)total_width, (float)total_height};

    uint64_t t_record_start = time_now_ns();
    render_output_render_view_tree(&rdcon, view_tree);
    rdcon.dl = nullptr;
    uint64_t t_record_end = time_now_ns();
    log_info("[TIMING] render_output_render_tiled_png record: %.1fms, %d display list items",
        time_elapsed_ms_f(t_record_start, t_record_end),
        dl_item_count(&display_list));
    if (!dl_validate_or_log(&display_list, "render_output_tiled_png")) {
        image_surface_destroy(rec_surf);
        uicon->surface = lam::up(saved_surface);
        uicon->window_height = saved_window_height;
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    for (int tile_y = 0; tile_y < total_height; tile_y += TILE_H) {
        int tile_h = (tile_y + TILE_H <= total_height) ? TILE_H : (total_height - tile_y);

        ImageSurface* tile_surf = (tile_y == 0) ? rec_surf
                                                : image_surface_create(total_width, tile_h);
        if (!tile_surf) {
            log_error("render_output_render_tiled_png: failed to allocate tile surface %dx%d at y=%d",
                total_width, tile_h, tile_y);
            break;
        }

        {
            Bound tile_clip = {0, 0, (float)total_width, (float)tile_h};
            RasterPaintContext raster = raster_paint_context(tile_surf, &tile_clip, nullptr, 0);
            raster_fill_rect(&raster, NULL, canvas_bg);
        }
        // dl_replay_tile uses tile-local coordinates and translates via tile_y,
        // so the strip surface itself starts at local origin 0.
        tile_surf->tile_offset_y = 0;

        // Rebind the vector backend to this strip's pixel buffer.
        rdt_vector_set_target(&rdcon.vec, (uint32_t*)tile_surf->pixels,
                              total_width, tile_h, total_width);

        dl_replay_tile(&display_list, &rdcon.vec, tile_surf, &rdcon.scratch,
                       0.0f, (float)tile_y, (float)total_width, (float)tile_h,
                       rdcon.raster_scale);

        for (int y = 0; y < tile_h; y++) {
            uint8_t* row = (uint8_t*)tile_surf->pixels + y * tile_surf->pitch;
            png_write_row(png, row);
        }

        if (tile_surf != rec_surf) {
            image_surface_destroy(tile_surf);
        }
        log_info("render_output_render_tiled_png: tile y=%d..%d done", tile_y, tile_y + tile_h);
    }

    DocState* rstate = uicon->document ? uicon->document->state : nullptr;
    RenderPathTrace trace = {};
    trace.target = lam::up("tiled_png");
    trace.replay_mode = lam::up("display_list_strip");
    trace.display_list_recorded = true;
    trace.paint_ir_enabled = rdcon.paint_list != nullptr;
    trace.selective = false;
    trace.tiled_replay = true;
    trace.large_tiled_export = true;
    trace.display_list_items = dl_item_count(&display_list);
    trace.tile_count = tile_count;
    trace.thread_count = 1;
    trace.surface_width = total_width;
    trace.surface_height = total_height;
    render_output_trace_backend_caps(&trace, render_backend_get_caps(&rdcon.vec));
    render_output_trace_retained_stats(&trace, rdcon.retained_dl_cache);
    render_profiler_emit_path_trace(rdcon.profiler, uicon, rstate, &trace);

    image_surface_destroy(rec_surf);

    uicon->surface = lam::up(saved_surface);
    uicon->window_height = saved_window_height;

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    uint64_t t_end = time_now_ns();
    log_info("[TIMING] render_output_render_tiled_png total: %.1fms (%dx%d)",
        time_elapsed_ms_f(t_start, t_end), total_width, total_height);
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    render_output_render_html_doc(uicon, view_tree, output_file);
}

void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree,
                           const char* output_file,
                           int total_width, int total_height) {
    render_output_render_tiled_png(uicon, view_tree, output_file,
                                   total_width, total_height);
}
