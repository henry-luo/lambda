#pragma once

#include "radiant/view.hpp"

// keep fixture-owned containment storage alive for the block that references it.
struct TestBlockContainment {
    BlockContainmentProp value = BLOCK_CONTAINMENT_DEFAULT;
    BlockPropExt extension = BLOCK_EXTRAS_DEFAULT;
    BlockProp& block;

    explicit TestBlockContainment(BlockProp& owner) : block(owner) {
        if (!block.ext) block.ext = lam::own(&extension);
        block.ext->containment_props = lam::own(&value);
    }
    ~TestBlockContainment() {
        if (block.ext.get() == &extension) block.ext = nullptr;
        else if (block.ext && block.ext->containment_props.get() == &value)
            block.ext->containment_props = nullptr;
    }
    TestBlockContainment(const TestBlockContainment&) = delete;
    TestBlockContainment& operator=(const TestBlockContainment&) = delete;
};
