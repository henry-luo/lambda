#include <gtest/gtest.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "lib/shell.h"

static constexpr unsigned navigation_rounds = 12;
static constexpr uint64_t mib = 1024 * 1024;

static uint64_t median_of_three(const uint64_t* samples) {
    uint64_t low = samples[0] < samples[1] ? samples[0] : samples[1];
    uint64_t high = samples[0] > samples[1] ? samples[0] : samples[1];
    return samples[2] < low ? low : samples[2] > high ? high : samples[2];
}

TEST(ViewerNavigationMemoryTest, RepeatedLoadsKeepWarmFootprintBounded) {
    const char* executable = shell_getenv("LAMBDA_VIEWER_MEMORY_EXE");
    if (!executable || !*executable) {
        GTEST_SKIP() << "Use make test-viewer-navigation-memory to build and supply a release host";
    }
    const char* python = shell_getenv("LAMBDA_VIEWER_MEMORY_PYTHON");
    if (!python || !*python) python = "python3";
    char rounds[16];
    snprintf(rounds, sizeof(rounds), "%u", navigation_rounds);
    const char* args[] = {python, "test/viewer_navigation_memory.py", executable, rounds, nullptr};
    ShellOptions options = {};
    options.merge_stderr = true;
    options.timeout_ms = 180000;
    struct Capture {
        ShellResult result;
        ~Capture() { shell_result_free(&result); }
    } capture = {shell_exec(python, args, &options)};
    const ShellResult& result = capture.result;
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    ASSERT_FALSE(result.timed_out) << output;
    ASSERT_FALSE(result.output_limit_exceeded) << output;
    ASSERT_EQ(result.exit_code, 0) << output;

    const char* marker = "VIEWER_MEMORY_SAMPLE ";
    uint64_t samples[navigation_rounds] = {};
    unsigned count = 0;
    for (const char* line = strstr(output, marker); line; line = strstr(line + 1, marker)) {
        unsigned round;
        uint64_t bytes;
        ASSERT_EQ(sscanf(line + strlen(marker), "%u %" SCNu64, &round, &bytes), 2) << output;
        ASSERT_LT(count, navigation_rounds) << output;
        ASSERT_EQ(round, count + 1) << output;
        ASSERT_GT(bytes, 0u) << output;
        samples[count++] = bytes;
    }
    ASSERT_EQ(count, navigation_rounds) << output;

    // Rounds 4–6 allow first-load caches to settle. Window medians absorb
    // transient allocator and OS reclamation steps without using peak RSS.
    const uint64_t warm = median_of_three(samples + 3);
    const uint64_t late = median_of_three(samples + navigation_rounds - 3);
    constexpr uint64_t growth_budget = 64 * mib;
    RecordProperty("warm_mib", static_cast<int>(warm / mib));
    RecordProperty("late_mib", static_cast<int>(late / mib));
    EXPECT_LE(late, warm + growth_budget) << "Warm rounds 4–6 and final rounds 10–12\n" << output;
}
