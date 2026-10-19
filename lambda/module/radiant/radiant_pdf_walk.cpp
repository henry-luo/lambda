#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant_image_job.hpp"
#include "radiant_pdf_graphic.hpp"
#include "radiant_pdf_text.hpp"
#include "radiant_pdf_source.hpp"
#include "radiant_pdf_semantics.hpp"
#include "../../../lib/tagged.hpp"
#include "../../runtime/binary_schema.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/context_capsule.h"
#include "../../runtime/lambda-error.h"
#include "../../runtime/lambda-number-types.hpp"
#include "../../core/mark_reader.hpp"
#include "../../core/collection_storage.h"
#include "../../io/mark_output_builder.hpp"
#include "../../jube/jube_node_zlib_codec.hpp"
#include "../../../radiant/render.hpp"
#include "../../../radiant/view.hpp"
#include "../../../radiant/layout.hpp"
#include "../../../radiant/layout_paged.hpp"
#include "../../../radiant/page_document.hpp"
#include "../../../radiant/view_tree_model.hpp"
#include "../../../radiant/view_tree_css.hpp"
#include "../../../radiant/radiant.hpp"
#include "../../../radiant/render_effect_raster_fallback.hpp"
#include "../../../radiant/render_glyph_run_raster_lower.hpp"
#include "../../../radiant/geomap.hpp"
#include "../../../radiant/scene3d.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/log.h"
#include "../../../lib/math_checked.hpp"
#include "../../../lib/lru_cache.h"
#include "../../../lib/hashmap.h"
#include <limits.h>
#include <math.h>

extern "C" Item vmap_new();

Item radiant_pdf_error(const char* message) {
    log_error("PDF_LAMBDA_WALK: %s", message);
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}


namespace {

struct PdfBoundaryView {
    float x, y, width, height;
    const BoundaryProp* bound;
    const FontProp* font;
};

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
    const PaintImageBox* image_box;
    const PaintGlyphRun* glyph_run;
    const LayoutViewNode* fragment;
    float width, height;
    uint32_t page_number, page_count;
    Item page_label;
    float point_scale;
    const PaintList* graphic;
    Bound graphic_bounds;
    RdtMatrix graphic_transform;
    DisplayList* backdrop;
    PdfTextResources* text_resources;
};
#include "pdf_view_schema.inc"

const PdfWalkFrame* pdf_walk_frame(Item context) {
    return (const PdfWalkFrame*)binary_schema_native_record(context, frame_schema.binding);
}

struct PdfWalkContext : RenderContext {
    PdfTextResources text_resources;
    BinarySchemaOwner* owner;
    const BinarySchema* element;
    const BinarySchema* text_rect;
    const BinarySchema* frame_schema;
    const BinarySchema* fragment_schema;
    const BinarySchema* page_schema;
    const BinarySchema* boundary_schema;
    PdfWalkFrame frame;
    MarkOutputBuilder* output;
    Rooted<Item>* callback;
    Rooted<Item>* error;
    Rooted<Item>* resources;
    Rooted<Item>* pages;
    Rooted<Item>* links;
    Rooted<Item>* destinations;
    const ViewPageBox* page_material;
    RenderEffectRasterFallback effect;
    OffscreenRenderArenas backdrop_arenas;
    DisplayList backdrop;
    bool backdrop_ready;
    int backdrop_clip_depth;
    RdtMatrix backdrop_matrix;
    lam::ArrayList<RdtMatrix> backdrop_transforms{MEM_CAT_RENDER, 0};
    lam::ArrayList<float> effect_opacities{MEM_CAT_RENDER, 0};
    struct PageImage { ImageSurface* image; void (*release)(ImageSurface*); };
    lam::ArrayList<PageImage> page_images{MEM_CAT_RENDER, 0};
};


bool pdf_walk_aborted(RenderContext* base) {
    return get_type_id(((PdfWalkContext*)base)->error->get()) == LMD_TYPE_ERROR;
}

void pdf_walk_fail(PdfWalkContext* ctx, const char* message) {
    if (!pdf_walk_aborted(ctx)) ctx->error->set(radiant_pdf_error(message));
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

// A bounded native resource is consumed synchronously; Lambda owns its returned bytes.
void pdf_walk_dispatch_graphic(PdfWalkContext* ctx, const char* event, PaintList* paint,
        Bound bounds, const BinarySchema* schema = nullptr, const void* source = nullptr, bool copy = false) {
    ctx->frame.graphic = paint; ctx->frame.graphic_bounds = bounds;
    ctx->frame.graphic_transform = ctx->backdrop_matrix;
    ctx->frame.ui = ctx->ui_context.get();
    pdf_walk_dispatch(ctx, event, schema, source, copy);
    ctx->frame.graphic = nullptr;
}

void pdf_walk_graphic(PdfWalkContext* ctx, PaintList* paint, Bound bounds) {
    if (pdf_walk_aborted(ctx)) return;
    bool needs_backdrop = false;
    for (int i = 0; i < paint_list_count(paint); i++) {
        const PaintCmd& command = paint->data()[i];
        if (command.op == PAINT_BEGIN_EFFECT_GROUP && render_effect_group_needs_page_backdrop(&command.effect_group))
            needs_backdrop = true;
    }
    ctx->frame.backdrop = needs_backdrop && ctx->backdrop_ready ? &ctx->backdrop : nullptr;
    int backdrop_count = ctx->backdrop.item_count();
    // a streamed prefix can have open clips; close only the sampled edition.
    if (ctx->frame.backdrop)
        for (int i = 0; i < ctx->backdrop_clip_depth; i++) dl_pop_clip(&ctx->backdrop);
    pdf_walk_dispatch_graphic(ctx, "graphic", paint, bounds);
    if (ctx->frame.backdrop) dl_truncate(&ctx->backdrop, backdrop_count);
    ctx->frame.backdrop = nullptr;
}

void pdf_walk_record_backdrop(PdfWalkContext* ctx, PaintList* paint) {
    if (!ctx->backdrop_ready || !paint_list_count(paint)) return;
    // Raster fragment lowering has a local transform stack; preserve streamed outer scopes here.
    if (paint_list_count(paint) == 1) {
        const PaintCmd& command = paint->data()[0];
        if (command.op == PAINT_PUSH_TRANSFORM) {
            if (!ctx->backdrop_transforms.push_back(ctx->backdrop_matrix)) { pdf_walk_fail(ctx, "cannot retain backdrop transform"); return; }
            ctx->backdrop_matrix = rdt_matrix_multiply(&ctx->backdrop_matrix, &command.push_transform.transform); return;
        }
        if (command.op == PAINT_POP_TRANSFORM) {
            if (ctx->backdrop_transforms.empty()) { pdf_walk_fail(ctx, "unbalanced backdrop transform"); return; }
            ctx->backdrop_matrix = ctx->backdrop_transforms.back();
            ctx->backdrop_transforms.remove(ctx->backdrop_transforms.size() - 1); return;
        }
    }
    // Deferred backdrop replay must retain snapshots beyond the originating fragment.
    for (int i = 0; i < paint_list_count(paint); i++) {
        PaintCmd& command = paint->data()[i];
        if (command.op == PAINT_PUSH_CLIP) ctx->backdrop_clip_depth++;
        if (command.op == PAINT_POP_CLIP && --ctx->backdrop_clip_depth < 0) {
            pdf_walk_fail(ctx, "unbalanced backdrop clip scope"); return;
        }
        if (command.op == PAINT_DRAW_IMAGE_RESOURCE && command.draw_image_resource.release_image) {
            PaintDrawImageResource& image = command.draw_image_resource;
            if (!ctx->page_images.push_back({image.image, image.release_image})) { pdf_walk_fail(ctx, "cannot retain backdrop image"); return; }
            image.release_image = nullptr;
        }
    }
    render_svg_inline_register_paint_ir_lowerers();
    paint_ir_register_glyph_run_raster_lowerer(render_glyph_run_raster_lower);
    RdtMatrix density = rdt_matrix_scale(ui_context_raster_scale(ctx->ui_context), ui_context_raster_scale(ctx->ui_context));
    RdtMatrix placement = rdt_matrix_multiply(&density, &ctx->backdrop_matrix);
    paint_ir_lower_raster_fragment(paint, &ctx->backdrop, &placement);
}
void pdf_walk_canvas_backdrop(PdfWalkContext* ctx, Color color) {
    float density = ui_context_raster_scale(ctx->ui_context);
    dl_fill_rect(&ctx->backdrop, 0, 0, ctx->frame.page_width * density,
        ctx->frame.page_height * density, color);
}

struct PdfPaintBatch {
    PdfWalkContext* ctx;
    PaintList local;
    PaintList* paint;
    Bound bounds;
    PdfPaintBatch(PdfWalkContext* c, Rect rect) : ctx(c),
        paint(c->effect.active ? &c->effect.paint_list : &local),
        bounds{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height} {}
    void finish(bool emit = true) {
        if (ctx->effect.active || pdf_walk_aborted(ctx)) return;
        if (emit && paint_list_count(paint)) pdf_walk_graphic(ctx, paint, bounds);
        if (!pdf_walk_aborted(ctx)) pdf_walk_record_backdrop(ctx, paint);
    }
    ~PdfPaintBatch() { paint_list_destroy(&local); }
};

bool pdf_walk_simple_boundary(const BoundaryProp* bound, float width, float height) {
    if (!bound) return true;
    // calculated radii remain expressions until paint resolves the final border box.
    Corner radius = bound->border ? bound->border->radius : Corner{};
    resolve_border_radius_percentages(&radius, width, height);
    if (!render_paint_background_is_simple(bound->background) ||
        (bound->border && (radiant_corner_has_radius(&radius) || bound->border->border_image_type != GRADIENT_NONE || bound->border->border_image_url)) ||
        bound->outline || bound->box_shadow) return false;
    if (bound->border) for (size_t edge = 0; edge < 4; edge++)
        if (bound->border->width.values[edge] > 0 && bound->border->styles[edge] != CSS_VALUE_SOLID) return false;
    return true;
}

void pdf_walk_boundary(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    // table captions and fragmented boundaries share the export paint rectangle.
    Rect rect = render_geometry_boundary_rect(view, {x, y, view->width, view->height});
    x = rect.x; y = rect.y;
    ctx->frame.x = x; ctx->frame.y = y;
    // table layer backgrounds require shared paint emission, even with a simple local boundary.
    bool direct = !ctx->effect.active && rect.width == view->width && rect.height == view->height &&
        !render_background_uses_table_layers(view) &&
        pdf_walk_simple_boundary(view->boundary(), rect.width, rect.height);
    if (direct) pdf_walk_dispatch(ctx, "boundary", ctx->element, view);
    PdfPaintBatch batch(ctx, rect);
    if (!render_paint_boundary_emit_full(batch.paint, ctx->ui_context, view, rect))
        pdf_walk_fail(ctx, "cannot record boundary geometry");
    batch.finish(!direct);
}
void pdf_walk_outline(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    const BoundaryProp* bound = view->boundary();
    if (!bound || !bound->outline) return;
    Rect rect = {x, y, view->width, view->height};
    PdfPaintBatch batch(ctx, rect);
    if (!render_outline_emit(rect, bound->outline, bound->border, render_paint_boundary_path, batch.paint))
        pdf_walk_fail(ctx, "cannot record deferred outline");
    batch.finish();
}

void pdf_walk_text(RenderContext* base, ViewText* view, float x, float y, FontBox* font, Color color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->font = *font; ctx->color = color;
    DomElement* parent = view->parent ? view->parent->as_element() : nullptr;
    RenderTextPaintIterator fragments(ctx, view);
    while (!pdf_walk_aborted(ctx)) {
        TextRect* rect = fragments.next(true);
        if (!rect) break;
        // the iterator selects the ellipsis/inline fragment's font and color.
        font = &ctx->font; color = ctx->color;
        const FontProp* style = font->style;
        bool decorated = style && (style->text_shadow || style->text_emphasis_enabled || style->text_deco_extra ||
            (style->text_deco && style->text_deco != CSS_VALUE_NONE));
        PaintGlyphRun run = {};
        lam::Temp<char> segment(render_text_rect_glyph_run(view, rect, font, color, x, y, &run,
            fragments.reverses_clusters()));
        if (!segment) { pdf_walk_fail(ctx, "cannot prepare text fragment"); break; }
        if (!segment.get()[0]) continue;
        radiant_pdf_link(*ctx->links, view, {x + rect->x, y + rect->y, rect->width, rect->height});
        PdfPaintBatch batch(ctx, {x + rect->x, y + rect->y, rect->width, rect->height});
        bool background = parent && render_paint_text_background(batch.paint, ctx->ui_context, parent, &run,
            {x, y, parent->width, parent->height});
        bool direct = !decorated && !background && !ctx->effect.active;
        if (direct) {
            paint_glyph_run(batch.paint, &run);
            RootFrame roots(1);
            Rooted<Item> text(roots, {.item = s2it(heap_strcpy(segment.get(), strlen(segment.get())))});
            ctx->frame.text = text.get(); ctx->frame.glyph_run = &run;
            ctx->frame.x = run.x; ctx->frame.y = y + rect->y;
            ctx->frame.font_size = run.font_size; ctx->frame.baseline = run.baseline_y;
            ctx->frame.color = color; ctx->frame.font = *font; ctx->frame.ui = ctx->ui_context;
            pdf_walk_dispatch_graphic(ctx, "text", batch.paint, batch.bounds, ctx->text_rect, rect, true);
            ctx->frame.text = ItemNull; ctx->frame.glyph_run = nullptr;
        }
        // Captured groups outlive this fragment; the PaintList owns their text.
        if (ctx->effect.active) run.owned_text = lam::own((const char*)segment.release());
        RenderTextDecorationSource decoration = {view->text_data(), rect, x, y};
        if (style && style->text_shadow && !render_paint_text_shadows(batch.paint, ctx->ui_context, &run, style->text_shadow, &decoration))
            pdf_walk_fail(ctx, "cannot record text shadows");
        if (!render_paint_text_decorations(batch.paint, font, view->text_data(), rect, x, y, color, true))
            pdf_walk_fail(ctx, "cannot record text decorations");
        if (!direct) paint_glyph_run(batch.paint, &run);
        if (!render_paint_text_emphasis(batch.paint, ctx->ui_context->font_ctx, &run) ||
            !render_paint_text_decorations(batch.paint, font, view->text_data(), rect, x, y, color, false))
            pdf_walk_fail(ctx, "cannot record text emphasis or decorations");
        batch.finish(!direct);
    }
}

void pdf_walk_image(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y; ctx->frame.image_view = view; ctx->frame.ui = ctx->ui_context;
    ImageSurface* image = render_media_image_surface(view);
    bool direct = !ctx->effect.active && (!image || image->format != IMAGE_FORMAT_SVG);
    if (direct) pdf_walk_dispatch(ctx, "image", ctx->element, view);
    ctx->frame.image_view = nullptr;
    radiant_pdf_link(*ctx->links, view, {x, y, view->width, view->height});
    BlockBlot origin = {}; origin.x = x - view->x; origin.y = y - view->y;
    Rect content = render_geometry_block_content_rect(&origin, view, 1.0f);
    PdfPaintBatch batch(ctx, content);
    render_media_paint_image_content(batch.paint, ctx->ui_context, view, &content);
    batch.finish(!direct);
}

void pdf_walk_vector(RenderContext* base, ViewBlock* view, float x, float y, FontBox*, Color color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    DomElement* element = lam::dom_require_element(lam::view_dom_node(view));
    if (!element || element->is_synthetic()) return;
    radiant_pdf_link(*ctx->links, view, {x, y, view->width, view->height});
    BlockBlot origin = {}; origin.x = x - view->x; origin.y = y - view->y;
    Rect content = render_geometry_block_content_rect(&origin, view, 1.0f);
    if (content.width <= 0 || content.height <= 0) return;
    PdfPaintBatch batch(ctx, content);
    SvgInitialPaint initial = {}; render_svg_initial_paint(view, color, &initial);
    PaintSvgSubscene subscene = {};
    RdtMatrix placement = rdt_matrix_translate(content.x, content.y);
    Bound clip = {content.x, content.y, content.x + content.width, content.y + content.height};
    render_svg_build_subscene(&subscene, dom_element_to_element(element), content.width, content.height,
        ctx->ui_context->document->document_pool, ui_context_raster_scale(ctx->ui_context),
        ctx->ui_context->font_ctx, &placement, &clip, &initial.current_color,
        initial.has_fill_color ? &initial.fill_color : nullptr, nullptr, 1.0f, initial.fill_none,
        initial.has_stroke_color ? &initial.stroke_color : nullptr, initial.stroke_none, initial.stroke_width, ctx->ui_context);
    subscene.id_scope = lam::up(render_svg_reference_scope(element));
    paint_svg_subscene(batch.paint, &subscene); batch.finish();
}

void pdf_walk_geomap(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    BlockBlot origin = {}; origin.x = x - view->x; origin.y = y - view->y;
    Rect content = render_geometry_block_content_rect(&origin, view, 1.0f);
    PdfPaintBatch batch(ctx, content); geomap_paint(batch.paint, view->as_element(), content); batch.finish();
}
void pdf_walk_scene3d(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    PdfPaintBatch batch(ctx, {x, y, view->width, view->height});
    if (!scene3d_paint_content(batch.paint, ctx->ui_context, view, x, y)) pdf_walk_fail(ctx, "cannot record 3D scene");
    batch.finish();
}
void pdf_walk_columns(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    RenderColumnRuleIterator iterator;
    if (!render_column_rules_begin(view, &iterator)) return;
    const MultiColumnProp* columns = view->multicol_prop();
    PdfPaintBatch batch(ctx, {x, y, view->width, view->height});
    float rx = 0, ry = 0, height = 0;
    while (render_column_rule_next(&iterator, &rx, &ry, &height)) {
        BorderProp border = {}; border.width.left = columns->rule_width;
        border.left_color = columns->rule_color; border.styles[3] = columns->rule_style;
        BoundaryProp bound = {}; bound.border = lam::own(&border);
        render_paint_boundary_emit_box(batch.paint, &bound,
            {x + rx - columns->rule_width / 2, y + ry, columns->rule_width, height});
    }
    batch.finish();
}
void pdf_walk_subscene(RenderContext* base, const PaintSvgSubscene* subscene) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    PdfPaintBatch batch(ctx, {0, 0, ctx->frame.page_width, ctx->frame.page_height});
    paint_svg_subscene(batch.paint, subscene); batch.finish();
}

void pdf_walk_begin_transform(RenderContext* base, ViewBlock* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y; ctx->frame.matrix = rdt_matrix_identity();
    render_geometry_transform_matrix(view->transform, x, y, view->width, view->height, &ctx->frame.matrix);
    PdfPaintBatch batch(ctx, {}); paint_push_transform(batch.paint, &ctx->frame.matrix);
    if (!ctx->effect.active) pdf_walk_dispatch(ctx, "begin_transform", ctx->element, view);
    batch.finish(false);
}
void pdf_walk_end_transform(RenderContext* base) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    PdfPaintBatch batch(ctx, {}); paint_pop_transform(batch.paint);
    if (!ctx->effect.active) pdf_walk_dispatch(ctx, "end_transform"); batch.finish(false);
}

bool pdf_walk_begin_clip(RenderContext* base, ViewElement* view, float x, float y, RenderClipKind kind) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    PdfPaintBatch batch(ctx, {});
    // open overflow axes use the page bounds projected through the shared walk transform.
    bool clipped = render_clip_push_vector(batch.paint, view, x, y, kind,
        {0.0f, 0.0f, ctx->frame.page_width, ctx->frame.page_height},
        ctx->has_overflow_clip_transform ? &ctx->overflow_clip_transform : nullptr);
    if (clipped && !ctx->effect.active) {
        const PaintPushClip& clip = batch.paint->data()[0].push_clip;
        ctx->frame.clip_path = clip.clip_path;
        ctx->frame.clip_transform = clip.has_transform ? clip.transform : rdt_matrix_identity();
        ctx->frame.clip_evenodd = clip.rule == RDT_FILL_EVEN_ODD;
        ctx->frame.x = x; ctx->frame.y = y;
        pdf_walk_dispatch(ctx, "begin_clip", ctx->element, view);
        ctx->frame.clip_path = nullptr; batch.finish(false);
    }
    return clipped;
}
void pdf_walk_end_clip(RenderContext* base) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    PdfPaintBatch batch(ctx, {}); paint_pop_clip(batch.paint);
    if (!ctx->effect.active) pdf_walk_dispatch(ctx, "end_clip"); batch.finish(false);
}
void pdf_walk_begin_effect(RenderContext* base, const PaintEffectGroup* group) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    if (ctx->effect.active) { paint_begin_effect_group(&ctx->effect.paint_list, group); ctx->effect.nested_depth++; }
    else render_effect_raster_begin(&ctx->effect, group, render_glyph_run_raster_lower);
    if (!ctx->effect_opacities.push_back(ctx->frame.opacity)) pdf_walk_fail(ctx, "cannot retain effect scope");
    ctx->frame.opacity = group->opacity;
    pdf_walk_dispatch(ctx, "begin_effect");
}
void pdf_walk_end_effect(RenderContext* base) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    if (!ctx->effect.active) { pdf_walk_fail(ctx, "unbalanced effect scope"); return; }
    paint_end_effect_group(&ctx->effect.paint_list);
    if (ctx->effect.nested_depth) {
        ctx->effect.nested_depth--;
    } else {
        ctx->effect.active = false;
        pdf_walk_graphic(ctx, &ctx->effect.paint_list, ctx->effect.group.bounds);
        if (!pdf_walk_aborted(ctx)) pdf_walk_record_backdrop(ctx, &ctx->effect.paint_list);
        paint_list_clear(&ctx->effect.paint_list);
    }
    // Scope observations remain paired even when child painting is captured natively.
    pdf_walk_dispatch(ctx, "end_effect");
    if (!ctx->effect_opacities.empty()) {
        ctx->frame.opacity = ctx->effect_opacities.back();
        ctx->effect_opacities.remove(ctx->effect_opacities.size() - 1);
    }
}
void pdf_walk_visit(RenderContext* base, ViewElement* view, float x, float y) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    ctx->frame.x = x; ctx->frame.y = y;
    radiant_pdf_destination(*ctx->destinations, view, x, y);
    if (view->is_block() && view->tag_id == MARKUP_NAME_A && !view_visibility_is_hidden(view))
        radiant_pdf_link(*ctx->links, view, {x, y, view->width, view->height});
    pdf_walk_dispatch(ctx, "element", ctx->element, view);
}
void pdf_walk_marker_resource(RasterRenderContext* raster, void* source) {
    render_marker_view(raster, (ViewSpan*)source);
}
void pdf_walk_marker(RenderContext* base, ViewSpan* view, float x, float y, FontBox* font, Color color) {
    PdfWalkContext* ctx = (PdfWalkContext*)base;
    RenderContext source = *base;
    source.block.x = x; source.block.y = y;
    source.block.clip = {0, 0, ctx->frame.page_width, ctx->frame.page_height};
    source.font = *font; source.color = color;
    PdfPaintBatch batch(ctx, {x, y, view->width, view->height});
    if (!render_paint_raster_resource(batch.paint, ctx->ui_context, &source, pdf_walk_marker_resource, view))
        pdf_walk_fail(ctx, "cannot capture marker resource");
    batch.finish();
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

} // namespace

Item radiant_pdf_path_array(const RdtPath* path, const RdtMatrix* transform) {
    RdtPath* normalized = rdt_path_new();
    struct Cleanup { RdtPath* path; ~Cleanup() { if (path) rdt_path_free(path); } } cleanup = {normalized};
    if (!path || !normalized || !render_path_append_transformed(normalized, path, transform))
        return radiant_pdf_error("cannot normalize selected path geometry");
    RootFrame roots(1);
    Rooted<Item> commands(roots, {.array = array_plain()});
    if (!commands.get().array || !rdt_path_visit(normalized, pdf_walk_path_command, &commands))
        return radiant_pdf_error("cannot allocate selected path commands");
    return commands.get();
}

namespace {

bool pdf_walk_page_finish(PdfWalkContext* ctx) {
    if (pdf_walk_aborted(ctx)) return false;
    const char* bytes = nullptr;
    size_t size = 0;
    if (!ctx->output->ops->finish(ctx->output) ||
            !ctx->output->ops->bytes(ctx->output, &bytes, &size) || size > INT64_MAX) {
        pdf_walk_fail(ctx, "cannot finish PDF page byte content"); return false;
    }
    RootFrame roots(1);
    Rooted<Item> page(roots, radiant_pdf_page_properties(ctx->page_material));
    Binary* content = heap_binary_from_bytes(bytes, size);
    if (!content || get_type_id(page.get()) != LMD_TYPE_VMAP) {
        pdf_walk_fail(ctx, "cannot retain PDF page content"); return false;
    }
    radiant_rooted_obj_set(page, "content", {.item = x2it(content)});
    radiant_rooted_obj_set(page, "width", push_d(ctx->frame.page_width));
    radiant_rooted_obj_set(page, "height", push_d(ctx->frame.page_height));
    radiant_rooted_obj_set(page, "scale", push_d(ctx->frame.point_scale));
    radiant_rooted_obj_set(page, "number", {.item = i2it(ctx->frame.page_number)});
    radiant_rooted_obj_set(page, "label", ctx->frame.page_label);
    radiant_rooted_obj_set(page, "resources", ctx->resources->get());
    radiant_rooted_obj_set(page, "links", ctx->links->get());
    radiant_rooted_obj_set(page, "destinations", ctx->destinations->get());
    int64_t before = ctx->pages->get().array->length;
    array_push(ctx->pages->get().array, page.get());
    if (ctx->pages->get().array->length != before + 1) {
        pdf_walk_fail(ctx, "cannot retain PDF page plan"); return false;
    }
    return true;
}

bool pdf_secondary_fragment(void* data, const LayoutViewNode* node, bool entering) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    ctx->frame.fragment = node;
    if (entering) {
        DomNode* source = dom_node_ref_validate(ctx->ui_context->document, node->source);
        if (node->first_fragment) radiant_pdf_destination(*ctx->destinations, source, node->rect.x, node->rect.y);
        if (node->glyph_run || (node->paint_box && !node->first_child))
            radiant_pdf_link(*ctx->links, source, {node->rect.x, node->rect.y, node->rect.width, node->rect.height});
    }
    ctx->frame.x = node->rect.x; ctx->frame.y = node->rect.y;
    if (!entering) {
        // retained box paint already includes its outline.
        if (node->paint_box && !node->paint_before && node->computed_boundary && node->computed_boundary->outline) {
            const float* outset = node->border_outset;
            Rect rect = {node->rect.x - outset[3], node->rect.y - outset[0],
                node->rect.width + outset[1] + outset[3], node->rect.height + outset[0] + outset[2]};
            PdfPaintBatch batch(ctx, rect);
            if (!render_outline_emit(rect, node->computed_boundary->outline, node->computed_boundary->border,
                    render_paint_boundary_path, batch.paint)) pdf_walk_fail(ctx, "cannot record page fragment outline");
            batch.finish();
        }
    }
    pdf_walk_dispatch(ctx, entering ? "begin_fragment" : "end_fragment", ctx->fragment_schema, node);
    if (!entering) ctx->frame.fragment = node->parent.get();
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_boundary(void* data, const BoundaryProp* bound, RdtLogicalRect rect) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    if (!pdf_walk_simple_boundary(bound, rect.width, rect.height)) {
        // A page fragment borrows committed styles; copy mutable border geometry locally.
        BoundaryProp local = bound ? *bound : BoundaryProp{};
        BorderProp border = bound && bound->border ? *bound->border : BorderProp{};
        if (bound && bound->border) local.border = lam::own(&border);
        DomElement element = {}; element.bound = lam::view_prop(&local);
        element.width = rect.width; element.height = rect.height;
        PdfPaintBatch batch(ctx, {rect.x, rect.y, rect.width, rect.height});
        if (!render_paint_boundary_emit_full(batch.paint, ctx->ui_context, (ViewBlock*)&element,
                {rect.x, rect.y, rect.width, rect.height})) pdf_walk_fail(ctx, "cannot record page boundary");
        batch.finish(); return !pdf_walk_aborted(ctx);
    }
    PdfBoundaryView view = {rect.x, rect.y, rect.width, rect.height, bound, nullptr};
    ctx->frame.x = rect.x; ctx->frame.y = rect.y;
    // page-body and column-rule boundaries may be native stack temporaries.
    pdf_walk_dispatch(ctx, "boundary", ctx->boundary_schema, &view, true);
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_fill(void* data, Color color, RdtLogicalRect rect) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    ctx->frame.color = color; ctx->frame.x = rect.x; ctx->frame.y = rect.y;
    ctx->frame.width = rect.width; ctx->frame.height = rect.height;
    pdf_walk_dispatch(ctx, "fill");
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_clip(void* data, const RdtLogicalRect* rect, const float* inset) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    if (!rect) { pdf_walk_dispatch(ctx, "end_clip"); return !pdf_walk_aborted(ctx); }
    RdtPath* path = rdt_path_new();
    if (!path) { pdf_walk_fail(ctx, "cannot allocate page fragment clip"); return false; }
    float top = inset ? inset[0] : 0.0f, right = inset ? inset[1] : 0.0f;
    float bottom = inset ? inset[2] : 0.0f, left = inset ? inset[3] : 0.0f;
    rdt_path_add_rect(path, rect->x + left, rect->y + top,
        fmaxf(0.0f, rect->width - left - right), fmaxf(0.0f, rect->height - top - bottom), 0.0f, 0.0f);
    ctx->frame.clip_path = path;
    ctx->frame.clip_transform = rdt_matrix_identity(); ctx->frame.clip_evenodd = false;
    pdf_walk_dispatch(ctx, "begin_clip");
    ctx->frame.clip_path = nullptr;
    rdt_path_free(path);
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_glyph(void* data, const LayoutViewNode* node, const PaintGlyphRun* run) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    ctx->frame.fragment = node; ctx->frame.glyph_run = run;
    ctx->frame.x = run->x; ctx->frame.baseline = run->baseline_y;
    ctx->frame.font_size = run->font_size; ctx->frame.color = run->color;
    ctx->frame.ui = ctx->ui_context.get();
    PdfPaintBatch batch(ctx, {node->rect.x, node->rect.y, node->rect.width, node->rect.height});
    paint_glyph_run(batch.paint, run);
    pdf_walk_dispatch_graphic(ctx, "text", batch.paint, batch.bounds, ctx->fragment_schema, node);
    ctx->frame.glyph_run = nullptr;
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_image(void* data, const LayoutViewNode* node, const PaintImageBox* image) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    ctx->frame.fragment = node; ctx->frame.image_box = image; ctx->frame.ui = ctx->ui_context.get();
    if (image->image && (image->image->format == IMAGE_FORMAT_SVG || image->opacity != 255)) {
        PdfPaintBatch batch(ctx, image->content_rect);
        if (!render_paint_image_box(batch.paint, image)) pdf_walk_fail(ctx, "cannot record selected page image");
        batch.finish();
    } else pdf_walk_dispatch(ctx, "image", ctx->fragment_schema, node);
    ctx->frame.image_box = nullptr;
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_paint(void* data, const PaintList* source) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    const LayoutViewNode* node = ctx->frame.fragment;
    Rect rect = node ? Rect{node->rect.x, node->rect.y, node->rect.width, node->rect.height} :
        Rect{0, 0, ctx->frame.page_width, ctx->frame.page_height};
    PdfPaintBatch batch(ctx, rect);
    // the committed edition owns retained payloads across repeated PDF visits.
    if (!paint_list_borrow_commands(batch.paint, source)) {
        pdf_walk_fail(ctx, "cannot borrow selected page paint"); return false;
    }
    batch.finish();
    return !pdf_walk_aborted(ctx);
}
bool pdf_secondary_page(ViewTree* tree, const ViewPageBox* selected, const ViewPagePlacement*, void* data) {
    PdfWalkContext* ctx = (PdfWalkContext*)data;
    const ViewPageBox* page = view_tree_page_material(tree, selected);
    if (!page || pdf_walk_aborted(ctx)) return false;
    // each page owns its byte destination and resource plan; sequence IDs remain document-wide.
    ctx->output->ops->destroy(ctx->output);
    ctx->output = pdf_builder_create();
    ctx->resources->set({.array = array_plain()});
    ctx->links->set({.array = array_plain()}); ctx->destinations->set({.array = array_plain()});
    ctx->page_material = page;
    if (!ctx->output || !ctx->resources->get().array) {
        pdf_walk_fail(ctx, "cannot allocate PDF page destination"); return false;
    }
    RootFrame roots(1);
    Rooted<Item> label(roots, page->label ? Item{.item = s2it(heap_strcpy(page->label, strlen(page->label)))} : ItemNull);
    ctx->frame.page_label = label.get(); ctx->frame.page_number = selected->page_number;
    if (ctx->backdrop_ready) dl_clear(&ctx->backdrop);
    ctx->backdrop_clip_depth = 0;
    for (size_t i = 0; i < ctx->page_images.size(); i++) ctx->page_images[i].release(ctx->page_images[i].image);
    ctx->page_images.clear();
    ctx->frame.page_width = page->node.rect.width; ctx->frame.page_height = page->node.rect.height;
    ctx->frame.fragment = nullptr; ctx->frame.color = {};
    pdf_walk_canvas_backdrop(ctx, page->style ? page->style->background : Color{0xffffffffu});
    RdtMatrix flip = rdt_matrix_identity(); flip.e22 = -1; flip.e23 = ctx->frame.page_height;
    RdtMatrix scale = rdt_matrix_scale(ctx->frame.point_scale, ctx->frame.point_scale);
    if (page->fixed && page->fixed->geometry.box_mask) {
        RdtMatrix source = radiant_fixed_page_source_transform(&page->fixed->geometry);
        scale = rdt_matrix_multiply(&scale, &source);
    }
    ctx->frame.matrix = rdt_matrix_multiply(&scale, &flip);
    pdf_walk_dispatch(ctx, "begin_page", ctx->page_schema, page);
    SecondaryViewVisitor visitor = {ctx, pdf_secondary_fragment, pdf_secondary_boundary,
        pdf_secondary_fill, pdf_secondary_clip, pdf_secondary_glyph, pdf_secondary_image,
        pdf_secondary_paint, true};
    if (!pdf_walk_aborted(ctx) && !layout_secondary_visit_page(tree, selected, &visitor))
        pdf_walk_fail(ctx, "selected page fragment traversal failed");
    ctx->frame.fragment = nullptr;
    pdf_walk_dispatch(ctx, "end_page", ctx->page_schema, page);
    return pdf_walk_page_finish(ctx);
}

} // namespace

RADIANT_C_API Item fn_radiant_pdf_object(Item dictionary) {
    RootFrame roots(1);
    Rooted<Item> value(roots, dictionary);
    if (item_semantic_type_id(get_type_id(value.get())) != LMD_TYPE_MAP)
        return radiant_pdf_error("PDF object helper requires a dictionary");
    return pdf_encode_object(value.get());
}

RADIANT_C_API Item fn_radiant_pdf_value(Item value) {
    return pdf_encode_object(value);
}

struct PdfDeflateEntry {
    uint8_t* source;
    size_t length;
    NodeZlibBytes encoded;
};

struct PdfDeflateCache { LruCache* entries; };

static void pdf_deflate_entry_free(const char*, void* value, size_t, void*) {
    PdfDeflateEntry* entry = (PdfDeflateEntry*)value;
    mem_free(entry->source);
    node_zlib_bytes_free(&entry->encoded);
    mem_free(entry);
}

static const size_t PDF_DEFLATE_CACHE_BYTES = 8 * 1024 * 1024;
static const uint32_t PDF_DEFLATE_CACHE_OWNER = 0x52504446; // RPDF

static void* pdf_deflate_cache_create(EvalContext*) {
    PdfDeflateCache* cache = (PdfDeflateCache*)mem_calloc(1, sizeof(PdfDeflateCache), MEM_CAT_RENDER);
    if (!cache) return nullptr;
    LruCacheConfig config = {};
    config.max_entries = 64; config.max_bytes = PDF_DEFLATE_CACHE_BYTES;
    config.on_evict = pdf_deflate_entry_free;
    cache->entries = lru_cache_new(&config);
    if (!cache->entries) { mem_free(cache); return nullptr; }
    return cache;
}

static void pdf_deflate_cache_destroy(void* value) {
    PdfDeflateCache* cache = (PdfDeflateCache*)value;
    lru_cache_free(cache->entries);
    mem_free(cache);
}

static PdfDeflateCache* pdf_deflate_cache() {
    if (!context) return nullptr;
    PdfDeflateCache* cache = (PdfDeflateCache*)context_capsule_extension(context, PDF_DEFLATE_CACHE_OWNER, 0);
    if (cache) return cache;
    static const ContextCapsuleOps operations = {
        "radiant.pdf_deflate", CONTEXT_CAPSULE_LIFETIME_CONTEXT, sizeof(PdfDeflateCache),
        pdf_deflate_cache_create, nullptr, pdf_deflate_cache_destroy
    };
    cache = (PdfDeflateCache*)pdf_deflate_cache_create(context);
    if (cache && !context_capsule_extension_install(context, PDF_DEFLATE_CACHE_OWNER, 0, cache, &operations)) {
        pdf_deflate_cache_destroy(cache); return nullptr;
    }
    return cache;
}

RADIANT_C_API Item fn_radiant_pdf_deflate(Item bytes) {
    RootFrame roots(2);
    Rooted<Item> source(roots, bytes), retained(roots, ItemNull);
    if (get_type_id(source.get()) != LMD_TYPE_BINARY || source.get().get_len() > INT_MAX)
        return radiant_pdf_error("PDF compression requires a supported binary extent");
    size_t length = (size_t)source.get().get_len();
    PdfDeflateCache* cache = length <= PDF_DEFLATE_CACHE_BYTES ? pdf_deflate_cache() : nullptr;
    char key[64];
    if (cache) {
        uint64_t hash = hashmap_xxhash3(source.get().get_chars(), length, 0, 0);
        snprintf(key, sizeof(key), "%llx:%zu", (unsigned long long)hash, length);
        PdfDeflateEntry* entry = (PdfDeflateEntry*)lru_cache_get(cache->entries, key);
        // hashes only locate candidates; owned source bytes prove identity across GC and font retirement.
        if (entry && entry->length == length && !memcmp(entry->source, source.get().get_chars(), length)) {
            Binary* result = heap_binary_from_bytes((const char*)entry->encoded.data, (size_t)entry->encoded.length);
            return result ? Item{.item = x2it(result)} : radiant_pdf_error("cannot retain compressed PDF stream");
        }
    }
    NodeZlibBytes encoded = {};
    struct Cleanup { NodeZlibBytes* bytes; ~Cleanup() { node_zlib_bytes_free(bytes); } } cleanup = {&encoded};
    if (!node_zlib_deflate_encode((const uint8_t*)source.get().get_chars(), (int)source.get().get_len(), &encoded))
        return radiant_pdf_error("cannot compress PDF stream");
    Binary* result = heap_binary_from_bytes((const char*)encoded.data, (size_t)encoded.length);
    if (result) retained.set(Item{.item = x2it(result)});
    if (result && cache && length && length <= PDF_DEFLATE_CACHE_BYTES - sizeof(PdfDeflateEntry) &&
            (size_t)encoded.length <= PDF_DEFLATE_CACHE_BYTES - sizeof(PdfDeflateEntry) - length) {
        PdfDeflateEntry* entry = (PdfDeflateEntry*)mem_calloc(1, sizeof(PdfDeflateEntry), MEM_CAT_RENDER);
        if (entry) {
            entry->source = (uint8_t*)mem_alloc(length, MEM_CAT_RENDER);
            if (entry->source) {
                memcpy(entry->source, source.get().get_chars(), length);
                entry->length = length; entry->encoded = encoded;
                encoded = {};
                size_t cost = sizeof(*entry) + length + (size_t)entry->encoded.length;
                if (!lru_cache_put(cache->entries, key, entry, cost)) pdf_deflate_entry_free(nullptr, entry, cost, nullptr);
            } else mem_free(entry);
        }
    }
    return result ? retained.get() : radiant_pdf_error("cannot retain compressed PDF stream");
}

RADIANT_C_API Item fn_radiant_pdf_measure(Item content) {
    RootFrame roots(1);
    Rooted<Item> source(roots, content);
    MarkOutputBuilder* builder = pdf_builder_create(true);
    if (!builder) return radiant_pdf_error("cannot allocate PDF measuring destination");
    struct Cleanup { MarkOutputBuilder* builder; ~Cleanup() { builder->ops->destroy(builder); } } cleanup = {builder};
    builder->ops->append(builder, source.get());
    Item failure = builder->ops->error(builder);
    if (get_type_id(failure) == LMD_TYPE_ERROR) return failure;
    if (!builder->ops->finish(builder) || builder->ops->size(builder) > INT64_MAX)
        return radiant_pdf_error("PDF measured length exceeds supported range");
    return box_int64_value((int64_t)builder->ops->size(builder));
}

RADIANT_C_API Item fn_radiant_pdf_graphic(Item context_item) {
    RootFrame roots(1); Rooted<Item> context(roots, context_item);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->graphic || !frame->ui) return radiant_pdf_error("graphic helper requires the active callback context");
    return radiant_pdf_graphic(frame->graphic, frame->ui, frame->graphic_bounds,
        frame->page_width, frame->page_height, frame->backdrop, &frame->graphic_transform, frame->text_resources);
}

RADIANT_C_API Item fn_radiant_pdf_text_run(Item context_item) {
    RootFrame roots(1); Rooted<Item> context(roots, context_item);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->glyph_run) return radiant_pdf_error("text helper requires an active text callback");
    return radiant_pdf_text(frame->glyph_run, frame->ui, frame->text_resources);
}

RADIANT_C_API Item fn_radiant_pdf_glyph_path(Item context_item) {
    RootFrame roots(2);
    Rooted<Item> context(roots, context_item), commands(roots, ItemNull);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->ui || (!frame->glyph_run && get_type_id(frame->text) != LMD_TYPE_STRING))
        return radiant_pdf_error("glyph outlines require a live text callback context");
    PaintGlyphRun run = {};
    if (!frame->glyph_run) {
        if (frame->text.get_len() > INT_MAX) return radiant_pdf_error("text fragment exceeds supported shaping length");
        run.captured_font = frame->font; run.has_captured_font = true;
        run.font_size = frame->font_size;
        run.text = lam::up(frame->text.get_chars()); run.text_len = frame->text.get_len();
        run.x = frame->x; run.baseline_y = frame->baseline;
        run.word_spacing = frame->word_spacing; run.cjk_spacing = frame->cjk_spacing;
        run.justify_mode = frame->justify_mode;
    }
    RdtPath* path = render_path_create_paint_glyph_run(frame->glyph_run ? frame->glyph_run : &run, frame->ui->font_ctx);
    // missing/bitmap-only glyphs use the bounded native text painter instead.
    if (!path) return ItemNull;
    struct Cleanup {
        RdtPath* path;
        ~Cleanup() { if (path) rdt_path_free(path); }
    } cleanup = {path};
    RdtMatrix identity = rdt_matrix_scale(1.0f, 1.0f);
    commands.set(radiant_pdf_path_array(path, &identity));
    return commands.get();
}

RADIANT_C_API Item fn_radiant_pdf_clip_path(Item context_item) {
    RootFrame roots(1);
    Rooted<Item> context(roots, context_item);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || !frame->clip_path) return radiant_pdf_error("clip helper needs the active clip callback context");
    return radiant_pdf_path_array(frame->clip_path, &frame->clip_transform);
}

RADIANT_C_API Item fn_radiant_pdf_image(Item context_item) {
    RootFrame roots(1);
    Rooted<Item> context(roots, context_item);
    const PdfWalkFrame* frame = pdf_walk_frame(context.get());
    if (!frame || (!frame->image_view && !frame->image_box) || !frame->ui)
        return radiant_pdf_error("image helper needs the active image callback context");
    ViewBlock* view = frame->image_view;
    ImageSurface* image = frame->image_box ? frame->image_box->image.get() : render_media_image_surface(view);
    if (!image) return ItemNull;
    BlockBlot origin = {};
    if (view) { origin.x = frame->x - view->x; origin.y = frame->y - view->y; }
    Rect content = frame->image_box ? frame->image_box->content_rect : render_geometry_block_content_rect(&origin, view, 1.0f);
    Rect placement = frame->image_box ? frame->image_box->image_rect : render_media_image_rect(view, image, content, 1.0f);
    if (placement.width <= 0.0f || placement.height <= 0.0f) return ItemNull;
    if (!isfinite(placement.width) || !isfinite(placement.height))
        return radiant_pdf_error("image placement requires finite dimensions");
    if (image->format == IMAGE_FORMAT_SVG) {
        if ((double)placement.width > INT_MAX || (double)placement.height > INT_MAX)
            return radiant_pdf_error("SVG placement exceeds supported decoder dimensions");
        int target_width = (int)ceilf(placement.width); // INT_CAST_OK: validated integer decoder extent.
        int target_height = (int)ceilf(placement.height); // INT_CAST_OK: validated integer decoder extent.
        if (!render_media_rasterize_svg_picture(image, target_width, target_height))
            return radiant_pdf_error("cannot rasterize the selected SVG image");
    } else {
        // PDF places intrinsic pixels with its image matrix; CSS size is not a decode allocation.
        image_surface_ensure_decoded(image, 0, 0);
        // unavailable lazy images keep the native painter's absent-image behavior.
        if (!image->pixels) return ItemNull;
    }
    ScaleMode sampling = view ? render_image_scale_mode(view, false) : SCALE_MODE_LINEAR;
    return radiant_pdf_surface(image, placement, content, sampling, ui_context_raster_scale(frame->ui));
}

static Item pdf_walk(Item path_item, Item options_item,
        Item callback_item, Item schemas_item, bool source_html,
        RenderExportSession* existing = nullptr, const ViewPageSelection* page_selection = nullptr) {
    RootFrame roots(12);
    Rooted<Item> fonts(roots, {.array = array_plain()});
    Rooted<Item> links(roots, {.array = array_plain()}), destinations(roots, {.array = array_plain()}), metadata(roots, ItemNull);
    Rooted<Item> path(roots, path_item), options(roots, options_item), callback(roots, callback_item),
        schemas(roots, schemas_item), error(roots, ItemNull), result(roots, ItemNull),
        resources(roots, {.array = array_plain()}), pages(roots, {.array = array_plain()});
    if ((!existing && get_type_id(path.get()) != LMD_TYPE_STRING) || item_semantic_type_id(get_type_id(options.get())) != LMD_TYPE_MAP ||
            get_type_id(callback.get()) != LMD_TYPE_FUNC || item_semantic_type_id(get_type_id(schemas.get())) != LMD_TYPE_MAP)
        return radiant_pdf_error("pdf_walk requires a path, options map, function and schemas map");
    error.set(lambda_fn_colour_guard(callback.get().function, nullptr, 0, LAMBDA_COLOUR_GUARD_CALLEE));
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    double width = 800, height = 600;
    const char* keys[] = {"width", "height", "paged", "pages"};
    SymbolKeyList* option_keys = item_keys(options.get());
    struct KeyCleanup {
        SymbolKeyList* keys;
        ~KeyCleanup() { if (keys) symbol_key_list_free(keys); }
    } key_cleanup = {option_keys};
    // an empty lazy VMap has no key storage until its first insertion.
    if (!option_keys && fn_len(options.get()) != 0) return radiant_pdf_error("cannot enumerate PDF export options");
    for (int64_t index = 0; index < symbol_key_list_len(option_keys); index++) {
        Symbol* key = symbol_key_list_at(option_keys, index);
        bool admitted = false;
        for (const char* known : keys) {
            if (key && key->len == strlen(known) && memcmp(key->chars, known, key->len) == 0) admitted = true;
        }
        if (!admitted) return radiant_pdf_error("unknown PDF export option");
    }
    double* values[] = {&width, &height};
    for (size_t i = 0; i < 2; i++) {
        Item value = item_attr(options.get(), keys[i]);
        if (get_type_id(value) == LMD_TYPE_ERROR) return value;
        if (value.item != ItemNull.item && (!item_try_to_double(value, values[i]) ||
                !isfinite(*values[i]) || *values[i] <= 0 || *values[i] > INT_MAX || floor(*values[i]) != *values[i]))
            return radiant_pdf_error("PDF viewport dimensions must be positive finite integers");
    }
    Item paged_option = item_attr(options.get(), "paged");
    if (get_type_id(paged_option) != LMD_TYPE_NULL && get_type_id(paged_option) != LMD_TYPE_BOOL)
        return radiant_pdf_error("PDF paged option must be boolean");
    bool paged = existing ? existing->paged_view != nullptr : get_type_id(paged_option) == LMD_TYPE_BOOL && it2b(paged_option);
    RootFrame selection_roots(1);
    Rooted<Item> selected_pages(selection_roots, item_attr(options.get(), "pages"));
    if (get_type_id(selected_pages.get()) != LMD_TYPE_NULL && (!paged ||
            get_type_id(selected_pages.get()) != LMD_TYPE_STRING ||
            memchr(selected_pages.get().get_chars(), 0, selected_pages.get().get_len())))
        return radiant_pdf_error("PDF page selection requires paged output and a page-range string");
    BinarySchemaOwner* owner = binary_schema_owner_create();
    if (!owner) return radiant_pdf_error("cannot allocate PDF export owner");
    RenderExportSession owned_session = {};
    RenderExportSession& session = existing ? *existing : owned_session;
    MarkOutputBuilder* output = pdf_builder_create();
    PdfWalkContext ctx = {};
    ctx.output = output;
    Pool* selection_pool = pool_create();
    struct Cleanup {
        BinarySchemaOwner* owner;
        RenderExportSession* session;
        PdfWalkContext* context;
        Pool* selection_pool;
        ~Cleanup() {
            binary_schema_owner_invalidate(owner);
            paint_list_destroy(&context->effect.paint_list);
            if (context->backdrop_ready) dl_destroy(&context->backdrop);
            for (size_t i = 0; i < context->page_images.size(); i++)
                context->page_images[i].release(context->page_images[i].image);
            context->backdrop_arenas.destroy();
            if (context->output) context->output->ops->destroy(context->output);
            if (selection_pool) pool_destroy(selection_pool);
            // selected handles must retire before their owning UI font context.
            context->text_resources.clear();
            render_export_session_end(session);
            binary_schema_owner_release(owner);
        }
    } cleanup = {owner, &owned_session, &ctx, selection_pool};
    if (!output || !selection_pool) return radiant_pdf_error("cannot allocate PDF callback byte destination");
    ctx.owner = owner; ctx.output = output; ctx.callback = &callback; ctx.error = &error;
    ctx.text_resources.fonts = &fonts; ctx.frame.text_resources = &ctx.text_resources;
    ctx.resources = &resources; ctx.pages = &pages; ctx.links = &links; ctx.destinations = &destinations;
    if (!resources.get().array || !pages.get().array) return radiant_pdf_error("cannot allocate PDF resource plan");
    ctx.element = pdf_walk_bind(owner, schemas.get(), "element", &element_schema, error);
    ctx.text_rect = pdf_walk_bind(owner, schemas.get(), "text_rect", &text_rect_schema, error);
    ctx.frame_schema = pdf_walk_bind(owner, schemas.get(), "context", &frame_schema, error);
    if (paged) {
        ctx.fragment_schema = pdf_walk_bind(owner, schemas.get(), "fragment", &fragment_schema, error);
        ctx.page_schema = pdf_walk_bind(owner, schemas.get(), "page", &page_schema, error);
        ctx.boundary_schema = pdf_walk_bind(owner, schemas.get(), "boundary", &boundary_view_schema, error);
    }
    if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
    if (!existing) {
    if (memchr(path.get().get_chars(), 0, path.get().get_len()))
        return radiant_pdf_error("PDF source path contains an embedded NUL");
    lam::Temp<char> filename(mem_strdup(path.get().get_chars(), MEM_CAT_RENDER));
    bool begun = filename && (source_html
        ? render_export_session_begin_source(&session, filename.get(), (int)width, (int)height, paged)
        : render_export_session_begin(&session, filename.get(),
            (int)width, (int)height, (int)width, (int)height, 1.0f, true, paged, 1.0f, false)); // INT_CAST_OK: validated integer viewport dimensions.
    if (!begun)
        return radiant_pdf_error("cannot load and lay out PDF export source");
    }
    if (!session.ui_context || !session.document || (!paged && !session.document->view_tree))
        return radiant_pdf_error("PDF session has no committed view");
    metadata.set(radiant_pdf_metadata(session.document));
    if (get_type_id(metadata.get()) == LMD_TYPE_ERROR) return metadata.get();
    ctx.ui_context = lam::up(session.ui_context.get());
    // legacy documents inherit a different initial face from HTML5 documents.
    ViewTree* source_tree = session.document->view_tree;
    FontProp* default_font = source_tree && source_tree->html_version != HTML5
        ? &session.ui_context->legacy_default_font : &session.ui_context->default_font;
    setup_font(session.ui_context, &ctx.font, default_font);
    ctx.color = {.r = 0, .g = 0, .b = 0, .a = 255};
    if (!ctx.backdrop_arenas.init("pdf.backdrop", "pdf.backdrop.list", "pdf.backdrop.scratch"))
        return radiant_pdf_error("cannot allocate page backdrop");
    dl_init(&ctx.backdrop, ctx.backdrop_arenas.list_arena); ctx.backdrop_ready = true;
    ctx.backdrop_matrix = rdt_matrix_identity();
    ctx.frame.point_scale = existing && !paged ? session.output_scale * (session.has_page_geometry ? 0.75f : 1.0f) : 0.75f;
    ctx.frame.opacity = 1.0f;
    ctx.frame.text = ItemNull; ctx.frame.page_label = ItemNull;
    ctx.frame.matrix = rdt_matrix_identity();
    log_info("PDF_LAMBDA_EDITION: borrowed=%d paged=%d pages=%zu", existing != nullptr, paged, session.paged_view ? session.paged_view->model->page_count : 0);
    if (paged) {
        if (!session.paged_view || session.paged_view->model->page_count > UINT32_MAX)
            return radiant_pdf_error("PDF page count exceeds supported range");
        ctx.frame.page_count = (uint32_t)session.paged_view->model->page_count;
        ViewPageSelection selection = {};
        const char* ranges = get_type_id(selected_pages.get()) == LMD_TYPE_STRING ? selected_pages.get().get_chars() : nullptr;
        if (view_page_selection_parse(selection_pool, ranges, &selection) != VIEW_MODEL_OK)
            return radiant_pdf_error("invalid PDF page selection");
        ViewModelStatus status = view_tree_pages_visit(session.paged_view, page_selection ? page_selection : &selection, pdf_secondary_page, &ctx);
        if (get_type_id(error.get()) == LMD_TYPE_ERROR) return error.get();
        if (status != VIEW_MODEL_OK || !pages.get().array->length)
            return radiant_pdf_error("cannot export the selected physical pages");
        result.set(vmap_new());
        radiant_rooted_obj_set(result, "pages", pages.get());
        radiant_rooted_obj_set(result, "info", item_attr(metadata.get(), "info"));
        radiant_rooted_obj_set(result, "outlines", item_attr(metadata.get(), "outlines"));
        return result.get();
    }
    ctx.frame.page_width = session.has_page_geometry ? session.page_width : (float)session.content_width;
    ctx.frame.page_height = session.has_page_geometry ? session.page_height : (float)session.content_height;
    ctx.frame.page_number = ctx.frame.page_count = 1;
    ctx.frame.color = render_document_output_background(session.document->view_tree->root);
    ctx.frame.matrix = rdt_matrix_scale(ctx.frame.point_scale, -ctx.frame.point_scale);
    ctx.frame.matrix.e23 = ctx.frame.page_height * ctx.frame.point_scale;
    pdf_walk_canvas_backdrop(&ctx, ctx.frame.color);
    pdf_walk_dispatch(&ctx, "begin_page");
    RenderBackend backend = {};
    backend.ctx = lam::up(&ctx);
    backend.render_bound = pdf_walk_boundary;
    backend.render_outline = pdf_walk_outline;
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
    if (!pdf_walk_page_finish(&ctx)) return error.get();
    result.set(array_item_read(pages.get().array, 0));
    radiant_rooted_obj_set(result, "info", item_attr(metadata.get(), "info"));
    radiant_rooted_obj_set(result, "outlines", item_attr(metadata.get(), "outlines"));
    return result.get();
}

RADIANT_C_API Item fn_radiant_pdf_walk(Item path, Item options, Item callback, Item schemas) {
    return pdf_walk(path, options, callback, schemas, false);
}

RADIANT_C_API Item fn_radiant_pdf_walk_source(Item source, Item options, Item callback, Item schemas) {
    return pdf_walk(source, options, callback, schemas, true);
}

namespace {
const BinarySchema pdf_source_schema = {"PdfSource", sizeof(RadiantPdfSource), nullptr, 0,
    BINARY_SCHEMA_NATIVE, "radiant.PdfSource"};
}

Item radiant_pdf_source_view(const RadiantPdfSource* source, BinarySchemaOwner* owner) {
    return binary_schema_native_view(&pdf_source_schema, source, owner);
}

RADIANT_C_API Item fn_radiant_pdf_walk_view(Item source, Item options, Item callback, Item schemas) {
    RootFrame roots(1); Rooted<Item> handle(roots, source);
    const RadiantPdfSource* native = (const RadiantPdfSource*)binary_schema_native_record(handle.get(), pdf_source_schema.binding);
    if (!native || !native->session) return radiant_pdf_error("PDF view export requires a live session capability");
    return pdf_walk(ItemNull, options, callback, schemas, false, native->session, native->selection);
}
