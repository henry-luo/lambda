// browsing_session.h
// Browsing session with history management for Radiant web browser.
// Manages page navigation, back/forward history, and session-scoped resources.

#ifndef BROWSING_SESSION_H
#define BROWSING_SESSION_H

#include "../lib/url.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for types used in function signatures
struct UiContext;
struct NetworkThreadPool;
struct EnhancedFileCache;
struct DomDocument;

// Maximum history entries to prevent unbounded growth
#define BROWSE_HISTORY_MAX 100

// A single history entry
typedef struct HistoryEntry {
    Url* url;                // page URL (owned)
    char* title;             // page title from <title> (owned, may be NULL)
    float scroll_y;          // saved scroll position for back/forward restoration
} HistoryEntry;

// Browsing session — manages navigation history and session-scoped resources
typedef struct BrowsingSession {
    // History stack
    HistoryEntry* history;   // array of history entries
    int history_count;       // total entries in history
    int history_index;       // current position (-1 = no page loaded)
    int history_capacity;    // allocated capacity

    // Session-scoped resources (shared across navigations)
    struct NetworkThreadPool* thread_pool;
    struct EnhancedFileCache* file_cache;
} BrowsingSession;

// Lifecycle
BrowsingSession* session_create(struct NetworkThreadPool* pool, struct EnhancedFileCache* cache);
void session_destroy(BrowsingSession* session);

// Navigation — returns new DomDocument* or NULL on failure.
// Pushes current page to history before loading new page.
// vw/vh are viewport dimensions in CSS logical pixels.
DomDocument* session_navigate(BrowsingSession* session, struct UiContext* uicon,
                              const char* url, int vw, int vh);

// Back/forward — returns new DomDocument* or NULL if at history boundary.
DomDocument* session_go_back(BrowsingSession* session, struct UiContext* uicon,
                             int vw, int vh);
DomDocument* session_go_forward(BrowsingSession* session, struct UiContext* uicon,
                                int vw, int vh);

// Query
bool session_can_go_back(const BrowsingSession* session);
bool session_can_go_forward(const BrowsingSession* session);
const char* session_current_url(const BrowsingSession* session);
const char* session_current_title(const BrowsingSession* session);

// Save current page's scroll position (called before navigating away)
void session_save_scroll_position(BrowsingSession* session, float scroll_y);

// Get saved scroll position for current history entry (for restoration after back/forward)
float session_get_scroll_position(const BrowsingSession* session);

// Extract <title> text from a document (returns arena-allocated string or NULL)
const char* session_extract_title(DomDocument* doc);

// Update title for current history entry
void session_set_current_title(BrowsingSession* session, const char* title);

#ifdef __cplusplus
}
#endif

#endif // BROWSING_SESSION_H
