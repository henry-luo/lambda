/**
 * CSS Animation Unit Tests
 *
 * Tests: property interpolation (float, color, transform),
 * @keyframes parsing, keyframe registry, animation creation/tick.
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

#include "../radiant/css_animation.h"
#include "../radiant/view.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arena.h"
}

// Stubs for unresolved symbols in standalone test builds
void dirty_mark_rect(DirtyTracker*, float, float, float, float) {}

// Helper: set up a stylesheet with one @keyframes rule on a doc
static void setup_keyframes_sheet(DomDocument* doc, CssStylesheet* sheet,
                                   CssRule* rule, CssRule** rule_ptr,
                                   CssStylesheet** sheet_ptr,
                                   const char* content) {
    memset(sheet, 0, sizeof(*sheet));
    sheet->pool = doc->pool;
    sheet->disabled = false;

    memset(rule, 0, sizeof(*rule));
    rule->type = CSS_RULE_KEYFRAMES;
    rule->data.generic_rule.name = "keyframes";
    rule->data.generic_rule.content = content;

    *rule_ptr = rule;
    sheet->rules = rule_ptr;
    sheet->rule_count = 1;

    *sheet_ptr = sheet;
    doc->stylesheets = sheet_ptr;
    doc->stylesheet_count = 1;
}

// ============================================================================
// Float Interpolation Tests
// ============================================================================

TEST(CssInterpolation, FloatLerp) {
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(css_interpolate_float(10.0f, 20.0f, 0.3f), 13.0f);
}

TEST(CssInterpolation, FloatNegative) {
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 0.0f), -10.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 1.0f), 10.0f);
}

// ============================================================================
// Color Interpolation Tests
// ============================================================================

TEST(CssInterpolation, ColorLerp) {
    Color a = {0}; a.r = 0; a.g = 0; a.b = 0; a.a = 255;
    Color b = {0}; b.r = 255; b.g = 255; b.b = 255; b.a = 255;

    Color mid = css_interpolate_color(a, b, 0.5f);
    EXPECT_EQ(mid.r, 128);
    EXPECT_EQ(mid.g, 128);
    EXPECT_EQ(mid.b, 128);
    EXPECT_EQ(mid.a, 255);
}

TEST(CssInterpolation, ColorAtBoundaries) {
    Color a = {0}; a.r = 100; a.g = 50; a.b = 200; a.a = 255;
    Color b = {0}; b.r = 200; b.g = 150; b.b = 100; b.a = 128;

    Color at0 = css_interpolate_color(a, b, 0.0f);
    EXPECT_EQ(at0.r, 100);
    EXPECT_EQ(at0.g, 50);
    EXPECT_EQ(at0.b, 200);
    EXPECT_EQ(at0.a, 255);

    Color at1 = css_interpolate_color(a, b, 1.0f);
    EXPECT_EQ(at1.r, 200);
    EXPECT_EQ(at1.g, 150);
    EXPECT_EQ(at1.b, 100);
    EXPECT_EQ(at1.a, 128);
}

TEST(CssInterpolation, ColorRedToBlue) {
    Color red = {0}; red.r = 255; red.g = 0; red.b = 0; red.a = 255;
    Color blue = {0}; blue.r = 0; blue.g = 0; blue.b = 255; blue.a = 255;

    Color quarter = css_interpolate_color(red, blue, 0.25f);
    EXPECT_NEAR(quarter.r, 191, 1);
    EXPECT_EQ(quarter.g, 0);
    EXPECT_NEAR(quarter.b, 64, 1);
}

// ============================================================================
// Keyframe Parsing Tests
// ============================================================================

class KeyframeParsingTest : public ::testing::Test {
protected:
    Pool* pool;
    DomDocument doc;
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;

    void SetUp() override {
        pool = pool_create();
        memset(&doc, 0, sizeof(doc));
        doc.pool = pool;
        doc.arena = arena_create_default(pool);
    }
    void TearDown() override {
        if (doc.arena) arena_destroy(doc.arena);
        pool_destroy(pool);
    }
    void setupKeyframes(const char* content) {
        setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr, content);
    }
};

TEST_F(KeyframeParsingTest, SimpleOpacityFromTo) {
    setupKeyframes("fadeIn { from { opacity: 0; } to { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->count, 1);

    CssKeyframes* kf = keyframe_registry_find(registry, "fadeIn");
    ASSERT_NE(kf, nullptr);
    EXPECT_STREQ(kf->name, "fadeIn");
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_FLOAT_EQ(kf->stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(kf->stops[1].offset, 1.0f);

    EXPECT_EQ(kf->stops[0].property_count, 1);
    EXPECT_EQ(kf->stops[0].properties[0].property_id, CSS_PROPERTY_OPACITY);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_FLOAT);
    EXPECT_FLOAT_EQ(kf->stops[0].properties[0].value.f, 0.0f);

    EXPECT_EQ(kf->stops[1].property_count, 1);
    EXPECT_EQ(kf->stops[1].properties[0].property_id, CSS_PROPERTY_OPACITY);
    EXPECT_FLOAT_EQ(kf->stops[1].properties[0].value.f, 1.0f);
}

TEST_F(KeyframeParsingTest, PercentageStops) {
    setupKeyframes("pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "pulse");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 3);

    EXPECT_FLOAT_EQ(kf->stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(kf->stops[1].offset, 0.5f);
    EXPECT_FLOAT_EQ(kf->stops[2].offset, 1.0f);

    EXPECT_FLOAT_EQ(kf->stops[1].properties[0].value.f, 0.5f);
}

TEST_F(KeyframeParsingTest, TransformKeyframes) {
    setupKeyframes("slideIn { from { transform: translateX(-100px); } to { transform: translateX(0px); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "slideIn");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].properties[0].property_id, CSS_PROPERTY_TRANSFORM);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_TRANSFORM);
    TransformFunction* tf = kf->stops[0].properties[0].value.transform;
    ASSERT_NE(tf, nullptr);
    EXPECT_EQ(tf->type, TRANSFORM_TRANSLATEX);
    EXPECT_FLOAT_EQ(tf->params.translate.x, -100.0f);
}

TEST_F(KeyframeParsingTest, ColorKeyframes) {
    setupKeyframes("colorShift { from { background-color: #ff0000; } to { background-color: #0000ff; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "colorShift");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].properties[0].property_id, CSS_PROPERTY_BACKGROUND_COLOR);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_COLOR);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.r, 255);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.g, 0);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.b, 0);

    EXPECT_EQ(kf->stops[1].properties[0].value.color.r, 0);
    EXPECT_EQ(kf->stops[1].properties[0].value.color.g, 0);
    EXPECT_EQ(kf->stops[1].properties[0].value.color.b, 255);
}

TEST_F(KeyframeParsingTest, MultipleProperties) {
    setupKeyframes("fadeSlide { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0px); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "fadeSlide");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].property_count, 2);
    EXPECT_EQ(kf->stops[1].property_count, 2);
}

TEST_F(KeyframeParsingTest, RegistryFindMissing) {
    doc.stylesheets = NULL;
    doc.stylesheet_count = 0;

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->count, 0);

    CssKeyframes* kf = keyframe_registry_find(registry, "nonExistent");
    EXPECT_EQ(kf, nullptr);
}

// ============================================================================
// Animation Tick Tests
// ============================================================================

class AnimationTickTest : public ::testing::Test {
protected:
    Pool* pool;
    AnimationScheduler* scheduler;
    DomDocument doc;

    void SetUp() override {
        timing_init_presets();
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
        memset(&doc, 0, sizeof(doc));
        doc.pool = pool;
        doc.arena = arena_create_default(pool);
    }
    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        if (doc.arena) arena_destroy(doc.arena);
        pool_destroy(pool);
    }

    struct MockElement {
        uint8_t buf[4096];
        InlineProp in_line;
    };

    DomElement* createMockElement(MockElement* mock) {
        memset(mock, 0, sizeof(*mock));
        DomElement* element = (DomElement*)mock->buf;
        element->doc = &doc;
        ((ViewSpan*)element)->in_line = &mock->in_line;
        return element;
    }

    CssAnimProp defaultAnimProp(const char* name, float duration) {
        CssAnimProp ap;
        memset(&ap, 0, sizeof(ap));
        ap.name = name;
        ap.duration = duration;
        ap.iteration_count = 1;
        ap.direction = ANIM_DIR_NORMAL;
        ap.fill_mode = ANIM_FILL_FORWARDS;
        ap.play_state = ANIM_PLAY_RUNNING;
        ap.timing.type = TIMING_LINEAR;
        return ap;
    }
};

TEST_F(AnimationTickTest, OpacityAnimation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp prop_from;
    prop_from.property_id = CSS_PROPERTY_OPACITY;
    prop_from.value_type = ANIM_VAL_FLOAT;
    prop_from.value.f = 0.0f;

    CssAnimatedProp prop_to;
    prop_to.property_id = CSS_PROPERTY_OPACITY;
    prop_to.value_type = ANIM_VAL_FLOAT;
    prop_to.value.f = 1.0f;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, &prop_from, 1, NULL};
    stops[1] = {1.0f, &prop_to, 1, NULL};

    CssKeyframes kf = {"testFade", stops, 2};

    CssAnimProp ap = defaultAnimProp("testFade", 1.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->type, ANIM_CSS_ANIMATION);

    css_animation_tick(inst, 0.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);

    css_animation_tick(inst, 0.5f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.5f);

    css_animation_tick(inst, 1.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
}

TEST_F(AnimationTickTest, ColorAnimation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp prop_from;
    memset(&prop_from, 0, sizeof(prop_from));
    prop_from.property_id = CSS_PROPERTY_COLOR;
    prop_from.value_type = ANIM_VAL_COLOR;
    prop_from.value.color.r = 255; prop_from.value.color.a = 255;

    CssAnimatedProp prop_to;
    memset(&prop_to, 0, sizeof(prop_to));
    prop_to.property_id = CSS_PROPERTY_COLOR;
    prop_to.value_type = ANIM_VAL_COLOR;
    prop_to.value.color.b = 255; prop_to.value.color.a = 255;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, &prop_from, 1, NULL};
    stops[1] = {1.0f, &prop_to, 1, NULL};

    CssKeyframes kf = {"colorAnim", stops, 2};

    CssAnimProp ap = defaultAnimProp("colorAnim", 1.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    css_animation_tick(inst, 0.5f);
    EXPECT_EQ(mock.in_line.color.r, 128);
    EXPECT_EQ(mock.in_line.color.g, 0);
    EXPECT_EQ(mock.in_line.color.b, 128);
    EXPECT_EQ(mock.in_line.color.a, 255);
}

TEST_F(AnimationTickTest, ThreeStopInterpolation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp props[3];
    for (int i = 0; i < 3; i++) {
        props[i].property_id = CSS_PROPERTY_OPACITY;
        props[i].value_type = ANIM_VAL_FLOAT;
    }
    props[0].value.f = 1.0f;
    props[1].value.f = 0.5f;
    props[2].value.f = 1.0f;

    CssKeyframeStop stops[3];
    stops[0] = {0.0f, &props[0], 1, NULL};
    stops[1] = {0.5f, &props[1], 1, NULL};
    stops[2] = {1.0f, &props[2], 1, NULL};

    CssKeyframes kf = {"pulse", stops, 3};

    CssAnimProp ap = defaultAnimProp("pulse", 2.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    // t=0.25 -> between stop 0 and 1, local_t=0.5
    css_animation_tick(inst, 0.25f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.75f);

    // t=0.75 -> between stop 1 and 2, local_t=0.5
    css_animation_tick(inst, 0.75f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.75f);
}

// ============================================================================
// Full Pipeline Test (parsing + tick)
// ============================================================================

TEST_F(AnimationTickTest, ParseAndTickOpacity) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "fadeIn { from { opacity: 0; } to { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "fadeIn");
    ASSERT_NE(kf, nullptr);

    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimProp ap = defaultAnimProp("fadeIn", 0.5f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    css_animation_tick(inst, 0.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);

    css_animation_tick(inst, 0.33f);
    EXPECT_NEAR(mock.in_line.opacity, 0.33f, 0.01f);

    css_animation_tick(inst, 0.67f);
    EXPECT_NEAR(mock.in_line.opacity, 0.67f, 0.01f);

    css_animation_tick(inst, 1.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
}
