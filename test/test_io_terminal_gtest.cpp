#include <gtest/gtest.h>

#include "../lambda/lambda.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/runtime/template_registry.h"
#include "../lambda/runtime/template_state.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/interp.hpp"
#include "../lambda/runtime/radiant_event_hook.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/runtime/terminal_host.h"
#include "../lambda/runtime/template_host.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/jube/jube_interface.h"
#include "../lib/memtrack.h"
#include <string.h>

typedef struct TerminalEmitCapture {
    Item session;
    Rooted<Item>* line_event;
} TerminalEmitCapture;

static Item capture_terminal_emit(void* receiver, Item event_name,
                                  Item event_data) {
    TerminalEmitCapture* capture = (TerminalEmitCapture*)receiver;
    const char* name = event_name.get_chars();
    if (name && strcmp(name, "readline_frame_event") == 0) {
        TemplateEntry* terminal_template = template_registry_match(
            g_template_registry, capture->session, false, nullptr);
        const char* active_name = template_entry_state_name(
            terminal_template, "active_frame");
        Item active = active_name ? tmpl_state_get(capture->session,
            terminal_template->template_ref, active_name) : ItemNull;
        if (get_type_id(active) == LMD_TYPE_NULL) {
            active = item_attr(capture->session, "frame");
        }
        Item requested = item_attr(event_data, "name");
        bool handled = false;
        template_dispatch_event(active, true, requested.get_chars(),
            item_attr(event_data, "event"), &handled);
        return {.item = b2it(handled)};
    }
    if (name && strcmp(name, "readline_line") == 0) {
        capture->line_event->set(event_data);
        return ItemNull;
    }
    if (name && strncmp(name, "readline_", 9) == 0) {
        bool handled = false;
        template_dispatch_event(capture->session, false, name,
            event_data, &handled);
        return {.item = b2it(handled)};
    }
    return ItemNull;
}

struct TerminalTestRuntime {
    Runtime runtime;
    LambdaTier saved_tier;

    explicit TerminalTestRuntime(LambdaTier tier)
        : runtime{}, saved_tier(lambda_tier_selected()) {
        lambda_tier_set(tier);
        runtime_init(&runtime);
    }

    ~TerminalTestRuntime() {
        runtime_cleanup(&runtime);
        lambda_tier_set(saved_tier);
    }
};

TEST(IoTerminal, RetainedTemplateHostDispatchesWithoutRadiant) {
    const char* source =
        "view <counter> state word: \"\" { <frame value: word> }\n"
        "on set(value) { word = value }\n"
        "<counter>\n";
    TemplateHostSession* mounted = template_host_session_open(source,
        "<generic-template-host-test>");
    ASSERT_NE(mounted, nullptr);
    {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        Item model{.item = template_host_session_root_word(mounted)};
        bool handled = false;
        Item before = fn_apply1(model);
        ASSERT_EQ(get_type_id(before), LMD_TYPE_ELEMENT);
        EXPECT_STREQ(item_attr(before, "value").get_chars(), "");
        template_dispatch_event(model, false, "set",
            {.item = s2it(heap_strcpy("hello", 5))},
            &handled);
        EXPECT_TRUE(handled);
        Item after = fn_apply1(model);
        EXPECT_STREQ(item_attr(after, "value").get_chars(), "hello");
    }
    template_host_session_close(mounted);
}

TEST(IoTerminal, RetainedTemplateCloseDefersUntilBindingUnwinds) {
    TemplateHostSession* mounted = template_host_session_open(
        "view <counter> state word: \"ok\" { <frame value: word> }\n"
        "<counter>\n", "<deferred-template-close-test>");
    ASSERT_NE(mounted, nullptr);
    {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        Item model{.item = template_host_session_root_word(mounted)};
        EXPECT_STREQ(item_attr(fn_apply1(model), "value").get_chars(), "ok");
        template_host_session_close(mounted);
    }
}

TEST(IoTerminal, RetainedTemplateRestoresHostJsOwner) {
    TerminalTestRuntime owned(LAMBDA_TIER_INTERP);
    Input* seed = run_script_mir(&owned.runtime, "1\n",
        (char*)"<terminal-js-owner-test>", false);
    ASSERT_NE(seed, nullptr);
    EvalContext* owner = runtime_get_eval_context(&owned.runtime);
    ASSERT_NE(owner, nullptr);
    ASSERT_TRUE(js_runtime_state_init(owner));

    TemplateHostSession* mounted = template_host_session_open(
        "view <counter> { <frame value: 1> }\n<counter>\n",
        "<terminal-js-owner-mount>");
    ASSERT_NE(mounted, nullptr);
    EXPECT_TRUE(js_runtime_state_thread_matches(owner));
    {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        // the host capsule must not remain active while the template owns TLS.
        EXPECT_FALSE(js_runtime_state_thread_matches(owner));
        Item model{.item = template_host_session_root_word(mounted)};
        EXPECT_EQ(get_type_id(fn_apply1(model)), LMD_TYPE_ELEMENT);
    }
    EXPECT_TRUE(js_runtime_state_thread_matches(owner));
    template_host_session_close(mounted);
    EXPECT_TRUE(js_runtime_state_thread_matches(owner));
    EXPECT_TRUE(js_runtime_state_shutdown(owner));
}

TEST(IoTerminal, JubeTemplateServiceDispatchesPlainValues) {
    const JubeHostAPI* host = jube_internal_host_api();
    ASSERT_NE(host, nullptr);
    ASSERT_NE(host->templates, nullptr);
    const JubeHostTemplateAPI* api = host->templates;
    void* session = api->open(
        "view <counter> state word: \"\" { <frame value: word> }\n"
        "on set(value) { word = value }\n"
        "<counter>\n", "<jube-template-service-test>");
    ASSERT_NE(session, nullptr);
    JubeTemplateValue value = {};
    value.kind = JUBE_TEMPLATE_STRING;
    value.bytes = "hello";
    value.byte_length = 5;
    EXPECT_EQ(api->dispatch(session, nullptr, "set", &value, nullptr, nullptr), 0);
    char* json = nullptr;
    size_t length = 0;
    ASSERT_EQ(api->render_json(session, &json, &length), 0);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"value\": \"hello\""), nullptr) << json;
    api->bytes_release(json);
    api->close(session);
}

TEST(IoTerminal, JubeTemplateServiceMountsTerminalPackage) {
    const JubeHostTemplateAPI* api = jube_internal_host_api()->templates;
    void* session = api->open(
        "import readline: lambda.io.terminal\n"
        "readline.terminal(\"node\", 0, \"\")\n",
        "<jube-readline-service-test>");
    ASSERT_NE(session, nullptr);
    JubeTemplateValue tty = {};
    tty.kind = JUBE_TEMPLATE_BOOL;
    tty.boolean = false;
    EXPECT_EQ(api->dispatch(session, nullptr, "readline_device", &tty,
        nullptr, nullptr), 0);
    JubeTemplateValue frame_id = {}, prompt = {}, next = {};
    frame_id.kind = JUBE_TEMPLATE_INT;
    frame_id.integer = 1;
    prompt.kind = JUBE_TEMPLATE_STRING;
    prompt.bytes = "x> ";
    prompt.byte_length = 3;
    JubeTemplateField fields[2] = {{"frame_id", &frame_id}, {"prompt", &prompt}};
    next.kind = JUBE_TEMPLATE_MAP;
    next.fields = fields;
    next.field_count = 2;
    EXPECT_EQ(api->dispatch(session, nullptr, "readline_next", &next,
        nullptr, nullptr), 0);
    api->close(session);
}

static void check_headless_frame_dispatch(LambdaTier tier) {
    TerminalTestRuntime owned(tier);

    const char* source =
        "import readline: lambda.io.terminal\n"
        "let session = readline.terminal(\"shell\", 7, \"λ> \");\n"
        "{session: session, bytes_event: b'\\xC3A9', "
        "left_event: {key: \"ArrowLeft\"}, "
        "enter_event: b'\\x0D', "
        "next_event: {frame_id: 8, prompt: \".. \"}, "
        "up_event: {key: \"ArrowUp\"}, "
        "home_event: {key: \"Control-A\"}, "
        "kill_event: {key: \"Control-K\"}, "
        "yank_event: {key: \"Control-Y\"}}\n";
    Input* output = run_script_mir(&owned.runtime, source,
        (char*)"<readline-headless-host-test>", false);
    ASSERT_NE(output, nullptr);
    RootFrame source_roots(1);
    Rooted<Item> source_root(source_roots, output->root);
    ASSERT_EQ(get_type_id(source_root.get()), LMD_TYPE_MAP);
    Item source_session = item_attr(source_root.get(), "session");
    Item source_frame = item_attr(source_session, "frame");

    Item initial = fn_apply1(source_session);
    ASSERT_EQ(get_type_id(initial), LMD_TYPE_ELEMENT);
    Item initial_frame = item_at(initial, 0);
    EXPECT_STREQ(item_attr(initial_frame, "text").get_chars(), "");

    bool handled = false;
    RootFrame emit_roots(1);
    Rooted<Item> line_event(emit_roots, ItemNull);
    TerminalEmitCapture capture = {source_session, &line_event};
    LambdaEmitScope emit_scope = {};
    lambda_emit_scope_enter(&emit_scope, capture_terminal_emit, &capture);
    template_dispatch_event(source_session, false, "readline_bytes",
        item_attr(source_root.get(), "bytes_event"), &handled);
    lambda_emit_scope_leave(&emit_scope);
    EXPECT_TRUE(handled);
    Item after_text = fn_apply1(source_session);
    Item after_text_frame = item_at(after_text, 0);
    EXPECT_STREQ(item_attr(after_text_frame, "text").get_chars(), "é");
    EXPECT_EQ(it2i(item_attr(item_attr(after_text_frame, "caret"), "offset")), 1);

    template_dispatch_event(source_frame, true, "keydown",
        item_attr(source_root.get(), "left_event"), &handled);
    EXPECT_TRUE(handled);
    Item after_move = fn_apply1(source_session);
    Item after_move_frame = item_at(after_move, 0);
    EXPECT_EQ(it2i(item_attr(item_attr(after_move_frame, "caret"), "offset")), 0);

    lambda_emit_scope_enter(&emit_scope, capture_terminal_emit, &capture);
    template_dispatch_event(source_session, false, "readline_bytes",
        item_attr(source_root.get(), "enter_event"), &handled);
    lambda_emit_scope_leave(&emit_scope);
    EXPECT_TRUE(handled);
    ASSERT_EQ(get_type_id(line_event.get()), LMD_TYPE_MAP);
    EXPECT_STREQ(item_attr(line_event.get(), "text").get_chars(), "é");
    EXPECT_EQ(it2i(item_attr(line_event.get(), "frame_id")), 7);

    TemplateEntry* terminal_template = template_registry_match(
        g_template_registry, source_session, false, nullptr);
    ASSERT_NE(terminal_template, nullptr);
    const char* history_name = template_entry_state_name(terminal_template, "history");
    const char* active_name = template_entry_state_name(terminal_template, "active_frame");
    ASSERT_NE(history_name, nullptr);
    ASSERT_NE(active_name, nullptr);
    Item history = tmpl_state_get(source_session,
        terminal_template->template_ref, history_name);
    ASSERT_EQ(get_type_id(history), LMD_TYPE_ARRAY);
    EXPECT_STREQ(item_at(history, 0).get_chars(), "é");

    template_dispatch_event(source_session, false, "readline_next",
        item_attr(source_root.get(), "next_event"), &handled);
    EXPECT_TRUE(handled);
    Item next_render = fn_apply1(source_session);
    Item next_frame = item_at(next_render, 0);
    EXPECT_EQ(it2i(item_attr(next_frame, "id")), 8);
    EXPECT_STREQ(item_attr(next_frame, "text").get_chars(), "");
    EXPECT_EQ(it2i(item_attr(item_attr(next_frame, "caret"), "offset")), 0);
    EXPECT_EQ(get_type_id(tmpl_state_get(source_session,
        terminal_template->template_ref, history_name)), LMD_TYPE_ARRAY);

    Item active_source_frame = tmpl_state_get(source_session,
        terminal_template->template_ref, active_name);
    template_dispatch_event(active_source_frame, true, "keydown",
        item_attr(source_root.get(), "up_event"), &handled);
    EXPECT_TRUE(handled);
    Item recalled = fn_apply1(source_session);
    EXPECT_STREQ(item_attr(item_at(recalled, 0), "text").get_chars(), "é");

    template_dispatch_event(active_source_frame, true, "keydown",
        item_attr(source_root.get(), "home_event"), &handled);
    lambda_emit_scope_enter(&emit_scope, capture_terminal_emit, &capture);
    template_dispatch_event(active_source_frame, true, "keydown",
        item_attr(source_root.get(), "kill_event"), &handled);
    lambda_emit_scope_leave(&emit_scope);
    Item after_kill = fn_apply1(source_session);
    EXPECT_STREQ(item_attr(item_at(after_kill, 0), "text").get_chars(), "");
    lambda_emit_scope_enter(&emit_scope, capture_terminal_emit, &capture);
    template_dispatch_event(active_source_frame, true, "keydown",
        item_attr(source_root.get(), "yank_event"), &handled);
    lambda_emit_scope_leave(&emit_scope);
    Item after_yank = fn_apply1(source_session);
    EXPECT_STREQ(item_attr(item_at(after_yank, 0), "text").get_chars(), "é");

}

TEST(IoTerminal, HeadlessTemplateDispatchJit) {
    check_headless_frame_dispatch(LAMBDA_TIER_JIT);
}

TEST(IoTerminal, HeadlessTemplateDispatchInterp) {
    check_headless_frame_dispatch(LAMBDA_TIER_INTERP);
}

struct PartialWriteTerminal {
    const char* input;
    size_t input_at;
    char output[4096];
    size_t output_at;
    int raw_enabled;
    int raw_disabled;
};

static int partial_set_raw(void* opaque, bool enable) {
    PartialWriteTerminal* terminal = (PartialWriteTerminal*)opaque;
    if (enable) terminal->raw_enabled++;
    else terminal->raw_disabled++;
    return 0;
}

static int partial_size(void*, int* rows, int* columns) {
    *rows = 24;
    *columns = 80;
    return 0;
}

static int partial_wait(void* opaque, int) {
    PartialWriteTerminal* terminal = (PartialWriteTerminal*)opaque;
    return terminal->input[terminal->input_at] ? 1 : -1;
}

static int64_t partial_read(void* opaque, char* bytes, size_t capacity) {
    PartialWriteTerminal* terminal = (PartialWriteTerminal*)opaque;
    if (capacity == 0 || !terminal->input[terminal->input_at]) return 0;
    bytes[0] = terminal->input[terminal->input_at++];
    return 1;
}

static int64_t partial_write(void* opaque, const char* bytes, size_t length) {
    PartialWriteTerminal* terminal = (PartialWriteTerminal*)opaque;
    if (length == 0 || terminal->output_at + 1 >= sizeof(terminal->output)) return -1;
    terminal->output[terminal->output_at++] = bytes[0];
    terminal->output[terminal->output_at] = '\0';
    return 1;
}

TEST(IoTerminal, SessionAcceptsPartialTransportWrites) {
    TerminalSession* session = terminal_session_open(true, 80);
    ASSERT_NE(session, nullptr);
    PartialWriteTerminal device = {"hi\r", 0, {}, 0, 0, 0};
    TerminalTransport transport = {};
    transport.device = &device;
    transport.is_tty = true;
    transport.set_raw = partial_set_raw;
    transport.size = partial_size;
    transport.wait = partial_wait;
    transport.read = partial_read;
    transport.write = partial_write;

    TerminalReadResult read = terminal_session_readline(session, &transport, "λ> ");
    ASSERT_EQ(read.status, TERMINAL_READ_LINE);
    EXPECT_STREQ(read.line, "hi");
    EXPECT_EQ(device.raw_enabled, 1);
    EXPECT_EQ(device.raw_disabled, 1);
    EXPECT_NE(strstr(device.output, "λ> hi"), nullptr);
    mem_free(read.line);
    terminal_session_close(session);
}
