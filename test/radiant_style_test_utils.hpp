#pragma once

#include "radiant/view.hpp"

// keep fixture-owned containment storage alive for the block that references it.
struct TestBlockContainment {
    BlockContainmentProp value = BLOCK_CONTAINMENT_DEFAULT;
    BlockProp& block;

    explicit TestBlockContainment(BlockProp& owner) : block(owner) {
        block.containment_props = lam::own(&value);
    }
    ~TestBlockContainment() {
        if (block.containment_props.get() == &value) block.containment_props = nullptr;
    }
    TestBlockContainment(const TestBlockContainment&) = delete;
    TestBlockContainment& operator=(const TestBlockContainment&) = delete;
};
