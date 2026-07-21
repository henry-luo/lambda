// browsing_session.cpp
// Browsing session with history management for Radiant web browser.

#include "view.hpp"
#include "radiant.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/mem_grow.hpp"
#include "../lib/url.h"
#include "../lambda/input/css/dom_element.hpp"
#include "network_integration.h"
#include <assert.h>
#include <string.h>

// Forward declarations (defined in window.cpp / cmd_layout.cpp)
extern DomDocument* show_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height);
extern void free_document(DomDocument* doc);

// ============================================================================
// Internal helpers
// ============================================================================

// Free a single history entry's owned resources
static void history_entry_free(HistoryEntry* entry) {
    if (!entry) return;
    if (entry->url) url_destroy(entry->url);
    if (entry->title) mem_free(entry->title);
    entry->url = nullptr;
    entry->title = nullptr;
    entry->scroll_y = 0.0f;
}

// Ensure history array has room for one more entry
static void history_ensure_capacity(BrowsingSession* session) {
    if (session->history_count < session->history_capacity) return;
    int target = session->history_count + 1;
    if (target > BROWSE_HISTORY_MAX) target = BROWSE_HISTORY_MAX;
    (void)lam::mem_grow_array(&session->history, &session->history_capacity,
                              target, 8, MEM_CAT_TEMP);
}

// Walk DOM to find <title> element text content
static const char* find_title_text(DomElement* root) {
    if (!root) return nullptr;
    if (root->tag_name && strcmp(root->tag_name, "title") == 0) {
        // get text content of <title>
        DomNode* child = root->first_child;
        while (child) {
            if (child->is_text()) {
                return child->as_text()->text;
            }
            child = child->next_sibling;
        }
        return nullptr;
    }
    // recurse into children
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            const char* title = find_title_text(child->as_element());
            if (title) return title;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

// ============================================================================
// Public API
// ============================================================================

BrowsingSession* session_create(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache) {
    BrowsingSession* session = (BrowsingSession*)mem_calloc(1, sizeof(BrowsingSession), MEM_CAT_TEMP);
    if (!session) return nullptr;

    session->init(pool, cache);
    log_info("browse_session: created");
    return session;
}

void BrowsingSession::init(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache) {
    history = nullptr;
    history_count = 0;
    history_index = -1;
    history_capacity = 0;
    thread_pool = pool;
    file_cache = cache;
}

void BrowsingSession::destroy() {
    // free all history entries
    for (int i = 0; i < history_count; i++) {
        history_entry_free(&history[i]);
    }
    mem_free(history);

    // note: thread_pool and file_cache are not owned by session — caller manages them
}

void session_destroy(BrowsingSession* session) {
    if (!session) return;
    session->destroy();
    mem_free(session);
    log_info("browse_session: destroyed");
}

DomDocument* BrowsingSession::navigate(struct UiContext* uicon, const char* url,
                                       int vw, int vh) {
    if (!uicon || !url) return nullptr;

    // resolve URL against current page (if any)
    Url* resolved = nullptr;
    if (history_index >= 0 && history[history_index].url) {
        resolved = url_resolve_relative(url, history[history_index].url);
    }
    if (!resolved) {
        resolved = url_parse(url);
    }
    if (!resolved || !resolved->href) {
        log_error("browse_session: failed to parse URL: %s", url);
        if (resolved) url_destroy(resolved);
        return nullptr;
    }

    const char* resolved_href = resolved->href->chars;
    log_info("browse_session: navigating to %s", resolved_href);

    // truncate forward history (discard entries after current index)
    for (int i = history_index + 1; i < history_count; i++) {
        history_entry_free(&history[i]);
    }
    history_count = history_index + 1;

    // evict oldest entry if at max capacity
    if (history_count >= BROWSE_HISTORY_MAX) {
        history_entry_free(&history[0]);
        memmove(&history[0], &history[1],
                (size_t)(history_count - 1) * sizeof(HistoryEntry));
        history_count--;
        history_index--;
    }

    // add new history entry
    history_ensure_capacity(this);
    if (history_count >= history_capacity) {
        url_destroy(resolved);
        return nullptr;  // allocation failed
    }

    HistoryEntry* entry = &history[history_count];
    entry->url = resolved;
    entry->title = nullptr;
    entry->scroll_y = 0.0f;
    history_count++;
    history_index = history_count - 1;

    // session navigation owns replacing the presented document, so keep the old document alive until the new load succeeds.
    DomDocument* old_doc = uicon->document;

    // load the new page
    char* href_copy = mem_strdup(resolved_href, MEM_CAT_TEMP);
    DomDocument* new_doc = show_html_doc(
        resolved,    // base URL
        href_copy,   // URL string
        vw, vh);

    if (!new_doc) {
        log_error("browse_session: failed to load %s", resolved_href);
        mem_free(href_copy);
        return nullptr;
    }

    if (old_doc) {
        assert(old_doc != uicon->document);
        radiant_cleanup_network_support(old_doc);
        free_document(old_doc);
    }

    // extract and store page title
    const char* title_text = session_extract_title(new_doc);
    if (title_text) {
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }

    log_info("browse_session: loaded %s (title: %s, history: %d/%d)",
             resolved_href,
             entry->title ? entry->title : "(none)",
             history_index + 1, history_count);

    mem_free(href_copy);
    return new_doc;
}

DomDocument* session_navigate(BrowsingSession* session, struct UiContext* uicon,
                              const char* url, int vw, int vh) {
    return session ? session->navigate(uicon, url, vw, vh) : nullptr;
}

static DomDocument* session_go_history(BrowsingSession* session, struct UiContext* uicon,
                                       int vw, int vh, int offset,
                                       const char* direction) {
    bool can_go = offset < 0 ? session->can_go_back() : session->can_go_forward();
    if (!can_go || !uicon) return nullptr;

    session->history_index += offset;
    HistoryEntry* entry = &session->history[session->history_index];

    if (!entry->url || !entry->url->href) {
        log_error("browse_session: %s — invalid history entry", direction);
        session->history_index -= offset;
        return nullptr;
    }

    const char* href = entry->url->href->chars;
    log_info("browse_session: going %s to %s (%d/%d)", direction, href,
             session->history_index + 1, session->history_count);

    // history navigation owns replacing the presented document, so keep the old document alive until the reload succeeds.
    DomDocument* old_doc = uicon->document;

    // destroy all webviews from the old page before loading the new one
    if (uicon->webview_mgr) {
        webview_manager_clear(uicon->webview_mgr);
    }

    char* href_copy = mem_strdup(href, MEM_CAT_TEMP);
    DomDocument* new_doc = show_html_doc(entry->url, href_copy, vw, vh);
    mem_free(href_copy);

    if (!new_doc) {
        log_error("browse_session: failed to reload %s", href);
        session->history_index -= offset;
        return nullptr;
    }

    if (old_doc) {
        assert(old_doc != uicon->document);
        radiant_cleanup_network_support(old_doc);
        free_document(old_doc);
    }

    // update title if it changed
    const char* title_text = session_extract_title(new_doc);
    if (title_text && (!entry->title || strcmp(entry->title, title_text) != 0)) {
        if (entry->title) mem_free(entry->title);
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }

    return new_doc;
}

DomDocument* session_go_back(BrowsingSession* session, struct UiContext* uicon,
                             int vw, int vh) {
    return session ? session->go_back(uicon, vw, vh) : nullptr;
}

DomDocument* session_go_forward(BrowsingSession* session, struct UiContext* uicon,
                                int vw, int vh) {
    return session ? session->go_forward(uicon, vw, vh) : nullptr;
}

DomDocument* BrowsingSession::go_back(struct UiContext* uicon, int vw, int vh) {
    return session_go_history(this, uicon, vw, vh, -1, "back");
}

DomDocument* BrowsingSession::go_forward(struct UiContext* uicon, int vw, int vh) {
    return session_go_history(this, uicon, vw, vh, 1, "forward");
}

bool BrowsingSession::can_go_back() const {
    return history_index > 0;
}

bool session_can_go_back(const BrowsingSession* session) {
    return session && session->can_go_back();
}

bool BrowsingSession::can_go_forward() const {
    return history_index < history_count - 1;
}

bool session_can_go_forward(const BrowsingSession* session) {
    return session && session->can_go_forward();
}

const char* BrowsingSession::current_url() const {
    if (history_index < 0) return nullptr;
    HistoryEntry* entry = &history[history_index];
    if (entry->url && entry->url->href) return entry->url->href->chars;
    return nullptr;
}

const char* session_current_url(const BrowsingSession* session) {
    return session ? session->current_url() : nullptr;
}

const char* BrowsingSession::current_title() const {
    if (history_index < 0) return nullptr;
    return history[history_index].title;
}

const char* session_current_title(const BrowsingSession* session) {
    return session ? session->current_title() : nullptr;
}

void BrowsingSession::save_scroll_position(float scroll_y) {
    if (history_index < 0) return;
    history[history_index].scroll_y = scroll_y;
}

void session_save_scroll_position(BrowsingSession* session, float scroll_y) {
    if (session) session->save_scroll_position(scroll_y);
}

float BrowsingSession::get_scroll_position() const {
    if (history_index < 0) return 0.0f;
    return history[history_index].scroll_y;
}

float session_get_scroll_position(const BrowsingSession* session) {
    return session ? session->get_scroll_position() : 0.0f;
}

const char* session_extract_title(DomDocument* doc) {
    if (!doc || !doc->root) return nullptr;
    return find_title_text(doc->root);
}

void BrowsingSession::set_current_title(const char* title) {
    if (history_index < 0) return;
    HistoryEntry* entry = &history[history_index];
    if (entry->title) mem_free(entry->title);
    entry->title = title ? mem_strdup(title, MEM_CAT_TEMP) : nullptr;
}

void session_set_current_title(BrowsingSession* session, const char* title) {
    if (session) session->set_current_title(title);
}
