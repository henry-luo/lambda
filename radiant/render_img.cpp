#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "../lib/tagged.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/mempool.h"
#include "../lib/memtrack.h"
#include "../lib/log.h"
}
#include "../lambda/input/input.hpp"
#include "../lambda/js/js_runtime.h"
#include "../radiant/radiant.hpp"
#include <stdio.h>
#include <string.h>
#include <turbojpeg.h>
#include <chrono>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/time.h>
#else
#include <windows.h>
#include <psapi.h>
// Stub for getrusage on Windows
#define RUSAGE_SELF 0
struct rusage { long ru_maxrss; };
static inline int getrusage(int who, struct rusage* r) {
    (void)who;
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        r->ru_maxrss = (long)(pmc.PeakWorkingSetSize / 1024); // report in KB like Linux
    } else { r->ru_maxrss = 0; }
    return 0;
}
#endif

// getrusage().ru_maxrss returns bytes on macOS/Darwin and kilobytes on Linux
#if defined(__APPLE__)
#define RUSAGE_MAXRSS_TO_BYTES(v) ((size_t)(v))
#else
#define RUSAGE_MAXRSS_TO_BYTES(v) ((size_t)(v) * 1024)
#endif

// Save surface to PNG using libpng
void save_surface_to_png(ImageSurface* surface, const char* filename) {
    StrBuf* png_bytes = render_encode_surface_png(surface);
    if (!png_bytes) {
        log_error("Failed to encode PNG surface");
        return;
    }
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("Failed to open file for writing: %s", filename);
        strbuf_free(png_bytes);
        return;
    }
    size_t png_length = png_bytes->length;
    size_t written = fwrite(png_bytes->str, 1, png_length, fp);
    fclose(fp);
    strbuf_free(png_bytes);
    if (written == png_length) {
        log_info("Successfully saved PNG: %s", filename);
    } else {
        log_error("Failed to write complete PNG: %s", filename);
    }
}

// Save surface to JPEG using TurboJPEG
void save_surface_to_jpeg(ImageSurface* surface, const char* filename, int quality) {
    tjhandle tj_instance = tjInitCompress();
    if (!tj_instance) {
        log_error("Failed to initialize TurboJPEG compressor: %s", tjGetErrorStr());
        return;
    }

    // Convert RGBA to RGB (JPEG doesn't support alpha channel)
    int width = surface->width;
    int height = surface->height;
    unsigned char* rgb_buffer = (unsigned char*)mem_alloc(width * height * 3, MEM_CAT_RENDER);
    if (!rgb_buffer) {
        log_error("Failed to allocate memory for RGB buffer");
        tjDestroy(tj_instance);
        return;
    }

    // Convert RGBA pixels to RGB
    uint8_t* src_pixels = (uint8_t*)surface->pixels;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = (y * surface->pitch) + (x * 4); // RGBA = 4 bytes per pixel
            int dst_idx = (y * width * 3) + (x * 3);      // RGB = 3 bytes per pixel

            rgb_buffer[dst_idx + 0] = src_pixels[src_idx + 0]; // R
            rgb_buffer[dst_idx + 1] = src_pixels[src_idx + 1]; // G
            rgb_buffer[dst_idx + 2] = src_pixels[src_idx + 2]; // B
            // Skip alpha channel
        }
    }

    unsigned char* jpeg_buffer = NULL;
    unsigned long jpeg_size = 0;

    // Compress to JPEG
    int result = tjCompress2(tj_instance, rgb_buffer, width, 0, height, TJPF_RGB,
                             &jpeg_buffer, &jpeg_size, TJSAMP_444, quality, TJFLAG_FASTDCT);

    if (result != 0) {
        log_error("TurboJPEG compression failed: %s", tjGetErrorStr());
        mem_free(rgb_buffer);
        tjDestroy(tj_instance);
        return;
    }

    // Write JPEG data to file
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("Failed to open file for writing: %s", filename);
        mem_free(rgb_buffer);
        tjFree(jpeg_buffer);
        tjDestroy(tj_instance);
        return;
    }

    size_t written = fwrite(jpeg_buffer, 1, jpeg_size, fp);
    if (written != jpeg_size) {
        log_error("Failed to write complete JPEG data to file: %s", filename);
    } else {
        log_info("Successfully saved JPEG: %s (quality: %d)", filename, quality);
    }

    // Clean up
    fclose(fp);
    mem_free(rgb_buffer);
    tjFree(jpeg_buffer);
    tjDestroy(tj_instance);
}

// Main function to layout HTML and render to PNG
// output_scale: Explicit export density (default 1.0).
// device_scale: Device pixel density (default 1.0, use 2.0 for Retina displays).
// Final output size is viewport * output_scale * device_scale.
static bool render_png_resolve_auto_size(DomDocument* doc, float raster_scale,
                                         bool auto_width, bool auto_height,
                                         int* output_width, int* output_height,
                                         int* content_max_x, int* content_max_y) {
    if ((!auto_width && !auto_height) || !doc || !doc->view_tree || !doc->view_tree->root) {
        return false;
    }

    calculate_content_bounds(doc->view_tree->root, content_max_x, content_max_y);
    *content_max_x += 50;
    *content_max_y += 50;
    if (auto_width) *output_width = (int)(*content_max_x * raster_scale);
    if (auto_height) *output_height = (int)(*content_max_y * raster_scale);

    View* root_view = doc->view_tree->root;
    if (root_view->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* root_block = lam::view_require_block(root_view);
        if (root_block->scroller && root_block->scroll_mut()->has_clip) {
            if (auto_width) root_block->scroll_mut()->clip.right = (float)*content_max_x;
            if (auto_height) root_block->scroll_mut()->clip.bottom = (float)*content_max_y;
        }
    }
    return true;
}

int render_html_to_png(const char* html_file, const char* png_file, int viewport_width, int viewport_height, float output_scale, float device_scale) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    RenderExportSession session;
    if (!render_export_session_begin_raster(&session, html_file,
            viewport_width, viewport_height, output_scale, device_scale)) {
        return 1;
    }
    UiContext* ui_context = session.ui_context;
    DomDocument* doc = session.document;
    float raster_scale = session.raster_scale;
    int output_width = (int)(session.content_width * raster_scale);
    int output_height = (int)(session.content_height * raster_scale);

    // pixel threshold above which tiled rendering is used to avoid OOM on huge pages
    // (32 M pixels × 4 bytes = 128 MB; e.g. 1200-px wide → ~26 000 px tall)
    // Overridable via RADIANT_TILE_THRESHOLD (pixels) so parity tests can force the
    // tiled path on a small page.
    int64_t PNG_TILE_THRESHOLD = (int64_t)32 * 1024 * 1024;
    if (const char* env = getenv("RADIANT_TILE_THRESHOLD")) {
        long long v = atoll(env);
        if (v > 0) PNG_TILE_THRESHOLD = (int64_t)v;
    }

    bool rendered = false;

    int content_max_x = 0, content_max_y = 0;
    if (render_png_resolve_auto_size(doc, raster_scale,
            session.auto_width, session.auto_height,
            &output_width, &output_height, &content_max_x, &content_max_y)) {
        log_info("Auto-sized output dimensions: %dx%d (content bounds with 50px padding, output_scale=%.2f, device_scale=%.2f)",
                 output_width, output_height, session.output_scale,
                 session.device_scale);

        if ((int64_t)output_width * output_height > PNG_TILE_THRESHOLD) {
            // Large page: render in tiles to avoid allocating a single huge surface
            log_info("render_html_to_png: using tiled render (%dx%d exceeds %lld-pixel threshold)",
                output_width, output_height, PNG_TILE_THRESHOLD);
            RenderOutputTarget target;
            render_output_target_init(&target, RENDER_OUTPUT_TILED_PNG, png_file);
            target.width = output_width;
            target.height = output_height;
            render_output_target_apply_session(&target, &session);
            render_output_render_view_tree_to_target(ui_context, doc->view_tree, &target);
            rendered = true;
        } else {
            ui_context_create_surface(ui_context, output_width, output_height);
        }
    }

    // Render the document (normal path only)
    if (!rendered) {
        if (doc && doc->view_tree) {
            RenderOutputTarget target;
            render_output_target_init(&target, RENDER_OUTPUT_PNG, png_file);
            target.surface = ui_context->surface;
            render_output_target_apply_session(&target, &session);
            render_output_render_view_tree_to_target(ui_context, doc->view_tree, &target);
        } else {
            render_export_session_end(&session);
            return 1;
        }
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] TOTAL: %.1fms", duration<double, std::milli>(t_end - t_start).count());
    render_export_session_end(&session);
    return 0;
}

// Main function to layout HTML and render to JPEG
// output_scale is export density; device_scale is platform pixel density.
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width, int viewport_height, float output_scale, float device_scale) {
    RenderExportSession session;
    if (!render_export_session_begin_raster(&session, html_file,
            viewport_width, viewport_height, output_scale, device_scale)) {
        return 1;
    }
    UiContext* ui_context = session.ui_context;
    DomDocument* doc = session.document;

    if (doc && doc->view_tree) {
        RenderOutputTarget target;
        render_output_target_init(&target, RENDER_OUTPUT_JPEG, jpeg_file);
        target.surface = ui_context->surface;
        target.jpeg_quality = quality;
        render_output_target_apply_session(&target, &session);
        render_output_render_view_tree_to_target(ui_context, doc->view_tree, &target);
    } else {
        render_export_session_end(&session);
        return 1;
    }

    render_export_session_end(&session);
    return 0;
}

/**
 * Render an existing UiContext with its current state to a PNG file.
 * This is used by event simulation to capture the current view with caret/selection.
 *
 * @param uicon The UI context with document and state already set up
 * @param png_file Path to output PNG file
 * @return 0 on success, 1 on failure
 */
int render_uicontext_to_png(UiContext* uicon, const char* png_file) {
    if (!uicon || !uicon->document || !uicon->document->view_tree) {
        log_error("render_uicontext_to_png: invalid uicontext or no view tree");
        return 1;
    }

    log_info("render_uicontext_to_png: rendering to %s", png_file);

    // Render the document (this will include caret/selection via render_ui_overlays)
    RenderOutputTarget target;
    render_output_target_init(&target, RENDER_OUTPUT_PNG, png_file);
    target.surface = uicon->surface;
    render_output_render_view_tree_to_target(uicon, uicon->document->view_tree, &target);

    log_info("render_uicontext_to_png: completed successfully");
    return 0;
}

/**
 * Render an existing UiContext with its current state to an SVG file.
 * This is used by event simulation to capture the current view with caret/selection.
 *
 * Note: SVG rendering currently doesn't include caret/selection overlays.
 *       This function renders the view tree only.
 *
 * @param uicon The UI context with document and state already set up
 * @param svg_file Path to output SVG file
 * @return 0 on success, 1 on failure
 */
int render_uicontext_to_svg(UiContext* uicon, const char* svg_file) {
    if (!uicon || !uicon->document || !uicon->document->view_tree) {
        log_error("render_uicontext_to_svg: invalid uicontext or no view tree");
        return 1;
    }

    log_info("render_uicontext_to_svg: rendering to %s", svg_file);

    // Get content dimensions
    int content_max_x = uicon->viewport_width;
    int content_max_y = uicon->viewport_height;

    calculate_content_bounds(uicon->document->view_tree->root, &content_max_x, &content_max_y);
    content_max_x += 50;
    content_max_y += 50;

    // Render to SVG (now includes caret if present)
    char* svg_content = render_view_tree_to_svg(uicon, uicon->document->view_tree->root,
                                                content_max_x, content_max_y,
                                                uicon->document->state);
    if (!svg_content) {
        log_error("render_uicontext_to_svg: failed to render view tree");
        return 1;
    }

    if (!save_svg_to_file(svg_content, svg_file)) {
        log_error("render_uicontext_to_svg: failed to save SVG to %s", svg_file);
        mem_free(svg_content);
        return 1;
    }

    mem_free(svg_content);
    log_info("render_uicontext_to_svg: completed successfully");
    return 0;
}

// ─── Batch render command ────────────────────────────────────────────────────
//
// Reads render jobs from stdin (one per line, tab-separated):
//   <html_file>\t<output_png>\t<viewport_width>\t<viewport_height>\t<device_scale>
//
// Initializes UiContext ONCE and reuses it across all renders, saving ~70MB of
// per-process overhead (GLFW/Metal driver, font database scan, ThorVG, native font backend).
//
// Writes results to stdout (one per line):
//   OK\t<html_file>
//   FAIL\t<html_file>\t<reason>

static void render_batch_cleanup_doc(UiContext* ui_context, DomDocument* doc) {
    if (doc) {
        script_runner_cleanup_js_state(doc);
        if (doc->view_tree) {
            view_pool_destroy(doc->view_tree);
            mem_free(doc->view_tree);
            doc->view_tree = nullptr;
        }
        dom_document_destroy(doc);
    }

    js_batch_reset();
    dom_batch_reset();
    js_globals_batch_reset();
    script_runner_cleanup_heap();

    font_context_reset_document_fonts(ui_context->font_ctx);
    font_context_reset_glyph_caches(ui_context->font_ctx);
    ui_context->font_face_count = 0;

    image_cache_cleanup(ui_context);
    InputManager::destroy_global();
    ui_context->document = nullptr;
}

static bool render_batch_single(
    UiContext* ui_context,
    const char* html_file,
    const char* png_file,
    int viewport_width,
    int viewport_height,
    float device_scale,
    Url* cwd
) {
    float output_scale = 1.0f;
    float raster_scale = output_scale * device_scale;

    int layout_width = viewport_width > 0 ? viewport_width : 100;
    int layout_height = viewport_height > 0 ? viewport_height : 100;

    // update ui_context dimensions for this render
    ui_context_set_device_scale(ui_context, device_scale, device_scale);
    int surface_width = (int)(layout_width * raster_scale);
    int surface_height = (int)(layout_height * raster_scale);
    ui_context_create_surface(ui_context, surface_width, surface_height);
    ui_context->window_width = surface_width;
    ui_context->window_height = surface_height;
    ui_context->viewport_width = layout_width;
    ui_context->viewport_height = layout_height;

    DomDocument* doc = load_html_doc(cwd, (char*)html_file, layout_width, layout_height);
    if (!doc) {
        log_error("render-batch: failed to load %s", html_file);
        render_batch_cleanup_doc(ui_context, nullptr);
        return false;
    }

    ui_context->document = doc;
    doc->viewport.output_scale = output_scale;
    ui_context_sync_document_raster_scale(ui_context, doc);

    process_document_font_faces(ui_context, doc);

    if (doc->root) {
        layout_html_doc(ui_context, doc, false);
    }

    int output_width = surface_width;
    int output_height = surface_height;

    bool rendered = false;
    static const int64_t PNG_TILE_THRESHOLD = (int64_t)32 * 1024 * 1024;

    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);
    int content_max_x = 0, content_max_y = 0;
    if (render_png_resolve_auto_size(doc, raster_scale, auto_width, auto_height,
            &output_width, &output_height, &content_max_x, &content_max_y)) {
        if ((int64_t)output_width * output_height > PNG_TILE_THRESHOLD) {
            render_html_doc_tiled(ui_context, doc->view_tree, png_file,
                output_width, output_height);
            rendered = true;
        } else {
            ui_context_create_surface(ui_context, output_width, output_height);
        }
    }

    if (!rendered) {
        if (doc->view_tree) {
            render_html_doc(ui_context, doc->view_tree, png_file);
        } else {
            log_error("render-batch: no view tree for %s", html_file);
            render_batch_cleanup_doc(ui_context, doc);
            return false;
        }
    }

    render_batch_cleanup_doc(ui_context, doc);
    return true;
}

int cmd_render_batch(int argc, char** argv) {
    // --pixel-ratio is the legacy CLI spelling for device scale.
    float default_device_scale = 1.0f;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--pixel-ratio") == 0 && i + 1 < argc) {
            default_device_scale = (float)atof(argv[i + 1]);
            if (default_device_scale <= 0) default_device_scale = 1.0f;
        }
    }

    // initialize UI context once
    UiContext ui_context;
    memset(&ui_context, 0, sizeof(UiContext));
    if (ui_context_init(&ui_context, true, default_device_scale) != 0) {
        fprintf(stderr, "FAIL\t(init)\tFailed to initialize UI context\n");
        return 1;
    }

    Url* cwd = get_current_dir();
    if (!cwd) {
        fprintf(stderr, "FAIL\t(init)\tFailed to get current directory\n");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // read jobs from stdin, one per line
    char line[4096];
    int success_count = 0;
    int failure_count = 0;

    // optional periodic memory report: set RENDER_BATCH_MEM_REPORT=N to log
    // memtrack usage every N successful renders. helps detect per-render
    // memory growth across batched renders.
    int mem_report_every = 0;
    const char* mem_report_env = getenv("RENDER_BATCH_MEM_REPORT");
    if (mem_report_env && *mem_report_env) {
        mem_report_every = atoi(mem_report_env);
        if (mem_report_every < 0) mem_report_every = 0;
    }
    if (mem_report_every > 0) {
        log_info("render-batch: initial memory state");
        memtrack_log_usage();
    }

    // optional per-render peak RSS report: set RENDER_BATCH_RSS_REPORT=1 to
    // log getrusage().ru_maxrss before/after each render. since ru_maxrss is
    // monotonic (high-water mark), the delta tells us whether THIS render
    // pushed peak RSS higher — useful for spotting individual heavy jobs.
    bool rss_report = false;
    const char* rss_report_env = getenv("RENDER_BATCH_RSS_REPORT");
    if (rss_report_env && *rss_report_env && strcmp(rss_report_env, "0") != 0) {
        rss_report = true;
    }
    size_t prev_peak_rss = 0;
    if (rss_report) {
        struct rusage ru0;
        if (getrusage(RUSAGE_SELF, &ru0) == 0) {
            prev_peak_rss = RUSAGE_MAXRSS_TO_BYTES(ru0.ru_maxrss);
            fprintf(stderr, "RSS\tinitial\t%zu\n", prev_peak_rss);
        }
    }

    while (fgets(line, sizeof(line), stdin)) {
        // strip trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        // parse tab-separated fields: html_file\toutput_png\tvw\tvh[\tdevice_scale]
        char* html_file = line;
        char* tab1 = strchr(html_file, '\t');
        if (!tab1) {
            fprintf(stdout, "FAIL\t%s\tmalformed input line\n", html_file);
            fflush(stdout);
            failure_count++;
            continue;
        }
        *tab1 = '\0';
        char* output_png = tab1 + 1;

        char* tab2 = strchr(output_png, '\t');
        if (!tab2) {
            fprintf(stdout, "FAIL\t%s\tmalformed input line\n", html_file);
            fflush(stdout);
            failure_count++;
            continue;
        }
        *tab2 = '\0';
        int vw = atoi(tab2 + 1);

        char* tab3 = strchr(tab2 + 1, '\t');
        int vh = 0;
        float device_scale = default_device_scale;
        if (tab3) {
            *tab3 = '\0';
            vw = atoi(tab2 + 1);
            vh = atoi(tab3 + 1);

            char* tab4 = strchr(tab3 + 1, '\t');
            if (tab4) {
                *tab4 = '\0';
                vh = atoi(tab3 + 1);
                device_scale = (float)atof(tab4 + 1);
                if (device_scale <= 0) device_scale = default_device_scale;
            }
        }

        bool ok = render_batch_single(&ui_context, html_file, output_png, vw, vh, device_scale, cwd);
        if (ok) {
            fprintf(stdout, "OK\t%s\n", html_file);
            success_count++;
        } else {
            fprintf(stdout, "FAIL\t%s\trender error\n", html_file);
            failure_count++;
        }
        fflush(stdout);

        if (rss_report) {
            struct rusage ru1;
            if (getrusage(RUSAGE_SELF, &ru1) == 0) {
                size_t cur_peak = RUSAGE_MAXRSS_TO_BYTES(ru1.ru_maxrss);
                long delta = (long)cur_peak - (long)prev_peak_rss;
                // emit one line per render: name, peak_rss_after, delta_bytes
                // delta > 0 means THIS render pushed the high-water mark up.
                fprintf(stderr, "RSS\t%s\tpeak=%zu\tdelta=%+ld\n",
                        html_file, cur_peak, delta);
                prev_peak_rss = cur_peak;
            }
        }

        if (mem_report_every > 0 &&
            ((success_count + failure_count) % mem_report_every == 0)) {
            log_info("render-batch: memory after %d renders (%d ok, %d fail)",
                     success_count + failure_count, success_count, failure_count);
            memtrack_log_usage();
        }
    }

    if (mem_report_every > 0) {
        log_info("render-batch: final memory state before cleanup");
        memtrack_log_usage();
    }

    ui_context_cleanup(&ui_context);
    return failure_count > 0 ? 1 : 0;
}
