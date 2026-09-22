#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
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

static bool test_radiant_view_unsupported_image_server_start(RadiantViewImageServer* server) {
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
            char header[256];
            int header_size = snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                is_image_request ? "image/avif" : "text/html", body_size);
            bool sent = header_size > 0 && header_size < (int)sizeof(header) &&
                test_radiant_view_send_all(client, header, (size_t)header_size);
            if (sent) {
                sent = test_radiant_view_send_all(client, body, body_size);
            }
            close(client);
            if (!sent) break;
            if (is_prime_request) served_prime = true;
            if (!is_image_request && !is_prime_request && served_prime) served_document = true;
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

static ShellResult test_radiant_view_run_logged_headless(const char* page,
                                                         const char* event_path,
                                                         const ShellEnvEntry* env) {
    const char* args[7] = {};
    int arg_count = 0;
    args[arg_count++] = "./lambda.exe";
    args[arg_count++] = "view";
    args[arg_count++] = page;
    if (event_path) {
        args[arg_count++] = "--event-file";
        args[arg_count++] = event_path;
    }
    args[arg_count++] = "--headless";
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
