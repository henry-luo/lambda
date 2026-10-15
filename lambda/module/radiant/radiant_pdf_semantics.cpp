#include "radiant_pdf_semantics.hpp"
#include "radiant_image_job.hpp"
#include "../../runtime/heap_api.h"
#include "../../../radiant/radiant.hpp"
#include "../../../radiant/page_document.hpp"
#include "../../../radiant/view_tree_model.hpp"
#include "../../../lib/str.h"
#include <math.h>

extern "C" Item vmap_new();

namespace {
Item pdf_text(const char* value) {
    return value ? Item{.item = s2it(heap_strcpy(value, strlen(value)))} : ItemNull;
}

const char* pdf_nearest_link(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        DomElement* element = current->as_element();
        if (element && element->tag_id == MARKUP_NAME_A) {
            const char* href = element->get_attribute("href");
            if (href && *href) return href;
        }
    }
    return nullptr;
}
}

void radiant_pdf_link(Rooted<Item>& links, DomNode* source, Rect rect) {
    const char* href = pdf_nearest_link(source);
    if (!href || !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) ||
        !isfinite(rect.height) || rect.width <= 0 || rect.height <= 0) return;
    RootFrame roots(1); Rooted<Item> entry(roots, vmap_new());
    radiant_rooted_obj_set(entry, "href", pdf_text(href));
    const char* names[] = {"x", "y", "width", "height"};
    const float values[] = {rect.x, rect.y, rect.width, rect.height};
    for (size_t i = 0; i < 4; i++) radiant_rooted_obj_set(entry, names[i], push_d(values[i]));
    array_push(links.get().array, entry.get());
}

void radiant_pdf_destination(Rooted<Item>& destinations, DomNode* source, float x, float y) {
    DomElement* element = source ? source->as_element() : nullptr;
    if (!element || !element->id || !*element->id || !isfinite(x) || !isfinite(y)) return;
    RootFrame roots(1); Rooted<Item> entry(roots, vmap_new());
    radiant_rooted_obj_set(entry, "id", pdf_text(element->id));
    radiant_rooted_obj_set(entry, "x", push_d(x)); radiant_rooted_obj_set(entry, "y", push_d(y));
    array_push(destinations.get().array, entry.get());
}

Item radiant_pdf_metadata(DomDocument* document) {
    RootFrame roots(4); Rooted<Item> result(roots, vmap_new()), info(roots, vmap_new()),
        outlines(roots, {.array = array_plain()}), entry(roots, ItemNull);
    radiant_rooted_obj_set(info, "Creator", pdf_text("Lambda Script Renderer"));
    radiant_rooted_obj_set(info, "Producer", pdf_text("Lambda PDF Renderer"));
    const char* title = document ? session_extract_title(document) : nullptr;
    if (title && *title) radiant_rooted_obj_set(info, "Title", pdf_text(title));
    // Walk the DOM iteratively so deeply nested source documents need no native recursion.
    DomNode* root = document ? document->root.get() : nullptr;
    for (DomNode* node = root; node;) {
        DomElement* element = node->as_element();
        if (element) {
            if (element->tag_id == MARKUP_NAME_META && element->parent &&
                    element->parent->as_element() && element->parent->as_element()->tag_id == MARKUP_NAME_HEAD) {
                const char* name = element->get_attribute("name");
                const char* content = element->get_attribute("content");
                const char* keys[] = {"author", "description", "keywords"};
                const char* fields[] = {"Author", "Subject", "Keywords"};
                for (size_t i = 0; name && content && *content && i < 3; i++)
                    if (str_icmp_cstr(name, keys[i]) == 0) radiant_rooted_obj_set(info, fields[i], pdf_text(content));
            }
            const char* heading = element->get_attribute("data-pdf-outline-title");
            if (heading && *heading) {
                const char* source = element->get_attribute("data-pdf-outline-level");
                char* end = nullptr; long level = source ? strtol(source, &end, 10) : 0;
                if (!element->id || !*element->id || level < 0 || level > 32 ||
                    (source && (!end || *end || end == source))) return radiant_pdf_error("invalid PDF outline attributes");
                entry.set(vmap_new());
                radiant_rooted_obj_set(entry, "title", pdf_text(heading));
                radiant_rooted_obj_set(entry, "destination", pdf_text(element->id));
                radiant_rooted_obj_set(entry, "level", {.item = i2it(level)});
                array_push(outlines.get().array, entry.get());
            }
            if (element->first_child) { node = element->first_child; continue; }
        }
        while (node != root && !node->next_sibling) node = node->parent;
        node = node == root ? nullptr : node->next_sibling.get();
    }
    radiant_rooted_obj_set(result, "info", info.get()); radiant_rooted_obj_set(result, "outlines", outlines.get());
    return result.get();
}

Item radiant_pdf_page_properties(const ViewPageBox* page) {
    RootFrame roots(3); Rooted<Item> result(roots, vmap_new()), boxes(roots, {.array = array_plain()}), values(roots, ItemNull);
    if (page && page->fixed && page->fixed->geometry.box_mask) {
        const RadiantFixedGeometry& geometry = page->fixed->geometry;
        const char* names[] = {"MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox"};
        for (size_t i = 0; i < RADIANT_SOURCE_BOX_COUNT; i++) if (geometry.box_mask & (1u << i)) {
            const RdtLogicalRect& rect = geometry.boxes[i];
            values.set({.array = array_plain()});
            const float coordinates[] = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
            for (float value : coordinates)
                array_push(values.get().array, push_d(value));
            RootFrame entry_roots(1); Rooted<Item> entry(entry_roots, vmap_new());
            radiant_rooted_obj_set(entry, "name", pdf_text(names[i])); radiant_rooted_obj_set(entry, "values", values.get());
            array_push(boxes.get().array, entry.get());
        }
        radiant_rooted_obj_set(result, "rotation", {.item = i2it(geometry.rotation)});
    }
    radiant_rooted_obj_set(result, "boxes", boxes.get());
    return result.get();
}
