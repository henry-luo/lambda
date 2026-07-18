#include <gtest/gtest.h>
#include <string.h>

#include "../../lib/ownership.hpp"
#include "../../lib/tagged.hpp"
#include "../../radiant/radiant.hpp"

namespace {

template<class T>
T&& test_declval();

template<class Field, class Arg>
class CanSet {
    template<class F, class A>
    static char test(int, decltype(test_declval<F&>().set(test_declval<A>()))* = 0);

    template<class, class>
    static long test(...);

public:
    enum { value = sizeof(test<Field, Arg>(0)) == sizeof(char) };
};

template<ViewType T>
class HasViewTag {
    template<ViewType U>
    static char test(typename lam::ViewTagToType<U>::type*);

    template<ViewType>
    static long test(...);

public:
    enum { value = sizeof(test<T>(nullptr)) == sizeof(char) };
};

struct VisitKind {
    int operator()(ViewText*) { return 1; }
    int operator()(ViewBlock*) { return 2; }
    int operator()(View*) { return 0; }

    template<class T>
    int operator()(T*) { return 9; }
};

} // namespace

TEST(OwnershipPointers, OwnedPtrMovesAndFreesSessionMemory) {
    memtrack_init(MEMTRACK_MODE_STATS);

    {
        lam::SessionPtr<int> p = lam::session_make<int>(MEM_CAT_LAYOUT);
        ASSERT_TRUE((bool)p);
        *p = 37;

        lam::SessionPtr<int> q(static_cast<lam::SessionPtr<int>&&>(p));
        EXPECT_FALSE((bool)p);
        ASSERT_TRUE((bool)q);
        EXPECT_EQ(*q, 37);
    }

    memtrack_shutdown();
}

TEST(OwnershipPersistentField, RejectsSessionSourcesAtCompileTime) {
    typedef lam::PersistentField<char, lam::PoolDomain> Field;
    typedef lam::PersistentFieldRef<char, lam::PoolDomain> FieldRef;

    static_assert(CanSet<Field, lam::PoolPtr<char>>::value,
                  "persistent pool field should accept pool borrows");
    static_assert(CanSet<Field, lam::GcPtr<char>>::value,
                  "persistent pool field should accept GC borrows");
    static_assert(!CanSet<Field, lam::SessionPtr<char>>::value,
                  "persistent pool field must reject session ownership");
    static_assert(!CanSet<Field, lam::BorrowedPtr<char, lam::LayoutSessionDomain>>::value,
                  "persistent pool field must reject session borrows");
    static_assert(CanSet<FieldRef, lam::PoolPtr<char>>::value,
                  "persistent pool field refs should accept pool borrows");
    static_assert(!CanSet<FieldRef, lam::SessionPtr<char>>::value,
                  "persistent pool field refs must reject session ownership");

    SUCCEED();
}

TEST(OwnershipPersistentField, PromotesSessionStringBeforeRetaining) {
    memtrack_init(MEMTRACK_MODE_STATS);
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    {
        lam::SessionPtr<char> session_tag = lam::session_strdup("div", MEM_CAT_LAYOUT);
        ASSERT_TRUE((bool)session_tag);

        lam::PoolPtr<char> stable_tag = lam::promote_to_pool(pool, session_tag.get());
        ASSERT_TRUE((bool)stable_tag);

        lam::PersistentField<char, lam::PoolDomain> retained_tag;
        retained_tag.set(stable_tag);
        EXPECT_EQ(strcmp(retained_tag.get(), "div"), 0);

        char* raw_field = nullptr;
        lam::PersistentFieldRef<char, lam::PoolDomain> retained_ref(raw_field);
        retained_ref.set(stable_tag);
        EXPECT_EQ(strcmp(raw_field, "div"), 0);
    }

    pool_destroy(pool);
    memtrack_shutdown();
}

TEST(OwnershipPersistentField, WebviewSourcesRemainBoundToDomPoolLifetime) {
    Pool* dom_pool = pool_create();
    Pool* view_pool = pool_create();
    ASSERT_NE(dom_pool, nullptr);
    ASSERT_NE(view_pool, nullptr);

    const char* src = pool_strdup(dom_pool, "https://example.test/page");
    const char* srcdoc = pool_strdup(dom_pool, "<p>retained</p>");
    WebViewProp webview = {};
    radiant_retain_webview_src(&webview, lam::PoolPtr<const char>(src));
    radiant_retain_webview_srcdoc(&webview, lam::PoolPtr<const char>(srcdoc));

    // A retained view-pool reset destroys only view-owned properties; the DOM
    // attribute pool remains the lifetime source for recreated WebViewProp data.
    pool_destroy(view_pool);
    EXPECT_STREQ(webview.src, "https://example.test/page");
    EXPECT_STREQ(webview.srcdoc, "<p>retained</p>");

    pool_destroy(dom_pool);
}

TEST(TaggedView, CastsOnlyWhenRuntimeTagMatches) {
    ViewBlock block = {};
    block.view_type = RDT_VIEW_BLOCK;
    View* view = static_cast<View*>(&block);

    EXPECT_EQ(lam::view_as<RDT_VIEW_BLOCK>(view), &block);
    EXPECT_EQ(lam::view_as<RDT_VIEW_TEXT>(view), nullptr);
    EXPECT_EQ(lam::view_as_block<RDT_VIEW_BLOCK>(view), &block);

    static_assert(HasViewTag<RDT_VIEW_BLOCK>::value,
                  "known Radiant view tags should be mapped");
    static_assert(!HasViewTag<RDT_VIEW_NONE>::value,
                  "unmapped Radiant view tags should fail trait checks");
}

TEST(TaggedView, VisitViewDispatchesTypedPointers) {
    ViewText text = {};
    text.view_type = RDT_VIEW_TEXT;

    ViewBlock block = {};
    block.view_type = RDT_VIEW_BLOCK;

    EXPECT_EQ(lam::visit_view(static_cast<View*>(&text), VisitKind()), 1);
    EXPECT_EQ(lam::visit_view(static_cast<View*>(&block), VisitKind()), 2);
    EXPECT_EQ(lam::visit_view(nullptr, VisitKind()), 0);
}

TEST(TaggedDomNode, DomAsCastsOnlyWhenRuntimeTagMatches) {
    DomText text = {};
    text.node_type = DOM_NODE_TEXT;
    DomNode* node = static_cast<DomNode*>(&text);

    EXPECT_EQ(lam::dom_as<DOM_NODE_TEXT>(node), &text);
    EXPECT_EQ(lam::dom_as<DOM_NODE_ELEMENT>(node), nullptr);
}
