# DOM Simplify: collapsing the script-runtime ↔ Radiant DOM interface

- **Date:** 2026-09-11.
- **Status:** IN PROGRESS. Sections 1–5 and 7–10 are the proposal; §6.0 below
  records what has landed. Every measurement in §2–§4 is of HEAD at the audit
  commit, not of a candidate.
- **Source audit:** HEAD `fa77dbdcc`, working tree as of 2026-09-11.
- **Scope:** the interface *between* the script runtimes (LambdaJS, Lambda
  script) and the Radiant engine — `lambda/dom/`, `lambda/module/radiant/`,
  the Jube declared-interface layer, and the JS-side adapters. Radiant's own
  layout/paint code and the DOM algorithms themselves (selector matching,
  HTML serialization, event cascade) are **out of scope** except where an
  algorithm is trapped inside a dispatcher.
- **Authority:**
  [formal design](../../doc/Lambda_Formal_Design.md) **D6.2.2v2**,
  **D7.4.1v2**, **D7.4.4**, **D7.4.5v2**;
  [formal semantics](../../doc/Lambda_Formal_Semantics.md) **S5.1.4v2**,
  **S9.1.4**, **S9.2.2**.
  Working design records: [DOM host API](../Lambda_Design_DOM_Host_API.md)
  (**ES38–ES47**, **ESO78–ESO114**),
  [DOM API](../Lambda_Design_DOM_API.md) (**ES33–ES36**),
  [DOM dispatch](../Lambda_Design_DOM_Dispatch.md),
  [DOM package](../Lambda_Design_DOM_Pkg.md).
  This proposal changes **implementation structure only**; it proposes no
  change to any `S#`/`D#` ruling, and it is in service of two of them that the
  code does not yet satisfy (D7.4.4 "no fallback tier", D7.4.5v2 "materializing
  array-shaped host state into real arrays … is an anti-pattern this ruling
  retires").
- **New ledger IDs:** `DS1`–`DS17` (rulings proposed here), `DSO1`–`DSO6`
  (open questions). Continues the host-API ledger at **ES48** and **ESO115**
  where a point needs to be recorded there instead.

---

## 1. Objective and completion contract

One DOM operation should be **declared once, implemented once, and reached in
at most three calls** from either script runtime. Today it is declared in as
many as six places and reached in five or six, and the "core" tier that
secondary operations are supposed to compose over is 83% of the catalog.

Four obligations — the three requirements, plus the hop budget:

1. **Fewer layers.** Delete the intermediate dispatch tiers — the ordinal
   enum, the per-ordinal thunks, the name-keyed property chains — so a member
   access resolves from a compiled record straight onto the operation's body.
2. **Minimal core, composed secondary.** Re-tier the catalog so that a row with
   a native body is one that *is* the mechanism. Everything expressible over
   other rows becomes `DERIVED` with `DOM_NO_BODY`, or keeps a native body only
   when a measurement justifies it and the ES43 oracle proves it equal.
3. **Wrap, do not translate.** Radiant DOM state reaches Lambda as `velmt` /
   `varray` / `vmap` over the live engine structures (D7.4.5v2), not as
   snapshot arrays built by a collect loop and not as a re-entry into a
   string-keyed property dispatcher.
4. **Three hops, every path.** One dispatch, one body, one engine access —
   DS17, with the counting rule and the per-class analysis in §4.5.

**Exit gate:** ≥ **1000 code lines** removed from the C++ DOM handling surface,
counted with blank lines and comment-only lines excluded and with no
reformatting credited. §8 states the measurement procedure and the per-phase
budget (conservative total ≈ **1590**).

---

## 2. What the interface looks like today

### 2.1 The surfaces

| Surface | Where | Size (code lines) |
|---|---|---|
| The operation catalog | `lambda/dom/dom_api.def` | 263 `DOM_OP` + 50 `DOM_RAW` rows |
| The core bodies | `lambda/dom/dom_core.cpp` | 494 |
| The DOM implementation | `lambda/dom/dom.cpp` | **13 350** (16 227 raw) |
| Other core files | `dom_events/selection/cssom/clipboard/formdata/observers` | 7 022 |
| The Lambda `dom.*` face | `lambda/dom/dom_module.cpp` | 86 (fully generated — the model to copy) |
| The module bridge | `lambda/module/radiant/radiant_dom_bridge.cpp` | 3 282 |
| The declared interface + binds | `lambda/module/radiant/radiant_dom_iface.cpp` | 1 900 |
| The `radiant.*` engine face | `lambda/module/radiant/radiant_module.cpp` | 3 121 |
| The record dispatcher | `lambda/jube/jube_interface.cpp` | 1 566 |
| JS realm shape | `lambda/js/js_dom_realm.cpp`, `js_dom_adapter.cpp` | 553 + 67 |

Measured set total (the nine `.cpp` rows above; the catalog row is a row count,
and headers are excluded): **31 441** code lines. `dom_api.def` itself is 376
code lines and `jube.h` 1 351; both are in the gate's file set (§8) because the
collapse adds rows to the first and deletes the enum from the second.

### 2.2 Five parallel tables, all keyed by the same operation

| Table | Where | Rows |
|---|---|---|
| Catalog rows | `dom_api.def` | 263 `DOM_OP` + 50 `DOM_RAW` |
| Element-operation ordinals | `jube.h:299–383` `JubeDomElementOperation` | **89** |
| Property ids | `dom.cpp:6154` `JS_DOM_PROPS(X)` | **152** |
| Reflected attributes | `dom.cpp:8609` `JS_DOM_REFLECTED_ATTRS(X)` | **32** |
| Declared interface members | `radiant_dom_iface.cpp` `radiant_dom_interface_decl[]` | 29 types, **300** member lines |
| Member bind rows | `radiant_dom_iface.cpp`, 23 `JubeMemberBind` tables | **387** rows (11 macro spellings), 302 distinct names, **315** distinct handler functions |

Only the first is compile-checked against anything (`dom_api_check.cpp`
expands it four ways). The other five are hand-maintained and must agree by
inspection.

Measured overlaps:

- **75** property names are declared in **both** `JS_DOM_PROPS` and the member
  bind tables — two implementations of the same property that must agree.
- **26** of the 32 `JS_DOM_REFLECTED_ATTRS` rows are *also* hand-written in the
  module as a tag guard + `m4b` accessor pair + two `extern` declarations + a
  `RADIANT_GUARDED_GET`/`_SET` pair + a `BIND_FIELD_SET` row + a decl-string
  member line. Two reflection tables for one set of facts.
- Only **21** of the 89 ordinals name a catalog row exactly. **31** have no
  candidate row under any spelling (`normalize`, `prepend`, `toggle_attribute`,
  `attach_shadow`, `split_text`, the six SVG matrix/CTM rows, the four
  `*_data` character-data rows, …). The remaining **37** are the same operation
  under two names in two catalogs — `DISPATCH_EVENT` ↔ `dispatch`,
  `ADD_EVENT_LISTENER` ↔ `add_listener`, `GET_ATTRIBUTE_NAMES` ↔
  `attribute_names`, `GET_ROOT_NODE` ↔ `root_node`.
- **84** `RADIANT_DOM_OPERATION_BINDING` thunks in the bridge, each
  `{ *out = radiant_dom_element_operation(receiver, <ORDINAL>, args, argc); return 1; }`,
  plus **84** matching `extern "C"` declarations in the iface. 168 hand-written
  lines that carry no information the ordinal does not already carry.

### 2.3 The three mega-dispatchers

| Function | Lines | Code lines | Shape |
|---|---|---|---|
| `dom_get_property_impl` | 8962–10115 | **905** | linear `if (prop_id == …)` chain, 135 tests |
| `dom_set_property_impl` | 10157–10795 | **513** | linear chain, 39 tests |
| `dom_document_get_property_for` | 6345–6607 | **189** | linear chain |
| `dom_element_operation_impl` | 14535–15372 | **658** | linear `if (operation == …)` chain, **80** tests, not even a `switch` |
| | | **2265** | |

`dom_cssom.cpp` repeats the pattern twice more
(`dom_cssom_rule_decl_get_property` / `_set_property`).

### 2.4 A worked trace — `el.getAttribute("x")` from JavaScript

```
JS property/call site
 └─ jube_member_call_by_ordinal            (jube_interface.cpp — compiled record, ordinal)
     └─ radiant_dom_m4d_get_attribute      (radiant_dom_bridge.cpp — generated thunk)
         └─ radiant_dom_element_operation  (bridge)
             └─ radiant_host_api->dom_catalog->invoke_raw   (host API seam)
                 └─ dom_element_operation_impl               (dom.cpp — 80-arm if-chain)
                     └─ (arm 15) elem->get_attribute(name)   (radiant DomElement)
```

Six hops, one of them a linear scan over 80 branch tests. The catalog row
`get_attribute` (`dom_core_get_attribute`) exists and is *not* on this path.

### 2.5 The same trace from Lambda — `dom.children(n)`

```
dom.children(n)
 └─ dom_pub_children        (dom_module.cpp — generated absence-rule trampoline)
     └─ dom_fp_children     (dom_core.cpp)
         └─ dom_prop_get(n, "children")                  ← re-enters by STRING
             └─ dom_get_property_impl                     (bsearch over 152 names,
                 └─ … linear chain …                       then a linear chain)
                     └─ dom_live_child_collection_bridge  (the VArray we wanted)
```

`dom_core.cpp` has **51** such delegation bodies, **21** of which re-enter the
JS-shaped property dispatcher by a camelCase string literal
(`dom_prop_get(n, "firstElementChild")`, `…"nodeType"`, `…"textContent"`, …).
These are labelled "native fast paths" (`DOM_F_FASTPATH`) and are slower than
the derivation they are meant to accelerate.

### 2.6 The core tier is not a core

`DOM_OP` rows by tier: **219 CORE, 44 DERIVED**. By cluster:

| Cluster | Total | CORE | DERIVED |
|---|---:|---:|---:|
| editing | 40 | 39 | 1 |
| range | 33 | 27 | 6 |
| selection | 29 | 26 | 3 |
| style | 27 | 27 | 0 |
| node | 25 | 15 | 10 |
| input | 15 | 15 | 0 |
| forms | 14 | 10 | 4 |
| tree | 11 | 5 | 6 |
| document | 10 | 10 | 0 |
| *(12 more)* | 59 | 45 | 14 |

ES40 says core is the ABI and secondary is provided as wrappers. A catalog that
is 83% core has not made that split; it has labelled everything core.

Two rows are outright duplicates today — one body, two rows, two names:

| Body | Rows |
|---|---|
| `js_range_get_collapsed` | `range_collapsed` (DERIVED, FASTPATH) **and** `range_get_collapsed` (CORE) |
| `js_selection_set_base_and_extent` | `set_base_and_extent` **and** `selection_set_base_and_extent` |

### 2.7 Snapshots where a virtual carrier already exists

`DOM_F_SNAPSHOT` is set on 13 rows. Four of them describe collections the spec
itself calls live, and three of those already have a live `VArray` backend
sitting unused next to them:

| Row | Live backend that already exists |
|---|---|
| `form_controls` | `dom_live_form_elements_bridge` (`DOM_VARRAY_FORM_ELEMENTS`) |
| `attribute_names` | `dom_attribute_collection_bridge` (`DOM_VARRAY_ATTRIBUTES`), and `VelmtOps::attrs.count`/`key_at` on the node itself |
| `radio_group`, `details_group` | `DOM_VARRAY_LOOKUP_NAME` / child walk |
| `focus_candidates` | engine list; no carrier |

Eleven `DomCollectionVArrayKind` backends exist (`CHILD_NODES`,
`ELEMENT_CHILDREN`, `DOCUMENT_FORMS`, `FORM_ELEMENTS`, `LOOKUP_TAG`,
`LOOKUP_CLASS`, `LOOKUP_NAME`, `SELECT_OPTIONS`, `SELECT_SELECTED_OPTIONS`,
`ATTRIBUTES`, `CLASS_LIST`) and the `radiant_dom_node_velmt_vtable` gives every
DOM node a tag face, an attribute map face and a child list face. Meanwhile
`dom_module.cpp`'s own header comment still states the opposite policy:

> Collections are returned as ordinary Lambda arrays, i.e. snapshots (S9.2.2)
> … Live collections stay a JS-adapter concern.

That sentence predates D7.4.5v2 and is the policy this proposal retires. Note
what S9.2.2 actually rules: *read views* have snapshot semantics. A live DOM
collection is not a read view of a Lambda value; it is host state, and
D7.4.5v2 names materializing it "an anti-pattern this ruling retires".

---

## 3. Findings

**F-1 — one operation, up to six declarations.** Name, signature, JS spelling,
receiver type, handler symbol and body are spread over `dom_api.def`, the
ordinal enum, `JS_DOM_PROPS`, `JS_DOM_REFLECTED_ATTRS`, the interface decl
string, and the bind tables. Only the first is checked.

**F-2 — two operation catalogs.** 89 ordinals and 313 catalog rows, overlapping
in 21 names, joined by the 658-line `dom_element_operation_impl` chain and 168
lines of thunks and externs. The ordinal is a *selector*, which D6.2.2v2 says a
published capability should not need: the record already resolved the member.

**F-3 — the property protocol is a fallback tier that D7.4.4 retired.** 1607
code lines of name-keyed chains, 75 of whose names are already bound as compiled
records. D7.4.4 rules that declared interfaces + record hooks are the *only*
host-object protocol, "no fallback tier". The chains are that tier.

**F-4 — two reflection tables.** 26 attributes declared once declaratively in
the core and once imperatively in the module, ≈ 14 code lines each on the module
side.

**F-5 — "fast paths" that are slow paths.** 21 `DOM_F_FASTPATH` bodies re-enter
the string-keyed dispatcher. They exist to satisfy the ES43 oracle, not to be
fast.

**F-6 — core is 83% of the catalog**, so "compose secondary from core" has no
teeth; and `range`/`selection` alone hold ~28 rows that are plain compositions
of boundary points.

**F-7 — snapshot-by-default for Lambda**, contradicting D7.4.5v2 and duplicating
live `VArray` backends that already exist.

**F-8 — the eight most primitive operations in the DOM are string re-entries.**
`node_type`, `node_name`, `node_value`, `parent_node`, `first_child`,
`last_child`, `next_sibling`, `previous_sibling` are all `CORE` rows, and every
one of their bodies is literally `dom_prop_get(n, "camelCaseName")`
(`dom_core.cpp:260–267`). The node link walk — the mechanism the whole DOM is
defined over — re-enters the JS property dispatcher by a string literal, paying
`fn_to_cstr` + a `bsearch` over 152 names + a linear chain, per link. F-5
under-stated this: these eight are worse than the 21 `DOM_F_FASTPATH` bodies,
because they are the rows every derivation composes over.

**F-9 — the document re-materializes the name the record just resolved.** All
**76** document members are `RADIANT_DOC_GET_FN` / `_SET_FN` / `_CALL_FN`
two-liners that take the resolved member and look it back up by *string*:

```c
#define RADIANT_DOC_GET_FN(fn, js) \
    static int fn(Item receiver, Item* out) { \
        return radiant_dom_document_host_get_property(receiver, radiant_dom_doc_key(js), out); \
    }
```

and `radiant_dom_document_host_get_property` then swaps the process-wide active
document (`dom_swap_active_document` / `dom_restore_active_document`) around
each read, because `dom_document_proxy_get_property(key)` takes only a key and
reads an ambient global. D7.4.4's corollary rules the opposite in as many
words: property keys "are resolved internally via one borrowed byte view, and
are never re-materialized for a fallback consumer."

**F-10 — the open-name fallback is four aliases of one function.**
`radiant_dom_node_named_get` → `radiant_dom_host_get_property` →
`radiant_dom_get_property` → `dom_get_property_impl`. Only the third does work,
and that work is two special cases: a `labels` tag test, and a geometry commit
gated on `strcmp(prop,"offsetWidth")… || strncmp(prop,"client",6) || strncmp(prop,"scroll",6)`
— nine property names, tested on **every** property read of every node.

---

## 4. Proposed rulings

### DS1 — one row per operation, and the row carries every surface's view of it

`dom_api.def` becomes the single declaration. The row gains four columns:

```
DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv,
       iface,      // receiver interface(s): node | html_element | input_element | … | ""
       js_name)    // JS spelling when it differs from camel(name); "" otherwise
```

`iface` and `js_name` are metadata (D7.4.4 already says member spellings are),
so adding them changes no ABI. From the one row we then generate:

- the catalog struct slot (exists),
- the operation-id enum (exists, in `dom_api_check.cpp`),
- **the `JubeMemberBind` row** (new),
- **the interface-declaration member line** (new),
- **the module thunk** (new — replaces the 84 hand-written ones),
- the `dom.*` published trampoline (exists),
- the compile-time arity and uniqueness checks (exist).

Rows with `iface == ""` publish no member and behave exactly as today.

### DS2 — the ordinal enum is deleted

`JubeDomElementOperation` stops being a hand-written enum and stops being an
ABI selector. Every ordinal becomes a `DOM_OP` row with a body of its own; the
80 arms of `dom_element_operation_impl` become 80 `extern "C" Item dom_core_<name>(…)`
functions with the same bodies. `invoke_raw` is retired along with the chain.

Call path after DS2, for the §2.4 trace — counting a hop the same way §2.4
does, one per call, the final engine call included:

```
JS property/call site
 └─ jube_member_call_by_ordinal            (1) compiled record, ordinal
     └─ <generated row thunk>              (2) arity adapter only
         └─ dom_catalog->get_attribute     (3) == dom_core_get_attribute, the body
             └─ elem->get_attribute        (4) radiant DomElement
```

**Six hops become four**, and the 80-test linear scan is gone entirely.
`radiant_dom_element_operation` and `invoke_raw` disappear; the host-API seam is
no longer a hop of its own, because the slot it dereferences now *is* the body
rather than a dispatcher that will go looking for one.

Hop 2 survives only because `JubeMemberBind.call` is
`int (Item, Item*, int, Item*)` while a row body is `Item (Item, …)` at the
row's own arity. **DS13** removes it, taking this path to three.

Receiver-kind guards that the chain performs up front (text vs comment vs
element vs `#document`) move into a generated per-row prologue keyed by the
row's `iface` column, so no row re-tests what its receiver type already proves.

**Variadic ordinals.** A handful of ordinals are genuinely variadic in the web
IDL — `append(...nodes)`, `prepend`, `replaceWith`, `after`, `before`, and the
overloaded `scroll`/`scrollTo`/`scrollBy`. A `DOM_OP` row is fixed-arity by
construction, so each takes one of two shapes, decided per row and recorded in
the row's comment: a fixed-arity row whose single parameter is a Lambda array
(the honest Lambda signature, `fn(n: any, nodes: array) -> null`), or a
`DOM_RAW` row keeping the `(Item*, int)` C shape where no Lambda face is wanted
yet (ES46). Neither needs the ordinal to survive; the record still resolves the
member, and the generated thunk is the one place that packs the arguments.

### DS3 — the property protocol keeps only what is genuinely name-driven

After DS1/DS2, `dom_get_property_impl` / `dom_set_property_impl` retain only:

- numeric-index access (`select[i]`),
- `data-*` / `dataset`,
- expando reads and writes,
- style shorthand and custom-property names,
- the document-node passthrough.

Every name that has a bind row is deleted from the chain. Per D7.4.4 the record
is the only implementation; a second one that must agree is the defect.

### DS4 — one reflection table, in the core, generating both sides

`JubeMemberBind` **already carries the slot for this**:

```c
const char* reflect_attr; // attribute-reflected member: generic reflect routine
                          //   handles get/set; no handler functions needed
```

It is declared, and `jube_interface.cpp` reads it in exactly two places — to
decide `record->readonly` and to pass the "a field needs a getter" validation.
**No binding table anywhere sets it, and no dispatch path reads it**: the
generic reflect routine its comment promises does not exist. So the 26
duplicated attributes are not working around a missing mechanism; they are
hand-written beside an unwired one. DS4 wires it rather than inventing it.

`JS_DOM_REFLECTED_ATTRS` moves to `lambda/dom/dom_reflect.def` with columns
`(idl, attr, kind, default, tags, js_name, after_hook)`. `after_hook` names the
invariant call a few attributes need (`dom_after_disabled_attribute_set`,
`dom_after_select_multiple_removed`, `dom_after_default_checked_set`,
`dom_after_default_selected_set`) and is empty for the rest. The module's tag
guards, `m4b` accessors, `RADIANT_GUARDED_*` wrappers, externs and
`BIND_FIELD_SET` rows are all expansions of this file, emitted as
`reflect_attr` rows with no handler functions at all.

Hop count for a reflected read (`el.disabled`), counted as in §2.4:

| | Today | After DS4 |
|---|---|---|
| 1 | `jube_member_call_by_ordinal` | `jube_member_call_by_ordinal` |
| 2 | `radiant_html_disabled_get` (guard wrapper) | generic reflect routine |
| 3 | `radiant_dom_m4b_disabled_get` | `elem->has_attribute` |
| 4 | `radiant_dom_reflected_bool_get` | — |
| 5 | `elem->has_attribute` | — |

**Five hops become three**, and the parallel `prop_id == JS_DOM_PROP_DISABLED`
arm in `dom_set_property_impl` that has to agree with them goes with DS3.

### DS5 — a native body must earn its row

Every `CORE` row is re-examined against one question: *is this the mechanism, or
is it a composition?* A composition becomes `DERIVED` with `DOM_NO_BODY` unless
a measurement on a `make release` build shows the derivation costs more than a
stated budget on a stated workload. `DOM_F_FASTPATH` then means what it says,
and the ES43 oracle proves the survivor equal to its derivation.

First-round demotion candidates (census, not a commitment):

| Cluster | Demote | Keep core |
|---|---|---|
| range | `range_get_{start,end}_{container,offset}`, `range_get_collapsed`, `range_get_common_ancestor`, `range_set_{start,end}_{before,after}`, `range_is_point_in_range`, `range_intersects_node`, `range_compare_point`, `range_clone_range`, `range_surround_contents` (≈ 14) | `range_boundaries`, `range_set_start`, `range_set_end`, `range_compare_boundary_points`, `range_extract_contents`, `range_insert_node`, `range_clone_contents`, the two geometry rows |
| selection | `selection_get_{anchor,focus}_{node,offset}`, `selection_get_is_collapsed`, `selection_get_type`, `selection_get_direction`, `selection_collapse_to_{start,end}`, `selection_set_position`, `selection_contains_node`, `selection_to_string`, `selection_delete_from_document` (≈ 14) | `selection_boundaries`, `set_base_and_extent`, `selection_{add,remove}_range`, `selection_remove_all_ranges`, `selection_get_range_{at,count}`, `selection_modify` |
| node/attr | `attribute_names` (→ DS6) | the eight link/identity rows |

### DS6 — duplicate rows are merged

`range_collapsed`/`range_get_collapsed` and
`set_base_and_extent`/`selection_set_base_and_extent` collapse to one row each;
the surviving spelling keeps the other as a `js_name` alias where a JS caller
needs it. A lint rule (DS11) keeps the catalog free of same-body rows.

### DS7 — Radiant DOM state reaches Lambda as a virtual carrier, never a snapshot

Three concrete changes:

1. **`attribute_names` retires as a snapshot row.** The node is already a
   `velmt` whose `VelmtOps::attrs` answers `count` and `key_at`; a Lambda script
   reads attribute names off the element itself. The row becomes `DERIVED` over
   the element's own attribute face, and the collect loop goes.
2. **`form_controls`, `radio_group`, `details_group` return the existing live
   `VArray` backends** rather than building an array. `form_controls` is a
   one-line change — `dom_live_form_elements_bridge` already exists and is
   already registered as `html_form_controls_collection`.
3. **`dom_module.cpp`'s snapshot policy note is replaced** by the D7.4.5v2 rule,
   and the `dom.*` face stops being documented as snapshot-only.

### DS8 — `dom_core.cpp` stops re-entering the property dispatcher

The 21 `dom_prop_get(n, "camelCaseName")` bodies call their operation's row (or
the `velmt` vtable) directly. After DS2 most of them have a row of their own and
the wrapper disappears entirely. The `dom_op0..3` helpers go with the ordinal.

The Lambda door shortens by the same two hops as the JS one:

```
dom.get_attribute(n, "x")            TODAY (5)          AFTER (3)
  1  dom_pub_get_attribute            ✓                  ✓
  2  dom_core_get_attribute           ✓                  ✓  (now the body)
  3  dom_op1                          ✓                  —
  4  dom_element_operation_impl       ✓ 80-test scan     —
  5  elem->get_attribute              ✓                  ✓
```

```
dom.children(n)                       TODAY (5)          AFTER (3)
  1  dom_pub_children                 ✓                  ✓
  2  dom_fp_children                  ✓                  ✓  (now the body)
  3  dom_prop_get(n, "children")      ✓ by STRING        —
  4  dom_get_property_impl            ✓ bsearch + scan   —
  5  dom_live_child_collection_bridge ✓                  ✓
```

And under DS7/DS10 a structural read — `n[i]`, `len(n)`, `n.attrname` — takes
**one** hop: the `velmt` vtable entry, with no `dom.*` call in the path at all.
The `.ls` package currently spends 30 `dom.get_attribute` sites at five hops
each on exactly that.

### DS9 — the `radiant.*` `velmt_*` family is folded into the element face

`fn_radiant_velmt_{tag,id,attr,attr_or,children,text,width,height,box,index,style,style_or,border,margin,padding}`
are fifteen `radiant.*` names for things a `velmt` either already answers
structurally (`tag`, `children`, `attr`, `index`) or that belong in the
`geometry` cluster (`box`, `width`, `height`, `border`, `margin`, `padding`).
The structural ones retire; the geometric ones become catalog rows so the
`.ls` package reaches them through one face. ES44 already schedules the
`radiant.*` alias removal; this names which of them are structural.

### DS10 — the `.ls` package migrates off the name-keyed doors

475 `dom.*`/`radiant.*` call sites over 156 distinct names in `lambda/dom/*.ls`.
The 30 `dom.get_attribute(n, "x")` sites and the structural readers
(`node_name`, `node_value`, `node_type`) become `velmt` faces. This is a
source-compatibility change to Lambda code under our control and is gated by
the existing `.ls` goldens.

### DS11 — two lint rules keep the collapse from reopening

- `no-hand-written-dom-bind` — a `JubeMemberBind` initializer for a DOM type
  outside a `dom_api.def` expansion is an error.
- `no-dom-name-dispatch` — `strcmp`/`prop_id ==` against a name that has a
  catalog row, inside `lambda/dom/`, is an error.

Both fit the existing `utils/lint/rules/c-cpp/*.yml` shape and run under
`make lint`.

### DS12 — `dom.cpp` splits along the seam the collapse exposes

Removing the dispatchers leaves `dom.cpp` as a set of unrelated algorithm
families. It splits into `dom_attr.cpp`, `dom_tree.cpp`, `dom_serialize.cpp`,
`dom_query.cpp`, `dom_geometry.cpp`, `dom_forms.cpp`, keeping `dom.cpp` for the
wrapper/identity layer. **The split is credited zero lines against the exit
gate** — it moves code, it does not remove it — and it is listed last so it
cannot be mistaken for progress. See the stale-artifact hazard in §9.

---

### DS13 — the record's slot is the row body; the member ABI carries the arity

`JubeMemberRecord` **already carries `int arity`** ("methods: declared parameter
count"), parsed from the interface declaration. The only reason a member call
cannot land on a catalog slot today is that `JubeMemberBind.call` is
`int (Item, Item*, int, Item*)` while a row body is `Item (Item, …)` at the
row's own arity.

So the bind gains one field — the row body plus the fact that it *is* a row —
and `jube_member_call_by_ordinal` / `jube_dispatch_get_record` switch on
`rec->arity` and call the slot directly. Argument padding and truncation (JS may
call with any count) happen **once**, in that switch, instead of in 84 generated
thunks and 80 `argc < n` tests inside the arms.

This is DSO7 promoted to a ruling, because the three-hop budget cannot be met
without it. It costs a `JUBE_ABI_VERSION` bump — the same kind D7.4.4 and
D7.4.5v2 each took. It removes one hop from **every** JS member access.

### DS14 — a `CORE` row never reaches its own mechanism by name

No catalog body may call `dom_prop_get`, `dom_get_property_impl` or
`dom_element_operation_impl`. The eight node-link rows (F-8) read the engine
link directly — `node->parent`, `node->first_child`, `node->node_name()` — or,
where the script-visible traversal rule applies, the one shared
`radiant_dom_*_script_visible_*` walker the `velmt` vtable already uses.

This is DS8 stated as a prohibition rather than a cleanup, so it is checkable:
the DS11 lint rule `no-dom-name-dispatch` enforces it.

### DS15 — document members are `iface: document` rows, not string re-lookups

The 76 `RADIANT_DOC_*_FN` two-liners are deleted. Each document member becomes
a catalog row whose body takes the document **explicitly**
(`dom_document_get_property_for` already proves this is possible — ESO101 built
it), so the active-document swap/restore disappears from the read path along
with the string round-trip. The ambient-global `dom_document_proxy_get_property(key)`
door survives only for the genuinely open-name cases (named access on the
document, expandos).

### DS16 — one residual resolver, and the geometry commit is a row flag

`radiant_dom_node_named_get`, `radiant_dom_host_get_property` and
`radiant_dom_get_property` collapse into a single `named_get` hook that *is*
the residual resolver. Its two pieces of real work move out:

- `labels` becomes a catalog row (it is a DOM operation, not a fallback case);
- the geometry commit becomes a per-row flag, **`DOM_F_GEOMETRY`**, emitted as a
  one-line prologue in the generated thunk for the nine rows that need it,
  instead of nine string comparisons on every property read of every node.

---

## 4.5 The three-hop budget

**DS17 — every published DOM operation reaches Radiant state in at most three
calls: one dispatch, one body, one engine access.**

*Definition, so the number is checkable rather than rhetorical.* A **hop** is a
call on the **dispatch path**: from the script site to the first function that
reads or writes Radiant state. Calls a body makes **after** it has begun doing
its own work — walking siblings, building a `VArray`, parsing a selector — are
the operation, not dispatch, and are not hops. A hop is a dispatch hop if
removing it would still leave the same state access reachable.

Per access class, today → after:

| # | Class | Today | After | Requires |
|---|---|---:|---:|---|
| A | JS method on a node — `el.getAttribute("x")` | 6 | **3** | DS1, DS2, **DS13** |
| B | JS field on a node — `el.nodeName`, `el.firstChild` | 5 | **3** | DS13, **DS14** |
| C | JS reflected attribute — `el.disabled` | 5 | **3** | **DS4** |
| D | JS document member — `document.body` | 5 (+2 global swaps) | **3** | DS13, **DS15** |
| E | JS open name / expando — `el.foo` | 6 | **3** | DS3, **DS16** |
| F | JS collection index — `el.children[0]` | 3 | **3** | already met (`indexed_get` → VArray vtable) |
| G | Lambda `dom.*` — `dom.get_attribute(n,"x")` | 5 | **3** | DS2, DS8 |
| H | Lambda structural — `n[i]`, `len(n)`, `n.attr` | 5 (via `dom.*`) | **1** | DS7, DS10 |
| I | Lambda `radiant.*` engine call | 2–3 | **2–3** | unchanged (out of scope) |

The three-hop shape, once, for all of A–E and G:

```
script site
 └─ record dispatch          (1)  jube_member_* / dom_pub_*  — resolves the member
     └─ the row body         (2)  dom_core_<name>            — the operation
         └─ engine access    (3)  DomElement::… / DomNode::… — Radiant state
```

**Where the budget is tight.** Class G spends hop 1 on the `dom_pub_*` absence
trampoline (ESO107). It fits in three, but only because DS8 removes `dom_op1`.
If a later row needs a fourth, the trampoline is the one to elide: flag the rows
whose bodies can answer `undefined` (`DOM_F_ABSENT`) and generate the
trampoline only for those, taking the rest to **two**. Recorded as an option,
not a requirement — the budget is met without it.

**Where three is the floor.** Hops 2 and 3 cannot merge. Hop 2 does the work
that makes the operation an operation — unwrapping `Item` → `DomNode*`,
filtering `__lambda_*` internal attributes, mapping a missing attribute to null
versus an empty string. Hop 3 is Radiant's own method. Collapsing them would put
DOM semantics inside the engine, which ES47 separates on purpose. Class F and H
reach two and one only because a vtable *is* the dispatch.

---

## 5. What this does *not* change

- **No new fast path, no new cache.** LC1v2 rules out inline caches in both
  lanes; nothing here introduces one.
- **ES45 holds.** Every JS DOM access still crosses the catalog — more of them
  than today, since DS2 puts the ordinal path on it.
- **ES47 holds.** The downward (script → DOM) and upward (DOM → script) APIs
  stay separate; DS1–DS12 touch only the downward one.
- **Absence semantics unchanged.** The Lambda face keeps `dom_absent_to_null`
  at the publication boundary (ESO107); JS keeps `undefined`.
- **No vendored code is touched.**

---

## 6. Implementation phases

Each phase is independently landable and independently gated.

### 6.0 Landed so far (2026-09-11)

**DS14 — the eight node-link `CORE` rows** (`node_type`, `node_name`,
`node_value`, `parent_node`, `first_child`, `last_child`, `next_sibling`,
`previous_sibling`). Bodies moved from `dom_core.cpp`, where each was
`dom_prop_get(n, "camelCaseName")`, into `dom.cpp` beside the script-visible
traversal helpers and the node-kind rules they need. The property-chain arms
now delegate *to* the rows instead of the rows re-entering the chain, so the
two doors keep one implementation (ES38, D7.4.4).

**DS14 extended — the seven element-traversal rows** (`parent_element`,
`first_element_child`, `last_element_child`, `next_element_sibling`,
`previous_element_sibling`, `children`, `child_nodes`). Same inversion. These
carry `DOM_F_FASTPATH`, so until now the declared *fast path* was slower than
the derivation it exists to accelerate; the flag states a fact for the first
time. The element-skipping walk was written out four times in the chain and
twice more in the textlike handler; it is one `dom_first_element_from(start,
forward)` helper now.

Incidental, found by the change rather than looked for:

- A **dead duplicate** `JS_DOM_PROP_CHILDREN` arm — two arms for one property,
  the second unreachable. Removed, with its now-unused `dom_collect_child_nodes`
  wrapper (the compiler's `-Werror=unused-function` found that one).
- **A live two-door divergence, recorded not fixed (see DSO8).**

**DS14 completed — the serialization rows** (`text_content`, `inner_html`,
`outer_html`) and two more re-entries the lint rule found that a grep for
string literals could not: `dom_core_serialize`, which chose the property name
*dynamically* (`is_outer ? "outerHTML" : "innerHTML"`), and `dom_fp_root_node`,
which called `dom_prop_get(cur, "parentNode")` **once per ancestor** — so a
deep tree paid the 152-name bsearch and the linear chain on every step of the
walk.

**DS11 (first rule) — `no-dom-row-name-dispatch`**, at
`utils/lint/rules/c-cpp/no-dom-row-name-dispatch.yml` and registered in the
lint manifest. It makes the invariant checkable: *no catalog body reaches its
own mechanism by property name*. It found the two cases above on its first run,
which is the argument for writing the rule rather than trusting the sweep.

| Measure | Before | After |
|---|---:|---:|
| `dom_prop_get` string re-entries in `dom_core.cpp` | 21 | **3**, each suppressed with a recorded DSO id |
| Rows whose body re-enters the property protocol | 18 | **0** unblocked |
| Net code lines | — | **≈ −4** |

The three that remain are blocked on recorded design issues, not on effort:
`owner_document` (DSO9), and the two scroll reads inside `scroll_state`
(DSO8). Both carry `// DOM_NAME_DISPATCH_OK: <id>` so the rule passes clean
while the debt stays visible.

**Net lines are ~zero, and that is the expected shape**: DS14 buys *hops*, not
lines (classes B and G, 5 → 3). The line gate is carried by P1–P3. The earlier
claim that DS14 contributed to P4's ≈190-line budget was wrong — that budget is
the snapshot→`VArray` work, which is untouched.

**DS4 (partial) — one reflection table for the module.**
`lambda/dom/dom_reflect.def` now declares 16 reflected attributes once, as
`(name, attr, [fallback,] tags)`. `radiant_dom_bridge.cpp` generates the
accessor pair from it and `radiant_dom_iface.cpp` generates the matching
`extern` declarations, so a binding row cannot name an accessor the table does
not define. Per attribute this replaced a `RADIANT_REFLECT_*` invocation, a
`RADIANT_GUARDED_GET`/`_SET` pair and two `extern`s; the element set an
attribute exists on is now stated beside the attribute instead of in a
separately-named guard function. **Nine tag-set guards became dead and were
removed** — they are `RADIANT_C_API`, so the compiler could not flag them.

All 16 tag sets were verified byte-identical against the guards they replaced,
mechanically rather than by eye.

Two rows were deliberately kept out of the table, because they are not
reflections:

- **`href`** resolves against the document base URL on `<a>`/`<area>`, so it is
  a computation. Putting it in the table would have silently broken anchor
  URL resolution.
- **`disabled`, `src`, `checked`, `selected`, `multiple`** run an invariant hook
  on set: their setter is a reflection *plus* a state transition.

**Correction to the P2 estimate (≈300 lines).** The real yield is **−47**. The
estimate double-counted: the per-attribute *accessor* collapse had already been
done by an earlier round (`RADIANT_REFLECT_BOOL/INT/STRING` were already one
line each), so what remained to remove was only the guard/wrapper/extern layer.
The ceiling for this technique was ~−100, not −300, and the `.def` itself costs
40 lines back.

**`reflect_attr` is still unused, and that is now a considered decision.**
Wiring it as the ABI describes would put the kind-aware logic (presence for
boolean, parse-with-default for long, verbatim for DOMString) inside
`jube_interface.cpp`, which must not know DOM semantics — or else add a hop to
reach a module hook. It becomes the right mechanism only alongside **DS13**:
with the record able to call a module-supplied reflect hook directly,
`reflect_attr` supplies the attribute name and the per-attribute thunk
disappears, taking class C to three hops. Until then the generated thunk is
what keeps the kind logic on the DOM side.

**P1 slice 1 — the ordinal table.** `lambda/dom/dom_element_ops.def` declares
the 83 element operations once, as `(NAME, thunk)`. The ordinal enum in
`jube.h`, the module's 84 binding thunks and their 84 `extern` declarations are
now expansions of it: **253 hand-written lines became 18.** Row order is stated
in the file to be the ABI, since the ordinal is the value that crosses the host
API.

Verified mechanically, not by eye: the ordinal sequence is **identical** to the
previous enum position-for-position, and the set of generated thunk symbols is
identical to the set that existed before.

Two entries resisted derivation and are recorded rather than smoothed over:
`remove2` (HTMLSelectElement.remove(index) and ChildNode.remove() are one
ordinal under two member names, so the second binding is an alias, not a row),
and the three `__lambda_` automation entries, whose script-facing spelling is
deliberately reserved rather than derived — the table carries their thunk
suffix explicitly.

**A negative result worth recording: the `BIND_CALL` rows are not worth
generating.** DS1 assumed they were. Measured: 86 rows across 5 binding tables,
83 name-derivable. But the preprocessor can only filter rows per table by
pasting the interface into a macro name, which costs 6 filter definitions plus
6 `#undef`s per table — ~65 lines to save ~83, and the rows carry information
the table would then have to carry instead (which table, which JS spelling).
**So DS1's line value is in the thunks, the externs and the enum — already
taken here — not in the bind rows.** The bind rows remain worth unifying for
*correctness* (one declaration that cannot disagree), which is DS13's argument,
not a line-count one.

**P1 slice 2 — duplicate member accessors.** The module kept its own
implementation of members the catalog already has. Removed:

- **9 accessors that were defined and never bound** (`first_child`, `last_child`,
  `next_sibling`, `previous_sibling`, `node_type`, `child_nodes`,
  `owner_document`, `parent_node`, `is_connected`) — the `_any` variants,
  already routed through the catalog, are what the binding tables use. They were
  `RADIANT_C_API`, so `-Werror=unused-function` could not see them.
- **4 element-traversal accessors** now route through the catalog
  (`first_element_child`, `last_element_child`, `next_element_sibling`,
  `previous_element_sibling`), which made the module's four duplicate
  element-skipping walkers dead. The compiler caught those, as intended.

**`children` was reverted: converting it regresses page load.** Routing
`radiant_dom_member_children` through the catalog row made
`bootstrap-5-kitchen-sink` exit -1 with peak RSS 283 MB against a 160 MB limit.
Isolated properly rather than guessed: with the change stashed the suite is
105/105, with it 104/1; a bisect then showed the four traversal conversions are
green and `children` alone is the cause. The page loads fine under `lambda.exe
view` standalone, so it needs the full-suite harness to show. **Root cause not
found**, so the conversion stays reverted (rule 1: no workaround) and this is
recorded as DSO11 rather than patched around.

Gates: build clean; **all five ES43 derivation oracles pass**
(`dom_derive_{traversal,chardata,query,forms,urlencode}` — `traversal` walks
every link on every node comparing native body against derivation);
**`make test-radiant-baseline` 3622 passed, 0 failed** (re-run after each
increment, including DS4's, which exercises form-control reflection through the
DOM UI integration and form layout suites).


### Phase P1 — DS1 + DS2 + DS13: one catalog, no ordinals, no adapter thunks

1. Add `iface` and `js_name` columns to every existing `dom_api.def` row
   (mechanical; `iface = ""` preserves today's behaviour).
2. Add the generation macros for bind rows, decl-string member lines and module
   thunks; switch **one** cluster (`attr`, 6 rows) over and verify byte-identical
   WPT and `.ls` golden output.
3. Promote the 31 ordinal-only operations to new rows and fold the 37 renamed
   ones onto their existing rows (the ordinal's spelling survives as `js_name`
   where JS needs it), one cluster at a time, each arm of
   `dom_element_operation_impl` becoming that row's body verbatim.
4. Add the arity field to `JubeMemberBind`, switch on `rec->arity` in
   `jube_member_call_by_ordinal` and `jube_dispatch_get_record`, bump
   `JUBE_ABI_VERSION` (DS13). Argument padding moves into that switch.
5. Delete the enum, `invoke_raw`, the 84 thunks, the 84 externs and the chain.
   With DS13 the thunks are not regenerated — they go entirely for any member
   with a catalog row.

**Budget: ≈ 400 code lines.** (89 enum + 168 thunk/extern + ≈ 120 chain
scaffolding + ≈ 40 `dom_op0..3` helpers + ≈ 80 `argc < n` prologue tests now
done once, less ≈ 100 for the arity switch and the remaining generation macros.)
**This is the phase that carries the ABI bump**, so it lands first and alone.

### Phase P2 — DS4: one reflection table, and `reflect_attr` finally wired

Write the generic reflect routine `jube_interface.cpp` promises but does not
have (≈ 30 lines). Extract `dom_reflect.def`; emit the module side as
`reflect_attr` rows with no handler functions. Delete the 13
`RADIANT_DOM_TAG_SET_GUARD`s, the 8 `RADIANT_REFLECT_BOOL`s, the ≈ 30
`RADIANT_GUARDED_GET`/`_SET` pairs, their ≈ 60 externs, and the 26 duplicate
bind rows.

**Budget: ≈ 300 code lines** (330 removed, 30 added).

### Phase P3 — DS3 + DS16: the chains shrink, the fallback stack collapses

Delete every arm whose name has a bind row, in this order: `node` cluster →
`html_element` → `input`/`select`/`textarea`/`option` → `document`. After each
group, run the WPT DOM suites and diff.

Then collapse `radiant_dom_node_named_get` / `radiant_dom_host_get_property` /
`radiant_dom_get_property` into one residual resolver, promote `labels` to a
row, and move the geometry commit onto `DOM_F_GEOMETRY`.

**Budget: ≈ 560 code lines** (of 1607 in the chains, plus ≈ 30 in the alias
stack).

### Phase P4 — DS7 + DS8 + DS14: carriers instead of snapshots, links direct

`attribute_names` → `velmt` attribute face; `form_controls`/`radio_group`/
`details_group` → live `VArray`; the 21 `dom_prop_get` re-entries → direct row
calls, **the eight node-link `CORE` rows first** (DS14) since every derivation
composes over them.

**Budget: ≈ 190 code lines.**

### Phase P5 — DS5 + DS6 + DS9: minimize the core

Run the re-tiering census, demote the ≈ 28 range/selection compositions, merge
the two duplicate rows, retire the structural `velmt_*` aliases. Each demoted
row's native body is deleted only once its derivation passes the ES43 oracle
(`test/lambda/dom_derive_*.ls`).

**Budget: ≈ 150 code lines**, plus 28 fewer native bodies to maintain.

### Phase P6 — DS15: the document stops looking itself up by name

Delete the 77 `RADIANT_DOC_*_FN` invocations and their three macros; each
document member becomes an `iface: document` row taking the document
explicitly. `dom_swap_active_document` / `dom_restore_active_document` leave the
property read path. The ambient-global door survives only for genuinely
open-name document access.

**Budget: ≈ 120 code lines.**

### Phase P7 — DS10 + DS11 + DS12 + DS17 verification

Package migration, lint rules, file split, and the hop-count audit that closes
DS17. **Budget: 0 credited lines.**

### Budget summary

| Phase | Net code lines removed | Classes it fixes |
|---|---:|---|
| P1 — one catalog, no ordinals, no thunks | 400 | A (→3), enables B, D |
| P2 — one reflection table | 300 | C (→3) |
| P3 — property chains + fallback stack | 560 | E (→3) |
| P4 — carriers, links direct | 190 | B (→3), G (→3), H (→1) |
| P5 — minimal core | 150 | — |
| P6 — document rows | 120 | D (→3) |
| **Total** | **1720** | all of A–H at ≤3 |
| **Exit gate** | **1000** | |

The 720-line margin is deliberate: P3's estimate is the least certain, because
some chain arms carry behaviour (geometry commits, mutation notification
ordering) that has to move rather than go. P1 alone clears 40% of the gate, and
P1–P3 clear it outright — the hop budget and the line gate are met by the same
work, in the same order.

---

## 7. Risk register

| Risk | Where | Mitigation |
|---|---|---|
| A chain arm carries an unrecorded side effect (ESO90: the `innerHTML` setter re-registers ids on Window; `set_attribute` runs reflection hooks; `insert_before` schedules iframe `load`) | P1, P3 | Move arms **verbatim**; never retype a body. Diff WPT output after every cluster, not every phase. |
| Precedence change: a record now wins where the chain used to | P3 | Delete an arm only after confirming the bind row exists for that receiver type *and* that its handler reaches the same body. |
| A demoted row is measurably slower | P5 | Demote behind the ES43 oracle plus a release-build timing on the WPT/`.ls` corpus; keep `DOM_F_FASTPATH` for any row that fails the budget, with the budget recorded in the row's comment. |
| Live carrier where a snapshot was semantically required | P4 | Only the four rows the spec calls live are converted. `query_selector_all` stays a snapshot — it is static by specification. |
| **`JUBE_ABI_VERSION` bump breaks prebuilt modules** | P1 (DS13) | Adding a field to `JubeMemberBind` changes `sizeof`, so any pre-built module carrying its own bind tables mis-strides them — and `modules/lang-python/lang-python.dylib` is checked in as a binary. `make build` does not rebuild it. P1 must bump `JUBE_ABI_VERSION` 6 → 7, rebuild every module in `modules/`, and verify the version guard rejects a stale one loudly rather than reading garbage. This is the same class of break that produced ABI 6 (D7.4.4 and D7.4.5v2 each took one), so the mechanism exists; the hazard is forgetting the rebuild. |
| Stale `.o` after the P7 split | P7 | Renamed/moved files leave objects in `build/` **and inside the `.a`**; purge both or the gates test stale code. |
| `JsRuntimeState` layout drift breaking `modules/node-*.dylib` | P1–P3 | `make build` does not rebuild them; rebuild both sides of any A/B. |
| DS15 changes *when* the active document is swapped | P6 | The swap/restore currently wraps every document property read. Removing it is correct only if every migrated row takes the document explicitly. Migrate a row and its swap in the same commit; the foreign-document WPT cases are the ones that will catch a miss. |

---

## 8. Gates

**Correctness (every phase):**

- `make test-lambda-baseline` — 100%.
- `make test-radiant-baseline` — 100%.
- `test/test_wpt_dom_nodes_gtest.exe`, `test_wpt_dom_events_gtest.exe`,
  `test_wpt_dom_ranges_gtest.exe` — no regression against the pre-phase run.
  Note the standing hazard: **20 segfaults and 2 spins pre-exist on HEAD**,
  hidden as skips; capture the pre-phase list so a pre-existing crash is not
  read as a new one, and vice versa.
- `test/lambda/dom_derive_*.ls` — the ES43 oracle, mandatory for P5.
- `make test` before declaring a phase done.

**The hop budget (DS17), audited at P7.** A script, run under the existing
`LAMBDA_*` tracing, walks one representative of each access class A–H and
asserts the call depth from the script site to the first Radiant state access is
≤ 3. It is committed with P1 so each phase can be measured against it, and it
is the artefact that closes DS17 — the number is checked, not asserted.

**Structural (P7 and the exit gate):**

- `make lint` including the two new rules.
- `dom_api_check.cpp` static assertions extended to the new columns: every
  row with a non-empty `iface` names a declared type, and every bind row is a
  catalog expansion.

**The line-count gate.** Measured with blank lines, `//` lines and `/* … */`
blocks excluded, over the nine `.cpp` files in §2.1 plus `lambda/dom/dom_api.def`
and `lambda/jube/jube.h`, taken at the phase's base commit and at its tip.
Generated expansions count as the macro definition only — a `.def` row is one
line, whatever it expands to. A file move contributes zero. The script and the
per-file baseline are committed with P1 so the number is reproducible rather
than asserted.

Baseline at HEAD `fa77dbdcc` for that set: 31 441 + 376 + 1 351 = **33 168**
code lines. Gate: **≤ 32 168** after P6; expected **≈ 31 448**. P1–P3 alone
(1 260) clear the gate.

---

## 9. Open questions

- **DSO1** — Does `iface` need to be a *set* (a member bound on both
  `html_element` and `svg_element`), or is one row per receiver type acceptable?
  27 of 232 bound names appear in more than one table today. A set column is
  more faithful; per-receiver rows are simpler to check. **Recommend the set**,
  spelled as a `|`-separated token list parsed at registration.
- **DSO2** — After DS2, is `JubeDomElementOperation` needed at all by the
  `radiant.*` face, or only by the DOM catalog? If the engine face still needs a
  selector, it should declare its own and not share the DOM's.
- **DSO3** — `editing` has 39 CORE rows and no census. It is engine-provided
  (`DOM_F_ENGINE`), so demotion may not apply, but 39 rows for one cluster wants
  an explanation in the design record.
- **DSO4** — Should the `document` node's property answers stay in
  `dom_document_get_property_for` (189 lines) or become `iface: document` rows?
  The latter is the DS1 shape; the former is what ESO101 just built.
- **DSO5** — `focus_candidates` has no carrier and is an engine list. Does it
  get a `VArray` backend in P4, or does it stay the one honest snapshot?
- **DSO6** — DS10 changes `.ls` source under our control. Does the package
  migrate in one commit (gated by the `.ls` goldens) or per file? Per file is
  safer; one commit keeps the two doors from coexisting.
- **DSO8 — a live two-door divergence on scroll geometry, found while landing
  DS14.** `dom_core_scroll_state` is a `CORE | DOM_F_NEUTRAL` row — the
  published `dom.scroll_state(n)` — and its body reads `scrollLeft`/`scrollTop`
  through the property protocol, which only *checks*
  `dom_has_committed_geometry_snapshot` and never commits. The commit exists
  only in `radiant_dom_get_property`, gated on
  `strncmp(prop, "scroll", 6)`, and only the JS door crosses it. So with layout
  pending, `dom.scroll_state(n)` and `el.scrollTop` can answer differently.
  This is DS16's case in the concrete: the geometry commit is a property-*name*
  test inside one door's wrapper rather than a property of the row. Fixing it
  is DS16 (`DOM_F_GEOMETRY`) and needs the DS1 columns, so it is recorded here
  rather than patched locally — patching it in `dom_core_scroll_state` would
  add a third place that knows which properties need layout.
- **DSO9 — `owner_document`'s two node-kind paths already disagree.** The
  element arm falls back to `dom_get_or_create_doc_node` when no realm is
  active; `dom_owner_document_from_node`, which serves text and comment nodes,
  does not — it answers `js_get_document_object_value()` regardless. So
  hoisting one body would change behaviour for CharacterData rather than
  preserve it. Which of the two is correct is a question for DS15 (the document
  rows), where the realm-free document object is already the subject.
- **DSO10 — the core and module reflection tables overlap but disagree, so
  they cannot simply be merged.** Measured: 11 rows exist only in the module
  (`accept`, `autocomplete`, `max`, `min`, `name`, `pattern`, `placeholder`,
  `size`(select), `step`, `target`, `wrap`), 12 only in the core
  (`contentEditable`, `defaultChecked`, `defaultSelected`, `disabled`,
  `enterKeyHint`, `formAction`, `formEncoding`, `formEnctype`, `formMethod`,
  `href`, `inputMode`, `tabIndex`), and **three shared names disagree on kind**
  — `acceptCharset`, `formTarget` and `htmlFor` are `MAP` (name-mapped) to the
  core and `STR` to the module. That is a behavioural question (which door is
  right?), not a refactor, so DS4 stopped at the module's own table and this is
  recorded rather than resolved. Whoever answers it should check what the core's
  `MAP` getter actually returns for those three, since the module's returns the
  attribute verbatim.
- **DSO11 — `children` cannot yet be routed through its catalog row.** The two
  implementations look equivalent by inspection — both end at
  `dom_live_child_collection_bridge(elem, true)` — but substituting one for the
  other crashes `bootstrap-5-kitchen-sink` under the page-load suite with a
  ~120 MB RSS excursion. Something differs about wrapper or collection lifetime
  between the module's direct call and the same call reached through the
  catalog slot, and it is not visible in the source. Worth finding before DS1
  routes more members through the catalog, because every such conversion has
  this shape. The four traversal members converted cleanly, so it is specific to
  the live collection, not to the catalog route itself.
- **DSO7 — PROMOTED to DS13.** It was listed here as optional on the grounds
  that the adapter thunk is generated and therefore free to maintain. That is
  true of maintenance and irrelevant to the budget: it is a real call on every
  JS member access, and without removing it class A stops at four. The ABI bump
  is the cost of the three-hop rule, not an optional extra.

---

## 10. Ledger

| ID | Ruling | Status |
|---|---|---|
| DS1 | One `dom_api.def` row per operation, carrying `iface` and `js_name`; every surface is an expansion | PROPOSED |
| DS2 | `JubeDomElementOperation` and `dom_element_operation_impl` are deleted | **PARTIAL** — the enum is now generated from `dom_element_ops.def`; deleting it needs DS13 |
| DS3 | The property protocol keeps only genuinely name-driven access | PROPOSED |
| DS4 | One reflection table (`dom_reflect.def`) generating core and module | **PARTIAL** — module side landed; core merge blocked on DSO10 |
| DS5 | A native body must earn its row; compositions are `DERIVED` | PROPOSED |
| DS6 | Duplicate rows merge; a lint rule keeps them merged | PROPOSED |
| DS7 | Radiant DOM state reaches Lambda as `velmt`/`varray`/`vmap`, never a snapshot | PROPOSED |
| DS8 | `dom_core.cpp` never re-enters the property dispatcher by name | PROPOSED |
| DS9 | The structural `radiant.velmt_*` aliases retire into the element face | PROPOSED |
| DS10 | The `.ls` package migrates off the name-keyed doors | PROPOSED |
| DS11 | Two lint rules gate the collapse | **`no-dom-row-name-dispatch` LANDED**; `no-hand-written-dom-bind` proposed |
| DS12 | `dom.cpp` splits, credited zero lines | PROPOSED |
| DS13 | The record's slot is the row body; the member ABI carries the arity | PROPOSED |
| DS14 | A `CORE` row never reaches its own mechanism by name | **LANDED** (3 rows suppressed on DSO8/DSO9) |
| DS15 | Document members are `iface: document` rows, not string re-lookups | PROPOSED |
| DS16 | One residual resolver; the geometry commit is a row flag | PROPOSED |
| DS17 | **Three hops, every path**: one dispatch, one body, one engine access | PROPOSED |
| DSO1–DSO6, DSO8–DSO11 | Open, §9 | OPEN |
| DSO7 | Promoted to DS13 | CLOSED |

### Hop counts, before and after

| Path | Today | After | What goes |
|---|---:|---:|---|
| JS `el.getAttribute("x")` | 6 | **3** | `radiant_dom_element_operation`, `invoke_raw`, the 80-test scan, the adapter thunk |
| JS `el.nodeName` / `el.firstChild` | 5 | **3** | the catalog wrapper, the string re-entry, the bsearch + chain |
| JS `el.disabled` (reflected) | 5 | **3** | the guard wrapper, the `m4b` accessor, the shared reflect helper |
| JS `document.body` | 5 (+2 global swaps) | **3** | the `DOC_GET_FN` re-lookup, the swap/restore, the document chain |
| JS `el.foo` (open name) | 6 | **3** | three of the four fallback aliases |
| JS `el.children[0]` | 3 | **3** | — already met |
| Lambda `dom.get_attribute(n,"x")` | 5 | **3** | `dom_op1`, the 80-test scan |
| Lambda `dom.children(n)` | 5 | **3** | the string re-entry, the bsearch + property chain |
| Lambda structural `n[i]` / `n.attr` / `len(n)` | 5 (via `dom.*`) | **1** | the whole `dom.*` door — the `velmt` vtable answers |

Counting rule in §4.5. Two is reachable for the Lambda `dom.*` rows that cannot
answer `undefined`, by eliding the absence trampoline; three is the floor for
everything else, because hop 2 (DOM semantics) and hop 3 (engine state) must
not merge — ES47 separates them on purpose.
