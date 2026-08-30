#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/ts/ts_transpiler.hpp"
#include "../lambda/js/parser/js_parser.h"

namespace {

enum { BENCH_RUNS = 5, BENCH_FIXTURES = 26 };

struct BenchFixture {
    const char* label;
    const char* path;
    JsParseMode mode;
    bool typescript;
    char* source;
    size_t length;
};

struct BenchResult {
    double run_ms[BENCH_RUNS];
    double fixture_ms[BENCH_RUNS][BENCH_FIXTURES];
};

static BenchFixture g_fixtures[BENCH_FIXTURES] = {
    {"underscore", "test/js/underscore_lib.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"ramda", "test/js/ramda_src_min.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"lodash", "test/js/lib_lodash.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"ajv", "test/js/lib_ajv.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"yup", "test/js/lib_yup.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"acorn", "test/js/lib_acorn.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"jquery", "test/js/dom_jquery_lib.js", JS_PARSE_SCRIPT, false, NULL, 0},
    {"ts_arithmetic", "test/ts/arithmetic.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_arrays", "test/ts/arrays.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_arrow_functions", "test/ts/arrow_functions.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_as_expression", "test/ts/as_expression.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_basic", "test/ts/basic_types.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_closures", "test/ts/closures.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_constructor_properties", "test/ts/constructor_properties.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_decorators", "test/ts/decorators.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_enums", "test/ts/enums.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_functions", "test/ts/functions.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_interface_type_alias", "test/ts/interface_type_alias.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_namespace", "test/ts/namespace.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_optional_params", "test/ts/optional_params.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_control_flow", "test/ts/control_flow.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_runtime_types", "test/ts/runtime_types.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_string_ops", "test/ts/string_ops.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_type_guards", "test/ts/type_guards.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_type_inference", "test/ts/type_inference.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
    {"ts_typeof_checks", "test/ts/typeof_checks.ts",
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), true, NULL, 0},
};

static uint64_t bench_now_ns() {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

static bool bench_load_fixture(BenchFixture* fixture) {
    FILE* file = fopen(fixture->path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size_t length = (size_t)file_size;
    char* source = (char*)malloc(length + 1);
    if (!source) {
        fclose(file);
        return false;
    }
    size_t read_count = fread(source, 1, length, file);
    fclose(file);
    if (read_count != length) {
        free(source);
        return false;
    }
    source[length] = '\0';
    fixture->source = source;
    fixture->length = length;
    return true;
}

static void bench_free_fixtures() {
    for (int i = 0; i < BENCH_FIXTURES; i++) {
        free(g_fixtures[i].source);
        g_fixtures[i].source = NULL;
        g_fixtures[i].length = 0;
    }
}

static bool bench_c_recognizer(const BenchFixture* fixture) {
    JsParseError error = {};
    JsParseMetrics metrics = {};
    return js_parser_parse_source(fixture->source, fixture->length,
        fixture->mode, NULL, NULL, &metrics, &error) == JS_PARSE_OK;
}

static bool bench_c_frontend(const BenchFixture* fixture) {
    JsTranspiler* transpiler = js_transpiler_create(NULL);
    if (!transpiler) return false;
    bool ok = js_transpiler_parse_c(transpiler, fixture->source,
        fixture->length, fixture->mode);
    JsAstNode* ast = ok ? (JsAstNode*)transpiler->ast_root : NULL;
    if (ok && fixture->typescript) ts_resolve_all_types(transpiler, ast);
    if (ok) ok = ast && js_check_early_errors(transpiler, ast) == 0;
    js_transpiler_destroy(transpiler);
    return ok;
}

typedef bool (*BenchOperation)(const BenchFixture* fixture);

static double bench_order_statistic(const double* values, int order) {
    double sorted[BENCH_RUNS];
    memcpy(sorted, values, sizeof(sorted));
    for (int i = 1; i < BENCH_RUNS; i++) {
        double value = sorted[i];
        int j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }
    return sorted[order];
}

static bool bench_run(const char* cohort, BenchOperation operation,
        BenchResult* result, bool typescript) {
    memset(result, 0, sizeof(*result));
    volatile uint64_t guard = 0;
    for (int run = 0; run <= BENCH_RUNS; run++) {
        uint64_t cohort_start = bench_now_ns();
        for (int fixture = 0; fixture < BENCH_FIXTURES; fixture++) {
            if (g_fixtures[fixture].typescript != typescript) continue;
            uint64_t fixture_start = bench_now_ns();
            bool ok = operation(&g_fixtures[fixture]);
            uint64_t fixture_end = bench_now_ns();
            if (!ok) return false;
            guard += (uint64_t)(fixture_end - fixture_start);
            if (run > 0) {
                result->fixture_ms[run - 1][fixture] =
                    (double)(fixture_end - fixture_start) / 1000000.0;
            }
        }
        uint64_t cohort_end = bench_now_ns();
        guard += cohort_end - cohort_start;
        if (run > 0) {
            result->run_ms[run - 1] =
                (double)(cohort_end - cohort_start) / 1000000.0;
        }
    }
    if (guard == 0) return false;

    uint64_t total_bytes = 0;
    int file_count = 0;
    for (int fixture = 0; fixture < BENCH_FIXTURES; fixture++) {
        if (g_fixtures[fixture].typescript != typescript) continue;
        file_count++;
        total_bytes += (uint64_t)g_fixtures[fixture].length;
    }
    GTEST_LOG_(INFO) << "js-c-parser-bench: cohort=" << cohort
        << " files=" << file_count << " bytes=" << total_bytes
        << " median_ms=" << bench_order_statistic(result->run_ms, 2)
        << " p95_ms=" << bench_order_statistic(result->run_ms, 4)
        << " run1_ms=" << result->run_ms[0]
        << " run2_ms=" << result->run_ms[1]
        << " run3_ms=" << result->run_ms[2]
        << " run4_ms=" << result->run_ms[3]
        << " run5_ms=" << result->run_ms[4];
    for (int fixture = 0; fixture < BENCH_FIXTURES; fixture++) {
        if (g_fixtures[fixture].typescript != typescript) continue;
        GTEST_LOG_(INFO) << "js-c-parser-bench: cohort=" << cohort
            << " fixture=" << g_fixtures[fixture].label
            << " bytes=" << g_fixtures[fixture].length
            << " run1_ms=" << result->fixture_ms[0][fixture]
            << " run2_ms=" << result->fixture_ms[1][fixture]
            << " run3_ms=" << result->fixture_ms[2][fixture]
            << " run4_ms=" << result->fixture_ms[3][fixture]
            << " run5_ms=" << result->fixture_ms[4][fixture];
    }
    return true;
}

}  // namespace

TEST(JsParserBenchmark, PreloadedRecognizerAndFrontend) {
    for (int i = 0; i < BENCH_FIXTURES; i++) {
        ASSERT_TRUE(bench_load_fixture(&g_fixtures[i]))
            << "cannot preload " << g_fixtures[i].path;
    }

    BenchResult recognizer_js = {};
    BenchResult recognizer_ts = {};
    BenchResult frontend_js = {};
    BenchResult frontend_ts = {};
    ASSERT_TRUE(bench_run("c-recognizer", bench_c_recognizer,
        &recognizer_js, false));
    ASSERT_TRUE(bench_run("c-recognizer-ts", bench_c_recognizer,
        &recognizer_ts, true));
    ASSERT_TRUE(bench_run("c-frontend", bench_c_frontend,
        &frontend_js, false));
    ASSERT_TRUE(bench_run("c-frontend-ts", bench_c_frontend,
        &frontend_ts, true));
    bench_free_fixtures();
}
