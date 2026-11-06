#pragma once

// Host checkpoints exclude native layout CPU; script-forced reflows remain charged.
extern "C" bool script_runner_suspend_js_watchdog(void);
extern "C" void script_runner_resume_js_watchdog(void);

struct ScriptRunnerHostLayoutScope {
    bool previous;
    ScriptRunnerHostLayoutScope();
    ~ScriptRunnerHostLayoutScope();
    static bool suspend_watchdog_for_layout();
    ScriptRunnerHostLayoutScope(const ScriptRunnerHostLayoutScope&) = delete;
    ScriptRunnerHostLayoutScope& operator=(const ScriptRunnerHostLayoutScope&) = delete;
};
