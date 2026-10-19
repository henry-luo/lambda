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
#include "../lambda/mir/mir.h"
#include "../lambda/mir/mir-gen.h"

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

TEST(LambdaMirProfiling, DiagnosticCallsPreserveProductionRootsAndSafepoints) {
    const char* args[] = {"python3", "test/benchmark/check_profile_root_parity.py", nullptr};
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
