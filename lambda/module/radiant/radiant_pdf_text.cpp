#include "radiant_pdf_text.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant_image_job.hpp"
#include "../../core/collection_storage.h"
#include "../../runtime/heap_api.h"
#include "../../../lib/font/font_export.h"
#include "../../../lib/utf.h"

extern "C" Item vmap_new();

namespace {
Item pdf_font_face(FontHandle* handle, PdfTextResources* resources) {
    if (!resources || !resources->fonts) return radiant_pdf_error("text needs the active export font resources");
    for (size_t i = 0; i < resources->faces.size(); i++)
        if (resources->faces[i].handle == handle)
            return item_attr(array_item_read(resources->fonts->get().array, resources->faces[i].resource), "font");
    FontExport exported = {};
    const char* error = nullptr;
    if (!font_export_face(handle, &exported, &error)) return radiant_pdf_error(error);
    struct Cleanup { FontExport* value; ~Cleanup() { font_export_free(value); } } cleanup = {&exported};
    int64_t index = -1;
    for (size_t i = 0; i < resources->faces.size(); i++) {
        if (resources->faces[i].fingerprint != exported.fingerprint) continue;
        Item candidate = item_attr(array_item_read(resources->fonts->get().array, resources->faces[i].resource), "font");
        Item bytes = item_attr(candidate, "bytes");
        if (exported.glyph_program || (bytes.get_len() == exported.length &&
                !memcmp(bytes.get_chars(), exported.bytes, exported.length))) {
            index = resources->faces[i].resource; break;
        }
    }
    RootFrame roots(4);
    Rooted<Item> result(roots, ItemNull), bytes(roots, ItemNull), box(roots, {.array = array_plain()}), cache(roots, ItemNull);
    if (index < 0) {
        index = resources->fonts->get().array->length;
        if (!exported.glyph_program) {
            bytes.set({.item = x2it(heap_binary_from_bytes((const char*)exported.bytes, exported.length))});
            if (get_type_id(bytes.get()) != LMD_TYPE_BINARY) return radiant_pdf_error("cannot retain font program bytes");
        }
        result.set(vmap_new());
        radiant_rooted_obj_set(result, "bytes", bytes.get());
        radiant_rooted_obj_set(result, "id", {.item = i2it(index)});
        radiant_rooted_obj_set(result, "name", {.item = s2it(heap_strcpy(exported.postscript_name, strlen(exported.postscript_name)))});
        radiant_rooted_obj_set(result, "cff", {.item = b2it(exported.cff)});
        radiant_rooted_obj_set(result, "glyph_program", {.item = b2it(exported.glyph_program)});
        radiant_rooted_obj_set(result, "bitmap_only", {.item = b2it(exported.bitmap_only)});
        radiant_rooted_obj_set(result, "flags", {.item = i2it(4 | (exported.monospace ? 1 : 0) | (exported.italic_angle != 0 ? 64 : 0))});
        const char* fields[] = {"ascent", "descent", "cap_height", "italic_angle"};
        float values[] = {exported.ascent, exported.descent, exported.cap_height, exported.italic_angle};
        for (size_t i = 0; i < 4; i++) radiant_rooted_obj_set(result, fields[i], push_d(values[i]));
        for (float value : exported.bbox) array_push(box.get().array, push_d(value));
        radiant_rooted_obj_set(result, "bbox", box.get());
        // growing glyph caches stay native-private; previously published font values remain immutable.
        cache.set(vmap_new()); radiant_rooted_obj_set(cache, "font", result.get());
        box.set({.array = array_plain()}); radiant_rooted_obj_set(cache, "programs", box.get());
        array_push(resources->fonts->get().array, cache.get());
    } else result.set(item_attr(array_item_read(resources->fonts->get().array, index), "font"));
    if (!resources->faces.push_back({font_handle_retain(handle), exported.fingerprint, index})) {
        font_handle_release(handle); return radiant_pdf_error("cannot retain font resource identity");
    }
    return result.get();
}

struct PdfTextCollector {
    PdfTextResources* resources;
    Rooted<Item>* glyphs;
    Rooted<Item>* error;
    const PaintGlyphRun* run;
};

Item pdf_glyph_program(FontHandle* face, uint32_t glyph, int64_t unicode, Item font_item, PdfTextResources* resources) {
    RootFrame roots(4);
    Rooted<Item> font(roots, font_item), programs(roots, item_attr(array_item_read(resources->fonts->get().array,
            item_attr(font.get(), "id").int_val), "programs")),
        result(roots, ItemNull), value(roots, ItemNull);
    for (int64_t i = 0; i < programs.get().array->length; i++) {
        Item program = array_item_read(programs.get().array, i);
        if (item_attr(program, "glyph").int_val == (int64_t)glyph) return program;
    }
    result.set(vmap_new()); radiant_rooted_obj_set(result, "glyph", {.item = i2it(glyph)});
    if (unicode >= 0 && !font_has_codepoint(face, (uint32_t)unicode)) {
        // the invisible missing character retains Unicode; its native box paints separately.
        value.set({.array = array_plain()}); radiant_rooted_obj_set(result, "commands", value.get());
        value.set({.array = array_plain()});
        for (size_t i = 0; i < 4; i++) array_push(value.get().array, {.item = i2it(0)});
        radiant_rooted_obj_set(result, "bbox", value.get());
        array_push(programs.get().array, result.get()); return result.get();
    }
    float size = font_handle_get_size_px(face);
    if (!(size > 0)) return radiant_pdf_error("native font has no logical glyph size");
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "pdf.native_font_glyph");
    RdtPath* path = rdt_path_new();
    struct Cleanup { Arena* arena; RdtPath* path;
        ~Cleanup() { if (path) rdt_path_free(path); if (arena) mem_arena_destroy(arena); }
    } cleanup = {arena, path};
    if (!arena || !path) return radiant_pdf_error("cannot allocate native font glyph program");
    bool bitmap_only = item_attr(font.get(), "bitmap_only").bool_val;
    bool color_glyph = unicode >= 0 && font_get_glyph(face, (uint32_t)unicode).is_color;
    bool outlined = !bitmap_only && !color_glyph && render_path_append_font_glyph_index(path, face, glyph, 0, 0, 1, arena);
    const GlyphBitmap* bitmap = nullptr;
    if (!outlined && unicode >= 0) bitmap = font_render_glyph(face, (uint32_t)unicode, GLYPH_RENDER_NORMAL);
    float scale = 1000.0f / size;
    if (!outlined && !bitmap_only && unicode >= 0) {
        bool colored = false;
        outlined = render_path_append_font_glyph(path, face, (uint32_t)unicode, 0, 0, 1, arena, &colored) && !colored;
    }
    if (outlined) {
        RdtMatrix normal = rdt_matrix_scale(scale, -scale);
        value.set(radiant_pdf_path_array(path, &normal));
        if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
        radiant_rooted_obj_set(result, "commands", value.get());
        float left = 0, top = 0, right = 0, bottom = 0;
        rdt_path_get_bounds(path, &left, &top, &right, &bottom);
        value.set({.array = array_plain()});
        const float bounds[] = {left * scale, -bottom * scale, right * scale, -top * scale};
        for (float bound : bounds) array_push(value.get().array, push_d(bound));
        radiant_rooted_obj_set(result, "bbox", value.get());
    } else if (bitmap && bitmap->buffer && bitmap->pixel_mode == GLYPH_PIXEL_BGRA) {
        size_t count = (size_t)bitmap->width * (size_t)bitmap->height;
        lam::Temp<uint32_t> pixels = lam::temp_array<uint32_t>(count, MEM_CAT_RENDER);
        if (!pixels) return radiant_pdf_error("cannot allocate native color glyph pixels");
        for (int row = 0; row < bitmap->height; row++) for (int col = 0; col < bitmap->width; col++) {
            const uint8_t* p = bitmap->buffer + (size_t)row * bitmap->pitch + (size_t)col * 4;
            pixels[(size_t)row * bitmap->width + col] = (uint32_t)p[2] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[0] << 16) | ((uint32_t)p[3] << 24);
        }
        ImageSurface image = {}; image.width = bitmap->width; image.height = bitmap->height;
        image.pitch = bitmap->width * 4; image.pixels = pixels.get(); image.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
        float pixel = render_font_pixel_to_css(face, bitmap) * scale;
        Rect placement = {bitmap->bearing_x * pixel, -bitmap->bearing_y * pixel,
            bitmap->width * pixel, bitmap->height * pixel};
        value.set(radiant_pdf_surface(&image, placement, placement));
        if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
        radiant_rooted_obj_set(result, "image", value.get());
        value.set({.array = array_plain()});
        const float bounds[] = {placement.x, -placement.y - placement.height,
            placement.x + placement.width, -placement.y};
        for (float bound : bounds) array_push(value.get().array, push_d(bound));
        radiant_rooted_obj_set(result, "bbox", value.get());
    } else return radiant_pdf_error("native font glyph cannot be represented as a PDF font program");
    array_push(programs.get().array, result.get());
    return result.get();
}

bool pdf_text_glyph(PdfTextCollector* collector, FontHandle* face, uint32_t glyph,
        int64_t unicode, float x, float y) {
    RootFrame roots(4);
    Rooted<Item> font(roots, pdf_font_face(face, collector->resources)), entry(roots, ItemNull), program(roots, ItemNull),
        missing_box(roots, ItemNull);
    if (get_type_id(font.get()) == LMD_TYPE_ERROR) { collector->error->set(font.get()); return false; }
    float width = font_export_glyph_width(face, glyph);
    if (unicode >= 0 && item_attr(font.get(), "glyph_program").bool_val && font_handle_get_size_px(face) > 0)
        width = font_get_glyph(face, (uint32_t)unicode).advance_x * 1000.0f / font_handle_get_size_px(face);
    if (glyph > UINT16_MAX || width < 0) {
        collector->error->set(radiant_pdf_error("selected glyph has no embeddable font metrics")); return false;
    }
    entry.set(vmap_new());
    if (item_attr(font.get(), "glyph_program").bool_val) {
        program.set(pdf_glyph_program(face, glyph, unicode, font.get(), collector->resources));
        if (get_type_id(program.get()) == LMD_TYPE_ERROR) { collector->error->set(program.get()); return false; }
        radiant_rooted_obj_set(entry, "program", program.get());
    }
    radiant_rooted_obj_set(entry, "font", font.get());
    radiant_rooted_obj_set(entry, "glyph", {.item = i2it(glyph)});
    radiant_rooted_obj_set(entry, "unicode", unicode >= 0 ? Item{.item = i2it(unicode)} : ItemNull);
    radiant_rooted_obj_set(entry, "x", push_d(x)); radiant_rooted_obj_set(entry, "y", push_d(y));
    radiant_rooted_obj_set(entry, "width", push_d(width));
    radiant_rooted_obj_set(entry, "shear", push_d(item_attr(font.get(), "glyph_program").bool_val
        ? 0 : font_export_italic_shear(face)));
    if (unicode >= 0 && !font_has_codepoint(face, (uint32_t)unicode)) {
        const FontBox* box = paint_glyph_run_font(collector->run);
        Rect missing = render_text_missing_glyph_rect(box, x,
            y - font_get_rendering_ascender(font_box_handle(box)), box->style->space_width);
        missing_box.set({.array = array_plain()});
        const float parts[] = {missing.x, missing.y, missing.width, missing.height};
        for (float part : parts) array_push(missing_box.get().array, push_d(part));
        radiant_rooted_obj_set(entry, "missing", missing_box.get());
        radiant_rooted_obj_set(entry, "missing_color", radiant_pdf_color(render_text_missing_glyph_color()));
    }
    array_push(collector->glyphs->get().array, entry.get());
    return true;
}

bool pdf_text_character(void* data, uint32_t codepoint, FontHandle* face,
        float x, float y, float advance) {
    return pdf_text_glyph((PdfTextCollector*)data, face, font_get_glyph_index(face, codepoint), codepoint, x, y);
}
}

Item radiant_pdf_text(const PaintGlyphRun* run, UiContext* ui, PdfTextResources* resources) {
    if (!run || !ui) return radiant_pdf_error("text helper needs a live glyph run");
    RootFrame roots(3);
    Rooted<Item> result(roots, vmap_new()), glyphs(roots, {.array = array_plain()}), error(roots, ItemNull);
    PdfTextCollector collector = {resources, &glyphs, &error, run};
    bool ok = true;
    if (run->glyph_ids && run->count > 0) {
        FontHandle* face = font_box_handle(paint_glyph_run_font(run));
        for (int i = 0; ok && i < run->count; i++)
            ok = pdf_text_glyph(&collector, face, run->glyph_ids.get()[i], -1,
                run->x + run->xs.get()[i], run->baseline_y + run->ys.get()[i]);
    } else ok = render_visit_text_run_glyphs(run, ui->font_ctx, pdf_text_character, &collector, true);
    if (!ok) return get_type_id(error.get()) == LMD_TYPE_ERROR ? error.get()
        : radiant_pdf_error("cannot prepare selected glyphs for PDF text");
    radiant_rooted_obj_set(result, "glyphs", glyphs.get());
    radiant_rooted_obj_set(result, "size", push_d(run->font_size));
    if (run->text) {
        size_t length = run->text_len >= 0 ? (size_t)run->text_len : strlen(run->text.get());
        radiant_rooted_obj_set(result, "text", {.item = s2it(heap_strcpy(run->text.get() + run->logical_text_offset, length))});
    }
    return result.get();
}
