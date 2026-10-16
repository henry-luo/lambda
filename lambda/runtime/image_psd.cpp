#include "image_raster.hpp"
#include "value_builder.hpp"
#include "lambda-number-runtime.hpp"
#include "../../lib/image_psd.h"
#include <math.h>

struct PsdOwner {
    PsdDocument* document = psd_document_create();
    ~PsdOwner() { psd_document_destroy(document); }
};

static Item psd_text(const char* text, size_t length) {
    return {.item = s2it(heap_create_name(text, length))};
}

static Item psd_position(int64_t left, int64_t top) {
    RootedValueBuilder result(true);
    result.append({.item = i2it(left)}); result.append({.item = i2it(top)});
    return result.value.get();
}

static Item psd_take_raster(RasterBuffer** buffer, bool profile) {
    RasterBuffer* owned = *buffer;
    *buffer = nullptr;
    return owned ? lambda_raster_wrap(owned, psd_text(profile ? "unknown" : "srgb", profile ? 7 : 4)) : ItemNull;
}

static Item psd_nest_records(Item flat, int64_t* index, bool group, unsigned depth) {
    RootFrame roots(2);
    Rooted<Item> records(roots, flat), row(roots, ItemNull);
    RootedValueBuilder children(true);
    if (depth > 64) return lambda_image_error("PSD group nesting exceeds the limit");
    while (*index < records.get().array->length) {
        row.set(item_at(records.get(), (*index)++));
        Item kind = item_attr(row.get(), "kind");
        if (!strcmp(kind.get_chars(), "end"))
            return group ? children.value.get() : lambda_image_error("unexpected PSD group end marker");
        if (!strcmp(kind.get_chars(), "group")) {
            RootedValueBuilder node(row.get());
            node.put("children", psd_nest_records(records.get(), index, true, depth + 1));
            if (item_is_error(item_attr(node.value.get(), "children"))) return item_attr(node.value.get(), "children");
        }
        children.append(row.get());
    }
    return group ? lambda_image_error("PSD group is missing its end marker") : children.value.get();
}

Item lambda_image_decode_psd(const uint8_t* bytes, size_t length) {
    PsdOwner owner;
    const char* error = nullptr;
    if (!owner.document || !psd_read(owner.document, bytes, length, &error))
        return lambda_image_error(error ? error : "cannot allocate PSD parser");
    PsdDocument* doc = owner.document;
    RootedValueBuilder document, records(true);
    for (int i = doc->layers->length; i-- > 0;) {
        PsdLayer* layer = (PsdLayer*)doc->layers->data[i];
        RootedValueBuilder record;
        const char* kind = layer->section == 3 ? "end" : layer->section ? "group" :
            layer->unsupported ? "unsupported" : layer->pixels ? "raster" : "empty";
        record.put("kind", psd_text(kind, strlen(kind)));
        record.put("id", {.item = i2it(layer->id ? layer->id : (uint32_t)i + 1)});
        record.put("source_index", {.item = i2it(i)});
        record.put("name", psd_text(layer->name, layer->name_length));
        record.put("position", psd_position(layer->left, layer->top));
        record.put("opacity", push_d(layer->opacity / 255.0));
        record.put("visible", {.item = b2it(!(layer->flags & 2))});
        record.put("clipping", {.item = b2it(layer->clipping != 0)});
        const char* blend = !strcmp(layer->blend, "norm") ? "normal" : layer->blend;
        record.put("blend", psd_text(blend, strlen(blend)));
        const char* isolation = !strcmp(layer->blend, "pass") ? "pass_through" : "isolated";
        record.put("isolation", psd_text(isolation, strlen(isolation)));
        record.put("unsupported", {.item = b2it(layer->unsupported)});
        if (layer->pixels) {
            record.put("raster", psd_take_raster(&layer->pixels, doc->profile_length != 0));
        }
        if (layer->mask) {
            RootedValueBuilder mask;
            int64_t left = layer->mask_left, top = layer->mask_top;
            if (!(layer->mask_flags & 1)) { left -= layer->left; top -= layer->top; }
            mask.put("position", psd_position(left, top));
            mask.put("default", push_d(layer->mask_default / 255.0));
            mask.put("enabled", {.item = b2it(!(layer->mask_flags & 2))});
            mask.put("invert", {.item = b2it((layer->mask_flags & 4) != 0)});
            mask.put("raster", psd_take_raster(&layer->mask, false));
            record.put("mask", mask.value.get());
        }
        records.append(record.value.get());
    }
    document.put("format", psd_text("psd", 3));
    document.put("width", {.item = i2it(doc->width)});
    document.put("height", {.item = i2it(doc->height)});
    document.put("unsupported", {.item = b2it(doc->unsupported)});
    int64_t index = 0;
    document.put("records", psd_nest_records(records.value.get(), &index, false, 0));
    if (item_is_error(item_attr(document.value.get(), "records"))) return item_attr(document.value.get(), "records");
    if (doc->profile_length) {
        Binary* profile = heap_binary_from_bytes((const char*)doc->profile, (int64_t)doc->profile_length);
        if (!profile) return ItemError;
        document.put("profile", {.item = x2it(profile)});
    }
    if (doc->composite) document.put("preview", psd_take_raster(&doc->composite, doc->profile_length != 0));
    return document.value.get();
}

struct PsdCompile {
    PsdDocument* doc;
    const char* error = nullptr;
    bool fail(const char* message) { error = message; return false; }

    PsdLayer* record(Item value, uint32_t section) {
        if (doc->layers->length >= 4096) { fail("PSD layer count exceeds the limit"); return nullptr; }
        PsdLayer* layer = (PsdLayer*)pool_calloc(doc->pool, sizeof(PsdLayer));
        if (!layer || !arraylist_append(doc->layers, layer)) { fail("cannot allocate PSD layer record"); return nullptr; }
        layer->section = section; layer->opacity = 255; memcpy(layer->blend, "norm", 5);
        layer->id = (uint32_t)doc->layers->length;
        Item id = item_attr(value, "id"); int64_t number;
        if (lambda_item_to_int64_exact(id, &number) && number > 0 && number <= UINT32_MAX) layer->id = (uint32_t)number;
        Item name = item_attr(value, "name");
        const char* chars = is_text_type_id(get_type_id(name)) ? name.get_chars() : "";
        layer->name_length = is_text_type_id(get_type_id(name)) ? name.get_len() : 0;
        layer->name = (char*)pool_alloc(doc->pool, layer->name_length + 1);
        if (!layer->name) { fail("cannot allocate PSD name"); return nullptr; }
        memcpy(layer->name, chars, layer->name_length); layer->name[layer->name_length] = 0;
        double opacity;
        if (!lambda_image_number(value, "opacity", 1, &opacity) || opacity < 0 || opacity > 1) {
            fail("PSD layer opacity is invalid"); return nullptr;
        }
        layer->opacity = (uint8_t)lround(opacity * 255);
        Item visible = item_attr(value, "visible");
        layer->flags = get_type_id(visible) == LMD_TYPE_BOOL && !it2b(visible) ? 2 : 0;
        return layer;
    }

    bool compile(Item value, bool root, unsigned depth) {
        RootFrame roots(2);
        Rooted<Item> owner(roots, value), child(roots, ItemNull);
        if (depth > 64) return fail("PSD group nesting limit exceeded");
        bool group = lambda_image_tag(owner.get(), "stack");
        if (group) {
            if (!root && !record(owner.get(), 3)) return false;
            for (int64_t i = owner.get().element->length; i-- > 0;) {
                child.set(item_at(owner.get(), i));
                if (lambda_image_tag(child.get(), "mask") || lambda_image_tag(child.get(), "effect"))
                    return fail("PSD group masks/effects require a later export profile");
                if (!compile(child.get(), false, depth + 1)) return false;
            }
            return root || record(owner.get(), 1) != nullptr;
        }
        if (!lambda_image_tag(owner.get(), "layer")) return fail("PSD composition requires layers or stacks");
        PsdLayer* layer = record(owner.get(), 0);
        if (!layer) return false;
        int64_t left, top;
        if (!lambda_image_position(owner.get(), &left, &top)) return fail("invalid PSD layer position");
        layer->left = layer->right = (int32_t)left; layer->top = layer->bottom = (int32_t)top;
        Item kind = item_attr(owner.get(), "kind");
        bool empty = is_text_type_id(get_type_id(kind)) && !strcmp(kind.get_chars(), "empty");
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            child.set(item_at(owner.get(), i));
            const RasterBuffer* raster = lambda_raster_buffer(child.get());
            if (raster) {
                if (empty) return fail("empty PSD layer cannot have primary raster content");
                if (!lambda_image_setting(child.get(), "color", "srgb"))
                    return fail("PSD writer requires explicit sRGB conversion for every layer");
                if (layer->pixels) return fail("PSD layer has multiple primary rasters");
                layer->pixels = (RasterBuffer*)raster;
                layer->right = layer->left + (int32_t)raster->width; layer->bottom = layer->top + (int32_t)raster->height;
            } else if (lambda_image_tag(child.get(), "mask")) {
                if (layer->mask) return fail("PSD layer has multiple masks");
                int64_t x, y; double coverage;
                if (!lambda_image_position(child.get(), &x, &y) ||
                        !lambda_image_number(child.get(), "default", 1, &coverage) || coverage < 0 || coverage > 1)
                    return fail("invalid PSD mask position/default");
                layer->mask_left = (int32_t)(left + x); layer->mask_top = (int32_t)(top + y);
                layer->mask_default = (uint8_t)lround(coverage * 255);
                Item enabled = item_attr(child.get(), "enabled"), inverse = item_attr(child.get(), "invert");
                if (get_type_id(inverse) == LMD_TYPE_BOOL && it2b(inverse)) return fail("inverted PSD mask export is not qualified");
                layer->mask_flags = get_type_id(enabled) == LMD_TYPE_BOOL && !it2b(enabled) ? 2 : 0;
                for (int64_t m = 0; m < child.get().element->length; m++) {
                    const RasterBuffer* candidate = lambda_raster_buffer(item_at(child.get(), m));
                    if (!candidate || layer->mask) return fail("PSD mask requires exactly one Raster");
                    layer->mask = (RasterBuffer*)candidate;
                }
                if (!layer->mask || layer->mask->channels != 1) return fail("PSD mask requires a gray raster");
                layer->mask_right = layer->mask_left + (int32_t)layer->mask->width;
                layer->mask_bottom = layer->mask_top + (int32_t)layer->mask->height;
            } else return fail("PSD export requires raster/empty layers without retained effects");
        }
        return layer->pixels || empty || fail("PSD layer has no raster content");
    }
};

Item lambda_image_encode_psd(Item source, Item composite, int64_t frame) {
    RootFrame roots(3);
    Rooted<Item> image(roots, source), merged(roots, composite), stack(roots, ItemNull);
    const RasterBuffer* pixels = lambda_raster_buffer(merged.get());
    if (!pixels || pixels->channels != 4) return lambda_image_error("PSD export requires an RGBA8 composite");
    PsdOwner owner;
    if (!owner.document) return ItemError;
    PsdDocument* doc = owner.document;
    doc->owns_rasters = false; doc->width = pixels->width; doc->height = pixels->height;
    doc->composite = (RasterBuffer*)pixels;
    stack.set(image.get());
    if (lambda_image_tag(image.get(), "image")) {
        stack.set(ItemNull); int64_t index = 0;
        for (int64_t i = 0; i < image.get().element->length; i++) {
            Item node = item_at(image.get(), i);
            if (lambda_image_tag(node, "stack")) stack.set(node);
            else if (lambda_image_tag(node, "frame") && index++ == frame) stack.set(node);
        }
    }
    if (lambda_image_tag(stack.get(), "frame")) stack.set(item_at(stack.get(), 0));
    if (!lambda_image_tag(stack.get(), "stack")) return lambda_image_error("PSD export requires an image root stack");
    PsdCompile compiler = {doc};
    if (!compiler.compile(stack.get(), true, 0)) return lambda_image_error(compiler.error);
    StrBuf* output = strbuf_new();
    if (!output) return ItemError;
    const char* error = nullptr;
    bool valid = psd_write(doc, output, &error);
    Binary* result = valid ? heap_binary_from_bytes(output->str, (int64_t)output->length) : nullptr;
    strbuf_free(output);
    return valid ? (result ? Item{.item = x2it(result)} : ItemError) : lambda_image_error(error);
}
