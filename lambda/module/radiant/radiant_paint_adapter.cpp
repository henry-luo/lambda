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
    int32_t bands = 0;                       // pigment layers only
    float substrate[PAINT_MAX_BANDS] = {};   // a clean pigment tile's reflectance per band
};

static bool paint_unit_list(const ItemReader& value, int count, float* out);

// layer = {width, height, tile, format: 'rgba'|'coverage'|'pigment', sample: 'f32'|'u8', tiles: [array|null]}
// (PT4); a pigment layer also has bands B and substrate, the clean paper's reflectance per band (PE8).
static const char* paint_layer_read(Item layer, PaintLayerInfo* info) {
    if (!is_map_family_type_id(get_type_id(layer))) return "paint layer must be a map";
    MapReader map = MapReader::fromItem(layer);
    if (!paint_integer(map.get("width"), 1, PAINT_MAX_EXTENT, &info->width) ||
        !paint_integer(map.get("height"), 1, PAINT_MAX_EXTENT, &info->height) ||
        !paint_integer(map.get("tile"), 1, 4096, &info->tile)) return "paint layer needs integer width, height and tile";
    ItemReader format = map.get("format"), sample = map.get("sample");
    if (paint_text_is(format, "rgba")) info->format = PAINT_FORMAT_RGBA;
    else if (paint_text_is(format, "coverage")) info->format = PAINT_FORMAT_COVERAGE;
    else if (paint_text_is(format, "pigment")) {
        info->format = PAINT_FORMAT_PIGMENT;
        int64_t bands;
        if (!paint_integer(map.get("bands"), 1, PAINT_MAX_BANDS, &bands)) return "a pigment layer needs bands, an integer in [1, 8]";
        info->bands = (int32_t)bands;
        if (!paint_unit_list(map.get("substrate"), info->bands, info->substrate))
            return "a pigment layer needs substrate, one reflectance in [0, 1] per band";
    }
    else return "paint layer format must be 'rgba', 'coverage' or 'pigment'";
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

static int paint_layer_channels(const PaintLayerInfo* info) {
    return info->format == PAINT_FORMAT_RGBA ? 4 : info->format == PAINT_FORMAT_PIGMENT ? paint_pigment_channels(info->bands) : 1;
}

// A tile is a dense (tile, tile, 4) f32 array for rgba, (tile, tile, 3B + 2) for pigment, or
// (tile, tile) for coverage.
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
    int expected = info->format == PAINT_FORMAT_COVERAGE ? 2 : 3;
    if (rank != expected || shape[0] != info->tile || shape[1] != info->tile) return false;
    if (rank == 3 && (shape[2] != paint_layer_channels(info) || strides[2] != 1)) return false;
    if (strides[0] < 0 || strides[1] < 0) return false;
    view->data = array->data;
    view->row_stride = strides[0];
    view->pixel_stride = strides[1];
    return true;
}

// a fresh tile: transparent rgba, zero coverage, or a clean pigment tile over the layer's substrate.
static ArrayNum* paint_tile_new(const PaintLayerInfo* info) {
    int channels = paint_layer_channels(info);
    int64_t shape[3] = {info->tile, info->tile, channels};
    ArrayNum* tile = alloc_ndim_arraynum(info->lane, info->format == PAINT_FORMAT_COVERAGE ? 2 : 3, shape);
    if (tile && info->format == PAINT_FORMAT_PIGMENT)
        for (int64_t k = 0; k < info->tile * info->tile; k++)
            for (int32_t b = 0; b < info->bands; b++)
                ((float*)tile->data)[k * channels + paint_pigment_r(info->bands) + b] = info->substrate[b];
    return tile;
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
    out->bands = info->bands;
    for (int32_t b = 0; b < PAINT_MAX_BANDS; b++) out->substrate[b] = info->substrate[b];
    return true;
}

// D7.6.4 for an in-place call: un-shares the layer in `*home`, its tile array and each tile marked
// in `touched`, copying a level only when it is shared (an empty tile becomes a fresh one) and
// storing each replacement before the next allocation. The unique tile array is left in `*tiles`;
// it is reachable from the home, so the caller roots it before allocating again.
static Item paint_unshare_touched(Item* home, const PaintLayerInfo* info, const uint8_t* touched, Item* tiles_out) {
    RootFrame roots(4);
    Rooted<Item> layer(roots, *home), tiles(roots, ItemNull), key(roots, paint_name("tiles")), fresh(roots, ItemNull);
    layer.set(cow_prepare_write(layer.get()));
    if (item_is_error(layer.get())) return layer.get();
    *home = layer.get();
    tiles.set(MapReader::fromItem(layer.get()).get("tiles").item());
    if (!cow_item_is_unique(tiles.get())) {
        tiles.set(cow_prepare_write(tiles.get()));
        if (item_is_error(tiles.get())) return tiles.get();
        if (item_is_error(cow_path_set_raw(layer.get(), key.get(), tiles.get()))) return paint_error("cannot install paint tiles");
    }
    // release and no-log builds erase the diagnostic that consumes these counters
    [[maybe_unused]] int64_t in_place = 0, copied = 0;
    for (int64_t i = 0; i < info->tiles_x * info->tiles_y; i++) {
        if (!touched[i]) continue;
        Item tile = ItemReader(tiles.get().to_const()).asArray().get(i).item();
        if (get_type_id(tile) != LMD_TYPE_NULL && cow_item_is_unique(tile)) { in_place++; continue; }
        copied++;
        ArrayNum* dense = paint_tile_new(info);
        if (!dense) return paint_error("cannot allocate a paint tile");
        fresh.set({.array_num = dense});
        // the source is read again after the allocation (D5.4.4)
        paint_tile_copy(dense, ItemReader(tiles.get().to_const()).asArray().get(i).item(), info);
        if (item_is_error(cow_path_set_raw(tiles.get(), {.item = i2it(i)}, fresh.get())))
            return paint_error("cannot install a paint tile");
    }
    log_debug("paint-stroke: %lld touched tiles written in place, %lld copied or created",
        (long long)in_place, (long long)copied);
    *tiles_out = tiles.get();
    return ItemNull;
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

// a single-channel plane (a bitmap tip or the paper) copied into native f32 samples, so no GC
// pointer is held across the forks that follow. u8 planes scale to [0, 1].
struct PaintPlane {
    PaintScratch samples;
    PaintView view = {};
};

// reads one (h, w) plane: the array itself (index -1), or plane `index` of a (k, h, w) stack.
// A Lambda array literal of equal-size planes, such as `[tip]`, is such a stack.
static const char* paint_plane_read(Item item, PaintPlane* plane, int64_t index = -1) {
    if (get_type_id(item) != LMD_TYPE_ARRAY_NUM) return "tips and paper must be numeric (h, w) arrays";
    ArrayNum* array = item.array_num;
    ArrayNumElemType lane = array->get_elem_type();
    int64_t dims[LAMBDA_ARRAY_NUM_MAX_NDIM], steps[LAMBDA_ARRAY_NUM_MAX_NDIM];
    int rank = array_num_get_shape_strides(array, dims, steps);
    int64_t base = rank == 3 ? index * steps[0] : 0;
    const int64_t* shape = rank == 3 ? dims + 1 : dims;
    const int64_t* strides = rank == 3 ? steps + 1 : steps;
    if ((rank != 2 && rank != 3) || (rank == 3 && (index < 0 || index >= dims[0])) || (rank == 2 && index != -1) ||
        shape[0] < 1 || shape[1] < 1 || shape[0] > 4096 || shape[1] > 4096 ||
        (lane != ELEM_FLOAT32 && lane != ELEM_FLOAT64 && lane != ELEM_UINT8))
        return "tips and paper must be f32, float or u8 (h, w) arrays of at most 4096 x 4096";
    float* data = (float*)mem_calloc((size_t)(shape[0] * shape[1]), sizeof(float), MEM_CAT_RENDER);
    if (!data) return "cannot allocate a paint plane";
    plane->samples.data = data;
    for (int64_t y = 0; y < shape[0]; y++) for (int64_t x = 0; x < shape[1]; x++) {
        int64_t at = base + y * strides[0] + x * strides[1];
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

// The native inputs of one dab batch (PT7, PE4): the layer's geometry, the dab table, tips,
// paper, smudge state and the touched-tile marks. Everything is copied out of the GC heap
// before the first allocation (D5.4.4) and released on every exit path. Both rendering forms
// share it.
struct PaintDabBatch {
    PaintLayerInfo info;
    PaintScratch table, touched, views, tip_views, tip_samples, buckets, resolved, pigments;
    PaintPlane paper;
    int64_t count = 0, tip_count = 0, bucket_count = 0, pigment_count = 0;
    int32_t bucket_fields = PAINT_SMUDGE_FIELDS;
    bool want_resolved = false;
    PaintDabParams params = {};
    ~PaintDabBatch() {
        for (int64_t i = 0; tip_samples.data && i < tip_count; i++)
            if (((void**)tip_samples.data)[i]) mem_free(((void**)tip_samples.data)[i]);
    }
};

// copies a numeric (rows, columns) array into native f32 samples; rows within [min_rows, max_rows].
static const char* paint_matrix_read(const ItemReader& value, int64_t columns, int64_t min_rows, int64_t max_rows,
        PaintScratch* out, int64_t* rows_out, const char* message) {
    if (get_type_id(value.item()) != LMD_TYPE_ARRAY_NUM) return message;
    ArrayNum* array = value.item().array_num;
    int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
    ArrayNumElemType lane = array->get_elem_type();
    if (array_num_get_shape_strides(array, shape, strides) != 2 || shape[1] != columns || shape[0] < min_rows ||
        shape[0] > max_rows || (lane != ELEM_FLOAT32 && lane != ELEM_FLOAT64)) return message;
    int64_t rows = shape[0];
    float* data = (float*)mem_calloc((size_t)(rows * columns > 0 ? rows * columns : 1), sizeof(float), MEM_CAT_RENDER);
    if (!data) return "cannot allocate paint scratch";
    out->data = data;
    for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < columns; c++) {
        int64_t at = r * strides[0] + c * strides[1];
        data[r * columns + c] = lane == ELEM_FLOAT32 ? ((const float*)array->data)[at] : (float)((const double*)array->data)[at];
    }
    *rows_out = rows;
    return nullptr;
}

static const int64_t PAINT_MAX_PIGMENTS = 4096;

// Reads and checks every input, then charges the whole work budget by bounding the dabs and
// marking the tiles they touch, so a failing call has written nothing (D7.6.4 step 1). Options:
// budget; tips, a list of (h, w) coverage planes; paper, an (h, w) height plane; smudge, the
// (buckets, fields) state from an earlier batch (4 fields on rgba, 2B + 1 on pigment), or
// smudge_buckets to start fresh; resolved: true returns each dab's colour and target alpha after
// smudge (PT16, rgba only); pigments, the (count, 2B) K and S table a pigment layer's dabs name
// (PE8). Returns a message or nullptr.
static const char* paint_dab_batch_read(PaintDabBatch* b, Item layer, Item dabs, Item options) {
    static const char* const names[] = {"budget", "tips", "paper", "smudge", "smudge_buckets", "resolved", "pigments"};
    const char* problem = nullptr;
    if (!paint_options_known(options, names, 7, &problem)) return problem;
    int64_t budget = PAINT_DEFAULT_BUDGET;
    ItemReader budget_option = paint_option(options, "budget");
    if (!budget_option.isNull() && !paint_integer(budget_option, 0, INT64_MAX / 2, &budget))
        return "paint budget must be a non-negative integer";
    if ((problem = paint_layer_read(layer, &b->info))) return problem;
    b->bucket_fields = paint_smudge_fields(b->info.format == PAINT_FORMAT_PIGMENT ? b->info.bands : 0);
    if ((problem = paint_dabs_read(dabs, (float**)&b->table.data, &b->count))) return problem;
    ItemReader pigments_option = paint_option(options, "pigments");
    if (!pigments_option.isNull()) {
        if (b->info.format != PAINT_FORMAT_PIGMENT) return "only a pigment layer takes a pigment table";
        if ((problem = paint_matrix_read(pigments_option, 2 * b->info.bands, 1, PAINT_MAX_PIGMENTS, &b->pigments,
                &b->pigment_count, "pigments must be a (count, 2 * bands) f32 or float array of K then S, 1 to 4096 rows")))
            return problem;
    }
    // tips and paper become native planes before any allocation (D5.4.4).
    // tips are a list of (h, w) planes, or a (k, h, w) numeric stack of equal-size planes.
    ItemReader tip_list = paint_option(options, "tips");
    bool tip_stack = !tip_list.isNull() && get_type_id(tip_list.item()) == LMD_TYPE_ARRAY_NUM;
    int64_t tip_dims[LAMBDA_ARRAY_NUM_MAX_NDIM], tip_steps[LAMBDA_ARRAY_NUM_MAX_NDIM];
    int64_t tip_count = tip_list.isNull() ? 0 : tip_stack
        ? (array_num_get_shape_strides(tip_list.item().array_num, tip_dims, tip_steps) == 3 ? tip_dims[0] : -1)
        : (tip_list.isArray() ? tip_list.asArray().length() : -1);
    if (tip_count < 0 || tip_count > PAINT_MAX_TIPS)
        return "tips must be a list of at most 256 (h, w) arrays or a (k, h, w) array";
    if (tip_count > 0) {
        b->tip_samples.data = mem_calloc((size_t)tip_count, sizeof(void*), MEM_CAT_RENDER);
        b->tip_views.data = mem_calloc((size_t)tip_count, sizeof(PaintView), MEM_CAT_RENDER);
        if (!b->tip_samples.data || !b->tip_views.data) return "cannot allocate paint tips";
        b->tip_count = tip_count;
    }
    for (int64_t i = 0; i < tip_count; i++) {
        PaintPlane plane;
        problem = tip_stack ? paint_plane_read(tip_list.item(), &plane, i)
                            : paint_plane_read(tip_list.asArray().get(i).item(), &plane);
        if (problem) return problem;
        ((void**)b->tip_samples.data)[i] = plane.samples.data;
        plane.samples.data = nullptr;
        ((PaintView*)b->tip_views.data)[i] = plane.view;
    }
    ItemReader paper_option = paint_option(options, "paper");
    if (!paper_option.isNull() && (problem = paint_plane_read(paper_option.item(), &b->paper))) return problem;
    ItemReader smudge_option = paint_option(options, "smudge"), buckets_option = paint_option(options, "smudge_buckets");
    if (!smudge_option.isNull()) {
        if ((problem = paint_matrix_read(smudge_option, b->bucket_fields, 1, PAINT_MAX_BUCKETS, &b->buckets, &b->bucket_count,
                "smudge must be a (buckets, fields) f32 or float array with 1 to 256 buckets: 4 fields on rgba, 2B + 1 on pigment")))
            return problem;
    } else if (!buckets_option.isNull()) {
        if (!paint_integer(buckets_option, 1, PAINT_MAX_BUCKETS, &b->bucket_count)) return "smudge_buckets must be an integer in [1, 256]";
        b->buckets.data = mem_calloc((size_t)(b->bucket_count * b->bucket_fields), sizeof(float), MEM_CAT_RENDER);
        if (!b->buckets.data) return "cannot allocate smudge state";
    }
    ItemReader resolved_option = paint_option(options, "resolved");
    if (!resolved_option.isNull() && !resolved_option.isBool()) return "resolved must be a bool";
    b->want_resolved = !resolved_option.isNull() && resolved_option.asBool();
    if (b->want_resolved && b->info.format == PAINT_FORMAT_PIGMENT) return "resolved colours are not available for pigment layers";
    if (b->want_resolved && b->count > 0) {
        b->resolved.data = mem_calloc((size_t)(b->count * 4), sizeof(float), MEM_CAT_RENDER);
        if (!b->resolved.data) return "cannot allocate resolved colours";
    }
    b->params = {(uint64_t)budget, (const PaintView*)b->tip_views.data, (int32_t)b->tip_count,
        b->paper.samples.data ? &b->paper.view : nullptr, (float*)b->buckets.data, (int32_t)b->bucket_count,
        (float*)b->resolved.data, b->info.format == PAINT_FORMAT_PIGMENT ? b->info.bands : 0,
        (const float*)b->pigments.data, (int32_t)b->pigment_count};
    int64_t tile_count = b->info.tiles_x * b->info.tiles_y;
    b->touched.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    b->views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!b->touched.data || !b->views.data) return "cannot allocate paint scratch";
    PaintResult bounds = paint_dab_bounds((const float*)b->table.data, b->count, PAINT_DAB_COLUMNS, (int32_t)b->info.width,
        (int32_t)b->info.height, (int32_t)b->info.tile, &b->params, (uint8_t*)b->touched.data);
    if (bounds.status != PAINT_OK) return bounds.reason ? bounds.reason : "paint engine call failed";
    return nullptr;
}

// Renders the batch into `tiles`, whose touched entries the call owns. The engine views are
// taken here, after the call's last allocation (D5.4.4, D5.5.2); the engine allocates no GC
// values. Returns {rect, work, smudge?, resolved?}, plus `layer` for the pure form.
static Item paint_dab_batch_render(PaintDabBatch* b, Item tiles_item, Item layer_item) {
    RootFrame roots(4);
    Rooted<Item> tiles(roots, tiles_item), layer(roots, layer_item), smudge(roots, ItemNull), resolved(roots, ItemNull);
    PaintTiles target;
    if (!paint_views(tiles.get(), &b->info, (PaintView*)b->views.data, &target)) return paint_error("a paint tile does not match its layer");
    PaintResult result = paint_dabs(&target, (const float*)b->table.data, b->count, PAINT_DAB_COLUMNS, &b->params);
    if (result.status != PAINT_OK) return paint_status_error(result);
    if (b->bucket_count > 0) {
        smudge.set(paint_floats_item((const float*)b->buckets.data, b->bucket_count, b->bucket_fields));
        if (item_is_error(smudge.get())) return smudge.get();
    }
    if (b->want_resolved) {
        resolved.set(paint_floats_item((const float*)b->resolved.data, b->count, 4));
        if (item_is_error(resolved.get())) return resolved.get();
    }
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP);
    if (get_type_id(layer.get()) != LMD_TYPE_NULL) out.put("layer", paint_layer_with_tiles(layer.get(), tiles.get()));
    out.put("rect", paint_rect_item(result.dirty));
    out.put("work", {.item = i2it((int64_t)result.work)});
    if (b->bucket_count > 0) out.put("smudge", smudge.get());
    if (b->want_resolved) out.put("resolved", resolved.get());
    return out.value.get();
}

// radiant.paint_dabs(layer, dabs, options) -> {layer, rect, work, smudge?, resolved?} (PT7 pure
// form, PE4): touched tiles are forked once per call, and the result is a new layer.
RADIANT_C_API Item fn_radiant_paint_dabs(Item layer_item, Item dabs_item, Item options_item) {
    RootFrame roots(4);
    Rooted<Item> layer(roots, layer_item), dabs(roots, dabs_item), options(roots, options_item), tiles(roots, ItemNull);
    PaintDabBatch batch;
    const char* problem = paint_dab_batch_read(&batch, layer.get(), dabs.get(), options.get());
    if (problem) return paint_error(problem);
    tiles.set(paint_fork_tiles(MapReader::fromItem(layer.get()).get("tiles").item(), &batch.info,
        (const uint8_t*)batch.touched.data));
    if (item_is_error(tiles.get())) return tiles.get();
    return paint_dab_batch_render(&batch, tiles.get(), layer.get());
}

// radiant.paint_stroke(var layer, dabs, options) -> {rect, work, smudge?, resolved?} (PT7 in-place
// form, D7.6.3): the batch of paint_dabs, written into the caller's layer through its home. The
// order is D7.6.4: check and charge the budget first; un-share the layer, its tile array and each
// touched tile, copying a level only when it is shared and storing each replacement before the
// next allocation; take the engine views after the last allocation. The layer keeps its shape.
RADIANT_C_API Item fn_radiant_paint_stroke(Item* home, Item dabs_item, Item options_item) {
    RootFrame roots(3);
    Rooted<Item> dabs(roots, dabs_item), options(roots, options_item), tiles(roots, ItemNull);
    PaintDabBatch batch;
    const char* problem = paint_dab_batch_read(&batch, *home, dabs.get(), options.get());
    if (problem) return paint_error(problem);
    Item unique = ItemNull;
    Item failed = paint_unshare_touched(home, &batch.info, (const uint8_t*)batch.touched.data, &unique);
    if (item_is_error(failed)) return failed;
    tiles.set(unique);
    return paint_dab_batch_render(&batch, tiles.get(), ItemNull);
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

// lighting = {light: [x, y, z], relief, diffuse, specular, shininess} for a pigment layer (PE7 step 5).
static const char* paint_lighting_read(Item lighting, PaintPresentParams* params) {
    static const char* const names[] = {"light", "relief", "diffuse", "specular", "shininess"};
    const char* problem = nullptr;
    if (!paint_options_known(lighting, names, 5, &problem)) return "present lighting is {light, relief, diffuse, specular, shininess}";
    ItemReader light = paint_option(lighting, "light");
    double v[3], relief, diffuse, specular, shininess;
    if (!light.isArray() || light.asArray().length() != 3) return "lighting light must be a direction [x, y, z]";
    for (int i = 0; i < 3; i++) if (!paint_number(light.asArray().get(i), &v[i])) return "lighting light must be a direction [x, y, z]";
    if (!paint_number(paint_option(lighting, "relief"), &relief) || !paint_number(paint_option(lighting, "diffuse"), &diffuse) ||
        !paint_number(paint_option(lighting, "specular"), &specular) || !paint_number(paint_option(lighting, "shininess"), &shininess))
        return "lighting needs numbers relief, diffuse, specular and shininess";
    params->has_lighting = true;
    for (int i = 0; i < 3; i++) params->light[i] = (float)v[i];
    params->relief = (float)relief; params->diffuse = (float)diffuse;
    params->specular = (float)specular; params->shininess = (float)shininess;
    return nullptr;
}

// radiant.paint_present(layer, options) -> (h, w, 4) u8 straight-alpha samples of `rect` (PE7).
// Options: rect, encode ('srgb' or 'none'), background (rgba layers), lighting (pigment layers).
RADIANT_C_API Item fn_radiant_paint_present(Item layer_item, Item options_item) {
    RootFrame roots(3);
    Rooted<Item> layer(roots, layer_item), options(roots, options_item), output(roots, ItemNull);
    static const char* const names[] = {"rect", "encode", "background", "lighting"};
    const char* problem = nullptr;
    if (!paint_options_known(options.get(), names, 4, &problem)) return paint_error(problem);
    PaintLayerInfo info;
    if ((problem = paint_layer_read(layer.get(), &info))) return paint_error(problem);
    if (info.format == PAINT_FORMAT_COVERAGE) return paint_error("present needs an rgba or pigment layer");
    PaintRect rect = {0, 0, (int32_t)info.width, (int32_t)info.height};
    ItemReader rect_option = paint_option(options.get(), "rect");
    if (!rect_option.isNull() && !radiant_paint_rect_read(rect_option, &rect)) return paint_error("present rect must be [x0, y0, x1, y1]");
    if (rect.x0 < 0 || rect.y0 < 0 || rect.x1 > info.width || rect.y1 > info.height || rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
        return paint_error("present rect must be a non-empty rectangle inside the layer");
    if ((int64_t)(rect.x1 - rect.x0) * (rect.y1 - rect.y0) > 64 * 1024 * 1024) return paint_error("present rect is too large");
    PaintPresentParams params = {true, false, {0, 0, 0, 0}, false, {0, 0, 1}, 0, 0, 0, 1, nullptr};
    ItemReader lighting = paint_option(options.get(), "lighting");
    if (!lighting.isNull() && (problem = paint_lighting_read(lighting.item(), &params))) return paint_error(problem);
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

// The inputs of a drying call (PE8): the rect clipped to the layer and the rate.
struct PaintDryCall {
    PaintLayerInfo info;
    PaintScratch touched, views;
    PaintRect rect = {0, 0, 0, 0};
    float rate = 1.0f;
};

// options: rect (the whole layer by default) and rate in (0, 1] (1 by default). Marks the tiles in
// the rect; empty ones stay empty, since a clean tile has no film (paint_pigment_dry skips it).
static const char* paint_dry_read(PaintDryCall* d, Item layer, Item options) {
    static const char* const names[] = {"rect", "rate"};
    const char* problem = nullptr;
    if (!paint_options_known(options, names, 2, &problem)) return problem;
    if ((problem = paint_layer_read(layer, &d->info))) return problem;
    if (d->info.format != PAINT_FORMAT_PIGMENT) return "drying needs a pigment layer";
    d->rect = {0, 0, (int32_t)d->info.width, (int32_t)d->info.height};
    ItemReader rect_option = paint_option(options, "rect");
    if (!rect_option.isNull() && !radiant_paint_rect_read(rect_option, &d->rect)) return "dry rect must be [x0, y0, x1, y1]";
    d->rect = paint_rect_clip(d->rect, &d->info);
    ItemReader rate_option = paint_option(options, "rate");
    double rate = 1.0;
    if (!rate_option.isNull() && (!paint_number(rate_option, &rate) || rate <= 0.0 || rate > 1.0)) return "dry rate must be in (0, 1]";
    d->rate = (float)rate;
    int64_t tile_count = d->info.tiles_x * d->info.tiles_y;
    d->touched.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    d->views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!d->touched.data || !d->views.data) return "cannot allocate paint scratch";
    paint_mark_rect(&d->info, d->rect, (uint8_t*)d->touched.data);
    // a clean tile has nothing to dry, so it is not forked into existence
    for (int64_t i = 0; i < tile_count; i++)
        if (get_type_id(MapReader::fromItem(layer).get("tiles").asArray().get(i).item()) == LMD_TYPE_NULL)
            ((uint8_t*)d->touched.data)[i] = 0;
    return nullptr;
}

static Item paint_dry_render(PaintDryCall* d, Item tiles_item, Item layer_item) {
    RootFrame roots(2);
    Rooted<Item> tiles(roots, tiles_item), layer(roots, layer_item);
    PaintTiles target;
    if (!paint_views(tiles.get(), &d->info, (PaintView*)d->views.data, &target)) return paint_error("a paint tile does not match its layer");
    PaintResult result = paint_pigment_dry(&target, d->rect, d->rate);
    if (result.status != PAINT_OK) return paint_status_error(result);
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP);
    if (get_type_id(layer.get()) != LMD_TYPE_NULL) out.put("layer", paint_layer_with_tiles(layer.get(), tiles.get()));
    out.put("rect", paint_rect_item(result.dirty));
    out.put("work", {.item = i2it((int64_t)result.work)});
    return out.value.get();
}

// radiant.paint_dried(layer, options) -> {layer, rect, work} (PT7 pure form): the layer with
// part of its wet film folded into the substrate (PE8); painted tiles in the rect are forked.
RADIANT_C_API Item fn_radiant_paint_dried(Item layer_item, Item options_item) {
    RootFrame roots(3);
    Rooted<Item> layer(roots, layer_item), options(roots, options_item), tiles(roots, ItemNull);
    PaintDryCall call;
    const char* problem = paint_dry_read(&call, layer.get(), options.get());
    if (problem) return paint_error(problem);
    tiles.set(paint_fork_tiles(MapReader::fromItem(layer.get()).get("tiles").item(), &call.info, (const uint8_t*)call.touched.data));
    if (item_is_error(tiles.get())) return tiles.get();
    return paint_dry_render(&call, tiles.get(), layer.get());
}

// radiant.paint_dry(var layer, options) -> {rect, work} (PT7 in-place form, D7.6.3, D7.6.4).
RADIANT_C_API Item fn_radiant_paint_dry(Item* home, Item options_item) {
    RootFrame roots(2);
    Rooted<Item> options(roots, options_item), tiles(roots, ItemNull);
    PaintDryCall call;
    const char* problem = paint_dry_read(&call, *home, options.get());
    if (problem) return paint_error(problem);
    Item unique = ItemNull;
    Item failed = paint_unshare_touched(home, &call.info, (const uint8_t*)call.touched.data, &unique);
    if (item_is_error(failed)) return failed;
    tiles.set(unique);
    return paint_dry_render(&call, tiles.get(), ItemNull);
}

static const int64_t PAINT_MAX_BRISTLES = 4096;

// The native inputs of one bristle batch (PE10): the layer, the path, the bristle state and
// paper, copied out of the GC heap before the first allocation (D5.4.4).
struct PaintBristleBatch {
    PaintLayerInfo info;
    PaintScratch path, state, touched, views;
    PaintPlane paper;
    int64_t count = 0, bristles = 0;
    int32_t columns = 0;
    PaintBristleParams params = {};
};

// options: budget, paper, depletion (load per pixel of trail at full strength, 0 by default) and
// pickup in [0, 1] (0). The state is an (n, columns) array in bristle layout v1.
static const char* paint_bristle_batch_read(PaintBristleBatch* b, Item layer, Item path, Item state, Item options) {
    static const char* const names[] = {"budget", "paper", "depletion", "pickup"};
    const char* problem = nullptr;
    if (!paint_options_known(options, names, 4, &problem)) return problem;
    int64_t budget = PAINT_DEFAULT_BUDGET;
    ItemReader budget_option = paint_option(options, "budget");
    if (!budget_option.isNull() && !paint_integer(budget_option, 0, INT64_MAX / 2, &budget)) return "paint budget must be a non-negative integer";
    if ((problem = paint_layer_read(layer, &b->info))) return problem;
    if (b->info.format == PAINT_FORMAT_COVERAGE) return "bristles need an rgba or pigment layer";
    int32_t bands = b->info.format == PAINT_FORMAT_PIGMENT ? b->info.bands : 0;
    b->columns = paint_bristle_columns(bands);
    if ((problem = paint_dabs_read(path, (float**)&b->path.data, &b->count))) return problem;
    if ((problem = paint_matrix_read(ItemReader(state.to_const()), b->columns, 1, PAINT_MAX_BRISTLES, &b->state, &b->bristles,
            "bristle state must be an (n, columns) f32 or float array: 1 to 4096 bristles, 11 columns on rgba, 10 + 2B on pigment")))
        return problem;
    ItemReader paper_option = paint_option(options, "paper");
    if (!paper_option.isNull() && (problem = paint_plane_read(paper_option.item(), &b->paper))) return problem;
    double depletion = 0.0, pickup = 0.0;
    ItemReader depletion_option = paint_option(options, "depletion"), pickup_option = paint_option(options, "pickup");
    if (!depletion_option.isNull() && !paint_number(depletion_option, &depletion)) return "bristle depletion must be a number";
    if (!pickup_option.isNull() && !paint_number(pickup_option, &pickup)) return "bristle pickup must be a number";
    b->params = {(uint64_t)budget, b->paper.samples.data ? &b->paper.view : nullptr, (float)depletion, (float)pickup, bands};
    int64_t tile_count = b->info.tiles_x * b->info.tiles_y;
    b->touched.data = mem_calloc((size_t)tile_count, 1, MEM_CAT_RENDER);
    b->views.data = mem_calloc((size_t)tile_count, sizeof(PaintView), MEM_CAT_RENDER);
    if (!b->touched.data || !b->views.data) return "cannot allocate paint scratch";
    PaintResult bounds = paint_bristle_bounds((const float*)b->path.data, b->count, PAINT_DAB_COLUMNS, (const float*)b->state.data,
        (int32_t)b->bristles, b->columns, (int32_t)b->info.width, (int32_t)b->info.height, (int32_t)b->info.tile, &b->params,
        (uint8_t*)b->touched.data);
    if (bounds.status != PAINT_OK) return bounds.reason ? bounds.reason : "paint engine call failed";
    return nullptr;
}

// Draws the batch into `tiles` (touched entries owned by the call) and returns {rect, work, state},
// plus `layer` for the pure form.
static Item paint_bristle_batch_render(PaintBristleBatch* b, Item tiles_item, Item layer_item) {
    RootFrame roots(3);
    Rooted<Item> tiles(roots, tiles_item), layer(roots, layer_item), state(roots, ItemNull);
    PaintTiles target;
    if (!paint_views(tiles.get(), &b->info, (PaintView*)b->views.data, &target)) return paint_error("a paint tile does not match its layer");
    PaintResult result = paint_bristles(&target, (const float*)b->path.data, b->count, PAINT_DAB_COLUMNS, (float*)b->state.data,
        (int32_t)b->bristles, b->columns, &b->params);
    if (result.status != PAINT_OK) return paint_status_error(result);
    state.set(paint_floats_item((const float*)b->state.data, b->bristles, b->columns));
    if (item_is_error(state.get())) return state.get();
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP);
    if (get_type_id(layer.get()) != LMD_TYPE_NULL) out.put("layer", paint_layer_with_tiles(layer.get(), tiles.get()));
    out.put("rect", paint_rect_item(result.dirty));
    out.put("work", {.item = i2it((int64_t)result.work)});
    out.put("state", state.get());
    return out.value.get();
}

// radiant.paint_bristles(layer, path, state, options) -> {layer, rect, work, state} (PT7 pure
// form, PE10): touched tiles are forked once per call.
RADIANT_C_API Item fn_radiant_paint_bristles(Item layer_item, Item path_item, Item state_item, Item options_item) {
    RootFrame roots(5);
    Rooted<Item> layer(roots, layer_item), path(roots, path_item), state(roots, state_item), options(roots, options_item),
        tiles(roots, ItemNull);
    PaintBristleBatch batch;
    const char* problem = paint_bristle_batch_read(&batch, layer.get(), path.get(), state.get(), options.get());
    if (problem) return paint_error(problem);
    tiles.set(paint_fork_tiles(MapReader::fromItem(layer.get()).get("tiles").item(), &batch.info, (const uint8_t*)batch.touched.data));
    if (item_is_error(tiles.get())) return tiles.get();
    return paint_bristle_batch_render(&batch, tiles.get(), layer.get());
}

// radiant.paint_bristle_stroke(var layer, path, state, options) -> {rect, work, state} (PT7
// in-place form, D7.6.3, D7.6.4): the batch of paint_bristles, written through the layer's home.
RADIANT_C_API Item fn_radiant_paint_bristle_stroke(Item* home, Item path_item, Item state_item, Item options_item) {
    RootFrame roots(4);
    Rooted<Item> path(roots, path_item), state(roots, state_item), options(roots, options_item), tiles(roots, ItemNull);
    PaintBristleBatch batch;
    const char* problem = paint_bristle_batch_read(&batch, *home, path.get(), state.get(), options.get());
    if (problem) return paint_error(problem);
    Item unique = ItemNull;
    Item failed = paint_unshare_touched(home, &batch.info, (const uint8_t*)batch.touched.data, &unique);
    if (item_is_error(failed)) return failed;
    tiles.set(unique);
    return paint_bristle_batch_render(&batch, tiles.get(), ItemNull);
}

// PT12 `layers/<id>.tiles`: a layer's present tiles as one little-endian blob. A 32-byte header
// (magic "LPT1", version, tile size, tiles_x, tiles_y, channels, sample kind 0 = f32 / 1 = u8,
// count) is followed by the cell index of each present tile, ascending, then their dense samples.
// Empty tiles are omitted; the ZIP entry's deflate compresses the rest.
static const uint32_t PAINT_TILES_VERSION = 1;
static const size_t PAINT_TILES_HEADER = 32;
static const size_t PAINT_TILES_MAX_BYTES = (size_t)1 << 30;

static void paint_put_u32(uint8_t* out, uint32_t v) {
    out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24);
}

static uint32_t paint_get_u32(const uint8_t* in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static size_t paint_tile_bytes(const PaintLayerInfo* info) {
    return (size_t)(info->tile * info->tile * paint_layer_channels(info)) * (info->elem == PAINT_ELEM_F32 ? 4 : 1);
}

// radiant.paint_tiles_encode(layer) -> binary (PT12).
RADIANT_C_API Item fn_radiant_paint_tiles_encode(Item layer_item) {
    RootFrame roots(1);
    Rooted<Item> layer(roots, layer_item);
    PaintLayerInfo info;
    const char* problem = paint_layer_read(layer.get(), &info);
    if (problem) return paint_error(problem);
    int64_t cells = info.tiles_x * info.tiles_y;
    int channels = paint_layer_channels(&info);
    size_t tile_bytes = paint_tile_bytes(&info);
    uint32_t count = 0;
    for (int64_t i = 0; i < cells; i++)
        if (get_type_id(MapReader::fromItem(layer.get()).get("tiles").asArray().get(i).item()) != LMD_TYPE_NULL) count++;
    size_t total = PAINT_TILES_HEADER + (size_t)count * (4 + tile_bytes);
    if (total > PAINT_TILES_MAX_BYTES) return paint_error("paint layer is too large to store");
    uint8_t* buffer = (uint8_t*)mem_alloc(total, MEM_CAT_RENDER);
    if (!buffer) return paint_error("cannot allocate stored tiles");
    memcpy(buffer, "LPT1", 4);
    uint32_t header[7] = {PAINT_TILES_VERSION, (uint32_t)info.tile, (uint32_t)info.tiles_x, (uint32_t)info.tiles_y,
                          (uint32_t)channels, info.elem == PAINT_ELEM_F32 ? 0u : 1u, count};
    for (int k = 0; k < 7; k++) paint_put_u32(buffer + 4 + k * 4, header[k]);
    uint8_t* index = buffer + PAINT_TILES_HEADER;
    uint8_t* samples = index + (size_t)count * 4;
    uint32_t written = 0;
    // no allocation happens while tile storage is read (D5.4.4)
    for (int64_t i = 0; i < cells; i++) {
        PaintView view;
        if (!paint_tile_view(MapReader::fromItem(layer.get()).get("tiles").asArray().get(i).item(), &info, &view)) {
            mem_free(buffer);
            return paint_error("a paint tile does not match its layer");
        }
        if (!view.data) continue;
        paint_put_u32(index + written * 4, (uint32_t)i);
        uint8_t* out = samples + (size_t)written * tile_bytes;
        for (int64_t y = 0; y < info.tile; y++) for (int64_t x = 0; x < info.tile; x++) for (int c = 0; c < channels; c++) {
            int64_t at = y * view.row_stride + x * view.pixel_stride + c;
            if (info.elem == PAINT_ELEM_U8) *out++ = ((const uint8_t*)view.data)[at];
            else {
                uint32_t bits;
                memcpy(&bits, (const float*)view.data + at, 4);
                paint_put_u32(out, bits);
                out += 4;
            }
        }
        written++;
    }
    Binary* blob = heap_binary_from_bytes((const char*)buffer, (int64_t)total);
    mem_free(buffer);
    if (!blob) return paint_error("cannot allocate stored tiles");
    return {.item = x2it(blob)};
}

// radiant.paint_tiles_decode(bytes, layer) -> the layer with the stored tiles (PT12). `layer`
// gives the geometry, format and sample kind the blob must match; its own tiles are replaced.
RADIANT_C_API Item fn_radiant_paint_tiles_decode(Item bytes_item, Item layer_item) {
    RootFrame roots(4);
    Rooted<Item> bytes(roots, bytes_item), layer(roots, layer_item), fresh(roots, ItemNull);
    PaintLayerInfo info;
    const char* problem = paint_layer_read(layer.get(), &info);
    if (problem) return paint_error(problem);
    if (get_type_id(bytes.get()) != LMD_TYPE_BINARY) return paint_error("stored tiles must be binary");
    size_t length = bytes.get().get_binary()->len;
    const uint8_t* data = binary_data(bytes.get().get_binary());
    if (length < PAINT_TILES_HEADER || memcmp(data, "LPT1", 4)) return paint_error("stored tiles have no LPT1 header");
    uint32_t header[7];
    for (int k = 0; k < 7; k++) header[k] = paint_get_u32(data + 4 + k * 4);
    int channels = paint_layer_channels(&info);
    size_t tile_bytes = paint_tile_bytes(&info);
    uint32_t count = header[6];
    int64_t cells = info.tiles_x * info.tiles_y;
    if (header[0] != PAINT_TILES_VERSION) return paint_error("stored tiles have an unknown version");
    if (header[1] != (uint32_t)info.tile || header[2] != (uint32_t)info.tiles_x || header[3] != (uint32_t)info.tiles_y ||
        header[4] != (uint32_t)channels || header[5] != (info.elem == PAINT_ELEM_F32 ? 0u : 1u))
        return paint_error("stored tiles do not match their layer");
    if ((int64_t)count > cells || length != PAINT_TILES_HEADER + (size_t)count * (4 + tile_bytes))
        return paint_error("stored tiles are truncated or have extra bytes");
    for (uint32_t k = 0; k < count; k++) {
        uint32_t cell = paint_get_u32(data + PAINT_TILES_HEADER + k * 4);
        if ((int64_t)cell >= cells || (k > 0 && cell <= paint_get_u32(data + PAINT_TILES_HEADER + (k - 1) * 4)))
            return paint_error("stored tile cells must be ascending and inside the layer");
    }
    RootedValueBuilder result(true);
    uint32_t next = 0;
    for (int64_t i = 0; i < cells; i++) {
        // the blob's storage is re-read after each tile allocation (D5.4.4)
        data = binary_data(bytes.get().get_binary());
        if (next >= count || paint_get_u32(data + PAINT_TILES_HEADER + next * 4) != (uint32_t)i) { result.append(ItemNull); continue; }
        ArrayNum* dense = paint_tile_new(&info);
        if (!dense) return paint_error("cannot allocate a paint tile");
        fresh.set({.array_num = dense});
        data = binary_data(bytes.get().get_binary());
        const uint8_t* in = data + PAINT_TILES_HEADER + (size_t)count * 4 + (size_t)next * tile_bytes;
        if (info.elem == PAINT_ELEM_U8) memcpy(dense->data, in, tile_bytes);
        else for (size_t k = 0; k < tile_bytes / 4; k++) {
            uint32_t bits = paint_get_u32(in + k * 4);
            memcpy((float*)dense->data + k, &bits, 4);
        }
        result.append(fresh.get());
        next++;
    }
    return paint_layer_with_tiles(layer.get(), result.value.get());
}

// radiant.paint_capabilities() -> the engine's profile (PT15).
RADIANT_C_API Item fn_radiant_paint_capabilities(void) {
    RootedValueBuilder out(RootedValueBuilder::DATA_MAP), formats(true), profiles(true);
    out.put("engine", {.item = b2it(true)});
    out.put("dab_layout", {.item = i2it(PAINT_DAB_LAYOUT_VERSION)});
    out.put("dab_columns", {.item = i2it(PAINT_DAB_COLUMNS)});
    out.put("tile", {.item = i2it(64)});
    out.put("budget", {.item = i2it((int64_t)PAINT_DEFAULT_BUDGET)});
    formats.append(paint_name("rgba")); formats.append(paint_name("coverage")); formats.append(paint_name("pigment"));
    out.put("formats", formats.value.get());
    out.put("bands", {.item = i2it(PAINT_MAX_BANDS)});
    out.put("pigments", {.item = i2it(PAINT_MAX_PIGMENTS)});
    out.put("bristles", {.item = i2it(PAINT_MAX_BRISTLES)});
    out.put("bristle_columns", {.item = i2it(PAINT_BRISTLE_INK)});   // plus the ink: 3 on rgba, 2B + 1 on pigment
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
