#pragma once

#include "../lib/arraylist.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ===== clipboard =====

/**
 * Radiant ClipboardStore — canonical multi-MIME clipboard for both the
 * sync DOM clipboard event path and the async navigator.clipboard API.
 *
 * In-process store plus a pluggable platform backend. The desktop macOS
 * frontend installs NSPasteboard; headless runs use the in-memory backend.
 */



#ifdef __cplusplus
extern "C" {
#endif

// One representation of a clipboard item (e.g. "text/plain", "text/html",
// "image/png"). Owned bytes; data is NOT null-terminated for binary MIMEs
// but always null-terminated for text/* (one extra byte past data_len).
typedef struct ClipboardEntry {
    char*   mime;       // owned, null-terminated MIME string
    char*   data;       // owned bytes (size data_len)
    size_t  data_len;
} ClipboardEntry;

// A single clipboard item is a set of alternative representations of
// the same payload (text/plain + text/html for the same selection, etc.).
typedef struct ClipboardItem {
    ArrayList* entries;     // ArrayList<ClipboardEntry*>
    int        is_unsanitized;
} ClipboardItem;

// Pluggable platform backend. The default (and headless) backend is a
// pure in-memory store. Platform adapters implement this same vtable.
typedef struct ClipboardBackend {
    void  (*write_items)(struct ClipboardBackend* self, ArrayList* items);
    ArrayList* (*read_items)(struct ClipboardBackend* self);
    void  (*clear)(struct ClipboardBackend* self);
    void* opaque;
} ClipboardBackend;

// Global store API ----------------------------------------------------------

void  clipboard_store_shutdown(void);
void  clipboard_store_set_backend(ClipboardBackend* backend);
// platform adapters share the store's owned item representation.
ClipboardItem* clipboard_item_new(void);
void clipboard_item_add(ClipboardItem* item, const char* mime, const void* bytes, size_t length);
void clipboard_items_free(ArrayList* items);
ClipboardEntry* clipboard_items_find(ArrayList* items, const char* mime);
// paste takes one owned snapshot so a subsequent read cannot invalidate it.
bool clipboard_store_read_paste(char** html, char** text);
bool clipboard_store_write_image(const char* data_uri, const char* html, const char* text);

// Built-in backends.
ClipboardBackend* clipboard_backend_inmemory(void);   // headless / tests
ClipboardBackend* clipboard_backend_glfw(void);       // existing plain-text-only

// High-level helpers shared by sync + async paths -----------------------------

// Copy the supplied UTF-8 text to the clipboard as text/plain. Replaces
// any prior contents.
void   clipboard_store_write_text(const char* text);

// Read the clipboard's text/plain representation. Returned pointer is
// owned by the store and valid until the next clipboard write/clear.
const char* clipboard_store_read_text(void);

// Copy HTML plus a plain-text fallback as one multi-MIME clipboard item.
void   clipboard_store_write_html(const char* html, const char* plain_text);

// Read a specific MIME representation. NULL if not present.
const char* clipboard_store_read_mime(const char* mime);

// Multi-MIME write / read (used by the async navigator.clipboard API).
// `items` is an ArrayList<ClipboardItem*>; the store deep-copies bytes.
void       clipboard_store_write_items(ArrayList* items);
ArrayList* clipboard_store_read_items(void);

void   clipboard_store_clear(void);

// Sanitiser: returns an arena-allocated sanitised copy of `raw` for the
// given MIME (currently a no-op that strips <script>/<style> for
// text/html; pass-through otherwise).
char*  clipboard_store_sanitize(struct Arena* arena, const char* mime, const char* raw);

// Permission state for navigator.permissions.query ----------------------------
// Phase 1B uses these constants; Permissions Policy gating is Phase 2.

typedef enum ClipboardPermission {
    CLIPBOARD_PERMISSION_PROMPT  = 0,
    CLIPBOARD_PERMISSION_GRANTED = 1,
    CLIPBOARD_PERMISSION_DENIED  = 2,
} ClipboardPermission;

#ifdef __cplusplus
// Process owner for the canonical clipboard contents and backend binding.
// The free clipboard_store_* functions below remain the C-compatible facade.
struct ClipboardStore {
    ArrayList* items;
    char* cached_text;
    ClipboardBackend* backend;
    ClipboardPermission perm_read;
    ClipboardPermission perm_write;

    bool init();
    void destroy();
    void clear();
    void set_backend(ClipboardBackend* next_backend);
    void write_mime(const char* mime, const char* text);
    void write_text(const char* text);
    void write_html(const char* html, const char* plain_text);
    const char* read_mime(const char* mime);
    const char* read_text();
    void write_items(ArrayList* next_items);
    ArrayList* read_items();
};
#endif

void                clipboard_store_set_permission_read(ClipboardPermission state);
void                clipboard_store_set_permission_write(ClipboardPermission state);
ClipboardPermission clipboard_store_get_permission_read(void);
ClipboardPermission clipboard_store_get_permission_write(void);

#ifdef __cplusplus
}
#endif
