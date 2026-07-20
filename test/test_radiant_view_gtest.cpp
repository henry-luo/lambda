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
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

struct RadiantViewCase {
    const char* test_name;
    const char* label;
    const char* path;
    const char* event_path;
};

struct RadiantViewCaseResult {
    bool executed;
    bool missing_path;
    bool missing_event_path;
    bool has_layout_prof;
    bool has_render_prof;
    bool has_peak_footprint;
    bool has_view_completed;
    int exit_code;
    char message[512];
};

static const RadiantViewCase g_radiant_view_cases[] = {
    {"RadiantViewTest.LoadsPngAsHeadlessView", "png", "test/layout/data/res/sample1.png", nullptr},
    {"RadiantViewTest.LoadsJpegAsHeadlessView", "jpg", "test/layout/data/res/sample1.jpg", nullptr},
    {"RadiantViewTest.LoadsGifAsHeadlessView", "gif", "test/layout/data/res/hn_s.gif", nullptr},
    {"RadiantViewTest.LoadsSvgAsHeadlessView", "svg", "test/layout/data/res/hn_y18.svg", nullptr},
    {"RadiantViewTest.LoadsHtmlAsHeadlessView", "html", "test/layout/data/page/sample1.html", nullptr},
    {"RadiantViewTest.LoadsXmlAsHeadlessView", "xml", "test/input/test.xml", nullptr},
    {"RadiantViewTest.LoadsMarkdownAsHeadlessView", "markdown", "test/input/comprehensive_test.md", nullptr},
    {"RadiantViewTest.LoadsMarkdownMathAsHeadlessView", "markdown_math", "test/input/simple_math_test.md", nullptr},
    {"RadiantViewTest.LoadsWikiAsHeadlessView", "wiki", "test/input/test.wiki", nullptr},
    {"RadiantViewTest.LoadsLatexShowcaseAsHeadlessView", "latex_showcase", "test/input/latex-showcase.tex", nullptr},
    {"RadiantViewTest.LoadsMathIntensiveLatexAsHeadlessView", "latex_math_intensive", "test/input/math_intensive_test.tex", nullptr},
    {"RadiantViewTest.LoadsYamlAsHeadlessView", "yaml", "test/input/more_test.yaml", nullptr},
    {"RadiantViewTest.LoadsLambdaReportAsHeadlessView", "lambda_report", "test/lambda/complex_iot_report_html.ls", nullptr},
    {"RadiantViewTest.LoadsLambdaChartDashboardAsHeadlessView", "lambda_chart_dashboard", "test/lambda/chart/chart_dashboard.ls", nullptr},
    {"RadiantViewTest.LoadsPdfAsHeadlessView", "pdf", "test/input/raw_commands_test.pdf", nullptr},
    {"RadiantViewTest.LoadsPdfIntoIframeAfterLinkClickWithNoLog", "pdf_iframe", "test/html/index.html", "test/ui/radiant_view_pdf_iframe.json"},
    {"RadiantViewTest.LoadsMarkdownIntoIframeAfterLinkClickWithNoLog", "markdown_iframe", "test/html/index.html", "test/ui/radiant_view_markdown_iframe.json"},
    {"RadiantViewTest.LoadsPngIntoIframeAfterLinkClickWithNoLog", "png_iframe", "test/html/index.html", "test/ui/radiant_view_png_iframe.json"},
    {"RadiantViewTest.KeepsIframeSizedAfterPdfThenSvgTargetNavigation", "iframe_pdf_svg_sequence", "test/html/index.html", "test/ui/radiant_view_iframe_pdf_svg_sequence.json"},
    {"RadiantViewTest.RecascadesLambdaReportInIframeDuringScrollHover", "lambda_report_iframe_scroll_hover", "test/html/index.html", "test/ui/radiant_view_lambda_report_iframe_hover.json"},
    {"RadiantViewTest.ResetsGlyphCacheAcrossIframeDocumentNavigation", "iframe_font_navigation", "test/html/index.html", "test/ui/radiant_view_iframe_font_navigation.json"},
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

static bool test_radiant_view_file_contains(const char* path, const char* needle) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return false;
    }
    size_t read_size = fread(buffer, 1, (size_t)size, file);
    buffer[read_size] = '\0';
    bool found = strstr(buffer, needle) != NULL;
    free(buffer);
    fclose(file);
    return found;
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
        result->missing_event_path = true;
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

    ShellOptions options = {0};
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
    if (result->exit_code != 0) {
        snprintf(result->message, sizeof(result->message),
                 "lambda view exited with code %d; see %s",
                 result->exit_code, log_path);
    } else if (result->has_layout_prof || result->has_render_prof ||
               result->has_peak_footprint || result->has_view_completed) {
        snprintf(result->message, sizeof(result->message),
                 "--no-log output leaked into %s", log_path);
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
    EXPECT_FALSE(result->missing_event_path) << result->message;
    EXPECT_EQ(0, result->exit_code) << view_case->path << ": " << result->message;
    EXPECT_FALSE(result->has_layout_prof) << view_case->path;
    EXPECT_FALSE(result->has_render_prof) << view_case->path;
    EXPECT_FALSE(result->has_peak_footprint) << view_case->path;
    EXPECT_FALSE(result->has_view_completed) << view_case->path;
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

TEST(RadiantViewTest, LoadsPdfIntoIframeAfterLinkClickWithNoLog) {
    test_radiant_view_expect_case(15);
}

TEST(RadiantViewTest, LoadsMarkdownIntoIframeAfterLinkClickWithNoLog) {
    test_radiant_view_expect_case(16);
}

TEST(RadiantViewTest, LoadsPngIntoIframeAfterLinkClickWithNoLog) {
    test_radiant_view_expect_case(17);
}

TEST(RadiantViewTest, KeepsIframeSizedAfterPdfThenSvgTargetNavigation) {
    test_radiant_view_expect_case(18);
}

TEST(RadiantViewTest, RecascadesLambdaReportInIframeDuringScrollHover) {
    test_radiant_view_expect_case(19);
}

TEST(RadiantViewTest, ResetsGlyphCacheAcrossIframeDocumentNavigation) {
    test_radiant_view_expect_case(20);
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
    const char* args[] = {
        "./lambda.exe", "view", "test/html/image_cache_promotion.html", "--headless", NULL,
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    // Per-child environment keeps this diagnostic isolated from parallel workers.
    ShellResult shell_result = shell_exec("./lambda.exe", args, &options);
    ASSERT_EQ(0, shell_result.exit_code)
        << (shell_result.stdout_buf ? shell_result.stdout_buf : "");
    shell_result_free(&shell_result);
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log,
        "[image] Decoded local image on demand: 64x42 (intrinsic 640x427, target 60x40)"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        view_log,
        "[image] Decoded local image on demand: 640x427 (intrinsic 640x427, target 640x427)"));
}

TEST(RadiantViewTest, JsMirCacheKeepsFreshDocumentRealms) {
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/js_cache_realm_mutate.html"));
    ASSERT_TRUE(test_radiant_view_file_readable("test/html/js_cache_realm_verify.html"));
    test_radiant_view_ensure_temp_dir();

    const char* output_dir = "./temp/test_js_mir_cache_realm";
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif

    const char* timing_path = "./temp/test_js_mir_cache_realm/timing.jsonl";
    const char* result_path =
        "./temp/test_js_mir_cache_realm/html__js_cache_realm_verify.json";
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
    shell_result_free(&shell_result);

    // Cached code may cross documents, but window values and prototype
    // mutations must remain owned by the document heap that created them.
    EXPECT_TRUE(test_radiant_view_file_contains(
        result_path, "js-cache-realm-isolated"));
    EXPECT_FALSE(test_radiant_view_file_contains(
        result_path, "js-cache-realm-leaked"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_cache_hits\":5"));
    EXPECT_TRUE(test_radiant_view_file_contains(
        timing_path, "\"script_cache_compiles\":0"));
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
