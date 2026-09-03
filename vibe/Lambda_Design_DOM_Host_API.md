# Lambda DOM Host API — one API in two tiers: core primitives and derived conveniences

> **Status**: **RATIFIED (rev 2) — implementation in progress** (2026-09-02, user: "proceed to impl the unified DOM host API"). DOM API **Phase 2** (the ESO78 target set by user: "two APIs becoming the new DOM API; dedup and unify, not only the API functions, but also the impl behind"). **Direction for this revision set by user 2026-09-02**: *dedup the APIs; define a set of core APIs; then a set of secondary APIs that derive from the core, provided for convenience and implemented by wrapping/calling the core.* Rulings **ES39–ES45 ratified**; stages **F28–F33 in progress** (§8 carries checkpoints). §2 verified against the tree at 2026-09-02 (post-`5f7b90a2d`).
> **Revision note (a wrong turn, recorded so it is not repeated)**: revision 1 organized the API by *calling mechanics* — flat "verb" slots admitted by usage frequency versus ordinal "executors" for the long tail — and generated everything from a catalog. It was rejected because it designed the ABI's grain without designing the API: a frequently-called operation is not thereby a *fundamental* one, and `closest`, `form_of`, `radio_group`, `focus_candidates` would all have been first-class "verbs" although each is a short walk over `parent`/`matches`/`attr`. The catalog mechanism and the realm split survive as implementation detail (§6); the organizing principle is now **irreducibility**, not frequency.
> **Role**: the design home for *what the DOM API is* — its deduplicated operation set, which operations are primitive, and how every other operation is defined in terms of the primitives. `Lambda_Design_DOM_API.md` owns where code lives and the core/adapter line; `DOM_Pkg` §4.2 owns *implementation language* placement (N vs L); this doc owns the **operation set and its two tiers**, which is orthogonal to both.
> **Companion docs**: `vibe/Lambda_Design_DOM_API.md` (§7 placeholder this fills; ES34 — the host table is the dynamic ABI and stays), `vibe/Lambda_Design_DOM_Pkg.md` §4.2–§4.3 (placement tests, the narrow-waist taxonomy), `vibe/Lambda_Design_DOM_Default.md` §2.4 (ES30 — generic traversal replaces policy-specific walkers: the precedent for the secondary tier), `vibe/Lambda_Design_DOM_State.md` (ES5/ES6/ES10 — the state waist), `vibe/Lambda_Design_Native_Module.md` §5–§6 (module ABI), `vibe/Lambda_Jube_DOM3.md`/`DOM4.md`.
> **Formal anchors**: D8 (module/runtime ownership), D7.4.1–D7.4.4 (the declared-interface records remain the JS property protocol; they bind to this API's bodies), D6.2.2v2, D4.5.1v3 (Radiant memory seam — node storage is mechanism), S9.2.2 (every collection this API returns is a snapshot), S9.1.4 (the state cluster is the view-state store), S12.1.3.
> **Ledger series**: extends the DOM area's `ES#`/`ESO#`/`F#` per `doc/Doc_Convention.md` §4. Decisions **ES39–ES44**; open issues **ESO84–ESO107** (ESO81, ESO93, ESO101, ESO106 and ESO107 resolved) (ESO81, ESO93 and ESO101 resolved); stages **F28–F33**.

---

## 1. What this document is

### 1.1 The problem

One DOM mechanism is exposed through three Lambda-visible-or-ABI surfaces that were never derived from one another: `radiant.*` (98 functions), `dom.*` (23, added in F26), and the `JubeHostDomAPI` table (218 slots) the JS bridge crosses. A census (§2) finds **388 distinct operation names** across them plus the ordinal enums and the property protocol, of which **47 are declared on two or more surfaces** under different spellings (`attr` / `get_attribute` / `JUBE_DOM_GET_ATTRIBUTE`; `dispatch` / `dispatch_event_bridge` / `JUBE_DOM_DISPATCH_EVENT`). Worse than the spelling drift is the *semantic* redundancy: most of the 388 are short compositions of a few dozen primitives, but nothing says so, so each grew its own body — which is how `radiant.first_element_child` came to walk a different tree than `el.firstElementChild` (ESO83).

### 1.2 What this proposal does

Defines **one API in two tiers**:

- **Core** — the irreducible primitives. An operation is core if it reaches mechanism script cannot otherwise touch (node links and storage, attribute storage, text buffers, the state store, the mutation ring, layout geometry, the dispatch loop, the parser/serializer, the selector matcher's *match*), **and** cannot be written as a finite composition of other core operations with identical observable results. About forty of them (§4).
- **Secondary** — everything else, each **defined by a derivation over core** written down next to it (§5). A secondary operation may have a native fast path where the derivation would be asymptotically worse on a hot path (`query_selector_all` via the selector engine rather than a Lambda DFS), but the derivation is its *specification* and its *test oracle*: the fast path must produce exactly what the derivation produces, and a fixture asserts it (ES43).

Then the three surfaces collapse onto it (§6): one canonical name and signature per operation, declared once; the host table carries the core (the true ABI) plus the secondary as wrappers (the convenience, per user direction); `dom.*` publishes both to Lambda; `radiant.*` keeps only what is not DOM; the JS declared interfaces bind to the same bodies.

### 1.3 Why this is the right cut for this codebase

Three rulings already point here and this proposal makes them one rule:

- **ES30** (DOM_Default §2.4) retired the policy-specific walkers — `form_of`, `radio_group`, `details_group`, `context_menu_target` — in favour of "one traversal implementation in Lambda rather than four native policy walkers" over a *generic* surface (`first_element_child`, `next_element_sibling`, `parent`, `closest`). That *is* the secondary-over-core move, made once for four operations. This doc makes it the definition of the tier.
- **ES38** (DOM_API) says one implementation per operation. Two tiers is how you get there without pretending `closest` and `parent` are peers: `closest` is *one body* precisely because it is `parent` in a loop with `matches`.
- **DOM_Pkg §3.3**'s placement tests (frequency, coupling, incumbency, shape) decide *which language* implements an operation. They never decided *which operations exist*. Core/secondary is the missing axis, and it is orthogonal: a secondary operation may still be N-implemented by the frequency test; a core operation is always N by the coupling test, since it touches mechanism by definition.

---

## 2. Current state (verified 2026-09-02)

### 2.1 The three surfaces and the ordinal/property layers beneath

| Surface | Count | Who consumes it |
|---|---|---|
| `radiant.*` (`radiant_module.cpp:2754–2952`) | 98 fns; **70** used by `lambda/package/dom/*.ls`, 28 test/POC-only | the UA behavior package; tests |
| `dom.*` (`dom_module.cpp`, F26) | 23 fns, hand-written, all delegating to the ordinal executor or property getter | `test/lambda/dom_api*.ls`; future Lambda scripts |
| `JubeHostDomAPI` (`jube.h:676–928`) | 223 fields = 173 live + 24 `NULL` + 26 unreferenced (F29 measured; the earlier "21 dead" undercounted because 66 range/selection/CSSOM slots are reached as *macro arguments* in `radiant_dom_iface.cpp`, which a `->dom->` grep cannot see) | **only** `lambda/module/radiant/` — no guest, no interpreter path reads `host->dom` today |
| element ordinals `JubeDomElementOperation` | 83 | the bridge's `dom_element_operation_impl` |
| document ordinals `RadiantDocumentOperation` | 38 | the bridge's internal switch |
| property protocol `JS_DOM_PROPS` (`dom.cpp:6058`) | 148 names | `dom_get_property_impl` — D7.4.4 member dispatch, both realms |

Union after normalizing spellings: **388 canonical operations**, **47 multi-declared**. Full list in Appendix A.

### 2.2 What the 388 actually are, by reducibility

Reading the union against the core criterion (§3) rather than against surface labels:

| Class | ≈ | Examples |
|---|---|---|
| **Irreducible mechanism** | ~40 | `insert_before`, `remove_child`, `set_attribute`, `set_node_value`, `matches`, `parse_fragment`, `serialize`, `get_state`/`set_state`, `dispatch`, `bounding_box`, `set_start`/`set_end` (live range), `set_base_and_extent` (selection), `tc_replace_range`, `focus_set`, `request_navigation`, `load` |
| **Reducible to core, currently with their own body** | ~150 | `append_child` (= `insert_before(p, n, null)`), `remove` (= `remove_child(parent(n), n)`), `closest`, `first_element_child`, `children`, `text_content`, `has_attribute`, `form_of`, `radio_group`, `check_validity`, `reset`, `range_select_node`, `range_collapse`, `selection_collapse_to_start`, `insert_adjacent_html`, `dom_wrap_range`, … |
| **Property reflections of core state** | ~110 | `id`/`class_name`/`href`/… (attribute reflections), `checked`/`value`/`selected` (state-store reflections), `offset_width`/`client_height` (geometry reads) — each a fixed derivation from one core read |
| **JS shape, not DOM** | ~28 | document proxy, prototype values, expandos, live collections, wrapper init (`Lambda_Design_DOM_API.md` ES33 / ESO79) |
| **Engine, not DOM** | ~28 | `layout`, `render_svg`, `box`, `register_layout`, `velmt_*` — stay `radiant.*` |
| **Dead / retired** | 26 (F29 measured; 45 was the estimate) | 23 `NULL` slots plus `is_css_namespace`, `item_is_range`, `item_is_selection`; the 24th `NULL` slot, `style_resource_has_property`, survives because a bridge `#define` still names it |

The middle rows are the point: roughly **260 of 388 operations are derivable**, and today almost every one of them has a native body of its own.

### 2.3 Where bodies live is bimodal, and the table only sees half

Core bodies split between the DOM core (`lambda/dom/`, target `lambda-rt`: tree, attributes, matching, parse/serialize, geometry reads) and the Radiant engine (`radiant/*.cpp`: the state store `state_store_*`, `radiant_dispatch_event_from_script`, the editing waist `editing_dom_*`, the text-control buffer `tc_*`, focus, caret, navigation). Only the first half is in the host table. A guest module today can reach `get_attribute` but not `set_state` or `dispatch` — the two operations the policy layer calls most (16 and 6 package call sites). Both halves are host code (the engine is not the module), so this is a gap in the *table*, not a layering constraint.

---

## 3. The core criterion

An operation is **core** iff both hold:

1. **Mechanism reach.** It reads or mutates state that no other operation exposes: the node link fields (`parent`, `first_child`, `last_child`, `next_sibling`, `previous_sibling`), attribute storage, a text node's data buffer, a text control's value buffer and selection, the canonical state store (S9.1.4), the live-range and selection boundary storage, the mutation ring, layout geometry, the event listener store and dispatch loop, the HTML/XML parser and serializer, the selector matcher, document loading, navigation execution, focus commit.
2. **Irreducibility.** It is not expressible as a finite composition of *other* core operations with identical observable results — including identical mutation records and identical live-range adjustments.

An operation is **secondary** iff it fails (2): it has a derivation. The derivation is written in the catalog beside it, in pseudo-Lambda over core names, and is normative.

Two consequences the criterion forces, both deliberate:

- **`has_attribute` is secondary** (`get_attribute(n, name) != null`), even though the struct has a dedicated `has_attribute()` method. The method is a fast path; the API does not grow a primitive for it.
- **`clone_node` is secondary**: `create_node` + `set_attribute` per attribute + recursion is a finite composition with identical result per the DOM Standard's cloning steps, which copy attributes and children and nothing else. The native body stays as the fast path, oracle-tested against the derivation (ES43). This is the kind of call the criterion settles mechanically that taste would argue about.

Where the criterion needs a judgment call, the call and its reason are recorded in §4/§5 rather than left implicit.

---

## 4. The core set (proposed, ~40)

Canonical names are snake_case. Signatures are Lambda type syntax as the catalog will carry them. "Body" says where the mechanism lives today.

### 4.1 Node identity and links (read) — the tree is the mechanism

| Core op | Signature | Why core |
|---|---|---|
| `node_type` | `fn(n: dom_node) -> int` | stored kind |
| `node_name` | `fn(n: dom_node) -> string` | stored tag/name; `tag_name`, `local_name` derive |
| `node_value` | `fn(n: dom_node) -> string\|null` | the text/comment data buffer |
| `parent_node` | `fn(n: dom_node) -> dom_node\|null` | link field |
| `first_child`, `last_child` | `fn(n: dom_node) -> dom_node\|null` | link fields |
| `next_sibling`, `previous_sibling` | `fn(n: dom_node) -> dom_node\|null` | link fields |
| `owner_document` | `fn(n: dom_node) -> dom_node` | stored owner |

All traversal returns the **script-visible** tree (generated pseudo-nodes skipped, anonymous wrappers transparent — the `dom_visible_child` rule ESO83 established). That rule lives *here*, once, so no derived traversal can disagree with it again.

### 4.2 Attributes

| Core op | Signature | Why core |
|---|---|---|
| `get_attribute` | `fn(n: dom_node, name: string) -> string\|null` | attribute storage read |
| `set_attribute` | `fn(n: dom_node, name: string, value: string) -> null` | storage write + mutation record + reflection side effects (`after_attribute_write` folds in here) |
| `remove_attribute` | `fn(n: dom_node, name: string) -> null` | storage write + record |
| `attribute_names` | `fn(n: dom_node) -> array` | enumeration of storage (snapshot) |

`has_attribute`, `toggle_attribute`, `id`/`class_name`/every reflected IDL attribute: secondary.

### 4.3 Tree mutation

| Core op | Signature | Why core |
|---|---|---|
| `create_node` | `fn(doc: dom_node, type: int, name: string\|null, data: string\|null) -> dom_node` | allocation in document storage |
| `insert_before` | `fn(parent: dom_node, node: dom_node, ref: dom_node\|null) -> dom_node` | the one structural write: link update, live-range adjustment, mutation record, connectedness |
| `remove_child` | `fn(parent: dom_node, node: dom_node) -> dom_node` | the other |
| `set_node_value` | `fn(n: dom_node, data: string) -> null` | text buffer write + live-range offset adjustment |

`append_child`, `remove`, `replace_child`, `replace_with`, `before`/`after`, `append`/`prepend`, `insert_adjacent_*`, `clone_node`, `normalize`, `insert_data`/`append_data`/`delete_data`/`replace_data`/`substring_data`, `split_text`, `create_element`/`create_text_node`/`create_comment`/`create_document_fragment`: secondary (§5.2).

### 4.4 Matching, parsing, serializing

| Core op | Signature | Why core |
|---|---|---|
| `matches` | `fn(n: dom_node, selector: string) -> bool` | the selector engine's match primitive — one engine shared with layout |
| `parse_fragment` | `fn(context: dom_node, markup: string) -> dom_node` | the HTML5 fragment parser; returns a detached fragment |
| `serialize` | `fn(n: dom_node, outer: bool) -> string` | the serializer |

`query_selector`, `query_selector_all`, `closest`, `get_element_by_id`, `get_elements_by_*`, `inner_html` (get/set), `outer_html`, `insert_adjacent_html`, `text_content` (get/set): secondary.

### 4.5 Documents

| Core op | Signature | Why core |
|---|---|---|
| `load` | `fn(path: string) -> dom_node` | the engine loader (`load_lambda_html_doc`) — closes ESO80 |
| `parse_document` | `fn(markup: string, mime: string) -> dom_node` | the document parser (DOMParser body) |

`document_element`, `body`, `head`, `title`: secondary.

### 4.6 State and events — the S9.1.4 store and the dispatch loop

| Core op | Signature | Why core |
|---|---|---|
| `get_state` | `fn(n: dom_node, name: string) -> any` | canonical view-state read |
| `set_state` | `fn(n: dom_node, name: string, value: any) -> bool` | canonical write (ES10: the one store both realms write) |
| `request_change` | `fn(n: dom_node) -> bool` | the change-request epoch |
| `add_listener` / `remove_listener` | `fn(n: dom_node, type: string, fn: fn, opts: map\|null) -> null` | listener store |
| `dispatch` | `fn(n: dom_node, event: any) -> bool` | the F17 record through the F18 cascade; returns not-cancelled |
| `create_event` | `fn(type: string, init: map\|null) -> any` | the native event record (ES24) |

`checked`/`value`/`selected`/`selected_index`/`disabled`-as-state, `reset`, `request_submit`, `check_validity`, `report_validity`, `click`: secondary.

### 4.7 Geometry and scroll — layout is mechanism

| Core op | Signature | Why core |
|---|---|---|
| `bounding_box` | `fn(n: dom_node) -> map\|null` | layout read (post-layout contract per ES30) |
| `client_rects` | `fn(n: dom_node) -> array` | fragment boxes (snapshot) |
| `scroll_state` / `set_scroll_state` | `fn(n: dom_node) -> map`, `fn(n: dom_node, x: float, y: float) -> null` | scrollport state |
| `element_from_point` | `fn(doc: dom_node, x: float, y: float) -> dom_node\|null` | hit test |
| `scroll_into_view` | `fn(n: dom_node) -> bool` | geometry-dependent action (ES30 keeps it native) |

`offset_*`/`client_*`/`scroll_*` reflections, `scroll_by`/`scroll_to`: secondary.

### 4.8 Focus, navigation, actions — engine mechanism the package drives

| Core op | Signature | Why core |
|---|---|---|
| `is_focusable` | `fn(n: dom_node) -> bool` | layout-coupled query (DOM_Pkg §4.2: M) |
| `focus_set` | `fn(n: dom_node, from_keyboard: bool) -> bool` | atomic focus commit (ES30) |
| `focused` | `fn(n: dom_node) -> bool` | focus state read |
| `request_navigation` | `fn(request: map) -> bool` | navigation execution (ES31) |
| `navigation_destination` | `fn(src: dom_node, url: string, root: dom_node) -> map` | URL/same-document resolution (ES31 keeps native) |
| `activate_popover` | `fn(n: dom_node) -> bool` | top-layer transition (mechanism) |

`focus_candidates` (= walk + `is_focusable`), `blur`, `form_of`, `radio_group`, `details_group`: secondary — this is ES30 made general.

### 4.9 Text controls and editing — the buffer and the structural waist

| Core op | Signature | Why core |
|---|---|---|
| `tc_value` / `tc_set_value` | `fn(n: dom_node) -> string`, `fn(n: dom_node, v: string) -> bool` | the control's value buffer, distinct from the `value` attribute |
| `tc_selection` / `tc_set_selection` | `fn(n: dom_node) -> map`, `fn(n: dom_node, start: int, end: int, dir: string\|null) -> bool` | buffer selection state |
| `tc_replace_range` | `fn(n: dom_node, start: int, end: int, text: string) -> bool` | the UTF-8 splice (DOM_State ES9's one primitive) |
| `set_caret` | `fn(n: dom_node, offset: int) -> bool` | caret placement in a contenteditable host |
| `caret_operation` | `fn(n: dom_node, op: string, extend: bool) -> bool` | named caret/selection move over live geometry (ES30) |
| `edit_range` | `fn(host: dom_node) -> map\|null` | the pending edit range `{node, start, end, text}` — replaces `dom_edit_node/start/end/text` (four reads of one record) |
| `edit_replace_range` | `fn(host: dom_node, start: int, end: int, text: string) -> int\|null` | the structural text splice |
| `edit_insert_at_boundary`, `edit_split_block`, `edit_insert_break` | per `editing_dom_waist.cpp` | block-structure mechanics that need caret/range math the composition cannot see |
| `set_password_reveal` | `fn(n: dom_node, start: int, end: int) -> bool` | paint state |
| `key_intent` | `fn(n: dom_node, name: string) -> bool` | the F11 translation hook (deliberately survives `preventDefault`) |
| `ime_preedit` / `set_ime_preedit` / `clear_ime_preedit` | — | composition session state |
| `clipboard_text` | `fn() -> string\|null` | host clipboard read |
| `open_context_menu` / `context_menu_target` | — | host menu paint |

`dom_wrap_range`, `dom_unwrap_range`, `dom_range_format`, `dom_insert_html`, `dom_replace_dom_range`, `dom_delete_dom_range`, `dom_insert_paragraph`, `dom_insert_line_break`, `hover_index`/`set_hover_index`, `dropdown_open`/`set_dropdown_open`, `option_count`: secondary or state reflections (§5).

### 4.10 Ranges and selection — boundary storage is mechanism

| Core op | Signature | Why core |
|---|---|---|
| `create_range` | `fn(doc: dom_node) -> range` | live-range registration |
| `range_boundaries` | `fn(r: range) -> map` | `{start_container, start_offset, end_container, end_offset}` in one read |
| `range_set_start` / `range_set_end` | `fn(r: range, n: dom_node, offset: int) -> null` | boundary storage write with validation (`IndexSizeError`, `WrongDocumentError`) |
| `selection` | `fn(doc: dom_node) -> selection` | the document's selection |
| `selection_boundaries` | `fn(s: selection) -> map` | anchor/focus in one read |
| `set_base_and_extent` | `fn(s: selection, an: dom_node, ao: int, fn_: dom_node, fo: int) -> null` | the one selection write (`state_store_set_selection`) |
| `selection_modify` | `fn(s: selection, alter: string, dir: string, gran: string) -> null` | geometry-dependent move |

`collapse`, `select_node`, `select_node_contents`, `set_start_before/after`, `set_end_before/after`, `clone_range`, `compare_boundary_points`, `compare_point`, `is_point_in_range`, `intersects_node`, `common_ancestor`, `collapsed`, `to_string`, `clone_contents`, `extract_contents`, `delete_contents`, `insert_node`, `surround_contents`, and every `selection_*` beyond the three above: secondary (§5.6).

### 4.11 Style and CSSOM

| Core op | Signature | Why core |
|---|---|---|
| `computed_style` | `fn(n: dom_node, prop: string) -> string` | the cascade |
| `inline_style_set` / `inline_style_remove` | — | the declaration store behind the `style` attribute |
| `stylesheet_insert_rule` / `stylesheet_delete_rule` | — | live sheet mutation |
| `stylesheet_rules` | `fn(sheet: stylesheet) -> array` | rule enumeration (snapshot) |

CSSOM getters (`selector_text`, `css_text`, `href`, `disabled`, `type`, …) are property reflections; `css_supports`/`css_escape` are pure functions (secondary, no mechanism).

### 4.12 Core-internal (host side only, not in the script API)

`notify_mutation`, `wrap_node`/`unwrap_node`, `get_document`/`set_document`, `get_ui_context`, `has_committed_geometry_snapshot`, `to_attribute_cstr`. These are how core bodies and the bridge talk to the runtime; secondary bodies never need them because the core mutators already notify. They form the table's *context* section.

**Core count: 41 script-visible + 7 internal.** Every one has a mechanism reason; none has a derivation.

---

## 5. The secondary set (proposed) — defined by derivation

Each row's derivation is normative. **FP** marks an operation that keeps a native fast path (frequency test) — oracle-tested against the derivation. Pseudo-Lambda; `null` is the absent value.

### 5.1 Element-only traversal (ES30's surface, now derived once)

```
first_element_child(n)   = let c = first_child(n); while c != null and node_type(c) != ELEMENT: c = next_sibling(c); c
last_element_child(n)    = symmetric over last_child / previous_sibling
next_element_sibling(n)  = let s = next_sibling(n); while s != null and node_type(s) != ELEMENT: s = next_sibling(s); s
previous_element_sibling = symmetric
parent_element(n)        = let p = parent_node(n); if p != null and node_type(p) == ELEMENT: p else null
children(n)              = [for each element child of n]                 -- snapshot, S9.2.2
child_nodes(n)           = [for each child of n]                          -- snapshot
child_element_count(n)   = len(children(n))
contains(a, b)           = b == a or (parent_node(b) != null and contains(a, parent_node(b)))
is_connected(n)          = node_type(root_node(n)) == DOCUMENT
root_node(n)             = if parent_node(n) == null: n else root_node(parent_node(n))
document_element(doc)    = first_element_child(doc);  body(doc) = query_selector(doc, "body"); head(doc) = ...
```

### 5.2 Structural sugar over `insert_before` / `remove_child` / `create_node`

```
append_child(p, n)       = insert_before(p, n, null)
prepend(p, n)            = insert_before(p, n, first_child(p))
remove(n)                = remove_child(parent_node(n), n)
replace_child(p, new, old) = insert_before(p, new, old); remove_child(p, old); old
replace_with(n, new)     = replace_child(parent_node(n), new, n)
before(n, x) / after(n, x) = insert_before(parent_node(n), x, n) / insert_before(parent_node(n), x, next_sibling(n))
insert_adjacent_element(n, where, e) = the four cases above by `where`
create_element(doc, tag) = create_node(doc, ELEMENT, tag, null);  create_text_node(doc, s) = create_node(doc, TEXT, null, s)
create_comment, create_document_fragment                                     = likewise
clone_node(n, deep)  FP  = let c = create_node(owner_document(n), node_type(n), node_name(n), node_value(n));
                            for name in attribute_names(n): set_attribute(c, name, get_attribute(n, name));
                            if deep: for k in child_nodes(n): append_child(c, clone_node(k, true)); c
normalize(n)         FP  = merge adjacent text children via set_node_value + remove_child, remove empty text nodes; recurse
```

### 5.3 Character data over `node_value` / `set_node_value`

```
data(t) = node_value(t);  length(t) = len(data(t))
insert_data(t, off, s)   = set_node_value(t, data(t)[0:off] + s + data(t)[off:])
append_data(t, s)        = insert_data(t, length(t), s)
delete_data(t, off, cnt) = set_node_value(t, data(t)[0:off] + data(t)[off+cnt:])
replace_data(t, off, cnt, s) = delete_data + insert_data
substring_data(t, off, cnt)  = data(t)[off:off+cnt]
split_text(t, off)       = let s = data(t)[off:]; set_node_value(t, data(t)[0:off]); let u = create_text_node(owner_document(t), s); after(t, u); u
text_content(n)          = if node_type(n) in {TEXT, COMMENT}: node_value(n) else join(text_content(k) for k in child_nodes(n))
set_text_content(n, s)   = for k in child_nodes(n): remove_child(n, k); if s != "": append_child(n, create_text_node(owner_document(n), s))
```

`set_text_content` is the realm-neutral body ESO81 lacked: it is *nothing but* core mutators, so it works with no JS realm by construction. ESO86 is discharged by this row, not by new mechanism.

### 5.4 Queries over `matches` and traversal

```
query_selector(root, sel)      FP = first n in tree-order descendants of root where matches(n, sel), else null
query_selector_all(root, sel)  FP = [n for n in tree-order descendants of root where matches(n, sel)]     -- snapshot
closest(n, sel)                   = if matches(n, sel): n else if parent_element(n) != null: closest(parent_element(n), sel) else null
get_element_by_id(root, id)    FP = query_selector(root, "#" + css_escape(id))
get_elements_by_tag_name(root, t) FP = query_selector_all(root, t)          -- snapshot (the live variant is realm shape, ES41)
get_elements_by_class_name(root, c) FP = query_selector_all(root, "." + css_escape(c))
has_attribute(n, name)            = get_attribute(n, name) != null
toggle_attribute(n, name, force)  = if (force ?? !has_attribute(n, name)): set_attribute(n, name, "") else remove_attribute(n, name)
```

The FP bodies use the selector engine's `find_first`/`find_all`; the oracle fixture runs both on the layout corpus and asserts identical node identity sequences.

### 5.5 Serialization over `parse_fragment` / `serialize`

```
inner_html(n)            = serialize(n, false);   outer_html(n) = serialize(n, true)
set_inner_html(n, html)  = for k in child_nodes(n): remove_child(n, k); let f = parse_fragment(n, html); for k in child_nodes(f): append_child(n, k)
insert_adjacent_html(n, where, html) = let f = parse_fragment(parent-or-n by `where`, html); insert children of f at the `where` position
```

`set_inner_html` is likewise realm-neutral by construction (the second half of ESO86). Named-element re-registration on Window, which the current body performs, is *realm* work and moves to the adapter as a mutation-ring subscriber (ESO90).

### 5.6 Ranges and selection over the boundary primitives

```
collapsed(r)                  = let b = range_boundaries(r); b.start_container == b.end_container and b.start_offset == b.end_offset
collapse(r, to_start)         = let b = range_boundaries(r); if to_start: range_set_end(r, b.start_container, b.start_offset) else range_set_start(r, b.end_container, b.end_offset)
index_of(n)                   = position of n among child_nodes(parent_node(n))
set_start_before(r, n)        = range_set_start(r, parent_node(n), index_of(n));   set_start_after(r, n) = ... index_of(n) + 1
set_end_before / set_end_after = symmetric
select_node(r, n)             = set_start_before(r, n); set_end_after(r, n)
select_node_contents(r, n)    = range_set_start(r, n, 0); range_set_end(r, n, length_of(n))   -- length_of: len(node_value) for text, len(child_nodes) otherwise
common_ancestor(r)            = the nearest node containing both boundary containers (ancestor walk)
compare_point / is_point_in_range / intersects_node / compare_boundary_points = boundary-point ordering (DOM §5.2 "position") over traversal
to_string(r)                  = concatenation of the text data within the boundaries
clone_range(r)                = let c = create_range(owner_document(...)); set both boundaries from range_boundaries(r); c
clone_contents(r)   FP        = DOM §5.5 "clone the contents" — a finite composition of clone_node / create_node / set_node_value over the boundaries
extract_contents(r) FP, delete_contents(r) FP = DOM §5.5 — compositions of remove_child / set_node_value plus a final collapse; live-range fix-up is
                                                already performed *inside* insert_before / remove_child / set_node_value, which is what makes these derivable
insert_node(r, n) FP          = split the start text node if needed (split_text), then insert_before at the start boundary
surround_contents(r, parent)  = extract_contents → append into parent → insert_node(parent) → select_node(parent)

selection: anchor/focus reads    = selection_boundaries(s)
range_count(s)                   = 0 or 1 (single-range selection); get_range_at(s, 0) = a range from selection_boundaries
collapse(s, n, off)              = set_base_and_extent(s, n, off, n, off);  collapse_to_start / _to_end = from selection_boundaries
extend(s, n, off)                = set_base_and_extent(s, anchor, anchor_off, n, off)
add_range / remove_range / remove_all_ranges / empty = set_base_and_extent from the range's boundaries, or clear
select_all_children(s, n)        = set_base_and_extent(s, n, 0, n, len(child_nodes(n)))
contains_node(s, n, partial)     = boundary-position test over traversal
delete_from_document(s)          = delete_contents(get_range_at(s, 0))
```

Everything here keeps its existing native body as a fast path *only where measured* (the WPT range suites are the oracle); the rest become thin Lambda or C wrappers. 55 host slots collapse to 7 core ops.

### 5.7 Forms and controls — spec algorithms over state, attributes and traversal

```
form_of(n)          = if has_attribute(n, "form"): get_element_by_id(root_node(n), get_attribute(n, "form")) else closest(n, "form")
radio_group(n)      = let scope = form_of(n) ?? root_node(n); [c for c in query_selector_all(scope, "input[type=radio]") where get_attribute(c,"name") == get_attribute(n,"name") and form_of(c) == form_of(n)]
details_group(n)    = [d for d in query_selector_all(root_node(n), "details[name]") where get_attribute(d,"name") == get_attribute(n,"name")]
focus_candidates(root) = [n for n in tree-order descendants of root where is_focusable(n)]
form_controls(f)    = [c for c in query_selector_all(root_node(f), "input,select,textarea,button,fieldset,object,output") where form_of(c) == f]
reset(f)            = for c in form_controls(f): set_state(c, "value", default_of(c)); set_state(c, "checked", has_attribute(c,"checked")); …; dispatch(f, create_event("reset"))
check_validity(f) FP = all(validity_of(c).valid for c in form_controls(f))   -- validity_of is DOM_Pkg §4.2's "constraint validation logic → L (Phase 3)"; until it moves, the native body is the fast path
report_validity(f)  = check_validity(f) with `invalid` events dispatched on the first failing control
request_submit(f, submitter) = if check_validity(f): dispatch(f, create_event("submit", {submitter})) and not cancelled → request_navigation(form_request(f, submitter))
form_entries(f, submitter) FP = the entry-list construction (DOM_Pkg §4.2: "L, Phase 3") over form_controls + get_state("value") + get_attribute("name")
checked(n) / value(n) / selected(n) / disabled_state(n) = get_state(n, …);  set_* = set_state
option_count(sel)   = len(query_selector_all(sel, "option"));  selected_index(sel) = index of first option with get_state(o,"selected")
dropdown_open / set_dropdown_open, hover_index / set_hover_index             = get_state / set_state on the select (state names, not new ops)
click(n)            = dispatch(n, create_event("click", {bubbles, cancelable, composed}))
```

### 5.8 Contenteditable conveniences over the editing core (§4.9)

```
dom_edit_node / _start / _end / _text = the four fields of edit_range(host)
dom_replace_dom_range(host, text)     = let r = edit_range(host); edit_replace_range(host, r.start, r.end, text)
dom_delete_dom_range(host)            = dom_replace_dom_range(host, "")
dom_insert_html(host, html)           = let f = parse_fragment(host, html); insert its children at the caret via edit_insert_at_boundary / insert_before
dom_wrap_range(host, s, e, tag)       = split_text at s and e, create_element(tag), move the covered nodes under it (insert_before), insert it at s
dom_unwrap_range(host, s, e, tag)     = for the matching wrapper ancestors: move children out (insert_before), remove_child the wrapper
dom_range_format(host, tag)           = wrap or unwrap depending on whether every covered node already has a `tag` ancestor
dom_insert_paragraph(host)            = edit_split_block(host, caret);  dom_insert_line_break(host) = edit_insert_break(host, caret)
```

### 5.9 Property reflections (the 148-name protocol) — each a one-line derivation

Attribute reflections (`id`, `class_name`, `href`, `src`, `title`, `type`, `name`, `placeholder`, …): `get_attribute`/`set_attribute` with the IDL's casing and default rules. State reflections (`checked`, `value`, `selected`, `selected_index`, `open`, `disabled`): `get_state`/`set_state`. Geometry reflections (`offset_*`, `client_*`, `scroll_*`): `bounding_box`/`scroll_state` fields. Traversal reflections (§5.1). Serialization reflections (§5.5). The property protocol's *dispatch* (D7.4.4) does not change; each case in `dom_get_property_impl` becomes a call to the named derivation rather than its own body.

---

## 6. Realization — how the three surfaces become one

### ES39 (proposed) — one operation set, two tiers, declared once

The unified DOM API is the set of §4 core and §5 secondary operations, each with **one canonical snake_case name and one signature**. It is declared once, in `lambda/dom/dom_api.def`, as rows `DOM_CORE(name, cluster, signature, body, flags)` and `DOM_DERIVED(name, cluster, signature, body, flags, derivation)`, where `derivation` is the normative pseudo-Lambda from §5 as a string (it is documentation *and* the input to the oracle tests). The file expands in four places — table layout, table fill, `dom.*` registration, `radiant.*` aliases — so no operation is declared anywhere else. (The mechanism survives from revision 1; the *content* of the rows is now decided by §3, not by frequency.)

### ES40 (proposed) — core is the ABI; secondary is provided as wrappers

`JubeHostDomAPI` v2 has three sections: **context** (§4.12, host-internal), **core** (§4, the true ABI — the only slots a host *must* implement natively), and **derived** (§5, wrappers whose bodies call only core or other derived operations — provided for convenience, per direction). A guest module can use either; a minimal host may leave the derived section to a shared library. The per-method slots, per-property slots and ordinal executors of v1 are gone as *API structure*; the bridge may keep ordinals as an internal dispatch encoding for the JS long tail (ESO88), but every ordinal case calls a catalog body. The 21 dead and 24 `NULL` slots are deleted; `JUBE_HOST_API_VERSION` → 2 with the append-only/reserved-slot discipline of the existing idiom.

### ES41 (proposed) — the DOM table is DOM; JS shape moves to `host->realm`

Unchanged from revision 1 and orthogonal to the tiering: the ~28 JS-shape slots (document proxy, prototype values, expandos, live collections, wrapper init, JS exceptions) move to a `JubeHostRealmAPI` section; the property-protocol *dispatch* pairs stay in `->dom` (D7.4.4).

### ES42 (proposed) — derived bodies may call only the core

A derived operation's body — whether C in `lambda/dom/`, Lambda in a `dom` package module, or a native fast path — **calls only core operations or other derived operations**. It never touches node links, buffers, the state store or the ring directly. Enforced two ways: the derived-tier translation unit(s) include only the core header (a lint, ESO87), and every FP body is oracle-tested against its derivation (ES43). This is what "implemented by wrapping/calling the core APIs" means as a checkable property rather than a convention. Module code (`lambda/module/radiant/`) reaches both tiers through the table only — the 8 externs and the 60 direct entries go (ES34's discipline, finally total).

**Language of the derived tier (user question 2026-09-02: C++ or Lambda?) — the rule:**

1. **Every derived operation has a Lambda reference implementation.** The §5 derivation is written as real Lambda in `lambda/dom/derived/*.ls` (one module per §5 cluster). It is the normative spec, the ES43 oracle, and — because it is Lambda over core only — realm-neutral and lint-clean by construction. This is also ES30's `tree.ls` (designed, never written: `lambda/package/dom/` has no `tree.ls` and `form_of`/`radio_group`/`details_group` are still native) generalized to the whole tier, and DOM_Pkg's ST1 stress test made concrete.
2. **The shipped body is chosen per operation by DOM_Pkg §3.3's frequency test, and only that.** *Hot* — reachable per node-visit or per frame from page JS — gets a C++ fast path in `lambda/dom/dom_derived.cpp`, oracle-tested against its Lambda reference. *Cold* — per discrete event or per API call — ships **as** the Lambda reference; there is no second body.
3. **Hot is a short, closed list**, not a judgment per row: element traversal (§5.1), structural sugar and `clone_node` (§5.2), character-data sugar and `text_content` (§5.3), the queries (§5.4), `inner_html`/`outer_html` (§5.5), range boundary sugar and the contents operations editors drive (§5.6), and the 148 property reflections (§5.9) — every `el.id` read is one. Roughly 45 operations plus the reflections. **Everything in §5.7 and §5.8 is cold** — forms, validation, entry lists, reset/submit, focus/radio/details walkers, the contenteditable conveniences — and ships in Lambda. Their main caller is the behavior package, which is Lambda, so for it this is Lambda calling Lambda with no realm crossing at all.
4. **How JS calls a Lambda body — the mechanism exists (verified 2026-09-02).** Both realms run on one runtime (ES12) and one value model, and JS's *sole internal Call kernel*, `js_call_value` (`js_runtime.cpp:14265`), already dispatches a Lambda export by its `entry_abi` — `FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION`/`_PROCEDURE`, `_HOST_ADAPTER`, `_LAMBDA_INTERPRETED` — through `fn_call_into`, so any Lambda function Item JS can reach is `[[Call]]`-able. The cross-language import is likewise real: `import { worker } from "./x.ls"` resolves through `js_mir_module_batch_lowering.cpp:4091`, which `load_script_mir_direct`s the `.ls` **onto the importing runtime**, builds a namespace from its `pub` declarations and registers it in the unified module registry; `test/js/concurrency_lambda_promise.js` proves it end to end, including `.length` and a `pn` returning a Promise. What is *not* built is small and local: (a) a declared-interface member record binding to a Lambda export — one generic `JubeMemberBind.call` stub that prepends the receiver as argument 0 (Lambda has no `this`; `form.reset()` becomes `reset(form)`) and calls the rooted export through `js_call_function`; and (b) loading `lambda/dom/derived/*.ls` onto the document runtime before first use, lazily, exactly as `radiant_dom_package_ensure` already does for the behavior package. Tracked as **ESO91**, now sized as a stub plus a lazy load, not a subsystem. Until it lands, a cold operation JS must reach keeps its C++ body as an oracle-tested fast path; the package never waits, since Lambda→Lambda needs neither.

So the end state is *Lambda except where measured hot*, the C++ derived tier is bounded at about 45 bodies plus reflections, and the count of operations with two bodies goes to zero once ESO91 lands — the derivation and the fast path are the only two things that ever exist for one operation, and the oracle keeps them equal. The tier says *what the operation is*; this rule says *who implements it*; DOM_Pkg §3.3 remains the arbiter of "hot".

### ES43 (proposed) — the derivation is the specification and the oracle

For every `DOM_DERIVED` row with a native fast path, a fixture `test/lambda/dom_derive_<cluster>.ls` evaluates the derivation in Lambda over the core and asserts equality with the fast path on the layout/WPT corpora — node identity sequences for queries, serialized markup for structural ops, boundary tuples for ranges. A fast path that diverges from its derivation is a bug in the fast path by definition. This is how ESO83's class of bug becomes impossible to reintroduce: there is no second traversal to disagree with, only a derivation and a body that must match it.

### ES44 (proposed) — `dom.*` publishes the API; `radiant.*` keeps the engine

`dom.*` publishes every core and derived operation whose `flags` include `NEUTRAL` (works with no JS realm — true of all core except the JS-listener pair, and of every derived op by construction). Its F26 rows are replaced by the expansion, typed `dom_node`. `radiant.*` keeps the engine set (`layout`, `render_svg`, `box`, `register_layout`, `velmt_*`, `poc_attr`, `free`) and, for one release, aliases for the 70 names the package uses; F32 migrates the package to `dom.*` and deletes the aliases (ESO84 decides whether to keep them longer). The JS declared-interface records bind to the same bodies; their `js_name` spellings are metadata, as D7.4.4 already says.

---
### ES45 (ruled by the user, 2026-09-03) — JS has no backdoor to the DOM

**Every DOM access from JavaScript goes through the DOM API.** No direct call
from `lambda/js/` into a DOM body, and no path from the JS runtime into the
Radiant engine that does not cross the API. The DOM API is the whole surface,
for both realms, not the Lambda-facing half of a pair.

This supersedes the earlier reading of ES33/ES34, under which the seam applied
only to *module* code (`lambda/module/radiant/`) and the JS adapter counted as
host code free to call the core directly. That reading is what let the second
paths below accumulate.

**What "DOM access" means here**: reading or mutating the document, its nodes,
or their state. Binding a document or a UI context to a runtime, tearing a
context down, and resetting a batch are runtime lifecycle, not DOM access, and
are exempt — named explicitly so the exemption is a decision rather than an
oversight.

**The measured gap (2026-09-03).** Three separate channels, none of which is the
catalog:

| Channel | How it reaches the DOM | Status |
|---|---|---|
| Property and operation access | Jube declared interfaces → module member ordinals → the core's property protocol and ordinal executor | 7 ordinals delegate to the catalog (F31); the rest call core entries directly |
| Event construction | `js_globals.cpp` calls `js_create_event_init` / `js_create_custom_event_init` straight into `lambda/dom/dom_events.cpp` | direct, no API |
| Runtime and realm plumbing | 49 distinct `dom_*` symbols over 77 call sites from `lambda/js/*.cpp` | **zero** are catalog bodies |

Of those 49, most are lifecycle (`dom_set_document`, `dom_set_ui_context`, the
`*_destroy_context` family, `dom_batch_reset`) or the realm seams the adapter
itself implements, and are exempt by the definition above. The genuine DOM
operations among them — `dom_css_supports_operation`, `dom_css_escape_operation`,
`dom_dataset_set_object_property`, `dom_event_handler_property_set` — are not,
and have no catalog row today.

**What this makes of `create_event`.** It stops being "JS-only by nature". Under
ES45 the question is not whether Lambda needs it but whether *JS* may construct
an event without crossing the API — and it may not. The row has to exist, and
the honest shape is realm-conditional, as `ownerDocument` and the geometry
results already are: a class-stamped JS Event inside a realm, a plain map
outside. That does not make the two representations one; it makes the *entry
point* one, which is what ES45 requires. ESO102 still owns the identity split.

**Consequence for the stages.** This is larger than F33's conformance pass: it
adds a stage of its own, since the property/ordinal path is the main JS→DOM
channel and routing it through the catalog is the bulk of the work. F33's
guest-module test proves a *guest* needs no static linkage; ES45 asks the same
of the JS runtime, which is a much bigger consumer.


## 7. Layering: JS → Lambda → native — calls, the stack, and the heap

This section records three things the user asked to be explicit (2026-09-02): that the derived tier is **hybrid**, how **JS calls a Lambda body**, and whether the **runtime heap** survives a call stack with JS, Lambda and native frames interleaved. Everything in §7.2–§7.4 is verified against the tree, not inferred.

### 7.1 The derived tier is hybrid — by rule, not by row

Per ES42: every derived operation has a **Lambda reference implementation** (the §5 derivation, as real Lambda in `lambda/dom/derived/*.ls`), and its *shipped* body is chosen only by DOM_Pkg §3.3's frequency test.

| Ships as | Clusters | Why |
|---|---|---|
| **Native fast path** (C++ in `lambda/dom/dom_derived.cpp`, oracle-tested against its Lambda reference) | §5.1 element traversal · §5.2 structural sugar, `clone_node` · §5.3 character data, `text_content` · §5.4 queries · §5.5 `inner_html`/`outer_html` · §5.6 range boundary sugar and the contents operations editors drive · §5.9 the 148 property reflections | reachable per node-visit or per frame from page JS; the frequency test says N |
| **Lambda** (the reference *is* the body; no second one) | §5.7 forms, validation, entry lists, `reset`/`request_submit`, the focus/radio/details walkers · §5.8 the contenteditable conveniences | per discrete event or per API call; their main caller is the behavior package, which is Lambda |

So the layering is, for a Lambda-shipped operation reached from a page: **JS → Lambda → native DOM core**; for a native-shipped one, **JS → native**; and for the package, **Lambda → Lambda → native**. The oracle (ES43) is what lets the two shapes coexist: a fast path is only ever a faster spelling of a derivation that exists in Lambda.

### 7.2 How JS calls a Lambda body — the mechanism (exists today)

1. **One runtime, one value model.** A document owns one script runtime and JS and Lambda share it — `dom_document_script_runtime(doc)`, `dom_element.hpp:355–361` (ES12). A Lambda export is a `Function` Item on the same heap the page's objects live on; nothing is marshalled.
2. **JS's one Call kernel already dispatches Lambda.** `js_call_value` (`js_runtime.cpp:~14265`, "sole internal Call kernel") tests the callee's `entry_abi` and routes `FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION` / `_BOXED_PROCEDURE` / `_HOST_ADAPTER` / `_LAMBDA_INTERPRETED` through `fn_call_into` → `lambda_dynamic_call`, which checks the Lambda signature (arity, optional/rest adaptation, `LAMBDA_MAX_FUNCTION_ARGS`), then invokes the native entry, the host adapter, or `interp_call` for a cold interpreted function. Any Lambda function Item JS can *reach* is therefore `[[Call]]`-able.
3. **The cross-language import is real and lands on the same runtime.** `import { f } from "./m.ls"` resolves through `js_mir_module_batch_lowering.cpp:4091`, which `load_script_mir_direct`s the `.ls` onto the *importing* runtime, builds a namespace from its `pub` declarations, and registers it in both the JS module system and the unified module registry. `test/js/concurrency_lambda_promise.js` exercises it end to end: `pn` exports called from JS, `.length` correct, a `pn` returning a Promise, `.catch` on failure.
4. **What ESO91 still has to add is a binding, not a bridge**: a generic `JubeMemberBind.call` stub that holds a rooted Lambda export, **prepends the receiver as argument 0** (Lambda has no `this`; `form.reset()` becomes `reset(form)`), and calls `js_call_function`; plus a lazy load of `lambda/dom/derived/*.ls` onto the document runtime on first touch, mirroring `radiant_dom_package_ensure`. Error mapping (a Lambda error Item → `DOMException` with the right `.name`) is DOM_Pkg Q2 and is the one part needing design rather than a stub.

### 7.3 The stack — nested watermarks, one checkpoint

Stacking is not a problem, and the reason is structural rather than lucky: both front ends emit frames through **one emitter**. `MirEmitter` is "bundled emit state shared by both transpilers (Lambda + JS)" (`transpile-mir.hpp:249`). A Lambda function's prologue reserves a side-root frame (`finalize_side_root_frame`, `transpile-mir.cpp:1739`) against `Context.side_root_top/limit/commit_limit`; a JS function's prologue reserves one the same way (`jm_finalize_side_root_prologue`, `js_mir_hashmap_scope_utils.cpp:544`, same `Context` offsets), and "nested callee epilogues restore `side_root_top` to their caller watermark" (`:535`). A JS → Lambda → native → JS-listener stack is therefore one contiguous side-root region with nested watermarks, whichever language owns each frame. Non-local exits keep it consistent: `LambdaRecoveryCheckpoint` captures every runtime-owned side-stack watermark in one snapshot "so new dynamic regions cannot be restored by only some setjmp/longjmp boundaries" (`side_stack.h:22–25`). Overflow fails closed (`lambda_root_frame_overflow_error`), not silently.

### 7.4 The heap — is the GC able to handle mixed frames? Yes, and here is why it cannot tell them apart

Conservative native-stack scanning is retired (CLAUDE.md rule 15), so the only question that matters is whether every live Item on a mixed stack is **precisely** rooted somewhere the collector looks. It is, because there is exactly one place it looks and everyone roots into it:

- **The collector is handed one dynamic root region and is language-blind.** `heap_gc_collect` (`lambda-mem.cpp:451`) computes `[context->side_root_base, context->side_root_top)` and calls `gc_collect_with_root_region` with it plus the registered-root set. It has no notion of which frame pushed which slot.
- **JIT'd Lambda frames root there** (§7.3). **JIT'd JS frames root there** (§7.3, same emitter, same offsets). **Native frames root there too**: `RootFrame`/`RootSpan` (`lambda-root-frame.hpp`) reserve exact slots "above the current side-root watermark; generated frames may nest above it" (`side_stack.h:33–35`) — which is how the native DOM core's own allocations (wrappers, arrays, strings) stay live while a Lambda or JS frame is below them.
- **Values crossing a call are owned by a rooted frame on each side.** Arguments cross `lambda_dynamic_call` as `const Item*` into the caller's frame and are stored into the callee's side-root slots by its prologue before any allocation can occur; the return value goes into a **caller-owned result home** (`result_home`, the Return-convention v3 contract) rather than a bare register. Class construction, the one case where an allocation has no owner yet, is explicitly kept rooted by the JS kernel (`js_runtime.cpp:~3365`).
- **DOM wrappers are the one non-heap dependency, and they are handled by ownership, not by scanning.** A `dom_node` VMap's `host_data` points at a Radiant-owned node (D4.5.1v3 — node storage is not GC memory); the wrapper Item itself is heap-allocated and rooted like any other, and the identity cache registers each cached wrapper as a GC root (`JS_13` §2). A Lambda frame holding `~` and a JS frame holding `el` hold the *same* rooted wrapper.

**Evidence, not argument.** `test_mir_gc_stress_gtest` runs its corpus under `LAMBDA_GC_FORCE_EVERY=1` + `LAMBDA_GC_POISON_FREED=1` (collect at *every* allocation, poison freed memory) and under a randomized `LAMBDA_GC_FORCE_ONE_IN=3` mode, with a byte-identical-output oracle against the unstressed run. The knobs live in the shared collector (`lambda-mem.cpp:333–378`), so JS and Lambda allocations are stressed identically — and the corpus already contains **`concurrency_lambda_promise.js`** (JS → Lambda `pn` calls, promises resolving across the boundary) and **`regression_side_stack_frame_gc.js`** (side-root frames across explicit `gc()` at recursion depth). Mixed frames are already collected under maximal stress, today, and pass.

**The gap this exposes (ESO92).** The stressed mixed-realm case is a *pure* Lambda module. The three-layer round trip this design introduces — JS → Lambda derived op → native DOM core → `dispatch` → JS listener → Lambda again, with `dom_node` wrappers crossing every boundary — is not in the corpus. It should be, before F31 ships a Lambda body to JS callers: one script that does exactly that round trip under `FORCE_EVERY=1`, added to the stress corpus, is the proof that "language-blind root region" holds for wrappers and not only for numbers and promises.

---

## 8. Migration stages

Gated as in `Lambda_Design_DOM_API.md` §5 (no new failure vs the previous run; DOM UI Integration 108, UI Automation 114, WPT DOM2 20, `dom_api*.ls`). One stage per commit.

| Stage | Contents | Exit criterion |
|---|---|---|
| **F28 — catalog + tiering, no behavior change** ✅ **LANDED 2026-09-02** | `lambda/dom/dom_api.def` (115 rows: 74 CORE, 41 DERIVED, each DERIVED row carrying its derivation string; `same_node` and `equal_node` were added by ESO96); `dom_core.h/.cpp` = the uniform `Item f(Item×argc)` bodies — the F26 `fn_dom_*` bodies *moved* here as fast paths (not copied), plus new core bodies `create_node`, `set_node_value`, `attribute_names`, `scroll_state`, `range_boundaries`, `selection_boundaries`, `computed_style`, and `parse_fragment` in dom.cpp sharing the innerHTML setter's parse loop (`dom_parse_markup_into`, one loop, two callers); `dom_api_check.cpp` = compile-time uniqueness (one enumerator per name), arity (`static_assert` per row against the body's C signature) and tier/derivation consistency; `dom_module.cpp` now *expands the catalog* (rows with a body and `DOM_F_NEUTRAL`) instead of listing functions by hand, so `dom.*` speaks canonical names (`get_attribute`, `query_selector`, `append_child`, …) and gained 12 operations. Engine-provided rows (`DOM_F_ENGINE`, 40) and realm-dependent rows carry `DOM_NO_BODY`/no NEUTRAL flag and wait for F30. | Tests: `dom_api.ls`/`dom_api_read.ls` renamed to canonical names with goldens unchanged byte for byte; new `dom_api_core.ls` drives creation of all four node kinds, `parse_fragment`, `set_node_value`, snapshot child lists and `attribute_names` from a realm-less script. Gates: lambda 4072/4072; radiant 8092 passed / 1 failed — the Layout Page Suite, which **passes when rerun standalone**; it checks against `test/layout/snapshot/page.json`, a local unversioned snapshot, and its per-page numbers swing tens of points between runs on static pages with no script (clojure 100%→22.5%, bootstrap +38%), so it measures capture noise, not DOM behavior; WPT DOM nodes rerun against the new code (see ESO95). |
| **F29 — realm split + dead slots** ✅ **LANDED 2026-09-02** | `JubeHostRealmAPI` added (24 slots): the document proxy (4), prototype values (3), expandos (4), live collections (8), wrapper init, the contenteditable JS throw, and the three slots whose bodies construct JS objects (`get_selection_function_for_document` returns a JS closure, `document_default_view_bridge` the Window, `document_create_event_bridge` a JS Event). `->dom` keeps 173 slots, including the property-protocol dispatch pairs (D7.4.4). 26 unreferenced slots deleted with their extern declarations. `JubeHostAPI` gains `realm`; `JUBE_HOST_API_VERSION` → 2; `radiant_module_init` now requires `->realm`. `get_foreign_doc` and `document_implementation_bridge` stay in `->dom`: they are DOM identity and a DOM interface, not JS shape. | build clean; `dom_api{,_read,_core}.ls` byte-identical; lambda 4072/4072; radiant 8092/1 (the Layout Page Suite snapshot flake) |
| **F30 — core section + first oracles** ◐ **oracles LANDED 2026-09-02** | `test/lambda/dom_derive_traversal.ls` (nine §5.1/§5.2 operations) and `dom_derive_chardata.ls` (§5.3 `text_content`) evaluate each derivation in Lambda over core only and compare against the native fast path on every node of a document; both report `divergent: []` after the three fixes in ESO98. Node identity uses `same_node`, never `==` (ESO96); accumulation counts and indexes the chain, never `(head, tail)` (ESO97). The host table's catalog section landed with it (`JubeHostDomCatalogAPI`, one slot per row, filled from the same file). **Still open**: the split of the fast paths into `lambda/dom/dom_derived.cpp` with the ES42 include-lint (ESO87) — attempted and reverted, blocked on ESO100. | oracles green; 3 defects fixed |
| **F31 — derive the rest; close the seam** ◐ **in progress 2026-09-03** | **Slice 1 (ESO94, the seam) landed**: seven member ordinals in `radiant_dom_bridge.cpp` that read node/element fields directly — `parentNode` (both forms), `parentElement`, `nodeName`, `nodeType`, `firstChild`, `lastChild` — now call the **catalog section** of the host table, its first real use. They were second implementations of core operations, and had drifted three times (ESO83's walkers, ESO93's document parent, and `nodeName` uppercasing `#document` into `#DOCUMENT`). Two are deliberately *not* delegated and say why in place: `childNodes`, because JS requires a live NodeList and Lambda a snapshot (S9.2.2), and `ownerDocument`, because `radiant.*` hands back its own document wrapper carrying `document_element`/`ready_state` — delegating it silently changed what the behaviour package receives, and unifying that is F32's job. **Slice 2 (§5.4/§5.5 oracle) landed**: `test/lambda/dom_derive_query.ls` checks nine selectors and every node against the derivations for `query_selector`, `query_selector_all`, `closest`, `get_element_by_id`, `has_attribute`, `inner_html` and `outer_html`; zero divergence. It forced two derivations to become writable and true — `closest` now states that it is an Element operation, and `get_element_by_id` no longer invokes a `css_escape` that does not exist — and found ESO103. **Slice 3 (`dom.load`, ESO80 closed) landed**: acquiring a document needs the engine's loader, which lives above this link target, so loading is split at a provider seam — the engine parses and answers a `DomDocument*`, the core wraps it as the document node (which since ESO101 answers as a Document too, so a script queries straight off what `load` returns). Splitting it that way keeps the engine half free of module *initialisation*; a second seam, `dom_engine_bind_host`, binds the engine module's host API from `dom_module_init`, because every engine-side wrapper call goes through that pointer and it was only set when `radiant` was imported — so `import dom` loaded a document and then crashed wrapping it. `test/lambda/dom_api.ls`, the POC-1 exit test, now **imports nothing but `dom`** and produces a byte-identical golden. **Slice 4 (the module seam closed) landed**: the nine remaining direct entries — `check_validity`, `form_reset`, `scroll_into_view`, the form-data collector, `focus_first_invalid_form_control`, `navigate_submit_target` and the popover trio — now cross the host API like everything else. Three already had slots; the six that did not were added to the table's additive tail, because form submission and popover activation are host policy rather than module policy. `grep '^extern "C" .*\b(dom|js)_' lambda/module/radiant/` now returns only the two *provider* seams the module supplies (`dom_engine_bind_host`, `dom_engine_load_document_native`), which are the module implementing a core-declared function rather than reaching into one — so **ES34's discipline is total**, which is F31's exit criterion. **Remaining, and blocked rather than pending**: the §5.6 range/selection oracle needs `dom.create_range()` to work without a realm — it answers an error today, because a Range is built through the JS object model exactly as the rects were before ESO81's third fix; the same treatment would unblock it. The §5.7 form walkers (`form_of`, `radio_group`, `details_group`, `focus_candidates`, `form_controls`, `click`) have no bodies at all: they are the tier DOM_Pkg §3.3 says ships **as Lambda**, so they need ESO91's binding — a `JubeMemberBind.call` stub that prepends the receiver and a lazy load of `lambda/dom/derived/*.ls` — before an oracle can compare anything. Both are F32-shaped. | oracles green; `dom_api.ls` still needs the `dom.load` round trip |
| **F32 — `dom.*` / `radiant.*` unification; package migration** ◐ **in progress 2026-09-03** | **The census first, because the migration is not a rename.** `lambda/package/dom/*.ls` uses **72 distinct `radiant.*` operations**, and when F32 opened only **4** of them existed in `dom.*`. Classified against the catalog: 9 are published today (once the spelling differences are allowed for — `attr`→`get_attribute`, `parent`→`parent_element`, `document_root`→`root_node`); **26 are catalog rows flagged `DOM_F_ENGINE` that simply had no body**; 4 are the §5.7 walkers that ship as Lambda (ESO91); and **33 are not in the catalog at all** — the editing, range-format, select and form-encoding policy that may well stay engine-only. Migrating call sites before closing that gap would just move breakage into the package. **Slice 1 landed**: the engine-provider seam, and the first six ENGINE rows filled — `get_state`, `set_state`, `request_change`, `dispatch`, `focused`, `focus_set`, which are the package's heaviest users (`get_state` alone appears 16 times). Each forwards to the same body `radiant.*` publishes, so the two surfaces are one implementation and the package's behaviour cannot change under it. `dom.*` goes 53 → 60 published operations; `test/lambda/dom_engine_rows.ls` drives them from a script importing only `dom`. **Slice 2 landed**: 17 more ENGINE rows wired the same way — `activate_popover`, `caret_operation`, `clear_ime_preedit`, `clipboard_text`, `context_menu_target`, the four editing insertions, `edit_replace_range`, `key_intent`, `navigation_destination`, `open_context_menu`, `request_navigation`, `set_caret`, `set_ime_preedit`, `set_password_reveal`, `tc_value` — generated from the catalog rather than written out, after matching each row's arity against its `radiant.*` counterpart. `dom.*` is now **77 published operations** (from 53 when F32 opened), and the package's coverage goes from 4 of 72 to **32 of 72**. `dom_engine_rows.ls` asserts that each wired row *agrees with its `radiant.*` spelling* — including where the answer is "not applicable", since agreeing on absence is what a second implementation would most easily get wrong. **Not wired, and why**: `tc_set_selection` takes four arguments in the catalog and three in `radiant.*`; `edit_range`, `ime_preedit`, `is_focusable`, `tc_replace_range`, `tc_selection` and `tc_set_value` have no `radiant.*` counterpart at all, so their bodies have to be written rather than forwarded. **Slice 3 landed — the first operations moved *out* of native and into Lambda**, on the ruling that as much as possible belongs under Lambda and as little as possible in the engine. `form_of`, `radio_group` and `details_group` were native `radiant.*` bodies; none is mechanism — each is policy over ordinary DOM reads — so each is now written in Lambda in `lambda/package/dom/tree.ls` (ES30's designed-but-never-written module), and `form.ls` and `details.ls` call those instead. The oracle `test/lambda/dom_derive_forms.ls` holds the Lambda definitions and the native bodies to the same answers over every element of two real documents (38 form elements, 22 details elements, zero divergence) — which is what makes this a refactor rather than a rewrite. It corrected **four** things the catalog's derivations had guessed wrong: `details_group` **excludes** the subject while `radio_group` **includes** it (an asymmetry that is the real contract — the radio caller unchecks the group then checks the subject, the details caller would otherwise close the element it just opened); an *empty* `name` is not a group name; and comparing form owners needs a null-aware test, because `same_node(null, null)` is not true and every form-less radio therefore matched nothing. That last one had silently emptied radio groups. **Slice 4 — the ruling on the 33, decided by what each body touches, not by name.** Every native body was read for engine markers (`DocState`, `form_control()`, a `View*`, the MarkEditor, selection or scroll state, the document's URL), following one level of delegation. **28 of the 33 are engine-resident**: the editing primitives (`dom_edit_*`, `dom_wrap_range`, `dom_range_format`, `dom_insert_html`, the range replacements), the widget state (`dropdown_open`, `hover_index`, `option_count`, `set_selected_index`, `range_min`/`max`/`value`, `caret_surface`, `value_at_focus`), validity (`check_validity`, `custom_validity`), scroll, and the iframe pair. They read state only the engine has, so they stay `radiant.*` — that is the engine set ES44 describes, not a backlog. The important finding is that **the policy had already moved**: `submit.ls` composes the submission body, `commands.ls` and `dom_edit.ls` choose the edit, and each calls native only for a primitive. Asking "how much can move" of these 33 mostly answers "it already did". **What did move: `form_encode`**, now `lambda/package/dom/urlencode.ls`. It is pure computation over a string — no DOM, no engine — so nothing justified it being native. The oracle `dom_derive_urlencode.ls` holds it to the native encoder over every printable ASCII code and a multi-byte corpus, and caught the mistake worth catching: percent-encoding escapes **bytes**, not code points, so `Ü` is `%C3%9C` and not `%DC`. Encoding the code point is how a form body becomes mojibake on the server, and that is what the Lambda version said until it was compared. **Slice 5 — the last engine rows, and the alias pass.** `ime_preedit` and five engine reads the composites needed (`tc_selection_start`/`_end`, `edit_node`/`_start`/`_end`) are wired; and **three rows moved from CORE to DERIVED and into Lambda** — `tc_selection`, `tc_set_value` and `edit_range` are each just a pair or triple of reads the engine already publishes, and a row that can be written over other rows is not part of the ABI. They live in `tree.ls` beside the walkers. **The alias pass migrated 114 call sites across 14 package files**, leaving 49 on `radiant.*` — exactly the engine set. It was gated on an equivalence check first, and that check earned itself: mapping `radiant.document_root` to `dom.root_node` looked obvious and was **wrong** — `document_root` means the document *element* (`<html>`) while `root_node` is the Document, so every one of 38 elements diverged. The fix was a new `document_element` row, and the same class of near-miss as `ownerDocument` in F31: names that look like synonyms are not, and only comparing the two bodies over a real document finds it. **Two more near-misses got past the equivalence check and were caught by the UI fixtures**, which is the argument for gating on them rather than on a script. `radiant.set_attr(n, name, null)` *removes* an attribute while `dom.set_attribute` sets it — the DOM spelling for that intent is `remove_attribute`, and the two `details` call sites now say so. And `document_element` written as `first_element_child(root_node(n))` agrees for a connected node but not a **detached** one, where the parent walk stops at the subtree's top; `radiant.document_root` reads the node's owning document directly, so the row is core rather than derived. Both broke real behaviour — details accordions and iframe navigation — and neither was visible to a check that only walks a connected document. — editing, range formatting, select and form encoding — each of which needs a ruling on whether it becomes a row or stays engine-only, and only then the aliases and the call-site migration. ⚠ `dispatch` is wired to radiant's event-*name* form while the catalog types its second argument as an event object; reconciling those two spellings belongs with the migration. | package fixtures unchanged; `radiant_module.cpp` still has its DOM rows |
| **F33 — conformance + docs** | Guest-module test (`hostobj_demo` or `dom_guest_demo`, loaded dynamically) drives load → query → mutate → serialize through `host->dom` core *and* derived sections; AST-interpreter path exercised (ESO89); `JS_13`, `Native_Module` §8, `DOM_API` §7, `DOM_Pkg` §4.3 updated; CLAUDE.md/AGENTS.md row | guest test passes with no static linkage to any DOM body; docs stamped |
| **F34 — close the JS backdoor (ES45)** ◐ **in progress 2026-09-03** | **The four direct DOM operations are done.** `CSS.supports` and `CSS.escape` had no catalog equivalent and got core rows; `element.dataset.foo = v` is a `data-*` write, so it got a `set_data` row over the existing setter, with the *proxy unwrapping* left on the JS side where the proxy lives; `el.onclick = fn` got a `set_event_handler` row. All four JS call sites — two in `js_runtime.cpp`, one in `js_globals.cpp`, plus a second dataset path found only by grepping after the first was fixed — now go through `jube_internal_host_api()->dom_catalog->…`, which is the API rather than the body. None of the four symbols is named in `lambda/js/` any more. Two notes for the rest of the stage. The catalog is fixed-arity and `CSS.supports` is overloaded (`supports(property, value)` and `supports(conditionText)`), so the row takes two arguments and the one-argument spelling passes null — the overload is a binding concern the adapter resolves before crossing, which keeps every row the same shape. And `set_event_handler` is **derived in substance**: it drives the same listener store as `add_listener`/`remove_listener`, finding and tombstoning the previous IDL handler. Stating it as a derivation needs a read for the current IDL handler that no row exposes; the row wraps the existing body until one does. **The property/ordinal path, measured.** The module reaches the core through the host API already, but through `->dom`, the **v1 per-method section** ES40 says should disappear as API structure — 114 distinct slots over 133 call sites — rather than through `->dom_catalog`, which is the API. So the migration is slot by slot, and the census is much less encouraging than a name match suggests: of the 19 slots whose name maps to a row, **checking signatures disqualified most of them**. `clone_node_bridge` carries an extra `has_deep` flag the row has no place for; `text_replace_data_bridge(text, offset, count, data)` is a splice, not `set_node_value(n, data)`; `computed_style_get_property` takes a *style object* where the row takes a *node*. Nine more take an unwrapped `void*` where the row takes an `Item`, so routing them means a wrap/unwrap round trip that is possible but not free. **Three slots routed, and the other three taught the stage its real lesson.** What survives is `create_range`, `stylesheet_insert_rule` and `stylesheet_delete_rule` — where the slot and the row are literally the same function, so routing is a spelling change. The three that were reverted each failed for a *different* reason, and none was visible in a signature: `check_validity` and `reset_form` name the same idea on both sides but not the same operation — the catalog rows carry radiant's **form-only** bodies, while the bridges accept any form control, so JS's `input.checkValidity()` began answering false. And `dispatch` cannot be routed at all as things stand: JS's `dispatchEvent` passes a class-stamped **JS Event**, the row's second argument is a **Lambda event map**, and *both are keyed maps carrying a `type`* — so the row cannot tell them apart by shape, and picking the wrong path silently dropped every event (the handler, lifecycle and jQuery suites failed together). That is ESO102's identity split appearing where it actually costs something: until the two event representations are one, or distinguishable, `dispatchEvent` keeps its own entry. The row does now fall back to the JS bridge when its argument is neither a name nor a readable event map, which is right for a Lambda caller holding a JS Event and harmless otherwise. For the record, the earlier phrasing of this row said six slots were 'verified equivalent'. They were verified only by *signature*, which this stage has now shown three times to be the weaker test. | **The three dispatch entry points are rows now.** The previous slice guessed that the property protocol and the ordinal executor were dispatch *mechanism* belonging in `->dom`; that guess is overruled. They are the three doors every unrouted slot goes through — `dom_get_property_impl`, `dom_set_property_impl`, `dom_element_operation_impl`, 2,692 lines of core behind them, and above them 41 ordinal intercepts, 11 member ordinals and 125 interface bindings in the module — so if the API does not name them, ES45 is satisfied only in the slots that happen to have rows. `get_property`, `set_property` and `invoke` are catalog rows, verified standalone against their named equivalents (`invoke(el, 20, ["p"])` agrees with `dom.matches`). `invoke` takes its arguments as an **array**, because a catalog row is fixed-arity and an executor is variadic; that is the one place the ABI shape and the operation's shape genuinely differ.\n\n**They are not published to `dom.*`, and the reason is the absence rule.** A Lambda script reaches everything they reach through named rows, so publishing them buys nothing — and it would cost something real: `dom_prop_get` and `dom_op0..3` normalise `undefined` to null at the Lambda face (ESO98, ESO103), and these three are the path **JS** dispatches on, where `undefined` is load-bearing. Normalising them was the first attempt, and DOM UI fell from 108 to 39. So the three rows are the un-normalised bodies, binding-only, with no `DOM_F_NEUTRAL`; the Lambda face keeps its rule on the named rows where it belongs.\n\n**Routing the module through them was attempted and reverted, for a second and independent reason.** With the absence bug fixed the fixtures came back except `form_submission`, and the adapter is why: turning `(op, args, argc)` into the row's array allocates **per call**, on the hottest path in the DOM — every property access — while the caller's `args` sit as raw `Item`s on the C stack, unrooted, so a collection during that allocation can free them. That is the standing JIT/GC rooting hazard, not a contract mismatch. The `#define`s stay pointed at `->dom` with the reason written at them; crossing needs a rooted adapter, which is ESO106 rather than a line in this stage. | **Slice 3 (ESO106) landed**: all three dispatch entry points now cross the catalog — the two property rows onto their own slots, the ordinal executor through `invoke_raw`, the catalog's native-shape companion door. The reverted routing was blamed on rooting; measuring it found `array_new`'s length/capacity contract instead (ESO106). **Slice 4 (the rowless slots, censused) landed**: "the ~95 rowless slots" is **107**, and counting them as one backlog was the mistake. They are three classes with different fates. **A (28) — Item-shaped operations on nodes and CSSOM objects**: candidates for rows, and the class ESO107 was blocking. **B (41) — the same operations with the receiver already unwrapped**: the module takes `DomNode* node = radiant_dom_unwrap_node(elem_item)` once at the top of its dispatch and then uses that pointer for field reads, direct core calls *and* these bridges, so crossing is not a rename but a question per slot of whether the module still needs the pointer after the unwrap moves into the core. **C (37) — not catalog-shaped at all**: the eight `after_*` notifiers and `notify_mutation*` (`void` returns, C strings, raw pointers), the five `is_*` predicates over JS-shaped objects, the `void*`/`const char*` accessors (`get_document`, `unwrap_element_impl`, `to_attribute_cstr`, `get_ui_context`), and the document/popover/select plumbing. A row is a Lambda function; none of these can be one, and forcing them would be a worse answer than leaving them. So the honest target is not "the `->dom` section disappears" but "A and B cross, C is named for what it is". **Also corrected**: F31's exit criterion was a grep for `extern "C"` declarations in the module, and it cannot see symbols declared by an included header. The module includes `input/css/dom_node.hpp`, `dom_element.hpp`, `dom_lifecycle.hpp` and `selector_matcher.hpp` and calls six core functions straight through them (`dom_node_clone`, `dom_element_create`, `dom_node_pin`, `dom_node_ref`, `dom_retire_sweep`, `dom_comment_create_detached`). ES34's discipline is total *for the functions the module declares*, which is a weaker claim than the one recorded. | **Remaining**: class B's 41 unwrap questions, class A's slots behind their own contract checks, and event construction. The lesson from this slice is that the count of migratable slots is set by signature agreement, not by naming, and each disagreement is a contract question rather than a rename. | six slots crossed + three dispatch rows published; `grep` over `lambda/js/` clean for the four operations |

---

## 9. Open issues

| ID | Issue |
|---|---|
| **ESO84 — OVERRULED by the user 2026-09-03: retire `radiant.*` entirely; the DOM API is the sole API.** My earlier ruling was to keep the aliases, on three consumers. The user's decision supersedes it, and the work follows: the 37 DOM operations that had no `dom.*` row got one (editing, form validation and submission, select and range widget state, embedded documents — engine-resident bodies reached through the seam, so the *entry point* moves even where the body cannot), and the behaviour package is now **entirely free of `radiant.*`** — the only occurrence left in `lambda/package/dom/` is a comment. `dom.*` publishes **118** operations, from 53 when F32 opened. What remains for `radiant.*` is the 21-function engine set: `layout`, `render_svg`, `box`, `register_layout`, `poc_attr`, `free` and the `velmt_*` view accessors, none of which is DOM. |
| **ESO84 (cont.) — the names are gone.** 77 DOM entries deleted from `radiant_module.cpp`'s function table; **21 remain**, and they are the engine set exactly: `layout`, `render_svg`, `box`, `register_layout`, `poc_attr`, `free` and the fifteen `velmt_*` view accessors. The `fn_radiant_*` *bodies* stay — they are what the catalog's engine seams call, so the implementation is unchanged and only the published spelling is retired. The three consumers that had argued for keeping the aliases were dealt with rather than worked around: the engine module's own tests migrated to `dom.*`, and the three oracles became goldens (their cross-check had already done its work — it is what caught the four form-walker contract errors and the bytes-versus-code-points bug — and there is nothing left to compare once one side is deleted). Migrating the tests surfaced one more contract difference: **`radiant.set_attr` returned the node so callers could chain, while `dom.set_attribute` answers nothing, as the DOM specifies** — two tests chained through it and read null. |
| **ESO105 — an import can be load-bearing for its side effect.** Removing `import radiant` from every package file — correct on the face of it, since nothing called a `radiant.*` function any more — killed the event cascade: the author's own `click` handler stopped running, and `dom_pkg_prevent_default` failed while four sibling fixtures passed. The module's *init* is what binds the host API the engine-side wrappers run through, and the template and event machinery the package's views depend on comes with loading it. The package entry now imports `radiant` once, with the reason written at the import, and calls nothing from it. Two hypotheses were wrong before this one — a shadowed module name and the loader's import alias — and both looked convincing; what settled it was bisecting file by file against HEAD rather than reasoning from the log, whose `missing identifier` lines are ordinary scope-walk noise (`len` appears among them). |
| ESO85 | **Engine-side core if the radiant module goes dynamic.** Core bodies in `radiant/*.cpp` (state store, editing waist, focus, navigation) are host code and the registry can point slots at them; if the *module* becomes a DSO the slots still resolve in-process. F33 should prove it rather than assume it. |
| ESO86 | ~~Realm-neutral `set_text`/`set_inner_html` bodies.~~ **Discharged by design**: §5.3 and §5.5 define them as compositions of core mutators, which are realm-free by construction. What remains is moving the named-element re-registration side effect out of the current `innerHTML` body (ESO90). |
| ESO87 | **The derived-tier lint.** `dom_derived.cpp` (and any Lambda derived module) may include/import only the core surface. Implement as a `make lint` rule beside `no-int-cast-radiant`; failing it is the concrete meaning of "calls only the core". |
| ESO88 | **Ordinals as internal encoding.** The bridge may keep `JubeDomElementOperation` etc. as its dispatch encoding for the JS long tail, provided every case calls a catalog body. Whether the JS declared-interface records should instead bind directly to catalog bodies (making ordinals redundant) is a Jube/DOM4 question; nothing here depends on the answer. |
| ESO89 | **The interpreter tier's entry.** ES34 named the AST interpreter as a table consumer; no such path exists today. F33 must build one or record that this rationale is still aspirational. |
| ESO91 | **Binding a JS member or a `dom.*` entry to a Lambda export.** The call path exists (ES42 point 4: `js_call_value` dispatches Lambda ABIs; `.ls` imports load onto the shared runtime). Three pieces remain, all small: **(a)** a generic `JubeMemberBind.call` stub carrying a rooted Lambda export, prepending the receiver as arg 0 and calling `js_call_function` — receiver binding is the one semantic gap, since Lambda has no `this`; **(b)** lazy load of `lambda/dom/derived/*.ls` onto the document runtime on first Lambda-bound member touch (mirror `radiant_dom_package_ensure`); **(c)** the Jube `dom` module publishing those same exports under `dom.*` — either a companion-`.ls` capability on `JubeModuleDef` or a `dom_derived.ls` re-export that `import dom` merges (a Jube question, ESO88's neighbour). Error mapping (a Lambda error Item → `DOMException`) is DOM_Pkg Q2 and is the only part that needs a design rather than a stub. Until (a)+(b) land, cold operations JS must reach keep oracle-tested C++ fast paths; F31 must not block on this. |
| ESO92 | **Mixed-realm DOM round trip in the forced-GC corpus.** §7.4 shows the collector is language-blind and that JS→Lambda calls already pass under `LAMBDA_GC_FORCE_EVERY=1`, but only for a pure Lambda module. Add `test/js/dom_mixed_realm_gc.js`: JS calls a Lambda-shipped derived op (e.g. `form_of`/`radio_group` over `dom_node` wrappers), which calls native core, which `dispatch`es to a JS listener that mutates the tree and calls back into Lambda — under `FORCE_EVERY=1` + `POISON_FREED=1`, with the byte-identical oracle. Land it in F30 (the first stage that ships a Lambda body), before F31 exposes one to JS. |
| ESO97 | **A one-element comma sequence is that element, so a derivation cannot accumulate with `(head, tail)`.** `(node, null)` *is* the node, and `for (c in node)` then iterates the node map's values rather than yielding the node — which made a 15-node document walk to 629 entries of `HTML`, `#DOCUMENT` and nulls before blowing the 10 000-frame recursion budget. With two or more elements the same construct is correct, so the failure appears only on single-child nodes. Combined with `var`/`while`/assignment being procedure-only (E224), this fixes the shape of every Lambda derivation: recursive, never looping, and accumulating by counting the chain and indexing it (`[for (i in 0 to chain_len(c) - 1) nth_from(c, i)]`), which always yields a list. The §5 derivation strings are written that way and the two oracles use it. Whether `for` over a bare map should iterate its values at all is a language question, not a DOM one. |
| ESO98 | **The ES43 oracles found three real defects on their first run — the discipline works.** (a) `children(text_node)` answered `[undefined]`: F28's snapshot walk stopped on null, and the protocol's absent-property answer is JS `undefined`, which is not null. (b) JS `undefined` leaked into the Lambda face generally; absence in Lambda is `null`, so the catalog's reads now normalise at that boundary in `dom_prop_get` and only there — the JS door keeps `undefined` by calling `dom_get_property_impl` itself. (c) **A genuine web-platform conformance bug**: `nextElementSibling`/`previousElementSibling` were missing for `CharacterData`, so every text and comment node answered `undefined` through *both* doors where DOM §4.2.8 requires the neighbouring element (`NonDocumentTypeChildNode` is implemented by CharacterData as well as Element). Fixed in the shared text-like property helper, so script and JS become correct together. None of the three was visible to inspection; all three fell out of comparing a derivation against its fast path on 24 nodes. |
| ESO96 | **`==` answers `true` for any two DOM nodes, and the language will not fix it.** A node wrapper is a host VMap that materialises no entries (its properties are computed through the protocol), so `a == b` compares two empty maps: `dom.get_element_by_id(root, "made") == dom.get_element_by_id(root, "intro")` is `true`. This is not a missing identity operator — **S5.1.4** rules that `==` is the only equality, values have no identity, and no `===`/`ref_eq` "exists or ever will". But it does contradict **S5.1.1** ("within-family it is deep, structural, and value-based"): the comparison sees none of the value. Two consequences. (a) DOM-level, settled here: node identity is an *operation* — the catalog gains core `same_node(a, b)` over `JUBE_DOM_IS_SAME_NODE`, and the `contains`, `radio_group` and `form_controls` derivations that were written with `==` now say `same_node(...)`; `dom_api_core.ls` pins both the fix and the trap. (b) Language-level, **for the user**: every Lambda script that compares two DOM nodes with `==` silently gets `true`. Either VMap equality must consult the host for computed content or identity, or `==` on a zero-entry host VMap should be an error rather than a silent `true`. That is an `S#` ruling, so it is asked, not invented. |
| ESO93 | **The document node is not a node from Lambda.** `owner_document(n)` answers the document *proxy* (a map with its own property getter), `parent_node(html)` answers null and `node_type(document)` is unreadable, so `root_node`'s walk ends at the document element while JS `getRootNode()` answers the Document. Found by F28's `dom_api_core.ls`. Needs the document to be a `dom_node` wrapper (node type 9) whose property reads go through the one protocol; until then derivations that mention the document (`root_node`, `owner_document`, `clone_node` of a document) are Lambda/JS-divergent. |
| ESO94 | **Module-side ordinals.** `JUBE_DOM_GET_ROOT_NODE` (and the `select` overloads of `NAMED_ITEM`/`ADD`/`REMOVE`) are answered in `radiant_dom_bridge.cpp` *before* delegation, never by `dom_element_operation_impl`, so a core body that delegates the ordinal is null from Lambda. F28 made `root_node` its own derivation (a `parent_node` walk); F30's core-section move must audit every `if (operation == …)` in the bridge's element dispatcher for this pattern and pull those bodies into the core executor. |
| ESO95 | **WPT dom/nodes: 20 files segfault and 2 spin forever — pre-existing, hidden by the harness.** Found while gating F28. The harness pre-runs 273 files by spawning `lambda.exe js … --document …`; a file outside the 23-entry passing baseline is reported as *skipped (known failure)*, so a crash there is invisible. Verified binary-against-binary on 2026-09-02 (HEAD `5f7b90a2d` built in a worktree vs the F28 tree, same generated scripts, 20 s bound): identical exit codes on every case — segfault (139) on `Document_characterSet_normalization_{1,2}`, `Document_contentType…datauri_02`, `MutationObserver_{attributes,characterData,cross_realm…,inner_outer,textContent}`, `Node_parentNode`, and the `moveBefore_*` css-animation/transition cases; an unbounded spin (124) on `MutationObserver_childList` and `Node_childNodes_cache_2` (the latter also fails the regression-enforced baseline on HEAD). The F28 catalog changes none of them. Two follow-ups outside this design: give the harness a per-case time bound (a spinning case blocks the whole run today), and fix the spins/crashes. |
| ESO99 | **`test_wpt_dom_events_gtest` fails 101 of 204 and nothing guards it.** Unlike the dom/nodes suite it has *no* baseline file, so `BASELINE_PATH` is empty and the "known failure" skip never engages — every failure is reported — and neither `make test-lambda-baseline` nor `make test-radiant-baseline` runs it. Measured while attributing the CharacterData fix: the failing set is **byte-identical with and without that fix** (101 = 101, no regressions, no repairs), so it is pre-existing. It needs a recorded baseline like the other suites, and then gating. |
| ESO100 | **ES42's "derived bodies call only core" is blocked on ESO81/ESO93 — attempted at F30 and reverted.** Writing the §5.1–§5.3 fast paths over `dom_core_*` instead of the ordinal executor (the prerequisite for splitting them into `dom_derived.cpp` and linting the includes) fails on three counts, each pre-existing: (a) **`node_value` on an element segfaults** in a realm-less script — the element getter has no `nodeValue` case, so it falls through to a realm-dependent fallback that reaches `js_create_constructor` with no realm (the ESO81 class, now with a concrete repro: `dom.node_value(element)`), and `clone_node`'s derivation reads exactly that; (b) **`create_node(owner_document(n), …)` returns null** because `owner_document` answers the document *proxy* and the creator cannot unwrap a proxy to a document (ESO93 biting directly), so `clone_node` cannot be expressed as its derivation states; (c) `remove_child` through the ordinal does not remove a comment node where `JUBE_DOM_REMOVE` does, so `remove(n) = remove_child(parent_node(n), n)` is not yet equivalent to the operation it replaces. The §5.1 traversal and §5.3 character-data derivations *did* hold — their oracles passed against bodies written over core — so the blockage is specific, not general. Order of work: fix ESO81's realm fallback and ESO93's document node, then split and lint (ESO87). Attempting the split before them replaces tested bodies with crashing ones. |
| **ESO81 — RESOLVED 2026-09-02** | **The realm fallback.** Three distinct dependencies, all fixed. (1) *Property reads*: the element getter ended in an unguarded prototype lookup, which builds an intrinsic constructor out of the realm's pool — `dom.node_value(element)` faulted on a null pool. The lookup is realm shape (ES33/D7.4.4), so it is now behind `dom_realm_active()`, a new seam in the ESO79 family. (2) *The seams disagreed*: `dom_realm_has_own`/`_lookup`/`_define`/`_constructor` tested only for a global object, which can exist while the realm's allocator does not — so a write walked into `map_put` with a null input. All of them now use the one predicate, which tests `js_input` as well. (3) *Result objects*: `getBoundingClientRect`, `getClientRects` and the scroll state build keyed objects through the JS object model, which needs a realm to allocate a shape; they now build the same fields out of the **document's own Input** when there is no realm. `js_create_constructor` and `js_create_builtin_function_from_spec` also answer null rather than dereferencing a null pool. `set_text_content` and `set_inner_html` — withheld from `dom.*` because they faulted — are published, and a **sweep of all 53 published operations from a realm-less script now crashes on none** (it crashed on three before). Live collections stay JS-only by design, not by defect: S9.2.2 gives Lambda snapshots. |
| **ESO93 — RESOLVED 2026-09-02** | **The document node.** The document answered nothing to a Lambda caller: no node type, no name, not the parent of `<html>`, and unusable as the document argument to `create_node`, because `dom_document_get_property` serves the *proxy* off `_js_current_document`, which a Lambda-only document never sets. Fixed at the node level rather than the proxy's: `parent_node(documentElement)` now answers the document node (DOM §4.4) via `dom_get_or_create_doc_node`, that node reports **type 9** and the spec-fixed name `#document` (names beginning `#` are no longer uppercased as if they were tags), `ownerDocument` falls back to it when no realm is bound, and one `dom_document_from_item` resolves a document from either spelling. A walk upward now terminates at the document, so `root_node` and JS `getRootNode()` agree. Regression test: `test/lambda/dom_realm_free.ls`. |
| **ESO101 — RESOLVED 2026-09-03** | **One Document, and it is a node.** Three things were wrong. (1) The Document's own properties lived only on a proxy served off a global "current document", so the document *node* — the thing a tree walk reaches — could answer none of them, and a Lambda-only document, which never sets that global, got nothing at all. `dom_document_get_property` is now document-explicit (`…_for(doc, prop)`), the `#document` node answers as a Document from its own `doc` before falling through to the element path, and it accepts Document writes the same way. (2) `documentElement.parentNode` answered the *node* while JS's `document` was the proxy, so `documentElement.parentNode === document` was **false** for JS while true for Lambda — a divergence ESO93 introduced. It now returns whichever object the realm calls the Document: the document object inside a realm, the node outside, so identity holds in both doors. (3) The reason (2) was invisible from the core: the radiant module's `parentNode` **member ordinal read `node->parent` directly** — a second implementation of a core operation, so the core's fix never reached JS. It delegates now, as ESO83's walkers do. Also: `document.parentNode` and `document.ownerDocument` answer `null` (DOM §4.4), not `undefined`. Tests: `test/js/dom_document_identity.js` pins the JS identity, `test/lambda/dom_realm_free.ls` the Lambda side. |
| ESO102 | **The Document is still two objects across realm states, not one identity.** ESO101 makes them behave identically and agree within any single run, but `js_get_document_object_value()` still hands back the proxy inside a realm and the node outside. Making `document` literally *be* the node was attempted at ESO101 and **segfaults**: the proxy's VMap host type is what several paths dispatch on, so handing back a node-typed wrapper breaks them. Closing this needs those dispatch sites to stop keying on the proxy's host type — the ES24 record-wrapper work — after which `js_get_document_object_value()` can return the node and the proxy can retire. |
| ESO103 | **`undefined` leaked through the *ordinal* path too — ESO98 fixed only the property half.** `dom_op0..3` returned the executor's answer raw, and the executor answers JS `undefined` for an operation that does not apply to a node kind. So `has_attribute(text_node)` produced a value that *printed* as `false` but was not a bool: `has_attribute(t, "id") == false` evaluated to **false**, and `type()` reported it as null. The op helpers now apply the same absence rule as `dom_prop_get`, and `has_attribute` is its own derivation over `get_attribute` so it answers a real bool for every node kind. Found by the §5.4 oracle on its first run — inspection had already passed over this code twice. |
| **ESO104 — RESOLVED 2026-09-03: `dispatch` takes an event, and `create_event` is JS's.** The catalog typed `dispatch(n, event)` while the wired body took a *name* and fixed `bubbles=true, cancelable=false` — right for the `input`/`change` notifications it was written for, and unable to express anything else, so the `click` derivation could not be written at all. The engine's own primitive already took both flags; only the script-facing wrapper hid them. `dispatch` now accepts **either spelling**: a string keeps the historical path, and an event value carries its own flags through a new seam. An event value is an ordinary Lambda **map literal** — `{type, bubbles, cancelable}` — and there is deliberately no constructor for it: the language already writes maps, and a native factory would be one more body to keep in step. That settles what `create_event` is, which had been recorded as "needs the realm" without saying why: it is JS's legacy `document.createEvent`, which answers a JS Event object. It is JS-only **by nature, not by omission**, and Lambda does not need it. One real limit: the simple-event path has no slot for a `CustomEvent` detail payload, so carrying one needs a richer event record rather than a wider signature. |
| **ESO106 — RESOLVED 2026-09-03: the executor crosses by its own shape, and the cause was not rooting.** The previous slice blamed GC rooting for `form_submission`, on inspection alone. Measuring it took one probe and named something else: `form_submission` failed **deterministically, on its first assertion**, which a collection race cannot do. The four calls that reached the row were all `JUBE_DOM_ADD_EVENT_LISTENER`, and logging the arguments either side of the array showed two going in and **four** coming out — `[null, null, "submit", fn]`. `js_array_new(n)` takes a **length**, not a capacity, so the adapter's `array_new(argc)` returned an array already holding `argc` nulls and pushed the real arguments after them. Every handler in the fixture silently registered nothing. The host table's parameter was *named* `capacity`, which is what sold the mistake; it now says `length` and what that means. **The fix is not to pack correctly — it is not to pack.** A row is fixed-arity because a row is a Lambda function; the ordinal executor is variadic, so the uniform `invoke` row can only take an array, and building one per method call is an allocation on the DOM's hottest path that exists purely to satisfy the ABI's shape. The catalog now also publishes `invoke_raw`, the executor's own signature, as a **companion door** beside the rows — ES38's one body, two doors, at the ABI rather than at the language face. All three `#define`s in the module cross the catalog now: the two property rows are already `Item`-uniform and map straight onto their slots, and dispatch adds no allocation anywhere. **The rooting hazard was real, and is closed where it actually lives.** `dom_core_invoke` still unpacks for a caller that legitimately holds an array, and its buffer is a `RootSpan` rather than a native array, because the executor allocates while it walks that span (D5.2.1). The span is sized to `argc` and fails closed on reservation overflow, so the old eight-argument cap is gone. `RootSpan::items()` was promoted out of a `static` in `js_runtime.cpp` rather than copied. The catalog's one-slot-per-row `static_assert` now counts declared companion doors explicitly, so the section cannot grow ad-hoc members by accident. **The lesson is the stage's own, turned on itself**: this stage has now shown four times that signature agreement is the weak test, and the ESO106 entry that this one replaces was written from a signature-level reading of my own adapter. The probe cost ten minutes. |
| **ESO107 — RESOLVED 2026-09-03: the catalog was Lambda-faced in its *return values*, and that is why class A would not migrate.** `dom_add_event_listener_body` and `dom_add_event_listener_bridge` call the same function and differ in one thing: the row answers `ItemNull`, the bridge answers `make_js_undefined()`. So the row could not *be* the bridge, and routing the module onto it would have changed what `addEventListener` returns for JS from `undefined` to `null`. That is not a contract accident in one slot — it is a property of every row, so the whole of class A was blocked by it. This is the **third** appearance of the same split: ESO98 found it in the property getter, ESO103 in the ordinal path, and F34's slice 2 had to strip normalisation from the three dispatch rows for exactly this reason. Each time it was treated as local. The rule belongs on the boundary that *is* the Lambda face, not inside the bodies. `dom_build_published_table` now publishes every row through a generated per-row trampoline that applies `dom_absent_to_null`, so a row body may answer `undefined` and still be the one body both doors call (ES38). Nothing is written by hand per operation; `dom_absent_to_null` was promoted out of a `static` rather than copied. The proof is that `add_listener` and `remove_listener` now **are** `dom_add_event_listener_bridge` and `dom_remove_event_listener_bridge` — the Lambda-only `_body` copies are deleted — and the module's two listener slots cross the catalog. The Lambda goldens are byte-identical, because null is what they always saw. |
| ESO90 | **Side effects hiding in current bodies.** The present `innerHTML` setter re-registers element ids on Window; `set_attribute` runs reflection hooks; `insert_before` fires iframe `load` scheduling. Each is either core (mutation record, reflection into the state store — stays inside the core mutator) or realm (Window named access — becomes a mutation-ring subscriber in the adapter). F30/F31 must classify every such side effect explicitly; the derivations in §5 assume the core mutators carry *all* DOM-visible effects and *no* realm effects. |

---

## Appendix A — the dedup census (47 operations declared on 2+ surfaces → one canonical row each)

| Canonical (tier) | `radiant.*` | `dom.*` | host slot(s) | element ordinal | document ordinal |
|---|---|---|---|---|---|
| `get_attribute` (core) | `attr` | `attr` | — | `GET_ATTRIBUTE` | — |
| `set_attribute` (core) | `set_attr` | `set_attr` | — | `SET_ATTRIBUTE` | — |
| `remove_attribute` (core) | — | `remove_attr` | — | `REMOVE_ATTRIBUTE` | — |
| `has_attribute` (derived) | `has_attr` | `has_attr` | — | `HAS_ATTRIBUTE` | — |
| `insert_before` (core) | — | `insert_before` | `insert_before_bridge` | `INSERT_BEFORE` | — |
| `remove_child` (core) | — | `remove_child` | `remove_child_bridge` | `REMOVE_CHILD` | — |
| `append_child` (derived) | — | `append` | `append_child_bridge` | `APPEND_CHILD` | `APPEND_CHILD` |
| `remove` (derived) | — | `remove` | `remove_bridge` | `REMOVE` | — |
| `replace_child` / `replace_with` (derived) | — | — | both `_bridge` | both | — |
| `clone_node` (derived, FP) | — | `clone` | `clone_node_bridge` | `CLONE_NODE` | — |
| `contains` (derived) | — | `contains` | — | `CONTAINS` | `CONTAINS` |
| `compare_document_position` (derived) | — | — | — | ✓ | ✓ |
| `matches` (core) | — | `matches` | — | `MATCHES` | — |
| `query_selector` / `_all` (derived, FP) | — | `query`/`query_all` | — | ✓ | ✓ |
| `closest` (derived) | `closest` | `closest` | — | `CLOSEST` | — |
| `get_element_by_id` (derived, FP) | — | `element_by_id` | — | ✓ | ✓ |
| `get_elements_by_tag_name` / `_class_name` (derived, FP) | — | — | `live_*` ×4 (→ realm) | ✓ | ✓ |
| `parent_element` (derived) | `parent` | `parent` | — | — | — |
| `first_element_child` / `next_element_sibling` (derived) | ✓ | ✓ | — | — | — |
| `root_node` (derived) | `root` | `root` | — | `GET_ROOT_NODE` | `GET_ROOT_NODE` |
| `document_element` (derived) | `document_root` | — | — | — | — |
| `dispatch` (core) | `dispatch` | — | `dispatch_event_bridge` | `DISPATCH_EVENT` | `DISPATCH_EVENT` |
| `add_listener` / `remove_listener` (core) | — | — | both `_bridge` | both | both |
| `check_validity` / `report_validity` (derived, FP) | `check_validity` | — | both `_bridge` | both | — |
| `reset` (derived) | `reset_form` | — | `form_reset_bridge` | `RESET` | — |
| `request_submit` (derived) | `submit_event` | — | `form_request_submit_bridge` | `REQUEST_SUBMIT` | — |
| `scroll_into_view` (core) | ✓ | — | `scroll_into_view_bridge` | ✓ | — |
| `scroll_operation` (core: `caret`/`scroll` named ops) | ✓ | — | `scroll_operation_bridge` | — | — |
| `bounding_box` (core) | — | — | `get_bounding_client_rect_bridge` | ✓ | — |
| `client_rects` (core) | — | — | `_bridge` | ✓ | — |
| `focus_set` (core) / `blur` (derived) | `focus_set` | — | `focus_method_bridge` | `FOCUS`/`BLUR` | both |
| `normalize` (derived, FP) | — | — | `normalize_bridge` | ✓ | ✓ |
| `insert_adjacent_element` / `_html` (derived) | — | — | both `_bridge` | both | — |
| `create_range` / `selection` (core) | — | — | `create_range`, `get_selection` | — | both |
| `adopt_node` (derived: `remove` + owner reassignment is core-internal) | — | — | `adopt_node_bridge` | — | ✓ |
| `element_from_point` (core) | — | — | `document_element_from_point_bridge` | — | ✓ |
| `text_control_caret_bounds` / `_boundary_from_point` (core, geometry) | — | — | both `_bridge` | both | — |

(Rows with ✓ in more than one column are the 47; single-surface rows are in the machine-generated census committed at F28.)

## Appendix B — source map (2026-09-02)

| Where | What |
|---|---|
| `lambda/module/radiant/radiant_module.cpp:2754–2952`, `:50–60` | the 98 `radiant.*` rows; the 8 direct externs |
| `lambda/dom/dom_module.cpp`, `dom_ops.h` | F26's 23 rows and the current core entry points |
| `lambda/jube/jube.h:676–928`, `jube_registry.cpp:1507–1731` | v1 table and fill |
| `lambda/dom/dom.cpp:6058` (`JS_DOM_PROPS`), `:838` (`dom_visible_child`) | the 148-name property protocol; the script-visible traversal rule §4.1 adopts |
| `radiant/editing_dom_waist.cpp`, `radiant/text_edit.cpp` (`tc_*`), `radiant/*state_store*` | the engine-side core bodies (§4.9, §4.6) |
| `lambda/module/radiant/radiant_dom_iface.cpp:316–357` | the 60 direct range/selection entries F31 removes |
| `lambda/package/dom/*.ls` | the 70-name usage census; migrated in F32 |
