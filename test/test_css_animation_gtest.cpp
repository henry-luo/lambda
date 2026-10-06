/**
 * CSS Animation Unit Tests
 *
 * Tests: property interpolation (float, color, transform),
 * @keyframes parsing, keyframe registry, animation creation/tick.
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/event.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arena.h"
}

// Stubs for unresolved symbols in standalone test builds
void dirty_mark_rect(DirtyTracker*, float, float, float, float) {}
// the isolated target has no reflow loop; the live fixture checks this consumer.
void doc_state_request_reflow(DocState*) {}
static struct {
    unsigned starts, ends, iterations, cancels;
    double elapsed;
} animation_events;
void radiant_dispatch_css_event(UiContext*, DomElement*, const char* type,
                                const char*, const char*, double elapsed) {
    if (strcmp(type, "animationstart") == 0) animation_events.starts++;
    if (strcmp(type, "animationend") == 0) animation_events.ends++;
    if (strcmp(type, "animationiteration") == 0) animation_events.iterations++;
    if (strcmp(type, "animationcancel") == 0) animation_events.cancels++;
    animation_events.elapsed = elapsed;
}

TEST(CssCascade, SelectorListUsesStrongestMatchingBranch) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    DomDocument* doc = dom_document_create(input);
    ASSERT_NE(doc, nullptr);
    DomElement* element = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(element, nullptr);
    ASSERT_TRUE(element->set_attribute("id", "target"));
    ASSERT_TRUE(element->add_class("a"));

    CssEngine* engine = css_engine_create(pool);
    ASSERT_NE(engine, nullptr);
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "div.a, #target.a { color: red; } #target { color: blue; }", nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 2u);
    SelectorMatcher* matcher = selector_matcher_create(pool);
    ASSERT_NE(matcher, nullptr);
    for (size_t i = 0; i < sheet->rule_count; i++) {
        radiant_apply_css_rule_to_element(element, sheet->rules[i], matcher, pool, engine);
    }

    CssDeclaration* winner = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(winner, nullptr);
    // the second branch has (1,1,0), above the later #target rule's (1,0,0).
    EXPECT_EQ(winner->specificity.ids, 1);
    EXPECT_EQ(winner->specificity.classes, 1);

    DomElement* inline_target = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(inline_target, nullptr);
    ASSERT_TRUE(inline_target->set_attribute("id", "inline-target"));
    ASSERT_TRUE(inline_target->set_attribute("style", "color: blue !important"));
    CssStylesheet* important_sheet = css_parse_stylesheet(engine,
        "#inline-target { color: red !important; }", nullptr);
    ASSERT_NE(important_sheet, nullptr);
    ASSERT_EQ(important_sheet->rule_count, 1u);
    radiant_apply_css_rule_to_element(inline_target, important_sheet->rules[0],
                                      matcher, pool, engine);
    CssDeclaration* important_winner = dom_element_get_specified_value(
        inline_target, CSS_PROPERTY_COLOR);
    ASSERT_NE(important_winner, nullptr);
    // Both are author-important; the inline declaration wins on specificity.
    EXPECT_EQ(important_winner->origin, CSS_ORIGIN_AUTHOR);
    EXPECT_EQ(important_winner->specificity.inline_style, 1);
    selector_matcher_destroy(matcher);
    dom_document_destroy(doc);
    pool_destroy(pool);
}

TEST(CssPropTable, RowsAreUniqueAndSerializeSyntheticElement) {
    size_t count = 0;
    const CssPropAccessor* rows = css_prop_accessors(&count);
    ASSERT_NE(rows, nullptr);
    ASSERT_GT(count, 0u);

    DomDocument doc = {};
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    element.doc = lam::up(&doc);
    element.set_styles_resolved(true);
    doc.root = lam::up(&element);
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(css_prop_accessor(rows[i].id), &rows[i]);
        EXPECT_GT(rows[i].id, CSS_PROPERTY_UNKNOWN);
        EXPECT_LT(rows[i].id, CSS_PROPERTY_COUNT);
        EXPECT_NE(rows[i].serialize, nullptr);
        for (size_t j = 0; j < i; j++) EXPECT_NE(rows[i].id, rows[j].id);

        char value[512];
        EXPECT_TRUE(css_prop_serialize_computed(
            &element, rows[i].id, 0, value, sizeof(value))) << rows[i].id;
    }
}

TEST(CssPropTable, DirtyMutationDoesNotConsumePendingLayout) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    DomDocument doc = {};
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    in_line.opacity = 1.0f;
    element.doc = lam::up(&doc);
    element.in_line = lam::view_ref(&in_line);
    element.set_styles_resolved(true);
    doc.root = lam::up(&element);
    doc.js.mutation_count = 1;
    char value[64];
    // A dirty computed-style read cannot consume the pending layout merely to
    // produce a used value; the next rendering checkpoint owns that commit.
    EXPECT_TRUE(css_prop_serialize_computed(
        &element, CSS_PROPERTY_OPACITY, 0, value, sizeof(value)));
    // metadata must be initialized: a missing declaration computes to the initial opacity.
    EXPECT_STREQ(value, "1");
    EXPECT_EQ(doc.js.mutation_count, 1);
    pool_destroy(pool);
}

TEST(CssPropTable, CommittedStylesIgnoreRangeDocumentWrapper) {
    DomDocument doc = {};
    DomElement element = {}, document_wrapper = {};
    element.node_type = document_wrapper.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    document_wrapper.set_synthetic(true);
    document_wrapper.tag_name = lam::up("#document");
    element.doc = lam::up(&doc);
    element.parent = lam::up(static_cast<DomNode*>(&document_wrapper));
    element.set_styles_resolved(true);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    in_line.opacity = 0.3f;
    element.in_line = lam::view_ref(&in_line);
    doc.root = lam::up(&element);
    char value[64];
    EXPECT_TRUE(css_prop_serialize_computed(
        &element, CSS_PROPERTY_OPACITY, 0, value, sizeof(value)));
    EXPECT_STREQ(value, "0.3");
}

class MotionCascadeTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    CssEngine* engine = nullptr;
    DomDocument doc = {};
    DomElement element = {};
    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        engine = css_engine_create(pool);
        ASSERT_NE(engine, nullptr);
        element.node_type = DOM_NODE_ELEMENT;
        element.set_synthetic(true);
        element.set_styles_resolved(true);
        element.doc = lam::up(&doc);
        doc.document_pool = lam::own(pool);
        doc.root = lam::up(&element);
        element.specified_style = lam::shared(style_tree_create(pool));
    }
    void TearDown() override {
        css_engine_destroy(engine);
        pool_destroy(pool);
    }
    void apply(const char* text) {
        CssDeclaration* declaration = css_parse_declaration_text(text, strlen(text), pool);
        ASSERT_NE(declaration, nullptr);
        ASSERT_NE(style_tree_apply_declaration(element.specified_style, declaration), nullptr);
    }
};

TEST_F(MotionCascadeTest, AnimationShorthandProjectsWinningLonghands) {
    const char* declarations[] = {
        "animation-duration: 9s", "animation: fade 2s linear -1s 1.5 alternate both paused, grow 4s",
        "animation-duration: 3000ms, 5s"
    };
    for (const char* text : declarations) apply(text);
    const CssPropertyCode properties[] = {CSS_PROPERTY_ANIMATION_NAME, CSS_PROPERTY_ANIMATION_DURATION,
        CSS_PROPERTY_ANIMATION_DELAY, CSS_PROPERTY_ANIMATION_ITERATION_COUNT,
        CSS_PROPERTY_ANIMATION_DIRECTION, CSS_PROPERTY_ANIMATION_FILL_MODE, CSS_PROPERTY_ANIMATION_PLAY_STATE};
    const char* expected[] = {"fade, grow", "3s, 5s", "-1s, 0s", "1.5, 1", "alternate, normal", "both, none", "paused, running"};
    for (unsigned i = 0; i < sizeof(properties) / sizeof(*properties); i++) {
        char value[128];
        ASSERT_TRUE(css_prop_serialize_computed(&element, properties[i], 0, value, sizeof(value)));
        EXPECT_STREQ(value, expected[i]);
    }
    apply("animation: 1s linear Ease");
    char name[64];
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_ANIMATION_NAME, 0, name, sizeof(name)));
    EXPECT_STREQ(name, "Ease");
    apply("animation-name: EASE, Red, BLOCK");
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_ANIMATION_NAME, 0, name, sizeof(name)));
    EXPECT_STREQ(name, "EASE, Red, BLOCK");
}

TEST_F(MotionCascadeTest, TransitionListsCycleAndLastPropertyEntryWinsBeyondEight) {
    apply("transition-duration: 9s");
    apply("transition: opacity 2s linear -1s, width 4s ease-in");
    apply("transition-property: first, second, third, fourth, fifth, sixth, seventh, eighth, opacity, width");
    apply("transition-duration: 3000ms, 5s");
    char serialized[128];
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_TRANSITION_DURATION, 0,
        serialized, sizeof(serialized)));
    EXPECT_STREQ(serialized, "3s, 5s");
    CssTransitionList list;
    CssTransitionProp config;
    css_transition_resolve_config(&element, pool, &list);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
    EXPECT_FLOAT_EQ(config.duration, 3.0f);
    EXPECT_FLOAT_EQ(config.delay, -1.0f);
    EXPECT_EQ(config.timing.type, TIMING_LINEAR);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_WIDTH, &config));
    EXPECT_FLOAT_EQ(config.duration, 5.0f);
    EXPECT_FLOAT_EQ(config.delay, 0.0f);
    apply("transition-property: all, opacity");
    css_transition_resolve_config(&element, pool, &list);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
    EXPECT_FLOAT_EQ(config.duration, 5.0f);
    apply("transition: none");
    css_transition_resolve_config(&element, pool, &list);
    EXPECT_FALSE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
}

TEST(CssPropTable, VisibilityUsesRenderEnumNames) {
    DomDocument doc = {};
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    element.doc = lam::up(&doc);
    element.in_line = lam::view_ref(&in_line);
    element.set_styles_resolved(true);
    doc.root = lam::up(&element);

    struct VisibilityCase {
        Visibility value;
        const char* expected;
    } cases[] = {
        {VIS_VISIBLE, "visible"},
        {VIS_HIDDEN, "hidden"},
        {VIS_COLLAPSE, "collapse"},
    };

    for (const VisibilityCase& test_case : cases) {
        in_line.visibility = test_case.value;
        char value[32];
        ASSERT_TRUE(css_prop_serialize_computed(
            &element, CSS_PROPERTY_VISIBILITY, 0, value, sizeof(value)));
        EXPECT_STREQ(value, test_case.expected);
    }
}

// Helper: set up a stylesheet with one @keyframes rule on a doc
static void setup_keyframes_sheet(DomDocument* doc, CssStylesheet* sheet,
                                   CssRule* rule, CssRule** rule_ptr,
                                   CssStylesheet** sheet_ptr,
                                   const char* content) {
    memset(sheet, 0, sizeof(*sheet));
    sheet->pool = doc->document_pool;
    sheet->disabled = false;

    memset(rule, 0, sizeof(*rule));
    rule->type = CSS_RULE_KEYFRAMES;
    rule->data.generic_rule.name = "keyframes";
    rule->data.generic_rule.content = content;

    *rule_ptr = rule;
    sheet->rules = rule_ptr;
    sheet->rule_count = 1;

    *sheet_ptr = sheet;
    doc->stylesheets = lam::own_arr(sheet_ptr);
    doc->stylesheet_count = 1;
}

TEST_F(MotionCascadeTest, ExtendingExpiredDurationResumesTheRetainedTimeline) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "fade { from { opacity: 0; } to { opacity: 1; } }");
    InlineProp in_line = INLINE_PROP_DEFAULT;
    element.in_line = lam::view_ref(&in_line);
    DocState state = {};
    state.animation_scheduler = animation_scheduler_create(pool);
    doc.state = lam::up(&state);
    UiContext ui = {};
    ui.document = lam::up(&doc);
    LayoutContext context = {};
    context.pool = lam::up(pool);
    context.ui_context = lam::up(&ui);
    apply("animation: fade 1s linear forwards");
    css_animation_resolve(&element, &context);
    AnimationInstance* instance = state.animation_scheduler->first;
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(state.animation_scheduler, 2.0, nullptr);
    ASSERT_EQ(instance->play_state, ANIM_PLAY_FINISHED);
    apply("animation-duration: 4s");
    css_animation_resolve(&element, &context);
    EXPECT_EQ(state.animation_scheduler->first, instance);
    EXPECT_EQ(instance->play_state, ANIM_PLAY_RUNNING);
    EXPECT_DOUBLE_EQ(instance->start_time, 0.0);
    EXPECT_TRUE(state.animation_scheduler->has_active_animations);
    EXPECT_FLOAT_EQ(in_line.opacity, 0.5f);
    animation_scheduler_tick(state.animation_scheduler, 3.0, nullptr);
    EXPECT_FLOAT_EQ(in_line.opacity, 0.75f);
    animation_scheduler_destroy(state.animation_scheduler);
    doc.state = nullptr;
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

TEST(CssTransform, Translate3dPercentagesUseTransformReferenceBox) {
    TransformFunction translate = {};
    translate.type = TRANSFORM_TRANSLATE3D;
    translate.translate_x_percent = 25.0f;
    translate.translate_y_percent = 100.0f;
    translate.params.translate3d.z = 12.0f;

    RdtMatrix matrix = radiant::compute_transform_matrix(
        &translate, 400.0f, 83.6f, 0.0f, 0.0f);

    EXPECT_FLOAT_EQ(matrix.e13, 100.0f);
    EXPECT_FLOAT_EQ(matrix.e23, 83.6f);
}

TEST(CssTransform, GroupingEffectsFlattenUsedTransformStyle) {
    DomElement element = {};
    DomElementExt extension = {};
    TransformProp transform = TRANSFORM_PROP_DEFAULT;
    ScrollProp scroll = SCROLL_PROP_DEFAULT;
    InlineProp inline_prop = INLINE_PROP_DEFAULT;
    BlockProp block = BLOCK_PROP_DEFAULT;
    FilterProp filter = {};
    FilterFunction function = {};
    element.set_synthetic(true);
    element.ext = lam::own(&extension);
    element.transform = lam::view_prop(&transform);
    element.scroller = lam::view_prop(&scroll);
    element.in_line = lam::view_ref(&inline_prop);
    element.blk = lam::view_prop(&block);
    extension.filter = lam::view_prop(&filter);
    transform.transform_style = CSS_VALUE_PRESERVE_3D;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    scroll.overflow_x = CSS_VALUE_CLIP;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    scroll.overflow_y = CSS_VALUE_HIDDEN;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    scroll.overflow_y = CSS_VALUE_VISIBLE;
    inline_prop.opacity = 0.5f;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    inline_prop.opacity = 1.0f;
    inline_prop.mix_blend_mode = CSS_VALUE_MULTIPLY;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    inline_prop.mix_blend_mode = CSS_VALUE_NORMAL;
    filter.functions = lam::own(&function);
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    filter.functions = nullptr;
    block.contain_paint = true;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    block.contain_paint = false;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    EXPECT_EQ(transform.transform_style, CSS_VALUE_PRESERVE_3D);
}

TEST(CssTransform, BackfaceNormalUsesInverseTranspose) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
    matrix.values[0] = -1.0f;
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
    matrix = rdt_matrix4_identity();
    matrix.values[10] = -1.0f;
    EXPECT_TRUE(rdt_matrix4_backface_visible(&matrix));
    matrix = rdt_matrix4_identity();
    matrix.values[2] = matrix.values[8] = 2.0f;
    // m33 remains positive, but the transformed normal points away from the viewer.
    EXPECT_TRUE(rdt_matrix4_backface_visible(&matrix));
    matrix.values[0] = 4.0f;
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
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
        doc.document_pool = lam::own(pool);
        doc.node_arena = lam::own(arena_create_default());
    }
    void TearDown() override {
        if (doc.node_arena) arena_destroy(doc.node_arena);
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
    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_OPACITY);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_FLOAT);
    EXPECT_FLOAT_EQ(kf->stops[0].properties[0].value.f, 0.0f);

    EXPECT_EQ(kf->stops[1].property_count, 1);
    EXPECT_EQ(kf->stops[1].properties[0].property_code, CSS_PROPERTY_OPACITY);
    EXPECT_FLOAT_EQ(kf->stops[1].properties[0].value.f, 1.0f);
}

TEST_F(KeyframeParsingTest, QuotedNamesAndLaterDefinitionsUseSharedNameGrammar) {
    setupKeyframes("\"quoted { name\" { from { opacity: 0; } to { opacity: 1; } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    ASSERT_EQ(registry->count, 1);
    CssKeyframes* keyframes = keyframe_registry_find(registry, "quoted { name");
    ASSERT_NE(keyframes, nullptr);
    CssKeyframes replacement = *keyframes;
    CssKeyframes* entries[] = {keyframes, &replacement};
    registry->entries = lam::own_arr(entries);
    registry->count = 2;
    EXPECT_EQ(keyframe_registry_find(registry, "quoted { name"), &replacement);
    setupKeyframes("Ease { from { opacity: 0; } to { opacity: 1; } }");
    registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(keyframe_registry_find(registry, "Ease"), nullptr);
    EXPECT_EQ(keyframe_registry_find(registry, "ease"), nullptr);
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

    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_TRANSFORM);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_TRANSFORM);
    TransformFunction* tf = kf->stops[0].properties[0].value.transform;
    ASSERT_NE(tf, nullptr);
    EXPECT_EQ(tf->type, TRANSFORM_TRANSLATEX);
    EXPECT_FLOAT_EQ(tf->params.translate.x, -100.0f);
}

TEST_F(KeyframeParsingTest, TransformKeyframesResolveCssAngleAndPercentUnits) {
    setupKeyframes("tail { from { transform: translateY(7%) rotate(1deg); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "tail");
    ASSERT_NE(kf, nullptr);
    ASSERT_EQ(kf->stop_count, 1);

    TransformFunction* translate = kf->stops[0].properties[0].value.transform;
    ASSERT_NE(translate, nullptr);
    EXPECT_EQ(translate->type, TRANSFORM_TRANSLATEY);
    EXPECT_FLOAT_EQ(translate->params.translate.y, 0.0f);
    EXPECT_FLOAT_EQ(translate->translate_y_percent, 7.0f);

    TransformFunction* rotate = translate->next;
    ASSERT_NE(rotate, nullptr);
    EXPECT_EQ(rotate->type, TRANSFORM_ROTATE);
    EXPECT_NEAR(rotate->params.angle, acosf(-1.0f) / 180.0f, 0.00001f);
}

TEST_F(KeyframeParsingTest, ColorKeyframes) {
    setupKeyframes("colorShift { from { background-color: #ff0000; } to { background-color: #0000ff; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "colorShift");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_BACKGROUND_COLOR);
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
        animation_events = {};
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
        memset(&doc, 0, sizeof(doc));
        doc.document_pool = lam::own(pool);
        doc.node_arena = lam::own(arena_create_default());
    }
    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        if (doc.node_arena) arena_destroy(doc.node_arena);
        pool_destroy(pool);
    }

    struct MockElement {
        uint8_t buf[4096];
        InlineProp in_line;
    };

    DomElement* createMockElement(MockElement* mock) {
        memset(mock, 0, sizeof(*mock));
        DomElement* element = (DomElement*)mock->buf;
        element->node_type = DOM_NODE_ELEMENT;
        element->doc = lam::up(&doc);
        ((ViewSpan*)element)->in_line = lam::view_ref(&mock->in_line);
        return element;
    }

    CssAnimProp defaultAnimProp(const char* name, float duration) {
        CssAnimProp ap;
        memset(&ap, 0, sizeof(ap));
        ap.name = lam::up(name);
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
    prop_from.property_code = CSS_PROPERTY_OPACITY;
    prop_from.value_type = ANIM_VAL_FLOAT;
    prop_from.value.f = 0.0f;

    CssAnimatedProp prop_to;
    prop_to.property_code = CSS_PROPERTY_OPACITY;
    prop_to.value_type = ANIM_VAL_FLOAT;
    prop_to.value.f = 1.0f;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, lam::own_arr(&prop_from), 1, NULL};
    stops[1] = {1.0f, lam::own_arr(&prop_to), 1, NULL};

    CssKeyframes kf = {lam::up("testFade"), lam::own_arr(stops), 2};

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

    animation_events = {};
    ap.delay = 1.0f;
    ap.fill_mode = ANIM_FILL_BOTH;
    AnimationInstance* delayed = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    animation_instance_sample(delayed, 0.0);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
    EXPECT_EQ(animation_events.starts, 0u);
    animation_instance_sample(delayed, 1.5);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.5f);
    EXPECT_EQ(animation_events.starts, 1u);
    animation_instance_pause(delayed, 1.5);
    animation_scheduler_cancel(scheduler, delayed);
    EXPECT_EQ(animation_events.cancels, 1u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 0.5);

    animation_events = {};
    ap.duration = 2.0f;
    ap.delay = -1.5f;
    AnimationInstance* negative = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    animation_instance_sample(negative, 0.0);
    EXPECT_EQ(animation_events.starts, 1u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 1.5);
    animation_scheduler_cancel(scheduler, negative);

    animation_events = {};
    ap.duration = 0.0f;
    ap.delay = 0.0f;
    ap.fill_mode = ANIM_FILL_NONE;
    AnimationInstance* instant = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    animation_instance_sample(instant, 0.0);
    css_animation_finish(instant);
    EXPECT_EQ(animation_events.starts, 1u);
    EXPECT_EQ(animation_events.ends, 1u);
    EXPECT_EQ(animation_events.iterations, 0u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 0.0);
}

TEST_F(AnimationTickTest, ColorAnimation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp prop_from;
    memset(&prop_from, 0, sizeof(prop_from));
    prop_from.property_code = CSS_PROPERTY_COLOR;
    prop_from.value_type = ANIM_VAL_COLOR;
    prop_from.value.color.r = 255; prop_from.value.color.a = 255;

    CssAnimatedProp prop_to;
    memset(&prop_to, 0, sizeof(prop_to));
    prop_to.property_code = CSS_PROPERTY_COLOR;
    prop_to.value_type = ANIM_VAL_COLOR;
    prop_to.value.color.b = 255; prop_to.value.color.a = 255;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, lam::own_arr(&prop_from), 1, NULL};
    stops[1] = {1.0f, lam::own_arr(&prop_to), 1, NULL};

    CssKeyframes kf = {lam::up("colorAnim"), lam::own_arr(stops), 2};

    CssAnimProp ap = defaultAnimProp("colorAnim", 1.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    css_animation_tick(inst, 0.5f);
    EXPECT_EQ(mock.in_line.color.r, 128);
    EXPECT_EQ(mock.in_line.color.g, 0);
    EXPECT_EQ(mock.in_line.color.b, 128);
    EXPECT_EQ(mock.in_line.color.a, 255);
}

TEST_F(AnimationTickTest, TransformAnimationMarksDocumentOwnedList) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    TransformProp transform = {};
    element->transform = lam::view_prop(&transform);

    TransformFunction keyframe_function = {};
    keyframe_function.type = TRANSFORM_TRANSLATEX;
    keyframe_function.params.translate.x = 24.0f;
    CssAnimatedProp property = {};
    property.property_code = CSS_PROPERTY_TRANSFORM;
    property.value_type = ANIM_VAL_TRANSFORM;
    property.value.transform = &keyframe_function;
    CssKeyframeStop stop = {0.0f, lam::own_arr(&property), 1, NULL};
    CssKeyframes keyframes = {lam::up("slide"), lam::own_arr(&stop), 1};

    CssAnimProp animation = defaultAnimProp("slide", 1.0f);
    AnimationInstance* instance = css_animation_create(
        scheduler, element, &animation, &keyframes, 0.0, pool);
    ASSERT_NE(instance, nullptr);

    css_animation_tick(instance, 0.0f);

    EXPECT_EQ(transform.functions, &keyframe_function);
    EXPECT_EQ(transform.functions_owner, TRANSFORM_FUNCTIONS_DOCUMENT_POOL);
}

TEST_F(AnimationTickTest, ThreeStopInterpolation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp props[3];
    for (int i = 0; i < 3; i++) {
        props[i].property_code = CSS_PROPERTY_OPACITY;
        props[i].value_type = ANIM_VAL_FLOAT;
    }
    props[0].value.f = 1.0f;
    props[1].value.f = 0.5f;
    props[2].value.f = 1.0f;

    CssKeyframeStop stops[3];
    stops[0] = {0.0f, lam::own_arr(&props[0]), 1, NULL};
    stops[1] = {0.5f, lam::own_arr(&props[1]), 1, NULL};
    stops[2] = {1.0f, lam::own_arr(&props[2]), 1, NULL};

    CssKeyframes kf = {lam::up("pulse"), lam::own_arr(stops), 3};

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
