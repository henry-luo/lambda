#pragma once

#include "../lambda-data.hpp"

// Output builders own bytes, never retained Items. Host-readable virtual
// containers (VArray/Velmt) have a separate contract (D7.4.5v2).
struct MarkOutputBuilder;
struct MarkOutputBuilderOps {
    void (*append)(MarkOutputBuilder* builder, Item item);
    bool (*finish)(MarkOutputBuilder* builder);
    bool (*bytes)(const MarkOutputBuilder* builder, const char** data, size_t* size);
    void (*destroy)(MarkOutputBuilder* builder);
};

struct MarkOutputBuilder {
    const MarkOutputBuilderOps* ops;
};

// Element already extends List; preserve that ancestry and its complete prefix.
struct VirtualOutputElement : Element {
    MarkOutputBuilder* builder;
};

inline bool is_file_element_type(const TypeElmt* type) {
    return type && type->name.length == 4 &&
        memcmp(type->name.str, "file", 4) == 0;
}

inline MarkOutputBuilder* virtual_output_builder(List* list) {
    return static_cast<VirtualOutputElement*>(static_cast<Element*>(list))->builder;
}

inline bool virtual_output_bytes(Item value, const char** data, size_t* size) {
    if (get_type_id(value) != LMD_TYPE_ELEMENT ||
            !container_is_virtual_list(value.container)) return false;
    MarkOutputBuilder* builder = virtual_output_builder(value.element);
    return builder && builder->ops->bytes(builder, data, size);
}

MarkOutputBuilder* pdf_builder_create();

