#pragma once
#include "transpiler.hpp"

// root the pending field before key construction or any growing-container call.
struct RootedValueBuilder {
    RootFrame roots;
    Rooted<Item> value, field;
    explicit RootedValueBuilder(bool array = false) : roots(2),
        value(roots, array ? Item{.array = array_plain()} : vmap_new()), field(roots, ItemNull) {}
    explicit RootedValueBuilder(Item initial) : roots(2), value(roots, initial), field(roots, ItemNull) {}
    void put(const char* key, Item item) {
        field.set(item);
        Item name = {.item = s2it(heap_create_name(key, strlen(key)))};
        Item status = vmap_set(value.get(), name, field.get());
        if (item_is_error(status)) value.set(status);
    }
    void append(Item item) {
        field.set(item);
        array_push_verbatim(value.get().array, field.get());
    }
};
