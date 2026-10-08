#include "view_tree_css.hpp"
#include "layout.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

struct ViewCssVariable {
    const char* name;
    const CssValue* value;
    uint8_t status; // resolving, valid, invalid
    ViewCssVariable* next;
};

struct ViewVariableQuery { ViewTree* tree; ViewCssStyle* style; };

struct ViewCssPageContext {
    const char* name;
    uint32_t page_number;
    uint8_t pseudos;
    CssPageAreaKind area;
    int margin_box;
};
static bool view_css_select_page(ViewTree* tree, const ViewCssPageContext* context,
    const char* name, CssDeclaration* result);

bool view_css_context_begin(ViewTree* tree) {
    if (!view_tree_model_source_valid(tree)) return false;
    if (tree->model->css) return true;
    ViewCssContext* css = (ViewCssContext*)pool_calloc(tree->prop_pool, sizeof(ViewCssContext));
    if (!css) return false;
    tree->model->css = lam::own(css);
    css->pool = lam::own(mem_pool_create((MemContext*)tree->model->document->services.mem_ctx,
                                       MEM_ROLE_VIEW, "view_tree.secondary.css"));
    if (!css->pool) { view_css_context_destroy(tree); return false; }
    css->engine = lam::up(css_engine_create(css->pool));
    css->matcher = lam::up(selector_matcher_create(css->pool));
    css->root_font_size = 16.0f;
    if (!css->engine || !css->matcher) { view_css_context_destroy(tree); return false; }
    const ViewEnvironment& environment = tree->model->environment;
    css_engine_set_viewport(css->engine, environment.viewport_width, environment.viewport_height);
    css->engine->context.device_pixel_ratio = environment.device_scale;
    css->engine->context.print_media = environment.print_media;
    DomDocument* doc = tree->model->document;
    // the GCPM Appendix B fallback stays at UA origin so author resets can override it.
    CssStylesheet* defaults = css_parse_stylesheet(css->engine,
        "::footnote-call { font-size: 65%; vertical-align: super }", nullptr);
    css->stylesheet_count = (size_t)doc->stylesheet_count + 1;
    css->stylesheets = lam::up((CssStylesheet**)pool_calloc(css->pool,
        css->stylesheet_count * sizeof(CssStylesheet*)));
    if (!defaults || !css->stylesheets) { view_css_context_destroy(tree); return false; }
    for (size_t i = 0; i < defaults->rule_count; i++) defaults->rules[i]->origin = CSS_ORIGIN_USER_AGENT;
    css->stylesheets.get()[0] = defaults;
    if (doc->stylesheet_count) memcpy(css->stylesheets.get() + 1, doc->stylesheets.get(),
        (size_t)doc->stylesheet_count * sizeof(CssStylesheet*));
    if (doc->services.cached_css_engine) {
        CssEngine* source = (CssEngine*)doc->services.cached_css_engine;
        css->engine->features = source->features;
        css->engine->context.color_scheme = source->context.color_scheme;
        css->engine->context.reduced_motion = source->context.reduced_motion;
        css->engine->context.high_contrast = source->context.high_contrast;
        css->engine->context.quirks_mode = source->context.quirks_mode;
    }
    return true;
}

void view_css_context_destroy(ViewTree* tree) {
    if (!tree || !tree->model || !tree->model->css) return;
    ViewCssContext* css = tree->model->css;
    for (ViewCssStyle* style = css->styles; style; style = style->next) {
        if (style->font.font_handle) font_handle_release(style->font.font_handle);
    }
    if (css->fonts) font_context_destroy(css->fonts);
    if (css->matcher) selector_matcher_destroy(css->matcher);
    if (css->engine) css_engine_destroy(css->engine);
    if (css->pool) mem_pool_destroy(css->pool);
    pool_free(tree->prop_pool, css);
    tree->model->css = nullptr;
}

static bool view_css_select(ViewTree* tree, ViewCssStyle* style, const char* name,
                            CssDeclaration* result) {
    if (style->page_context) return view_css_select_page(tree, style->page_context, name, result);
    ViewCssContext* css = tree->model->css;
    return css_select_element_declaration(css->engine, css->matcher, style->source,
        css->stylesheets, css->stylesheet_count,
        style->inline_declarations, style->inline_count, name, result, style->pseudo_element);
}

static const CssValue* view_css_variable(void* context, DomElement*, const char* name, DomElement** owner) {
    if (owner) *owner = nullptr;
    ViewVariableQuery* query = (ViewVariableQuery*)context;
    if (!name) return nullptr;
    ViewCssStyle* style = query->style;
    for (ViewCssVariable* variable = style->variables; variable; variable = variable->next) {
        if (strcmp(variable->name, name) == 0) return variable->status == 2 ? variable->value : nullptr;
    }
    ViewCssContext* css = query->tree->model->css;
    ViewCssVariable* variable = (ViewCssVariable*)pool_calloc(css->pool, sizeof(ViewCssVariable));
    if (!variable) return nullptr;
    variable->name = pool_dup_n(css->pool, name, strlen(name));
    if (!variable->name) return nullptr;
    variable->status = 1;
    variable->next = style->variables;
    style->variables = lam::up(variable);
    CssDeclaration declaration = {};
    if (view_css_select(query->tree, style, name, &declaration) && declaration.value &&
        !css_value_is_inherit(declaration.value) && !css_value_is_unset(declaration.value)) {
        if (!css_value_is_initial(declaration.value)) {
            variable->value = css_resolve_var_value(css->pool, declaration.value,
                                                    view_css_variable, query);
        }
    } else if (style->parent) {
        // Inherited variables retain their declaration owner's computed environment.
        ViewVariableQuery parent = {query->tree, style->parent};
        variable->value = view_css_variable(&parent, nullptr, name, nullptr);
    }
    variable->status = variable->value ? 2 : 3;
    return variable->value;
}

static const CssValue* view_css_project(ViewTree* tree, const CssDeclaration& declaration,
                                       const char* name, ViewCssStyle* style) {
    const CssValue* value = declaration.value;
    if (!value || css_value_is_inherit(value) || css_value_is_initial(value) || css_value_is_unset(value)) return value;
    const char* property = declaration.property_name;
    if (!property || strcmp(property, name) == 0) return value;
    if (strcmp(property, "font") == 0) return css_font_shorthand_longhand(value, name, tree->model->css->pool);
    if (strcmp(property, "list-style") == 0)
        return layout_list_style_longhand(value, css_property_code_from_name(name), tree->model->css->pool);
    static const char* sides[] = {"top", "right", "bottom", "left"};
    for (int i = 0; i < 4; i++) {
        if ((strcmp(property, "margin") == 0 && strncmp(name, "margin-", 7) == 0 && strcmp(name + 7, sides[i]) == 0) ||
            (strcmp(property, "padding") == 0 && strncmp(name, "padding-", 8) == 0 && strcmp(name + 8, sides[i]) == 0)) {
            return css_box_shorthand_side_value(value, i);
        }
    }
    if (strncmp(property, "border", 6) == 0 && css_property_is_shorthand(declaration.property_code)) {
        const char* component = strrchr(name, '-');
        if (!component) return nullptr;
        component++;
        if (strcmp(component, "width") != 0 && strcmp(component, "style") != 0 && strcmp(component, "color") != 0) return value;
        if (strcmp(property, "border-width") == 0 || strcmp(property, "border-style") == 0 || strcmp(property, "border-color") == 0) {
            for (int i = 0; i < 4; i++) if (strncmp(name + 7, sides[i], strlen(sides[i])) == 0)
                return css_box_shorthand_side_value(value, i);
        }
        LayoutContext context = {}; context.doc = tree->model->document;
        context.pool = lam::up(tree->model->css->pool.get());
        context.selected_view_tree = lam::up(tree); context.selected_style = lam::up(style);
        MultiValue parts = {}; set_multi_value(&context, &parts, value);
        const CssValue* selected = strcmp(component, "width") == 0 ? parts.length :
            strcmp(component, "style") == 0 ? parts.style : strcmp(component, "color") == 0 ? parts.color : nullptr;
        return selected ? selected : css_value_create_keyword(tree->model->css->pool,
            strcmp(component, "width") == 0 ? "medium" : strcmp(component, "style") == 0 ? "none" : "currentcolor");
    }
    if (strcmp(property, "all") == 0) return value;
    // The break alias is a value alias; `always` is normalized by view_css_break.
    if (strncmp(property, "page-break-", 11) == 0) return value;
    if (strcmp(property, "background") == 0 && strcmp(name, "background-color") == 0) {
        int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
        for (int i = count; i > 0; i--) {
            const CssValue* item = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.values[i - 1] : value;
            if (item && (item->type == CSS_VALUE_TYPE_COLOR ||
                (item->type == CSS_VALUE_TYPE_KEYWORD && css_enum_info(item->data.keyword) &&
                 (css_enum_info(item->data.keyword)->group == CSS_VALUE_GROUP_COLOR ||
                  item->data.keyword == CSS_VALUE_TRANSPARENT)))) return item;
        }
    }
    return value;
}

const CssValue* view_css_resolve_value(ViewTree* tree, ViewCssStyle* style, const CssValue* value) {
    if (!style || !view_tree_model_source_valid(tree) || !tree->model->css) return nullptr;
    ViewVariableQuery variables = {tree, style};
    return css_resolve_var_value(tree->model->css->pool, value, view_css_variable, &variables);
}

const CssValue* view_css_property(ViewTree* tree, ViewCssStyle* style, const char* name,
                                 CssDeclaration* winning) {
    if (!style || !name || !view_tree_model_source_valid(tree) || !tree->model->css) return nullptr;
    CssDeclaration declaration = {};
    if (!view_css_select(tree, style, name, &declaration)) return nullptr;
    if (winning) *winning = declaration;
    return view_css_declaration_value(tree, style, &declaration, name);
}

const CssValue* view_css_declaration_value(ViewTree* tree, ViewCssStyle* style,
        const CssDeclaration* declaration, const char* property) {
    if (!declaration || !style) return nullptr;
    CssDeclaration computed = *declaration;
    computed.value = const_cast<CssValue*>(view_css_resolve_value(tree, style, declaration->value));
    return view_css_project(tree, computed, property, style);
}

static LayoutContext view_css_length_context(ViewTree* tree, const ViewCssStyle* style,
                                             float inline_size, float block_size) {
    LayoutContext context = {};
    context.doc = tree->model->document;
    context.pool = lam::up(tree->model->css->pool.get());
    context.width = inline_size;
    context.height = block_size;
    context.root_font_size = tree->model->css->root_font_size;
    context.selected_view_tree = lam::up(tree);
    context.selected_style = lam::up(const_cast<ViewCssStyle*>(style));
    if (style) context.font = style->font_box;
    return context;
}

float view_css_length(ViewTree* tree, const ViewCssStyle* style, const CssValue* value,
                     CssPropertyCode property, float inline_size, float block_size) {
    if (!value || css_value_is_auto(value) || css_value_is_none(value)) return NAN;
    LayoutContext context = view_css_length_context(tree, style, inline_size, block_size);
    // Viewport units are edition inputs, whereas percentages use the containing block.
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        double pixels = 0.0;
        const ViewEnvironment& environment = tree->model->environment;
        if (css_viewport_length_to_px(value->data.length.unit, value->data.length.value,
            environment.viewport_width, environment.viewport_height, false, &pixels)) return (float)pixels;
    }
    return resolve_length_value(&context, property, value);
}

ViewBreak view_css_break(const CssValue* value) {
    const char* name = css_value_identifier_name(value);
    if (!name) return VIEW_BREAK_AUTO;
    if (strcmp(name, "page") == 0 || strcmp(name, "always") == 0 || strcmp(name, "all") == 0) return VIEW_BREAK_PAGE;
    if (strcmp(name, "avoid") == 0 || strcmp(name, "avoid-page") == 0) return VIEW_BREAK_AVOID;
    if (strcmp(name, "left") == 0) return VIEW_BREAK_LEFT;
    if (strcmp(name, "right") == 0) return VIEW_BREAK_RIGHT;
    if (strcmp(name, "recto") == 0) return VIEW_BREAK_RECTO;
    if (strcmp(name, "verso") == 0) return VIEW_BREAK_VERSO;
    return VIEW_BREAK_AUTO;
}

static CssEnum view_css_keyword(ViewTree* tree, ViewCssStyle* style, const char* name,
                               CssEnum initial, CssEnum inherited, bool inherits = false) {
    const CssValue* value = view_css_property(tree, style, name);
    if (!value) return inherits ? inherited : initial;
    if (css_value_is_inherit(value) || (css_value_is_unset(value) && inherits)) return inherited;
    return value->type == CSS_VALUE_TYPE_KEYWORD && !css_value_is_initial(value) && !css_value_is_unset(value)
        ? value->data.keyword : initial;
}

static uint32_t view_css_line_limit(ViewTree* tree, ViewCssStyle* style, const char* name,
                                    uint32_t inherited) {
    const CssValue* value = view_css_property(tree, style, name);
    if (value && css_value_is_initial(value)) return 2;
    if (!value || css_value_is_inherit(value) || css_value_is_unset(value)) return inherited;
    double number = value->type == CSS_VALUE_TYPE_NUMBER ? value->data.number.value : 0.0;
    return isfinite(number) && number >= 1.0 && number <= UINT32_MAX
        ? static_cast<uint32_t>(number) : inherited;
}

static const FontMetrics* view_css_finish_font(ViewTree* tree, ViewCssStyle* style) {
    ViewCssContext* css = tree->model->css;
    if (!css->fonts) {
        FontContextConfig config = {};
        config.pixel_ratio = tree->model->environment.device_scale;
        css->fonts = lam::counted(font_context_create(&config));
    }
    if (css->fonts && style->font.font_size > 0.0f) {
        FontStyleDesc desc = {style->font.family, style->font.font_size,
            static_cast<FontWeight>(style->font.font_weight_numeric),
            style->font.font_style == CSS_VALUE_NORMAL ? FONT_SLANT_NORMAL : FONT_SLANT_ITALIC, nullptr};
        style->font.font_handle = lam::counted(font_resolve(css->fonts, &desc));
    }
    const FontMetrics* metrics = style->font.font_handle ? font_get_metrics(style->font.font_handle) : nullptr;
    if (metrics) {
        style->font.ascender = metrics->ascender;
        style->font.descender = metrics->descender;
        style->font.space_width = font_measure_char(style->font.font_handle, ' ');
    }
    style->font_box.style = lam::up(&style->font);
    style->font_box.current_font_size = style->font.font_size;
    return metrics;
}

static bool view_css_line_height(ViewTree* tree, ViewCssStyle* style, const CssValue* value) {
    ViewCssContext* css = tree->model->css;
    const FontMetrics* metrics = style->font.font_handle ? font_get_metrics(style->font.font_handle) : nullptr;
    style->line_height = metrics ? metrics->line_height : style->font.font_size * 1.2f;
    style->line_height_value = lam::up(value);
    if (value && !(value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL)) {
        float height = view_css_length(tree, style, value, CSS_PROPERTY_LINE_HEIGHT, 0.0f, 0.0f);
        if (isfinite(height) && height >= 0.0f) {
            style->line_height = height;
            if (value->type != CSS_VALUE_TYPE_NUMBER) {
                CssValue* computed = (CssValue*)pool_calloc(css->pool, sizeof(CssValue));
                if (!computed) return false;
                computed->type = CSS_VALUE_TYPE_LENGTH;
                computed->data.length = {height, CSS_UNIT_PX};
                style->line_height_value = lam::up(computed);
            }
        }
    }
    return true;
}

static bool view_css_font_style(ViewTree* tree, ViewCssStyle* style, ViewCssStyle* parent) {
    ViewCssContext* css = tree->model->css;
    const CssValue* value = nullptr;
    if (parent) {
        style->font.family = parent->font.family;
        style->font.font_size = parent->font.font_size;
        style->font.font_weight_numeric = parent->font.font_weight_numeric;
        style->font.font_style = parent->font.font_style;
        style->font.letter_spacing = parent->font.letter_spacing;
        style->font.word_spacing = parent->font.word_spacing;
        style->color = parent->color;
    } else {
        style->font.family = lam::up(const_cast<char*>("serif"));
        style->font.font_size = 16.0f;
        style->font.font_weight_numeric = 400;
        style->font.font_style = CSS_VALUE_NORMAL;
        style->color = {.r = 0, .g = 0, .b = 0, .a = 255};
    }
    style->font.used_zoom = 1.0f;
    style->font_box.style = lam::up(&style->font);
    style->font_box.current_font_size = style->font.font_size;
    value = view_css_property(tree, style, "font-size");
    if (value && !css_value_is_inherit(value) && !css_value_is_unset(value)) {
        float size = NAN;
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            size = value->data.keyword == CSS_VALUE_LARGER ? style->font.font_size * 1.2f :
                value->data.keyword == CSS_VALUE_SMALLER ? style->font.font_size / 1.2f : css_font_size_keyword_px(value->data.keyword);
        } else size = view_css_length(tree, style, value, CSS_PROPERTY_FONT_SIZE,
                                     tree->model->environment.viewport_width, tree->model->environment.viewport_height);
        if (isfinite(size) && size >= 0.0f) style->font.font_size = size;
    }
    if (!parent && !style->page_context) css->root_font_size = style->font.font_size;
    style->font_box.current_font_size = style->font.font_size;
    value = view_css_property(tree, style, "font-family");
    if (value && !css_value_is_inherit(value) && !css_value_is_unset(value)) {
        // Shorthand projections retain family groups; use the shared family-list resolver in this view's pool.
        LayoutContext context = view_css_length_context(tree, style, 0.0f, 0.0f);
        const char* family = css_select_font_family(&context, value);
        style->font.family = lam::up(const_cast<char*>(family ? family : "serif"));
    }
    style->font.font_style = view_css_keyword(tree, style, "font-style", CSS_VALUE_NORMAL,
        parent ? parent->font.font_style : CSS_VALUE_NORMAL, true);
    value = view_css_property(tree, style, "font-weight");
    if (value && value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value >= 1 && value->data.number.value <= 1000) {
        style->font.font_weight_numeric = static_cast<int16_t>(value->data.number.value);
    } else if (value && value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_BOLD) style->font.font_weight_numeric = 700;
        else if (value->data.keyword == CSS_VALUE_NORMAL || css_value_is_initial(value)) style->font.font_weight_numeric = 400;
    }
    view_css_finish_font(tree, style);
    const char* spacing[] = {"letter-spacing", "word-spacing"};
    float* spacing_values[] = {&style->font.letter_spacing, &style->font.word_spacing};
    for (size_t i = 0; i < 2; i++) {
        const CssValue* specified = view_css_property(tree, style, spacing[i]);
        if (specified && !css_value_is_inherit(specified) && !css_value_is_unset(specified)) {
            float used = view_css_length(tree, style, specified,
                i ? CSS_PROPERTY_WORD_SPACING : CSS_PROPERTY_LETTER_SPACING, 0.0f, 0.0f);
            *spacing_values[i] = isfinite(used) ? used : 0.0f;
        }
    }
    value = view_css_property(tree, style, "line-height");
    if (!value || css_value_is_inherit(value) || css_value_is_unset(value)) value = parent ? parent->line_height_value.get() : nullptr;
    if (!view_css_line_height(tree, style, value)) return false;
    LayoutContext color_context = view_css_length_context(tree, style, 0.0f, 0.0f);
    value = view_css_property(tree, style, "color");
    if (value && !css_value_is_inherit(value) && !css_value_is_unset(value)) style->color = resolve_color_value(&color_context, value);
    return true;
}

static const char* view_css_counter_names[] = {"counter-reset", "counter-increment", "counter-set"};
static void view_css_counter_style(ViewTree* tree, ViewCssStyle* style, ViewCssStyle* parent) {
    lam::Up<const CssValue>* values[] = {&style->counter_reset, &style->counter_increment, &style->counter_set};
    const CssValue* inherited[] = {parent ? parent->counter_reset.get() : nullptr,
        parent ? parent->counter_increment.get() : nullptr, parent ? parent->counter_set.get() : nullptr};
    for (size_t i = 0; i < 3; i++) {
        const CssValue* value = view_css_property(tree, style, view_css_counter_names[i]);
        *values[i] = css_value_is_inherit(value) ? lam::up(inherited[i]) :
            css_value_is_initial(value) || css_value_is_unset(value) ? nullptr : lam::up(value);
    }
}

static ViewCssStyle* view_css_build_style(ViewTree* tree, DomElement* element,
        ViewCssStyle* parent, uint8_t pseudo_element) {
    ViewCssContext* css = tree->model->css;
    ViewCssStyle* style = (ViewCssStyle*)pool_calloc(css->pool, sizeof(ViewCssStyle));
    if (!style) return nullptr;
    style->source = lam::up(element);
    style->pseudo_element = pseudo_element;
    style->parent = lam::up(parent);
    style->next = css->styles;
    css->styles = lam::up(style);
    const char* inline_text = pseudo_element ? nullptr : dom_element_get_inline_style(element);
    CssRule* authored = pseudo_element ? nullptr : dom_element_inline_declaration_block(element);
    if (authored) {
        size_t count = authored->data.style_rule.declaration_count;
        CssDeclaration** declarations = (CssDeclaration**)pool_calloc(css->pool, count * sizeof(CssDeclaration*));
        if (declarations) {
            for (size_t i = 0; i < count; i++) {
                CssDeclaration* copy = css_declaration_snapshot(authored->data.style_rule.declarations[i], css->pool);
                if (copy) declarations[style->inline_count++] = copy;
            }
            style->inline_declarations = lam::up(declarations);
        }
    } else if (inline_text && *inline_text) {
        style->inline_declarations = lam::up(css_parse_declaration_list_text(inline_text,
            strlen(inline_text), css->pool, &style->inline_count));
    }
    style->display = pseudo_element ? DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW} : css_default_display_for_element(element, element);
    const CssValue* value = view_css_property(tree, style, "display");
    if (value) {
        if (css_value_is_inherit(value) && parent) style->display = parent->display;
        else if (css_value_is_initial(value) || css_value_is_unset(value)) style->display = {CSS_VALUE_INLINE, CSS_VALUE_FLOW};
        else css_resolve_display_css_value(element, value, &style->display);
    }
    // the default adapter retains the legacy outer token; independent flows use the principal block plus marker flag.
    if (style->display.outer == CSS_VALUE_LIST_ITEM) {
        style->display.outer = CSS_VALUE_BLOCK; style->display.list_item = true;
    }
    if (!view_css_font_style(tree, style, parent)) return nullptr;
    LayoutContext color_context = view_css_length_context(tree, style, 0.0f, 0.0f);
    value = view_css_property(tree, style, "background-color");
    if (value && !css_value_is_initial(value) && !css_value_is_unset(value)) style->background = resolve_color_value(&color_context, value);
    const char* lengths[] = {"width", "height", "min-width", "max-width", "min-height", "max-height"};
    lam::Up<const CssValue>* destinations[] = {&style->width, &style->height, &style->min_width,
        &style->max_width, &style->min_height, &style->max_height};
    for (size_t i = 0; i < 6; i++) *destinations[i] = lam::up(view_css_property(tree, style, lengths[i]));
    const char* margins[] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
    const char* padding[] = {"padding-top", "padding-right", "padding-bottom", "padding-left"};
    const char* borders[] = {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width"};
    for (size_t i = 0; i < 4; i++) {
        style->margin[i] = lam::up(view_css_property(tree, style, margins[i]));
        style->padding[i] = lam::up(view_css_property(tree, style, padding[i]));
        style->border_width[i] = lam::up(view_css_property(tree, style, borders[i]));
        style->border_color[i] = style->color;
    }
    size_t list_level = 0;
    if (!pseudo_element && layout_is_html_list_container_tag(element->tag_id)) {
        if (!style->padding[3]) style->padding[3] = lam::up(css_value_create_length(css->pool, 40.0, CSS_UNIT_PX));
        bool nested = false;
        for (DomElement* ancestor = element->parent_element(); ancestor; ancestor = ancestor->parent_element())
            if (layout_is_html_list_container_tag(ancestor->tag_id)) { nested = true; list_level++; }
        for (size_t edge = 0; edge < 4; edge += 2)
            if (!style->margin[edge]) style->margin[edge] = lam::up(css_value_create_length(css->pool, nested ? 0.0 : 1.0, CSS_UNIT_EM));
    }
    style->text_align = view_css_keyword(tree, style, "text-align", CSS_VALUE_START,
        parent ? parent->text_align : CSS_VALUE_START, true);
    style->white_space = view_css_keyword(tree, style, "white-space", CSS_VALUE_NORMAL,
        parent ? parent->white_space : CSS_VALUE_NORMAL, true);
    style->list_style_type = parent ? parent->list_style_type : CSS_VALUE_DISC;
    style->list_style_string = parent ? parent->list_style_string : nullptr;
    if (!pseudo_element && element->tag_id == MARKUP_NAME_OL) {
        style->list_style_type = CSS_VALUE_DECIMAL; style->list_style_string = nullptr;
    } else if (!pseudo_element && layout_is_html_list_container_tag(element->tag_id)) {
        style->list_style_type = list_level == 0 ? CSS_VALUE_DISC : list_level == 1 ? CSS_VALUE_CIRCLE : CSS_VALUE_SQUARE;
        style->list_style_string = nullptr;
    }
    value = view_css_property(tree, style, "list-style-type");
    if (value && (css_value_is_inherit(value) || css_value_is_unset(value))) {
        style->list_style_type = parent ? parent->list_style_type : CSS_VALUE_DISC;
        style->list_style_string = parent ? parent->list_style_string : nullptr;
    } else if (value) {
        style->list_style_string = nullptr;
        if (value->type == CSS_VALUE_TYPE_STRING) style->list_style_string = lam::up(value->data.string);
        else style->list_style_type = css_value_is_initial(value) ? CSS_VALUE_DISC :
            value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : (CssEnum)0;
    }
    value = view_css_property(tree, style, "list-style-position");
    style->list_marker_inside = !value || css_value_is_inherit(value) || css_value_is_unset(value)
        ? parent && parent->list_marker_inside
        : css_value_identifier_name(value) && strcmp(css_value_identifier_name(value), "inside") == 0;
    value = view_css_property(tree, style, "list-style-image");
    style->list_style_image = !value || css_value_is_inherit(value) || css_value_is_unset(value)
        ? (parent ? parent->list_style_image : nullptr) : lam::up(value);
    style->float_spec = lam::up(view_css_property(tree, style, "float"));
    style->float_value = view_css_keyword(tree, style, "float", CSS_VALUE_NONE, CSS_VALUE_NONE);
    style->clear_value = view_css_keyword(tree, style, "clear", CSS_VALUE_NONE, CSS_VALUE_NONE);
    style->position = view_css_keyword(tree, style, "position", CSS_VALUE_STATIC, CSS_VALUE_STATIC);
    style->box_sizing = view_css_keyword(tree, style, "box-sizing", CSS_VALUE_CONTENT_BOX, CSS_VALUE_CONTENT_BOX);
    style->running_position = lam::up(view_css_property(tree, style, "position"));
    style->break_before = view_css_break(view_css_property(tree, style, "break-before"));
    style->break_after = view_css_break(view_css_property(tree, style, "break-after"));
    style->break_inside = view_css_break(view_css_property(tree, style, "break-inside"));
    style->orphans = view_css_line_limit(tree, style, "orphans", parent ? parent->orphans : 2);
    style->widows = view_css_line_limit(tree, style, "widows", parent ? parent->widows : 2);
    value = view_css_property(tree, style, "box-decoration-break");
    const char* keyword = css_value_identifier_name(value);
    style->decoration_clone = keyword && strcmp(keyword, "clone") == 0;
    value = view_css_property(tree, style, "page");
    if (css_value_is_inherit(value) || css_value_is_unset(value)) style->page_name = parent ? parent->page_name : nullptr;
    else { const char* name = css_value_identifier_name(value); if (name && strcmp(name, "auto") != 0) style->page_name = lam::up(name); }
    value = view_css_property(tree, style, "content");
    style->content = css_value_is_inherit(value) ? (parent ? parent->content : nullptr) :
        css_value_is_initial(value) || css_value_is_unset(value) ? nullptr : lam::up(value);
    value = view_css_property(tree, style, "vertical-align");
    style->vertical_align = css_value_is_inherit(value) ? (parent ? parent->vertical_align : nullptr) :
        css_value_is_initial(value) || css_value_is_unset(value) ? nullptr : lam::up(value);
    value = view_css_property(tree, style, "quotes");
    style->quotes = !value || css_value_is_inherit(value) || css_value_is_unset(value)
        ? (parent ? parent->quotes : nullptr) : lam::up(value);
    value = view_css_property(tree, style, "string-set");
    style->string_set = css_value_is_inherit(value) ? (parent ? parent->string_set : nullptr) :
        css_value_is_initial(value) || css_value_is_unset(value) ? nullptr : lam::up(value);
    view_css_counter_style(tree, style, parent);
    style->list_reversed = parent && parent->list_reversed;
    if (!pseudo_element && layout_is_html_list_container_tag(element->tag_id))
        style->list_reversed = element->tag_id == MARKUP_NAME_OL && element->has_attribute("reversed") && !style->counter_reset;
    else { int ignored = 0; if (layout_counter_named_value(style->counter_reset, "list-item", 0, &ignored)) style->list_reversed = false; }
    style->float_reference = lam::up(view_css_property(tree, style, "float-reference"));
    style->float_defer = lam::up(view_css_property(tree, style, "float-defer"));
    style->footnote_policy = lam::up(view_css_property(tree, style, "footnote-policy"));
    return style;
}

const ViewCssStyle* view_css_common_ancestor(const ViewCssStyle* left, const ViewCssStyle* right) {
    size_t left_depth = 0, right_depth = 0;
    for (const ViewCssStyle* style = left; style; style = style->parent) left_depth++;
    for (const ViewCssStyle* style = right; style; style = style->parent) right_depth++;
    while (left_depth > right_depth) { left = left->parent; left_depth--; }
    while (right_depth > left_depth) { right = right->parent; right_depth--; }
    while (left != right) { left = left->parent; right = right->parent; }
    return left;
}

ViewCssStyle* view_css_resolve(ViewTree* tree, DomElement* element) {
    if (!element || !view_css_context_begin(tree)) return nullptr;
    ViewNodeState* state = view_tree_node_state(tree, element, true);
    if (!state) return nullptr;
    if (state->computed_style) return state->computed_style;
    ViewCssStyle* parent = element->parent_element() ? view_css_resolve(tree, element->parent_element()) : nullptr;
    if (element->parent_element() && !parent) return nullptr;
    ViewCssStyle* style = view_css_build_style(tree, element, parent, PSEUDO_ELEMENT_NONE);
    state->computed_style = lam::up(style);
    return style;
}

ViewCssStyle* view_css_resolve_pseudo(ViewTree* tree, DomElement* element, uint8_t pseudo_element) {
    if (!pseudo_element) return view_css_resolve(tree, element);
    ViewCssStyle* parent = view_css_resolve(tree, element);
    if (!parent) return nullptr;
    for (ViewCssStyle* style = tree->model->css->styles; style; style = style->next)
        if (style->source == element && style->pseudo_element == pseudo_element) return style;
    // A pseudo occurrence borrows its semantic origin and owns its own computed style.
    return view_css_build_style(tree, element, parent, pseudo_element);
}

ViewCssStyle* view_css_generated_style(ViewTree* tree, ViewCssStyle* base,
        const CssValue* font_size, const CssValue* color, CssEnum align) {
    if (!base || !view_css_context_begin(tree)) return nullptr;
    ViewCssContext* css = tree->model->css;
    ViewCssStyle* style = (ViewCssStyle*)pool_calloc(css->pool, sizeof(ViewCssStyle));
    if (!style) return nullptr;
    style->source = base->source; style->parent = lam::up(base);
    style->next = css->styles; css->styles = lam::up(style);
    style->font = base->font; style->font.font_handle = nullptr;
    style->font_box = {lam::up(&style->font), style->font.font_size};
    style->color = base->color; style->text_align = align; style->white_space = base->white_space;
    style->orphans = style->widows = 1;
    font_size = view_css_resolve_value(tree, base, font_size);
    if (font_size && !css_value_is_inherit(font_size) && !css_value_is_unset(font_size)) {
        float size = view_css_length(tree, style, font_size, CSS_PROPERTY_FONT_SIZE,
            tree->model->environment.viewport_width, tree->model->environment.viewport_height);
        if (isfinite(size) && size > 0.0f) style->font.font_size = size;
    }
    const FontMetrics* metrics = view_css_finish_font(tree, style);
    if (!metrics) return nullptr;
    if (!view_css_line_height(tree, style, base->line_height_value)) return nullptr;
    color = view_css_resolve_value(tree, base, color);
    if (color && !css_value_is_inherit(color) && !css_value_is_unset(color)) {
        LayoutContext context = view_css_length_context(tree, style, 0.0f, 0.0f);
        style->color = resolve_color_value(&context, color);
    }
    return style;
}

struct PageDeclarationWinner {
    CssDeclaration declaration;
    CssPageSelector selector;
    uint64_t order;
    bool found;
};

struct PageStyleQuery {
    ViewTree* tree;
    const char* name;
    uint8_t pseudos;
    const char* property;
    int margin_box;
    CssPageAreaKind area_kind;
    uint64_t order;
    PageDeclarationWinner winner;
};

static bool page_property_matches(const CssDeclaration* declaration, const char* name) {
    if (!declaration || !declaration->valid || !declaration->property_name) return false;
    if (strcmp(declaration->property_name, name) == 0) return true;
    return css_property_shorthand_contains(declaration->property_code, css_property_code_from_name(name));
}

static void page_consider_declaration(PageStyleQuery* query, const CssDeclaration* declaration,
                                      const CssPageSelector* selector, CssOrigin origin) {
    uint64_t order = query->order++;
    if (!page_property_matches(declaration, query->property)) return;
    CssDeclaration candidate = *declaration;
    candidate.origin = origin;
    // Origin/importance/layer priority precedes the page-specific (f,g,h) tuple.
    CssDeclaration a = candidate;
    CssDeclaration b = query->winner.declaration;
    a.specificity = b.specificity = {};
    a.source_order = b.source_order = 0;
    int comparison = query->winner.found ? css_declaration_cascade_compare(&a, &b) : 1;
    if (!comparison) comparison = css_page_specificity_compare(selector, &query->winner.selector);
    if (comparison > 0 || (!comparison && order >= query->winner.order)) {
        query->winner = {candidate, *selector, order, true};
    }
}

static void page_query_rule(PageStyleQuery* query, CssRule* rule, size_t depth) {
    if (!rule || depth > 512) return;
    CssEngine* engine = query->tree->model->css->engine;
    if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS || rule->type == CSS_RULE_LAYER) {
        bool active = rule->type == CSS_RULE_LAYER || (rule->type == CSS_RULE_MEDIA
            ? css_evaluate_media_query(engine, rule->data.conditional_rule.condition)
            : css_evaluate_supports_condition(engine, rule->data.conditional_rule.condition));
        if (active) for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            page_query_rule(query, rule->data.conditional_rule.rules[i], depth + 1);
        }
        return;
    }
    if (rule->type != CSS_RULE_PAGE || !rule->page) return;
    const CssPageSelector* selector = nullptr;
    for (size_t i = 0; i < rule->page->selector_count; i++) {
        const CssPageSelector* candidate = &rule->page->selectors[i];
        if (css_page_selector_matches(candidate, query->name, query->pseudos) &&
            (!selector || css_page_specificity_compare(candidate, selector) > 0)) selector = candidate;
    }
    if (!selector) return;
    if (query->area_kind == CSS_PAGE_AREA_PAGE) {
        for (size_t i = 0; i < rule->page->declaration_count; i++) {
            page_consider_declaration(query, rule->page->declarations[i], selector, rule->origin);
        }
    } else for (size_t i = 0; i < rule->page->area_count; i++) {
        const CssPageAreaRule& area = rule->page->areas[i];
        if (area.kind != query->area_kind || (area.kind == CSS_PAGE_AREA_MARGIN && area.box != query->margin_box)) continue;
        for (size_t j = 0; j < area.declaration_count; j++) {
            page_consider_declaration(query, area.declarations[j], selector, rule->origin);
        }
    }
}

static void page_query_sheet(PageStyleQuery* query, CssStylesheet* sheet, size_t depth) {
    if (!sheet || sheet->disabled || depth > 512) return;
    CssEngine* engine = query->tree->model->css->engine;
    if (sheet->media && *sheet->media && !css_evaluate_media_query(engine, sheet->media)) return;
    for (size_t i = 0; i < sheet->imported_count; i++) page_query_sheet(query, sheet->imported_stylesheets[i], depth + 1);
    for (size_t i = 0; i < sheet->rule_count; i++) page_query_rule(query, sheet->rules[i], 0);
}

static const CssDeclaration* page_query_declaration(PageStyleQuery* query,
        const char* property, int margin_box = -1, CssPageAreaKind area = CSS_PAGE_AREA_PAGE) {
    query->property = property;
    query->margin_box = margin_box;
    query->area_kind = margin_box >= 0 ? CSS_PAGE_AREA_MARGIN : area;
    query->order = 0;
    query->winner = {};
    DomDocument* doc = query->tree->model->document;
    for (int i = 0; i < doc->stylesheet_count; i++) page_query_sheet(query, doc->stylesheets.get()[i], 0);
    if (!query->winner.found) return nullptr;
    CssDeclaration* result = (CssDeclaration*)pool_alloc(query->tree->model->css->pool, sizeof(CssDeclaration));
    if (result) *result = query->winner.declaration;
    return result;
}

static bool view_css_select_page(ViewTree* tree, const ViewCssPageContext* context,
        const char* name, CssDeclaration* result) {
    PageStyleQuery query = {}; query.tree = tree; query.name = context->name; query.pseudos = context->pseudos;
    const CssDeclaration* declaration = page_query_declaration(&query, name, context->margin_box, context->area);
    if (!declaration) return false;
    *result = *declaration; return true;
}

static ViewCssStyle* view_css_page_context_style(ViewTree* tree, ViewCssStyle* parent,
        const PageStyleQuery& query, uint32_t page_number, CssPageAreaKind area, int margin_box = -1) {
    ViewCssContext* css = tree->model->css;
    for (ViewCssStyle* style = css->styles; style; style = style->next) {
        const ViewCssPageContext* context = style->page_context;
        if (context && style->parent.get() == parent && context->page_number == page_number &&
            context->pseudos == query.pseudos && context->area == area && context->margin_box == margin_box &&
            (context->name == query.name || (context->name && query.name && strcmp(context->name, query.name) == 0))) return style;
    }
    ViewCssPageContext* context = (ViewCssPageContext*)pool_calloc(css->pool, sizeof(ViewCssPageContext));
    ViewCssStyle* style = (ViewCssStyle*)pool_calloc(css->pool, sizeof(ViewCssStyle));
    if (!context || !style) return nullptr;
    *context = {nullptr, page_number, query.pseudos, area, margin_box};
    if (query.name && !(context->name = pool_dup_n(css->pool, query.name, strlen(query.name)))) return nullptr;
    style->page_context = lam::up(context); style->parent = lam::up(parent);
    style->source = parent ? parent->source : nullptr;
    style->display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW}; style->orphans = style->widows = 1;
    style->next = css->styles; css->styles = lam::up(style);
    // Page contexts inherit from their CSS parent, while relocated notes retain their DOM cascade.
    if (!view_css_font_style(tree, style, parent)) return nullptr;
    style->text_align = view_css_keyword(tree, style, "text-align", CSS_VALUE_START,
        parent ? parent->text_align : CSS_VALUE_START, true);
    style->white_space = view_css_keyword(tree, style, "white-space", CSS_VALUE_NORMAL,
        parent ? parent->white_space : CSS_VALUE_NORMAL, true);
    const CssValue* quotes = view_css_property(tree, style, "quotes");
    style->quotes = !quotes || css_value_is_inherit(quotes) || css_value_is_unset(quotes)
        ? (parent ? parent->quotes : nullptr) : lam::up(quotes);
    view_css_counter_style(tree, style, parent);
    return style;
}

static bool page_size_resolve(ViewTree* tree, const CssValue* value, ViewPageStyle* style) {
    if (!value || css_value_is_auto(value)) return true;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    bool landscape = false, portrait = false, named = false;
    float lengths[2] = {};
    size_t length_count = 0;
    static const struct { const char* name; float width_mm, height_mm; } papers[] = {
        {"a5", 148.0f, 210.0f}, {"a4", 210.0f, 297.0f}, {"a3", 297.0f, 420.0f},
        {"b5", 176.0f, 250.0f}, {"b4", 250.0f, 353.0f},
        {"letter", 215.9f, 279.4f}, {"legal", 215.9f, 355.6f}, {"ledger", 279.4f, 431.8f},
    };
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.values[i] : value;
        const char* name = css_value_identifier_name(item);
        if (name) {
            if (str_icmp_cstr(name, "landscape") == 0 && !landscape && !portrait) landscape = true;
            else if (str_icmp_cstr(name, "portrait") == 0 && !landscape && !portrait) portrait = true;
            else {
                bool found = false;
                for (const auto& paper : papers) if (str_icmp_cstr(name, paper.name) == 0 && !named) {
                    style->width = paper.width_mm * 96.0f / 25.4f;
                    style->height = paper.height_mm * 96.0f / 25.4f;
                    named = found = true;
                    break;
                }
                if (!found) return false;
            }
        } else {
            if (!item || item->type != CSS_VALUE_TYPE_LENGTH || length_count >= 2) return false;
            double absolute = 0.0;
            CssUnit unit = item->data.length.unit;
            bool fixed = css_absolute_length_to_px(unit, item->data.length.value, &absolute);
            if (!fixed && (unit < CSS_UNIT_EM || unit > CSS_UNIT_RLH)) return false;
            // sheet dimensions must use the selected page font before resolving their edges.
            float length = fixed ? (float)absolute : view_css_length(tree, style->computed_style, item,
                CSS_PROPERTY_WIDTH, style->width, style->height);
            if (!isfinite(length) || length <= 0.0f) return false;
            lengths[length_count++] = length;
        }
    }
    if (length_count) {
        if (named || portrait || landscape) return false;
        style->width = lengths[0];
        style->height = length_count == 2 ? lengths[1] : lengths[0];
    } else if (landscape || portrait) {
        float shorter = fminf(style->width, style->height), longer = fmaxf(style->width, style->height);
        style->width = landscape ? longer : shorter;
        style->height = landscape ? shorter : longer;
    }
    return true;
}

ViewModelStatus view_css_page_style(ViewTree* tree, const char* name, uint32_t page_number,
                                  ViewPageSide side, bool blank, ViewPageStyle* result) {
    if (!result || !page_number || side > VIEW_PAGE_RIGHT || !view_css_context_begin(tree)) return VIEW_MODEL_INVALID_ARGUMENT;
    if (!view_tree_model_source_valid(tree)) return VIEW_MODEL_STALE_SOURCE;
    PageStyleQuery query = {};
    query.tree = tree;
    query.name = name;
    query.pseudos = side == VIEW_PAGE_LEFT ? CSS_PAGE_LEFT : CSS_PAGE_RIGHT;
    if (page_number == 1) query.pseudos |= CSS_PAGE_FIRST;
    if (blank) query.pseudos |= CSS_PAGE_BLANK;
    ViewPageStyle style = {};
    style.width = tree->model->environment.page_width;
    style.height = tree->model->environment.page_height;
    style.background = {.r = 255, .g = 255, .b = 255, .a = 255};
    const char* margins[] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
    const char* padding[] = {"padding-top", "padding-right", "padding-bottom", "padding-left"};
    const char* borders[] = {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width"};
    DomElement* root = tree->model->document->root;
    ViewCssStyle* parent = root ? view_css_resolve(tree, root) : nullptr;
    if (root && !parent) return VIEW_MODEL_OUT_OF_MEMORY;
    style.computed_style = lam::up(view_css_page_context_style(tree, parent, query, page_number, CSS_PAGE_AREA_PAGE));
    if (!style.computed_style) return VIEW_MODEL_OUT_OF_MEMORY;
    ViewCssStyle& default_style = *style.computed_style;
    const CssValue* value = view_css_property(tree, &default_style, "size");
    if (!page_size_resolve(tree, value, &style)) return VIEW_MODEL_INVALID_ARGUMENT;
    for (size_t i = 0; i < 4; i++) {
        // Page-context percentages resolve against the corresponding page dimension.
        CssPropertyCode axis = i % 2 ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
        value = view_css_property(tree, &default_style, margins[i]);
        if (value) style.margin[i] = view_css_length(tree, &default_style, value, axis, style.width, style.height);
        value = view_css_property(tree, &default_style, padding[i]);
        if (value) style.padding[i] = view_css_length(tree, &default_style, value, axis, style.width, style.height);
        value = view_css_property(tree, &default_style, borders[i]);
        if (value) style.border_width[i] = view_css_length(tree, &default_style, value, axis, style.width, style.height);
        if (!isfinite(style.margin[i]) || style.margin[i] < 0.0f ||
            !isfinite(style.padding[i]) || style.padding[i] < 0.0f ||
            !isfinite(style.border_width[i]) || style.border_width[i] < 0.0f) return VIEW_MODEL_INVALID_ARGUMENT;
    }
    float left = style.margin[3] + style.padding[3] + style.border_width[3];
    float top = style.margin[0] + style.padding[0] + style.border_width[0];
    float right = style.margin[1] + style.padding[1] + style.border_width[1];
    float bottom = style.margin[2] + style.padding[2] + style.border_width[2];
    style.content_rect = {left, top, style.width - left - right, style.height - top - bottom};
    if (!isfinite(style.content_rect.width) || style.content_rect.width <= 0.0f ||
        !isfinite(style.content_rect.height) || style.content_rect.height <= 0.0f) return VIEW_MODEL_INVALID_ARGUMENT;
    value = view_css_property(tree, &default_style, "background-color");
    if (value) {
        LayoutContext context = view_css_length_context(tree, &default_style, style.width, style.height);
        style.background = resolve_color_value(&context, value);
    }
    value = view_css_property(tree, &default_style, "bleed");
    if (value && !css_value_is_auto(value)) style.bleed = view_css_length(tree, &default_style, value,
                                                                       CSS_PROPERTY_WIDTH, style.width, style.height);
    if (!isfinite(style.bleed) || style.bleed < 0.0f) return VIEW_MODEL_INVALID_ARGUMENT;
    value = view_css_property(tree, &default_style, "marks");
    const char* marks = css_value_identifier_name(value);
    style.crop_marks = marks && strcmp(marks, "crop") == 0;
    for (int i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
        style.margin_content[i] = page_query_declaration(&query, "content", i);
        style.margin_font_size[i] = page_query_declaration(&query, "font-size", i);
        style.margin_color[i] = page_query_declaration(&query, "color", i);
        style.margin_align[i] = page_query_declaration(&query, "text-align", i);
        style.margin_overflow[i] = page_query_declaration(&query, "overflow", i);
        bool counter_context = false;
        if (!style.margin_content[i]) for (const char* name : view_css_counter_names)
            counter_context |= page_query_declaration(&query, name, i) != nullptr;
        // Margin counter state advances with page generation even before this box acquires content.
        if (style.margin_content[i] || counter_context) {
            style.margin_style[i] = lam::up(view_css_page_context_style(tree, style.computed_style, query,
                page_number, CSS_PAGE_AREA_MARGIN, i));
            if (!style.margin_style[i]) return VIEW_MODEL_OUT_OF_MEMORY;
        }
    }
    ViewPageAreaStyle* note = &style.footnote;
    note->computed_style = lam::up(view_css_page_context_style(tree, style.computed_style, query,
        page_number, CSS_PAGE_AREA_FOOTNOTE));
    if (!note->computed_style) return VIEW_MODEL_OUT_OF_MEMORY;
    note->width = page_query_declaration(&query, "width", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->min_width = page_query_declaration(&query, "min-width", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->max_width = page_query_declaration(&query, "max-width", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->height = page_query_declaration(&query, "height", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->min_height = page_query_declaration(&query, "min-height", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->max_height = page_query_declaration(&query, "max-height", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->background = page_query_declaration(&query, "background-color", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->color = page_query_declaration(&query, "color", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->box_sizing = page_query_declaration(&query, "box-sizing", -1, CSS_PAGE_AREA_FOOTNOTE);
    note->float_value = page_query_declaration(&query, "float", -1, CSS_PAGE_AREA_FOOTNOTE);
    const char* styles[] = {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style"};
    const char* colors[] = {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color"};
    for (size_t i = 0; i < 4; i++) {
        note->margin[i] = page_query_declaration(&query, margins[i], -1, CSS_PAGE_AREA_FOOTNOTE);
        note->padding[i] = page_query_declaration(&query, padding[i], -1, CSS_PAGE_AREA_FOOTNOTE);
        note->border_width[i] = page_query_declaration(&query, borders[i], -1, CSS_PAGE_AREA_FOOTNOTE);
        note->border_style[i] = page_query_declaration(&query, styles[i], -1, CSS_PAGE_AREA_FOOTNOTE);
        note->border_color[i] = page_query_declaration(&query, colors[i], -1, CSS_PAGE_AREA_FOOTNOTE);
    }
    *result = style;
    return VIEW_MODEL_OK;
}
