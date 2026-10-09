#include "page_fo.hpp"
#include "page_fo_expression.hpp"
#include "view_tree_css.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/format/format.h"
#include "../lib/strbuf.h"
#include "../lib/arraylist.hpp"
#include "../lib/hashmap_helpers.h"
#include "../lib/url.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>

HASHMAP_DEFINE_PTRKEY(fo_source_spans, RadiantFoSourceSpan, source)
struct FoSourceId { const char* name; DomNodeRef source; };
HASHMAP_DEFINE_STRKEY(fo_source_ids, FoSourceId, name)

struct FoTranslationContext {
    DomDocument* document;
    RadiantFoTranslation* result;
    const RadiantFoOptions* options;
    MarkBuilder builder;
    HashMap* spans = nullptr;
    HashMap* ids = nullptr;
    size_t expression_nodes = 0;
    StrBuf* property_bindings = nullptr;
    size_t binding_count = 0;
    FoTranslationContext(DomDocument* doc, RadiantFoTranslation* out, const RadiantFoOptions* opts)
        : document(doc), result(out), options(opts), builder(doc->input) {}
    ~FoTranslationContext() { if (spans) hashmap_free(spans); if (ids) hashmap_free(ids); }
};

RadiantFoOptions radiant_fo_options_default() { return {1000000, 256, nullptr}; }

static bool fo_element(DomElement* element, const char* local) {
    return element && strcmp(element->local_name(), local) == 0 &&
        strcmp(dom_element_namespace_uri(element), RADIANT_FO_NAMESPACE) == 0;
}

static bool fo_graphic(DomElement* source) {
    return fo_element(source, "external-graphic") || fo_element(source, "instream-foreign-object");
}

static const char* fo_region_names[] = {"region-before", "region-after", "region-start", "region-end", "region-body"};
static const char* fo_region_roles[] = {"before", "after", "start", "end", "body"};
static size_t fo_region_kind(DomElement* source) {
    for (size_t i = 0; i < 5; i++) if (fo_element(source, fo_region_names[i])) return i;
    return SIZE_MAX;
}

enum FoTableKind { FO_TABLE, FO_TABLE_COLUMN, FO_TABLE_HEADER, FO_TABLE_FOOTER,
    FO_TABLE_BODY, FO_TABLE_ROW, FO_TABLE_CELL, FO_TABLE_NONE };
static const struct { const char* source; const char* target; } fo_table_objects[] = {
    {"table", "table"}, {"table-column", "col"}, {"table-header", "thead"},
    {"table-footer", "tfoot"}, {"table-body", "tbody"}, {"table-row", "tr"}, {"table-cell", "td"}
};
static FoTableKind fo_table_kind(DomElement* source) {
    for (size_t i = 0; i < FO_TABLE_NONE; i++)
        if (fo_element(source, fo_table_objects[i].source)) return static_cast<FoTableKind>(i);
    return FO_TABLE_NONE;
}
static bool fo_table_group(FoTableKind kind) {
    return kind == FO_TABLE_HEADER || kind == FO_TABLE_FOOTER || kind == FO_TABLE_BODY;
}

enum FoListKind { FO_LIST_BLOCK, FO_LIST_ITEM, FO_LIST_LABEL, FO_LIST_BODY, FO_LIST_NONE };
static const struct { const char* source; const char* target; } fo_list_objects[] = {
    {"list-block", "div"}, {"list-item", "table"}, {"list-item-label", "td"}, {"list-item-body", "td"}
};
static FoListKind fo_list_kind(DomElement* source) {
    for (size_t i = 0; i < FO_LIST_NONE; i++)
        if (fo_element(source, fo_list_objects[i].source)) return static_cast<FoListKind>(i);
    return FO_LIST_NONE;
}

static bool fo_block_object(DomElement* source) {
    return fo_element(source, "block") || fo_table_kind(source) == FO_TABLE || fo_list_kind(source) == FO_LIST_BLOCK;
}

static const RadiantFoSourceSpan* fo_source_span(FoTranslationContext* context, DomElement* source) {
    RadiantFoSourceSpan key = {}; key.source = dom_element_render_source(source);
    return context->spans ? (const RadiantFoSourceSpan*)hashmap_get(context->spans, &key) : nullptr;
}

static bool fo_failure(FoTranslationContext* context, DomElement* source, const char* property,
        const char* reason, TypesetStatus status = TYPESET_INVALID) {
    RadiantFoDiagnostic& diagnostic = context->result->diagnostic;
    if (diagnostic.status == TYPESET_OK) {
        diagnostic.status = status; diagnostic.source = source ? dom_node_ref(source) : DomNodeRef{};
        diagnostic.qname = source ? source->tag_name.get() : nullptr;
        diagnostic.property = property; diagnostic.reason = reason;
        const RadiantFoSourceSpan* span = source ? fo_source_span(context, source) : nullptr;
        if (span) { diagnostic.start = span->start; diagnostic.end = span->end; diagnostic.has_range = true; }
    }
    return false;
}

static const char* fo_parent_value(FoTranslationContext* context, DomElement* source, const char* property,
    const char* value, bool nominal_context = true);
enum FoPropertyReference { FO_PROPERTY_PARENT, FO_PROPERTY_INHERITED, FO_PROPERTY_NEAREST };
static const char* fo_reference_binding(FoTranslationContext* context, DomElement* source,
    const char* property, const char* query, FoPropertyReference reference);
static bool fo_property_assigned(DomElement* source, const char* name);
static const char* fo_computed_property_name(const char* name);
static bool fo_decoration_condition_name(const char* name);

static const char* fo_inherited(FoTranslationContext* context, DomElement* source, const char* name) {
    for (DomElement* node = source; node; node = node->parent_element()) {
        const char* raw = node->get_attribute(name);
        const char* value = fo_parent_value(context, node, name, raw);
        if (raw && !value) return nullptr;
        if (value && strcmp(value, "inherit") != 0) return value;
        if (fo_element(node, "root")) break;
    }
    return nullptr;
}

static bool fo_property_inherits(const char* name) {
    const CssProperty* property = css_property_get_by_name(name);
    if (property && property->inheritance == PROP_INHERIT_YES) return true;
    bool space = false; size_t index = 0, component = 0;
    if (radiant_flow_trait_key(name, &space, &index, &component) && !space && !index) return true;
    static const char* inherited[] = {"border-collapse", "empty-cells", "start-indent", "end-indent",
        "line-stacking-strategy", "display-align", "relative-align", "provisional-distance-between-starts", "provisional-label-separation",
        "allowed-width-scale", "allowed-height-scale"};
    for (const char* candidate : inherited) if (!strcmp(candidate, name)) return true;
    return radiant_whitespace_trait_name(name);
}

static const char* fo_parent_value(FoTranslationContext* context, DomElement* source,
        const char* property, const char* value, bool nominal_context) {
    if (!value) return nullptr;
    const char* first = value + strspn(value, " \t\r\n");
    static const char* functions[] = {"from-parent", "inherited-property-value", "from-nearest-specified-value"};
    for (size_t kind = 0; kind < 3; kind++) {
        size_t length = strlen(functions[kind]);
        if (strncmp(first, functions[kind], length)) continue;
        const char* cursor = first + length;
        cursor += strspn(cursor, " \t\r\n");
        if (*cursor != '(') continue;
        if (context->result->node_count > context->options->max_nodes ||
            context->expression_nodes >= context->options->max_nodes - context->result->node_count) {
            fo_failure(context, source, property, "FO expression budget exhausted", TYPESET_BUDGET_EXHAUSTED); return nullptr;
        }
        context->expression_nodes++;
        cursor++; cursor += strspn(cursor, " \t\r\n");
        size_t argument = strcspn(cursor, " \t\r\n),");
        const char* query = argument ? pool_dup_n(context->document->document_pool, cursor, argument) : property;
        if (!query) { fo_failure(context, source, property, "FO reference allocation failed", TYPESET_OUT_OF_MEMORY); return nullptr; }
        cursor += argument; cursor += strspn(cursor, " \t\r\n");
        if (*cursor != ')') {
            fo_failure(context, source, property, "parent reference requires one whole-property function with an optional matching property name"); return nullptr;
        }
        if (cursor[1 + strspn(cursor + 1, " \t\r\n")]) return value;
        if (kind == FO_PROPERTY_INHERITED && !fo_property_inherits(query)) {
            fo_failure(context, source, property, "inherited-property-value requires an inherited FO property"); return nullptr;
        }
        FoPropertyReference reference = static_cast<FoPropertyReference>(kind);
        if (kind == FO_PROPERTY_NEAREST || strcmp(query, property)) return fo_reference_binding(context, source, property, query, reference);
        if (fo_element(source, "root") || !source->parent_element()) {
            if (fo_decoration_condition_name(property)) return "discard";
            static const struct { const char* name; const char* initial; } initials[] = {
                {"line-stacking-strategy", "max-height"}, {"text-altitude", "use-font-metrics"}, {"text-depth", "use-font-metrics"},
                {"linefeed-treatment", "treat-as-space"}, {"white-space-treatment", "ignore-if-surrounding-linefeed"},
                {"white-space-collapse", "true"}, {"wrap-option", "wrap"}, {"border-collapse", "collapse"}
            };
            for (const auto& initial : initials) if (!strcmp(initial.name, property)) return initial.initial;
            if (view_css_computed_property_supported(fo_computed_property_name(query)))
                return fo_reference_binding(context, source, property, query, reference);
        }
        value = "inherit";
        break;
    }
    if (nominal_context && !strcmp(value, "inherit") &&
        (!strcmp(property, "text-altitude") || !strcmp(property, "text-depth"))) {
        // FO inherits the metric keyword, whose actual value belongs to the child's font.
        for (DomElement* parent = source->parent_element(); parent; parent = parent->parent_element()) {
            const char* raw = parent->get_attribute(property);
            const char* computed = fo_parent_value(context, parent, property, raw, false);
            if (raw && !computed) return nullptr;
            if (!computed || !strcmp(computed, "use-font-metrics")) return "use-font-metrics";
            if (strcmp(computed, "inherit")) return value;
        }
        return "use-font-metrics";
    }
    return value;
}

struct FoExpressionReferenceContext { FoTranslationContext* translation; DomElement* source; const char* property; };
static CssValue* fo_expression_reference(void* owner, const char* function, const char* property) {
    FoExpressionReferenceContext* query = (FoExpressionReferenceContext*)owner;
    FoPropertyReference reference = !strcmp(function, "from-nearest-specified-value") ? FO_PROPERTY_NEAREST :
        !strcmp(function, "inherited-property-value") ? FO_PROPERTY_INHERITED : FO_PROPERTY_PARENT;
    const char* text = fo_reference_binding(query->translation, query->source, query->property,
        property ? property : query->property, reference);
    if (!text) return nullptr;
    CssDeclaration* value = css_parse_property_value_declaration("margin-top", 10, text, strlen(text), query->translation->document->document_pool);
    return value ? value->value : nullptr;
}

static CssValue* fo_expression(FoTranslationContext* context, DomElement* source, const char* property, const char* text,
        double* proportion = nullptr, RadiantFoExpressionSyntax syntax = FO_EXPRESSION_SCALAR) {
    size_t used = context->result->node_count + context->expression_nodes;
    size_t remaining = used < context->options->max_nodes ? context->options->max_nodes - used : 0;
    FoExpressionReferenceContext query = {context, source, property};
    RadiantFoExpression expression = radiant_fo_expression(context->document->document_pool, text,
        remaining, context->options->max_depth, fo_expression_reference, &query, proportion, syntax);
    context->expression_nodes += expression.nodes;
    if (expression.status != TYPESET_OK) {
        fo_failure(context, source, property, expression.reason, expression.status); return nullptr;
    }
    return expression.value;
}

static const char* fo_computed_text(FoTranslationContext* context, DomElement* source, const char* property, CssValue* value) {
    if (!value) return nullptr;
    // unitless arithmetic is fully computed here; lengths retain the selected style's font/percentage context.
    CssMathEvaluationContext evaluation = {};
    CssMathResult number = css_math_evaluate(value, &evaluation);
    if (number.type == CSS_MATH_NUMBER && number.resolved) {
        char buffer[64]; snprintf(buffer, sizeof(buffer), "%.17g", number.value);
        const char* result = pool_strdup(context->document->document_pool, buffer);
        if (!result) fo_failure(context, source, property, "FO expression allocation failed", TYPESET_OUT_OF_MEMORY);
        return result;
    }
    CssDeclaration declaration = {}; declaration.value = value;
    const char* result = css_serialize_declaration_value(&declaration, context->document->document_pool);
    if (!result || !*result) { fo_failure(context, source, property, "FO expression serialization failed", TYPESET_OUT_OF_MEMORY); return nullptr; }
    return result;
}

static const char* fo_expression_text(FoTranslationContext* context, DomElement* source, const char* property, const char* text,
        RadiantFoExpressionSyntax syntax = FO_EXPRESSION_SCALAR) {
    return fo_computed_text(context, source, property, fo_expression(context, source, property, text, nullptr, syntax));
}

static void fo_style_append(StrBuf* style, const char* name, const char* value) {
    strbuf_append_str(style, name); strbuf_append_char(style, ':'); strbuf_append_str(style, value); strbuf_append_char(style, ';');
}

static bool fo_translation_charge(FoTranslationContext* context) {
    return ++context->result->node_count <= context->options->max_nodes &&
        context->expression_nodes <= context->options->max_nodes - context->result->node_count;
}

static Item fo_transparent_row(FoTranslationContext* context, DomElement* source, const Item* cells, size_t count) {
    if (!fo_translation_charge(context)) {
        fo_failure(context, source, nullptr, "FO translation budget exhausted", TYPESET_BUDGET_EXHAUSTED); return ItemNull;
    }
    // inserted rows preserve the original list-item or table-group inheritance context.
    ElementBuilder row = context->builder.element("tr"); row.attr("r:style-transparent", "true");
    for (size_t i = 0; i < count; i++) row.child(cells[i]);
    return row.final();
}

static bool fo_style_property(FoTranslationContext* context, DomElement* source, StrBuf* style,
        const char* name, const char* value, const char* source_name = nullptr, bool emit = true, bool length_component = false) {
    if (!value) return true;
    const char* authored = value;
    value = fo_parent_value(context, source, source_name ? source_name : name, value);
    if (!value) return false;
    if (!strcmp(name, "border-collapse") && !strcmp(value, "inherit")) {
        // FO inherits this keyword with an initial collapse; HTML's separate-border initial cannot stand in for it.
        value = fo_inherited(context, source->parent_element(), "border-collapse");
        if (!value) value = "collapse";
    }
    CssDeclaration* declaration = css_parse_property_value_declaration(name, strlen(name), value, strlen(value), context->document->document_pool);
    const CssProperty* property = css_property_get_by_name(name);
    bool expression = property && (property->type == PROP_TYPE_LENGTH || property->type == PROP_TYPE_NUMBER ||
        property->type == PROP_TYPE_COLOR || property->code == CSS_PROPERTY_FONT_WEIGHT);
    bool shorthand = property && css_property_is_shorthand(property->code);
    expression |= shorthand;
    if (expression && (!declaration || !declaration->valid || !css_declaration_is_supported(declaration) ||
        (declaration->value && (declaration->value->type == CSS_VALUE_TYPE_FUNCTION || (shorthand && strchr(value, '('))) &&
            (value == authored || !css_value_contains_var_reference(declaration->value))))) {
        value = fo_expression_text(context, source, source_name ? source_name : name, value,
            shorthand ? FO_EXPRESSION_COMPONENTS : FO_EXPRESSION_SCALAR);
        if (!value) return false;
        declaration = css_parse_property_value_declaration(name, strlen(name), value, strlen(value), context->document->document_pool);
    }
    if (!declaration || !declaration->valid || !css_declaration_is_supported(declaration))
        return fo_failure(context, source, source_name ? source_name : name, "FO property value is unsupported by the common style engine");
    const CssValue* parsed = declaration->value;
    if (length_component && !(css_value_is_inherit(parsed) || css_value_contains_var_reference(parsed) || parsed->type == CSS_VALUE_TYPE_LENGTH ||
        parsed->type == CSS_VALUE_TYPE_PERCENTAGE || parsed->type == CSS_VALUE_TYPE_FUNCTION ||
        (parsed->type == CSS_VALUE_TYPE_NUMBER && parsed->data.number.value == 0.0)))
        return fo_failure(context, source, source_name, "FO length component requires a length or inherit");
    if (emit) fo_style_append(style, name, value);
    return true;
}

static bool fo_sheet_size(FoTranslationContext* context, DomElement* source, StrBuf* style,
        const char* width, const char* height) {
    const char* names[] = {"page-width", "page-height"};
    const char* values[] = {width, height};
    double pixels[2] = {};
    for (size_t i = 0; i < 2; i++) {
        CssValue* value = fo_expression(context, source, names[i], values[i]);
        if (!value) return false;
        CssMathEvaluationContext evaluation = {};
        CssMathResult computed = css_math_evaluate(value, &evaluation);
        if (computed.type != CSS_MATH_LENGTH || !computed.resolved || !isfinite(computed.value) || computed.value <= 0.0)
            return fo_failure(context, source, names[i], "FO initial profile requires positive absolute sheet dimensions");
        pixels[i] = computed.value;
    }
    // size is a page descriptor, outside CSS.supports' ordinary property registry.
    char dimensions[128]; snprintf(dimensions, sizeof(dimensions), "size:%.17gpx %.17gpx;", pixels[0], pixels[1]);
    strbuf_append_str(style, dimensions);
    return true;
}

struct FoStyleBuffer {
    StrBuf* value = strbuf_new();
    ~FoStyleBuffer() { strbuf_free(value); }
};

struct FoBindingScope {
    FoTranslationContext* context;
    StrBuf* previous;
    FoStyleBuffer buffer;
    FoBindingScope(FoTranslationContext* owner) : context(owner), previous(owner->property_bindings) {
        context->property_bindings = buffer.value;
    }
    ~FoBindingScope() { context->property_bindings = previous; }
};

static const char* fo_reference_binding(FoTranslationContext* context, DomElement* source,
        const char* property, const char* query, FoPropertyReference reference) {
    const char* native_query = fo_computed_property_name(query);
    if ((reference == FO_PROPERTY_INHERITED && !fo_property_inherits(query)) || !view_css_computed_property_supported(native_query)) {
        fo_failure(context, source, property, "FO property reference requires an admitted computed property and inheritance domain"); return nullptr;
    }
    size_t levels = 1;
    if (reference == FO_PROPERTY_NEAREST) {
        DomElement* ancestor = source->parent_element();
        while (ancestor && !fo_property_assigned(ancestor, query)) {
            if (fo_element(ancestor, "root")) { ancestor = nullptr; break; }
            ancestor = ancestor->parent_element(); levels++;
        }
        if (!ancestor) levels = 0;
    }
    if (!levels || fo_element(source, "root") || !source->parent_element()) {
        if (!strcmp(native_query, "font-size")) return "12pt";
        if (radiant_image_trait_name(native_query))
            return fo_computed_text(context, source, property,
                radiant_image_computed_trait(context->document->document_pool, nullptr, native_query));
        bool space = false; size_t index = 0, component = 0;
        if (radiant_flow_trait_key(native_query, &space, &index, &component) && component)
            return !space ? "auto" : component < 4 ? "0px" : component == 4 ? "0" : "discard";
        const char* suffix = strrchr(native_query, '-');
        const char* initial = !strcmp(native_query, "start-indent") || !strcmp(native_query, "end-indent") ||
            (!strncmp(native_query, "border-", 7) && suffix && !strcmp(suffix, "-width")) ? "0px" : nullptr;
        if (initial) return initial;
        const CssProperty* definition = css_property_get_by_name(native_query);
        return definition ? definition->initial_value : nullptr;
    }
    if (!context->property_bindings) { fo_failure(context, source, property, "FO binding allocation failed", TYPESET_OUT_OF_MEMORY); return nullptr; }
    char variable[64]; snprintf(variable, sizeof(variable), "--rpd-property-%zu", ++context->binding_count);
    strbuf_append_str(context->property_bindings, variable);
    if (levels == 1) strbuf_append_str(context->property_bindings, ":parent(");
    else { strbuf_append_str(context->property_bindings, ":ancestor("); strbuf_append_uint64(context->property_bindings, levels); strbuf_append_char(context->property_bindings, ','); }
    // native component names use CSS identifier escaping inside binding expressions.
    StringBuf* escaped = stringbuf_new(context->document->document_pool);
    if (!escaped) { fo_failure(context, source, property, "FO binding allocation failed", TYPESET_OUT_OF_MEMORY); return nullptr; }
    css_append_identifier(escaped, native_query, strlen(native_query));
    strbuf_append_str(context->property_bindings, escaped->str->chars); stringbuf_free(escaped);
    strbuf_append_str(context->property_bindings, ");");
    char value[80]; snprintf(value, sizeof(value), "var(%s)", variable);
    const char* result = pool_strdup(context->document->document_pool, value);
    if (!result) fo_failure(context, source, property, "FO binding allocation failed", TYPESET_OUT_OF_MEMORY);
    return result;
}

static const char* fo_component_name(StrBuf* buffer, const char* property, const char* component, bool native = false) {
    strbuf_reset(buffer);
    if (native) strbuf_append_str(buffer, "r:");
    strbuf_append_str(buffer, property); strbuf_append_char(buffer, '.'); strbuf_append_str(buffer, component);
    return buffer->str;
}

static const char* fo_common_properties[] = {"font-family", "font-size", "font-style", "font-weight", "color", "line-height", "text-align", "orphans", "widows",
    "background-color", "border", "border-width", "border-style", "border-color", "border-top", "border-right", "border-bottom", "border-left",
    "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
    "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
    "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
    "padding", "padding-top", "padding-right", "padding-bottom", "padding-left", "border-collapse", "empty-cells"};
static const struct { const char* relative; CssPropertyCode family; CssBoxSide side; } fo_corresponding_properties[] = {
    {"border-before-width", CSS_PROPERTY_BORDER_WIDTH, CSS_BOX_SIDE_TOP},
    {"border-after-width", CSS_PROPERTY_BORDER_WIDTH, CSS_BOX_SIDE_BOTTOM},
    {"border-start-width", CSS_PROPERTY_BORDER_WIDTH, CSS_BOX_SIDE_LEFT},
    {"border-end-width", CSS_PROPERTY_BORDER_WIDTH, CSS_BOX_SIDE_RIGHT},
    {"border-before-style", CSS_PROPERTY_BORDER_STYLE, CSS_BOX_SIDE_TOP},
    {"border-after-style", CSS_PROPERTY_BORDER_STYLE, CSS_BOX_SIDE_BOTTOM},
    {"border-start-style", CSS_PROPERTY_BORDER_STYLE, CSS_BOX_SIDE_LEFT},
    {"border-end-style", CSS_PROPERTY_BORDER_STYLE, CSS_BOX_SIDE_RIGHT},
    {"border-before-color", CSS_PROPERTY_BORDER_COLOR, CSS_BOX_SIDE_TOP},
    {"border-after-color", CSS_PROPERTY_BORDER_COLOR, CSS_BOX_SIDE_BOTTOM},
    {"border-start-color", CSS_PROPERTY_BORDER_COLOR, CSS_BOX_SIDE_LEFT},
    {"border-end-color", CSS_PROPERTY_BORDER_COLOR, CSS_BOX_SIDE_RIGHT},
    {"padding-before", CSS_PROPERTY_PADDING, CSS_BOX_SIDE_TOP},
    {"padding-after", CSS_PROPERTY_PADDING, CSS_BOX_SIDE_BOTTOM},
    {"padding-start", CSS_PROPERTY_PADDING, CSS_BOX_SIDE_LEFT},
    {"padding-end", CSS_PROPERTY_PADDING, CSS_BOX_SIDE_RIGHT}
};
static bool fo_conditional_length(CssPropertyCode family) {
    return family == CSS_PROPERTY_BORDER_WIDTH || family == CSS_PROPERTY_PADDING;
}

static const char* fo_computed_property_name(const char* name) {
    for (const auto& property : fo_corresponding_properties) {
        size_t length = strlen(property.relative);
        bool length_component = fo_conditional_length(property.family) && !strncmp(name, property.relative, length) &&
            !strcmp(name + length, ".length");
        if (length_component || (!fo_conditional_length(property.family) && !strcmp(name, property.relative)))
            return css_property_get_by_code(radiant_box_side_property(property.family, property.side))->name;
    }
    return name;
}

static bool fo_property_assigned(DomElement* source, const char* name) {
    if (source->get_attribute(name)) return true;
    bool space = false; size_t index = 0, component = 0; const char* base = nullptr;
    if (radiant_flow_trait_key(name, &space, &index, &component, &base) && component)
        return source->get_attribute(base) != nullptr;
    CssPropertyCode query = css_property_code_from_name(fo_computed_property_name(name));
    if (query == CSS_PROPERTY_UNKNOWN) return false;
    int count = 0; const char** names = source->attribute_names(&count);
    for (int i = 0; i < count; i++) if (css_property_shorthand_contains(css_property_code_from_name(names[i]), query)) return true;
    for (const auto& property : fo_corresponding_properties) if (radiant_box_side_property(property.family, property.side) == query) {
        size_t length = strlen(property.relative);
        for (int i = 0; i < count; i++) if (!strncmp(names[i], property.relative, length) &&
            (!names[i][length] || names[i][length] == '.')) return true;
    }
    return false;
}

static bool fo_length_component_name(const char* name) {
    for (const auto& property : fo_corresponding_properties) if (fo_conditional_length(property.family)) {
        size_t length = strlen(property.relative);
        if (!strncmp(name, property.relative, length) && !strcmp(name + length, ".length")) return true;
    }
    return false;
}

static bool fo_decoration_condition_name(const char* name) {
    return radiant_flow_trait_name(name) && (!strncmp(name, "border-", 7) || !strncmp(name, "padding-", 8));
}

static const char* fo_margin_properties[] = {"margin", "margin-top", "margin-right", "margin-bottom", "margin-left"};
static const char* fo_number_properties[] = {"format", "letter-value", "grouping-separator", "grouping-size"};
static const char* fo_region_properties[] = {"extent", "precedence"};

static bool fo_zero_length(FoTranslationContext* context, const char* value) {
    if (!value) return true;
    CssDeclaration* declaration = css_parse_property_value_declaration("margin-left", 11, value, strlen(value), context->document->document_pool);
    const CssValue* parsed = declaration ? declaration->value : nullptr;
    return parsed && ((parsed->type == CSS_VALUE_TYPE_NUMBER && !parsed->data.number.value) ||
        (parsed->type == CSS_VALUE_TYPE_LENGTH && !parsed->data.length.value) ||
        (parsed->type == CSS_VALUE_TYPE_PERCENTAGE && !parsed->data.percentage.value));
}

static bool fo_property_in(const char* name, const char* const* properties, size_t count) {
    for (size_t i = 0; i < count; i++) if (!strcmp(name, properties[i])) return true;
    return false;
}

static const char* fo_display_alignment(FoTranslationContext* context, DomElement* source, bool relative) {
    const char* value = fo_inherited(context, source, "display-align");
    if (value && strcmp(value, "auto")) return value;
    // auto defers to relative-align only on cells/list items; regions use before (XSL 1.1 §7.14).
    value = relative ? fo_inherited(context, source, "relative-align") : nullptr;
    return value ? value : "before";
}

static bool fo_column_proportion(FoTranslationContext* context, DomElement* source, bool* proportional, float* weight,
        const char** base) {
    const char* raw = source->get_attribute("column-width");
    *proportional = false;
    if (!raw) return true;
    Pool* pool = context->document->document_pool;
    struct Tokens {
        Pool* pool; size_t count = 0; CssToken* values;
        Tokens(Pool* owner, const char* text) : pool(owner), values(css_tokenize(text, strlen(text), owner, &count)) {}
        ~Tokens() { if (values) css_token_array_release(pool, values, count); }
    } tokens(pool, raw);
    if (!tokens.values) return fo_failure(context, source, "column-width", "FO column expression allocation failed", TYPESET_OUT_OF_MEMORY);
    bool found = false;
    const char* name = "proportional-column-width"; size_t length = strlen(name);
    for (size_t i = 0; i < tokens.count; i++) found |= tokens.values[i].type == CSS_TOKEN_FUNCTION &&
        tokens.values[i].length == length + 1 && !strncmp(tokens.values[i].start, name, length);
    if (!found) return true;
    double coefficient = 0.0;
    CssValue* expression = fo_expression(context, source, "column-width", raw, &coefficient);
    if (!expression) return false;
    CssValue number = {}; number.type = CSS_VALUE_TYPE_NUMBER; number.data.number.value = coefficient;
    if (!radiant_table_column_proportion(&number, weight))
        return fo_failure(context, source, "column-width", "proportional-column-width requires a finite positive track coefficient");
    CssMathEvaluationContext evaluation = {}; evaluation.preserve_percentages = true;
    CssMathResult computed = css_math_evaluate(expression, &evaluation);
    if (!(computed.resolved && !computed.value && !computed.percentage)) {
        *base = fo_computed_text(context, source, "column-width", expression);
        if (!*base) return false;
        CssDeclaration* declaration = css_parse_property_value_declaration("width", 5, *base, strlen(*base), pool);
        if (!declaration || !declaration->valid || !css_declaration_is_supported(declaration))
            return fo_failure(context, source, "column-width", "proportional column base requires a common CSS length-percentage");
    }
    DomElement* table = source->parent_element();
    const char* layout = table ? table->get_attribute("table-layout") : nullptr;
    if (!fo_element(table, "table") || !layout || strcmp(layout, "fixed"))
        return fo_failure(context, source, "column-width", "proportional-column-width requires fixed table layout");
    *proportional = true;
    return true;
}

static bool fo_style(FoTranslationContext* context, DomElement* source, StrBuf* style, bool proportional_column) {
    FoTableKind table = fo_table_kind(source);
    FoListKind list = fo_list_kind(source);
    bool graphic = fo_graphic(source);
    if (table == FO_TABLE || list == FO_LIST_BLOCK) for (const char* property : {"start-indent", "end-indent"})
        if (!fo_zero_length(context, fo_inherited(context, source, property)))
            return fo_failure(context, source, property, "nonzero table/list indents require common grid reference refinement");
    if (graphic) {
        for (const char* property : {"width", "height"})
            if (!fo_style_property(context, source, style, property, source->get_attribute(property))) return false;
        const char* align = fo_inherited(context, source, "text-align");
        const char* x = !align || !strcmp(align, "start") || !strcmp(align, "left") || !strcmp(align, "justify") ? "0%" :
            !strcmp(align, "center") ? "50%" : !strcmp(align, "end") || !strcmp(align, "right") ? "100%" : nullptr;
        if (!x) return fo_failure(context, source, "text-align", "graphic alignment requires a common logical-axis policy");
        align = fo_display_alignment(context, source, false);
        strbuf_append_str(style, "object-position:"); strbuf_append_str(style, x); strbuf_append_char(style, ' ');
        strbuf_append_str(style, !strcmp(align, "center") ? "50%" : !strcmp(align, "after") ? "100%" : "0%"); strbuf_append_char(style, ';');
    }
    if (table == FO_TABLE) {
        const char* collapse = fo_inherited(context, source, "border-collapse");
        // FO's initial collapsed model differs from HTML; unsupported common geometry stays diagnostic.
        if (!fo_style_property(context, source, style, "border-collapse", collapse ? collapse : "collapse")) return false;
        strbuf_append_str(style, "border-spacing:0;");
        if (!fo_style_property(context, source, style, "table-layout", source->get_attribute("table-layout"))) return false;
        static const char* omissions[] = {"table-omit-header-at-break", "table-omit-footer-at-break"};
        for (const char* property : omissions) if (const char* value = source->get_attribute(property)) {
            bool ignored = false;
            if (!radiant_page_boolean(value, false, &ignored))
                return fo_failure(context, source, property, "FO table furniture omission requires true, false or inherit");
        }
    }
    if (table == FO_TABLE_COLUMN && !proportional_column &&
        !fo_style_property(context, source, style, "width", source->get_attribute("column-width"), "column-width")) return false;
    if (list == FO_LIST_ITEM) strbuf_append_str(style, "table-layout:fixed;width:100%;border-collapse:separate;border-spacing:0;");
    if (table == FO_TABLE_CELL || list == FO_LIST_LABEL || list == FO_LIST_BODY) {
        strbuf_append_str(style, "padding:0;");
        const char* align = fo_display_alignment(context, table == FO_TABLE_CELL ? source : source->parent_element(), true);
        const char* vertical = !strcmp(align, "center") ? "middle" :
            !strcmp(align, "after") ? "bottom" : !strcmp(align, "baseline") ? "baseline" : "top";
        if (!fo_style_property(context, source, style, "vertical-align", vertical)) return false;
    }
    if (table == FO_TABLE || table == FO_TABLE_CELL)
        if (!fo_style_property(context, source, style, "width", source->get_attribute("width"))) return false;
    if (table == FO_TABLE || table == FO_TABLE_ROW || table == FO_TABLE_CELL)
        if (!fo_style_property(context, source, style, "height", source->get_attribute("height"))) return false;
    // translated parents preserve the FO inheritance chain; inherit computed CSS values once.
    for (const char* property : fo_common_properties) {
        // table collapse already has its FO initial/inherited value; CSS inherit has a different initial.
        if ((table == FO_TABLE || list == FO_LIST_ITEM) && !strcmp(property, "border-collapse")) continue;
        if (!fo_style_property(context, source, style, property, source->get_attribute(property))) return false;
    }
    FoStyleBuffer component_name;
    if (!component_name.value) return fo_failure(context, source, nullptr, "FO component allocation failed", TYPESET_OUT_OF_MEMORY);
    // explicit absolute longhands win; relative properties override only shorthand expansion (XSL §5.3.1).
    for (const auto& property : fo_corresponding_properties) {
        const CssProperty* absolute = css_property_get_by_code(radiant_box_side_property(property.family, property.side));
        if (!absolute || !fo_style_property(context, source, style, absolute->name, source->get_attribute(property.relative),
            property.relative, !source->get_attribute(absolute->name))) return false;
        if (fo_conditional_length(property.family)) {
            const char* name = fo_component_name(component_name.value, property.relative, "length");
            const char* length = source->get_attribute(name);
            if (length) {
                if (!fo_style_property(context, source, style, absolute->name, length,
                    pool_strdup(context->document->document_pool, name), !source->get_attribute(absolute->name), true)) return false;
            } else if (!source->get_attribute(property.relative)) {
                name = fo_component_name(component_name.value, property.relative, "conditionality");
                // a specified compound component selects the relative property's initial remaining components (§5.11).
                if (source->get_attribute(name) && !fo_style_property(context, source, style, absolute->name, "initial",
                    property.relative, !source->get_attribute(absolute->name))) return false;
            }
        }
    }
    if (fo_element(source, "simple-page-master") || fo_element(source, "region-body"))
        for (const char* property : fo_margin_properties)
            if (!fo_style_property(context, source, style, property, source->get_attribute(property))) return false;
    if (fo_region_kind(source) != SIZE_MAX || graphic) {
        const char* overflow = source->get_attribute("overflow");
        if (!overflow || !strcmp(overflow, "auto")) overflow = "hidden";
        if (graphic && strcmp(overflow, "hidden") && strcmp(overflow, "visible"))
            return fo_failure(context, source, "overflow", "graphic overflow requires visible or hidden in the initial profile");
        if (!fo_style_property(context, source, style, "overflow", overflow)) return false;
    }
    const char* before = source->get_attribute("break-before"), *after = source->get_attribute("break-after");
    if ((before && strcmp(before, "auto") && strcmp(before, "page")) || (after && strcmp(after, "auto") && strcmp(after, "page")))
        return fo_failure(context, source, "break-before/break-after", "FO break requires common logical folio or column support");
    return fo_style_property(context, source, style, "break-before", before) &&
        fo_style_property(context, source, style, "break-after", after);
}

static bool fo_attribute_admitted(FoTranslationContext* context, DomElement* source, const char* name) {
    if (!strncmp(name, "xmlns", 5) && (!name[5] || name[5] == ':')) return true;
    for (const auto& property : fo_corresponding_properties) if (!strcmp(name, property.relative)) return true;
    if (fo_length_component_name(name)) return true;
    if (fo_decoration_condition_name(name)) {
        const char* value = fo_parent_value(context, source, name, source->get_attribute(name));
        if (!value) return false;
        return !strcmp(value, "discard") || !strcmp(value, "retain") || !strcmp(value, "inherit") ||
            fo_failure(context, source, pool_strdup(context->document->document_pool, name), "FO decoration conditionality requires discard, retain or inherit");
    }
    if (!strcmp(name, "line-stacking-strategy")) {
        static const char* policies[] = {"line-height", "max-height", "font-height", "inherit"};
        const char* value = fo_parent_value(context, source, name, source->get_attribute(name));
        if (!value) return false;
        return fo_property_in(value, policies, sizeof(policies) / sizeof(policies[0])) ||
            fo_failure(context, source, name, "FO line stacking requires an admitted keyword");
    }
    if (!strcmp(name, "display-align") || !strcmp(name, "relative-align")) {
        static const char* display[] = {"auto", "before", "center", "after", "inherit"};
        static const char* relative[] = {"before", "baseline", "inherit"};
        const char* value = fo_parent_value(context, source, name, source->get_attribute(name));
        if (!value) return false;
        bool valid = !strcmp(name, "display-align") ?
            fo_property_in(value, display, sizeof(display) / sizeof(display[0])) :
            fo_property_in(value, relative, sizeof(relative) / sizeof(relative[0]));
        return valid || fo_failure(context, source, name, "FO alignment requires an admitted keyword");
    }
    if (!strcmp(name, "provisional-distance-between-starts") || !strcmp(name, "provisional-label-separation")) return true;
    FoListKind list = fo_list_kind(source);
    if ((list == FO_LIST_LABEL || list == FO_LIST_BODY) && (!strcmp(name, "start-indent") || !strcmp(name, "end-indent"))) {
        bool function = (list == FO_LIST_LABEL) == !strcmp(name, "end-indent");
        const char* value = source->get_attribute(name);
        if (function) return !strcmp(value, list == FO_LIST_LABEL ? "label-end()" : "body-start()") ||
            fo_failure(context, source, name, "list part requires its reference-relative label-end() or body-start() function");
        return fo_zero_length(context, value) ||
            fo_failure(context, source, name, "complementary list indents require zero in the current reference-area profile");
    }
    if ((radiant_flow_trait_name(name) && strcmp(name, "column-proportion") && strcmp(name, "column-number") &&
        strcmp(name, "style-transparent")) || radiant_whitespace_trait_name(name) || radiant_image_trait_name(name)) return true;
    static const char* metadata[] = {"id", "xml:base", "xml:lang", "break-before", "break-after"};
    if (fo_property_in(name, metadata, sizeof(metadata) / sizeof(metadata[0])) ||
        fo_property_in(name, fo_common_properties, sizeof(fo_common_properties) / sizeof(fo_common_properties[0]))) return true;
    if ((fo_element(source, "simple-page-master") || fo_element(source, "region-body")) &&
        fo_property_in(name, fo_margin_properties, sizeof(fo_margin_properties) / sizeof(fo_margin_properties[0]))) return true;
    if (fo_element(source, "simple-page-master") && (!strcmp(name, "master-name") || !strcmp(name, "page-width") || !strcmp(name, "page-height"))) return true;
    size_t region = fo_region_kind(source);
    if (region != SIZE_MAX && !strcmp(name, "region-name")) return true;
    if (region != SIZE_MAX && !strcmp(name, "overflow")) return true;
    if (fo_element(source, "external-graphic") && !strcmp(name, "src")) return true;
    if (fo_graphic(source) && (!strcmp(name, "width") ||
        !strcmp(name, "height") || !strcmp(name, "overflow"))) return true;
    if (region < RADIANT_REGION_EDGE_COUNT && (!strcmp(name, "extent") ||
        (region < RADIANT_REGION_START && !strcmp(name, "precedence")))) return true;
    if ((fo_element(source, "page-sequence") || fo_element(source, "single-page-master-reference") ||
        fo_element(source, "repeatable-page-master-reference") || fo_element(source, "conditional-page-master-reference")) &&
        !strcmp(name, "master-reference")) return true;
    if (fo_element(source, "page-sequence-master") && !strcmp(name, "master-name")) return true;
    if ((fo_element(source, "repeatable-page-master-reference") || fo_element(source, "repeatable-page-master-alternatives")) &&
        !strcmp(name, "maximum-repeats")) return true;
    if (fo_element(source, "conditional-page-master-reference") &&
        (!strcmp(name, "page-position") || !strcmp(name, "odd-or-even") || !strcmp(name, "blank-or-not-blank"))) return true;
    if (fo_element(source, "page-sequence") && (!strcmp(name, "initial-page-number") || !strcmp(name, "force-page-count"))) return true;
    if (fo_element(source, "page-sequence") && fo_property_in(name, fo_number_properties,
        sizeof(fo_number_properties) / sizeof(fo_number_properties[0]))) return true;
    if ((fo_element(source, "page-number-citation") || fo_element(source, "page-number-citation-last")) && !strcmp(name, "ref-id")) return true;
    if (fo_element(source, "page-number-citation-last") && !strcmp(name, "page-citation-strategy")) return true;
    if ((fo_element(source, "flow") || fo_element(source, "static-content")) && !strcmp(name, "flow-name")) return true;
    FoTableKind table = fo_table_kind(source);
    if (table == FO_TABLE_CELL && (!strcmp(name, "starts-row") || !strcmp(name, "ends-row")))
        return fo_table_group(fo_table_kind(source->parent_element())) ||
            fo_failure(context, source, name, "FO row boundaries require a cell directly inside a table group");
    if ((table == FO_TABLE_COLUMN || table == FO_TABLE_CELL) && !strcmp(name, "column-number")) return true;
    if ((table == FO_TABLE || table == FO_TABLE_CELL) && !strcmp(name, "width")) return true;
    if ((table == FO_TABLE || table == FO_TABLE_ROW || table == FO_TABLE_CELL) && !strcmp(name, "height")) return true;
    if (table == FO_TABLE && (!strcmp(name, "table-layout") || !strcmp(name, "table-omit-header-at-break") ||
        !strcmp(name, "table-omit-footer-at-break"))) return true;
    if (table == FO_TABLE_COLUMN && (!strcmp(name, "column-width") || !strcmp(name, "number-columns-repeated"))) return true;
    if (table == FO_TABLE_CELL && (!strcmp(name, "number-columns-spanned") ||
        !strcmp(name, "number-rows-spanned"))) return true;
    return fo_failure(context, source, pool_strdup(context->document->document_pool, name), "FO property requires an unimplemented common Radiant trait");
}

static bool fo_block_children_allowed(DomElement* source) {
    while (fo_element(source, "wrapper")) source = source->parent_element();
    return fo_element(source, "block") || fo_element(source, "flow") || fo_element(source, "static-content") ||
        fo_element(source, "footnote-body") || fo_table_kind(source) == FO_TABLE_CELL ||
        fo_list_kind(source) == FO_LIST_LABEL || fo_list_kind(source) == FO_LIST_BODY;
}

static Item fo_foreign_copy(FoTranslationContext* context, DomElement* source, size_t depth, bool root) {
    if (depth > context->options->max_depth || !fo_translation_charge(context)) {
        fo_failure(context, source, nullptr, "FO translation budget exhausted", TYPESET_BUDGET_EXHAUSTED); return ItemNull;
    }
    ElementBuilder output = context->builder.element(source->tag_name);
    if (root) {
        HashMap* bindings = fo_source_ids_new(16);
        if (!bindings) { fo_failure(context, source, nullptr, "SVG namespace allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
        // make the detached SVG self-contained without mutating the original FO namespace scope.
        for (DomElement* node = source; node; node = node->parent_element()) {
            int count = 0; const char** names = node->attribute_names(&count);
            for (int i = 0; i < count; i++) if (!strcmp(names[i], "xmlns") || !strncmp(names[i], "xmlns:", 6)) {
                FoSourceId key = {names[i], dom_node_ref(node)};
                if (!hashmap_get(bindings, &key)) { hashmap_set(bindings, &key); output.attr(names[i], node->get_attribute(names[i])); }
            }
        }
        bool oom = hashmap_oom(bindings); hashmap_free(bindings);
        if (oom) { fo_failure(context, source, nullptr, "SVG namespace allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    }
    int count = 0; const char** names = source->attribute_names(&count);
    for (int i = 0; i < count; i++) {
        if (root && (!strcmp(names[i], "xmlns") || !strncmp(names[i], "xmlns:", 6))) continue;
        output.attr(names[i], source->get_attribute(names[i]));
    }
    for (DomNode* child = source->first_child; child; child = child->next_sibling) {
        if (child->is_text()) output.child(context->builder.createStringItem(child->as_text()->text, child->as_text()->length));
        else if (child->is_element()) {
            Item copied = fo_foreign_copy(context, child->as_element(), depth + 1, false);
            if (context->result->diagnostic.status != TYPESET_OK) return ItemNull;
            output.child(copied);
        }
    }
    return output.final();
}

static const char* fo_foreign_image(FoTranslationContext* context, DomElement* source, size_t depth) {
    DomElement* svg = nullptr;
    for (DomNode* child = source->first_child; child; child = child->next_sibling) {
        if (child->is_text() && !radiant_page_whitespace(child->as_text())) {
            fo_failure(context, source, nullptr, "instream foreign object requires exactly one SVG root"); return nullptr;
        }
        if (!child->is_element()) continue;
        DomElement* element = child->as_element();
        if (svg || strcmp(element->local_name(), "svg") || strcmp(dom_element_namespace_uri(element), "http://www.w3.org/2000/svg")) {
            fo_failure(context, element, nullptr, "instream foreign object requires exactly one SVG root"); return nullptr;
        }
        svg = element;
    }
    if (!svg) { fo_failure(context, source, nullptr, "instream foreign object requires exactly one SVG root"); return nullptr; }
    Item copied = fo_foreign_copy(context, svg, depth + 1, true);
    if (context->result->diagnostic.status != TYPESET_OK) return nullptr;
    String* xml = format_xml(context->document->document_pool, copied);
    char* encoded = xml ? url_encode_component(xml->chars, xml->len) : nullptr;
    if (!encoded) { fo_failure(context, source, nullptr, "SVG serialization allocation failed", TYPESET_OUT_OF_MEMORY); return nullptr; }
    FoStyleBuffer buffer;
    if (buffer.value) { strbuf_append_str(buffer.value, "data:image/svg+xml,"); strbuf_append_str(buffer.value, encoded); }
    mem_free(encoded);
    const char* result = buffer.value ? pool_strdup(context->document->document_pool, buffer.value->str) : nullptr;
    if (!result) fo_failure(context, source, nullptr, "SVG resource allocation failed", TYPESET_OUT_OF_MEMORY);
    return result;
}

static Item fo_translate_element(FoTranslationContext* context, DomElement* source, size_t depth) {
    if (depth > context->options->max_depth || !fo_translation_charge(context)) {
        fo_failure(context, source, nullptr, "FO translation budget exhausted", TYPESET_BUDGET_EXHAUSTED); return ItemNull;
    }
    FoBindingScope bindings(context);
    if (!bindings.buffer.value) { fo_failure(context, source, nullptr, "FO binding allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    const char* target = nullptr;
    FoTableKind table = fo_table_kind(source);
    FoListKind list = fo_list_kind(source);
    bool graphic = fo_graphic(source);
    bool foreign = fo_element(source, "instream-foreign-object");
    bool inline_content = false;
    if (fo_element(source, "root")) target = "r:page-document";
    else if (fo_element(source, "layout-master-set")) target = "r:master-set";
    else if (fo_element(source, "simple-page-master")) target = "r:page-master";
    else if (fo_element(source, "page-sequence-master")) target = "r:sequence-master";
    else if (fo_element(source, "single-page-master-reference") || fo_element(source, "repeatable-page-master-reference") ||
        fo_element(source, "repeatable-page-master-alternatives")) target = "r:master-run";
    else if (fo_element(source, "conditional-page-master-reference")) target = "r:master-rule";
    else if (fo_region_kind(source) != SIZE_MAX) target = "r:region";
    else if (fo_element(source, "page-sequence")) target = "r:page-sequence";
    else if (fo_element(source, "flow")) target = "r:flow";
    else if (fo_element(source, "static-content")) target = "r:static-content";
    else if (fo_element(source, "footnote")) target = "r:note";
    else if (fo_element(source, "footnote-body")) target = "r:note-body";
    else if (fo_element(source, "page-number")) target = "r:folio";
    else if (fo_element(source, "page-number-citation") || fo_element(source, "page-number-citation-last")) target = "r:folio-ref";
    else if (graphic) target = "img";
    else if (table != FO_TABLE_NONE) target = fo_table_objects[table].target;
    else if (list != FO_LIST_NONE) target = fo_list_objects[list].target;
    else if (fo_element(source, "block")) { target = "div"; inline_content = true; }
    else if (fo_element(source, "inline") || fo_element(source, "wrapper")) {
        target = fo_element(source, "inline") && fo_element(source->parent_element(), "footnote") ? "r:note-call" : "span";
        inline_content = true;
    }
    else { fo_failure(context, source, nullptr, "FO object is outside the implemented translation profile"); return ItemNull; }
    if (fo_element(source, "footnote")) {
        for (DomElement* ancestor = source->parent_element(); ancestor; ancestor = ancestor->parent_element())
            if (fo_element(ancestor, "footnote") || fo_element(ancestor, "float") || fo_element(ancestor, "marker")) {
                fo_failure(context, source, nullptr, "FO footnote cannot descend from a footnote, float or marker"); return ItemNull;
            }
    }
    int attribute_count = 0;
    const char** attributes = source->attribute_names(&attribute_count);
    for (int i = 0; i < attribute_count; i++)
        if (!fo_attribute_admitted(context, source, attributes[i])) return ItemNull;
    if (const char* id = source->get_attribute("id")) {
        FoSourceId entry = {id, dom_node_ref(source)};
        if (!*id || !strcmp(id, "inherit") || hashmap_get(context->ids, &entry)) {
            fo_failure(context, source, "id", "FO IDs must be nonempty, unique and cannot inherit"); return ItemNull;
        }
        hashmap_set(context->ids, &entry);
        if (hashmap_oom(context->ids)) { fo_failure(context, source, "id", "FO ID index allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    }
    bool proportional_column = false; float column_weight = 0.0f; const char* column_base = nullptr;
    if (table == FO_TABLE_COLUMN && !fo_column_proportion(context, source, &proportional_column, &column_weight, &column_base)) return ItemNull;
    ElementBuilder output = context->builder.element(target);
    if (proportional_column) {
        char number[32]; snprintf(number, sizeof(number), "%.9g", (double)column_weight);
        output.attr("r:column-proportion", number);
    }
    if (fo_element(source, "block") || list == FO_LIST_BLOCK) output.attr("r:block-inline-geometry", "reference");
    if (list == FO_LIST_BLOCK) output.attr("r:label-body-context", "true");
    if (list == FO_LIST_ITEM) output.attr("r:label-body-grid", "true");
    if (list == FO_LIST_LABEL || list == FO_LIST_BODY) {
        output.attr("r:area-source", "descendants");
        const char* name = list == FO_LIST_LABEL ? "end-indent" : "start-indent";
        if (!source->get_attribute(name)) {
            fo_failure(context, source, name, "list label/body requires explicit label-end() and body-start() to avoid overlapping content rectangles"); return ItemNull;
        }
    }
    for (const char* property : {"text-altitude", "text-depth"}) if (!source->get_attribute(property)) {
        FoStyleBuffer name;
        if (!name.value) { fo_failure(context, source, property, "FO metric allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
        strbuf_append_str(name.value, "r:"); strbuf_append_str(name.value, property); output.attr(name.value->str, "use-font-metrics");
    }
    if (graphic) {
        const char* resource = nullptr;
        if (foreign) resource = fo_foreign_image(context, source, depth);
        else {
            const char* raw = source->get_attribute("src");
            CssDeclaration* declaration = raw ? css_parse_property_value_declaration("background-image", 16,
                raw, strlen(raw), context->document->document_pool) : nullptr;
            const CssValue* value = declaration ? declaration->value : nullptr;
            if (!value || value->type != CSS_VALUE_TYPE_URL || !value->data.url || !*value->data.url) {
                fo_failure(context, source, "src", "external graphic requires one nonempty url() resource"); return ItemNull;
            }
            resource = value->data.url;
        }
        if (!resource) return ItemNull;
        output.attr("src", resource);
        if (!source->get_attribute("content-width")) output.attr("r:content-width", "auto");
        if (!source->get_attribute("content-height")) output.attr("r:content-height", "auto");
        if (!source->get_attribute("scaling")) output.attr("r:scaling", "uniform");
    }
    if (table == FO_TABLE_CELL || list == FO_LIST_LABEL || list == FO_LIST_BODY) {
        DomElement* aligned = table == FO_TABLE_CELL ? source : source->parent_element();
        const char* display = fo_inherited(context, aligned, "display-align");
        if ((!display || !strcmp(display, "auto")) && !strcmp(fo_display_alignment(context, aligned, true), "before"))
            output.attr("r:cell-alignment", "relative-before");
    }
    static const struct { FoTableKind kind; const char* source; const char* target; uint32_t maximum; } counts[] = {
        {FO_TABLE_COLUMN, "column-number", "r:column-number", 1000},
        {FO_TABLE_CELL, "column-number", "r:column-number", 1000},
        {FO_TABLE_COLUMN, "number-columns-repeated", "span", 1000},
        {FO_TABLE_CELL, "number-columns-spanned", "colspan", 1000},
        {FO_TABLE_CELL, "number-rows-spanned", "rowspan", 65534}
    };
    for (const auto& count : counts) if (table == count.kind) if (const char* raw = source->get_attribute(count.source)) {
        CssValue* expression = fo_expression(context, source, count.source, raw);
        if (!expression) return ItemNull;
        CssMathEvaluationContext evaluation = {};
        CssMathResult computed = css_math_evaluate(expression, &evaluation);
        uint32_t value = 0;
        if (computed.type != CSS_MATH_NUMBER || !computed.resolved ||
            !radiant_page_rounded_count(computed.value, 1, &value) || value > count.maximum) {
            fo_failure(context, source, count.source, "FO table count exceeds the admitted HTML grid domain"); return ItemNull;
        }
        char number[16]; snprintf(number, sizeof(number), "%u", value); output.attr(count.target, number);
    }
    FoStyleBuffer buffer;
    StrBuf* style = buffer.value;
    if (!style) { fo_failure(context, source, nullptr, "FO style allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    if (fo_element(source, "root")) {
        output.attr("xmlns:r", RADIANT_PAGE_NAMESPACE);
        if (!source->get_attribute("line-stacking-strategy")) output.attr("r:line-stacking-strategy", "max-height");
        output.attr("r:linefeed-treatment", "treat-as-space").attr("r:white-space-treatment", "ignore-if-surrounding-linefeed")
            .attr("r:white-space-collapse", "true").attr("r:wrap-option", "wrap");
        strbuf_append_str(style, "margin:0; font-family:serif; font-size:12pt; line-height:normal; color:black; white-space:normal;");
    }
    if (!fo_style(context, source, style, proportional_column)) return ItemNull;
    if (column_base) fo_style_append(style, "width", column_base);
    // common typed traits retain ranges/strengths instead of approximating them with CSS margins/avoid.
    FoStyleBuffer attribute_buffer;
    if (!attribute_buffer.value) { fo_failure(context, source, nullptr, "FO trait allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    // FO inherits the whole keep-together compound; native authors select inheritance explicitly.
    if (!source->get_attribute("keep-together")) output.attr("r:keep-together", "inherit");
    // FO's discard initial is explicit; native CSS authors continue to use box-decoration-break.
    for (const auto& property : fo_corresponding_properties) if (fo_conditional_length(property.family) &&
        (property.side == CSS_BOX_SIDE_TOP || property.side == CSS_BOX_SIDE_BOTTOM)) {
        const CssProperty* absolute = css_property_get_by_code(radiant_box_side_property(property.family, property.side));
        const char* raw = source->get_attribute(property.relative);
        const char* shorthand = fo_parent_value(context, source, property.relative, raw);
        if (raw && !shorthand) return ItemNull;
        const char* condition_name = fo_component_name(attribute_buffer.value, property.relative, "conditionality");
        raw = source->get_attribute(condition_name);
        const char* owned_name = raw ? pool_strdup(context->document->document_pool, condition_name) : condition_name;
        if (!owned_name) { fo_failure(context, source, property.relative, "FO component allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
        const char* conditionality = fo_parent_value(context, source, owned_name, raw);
        if (raw && !conditionality) return ItemNull;
        if (source->get_attribute(absolute->name)) conditionality = "discard";
        else if (!conditionality) conditionality = shorthand && !strcmp(shorthand, "inherit") ? "inherit" : "discard";
        output.attr(fo_component_name(attribute_buffer.value, property.relative, "conditionality", true), conditionality);
    }
    for (int i = 0; i < attribute_count; i++) if (radiant_flow_trait_name(attributes[i]) || radiant_whitespace_trait_name(attributes[i]) ||
        radiant_image_trait_name(attributes[i]) || radiant_label_body_trait_name(attributes[i])) {
        if (!strcmp(attributes[i], "column-number")) continue;
        // corresponding absolute properties replace every compound component, including conditionality (§5.11).
        if (fo_decoration_condition_name(attributes[i])) continue;
        // list-part functions have already become independent grid tracks, rather than inherited block lengths.
        if ((list == FO_LIST_LABEL || list == FO_LIST_BODY) &&
            (!strcmp(attributes[i], "start-indent") || !strcmp(attributes[i], "end-indent"))) continue;
        strbuf_reset(attribute_buffer.value); strbuf_append_str(attribute_buffer.value, "r:"); strbuf_append_str(attribute_buffer.value, attributes[i]);
        const char* authored = source->get_attribute(attributes[i]);
        const char* value = fo_parent_value(context, source, attributes[i], authored);
        if (!value) return ItemNull;
        // keyword traits remain keywords; numeric FO syntax enters the same typed native trait path.
        const char* first = value + strspn(value, " \t\r\n");
        bool scales = !strcmp(attributes[i], "allowed-width-scale") || !strcmp(attributes[i], "allowed-height-scale");
        if (scales && strcmp(first, "inherit") && strncmp(first, "var(", 4)) {
            value = fo_expression_text(context, source, attributes[i], value, FO_EXPRESSION_SCALES);
            if (!value) return ItemNull;
        } else if ((*first >= '0' && *first <= '9') || *first == '.' || *first == '-' || *first == '+' ||
            (strchr(value, '(') && !(value != authored && !strncmp(first, "var(", 4)))) {
            value = fo_expression_text(context, source, attributes[i], value);
            if (!value) return ItemNull;
        }
        output.attr(attribute_buffer.value->str, value);
    }
    const char* id = source->get_attribute("id"); if (id) output.attr("id", id);
    const char* language = fo_inherited(context, source, "xml:lang"); if (language) output.attr("lang", language);
    const char* base = source->get_attribute("xml:base"); if (base) output.attr("xml:base", base);
    bool single = fo_element(source, "single-page-master-reference");
    bool repeated = fo_element(source, "repeatable-page-master-reference");
    bool conditional = fo_element(source, "conditional-page-master-reference");
    if (fo_element(source, "page-sequence-master")) {
        const char* name = source->get_attribute("master-name");
        if (!name || !*name) { fo_failure(context, source, "master-name", "FO sequence master requires a name"); return ItemNull; }
        output.attr("name", name);
    }
    if (single || repeated || conditional) {
        const char* reference = source->get_attribute("master-reference");
        if (!reference || !*reference) { fo_failure(context, source, "master-reference", "FO master rule requires a page master reference"); return ItemNull; }
        if (conditional) output.attr("master-reference", reference);
        else output.child(context->builder.element("r:master-rule").attr("master-reference", reference).final());
    }
    static const char* sequence_properties[] = {"initial-page-number", "force-page-count"};
    static const char* predicate_properties[] = {"page-position", "odd-or-even", "blank-or-not-blank"};
    if (fo_element(source, "page-sequence")) for (const char* property : sequence_properties)
        if (const char* value = source->get_attribute(property)) output.attr(property, value);
    if (fo_element(source, "page-sequence")) for (const char* property : fo_number_properties)
        if (const char* value = source->get_attribute(property)) output.attr(property, value);
    bool query = fo_element(source, "page-number") || fo_element(source, "page-number-citation") || fo_element(source, "page-number-citation-last");
    if (query && !fo_element(source, "page-number")) {
        const char* reference = source->get_attribute("ref-id");
        if (!reference || !*reference || !strcmp(reference, "inherit")) {
            fo_failure(context, source, "ref-id", "FO page citation requires a target ID"); return ItemNull;
        }
        output.attr("ref-id", reference);
        bool last = fo_element(source, "page-number-citation-last");
        output.attr("edge", last ? "last" : "first");
        const char* strategy = last ? source->get_attribute("page-citation-strategy") : "normal";
        if (strategy) output.attr("area", strategy);
    }
    if (conditional) for (const char* property : predicate_properties)
        if (const char* value = source->get_attribute(property)) output.attr(property, value);
    if (single) output.attr("maximum-repeats", "1");
    else if (repeated || fo_element(source, "repeatable-page-master-alternatives"))
        if (const char* value = source->get_attribute("maximum-repeats")) output.attr("maximum-repeats", value);
    if (fo_element(source, "simple-page-master")) {
        const char* name = source->get_attribute("master-name");
        const char* width = source->get_attribute("page-width"), *height = source->get_attribute("page-height");
        if (!name || !*name || !width || !height) {
            fo_failure(context, source, "master-name/page-width/page-height", "FO initial profile requires a named master with explicit sheet dimensions"); return ItemNull;
        }
        output.attr("name", name);
        if (!fo_sheet_size(context, source, style, width, height)) return ItemNull;
    }
    size_t region_kind = fo_region_kind(source);
    if (region_kind != SIZE_MAX) {
        output.attr("role", fo_region_roles[region_kind]);
        output.attr("box-policy", "zero-border-padding");
        output.attr("display-align", fo_display_alignment(context, source, false));
        const char* name = source->get_attribute("region-name");
        strbuf_reset(attribute_buffer.value); strbuf_append_str(attribute_buffer.value, "xsl-"); strbuf_append_str(attribute_buffer.value, fo_region_names[region_kind]);
        output.attr("name", name ? name : attribute_buffer.value->str);
        if (region_kind < RADIANT_REGION_EDGE_COUNT) for (const char* property : fo_region_properties)
            if (const char* value = source->get_attribute(property)) output.attr(property, value);
    }
    if (style->length) output.attr("style", style->str);
    if (bindings.buffer.value->length) output.attr("r:property-bindings", bindings.buffer.value->str);
    if (fo_element(source, "page-sequence")) {
        const char* master = source->get_attribute("master-reference");
        if (!master || !*master) { fo_failure(context, source, "master-reference", "FO page sequence requires a master reference"); return ItemNull; }
        output.attr("master-reference", master);
    }
    if (fo_element(source, "flow") || fo_element(source, "static-content")) {
        const char* name = source->get_attribute("flow-name");
        if (!name || !*name) { fo_failure(context, source, "flow-name", "FO flow requires a region binding"); return ItemNull; }
        output.attr("region-name", name);
    }
    size_t element_count = 0;
    bool seen_master_set = false;
    size_t previous_region = 0; bool seen_flow = false;
    unsigned table_stage = 0;
    FoTableKind group_children = FO_TABLE_NONE;
    lam::ArrayList<Item> row_cells(MEM_CAT_CONTAINER, 0);
    auto finish_row = [&]() -> bool {
        if (row_cells.empty()) return true;
        Item row = fo_transparent_row(context, source, row_cells.data(), row_cells.size());
        if (context->result->diagnostic.status != TYPESET_OK) return false;
        output.child(row); row_cells.clear(); return true;
    };
    Item list_parts[2] = {};
    for (DomNode* child = foreign ? nullptr : source->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (inline_content) output.child(context->builder.createStringItem(child->as_text()->text, child->as_text()->length));
            else if (!radiant_page_whitespace(child->as_text())) { fo_failure(context, source, nullptr, "FO structural object contains character content"); return ItemNull; }
            continue;
        }
        if (!child->is_element()) continue;
        DomElement* element = child->as_element(); element_count++;
        bool admitted = inline_content && (fo_element(element, "inline") || fo_element(element, "wrapper") ||
            fo_element(element, "footnote") || fo_graphic(element) ||
            fo_element(element, "page-number") || fo_element(element, "page-number-citation") || fo_element(element, "page-number-citation-last") ||
            (fo_block_object(element) && fo_block_children_allowed(source)));
        if (fo_element(source, "root")) {
            admitted = (!seen_master_set && element_count == 1 && fo_element(element, "layout-master-set")) ||
                (seen_master_set && fo_element(element, "page-sequence"));
            if (fo_element(element, "layout-master-set")) seen_master_set = true;
        } else if (fo_element(source, "layout-master-set")) admitted = fo_element(element, "simple-page-master") || fo_element(element, "page-sequence-master");
        else if (fo_element(source, "page-sequence-master")) admitted = fo_element(element, "single-page-master-reference") ||
            fo_element(element, "repeatable-page-master-reference") || fo_element(element, "repeatable-page-master-alternatives");
        else if (fo_element(source, "repeatable-page-master-alternatives")) admitted = fo_element(element, "conditional-page-master-reference");
        else if (fo_element(source, "simple-page-master")) {
            size_t child_region = fo_region_kind(element);
            size_t order = child_region == RADIANT_REGION_BODY ? 0 : child_region + 1;
            admitted = child_region != SIZE_MAX && (element_count == 1 ? !order : order > previous_region);
            previous_region = order;
        } else if (fo_element(source, "page-sequence")) {
            admitted = !seen_flow && (fo_element(element, "static-content") || fo_element(element, "flow"));
            if (fo_element(element, "flow")) seen_flow = true;
        } else if (fo_element(source, "flow") || fo_element(source, "static-content") || fo_element(source, "footnote-body") ||
            table == FO_TABLE_CELL || list == FO_LIST_LABEL || list == FO_LIST_BODY)
            admitted = fo_block_object(element);
        else if (list == FO_LIST_BLOCK) admitted = fo_list_kind(element) == FO_LIST_ITEM;
        else if (list == FO_LIST_ITEM) admitted = (element_count == 1 && fo_list_kind(element) == FO_LIST_LABEL) ||
            (element_count == 2 && fo_list_kind(element) == FO_LIST_BODY);
        else if (table == FO_TABLE) {
            FoTableKind child_kind = fo_table_kind(element);
            admitted = (child_kind == FO_TABLE_COLUMN && table_stage == 0) ||
                (child_kind == FO_TABLE_HEADER && table_stage == 0) ||
                (child_kind == FO_TABLE_FOOTER && table_stage <= 1) || child_kind == FO_TABLE_BODY;
            if (child_kind == FO_TABLE_HEADER) table_stage = 1;
            else if (child_kind == FO_TABLE_FOOTER) table_stage = 2;
            else if (child_kind == FO_TABLE_BODY) table_stage = 3;
        } else if (fo_table_group(table)) {
            FoTableKind kind = fo_table_kind(element);
            admitted = (kind == FO_TABLE_ROW || kind == FO_TABLE_CELL) &&
                (group_children == FO_TABLE_NONE || group_children == kind);
            if (admitted) group_children = kind;
        }
        else if (table == FO_TABLE_ROW) admitted = fo_table_kind(element) == FO_TABLE_CELL;
        else if (fo_element(source, "footnote")) admitted = (element_count == 1 && fo_element(element, "inline")) ||
            (element_count == 2 && fo_element(element, "footnote-body"));
        if (!admitted) { fo_failure(context, element, nullptr, "FO object is invalid in this parent or requires an unimplemented translation"); return ItemNull; }
        bool ends_row = false;
        if (group_children == FO_TABLE_CELL) {
            bool starts_row = false;
            for (const auto& boundary : {"starts-row", "ends-row"}) if (const char* raw = element->get_attribute(boundary)) {
                const char* value = fo_parent_value(context, element, boundary, raw);
                if (!value) return ItemNull;
                bool* selected = !strcmp(boundary, "starts-row") ? &starts_row : &ends_row;
                if (!radiant_page_boolean(value, false, selected)) {
                    fo_failure(context, element, boundary, "FO row boundaries require true, false or inherited false"); return ItemNull;
                }
            }
            if (starts_row && !finish_row()) return ItemNull;
        }
        Item translated = fo_translate_element(context, element, depth + 1);
        if (context->result->diagnostic.status != TYPESET_OK) return ItemNull;
        if (list == FO_LIST_ITEM) list_parts[element_count - 1] = translated;
        else if (group_children == FO_TABLE_CELL) {
            if (!row_cells.append(translated)) {
                fo_failure(context, element, nullptr, "FO row grouping allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull;
            }
            if (ends_row && !finish_row()) return ItemNull;
        }
        else output.child(translated);
    }
    if (!finish_row()) return ItemNull;
    if ((fo_element(source, "root") && element_count < 2) ||
        (fo_element(source, "page-sequence") && !seen_flow) ||
        (fo_element(source, "footnote") && element_count != 2) ||
        (list == FO_LIST_ITEM && element_count != 2) ||
        (table == FO_TABLE && table_stage != 3) ||
        ((!inline_content && region_kind == SIZE_MAX && !single && !repeated && !conditional && !query && !graphic && table != FO_TABLE_COLUMN) && !element_count)) {
        fo_failure(context, source, nullptr, "FO object is missing required child content"); return ItemNull;
    }
    if (list == FO_LIST_ITEM) {
        Item row = fo_transparent_row(context, source, list_parts, 2);
        if (context->result->diagnostic.status != TYPESET_OK) return ItemNull;
        output.child(row);
    }
    Item translated = output.final();
    RadiantFoOrigin* origin = (RadiantFoOrigin*)pool_calloc(context->document->document_pool, sizeof(RadiantFoOrigin));
    if (!origin) { fo_failure(context, source, nullptr, "FO provenance allocation failed", TYPESET_OUT_OF_MEMORY); return ItemNull; }
    origin->source = dom_node_ref(source); origin->translated = translated.element; origin->qname = source->tag_name;
    const RadiantFoSourceSpan* span = fo_source_span(context, source);
    if (span) { origin->start = span->start; origin->end = span->end; origin->has_range = true; }
    origin->next = context->result->origins; context->result->origins = origin;
    return translated;
}

RadiantFoTranslation* radiant_fo_translate(DomDocument* owner, DomElement* source, const RadiantFoOptions* options) {
    if (!owner || !source || !options || !owner->input || !options->max_nodes || !options->max_depth) return nullptr;
    RadiantFoTranslation* result = (RadiantFoTranslation*)pool_calloc(owner->document_pool, sizeof(RadiantFoTranslation));
    if (!result) return nullptr;
    FoTranslationContext context(owner, result, options);
    context.ids = fo_source_ids_new(16);
    if (!context.ids) { fo_failure(&context, source, nullptr, "FO ID index allocation failed", TYPESET_OUT_OF_MEMORY); return result; }
    if (options->spans) {
        context.spans = fo_source_spans_new(16);
        if (!context.spans) { fo_failure(&context, source, nullptr, "FO source index allocation failed", TYPESET_OUT_OF_MEMORY); return result; }
        size_t count = 0;
        for (const RadiantFoSourceSpan* span = options->spans; span; span = span->next) {
            if (++count > options->max_nodes) { fo_failure(&context, source, nullptr, "FO source index budget exhausted", TYPESET_BUDGET_EXHAUSTED); return result; }
            hashmap_set(context.spans, span);
            if (hashmap_oom(context.spans)) { fo_failure(&context, source, nullptr, "FO source index allocation failed", TYPESET_OUT_OF_MEMORY); return result; }
        }
    }
    if (!css_property_system_init(owner->document_pool)) {
        fo_failure(&context, source, nullptr, "common CSS property initialization failed", TYPESET_OUT_OF_MEMORY); return result;
    }
    if (source->doc.get() != owner || !fo_element(source, "root")) {
        fo_failure(&context, source, nullptr, "FO input must have a root in the XSL formatting namespace"); return result;
    }
    Item translated = fo_translate_element(&context, source, 0);
    if (result->diagnostic.status == TYPESET_OK) result->root = translated.element;
    return result;
}
