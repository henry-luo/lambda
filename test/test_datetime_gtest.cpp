#include <gtest/gtest.h>

extern "C" {
#include "../lib/datetime.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

class DateTimeTest : public ::testing::Test {
protected:
    VariableMemPool* pool = nullptr;

    void SetUp() override {
        pool_variable_init(&pool, 4096, 20);
    }

    void TearDown() override {
        if (pool) {
            pool_variable_destroy(pool);
            pool = nullptr;
        }
    }
};

TEST_F(DateTimeTest, Basic) {
    EXPECT_EQ(sizeof(DateTime), 8UL);
}
