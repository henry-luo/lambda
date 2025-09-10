#include "math_layout.h"
#include "../../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Standard mathematical constants
#define MATH_PI 3.14159265358979323846
#define MATH_E  2.71828182845904523536

// Standard math style scaling factors
static const double style_scale_factors[] = {
    1.0,    // DISPLAY
    1.0,    // TEXT  
    0.7,    // SCRIPT
    0.5     // SCRIPTSCRIPT
};

// Mathematical spacing amounts (in em units)
static const double math_spacing_table[8][8] = {
    // ORD  OP   BIN  REL  OPEN CLOSE PUNCT INNER
    {0.0, 0.2, 0.3, 0.3, 0.0, 0.0,  0.0,  0.2}, // ORD
    {0.2, 0.2, 0.0, 0.3, 0.0, 0.0,  0.0,  0.2}, // OP
    {0.3, 0.3, 0.0, 0.0, 0.3, 0.0,  0.0,  0.3}, // BIN
    {0.3, 0.3, 0.3, 0.0, 0.3, 0.0,  0.0,  0.3}, // REL
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0,  0.0,  0.0}, // OPEN
    {0.0, 0.2, 0.3, 0.3, 0.0, 0.0,  0.0,  0.2}, // CLOSE
    {0.2, 0.2, 0.0, 0.2, 0.2, 0.2,  0.2,  0.2}, // PUNCT
    {0.2, 0.2, 0.3, 0.3, 0.2, 0.0,  0.2,  0.2}  // INNER
};

// Main math layout function
ViewNode* layout_math_expression(ViewNode* math_node, MathLayoutContext* ctx) {
    if (!math_node || !ctx) return NULL;
    
    if (math_node->type != VIEW_NODE_MATH_ELEMENT) {
        log_error("layout_math_expression: Expected math element node");
        return NULL;
    }
    
    ViewMathElement* math_elem = math_node->content.math_elem;
    if (!math_elem) return NULL;
    
    // Dispatch to specific layout function based on math element type
    switch (math_elem->type) {
        case VIEW_MATH_ATOM:
            return layout_math_atom(math_node, ctx);
        case VIEW_MATH_FRACTION:
            return layout_math_fraction(math_node, ctx);
        case VIEW_MATH_SUPERSCRIPT:
        case VIEW_MATH_SUBSCRIPT:
            return layout_math_script(math_node, ctx, math_elem->type == VIEW_MATH_SUPERSCRIPT);
        case VIEW_MATH_RADICAL:
            return layout_math_radical(math_node, ctx);
        case VIEW_MATH_MATRIX:
            return layout_math_matrix(math_node, ctx);
        case VIEW_MATH_DELIMITER:
            return layout_math_delimiter(math_node, ctx);
        case VIEW_MATH_FUNCTION:
            return layout_math_function(math_node, ctx);
        case VIEW_MATH_OPERATOR:
            return layout_math_operator(math_node, ctx);
        case VIEW_MATH_ACCENT:
            return layout_math_accent(math_node, ctx);
        case VIEW_MATH_UNDEROVER:
            return layout_math_underover(math_node, ctx);
        case VIEW_MATH_SPACING:
            return layout_math_spacing(math_node, ctx);
        default:
            log_error("layout_math_expression: Unknown math element type %d", math_elem->type);
            return NULL;
    }
}

// Layout math atom (symbol/variable)
static ViewNode* layout_math_atom(ViewNode* atom_node, MathLayoutContext* ctx) {
    if (!atom_node || !ctx) return NULL;
    
    ViewMathElement* math_elem = atom_node->content.math_elem;
    if (!math_elem || math_elem->type != VIEW_MATH_ATOM) return NULL;
    
    // Get symbol and unicode from content
    const char* symbol = math_elem->content.atom.symbol;
    const char* unicode = math_elem->content.atom.unicode;
    
    if (!symbol) return NULL;
    
    // Create text run for the symbol
    ViewNode* text_node = view_node_create(VIEW_NODE_TEXT_RUN);
    if (!text_node) return NULL;
    
    // Allocate text run structure
    text_node->content.text_run = calloc(1, sizeof(ViewTextRun));
    if (!text_node->content.text_run) {
        free(text_node);
        return NULL;
    }
    
    ViewTextRun* text_run = text_node->content.text_run;
    
    // Use unicode if available, otherwise use symbol
    const char* display_text = unicode ? unicode : symbol;
    text_run->text = strdup(display_text);
    text_run->text_length = strlen(display_text);
    text_run->font = ctx->math_font;
    text_run->font_size = ctx->metrics.font_size;
    text_run->color.r = 0.0; text_run->color.g = 0.0; text_run->color.b = 0.0; text_run->color.a = 1.0;
    
    // Calculate dimensions
    if (ctx->math_font) {
        uint32_t glyph_id = get_math_glyph(ctx->math_font, display_text);
        text_run->total_width = get_glyph_width(ctx->math_font, glyph_id);
        text_run->ascent = get_glyph_height(ctx->math_font, glyph_id);
        text_run->descent = get_glyph_depth(ctx->math_font, glyph_id);
    } else {
        // Fallback dimensions
        text_run->total_width = ctx->metrics.font_size * 0.6;
        text_run->ascent = ctx->metrics.font_size * 0.7;
        text_run->descent = ctx->metrics.font_size * 0.2;
    }
    
    // Set node dimensions
    text_node->size.width = text_run->total_width;
    text_node->size.height = text_run->ascent + text_run->descent;
    text_node->bounds.size = text_node->size;
    
    return text_node;
}

// Layout math fraction
ViewNode* layout_math_fraction(ViewNode* fraction_node, MathLayoutContext* ctx) {
    if (!fraction_node || !ctx) return NULL;
    
    ViewMathElement* math_elem = fraction_node->content.math_elem;
    if (!math_elem || math_elem->type != VIEW_MATH_FRACTION) return NULL;
    
    ViewNode* numerator = math_elem->content.fraction.numerator;
    ViewNode* denominator = math_elem->content.fraction.denominator;
    
    if (!numerator || !denominator) return NULL;
    
    // Create smaller context for numerator and denominator
    MathLayoutContext* smaller_ctx = math_layout_context_copy(ctx);
    apply_math_style_scaling(smaller_ctx, get_smaller_style(ctx->style));
    
    // Layout numerator and denominator
    ViewNode* num_layout = layout_math_expression(numerator, smaller_ctx);
    ViewNode* denom_layout = layout_math_expression(denominator, smaller_ctx);
    
    if (!num_layout || !denom_layout) {
        math_layout_context_destroy(smaller_ctx);
        return NULL;
    }
    
    // Calculate fraction dimensions
    double num_width = num_layout->size.width;
    double denom_width = denom_layout->size.width;
    double max_width = fmax(num_width, denom_width);
    
    double line_thickness = ctx->metrics.frac_line_thickness;
    double num_shift = ctx->metrics.num_shift;
    double denom_shift = ctx->metrics.denom_shift;
    
    // Create container for fraction
    ViewNode* container = view_node_create(VIEW_NODE_GROUP);
    if (!container) {
        math_layout_context_destroy(smaller_ctx);
        return NULL;
    }
    
    // Position numerator (centered above line)
    num_layout->position.x = (max_width - num_width) / 2.0;
    num_layout->position.y = -(num_shift + num_layout->size.height);
    view_node_add_child(container, num_layout);
    
    // Position denominator (centered below line)
    denom_layout->position.x = (max_width - denom_width) / 2.0;
    denom_layout->position.y = denom_shift;
    view_node_add_child(container, denom_layout);
    
    // Create fraction line
    ViewNode* line = view_node_create(VIEW_NODE_LINE);
    if (line) {
        line->content.geometry = calloc(1, sizeof(ViewGeometry));
        if (line->content.geometry) {
            line->content.geometry->type = VIEW_GEOM_LINE;
            line->content.geometry->stroke_width = line_thickness;
            line->content.geometry->color.r = 0.0; line->content.geometry->color.g = 0.0; 
            line->content.geometry->color.b = 0.0; line->content.geometry->color.a = 1.0;
        }
        line->position.x = 0;
        line->position.y = 0;
        line->size.width = max_width;
        line->size.height = line_thickness;
        view_node_add_child(container, line);
    }
    
    // Set container dimensions
    container->size.width = max_width;
    container->size.height = num_shift + num_layout->size.height + denom_shift + denom_layout->size.height;
    container->bounds.size = container->size;
    
    math_layout_context_destroy(smaller_ctx);
    return container;
}

// Layout math script (superscript/subscript)
ViewNode* layout_math_script(ViewNode* script_node, MathLayoutContext* ctx, bool is_superscript) {
    if (!script_node || !ctx) return NULL;
    
    ViewMathElement* math_elem = script_node->content.math_elem;
    if (!math_elem) return NULL;
    
    ViewNode* base = math_elem->content.script.base;
    ViewNode* script = math_elem->content.script.script;
    
    if (!base || !script) return NULL;
    
    // Layout base with current context
    ViewNode* base_layout = layout_math_expression(base, ctx);
    if (!base_layout) return NULL;
    
    // Create script context (smaller style)
    MathLayoutContext* script_ctx = math_layout_context_copy(ctx);
    apply_math_style_scaling(script_ctx, is_superscript ? 
                           get_superscript_style(ctx->style) : 
                           get_subscript_style(ctx->style));
    
    // Layout script
    ViewNode* script_layout = layout_math_expression(script, script_ctx);
    if (!script_layout) {
        math_layout_context_destroy(script_ctx);
        return NULL;
    }
    
    // Create container
    ViewNode* container = view_node_create(VIEW_NODE_GROUP);
    if (!container) {
        math_layout_context_destroy(script_ctx);
        return NULL;
    }
    
    // Position base at origin
    base_layout->position.x = 0;
    base_layout->position.y = 0;
    view_node_add_child(container, base_layout);
    
    // Position script
    script_layout->position.x = base_layout->size.width;
    if (is_superscript) {
        script_layout->position.y = -(ctx->metrics.sup_shift + script_layout->size.height * 0.8);
    } else {
        script_layout->position.y = ctx->metrics.sub_shift;
    }
    view_node_add_child(container, script_layout);
    
    // Set container dimensions
    container->size.width = base_layout->size.width + script_layout->size.width;
    container->size.height = fmax(base_layout->size.height, 
                                 abs(script_layout->position.y) + script_layout->size.height);
    container->bounds.size = container->size;
    
    math_layout_context_destroy(script_ctx);
    return container;
}

// Calculate math spacing between two math classes
double calculate_math_spacing(enum ViewMathClass left, enum ViewMathClass right, enum ViewMathStyle style) {
    if (left >= VIEW_MATH_INNER || right >= VIEW_MATH_INNER) return 0.0;
    
    double base_spacing = math_spacing_table[left][right];
    
    // Reduce spacing for script styles
    if (style == VIEW_MATH_SCRIPT || style == VIEW_MATH_SCRIPTSCRIPT) {
        base_spacing *= 0.7;
    }
    
    return base_spacing;
}

// Calculate math metrics for a given font and style
MathMetrics calculate_math_metrics(ViewFont* font, enum ViewMathStyle style) {
    MathMetrics metrics = {0};
    
    // Base font size depends on style
    double base_size = font ? 12.0 : 12.0; // Default 12pt
    metrics.font_size = base_size * style_scale_factors[style];
    
    // Calculate derived metrics
    metrics.axis_height = metrics.font_size * 0.25;
    metrics.x_height = metrics.font_size * 0.5;
    metrics.sup_shift = metrics.font_size * 0.4;
    metrics.sub_shift = metrics.font_size * 0.2;
    metrics.num_shift = metrics.font_size * 0.3;
    metrics.denom_shift = metrics.font_size * 0.3;
    metrics.frac_line_thickness = metrics.font_size * 0.04;
    metrics.radical_rule_thickness = metrics.font_size * 0.04;
    metrics.default_rule_thickness = metrics.font_size * 0.04;
    
    return metrics;
}

// Style utility functions
enum ViewMathStyle get_smaller_style(enum ViewMathStyle style) {
    switch (style) {
        case VIEW_MATH_DISPLAY: return VIEW_MATH_TEXT;
        case VIEW_MATH_TEXT: return VIEW_MATH_SCRIPT;
        case VIEW_MATH_SCRIPT: return VIEW_MATH_SCRIPTSCRIPT;
        case VIEW_MATH_SCRIPTSCRIPT: return VIEW_MATH_SCRIPTSCRIPT;
        default: return VIEW_MATH_TEXT;
    }
}

enum ViewMathStyle get_superscript_style(enum ViewMathStyle style) {
    switch (style) {
        case VIEW_MATH_DISPLAY: return VIEW_MATH_SCRIPT;
        case VIEW_MATH_TEXT: return VIEW_MATH_SCRIPT;
        case VIEW_MATH_SCRIPT: return VIEW_MATH_SCRIPTSCRIPT;
        case VIEW_MATH_SCRIPTSCRIPT: return VIEW_MATH_SCRIPTSCRIPT;
        default: return VIEW_MATH_SCRIPT;
    }
}

enum ViewMathStyle get_subscript_style(enum ViewMathStyle style) {
    return get_superscript_style(style); // Same as superscript
}

bool is_display_style(enum ViewMathStyle style) {
    return style == VIEW_MATH_DISPLAY;
}

double get_style_scale_factor(enum ViewMathStyle from_style, enum ViewMathStyle to_style) {
    return style_scale_factors[to_style] / style_scale_factors[from_style];
}

// Layout context management
MathLayoutContext* math_layout_context_create(ViewFont* math_font, ViewFont* text_font, enum ViewMathStyle style) {
    MathLayoutContext* ctx = calloc(1, sizeof(MathLayoutContext));
    if (!ctx) return NULL;
    
    ctx->style = style;
    ctx->cramped = false;
    ctx->scale_factor = 1.0;
    ctx->math_font = math_font;
    ctx->text_font = text_font;
    ctx->metrics = calculate_math_metrics(math_font, style);
    
    return ctx;
}

MathLayoutContext* math_layout_context_copy(MathLayoutContext* ctx) {
    if (!ctx) return NULL;
    
    MathLayoutContext* copy = malloc(sizeof(MathLayoutContext));
    if (!copy) return NULL;
    
    *copy = *ctx; // Shallow copy
    return copy;
}

void math_layout_context_destroy(MathLayoutContext* ctx) {
    if (ctx) {
        free(ctx);
    }
}

void apply_math_style_scaling(MathLayoutContext* ctx, enum ViewMathStyle new_style) {
    if (!ctx) return;
    
    double scale = get_style_scale_factor(ctx->style, new_style);
    ctx->style = new_style;
    ctx->scale_factor *= scale;
    ctx->metrics = calculate_math_metrics(ctx->math_font, new_style);
}

// Math element creation helpers
ViewNode* create_math_atom_node(const char* symbol, const char* unicode) {
    ViewNode* node = view_node_create(VIEW_NODE_MATH_ELEMENT);
    if (!node) return NULL;
    
    node->content.math_elem = calloc(1, sizeof(ViewMathElement));
    if (!node->content.math_elem) {
        free(node);
        return NULL;
    }
    
    ViewMathElement* math_elem = node->content.math_elem;
    math_elem->type = VIEW_MATH_ATOM;
    math_elem->content.atom.symbol = symbol ? strdup(symbol) : NULL;
    math_elem->content.atom.unicode = unicode ? strdup(unicode) : NULL;
    
    return node;
}

ViewNode* create_math_fraction_node(ViewNode* numerator, ViewNode* denominator) {
    ViewNode* node = view_node_create(VIEW_NODE_MATH_ELEMENT);
    if (!node) return NULL;
    
    node->content.math_elem = calloc(1, sizeof(ViewMathElement));
    if (!node->content.math_elem) {
        free(node);
        return NULL;
    }
    
    ViewMathElement* math_elem = node->content.math_elem;
    math_elem->type = VIEW_MATH_FRACTION;
    math_elem->content.fraction.numerator = numerator;
    math_elem->content.fraction.denominator = denominator;
    math_elem->content.fraction.line_thickness = 1.0; // Default thickness
    
    return node;
}

// Placeholder implementations for remaining layout functions
ViewNode* layout_math_radical(ViewNode* radical_node, MathLayoutContext* ctx) {
    // TODO: Implement radical layout
    return NULL;
}

ViewNode* layout_math_matrix(ViewNode* matrix_node, MathLayoutContext* ctx) {
    // TODO: Implement matrix layout
    return NULL;
}

ViewNode* layout_math_delimiter(ViewNode* delimiter_node, MathLayoutContext* ctx) {
    // TODO: Implement delimiter layout
    return NULL;
}

ViewNode* layout_math_function(ViewNode* function_node, MathLayoutContext* ctx) {
    // TODO: Implement function layout
    return NULL;
}

ViewNode* layout_math_operator(ViewNode* operator_node, MathLayoutContext* ctx) {
    // TODO: Implement operator layout
    return NULL;
}

ViewNode* layout_math_accent(ViewNode* accent_node, MathLayoutContext* ctx) {
    // TODO: Implement accent layout
    return NULL;
}

ViewNode* layout_math_underover(ViewNode* underover_node, MathLayoutContext* ctx) {
    // TODO: Implement under/over layout
    return NULL;
}

ViewNode* layout_math_spacing(ViewNode* spacing_node, MathLayoutContext* ctx) {
    // TODO: Implement spacing layout
    return NULL;
}

// Placeholder font functions (to be implemented with actual font system)
ViewFont* get_math_font(const char* font_name, double size) {
    // TODO: Implement actual font loading
    return NULL;
}

ViewFont* get_text_font(const char* font_name, double size) {
    // TODO: Implement actual font loading
    return NULL;
}

uint32_t get_math_glyph(ViewFont* font, const char* symbol) {
    // TODO: Implement glyph lookup
    return 0;
}

double get_glyph_width(ViewFont* font, uint32_t glyph_id) {
    // TODO: Implement glyph metrics
    return 10.0; // Placeholder width
}

double get_glyph_height(ViewFont* font, uint32_t glyph_id) {
    // TODO: Implement glyph metrics
    return 12.0; // Placeholder height
}

double get_glyph_depth(ViewFont* font, uint32_t glyph_id) {
    // TODO: Implement glyph metrics
    return 3.0; // Placeholder depth
}
