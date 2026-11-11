#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

#ifdef _WIN32
TEST(RadiantOnlineViewTest, SkippedOnWindows) {
    GTEST_SKIP() << "online view smoke tests use fork/select/kill on Unix";
}
#else

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../lambda/module/radiant/script_timeout.hpp"

#define LAMBDA_EXE "./lambda.exe"
#define ONLINE_VIEW_CLEANUP_GRACE_SECONDS 30
#define ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS \
    (RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS + ONLINE_VIEW_CLEANUP_GRACE_SECONDS)
#define ONLINE_VIEW_OUTPUT_LIMIT (1024 * 1024)
#define ONLINE_VIEW_METRICS_PATH "./temp/test_radiant_online_view_metrics.tsv"

static const char* online_view_lambda_exe() {
    const char* executable = getenv("LAMBDA_RADIANT_ONLINE_VIEW_EXE");
    return executable && executable[0] ? executable : LAMBDA_EXE;
}

static_assert(ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS >
                  RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS,
              "online parent timeout must outlive the script watchdog");

#define ONLINE_VIEW_PAGES_PATH "test/online/pages.csv"
#define ONLINE_VIEW_CASE_CAPACITY 2048
#define ONLINE_VIEW_LABEL_CAPACITY 128
#define ONLINE_VIEW_URL_CAPACITY 2048

struct RadiantOnlineViewCase {
    char label[ONLINE_VIEW_LABEL_CAPACITY];
    char url[ONLINE_VIEW_URL_CAPACITY];
    bool expect_linked_resources;
};

static RadiantOnlineViewCase g_online_view_cases[ONLINE_VIEW_CASE_CAPACITY];

static bool online_view_read_csv_field(char** cursor, char* output, size_t capacity,
                                       bool has_following_field) {
    char* position = *cursor;
    size_t length = 0;
    bool quoted = *position == '"';
    bool closed_quote = !quoted;
    if (quoted) position++;

    while (*position) {
        if (quoted && *position == '"') {
            if (position[1] == '"') {
                if (length + 1 >= capacity) return false;
                output[length++] = '"';
                position += 2;
            } else {
                closed_quote = true;
                position++;
                break;
            }
        } else if (!quoted && *position == ',') {
            break;
        } else {
            if (length + 1 >= capacity) return false;
            output[length++] = *position++;
        }
    }

    if (!closed_quote || length == 0) return false;
    output[length] = '\0';
    if (has_following_field) {
        if (*position != ',') return false;
        *cursor = position + 1;
    } else {
        if (*position != '\0') return false;
        *cursor = position;
    }
    return true;
}

static size_t online_view_load_cases() {
    FILE* file = fopen(ONLINE_VIEW_PAGES_PATH, "r");
    if (!file) return 0;

    char line[4096];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return 0;
    }

    size_t count = 0;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0) continue;
        if (count >= ONLINE_VIEW_CASE_CAPACITY) {
            fclose(file);
            return 0;
        }

        char linked_resources[16];
        char* cursor = line;
        RadiantOnlineViewCase* view_case = &g_online_view_cases[count];
        if (!online_view_read_csv_field(&cursor, view_case->label,
                                        sizeof(view_case->label), true) ||
            !online_view_read_csv_field(&cursor, view_case->url,
                                        sizeof(view_case->url), true) ||
            !online_view_read_csv_field(&cursor, linked_resources,
                                        sizeof(linked_resources), false)) {
            fclose(file);
            return 0;
        }
        if (strcmp(linked_resources, "true") == 0) {
            view_case->expect_linked_resources = true;
        } else if (strcmp(linked_resources, "false") == 0) {
            view_case->expect_linked_resources = false;
        } else {
            fclose(file);
            return 0;
        }
        count++;
    }

    bool read_failed = ferror(file) != 0;
    fclose(file);
    return read_failed ? 0 : count;
}

static const size_t g_online_view_case_count = online_view_load_cases();

struct RadiantOnlineViewBuffer {
    char* data;
    size_t len;
    size_t cap;
    bool truncated;
};

struct RadiantOnlineViewResult {
    int exit_code;
    bool timed_out;
    bool exec_failed;
    bool output_alloc_failed;
    uint64_t elapsed_ms;
    uint64_t memtrack_peak_bytes;
    uint64_t memtrack_live_bytes;
    uint64_t memtrack_live_count;
    uint64_t peak_footprint_mb;
    uint64_t peak_resident_mb;
    char output_path[256];
    char lambda_log_path[256];
    char timing_path[256];
    RadiantOnlineViewBuffer output;
};

// The shared CSV inventory drives both this smoke test and local snapshot capture.
static const char* online_view_normalized_site_start(const char* start,
                                                     const char* end) {
    const char* previous_dot = NULL;
    const char* last_dot = NULL;
    for (const char* pos = start; pos < end; pos++) {
        if (*pos != '.') continue;
        previous_dot = last_dot;
        last_dot = pos;
    }
    return previous_dot ? previous_dot + 1 : start;
}

static bool online_view_same_normalized_site(const char* first,
                                              const char* second) {
    const char* first_start = first ? strstr(first, "://") : NULL;
    const char* second_start = second ? strstr(second, "://") : NULL;
    if (!first_start || !second_start) return false;
    first_start += 3;
    second_start += 3;
    if (strncmp(first_start, "www.", 4) == 0) first_start += 4;
    if (strncmp(second_start, "www.", 4) == 0) second_start += 4;
    const char* first_end = strpbrk(first_start, "/:?#");
    const char* second_end = strpbrk(second_start, "/:?#");
    if (!first_end) first_end = first_start + strlen(first_start);
    if (!second_end) second_end = second_start + strlen(second_start);
    first_start = online_view_normalized_site_start(first_start, first_end);
    second_start = online_view_normalized_site_start(second_start, second_end);
    size_t first_len = (size_t)(first_end - first_start);
    size_t second_len = (size_t)(second_end - second_start);
    return first_len == second_len &&
        strncmp(first_start, second_start, first_len) == 0;
}

static bool online_view_file_readable(const char* path) {
    return access(path, R_OK) == 0;
}

static bool online_view_file_executable(const char* path) {
    return access(path, X_OK) == 0;
}

static void online_view_ensure_temp_dir() {
    mkdir("./temp", 0755);
}

static bool online_view_buffer_append(RadiantOnlineViewBuffer* buffer,
                                      const char* data, size_t len) {
    if (!buffer || !data || len == 0) return true;
    if (buffer->len >= ONLINE_VIEW_OUTPUT_LIMIT) {
        buffer->truncated = true;
        return true;
    }
    size_t allowed = ONLINE_VIEW_OUTPUT_LIMIT - buffer->len;
    if (len > allowed) {
        len = allowed;
        buffer->truncated = true;
    }
    if (buffer->len + len + 1 > buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 4096;
        while (new_cap < buffer->len + len + 1) new_cap *= 2;
        char* next = (char*)realloc(buffer->data, new_cap);
        if (!next) return false;
        buffer->data = next;
        buffer->cap = new_cap;
    }
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return true;
}

static void online_view_buffer_free(RadiantOnlineViewBuffer* buffer) {
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static void online_view_save_buffer(const char* path, const RadiantOnlineViewBuffer* buffer) {
    if (!path || !buffer) return;
    FILE* file = fopen(path, "wb");
    if (!file) return;
    if (buffer->data && buffer->len > 0) {
        fwrite(buffer->data, 1, buffer->len, file);
    }
    fclose(file);
}

static int online_view_timeout_seconds() {
    const char* env = getenv("LAMBDA_RADIANT_ONLINE_VIEW_TIMEOUT");
    if (env && env[0]) {
        int parsed = atoi(env);
        if (parsed > 0) return parsed;
    }
    return radiant_script_exec_timeout_ceiling_seconds() +
        ONLINE_VIEW_CLEANUP_GRACE_SECONDS;
}

static uint64_t online_view_now_ms() {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
        (uint64_t)(now.tv_nsec / 1000000);
}

static int online_view_exit_code(int status) {
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
}

static bool online_view_file_contains(const char* path, const char* needle) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    bool found = false;
    char chunk[4096];
    size_t needle_len = strlen(needle);
    char carry[128];
    size_t carry_len = 0;
    carry[0] = '\0';

    while (!found) {
        size_t n = fread(chunk, 1, sizeof(chunk) - 1, file);
        if (n == 0) break;
        chunk[n] = '\0';

        char scan[sizeof(carry) + sizeof(chunk)];
        size_t scan_len = carry_len;
        memcpy(scan, carry, carry_len);
        memcpy(scan + scan_len, chunk, n + 1);
        if (strstr(scan, needle)) {
            found = true;
            break;
        }
        if (needle_len > 1) {
            carry_len = needle_len - 1;
            if (carry_len >= sizeof(carry)) carry_len = sizeof(carry) - 1;
            if (n >= carry_len) {
                memcpy(carry, chunk + n - carry_len, carry_len);
            } else {
                carry_len = n;
                memcpy(carry, chunk, carry_len);
            }
            carry[carry_len] = '\0';
        }
    }
    fclose(file);
    return found;
}

static bool online_view_output_contains(const RadiantOnlineViewResult* result,
                                        const char* needle) {
    return result && result->output.data && strstr(result->output.data, needle) != NULL;
}

static bool online_view_result_contains(const RadiantOnlineViewResult* result,
                                        const char* needle) {
    if (online_view_output_contains(result, needle)) return true;
    if (result && online_view_file_contains(result->lambda_log_path, needle)) return true;
    return false;
}

static bool online_view_timing_has_detail(const char* timing_path) {
    static const char* required_metrics[] = {
        "html_download_read\t",
        "stylesheet_setup\t",
        "initial_cascade\t",
        "post_script_recascade\t",
        "script_network_wait\t",
        "layout\t",
        "behavior_handler_dispatch\t",
        "render\t",
        "cleanup_network\t",
    };
    if (!timing_path) return false;
    for (size_t i = 0; i < sizeof(required_metrics) / sizeof(required_metrics[0]); i++) {
        if (!online_view_file_contains(timing_path, required_metrics[i])) return false;
    }
    return true;
}

static uint64_t online_view_parse_tagged_uint64(const char* output,
                                                const char* tag,
                                                const char* key) {
    if (!output || !tag || !key) return 0;
    const char* pos = strstr(output, tag);
    if (!pos) return 0;
    pos = strstr(pos, key);
    if (!pos) return 0;
    pos += strlen(key);
    while (*pos && (*pos < '0' || *pos > '9')) pos++;
    if (!*pos) return 0;
    return strtoull(pos, NULL, 10);
}

static uint64_t online_view_parse_peak_mem_stage_mb(const char* output,
                                                     const char* key) {
    if (!output || !key) return 0;
    uint64_t peak = 0;
    const char* stage = output;
    while ((stage = strstr(stage, "MEMSTAGE:")) != NULL) {
        const char* line_end = strchr(stage, '\n');
        const char* value = strstr(stage, key);
        if (value && (!line_end || value < line_end)) {
            value += strlen(key);
            while (*value && (*value < '0' || *value > '9')) value++;
            if (*value) {
                uint64_t parsed = strtoull(value, NULL, 10);
                if (parsed > peak) peak = parsed;
            }
        }
        stage = line_end ? line_end + 1 : stage + strlen(stage);
    }
    return peak;
}

static bool online_view_append_metrics(const RadiantOnlineViewCase* view_case,
                                       const RadiantOnlineViewResult* result) {
    static bool initialized = false;
    if (!view_case || !result) return false;

    FILE* file = fopen(ONLINE_VIEW_METRICS_PATH, initialized ? "ab" : "wb");
    if (!file) return false;
    int header_written = 1;
    if (!initialized) {
        header_written = fprintf(file, "label\turl\telapsed_ms\tmemtrack_peak_bytes\t"
            "peak_footprint_mb\tpeak_resident_mb\tmemtrack_live_bytes\t"
            "memtrack_live_count\texit_code\ttimed_out\toutput_path\ttiming_path\n");
    }
    int written = fprintf(file,
        "%s\t%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%d\t%d\t%s\t%s\n",
        view_case->label, view_case->url,
        (unsigned long long)result->elapsed_ms,
        (unsigned long long)result->memtrack_peak_bytes,
        (unsigned long long)result->peak_footprint_mb,
        (unsigned long long)result->peak_resident_mb,
        (unsigned long long)result->memtrack_live_bytes,
        (unsigned long long)result->memtrack_live_count,
        result->exit_code, result->timed_out ? 1 : 0, result->output_path,
        result->timing_path);
    bool saved = header_written > 0 && written > 0 && fclose(file) == 0;
    if (saved) initialized = true;
    return saved;
}

static bool online_view_run_case(const RadiantOnlineViewCase* view_case,
                                 RadiantOnlineViewResult* result) {
    if (!view_case || !result) return false;
    memset(result, 0, sizeof(*result));
    result->exit_code = -1;
    online_view_ensure_temp_dir();
    uint64_t run_start_ms = online_view_now_ms();

    int log_written = snprintf(result->lambda_log_path, sizeof(result->lambda_log_path),
                               "./temp/test_radiant_online_view_%s_lambda.log",
                               view_case->label);
    int out_written = snprintf(result->output_path, sizeof(result->output_path),
                               "./temp/test_radiant_online_view_%s_output.log",
                               view_case->label);
    int timing_written = snprintf(result->timing_path, sizeof(result->timing_path),
                                  "./temp/test_radiant_online_view_%s_timing.tsv",
                                  view_case->label);
    if (log_written <= 0 || log_written >= (int)sizeof(result->lambda_log_path) ||
        out_written <= 0 || out_written >= (int)sizeof(result->output_path) ||
        timing_written <= 0 || timing_written >= (int)sizeof(result->timing_path)) {
        result->exec_failed = true;
        return false;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result->exec_failed = true;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result->exec_failed = true;
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        setpgid(0, 0);
        setenv("VIEW_MEM_STAGES", "1", 1);
        setenv("VIEW_MEM_STATS", "1", 1);
        setenv("LAMBDA_LOG_FILE", result->lambda_log_path, 1);
        setenv("RADIANT_VIEW_TIMING_PATH", result->timing_path, 1);
        // Keep smoke-test milestones while suppressing per-node diagnostics
        // that distort the load time of large real documents.
        setenv("LAMBDA_LOG_LEVEL", "NOTICE", 1);
        const char* lambda_exe = online_view_lambda_exe();
        // The smoke test ends after first render. A synthetic wait pumps arbitrary
        // page timers and turns post-load reflows into apparent load time.
        execl(lambda_exe, lambda_exe, "view", view_case->url, "--headless", (char*)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    int timeout_seconds = online_view_timeout_seconds();
    time_t deadline = time(NULL) + timeout_seconds;
    bool child_done = false;
    while (!child_done) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int selected = select(pipefd[0] + 1, &fds, NULL, NULL, &tv);
        if (selected > 0 && FD_ISSET(pipefd[0], &fds)) {
            char chunk[2048];
            while (true) {
                ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
                if (n > 0) {
                    if (!online_view_buffer_append(&result->output, chunk, (size_t)n)) {
                        result->output_alloc_failed = true;
                    }
                } else if (n == 0) {
                    child_done = true;
                    break;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    child_done = true;
                    break;
                }
            }
        }
        if (!child_done && time(NULL) >= deadline) {
            result->timed_out = true;
            kill(-pid, SIGKILL);
            child_done = true;
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!result->timed_out) result->exit_code = online_view_exit_code(status);
    uint64_t run_end_ms = online_view_now_ms();
    if (run_end_ms >= run_start_ms) result->elapsed_ms = run_end_ms - run_start_ms;

    result->memtrack_live_bytes = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "bytes=");
    result->memtrack_live_count = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "count=");
    result->memtrack_peak_bytes = online_view_parse_tagged_uint64(
        result->output.data, "[MEMSTATS] total", "peak=");
    result->peak_footprint_mb = online_view_parse_peak_mem_stage_mb(
        result->output.data, "peak=");
    result->peak_resident_mb = online_view_parse_peak_mem_stage_mb(
        result->output.data, "resident=");
    online_view_save_buffer(result->output_path, &result->output);
    return true;
}

static const char* online_view_first_runtime_error(const RadiantOnlineViewResult* result) {
    static const char* patterns[] = {
        // LLVM pages legitimately mention sanitizer names; match sanitizer report prefixes instead.
        "ERROR: AddressSanitizer",
        "AddressSanitizer:DEADLYSIGNAL",
        "ERROR: LeakSanitizer",
        "Segmentation fault",
        "Assertion failed",
        "mempool: free pointer ",
        "mempool: realloc pointer ",
        "Fatal error",
        "Failed to download HTML from URL",
        "Failed to load document",
        "failed to create resource manager",
        "view: network resource failures detected",
        "network: download failed",
        "network: failed to load image",
        "network: no UI context available for font loading",
        "Unsupported image format",
        "IMAGE_IO: load ",
        "[BG-IMAGE] Failed",
        "resource_loaders: failed",
        "curl-multi: failed",
        "curl-multi: HTTP",
        "script_runner: failed to download external script",
        "execute_document_scripts: JS execution timed out",
        "execute_document_scripts: recovered from crash",
        "execute_document_scripts: post-dom exception",
        "execute_document_scripts: post-dom script task failed",
        "execute_document_scripts: defer script task failed",
        "execute_document_scripts: async-ready script task failed",
        "mir: undefined variable",
        "failed to resolve native fn/pn",
        "import of undefined item",
        "layout_flow_node: node visit budget",
        "container-layout: size dependencies failed to converge",
        "stack cleanup: sigaltstack",
        "js-mir: unsupported",
        "memtrack: LEAK",
        "Uncaught"
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (online_view_result_contains(result, patterns[i])) return patterns[i];
    }
    return NULL;
}

static void online_view_expect_case(size_t index) {
    const char* lambda_exe = online_view_lambda_exe();
    ASSERT_TRUE(online_view_file_readable(lambda_exe)) << "online renderer not found";
    ASSERT_TRUE(online_view_file_executable(lambda_exe)) << "online renderer is not executable";
    ASSERT_LT(index, g_online_view_case_count);

    RadiantOnlineViewResult result;
    const RadiantOnlineViewCase* view_case = &g_online_view_cases[index];
    bool ran = online_view_run_case(view_case, &result);
    ASSERT_TRUE(online_view_append_metrics(view_case, &result))
        << "failed to append metrics to " << ONLINE_VIEW_METRICS_PATH;
    ASSERT_TRUE(ran)
        << "failed to launch online view case " << view_case->label;

    GTEST_LOG_(INFO) << "radiant-online-view: url=" << view_case->url
        << " elapsed_ms=" << result.elapsed_ms
        << " exit_code=" << result.exit_code
        << " timed_out=" << (result.timed_out ? "true" : "false");

    EXPECT_FALSE(result.exec_failed) << "launch failed for " << view_case->url;
    EXPECT_FALSE(result.output_alloc_failed) << "failed to capture output for " << view_case->url;
    EXPECT_FALSE(result.timed_out)
        << view_case->url << " timed out; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_EQ(0, result.exit_code)
        << view_case->url << " exited with " << result.exit_code
        << "; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_TRUE(online_view_file_readable(result.timing_path))
        << "missing phase timing report for " << view_case->url
        << ": " << result.timing_path;
    EXPECT_TRUE(online_view_timing_has_detail(result.timing_path))
        << "incomplete phase timing report for " << view_case->url
        << ": " << result.timing_path;

    // Top-level HTTP pages are staged through ./temp; the invariant is that
    // the original document URL still drives resource discovery and downloads.
    EXPECT_TRUE(online_view_result_contains(&result, "view: network support initialized for HTTP document"))
        << view_case->url << " did not initialize network support; see "
        << result.lambda_log_path;
    EXPECT_TRUE(online_view_result_contains(&result, "view: network resource discovery complete"))
        << view_case->url << " did not finish resource discovery; see "
        << result.lambda_log_path;
    if (view_case->expect_linked_resources) {
        EXPECT_TRUE(online_view_result_contains(&result, "view: network resource stats total="))
            << view_case->url << " did not report linked resource stats; see "
            << result.lambda_log_path;
    }

    EXPECT_TRUE(online_view_result_contains(&result, "view: document loaded, starting layout"))
        << view_case->url << " did not report document load completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view: layout complete, rendering"))
        << view_case->url << " did not report layout completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view: render complete"))
        << view_case->url << " did not report render completion";
    EXPECT_TRUE(online_view_result_contains(&result, "view command completed with result: 0"))
        << view_case->url << " did not report clean view command shutdown";

    // Runtime errors used to be easy to swallow in logs while the process still
    // exited zero; keep the smoke test sensitive to those reported failures.
    const char* runtime_error = online_view_first_runtime_error(&result);
    EXPECT_EQ((const char*)NULL, runtime_error)
        << view_case->url << " reported runtime/resource/script error pattern: "
        << (runtime_error ? runtime_error : "(none)") << "; stdout/stderr: "
        << result.output_path << ", lambda log: " << result.lambda_log_path;

    EXPECT_TRUE(online_view_output_contains(&result, "[MEMTRACK_LIVE]"))
        << view_case->url << " did not emit memtrack shutdown telemetry";
    EXPECT_EQ(0ULL, result.memtrack_live_bytes)
        << view_case->url << " retained " << (unsigned long long)result.memtrack_live_bytes
        << " bytes; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_EQ(0ULL, result.memtrack_live_count)
        << view_case->url << " retained " << (unsigned long long)result.memtrack_live_count
        << " allocations; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;

    online_view_buffer_free(&result.output);
}

TEST(RadiantOnlineViewTest, CatalogLoadsWithUniqueLabels) {
    ASSERT_GT(g_online_view_case_count, 0u) << "failed to read " << ONLINE_VIEW_PAGES_PATH;
    for (size_t index = 0; index < g_online_view_case_count; index++) {
        for (size_t candidate = index + 1; candidate < g_online_view_case_count; candidate++) {
            EXPECT_STRNE(g_online_view_cases[index].label, g_online_view_cases[candidate].label);
        }
    }
}

TEST(RadiantOnlineViewTest, GeneratedSaasLandingUrlsStayDiverse) {
    size_t generated_count = 0;
    for (size_t index = 0; index < g_online_view_case_count; index++) {
        if (strncmp(g_online_view_cases[index].label, "saas_landing_", 13) != 0) continue;
        generated_count++;
        size_t matches = 0;
        for (size_t candidate = 0; candidate < g_online_view_case_count; candidate++) {
            if (strncmp(g_online_view_cases[candidate].label, "saas_landing_", 13) == 0 &&
                online_view_same_normalized_site(g_online_view_cases[index].url,
                    g_online_view_cases[candidate].url)) {
                matches++;
            }
        }
        EXPECT_LE(matches, 2u) << g_online_view_cases[index].url;
    }
    EXPECT_GT(generated_count, 0u);
}

TEST(RadiantOnlineViewTest, DetectsRendererErrorsEvenWhenViewSucceeds) {
    RadiantOnlineViewResult result = {};
    struct Case { char output[160]; const char* pattern; } cases[] = {
        {"IMAGE_IO: load cached-file: unrecognized image signature\nview command completed with result: 0\n",
            "IMAGE_IO: load "},
        {"container-layout: size dependencies failed to converge after 8 passes\nview command completed with result: 0\n",
            "container-layout: size dependencies failed to converge"},
        {"network: no UI context available for font loading\nview command completed with result: 0\n",
            "network: no UI context available for font loading"},
        {"mempool: free pointer 0x123 is not owned by pool 2\nview command completed with result: 0\n",
            "mempool: free pointer "},
        {"mempool: realloc pointer 0x123 is not owned by pool 2\nview command completed with result: 0\n",
            "mempool: realloc pointer "},
        {"execute_document_scripts: defer script task failed: page.js\nview command completed with result: 0\n",
            "execute_document_scripts: defer script task failed"},
        {"execute_document_scripts: async-ready script task failed: page.js\nview command completed with result: 0\n",
            "execute_document_scripts: async-ready script task failed"}
    };
    for (Case& entry : cases) {
        result.output.data = entry.output;
        EXPECT_STREQ(online_view_first_runtime_error(&result), entry.pattern);
    }
    result.output.data = nullptr;
    EXPECT_EQ(online_view_first_runtime_error(&result), nullptr);
}

class RadiantOnlinePageTest : public ::testing::Test {
    size_t index_;
public:
    explicit RadiantOnlinePageTest(size_t index) : index_(index) {}
    void TestBody() override { online_view_expect_case(index_); }
};

static void online_view_register_cases() {
    // catalog labels keep discovery stable when unavailable URLs are removed or reordered.
    for (size_t index = 0; index < g_online_view_case_count; index++) {
        char name[ONLINE_VIEW_LABEL_CAPACITY + 8];
        snprintf(name, sizeof(name), "Loads_%s", g_online_view_cases[index].label);
        for (char* ch = name; *ch; ch++) if (*ch == '-') *ch = '_';
        ::testing::RegisterTest("RadiantOnlineViewTest", name, nullptr, nullptr, __FILE__, __LINE__,
            [index]() -> ::testing::Test* { return new RadiantOnlinePageTest(index); });
    }
}

int main(int argc, char** argv) {
    online_view_register_cases();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif
