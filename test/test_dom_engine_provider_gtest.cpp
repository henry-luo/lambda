#include <gtest/gtest.h>
#include "../lambda/dom/dom_graphics.h"
#include "../lambda/dom/dom_style.h"
#include <pthread.h>

template<class Result, class... Parameters>
static Result provider_default_operation(Parameters...) { return Result(); }

template<class Result, class... Parameters>
static constexpr auto provider_default_callback(Result (*)(Parameters...))
    -> Result (*)(Parameters...) {
    return provider_default_operation<Result, Parameters...>;
}

static unsigned graphics_error_count;
static bool graphics_test_canvas_ensure(void* canvas) { return canvas == &graphics_error_count; }
static bool graphics_test_canvas_rect(void* canvas, float x, float y, float width, float height) {
    return graphics_test_canvas_ensure(canvas) && x == 1.0f && y == 2.0f &&
           width == 3.0f && height == 4.0f;
}
static void graphics_test_webgl_error(uint64_t canvas, unsigned error) {
    if (canvas == 42 && error == 0x0502)
        __atomic_add_fetch(&graphics_error_count, 1, __ATOMIC_RELAXED);
}

#define DOM_PROVIDER_DEFAULT(result, name, parameters) provider_default_callback((result (*) parameters)nullptr),

static const DomCanvasEngineOps graphics_test_canvas = []() {
    DomCanvasEngineOps operations = {
DOM_CANVAS_ENGINE_OPERATIONS(DOM_PROVIDER_DEFAULT)
    };
    operations.ensure = graphics_test_canvas_ensure;
    operations.fill_rect = graphics_test_canvas_rect;
    return operations;
}();
static const DomWebGlEngineOps graphics_test_webgl = []() {
    DomWebGlEngineOps operations = {
DOM_WEBGL_ENGINE_OPERATIONS(DOM_PROVIDER_DEFAULT)
    };
    operations.error = graphics_test_webgl_error;
    return operations;
}();

static void* graphics_register_worker(void* failures) {
    for (unsigned i = 0; i < 1000; ++i) {
        if (!dom_canvas_register_engine(&graphics_test_canvas) ||
            !dom_webgl_register_engine(&graphics_test_webgl) ||
            dom_canvas_engine_ops() != &graphics_test_canvas ||
            dom_webgl_engine_ops() != &graphics_test_webgl ||
            !dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::fill_rect,
                               &graphics_error_count, 1, 2.0, 3.0f, 4))
            __atomic_add_fetch((unsigned*)failures, 1, __ATOMIC_RELAXED);
        dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::error, 42, 0x0502);
    }
    return nullptr;
}

// This runner links the lower seam and native DOM only, with no Radiant provider.
// One test owns publication so shuffled execution cannot hide the absence case.
TEST(DomGraphicsProviderTest, AbsenceValidationAndConcurrentPublication) {
    ASSERT_EQ(dom_canvas_engine_ops(), nullptr);
    ASSERT_EQ(dom_webgl_engine_ops(), nullptr);
    DomCanvasStateSnapshot state = {};
    state.line_width = 55.0f;
    WebGlReply reply = {};
    reply.n[0] = 55.0;
    EXPECT_FALSE(dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::ensure, nullptr));
    EXPECT_FALSE(dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::get_state, nullptr, &state));
    EXPECT_EQ(dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::to_data_url, nullptr), nullptr);
    EXPECT_EQ(dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::create, nullptr, nullptr), 0u);
    EXPECT_FALSE(dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::call, 0, nullptr, &reply));
    dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::error, 42, 0x0502);
    EXPECT_FLOAT_EQ(state.line_width, 55.0f);
    EXPECT_DOUBLE_EQ(reply.n[0], 55.0);
    EXPECT_EQ(graphics_error_count, 0u);
    EXPECT_FALSE(dom_canvas_register_engine(nullptr));
    EXPECT_FALSE(dom_webgl_register_engine(nullptr));

#define DOM_CANVAS_REJECT_MISSING(result, name, parameters) { \
    DomCanvasEngineOps incomplete = graphics_test_canvas; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_canvas_register_engine(&incomplete)) << #name; }
    DOM_CANVAS_ENGINE_OPERATIONS(DOM_CANVAS_REJECT_MISSING)
#undef DOM_CANVAS_REJECT_MISSING
#define DOM_WEBGL_REJECT_MISSING(result, name, parameters) { \
    DomWebGlEngineOps incomplete = graphics_test_webgl; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_webgl_register_engine(&incomplete)) << #name; }
    DOM_WEBGL_ENGINE_OPERATIONS(DOM_WEBGL_REJECT_MISSING)
#undef DOM_WEBGL_REJECT_MISSING
    ASSERT_EQ(dom_canvas_engine_ops(), nullptr);
    ASSERT_EQ(dom_webgl_engine_ops(), nullptr);

    unsigned failures = 0;
    pthread_t workers[8];
    unsigned started = 0;
    for (; started < 8; ++started) {
        int error = pthread_create(&workers[started], nullptr, graphics_register_worker, &failures);
        EXPECT_EQ(error, 0);
        if (error) break;
    }
    for (unsigned i = 0; i < started; ++i) EXPECT_EQ(pthread_join(workers[i], nullptr), 0);
    EXPECT_EQ(started, 8u);
    EXPECT_EQ(failures, 0u);
    EXPECT_EQ(graphics_error_count, 8000u);
    static const DomCanvasEngineOps other_canvas = graphics_test_canvas;
    static const DomWebGlEngineOps other_webgl = graphics_test_webgl;
    EXPECT_FALSE(dom_canvas_register_engine(&other_canvas));
    EXPECT_FALSE(dom_webgl_register_engine(&other_webgl));
    EXPECT_EQ(dom_canvas_engine_ops(), &graphics_test_canvas);
    EXPECT_EQ(dom_webgl_engine_ops(), &graphics_test_webgl);
}

static unsigned style_operation_count;
static String style_result_string = {};
static CssValue style_result_value = {};
static bool style_test_sync(DomDocument* document) { return document == nullptr; }
static bool style_test_ensure(DomElement* element, bool used) { return !element && used; }
static String* style_test_serialize(Pool* pool, DomElement* element, CssPropertyCode property, int pseudo) {
    return !pool && !element && property == CSS_PROPERTY_WIDTH && pseudo == 2 ? &style_result_string : nullptr;
}
static String* style_test_custom(Pool* pool, DomElement* element, const char* name, size_t length) {
    return !pool && !element && name && length == 10 && memcmp(name, "--provider", 10) == 0
        ? &style_result_string : nullptr;
}
static const CssValue* style_test_compute(Pool* pool, DomElement* element, const char* name, size_t length, bool preserve) {
    return preserve && style_test_custom(pool, element, name, length) ? &style_result_value : nullptr;
}
static void style_test_capture(DomElement* element, CssPropertyCode property) {
    if (!element && property == CSS_PROPERTY_WIDTH) __atomic_add_fetch(&style_operation_count, 1, __ATOMIC_RELAXED);
}
static void style_test_set_sync(DomStyleUsedValueSync sync) {
    if (sync == style_test_sync && sync(nullptr)) __atomic_add_fetch(&style_operation_count, 1, __ATOMIC_RELAXED);
}
static const DomStyleEngineOps style_test_operations = []() {
    DomStyleEngineOps operations = {DOM_STYLE_ENGINE_OPERATIONS(DOM_PROVIDER_DEFAULT)};
    operations.ensure_computed = style_test_ensure;
    operations.serialize_computed_value = style_test_serialize;
    operations.serialize_custom_property = style_test_custom;
    operations.compute_custom_property = style_test_compute;
    operations.capture_transition = style_test_capture;
    operations.set_used_value_sync = style_test_set_sync;
    return operations;
}();
#undef DOM_PROVIDER_DEFAULT

static void* style_register_worker(void* failures) {
    for (unsigned i = 0; i < 1000; ++i) {
        const DomStyleEngineOps* operations = nullptr;
        if (!dom_style_register_engine(&style_test_operations) ||
            (operations = dom_style_engine_ops()) != &style_test_operations ||
            !dom_provider_call(operations, &DomStyleEngineOps::ensure_computed, nullptr, true) ||
            dom_provider_call(operations, &DomStyleEngineOps::serialize_computed_value,
                nullptr, nullptr, CSS_PROPERTY_WIDTH, 2) != &style_result_string ||
            dom_provider_call(operations, &DomStyleEngineOps::serialize_custom_property,
                nullptr, nullptr, "--provider", 10) != &style_result_string ||
            dom_provider_call(operations, &DomStyleEngineOps::compute_custom_property,
                nullptr, nullptr, "--provider", 10, true) != &style_result_value)
            __atomic_add_fetch((unsigned*)failures, 1, __ATOMIC_RELAXED);
        dom_provider_call(operations, &DomStyleEngineOps::capture_transition, nullptr, CSS_PROPERTY_WIDTH);
        dom_provider_call(operations, &DomStyleEngineOps::set_used_value_sync, style_test_sync);
    }
    return nullptr;
}

TEST(DomStyleProviderTest, AbsenceValidationAndConcurrentPublication) {
    ASSERT_EQ(dom_style_engine_ops(), nullptr);
    EXPECT_FALSE(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::ensure_computed, nullptr, true));
    EXPECT_EQ(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::serialize_computed_value,
        nullptr, nullptr, CSS_PROPERTY_WIDTH, 2), nullptr);
    EXPECT_EQ(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::serialize_custom_property,
        nullptr, nullptr, "--provider", 10), nullptr);
    EXPECT_EQ(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::compute_custom_property,
        nullptr, nullptr, "--provider", 10, true), nullptr);
    dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::capture_transition, nullptr, CSS_PROPERTY_WIDTH);
    dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::set_used_value_sync, style_test_sync);
    EXPECT_EQ(style_operation_count, 0u);
    EXPECT_FALSE(dom_style_register_engine(nullptr));
#define DOM_STYLE_REJECT_MISSING(result, name, parameters) { \
    DomStyleEngineOps incomplete = style_test_operations; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_style_register_engine(&incomplete)) << #name; }
    DOM_STYLE_ENGINE_OPERATIONS(DOM_STYLE_REJECT_MISSING)
#undef DOM_STYLE_REJECT_MISSING
    ASSERT_EQ(dom_style_engine_ops(), nullptr);
    unsigned failures = 0, started = 0;
    pthread_t workers[8];
    for (; started < 8; ++started) {
        int error = pthread_create(&workers[started], nullptr, style_register_worker, &failures);
        EXPECT_EQ(error, 0);
        if (error) break;
    }
    for (unsigned i = 0; i < started; ++i) EXPECT_EQ(pthread_join(workers[i], nullptr), 0);
    EXPECT_EQ(started, 8u);
    EXPECT_EQ(failures, 0u);
    EXPECT_EQ(style_operation_count, 16000u);
    static const DomStyleEngineOps conflict = style_test_operations;
    EXPECT_FALSE(dom_style_register_engine(&conflict));
    EXPECT_EQ(dom_style_engine_ops(), &style_test_operations);
}
