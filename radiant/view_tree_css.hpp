#pragma once
#include "view.hpp"
#include "view_tree_model.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_paged_media.hpp"

struct ViewCssVariable;
struct CounterSnapshot;
struct ViewCssPageContext;
struct RadiantPageDocument;
struct RadiantPageRegion;
struct RadiantPageDiagnostic;
struct RadiantFlowTraits;
struct RadiantPageQuery;
struct RadiantNoteBinding;
struct RadiantWhitespaceSpec;
struct RadiantImageSpec;
struct RadiantLabelBodySpec;

enum ViewBreak : uint8_t {
    VIEW_BREAK_AUTO, VIEW_BREAK_AVOID, VIEW_BREAK_AVOID_COLUMN, VIEW_BREAK_AVOID_PAGE, VIEW_BREAK_COLUMN, VIEW_BREAK_PAGE,
    VIEW_BREAK_LEFT, VIEW_BREAK_RIGHT, VIEW_BREAK_RECTO, VIEW_BREAK_VERSO,
};

struct ViewCssStyle {
    uint8_t pseudo_element;
    lam::Up<ViewTree> view;
    lam::Up<DomElement> source;
    lam::Up<ViewCssStyle> parent;
    lam::Up<ViewCssStyle> next;
    lam::Up<ViewCssVariable> variables;
    bool computed_bindings;
    ViewModelStatus binding_status;
    const char* binding_reason;
    lam::Up<ViewCssPageContext> page_context;
    DisplayValue display;
    bool vertical;
    FontProp font;
    FontBox font_box;
    // retain family grouping and normal-versus-length spacing in this CSS generation.
    lam::Up<const CssValue> font_values[3]; // family, letter-spacing, word-spacing
    lam::Up<const CssValue> column_values[2]; // count and gap computed at their declaration owner
    lam::Up<const CssValue> column_rule_width;
    CssEnum column_rule_style;
    Color column_rule_color;
    Color color, background;
    lam::Up<const CssValue> width, height, min_width, max_width, min_height, max_height;
    lam::Up<const CssValue> margin[4], padding[4], border_width[4];
    CssEnum border_style[4];
    Color border_color[4];
    float line_height;
    lam::Up<const CssValue> line_height_value;
    CssEnum text_align, white_space, box_sizing;
    uint32_t orphans, widows;
    bool decoration_clone;
    lam::Up<const CssValue> content, counter_reset, counter_increment, counter_set;
    lam::Up<const CounterSnapshot> counters;
    lam::Up<const char> generated_text;
    lam::Up<const CssValue> quotes;
    lam::Up<const CssValue> vertical_align;
    lam::Up<RadiantFlowTraits> flow_traits;
    lam::Up<RadiantPageQuery> page_query;
    lam::Up<RadiantNoteBinding> note_binding;
    lam::Up<RadiantWhitespaceSpec> whitespace;
    lam::Up<RadiantImageSpec> image_spec;
    lam::Up<RadiantLabelBodySpec> label_body;
};

struct ViewCssContext {
    lam::Own<Pool> pool;
    lam::Up<CssEngine> engine;
    lam::Counted<FontContext> fonts;
    lam::Up<ViewCssStyle> styles;
    float root_font_size;
    lam::Up<RadiantPageDocument> page_document;
};

struct ViewPageAreaStyle {
    lam::Up<ViewCssStyle> computed_style;
    const CssDeclaration* width;
    const CssDeclaration* min_width;
    const CssDeclaration* max_width;
    const CssDeclaration* height;
    const CssDeclaration* min_height;
    const CssDeclaration* max_height;
    const CssDeclaration* margin[4];
    const CssDeclaration* padding[4];
    const CssDeclaration* border_width[4];
    const CssDeclaration* border_style[4];
    const CssDeclaration* border_color[4];
    const CssDeclaration* background;
    const CssDeclaration* color;
    const CssDeclaration* box_sizing;
    const CssDeclaration* float_value;
};
struct ViewCssBoxEdges {
    float edges[4], padding[4];
    BorderProp border;
    BackgroundProp background;
};
struct ViewPageStyle {
    lam::Up<ViewCssStyle> computed_style;
    lam::Up<ViewCssStyle> margin_style[CSS_PAGE_MARGIN_BOX_COUNT];
    float width, height;
    float margin[4], padding[4], border_width[4];
    Color background;
    RdtLogicalRect content_rect;
    uint32_t column_count;
    float column_gap;
    lam::Up<const RdtLogicalRect> column_rects; // selected explicit geometry, owned by the CSS generation
    lam::Up<const RadiantPageRegion> body_region;
    lam::Up<ViewCssStyle> body_style;
    RdtLogicalRect body_rect;
    ViewCssBoxEdges body_box;
    bool body_clip;
    lam::Up<const RadiantPageRegion> edge_regions[4];
    lam::Up<ViewCssStyle> edge_style[4];
    RdtLogicalRect edge_rects[4];
    ViewCssBoxEdges edge_boxes[4];
    bool edge_clip[4];
    const CssDeclaration* margin_content[CSS_PAGE_MARGIN_BOX_COUNT];
    const CssDeclaration* margin_font_size[CSS_PAGE_MARGIN_BOX_COUNT];
    const CssDeclaration* margin_color[CSS_PAGE_MARGIN_BOX_COUNT];
    const CssDeclaration* margin_align[CSS_PAGE_MARGIN_BOX_COUNT];
    const CssDeclaration* margin_overflow[CSS_PAGE_MARGIN_BOX_COUNT];
    float bleed;
    bool crop_marks;
    ViewPageAreaStyle footnote;
};

bool view_css_context_begin(ViewTree* tree);
bool view_css_page_column(const ViewPageStyle* style, uint32_t index, RdtLogicalRect* rect);
double view_css_number(const CssValue* value, ViewTree* tree = nullptr, const ViewCssStyle* style = nullptr);
void view_css_context_destroy(ViewTree* tree);
void view_css_context_rebind(ViewTree* tree);
ViewCssStyle* view_css_resolve(ViewTree* tree, DomElement* element);
ViewCssStyle* view_css_resolve_pseudo(ViewTree* tree, DomElement* element, uint8_t pseudo_element);
const CssValue* view_css_property(ViewTree* tree, ViewCssStyle* style, const char* name,
                                 CssDeclaration* winning = nullptr);
const CssValue* view_css_resolve_value(ViewTree* tree, ViewCssStyle* style, const CssValue* value);
bool view_css_computed_property_supported(const char* name);
const CssValue* view_css_computed_property(ViewTree* tree, const ViewCssStyle* style, const char* name);
const CssValue* view_css_declaration_value(ViewTree* tree, ViewCssStyle* style,
    const CssDeclaration* declaration, const char* property);
const CssValue* view_css_compute_length(ViewTree* tree, ViewCssStyle* style,
    CssPropertyCode property, const CssValue* value, const CssValue* inherited);
float view_css_length(ViewTree* tree, const ViewCssStyle* style, const CssValue* value,
                     CssPropertyCode property, float inline_size, float block_size);
ViewModelStatus view_css_page_style(ViewTree* tree, const char* name, uint32_t page_number,
                                  ViewPageSide side, bool blank, ViewPageStyle* result,
                                  RadiantPageDiagnostic* diagnostic = nullptr);

// used border width of one physical side; none and hidden styles have zero width.
float view_css_border_width(ViewTree* tree, const ViewCssStyle* source, size_t side, float width);
ViewModelStatus view_css_box_edges(ViewTree* tree, const ViewCssStyle* source,
    float width, ViewCssBoxEdges* box);
