#include "input.hpp"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "../../lib/image_psd.h"

namespace {
struct PsdInput {
    MarkBuilder builder;
    PsdDocument* document = psd_document_create();
    const char* error = nullptr;
    explicit PsdInput(Input* input) : builder(input) {}
    ~PsdInput() { psd_document_destroy(document); }

    Item position(int64_t left, int64_t top) {
        return builder.array().append(left).append(top).final();
    }
    Item raster(const RasterBuffer* pixels, bool profile) {
        const char* const gray[] = {"gray", "a"};
        const char* const rgb[] = {"r", "g", "b", "a"};
        auto channels = builder.array();
        for (uint8_t c = 0; c < pixels->channels; c++) channels.append(pixels->channels <= 2 ? gray[c] : rgb[c]);
        int64_t shape[] = {pixels->height, pixels->width, pixels->channels};
        Item samples = builder.createNumericArray(ELEM_UINT8, 3, shape, pixels->pixels);
        if (get_type_id(samples) != LMD_TYPE_ARRAY_NUM) { error = "cannot allocate PSD raster samples"; return ItemError; }
        return builder.element("raster").attr("width", (int64_t)pixels->width)
            .attr("height", (int64_t)pixels->height).attr("sample_type", "u8")
            .attr("channels", channels.final()).attr("color", profile ? "unknown" : "srgb")
            .attr("alpha_mode", pixels->channels == 4 ? "straight" : "none").attr("pixels", samples).final();
    }
    Item stack(int* index, PsdLayer* group, unsigned depth) {
        if (depth > 64) { error = "PSD group nesting limit exceeded"; return ItemError; }
        auto result = builder.element("stack");
        if (group) attributes(result, group, *index + 1, true);
        while (*index >= 0) {
            int source_index = (*index)--;
            auto* layer = (PsdLayer*)document->layers->data[source_index];
            if (layer->section == 3) {
                if (!group) error = "unexpected PSD group end marker";
                return error ? ItemError : result.final();
            }
            if (layer->section) result.child(stack(index, layer, depth + 1));
            else {
                auto node = builder.element("layer");
                attributes(node, layer, source_index, false);
                node.attr("kind", layer->unsupported ? "unsupported" : layer->pixels ? "raster" : "empty");
                if (layer->clipping) node.attr("clipping", true);
                if (layer->pixels) node.child(raster(layer->pixels, document->profile_length != 0));
                if (layer->mask) {
                    int64_t left = layer->mask_left, top = layer->mask_top;
                    if (!(layer->mask_flags & 1)) { left -= layer->left; top -= layer->top; }
                    node.child(builder.element("mask").attr("role", "alpha").attr("position", position(left, top))
                        .attr("default", layer->mask_default / 255.0).attr("enabled", !(layer->mask_flags & 2))
                        .attr("invert", (bool)(layer->mask_flags & 4)).child(raster(layer->mask, false)).final());
                }
                result.child(node.final());
            }
            if (error) return ItemError;
        }
        if (group) error = "PSD group is missing its end marker";
        return error ? ItemError : result.final();
    }
    void attributes(ElementBuilder& node, const PsdLayer* layer, int index, bool group) {
        node.attr("id", (int64_t)(layer->id ? layer->id : (uint32_t)index + 1))
            .attr("source_index", index).attr("name", {.item = s2it(builder.createString(layer->name, layer->name_length))})
            .attr("opacity", layer->opacity / 255.0).attr("visible", !(layer->flags & 2))
            .attr("unsupported", layer->unsupported);
        node.attr("blend", !strcmp(layer->blend, "norm") || (group && !strcmp(layer->blend, "pass")) ? "normal" : layer->blend);
        if (group) node.attr("isolation", !strcmp(layer->blend, "pass") ? "pass_through" : "isolated");
        else node.attr("position", position(layer->left, layer->top));
    }
};
}

void parse_psd(Input* input, const char* bytes, size_t length) {
    PsdInput parser(input);
    if (parser.document && psd_read(parser.document, (const uint8_t*)bytes, length, &parser.error)) {
        auto* doc = parser.document;
        auto image = parser.builder.element("image");
        image.attr("width", (int64_t)doc->width).attr("height", (int64_t)doc->height)
            .attr("plays", (int64_t)1).attr("unsupported", doc->unsupported);
        auto assets = parser.builder.element("assets");
        assets.child(parser.builder.element("asset").attr("id", "source").attr("format", "psd")
            .attr("data", {.item = x2it(parser.builder.createBinary(bytes, length))}).final());
        if (doc->profile_length) assets.child(parser.builder.element("profile").attr("id", "icc").attr("format", "icc")
            .attr("data", {.item = x2it(parser.builder.createBinary(doc->profile, doc->profile_length))}).final());
        image.child(assets.final());
        if (doc->composite) image.child(parser.builder.element("preview").child(parser.raster(doc->composite, doc->profile_length != 0)).final());
        int index = doc->layers->length - 1;
        if (index >= 0) image.child(parser.stack(&index, nullptr, 0));
        else image.child(parser.builder.element("stack").child(parser.builder.element("layer").attr("kind", "raster")
            .child(parser.raster(doc->composite, doc->profile_length != 0)).final()).final());
        if (!parser.error) { input->root = image.final(); return; }
    }
    input->root = ItemError; input->parse_failed = true;
    input->parse_error_message = pool_strdup(input->pool, parser.error ? parser.error : "cannot allocate PSD parser");
}
