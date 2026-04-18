// webview_manager.cpp — WebView lifecycle management and post-layout sync
//
// Central coordinator that owns all web view instances for a document.
// Walks the view tree after layout to create/reposition/destroy web views.

#include "webview.h"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"

// ---------------------------------------------------------------------------
// WebViewManager
// ---------------------------------------------------------------------------

struct WebViewManager {
    GLFWwindow* window;
    WebViewHandle** handles;     // dynamic array of active handles
    int handle_count;
    int handle_capacity;
};

WebViewManager* webview_manager_create(GLFWwindow* window) {
    if (!window) {
        log_error("webview_manager_create: NULL window");
        return nullptr;
    }
    WebViewManager* mgr = (WebViewManager*)mem_calloc(1, sizeof(WebViewManager), MEM_CAT_LAYOUT);
    mgr->window = window;
    mgr->handle_capacity = 4;
    mgr->handles = (WebViewHandle**)mem_calloc(mgr->handle_capacity, sizeof(WebViewHandle*), MEM_CAT_LAYOUT);
    log_info("webview manager created (window=%p)", (void*)window);
    return mgr;
}

static void mgr_track_handle(WebViewManager* mgr, WebViewHandle* handle) {
    if (mgr->handle_count >= mgr->handle_capacity) {
        int new_cap = mgr->handle_capacity * 2;
        WebViewHandle** new_arr = (WebViewHandle**)mem_calloc(new_cap, sizeof(WebViewHandle*), MEM_CAT_LAYOUT);
        for (int i = 0; i < mgr->handle_count; i++) {
            new_arr[i] = mgr->handles[i];
        }
        mem_free(mgr->handles);
        mgr->handles = new_arr;
        mgr->handle_capacity = new_cap;
    }
    mgr->handles[mgr->handle_count++] = handle;
}

static void mgr_untrack_handle(WebViewManager* mgr, WebViewHandle* handle) {
    for (int i = 0; i < mgr->handle_count; i++) {
        if (mgr->handles[i] == handle) {
            mgr->handles[i] = mgr->handles[mgr->handle_count - 1];
            mgr->handles[mgr->handle_count - 1] = nullptr;
            mgr->handle_count--;
            return;
        }
    }
}

void webview_manager_destroy(WebViewManager* mgr) {
    if (!mgr) return;
    webview_manager_clear(mgr);
    mem_free(mgr->handles);
    mem_free(mgr);
    log_info("webview manager destroyed");
}

// ---------------------------------------------------------------------------
// Lifecycle wrappers (delegate to platform)
// ---------------------------------------------------------------------------

WebViewHandle* webview_handle_create(WebViewManager* mgr, float w, float h, float pixel_ratio) {
    if (!mgr) return nullptr;
    WebViewHandle* handle = webview_platform_create(mgr->window, 0, 0, w, h, pixel_ratio);
    if (handle) {
        mgr_track_handle(mgr, handle);
        log_info("webview handle created (%p), total=%d", (void*)handle, mgr->handle_count);
    }
    return handle;
}

void webview_handle_destroy(WebViewManager* mgr, WebViewHandle* handle) {
    if (!mgr || !handle) return;
    mgr_untrack_handle(mgr, handle);
    webview_platform_destroy(handle);
    log_info("webview handle destroyed, remaining=%d", mgr->handle_count);
}

void webview_navigate(WebViewHandle* handle, const char* url) {
    if (handle && url) webview_platform_navigate(handle, url);
}

void webview_set_html(WebViewHandle* handle, const char* html) {
    if (handle && html) webview_platform_set_html(handle, html);
}

void webview_eval_js(WebViewHandle* handle, const char* js) {
    if (handle && js) webview_platform_eval_js(handle, js);
}

void webview_set_bounds(WebViewHandle* handle, float x, float y,
                        float w, float h, float pixel_ratio) {
    if (handle) webview_platform_set_bounds(handle, x, y, w, h, pixel_ratio);
}

void webview_set_visible(WebViewHandle* handle, bool visible) {
    if (handle) webview_platform_set_visible(handle, visible);
}

// ---------------------------------------------------------------------------
// Post-layout sync: walk view tree, create/reposition web views
// ---------------------------------------------------------------------------

// forward: check if tree contains any <webview> elements
static bool tree_has_webview(ViewBlock* block) {
    if (!block) return false;
    if (block->tag_id == HTM_TAG_WEBVIEW && block->embed && block->embed->webview) {
        return true;
    }
    DomNode* child = block->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            ViewBlock* child_block = (ViewBlock*)child;
            if (child_block->view_type && tree_has_webview(child_block)) {
                return true;
            }
        }
        child = child->next_sibling;
    }
    return false;
}

static void sync_walk(WebViewManager* mgr, ViewBlock* block,
                      float parent_x, float parent_y, float pixel_ratio) {
    if (!block) return;

    float abs_x = parent_x + block->x;
    float abs_y = parent_y + block->y;

    // handle scroll offset if this block is a scroll container
    float scroll_dx = 0, scroll_dy = 0;
    if (block->scroller && block->scroller->pane) {
        scroll_dx = -block->scroller->pane->h_scroll_position;
        scroll_dy = -block->scroller->pane->v_scroll_position;
    }

    if (block->tag_id == HTM_TAG_WEBVIEW && block->embed && block->embed->webview) {
        WebViewProp* wv = block->embed->webview;

        if (wv->needs_create && !wv->handle) {
            // create the native web view
            wv->handle = webview_handle_create(mgr, block->width, block->height, pixel_ratio);
            wv->needs_create = false;

            if (wv->handle) {
                // load content
                if (wv->srcdoc) {
                    webview_set_html(wv->handle, wv->srcdoc);
                } else if (wv->src) {
                    webview_navigate(wv->handle, wv->src);
                }
                log_info("webview created and navigated: src=%s, pos=(%.0f,%.0f) size=(%.0f,%.0f)",
                         wv->src ? wv->src : "(srcdoc)",
                         abs_x, abs_y, block->width, block->height);
            }
        }

        if (wv->handle) {
            // reposition if bounds changed
            bool bounds_changed = (abs_x != wv->last_x || abs_y != wv->last_y ||
                                   block->width != wv->last_w || block->height != wv->last_h);
            if (bounds_changed) {
                webview_set_bounds(wv->handle, abs_x, abs_y,
                                   block->width, block->height, pixel_ratio);
                wv->last_x = abs_x;
                wv->last_y = abs_y;
                wv->last_w = block->width;
                wv->last_h = block->height;
                log_debug("webview repositioned: (%.0f,%.0f) %.0fx%.0f",
                          abs_x, abs_y, block->width, block->height);
            }

            // visibility: display:none or scrolled out of viewport
            bool should_be_visible = (block->display.outer != CSS_VALUE_NONE);
            if (should_be_visible != wv->visible) {
                webview_set_visible(wv->handle, should_be_visible);
                wv->visible = should_be_visible;
            }
        }
    }

    // recurse into children
    DomNode* child = block->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            ViewBlock* child_block = (ViewBlock*)child;
            if (child_block->view_type) {
                sync_walk(mgr, child_block,
                          abs_x + scroll_dx, abs_y + scroll_dy, pixel_ratio);
            }
        }
        child = child->next_sibling;
    }
}

void webview_manager_sync_layout(UiContext* uicon, ViewTree* tree) {
    if (!uicon || !tree || !tree->root) return;

    ViewBlock* root = (ViewBlock*)tree->root;

    // lazy-init: create the manager only when the tree has <webview> elements
    if (!uicon->webview_mgr) {
        if (!tree_has_webview(root)) return;  // no webviews, skip
        uicon->webview_mgr = webview_manager_create(uicon->window);
        if (!uicon->webview_mgr) {
            log_error("webview_manager_sync_layout: failed to create webview manager");
            return;
        }
    }

    sync_walk(uicon->webview_mgr, root, 0, 0, uicon->pixel_ratio);
}

void webview_manager_clear(WebViewManager* mgr) {
    if (!mgr) return;
    // destroy all tracked handles (iterate backwards since destroy modifies array)
    while (mgr->handle_count > 0) {
        WebViewHandle* h = mgr->handles[mgr->handle_count - 1];
        webview_handle_destroy(mgr, h);
    }
    log_info("webview manager cleared all handles");
}
