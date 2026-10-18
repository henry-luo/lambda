#include "radiant_pdf_graphic.hpp"
#include "radiant_pdf_text.hpp"
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

Item radiant_pdf_color(Color color) {
    RootFrame roots(1);
    Rooted<Item> value(roots, vmap_new());
    const char* names[] = {"r", "g", "b", "a"};
    const uint8_t channels[] = {color.r, color.g, color.b, color.a};
    for (size_t i = 0; i < 4; i++) radiant_rooted_obj_set(value, names[i], {.item = i2it(channels[i])});
    return value.get();
}

namespace {
Item pdf_matrix(const RdtMatrix& matrix) {
    RootFrame roots(1);
    Rooted<Item> value(roots, {.array = array_plain()});
    const float parts[] = {matrix.e11, matrix.e21, matrix.e12, matrix.e22, matrix.e13, matrix.e23};
    for (float part : parts) array_push(value.get().array, {.item = f32_to_item(part)});
    return value.get();
}

bool pdf_vector_resource(const PaintList*, UiContext*, Rooted<Item>&, Rooted<Item>&, PdfTextResources*,
    const RdtMatrix* = nullptr, Array* = nullptr, bool = false, int = 0, int = -1);
struct PdfSvgResource {
    UiContext* ui;
    Rooted<Item>* paths;
    RdtMatrix matrix;
    Array* clips;
    Rooted<Item>* error;
    PdfTextResources* text_resources;
    bool text_only;
};
bool pdf_svg_shapes(PaintList* paint, void* data) {
    PdfSvgResource* source = (PdfSvgResource*)data;
    return pdf_vector_resource(paint, source->ui, *source->paths, *source->error, source->text_resources,
        &source->matrix, source->clips, source->text_only);
}

// The resource contains independent geometric shapes, with each clip and
// transform already resolved. It carries no paint opcodes or native pointers.
bool pdf_vector_resource(const PaintList* paint, UiContext* ui, Rooted<Item>& paths,
        Rooted<Item>& error, PdfTextResources* text_resources,
        const RdtMatrix* initial, Array* parent_clips, bool text_only, int begin, int end) {
    RootFrame roots(6);
    Rooted<Item> clips(roots, {.array = array_plain()}), entry(roots, ItemNull),
        commands(roots, ItemNull), active_clips(roots, ItemNull), text(roots, ItemNull), rect_commands(roots, ItemNull);
    Color rect_color = {};
    RdtMatrix rect_matrix = rdt_matrix_identity();
    int64_t rect_clip_count = -1;
    RdtMatrix matrix = initial ? *initial : rdt_matrix_identity();
    int64_t inherited_clips = parent_clips ? parent_clips->length : 0;
    for (int64_t i = 0; i < inherited_clips; i++) array_push(clips.get().array, array_item_read(parent_clips, i));
    lam::ArrayList<RdtMatrix> transforms(MEM_CAT_RENDER);
    if (end < 0) end = paint->item_count();
    for (int index = begin; index < end; index++) {
        const PaintCmd& command = paint->data()[index];
        if (paint_op_has_flags(command.op, PAINT_OP_FLAG_RASTER_NOOP)) continue;
        bool rectangle = command.op == PAINT_FILL_RECT || (command.op == PAINT_FILL_ROUNDED_RECT &&
            command.fill_rounded_rect.rx == 0 && command.fill_rounded_rect.ry == 0);
        bool scope = command.op == PAINT_PUSH_TRANSFORM || command.op == PAINT_POP_TRANSFORM ||
            command.op == PAINT_PUSH_CLIP || command.op == PAINT_POP_CLIP;
        if (!rectangle && !scope) rect_commands.set(ItemNull);
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
            PdfSvgResource source = {ui, &paths, matrix, clips.get().array, &error, text_resources, text_only};
            if (!render_svg_subscene_with_paint(&command.svg_subscene, pdf_svg_shapes, &source, false, true,
                    text_resources ? &text_resources->contexts : nullptr)) return false;
            continue;
        }
        if (command.op == PAINT_BEGIN_EFFECT_GROUP && !text_only) {
            const PaintEffectGroup& group = command.effect_group;
            if (group.filter || group.backdrop || group.backdrop_filter || group.shadow ||
                    group.mask.gradient || group.blend_mode || group.has_transform || group.has_clip) return false;
            int stop = index + 1, depth = 1;
            for (; stop < end; stop++) {
                if (paint->data()[stop].op == PAINT_BEGIN_EFFECT_GROUP) depth++;
                if (paint->data()[stop].op == PAINT_END_EFFECT_GROUP && --depth == 0) break;
            }
            if (stop == end) return false;
            commands.set({.array = array_plain()});
            if (!pdf_vector_resource(paint, ui, commands, error, text_resources, &matrix, clips.get().array,
                    false, index + 1, stop)) return false;
            entry.set(vmap_new());
            radiant_rooted_obj_set(entry, "group", commands.get());
            radiant_rooted_obj_set(entry, "opacity", push_d(group.opacity));
            active_clips.set({.array = array_plain()});
            PaintList body;
            Bound ink = {};
            if (!paint_list_borrow_range(&body, paint, index + 1, stop) ||
                    !render_paint_ink_bounds(&body, &matrix, &ink)) return false;
            // layout bounds omit overflowing glyph/decorative ink; a PDF form clips to its BBox.
            const float bounds[] = {floorf(fminf(group.bounds.left, ink.left)), floorf(fminf(group.bounds.top, ink.top)),
                ceilf(fmaxf(group.bounds.right, ink.right)), ceilf(fmaxf(group.bounds.bottom, ink.bottom))};
            for (float value : bounds) array_push(active_clips.get().array, push_d(value));
            radiant_rooted_obj_set(entry, "bounds", active_clips.get());
            array_push(paths.get().array, entry.get());
            index = stop; continue;
        }
        if (text_only && command.op != PAINT_GLYPH_RUN) continue;
        RdtPath* owned = nullptr;
        const RdtPath* path = nullptr;
        Color color = {};
        RdtMatrix placed = matrix;
        const PaintStrokePath* stroke = nullptr;
        text.set(ItemNull);
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
            text.set(radiant_pdf_text(&command.glyph_run, ui, text_resources));
            if (get_type_id(text.get()) == LMD_TYPE_ERROR) { error.set(text.get()); return false; }
            color = command.glyph_run.color;
            if (command.glyph_run.has_transform) placed = rdt_matrix_multiply(&matrix, &command.glyph_run.transform);
            break;
        default: return false;
        }
        struct PathCleanup { RdtPath* path; ~PathCleanup() { if (path) rdt_path_free(path); } } cleanup = {owned};
        if (!path && get_type_id(text.get()) == LMD_TYPE_NULL) return false;
        entry.set(vmap_new());
        if (get_type_id(text.get()) != LMD_TYPE_NULL) radiant_rooted_obj_set(entry, "text", text.get());
        else {
            RdtMatrix identity = rdt_matrix_identity();
            commands.set(radiant_pdf_path_array(path, &identity));
            if (get_type_id(commands.get()) == LMD_TYPE_ERROR) return false;
            // union adjacent opaque same-color rectangles before edge antialiasing;
            // overlapping primitives must not compound coverage inside opacity forms.
            if (rectangle && color.a == 255) {
                if (get_type_id(rect_commands.get()) == LMD_TYPE_ARRAY && rect_color.c == color.c &&
                        rect_clip_count == 0 && clips.get().array->length == 0 &&
                        !memcmp(&rect_matrix, &placed, sizeof(placed))) {
                    for (int64_t i = 0; i < commands.get().array->length; i++)
                        array_push(rect_commands.get().array, array_item_read(commands.get().array, i));
                    continue;
                }
                rect_commands.set(commands.get()); rect_color = color;
                rect_matrix = placed; rect_clip_count = clips.get().array->length;
            } else rect_commands.set(ItemNull);
            radiant_rooted_obj_set(entry, "commands", commands.get());
        }
        radiant_rooted_obj_set(entry, "color", radiant_pdf_color(color));
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
        float viewport_width, float viewport_height, DisplayList* backdrop, const RdtMatrix* transform,
        PdfTextResources* text_resources) {
    if (!paint || !ui) return radiant_pdf_error("graphic helper requires a live native resource");
    RootFrame roots(3);
    Rooted<Item> result(roots, vmap_new()), paths(roots, {.array = array_plain()}), error(roots, ItemNull);
    if (pdf_vector_resource(paint, ui, paths, error, text_resources)) {
        radiant_rooted_obj_set(result, "paths", paths.get());
        return result.get();
    }
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    // pixel-only effects keep an invisible semantic text layer over the capture.
    paths.set({.array = array_plain()});
    if (!pdf_vector_resource(paint, ui, paths, error, text_resources, nullptr, nullptr, true))
        return get_type_id(error.get()) == LMD_TYPE_ERROR ? error.get() : radiant_pdf_error("cannot retain raster-effect text");
    radiant_rooted_obj_set(result, "texts", paths.get());
    RdtMatrix inverse = rdt_matrix_identity();
    ImageSurface* surface = nullptr;
    Rect placement = {};
    if (!render_paint_snapshot(paint, bounds, viewport_width, viewport_height,
            ui_context_raster_scale(ui), &surface, &placement, backdrop, transform))
        return radiant_pdf_error("cannot rasterize the selected native graphic");
    if (!surface) {
        paths.set({.array = array_plain()});
        radiant_rooted_obj_set(result, "paths", paths.get());
        return result.get();
    }
    struct SurfaceCleanup { ImageSurface* value; ~SurfaceCleanup() { image_surface_destroy(value); } } cleanup = {surface};
    if (transform && !rdt_matrix_inverse(transform, &inverse))
        return radiant_pdf_error("raster capture requires an invertible placement");
    paths.set(radiant_pdf_surface(surface, placement, placement));
    if (get_type_id(paths.get()) == LMD_TYPE_ERROR) return paths.get();
    radiant_rooted_obj_set(result, "image", paths.get());
    // capture is in page coordinates; cancel the enclosing PDF transform for this image.
    if (transform) radiant_rooted_obj_set(result, "matrix", pdf_matrix(inverse));
    return result.get();
}
