// test_js_mir_emission_gtest.cpp
//
// LambdaJS MIR emission fixtures (MT1). Every `test/mir/js/*.js` script is run
// through `lambda.exe js` and its finalized MIR artifact is checked against the
// matching `.mir-check` sidecar.
//
// JS fixtures execute in v1: there is no JS compile-only entry point yet, so a
// fixture script must be able to run to completion.
//
// See vibe/Lambda_Design_MIR_Emission_Test.md and test_mir_check_helpers.hpp.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_mir_check_helpers.hpp"

static const char* kJsMirDir = "test/mir/js";

static std::vector<mir_check::Fixture> collect_js_fixtures() {
    return mir_check::discover_fixtures(kJsMirDir, ".js");
}

static std::vector<mir_check::Fixture> g_js_mir_fixtures = collect_js_fixtures();

class JsMirEmissionTest : public ::testing::TestWithParam<mir_check::Fixture> {};

TEST_P(JsMirEmissionTest, MatchesMirCheck) {
    const mir_check::Fixture& fixture = GetParam();
    std::string unused;
    ASSERT_TRUE(mir_check::read_file_text(fixture.sidecar_path, &unused))
        << "fixture " << fixture.script_path << " has no sidecar at " << fixture.sidecar_path;
    mir_check::run_fixture(fixture.script_path, fixture.sidecar_path, mir_check::LANG_JS);
}

static std::string fixture_name(const ::testing::TestParamInfo<mir_check::Fixture>& info) {
    return info.param.name;
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(JsMirEmissionTest);

INSTANTIATE_TEST_SUITE_P(Fixtures, JsMirEmissionTest,
                         ::testing::ValuesIn(g_js_mir_fixtures), fixture_name);

TEST(JsMirEmissionCorpus, IsNotEmpty) {
    EXPECT_FALSE(g_js_mir_fixtures.empty())
        << "no .js fixtures found in " << kJsMirDir;
}
