#include <gtest/gtest.h>

#include "../radiant/event.hpp"
#include "../lib/memtrack.h"

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

TEST(ClipboardStoreImage, BinaryImageSurvivesCopyAndRichPasteSnapshot) {
    clipboard_store_clear();
    // embedded zero bytes must survive the multi-MIME transport.
    ASSERT_TRUE(clipboard_store_write_image("data:image/png;base64,iVBORw0KGgoAAA==", nullptr, ""));
    ArrayList* items = clipboard_store_read_items();
    ClipboardEntry* image = clipboard_items_find(items, "image/png");
    ASSERT_NE(image, nullptr);
    const unsigned char bytes[] = {137,80,78,71,13,10,26,10,0,0};
    ASSERT_EQ(image->data_len, sizeof(bytes));
    EXPECT_EQ(memcmp(image->data, bytes, sizeof(bytes)), 0);
    clipboard_items_free(items);
    char* html = nullptr;
    char* text = nullptr;
    ASSERT_TRUE(clipboard_store_read_paste(&html, &text));
    EXPECT_STREQ(html, "<img src=\"data:image/png;base64,iVBORw0KGgoAAA==\" alt=\"\">");
    EXPECT_STREQ(text, "");
    mem_free(html);
    mem_free(text);
    clipboard_store_shutdown();
}

TEST(ClipboardStoreImage, HtmlAlternativePreservesSizeAndFailedCopyPreservesClipboard) {
    const char* html = "<img src=\"data:image/png;base64,iVBORw0KGgoAAA==\" width=\"240\" height=\"150\">";
    ASSERT_TRUE(clipboard_store_write_image("data:image/png;base64,iVBORw0KGgoAAA==", html, "Picture"));
    EXPECT_FALSE(clipboard_store_write_image("https://example.com/image.png", nullptr, ""));
    char* markup = nullptr;
    char* text = nullptr;
    ASSERT_TRUE(clipboard_store_read_paste(&markup, &text));
    EXPECT_STREQ(markup, html);
    EXPECT_STREQ(text, "Picture");
    clipboard_store_write_text("another clipboard owner");
    // a later read/write may retire the store without invalidating the owned snapshot.
    EXPECT_STREQ(markup, html);
    mem_free(markup);
    mem_free(text);
    clipboard_store_shutdown();
}
