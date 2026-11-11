#include "image_raster.hpp"
#include "heap_api.h"
#include "lambda-error.h"
#include "lambda-number-runtime.hpp"
#include "lambda/core/mark_reader.hpp"
#include "value_builder.hpp"
#include "lib/image_process.h"
#include <math.h>

// native kernels operate on owned tiles; the adapter roots only the input values (D6.3.3).
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
        return lambda_image_color(value,channels,out) || fail("paint color must match target channels with samples in [0,1]");
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

// Lambda_Pkg_Paint PT13: stroke coverage comes from lambda.paint, whose engine accumulates the
// brush's wash dabs into a coverage layer of u8 (tile, tile) arrays (null for untouched tiles).
// The tiles are read in place; nothing allocates from the GC heap while they are in use (D5.4.4).
struct PaintCoverage {
    ByteStorage* index = nullptr;
    const uint8_t** tiles = nullptr;
    uint32_t tile = 0, columns = 0, rows = 0;
    uint32_t left = 0, top = 0, right = 0, bottom = 0;   // the samples whose coverage the batch changed
    ~PaintCoverage() { if (index) byte_storage_release(index); }
    double at(uint32_t x, uint32_t y) const {
        const uint8_t* samples = tiles[(size_t)(y / tile) * columns + x / tile];
        return samples ? samples[(size_t)(y % tile) * tile + x % tile] / 255.0 : 0;
    }
    bool read(RasterEdit& edit, Item layer, Item rect, const RasterBuffer* source) {
        double width, height, size;
        if (!is_map_family_type_id(get_type_id(layer)) || !edit_is(item_attr(layer, "format"), "coverage") ||
            !edit_is(item_attr(layer, "sample"), "u8")) return edit.fail("stroke coverage must be a lambda.paint u8 coverage layer");
        if (!edit.number(layer, "width", 0, 1, 16000000, &width) || !edit.number(layer, "height", 0, 1, 16000000, &height) ||
            !edit.number(layer, "tile", 0, 1, 4096, &size) || width != source->width || height != source->height ||
            floor(size) != size) return edit.fail("stroke coverage must match the raster extent");
        tile = (uint32_t)size;
        columns = (source->width - 1) / tile + 1; rows = (source->height - 1) / tile + 1;
        ItemReader list(item_attr(layer, "tiles").to_const());
        if (!list.isArray() || list.asArray().length() != (int64_t)columns * rows) return edit.fail("stroke coverage lists the wrong number of tiles");
        index = byte_storage_alloc((size_t)columns * rows * sizeof(uint8_t*), MEM_CAT_TEMP);
        if (!index) return edit.fail("cannot allocate the stroke coverage index");
        tiles = (const uint8_t**)index->data;
        for (int64_t i = 0; i < (int64_t)columns * rows; i++) {
            Item entry = list.asArray().get(i).item();
            tiles[i] = nullptr;
            if (get_type_id(entry) == LMD_TYPE_NULL) continue;
            int64_t shape[LAMBDA_ARRAY_NUM_MAX_NDIM], strides[LAMBDA_ARRAY_NUM_MAX_NDIM];
            if (get_type_id(entry) != LMD_TYPE_ARRAY_NUM || entry.array_num->get_elem_type() != ELEM_UINT8 ||
                array_num_get_shape_strides(entry.array_num, shape, strides) != 2 || shape[0] != tile || shape[1] != tile ||
                !array_num_is_dense(entry.array_num)) return edit.fail("stroke coverage tiles must be dense u8 (tile, tile) arrays");
            tiles[i] = (const uint8_t*)entry.array_num->data;
        }
        if (get_type_id(rect) == LMD_TYPE_NULL) return true;
        int64_t bounds[4];
        if (!lambda_image_rect(rect, bounds)) return edit.fail("stroke coverage rect must be [x0, y0, x1, y1]");
        left = (uint32_t)fmax(0, (double)bounds[0]); top = (uint32_t)fmax(0, (double)bounds[1]);
        right = (uint32_t)fmin(source->width, fmax(0, (double)bounds[2]));
        bottom = (uint32_t)fmin(source->height, fmax(0, (double)bounds[3]));
        if (right < left) right = left; if (bottom < top) bottom = top;
        return true;
    }
    // the public continuation keeps a gray Raster of the cumulative coverage.
    RasterBuffer* gray(RasterEdit& edit, uint32_t width, uint32_t height) const {
        if (!edit.create(width, height, 1)) return nullptr;
        for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < columns; c++) {
            const uint8_t* samples = tiles[(size_t)r * columns + c];
            if (!samples) continue;
            for (uint32_t y = r * tile; y < height && y < (r + 1) * tile; y++)
                for (uint32_t x = c * tile; x < width && x < (c + 1) * tile; x++) {
                    uint8_t s = samples[(size_t)(y - r * tile) * tile + (x - c * tile)];
                    if (s && !edit.write(x, y, &s)) return nullptr;
                }
        }
        RasterBuffer* result = edit.result; edit.result = nullptr;
        return result;
    }
};

struct RetouchPaint {
    const RasterBuffer *sample=nullptr, *context=nullptr;
    double offset[3]={}, context_offset[3]={}, strength=0.5;
    int radius=3;
    const char* kind=nullptr;
    bool sampled=false, heal=false, neighborhood=false;

    bool configure(RasterEdit& edit, const RasterBuffer* source, Item options) {
        if (source->channels!=4) return edit.fail("retouch requires RGBA samples");
        Item name=item_attr(options,"kind");
        if (!is_text_type_id(get_type_id(name))) return edit.fail("retouch requires a tool kind");
        kind=name.get_chars();
        sampled=!strcmp(kind,"clone") || !strcmp(kind,"heal"); heal=!strcmp(kind,"heal");
        neighborhood=heal || !strcmp(kind,"blur") || !strcmp(kind,"sharpen");
        if (!sampled && strcmp(kind,"dodge") && strcmp(kind,"burn") && strcmp(kind,"sponge") && !neighborhood)
            return edit.fail("unknown retouch tool");
        double requested_radius;
        if (!edit.number(options,"strength",0.5,0,1,&strength) ||
            !edit.number(options,"radius",3,1,32,&requested_radius) || floor(requested_radius)!=requested_radius)
            return edit.fail("retouch radius must be an integer from 1 to 32");
        radius=(int)requested_radius;
        context=lambda_raster_buffer(item_attr(options,"context"));
        if (get_type_id(item_attr(options,"context"))!=LMD_TYPE_NULL && (!context || context->channels!=4))
            return edit.fail("retouch context requires RGBA samples");
        if (!context) context=source;
        if (get_type_id(item_attr(options,"context_offset"))!=LMD_TYPE_NULL &&
            !edit.point(item_attr(options,"context_offset"),context_offset)) return false;
        if (sampled) {
            sample=lambda_raster_buffer(item_attr(options,"sample"));
            if (!sample || sample->channels!=4 || !edit.point(item_attr(options,"sample_offset"),offset))
                return edit.fail("clone/heal require an RGBA source snapshot and source offset");
        }
        return true;
    }
    void mean(const RasterBuffer* pixels, int64_t x, int64_t y, double* out) const {
        out[0]=out[1]=out[2]=0; double total=0;
        int64_t left=x-radius,top=y-radius,right=x+radius+1,bottom=y+radius+1;
        if (left<0) left=0; if (top<0) top=0;
        if (right>pixels->width) right=pixels->width; if (bottom>pixels->height) bottom=pixels->height;
        for (int64_t yy=top;yy<bottom;yy++) for (int64_t xx=left;xx<right;xx++) {
            const uint8_t* p=raster_buffer_pixel(pixels,(uint32_t)xx,(uint32_t)yy);
            double a=p[3]/255.0; total+=a;
            for (int c=0;c<3;c++) out[c]+=raster_linear_sample(p[c])*a;
        }
        if (total>0) for (int c=0;c<3;c++) out[c]/=total;
    }
    bool pixel(const RasterBuffer* source, uint32_t x, uint32_t y, uint8_t* out) const {
        const uint8_t* original=raster_buffer_pixel(source,x,y);
        if (sampled) {
            int64_t sx=(int64_t)floor(x+offset[0]),sy=(int64_t)floor(y+offset[1]);
            if (sx<0 || sy<0 || sx>=sample->width || sy>=sample->height) return false;
            memcpy(out,raster_buffer_pixel(sample,(uint32_t)sx,(uint32_t)sy),4);
            if (heal && out[3]) {
                double from[3],to[3]; mean(sample,sx,sy,from);
                mean(context,(int64_t)floor(x+context_offset[0]),(int64_t)floor(y+context_offset[1]),to);
                // color-match source texture to the destination neighborhood; no Poisson-solver claim.
                for (int c=0;c<3;c++) out[c]=raster_encoded_sample(raster_linear_sample(out[c])+strength*(to[c]-from[c]));
            }
            return out[3]>0;
        }
        if (!original[3]) return false;
        memcpy(out,original,4);
        double average[3]={}, values[3];
        if (neighborhood) mean(source,x,y,average);
        for (int c=0;c<3;c++) values[c]=raster_linear_sample(original[c]);
        double luminance=values[0]*0.2126+values[1]*0.7152+values[2]*0.0722;
        for (int c=0;c<3;c++) {
            double value=!strcmp(kind,"dodge") ? values[c]*pow(2,strength) :
                !strcmp(kind,"burn") ? values[c]/pow(2,strength) :
                !strcmp(kind,"sponge") ? values[c]*(1-strength)+luminance*strength :
                !strcmp(kind,"blur") ? values[c]*(1-strength)+average[c]*strength :
                values[c]+strength*(values[c]-average[c]);
            out[c]=raster_encoded_sample(value);
        }
        return true;
    }
};

static bool edit_paint(RasterEdit& edit, const RasterBuffer* source, Item options, const char* operation,
        PaintCoverage* accumulated = nullptr) {
    if (!source || (source->channels != 1 && source->channels != 4)) return edit.fail("paint requires gray coverage or RGBA samples");
    double opacity, origin[3] = {};
    if (!edit.number(options, "opacity", 1, 0, 1, &opacity)) return false;
    Item position = item_attr(options, "origin");
    if (get_type_id(position) != LMD_TYPE_NULL && !edit.point(position, origin)) return false;
    const RasterBuffer* selection = lambda_raster_buffer(item_attr(options, "selection"));
    if (get_type_id(item_attr(options, "selection")) != LMD_TYPE_NULL && (!selection || selection->channels != 1))
        return edit.fail("paint selection requires gray coverage");
    bool retouch=!strcmp(operation,"retouch");
    bool stroke = !strcmp(operation, "stroke") || retouch, erase = edit_is(item_attr(options, "mode"), "erase");
    if (get_type_id(item_attr(options, "mode")) != LMD_TYPE_NULL && !erase && !edit_is(item_attr(options, "mode"), "paint"))
        return edit.fail("unknown paint mode");
    uint8_t color[4] = {0, 0, 0, 255}, end_color[4] = {};
    if (!retouch && !erase && !edit.color(item_attr(options, "color"), source->channels, color)) return false;
    RetouchPaint tool;
    if (retouch && !tool.configure(edit,source,options)) return false;
    double start[3] = {}, end[3] = {}, length_squared = 0;
    bool gradient = !strcmp(operation, "gradient"), radial = edit_is(item_attr(options, "kind"), "radial");
    if (gradient) {
        if (!edit.color(item_attr(options, "end_color"), source->channels, end_color) ||
            !edit.point(item_attr(options, "start"), start) || !edit.point(item_attr(options, "end"), end)) return false;
        if (!radial && !edit_is(item_attr(options, "kind"), "linear")) return edit.fail("unknown gradient kind");
        length_squared = (end[0] - start[0]) * (end[0] - start[0]) + (end[1] - start[1]) * (end[1] - start[1]);
        if (length_squared <= 0) return edit.fail("gradient endpoints must differ");
    }
    PaintCoverage local_coverage;
    PaintCoverage& coverage = accumulated ? *accumulated : local_coverage;
    if (stroke && !coverage.read(edit, item_attr(options, "coverage"), item_attr(options, "rect"), source)) return false;
    if (retouch && tool.neighborhood && coverage.right>coverage.left && coverage.bottom>coverage.top &&
        (uint64_t)(coverage.right-coverage.left)*(coverage.bottom-coverage.top)*(2*tool.radius+1)*(2*tool.radius+1)*(tool.heal?2:1)>128000000)
        return edit.fail("retouch neighborhood exceeds the work budget");
    // a stroke batch recomposites only its changed coverage onto the previous preview.
    Item preview = item_attr(options, "base");
    const RasterBuffer* base = get_type_id(preview)==LMD_TYPE_NULL ? source : lambda_raster_buffer(preview);
    if (!base || base->width!=source->width || base->height!=source->height || base->channels!=source->channels)
        return edit.fail("invalid stroke preview raster");
    // coverage accumulates across batches; opacity is always applied to the frozen source.
    edit.result = raster_buffer_fork(base);
    if (!edit.result) return edit.fail("cannot allocate paint tile index");
    uint32_t left = stroke ? coverage.left : 0, top = stroke ? coverage.top : 0;
    uint32_t right = stroke ? coverage.right : source->width, bottom = stroke ? coverage.bottom : source->height;
    for (uint32_t y = top; y < bottom; y++) for (uint32_t x = left; x < right; x++) {
        double alpha = opacity * edit_selection(selection, origin, x, y);
        if (stroke) alpha *= coverage.at(x, y);
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
        if (retouch && !tool.pixel(source,x,y,paint)) continue;
        if (retouch && !tool.sampled) {
            // tonal and neighborhood tools change color without increasing existing alpha.
            for (int c=0;c<3;c++) sample[c]=raster_encoded_sample(raster_linear_sample(sample[c])*(1-alpha)+raster_linear_sample(paint[c])*alpha);
        } else raster_mix_pixel(sample, paint, source->channels, alpha, erase);
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

static bool edit_parameters(Item options, const char* allowed) {
    if (get_type_id(options)!=LMD_TYPE_MAP) return false;
    MapReader opts = MapReader::fromItem(options);
    auto entries = opts.entries(); const char* key; ItemReader field;
    while (entries.next(&key, &field)) {
        char token[96];
        if (strlen(key)>sizeof(token)-3) return false;
        token[0]=' '; memcpy(token+1,key,strlen(key)); token[strlen(key)+1]=' '; token[strlen(key)+2]=0;
        if (!strstr(allowed,token)) return false;
    }
    return true;
}

// one batch of an incremental stroke: {raster, coverage, rect}. lambda.image keeps the
// continuation (source, settings, carry, work) in Lambda; the composite stays native.
static Item edit_stroke_step(Item source, Item config) {
    RootFrame roots(2);
    Rooted<Item> owner(roots,source), options(roots,config);
    const RasterBuffer* pixels=lambda_raster_buffer(owner.get());
    if (!pixels) return lambda_image_error("stroke requires a Raster");
    RasterEdit edit, gray_edit;
    PaintCoverage coverage;
    if (!edit_paint(edit,pixels,options.get(),"stroke",&coverage))
        return lambda_image_error(edit.error ? edit.error : "stroke continuation failed");
    // the coverage tiles are GC arrays: copy them before the first wrap allocates (D5.4.4).
    RasterBuffer* gray=coverage.gray(gray_edit,pixels->width,pixels->height);
    if (!gray) return lambda_image_error(gray_edit.error ? gray_edit.error : "cannot copy stroke coverage");
    RootedValueBuilder result,rect(true);
    RasterBuffer* output=edit.result; edit.result=nullptr;
    Item wrapped=lambda_raster_wrap(output,item_attr(owner.get(),"color"));
    if (item_is_error(wrapped)) { raster_buffer_destroy(gray); return wrapped; }
    result.put("raster",wrapped);
    wrapped=lambda_raster_wrap(gray,ItemNull);
    if (item_is_error(wrapped)) return wrapped;
    result.put("coverage",wrapped);
    // selection clipping and zero opacity may accumulate coverage without changing any samples.
    if (edit.right>edit.left && edit.bottom>edit.top) {
        uint32_t bounds[]={edit.left,edit.top,edit.right,edit.bottom};
        for (int i=0;i<4;i++) rect.append({.item=i2it(bounds[i])});
        result.put("rect",rect.value.get());
    } else result.put("rect",ItemNull);
    return result.value.get();
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
        !strcmp(operation, "stroke") ? " op coverage rect base opacity color mode selection origin " :
        !strcmp(operation, "stroke_step") ? " op coverage rect base opacity color mode selection origin " :
        !strcmp(operation,"retouch") ? " op coverage rect opacity selection origin kind strength radius sample sample_offset context context_offset " :
        !strcmp(operation, "fill") ? " op color opacity mode selection origin " :
        !strcmp(operation, "gradient") ? " op color end_color start end kind opacity selection origin " :
        !strcmp(operation, "wand") ? " op point tolerance contiguous " :
        !strcmp(operation, "reframe") ? " op rect fill " :
        !strcmp(operation, "mix") ? " op other selection origin " :
        !strcmp(operation, "extract") ? " op selection origin " :
        (!strcmp(operation, "invert") || !strcmp(operation, "bounds") || !strcmp(operation, "overlay") || !strcmp(operation,"pack") || !strcmp(operation,"histogram")) ? " op " : nullptr;
    if (!allowed) return lambda_image_error("unknown image edit operation");
    if (!edit_parameters(config.get(),allowed)) return lambda_image_error("unknown image edit parameter");
    if (!strcmp(operation,"stroke_step")) return edit_stroke_step(owner.get(),config.get());
    const RasterBuffer* source = lambda_raster_buffer(owner.get());
    RasterEdit edit;
    if (!strcmp(operation, "shape")) edit_shape(edit, config.get());
    else if (!strcmp(operation, "wand")) edit_wand(edit, source, config.get());
    else if (!strcmp(operation, "invert") || !strcmp(operation, "combine") || !strcmp(operation, "feather"))
        edit_coverage(edit, source, config.get(), operation);
    else if (!strcmp(operation, "stroke") || !strcmp(operation, "fill") || !strcmp(operation, "gradient") || !strcmp(operation,"retouch"))
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
