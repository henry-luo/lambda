#ifndef TEST_AST_TUNE_CAPTURE_HPP
#define TEST_AST_TUNE_CAPTURE_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "../lambda/runtime/compiler_timing.hpp"

// Test-only capture support. The compiler stays allocation-free when timing is
// disabled; this file is used by the GTest harness only when a TSV destination
// is explicitly requested by the capture script.
inline unsigned long long ast_tune_source_bytes(const char* path) {
    struct stat st = {};
    return path && stat(path, &st) == 0 ? (unsigned long long)st.st_size : 0;
}

inline void ast_tune_escape_field(const char* value, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    size_t w = 0;
    if (value) {
        for (const char* p = value; *p && w + 2 < out_size; p++) {
            if (*p == '\t' || *p == '\n' || *p == '\r') {
                out[w++] = '_';
            } else {
                out[w++] = *p;
            }
        }
    }
    out[w] = '\0';
}

inline void ast_tune_append_timing_row(const char* suite, const char* sample_id,
        const char* test_name, int status, const LambdaCompilerTiming* timing,
        bool has_timing, bool has_volume = false,
        const char* cache_state = "cold") {
    const char* path = getenv("AST_TUNE_TIMING_TSV");
    if (!path || !path[0] || !suite || !sample_id || !test_name) return;

    FILE* file = fopen(path, "a+");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file,
            "schema_version\tsuite\trun_id\tsample_id\ttest_name\tstatus\tcache_state\tsource_bytes\t"
            "parse_us\tast_build_us\tbind_us\tvalidate_us\tindex_us\tanalysis_us\tplan_us\t"
            "mir_lower_us\temitter_finalize_us\tmodule_finalize_us\tlink_us\tbuild_transpile_us\t"
            "execute_us\tcleanup_us\tmir_module_count\tmir_function_count\tmir_insn_count\n");
    }

    char sample[1024], name[1024];
    ast_tune_escape_field(sample_id, sample, sizeof(sample));
    ast_tune_escape_field(test_name, name, sizeof(name));
    const char* run_id = getenv("AST_TUNE_RUN_ID");
    if (!run_id || !run_id[0]) run_id = "unset";
    const LambdaCompilerTiming zero = {};
    const bool complete = has_timing && has_volume;
    const LambdaCompilerTiming* t = complete && timing ? timing : &zero;
    const char* recorded_cache_state = complete ? cache_state : "cached";
    fprintf(file,
        "1\t%s\t%s\t%s\t%s\t%d\t%s\t%llu\t"
        "%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t"
        "%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t"
        "%llu\t%llu\t%llu\n",
        suite, run_id, sample, name, status, recorded_cache_state,
        ast_tune_source_bytes(sample_id),
        (unsigned long long)t->parse_us,
        (unsigned long long)t->ast_build_us,
        (unsigned long long)t->bind_us,
        (unsigned long long)t->validate_us,
        (unsigned long long)t->index_us,
        (unsigned long long)t->analysis_us,
        (unsigned long long)t->plan_us,
        (unsigned long long)t->mir_lower_us,
        (unsigned long long)t->emitter_finalize_us,
        (unsigned long long)t->module_finalize_us,
        (unsigned long long)t->link_us,
        (unsigned long long)t->build_transpile_us,
        (unsigned long long)t->execute_us,
        (unsigned long long)t->cleanup_us,
        (unsigned long long)t->mir_module_count,
        (unsigned long long)t->mir_function_count,
        (unsigned long long)t->mir_insn_count);
    fclose(file);
}

#endif
