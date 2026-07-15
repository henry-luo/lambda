#define WPT_RUNNER_DIR "ref/wpt/intersection-observer"
#define WPT_RUNNER_TEMP_PREFIX "wpt_intersection_observer_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_intersection_observer_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_INTERSECTION_OBSERVER_UPDATE_BASELINE"
#define WPT_RUNNER_INCLUDE(name) \
    ((name) == "disconnect.html" || (name) == "empty-root-margin.html" || \
     (name) == "explicit-root-different-document.html" || \
     (name) == "initial-observation-with-threshold.html" || \
     (name) == "multiple-targets.html" || (name) == "observer-attributes.html" || \
     (name) == "observer-callback-arguments.html" || (name) == "remove-element.html" || \
     (name) == "root-margin.html" || (name) == "same-document-no-root.html" || \
     (name) == "same-document-root.html" || (name) == "scroll-and-root-margin.html" || \
     (name) == "timestamp.html")
#include "test_wpt_dom_events_gtest.cpp"
