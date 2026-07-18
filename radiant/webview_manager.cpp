// webview_manager.cpp — WebView lifecycle management and post-layout sync
//
// Central coordinator that owns all web view instances for a document.
// Walks the view tree after layout to create/reposition/destroy web views.

#include "radiant.hpp"
#include "view.hpp"
#include "event.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/mem_grow.hpp"

// ---------------------------------------------------------------------------
// WebViewManager
// ---------------------------------------------------------------------------

struct WebViewManager {
    GLFWwindow* window;
    struct TrackedHandle {
        WebViewHandle* handle;
        int mode;  // WebViewMode: 0=window, 1=layer
    };
    TrackedHandle* tracked;      // dynamic array of active handles
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
    mgr->tracked = (WebViewManager::TrackedHandle*)mem_calloc(
        mgr->handle_capacity, sizeof(WebViewManager::TrackedHandle), MEM_CAT_LAYOUT);
    log_info("webview manager created (window=%p)", (void*)window);
    return mgr;
}

static bool mgr_track_handle(WebViewManager* mgr, WebViewHandle* handle, int mode) {
    if (mgr->handle_count >= mgr->handle_capacity) {
        if (!lam::mem_grow_array(&mgr->tracked, &mgr->handle_capacity,
                                 mgr->handle_count + 1, 4, MEM_CAT_LAYOUT)) return false;
    }
    mgr->tracked[mgr->handle_count] = { handle, mode };
    mgr->handle_count++;
    return true;
}

static void mgr_untrack_handle(WebViewManager* mgr, WebViewHandle* handle) {
    for (int i = 0; i < mgr->handle_count; i++) {
        if (mgr->tracked[i].handle == handle) {
            mgr->tracked[i] = mgr->tracked[mgr->handle_count - 1];
            mgr->tracked[mgr->handle_count - 1] = { nullptr, 0 };
            mgr->handle_count--;
            return;
        }
    }
}

void webview_manager_destroy(WebViewManager* mgr) {
    if (!mgr) return;
    webview_manager_clear(mgr);
    mem_free(mgr->tracked);
    mem_free(mgr);
    log_info("webview manager destroyed");
}

// ---------------------------------------------------------------------------
// Lifecycle wrappers — child window mode (delegate to platform)
// ---------------------------------------------------------------------------

WebViewHandle* webview_handle_create(WebViewManager* mgr, float w, float h, float pixel_ratio) {
    if (!mgr) return nullptr;
    WebViewHandle* handle = webview_platform_create(mgr->window, 0, 0, w, h, pixel_ratio);
    if (handle) {
        if (!mgr_track_handle(mgr, handle, WEBVIEW_MODE_WINDOW)) {
            // tracking owns lifecycle; destroy immediately if the registry cannot grow
            webview_platform_destroy(handle);
            return nullptr;
        }
        log_info("webview child handle created (%p), total=%d", (void*)handle, mgr->handle_count);
    }
    return handle;
}

// ---------------------------------------------------------------------------
// Lifecycle wrappers — layer mode (offscreen rendering)
// ---------------------------------------------------------------------------

static WebViewHandle* webview_layer_handle_create(WebViewManager* mgr, float w, float h, float pixel_ratio) {
    if (!mgr) return nullptr;
    WebViewHandle* handle = webview_layer_platform_create(w, h, pixel_ratio);
    if (handle) {
        if (!mgr_track_handle(mgr, handle, WEBVIEW_MODE_LAYER)) {
            // tracking owns lifecycle; destroy immediately if the registry cannot grow
            webview_layer_platform_destroy(handle);
            return nullptr;
        }
        log_info("webview layer handle created (%p), total=%d", (void*)handle, mgr->handle_count);
    }
    return handle;
}

// ---------------------------------------------------------------------------
// Lifecycle wrappers — shared
// ---------------------------------------------------------------------------

void webview_handle_destroy(WebViewManager* mgr, WebViewHandle* handle) {
    if (!mgr || !handle) return;
    mgr_untrack_handle(mgr, handle);
    webview_platform_destroy(handle);
    log_info("webview handle destroyed, remaining=%d", mgr->handle_count);
}

static void webview_layer_handle_destroy(WebViewManager* mgr, WebViewHandle* handle) {
    if (!mgr || !handle) return;
    mgr_untrack_handle(mgr, handle);
    webview_layer_platform_destroy(handle);
    log_info("webview layer handle destroyed, remaining=%d", mgr->handle_count);
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

// layer mode wrappers
static void webview_layer_navigate(WebViewHandle* handle, const char* url) {
    if (handle && url) webview_layer_platform_navigate(handle, url);
}

static void webview_layer_set_html(WebViewHandle* handle, const char* html) {
    if (handle && html) webview_layer_platform_set_html(handle, html);
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
static bool tree_has_webview(View* view) {
    ViewElement* elem = lam::view_as_element(view);
    if (!elem) return false;
    if (elem->tag_id == HTM_TAG_WEBVIEW && elem->embed && elem->embedp()->webview) {
        return true;
    }
    DomNode* child = elem->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            if (child->view_type && tree_has_webview(child)) {
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
    if (block->scroller && block->scroll_mut()->pane) {
        DocState* state = block->doc ? block->doc->state : NULL;
        float scroll_x = 0.0f, scroll_y = 0.0f;
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroll()->pane,
                                           &scroll_x, &scroll_y, NULL, NULL);
        scroll_dx = -scroll_x;
        scroll_dy = -scroll_y;
    }

    if (block->tag_id == HTM_TAG_WEBVIEW && block->embed && block->embedp()->webview) {
        WebViewProp* wv = block->embedp()->webview;

        if (wv->mode == WEBVIEW_MODE_LAYER) {
            // --- Layer mode: offscreen rendering ---
            if (wv->needs_create && !wv->handle) {
                wv->handle = webview_layer_handle_create(mgr, block->width, block->height, pixel_ratio);
                wv->needs_create = false;

                if (wv->handle) {
                    // allocate the ImageSurface for snapshot storage
                    if (!wv->surface) {
                        int phys_w = (int)(block->width * pixel_ratio);   // INT_CAST_OK: pixel dimension
                        int phys_h = (int)(block->height * pixel_ratio);  // INT_CAST_OK: pixel dimension
                        wv->surface = image_surface_create(phys_w, phys_h);
                    }

                    if (wv->srcdoc) {
                        webview_layer_set_html(wv->handle, wv->srcdoc);
                    } else if (wv->src) {
                        webview_layer_navigate(wv->handle, wv->src);
                    }
                    wv->dirty = true;
                    log_info("webview layer created: src=%s, size=(%.0f,%.0f)",
                             wv->src ? wv->src : "(srcdoc)",
                             block->width, block->height);
                }
            }

            if (wv->handle) {
                // resize if dimensions changed
                bool size_changed = (block->width != wv->last_w || block->height != wv->last_h);
                if (size_changed) {
                    webview_layer_platform_resize(wv->handle, block->width, block->height, pixel_ratio);
                    // reallocate surface at new size
                    if (wv->surface) {
                        image_surface_destroy(wv->surface);
                    }
                    int phys_w = (int)(block->width * pixel_ratio);   // INT_CAST_OK: pixel dimension
                    int phys_h = (int)(block->height * pixel_ratio);  // INT_CAST_OK: pixel dimension
                    wv->surface = image_surface_create(phys_w, phys_h);
                    wv->dirty = true;
                }

                // update stored position (for rendering)
                wv->last_x = abs_x;
                wv->last_y = abs_y;
                wv->last_w = block->width;
                wv->last_h = block->height;

                // capture snapshot if dirty and loaded
                if (wv->dirty && wv->surface) {
                    bool ok = webview_layer_platform_snapshot(wv->handle, wv->surface);
                    if (ok) {
                        image_surface_bump_generation(wv->surface);
                        wv->dirty = false;
                        log_debug("webview layer: snapshot captured %.0fx%.0f",
                                  block->width, block->height);
                    }
                }

                wv->visible = (block->display.outer != CSS_VALUE_NONE);
            }
        } else {
            // --- Child window mode: native overlay ---
            if (wv->needs_create && !wv->handle) {
                wv->handle = webview_handle_create(mgr, block->width, block->height, pixel_ratio);
                wv->needs_create = false;

                if (wv->handle) {
                    if (wv->srcdoc) {
                        webview_set_html(wv->handle, wv->srcdoc);
                    } else if (wv->src) {
                        webview_navigate(wv->handle, wv->src);
                    }
                    log_info("webview child created: src=%s, pos=(%.0f,%.0f) size=(%.0f,%.0f)",
                             wv->src ? wv->src : "(srcdoc)",
                             abs_x, abs_y, block->width, block->height);
                }
            }

            if (wv->handle) {
                bool bounds_changed = (abs_x != wv->last_x || abs_y != wv->last_y ||
                                       block->width != wv->last_w || block->height != wv->last_h);
                if (bounds_changed) {
                    webview_set_bounds(wv->handle, abs_x, abs_y,
                                       block->width, block->height, pixel_ratio);
                    wv->last_x = abs_x;
                    wv->last_y = abs_y;
                    wv->last_w = block->width;
                    wv->last_h = block->height;
                    log_debug("webview child repositioned: (%.0f,%.0f) %.0fx%.0f",
                              abs_x, abs_y, block->width, block->height);
                }

                bool should_be_visible = (block->display.outer != CSS_VALUE_NONE);
                if (should_be_visible != wv->visible) {
                    webview_set_visible(wv->handle, should_be_visible);
                    wv->visible = should_be_visible;
                }
            }
        }
    }

    // recurse into children
    DomNode* child = block->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            ViewBlock* child_block = lam::view_as_block(child);
            if (child_block) {
                sync_walk(mgr, child_block,
                          abs_x + scroll_dx, abs_y + scroll_dy, pixel_ratio);
            }
        }
        child = child->next_sibling;
    }
}

void webview_manager_sync_layout(UiContext* uicon, ViewTree* tree) {
    if (!uicon || !tree || !tree->root) return;

    ViewBlock* root = lam::view_require_block(tree->root);

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

// poll dirty layer-mode webviews: walk tree, re-snapshot any that became dirty
// returns true if any webview was re-snapshotted (caller should trigger repaint)
static bool poll_dirty_walk(ViewBlock* block) {
    if (!block) return false;
    bool any_dirty = false;

    if (block->tag_id == HTM_TAG_WEBVIEW && block->embed && block->embedp()->webview) {
        WebViewProp* wv = block->embedp()->webview;
        if (wv->mode == WEBVIEW_MODE_LAYER && wv->handle && wv->surface) {
            // check the platform dirty flag (set by navigation delegate / mutation observer)
            if (webview_layer_platform_is_dirty(wv->handle)) {
                wv->dirty = true;
                webview_layer_platform_mark_dirty(wv->handle, false);
            }
            if (wv->dirty) {
                bool ok = webview_layer_platform_snapshot(wv->handle, wv->surface);
                if (ok) {
                    image_surface_bump_generation(wv->surface);
                    wv->dirty = false;
                    any_dirty = true;
                    log_debug("webview poll: re-snapshotted layer webview");
                }
            }
        }
    }

    DomNode* child = block->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_ELEMENT) {
            ViewBlock* cb = lam::view_as_block(child);
            if (cb && poll_dirty_walk(cb)) any_dirty = true;
        }
        child = child->next_sibling;
    }
    return any_dirty;
}

bool webview_manager_poll_dirty(UiContext* uicon, ViewTree* tree) {
    if (!uicon || !uicon->webview_mgr || !tree || !tree->root) return false;
    ViewBlock* root = lam::view_require_block(tree->root);
    return poll_dirty_walk(root);
}

void webview_manager_clear(WebViewManager* mgr) {
    if (!mgr) return;
    // destroy all tracked handles (iterate backwards since destroy modifies array)
    while (mgr->handle_count > 0) {
        auto& entry = mgr->tracked[mgr->handle_count - 1];
        if (entry.mode == WEBVIEW_MODE_LAYER) {
            webview_layer_handle_destroy(mgr, entry.handle);
        } else {
            webview_handle_destroy(mgr, entry.handle);
        }
    }
    log_info("webview manager cleared all handles");
}
