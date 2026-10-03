#include <gtest/gtest.h>

#include "../radiant/render.hpp"
#include "../radiant/event.hpp"
#include "../radiant/svg_animation.hpp"
#ifdef __APPLE__
#include "../lib/font/font_internal.h"
#endif
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_engine.h"
#include "../lambda/io/mark_builder.hpp"

#include "../lib/image.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"

class SvgAnimationLifetimeTest : public ::testing::Test {
protected:
    Input input{};
    Pool* authored_pool = nullptr;
    Input* authored_owner = nullptr;
    DomDocument doc{};
    UiContext ui{};
    DomElement* root = nullptr;
    DomElement* svg = nullptr;

    DomElement* element(const char* tag, DomElement* parent = nullptr, Element* source = nullptr) {
        DomElement* node = DomElement::create(&doc, tag, source);
        if (node && parent && !static_cast<DomNode*>(parent)->append_child(node)) return nullptr;
        return node;
    }

    Input* authored_input() {
        if (!authored_pool) authored_pool = mem_pool_create(nullptr, MEM_ROLE_INPUT, "test.svg.animation.authored");
        if (authored_pool && !authored_owner) authored_owner = Input::create(authored_pool);
        return authored_owner;
    }

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
        root = element("div"); ASSERT_NE(root, nullptr); doc.root = root;
        svg = element("svg", root); ASSERT_NE(svg, nullptr);
        ASSERT_TRUE(svg->set_attribute("width", "200"));
        ASSERT_TRUE(svg->set_attribute("height", "200"));
        ui.document = &doc; doc.js.host_ui_context = &ui;
        doc.js.host_driven_loop = true;
        ASSERT_NE(state_store_create(&doc), nullptr);
    }

    void TearDown() override {
        state_store_destroy(&doc); doc.destroy();
        mem_pool_destroy(authored_pool);
    }

    DomElement* animate(DomElement* target, const char* name, const char* from, const char* to) {
        DomElement* animation = element("animate", target);
        if (!animation || !animation->set_attribute("attributeName", name) ||
            !animation->set_attribute("attributeType", "XML") ||
            !animation->set_attribute("dur", "2s") || !animation->set_attribute("fill", "freeze") ||
            (from && !animation->set_attribute("from", from)) || !animation->set_attribute("to", to)) return nullptr;
        return animation;
    }

    uint64_t animation_allocations() {
        MemSnapshot* snapshot = mem_snapshot_capture(mem_context_root());
        uint64_t allocations = 0;
        if (snapshot) for (uint32_t i = 0; i < snapshot->count; i++)
            if (strcmp(snapshot->samples[i].label, "svg.animation.state") == 0)
                allocations += snapshot->samples[i].alloc_count;
        mem_snapshot_free(snapshot);
        return allocations;
    }
};

TEST_F(SvgAnimationLifetimeTest, WallclockGrammarCalendarZonesAndFractionalSeconds) {
    const double origin = 946684800.25; // 2000-01-01T00:00:00.25Z
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(2000-01-01T00:00Z)", origin), -.25);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock( 2000-01-01T01:00:01.125+01:00 )", origin), .875);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(1999-12-31T23:00:00-01:00)", origin), -.25);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value("wallclock(2000-02-29T00:00Z)", origin), 59 * 86400.0 - .25);
    EXPECT_TRUE(isfinite(svg_animation_wallclock_value("wallclock(9999-12-31T23:59:59Z)", origin)));
    time_t epoch = (time_t)floor(origin);
    struct tm local = {};
#ifdef _WIN32
    ASSERT_EQ(localtime_s(&local, &epoch), 0);
#else
    ASSERT_NE(localtime_r(&epoch, &local), nullptr);
#endif
    char clock[64], value[96];
    ASSERT_GT(strftime(clock, sizeof(clock), "%H:%M:%S", &local), 0u);
    snprintf(value, sizeof(value), "wallclock(%s)", clock);
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value(value, origin), -.25);
    ASSERT_GT(strftime(clock, sizeof(clock), "%Y-%m-%d", &local), 0u);
    snprintf(value, sizeof(value), "wallclock(%s)", clock);
    local.tm_hour = local.tm_min = local.tm_sec = 0; local.tm_isdst = -1;
    EXPECT_DOUBLE_EQ(svg_animation_wallclock_value(value, origin), (double)mktime(&local) - origin);
    const char* invalid[] = {"wallclock(2001-02-29T00:00Z)", "wallclock(2000-04-31)",
        "wallclock(2000-01-01t00:00Z)", "wallclock(2000-01-01T24:00Z)", "wallclock(00:00:60)",
        "wallclock(00:00:01.)", "wallclock(00:00 +01:00)", "wallclock(00:00+01)",
        "wallclock(2000-01-01Z)", "wallclock(2000-01-01T00:00Z)+1s", "wallclock()"};
    for (const char* value : invalid) EXPECT_TRUE(isnan(svg_animation_wallclock_value(value, origin))) << value;
}

TEST_F(SvgAnimationLifetimeTest, AccessKeysResolveOffsetsRestartAndEndAcrossFocusedFragments) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("x", "10"));
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("begin", "accessKey(\xc3\xa9)+0.5s;accessKey(a)"));
    ASSERT_TRUE(animation->set_attribute("end", "accessKey(z)"));
    svg_animation_pause(svg, true);
    dom_engine_svg_timing_key(root, "\xc3\xa9");
    svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "35");
    dom_engine_svg_timing_key(root, "z");
    svg_animation_set_time(svg, 2);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "35");
    dom_engine_svg_timing_key(root, "a");
    svg_animation_set_time(svg, 3);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    ASSERT_TRUE(animation->set_attribute("restart", "whenNotActive"));
    dom_engine_svg_timing_key(root, "a");
    svg_animation_set_time(svg, 3.5);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "85");
}

TEST_F(SvgAnimationLifetimeTest, AnimationQueriesUseResolvedIntervalsAndLiveTarget) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("begin", "2s;6s"));
    svg_animation_pause(svg, true);
    double start = -1;
    EXPECT_TRUE(svg_animation_start_time(animation, &start)); EXPECT_DOUBLE_EQ(start, 2);
    EXPECT_DOUBLE_EQ(svg_animation_simple_duration(animation), 2);
    EXPECT_EQ(svg_animation_target_element(animation), rect);
    svg_animation_set_time(svg, 4);
    EXPECT_TRUE(svg_animation_start_time(animation, &start)); EXPECT_DOUBLE_EQ(start, 6);
    svg_animation_set_time(svg, 8);
    EXPECT_FALSE(svg_animation_start_time(animation, &start));
    ASSERT_TRUE(animation->set_attribute("href", "#absent"));
    EXPECT_EQ(svg_animation_target_element(animation), nullptr);
    ASSERT_TRUE(animation->set_attribute("dur", "indefinite"));
    EXPECT_TRUE(isnan(svg_animation_simple_duration(animation)));
}

TEST_F(SvgAnimationLifetimeTest, ExpiredNegativeIntervalsDoNotFreezeOrFeedSyncbases) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* expired = animate(rect, "x", "10", "110"); ASSERT_NE(expired, nullptr);
    ASSERT_TRUE(expired->set_attribute("id", "expired"));
    ASSERT_TRUE(expired->set_attribute("begin", "-3s"));
    DomElement* dependent = animate(rect, "y", "10", "110"); ASSERT_NE(dependent, nullptr);
    ASSERT_TRUE(dependent->set_attribute("begin", "expired.end+2s"));
    svg_animation_pause(svg, true);
    svg_animation_set_time(svg, 1.5);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    EXPECT_EQ(svg_animation_value(rect, "y"), nullptr);
    double start = 0;
    EXPECT_FALSE(svg_animation_start_time(expired, &start));
    ASSERT_TRUE(expired->set_attribute("begin", "-1s"));
    svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    ASSERT_TRUE(expired->set_attribute("begin", "-3s;1s"));
    svg_animation_set_time(svg, 2);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    EXPECT_TRUE(svg_animation_start_time(expired, &start)); EXPECT_DOUBLE_EQ(start, 1);
}

TEST_F(SvgAnimationLifetimeTest, UseControlsIsolateImplicitEventsShareQualifiedIdsAndReleaseTheirOwners) {
    DomElement* defs = element("defs", svg); ASSERT_NE(defs, nullptr);
    DomElement* prototype = element("g", defs); ASSERT_NE(prototype, nullptr);
    ASSERT_TRUE(prototype->set_attribute("id", "prototype"));
    DomElement* rect = element("rect", prototype); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("id", "source"));
    DomElement* implicit = animate(rect, "x", "10", "110"); ASSERT_NE(implicit, nullptr);
    ASSERT_TRUE(implicit->set_attribute("begin", "click"));
    DomElement* qualified = animate(rect, "y", "10", "110"); ASSERT_NE(qualified, nullptr);
    ASSERT_TRUE(qualified->set_attribute("begin", "source.click"));
    DomElement* first = element("use", svg); ASSERT_NE(first, nullptr);
    DomElement* second = element("use", svg); ASSERT_NE(second, nullptr);
    svg_animation_pause(svg, true);
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_EQ(svg_animation_value(rect, "x"), nullptr); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_EQ(svg_animation_value(rect, "x"), nullptr); }
    svg_animation_use_event(first, rect, "click", true, 0);
    svg_animation_set_time(svg, 1);
    {
        SvgAnimationSourceScope scope(&doc, prototype, first);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
        EXPECT_STREQ(svg_animation_value(rect, "y"), "60");
    }
    {
        SvgAnimationSourceScope scope(&doc, prototype, second);
        EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
        EXPECT_STREQ(svg_animation_value(rect, "y"), "60");
    }
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    uint64_t retained = animation_allocations();
    svg_animation_forget_source_document(&doc, &doc);
    EXPECT_LT(animation_allocations(), retained);
    EXPECT_EQ(svg_animation_use_source(first), nullptr);
    EXPECT_EQ(svg_animation_use_source(second), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, UseRepeatEventsDriveQualifiedDependentsOnTheHostClock) {
    DomElement* defs = element("defs", svg); ASSERT_NE(defs, nullptr);
    DomElement* prototype = element("g", defs); ASSERT_NE(prototype, nullptr);
    DomElement* rect = element("rect", prototype); ASSERT_NE(rect, nullptr);
    DomElement* pulse = animate(rect, "x", "10", "110"); ASSERT_NE(pulse, nullptr);
    ASSERT_TRUE(pulse->set_attribute("id", "pulse"));
    ASSERT_TRUE(pulse->set_attribute("begin", "click"));
    ASSERT_TRUE(pulse->set_attribute("dur", "1s"));
    ASSERT_TRUE(pulse->set_attribute("repeatCount", "2"));
    DomElement* dependent = animate(rect, "y", "10", "110"); ASSERT_NE(dependent, nullptr);
    ASSERT_TRUE(dependent->set_attribute("begin", "pulse.repeat(1)"));
    DomElement* first = element("use", svg); ASSERT_NE(first, nullptr);
    DomElement* second = element("use", svg); ASSERT_NE(second, nullptr);
    svg_animation_prepare(svg);
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_EQ(svg_animation_value(rect, "y"), nullptr); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_EQ(svg_animation_value(rect, "y"), nullptr); }
    svg_animation_use_event(first, rect, "click", true, 0);
    ASSERT_TRUE(animation_scheduler_tick(doc.state->animation_scheduler, 1.25, nullptr));
    { SvgAnimationSourceScope scope(&doc, prototype, first); EXPECT_STREQ(svg_animation_value(rect, "y"), "22.5"); }
    { SvgAnimationSourceScope scope(&doc, prototype, second); EXPECT_STREQ(svg_animation_value(rect, "y"), "22.5"); }
}

TEST_F(SvgAnimationLifetimeTest, ClockSurvivesLayoutReleaseAndStopsOnPauseDetachAndDocumentTeardown) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_TRUE(rect->set_attribute("x", "10"));
    ASSERT_NE(animate(rect, "x", "10", "110"), nullptr);
    svg_animation_prepare(svg);
    AnimationScheduler* scheduler = doc.state->animation_scheduler;
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_TRUE(animation_scheduler_tick(scheduler, 1.0, nullptr));
    EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
    EXPECT_STREQ(rect->get_attribute("x"), "10");
    animation_scheduler_remove_views(scheduler);
    EXPECT_EQ(scheduler->count, 1);
    svg_animation_pause(svg, true);
    EXPECT_EQ(scheduler->count, 0);
    svg_animation_set_time(svg, 1.5);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "85");
    svg_animation_pause(svg, false);
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_TRUE(animation_scheduler_tick(scheduler, 1.25, nullptr));
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 1.75);
    ASSERT_TRUE(root->remove_child(svg));
    EXPECT_FALSE(animation_scheduler_tick(scheduler, 1.5, nullptr));
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    svg_animation_prepare(svg);
    EXPECT_EQ(scheduler->count, 0); // a detached pinned tree must not restart its driver.
    ASSERT_TRUE(root->append_child(svg));
    svg_animation_prepare(svg);
    ASSERT_EQ(scheduler->count, 1);
    // D4.2.6: scheduler teardown releases its clock before document resources disappear.
    state_store_destroy(&doc);
    doc.destroy();
    EXPECT_EQ(doc.services.svg_animation_registry, nullptr);
    EXPECT_EQ(doc.state, nullptr);
}

TEST_F(SvgAnimationLifetimeTest, HtmlBoundariesOwnIndependentFragmentClocksAndPrivateDocumentsAdvanceAllFragments) {
    DomElement* foreign = element("foreignObject", svg); ASSERT_NE(foreign, nullptr);
    DomElement* html = element("div", foreign); ASSERT_NE(html, nullptr); ASSERT_FALSE(dom_element_is_svg(html));
    DomElement* inner = element("svg", html); ASSERT_NE(inner, nullptr);
    DomElement* nested = element("svg", svg); ASSERT_NE(nested, nullptr);
    DomElement* outer_shape = element("rect", svg); ASSERT_NE(outer_shape, nullptr);
    DomElement* inner_shape = element("rect", inner); ASSERT_NE(inner_shape, nullptr);
    DomElement* nested_shape = element("rect", nested); ASSERT_NE(nested_shape, nullptr);
    DomElement* shapes[] = {outer_shape, inner_shape, nested_shape};
    for (DomElement* target : shapes)
        ASSERT_NE(animate(target, "x", "10", "110"), nullptr);
    EXPECT_TRUE(svg_animation_has_elements(foreign));
    svg_animation_prepare(svg); svg_animation_pause(svg, true); svg_animation_pause(inner, true);
    svg_animation_set_time(inner, 2);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 0);
    EXPECT_STREQ(svg_animation_value(outer_shape, "x"), "10");
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "110");
    svg_animation_set_time(svg, 1);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(nested), 1);
    svg_animation_set_time(nested, 2);
    svg_animation_pause(nested, false);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), 1);
    EXPECT_TRUE(svg_animation_paused(svg));
    EXPECT_STREQ(svg_animation_value(nested_shape, "x"), "60");
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "110");
    svg_animation_set_document_time(&doc, .5);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(svg), .5);
    EXPECT_DOUBLE_EQ(svg_animation_current_time(inner), .5);
    EXPECT_STREQ(svg_animation_value(inner_shape, "x"), "35");
}

TEST_F(SvgAnimationLifetimeTest, FilterProgramsTrackClockChangesWithoutDomMutation) {
    DomElement* offset = element("feOffset", svg); ASSERT_NE(offset, nullptr);
    ASSERT_NE(animate(offset, "dx", "0", "100"), nullptr);
    svg_animation_prepare(svg); svg_animation_pause(svg, true);
    Element filter = {};
    uint64_t epoch = doc.mutation_epoch;
    RdtSvgFilterProgram* before = render_svg_filter_program_acquire(&doc, &filter);
    ASSERT_NE(before, nullptr); before->compiled = true;
    EXPECT_EQ(render_svg_filter_program_acquire(&doc, &filter), before);
    render_svg_filter_program_release(before);
    svg_animation_set_time(svg, 1);
    EXPECT_EQ(doc.mutation_epoch, epoch);
    RdtSvgFilterProgram* after = render_svg_filter_program_acquire(&doc, &filter);
    ASSERT_NE(after, nullptr); EXPECT_NE(after, before); EXPECT_FALSE(after->compiled);
    EXPECT_TRUE(before->compiled); // pinned callers keep their earlier immutable facts.
    EXPECT_NE(after->animation_generation, before->animation_generation);
    render_svg_filter_program_release(before); render_svg_filter_program_release(after);
}

TEST_F(SvgAnimationLifetimeTest, FilterProgramsSeparateAnimatedInstancesFromStaticReferences) {
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("g").final();
    DomElement* group = element("g", svg, source.element); ASSERT_NE(group, nullptr);
    source = builder.element("filter").final();
    Element* filter_source = source.element;
    DomElement* filter = element("filter", group, filter_source); ASSERT_NE(filter, nullptr);
    DomElement* flood = element("feFlood", filter); ASSERT_NE(flood, nullptr);
    ASSERT_NE(animate(flood, "flood-color", "red", "blue"), nullptr);
    DomElement* outside = element("g", svg); ASSERT_NE(outside, nullptr);
    svg_animation_mark_reference(svg); svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    RdtSvgFilterProgram* base = render_svg_filter_program_acquire(&doc, filter_source); ASSERT_NE(base, nullptr);
    RdtSvgFilterProgram* sampled = nullptr;
    {
        SvgAnimationSourceScope instance(&doc, group);
        sampled = render_svg_filter_program_acquire(&doc, filter_source); ASSERT_NE(sampled, nullptr);
        EXPECT_NE(sampled, base); EXPECT_GT(sampled->animation_generation, 0u);
        EXPECT_STREQ(svg_animation_value(flood, "flood-color"), "rgba(127.5,0,127.5,1)");
    }
    {
        SvgAnimationSourceScope static_resource(&doc, outside);
        RdtSvgFilterProgram* same = render_svg_filter_program_acquire(&doc, filter_source);
        EXPECT_EQ(same, base); EXPECT_EQ(same->animation_generation, 0u);
        render_svg_filter_program_release(same);
    }
    render_svg_filter_program_release(sampled); render_svg_filter_program_release(base);
}

TEST_F(SvgAnimationLifetimeTest, TypedSamplesResolveLengthListsPairsAnglesAndIntegerRounding) {
    DomElement* turbulence = element("feTurbulence", svg); ASSERT_NE(turbulence, nullptr);
    ASSERT_NE(animate(turbulence, "numOctaves", "1", "4"), nullptr);
    DomElement* blur = element("feGaussianBlur", svg); ASSERT_NE(blur, nullptr);
    ASSERT_NE(animate(blur, "stdDeviation", "2", "6 10"), nullptr);
    DomElement* marker = element("marker", svg); ASSERT_NE(marker, nullptr);
    ASSERT_NE(animate(marker, "orient", "0deg", "0.5turn"), nullptr);
    DomElement* text = element("text", svg); ASSERT_NE(text, nullptr);
    ASSERT_NE(animate(text, "x", "10px 20px", "1in 2in"), nullptr);
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    ASSERT_NE(animate(rect, "x", nullptr, "100"), nullptr);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(turbulence, "numOctaves"), "3");
    EXPECT_STREQ(svg_animation_value(blur, "stdDeviation"), "4 6");
    EXPECT_STREQ(svg_animation_value(marker, "orient"), "90");
    EXPECT_STREQ(svg_animation_value(text, "x"), "53 106");
    EXPECT_STREQ(svg_animation_value(rect, "x"), "50");
    EXPECT_EQ(rect->get_attribute("x"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, ToAnimationsUseResourceAndFilterInitialValues) {
    struct Case { const char* tag; const char* name; const char* to; const char* expected; };
    const Case cases[] = {
        {"marker", "orient", "90", "45"}, {"marker", "markerWidth", "9", "6"},
        {"linearGradient", "x2", "0%", "50%"}, {"radialGradient", "fx", "100%", "75%"},
        {"feTurbulence", "numOctaves", "5", "3"}, {"feGaussianBlur", "stdDeviation", "4", "2 2"},
        {"feMorphology", "radius", "4 6", "2 3"}, {"feFuncR", "slope", "3", "2"},
        {"filter", "x", "10%", "0%"}, {"filter", "width", "80%", "100%"},
        {"feFlood", "width", "80%", "90%"}, {"feOffset", "x", "10%", "5%"},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        ASSERT_NE(animate(targets[i], cases[i].name, nullptr, cases[i].to), nullptr);
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].expected) << cases[i].name;
}

TEST_F(SvgAnimationLifetimeTest, DiscreteToUsesSvgHalfwayAndKeyTimeBoundaries) {
    DomElement* normal = element("rect", svg); ASSERT_NE(normal, nullptr);
    DomElement* keyed = element("rect", svg); ASSERT_NE(keyed, nullptr);
    DomElement* targets[] = {normal, keyed};
    for (DomElement* target : targets) {
        ASSERT_TRUE(target->set_attribute("x", "10"));
        DomElement* animation = animate(target, "x", nullptr, "110"); ASSERT_NE(animation, nullptr);
        ASSERT_TRUE(animation->set_attribute("calcMode", "discrete"));
        if (target == keyed) ASSERT_TRUE(animation->set_attribute("keyTimes", "0;.75"));
    }
    svg_animation_pause(svg, true);
    struct Case { double time; const char* normal; const char* keyed; };
    const Case cases[] = {{0, "10", "10"}, {.5, "10", "10"}, {1, "110", "10"},
        {1.5, "110", "110"}, {2, "110", "110"}, {0, "10", "10"}};
    for (const Case& sample : cases) {
        svg_animation_set_time(svg, sample.time);
        EXPECT_STREQ(svg_animation_value(normal, "x"), sample.normal);
        EXPECT_STREQ(svg_animation_value(keyed, "x"), sample.keyed);
    }
}

TEST_F(SvgAnimationLifetimeTest, IndefiniteDurationIgnoresKeyTimesAndKeepsFirstValue) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "20", "100"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("dur", "indefinite"));
    ASSERT_TRUE(animation->set_attribute("keyTimes", ".5;1"));
    ASSERT_TRUE(animation->set_attribute("end", "2s"));
    svg_animation_pause(svg, true);
    const double times[] = {0.0, 1.0, 2.0, 3.0};
    for (double time : times) {
        svg_animation_set_time(svg, time);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "20");
    }
}

TEST_F(SvgAnimationLifetimeTest, DiscreteToUsesAbsentXmlEnumInitialValues) {
    struct Case { const char* tag; const char* name; const char* to; const char* initial; };
    const Case cases[] = {
        {"linearGradient", "spreadMethod", "repeat", "pad"},
        {"pattern", "patternUnits", "userSpaceOnUse", "objectBoundingBox"},
        {"marker", "markerUnits", "userSpaceOnUse", "strokeWidth"},
        {"svg", "preserveAspectRatio", "none", "xMidYMid meet"},
        {"feComposite", "operator", "xor", "over"},
        {"feTurbulence", "type", "fractalNoise", "turbulence"},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        ASSERT_NE(animate(targets[i], cases[i].name, nullptr, cases[i].to), nullptr);
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, .5);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].initial);
    svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EXPECT_STREQ(svg_animation_value(targets[i], cases[i].name), cases[i].to);
        EXPECT_EQ(targets[i]->get_attribute(cases[i].name), nullptr);
    }
}

TEST_F(SvgAnimationLifetimeTest, RelativeLengthsRetainFontDependencyAndOpacityPercentagesStayDimensionless) {
    DomElement* parent = element("g", svg); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("font-size", "20"));
    DomElement* font = animate(parent, "font-size", "20", "40"); ASSERT_NE(font, nullptr);
    ASSERT_TRUE(font->set_attribute("begin", "0.5s")); ASSERT_TRUE(font->set_attribute("dur", "1s"));
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr); MarkBuilder builder(owner);
    Item source = builder.element("rect").attr("width", "1em").attr("height", "20").final();
    DomElement* rect = element("rect", parent, source.element); ASSERT_NE(rect, nullptr);
    ASSERT_NE(animate(rect, "width", "1em", "2em"), nullptr);
    ASSERT_NE(animate(rect, "opacity", "100%", "0.5"), nullptr);
    DomElement* path = element("path", parent); ASSERT_NE(path, nullptr);
    ASSERT_NE(animate(path, "stroke-dasharray", "1em 1em", "2em 2em"), nullptr);
    source = builder.element("rect").attr("width", "1em").attr("height", "20").final();
    DomElement* mixed = element("rect", parent, source.element); ASSERT_NE(mixed, nullptr);
    ASSERT_NE(animate(mixed, "width", "1em", "80px"), nullptr);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(rect, "width"), "1.5em");
    EXPECT_STREQ(svg_animation_value(path, "stroke-dasharray"), "1.5em 1.5em");
    EXPECT_STREQ(svg_animation_value(rect, "opacity"), "0.75");
    float left, top, right, bottom;
    ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 45);
    ASSERT_TRUE(dom_svg_element_geometry_bounds(mixed, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 55);
    svg_animation_set_time(svg, 2);
    ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(right - left, 80);
    EXPECT_STREQ(svg_animation_value(rect, "opacity"), "0.5");
}

TEST_F(SvgAnimationLifetimeTest, ToAnimationsUseAbsentIriStopAndColorMatrixInitialValues) {
    DomElement* use = element("use", svg); ASSERT_NE(use, nullptr);
    ASSERT_NE(animate(use, "href", nullptr, "#shape"), nullptr);
    DomElement* stop = element("stop", svg); ASSERT_NE(stop, nullptr);
    ASSERT_NE(animate(stop, "offset", nullptr, "1"), nullptr);
    DomElement* matrix = element("feColorMatrix", svg); ASSERT_NE(matrix, nullptr);
    ASSERT_NE(animate(matrix, "values", nullptr, "0 0 1 0 0 0 1 0 0 0 1 0 0 0 0 0 0 0 1 0"), nullptr);
    DomElement* typed = element("feColorMatrix", svg); ASSERT_NE(typed, nullptr);
    ASSERT_TRUE(typed->set_attribute("type", "saturate"));
    ASSERT_NE(animate(typed, "values", nullptr, "180"), nullptr);
    DomElement* type = animate(typed, "type", nullptr, "hueRotate"); ASSERT_NE(type, nullptr);
    ASSERT_TRUE(type->set_attribute("begin", "0.1s"));
    ASSERT_TRUE(type->set_attribute("dur", "0.2s"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(use, "href"), "");
    EXPECT_STREQ(svg_animation_value(matrix, "values"), "1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 0");
    svg_animation_set_time(svg, 1);
    EXPECT_STREQ(svg_animation_value(use, "href"), "#shape");
    EXPECT_STREQ(svg_animation_value(stop, "offset"), "0.5");
    EXPECT_STREQ(svg_animation_value(matrix, "values"), "0.5 0 0.5 0 0 0 1 0 0 0 0.5 0 0.5 0 0 0 0 0 1 0");
    // the default values list follows the sampled type, regardless of animation begin priority.
    EXPECT_STREQ(svg_animation_value(typed, "type"), "hueRotate");
    EXPECT_STREQ(svg_animation_value(typed, "values"), "90");
    EXPECT_EQ(use->get_attribute("href"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, InvalidPathStructuresAndAttributeTypesDoNotReplaceBaseValues) {
    DomElement* path = element("path", svg); ASSERT_NE(path, nullptr);
    ASSERT_NE(animate(path, "d", "M0 0 L20 0", "M0 0 Q10 20 20 0"), nullptr);
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("attributeType", "invalid"));
    DomElement* spaced = element("rect", svg); ASSERT_NE(spaced, nullptr);
    animation = animate(spaced, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("attributeType", " XML "));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(path, "d"), nullptr);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    EXPECT_STREQ(svg_animation_value(spaced, "x"), "60");
}

TEST_F(SvgAnimationLifetimeTest, RetiredControlsArePrunedWhileDetachedPinnedControlsSurvive) {
    // authored nodes retire through the lifecycle registry; synthetic layout nodes have a different owner.
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("rect").final();
    DomElement* rect = element("rect", svg, source.element); ASSERT_NE(rect, nullptr);
    source = builder.element("animate").attr("attributeName", "x").attr("attributeType", "XML")
        .attr("from", "10").attr("to", "110").attr("dur", "2s").attr("fill", "freeze")
        .attr("begin", "indefinite").final();
    DomElement* animation = element("animate", rect, source.element); ASSERT_NE(animation, nullptr);
    svg_animation_pause(svg, true);
    ASSERT_TRUE(svg_animation_begin_end(animation, false, 0));
    svg_animation_set_time(svg, 1);
    ASSERT_STREQ(svg_animation_value(rect, "x"), "60");
    uint64_t attached = animation_allocations(); ASSERT_GT(attached, 0u);
    DomNodeRef ref = dom_node_ref(animation);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_EXTERNAL));
    ASSERT_TRUE(svg->remove_child(rect));
    dom_retire_sweep(&doc);
    ASSERT_NE(dom_node_ref_validate(&doc, ref), nullptr);
    svg_animation_set_time(svg, 1.25); svg_animation_value(rect, "x");
    uint64_t detached = animation_allocations();
    ASSERT_TRUE(svg->append_child(rect));
    // native synthetic links have no JS mutation notifier; a clock sample rebuilds their paint facts.
    svg_animation_set_time(svg, 1.5);
    ASSERT_STREQ(svg_animation_value(rect, "x"), "85");
    ASSERT_TRUE(svg->remove_child(rect));
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_EXTERNAL));
    EXPECT_GT(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    svg_animation_set_time(svg, 1.75);
    svg_animation_value(svg, "width");
    EXPECT_LT(animation_allocations(), detached);
}

TEST_F(SvgAnimationLifetimeTest, ExcessiveSyncbaseHistoryDoesNotPublishPartialSamplesAndCanRecover) {
    DomElement* rect = element("rect", svg); ASSERT_NE(rect, nullptr);
    DomElement* animation = animate(rect, "x", "10", "110"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("id", "cycle"));
    ASSERT_TRUE(animation->set_attribute("begin", "0;cycle.end"));
    ASSERT_TRUE(animation->set_attribute("dur", "0.001s"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    svg_animation_set_time(svg, 0);
    EXPECT_STREQ(svg_animation_value(rect, "x"), "10");
}

TEST_F(SvgAnimationLifetimeTest, ReferenceAnimationScopesReachGeometryWithoutLeakingIntoStaticResources) {
    Input* owner = authored_input(); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    Item source = builder.element("rect").attr("x", "10").attr("width", "20").attr("height", "30").final();
    DomElement* rect = element("rect", svg, source.element); ASSERT_NE(rect, nullptr);
    source = builder.element("animate").attr("attributeName", "x").attr("attributeType", "XML")
        .attr("from", "10").attr("to", "110").attr("dur", "2s").attr("fill", "freeze").final();
    ASSERT_NE(element("animate", rect, source.element), nullptr);
    svg_animation_mark_reference(svg);
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    EXPECT_EQ(svg_animation_value(rect, "x"), nullptr);
    {
        SvgAnimationSourceScope instance(&doc, rect);
        EXPECT_STREQ(svg_animation_value(rect, "x"), "60");
        float left, top, right, bottom;
        ASSERT_TRUE(dom_svg_element_geometry_bounds(rect, &left, &top, &right, &bottom));
        EXPECT_FLOAT_EQ(left, 60); EXPECT_FLOAT_EQ(right, 80);
    }
    SvgAnimationSourceScope resource(&doc);
    EXPECT_EQ(svg_animation_source_value(dom_element_to_element(rect), "x"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, AttributeNamespaceIdentityUsesAnimationBindingsAndRejectsUnrelatedNamespaces) {
    DomElement* scope = element("g", svg); ASSERT_NE(scope, nullptr);
    ASSERT_TRUE(scope->set_attribute("xmlns:a", "http://www.w3.org/1999/xlink"));
    DomElement* target_scope = element("g", svg); ASSERT_NE(target_scope, nullptr);
    ASSERT_TRUE(target_scope->set_attribute("xmlns:b", "http://www.w3.org/1999/xlink"));
    ASSERT_TRUE(target_scope->set_attribute("xmlns:xlink", "urn:unrelated"));
    DomElement* target = element("use", target_scope); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("id", "target")); ASSERT_TRUE(target->set_attribute("b:href", "#red"));
    DomElement* animation = animate(scope, "a:href", "#red", "#blue"); ASSERT_NE(animation, nullptr);
    ASSERT_TRUE(animation->set_attribute("href", "#target"));
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1.5);
    EXPECT_STREQ(svg_animation_value(target, "xlink:href"), "#blue");
    EXPECT_STREQ(dom_element_attribute_ns(target, "http://www.w3.org/1999/xlink", "href"), "#red");
    ASSERT_TRUE(scope->set_attribute("xmlns:a", "urn:unrelated"));
    svg_animation_set_time(svg, 1.75);
    EXPECT_EQ(svg_animation_value(target, "xlink:href"), nullptr);
}

TEST_F(SvgAnimationLifetimeTest, NonAnimatableAndInapplicableAttributesDoNotProduceSamples) {
    struct Case { const char* tag; const char* name; const char* from; const char* to; bool transform; };
    const Case cases[] = {
        {"feFlood", "result", "first", "second", false}, {"rect", "r", "10", "20", false},
        {"rect", "transform", "translate(0)", "translate(100)", false},
        {"rect", "fill", "0", "100", true}, {"linearGradient", "gradientUnits", "userSpaceOnUse", "invalid", false},
    };
    DomElement* targets[sizeof(cases) / sizeof(cases[0])] = {};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        targets[i] = element(cases[i].tag, svg); ASSERT_NE(targets[i], nullptr);
        DomElement* animation = cases[i].transform ? element("animateTransform", targets[i])
            : animate(targets[i], cases[i].name, cases[i].from, cases[i].to);
        ASSERT_NE(animation, nullptr);
        if (cases[i].transform) {
            ASSERT_TRUE(animation->set_attribute("attributeName", cases[i].name));
            ASSERT_TRUE(animation->set_attribute("from", cases[i].from));
            ASSERT_TRUE(animation->set_attribute("to", cases[i].to));
            ASSERT_TRUE(animation->set_attribute("dur", "2s"));
        }
    }
    svg_animation_pause(svg, true); svg_animation_set_time(svg, 1);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_EQ(svg_animation_value(targets[i], cases[i].name), nullptr) << cases[i].name;
}

TEST(SvgAnimationTest, PictureDuplicatesRetainPrivateTimeWithoutChangingTheParsedCacheOwner) {
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='60' height='60'>"
        "<rect width='40' height='40'><set attributeName='fill' to='blue' begin='2s'/></rect></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    EXPECT_TRUE(render_svg_picture_has_animation(picture));
    rdt_picture_set_animation_time(picture, 2.25);
    RdtPicture* duplicate = rdt_picture_dup(picture); ASSERT_NE(duplicate, nullptr);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(duplicate), 2.25);
    rdt_picture_set_animation_time(duplicate, .5);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(picture), 2.25);
    RdtPicture* cached = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(cached, nullptr);
    EXPECT_DOUBLE_EQ(rdt_picture_animation_time(cached), 0);
    rdt_picture_free(cached); rdt_picture_free(duplicate); rdt_picture_free(picture);
}

TEST(RenderCompositeTest, MultiplyPreservesSurfaceAlphaRepresentation) {
    const struct {
        ImageAlphaMode mode;
        uint32_t backdrop, source, expected;
    } cases[] = {
        {IMAGE_ALPHA_STRAIGHT, 0xff808080u, 0x800000ffu, 0xff404080u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0xff808080u, 0x80000080u, 0xff404080u},
        {IMAGE_ALPHA_STRAIGHT, 0x8000ff00u, 0x800000ffu, 0xc0005555u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0x80008000u, 0x80000080u, 0xc0004040u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0u, 0x80000080u, 0x80000080u},
        {IMAGE_ALPHA_PREMULTIPLIED, 0x80008000u, 0u, 0x80008000u},
    };
    for (const auto& sample : cases) {
        uint32_t pixel = sample.source;
        ImageSurface surface = {};
        surface.width = surface.height = 1; surface.pitch = sizeof(pixel);
        surface.pixels = &pixel; surface.alpha_mode = sample.mode;
        render_composite_blend_surface(&surface, &sample.backdrop, 0, 0, 1, 1, CSS_VALUE_MULTIPLY);
        EXPECT_EQ(pixel, sample.expected) << "alpha mode " << sample.mode;
    }
}

TEST(SvgExportTest, EncodingCopyRespectsMemoryBudgetAndRecovers) {
    uint32_t pixels[64 * 64] = {};
    ImageSurface surface = {};
    surface.width = surface.height = 64; surface.pitch = 64 * sizeof(uint32_t);
    surface.pixels = pixels; surface.alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
    size_t soft, hard, critical;
    memtrack_get_limits(&soft, &hard, &critical);
    // the 16KB straight-alpha copy cannot fit the temporary 256-byte critical budget.
    memtrack_set_limits(0, 0, 256);
    StrBuf* rejected = render_encode_surface_data_uri(&surface);
    memtrack_set_limits(soft, hard, critical);
    EXPECT_EQ(rejected, nullptr);
    if (rejected) strbuf_free(rejected);
    StrBuf* recovered = render_encode_surface_data_uri(&surface);
    EXPECT_NE(recovered, nullptr);
    if (recovered) strbuf_free(recovered);
}

TEST(SvgExportTest, RasterSnapshotRetainsTransparencyAndLogicalBoundsAtBothDensities) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
        "<rect x='2.25' y='2.25' width='8' height='8' fill='red' opacity='.5'/></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    for (float density : {1.0f, 2.0f}) {
        PaintSvgSubscene subscene = {};
        render_svg_build_subscene(&subscene, rdt_picture_get_svg_root(picture), 20, 20,
            rdt_picture_get_pool(picture), density, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 1, false, nullptr, true, -1);
        subscene.image_document = true;
        Bound bounds = {};
        ImageSurface* surface = render_svg_subscene_rasterize(&subscene, &bounds);
        ASSERT_NE(surface, nullptr);
        EXPECT_FLOAT_EQ(bounds.left, 0); EXPECT_FLOAT_EQ(bounds.right, 20);
        EXPECT_EQ(surface->width, 20 * density); EXPECT_EQ(surface->height, 20 * density);
        EXPECT_EQ(surface->alpha_mode, IMAGE_ALPHA_STRAIGHT);
        const uint32_t* pixels = (const uint32_t*)surface->pixels;
        EXPECT_EQ(pixels[0], 0u);
        size_t coordinate = density == 1 ? 4 : 8;
        uint32_t red = pixels[coordinate * (size_t)surface->width + coordinate];
        EXPECT_EQ(red & 255u, 255u); EXPECT_NEAR(red >> 24, 128u, 1);
        // the fractional left edge covers 3/4 of a 1x pixel, or 1/2 of a 2x pixel, at half opacity.
        size_t edge_x = density == 1 ? 2 : 4;
        uint32_t edge = pixels[coordinate * (size_t)surface->width + edge_x];
        EXPECT_NEAR(edge & 255u, 255u, 1);
        EXPECT_NEAR(edge >> 24, density == 1 ? 96u : 64u, 3);
        image_surface_destroy(surface);
    }
    rdt_picture_free(picture);
    rdt_engine_term();
}

TEST(SvgExportTest, FinalPaintRetainsSampledVectorsAndOwnsDeferredClips) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
        "<rect width='10' height='10' fill='red'><set attributeName='fill' to='blue' begin='0s'/></rect></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    PaintSvgSubscene scene = {};
    render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 20, 20,
        rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, 1, false, nullptr, true, -1);
    scene.image_document = true;
    StrBuf* svg = strbuf_new();
    auto consume = [](PaintList* paint, void* context) {
        PaintSvgLoweringStats stats = {};
        paint_ir_lower_svg(paint, (StrBuf*)context, nullptr, &stats);
        return stats.unsupported_count == 0;
    };
    ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, svg));
    EXPECT_NE(strstr(svg->str, "<path"), nullptr);
    EXPECT_NE(strstr(svg->str, "rgb(0,0,255)"), nullptr);
    EXPECT_EQ(strstr(svg->str, "<set"), nullptr);
    strbuf_reset(svg);
    PaintList paint = {};
    RdtPath* path = rdt_path_new(); rdt_path_add_rect(path, 0, 0, 20, 20, 0, 0);
    paint_push_clip(&paint, path, nullptr); rdt_path_free(path);
    Color red = {}; red.r = red.a = 255;
    paint_fill_rect(&paint, 0, 0, 20, 20, red);
    paint_pop_clip(&paint);
    EXPECT_TRUE(consume(&paint, svg));
    EXPECT_NE(strstr(svg->str, "<clipPath"), nullptr);
    paint_list_destroy(&paint); strbuf_free(svg);
    rdt_picture_free(picture); rdt_engine_term();
}

TEST(SvgExportTest, FinalPaintRetainsNestedMetadataEmptyGroupsAndOutlinedText) {
    rdt_engine_init(0);
    const char source[] = "<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40' data-root='scene'>"
        "<g data-graph-role='edge' data-edge-id='a&amp;&quot;b' data-route='0,0 40,20'>"
        "<rect width='20' height='20' fill='red'/><g data-empty='kept'/>"
        "<text x='0' y='35' font-size='12'>A &amp; B</text></g></svg>";
    RdtPicture* picture = rdt_picture_load_data(source, sizeof(source) - 1, "svg");
    ASSERT_NE(picture, nullptr);
    PaintSvgSubscene scene = {};
    render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 80, 40,
        rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, 1, false, nullptr, true, -1);
    scene.image_document = true;
    StrBuf* svg = strbuf_new();
    auto consume = [](PaintList* paint, void* context) {
        PaintSvgLoweringStats stats = {};
        paint_ir_lower_svg(paint, (StrBuf*)context, nullptr, &stats);
        return stats.unsupported_count == 0;
    };
    ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, svg));
    EXPECT_NE(strstr(svg->str, "data-root=\"scene\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-edge-id=\"a&amp;&quot;b\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-route=\"0,0 40,20\""), nullptr);
    EXPECT_NE(strstr(svg->str, "data-empty=\"kept\""), nullptr);
    EXPECT_NE(strstr(svg->str, "<title>A &amp; B</title>"), nullptr);
    EXPECT_NE(strstr(svg->str, "<path"), nullptr);
    strbuf_free(svg); rdt_picture_free(picture); rdt_engine_term();
}

TEST(SvgExportTest, SemanticSnapshotsOwnValuesAndDoNotChangeRasterPaint) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "test.svg.export.semantics");
    ASSERT_NE(arena, nullptr);
    DisplayList dl = {}; dl_init(&dl, arena);
    char name[] = "data-node-id", value[] = "n0", title[] = "label";
    RenderSemanticAttribute attribute = {name, value};
    RenderSemanticGroup source = {&attribute, 1, title}, snapshot = {};
    ASSERT_TRUE(dl_copy_semantic_group(&dl, &snapshot, &source));
    name[0] = value[0] = title[0] = '!';
    EXPECT_STREQ(snapshot.attributes[0].name, "data-node-id");
    EXPECT_STREQ(snapshot.attributes[0].value, "n0");
    EXPECT_STREQ(snapshot.title, "label");
    PaintList paint = {};
    paint_begin_semantic_group(&paint, &snapshot);
    Color red = {}; red.r = red.a = 255;
    paint_fill_rect(&paint, 0, 0, 20, 20, red);
    paint_end_semantic_group(&paint);
    EXPECT_TRUE(paint_ir_validate(&paint, nullptr));
    paint_ir_lower_raster(&paint, &dl);
    ASSERT_EQ(dl.item_count(), 1);
    EXPECT_EQ(dl.data()[0].op, DL_FILL_RECT);
    paint_list_clear(&paint);
    paint_end_semantic_group(&paint);
    EXPECT_FALSE(paint_ir_validate(&paint, nullptr));
    paint_list_destroy(&paint); dl_destroy(&dl); mem_arena_destroy(arena);
}

TEST(SvgExportTest, RasterFallbackRetainsSemanticGroupsAndTitles) {
    rdt_engine_init(0);
    const char* effects[] = {"opacity='.5'", "filter='url(#blur)'"};
    for (const char* effect : effects) {
        StrBuf* source = strbuf_create("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='40'>"
            "<defs><filter id='blur'><feGaussianBlur stdDeviation='1'/></filter></defs>");
        strbuf_append_format(source,
            "<g data-node-id='n0' %s><g data-node-id='n1'><rect width='20' height='20' fill='red'/>"
            "<text x='0' y='35' font-size='12'>fallback label</text></g></g>"
            "<text x='40' y='20' font-size='12'>vector label</text></svg>", effect);
        RdtPicture* picture = rdt_picture_load_data(source->str, source->length, "svg");
        strbuf_free(source);
        ASSERT_NE(picture, nullptr);
        PaintSvgSubscene scene = {};
        render_svg_build_subscene(&scene, rdt_picture_get_svg_root(picture), 80, 40,
            rdt_picture_get_pool(picture), 1, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 1, false, nullptr, true, -1);
        scene.image_document = true;
        StrBuf* svg = strbuf_new();
        struct Output { StrBuf* svg; int calls; } output = {svg, 0};
        auto consume = [](PaintList* paint, void* context) {
            Output* output = (Output*)context;
            output->calls++;
            // reject vector geometry to exercise the whole-subscene fallback after private effect capture.
            for (int i = 0; i < paint->item_count(); i++)
                if (paint->data()[i].op == PAINT_FILL_PATH) return false;
            PaintSvgLoweringStats stats = {};
            paint_ir_lower_svg(paint, output->svg, nullptr, &stats);
            return stats.unsupported_count == 0;
        };
        ASSERT_TRUE(render_svg_subscene_with_paint(&scene, consume, &output, true));
        EXPECT_EQ(output.calls, 2);
        EXPECT_NE(strstr(svg->str, "data-node-id=\"n0\""), nullptr);
        EXPECT_NE(strstr(svg->str, "data-node-id=\"n1\""), nullptr);
        EXPECT_NE(strstr(svg->str, "<title>fallback label</title>"), nullptr);
        EXPECT_NE(strstr(svg->str, "data:image/png;base64,"), nullptr);
        strbuf_free(svg); rdt_picture_free(picture);
    }
    rdt_engine_term();
}

static RdtSvgFilterProgram* svg_filter_test_program(DomDocument* document, Element* element, size_t count, float width, float height) {
    RdtSvgFilterProgram* program = render_svg_filter_program_acquire(document, element);
    if (!program) return nullptr;
    program->compiled = program->valid = true; program->count = count;
    program->nodes = (RdtSvgFilterNode*)arena_calloc(program->arena, count * sizeof(RdtSvgFilterNode));
    program->region[0] = program->region[1] = arena_strdup(program->arena, "0");
    program->region[2] = arena_sprintf(program->arena, "%g", (double)width);
    program->region[3] = arena_sprintf(program->arena, "%g", (double)height);
    return program;
}

static bool svg_filter_test_input(void* data, int input, const RdtSvgFilterRun*, Bound, ImageSurface* output) {
    unsigned* calls = (unsigned*)data;
    unsigned slot = input == RDT_SVG_FILTER_FILL ? 0 : input == RDT_SVG_FILTER_STROKE ? 1 : 2;
    const uint32_t colors[] = {0x80000080u, 0x40004000u, 0x80800000u};
    calls[slot]++;
    for (size_t index = 0, count = (size_t)output->width * (size_t)output->height; index < count; index++)
        ((uint32_t*)output->pixels)[index] = colors[slot];
    return true;
}

static bool svg_filter_test_source(void* data, const RdtSvgFilterRun* run, Bound, ImageSurface* output) {
    unsigned* calls = (unsigned*)data; (*calls)++;
    if (!render_svg_filter_spend_work(run, 16)) return false;
    for (size_t index = 0, count = (size_t)output->width * (size_t)output->height; index < count; index++)
        ((uint32_t*)output->pixels)[index] = 0xff808080u;
    return true;
}

TEST(SvgFilterTest, ResourceFontsAndReferencingViewportHaveDistinctLengthBases) {
    DomDocument document; Element filter = {}, primitive = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &filter, 1, 20, 80);
    ASSERT_NE(program, nullptr);
    program->region[0] = arena_strdup(program->arena, "1em");
    program->region[2] = arena_strdup(program->arena, "2em");
    program->region[3] = arena_strdup(program->arena, "50%");
    RdtSvgFilterNode* node = program->nodes;
    node->kind = RDT_SVG_FILTER_FLOOD; node->valid = true; node->element = &primitive;
    node->color.r = node->color.a = 255;
    node->region[0] = arena_strdup(program->arena, "2em");
    node->region[1] = arena_strdup(program->arena, "25%");
    node->region[2] = arena_strdup(program->arena, "1em");
    node->region[3] = arena_strdup(program->arena, "10%");
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.geometry = {0,0,20,80};
    run.lengths = {200,160,100,50}; run.frame = rdt_matrix_identity(); run.density = 1;
    run.length_context = &filter;
    run.resolve_lengths = [](void* context, Element* resource, SvgLengthContext* lengths) {
        lengths->font_size = resource == context ? 10.0f : 5.0f;
        lengths->x_height = lengths->font_size * .5f;
    };
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr);
    EXPECT_FLOAT_EQ(bounds.left, 10); EXPECT_FLOAT_EQ(bounds.right, 30);
    EXPECT_FLOAT_EQ(bounds.bottom, 80);
    EXPECT_EQ(((uint32_t*)result->pixels)[40 * 20], 0xff0000ffu);
    EXPECT_EQ(((uint32_t*)result->pixels)[40 * 20 + 5], 0u);
    EXPECT_EQ(((uint32_t*)result->pixels)[39 * 20], 0u);
    EXPECT_EQ(((uint32_t*)result->pixels)[56 * 20], 0u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, CaptureAndFinalColorConversionShareTheGraphWorkBudget) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 2, 2);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_OFFSET;
    node->valid = true; node->input = RDT_SVG_FILTER_SOURCE;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    unsigned calls = 0; size_t used = 0;
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.geometry = {0,0,2,2}; run.lengths = {2,2,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1; run.draw_source = svg_filter_test_source;
    run.source_context = &calls; run.work_used = &used; run.work_limit = 23;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    EXPECT_EQ(calls, 1u); EXPECT_EQ(used, 20u);
    used = 0; run.work_limit = 24;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    EXPECT_EQ(used, 24u); EXPECT_EQ(calls, 2u); EXPECT_EQ(((uint32_t*)result->pixels)[0], 0xff808080u);
    image_surface_destroy(result);
    node->kind = RDT_SVG_FILTER_MATRIX; node->linear = true;
    node->values[0] = node->values[6] = node->values[12] = node->values[18] = 1;
    used = 0; run.work_limit = 123;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    EXPECT_EQ(used, 120u);
    used = 0; run.work_limit = 124;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(used, 124u);
    uint32_t pixel = ((uint32_t*)result->pixels)[0];
    for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, 128u, 1);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, StandardInputsAreCapturedLazilyAndBackgroundAlphaSharesItsImage) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 2, 2);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_MERGE; node->valid = true;
    const int inputs[] = {RDT_SVG_FILTER_FILL, RDT_SVG_FILTER_STROKE, RDT_SVG_FILTER_BACKGROUND, RDT_SVG_FILTER_BACKGROUND_ALPHA};
    node->merge_count = 4; node->merge_inputs = (int*)arena_alloc(program->arena, sizeof(inputs));
    ASSERT_NE(node->merge_inputs, nullptr); memcpy(node->merge_inputs, inputs, sizeof(inputs));
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    unsigned calls[3] = {};
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.geometry = {0,0,2,2}; run.lengths = {2,2,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1; run.draw_input = svg_filter_test_input; run.input_context = calls;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned slot = 0; slot < 3; slot++) EXPECT_EQ(calls[slot], 1u);
    for (unsigned index = 0; index < 4; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0xe8401018u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, GaussianExtensionSamplesTheInputBordersAndOppositeEdge) {
    for (unsigned axis = 0; axis < 2; axis++) {
        DomDocument document; Element element = {};
        unsigned width = axis ? 1 : 6, height = axis ? 6 : 1;
        RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, (float)width, (float)height);
        ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
        RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_BLUR; node->valid = true;
        node->input = RDT_SVG_FILTER_SOURCE; node->values[axis] = 1;
        ImageSurface* source = image_surface_create(width, height); ASSERT_NE(source, nullptr);
        for (unsigned index = 0; index < 6; index++) ((uint32_t*)source->pixels)[index] = index < 3 ? 0xff0000ffu : 0xffff0000u;
        Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
        RdtSvgFilterRun run = {}; run.scratch = &scratch; run.source = source; run.geometry = run.source_bounds = {0,0,(float)width,(float)height};
        run.lengths = {(float)width,(float)height,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
        // normalized Gaussian taps at sigma 1 yield distinct transparent, duplicated and wrapped edge colors.
        const uint32_t expected[] = {0xb20100b1u, 0xff0100feu, 0xff4e00b1u};
        for (unsigned mode = 0; mode < 3; mode++) {
            node->variant = mode; ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
            ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
            uint32_t pixel = ((uint32_t*)result->pixels)[0];
            for (unsigned channel = 0; channel < 4; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, (expected[mode] >> (channel * 8)) & 255u, 1);
            image_surface_destroy(result);
        }
        image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
        render_svg_filter_program_release(program); document.destroy();
    }
}

TEST(SvgFilterTest, FractionalTilesInterpolateAcrossBothPeriodicBorders) {
    for (unsigned axis = 0; axis < 2; axis++) {
        DomDocument document; Element element = {};
        float width = axis ? 1 : 12, height = axis ? 12 : 1;
        RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, width, height);
        ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
        RdtSvgFilterNode* input = &program->nodes[0]; input->kind = RDT_SVG_FILTER_OFFSET;
        input->valid = true; input->input = RDT_SVG_FILTER_SOURCE;
        input->region[axis + 2] = arena_strdup(program->arena, "5.5");
        RdtSvgFilterNode* tile = &program->nodes[1]; tile->kind = RDT_SVG_FILTER_TILE;
        tile->valid = true; tile->input = 0;
        ImageSurface* source = image_surface_create(axis ? 1 : 6, axis ? 6 : 1); ASSERT_NE(source, nullptr);
        for (unsigned index = 0; index < 6; index++) ((uint32_t*)source->pixels)[index] = 0xff00ff00u;
        Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
        RdtSvgFilterRun run = {}; run.scratch = &scratch; run.source = source;
        run.geometry = {0,0,width,height}; run.source_bounds = {0,0,(float)source->width,(float)source->height};
        run.lengths = {width,height,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
        ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        // a constant opaque input stays opaque across fractional repeat seams in either axis.
        for (unsigned index = 0; index < 12; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0xff00ff00u);
        image_surface_destroy(result); image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
        render_svg_filter_program_release(program); document.destroy();
    }
}

TEST(SvgFilterTest, LightingUsesSobelSlopesAtAllImageEdgesAndSpecularPremultiplication) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 5, 5);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->valid = true; node->input = RDT_SVG_FILTER_ALPHA;
    node->kind = RDT_SVG_FILTER_DIFFUSE; node->color.r = node->color.g = node->color.b = node->color.a = 255;
    node->light.surface_scale = node->light.constant = 1; node->light.kind = 1;
    node->light.position[0] = .6f; node->light.position[2] = .8f;
    ImageSurface* source = image_surface_create(5, 5); ASSERT_NE(source, nullptr);
    for (unsigned row = 0; row < 5; row++) for (unsigned column = 0; column < 5; column++)
        ((uint32_t*)source->pixels)[row * 5 + column] = (32u + 16u * column + 8u * row) << 24;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.source = source; run.geometry = run.source_bounds = {0,0,5,5};
    run.lengths = {5,5,16,8}; run.frame = rdt_matrix_identity(); run.density = 1;
    // the affine height field has the same analytically known normal at corners, edges and interiors.
    double nx = -32.0 / 255.0, ny = -16.0 / 255.0;
    unsigned expected = clamp_byte_round((float)((.6 * nx + .8) / sqrt(nx * nx + ny * ny + 1.0) * 255.0));
    Bound bounds = {}; RdtMatrix placement; ImageSurface* result = nullptr;
    for (unsigned kernel = 1; kernel <= 2; kernel++) {
        node->light.kernel[0] = node->light.kernel[1] = (float)kernel;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        for (unsigned index = 0; index < 25; index++) {
            uint32_t pixel = ((uint32_t*)result->pixels)[index];
            EXPECT_EQ(pixel >> 24, 255u);
            for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR((pixel >> (channel * 8)) & 255u, expected, 1);
        }
        image_surface_destroy(result); result = nullptr;
    }
    for (unsigned index = 0; index < 25; index++) ((uint32_t*)source->pixels)[index] = 0xff000000u;
    node->kind = RDT_SVG_FILTER_SPECULAR; node->light.constant = .5f; node->light.exponent = 4;
    node->light.position[0] = sqrtf(.75f); node->light.position[2] = .5f; node->color.g = node->color.b = 0;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned index = 0; index < 25; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0x48000048u);
    image_surface_destroy(result); result = nullptr;
    node->light.constant = 0;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    for (unsigned index = 0; index < 25; index++) EXPECT_EQ(((uint32_t*)result->pixels)[index], 0u);
    image_surface_destroy(result); result = nullptr;
    run.work_limit = 50;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement)); EXPECT_EQ(result, nullptr);
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}


TEST(SvgFilterTest, NamedGraphUsesPremultipliedSourceAndLeavesSourcePixelsOwned) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    program->nodes[0].kind = RDT_SVG_FILTER_OFFSET; program->nodes[0].valid = true;
    program->nodes[0].input = RDT_SVG_FILTER_SOURCE; program->nodes[0].input2 = RDT_SVG_FILTER_EMPTY;
    program->nodes[0].values[0] = 2.0f; program->nodes[0].result = arena_strdup(program->arena, "moved");
    RdtSvgFilterNode* matrix = &program->nodes[1]; matrix->kind = RDT_SVG_FILTER_MATRIX; matrix->valid = true;
    matrix->input = render_svg_filter_input(program, 1, "moved"); matrix->input2 = RDT_SVG_FILTER_EMPTY;
    matrix->values[5] = matrix->values[18] = 1.0f;
    EXPECT_EQ(matrix->input, 0);
    EXPECT_EQ(render_svg_filter_input(program, 1, "unknown"), 0);
    EXPECT_EQ(render_svg_filter_input(program, 0, "forward"), RDT_SVG_FILTER_SOURCE);
    EXPECT_EQ(render_svg_filter_input(program, 1, nullptr), 0);
    ImageSurface* source = image_surface_create(2, 2);
    ASSERT_NE(source, nullptr);
    for (size_t index = 0; index < 4; index++) ((uint32_t*)source->pixels)[index] = 0x80000080u;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.source = source;
    run.source_bounds = {0,0,2,2}; run.geometry = {0,0,2,2}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(((uint32_t*)result->pixels)[2], 0x80008000u);
    EXPECT_EQ(((uint32_t*)result->pixels)[0], 0u);
    EXPECT_EQ(((uint32_t*)source->pixels)[0], 0x80000080u);
    ImageSurface* retained = result; result = nullptr;
    run.work_limit = 1;
    EXPECT_FALSE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    EXPECT_EQ(result, nullptr);
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
    // deferred paint owns its output after input, execution scratch and program teardown.
    EXPECT_EQ(((uint32_t*)retained->pixels)[2], 0x80008000u);
    EXPECT_EQ(((uint32_t*)retained->pixels)[0], 0u);
    image_surface_destroy(retained);
}

TEST(SvgFilterTest, CoordinatorPreservesPinnedProgramsAndMutationRetiresOldFacts) {
    DomDocument document; Element element = {};
    document.services.mem_ctx = mem_context_create(nullptr, MEM_ROLE_RENDER, "test.svg.filter");
    MemContext* memory = (MemContext*)document.services.mem_ctx;
    ASSERT_NE(memory, nullptr);
    RdtSvgFilterProgram* original = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(original, nullptr);
    original->compiled = original->valid = true;
    EXPECT_EQ(render_svg_filter_program_acquire(&document, &element), original);
    render_svg_filter_program_release(original);
    EXPECT_EQ(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    document.mutation_epoch++;
    RdtSvgFilterProgram* changed = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(changed, nullptr); EXPECT_NE(changed, original);
    EXPECT_FALSE(changed->compiled); EXPECT_TRUE(original->compiled);
    render_svg_filter_program_release(original); render_svg_filter_program_release(changed);
    EXPECT_GT(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    RdtSvgFilterProgram* fresh = render_svg_filter_program_acquire(&document, &element);
    ASSERT_NE(fresh, nullptr); EXPECT_FALSE(fresh->compiled);
    render_svg_filter_program_release(fresh); document.destroy();
    EXPECT_EQ(mem_context_live_count(memory), 0u);
    mem_context_destroy(memory);
}

TEST(SvgFilterTest, MorphologySeparatesAxesAndPreservesPremultipliedExtrema) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 9, 7);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_MORPHOLOGY;
    node->valid = true; node->input = RDT_SVG_FILTER_SOURCE;
    ImageSurface* source = image_surface_create(9, 7); ASSERT_NE(source, nullptr);
    for (unsigned row = 1; row < 6; row++) for (unsigned column = 2; column < 7; column++)
        ((uint32_t*)source->pixels)[row * 9 + column] = 0x80000080u;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.source = source;
    run.source_bounds = run.geometry = {0,0,9,7}; run.lengths = {9,7,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    for (unsigned dilate = 0; dilate < 2; dilate++) for (unsigned axis = 0; axis < 2; axis++) {
        node->variant = dilate; node->values[0] = axis ? 0 : 2; node->values[1] = axis ? 1 : 0;
        ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
        ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        for (unsigned row = 0; row < 7; row++) for (unsigned column = 0; column < 9; column++) {
            bool inside = axis ? (column >= 2 && column < 7 && row >= (dilate ? 0u : 2u) && row < (dilate ? 7u : 5u)) :
                (row >= 1 && row < 6 && column >= (dilate ? 0u : 4u) && column < (dilate ? 9u : 5u));
            EXPECT_EQ(((uint32_t*)result->pixels)[row * 9 + column], inside ? 0x80000080u : 0u);
        }
        image_surface_destroy(result);
    }
    image_surface_destroy(source); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, NonseparableBlendsPreserveLuminosityAndHandleGray) {
    float mixed[3];
    render_composite_blend_rgb(0xffff0000u, 0xff0000ffu, CSS_VALUE_HUE_BLEND, mixed);
    EXPECT_NEAR(.3f * mixed[0] + .59f * mixed[1] + .11f * mixed[2], .11f, .00001f);
    EXPECT_GT(mixed[0], mixed[2]); EXPECT_NEAR(mixed[1], 0.0f, .00001f);
    render_composite_blend_rgb(0xff808080u, 0xff0000ffu, CSS_VALUE_SATURATION_BLEND, mixed);
    for (unsigned channel = 0; channel < 3; channel++) EXPECT_NEAR(mixed[channel], 128.0f / 255.0f, .00001f);
    EXPECT_EQ(render_composite_blend_pixel(0, 0x80000080u, CSS_VALUE_COLOR_BLEND), 0x80000080u);
    EXPECT_EQ(render_composite_blend_pixel(0xff123456u, 0, CSS_VALUE_LUMINOSITY_BLEND), 0xff123456u);
}

TEST(SvgFilterTest, SurfaceBudgetReclaimsUnpinnedGraphsAndRejectsOversizedGrid) {
    DomDocument document; Element element = {};
    document.services.mem_ctx = mem_context_create(nullptr, MEM_ROLE_RENDER, "test.svg.filter.budget");
    MemContext* memory = (MemContext*)document.services.mem_ctx; ASSERT_NE(memory, nullptr);
    RdtSvgFilterProgram* program = render_svg_filter_program_acquire(&document, &element); ASSERT_NE(program, nullptr);
    render_svg_filter_program_release(program);
    size_t soft, hard, critical; memtrack_get_limits(&soft, &hard, &critical);
    size_t usage = memtrack_get_current_usage();
    memtrack_set_limits(usage, usage, usage + 256);
    EXPECT_EQ(render_surface_create_budgeted(memory, 1024, 1024), nullptr);
    EXPECT_EQ(document.services.svg_filter_registry != nullptr, true);
    EXPECT_EQ(mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, SIZE_MAX), 0u);
    memtrack_set_limits(soft, hard, critical);
    ImageSurface* surface = render_surface_create_budgeted(memory, 8, 8);
    EXPECT_NE(surface, nullptr); if (surface) image_surface_destroy(surface);
    document.destroy(); EXPECT_EQ(mem_context_live_count(memory), 0u); mem_context_destroy(memory);
}

TEST(SvgFilterTest, UnusedTreeDoesNotSpendWorkOrCaptureSource) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 2, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* blur = &program->nodes[0]; blur->kind = RDT_SVG_FILTER_BLUR; blur->valid = true;
    blur->input = RDT_SVG_FILTER_SOURCE; blur->values[0] = blur->values[1] = 1e20f;
    RdtSvgFilterNode* flood = &program->nodes[1]; flood->kind = RDT_SVG_FILTER_FLOOD; flood->valid = true;
    flood->color.r = 255; flood->color.a = 255;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.geometry = {0,0,8,8}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f; run.work_limit = 64;
    unsigned calls = 0; run.draw_source = svg_filter_test_source; run.source_context = &calls;
    ImageSurface* result = nullptr; Bound bounds = {}; RdtMatrix placement;
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
    ASSERT_NE(result, nullptr); EXPECT_EQ(((uint32_t*)result->pixels)[0], 0xff0000ffu);
    EXPECT_EQ(calls, 0u);
    image_surface_destroy(result); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(SvgFilterTest, NoiseSeedTruncationAndRegenerationAreDeterministic) {
    DomDocument document; Element element = {};
    RdtSvgFilterProgram* program = svg_filter_test_program(&document, &element, 1, 8, 8);
    ASSERT_NE(program, nullptr); ASSERT_NE(program->nodes, nullptr);
    RdtSvgFilterNode* node = program->nodes; node->kind = RDT_SVG_FILTER_TURBULENCE; node->valid = true;
    node->variant = 1; node->values[0] = .08f; node->values[1] = .1f; node->values[2] = 2;
    Arena* arena = arena_create_default(); ScratchArena scratch = {}; scratch_init(&scratch, arena);
    RdtSvgFilterRun run = {}; run.scratch = &scratch; run.geometry = {0,0,8,8}; run.lengths = {8,8,16,8};
    run.frame = rdt_matrix_identity(); run.density = 1.0f;
    ImageSurface* original = nullptr; Bound bounds = {}; RdtMatrix placement;
    node->noise = render_svg_filter_noise_create(program->arena, nullptr, -4.8f); ASSERT_NE(node->noise, nullptr);
    ASSERT_TRUE(render_svg_filter_execute(program, &run, &original, &bounds, &placement));
    for (unsigned repetition = 0; repetition < 2; repetition++) {
        node->noise = render_svg_filter_noise_create(program->arena, nullptr, -4.0f); ASSERT_NE(node->noise, nullptr);
        ImageSurface* result = nullptr; ASSERT_TRUE(render_svg_filter_execute(program, &run, &result, &bounds, &placement));
        EXPECT_EQ(memcmp(original->pixels, result->pixels, 8 * 8 * 4), 0);
        for (size_t index = 0; index < 64; index++) {
            uint32_t pixel = ((uint32_t*)result->pixels)[index], alpha = pixel >> 24;
            for (unsigned channel = 0; channel < 3; channel++) EXPECT_LE((pixel >> (channel * 8)) & 255u, alpha);
        }
        image_surface_destroy(result);
    }
    image_surface_destroy(original); scratch_release(&scratch); arena_destroy(arena);
    render_svg_filter_program_release(program); document.destroy();
}

TEST(RdtPathMetricsTest, SubpathsDoNotCountMoveGapsAndEndpointsExtrapolate) {
    RdtPath* path = svg_parse_path_d("M0 0L30 40M100 100L100 140");
    ASSERT_NE(path, nullptr);
    RdtPathMetrics metrics = {};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    EXPECT_NEAR(metrics.length, 90.0f, .001f);
    EXPECT_EQ(metrics.subpath_count, 2u);
    EXPECT_FALSE(metrics.closed);
    float x, y, tx, ty;
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 25.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 15.0f, .001f); EXPECT_NEAR(y, 20.0f, .001f);
    EXPECT_NEAR(tx, .6f, .001f); EXPECT_NEAR(ty, .8f, .001f);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 70.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 100.0f, .001f); EXPECT_NEAR(y, 120.0f, .001f);
    float distance, normal;
    ASSERT_TRUE(render_path_metrics_project(&metrics, 110.0f, 120.0f, 50.0f, 90.0f, &distance, &normal));
    EXPECT_NEAR(distance, 70.0f, .001f); EXPECT_NEAR(normal, -10.0f, .001f);
    EXPECT_FALSE(render_path_metrics_sample(&metrics, -10.0f, &x, &y, &tx, &ty));
    ASSERT_TRUE(render_path_metrics_sample(&metrics, -10.0f, &x, &y, &tx, &ty, true));
    EXPECT_NEAR(x, -6.0f, .001f); EXPECT_NEAR(y, -8.0f, .001f);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 100.0f, &x, &y, &tx, &ty, true));
    EXPECT_NEAR(x, 100.0f, .001f); EXPECT_NEAR(y, 150.0f, .001f);
    render_path_metrics_destroy(&metrics); rdt_path_free(path);
}

TEST(RdtPathMetricsTest, CubicArcLengthAndAffineCompactContours) {
    RdtPath* path = svg_parse_path_d("M0 0C0 100 100 100 100 0");
    ASSERT_NE(path, nullptr);
    RdtPathMetrics metrics = {};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    EXPECT_NEAR(metrics.length, 200.0f, .02f);
    float x, y, tx, ty;
    ASSERT_TRUE(render_path_metrics_sample(&metrics, metrics.length * .5f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 50.0f, .02f); EXPECT_NEAR(y, 75.0f, .02f);
    EXPECT_NEAR(tx, 1.0f, .01f); EXPECT_NEAR(ty, 0.0f, .02f);
    rdt_path_free(path);
    path = rdt_path_new(); rdt_path_add_rect(path, 0, 0, 10, 20, 0, 0);
    RdtMatrix mapping = {2, 0, 30, 0, 3, 40, 0, 0, 1};
    ASSERT_TRUE(render_path_metrics_build(&metrics, path, &mapping));
    EXPECT_NEAR(metrics.length, 160.0f, .001f);
    EXPECT_TRUE(metrics.closed);
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 50.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 50.0f, .001f); EXPECT_NEAR(y, 70.0f, .001f);
    float distance, normal;
    ASSERT_TRUE(render_path_metrics_project(&metrics, 35.0f, 35.0f, 140.0f, 190.0f, &distance, &normal, true));
    EXPECT_NEAR(distance, 165.0f, .001f); EXPECT_NEAR(normal, -5.0f, .001f);
    rdt_path_free(path);
    path = rdt_path_new(); rdt_path_add_circle(path, 10, 20, 30, 15);
    ASSERT_TRUE(render_path_metrics_build(&metrics, path));
    ASSERT_TRUE(render_path_metrics_sample(&metrics, 0.0f, &x, &y, &tx, &ty));
    EXPECT_NEAR(x, 40.0f, .001f); EXPECT_NEAR(y, 20.0f, .001f);
    EXPECT_GT(ty, .99f);
    render_path_metrics_destroy(&metrics); rdt_path_free(path);
}

TEST(RdtImageTest, AlphaModeSurvivesOwnedImageReleaseAndSeparatesCachedUploads) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    ImageSurface* image = image_surface_create(1, 1);
    ASSERT_NE(image, nullptr);
    image->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    ((uint32_t*)image->pixels)[0] = 0x80000064u;
    paint_draw_image_resource(&paint, image, 0, 0, 8, 8, 255, nullptr, image_surface_destroy);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy();
    ASSERT_EQ(dl.item_count(), 1);
    EXPECT_TRUE(dl.data()[0].draw_image.straight_alpha);
    uint32_t pixels[64];
    for (uint32_t& pixel : pixels) pixel = 0xffffffffu;
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 8, 8, 8);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0], false), DL_REPLAY_VECTOR_DREW);
    EXPECT_NEAR((float)(pixels[36] & 255u), 177.0f, 1.0f);
    EXPECT_NEAR((float)((pixels[36] >> 8) & 255u), 127.0f, 1.0f);
    const uint32_t source = 0x80000064u;
    for (bool straight : {false, true, false}) {
        for (uint32_t& pixel : pixels) pixel = 0xffffffffu;
        rdt_draw_image(&vector, &source, 1, 1, 1, 0, 0, 8, 8, 255, nullptr, 1, straight);
        EXPECT_NEAR((float)(pixels[36] & 255u), straight ? 177.0f : 227.0f, 1.0f);
        EXPECT_EQ(source, 0x80000064u);
    }
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena);
}

TEST(SvgStrokeTest, MiterLimitSurvivesRecordingAndChangesCachedStrokeGeometry) {
    rdt_engine_init(0);
    RdtPath* path = svg_parse_path_d("M20 100L50 20L80 100");
    ASSERT_NE(path, nullptr);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    Color blue = {}; blue.b = blue.a = 255;
    float dashes[] = {1000.0f, 10.0f};
    paint_stroke_path(&paint, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        dashes, 2, 0.0f, nullptr, 1.0f);
    dashes[0] = 0.0f;
    ASSERT_TRUE(paint_ir_validate(&paint, nullptr));
    EXPECT_FLOAT_EQ(paint.data()[0].stroke_path.dash_array[0], 1000.0f);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy();
    EXPECT_FLOAT_EQ(dl.data()[0].stroke_path.miter_limit, 1.0f);
    uint32_t pixels[100 * 120] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 100, 120, 100);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0], false), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 100 + 50], 0u);
    memset(pixels, 0, sizeof(pixels));
    rdt_stroke_path(&vector, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        nullptr, 0, 0, nullptr, 10.0f);
    EXPECT_EQ(pixels[10 * 100 + 50], 0xffff0000u);
    memset(pixels, 0, sizeof(pixels));
    rdt_stroke_path(&vector, path, blue, 12.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
        nullptr, 0, 0, nullptr, 1.0f);
    EXPECT_EQ(pixels[10 * 100 + 50], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena); rdt_path_free(path);
}

TEST(RdtPathTransformTest, CompactEllipseAndRoundedRectangleExpandBeforeAffineStroke) {
    RdtPath* source = rdt_path_new();
    RdtPath* transformed = rdt_path_new();
    rdt_path_add_circle(source, 0, 0, 10, 4);
    RdtMatrix matrix = {2, 1, 10, 0, 3, 20, 0, 0, 1};
    ASSERT_TRUE(render_path_append_transformed(transformed, source, &matrix));
    float left, top, right, bottom;
    ASSERT_TRUE(rdt_path_get_bounds(transformed, &left, &top, &right, &bottom));
    EXPECT_NEAR(left, 10.0f - sqrtf(416.0f), .02f);
    EXPECT_NEAR(right, 10.0f + sqrtf(416.0f), .02f);
    EXPECT_FLOAT_EQ(top, 8.0f); EXPECT_FLOAT_EQ(bottom, 32.0f);
    rdt_path_free(source); rdt_path_free(transformed);
    source = rdt_path_new(); transformed = rdt_path_new();
    rdt_path_add_rect(source, 0, 0, 20, 10, 3, 2);
    RdtMatrix rotation = {0, -1, 50, 1, 0, 20, 0, 0, 1};
    ASSERT_TRUE(render_path_append_transformed(transformed, source, &rotation));
    ASSERT_TRUE(rdt_path_get_bounds(transformed, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(left, 40); EXPECT_FLOAT_EQ(right, 50);
    EXPECT_FLOAT_EQ(top, 20); EXPECT_FLOAT_EQ(bottom, 40);
    rdt_path_free(source); rdt_path_free(transformed);
}

TEST(SvgGradientTest, RecordingOwnsStrokeDashesAndReplayPreservesSpread) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintList paint;
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, 10.0f, 10.0f, 60.0f, 30.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.5f, 255, 0, 0, 255}, {.5f, 0, 0, 255, 255}};
    float dashes[] = {10.0f, 10.0f};
    RdtGradientOptions options = {};
    options.spread = RDT_GRADIENT_REPEAT;
    options.stroke_width = 8.0f; options.miter_limit = 6.0f;
    options.dash_array = dashes; options.dash_count = 2;
    paint_fill_linear_gradient(&paint, path, 10.0f, 10.0f, 30.0f, 10.0f,
        stops, 2, RDT_FILL_WINDING, nullptr, nullptr, &options);
    dashes[0] = 0.0f;
    ASSERT_TRUE(paint_ir_validate(&paint, nullptr));
    EXPECT_FLOAT_EQ(paint.data()[0].fill_linear_gradient.options.dash_array[0], 10.0f);
    paint_ir_lower_raster(&paint, &dl);
    paint.destroy(); rdt_path_free(path);
    const DlFillLinearGradient* gradient = &dl.data()[0].fill_linear_gradient;
    EXPECT_EQ(gradient->options.spread, RDT_GRADIENT_REPEAT);
    EXPECT_FLOAT_EQ(gradient->options.miter_limit, 6.0f);
    EXPECT_FLOAT_EQ(gradient->options.dash_array[0], 10.0f);
    uint32_t pixels[80 * 50] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 80, 50, 80);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0], false), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 80 + 15], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 35], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 25], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&dl); arena_destroy(arena);
    rdt_engine_term();
}

TEST(SvgGradientTest, RetainedFragmentOwnsStrokeFactsAfterRecordingArenaDies) {
    rdt_engine_init(0);
    Pool* pool = pool_create(); Arena* arena = arena_create_default();
    DisplayList source = {}; dl_init(&source, arena);
    int begin = dl_begin_element(&source, 42, 0.0f, 0.0f, 80.0f, 50.0f);
    RdtPath* path = rdt_path_new(); rdt_path_add_rect(path, 10.0f, 10.0f, 60.0f, 30.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.5f, 255, 0, 0, 255}, {.5f, 0, 0, 255, 255}};
    float dashes[] = {10.0f, 10.0f};
    RdtGradientOptions options = {}; options.spread = RDT_GRADIENT_REPEAT;
    options.stroke_width = 8.0f; options.miter_limit = 6.0f; options.dash_array = dashes; options.dash_count = 2;
    PaintList paint;
    paint_fill_linear_gradient(&paint, path, 10.0f, 10.0f, 30.0f, 10.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    StrBuf* svg = strbuf_new(); PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&paint, svg, nullptr, &stats);
    EXPECT_EQ(stats.emitted_count, 1);
    EXPECT_NE(strstr(svg->str, "spreadMethod=\"repeat\""), nullptr);
    EXPECT_NE(strstr(svg->str, "stroke=\"url(#paint-ir-linear-"), nullptr);
    EXPECT_NE(strstr(svg->str, "stroke-miterlimit=\"6.000\""), nullptr);
    strbuf_free(svg);
    paint_ir_lower_raster(&paint, &source); dl_end_element(&source, begin);
    RetainedDisplayListCache* cache = retained_dl_cache_create(pool);
    retained_dl_cache_capture(cache, &source);
    const RetainedDisplayListFragment* fragment = retained_dl_cache_get(cache, 42);
    ASSERT_NE(fragment, nullptr);
    paint.destroy(); dl_destroy(&source); arena_destroy(arena); rdt_path_free(path);
    memset(stops, 0, sizeof(stops)); memset(dashes, 0, sizeof(dashes));
    arena = arena_create_default(); DisplayList replay = {}; dl_init(&replay, arena);
    ASSERT_TRUE(retained_dl_append_fragment(&replay, fragment));
    ASSERT_TRUE(dl_validate(&replay, nullptr));
    const DlFillLinearGradient* gradient = &replay.data()[1].fill_linear_gradient;
    EXPECT_EQ(gradient->options.spread, RDT_GRADIENT_REPEAT);
    EXPECT_FLOAT_EQ(gradient->options.miter_limit, 6.0f);
    EXPECT_FLOAT_EQ(gradient->options.dash_array[0], 10.0f);
    uint32_t pixels[80 * 50] = {}; RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 80, 50, 80);
    EXPECT_EQ(dl_replay_vector_item(&vector, &replay.data()[1], false), DL_REPLAY_VECTOR_DREW);
    EXPECT_EQ(pixels[10 * 80 + 15], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 35], 0xff0000ffu);
    EXPECT_EQ(pixels[10 * 80 + 25], 0u);
    rdt_vector_destroy(&vector); dl_destroy(&replay); arena_destroy(arena);
    retained_dl_cache_destroy(cache); pool_destroy(pool); rdt_engine_term();
}

TEST(SvgGradientTest, FocalAndSpreadFactsParticipateInBackendCacheKeys) {
    rdt_engine_init(0);
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, 0.0f, 0.0f, 60.0f, 60.0f, 0.0f, 0.0f);
    RdtGradientStop stops[] = {{.2f, 255, 0, 0, 255}, {.2f, 0, 0, 255, 255}};
    uint32_t pixels[60 * 60] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 60, 60, 60);
    RdtGradientOptions options = {};
    options.has_focal = true; options.fx = 20.0f; options.fy = 30.0f;
    rdt_fill_radial_gradient(&vector, path, 30.0f, 30.0f, 25.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    EXPECT_EQ(pixels[30 * 60 + 20], 0xff0000ffu);
    EXPECT_EQ(pixels[30 * 60 + 40], 0xffff0000u);
    memset(pixels, 0, sizeof(pixels));
    options.fx = 40.0f;
    rdt_fill_radial_gradient(&vector, path, 30.0f, 30.0f, 25.0f, stops, 2,
        RDT_FILL_WINDING, nullptr, nullptr, &options);
    EXPECT_EQ(pixels[30 * 60 + 40], 0xff0000ffu);
    EXPECT_EQ(pixels[30 * 60 + 20], 0xffff0000u);
    options = {};
    for (RdtGradientSpread spread : {RDT_GRADIENT_PAD, RDT_GRADIENT_REPEAT, RDT_GRADIENT_REFLECT}) {
        memset(pixels, 0, sizeof(pixels)); options.spread = spread;
        rdt_fill_linear_gradient(&vector, path, 0.0f, 0.0f, 20.0f, 0.0f, stops, 2,
            RDT_FILL_WINDING, nullptr, nullptr, &options);
        EXPECT_EQ(pixels[30 * 60 + 22], spread == RDT_GRADIENT_REPEAT ? 0xff0000ffu : 0xffff0000u);
    }
    rdt_vector_destroy(&vector); rdt_path_free(path); rdt_engine_term();
}

TEST(SvgPatternTest, NestedOffscreenClipsPreserveSuspendedPathsAcrossStackGrowth) {
    rdt_engine_init(0);
    uint32_t pixels[20 * 10] = {};
    RdtVector vector = {}; rdt_vector_init(&vector, pixels, 20, 10, 20);
    RdtPath* outer = rdt_path_new(); rdt_path_add_rect(outer, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
    RdtPath* inner = rdt_path_new(); rdt_path_add_rect(inner, 10.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
    rdt_push_clip(&vector, outer, nullptr);
    int saved = rdt_clip_save_depth();
    for (int i = 0; i < 9; i++) rdt_push_clip(&vector, inner, nullptr);
    Color blue = {}; blue.b = blue.a = 255;
    rdt_fill_rect(&vector, 0.0f, 0.0f, 20.0f, 10.0f, blue);
    EXPECT_EQ(pixels[5 * 20 + 5], 0u);
    EXPECT_EQ(pixels[5 * 20 + 15], 0xffff0000u);
    for (int i = 0; i < 9; i++) rdt_pop_clip(&vector);
    rdt_clip_restore_depth(saved);
    Color red = {}; red.r = red.a = 255;
    rdt_fill_rect(&vector, 0.0f, 0.0f, 20.0f, 10.0f, red);
    EXPECT_EQ(pixels[5 * 20 + 5], 0xff0000ffu);
    EXPECT_EQ(pixels[5 * 20 + 15], 0xffff0000u);
    rdt_pop_clip(&vector);
    rdt_path_free(inner); rdt_path_free(outer); rdt_vector_destroy(&vector); rdt_engine_term();
}

TEST(SvgImageTest, SharedGifAndWebPDecodersMatchPngAndRejectTruncation) {
    const char* paths[] = {"test/ui/svg_image_assets/split.png",
        "test/ui/svg_image_assets/split.gif", "test/ui/svg_image_assets/split.webp"};
    for (const char* path : paths) {
        int width = 0, height = 0, channels = 0;
        ASSERT_TRUE(image_get_dimensions(path, &width, &height));
        EXPECT_EQ(width, 20); EXPECT_EQ(height, 10);
        unsigned char* pixels = image_load(path, &width, &height, &channels, 4);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(channels, 4);
        EXPECT_EQ(pixels[0], 255); EXPECT_EQ(pixels[1], 0); EXPECT_EQ(pixels[2], 0);
        EXPECT_EQ(pixels[4 * 19], 0); EXPECT_EQ(pixels[4 * 19 + 2], 255);
        image_free(pixels);
        char* bytes = nullptr;
        size_t size = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_IMAGE, &bytes, &size));
        pixels = image_load_from_memory((unsigned char*)bytes, size, &width, &height, &channels);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(pixels[0], 255); EXPECT_EQ(pixels[4 * 19 + 2], 255);
        image_free(pixels);
        if (strstr(path, ".webp")) {
            EXPECT_FALSE(image_get_dimensions_from_memory((unsigned char*)bytes, 12, &width, &height));
            EXPECT_EQ(image_load_from_memory((unsigned char*)bytes, 12, &width, &height, &channels), nullptr);
        }
        mem_free(bytes);
    }
}

TEST(SvgImageTest, ReplayRefreshesPixelsAfterSharedDecodePromotion) {
    rdt_engine_init(0);
    Arena* arena = arena_create_default();
    DisplayList dl = {};
    dl_init(&dl, arena);
    uint32_t old_pixels[] = {0xff0000ffu};
    uint32_t new_pixels[] = {0xffff0000u, 0xffff0000u};
    // shared decode owners now register the handle used by deferred replay.
    ImageSurface* image = image_surface_alloc();
    ASSERT_NE(image, nullptr); ASSERT_FALSE(image->self.is_null());
    image->width = image->height = 1;
    image->pitch = 4;
    image->pixels = old_pixels;
    image->generation = 1;
    dl_draw_image(&dl, old_pixels, 1, 1, 1, 0.0f, 0.0f, 8.0f, 8.0f,
        255, nullptr, image, image->generation);
    image->pixels = new_pixels;
    image->decoded_width = 2;
    image->decoded_height = 1;
    image->pitch = 8;
    image->generation++;
    uint32_t pixels[8 * 8] = {};
    RdtVector vector = {};
    rdt_vector_init(&vector, pixels, 8, 8, 8);
    rdt_vector_begin_batch(&vector);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0], false), DL_REPLAY_VECTOR_DREW);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(pixels[4 * 8 + 4], 0xffff0000u);
    image_surface_detach_pixels(image); image_surface_destroy(image);
    memset(pixels, 0, sizeof(pixels));
    rdt_vector_begin_batch(&vector);
    EXPECT_EQ(dl_replay_vector_item(&vector, &dl.data()[0], false), DL_REPLAY_VECTOR_DREW);
    rdt_vector_end_batch(&vector);
    EXPECT_EQ(pixels[4 * 8 + 4], 0u);
    rdt_vector_destroy(&vector);
    dl_destroy(&dl);
    arena_destroy(arena);
    rdt_engine_term();
}

TEST(SvgCascadeTest, InlineDefaultVisibilityUsesVisibilityDomain) {
    EXPECT_EQ(INLINE_PROP_DEFAULT.visibility, VIS_VISIBLE);
}

TEST(SvgCascadeTest, FontShorthandProjectsResetsAndPreservesDeferredVariables) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* source = "font:italic 700 24px/1.5 Missing, SVG-Ahem; font:20px SVG-Ahem; font:var(--face); font:20px";
    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text(source, strlen(source), pool, &count);
    ASSERT_EQ(count, 3u); ASSERT_NE(declarations, nullptr);
    const CssValue* size = css_font_shorthand_longhand(declarations[0]->value, "font-size", pool);
    ASSERT_NE(size, nullptr); EXPECT_EQ(size->type, CSS_VALUE_TYPE_LENGTH); EXPECT_EQ(size->data.length.value, 24);
    const CssValue* weight = css_font_shorthand_longhand(declarations[0]->value, "font-weight", pool);
    ASSERT_NE(weight, nullptr); EXPECT_EQ(weight->data.number.value, 700);
    const CssValue* families = css_font_shorthand_longhand(declarations[0]->value, "font-family", pool);
    ASSERT_NE(families, nullptr); EXPECT_EQ(families->data.list.count, 2); EXPECT_TRUE(families->data.list.comma_separated);
    const CssValue* reset = css_font_shorthand_longhand(declarations[1]->value, "font-weight", pool);
    ASSERT_NE(reset, nullptr); EXPECT_EQ(reset->type, CSS_VALUE_TYPE_KEYWORD); EXPECT_EQ(reset->data.keyword, CSS_VALUE_NORMAL);
    EXPECT_EQ(declarations[2]->value->type, CSS_VALUE_TYPE_FUNCTION);
    pool_destroy(pool);
}

TEST(SvgConditionalTest, LanguageMatchingUsesPreferencePrefixesAndRefreshesDocumentEpoch) {
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, nullptr, ""));
    EXPECT_FALSE(dom_svg_conditions_match("", nullptr, "en"));
    EXPECT_FALSE(dom_svg_conditions_match("https://example.invalid/extension", nullptr, "en"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "", "en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, " zz, EN-gb ", "en"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "en", "en-GB"));
    EXPECT_FALSE(dom_svg_conditions_match(nullptr, "english", "en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, "en", "en-GB, en"));
    EXPECT_TRUE(dom_svg_conditions_match(nullptr, "fr-FR", "de, fr"));
    DomDocument document;
    uint64_t epoch = document.mutation_epoch;
    ASSERT_TRUE(dom_document_set_preferred_languages(&document, "fr, de"));
    EXPECT_STREQ(dom_document_preferred_languages(&document), "fr, de");
    EXPECT_EQ(document.mutation_epoch, epoch + 1);
    ASSERT_TRUE(dom_document_set_preferred_languages(&document, "fr, de"));
    EXPECT_EQ(document.mutation_epoch, epoch + 1);
    document.destroy();
}

TEST(SvgCascadeTest, InlineImportanceAndSelectorListSpecificity) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    CssEngine* engine = css_engine_create(pool);
    SelectorMatcher* matcher = selector_matcher_create(pool);
    DomElement node = {};
    node.tag_name = "rect";
    node.id = "target";
    const char* classes[] = {"paint"};
    node.class_names = classes;
    node.class_count = 1;
    CssStylesheet* sheets[] = {css_parse_stylesheet(engine,
        ".paint { fill:blue !important; } .paint, #target { stroke:red; } .paint { stroke:blue; }"
        ".paint { marker:url(#all); marker-end:url(#end); } #target { marker-start:none !important; }",
        nullptr)};
    size_t inline_count = 0;
    const char* inline_text = "fill:red !important";
    CssDeclaration** declarations = css_parse_declaration_list_text(
        inline_text, strlen(inline_text), pool, &inline_count);
    ASSERT_EQ(inline_count, 1u);
    ASSERT_TRUE(declarations[0]->important);
    CssDeclaration result = {};
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        declarations, inline_count, "fill", &result));
    EXPECT_TRUE(result.specificity.inline_style);
    EXPECT_STREQ(result.value_text, "red");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-start", &result));
    EXPECT_STREQ(result.value_text, "none");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-mid", &result));
    EXPECT_STREQ(result.value_text, "url(#all)");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "marker-end", &result));
    EXPECT_STREQ(result.value_text, "url(#end)");
    ASSERT_TRUE(css_select_element_declaration(engine, matcher, &node, sheets, 1,
        nullptr, 0, "stroke", &result));
    EXPECT_EQ(result.specificity.ids, 1u);
    EXPECT_STREQ(result.value_text, "red");
    selector_matcher_destroy(matcher);
    css_engine_destroy(engine);
    css_property_system_cleanup();
    pool_destroy(pool);
}

TEST(SvgCascadeTest, InvalidPaintDoesNotDisplaceValidDeclaration) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    size_t count = 0;
    const char* text = "fill:red;fill:invalid-color;stroke:context-fill";
    CssDeclaration** declarations = css_parse_declaration_list_text(text, strlen(text), pool, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(declarations[0]->value_text, "red");
    EXPECT_STREQ(declarations[1]->value_text, "context-fill");
    css_property_system_cleanup();
    pool_destroy(pool);
}

TEST(SvgCascadeTest, FontDescriptorChangesAdvancePaintResourceGeneration) {
    FontContext* context = font_context_create(nullptr);
    ASSERT_NE(context, nullptr);
    uint64_t before = font_context_resource_generation(context);
    FontFaceDesc face = {};
    face.family = "SVG resource generation fixture";
    face.weight = FONT_WEIGHT_NORMAL;
    face.slant = FONT_SLANT_NORMAL;
    ASSERT_TRUE(font_face_register(context, &face));
    uint64_t registered = font_context_resource_generation(context);
    EXPECT_GT(registered, before);
    font_face_clear(context);
    EXPECT_GT(font_context_resource_generation(context), registered);
    font_context_destroy(context);
}

TEST(SvgAnimationTest, ClockValuesUseSmilUnitsAndClockFields) {
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("150ms", -1.0), 0.15);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1.5min", -1.0), 90.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("01:02:03.5", -1.0), 3723.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("02:03.5", -1.0), 123.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-2s", -1.0), -2.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("00:99:00", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("2bogus", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("  150ms  ", -1.0), 0.15);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-01:02.5", -1.0), -62.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("-01:02:03.5", -1.0), -3723.5);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1:02", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("01:2", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1.5:02", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1 s", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(svg_animation_clock_value("1e2s", -1.0), -1.0);
}

TEST(SvgLengthTest, UsesViewportAxesFontMetricsAndRejectsUnknownUnits) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    EXPECT_FLOAT_EQ(svg_resolve_length("25%", &lengths, SVG_LENGTH_X, -1.0f), 50.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("25%", &lengths, SVG_LENGTH_Y, -1.0f), 25.0f);
    EXPECT_NEAR(svg_resolve_length("10%", &lengths, SVG_LENGTH_DIAGONAL, -1.0f), 15.811388f, 0.0001f);
    EXPECT_FLOAT_EQ(svg_resolve_length("2em", &lengths, SVG_LENGTH_X, -1.0f), 60.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("2ex", &lengths, SVG_LENGTH_X, -1.0f), 34.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("1in", &lengths, SVG_LENGTH_X, -1.0f), 96.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("12bogus", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("12%bad", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
}

TEST(SvgLengthTest, AnglesShareCssUnitConversionAndRejectInvalidSuffixes) {
    EXPECT_NEAR(svg_resolve_angle("0.25turn", -1), 90, 0.0001f);
    EXPECT_NEAR(svg_resolve_angle("100grad", -1), 90, 0.0001f);
    EXPECT_NEAR(svg_resolve_angle("1.5707963268rad", -1), 90, 0.0001f);
    EXPECT_FLOAT_EQ(svg_resolve_angle(" 90deg ", -1), 90);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90", -1), 90);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90px", -1), -1);
    EXPECT_FLOAT_EQ(svg_resolve_angle("90deg suffix", -1), -1);
}

TEST(SvgLengthTest, CssMathUsesSvgViewportAndMeasuredFontBases) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 2ex)", &lengths, SVG_LENGTH_X, -1.0f), 134.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(50% + 2ex)", &lengths, SVG_LENGTH_Y, -1.0f), 84.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("min(2em, 50%)", &lengths, SVG_LENGTH_Y, -1.0f), 50.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("clamp(1in, calc(50% + 2em), 200px)", &lengths, SVG_LENGTH_X, -1.0f), 160.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc((25% + 2ex) * 2)", &lengths, SVG_LENGTH_X, -1.0f), 168.0f);
    EXPECT_FLOAT_EQ(svg_resolve_length("calc(2bogus + 10px)", &lengths, SVG_LENGTH_X, -1.0f), -1.0f);
}

TEST(SvgLengthTest, DashZerosOddListsAndInvalidLists) {
    SvgLengthContext lengths = {200.0f, 100.0f, 30.0f, 17.0f};
    float dashes[32];
    ASSERT_EQ(svg_resolve_dash_array("0 10% 2em", &lengths, dashes, 32), 6);
    EXPECT_FLOAT_EQ(dashes[0], 0.0f);
    EXPECT_NEAR(dashes[1], 15.811388f, 0.0001f);
    EXPECT_FLOAT_EQ(dashes[2], 60.0f);
    EXPECT_FLOAT_EQ(dashes[3], 0.0f);
    EXPECT_EQ(svg_resolve_dash_array("0 0", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10 -1", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10,", &lengths, dashes, 32), 0);
    EXPECT_EQ(svg_resolve_dash_array("10bogus 1", &lengths, dashes, 32), 0);
    const char* long_list = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21";
    ASSERT_EQ(svg_resolve_dash_array(long_list, &lengths, nullptr, 0), 42);
    float long_dashes[42];
    ASSERT_EQ(svg_resolve_dash_array(long_list, &lengths, long_dashes, 42), 42);
    EXPECT_FLOAT_EQ(long_dashes[20], 21.0f); EXPECT_FLOAT_EQ(long_dashes[41], 21.0f);
}

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

typedef struct SvgFontPathProbe {
    float left, top, right, bottom;
    bool has_point;
    int closes;
    int calls;
    bool abort;
    float area_twice, current_x, current_y, start_x, start_y;
} SvgFontPathProbe;

static bool svg_font_path_probe(void* context, FontPathCommand command,
    const float* args, int count) {
    SvgFontPathProbe* probe = (SvgFontPathProbe*)context;
    probe->calls++;
    if (probe->abort) return false;
    if (command == FONT_PATH_MOVE) {
        probe->start_x = probe->current_x = args[0]; probe->start_y = probe->current_y = args[1];
    } else if (command == FONT_PATH_LINE || command == FONT_PATH_CLOSE) {
        float x = command == FONT_PATH_CLOSE ? probe->start_x : args[0];
        float y = command == FONT_PATH_CLOSE ? probe->start_y : args[1];
        probe->area_twice += probe->current_x * y - x * probe->current_y;
        probe->current_x = x; probe->current_y = y;
    }
    if (command == FONT_PATH_CLOSE) probe->closes++;
    for (int i = 0; i < count; i += 2) {
        if (!probe->has_point) {
            probe->left = probe->right = args[i]; probe->top = probe->bottom = args[i + 1];
            probe->has_point = true;
        } else {
            probe->left = fminf(probe->left, args[i]); probe->right = fmaxf(probe->right, args[i]);
            probe->top = fminf(probe->top, args[i + 1]); probe->bottom = fmaxf(probe->bottom, args[i + 1]);
        }
    }
    return true;
}

TEST(SvgTextTest, GlyphOutlinesStayInLogicalBaselineCoordinates) {
    const float ratios[] = {1.0f, 2.0f};
    for (float ratio : ratios) {
        FontContextConfig config = {};
        config.pixel_ratio = ratio;
        FontContext* context = font_context_create(&config);
        ASSERT_NE(context, nullptr);
        FontStyleDesc style = {};
        style.family = "Ahem"; style.size_px = 20.0f; style.weight = FONT_WEIGHT_NORMAL;
        FontHandle* font = font_load_from_file(context, "test/layout/data/font/Ahem.ttf", &style);
        ASSERT_NE(font, nullptr);
        Arena* arena = arena_create_default();
        const uint32_t codepoints[] = {0x41u, 0xe9u};
        for (uint32_t codepoint : codepoints) {
            SvgFontPathProbe probe = {};
            ASSERT_TRUE(font_visit_glyph_path(font, codepoint, svg_font_path_probe, &probe, arena));
            EXPECT_TRUE(probe.has_point);
            EXPECT_GT(probe.closes, 0);
            EXPECT_NEAR(probe.left, 0.0f, 0.001f);
            EXPECT_NEAR(probe.top, -16.0f, 0.001f);
            EXPECT_NEAR(probe.right, 20.0f, 0.001f);
            EXPECT_NEAR(probe.bottom, 4.0f, 0.001f);
        }
        SvgFontPathProbe absent = {};
        EXPECT_FALSE(font_visit_glyph_path(font, 0x10ffffu, svg_font_path_probe, &absent, arena));
        EXPECT_FALSE(absent.has_point);
        SvgFontPathProbe aborted = {};
        aborted.abort = true;
        EXPECT_FALSE(font_visit_glyph_path(font, 0x41u, svg_font_path_probe, &aborted, arena));
        EXPECT_EQ(aborted.calls, 1);
        arena_destroy(arena);
        font_handle_release(font);
        font_context_destroy(context);
    }
}

TEST(SvgTextTest, BitmapCoverageContoursPreserveHolesAndRemoveInternalEdges) {
    uint8_t gray[] = {255,255,255,0, 255,0,255,0, 255,255,255,0};
    GlyphBitmap bitmap = {};
    bitmap.buffer = gray; bitmap.width = bitmap.height = 3; bitmap.pitch = 4;
    bitmap.bearing_x = 1; bitmap.bearing_y = 3; bitmap.pixel_mode = GLYPH_PIXEL_GRAY;
    Arena* arena = arena_create_default();
    SvgFontPathProbe probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(&bitmap, 0.5f, svg_font_path_probe, &probe, arena));
    EXPECT_EQ(probe.closes, 2);
    EXPECT_FLOAT_EQ(probe.left, 0.5f); EXPECT_FLOAT_EQ(probe.right, 2.0f);
    EXPECT_FLOAT_EQ(probe.top, -1.5f); EXPECT_FLOAT_EQ(probe.bottom, 0.0f);
    EXPECT_FLOAT_EQ(probe.area_twice, 4.0f);
    uint8_t diagonal[] = {0x80, 0x40};
    bitmap.buffer = diagonal; bitmap.width = bitmap.height = 2; bitmap.pitch = 1;
    bitmap.bearing_x = bitmap.bearing_y = 0; bitmap.pixel_mode = GLYPH_PIXEL_MONO;
    probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(&bitmap, 1.0f, svg_font_path_probe, &probe, arena));
    EXPECT_EQ(probe.closes, 2);
    EXPECT_FLOAT_EQ(probe.area_twice, 4.0f);
    arena_destroy(arena);
}

TEST(SvgTextTest, ColorGlyphFallsBackToCoverageGeometry) {
    FontContextConfig config = {}; config.pixel_ratio = 1.0f;
    FontContext* context = font_context_create(&config);
    ASSERT_NE(context, nullptr);
    FontStyleDesc style = {}; style.family = "sans-serif"; style.size_px = 20.0f;
    style.weight = FONT_WEIGHT_NORMAL;
    FontHandle* emoji = font_resolve_for_emoji(context, &style, 0x1f600u);
    if (!emoji) { font_context_destroy(context); GTEST_SKIP() << "no emoji font installed"; }
    Arena* arena = arena_create_default();
    SvgFontPathProbe probe = {};
    bool outlined = font_visit_glyph_path(emoji, 0x1f600u, svg_font_path_probe, &probe, arena);
    EXPECT_FALSE(outlined) << "outline calls=" << probe.calls << " bounds=" << probe.left << ',' << probe.top << ',' << probe.right << ',' << probe.bottom;
    const GlyphBitmap* bitmap = font_render_glyph(emoji, 0x1f600u, GLYPH_RENDER_NORMAL);
    ASSERT_NE(bitmap, nullptr); ASSERT_EQ(bitmap->pixel_mode, GLYPH_PIXEL_BGRA);
#ifdef __APPLE__
    GlyphInfo platform = {};
    ASSERT_TRUE(font_rasterize_ct_metrics(emoji->ct_font_ref, 0x1f600u, emoji->bitmap_scale, &platform));
    // emoji fallback must retain the selected CoreText baseline, including its descender adjustment.
    EXPECT_NEAR((float)bitmap->bearing_y, platform.bearing_y, 2.0f);
#endif
    probe = {};
    ASSERT_TRUE(font_visit_bitmap_contours(bitmap, 1.0f, svg_font_path_probe, &probe, arena));
    EXPECT_TRUE(probe.has_point); EXPECT_GT(probe.right - probe.left, 0.0f);
    RdtPath* path = rdt_path_new(); bool color_bitmap = false;
    EXPECT_TRUE(render_path_append_font_glyph(path, emoji, 0x1f600u, 10.0f, 40.0f, 1.0f, arena, &color_bitmap));
    EXPECT_TRUE(color_bitmap);
    float left, top, right, bottom;
    EXPECT_TRUE(rdt_path_get_bounds(path, &left, &top, &right, &bottom));
    EXPECT_GT(right - left, 0.0f); EXPECT_GT(bottom - top, 0.0f);
    rdt_path_free(path); arena_destroy(arena); font_handle_release(emoji); font_context_destroy(context);
}
