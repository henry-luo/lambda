/**
 * Radiant ClipboardStore — implementation.
 *
 * See radiant/clipboard.hpp + vibe/radiant/Radiant_Design_Clipboard.md.
 *
 * Phase 1A scope:
 *   - In-memory canonical store with multi-MIME items.
 *   - Two built-in backends: in-memory (default; used by tests + headless)
 *     and GLFW (plain-text bridge; mirrors the legacy behaviour previously
 *     in state_store.cpp).
 *   - Permission state slots for the upcoming Permissions API.
 *   - HTML sanitiser stub that strips <script> / <style> blocks.
 */

#include "event.hpp"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "../lib/log.h"
#include "../lib/arena.h"
#include "../lib/arraylist.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"

// GLFW backend is opt-in: define RADIANT_CLIPBOARD_GLFW when linking GLFW.
// The default headless / test build uses the in-memory backend and never
// touches GLFW, so this header is not required there.
#ifdef RADIANT_CLIPBOARD_GLFW
#include <GLFW/glfw3.h>
struct UiContextRef { void* window; };
extern struct UiContextRef ui_context;
#endif

// ---------------------------------------------------------------------------
// Internal store + global state
// ---------------------------------------------------------------------------

static ClipboardStore g_store = { NULL, NULL, NULL,
                                  CLIPBOARD_PERMISSION_PROMPT,
                                  CLIPBOARD_PERMISSION_PROMPT };

// ---------------------------------------------------------------------------
// ClipboardEntry / ClipboardItem helpers
// ---------------------------------------------------------------------------

static ClipboardEntry* entry_new(const char* mime, const char* data, size_t data_len) {
    ClipboardEntry* e = (ClipboardEntry*)mem_calloc(1, sizeof(ClipboardEntry), MEM_CAT_SYSTEM);
    if (!e) return NULL;
    e->mime = mem_strdup(mime ? mime : "", MEM_CAT_SYSTEM);
    // always allocate one extra byte and null-terminate so text/* is safe to
    // read as a C string; binary callers use data_len.
    e->data = (char*)mem_alloc(data_len + 1, MEM_CAT_SYSTEM);
    if (e->data) {
        if (data && data_len) memcpy(e->data, data, data_len);
        e->data[data_len] = '\0';
    }
    e->data_len = data_len;
    return e;
}

static void entry_free(ClipboardEntry* e) {
    if (!e) return;
    mem_free(e->mime);
    mem_free(e->data);
    mem_free(e);
}

static ClipboardItem* item_new(void) {
    ClipboardItem* it = (ClipboardItem*)mem_calloc(1, sizeof(ClipboardItem), MEM_CAT_SYSTEM);
    if (!it) return NULL;
    it->entries = arraylist_new(2);
    return it;
}

static void item_free(ClipboardItem* it) {
    if (!it) return;
    if (it->entries) {
        for (int i = 0; i < it->entries->length; i++) {
            entry_free((ClipboardEntry*)it->entries->data[i]);
        }
        arraylist_free(it->entries);
    }
    mem_free(it);
}

static void items_free(ArrayList* items) {
    if (!items) return;
    for (int i = 0; i < items->length; i++) {
        item_free((ClipboardItem*)items->data[i]);
    }
    arraylist_free(items);
}

static ClipboardEntry* item_find_entry(ClipboardItem* it, const char* mime) {
    if (!it || !it->entries || !mime) return NULL;
    for (int i = 0; i < it->entries->length; i++) {
        ClipboardEntry* e = (ClipboardEntry*)it->entries->data[i];
        if (e && e->mime && strcmp(e->mime, mime) == 0) return e;
    }
    return NULL;
}

// Find the first entry across all items matching the requested MIME.
static ClipboardEntry* store_find_entry(ArrayList* items, const char* mime) {
    if (!items) return NULL;
    for (int i = 0; i < items->length; i++) {
        ClipboardItem* it = (ClipboardItem*)items->data[i];
        ClipboardEntry* e = item_find_entry(it, mime);
        if (e) return e;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// In-memory backend (default, used by headless tests)
// ---------------------------------------------------------------------------

typedef struct {
    ArrayList* items;   // backend-owned snapshot mirror
} InMemoryBackendState;

static void inmem_clear(ClipboardBackend* self) {
    InMemoryBackendState* s = (InMemoryBackendState*)self->opaque;
    if (s->items) { items_free(s->items); s->items = NULL; }
}

static ArrayList* clone_items(ArrayList* src) {
    if (!src) return NULL;
    ArrayList* dst = arraylist_new(src->length);
    for (int i = 0; i < src->length; i++) {
        ClipboardItem* in = (ClipboardItem*)src->data[i];
        ClipboardItem* out = item_new();
        out->is_unsanitized = in->is_unsanitized;
        if (in->entries) {
            for (int j = 0; j < in->entries->length; j++) {
                ClipboardEntry* e = (ClipboardEntry*)in->entries->data[j];
                ClipboardEntry* copy = entry_new(e->mime, e->data, e->data_len);
                arraylist_append(out->entries, copy);
            }
        }
        arraylist_append(dst, out);
    }
    return dst;
}

static void inmem_write_items(ClipboardBackend* self, ArrayList* items) {
    InMemoryBackendState* s = (InMemoryBackendState*)self->opaque;
    if (s->items) items_free(s->items);
    s->items = clone_items(items);
}

static ArrayList* inmem_read_items(ClipboardBackend* self) {
    InMemoryBackendState* s = (InMemoryBackendState*)self->opaque;
    return clone_items(s->items);
}

static ClipboardBackend* g_inmem_backend = NULL;

ClipboardBackend* clipboard_backend_inmemory(void) {
    if (g_inmem_backend) return g_inmem_backend;
    g_inmem_backend = (ClipboardBackend*)mem_calloc(1, sizeof(ClipboardBackend), MEM_CAT_SYSTEM); // OBJ_HEAP_OK: process-lifetime clipboard backend singleton.
    g_inmem_backend->opaque = mem_calloc(1, sizeof(InMemoryBackendState), MEM_CAT_SYSTEM);
    g_inmem_backend->write_items = inmem_write_items;
    g_inmem_backend->read_items  = inmem_read_items;
    g_inmem_backend->clear       = inmem_clear;
    return g_inmem_backend;
}

static void clipboard_backend_inmemory_shutdown(void) {
    if (!g_inmem_backend) return;
    if (g_inmem_backend->clear) g_inmem_backend->clear(g_inmem_backend);
    if (g_inmem_backend->opaque) {
        mem_free(g_inmem_backend->opaque);
        g_inmem_backend->opaque = NULL;
    }
    mem_free(g_inmem_backend);
    g_inmem_backend = NULL;
}

// ---------------------------------------------------------------------------
// GLFW plain-text backend (legacy bridge)
// ---------------------------------------------------------------------------

#ifdef RADIANT_CLIPBOARD_GLFW
static void glfw_write_items(ClipboardBackend* self, ArrayList* items) {
    (void)self;
    if (!items) return;
    // Find first text/plain entry across items.
    for (size_t i = 0; i < items->length; i++) {
        ClipboardItem* it = (ClipboardItem*)items->data[i];
        ClipboardEntry* e = item_find_entry(it, "text/plain");
        if (e && ui_context.window) {
            glfwSetClipboardString((GLFWwindow*)ui_context.window, e->data);
            return;
        }
    }
}

static ArrayList* glfw_read_items(ClipboardBackend* self) {
    (void)self;
    if (!ui_context.window) return NULL;
    const char* s = glfwGetClipboardString((GLFWwindow*)ui_context.window);
    if (!s) return NULL;
    ArrayList* out = arraylist_new(1);
    ClipboardItem* it = item_new();
    arraylist_append(it->entries, entry_new("text/plain", s, strlen(s)));
    arraylist_append(out, it);
    return out;
}

static void glfw_clear_fn(ClipboardBackend* self) {
    (void)self;
    if (ui_context.window) glfwSetClipboardString((GLFWwindow*)ui_context.window, "");
}

static ClipboardBackend g_glfw_backend = {
    glfw_write_items, glfw_read_items, glfw_clear_fn, NULL
};

ClipboardBackend* clipboard_backend_glfw(void) { return &g_glfw_backend; }
#else
ClipboardBackend* clipboard_backend_glfw(void) { return clipboard_backend_inmemory(); }
#endif

// ---------------------------------------------------------------------------
// Public store API
// ---------------------------------------------------------------------------

void clipboard_store_init(void) {
    g_store.init();
}

void clipboard_store_shutdown(void) {
    g_store.destroy();
}

void clipboard_store_clear(void) {
    g_store.clear();
}

bool ClipboardStore::init() {
    if (items) return true;     // idempotent
    items = arraylist_new(2);
    if (!items) return false;
    backend = clipboard_backend_inmemory();
    perm_read  = CLIPBOARD_PERMISSION_PROMPT;
    perm_write = CLIPBOARD_PERMISSION_PROMPT;
    return true;
}

void ClipboardStore::destroy() {
    if (items) {
        items_free(items);
        items = NULL;
    }
    mem_free(cached_text);
    cached_text = NULL;
    backend = NULL;
    // The process clipboard owner is also responsible for its default backend;
    // tearing both down together prevents a live backend mirror outliving state.
    clipboard_backend_inmemory_shutdown();
}

void ClipboardStore::set_backend(ClipboardBackend* next_backend) {
    backend = next_backend ? next_backend : clipboard_backend_inmemory();
}

void ClipboardStore::clear() {
    if (!items) init();
    items_free(items);
    items = arraylist_new(2);
    mem_free(cached_text);
    cached_text = NULL;
    if (backend && backend->clear) backend->clear(backend);
}

void ClipboardStore::write_mime(const char* mime, const char* text) {
    if (!mime || !text) return;
    if (!items) init();
    clear();
    ClipboardItem* it = item_new();
    arraylist_append(it->entries, entry_new(mime, text, strlen(text)));
    arraylist_append(items, it);
    if (backend && backend->write_items) {
        backend->write_items(backend, items);
    }
    log_debug("clipboard_store: wrote %zu bytes mime=%s", strlen(text), mime);
}

void ClipboardStore::write_text(const char* text) {
    write_mime("text/plain", text ? text : "");
}

void ClipboardStore::write_html(const char* html, const char* plain_text) {
    if (!html) return;
    if (!items) init();
    clear();

    ClipboardItem* it = item_new();
    if (!it) return;
    arraylist_append(it->entries, entry_new("text/html", html, strlen(html)));
    const char* text = plain_text ? plain_text : html;
    arraylist_append(it->entries, entry_new("text/plain", text, strlen(text)));
    arraylist_append(items, it);

    if (backend && backend->write_items) {
        backend->write_items(backend, items);
    }
    log_debug("clipboard_store: wrote html=%zu bytes plain=%zu bytes",
              strlen(html), strlen(text));
}

const char* ClipboardStore::read_mime(const char* mime) {
    if (!items) init();
    // pull from backend (if it has data not yet mirrored)
    if (backend && backend->read_items) {
        ArrayList* fresh = backend->read_items(backend);
        if (fresh) {
            items_free(items);
            items = fresh;
        }
    }
    ClipboardEntry* e = store_find_entry(items, mime);
    return e ? e->data : NULL;
}

const char* ClipboardStore::read_text() {
    return read_mime("text/plain");
}

void ClipboardStore::write_items(ArrayList* next_items) {
    if (!items) init();
    items_free(items);
    items = clone_items(next_items);
    if (!items) items = arraylist_new(2);
    if (backend && backend->write_items) {
        backend->write_items(backend, items);
    }
}

ArrayList* ClipboardStore::read_items() {
    if (!items) init();
    if (backend && backend->read_items) {
        ArrayList* fresh = backend->read_items(backend);
        if (fresh) {
            items_free(items);
            items = fresh;
        }
    }
    return clone_items(items);
}

void clipboard_store_write_mime(const char* mime, const char* text) { g_store.write_mime(mime, text); }
void clipboard_store_write_text(const char* text) { g_store.write_text(text); }
void clipboard_store_write_html(const char* html, const char* plain_text) { g_store.write_html(html, plain_text); }
const char* clipboard_store_read_mime(const char* mime) { return g_store.read_mime(mime); }
const char* clipboard_store_read_text(void) { return g_store.read_text(); }
void clipboard_store_write_items(ArrayList* items) { g_store.write_items(items); }
ArrayList* clipboard_store_read_items(void) { return g_store.read_items(); }

// ---------------------------------------------------------------------------
// Permission slots
// ---------------------------------------------------------------------------

void clipboard_store_set_permission_read(ClipboardPermission s)  { g_store.perm_read  = s; }
void clipboard_store_set_permission_write(ClipboardPermission s) { g_store.perm_write = s; }
ClipboardPermission clipboard_store_get_permission_read(void)    { return g_store.perm_read; }
ClipboardPermission clipboard_store_get_permission_write(void)   { return g_store.perm_write; }

// ---------------------------------------------------------------------------
// Sanitiser (Phase 1A: minimal HTML script/style stripper)
// ---------------------------------------------------------------------------

static int starts_with_ci(const char* s, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
        s++; prefix++;
    }
    return 1;
}

static char* strip_html_block(const char* src, const char* open, const char* close, struct Arena* arena) {
    size_t len = strlen(src);
    StrBuf* sb = strbuf_new_cap(len);
    if (!sb) return NULL;
    const char* p = src;
    while (*p) {
        if (*p == '<' && starts_with_ci(p + 1, open + 1)) {
            const char* end = strstr(p, close);
            if (!end) break;
            p = end + strlen(close);
            continue;
        }
        strbuf_append_char(sb, *p++);
    }
    char* out = (char*)arena_alloc(arena, sb->length + 1);
    if (out) { memcpy(out, sb->str, sb->length); out[sb->length] = '\0'; }
    strbuf_free(sb);
    return out;
}

char* clipboard_store_sanitize(struct Arena* arena, const char* mime, const char* raw) {
    if (!raw) return NULL;
    if (!mime || strcmp(mime, "text/html") != 0) {
        size_t n = strlen(raw);
        char* out = (char*)arena_alloc(arena, n + 1);
        if (out) { memcpy(out, raw, n); out[n] = '\0'; }
        return out;
    }
    char* step1 = strip_html_block(raw, "<script", "</script>", arena);
    char* step2 = strip_html_block(step1 ? step1 : raw, "<style", "</style>", arena);
    return step2 ? step2 : (step1 ? step1 : (char*)raw);
}
