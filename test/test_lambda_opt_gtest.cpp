// Focused Lambda optimization-contract tests: runtime boundary admission.
//
// Each fixture runs one small Lambda script in a child process with
// COW_EXEC_PROFILE=1 and asserts the admission path taken — counters the .ls
// golden suite cannot observe — in addition to the semantic output. The
// profile is env-gated (no dedicated build flag), so lambda.exe is the default
// host; callers may override it through LAMBDA_JS_OPT_EXE.
//
// What the counters pin (Tune19 §11.5 recursive-record adoption, D2.2.2):
//  - a self-referential record contract passes the adoption gate
//    (mir_map_contract_storage_valid), so under the JIT tier every declared
//    crossing is proven statically: ZERO runtime admissions;
//  - under the interp tier fresh destination construction and immutable
//    boundary plans reuse maintained proofs — never reify or copy;
//  - an ANY-bearing construction (union field) is REFUSED by the gate and
//    reifies at constructor boundaries; later crossings can reuse its admitted
//    representation. Without concrete field storage, adopting the construction
//    would misaddress direct MIR reads (Tune19 §11.3), so refusal stays pinned.
//
// The v33→v34 self-reference regression (type-pattern name degraded to ANY,
// fixed 2026-08-25) inverted the recursive fixtures' signature: 20 admissions
// with 20 reifications where the fixed build has zero. These tests hold that
// distinction pinned.
//
// Test-only code: std:: containers are allowed here (the lib/ types rule
// governs lambda/ and radiant/ production code), matching
// test_mir_check_helpers.hpp.

#include <gtest/gtest.h>
#include "test_lambda_tier_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define OPT_ACCESS _access
#define OPT_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define OPT_ACCESS access
#define OPT_MKDIR(path) mkdir(path, 0755)
#endif

extern "C" {
#include "../lib/shell.h"
#include "../lib/file.h"
}

namespace {

static const char* kOptDir = "./temp/lambda_opt_contract";

static void ensure_opt_dir() {
    if (OPT_ACCESS("./temp", 0) != 0) OPT_MKDIR("./temp");
    if (OPT_ACCESS(kOptDir, 0) != 0) OPT_MKDIR(kOptDir);
}

static bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << text;
    return out.good();
}

// Scalar counters from the COW exec profile TSV. The per-type table rows
// (5 tab-separated columns) are skipped; only `name\tvalue` rows are counters.
struct AdmitProfile {
    std::map<std::string, uint64_t> counters;

    uint64_t get(const std::string& name) const {
        std::map<std::string, uint64_t>::const_iterator it = counters.find(name);
        return it == counters.end() ? 0 : it->second;
    }
    bool has(const std::string& name) const {
        return counters.find(name) != counters.end();
    }
};

static bool parse_profile(const std::string& path, AdmitProfile* out) {
    std::ifstream in(path.c_str());
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t first_tab = line.find('\t');
        if (first_tab == std::string::npos) continue;
        std::string key = line.substr(0, first_tab);
        if (line.find('\t', first_tab + 1) != std::string::npos) {
            // per-type COW table row: `<type>\tshare_marks\tunique_mutations\tshared_copies\tcopied_bytes`
            // (the header row's cells are not numbers and are skipped below).
            // Exposed as `<type>_share_marks` etc.; `array[num]` spells `array_num`.
            static const char* kColumns[] = {"share_marks", "unique_mutations", "shared_copies", "copied_bytes"};
            std::string type_key;
            for (size_t i = 0; i < key.size(); i++) {
                char c = key[i];
                if (c == '[') type_key += '_';
                else if (c == ']') continue;
                else type_key += c;
            }
            size_t pos = first_tab + 1;
            for (int col = 0; col < 4 && pos <= line.size(); col++) {
                size_t next = line.find('\t', pos);
                std::string cell = line.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
                char* cell_end = NULL;
                unsigned long long cell_value = strtoull(cell.c_str(), &cell_end, 10);
                if (!cell.empty() && cell_end && *cell_end == '\0') {
                    out->counters[type_key + "_" + kColumns[col]] = (uint64_t)cell_value;
                }
                if (next == std::string::npos) break;
                pos = next + 1;
            }
            continue;
        }
        std::string value = line.substr(first_tab + 1);
        if (value.empty()) continue;
        char* end = NULL;
        unsigned long long parsed = strtoull(value.c_str(), &end, 10);
        if (!end || *end != '\0') continue;
        out->counters[key] = (uint64_t)parsed;
    }
    // an empty or headers-only file means the child never dumped its counters
    return !out->counters.empty();
}

// run against lambda.exe by default; callers may select another compatible
// host through LAMBDA_JS_OPT_EXE.
static const char* opt_executable() {
    const char* configured = getenv("LAMBDA_JS_OPT_EXE");
    if (configured && configured[0]) return configured;
#ifdef _WIN32
    return "lambda.exe";
#else
    return "./lambda.exe";
#endif
}

struct FixtureRun {
    bool ok = false;
    std::string std_out;
    std::string std_err;
    AdmitProfile profile;
    std::string profile_path;
    std::string sites_path;
};

// Runs `lambda.exe run --tier=<tier> <fixture>` in a child with the COW exec
// profile pointed at a private TSV. The tier is pinned explicitly: the JIT
// tier must prove admitted recursive boundaries statically, while the interp
// tier exercises the runtime relation classifier — letting the auto tier
// choose would make the counters depend on its heuristics.
// `tier == NULL` runs the script through the `js` subcommand (LambdaJS), so
// its exec-profile rows -- the same TSV -- can be pinned by the same reader.
static FixtureRun run_fixture_script(const char* name, const char* tier,
                                     const std::string& script_path, bool profile_enabled,
                                     bool sites_enabled = false) {
    FixtureRun run;
    ensure_opt_dir();
    bool js = tier == NULL;
    run.profile_path = std::string(kOptDir) + "/" + name + "_" + (js ? "js" : tier) +
        (profile_enabled ? "" : "_off") + ".tsv";
    run.sites_path = std::string(kOptDir) + "/" + name + "_" + (js ? "js" : tier) +
        (profile_enabled ? "" : "_off") + "_sites.tsv";
    remove(run.profile_path.c_str());
    remove(run.sites_path.c_str());

    std::string tier_arg = std::string("--tier=") + (js ? "" : tier);
    const char* executable = opt_executable();
    const char* run_args[] = {executable, "run", tier_arg.c_str(), script_path.c_str(), NULL};
    const char* js_args[] = {executable, "js", script_path.c_str(), NULL};
    const char** args = js ? js_args : run_args;
    ShellEnvEntry env[] = {
        {"COW_EXEC_PROFILE", profile_enabled ? "1" : "0"},
        {"COW_EXEC_PROFILE_OUT", run.profile_path.c_str()},
        {"COW_EXEC_PROFILE_SITES", sites_enabled ? "1" : "0"},
        {"COW_EXEC_PROFILE_SITES_OUT", run.sites_path.c_str()},
        // a module-cache hit would reuse prior emission; keep each fixture
        // child hermetic so tier pinning always takes effect
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    options.merge_stderr = false;
    ShellResult result = shell_exec(executable, args, &options);
    if (result.stdout_buf) run.std_out.assign(result.stdout_buf);
    if (result.stderr_buf) run.std_err.assign(result.stderr_buf);
    bool exited_clean = result.exit_code == 0 && !result.timed_out;
    int exit_code = result.exit_code;
    bool timed_out = result.timed_out;
    shell_result_free(&result);

    if (!exited_clean) {
        ADD_FAILURE() << "fixture '" << name << "' (" << (js ? "js" : tier) << "): child "
            << executable << " exited " << exit_code
            << (timed_out ? " (timed out)" : "")
            << "\n--- stderr ---\n" << run.std_err;
        return run;
    }
    if (!profile_enabled) {
        run.ok = true;
        return run;
    }
    if (!parse_profile(run.profile_path, &run.profile)) {
        // The dump is written by an atexit hook; a clean exit with no TSV
        // means the profile env plumbing broke, not that counters were zero.
        ADD_FAILURE() << "fixture '" << name << "' (" << tier
            << "): ran cleanly but wrote no usable profile to " << run.profile_path;
        return run;
    }
    run.ok = true;
    return run;
}

static FixtureRun run_fixture(const char* name, const char* tier,
                              const std::string& source, bool profile_enabled,
                              bool sites_enabled = false) {
    ensure_opt_dir();
    std::string script_path = std::string(kOptDir) + "/" + name + (tier ? ".ls" : ".js");
    if (!write_text(script_path, source)) {
        ADD_FAILURE() << "cannot write fixture script " << script_path;
        return {};
    }
    return run_fixture_script(name, tier, script_path, profile_enabled,
        sites_enabled);
}

// a checked-in script runs in place: its relative imports (S16.9.8) resolve
// beside the file, so a copy under the fixture dir would lose its modules
static FixtureRun run_source_fixture(const char* name, const char* source_path,
        const char* tier) {
    EXPECT_TRUE(file_exists(source_path)) << source_path;
    return run_fixture_script(name, tier, source_path, true);
}

TEST(LambdaOptGc, ContradictoryPlannedEffectFailsBeforePublication) {
    const char* executable = opt_executable();
    const char* args[] = {executable, "run", "--tier=jit",
        "test/mir/lambda/gc_effect_forward_allocating.ls", NULL};
    ShellEnvEntry env[] = {
        {"LAMBDA_TEST_GC_PLAN_CLAIM", "gc_order_alloc_leaf"},
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    ShellResult result = shell_exec(executable, args, &options);
    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0);
    const char* diagnostic = result.stderr_buf ? result.stderr_buf : "";
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_TRUE(strstr(diagnostic, "mir-gc-verify: collecting edge") ||
        strstr(output, "mir-gc-verify: collecting edge"))
        << diagnostic << output;
    shell_result_free(&result);
}

TEST(LambdaOptGc, RootlessMirStackProbeStillRecoversOverflow) {
    const char* executable = opt_executable();
    const char* args[] = {executable, "--tier=jit",
        "test/lambda/negative/test_stack_overflow.ls", NULL};
    ShellEnvEntry env[] = {
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    ShellResult result = shell_exec(executable, args, &options);
    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.exit_code, 0);
    const char* diagnostic = result.stderr_buf ? result.stderr_buf : "";
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_TRUE(strstr(diagnostic, "error[E308]: Stack overflow") ||
        strstr(output, "error[E308]: Stack overflow"))
        << diagnostic << output;
    shell_result_free(&result);
}

TEST(LambdaOptStrings, LengthObservationRetainsGeometricGrowth) {
    auto run = run_source_fixture("string_observer",
        "test/mir/lambda/string_builder_observer.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "8384\n");
    EXPECT_EQ(run.profile.get("string_append_calls"), 128u);
    EXPECT_EQ(run.profile.get("string_freezes"), 0u);
    EXPECT_EQ(run.profile.get("string_generic_joins"), 0u);
    EXPECT_LE(run.profile.get("string_growth_copies"), 9u);
    EXPECT_LT(run.profile.get("string_copied_bytes"), 512u);
}

TEST(LambdaOptStrings, TailAccumulatorCopiesLinearBytes) {
    auto run = run_source_fixture("string_tail",
        "test/mir/lambda/string_builder_tail.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_NE(strstr(run.std_out.c_str(), "129\ns\n"), nullptr);
    EXPECT_NE(strstr(run.std_out.c_str(), "xx:xxx"), nullptr);
    EXPECT_GE(run.profile.get("string_inplace_appends"), 120u);
    EXPECT_LT(run.profile.get("string_copied_bytes"), 1024u);
    EXPECT_GT(run.profile.get("string_freezes"), 0u);
}

// Tune31 Phase II: S7.1.2 string spans avoid a call per character, and
// D3.3.3v3 admits the flat table record once before the repeated traversal.
TEST(LambdaOptStrings, TypedHyphenUsesStringSpansAndOneTableAdmission) {
    auto run = run_source_fixture("tune31_hyphen_typed_benchmark",
        "test/benchmark/text/hyphen2.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_NE(strstr(run.std_out.c_str(), "hyphen: CHECKSUM:1183296\n"), nullptr);
    EXPECT_EQ(strstr(run.std_out.c_str(), "FAIL"), nullptr);
    ASSERT_TRUE(run.profile.has("string_append_calls"));
    ASSERT_TRUE(run.profile.has("map_admit_calls"));
    // 11,092 appends after the span rewrite, versus 71,086 in the old core.
    // Leave headroom for boundary changes while rejecting character-by-character output.
    EXPECT_LT(run.profile.get("string_append_calls"), 15000u);
    EXPECT_LE(run.profile.get("map_admit_calls"), 2u);
}

// The typed log port keeps its generated string corpus, but parses each row
// into scalar state instead of repeatedly admitting and mutating LogRecord
// maps. D3.3.3v3 permits this representation choice within the closed
// benchmark schema; the checksum remains the behavioral oracle.
TEST(LambdaOptStrings, TypedLogPipelineStreamsWithoutRecordMapTraffic) {
    auto run = run_source_fixture("log_pipeline_typed_stream",
        "test/benchmark/text/log_pipeline2.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_NE(strstr(run.std_out.c_str(), "log_pipeline: CHECKSUM:292634526\n"), nullptr);
    EXPECT_EQ(strstr(run.std_out.c_str(), "FAIL"), nullptr);
    EXPECT_EQ(run.profile.get("map_admit_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_unique_mutations"), 0u);
    EXPECT_EQ(run.profile.get("map_shared_copies"), 0u);
}

// The fixed benchmark corpus has the closed A/C/G/T domain represented by
// D3.3.3v3's admitted int[] lanes. Keep the full output oracle so table
// ordering and the longer literal-query scans cannot be simplified away.
TEST(LambdaOptStrings, TypedKnucleotideUsesClosedAlphabetTables) {
    auto run = run_source_fixture("knucleotide_typed_table",
        "test/benchmark/beng/knucleotide2.ls", "jit");
    ASSERT_TRUE(run.ok);
    size_t timing = run.std_out.find("__TIMING__:");
    ASSERT_NE(timing, std::string::npos);
    char* expected_text = read_text_file("test/benchmark/beng/knucleotide2.txt");
    ASSERT_NE(expected_text, nullptr);
    if (!expected_text) return;
    // Benchmark goldens retain the timing line's terminating blank line. The
    // child exposes the marker so tests can replace that line with its newline.
    EXPECT_EQ(run.std_out.substr(0, timing) + "\n", expected_text);
    free(expected_text);
    EXPECT_LT(run.profile.get("string_generic_joins"), 100u);
    char* source_text = read_text_file("test/benchmark/beng/knucleotide2.ls");
    ASSERT_NE(source_text, nullptr);
    if (!source_text) return;
    std::string source(source_text);
    free(source_text);
    EXPECT_NE(source.find("fill(20, 0)"), std::string::npos);
    EXPECT_EQ(source.find("var counts = map()"), std::string::npos);
}

// Fast Diff admits its fixed source strings once, then its repeated LCS loop
// compares D3.3.3v3 int[] lanes. The companion MIR fixture pins the raw
// equality arm; this benchmark-level test pins the complete checksum.
TEST(LambdaOptStrings, TypedFastDiffPrecodesStaticTextBeforeLcsLoop) {
    auto run = run_source_fixture("fast_diff_typed_code_tables",
        "test/benchmark/text/fast_diff2.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_NE(strstr(run.std_out.c_str(), "fast_diff: CHECKSUM:748544\n"), nullptr);
    EXPECT_EQ(strstr(run.std_out.c_str(), "FAIL"), nullptr);
    char* source_text = read_text_file("test/benchmark/text/fast_diff2.ls");
    ASSERT_NE(source_text, nullptr);
    if (!source_text) return;
    std::string source(source_text);
    free(source_text);
    EXPECT_NE(source.find("pn score_diff(left: int[], right: int[]) int"), std::string::npos);
    EXPECT_NE(source.find("pn text_codes(text: string) int[]"), std::string::npos);
    EXPECT_NE(source.find("score_diff(left_codes[index], right_codes[index])"),
        std::string::npos);
}

// Tune31 Phase II F: an inferred numeric parameter does not gain a source
// contract, but its proven raw ArrayNum witness must keep loop stores out of
// the representation-agnostic COW setter after the one snapshot detach.
TEST(LambdaOptStores, InferredFloatArrayKeepsNativeStoreHotPath) {
    auto run = run_source_fixture("tune31_inferred_float_store",
        "test/mir/lambda/tune31_inferred_float_store.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "5 1 1\n\n");
    EXPECT_EQ(run.profile.get("array_num_shared_copies"), 1u);
    EXPECT_EQ(run.profile.get("array_num_unique_mutations"), 0u);
}

// Tune31 Phase II F: a caller-visible inferred `var` array keeps the same
// native success arm after nullable intermediate arithmetic, while the first
// write still detaches its snapshot through the established COW fallback.
TEST(LambdaOptStores, InferredVarFloatArrayKeepsNativeStoreHotPath) {
    auto run = run_source_fixture("tune31_inferred_var_float_store",
        "test/mir/lambda/tune31_inferred_var_float_store.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[2, 3, 4] [3, 4, 5]\n\n");
    EXPECT_EQ(run.profile.get("array_num_shared_copies"), 1u);
    EXPECT_EQ(run.profile.get("array_num_unique_mutations"), 0u);
}

// Tune31 Phase II A: a snapshot causes one root detach at the outer `var`
// call boundary. The repeated field re-borrow must then prepare only its
// ArrayNum child; a no-op map preparation per loop trip is a regression.
TEST(LambdaOptCow, VarPathBorrowAvoidsRepeatedUniqueRootPreparation) {
    auto run = run_source_fixture("tune31_var_path_borrow",
        "test/mir/lambda/tune31_var_path_borrow.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "4 0\n\n");
    EXPECT_EQ(run.profile.get("map_shared_copies"), 1u);
    EXPECT_EQ(run.profile.get("map_unique_mutations"), 0u);
    EXPECT_EQ(run.profile.get("array_num_shared_copies"), 1u);
}

// Tune28 T28-5: literal string separators search for the next first-byte hit
// and compare only multi-byte candidates. S17.1.1 keeps the segments and
// delimiters observable while the structural check pins the non-bytewise scan.
TEST(LambdaOptStrings, LiteralSplitKernelAvoidsBytewiseComparisons) {
    auto run = run_source_fixture("tune28_split_literal_kernel",
        "test/lambda/proc/tune28_split_literal_kernel.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out,
        "[\"aXb\", \"c\"]\n[\"aXb\", \"XX\", \"c\"]\n[\"a\", \"b\", \"\", \"c\"]\n\n");
    char* runtime_text = read_text_file("lambda/runtime/lambda-eval.cpp");
    ASSERT_NE(runtime_text, nullptr);
    if (!runtime_text) return;
    std::string runtime_source(runtime_text);
    free(runtime_text);
    // split is a runtime builtin rather than a MIR lowering. Keep the
    // non-bytewise implementation pinned beside its semantic fixture: scan to
    // the next first-byte hit, skip memcmp for one-byte delimiters, and compare
    // only the remaining candidate suffix.
    size_t kernel_start = runtime_source.find("static size_t literal_find(");
    ASSERT_NE(kernel_start, std::string::npos);
    size_t kernel_end = runtime_source.find("\n}\n", kernel_start);
    ASSERT_NE(kernel_end, std::string::npos);
    std::string kernel = runtime_source.substr(kernel_start, kernel_end - kernel_start);
    // The case-sensitive scan is lib's str_find, the one candidate-scan kernel
    // shared with contains/index_of and LambdaJS (string tuning P2).
    EXPECT_NE(kernel.find("str_find(chars + from"), std::string::npos);
    char* lib_text = read_text_file("lib/str.c");
    ASSERT_NE(lib_text, nullptr);
    if (!lib_text) return;
    std::string lib_source(lib_text);
    free(lib_text);
    size_t find_kernel_start = lib_source.find("size_t str_find(const char* s, size_t s_len,");
    ASSERT_NE(find_kernel_start, std::string::npos);
    size_t find_kernel_end = lib_source.find("\n}\n", find_kernel_start);
    ASSERT_NE(find_kernel_end, std::string::npos);
    std::string find_kernel = lib_source.substr(find_kernel_start,
        find_kernel_end - find_kernel_start);
    EXPECT_NE(find_kernel.find("if (needle_len == 1) return str_find_byte("), std::string::npos);
    // GRP8 (vibe/Lambda_Lib_Grep.md): the candidate scan anchors on the
    // needle's rarest byte, or on its two rarest at a fixed distance (the
    // packed pair in lib/str_simd.c), and compares the needle only at a
    // candidate -- still never a comparison at every byte.
    EXPECT_NE(find_kernel.find("str_needle_find(&prepared, s, s_len)"), std::string::npos);
    size_t needle_start = lib_source.find("size_t str_needle_find(const StrNeedle* needle,");
    ASSERT_NE(needle_start, std::string::npos);
    std::string needle_kernel = lib_source.substr(needle_start,
        lib_source.find("\n}\n", needle_start) - needle_start);
    EXPECT_NE(needle_kernel.find("memchr(p, needle->rare"), std::string::npos);
    EXPECT_NE(needle_kernel.find("str_needle_equal_at(needle, start)"), std::string::npos);
    EXPECT_NE(needle_kernel.find("return str_needle_find_pair(needle, s, s_len)"), std::string::npos);
    char* simd_text = read_text_file("lib/str_simd.c");
    ASSERT_NE(simd_text, nullptr);
    if (!simd_text) return;
    std::string simd_source(simd_text);
    free(simd_text);
    EXPECT_NE(simd_source.find("str_needle_equal_at(n, s + i + k)"), std::string::npos);
    // The literal replace() and find() paths share the kernel; before they did,
    // both called memcmp at every byte (four fifths of revcomp).
    size_t replace_start = runtime_source.find("static Item fn_replace_impl(");
    size_t find_start = runtime_source.find("static Item fn_find_impl(");
    ASSERT_NE(replace_start, std::string::npos);
    ASSERT_NE(find_start, std::string::npos);
    size_t replace_end = runtime_source.find("\n}\n", replace_start);
    size_t find_end = runtime_source.find("\n}\n", find_start);
    EXPECT_LT(runtime_source.find("literal_find(", replace_start), replace_end);
    EXPECT_LT(runtime_source.find("literal_find(", find_start), find_end);
}

// A self-referential record contract with a typed recursive traversal. The
// declared boundaries here are the `let node: Node` initializer, the `head`
// stores, and the `depth(n.next)` recursion — 20 nodes' worth per run.
static const char* kRecursiveLinkSource =
    "type Node = {val: int, next: Node?}\n"
    "\n"
    "pn depth(n: Node?) int {\n"
    "    if (n == null) { return 0 }\n"
    "    return 1 + depth(n.next)\n"
    "}\n"
    "\n"
    "pn main() {\n"
    "    var head: Node? = null\n"
    "    for (i in 1 to 20) {\n"
    "        let node: Node = {val: i, next: head}\n"
    "        head = node\n"
    "    }\n"
    "    print(depth(head))\n"
    "}\n";

// One adopted node crossing the same typed boundary 10 times: the candidate
// carries the exact trusted TypeMap, so the relation classifier must answer
// EXACT_TRUSTED on every crossing.
static const char* kShapeIdentitySource =
    "type Node = {val: int, next: Node?}\n"
    "\n"
    "pn total(n: Node) int {\n"
    "    return n.val\n"
    "}\n"
    "\n"
    "pn main() {\n"
    "    let a: Node = {val: 7, next: null}\n"
    "    var acc = 0\n"
    "    for (i in 1 to 10) {\n"
    "        acc = acc + total(a)\n"
    "    }\n"
    "    print(acc)\n"
    "}\n";

// The union field classifies ANY, so mir_map_contract_storage_valid refuses
// the contract and every declared crossing must take the reify path.
static const char* kAnyBearingSource =
    "type Person = {name: string, choice: int | string}\n"
    "\n"
    "pn describe(p: Person) string {\n"
    "    return p.name\n"
    "}\n"
    "\n"
    "pn main() {\n"
    "    var out = \"\"\n"
    "    for (i in 1 to 5) {\n"
    "        let p: Person = {name: \"n\" ++ i, choice: i}\n"
    "        out = describe(p)\n"
    "    }\n"
    "    print(out)\n"
    "}\n";

static void expect_no_unresolved_type_warning(const FixtureRun& run, const char* name) {
    // the v33→v34 regression's tell: the recursive field silently became ANY
    EXPECT_EQ(run.std_err.find("unresolved type name"), std::string::npos)
        << "fixture '" << name << "': a type name in the fixture degraded to ANY\n"
        << "--- stderr ---\n" << run.std_err;
}

// JIT tier: every crossing of an admitted recursive contract must be free —
// the relation classifier answers EXACT_TRUSTED off shape identity, so nothing
// reifies and nothing is copied. The v34 ANY-degradation signature for this
// fixture was calls=20, reifications=20; what separates health from that is the
// reification/copy counters, not the call count.
//
// Tune27 T27-8 (S11.4.1v3, D3.2.4v3): the JIT no longer crosses at all. The
// argument `n.next` is a field read of a trusted record whose field contract
// is structurally the parameter's `Node?`, and `head` is a declared binding
// admitted at its own declaration -- both proofs are reached through the
// non-null arm, so the direct edge carries no runtime admission. The v34
// ANY-degradation signature for this fixture was calls=20, reifications=20;
// an unreified literal shape behind a contract-shaped field read is still
// what the warning check and the reification/copy counters guard against.
// The interp tier reuses the same maintained representation proof.
TEST(LambdaOptAdmission, RecursiveContractJitFullyStatic) {
    FixtureRun run = run_fixture("recursive_link", "jit", kRecursiveLinkSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "20\n");
    expect_no_unresolved_type_warning(run, "recursive_link");
    // every crossing that happens is classified trusted
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits") +
              run.profile.get("map_admit_storage_compatible_hits"),
              run.profile.get("map_admit_calls"));
    EXPECT_EQ(run.profile.get("map_admit_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 0u);
}

// T0 destination construction and boundary plans reuse exact shape proofs
// without entering admission or copying the recursive graph (D3.2.4v4).
TEST(LambdaOptAdmission, RecursiveContractInterpAdmitsWithoutCopy) {
    FixtureRun run = run_fixture("recursive_link", "interp", kRecursiveLinkSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "20\n");
    EXPECT_EQ(run.profile.get("map_admit_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits") +
              run.profile.get("map_admit_storage_compatible_hits"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 0u);
}

// Interp tier, one shared node: 10 boundary crossings must all be
// EXACT_TRUSTED — the adopted construction and the declared parameter carry
// the same TypeMap identity.
// A checked-in fixture (with its own golden) run under the census: the COW
// rulings of Tune27 rounds 3-4 are pinned by their copy counts, not only by
// their output, so a lost borrow (S9.2.2 un-share back on the hot path) or a
// lost detach (an aliased write) fails here before a benchmark sees it.
static std::string fixture_source(const char* path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// S4.4.4: MIR's width guard must bypass, rather than re-enter, its shift arm.
TEST(LambdaOptNumeric, U32ShiftWidthZeroArmMatchesInterpreter) {
    const std::string source = fixture_source("test/mir/lambda/u32_shift_width.ls");
    const std::string expected = fixture_source("test/mir/lambda/u32_shift_width.txt");
    ASSERT_FALSE(source.empty());
    ASSERT_FALSE(expected.empty());
    static const char* const tiers[] = {"jit", "interp"};
    for (const char* tier : tiers) {
        FixtureRun run = run_fixture("u32_shift_width", tier, source, false);
        ASSERT_TRUE(run.ok) << tier;
        EXPECT_EQ(run.std_out, expected + "\n") << tier;
    }
}

// D2.4.1-D2.4.3: the guarded identity and the original admission agree on
// observable values at direct and boxed calls.
TEST(LambdaOptNumeric, ExactSizedParameterAdmissionMatchesInterpreter) {
    const std::string source = fixture_source("test/mir/lambda/sized_param_exact_admission.ls");
    const std::string expected = fixture_source("test/mir/lambda/sized_param_exact_admission.txt");
    ASSERT_FALSE(source.empty());
    ASSERT_FALSE(expected.empty());
    static const char* const tiers[] = {"jit", "interp"};
    for (const char* tier : tiers) {
        FixtureRun run = run_fixture("sized_param_exact_admission", tier, source, false);
        ASSERT_TRUE(run.ok) << tier;
        EXPECT_EQ(run.std_out, expected + "\n") << tier;
    }
}

// D5.4.4: diagnostic call counters are emitted only for the opted-in JIT;
// the same source and tier must retain identical observable output.
TEST(LambdaOptProfile, ExecutedCallsAreCountedOnlyWhenEnabled) {
    ensure_opt_dir();
    const char* executable = opt_executable();
    const char* args[] = {executable, "run", "--tier=jit",
        "test/mir/lambda/sized_param_exact_admission.ls", NULL};
    const char* profile_path = "temp/lambda_opt_contract/exec_calls.tsv";
    const char* mir_path = "temp/lambda_opt_contract/exec_calls.mir";
    for (int enabled = 0; enabled < 2; enabled++) {
        remove(profile_path);
        remove(mir_path);
        ShellEnvEntry env[] = {
            {"LAMBDA_EXEC_PROFILE", enabled ? "1" : "0"},
            {"LAMBDA_EXEC_PROFILE_OUT", profile_path},
            {"LAMBDA_MIR_DUMP_PATH", mir_path},
            {"LAMBDA_DISABLE_MIR_CACHE", "1"},
            {NULL, NULL}
        };
        ShellOptions options = {};
        options.env = env;
        options.timeout_ms = 60000;
        ShellResult result = shell_exec(executable, args, &options);
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_FALSE(result.timed_out);
        ASSERT_NE(result.stdout_buf, nullptr);
        EXPECT_NE(strstr(result.stdout_buf,
            "7 7 -1 65535 7 7 2 error error error error error error"), nullptr);
        shell_result_free(&result);

        char* mir = read_text_file(mir_path);
        ASSERT_NE(mir, nullptr);
        if (enabled) {
            EXPECT_NE(strstr(mir, "lambda_exec_profile_note_call"), nullptr);
            char* profile = read_text_file(profile_path);
            ASSERT_NE(profile, nullptr);
            EXPECT_NE(strstr(profile, "raw\t_accept_u32_"), nullptr);
            EXPECT_NE(strstr(profile, "helper\tpn_print\t26"), nullptr);
            EXPECT_NE(strstr(profile, "frame_entry\t_accept_u32_"), nullptr);
            EXPECT_NE(strstr(profile, "root_store\t_main_"), nullptr);
            // NO_GC diagnostic markers do not create production root reloads.
            EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
            free(profile);
        } else {
            EXPECT_EQ(strstr(mir, "lambda_exec_profile_note_call"), nullptr);
            EXPECT_NE(OPT_ACCESS(profile_path, 0), 0);
        }
        free(mir);
    }
}

// D5.4.4: caller attribution is a separate diagnostic compile mode; ordinary
// call profiles retain their aggregate target names.
TEST(LambdaOptProfile, ExecutedCallsCanBeAttributedToCaller) {
    ensure_opt_dir();
    const char* executable = opt_executable();
    const char* args[] = {executable, "run", "--tier=jit",
        "test/mir/lambda/readonly_length_no_gc.ls", NULL};
    const char* profile_path = "temp/lambda_opt_contract/exec_callers.tsv";
    remove(profile_path);
    ShellEnvEntry env[] = {
        {"LAMBDA_EXEC_PROFILE", "1"},
        {"LAMBDA_EXEC_PROFILE_CALLERS", "1"},
        {"LAMBDA_EXEC_PROFILE_OUT", profile_path},
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    ShellResult result = shell_exec(executable, args, &options);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_FALSE(result.timed_out);
    ASSERT_NE(result.stdout_buf, nullptr);
    EXPECT_NE(strstr(result.stdout_buf, "[7, 2, 1]"), nullptr);
    shell_result_free(&result);
    char* profile = read_text_file(profile_path);
    ASSERT_NE(profile, nullptr);
    EXPECT_NE(strstr(profile, "helper\t_shaped__raw0_"), nullptr);
    EXPECT_NE(strstr(profile, "helper\t_shaped__raw1_"), nullptr);
    EXPECT_NE(strstr(profile, " -> fn_len_l\t1\n"), nullptr);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

static uint64_t exec_profile_count_prefix(const char* profile,
        const char* row_prefix) {
    const char* row = profile ? strstr(profile, row_prefix) : nullptr;
    if (!row) return UINT64_MAX;
    const char* count = strchr(row + strlen(row_prefix), '\t');
    return count ? strtoull(count + 1, nullptr, 10) : UINT64_MAX;
}

static uint64_t exec_profile_sum_prefix(const char* profile,
        const char* row_prefix) {
    if (!profile || !row_prefix) return UINT64_MAX;
    uint64_t total = 0;
    size_t prefix_len = strlen(row_prefix);
    const char* cursor = profile;
    while ((cursor = strstr(cursor, row_prefix))) {
        if (cursor == profile || cursor[-1] == '\n') {
            const char* count = strchr(cursor + prefix_len, '\t');
            const char* end = strchr(cursor, '\n');
            if (!count || (end && count >= end)) return UINT64_MAX;
            total += strtoull(count + 1, nullptr, 10);
        }
        cursor += prefix_len;
    }
    return total;
}

static char* run_profiled_lambda_fixture(const char* script,
        const char* expected_output, const char* profile_path,
        bool callers = false, bool procedural = true) {
    ensure_opt_dir();
    const char* executable = opt_executable();
    const char* args[] = {executable,
        procedural ? "run" : script,
        procedural ? "--tier=jit" : NULL,
        procedural ? script : NULL, NULL};
    remove(profile_path);
    ShellEnvEntry env[] = {
        {"LAMBDA_EXEC_PROFILE", "1"},
        {"LAMBDA_EXEC_PROFILE_CALLERS", callers ? "1" : "0"},
        {"LAMBDA_EXEC_PROFILE_OUT", profile_path},
        {"LAMBDA_DISABLE_MIR_CACHE", "1"},
        {"LAMBDA_EXEC_BACKEND", "jit"},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 60000;
    ShellResult result = shell_exec(executable, args, &options);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_FALSE(result.timed_out);
    EXPECT_NE(result.stdout_buf, nullptr);
    if (result.stdout_buf) EXPECT_NE(strstr(result.stdout_buf, expected_output), nullptr);
    shell_result_free(&result);
    return read_text_file(profile_path);
}

// D5.4.3/D8.5.1v2: the current JIT module reads its context-owned slab
// directly, while imported functions keep the checked logical-unit lookup.
TEST(LambdaOptProfile, JitModuleHintRetainsCrossModuleFallback) {
    char* own = run_profiled_lambda_fixture(
        "test/mir/lambda/empty_string_literal_direct.ls",
        "[\"\", 0, true, \"filled\"]",
        "temp/lambda_opt_contract/module_hint_own.tsv", true);
    ASSERT_NE(own, nullptr);
    EXPECT_EQ(strstr(own, " -> lambda_module_state_for_unit"), nullptr);
    EXPECT_NE(strstr(own, "overflow\t-\t0"), nullptr);
    free(own);

    char* imported = run_profiled_lambda_fixture(
        "test/lambda/import_pub_let_same_name.ls",
        "[\"<from-a>\", \"[from-b]\"]",
        "temp/lambda_opt_contract/module_hint_import.tsv", true, false);
    ASSERT_NE(imported, nullptr);
    EXPECT_NE(strstr(imported, "helper\t_who_"), nullptr);
    EXPECT_NE(strstr(imported, " -> lambda_module_state_for_unit\t1\n"), nullptr);
    EXPECT_NE(strstr(imported, "overflow\t-\t0"), nullptr);
    free(imported);

    char* repeated = run_profiled_lambda_fixture(
        "test/lambda/module_state_hint_reuse.ls",
        "[\"<from-a>\", \"<from-a>\", \"<from-a>\"]",
        "temp/lambda_opt_contract/module_hint_reuse.tsv", true, false);
    ASSERT_NE(repeated, nullptr);
    EXPECT_NE(strstr(repeated, "helper\t_framed_"), nullptr);
    EXPECT_NE(strstr(repeated,
        " -> lambda_module_state_for_unit\t1\n"), nullptr);
    EXPECT_NE(strstr(repeated, "overflow\t-\t0"), nullptr);
    free(repeated);
}

// D5.4.4/D3.2.6: a speculative field read reports both guard entries and
// actual fast-arm entries, so a persistent wrong shape is visible in P0.
TEST(LambdaOptProfile, FieldGuardDistinguishesFastAndFallback) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/tune24_branch_proofs.ls", "[10, 28, 28, 56, 0, 5, 20, 40]",
        "temp/lambda_opt_contract/field_guard.tsv", false, false);
    ASSERT_NE(profile, nullptr);
    EXPECT_NE(strstr(profile, "field_attempt\t_total_"), nullptr);
    EXPECT_NE(strstr(profile, "field_fast\t_total_"), nullptr);
    EXPECT_NE(strstr(profile, "@160.value\t31\n"), nullptr);
    EXPECT_NE(strstr(profile, "@160.value\t1\n"), nullptr);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D3.3.3v3/S8.2.4v3: an open array erases its elements' declared layout, so
// only the exact runtime shape may take the packed-field read.
TEST(LambdaOptProfile, OpenArrayTrustedRecordFieldKeepsGenericFallback) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/open_array_trusted_record_field.ls",
        "[7, 17, null, null]\n",
        "temp/lambda_opt_contract/open_array_trusted_record_field.tsv", true);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "field_attempt\t_read_count_"), 4u);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "field_fast\t_read_count_"), 1u);
    EXPECT_NE(strstr(profile, " -> fn_member_by_id\t3\n"), nullptr);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D3.3.3v3/S7.1.1v3: packed ints after bool fields are read by bytes only
// after an exact shape hit; inferred shape and out-of-range cases stay generic.
TEST(LambdaOptProfile, UnalignedTrustedArrayIntFieldGuardsAndFallsBack) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/trusted_array_dynamic_index_field.ls",
        "[-7, null]\n",
        "temp/lambda_opt_contract/unaligned_trusted_array_int.tsv", true);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile, "field_attempt\t_task_id_"), 4u);
    EXPECT_EQ(exec_profile_count_prefix(profile, "field_fast\t_task_id_"), 1u);
    EXPECT_NE(strstr(profile, " -> fn_member_by_id\t3\n"), nullptr);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D2.4.1–D2.4.3: a parameter with several unresolved callers has no layout
// evidence, so its member reads must not spend hot work on a module-wide guess.
TEST(LambdaOptProfile, MultiSourceParameterAvoidsWrongShapeGuard) {
    char* profile = run_profiled_lambda_fixture(
        "test/benchmark/text/prettier_ast.ls",
        "prettier_ast: CHECKSUM:56483873",
        "temp/lambda_opt_contract/multi_source_shape.tsv", true);
    ASSERT_NE(profile, nullptr);
    EXPECT_LE(exec_profile_sum_prefix(profile,
        "field_attempt\t_print_node_"), 10000u);
    EXPECT_EQ(exec_profile_sum_prefix(profile,
        "field_attempt\t_expression_precedence_"), 0u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D8.2.6/S9.1.2: a checked int tail reuses one frame after evaluating its
// arguments, while a non-tail call and a replaced pointer owner keep calls.
TEST(LambdaOptProfile, CheckedScalarBracedTailElidesRecursiveFrames) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/checked_scalar_braced_tail.ls",
        "[3, 3, 0, 10, 3]",
        "temp/lambda_opt_contract/checked_scalar_braced_tail.tsv");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_sum_prefix(profile,
        "frame_entry\t_checked_tail_"), 6u);
    EXPECT_GE(exec_profile_sum_prefix(profile,
        "frame_entry\t_non_tail_"), 5u);
    EXPECT_GE(exec_profile_sum_prefix(profile,
        "frame_entry\t_changing_owner_"), 4u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D5.3.1: reads of a live array and string cannot collect, so their helper
// calls must not force publication/reload of the other live argument.
TEST(LambdaOptProfile, ReadOnlyLengthAvoidsGcRootTraffic) {
    const char* profile_path = "temp/lambda_opt_contract/readonly_length.tsv";
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/readonly_length_no_gc.ls", "[7, 2, 1]\n",
        profile_path);
    ASSERT_NE(profile, nullptr);
    EXPECT_NE(strstr(profile, "helper\tfn_len_l\t2"), nullptr);
    EXPECT_NE(strstr(profile, "helper\tfn_len_s\t1"), nullptr);
    EXPECT_LE(exec_profile_sum_prefix(profile,
        "root_store\t_lens__raw0_"), 2u);
    EXPECT_LE(exec_profile_sum_prefix(profile,
        "root_reload\t_lens__raw0_"), 3u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D3.3.3v3/S4.1.2: a literal's finite lanes need no setters after their
// fresh array allocation; an expression-valued member keeps checked stores.
TEST(LambdaOptProfile, FiniteIntLiteralArraySkipsPerElementSetters) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/finite_int_literal_array.ls",
        "[[0, 1, 2, 3], [0, 7, 2]]",
        "temp/lambda_opt_contract/finite_int_literal_array.tsv");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile, "helper\tarray_int_set"), 3u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D5.3.1/D3.3.3v3: a packed float literal fills its buffer before the next
// safepoint; the one nullable arm still builds a generic array on demand.
TEST(LambdaOptProfile, FloatLiteralFillsBeforeNullableFallback) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/tune27_float_literal_nullable.ls",
        "[2.5, null]",
        "temp/lambda_opt_contract/float_literal_uninit.tsv");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "helper\tarray_num_new_uninit"), 8u);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "helper\tarray_float_literal_with_nulls"), 1u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D4.4.4v4/D5.4.4: only the live relocating data-pointer reload executes
// after the collecting call for a stable typed-array parameter.
TEST(LambdaOptProfile, StableArrayLengthSurvivesCollectingCall) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/stable_array_length_after_gc.ls", "[9, 6, 8]",
        "temp/lambda_opt_contract/stable_array_length_after_gc.tsv");
    ASSERT_NE(profile, nullptr);
    ASSERT_NE(strstr(profile, "layout_items_reload\t_stable_length_"), nullptr);
    EXPECT_GT(exec_profile_count_prefix(profile,
        "layout_items_reload\t_stable_length_"), 0u);
    EXPECT_EQ(strstr(profile, "layout_length_reload\t_stable_length_"), nullptr);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D5.3.2/S9.1.2: a dynamic binding still marks the owner, but that
// non-allocating mark must not force a post-call GC-root reload.
TEST(LambdaOptProfile, CowBindingDoesNotReloadLiveOwner) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/string_cow_capture_guard.ls", "[true, true, true]\n",
        "temp/lambda_opt_contract/cow_bind_no_gc.tsv");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "helper\tcow_bind_var"), 2u);
    EXPECT_LE(exec_profile_count_prefix(profile,
        "root_reload\t_capture_dynamic_"), 17u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D3.3.3v3/S7.1.1v3: a valid dense carrier enters the fast loop, while a
// short carrier reaches the checked sibling without suppressing its error.
TEST(LambdaOptProfile, DenseGuardSelectsFastAndCheckedCopies) {
    char* profile = run_profiled_lambda_fixture(
        "test/mir/lambda/tune26_dense_carried_index.ls", "[45, true]\n",
        "temp/lambda_opt_contract/dense_guard.tsv");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "dense_attempt\t_tune26_dense_carried_index_"), 2u);
    EXPECT_EQ(exec_profile_count_prefix(profile,
        "dense_fast\t_tune26_dense_carried_index_"), 1u);
    EXPECT_NE(strstr(profile, "overflow\t-\t0"), nullptr);
    free(profile);
}

// D4.4.4v2 sibling-field handles + early return (cd's rbt_put): 29 array
// copies are the fixture's deliberate snapshots (S9.1.2), down from 90 before
// the ruling; the one map copy is `let before = t`.
TEST(LambdaOptCow, RmwSiblingHandlesBorrowWithoutCopies) {
    static const char* const tiers[] = {"jit", "interp"};
    for (int t = 0; t < 2; t++) {
        FixtureRun run = run_fixture("cow_rmw_sibling_borrow", tiers[t],
            fixture_source("test/lambda/proc/cow_rmw_sibling_borrow.ls"), true);
        ASSERT_TRUE(run.ok) << tiers[t];
        EXPECT_EQ(run.profile.get("array_shared_copies"), 29u) << tiers[t];
        EXPECT_EQ(run.profile.get("map_shared_copies"), 1u) << tiers[t];
    }
}

// D4.4.5 move-out binds (splay rotations): every rotation in the 40-iteration
// loop borrows (123 -> 83 map copies). LR12-11 adds 42 share marks, no copies:
// a rotation returns part of its `var` parameter, so the call site marks the
// result (164 -> 206). make()'s `N?` literal declarations now cross their
// deferred boundary and reify there (S11.4.1v3, D3.2.4v4) instead of at the
// rotation's `var` admission, leaving one fewer shared map per rotation
// (83 -> 43 copies, 206 -> 166 marks).
TEST(LambdaOptCow, MoveOutBindsBorrow) {
    // this construct needs MIR; a strict T0 pin must reject it (D8.1.1v15)
    expect_interp_rejection(opt_executable(), "test/lambda/proc/cow_move_out_bind.ls",
        "AST_NODE_MEMBER_ASSIGN_STAM");
    static const char* const tiers[] = {"jit", "auto"};
    for (int t = 0; t < 2; t++) {
        FixtureRun run = run_fixture("cow_move_out_bind", tiers[t],
            fixture_source("test/lambda/proc/cow_move_out_bind.ls"), true);
        ASSERT_TRUE(run.ok) << tiers[t];
        EXPECT_EQ(run.profile.get("map_shared_copies"), 43u) << tiers[t];
        EXPECT_EQ(run.profile.get("map_share_marks"), 166u) << tiers[t];
    }
}

// S9.2.2 place mutators (`push(m.a, v)` with `m.a = x; m.b = x`): each aliased
// slot detaches exactly once; the 1,000 appends through one unique place
// never copy (array_unique_mutations counts them).
TEST(LambdaOptCow, PlaceMutatorDetachesEachAliasedSlotOnce) {
    // this construct needs MIR; a strict T0 pin must reject it (D8.1.1v15)
    expect_interp_rejection(opt_executable(), "test/lambda/proc/cow_place_mutator.ls",
        "AST_NODE_SYS_FUNC");
    static const char* const tiers[] = {"jit", "auto"};
    for (int t = 0; t < 2; t++) {
        FixtureRun run = run_fixture("cow_place_mutator", tiers[t],
            fixture_source("test/lambda/proc/cow_place_mutator.ls"), true);
        ASSERT_TRUE(run.ok) << tiers[t];
        // the map rows are not pinned: the census prints two container kinds
        // under the `map` label, and the reader keeps the last one
        EXPECT_EQ(run.profile.get("array_shared_copies"), 7u) << tiers[t];
        EXPECT_EQ(run.profile.get("array_num_shared_copies"), 1u) << tiers[t];
        EXPECT_GE(run.profile.get("array_unique_mutations"), 1000u) << tiers[t];
    }
}

// D8.1.1v10: a typed `var` rebind reaches the caller through its home on
// every tier without copying the array; the one map copy is `let` snapshot.
TEST(LambdaOptCow, TypedVarRebindPublishesWithoutArrayCopies) {
    static const char* const tiers[] = {"jit", "interp"};
    for (int t = 0; t < 2; t++) {
        FixtureRun run = run_fixture("cow_var_typed_rebind", tiers[t],
            fixture_source("test/lambda/proc/cow_var_typed_rebind.ls"), true);
        ASSERT_TRUE(run.ok) << tiers[t];
        EXPECT_EQ(run.profile.get("array_num_shared_copies"), 0u) << tiers[t];
        EXPECT_EQ(run.profile.get("map_shared_copies"), 1u) << tiers[t];
    }
}

// T27-4 fixed-key path setter: the snapshot detaches once per container kind
// and the 800 in-place stores through a proven root never copy.
TEST(LambdaOptCow, FixedKeyPathStoreDetachesOnce) {
    static const char* const tiers[] = {"jit", "interp"};
    for (int t = 0; t < 2; t++) {
        FixtureRun run = run_fixture("tune27_fixed_path_store", tiers[t],
            fixture_source("test/lambda/proc/tune27_fixed_path_store.ls"), true);
        ASSERT_TRUE(run.ok) << tiers[t];
        EXPECT_EQ(run.profile.get("array_shared_copies"), 1u) << tiers[t];
        EXPECT_EQ(run.profile.get("map_shared_copies"), 2u) << tiers[t];
        EXPECT_GE(run.profile.get("array_unique_mutations"), 800u) << tiers[t];
    }
}

// Result44 (Tune27 §12): LambdaJS reserves the realm-slot suffix ONCE per
// realm store. c7e285e51 put js_realm_intrinsic_slots_ensure_roots on every
// intrinsic prototype lookup and re-walked the reservation each time (20x);
// the census row counts full walks, so the pin is one, not a wall-clock bound.
static const char* kJsRealmSlotsSource =
    "var arr = [];\n"
    "var total = 0;\n"
    "for (var i = 0; i < 20000; i++) {\n"
    "    arr.push(i);\n"
    "    total += arr.length + Object.getPrototypeOf(arr).constructor.name.length;\n"
    "    var s = 'x' + i;\n"
    "    total += s.length + Math.floor(i / 3);\n"
    "}\n"
    "console.log(total);\n";

TEST(LambdaOptJs, IntrinsicPrototypeLookupsReserveRealmSlotsOnce) {
    FixtureRun run = run_fixture("js_realm_slots_once", NULL, kJsRealmSlotsSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_TRUE(run.profile.has("js_realm_slot_reservations"));
    EXPECT_EQ(run.profile.get("js_realm_slot_reservations"), 1u);
}

TEST(LambdaOptAdmission, RecursiveShapeIdentityInterpExactHits) {
    FixtureRun run = run_fixture("shape_identity", "interp", kShapeIdentitySource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "70\n");
    // The counters alone can't distinguish this fixture from an ANY-degraded
    // contract (the reified carrier is also pointer-identical afterwards), so
    // the resolution warning check carries the regression sensitivity here.
    expect_no_unresolved_type_warning(run, "shape_identity");
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    // Fresh construction adopts Node, and immutable boundary plans reuse its
    // certificate at all ten crossings; neither admission nor reification runs.
    EXPECT_EQ(run.profile.get("map_admit_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
}

// D3.2.4v4/S7.7.2: fresh call arguments adopt a known layout before entry.
TEST(LambdaOptAdmission, FreshRecordCallArgumentsInterpAvoidReification) {
    const char* source =
        "type Context = {column: int, indent: int}\n"
        "fn dynamic(value) any => value\n"
        "fn walk(ctx: Context, remaining: int) int {\n"
        "    if (remaining == 0) ctx.column\n"
        "    else walk({column: ctx.column + 1, indent: ctx.indent}, remaining - 1)\n"
        "}\n"
        "pn main() {\n"
        "    print(walk(remaining: 100, ctx: {column: dynamic(0), indent: 0}))\n"
        "}\n";
    FixtureRun run = run_fixture("call_argument_layout", "interp", source, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "100\n");
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 0u);
}

// A nullable record-array path must not re-admit its owning graph after each
// scalar store or same-contract array relink (D3.2.4v3, D3.3.3v3).
static const char* kTypedPathSource =
    "type Row = {value: int, values: int[]}\n"
    "type World = {rows: Row?[]}\n"
    "pn update(var world: World, index: int) any {\n"
    "    world.rows[index].value = world.rows[index].value + 1\n"
    "    var values: int[] = world.rows[index].values\n"
    "    values[0] = values[0] + 1\n"
    "    world.rows[index].values = values\n"
    "}\n"
    "pn main() {\n"
    "    var world: World = {rows: [{value: 0, values: [0]}, null]}\n"
    "    var i = 0\n"
    "    while (i < 100) { update(world, 0); i = i + 1 }\n"
    "    print([world.rows[0].value, world.rows[0].values[0]])\n"
    "}\n";

TEST(LambdaOptAdmission, TypedArrayPathPreservesGraphProof) {
    FixtureRun run = run_fixture("typed_array_path", "jit", kTypedPathSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[100, 100]\n");
    // The initial nullable array reifies its one literal Row once. None of
    // the 100 updates may revisit fields or rebuild a graph after that.
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 1u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 2u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 48u);
    // Admission copies the uncertified `rows` literal one level (D4.4.2), so
    // its Row children are shared and marked; the first `values` write then
    // detaches that one-element array once. The count stays 1 for any number
    // of updates -- it replaces the deep clone of the whole graph that
    // admission used to make -- and must never grow per update.
    EXPECT_EQ(run.profile.get("array_checked_store_full_clone"), 1u);
}

// The refusal control: an ANY-bearing contract must keep reifying at declared
// crossings on BOTH tiers. A zero here without concrete storage classification
// for the union field means the adoption gate started admitting a shape whose
// direct reads would misaddress (Tune19 §11.3) — a memory-safety regression,
// not an optimization.
TEST(LambdaOptAdmission, AnyBearingContractRefusedJit) {
    FixtureRun run = run_fixture("any_bearing", "jit", kAnyBearingSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "n5\n");
    expect_no_unresolved_type_warning(run, "any_bearing");
    EXPECT_EQ(run.profile.get("map_admit_calls"), 5u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 5u);
}

TEST(LambdaOptAdmission, AnyBearingContractRefusedInterp) {
    FixtureRun run = run_fixture("any_bearing", "interp", kAnyBearingSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "n5\n");
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 5u);
    // The five constructor boundaries still reify the unsupported layout;
    // describe reuses the resulting storage proof through its immutable plan.
    EXPECT_EQ(run.profile.get("map_admit_storage_compatible_hits"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_calls"), 5u);
}

// The reification guard, and the tier-pinned regression for the JIT segfault.
//
// Nothing here is annotated with the contract: `let e = {...}` gives the
// literal NO contract hint, so it keeps its own inferred shape — in which the
// `Node?` field is a 9-byte TypedItem — and only the declared `Node?` RETURN
// carries the contract. The direct field read on the consumer side indexes by
// the contract's byte offsets, so that return crossing is not a check, it is
// the reification that makes the two agree.
//
// While the crossing was skipped, `sum_chain` read the tag byte plus seven
// pointer bytes as an Item and the next lambda_type_check faulted on it
// (SIGSEGV on release, ASan BUS on debug).
//
// `walk_len` guards the second half: an unannotated local off a `Node?`
// parameter must inherit the initializer's nullable POINTER-lane contract, or
// its lane null (0) boxes as a raw 0 instead of ItemNull and `walk != null`
// answers true — every idiomatic linked-list walk then runs one step long.
//
// Pinned to the JIT tier deliberately: the interpreter answers both correctly,
// so an auto-tier run of the same source proves nothing.
static const char* kUnadoptedRecursiveSource =
    "type Node = {val: int, next: Node?}\n"
    "\n"
    "pn make_chain(n: int) Node? {\n"
    "    if (n == 0) { return null }\n"
    "    let e = {val: n, next: make_chain(n - 1)}\n"
    "    return e\n"
    "}\n"
    "\n"
    "pn sum_chain(node: Node?) int {\n"
    "    if (node == null) { return 0 }\n"
    "    return node.val + sum_chain(node.next)\n"
    "}\n"
    "\n"
    "pn walk_len(node: Node?) int {\n"
    "    var walk = node\n"
    "    var n: int = 0\n"
    "    while (walk != null) { n = n + 1; walk = walk.next }\n"
    "    return n\n"
    "}\n"
    "\n"
    "pn main() {\n"
    "    let c = make_chain(100)\n"
    "    print(sum_chain(c) ++ \" \" ++ walk_len(c) ++ \" \" ++ walk_len(null))\n"
    "}\n";

TEST(LambdaOptAdmission, UnadoptedRecursiveLiteralReifiesOnJit) {
    FixtureRun run = run_fixture("unadopted_recursive", "jit",
        kUnadoptedRecursiveSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "5050 100 0\n");
    expect_no_unresolved_type_warning(run, "unadopted_recursive");
    // the literal's inferred shape is NOT the contract's, so the return
    // firewall must actually convert it — a zero here means the crossing was
    // elided again and the direct field reads are back to misaddressing
    EXPECT_GT(run.profile.get("map_admit_reifications"), 0u);
}

TEST(LambdaOptAdmission, UnadoptedRecursiveLiteralMatchesInterp) {
    FixtureRun run = run_fixture("unadopted_recursive", "interp",
        kUnadoptedRecursiveSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "5050 100 0\n");
}

// The profile must stay strictly env-gated: with COW_EXEC_PROFILE unset/0 the
// child writes no TSV, so release benchmarks can never pay for the counters.
TEST(LambdaOptAdmission, ProfileDisabledWritesNoTsv) {
    FixtureRun run = run_fixture("recursive_link", "jit", kRecursiveLinkSource, false);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "20\n");
    EXPECT_NE(OPT_ACCESS(run.profile_path.c_str(), 0), 0)
        << "profile TSV was written with COW_EXEC_PROFILE=0: " << run.profile_path;
}

TEST(LambdaOptAdmission, CowSiteCensusMatchesAggregateAndIsOptIn) {
    const char* source =
        "pn main() {\n"
        "  var first = {x: 1}\n"
        "  var second = first\n"
        "  first.x = 2\n"
        "  print([first.x, second.x])\n"
        "}\n";
    FixtureRun enabled = run_fixture("cow_sites", "jit", source, true, true);
    ASSERT_TRUE(enabled.ok);
    EXPECT_EQ(enabled.std_out, "[2, 1]\n");
    std::ifstream sites(enabled.sites_path.c_str());
    ASSERT_TRUE(sites.good());
    uint64_t marks = 0, copies = 0, unique = 0, overflow = 0;
    uint64_t jit_copies = 0;
    std::string line;
    while (std::getline(sites, line)) {
        std::istringstream row(line);
        std::string event, type, pc, count, bytes;
        if (!std::getline(row, event, '\t') ||
                !std::getline(row, type, '\t') ||
                !std::getline(row, pc, '\t') ||
                !std::getline(row, count, '\t') ||
                !std::getline(row, bytes, '\t')) continue;
        uint64_t n = (uint64_t)strtoull(count.c_str(), NULL, 10);
        if (event == "share_mark" && type == "map") marks += n;
        if (event == "shared_copy" && type == "map") {
            copies += n;
            std::string symbol;
            if (std::getline(row, symbol, '\t') && symbol.empty() && pc != "0x0") {
                jit_copies += n;
            }
        }
        if (event == "unique_write" && type == "map") unique += n;
        if (event == "overflow") overflow += n;
    }
    EXPECT_EQ(overflow, 0u);
    EXPECT_EQ(marks, enabled.profile.get("map_share_marks"));
    EXPECT_EQ(copies, enabled.profile.get("map_shared_copies"));
    EXPECT_EQ(unique, enabled.profile.get("map_unique_mutations"));
    EXPECT_GT(marks, 0u);
    EXPECT_GT(copies, 0u);
    EXPECT_GT(jit_copies, 0u);

    // Windows cannot remove the census file while this reader still holds it open.
    sites.close();
    FixtureRun disabled = run_fixture("cow_sites", "jit", source, true);
    ASSERT_TRUE(disabled.ok);
    EXPECT_EQ(disabled.std_out, enabled.std_out);
    EXPECT_NE(OPT_ACCESS(disabled.sites_path.c_str(), 0), 0);

    FixtureRun off = run_fixture("cow_sites_off", "jit", source, false, true);
    ASSERT_TRUE(off.ok);
    EXPECT_EQ(off.std_out, enabled.std_out);
    EXPECT_NE(OPT_ACCESS(off.profile_path.c_str(), 0), 0);
    EXPECT_NE(OPT_ACCESS(off.sites_path.c_str(), 0), 0);
}

TEST(LambdaOptAdmission, ImmutableArrayConsumerAdmitsOnce) {
    FixtureRun run = run_source_fixture("array_consumer",
        "test/lambda/proc/tune23_array_consumer.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[93, \"one\", 2, 42]\n");
    // one conversion for the closed producer and one for the open control;
    // increasing the traversal count must not allocate another admitted copy.
    EXPECT_LE(run.profile.get("fn_mutable_value_calls"), 2u);
}

TEST(LambdaOptAdmission, ExpressionConstructorUsesDeclaredLayout) {
    FixtureRun run = run_source_fixture("record_constructor",
        "test/lambda/proc/tune23_record_constructor.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[190, 3, 2, null]\n");
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
}

TEST(LambdaOptAdmission, BorrowedNumericArrayRetainsStoreLane) {
    FixtureRun run = run_source_fixture("array_borrow_lane",
        "test/lambda/proc/tune23_array_borrow_lane.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[[1, 2, 3], [10, 12, 15], [4, 5, 6]]\n");
    EXPECT_EQ(run.profile.get("array_checked_store_calls"), 0u);
}

TEST(LambdaOptAdmission, RecursiveUnionFieldsReuseAdmittedContract) {
    FixtureRun run = run_source_fixture("union_field",
        "test/lambda/proc/tune23_union_field.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[340, true, true]\n");
    ASSERT_TRUE(run.profile.has("union_admit_calls"));
    // Construction and the missing-field slow path still admit. Traversing
    // the ten-node tree twenty times must not revalidate every child.
    EXPECT_GT(run.profile.get("union_admit_calls"), 0u);
    EXPECT_LT(run.profile.get("union_admit_calls"), 40u);
}

TEST(LambdaOptAdmission, OpenUnionMemberShapeCachesAdmissionProof) {
    FixtureRun run = run_source_fixture("open_union_shape",
        "test/lambda/proc/tune26_open_union_shape.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[220, true]\n");
    // The open first arm cannot statically certify `child`, so the recursive
    // boundary remains. A compiler-built member carrier proves it once per
    // shape and the context cache reuses that proof (D3.2.4v3, D8.3.2-D8.3.3).
    EXPECT_GT(run.profile.get("union_map_rep_cache_hits"), 200u);
    EXPECT_LE(run.profile.get("union_map_rep_cache_misses"), 4u);
    EXPECT_EQ(run.profile.get("map_admit_relation_cache_hits"), 0u);
    // The malformed child must still reach the checked slow path.
    EXPECT_NE(run.std_err.find("got int 42"), std::string::npos);
}

TEST(LambdaOptAdmission, SplitBuildsInferredStringLane) {
    FixtureRun run = run_source_fixture("split_string_lane",
        "test/lambda/proc/tune26_split_string_lane.ls", "jit");
    ASSERT_TRUE(run.ok);
    // split builds an array (S2.5.7), so `parts` is one item of the printed array
    EXPECT_EQ(run.std_out, "[[\"alpha\", \"beta\", 7], \"gamma\"]\n");
    // The open first result must widen for the int append. The second result
    // already owns the string pointer lane, so its explicit string[] crossing
    // certifies in place instead of cloning (D3.3.1v2, D3.3.3v3, D3.3.4).
    EXPECT_EQ(run.profile.get("fn_mutable_value_calls"), 0u);
}

TEST(LambdaOptAdmission, BoxedUnionArrayAdmissionRetainsCarrier) {
    FixtureRun run = run_source_fixture("union_array",
        "test/lambda/proc/tune23_union_array.ls", "jit");
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[40, 2, 3, true, true, 2, 3]\n");
    EXPECT_EQ(run.profile.get("fn_mutable_value_calls"), 0u);
}

}  // namespace

TEST(LambdaOptAdmission, TypedConstructionPlanPreservesContainmentAndSnapshots) {
    const char* tiers[] = {"interp", "jit"};
    for (const char* tier : tiers) {
        FixtureRun run = run_fixture_script("typed_construction_plan", tier,
            "test/mir/lambda/typed_construction_plan.ls", true);
        ASSERT_TRUE(run.ok);
        EXPECT_EQ(run.std_out, "[2, 3, 2, 5][7, 5]error4\n");
    }
}

TEST(LambdaOptAdmission, PreplannedInterpPathRetainsGraphProof) {
    FixtureRun run = run_fixture("typed_path_interp", "interp", kTypedPathSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "[100, 100]\n");
    // only the initial nullable Row reifies; writes retain its graph proof
    // and detach the shared leaf array once (D3.3.3v3, S9.1.3)
    EXPECT_EQ(run.profile.get("map_admit_calls"), 1u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 1u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 2u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 48u);
    EXPECT_EQ(run.profile.get("array_checked_store_calls"), 100u);
    EXPECT_EQ(run.profile.get("array_checked_store_full_clone"), 1u);
}
