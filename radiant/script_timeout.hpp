#ifndef RADIANT_SCRIPT_TIMEOUT_HPP
#define RADIANT_SCRIPT_TIMEOUT_HPP

#include <stddef.h>
#include <stdlib.h>

#define RADIANT_SCRIPT_EXEC_TIMEOUT_BASE_SECONDS 5
#define RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS 120
#define RADIANT_SCRIPT_EXEC_TIMEOUT_ENV_MAX_SECONDS 600

static inline int radiant_script_exec_timeout_override_seconds() {
    const char* env = getenv("LAMBDA_JS_EXEC_TIMEOUT_SECONDS");
    if (!env || !env[0]) return 0;
    char* end = nullptr;
    long parsed = strtol(env, &end, 10);
    if (end == env || parsed <= 0) return 0;
    return parsed > RADIANT_SCRIPT_EXEC_TIMEOUT_ENV_MAX_SECONDS
        ? RADIANT_SCRIPT_EXEC_TIMEOUT_ENV_MAX_SECONDS : (int)parsed;
}

static inline int radiant_script_exec_timeout_seconds(size_t source_len) {
    int override_seconds = radiant_script_exec_timeout_override_seconds();
    if (override_seconds > 0) return override_seconds;
    int seconds = RADIANT_SCRIPT_EXEC_TIMEOUT_BASE_SECONDS;
    if (source_len > 8192) {
        seconds += (int)((source_len + 16383) / 16384) *
            RADIANT_SCRIPT_EXEC_TIMEOUT_BASE_SECONDS;
    }
    return seconds > RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS
        ? RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS : seconds;
}

static inline int radiant_script_exec_timeout_ceiling_seconds() {
    int override_seconds = radiant_script_exec_timeout_override_seconds();
    return override_seconds > 0 ? override_seconds :
        RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS;
}

#endif
