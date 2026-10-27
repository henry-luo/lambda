#include "../io/mark_output_builder.hpp"
#include "../core/collection_storage.h"
#include "lambda-error.h"
#include "heap_api.h"

// Element output belongs to the runtime even when a host omits PDF encoding.
Item output_content_append(List* destination, Item value) {
    if (get_type_id(value) == LMD_TYPE_ERROR) return value;
    list_push_spread(destination, value);
    MarkOutputBuilder* builder = container_is_virtual_list(destination)
        ? virtual_output_builder(destination) : nullptr;
    return builder && builder->ops->error ? builder->ops->error(builder) : ItemNull;
}

uint64_t output_content_checkpoint(List* destination) {
    MarkOutputBuilder* builder = container_is_virtual_list(destination)
        ? virtual_output_builder(destination) : nullptr;
    return builder && builder->ops->size ? builder->ops->size(builder) : 0;
}

void output_content_rewind(List* destination, uint64_t size) {
    MarkOutputBuilder* builder = container_is_virtual_list(destination)
        ? virtual_output_builder(destination) : nullptr;
    if (builder && builder->ops->rewind) builder->ops->rewind(builder, (size_t)size);
}

Bool output_content_is_virtual(List* destination) {
    return destination && container_is_virtual_list(destination) ? BOOL_TRUE : BOOL_FALSE;
}

void elmt_content_begin(Element* element) {
    if (!element || !is_file_element_type((TypeElmt*)element->type) ||
            !element->is_heap || container_is_virtual_list(element)) return;
    Item format = {.item = element->get_attr("format").item};
    TypeId type = get_type_id(format);
    if (!is_text_type_id(type)) return;
    const char* chars = format.get_chars();
    size_t length = type == LMD_TYPE_STRING ? format.get_safe_string()->len
        : format.get_safe_symbol()->len;
    if (length != 3 || memcmp(chars, "pdf", 3) != 0) return;

#ifdef LAMBDA_NO_PDF
    set_runtime_error(ERR_INVALID_OPERATION, "PDF output is unavailable in this build");
#else
    // Reserve the virtual builder only after the evaluated format selects PDF.
    VirtualOutputElement* output = static_cast<VirtualOutputElement*>(element);
    output->builder = pdf_builder_create();
    element->is_virtual = 1;
    if (!output->builder) set_runtime_error(ERR_OUT_OF_MEMORY, "PDF builder allocation failed");
#endif
}
