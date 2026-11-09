// radiant.paint_* bridge (Lambda_Pkg_Paint PT7, PT15): converts tiled typed-array layers into
// engine views, forks the tiles a call writes, and maps engine status to error values.
// The engine never sees an Item and keeps no pointer past a call (D5.5.2). Storage pointers
// are taken only after the call's last allocation point (D5.4.4).
#include "radiant_dom_bridge.hpp"
#include "radiant/paint/paint.hpp"
#include "lambda/core/mark_reader.hpp"
#include "../../runtime/value_builder.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-runtime.hpp"
#include "lib/log.h"
#include "lib/mem.h"
#include <math.h>
#include <string.h>

static const uint64_t PAINT_DEFAULT_BUDGET = 128000000;
static const int64_t PAINT_MAX_EXTENT = 1 << 20;
static const int64_t PAINT_MAX_TILE_COUNT = 1 << 20;
static const int64_t PAINT_MAX_DABS = 1 << 24;

static Item paint_error(const char* message) {
    log_debug("paint: %s", message);
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}

static Item paint_name(const char* text) { return {.item = s2it(heap_create_name(text, strlen(text)))}; }

static bool paint_text_is(const ItemReader& value, const char* expected) {
    const char* text = value.isString() ? value.asString()->chars : value.isSymbol() ? value.asSymbol()->chars : nullptr;
    return text && !strcmp(text, expected);
}

static bool paint_number(const ItemReader& value, double* out) {
    return !value.isNull() && item_try_to_double(value.item(), out) && isfinite(*out);
}

static bool paint_integer(const ItemReader& value, int64_t low, int64_t high, int64_t* out) {
    double number;
    if (!paint_number(value, &number) || floor(number) != number || number < (double)low || number > (double)high) return false;
    *out = (int64_t)number;
    return true;
}

// S17.8.1: an option name the function does not define is an error, not a silent no-op.
static bool paint_options_known(Item options, const char* const* names, int count, const char** unknown) {
    if (get_type_id(options) == LMD_TYPE_NULL) return true;
    if (!is_map_family_type_id(get_type_id(options))) { *unknown = "options must be a map"; return false; }
    MapReader map = MapReader::fromItem(options);
    auto entries = map.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value)) {
        bool known = false;
        for (int i = 0; i < count && !known; i++) known = !strcmp(key, names[i]);
        if (!known) { *unknown = "unknown paint option"; return false; }
    }
    return true;
}

static ItemReader paint_option(Item options, const char* name) {
    if (get_type_id(options) == LMD_TYPE_NULL) return ItemReader(ItemNull.to_const());
    return MapReader::fromItem(options).get(name);
}

struct PaintLayerInfo {
    int64_t width = 0, height = 0, tile = 0, tiles_x = 0, tiles_y = 0;
    PaintFormat format = PAINT_FORMAT_RGBA;
    PaintElem elem = PAINT_ELEM_F32;
    ArrayNumElemType lane = ELEM_FLOAT32;
};

// layer = {width, height, tile, format: 'rgba'|'coverage', sample: 'f32'|'u8', tiles: [array|null]} (PT4)
static const char* paint_layer_read(Item layer, PaintLayerInfo* info) {
    if (!is_map_family_type_id(get_type_id(layer))) return "paint layer must be a map";
    MapReader map = MapReader::fromItem(layer);
    if (!paint_integer(map.get("width"), 1, PAINT_MAX_EXTENT, &info->width) ||
        !paint_integer(map.get("height"), 1, PAINT_MAX_EXTENT, &info->height) ||
        !paint_integer(map.get("tile"), 1, 4096, &info->tile)) return "paint layer needs integer width, height and tile";
    ItemReader format = map.get("format"), sample = map.get("sample");
    if (paint_text_is(format, "rgba")) info->format = PAINT_FORMAT_RGBA;
    else if (paint_text_is(format, "coverage")) info->format = PAINT_FORMAT_COVERAGE;
    else return "paint layer format must be 'rgba' or 'coverage'";
    if (paint_text_is(sample, "f32")) { info->elem = PAINT_ELEM_F32; info->lane = ELEM_FLOAT32; }
    else if (paint_text_is(sample, "u8") && info->format == PAINT_FORMAT_COVERAGE) { info->elem = PAINT_ELEM_U8; info->lane = ELEM_UINT8; }
    else return "paint layer sample must be 'f32' (or 'u8' for coverage)";
    info->tiles_x = (info->width + info->tile - 1) / info->tile;
    info->tiles_y = (info->height + info->tile - 1) / info->tile;
    if (info->tiles_x * info->tiles_y > PAINT_MAX_TILE_COUNT) return "paint layer has too many tiles";
    ItemReader tiles = map.get("tiles");
    if (!tiles.isArray() || get_type_id(tiles.item()) == LMD_TYPE_ARRAY_NUM ||
        tiles.asArray().length() != info->tiles_x * info->tiles_y) return "paint layer tiles must list one entry per tile";
    return nullptr;
}

static int paint_layer_channels(const PaintLayerInfo* info) { return info->format == PAINT_FORMAT_RGBA ? 4 : 1; }

// A tile is a dense (tile, tile, 4) f32 array for rgba, or (tile, tile) for coverage.
static bool paint_tile_view(Item tile, const PaintLayerInfo* info, PaintView* view) {
    memset(view, 0, sizeof(*view));
    view->elem = info->elem; view->width = (int32_t)info->tile; view->height = (int32_t)info->tile;
    view->channels = paint_layer_channels(info);
    if (get_type_id(tile) == LMD_TYPE_NULL) return true;
    if (get_type_id(tile) != LMD_TYPE_ARRAY_NUM) return false;
    ArrayNum* array = tile.array_num;
    if (array->get_elem_type() != info->lane) return false;
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    int rank = array_num_get_shape_strides(array, shape, strides);
    int expected = info->format == PAINT_FORMAT_RGBA ? 3 : 2;
    if (rank != expected || shape[0] != info->tile || shape[1] != info->tile) return false;
    if (rank == 3 && (shape[2] != 4 || strides[2] != 1)) return false;
    if (strides[0] < 0 || strides[1] < 0) return false;
    view->data = array->data;
    view->row_stride = strides[0];
    view->pixel_stride = strides[1];
    return true;
}

static ArrayNum* paint_tile_new(const PaintLayerInfo* info) {
    int64_t shape[3] = {info->tile, info->tile, 4};
    return alloc_ndim_arraynum(info->lane, info->format == PAINT_FORMAT_RGBA ? 3 : 2, shape);
}

// copies a (possibly strided) source tile into a fresh dense one; no allocation happens between
// reading the source pointer and the copy.
static void paint_tile_copy(ArrayNum* dense, Item source, const PaintLayerInfo* info) {
    PaintView from;
    paint_tile_view(source, info, &from);
    if (!from.data) return;
    size_t sample = info->elem == PAINT_ELEM_F32 ? sizeof(float) : 1;
    int channels = paint_layer_channels(info);
    for (int64_t y = 0; y < info->tile; y++) for (int64_t x = 0; x < info->tile; x++)
        memcpy((uint8_t*)dense->data + ((y * info->tile + x) * channels) * sample,
               (const uint8_t*)from.data + (y * from.row_stride + x * from.pixel_stride) * sample, channels * sample);
}

// Builds the result tile array: tiles marked in `write` become fresh unique copies (empty
// tiles become zeroed ones); the others are shared with the input and marked shared (D5.4.2).
static Item paint_fork_tiles(Item tiles, const PaintLayerInfo* info, const uint8_t* write) {
    RootFrame roots(2);
    Rooted<Item> source(roots, tiles), fresh(roots, ItemNull);
    RootedValueBuilder result(true);
    int64_t count = info->tiles_x * info->tiles_y;
    for (int64_t i = 0; i < count; i++) {
        Item tile = ItemReader(source.get().to_const()).asArray().get(i).item();
        if (!write[i]) { result.append(cow_mark_shared(tile)); continue; }
        ArrayNum* dense = paint_tile_new(info);
        if (!dense) return paint_error("cannot allocate a paint tile");
        fresh.set({.array_num = dense});
        paint_tile_copy(dense, ItemReader(source.get().to_const()).asArray().get(i).item(), info);
        result.append(fresh.get());
    }
    return result.value.get();
}

// Engine views over a tile array, taken after every allocation of the call (D5.4.4).
static bool paint_views(Item tiles, const PaintLayerInfo* info, PaintView* views, PaintTiles* out) {
    int64_t count = info->tiles_x * info->tiles_y;
    for (int64_t i = 0; i < count; i++)
        if (!paint_tile_view(ItemReader(tiles.to_const()).asArray().get(i).item(), info, &views[i])) return false;
    out->tiles = views;
    out->width = (int32_t)info->width; out->height = (int32_t)info->height;
    out->tiles_x = (int32_t)info->tiles_x; out->tiles_y = (int32_t)info->tiles_y;
    out->tile_size = (int32_t)info->tile;
    out->format = info->format; out->elem = info->elem;
    return true;
}

// the result layer keeps every field of the input and replaces `tiles`.
static Item paint_layer_with_tiles(Item layer, Item tiles) {
    RootFrame roots(2);
    Rooted<Item> source(roots, layer), replacement(roots, tiles);
    RootedValueBuilder result(RootedValueBuilder::DATA_MAP);
    MapReader map = MapReader::fromItem(source.get());
    auto entries = map.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value))
        result.put(key, !strcmp(key, "tiles") ? replacement.get() : cow_mark_shared(value.item()));
    return result.value.get();
}

static Item paint_rect_item(PaintRect rect) {
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) return ItemNull;
    RootedValueBuilder result(true);
    int32_t values[4] = {rect.x0, rect.y0, rect.x1, rect.y1};
    for (int i = 0; i < 4; i++) result.append({.item = i2it(values[i])});
    return result.value.get();
}

bool radiant_paint_rect_read(const ItemReader& value, PaintRect* rect) {
    if (!value.isArray() || value.asArray().length() != 4) return false;
    int64_t v[4];
    for (int i = 0; i < 4; i++)
        if (!paint_integer(value.asArray().get(i), -PAINT_MAX_EXTENT, PAINT_MAX_EXTENT, &v[i])) return false;
    rect->x0 = (int32_t)v[0]; rect->y0 = (int32_t)v[1]; rect->x1 = (int32_t)v[2]; rect->y1 = (int32_t)v[3];
    return true;
}

static Item paint_status_error(PaintResult result) {
    return paint_error(result.reason ? result.reason : "paint engine call failed");
}

// A dab table is an f32 or float (N, 34) array, or its flattening; it is copied to a native
// f32 buffer so no GC pointer is held across the forks that follow.
static const char* paint_dabs_read(Item dabs, float** out, int64_t* count) {
    *out = nullptr; *count = 0;
    if (get_type_id(dabs) != LMD_TYPE_ARRAY_NUM) return "dab table must be a numeric array";
    ArrayNum* array = dabs.array_num;
    ArrayNumElemType lane = array->get_elem_type();
    if (lane != ELEM_FLOAT32 && lane != ELEM_FLOAT64) return "dab table must hold f32 or float samples";
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    int rank = array_num_get_shape_strides(array, shape, strides);
    int64_t rows;
    if (rank == 2 && shape[1] == PAINT_DAB_COLUMNS) rows = shape[0];
    else if (rank == 1 && shape[0] % PAINT_DAB_COLUMNS == 0) {
        rows = shape[0] / PAINT_DAB_COLUMNS; strides[1] = strides[0]; strides[0] *= PAINT_DAB_COLUMNS;
    } else return "dab table must have 34 columns (layout v1)";
    if (rows > PAINT_MAX_DABS) return "dab table has too many rows";
    if (rows == 0) return nullptr;
    float* table = (float*)mem_calloc((size_t)rows * PAINT_DAB_COLUMNS, sizeof(float), MEM_CAT_RENDER);
    if (!table) return "cannot allocate the dab table";
    for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < PAINT_DAB_COLUMNS; c++) {
        int64_t at = r * strides[0] + c * strides[1];
        table[r * PAINT_DAB_COLUMNS + c] = lane == ELEM_FLOAT32 ? ((const float*)array->data)[at]
                                                               : (float)((const double*)array->data)[at];
    }
    *out = table; *count = rows;
    return nullptr;
}

struct PaintScratch {
    void* data = nullptr;
    ~PaintScratch() { if (data) mem_free(data); }
};

// radiant.paint_dabs(layer, dabs, options) -> {layer, rect, work} (PT7 pure form, PE4).
// a single-channel plane (a bitmap tip or the paper) copied into native f32 samples, so no GC
// pointer is held across the forks that follow. u8 planes scale to [0, 1].
struct PaintPlane {
    PaintScratch samples;
    PaintView view = {};
};

static const char* paint_plane_read(Item item, PaintPlane* plane) {
    if (get_type_id(item) != LMD_TYPE_ARRAY_NUM) return "tips and paper must be numeric (h, w) arrays";
    ArrayNum* array = item.array_num;
    ArrayNumElemType lane = array->get_elem_type();
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    if (array_num_get_shape_strides(array, shape, strides) != 2 || shape[0] < 1 || shape[1] < 1 ||
        shape[0] > 4096 || shape[1] > 4096 || (lane != ELEM_FLOAT32 && lane != ELEM_FLOAT64 && lane != ELEM_UINT8))
        return "tips and paper must be f32, float or u8 (h, w) arrays of at most 4096 x 4096";
    float* data = (float*)mem_calloc((size_t)(shape[0] * shape[1]), sizeof(float), MEM_CAT_RENDER);
    if (!data) return "cannot allocate a paint plane";
    plane->samples.data = data;
    for (int64_t y = 0; y < shape[0]; y++) for (int64_t x = 0; x < shape[1]; x++) {
        int64_t at = y * strides[0] + x * strides[1];
        float v = lane == ELEM_FLOAT32 ? ((const float*)array->data)[at] : lane == ELEM_FLOAT64 ?
            (float)((const double*)array->data)[at] : ((const uint8_t*)array->data)[at] / 255.0f;
        if (!isfinite(v)) return "tip and paper samples must be finite";
        data[y * shape[1] + x] = v;
    }
    plane->view = {data, PAINT_ELEM_F32, (int32_t)shape[1], (int32_t)shape[0], 1, shape[1], 1};
    return nullptr;
}

// a fresh dense f32 (rows, columns) array holding native samples.
static Item paint_floats_item(const float* samples, int64_t rows, int64_t columns) {
    int64_t shape[2] = {rows, columns};
    ArrayNum* array = alloc_ndim_arraynum(ELEM_FLOAT32, 2, shape);
    if (!array) return paint_error("cannot allocate a paint result array");
    if (rows * columns > 0) memcpy(array->data, samples, (size_t)(rows * columns) * sizeof(float));
    return {.array_num = array};
}

static const int32_t PAINT_MAX_TIPS = 256;
static const int32_t PAINT_MAX_BUCKETS = 256;

// radiant.paint_dabs(layer, dabs, options) -> {layer, rect, work, smudge?, resolved?} (PT7 pure
// form, PE4). Options: budget; tips, a list of (h, w) coverage planes; paper, an (h, w) height
// plane; smudge, the (buckets, 4) state from an earlier batch, or smudge_buckets to start fresh;
// resolved: true returns each dab's colour and target alpha after smudge (PT16).
RADIANT_C_API Item fn_radiant_paint_dabs(Item layer_item, Item dabs_item, Item options_item) {
    RootFrame roots(6);
    Rooted<Item> layer(roots, layer_item), dabs(roots, dabs_item), options(roots, options_item), tiles(roots, ItemNull),
        result_smudge(roots, ItemNull), result_resolved(roots, ItemNull);
    static const char* const names[] = {"budget", "tips", "paper", "smudge", "smudge_buckets", "resolved"};
    const char* problem = nullptr;
    if (!paint_options_known(options.get(), names, 6, &problem)) return paint_error(problem);
    int64_t budget = PAINT_DEFAULT_BUDGET;
    ItemReader budget_option = paint_option(options.get(), "budget");
    if (!budget_option.isNull() && !paint_integer(budget_option, 0, INT64_MAX / 2, &budget))
        return paint_error("paint budget must be a non-negative integer");
    PaintLayerInfo info;
    if ((problem = paint_layer_read(layer.get(), &info))) return paint_error(problem);
    PaintScratch table, touched, views, tip_views, buckets, resolved;
    int64_t count = 0;
    if ((problem = paint_dabs_read(dabs.get(), (float**)&table.data, &count))) return paint_error(problem);
    // tips and paper become native planes before any allocation (D5.4.4).
    ItemReader tip_list = paint_option(options.get(), "tips");
    int64_t tip_count = tip_list.isNull() ? 0 : (tip_list.isArray() && get_type_id(tip_list.item()) != LMD_TYPE_ARRAY_NUM ?
        tip_list.asArray().length() : -1);
    if (tip_count < 0 || tip_count > PAINT_MAX_TIPS) return paint_error("tips must be a list of at most 256 (h, w) arrays");
    PaintScratch tip_samples;
    if (tip_count > 0) {
        tip_samples.data = mem_calloc((size_t)tip_count, sizeof(void*), MEM_CAT_RENDER);
        tip_views.data = mem_calloc((size_t)tip_count, sizeof(PaintView), MEM_CAT_RENDER);
        if (!tip_samples.data || !tip_views.data) return paint_error("cannot allocate paint tips");
    }
    struct TipRelease {   // frees each tip's samples on every exit path
        void** samples; int64_t count;
        ~TipRelease() { for (int64_t i = 0; i < count; i++) if (samples[i]) mem_free(samples[i]); }
    } release = {(void**)tip_samples.data, tip_count};
    for (int64_t i = 0; i < tip_count; i++) {
        PaintPlane plane;
        if ((problem = paint_plane_read(tip_list.asArray().get(i).item(), &plane))) return paint_error(problem);
        ((void**)tip_samples.data)[i] = plane.samples.data;
        plane.samples.data = nullptr;
        ((PaintView*)tip_views.data)[i] = plane.view;
    }
    PaintPlane paper;
    ItemReader paper_option = paint_option(options.get(), "paper");
    if (!paper_option.isNull() && (problem = paint_plane_read(paper_option.item(), &paper))) return paint_error(problem);
    int64_t bucket_count = 0;
    ItemReader smudge_option = paint_option(options.get(), "smudge"), buckets_option = paint_option(options.get(), "smudge_buckets");
    if (!smudge_option.isNull()) {
        if (get_type_id(smudge_option.item()) != LMD_TYPE_ARRAY_NUM) return paint_error("smudge must be a (buckets, 4) numeric array");
        float* state = nullptr; int64_t rows = 0;
        // the smudge state reuses the dab-table reader's lane handling with four columns.
        ArrayNum* array = smudge_option.item().array_num;
        int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
        ArrayNumElemType lane = array->get_elem_type();
        if (array_num_get_shape_strides(array, shape, strides) != 2 || shape[1] != PAINT_SMUDGE_FIELDS || shape[0] < 1 ||
            shape[0] > PAINT_MAX_BUCKETS || (lane != ELEM_FLOAT32 && lane != ELEM_FLOAT64))
            return paint_error("smudge must be a (buckets, 4) f32 or float array with 1 to 256 buckets");
        rows = shape[0];
        state = (float*)mem_calloc((size_t)(rows * PAINT_SMUDGE_FIELDS), sizeof(float), MEM_CAT_RENDER);
        if (!state) return paint_error("cannot allocate smudge state");
        buckets.data = state;
        for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < PAINT_SMUDGE_FIELDS; c++) {
            int64_t at = r * strides[0] + c * strides[1];
            state[r * PAINT_SMUDGE_FIELDS + c] = lane == ELEM_FLOAT32 ? ((const float*)array->data)[at] : (float)((const double*)array->data)[at];
        }
        bucket_count = rows;
    } else if (!buckets_option.isNull()) {
        if (!paint_integer(buckets_option, 1, PAINT_MAX_BUCKETS, &bucket_count)) return paint_error("smudge_buckets must be an integer in [1, 256]");
        buckets.data = mem_calloc((size_t)(bucket_count * PAINT_SMUDGE_FIELDS), sizeof(float), MEM_CAT_RENDER);
        if (!buckets.data) return paint_error("cannot allocate smudge state");
    }
    ItemReader resolved_option = paint_option(options.get(), "resolved");
    bool want_resolved = !resolved_option.isNull() && resolved_option.isBool() && resolved_option.asBool();
    if (!resolved_option.isNull() && !resolved_option.isBool()) return paint_error("resolved must be a bool");
    if (want_resolved && count > 0) {
        resolved.data = mem_calloc((size_t)(count * 4), sizeof(float), MEM_CAT_RENDER);
        if (!resolved.data) return paint_error("cannot allocate resolved colours");
    }
    PaintDabParams params = {(uint64_t)budget, (const PaintView*)tip_views.data, (int32_t)tip_count,
        paper.samples.data ? &paper.view : nullptr, (float*)buckets.data, (int32_t)bucket_count, (float*)resolved.data};
    int64_t tile_count = info.tiles_x * info.tiles_y;
    touched.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!touched.data || !views.data) return paint_error("cannot allocate paint scratch");
    PaintResult bounds = paint_dab_bounds((const float*)table.data, count, PAINT_DAB_COLUMNS, (int32_t)info.width,
        (int32_t)info.height, (int32_t)info.tile, &params, (uint8_t*)touched.data);
    if (bounds.status != PAINT_OK) return paint_status_error(bounds);
    tiles.set(paint_fork_tiles(MapReader::fromItem(layer.get()).get("tiles").item(), &info, (const uint8_t*)touched.data));
    if (item_is_error(tiles.get())) return tiles.get();
    PaintTiles target;
    if (!paint_views(tiles.get(), &info, (PaintView*)views.data, &target)) return paint_error("a paint tile does not match its layer");
    PaintResult result = paint_dabs(&target, (const float*)table.data, count, PAINT_DAB_COLUMNS, &params);
    if (result.status != PAINT_OK) return paint_status_error(result);
    if (bucket_count > 0) {
        result_smudge.set(paint_floats_item((const float*)buckets.data, bucket_count, PAINT_SMUDGE_FIELDS));
        if (item_is_error(result_smudge.get())) return result_smudge.get();
    }
    if (want_resolved) {
        result_resolved.set(paint_floats_item((const float*)resolved.data, count, 4));
        if (item_is_error(result_resolved.get())) return result_resolved.get();
    }
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP);
    out.put("layer", paint_layer_with_tiles(layer.get(), tiles.get()));
    out.put("rect", paint_rect_item(result.dirty));
    out.put("work", {.item = i2it((int64_t)result.work)});
    if (bucket_count > 0) out.put("smudge", result_smudge.get());
    if (want_resolved) out.put("resolved", result_resolved.get());
    return out.value.get();
}

// radiant.paint_sample(layer, x, y, radius) -> [r, g, b, a] straight colour under a round
// footprint (PE5, libmypaint get_color), in the layer's blend space.
RADIANT_C_API Item fn_radiant_paint_sample(Item layer_item, Item x_item, Item y_item, Item radius_item) {
    RootFrame roots(1);
    Rooted<Item> layer(roots, layer_item);
    PaintLayerInfo info;
    const char* problem = nullptr;
    if ((problem = paint_layer_read(layer.get(), &info))) return paint_error(problem);
    if (info.format != PAINT_FORMAT_RGBA) return paint_error("sampling needs an rgba layer");
    double x, y, radius;
    if (!paint_number(ItemReader(x_item.to_const()), &x) || !paint_number(ItemReader(y_item.to_const()), &y) ||
        !paint_number(ItemReader(radius_item.to_const()), &radius)) return paint_error("sample x, y and radius must be finite numbers");
    PaintScratch views;
    views.data = mem_calloc((size_t)(info.tiles_x * info.tiles_y), sizeof(PaintView), MEM_CAT_RENDER);
    if (!views.data) return paint_error("cannot allocate paint scratch");
    PaintTiles source;
    if (!paint_views(MapReader::fromItem(layer.get()).get("tiles").item(), &info, (PaintView*)views.data, &source))
        return paint_error("a paint tile does not match its layer");
    float out[4];
    PaintResult result = paint_sample(&source, (float)x, (float)y, (float)radius, out);
    if (result.status != PAINT_OK) return paint_status_error(result);
    RootedValueBuilder color(true);
    for (int c = 0; c < 4; c++) color.append(push_d(out[c]));
    return color.value.get();
}

static void paint_mark_rect(const PaintLayerInfo* info, PaintRect rect, uint8_t* marks) {
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) return;
    for (int64_t ty = rect.y0 / info->tile; ty <= (rect.y1 - 1) / info->tile; ty++)
        for (int64_t tx = rect.x0 / info->tile; tx <= (rect.x1 - 1) / info->tile; tx++)
            marks[ty * info->tiles_x + tx] = 1;
}

static PaintRect paint_rect_clip(PaintRect rect, const PaintLayerInfo* info) {
    if (rect.x0 < 0) rect.x0 = 0; if (rect.y0 < 0) rect.y0 = 0;
    if (rect.x1 > info->width) rect.x1 = (int32_t)info->width;
    if (rect.y1 > info->height) rect.y1 = (int32_t)info->height;
    return rect;
}

static bool paint_unit_list(const ItemReader& value, int count, float* out) {
    if (!value.isArray() || value.asArray().length() != count) return false;
    for (int i = 0; i < count; i++) {
        double v;
        if (!paint_number(value.asArray().get(i), &v) || v < 0.0 || v > 1.0) return false;
        out[i] = (float)v;
    }
    return true;
}

static bool paint_unit_option(Item options, const char* name, float fallback, float* out) {
    ItemReader value = paint_option(options, name);
    if (value.isNull()) { *out = fallback; return true; }
    double v;
    if (!paint_number(value, &v) || v < 0.0 || v > 1.0) return false;
    *out = (float)v;
    return true;
}

// radiant.paint_wash(source, coverage, options) -> {layer, rect}: one composite of a stroke's
// accumulated coverage at the stroke opacity (PE4 wash).
RADIANT_C_API Item fn_radiant_paint_wash(Item source_item, Item coverage_item, Item options_item) {
    RootFrame roots(4);
    Rooted<Item> source(roots, source_item), coverage(roots, coverage_item), options(roots, options_item), tiles(roots, ItemNull);
    static const char* const names[] = {"color", "opacity", "eraser", "lock_alpha", "rect"};
    const char* problem = nullptr;
    if (!paint_options_known(options.get(), names, 5, &problem)) return paint_error(problem);
    PaintLayerInfo info, cover;
    if ((problem = paint_layer_read(source.get(), &info)) || (problem = paint_layer_read(coverage.get(), &cover)))
        return paint_error(problem);
    if (info.format != PAINT_FORMAT_RGBA || cover.format != PAINT_FORMAT_COVERAGE)
        return paint_error("wash needs an rgba layer and a coverage layer");
    if (info.width != cover.width || info.height != cover.height || info.tile != cover.tile)
        return paint_error("wash layers must share one tile grid");
    PaintWashParams params;
    if (!paint_unit_list(paint_option(options.get(), "color"), 3, params.color)) return paint_error("wash color must be [r, g, b] in [0, 1]");
    if (!paint_unit_option(options.get(), "opacity", 1.0f, &params.opacity) ||
        !paint_unit_option(options.get(), "eraser", 1.0f, &params.eraser) ||
        !paint_unit_option(options.get(), "lock_alpha", 0.0f, &params.lock_alpha))
        return paint_error("wash opacity, eraser and lock_alpha must be in [0, 1]");
    PaintRect rect = {0, 0, (int32_t)info.width, (int32_t)info.height};
    ItemReader rect_option = paint_option(options.get(), "rect");
    if (!rect_option.isNull() && !radiant_paint_rect_read(rect_option, &rect)) return paint_error("wash rect must be [x0, y0, x1, y1]");
    rect = paint_rect_clip(rect, &info);
    int64_t tile_count = info.tiles_x * info.tiles_y;
    PaintScratch marks, views, cover_views;
    marks.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    cover_views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!marks.data || !views.data || !cover_views.data) return paint_error("cannot allocate paint scratch");
    paint_mark_rect(&info, rect, (uint8_t*)marks.data);
    tiles.set(paint_fork_tiles(MapReader::fromItem(source.get()).get("tiles").item(), &info, (const uint8_t*)marks.data));
    if (item_is_error(tiles.get())) return tiles.get();
    PaintTiles target, mask;
    if (!paint_views(tiles.get(), &info, (PaintView*)views.data, &target) ||
        !paint_views(MapReader::fromItem(coverage.get()).get("tiles").item(), &cover, (PaintView*)cover_views.data, &mask))
        return paint_error("a paint tile does not match its layer");
    // the forked target starts as the frozen source, so it can serve as both.
    PaintResult result = paint_wash_composite(&target, &target, &mask, rect, &params);
    if (result.status != PAINT_OK) return paint_status_error(result);
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP);
    out.put("layer", paint_layer_with_tiles(source.get(), tiles.get()));
    out.put("rect", paint_rect_item(result.dirty));
    return out.value.get();
}

// radiant.paint_import(layer, pixels, options) -> layer: straight-alpha (H, W, 3|4) u8 or
// f32 samples written at `origin`, decoded from sRGB unless decode is 'none'.
RADIANT_C_API Item fn_radiant_paint_import(Item layer_item, Item pixels_item, Item options_item) {
    RootFrame roots(4);
    Rooted<Item> layer(roots, layer_item), pixels(roots, pixels_item), options(roots, options_item), tiles(roots, ItemNull);
    static const char* const names[] = {"origin", "decode"};
    const char* problem = nullptr;
    if (!paint_options_known(options.get(), names, 2, &problem)) return paint_error(problem);
    PaintLayerInfo info;
    if ((problem = paint_layer_read(layer.get(), &info))) return paint_error(problem);
    if (info.format != PAINT_FORMAT_RGBA) return paint_error("import needs an rgba layer");
    int64_t origin[2] = {0, 0};
    ItemReader origin_option = paint_option(options.get(), "origin");
    if (!origin_option.isNull() && (!origin_option.isArray() || origin_option.asArray().length() != 2 ||
        !paint_integer(origin_option.asArray().get(0), -PAINT_MAX_EXTENT, PAINT_MAX_EXTENT, &origin[0]) ||
        !paint_integer(origin_option.asArray().get(1), -PAINT_MAX_EXTENT, PAINT_MAX_EXTENT, &origin[1])))
        return paint_error("import origin must be integer [x, y]");
    ItemReader decode_option = paint_option(options.get(), "decode");
    bool decode = decode_option.isNull() || paint_text_is(decode_option, "srgb");
    if (!decode && !paint_text_is(decode_option, "none")) return paint_error("import decode must be 'srgb' or 'none'");
    if (get_type_id(pixels.get()) != LMD_TYPE_ARRAY_NUM) return paint_error("import pixels must be a numeric (H, W, C) array");
    ArrayNum* array = pixels.get().array_num;
    ArrayNumElemType lane = array->get_elem_type();
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    if (array_num_get_shape_strides(array, shape, strides) != 3 || (shape[2] != 3 && shape[2] != 4) || strides[2] != 1 ||
        (lane != ELEM_UINT8 && lane != ELEM_FLOAT32) || shape[0] < 1 || shape[1] < 1 ||
        shape[0] > PAINT_MAX_EXTENT || shape[1] > PAINT_MAX_EXTENT)
        return paint_error("import pixels must be u8 or f32 (H, W, 3|4) samples");
    PaintRect rect = paint_rect_clip({(int32_t)origin[0], (int32_t)origin[1], (int32_t)(origin[0] + shape[1]), (int32_t)(origin[1] + shape[0])}, &info);
    int64_t tile_count = info.tiles_x * info.tiles_y;
    PaintScratch marks, views;
    marks.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!marks.data || !views.data) return paint_error("cannot allocate paint scratch");
    paint_mark_rect(&info, rect, (uint8_t*)marks.data);
    tiles.set(paint_fork_tiles(MapReader::fromItem(layer.get()).get("tiles").item(), &info, (const uint8_t*)marks.data));
    if (item_is_error(tiles.get())) return tiles.get();
    PaintTiles target;
    if (!paint_views(tiles.get(), &info, (PaintView*)views.data, &target)) return paint_error("a paint tile does not match its layer");
    array = pixels.get().array_num;   // reload after the forks' allocations (D5.4.4)
    PaintView source = {array->data, lane == ELEM_UINT8 ? PAINT_ELEM_U8 : PAINT_ELEM_F32, (int32_t)shape[1], (int32_t)shape[0],
        (int32_t)shape[2], strides[0], strides[1]};
    PaintResult result = paint_import(&target, &source, (int32_t)origin[0], (int32_t)origin[1], decode);
    if (result.status != PAINT_OK) return paint_status_error(result);
    return paint_layer_with_tiles(layer.get(), tiles.get());
}

// radiant.paint_present(layer, options) -> (h, w, 4) u8 straight-alpha samples of `rect` (PE7).
RADIANT_C_API Item fn_radiant_paint_present(Item layer_item, Item options_item) {
    RootFrame roots(3);
    Rooted<Item> layer(roots, layer_item), options(roots, options_item), output(roots, ItemNull);
    static const char* const names[] = {"rect", "encode", "background"};
    const char* problem = nullptr;
    if (!paint_options_known(options.get(), names, 3, &problem)) return paint_error(problem);
    PaintLayerInfo info;
    if ((problem = paint_layer_read(layer.get(), &info))) return paint_error(problem);
    if (info.format != PAINT_FORMAT_RGBA) return paint_error("present needs an rgba layer");
    PaintRect rect = {0, 0, (int32_t)info.width, (int32_t)info.height};
    ItemReader rect_option = paint_option(options.get(), "rect");
    if (!rect_option.isNull() && !radiant_paint_rect_read(rect_option, &rect)) return paint_error("present rect must be [x0, y0, x1, y1]");
    if (rect.x0 < 0 || rect.y0 < 0 || rect.x1 > info.width || rect.y1 > info.height || rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
        return paint_error("present rect must be a non-empty rectangle inside the layer");
    if ((int64_t)(rect.x1 - rect.x0) * (rect.y1 - rect.y0) > 64 * 1024 * 1024) return paint_error("present rect is too large");
    PaintPresentParams params = {true, false, {0, 0, 0, 0}};
    ItemReader encode = paint_option(options.get(), "encode");
    if (!encode.isNull()) {
        if (paint_text_is(encode, "none")) params.encode_srgb = false;
        else if (!paint_text_is(encode, "srgb")) return paint_error("present encode must be 'srgb' or 'none'");
    }
    ItemReader background = paint_option(options.get(), "background");
    if (!background.isNull()) {
        if (!paint_unit_list(background, 4, params.background)) return paint_error("present background must be [r, g, b, a] in [0, 1]");
        params.has_background = true;
    }
    int64_t shape[3] = {rect.y1 - rect.y0, rect.x1 - rect.x0, 4};
    ArrayNum* pixels = alloc_ndim_arraynum(ELEM_UINT8, 3, shape);
    if (!pixels) return paint_error("cannot allocate presented pixels");
    output.set({.array_num = pixels});
    PaintScratch views;
    views.data = mem_calloc((size_t)(info.tiles_x * info.tiles_y), sizeof(PaintView), MEM_CAT_RENDER);
    if (!views.data) return paint_error("cannot allocate paint scratch");
    PaintTiles source;
    if (!paint_views(MapReader::fromItem(layer.get()).get("tiles").item(), &info, (PaintView*)views.data, &source))
        return paint_error("a paint tile does not match its layer");
    PaintView out = {output.get().array_num->data, PAINT_ELEM_U8, rect.x1 - rect.x0, rect.y1 - rect.y0, 4,
                     (int64_t)(rect.x1 - rect.x0) * 4, 4};
    PaintResult result = paint_present(&source, rect, &out, &params);
    if (result.status != PAINT_OK) return paint_status_error(result);
    return output.get();
}

// radiant.paint_capabilities() -> the engine's profile (PT15).
RADIANT_C_API Item fn_radiant_paint_capabilities(void) {
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP), formats(true), profiles(true);
    out.put("engine", {.item = b2it(true)});
    out.put("dab_layout", {.item = i2it(PAINT_DAB_LAYOUT_VERSION)});
    out.put("dab_columns", {.item = i2it(PAINT_DAB_COLUMNS)});
    out.put("tile", {.item = i2it(64)});
    out.put("budget", {.item = i2it((int64_t)PAINT_DEFAULT_BUDGET)});
    formats.append(paint_name("rgba")); formats.append(paint_name("coverage"));
    out.put("formats", formats.value.get());
    profiles.append(paint_name("mypaint")); profiles.append(paint_name("linear"));
    out.put("profiles", profiles.value.get());
    // blend modes in dab-column order (PaintBlend)
    static const char* const blend_names[] = {"normal", "multiply", "screen", "overlay", "darken", "lighten", "add"};
    RootedValueBuilder blends(true);
    for (const char* name : blend_names) blends.append(paint_name(name));
    out.put("blends", blends.value.get());
    out.put("smudge_buckets", {.item = i2it(PAINT_MAX_BUCKETS)});
    out.put("tips", {.item = i2it(PAINT_MAX_TIPS)});
    return out.value.get();
}
