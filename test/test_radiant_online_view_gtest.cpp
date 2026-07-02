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

#define LAMBDA_EXE "./lambda.exe"
#define ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS 90
#define ONLINE_VIEW_OUTPUT_LIMIT (1024 * 1024)

struct RadiantOnlineViewCase {
    const char* label;
    const char* url;
    bool expect_linked_resources;
};

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
    uint64_t memtrack_live_bytes;
    uint64_t memtrack_live_count;
    char output_path[256];
    char lambda_log_path[256];
    RadiantOnlineViewBuffer output;
};

static const RadiantOnlineViewCase g_online_view_cases[] = {
    {"example", "https://example.com/", false},
    {"wikipedia", "https://www.wikipedia.org/", true},
    {"google", "https://www.google.com/", true},
    {"reactos", "https://reactos.org/", true},
    {"firefox", "https://www.firefox.com/en-US/", true},
    {"wikipedia_main_page", "https://en.wikipedia.org/wiki/Main_Page", true},
};

static bool online_view_file_readable(const char* path) {
    return access(path, R_OK) == 0;
}

static bool online_view_file_executable(const char* path) {
    return access(path, X_OK) == 0;
}

static void online_view_ensure_temp_dir() {
    mkdir("./temp", 0755);
}

static bool online_view_write_noop_events(const char* path) {
    static const char content[] =
        "{\n"
        "  \"name\": \"radiant online URL smoke\",\n"
        "  \"viewport\": {\"width\": 1200, \"height\": 800},\n"
        "  \"events\": [\n"
        "    {\"type\": \"wait\", \"ms\": 200}\n"
        "  ]\n"
        "}\n";
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    size_t written = fwrite(content, 1, sizeof(content) - 1, file);
    fclose(file);
    return written == sizeof(content) - 1;
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
    return ONLINE_VIEW_DEFAULT_TIMEOUT_SECONDS;
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

static bool online_view_run_case(const RadiantOnlineViewCase* view_case,
                                 RadiantOnlineViewResult* result) {
    if (!view_case || !result) return false;
    memset(result, 0, sizeof(*result));
    result->exit_code = -1;
    online_view_ensure_temp_dir();

    int log_written = snprintf(result->lambda_log_path, sizeof(result->lambda_log_path),
                               "./temp/test_radiant_online_view_%s_lambda.log",
                               view_case->label);
    int out_written = snprintf(result->output_path, sizeof(result->output_path),
                               "./temp/test_radiant_online_view_%s_output.log",
                               view_case->label);
    if (log_written <= 0 || log_written >= (int)sizeof(result->lambda_log_path) ||
        out_written <= 0 || out_written >= (int)sizeof(result->output_path)) {
        result->exec_failed = true;
        return false;
    }

    const char* event_path = "./temp/test_radiant_online_view_events.json";
    if (!online_view_write_noop_events(event_path)) {
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
        setenv("LAMBDA_LOG_FILE", result->lambda_log_path, 1);
        execl(LAMBDA_EXE, LAMBDA_EXE, "view", view_case->url,
              "--event-file", event_path, "--headless", (char*)NULL);
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

    result->memtrack_live_bytes = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "bytes=");
    result->memtrack_live_count = online_view_parse_tagged_uint64(
        result->output.data, "[MEMTRACK_LIVE]", "count=");
    online_view_save_buffer(result->output_path, &result->output);
    return true;
}

static const char* online_view_first_runtime_error(const RadiantOnlineViewResult* result) {
    static const char* patterns[] = {
        "AddressSanitizer",
        "LeakSanitizer",
        "Segmentation fault",
        "Assertion failed",
        "SIGSEGV",
        "SIGBUS",
        "Fatal error",
        "panic",
        "Failed to load document",
        "failed to create resource manager",
        "view: network resource failures detected",
        "network: download failed",
        "network: failed to load image",
        "Unsupported image format",
        "[BG-IMAGE] Failed",
        "resource_loaders: failed",
        "curl-multi: failed",
        "curl-multi: HTTP",
        "script_runner: failed to download external script",
        "execute_document_scripts: JS execution timed out",
        "execute_document_scripts: recovered from crash",
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
    ASSERT_TRUE(online_view_file_readable(LAMBDA_EXE)) << "lambda.exe not found";
    ASSERT_TRUE(online_view_file_executable(LAMBDA_EXE)) << "lambda.exe is not executable";
    ASSERT_LT(index, sizeof(g_online_view_cases) / sizeof(g_online_view_cases[0]));

    RadiantOnlineViewResult result;
    const RadiantOnlineViewCase* view_case = &g_online_view_cases[index];
    ASSERT_TRUE(online_view_run_case(view_case, &result))
        << "failed to launch online view case " << view_case->label;

    EXPECT_FALSE(result.exec_failed) << "launch failed for " << view_case->url;
    EXPECT_FALSE(result.output_alloc_failed) << "failed to capture output for " << view_case->url;
    EXPECT_FALSE(result.timed_out)
        << view_case->url << " timed out; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;
    EXPECT_EQ(0, result.exit_code)
        << view_case->url << " exited with " << result.exit_code
        << "; stdout/stderr: " << result.output_path
        << ", lambda log: " << result.lambda_log_path;

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

TEST(RadiantOnlineViewTest, LoadsExampleDotCom) {
    online_view_expect_case(0);
}

TEST(RadiantOnlineViewTest, LoadsWikipediaOrg) {
    online_view_expect_case(1);
}

TEST(RadiantOnlineViewTest, LoadsGoogleCom) {
    online_view_expect_case(2);
}

TEST(RadiantOnlineViewTest, LoadsReactOSOrg) {
    online_view_expect_case(3);
}

TEST(RadiantOnlineViewTest, LoadsFirefoxCom) {
    online_view_expect_case(4);
}

TEST(RadiantOnlineViewTest, LoadsWikipediaMainPage) {
    online_view_expect_case(5);
}

#endif
