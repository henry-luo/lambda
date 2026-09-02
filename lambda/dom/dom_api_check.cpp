/**
 * dom_api_check.cpp — compile-time integrity of the DOM operation catalog.
 *
 * Expands lambda/dom/dom_api.def twice: once into an enum, so a duplicated
 * canonical name is a duplicate enumerator (compile error), and once into a
 * static_assert per row that the body's C arity equals the row's `argc`.
 * There is no runtime here; the checks cost nothing after compilation.
 */

#include "dom_core.h"
#include <cstddef>
#include <type_traits>


namespace {

template <class F> struct dom_arity;
template <class R, class... A> struct dom_arity<R(A...)> {
    static constexpr int value = (int)sizeof...(A);
};
template <class R, class... A> struct dom_arity<R (*)(A...)> {
    static constexpr int value = (int)sizeof...(A);
};
template <> struct dom_arity<std::nullptr_t> { static constexpr int value = -1; };

template <class F> constexpr bool dom_arity_ok(int argc) {
    return dom_arity<F>::value < 0 || dom_arity<F>::value == argc;
}

// 1. uniqueness: one enumerator per canonical name
enum DomOpId {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) DOM_OP_ID_##name,
#include "dom_api.def"
#undef DOM_OP
    DOM_OP_ID_COUNT
};

// 2. arity: a body with a C signature must take exactly `argc` Items
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) \
    static_assert(dom_arity_ok<decltype(body)>(argc), \
                  "dom_api.def: body arity != argc for " #name);
#include "dom_api.def"
#undef DOM_OP

// 3. a DERIVED row carries a derivation; a CORE row carries none
#define DOM_OP_TIER_CORE 0
#define DOM_OP_TIER_DERIVED 1
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) \
    static_assert((DOM_OP_TIER_##tier == DOM_OP_TIER_DERIVED) == (sizeof(deriv) > 1), \
                  "dom_api.def: derivation presence must match tier for " #name);
#include "dom_api.def"
#undef DOM_OP

}  // namespace
