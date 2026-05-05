/**
 * Radiant ClipboardStore — canonical multi-MIME clipboard for both the
 * sync DOM clipboard event path and the async navigator.clipboard API.
 *
 * Phase 1A of vibe/radiant/Radiant_Design_Clipboard.md: in-process store
 * + a pluggable backend that, by default, mirrors to the GLFW window
 * clipboard for plain text. Per-OS rich-MIME backends (NSPasteboard,
 * Win32, X11) are wired into the same interface in later phases.
 */

#ifndef RADIANT_CLIPBOARD_HPP
#define RADIANT_CLIPBOARD_HPP

#include <stddef.h>
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"

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
// pure in-memory store. Real OS backends (NSPasteboard, Win32, X11) will
// implement this same vtable in a later phase.
typedef struct ClipboardBackend {
    void  (*write_items)(struct ClipboardBackend* self, ArrayList* items);
    ArrayList* (*read_items)(struct ClipboardBackend* self);
    void  (*clear)(struct ClipboardBackend* self);
    void* opaque;
} ClipboardBackend;

// Global store API ----------------------------------------------------------

void  clipboard_store_init(void);
void  clipboard_store_shutdown(void);

// Switch backend (e.g. tests use the in-memory backend; an interactive
// app installs the GLFW or NSPasteboard backend).
void  clipboard_store_set_backend(ClipboardBackend* backend);

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

// Copy text under a specific MIME type ("text/plain", "text/html"...).
void   clipboard_store_write_mime(const char* mime, const char* text);

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

void                clipboard_store_set_permission_read(ClipboardPermission state);
void                clipboard_store_set_permission_write(ClipboardPermission state);
ClipboardPermission clipboard_store_get_permission_read(void);
ClipboardPermission clipboard_store_get_permission_write(void);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_CLIPBOARD_HPP
