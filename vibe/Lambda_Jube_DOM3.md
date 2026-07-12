# Lambda Jube DOM — Stage 3 (DOM3): Table-Driven Property Dispatch — Review and Proposal

> **Status**: Phases 0–3, 4a–4e, and Phase 5 are implemented
> and green (2026-07-12). The Phase 4e
> DOM/document family is record-driven with `host_ops = NULL`; `dom_node` has **142 declared
> members** (identity/nav, reflected attributes, live form controls, and all node methods
> with D0d function-object reads, including restored `get_attribute`) and now routes the
> residual DOM object behavior through record-owned Jube hooks instead of `legacy_ops`.
> `document`/`foreign_document` are binding-hook driven, the residual engine switchboards
> were swept, and the original jQuery/Sizzle `.call` repro stays true in release. Phase 5
> removed VMap's DOM-specific snake/camel and dashed-attribute shortcuts, moved Lambda
> projection keys to declared Jube snake-case rows, widened `document` and character-data
> projection records, and added a Lambda-native Jube method closure path for non-JS runtime
> method reads. Phase 4e gates: `make build`, `make build-test`, direct
> `jq_find_repro` (`PRE: true`, `LEN: 3`, `POST: true`), direct `dom_module_props`
> diff-exact, focused DOM GTests 4/4, full JS GTest 309/309, UI-automation 233 passed
> with two native-webview tests skipped, `make release`, and release `jq_find_repro`
> (`POST: true`). Phase 5 slice gates: `make build`, `radiant_dom_read` diff-exact,
> `radiant_dom_mutate`, procedural `radiant_dom_set`, `make test-lambda-baseline`
> (3299/3299), UI automation (233 passed, 2 native-webview skips), `make release`,
> and release wrap-sweep benchmark (`query_ms=9/7/8`, `walk_ms=901/773/807`).
> **Parent design**: [Lambda_Desing_Native_Module.md](./Lambda_Desing_Native_Module.md) — Jube modules, signatures in Lambda type syntax, VMap projections.
> **Predecessors**: [Lambda_Jube_DOM.md](./Lambda_Jube_DOM.md) (DOM1: carrier switch to branded VMaps),
> [Lambda_Jube_DOM2.md](./Lambda_Jube_DOM2.md) (DOM2: generic host-object protocol, real host API,
> descriptor-driven registration, Lambda projections).
> **Downstream consumer**: [Lambda_Design_DOM_Pkg.md](./Lambda_Design_DOM_Pkg.md) — its L4 adapter is
> "generic machinery (route getter X of prototype Y to Lambda function F), not per-API C+ code."
> The property tables proposed here **are** that machinery.

## Progress snapshot (2026-07-11)

- **Review cleanup complete**. The DOM3 review has been refreshed around the current live
  code shape: `JubeTypeDef` stays frozen, the authored IDL surface is now the module-level
  Lambda interface declaration, and the C side is narrowed to binding tables.
- **DOM2/Jube prerequisites verified in code.** `lambda/jube/jube.h` already has ABI v1
  `JubeModuleDef.struct_size`, `JubeTypeDef.host_ops`, `JubeFuncDef.signature`, and the host API
  surfaces DOM3 builds on; `lambda/module/radiant/radiant_module.cpp` registers the static
  `radiant` module with `dom_node`/Range/Selection/CSSOM/document host types and top-level
  `JubeFuncDef` functions.
-  **Current signature gap documented.** `build_ast.cpp` still parses `JubeFuncDef.signature`
  with positional string scanning and hardcodes `can_raise = false`; DOM3 Phase 0 keeps the
  parser upgrade as an explicit task instead of pretending it is done.
-  **Phase 0 ABI direction cleaned up.** The plan now uses an additive `JubeModuleDef`
  extension guarded by `struct_size`; it no longer proposes a `JubeTypeDef` stride change or
  `JUBE_ABI_VERSION` bump.
- **Phase 0 implemented (2026-07-11):** `interface_decl` + `JubeMemberBind`/`JubeTypeBinding`
  on the `JubeModuleDef` additive tail; registration-time interface parsing (real Lambda
  grammar) with binding cross-checks; per-type content-hashed dual-keyed property indexes;
  generic record dispatch wired ahead of host_ops in js_runtime/js_globals/vmap;
  `hostobj_demo` converted (`host_ops = NULL`) with its golden matching byte-for-byte. Full
  gates recorded in the Phase 0 progress log below. **Not started:** Phases 1+ (range/
  selection, CSSOM, style hosts, element/document, Lambda projection convergence), the
  `reflect_attr` generic routine, `applies_to` tag guards, `pn`-typed members.

Four directional decisions are fixed as input to this proposal (not open questions):

- **D0a — No IDL.** Property/method interfaces are declared in **Lambda type syntax**. The
  parent design's rule stands: *"the module interface language is the Lambda type language.
  No IDL, no second grammar."*
- **D0b — Text now, binary later.** The interface declaration stays human-readable text parsed
  at registration. Compiling it to a binary form for faster load is acknowledged future
  fine-tuning, not part of this design.
- **D0c — Module-level interface via Lambda object types; `JubeTypeDef` frozen.** The
  IDL-equivalent is one **module interface declaration**: a sequence of Lambda `type` object
  declarations (the existing `object_type` grammar — fields, `;`-separated methods,
  inheritance, defaults) plus top-level `fn`/`pn` signatures. It hangs off `JubeModuleDef`
  **additively** (its `struct_size` gate makes that safe); `JubeTypeDef` keeps its v1 layout —
  no array-stride ABI break. The C side supplies only *binding tables* mapping declared member
  names to handler pointers.
- **D0d — Method-name property reads return real function objects**, not the `ITEM_TRUE`
  feature-detection sentinel (§2.6 has the in-code evidence this was already breaking and
  already being patched piecemeal).

---

## Part 1 — Review: what the current dispatch code actually looks like

DOM2 fixed *who dispatches* (the engine consults the Jube registry, not brand predicates). It did
not touch *how each type answers*: every host-ops implementation still resolves property and
method names by walking `strcmp` chains. A full scan of the five DOM implementation files
(2026-07-11) gives the ground truth below.

### 1.1 Scale

| File | strcmp/strncmp/strcasecmp | Name-dispatch surfaces |
|---|---:|---|
| `lambda/js/js_dom.cpp` (14,325 lines) | **708** | element/text/comment get (147 names), set (33), element methods (58), document get (~42) + methods (40), location (9), classList (7+2), DOMImplementation (4), style hosts, reflection helper chains |
| `lambda/js/js_dom_selection.cpp` (2,022) | **130** | range (33 names), selection (29) — each list duplicated in a `*_is_native_property` predicate |
| `lambda/js/js_cssom.cpp` (1,633) | **41** | stylesheet (~9), css_rule (~8), rule_style_decl (2+3 + CSS-table), CSS namespace (2) |
| `lambda/module/radiant/radiant_dom_bridge.cpp` | **279** | its own element get/set/method chains (32+28+59 names), reflected-attr predicate chains, hardcoded own-key lists |
| `lambda/js/js_dom_events.cpp` (2,274) | 26 (none is property dispatch) | — events are ordinary stamped Map objects; **out of scope for DOM3** |

Totals across the dispatching files: **~1,150 name-comparison sites**, **~184 distinct element/
document property names**, **~145 distinct method names**, plus range/selection/CSSOM surfaces.
Every property read on a DOM element is a linear walk that averages dozens of `strcmp`s; a miss
(expando read, feature probe) walks the entire chain — for elements that is ~150 comparisons
before reaching the expando/prototype fallback.

### 1.2 The findings (ranked)

#### F1. The property model is stored as control flow, in up to four copies per type

The same name list is hand-maintained in parallel chains that must agree but have no mechanism
forcing agreement:

- **Range**: `range_is_native_property` (js_dom_selection.cpp:259, 33 names) duplicates the
  getter chain (`js_dom_range_get_property`:940), feeds the setter guard (:1005), *and* the
  bridge keeps a third copy as the own-keys list (radiant_dom_bridge.cpp:2616) plus a fourth in
  `init_range_methods` (:912) binding method arities. Selection: same ×4 (29 names).
- **Element**: getter chain (js_dom.cpp:8915), setter chain (:10477), method chain (:12533),
  `dom_methods[]` feature-detect list (:10344, 55 names), `form_idl_props[]` skip list (:10191),
  bridge own-keys list (radiant_dom_bridge.cpp:2630, 25 names), bridge's *own* get/set/method
  chains (:1436/:1624/:1888) that partially shadow the engine's.
- **Document**: method chain (:5689), property chain (:6225), `doc_methods[]` (:6499, 26 names).
- **Drift is already observable**: `js_cssom_resource_has_property` (js_cssom.cpp:359) omits
  `length`/`disabled`/`href`/`title` for stylesheets and diverges on `parentStyleSheet` — the
  `in` operator and the getter disagree today. This is the bug class the structure guarantees.

#### F2. Keys are compared by content on every access; nothing is interned or hashed

Property keys arriving at host ops are **transient, non-pooled strings**: the vmap host path
builds lookup keys with `heap_strcpy` (`string_key_item`, vmap.cpp:52), not `heap_create_name`.
So pointer identity is unavailable, and the bridge does `fn_to_cstr` + `strcmp` per access
(`radiant_dom_key_equals`, radiant_dom_bridge.cpp:2448). Meanwhile the infrastructure for doing
better already exists and is unused here: `NamePool` guarantees one canonical `String*` per
content (`name_pool_create_name`, name_pool.cpp:79), and `lib/hashmap.h` offers content-hashed
tables with `_with_hash` variants. The in-repo precedent is the CSS property system:
`css_property_id_from_name` (css_properties.cpp:448) resolves ~name→id through a static
descriptor array + djb2 hash table built once at init — **the style half of DOM dispatch is
already table-driven; only the API-surface half is strcmp chains.**

#### F3. Per-access costs stack multiplicatively

One `node.tag_name` read from Lambda today: linear `jube_find_type_by_host_type` scan over all
modules×types (jube_registry.cpp:578) → `snake_to_camel_key` transform + transient key alloc
(vmap.cpp:119) → bridge strcmp chain (up to ~150 compares) → possibly a second chain in
`js_dom_get_property_impl` as fallback. Three of those four costs are pure dispatch overhead
that a registration-time index eliminates. (`host_type` *is* the `JubeTypeDef*` — the linear
scan is only validating a pointer we already trust everywhere else.)

#### F4. Derived surfaces are hand-built instead of derived

`own_property_keys` returns hardcoded camelCase arrays (radiant_dom_bridge.cpp:2612–2657);
descriptors are synthesized ad hoc; `has_property` chains re-list names (F1); prototype method
objects are built by hand-maintained binders with hardcoded arities
(`init_range_methods`/`init_selection_methods`, js_dom_selection.cpp:912/1504); the
feature-detect lists `dom_methods[]`/`doc_methods[]` return `ITEM_TRUE` for method names read as
properties. All six surfaces are projections of one fact base — "this type has these properties
and methods" — that exists nowhere as data.

#### F5. Attribute reflection is predicate chains + scattered defaults

`_idl_to_attr_name` (js_dom.cpp:8686), `_is_bool_reflected` (:8739), `_is_int_reflected`
(:8769), `_is_string_reflected` (:8805) — plus the bridge's parallel copies
(radiant_dom_bridge.cpp:359/495/560/683) — encode what WebIDL calls *reflected attributes*
(`checked` ⇄ attribute `checked`, `maxLength` ⇄ `maxlength` with default −1, …) as code. These
~40 properties need **no handler function at all** under a table: name, attr name, type, default,
tag gate — five data columns.

#### F6. Tag-dependent properties are real but bounded, and guards are enumerable

The scan found 90 `_is_tag(elem, "…")` sites (37 in get, 17 in set) and 30
`tc_is_text_control_elem` sites. The pattern is always the same shape: *same property name,
different behavior per tag set* (`value` on select/option/input/textarea/output; `type` on
select/button/input; `checked` on input; `selected` on option; size/rows/cols int-reflection
gates). This does not break table dispatch — it means a name can map to an ordered list of
guarded entries (WebIDL solves the same problem with per-interface tables; we can solve it with
per-entry tag guards without splitting the `dom_node` brand).

#### F7. A minority of the surface is catch-all by specification, not enumerable

Four patterns cannot be table rows because their name sets are open:

- the `<form>` **named getter** (js_dom.cpp:10188): any name → matching descendant control;
- the **generic attribute fallback**: any unmatched name on an element reads/writes the
  attribute (:10247, :10979);
- **`on*` event-handler properties** (:1283): recognized by prefix;
- **live-collection indexed/named access** with before-get refresh
  (`js_dom_collection_before_property_get`, :7557) — also the source of the ~13 residual direct
  `js_dom_*` calls still sitting in generic engine files (js_runtime.cpp:4179 etc., noted at the
  DOM2 exit review).

WebIDL names this exact split: *regular attributes/operations* vs *named/indexed getters and
setters* on legacy platform objects. The design must mirror it: tables for the enumerable
surface, explicit per-type fallback hooks for the catch-alls.

#### F8. The signature parser is not yet strong enough to carry the design

Today's `JubeFuncDef.signature` parsing (build_ast.cpp:156–266) is positional string scanning:
comma-counted arity, first-parameter type only, a six-keyword type list (everything else →
VMAP/any), and **no `T^` error-tier parsing** (`can_raise` hardcoded false,
build_ast.cpp:317). DOM2 Phase 4 already flagged reusing the real Lambda type parser as the
intended end state; DOM3 depends on it for method arity/typing and consistency.

### 1.3 Review verdict

DOM2 built the right skeleton: one generic dispatch rule, per-type ops tables, a versioned host
API. But each ops implementation is a hand-written interpreter of an implicit interface. The
interface itself — names, types, mutability, applicability, arity — must become **declared
data**, parsed once, indexed once, and then *derived from* everywhere it is needed. That is also
precisely the seam `Lambda_Design_DOM_Pkg.md` needs: once "property X of type Y routes to
function F" is a table row, rebinding F from a C+ function to a Lambda function is a data change,
not an engine change.

---

## Part 2 — Design

### 2.1 Goal

1. **One fact base per type**: a module-level interface declaration in Lambda object-type
   syntax, plus a C binding table, declaring every regular property, method, and constant
   (kills F1, F4, F5).
2. **O(1) name resolution**: a per-type name index built at registration — content-hashed,
   dual-keyed camelCase + snake_case (kills F2, F3; deletes the per-access snake↔camel
   transforms from `vmap.cpp`).
3. **Declared applicability**: per-entry tag/kind guards so tag-dependent properties are ordered
   data, not nested if-trees (absorbs F6).
4. **Spec-honest catch-alls**: explicit `named_get/named_set/indexed_get/indexed_set` hooks per
   type for the four open-name patterns (absorbs F7; re-homes the residual generic-engine
   `js_dom_*` calls behind the protocol).
5. **Derived everything**: `has`, `own_property_keys`, descriptors, prototype method objects,
   feature-detection, and Lambda-side key iteration are all computed from the table by the
   generic layer — never hand-listed again.
6. **Module-owned tables, generic-owned machinery**: descriptor tables live in the module
   (radiant); parsing, indexing, lookup, guard evaluation, and derivation live in the generic
   Jube layer, so `hostobj_demo` and every future module get it all for free — the DOM2 promise,
   extended one level down.

Non-goals: migrating behavior *implementations* to Lambda script (that is
`Lambda_Design_DOM_Pkg.md` Phase 3 — DOM3 builds its seam); event objects (already ordinary
maps; no dispatch to fix); inline caches (enabled by this design, not included); binary signature
form (D0b); dataset (`js_dataset_get_property` is already zero-strcmp algorithmic conversion —
it becomes a named-hook, unchanged).

### 2.2 The module interface declaration — Lambda object types as the IDL level (D0c)

The whole-module interface is **one Lambda-syntax text**: a sequence of `type` object
declarations plus top-level `fn`/`pn` signatures — a Lambda header file. The grammar already
exists: `object_type` (grammar.js:1130; doc/Lambda_Type.md §Object Types) gives nominally-typed
maps with fields, a `;`-separated methods section, single inheritance, and default values.

```lambda
// radiant module interface (excerpt) — pure existing Lambda type syntax
type dom_node {
    node_name: string,
    node_type: int,
    parent_node: dom_node?,
    text_content: string,
    ELEMENT_NODE: int = 1;               // spec constant: default literal + no setter binding
    fn contains(other: dom_node) -> bool
    pn remove_child(child: dom_node) -> dom_node^
}

type element : dom_node {                // inheritance: node basics declared once
    tag_name: string,
    id: string,
    value: string;                       // tag-guarded behavior lives in bindings (§2.3)
    fn matches(selectors: string) -> bool
    pn set_attribute(name: string, value: string)
}

type document : dom_node { ...; fn query_selector(sel: string) -> element? }

pn load(path: string) -> document^      // top-level module functions (today's JubeFuncDef)
```

Conventions, all derived rather than invented:

- **Method vs property**: declared with `fn`/`pn` vs declared as a field. The fn/pn purity
  split stays part of the interface (mutating methods are `pn`), `->T^` marks can-raise.
- **Writability = "a setter is bound"** (§2.3). No readonly syntax is invented; a field with no
  `set` binding is readonly, exactly like today's fall-through-to-expando behavior.
- **Constants** are fields with default literals and no bindings — existing grammar
  (`START_TO_START: int = 0`).
- **Inheritance** (`element : dom_node`) flattens at registration into the per-type index, so
  the node/element/document surfaces are declared once each — the hand-list duplication class
  (F1) cannot re-form even inside the declaration.
- **Names are snake_case** (Lambda convention is canonical in the interface); the JS camelCase
  spelling is derived at registration. Derivation is not bijective for acronyms
  (`inner_html` → `innerHtml`, not `innerHTML`), so bindings carry an optional `js_name`
  override for the ~dozen irregulars (`innerHTML`, `outerHTML`, `namespaceURI`, `baseURI`, …).

**One grammar accommodation to decide in Phase 0**: interface methods are declaration-only
(no body), while `fn_stam` in script context expects one. Either the object-type methods
section learns an optional-body form (also useful for future `.li` interface files), or the
registry's parse context accepts the body-less form specially. Parsing goes through the real
Lambda type parser (finishing the upgrade DOM2 Phase 4 deferred — F8); the parsed form is
cached in the registry, and a binary pre-parsed blob remains future fine-tuning (D0b).

The interface text is also the human-readable module manifest: `lambda module info radiant`
prints a real API listing — types, properties, methods, purity, error tiers — for free.

### 2.3 Binding tables — the C side shrinks to name → handler pointers

`JubeTypeDef` is **unchanged** (D0c). `JubeModuleDef` gains three fields, appended under its
existing `struct_size` gate (the registry's check becomes `struct_size >= V1_SIZE` with
new-field access gated on the module's declared size — additive, no stride hazard, because
`JubeModuleDef` is always passed by pointer, never embedded in arrays):

```c
struct JubeModuleDef {
    ...                                   // v1 fields exactly as today
    const char* interface_decl;           // §2.2 Lambda interface text (NULL = none)
    const JubeTypeBinding* type_bindings; // one per declared type
    int32_t type_binding_count;
};

typedef struct JubeMemberBind {
    const char* name;         // snake_case, must match a declared interface member
    const char* js_name;      // optional camelCase override for irregulars; NULL ⇒ derived
    const char* applies_to;   // NULL = all receivers; else lowercase tag list
                              //   ("input select textarea"), interned at registration
    int (*guard)(Item receiver);          // optional extra predicate (text-control check…)
    int (*get)(Item receiver, Item* out);
    int (*set)(Item receiver, Item value, Item* out);            // absent ⇒ readonly
    int (*call)(Item receiver, Item* args, int argc, Item* out); // methods
    const char* reflect_attr; // non-NULL ⇒ attribute-reflected: generic reflect routine
                              //   handles get/set from the declared type + default; no
                              //   handler functions needed at all
} JubeMemberBind;

typedef struct JubeTypeBinding {
    const char* type_name;    // matches `type X {...}` in interface_decl
    const void* host_brand;   // the JubeTypeDef* used as vmap->host_type (unchanged carrier)
    const JubeMemberBind* members;
    int32_t member_count;
    // open-name catch-alls (WebIDL named/indexed getters — §2.5); any may be NULL
    int (*named_get)(Item receiver, Item key, Item* out);
    int (*named_set)(Item receiver, Item key, Item value, Item* out);
    int (*indexed_get)(Item receiver, int64_t index, Item* out);
    int (*indexed_set)(Item receiver, int64_t index, Item value, Item* out);
} JubeTypeBinding;
```

Handlers return status ints under the pending-exception model, matching `JubeHostObjectOps`.
Same-name members with different `applies_to`/`guard` are legal; **declaration order is
resolution order** (first matching guard wins), preserving today's ordering-dependent overloads
(`<select>.remove(index)` before `ChildNode.remove()`, js_dom.cpp:12550) as visible data.

**Registration cross-checks the two halves** and fails loudly on mismatch: every declared
member must have exactly one binding cluster (declared-but-unbound is an error, except
constants) and every binding must match a declared member (bound-but-undeclared is an error).
The registry then compiles interface + bindings into **internal per-type descriptor records**
(name, parsed type, kind, flags, guards, handlers) — the property-descriptor concept survives,
but as a *derived internal structure*, not authored ABI surface. Everything downstream (§2.4
index, §2.5 dispatch, §2.6 prototypes, §2.7 projections) consumes those records.

This is the DOM package's shape/behavior split applied at the module boundary: **shape is
Lambda text, behavior is a table of C pointers** — and when the DOM package later rebinds a
member from a C+ handler to a Lambda function, only the binding side changes.

### 2.4 Name resolution: registration-time index, content-hashed, dual-keyed

At `jube_register_module_descriptor` time, for each declared type:

1. Parse the interface declaration (once), flatten inheritance, and cross-check bindings
   (§2.3). Reject malformed or mismatched declarations loudly (`log_error`, registration
   fails) — the interface does not get to be half-valid.
2. Build the type's **property index**: a `lib/hashmap.h` table mapping name → head of the
   same-name entry chain (declaration-ordered). Entries are keyed by **content hash**
   (xxhash3/sip over bytes — the CSS property table precedent, css_properties.cpp:619), because
   incoming keys are transient strings (F2). Both spellings are inserted per entry: the declared
   snake_case and the derived camelCase — honoring any `js_name` override — (`tag_name` and
   `tagName` → same descriptor record). Both precomputed strings live on the record, so neither
   front-end ever converts case at runtime again.
3. Intern the `applies_to` tag names via `name_pool_create_name`; a guard evaluation is then a
   ≤4-element pointer/`strcasecmp` check against the element's tag, not a fresh parse.

Lookup at dispatch time: one hash of the incoming key (length available on `String`), one probe,
then guard evaluation down the (almost always length-1) chain. This replaces: the snake→camel
transform + transient allocation in `vmap_host_get_by_item` (vmap.cpp:119), the camel→snake
transform in `append_host_key` (vmap.cpp:172), the hardcoded `setAttribute` special case
(vmap.cpp:141 — becomes radiant's `named_set` hook, §2.5), and every strcmp chain the tables
absorb.

Also fixed here: `jube_find_type_by_host_type` stops being a linear scan per access (F3). Since
`host_type` *is* the `JubeTypeDef*`, validation becomes an O(1) membership check against a small
registered-typedef hashset built at registration; the hot path trusts the brand exactly as much
as it does today, minus the loop.

### 2.5 Generic dispatch: table first, hooks second, expando/prototype last

The generic layer (the `js_host_object_*` entries DOM2 created, plus the vmap host path) gains
one shared resolution routine:

```
resolve(type, receiver, key):
    entry = prop_index_lookup(type, key)          // §2.4; guard-filtered descriptor records
    if entry: return entry                        // accessor/method/constant/reflected/alias
    if key is array-index and binding->indexed_*: → indexed hook
    if binding->named_*:                          → named hook (form named getter, attribute
                                                     fallback, on* handlers, dataset,
                                                     CSS property names on style/decl types)
    → expando store → prototype chain             // unchanged DOM1-pinned order
```

- `get_property`/`set_property`/`call_method` consume the resolved record: reflected members
  run a shared generic reflect routine (attribute read/write + declared-type coercion +
  default — one implementation, ~40 chains deleted); constants return their declared default
  literal; methods read as a *property* return the type's cached bound-function object (§2.6).
- `has_property` = "resolves to entry or named/indexed hook says yes or expando has it" — the
  `*_is_native_property` predicate copies and the drifted CSSOM `has` chain are deleted, and
  `in` can no longer disagree with `get` (F1's bug class is structurally gone).
- `own_property_keys` = enumerable table entries (guard-filtered against the receiver) + the
  hook's contribution (live collections enumerate indices) + expandos. The bridge's hardcoded
  key arrays (radiant_dom_bridge.cpp:2612–2657) are deleted.
- `get_own_property_descriptor` derives writable/enumerable from entry flags.
- **Live-collection before-get refresh (F7)** becomes the collection types' hook prologue:
  `indexed_get`/`named_get` on the collection type call the refresh internally. The ~13 residual
  direct `js_dom_collection_before_property_get` / `js_dom_implementation_method` calls in
  generic engine files move behind the protocol — closing the last generic-engine coupling noted
  at DOM2 exit.

`JubeHostObjectOps` remains as the escape hatch: a type may still supply custom ops (and during
migration radiant's ops delegate *table-first, legacy-chain-second*, which is what makes the
migration incremental). End state for a fully migrated type: `host_ops = NULL`, generic dispatch
runs everything from the table + hooks.

### 2.6 Prototypes, methods, and feature detection

- The generic layer synthesizes each type's prototype method objects from declared methods
  (`js_new_function(handler, arity-from-signature)`), cached **one function object per method
  per type** and GC-rooted at registration — the pattern `init_range_methods`/
  `init_selection_methods` (js_dom_selection.cpp:912/1504) already implement by hand, now
  generated. Handlers recover the receiver via `js_get_this()`, unchanged. Identity is
  spec-shaped (`a.foo === b.foo`, prototype-level), and there is no per-read allocation.
- Reading a method name as a property returns the cached function object (**D0d, decided**).
  The evidence this is a correction, not a gamble: `ITEM_TRUE` already broke real code, and the
  codebase has been converting names to real functions piecemeal — the trampoline block at
  js_dom.cpp:2282 says it outright: *"Trampolines so property access to these method names
  returns a real, callable function instead of the ITEM_TRUE feature-detection sentinel. …
  without a callable property, optional-chaining calls (`el?.querySelector(sel)`) fall through
  to `(true)(sel)` and throw 'is not a function'."* DOM3 finishes that conversion for the ~80
  names still on the `dom_methods[]`/`doc_methods[]` `ITEM_TRUE` lists (js_dom.cpp:10344/6499),
  mechanically instead of trampoline-by-trampoline. Both values are truthy, so
  `if (el.method)` probes are unaffected; `typeof`-probes flip from the wrong answer to the
  spec answer. Golden updates are isolated to the sub-step that lands this (Phase 4d), with
  `dom_jquery_lib`/`dom_v12b` watched as the real-library canaries.
- Detached-call semantics footnote: `const f = el.matches; f("div")` gets `this = undefined`;
  spec says throw "Illegal invocation", current trampolines log an error and return null. The
  generic layer keeps the current lenient behavior initially (one place to tighten later), so
  D0d introduces no third behavior change.

### 2.7 Lambda projections ride the same table

DOM2 Phase 5's projection layer currently works but pays per access (snake→camel transform) and
under-enumerates (`keys` returns a curated hardcoded list). With dual-keyed indexing (§2.4):

- Lambda `node.tag_name` and JS `node.tagName` hit the same descriptor — one write path, two
  spellings, by construction.
- `keys`/`for` iteration enumerates the guard-filtered enumerable entries with their precomputed
  snake names (a per-binding visibility flag can prune JS-isms from Lambda's view where
  wanted).
- `set` from Lambda stays pn-scoped (vmap vtable contract) and routes through the same entry —
  the mutation-notify path cannot diverge between front-ends.
- The `setAttribute`-in-generic-code hack and both case transforms disappear from `vmap.cpp`;
  what remains generic there is truly generic.

### 2.8 What stays hand-written (honest residue)

- **Handler bodies**: getters/setters/methods keep their current C+ implementations, extracted
  from strcmp arms into named functions the descriptors point at. DOM3 changes *dispatch*, not
  behavior. (Rebinding bodies to Lambda functions is the DOM package's Phase 3, and the table is
  deliberately the place where that rebinding will happen.)
- **Value-space comparisons**: input-`type` sub-dispatch (37 `itype` strcmps), enumerated-
  attribute keyword validation (`inputMode` keyword lists), tag checks *inside* behavior — these
  compare data, not API names. They stay. Expected end-state strcmp counts drop from ~1,150 to
  roughly 150–200, all of them data-value comparisons.
- **CSS property names**: already table-driven via `css_property_id_from_name`; style/decl types
  expose them through their `named_get/named_set` hooks, unchanged underneath.

### 2.9 Finding → remedy map

| Finding | Remedy | Where |
|---|---|---|
| F1 four hand-synced copies per name list | one interface declaration + binding table per type; has/keys/descriptors/prototypes derived | §2.2, §2.3, §2.5, §2.6 |
| F2 transient keys, strcmp per access | content-hashed per-type index built at registration; dual camel/snake keys | §2.4 |
| F3 linear type scan + case transforms per access | O(1) typedef membership check; precomputed snake names; transforms deleted from vmap.cpp | §2.4, §2.7 |
| F4 hand-built derived surfaces | generic derivation from the table | §2.5, §2.6 |
| F5 reflection predicate chains | `reflect_attr` binding column + one generic reflect routine | §2.3, §2.5 |
| F6 tag-dependent props | `applies_to` tag guards + declaration-ordered same-name bindings | §2.3 |
| F7 open-name catch-alls | `named_/indexed_` hooks per type; collection refresh moves inside them | §2.4, §2.5 |
| F8 weak signature parser | real Lambda type parser + `T^`; arity/can_raise derived | §2.3 |

---

## Part 3 — Phased Implementation Plan

Ground rules unchanged from DOM1/DOM2: narrowest gate per step; every phase ends with the three
anchors green (`make build`, full `./test/test_js_gtest.exe`, direct `radiant_poc`), plus
phase-specific gates; cluster-by-cluster deletion — a strcmp arm is deleted in the same change
that lands its table row, never bypassed. New standing gate: a per-file **strcmp budget** table
recorded at each phase end (the countdown meter, like DOM2's dom-hooks entry count).

### Phase 0 — Interface + binding infrastructure, proof module

1. `JubeModuleDef` additive extension (`interface_decl`, `type_bindings`) with the relaxed
   `struct_size >= V1_SIZE` check and size-gated access to the new tail; `JubeMemberBind` /
   `JubeTypeBinding` structs. **`JubeTypeDef` and `JUBE_ABI_VERSION` unchanged** (D0c) — v1
   modules keep loading as-is.
2. Interface parsing through the real Lambda type parser incl. `T^` and inheritance flattening
   (finishes the DOM2 Phase-4 deferred parser item); settle the body-less method declaration
   form (§2.2); registration-time interface↔binding cross-check; per-type content-hashed
   dual-keyed index; interned tag guards; O(1) typedef membership check.
3. Generic resolution routine (§2.5) wired into `js_host_object_*` and the vmap host path,
   consulting the compiled records **before** existing host_ops (types without an interface
   declaration see zero change).
4. Generic reflect routine; prototype method-object synthesis + GC-rooted per-type cache
   (§2.6); derived has/keys/descriptor paths for declared types.
5. **`hostobj_demo` converts to an interface declaration** (its `value`/`label`/`bump` move
   from hand-written ops to a declared type + a 3-row binding table; `host_ops` goes NULL) —
   the falsifiable proof that a module gets full dispatch from declared shape plus bound
   behavior with zero engine edits.

Gates: anchors; full JS gtest; `hostobj_demo` direct + gtest; startup `log_info` summary
`JUBE_REG: type <name> members=<n> (methods=<m>, reflected=<r>, inherited=<i>)`; Lambda
baseline (registration machinery touched).

Progress (2026-07-11): **Phase 0 complete.**

- ABI: `jube.h` gained `JubeMemberBind` / `JubeTypeBinding` and the `JubeModuleDef` additive
  tail (`interface_decl`, `type_bindings`, `type_binding_count`) guarded by
  `JUBE_MODULE_DEF_V1_SIZE`; the registry check relaxed to the v1 prefix with size-gated
  accessors (`jube_module_interface_decl` / `jube_module_type_bindings`). `JubeTypeDef` and
  `JUBE_ABI_VERSION` are unchanged.
- New `lambda/jube/jube_interface.cpp|.h`: parses `interface_decl` with the real Lambda
  grammar (Tree-sitter `lambda_parse_source`; object types live under `document > content`,
  found by recursive walk), flattens single inheritance, cross-checks declarations against
  bindings (declared-but-unbound and bound-but-undeclared fail registration loudly), and
  compiles per-type member records with a content-hashed dual-keyed (snake+camel, `js_name`
  override honored) `lib/hashmap` index. Methods are declared as **fn-typed fields**
  (`bump: fn(delta: int) int`) — existing grammar, no body-less `fn_stam` ambiguity; arity and
  `^` can-raise come from the parsed `fn_type`. Constants = default literal + no binding.
- Generic dispatch: `jube_member_get/set/call/has/delete/descriptor/own_keys/prototype`
  consult compiled records ahead of `host_ops` in `js_runtime.cpp`, `js_globals.cpp`, and the
  vmap host path (get/set/keys). Expandos are generic now: a JS object in the wrapper's own
  lazy VMap backing store under a reserved key (GC-marked via backing traversal, no rooting;
  raw `vmap_backing_get/set` accessors bypass host routing to avoid recursion). Method-name
  property reads return one cached, GC-rooted, named function object per member — the record
  pointer rides the `JsFunction` closure env (the established JsProxyData pattern) into one of
  nine per-arity trampolines; `js_get_this()` recovers the receiver. Per-type prototype objects
  are lazily created and shared with module constructors via `jube_type_prototype()`
  (instanceof identity). `gc_finalize_vmap_host_payload` falls back to `JubeTypeDef.destroy`
  when `host_ops` is NULL. Compiled records free at process exit via `jube_interface_cleanup()`
  in the pre-memtrack cleanup hook (zero-leak gate stays honest).
- **Proof: `hostobj_demo` now has `host_ops = NULL`** — a 4-line interface declaration plus a
  3-row binding table; its hand-written ops (get/set/call/has/delete/descriptor/keys/
  prototype/invalidate, ~160 lines) are deleted. `test/js/hostobj_demo.js` output matches its
  golden byte-for-byte, including expandos, `Object.keys` order, descriptor flags
  (`true,false,true`), `instanceof`, delete semantics, release-then-read undefined, and
  explicit-release + GC destroy-count stability.
- Verified gates: `make build` (0 errors); direct `hostobj_demo.js` golden match with
  memtrack-clean shutdown; full JS gtest **309/309**; focused lambda gtest
  (`*radiant*:*hostobj*`) 6/6; direct `radiant_poc` (`"ok"`) / `radiant_poc_uaf` (`null`) /
  `hostobj_demo_jube_import` (`{answer: 42, sum: 12}`) / `radiant_dom_read`;
  `make test-lambda-baseline` exit 0; startup summary
  `JUBE_REG: type hostobj_demo.hostobj_demo members=3 (methods=1, consts=0, inherited=0)`.
- Deferred within Phase 0 scope, carried to Phase 1: `pn`-typed members in the type grammar
  (fn-typed fields cover Phase 0; purity split for mutating methods lands with range/
  selection), interned tag guards for `applies_to` (no tag-gated members exist yet), the
  generic `reflect_attr` routine (no reflected members yet), and the O(1) typedef membership
  set (record lookup is a ≤64-entry scan until radiant types convert and the hot path moves
  onto records).

### Phase 1 — Range + Selection (best first target)

Closed 33+29-name surfaces, no tag gating, worst duplication ratio (×4). Convert both types to
tables (data props, constants as `CONST_INT`, aliases `baseNode`→`anchorNode` as `ALIAS`,
methods with signature-derived arities). Delete: both `*_is_native_property` predicates, both
getter/setter chains, both method binders, the bridge's range/selection own-key lists, the
range/selection branches inside the bridge host_ops.

Gates: anchors; focused `dom_module_props` + WPT-shim selection tests; grep gate:
`rg -c "strcmp" lambda/js/js_dom_selection.cpp` ≤ 10 (residual data compares only); full JS
gtest; UI-automation (selection is editing-coupled).

Progress (2026-07-11): **Phase 1 complete.**

- **Interface**: `range` (6 props + 4 constants as default literals + 23 methods) and
  `selection` (8 props + 4 non-enumerable aliases + 17 methods) declared in
  `lambda/module/radiant/radiant_dom_iface.cpp`; methods are fn-typed fields; `'type'` uses
  the symbol-name spelling (keyword as member name — the compiler strips quotes);
  `__force_direction` carries a `js_name` override (`__forceDirection`, since snake→camel
  cannot preserve the double-underscore spelling). Bindings are macro-generated thin adapters
  onto 54 new receiver-explicit `JubeHostDomAPI` entries.
- **Engine** (`js_dom_selection.cpp`, 2022 → 1606 lines): all 40 behavior functions refactored
  receiver-first (no `js_get_this()` in behavior paths); 6+8 per-property getters added; both
  `*_is_native_property` predicates, both strcmp getter/setter chains, both method
  caches/binders, the expando side tables (+ their 8 extern wrappers and GC-root arrays), and
  the prototype method publication block are **deleted**. strcmp count 130 → **5**, and the
  survivors compare data values (tag content, direction enum names), not API names.
- **Generic layer additions** driven by this phase: `JubeTypeBinding.prototype_seed` (the
  engine's live `Range.prototype`/`Selection.prototype` identity is adopted, so
  `r.__proto__ === Range.prototype` and instanceof hold), prototype-chain fallthrough on get
  miss (prototype patching + Object.prototype methods stay reachable), `__proto__` key
  handling, method function objects published onto the seeded prototype (same Items instance
  reads return), `JUBE_MEMBER_NON_ENUMERABLE` flags, and `jube_interface_runtime_reset()` —
  batch runs recreate globals per script, so cached prototypes/method Items drop in
  `js_dom_selection_reset` and re-seed against the new runtime (this also fixed the same
  latent staleness for `hostobj_demo`'s cached items).
- **Bridge**: all range/selection branches deleted from get/set/call/projected/expando/
  own-keys/prototype host ops plus 16 dead `#define` aliases; the retired `JubeHostDomAPI`
  slots (get/set_property, native_property, expando triads — 14) are NULLed with layout
  preserved. `js_dom.cpp`'s two routing arms deleted.
- **Expandos** are now the generic backing-store expando (side tables gone); `__`-prefixed
  key filtering from enumeration is preserved by the generic layer's declared-members-first
  ordering (WPT-shim internals were only reachable through the deleted side tables' name
  filter; the pinned tests pass unchanged).
- Net diff: **+485 / −735** lines across 9 files — the conversion *shrinks* the codebase
  while adding the whole declarative layer.
- Verified gates: `make build` 0 errors; **`dom_module_props` diff exit 0** (its range/
  selection section pins `__proto__` identity, prototype patching, expandos, descriptor
  `.value`, keys ordering, instanceof, delete semantics, direction/backward flows); full JS
  gtest **309/309** (suite wall-time 65s vs ~240s at Phase 0 — hash-index dispatch replaces
  chain walks in DOM-heavy batch tests, build-cache variance notwithstanding); UI-automation
  **233 passed / 2 known webview skips / 0 failed**; focused lambda gtests 6/6; direct
  `radiant_poc` / `radiant_poc_uaf` / `hostobj_demo` goldens; `make test-lambda-baseline`
  exit 0; strcmp budget: js_dom_selection.cpp = 5 (gate ≤ 10).
- Carried to Phase 2+: `pn`-typed members in the type grammar (purity split for mutating
  methods — all range/selection methods currently declare `fn`), the `reflect_attr` generic
  routine, `applies_to` tag guards, O(1) typedef membership set, and radiant_dom_bridge.cpp's
  own element chains (279 strcmps, Phase 4 scope).

### Phase 2 — CSSOM types (stylesheet, css_rule, rule_style_decl, CSS namespace)

Fixed API names → tables; `sheet[N]`/`cssRules` index access → `indexed_get`; CSS property
names on declarations → `named_get/named_set` delegating to `css_property_id_from_name` (the
camel→css conversion moves inside the hook). The drifted `js_cssom_resource_has_property` chain
is deleted — `in` is now derived and cannot disagree with `get` again.

Gates: anchors; `dom_style`, CSSOM-focused JS tests; grep gate on js_cssom.cpp; full JS gtest.

Progress (2026-07-12): **Phase 2 complete** (implemented after Phase 3 at user direction).

- **The drifted `js_cssom_resource_has_property` chain is deleted** — the scan showed the drift
  was worse than recorded: stylesheet `has` and `get` were *nearly disjoint* (has claimed
  ownerNode/insertRule/deleteRule; get served length/disabled/type/href/title/indices). Under
  records both derive from one declaration; the has-only legacy names (`owner_node`,
  `parent_style_sheet`) are declared with null getters so `in` stays true and reads stay null.
- All three types (`stylesheet`, `css_rule`, `rule_style_decl`) registered with
  **`host_ops = NULL`**; the shared `radiant_dom_cssom_host_ops` table and its eight bridge
  impls are deleted. The CSS namespace (`CSS.supports`/`escape`) is untouched — it is a plain
  map-kind object, not a branded VMAP, and dispatches through builtins.
- Engine chains split into receiver-explicit functions (~18 new dom-API entries); the retired
  chain slots are NULLed. `sheet[N]` proved the **`indexed_get`** hook (raw index incl.
  charset rules, matching the legacy bracket path); rule declarations reuse the kept
  `cssom_rule_decl_get/set_property` as their named hooks with a new CSS-table `named_has`.
  `js_dom.cpp`'s rule-style routers now call the receiver-explicit `js_cssom_rule_get_style`.
- js_cssom.cpp strcmp **41 → 7** (two are the kept declaration-getter's length/cssText backend
  branches; five compare data values). Pinned: keys=[], no prototypes, descriptors undefined
  for open names, unknown writes swallowed (sheet/rule) or parsed as declarations (decl),
  setProperty still drops its priority argument.
- Verified: `dom_module_props`/`dom_style`/`dom_v12b`/`dom_jquery_lib`/`css_namespace`/
  `hostobj_demo` goldens diff-exact; full JS gtest **309/309**; UI-automation clean
  (2 known webview skips); direct anchors; Lambda baseline green.

### Phase 3 — Style hosts (inline_style, computed_style)

API surface (`cssText`, `length`, `getPropertyValue`, `setProperty`, `removeProperty`) → table;
all CSS property names → hooks. `js_dom_style_resource_has_property` (this document's original
provocation) is deleted, replaced by derivation.

Gates: anchors; `dom_style` + computed-style tests; full JS gtest; UI-automation.

Progress (2026-07-12): **Phase 3 complete** (done before Phase 2 — the style hosts are
independent of CSSOM and are the proving ground for the open-name hooks; Phase 2 remains
pending).

- **`js_dom_style_resource_has_property` — the function that provoked DOM3 — is deleted.**
  `in` on style objects is now derived: declared members answer from records, CSS property
  names from the new `named_has` hook (`js_style_css_has`: camel→css conversion + property
  table, no getter invoked). `js_dom_style_method`, the bridge's `radiant_dom_style_method`
  module dispatch, all eight `radiant_dom_style_host_*` ops, and their ops table are deleted;
  `inline_style`/`computed_style` now register with **`host_ops = NULL`** — the first radiant
  types running fully generic. The `style_resource_has_property`/`style_method` dom-API slots
  are NULLed (layout preserved).
- **Open-name hooks proven**: this is the first real use of `named_get`/`named_set`/
  `named_has`. CSS property reads/writes flow through three new receiver-explicit dom-API
  entries (`style_get_property`/`style_set_property` = the surviving engine implementations
  taking the owner element item; `style_css_has`); inline adapters unwrap the owner
  `DomElement*` from `host_data` and reuse the existing `style_set/remove_property_bridge`
  entries for `setProperty`/`removeProperty` (mutation-notify hooks unchanged, one write
  path). Computed-style adapters call the existing `computed_style_get_property` resolver;
  its `setProperty`/`removeProperty` are bound no-ops returning null (pinned).
- **Pinned quirks preserved by construction**, each mapped to a generic mechanism rather than
  special-cased: `Object.keys(el.style)` = `[]` (all members `JUBE_MEMBER_NON_ENUMERABLE`;
  expandos structurally impossible because `named_set` swallows every non-declared write into
  the CSS parser exactly as before); `.length` reads as `""` (bound getter routes the name
  through the CSS-table miss path — a pinned legacy wart, correct count deferred);
  **no prototype** (a `prototype_seed` returning a non-map records "none": no publication, no
  root, prototype op reports null); `style.__proto__` still reads `""` (named_get precedes the
  `__proto__` special-case in generic get order); `delete style.color` returns false (generic
  delete now consults `named_has` — projected non-configurable contract).
- Generic layer additions: `JubeTypeBinding.named_has`, `named_has` consult in
  `jube_member_has` and `jube_member_delete`, and non-map prototype seeds.
- Known intentional refinements (same class as D0d, watched in goldens, no diffs appeared):
  reading `computedStyle.setProperty` returns a function object instead of `""`;
  `getOwnPropertyDescriptor(style, "cssText")` returns a real descriptor instead of undefined.
- Verified gates: `make build` 0 errors; **`dom_style`, `dom_v12b`, `dom_module_props`,
  `hostobj_demo` goldens all diff-exact**; full JS gtest **309/309**; UI-automation
  **passed with only the 2 known webview skips**; direct `radiant_poc`/`radiant_poc_uaf`;
  `make test-lambda-baseline` exit 0. Net diff for the phase: +198/−219 lines.

### Phase 4 — Element, text/comment, document (the big one; one cluster per checkpoint)

- **4a** Constants, aliases, identity/navigation cluster (nodeName/nodeType/parentNode/…,
  the DOM1 "first cluster" again) — pure rows, no guards.

  Progress (2026-07-12): **4a complete** (suites recorded below). The migration mechanism is
  `JubeTypeBinding.legacy_ops` — a transitional marker meaning *fall through on record miss*:
  the generic dispatch returns unhandled and the caller's existing host_ops path runs, which
  preserves key-conversion (vmap snake→camel), text/comment kinds, side-table expandos,
  per-kind prototypes (Element/Node identity), own-keys, and live-collection semantics for
  everything unconverted. Two lessons paid for during bring-up: (1) delegation-with-the-raw-key
  is wrong — the Lambda path hands snake_case keys that only the caller's fallback converts,
  hence fall-through-not-delegate; (2) `radiant_dom_projected_own_value` had to become
  record-aware, since own-keys projection and descriptors filter by "does this name project"
  and the converted arms no longer answer. `dom_node` declares the 24-member identity/nav
  cluster (tag_name/node_name alias, local_name, namespace_uri with `js_name` override,
  prefix, id, class_name, node_type, parent_node/parent_element alias, is_connected,
  child_element_count, children, attributes, owner_document, first/last_child,
  next/previous_sibling, first/last_element_child, next/previous_element_sibling,
  child_nodes), every member guarded element-only (`radiant_dom_member_is_element`) so
  text/comment reads flow legacy; converted getters without converted setters (id,
  class_name) fall through on write so the legacy write chains keep working. 22 strcmp arms
  deleted from the bridge element chain; excluded from 4a on purpose: `length`/`elements`
  (form-gated, needs `applies_to`), `content` (template-gated), reflected attributes (4b),
  editable-state trio (4b/4c). Goldens diff-exact: dom_module_props, dom_identity, dom_style,
  dom_v12b, dom_jquery_lib, hostobj_demo, radiant_dom_read/mutate, proc_radiant_dom_set.
- **4b** Reflected attributes: the four `_is_*_reflected`/`_idl_to_attr_name` helper chains and
  the bridge's parallel copies become `REFLECT_*` rows (~40 props). Enumerated attributes
  (`inputMode`, `contentEditable`, …) stay ACCESSOR rows over their canonicalizers.

  Progress (2026-07-12): **4b complete on the bridge side** — 38 members generated from one
  spec (10 bools incl. the `readonly` alias and split-guard `multiple`/`size` rows, 7 ints
  with defaults, 13 strings incl. `htmlFor`/`acceptCharset`/`formTarget` overrides and the
  `wrap`→"soft" default, 2 keyword-canonicalized hints, `contentEditable` get+set with
  SyntaxError path, `isContentEditable`). The four bridge reflection predicate chains and all
  their get/set consumer arms are deleted; live-state hooks (`after_disabled_attribute_set`,
  `after_default_checked_set`, `after_default_selected_set`, `after_select_multiple_removed`)
  ride the converted setters unchanged. `dom_node` registers **62 members**. Deviations from
  the doc's original sketch, recorded: guards are module-side tag-set functions (not the
  declarative `applies_to` column — keeps DOM knowledge out of the generic layer, same
  layering rule that evicted `setAttribute` from vmap.cpp) and reflection handlers are
  macro-generated module code (not the generic `reflect_attr` routine — revisit when a second
  module needs reflection). The engine's parallel `_is_*_reflected` chains in js_dom.cpp
  remain (shadowed for dispatch, reachable by internal callers) — they go in the 4e engine
  sweep. Suites: recorded below with 4c.

  Continuation plan for 4c–4e (precise state for the next checkpoint): **4c** set-side arms
  (checked, select value/selectedIndex/length, option selected/value/text, text-control
  value/selection*/defaultValue, non-tc input value) have bridge-local bodies read and ready
  to convert; get-side rows bind thin adapters onto `dom->dom_get_property_impl` (the
  select/option get chain already just delegates there — behavior unchanged, dispatch
  converted), with `radiant_dom_get_select_option_property` deleted once its names are rows.
  **4d** methods: split `radiant_dom_element_method_basic` arms (59 names, mostly one-line
  `*_bridge` calls) into call handlers; ITEM_TRUE alignment happens engine-side afterwards.
  **4e** catch-alls (form named getter, attribute fallback, on*, collections) become
  named/indexed hooks; then the engine-chain sweep and the final legacy_ops → NULL flip with
  document/foreign_document types converted last.
- **4c** Tag-gated form-control cluster (`value`, `type`, `checked`, `selected`,
  `selectedIndex`, size/rows/cols, …) via `applies_to` + guards — the 90 `_is_tag` dispatch
  sites become data; behavior bodies unchanged.

  Progress (2026-07-12): **4c complete** — 19 guarded rows over 16 declared members
  (`value` alone carries four same-name guard variants: select / text-control /
  non-text-control input / option, each with its distinct live setter and hooks —
  `set_checked_dirty`, `select_set_value/selected_index/length`,
  `set_option_selected_dirty/text`, the five text-control selection/default bridges, and the
  non-tc input attribute+form-value write). Get side binds thin adapters onto
  `dom->dom_get_property_impl` — the deleted `radiant_dom_get_select_option_property` chain
  already just delegated there, so behavior is unchanged while dispatch converts; body
  extraction is 4e's engine sweep. The whole 4c set-arm block and the select/option get
  chain are deleted from the bridge. `dom_node` registers **78 members**; bridge strcmp
  is down to **176** (from 279 pre-DOM3; the survivors are the element method chain — 4d —
  plus data-value compares). Suites recorded with 4b: full JS gtest 309/309, UI-automation
  clean, Lambda baseline green, all nine focused goldens diff-exact.
- **4d** Methods: element (58), document (40), classList, Location, DOMImplementation — small
  types get their own `JubeTypeDef` + table where they are host-branded, or stay stamped objects
  where they are ordinary maps today. `dom_methods[]`/`doc_methods[]` deleted; feature-detect
  behavior aligned per §2.6 (golden updates isolated to this sub-step).

  Progress (2026-07-12): **4d node methods complete** — 64 method rows over the union of the
  bridge and engine method-name inventories (select's `namedItem`/`add`/`remove` override rows
  declared before the generic node `remove` — the ordering-dependent overload as visible
  declaration order; 5 CharacterData methods text-guarded; 6 all-node methods node-guarded;
  the rest element-guarded; `insertAdjacentHTML`/`__lambda*` internals via `js_name`). Call
  handlers delegate into `radiant_dom_element_method` (dispatch converted, arms keep exact
  behavior; extraction = 4e). This lands **D0d incrementally**: method-name property reads now
  return real function objects. The earlier **`get_attribute`** deferral was resolved in the
  4e opening engine fix below; the row now converts with the rest of the node methods, and
  jQuery/Sizzle's `support.attributes` path remains golden-stable once record-native `.call`
  dispatch is correct. `dom_node` registers **142 members**. Document/classList/Location/
  DOMImplementation methods remain legacy (document types not yet declared). All nine focused
  goldens diff-exact; suites recorded below.
- **4e** Catch-alls: form named getter, attribute fallback, `on*` handlers, dataset →
  `named_get/named_set`; live collections → `indexed_/named_` hooks with refresh inside;
  delete the residual direct `js_dom_*` calls from `js_runtime.cpp`/`js_globals.cpp`/
  `js_runtime_value.cpp`.

  Progress (2026-07-12, 4e opening fix): the `get_attribute` alignment was attempted first
  and **root-caused to an engine bug, not a getAttribute defect**. Findings, fully bisected
  (repro: `temp/jq_find_repro.js`): our `getAttribute` semantics are correct (Sizzle's probe
  expectations verified value-by-value). Enabling the row flips Sizzle's `support.attributes`
  (its probe relies on `true("className")` *throwing*), which reroutes jQuery into paths
  where the real breakage lived: after Sizzle's context-scoped `find()` executed,
  `Function.prototype.call`/`apply` on record env-closure natives returned null/undefined.
  The first instrumentation showed this happened before the trampoline/env branch; the final
  pointer trace showed why: Jube methods shared per-arity trampolines, and `js_new_function`
  cached wrappers by `func_ptr`, so later records mutated the same `JsFunction` wrapper's
  closure env/name (`matches` became `getElementsByClassName` after Sizzle). The fix routes
  Jube member methods through uncached `js_new_method_function`, preserving one wrapper/env
  per record. Repro now stays true across direct, saved, and member `.call` paths after
  `$app.find(".item")`; `get_attribute` is restored in the radiant interface and binding row.
  Verification for this slice: `make release` green; `dom_jquery_lib` direct diff-exact;
  focused `dom_jquery_lib` GTest green; full `test_js_gtest` green 309/309.

  Progress (2026-07-12, 4e named get/set slice): `dom_node` now wires Jube
  `named_get`/`named_set` to the existing Radiant DOM bridge, so open-name DOM reads/writes
  enter through the record path before the transitional `legacy_ops` fallback. This keeps
  the large legacy property implementation as the semantic source while moving the dispatch
  boundary one step toward Phase 4e. A descriptor regression during the slice exposed the
  boundary invariant: host own-property descriptors need declared record/basic DOM members,
  not full property reads after named-hook/prototype/expando fallback. `jube_member_projected_get`
  captures that declared-member-only probe for the host descriptor/delete path. Verification:
  `make build` green; `temp/jq_find_repro.js` still prints `POST: true`;
  `dom_jquery_lib`, `dom_v12b`, `dom_identity`, and `dom_module_props` focused GTests green;
  full `test_js_gtest` green 309/309; UI-automation green with 233 selected tests passed
  and two native-webview tests skipped.

  Progress (2026-07-12, Phase 4e completion): **4e complete**. The opening engine fix
  landed first, then the final record-hook conversion removed the remaining active
  `legacy_ops` dependency from the DOM/document family:
  - `dom_node` registers with `legacy_ops = NULL`. Its residual call/has/delete/
    descriptor/ownKeys/prototype behavior is now reached through record-owned
    `object_*` hooks, while `named_get`/`named_set` remain the open-name DOM entry points
    for form named getter, attribute fallback, `on*`, and dataset behavior.
  - `document` and `foreign_document` are declared in the module interface, have
    `host_ops = NULL`, and have record binding rows. Their object hooks delegate to the
    existing `radiant_dom_document_host_*` bridge so active-document replacement behavior
    stays centralized.
  - `dom_methods[]`, `doc_methods[]`, and `form_idl_props[]` were deleted from
    `js_dom.cpp`; the remaining name checks are predicate helpers or semantic/data-value
    compares, not duplicated dispatch arrays.
  - Runtime direct DOM helper calls were swept: live collection/options refresh now goes
    through the neutral `js_array_exotic_before_property_*` hooks, strict equality uses
    `jube_host_identity`, and `document.implementation` no longer has a direct
    `js_runtime.cpp` call branch.
  - The post-flip setter regression was root-caused and fixed at the Jube boundary:
    declared getter-only DOM rows now give the record-owned `named_set` hook a chance to
    handle reflected writes such as `id`/`className`, preserving live class-name collection
    behavior after `legacy_ops` removal.
  - DOMImplementation remains an ordinary map/sentinel object with real function slots;
    calls go through normal property-call fallback, so the Phase 4e engine switchboard is
    gone without inventing a host binding for a surface that is not currently host-branded.
  - `Location` is modeled through the document proxy path in this tree, and `classList` is
    not currently exposed as an active property path, so neither left an active Phase 4e
    runtime switchboard after the sweep.

  Phase-end verification: `make build` green; `make build-test` green; release build green;
  direct `temp/jq_find_repro.js` prints `PRE: true`, `LEN: 3`, `POST: true` in both debug
  and release; direct `dom_module_props` golden diff is clean; focused
  `dom_jquery_lib`/`dom_v12b`/`dom_identity`/`dom_module_props` GTests are 4/4; full
  `test_js_gtest` is 309/309; UI-automation reports 233 passed with two native-webview
  skips. Grep gates are clean for `dom_methods[]`, `doc_methods[]`, `form_idl_props[]`,
  and for the old runtime direct DOM hooks in `js_runtime.cpp`/`js_runtime_value.cpp`.

Gates per sub-step: anchors + full JS gtest + `dom_jquery_lib`/`dom_v12b`/`dom_identity` direct
diffs; 4c/4e additionally UI-automation + `make test-radiant-baseline` vs the DOM2 Phase-0
triage table; phase-end grep gates: `dom_methods\[\]|doc_methods\[\]|form_idl_props\[\]` → 0 hits; strcmp budget for js_dom.cpp ≤ ~200 (data compares), bridge dispatch chains deleted.

### Phase 5 — Lambda projection convergence

Delete `snake_to_camel_key`/`camel_to_snake_key`/`vmap_host_set_attribute` from `vmap.cpp`
(dual-keyed index + `named_set` replace them); Lambda `keys` iteration enumerates
guard-filtered table entries with precomputed snake names; extend
`radiant_dom_read/mutate/set` `.ls` tests to cover the widened surface; cross-front-end
coherence re-pinned.

Progress (2026-07-12, projection-convergence slice): VMap no longer owns DOM-specific
string rewrites or dashed-attribute writes. `vmap_keys_for_item()` now asks Jube for
guard-filtered projection keys, `jube_member_projection_keys()` emits declared snake-case
field rows plus expandos, and DOM named setters own attr-name writes before JS property
fallback. `document`/`foreign_document` now inherit a declared document surface with
bound getters/methods, shared Node fields are record-owned for text/comment wrappers, and
getter-only reflected rows pass the canonical JS name into `named_set` after the VMap
fallback removal. Jube method property reads outside JS runtime now allocate a Lambda
closure capturing receiver + method name instead of calling `js_new_method_function`
without a `js_input` pool. The strengthened `radiant_dom_read` golden covers document
projection keys, text-node `data`/`text_content`, and `document.query_selector()`;
procedural `radiant_dom_set` covers `id`, `class_name`, and dashed `data-phase` writes.

Gates run for this slice: `make build`; `radiant_dom_read` diff-exact; `radiant_dom_mutate`;
procedural `radiant_dom_set`; `make test-lambda-baseline` (3299/3299); UI automation
(233 passed, 2 native-webview skips); `make release`; release wrap-sweep benchmark on
the 4,255-element / 4,602,200-hop fixture (`query_ms=9/7/8`, `walk_ms=901/773/807`,
versus the DOM2 Phase-6 checkpoint `query_ms=3/3/3`, `walk_ms=3119/3092/3252`).

Phase 5 exit gates are satisfied: anchors, `make test-lambda-baseline`, and the DOM2
Phase-6 release-build wrap-sweep benchmark re-run. Property-walk time improved on the
same 4,602,200-hop shape after §2.4 removed per-hop transforms and chains.

### Phase 6 (stretch) — measurement and the deferred fast paths

Release-build microbenchmark of hot property access (`parentNode` walk, `value` read on
text controls, computed-style read) before/after; record numbers in this doc. Explicitly
**deferred**: binary signature blobs (D0b), interned-pointer key fast path (needs pooled keys at
the JS atom layer — worth doing when measurements say so), per-site inline caches keyed on
(typedef, interned name).

### Sequencing and exit

```
Phase 0 ─→ Phase 1 ─→ Phase 2 ─→ Phase 3 ─→ Phase 4a…4e ─→ Phase 5 ─→ (6)
infra +     range/     CSSOM      style      element/doc     Lambda      perf
demo proof  selection             hosts      clusters        convergence
```

**DOM3 exit criteria**: every branded DOM type dispatches through its declared interface +
binding table + hooks (`host_ops` NULL or trivially thin); zero hand-maintained name lists
(`*_is_native_property`, `dom_methods[]`, `doc_methods[]`, bridge own-key arrays, reflection
predicate chains — all grep-gated to 0); strcmp in DOM dispatch reduced to data-value
comparisons (budget table recorded); `vmap.cpp` free of DOM-specific knowledge; generic engine
files free of direct `js_dom_*` behavior calls; `hostobj_demo` proves the whole protocol from
pure data; all DOM1/DOM2 anchors and baselines green against the standing triage table.

After DOM3, `Lambda_Design_DOM_Pkg.md` gets its L4 adapter for free: "route getter X of
prototype Y to Lambda function F" is a one-row change to a table that already exists, and the
migration of behavior into Lambda script can proceed API-by-API without ever touching dispatch
again.

---

## Appendix — scan evidence index (2026-07-11)

| Fact | Evidence |
|---|---|
| 708 strcmp in js_dom.cpp; 147-name element getter | `js_dom_get_property_impl` js_dom.cpp:8915; set :10477; methods :12533 |
| Document chains + `doc_methods[]` | :5689, :6225, :6499 |
| Feature-detect + skip lists | `dom_methods[]` :10344 (55), `form_idl_props[]` :10191 (16) |
| Reflection helper chains | `_idl_to_attr_name` :8686, `_is_bool/int/string_reflected` :8739/:8769/:8805 |
| Tag-gating density | 90 `_is_tag` sites (:1365), 30 `tc_is_text_control_elem`, 37 input-`itype` compares |
| Catch-alls | form named getter :10188; attr fallback :10247/:10979; `on*` :1283; collection refresh :7557 |
| Range/selection ×4 duplication | `range_is_native_property` js_dom_selection.cpp:259; getters :940/:1526; binders :912/:1504; bridge keys radiant_dom_bridge.cpp:2616–2625 |
| CSSOM has/get drift | `js_cssom_resource_has_property` js_cssom.cpp:359 vs getters :731/:889/:1067 |
| Bridge chains + own-key arrays | radiant_dom_bridge.cpp:1436/:1624/:1888; :2612–2657 |
| Transient keys on host path | `string_key_item` → `heap_strcpy` vmap.cpp:52; `radiant_dom_key_equals` radiant_dom_bridge.cpp:2448 |
| Case transforms + setAttribute in generic layer | vmap.cpp:56/:74/:141 |
| Linear type lookup per access | `jube_find_type_by_host_type` jube_registry.cpp:578 |
| NamePool canonical-pointer guarantee | name_pool.cpp:79–126; String has len+is_ascii, no hash (lambda.h:596) |
| CSS table precedent | `property_definitions[]` + djb2 table css_properties.cpp:400–697, lookup :734 |
| Weak signature parser, can_raise=false | build_ast.cpp:156–266, :317 |
| Events = ordinary objects (out of scope) | js_dom_events.cpp — no property-name dispatch found |
