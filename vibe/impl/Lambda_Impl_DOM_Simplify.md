# DOM Simplify: collapsing the script-runtime ↔ Radiant DOM interface

- **Date:** 2026-09-11.
- **Status:** PROPOSED. No implementation has started; every number below is a
  measurement of HEAD, not of a candidate.
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
- **New ledger IDs:** `DS1`–`DS12` (rulings proposed here), `DSO1`–`DSO6`
  (open questions). Continues the host-API ledger at **ES48** and **ESO115**
  where a point needs to be recorded there instead.

---

## 1. Objective and completion contract

One DOM operation should be **declared once, implemented once, and reached in
one hop** from either script runtime. Today it is declared in as many as six
places and reached in five or six hops, and the "core" tier that secondary
operations are supposed to compose over is 83% of the catalog.

Three obligations, matching the three requirements:

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
| Member bind rows | `radiant_dom_iface.cpp`, 23 `JubeMemberBind` tables | **272** rows, 232 distinct names, **315** distinct handler functions |

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

Call path after DS2, for the §2.4 trace:

```
JS site → jube_member_call_by_ordinal → dom_catalog-><row>   (2 hops)
```

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

`JS_DOM_REFLECTED_ATTRS` moves to `lambda/dom/dom_reflect.def` with columns
`(idl, attr, kind, default, tags, js_name, after_hook)`. `after_hook` names the
invariant call a few attributes need (`dom_after_disabled_attribute_set`,
`dom_after_select_multiple_removed`, `dom_after_default_checked_set`,
`dom_after_default_selected_set`) and is empty for the rest. The module's tag
guards, `m4b` accessors, `RADIANT_GUARDED_*` wrappers, externs and `BIND_FIELD_SET`
rows are all expansions of this file.

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
the wrapper disappears entirely.

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

### Phase P1 — DS1 + DS2: one catalog, no ordinals

1. Add `iface` and `js_name` columns to every existing `dom_api.def` row
   (mechanical; `iface = ""` preserves today's behaviour).
2. Add the generation macros for bind rows, decl-string member lines and module
   thunks; switch **one** cluster (`attr`, 6 rows) over and verify byte-identical
   WPT and `.ls` golden output.
3. Promote the 31 ordinal-only operations to new rows and fold the 37 renamed
   ones onto their existing rows (the ordinal's spelling survives as `js_name`
   where JS needs it), one cluster at a time, each arm of
   `dom_element_operation_impl` becoming that row's body verbatim.
4. Delete the enum, `invoke_raw`, the 84 thunks, the 84 externs and the chain.

**Budget: ≈ 380 code lines.** (89 enum + 168 thunk/extern + ≈ 120 chain
scaffolding + ≈ 40 `dom_op0..3` delegation helpers in `dom_core.cpp`, less the
generation macros added.)

### Phase P2 — DS4: one reflection table

Extract `dom_reflect.def`; generate the module side. Delete the 13
`RADIANT_DOM_TAG_SET_GUARD`s, the 8 `RADIANT_REFLECT_BOOL`s, the ≈ 30
`RADIANT_GUARDED_GET`/`_SET` pairs, their ≈ 60 externs, and the 26 duplicate
bind rows.

**Budget: ≈ 330 code lines.**

### Phase P3 — DS3: the property chains shrink to what is name-driven

Delete every arm whose name has a bind row, in this order: `node` cluster →
`html_element` → `input`/`select`/`textarea`/`option` → `document`. After each
group, run the WPT DOM suites and diff.

**Budget: ≈ 550 code lines** (of 1607).

### Phase P4 — DS7 + DS8: carriers instead of snapshots

`attribute_names` → `velmt` attribute face; `form_controls`/`radio_group`/
`details_group` → live `VArray`; the 21 `dom_prop_get` re-entries → direct row
calls.

**Budget: ≈ 180 code lines.**

### Phase P5 — DS5 + DS6 + DS9: minimize the core

Run the re-tiering census, demote the ≈ 28 range/selection compositions, merge
the two duplicate rows, retire the structural `velmt_*` aliases. Each demoted
row's native body is deleted only once its derivation passes the ES43 oracle
(`test/lambda/dom_derive_*.ls`).

**Budget: ≈ 150 code lines**, plus 28 fewer native bodies to maintain.

### Phase P6 — DS10 + DS11 + DS12

Package migration, lint rules, file split. **Budget: 0 credited lines.**

### Budget summary

| Phase | Code lines removed |
|---|---:|
| P1 — one catalog, no ordinals | 380 |
| P2 — one reflection table | 330 |
| P3 — property chains | 550 |
| P4 — carriers, not snapshots | 180 |
| P5 — minimal core | 150 |
| **Total** | **1590** |
| **Exit gate** | **1000** |

The 590-line margin is deliberate: P3's estimate is the least certain, because
some chain arms carry behaviour (geometry commits, mutation notification
ordering) that has to move rather than go.

---

## 7. Risk register

| Risk | Where | Mitigation |
|---|---|---|
| A chain arm carries an unrecorded side effect (ESO90: the `innerHTML` setter re-registers ids on Window; `set_attribute` runs reflection hooks; `insert_before` schedules iframe `load`) | P1, P3 | Move arms **verbatim**; never retype a body. Diff WPT output after every cluster, not every phase. |
| Precedence change: a record now wins where the chain used to | P3 | Delete an arm only after confirming the bind row exists for that receiver type *and* that its handler reaches the same body. |
| A demoted row is measurably slower | P5 | Demote behind the ES43 oracle plus a release-build timing on the WPT/`.ls` corpus; keep `DOM_F_FASTPATH` for any row that fails the budget, with the budget recorded in the row's comment. |
| Live carrier where a snapshot was semantically required | P4 | Only the four rows the spec calls live are converted. `query_selector_all` stays a snapshot — it is static by specification. |
| Stale `.o` after the P6 split | P6 | Renamed/moved files leave objects in `build/` **and inside the `.a`**; purge both or the gates test stale code. |
| `JsRuntimeState` layout drift breaking `modules/node-*.dylib` | P1–P3 | `make build` does not rebuild them; rebuild both sides of any A/B. |

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

**Structural (P6 and the exit gate):**

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
code lines. Target after P5: **≤ 32 168**; expected **≈ 31 578**.

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

---

## 10. Ledger

| ID | Ruling | Status |
|---|---|---|
| DS1 | One `dom_api.def` row per operation, carrying `iface` and `js_name`; every surface is an expansion | PROPOSED |
| DS2 | `JubeDomElementOperation` and `dom_element_operation_impl` are deleted | PROPOSED |
| DS3 | The property protocol keeps only genuinely name-driven access | PROPOSED |
| DS4 | One reflection table (`dom_reflect.def`) generating core and module | PROPOSED |
| DS5 | A native body must earn its row; compositions are `DERIVED` | PROPOSED |
| DS6 | Duplicate rows merge; a lint rule keeps them merged | PROPOSED |
| DS7 | Radiant DOM state reaches Lambda as `velmt`/`varray`/`vmap`, never a snapshot | PROPOSED |
| DS8 | `dom_core.cpp` never re-enters the property dispatcher by name | PROPOSED |
| DS9 | The structural `radiant.velmt_*` aliases retire into the element face | PROPOSED |
| DS10 | The `.ls` package migrates off the name-keyed doors | PROPOSED |
| DS11 | Two lint rules gate the collapse | PROPOSED |
| DS12 | `dom.cpp` splits, credited zero lines | PROPOSED |
| DSO1–DSO6 | Open, §9 | OPEN |
