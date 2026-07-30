#include "event.hpp"

#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <string.h>

struct EditingActionRegistryEntry {
    EditingActionRegistration registration;
    char* owned_handler_id;
};

struct EditingActionRegistry {
    DomDocument* document;
    ArrayList* entries;
    uint64_t next_registration_id;
    uint64_t generation;
    uint32_t dispatch_depth;
    bool destroying;
};

static void editing_action_registry_free_entry(EditingActionRegistryEntry* entry) {
    if (!entry) return;
    if (entry->registration.destroy_user) {
        entry->registration.destroy_user(entry->registration.user);
    }
    mem_free(entry->owned_handler_id);
    mem_free(entry);
}

static void editing_action_registry_reclaim(EditingActionRegistry* registry) {
    if (!registry || !registry->entries || registry->dispatch_depth != 0) return;
    for (int index = registry->entries->length - 1; index >= 0; index--) {
        EditingActionRegistryEntry* entry = (EditingActionRegistryEntry*)
            arraylist_get(registry->entries, index);
        if (!entry || !entry->registration.tombstoned) continue;
        arraylist_remove(registry->entries, index);
        editing_action_registry_free_entry(entry);
    }
}

static void editing_action_registry_destroy(void* data) {
    EditingActionRegistry* registry = (EditingActionRegistry*)data;
    if (!registry) return;
    registry->destroying = true;
    registry->dispatch_depth = 0;
    if (registry->entries) {
        for (int index = 0; index < registry->entries->length; index++) {
            editing_action_registry_free_entry((EditingActionRegistryEntry*)
                arraylist_get(registry->entries, index));
        }
        arraylist_free(registry->entries);
    }
    if (registry->document &&
        registry->document->editing_action_registry == registry) {
        registry->document->editing_action_registry = nullptr;
    }
    mem_free(registry);
}

EditingActionRegistry* editing_action_registry_get(DomDocument* document) {
    if (!document) return nullptr;
    EditingActionRegistry* existing = (EditingActionRegistry*)
        document->editing_action_registry;
    if (existing) return existing;

    EditingActionRegistry* registry = (EditingActionRegistry*)mem_calloc(
        1, sizeof(EditingActionRegistry), MEM_CAT_LAYOUT);
    if (!registry) return nullptr;
    registry->document = document;
    registry->entries = arraylist_new(4);
    registry->next_registration_id = 1;
    registry->generation = 1;
    if (!registry->entries || !dom_document_add_resource(
            document, registry, editing_action_registry_destroy)) {
        if (registry->entries) arraylist_free(registry->entries);
        mem_free(registry);
        return nullptr;
    }
    document->editing_action_registry = registry;
    return registry;
}

uint64_t editing_action_registry_register(
        DomDocument* document, const EditingActionRegistration* registration) {
    if (!document || !registration || !registration->handler_id ||
        !registration->handler_id[0] || !registration->route_mask ||
        !registration->matches || !registration->handle) {
        return 0;
    }
    EditingActionRegistry* registry = editing_action_registry_get(document);
    if (!registry || registry->destroying) return 0;

    for (int index = 0; index < registry->entries->length; index++) {
        EditingActionRegistryEntry* current = (EditingActionRegistryEntry*)
            arraylist_get(registry->entries, index);
        if (!current || current->registration.tombstoned) continue;
        if (current->registration.route_mask == registration->route_mask &&
            strcmp(current->registration.handler_id, registration->handler_id) == 0) {
            return current->registration.registration_id;
        }
    }

    EditingActionRegistryEntry* entry = (EditingActionRegistryEntry*)mem_calloc(
        1, sizeof(EditingActionRegistryEntry), MEM_CAT_LAYOUT);
    if (!entry) return 0;
    entry->owned_handler_id = mem_strdup(registration->handler_id, MEM_CAT_LAYOUT);
    if (!entry->owned_handler_id) {
        mem_free(entry);
        return 0;
    }
    entry->registration = *registration;
    entry->registration.registration_id = registry->next_registration_id++;
    if (registry->next_registration_id == 0) registry->next_registration_id = 1;
    entry->registration.handler_id = entry->owned_handler_id;
    entry->registration.tombstoned = false;
    if (!arraylist_append(registry->entries, entry)) {
        editing_action_registry_free_entry(entry);
        return 0;
    }
    registry->generation++;
    return entry->registration.registration_id;
}

bool editing_action_registry_unregister(DomDocument* document,
                                        uint64_t registration_id) {
    if (!document || !registration_id) return false;
    EditingActionRegistry* registry = (EditingActionRegistry*)
        document->editing_action_registry;
    if (!registry || registry->destroying || !registry->entries) return false;
    for (int index = 0; index < registry->entries->length; index++) {
        EditingActionRegistryEntry* entry = (EditingActionRegistryEntry*)
            arraylist_get(registry->entries, index);
        if (!entry || entry->registration.registration_id != registration_id ||
            entry->registration.tombstoned) {
            continue;
        }
        entry->registration.tombstoned = true;
        registry->generation++;
        editing_action_registry_reclaim(registry);
        return true;
    }
    return false;
}

static bool editing_action_registry_route_matches(
        const EditingActionRegistration* registration,
        const EditingPreparedTransaction* prepared) {
    if (!registration || !prepared || prepared->route.kind <= EDITING_ROUTE_NONE ||
        prepared->route.kind > EDITING_ROUTE_RADIANT_TEMPLATE) {
        return false;
    }
    uint32_t route_bit = 1u << prepared->route.kind;
    return (registration->route_mask & route_bit) != 0;
}

bool editing_action_registry_select(DomDocument* document,
                                    const EditingPreparedTransaction* prepared,
                                    EditingActionSnapshot* out_snapshot,
                                    bool* out_configuration_error) {
    if (out_snapshot) memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (out_configuration_error) *out_configuration_error = false;
    if (!document || !prepared || !out_snapshot) return false;
    EditingActionRegistry* registry = (EditingActionRegistry*)
        document->editing_action_registry;
    if (!registry || registry->destroying || !registry->entries) return false;

    EditingActionRegistryEntry* selected = nullptr;
    for (int index = 0; index < registry->entries->length; index++) {
        EditingActionRegistryEntry* entry = (EditingActionRegistryEntry*)
            arraylist_get(registry->entries, index);
        if (!entry || entry->registration.tombstoned ||
            !editing_action_registry_route_matches(&entry->registration, prepared) ||
            !entry->registration.matches(prepared, entry->registration.user)) {
            continue;
        }
        if (!selected || entry->registration.priority > selected->registration.priority) {
            selected = entry;
            continue;
        }
        if (entry->registration.priority == selected->registration.priority) {
            log_error("editing_action_registry: ambiguous handlers '%s' and '%s' at priority %d",
                      selected->registration.handler_id, entry->registration.handler_id,
                      entry->registration.priority);
            if (out_configuration_error) *out_configuration_error = true;
            return false;
        }
    }
    if (!selected) return false;

    // Tombstones are retained until this snapshot is released, so callbacks
    // may unregister themselves or competitors during beforeinput safely.
    registry->dispatch_depth++;
    out_snapshot->registry = registry;
    out_snapshot->entry = selected;
    out_snapshot->registration_id = selected->registration.registration_id;
    out_snapshot->handler_id = selected->registration.handler_id;
    return true;
}

EditingActionOutcome editing_action_snapshot_invoke(
        EditingActionSnapshot* snapshot, EventContext* evcon,
        const EditingPreparedTransaction* prepared) {
    EditingActionOutcome outcome = {EDITING_ACTION_ERROR, false, false};
    if (!snapshot || !snapshot->registry || !snapshot->entry ||
        snapshot->registry->destroying) {
        return outcome;
    }
    EditingActionRegistryEntry* entry = (EditingActionRegistryEntry*)snapshot->entry;
    if (entry->registration.registration_id != snapshot->registration_id ||
        !entry->registration.handle) {
        return outcome;
    }
    return entry->registration.handle(evcon, prepared, entry->registration.user);
}

void editing_action_snapshot_release(EditingActionSnapshot* snapshot) {
    if (!snapshot || !snapshot->registry) return;
    EditingActionRegistry* registry = snapshot->registry;
    if (registry->dispatch_depth > 0) registry->dispatch_depth--;
    editing_action_registry_reclaim(registry);
    memset(snapshot, 0, sizeof(*snapshot));
}

uint64_t editing_action_registry_generation(const DomDocument* document) {
    const EditingActionRegistry* registry = document
        ? (const EditingActionRegistry*)document->editing_action_registry
        : nullptr;
    return registry ? registry->generation : 0;
}
