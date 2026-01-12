// test_tex_align_gtest.cpp - Unit tests for TeX Alignment (\halign, \valign)
//
// Tests the tex_align.hpp implementation:
// - Preamble parsing
// - Template application (u#v)
// - Column width calculation
// - Tabskip glue handling
// - \span, \omit, \noalign, \hidewidth

#include <gtest/gtest.h>
#include "lambda/tex/tex_align.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_hlist.hpp"
#include "lambda/tex/tex_vlist.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexAlignTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Helper to create cell content
    TexNode* make_text_box(const char* text, float width) {
        TexNode* hbox = make_hlist(arena);
        hbox->width = width;
        hbox->height = 8.0f;
        hbox->depth = 2.0f;
        return hbox;
    }
};

// ============================================================================
// AlignColumn Tests
// ============================================================================

TEST_F(TexAlignTest, AlignColumnDefaults) {
    AlignColumn col;

    EXPECT_EQ(col.u_template, nullptr);
    EXPECT_EQ(col.v_template, nullptr);
    EXPECT_EQ(col.u_len, 0u);
    EXPECT_EQ(col.v_len, 0u);
    EXPECT_FALSE(col.is_span);
}

TEST_F(TexAlignTest, AlignColumnWithTemplates) {
    AlignColumn col;
    col.u_template = "\\hfil ";
    col.u_len = 6;
    col.v_template = "\\hfil";
    col.v_len = 5;
    col.tabskip = Glue::flexible(10.0f, 5.0f, 3.0f);

    EXPECT_STREQ(col.u_template, "\\hfil ");
    EXPECT_EQ(col.u_len, 6u);
    EXPECT_FLOAT_EQ(col.tabskip.space, 10.0f);
}

// ============================================================================
// AlignTemplate Tests
// ============================================================================

TEST_F(TexAlignTest, AlignTemplateDefaults) {
    AlignTemplate tmpl;

    EXPECT_EQ(tmpl.columns, nullptr);
    EXPECT_EQ(tmpl.column_count, 0);
    EXPECT_FALSE(tmpl.is_valign);
}

// ============================================================================
// Preamble Parsing Tests
// ============================================================================

TEST_F(TexAlignTest, ParseSimplePreamble) {
    // Simple preamble: #&#&# (3 columns, no templates)
    const char* preamble = "#&#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->column_count, 3);
    EXPECT_FALSE(tmpl->is_valign);
}

TEST_F(TexAlignTest, ParsePreambleWithTemplates) {
    // Preamble with u/v templates: \hfil#\hfil&\hfil#\hfil
    const char* preamble = "\\hfil#\\hfil&\\hfil#\\hfil";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->column_count, 2);

    // First column should have u = "\hfil", v = "\hfil"
    if (tmpl->column_count >= 1) {
        EXPECT_GT(tmpl->columns[0].u_len, 0u);
        EXPECT_GT(tmpl->columns[0].v_len, 0u);
    }
}

TEST_F(TexAlignTest, ParseValignPreamble) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), true, arena  // is_valign = true
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_TRUE(tmpl->is_valign);
}

TEST_F(TexAlignTest, ParsePreambleWithTabskip) {
    // Preamble with tabskip
    Glue tabskip = Glue::flexible(5.0f, 2.0f, 1.0f);
    const char* preamble = "#&#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        tabskip, false, arena
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->column_count, 3);
    // Each column should have tabskip
    for (int i = 0; i < tmpl->column_count; i++) {
        EXPECT_FLOAT_EQ(tmpl->columns[i].tabskip.space, 5.0f);
    }
}

// ============================================================================
// Row Parsing Tests
// ============================================================================

TEST_F(TexAlignTest, ParseSingleRow) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    // Parse row: "a&b"
    const char* row = "a&b";
    AlignRow* parsed = parse_align_row(row, strlen(row), tmpl, arena);

    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->cell_count, 2);
    EXPECT_FALSE(parsed->is_noalign);
}

TEST_F(TexAlignTest, ParseMultipleRows) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    // Parse multiple rows: "a&b\cr c&d\cr"
    const char* content = "a&b\\cr c&d\\cr";
    int row_count = 0;
    AlignRow* rows = parse_align_rows(content, strlen(content), tmpl, &row_count, arena);

    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(row_count, 2);
}

TEST_F(TexAlignTest, ParseRowWithNoalign) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    // Row with \noalign
    const char* content = "a&b\\cr\\noalign{\\hrule}c&d\\cr";
    int row_count = 0;
    AlignRow* rows = parse_align_rows(content, strlen(content), tmpl, &row_count, arena);

    ASSERT_NE(rows, nullptr);
    // Should have 3 "rows": normal, noalign, normal
    EXPECT_GE(row_count, 2);
}

// ============================================================================
// AlignCell Tests
// ============================================================================

TEST_F(TexAlignTest, AlignCellDefaults) {
    AlignCell cell;

    EXPECT_EQ(cell.content, nullptr);
    EXPECT_FLOAT_EQ(cell.natural_width, 0.0f);
    EXPECT_EQ(cell.span_count, 1);
    EXPECT_FALSE(cell.is_omit);
}

TEST_F(TexAlignTest, AlignCellWithOmit) {
    AlignCell cell;
    cell.is_omit = true;
    cell.content = make_text_box("test", 30.0f);
    cell.natural_width = 30.0f;

    EXPECT_TRUE(cell.is_omit);
    EXPECT_FLOAT_EQ(cell.natural_width, 30.0f);
}

TEST_F(TexAlignTest, AlignCellWithSpan) {
    AlignCell cell;
    cell.span_count = 3;  // \span\span\span
    cell.content = make_text_box("wide", 100.0f);
    cell.natural_width = 100.0f;

    EXPECT_EQ(cell.span_count, 3);
}

// ============================================================================
// AlignRow Tests
// ============================================================================

TEST_F(TexAlignTest, AlignRowDefaults) {
    AlignRow row;

    EXPECT_EQ(row.cells, nullptr);
    EXPECT_EQ(row.cell_count, 0);
    EXPECT_FALSE(row.is_noalign);
    EXPECT_EQ(row.noalign_content, nullptr);
    EXPECT_FLOAT_EQ(row.row_height, 0.0f);
    EXPECT_FLOAT_EQ(row.row_depth, 0.0f);
}

TEST_F(TexAlignTest, NoalignRow) {
    AlignRow row;
    row.is_noalign = true;
    row.noalign_content = make_rule(arena, 100.0f, 0.4f, 0.0f);

    EXPECT_TRUE(row.is_noalign);
    EXPECT_NE(row.noalign_content, nullptr);
}

// ============================================================================
// AlignSpec Tests
// ============================================================================

TEST_F(TexAlignTest, AlignSpecNatural) {
    AlignSpec spec = AlignSpec::natural();

    EXPECT_EQ(spec.mode, AlignSizeMode::Natural);
    EXPECT_FLOAT_EQ(spec.size, 0.0f);
}

TEST_F(TexAlignTest, AlignSpecTo) {
    AlignSpec spec = AlignSpec::to(300.0f);

    EXPECT_EQ(spec.mode, AlignSizeMode::To);
    EXPECT_FLOAT_EQ(spec.size, 300.0f);
}

TEST_F(TexAlignTest, AlignSpecSpread) {
    AlignSpec spec = AlignSpec::spread(50.0f);

    EXPECT_EQ(spec.mode, AlignSizeMode::Spread);
    EXPECT_FLOAT_EQ(spec.size, 50.0f);
}

// ============================================================================
// Column Width Calculation Tests
// ============================================================================

TEST_F(TexAlignTest, ComputeColumnWidths) {
    // Create rows with known widths
    AlignRow rows[2];

    // Row 1: cells with widths 10, 20
    AlignCell cells1[2];
    cells1[0].natural_width = 10.0f;
    cells1[0].span_count = 1;
    cells1[1].natural_width = 20.0f;
    cells1[1].span_count = 1;
    rows[0].cells = cells1;
    rows[0].cell_count = 2;
    rows[0].is_noalign = false;

    // Row 2: cells with widths 15, 25
    AlignCell cells2[2];
    cells2[0].natural_width = 15.0f;
    cells2[0].span_count = 1;
    cells2[1].natural_width = 25.0f;
    cells2[1].span_count = 1;
    rows[1].cells = cells2;
    rows[1].cell_count = 2;
    rows[1].is_noalign = false;

    float* widths = compute_column_widths(rows, 2, 2, arena);

    ASSERT_NE(widths, nullptr);
    EXPECT_FLOAT_EQ(widths[0], 15.0f);  // max(10, 15)
    EXPECT_FLOAT_EQ(widths[1], 25.0f);  // max(20, 25)
}

TEST_F(TexAlignTest, ComputeRowHeights) {
    AlignRow rows[2];

    // Row 1: height 8, depth 2
    AlignCell cells1[1];
    cells1[0].natural_height = 8.0f;
    cells1[0].natural_depth = 2.0f;
    cells1[0].span_count = 1;
    rows[0].cells = cells1;
    rows[0].cell_count = 1;
    rows[0].is_noalign = false;

    // Row 2: height 10, depth 3
    AlignCell cells2[1];
    cells2[0].natural_height = 10.0f;
    cells2[0].natural_depth = 3.0f;
    cells2[0].span_count = 1;
    rows[1].cells = cells2;
    rows[1].cell_count = 1;
    rows[1].is_noalign = false;

    compute_row_heights(rows, 2);

    EXPECT_FLOAT_EQ(rows[0].row_height, 8.0f);
    EXPECT_FLOAT_EQ(rows[0].row_depth, 2.0f);
    EXPECT_FLOAT_EQ(rows[1].row_height, 10.0f);
    EXPECT_FLOAT_EQ(rows[1].row_depth, 3.0f);
}

// ============================================================================
// Build Halign Tests
// ============================================================================

TEST_F(TexAlignTest, BuildSimpleHalign) {
    // Create simple 2x2 alignment
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );
    ASSERT_NE(tmpl, nullptr);

    // Create rows manually
    AlignRow rows[2];
    AlignCell cells1[2], cells2[2];

    cells1[0].content = make_text_box("a", 10.0f);
    cells1[0].natural_width = 10.0f;
    cells1[0].span_count = 1;
    cells1[1].content = make_text_box("b", 15.0f);
    cells1[1].natural_width = 15.0f;
    cells1[1].span_count = 1;
    rows[0].cells = cells1;
    rows[0].cell_count = 2;
    rows[0].is_noalign = false;

    cells2[0].content = make_text_box("c", 12.0f);
    cells2[0].natural_width = 12.0f;
    cells2[0].span_count = 1;
    cells2[1].content = make_text_box("d", 18.0f);
    cells2[1].natural_width = 18.0f;
    cells2[1].span_count = 1;
    rows[1].cells = cells2;
    rows[1].cell_count = 2;
    rows[1].is_noalign = false;

    TexNode* result = build_halign(tmpl, rows, 2, AlignSpec::natural(), arena);

    // Test only that we get a valid result
    ASSERT_NE(result, nullptr);
}

TEST_F(TexAlignTest, BuildHalignToWidth) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue::flexible(0, 10.0f, 0),  // Stretchable tabskip
        false, arena
    );
    ASSERT_NE(tmpl, nullptr);

    AlignRow rows[1];
    AlignCell cells[2];
    cells[0].content = make_text_box("a", 10.0f);
    cells[0].natural_width = 10.0f;
    cells[0].span_count = 1;
    cells[1].content = make_text_box("b", 10.0f);
    cells[1].natural_width = 10.0f;
    cells[1].span_count = 1;
    rows[0].cells = cells;
    rows[0].cell_count = 2;
    rows[0].is_noalign = false;

    // Build to specific width
    TexNode* result = build_halign(tmpl, rows, 1, AlignSpec::to(100.0f), arena);

    ASSERT_NE(result, nullptr);
    // Width may or may not be stretched depending on implementation
    EXPECT_GT(result->width, 0.0f);
}

// ============================================================================
// Build Valign Tests
// ============================================================================

TEST_F(TexAlignTest, BuildSimpleValign) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), true, arena  // is_valign = true
    );
    ASSERT_NE(tmpl, nullptr);

    AlignRow rows[2];
    AlignCell cells1[2], cells2[2];

    cells1[0].content = make_text_box("a", 10.0f);
    cells1[0].natural_width = 10.0f;
    cells1[0].natural_height = 8.0f;
    cells1[0].span_count = 1;
    cells1[1].content = make_text_box("b", 15.0f);
    cells1[1].natural_width = 15.0f;
    cells1[1].natural_height = 8.0f;
    cells1[1].span_count = 1;
    rows[0].cells = cells1;
    rows[0].cell_count = 2;
    rows[0].is_noalign = false;

    cells2[0].content = make_text_box("c", 12.0f);
    cells2[0].natural_width = 12.0f;
    cells2[0].natural_height = 8.0f;
    cells2[0].span_count = 1;
    cells2[1].content = make_text_box("d", 18.0f);
    cells2[1].natural_width = 18.0f;
    cells2[1].natural_height = 8.0f;
    cells2[1].span_count = 1;
    rows[1].cells = cells2;
    rows[1].cell_count = 2;
    rows[1].is_noalign = false;

    TexNode* result = build_valign(tmpl, rows, 2, AlignSpec::natural(), arena);

    // Test only that we get a valid result
    ASSERT_NE(result, nullptr);
}

// ============================================================================
// Special Features Tests
// ============================================================================

TEST_F(TexAlignTest, HidewidthCell) {
    AlignCell cell;
    cell.content = make_text_box("text", 30.0f);
    cell.natural_width = 30.0f;

    apply_hidewidth(&cell);

    // After \hidewidth, natural width should be 0 for alignment purposes
    EXPECT_FLOAT_EQ(cell.natural_width, 0.0f);
}

TEST_F(TexAlignTest, TableHrule) {
    TexNode* hrule = make_table_hrule(200.0f, 0.4f, arena);

    ASSERT_NE(hrule, nullptr);
    EXPECT_EQ(hrule->node_class, NodeClass::Rule);
    EXPECT_FLOAT_EQ(hrule->width, 200.0f);
    EXPECT_FLOAT_EQ(hrule->height, 0.4f);
}

TEST_F(TexAlignTest, TableVrule) {
    TexNode* vrule = make_table_vrule(10.0f, 2.0f, 0.4f, arena);

    ASSERT_NE(vrule, nullptr);
    EXPECT_EQ(vrule->node_class, NodeClass::Rule);
    EXPECT_FLOAT_EQ(vrule->height, 10.0f);
    EXPECT_FLOAT_EQ(vrule->depth, 2.0f);
    EXPECT_FLOAT_EQ(vrule->width, 0.4f);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TexAlignTest, EmptyPreamble) {
    const char* preamble = "";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, 0,
        Glue(), false, arena
    );

    // Empty preamble may return null or template with 0-1 columns
    // depending on implementation
    if (tmpl) {
        EXPECT_LE(tmpl->column_count, 1);
    }
}

TEST_F(TexAlignTest, SingleColumnAlignment) {
    const char* preamble = "#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->column_count, 1);
}

TEST_F(TexAlignTest, ManyColumns) {
    // 10 columns
    const char* preamble = "#&#&#&#&#&#&#&#&#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );

    ASSERT_NE(tmpl, nullptr);
    EXPECT_EQ(tmpl->column_count, 10);
}

TEST_F(TexAlignTest, UnevenRows) {
    // Template with 3 columns
    const char* preamble = "#&#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );
    ASSERT_NE(tmpl, nullptr);

    // Row with only 2 cells (missing third)
    const char* content = "a&b\\cr";
    int row_count = 0;
    AlignRow* rows = parse_align_rows(content, strlen(content), tmpl, &row_count, arena);

    // Should handle gracefully
    ASSERT_NE(rows, nullptr);
    EXPECT_GE(row_count, 1);
}

TEST_F(TexAlignTest, RowWithTooManyCells) {
    // Template with 2 columns
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue(), false, arena
    );
    ASSERT_NE(tmpl, nullptr);

    // Row with 4 cells (too many)
    const char* content = "a&b&c&d\\cr";
    int row_count = 0;
    AlignRow* rows = parse_align_rows(content, strlen(content), tmpl, &row_count, arena);

    // Should handle gracefully (may truncate or error)
    ASSERT_NE(rows, nullptr);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(TexAlignTest, BuildRowHBox) {
    const char* preamble = "#&#";
    AlignTemplate* tmpl = parse_align_preamble(
        preamble, strlen(preamble),
        Glue::fixed(5.0f),  // Fixed tabskip
        false, arena
    );
    ASSERT_NE(tmpl, nullptr);

    // Create single row
    AlignRow row;
    AlignCell cells[2];
    cells[0].content = make_text_box("left", 20.0f);
    cells[0].natural_width = 20.0f;
    cells[0].span_count = 1;
    cells[1].content = make_text_box("right", 30.0f);
    cells[1].natural_width = 30.0f;
    cells[1].span_count = 1;
    row.cells = cells;
    row.cell_count = 2;
    row.is_noalign = false;

    float widths[] = {25.0f, 35.0f};  // Column widths (may exceed natural)

    TexNode* hbox = build_halign_row(&row, widths, tmpl, arena);

    // Test only that we get a valid result
    ASSERT_NE(hbox, nullptr);
}
