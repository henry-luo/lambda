// tex_output.cpp - Output format implementations
//
// JSON serialization and comparison utilities for TeX typesetting.

#include "tex_output.hpp"
#include "dvi_parser.hpp"
#include "../../lib/log.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace tex {

// ============================================================================
// JSON Output Implementation
// ============================================================================

// Internal helper for JSON writing
struct JSONWriter {
    StrBuf* buf;
    int indent;
    bool pretty;
    int decimals;

    void write_indent() {
        if (pretty) {
            for (int i = 0; i < indent; i++) {
                strbuf_append_str(buf, "  ");
            }
        }
    }

    void write_newline() {
        if (pretty) {
            strbuf_append_char(buf, '\n');
        }
    }

    void write_key(const char* key) {
        write_indent();
        strbuf_append_char(buf, '"');
        strbuf_append_str(buf, key);
        strbuf_append_str(buf, pretty ? "\": " : "\":");
    }

    void write_string(const char* value) {
        strbuf_append_char(buf, '"');
        if (value) {
            // escape special characters
            for (const char* p = value; *p; p++) {
                switch (*p) {
                    case '"':  strbuf_append_str(buf, "\\\""); break;
                    case '\\': strbuf_append_str(buf, "\\\\"); break;
                    case '\n': strbuf_append_str(buf, "\\n"); break;
                    case '\r': strbuf_append_str(buf, "\\r"); break;
                    case '\t': strbuf_append_str(buf, "\\t"); break;
                    default:   strbuf_append_char(buf, *p); break;
                }
            }
        }
        strbuf_append_char(buf, '"');
    }

    void write_int(int value) {
        strbuf_append_int(buf, value);
    }

    void write_float(float value) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%.*f", decimals, value);
        strbuf_append_str(buf, tmp);
    }

    void write_bool(bool value) {
        strbuf_append_str(buf, value ? "true" : "false");
    }
};

static const char* box_kind_to_string(BoxKind kind) {
    switch (kind) {
        case BoxKind::Char:      return "char";
        case BoxKind::HBox:      return "hbox";
        case BoxKind::VBox:      return "vbox";
        case BoxKind::Rule:      return "rule";
        case BoxKind::Glue:      return "glue";
        case BoxKind::Kern:      return "kern";
        case BoxKind::Math:      return "math";
        case BoxKind::Fraction:  return "fraction";
        case BoxKind::Radical:   return "radical";
        case BoxKind::Delimiter: return "delimiter";
        case BoxKind::Accent:    return "accent";
        default:                 return "unknown";
    }
}

static void write_tex_box_json(JSONWriter& w, const TexBox* box);

static void write_tex_box_children(JSONWriter& w, const TexBox* box) {
    if (box->kind == BoxKind::HBox && box->content.hbox.count > 0) {
        w.write_key("children");
        strbuf_append_char(w.buf, '[');
        w.write_newline();
        w.indent++;

        for (int i = 0; i < box->content.hbox.count; i++) {
            if (i > 0) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
            }
            write_tex_box_json(w, box->content.hbox.children[i]);
        }

        w.write_newline();
        w.indent--;
        w.write_indent();
        strbuf_append_char(w.buf, ']');
    } else if (box->kind == BoxKind::VBox && box->content.vbox.count > 0) {
        w.write_key("children");
        strbuf_append_char(w.buf, '[');
        w.write_newline();
        w.indent++;

        for (int i = 0; i < box->content.vbox.count; i++) {
            if (i > 0) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
            }
            write_tex_box_json(w, box->content.vbox.children[i]);
        }

        w.write_newline();
        w.indent--;
        w.write_indent();
        strbuf_append_char(w.buf, ']');
    }
}

static void write_tex_box_json(JSONWriter& w, const TexBox* box) {
    if (!box) {
        strbuf_append_str(w.buf, "null");
        return;
    }

    w.write_indent();
    strbuf_append_char(w.buf, '{');
    w.write_newline();
    w.indent++;

    // type
    w.write_key("type");
    w.write_string(box_kind_to_string(box->kind));
    strbuf_append_char(w.buf, ',');
    w.write_newline();

    // dimensions
    w.write_key("width");
    w.write_float(box->width);
    strbuf_append_char(w.buf, ',');
    w.write_newline();

    w.write_key("height");
    w.write_float(box->height);
    strbuf_append_char(w.buf, ',');
    w.write_newline();

    w.write_key("depth");
    w.write_float(box->depth);

    // position (if set)
    if (box->x != 0 || box->y != 0) {
        strbuf_append_char(w.buf, ',');
        w.write_newline();
        w.write_key("x");
        w.write_float(box->x);
        strbuf_append_char(w.buf, ',');
        w.write_newline();
        w.write_key("y");
        w.write_float(box->y);
    }

    // content-specific fields
    switch (box->kind) {
        case BoxKind::Char:
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("codepoint");
            w.write_int(box->content.ch.codepoint);
            break;

        case BoxKind::Glue:
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("space");
            w.write_float(box->content.glue.space);
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("stretch");
            w.write_float(box->content.glue.stretch);
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("shrink");
            w.write_float(box->content.glue.shrink);
            break;

        case BoxKind::Rule:
            // dimensions already output
            break;

        case BoxKind::Fraction:
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("rule_thickness");
            w.write_float(box->content.fraction.rule_thickness);
            if (box->content.fraction.numerator) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
                w.write_key("numerator");
                w.write_newline();
                write_tex_box_json(w, box->content.fraction.numerator);
            }
            if (box->content.fraction.denominator) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
                w.write_key("denominator");
                w.write_newline();
                write_tex_box_json(w, box->content.fraction.denominator);
            }
            break;

        case BoxKind::Radical:
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("rule_thickness");
            w.write_float(box->content.radical.rule_thickness);
            if (box->content.radical.radicand) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
                w.write_key("radicand");
                w.write_newline();
                write_tex_box_json(w, box->content.radical.radicand);
            }
            if (box->content.radical.index) {
                strbuf_append_char(w.buf, ',');
                w.write_newline();
                w.write_key("index");
                w.write_newline();
                write_tex_box_json(w, box->content.radical.index);
            }
            break;

        case BoxKind::Delimiter:
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("codepoint");
            w.write_int(box->content.delimiter.codepoint);
            strbuf_append_char(w.buf, ',');
            w.write_newline();
            w.write_key("is_left");
            w.write_bool(box->content.delimiter.is_left);
            break;

        default:
            break;
    }

    // children for HBox/VBox
    if (box->kind == BoxKind::HBox || box->kind == BoxKind::VBox) {
        strbuf_append_char(w.buf, ',');
        w.write_newline();
        write_tex_box_children(w, box);
    }

    w.write_newline();
    w.indent--;
    w.write_indent();
    strbuf_append_char(w.buf, '}');
}

char* tex_box_to_json(
    const TexBox* box,
    Arena* arena,
    const JSONOutputOptions& options
) {
    StrBuf* buf = strbuf_create("");

    JSONWriter w;
    w.buf = buf;
    w.indent = 0;
    w.pretty = options.pretty_print;
    w.decimals = options.decimal_places;

    write_tex_box_json(w, box);

    // copy to arena
    char* result = (char*)arena_alloc(arena, buf->length + 1);
    memcpy(result, buf->str, buf->length);
    result[buf->length] = '\0';

    strbuf_free(buf);
    return result;
}

char* typeset_result_to_json(
    const TypesetResult& result,
    Arena* arena,
    const JSONOutputOptions& options
) {
    StrBuf* buf = strbuf_create("");

    JSONWriter w;
    w.buf = buf;
    w.indent = 0;
    w.pretty = options.pretty_print;
    w.decimals = options.decimal_places;

    strbuf_append_char(buf, '{');
    w.write_newline();
    w.indent++;

    w.write_key("success");
    w.write_bool(result.success);
    strbuf_append_char(buf, ',');
    w.write_newline();

    w.write_key("page_count");
    w.write_int(result.page_count);
    strbuf_append_char(buf, ',');
    w.write_newline();

    w.write_key("pages");
    strbuf_append_char(buf, '[');
    w.write_newline();
    w.indent++;

    for (int i = 0; i < result.page_count; i++) {
        if (i > 0) {
            strbuf_append_char(buf, ',');
            w.write_newline();
        }

        w.write_indent();
        strbuf_append_char(buf, '{');
        w.write_newline();
        w.indent++;

        w.write_key("page_number");
        w.write_int(result.pages[i].page_number);
        strbuf_append_char(buf, ',');
        w.write_newline();

        w.write_key("width");
        w.write_float(result.pages[i].width);
        strbuf_append_char(buf, ',');
        w.write_newline();

        w.write_key("height");
        w.write_float(result.pages[i].height);
        strbuf_append_char(buf, ',');
        w.write_newline();

        w.write_key("content");
        w.write_newline();
        write_tex_box_json(w, result.pages[i].content);

        w.write_newline();
        w.indent--;
        w.write_indent();
        strbuf_append_char(buf, '}');
    }

    w.write_newline();
    w.indent--;
    w.write_indent();
    strbuf_append_char(buf, ']');

    if (result.error_count > 0) {
        strbuf_append_char(buf, ',');
        w.write_newline();

        w.write_key("errors");
        strbuf_append_char(buf, '[');
        w.write_newline();
        w.indent++;

        for (int i = 0; i < result.error_count; i++) {
            if (i > 0) {
                strbuf_append_char(buf, ',');
                w.write_newline();
            }
            w.write_indent();
            w.write_string(result.errors[i].message);
        }

        w.write_newline();
        w.indent--;
        w.write_indent();
        strbuf_append_char(buf, ']');
    }

    w.write_newline();
    w.indent--;
    strbuf_append_char(buf, '}');

    char* json_result = (char*)arena_alloc(arena, buf->length + 1);
    memcpy(json_result, buf->str, buf->length);
    json_result[buf->length] = '\0';

    strbuf_free(buf);
    return json_result;
}

bool write_tex_box_json(
    const TexBox* box,
    const char* filename,
    const JSONOutputOptions& options
) {
    Arena arena;
    arena_init(&arena, 64 * 1024);

    char* json = tex_box_to_json(box, &arena, options);

    FILE* f = fopen(filename, "w");
    if (!f) {
        arena_destroy(&arena);
        return false;
    }

    fprintf(f, "%s\n", json);
    fclose(f);

    arena_destroy(&arena);
    return true;
}

// ============================================================================
// Positioned Glyph Extraction
// ============================================================================

// Recursive helper to collect glyphs
static void collect_glyphs(
    const TexBox* box,
    float x,
    float y,
    OutputPage* page,
    Arena* arena
) {
    if (!box) return;

    // adjust for box's local position
    x += box->x;
    y += box->y;

    switch (box->kind) {
        case BoxKind::Char: {
            // grow glyph array if needed
            if (page->glyph_count >= 1000000) return;  // sanity limit

            if (page->glyph_count % 256 == 0) {
                // reallocate
                int new_cap = page->glyph_count + 256;
                OutputGlyph* new_glyphs = (OutputGlyph*)arena_alloc(arena,
                    new_cap * sizeof(OutputGlyph));
                if (page->glyphs) {
                    memcpy(new_glyphs, page->glyphs, page->glyph_count * sizeof(OutputGlyph));
                }
                page->glyphs = new_glyphs;
            }

            OutputGlyph* g = &page->glyphs[page->glyph_count++];
            g->codepoint = box->content.ch.codepoint;
            g->x = x;
            g->y = y;
            g->font = nullptr;  // TODO: track font
            g->size = 0;
            break;
        }

        case BoxKind::Rule: {
            // grow rule array if needed
            if (page->rule_count >= 10000) return;

            if (page->rule_count % 32 == 0) {
                int new_cap = page->rule_count + 32;
                OutputRule* new_rules = (OutputRule*)arena_alloc(arena,
                    new_cap * sizeof(OutputRule));
                if (page->rules) {
                    memcpy(new_rules, page->rules, page->rule_count * sizeof(OutputRule));
                }
                page->rules = new_rules;
            }

            OutputRule* r = &page->rules[page->rule_count++];
            r->x = x;
            r->y = y;
            r->width = box->width;
            r->height = box->height + box->depth;
            break;
        }

        case BoxKind::HBox: {
            float cx = 0;
            for (int i = 0; i < box->content.hbox.count; i++) {
                TexBox* child = box->content.hbox.children[i];
                if (child) {
                    collect_glyphs(child, x + cx, y, page, arena);
                    cx += child->width;
                }
            }
            break;
        }

        case BoxKind::VBox: {
            float cy = 0;
            for (int i = 0; i < box->content.vbox.count; i++) {
                TexBox* child = box->content.vbox.children[i];
                if (child) {
                    cy += child->height;
                    collect_glyphs(child, x, y + cy, page, arena);
                    cy += child->depth;
                }
            }
            break;
        }

        case BoxKind::Fraction:
            if (box->content.fraction.numerator) {
                collect_glyphs(box->content.fraction.numerator,
                    x, y - box->content.fraction.num_shift, page, arena);
            }
            if (box->content.fraction.denominator) {
                collect_glyphs(box->content.fraction.denominator,
                    x, y + box->content.fraction.denom_shift, page, arena);
            }
            break;

        case BoxKind::Radical:
            if (box->content.radical.radicand) {
                collect_glyphs(box->content.radical.radicand, x, y, page, arena);
            }
            break;

        default:
            break;
    }
}

OutputPage* extract_output_page(
    const TexBox* page_content,
    float page_width,
    float page_height,
    Arena* arena
) {
    OutputPage* page = (OutputPage*)arena_alloc(arena, sizeof(OutputPage));
    memset(page, 0, sizeof(OutputPage));

    page->width = page_width;
    page->height = page_height;

    collect_glyphs(page_content, 0, 0, page, arena);

    return page;
}

char* output_page_to_json(
    const OutputPage* page,
    Arena* arena,
    const JSONOutputOptions& options
) {
    StrBuf* buf = strbuf_create("");

    JSONWriter w;
    w.buf = buf;
    w.indent = 0;
    w.pretty = options.pretty_print;
    w.decimals = options.decimal_places;

    strbuf_append_char(buf, '{');
    w.write_newline();
    w.indent++;

    w.write_key("width");
    w.write_float(page->width);
    strbuf_append_char(buf, ',');
    w.write_newline();

    w.write_key("height");
    w.write_float(page->height);
    strbuf_append_char(buf, ',');
    w.write_newline();

    w.write_key("glyphs");
    strbuf_append_char(buf, '[');
    w.write_newline();
    w.indent++;

    for (int i = 0; i < page->glyph_count; i++) {
        if (i > 0) {
            strbuf_append_char(buf, ',');
            w.write_newline();
        }

        const OutputGlyph& g = page->glyphs[i];
        w.write_indent();
        strbuf_append_char(buf, '{');
        strbuf_append_str(buf, "\"c\":");
        w.write_int(g.codepoint);
        strbuf_append_str(buf, ",\"x\":");
        w.write_float(g.x);
        strbuf_append_str(buf, ",\"y\":");
        w.write_float(g.y);
        strbuf_append_char(buf, '}');
    }

    w.write_newline();
    w.indent--;
    w.write_indent();
    strbuf_append_char(buf, ']');

    if (page->rule_count > 0) {
        strbuf_append_char(buf, ',');
        w.write_newline();

        w.write_key("rules");
        strbuf_append_char(buf, '[');
        w.write_newline();
        w.indent++;

        for (int i = 0; i < page->rule_count; i++) {
            if (i > 0) {
                strbuf_append_char(buf, ',');
                w.write_newline();
            }

            const OutputRule& r = page->rules[i];
            w.write_indent();
            strbuf_append_char(buf, '{');
            strbuf_append_str(buf, "\"x\":");
            w.write_float(r.x);
            strbuf_append_str(buf, ",\"y\":");
            w.write_float(r.y);
            strbuf_append_str(buf, ",\"w\":");
            w.write_float(r.width);
            strbuf_append_str(buf, ",\"h\":");
            w.write_float(r.height);
            strbuf_append_char(buf, '}');
        }

        w.write_newline();
        w.indent--;
        w.write_indent();
        strbuf_append_char(buf, ']');
    }

    w.write_newline();
    w.indent--;
    strbuf_append_char(buf, '}');

    char* result = (char*)arena_alloc(arena, buf->length + 1);
    memcpy(result, buf->str, buf->length);
    result[buf->length] = '\0';

    strbuf_free(buf);
    return result;
}

// ============================================================================
// Text Output (Debugging)
// ============================================================================

void dump_tex_box_tree(const TexBox* box, FILE* out, int indent) {
    if (!box) {
        fprintf(out, "%*snull\n", indent * 2, "");
        return;
    }

    fprintf(out, "%*s%s: w=%.2f h=%.2f d=%.2f",
            indent * 2, "",
            box_kind_to_string(box->kind),
            box->width, box->height, box->depth);

    if (box->x != 0 || box->y != 0) {
        fprintf(out, " @(%.2f,%.2f)", box->x, box->y);
    }

    switch (box->kind) {
        case BoxKind::Char:
            fprintf(out, " char=U+%04X '%c'",
                    box->content.ch.codepoint,
                    (box->content.ch.codepoint >= 32 && box->content.ch.codepoint < 127)
                        ? box->content.ch.codepoint : '?');
            break;

        case BoxKind::Glue:
            fprintf(out, " space=%.2f±%.2f/%.2f",
                    box->content.glue.space,
                    box->content.glue.stretch,
                    box->content.glue.shrink);
            break;

        case BoxKind::Kern:
            fprintf(out, " kern=%.2f", box->width);
            break;

        default:
            break;
    }

    fprintf(out, "\n");

    // children
    if (box->kind == BoxKind::HBox) {
        for (int i = 0; i < box->content.hbox.count; i++) {
            dump_tex_box_tree(box->content.hbox.children[i], out, indent + 1);
        }
    } else if (box->kind == BoxKind::VBox) {
        for (int i = 0; i < box->content.vbox.count; i++) {
            dump_tex_box_tree(box->content.vbox.children[i], out, indent + 1);
        }
    } else if (box->kind == BoxKind::Fraction) {
        fprintf(out, "%*snumerator:\n", (indent + 1) * 2, "");
        dump_tex_box_tree(box->content.fraction.numerator, out, indent + 2);
        fprintf(out, "%*sdenominator:\n", (indent + 1) * 2, "");
        dump_tex_box_tree(box->content.fraction.denominator, out, indent + 2);
    } else if (box->kind == BoxKind::Radical) {
        fprintf(out, "%*sradicand:\n", (indent + 1) * 2, "");
        dump_tex_box_tree(box->content.radical.radicand, out, indent + 2);
        if (box->content.radical.index) {
            fprintf(out, "%*sindex:\n", (indent + 1) * 2, "");
            dump_tex_box_tree(box->content.radical.index, out, indent + 2);
        }
    }
}

void dump_typeset_result(const TypesetResult& result, FILE* out) {
    fprintf(out, "TypesetResult: success=%s, pages=%d, errors=%d\n",
            result.success ? "true" : "false",
            result.page_count,
            result.error_count);

    for (int i = 0; i < result.page_count; i++) {
        fprintf(out, "\n=== Page %d (%.0f x %.0f) ===\n",
                result.pages[i].page_number,
                result.pages[i].width,
                result.pages[i].height);
        dump_tex_box_tree(result.pages[i].content, out, 0);
    }

    if (result.error_count > 0) {
        fprintf(out, "\nErrors:\n");
        for (int i = 0; i < result.error_count; i++) {
            fprintf(out, "  [%d:%d] %s\n",
                    result.errors[i].loc.line,
                    result.errors[i].loc.column,
                    result.errors[i].message);
        }
    }
}

// ============================================================================
// Comparison with DVI
// ============================================================================

// Helper to sort glyphs by position
static int compare_glyph_position(const void* a, const void* b) {
    const OutputGlyph* ga = (const OutputGlyph*)a;
    const OutputGlyph* gb = (const OutputGlyph*)b;

    // sort by y first, then x
    if (ga->y != gb->y) {
        return (ga->y < gb->y) ? -1 : 1;
    }
    if (ga->x != gb->x) {
        return (ga->x < gb->x) ? -1 : 1;
    }
    return 0;
}

ComparisonResult compare_with_dvi(
    const OutputPage* lambda_output,
    const dvi::DVIPage* dvi_page,
    float position_tolerance,
    Arena* arena
) {
    ComparisonResult result;
    memset(&result, 0, sizeof(result));

    if (!lambda_output || !dvi_page) {
        result.passed = false;
        return result;
    }

    // copy and sort Lambda glyphs
    OutputGlyph* sorted_lambda = (OutputGlyph*)arena_alloc(arena,
        lambda_output->glyph_count * sizeof(OutputGlyph));
    memcpy(sorted_lambda, lambda_output->glyphs,
           lambda_output->glyph_count * sizeof(OutputGlyph));
    qsort(sorted_lambda, lambda_output->glyph_count, sizeof(OutputGlyph),
          compare_glyph_position);

    // convert DVI glyphs to OutputGlyph and sort
    OutputGlyph* sorted_dvi = (OutputGlyph*)arena_alloc(arena,
        dvi_page->glyph_count * sizeof(OutputGlyph));
    for (int i = 0; i < dvi_page->glyph_count; i++) {
        sorted_dvi[i].codepoint = dvi_page->glyphs[i].codepoint;
        sorted_dvi[i].x = dvi::DVIParser::sp_to_pt(dvi_page->glyphs[i].h);
        sorted_dvi[i].y = dvi::DVIParser::sp_to_pt(dvi_page->glyphs[i].v);
        sorted_dvi[i].font = nullptr;
        sorted_dvi[i].size = 0;
    }
    qsort(sorted_dvi, dvi_page->glyph_count, sizeof(OutputGlyph),
          compare_glyph_position);

    // compare
    int lambda_idx = 0, dvi_idx = 0;
    float total_h_error = 0, total_v_error = 0;

    result.mismatches = (ComparisonResult::Mismatch*)arena_alloc(arena,
        100 * sizeof(ComparisonResult::Mismatch));

    while (lambda_idx < lambda_output->glyph_count && dvi_idx < dvi_page->glyph_count) {
        const OutputGlyph& lg = sorted_lambda[lambda_idx];
        const OutputGlyph& dg = sorted_dvi[dvi_idx];

        // check if codepoints match
        if (lg.codepoint == dg.codepoint) {
            float h_err = fabsf(lg.x - dg.x);
            float v_err = fabsf(lg.y - dg.y);

            if (h_err <= position_tolerance && v_err <= position_tolerance) {
                result.matching_glyphs++;
            } else {
                result.mismatched_glyphs++;

                // record mismatch
                if (result.mismatch_count < 100) {
                    auto& m = result.mismatches[result.mismatch_count++];
                    m.index = result.total_glyphs;
                    m.codepoint = lg.codepoint;
                    m.ref_x = dg.x;
                    m.ref_y = dg.y;
                    m.out_x = lg.x;
                    m.out_y = lg.y;
                }
            }

            total_h_error += h_err;
            total_v_error += v_err;
            if (h_err > result.max_h_error) result.max_h_error = h_err;
            if (v_err > result.max_v_error) result.max_v_error = v_err;

            lambda_idx++;
            dvi_idx++;
        } else {
            // try to find matching glyph
            // for now, simple heuristic: advance whichever has smaller position
            if (compare_glyph_position(&lg, &dg) < 0) {
                result.extra_glyphs++;
                lambda_idx++;
            } else {
                result.missing_glyphs++;
                dvi_idx++;
            }
        }

        result.total_glyphs++;
    }

    // remaining glyphs
    result.extra_glyphs += (lambda_output->glyph_count - lambda_idx);
    result.missing_glyphs += (dvi_page->glyph_count - dvi_idx);
    result.total_glyphs = lambda_output->glyph_count > dvi_page->glyph_count
                          ? lambda_output->glyph_count : dvi_page->glyph_count;

    // compute averages
    int compared = result.matching_glyphs + result.mismatched_glyphs;
    if (compared > 0) {
        result.avg_h_error = total_h_error / compared;
        result.avg_v_error = total_v_error / compared;
    }

    // determine pass/fail
    result.passed = (result.missing_glyphs == 0 &&
                     result.extra_glyphs == 0 &&
                     result.mismatched_glyphs == 0);

    return result;
}

void print_comparison_result(const ComparisonResult& result, FILE* out) {
    fprintf(out, "Comparison Result:\n");
    fprintf(out, "  Total glyphs: %d\n", result.total_glyphs);
    fprintf(out, "  Matching:     %d (%.1f%%)\n",
            result.matching_glyphs,
            result.total_glyphs > 0
                ? 100.0f * result.matching_glyphs / result.total_glyphs : 0);
    fprintf(out, "  Mismatched:   %d\n", result.mismatched_glyphs);
    fprintf(out, "  Missing:      %d\n", result.missing_glyphs);
    fprintf(out, "  Extra:        %d\n", result.extra_glyphs);
    fprintf(out, "  Max H error:  %.2f pt\n", result.max_h_error);
    fprintf(out, "  Max V error:  %.2f pt\n", result.max_v_error);
    fprintf(out, "  Avg H error:  %.2f pt\n", result.avg_h_error);
    fprintf(out, "  Avg V error:  %.2f pt\n", result.avg_v_error);
    fprintf(out, "  PASSED:       %s\n", result.passed ? "YES" : "NO");

    if (result.mismatch_count > 0 && !result.passed) {
        fprintf(out, "\nFirst %d mismatches:\n", result.mismatch_count);
        for (int i = 0; i < result.mismatch_count && i < 10; i++) {
            const auto& m = result.mismatches[i];
            fprintf(out, "  [%d] char=%d '%c': ref=(%.2f,%.2f) out=(%.2f,%.2f)\n",
                    m.index, m.codepoint,
                    (m.codepoint >= 32 && m.codepoint < 127) ? m.codepoint : '?',
                    m.ref_x, m.ref_y, m.out_x, m.out_y);
        }
    }
}

} // namespace tex
