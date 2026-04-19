// browsing_session.cpp
// Browsing session with history management for Radiant web browser.

#include "view.hpp"
#include "browsing_session.h"
#include "webview.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/url.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/network/network_integration.h"
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
    int new_cap = session->history_capacity < 8 ? 8 : session->history_capacity * 2;
    if (new_cap > BROWSE_HISTORY_MAX) new_cap = BROWSE_HISTORY_MAX;
    HistoryEntry* new_arr = (HistoryEntry*)mem_realloc(session->history,
        (size_t)new_cap * sizeof(HistoryEntry), MEM_CAT_TEMP);
    if (!new_arr) return;
    session->history = new_arr;
    session->history_capacity = new_cap;
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

    session->history = nullptr;
    session->history_count = 0;
    session->history_index = -1;
    session->history_capacity = 0;
    session->thread_pool = pool;
    session->file_cache = cache;

    log_info("browse_session: created");
    return session;
}

void session_destroy(BrowsingSession* session) {
    if (!session) return;

    // free all history entries
    for (int i = 0; i < session->history_count; i++) {
        history_entry_free(&session->history[i]);
    }
    mem_free(session->history);

    // note: thread_pool and file_cache are not owned by session — caller manages them
    mem_free(session);
    log_info("browse_session: destroyed");
}

DomDocument* session_navigate(BrowsingSession* session, struct UiContext* uicon,
                              const char* url, int vw, int vh) {
    if (!session || !uicon || !url) return nullptr;

    // resolve URL against current page (if any)
    Url* resolved = nullptr;
    if (session->history_index >= 0 && session->history[session->history_index].url) {
        resolved = url_resolve_relative(url, session->history[session->history_index].url);
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
    for (int i = session->history_index + 1; i < session->history_count; i++) {
        history_entry_free(&session->history[i]);
    }
    session->history_count = session->history_index + 1;

    // evict oldest entry if at max capacity
    if (session->history_count >= BROWSE_HISTORY_MAX) {
        history_entry_free(&session->history[0]);
        memmove(&session->history[0], &session->history[1],
                (size_t)(session->history_count - 1) * sizeof(HistoryEntry));
        session->history_count--;
        session->history_index--;
    }

    // add new history entry
    history_ensure_capacity(session);
    if (session->history_count >= session->history_capacity) {
        url_destroy(resolved);
        return nullptr;  // allocation failed
    }

    HistoryEntry* entry = &session->history[session->history_count];
    entry->url = resolved;
    entry->title = nullptr;
    entry->scroll_y = 0.0f;
    session->history_count++;
    session->history_index = session->history_count - 1;

    // clean up old document's network resources
    DomDocument* old_doc = uicon->document;
    if (old_doc) {
        radiant_cleanup_network_support(old_doc);
    }

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

    // extract and store page title
    const char* title_text = session_extract_title(new_doc);
    if (title_text) {
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }

    log_info("browse_session: loaded %s (title: %s, history: %d/%d)",
             resolved_href,
             entry->title ? entry->title : "(none)",
             session->history_index + 1, session->history_count);

    mem_free(href_copy);
    return new_doc;
}

DomDocument* session_go_back(BrowsingSession* session, struct UiContext* uicon,
                             int vw, int vh) {
    if (!session_can_go_back(session) || !uicon) return nullptr;

    session->history_index--;
    HistoryEntry* entry = &session->history[session->history_index];

    if (!entry->url || !entry->url->href) {
        log_error("browse_session: back — invalid history entry");
        session->history_index++;
        return nullptr;
    }

    const char* href = entry->url->href->chars;
    log_info("browse_session: going back to %s (%d/%d)",
             href, session->history_index + 1, session->history_count);

    // clean up old document's network resources
    DomDocument* old_doc = uicon->document;
    if (old_doc) {
        radiant_cleanup_network_support(old_doc);
    }

    // destroy all webviews from the old page before loading the new one
    if (uicon->webview_mgr) {
        webview_manager_clear(uicon->webview_mgr);
    }

    char* href_copy = mem_strdup(href, MEM_CAT_TEMP);
    DomDocument* new_doc = show_html_doc(entry->url, href_copy, vw, vh);
    mem_free(href_copy);

    if (!new_doc) {
        log_error("browse_session: failed to reload %s", href);
        session->history_index++;
        return nullptr;
    }

    // update title if it changed
    const char* title_text = session_extract_title(new_doc);
    if (title_text && (!entry->title || strcmp(entry->title, title_text) != 0)) {
        if (entry->title) mem_free(entry->title);
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }

    return new_doc;
}

DomDocument* session_go_forward(BrowsingSession* session, struct UiContext* uicon,
                                int vw, int vh) {
    if (!session_can_go_forward(session) || !uicon) return nullptr;

    session->history_index++;
    HistoryEntry* entry = &session->history[session->history_index];

    if (!entry->url || !entry->url->href) {
        log_error("browse_session: forward — invalid history entry");
        session->history_index--;
        return nullptr;
    }

    const char* href = entry->url->href->chars;
    log_info("browse_session: going forward to %s (%d/%d)",
             href, session->history_index + 1, session->history_count);

    // clean up old document's network resources
    DomDocument* old_doc = uicon->document;
    if (old_doc) {
        radiant_cleanup_network_support(old_doc);
    }

    // destroy all webviews from the old page before loading the new one
    if (uicon->webview_mgr) {
        webview_manager_clear(uicon->webview_mgr);
    }

    char* href_copy = mem_strdup(href, MEM_CAT_TEMP);
    DomDocument* new_doc = show_html_doc(entry->url, href_copy, vw, vh);
    mem_free(href_copy);

    if (!new_doc) {
        log_error("browse_session: failed to reload %s", href);
        session->history_index--;
        return nullptr;
    }

    // update title if it changed
    const char* title_text = session_extract_title(new_doc);
    if (title_text && (!entry->title || strcmp(entry->title, title_text) != 0)) {
        if (entry->title) mem_free(entry->title);
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }

    return new_doc;
}

bool session_can_go_back(const BrowsingSession* session) {
    return session && session->history_index > 0;
}

bool session_can_go_forward(const BrowsingSession* session) {
    return session && session->history_index < session->history_count - 1;
}

const char* session_current_url(const BrowsingSession* session) {
    if (!session || session->history_index < 0) return nullptr;
    HistoryEntry* entry = &session->history[session->history_index];
    if (entry->url && entry->url->href) return entry->url->href->chars;
    return nullptr;
}

const char* session_current_title(const BrowsingSession* session) {
    if (!session || session->history_index < 0) return nullptr;
    return session->history[session->history_index].title;
}

void session_save_scroll_position(BrowsingSession* session, float scroll_y) {
    if (!session || session->history_index < 0) return;
    session->history[session->history_index].scroll_y = scroll_y;
}

float session_get_scroll_position(const BrowsingSession* session) {
    if (!session || session->history_index < 0) return 0.0f;
    return session->history[session->history_index].scroll_y;
}

const char* session_extract_title(DomDocument* doc) {
    if (!doc || !doc->root) return nullptr;
    return find_title_text(doc->root);
}

void session_set_current_title(BrowsingSession* session, const char* title) {
    if (!session || session->history_index < 0) return;
    HistoryEntry* entry = &session->history[session->history_index];
    if (entry->title) mem_free(entry->title);
    entry->title = title ? mem_strdup(title, MEM_CAT_TEMP) : nullptr;
}
