#pragma once

// CSS shorthand resolve-only declaration helpers.
//
// Background: shorthand expansion in resolve_css_style.cpp copies a parsed
// CssDeclaration, rewrites property_id, and points value at a longhand
// CSS value before calling resolve_css_property(). When the value is a small
// synthetic list built on the stack, the list must stay alive for the whole
// resolve call. Manually assigning decl.value = &local_list is fragile: a
// narrower lexical scope for the list leads to stack-use-after-scope (see
// vibe/Memory_Safety_Template4.md §1).
//
// These helpers tie the scratch list storage to the resolve() call so the
// stack value cannot outlive — or under-live — the call. The copied
// declaration is never handed back to callers, so it cannot be re-pointed at
// a narrower-scope value after construction.
//
// Contract (Template4 §3, §9): resolve_css_property() may read decl->value
// during the call but must not retain a pointer from a resolve-only
// declaration. Persistent CSS values use the PersistentField path instead.

// CssDeclaration, CssValue, CssPropertyId, CSS_VALUE_TYPE_LIST
#include "../lambda/input/css/css_style.hpp"

// LayoutContext and resolve_css_property live in radiant/layout.hpp. Forward
// declare them here so this header stays lightweight (and unit-testable) and
// does not drag in the whole layout engine. The declaration must match
// layout.hpp exactly.
struct LayoutContext;
void resolve_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon);

namespace lam {

// Resolve-only single-value declaration. Routes one parsed shorthand
// component to a longhand resolver without exposing the copied declaration.
class CssTempDecl {
    CssDeclaration decl_;

public:
    CssTempDecl(const CssDeclaration* base, CssPropertyId prop, const CssValue* value)
        : decl_(*base) {
        decl_.property_id = prop;
        // resolve-only contract: value is read during the call, never retained
        decl_.value = const_cast<CssValue*>(value);
    }

    CssTempDecl(const CssTempDecl&) = delete;
    CssTempDecl& operator=(const CssTempDecl&) = delete;

    void resolve(LayoutContext* lycon) {
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }
};

// Resolve-only list declaration with compile-time capacity N. The scratch
// list value and its backing pointer array live inside the helper, so they
// stay alive for the whole resolve() call. Appending past capacity returns
// false instead of overflowing.
template<int N>
class CssTempListDecl {
    CssDeclaration decl_;
    CssValue list_;
    const CssValue* values_[N];
    int count_;

public:
    CssTempListDecl(const CssDeclaration* base, CssPropertyId prop)
        : decl_(*base), list_(), count_(0) {
        decl_.property_id = prop;
        for (int i = 0; i < N; i++) values_[i] = nullptr;
    }

    CssTempListDecl(const CssTempListDecl&) = delete;
    CssTempListDecl& operator=(const CssTempListDecl&) = delete;

    int count() const { return count_; }

    // Append a borrowed shorthand component. Returns false on null value or
    // when the compile-time capacity N is already reached.
    bool append(const CssValue* value) {
        if (!value || count_ >= N) return false;
        values_[count_++] = value;
        return true;
    }

    // Route the collected components to the longhand resolver. A single
    // component is passed directly; multiple components are wrapped in the
    // helper-owned scratch list value. No-op when nothing was appended.
    void resolve(LayoutContext* lycon) {
        if (count_ <= 0) return;
        // resolve-only contract: components are read during the call, never retained
        if (count_ == 1) {
            decl_.value = const_cast<CssValue*>(values_[0]);
        } else {
            list_.type = CSS_VALUE_TYPE_LIST;
            list_.data.list.values = const_cast<CssValue**>(values_);
            list_.data.list.count = count_;
            decl_.value = &list_;  // CSS_TEMP_DECL_OK: list_ outlives this resolve call.
        }
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }
};

} // namespace lam
