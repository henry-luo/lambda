// tex_typeset.cpp - Main TeX Typesetting Implementation
//
// Entry point for typesetting LaTeX/TeX documents

#include "tex_typeset.hpp"
#include "tex_ast_builder.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

// External tree-sitter parser (from tree-sitter-latex or tree-sitter-latex-math)
extern "C" {
    const TSLanguage* tree_sitter_latex(void);
    const TSLanguage* tree_sitter_latex_math(void);
}

namespace tex {

// ============================================================================
// Default Font Provider (Placeholder)
// ============================================================================

class DefaultFontProvider : public FontProvider {
public:
    float base_size;

    DefaultFontProvider(float size) : base_size(size) {}

    const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) override {
        // TODO: Implement proper font loading
        return nullptr;
    }

    const FontMetrics* get_math_symbol_font(float size_pt) override {
        return nullptr;
    }

    const FontMetrics* get_math_extension_font(float size_pt) override {
        return nullptr;
    }

    const FontMetrics* get_math_text_font(float size_pt, bool italic) override {
        return nullptr;
    }
};

FontProvider* create_default_font_provider(Arena* arena) {
    DefaultFontProvider* provider = (DefaultFontProvider*)arena_alloc(arena,
        sizeof(DefaultFontProvider));
    new (provider) DefaultFontProvider(10.0f);
    return provider;
}

// ============================================================================
// TypesetContext Methods
// ============================================================================

void TypesetContext::add_error(SourceLoc loc, const char* msg) {
    if (error_count >= error_capacity) {
        error_capacity = error_capacity ? error_capacity * 2 : 16;
        TypesetResult::Error* new_errors = (TypesetResult::Error*)arena_alloc(arena,
            error_capacity * sizeof(TypesetResult::Error));
        if (errors) {
            memcpy(new_errors, errors, error_count * sizeof(TypesetResult::Error));
        }
        errors = new_errors;
    }

    errors[error_count].loc = loc;
    errors[error_count].message = msg;
    error_count++;

    log_error("tex_typeset: %s at line %d", msg, loc.line);
}

void TypesetContext::start_new_page() {
    // Save current page if exists
    if (current_page && current_page->content.list.count > 0) {
        if (page_count >= page_capacity) {
            page_capacity = page_capacity ? page_capacity * 2 : 4;
            TexBox** new_pages = (TexBox**)arena_alloc(arena,
                page_capacity * sizeof(TexBox*));
            if (pages) {
                memcpy(new_pages, pages, page_count * sizeof(TexBox*));
            }
            pages = new_pages;
        }
        pages[page_count++] = current_page;
    }

    // Start new page
    current_page = make_vlist_box(arena);
    current_y = config->margin_top;
    available_height = config->page_height - config->margin_top - config->margin_bottom;
}

void TypesetContext::add_to_page(TexBox* content) {
    if (!content) return;

    float content_height = content->total_height();

    // Check if we need a new page
    if (content_height > available_height && current_page->content.list.count > 0) {
        start_new_page();
    }

    // Position content
    content->x = config->margin_left;
    content->y = current_y;

    add_child(current_page, content, arena);

    current_y += content_height;
    available_height -= content_height;
}

void TypesetContext::ensure_vertical_space(float height) {
    if (height > available_height && current_page->content.list.count > 0) {
        start_new_page();
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

float compute_baseline_skip(float font_size, float line_spacing) {
    // Standard baseline skip is 1.2 * font size
    return font_size * 1.2f * line_spacing;
}

bool is_paragraph_break(TexNode* node) {
    if (!node) return true;

    switch (node->type) {
        case NodeType::Paragraph:
        case NodeType::VSkip:
            return true;

        case NodeType::Command: {
            CommandNode* cmd = (CommandNode*)node;
            // par, section, etc. cause paragraph breaks
            if (strcmp(cmd->name, "par") == 0 ||
                strcmp(cmd->name, "section") == 0 ||
                strcmp(cmd->name, "subsection") == 0 ||
                strcmp(cmd->name, "newpage") == 0) {
                return true;
            }
            break;
        }

        case NodeType::Environment:
            // Most environments cause paragraph breaks
            return true;

        default:
            break;
    }

    return false;
}

// ============================================================================
// Main Typesetting Functions
// ============================================================================

TypesetResult typeset_latex(
    const char* source,
    size_t source_len,
    const TypesetConfig& config,
    Arena* arena
) {
    log_debug("tex_typeset: starting typeset of %zu bytes", source_len);

    // Parse with tree-sitter
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, source_len);

    if (!tree) {
        log_error("tex_typeset: failed to parse LaTeX source");
        TypesetResult result = {};
        result.success = false;
        ts_parser_delete(parser);
        return result;
    }

    TypesetResult result = typeset_from_tree(source, tree, config, arena);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return result;
}

TypesetResult typeset_from_tree(
    const char* source,
    TSTree* tree,
    const TypesetConfig& config,
    Arena* arena
) {
    // Build AST from tree-sitter parse
    ASTBuilder* builder = create_ast_builder(arena, source, strlen(source), tree);
    TexNode* ast = build_ast(builder);

    if (!ast) {
        log_error("tex_typeset: failed to build AST");
        TypesetResult result = {};
        result.success = false;
        return result;
    }

    return typeset_from_ast(ast, config, arena);
}

TypesetResult typeset_from_ast(
    TexNode* ast,
    const TypesetConfig& config,
    Arena* arena
) {
    TypesetResult result = {};

    // Initialize context
    TypesetContext ctx = {};
    ctx.arena = arena;
    ctx.config = &config;
    ctx.fonts = create_default_font_provider(arena);

    ctx.pages = nullptr;
    ctx.page_count = 0;
    ctx.page_capacity = 0;

    ctx.errors = nullptr;
    ctx.error_count = 0;
    ctx.error_capacity = 0;

    // Initialize math context
    ctx.math_ctx.arena = arena;
    ctx.math_ctx.fonts = ctx.fonts;
    ctx.math_ctx.style = MathStyle::Display;
    ctx.math_ctx.base_size_pt = config.base_font_size;

    // Start first page
    ctx.start_new_page();

    // Typeset the AST
    TexBox* content = typeset_node(ast, ctx);
    if (content) {
        ctx.add_to_page(content);
    }

    // Finalize last page
    if (ctx.current_page && ctx.current_page->content.list.count > 0) {
        ctx.start_new_page();  // This saves current page
    }

    // Build result
    result.pages = (TypesetPage*)arena_alloc(arena,
        ctx.page_count * sizeof(TypesetPage));
    result.page_count = ctx.page_count;

    for (int i = 0; i < ctx.page_count; ++i) {
        result.pages[i].content = ctx.pages[i];
        result.pages[i].width = config.page_width;
        result.pages[i].height = config.page_height;
        result.pages[i].page_number = i + 1;
    }

    result.errors = ctx.errors;
    result.error_count = ctx.error_count;
    result.success = (ctx.error_count == 0);

    log_debug("tex_typeset: completed with %d pages, %d errors",
        result.page_count, result.error_count);

    return result;
}

// ============================================================================
// Math-Only Typesetting
// ============================================================================

TexBox* typeset_math_inline(
    const char* math_source,
    size_t source_len,
    float font_size,
    Arena* arena
) {
    // Parse math
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex_math());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, math_source, source_len);

    if (!tree) {
        log_error("tex_typeset: failed to parse math");
        ts_parser_delete(parser);
        return nullptr;
    }

    // Build AST
    ASTBuilderConfig cfg = default_config();
    cfg.initial_mode = Mode::Math;

    ASTBuilder* builder = create_ast_builder(arena, math_source, source_len, tree, cfg);
    TexNode* ast = build_ast(builder);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!ast) {
        return nullptr;
    }

    // Create math layout context
    MathLayoutContext math_ctx = {};
    math_ctx.arena = arena;
    math_ctx.fonts = create_default_font_provider(arena);
    math_ctx.style = MathStyle::Text;  // Inline = text style
    math_ctx.base_size_pt = font_size;

    // TODO: Convert AST to MathAtom array and call layout_math_list
    // For now, return placeholder
    TexBox* result = make_hlist_box(arena);
    return result;
}

TexBox* typeset_math_display(
    const char* math_source,
    size_t source_len,
    float font_size,
    float line_width,
    Arena* arena
) {
    // Parse math
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex_math());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, math_source, source_len);

    if (!tree) {
        log_error("tex_typeset: failed to parse math");
        ts_parser_delete(parser);
        return nullptr;
    }

    // Build AST
    ASTBuilderConfig cfg = default_config();
    cfg.initial_mode = Mode::Math;

    ASTBuilder* builder = create_ast_builder(arena, math_source, source_len, tree, cfg);
    TexNode* ast = build_ast(builder);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!ast) {
        return nullptr;
    }

    // Create math layout context
    MathLayoutContext math_ctx = {};
    math_ctx.arena = arena;
    math_ctx.fonts = create_default_font_provider(arena);
    math_ctx.style = MathStyle::Display;
    math_ctx.base_size_pt = font_size;

    // TODO: Convert AST to MathAtom array and call layout_math_list
    // For now, return placeholder
    TexBox* result = make_hlist_box(arena);

    // Center on line
    result = rebox(result, line_width, math_ctx);

    return result;
}

// ============================================================================
// AST Node Typesetting
// ============================================================================

TexBox* typeset_node(TexNode* node, TypesetContext& ctx) {
    if (!node) return nullptr;

    switch (node->type) {
        case NodeType::Char:
            return typeset_text((CharNode*)node, ctx);

        case NodeType::Math:
            return typeset_math((MathNode*)node, ctx);

        case NodeType::Group:
            return typeset_group((GroupNode*)node, ctx);

        case NodeType::Command:
            return typeset_command((CommandNode*)node, ctx);

        case NodeType::Environment:
            return typeset_environment((EnvironmentNode*)node, ctx);

        case NodeType::Fraction:
            return typeset_fraction((FractionNode*)node, ctx);

        case NodeType::Radical:
            return typeset_radical((RadicalNode*)node, ctx);

        case NodeType::Script:
            return typeset_scripts((ScriptNode*)node, ctx);

        case NodeType::Glue: {
            GlueNode* glue = (GlueNode*)node;
            return make_glue_box(glue->glue, ctx.arena);
        }

        case NodeType::Kern: {
            KernNode* kern = (KernNode*)node;
            return make_kern_box(kern->amount, ctx.arena);
        }

        case NodeType::Rule: {
            RuleNode* rule = (RuleNode*)node;
            return make_rule_box(rule->width, rule->height, rule->depth, ctx.arena);
        }

        case NodeType::Paragraph:
            return typeset_paragraph((GroupNode*)node, ctx);

        default:
            log_debug("tex_typeset: unhandled node type %d", (int)node->type);
            return nullptr;
    }
}

TexBox* typeset_text(CharNode* node, TypesetContext& ctx) {
    if (node->text && node->text_len > 0) {
        // Text string - create glyph boxes
        TexBox* hlist = make_hlist_box(ctx.arena);

        for (size_t i = 0; i < node->text_len; ++i) {
            char c = node->text[i];
            if (c == ' ') {
                // Interword space
                add_child(hlist, make_glue_box(interword_space(ctx.config->base_font_size), ctx.arena), ctx.arena);
            } else {
                TexBox* glyph = make_glyph_box((uint32_t)c, ctx.arena);
                // TODO: Get actual metrics from font
                glyph->width = ctx.config->base_font_size * 0.5f;  // Approximate
                glyph->height = ctx.config->base_font_size * 0.7f;
                glyph->depth = ctx.config->base_font_size * 0.2f;
                add_child(hlist, glyph, ctx.arena);
            }
        }

        compute_hlist_natural_dims(hlist);
        return hlist;
    } else {
        // Single codepoint
        TexBox* glyph = make_glyph_box(node->codepoint, ctx.arena);
        glyph->atom_type = node->atom_type;
        // TODO: Get actual metrics from font
        glyph->width = ctx.config->base_font_size * 0.5f;
        glyph->height = ctx.config->base_font_size * 0.7f;
        glyph->depth = 0;
        return glyph;
    }
}

TexBox* typeset_math(MathNode* node, TypesetContext& ctx) {
    // Set up math context
    MathLayoutContext math_ctx = ctx.math_ctx;
    math_ctx.style = node->is_display ? MathStyle::Display : MathStyle::Text;

    // Typeset content
    TexBox* content = nullptr;
    if (node->content) {
        // Save context, switch to math mode
        content = typeset_node(node->content, ctx);
    }

    if (!content) {
        content = make_empty_box(0, ctx.config->base_font_size * 0.7f, 0, ctx.arena);
    }

    if (node->is_display) {
        // Add vertical spacing for display math
        TexBox* vlist = make_vlist_box(ctx.arena);

        // Space above
        add_child(vlist, make_glue_box(
            Glue{ctx.config->display_skip_above, 3.0f, GlueOrder::Normal, 1.0f, GlueOrder::Normal},
            ctx.arena), ctx.arena);

        // Content (centered if configured)
        if (ctx.config->display_math_centered) {
            content = rebox(content, ctx.config->line_break.line_width, math_ctx);
        }
        add_child(vlist, content, ctx.arena);

        // Space below
        add_child(vlist, make_glue_box(
            Glue{ctx.config->display_skip_below, 3.0f, GlueOrder::Normal, 1.0f, GlueOrder::Normal},
            ctx.arena), ctx.arena);

        compute_vlist_natural_dims(vlist);
        return vlist;
    }

    return content;
}

TexBox* typeset_group(GroupNode* node, TypesetContext& ctx) {
    if (node->child_count == 0) {
        return nullptr;
    }

    if (node->child_count == 1) {
        return typeset_node(node->children[0], ctx);
    }

    // Multiple children - check if paragraph-like
    bool has_paragraph_breaks = false;
    for (int i = 0; i < node->child_count; ++i) {
        if (is_paragraph_break(node->children[i])) {
            has_paragraph_breaks = true;
            break;
        }
    }

    if (has_paragraph_breaks) {
        // Build as vlist of paragraphs
        TexBox* vlist = make_vlist_box(ctx.arena);

        int para_start = 0;
        for (int i = 0; i <= node->child_count; ++i) {
            bool is_break = (i == node->child_count) ||
                           is_paragraph_break(node->children[i]);

            if (is_break && i > para_start) {
                // Collect paragraph content
                TexBox* para = collect_paragraph_content(
                    &node->children[para_start],
                    i - para_start,
                    ctx
                );

                if (para) {
                    // Break into lines
                    LineBreakResult breaks = break_paragraph(
                        para,
                        ctx.config->line_break,
                        ctx.arena
                    );

                    if (breaks.success) {
                        Line* lines = build_lines(para, breaks, ctx.config->line_break, ctx.arena);
                        TexBox* para_vlist = build_paragraph_vlist(
                            lines,
                            breaks.line_count,
                            compute_baseline_skip(ctx.config->base_font_size, ctx.config->line_spacing),
                            ctx.arena
                        );
                        add_child(vlist, para_vlist, ctx.arena);
                    }
                }

                para_start = i + 1;
            }
        }

        compute_vlist_natural_dims(vlist);
        return vlist;
    } else {
        // Build as hlist
        TexBox* hlist = make_hlist_box(ctx.arena);

        for (int i = 0; i < node->child_count; ++i) {
            TexBox* child = typeset_node(node->children[i], ctx);
            if (child) {
                add_child(hlist, child, ctx.arena);
            }
        }

        compute_hlist_natural_dims(hlist);
        return hlist;
    }
}

TexBox* typeset_paragraph(GroupNode* para, TypesetContext& ctx) {
    // Collect content into hlist
    TexBox* hlist = collect_paragraph_content(para->children, para->child_count, ctx);

    if (!hlist || hlist->content.list.count == 0) {
        return nullptr;
    }

    // Break into lines
    LineBreakResult breaks = break_paragraph(hlist, ctx.config->line_break, ctx.arena);

    if (!breaks.success) {
        log_error("tex_typeset: paragraph line breaking failed");
        return hlist;  // Return unbroken
    }

    // Build lines
    Line* lines = build_lines(hlist, breaks, ctx.config->line_break, ctx.arena);

    // Stack into vlist
    return build_paragraph_vlist(
        lines,
        breaks.line_count,
        compute_baseline_skip(ctx.config->base_font_size, ctx.config->line_spacing),
        ctx.arena
    );
}

TexBox* collect_paragraph_content(
    TexNode** nodes,
    int node_count,
    TypesetContext& ctx
) {
    TexBox* hlist = make_hlist_box(ctx.arena);

    // Add paragraph indent
    if (ctx.config->line_break.par_indent > 0) {
        add_child(hlist, make_empty_box(ctx.config->line_break.par_indent, 0, 0, ctx.arena), ctx.arena);
    }

    for (int i = 0; i < node_count; ++i) {
        TexBox* box = typeset_node(nodes[i], ctx);
        if (box) {
            add_child(hlist, box, ctx.arena);

            // Add interword space after most items
            // (Simplified - real implementation would be more sophisticated)
        }
    }

    // Add parfillskip at end
    add_child(hlist, make_glue_box(Glue::hfil(), ctx.arena), ctx.arena);

    compute_hlist_natural_dims(hlist);
    return hlist;
}

TexBox* typeset_command(CommandNode* node, TypesetContext& ctx) {
    // Handle various commands

    if (strcmp(node->name, "textbf") == 0 || strcmp(node->name, "bf") == 0) {
        // Bold - would change font, for now just pass through
        if (node->arg_count > 0) {
            return typeset_node(node->args[0], ctx);
        }
    }

    if (strcmp(node->name, "textit") == 0 || strcmp(node->name, "it") == 0 ||
        strcmp(node->name, "emph") == 0) {
        // Italic
        if (node->arg_count > 0) {
            return typeset_node(node->args[0], ctx);
        }
    }

    if (strcmp(node->name, "par") == 0) {
        // Paragraph break - return vskip
        return make_glue_box(Glue{ctx.config->base_font_size,
            ctx.config->base_font_size * 0.5f, GlueOrder::Normal,
            0, GlueOrder::Normal}, ctx.arena);
    }

    if (strcmp(node->name, "hspace") == 0) {
        // Horizontal space
        // Would parse argument for amount
        return make_glue_box(Glue{10.0f, 5.0f, GlueOrder::Normal, 2.0f, GlueOrder::Normal}, ctx.arena);
    }

    if (strcmp(node->name, "vspace") == 0) {
        return make_glue_box(Glue{10.0f, 5.0f, GlueOrder::Normal, 2.0f, GlueOrder::Normal}, ctx.arena);
    }

    // Unhandled command - log and skip
    log_debug("tex_typeset: unhandled command \\%s", node->name);
    return nullptr;
}

TexBox* typeset_environment(EnvironmentNode* node, TypesetContext& ctx) {
    const EnvironmentInfo* info = get_environment_info(node->name);

    if (info && info->is_math) {
        // Math environment
        MathNode math_node = {};
        math_node.base.type = NodeType::Math;
        math_node.is_display = info->is_display;
        math_node.content = node->content;

        return typeset_math(&math_node, ctx);
    }

    // Regular environment - just typeset content
    if (node->content) {
        return typeset_node(node->content, ctx);
    }

    return nullptr;
}

TexBox* typeset_fraction(FractionNode* node, TypesetContext& ctx) {
    MathLayoutContext math_ctx = ctx.math_ctx;

    TexBox* num = node->numerator ? typeset_node(node->numerator, ctx) :
                  make_empty_box(10, 10, 0, ctx.arena);
    TexBox* denom = node->denominator ? typeset_node(node->denominator, ctx) :
                    make_empty_box(10, 10, 0, ctx.arena);

    FractionParams params = {};
    params.numerator = num;
    params.denominator = denom;
    params.rule_thickness = -1;  // Use default

    return layout_fraction(params, math_ctx);
}

TexBox* typeset_radical(RadicalNode* node, TypesetContext& ctx) {
    MathLayoutContext math_ctx = ctx.math_ctx;

    TexBox* radicand = node->radicand ? typeset_node(node->radicand, ctx) :
                       make_empty_box(10, 10, 0, ctx.arena);
    TexBox* degree = node->degree ? typeset_node(node->degree, ctx) : nullptr;

    return layout_radical(radicand, degree, math_ctx);
}

TexBox* typeset_scripts(ScriptNode* node, TypesetContext& ctx) {
    MathLayoutContext math_ctx = ctx.math_ctx;

    TexBox* base = node->base ? typeset_node(node->base, ctx) :
                   make_empty_box(0, 0, 0, ctx.arena);

    TexBox* script_content = node->script ? typeset_node(node->script, ctx) : nullptr;

    ScriptAttachment scripts = {};
    scripts.nucleus = base;

    if (node->is_superscript) {
        scripts.superscript = script_content;
    } else {
        scripts.subscript = script_content;
    }

    return attach_scripts(scripts, base->atom_type, math_ctx);
}

// ============================================================================
// Radiant Integration
// ============================================================================

// Include Radiant font provider header for integration
#include "tex_radiant_font.hpp"
#include "tex_radiant_bridge.hpp"

FontProvider* create_radiant_font_provider(UiContext* uicon, Arena* arena) {
    if (!uicon || !arena) {
        log_error("tex_typeset: null uicon or arena for Radiant font provider");
        return create_default_font_provider(arena);
    }

    RadiantFontProvider* provider = (RadiantFontProvider*)arena_alloc(arena,
        sizeof(RadiantFontProvider));
    new (provider) RadiantFontProvider(uicon, arena);
    return provider;
}

radiant::MathBox* typeset_math_for_radiant(
    const char* math_source,
    size_t source_len,
    float font_size,
    bool display_mode,
    UiContext* uicon,
    Arena* arena
) {
    if (!math_source || source_len == 0 || !arena) {
        return nullptr;
    }

    // Parse math with tree-sitter
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex_math());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, math_source, source_len);

    if (!tree) {
        log_error("tex_typeset: failed to parse math for Radiant");
        ts_parser_delete(parser);
        return nullptr;
    }

    // Build AST
    ASTBuilderConfig cfg = default_config();
    cfg.initial_mode = Mode::Math;

    ASTBuilder* builder = create_ast_builder(arena, math_source, source_len, tree, cfg);
    TexNode* ast = build_ast(builder);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!ast) {
        log_error("tex_typeset: failed to build math AST");
        return nullptr;
    }

    // Create font provider using Radiant's FreeType infrastructure
    RadiantFontProvider* font_provider = nullptr;
    if (uicon) {
        font_provider = (RadiantFontProvider*)arena_alloc(arena, sizeof(RadiantFontProvider));
        new (font_provider) RadiantFontProvider(uicon, arena);
    }

    // Create math layout context
    MathLayoutContext math_ctx = {};
    math_ctx.arena = arena;
    math_ctx.fonts = font_provider ? font_provider : create_default_font_provider(arena);
    math_ctx.style = display_mode ? MathStyle::Display : MathStyle::Text;
    math_ctx.base_size_pt = font_size;

    // Create typeset context for node processing
    TypesetConfig config = TypesetConfig::defaults();
    config.base_font_size = font_size;

    TypesetContext ctx = {};
    ctx.arena = arena;
    ctx.config = &config;
    ctx.fonts = math_ctx.fonts;
    ctx.math_ctx = math_ctx;

    // Typeset the AST to get TexBox
    TexBox* tex_result = typeset_node(ast, ctx);

    if (!tex_result) {
        log_warn("tex_typeset: typeset_node returned null");
        return nullptr;
    }

    // Convert TexBox to Radiant MathBox
    ConversionContext conv_ctx;
    conv_ctx.arena = arena;
    conv_ctx.font_provider = font_provider;
    conv_ctx.base_size = font_size;
    conv_ctx.scale = 1.0f;

    radiant::MathBox* result = convert_tex_to_math_box(tex_result, &conv_ctx);

    log_debug("tex_typeset: typeset_math_for_radiant completed, result=%p", result);
    return result;
}

radiant::MathBox* typeset_lambda_math_for_radiant(
    Item math_node,
    radiant::MathContext* ctx,
    UiContext* uicon,
    Arena* arena
) {
    if (math_node == ItemNull || !arena) {
        return nullptr;
    }

    // This function handles typesetting Lambda math nodes (from HTML/MathML)
    // For now, we delegate to the existing layout_math_with_tex bridge function

    RadiantFontProvider* font_provider = nullptr;
    if (uicon) {
        font_provider = (RadiantFontProvider*)arena_alloc(arena, sizeof(RadiantFontProvider));
        new (font_provider) RadiantFontProvider(uicon, arena);
    }

    return layout_math_with_tex(math_node, ctx, arena, font_provider);
}

} // namespace tex
