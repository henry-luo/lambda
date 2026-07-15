#include <gtest/gtest.h>

#include "../radiant/event.hpp"

TEST(ClipboardStoreOwnership, DestroyReleasesContentsAndAllowsReinitialization) {
    ClipboardStore store = {};
    ASSERT_TRUE(store.init());

    store.write_html("<b>rich</b>", "rich");
    EXPECT_STREQ(store.read_mime("text/html"), "<b>rich</b>");
    EXPECT_STREQ(store.read_text(), "rich");

    store.destroy();
    EXPECT_EQ(store.items, nullptr);
    EXPECT_EQ(store.cached_text, nullptr);
    EXPECT_EQ(store.backend, nullptr);

    // The owner must be restartable because headless and embedded runtimes can
    // initialize and shut down clipboard state more than once in one process.
    ASSERT_TRUE(store.init());
    store.write_text("second lifetime");
    EXPECT_STREQ(store.read_text(), "second lifetime");
    store.destroy();
}
