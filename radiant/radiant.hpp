#pragma once

#include "view.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "event.hpp"

#include "../lib/url.h"

#include <stdbool.h>
#include <stdint.h>

// consolidated Radiant shell API (DD4).

#ifdef __cplusplus
extern "C" {
#endif

// ===== browsing session =====

struct NetworkThreadPool;
struct EnhancedFileCache;

#define BROWSE_HISTORY_MAX 100

typedef struct HistoryEntry {
    Url* url;
    char* title;
    float scroll_y;
} HistoryEntry;

typedef struct BrowsingSession {
    HistoryEntry* history;
    int history_count;
    int history_index;
    int history_capacity;

    struct NetworkThreadPool* thread_pool;
    struct EnhancedFileCache* file_cache;
} BrowsingSession;

BrowsingSession* session_create(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache);
void session_destroy(BrowsingSession* session);
DomDocument* session_navigate(BrowsingSession* session, struct UiContext* uicon,
                              const char* url, int vw, int vh);
DomDocument* session_go_back(BrowsingSession* session, struct UiContext* uicon,
                             int vw, int vh);
DomDocument* session_go_forward(BrowsingSession* session, struct UiContext* uicon,
                                int vw, int vh);
bool session_can_go_back(const BrowsingSession* session);
bool session_can_go_forward(const BrowsingSession* session);
const char* session_current_url(const BrowsingSession* session);
const char* session_current_title(const BrowsingSession* session);
void session_save_scroll_position(BrowsingSession* session, float scroll_y);
float session_get_scroll_position(const BrowsingSession* session);
const char* session_extract_title(DomDocument* doc);
void session_set_current_title(BrowsingSession* session, const char* title);

// ===== frame clock =====

typedef enum RadiantFrameClockMode {
    RADIANT_FRAME_CLOCK_MONOTONIC = 0,
    RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK,
    RADIANT_FRAME_CLOCK_WINDOWS_DWM,
    RADIANT_FRAME_CLOCK_WINDOWS_QPC_TIMER,
    RADIANT_FRAME_CLOCK_LINUX_TIMERFD
} RadiantFrameClockMode;

typedef void (*RadiantFrameWakeCallback)(void* user_data);
typedef struct RadiantFrameClockPlatform RadiantFrameClockPlatform;

typedef struct RadiantFrameClock {
    RadiantFrameClockMode mode;
    double refresh_hz;
    double refresh_interval;
    double last_frame_time;
    double next_frame_time;
    RadiantFrameWakeCallback wake_callback;
    void* wake_user_data;
    RadiantFrameClockPlatform* platform;
    bool initialized;
    bool native_started;
} RadiantFrameClock;

bool radiant_frame_clock_init(RadiantFrameClock* clock, double refresh_hz);
bool radiant_frame_clock_start(RadiantFrameClock* clock);
void radiant_frame_clock_shutdown(RadiantFrameClock* clock);
void radiant_frame_clock_set_wake_callback(RadiantFrameClock* clock,
                                           RadiantFrameWakeCallback callback,
                                           void* user_data);
double radiant_frame_clock_now(RadiantFrameClock* clock);
double radiant_frame_clock_next_timeout(RadiantFrameClock* clock, double now,
                                        bool frame_driven, bool needs_redraw);
void radiant_frame_clock_mark_presented(RadiantFrameClock* clock, double frame_time);
const char* radiant_frame_clock_mode_name(const RadiantFrameClock* clock);

// ===== script runner =====

void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool, Url* base_url);
void script_runner_set_retain_js_state(bool retain);
void script_runner_set_execute_external_scripts(bool execute);
void script_runner_set_preamble_cache_enabled(bool enabled);
void collect_and_compile_event_handlers(DomDocument* dom_doc);
void script_runner_cleanup_js_state(DomDocument* dom_doc);
void script_runner_cleanup_heap(void);
bool script_runner_js_batch_cleanup_unsafe(void);

// ===== webview =====

struct GLFWwindow;
struct ImageSurface;
struct ViewTree;

typedef struct WebViewHandle WebViewHandle;

enum WebViewMode {
    WEBVIEW_MODE_WINDOW = 0,
    WEBVIEW_MODE_LAYER  = 1,
};

#define WEBVIEW_SANDBOX_ALLOW_SCRIPTS     0x01
#define WEBVIEW_SANDBOX_ALLOW_IPC         0x02
#define WEBVIEW_SANDBOX_ALLOW_NAVIGATION  0x04
#define WEBVIEW_SANDBOX_ALLOW_POPUPS      0x08

typedef struct WebViewProp {
    WebViewHandle* handle;
    enum WebViewMode mode;

    const char* src;
    const char* srcdoc;

    float last_x, last_y;
    float last_w, last_h;

    struct ImageSurface* surface;
    bool dirty;
    uint64_t last_snapshot_ms;

    bool visible;
    bool loaded;
    bool needs_create;
} WebViewProp;

typedef struct WebViewManager WebViewManager;

WebViewManager* webview_manager_create(struct GLFWwindow* window);
void webview_manager_destroy(WebViewManager* mgr);
WebViewHandle* webview_handle_create(WebViewManager* mgr, float w, float h, float pixel_ratio);
void webview_handle_destroy(WebViewManager* mgr, WebViewHandle* handle);
void webview_navigate(WebViewHandle* handle, const char* url);
void webview_set_html(WebViewHandle* handle, const char* html);
void webview_eval_js(WebViewHandle* handle, const char* js);
void webview_set_bounds(WebViewHandle* handle, float x, float y,
                        float w, float h, float pixel_ratio);
void webview_set_visible(WebViewHandle* handle, bool visible);
void webview_manager_sync_layout(struct UiContext* uicon, struct ViewTree* tree);
bool webview_manager_poll_dirty(struct UiContext* uicon, struct ViewTree* tree);
void webview_manager_clear(WebViewManager* mgr);

WebViewHandle* webview_platform_create(struct GLFWwindow* window,
                                       float x, float y, float w, float h,
                                       float pixel_ratio);
void webview_platform_destroy(WebViewHandle* handle);
void webview_platform_navigate(WebViewHandle* handle, const char* url);
void webview_platform_set_html(WebViewHandle* handle, const char* html);
void webview_platform_eval_js(WebViewHandle* handle, const char* js);
void webview_platform_set_bounds(WebViewHandle* handle,
                                 float x, float y, float w, float h,
                                 float pixel_ratio);
void webview_platform_set_visible(WebViewHandle* handle, bool visible);

WebViewHandle* webview_layer_platform_create(float w, float h, float pixel_ratio);
void webview_layer_platform_destroy(WebViewHandle* handle);
void webview_layer_platform_navigate(WebViewHandle* handle, const char* url);
void webview_layer_platform_set_html(WebViewHandle* handle, const char* html);
void webview_layer_platform_eval_js(WebViewHandle* handle, const char* js);
void webview_layer_platform_resize(WebViewHandle* handle, float w, float h, float pixel_ratio);
bool webview_layer_platform_snapshot(WebViewHandle* handle, struct ImageSurface* surface);
bool webview_layer_platform_is_dirty(WebViewHandle* handle);
void webview_layer_platform_mark_dirty(WebViewHandle* handle, bool dirty);
void webview_layer_platform_inject_mouse(WebViewHandle* handle,
                                         int type, float x, float y, int button, int mods);
void webview_layer_platform_inject_key(WebViewHandle* handle,
                                       int type, int keycode, int mods);
void webview_layer_platform_inject_text(WebViewHandle* handle, uint32_t codepoint);
void webview_layer_platform_inject_scroll(WebViewHandle* handle,
                                          float dx, float dy, float x, float y);

#ifdef __cplusplus
}
#endif

// ===== window / surface / UI context =====

void render(struct GLFWwindow* window);
DomDocument* show_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height);
void reflow_html_doc(DomDocument* doc);
void update_window_title(const char* title);
void repaint_window(void);
int run_layout(const char* html_file);
int view_doc_in_window_with_events(const char* doc_file, const char* event_file, bool headless,
                                   const char* script_source);
int view_lambda_script_source_in_window_with_events(const char* script_name,
                                                    const char* script_source,
                                                    const char* event_file,
                                                    bool headless);
int view_doc_in_window(const char* doc_file);
int window_main(int argc, char* argv[]);

int ui_context_init(UiContext* uicon, bool headless);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void ui_context_cleanup(UiContext* uicon);
void free_document(DomDocument* doc);
void image_cache_cleanup(UiContext* uicon);
ImageSurface* load_image(UiContext* uicon, const char* img_url);
