/**
 * dom_api_check.cpp — compile-time integrity of the DOM operation catalog.
 *
 * Expands lambda/dom/dom_api.def twice: once into an enum, so a duplicated
 * canonical name is a duplicate enumerator (compile error), and once into a
 * static_assert per row that the body's C arity equals the row's `argc`.
 * There is no runtime here; the checks cost nothing after compilation.
 */

#include "dom_core.h"
#include "../jube/jube.h"
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

// 4. the host table's catalog section is the catalog: one slot per row, in row
// order, followed by the fixed set of companion doors -- native-shape entries for
// operations no fixed-arity row can express (ES38). Counting them explicitly is
// what keeps the section from growing ad-hoc members: adding one is a deliberate
// edit here, not a silent widening. Slot count is checked here; each slot's arity
// is checked by the cast in jube_registry.cpp, which will not compile if a body
// disagrees with its row.
// Companion doors: invoke_raw (the ordinal executor's variadic shape).
static constexpr int dom_catalog_companion_slots = 1;
static constexpr int dom_catalog_row_count =
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) 1 +
#define DOM_RAW(name, cluster, ret, params, body, flags) 1 +
#include "dom_api.def"
#undef DOM_OP
#undef DOM_RAW
    0;
static_assert(sizeof(JubeHostDomCatalogAPI) / sizeof(void*) ==
                  dom_catalog_row_count + dom_catalog_companion_slots,
              "JubeHostDomCatalogAPI must have one slot per dom_api.def row, "
              "plus exactly the declared companion doors");
// Only DOM_OP rows carry an operation id: a DOM_RAW row has no Lambda face and
// no uniform arity, so there is nothing for an id to name.
static constexpr int dom_catalog_op_row_count =
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) 1 +
#include "dom_api.def"
#undef DOM_OP
    0;
static_assert(dom_catalog_op_row_count == DOM_OP_ID_COUNT,
              "DOM_OP row count must agree with the operation-id enum");

}  // namespace
