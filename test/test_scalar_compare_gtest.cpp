// =============================================================================
// test_scalar_compare_gtest.cpp
//
// Ordered comparison semantics for <, <=, >, >= — with NaN as the focus.
//
// The bug this suite guards against: `<=` and `>=` were derived by negating
// `>` and `<`. That identity only holds for *ordered* pairs. With a NaN
// operand no relation holds, so both negations answered `true` and
// `nan <= x` was true — contradicting doc/Lambda_Formal_Semantics.md 6.1
// ("poison stays incomparable — nan < x false both ways, IEEE").
//
// What made it dangerous rather than merely wrong: the two lowerings
// DISAGREED. Statically-typed operands lower to native MIR compares, which
// were already IEEE-correct; only `any`-typed operands went through the
// runtime's negating wrappers. So adding or removing a type annotation
// silently flipped the answer. Every case below is therefore asserted on
// BOTH paths and required to agree.
//
// Driven via subprocess (./lambda.exe) like test_js_coerce_gtest, so it
// validates the whole path including JIT lowering — which is the only level
// at which "native and runtime agree" is even expressible.
//
// The companion script fixture test/lambda/numeric_fastpath_edges.ls pins the
// same semantics as a golden; this suite adds named cases and precise failure
// messages so a regression says which relation broke on which path.
// =============================================================================

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
    #include <direct.h>
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

namespace {

const char* kTempDir = "./temp/scalar_compare";

void ensure_temp_dir() {
    struct stat st;
    if (stat("./temp", &st) != 0) {
#ifdef _WIN32
        _mkdir("./temp");
#else
        mkdir("./temp", 0755);
#endif
    }
    if (stat(kTempDir, &st) != 0) {
#ifdef _WIN32
        _mkdir(kTempDir);
#else
        mkdir(kTempDir, 0755);
#endif
    }
}

std::string write_snippet(const char* test_name, const std::string& source) {
    ensure_temp_dir();
    std::string path = std::string(kTempDir) + "/" + test_name + ".ls";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return std::string();
    fwrite(source.data(), 1, source.size(), f);
    fclose(f);
    return path;
}

std::string run_script(const std::string& path, int* exit_code) {
    const char* args[] = {LAMBDA_EXE, path.c_str(), "--no-log", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult res = shell_exec(LAMBDA_EXE, args, &options);
    std::string out;
    if (res.stdout_buf) out.assign(res.stdout_buf, res.stdout_len);
    if (exit_code) *exit_code = res.exit_code;
    shell_result_free(&res);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                            out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

// Emits the four ordered relations for `a` and `b` twice: once through
// statically-typed params (native MIR compares) and once through untyped
// params (the runtime comparison path). Returns the two result lines.
struct BothPaths {
    std::string native;
    std::string runtime;
};

BothPaths eval_both_paths(const char* test_name, const char* type_annot,
                          const char* a, const char* b) {
    std::string src;
    src += "fn native_ops(x: ";  src += type_annot;
    src += ", y: ";              src += type_annot;
    src += ") => [x < y, x <= y, x > y, x >= y]\n";
    src += "fn any_ops(x, y) => [x < y, x <= y, x > y, x >= y]\n";
    src += "native_ops("; src += a; src += ", "; src += b; src += ")\n";
    src += "any_ops(";    src += a; src += ", "; src += b; src += ")\n";

    std::string path = write_snippet(test_name, src);
    EXPECT_FALSE(path.empty()) << "could not write snippet to " << kTempDir;
    if (path.empty()) return BothPaths{};

    int code = -1;
    std::string out = run_script(path, &code);
    EXPECT_EQ(code, 0) << "non-zero exit for " << test_name << "\n" << out;

    BothPaths r;
    size_t nl = out.find('\n');
    if (nl == std::string::npos) {
        ADD_FAILURE() << "expected two result lines for " << test_name
                      << ", got:\n" << out;
        return r;
    }
    r.native = out.substr(0, nl);
    r.runtime = out.substr(nl + 1);
    while (!r.native.empty() && (r.native.back() == '\r')) r.native.pop_back();
    return r;
}

// `expected` is the [lt, le, gt, ge] row both paths must produce.
void ExpectBothPaths(const char* test_name, const char* type_annot,
                     const char* a, const char* b, const char* expected) {
    BothPaths r = eval_both_paths(test_name, type_annot, a, b);
    EXPECT_EQ(r.native, expected)
        << test_name << ": statically-typed operands (native MIR compares)\n"
        << "  " << a << " ? " << b;
    EXPECT_EQ(r.runtime, expected)
        << test_name << ": any-typed operands (runtime comparison path)\n"
        << "  " << a << " ? " << b;
    EXPECT_EQ(r.native, r.runtime)
        << test_name << ": the two lowerings disagree — an annotation would "
        << "change the answer\n  " << a << " ? " << b;
}

// [lt, le, gt, ge] rows.
const char* kLess      = "[true, true, false, false]";
const char* kEqual     = "[false, true, false, true]";
const char* kGreater   = "[false, false, true, true]";
const char* kUnordered = "[false, false, false, false]";

const char* kNan = "(0.0 / 0.0)";
const char* kInf = "(1.0 / 0.0)";
const char* kNegInf = "(-1.0 / 0.0)";

}  // namespace

// =============================================================================
// 1. NaN is unordered — the regression this suite exists for.
// =============================================================================

TEST(ScalarCompareNan, NanVersusFloat) {
    ExpectBothPaths("NanVersusFloat_a", "float", kNan, "1.0", kUnordered);
    ExpectBothPaths("NanVersusFloat_b", "float", "1.0", kNan, kUnordered);
}

TEST(ScalarCompareNan, NanVersusNan) {
    ExpectBothPaths("NanVersusNan", "float", kNan, kNan, kUnordered);
}

TEST(ScalarCompareNan, NanVersusZeroAndNegative) {
    ExpectBothPaths("NanVersusZero", "float", kNan, "0.0", kUnordered);
    ExpectBothPaths("NanVersusNeg", "float", kNan, "-7.5", kUnordered);
}

TEST(ScalarCompareNan, NanVersusInfinities) {
    ExpectBothPaths("NanVersusInf", "float", kNan, kInf, kUnordered);
    ExpectBothPaths("NanVersusNegInf", "float", kNan, kNegInf, kUnordered);
    ExpectBothPaths("InfVersusNan", "float", kInf, kNan, kUnordered);
}

// The precise defect: `<=` must not be `!(>)`. For an unordered pair BOTH
// strict relations are false, so a negating implementation reports `<=` and
// `>=` true. This case fails loudly if anyone reintroduces that derivation.
TEST(ScalarCompareNan, LeAndGeAreNotNegatedStrictRelations) {
    BothPaths r = eval_both_paths("LeGeNotNegated", "float", kNan, "1.0");
    EXPECT_EQ(r.runtime, kUnordered)
        << "le/ge must be read from the ordering, not derived by negating "
           "gt/lt: an unordered pair satisfies neither";
    EXPECT_EQ(r.native, kUnordered);
}

// NaN reaching the comparison from arithmetic rather than a literal division,
// so constant folding cannot mask the runtime path.
TEST(ScalarCompareNan, NanFromArithmeticIsStillUnordered) {
    ExpectBothPaths("NanFromArith", "float", "(0.0 / 0.0 + 1.0)", "1.0", kUnordered);
    ExpectBothPaths("NanFromInfSub", "float", "(1.0 / 0.0 - 1.0 / 0.0)", "0.0", kUnordered);
}

// =============================================================================
// 2. Ordered pairs still behave — the fix must not weaken normal comparison.
// =============================================================================

TEST(ScalarCompareOrdered, Floats) {
    ExpectBothPaths("FloatLess", "float", "0.25", "0.5", kLess);
    ExpectBothPaths("FloatGreater", "float", "0.5", "0.25", kGreater);
    ExpectBothPaths("FloatEqual", "float", "0.5", "0.5", kEqual);
}

TEST(ScalarCompareOrdered, NegativeZeroEqualsZero) {
    ExpectBothPaths("NegZero", "float", "-0.0", "0.0", kEqual);
}

TEST(ScalarCompareOrdered, Integers) {
    ExpectBothPaths("IntLess", "int", "-1", "1", kLess);
    ExpectBothPaths("IntGreater", "int", "1", "-1", kGreater);
    ExpectBothPaths("IntEqual", "int", "42", "42", kEqual);
}

TEST(ScalarCompareOrdered, Infinities) {
    ExpectBothPaths("NegInfLessInf", "float", kNegInf, kInf, kLess);
    ExpectBothPaths("InfEqualInf", "float", kInf, kInf, kEqual);
}

// =============================================================================
// 3. Mixed int/float — the exact-comparison paths added by the M1 fast path.
//    Only the runtime lowering is exercised here (a single annotation cannot
//    describe both operands), so these assert the runtime row directly.
// =============================================================================

namespace {
std::string eval_any_row(const char* test_name, const char* a, const char* b) {
    std::string src = "fn any_ops(x, y) => [x < y, x <= y, x > y, x >= y]\n";
    src += "any_ops("; src += a; src += ", "; src += b; src += ")\n";
    std::string path = write_snippet(test_name, src);
    EXPECT_FALSE(path.empty());
    if (path.empty()) return std::string();
    int code = -1;
    std::string out = run_script(path, &code);
    EXPECT_EQ(code, 0) << "non-zero exit for " << test_name << "\n" << out;
    return out;
}
}  // namespace

TEST(ScalarCompareMixed, FractionalPartsDecideAgainstIntegers) {
    EXPECT_EQ(eval_any_row("Frac_2_5_vs_2", "2.5", "2"), kGreater);
    EXPECT_EQ(eval_any_row("Frac_2_5_vs_3", "2.5", "3"), kLess);
    EXPECT_EQ(eval_any_row("Frac_neg2_5_vs_neg2", "-2.5", "-2"), kLess);
    EXPECT_EQ(eval_any_row("Frac_neg2_5_vs_neg3", "-2.5", "-3"), kGreater);
    EXPECT_EQ(eval_any_row("Frac_3_0_vs_3", "3.0", "3"), kEqual);
}

// The reason integers are never routed through binary64: that cast rounds
// above 2^53, which would make these two values compare equal.
TEST(ScalarCompareMixed, ExactAboveTwoToThe53) {
    EXPECT_EQ(eval_any_row("Exact53_lt", "9007199254740992.0", "9007199254740993i64"),
              kLess) << "2^53 vs 2^53+1 must not collapse to equal";
    EXPECT_EQ(eval_any_row("Exact53_gt", "9007199254740993i64", "9007199254740992.0"),
              kGreater);
    EXPECT_EQ(eval_any_row("Exact53_eq", "9007199254740992.0", "9007199254740992i64"),
              kEqual);
}

TEST(ScalarCompareMixed, UnsignedRangeBeyondInt64) {
    EXPECT_EQ(eval_any_row("U64_vs_float", "18446744073709551615u64",
                           "1.8446744073709552e19"), kLess);
    EXPECT_EQ(eval_any_row("U64_zero_vs_negzero", "0u64", "-0.0"), kEqual);
    EXPECT_EQ(eval_any_row("U64_zero_vs_neghalf", "0u64", "-0.5"), kGreater);
}

TEST(ScalarCompareMixed, InfinitiesAgainstIntegers) {
    EXPECT_EQ(eval_any_row("Inf_vs_i64max", kInf, "9223372036854775807i64"), kGreater);
    EXPECT_EQ(eval_any_row("NegInf_vs_i64min", kNegInf, "-9223372036854775807i64"), kLess);
}

// Decimal operands keep the decimal relation (they do not take the float fast
// path), so the fix must leave them untouched.
TEST(ScalarCompareMixed, DecimalRelationPreserved) {
    EXPECT_EQ(eval_any_row("Decimal_vs_float", "0.1m", "0.1"), kEqual);
    EXPECT_EQ(eval_any_row("Decimal_vs_zero", "0.1m", "0"), kGreater);
    EXPECT_EQ(eval_any_row("Bigint_vs_int", "1n", "1"), kEqual);
}

// =============================================================================
// 4. Elementwise keyword comparisons share the same ordering, so a NaN lane
//    must be false for every ordered operator there too.
// =============================================================================

TEST(ScalarCompareElementwise, NanLaneIsFalseForAllOrderedOps) {
    std::string src =
        "let ew = [1.0, 0.0 / 0.0, 2.0]\n"
        "ew lt 1.5\n"
        "ew le 1.5\n"
        "ew gt 1.5\n"
        "ew ge 1.5\n";
    std::string path = write_snippet("ElementwiseNan", src);
    ASSERT_FALSE(path.empty());
    int code = -1;
    std::string out = run_script(path, &code);
    EXPECT_EQ(code, 0) << out;
    // Middle lane is NaN in every row; the outer lanes prove the operator
    // still discriminates.
    EXPECT_EQ(out,
        "[true, false, false]\n"
        "[true, false, false]\n"
        "[false, false, true]\n"
        "[false, false, true]")
        << "a NaN lane must be false for lt/le/gt/ge alike";
}
