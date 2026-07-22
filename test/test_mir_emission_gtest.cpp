// test_mir_emission_gtest.cpp
//
// Lambda MIR emission fixtures (MT1). Every `test/mir/lambda/*.ls` script is
// compiled with --transpile-only and its finalized MIR artifact is checked
// against the matching `.mir-check` sidecar.
//
// See vibe/Lambda_Design_MIR_Emission_Test.md and test_mir_check_helpers.hpp.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_mir_check_helpers.hpp"

static const char* kLambdaMirDir = "test/mir/lambda";

static std::vector<mir_check::Fixture> collect_lambda_fixtures() {
    return mir_check::discover_fixtures(kLambdaMirDir, ".ls");
}

// populated before the INSTANTIATE_TEST_SUITE_P below runs: both are static
// initializers in this translation unit and run in declaration order.
static std::vector<mir_check::Fixture> g_lambda_mir_fixtures = collect_lambda_fixtures();

class LambdaMirEmissionTest : public ::testing::TestWithParam<mir_check::Fixture> {};

TEST_P(LambdaMirEmissionTest, MatchesMirCheck) {
    const mir_check::Fixture& fixture = GetParam();
    std::string unused;
    // a fixture without a sidecar asserts nothing; fail loudly instead of
    // letting it look like passing coverage.
    ASSERT_TRUE(mir_check::read_file_text(fixture.sidecar_path, &unused))
        << "fixture " << fixture.script_path << " has no sidecar at " << fixture.sidecar_path;
    mir_check::run_fixture(fixture.script_path, fixture.sidecar_path, mir_check::LANG_LAMBDA);
}

static std::string fixture_name(const ::testing::TestParamInfo<mir_check::Fixture>& info) {
    return info.param.name;
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(LambdaMirEmissionTest);

INSTANTIATE_TEST_SUITE_P(Fixtures, LambdaMirEmissionTest,
                         ::testing::ValuesIn(g_lambda_mir_fixtures), fixture_name);

// the corpus is the point of this binary, so an empty directory (a bad path, a
// botched merge) must not read as a green run.
TEST(LambdaMirEmissionCorpus, IsNotEmpty) {
    EXPECT_FALSE(g_lambda_mir_fixtures.empty())
        << "no .ls fixtures found in " << kLambdaMirDir;
}
