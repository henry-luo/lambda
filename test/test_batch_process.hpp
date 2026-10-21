#ifndef TEST_BATCH_PROCESS_HPP
#define TEST_BATCH_PROCESS_HPP

#include "test_process.h"
#include "../lib/shell.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include <errno.h>
#include <limits.h>

struct TestBatchResult {
    int exit_code = -1;
    bool complete = false;
    StrBuf* output = nullptr;
};

inline void test_batch_result_free(TestBatchResult* result) {
    strbuf_free(result->output);
    *result = {};
}

// decode exactly the assigned scripts in order; success markers alone cannot
// hide a truncated process, a lost script, or a teardown crash (D5.3.3).
inline bool test_batch_decode(const ShellResult& process, const char* const* paths,
        size_t count, TestBatchResult* results, StrBuf* error) {
    const char* cursor = process.stdout_buf ? process.stdout_buf : "";
    const char* end = cursor + process.stdout_len;
    size_t next = 0;
    bool active = false;
    const char* failure = nullptr;
    while (cursor < end && !failure) {
        const char* marker = nullptr;
        TestBatchControl kind = test_process_find_batch_control(cursor, (size_t)(end - cursor), &marker);
        const char* body_end = marker ? marker : end;
        if (active) strbuf_append_str_n(results[next].output, cursor, (size_t)(body_end - cursor));
        else if (body_end != cursor) { failure = "output outside a script result"; break; }
        if (!marker) break;
        const char* newline = (const char*)memchr(marker, '\n', (size_t)(end - marker));
        if (!newline) { failure = "unterminated batch marker"; break; }
        if (kind == TEST_BATCH_START) {
            const char* path_end = newline;
            if (path_end > marker && path_end[-1] == '\r') path_end--;
            if (active || next == count || !str_eq(marker + 13,
                    (size_t)(path_end - marker - 13), paths[next], strlen(paths[next]))) {
                failure = "unexpected, duplicate or misordered script";
                break;
            }
            results[next].output = strbuf_new();
            active = true;
        } else if (kind == TEST_BATCH_END) {
            char* number_end = nullptr;
            errno = 0;
            long status = strtol(marker + 11, &number_end, 10);
            if (number_end < newline && *number_end == '\r') number_end++;
            if (!active || number_end == marker + 11 || number_end != newline || errno ||
                    status < INT_MIN || status > INT_MAX) {
                failure = "invalid batch completion";
                break;
            }
            results[next].exit_code = (int)status;
            results[next].complete = true;
            next++;
            active = false;
        } else if (!active) {
            failure = "telemetry outside a script result";
        }
        cursor = newline + 1;
    }
    if (!failure && (active || next != count)) failure = "missing batch result";
    if (!failure && (process.exit_code != 0 || process.timed_out || process.cancelled ||
            process.output_limit_exceeded)) failure = "batch process failed or capture was truncated";
    if (failure) {
        strbuf_append_str(error, failure);
        // all assigned cases fail, including a crash after the final marker;
        // never retry a crashed batch into an apparently green result.
        for (size_t i = 0; i < count; i++) results[i].exit_code = -1;
        return false;
    }
    return true;
}

#endif
