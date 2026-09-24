#pragma once
// markup_name_classes.hpp — class bits of well-known markup names, by id.
//
// Classifying a tag against string lists (void elements, scope markers, ...)
// costs a string compare per list entry. Elements already carry
// TypeElmt::name_id: the name pool resolves every well-known spelling to its
// MarkupNameId record, Input pools included. A table indexed by that id
// answers in one load. It is built once by running the caller's string
// classifier over every well-known markup record, so an id answers exactly
// what its spelling would. Any other id -- NAME_ID_NONE, a custom name, a
// name from another catalog -- must fall back to the string classifier.

#include "well_known_markup_names.h"

struct MarkupNameClassTable {
    // markup name ids are the dense ordinals of segment 0 (1..count); an id at
    // or past the limit falls back like any other non-markup id
    enum { LIMIT = 1024 };
    uint32_t bits[LIMIT];

    explicit MarkupNameClassTable(uint32_t (*classify)(const char* name, size_t len)) {
        for (size_t i = 0; i < LIMIT; i++) bits[i] = 0;
        for (size_t i = 0; i < g_well_known_markup_name_count; i++) {
            const WellKnownNameRecord* rec = &g_well_known_markup_names[i];
            NameId id = rec->meta.name_id;
            if (id != NAME_ID_NONE && id < LIMIT) bits[id] = classify(rec->chars, rec->len);
        }
    }

    static bool is_markup_id(NameId id) { return id != NAME_ID_NONE && id < LIMIT; }

    // the class bits of `id`; false when it is not a markup name id
    bool lookup(NameId id, uint32_t* out) const {
        if (!is_markup_id(id)) return false;
        *out = bits[id];
        return true;
    }
};
