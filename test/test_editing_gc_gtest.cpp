#include <gtest/gtest.h>
#include "../lib/shell.h"
#include <string.h>

struct EditingGcCase {
    const char* document;
    const char* events;
};

class EditingGcTest : public testing::TestWithParam<EditingGcCase> {};

static void run_editing_gc_case(EditingGcCase fixture) {
    const char* args[] = {"./lambda.exe", "view", fixture.document, "--event-file", fixture.events,
                          "--headless", "--no-log", nullptr};
    ShellEnvEntry environment[] = {{"LAMBDA_GC_FORCE_EVERY", "1"},
                                   {"LAMBDA_GC_POISON_FREED", "1"}, {nullptr, nullptr}};
    ShellOptions options = {};
    options.env = environment;
    options.merge_stderr = true;
    options.timeout_ms = 30000;
    ShellResult result = shell_exec(args[0], args, &options);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_FALSE(result.timed_out) << output;
    EXPECT_EQ(result.exit_code, 0) << output;
    EXPECT_NE(strstr(output, "Result: PASS"), nullptr) << output;
    shell_result_free(&result);
}

TEST_P(EditingGcTest, RetainedHistorySurvivesCollection) {
    run_editing_gc_case(GetParam());
}

TEST(EditingGcIntegration, MutationObserverRetainsOldTextDuringCollection) {
    run_editing_gc_case({"test/ui/ce/editable-dom-observer.html",
                         "test/ui/test_editing_contenteditable_observer.json"});
}

INSTANTIATE_TEST_SUITE_P(StructuralUndo, EditingGcTest, testing::Values(
    EditingGcCase{"test/ui/ce/editable-dom-structural-history.html",
                  "test/ui/test_editing_contenteditable_structural_history.json"},
    EditingGcCase{"test/ui/ce/editable-dom-structural-history-direction.html",
                  "test/ui/test_editing_contenteditable_structural_history_direction.json"}));
