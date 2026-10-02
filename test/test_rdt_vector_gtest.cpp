#include <gtest/gtest.h>

#include "../radiant/render.hpp"

struct SvgPathTrace {
    int count;
    RdtPathCommand commands[16];
    float args[16][6];
};

static bool trace_svg_path(void* context, RdtPathCommand command,
                            const float* args, int arg_count) {
    SvgPathTrace* trace = (SvgPathTrace*)context;
    if (trace->count >= 16 || arg_count > 6) return false;
    trace->commands[trace->count] = command;
    for (int i = 0; i < arg_count; i++) trace->args[trace->count][i] = args[i];
    trace->count++;
    return true;
}

TEST(SvgPathTest, PreservesCompleteSegmentsBeforeMalformedParameters) {
    const char* cases[] = {
        "M10 10L20 20 ?", "M10 10L20 20X30 30", "M10 10L20 20 30",
        "M10 10L20 20C40 40", "M10 10L20 20L", "M10 10L20 20H+",
        "M10 10L20 20A10 10 0 2 0 30 30",
        "M10 10L20 20A10 10 0 0 3 30 30",
        "M10 10L20 20L1e999 40", "M10 10L20 20L30,,40",
        "M10 10L20 20,,L30 30"
    };
    for (const char* d : cases) {
        SCOPED_TRACE(d);
        RdtPath* path = svg_parse_path_d(d);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 2);
        EXPECT_EQ(trace.commands[0], RDT_PATH_MOVE);
        EXPECT_EQ(trace.commands[1], RDT_PATH_LINE);
        EXPECT_FLOAT_EQ(trace.args[1][0], 20.0f);
        EXPECT_FLOAT_EQ(trace.args[1][1], 20.0f);
    }
}

TEST(SvgPathTest, RequiresInitialMovetoAndTerminatesAfterClosepath) {
    const char* invalid[] = {"L10 10L20 20", "M10", "M,10 10", "M0x10 20L30 30"};
    for (const char* d : invalid) {
        SCOPED_TRACE(d);
        RdtPath* path = svg_parse_path_d(d);
        EXPECT_EQ(path, nullptr);
        if (path) rdt_path_free(path);
    }
    RdtPath* path = svg_parse_path_d("M10 10L20 20Z30 30");
    ASSERT_NE(path, nullptr);
    SvgPathTrace trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    ASSERT_EQ(trace.count, 3);
    EXPECT_EQ(trace.commands[2], RDT_PATH_CLOSE);
}

TEST(SvgPathTest, ParsesCompactDecimalExponentAndArcFlagParameters) {
    RdtPath* path = svg_parse_path_d("M.5.5L1e1-2e1");
    ASSERT_NE(path, nullptr);
    SvgPathTrace trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    ASSERT_EQ(trace.count, 2);
    EXPECT_FLOAT_EQ(trace.args[0][0], 0.5f);
    EXPECT_FLOAT_EQ(trace.args[0][1], 0.5f);
    EXPECT_FLOAT_EQ(trace.args[1][0], 10.0f);
    EXPECT_FLOAT_EQ(trace.args[1][1], -20.0f);

    path = svg_parse_path_d("M10 10A10 10 0 0130 30");
    ASSERT_NE(path, nullptr);
    trace = {};
    EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
    rdt_path_free(path);
    EXPECT_GT(trace.count, 1);
}

TEST(SvgPathTest, SmoothCurvesReflectOnlyTheMatchingControlPointKind) {
    const char* cases[] = {
        "M0 0Q10 10 20 0S30 -10 40 0",
        "M0 0C0 10 10 10 10 0T20 0"
    };
    const float expected_x[] = {20.0f, 10.0f};
    for (int i = 0; i < 2; i++) {
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 3);
        EXPECT_EQ(trace.commands[2], RDT_PATH_CUBIC);
        EXPECT_FLOAT_EQ(trace.args[2][0], expected_x[i]);
        EXPECT_FLOAT_EQ(trace.args[2][1], 0.0f);
    }
}

TEST(SvgPathTest, EitherZeroArcRadiusEmitsALine) {
    const char* cases[] = {"M10 10A0 10 0 0 1 30 30", "M10 10A10 0 0 0 1 30 30"};
    for (const char* d : cases) {
        RdtPath* path = svg_parse_path_d(d);
        ASSERT_NE(path, nullptr);
        SvgPathTrace trace = {};
        EXPECT_TRUE(rdt_path_visit(path, trace_svg_path, &trace));
        rdt_path_free(path);
        ASSERT_EQ(trace.count, 2);
        EXPECT_EQ(trace.commands[1], RDT_PATH_LINE);
        EXPECT_FLOAT_EQ(trace.args[1][0], 30.0f);
        EXPECT_FLOAT_EQ(trace.args[1][1], 30.0f);
    }
}

TEST(RdtPathBoundsTest, UsesCurveExtremaAndRestoresClosedSubpathOrigin) {
    const char* cases[] = {
        "M10 170C10 90 110 90 110 170Z",
        "M0 0C100 100 100 -100 0 0",
        "M10 10L20 20ZC10 30 30 30 30 10",
        "M1000 1000M10 20L30 40"
    };
    const float expected[][4] = {
        {10.0f, 110.0f, 110.0f, 170.0f},
        {0.0f, -28.867514f, 75.0f, 28.867514f},
        {10.0f, 10.0f, 30.0f, 25.0f},
        {10.0f, 20.0f, 30.0f, 40.0f}
    };
    for (int i = 0; i < 4; i++) {
        SCOPED_TRACE(cases[i]);
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        for (int j = 0; j < 4; j++) EXPECT_NEAR(bounds[j], expected[i][j], 0.0001f);
    }
}

TEST(RdtPathBoundsTest, HandlesLinearAndDegenerateCubicDerivatives) {
    const char* cases[] = {"M0 0C0 10 0 10 0 0", "M0 0C10 10 20 20 30 30"};
    const float expected[][4] = {{0.0f, 0.0f, 0.0f, 7.5f}, {0.0f, 0.0f, 30.0f, 30.0f}};
    for (int i = 0; i < 2; i++) {
        RdtPath* path = svg_parse_path_d(cases[i]);
        ASSERT_NE(path, nullptr);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        for (int j = 0; j < 4; j++) EXPECT_NEAR(bounds[j], expected[i][j], 0.0001f);
    }
    RdtPath* path = rdt_path_new();
    ASSERT_NE(path, nullptr);
    rdt_path_move_to(path, 10.0f, 20.0f);
    float bounds[4] = {};
    EXPECT_FALSE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
    rdt_path_free(path);
}

TEST(RdtPathBoundsTest, ContinuesFromAppendedClosedPrimitives) {
    for (int i = 0; i < 2; i++) {
        RdtPath* path = rdt_path_new();
        ASSERT_NE(path, nullptr);
        if (i == 0) rdt_path_add_rect(path, 10.0f, 10.0f, 20.0f, 20.0f, 0.0f, 0.0f);
        else rdt_path_add_circle(path, 20.0f, 20.0f, 10.0f, 10.0f);
        rdt_path_cubic_to(path, 30.0f, 50.0f, 70.0f, 50.0f, 70.0f, 10.0f);
        float bounds[4] = {};
        EXPECT_TRUE(rdt_path_get_bounds(path, &bounds[0], &bounds[1], &bounds[2], &bounds[3]));
        rdt_path_free(path);
        EXPECT_FLOAT_EQ(bounds[0], 10.0f); EXPECT_FLOAT_EQ(bounds[1], 10.0f);
        EXPECT_FLOAT_EQ(bounds[2], 70.0f); EXPECT_FLOAT_EQ(bounds[3], 40.0f);
    }
}

TEST(RdtVectorTest, ComposesOneClipMaskPerStableBatch) {
    rdt_engine_init(0);

    uint32_t pixels[24 * 12] = {};
    RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 24, 12, 24);

    RdtPath* outer = rdt_path_new();
    RdtPath* inner = rdt_path_new();
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    rdt_path_add_rect(outer, 2.0f, 2.0f, 16.0f, 8.0f, 0.0f, 0.0f);
    rdt_path_add_rect(inner, 8.0f, 2.0f, 8.0f, 8.0f, 0.0f, 0.0f);

    Color red = {};
    red.r = 255;
    red.a = 255;
    Color blue = {};
    blue.b = 255;
    blue.a = 255;

    rdt_vector_begin_batch(&vector);
    rdt_push_clip(&vector, outer, nullptr);
    // Many paints under one unchanged clip must share one composed mask.
    for (int x = 0; x < 24; x += 2) {
        rdt_fill_rect(&vector, (float)x, 0.0f, 2.0f, 12.0f, red);
    }
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 0u);
    rdt_vector_flush_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 1u);
    EXPECT_NE(pixels[4 * 24 + 4], 0u);
    EXPECT_EQ(pixels[4 * 24 + 0], 0u);

    rdt_push_clip(&vector, inner, nullptr);
    rdt_fill_rect(&vector, 0.0f, 0.0f, 24.0f, 12.0f, blue);
    rdt_pop_clip(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);
    EXPECT_EQ(pixels[4 * 24 + 4], pixels[4 * 24 + 6]);
    EXPECT_NE(pixels[4 * 24 + 10], pixels[4 * 24 + 4]);
    EXPECT_EQ(pixels[4 * 24 + 20], 0u);

    rdt_pop_clip(&vector);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(rdt_vector_clip_mask_count(&vector), 2u);

    rdt_path_free(inner);
    rdt_path_free(outer);
    rdt_vector_destroy(&vector);
    rdt_engine_term();
}
