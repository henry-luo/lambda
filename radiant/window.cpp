#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/file.h"
#include "../lib/shell.h"
#include "../lib/uv_loop.h"
#include "layout.hpp"
#include "view.hpp"
#include "event.hpp"
#include "event_sim.hpp"
#include "render.hpp"
#include "radiant.hpp"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/network_integration.h"
#include "../lambda/network/network_thread_pool.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_downloader.h"
#include "../lambda/input/input.hpp"
#include "../lambda/js/js_event_loop.h"
#include "../lambda/js/js_dom.h"
#include "../lambda/js/js_dom_observers.h"
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/transpiler.hpp"
extern "C" {
#include "../lib/url.h"
}

extern "C" void js_batch_reset(void);
extern "C" void js_dom_batch_reset(void);
extern "C" void js_globals_batch_reset(void);
extern "C" void js_dom_set_ui_context(void* ui_context);
extern "C" void js_dom_set_host_driven_loop(bool enabled);
extern __thread EvalContext* context;
extern __thread Context* input_context;
extern "C" Context* _lambda_rt;
extern UiContext ui_context;

bool radiant_pump_js_event_loop(UiContext* uicon, int wait_ms) {
    DomDocument* doc = uicon ? uicon->document : nullptr;
    if (!doc || !doc->js.runtime_heap || !doc->js.runtime_name_pool) {
        if (wait_ms >= 0) return js_event_loop_pump_wait(wait_ms);
        js_event_loop_pump_nowait();
        return true;
    }

    Heap* heap = (Heap*)doc->js.runtime_heap;
    EvalContext pump_ctx = {};
    pump_ctx.heap = heap;
    pump_ctx.name_pool = (NamePool*)doc->js.runtime_name_pool;
    pump_ctx.type_list = (ArrayList*)doc->js.runtime_type_list;
    pump_ctx.pool = doc->js.runtime_pool ? (Pool*)doc->js.runtime_pool : heap->pool;

    EvalContext* saved_ctx = context;
    Context* saved_input_ctx = input_context;
    Context* saved_lambda_rt = _lambda_rt;
    // Promise and timer callbacks allocate during the host pump just like event
    // listeners do; use the retained document heap instead of the loader's
    // already-restored context, which may be null or belong to another batch.
    context = &pump_ctx;
    input_context = nullptr;
    _lambda_rt = (Context*)&pump_ctx;
    js_dom_set_document(doc);
    js_dom_observers_post_layout();
    bool pumped = true;
    if (wait_ms >= 0) pumped = js_event_loop_pump_wait(wait_ms);
    else js_event_loop_pump_nowait();
    if (uicon) radiant_reconcile_js_dom_mutations(uicon, doc);
    context = saved_ctx;
    input_context = saved_input_ctx;
    _lambda_rt = saved_lambda_rt;
    return pumped;
}

#ifdef __APPLE__
#include <mach/mach.h>
// Sample current and peak phys_footprint, log under tag MEMSTAGE if VIEW_MEM_STAGES=1.
extern "C" void log_mem_stage(const char* stage) {
    static int env_checked = 0;
    static int enabled = 0;
    if (!env_checked) {
        const char* e = getenv("VIEW_MEM_STAGES");
        enabled = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
        env_checked = 1;
    }
    if (!enabled) return;
    task_vm_info_data_t info;
    mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &cnt) != KERN_SUCCESS) {
        return;
    }
    fprintf(stderr, "[MEMSTAGE] %-28s footprint=%6lluMB peak=%6lluMB resident=%6lluMB\n", // PRINTF_OK: env-gated VIEW_MEM_STAGES dev profiler.
            stage,
            (unsigned long long)(info.phys_footprint / (1024 * 1024)),
            (unsigned long long)(info.ledger_phys_footprint_peak / (1024 * 1024)),
            (unsigned long long)(info.resident_size / (1024 * 1024)));
}
#else
extern "C" void log_mem_stage(const char*) {}
#endif

void render(GLFWwindow* window);
// load_html_doc is declared in view.hpp (via layout.hpp)
DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_lambda_script_source_doc(Url* script_url, const char* script_source,
                                           int viewport_width, int viewport_height, Pool* pool);
DomDocument* load_svg_doc(Url* svg_url, int viewport_width, int viewport_height, Pool* pool, float pixel_ratio);
void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);
bool radiant_editing_animation_active(DocState* state);
bool radiant_editing_animation_tick(UiContext* uicon, double timestamp);

static void view_wake_glfw(void* user_data) {
    (void)user_data;
    glfwPostEmptyEvent();
}

static void network_wake_glfw(void* user_data) {
    view_wake_glfw(user_data);
}

static void view_cleanup_js_batch_state(void) {
    if (!script_runner_js_batch_cleanup_unsafe()) {
        js_event_loop_shutdown();
        // js_batch_reset owns the DOM reset; repeating it releases Range wrappers twice.
        js_batch_reset();
        js_globals_batch_reset();
        script_runner_cleanup_heap();
    }
}

static void view_cleanup_input_manager(void) {
    InputManager::destroy_global();
}

static double view_min_timeout(double current, double candidate) {
    if (candidate < 0.0) return current;
    if (current < 0.0 || candidate < current) return candidate;
    return current;
}

static double view_uv_timeout_seconds(uv_loop_t* uv_loop, double active_fallback) {
    if (!uv_loop || !uv_loop_alive(uv_loop)) return -1.0;

    int timeout_ms = uv_backend_timeout(uv_loop);
    if (timeout_ms < 0) return active_fallback;
    if (timeout_ms == 0) return 0.0;

    double timeout = (double)timeout_ms / 1000.0;
    if (timeout > 1.0) return 1.0;
    return timeout;
}

static double view_next_wait_timeout(RadiantFrameClock* frame_clock, double now,
                                     bool frame_driven, bool caret_visible,
                                     double caret_blink_elapsed,
                                     double caret_blink_interval,
                                     bool editing_animation_active,
                                     EventSimContext* sim_ctx,
                                     double sim_start_time,
                                     uv_loop_t* uv_loop) {
    double timeout = radiant_frame_clock_next_timeout(frame_clock, now, frame_driven, false);
    double frame_interval = frame_clock && frame_clock->initialized ?
        frame_clock->refresh_interval : (1.0 / 60.0);

    if (caret_visible) {
        double caret_timeout = caret_blink_interval - caret_blink_elapsed;
        if (caret_timeout < 0.0) caret_timeout = 0.0;
        timeout = view_min_timeout(timeout, caret_timeout);
    }
    if (editing_animation_active) {
        timeout = view_min_timeout(timeout, frame_interval);
    }

    if (sim_ctx && sim_ctx->is_running) {
        double sim_timeout = now < sim_start_time ? sim_start_time - now : frame_interval;
        timeout = view_min_timeout(timeout, sim_timeout);
    }

    timeout = view_min_timeout(timeout, view_uv_timeout_seconds(uv_loop, frame_interval));
    if (timeout < 0.0) return 0.0;
    if (timeout > 1.0) return 1.0;
    return timeout;
}

// Document format detection
typedef enum {
    DOC_FORMAT_UNKNOWN,
    DOC_FORMAT_HTML,
    DOC_FORMAT_MARKDOWN,
    DOC_FORMAT_LATEX,
    DOC_FORMAT_XML,
    DOC_FORMAT_RST,
    DOC_FORMAT_WIKI,
    DOC_FORMAT_LAMBDA_SCRIPT,
    DOC_FORMAT_PDF,
    DOC_FORMAT_SVG,
    DOC_FORMAT_IMAGE,  // PNG, JPG, JPEG, GIF
    DOC_FORMAT_TEXT    // JSON, YAML, TOML, TXT, etc.
} DocFormat;

// Detect document format from file extension
static DocFormat detect_doc_format(const char* filename) {
    if (!filename) return DOC_FORMAT_UNKNOWN;

    size_t ext_len = 0;
    const char* ext = file_path_ext_len(filename, strlen(filename), &ext_len);
    if (!ext) return DOC_FORMAT_UNKNOWN;
    ext++; // skip the '.'
    ext_len--;

    if (str_ieq_const(ext, ext_len, "html") || str_ieq_const(ext, ext_len, "htm")) {
        return DOC_FORMAT_HTML;
    } else if (str_ieq_const(ext, ext_len, "md") || str_ieq_const(ext, ext_len, "markdown")) {
        return DOC_FORMAT_MARKDOWN;
    } else if (str_ieq_const(ext, ext_len, "tex") || str_ieq_const(ext, ext_len, "latex")) {
        return DOC_FORMAT_LATEX;
    } else if (str_ieq_const(ext, ext_len, "xml")) {
        return DOC_FORMAT_XML;
    } else if (str_ieq_const(ext, ext_len, "rst")) {
        return DOC_FORMAT_RST;
    } else if (str_ieq_const(ext, ext_len, "wiki")) {
        return DOC_FORMAT_WIKI;
    } else if (str_ieq_const(ext, ext_len, "ls")) {
        return DOC_FORMAT_LAMBDA_SCRIPT;
    } else if (str_ieq_const(ext, ext_len, "pdf")) {
        return DOC_FORMAT_PDF;
    } else if (str_ieq_const(ext, ext_len, "svg")) {
        return DOC_FORMAT_SVG;
    } else if (str_ieq_const(ext, ext_len, "png") || str_ieq_const(ext, ext_len, "jpg") ||
               str_ieq_const(ext, ext_len, "jpeg") || str_ieq_const(ext, ext_len, "gif")) {
        return DOC_FORMAT_IMAGE;
    } else if (str_ieq_const(ext, ext_len, "json") || str_ieq_const(ext, ext_len, "yaml") ||
               str_ieq_const(ext, ext_len, "yml") || str_ieq_const(ext, ext_len, "toml") ||
               str_ieq_const(ext, ext_len, "txt") || str_ieq_const(ext, ext_len, "csv") ||
               str_ieq_const(ext, ext_len, "ini") || str_ieq_const(ext, ext_len, "conf") ||
               str_ieq_const(ext, ext_len, "cfg") || str_ieq_const(ext, ext_len, "log")) {
        return DOC_FORMAT_TEXT;
    }

    return DOC_FORMAT_UNKNOWN;
}

// Load document based on detected format
static DomDocument* load_doc_by_format(const char* filename, Url* base_url, int width, int height, Pool* pool) {
    // For HTTP/HTTPS URLs, always route to HTML loader regardless of extension
    if (strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0) {
        log_debug("Loading as remote HTML document (HTTP/HTTPS)");
        return load_html_doc(base_url, (char*)filename, width, height);
    }

    DocFormat format = detect_doc_format(filename);

    switch (format) {
        case DOC_FORMAT_HTML:
            log_debug("Loading as HTML document");
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_MARKDOWN: {
            log_debug("Loading as Markdown document");
            Url* doc_url = url_parse_with_base(filename, base_url);
            if (!doc_url) {
                log_error("Failed to parse document URL: %s", filename);
                return NULL;
            }
            DomDocument* doc = load_markdown_doc(doc_url, width, height, pool);
            return doc;
        }

        case DOC_FORMAT_LATEX: {
            log_debug("Loading as LaTeX document");
            // Use HTML conversion pipeline (LaTeX→HTML)
            log_info("Using LaTeX→HTML pipeline for LaTeX");
            return load_html_doc(base_url, (char*)filename, width, height);
        }

        case DOC_FORMAT_XML:
            log_debug("Loading as XML document with CSS stylesheet");
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_RST:
            log_warn("RST format not yet implemented");
            return NULL;

        case DOC_FORMAT_LAMBDA_SCRIPT:
            log_debug("Loading as Lambda script document");
            // load_html_doc will detect .ls extension and route to load_lambda_script_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_WIKI: {
            log_debug("Loading as Wiki document");
            Url* doc_url = url_parse_with_base(filename, base_url);
            if (!doc_url) {
                log_error("Failed to parse document URL: %s", filename);
                return NULL;
            }
            DomDocument* doc = load_wiki_doc(doc_url, width, height, pool);
            return doc;
        }

        case DOC_FORMAT_PDF:
            log_debug("Loading as PDF document");
            // load_html_doc will detect .pdf extension and route to load_pdf_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_SVG:
            log_debug("Loading as SVG document");
            // load_html_doc will detect .svg extension and route to load_svg_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_IMAGE:
            log_debug("Loading as image document");
            // load_html_doc will detect image extensions and route to load_image_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        case DOC_FORMAT_TEXT:
            log_debug("Loading as text document (source view)");
            // load_html_doc will detect text extensions and route to load_text_doc
            return load_html_doc(base_url, (char*)filename, width, height);

        default:
            log_error("Unsupported document format for file: %s", filename);
            log_error("Supported formats: .html, .htm, .md, .markdown, .tex, .latex, .ls, .xml, .pdf, .svg, .png, .jpg, .jpeg, .gif, .json, .yaml, .yml, .toml, .txt, .csv, .ini, .conf, .cfg, .log");
            return NULL;
    }
}

// Get human-readable format name for window title
static const char* get_format_name(const char* filename) {
    DocFormat format = detect_doc_format(filename);
    switch (format) {
        case DOC_FORMAT_HTML: return "HTML";
        case DOC_FORMAT_MARKDOWN: return "Markdown";
        case DOC_FORMAT_LATEX: return "LaTeX";
        case DOC_FORMAT_XML: return "XML";
        case DOC_FORMAT_RST: return "RST";
        case DOC_FORMAT_WIKI: return "Wiki";
        case DOC_FORMAT_LAMBDA_SCRIPT: return "Lambda Script";
        case DOC_FORMAT_PDF: return "PDF";
        case DOC_FORMAT_SVG: return "SVG";
        case DOC_FORMAT_IMAGE: return "Image";
        case DOC_FORMAT_TEXT: return "Text";
        default: return "Document";
    }
}

// Window dimensions
bool do_redraw = false;
UiContext ui_context;

static void view_close_event_log() {
    if (!ui_context.event_log) return;
    event_state_log_document(ui_context.event_log, "unload_start");
    event_state_log_document(ui_context.event_log, "unload_complete");
    event_state_log_close(ui_context.event_log);
    ui_context.event_log = nullptr;
}

static void view_close_state_dump() {
    if (!ui_context.state_dump_log) return;
    radiant_state_dump_close(ui_context.state_dump_log);
    ui_context.state_dump_log = nullptr;
}

static void view_attach_event_log(DomDocument* doc, const char* doc_name) {
    if ((!ui_context.event_log_enabled && !ui_context.state_dump_enabled) || !doc) return;

    view_close_event_log();
    view_close_state_dump();

    const char* href = doc->url ? url_get_href(doc->url) : doc_name;
    if (ui_context.event_log_enabled) {
        ui_context.event_log = event_state_log_open(doc_name ? doc_name : "document", href);
        if (ui_context.event_log) {
            event_state_log_session_start(ui_context.event_log,
                (int)ui_context.viewport_width, (int)ui_context.viewport_height,
                doc->viewport.scale > 0 ? doc->viewport.scale : 1.0);
            event_state_log_document(ui_context.event_log, "load_start");
            event_state_log_document(ui_context.event_log, "load_complete");
        }
    }
    if (ui_context.state_dump_enabled) {
        ui_context.state_dump_log = radiant_state_dump_open(doc_name ? doc_name : "document");
    }

    radiant_document_ensure_state(doc, "view_attach_event_log");
    if (doc->state) {
        doc->state->active_event_log = ui_context.event_log;
        radiant_state_set_dump_log(doc->state, ui_context.state_dump_log);
    }
}

// update the GLFW window title (safe to call from event handlers)
void update_window_title(const char* title) {
    if (!ui_context.window || !title) return;
    glfwSetWindowTitle(ui_context.window, title);
}

DomDocument* show_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height) {
    log_debug("Showing HTML document %s", doc_url);
    DomDocument* doc = load_html_doc(base, doc_url, viewport_width, viewport_height);
    if (!doc) return nullptr;

    // Set scale for window display: given_scale = 1.0, scale = pixel_ratio
    doc->viewport.given_scale = 1.0f;
    doc->viewport.scale = doc->viewport.given_scale * ui_context.pixel_ratio;

    // BrowsingSession owns replacement of the previously presented document;
    // this presentation helper only publishes the newly loaded document.
    ui_context.document = doc;

    radiant_document_ensure_state(doc, "show_html_doc");
    view_attach_event_log(doc, doc_url);

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    // layout html doc
    if (doc->root) {
        layout_html_doc(&ui_context, doc, false);
    }
    // render html doc
    if (doc && doc->view_tree) {
        log_debug("html version: %d", doc->view_tree->html_version);
        render_html_doc(&ui_context, doc->view_tree, NULL);
    }
    return doc;
}

void reflow_html_doc(DomDocument* doc) {
    if (!doc || !doc->root) {
        log_debug("No document to reflow");
        return;
    }
    layout_html_doc(&ui_context, doc, true);
    // Skip render here — let the main loop handle it via render().
    // Mark dirty so the main loop knows to repaint.
    if (doc->state) {
        DocState* state = (DocState*)doc->state;
        doc_state_mark_dirty(state);
    }
}

static void window_save_document_scroll(BrowsingSession* session, DomDocument* doc) {
    ViewBlock* root = doc && doc->view_tree
        ? lam::view_require_block(doc->view_tree->root) : nullptr;
    if (!root || !root->scroller || !root->scroll()->pane) return;

    float scroll_y = 0.0f;
    scroll_state_get_position_for_view(doc->state, static_cast<View*>(root),
        root->scroll()->pane, NULL, &scroll_y, NULL, NULL);
    session_save_scroll_position(session, scroll_y);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    // Alt+Left = browser back, Alt+Right = browser forward
    if (action == GLFW_PRESS && (mods & GLFW_MOD_ALT)) {
        BrowsingSession* session = ui_context.browsing_session;
        if (session && ui_context.document) {
            int css_vw = ui_context.viewport_width;
            int css_vh = ui_context.viewport_height;
            DomDocument* new_doc = nullptr;
            if (key == GLFW_KEY_LEFT && session_can_go_back(session)) {
                log_info("browse_nav: keyboard back");
                window_save_document_scroll(session, ui_context.document);
                new_doc = session_go_back(session, &ui_context, css_vw, css_vh);
            } else if (key == GLFW_KEY_RIGHT && session_can_go_forward(session)) {
                log_info("browse_nav: keyboard forward");
                window_save_document_scroll(session, ui_context.document);
                new_doc = session_go_forward(session, &ui_context, css_vw, css_vh);
            }
            if (new_doc) {
                // restore saved scroll position for this history entry
                ViewBlock* root = new_doc->view_tree ? lam::view_require_block(new_doc->view_tree->root) : nullptr;
                if (root && root->scroller && root->scroll_mut()->pane) {
                    float saved_y = session_get_scroll_position(session);
                    float scroll_x = 0.0f;
                    scroll_state_get_position_for_view((DocState*)new_doc->state, static_cast<View*>(root),
                        root->scroll()->pane, &scroll_x, NULL, NULL, NULL);
                    scroll_state_set_position_for_view((DocState*)new_doc->state, static_cast<View*>(root),
                        root->scroll()->pane, scroll_x, saved_y, true);
                }
                // update title
                const char* page_title = session_current_title(session);
                if (!page_title) page_title = session_extract_title(new_doc);
                if (page_title) {
                    char title_buf[512];
                    snprintf(title_buf, sizeof(title_buf), "Lambda - %s", page_title);
                    update_window_title(title_buf);
                }
                do_redraw = 1;
                return;
            }
        }
    }

    // Build keyboard event
    RdtEvent event;
    event.key.type = (action == GLFW_PRESS || action == GLFW_REPEAT) ? RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP;
    event.key.timestamp = glfwGetTime();
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.mods = 0;
    if (mods & GLFW_MOD_SHIFT) event.key.mods |= RDT_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) event.key.mods |= RDT_MOD_CTRL;
    if (mods & GLFW_MOD_ALT) event.key.mods |= RDT_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) event.key.mods |= RDT_MOD_SUPER;

    // Handle key events
    handle_event(&ui_context, ui_context.document, &event);
    // Always repaint after a key event so caret motion, character
    // insertion/deletion, selection changes, and context-menu open/close
    // become visible immediately.
    do_redraw = 1;
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
    // Build text input event
    RdtEvent event;
    event.text_input.type = RDT_EVENT_TEXT_INPUT;
    event.text_input.timestamp = glfwGetTime();
    event.text_input.codepoint = codepoint;

    if (codepoint > 127) {
        log_debug("Unicode character entered: U+%04X", codepoint);
    } else {
        log_debug("Character entered: %u '%c'", codepoint, codepoint);
    }

    handle_event(&ui_context, ui_context.document, &event);
    // Repaint so the typed character appears without waiting for the
    // next mouse move / animation tick.
    do_redraw = 1;
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    RdtEvent event;
    log_debug("Cursor position: (%.1f, %.1f)", xpos, ypos);
    event.mouse_position.type = RDT_EVENT_MOUSE_MOVE;
    event.mouse_position.timestamp = glfwGetTime();
    // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
    event.mouse_position.x = xpos;
    event.mouse_position.y = ypos;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);

    // Trigger redraw so any pending reflows/repaints from hover state
    // changes get processed promptly. Without this, reflow requests
    // accumulate without being cleared, causing O(n^2) list traversal.
    DocState* mstate = ui_context.document ? ui_context.document->state : nullptr;
    if (mstate && (mstate->needs_reflow || mstate->needs_repaint || mstate->is_dirty)) {
        do_redraw = 1;
    }
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    log_info("MOUSE_BUTTON_CALLBACK: button=%d action=%d mods=%d", button, action, mods);
    RdtEvent event;
    event.mouse_button.type = action == GLFW_PRESS ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
    event.mouse_button.timestamp = glfwGetTime();
    event.mouse_button.button = button;

    // Get cursor position for all mouse button events
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    event.mouse_button.x = xpos;
    event.mouse_button.y = ypos;

    static double last_click_time = -1.0;
    static double last_click_x = 0.0;
    static double last_click_y = 0.0;
    static int last_click_button = -1;
    static uint8_t click_count = 1;
    static uint8_t active_click_count = 1;
    if (action == GLFW_PRESS) {
        double dx = xpos - last_click_x;
        double dy = ypos - last_click_y;
        bool same_click_series = last_click_time >= 0.0 &&
            button == last_click_button &&
            event.mouse_button.timestamp - last_click_time <= 0.5 &&
            dx * dx + dy * dy <= 16.0;
        click_count = same_click_series ? (uint8_t)(click_count + 1) : 1;
        if (click_count > 3) click_count = 3;
        last_click_time = event.mouse_button.timestamp;
        last_click_x = xpos;
        last_click_y = ypos;
        last_click_button = button;
        active_click_count = click_count;
    } else {
        click_count = active_click_count;
    }
    event.mouse_button.clicks = click_count;

    // Map GLFW modifiers to RDT modifiers
    event.mouse_button.mods = 0;
    if (mods & GLFW_MOD_SHIFT) event.mouse_button.mods |= RDT_MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) event.mouse_button.mods |= RDT_MOD_CTRL;
    if (mods & GLFW_MOD_ALT) event.mouse_button.mods |= RDT_MOD_ALT;
    if (mods & GLFW_MOD_SUPER) event.mouse_button.mods |= RDT_MOD_SUPER;

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        log_debug("Right mouse button pressed");
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        log_debug("Right mouse button released");
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        log_debug("Left mouse button pressed at (%.2f, %.2f)", xpos, ypos);
        ui_context.mouse_state.is_mouse_down = 1;
        // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
        ui_context.mouse_state.down_x = xpos;
        ui_context.mouse_state.down_y = ypos;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        log_debug("Left mouse button released");

    // Forward both LEFT and RIGHT mouse buttons. RIGHT is required for
    // the native context menu (event.cpp opens it on RDT_EVENT_MOUSE_DOWN
    // when btn_event->button == GLFW_MOUSE_BUTTON_RIGHT). MIDDLE is
    // unused so we skip it.
    if (button == GLFW_MOUSE_BUTTON_LEFT ||
        button == GLFW_MOUSE_BUTTON_RIGHT) {
        handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
        do_redraw = 1;  // trigger repaint after mouse click
    }
}

// handles mouse/touchpad scroll input
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    RdtEvent event;
    log_debug("Scroll_callback");
    log_enter();
    event.scroll.type = RDT_EVENT_SCROLL;
    event.scroll.timestamp = glfwGetTime();
    // Scroll offset can stay as-is (relative motion)
    event.scroll.xoffset = xoffset;
    event.scroll.yoffset = yoffset;
    log_debug("Scroll offset: (%.1f, %.1f)", xoffset, yoffset);
    assert(xoffset != 0 || yoffset != 0);
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    log_debug("Mouse position: (%.1f, %.1f)", xpos, ypos);
    // GLFW returns logical (CSS) pixels, which matches our layout coordinate system
    event.scroll.x = xpos;
    event.scroll.y = ypos;
    handle_event(&ui_context, ui_context.document, (RdtEvent*)&event);
    do_redraw = 1;
    log_leave();
}

// Callback function to handle window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    log_debug("Window resized to: %d x %d", width, height);
    do_redraw = 1;
}

void window_refresh_callback(GLFWwindow *window) {
    render(window);
    do_redraw = 0;
}

void to_repaint() {
    log_debug("Requesting repaint");
    do_redraw = 1;
}

void repaint_window() {
    if (!ui_context.surface || !ui_context.surface->pixels) {
        log_error("repaint_window: surface or pixels is NULL");
        return;
    }

    int framebuffer_width = 0;
    int framebuffer_height = 0;
    if (ui_context.window) {
        glfwGetFramebufferSize(ui_context.window, &framebuffer_width, &framebuffer_height);
    }
    bool exact_framebuffer_present =
        framebuffer_width == ui_context.surface->width &&
        framebuffer_height == ui_context.surface->height;

    // generate a texture from the bitmap
    log_debug("creating rendering texture");
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ui_context.surface->width, ui_context.surface->height, 0,
        GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ui_context.surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint present_filter = exact_framebuffer_present ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, present_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, present_filter);

    // render the texture as a quad
    log_debug("rendering texture");
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1, -1);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1, -1);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1, 1);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // cleanup
    glDeleteTextures(1, &texture);
}

void render(GLFWwindow* window) {
    // get window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // set up OpenGL viewport and projection for 2D rendering
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);  // normalized device coordinates for quad rendering
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // clear the framebuffer
    glClear(GL_COLOR_BUFFER_BIT);

    // NETWORK INTEGRATION: Process network callbacks (MAIN THREAD ONLY)
    // This handles resource completions and triggers reflows/repaints for loaded resources
    if (ui_context.document && ui_context.document->resource_manager) {
        resource_manager_flush_layout_updates(ui_context.document->resource_manager);

        // Detect fully loaded state and trigger final stabilization reflow
        if (!ui_context.document->fully_loaded &&
            resource_manager_is_fully_loaded(ui_context.document->resource_manager)) {
            ui_context.document->fully_loaded = true;
            log_info("view: all network resources loaded, triggering final reflow");
            reflow_html_doc(ui_context.document);

            // restore window title after load completes
            if (ui_context.document->url) {
                // prefer page <title> tag over URL
                const char* page_title = session_extract_title(ui_context.document);
                char title[512];
                if (page_title) {
                    snprintf(title, sizeof(title), "Lambda - %s", page_title);
                    // update session title now that doc is fully loaded
                    if (ui_context.browsing_session) {
                        session_set_current_title(ui_context.browsing_session, page_title);
                    }
                } else {
                    const char* doc_href = url_get_href(ui_context.document->url);
                    snprintf(title, sizeof(title), "Lambda - %s", doc_href ? doc_href : "");
                }
                glfwSetWindowTitle(window, title);
            }
        } else if (!ui_context.document->fully_loaded) {
            // Check for page load timeout — show partial content instead of waiting forever
            if (resource_manager_check_page_timeout(ui_context.document->resource_manager)) {
                ui_context.document->fully_loaded = true;
                int pending = resource_manager_get_pending_count(ui_context.document->resource_manager);
                log_warn("view: page load timeout, showing partial content (%d resources still pending)", pending);
                reflow_html_doc(ui_context.document);

                // update title to indicate partial load
                if (ui_context.document->url) {
                    const char* page_title = session_extract_title(ui_context.document);
                    char title[512];
                    if (page_title) {
                        snprintf(title, sizeof(title), "Lambda - %s (partial)", page_title);
                    } else {
                        const char* doc_href = url_get_href(ui_context.document->url);
                        snprintf(title, sizeof(title), "Lambda - %s (partial)", doc_href ? doc_href : "");
                    }
                    glfwSetWindowTitle(window, title);
                }
            } else {
                // show loading progress in window title
                float progress = resource_manager_get_load_progress(ui_context.document->resource_manager);
                char title[512];
                snprintf(title, sizeof(title), "Loading... %.0f%%", progress * 100.0f);
                glfwSetWindowTitle(window, title);
            }
        }
    }

    // reflow the document if window size has changed
    if (width != ui_context.window_width || height != ui_context.window_height) {
        log_debug("render: window size changed to %dx%d, reflowing", width, height);
        double start_time = glfwGetTime();
        ui_context.window_width = width;  ui_context.window_height = height;
        // CRITICAL: Update viewport dimensions (CSS logical pixels) for layout
        // This ensures vh/vw units and percentage heights use the correct window size
        ui_context.viewport_width = (int)(width / ui_context.pixel_ratio);
        ui_context.viewport_height = (int)(height / ui_context.pixel_ratio);
        log_debug("render: updated viewport to %dx%d CSS pixels",
                  (int)ui_context.viewport_width, (int)ui_context.viewport_height);
        // resize the surface
        ui_context_create_surface(&ui_context, width, height);
        // reflow the document
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);
            // Resize listeners must observe the new metrics and may mutate
            // layout synchronously before this frame is presented.
            radiant_dispatch_window_event(&ui_context, ui_context.document, "resize");
        }
        // new surface is blank — force full repaint (not selective)
        if (ui_context.document && ui_context.document->state) {
            doc_state_mark_dirty(ui_context.document->state);
        }
        log_debug("Reflow time: %.2f ms", (glfwGetTime() - start_time) * 1000);
    }

    // Check for incremental reflow due to state changes (pseudo-classes, etc.)
    DocState* state = ui_context.document ? ui_context.document->state : nullptr;
    if (state && state->needs_reflow) {
        log_debug("render: incremental reflow triggered by state change");
        double start_time = glfwGetTime();
        // Reflow the document (styles will be recalculated for marked elements)
        if (ui_context.document) {
            reflow_html_doc(ui_context.document);
        }
        doc_state_clear_reflow(state);
        log_debug("Incremental reflow time: %.2f ms", (glfwGetTime() - start_time) * 1000);
    }

    // rerender if the document is dirty or needs repaint (e.g., caret changed)
    if (ui_context.document && ui_context.document->state &&
        (ui_context.document->state->is_dirty ||
         (ui_context.document->state->needs_repaint &&
          dirty_has_regions(&ui_context.document->state->dirty_tracker)))) {
        render_html_doc(&ui_context, ui_context.document->view_tree, NULL);
        // Phase 19: clear dirty tracker after render (for caret-only repaints)
        doc_state_clear_render_flags(ui_context.document->state);
    } else if (ui_context.document && ui_context.document->state) {
        // Clear stale needs_repaint when there are no dirty regions to render
        doc_state_clear_repaint(ui_context.document->state);

        // Video-only dirty path: skip full DL rebuild, just blit new video frames
        if (ui_context.document->state->has_active_video &&
            ui_context.document->state->video_placement_count > 0) {
            render_video_frames_cached(ui_context.document->state, ui_context.surface, &ui_context);
        }
    }

    // repaint to screen
    repaint_window();

    // Swap front and back buffers
    glfwSwapBuffers(window);
    glFinish(); // important, this waits until rendering result is actually visible, thus making resizing less ugly
}

void log_init_wrapper() {
    // empty existing log file and load config only when log.conf is present (dev/debug mode)
    // in release mode (no log.conf), skip to avoid creating log.txt in the working directory
    if (file_exists("log.conf")) {
        FILE *file = fopen("log.txt", "w");
        if (file) { fclose(file); }
        log_parse_config_file("log.conf");
    }
}
void log_cleanup() {
    log_finish();
}

// Layout test function for headless testing
static int window_finish_event_sim(EventSimContext* sim_ctx) {
    if (!sim_ctx) return 0;
    int fail_count = sim_ctx->fail_count;
    if (sim_ctx->original_document) {
        ui_context.document = (DomDocument*)sim_ctx->original_document;
        sim_ctx->frame_stack_depth = 0;
    }
    event_sim_free(sim_ctx);
    return fail_count;
}

static void window_cleanup_view_runtime(NetworkThreadPool* thread_pool,
                                        EnhancedFileCache* file_cache,
                                        bool log_memory) {
    view_cleanup_js_batch_state();
    if (ui_context.document) radiant_cleanup_network_support(ui_context.document);
    if (ui_context.browsing_session) {
        session_destroy(ui_context.browsing_session);
        ui_context.browsing_session = nullptr;
    }
    if (thread_pool) thread_pool_destroy(thread_pool);
    if (file_cache) enhanced_cache_destroy(file_cache);
    network_downloader_cleanup_shared();
    view_close_event_log();
    view_close_state_dump();
    js_dom_set_ui_context(nullptr);
    // ui_context is stack-backed; JS must drop the host loop reference before teardown.
    js_dom_set_host_driven_loop(false);
    ui_context_cleanup(&ui_context);
    view_cleanup_input_manager();
    lambda_uv_cleanup();
    if (log_memory) log_mem_stage("after-cleanup");
    log_cleanup();
}

// Unified document viewer supporting multiple formats (HTML, Markdown, XML, RST, etc.)
// event_file: optional JSON file with simulated events for automated testing
// headless: if true, run without creating a window (for CI/automated testing)
static int view_doc_in_window_with_events_internal(const char* doc_file, const char* doc_source,
                                                   const char* event_file, bool headless,
                                                   const char** font_dirs, int font_dir_count,
                                                   bool enable_event_log,
                                                   bool enable_state_dump) {
    log_init_wrapper();
    log_info("VIEW_DOC_IN_WINDOW STARTED with file: %s, source: %s, event_file: %s, headless: %d",
             doc_file ? doc_file : "NULL", doc_source ? "memory" : "file",
             event_file ? event_file : "NULL", headless);
    ui_context_init(&ui_context, headless);
    ui_context.event_log_enabled = enable_event_log;
    ui_context.state_dump_enabled = enable_state_dump;

    // Add custom font scan directories (must be done before any font resolution)
    for (int i = 0; i < font_dir_count; i++) {
        font_context_add_scan_directory(ui_context.font_ctx, font_dirs[i]);
        log_debug("view: Added font directory: %s", font_dirs[i]);
    }
    log_debug("view_doc_in_window: after ui_context_init: window_width=%.1f, window_height=%.1f, pixel_ratio=%.2f",
              ui_context.window_width, ui_context.window_height, ui_context.pixel_ratio);
    GLFWwindow* window = ui_context.window;
    if (!headless && !window) {
        ui_context_cleanup(&ui_context);
        return -1;
    }

    // Load event simulation if specified
    EventSimContext* sim_ctx = NULL;
    if (event_file) {
        size_t event_file_len = strlen(event_file);
        bool replay_jsonl = event_file_len >= 6 && strcmp(event_file + event_file_len - 6, ".jsonl") == 0;
        sim_ctx = replay_jsonl ? event_sim_load_replay_log(event_file) : event_sim_load(event_file);
        if (!sim_ctx) {
            log_error("Failed to load event file: %s", event_file);
            // Continue without simulation
        } else {
            log_info("Event simulation loaded: %d events", sim_ctx->events->length);
        }
    }

    int width, height;

    if (!headless) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);  // enable vsync
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

        glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // receive the state of the Caps Lock and Num Lock keys
        glfwSetKeyCallback(window, key_callback);  // receive raw keyboard input
        glfwSetCharCallback(window, character_callback);  // receive character input
        glfwSetCursorPosCallback(window, cursor_position_callback);  // receive cursor/mouse position
        glfwSetMouseButtonCallback(window, mouse_button_callback);  // receive mouse button input
        log_info("Mouse button callback registered: %p", mouse_button_callback);
        glfwSetScrollCallback(window, scroll_callback);  // receive mouse/touchpad scroll input
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetWindowRefreshCallback(window, window_refresh_callback);

        glClearColor(0.8f, 0.8f, 0.8f, 1.0f); // Light grey color

        glfwGetFramebufferSize(window, &width, &height);
        framebuffer_size_callback(window, width, height);

        // CRITICAL: Update ui_context dimensions to match actual framebuffer size
        // This ensures the initial layout uses the correct viewport dimensions
        ui_context.window_width = width;
        ui_context.window_height = height;
        ui_context.viewport_width = (int)(width / ui_context.pixel_ratio);
        ui_context.viewport_height = (int)(height / ui_context.pixel_ratio);
        log_debug("view_doc_in_window: updated viewport to %dx%d CSS pixels (framebuffer %dx%d)",
                  (int)ui_context.viewport_width, (int)ui_context.viewport_height, width, height);
    } else {
        // Headless mode: use default dimensions from ui_context_init
        width = (int)ui_context.window_width;
        height = (int)ui_context.window_height;
    }

    // Recreate surface with correct dimensions
    ui_context_create_surface(&ui_context, width, height);

    // Expose this window's UiContext to the JS DOM layer BEFORE the document is
    // loaded, because load-time inline scripts (e.g. the editor bootstrap) run
    // synchronously inside load_doc_by_format() and may call geometry APIs
    // (getBoundingClientRect/getClientRects). Those route through
    // js_dom_ensure_layout_for_geometry(), which needs a UiContext to force a
    // synchronous layout when the DOM has been mutated. In the `view` path this
    // thread-local was never set (only the headless `lambda.exe js` session set
    // it), so it bailed and returned stale/zero rects for freshly built
    // subtrees (e.g. an editor table read by syncColResizers). Every
    // ui_context_cleanup() below is paired with a matching reset to null since
    // ui_context is a stack local that must not outlive this frame.
    js_dom_set_ui_context(&ui_context);

    // Mark this as a host-driven session ONLY when a loop will actually pump the
    // JS event loop after the first layout commits: an interactive GUI window
    // (!headless) or a headless event simulation (sim_ctx). In that mode geometry
    // queries read committed geometry instead of rebuilding the live view tree,
    // and load-time timers defer to the post-commit pump (browser-faithful:
    // setTimeout(0) queued during load fires after the first layout). A static
    // headless render (no window, no events) has no such pump, so it must keep
    // the self-contained load-time drain — leaving host-driven false there avoids
    // stranding its timers. Cleared alongside the UiContext reset before each
    // ui_context_cleanup().
    bool host_driven_loop = (!headless || sim_ctx != nullptr);
    js_dom_set_host_driven_loop(host_driven_loop);

    // Network resources (owned by this function, shared across document lifetime)
    NetworkThreadPool* thread_pool = nullptr;
    EnhancedFileCache* file_cache = nullptr;

    Url* cwd = get_current_dir();
    if (cwd) {
        // Use provided document file or default to test HTML file
        const char* file_to_load = doc_file ? doc_file : "test/html/index.html";

        log_debug("Loading document: %s", file_to_load);

        // Create memory pool for document loading
        Pool* pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "window");
        if (!pool) {
            log_error("Failed to create memory pool for document");
            url_destroy(cwd);
            js_dom_set_ui_context(nullptr);
            js_dom_set_host_driven_loop(false);
            ui_context_cleanup(&ui_context);
            return -1;
        }

        // CSS media queries should use CSS pixels (logical pixels), not physical pixels
        int css_width = (int)(width / ui_context.pixel_ratio);
        int css_height = (int)(height / ui_context.pixel_ratio);

        // Apply viewport override from event simulation if specified
        if (sim_ctx && sim_ctx->viewport_width > 0 && sim_ctx->viewport_height > 0) {
            css_width = sim_ctx->viewport_width;
            css_height = sim_ctx->viewport_height;
            ui_context.viewport_width = css_width;
            ui_context.viewport_height = css_height;
            log_info("event_sim: viewport override to %dx%d CSS pixels", css_width, css_height);
        }

        // Static headless smoke renders do not need retained JS event state after
        // load-time scripts have mutated the DOM. Interactive windows and event
        // simulations keep the compiled context alive for dispatch.
        bool needs_interactive_js = !headless || sim_ctx != nullptr;
        script_runner_set_retain_js_state(needs_interactive_js);
        script_runner_set_execute_external_scripts(needs_interactive_js);

        // Load document based on file extension, or evaluate an in-memory
        // Lambda document script supplied by a caller such as PDF view.
        Url* log_doc_url = url_parse_with_base(file_to_load, cwd);
        const char* log_doc_href = log_doc_url ? url_get_href(log_doc_url) : file_to_load;
        log_notice("view: loading document: %s", log_doc_href ? log_doc_href : file_to_load);
        if (log_doc_url) url_destroy(log_doc_url);
        log_mem_stage("before-load");
        DomDocument* doc = nullptr;
        if (doc_source) {
            Url* script_url = url_parse_with_base(file_to_load, cwd);
            if (!script_url) {
                log_error("Failed to parse in-memory script URL: %s", file_to_load);
                pool_destroy(pool);
                url_destroy(cwd);
                js_dom_set_ui_context(nullptr);
                js_dom_set_host_driven_loop(false);
                ui_context_cleanup(&ui_context);
                return -1;
            }
            doc = load_lambda_script_source_doc(script_url, doc_source, css_width, css_height, pool);
        } else {
            doc = load_doc_by_format(file_to_load, cwd, css_width, css_height, pool);
        }
        if (!doc) {
            log_error("Failed to load document: %s", file_to_load);
            pool_destroy(pool);
            url_destroy(cwd);
            js_dom_set_ui_context(nullptr);
            js_dom_set_host_driven_loop(false);
            ui_context_cleanup(&ui_context);
            return -1;
        }
        log_mem_stage("after-load");
        log_notice("view: document loaded, starting layout...");

        // Set scale for window display: given_scale = 1.0, scale = pixel_ratio
        // For HTML documents, this updates the default; for PDF/SVG/Image, this was already set in loader
        if (doc->html_root) {
            // HTML documents need scale set for display (layout is in CSS pixels)
            doc->viewport.given_scale = 1.0f;
            doc->viewport.scale = doc->viewport.given_scale * ui_context.pixel_ratio;
        }
        // Note: PDF/SVG/Image documents already have scale set in their respective loaders

        ui_context.document = doc;

        // Initialize network support for HTTP-loaded documents.
        // Top-level URL views may be staged through ./temp after content-type
        // probing, so use the parsed document URL instead of the local path.
        bool doc_url_is_http = doc->url &&
            (doc->url->scheme == URL_SCHEME_HTTP || doc->url->scheme == URL_SCHEME_HTTPS);
        if (doc->html_root && doc_url_is_http) {
            network_downloader_init_shared();
            file_cache = enhanced_cache_create("./temp/cache", 100 * 1024 * 1024, 10000);
            if (radiant_init_network_support(doc, NULL, file_cache) == 0) {
                resource_manager_set_ui_context(doc->resource_manager, &ui_context);
                if (!headless && doc->resource_manager) {
                    resource_manager_set_wake_callback(doc->resource_manager,
                                                       network_wake_glfw, NULL);
                }
                log_info("view: network support initialized for HTTP document");
            }
        }

        // Create browsing session for navigation history and session state
        ui_context.browsing_session = session_create(thread_pool, file_cache);
        if (ui_context.browsing_session) {
            log_info("view: browsing session created");
        }

        radiant_document_ensure_state(doc, "view_doc_in_window");
        view_attach_event_log(doc, file_to_load);

        // Process @font-face rules before layout
        process_document_font_faces(&ui_context, doc);

        // Discover and queue network resources BEFORE layout
        // This starts async downloads for CSS, images, fonts early
        if (doc->resource_manager) {
            radiant_discover_document_resources(doc);
            log_info("view: network resource discovery complete");

            // Wait for render-blocking CSS (up to 5 seconds)
            // CSS in <head> must be loaded before first meaningful layout
            double wait_start = glfwGetTime();
            const double CSS_TIMEOUT = 5.0;
            while (!resource_manager_is_fully_loaded(doc->resource_manager)) {
                double elapsed = glfwGetTime() - wait_start;
                if (elapsed >= CSS_TIMEOUT) {
                    log_warn("view: CSS load timeout after %.1fs, proceeding with layout", elapsed);
                    break;
                }
                // Process any completed resources (CSS parsed → stylesheets added)
                resource_manager_flush_layout_updates(doc->resource_manager);
                // Brief sleep to avoid busy-waiting
                glfwWaitEventsTimeout(0.05);  // 50ms poll
            }
            double wait_time = glfwGetTime() - wait_start;
            if (wait_time > 0.01) {
                log_info("view: waited %.2fs for network resources before layout", wait_time);
            }
            int total_resources = 0;
            int completed_resources = 0;
            int failed_resources = 0;
            resource_manager_get_stats(doc->resource_manager, &total_resources,
                                       &completed_resources, &failed_resources);
            log_info("view: network resource stats total=%d completed=%d failed=%d",
                     total_resources, completed_resources, failed_resources);
            if (failed_resources > 0) {
                log_error("view: network resource failures detected: %d of %d",
                          failed_resources, total_resources);
            }
        }

        // Layout document when it has an HTML/CSS DOM; pre-built view trees skip this
        if (doc->root) {
            log_mem_stage("before-layout");
            layout_html_doc(&ui_context, doc, false);
            log_mem_stage("after-layout");
        }
        log_notice("view: layout complete, rendering...");
        // Render document
        if (doc && doc->view_tree) {
            log_mem_stage("before-render");
            render_html_doc(&ui_context, doc->view_tree, NULL);
            log_mem_stage("after-render");
        }
        log_notice("view: render complete");

        url_destroy(cwd);

        // Set custom window title with format name
        if (!headless && doc_file) {
            // Prefer <title> element text for HTML documents
            const char* page_title = doc->html_root ? session_extract_title(doc) : nullptr;
            char title[512];
            if (page_title) {
                snprintf(title, sizeof(title), "Lambda - %s", page_title);
            } else {
                const char* format_name = get_format_name(file_to_load);
                snprintf(title, sizeof(title), "Lambda %s Viewer - %s", format_name, file_to_load);
            }
            glfwSetWindowTitle(window, title);
            // store initial title in session if available
            if (ui_context.browsing_session && page_title) {
                session_set_current_title(ui_context.browsing_session, page_title);
            }
        }
    }

    // --- Headless mode: run events synchronously without a window ---
    if (headless) {
        if (sim_ctx && sim_ctx->is_running) {
            double current_time = 0.0;
            while (sim_ctx->is_running) {
                bool running = event_sim_update(sim_ctx, &ui_context, window, current_time);
                if (!running) break;
                // Tick the JS event loop between sim events so deferred callbacks
                // (setTimeout/queueMicrotask-scheduled work, e.g. the coalesced
                // `selectionchange` dispatch) run — mirroring a real event loop
                // ticking between user actions. Without this, page-JS never sees
                // selection changes from native caret moves. Bounded/non-blocking
                // so a self-rescheduling callback can't spin to the watchdog.
                if (event_sim_assertion_retry_pending(sim_ctx)) {
                    int retry_wait_ms = event_sim_assertion_retry_wait_ms(sim_ctx);
                    if (radiant_pump_js_event_loop(&ui_context, retry_wait_ms)) {
                        event_sim_wake_assertion_retry(sim_ctx);
                    }
                } else {
                    radiant_pump_js_event_loop(&ui_context, -1);
                }
                // Advance time by event interval
                SimEvent* ev = (sim_ctx->current_index > 0 && sim_ctx->current_index <= sim_ctx->events->length)
                    ? (SimEvent*)sim_ctx->events->data[sim_ctx->current_index - 1] : NULL;
                int wait_ms = 50;
                if (ev && ev->type == SIM_EVENT_WAIT) wait_ms = ev->wait_ms;
                current_time += wait_ms / 1000.0;
            }
        }
        int sim_fail_count = window_finish_event_sim(sim_ctx);
        log_info("End of headless document viewer");
        window_cleanup_view_runtime(thread_pool, file_cache, true);
        return sim_fail_count > 0 ? 1 : 0;
    }

    // --- GUI mode: full window event loop ---

    // Check for auto-close after initial render (for testing/benchmarking)
    bool auto_close_enabled = (shell_getenv("LAMBDA_AUTO_CLOSE") != NULL);
    if (auto_close_enabled) {
        log_info("First frame rendered, auto-closing window for testing");
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Initial render to screen - must call render() to set up OpenGL state and blit surface to screen
    do_redraw = 1;

    // Main loop
    RadiantFrameClock frame_clock;
    if (!radiant_frame_clock_init(&frame_clock, 60.0)) {
        log_warn("view_frame_clock: failed to initialize frame clock, using monotonic fallback");
    }
    radiant_frame_clock_set_wake_callback(&frame_clock, view_wake_glfw, NULL);
    radiant_video_set_wake_callback(view_wake_glfw, NULL);
    if (!radiant_frame_clock_start(&frame_clock)) {
        log_warn("view_frame_clock: native clock start failed, using timed fallback");
    }
    log_info("view_frame_clock: using %s clock", radiant_frame_clock_mode_name(&frame_clock));

    const double CARET_BLINK_INTERVAL = 0.5;  // 500ms blink interval

    // Give the window a moment to render before starting simulation
    double sim_start_delay = sim_ctx ? 0.5 : 0.0;  // 500ms delay before starting simulation
    double sim_start_time = radiant_frame_clock_now(&frame_clock) + sim_start_delay;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = radiant_frame_clock_now(&frame_clock);
        bool frame_driven = false;

        // poll for new events
        glfwPollEvents();
        uv_loop_t* uv_loop = lambda_uv_loop();
        if (uv_loop) {
            uv_run(uv_loop, UV_RUN_NOWAIT);
        }

        if (js_animation_frame_has_pending()) {
            int callbacks = js_animation_frame_flush(currentTime * 1000.0);
            if (callbacks > 0) {
                do_redraw = 1;
            }
            frame_driven = js_animation_frame_has_pending() != 0;
        }

        // Drain network completions on the UI thread before deciding whether
        // this tick needs a reflow/repaint.
        if (ui_context.document && ui_context.document->resource_manager) {
            resource_manager_flush_layout_updates(ui_context.document->resource_manager);
        }

        // Process simulated events if simulation is active
        if (sim_ctx && sim_ctx->is_running && currentTime >= sim_start_time) {
            bool sim_running = event_sim_update(sim_ctx, &ui_context, window, currentTime);
            if (!sim_running && sim_ctx->auto_close) {
                // Simulation complete, auto-close window
                log_info("Simulation complete, auto-closing window");
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            do_redraw = 1;  // redraw after each simulated event
        }

        DocState* state = ui_context.document ? ui_context.document->state : nullptr;
        bool caret_visible = caret_has_projection(state);
        bool editing_animation_active = radiant_editing_animation_active(state);
        if (editing_animation_active) {
            if (radiant_editing_animation_tick(&ui_context, currentTime)) {
                do_redraw = 1;
            }
            caret_visible = caret_has_projection(state);
            editing_animation_active = radiant_editing_animation_active(state);
        }

        // Tick active animations
        if (state && state->animation_scheduler && state->animation_scheduler->has_active_animations) {
            // set viewport bounds so off-screen animations don't inflate dirty region
            float scroll_y = 0;
            if (ui_context.document && ui_context.document->view_tree && ui_context.document->view_tree->root) {
                ViewBlock* root = lam::view_require_block(ui_context.document->view_tree->root);
                if (root->scroller && root->scroll_mut()->pane) {
                    scroll_state_get_position_for_view(state, static_cast<View*>(root), root->scroll()->pane,
                        NULL, &scroll_y, NULL, NULL);
                }
            }
            state->dirty_tracker.viewport_y = scroll_y;
            state->dirty_tracker.viewport_height = (float)ui_context.viewport_height;

            bool still_active = animation_scheduler_tick(state->animation_scheduler,
                                                         currentTime, &state->dirty_tracker);
            doc_state_request_repaint(state);
            frame_driven = frame_driven || still_active;
            do_redraw = 1;
        }

        // Video playback wakes through RdtVideoCallbacks::on_frame_ready.
        // Only redraw when a frame is pending; paused and low-FPS videos no
        // longer force a full display-refresh loop.
        if (state && state->has_active_video && state->video_frame_pending) {
            do_redraw = 1;
        }

        // Pick up repaint requests from paths that don't go through a GLFW
        // event callback (e.g. macOS IME setMarkedText / insertText, async
        // resource loaders that flip needs_repaint directly). Combined with
        // a glfwPostEmptyEvent these unblock the wait-for-event loop.
        if (state && (state->needs_repaint || state->needs_reflow || state->is_dirty)) {
            do_redraw = 1;
        }

        // Webview layer mode: poll dirty webviews, re-snapshot and redraw
        if (ui_context.webview_mgr && ui_context.document && ui_context.document->view_tree) {
            if (webview_manager_poll_dirty(&ui_context, ui_context.document->view_tree)) {
                do_redraw = 1;
                // mark document dirty so render_html_doc rebuilds the DL
                // (the post-composite blit needs fresh DL_WEBVIEW_LAYER_PLACEHOLDER items)
                if (ui_context.document->state) {
                    doc_state_mark_dirty(ui_context.document->state);
                }
            }
        }

        // only redraw if we need to
        if (do_redraw) {
            window_refresh_callback(window);
            radiant_frame_clock_mark_presented(&frame_clock, currentTime);
        }

        double wait_timeout = view_next_wait_timeout(&frame_clock, currentTime,
                                                     frame_driven, caret_visible,
                                                     state ? state->editing_caret_blink_elapsed : 0.0,
                                                     CARET_BLINK_INTERVAL,
                                                     editing_animation_active,
                                                     sim_ctx, sim_start_time,
                                                     uv_loop);
        if (wait_timeout > 0.0) {
            glfwWaitEventsTimeout(wait_timeout);
        }
    }

    radiant_video_set_wake_callback(NULL, NULL);
    radiant_frame_clock_shutdown(&frame_clock);

    // Get simulation results before cleanup
    int sim_fail_count = window_finish_event_sim(sim_ctx);

    log_info("End of document viewer");
    window_cleanup_view_runtime(thread_pool, file_cache, false);

    // Return non-zero if simulation had failures
    return sim_fail_count > 0 ? 1 : 0;
}

int view_doc_in_window_with_events(const char* doc_file, const char* event_file, bool headless,
                                    const char** font_dirs, int font_dir_count,
                                    bool enable_event_log, bool enable_state_dump) {
    return view_doc_in_window_with_events_internal(doc_file, nullptr, event_file, headless,
                                                   font_dirs, font_dir_count, enable_event_log,
                                                   enable_state_dump);
}

int view_lambda_script_source_in_window_with_events(const char* script_name, const char* script_source,
                                                    const char* event_file, bool headless,
                                                    const char** font_dirs, int font_dir_count,
                                                    bool enable_event_log, bool enable_state_dump) {
    return view_doc_in_window_with_events_internal(script_name, script_source, event_file, headless,
                                                   font_dirs, font_dir_count, enable_event_log,
                                                   enable_state_dump);
}

// Wrapper for backward compatibility
int view_doc_in_window(const char* doc_file) {
    return view_doc_in_window_with_events(doc_file, NULL, false, NULL, 0, false, false);
}
