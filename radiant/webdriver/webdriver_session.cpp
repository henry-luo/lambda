/**
 * @file webdriver_session.cpp
 * @brief WebDriver session management
 */

#include "webdriver.hpp"
#include "../layout.hpp"
#include "../state_store.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/strbuf.h"
#include "../../lib/url.h"
#include "../../lambda/input/css/dom_element.hpp"
#include <cstring>
#include <cstdlib>
#include <ctime>

// External functions from radiant (already in view.hpp as C++)
extern int ui_context_init(UiContext* uicon, bool headless);
extern void ui_context_cleanup(UiContext* uicon);
extern void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
extern View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);

// ============================================================================
// UUID Generation
// ============================================================================

static void generate_uuid(char* buf) {
    // Simple UUID v4 generation
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    
    unsigned char bytes[16];
    for (int i = 0; i < 16; i++) {
        bytes[i] = rand() & 0xFF;
    }
    
    // Set version (4) and variant bits
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    
    snprintf(buf, WD_ELEMENT_ID_LEN,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

// ============================================================================
// Element Registry
// ============================================================================

ElementRegistry* element_registry_create(Arena* arena) {
    ElementRegistry* reg = (ElementRegistry*)arena_alloc(arena, sizeof(ElementRegistry));
    if (!reg) return NULL;
    
    reg->arena = arena;
    reg->next_id = 1;
    
    // Create hashmap for element references
    reg->refs = hashmap_new(sizeof(ElementRef), 64, 0, 0,
        [](const void* item, uint64_t seed0, uint64_t seed1) -> uint64_t {
            const ElementRef* ref = (const ElementRef*)item;
            return hashmap_murmur(ref->id, strlen(ref->id), seed0, seed1);
        },
        [](const void* a, const void* b, void* udata) -> int {
            const ElementRef* ref_a = (const ElementRef*)a;
            const ElementRef* ref_b = (const ElementRef*)b;
            return strcmp(ref_a->id, ref_b->id);
        },
        NULL, NULL);
    
    return reg;
}

const char* element_registry_add(ElementRegistry* reg, View* view, uint64_t doc_version) {
    if (!reg || !view) return NULL;
    
    ElementRef ref;
    generate_uuid(ref.id);
    ref.view = view;
    ref.document_version = doc_version;
    
    // Store in hashmap
    hashmap_set(reg->refs, &ref);
    
    // Return pointer to stored ID (hashmap makes a copy)
    ElementRef* stored = (ElementRef*)hashmap_get(reg->refs, &ref);
    return stored ? stored->id : NULL;
}

View* element_registry_get(ElementRegistry* reg, const char* id) {
    if (!reg || !id) return NULL;
    
    ElementRef key;
    strncpy(key.id, id, WD_ELEMENT_ID_LEN - 1);
    key.id[WD_ELEMENT_ID_LEN - 1] = '\0';
    
    ElementRef* ref = (ElementRef*)hashmap_get(reg->refs, &key);
    return ref ? ref->view : NULL;
}

bool element_registry_is_stale(ElementRegistry* reg, const char* id, uint64_t current_version) {
    if (!reg || !id) return true;
    
    ElementRef key;
    strncpy(key.id, id, WD_ELEMENT_ID_LEN - 1);
    key.id[WD_ELEMENT_ID_LEN - 1] = '\0';
    
    ElementRef* ref = (ElementRef*)hashmap_get(reg->refs, &key);
    if (!ref) return true;
    
    return ref->document_version != current_version;
}

void element_registry_clear(ElementRegistry* reg) {
    if (!reg || !reg->refs) return;
    hashmap_clear(reg->refs, false);
}

// ============================================================================
// Session Lifecycle
// ============================================================================

WebDriverSession* webdriver_session_create(int width, int height, bool headless) {
    Pool* pool = pool_create();
    if (!pool) {
        log_error("webdriver: failed to create session pool");
        return NULL;
    }
    
    Arena* arena = arena_create(pool, 64 * 1024, 256 * 1024);
    if (!arena) {
        log_error("webdriver: failed to create session arena");
        pool_destroy(pool);
        return NULL;
    }
    
    WebDriverSession* session = (WebDriverSession*)arena_alloc(arena, sizeof(WebDriverSession));
    if (!session) {
        log_error("webdriver: failed to allocate session");
        pool_destroy(pool);
        return NULL;
    }
    
    memset(session, 0, sizeof(WebDriverSession));
    session->arena = arena;
    session->pool = pool;
    
    // Generate session ID
    generate_uuid(session->id);
    
    // Initialize timeouts (W3C defaults)
    session->implicit_wait_ms = 0;
    session->page_load_ms = 300000;  // 5 minutes
    session->script_ms = 30000;      // 30 seconds (not used)
    
    // Window state
    session->window_width = width;
    session->window_height = height;
    session->headless = headless;
    
    // Create element registry
    session->elements = element_registry_create(arena);
    if (!session->elements) {
        log_error("webdriver: failed to create element registry");
        pool_destroy(pool);
        return NULL;
    }
    
    // Create UI context
    session->uicon = (UiContext*)arena_alloc(arena, sizeof(UiContext));
    if (!session->uicon) {
        log_error("webdriver: failed to allocate UI context");
        pool_destroy(pool);
        return NULL;
    }
    
    memset(session->uicon, 0, sizeof(UiContext));
    
    // Initialize headless context
    if (ui_context_init(session->uicon, headless) != 0) {
        log_error("webdriver: failed to initialize UI context");
        pool_destroy(pool);
        return NULL;
    }
    
    // Create render surface
    ui_context_create_surface(session->uicon, width, height);
    
    session->document_version = 0;
    
    log_info("webdriver: session created id=%s, %dx%d, headless=%d",
             session->id, width, height, headless);
    
    return session;
}

void webdriver_session_destroy(WebDriverSession* session) {
    if (!session) return;
    
    log_info("webdriver: destroying session %s", session->id);
    
    if (session->uicon) {
        ui_context_cleanup(session->uicon);
    }
    
    if (session->elements && session->elements->refs) {
        hashmap_free(session->elements->refs);
    }
    
    // Arena and pool will be freed together
    // Note: Need to save pool pointer before session is invalidated
    Pool* pool = session->pool;
    if (pool) {
        pool_destroy(pool);
    }
}

// ============================================================================
// Navigation
// ============================================================================

WebDriverError webdriver_session_navigate(WebDriverSession* session, const char* url) {
    if (!session || !url) return WD_ERROR_INVALID_ARGUMENT;
    
    log_info("webdriver: navigating to %s", url);
    
    // Load the document
    Url* base = url_parse(url);
    if (!base) {
        log_error("webdriver: failed to parse URL: %s", url);
        return WD_ERROR_INVALID_ARGUMENT;
    }
    
    DomDocument* doc = load_html_doc(base, (char*)url, 
                                      session->window_width, session->window_height);
    if (!doc) {
        log_error("webdriver: failed to load document: %s", url);
        return WD_ERROR_UNKNOWN_ERROR;
    }
    
    // Update session
    session->document = doc;
    session->document_version++;
    session->uicon->document = doc;
    
    // Clear old element references (they're now stale)
    element_registry_clear(session->elements);
    
    // Layout the document
    layout_html_doc(session->uicon, doc, false);
    
    log_info("webdriver: document loaded and laid out");
    
    return WD_SUCCESS;
}

const char* webdriver_session_get_url(WebDriverSession* session) {
    if (!session || !session->document || !session->document->url) {
        return "";
    }
    String* url_str = url_serialize(session->document->url);
    if (!url_str || !url_str->chars) {
        return "";
    }
    return url_str->chars;
}

const char* webdriver_session_get_title(WebDriverSession* session) {
    if (!session || !session->document) {
        return "";
    }
    
    // Find <title> element and extract text
    // For now, return empty - TODO: implement title extraction
    return "";
}

char* webdriver_session_get_source(WebDriverSession* session) {
    if (!session || !session->document) {
        return NULL;
    }
    
    // TODO: Serialize DOM to HTML string
    return NULL;
}
