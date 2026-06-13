# CSS Value Retention Audit (Memory_Safety_Template4.md §10 Phase 4)

> Phase 4 of `Memory_Safety_Template4.md`: after shorthand resolve-only
> temporaries were contained (Phases 1–3, `radiant/css_temp_decl.hpp` +
> `make check-css-temp-decl`), audit the places that *retain* `CssValue*` /
> `CssDeclaration*` pointers long-term and confirm each points into storage that
> outlives the retaining structure. Domain model reference:
> `Memory_Safety_Template.md` (`DomainOutlives`, `PersistentField`,
> `lib/ownership.hpp`).
>
> Method: trace, for every long-lived retention site, the allocator backing the
> stored pointer. "SAFE" = the pointee lives in a pool/arena that outlives the
> retaining structure. "SUSPECT" = the pointee could be freed while the
> retaining structure is still reachable.

## Scope audited

The five retained-CSS-value categories named in Template4 §4 / §10 Phase 4:

1. style tree declarations
2. DOM inline style declarations
3. CSSOM-created declarations (incl. JS style mutation)
4. animation keyframe declarations
5. computed-style caches

## Result summary

All retention reachable in **production** is pool/arena-stable (SAFE). The
computed-style cache fields are dead (never written). The only memory-safety
concern is a **latent, currently test-only** cross-pool aliasing in the
style-tree clone/subset family, now documented in code as an explicit lifetime
contract rather than left implicit (a correct deep `CssValue` copy does not yet
exist, and the path is not reached at runtime).

## Findings

### 1. Style tree declarations — SAFE (one latent caveat)

| Site | Field | Source domain | Verdict |
|---|---|---|---|
| `css_style_node.cpp` `css_declaration_create` | `CssDeclaration.value` | parser/cascade `Pool` (`pool_calloc`/`pool_strdup`, all values from `css_value_parser.cpp`) | SAFE |
| `css_style_node.cpp` `style_node_apply_declaration` | `StyleNode.winning_decl` / `weak_list` (ref-counted) | document `Pool` | SAFE |
| `css_style_node.cpp` inheritance (`style_tree_get_computed_value` → `css_declaration_create`) | inherited `CssDeclaration` | child tree `Pool` | SAFE |
| `css_style_node.cpp` `style_tree_clone` / `clone_tree_callback` | cloned tree's nodes | **SOURCE pool, retained in target_pool tree** | **LATENT (test-only)** |
| `css_style_node.cpp` `style_tree_create_subset` | new decl in `target_pool`, `value` aliased | **SOURCE pool value, retained in target_pool decl** | **LATENT (test-only)** |

Detail on the latent sites:

- `style_tree_clone` performs a **shallow** clone: `clone_tree_callback`
  re-applies the *source* node's `CssDeclaration*` into the cloned tree by
  reference + `css_declaration_ref`. The declarations and their `CssValue*`
  remain owned by the **source** pool. Refcounting does not protect across
  pools — `pool` free reclaims memory regardless of `ref_count` (`unref` only
  flips `valid=false`). So a clone whose `target_pool` outlives the source pool
  would dangle.
- `style_tree_create_subset` re-creates the `CssDeclaration` struct in
  `target_pool` but aliases the source `CssValue*` (no deep value copy).

Reachability: `style_tree_create_subset` and `style_tree_merge` are called only
from `test/css/test_css_system.cpp`. `style_tree_clone`'s only production caller
is `dom_element_clone` (`dom_element.cpp:1997`), which is itself only called from
`test/css/test_css_dom_crud.cpp`. In every current caller the source outlives the
clone, so no live bug exists — but the contract was previously implicit and the
`dom_element.cpp` comment misleadingly said "Deep copy".

Action taken (no behavior change): added explicit lifetime-contract comments at
`style_tree_clone`, `style_tree_create_subset`, and the `dom_element_clone` call
site documenting that the result shares/aliases source-pool memory and that the
source pool must outlive the clone. A correct fix (deep `CssValue` copy into
`target_pool`) is recorded as the follow-up if these enter a production path
where the clone can outlive the source document.

### 2. DOM inline style declarations — SAFE

`style="..."` is parsed in `dom_element_apply_inline_style`
(`lambda/input/css/dom_element.cpp`): tokens and the `CssDeclaration` are built
with `element->doc->pool` and applied to `DomElement::specified_style`. Values
are `pool_calloc`-backed for the document lifetime → outlives the element's
style tree. SAFE.

### 3. CSSOM-created declarations / JS style mutation — SAFE

- `element.style.foo = ...` (`lambda/js/js_dom.cpp` `js_dom_set_style_property`)
  routes through `dom_element_apply_inline_style` → `element->doc->pool`. SAFE.
- `rule.style.setProperty(...)` (`lambda/js/js_cssom.cpp`
  `js_cssom_rule_decl_set_property`) allocates the declaration and its array
  slot from `rule->pool` (fallback `get_document_pool()`) and stores it in
  `CssRule::data.style_rule.declarations[]`. SAFE.
- Custom properties (`--x`) (`dom_element.cpp` `dom_element_apply_declaration`)
  wrap the value in a `CssCustomProp` from `element->doc->pool`, linked on
  `DomElement::css_variables`; the stored value pointer is the pool-allocated
  declaration value. SAFE.

No synthetic stack/JS-heap `CssValue` is retained: JS string inputs are
stringified into declaration text and re-parsed into the pool.

### 4. Animation keyframe declarations — SAFE

`radiant/css_animation.cpp` parses keyframes and animation properties into
**primitive/self-contained** types (`CssAnimatedProp`, `TimingFunction`,
`TransformFunction`, `float`, `Color`) allocated from `doc->pool` / `doc->arena`
(`KeyframeRegistry`, `CssKeyframes`, `CssKeyframeStop[]`, `CssAnimState`,
interpolated transforms). `CssValue*`/`CssDeclaration*` are read-only during
resolve and **never retained**. SAFE.

### 5. Computed-style caches — N/A (dead fields, now removed)

`CssComputedStyle` (`css_style.hpp`) previously declared cached `CssValue*`
fields (`display`, `position`, `width`, `height`, `color`, `background_color`,
`font_size`, `font_family`). Grep confirmed **no code assigned or read any of
them**; the whole `css_computed_style_*` / `css_style_*` function family is
declared-only (the struct is never even constructed). No retention site existed.

Action taken: the eight dead cache fields were removed from `CssComputedStyle`,
along with the two unimplemented declarations `css_style_set_property` and
`css_style_add_declaration`. Build clean; CSS tests pass (`test_css_system`
33/33, `test_css_dom_crud` 63/63, `test_css_style_application_gtest` 15/15).
(The sibling declared-only functions `css_style_get_property` /
`css_style_has_property` / `css_style_remove_property` /
`css_style_cascade_resolve` / `css_computed_style_create` / `_destroy` and the
`css_engine.hpp` style-application stubs remain — a larger dead-API cleanup left
for a separate pass.)

## Why no `check-` lint for Phase 4

Phases 1–3 added `make check-css-temp-decl` because the risk was a textual
pattern (`.value = &local`). The Phase 4 risk is *semantic* cross-pool aliasing
through `void* value` in `css_declaration_create` — not detectable by grep
without false positives. The mitigation here is the in-code lifetime contract
plus this audit. If `style_tree_clone`/`_subset` ever gain a production caller
whose clone can outlive the source document, implement a deep `CssValue` copy
into `target_pool` (and only then is a stronger guard warranted).

## Follow-ups (not done; recorded)

1. Deep `CssValue` copy helper (recursive over list/function/calc/color-mix/
   string) to make `style_tree_clone`/`style_tree_create_subset` produce
   self-contained trees — only if a production path needs clone > source
   lifetime.
2. ~~Remove the unused `CssComputedStyle` cache fields and the unimplemented
   `css_style_set_property` / `css_style_add_declaration` declarations.~~ Done.
