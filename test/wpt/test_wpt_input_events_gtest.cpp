#define WPT_RUNNER_DIR "ref/wpt/input-events"
#define WPT_RUNNER_TEMP_PREFIX "wpt_input_events_"
#define WPT_RUNNER_BASELINE_PATH "test/wpt/wpt_input_events_baseline.txt"
#define WPT_RUNNER_UPDATE_ENV "WPT_INPUT_EVENTS_UPDATE_BASELINE"
// execCommand is an explicit non-goal of the script-owned editing contract.
#define WPT_RUNNER_INCLUDE(name) ((name).find("exec-command") == std::string::npos)
#include "test_wpt_dom_events_gtest.cpp"
