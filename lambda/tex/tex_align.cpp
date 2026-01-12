// tex_align.cpp - TeX Alignment Implementation
//
// Implements \halign and \valign following TeXBook Chapter 22.
//
// Reference: TeXBook Chapter 22

#include "tex_align.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// Helper Functions
// ============================================================================

static size_t skip_whitespace(const char* str, size_t pos, size_t len) {
    while (pos < len && (str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n' || str[pos] == '\r')) {
        pos++;
    }
    return pos;
}

static bool match_command(const char* str, size_t pos, size_t len, const char* cmd) {
    size_t cmd_len = strlen(cmd);
    if (pos + cmd_len > len) return false;
    return strncmp(str + pos, cmd, cmd_len) == 0;
}

// ============================================================================
// Preamble Parsing
// ============================================================================

AlignTemplate* parse_align_preamble(
    const char* preamble,
    size_t len,
    Glue default_tabskip,
    bool is_valign,
    Arena* arena
) {
    AlignTemplate* tmpl = (AlignTemplate*)arena_alloc(arena, sizeof(AlignTemplate));
    tmpl->initial_tabskip = default_tabskip;
    tmpl->is_valign = is_valign;

    // First pass: count columns (separated by &)
    int col_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (preamble[i] == '&') col_count++;
        // Skip over braced groups
        if (preamble[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (preamble[i] == '{') depth++;
                else if (preamble[i] == '}') depth--;
                i++;
            }
            i--;  // compensate for loop increment
        }
    }

    tmpl->columns = (AlignColumn*)arena_alloc(arena, col_count * sizeof(AlignColumn));
    tmpl->column_count = col_count;

    // Initialize columns
    for (int c = 0; c < col_count; c++) {
        tmpl->columns[c] = AlignColumn();
        tmpl->columns[c].tabskip = default_tabskip;
    }

    // Second pass: parse each column template
    int col_idx = 0;
    size_t col_start = 0;

    for (size_t i = 0; i <= len; i++) {
        // End of column (& or end of string)
        if (i == len || (preamble[i] == '&' && i > 0)) {
            // Parse column template from col_start to i
            size_t col_end = i;
            AlignColumn& col = tmpl->columns[col_idx];

            // Find # separator within this column
            size_t hash_pos = col_start;
            bool found_hash = false;
            for (size_t j = col_start; j < col_end; j++) {
                if (preamble[j] == '#') {
                    hash_pos = j;
                    found_hash = true;
                    break;
                }
                // Skip braced groups
                if (preamble[j] == '{') {
                    int depth = 1;
                    j++;
                    while (j < col_end && depth > 0) {
                        if (preamble[j] == '{') depth++;
                        else if (preamble[j] == '}') depth--;
                        j++;
                    }
                    j--;
                }
            }

            if (found_hash) {
                // u_template is before #
                col.u_template = preamble + col_start;
                col.u_len = hash_pos - col_start;
                // v_template is after #
                col.v_template = preamble + hash_pos + 1;
                col.v_len = col_end - hash_pos - 1;
            } else {
                // No # found - entire template is u_template
                col.u_template = preamble + col_start;
                col.u_len = col_end - col_start;
                col.v_template = nullptr;
                col.v_len = 0;
            }

            col_idx++;
            col_start = i + 1;
        }

        // Skip braced groups
        if (i < len && preamble[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (preamble[i] == '{') depth++;
                else if (preamble[i] == '}') depth--;
                i++;
            }
            i--;
        }
    }

    log_debug("align: parsed preamble with %d columns", tmpl->column_count);
    return tmpl;
}

// ============================================================================
// Row Parsing
// ============================================================================

AlignRow* parse_align_row(
    const char* row_text,
    size_t len,
    const AlignTemplate* tmpl,
    Arena* arena
) {
    AlignRow* row = (AlignRow*)arena_alloc(arena, sizeof(AlignRow));
    *row = AlignRow();

    // Check for \noalign at start
    size_t pos = skip_whitespace(row_text, 0, len);
    if (match_command(row_text, pos, len, "\\noalign")) {
        row->is_noalign = true;
        // Parse braced content
        pos += 8;  // skip \noalign
        pos = skip_whitespace(row_text, pos, len);
        if (pos < len && row_text[pos] == '{') {
            pos++;
            size_t start = pos;
            int depth = 1;
            while (pos < len && depth > 0) {
                if (row_text[pos] == '{') depth++;
                else if (row_text[pos] == '}') depth--;
                pos++;
            }
            // TODO: parse noalign content into TexNode
            row->noalign_content = nullptr;
        }
        return row;
    }

    // Count cells (separated by &)
    int cell_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (row_text[i] == '&') cell_count++;
        if (row_text[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (row_text[i] == '{') depth++;
                else if (row_text[i] == '}') depth--;
                i++;
            }
            i--;
        }
    }

    row->cells = (AlignCell*)arena_alloc(arena, cell_count * sizeof(AlignCell));
    row->cell_count = cell_count;

    // Initialize cells
    for (int c = 0; c < cell_count; c++) {
        row->cells[c] = AlignCell();
    }

    // Parse each cell
    int cell_idx = 0;
    size_t cell_start = 0;

    for (size_t i = 0; i <= len; i++) {
        if (i == len || row_text[i] == '&') {
            if (cell_idx < cell_count) {
                AlignCell& cell = row->cells[cell_idx];
                size_t cell_end = i;

                // Check for \omit at start of cell
                size_t cs = skip_whitespace(row_text, cell_start, cell_end);
                if (match_command(row_text, cs, cell_end, "\\omit")) {
                    cell.is_omit = true;
                    cs += 5;  // skip \omit
                }

                // Check for \span
                // TODO: handle \multispan{n}

                // Store cell content (without template application for now)
                cell.span_count = 1;
                // TODO: Actually parse and typeset cell content

                cell_idx++;
            }
            cell_start = i + 1;
        }

        if (i < len && row_text[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (row_text[i] == '{') depth++;
                else if (row_text[i] == '}') depth--;
                i++;
            }
            i--;
        }
    }

    return row;
}

AlignRow* parse_align_rows(
    const char* content,
    size_t len,
    const AlignTemplate* tmpl,
    int* row_count,
    Arena* arena
) {
    // Count rows (separated by \cr or \\)
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (content[i] == '\\') {
            if (i + 1 < len && content[i + 1] == '\\') {
                count++;
                i++;  // skip second backslash
            } else if (i + 2 < len && strncmp(content + i + 1, "cr", 2) == 0) {
                count++;
                i += 2;  // skip cr
            }
        }
        // Skip braced groups
        if (content[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (content[i] == '{') depth++;
                else if (content[i] == '}') depth--;
                i++;
            }
            i--;
        }
    }
    // Last row (no trailing \cr)
    count++;

    AlignRow* rows = (AlignRow*)arena_alloc(arena, count * sizeof(AlignRow));
    for (int r = 0; r < count; r++) {
        rows[r] = AlignRow();
    }

    // Parse each row
    int row_idx = 0;
    size_t row_start = 0;

    for (size_t i = 0; i <= len; i++) {
        bool is_row_end = (i == len);
        if (i < len && content[i] == '\\') {
            if (i + 1 < len && content[i + 1] == '\\') {
                is_row_end = true;
            } else if (i + 2 < len && strncmp(content + i + 1, "cr", 2) == 0) {
                is_row_end = true;
            }
        }

        if (is_row_end && row_idx < count) {
            size_t row_end = i;
            AlignRow* parsed = parse_align_row(content + row_start, row_end - row_start, tmpl, arena);
            rows[row_idx] = *parsed;
            row_idx++;

            // Skip row terminator
            if (i < len && content[i] == '\\') {
                i++;
                if (i < len && content[i] == '\\') {
                    i++;
                } else if (i + 1 < len && strncmp(content + i, "cr", 2) == 0) {
                    i += 2;
                }
            }
            row_start = i;
        }

        // Skip braced groups
        if (i < len && content[i] == '{') {
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (content[i] == '{') depth++;
                else if (content[i] == '}') depth--;
                i++;
            }
            i--;
        }
    }

    *row_count = row_idx;
    log_debug("align: parsed %d rows", *row_count);
    return rows;
}

// ============================================================================
// Width/Height Calculation
// ============================================================================

float* compute_column_widths(
    AlignRow* rows,
    int row_count,
    int column_count,
    Arena* arena
) {
    float* widths = (float*)arena_alloc(arena, column_count * sizeof(float));

    // Initialize to zero
    for (int c = 0; c < column_count; c++) {
        widths[c] = 0;
    }

    // Find maximum width in each column
    for (int r = 0; r < row_count; r++) {
        if (rows[r].is_noalign) continue;

        for (int c = 0; c < rows[r].cell_count && c < column_count; c++) {
            AlignCell& cell = rows[r].cells[c];
            if (cell.span_count == 1) {
                if (cell.natural_width > widths[c]) {
                    widths[c] = cell.natural_width;
                }
            }
            // TODO: handle spanning cells
        }
    }

    log_debug("align: computed column widths");
    return widths;
}

void compute_row_heights(
    AlignRow* rows,
    int row_count
) {
    for (int r = 0; r < row_count; r++) {
        if (rows[r].is_noalign) {
            if (rows[r].noalign_content) {
                rows[r].row_height = rows[r].noalign_content->height;
                rows[r].row_depth = rows[r].noalign_content->depth;
            }
            continue;
        }

        float max_height = 0;
        float max_depth = 0;

        for (int c = 0; c < rows[r].cell_count; c++) {
            AlignCell& cell = rows[r].cells[c];
            if (cell.natural_height > max_height) {
                max_height = cell.natural_height;
            }
            if (cell.natural_depth > max_depth) {
                max_depth = cell.natural_depth;
            }
        }

        rows[r].row_height = max_height;
        rows[r].row_depth = max_depth;
    }

    log_debug("align: computed row heights");
}

// ============================================================================
// Table Building
// ============================================================================

TexNode* build_halign_row(
    AlignRow* row,
    float* column_widths,
    const AlignTemplate* tmpl,
    Arena* arena
) {
    if (row->is_noalign) {
        return row->noalign_content;
    }

    TexNode* hbox = make_hbox(arena);
    float total_width = 0;

    // Add initial tabskip
    if (tmpl->initial_tabskip.space != 0) {
        TexNode* skip = make_glue(arena, tmpl->initial_tabskip);
        hbox->append_child(skip);
        total_width += tmpl->initial_tabskip.space;
    }

    // Add cells with tabskip between them
    for (int c = 0; c < row->cell_count && c < tmpl->column_count; c++) {
        AlignCell& cell = row->cells[c];

        // Create cell box with specified width
        TexNode* cell_box = make_hbox(arena);
        cell_box->width = column_widths[c];
        cell_box->height = cell.natural_height;
        cell_box->depth = cell.natural_depth;

        if (cell.content) {
            cell_box->append_child(cell.content);
        }

        hbox->append_child(cell_box);
        total_width += column_widths[c];

        // Add tabskip after this column (except after last)
        if (c < tmpl->column_count - 1) {
            Glue tabskip = tmpl->columns[c].tabskip;
            if (tabskip.space != 0) {
                TexNode* skip = make_glue(arena, tabskip);
                hbox->append_child(skip);
                total_width += tabskip.space;
            }
        }
    }

    hbox->width = total_width;
    hbox->height = row->row_height;
    hbox->depth = row->row_depth;

    return hbox;
}

TexNode* build_halign(
    const AlignTemplate* tmpl,
    AlignRow* rows,
    int row_count,
    AlignSpec spec,
    Arena* arena
) {
    // Compute column widths
    float* col_widths = compute_column_widths(rows, row_count, tmpl->column_count, arena);

    // Compute row heights
    compute_row_heights(rows, row_count);

    // Build VBox containing all rows
    TexNode* vbox = make_vbox(arena);
    float total_height = 0;
    float total_depth = 0;

    for (int r = 0; r < row_count; r++) {
        TexNode* row_box = build_halign_row(&rows[r], col_widths, tmpl, arena);
        if (row_box) {
            vbox->append_child(row_box);
            if (r == 0) {
                total_height = row_box->height;
            } else {
                total_height += row_box->height + row_box->depth;
            }
            if (r == row_count - 1) {
                total_depth = row_box->depth;
            }
        }
    }

    // Calculate total width
    float total_width = tmpl->initial_tabskip.space;
    for (int c = 0; c < tmpl->column_count; c++) {
        total_width += col_widths[c];
        if (c < tmpl->column_count - 1) {
            total_width += tmpl->columns[c].tabskip.space;
        }
    }

    vbox->width = total_width;
    vbox->height = total_height;
    vbox->depth = total_depth;

    log_debug("align: built halign vbox w=%.1f h=%.1f d=%.1f",
              vbox->width, vbox->height, vbox->depth);
    return vbox;
}

TexNode* build_valign_column(
    AlignRow* rows,
    int row_count,
    int column_index,
    float* row_heights,
    Arena* arena
) {
    TexNode* vbox = make_vbox(arena);
    float total_height = 0;

    for (int r = 0; r < row_count; r++) {
        if (rows[r].is_noalign) {
            // Add noalign content
            if (rows[r].noalign_content) {
                vbox->append_child(rows[r].noalign_content);
                total_height += rows[r].noalign_content->height + rows[r].noalign_content->depth;
            }
            continue;
        }

        if (column_index < rows[r].cell_count) {
            AlignCell& cell = rows[r].cells[column_index];
            TexNode* cell_box = make_vbox(arena);
            cell_box->height = row_heights[r];
            cell_box->width = cell.natural_width;

            if (cell.content) {
                cell_box->append_child(cell.content);
            }

            vbox->append_child(cell_box);
            total_height += row_heights[r];
        }
    }

    vbox->height = total_height;
    return vbox;
}

TexNode* build_valign(
    const AlignTemplate* tmpl,
    AlignRow* rows,
    int row_count,
    AlignSpec spec,
    Arena* arena
) {
    // For valign, rows become columns
    // Compute row "widths" which become column heights
    float* row_heights = (float*)arena_alloc(arena, row_count * sizeof(float));
    compute_row_heights(rows, row_count);
    for (int r = 0; r < row_count; r++) {
        row_heights[r] = rows[r].row_height + rows[r].row_depth;
    }

    // Build HBox containing all columns
    TexNode* hbox = make_hbox(arena);
    float total_width = 0;
    float max_height = 0;

    // Add initial tabskip
    if (tmpl->initial_tabskip.space != 0) {
        TexNode* skip = make_glue(arena, tmpl->initial_tabskip);
        hbox->append_child(skip);
        total_width += tmpl->initial_tabskip.space;
    }

    for (int c = 0; c < tmpl->column_count; c++) {
        TexNode* col_box = build_valign_column(rows, row_count, c, row_heights, arena);
        hbox->append_child(col_box);
        total_width += col_box->width;
        if (col_box->height > max_height) {
            max_height = col_box->height;
        }

        // Add tabskip
        if (c < tmpl->column_count - 1) {
            Glue tabskip = tmpl->columns[c].tabskip;
            if (tabskip.space != 0) {
                TexNode* skip = make_glue(arena, tabskip);
                hbox->append_child(skip);
                total_width += tabskip.space;
            }
        }
    }

    hbox->width = total_width;
    hbox->height = max_height;

    log_debug("align: built valign hbox w=%.1f h=%.1f", hbox->width, hbox->height);
    return hbox;
}

// ============================================================================
// Special Features
// ============================================================================

TexNode* build_multispan_cell(
    AlignCell* cell,
    float* column_widths,
    int start_column,
    Arena* arena
) {
    // Calculate total width of spanned columns
    float total_width = 0;
    for (int c = 0; c < cell->span_count; c++) {
        total_width += column_widths[start_column + c];
        // TODO: add tabskip widths between columns
    }

    TexNode* box = make_hbox(arena);
    box->width = total_width;
    box->height = cell->natural_height;
    box->depth = cell->natural_depth;

    if (cell->content) {
        box->append_child(cell->content);
    }

    return box;
}

void apply_hidewidth(AlignCell* cell) {
    // Zero width for alignment purposes but content still rendered
    cell->natural_width = 0;
}

// ============================================================================
// Rule Building
// ============================================================================

TexNode* make_table_hrule(float width, float thickness, Arena* arena) {
    return make_rule(arena, width, thickness, 0);
}

TexNode* make_table_vrule(float height, float depth, float thickness, Arena* arena) {
    return make_rule(arena, thickness, height, depth);
}

} // namespace tex
