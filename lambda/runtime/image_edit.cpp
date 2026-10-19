#include "image_raster.hpp"
#include "heap_api.h"
#include "lambda-error.h"
#include "lambda-number-runtime.hpp"
#include "../core/mark_reader.hpp"
#include "value_builder.hpp"
#include "../../lib/image_process.h"
#include <math.h>

// native kernels operate on owned tiles; the adapter roots only the input values (D5.3.3).
struct RasterEdit : RasterOwner {
    const char* error = nullptr;
    uint32_t left = UINT32_MAX, top = UINT32_MAX, right = 0, bottom = 0;
    bool fail(const char* message) { error = message; return false; }
    bool number(Item options, const char* key, double fallback, double low, double high, double* out) {
        return (lambda_image_number(options, key, fallback, out) && *out >= low && *out <= high) || fail("image edit option is outside its finite range");
    }
    bool point(const ItemReader& reader, double* out) {
        if (!reader.isArray() || reader.asArray().length() < 2 || reader.asArray().length() > 3)
            return fail("image edit point requires [x,y] or [x,y,pressure]");
        for (int i = 0; i < 2; i++) if (!item_try_to_double(reader.asArray().get(i).item(), out + i) ||
            !isfinite(out[i]) || fabs(out[i]) > 1000000000) return fail("image edit point must be finite and bounded");
        out[2] = 1;
        if (reader.asArray().length() == 3 && (!item_try_to_double(reader.asArray().get(2).item(), out + 2) ||
            !isfinite(out[2]) || out[2] < 0 || out[2] > 1)) return fail("pressure must be in [0,1]");
        return true;
    }
    bool point(Item value, double* out) { return point(ItemReader(value.to_const()), out); }
    bool color(Item value, uint8_t channels, uint8_t* out) {
        ItemReader reader(value.to_const());
        if (!reader.isArray() || reader.asArray().length() != channels) return fail("paint color must match target channels");
        for (uint8_t c = 0; c < channels; c++) {
            double n;
            if (!item_try_to_double(reader.asArray().get(c).item(), &n) || !isfinite(n) || n < 0 || n > 1)
                return fail("paint color samples must be in [0,1]");
            out[c] = (uint8_t)lround(n * 255);
        }
        return true;
    }
    bool write(uint32_t x, uint32_t y, const uint8_t* samples) {
        if (!memcmp(raster_buffer_pixel(result, x, y), samples, result->channels)) return true;
        uint8_t* pixel = raster_buffer_edit_pixel(result, x, y);
        if (!pixel) return fail("cannot allocate edited tile");
        memcpy(pixel, samples, result->channels);
        if (x<left) left=x; if (y<top) top=y;
        if (x+1>right) right=x+1; if (y+1>bottom) bottom=y+1;
        return true;
    }
    bool create(uint32_t width, uint32_t height, uint8_t channels) {
        result = raster_buffer_sparse(width, height, channels, 64 * 1024 * 1024);
        return result || fail("image edit exceeds the raster budget");
    }
};

static bool edit_is(Item value, const char* expected) {
    return is_text_type_id(get_type_id(value)) && value.get_len() == strlen(expected) && !memcmp(value.get_chars(), expected, value.get_len());
}
static int coverage_crossing_order(const void* left, const void* right) {
    double a = *(const double*)left, b = *(const double*)right;
    return (a > b) - (a < b);
}

static bool edit_shape(RasterEdit& edit, Item options) {
    double width, height;
    if (!edit.number(options, "width", 0, 1, 16000000, &width) ||
        !edit.number(options, "height", 0, 1, 16000000, &height)) return false;
    if (floor(width) != width || floor(height) != height || width * height > 16000000)
        return edit.fail("selection requires bounded integer dimensions");
    if (!edit.create((uint32_t)width, (uint32_t)height, 1)) return false;
    Item shape = item_attr(options, "shape"), points = item_attr(options, "points");
    bool polygon = edit_is(shape, "polygon"), ellipse = edit_is(shape, "ellipse"), all = edit_is(shape, "all");
    if (!polygon && !ellipse && !all && !edit_is(shape, "rectangle")) return edit.fail("unknown selection shape");
    double first[3] = {}, last[3] = {};
    ItemReader vertices(points.to_const());
    if (polygon) {
        if (!vertices.isArray() || vertices.asArray().length() < 3 || vertices.asArray().length() > 4096)
            return edit.fail("polygon requires 3 to 4096 vertices");
        size_t count = (size_t)vertices.asArray().length();
        if (height * count > 128000000) return edit.fail("polygon exceeds the scanline work budget");
        ByteStorage* workspace = byte_storage_alloc(count * 3 * sizeof(double), MEM_CAT_TEMP);
        if (!workspace) return edit.fail("cannot allocate polygon scanlines");
        double* coordinates = (double*)workspace->data;
        double* crossings = coordinates + count * 2;
        bool valid = true;
        for (size_t i = 0; i < count && valid; i++) {
            valid = edit.point(vertices.asArray().get(i), first);
            coordinates[i * 2] = first[0]; coordinates[i * 2 + 1] = first[1];
        }
        for (uint32_t y = 0; y < edit.result->height && valid; y++) {
            size_t hits = 0;
            for (size_t i = 0, previous = count - 1; i < count; previous = i++) {
                double ax = coordinates[previous * 2], ay = coordinates[previous * 2 + 1];
                double bx = coordinates[i * 2], by = coordinates[i * 2 + 1], py = y + 0.5;
                if ((ay > py) != (by > py)) crossings[hits++] = (bx - ax) * (py - ay) / (by - ay) + ax;
            }
            qsort(crossings, hits, sizeof(double), coverage_crossing_order);
            for (size_t i = 0; i + 1 < hits && valid; i += 2) {
                int64_t left = (int64_t)fmax(0, ceil(crossings[i] - 0.5));
                int64_t right = (int64_t)fmin(width, ceil(crossings[i + 1] - 0.5));
                uint8_t selected = 255;
                for (int64_t x = left; x < right && valid; x++) valid = edit.write((uint32_t)x, y, &selected);
            }
        }
        byte_storage_release(workspace); return valid;
    } else if (!all && (!edit.point(item_attr(options, "start"), first) || !edit.point(item_attr(options, "end"), last))) return false;
    double left = fmin(first[0], last[0]), top = fmin(first[1], last[1]);
    double right = fmax(first[0], last[0]), bottom = fmax(first[1], last[1]);
    for (uint32_t y = 0; y < edit.result->height; y++) for (uint32_t x = 0; x < edit.result->width; x++) {
        double px = x + 0.5, py = y + 0.5;
        bool inside = all;
        if (!all && right > left && bottom > top) {
            inside = px >= left && px < right && py >= top && py < bottom;
            if (inside && ellipse) {
                double nx = (px - (left + right) / 2) / ((right - left) / 2);
                double ny = (py - (top + bottom) / 2) / ((bottom - top) / 2);
                inside = nx * nx + ny * ny <= 1;
            }
        }
        uint8_t sample = inside ? 255 : 0;
        if (sample && !edit.write(x, y, &sample)) return false;
    }
    return true;
}

struct CoverageFilter {
    const RasterBuffer* source;
    RasterEdit* edit;
};
static bool coverage_read_row(void* opaque, int64_t y, double* row) {
    const RasterBuffer* source = ((CoverageFilter*)opaque)->source;
    for (uint32_t x = 0; x < source->width; x++) row[x] = raster_buffer_pixel(source, x, (uint32_t)y)[0];
    return true;
}
static bool coverage_write_row(void* opaque, int64_t y, const double* row) {
    RasterEdit& edit = *((CoverageFilter*)opaque)->edit;
    for (uint32_t x = 0; x < edit.result->width; x++) {
        uint8_t sample = (uint8_t)lround(fmax(0, fmin(255, row[x])));
        if (!edit.write(x, (uint32_t)y, &sample)) return false;
    }
    return true;
}

static bool edit_coverage(RasterEdit& edit, const RasterBuffer* source, Item options, const char* operation) {
    if (!source || source->channels != 1) return edit.fail("coverage operations require one gray channel");
    if (!strcmp(operation, "feather")) {
        double radius;
        if (!edit.number(options, "radius", 1, 0, 256, &radius) || !edit.create(source->width, source->height, 1)) return false;
        CoverageFilter filter = {source, &edit};
        return image_box_filter(source->width, source->height, 1, 2 * (int64_t)ceil(radius) + 1,
            coverage_read_row, coverage_write_row, &filter) || edit.fail("coverage feather failed");
    }
    const RasterBuffer* other = lambda_raster_buffer(item_attr(options, "other"));
    Item mode = item_attr(options, "mode");
    bool invert = !strcmp(operation, "invert");
    if (!invert && (!other || other->channels != 1 || other->width != source->width || other->height != source->height))
        return edit.fail("coverage extents must match");
    bool add = edit_is(mode, "add"), subtract = edit_is(mode, "subtract"), intersect = edit_is(mode, "intersect");
    if (!invert && !add && !subtract && !intersect && !edit_is(mode, "replace")) return edit.fail("unknown selection combination");
    if (!edit.create(source->width, source->height, 1)) return false;
    for (uint32_t y = 0; y < source->height; y++) for (uint32_t x = 0; x < source->width; x++) {
        double a = raster_buffer_pixel(source, x, y)[0] / 255.0;
        double b = other ? raster_buffer_pixel(other, x, y)[0] / 255.0 : 0;
        double n = invert ? 1 - a : add ? 1 - (1 - a) * (1 - b) : subtract ? a * (1 - b) : intersect ? a * b : b;
        uint8_t sample = (uint8_t)lround(n * 255);
        if (!edit.write(x, y, &sample)) return false;
    }
    return true;
}

static double edit_selection(const RasterBuffer* selection, const double* origin, uint32_t x, uint32_t y) {
    if (!selection) return 1;
    double px = floor(x + origin[0]), py = floor(y + origin[1]);
    if (px < 0 || py < 0 || px >= selection->width || py >= selection->height) return 0;
    return raster_buffer_pixel(selection, (uint32_t)px, (uint32_t)py)[0] / 255.0;
}

static bool edit_dab(RasterEdit& edit, double px, double py, double radius, double hardness, double flow, size_t* work) {
    int64_t left = (int64_t)fmax(0, floor(px - radius)), top = (int64_t)fmax(0, floor(py - radius));
    int64_t right = (int64_t)fmin(edit.result->width, ceil(px + radius)), bottom = (int64_t)fmin(edit.result->height, ceil(py + radius));
    if (right <= left || bottom <= top) return true;
    size_t area = (size_t)(right - left) * (bottom - top);
    if (area > 128000000 - *work) return edit.fail("stroke exceeds the bounded dab work budget");
    *work += area;
    for (int64_t y = top; y < bottom; y++) for (int64_t x = left; x < right; x++) {
        double distance = hypot(x + 0.5 - px, y + 0.5 - py) / radius;
        double coverage = distance <= hardness ? 1 : hardness >= 1 ? 0 : fmax(0, (1 - distance) / (1 - hardness));
        if (coverage <= 0) continue;
        double old = raster_buffer_pixel(edit.result, (uint32_t)x, (uint32_t)y)[0] / 255.0;
        uint8_t sample = (uint8_t)lround((1 - (1 - old) * (1 - coverage * flow)) * 255);
        if (!edit.write((uint32_t)x, (uint32_t)y, &sample)) return false;
    }
    return true;
}

static bool edit_stroke(RasterEdit& edit, const RasterBuffer* source, Item options) {
    double size, hardness, spacing, flow;
    if (!edit.number(options, "size", 20, 0.5, 4096, &size) || !edit.number(options, "hardness", 0.8, 0, 1, &hardness) ||
        !edit.number(options, "spacing", 0.2, 0.01, 2, &spacing) || !edit.number(options, "flow", 1, 0, 1, &flow)) return false;
    ItemReader points(item_attr(options, "points").to_const());
    if (!points.isArray() || !points.asArray().length() || points.asArray().length() > 65536)
        return edit.fail("stroke requires 1 to 65536 points");
    if (!edit.create(source->width, source->height, 1)) return false;
    double a[3], b[3], distance_to_next = fmax(0.25, size * spacing);
    if (!edit.point(points.asArray().get(0), a)) return false;
    size_t work = 0;
    if (!edit_dab(edit, a[0], a[1], size / 2, hardness, flow * a[2], &work)) return false;
    // carry spacing across segments so event subdivision cannot change dab placement.
    for (int64_t i = 1; i < points.asArray().length(); i++) {
        if (!edit.point(points.asArray().get(i), b)) return false;
        double length = hypot(b[0] - a[0], b[1] - a[1]), at = distance_to_next;
        while (at <= length && length > 0) {
            double t = at / length;
            if (!edit_dab(edit, a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
                size / 2, hardness, flow * (a[2] + (b[2] - a[2]) * t), &work)) return false;
            at += fmax(0.25, size * spacing);
        }
        distance_to_next = at - length; memcpy(a, b, sizeof(a));
    }
    return true;
}

static bool edit_paint(RasterEdit& edit, const RasterBuffer* source, Item options, const char* operation) {
    if (!source || (source->channels != 1 && source->channels != 4)) return edit.fail("paint requires gray coverage or RGBA samples");
    double opacity, origin[3] = {};
    if (!edit.number(options, "opacity", 1, 0, 1, &opacity)) return false;
    Item position = item_attr(options, "origin");
    if (get_type_id(position) != LMD_TYPE_NULL && !edit.point(position, origin)) return false;
    const RasterBuffer* selection = lambda_raster_buffer(item_attr(options, "selection"));
    if (get_type_id(item_attr(options, "selection")) != LMD_TYPE_NULL && (!selection || selection->channels != 1))
        return edit.fail("paint selection requires gray coverage");
    bool stroke = !strcmp(operation, "stroke"), erase = edit_is(item_attr(options, "mode"), "erase");
    if (get_type_id(item_attr(options, "mode")) != LMD_TYPE_NULL && !erase && !edit_is(item_attr(options, "mode"), "paint"))
        return edit.fail("unknown paint mode");
    uint8_t color[4] = {0, 0, 0, 255}, end_color[4] = {};
    if (!erase && !edit.color(item_attr(options, "color"), source->channels, color)) return false;
    double start[3] = {}, end[3] = {}, length_squared = 0;
    bool gradient = !strcmp(operation, "gradient"), radial = edit_is(item_attr(options, "kind"), "radial");
    if (gradient) {
        if (!edit.color(item_attr(options, "end_color"), source->channels, end_color) ||
            !edit.point(item_attr(options, "start"), start) || !edit.point(item_attr(options, "end"), end)) return false;
        if (!radial && !edit_is(item_attr(options, "kind"), "linear")) return edit.fail("unknown gradient kind");
        length_squared = (end[0] - start[0]) * (end[0] - start[0]) + (end[1] - start[1]) * (end[1] - start[1]);
        if (length_squared <= 0) return edit.fail("gradient endpoints must differ");
    }
    RasterEdit coverage;
    if (stroke && !edit_stroke(coverage, source, options)) return edit.fail(coverage.error);
    edit.result = raster_buffer_fork(source);
    if (!edit.result) return edit.fail("cannot allocate paint tile index");
    uint32_t left = stroke ? coverage.left : 0, top = stroke ? coverage.top : 0;
    uint32_t right = stroke ? coverage.right : source->width, bottom = stroke ? coverage.bottom : source->height;
    for (uint32_t y = top; y < bottom; y++) for (uint32_t x = left; x < right; x++) {
        double alpha = opacity * edit_selection(selection, origin, x, y);
        if (stroke) alpha *= raster_buffer_pixel(coverage.result, x, y)[0] / 255.0;
        if (alpha <= 0) continue;
        uint8_t sample[4], paint[4]; memcpy(sample, raster_buffer_pixel(source, x, y), source->channels);
        memcpy(paint, color, source->channels);
        if (gradient) {
            double t = radial ? hypot(x + 0.5 - start[0], y + 0.5 - start[1]) / sqrt(length_squared) :
                ((x + 0.5 - start[0]) * (end[0] - start[0]) + (y + 0.5 - start[1]) * (end[1] - start[1])) / length_squared;
            t = fmax(0, fmin(1, t));
            double alpha = source->channels == 4 ? color[3]*(1-t)+end_color[3]*t : 255;
            for (uint8_t c = 0; c < source->channels; c++) paint[c] = (uint8_t)lround(
                source->channels == 4 && c<3 ? (alpha<=0 ? 0 : (color[c]*color[3]*(1-t)+end_color[c]*end_color[3]*t)/alpha) :
                color[c]*(1-t)+end_color[c]*t);
        }
        raster_mix_pixel(sample, paint, source->channels, alpha, erase);
        if (!edit.write(x, y, sample)) return false;
    }
    return true;
}

static bool edit_map(RasterEdit& edit, const RasterBuffer* source, Item options, const char* operation) {
    if (!source) return edit.fail("sample mapping requires a Raster");
    bool reframe = !strcmp(operation,"reframe"), mix = !strcmp(operation,"mix");
    int64_t rect[] = {0,0,source->width,source->height};
    uint8_t fill[4] = {};
    if (reframe) {
        if (!lambda_image_rect(item_attr(options,"rect"),rect) || rect[0]<-1000000000 || rect[1]<-1000000000 ||
            rect[2]>1000000000 || rect[3]>1000000000 || rect[2]<=rect[0] || rect[3]<=rect[1] ||
            rect[2]-rect[0]>16000000 || rect[3]-rect[1]>16000000 ||
            (rect[2]-rect[0])*(rect[3]-rect[1])>16000000) return edit.fail("invalid sample extent");
        Item requested = item_attr(options,"fill");
        if (get_type_id(requested)!=LMD_TYPE_NULL && !edit.color(requested,source->channels,fill)) return false;
    }
    const RasterBuffer* other = lambda_raster_buffer(item_attr(options,"other"));
    if (mix && (!other || other->width!=source->width || other->height!=source->height || other->channels!=source->channels))
        return edit.fail("mixed raster extents and channels must match");
    const RasterBuffer* selection = lambda_raster_buffer(item_attr(options,"selection"));
    if (get_type_id(item_attr(options,"selection"))!=LMD_TYPE_NULL && (!selection || selection->channels!=1))
        return edit.fail("sample selection requires coverage");
    double origin[3] = {};
    if (get_type_id(item_attr(options,"origin"))!=LMD_TYPE_NULL && !edit.point(item_attr(options,"origin"),origin)) return false;
    if (reframe) { if (!edit.create((uint32_t)(rect[2]-rect[0]),(uint32_t)(rect[3]-rect[1]),source->channels)) return false; }
    else {
        if (source->channels!=1 && source->channels!=4) return edit.fail("coverage mapping requires gray or RGBA samples");
        edit.result=raster_buffer_fork(source);
        if (!edit.result) return edit.fail("cannot allocate sample map");
    }
    for (uint32_t y=0;y<edit.result->height;y++) for (uint32_t x=0;x<edit.result->width;x++) {
        int64_t sx=x+rect[0], sy=y+rect[1];
        const uint8_t* old = sx>=0 && sy>=0 && sx<source->width && sy<source->height ?
            raster_buffer_pixel(source,(uint32_t)sx,(uint32_t)sy) : fill;
        uint8_t sample[4]; memcpy(sample,old,source->channels);
        if (!reframe) {
            double coverage = edit_selection(selection,origin,x,y);
            if (mix) {
                const uint8_t* next=raster_buffer_pixel(other,x,y);
                for (uint8_t c=0;c<source->channels;c++) sample[c]=(uint8_t)lround(old[c]*(1-coverage)+next[c]*coverage);
            } else {
                uint8_t alpha=source->channels==4 ? 3 : 0;
                sample[alpha]=(uint8_t)lround(old[alpha]*coverage);
            }
        }
        if (!edit.write(x,y,sample)) return false;
    }
    return true;
}

static bool edit_wand(RasterEdit& edit, const RasterBuffer* source, Item options) {
    if (!source) return edit.fail("wand requires raster samples");
    double point[3], tolerance;
    if (!edit.point(item_attr(options, "point"), point) || !edit.number(options, "tolerance", 0.1, 0, 1, &tolerance)) return false;
    int64_t x = (int64_t)floor(point[0]), y = (int64_t)floor(point[1]);
    if (x < 0 || y < 0 || x >= source->width || y >= source->height) return edit.fail("wand seed is outside the raster");
    if (!edit.create(source->width, source->height, 1)) return false;
    Item contiguous = item_attr(options, "contiguous");
    if (get_type_id(contiguous) != LMD_TYPE_NULL && get_type_id(contiguous) != LMD_TYPE_BOOL) return edit.fail("contiguous must be boolean");
    const uint8_t* seed = raster_buffer_pixel(source, (uint32_t)x, (uint32_t)y);
    auto matches = [&](uint32_t px, uint32_t py) {
        const uint8_t* sample = raster_buffer_pixel(source, px, py);
        for (uint8_t c = 0; c < source->channels; c++) if (abs((int)sample[c] - seed[c]) > tolerance * 255) return false;
        return true;
    };
    uint8_t selected = 255;
    if (get_type_id(contiguous) == LMD_TYPE_BOOL && !it2b(contiguous)) {
        for (uint32_t py = 0; py < source->height; py++) for (uint32_t px = 0; px < source->width; px++)
            if (matches(px, py) && !edit.write(px, py, &selected)) return false;
        return true;
    }
    size_t count = (size_t)source->width * source->height;
    ByteStorage* queue = byte_storage_alloc(count * sizeof(uint32_t) + (count + 7) / 8, MEM_CAT_TEMP);
    if (!queue) return edit.fail("cannot allocate bounded flood queue");
    uint32_t* pending = (uint32_t*)queue->data;
    uint8_t* visited = queue->data + count * sizeof(uint32_t); memset(visited, 0, (count + 7) / 8);
    size_t head = 0, tail = 0;
    auto push = [&](uint32_t index) {
        if (visited[index / 8] & (1u << (index % 8))) return;
        visited[index / 8] |= 1u << (index % 8);
        if (matches(index % source->width, index / source->width)) pending[tail++] = index;
    };
    push((uint32_t)y * source->width + (uint32_t)x);
    bool valid = true;
    while (head < tail && valid) {
        uint32_t index = pending[head++], px = index % source->width, py = index / source->width;
        valid = edit.write(px, py, &selected);
        if (px) push(index - 1);
        if (px + 1 < source->width) push(index + 1);
        if (py) push(index - source->width);
        if (py + 1 < source->height) push(index + source->width);
    }
    byte_storage_release(queue); return valid;
}

extern "C" Item fn_image_edit(Item raster, Item options) {
    GUARD_ERROR2(raster, options);
    RootFrame roots(2);
    Rooted<Item> owner(roots, raster), config(roots, options);
    if (get_type_id(config.get()) != LMD_TYPE_MAP) return lambda_image_error("image edit options must be a map");
    Item op = item_attr(config.get(), "op");
    if (!is_text_type_id(get_type_id(op))) return lambda_image_error("image edit requires an operation name");
    const char* operation = op.get_chars();
    // each operation has one checked parameter surface; misspellings never silently fall back.
    const char* allowed = !strcmp(operation, "shape") ? " op width height shape start end points " :
        !strcmp(operation, "combine") ? " op other mode " : !strcmp(operation, "feather") ? " op radius " :
        !strcmp(operation, "stroke") ? " op points size hardness spacing flow opacity color mode selection origin " :
        !strcmp(operation, "fill") ? " op color opacity mode selection origin " :
        !strcmp(operation, "gradient") ? " op color end_color start end kind opacity selection origin " :
        !strcmp(operation, "wand") ? " op point tolerance contiguous " :
        !strcmp(operation, "reframe") ? " op rect fill " :
        !strcmp(operation, "mix") ? " op other selection origin " :
        !strcmp(operation, "extract") ? " op selection origin " :
        (!strcmp(operation, "invert") || !strcmp(operation, "bounds") || !strcmp(operation, "overlay") || !strcmp(operation,"pack") || !strcmp(operation,"histogram")) ? " op " : nullptr;
    if (!allowed) return lambda_image_error("unknown image edit operation");
    MapReader opts = MapReader::fromItem(config.get());
    auto entries = opts.entries(); const char* key; ItemReader field;
    while (entries.next(&key, &field)) {
        char token[96];
        if (strlen(key) > sizeof(token) - 3) return lambda_image_error("unknown image edit parameter");
        token[0] = ' '; memcpy(token + 1, key, strlen(key)); token[strlen(key) + 1] = ' '; token[strlen(key) + 2] = 0;
        if (!strstr(allowed, token)) return lambda_image_error("unknown image edit parameter");
    }
    const RasterBuffer* source = lambda_raster_buffer(owner.get());
    RasterEdit edit;
    if (!strcmp(operation, "shape")) edit_shape(edit, config.get());
    else if (!strcmp(operation, "wand")) edit_wand(edit, source, config.get());
    else if (!strcmp(operation, "invert") || !strcmp(operation, "combine") || !strcmp(operation, "feather"))
        edit_coverage(edit, source, config.get(), operation);
    else if (!strcmp(operation, "stroke") || !strcmp(operation, "fill") || !strcmp(operation, "gradient"))
        edit_paint(edit, source, config.get(), operation);
    else if (!strcmp(operation,"reframe") || !strcmp(operation,"mix") || !strcmp(operation,"extract"))
        edit_map(edit,source,config.get(),operation);
    else if (!source) return lambda_image_error("image edit requires a Raster");
    else if (!strcmp(operation,"histogram")) {
        int64_t counts[4][256]={};
        for (uint32_t y=0;y<source->height;y++) for (uint32_t x=0;x<source->width;x++) {
            const uint8_t* pixel=raster_buffer_pixel(source,x,y);
            bool alpha=source->channels==2 || source->channels==4;
            for (uint8_t c=0;c<source->channels;c++)
                if (!alpha || pixel[source->channels-1] || c==source->channels-1) counts[c][pixel[c]]++;
        }
        RootedValueBuilder result(true), row(true);
        for (uint8_t c=0;c<source->channels;c++) {
            row.value.set({.array=array_plain()});
            for (int i=0;i<256;i++) row.append({.item=i2it(counts[c][i])});
            result.append(row.value.get());
        }
        return result.value.get();
    }
    else if (!strcmp(operation,"pack")) {
        RasterOwner dense; dense.result=raster_buffer_create(source->width,source->height,source->channels,source->size);
        if (!dense.result || !raster_buffer_copy_region(source,0,0,source->width,source->height,-1,dense.result->pixels,source->size))
            return lambda_image_error("cannot pack raster bytes");
        Binary* bytes=heap_binary_from_bytes((const char*)dense.result->pixels,(int64_t)source->size);
        return bytes ? Item{.item=x2it(bytes)} : ItemError;
    }
    else if (!strcmp(operation, "bounds")) {
        if (source->channels != 1) return lambda_image_error("selection bounds require coverage");
        int64_t left = source->width, top = source->height, right = 0, bottom = 0;
        for (uint32_t y = 0; y < source->height; y++) for (uint32_t x = 0; x < source->width; x++) if (raster_buffer_pixel(source, x, y)[0]) {
            if (x < left) left = x; if (y < top) top = y;
            if (x + 1 > right) right = x + 1; if (y + 1 > bottom) bottom = y + 1;
        }
        if (!right || !bottom) return ItemNull;
        RootedValueBuilder result(true);
        int64_t bounds[] = {left, top, right, bottom};
        for (int i = 0; i < 4; i++) result.append({.item = i2it(bounds[i])});
        return result.value.get();
    } else if (!strcmp(operation, "overlay")) {
        if (source->channels != 1) return lambda_image_error("selection overlay requires coverage");
        if (edit.create(source->width, source->height, 4)) for (uint32_t y = 0; y < source->height && !edit.error; y++)
            for (uint32_t x = 0; x < source->width; x++) {
                uint8_t sample[4] = {70, 150, 255, (uint8_t)(raster_buffer_pixel(source, x, y)[0] / 3)};
                if (sample[3] && !edit.write(x, y, sample)) break;
            }
    }
    if (edit.error || !edit.result) return lambda_image_error(edit.error ? edit.error : "image edit failed");
    RasterBuffer* result = edit.result; edit.result = nullptr;
    Item color = source ? item_attr(owner.get(), "color") : ItemNull;
    return lambda_raster_wrap(result, color);
}
