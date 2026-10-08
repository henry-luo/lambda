#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "../radiant/script_timeout.hpp"

extern "C" {
#include "../lib/shell.h"
#include "../lib/file.h"
}

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

struct RadiantViewCase {
    const char* test_name;
    const char* label;
    const char* path;
    const char* event_path;
    bool requires_clean_memtrack;
};

struct RadiantViewCaseResult {
    bool executed;
    bool missing_path;
    bool has_layout_prof;
    bool has_render_prof;
    bool has_peak_footprint;
    bool has_view_completed;
    bool has_clean_memtrack;
    int exit_code;
    char message[512];
};

static const RadiantViewCase g_radiant_view_cases[] = {
    {"RadiantViewTest.LoadsPngAsHeadlessView", "png", "test/layout/data/res/sample1.png"},
    {"RadiantViewTest.LoadsJpegAsHeadlessView", "jpg", "test/layout/data/res/sample1.jpg"},
    {"RadiantViewTest.LoadsGifAsHeadlessView", "gif", "test/layout/data/res/hn_s.gif"},
    {"RadiantViewTest.LoadsSvgAsHeadlessView", "svg", "test/layout/data/res/hn_y18.svg"},
    {"RadiantViewTest.LoadsHtmlAsHeadlessView", "html", "test/layout/data/page/sample1.html"},
    {"RadiantViewTest.LoadsXmlAsHeadlessView", "xml", "test/input/test.xml"},
    {"RadiantViewTest.LoadsMarkdownAsHeadlessView", "markdown", "test/input/comprehensive_test.md"},
    {"RadiantViewTest.LoadsMarkdownMathAsHeadlessView", "markdown_math", "test/input/simple_math_test.md"},
    {"RadiantViewTest.LoadsWikiAsHeadlessView", "wiki", "test/input/test.wiki"},
    {"RadiantViewTest.LoadsLatexShowcaseAsHeadlessView", "latex_showcase", "test/input/latex-showcase.tex"},
    {"RadiantViewTest.LoadsMathIntensiveLatexAsHeadlessView", "latex_math_intensive", "test/input/math_intensive_test.tex"},
    {"RadiantViewTest.LoadsYamlAsHeadlessView", "yaml", "test/input/more_test.yaml"},
    {"RadiantViewTest.LoadsLambdaReportAsHeadlessView", "lambda_report", "test/lambda/complex_iot_report_html.ls"},
    {"RadiantViewTest.LoadsLambdaChartDashboardAsHeadlessView", "lambda_chart_dashboard", "test/lambda/chart/chart_dashboard.ls"},
    {"RadiantViewTest.LoadsPdfAsHeadlessView", "pdf", "test/input/raw_commands_test.pdf"},
    {"RadiantViewTest.ReleasesDetachedScriptTextControl", "detached_script_text_control",
     "test/html/js_detached_text_control.html",
     "test/html/js_detached_text_control_events.json", true},
    {"RadiantViewTest.PreservesDocumentUrlAcrossScriptFormSubmit", "script_form_submit_url_ownership",
     "test/html/js_form_submit_url_ownership.html", nullptr, true},
    {"RadiantViewTest.LaysOutDenseCollapsedTable", "dense_collapsed_table",
     "test/html/dense_collapsed_table.html", nullptr, true},
    {"RadiantViewTest.PreservesMarkerPropsDuringRetainedTableReflow",
     "retained_table_marker_intrinsic", "test/html/retained_table_marker_intrinsic.html",
     "test/html/retained_table_marker_intrinsic_events.json", true},
    {"RadiantViewTest.ReleasesForeignDocumentRegistryBeyondInitialCapacity",
     "foreign_document_registry", "test/html/js_foreign_document_registry.html", nullptr, true},
    {"RadiantViewTest.ReleasesFailedFontFallbackHandles",
     "failed_font_fallback_registry", "test/html/failed_font_fallback_registry.html", nullptr, true},
};

static const size_t g_radiant_view_case_count =
    sizeof(g_radiant_view_cases) / sizeof(g_radiant_view_cases[0]);

static RadiantViewCaseResult g_radiant_view_results[
    sizeof(g_radiant_view_cases) / sizeof(g_radiant_view_cases[0])
];

static bool test_radiant_view_file_readable(const char* path) {
#ifdef _WIN32
    return _access(path, 4) == 0;
#else
    return access(path, R_OK) == 0;
#endif
}

static void test_radiant_view_ensure_temp_dir() {
#ifdef _WIN32
    _mkdir(".\\temp");
#else
    mkdir("./temp", 0755);
#endif
}

#ifndef _WIN32
struct RadiantViewImageServer {
    pid_t pid;
    int port;
};

static bool test_radiant_view_send_all(int client, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t count = send(client, data + sent, size - sent, 0);
        if (count <= 0) return false;
        sent += (size_t)count;
    }
    return true;
}

struct RadiantViewHttpResource { const char* request; const char* type; const char* body; size_t size; };

static bool test_radiant_view_unsupported_image_server_start(RadiantViewImageServer* server,
        const RadiantViewHttpResource* resources = nullptr, size_t resource_count = 0,
        const char* audit_path = nullptr) {
    if (!server) return false;
    memset(server, 0, sizeof(*server));

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    int reuse_address = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) != 0) {
        close(listener);
        return false;
    }
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
            listen(listener, 2) != 0) {
        close(listener);
        return false;
    }
    socklen_t address_size = sizeof(address);
    if (getsockname(listener, (struct sockaddr*)&address, &address_size) != 0) {
        close(listener);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(listener);
        return false;
    }
    if (pid == 0) {
        static const char page_document[] =
            "<!doctype html><html><head><style>"
            ".fixture { width: 10px; height: 10px; background-image: url('/unsupported.avif'); }"
            "</style></head><body>"
            "<img src=\"/unsupported.avif\" alt=\"unsupported\">"
            "<div class=\"fixture\"></div>"
            "</body></html>";
        static const char prime_document[] =
            "<!doctype html><script src=\"/unsupported.avif\"></script>";
        bool served_document = false;
        bool served_prime = false;
        while (!served_document) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(listener, &ready);
            struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
            int select_result = select(listener + 1, &ready, nullptr, nullptr, &timeout);
            if (select_result <= 0) break;
            int client = accept(listener, nullptr, nullptr);
            if (client < 0) continue;
            char request[1024] = {};
            ssize_t request_size = recv(client, request, sizeof(request) - 1, 0);
            bool is_image_request = request_size > 0 &&
                strstr(request, "GET /unsupported.avif ") != nullptr;
            bool is_prime_request = request_size > 0 &&
                strstr(request, "GET /prime.html ") != nullptr;
            static const unsigned char avif[] = {
                0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f',
                0x00, 0x00, 0x00, 0x00,
            };
            const char* body = is_image_request ? (const char*)avif :
                (is_prime_request ? prime_document : page_document);
            size_t body_size = is_image_request ? sizeof(avif) :
                (is_prime_request ? sizeof(prime_document) - 1 : sizeof(page_document) - 1);
            const char* type = is_image_request ? "image/avif" : "text/html";
            if (resources) {
                body = "missing fixture resource"; body_size = strlen(body); type = "text/plain";
                for (size_t i = 0; i < resource_count; i++) if (strstr(request, resources[i].request)) {
                    body = resources[i].body; body_size = resources[i].size; type = resources[i].type; break;
                }
            }
            if (audit_path) {
                FILE* audit = fopen(audit_path, "ab");
                if (audit) { fwrite(request, 1, request_size > 0 ? (size_t)request_size : 0, audit); fclose(audit); }
            }
            char header[256];
            int header_size = snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                type, body_size);
            bool sent = header_size > 0 && header_size < (int)sizeof(header) &&
                test_radiant_view_send_all(client, header, (size_t)header_size);
            if (sent) {
                sent = test_radiant_view_send_all(client, body, body_size);
            }
            close(client);
            if (!sent) break;
            if (is_prime_request) served_prime = true;
            if (!resources && !is_image_request && !is_prime_request && served_prime) served_document = true;
        }
        close(listener);
        _exit(served_document ? 0 : 1);
    }

    close(listener);
    server->pid = pid;
    server->port = ntohs(address.sin_port);
    return true;
}
#endif

static char* test_radiant_view_read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return nullptr;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return nullptr;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return nullptr;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return nullptr;
    }
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return nullptr;
    }
    size_t read_size = fread(buffer, 1, (size_t)size, file);
    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}

static bool test_radiant_view_file_contains(const char* path, const char* needle) {
    char* buffer = test_radiant_view_read_file(path);
    if (!buffer) return false;
    bool found = strstr(buffer, needle) != NULL;
    free(buffer);
    return found;
}

static bool test_radiant_view_profile_has_intrinsic_measurement(const char* path) {
    char* buffer = test_radiant_view_read_file(path);
    if (!buffer) return false;
    const char* profile = strstr(buffer, "[LAYOUT_PROFILE] intrinsic:");
    const char* requests = profile ? strstr(profile, "requests=") : nullptr;
    bool found = false;
    if (requests) {
        char* end = nullptr;
        const char* value = requests + strlen("requests=");
        found = strtoul(value, &end, 10) > 0 && end != value;
    }
    free(buffer);
    return found;
}

static bool test_radiant_view_profile_shifted_reuse_at(const char* path,
                                                       size_t layout_index,
                                                       unsigned long long* reuse_count) {
    if (!reuse_count) return false;
    char* buffer = test_radiant_view_read_file(path);
    if (!buffer) return false;
    const char* prefix = "[LAYOUT_PROFILE] shifted_reuse: ";
    const char* cursor = buffer;
    bool found = false;
    for (size_t index = 0; index <= layout_index; index++) {
        cursor = strstr(cursor, prefix);
        if (!cursor) break;
        cursor += strlen(prefix);
        if (index == layout_index) {
            char* end = nullptr;
            unsigned long long value = strtoull(cursor, &end, 10);
            found = end != cursor;
            if (found) *reuse_count = value;
        }
    }
    free(buffer);
    return found;
}

static ShellResult test_radiant_view_run_logged_headless(const char* page,
                                                         const char* event_path,
                                                         const ShellEnvEntry* env,
                                                         const char* optimization = nullptr) {
    const char* args[8] = {};
    int arg_count = 0;
    args[arg_count++] = "./lambda.exe";
    args[arg_count++] = "view";
    args[arg_count++] = page;
    if (event_path) {
        args[arg_count++] = "--event-file";
        args[arg_count++] = event_path;
    }
    args[arg_count++] = "--headless";
    if (optimization) args[arg_count++] = optimization;
    args[arg_count] = NULL;
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    return shell_exec("./lambda.exe", args, &options);
}

static ShellResult test_radiant_view_run_layout_timing(const char* page,
                                                        const char* timing_path) {
    const char* args[] = {
        "./lambda.exe", "layout", page, "--timing-output", timing_path, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    return shell_exec("./lambda.exe", args, &options);
}

static bool test_radiant_view_write_script_bytes(FILE* file, size_t bytes) {
    const char statement[] = "var scriptBudgetPadding = 0;\n";
    if (!file) return false;
    size_t written = 0;
    while (written < bytes) {
        size_t remaining = bytes - written;
        size_t chunk = remaining < sizeof(statement) - 1 ? remaining : sizeof(statement) - 1;
        if (fwrite(statement, 1, chunk, file) != chunk) {
            return false;
        }
        written += chunk;
    }
    return true;
}

static bool test_radiant_view_write_large_registry_table(FILE* file, int row_count) {
    if (!file || row_count <= 0) return false;
    if (fputs("<!doctype html><style>table{border-collapse:collapse}td{padding:2px}</style>"
              "<table><tbody>", file) < 0) {
        return false;
    }
    for (int row = 0; row < row_count; row++) {
        char cells[256];
        int count = snprintf(cells, sizeof(cells),
            "<tr><td>%d</td><td>Protocol registry row</td>"
            "<td>RFC %04d</td><td>stable table culling coverage</td></tr>",
            row, row);
        if (count <= 0 || count >= (int)sizeof(cells) || fputs(cells, file) < 0) {
            return false;
        }
    }
    return fputs("</tbody></table>", file) >= 0;
}

static void test_radiant_view_run_case(size_t index) {
    RadiantViewCaseResult* result = &g_radiant_view_results[index];
    const RadiantViewCase* view_case = &g_radiant_view_cases[index];
    memset(result, 0, sizeof(*result));
    result->exit_code = -1;

    if (!test_radiant_view_file_readable(view_case->path)) {
        result->missing_path = true;
        snprintf(result->message, sizeof(result->message),
                 "document is not readable: %s", view_case->path);
        result->executed = true;
        return;
    }
    if (view_case->event_path && !test_radiant_view_file_readable(view_case->event_path)) {
        result->missing_path = true;
        snprintf(result->message, sizeof(result->message),
                 "event file is not readable: %s", view_case->event_path);
        result->executed = true;
        return;
    }
    test_radiant_view_ensure_temp_dir();

    char log_path[256];
    int log_written = snprintf(log_path, sizeof(log_path),
        "./temp/test_radiant_view_%s.log", view_case->label);
    if (log_written <= 0 || log_written >= (int)sizeof(log_path)) {
        snprintf(result->message, sizeof(result->message),
                 "failed to build log path for %s", view_case->label);
        result->executed = true;
        return;
    }

    const char* args[10];
    int arg_count = 0;
    args[arg_count++] = "./lambda.exe";
    args[arg_count++] = "view";
    args[arg_count++] = view_case->path;
    if (view_case->event_path) {
        args[arg_count++] = "--event-file";
        args[arg_count++] = view_case->event_path;
    }
    args[arg_count++] = "--headless";
    args[arg_count++] = "--no-log";
    args[arg_count] = nullptr;

    const ShellEnvEntry memtrack_env[] = {
        {"VIEW_MEM_STAGES", "1"},
        {NULL, NULL},
    };
    ShellOptions options = {0};
    if (view_case->requires_clean_memtrack) options.env = memtrack_env;
    options.merge_stderr = true;
    // system() serialized worker-thread launches on macOS; direct argv spawning
    // preserves the parallel work queue and avoids shell quoting entirely.
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    result->exit_code = shell_result.exit_code;

    const char* output = shell_result.stdout_buf ? shell_result.stdout_buf : "";
    FILE* log_file = fopen(log_path, "wb");
    if (log_file) {
        fwrite(output, 1, shell_result.stdout_len, log_file);
        fclose(log_file);
    }

    result->has_layout_prof = strstr(output, "[LAYOUT_PROF]") != nullptr;
    result->has_render_prof = strstr(output, "[RENDER_PROF]") != nullptr;
    result->has_peak_footprint = strstr(output, "[PEAK_FOOTPRINT]") != nullptr;
    result->has_view_completed = strstr(output, "view command completed") != nullptr;
    result->has_clean_memtrack = !view_case->requires_clean_memtrack ||
        strstr(output, "[MEMTRACK_LIVE] bytes=0 count=0") != nullptr;
    if (result->exit_code != 0) {
        snprintf(result->message, sizeof(result->message),
                 "lambda view exited with code %d; see %s",
                 result->exit_code, log_path);
    } else if (result->has_layout_prof || result->has_render_prof ||
               result->has_peak_footprint || result->has_view_completed) {
        snprintf(result->message, sizeof(result->message),
                 "--no-log output leaked into %s", log_path);
    } else if (!result->has_clean_memtrack) {
        snprintf(result->message, sizeof(result->message),
                 "process retained allocations; see %s", log_path);
    } else {
        snprintf(result->message, sizeof(result->message), "ok");
    }
    shell_result_free(&shell_result);
    result->executed = true;
}

static void test_radiant_view_expect_case(size_t index) {
    if (index >= g_radiant_view_case_count) {
        FAIL() << "invalid radiant view case index";
    }
    if (!g_radiant_view_results[index].executed) {
        test_radiant_view_run_case(index);
    }
    const RadiantViewCase* view_case = &g_radiant_view_cases[index];
    const RadiantViewCaseResult* result = &g_radiant_view_results[index];

    EXPECT_FALSE(result->missing_path) << result->message;
    EXPECT_EQ(0, result->exit_code) << view_case->path << ": " << result->message;
    EXPECT_FALSE(result->has_layout_prof) << view_case->path;
    EXPECT_FALSE(result->has_render_prof) << view_case->path;
    EXPECT_FALSE(result->has_peak_footprint) << view_case->path;
    EXPECT_FALSE(result->has_view_completed) << view_case->path;
    EXPECT_TRUE(result->has_clean_memtrack) << view_case->path << ": " << result->message;
}

TEST(RadiantViewTest, LoadsPngAsHeadlessView) {
    test_radiant_view_expect_case(0);
}

TEST(RadiantViewTest, LoadsJpegAsHeadlessView) {
    test_radiant_view_expect_case(1);
}

TEST(RadiantViewTest, LoadsGifAsHeadlessView) {
    test_radiant_view_expect_case(2);
}

TEST(RadiantViewTest, LoadsSvgAsHeadlessView) {
    test_radiant_view_expect_case(3);
}

TEST(RadiantViewTest, LoadsHtmlAsHeadlessView) {
    test_radiant_view_expect_case(4);
}

TEST(RadiantViewTest, LoadsXmlAsHeadlessView) {
    test_radiant_view_expect_case(5);
}

TEST(RadiantViewTest, LoadsMarkdownAsHeadlessView) {
    test_radiant_view_expect_case(6);
}

TEST(RadiantViewTest, LoadsMarkdownMathAsHeadlessView) {
    test_radiant_view_expect_case(7);
}

TEST(RadiantViewTest, LoadsWikiAsHeadlessView) {
    test_radiant_view_expect_case(8);
}

TEST(RadiantViewTest, LoadsLatexShowcaseAsHeadlessView) {
    test_radiant_view_expect_case(9);
}

TEST(RadiantViewTest, LoadsMathIntensiveLatexAsHeadlessView) {
    test_radiant_view_expect_case(10);
}

TEST(RadiantViewTest, LoadsYamlAsHeadlessView) {
    test_radiant_view_expect_case(11);
}

TEST(RadiantViewTest, LoadsLambdaReportAsHeadlessView) {
    test_radiant_view_expect_case(12);
}

TEST(RadiantViewTest, LoadsLambdaChartDashboardAsHeadlessView) {
    test_radiant_view_expect_case(13);
}

TEST(RadiantViewTest, LoadsPdfAsHeadlessView) {
    test_radiant_view_expect_case(14);
}

TEST(RadiantViewTest, ReleasesDetachedScriptTextControl) {
    test_radiant_view_expect_case(15);
}

TEST(RadiantViewTest, PreservesDocumentUrlAcrossScriptFormSubmit) {
    test_radiant_view_expect_case(16);
}

TEST(RadiantViewTest, LaysOutDenseCollapsedTable) {
    test_radiant_view_expect_case(17);
}

TEST(RadiantViewTest, CullsLargeOffscreenRegistryTableWithoutRepeatedBoundsWalks) {
    const char* page = "./temp/test_radiant_view_large_registry_table.html";
    const char* view_log = "./temp/test_radiant_view_large_registry_table.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    bool page_written = test_radiant_view_write_large_registry_table(page_file, 3000);
    ASSERT_EQ(0, fclose(page_file));
    ASSERT_TRUE(page_written);

    const char* args[] = {"./lambda.exe", "view", page, "--headless", NULL};
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.merge_stderr = true;
    options.timeout_ms = 20000;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    EXPECT_FALSE(shell_result.timed_out);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "view command completed with result: 0"));
    shell_result_free(&shell_result);
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, InitialGeometryFlushDoesNotConsumeScriptCpuBudget) {
    const char* page = "./temp/test_radiant_view_initial_geometry_flush.html";
    const char* view_log = "./temp/test_radiant_view_initial_geometry_flush.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    bool page_written = test_radiant_view_write_large_registry_table(page_file, 5000);
    const char* geometry_read =
        "<script>document.addEventListener('DOMContentLoaded',function(){"
        "if(document.body.offsetTop<0)throw Error('invalid geometry');});</script>";
    ASSERT_EQ(strlen(geometry_read), fwrite(geometry_read, 1, strlen(geometry_read), page_file));
    ASSERT_EQ(0, fclose(page_file));
    ASSERT_TRUE(page_written);

    const char* args[] = {"./lambda.exe", "view", page, "--headless", NULL};
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {"LAMBDA_JS_EXEC_TIMEOUT_SECONDS", "6"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.merge_stderr = true;
    options.timeout_ms = 60000;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    EXPECT_FALSE(shell_result.timed_out);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: JS execution timed out"));
    shell_result_free(&shell_result);
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, LaysOutInlineFlexWithInlineSiblingAndAbsoluteChild) {
    const char* page = "./temp/test_radiant_view_inline_flex_absolute.html";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><style>"
        ".container{width:240px}.flex{display:inline-flex;border:1px solid #000}"
        ".absolute{position:absolute;top:0}</style>"
        "<div class=container><span class=flex><span>inline sibling</span>"
        "<span class=absolute>absolute child</span></span></div>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    remove(page);
}

TEST(RadiantViewTest, RestoresDocumentRealmAfterScriptException) {
    const char* page = "./temp/test_radiant_view_script_fault_recovery.html";
    const char* view_log = "./temp/test_radiant_view_script_fault_recovery.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "throw Error('intentional script fault recovery probe');</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "no js_input context"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "dom_set_document: could not restore the active JS Input"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, WindowScrollOnlyEmitsForPositionChanges) {
    const char* page = "./temp/test_radiant_view_window_scroll_events.html";
    const char* view_log = "./temp/test_radiant_view_window_scroll_events.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "var scroll_events = 0;"
        "window.addEventListener('scroll', function(){ scroll_events++; });"
        "window.scrollTo(0, 0);"
        "if (scroll_events !== 0) throw Error('no-op scroll dispatched an event');"
        "window.scrollTo({left: 0, top: 12});"
        "if (scroll_events !== 1) throw Error('position-changing scroll event count');"
        "window.scroll(0, 12);"
        "if (scroll_events !== 1) throw Error('aliased no-op scroll dispatched an event');"
        "</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, RestoresDocumentRealmBeforeAutofocusBehavior) {
    const char* page = "test/ui/js_autofocus_realm_recovery.html";
    const char* view_log = "./temp/test_radiant_view_autofocus_realm.log";
    test_radiant_view_ensure_temp_dir();
    ASSERT_TRUE(test_radiant_view_file_readable(page));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "no js_input context"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "mir cache: import"));
    remove(view_log);
}

TEST(RadiantViewTest, KeepsNativeExportsForAutomaticInterpreterPolicy) {
    const char* page = "test/ui/ce/editable-dom-blocks.html";
    const char* events = "test/ui/test_editing_contenteditable_blocks.json";
    const char* view_log = "./temp/test_radiant_view_native_imports.log";
    test_radiant_view_ensure_temp_dir();
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    // Exercise the automatic large-source branch without treating it as an
    // explicit interpreter request: imported package exports remain native.
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {"LAMBDA_JS_LARGE_INTERP_BYTES", "1"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, env, "--optimize=0");
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "failed to resolve native fn/pn"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "import of undefined item"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "mir: undefined variable"));
    remove(view_log);
}

TEST(RadiantViewTest, PreservesClassicVarBindingAcrossScriptTasks) {
    const char* page = "./temp/test_radiant_view_classic_var_binding.html";
    const char* view_log = "./temp/test_radiant_view_classic_var_binding.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>var bloom = { doLottie: function() {} };</script>"
        "<script>var bloom; bloom.doLottie();</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, PublishesInsertedNamedElementsWithoutRebindingDocument) {
    const char* page = "./temp/test_radiant_view_inserted_named_element.html";
    const char* view_log = "./temp/test_radiant_view_inserted_named_element.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><body><div id='staticNamed'></div>"
        "<script>"
        "if (window.staticNamed !== document.getElementById('staticNamed')) "
        "throw Error('static named element missing');"
        "var inserted = document.createElement('div');"
        "inserted.id = 'insertedNamed'; document.body.appendChild(inserted);"
        "</script>"
        "<script>"
        "if (window.insertedNamed !== document.getElementById('insertedNamed')) "
        "throw Error('inserted named element missing');"
        "</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, UsesPlaceholderForUnsupportedHttpImage) {
#ifdef _WIN32
    GTEST_SKIP() << "local HTTP fixture uses POSIX sockets";
#else
    RadiantViewImageServer server = {};
    ASSERT_TRUE(test_radiant_view_unsupported_image_server_start(&server));
    char page[128];
    ASSERT_GT(snprintf(page, sizeof(page), "http://127.0.0.1:%d/page.html", server.port), 0);
    char image_url[128];
    ASSERT_GT(snprintf(image_url, sizeof(image_url),
                       "http://127.0.0.1:%d/unsupported.avif", server.port), 0);
    char prime_url[128];
    ASSERT_GT(snprintf(prime_url, sizeof(prime_url),
                       "http://127.0.0.1:%d/prime.html", server.port), 0);
    const char* view_log = "./temp/test_radiant_view_unsupported_http_image.log";
    test_radiant_view_ensure_temp_dir();

    // Prime the same URL as a parser-blocking subresource so layout receives
    // an immediately ready resource instead of a timing-dependent transfer.
    const char* cache_args[] = {"./lambda.exe", "view", prime_url, "--headless", NULL};
    ShellOptions cache_options = {};
    cache_options.merge_stderr = true;
    ShellResult cache_result = shell_exec("./lambda.exe", cache_args, &cache_options);
    EXPECT_EQ(0, cache_result.exit_code)
        << (cache_result.stdout_buf ? cache_result.stdout_buf : "");
    shell_result_free(&cache_result);

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    int server_status = 0;
    ASSERT_EQ(server.pid, waitpid(server.pid, &server_status, 0));
    EXPECT_TRUE(WIFEXITED(server_status));
    EXPECT_EQ(0, WEXITSTATUS(server_status));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "image: unsupported HTTP image, using placeholder"));
    EXPECT_FALSE(test_radiant_view_file_contains(
        view_log, "Unsupported or unrecognized image format in memory buffer"));
    remove(view_log);
#endif
}

TEST(RadiantViewTest, ExposesNonConstructibleCssStyleDeclarationInterface) {
    const char* page = "./temp/test_radiant_view_css_style_declaration.html";
    const char* view_log = "./temp/test_radiant_view_css_style_declaration.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "if(typeof CSSStyleDeclaration!=='function')throw Error('missing CSSStyleDeclaration');"
        "try{new CSSStyleDeclaration();throw Error('CSSStyleDeclaration constructed')}"
        "catch(error){if(!(error instanceof TypeError))throw error}</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ExposesSupportedLinkRelationsThroughRelList) {
    const char* page = "./temp/test_radiant_view_link_rel_list.html";
    const char* view_log = "./temp/test_radiant_view_link_rel_list.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "var link=document.createElement('link');var rels=link.relList;"
        "if(!(rels instanceof DOMTokenList)||rels!==link.relList)"
        "throw Error('missing stable link relList');"
        "if(!rels.supports('prefetch')||!rels.supports('modulepreload')||"
        "rels.supports('not-a-link-relation'))throw Error('invalid rel support');"
        "rels.add('preload');if(!rels.contains('preload')||rels.length!==1)"
        "throw Error('relList add failed');"
        "rels.remove('preload');if(rels.length!==0)throw Error('relList remove failed');"
        "</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ExposesMediaElementInterfacesForMediaNodes) {
    const char* page = "./temp/test_radiant_view_media_interfaces.html";
    const char* view_log = "./temp/test_radiant_view_media_interfaces.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "var video=document.createElement('video');"
        "var audio=document.createElement('audio');"
        "if(!(video instanceof HTMLVideoElement)||!(video instanceof HTMLMediaElement)||"
        "!(audio instanceof HTMLAudioElement)||!(audio instanceof HTMLMediaElement))"
        "throw Error('media element interface mismatch');"
        "try{new HTMLVideoElement();throw Error('HTMLVideoElement constructed')}"
        "catch(error){if(!(error instanceof TypeError))throw error}</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ExposesScriptElementInterfaceForScriptNodes) {
    const char* page = "./temp/test_radiant_view_script_interface.html";
    const char* view_log = "./temp/test_radiant_view_script_interface.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "var script=document.createElement('script');"
        "if(!(script instanceof HTMLScriptElement)||!(script instanceof HTMLElement))"
        "throw Error('script element interface mismatch');"
        "try{new HTMLScriptElement();throw Error('HTMLScriptElement constructed')}"
        "catch(error){if(!(error instanceof TypeError))throw error}</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ReflectsInlineScriptSourceThroughScriptText) {
    const char* page = "./temp/test_radiant_view_script_text.html";
    const char* view_log = "./temp/test_radiant_view_script_text.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script type=\"framer/appear\" id=\"appearData\">{\"entry\":true}</script>"
        "<script>var data=window.appearData;"
        "if(data!==document.getElementById('appearData')||data.text!=='{\"entry\":true}')"
        "throw Error('script text getter mismatch');"
        "data.text='{\"entry\":false}';"
        "if(data.text!==data.textContent||data.text!=='{\"entry\":false}')"
        "throw Error('script text setter mismatch');</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, SchedulesIdleCallbackWithDeadline) {
    const char* page = "./temp/test_radiant_view_idle_callback.html";
    const char* view_log = "./temp/test_radiant_view_idle_callback.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><script>"
        "var idle=requestIdleCallback(function(deadline){"
        "if(deadline.didTimeout!==false||typeof deadline.timeRemaining!=='function'||"
        "deadline.timeRemaining()!==0)throw Error('invalid idle deadline');"
        "document.documentElement.setAttribute('data-idle-ran','yes');});"
        "if(idle===undefined||idle===null)throw Error('missing idle callback handle');</script>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ExposesBinaryFetchWithoutUnsupportedWorker) {
    const char* page = "./temp/test_radiant_view_binary_fetch.html";
    const char* payload = "./temp/test_radiant_view_binary_fetch.bin";
    const char* output = "./temp/test_radiant_view_binary_fetch.svg";
    const char* view_log = "./temp/test_radiant_view_binary_fetch.log";
    test_radiant_view_ensure_temp_dir();

    static const unsigned char response_bytes[] = {0x4c, 0x61, 0x6d, 0x62, 0x64, 0x61};
    FILE* payload_file = fopen(payload, "wb");
    ASSERT_NE(nullptr, payload_file);
    ASSERT_EQ(sizeof(response_bytes), fwrite(response_bytes, 1, sizeof(response_bytes), payload_file));
    ASSERT_EQ(0, fclose(payload_file));

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><body><script>"
        "if(typeof Worker!=='undefined')throw Error('unsupported Worker advertised');"
        "fetch('temp/test_radiant_view_binary_fetch.bin').then(function(response){"
        "return response.arrayBuffer();}).then(function(buffer){"
        "var bytes=new Uint8Array(buffer);"
        "if(!(buffer instanceof ArrayBuffer)||bytes.length!==6||bytes[0]!==76||"
        "bytes[5]!==97)throw Error('binary response mismatch');"
        "document.body.appendChild(document.createTextNode('response-binary-ready'));"
        "}).catch(function(error){throw error;});"
        "</script></body>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const char* args[] = {
        "./lambda.exe", "render", page, "-o", output, "--no-log", NULL,
    };
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(output, "response-binary-ready"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: post-dom exception"));

    remove(page);
    remove(payload);
    remove(output);
    remove(view_log);
}

#ifndef _WIN32
static void test_radiant_view_expect_cli_success(const char** arguments) {
    ShellOptions options = {}; options.merge_stderr = true; options.timeout_ms = 15000;
    ShellResult result = shell_exec("./lambda.exe", arguments, &options);
    EXPECT_FALSE(result.timed_out); EXPECT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
    shell_result_free(&result);
}
#endif

TEST(RadiantViewTest, CaptureAdmissionKeepsLocalSourcesAndDeniesColdAndWarmRemoteDependencies) {
#ifdef _WIN32
    GTEST_SKIP() << "local HTTP fixture uses POSIX sockets";
#else
    test_radiant_view_ensure_temp_dir();
    const char* page = "temp/test_capture_admission.html";
    const char* local_script = "temp/test_capture_local.js";
    const char* module_script = "temp/test_capture_import.mjs";
    const char* audit = "temp/test_capture_admission_requests.txt";
    remove(audit);
    const char* script = "document.getElementById('script-status').textContent='remote-script-ran';";
    const char* css = "@import url('/remote-import.css'); #style-status { width:137px }";
    const char* imported_css = "#import-status { width:149px }";
    const char* module = "export const label='remote-module-ran';";
    const char* child_script = "document.body.setAttribute('data-admission','child-script-ran');";
    const char* svg = "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'><rect width='20' height='20' fill='blue'/></svg>";
    size_t font_length = 0;
    char* font = read_binary_file("test/layout/data/font/Ahem.ttf", &font_length); ASSERT_NE(font, nullptr);
    const RadiantViewHttpResource resources[] = {
        {"GET /remote-script.js ", "text/javascript", script, strlen(script)},
        {"GET /remote.css ", "text/css", css, strlen(css)},
        {"GET /remote-import.css ", "text/css", imported_css, strlen(imported_css)},
        {"GET /remote-module.mjs ", "text/javascript", module, strlen(module)},
        {"GET /remote-image.svg ", "image/svg+xml", svg, strlen(svg)},
        {"GET /remote-font.ttf ", "font/ttf", font, font_length},
        {"GET /remote-fetch.txt ", "text/plain", "remote-fetch-ran", 16},
        {"GET /remote-child.js ", "text/javascript", child_script, strlen(child_script)},
        {"GET /remote-wrapper.css ", "text/css", css, strlen(css)},
    };
    RadiantViewImageServer server = {};
    ASSERT_TRUE(test_radiant_view_unsupported_image_server_start(&server, resources,
        sizeof(resources) / sizeof(resources[0]), audit));
    // the child owns a forked resource snapshot; cleanup also runs after fatal assertions.
    struct ServerScope {
        pid_t pid; char* font;
        ~ServerScope() { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); free(font); }
    } server_scope = {server.pid, font};
    char document[4096], importing[512];
    ASSERT_GT(snprintf(document, sizeof(document), "<!doctype html><head>"
        "<link rel='stylesheet' href='http://127.0.0.1:%d/remote.css'>"
        "<style>@page {size:200px 140px; margin:10px} p{margin:0;font-size:10px;line-height:12px} "
        "p{width:123px} "
        "@font-face{font-family:CaptureFont;src:url('http://127.0.0.1:%d/remote-font.ttf')} "
        "#font-status{font-family:CaptureFont,serif} #image-status{width:20px;height:20px;background:red "
        "url('http://127.0.0.1:%d/remote-image.svg')} </style></head><body>"
        "<p id='script-status'>pending-script</p><p id='style-status'>style</p><p id='import-status'>import</p>"
        "<p id='module-status'>module-pending</p><p id='inline-status'>inline-pending</p>"
        "<p id='fetch-status'>fetch-pending</p><p id='font-status'>AAA</p><div id='image-status'></div>"
        "<img width='20' height='20' src='http://127.0.0.1:%d/remote-image.svg'>"
        "<script src='test_capture_local.js'></script><script src='http://127.0.0.1:%d/remote-script.js'></script>"
        "<script type='module' src='test_capture_import.mjs'></script><script>"
        "document.getElementById('inline-status').textContent='inline-script-ran';"
        "var style=document.getElementById('style-status'), imported=document.getElementById('import-status');"
        "style.textContent='style-width-'+getComputedStyle(style).width;"
        "imported.textContent='import-width-'+getComputedStyle(imported).width;"
        "fetch('http://127.0.0.1:%d/remote-fetch.txt').then(function(r){return r.text();})"
        ".then(function(t){document.getElementById('fetch-status').textContent=t;})"
        ".catch(function(){document.getElementById('fetch-status').textContent='fetch-denied';});"
        "</script><iframe srcdoc='&lt;p>child-local&lt;/p>&lt;script src=&quot;http://127.0.0.1:%d/remote-child.js&quot;>&lt;/script>'></iframe></body>",
        server.port, server.port, server.port, server.port, server.port, server.port, server.port), 0);
    ASSERT_GT(snprintf(importing, sizeof(importing), "import {label} from 'http://127.0.0.1:%d/remote-module.mjs';"
        "document.getElementById('module-status').textContent=label;", server.port), 0);
    const char* local = "document.getElementById('script-status').textContent='local-script-ran';";
    ASSERT_EQ(write_binary_file(page, document, strlen(document)), 0);
    ASSERT_EQ(write_binary_file(local_script, local, strlen(local)), 0);
    ASSERT_EQ(write_binary_file(module_script, importing, strlen(importing)), 0);
    const bool policies[] = {true, false, true, false};
    for (size_t job = 0; job < sizeof(policies) / sizeof(policies[0]); job++) {
        bool blocked = policies[job];
        char output[128]; snprintf(output, sizeof(output), "temp/test_capture_admission_%zu.json", job);
        SCOPED_TRACE(blocked ? "deny cold/warm dependencies" : "ordinary document admission");
        if (blocked) remove(audit);
        const char* arguments[] = {"./lambda.exe", "layout", page, "--auto-close", "--post-load-settle-ms", "200",
            "--view-output", output, "--no-log", blocked ? "--block-remote-resources" : nullptr, nullptr};
        test_radiant_view_expect_cli_success(arguments);
        EXPECT_TRUE(test_radiant_view_file_contains(output, "inline-script-ran"));
        EXPECT_TRUE(test_radiant_view_file_contains(output, blocked ? "local-script-ran" : "remote-script-ran"));
        EXPECT_TRUE(test_radiant_view_file_contains(output, blocked ? "style-width-123px" : "style-width-137px"));
        EXPECT_TRUE(test_radiant_view_file_contains(output, blocked ? "import-width-123px" : "import-width-149px"));
        EXPECT_EQ(test_radiant_view_file_contains(output, "remote-module-ran"), !blocked);
        if (blocked) {
            EXPECT_TRUE(test_radiant_view_file_contains(output, "fetch-denied"));
            EXPECT_EQ(access(audit, F_OK), -1) << "denied dependencies reached the HTTP fixture";
        } else {
            EXPECT_TRUE(test_radiant_view_file_contains(output, "remote-fetch-ran"));
            if (job == 1) {
                // the final admitted run may reuse already acquired source bytes.
                for (const RadiantViewHttpResource& resource : resources) {
                    if (strstr(resource.request, "/remote-wrapper.css")) continue;
                    EXPECT_TRUE(test_radiant_view_file_contains(audit, resource.request)) << resource.request;
                }
            }
        }
    }
    // exercise paint-time backgrounds separately from the pending paged replaced producer.
    const char* paged_page = "temp/test_capture_paged_admission.html";
    char* replaced = strstr(document, "<img "); ASSERT_NE(replaced, nullptr);
    char* replaced_end = strchr(replaced, '>'); ASSERT_NE(replaced_end, nullptr);
    memmove(replaced, replaced_end + 1, strlen(replaced_end + 1) + 1);
    char* frame = strstr(document, "<iframe "); ASSERT_NE(frame, nullptr);
    char* frame_end = strstr(frame, "</iframe>"); ASSERT_NE(frame_end, nullptr);
    memmove(frame, frame_end + strlen("</iframe>"), strlen(frame_end + strlen("</iframe>")) + 1);
    ASSERT_EQ(write_binary_file(paged_page, document, strlen(document)), 0);
    const char* wrapped = "temp/test_capture_admission.svg";
    char svg_document[512];
    snprintf(svg_document, sizeof(svg_document), "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
        "<style>@import url('http://127.0.0.1:%d/remote-wrapper.css');</style><rect width='20' height='20' fill='red'/></svg>", server.port);
    ASSERT_EQ(write_binary_file(wrapped, svg_document, strlen(svg_document)), 0);
    const bool wrapper_policies[] = {true, false, true};
    for (bool blocked : wrapper_policies) {
        remove(audit);
        const char* arguments[] = {"./lambda.exe", "layout", wrapped, "--auto-close", "--no-log",
            blocked ? "--block-remote-resources" : nullptr, nullptr};
        test_radiant_view_expect_cli_success(arguments);
        if (blocked) EXPECT_EQ(access(audit, F_OK), -1);
        else EXPECT_TRUE(test_radiant_view_file_contains(audit, "GET /remote-wrapper.css "));
    }
    const char* formats[] = {"pdf", "png"};
    const bool export_policies[] = {true, false};
    for (bool blocked : export_policies) {
        SCOPED_TRACE(blocked ? "paged local dependencies" : "paged admitted dependencies");
        if (blocked) remove(audit);
        for (const char* format : formats) {
            char exported[160]; snprintf(exported, sizeof(exported),
                "temp/test_capture_admission_%s.%s", blocked ? "blocked" : "allowed", format);
            const char* arguments[16] = {"./lambda.exe", "render", paged_page, "--paged",
                "--page-padding", "0", "-o", exported, "--no-log"};
            size_t argument_count = 9;
            if (strcmp(format, "png") == 0) {
                arguments[argument_count++] = "--thumbnail-page"; arguments[argument_count++] = "1";
            }
            if (blocked) arguments[argument_count++] = "--block-remote-resources";
            arguments[argument_count] = nullptr;
            test_radiant_view_expect_cli_success(arguments); EXPECT_TRUE(test_radiant_view_file_readable(exported));
        }
        if (blocked) EXPECT_EQ(access(audit, F_OK), -1) << "paged denied dependencies reached the HTTP fixture";
    }
#endif
}

TEST(RadiantViewTest, ExplicitLayoutTimerWindowOverridesTheEnvironmentIncludingZero) {
    test_radiant_view_ensure_temp_dir();
    const char* page = "temp/test_layout_timer_window.html";
    const char* output = "temp/test_layout_timer_window.json";
    const char* document = "<!doctype html><body><p id='status'>capture-now-ready</p><p id='clock'></p><p id='zero'>zero-pending</p><script>"
        "document.getElementById('clock').textContent='capture-clock-'+performance.now();"
        "setTimeout(function(){setTimeout(function(){document.getElementById('zero').textContent='zero-nested-'+performance.now();},0);},0);"
        "setTimeout(function(){document.getElementById('status').textContent='timer-fired';},100);</script></body>";
    FILE* file = fopen(page, "wb"); ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(document, 1, strlen(document), file), strlen(document)); ASSERT_EQ(fclose(file), 0);
    const ShellEnvEntry environment[] = {{"LAMBDA_POST_LOAD_SETTLE_MS", "200"}, {nullptr, nullptr}};
    const char* windows[] = {"0", "50", nullptr};
    for (const char* window : windows) {
        SCOPED_TRACE(window ? window : "environment fallback"); remove(output);
        const char* arguments[] = {"./lambda.exe", "layout", page, "--auto-close", "--view-output", output,
            "--no-log", window ? "--post-load-settle-ms" : nullptr, window, nullptr};
        ShellOptions options = {}; options.env = environment; options.merge_stderr = true; options.timeout_ms = 10000;
        ShellResult result = shell_exec("./lambda.exe", arguments, &options);
        EXPECT_FALSE(result.timed_out); EXPECT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
        shell_result_free(&result);
        EXPECT_TRUE(test_radiant_view_file_contains(output, window ? "capture-now-ready" : "timer-fired"));
        EXPECT_FALSE(test_radiant_view_file_contains(output, window ? "timer-fired" : "capture-now-ready"));
        EXPECT_TRUE(test_radiant_view_file_contains(output, "capture-clock-0"));
        EXPECT_TRUE(test_radiant_view_file_contains(output, "zero-nested-0"));
        EXPECT_FALSE(test_radiant_view_file_contains(output, "zero-pending"));
    }
    remove(page); remove(output);
}

TEST(RadiantViewTest, StaticHeadlessViewClosesRecursivePostLoadTimers) {
    const char* page = "./temp/test_radiant_view_recursive_timer.html";
    const char* view_log = "./temp/test_radiant_view_recursive_timer.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><body>first-render-ready<script>"
        "(function tick(){setTimeout(tick,0);})();"
        "</script></body>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const char* args[] = {"./lambda.exe", "view", page, "--headless", NULL};
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "INFO"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 10000;
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    EXPECT_FALSE(shell_result.timed_out);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(view_log,
        "execute_document_scripts: timer queue drained"));

    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, TetrisClosesAfterSustainedReactiveRedraws) {
    test_radiant_view_ensure_temp_dir();
    const char* result_path = "./temp/test_radiant_view_tetris_close.json";
    remove(result_path);
    const char* args[] = {
        "./lambda.exe", "view", "test/demo/tetris/tetris.ls", "--headless", "--no-log",
        "--event-file", "test/demo/tetris/tetris_close.json",
        "--event-result", result_path, nullptr,
    };
    ShellOptions options = {};
    // bound a shutdown hang; the replay advances gameplay on the virtual clock.
    options.timeout_ms = 30000;
    options.merge_stderr = true;
    ShellResult result = shell_exec("./lambda.exe", args, &options);
    EXPECT_FALSE(result.timed_out);
    EXPECT_EQ(0, result.exit_code) << (result.stdout_buf ? result.stdout_buf : "");
    shell_result_free(&result);
    EXPECT_TRUE(test_radiant_view_file_contains(result_path, "\"result\":\"PASS\""));
    remove(result_path);
}

TEST(RadiantViewTest, RendersObjectBoundingBoxPatternWithoutUserUnitTiling) {
    const char* page = "./temp/test_radiant_view_object_bounding_box_pattern.html";
    const char* view_log = "./temp/test_radiant_view_object_bounding_box_pattern.log";
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* document =
        "<!doctype html><svg width=200 height=100 viewBox='0 0 200 100'>"
        "<defs><pattern id=grid width=.1 height=.1>"
        "<rect width=1 height=1 fill='#246'/></pattern></defs>"
        "<rect width=200 height=100 fill='url(#grid)'/></svg>";
    ASSERT_EQ(strlen(document), fwrite(document, 1, strlen(document), page_file));
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_FALSE(shell_result.timed_out);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(view_log,
        "view command completed with result: 0"));
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, ReportsNestedFlexIntrinsicMeasurements) {
    const char* page = "test/layout/data/baseline/flex_019_nested_flex.html";
    const char* view_log = "./temp/test_radiant_view_intrinsic_cache_log.txt";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    const ShellEnvEntry env[] = {
        {"LAYOUT_PROFILE", "1"},
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    EXPECT_TRUE(test_radiant_view_profile_has_intrinsic_measurement(view_log));
}

TEST(RadiantViewTest, ArchivesAndDirectoriesOpenAsFileTrees) {
    const char* paths[] = {"test/input/zip", "test/input/zip/wide.zip",
        "test/input/zip/viewer.zip", "test/input/zip/empty.zip", "test/input/zip/renamed.dat", "test/input/zip/extensionless",
        "test/input/zip/office.docx", "test/input/zip/sample.jar"};
    const char* view_log = "./temp/test_radiant_view_archive_tree.log";
    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log}, {"LAMBDA_LOG_LEVEL", "INFO"}, {NULL, NULL},
    };
    test_radiant_view_ensure_temp_dir();
    for (const char* path : paths) {
        SCOPED_TRACE(path);
        remove(view_log);
        ShellResult result = test_radiant_view_run_logged_headless(path, nullptr, env);
        EXPECT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
        EXPECT_TRUE(test_radiant_view_file_contains(view_log, "VIEW_FILE_TREE:"));
        shell_result_free(&result);
    }
    remove(view_log);
}

TEST(RadiantViewTest, RejectsUnsafeArchiveBeforeOpeningTree) {
    ASSERT_TRUE(test_radiant_view_file_readable("./lambda.exe"));
    ShellResult result = test_radiant_view_run_logged_headless(
        "test/input/zip/traversal.zip", nullptr, nullptr);
    EXPECT_NE(result.exit_code, 0);
    shell_result_free(&result);
}

TEST(RadiantViewTest, ReusesCleanRowsAfterDirectoryClose) {
    const char* page = "lmd/package/doc/doc_viewer.ls";
    const char* events = "test/ui/doc_viewer_layout_shift.json";
    const char* view_log = "./temp/test_radiant_view_directory_close.log";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));
    test_radiant_view_ensure_temp_dir();
    remove(view_log);

    const ShellEnvEntry env[] = {
        {"LAYOUT_PROFILE", "1"},
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, events, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    unsigned long long initial_reuse = 0;
    unsigned long long close_reuse = 0;
    ASSERT_TRUE(test_radiant_view_profile_shifted_reuse_at(view_log, 0, &initial_reuse));
    // Two setup layouts and two opens precede the close relayout in this fixture.
    ASSERT_TRUE(test_radiant_view_profile_shifted_reuse_at(view_log, 4, &close_reuse));
    EXPECT_EQ(0ULL, initial_reuse);
    EXPECT_GT(close_reuse, 0ULL);
    remove(view_log);
}

TEST(RadiantViewTest, SkipsGlobalCascadeForLoadTimeInlineStyleWrites) {
    const char* page = "test/ui/js_load_inline_style_no_recascade.html";
    const char* timing_path = "./temp/test_radiant_view_inline_style_timing.jsonl";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    ShellResult shell_result = test_radiant_view_run_layout_timing(page, timing_path);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_full_recascade\":false"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_incremental_recascade\":false"));
}

TEST(RadiantViewTest, ReCascadesOnlyMutatedSubtreeForLoadTimeClassWrite) {
    const char* page = "test/ui/js_load_class_mutation_subtree_recascade.html";
    const char* timing_path = "./temp/test_radiant_view_class_mutation_timing.jsonl";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    ShellResult shell_result = test_radiant_view_run_layout_timing(page, timing_path);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_incremental_recascade\":true"));
}

TEST(RadiantViewTest, SkipsRecascadeForNoOpLoadTimeClassRemovals) {
    const char* page = "./temp/test_radiant_view_noop_class_removals.html";
    const char* timing_path = "./temp/test_radiant_view_noop_class_removals_timing.jsonl";
    test_radiant_view_ensure_temp_dir();
    remove(timing_path);

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    const char* prefix = "<!doctype html><body>";
    ASSERT_EQ(strlen(prefix), fwrite(prefix, 1, strlen(prefix), page_file));
    for (int index = 0; index < 65; index++) {
        ASSERT_EQ(5u, fwrite("<div>", 1, 5, page_file));
    }
    const char* suffix =
        "<script>"
        "var rows = document.querySelectorAll('div');"
        "for (var i = 0; i < rows.length; i++) rows[i].classList.remove('absent');"
        "</script>";
    ASSERT_EQ(strlen(suffix), fwrite(suffix, 1, strlen(suffix), page_file));
    ASSERT_EQ(0, fclose(page_file));

    ShellResult shell_result = test_radiant_view_run_layout_timing(page, timing_path);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_mutation_count\":0"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_mutation_overflow\":false"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_full_recascade\":false"));
    remove(page);
    remove(timing_path);
}

TEST(RadiantViewTest, PreservesMarkerPropsDuringRetainedTableReflow) {
    test_radiant_view_expect_case(18);
}

TEST(RadiantViewTest, ReleasesForeignDocumentRegistryBeyondInitialCapacity) {
    test_radiant_view_expect_case(19);
}

TEST(RadiantViewTest, ReleasesFailedFontFallbackHandles) {
    test_radiant_view_expect_case(20);
}

TEST(RadiantViewTest, ExposesCurrentScriptDuringClassicExecution) {
    const char* page = "test/html/dom_document_current_script.html";
    const char* output = "./temp/test_radiant_current_script.svg";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    const char* args[] = {
        "./lambda.exe", "render", page, "-o", output, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    // The script turns this visible only after it verifies its own DOM identity.
    EXPECT_TRUE(test_radiant_view_file_contains(output, "current-script-classic"));
}

TEST(RadiantViewTest, PromotesCachedPngDecodeFromThumbnailToFullSize) {
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/image_cache_promotion.html"));
    test_radiant_view_ensure_temp_dir();

    // Route this run's file logging to a private path via LAMBDA_LOG_FILE. The
    // default log.txt is shared and truncated on every lambda startup, so under
    // parallel test runs concurrent processes clobber it — making assertions on
    // log.txt flaky. A per-test log file is isolated.
    const char* view_log = "./temp/test_radiant_view_cache_promotion_log.txt";
    const ShellEnvEntry env[] = {
        {"LAMBDA_IMAGE_DECODE_TRACE", "1"},
        {"LAMBDA_LOG_FILE", view_log},
        {NULL, NULL},
    };
    // Per-child environment keeps this diagnostic isolated from parallel workers.
    ShellResult shell_result = test_radiant_view_run_logged_headless(
        "test/html/image_cache_promotion.html", nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    // The window releases its runtime before CLI teardown records this result.
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "view command completed with result: 0"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log,
        "[image] Decoded local image on demand: 64x42 (intrinsic 640x427, target 60x40)"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log,
        "[image] Decoded local image on demand: 640x427 (intrinsic 640x427, target 640x427)"));
}

TEST(RadiantViewTest, ReportsViewCompletionAtNoticeLevel) {
    const char* page = "test/layout/data/page/sample1.html";
    const char* view_log = "./temp/test_radiant_view_notice_completion_log.txt";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "view command completed with result: 0"));
}

TEST(RadiantViewTest, InitializesScriptScreenFromHostViewport) {
    const char* page = "test/html/js_screen_viewport_preamble.html";
    const char* events = "test/html/js_screen_viewport_preamble_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, DefersPrecommitGeometryReadInHostDrivenView) {
    const char* page = "test/html/js_precommit_geometry_snapshot.html";
    const char* events = "test/html/js_precommit_geometry_snapshot_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, FlushesLoadGeometryInHostDrivenView) {
    const char* page = "test/html/js_load_geometry_snapshot.html";
    const char* events = "test/html/js_load_geometry_snapshot_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, BalancesFragmentedMulticolBlocksAtTheirUsedWidth) {
    const char* page = "test/html/js_multicol_balanced_continuation.html";
    const char* events = "test/html/js_multicol_balanced_continuation_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, RecoversTimedOutLoadScriptWithoutUnsafeBatchReset) {
    const char* page = "test/ui/js_load_watchdog_recovery.html";
    ASSERT_TRUE(test_radiant_view_file_readable(page));

    const ShellEnvEntry env[] = {
        {"LAMBDA_JS_EXEC_TIMEOUT_SECONDS", "1"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_NE(nullptr, shell_result.stdout_buf);
    EXPECT_NE(nullptr, strstr(shell_result.stdout_buf,
                              "execute_document_scripts: JS execution timed out"));
    EXPECT_NE(nullptr, strstr(shell_result.stdout_buf,
                              "view command completed with result: 0"));
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, ContinuesLayoutBatchAfterTimedOutLoadScript) {
    const char* timed_out_page = "test/ui/js_load_watchdog_recovery.html";
    const char* following_page = "test/layout/data/page/sample1.html";
    ASSERT_TRUE(test_radiant_view_file_readable(timed_out_page));
    ASSERT_TRUE(test_radiant_view_file_readable(following_page));

    const char* args[] = {
        "./lambda.exe", "layout", timed_out_page, following_page,
        "--stream-layout-results", "--continue-on-error", "--auto-close", "--no-log", NULL,
    };
    const ShellEnvEntry env[] = {
        {"LAMBDA_JS_EXEC_TIMEOUT_SECONDS", "1"},
        {NULL, NULL},
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    options.timeout_ms = 10000;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    EXPECT_FALSE(shell_result.timed_out);
    EXPECT_EQ(0, shell_result.exit_code)
        << "the watchdog must not terminate the batch before its next document";
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, ScalesWatchdogForDenseProductionBundles) {
    EXPECT_EQ(RADIANT_SCRIPT_EXEC_TIMEOUT_BASE_SECONDS,
              radiant_script_exec_timeout_source_seconds(8192));
    EXPECT_EQ(58, radiant_script_exec_timeout_source_seconds(49152));
    EXPECT_EQ(RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS,
              radiant_script_exec_timeout_source_seconds(114688));
}

TEST(RadiantViewTest, SkipsDefaultCumulativeBrowserScriptBudget) {
    const char* page = "./temp/test_radiant_view_script_budget.html";
    const char* view_log = "./temp/test_radiant_view_script_budget.log";
    // Exceed the static snapshot admission limit without an environment
    // override, so page bundles cannot starve parallel smoke renders.
    const size_t script_bytes = 768u * 1024u;
    const int script_count = 1;
    test_radiant_view_ensure_temp_dir();

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    bool page_written = fputs("<!doctype html><html><head></head><body>", page_file) >= 0;
    for (int i = 0; i < script_count && page_written; i++) {
        page_written = fputs("<script>", page_file) >= 0 &&
            test_radiant_view_write_script_bytes(page_file, script_bytes) &&
            fputs("</script>", page_file) >= 0;
    }
    page_written = page_written && fputs("script budget</body></html>", page_file) >= 0;
    ASSERT_EQ(0, fclose(page_file));
    ASSERT_TRUE(page_written);

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "INFO"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "skipping document JS after browser source budget"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "view command completed with result: 0"));
    shell_result_free(&shell_result);
    remove(page);
    remove(view_log);
}

TEST(RadiantViewTest, SkipsExternalScriptResponseThatIsHtml) {
    const char* page = "./temp/test_radiant_view_html_script_response.html";
    const char* script = "./temp/test_radiant_view_html_script_response.js";
    const char* events = "./temp/test_radiant_view_html_script_response_events.json";
    const char* view_log = "./temp/test_radiant_view_html_script_response.log";
    test_radiant_view_ensure_temp_dir();

    FILE* script_file = fopen(script, "wb");
    ASSERT_NE(nullptr, script_file);
    ASSERT_GE(fputs("<!doctype html><html><body>redirected response</body></html>",
                    script_file), 0);
    ASSERT_EQ(0, fclose(script_file));

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    ASSERT_GE(fputs("<!doctype html><html><head><script src=\"test_radiant_view_html_script_response.js\"></script></head><body>script response</body></html>",
                    page_file), 0);
    ASSERT_EQ(0, fclose(page_file));

    FILE* events_file = fopen(events, "wb");
    ASSERT_NE(nullptr, events_file);
    ASSERT_GE(fputs("{\"events\":[]}", events_file), 0);
    ASSERT_EQ(0, fclose(events_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "INFO"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, events, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "skipping HTML response for external script"));
    EXPECT_FALSE(test_radiant_view_file_contains(view_log, "js-mir: parse failed"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "view command completed with result: 0"));
    shell_result_free(&shell_result);
    remove(page);
    remove(script);
    remove(events);
    remove(view_log);
}

TEST(RadiantViewTest, ExecutesExternalDependencyInStaticHeadlessView) {
    const char* page = "./temp/test_radiant_view_headless_external.html";
    const char* script = "./temp/test_radiant_view_headless_external.js";
    const char* view_log = "./temp/test_radiant_view_headless_external.log";
    test_radiant_view_ensure_temp_dir();

    FILE* script_file = fopen(script, "wb");
    ASSERT_NE(nullptr, script_file);
    ASSERT_GE(fputs("window.headlessExternalDependency = 'ready';", script_file), 0);
    ASSERT_EQ(0, fclose(script_file));

    FILE* page_file = fopen(page, "wb");
    ASSERT_NE(nullptr, page_file);
    ASSERT_GE(fputs("<!doctype html><html><head><script src=\"test_radiant_view_headless_external.js\"></script><script>if (window.headlessExternalDependency !== 'ready') { throw new Error('headless external dependency unavailable'); }</script></head><body>headless external dependency</body></html>", page_file), 0);
    ASSERT_EQ(0, fclose(page_file));

    const ShellEnvEntry env[] = {
        {"LAMBDA_LOG_FILE", view_log},
        {"LAMBDA_LOG_LEVEL", "INFO"},
        {NULL, NULL},
    };
    ShellResult shell_result = test_radiant_view_run_logged_headless(page, nullptr, env);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log, "script_runner: external scripts: 1 loaded, 0 failed"));
    EXPECT_FALSE(test_radiant_view_file_contains(
        view_log, "headless external dependency unavailable"));
    shell_result_free(&shell_result);
    remove(page);
    remove(script);
    remove(view_log);
}

TEST(RadiantViewTest, ExecutesUmdBrowserGlobalWithoutImplicitAmdLoader) {
    const char* page = "test/ui/js_load_commonjs_umd_global.html";
    const char* events = "test/ui/js_load_commonjs_umd_global.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));

    ShellResult shell_result = test_radiant_view_run_logged_headless(
        page, events, nullptr);
    EXPECT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
}

TEST(RadiantViewTest, DefersCrossOriginIframeNavigationOutsideLayout) {
    const char* page = "test/html/iframe_cross_origin_deferred.html";
    const char* output = "./temp/test_radiant_view_cross_origin_iframe.svg";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    test_radiant_view_ensure_temp_dir();

    const char* args[] = {
        "./lambda.exe", "render", page, "-o", output, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    // A remote frame must not prevent the parent document's first render.
    EXPECT_TRUE(test_radiant_view_file_contains(
        output, "The parent document must render before a remote frame navigates."));
}

TEST(RadiantViewTest, RapidLocalIframeNavigationCommitsLatestScriptPage) {
    const char* page = "test/html/async_navigation_parent.html";
    const char* events = "test/html/async_navigation_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));
    test_radiant_view_ensure_temp_dir();

    const ShellEnvEntry env[] = {
        {"VIEW_MEM_STAGES", "1"},
        {NULL, NULL},
    };
    ShellResult result = test_radiant_view_run_logged_headless(page, events, env);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_EQ(0, result.exit_code) << output;
    EXPECT_NE(nullptr, strstr(output, "[MEMTRACK_LIVE] bytes=0 count=0"));
    shell_result_free(&result);
}

TEST(RadiantViewTest, LambdaLoadedDocumentsReleaseNativeStorageAtExit) {
    const char* cases[] = {
        "test/lambda/dom_stylesheet.ls",
        "test/lambda/proc/dom_stylesheet_declaration.ls",
        "test/lambda/proc/dom_document_ownership.ls",
        "test/lambda/proc/dom_css_rule_interface_projection.ls",
        "test/lambda/proc/dom_css_rule_mixed_projection.ls",
    };
    const ShellEnvEntry env[] = {
        {"VIEW_MEM_STAGES", "1"},
        {"MEMTRACK_MODE", "DEBUG"},
        {"LAMBDA_GC_FORCE_EVERY", "1"},
        {"LAMBDA_GC_POISON_FREED", "1"},
        {NULL, NULL},
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    for (const char* path : cases) {
        SCOPED_TRACE(path);
        ASSERT_TRUE(test_radiant_view_file_readable(path));
        const char* args[5] = {"./lambda.exe", "run", path, "--no-log", NULL};
        // the existing functional fixture has no procedural entry point.
        if (strcmp(path, cases[0]) == 0) {
            args[1] = path;
            args[2] = "--no-log";
            args[3] = NULL;
        }
        ShellResult result = shell_exec("./lambda.exe", args, &options);
        const char* output = result.stdout_buf ? result.stdout_buf : "";
        EXPECT_EQ(result.exit_code, 0) << output;
        EXPECT_NE(strstr(output, "[MEMTRACK_LIVE] bytes=0 count=0"), nullptr) << output;
        shell_result_free(&result);
    }
}

TEST(RadiantViewTest, LatexIframeNavigationAcceptsParentClickWithoutScroll) {
    const char* page = "test/html/index.html";
    const char* events = "test/html/latex_navigation_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));
    test_radiant_view_ensure_temp_dir();

    ShellResult result = test_radiant_view_run_logged_headless(page, events, nullptr);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_EQ(0, result.exit_code) << output;
    shell_result_free(&result);
}

TEST(RadiantViewTest, ParentLinksKeepTheirEvaluatorAfterIframeReplacement) {
    const char* page = "test/html/index.html";
    const char* events = "test/html/iframe_evaluator_owner_events.json";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(events));
    test_radiant_view_ensure_temp_dir();

    ShellResult result = test_radiant_view_run_logged_headless(page, events, nullptr);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_EQ(0, result.exit_code) << output;
    shell_result_free(&result);
}

TEST(RadiantViewTest, KeepsModuleCodeAliveAcrossBrowserTaskSync) {
    const char* page = "test/html/js_module_task_lifetime.html";
    const char* output = "./temp/test_radiant_module_task_lifetime.svg";
    ASSERT_TRUE(test_radiant_view_file_readable(page));
    ASSERT_TRUE(test_radiant_view_file_readable(
        "test/html/js_module_task_lifetime_source.js"));
    ASSERT_TRUE(test_radiant_view_file_readable(
        "test/html/js_module_task_lifetime_consumer.js"));
    test_radiant_view_ensure_temp_dir();

    const char* args[] = {
        "./lambda.exe", "render", page, "-o", output, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    // The second module imports a callable exported before browser-global sync.
    EXPECT_TRUE(test_radiant_view_file_contains(output, "module-code-alive"));
}

TEST(RadiantViewTest, AstDocumentExecutionKeepsFreshDocumentRealms) {
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/js_cache_realm_mutate.html"));
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/js_cache_realm_verify.html"));
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/js_cache_external_classic.js"));
    test_radiant_view_ensure_temp_dir();

    const char* output_dir = "./temp/test_js_ast_realm";
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif

    const char* timing_path = "./temp/test_js_ast_realm/timing.jsonl";
    const char* result_path =
        "./temp/test_js_ast_realm/html__js_cache_realm_verify.json";
    const char* args[] = {
        "./lambda.exe", "layout",
        "test/html/js_cache_realm_mutate.html",
        "test/html/js_cache_realm_verify.html",
        "--output-dir", output_dir,
        "--timing-output", timing_path,
        NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    // D4.1.1v2: descriptor type changes must not move a runtime Map payload
    // from the GC data zone back through the pool allocator on later appends.
    EXPECT_EQ(nullptr, shell_result.stdout_buf
        ? strstr(shell_result.stdout_buf, "pool_free: pointer") : nullptr);
    shell_result_free(&shell_result);

    // Each AST document realm keeps globals, prototypes, declarations,
    // lifecycle tasks, and DOM bindings document-local.
    EXPECT_TRUE(test_radiant_view_file_contains(
        result_path, "js-cache-realm-isolated|dom|load"));
    EXPECT_FALSE(test_radiant_view_file_contains(
        result_path, "js-cache-realm-leaked"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_cache_lookups\":0"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_cache_compiles\":0"));
    // Local sources do not enter the network batch, but every timing record
    // must expose the transport split used by remote documents.
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_source_prefetch_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_source_wait_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_source_read_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_recascade_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_handler_install_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"load_post_script_mutation_kind_mask\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"first_render_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"cleanup_ms\":"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"document_context_live_bytes\":"));
}

TEST(RadiantViewTest, UiScriptContentSurvivesForcedGc) {
    // D4.5.2: a UI-mode script result lives in the document's untraced arena.
    // A runtime symbol, a non-spreadable array or a group element left there
    // as a GC pointer is collected, and its memory reused, before the DOM build
    // reads it; forced, poisoning collection makes that deterministic.
    test_radiant_view_ensure_temp_dir();
    const char* view_path = "./temp/ui_script_content_gc_view.json";
    const ShellEnvEntry env[] = {
        {"LAMBDA_GC_FORCE_EVERY", "1"},
        {"LAMBDA_GC_POISON_FREED", "1"},
        {NULL, NULL},
    };
    const char* args[] = {
        "./lambda.exe", "layout", "test/html/ui_script_content_gc.ls",
        "--view-output", view_path, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    const char* expected[] = {"r1s1e10", "k2", "n1n2n3", "w1w1", "v1", "r4s4e40", "r5s5e50",
        "[9, 6, 13]override7128"};
    for (const char* text : expected) {
        EXPECT_TRUE(test_radiant_view_file_contains(view_path, text))
            << "missing content '" << text << "' after forced GC";
    }
}

TEST(RadiantViewTest, BatchHtmlMeasurementsReleaseLoaderPools) {
    test_radiant_view_ensure_temp_dir();
    const char* snapshot_path = "./temp/wordcloud_metrics_memory.json";
    remove(snapshot_path);
    const char* args[] = {
        "./lambda.exe", "--mem-dump=./temp/wordcloud_metrics_memory.json",
        "test/lambda/chart/test_wordcloud_metrics.ls", "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult result = shell_exec("./lambda.exe", args, &options);
    EXPECT_EQ(0, result.exit_code) << (result.stdout_buf ? result.stdout_buf : "");
    shell_result_free(&result);
    ASSERT_TRUE(test_radiant_view_file_contains(snapshot_path, "physical_total"));
    EXPECT_FALSE(test_radiant_view_file_contains(snapshot_path, "radiant.render.document"));
}

TEST(RadiantViewTest, EmbeddedSvgNumericTextPositionsMatchStringAttributes) {
    test_radiant_view_ensure_temp_dir();
    const char* script_path = "./temp/ui_svg_numeric_text.ls";
    const char* output_paths[] = {"./temp/ui_svg_numeric_text.svg", "./temp/ui_svg_string_text.svg"};
    const char* sources[] = {
        "<html <body <svg width: 300, height: 120, "
        "<text x: 35.5, y: 40.25, dx: -2.5, dy: 1.5, rotate: 25, 'font-size': 24, \"Position\"> "
        "<text x: 40, y: 80, \"A\" <tspan dx: 5.5, dy: -2.5, rotate: -15, \"B\">>>>>\n",
        "<html <body <svg width: 300, height: 120, "
        "<text x: \"35.5\", y: \"40.25\", dx: \"-2.5\", dy: \"1.5\", rotate: \"25\", 'font-size': 24, \"Position\"> "
        "<text x: \"40\", y: \"80\", \"A\" <tspan dx: \"5.5\", dy: \"-2.5\", rotate: \"-15\", \"B\">>>>>\n",
    };
    for (size_t i = 0; i < 2; i++) {
        write_text_file(script_path, sources[i]);
        remove(output_paths[i]);
        const char* args[] = {"./lambda.exe", "render", script_path, "-o", output_paths[i], "--no-log", NULL};
        ShellOptions options = {0};
        options.merge_stderr = true;
        ShellResult result = shell_exec("./lambda.exe", args, &options);
        EXPECT_EQ(0, result.exit_code) << (result.stdout_buf ? result.stdout_buf : "");
        shell_result_free(&result);
    }
    char* numeric = test_radiant_view_read_file(output_paths[0]);
    char* strings = test_radiant_view_read_file(output_paths[1]);
    ASSERT_NE(numeric, nullptr);
    ASSERT_NE(strings, nullptr);
    EXPECT_NE(strstr(numeric, "<path"), nullptr);
    EXPECT_STREQ(numeric, strings);
    free(numeric);
    free(strings);
}

TEST(RadiantViewTest, SerializedScriptDocumentsPreserveUtf8AndCallerUrl) {
    test_radiant_view_ensure_temp_dir();
    const char* script_path = "./temp/ui_serialized_script_utf8.ls";
    const char* view_path = "./temp/ui_serialized_script_utf8.json";
    const char* svg_path = "./temp/ui_serialized_script_utf8.svg";
    const char* sources[] = {
        "<svg width: 300, height: 80, <text x: 10, y: 30, \"café 中文\">>\n",
        "format(<svg width: 300, height: 80, <text x: 10, y: 30, \"café 中文\">>, 'xml')\n",
        "\"<html><head><meta charset='utf-8'></head><body>café 中文</body></html>\"\n",
    };
    for (const char* source : sources) {
        write_text_file(script_path, source);
        const char* layout_args[] = {
            "./lambda.exe", "layout", script_path,
            "--view-output", view_path, "--no-log", NULL,
        };
        ShellOptions options = {0};
        options.merge_stderr = true;
        ShellResult layout_result = shell_exec("./lambda.exe", layout_args, &options);
        // The serialized branch must leave the CLI's borrowed input URL intact.
        EXPECT_EQ(0, layout_result.exit_code)
            << (layout_result.stdout_buf ? layout_result.stdout_buf : "");
        shell_result_free(&layout_result);

        remove(svg_path);
        const char* render_args[] = {
            "./lambda.exe", "render", script_path, "-o", svg_path, "--no-log", NULL,
        };
        ShellResult render_result = shell_exec("./lambda.exe", render_args, &options);
        EXPECT_EQ(0, render_result.exit_code)
            << (render_result.stdout_buf ? render_result.stdout_buf : "");
        shell_result_free(&render_result);
        EXPECT_TRUE(test_radiant_view_file_contains(svg_path, "café"));
        EXPECT_TRUE(test_radiant_view_file_contains(svg_path, "中文"));
    }
}

// Two view-tree dumps match apart from their capture timestamp line.
static bool test_radiant_view_same_view_tree(const char* left_path, const char* right_path) {
    char* left = test_radiant_view_read_file(left_path);
    char* right = test_radiant_view_read_file(right_path);
    bool same = false;
    if (left && right) {
        const char* left_stamp = strstr(left, "\"timestamp\"");
        const char* right_stamp = strstr(right, "\"timestamp\"");
        if (left_stamp && right_stamp && left_stamp - left == right_stamp - right) {
            const char* left_rest = strchr(left_stamp, '\n');
            const char* right_rest = strchr(right_stamp, '\n');
            same = left_rest && right_rest &&
                strncmp(left, right, (size_t)(left_stamp - left)) == 0 &&
                strcmp(left_rest, right_rest) == 0;
        }
    }
    free(left);
    free(right);
    return same;
}

TEST(RadiantViewTest, BatchLoaderRuntimeMatchesFreshRuntimes) {
    // ES12v2: a batch's stateless loaders (LaTeX, markdown math, graph and
    // TikZ custom layouts) share one loader runtime across documents; each
    // document must lay out exactly as it does on a fresh runtime of its own.
    struct LoaderCase { const char* path; const char* batch_name; const char* single_name; };
    const LoaderCase cases[] = {
        {"test/input/math_test.tex", "input__math_test.json", "math_test.json"},
        {"test/input/simple_math_test.md", "input__simple_math_test.json", "simple_math_test.json"},
        {"test/input/test_graph.dot", "input__test_graph.json", "test_graph.json"},
        {"test/input/tikz/plot_parametric.pgf", "tikz__plot_parametric.json", "plot_parametric.json"},
    };
    test_radiant_view_ensure_temp_dir();
    const char* output_dir = "./temp/test_loader_runtime_batch";
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif
    const char* batch_args[] = {
        "./lambda.exe", "layout", cases[0].path, cases[1].path, cases[2].path, cases[3].path,
        "--output-dir", output_dir, "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult batch_result = shell_exec("./lambda.exe", batch_args, &options);
    ASSERT_EQ(0, batch_result.exit_code)
        << (batch_result.stdout_buf ? batch_result.stdout_buf : "");
    shell_result_free(&batch_result);

    for (const LoaderCase& loader_case : cases) {
        char batch_path[256];
        char single_path[256];
        snprintf(batch_path, sizeof(batch_path), "%s/%s", output_dir, loader_case.batch_name);
        snprintf(single_path, sizeof(single_path), "%s/single_%s", output_dir,
                 loader_case.single_name);
        const char* single_args[] = {
            "./lambda.exe", "layout", loader_case.path, "--view-output", single_path,
            "--no-log", NULL,
        };
        ShellResult single_result = shell_exec("./lambda.exe", single_args, &options);
        ASSERT_EQ(0, single_result.exit_code)
            << (single_result.stdout_buf ? single_result.stdout_buf : "");
        shell_result_free(&single_result);
        EXPECT_TRUE(test_radiant_view_same_view_tree(batch_path, single_path))
            << loader_case.path << " lays out differently on the shared loader runtime";
    }
}

TEST(RadiantViewTest, RenderBatchReleasesImageCacheAfterDocumentOwner) {
    const char* jobs = "test/html/render_batch_image_cleanup.tsv";
    ASSERT_TRUE(test_radiant_view_file_readable(jobs));
    test_radiant_view_ensure_temp_dir();
    const char* args[] = {"./lambda.exe", "render-batch", "--no-log", nullptr};
    ShellOptions options = {};
    options.stdin_path = jobs;
    options.merge_stderr = true;
    ShellResult result = shell_exec("./lambda.exe", args, &options);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    // image-cache cleanup must finish after each document, including reuse of the same UI context.
    EXPECT_EQ(result.exit_code, 0) << output;
    EXPECT_NE(strstr(output, "OK\ttest/render/page/bg_image_01.html"), nullptr) << output;
    EXPECT_NE(strstr(output, "OK\ttest/render/page/enhance5_svg_data_uri_image_stack_01.html"), nullptr) << output;
    EXPECT_TRUE(test_radiant_view_file_readable("temp/render_batch_image_cleanup_raster.png"));
    EXPECT_TRUE(test_radiant_view_file_readable("temp/render_batch_image_cleanup_svg.png"));
    shell_result_free(&result);
}

TEST(RadiantViewTest, BatchDocumentFontFaceOverridesSystemCache) {
    const char* system_font_page =
        "test/layout/data/baseline/fixed-table-layout-001.htm";
    const char* document_font_page =
        "test/layout/data/baseline/grid_span_2_max_content_max_content_indefinite.html";
    ASSERT_TRUE(test_radiant_view_file_readable(system_font_page));
    ASSERT_TRUE(test_radiant_view_file_readable(document_font_page));
    test_radiant_view_ensure_temp_dir();

    const char* output_dir = "./temp/test_document_font_cache";
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif
    const char* result_path =
        "./temp/test_document_font_cache/baseline__grid_span_2_max_content_max_content_indefinite.json";
    const char* args[] = {
        "./lambda.exe", "layout", system_font_page, document_font_page,
        "--output-dir", output_dir,
        "--font-dir", "test/layout/data/font",
        "--auto-close", "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);

    // The second document's embedded Ahem must replace the earlier installed
    // face, keeping the zero-width-space text on one 80px line.
    EXPECT_TRUE(test_radiant_view_file_contains(
        result_path, "\"content\": \"HHHH\xE2\x80\x8B" "HHHH\""));
    EXPECT_FALSE(test_radiant_view_file_contains(
        result_path, "\"content\": \"HHHH\""));
}

struct RadiantViewWorkQueue {
    const size_t* selected;
    size_t selected_count;
    size_t next;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
};

static size_t test_radiant_view_claim_work(RadiantViewWorkQueue* queue) {
#ifdef _WIN32
    EnterCriticalSection(&queue->mutex);
#else
    pthread_mutex_lock(&queue->mutex);
#endif
    size_t selected_pos = queue->next++;
#ifdef _WIN32
    LeaveCriticalSection(&queue->mutex);
#else
    pthread_mutex_unlock(&queue->mutex);
#endif
    if (selected_pos >= queue->selected_count) return g_radiant_view_case_count;
    return queue->selected[selected_pos];
}

#ifdef _WIN32
static DWORD WINAPI test_radiant_view_worker(LPVOID data) {
#else
static void* test_radiant_view_worker(void* data) {
#endif
    RadiantViewWorkQueue* queue = (RadiantViewWorkQueue*)data;
    while (true) {
        size_t case_index = test_radiant_view_claim_work(queue);
        if (case_index >= g_radiant_view_case_count) break;
        test_radiant_view_run_case(case_index);
    }
#ifdef _WIN32
    return 0;
#else
    return nullptr;
#endif
}

static int test_radiant_view_cpu_count() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? (int)nprocs : 1;
#endif
}

static int test_radiant_view_default_jobs() {
    int jobs = test_radiant_view_cpu_count();
    if (jobs > 1) jobs--;
    if (jobs < 1) jobs = 1;
    const char* env_jobs = getenv("LAMBDA_RADIANT_VIEW_TEST_JOBS");
    if (env_jobs && env_jobs[0]) {
        int parsed = atoi(env_jobs);
        if (parsed > 0) jobs = parsed;
    }
    return jobs;
}

static bool test_radiant_view_wildcard_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false;
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            while (*text) {
                if (test_radiant_view_wildcard_match(pattern, text)) return true;
                text++;
            }
            return false;
        }
        if (*pattern == '?') {
            if (!*text) return false;
            pattern++;
            text++;
            continue;
        }
        if (*pattern != *text) return false;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static bool test_radiant_view_filter_list_matches(const char* list, size_t list_len,
                                                  const char* test_name) {
    if (!list || list_len == 0) return false;
    const char* cursor = list;
    const char* end = list + list_len;
    while (cursor < end) {
        const char* sep = (const char*)memchr(cursor, ':', (size_t)(end - cursor));
        const char* pattern_end = sep ? sep : end;
        size_t pattern_len = (size_t)(pattern_end - cursor);
        if (pattern_len > 0) {
            char pattern[256];
            if (pattern_len >= sizeof(pattern)) pattern_len = sizeof(pattern) - 1;
            memcpy(pattern, cursor, pattern_len);
            pattern[pattern_len] = '\0';
            if (test_radiant_view_wildcard_match(pattern, test_name)) return true;
        }
        if (!sep) break;
        cursor = sep + 1;
    }
    return false;
}

static bool test_radiant_view_filter_matches(const char* filter, const char* test_name) {
    if (!filter || !filter[0]) filter = "*";
    const char* negative = strchr(filter, '-');
    size_t positive_len = negative ? (size_t)(negative - filter) : strlen(filter);
    if (positive_len == 0) positive_len = 1;

    bool positive_match = false;
    if (positive_len == 1 && filter[0] == '*') {
        positive_match = true;
    } else {
        positive_match = test_radiant_view_filter_list_matches(filter, positive_len, test_name);
    }
    if (!positive_match) return false;
    if (negative && negative[1]) {
        if (test_radiant_view_filter_list_matches(negative + 1, strlen(negative + 1), test_name)) {
            return false;
        }
    }
    return true;
}

static const char* test_radiant_view_filter_from_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--gtest_filter=", 15) == 0) {
            return argv[i] + 15;
        }
        if (strcmp(argv[i], "--gtest_filter") == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return "*";
}

static bool test_radiant_view_list_tests_requested(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gtest_list_tests") == 0) return true;
    }
    return false;
}

static void test_radiant_view_run_selected_parallel(const size_t* selected,
                                                    size_t selected_count,
                                                    int jobs) {
    if (!selected || selected_count == 0) return;
    if (jobs < 1) jobs = 1;
    if ((size_t)jobs > selected_count) jobs = (int)selected_count;

    RadiantViewWorkQueue queue;
    queue.selected = selected;
    queue.selected_count = selected_count;
    queue.next = 0;
#ifdef _WIN32
    InitializeCriticalSection(&queue.mutex);
    HANDLE* threads = (HANDLE*)malloc(sizeof(HANDLE) * (size_t)jobs);
    if (!threads) {
        for (size_t i = 0; i < selected_count; i++) test_radiant_view_run_case(selected[i]);
        DeleteCriticalSection(&queue.mutex);
        return;
    }
    for (int i = 0; i < jobs; i++) {
        threads[i] = CreateThread(nullptr, 0, test_radiant_view_worker, &queue, 0, nullptr);
    }
    WaitForMultipleObjects((DWORD)jobs, threads, TRUE, INFINITE);
    for (int i = 0; i < jobs; i++) CloseHandle(threads[i]);
    free(threads);
    DeleteCriticalSection(&queue.mutex);
#else
    pthread_mutex_init(&queue.mutex, nullptr);
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)jobs);
    if (!threads) {
        for (size_t i = 0; i < selected_count; i++) test_radiant_view_run_case(selected[i]);
        pthread_mutex_destroy(&queue.mutex);
        return;
    }
    for (int i = 0; i < jobs; i++) {
        pthread_create(&threads[i], nullptr, test_radiant_view_worker, &queue);
    }
    for (int i = 0; i < jobs; i++) {
        pthread_join(threads[i], nullptr);
    }
    free(threads);
    pthread_mutex_destroy(&queue.mutex);
#endif
}

int main(int argc, char** argv) {
    const char* filter = test_radiant_view_filter_from_args(argc, argv);
    bool list_only = test_radiant_view_list_tests_requested(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);

    if (!list_only) {
        size_t selected[g_radiant_view_case_count];
        size_t selected_count = 0;
        for (size_t i = 0; i < g_radiant_view_case_count; i++) {
            if (test_radiant_view_filter_matches(filter, g_radiant_view_cases[i].test_name)) {
                selected[selected_count++] = i;
            }
        }
        test_radiant_view_run_selected_parallel(selected, selected_count,
                                                test_radiant_view_default_jobs());
    }

    return RUN_ALL_TESTS();
}
