#pragma once
#include "view_tree_model.hpp"
#include "view.hpp"
#include "../lambda/input/css/css_paged_media.hpp"

struct CssEngine;
struct ViewCssStyle;
struct DomText;

struct RadiantLengthPercentage { float value; bool percentage; };
struct RadiantLabelBodySpec {
    RadiantLengthPercentage distance, separation;
    bool grid, context;
    ViewModelStatus status;
    const char* reason;
};
bool radiant_label_body_trait_name(const char* name);
bool radiant_label_body_resolve(ViewTree* tree, ViewCssStyle* style);
const RadiantLabelBodySpec* radiant_label_body_geometry(const ViewCssStyle* style);
bool radiant_label_body_size(const RadiantLabelBodySpec* spec, float width,
    float* label, float* separation, float* body);

// page controls use CSS values, but their source and lifetime are independent of CSS rules.
constexpr const char* RADIANT_PAGE_NAMESPACE = "urn:lambda:radiant:page";

struct RadiantSpaceSpec {
    float minimum, optimum, maximum;
    int32_t precedence;
    bool force, retain, specified;
};
enum RadiantKeepKind : uint8_t { RADIANT_KEEP_AUTO, RADIANT_KEEP_NUMBER, RADIANT_KEEP_ALWAYS };
struct RadiantKeepStrength { int32_t value; RadiantKeepKind kind; };
struct RadiantKeepSpec { RadiantKeepStrength scope[3]; }; // line, column, page
enum RadiantLineStacking : uint8_t { RADIANT_LINE_STACK_CSS, RADIANT_LINE_STACK_MAX, RADIANT_LINE_STACK_FONT };
enum RadiantDecorationConditionality : uint8_t { RADIANT_DECORATION_CSS, RADIANT_DECORATION_DISCARD, RADIANT_DECORATION_RETAIN };
struct RadiantFlowTraits {
    RadiantSpaceSpec before, after;
    RadiantKeepSpec together, next, previous;
    bool relative_cell_before;
    bool descendant_areas;
    bool reference_inline;
    bool table_omit[2]; // header at continuation starts, footer at nonterminal ends
    RadiantDecorationConditionality decoration[2][2]; // border/padding, before/after
    float column_proportion; // zero selects ordinary CSS sizing; positive values share the fixed-track remainder
    uint32_t column_number; // zero follows the preceding placed column/cell; authored numbers are one-based
    float indents[2]; // inherited computed absolute start/end lengths
    lam::Up<const CssValue> indent_expressions[2]; // percentages retain their declaring reference area
    lam::Up<const ViewCssStyle> indent_owners[2];
    RadiantLineStacking line_stacking;
    float text_metrics[2]; // altitude, depth
    bool text_metrics_set[2];
    ViewModelStatus status;
    const char* reason;
};

struct RadiantNoteBinding {
    DomNodeRef call, body;
    ViewModelStatus status;
    const char* reason;
};

enum RadiantLinefeedTreatment : uint8_t { RADIANT_LINEFEED_SPACE, RADIANT_LINEFEED_PRESERVE,
    RADIANT_LINEFEED_IGNORE, RADIANT_LINEFEED_ZERO_WIDTH };
struct RadiantWhitespaceSpec {
    RadiantLinefeedTreatment linefeed;
    bool collapse, wrap, ignore, discard_start, discard_end;
    ViewModelStatus status;
    const char* reason;
};
bool radiant_whitespace_trait_name(const char* name);
bool radiant_whitespace_resolve(ViewTree* tree, ViewCssStyle* style);
RadiantWhitespaceSpec radiant_whitespace_spec(const ViewCssStyle* style);

enum RadiantImageAxisKind : uint8_t { RADIANT_IMAGE_AUTO, RADIANT_IMAGE_LENGTH, RADIANT_IMAGE_PERCENT,
    RADIANT_IMAGE_FIT, RADIANT_IMAGE_FIT_DOWN, RADIANT_IMAGE_FIT_UP };
struct RadiantImageAxis { RadiantImageAxisKind kind; float value; };
struct RadiantImageSpec {
    RadiantImageAxis axes[2];
    bool non_uniform;
    ViewModelStatus status;
    const char* reason;
};
bool radiant_image_trait_name(const char* name);
bool radiant_image_traits_resolve(ViewTree* tree, ViewCssStyle* style);
bool radiant_image_size(const RadiantImageSpec* spec, float natural_width, float natural_height,
    float* viewport_width, float* viewport_height, float* content_width, float* content_height);
const char* radiant_page_resource_url(DomElement* source, const char* resource, Pool* pool, size_t max_depth);

// native attributes use the page namespace; values resolve in the selected style's font context.
bool radiant_flow_trait_name(const char* name);
bool radiant_flow_traits_resolve(ViewTree* tree, ViewCssStyle* style);
bool radiant_table_column_proportion(const CssValue* value, float* result);
bool radiant_decoration_retain(const ViewCssStyle* style, bool padding, bool after);
bool radiant_text_metrics(const ViewCssStyle* style, float* altitude, float* depth);
RadiantSpaceSpec radiant_spaces_resolve(const RadiantSpaceSpec* spaces, size_t count, bool start, bool end);
RadiantKeepStrength radiant_keep_max(RadiantKeepStrength left, RadiantKeepStrength right);
int radiant_keep_compare(RadiantKeepStrength left, RadiantKeepStrength right);

enum RadiantPageRuleSource : uint8_t { RADIANT_PAGE_CSS, RADIANT_PAGE_NATIVE };

enum RadiantPageRegionRole : uint8_t { RADIANT_REGION_BEFORE, RADIANT_REGION_AFTER,
    RADIANT_REGION_START, RADIANT_REGION_END, RADIANT_REGION_BODY, RADIANT_REGION_EDGE_COUNT = 4 };
enum RadiantRegionAlign : uint8_t { RADIANT_REGION_ALIGN_BEFORE, RADIANT_REGION_ALIGN_CENTER, RADIANT_REGION_ALIGN_AFTER };
struct RadiantPageRegion {
    DomNodeRef source;
    const char* name;
    CssDeclaration** declarations;
    size_t declaration_count;
    const CssValue* extent;
    RadiantPageRegionRole role;
    RadiantRegionAlign align;
    bool precedence;
    bool zero_box;
};

struct RadiantPageRule {
    RadiantPageRuleSource kind;
    DomNodeRef source;
    const char* name;
    CssPageSelector* selectors;
    size_t selector_count;
    CssDeclaration** declarations;
    size_t declaration_count;
    CssPageAreaRule* areas;
    size_t area_count;
    CssOrigin origin;
    RadiantPageRegion* body;
    RadiantPageRegion* edges[RADIANT_REGION_EDGE_COUNT];
    RadiantPageRule* next;
};

struct RadiantPageDiagnostic {
    ViewModelStatus status;
    DomNodeRef source;
    const char* reason;
};

struct RadiantSourceOrigin {
    DomNodeRef source;
    const Element* translated;
    const char* qname;
    size_t start, end;
    bool has_range;
    RadiantSourceOrigin* next;
};

struct RadiantPageFlowBinding {
    DomNodeRef source;
    const char* region_name;
    RadiantPageFlowBinding* next;
};

enum RadiantMasterPosition : uint8_t { RADIANT_MASTER_ANY, RADIANT_MASTER_FIRST, RADIANT_MASTER_LAST,
    RADIANT_MASTER_ONLY, RADIANT_MASTER_REST };
enum RadiantMasterParity : uint8_t { RADIANT_PARITY_ANY, RADIANT_PARITY_ODD, RADIANT_PARITY_EVEN };
enum RadiantMasterBlank : uint8_t { RADIANT_BLANK_ANY, RADIANT_BLANK_ONLY, RADIANT_NOT_BLANK };
struct RadiantMasterRule {
    DomNodeRef source;
    const RadiantPageRule* master;
    RadiantMasterPosition position;
    RadiantMasterParity parity;
    RadiantMasterBlank blank;
    RadiantMasterRule* next;
};
struct RadiantMasterRun {
    DomNodeRef source;
    uint32_t maximum_repeats; // UINT32_MAX denotes an unbounded run; zero skips the run.
    RadiantMasterRule* rules;
    RadiantMasterRun* next;
};
struct RadiantSequenceMaster {
    DomNodeRef source;
    const char* name;
    RadiantMasterRun* runs;
    RadiantSequenceMaster* next;
    bool terminal;
};
enum RadiantInitialFolio : uint8_t { RADIANT_FOLIO_AUTO, RADIANT_FOLIO_ODD, RADIANT_FOLIO_EVEN, RADIANT_FOLIO_NUMBER };
enum RadiantSequenceEnd : uint8_t { RADIANT_END_AUTO, RADIANT_END_EVEN, RADIANT_END_ODD,
    RADIANT_END_ON_EVEN, RADIANT_END_ON_ODD, RADIANT_END_NO_FORCE };

struct RadiantFolioFormat {
    CssEnum style;
    uint32_t zero_digit, minimum_digits, grouping_size;
    const char* prefix;
    const char* suffix;
    const char* grouping_separator;
};

enum RadiantPageQueryEdge : uint8_t { RADIANT_QUERY_CURRENT, RADIANT_QUERY_FIRST, RADIANT_QUERY_LAST };
enum RadiantPageQueryArea : uint8_t { RADIANT_QUERY_ALL, RADIANT_QUERY_NORMAL, RADIANT_QUERY_NON_BLANK };
struct RadiantPageQuery {
    const char* target;
    RadiantPageQueryEdge edge;
    RadiantPageQueryArea area;
    ViewModelStatus status;
    const char* reason;
};

struct RadiantPageSequence {
    DomNodeRef source;
    const char* master_reference;
    const RadiantSequenceMaster* master_program;
    RadiantInitialFolio initial;
    uint32_t initial_folio;
    RadiantSequenceEnd end;
    RadiantFolioFormat format;
    RadiantPageFlowBinding* flows;
    RadiantPageFlowBinding* static_content;
    RadiantPageSequence* next;
    bool implicit;
};

enum RadiantSourcePageBox : uint8_t { RADIANT_SOURCE_MEDIA, RADIANT_SOURCE_CROP,
    RADIANT_SOURCE_BLEED, RADIANT_SOURCE_TRIM, RADIANT_SOURCE_ART, RADIANT_SOURCE_BOX_COUNT };
struct RadiantFixedGeometry {
    RdtLogicalRect boxes[RADIANT_SOURCE_BOX_COUNT]; // original PDF-point boxes, before intersection/rotation
    uint8_t box_mask;
    int32_t rotation;
    uint32_t source_page;
};
struct RadiantFixedPage {
    DomNodeRef source;
    RdtLogicalRect viewport;
    RadiantFixedGeometry geometry;
    const char* label;
    RadiantFixedPage* next;
};
RdtLogicalRect radiant_fixed_page_crop(const RadiantFixedGeometry* geometry);
// inverse normalization maps the exporter’s y-up logical viewport into source user space.
RdtMatrix radiant_fixed_page_source_transform(const RadiantFixedGeometry* geometry);

struct RadiantPageDocument {
    RadiantPageRule* rules;
    RadiantPageRule* last_rule;
    size_t rule_count;
    RadiantSequenceMaster* masters;
    RadiantPageSequence* sequences;
    size_t sequence_count;
    DomNodeRef fixed_source;
    RadiantFixedPage* fixed_pages;
    size_t fixed_count;
    RadiantPageDiagnostic diagnostic;
};

// immutable snapshots belong to the selected view's CSS pool (D4.5.1v4 / D4.1.4v5).
RadiantPageDocument* radiant_page_document_compile(ViewTree* tree, CssEngine* engine, Pool* pool);
bool radiant_page_element(DomElement* element, const char* local_name);
// shared FO/native cardinality normalization within the common counter domain.
bool radiant_page_rounded_count(const char* text, uint32_t minimum, uint32_t* result);
bool radiant_page_rounded_count(double value, uint32_t minimum, uint32_t* result);
bool radiant_page_boolean(const char* text, bool inherited, bool* result);
DomElement* radiant_page_style_parent(DomElement* source);
bool radiant_page_control_hidden(DomElement* element);
const RadiantPageSequence* radiant_page_sequence_for(const RadiantPageDocument* document, DomElement* element);
const RadiantPageRule* radiant_page_master_select(const RadiantPageSequence* sequence,
    uint32_t ordinal, uint32_t folio, bool blank, bool terminal);
bool radiant_page_query_resolve(ViewTree* tree, ViewCssStyle* style);
bool radiant_note_resolve(ViewTree* tree, ViewCssStyle* style);
bool radiant_page_whitespace(DomText* text);
bool radiant_folio_append(const RadiantFolioFormat* format, uint32_t folio, StrBuf* text);
bool radiant_page_set_origins(DomDocument* document, RadiantSourceOrigin* origins);
const RadiantSourceOrigin* radiant_page_source_origin(DomDocument* document, DomNode* translated);
