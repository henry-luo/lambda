#include <gtest/gtest.h>
#include "test_radiant_view_helpers.hpp"
#include "../lib/file.h"
#include "../lib/strbuf.h"
#include "../lib/mem.h"
#include <cstdio>
#include <cstring>

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
        RadiantViewLaunch launch = {};
        launch.label = fixture->name;
        launch.page = fixture->document;
        launch.event_path = fixture->events;
        launch.timeout_ms = 120000;
        launch.graphics = !fixture->without_graphics;
        ASSERT_TRUE(test_radiant_view_prepare_launch(&launch));
        // event-sim artifacts stay relative to the isolated child's scratch directory.
        StrBuf* directory = strbuf_new();
        strbuf_append_format(directory, "%s/temp/scene3d", launch.cwd);
        bool created = create_dir(directory->str);
        RecordProperty("scene3d_artifacts", directory->str);
        strbuf_free(directory);
        ASSERT_TRUE(created);
        ShellEnvEntry environment[] = {
            {"LAMBDA_HEADLESS_GLFW_WINDOW", fixture->without_graphics ? nullptr : "1"},
            {"LAMBDA_EVENT_SIM_OUTPUT_DIR", "temp/scene3d"},
            {nullptr, nullptr},
        };
        launch.env = environment;
        test_radiant_view_launch(&launch);
        test_radiant_view_record_launch(launch.label, launch.elapsed_ms,
            launch.viewer_log_path, launch.timing_path);
        ShellResult& result = launch.result;
        EXPECT_FALSE(result.timed_out) << fixture->name;
        EXPECT_FALSE(result.output_limit_exceeded);
        EXPECT_EQ(result.exit_code, fixture->fails ? 1 : 0) << (result.stdout_buf ? result.stdout_buf : "")
            << "\nSee " << launch.viewer_log_path;
        char* report = file_exists(launch.event_result) ? read_text_file(launch.event_result) : nullptr;
        if (fixture->malformed) EXPECT_EQ(report, nullptr) << "Malformed fixtures must fail before execution";
        else EXPECT_NE(report, nullptr) << "Missing event-sim result: " << launch.event_result;
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
    }
};

void test_radiant_view_register_scene3d() {
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
}
