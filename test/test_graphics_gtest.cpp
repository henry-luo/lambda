// test_graphics_gtest.cpp - Unit tests for LaTeX graphics support
//
// Tests for tex_graphics.hpp (GraphicsElement IR and SVG output)
// NOTE: Picture and PGF driver tests require full Lambda runtime integration
//       and are tested separately via integration tests.

#include <gtest/gtest.h>
#include "lambda/tex/tex_graphics.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"
#include "lib/mempool.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class GraphicsTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    StrBuf* output;
    
    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        output = strbuf_new();
    }
    
    void TearDown() override {
        strbuf_free(output);
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// ============================================================================
// Transform2D Tests
// ============================================================================

TEST_F(GraphicsTest, Transform2D_Identity) {
    Transform2D t = Transform2D::identity();
    EXPECT_TRUE(t.is_identity());
    
    Point2D p(10, 20);
    Point2D result = t.apply(p);
    EXPECT_FLOAT_EQ(result.x, 10);
    EXPECT_FLOAT_EQ(result.y, 20);
}

TEST_F(GraphicsTest, Transform2D_Translate) {
    Transform2D t = Transform2D::translate(5, 10);
    EXPECT_FALSE(t.is_identity());
    
    Point2D p(10, 20);
    Point2D result = t.apply(p);
    EXPECT_FLOAT_EQ(result.x, 15);
    EXPECT_FLOAT_EQ(result.y, 30);
}

TEST_F(GraphicsTest, Transform2D_Scale) {
    Transform2D t = Transform2D::scale(2, 3);
    
    Point2D p(10, 20);
    Point2D result = t.apply(p);
    EXPECT_FLOAT_EQ(result.x, 20);
    EXPECT_FLOAT_EQ(result.y, 60);
}

TEST_F(GraphicsTest, Transform2D_Rotate90) {
    Transform2D t = Transform2D::rotate(90);
    
    Point2D p(1, 0);
    Point2D result = t.apply(p);
    EXPECT_NEAR(result.x, 0, 1e-5);
    EXPECT_NEAR(result.y, 1, 1e-5);
}

TEST_F(GraphicsTest, Transform2D_Multiply) {
    Transform2D t1 = Transform2D::translate(10, 0);
    Transform2D t2 = Transform2D::scale(2, 2);
    Transform2D combined = t1.multiply(t2);
    
    Point2D p(5, 5);
    // First scale: (10, 10), then translate: (20, 10)
    Point2D result = combined.apply(p);
    EXPECT_FLOAT_EQ(result.x, 20);
    EXPECT_FLOAT_EQ(result.y, 10);
}

// ============================================================================
// GraphicsElement Allocation Tests
// ============================================================================

TEST_F(GraphicsTest, Alloc_Canvas) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 200, 10, 20, 2.5f);
    
    ASSERT_NE(canvas, nullptr);
    EXPECT_EQ(canvas->type, GraphicsType::CANVAS);
    EXPECT_FLOAT_EQ(canvas->canvas.width, 100);
    EXPECT_FLOAT_EQ(canvas->canvas.height, 200);
    EXPECT_FLOAT_EQ(canvas->canvas.origin_x, 10);
    EXPECT_FLOAT_EQ(canvas->canvas.origin_y, 20);
    EXPECT_FLOAT_EQ(canvas->canvas.unitlength, 2.5f);
    EXPECT_TRUE(canvas->canvas.flip_y);
}

TEST_F(GraphicsTest, Alloc_Line) {
    GraphicsElement* line = graphics_line(arena, 0, 0, 100, 50);
    
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->type, GraphicsType::LINE);
    EXPECT_EQ(line->line.point_count, 2);
    EXPECT_FLOAT_EQ(line->line.points[0].x, 0);
    EXPECT_FLOAT_EQ(line->line.points[0].y, 0);
    EXPECT_FLOAT_EQ(line->line.points[1].x, 100);
    EXPECT_FLOAT_EQ(line->line.points[1].y, 50);
    EXPECT_FALSE(line->line.has_arrow);
}

TEST_F(GraphicsTest, Alloc_Circle) {
    GraphicsElement* circle = graphics_circle(arena, 50, 50, 25, false);
    
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->type, GraphicsType::CIRCLE);
    EXPECT_FLOAT_EQ(circle->circle.center.x, 50);
    EXPECT_FLOAT_EQ(circle->circle.center.y, 50);
    EXPECT_FLOAT_EQ(circle->circle.radius, 25);
    EXPECT_FALSE(circle->circle.filled);
}

TEST_F(GraphicsTest, Alloc_CircleFilled) {
    GraphicsElement* circle = graphics_circle(arena, 0, 0, 10, true);
    
    ASSERT_NE(circle, nullptr);
    EXPECT_TRUE(circle->circle.filled);
    EXPECT_STREQ(circle->style.fill_color, "#000000");
    EXPECT_STREQ(circle->style.stroke_color, "none");
}

TEST_F(GraphicsTest, Alloc_Rect) {
    GraphicsElement* rect = graphics_rect(arena, 10, 20, 100, 50, 5, 5);
    
    ASSERT_NE(rect, nullptr);
    EXPECT_EQ(rect->type, GraphicsType::RECT);
    EXPECT_FLOAT_EQ(rect->rect.corner.x, 10);
    EXPECT_FLOAT_EQ(rect->rect.corner.y, 20);
    EXPECT_FLOAT_EQ(rect->rect.width, 100);
    EXPECT_FLOAT_EQ(rect->rect.height, 50);
    EXPECT_FLOAT_EQ(rect->rect.rx, 5);
    EXPECT_FLOAT_EQ(rect->rect.ry, 5);
}

TEST_F(GraphicsTest, Alloc_QuadraticBezier) {
    GraphicsElement* bezier = graphics_qbezier(arena, 0, 0, 50, 100, 100, 0);
    
    ASSERT_NE(bezier, nullptr);
    EXPECT_EQ(bezier->type, GraphicsType::BEZIER);
    EXPECT_TRUE(bezier->bezier.is_quadratic);
    EXPECT_FLOAT_EQ(bezier->bezier.p0.x, 0);
    EXPECT_FLOAT_EQ(bezier->bezier.p0.y, 0);
    EXPECT_FLOAT_EQ(bezier->bezier.p1.x, 50);
    EXPECT_FLOAT_EQ(bezier->bezier.p1.y, 100);
    EXPECT_FLOAT_EQ(bezier->bezier.p2.x, 100);
    EXPECT_FLOAT_EQ(bezier->bezier.p2.y, 0);
}

TEST_F(GraphicsTest, Alloc_CubicBezier) {
    GraphicsElement* bezier = graphics_cbezier(arena, 0, 0, 25, 50, 75, 50, 100, 0);
    
    ASSERT_NE(bezier, nullptr);
    EXPECT_EQ(bezier->type, GraphicsType::BEZIER);
    EXPECT_FALSE(bezier->bezier.is_quadratic);
    EXPECT_FLOAT_EQ(bezier->bezier.p3.x, 100);
    EXPECT_FLOAT_EQ(bezier->bezier.p3.y, 0);
}

TEST_F(GraphicsTest, Alloc_Path) {
    GraphicsElement* path = graphics_path(arena, "M 0 0 L 100 100 Z");
    
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(path->type, GraphicsType::PATH);
    EXPECT_STREQ(path->path.d, "M 0 0 L 100 100 Z");
}

TEST_F(GraphicsTest, Alloc_Text) {
    GraphicsElement* text = graphics_text(arena, 50, 50, "Hello");
    
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->type, GraphicsType::TEXT);
    EXPECT_FLOAT_EQ(text->text.pos.x, 50);
    EXPECT_FLOAT_EQ(text->text.pos.y, 50);
    EXPECT_STREQ(text->text.text, "Hello");
}

// ============================================================================
// Tree Operations Tests
// ============================================================================

TEST_F(GraphicsTest, TreeOps_AppendChild) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* line1 = graphics_line(arena, 0, 0, 50, 50);
    GraphicsElement* line2 = graphics_line(arena, 50, 0, 0, 50);
    
    graphics_append_child(canvas, line1);
    graphics_append_child(canvas, line2);
    
    EXPECT_EQ(canvas->children, line1);
    EXPECT_EQ(line1->next, line2);
    EXPECT_EQ(line2->next, nullptr);
}

TEST_F(GraphicsTest, TreeOps_NestedGroups) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    Transform2D t = Transform2D::translate(10, 10);
    GraphicsElement* group = graphics_group(arena, &t);
    GraphicsElement* circle = graphics_circle(arena, 0, 0, 20, false);
    
    graphics_append_child(group, circle);
    graphics_append_child(canvas, group);
    
    EXPECT_EQ(canvas->children, group);
    EXPECT_EQ(group->children, circle);
}

// ============================================================================
// Bounding Box Tests
// ============================================================================

TEST_F(GraphicsTest, BoundingBox_Line) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* line = graphics_line(arena, 10, 20, 80, 70);
    graphics_append_child(canvas, line);
    
    BoundingBox bbox = graphics_bounding_box(canvas);
    
    EXPECT_FLOAT_EQ(bbox.min_x, 10);
    EXPECT_FLOAT_EQ(bbox.min_y, 20);
    EXPECT_FLOAT_EQ(bbox.max_x, 80);
    EXPECT_FLOAT_EQ(bbox.max_y, 70);
}

TEST_F(GraphicsTest, BoundingBox_Circle) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* circle = graphics_circle(arena, 50, 50, 25, false);
    graphics_append_child(canvas, circle);
    
    BoundingBox bbox = graphics_bounding_box(canvas);
    
    EXPECT_FLOAT_EQ(bbox.min_x, 25);
    EXPECT_FLOAT_EQ(bbox.min_y, 25);
    EXPECT_FLOAT_EQ(bbox.max_x, 75);
    EXPECT_FLOAT_EQ(bbox.max_y, 75);
}

// ============================================================================
// SVG Output Tests
// ============================================================================

TEST_F(GraphicsTest, SVG_EmptyCanvas) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 50, 0, 0, 1);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<svg"), nullptr);
    EXPECT_NE(strstr(svg, "width=\"100.00\""), nullptr);
    EXPECT_NE(strstr(svg, "height=\"50.00\""), nullptr);
    EXPECT_NE(strstr(svg, "</svg>"), nullptr);
}

TEST_F(GraphicsTest, SVG_Line) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* line = graphics_line(arena, 0, 0, 100, 100);
    graphics_append_child(canvas, line);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<line"), nullptr);
    EXPECT_NE(strstr(svg, "x1=\"0.00\""), nullptr);
    EXPECT_NE(strstr(svg, "y1=\"0.00\""), nullptr);
    EXPECT_NE(strstr(svg, "x2=\"100.00\""), nullptr);
    EXPECT_NE(strstr(svg, "y2=\"100.00\""), nullptr);
}

TEST_F(GraphicsTest, SVG_Circle) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* circle = graphics_circle(arena, 50, 50, 30, false);
    graphics_append_child(canvas, circle);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<circle"), nullptr);
    EXPECT_NE(strstr(svg, "cx=\"50.00\""), nullptr);
    EXPECT_NE(strstr(svg, "cy=\"50.00\""), nullptr);
    EXPECT_NE(strstr(svg, "r=\"30.00\""), nullptr);
}

TEST_F(GraphicsTest, SVG_Rect) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* rect = graphics_rect(arena, 10, 20, 60, 40, 0, 0);
    graphics_append_child(canvas, rect);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<rect"), nullptr);
    EXPECT_NE(strstr(svg, "x=\"10.00\""), nullptr);
    EXPECT_NE(strstr(svg, "y=\"20.00\""), nullptr);
    EXPECT_NE(strstr(svg, "width=\"60.00\""), nullptr);
    EXPECT_NE(strstr(svg, "height=\"40.00\""), nullptr);
}

TEST_F(GraphicsTest, SVG_Path) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* path = graphics_path(arena, "M 0 0 L 50 100 L 100 0 Z");
    graphics_append_child(canvas, path);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<path"), nullptr);
    EXPECT_NE(strstr(svg, "d=\"M 0 0 L 50 100 L 100 0 Z\""), nullptr);
}

TEST_F(GraphicsTest, SVG_QuadraticBezier) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* bezier = graphics_qbezier(arena, 0, 0, 50, 100, 100, 0);
    graphics_append_child(canvas, bezier);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<path"), nullptr);
    EXPECT_NE(strstr(svg, "M 0.00,0.00"), nullptr);
    EXPECT_NE(strstr(svg, "Q 50.00,100.00 100.00,0.00"), nullptr);
}

TEST_F(GraphicsTest, SVG_ArrowMarker) {
    GraphicsElement* canvas = graphics_canvas(arena, 100, 100, 0, 0, 1);
    GraphicsElement* line = graphics_line(arena, 0, 0, 100, 100);
    line->line.has_arrow = true;
    graphics_append_child(canvas, line);
    
    graphics_to_svg(canvas, output);
    
    const char* svg = output->str;
    EXPECT_NE(strstr(svg, "<defs>"), nullptr);
    EXPECT_NE(strstr(svg, "<marker id=\"arrow\""), nullptr);
    EXPECT_NE(strstr(svg, "marker-end=\"url(#arrow)\""), nullptr);
}

// NOTE: PGF Driver tests, Picture parsing tests, and PgfColor tests
// require additional Lambda runtime dependencies and are tested
// via integration tests in the full test suite.

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
