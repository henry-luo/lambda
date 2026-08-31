#include <gtest/gtest.h>

#include "../lambda/runtime/compiler_timing.hpp"

namespace {
int pass_runs = 0;
int pass_ok(void*) { pass_runs++; return 1; }
int pass_fail(void*) { return 0; }
}

TEST(CompilerPassManager, EnforcesFactsAndPublishesResults) {
    CompilerPassManager manager;
    compiler_pass_manager_init(&manager, COMPILER_FACT_AST);
    CompilerPassSpec bind = {"bind", COMPILER_FACT_AST, COMPILER_FACT_BOUND, pass_ok};
    CompilerPassSpec lower = {"lower", COMPILER_FACT_PLANNED,
                              COMPILER_FACT_MIR_LOWERED, pass_ok};
    ASSERT_EQ(compiler_pass_manager_add(&manager, &bind), 1);
    ASSERT_EQ(compiler_pass_manager_add(&manager, &lower), 1);
    EXPECT_EQ(compiler_pass_manager_run(&manager, nullptr), 0);
    EXPECT_EQ(manager.facts,
              (uint32_t)(COMPILER_FACT_AST | COMPILER_FACT_BOUND));
    EXPECT_EQ(pass_runs, 1);
    manager.facts |= COMPILER_FACT_PLANNED;
    EXPECT_EQ(compiler_pass_manager_run(&manager, nullptr), 1);
    EXPECT_EQ(pass_runs, 2);
}

TEST(CompilerPassManager, FailingPassStopsSchedule) {
    CompilerPassManager manager;
    compiler_pass_manager_init(&manager, COMPILER_FACT_AST);
    CompilerPassSpec pass = {"fail", COMPILER_FACT_AST, COMPILER_FACT_BOUND, pass_fail};
    ASSERT_EQ(compiler_pass_manager_add(&manager, &pass), 1);
    EXPECT_EQ(compiler_pass_manager_run(&manager, nullptr), 0);
    EXPECT_EQ(manager.facts, (uint32_t)COMPILER_FACT_AST);
}
