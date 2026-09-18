// Standard-host page regression for CSS cascade ownership and reuse.

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
#define LAMBDA_EXE "lambda.exe"
#else
#define LAMBDA_EXE "./lambda.exe"
#endif

#define CSS_MEMORY_BASELINE "test/css_cascade_memory_baseline.tsv"
#define CSS_MEMORY_LOG "log.txt"
#define CSS_MEMORY_TIMEOUT_MS 60000
#define CSS_MEMORY_TOLERANCE_PERCENT 15ULL

struct CssMemoryCase {
    const char* name;
    const char* path;
};

struct CssMemorySample {
    uint64_t document_live;
    int64_t document_live_delta;
    int64_t work_live_delta;
    uint64_t css_live;
    uint64_t canonical_live;
    int64_t canonical_live_delta;
    uint64_t entries;
    uint64_t bound_refs;
    uint64_t payloads;
    uint64_t payload_refs;
    uint64_t bound_entries;
    uint64_t cold_entries;
    uint64_t cold_bytes;
};

struct CssMemoryBaseline {
    uint64_t document_live;
    uint64_t document_live_delta;
    uint64_t work_live_delta;
    uint64_t css_live;
    uint64_t canonical_live;
    uint64_t canonical_live_delta;
    uint64_t entries;
    uint64_t bound_refs;
    uint64_t payloads;
    uint64_t payload_refs;
    uint64_t bound_entries;
    uint64_t cold_entries;
    uint64_t cold_bytes;
};

static const CssMemoryCase k_css_memory_cases[] = {
    {"jqueryui", "test/layout/data/page/jqueryui.html"},
    {"linuxmint", "test/layout/data/page/linuxmint.html"},
    {"netflix", "test/layout/data/page/netflix.html"},
    {"bootstrap5_kitchen_sink", "test/layout/data/page/bootstrap-5-kitchen-sink_.html"},
    {"matrix_admin", "test/layout/data/web-tmpl/matrix-free-bootstrap-admin-template/index.html"},
    {"bschool", "test/layout/data/web-tmpl/b-school-free-education-html5-website-template/index.html"},
};

static bool css_memory_file_exists(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return false;
    fclose(file);
    return true;
}

static uint64_t css_memory_budget(uint64_t baseline) {
    // Pool extents round differently on supported allocators; preserve a small
    // fixed allowance while making meaningful cascade growth fail the test.
    return baseline + (baseline * CSS_MEMORY_TOLERANCE_PERCENT) / 100ULL + 4096ULL;
}

static bool css_memory_parse_uint64(const char* line, const char* key,
                                    uint64_t* out_value) {
    if (!line || !key || !out_value) return false;
    const char* value = strstr(line, key);
    if (!value) return false;
    value += strlen(key);
    char* end = nullptr;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value) return false;
    *out_value = (uint64_t)parsed;
    return true;
}

static bool css_memory_parse_int64(const char* line, const char* key,
                                   int64_t* out_value) {
    if (!line || !key || !out_value) return false;
    const char* value = strstr(line, key);
    if (!value) return false;
    value += strlen(key);
    char* end = nullptr;
    long long parsed = strtoll(value, &end, 10);
    if (end == value) return false;
    *out_value = (int64_t)parsed;
    return true;
}

static bool css_memory_parse_sample(const char* line, CssMemorySample* sample) {
    if (!line || !sample || !strstr(line, "[CSS_CASCADE_MEMORY]")) return false;
    memset(sample, 0, sizeof(*sample));
    return css_memory_parse_uint64(line, "document_live=", &sample->document_live) &&
        css_memory_parse_int64(line, "document_live_delta=", &sample->document_live_delta) &&
        css_memory_parse_int64(line, "work_live_delta=", &sample->work_live_delta) &&
        css_memory_parse_uint64(line, "css_live=", &sample->css_live) &&
        css_memory_parse_uint64(line, "canonical_live=", &sample->canonical_live) &&
        css_memory_parse_int64(line, "canonical_live_delta=", &sample->canonical_live_delta) &&
        css_memory_parse_uint64(line, "entries=", &sample->entries) &&
        css_memory_parse_uint64(line, "bound_refs=", &sample->bound_refs) &&
        css_memory_parse_uint64(line, "payloads=", &sample->payloads) &&
        css_memory_parse_uint64(line, "payload_refs=", &sample->payload_refs) &&
        css_memory_parse_uint64(line, "bound_entries=", &sample->bound_entries) &&
        css_memory_parse_uint64(line, "cold_entries=", &sample->cold_entries) &&
        css_memory_parse_uint64(line, "cold_bytes=", &sample->cold_bytes);
}

static bool css_memory_read_samples(CssMemorySample* initial,
                                    CssMemorySample* recascade) {
    if (!initial || !recascade) return false;
    FILE* file = fopen(CSS_MEMORY_LOG, "r");
    if (!file) return false;

    bool saw_initial = false;
    bool saw_recascade = false;
    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        CssMemorySample sample = {};
        if (!css_memory_parse_sample(line, &sample)) continue;
        if (strstr(line, "phase=initial")) {
            *initial = sample;
            saw_initial = true;
        } else if (strstr(line, "phase=recascade")) {
            *recascade = sample;
            saw_recascade = true;
        }
    }
    fclose(file);
    return saw_initial && saw_recascade;
}

static bool css_memory_read_baseline(const char* case_name, const char* phase,
                                     CssMemoryBaseline* out_baseline) {
    if (!case_name || !phase || !out_baseline) return false;
    FILE* file = fopen(CSS_MEMORY_BASELINE, "r");
    if (!file) return false;

    bool found = false;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[96] = {};
        char recorded_phase[32] = {};
        unsigned long long document_live = 0;
        unsigned long long document_live_delta = 0;
        unsigned long long work_live_delta = 0;
        unsigned long long css_live = 0;
        unsigned long long canonical_live = 0;
        unsigned long long canonical_live_delta = 0;
        unsigned long long entries = 0;
        unsigned long long bound_refs = 0;
        unsigned long long payloads = 0;
        unsigned long long payload_refs = 0;
        unsigned long long bound_entries = 0;
        unsigned long long cold_entries = 0;
        unsigned long long cold_bytes = 0;
        int field_count = sscanf(line,
            "%95s %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            name, recorded_phase, &document_live, &document_live_delta,
            &work_live_delta, &css_live, &canonical_live, &canonical_live_delta,
            &entries, &bound_refs, &payloads, &payload_refs, &bound_entries,
            &cold_entries, &cold_bytes);
        if (field_count != 15 || strcmp(name, case_name) != 0 ||
            strcmp(recorded_phase, phase) != 0) {
            continue;
        }
        out_baseline->document_live = (uint64_t)document_live;
        out_baseline->document_live_delta = (uint64_t)document_live_delta;
        out_baseline->work_live_delta = (uint64_t)work_live_delta;
        out_baseline->css_live = (uint64_t)css_live;
        out_baseline->canonical_live = (uint64_t)canonical_live;
        out_baseline->canonical_live_delta = (uint64_t)canonical_live_delta;
        out_baseline->entries = (uint64_t)entries;
        out_baseline->bound_refs = (uint64_t)bound_refs;
        out_baseline->payloads = (uint64_t)payloads;
        out_baseline->payload_refs = (uint64_t)payload_refs;
        out_baseline->bound_entries = (uint64_t)bound_entries;
        out_baseline->cold_entries = (uint64_t)cold_entries;
        out_baseline->cold_bytes = (uint64_t)cold_bytes;
        found = true;
        break;
    }
    fclose(file);
    return found;
}

static ShellResult css_memory_run_page(const char* path) {
    const char* args[] = {LAMBDA_EXE, "view", path, "--headless", nullptr};
    const ShellEnvEntry env[] = {
        {"LAMBDA_AUTO_CLOSE", "1"},
        {"LAMBDA_LOG_LEVEL", "NOTICE"},
        {"RADIANT_CSS_CASCADE_MEMORY_PROFILE", "1"},
        {"RADIANT_CSS_CASCADE_MEMORY_FORCE_RECASCADE", "1"},
        {nullptr, nullptr},
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = CSS_MEMORY_TIMEOUT_MS;
    options.merge_stderr = true;
    return shell_exec(LAMBDA_EXE, args, &options);
}

static void css_memory_expect_within_baseline(const CssMemoryCase* test_case,
                                              const char* phase,
                                              const CssMemorySample* actual,
                                              const CssMemoryBaseline* baseline) {
    ASSERT_NE(test_case, nullptr);
    ASSERT_NE(phase, nullptr);
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(baseline, nullptr);
    // document_live includes pre-cascade parsing and script allocations; the
    // cascade budget is the scoped document_live_delta checked below.
    EXPECT_LE(actual->document_live_delta,
              (int64_t)css_memory_budget(baseline->document_live_delta))
        << test_case->name << " " << phase << " document cascade delta";
    EXPECT_LE(actual->work_live_delta,
              (int64_t)css_memory_budget(baseline->work_live_delta))
        << test_case->name << " " << phase << " CSS work-pool cascade delta";
    EXPECT_LE(actual->css_live, css_memory_budget(baseline->css_live))
        << test_case->name << " " << phase << " CSS-role live bytes";
    EXPECT_LE(actual->canonical_live, css_memory_budget(baseline->canonical_live))
        << test_case->name << " " << phase << " canonical CSS live bytes";
    EXPECT_LE(actual->canonical_live_delta,
              (int64_t)css_memory_budget(baseline->canonical_live_delta))
        << test_case->name << " " << phase << " canonical cascade delta";
    EXPECT_LE(actual->entries, baseline->entries)
        << test_case->name << " " << phase << " canonical recipe entries";
    EXPECT_LE(actual->bound_refs, baseline->bound_refs)
        << test_case->name << " " << phase << " canonical style bindings";
    EXPECT_LE(actual->payloads, baseline->payloads)
        << test_case->name << " " << phase << " immutable declaration payloads";
    EXPECT_LE(actual->payload_refs, baseline->payload_refs)
        << test_case->name << " " << phase << " immutable declaration payload bindings";
    EXPECT_LE(actual->bound_entries, baseline->bound_entries)
        << test_case->name << " " << phase << " currently bound canonical entries";
    EXPECT_LE(actual->cold_entries, baseline->cold_entries)
        << test_case->name << " " << phase << " retained cold canonical entries";
    EXPECT_LE(actual->cold_bytes, baseline->cold_bytes)
        << test_case->name << " " << phase << " retained cold canonical bytes";
}

TEST(CssCascadeMemory, PageLoadAndRecascadeStayWithinBaseline) {
    if (!css_memory_file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make test-css-cascade-memory";
    }
    ASSERT_TRUE(css_memory_file_exists(CSS_MEMORY_BASELINE))
        << "missing CSS cascade memory baseline";

    for (size_t index = 0;
         index < sizeof(k_css_memory_cases) / sizeof(k_css_memory_cases[0]); index++) {
        const CssMemoryCase* test_case = &k_css_memory_cases[index];
        ASSERT_TRUE(css_memory_file_exists(test_case->path))
            << "missing fixture " << test_case->path;

        ShellResult result = css_memory_run_page(test_case->path);
        bool child_ok = result.exit_code == 0 && !result.timed_out;
        shell_result_free(&result);
        ASSERT_TRUE(child_ok) << test_case->name << " page load failed";

        CssMemorySample initial = {};
        CssMemorySample recascade = {};
        ASSERT_TRUE(css_memory_read_samples(&initial, &recascade))
            << test_case->name << " did not emit both CSS cascade memory samples";

        CssMemoryBaseline initial_baseline = {};
        CssMemoryBaseline recascade_baseline = {};
        ASSERT_TRUE(css_memory_read_baseline(test_case->name, "initial", &initial_baseline));
        ASSERT_TRUE(css_memory_read_baseline(test_case->name, "recascade", &recascade_baseline));
        css_memory_expect_within_baseline(test_case, "initial", &initial, &initial_baseline);
        css_memory_expect_within_baseline(test_case, "recascade", &recascade, &recascade_baseline);
    }
}
