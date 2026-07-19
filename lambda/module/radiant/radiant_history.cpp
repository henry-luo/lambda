#include "radiant_history.hpp"
#include "radiant_host_api.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../../lib/arraylist.h"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"
#include "../../../lib/url.h"
#include <string.h>

typedef struct RadiantHistoryEntry {
    Item state;
    Url* url;
    bool rooted;
} RadiantHistoryEntry;

typedef struct RadiantHistoryState {
    DomDocument* document;
    ArrayList* entries;
    int index;
    bool manual_scroll_restoration;
} RadiantHistoryState;

static Url* history_copy_url(const Url* url) {
    const char* href = url ? url_get_href((Url*)url) : nullptr;
    return href ? url_parse(href) : nullptr;
}

static void history_entry_destroy(RadiantHistoryEntry* entry) {
    if (!entry) return;
    if (entry->rooted && radiant_host_api && radiant_host_api->gc) {
        radiant_host_api->gc->unregister_root(&entry->state.item);
    }
    if (entry->url) url_destroy(entry->url);
    mem_free(entry);
}

static void history_state_destroy(void* data) {
    RadiantHistoryState* history = (RadiantHistoryState*)data;
    if (!history) return;
    if (history->entries) {
        for (int i = 0; i < history->entries->length; i++) {
            history_entry_destroy((RadiantHistoryEntry*)arraylist_get(history->entries, i));
        }
        arraylist_free(history->entries);
    }
    mem_free(history);
}

static RadiantHistoryState* history_find(DomDocument* document) {
    if (!document) return nullptr;
    for (DomDocumentResource* resource = document->resources;
         resource; resource = resource->next) {
        if (resource->destroy == history_state_destroy) {
            return (RadiantHistoryState*)resource->data;
        }
    }
    return nullptr;
}

static RadiantHistoryEntry* history_entry_create(Item state, Url* url) {
    if (!url) return nullptr;
    RadiantHistoryEntry* entry = (RadiantHistoryEntry*)mem_calloc(
        1, sizeof(RadiantHistoryEntry), MEM_CAT_JS_RUNTIME);
    if (!entry) {
        url_destroy(url);
        return nullptr;
    }
    entry->state = state;
    entry->url = url;
    if (radiant_host_api && radiant_host_api->gc) {
        radiant_host_api->gc->register_root(&entry->state.item);
        entry->rooted = true;
    }
    return entry;
}

static bool history_seed_initial_entry(RadiantHistoryState* history,
                                       DomDocument* document) {
    if (!history || !history->entries || history->entries->length > 0 ||
        !document || !document->url) return false;
    RadiantHistoryEntry* initial = history_entry_create(
        ItemNull, history_copy_url(document->url));
    if (!initial || !arraylist_append(history->entries, initial)) {
        history_entry_destroy(initial);
        return false;
    }
    history->index = 0;
    return true;
}

static RadiantHistoryState* history_get(DomDocument* document) {
    RadiantHistoryState* history = history_find(document);
    if (history) {
        // The JS realm can bind the document before its final URL is assigned;
        // seed the retained history state once that URL becomes available.
        history_seed_initial_entry(history, document);
        return history;
    }
    if (!document) return nullptr;

    history = (RadiantHistoryState*)mem_calloc(
        1, sizeof(RadiantHistoryState), MEM_CAT_JS_RUNTIME);
    if (!history) return nullptr;
    history->document = document;
    history->entries = arraylist_new(4);
    history->index = 0;
    if (!history->entries ||
        !dom_document_add_resource(document, history, history_state_destroy)) {
        if (history->entries) arraylist_free(history->entries);
        mem_free(history);
        return nullptr;
    }

    history_seed_initial_entry(history, document);
    return history;
}

static RadiantHistoryEntry* history_current(RadiantHistoryState* history) {
    if (!history || !history->entries || history->index < 0 ||
        history->index >= history->entries->length) return nullptr;
    return (RadiantHistoryEntry*)arraylist_get(history->entries, history->index);
}

static Url* history_resolve_url(DomDocument* document, const char* url_text) {
    if (!document) return nullptr;
    if (!url_text || !url_text[0]) return history_copy_url(document->url);
    Url* resolved = document->url
        ? url_parse_with_base(url_text, document->url)
        : url_parse(url_text);
    if (!resolved || !url_is_valid(resolved)) {
        if (resolved) url_destroy(resolved);
        return nullptr;
    }
    return resolved;
}

static bool history_apply_document_url(DomDocument* document, const Url* url) {
    Url* copy = history_copy_url(url);
    if (!document || !copy) return false;
    if (!document->url) {
        document->url = copy;
        return true;
    }
    // The loader and Input retain this Url address while scripts run, so swap
    // owned contents in place instead of leaving those aliases dangling.
    Url previous = *document->url;
    *document->url = *copy;
    *copy = previous;
    url_destroy(copy);
    return true;
}

static void history_truncate_forward(RadiantHistoryState* history) {
    if (!history || !history->entries) return;
    for (int i = history->entries->length - 1; i > history->index; i--) {
        RadiantHistoryEntry* entry =
            (RadiantHistoryEntry*)arraylist_get(history->entries, i);
        arraylist_remove(history->entries, i);
        history_entry_destroy(entry);
    }
}

static const char* history_url_part(const char* value) {
    return value ? value : "";
}

static bool history_urls_differ_only_by_fragment(const Url* current, const Url* next) {
    if (!current || !next) return false;
    return strcmp(history_url_part(url_get_protocol((Url*)current)),
                  history_url_part(url_get_protocol((Url*)next))) == 0 &&
           strcmp(history_url_part(url_get_host((Url*)current)),
                  history_url_part(url_get_host((Url*)next))) == 0 &&
           strcmp(history_url_part(url_get_pathname((Url*)current)),
                  history_url_part(url_get_pathname((Url*)next))) == 0 &&
           strcmp(history_url_part(url_get_search((Url*)current)),
                  history_url_part(url_get_search((Url*)next))) == 0;
}

static bool history_fragments_differ(const Url* first, const Url* second) {
    return strcmp(history_url_part(first ? url_get_hash((Url*)first) : nullptr),
                  history_url_part(second ? url_get_hash((Url*)second) : nullptr)) != 0;
}

extern "C" bool radiant_history_initialize(DomDocument* document) {
    RadiantHistoryState* history = history_get(document);
    return history && history->entries && history->entries->length > 0;
}

extern "C" int radiant_history_length(DomDocument* document) {
    RadiantHistoryState* history = history_get(document);
    return history && history->entries ? history->entries->length : 0;
}

extern "C" Item radiant_history_state(DomDocument* document) {
    RadiantHistoryEntry* entry = history_current(history_get(document));
    return entry ? entry->state : ItemNull;
}

extern "C" const char* radiant_history_scroll_restoration(DomDocument* document) {
    RadiantHistoryState* history = history_get(document);
    return history && history->manual_scroll_restoration ? "manual" : "auto";
}

extern "C" void radiant_history_set_scroll_restoration(
    DomDocument* document, const char* value) {
    RadiantHistoryState* history = history_get(document);
    if (!history || !value) return;
    if (strcmp(value, "manual") == 0) history->manual_scroll_restoration = true;
    else if (strcmp(value, "auto") == 0) history->manual_scroll_restoration = false;
}

extern "C" bool radiant_history_push_state(
    DomDocument* document, Item cloned_state, const char* url_text) {
    RadiantHistoryState* history = history_get(document);
    Url* resolved = history_resolve_url(document, url_text);
    if (!history || !resolved) return false;

    RadiantHistoryEntry* entry = history_entry_create(cloned_state, resolved);
    if (!entry) return false;
    history_truncate_forward(history);
    if (!arraylist_append(history->entries, entry)) {
        history_entry_destroy(entry);
        return false;
    }
    history->index = history->entries->length - 1;
    history_apply_document_url(document, entry->url);
    return true;
}

extern "C" bool radiant_history_replace_state(
    DomDocument* document, Item cloned_state, const char* url_text) {
    RadiantHistoryState* history = history_get(document);
    Url* resolved = history_resolve_url(document, url_text);
    RadiantHistoryEntry* current = history_current(history);
    if (!history || !resolved || !current) {
        if (resolved) url_destroy(resolved);
        return false;
    }

    RadiantHistoryEntry* replacement = history_entry_create(cloned_state, resolved);
    if (!replacement) return false;
    arraylist_set(history->entries, history->index, replacement);
    history_entry_destroy(current);
    history_apply_document_url(document, replacement->url);
    return true;
}

extern "C" bool radiant_history_go(
    DomDocument* document, int delta, RadiantHistoryTraversal* traversal) {
    RadiantHistoryState* history = history_get(document);
    RadiantHistoryEntry* old_entry = history_current(history);
    if (!history || !old_entry || delta == 0) return false;
    int next_index = history->index + delta;
    if (next_index < 0 || next_index >= history->entries->length) return false;

    RadiantHistoryEntry* next_entry =
        (RadiantHistoryEntry*)arraylist_get(history->entries, next_index);
    if (!next_entry || !history_apply_document_url(document, next_entry->url)) return false;
    history->index = next_index;
    if (traversal) {
        traversal->state = next_entry->state;
        traversal->old_url = url_get_href(old_entry->url);
        traversal->new_url = url_get_href(next_entry->url);
        traversal->hash_changed = history_fragments_differ(old_entry->url, next_entry->url);
    }
    return true;
}

extern "C" bool radiant_history_set_location(
    DomDocument* document, const char* url_text, RadiantHistoryTraversal* traversal) {
    RadiantHistoryState* history = history_get(document);
    RadiantHistoryEntry* current = history_current(history);
    Url* resolved = history_resolve_url(document, url_text);
    if (!history || !current || !resolved) {
        if (resolved) url_destroy(resolved);
        return false;
    }
    if (!history_urls_differ_only_by_fragment(current->url, resolved)) {
        log_info("dom-history: ignored cross-document location assignment to %s",
                 url_text ? url_text : "");
        url_destroy(resolved);
        return false;
    }
    if (!history_fragments_differ(current->url, resolved)) {
        url_destroy(resolved);
        return false;
    }

    RadiantHistoryEntry* entry = history_entry_create(ItemNull, resolved);
    if (!entry) return false;
    history_truncate_forward(history);
    if (!arraylist_append(history->entries, entry)) {
        history_entry_destroy(entry);
        return false;
    }
    history->index = history->entries->length - 1;
    if (!history_apply_document_url(document, entry->url)) return false;
    if (traversal) {
        traversal->state = ItemNull;
        traversal->old_url = url_get_href(current->url);
        traversal->new_url = url_get_href(entry->url);
        traversal->hash_changed = true;
    }
    return true;
}
