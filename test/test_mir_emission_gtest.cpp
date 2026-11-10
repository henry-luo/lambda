// test_mir_emission_gtest.cpp
//
// Lambda MIR emission fixtures (MT1). Every `test/mir/lambda/*.ls` script is
// run through the normal CLI path and its finalized MIR artifact is checked
// against the matching `.mir-check` sidecar.
//
// See vibe/Lambda_Design_MIR_Emission_Test.md and test_mir_check_helpers.hpp.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_mir_check_helpers.hpp"
#include "test_lambda_helpers.hpp"
#include "../lambda/mir/mir.h"
#include "../lambda/mir/mir-gen.h"
#include "lib/file.h"

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

TEST_P(LambdaMirEmissionTest, MatchesGolden) {
    const auto& fixture = GetParam();
    char golden[1024];
    int size = snprintf(golden, sizeof(golden), "%.*s.txt",
        (int)fixture.script_path.size() - 3, fixture.script_path.c_str());
    ASSERT_GT(size, 0);
    ASSERT_LT((size_t)size, sizeof(golden));
    // MIR patterns and stress parity alone cannot reject a consistently wrong value.
    test_lambda_script_against_file(fixture.script_path.c_str(), golden,
        mir_check::script_is_procedural(fixture.script_path), "jit", "mvp", mir_check::executable());
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

TEST(MirCheckMatcher, MvpSelectorsRequireTheNamedBody) {
    EXPECT_TRUE(mir_check::function_name_matches("mvp_#_reduce", "mvp_12_reduce"));
    EXPECT_FALSE(mir_check::function_name_matches("mvp_#_reduce", "mvp_12_reduce_dp"));
    EXPECT_FALSE(mir_check::function_name_matches("mvp_#_reduce", "_reduce_12"));
    EXPECT_FALSE(mir_check::function_name_matches("mvp_#_reduce", "mvp__reduce"));
    EXPECT_TRUE(mir_check::function_name_matches("_js_reduce_#", "_js_reduce_12"));
}

TEST(MirCheckMatcher, DecimalCalleeIdsKeepTheCallContract) {
    EXPECT_TRUE(mir_check::line_contains("\tcall\tmvp_12_sum_dp, mvp_12_sum, %r1",
        "call\tmvp_{{n}}_sum_dp, mvp_{{n}}_sum"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\tmvp_12_sum_dp, mvp_12_sum, %r1",
        "call\tmvp_{{n}}_reduce_dp"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\tmvp_ab_sum_dp", "call\tmvp_{{n}}_sum_dp"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\tmvp__sum_dp", "call\tmvp_{{n}}_sum_dp"));
    EXPECT_TRUE(mir_check::line_contains("\tmov\t%r1f, 4", "mov\t{{r}}, 4"));
}

TEST(MirCheckMatcher, HelperChecksIgnorePrototypeSpellingButRequireTheCallee) {
    EXPECT_TRUE(mir_check::line_contains("\tcall\tfn_index_p_r6_n1_a2, fn_index, %r0, %r1",
        "call\t{{p}}, fn_index,"));
    EXPECT_TRUE(mir_check::line_contains("\tcall\tfn_index_p, fn_index, %r0, %r1",
        "call\t{{p}}, fn_index,"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\tfn_index_p, fn_index_set, %r0",
        "call\t{{p}}, fn_index,"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\t, fn_index, %r0",
        "call\t{{p}}, fn_index,"));
    EXPECT_TRUE(mir_check::line_contains("\tcall\tp2, helper, %r0, 7",
        "call\t{{p}}, helper, {{r}}, 7"));
    EXPECT_FALSE(mir_check::line_contains("\tcall\tp2, helper, %r0, 8",
        "call\t{{p}}, helper, {{r}}, 7"));
}

TEST(MirCheckMatcher, SequencesRetryPrefixesWithoutLeakingRegisterBindings) {
    const mir_check::MirLine lines[] = {
        {"\tlsh\t%r1, %r0, 3", 1}, {"\tadd\t%r2, %r3, %r1", 2},
        {"\tmov\ti64:(%r2), %r4", 3},
        {"\tlsh\t%r5, %r0, 3", 4}, {"\tadd\t%r6, %r3, %r5", 5},
        {"\tmov\t%r7, i64:(%r6)", 6}
    };
    mir_check::SeqExpectation steps[] = {
        {"lsh\t{{r:offset}}, {{r}}, 3", false},
        {"add\t{{r:address}}, {{r}}, {{r:offset}}", true},
        {"mov\t{{r}}, i64:({{r:address}})", true}
    };
    size_t positions[3], depth = 0;
    bool exhausted = false;
    EXPECT_TRUE(mir_check::match_sequence(lines, 6, steps, 3, positions, &depth, &exhausted));
    EXPECT_EQ(depth, 3u);
    EXPECT_EQ(positions[0], 3u);
    EXPECT_FALSE(exhausted);
    EXPECT_FALSE(mir_check::match_sequence(lines, 3, steps, 3, positions, &depth, &exhausted));
    const mir_check::MirLine delayed[] = {
        {"\tlsh\t%r5, %r0, 3", 1}, {"\tadd\t%r6, %r3, %r5", 2},
        {"L1:", 3}, {"\tmov\t%r7, i64:(%r6)", 4}
    };
    EXPECT_FALSE(mir_check::match_sequence(delayed, 4, steps, 3, positions, &depth, &exhausted));
    steps[2].next_line = false;
    EXPECT_TRUE(mir_check::match_sequence(delayed, 4, steps, 3, positions, &depth, &exhausted));
    steps[2].pattern = "mov\t{{r}}, i64:({{r:offset}})";
    EXPECT_FALSE(mir_check::match_sequence(lines, 6, steps, 3, positions, &depth, &exhausted));
}

TEST(MirCheckMatcher, ControlFlowLabelsRequireTheSameBackedge) {
    const mir_check::MirLine lines[] = {
        {"L17:", 1}, {"\tadd\t%r1, %r2, 1", 2}, {"\tjmp\tL17", 3}
    };
    mir_check::SeqExpectation steps[] = {
        {"{{l:head}}:", false}, {"add\t{{r}}, {{r}}, 1", true}, {"jmp\t{{l:head}}", true}
    };
    size_t positions[3], depth = 0;
    bool exhausted = false;
    EXPECT_TRUE(mir_check::match_sequence(lines, 3, steps, 3, positions, &depth, &exhausted));
    steps[2].pattern = "jmp\t{{l!head}}";
    EXPECT_FALSE(mir_check::match_sequence(lines, 3, steps, 3, positions, &depth, &exhausted));
    mir_check::RegisterCaptures captures;
    EXPECT_TRUE(mir_check::line_contains("L17:", "{{l:head}}:", &captures));
    EXPECT_FALSE(mir_check::line_contains("\tjmp\tL18", "jmp\t{{l:head}}", &captures));
    EXPECT_FALSE(mir_check::line_contains("\tjmp\tLff", "jmp\t{{l}}"));
    EXPECT_FALSE(mir_check::line_contains("\tjmp\tL17ff", "jmp\t{{l:head}}", &captures));
}

TEST(MvpMirAbi, OutcomeEntriesUseThePlatformCompanionTransport) {
    mir_check::CompileOptions options;
    mir_check::CompileResult compiled = mir_check::compile_and_dump(
        "test/mir/lambda/mvp_call_ownership.ls", options);
    ASSERT_TRUE(compiled.ok) << compiled.error;
    const char* names[] = {"mvp_#_wide", "mvp_#_forward"};
    for (const char* name : names) {
        bool found = false;
        for (const auto& function : compiled.dump.functions) {
            if (!mir_check::function_name_matches(name, function.function)) continue;
            ASSERT_FALSE(found) << "ambiguous outcome body " << name;
            found = true;
            ASSERT_FALSE(function.lines.empty());
            const char* header = function.lines[0].text.c_str();
#ifdef _WIN32
            EXPECT_NE(strstr(header, "\tfunc\ti64, p:runtime"), nullptr) << name;
#else
            EXPECT_NE(strstr(header, "\tfunc\ti64, i64, p:runtime"), nullptr) << name;
            bool distinct_pair = false;
            for (const auto& line : function.lines) {
                const char* text = line.text.c_str();
                if (strncmp(text, "\tret\t%r", 7)) continue;
                const char* comma = strchr(text, ',');
                if (!comma) continue;
                const char* first = text + 5;
                const char* second = comma + 1;
                while (*second == ' ') second++;
                size_t first_len = (size_t)(comma - first);
                size_t second_len = strcspn(second, " \t\r\n");
                EXPECT_TRUE(first_len != second_len || memcmp(first, second, first_len))
                    << "outcome lanes alias in " << name;
                distinct_pair = true;
            }
            EXPECT_TRUE(distinct_pair) << "missing normal outcome pair in " << name;
#endif
        }
        EXPECT_TRUE(found) << "missing outcome body " << name;
    }
}

TEST(LambdaMirProfiling, DiagnosticCallsPreserveProductionRootsAndSafepoints) {
    const char* args[] = {"python3", "test/benchmark/check_profile_root_parity.py",
        "--exe", mir_check::executable(), "--cow-sites", nullptr};
    ShellResult result = shell_exec_simple("python3", args);
    EXPECT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    shell_result_free(&result);
}

TEST(MirOptimizer, UnreachableSelfPhiKeepsItsDefinitionUntilCleanup) {
    // a value-numbered load removes the loop's entry edges after SSA is built.
    const char* program =
        "self_phi: module\n"
        "export probe\n"
        "probe: func i64, i64:arg\n"
        "local i64:cell, i64:condition, i64:guard\n"
        "alloca cell, 8\n"
        "mov i64:(cell), 0\n"
        "mov condition, i64:(cell)\n"
        "bt dead, condition\n"
        "ret 77\n"
        "dead:\n"
        "beq set_false, arg, 0\n"
        "mov guard, 1\n"
        "jmp loop\n"
        "set_false:\n"
        "mov guard, 0\n"
        "loop:\n"
        "bt escape, guard\n"
        "jmp loop\n"
        "escape:\n"
        "ret 0\n"
        "endfunc\n"
        "endmodule\n";

    MIR_context_t ctx = MIR_init();
    ASSERT_NE(ctx, nullptr);
    MIR_gen_init(ctx);
    MIR_gen_set_optimize_level(ctx, 2);
    MIR_scan_string(ctx, program);
    MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
    ASSERT_NE(module, nullptr);
    MIR_item_t probe = nullptr;
    for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items); item != nullptr;
         item = DLIST_NEXT(MIR_item_t, item)) {
        if (item->item_type != MIR_func_item) continue;
        ASSERT_EQ(probe, nullptr);
        probe = item;
    }
    ASSERT_NE(probe, nullptr);
    MIR_load_module(ctx, module);
    MIR_link(ctx, MIR_set_gen_interface, nullptr);
    using Probe = int64_t (*)(int64_t);
    Probe run = (Probe)probe->addr;
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run(0), 77);
    EXPECT_EQ(run(1), 77);
    MIR_gen_finish(ctx);
    MIR_finish(ctx);
}

TEST(MirOptimizer, Aarch64PackedMemoryAddressesStayEncodable) {
#if defined(__aarch64__)
    const struct { MIR_type_t type; int size; int64_t expected; } lanes[] = {
        {MIR_T_I8, 1, -7}, {MIR_T_U8, 1, 249},
        {MIR_T_I16, 2, -7}, {MIR_T_U16, 2, 65529},
        {MIR_T_I32, 4, -7}, {MIR_T_U32, 4, 4294967289LL},
        {MIR_T_I64, 8, -7}, {MIR_T_U64, 8, -7},
        {MIR_T_F, 4, -7}, {MIR_T_D, 8, -7}
    };
    for (const auto& lane : lanes) for (int offset : {0, 1, 3, 6, 9, 42, 32760, 32766}) {
        SCOPED_TRACE((int)lane.type);
        SCOPED_TRACE(offset);
        MIR_context_t ctx = MIR_init();
        ASSERT_NE(ctx, nullptr);
        MIR_gen_init(ctx);
        MIR_gen_set_optimize_level(ctx, 2);
        MIR_module_t module = MIR_new_module(ctx, "packed_addresses");
        MIR_type_t result_type = MIR_T_I64;
        MIR_item_t functions[2] = {};
        for (int write = 0; write < 2; write++) {
            MIR_item_t fn = functions[write] = write
                ? MIR_new_func(ctx, "store", 1, &result_type, 2,
                    MIR_T_I64, "base", MIR_T_I64, "input")
                : MIR_new_func(ctx, "load", 1, &result_type, 1, MIR_T_I64, "base");
            MIR_reg_t base = MIR_reg(ctx, "base", fn->u.func);
            MIR_reg_t address = MIR_new_func_reg(ctx, fn->u.func, MIR_T_I64, "address");
            MIR_reg_t output = write ? MIR_reg(ctx, "input", fn->u.func)
                : MIR_new_func_reg(ctx, fn->u.func, MIR_T_I64, "output");
            bool floating = lane.type == MIR_T_F || lane.type == MIR_T_D;
            MIR_reg_t value = floating ? MIR_new_func_reg(ctx, fn->u.func, lane.type, "value") : output;
            auto emit = [&](MIR_insn_t insn) { MIR_append_insn(ctx, fn, insn); };
            // address folding must validate the byte displacement against the final lane.
            emit(MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, address),
                MIR_new_reg_op(ctx, base), MIR_new_int_op(ctx, offset)));
            if (write && floating) emit(MIR_new_insn(ctx,
                lane.type == MIR_T_F ? MIR_I2F : MIR_I2D,
                MIR_new_reg_op(ctx, value), MIR_new_reg_op(ctx, output)));
            MIR_op_t memory = MIR_new_mem_op(ctx, lane.type, 0, address, 0, 1);
            MIR_op_t reg = MIR_new_reg_op(ctx, value);
            emit(MIR_new_insn(ctx, lane.type == MIR_T_F ? MIR_FMOV :
                lane.type == MIR_T_D ? MIR_DMOV : MIR_MOV,
                write ? memory : reg, write ? reg : memory));
            if (!write && floating) emit(MIR_new_insn(ctx,
                lane.type == MIR_T_F ? MIR_F2I : MIR_D2I,
                MIR_new_reg_op(ctx, output), MIR_new_reg_op(ctx, value)));
            MIR_op_t result = MIR_new_reg_op(ctx, output);
            emit(MIR_new_insn_arr(ctx, MIR_RET, 1, &result));
            MIR_finish_func(ctx);
        }
        MIR_finish_module(ctx);
        MIR_load_module(ctx, module);
        MIR_link(ctx, MIR_set_gen_interface, nullptr);
        uint8_t bytes[32784] = {};
        using Store = int64_t (*)(void*, int64_t);
        using Load = int64_t (*)(void*);
        EXPECT_EQ(((Store)functions[1]->addr)(bytes, -7), -7);
        EXPECT_EQ(((Load)functions[0]->addr)(bytes), lane.expected);
        if (offset > 0) EXPECT_EQ(bytes[offset-1], 0);
        EXPECT_EQ(bytes[offset+lane.size], 0);
        MIR_gen_finish(ctx);
        MIR_finish(ctx);
    }
#else
    GTEST_SKIP() << "ARM64 memory-operand validation";
#endif
}

TEST(MirOptimizer, Aarch64LongConditionalBranchesAndLabelAddresses) {
#if defined(__aarch64__)
    MIR_context_t ctx = MIR_init();
    ASSERT_NE(ctx, nullptr);
    MIR_gen_init(ctx);
    MIR_gen_set_optimize_level(ctx, 0);
    MIR_module_t module = MIR_new_module(ctx, "long_branches");
    MIR_type_t result = MIR_T_I64;
    MIR_item_t function = MIR_new_func(ctx, "probe", 1, &result, 1, MIR_T_I64, "rounds");
    MIR_reg_t rounds = MIR_reg(ctx, "rounds", function->u.func);
    MIR_reg_t sum = MIR_new_func_reg(ctx, function->u.func, MIR_T_I64, "sum");
    MIR_reg_t address = MIR_new_func_reg(ctx, function->u.func, MIR_T_I64, "address");
    MIR_label_t again = MIR_new_label(ctx), done = MIR_new_label(ctx);
    auto emit = [&](MIR_insn_t instruction) { MIR_append_insn(ctx, function, instruction); };
    emit(MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, sum), MIR_new_int_op(ctx, 0)));
    emit(MIR_new_insn(ctx, MIR_LADDR, MIR_new_reg_op(ctx, address), MIR_new_label_op(ctx, done)));
    emit(MIR_new_insn(ctx, MIR_BF, MIR_new_label_op(ctx, done), MIR_new_reg_op(ctx, rounds)));
    emit(MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, done), MIR_new_reg_op(ctx, rounds), MIR_new_int_op(ctx, 0)));
    emit(again);
    int64_t expected = 0;
    // varying wide immediates produce more than 1 MiB of live code without a huge JS fixture.
    for (int64_t i = 0; i < 70000; i++) {
        int64_t value = 0x123456789abLL + i;
        expected += value;
        emit(MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, sum), MIR_new_reg_op(ctx, sum), MIR_new_int_op(ctx, value)));
    }
    emit(MIR_new_insn(ctx, MIR_SUB, MIR_new_reg_op(ctx, rounds), MIR_new_reg_op(ctx, rounds), MIR_new_int_op(ctx, 1)));
    emit(MIR_new_insn(ctx, MIR_BGT, MIR_new_label_op(ctx, again), MIR_new_reg_op(ctx, rounds), MIR_new_int_op(ctx, 0)));
    emit(MIR_new_insn(ctx, MIR_JMPI, MIR_new_reg_op(ctx, address)));
    emit(done);
    emit(MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, sum)));
    MIR_finish_func(ctx);
    MIR_finish_module(ctx);
    MIR_load_module(ctx, module);
    MIR_link(ctx, MIR_set_gen_interface, nullptr);
    auto run = (int64_t (*)(int64_t))MIR_gen(ctx, function);
    ASSERT_NE(run, nullptr);
    EXPECT_GT(_MIR_get_new_code_addr(ctx, 0) - (uint8_t*)function->u.func->machine_code, 1 << 20);
    EXPECT_EQ(run(-1), 0);
    EXPECT_EQ(run(0), 0);
    EXPECT_EQ(run(1), expected);
    EXPECT_EQ(run(2), expected * 2);
    MIR_gen_finish(ctx);
    MIR_finish(ctx);
#else
    GTEST_SKIP() << "ARM64 branch displacement regression";
#endif
}

TEST(MirOptimizer, CrossBlockPhiAddressFoldingKeepsSavedValue) {
    // GVN exposes the saved index; address folding must retain its snapshot.
    char* program = read_text_file("test/mir/native/phi_saved_loop_value.mir");
    ASSERT_NE(program, nullptr);
    for (unsigned level = 0; level <= 3; level++) {
        SCOPED_TRACE(level);
        MIR_context_t ctx = MIR_init();
        ASSERT_NE(ctx, nullptr);
        MIR_gen_init(ctx);
        MIR_gen_set_optimize_level(ctx, level);
        MIR_scan_string(ctx, program);
        MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
        ASSERT_NE(module, nullptr);
        MIR_item_t function = nullptr;
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items); item;
                item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item) function = item;
        }
        ASSERT_NE(function, nullptr);
        MIR_load_module(ctx, module);
        MIR_link(ctx, MIR_set_gen_interface, nullptr);
        using Probe = int64_t (*)(void*, uintptr_t, int64_t, int64_t);
        Probe run = (Probe)MIR_gen(ctx, function);
        ASSERT_NE(run, nullptr);
        int64_t plain[] = {1, 2, 3, 4};
        int64_t mixed[] = {1, -1, 2, -3, 4, 2};
        int64_t handoff[] = {9007199254740992LL, 1, -1};
        uint64_t header[6] = {};
        auto invoke = [&](int64_t* items, int64_t length) {
            header[4] = (uintptr_t)items;
            header[5] = (uint64_t)length;
            return run(nullptr, (uintptr_t)header, length, 1);
        };
        EXPECT_EQ(invoke(plain, 4), 9990);
        EXPECT_EQ(invoke(mixed, 6), 8991);
        EXPECT_EQ(invoke(plain, 4), 9990);
        EXPECT_EQ(invoke(plain, 0), 0);
        EXPECT_EQ(invoke(handoff, 3), 9007199254740993LL * 999);
        MIR_gen_finish(ctx);
        MIR_finish(ctx);
    }
    free(program);
}
