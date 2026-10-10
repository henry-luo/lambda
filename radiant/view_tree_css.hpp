#pragma once
#include "view.hpp"
#include "view_tree_model.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_paged_media.hpp"

struct SelectorMatcher;
struct ViewCssVariable;
struct CounterSnapshot;
struct ViewCssPageContext;
struct RadiantPageDocument;
struct RadiantPageRegion;
struct RadiantFlowTraits;
struct RadiantPageQuery;
struct RadiantNoteBinding;
struct RadiantWhitespaceSpec;
struct RadiantImageSpec;
struct RadiantLabelBodySpec;

enum ViewBreak : uint8_t {
    VIEW_BREAK_AUTO, VIEW_BREAK_AVOID, VIEW_BREAK_PAGE,
    VIEW_BREAK_LEFT, VIEW_BREAK_RIGHT, VIEW_BREAK_RECTO, VIEW_BREAK_VERSO,
};

struct ViewCssStyle {
    uint8_t pseudo_element;
    lam::Up<ViewTree> view;
    lam::Up<DomElement> source;
    lam::Up<ViewCssStyle> parent;
    lam::Up<ViewCssStyle> next;
    lam::Up<CssDeclaration*> inline_declarations;
    size_t inline_count;
    lam::Up<ViewCssVariable> variables;
    bool computed_bindings;
    ViewModelStatus binding_status;
    const char* binding_reason;
    lam::Up<ViewCssPageContext> page_context;
    DisplayValue display;
    lam::Up<const CssValue> container_names;
    uint8_t container_axes;
    uint8_t computed_containment;
    bool vertical;
    FontProp font;
    FontBox font_box;
    Color color, background;
    lam::Up<const CssValue> width, height, min_width, max_width, min_height, max_height;
    lam::Up<const CssValue> margin[4], padding[4], border_width[4];
    CssEnum border_style[4];
    Color border_color[4];
    float line_height;
    lam::Up<const CssValue> line_height_value;
    CssEnum text_align, white_space, float_value, clear_value, position, box_sizing, caption_side;
    CssEnum list_style_type;
    bool list_marker_inside;
    bool list_reversed;
    lam::Up<const char> list_style_string;
    lam::Up<const CssValue> list_style_image;
    ViewBreak break_before, break_after, break_inside;
    uint32_t orphans, widows;
    bool decoration_clone;
    lam::Up<const char> page_name;
    lam::Up<const CssValue> content, string_set, counter_reset, counter_increment, counter_set;
    lam::Up<const CounterSnapshot> counters;
    lam::Up<const char> generated_text;
    lam::Up<const CssValue> quotes;
    lam::Up<const CssValue> vertical_align;
    lam::Up<const CssValue> running_position, float_reference, float_defer, footnote_policy;
    lam::Up<const CssValue> float_spec;
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
    lam::Up<SelectorMatcher> matcher;
    lam::Counted<FontContext> fonts;
    lam::Up<ViewCssStyle> styles;
    lam::Up<CssStylesheet*> stylesheets;
    size_t stylesheet_count;
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
double view_css_number(const CssValue* value, ViewTree* tree = nullptr, const ViewCssStyle* style = nullptr);
void view_css_context_destroy(ViewTree* tree);
void view_css_context_rebind(ViewTree* tree);
bool view_css_container_pass_begin(ViewTree* tree);
bool view_css_container_pass_end(ViewTree* tree, bool* settled);
bool view_css_container_pass_reset(ViewTree* tree);
void view_css_container_state_destroy(ViewTree* tree);
ViewCssStyle* view_css_resolve(ViewTree* tree, DomElement* element);
ViewCssStyle* view_css_resolve_pseudo(ViewTree* tree, DomElement* element, uint8_t pseudo_element);
ViewCssStyle* view_css_anonymous_style(ViewTree* tree, ViewCssStyle* parent, DisplayValue display);
const ViewCssStyle* view_css_common_ancestor(const ViewCssStyle* left, const ViewCssStyle* right);
const CssValue* view_css_property(ViewTree* tree, ViewCssStyle* style, const char* name,
                                 CssDeclaration* winning = nullptr);
const CssValue* view_css_resolve_value(ViewTree* tree, ViewCssStyle* style, const CssValue* value);
bool view_css_computed_property_supported(const char* name);
const CssValue* view_css_computed_property(ViewTree* tree, const ViewCssStyle* style, const char* name);
const CssValue* view_css_declaration_value(ViewTree* tree, ViewCssStyle* style,
    const CssDeclaration* declaration, const char* property);
ViewCssStyle* view_css_generated_style(ViewTree* tree, ViewCssStyle* base,
    const CssValue* font_size, const CssValue* color, CssEnum align);
const CssValue* view_css_compute_length(ViewTree* tree, ViewCssStyle* style,
    CssPropertyCode property, const CssValue* value, const CssValue* inherited);
float view_css_length(ViewTree* tree, const ViewCssStyle* style, const CssValue* value,
                     CssPropertyCode property, float inline_size, float block_size);
bool view_css_border_spacing(ViewTree* tree, ViewCssStyle* style, float* horizontal, float* vertical);
ViewBreak view_css_break(const CssValue* value);
ViewModelStatus view_css_page_style(ViewTree* tree, const char* name, uint32_t page_number,
                                  ViewPageSide side, bool blank, ViewPageStyle* result);

ViewModelStatus view_css_box_edges(ViewTree* tree, const ViewCssStyle* source,
    float width, ViewCssBoxEdges* box);
