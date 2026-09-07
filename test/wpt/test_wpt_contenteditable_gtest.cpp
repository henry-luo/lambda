// A no-emulation WPT editing runner. The reused generic runner supplies
// process isolation, script extraction and reporting; this wrapper admits
// only protocol-verifiable cases and never supplies an editing result.
#define WPT_RUNNER_DIR "ref/wpt"
#define WPT_RUNNER_TEMP_PREFIX "wpt_contenteditable_"
#define WPT_RUNNER_SHIM_PATH "test/wpt/wpt_contenteditable_harness.js"
#define WPT_RUNNER_INCLUDE(name) \
    (strcmp((name).c_str(), "contenteditable/plaintext-only.html") == 0 || \
     strcmp((name).c_str(), "contenteditable/select-text-change-crash.html") == 0 || \
     strcmp((name).c_str(), "input-events/input-events-exec-command.html") == 0)
#include "test_wpt_dom_events_gtest.cpp"
