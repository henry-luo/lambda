#include "../../lib/mem_vm.h"
#include "../../lib/memtrack.h"

#include <gtest/gtest.h>

class MemVmTest : public ::testing::Test {
protected:
    void SetUp() override {
        memtrack_fault_clear();
        ASSERT_TRUE(memtrack_init(MEMTRACK_MODE_DEBUG));
    }

    void TearDown() override {
        memtrack_fault_clear();
        EXPECT_EQ(memtrack_shutdown(), 0u);
    }
};

TEST_F(MemVmTest, ReserveCommitDecommitAndRelease) {
    const size_t page = mem_vm_page_size();
    ASSERT_GT(page, 0u);
    MemVmRegion* region = mem_vm_region_reserve(NULL, NULL, MEM_ROLE_RUNTIME_HEAP,
                                                page * 2, page);
    ASSERT_NE(region, nullptr);
    ASSERT_NE(mem_vm_region_base(region), nullptr);
    EXPECT_EQ(mem_vm_region_reserved_bytes(region), page * 2);
    EXPECT_EQ(mem_vm_region_committed_bytes(region), 0u);

    ASSERT_TRUE(mem_vm_region_commit(region, 0, page));
    EXPECT_EQ(mem_vm_region_committed_bytes(region), page);
    static_cast<unsigned char*>(mem_vm_region_base(region))[0] = 0x5A;

    ASSERT_TRUE(mem_vm_region_decommit(region, 0, page));
    EXPECT_EQ(mem_vm_region_committed_bytes(region), 0u);
    mem_vm_region_release(region);
}

TEST_F(MemVmTest, RejectsInvalidRangesAndAlignment) {
    const size_t page = mem_vm_page_size();
    EXPECT_EQ(mem_vm_region_reserve(NULL, NULL, MEM_ROLE_TEMP, page, page + 1), nullptr);
    MemVmRegion* region = mem_vm_region_reserve(NULL, NULL, MEM_ROLE_TEMP, page, page);
    ASSERT_NE(region, nullptr);
    EXPECT_FALSE(mem_vm_region_commit(region, 1, page));
    EXPECT_FALSE(mem_vm_region_commit(region, 0, 0));
    EXPECT_FALSE(mem_vm_region_commit(region, page, page));
    ASSERT_TRUE(mem_vm_region_commit(region, 0, page));
    EXPECT_FALSE(mem_vm_region_commit(region, 0, page));
    ASSERT_TRUE(mem_vm_region_decommit(region, 0, page));
    mem_vm_region_release(region);
}

TEST_F(MemVmTest, FaultInjectionCoversReserveAndCommit) {
    const size_t page = mem_vm_page_size();
    memtrack_fault_inject(0);
    EXPECT_EQ(mem_vm_region_reserve(NULL, NULL, MEM_ROLE_TEMP, page, page), nullptr);
    memtrack_fault_clear();

    MemVmRegion* region = mem_vm_region_reserve(NULL, NULL, MEM_ROLE_TEMP, page, page);
    ASSERT_NE(region, nullptr);
    memtrack_fault_inject(0);
    EXPECT_FALSE(mem_vm_region_commit(region, 0, page));
    memtrack_fault_clear();
    mem_vm_region_release(region);
}
