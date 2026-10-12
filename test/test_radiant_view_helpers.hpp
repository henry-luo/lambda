#pragma once

#include "../lib/shell.h"

// sustained replays include many redraws before shutdown, even on a virtual clock.
constexpr int RADIANT_VIEW_REPLAY_TIMEOUT_MS = 180000;

// share isolated workspaces, host selection and launch reporting across View cases.
struct RadiantViewLaunch {
    const char* label;
    const char* page;
    const char* event_path;
    const ShellEnvEntry* env;
    const char* command = "view";
    bool graphics = false;
    bool quiet = false;
    int timeout_ms = 30000;
    char cwd[1024];
    char log_path[1024];
    char viewer_log_path[1024];
    char timing_path[1024];
    char event_result[1024];
    ShellResult result = {-1};
    double elapsed_ms = 0;
};

bool test_radiant_view_prepare_launch(RadiantViewLaunch* launch);
void test_radiant_view_launch(void* argument);
void test_radiant_view_record_launch(const char* label, double elapsed_ms,
                                    const char* log_path, const char* timing_path);
void test_radiant_view_register_scene3d();
