#include "radiant_pdf_graphic.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant_image_job.hpp"
#include "../../core/collection_storage.h"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-error.h"
#include "../../../lib/math_checked.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/arraylist.hpp"
#include "../../../radiant/radiant.hpp"

extern "C" Item vmap_new();

Item radiant_pdf_surface(ImageSurface* image, Rect placement, Rect content) {
    RootFrame roots(3);
    Rooted<Item> result(roots, ItemNull), rgb(roots, ItemNull), alpha(roots, ItemNull);
    ImageSurfaceReadScope read_scope;
    const uint32_t* pixels = nullptr;
    int width = 0, height = 0, stride = 0;
    if (!render_image_resource_pixels(image, placement.width, placement.height, &pixels, &width, &height, &stride) || stride < width)
        return radiant_pdf_error("cannot decode the selected image resource");
    size_t count = 0, bytes = 0;
    if (!math_checked_mul((size_t)width, (size_t)height, &count) ||
            !math_checked_mul(count, 3, &bytes) || bytes > INT64_MAX)
        return radiant_pdf_error("decoded image exceeds supported byte length");
    lam::Temp<uint8_t> colors = lam::temp_array<uint8_t>(bytes, MEM_CAT_RENDER);
    lam::Temp<uint8_t> masks = lam::temp_array<uint8_t>(count, MEM_CAT_RENDER);
    if (!colors || !masks) return radiant_pdf_error("cannot allocate image color and alpha bytes");
    bool opaque = true;
    for (int row = 0; row < height; row++) for (int column = 0; column < width; column++) {
        uint32_t pixel = pixels[(size_t)row * (size_t)stride + (size_t)column];
        if (image->alpha_mode != IMAGE_ALPHA_STRAIGHT) pixel = render_pixel_unpremultiply_abgr(pixel);
        size_t offset = (size_t)row * (size_t)width + (size_t)column;
        colors[offset * 3] = (uint8_t)pixel;
        colors[offset * 3 + 1] = (uint8_t)(pixel >> 8);
        colors[offset * 3 + 2] = (uint8_t)(pixel >> 16);
        masks[offset] = (uint8_t)(pixel >> 24);
        opaque = opaque && masks[offset] == 255;
    }
    rgb.set({.item = x2it(heap_binary_from_bytes((const char*)colors.get(), bytes))});
    if (!opaque) alpha.set({.item = x2it(heap_binary_from_bytes((const char*)masks.get(), count))});
    if (get_type_id(rgb.get()) != LMD_TYPE_BINARY || (!opaque && get_type_id(alpha.get()) != LMD_TYPE_BINARY))
        return radiant_pdf_error("cannot retain selected image bytes");
    result.set(vmap_new());
    if (get_type_id(result.get()) != LMD_TYPE_VMAP) return radiant_pdf_error("cannot allocate selected image result");
    radiant_rooted_obj_set(result, "pixels", rgb.get());
    radiant_rooted_obj_set(result, "alpha", alpha.get());
    radiant_rooted_obj_set(result, "width", {.item = i2it(width)});
    radiant_rooted_obj_set(result, "height", {.item = i2it(height)});
    const char* names[] = {"x", "y", "display_width", "display_height", "clip_x", "clip_y", "clip_width", "clip_height"};
    const float values[] = {placement.x, placement.y, placement.width, placement.height,
        content.x, content.y, content.width, content.height};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        radiant_rooted_obj_set(result, names[i], push_d(values[i]));
    radiant_rooted_obj_set(result, "clipped", {.item = b2it(!view_geometry_rect_contains_rect(content, placement, 0.01f))});
    return result.get();
}

namespace {
Item pdf_color(Color color) {
    RootFrame roots(1);
    Rooted<Item> value(roots, vmap_new());
    const char* names[] = {"r", "g", "b", "a"};
    const uint8_t channels[] = {color.r, color.g, color.b, color.a};
    for (size_t i = 0; i < 4; i++) radiant_rooted_obj_set(value, names[i], {.item = i2it(channels[i])});
    return value.get();
}

Item pdf_matrix(const RdtMatrix& matrix) {
    RootFrame roots(1);
    Rooted<Item> value(roots, {.array = array_plain()});
    const float parts[] = {matrix.e11, matrix.e21, matrix.e12, matrix.e22, matrix.e13, matrix.e23};
    for (float part : parts) array_push(value.get().array, {.item = f32_to_item(part)});
    return value.get();
}

bool pdf_vector_resource(const PaintList*, UiContext*, Rooted<Item>&,
    const RdtMatrix* = nullptr, Array* = nullptr);
struct PdfSvgResource {
    UiContext* ui;
    Rooted<Item>* paths;
    RdtMatrix matrix;
    Array* clips;
};
bool pdf_svg_shapes(PaintList* paint, void* data) {
    PdfSvgResource* source = (PdfSvgResource*)data;
    return pdf_vector_resource(paint, source->ui, *source->paths, &source->matrix, source->clips);
}

// The resource contains independent geometric shapes, with each clip and
// transform already resolved. It carries no paint opcodes or native pointers.
bool pdf_vector_resource(const PaintList* paint, UiContext* ui, Rooted<Item>& paths,
        const RdtMatrix* initial, Array* parent_clips) {
    RootFrame roots(4);
    Rooted<Item> clips(roots, {.array = array_plain()}), entry(roots, ItemNull),
        commands(roots, ItemNull), active_clips(roots, ItemNull);
    RdtMatrix matrix = initial ? *initial : rdt_matrix_identity();
    int64_t inherited_clips = parent_clips ? parent_clips->length : 0;
    for (int64_t i = 0; i < inherited_clips; i++) array_push(clips.get().array, array_item_read(parent_clips, i));
    lam::ArrayList<RdtMatrix> transforms(MEM_CAT_RENDER);
    for (int index = 0; index < paint->item_count(); index++) {
        const PaintCmd& command = paint->data()[index];
        if (paint_op_has_flags(command.op, PAINT_OP_FLAG_RASTER_NOOP)) continue;
        if (command.op == PAINT_PUSH_TRANSFORM) {
            if (!transforms.push_back(matrix)) return false;
            matrix = rdt_matrix_multiply(&matrix, &command.push_transform.transform);
            continue;
        }
        if (command.op == PAINT_POP_TRANSFORM) {
            if (transforms.empty()) return false;
            matrix = transforms.back(); transforms.remove(transforms.size() - 1);
            continue;
        }
        if (command.op == PAINT_PUSH_CLIP) {
            const PaintPushClip& clip = command.push_clip;
            RdtMatrix placed = clip.has_transform ? rdt_matrix_multiply(&matrix, &clip.transform) : matrix;
            commands.set(radiant_pdf_path_array(clip.clip_path, &placed));
            if (get_type_id(commands.get()) == LMD_TYPE_ERROR) return false;
            entry.set(vmap_new());
            radiant_rooted_obj_set(entry, "commands", commands.get());
            radiant_rooted_obj_set(entry, "evenodd", {.item = b2it(clip.rule == RDT_FILL_EVEN_ODD)});
            array_push(clips.get().array, entry.get());
            continue;
        }
        if (command.op == PAINT_POP_CLIP) {
            if (!clips.get().array->length) return false;
            clips.get().array->length--;
            continue;
        }
        if (command.op == PAINT_SVG_SUBSCENE) {
            PdfSvgResource source = {ui, &paths, matrix, clips.get().array};
            if (!render_svg_subscene_with_paint(&command.svg_subscene, pdf_svg_shapes, &source, false)) return false;
            continue;
        }
        RdtPath* owned = nullptr;
        const RdtPath* path = nullptr;
        Color color = {};
        RdtMatrix placed = matrix;
        const PaintStrokePath* stroke = nullptr;
        RdtFillRule rule = RDT_FILL_WINDING;
        switch (command.op) {
        case PAINT_FILL_RECT: {
            const PaintFillRect& box = command.fill_rect;
            owned = rdt_path_new();
            if (owned) rdt_path_add_rect(owned, box.x, box.y, box.w, box.h, 0, 0);
            path = owned; color = box.color; break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            const PaintFillRoundedRect& box = command.fill_rounded_rect;
            owned = rdt_path_new();
            if (owned) rdt_path_add_rect(owned, box.x, box.y, box.w, box.h, box.rx, box.ry);
            path = owned; color = box.color; break;
        }
        case PAINT_FILL_PATH:
            path = command.fill_path.path; color = command.fill_path.color; rule = command.fill_path.rule;
            if (command.fill_path.has_transform) placed = rdt_matrix_multiply(&matrix, &command.fill_path.transform);
            break;
        case PAINT_STROKE_PATH:
            stroke = &command.stroke_path; path = stroke->path; color = stroke->color;
            if (stroke->has_transform) placed = rdt_matrix_multiply(&matrix, &stroke->transform);
            break;
        case PAINT_GLYPH_RUN:
            owned = render_path_create_paint_glyph_run(&command.glyph_run, ui ? ui->font_ctx.get() : nullptr);
            path = owned; color = command.glyph_run.color;
            if (command.glyph_run.has_transform) placed = rdt_matrix_multiply(&matrix, &command.glyph_run.transform);
            break;
        default: return false;
        }
        struct PathCleanup { RdtPath* path; ~PathCleanup() { if (path) rdt_path_free(path); } } cleanup = {owned};
        if (!path) return false;
        RdtMatrix identity = rdt_matrix_identity();
        commands.set(radiant_pdf_path_array(path, &identity));
        if (get_type_id(commands.get()) == LMD_TYPE_ERROR) return false;
        entry.set(vmap_new());
        radiant_rooted_obj_set(entry, "commands", commands.get());
        radiant_rooted_obj_set(entry, "color", pdf_color(color));
        radiant_rooted_obj_set(entry, "matrix", pdf_matrix(placed));
        radiant_rooted_obj_set(entry, "stroke", {.item = b2it(stroke != nullptr)});
        radiant_rooted_obj_set(entry, "evenodd", {.item = b2it(rule == RDT_FILL_EVEN_ODD)});
        active_clips.set({.array = array_plain()});
        for (int64_t c = 0; c < clips.get().array->length; c++)
            array_push(active_clips.get().array, array_item_read(clips.get().array, c));
        radiant_rooted_obj_set(entry, "clips", active_clips.get());
        if (stroke) {
            radiant_rooted_obj_set(entry, "width", {.item = f32_to_item(stroke->width)});
            radiant_rooted_obj_set(entry, "cap", {.item = i2it(stroke->cap)});
            radiant_rooted_obj_set(entry, "join", {.item = i2it(stroke->join)});
            radiant_rooted_obj_set(entry, "miter", {.item = f32_to_item(stroke->miter_limit)});
            radiant_rooted_obj_set(entry, "phase", {.item = f32_to_item(stroke->dash_phase)});
            commands.set({.array = array_plain()});
            for (int c = 0; c < stroke->dash_count; c++) array_push(commands.get().array, {.item = f32_to_item(stroke->dash_array[c])});
            radiant_rooted_obj_set(entry, "dash", commands.get());
        }
        array_push(paths.get().array, entry.get());
    }
    return transforms.empty() && clips.get().array->length == inherited_clips;
}
} // namespace

Item radiant_pdf_graphic(const PaintList* paint, UiContext* ui, Bound bounds,
        float viewport_width, float viewport_height, DisplayList* backdrop, const RdtMatrix* transform) {
    if (!paint || !ui) return radiant_pdf_error("graphic helper requires a live native resource");
    RootFrame roots(2);
    Rooted<Item> result(roots, vmap_new()), paths(roots, {.array = array_plain()});
    if (pdf_vector_resource(paint, ui, paths)) {
        radiant_rooted_obj_set(result, "paths", paths.get());
        return result.get();
    }
    RdtMatrix inverse = rdt_matrix_identity();
    if (backdrop && transform && !rdt_matrix_inverse(transform, &inverse))
        return radiant_pdf_error("backdrop capture requires an invertible placement");
    ImageSurface* surface = nullptr;
    Rect placement = {};
    if (!render_paint_snapshot(paint, bounds, viewport_width, viewport_height,
            ui_context_raster_scale(ui), &surface, &placement, backdrop, backdrop ? transform : nullptr))
        return radiant_pdf_error("cannot rasterize the selected native graphic");
    struct SurfaceCleanup { ImageSurface* value; ~SurfaceCleanup() { image_surface_destroy(value); } } cleanup = {surface};
    paths.set(radiant_pdf_surface(surface, placement, placement));
    if (get_type_id(paths.get()) == LMD_TYPE_ERROR) return paths.get();
    radiant_rooted_obj_set(result, "image", paths.get());
    // Backdrop sampling is in page coordinates; cancel the enclosing PDF transform for this image.
    if (backdrop && transform) radiant_rooted_obj_set(result, "matrix", pdf_matrix(inverse));
    return result.get();
}
