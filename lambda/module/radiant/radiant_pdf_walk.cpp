#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant_image_job.hpp"
#include "../../runtime/binary_schema.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-types.hpp"
#include "../../core/mark_reader.hpp"
#include "../../core/collection_storage.h"
#include "../../io/mark_output_builder.hpp"
#include "../../../radiant/render.hpp"
#include "../../../radiant/view.hpp"
#include "../../../radiant/layout.hpp"
#include "../../../radiant/radiant.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/log.h"
#include "../../../lib/math_checked.hpp"
#include <limits.h>
#include <math.h>

extern "C" Item vmap_new();

namespace {

struct PdfWalkFrame {
    float x, y, page_width, page_height, baseline, font_size, opacity;
    Color color;
    RdtMatrix matrix;
    Item text;
    FontBox font;
    UiContext* ui;
    float word_spacing, cjk_spacing;
    CssEnum justify_mode;
    const RdtPath* clip_path;
    RdtMatrix clip_transform;
    bool clip_evenodd;
    int32_t sequence;
    ViewBlock* image_view;
};
#include "pdf_view_schema.inc"

const PdfWalkFrame* pdf_walk_frame(Item context) {
    return (const PdfWalkFrame*)binary_schema_native_record(context, frame_schema.binding);
}

struct PdfWalkContext : RenderContext {
    BinarySchemaOwner* owner;
    const BinarySchema* element;
    const BinarySchema* text_rect;
    const BinarySchema* frame_schema;
    PdfWalkFrame frame;
    MarkOutputBuilder* output;
    Rooted<Item>* callback;
    Rooted<Item>* error;
    Rooted<Item>* resources;
};

Item pdf_walk_error(const char* message) {
    log_error("PDF_LAMBDA_WALK: %s", message);
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}

bool pdf_walk_aborted(RenderContext* base) {
    return get_type_id(((PdfWalkContext*)base)->error->get()) == LMD_TYPE_ERROR;
}

void pdf_walk_fail(PdfWalkContext* ctx, const char* message) {
    if (!pdf_walk_aborted(ctx)) ctx->error->set(pdf_walk_error(message));
}

void pdf_walk_dispatch(PdfWalkContext* ctx, const char* event,
        const BinarySchema* schema = nullptr, const void* record = nullptr,
        bool callback_local = false) {
    if (pdf_walk_aborted(ctx)) return;
    if (ctx->frame.sequence == INT32_MAX) { pdf_walk_fail(ctx, "PDF callback count exceeds supported range"); return; }
    ctx->frame.sequence++;
    BinarySchemaOwner* local = binary_schema_owner_create();
    if (!local) { pdf_walk_fail(ctx, "cannot allocate callback lifetime"); return; }
    RootFrame roots(4);
    Rooted<Item> tag(roots, {.item = s2it(heap_create_name(event, strlen(event)))});
    Rooted<Item> view(roots, record ? binary_schema_native_view(schema, record,
        callback_local ? local : ctx->owner) : ItemNull);
    Rooted<Item> frame(roots, binary_schema_native_view(ctx->frame_schema, &ctx->frame, local));
    Rooted<Item> result(roots, ItemNull);
    if (get_type_id(view.get()) == LMD_TYPE_ERROR) ctx->error->set(view.get());
    else if (get_type_id(frame.get()) == LMD_TYPE_ERROR) ctx->error->set(frame.get());
    else {
        LAMBDA_SCALAR_HOME(callback_home);
        result.set(radiant_lambda_fn_call3_into(ctx->callback->get().function,
            tag.get(), view.get(), frame.get(), &callback_home));
        if (get_type_id(result.get()) == LMD_TYPE_ERROR) ctx->error->set(result.get());
        else {
            Item content = result.get();
            if (item_semantic_type_id(get_type_id(content)) == LMD_TYPE_MAP) {
                Item entries = item_attr(content, "resources");
                if (get_type_id(entries) != LMD_TYPE_ARRAY) {
                    pdf_walk_fail(ctx, "PDF callback resource result requires an array");
                } else for (int64_t i = 0; i < entries.array->length; i++) {
                    int64_t length = ctx->resources->get().array->length;
                    array_push(ctx->resources->get().array, array_item_read(entries.array, i));
                    if (ctx->resources->get().array->length != length + 1) {
                        pdf_walk_fail(ctx, "cannot retain PDF resource contribution");
                        break;
                    }
                }
                content = item_attr(content, "content");
            }
            if (!pdf_walk_aborted(ctx)) ctx->output->ops->append(ctx->output, content);
            if (ctx->output->ops->error) {
                Item failure = ctx->output->ops->error(ctx->output);
                if (get_type_id(failure) == LMD_TYPE_ERROR) ctx->error->set(failure);
            }
        }
    }
    // TextRect projections and traversal context may point into callback scratch.
    binary_schema_owner_invalidate(local);
    binary_schema_owner_release(local);
}

void pdf_walk_boundary(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    const BoundaryProp* bound = view->boundary();
    if (bound && ((bound->background && (bound->background->image ||
            bound->background->gradient_type != GRADIENT_NONE || bound->background->layer_count)) ||
            (bound->border && (radiant_corner_has_radius(&bound->border->radius) ||
                bound->border->border_image_type != GRADIENT_NONE || bound->border->border_image_url)) ||
            bound->outline || bound->box_shadow)) {
        pdf_walk_fail(ctx, "this PDF callback profile needs a helper for complex boundaries");
        return;
    }
    if (bound && bound->border) {
        const BorderProp* border = bound->border;
        for (size_t edge = 0; edge < 4; edge++) {
            if (border->width.values[edge] > 0.0f && border->styles[edge] != CSS_VALUE_SOLID) {
                pdf_walk_fail(ctx, "this PDF callback profile needs a helper for nonsolid borders");
                return;
            }
        }
    }
    pdf_walk_dispatch(ctx, "boundary", ctx->element, view);
}

void pdf_walk_text(RenderContext* base, ViewText* view, float x, float y,
                   FontBox* font, Color color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->font = *font;
    ctx->color = color;
    for (DomNode* ancestor = view->parent; ancestor; ancestor = ancestor->parent) {
        DomElement* element = ancestor->as_element();
        const FontProp* style = element && element->font ? element->font.get() : nullptr;
        if (style && (style->text_shadow || style->text_emphasis_enabled ||
                style->text_deco_extra || (style->text_deco && style->text_deco != CSS_VALUE_NONE))) {
            pdf_walk_fail(ctx, "this PDF callback profile needs a helper for decorated text");
            return;
        }
    }
    RenderTextPaintIterator fragments(ctx, view);
    while (!pdf_walk_aborted(ctx)) {
        TextRect* rect = fragments.next();
        if (!rect) break;
        lam::Temp<char> segment(render_text_create_export_segment(view->text_data(), rect,
            render_text_inherited_transform(view), true, get_white_space_value(view)));
        if (!segment) { pdf_walk_fail(ctx, "cannot prepare text fragment"); break; }
        RootFrame roots(1);
        Rooted<Item> text(roots, {.item = s2it(heap_strcpy(segment.get(), strlen(segment.get())))});
        ctx->frame.text = text.get();
        ctx->frame.x = x + rect->x; ctx->frame.y = y + rect->y;
        ctx->frame.font_size = font->style ? font->style->font_size : 16.0f;
        ctx->frame.baseline = render_text_baseline_y(&ctx->font, ctx->frame.y);
        ctx->frame.color = ctx->color;
        ctx->frame.font = ctx->font;
        ctx->frame.ui = ctx->ui_context.get();
        ctx->frame.justify_mode = text_justify_computed_value(view->parent);
        RenderTextJustification justification = render_text_justification(rect, ctx->frame.justify_mode);
        ctx->frame.word_spacing = justification.word;
        ctx->frame.cjk_spacing = justification.character;
        pdf_walk_dispatch(ctx, "text", ctx->text_rect, rect, true);
        ctx->frame.text = ItemNull;
    }
}

void pdf_walk_image(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    ctx->frame.image_view = view;
    ctx->frame.ui = ctx->ui_context.get();
    pdf_walk_dispatch(ctx, "image", ctx->element, view);
    ctx->frame.image_view = nullptr;
}

void pdf_walk_vector(RenderContext* base, ViewBlock* view, float x, float y,
                      FontBox*, Color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    pdf_walk_dispatch(ctx, "vector", ctx->element, view);
}

void pdf_walk_view_event(RenderContext* base, ViewBlock* view, float x, float y, const char* event) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    pdf_walk_dispatch(ctx, event, ctx->element, view);
}
void pdf_walk_geomap(RenderContext* base, ViewBlock* view, float x, float y) {
    pdf_walk_view_event(base, view, x, y, "geomap");
}
void pdf_walk_scene3d(RenderContext* base, ViewBlock* view, float x, float y) {
    pdf_walk_view_event(base, view, x, y, "scene3d");
}
void pdf_walk_columns(RenderContext* base, ViewBlock* view, float x, float y) {
    pdf_walk_view_event(base, view, x, y, "column_rules");
}

void pdf_walk_subscene(RenderContext* base, const PaintSvgSubscene*) {
    pdf_walk_dispatch((PdfWalkContext*)base, "subscene");
}

void pdf_walk_begin_transform(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    ctx->frame.matrix = rdt_matrix_scale(1.0f, 1.0f);
    render_geometry_transform_matrix(view->transform, x, y, view->width, view->height, &ctx->frame.matrix);
    pdf_walk_dispatch(ctx, "begin_transform", ctx->element, view);
}
void pdf_walk_end_transform(RenderContext* base) { pdf_walk_dispatch((PdfWalkContext*)base, "end_transform"); }

bool pdf_walk_begin_clip(RenderContext* base, ViewElement* view, float x, float y, RenderClipKind kind) {
    // The shared geometry helper decides whether this scope has an actual clip.
    PaintList probe;
    paint_list_init(&probe, nullptr);
    bool clipped = render_clip_push_vector(&probe, view, x, y, kind);
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    if (clipped && paint_list_count(&probe) == 1 && probe.data()[0].op == PAINT_PUSH_CLIP) {
        const PaintPushClip& clip = probe.data()[0].push_clip;
        ctx->frame.clip_path = clip.clip_path.get();
        ctx->frame.clip_transform = clip.has_transform ? clip.transform : rdt_matrix_identity();
        ctx->frame.clip_evenodd = clip.rule == RDT_FILL_EVEN_ODD;
        ctx->frame.x = x; ctx->frame.y = y;
        pdf_walk_dispatch(ctx, "begin_clip", ctx->element, view);
        ctx->frame.clip_path = nullptr;
    } else if (clipped) pdf_walk_fail(ctx, "cannot resolve the native clip geometry");
    paint_list_destroy(&probe);
    return clipped;
}
void pdf_walk_end_clip(RenderContext* base) { pdf_walk_dispatch((PdfWalkContext*)base, "end_clip"); }
void pdf_walk_begin_effect(RenderContext* base, const PaintEffectGroup* group) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.opacity = group->opacity;
    pdf_walk_dispatch(ctx, "begin_effect");
}
void pdf_walk_end_effect(RenderContext* base) { pdf_walk_dispatch((PdfWalkContext*)base, "end_effect"); }
void pdf_walk_visit(RenderContext* base, ViewElement* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    pdf_walk_dispatch(ctx, "element", ctx->element, view);
}
void pdf_walk_marker(RenderContext* base, ViewSpan* view, float x, float y, FontBox*, Color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    pdf_walk_dispatch(ctx, "marker", ctx->element, view);
}

const BinarySchema* pdf_walk_bind(BinarySchemaOwner* owner, Item schemas,
        const char* key, const BinarySchema* native, Rooted<Item>& error) {
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return nullptr;
    Item value = item_attr(schemas, key);
    const BinarySchema* declared = get_type_id(value) == LMD_TYPE_TYPE
        ? binary_schema_from_type(value.type) : nullptr;
    log_debug("PDF_WALK_SCHEMA_BIND: key=%s type=%u kind=%u declared=%p binding=%s native=%s",
        key, get_type_id(value), get_type_id(value) == LMD_TYPE_TYPE ? value.type->kind : 0,
        declared, declared && declared->binding ? declared->binding : "(none)", native->binding);
    Item failure = ItemNull;
    const BinarySchema* bound = binary_schema_bind_native(owner, declared, native, &failure);
    if (!bound) error.set(failure);
    return bound;
}

bool pdf_walk_path_command(void* data, RdtPathCommand command, const float* args, int count) {
    Rooted<Item>* commands = (Rooted<Item>*)data;
    if (command != RDT_PATH_MOVE && command != RDT_PATH_LINE &&
            command != RDT_PATH_CUBIC && command != RDT_PATH_CLOSE) return false;
    RootFrame roots(1);
    Rooted<Item> entry(roots, {.array = array_plain()});
    if (!entry.get().array) return false;
    const char* name = command == RDT_PATH_MOVE ? "M" : command == RDT_PATH_LINE ? "L" :
        command == RDT_PATH_CUBIC ? "C" : "Z";
    array_push(entry.get().array, {.item = s2it(heap_create_name(name, 1))});
    for (int i = 0; i < count; i++) array_push(entry.get().array, {.item = f32_to_item(args[i])});
    int64_t length = commands->get().array->length;
    array_push(commands->get().array, entry.get());
    return entry.get().array->length == count + 1 && commands->get().array->length == length + 1;
}

Item pdf_walk_path_array(const RdtPath* path, const RdtMatrix* transform) {
    RdtPath* normalized = rdt_path_new();
    struct Cleanup { RdtPath* path; ~Cleanup() { if (path) rdt_path_free(path); } } cleanup = {normalized};
    if (!path || !normalized || !render_path_append_transformed(normalized, path, transform))
        return pdf_walk_error("cannot normalize selected path geometry");
    RootFrame roots(1);
    Rooted<Item> commands(roots, {.array = array_plain()});
    if (!commands.get().array || !rdt_path_visit(normalized, pdf_walk_path_command, &commands))
        return pdf_walk_error("cannot allocate selected path commands");
    return commands.get();
}

} // namespace

RADIANT_C_API Item fn_radiant_pdf_glyph_path(Item context_item) {
    RootFrame roots(2);
    Rooted<Item> context(roots, context_item), commands(roots, ItemNull);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->ui || get_type_id(frame->text) != LMD_TYPE_STRING)
        return pdf_walk_error("glyph outlines require a live text callback context");
    if (frame->text.get_len() > INT_MAX) return pdf_walk_error("text fragment exceeds supported shaping length");
    PaintGlyphRun run = {};
    run.captured_font = frame->font;
    run.has_captured_font = true;
    run.font_size = frame->font_size;
    run.text = lam::up(frame->text.get_chars());
    run.text_len = frame->text.get_len();
    run.x = frame->x; run.baseline_y = frame->baseline;
    run.word_spacing = frame->word_spacing; run.cjk_spacing = frame->cjk_spacing;
    run.justify_mode = frame->justify_mode;
    RdtPath* path = render_path_create_paint_glyph_run(&run, frame->ui->font_ctx);
    struct Cleanup {
        RdtPath* path;
        ~Cleanup() { if (path) rdt_path_free(path); }
    } cleanup = {path};
    RdtMatrix identity = rdt_matrix_scale(1.0f, 1.0f);
    commands.set(pdf_walk_path_array(path, &identity));
    return commands.get();
}

RADIANT_C_API Item fn_radiant_pdf_clip_path(Item context_item) {
    RootFrame roots(1);
    Rooted<Item> context(roots, context_item);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->clip_path) return pdf_walk_error("clip helper needs the active clip callback context");
    return pdf_walk_path_array(frame->clip_path, &frame->clip_transform);
}

RADIANT_C_API Item fn_radiant_pdf_image(Item context_item) {
    RootFrame roots(4);
    Rooted<Item> context(roots, context_item), result(roots, ItemNull), rgb(roots, ItemNull), alpha(roots, ItemNull);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->image_view || !frame->ui)
        return pdf_walk_error("image helper needs the active image callback context");
    ViewBlock* view = frame->image_view;
    ImageSurfaceReadScope read_scope;
    ImageSurface* image = render_media_image_surface(view);
    if (!image) return ItemNull;
    BlockBlot origin = {};
    origin.x = frame->x - view->x; origin.y = frame->y - view->y;
    Rect content = render_geometry_block_content_rect(&origin, view, 1.0f);
    Rect placement = render_media_image_rect(view, image, content, 1.0f);
    if (placement.width <= 0.0f || placement.height <= 0.0f) return ItemNull;
    if (!isfinite(placement.width) || !isfinite(placement.height) ||
            (double)placement.width > INT_MAX || (double)placement.height > INT_MAX)
        return pdf_walk_error("image placement exceeds supported decoder dimensions");
    int target_width = (int)ceilf(placement.width); // INT_CAST_OK: validated integer decoder extent.
    int target_height = (int)ceilf(placement.height); // INT_CAST_OK: validated integer decoder extent.
    if (image->format == IMAGE_FORMAT_SVG) {
        if (!render_media_rasterize_svg_picture(image, target_width, target_height))
            return pdf_walk_error("cannot rasterize the selected SVG image");
    } else image_surface_ensure_decoded(image, target_width, target_height);
    const uint32_t* pixels = nullptr;
    int width = 0, height = 0, stride = 0;
    if (!render_image_resource_pixels(image, placement.width, placement.height, &pixels, &width, &height, &stride) || stride < width)
        return pdf_walk_error("cannot decode the selected image resource");
    size_t count = 0, bytes = 0;
    if (!math_checked_mul((size_t)width, (size_t)height, &count) ||
            !math_checked_mul(count, 3, &bytes) || bytes > INT64_MAX)
        return pdf_walk_error("decoded image exceeds supported byte length");
    lam::Temp<uint8_t> colors = lam::temp_array<uint8_t>(bytes, MEM_CAT_RENDER);
    lam::Temp<uint8_t> masks = lam::temp_array<uint8_t>(count, MEM_CAT_RENDER);
    if (!colors || !masks) return pdf_walk_error("cannot allocate image color and alpha bytes");
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
        return pdf_walk_error("cannot retain selected image bytes");
    result.set(vmap_new());
    if (get_type_id(result.get()) != LMD_TYPE_VMAP) return pdf_walk_error("cannot allocate selected image result");
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

RADIANT_C_API Item fn_radiant_pdf_walk(Item path_item, Item options_item,
                                      Item callback_item, Item schemas_item) {
    RootFrame roots(7);
    Rooted<Item> path(roots, path_item), options(roots, options_item), callback(roots, callback_item),
        schemas(roots, schemas_item), error(roots, ItemNull), result(roots, ItemNull),
        resources(roots, {.array = array_plain()});
    if (get_type_id(path.get()) != LMD_TYPE_STRING || item_semantic_type_id(get_type_id(options.get())) != LMD_TYPE_MAP ||
            get_type_id(callback.get()) != LMD_TYPE_FUNC || item_semantic_type_id(get_type_id(schemas.get())) != LMD_TYPE_MAP)
        return pdf_walk_error("pdf_walk requires a path, options map, function and schemas map");
    error.set(lambda_fn_colour_guard(callback.get().function, nullptr, 0, LAMBDA_COLOUR_GUARD_CALLEE));
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    double width = 800, height = 600;
    const char* keys[] = {"width", "height"};
    SymbolKeyList* option_keys = item_keys(options.get());
    struct KeyCleanup {
        SymbolKeyList* keys;
        ~KeyCleanup() { if (keys) symbol_key_list_free(keys); }
    } key_cleanup = {option_keys};
    // an empty lazy VMap has no key storage until its first insertion.
    if (!option_keys && fn_len(options.get()) != 0) return pdf_walk_error("cannot enumerate PDF export options");
    for (int64_t index = 0; index < symbol_key_list_len(option_keys); index++) {
        Symbol* key = symbol_key_list_at(option_keys, index);
        bool admitted = false;
        for (const char* known : keys) {
            if (key && key->len == strlen(known) && memcmp(key->chars, known, key->len) == 0) admitted = true;
        }
        if (!admitted) return pdf_walk_error("this PDF export profile accepts only width and height options");
    }
    double* values[] = {&width, &height};
    for (size_t i = 0; i < 2; i++) {
        Item value = item_attr(options.get(), keys[i]);
        if (get_type_id(value) == LMD_TYPE_ERROR) return value;
        if (value.item != ItemNull.item && (!item_try_to_double(value, values[i]) ||
                !isfinite(*values[i]) || *values[i] <= 0 || *values[i] > INT_MAX || floor(*values[i]) != *values[i]))
            return pdf_walk_error("PDF viewport dimensions must be positive finite integers");
    }
    BinarySchemaOwner* owner = binary_schema_owner_create();
    if (!owner) return pdf_walk_error("cannot allocate PDF export owner");
    RenderExportSession session = {};
    MarkOutputBuilder* output = pdf_builder_create();
    struct Cleanup {
        BinarySchemaOwner* owner;
        RenderExportSession* session;
        MarkOutputBuilder* output;
        ~Cleanup() {
            binary_schema_owner_invalidate(owner);
            render_export_session_end(session);
            if (output) output->ops->destroy(output);
            binary_schema_owner_release(owner);
        }
    } cleanup = {owner, &session, output};
    if (!output) return pdf_walk_error("cannot allocate PDF callback byte destination");
    PdfWalkContext ctx = {};
    ctx.owner = owner; ctx.output = output; ctx.callback = &callback; ctx.error = &error;
    ctx.resources = &resources;
    if (!resources.get().array) return pdf_walk_error("cannot allocate PDF resource plan");
    ctx.element = pdf_walk_bind(owner, schemas.get(), "element", &element_schema, error);
    ctx.text_rect = pdf_walk_bind(owner, schemas.get(), "text_rect", &text_rect_schema, error);
    ctx.frame_schema = pdf_walk_bind(owner, schemas.get(), "context", &frame_schema, error);
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    if (memchr(path.get().get_chars(), 0, path.get().get_len()))
        return pdf_walk_error("PDF source path contains an embedded NUL");
    lam::Temp<char> filename(mem_strdup(path.get().get_chars(), MEM_CAT_RENDER));
    if (!filename || !render_export_session_begin(&session, filename.get(),
            (int)width, (int)height, (int)width, (int)height, 1.0f, true, false, 1.0f)) // INT_CAST_OK: validated integer viewport dimensions.
        return pdf_walk_error("cannot load and lay out PDF export source");
    ctx.ui_context = lam::up(session.ui_context.get());
    ctx.font.style = lam::up(&session.ui_context->default_font);
    ctx.color = {.r = 0, .g = 0, .b = 0, .a = 255};
    ctx.frame.page_width = session.has_page_geometry ? session.page_width : (float)session.content_width;
    ctx.frame.page_height = session.has_page_geometry ? session.page_height : (float)session.content_height;
    ctx.frame.text = ItemNull;
    ctx.frame.color = render_document_output_background(session.document->view_tree->root);
    ctx.frame.matrix = rdt_matrix_scale(1.0f, 1.0f);
    pdf_walk_dispatch(&ctx, "begin_page");
    RenderBackend backend = {};
    backend.ctx = lam::up(&ctx);
    backend.render_bound = pdf_walk_boundary;
    backend.render_text = pdf_walk_text;
    backend.render_image = pdf_walk_image;
    backend.render_inline_svg = pdf_walk_vector;
    backend.render_geomap = pdf_walk_geomap;
    backend.render_scene3d = pdf_walk_scene3d;
    backend.render_svg_subscene = pdf_walk_subscene;
    backend.visit_element = pdf_walk_visit;
    backend.render_marker = pdf_walk_marker;
    backend.render_column_rules = pdf_walk_columns;
    backend.begin_transform = pdf_walk_begin_transform; backend.end_transform = pdf_walk_end_transform;
    backend.begin_clip = pdf_walk_begin_clip; backend.end_clip = pdf_walk_end_clip;
    backend.begin_effect_group = pdf_walk_begin_effect; backend.end_effect_group = pdf_walk_end_effect;
    backend.should_abort = pdf_walk_aborted;
    RenderWalkState walk = {};
    walk.x = session.page_content_x; walk.y = session.page_content_y;
    walk.font = ctx.font; walk.color = ctx.color; walk.ui_context = lam::up(session.ui_context.get());
    render_walk_view_one(&backend, &walk, session.document->view_tree->root);
    pdf_walk_dispatch(&ctx, "end_page");
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    if (!output->ops->finish(output)) return pdf_walk_error("PDF callback returned unsupported byte content");
    const char* bytes = nullptr;
    size_t size = 0;
    if (!output->ops->bytes(output, &bytes, &size) || size > INT64_MAX)
        return pdf_walk_error("PDF callback output exceeds supported byte length");
    result.set(vmap_new());
    if (get_type_id(result.get()) != LMD_TYPE_VMAP)
        return pdf_walk_error("cannot allocate PDF callback result");
    Binary* content = heap_binary_from_bytes(bytes, size);
    if (!content) return pdf_walk_error("cannot allocate PDF callback bytes");
    radiant_rooted_obj_set(result, "content", {.item = x2it(content)});
    radiant_rooted_obj_set(result, "width", push_d(ctx.frame.page_width));
    radiant_rooted_obj_set(result, "height", push_d(ctx.frame.page_height));
    radiant_rooted_obj_set(result, "resources", resources.get());
    return result.get();
}
