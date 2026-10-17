#include "image_raster.hpp"
#include "transpiler.hpp"
#include "heap_api.h"
#include "lambda-number-runtime.hpp"
#include "lambda-error.h"
#include "gc/gc_heap.h"
#include "../jube/jube_registry.h"
#include "../core/mark_reader.hpp"
#include "../../lib/mem.h"
#include <math.h>

struct RasterValue {
    RasterBuffer* buffer;
    Item color, channels, tag;
    void* accounting;
};

Item lambda_image_error(const char* message) {
    log_error("IMAGE_RASTER: %s", message);
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}

static Item raster_name(const char* name) {
    return {.item = s2it(heap_create_name(name, strlen(name)))};
}

static void raster_destroy(void* data) {
    RasterValue* value = (RasterValue*)data;
    heap_gc_external_record_release(value->accounting, value->buffer->size, GC_EXTERNAL_KIND_OTHER);
    raster_buffer_destroy(value->buffer);
    mem_free(value);
}

static void raster_trace(void* data, gc_heap* gc) {
    RasterValue* value = (RasterValue*)data;
    gc_mark_item(gc, value->color.item);
    gc_mark_item(gc, value->channels.item);
    gc_mark_item(gc, value->tag.item);
}

static Item raster_snapshot(RasterValue* value, uint32_t left, uint32_t top,
        uint32_t width, uint32_t height, int channel) {
    int64_t shape[] = {height, width, value->buffer->channels};
    ArrayNum* result = alloc_ndim_arraynum(ELEM_UINT8, channel < 0 ? 3 : 2, shape);
    if (!result) return ItemError;
    if (!raster_buffer_copy_region(value->buffer, left, top, width, height, channel,
            (uint8_t*)result->data, (size_t)result->length))
        return lambda_image_error("invalid raster region or channel");
    return {.array_num = result};
}

static const char* const raster_fields[] = {
    "width", "height", "sample_type", "channels", "color", "alpha_mode", "pixels"
};

static bool raster_plain_name(Item key) {
    if (!is_text_type_id(get_type_id(key))) return false;
    if (get_type_id(key) != LMD_TYPE_SYMBOL) return true;
    Symbol* symbol = key.get_safe_symbol();
    return symbol_is_lambda_name(symbol) && !symbol_lambda_namespace(symbol);
}

static VirtualOpStatus raster_field(RasterValue* value, int64_t index, Item* out) {
    switch (index) {
    case 0: *out = {.item = i2it(value->buffer->width)}; break;
    case 1: *out = {.item = i2it(value->buffer->height)}; break;
    case 2: *out = raster_name("u8"); break;
    case 3: *out = value->channels; break;
    case 4: *out = value->color; break;
    case 5: *out = raster_name(value->buffer->channels == 2 || value->buffer->channels == 4 ? "straight" : "none"); break;
    case 6: *out = raster_snapshot(value, 0, 0, value->buffer->width, value->buffer->height, -1); break;
    default: return VIRTUAL_OP_MISSING;
    }
    return item_is_error(*out) ? VIRTUAL_OP_ERROR : VIRTUAL_OP_OK;
}

static VirtualOpStatus raster_get(void* data, Item key, Item* out) {
    if (!raster_plain_name(key)) return VIRTUAL_OP_MISSING;
    const char* name = key.get_chars();
    for (size_t i = 0; i < sizeof(raster_fields) / sizeof(*raster_fields); i++)
        if (strlen(raster_fields[i]) == key.get_len() &&
                memcmp(name, raster_fields[i], key.get_len()) == 0)
            return raster_field((RasterValue*)data, i, out);
    return VIRTUAL_OP_MISSING;
}

static VirtualOpStatus raster_readonly(void*, Item, Item, Item*) { return VIRTUAL_OP_READONLY; }
static int64_t raster_count(void*) { return sizeof(raster_fields) / sizeof(*raster_fields); }
static VirtualOpStatus raster_key(void*, int64_t index, Item* out) {
    if (index < 0 || index >= raster_count(nullptr)) return VIRTUAL_OP_MISSING;
    *out = raster_name(raster_fields[index]);
    return VIRTUAL_OP_OK;
}
static VirtualOpStatus raster_value(void* data, int64_t index, Item* out) {
    return raster_field((RasterValue*)data, index, out);
}
static int64_t raster_child_count(void*) { return 0; }
static VirtualOpStatus raster_child(void*, int64_t, Item*) { return VIRTUAL_OP_MISSING; }
static VirtualOpStatus raster_tag(void* data, Item* out) {
    *out = ((RasterValue*)data)->tag;
    return VIRTUAL_OP_OK;
}
static VirtualOpStatus raster_namespace(void*, Item* out) { *out = ItemNull; return VIRTUAL_OP_OK; }

static const VelmtVtable raster_vtable = {
    {LAMBDA_VIRTUAL_ABI_VERSION, LMD_TYPE_VELMT, {0, 0, 0}, raster_destroy, raster_trace, nullptr},
    {raster_tag, raster_namespace,
        {raster_get, raster_readonly, nullptr, nullptr, raster_count, raster_key, raster_value},
        {raster_child_count, raster_child, nullptr, nullptr, nullptr}}
};

const RasterBuffer* lambda_raster_buffer(Item value) {
    return get_type_id(value) == LMD_TYPE_VELMT && value.velmt->vtable == &raster_vtable
        ? ((RasterValue*)value.velmt->data)->buffer : nullptr;
}

static int raster_component(Item receiver, const char* name, Item* out) {
    RootFrame roots(1);
    Rooted<Item> owner(roots, receiver);
    if (!lambda_raster_buffer(owner.get())) { *out = ItemNull; return 1; }
    RasterValue* value = (RasterValue*)owner.get().velmt->data;
    for (int64_t i = 0; i < value->channels.array->length; i++) {
        const char* candidate = value->channels.array->items[i].get_chars();
        if (candidate && strcmp(candidate, name) == 0) {
            *out = raster_snapshot(value, 0, 0, value->buffer->width, value->buffer->height, (int)i);
            return 1;
        }
    }
    *out = ItemNull;
    return 1;
}

#define RASTER_COMPONENT(name) \
    static int raster_##name(Item receiver, Item* out) { return raster_component(receiver, #name, out); }
RASTER_COMPONENT(r)
RASTER_COMPONENT(g)
RASTER_COMPONENT(b)
RASTER_COMPONENT(a)
#undef RASTER_COMPONENT

static const JubeTypeDef raster_type = {"image_raster", 0, &raster_vtable, nullptr, JUBE_CARRIER_VELMT};
static const JubeMemberBind raster_members[] = {
    {"r", nullptr, raster_r, nullptr, nullptr, nullptr, JUBE_MEMBER_NON_ENUMERABLE},
    {"g", nullptr, raster_g, nullptr, nullptr, nullptr, JUBE_MEMBER_NON_ENUMERABLE},
    {"b", nullptr, raster_b, nullptr, nullptr, nullptr, JUBE_MEMBER_NON_ENUMERABLE},
    {"a", nullptr, raster_a, nullptr, nullptr, nullptr, JUBE_MEMBER_NON_ENUMERABLE}
};
static const JubeTypeBinding raster_binding = {"image_raster", &raster_type, raster_members, 4};
static const JubeModuleDef raster_module = {
    JUBE_ABI_VERSION, sizeof(JubeModuleDef), "image-buffer", "0.1.0", "Raster channel projections",
    &raster_type, 1, nullptr, 0, nullptr, 0, nullptr, nullptr,
    "type image_raster { r: any, g: any, b: any, a: any }\n", &raster_binding, 1
};

Item lambda_raster_wrap(RasterBuffer* buffer, Item color) {
    if (!buffer) return lambda_image_error("raster allocation exceeds the image budget");
    RootFrame roots(3);
    Rooted<Item> description(roots, color), channels(roots, ItemNull), tag(roots, ItemNull);
    void* accounting = heap_gc_external_preflight(buffer->size, GC_EXTERNAL_KIND_OTHER);
    if (!jube_find_static_module(raster_module.name) && jube_register_static_module(&raster_module) != 0) {
        raster_buffer_destroy(buffer);
        return lambda_image_error("cannot register raster projection interface");
    }
    if (!jube_activate_module(&raster_module)) {
        raster_buffer_destroy(buffer);
        return lambda_image_error("cannot activate raster projection interface");
    }
    channels.set({.array = array_plain()});
    const char* const gray[] = {"gray", "a"};
    const char* const rgb[] = {"r", "g", "b", "a"};
    for (uint8_t i = 0; i < buffer->channels; i++) {
        Item name = raster_name(buffer->channels <= 2 ? gray[i] : rgb[i]);
        array_push_verbatim(channels.get().array, name);
    }
    tag.set(raster_name("raster"));
    cow_mark_shared(channels.get());
    cow_mark_shared(description.get());
    RasterValue* value = (RasterValue*)mem_calloc(1, sizeof(RasterValue), MEM_CAT_CONTAINER);
    if (!value) { raster_buffer_destroy(buffer); return ItemError; }
    *value = {buffer, description.get(), channels.get(), tag.get(), accounting};
    heap_gc_external_record_alloc(accounting, buffer->size, GC_EXTERNAL_KIND_OTHER);
    Item result = velmt_new(&raster_vtable, value, &raster_type, value);
    value->color = description.get(); value->channels = channels.get(); value->tag = tag.get();
    if (get_type_id(result) != LMD_TYPE_VELMT) raster_destroy(value);
    return result;
}

extern "C" Item fn_image_raster(Item samples, Item options) {
    GUARD_ERROR2(samples, options);
    RootFrame roots(3);
    Rooted<Item> source(roots, samples), config(roots, options), color(roots, ItemNull);
    if (get_type_id(config.get()) != LMD_TYPE_MAP) return lambda_image_error("raster options must be a map");
    MapReader opts = MapReader::fromItem(config.get());
    auto entries = opts.entries(); const char* key; ItemReader field;
    while (entries.next(&key, &field))
        if (strcmp(key, "width") && strcmp(key, "height") && strcmp(key, "channels") &&
                strcmp(key, "color") && strcmp(key, "color_space"))
            return lambda_image_error("unknown raster option");
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM] = {}, strides[LAMBDA_ARRAY_NUM_MAX_NDIM] = {};
    bool blank = get_type_id(source.get()) == LMD_TYPE_NULL;
    if (blank) {
        if (!opts.get("width").isInt() || !opts.get("height").isInt())
            return lambda_image_error("blank raster requires integer width and height");
        shape[0] = opts.get("height").asInt(); shape[1] = opts.get("width").asInt();
        auto count = opts.get("channels");
        if (!count.isNull() && !count.isInt()) return lambda_image_error("channel count must be an integer");
        shape[2] = count.isNull() ? 4 : count.asInt();
    } else {
        if (!opts.get("width").isNull() || !opts.get("height").isNull() ||
                !opts.get("channels").isNull() || !opts.get("color").isNull())
            return lambda_image_error("array shape defines raster extent/channels; fill color is only for blank");
        if (get_type_id(source.get()) != LMD_TYPE_ARRAY_NUM || source.get().array_num->get_elem_type() != ELEM_UINT8)
            return lambda_image_error("raster requires a u8 numeric image array");
        int ndim = array_num_get_shape_strides(source.get().array_num, shape, strides);
        if (ndim == 2) shape[2] = 1;
        else if (ndim != 3) return lambda_image_error("raster array must have shape (H,W) or (H,W,C)");
    }
    if (shape[0] < 1 || shape[1] < 1 || shape[2] < 1 || shape[2] > 4 ||
            shape[0] > 16000000 || shape[1] > 16000000 || shape[0] * shape[1] > 16000000)
        return lambda_image_error("invalid raster dimensions or channel count");
    color.set(opts.get("color_space").item());
    if (get_type_id(color.get()) != LMD_TYPE_NULL && !is_text_type_id(get_type_id(color.get())))
        return lambda_image_error("color_space must be a color identity name");
    RasterBuffer* buffer = raster_buffer_create((uint32_t)shape[1], (uint32_t)shape[0],
        (uint8_t)shape[2], 64 * 1024 * 1024);
    if (!buffer) return lambda_image_error("raster allocation failed");
    if (!blank) {
        const uint8_t* data = (const uint8_t*)source.get().array_num->data;
        for (int64_t y = 0; y < shape[0]; y++) for (int64_t x = 0; x < shape[1]; x++)
            for (int64_t c = 0; c < shape[2]; c++)
                buffer->pixels[(y * shape[1] + x) * shape[2] + c] = data[y * strides[0] + x * strides[1] + c * strides[2]];
    } else {
        auto fill = opts.get("color");
        if (!fill.isNull()) {
            if (!fill.isArray() || fill.asArray().length() != shape[2]) {
                raster_buffer_destroy(buffer); return lambda_image_error("fill color must match channel count");
            }
            uint8_t pixel[4];
            for (int64_t c = 0; c < shape[2]; c++) {
                double n;
                if (!item_try_to_double(fill.asArray().get(c).item(), &n) || !isfinite(n) || n < 0 || n > 1) {
                    raster_buffer_destroy(buffer); return lambda_image_error("fill samples must be in [0,1]");
                }
                pixel[c] = (uint8_t)lround(n * 255);
            }
            for (size_t i = 0; i < buffer->size; i += buffer->channels) memcpy(buffer->pixels + i, pixel, buffer->channels);
        }
    }
    if (get_type_id(color.get()) == LMD_TYPE_NULL) color.set(raster_name("srgb"));
    return lambda_raster_wrap(buffer, color.get());
}

static const char* raster_region_bounds(const RasterBuffer* buffer, Item bounds, int64_t* region) {
    region[0] = region[1] = 0; region[2] = buffer->width; region[3] = buffer->height;
    if (get_type_id(bounds) != LMD_TYPE_NULL) {
        ItemReader input(bounds.to_const());
        if (!input.isArray() || input.asArray().length() != 4) return "region requires [left,top,right,bottom]";
        for (size_t i = 0; i < 4; i++) if (!lambda_item_to_int64_exact(input.asArray().get(i).item(), &region[i]))
            return "region bounds must be integers";
    }
    if (region[0] < 0 || region[1] < 0 || region[2] <= region[0] || region[3] <= region[1] ||
            region[2] > buffer->width || region[3] > buffer->height) return "raster region is out of bounds";
    return nullptr;
}

static int raster_channel_index(RasterValue* value, Item component) {
    if (get_type_id(component) == LMD_TYPE_NULL) return -1;
    if (raster_plain_name(component))
        for (int64_t c = 0; c < value->channels.array->length; c++)
            if (component.get_len() == value->channels.array->items[c].get_len() &&
                    !memcmp(component.get_chars(), value->channels.array->items[c].get_chars(), component.get_len())) return (int)c;
    return -2;
}

extern "C" Item fn_image_region(Item raster, Item rect, Item channel) {
    GUARD_ERROR3(raster, rect, channel);
    RootFrame roots(3);
    Rooted<Item> owner(roots, raster), bounds(roots, rect), component(roots, channel);
    const RasterBuffer* buffer = lambda_raster_buffer(owner.get());
    if (!buffer) return lambda_image_error("region access requires a Raster");
    int64_t region[4];
    const char* error = raster_region_bounds(buffer, bounds.get(), region);
    if (error) return lambda_image_error(error);
    auto* value = (RasterValue*)owner.get().velmt->data;
    int selected = raster_channel_index(value, component.get());
    if (selected == -2) return lambda_image_error("raster channel does not exist");
    return raster_snapshot(value, (uint32_t)region[0], (uint32_t)region[1],
        (uint32_t)(region[2] - region[0]), (uint32_t)(region[3] - region[1]), selected);
}

extern "C" Item fn_image_write_region(Item raster, Item samples, Item options) {
    GUARD_ERROR3(raster, samples, options);
    RootFrame roots(3);
    Rooted<Item> owner(roots, raster), input(roots, samples), config(roots, options);
    const RasterBuffer* original = lambda_raster_buffer(owner.get());
    if (!original || get_type_id(input.get()) != LMD_TYPE_ARRAY_NUM ||
            input.get().array_num->get_elem_type() != ELEM_UINT8 || get_type_id(config.get()) != LMD_TYPE_MAP)
        return lambda_image_error("region write requires a Raster, u8 samples and options map");
    MapReader opts = MapReader::fromItem(config.get());
    auto entries = opts.entries(); const char* key; ItemReader field;
    while (entries.next(&key, &field))
        if (strcmp(key, "rect") && strcmp(key, "channel")) return lambda_image_error("unknown region write option");
    int64_t rect[4], shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    const char* error = raster_region_bounds(original, opts.get("rect").item(), rect);
    if (error) return lambda_image_error(error);
    auto* value = (RasterValue*)owner.get().velmt->data;
    int channel = raster_channel_index(value, opts.get("channel").item());
    if (channel == -2) return lambda_image_error("raster channel does not exist");
    int rank = array_num_get_shape_strides(input.get().array_num, shape, strides);
    int channels = channel < 0 ? original->channels : 1;
    if (rank != (channel < 0 ? 3 : 2) || shape[0] != rect[3] - rect[1] || shape[1] != rect[2] - rect[0] ||
            (channel < 0 && shape[2] != channels)) return lambda_image_error("region sample shape does not match the destination");
    // validate before allocation/publication; source snapshots may alias the original owner.
    RasterBuffer* result = raster_buffer_create(original->width, original->height, original->channels, original->size);
    if (!result) return lambda_image_error("cannot allocate region edit");
    memcpy(result->pixels, original->pixels, original->size);
    const uint8_t* data = (const uint8_t*)input.get().array_num->data;
    for (int64_t y = 0; y < shape[0]; y++) for (int64_t x = 0; x < shape[1]; x++)
        for (int c = 0; c < channels; c++)
            result->pixels[((size_t)(rect[1] + y) * original->width + rect[0] + x) * original->channels + (channel < 0 ? c : channel)] =
                data[y * strides[0] + x * strides[1] + (channel < 0 ? c * strides[2] : 0)];
    return lambda_raster_wrap(result, item_attr(owner.get(), "color"));
}

extern "C" Item fn_image_patch(Item source, Item attributes, Item children) {
    GUARD_ERROR3(source, attributes, children);
    RootFrame roots(4);
    Rooted<Item> owner(roots, source), attrs(roots, attributes), content(roots, children), result(roots, ItemNull);
    if (get_type_id(attrs.get()) != LMD_TYPE_MAP) return lambda_image_error("image attributes must be a map");
    if (get_type_id(content.get()) == LMD_TYPE_NULL) content.set(fn_content(owner.get()));
    result.set(lambda_element_with_content(owner.get(), content.get()));
    if (item_is_error(result.get())) return result.get();
    MapReader map = MapReader::fromItem(attrs.get());
    auto entries = map.entries(); const char* key; ItemReader field;
    while (entries.next(&key, &field)) {
        RootFrame field_roots(2);
        Rooted<Item> value(field_roots, field.item()), name(field_roots, raster_name(key));
        Item set = fn_index_set(result.get(), name.get(), value.get());
        if (item_is_error(set)) return set;
    }
    return result.get();
}
