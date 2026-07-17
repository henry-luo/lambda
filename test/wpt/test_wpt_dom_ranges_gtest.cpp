#define WPT_RUNNER_DIR "ref/wpt/dom/ranges"
#define WPT_RUNNER_TEMP_PREFIX "wpt_dom_ranges_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_dom_ranges_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_DOM_RANGES_UPDATE_BASELINE"
// The pinned runner covers the 42 standard Range files; OpaqueRange is a
// separate tentative API and Range-test-iframe.html is only a helper document.
#define WPT_RUNNER_INCLUDE(name) \
    ((name).find("tentative/") != 0 && (name) != "Range-test-iframe.html")
#include "test_wpt_dom_events_gtest.cpp"
