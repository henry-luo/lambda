// Unit tests for the CSS shorthand resolve-only declaration helpers
// (radiant/css_temp_decl.hpp). See vibe/Memory_Safety_Template4.md §10 Phase 1.
//
// These tests exercise the engine-independent surface: capacity, append
// semantics, single-vs-list routing, and non-copyability. resolve_css_property
// is stubbed here to capture the declaration the helper routes to a longhand
// resolver, so we can assert the contract without the full layout engine.

#include <gtest/gtest.h>
#include <type_traits>

#include "../../radiant/view.hpp"

// Capture of the most recent resolve_css_property() call. Defined in the
// global namespace to satisfy the forward declaration in css_temp_decl.hpp.
namespace {
struct ResolveCapture {
    int call_count = 0;
    CssPropertyId last_prop = (CssPropertyId)0;
    const CssValue* last_value = nullptr;
    CssValueType last_value_type = (CssValueType)0;
    int last_list_count = 0;
};
ResolveCapture g_capture;
}  // namespace

// Test-local definition matching the forward declaration. The real engine
// definition lives in radiant/resolve_css_style.cpp and is not linked here.
void resolve_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* /*lycon*/) {
    g_capture.call_count++;
    g_capture.last_prop = prop_id;
    g_capture.last_value = decl ? decl->value : nullptr;
    if (decl && decl->value) {
        g_capture.last_value_type = decl->value->type;
        if (decl->value->type == CSS_VALUE_TYPE_LIST) {
            g_capture.last_list_count = decl->value->data.list.count;
        }
    }
}

namespace {

CssDeclaration make_base_decl() {
    CssDeclaration decl = {};
    decl.property_id = CSS_PROPERTY_BACKGROUND;
    decl.value = nullptr;
    return decl;
}

CssValue make_length(double n) {
    CssValue v = {};
    v.type = CSS_VALUE_TYPE_LENGTH;
    v.data.length.value = n;
    return v;
}

class CssTempDeclTest : public ::testing::Test {
protected:
    void SetUp() override { g_capture = ResolveCapture{}; }
};

// --- CssTempDecl: single value routing ------------------------------------

TEST_F(CssTempDeclTest, SingleValueRoutesToLonghand) {
    CssDeclaration base = make_base_decl();
    CssValue val = make_length(10.0f);

    lam::CssTempDecl decl(&base, CSS_PROPERTY_BACKGROUND_COLOR, &val);
    decl.resolve(nullptr);

    EXPECT_EQ(g_capture.call_count, 1);
    EXPECT_EQ(g_capture.last_prop, CSS_PROPERTY_BACKGROUND_COLOR);
    EXPECT_EQ(g_capture.last_value, &val);
}

// --- CssTempListDecl<2>: two-value list routing ---------------------------

TEST_F(CssTempDeclTest, TwoValuesRouteAsList) {
    CssDeclaration base = make_base_decl();
    CssValue a = make_length(1.0f);
    CssValue b = make_length(2.0f);

    lam::CssTempListDecl<2> decl(&base, CSS_PROPERTY_BACKGROUND_POSITION);
    EXPECT_TRUE(decl.append(&a));
    EXPECT_TRUE(decl.append(&b));
    EXPECT_EQ(decl.count(), 2);

    decl.resolve(nullptr);

    EXPECT_EQ(g_capture.call_count, 1);
    EXPECT_EQ(g_capture.last_prop, CSS_PROPERTY_BACKGROUND_POSITION);
    EXPECT_EQ(g_capture.last_value_type, CSS_VALUE_TYPE_LIST);
    EXPECT_EQ(g_capture.last_list_count, 2);
}

// --- CssTempListDecl<2>: single value is passed through, not wrapped -------

TEST_F(CssTempDeclTest, SingleValueListIsNotWrapped) {
    CssDeclaration base = make_base_decl();
    CssValue a = make_length(5.0f);

    lam::CssTempListDecl<2> decl(&base, CSS_PROPERTY_BACKGROUND_SIZE);
    EXPECT_TRUE(decl.append(&a));
    EXPECT_EQ(decl.count(), 1);

    decl.resolve(nullptr);

    EXPECT_EQ(g_capture.call_count, 1);
    EXPECT_EQ(g_capture.last_value, &a);
    EXPECT_EQ(g_capture.last_value_type, CSS_VALUE_TYPE_LENGTH);
}

// --- CssTempListDecl<N>: capacity and null guards -------------------------

TEST_F(CssTempDeclTest, AppendPastCapacityReturnsFalse) {
    CssDeclaration base = make_base_decl();
    CssValue a = make_length(1.0f), b = make_length(2.0f), c = make_length(3.0f);

    lam::CssTempListDecl<2> decl(&base, CSS_PROPERTY_BACKGROUND_POSITION);
    EXPECT_TRUE(decl.append(&a));
    EXPECT_TRUE(decl.append(&b));
    EXPECT_FALSE(decl.append(&c));  // capacity 2 reached
    EXPECT_EQ(decl.count(), 2);
}

TEST_F(CssTempDeclTest, AppendNullReturnsFalse) {
    CssDeclaration base = make_base_decl();
    lam::CssTempListDecl<2> decl(&base, CSS_PROPERTY_BACKGROUND_POSITION);
    EXPECT_FALSE(decl.append(nullptr));
    EXPECT_EQ(decl.count(), 0);
}

TEST_F(CssTempDeclTest, EmptyListDoesNotResolve) {
    CssDeclaration base = make_base_decl();
    lam::CssTempListDecl<2> decl(&base, CSS_PROPERTY_BACKGROUND_POSITION);
    decl.resolve(nullptr);
    EXPECT_EQ(g_capture.call_count, 0);
}

// --- Non-copyability (compile-time contract) ------------------------------

TEST_F(CssTempDeclTest, HelpersAreNonCopyable) {
    static_assert(!std::is_copy_constructible<lam::CssTempDecl>::value,
                  "CssTempDecl must not be copyable");
    static_assert(!std::is_copy_assignable<lam::CssTempDecl>::value,
                  "CssTempDecl must not be copy-assignable");
    static_assert(!std::is_copy_constructible<lam::CssTempListDecl<2>>::value,
                  "CssTempListDecl must not be copyable");
    static_assert(!std::is_copy_assignable<lam::CssTempListDecl<2>>::value,
                  "CssTempListDecl must not be copy-assignable");
    SUCCEED();
}

}  // namespace
