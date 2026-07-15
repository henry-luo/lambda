#include <gtest/gtest.h>

#include "../radiant/radiant.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/mem.h"

static UiContext* g_session_test_uicon = nullptr;
static int g_session_test_live_documents = 0;
static int g_session_test_peak_documents = 0;

DomDocument* show_html_doc(Url* base, char* doc_url,
                           int viewport_width, int viewport_height) {
    (void)base;
    (void)doc_url;
    (void)viewport_width;
    (void)viewport_height;
    DomDocument* doc = (DomDocument*)mem_calloc(
        1, sizeof(DomDocument), MEM_CAT_DOM); // OBJ_HEAP_OK: test document models the session-owned document root.
    if (!doc) return nullptr;
    g_session_test_live_documents++;
    if (g_session_test_live_documents > g_session_test_peak_documents) {
        g_session_test_peak_documents = g_session_test_live_documents;
    }
    if (g_session_test_uicon) g_session_test_uicon->document = doc;
    return doc;
}

void free_document(DomDocument* doc) {
    if (!doc) return;
    g_session_test_live_documents--;
    mem_free(doc);
}

extern "C" void radiant_cleanup_network_support(DomDocument* doc) { (void)doc; }
void webview_manager_clear(WebViewManager* manager) { (void)manager; }

TEST(BrowsingSessionMemory, NavigateBackForwardKeepsOnePresentedDocument) {
    UiContext uicon = {};
    BrowsingSession session = {};
    session.init(nullptr, nullptr);
    g_session_test_uicon = &uicon;
    g_session_test_live_documents = 0;
    g_session_test_peak_documents = 0;

    ASSERT_NE(session.navigate(&uicon, "file:///a.html", 800, 600), nullptr);
    ASSERT_NE(session.navigate(&uicon, "file:///b.html", 800, 600), nullptr);
    for (int i = 0; i < 50; i++) {
        ASSERT_NE(session.go_back(&uicon, 800, 600), nullptr);
        ASSERT_NE(session.go_forward(&uicon, 800, 600), nullptr);
        EXPECT_EQ(g_session_test_live_documents, 1);
    }

    // Replacement temporarily overlaps old/new ownership, but completed
    // navigation must never accumulate more than that pair.
    EXPECT_LE(g_session_test_peak_documents, 2);
    free_document(uicon.document);
    uicon.document = nullptr;
    session.destroy();
    EXPECT_EQ(g_session_test_live_documents, 0);
    g_session_test_uicon = nullptr;
}
