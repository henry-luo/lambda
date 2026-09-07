// The Chromium smoke runner reuses the generic process-isolated page runner.
// Rebuild this wrapper when its compile-time runner configuration changes.
// Its auxiliary harness is protocol-only; contenteditable behavior comes from
// the product package, never from the harness.
#define WPT_RUNNER_DIR "../lambda-test/editing"
#define WPT_RUNNER_TEMP_PREFIX "chromium_contenteditable_"
#define WPT_RUNNER_SHIM_PATH "test/wpt/wpt_contenteditable_harness.js"
#define WPT_RUNNER_AUXILIARY_SHIM_PATH "test/chromium/chromium_contenteditable_harness.js"
#define WPT_RUNNER_INCLUDE(name) \
    (strcmp((name).c_str(), "selection/basic-selection.html") == 0 || \
     strcmp((name).c_str(), "selection/selectNode.html") == 0 || \
     strcmp((name).c_str(), "selection/selectNodeContents.html") == 0 || \
     strcmp((name).c_str(), "execCommand/bold-basic.html") == 0)
#include "../wpt/test_wpt_dom_events_gtest.cpp"
