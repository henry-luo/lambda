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
#include "../lambda/network/cookie_jar.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/radiant_state_store.h"
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
static bool history_ensure_capacity(BrowsingSession* session) {
    if (!session) return false;
    if (session->history_count < session->history_capacity) return true;
    int target = session->history_count + 1;
    if (target > BROWSE_HISTORY_MAX) target = BROWSE_HISTORY_MAX;
    return lam::mem_grow_array(&session->history, &session->history_capacity,
                               target, 8, MEM_CAT_TEMP);
}

static bool history_append_loaded(BrowsingSession* session, Url* url, const char* title,
                                  float scroll_y, const char* transition) {
    if (!session || !url) return false;

    // Reserve all memory before committing SQLite so a reported successful
    // navigation cannot leave the durable and in-memory stacks different.
    if (!history_ensure_capacity(session)) return false;
    char* title_copy = title ? mem_strdup(title, MEM_CAT_TEMP) : nullptr;
    if (title && !title_copy) return false;
    if (session->state_store && session->browsing_context_id && url->href &&
        !radiant_state_store_history_append(session->state_store,
            session->browsing_context_id, url->href->chars, title,
            0.0f, scroll_y, transition, nullptr)) {
        log_error("browse_session: failed to persist navigation history");
        mem_free(title_copy);
        return false;
    }

    for (int i = session->history_index + 1; i < session->history_count; i++) {
        history_entry_free(&session->history[i]);
    }
    session->history_count = session->history_index + 1;

    if (session->history_count >= BROWSE_HISTORY_MAX) {
        history_entry_free(&session->history[0]);
        memmove(&session->history[0], &session->history[1],
                (size_t)(session->history_count - 1) * sizeof(HistoryEntry));
        session->history_count--;
        session->history_index--;
    }

    if (session->history_count >= session->history_capacity) {
        mem_free(title_copy);
        return false;
    }

    HistoryEntry* entry = &session->history[session->history_count];
    entry->url = url;
    entry->title = title_copy;
    entry->scroll_y = scroll_y;
    session->history_count++;
    session->history_index = session->history_count - 1;
    return true;
}

static bool history_load_from_state(const char* url, const char* title,
                                    float scroll_x, float scroll_y, void* user_data) {
    (void)scroll_x;
    BrowsingSession* session = (BrowsingSession*)user_data;
    Url* parsed = url_parse(url);
    if (!parsed) return true;  // skip an unusable historical URL and keep the store readable
    if (!history_ensure_capacity(session)) {
        url_destroy(parsed);
        return false;
    }
    if (session->history_count >= session->history_capacity) {
        url_destroy(parsed);
        return false;
    }
    HistoryEntry* entry = &session->history[session->history_count++];
    entry->url = parsed;
    entry->title = title ? mem_strdup(title, MEM_CAT_TEMP) : nullptr;
    entry->scroll_y = scroll_y;
    return true;
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

static void session_init_with_profile(BrowsingSession* session,
                                      struct NetworkThreadPool* pool,
                                      struct EnhancedFileCache* cache,
                                      const char* profile_name) {
    if (!session) return;
    session->history = nullptr;
    session->history_count = 0;
    session->history_index = -1;
    session->history_capacity = 0;
    session->thread_pool = pool;
    session->file_cache = cache;
    session->state_store = nullptr;
    session->cookie_jar = nullptr;
    session->browsing_context_id = nullptr;

    const char* cache_dir = enhanced_cache_get_directory(cache);
    if (cache_dir) {
        session->state_store = radiant_state_store_open(cache_dir, profile_name);
        if (session->state_store) {
            session->browsing_context_id = radiant_state_store_open_browsing_context(
                session->state_store, &session->history_index);
            if (session->browsing_context_id) {
                if (!radiant_state_store_history_load(session->state_store,
                                                      session->browsing_context_id,
                                                      history_load_from_state, session)) {
                    log_error("browse_session: failed to restore navigation history");
                    for (int i = 0; i < session->history_count; i++) {
                        history_entry_free(&session->history[i]);
                    }
                    session->history_count = 0;
                    session->history_index = -1;
                }
                if (session->history_index >= session->history_count) {
                    session->history_index = session->history_count - 1;
                }
            }
        }
    }
    session->cookie_jar = cookie_jar_create(session->state_store);
    if (!session->cookie_jar) log_error("browse_session: failed to create cookie jar");
}

BrowsingSession* session_create_for_profile(struct NetworkThreadPool* pool,
                                            struct EnhancedFileCache* cache,
                                            const char* profile_name) {
    BrowsingSession* session = (BrowsingSession*)mem_calloc(1, sizeof(BrowsingSession), MEM_CAT_TEMP);
    if (!session) return nullptr;

    session_init_with_profile(session, pool, cache, profile_name);
    log_info("browse_session: created");
    return session;
}

BrowsingSession* session_create(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache) {
    return session_create_for_profile(pool, cache, "default");
}

void BrowsingSession::init(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache) {
    session_init_with_profile(this, pool, cache, "default");
}

void BrowsingSession::destroy() {
    if (cookie_jar) {
        cookie_jar_clear_session(cookie_jar);
        (void)cookie_jar_flush(cookie_jar);
        cookie_jar_destroy(cookie_jar);
        cookie_jar = nullptr;
    }
    if (state_store) {
        radiant_state_store_close(state_store);
        state_store = nullptr;
    }
    browsing_context_id = nullptr;

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
        url_destroy(resolved);
        return nullptr;
    }

    if (old_doc) {
        assert(old_doc != uicon->document);
        radiant_cleanup_network_support(old_doc);
        free_document(old_doc);
    }

    // Commit history only after the replacement document successfully loaded.
    const char* title_text = session_extract_title(new_doc);
    if (!history_append_loaded(this, resolved, title_text, 0.0f, "link")) {
        log_error("browse_session: failed to append navigation history");
        url_destroy(resolved);
    } else {
        HistoryEntry* entry = &history[history_index];
        log_info("browse_session: loaded %s (title: %s, history: %d/%d)",
                 resolved_href,
                 entry->title ? entry->title : "(none)",
                 history_index + 1, history_count);
    }
    session_attach_document(this, new_doc);

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

    session_attach_document(session, new_doc);

    // update title if it changed
    const char* title_text = session_extract_title(new_doc);
    if (title_text && (!entry->title || strcmp(entry->title, title_text) != 0)) {
        if (entry->title) mem_free(entry->title);
        entry->title = mem_strdup(title_text, MEM_CAT_TEMP);
    }
    if (session->state_store && session->browsing_context_id) {
        (void)radiant_state_store_history_select(session->state_store,
                                                 session->browsing_context_id,
                                                 session->history_index);
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
    if (state_store && browsing_context_id) {
        (void)radiant_state_store_history_update_current(state_store, browsing_context_id,
            history[history_index].title, 0.0f, scroll_y);
    }
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
    if (state_store && browsing_context_id) {
        (void)radiant_state_store_history_update_current(state_store, browsing_context_id,
            entry->title, 0.0f, entry->scroll_y);
    }
}

void session_set_current_title(BrowsingSession* session, const char* title) {
    if (session) session->set_current_title(title);
}

void session_seed_document(BrowsingSession* session, DomDocument* document) {
    if (!session || !document || !document->url || !document->url->href) return;
    // A caller-provided initial URL supersedes automatic restore and becomes
    // a fresh branch from any persisted back/forward stack.
    Url* url = url_parse(document->url->href->chars);
    if (!url) return;
    if (!history_append_loaded(session, url, session_extract_title(document), 0.0f, "typed")) {
        url_destroy(url);
    }
}

void session_attach_document(BrowsingSession* session, DomDocument* document) {
    if (!session || !document || !document->resource_manager) return;
    resource_manager_set_cookie_jar(document->resource_manager, session->cookie_jar);
}

CookieJar* session_cookie_jar(const BrowsingSession* session) {
    return session ? session->cookie_jar : nullptr;
}

RadiantStateStore* session_state_store(const BrowsingSession* session) {
    return session ? session->state_store : nullptr;
}

const char* session_browsing_context_id(const BrowsingSession* session) {
    return session ? session->browsing_context_id : nullptr;
}
