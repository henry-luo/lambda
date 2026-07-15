#define WPT_RUNNER_DIR "ref/wpt/css/css-transitions"
#define WPT_RUNNER_TEMP_PREFIX "wpt_css_transitions_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_css_transitions_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_CSS_TRANSITIONS_UPDATE_BASELINE"
#define WPT_RUNNER_INCLUDE(name) \
    ((name) == "events-001.html" || (name) == "events-002.html" || \
     (name) == "events-003.html" || (name) == "events-004.html" || \
     (name) == "events-005.html" || (name) == "events-006.html" || \
     (name) == "events-007.html" || \
     (name) == "transition-behavior-events.html" || \
     (name) == "transition-events-with-document-change.html" || \
     (name) == "transitionevent-interface.html")
#include "test_wpt_dom_events_gtest.cpp"
