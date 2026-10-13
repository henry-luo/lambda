#include <gtest/gtest.h>
#include "../lambda/dom/dom_graphics.h"
#include "../lambda/dom/dom_style.h"
#include "../lambda/dom/dom_host_engine.h"
#include "../lambda/dom/dom_collection_types.h"
#include "../lambda/dom/dom_event_host.h"
#include "../lambda/dom/dom_core.h"
#include <pthread.h>

template<class Result, class... Parameters>
static Result provider_default_operation(Parameters...) { return Result(); }

template<class Result, class... Parameters>
static constexpr auto provider_default_callback(Result (*)(Parameters...))
    -> Result (*)(Parameters...) {
    return provider_default_operation<Result, Parameters...>;
}

template<bool (*Round)()>
static void* provider_register_worker(void* failures) {
    for (unsigned i = 0; i < 1000; ++i) {
        if (!Round()) __atomic_add_fetch((unsigned*)failures, 1, __ATOMIC_RELAXED);
    }
    return nullptr;
}

static void provider_run_workers(void* (*worker)(void*), unsigned* failures) {
    pthread_t workers[8];
    unsigned started = 0;
    for (; started < 8; ++started) {
        int error = pthread_create(&workers[started], nullptr, worker, failures);
        EXPECT_EQ(error, 0);
        if (error) break;
    }
    for (unsigned i = 0; i < started; ++i) EXPECT_EQ(pthread_join(workers[i], nullptr), 0);
    EXPECT_EQ(started, 8u);
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

static bool graphics_registration_round() {
    bool accepted = dom_canvas_register_engine(&graphics_test_canvas) &&
        dom_webgl_register_engine(&graphics_test_webgl) &&
        dom_canvas_engine_ops() == &graphics_test_canvas &&
        dom_webgl_engine_ops() == &graphics_test_webgl &&
        dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::fill_rect,
            &graphics_error_count, 1, 2.0, 3.0f, 4);
    dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::error, 42, 0x0502);
    return accepted;
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
    provider_run_workers(provider_register_worker<graphics_registration_round>, &failures);
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
static bool style_test_focus_is_hidden(DomElement* element) { return !element; }
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
    operations.focus_is_hidden = style_test_focus_is_hidden;
    return operations;
}();
#undef DOM_PROVIDER_DEFAULT

static bool style_registration_round() {
    const DomStyleEngineOps* operations = nullptr;
    bool accepted = dom_style_register_engine(&style_test_operations) &&
        (operations = dom_style_engine_ops()) == &style_test_operations &&
        dom_provider_call(operations, &DomStyleEngineOps::ensure_computed, nullptr, true) &&
        dom_provider_call(operations, &DomStyleEngineOps::focus_is_hidden, nullptr) &&
        dom_provider_call(operations, &DomStyleEngineOps::serialize_computed_value,
            nullptr, nullptr, CSS_PROPERTY_WIDTH, 2) == &style_result_string &&
        dom_provider_call(operations, &DomStyleEngineOps::serialize_custom_property,
            nullptr, nullptr, "--provider", 10) == &style_result_string &&
        dom_provider_call(operations, &DomStyleEngineOps::compute_custom_property,
            nullptr, nullptr, "--provider", 10, true) == &style_result_value;
    dom_provider_call(operations, &DomStyleEngineOps::capture_transition, nullptr, CSS_PROPERTY_WIDTH);
    dom_provider_call(operations, &DomStyleEngineOps::set_used_value_sync, style_test_sync);
    return accepted;
}

TEST(DomStyleProviderTest, AbsenceValidationAndConcurrentPublication) {
    ASSERT_EQ(dom_style_engine_ops(), nullptr);
    EXPECT_FALSE(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::ensure_computed, nullptr, true));
    EXPECT_FALSE(dom_provider_call(dom_style_engine_ops(), &DomStyleEngineOps::focus_is_hidden, nullptr));
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
    unsigned failures = 0;
    provider_run_workers(provider_register_worker<style_registration_round>, &failures);
    EXPECT_EQ(failures, 0u);
    EXPECT_EQ(style_operation_count, 16000u);
    static const DomStyleEngineOps conflict = style_test_operations;
    EXPECT_FALSE(dom_style_register_engine(&conflict));
    EXPECT_EQ(dom_style_engine_ops(), &style_test_operations);
}

static Item host_test_item(int value) {
    Item result = {}; result._type_id = LMD_TYPE_INT; result.int_val = value;
    return result;
}

template<unsigned Operation, class... Arguments>
static Item host_test_operation(Arguments... arguments) {
    Item result = host_test_item(Operation);
    // encode the operation and every argument so routing and forwarding remain observable.
    ((result.int_val = result.int_val * 10 + arguments.int_val), ...);
    return result;
}

static unsigned host_test_document;
static Item host_test_query_command(void* document, const char* kind, const char* command) {
    return document == &host_test_document && kind && command &&
        strcmp(kind, "state") == 0 && strcmp(command, "bold") == 0 ? host_test_item(10) : ItemNull;
}
static bool host_test_design_mode(void* document, const char* value) {
    return document == &host_test_document && value && strcmp(value, "on") == 0;
}

static unsigned host_test_node, host_repaint_calls, host_clear_reflow_calls;
static bool host_test_state_bool(void* document, void* node, const char* name) {
    return document == &host_test_document && node == &host_test_node &&
        name && strcmp(name, "indeterminate") == 0;
}
static void host_test_document_request_repaint(void* document) {
    if (document == &host_test_document) __atomic_add_fetch(&host_repaint_calls, 1u, __ATOMIC_RELAXED);
}
static void host_test_document_clear_reflow(void* document) {
    if (document == &host_test_document) __atomic_add_fetch(&host_clear_reflow_calls, 1u, __ATOMIC_RELAXED);
}

static unsigned host_layout_calls[2], host_prepare_mutation_calls;
static bool host_test_layout_document(void* ui, void* document, bool is_reflow) {
    if (ui != &host_test_node || document != &host_test_document) return false;
    __atomic_add_fetch(&host_layout_calls[is_reflow ? 1 : 0], 1u, __ATOMIC_RELAXED);
    return true;
}
static float host_test_element_effective_zoom(void* element) {
    return element == &host_test_node ? 1.25f : 1.0f;
}
static bool host_test_element_has_vertical_rl_flow(void* element) { return element == &host_test_node; }
static CssEnum host_test_element_specified_keyword(void* element, CssPropertyCode property, CssEnum fallback) {
    return element == &host_test_node && property == CSS_PROPERTY_FLEX_DIRECTION ? CSS_VALUE_COLUMN_REVERSE : fallback;
}
static void host_test_prepare_children_mutation(void* element) {
    if (element == &host_test_node) __atomic_add_fetch(&host_prepare_mutation_calls, 1u, __ATOMIC_RELAXED);
}

static unsigned host_control_calls[4];
static bool host_test_control_disabled(void* state, void* node) {
    return state == &host_test_document && node == &host_test_node;
}
static void host_test_control_record(void* state, void* node, unsigned operation) {
    if (state == &host_test_document && node == &host_test_node)
        __atomic_add_fetch(&host_control_calls[operation], 1u, __ATOMIC_RELAXED);
}
static void host_test_control_focus(void* state, void* node, bool keyboard) {
    if (keyboard) host_test_control_record(state, node, 0);
}
template<unsigned operation> static void host_test_control_action(void* state, void* node) {
    host_test_control_record(state, node, operation);
}
static bool host_test_control_text(void* state, void* node, const char* value, uint32_t bytes, uint32_t units) {
    return state == &host_test_document && node == &host_test_node && value &&
        bytes == 4 && units == 2 && memcmp(value, "\xf0\x9f\x98\x80", 4) == 0;
}
static bool host_test_scroll_begin(void* state, void* node, void* pane, float x, float y) {
    return state == &host_test_document && node == &host_test_node && pane == &host_test_node &&
        x == -12.5f && y == 30.25f;
}
static bool host_test_document_tick(void* document, double now) {
    return document == &host_test_document && now == 1.25;
}
static bool host_test_document_animation(void* document, double now, void* dirty) {
    return host_test_document_tick(document, now) && dirty == &host_test_node;
}

static const DomHostEngineOps host_test_operations = {
    host_test_operation<1, Item, Item>, host_test_operation<2, Item, Item>,
    host_test_operation<3, Item>, host_test_operation<4, Item, Item, Item>,
    host_test_operation<5, Item, Item>, host_test_operation<6, Item>,
    host_test_operation<7, Item, Item, Item>,
    host_test_operation<8, Item>, host_test_operation<9, Item, Item>,
    host_test_query_command, host_test_design_mode,
    host_test_state_bool, host_test_document_request_repaint, host_test_document_clear_reflow,
    host_test_layout_document, host_test_element_effective_zoom, host_test_element_has_vertical_rl_flow,
    host_test_element_specified_keyword, host_test_prepare_children_mutation,
    host_test_control_disabled, host_test_control_focus, host_test_control_text,
    host_test_control_action<1>, host_test_control_action<2>, host_test_scroll_begin,
    host_test_control_action<3>, host_test_document_tick, host_test_document_animation
};

static bool host_test_dispatch(bool present) {
    dom_engine_document_request_repaint(&host_test_document);
    dom_engine_document_clear_reflow(&host_test_document);
    dom_engine_prepare_children_mutation(&host_test_node);
    dom_engine_control_focus_programmatic(&host_test_document, &host_test_node, true);
    dom_engine_control_invalidate_behavior(&host_test_document, &host_test_node);
    dom_engine_control_open_dropdown(&host_test_document, &host_test_node);
    dom_engine_scroll_cancel_smooth(&host_test_document, &host_test_node);
    Item owner = host_test_item(1), second = host_test_item(2), third = host_test_item(3);
    return dom_engine_request_frame(owner, second).item == (present ? host_test_item(112) : ItemNull).item &&
        dom_engine_cancel_frame(owner, second).item == (present ? host_test_item(212) : ItemNull).item &&
        dom_engine_viewport_size(owner).item == (present ? host_test_item(31) : ItemNull).item &&
        dom_engine_watch_window(owner, second, third).item == (present ? host_test_item(4123) : ItemNull).item &&
        dom_engine_history_traverse(owner, second).item == (present ? host_test_item(512) : ItemNull).item &&
        dom_engine_media_info(owner).item == (present ? host_test_item(61) : ItemNull).item &&
        dom_engine_media_command(owner, second, third).item == (present ? host_test_item(7123) : ItemNull).item &&
        dom_engine_clear_editing_focus(owner).item == (present ? host_test_item(81) : ItemNull).item &&
        dom_engine_edit_target(owner, second).item == (present ? host_test_item(912) : ItemNull).item &&
        dom_engine_query_command(&host_test_document, "state", "bold").item == (present ? host_test_item(10) : ItemNull).item &&
        dom_engine_set_design_mode(&host_test_document, "on") == present &&
        dom_engine_state_bool(&host_test_document, &host_test_node, "indeterminate") == present &&
        !dom_engine_state_bool(nullptr, &host_test_node, "indeterminate") &&
        !dom_engine_state_bool(&host_test_document, nullptr, "indeterminate") &&
        !dom_engine_state_bool(&host_test_document, &host_test_node, "checked") &&
        dom_engine_layout_document(&host_test_node, &host_test_document, false) == present &&
        dom_engine_layout_document(&host_test_node, &host_test_document, true) == present &&
        !dom_engine_layout_document(nullptr, &host_test_document, true) &&
        dom_engine_element_effective_zoom(&host_test_node) == (present ? 1.25f : 1.0f) &&
        dom_engine_element_has_vertical_rl_flow(&host_test_node) == present &&
        dom_engine_element_specified_keyword(&host_test_node, CSS_PROPERTY_FLEX_DIRECTION, CSS_VALUE_ROW) ==
            (present ? CSS_VALUE_COLUMN_REVERSE : CSS_VALUE_ROW) &&
        dom_engine_element_specified_keyword(nullptr, CSS_PROPERTY_FLEX_DIRECTION, CSS_VALUE_COLUMN) == CSS_VALUE_COLUMN &&
        dom_engine_control_is_disabled(&host_test_document, &host_test_node) == present &&
        !dom_engine_control_is_disabled(nullptr, &host_test_node) &&
        dom_engine_control_store_text(&host_test_document, &host_test_node, "\xf0\x9f\x98\x80", 4, 2) == present &&
        !dom_engine_control_store_text(&host_test_document, &host_test_node, "\xf0\x9f\x98\x80", 4, 4) &&
        dom_engine_scroll_begin_smooth(&host_test_document, &host_test_node, &host_test_node, -12.5f, 30.25f) == present &&
        dom_engine_document_tick_smooth_scroll(&host_test_document, 1.25) == present &&
        dom_engine_document_tick_animation(&host_test_document, 1.25, &host_test_node) == present &&
        !dom_engine_document_tick_animation(&host_test_document, 1.25, nullptr);
}

static bool host_registration_round() {
    return dom_host_register_engine(&host_test_operations) &&
        dom_host_engine_ops() == &host_test_operations && host_test_dispatch(true);
}

TEST(DomHostProviderTest, AbsenceValidationDispatchAndConcurrentPublication) {
    ASSERT_EQ(dom_host_engine_ops(), nullptr);
    EXPECT_TRUE(host_test_dispatch(false));
    EXPECT_EQ(host_repaint_calls, 0u); EXPECT_EQ(host_clear_reflow_calls, 0u);
    EXPECT_EQ(host_layout_calls[0], 0u); EXPECT_EQ(host_layout_calls[1], 0u);
    EXPECT_EQ(host_prepare_mutation_calls, 0u);
    for (unsigned calls : host_control_calls) EXPECT_EQ(calls, 0u);
    EXPECT_FALSE(dom_host_register_engine(nullptr));
#define DOM_HOST_REJECT_MISSING(result, name, parameters, arguments, absence) { \
    DomHostEngineOps incomplete = host_test_operations; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_host_register_engine(&incomplete)) << #name; }
    DOM_HOST_ENGINE_OPERATIONS(DOM_HOST_REJECT_MISSING)
#undef DOM_HOST_REJECT_MISSING
    ASSERT_EQ(dom_host_engine_ops(), nullptr);
    unsigned failures = 0;
    provider_run_workers(provider_register_worker<host_registration_round>, &failures);
    EXPECT_EQ(failures, 0u);
    static const DomHostEngineOps conflict = host_test_operations;
    EXPECT_FALSE(dom_host_register_engine(&conflict));
    EXPECT_EQ(dom_host_engine_ops(), &host_test_operations);
    EXPECT_TRUE(host_test_dispatch(true));
    EXPECT_EQ(host_repaint_calls, 8001u); EXPECT_EQ(host_clear_reflow_calls, 8001u);
    EXPECT_EQ(host_layout_calls[0], 8001u); EXPECT_EQ(host_layout_calls[1], 8001u);
    EXPECT_EQ(host_prepare_mutation_calls, 8001u);
    for (unsigned calls : host_control_calls) EXPECT_EQ(calls, 8001u);
}

static unsigned collection_type_identities[] = {
#define DOM_COLLECTION_IDENTITY(name) 0,
    DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_IDENTITY)
#undef DOM_COLLECTION_IDENTITY
};
static const DomCollectionHostTypes collection_test_types = []() {
    DomCollectionHostTypes types = {};
    size_t index = 0;
#define DOM_COLLECTION_TEST_TYPE(name) types.name = &collection_type_identities[index++];
    DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_TEST_TYPE)
#undef DOM_COLLECTION_TEST_TYPE
    return types;
}();

static bool collection_test_identities(bool present) {
    bool match = true;
#define DOM_COLLECTION_TEST_IDENTITY(name) \
    match = match && dom_##name##_host_type() == (present ? collection_test_types.name : nullptr);
    DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_TEST_IDENTITY)
#undef DOM_COLLECTION_TEST_IDENTITY
    return match;
}

static bool collection_registration_round() {
    return dom_collection_register_types(&collection_test_types) &&
        dom_collection_host_types() == &collection_test_types && collection_test_identities(true);
}

TEST(DomCollectionProviderTest, AbsenceValidationIdentityAndConcurrentPublication) {
    ASSERT_EQ(dom_collection_host_types(), nullptr);
    EXPECT_TRUE(collection_test_identities(false));
    EXPECT_FALSE(dom_collection_register_types(nullptr));
#define DOM_COLLECTION_REJECT_MISSING(name) { \
    DomCollectionHostTypes incomplete = collection_test_types; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_collection_register_types(&incomplete)) << #name; }
    DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_REJECT_MISSING)
#undef DOM_COLLECTION_REJECT_MISSING
    ASSERT_EQ(dom_collection_host_types(), nullptr);
    unsigned failures = 0;
    provider_run_workers(provider_register_worker<collection_registration_round>, &failures);
    EXPECT_EQ(failures, 0u);
    static const DomCollectionHostTypes conflict = collection_test_types;
    EXPECT_FALSE(dom_collection_register_types(&conflict));
    EXPECT_EQ(dom_collection_host_types(), &collection_test_types);
    EXPECT_TRUE(collection_test_identities(true));
}

// signatures and argument order are observed independently by the fake native provider.
enum EventTestOperation {
#define DOM_EVENT_TEST_ID(result, name, parameters, arguments, absence) EVENT_TEST_##name,
    DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_TEST_ID)
#undef DOM_EVENT_TEST_ID
    EVENT_TEST_COUNT
};
static __thread uint64_t event_test_seen[EVENT_TEST_COUNT];
static uint64_t event_test_argument(Item value) { return value.item; }
static uint64_t event_test_argument(int value) { return (uint64_t)value; }
static uint64_t event_test_argument(bool value) { return value; }
template<class Value> static uint64_t event_test_argument(Value* value) { return (uintptr_t)value; }
static uint64_t event_test_argument(DomEventTargetKey key) {
    return ((uint64_t)key.kind << 56) ^ (uintptr_t)key.ptr;
}
template<class... Arguments> static uint64_t event_test_hash(Arguments... arguments) {
    uint64_t hash = 14695981039346656037ull;
    ((hash = (hash ^ event_test_argument(arguments)) * 1099511628211ull), ...);
    return hash;
}
template<class Result> static Result event_test_result(unsigned operation) {
    return (Result)(operation + 1);
}
template<> Item event_test_result<Item>(unsigned operation) { return host_test_item(operation + 1); }
template<> void event_test_result<void>(unsigned operation) { (void)operation; }
#define DOM_EVENT_TEST_UNPACK(...) __VA_ARGS__
#define DOM_EVENT_TEST_CALLBACK(result, name, parameters, arguments, absence) \
    static result event_test_##name parameters { \
        event_test_seen[EVENT_TEST_##name] = event_test_hash(DOM_EVENT_TEST_UNPACK arguments); \
        return event_test_result<result>(EVENT_TEST_##name); \
    }
DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_TEST_CALLBACK)
#undef DOM_EVENT_TEST_CALLBACK
#undef DOM_EVENT_TEST_UNPACK
static const DomEventHostOps event_test_operations = {
#define DOM_EVENT_TEST_ENTRY(result, name, parameters, arguments, absence) event_test_##name,
    DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_TEST_ENTRY)
#undef DOM_EVENT_TEST_ENTRY
};
template<class Result> static bool event_test_result_matches(Result actual, unsigned operation, bool present) {
    return actual == (present ? event_test_result<Result>(operation) : Result());
}
static bool event_test_result_matches(Item actual, unsigned operation, bool present) {
    return actual.item == (present ? event_test_result<Item>(operation) : ItemNull).item;
}
template<class Result, class... Parameters, class... Arguments>
static bool event_test_call(bool present, unsigned operation,
                           Result (*call)(Parameters...), Arguments... arguments) {
    event_test_seen[operation] = 0;
    Result actual = call(arguments...);
    return event_test_result_matches(actual, operation, present) &&
        event_test_seen[operation] == (present ? event_test_hash(arguments...) : 0);
}
template<class... Parameters, class... Arguments>
static bool event_test_call(bool present, unsigned operation,
                           void (*call)(Parameters...), Arguments... arguments) {
    event_test_seen[operation] = 0;
    call(arguments...);
    return event_test_seen[operation] == (present ? event_test_hash(arguments...) : 0);
}
static bool event_test_dispatch(bool present) {
    Item owner = host_test_item(1), second = host_test_item(2), third = host_test_item(3);
    Item output = third, values[] = {owner, second, third};
    const DomEventTargetKey keys[] = {{DOM_EVENT_KEY_NODE, &owner}, {DOM_EVENT_KEY_CONTAINER, &second}};
    bool valid = true;
#define EVENT_TEST_CALL(name, ...) valid &= event_test_call(present, EVENT_TEST_##name, dom_event_host_##name, __VA_ARGS__);
    for (unsigned flags = 0; flags < 8; flags++) {
        EVENT_TEST_CALL(create, "event", (flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0, 3)
    }
    EVENT_TEST_CALL(is, owner)
    EVENT_TEST_CALL(default_prevented, owner)
    EVENT_TEST_CALL(prevent_default, owner)
    EVENT_TEST_CALL(set_trusted, owner, true)
    EVENT_TEST_CALL(set_time_values, owner, second, third)
    EVENT_TEST_CALL(set_payload, owner, second)
    EVENT_TEST_CALL(payload, owner, 3)
    EVENT_TEST_CALL(set_prototype_override, owner, second)
    EVENT_TEST_CALL(named_get, owner, second, &output)
    EVENT_TEST_CALL(named_set, owner, second, third, &output)
    EVENT_TEST_CALL(set_target, owner, second)
    EVENT_TEST_CALL(set_position_key, owner, keys[0], 3)
    EVENT_TEST_CALL(set_path, owner, keys, 2)
    EVENT_TEST_CALL(clear_path, owner)
    EVENT_TEST_CALL(set_lambda_dispatch_position, owner, second, 3)
    EVENT_TEST_CALL(clear_lambda_dispatch_position, owner)
    EVENT_TEST_CALL(call, owner, "listener", values, 3, &output)
    EVENT_TEST_CALL(dispatch_synthetic, owner, second)
    EVENT_TEST_CALL(synthetic_dispatch_is_reentry, owner)
    EVENT_TEST_CALL(author_template_dispatch_begin, owner)
    EVENT_TEST_CALL(author_template_dispatch_end, owner)
#undef EVENT_TEST_CALL
    return valid && output.item == third.item;
}
static bool event_registration_round() {
    return dom_event_register_host(&event_test_operations) &&
        dom_event_host_ops() == &event_test_operations && event_test_dispatch(true);
}
TEST(DomEventProviderTest, AbsenceValidationArgumentsAndConcurrentPublication) {
    ASSERT_EQ(dom_event_host_ops(), nullptr);
    EXPECT_TRUE(event_test_dispatch(false));
    EXPECT_FALSE(dom_event_register_host(nullptr));
#define DOM_EVENT_REJECT_MISSING(result, name, parameters, arguments, absence) { \
    DomEventHostOps incomplete = event_test_operations; incomplete.name = nullptr; \
    EXPECT_FALSE(dom_event_register_host(&incomplete)) << #name; }
    DOM_EVENT_HOST_OPERATIONS(DOM_EVENT_REJECT_MISSING)
#undef DOM_EVENT_REJECT_MISSING
    ASSERT_EQ(dom_event_host_ops(), nullptr);
    unsigned failures = 0;
    provider_run_workers(provider_register_worker<event_registration_round>, &failures);
    EXPECT_EQ(failures, 0u);
    static const DomEventHostOps conflict = event_test_operations;
    EXPECT_FALSE(dom_event_register_host(&conflict));
    EXPECT_EQ(dom_event_host_ops(), &event_test_operations);
    EXPECT_TRUE(event_test_dispatch(true));
}
