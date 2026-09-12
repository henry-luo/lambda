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
//  - under the interp tier the same crossings go through
//    runtime_type_admit_value and must classify EXACT_TRUSTED or
//    STORAGE_COMPATIBLE — never reify or copy;
//  - an ANY-bearing contract (union field) is REFUSED by the gate and reifies
//    at every declared crossing. If it ever stops reifying without the gate
//    learning concrete storage for those fields, direct MIR reads would
//    misaddress the shape (Tune19 §11.3) — so the control asserts the refusal.
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
        if (line.find('\t', first_tab + 1) != std::string::npos) continue;  // per-type table row
        std::string key = line.substr(0, first_tab);
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
};

// Runs `lambda.exe run --tier=<tier> <fixture>` in a child with the COW exec
// profile pointed at a private TSV. The tier is pinned explicitly: the JIT
// tier must prove admitted recursive boundaries statically, while the interp
// tier exercises the runtime relation classifier — letting the auto tier
// choose would make the counters depend on its heuristics.
static FixtureRun run_fixture(const char* name, const char* tier,
                              const std::string& source, bool profile_enabled) {
    FixtureRun run;
    ensure_opt_dir();
    std::string script_path = std::string(kOptDir) + "/" + name + ".ls";
    run.profile_path = std::string(kOptDir) + "/" + name + "_" + tier +
        (profile_enabled ? "" : "_off") + ".tsv";
    remove(run.profile_path.c_str());
    if (!write_text(script_path, source)) {
        ADD_FAILURE() << "cannot write fixture script " << script_path;
        return run;
    }

    std::string tier_arg = std::string("--tier=") + tier;
    const char* executable = opt_executable();
    const char* args[] = {executable, "run", tier_arg.c_str(), script_path.c_str(), NULL};
    ShellEnvEntry env[] = {
        {"COW_EXEC_PROFILE", profile_enabled ? "1" : "0"},
        {"COW_EXEC_PROFILE_OUT", run.profile_path.c_str()},
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
        ADD_FAILURE() << "fixture '" << name << "' (" << tier << "): child "
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

static FixtureRun run_source_fixture(const char* name, const char* source_path,
        const char* tier) {
    char* source = read_text_file(source_path);
    EXPECT_NE(source, nullptr) << source_path;
    if (!source) return {};
    FixtureRun run = run_fixture(name, tier, source, true);
    free(source);
    return run;
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
// The call count is NOT zero and must not be asserted to be. A `Node?` contract
// only reaches the classifier at all because runtime_type_admit_value now takes
// the named contract through its non-null arm. Before that it fell through to
// the lambda_type_matches shortcut — invisible to these counters, and the exact
// bypass that let an unreified literal shape sit behind a contract-shaped
// direct field read and segfault the JIT.
TEST(LambdaOptAdmission, RecursiveContractJitFullyStatic) {
    FixtureRun run = run_fixture("recursive_link", "jit", kRecursiveLinkSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "20\n");
    expect_no_unresolved_type_warning(run, "recursive_link");
    // every crossing that happens is classified trusted
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits") +
              run.profile.get("map_admit_storage_compatible_hits"),
              run.profile.get("map_admit_calls"));
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits"), 20u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 0u);
}

// Interp tier: the same crossings reach runtime_type_admit_value, and every
// one must classify as trusted (exact or storage-compatible) — shape identity
// holds, so nothing reifies and nothing is copied.
TEST(LambdaOptAdmission, RecursiveContractInterpAdmitsWithoutCopy) {
    FixtureRun run = run_fixture("recursive_link", "interp", kRecursiveLinkSource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "20\n");
    EXPECT_EQ(run.profile.get("map_admit_calls"), 60u);
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits") +
              run.profile.get("map_admit_storage_compatible_hits"), 60u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_fields_visited"), 0u);
    EXPECT_EQ(run.profile.get("map_admit_bytes_copied"), 0u);
}

// Interp tier, one shared node: 10 boundary crossings must all be
// EXACT_TRUSTED — the adopted construction and the declared parameter carry
// the same TypeMap identity.
TEST(LambdaOptAdmission, RecursiveShapeIdentityInterpExactHits) {
    FixtureRun run = run_fixture("shape_identity", "interp", kShapeIdentitySource, true);
    ASSERT_TRUE(run.ok);
    EXPECT_EQ(run.std_out, "70\n");
    // The counters alone can't distinguish this fixture from an ANY-degraded
    // contract (the reified carrier is also pointer-identical afterwards), so
    // the resolution warning check carries the regression sensitivity here.
    expect_no_unresolved_type_warning(run, "shape_identity");
    EXPECT_EQ(run.profile.get("map_admit_exact_shape_hits"), 10u);
    EXPECT_EQ(run.profile.get("map_admit_deep_clone_calls"), 0u);
    // Pinned current behavior: the `let a: Node = {val: 7, next: null}`
    // initializer takes the runtime path on the interp tier and reifies once
    // (48 bytes). If construction learns to adopt this literal statically,
    // ratchet these to 10/0 — do not loosen them.
    EXPECT_EQ(run.profile.get("map_admit_calls"), 11u);
    EXPECT_EQ(run.profile.get("map_admit_reifications"), 1u);
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
    EXPECT_EQ(run.profile.get("array_checked_store_full_clone"), 0u);
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
    // the describe(p) crossings see the already-reified carrier and pass as
    // storage-compatible; only the let-boundary reifies
    EXPECT_EQ(run.profile.get("map_admit_storage_compatible_hits"), 5u);
    EXPECT_EQ(run.profile.get("map_admit_calls"), 10u);
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
    EXPECT_EQ(run.std_out, "[\"alpha\", \"beta\", 7, \"gamma\"]\n");
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
