#include "radiant_input_value.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../runtime/root_vector.h"
#include "../../input/css/dom_element.hpp"
#include "../../../lib/arraylist.h"
#include "../../../lib/mem.h"

typedef struct RadiantInputFileEntry {
    DomElement* element;
    Item files;
    RootVector roots;
} RadiantInputFileEntry;

typedef struct RadiantInputFileState : DomDocumentResourceData {
    ArrayList* entries;
} RadiantInputFileState;

static void rif_entry_destroy(RadiantInputFileEntry* entry) {
    if (!entry) return;
    root_vector_destroy(&entry->roots);
    mem_free(entry);
}

static void rif_state_destroy(DomDocumentResourceData* data) {
    RadiantInputFileState* state = (RadiantInputFileState*)data;
    for (int i = 0; state->entries && i < state->entries->length; i++)
        rif_entry_destroy((RadiantInputFileEntry*)arraylist_get(state->entries, i));
    if (state->entries) arraylist_free(state->entries);
    mem_free(state);
}

static void rif_state_retire_runtime(DomDocumentResourceData* data) {
    RadiantInputFileState* state = (RadiantInputFileState*)data;
    for (int i = 0; state->entries && i < state->entries->length; i++) {
        RadiantInputFileEntry* entry = (RadiantInputFileEntry*)arraylist_get(state->entries, i);
        root_vector_destroy(&entry->roots);
        entry->files = ItemNull;
    }
}

static RadiantInputFileState* rif_state_get(DomDocument* document, bool create) {
    if (!document) return nullptr;
    for (DomDocumentResource* resource = document->resources; resource; resource = resource->next) {
        if (resource->destroy == rif_state_destroy) return (RadiantInputFileState*)resource->data;
    }
    if (!create) return nullptr;
    RadiantInputFileState* state = (RadiantInputFileState*)mem_calloc(
        1, sizeof(RadiantInputFileState), MEM_CAT_JS_RUNTIME);
    if (!state) return nullptr;
    state->entries = arraylist_new(8);
    if (!state->entries || !dom_document_add_resource(document, state, rif_state_destroy)) {
        if (state->entries) arraylist_free(state->entries);
        mem_free(state);
        return nullptr;
    }
    return state;
}

static RadiantInputFileEntry* rif_entry_get(DomElement* element) {
    // File access previously initialized live text in the same entry.
    if (!element || !radiant_input_ensure_live_value(element)) return nullptr;
    RadiantInputFileState* state = rif_state_get(element->doc, true);
    if (!state) return nullptr;
    for (int i = 0; i < state->entries->length; i++) {
        RadiantInputFileEntry* entry = (RadiantInputFileEntry*)arraylist_get(state->entries, i);
        if (entry && entry->element == element) return entry;
    }
    RadiantInputFileEntry* entry = (RadiantInputFileEntry*)mem_calloc(
        1, sizeof(RadiantInputFileEntry), MEM_CAT_JS_RUNTIME);
    if (!entry) return nullptr;
    entry->element = element;
    if (!arraylist_append(state->entries, entry)) {
        rif_entry_destroy(entry);
        return nullptr;
    }
    return entry;
}

extern "C" Item radiant_input_files(DomElement* element) {
    RadiantInputFileEntry* entry = rif_entry_get(element);
    Item* value = entry ? root_vector_external_at(&entry->roots, 0) : nullptr;
    return value ? *value : ItemNull;
}

extern "C" void radiant_input_set_files(DomElement* element, Item files) {
    RadiantInputFileEntry* entry = rif_entry_get(element);
    if (!entry) return;
    if (files.item == ItemNull.item) {
        root_vector_destroy(&entry->roots);
        entry->files = ItemNull;
        return;
    }
    RadiantInputFileState* state = rif_state_get(element->doc, false);
    Context* owner = radiant_dom_resource_owner(element->doc);
    if (!owner || !dom_document_bind_resource_runtime(element->doc, state, owner, rif_state_retire_runtime)) return;
    root_vector_bind_external(&entry->roots, owner, &entry->files, 1, "document input files");
    if (root_vector_ensure_external(&entry->roots)) entry->files = files;
}
