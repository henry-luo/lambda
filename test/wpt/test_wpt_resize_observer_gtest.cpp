#define WPT_RUNNER_DIR "ref/wpt/resize-observer"
#define WPT_RUNNER_TEMP_PREFIX "wpt_resize_observer_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_resize_observer_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_RESIZE_OBSERVER_UPDATE_BASELINE"
#define WPT_RUNNER_INCLUDE(name) \
    ((name) == "calculate-depth-for-node.html" || (name) == "change-layout-in-error.html" || \
     (name) == "eventloop.html" || (name) == "fragments.html" || \
     (name) == "multiple-observers-with-mutation-crash.html" || (name) == "notify.html" || \
     (name) == "observe.html" || (name) == "ordering.html" || \
     (name) == "scrollbars.html" || (name) == "svg-with-css-box-001.html" || \
     (name) == "zoom.html")
#include "test_wpt_dom_events_gtest.cpp"
