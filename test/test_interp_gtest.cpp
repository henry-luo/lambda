//==============================================================================
// T0 AST interpreter tests (D8.1.1v2, vibe/Lambda_Impl_Ast_Interp.md §5)
//
// Three layers:
//   1. Differential — every script in the committed P0 subset must produce
//      byte-identical output under LAMBDA_TIER=interp and the default tier,
//      with zero fallbacks. The goldens are the shared oracle (SI3/D3.3.1).
//   2. Walker micro-tests — one small source per node family, compared across
//      tiers so a divergence names the construct that caused it.
//   3. Frame-plan properties — generated deep expressions must evaluate without
//      the walker ever exceeding its statically planned scratch window (R1).
//
// The interpreter is driven as a subprocess: that is the same surface the gate
// measures, and it keeps the test independent of runtime link order.
//==============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#ifdef _WIN32
    #include <process.h>
    #define LAMBDA_EXE "lambda.exe"
    #define popen _popen
    #define pclose _pclose
#else
    #include <unistd.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

namespace {

struct RunResult {
    std::string stdout_text;
    std::string stderr_text;
    int exit_code = 0;
};

long interp_test_process_id() {
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

static unsigned long interp_gtest_run_sequence = 0;

// The project's own harness (test_lambda_helpers.hpp) trims trailing whitespace
// on both sides before comparing, and several goldens are stored without a
// final newline. Compare on the same footing or every such script fails on
// both tiers for a reason that has nothing to do with the interpreter.
std::string trim_trailing(const std::string& text) {
    size_t end = text.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Runs a script with an explicit tier. `tier` may be null for the default path.
// `procedural` selects `lambda.exe run <script>`, which is how a `pn main()`
// script is executed at all — invoking one directly runs no main and produces
// empty output, so a run-mode defect would compare equal on both tiers.
RunResult run_script(const std::string& script, const char* tier,
                     bool procedural = false) {
    RunResult result;
    char err_path[128];
    // A fixed stderr path races with another interpreter gtest invocation and
    // can make this run read an empty or unrelated summary after pclose().
    snprintf(err_path, sizeof(err_path), "temp/interp_gtest_stderr_%ld_%lu.txt",
             interp_test_process_id(), ++interp_gtest_run_sequence);
    std::string command;
    if (tier) command += std::string("LAMBDA_TIER=") + tier + " ";
    command += LAMBDA_EXE;
    if (procedural) command += " run";
    command += " " + script + " 2>" + err_path;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) { result.exit_code = -1; return result; }
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), pipe)) result.stdout_text += chunk;
    int status = pclose(pipe);
#ifdef _WIN32
    result.exit_code = status;
#else
    result.exit_code = WEXITSTATUS(status);
#endif
    result.stderr_text = read_file(err_path);
    ::remove(err_path);
    return result;
}

// The run summary is printed on stderr precisely so stdout stays comparable.
long summary_field(const std::string& stderr_text, const char* key) {
    size_t at = stderr_text.rfind(key);
    if (at == std::string::npos) return -1;
    return strtol(stderr_text.c_str() + at + strlen(key), nullptr, 10);
}

// Each list line is `<script>` or `<script>\t<field>`; the subset list's field
// is the invocation mode, the exclusion list's is the reject reason.
struct ListEntry {
    std::string script;
    std::string field;
    bool procedural() const { return field == "run"; }
};

std::vector<ListEntry> read_list(const std::string& path) {
    std::vector<ListEntry> entries;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) entries.push_back({line, ""});
        else entries.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    return entries;
}

void write_script(const std::string& path, const std::string& source) {
    ::system("mkdir -p temp");
    std::ofstream out(path, std::ios::binary);
    out << source;
}

// log.txt accumulates across runs, so a scratch-overflow assertion has to
// start from an empty log or it would report an earlier case's failure.
void truncate_log() { std::ofstream clear("log.txt", std::ios::trunc); }

// Compares a source snippet across both tiers and asserts the interpreter ran
// it rather than bouncing to the JIT.
void expect_tiers_agree(const std::string& name, const std::string& source) {
    std::string path = "temp/interp_case_" + name + ".ls";
    write_script(path, source);
    RunResult jit = run_script(path, "jit");
    RunResult interp = run_script(path, "interp");
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0)
        << name << ": expected the walker to execute this source, not fall back";
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text))
        << name << ": tier outputs diverge";
    EXPECT_EQ(jit.exit_code, interp.exit_code) << name << ": tier exit codes diverge";
}

//==============================================================================
// 1. Differential over the committed subset
//==============================================================================

class InterpSubsetTest : public ::testing::TestWithParam<ListEntry> {};

TEST_P(InterpSubsetTest, MatchesGoldenWithoutFallback) {
    const ListEntry& entry = GetParam();
    const std::string& script = entry.script;
    std::string golden_path = script.substr(0, script.size() - 3) + ".txt";
    std::string golden = read_file(golden_path);
    ASSERT_FALSE(golden.empty()) << "missing golden for " << script;

    RunResult interp = run_script(script, "interp", entry.procedural());
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0)
        << script << " fell back to the JIT; it must leave the subset list";
    EXPECT_EQ(trim_trailing(golden), trim_trailing(interp.stdout_text))
        << script << " diverges from its golden";
}

std::vector<ListEntry> subset_scripts() {
    std::vector<ListEntry> scripts = read_list("test/lambda/interp_p0_subset.txt");
    if (scripts.empty()) scripts.push_back({"", ""});   // keep the suite instantiable
    return scripts;
}

INSTANTIATE_TEST_SUITE_P(P0Subset, InterpSubsetTest,
    ::testing::ValuesIn(subset_scripts()),
    [](const ::testing::TestParamInfo<ListEntry>& info) {
        std::string name = info.param.script;
        // Parameter names must remain unique once the partition includes
        // same-basename fixtures from `test/lambda/` and `test/lambda/proc/`
        // (for example `vmap.ls`). Keep the relative directory in the GTest
        // name so the full P1 subset can be instantiated without aborting.
        const std::string root = "test/lambda/";
        if (name.compare(0, root.size(), root) == 0) {
            name = name.substr(root.size());
        }
        if (name.size() > 3) name = name.substr(0, name.size() - 3);
        for (char& c : name) if (!isalnum((unsigned char)c)) c = '_';
        return name.empty() ? std::string("none") : name;
    });

//==============================================================================
// 2. Walker micro-tests, one per node family
//==============================================================================

TEST(InterpWalker, LiteralsAndArithmetic) {
    expect_tiers_agree("literals",
        "1\n2.5\ntrue\nnull\n'sym'\n\"str\"\n1 + 2 * 3\n(7 - 2) / 2\n-5\nnot true\n");
}

TEST(InterpWalker, ShortCircuitAndTruthiness) {
    expect_tiers_agree("shortcircuit",
        "let a = 1\nlet b = 0\na and b\na or b\nfalse and 1\ntrue or 0\n"
        "if (a) \"yes\" else \"no\"\nif (b) \"yes\" else \"no\"\n");
}

TEST(InterpWalker, BindingsAndClosures) {
    expect_tiers_agree("closures",
        "let base = 10\nfn add(x, y) { x + y }\nlet plus = (n) => n + base\n"
        "add(1, 2)\nplus(5)\n(let inner = 3, inner * 2)\n");
}

TEST(InterpWalker, ContainersAndAccess) {
    expect_tiers_agree("containers",
        "let nums = [1, 2, 3]\nlet words = [\"a\", \"b\"]\nlet m = {x: 1, y: \"two\"}\n"
        "nums\nwords\nm\nnums[0]\nnums[2]\nm.x\nm.y\nlen(nums)\n");
}

TEST(InterpWalker, SystemFunctionsAndMethods) {
    expect_tiers_agree("sysfuncs",
        "let s = \"Hello\"\nlen(s)\nupper(s)\nlower(s)\ncontains(s, \"ell\")\n"
        "let nums = [3, 1, 2]\nsum(nums)\nmin(nums)\nmax(nums)\nnums.len()\n");
}

TEST(InterpWalker, NamedPureSystemArgsKeepTheirPositionalAbi) {
    // The system registry has no formal-name table: MIR evaluates the named
    // value in source order, including when the pipe supplies argument zero.
    expect_tiers_agree("named_sys_pipe",
        "let m = [[1, 5, 3], [4, 2, 6]]\n"
        "min(m, axis: 0)\n"
        "m |> min(axis: 0)\n"
        "m |> max(axis: 1)\n");
}

TEST(InterpWalker, DeferredDeclarationFailureSkipsItsDeclaringBlock) {
    // A dynamic decimal reaches the int boundary only at runtime. Both tiers
    // must abandon the function before the following expression can run.
    expect_tiers_agree("declaration_boundary_skip",
        "fn dynamic_decimal() any { 3.5m }\n"
        "fn must_stop() {\n"
        "  let rejected: int = dynamic_decimal()\n"
        "  \"unreachable\"\n"
        "}\n"
        "must_stop()\n");
}

TEST(InterpWalker, DeferredParameterFailureReturnsAtTheCallBoundary) {
    // The direct-call boundary owns a rejected parameter. MIR returns before
    // the callee body or an enclosing `or` expression can turn it into a value.
    expect_tiers_agree("parameter_boundary_skip",
        "fn dynamic_decimal() any { 3.5m }\n"
        "fn must_not_enter(value: int) int { value }\n"
        "must_not_enter(dynamic_decimal()) or 9\n");
}

TEST(InterpWalker, NamedAndInlinePatterns) {
    // Pattern values have no slab binding: both tiers must materialize their
    // module-local TypePattern before `is`, match, or a partial text operation.
    expect_tiers_agree("patterns",
        "type digits = \\(d+)\n"
        "type symbol_digits = \\symbol(d+)\n"
        "\"123\" is digits\n"
        "'123' is symbol_digits\n"
        "\"123\" is \\(d+)\n"
        "match \"42\" { case digits: \"number\" default: \"other\" }\n"
        "replace(\"a1b2\", digits, \"x\")\n");
}

TEST(InterpWalker, OrderedForWindowsUseSharedSortKeys) {
    // Ordered comprehensions must sort their emitted rows before selecting the
    // requested window; the key stream is separate from the spreadable output.
    expect_tiers_agree("ordered_for",
        "for (x in [3, 1, 4, 1, 5] order by x desc limit 3 offset 1) x\n");
}

TEST(InterpWalker, GroupedForMaterializesRowsBeforeAggregateBody) {
    // Group keys are evaluated against the filtered row scope, whereas the
    // body observes only the materialized `into` Element and its child rows.
    expect_tiers_agree("grouped_for",
        "for (x in [1, 2, 3, 4], let parity = x % 2 where x > 1 "
        "group by parity as p into g order by g.p desc) "
        "{key: g.p, total: sum(g |> ~)}\n");
}

TEST(InterpWalker, JoinedForPreservesTupleAndLeftJoinBindings) {
    // S14.1 requires tuple-stream joins to retain prior index bindings and
    // null-pad the optional source without allowing null keys to match.
    expect_tiers_agree("joined_for",
        "let orders = [{cid: 1, item: \"a\"}, {cid: 9, item: \"z\"}]\n"
        "let customers = [{id: 1, name: \"Ann\"}]\n"
        "for (i, o in orders, j, c? in customers on o.cid == c.id) "
        "{order_pos: i, customer_pos: j, item: o.item, name: c.name}\n");
}

TEST(InterpWalker, HigherOrderCalls) {
    expect_tiers_agree("hof",
        "fn apply(f, v) { f(v) }\nlet dbl = (x) => x * 2\napply(dbl, 21)\n"
        "reduce([1, 2, 3, 4], (a, b) => a + b)\n");
}

TEST(InterpWalker, ForwardReferencedTopLevelFunction) {
    // build_content's pass 1 hoists every top-level definition, so a call may
    // legally precede its textual definition; the walker hoists to match.
    expect_tiers_agree("hoisting",
        "let early = later(4)\nfn later(n) { n * n }\nearly\n");
}

TEST(InterpWalker, OptionalParameterDefault) {
    expect_tiers_agree("defaults",
        "fn greet(name, greeting: string = \"hi\") { greeting }\n"
        "greet(\"a\")\ngreet(\"a\", \"yo\")\n");
}

TEST(InterpWalker, ProceduralMainIsInvokedUnderRunMode) {
    // `lambda.exe run` calls a user-defined `pn main()` and makes its result the
    // script result. A script whose only top-level item is that definition also
    // has to bind it: it sits directly under AST_SCRIPT rather than inside a
    // content list, and evaluating it as an expression would leave it unbound.
    std::string path = "temp/interp_case_procmain.ls";
    write_script(path, "pn main() {\n    let base = 40\n    base + 2\n}\n");
    RunResult jit = run_script(path, "jit", /*procedural=*/true);
    RunResult interp = run_script(path, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(jit.stdout_text, interp.stdout_text) << "run-mode tiers diverge";
    EXPECT_NE(interp.stdout_text.find("42"), std::string::npos)
        << "run mode produced no main() result: [" << interp.stdout_text << "]";
}

TEST(InterpWalker, IntegerBoundedRangeIndicesUseCowAssignment) {
    // A `to` range is AST-typed any, but an explicit integer bound makes every
    // successful iteration integral; nested arithmetic must retain that proof.
    const char* path = "temp/interp_case_range_index.ls";
    write_script(path,
        "pn main() {\n"
        "    var values = [null, null, null, null]\n"
        "    for i in 0 to 1 {\n"
        "        for j in 0 to 1 { values[i * 2 + j] = i * 2 + j }\n"
        "    }\n"
        "    print(string(int(values[0])))\n"
        "    print(string(int(values[1])))\n"
        "    print(string(int(values[2])))\n"
        "    print(string(int(values[3])))\n"
        "}\n");
    RunResult jit = run_script(path, "jit", /*procedural=*/true);
    RunResult interp = run_script(path, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text))
        << "integer range-index stores diverge across tiers";
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, Uint64TypedArrayKeepsItsFullWidthLane) {
    // A u64[] literal must retain ELEM_UINT64 through a checked COW write;
    // routing it through generic storage would erase the typed-index proof.
    const std::string script = "test/lambda/proc/proc_uint64_array_set.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, NullableNativeTypedArraysKeepTheirDestinationLane) {
    // Optional array elements are full contracts, so T0 must enter the same
    // nullable native carrier used by MIR before its checked COW write.
    const char* scripts[] = {
        "test/lambda/proc/proc_nullable_native_array.ls",
        "test/lambda/proc/proc_nullable_native_float_array.ls",
        "test/lambda/proc/proc_nullable_native_int64_array.ls",
        "test/lambda/proc/proc_nullable_native_sized_array.ls",
        "test/lambda/proc/proc_nullable_native_pointer.ls",
        "test/lambda/proc/proc_nullable_native_extended_pointer.ls",
    };
    for (const char* script : scripts) {
        RunResult jit = run_script(script, "jit", /*procedural=*/true);
        RunResult interp = run_script(script, "interp", /*procedural=*/true);
        EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0) << script;
        EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text)) << script;
        EXPECT_EQ(jit.exit_code, interp.exit_code) << script;
    }
}

TEST(InterpWalker, NumericMaskAssignmentUsesTheVectorStore) {
    // A typed bool mask owns its lane/shape checks in fn_index_assign; T0 must
    // use that same in-place vector store for scalar, block, and N-D writes.
    const char* script = "test/lambda/proc/proc_mask_assign.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, DirectNumericNdimIndicesUseArrayNumHelpers) {
    // Fixed coordinates on a direct N-D numeric literal must retain ArrayNum's
    // axis bounds and in-place store semantics across both execution tiers.
    const char* script = "test/lambda/proc/proc_ndim_index_write.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, DirectVmapSetUsesTheCowReplacement) {
    // A direct VMap receiver must publish vmap_set_cow's replacement so a
    // later alias observes its own snapshot rather than the writer's update.
    const char* script = "test/lambda/proc/vmap.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, OpenItemMarkupMutationUsesRuntimeCow) {
    // `any | error` is a boxed open contract, so its map/Element writes must
    // keep the runtime COW ownership bridge instead of being rejected as a
    // typed structural write.
    const char* script = "test/lambda/proc/proc_markup_mutation.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, ObjectMethodsUseTracedCowReceiver) {
    // A bound T0 method keeps self in a GC-traced closure slot. Procedural
    // methods must detach and publish that receiver before field write-back,
    // while ordinary methods read the same receiver without mutating aliases.
    const char* scripts[] = {
        "test/lambda/proc/proc_object_counter.ls",
        "test/lambda/proc/object_mutation.ls",
    };
    for (const char* script : scripts) {
        RunResult jit = run_script(script, "jit", /*procedural=*/true);
        RunResult interp = run_script(script, "interp", /*procedural=*/true);
        EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0) << script;
        EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text)) << script;
        EXPECT_EQ(jit.exit_code, interp.exit_code) << script;
    }
}

TEST(InterpWalker, NestedDeclaredMapWritesUseFullContract) {
    // Nested writes must validate the rebuilt Person root, so dynamic numeric
    // fields are converted at admission and a rejected child write is atomic.
    const char* script = "test/lambda/proc/proc_type_numeric_structural_admission.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, ElementLiteralMutationUsesTheMatchingCowLayout) {
    // Element attributes and children occupy distinct layouts; the walker must
    // dispatch their direct literal writes through map- and array-COW alike.
    const std::string script = "test/lambda/proc/proc_element_mutation.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, ImmutableProcAliasStaysSynchronous) {
    // Native concurrency analysis conservatively treats an indirect pn call as
    // await-capable. T0 may execute only the immutable-alias case after its
    // source-level proof finds no task edge.
    const std::string script = "test/lambda/proc/proc_closure_mutation.ls";
    RunResult jit = run_script(script, "jit", /*procedural=*/true);
    RunResult interp = run_script(script, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpWalker, ImmutableProcAliasWithTaskUsesSatellite) {
    // The T0 module owns the alias, while the task-backed target runs through
    // its resumable MIR satellite; this is the mixed-tier boundary required by
    // D8.1.1v2 rather than a whole-module fallback.
    const std::string path = "temp/interp_case_proc_alias_task.ls";
    write_script(path,
        "pn worker() { sleep(1) }\n"
        "pn main() { let run = worker\n run() }\n");
    RunResult interp = run_script(path, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "executed="), 1);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
}

//==============================================================================
// 3. Frame-plan properties
//==============================================================================

// The plan pass computes a static scratch bound per function. A deeply nested
// expression is the shape most likely to undercount it, and an undercount would
// leave an Item unrooted across a collection — so the walker logs
// `interp: scratch overflow` and this test pins that log line to zero.
TEST(InterpFramePlan, DeepNestingStaysInsideThePlannedWindow) {
    for (int depth : {8, 32, 128}) {
        std::string source = "let base = 1\n";
        std::string expr = "base";
        for (int i = 0; i < depth; i++) {
            expr = "(" + expr + " + " + std::to_string(i % 7) + ")";
        }
        source += expr + "\n";
        std::string name = "deep" + std::to_string(depth);
        truncate_log();
        expect_tiers_agree(name, source);

        std::string log = read_file("log.txt");
        EXPECT_EQ(log.find("interp: scratch overflow"), std::string::npos)
            << "frame plan undercounted scratch at nesting depth " << depth;
    }
}

// Nested calls stack the callee plus each pending argument, which is the other
// direction the Sethi-Ullman bound has to cover.
TEST(InterpFramePlan, NestedCallArgumentsStayInsideThePlannedWindow) {
    std::string source =
        "fn f3(a, b, c) { a + b + c }\n"
        "f3(f3(1, 2, 3), f3(4, 5, 6), f3(f3(7, 8, 9), 10, 11))\n";
    truncate_log();
    expect_tiers_agree("nestedcalls", source);
    std::string log = read_file("log.txt");
    EXPECT_EQ(log.find("interp: scratch overflow"), std::string::npos)
        << "frame plan undercounted scratch for nested calls";
}

TEST(InterpFramePlan, NominalMatchPatternsStayInsideThePlannedWindow) {
    // A type-name arm keeps its evaluated Type value live while fn_is runs;
    // account for that temporary above eval_match's four context homes.
    const char* script = "test/lambda/object_pattern.ls";
    RunResult jit = run_script(script, "jit");
    RunResult interp = run_script(script, "interp");
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
    EXPECT_EQ(interp.stderr_text.find("interp: scratch overflow"), (size_t)-1)
        << "frame plan undercounted a nominal match pattern";
}

// Recursion beyond the budget must produce a clean S7.4.3-channel fault, not a
// crash. Fault timing may differ from T1 (S7.11.4); reaching the fault at all
// is what this pins.
TEST(InterpFramePlan, RecursionDepthBudgetFaultsCleanly) {
    std::string path = "temp/interp_case_deeprec.ls";
    write_script(path, "fn down(n) { if (n <= 0) 0 else 1 + down(n - 1) }\ndown(200000)\n");
    RunResult interp = run_script(path, "interp");
    EXPECT_NE(interp.exit_code, -1) << "interpreter did not survive deep recursion";
    // The property under test is T0's: the fault lands on the recovery frame
    // and is reported, instead of killing the process. The JIT tier is not
    // asserted here — the depth at which its native guard fires depends on
    // ambient stack state, and fault *timing* may differ across tiers anyway
    // (S7.11.4).
    EXPECT_NE(interp.stderr_text.find("Stack overflow"), std::string::npos)
        << "expected a clean fault, got stdout=[" << interp.stdout_text
        << "] stderr=[" << interp.stderr_text << "]";
}

//==============================================================================
// 4. Fallback accounting
//==============================================================================

// Every excluded script must avoid T0 execution, never silently half-
// interpreted (R4). A node:none row can fail before the runner emits a
// summary, so its parsed executed count is -1 rather than zero.
TEST(InterpFallback, ExcludedScriptsAreCountedNotInterpreted) {
    std::vector<ListEntry> excluded = read_list("test/lambda/interp_excluded.txt");
    ASSERT_FALSE(excluded.empty()) << "exclusion list is missing or empty";
    int checked = 0;
    for (const ListEntry& entry : excluded) {
        if (checked++ >= 12) break;   // a sample; the sweep covers the full list
        RunResult interp = run_script(entry.script, "interp");
        long executed = summary_field(interp.stderr_text, "executed=");
        EXPECT_LE(executed, 0) << entry.script
            << " is on the exclusion list but ran under T0";
    }
}

TEST(InterpWalker, GenericUint64ArrayReadsKeepTheirLane) {
    // The interpreter re-reads a generic array's raw u64 carrier after the
    // shared accessor narrows it, preserving the declared numeric identity
    // without inspecting native lane words as Item pointers (D3.2.2).
    const char* script = "test/lambda/sized_numeric_collections.ls";
    RunResult jit = run_script(script, "jit");
    RunResult interp = run_script(script, "interp");
    EXPECT_EQ(summary_field(interp.stderr_text, "executed="), 1);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 0);
    EXPECT_EQ(trim_trailing(jit.stdout_text), trim_trailing(interp.stdout_text));
    EXPECT_EQ(jit.exit_code, interp.exit_code);
}

TEST(InterpFallback, CharacterRangeIndexRemainsPinned) {
    // `to` can also make a character range, so it cannot use the integer COW
    // bridge merely because the loop's conservative AST type is `any`.
    const char* path = "temp/interp_case_char_range_index.ls";
    write_script(path,
        "pn main() {\n"
        "    var values = [null, null, null]\n"
        "    for c in \"a\" to \"c\" { values[c] = c }\n"
        "}\n");
    RunResult interp = run_script(path, "interp", /*procedural=*/true);
    EXPECT_EQ(summary_field(interp.stderr_text, "fallback="), 1);
}

}  // namespace
