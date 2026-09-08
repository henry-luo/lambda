# Lambda Virtual Container Types: VMap, VArray, and Velmt

> **Status:** PARTIALLY IMPLEMENTED (2026-09-08). The distinct VArray/Velmt
> carriers, common host metadata, Jube carrier selection, Radiant pilot, DOM
> Node-family Velmt migration, and primary DOM collection VArrays are landed.
> Specialized File/DataTransfer and SVG-transform collection backends, generic
> VArray splice/materialization tests, and the full GC/WPT exit matrix remain
> open, so **D7.4.5v2*** remains marked partially implemented.
> **Scope:** the Lambda value model, runtime dispatch, Jube carrier selection,
> and migration of DOM nodes and DOM collection values. JavaScript prototype
> behavior remains in the JavaScript/Jube adapter.
> **Formal anchors:** **S2.1.1v3** (array/map/element semantic kinds),
> **S5.3.1** (sequence equality), **S5.4.1–S5.4.3** (map and element
> equality), **S9.2.2–S9.2.3** (Lambda-owned read views and entry-time
> iteration),
> **S12.1.3** (mutation boundary), **D1.3** (shared runtime without shared
> language semantics), **D2.3.2** (safe container decoding), **D2.6.6v2**
> (map → array → element container taxonomy), **D2.6.8** (identity is data,
> not an address), **D5.2.1v3** (precise GC/rooting), and
> **D7.4.1v2/D7.4.4/D7.4.5v2** (Item boundary, record-owned host protocol, and
> three virtual carriers).
> **Design record:** extends D4l from
> [`Lambda_Jube_DOM4.md`](Lambda_Jube_DOM4.md). Refinements are numbered
> D4l.1–D4l.10; the formal authority is **D7.4.5v2***, whose implementation mark
> remains until the exit gates below close.

---

## 0. Outcome

Lambda should have three internal virtual container representations:

| Virtual carrier | Public semantic kind | Structural faces | First major host use |
|---|---|---|---|
| `VMap` | `map` | ordered key/value map | ordinary Jube host objects |
| `VArray` | `array` | indexed sequence | DOM collections and custom-layout child lists |
| `Velmt` | `element` | tag + attribute map + ordered children | DOM nodes and Radiant custom-layout handles |

The virtual name is a storage/dispatch fact, not a new Lambda-visible type.
`type(vmap)`, `type(varray)`, and `type(velmt)` return `"map"`, `"array"`, and
`"element"` respectively. `is map`, `is array`, `is element`, equality,
iteration, validation, and formatting use the public semantic kind. This is
representation transparency, as required by **S2.1.1v3**, **S5.3.1**, and
**S5.4.1–S5.4.3**.

Two different concepts currently share the name `Velmt`. The native Radiant
layout snapshot is renamed `RadiantVelmt`; the new Lambda runtime carrier owns
the unqualified `Velmt` name:

```text
RadiantVelmt  = pass-local Radiant measurement/placement snapshot
Velmt         = Lambda virtual element carrier
```

The first Velmt backend wraps a `RadiantVelmt`. Later backends may wrap a DOM
node, a binary-schema view, or another host element without changing Lambda's
element operations.

## 1. Problem

The existing `VMap` solved map-shaped host projection, but it became the only
host carrier. That flattens two different shapes:

1. Array-shaped DOM values are materialized as real `Array`s and decorated
   with companion-map properties. Live values are kept fresh through four
   fixed 4096-entry registry families and mutation-time refresh walks in
   `lambda/dom/dom.cpp`.
2. Element-shaped values are exposed as branded VMaps. Radiant custom layout
   copies a native layout snapshot into `VMap::host_data`, then projects
   `tag`, `attrs`, `children`, and box fields through named map lookup. DOM
   nodes likewise use branded VMaps even though their natural Lambda shape is
   tag + attributes + children.

The first arrangement copies on read and sweeps on write. The second loses the
element's structural identity and makes generic Lambda element operations
unavailable. Both are the mismatch identified by **D7.4.5v2**.

The fix is not three names for one object shell. Each carrier must implement
the structural face of the semantic kind it represents, while the declared
Jube interface continues to provide nominal members and methods under
**D7.4.4**.

## 2. Proposed decisions

### D4l.1 — Virtuality is representation, not a fourth container taxonomy

The semantic kinds remain map, array, and element. Internal TypeIds distinguish
the virtual layouts so GC and native dispatch can safely decode their headers:

```text
LMD_TYPE_VMAP    -> semantic map
LMD_TYPE_VARRAY  -> semantic array
LMD_TYPE_VELMT   -> semantic element
```

The TypeId ABI groups the full sequence family contiguously and places each new
virtual carrier beside its materialized counterpart:

```text
LMD_TYPE_RANGE <= type_id <= LMD_TYPE_VARRAY
LMD_TYPE_VARRAY = LMD_TYPE_ARRAY + 1
LMD_TYPE_VELMT  = LMD_TYPE_ELEMENT + 1
LMD_TYPE_COUNT  = LMD_TYPE_ERROR + 1

is_map_family_type_id     = MAP | VMAP | ELEMENT | VELMT
is_array_family_type_id   = RANGE ... VARRAY
is_element_family_type_id = ELEMENT | VELMT
```

`is_array_family_type_id` is therefore one inclusive range check. The broader
container band is `RANGE ... VELMT`, and Error is the last valid TypeId. These
ordering invariants are compile-time assertions. Every physical decode must
still use an exact representation test: VArray is not layout-compatible with
Array, and Velmt is not layout-compatible with Element. A raw `Item` cast based
only on semantic family remains forbidden by **D2.3.2**.

`D2.6.6v2` continues to define the packed layout of materialized containers.
Virtual carriers are alternate representations of those same three semantic
kinds; they do not pretend to inherit the packed `Map`/`Array`/`Element`
storage bodies.

### D4l.2 — One common virtual header, three structural vtables

All three carriers share one prefix so GC, Jube branding, invalidation, and
native payload access have one implementation. The type-specific vtable owns
the structural operations:

```cpp
typedef enum VirtualOpStatus {
    VIRTUAL_OP_OK,
    VIRTUAL_OP_MISSING,
    VIRTUAL_OP_READONLY,
    VIRTUAL_OP_ERROR
} VirtualOpStatus;

typedef struct VirtualVtable {
    uint32_t abi_version;
    TypeId carrier_type;
    uint8_t reserved[3];
    void (*destroy)(void* data);
    void (*trace)(void* data, gc_heap* gc);
    uint64_t (*version)(void* data);
} VirtualVtable;

typedef struct VirtualContainer : Container {
    void* data;
    const VirtualVtable* vtable;
    const void* host_type;
    void* host_data;
    Item expando;
} VirtualContainer;

struct VMap : VirtualContainer {};
struct VArray : VirtualContainer {};
struct Velmt : VirtualContainer {};
```

This preserves the useful shape of today's `VMap` header without copying its
metadata policy into three unrelated implementations. VMap retains its legacy
operation-table ABI during this migration, but its carrier header is
offset-compatible with `VirtualContainer`; VArray and Velmt use the structural
vtables above. Static assertions pin the common
offsets in the C and C++ mirrors. The core owns allocation and metadata;
modules receive constructors/accessors and never write the header directly.

The status result separates an absent value from a present `null`, and a
read-only operation from an exception. `ItemNull` alone cannot express those
four outcomes. `VIRTUAL_OP_ERROR` places the explicit error Item in `out`, in
line with **D1.4v3** and **D8.4.3v2**.

### D4l.3 — Lifecycle is precise and centralized

Every virtual carrier is GC-managed. Its `trace` callback marks every retained
Item edge; `destroy` releases only native/backend storage and runs once when
the carrier is swept. Raw host pointers are never conservatively scanned.

If a backend retains a DOM node or another arena-owned object, it must use that
owner's generation/pin protocol. Teardown neuters the common `host_data` slot
through one `virtual_host_invalidate()` helper. Reads from a neutered host
return the door's missing/undefined result without dereferencing freed native
state. This generalizes the existing DOM VMap husk protocol and follows
**D5.2.1v3** and **D2.6.8**.

### D4l.4 — VMap is the virtual form of the complete map contract

The current VMap remains the first carrier. The following is the target
structural contract for a later compatibility-neutral vtable revision:

```cpp
typedef struct VMapOps {
    VirtualOpStatus (*get)(void* data, Item key, Item* out);
    VirtualOpStatus (*set)(void* data, Item key, Item value, Item* out);
    VirtualOpStatus (*has)(void* data, Item key, bool* out);
    VirtualOpStatus (*remove)(void* data, Item key, bool* out);
    int64_t (*count)(void* data);
    VirtualOpStatus (*key_at)(void* data, int64_t index, Item* out);
    VirtualOpStatus (*value_at)(void* data, int64_t index, Item* out);
} VMapOps;

typedef struct VMapVtable {
    VirtualVtable common;
    VMapOps map;
} VMapVtable;
```

Keys retain insertion order as required by **S2.3.1**. Equality remains
key-unordered under **S5.4.1**. A backend may be immutable; absent write hooks
or `VIRTUAL_OP_READONLY` produce a checked write failure instead of silently
adding an unrelated side map.

The generic hashmap-backed VMap continues to support arbitrary Item keys.
Host expandos are not part of the VMap structural vtable: they remain an
adapter policy implemented by the declared Jube record and its backing map.

The landed implementation deliberately does not replace VMap's released
callback layout in the same ABI step that adds VArray and Velmt. Existing VMap
get/set/count/key callbacks remain authoritative; the common host metadata and
carrier-neutral Jube paths are implemented now. The status-bearing VMapOps
upgrade remains follow-up work.

### D4l.5 — VArray is the virtual form of an indexed sequence

VArray exposes the complete structural operations needed by Lambda array
reads, iteration, and procedural mutation:

```cpp
typedef struct VArrayOps {
    int64_t (*count)(void* data);
    VirtualOpStatus (*get_at)(void* data, int64_t index, Item* out);
    VirtualOpStatus (*set_at)(void* data, int64_t index, Item value, Item* out);
    VirtualOpStatus (*splice)(void* data, int64_t start, int64_t remove_count,
                              const Item* values, int64_t value_count,
                              Item* out);
} VArrayOps;

typedef struct VArrayVtable {
    VirtualVtable common;
    VArrayOps items;
} VArrayVtable;
```

`count` backs `len`; `get_at` backs indexing and iteration; `set_at` backs a
procedural indexed write; `splice` is the one mutation primitive from which
append, insert, remove, resize, and replacement can be derived. Read-only DOM
collections omit the mutation hooks. `HTMLOptionsCollection.add/remove` stays
a declared DOM method and applies DOM validity rules; it does not bypass them
through a raw VArray splice.

VArray iteration is ordered and participates in **S5.3.1** sequence equality.
It is not a JavaScript Array merely because Lambda sees it as an array:
`Array.isArray(NodeList)` remains false. Guest type predicates use guest
nominal brands under **D1.3**; Lambda structural predicates use the semantic
carrier family.

### D4l.6 — Velmt virtualizes both Element faces and the tag

Velmt composes the VMap attribute face and VArray child face, then adds element
identity fields:

```cpp
typedef struct VelmtOps {
    VirtualOpStatus (*tag)(void* data, Item* out);
    VirtualOpStatus (*namespace_uri)(void* data, Item* out);
    VMapOps attrs;
    VArrayOps children;
} VelmtOps;

typedef struct VelmtVtable {
    VirtualVtable common;
    VelmtOps element;
} VelmtVtable;
```

The map face means element attributes, not every host-language object
property. The array face means ordered child content. The tag and namespace
complete **S5.4.3** element equality:

```text
Velmt equality = tag + namespace + unordered attributes + ordered children
```

The runtime routes generic element operations through these faces:

- `len(velmt)`, numeric indexing, and iteration use `children`;
- member/index lookup for an attribute uses `attrs` after declared type/member
  resolution;
- `name(velmt)` and formatters use `tag` and `namespace_uri`;
- `keys(velmt)` enumerates attributes in backend order;
- validation and pattern matching see the semantic element kind.

A backend may expose child mutation only when its contract can preserve the
host's invariants. DOM Velmt does not expose raw `set_at`/`splice`; DOM tree
mutation continues through checked `dom.*` operations. Radiant custom-layout
Velmt is entirely read-only and pass-scoped.

### D4l.7 — Declared Jube interfaces are carrier-neutral

`JubeTypeDef` gains a carrier selector:

```cpp
typedef enum JubeCarrierKind {
    JUBE_CARRIER_VMAP,
    JUBE_CARRIER_VARRAY,
    JUBE_CARRIER_VELMT
} JubeCarrierKind;
```

The host factory allocates the selected carrier and attaches `host_type` and
`host_data`. Jube code obtains those fields through common helpers rather than
`receiver.vmap`. The following paths become carrier-neutral together:

- type-record lookup and family-brand checks;
- generic and by-ordinal member get/set/call;
- named and indexed hooks;
- has/delete/descriptor/own-key/prototype hooks;
- expando lookup;
- native payload access and invalidation;
- native owning-type destruction.

`JubeTypeBinding` remains the only named-member protocol under **D7.4.4**.
The VArray/Velmt structural vtables do not reintroduce the retired
`JubeHostObjectOps` fallback. Resolution order is:

```text
declared member/ordinal
-> declared indexed or named hook
-> carrier structural face
-> expando/prototype policy of the active language door
```

For Lambda element access, a Velmt attribute is the structural fallback. For
JavaScript DOM access, WebIDL declared/named/indexed rules remain authoritative
before markup attributes or expandos.

Adding either new TypeId and the carrier selector changes the native Item/Jube
ABI. The implementation must land the C/C++ mirrors and module-facing accessors
atomically, bump the relevant runtime and `JUBE_ABI_VERSION` values, and reject
modules compiled against the older layout. It must not infer the carrier from
header size or silently fall back to VMap.

The **D7.4.5v2** ABI intentionally renumbers the container-and-later band so the
classification order is structural: `RANGE=16`, `ARRAY_NUM=17`, `ARRAY=18`,
`VARRAY=19`, `MAP=20`, `VMAP=21`, `ELEMENT=22`, `VELMT=23`, `TYPE=24`,
`FUNC=25`, `ANY=26`, and `ERROR=27`. `LMD_TYPE_ERROR` is the last valid type;
`LMD_TYPE_COUNT=28` is only the exclusive bound. `JUBE_ABI_VERSION = 6` rejects
modules compiled against the previous tag layout. The two MIR checks that pin
the Error tag use 27. Exact physical guards remain mandatory under
**D2.3.2/D7.4.5v2** even though family classification can use the new ranges.

### D4l.8 — Lambda reads the native DOM under a fixed-DOM execution contract

DOM-backed VArray and Velmt do not implement `snapshot_for_lambda` and do not
copy collection membership at the Lambda boundary. This follows **D7.4.5v2**:
host collections are live by reading, not materialized copies kept coherent by
refresh sweeps.

Instead, Lambda execution establishes a fixed-DOM interval. For the duration of
that run, the DOM graph, node generations, attributes, child order, and
collection membership are held static. This is a scheduler/execution contract,
not a carrier hook: a single-threaded, non-reentrant implementation needs no
epoch object or lock. VArray `count`/`get_at` and Velmt tag/attribute/child
operations read the underlying native DOM directly.

| Door | Collection policy |
|---|---|
| Lambda | Direct native reads during the fixed-DOM run; no boundary snapshot or membership copy. |
| JavaScript | Live NodeList/HTMLCollection backends read current DOM state. A static NodeList retains its creation-time membership. |
| Cross-language handoff into Lambda | Reuse the same carrier/backend; the Lambda execution contract holds its DOM fixed. Do not convert or copy it. |

All mutation sources must respect the fixed interval. Script/event/loader work
that would mutate the DOM is deferred until the Lambda run exits. A Lambda-facing
mutation operation attempted inside this read-only run returns a checked
error rather than changing the tree underneath a running Lambda expression.
Document teardown likewise waits for the run to exit.

This is compatible with **S9.2.2** because a DOM carrier is host state, not a
read view cut from a Lambda-owned container; Lambda-owned slices still have
snapshot semantics. It is compatible with **S9.2.3** because the DOM source
cannot mutate while a Lambda loop or pipe is walking it. Generic mutable
VArrays still use Lambda's entry-time iteration mechanism when their source can
be written by the loop.

A retained DOM carrier may observe newer native state in a later Lambda run.
Within any one run it is stable. `version()` supports JavaScript iterators and
inline caches across host mutations; it is not observable identity and does
not participate in equality.

### D4l.9 — Identity is independent of the carrier address

Wrapper caching may preserve host wrapper reuse, but neither the GC address nor
`host_data` pointer becomes Lambda identity. Lambda `===` remains governed by
**S5.1.4v2/D2.6.8** and DO25. DOM `isSameNode` and JavaScript strict identity
continue to use their host-language contracts. VArray/Velmt implementation
must not pre-empt the universal node-identity ruling.

### D4l.10 — Public construction stays host-controlled initially

Phase 1 adds no Lambda syntax for constructing arbitrary VMaps, VArrays, or
Velmts. Scripts construct ordinary maps, arrays, and elements. Core and Jube
host factories construct virtual carriers. This keeps backend lifetime,
mutation, and tracing contracts out of user code while making the virtual
representation transparent once a value crosses the Item boundary.

## 3. Runtime integration surface

Adding two TypeIds is not sufficient. Every operation that currently switches
on `LMD_TYPE_ARRAY`, `LMD_TYPE_MAP`, `LMD_TYPE_ELEMENT`, or `LMD_TYPE_VMAP`
must be classified as semantic or physical.

| Runtime concern | Required virtual behavior |
|---|---|
| Type reporting and matching | collapse VMap/VArray/Velmt to map/array/element; nominal host brand remains orthogonal |
| `fn_len` | map count, array length, element child count |
| `fn_index`, `item_at`, `item_attr` | dispatch through the matching structural face |
| `fn_index_set` and procedural collection operations | call optional write/splice hooks; checked failure for read-only values |
| `item_keys` | VMap keys or Velmt attributes; VArray uses numeric iteration keys |
| `iter_len`, `iter_key_at`, `iter_val_at` | iterate virtual contents without layout casts; DOM reads rely on the fixed-DOM contract, while mutable Lambda sources retain S9.2.3 entry-time behavior |
| `fn_eq`, hashing, total order | compare by semantic kind and content under S5.3/S5.4, never by virtual pointer |
| Printing and formatters | consume generic map/array/element accessors; no materialization requirement |
| Validation and type inference | accept virtual values wherever the corresponding structural type is accepted |
| Query/vector operations | generic fallback consumes virtual accessors; packed-array fast paths remain exact-type only |
| COW and borrowing | Lambda-owned read views retain COW/snapshot rules; DOM carriers read directly under the fixed-DOM contract; write hooks require a procedural borrow/capability |
| MIR specialization | exact packed offsets only after an exact materialized-kind guard; otherwise call shared helpers |
| GC mark/compact/sweep | mark carrier, call precise `trace`, never scan host memory, call `destroy` once |
| Copy/materialization | explicit `materialize()` helper builds ordinary containers when an API truly needs owned packed storage |

The first implementation should introduce semantic helper functions such as
`item_semantic_type_id`, `virtual_container_header`, `virtual_host_type`, and
`virtual_host_data`. This prevents dozens of repeated `VMAP || VARRAY ||
VELMT` switches and follows the no-duplicate-code rule.

## 4. DOM migration

### 4.1 Values that migrate to Velmt

The carrier switch is based on DOM node shape, not on which module produced
the wrapper. All wrappers for the DOM Node family should migrate together:

| Current host value | New carrier | Structural projection |
|---|---|---|
| `dom_node` and `html_element` | Velmt | node tag/name, attributes, childNodes |
| `character_data` | Velmt | `#text`/`#comment` tag, empty attribute face, empty child face; data stays a declared member |
| `svg_element` | Velmt | namespace-aware tag, attributes, children |
| `input_element`, `select_element`, `textarea_element`, `option_element` | Velmt | same element faces; form-control state stays in declared members/engine state |
| `document`, `foreign_document` | Velmt | `#document` tag, no attributes, ordered document children |
| document fragments and comments returned through `dom_node` | Velmt | `#document-fragment`/`#comment` tag and their appropriate child face |
| Radiant custom-layout parent/child handles | Velmt | read-only `RadiantVelmt` backend with layout fields as declared members |

The migration begins at the wrapper factories and caches, especially
`radiant_dom_wrap_node`, `dom_wrap_element`, document wrappers, custom-layout
argument construction, unwrap/type checks, husk invalidation, and event-target
conversion. One cached DOM node must still yield one wrapper per realm/runtime;
only the carrier changes. This makes the inventory exhaustive by value shape:
every current or future path through the shared node wrap/unwrap boundary moves
with the factory, rather than requiring a second per-operation carrier switch.

All node-valued DOM operations then return/accept Velmt without individual
special cases. The audit set is:

- navigation: `parentNode`, `parentElement`, `ownerDocument`, `firstChild`,
  `lastChild`, `nextSibling`, `previousSibling`, their element-only variants,
  `getRootNode`, `closest`;
- document lookup: `documentElement`, `body`, `head`, `doctype`,
  `activeElement`, `elementFromPoint`, `getElementById`, `querySelector`;
- node creation/copy: `createElement`, `createElementNS`, `createTextNode`,
  `createDocumentFragment`, `createComment`, `createProcessingInstruction`,
  `importNode`, `adoptNode`, `cloneNode`;
- mutation results/arguments: `appendChild`, `removeChild`, `insertBefore`,
  `replaceChild`, `replaceWith`, `before`, `after`, `insertAdjacentElement`,
  `append`, `prepend`;
- range/selection/event links: Range boundary containers, Selection
  anchor/focus/base/extent nodes, Event `target`/`currentTarget`/`srcElement`,
  `relatedTarget`/`submitter`, and the node entries returned by
  `composedPath()`;
- host records and traversal objects: MutationRecord `target`/sibling fields,
  observer-entry `target`, TreeWalker `root`/`currentNode` and traversal results,
  form-control owner/link fields, `offsetParent`, and any other field declared
  as Node or Element;
- Lambda `dom.*` operations whose argument or result contract is `dom_node`;
- Radiant layout, state, event, editing, form, and rendering operations that
  unwrap a DOM node Item.

Range, Selection, Event, Window, Location, History, CSSStyleDeclaration,
CSSStyleSheet, CSSRule, DOMRect, ValidityState, and other object/map-shaped
records remain VMaps. Node-valued fields inside them return Velmt.

### 4.2 Values that migrate to VArray

Every DOM/WebIDL collection object should use VArray; ordinary WebIDL
`sequence<T>` return values should remain ordinary Arrays. This prevents
“array-like object” from becoming an ad-hoc decorated Array again. “Live/fixed”
below means live direct reads in JavaScript and direct reads of the same native
backend while Lambda's fixed-DOM execution contract is active.

| Producer/property | WebIDL shape | VArray policy |
|---|---|---|
| `Node.childNodes`, including `Document.childNodes` | NodeList | live/fixed direct backend |
| `Element.children` | HTMLCollection | live/fixed direct backend |
| Lambda `dom.child_nodes` and `dom.children` | Lambda node sequence | read-only VArray over native child links; stable for the run |
| `Document.forms` | HTMLCollection | live/fixed direct backend |
| `HTMLFormElement.elements` | HTMLFormControlsCollection | live/fixed direct backend |
| form named getter with multiple matches | RadioNodeList | live/fixed when owner-backed; otherwise static membership |
| `getElementsByTagName`, `getElementsByClassName` | HTMLCollection | live/fixed direct backend |
| `Document.getElementsByName` | NodeList | live/fixed direct backend |
| `querySelectorAll` | NodeList | static membership in both doors |
| `HTMLSelectElement.options` | HTMLOptionsCollection | live/fixed; declared `item`, `namedItem`, `add`, `remove` methods |
| `HTMLSelectElement.selectedOptions` | HTMLCollection | live/fixed direct backend |
| `Element.attributes` | NamedNodeMap | live/fixed indexed/named view; attribute entries remain records until real Attr-node identity is implemented |
| `Element.classList` | DOMTokenList | live/fixed owner-backed token view; mutation only through declared token-list methods |
| `Document.styleSheets` | StyleSheetList | live/fixed owner-backed view |
| `CSSStyleSheet.cssRules`/`rules`, grouping-rule `cssRules` | CSSRuleList | live/fixed owner-backed view |
| `HTMLInputElement.files`, `DataTransfer.files` | FileList | live/fixed view owned by the input/DataTransfer |
| `DataTransfer.items` | DataTransferItemList | live/fixed view with declared mutation methods |
| MutationRecord `addedNodes`/`removedNodes` | static NodeList | immutable static-membership VArray |
| Element/Range `getClientRects()` | DOMRectList | static result VArray |
| SVG animated transform `baseVal`/`animVal` | SVGTransformList | live/fixed owner-backed view; mutation through declared SVG list methods |
| Radiant custom-layout `children` argument and Velmt `.children` view | host sequence | pass-scoped fixed view |

The same rule applies to future DOM collections such as `labels`, table
`rows`/`cells`/`tBodies`, `areas`, `TouchList`, and media/text-track lists when
they are implemented: use VArray if the WebIDL value is a collection object;
use Array if the operation returns a sequence value.

The following stay ordinary Arrays because the DOM API specifies a sequence or
fresh list value rather than a branded collection object:

- `getAttributeNames()`;
- Event `composedPath()` (although its node entries become Velmt);
- `DataTransfer.types` when modeled as the specified frozen sequence;
- MutationObserver callback record batches, ResizeObserver size arrays,
  observer thresholds, and InputEvent `getTargetRanges()` sequence results;
- internal temporary option/form/query result buffers;
- any explicit materialized copy requested through a conversion API.

An indexed exotic object is not automatically a VArray. Window/WindowProxy,
CSSStyleDeclaration, and form/select elements keep their semantic VMap/Velmt
carrier and use declared indexed hooks; the VArray rule applies when the value
itself is the collection or Lambda sequence abstraction.

### 4.3 Collection operations that move onto VArray

The carrier owns `length`, numeric access, and iteration. Jube declared
interfaces own WebIDL behavior:

| Operation family | Owner after migration |
|---|---|
| `length`, `[index]`, `for`/iterator reads | VArray structural vtable |
| `item(index)` | one shared method over VArray `get_at` |
| `namedItem(name)` and supported named properties | collection's Jube named hook over the same backend |
| `HTMLOptionsCollection.add/remove`, writable length if admitted | declared DOM binding with DOM mutation checks |
| `DOMTokenList.add/remove/toggle/replace`, `contains` | declared DOM binding over token backend |
| FileList/DataTransfer item access | declared interface or shared `item(index)` helper |
| prototype, descriptors, enumeration, `instanceof` | Jube/JavaScript adapter, not VArray structural ops |

Once these producers use VArray, delete the materialized-live-collection
machinery rather than leaving a shadow path:

- `SelectOptionsOwnerEntry`, `LiveChildCollectionEntry`,
  `LiveFormCollectionEntry`, and `LiveLookupCollectionEntry` Array registries;
- the four fixed 4096-entry cache arrays and their weak-root/pin bookkeeping;
- `_decorate_dom_collection`, `_decorate_options_collection`, and companion
  `length` repair;
- `_refresh_live_child_collection`, `_refresh_live_form_collection`,
  `_refresh_live_lookup_collection`, select-option refresh copies, and the
  mutation-wide refresh sweeps;
- `js_array_exotic_before_property_get` collection detection;
- array-companion named/indexed shims used only to imitate NodeList,
  HTMLCollection, NamedNodeMap, or related DOM collection brands.

Internal collectors may remain as reusable backend walkers. They fill an
explicit materialized copy only when the caller requests one; they no longer
keep issued public Arrays coherent.

## 5. DOM backend shapes

The DOM implementation should use a small set of parameterized backends, not
one vtable per property:

```text
DomChildVArray(owner_ref, filter = all | elements)
DomLookupVArray(root_ref, query_kind, query, include_root)
DomFormVArray(document_ref | form_ref, listed_filter)
DomOptionsVArray(select_ref, selected_only)
DomAttributeVArray(element_ref)
DomCssRuleVArray(sheet_or_rule_ref)
DomStaticVArray(rooted_items)
DomNodeVelmt(node_ref)
RadiantLayoutVelmt(pass_id, RadiantVelmt snapshot)
```

Near-identical count/get/named-item walkers must be shared through filter and
projection callbacks. A third per-collection copy is not acceptable. Each
backend validates its owner generation before access and returns a neutered
result after document teardown.

DOM Velmt's structural child face is the all-node `childNodes` order. The
element-only `children` property returns a filtered VArray. Its structural
attribute face exposes content attributes; WebIDL properties and methods are
declared Jube members and take precedence. Text and comment nodes have empty
attribute/child faces and expose `data` through their declared
`character_data` interface.

## 6. Migration plan

### Phase 0 — Disambiguate the name

- Rename native `Velmt`, `VelmtBox`, and `VelmtEdges` to `RadiantVelmt`,
  `RadiantVelmtBox`, and `RadiantVelmtEdges`.
- Rename native snapshot-building helpers accordingly.
- Keep public `radiant.velmt_*` compatibility functions until the real Velmt
  carrier supplies direct structural access.

### Phase 1 — Common header and VMap no-op port

- Add `VirtualContainer`/`VirtualVtable` and make VMap use the common prefix
  without changing observable behavior.
- Add common brand, payload, invalidation, trace, and destroy helpers.
- Convert Jube's direct `receiver.vmap` assumptions to common helpers.
- Pin header offsets and update GC ABI assertions.

Gate: all existing VMap, Jube, DOM, Lambda, and GC tests pass before either new
TypeId is introduced.

### Phase 2 — VArray core

- Add `LMD_TYPE_VARRAY`, `VArrayOps`, constructors, GC, type-family routing,
  indexing, length, iteration, equality, formatting, validation, and
  materialization.
- Add immutable and mutable synthetic test backends.
- Add Jube VArray branding, indexed/named hooks, and JS `Array.isArray` guard.

Gate: VArray is representation-equivalent to Array for Lambda reads while a
branded NodeList-like test object remains non-Array to JavaScript.

### Phase 3 — Velmt core

- Add `LMD_TYPE_VELMT`, `VelmtOps`, dual-face access, tag/namespace access,
  GC, equality, formatting, validation, and materialization.
- Add a synthetic element backend and compare it with a materialized Element
  across the generic runtime surface.

Gate: tag/namespace/attribute/child equality and formatting match
**S5.4.3** exactly.

### Phase 4 — Radiant pilot

- Make the custom-layout wrapper allocate Velmt over `RadiantVelmt` rather
  than VMap.
- Pass custom-layout children as VArray rather than a materialized Array.
- Preserve pass-id stale-handle checks, bounded descendant traversal, direct
  layout properties, and result parsing by child/index.

Gate: graph/custom-layout baselines are byte-identical and stale handles never
dereference native state.

### Phase 5 — DOM node carrier

- Switch all DOM Node-family wrapper factories and wrapper caches to Velmt.
- Convert unwrap, type-record, ordinal, event-target, range/selection, document,
  and invalidation paths together.
- Retain one declared-interface/member implementation; do not duplicate DOM
  behavior into the Velmt vtable.

Gate: Lambda DOM fixtures, JavaScript DOM/WPT fixtures, wrapper identity,
document teardown, detached nodes, foreign documents, and forced GC pass.

### Phase 6 — DOM collections

- Convert static collection objects first: querySelectorAll, rect lists, and
  custom-layout lists.
- Convert live child/lookup/form/options collections to owner-backed VArrays.
- Convert attributes, token lists, CSSOM lists, and file/data-transfer lists.
- Delete decorated-Array registries and refresh sweeps only after the last
  producer moves.

Gate: live JS collections update after insert/remove/reorder; static NodeLists
do not; Lambda reads the same native backends without copying while the DOM
stays fixed for the full run; named access, iteration, prototypes, descriptors,
and teardown remain correct.

### Phase 7 — Optimize and close

- Teach MIR type facts to retain semantic array/element information while
  guarding packed-storage specializations.
- Replace remaining repeated TypeId lists with semantic helpers.
- Update formal D7.4.5v2 implementation status only after every exit gate passes.
- Distill the landed design into the relevant `doc/dev` runtime/DOM documents.

### 6.1 Implementation status (2026-09-08)

| Phase | Status | Landed scope / remaining gate |
|---|---|---|
| 0 | complete | Native layout types are `RadiantVelmt`, `RadiantVelmtBox`, and `RadiantVelmtEdges`; unqualified `Velmt` is the Lambda carrier. |
| 1 | substantial | Common host metadata, carrier-neutral Jube dispatch/invalidation, precise expando tracing, and offset assertions landed. VMap intentionally retains its released operation vtable. |
| 2 | substantial | Distinct VArray TypeId, core/Jube allocation, read/write dispatch, type/validation/iteration/equality/order/printing, JS prototype behavior, and precise GC landed. Synthetic mutable-splice and explicit materialization tests remain. |
| 3 | substantial | Distinct Velmt TypeId, tag/namespace/attribute/child faces, core/Jube allocation, validation, printing, semantic equality/total order, raw shaped-field storage, and precise GC landed. Wider formatter and forced-GC matrices remain. |
| 4 | complete | Custom-layout handles are Velmt over `RadiantVelmt`; their children are pass-scoped VArrays. |
| 5 | substantial | Shared Node-family wrapper creation, cache identity, document/text/SVG/form-control brands, unwrap, Jube dispatch, expandos, and teardown use Velmt. Full WPT/foreign-document/forced-GC gates remain. |
| 6 | partial | Live child/element/form/lookup/options/selected-options/attributes/token/CSSOM collections and fixed query/radio/rect/mutation lists use VArray. FileList, DataTransferItemList, and SVGTransformList remain on their pre-migration representations. |
| 7 | partial | MIR physical guards, range-based sequence classification, semantic carrier helpers, Jube ABI 6, formal status, and repository baselines are updated. Specialized WPT, remaining migration, and release-performance closure remain. |

Lambda-facing `dom.child_nodes` and `dom.children` now return the same direct
native-reading VArray backends as the host model. There is no
`snapshot_for_lambda` hook or collection-membership copy: the caller supplies
the fixed-DOM interval required by D4l.8. A held Lambda VArray therefore reads
the native membership present during that run and may observe a later state in
a later run.

## 7. Verification matrix

At minimum, the implementation needs these focused tests in addition to the
full Lambda, Radiant, JavaScript, and GC suites:

| Area | Required checks |
|---|---|
| VMap compatibility | insertion order, arbitrary keys, present-null vs missing, readonly/error routing, trace/destroy |
| VArray transparency | `type`, `is`, len, positive/negative/out-of-range index policy, iteration, equality with Array/range, formatting |
| VArray mutation | read-only rejection; mutable set/splice; mutation during Lambda iteration walks entry snapshot |
| Velmt transparency | tag, namespace, attrs, children, equality with materialized Element, nested formatting, validation |
| Jube | all three carrier brands through generic and ordinal get/set/call, named/indexed hooks, own keys, descriptors, prototypes, invalidation |
| GC | forced collection inside every allocating callback, precise backend edges, wrapper weak cache, owner teardown, destroy exactly once |
| DOM live lists | held `childNodes`, `children`, forms, elements, lookups, options, selectedOptions, attributes, cssRules, files update correctly in JS |
| Fixed-DOM run | Lambda collection reads allocate no membership copy; mutation sources and teardown wait or fail during the run; a later run observes later DOM state |
| DOM nodes | navigation, creation, mutation, Range/Selection/Event node fields, detached/foreign-document behavior, wrapper identity |
| Custom layout | direct properties, nested children, stale pass, placement by handle/index, graph rendering |
| Guest boundary | `Array.isArray(NodeList) == false`; Lambda `type(node_list) == "array"`; cross-language handoff reuses the backend under the fixed-DOM run |

Performance gates should compare:

- DOM mutation cost before/after removal of issued-array refresh sweeps;
- held live-collection read cost;
- custom-layout allocation count and callback time;
- generic iteration overhead of VArray versus materialized Array;
- DOM member ordinal hit rate before/after carrier-neutral Jube routing.

Use release builds for every performance result.

### 7.1 Landed verification (2026-09-08)

- `make test-lambda-baseline`: 5,078/5,078 passed, including 107/107 MIR
  forced-GC stress cases.
- Full JavaScript regression suite: 371/371 passed. Focused DOM collection
  coverage includes live/fixed indexing, descriptors, iteration, `for...in`,
  prototypes, `instanceof`, and `Array.isArray` guest-boundary behavior under
  **D1.3** and **D7.4.5v2**.
- `make test-radiant-baseline`: all 7,771 recorded layout assertions passed
  (7,410 full and 361 partial-baseline assertions). Its six reported failures
  reproduce with an unmodified clean-HEAD executable: one page snapshot gate,
  one render threshold, `js_computed_style_table`, `dom_sortable_drag`,
  `dom_splide_swipe`, and `dom_pkg_listbox`. They are not regressions from the
  carrier migration.
- `make lint ARGS='--rule ^no-int-cast-radiant$'` and `git diff --check` pass.

These results close the repository baseline regression check for the landed
scope. They do not close the specialized WPT, remaining collection migration,
generic mutable-splice/materialization, or release-performance gates listed
above, so **D7.4.5v2*** remains partially implemented.

## 8. Resolved decisions and remaining open question

1. Resolved: `LMD_TYPE_VARRAY` and `LMD_TYPE_VELMT` use distinct ABI tags so
   every physical decode is explicit (**D2.3.2**, **D7.4.5v2**).
2. Resolved for this stage: NamedNodeMap is VArray, while each attribute entry
   remains a map-shaped record until Attr identity/lifecycle is implemented.
3. Resolved for the carrier ABI: generic Velmt retains optional child
   `set_at`/`splice` hooks. DOM and Radiant install read-only callbacks; a future
   backend may opt into checked procedural mutation.
4. Open: whether virtual values may be used as hash keys when backed by mutable host
   state. The safe default is to require immutable materialization before
   hashing so **S5.6.2** cannot be invalidated by later host mutation.

The hashing question does not block structural reads or host collection use,
but it must be settled before mutable virtual values are admitted as hash keys
or **D7.4.5v2*** loses its implementation mark.
