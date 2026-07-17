#define WPT_RUNNER_DIR "ref/wpt/html/dom"
#define WPT_RUNNER_TEMP_PREFIX "wpt_html_reflection_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_html_reflection_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_HTML_REFLECTION_UPDATE_BASELINE"
#define WPT_RUNNER_INCLUDE(name) \
    ((name).find("reflection-") == 0 || \
     ((name).find("aria-") == 0 && (name).find("reflection") != std::string::npos))
#include "test_wpt_dom_events_gtest.cpp"
