#include <gtest/gtest.h>
#include "../lib/file.h"
#include "../lib/shell.h"
#include "../lib/strbuf.h"
#include "../lib/time_util.h"
#include "../lib/mem.h"
#include "../lib/str.h"
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

struct Scene3dCase {
    const char* name;
    const char* document;
    const char* events;
    int event_count;
    bool fails = false;
    bool malformed = false;
    bool without_graphics = false;
};
static const Scene3dCase scene3d_cases[] = {
#include "scene3d/cases.inc"
};
static const Scene3dCase render_contracts[] = {
#include "scene3d/contracts.inc"
};

class Scene3dTest : public ::testing::Test {
    const Scene3dCase* fixture;
public:
    explicit Scene3dTest(const Scene3dCase* selected) : fixture(selected) {}
    void TestBody() override {
        ASSERT_TRUE(file_exists(fixture->document)) << fixture->document;
        ASSERT_TRUE(file_exists(fixture->events)) << fixture->events;
#ifdef _WIN32
        int process_id = _getpid();
#else
        int process_id = getpid();
#endif
        StrBuf* directory = strbuf_new();
        strbuf_append_format(directory, "./temp/scene3d/%d-%llu-%s", process_id,
            (unsigned long long)time_now_ns(), fixture->name);
        if (!create_dir(directory->str)) {
            strbuf_free(directory);
            FAIL() << "Could not create Scene3D output directory";
        }
        StrBuf* result_path = strbuf_dup(directory);
        StrBuf* log_path = strbuf_dup(directory);
        strbuf_append_str(result_path, "/events.json");
        strbuf_append_str(log_path, "/viewer.log");
        const char* executable = getenv("RADIANT_TEST_EXECUTABLE");
        if (!executable || !executable[0]) executable = "./lambda.exe";
        const char* args[] = {executable, "view", fixture->document, "--headless",
            "--event-file", fixture->events, "--event-result", result_path->str, nullptr};
        ShellEnvEntry environment[] = {
            {"LAMBDA_HEADLESS_GLFW_WINDOW", fixture->without_graphics ? nullptr : "1"},
            {"LAMBDA_LOG_FILE", log_path->str}, {"LAMBDA_EVENT_SIM_OUTPUT_DIR", directory->str},
            {nullptr, nullptr},
        };
        ShellOptions options = {};
        options.env = environment;
        options.merge_stderr = true;
        options.timeout_ms = 120000;
        uint64_t start = time_now_ns();
        ShellResult result = shell_exec(executable, args, &options);
        double elapsed = time_elapsed_ms_f(start, time_now_ns());
        char milliseconds[64];
        str_fmt(milliseconds, sizeof(milliseconds), "%.3f", elapsed);
        RecordProperty("scene3d_elapsed_ms", milliseconds);
        RecordProperty("scene3d_log", log_path->str);
        EXPECT_FALSE(result.timed_out) << fixture->name;
        EXPECT_FALSE(result.output_limit_exceeded);
        EXPECT_EQ(result.exit_code, fixture->fails ? 1 : 0) << (result.stdout_buf ? result.stdout_buf : "")
            << "\nSee " << log_path->str;
        char* report = file_exists(result_path->str) ? read_text_file(result_path->str) : nullptr;
        if (fixture->malformed) EXPECT_EQ(report, nullptr) << "Malformed fixtures must fail before execution";
        else EXPECT_NE(report, nullptr) << "Missing event-sim result: " << result_path->str;
        if (report) {
            int version = 0, executed = 0, passed = 0, failed = 0, consumed = 0;
            char verdict[8] = {};
            // Consume the stable machine protocol, including its entire JSON object.
            int fields = sscanf(report,
                "{\"version\":%d,\"events\":%d,\"assertions\":{\"passed\":%d,\"failed\":%d},\"result\":\"%7[^\"]\"} %n",
                &version, &executed, &passed, &failed, verdict, &consumed);
            EXPECT_EQ(fields, 5) << report;
            EXPECT_EQ((size_t)consumed, strlen(report));
            EXPECT_EQ(version, 1);
            EXPECT_EQ(executed, fixture->event_count) << "Event inventory changed";
            if (fixture->fails) {
                EXPECT_GT(failed, 0);
                EXPECT_STREQ(verdict, "FAIL");
            } else {
                EXPECT_GT(passed, 0);
                EXPECT_EQ(failed, 0);
                EXPECT_STREQ(verdict, "PASS");
            }
            RecordProperty("scene3d_assertions", passed + failed);
            mem_free(report);
        }
        shell_result_free(&result);
        strbuf_free(log_path); strbuf_free(result_path); strbuf_free(directory);
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    for (const auto& fixture : scene3d_cases) {
        const Scene3dCase* selected = &fixture;
        ::testing::RegisterTest("Scene3dTest", fixture.name, nullptr, nullptr,
            __FILE__, __LINE__, [selected]() -> Scene3dTest* { return new Scene3dTest(selected); });
    }
    for (const auto& fixture : render_contracts) {
        const Scene3dCase* selected = &fixture;
        ::testing::RegisterTest("EventSimRenderTest", fixture.name, nullptr, nullptr,
            __FILE__, __LINE__, [selected]() -> Scene3dTest* { return new Scene3dTest(selected); });
    }
    return RUN_ALL_TESTS();
}
