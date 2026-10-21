#pragma once
#include "input.hpp"
#include "input-context.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/arraylist.hpp"
#include <math.h>

namespace lambda {
static constexpr size_t SCENE_ASSET_BUFFER_LIMIT = 64 * 1024 * 1024;
// bones per skeleton, the native skinning palette's capacity (radiant/scene3d.cpp)
static constexpr unsigned SCENE_ASSET_BONE_LIMIT = 256;
// D4.1.3: imported values belong to the destination Input, including decoded buffers.
struct SceneAsset {
    InputContext ctx;
    Item root, resources;
    unsigned serial = 0;
    // a scene group gathers its resources first; a `<character>` has none
    explicit SceneAsset(Input* input, const char* root_tag = "group") : ctx(input) {
        root = element(root_tag); resources = ItemNull;
        if (!strcmp(root_tag, "group")) { resources = element("resources"); append(root, resources); }
    }
    Item element(const char* tag) { return ctx.builder.createElement(tag); }
    Item text(const char* value) { return ctx.builder.createStringItem(value); }
    Item symbol(const char* value) { return ctx.builder.createSymbolItem(value); }
    Item number(double value) { return ctx.builder.createFloat(value); }
    void attr(Item node, const char* key, Item value) {
        ctx.builder.putToElement(lam::gc_borrow(node.element), ctx.builder.createName(key), value);
    }
    void append(Item node, Item child) {
        list_push_with_owner((List*)node.element, child, ctx.builder.pool(), ctx.builder.arena(),
            ctx.input()->ui_mode ? ctx.input() : nullptr);
    }
    Item array(const double* data, size_t count) {
        auto result = ctx.builder.array();
        for (size_t i = 0; i < count; i++) result.append(number(data[i]));
        return result.final();
    }
    // a packed float stream (a float ArrayNum), observably an ordinary array
    Item floats(const double* data, size_t count) {
        int64_t length = (int64_t)count;
        return ctx.builder.createNumericArray(ELEM_FLOAT64, 1, &length, data);
    }
    Item id(const char* kind, unsigned index) {
        char value[80]; snprintf(value, sizeof(value), "asset-%s-%u", kind, index); return text(value);
    }
    bool fail(const char* reason) { ctx.addError("scene3d asset: %s", reason); return false; }
    const char* absolute(const char* source, Url* base = nullptr);
    Input* dependency(const char* source, const char* type, Url* base = nullptr);
    Item texture(const char* source, Url* base = nullptr, Item* node = nullptr);
    Item material(const char* color = "#ffffff", const char* kind = "lambert");
    void finish() {
        ctx.input()->root = ctx.hasErrors() ? ItemError : root;
        if (ctx.hasErrors() || ctx.hasWarnings()) ctx.logErrors();
    }
};
inline ItemReader asset_read(Item item) { return ItemReader(item.to_const()); }
inline double asset_number(ItemReader item, double fallback = NAN) {
    return item.isInt() ? (double)item.asInt() : item.isFloat() ? item.asFloat() : fallback;
}
inline ItemReader asset_field(ItemReader item, const char* key) { return item.asMap().get(key); }
inline bool asset_is(const char* a, const char* b) { return a && !strcmp(a, b); }
inline size_t asset_count(ItemReader item) { return item.isArray() ? item.asArray().length() : 0; }
inline ItemReader asset_at(ItemReader item, size_t index) { return item.asArray().get(index); }
inline double asset_component(ItemReader item, size_t index, double fallback = NAN) {
    return asset_number(asset_at(item,index),fallback);
}
Item asset_color(SceneAsset& asset, const double* color);
bool asset_triangulate(SceneAsset& asset, const double* positions, size_t count, lam::ArrayList<unsigned>& triangles, double* normal = nullptr);
bool asset_import_gltf(SceneAsset& asset, Item source, const uint8_t* binary = nullptr, size_t binary_length = 0);
bool asset_import_glb(SceneAsset& asset, const char* source, size_t length);
bool asset_import_wavefront(SceneAsset& asset, Item source);
bool asset_import_a3d(SceneAsset& asset, Item source);
}
