# Radiant CSS Cascade: Memory Retention, Sharing, and Reuse

**Status:** Implemented. The selected representations passed the ownership,
accounting, and page-memory gates below: bounded condition/program reuse,
owned and pseudo retirement, mutation-scope planning, epoch-local immutable
payload sharing, compacted property records, bounded cold history, and
explicit replacement/extension entry points. The opt-in loader probe and its
standard-host regression harness remain disabled in ordinary page loads.
**Date:** 2026-09-18  
**Verified against:** `0a46bd2129c6a4f5bf444bf5deec8a767c250126`  
**Scope:** CSS condition evaluation, matching, specified-style storage, style
epochs, custom properties, pseudo styles, and the seam to computed view props.  
**Related record:** [Slow Online-Page Loading](Radiant_Issue_Slow_Page_Loading.md).  
**Spec linkage:** [Formal Design](../../doc/Lambda_Formal_Design.md)
**D4.1.4v4** (arena versus pool reclamation), **D4.2.1v3–D4.2.5v3**
(allocator ownership, attribution, shared lifetime, and diagnostics), and
**D4.5.1v3** (Radiant's region/pool policy). CSS-specific existing decisions are
[View Reuse](Radiant_Design_View_Reuse.md) **VR1–VR6** (computed-prop sharing),
**VR12** (accounting), **VR13** (canonical specified styles), and **VR14**
(singular lifecycle paths).

This document records extensions to those decisions, particularly VR13's
whole-epoch retention policy. It does not revise a formal ruling or silently
replace an adopted VR decision. Ratification and implementation must update
the affected working decisions and any formal ruling whose meaning changes.

## 1. Findings and recommended direction

The recorded GitHub run grew by **1,644 MB during the initial cascade** and
**2,019 MB during the post-script cascade**. These are changes in process
physical footprint, not measurements of canonical-style ownership. The
current file at the recorded Brotli log path contains a later DNS-failure
run, so the historical figures remain evidence from the investigation record,
not a workload reproduced by this analysis.

Focused release probes establish four independent allocation mechanisms:

1. **Conditional evaluation retains temporary allocations.** Evaluating
   `(min-width: 1px)` 10,000 times retains 340,000 requested bytes; evaluating
   `(display: block)` as an `@supports` condition retains 20,550,000 requested
   bytes. Neither loop frees any of those allocations.
2. **Owned and pseudo styles accumulate discarded storage.** Over 99 further
   cascades of 128 elements, inline-style and pseudo-style cases each retain
   another 5,677,056 document-pool bytes. The custom-property case retains
   another 7,299,072 bytes and grows from 128 to 12,800 custom-property records.
3. **Exact recipe sharing works when eligible.** The equivalent plain-style
   case retains no additional document-pool or canonical-pool live bytes
   between cascade 1 and cascade 100. It keeps two canonical entries.
4. **Canonical history is retained even without bindings.** One element
   visiting 100 distinct rule identities with identical declaration contents
   creates 101 canonical entries including the empty recipe. The current
   epoch retains 72,200 live bytes even after all element bindings are removed.

Therefore, disabling canonical styles or rotating epochs after every cascade
would target the wrong abstraction. The first changes should remove transient
allocation retention, reclaim discarded owned styles safely, and reduce the
scope of recascading. Sharing should then extend from whole recipe trees down
to immutable declaration payloads and property blocks.

The target is **memory proportional to the current document, distinct live
styles, and a bounded reuse cache**, rather than the number of previous
cascades or selector-condition evaluations. Whole-document fallback must also
have bounded retained growth; incremental invalidation alone cannot provide
that guarantee.

### 1.1 Implemented result

The implementation follows the ownership boundaries in **D4.1.4v4**,
**D4.2.1v3–D4.2.5v3**, and **D4.5.1v3** without adding a new allocator or a
GC path:

- A 128-entry condition cache owns at most 1 KiB of text per key and covers
  all media/supports inputs read by the evaluator. Parser/tokenizer scratch
  is now a temporary pool destroyed at the end of the evaluation. The active
  stylesheet program evaluates each conditional block before DOM traversal and
  is held only in cascade scratch.
- Every replace recascade destroys exclusive old specified trees. A pseudo
  source tracks generated-box borrowers, so replacement publishes a new tree,
  retires the old source, and frees it after its final borrower rebinds.
  Stylesheet custom-property records are rebuilt rather than accumulated.
- Canonical declarations are thin cascade records pointing at one frozen,
  epoch-owned source payload. COW still creates an owned deep snapshot. This
  retains CSSOM/source-mutation safety while making payload count scale with
  unique source declarations rather than recipes.
- `StyleNode` now stores only its property identity; the existing `AvlTree`
  wrapper retains links and balancing metadata. This removes the second,
  unused embedded AVL record from every specified property while leaving
  winner/loser rollback semantics unchanged.
- Canonical entries without consumers enter a 4 MiB LRU cold list after the
  outer batch. `style_epoch_cascade_begin_replace` and
  `style_epoch_cascade_begin_extend` make snapshot rebuild and additive
  stylesheet placement distinct. Existing exact `InlineProp` sharing remains
  the only computed-property group admitted after its write/ownership audit;
  mutable layout, font, resource, and geometry groups remain element-owned.
- The mutation planner uses a parent closure for structural and `:empty`
  dependencies and falls back to a full recascade for broad relational
  selectors such as `:has()`. This keeps local work local without weakening
  the conservative correctness fallback.

Cross-epoch payload interning, a persistent pseudo-style DAG, and broader
computed-prop interning were deliberately not admitted: the measured retained
paths are fixed by the safe forms above, while those representations would
need version owners or mutation audits beyond the contracts currently present.
The current implementation records their required accounting fields, so a
later measured admission can preserve the same ownership gates.

## 2. Current issues and their identity

The names below identify mechanisms within this proposal. They are not a new
formal decision series or a replacement for the central issue ledger.

| Issue | Evidence | Consequence |
|---|---|---|
| [Global load-time invalidation](#global-load-time-invalidation) | Source verified; scripted local fixtures exercised | Small mutations can repeat all matching and allocation work. |
| [Retained condition scratch](#retained-condition-scratch) | Direct allocation probes | Document-independent predicates allocate repeatedly in long-lived storage. |
| [Discarded owned-style storage](#discarded-owned-style-storage) | Direct repeated-cascade probes | Clearing a tree does not reclaim its previous graph. |
| [Custom-property accumulation](#custom-property-accumulation) | Direct record counts and pool bytes | Repeated application grows a list of historical declarations. |
| [Sharing exclusions and payload duplication](#sharing-exclusions-and-payload-duplication) | Source verified; eligibility measured | Inline/custom cases lose whole-tree reuse; recipe misses duplicate common values. |
| [Unbounded canonical history](#unbounded-canonical-history) | Distinct-recipe and unbind probes | Current zero-binding entries have no eviction path. |
| [Batch-dependent recipe identity](#batch-dependent-recipe-identity) | Additive-API diagnostic probe | Equivalent visible state can acquire longer recipes and larger snapshots. |
| [Redundant property-node storage](#redundant-property-node-storage) | Source verified; release `sizeof` values | Each property has both an embedded AVL node and another allocated AVL wrapper. |
| [Incomplete accounting and dependency keys](#incomplete-accounting-and-dependency-keys) | Source review and page-profile comparison | Selected-pool totals miss owners; incomplete keys threaten future cache correctness. |

### 2.1 Global load-time invalidation

<a id="global-load-time-invalidation"></a>

The loader treats `js.mutation_count > 0` as sufficient to clear cascaded and
pseudo styles across the DOM, then traverses the document again. The cascade
visits each element and loops through every stylesheet rule; complex selectors
add ancestor/sibling traversal to that work.

Mutation count is not invalidation scope. Two changes can be irrelevant to
selectors, or can invalidate the entire document through a root class or
stylesheet edit. The missing abstraction is a shared dependency-based style
invalidation plan, usable both before initial layout and after UI events.

The event path already contains mutation classification, structural-selector
checks, and subtree cascade helpers. Its incremental gate currently requires
layout state. Reuse the style-specific parts after separating that dependency;
do not copy the event implementation into the loader.

### 2.2 Retained condition scratch

<a id="retained-condition-scratch"></a>

Conditional rules are evaluated inside the element/rule traversal:

- `css_evaluate_media_query` allocates a mutable query copy and copies of query
  alternatives into `engine->pool`, without reclaiming them on return.
- `css_evaluate_supports_span` copies declaration text and invokes the
  declaration parser in that pool. Tokenizer state, token buffers, values,
  and declaration records survive a Boolean capability check.
- The tokenizer reserves a token array based on source length and also creates
  a compatibility token array. This amplifies supports-check allocation.

For `E` visited elements and `C` condition blocks, the current traversal can
perform `O(E × C)` repeated evaluations. The measured 2,055 bytes per supports
call apply to the specific short fixture, not every condition. They are enough
to establish the retention mechanism independently of canonical trees.

### 2.3 Discarded owned-style storage

<a id="discarded-owned-style-storage"></a>

`style_tree_clear` calls `avl_tree_clear`, which discards the root and counters.
It does not walk and free the old nodes, declaration records, or weak-list
links. The inline-preserving removal path also detaches declarations and
nodes without reclaiming all detached storage. `css_declaration_unref` changes
the logical reference count; it is not a reclamation operation.

This is retention during the document lifetime, not proof that document teardown
leaks memory. Once the old root has been forgotten, ordinary final destruction
cannot walk that old graph; the pool teardown remains its reclamation boundary.

The clear operation deliberately preserves allocations because retained pseudo
views can still refer to them. Blindly replacing it with `style_tree_destroy_owned`
would risk dangling borrowers. Reclamation requires explicit consumer lifetime,
not an unconditional free at the current clear call.

### 2.4 Custom-property accumulation

<a id="custom-property-accumulation"></a>

The custom-property declaration path prepends `CssCustomProp` records to
`element->css_variables`. Clearing the ordinary cascaded tree does not rebuild
or clear this list. The focused fixture adds 128 records per cascade for 128
elements and one custom-property declaration.

This also interacts with eligibility: a non-null custom-property list normally
prevents subsequent canonical collection for an owned element. Custom values,
ordinary stylesheet winners, and inline CSSOM state need separate, explicit
ownership rather than one accumulated side list.

The profile proves record growth. Correctness of removal, priority, and stale
custom-property lookup requires dedicated semantic tests; it was not inferred
solely from the byte counters.

### 2.5 Sharing exclusions and payload duplication

<a id="sharing-exclusions-and-payload-duplication"></a>

The epoch collector excludes ordinary owned trees that already contain inline
declarations or custom properties. A matched rule containing a custom-property
declaration, an invalid declaration, or a value that cannot be safely cloned
causes an eligible builder to materialize owned storage. Pseudo-element rules
use a separate per-element tree path. Some `calc` representations are excluded
because their pointer union cannot currently be traversed safely.

On a canonical miss, the implementation deep-copies every applied declaration,
including losing candidates, nested value graphs, authored value text, property
names, and source-file strings. An owned COW clone deep-copies the full graph
again. Different recipes therefore duplicate identical common payloads even
when most final properties agree.

This safety-oriented snapshot contract must remain valid during optimization:
CSSOM can mutate source declarations, and applying a declaration currently
rewrites its `source_order`. Direct pointer sharing of today's mutable
`CssDeclaration` is not a valid replacement.

### 2.6 Unbounded canonical history

<a id="unbounded-canonical-history"></a>

The current epoch's recipe index admits every miss. Unbinding decrements entry
and epoch references but does not evict a zero-reference entry. Pool release
requires both a retired epoch and zero epoch bindings. Thus:

- Many short-lived styles can accumulate in one long-lived current epoch.
- One remaining binding can pin an entire retired epoch.
- An epoch pool can have no element bindings yet remain retained because it
  is current.

The existing policy has a demonstrated benefit: the prior 46-page VR13 campaign
reported approximately 3.44 MB less physical-live storage than disabling style
epochs. Its duplicate-only-within-one-batch experiment also performed worse
than cross-batch reuse. Preserve that reuse while bounding cold history.

### 2.7 Batch-dependent recipe identity

<a id="batch-dependent-recipe-identity"></a>

Recipe identity includes the base canonical entry and an ordered list of newly
matched rules. A miss clones the base tree before applying the extension.
Repeated additive calls without a clear can therefore produce nested identities
and repeated snapshots even when visible winners remain unchanged.

The diagnostic probe reapplied one unchanged stylesheet 100 times to a root
and one child **without clearing**. It produced 200 entries and 1,495,096
canonical live bytes, versus 2,900 after the first application. This tests the
additive API; it does not establish that the historical GitHub loader took
that path. Normal replace-and-recascade stayed flat in the plain-style probe.

Separate replacement from extension in the API and avoid minting an entry for
an empty extension. Do not make an additive call idempotent by discarding real
source-order changes; distinguish reapplying an existing stylesheet placement
from inserting another occurrence of that stylesheet.

### 2.8 Redundant property-node storage

<a id="redundant-property-node-storage"></a>

The release probe measured `AvlNode=48`, `StyleNode=88`, `CssDeclaration=88`,
`CssValue=24`, `WeakDeclaration=16`, and `StyleTree=40` bytes on arm64.
`StyleNode` already embeds an `AvlNode`, but `avl_tree_insert` allocates another
48-byte wrapper. A simple winning property therefore has 136 bytes of node
structures before its declaration/value, losing candidates, and allocator
overhead. Several small allocations also pay separate pool block headers and
alignment costs.

An intrusive mutable tree could remove one wrapper. Immutable canonical styles
have a stronger opportunity: contiguous sorted entries or shared property
blocks can remove most links and reduce allocation count.

### 2.9 Incomplete accounting and dependency keys

<a id="incomplete-accounting-and-dependency-keys"></a>

The existing view-memory profiler is useful but selects a fixed set of labels.
Its `physical_total` field sums selected pools, not the process footprint or all
document-owned allocators. In the normal loader the CSS engine uses the loader's
pool, which need not be `dom.document.pool`; its condition scratch is not
isolated by the six-domain report. Independently owned arenas must also be
accounted according to current allocator ownership, not historical backing
assumptions.

`StyleEpochStats` mixes cumulative counters with present-state bytes. For
example, `canonical_entry_count` is cumulative even after old epochs release.
It does not expose current bound versus unbound entries or their byte shares.

The style environment fingerprint includes viewport dimensions, pixel ratio,
reduced motion, and high contrast, but omits color scheme, which media evaluation
reads. Other invalidation paths may compensate; this review does not claim a
reproduced stale-color-scheme result. A new condition cache must explicitly cover
all its dependencies rather than copy this incomplete key.

## 3. Proposed architecture

### 3.1 Separate source, cascade state, computed values, and layout state

The proposed ownership graph is:

```text
document MemContext
  stylesheet versions: immutable rules, declaration payloads, condition programs
  cascade scratch: temporary matches, candidate ranking, recipe construction
  canonical style store:
    exact recipe index -> immutable specified-style blocks -> value payloads
    bounded cache of unbound entries
  element binding:
    shared stylesheet contribution + inline contribution + custom-property state
    explicit pseudo-style bindings
  computed descriptors: shared only after exact contextual resolution
  layout/view state: mutable geometry, collapse state, handles, animation samples
```

These are logical roles, not a proposal for a new allocator mechanism.
**D4.1.4v4** requires individual reclamation to use pools; arenas remain
batch-lifetime storage. **D4.2.1v3** and **D4.2.3** require every allocator to be
owned and attributed through `MemContext`.

| Object | Sharing and lifetime contract |
|---|---|
| Declaration payload | Immutable property/value/source data, shared across cascade references. |
| Cascade reference | Compact payload reference plus effective specificity, origin, importance, layer/ordering context, and any required scope metadata. |
| Specified-style block | Immutable property outcomes plus enough rollback/provenance information for subsequent operations. |
| Inline contribution | Current authored inline state, independently replaceable; can itself intern after freezing. |
| Custom-property environment | Immutable current map, with shared unchanged structure and correct computed inheritance. |
| Element style binding | Acquires all necessary owners; publication and replacement are explicit operations. |
| Computed descriptor | Interned only after resolving its dependency context or proving exact resolved-value equality. |
| Layout state | Element-owned mutable data, excluded from canonical style equality. |

### 3.2 Compile and reuse the active rule program

Parse conditional expressions once with the stylesheet. Cache evaluation by
condition identity/version and the inputs it reads:

- Media conditions: relevant viewport/device/preferences/media inputs.
- Supports conditions: engine capability/configuration generation.
- Future container-dependent conditions: the particular container and its
  relevant generation, never only the document environment.

For a stable environment, build one ordered active-rule program. Flattening
conditional blocks must retain stylesheet placement, nested source order,
origin, and cascade-layer metadata. False conditions should skip their blocks
before any element matching.

Use spans or scoped scratch for any remaining temporary condition processing.
No pointers from that scratch may escape into declarations, recipes, values,
or CSSOM wrappers. Ordinary repeated evaluation should allocate no persistent
bytes after program construction.

Index candidate rules by useful rightmost-selector features: ID, class, tag,
and attribute name, with a fallback bucket for rules that cannot be safely
indexed. Union and deduplicate candidates while preserving cascade ordering;
perform full selector matching afterward. Handle selector lists and functional
selectors without excluding valid matches, and use the correct effective
specificity for the matching selector group.

Avoid a cache containing every element/rule pair: it recreates the `E × R`
memory problem. Program indexes are shared; element caches, if justified,
should store compact results with explicit generation dependencies.

### 3.3 Share immutable declaration and value payloads

First introduce a separation between immutable payload and cascade-local
metadata. Preserve a source declaration's authored text and debugging metadata
once, rather than copying them into each canonical tree.

Adopt sharing in two steps:

1. **Within an epoch:** snapshot a source payload once and reuse it across
   canonical entries. This retains the current independent-snapshot safety
   contract while removing per-recipe recursive copies.
2. **Across stylesheet versions/epochs, where measured useful:** use immutable
   versioned owners with explicit lifetime references. CSSOM mutation publishes
   a new version instead of modifying a payload observed by existing bindings.

Start deduplication with source payload identity and version. Add content
interning only where repeated content justifies hashing cost. Hashing is an
accelerator; exact field-aware equality decides reuse (**VR6**). Compare nested
lists/functions structurally, preserve case-sensitive custom-property names,
and include base-URL ownership when sharing unresolved URL values.

Do not hash padding or mutable resolution caches. A COW mutation must not
reveal an updated source payload through an older snapshot. An owned style
borrowing a shared payload must retain its owner; otherwise it must still copy.
Cross-owner references follow **D4.2.4**, not an assumption that an epoch lives
as long as the document.

### 3.4 Build compact canonical property blocks

Keep matching and ranking mutable in scratch. Freeze the outcome into a
property-ID-sorted block, with a compact presence index where beneficial.
Provide lookup and iteration adapters so consumers can migrate without each
inventing a second property representation.

A compact block is the initial candidate because normal style sets are small
enough that locality and fewer allocations may outweigh an AVL lookup's
theoretical advantage. Measure binary search, indexed lookup, and ordered
iteration under actual property distributions.

For styles that differ in a few properties, share immutable blocks by property
group or use a bounded sparse overlay. Flatten overlays when lookup depth or
combined size exceeds a measured threshold. A persistent trie/tree is an
alternative only if profiles justify its metadata and lookup costs.

Do not directly share today's mutable AVL branches: parent pointers, rotations,
invalidation fields, and cached computed values make them unsuitable as an
immutable DAG. If mutable AVL storage remains for builders, remove its duplicate
wrapper through a supported intrusive insertion API or a single node record.

Whole-style recipe sharing remains the first lookup. Block sharing then reuses
content across different recipes; it must not confuse equal visible winners
with equal future cascade behavior.

### 3.5 Preserve rollback and shorthand semantics compactly

Retaining every losing declaration as a deep graph is expensive, but removing
all losers is unsafe. Current declaration removal and `revert` can expose an
earlier candidate. `revert-layer` requires layer-aware rollback as support
develops. Preserve either compact ordered candidate references at the necessary
boundaries or an immutable recipe that can reconstruct the affected property.

For ordinary reads, expose already selected winners. For removal or rollback,
recompute from compact provenance rather than retain duplicate parsed values.
Do not implement invalid-at-computed-value-time `var()` substitution by falling
back to an earlier declaration: the CSS defaulting rules apply instead.
See [CSS Cascade Level 5](https://www.w3.org/TR/css-cascade-5/) and
[CSS Variables §3.1](https://www.w3.org/TR/css-variables-1/#invalid-variables).

Shorthand/longhand and logical/physical-property interactions need special care.
The current used-value resolver still compares declaration priorities for some
sub-values. A frozen block must retain that priority information until the
relevant expansion is complete. Shorthands containing variables may require
pending substitution; logical mapping may depend on writing mode/direction.
Do not flatten these prematurely just to obtain a smaller equality key.

### 3.6 Share the stylesheet contribution even with inline/custom styles

Inline declarations should no longer make the entire element ineligible for
sharing. Retain a shared stylesheet contribution and merge the current inline
contribution through the same cascade comparator. The inline contribution is
not an unconditional last-write override: importance and origin remain relevant.

For custom properties, replace historical linked-list accumulation with a
current immutable map keyed by exact property identity. Keep authored inline
custom properties distinct from stylesheet-derived candidates so recascade
does not erase CSSOM writes or preserve obsolete stylesheet records.

Share the unchanged portion of inherited custom-property environments. Resolve
custom-property values at the correct element before inheritance; a child
overriding `--a` must not retroactively change an inherited `--b` that was
computed from `--a` on the parent. Include cycle/invalid states, fallback
behavior, and registration semantics where supported. See
[CSS Variables](https://www.w3.org/TR/css-variables-1/).

Separate immutable maps for authored candidates and computed inherited values
if one representation cannot express both safely. Identical specified recipes
may still resolve differently under different inherited environments.

### 3.7 Give pseudo styles and retired trees explicit bindings

Use the same canonical-store machinery for element and pseudo-element targets,
with pseudo kind and applicable cascade context represented in the binding/key.
Generated boxes should retain a style handle or a registered borrowed view of
that handle, instead of relying on an untracked raw tree pointer.

Replacement follows a publish/retire sequence:

1. Construct the new style and acquire its payload owners.
2. Publish the new binding and invalidate dependent computed state.
3. Retire the previous tree while keeping its allocation graph reachable.
4. Release it after generated views, active resolver calls, and other registered
   consumers stop using it.

Where ownership is exclusive, return obsolete nodes and records directly to
the pool. Where a borrower remains, keep an explicit retirement entry; never
discard the only pointer to its allocation graph. Audit pseudo views, persistent
prop payloads, active recipe builders, and CSSOM/native references before
changing the current clear behavior.

Reuse existing ownership gates and retirement machinery where their contracts
fit (**VR14**, **D4.5.1v3**). This proposal does not introduce GC for CSS or rely
on native-stack scanning.

### 3.8 Bound canonical cache history

Distinguish three quantities:

- **Live style storage:** required by element bindings and other consumers.
- **Unbound reusable entries:** optional history with a byte budget and reuse
  policy.
- **Allocator capacity:** reserved/committed space that may remain reusable
  after individual objects are freed.

Keep exact recipe hits across batches. Put entries with no consumers on a cold
reuse list; evict them by retained bytes and recency when the cache budget or
memory-pressure policy requires it. A cap limits optional history, not the
document's legitimate live style set.

Initially sweep only after the outer cascade batch ends. Recipe builders can
hold base-entry pointers, so zero element references alone are insufficient
for eviction during collection. If earlier reclamation is later needed, add
explicit builder pins. Future shared blocks must count incoming structural
references or retain their backing owner. Acquire the new binding before
releasing the old binding during replacement.

Retired epoch pools can still release wholesale when no consumers remain.
For current epochs, support entry/block reclamation through the pool. Individual
free enables reuse but does not promise an immediate drop in process footprint.
If sparse long-lived bindings pin excessive retired capacity, consider smaller
reclaimable storage segments or migration at a verified safe boundary, with
reference updates and peak-memory accounting. Do not add a new allocator
mechanism or compact raw pointers opportunistically.

Keep the **input generation** used to validate recipes separate from the
**storage lifetime** needed by their consumers. Ordinary reflow must not create
a new style epoch (**VR13**). Coordinated pressure reclamation belongs to
`MemContext` (**D4.2.2v2**).

### 3.9 Make replacement and extension explicit

Define separate operations for rebuilding an element's stylesheet contribution
and extending it with genuinely new stylesheet placements. A replacement
builds one normalized ordered recipe for the current stylesheet program, rather
than a recipe chained to the prior result.

An empty extension should return its existing binding. Reapplying the same
program/version in replacement mode should produce a cache hit. Preserve
multiple intentional stylesheet placements and declaration order; do not
deduplicate solely by a rule pointer or declaration text.

This reduces batch-dependent history while leaving exact collision checking
and valid additive behavior intact. Measure recipe-prefix sharing separately;
do not retain a deep-copied full tree at every prefix merely to reuse the prefix.

### 3.10 Use one style invalidation planner

Extract a shared style-only planner from the event path and use it during load,
events, CSSOM reads, and other style checkpoints. Its output should identify
affected targets/subtrees and computed-style dependencies, independently of
whether layout has run.

The plan must account for:

- Changed IDs, classes, attributes, and dynamic pseudo state.
- Descendant effects from ancestor selectors and inherited values.
- Sibling and structural selectors after insertion/removal/reordering.
- Ancestor effects from `:has()` and other supported relational dependencies.
- Text changes affecting selector state such as `:empty`.
- Inline-style attribute selectors as well as direct property changes.
- Stylesheet/CSSOM changes, media environment changes, and record overflow.

Merge overlapping dirty roots, leave clean bindings untouched, and stop
inherited computed-value propagation when the relevant resolved state is
unchanged. Geometry-only changes should not rematch selectors unless the
selector/conditional dependencies require it.

Retain a conservative full-document fallback for unsupported dependencies or
incomplete mutation records. Its allocations must still be reclaimable. Initial
script `getComputedStyle` and geometry reads remain observable checkpoints;
do not defer required style correctness until after script execution.

### 3.11 Extend computed-property sharing after the cascade fixes

Existing exact `InlineProp` interning is a useful starting point (**VR1–VR6**).
Evaluate further groups using a write/ownership audit and measured hit rates.
Split shareable computed descriptors from mutable font handles/metrics,
margin-collapse bookkeeping, resource state, geometry, and animation samples.

Computed sharing should use either exact fully resolved value equality or a
complete dependency key covering inherited state, custom properties, relevant
font/root/viewport inputs, writing mode, and element-sensitive values. Store
percentages and other unresolved values at the appropriate CSS value stage;
used values that depend on a containing block cannot be shared solely because
specified declarations match.

Continue using COW gates for every post-publication write. Measure canonical
metadata, payload ownership, and clone costs before admitting another prop
group. A large global computed-style cache is not the first remedy for the
measured specified-style retention.

## 4. Instrumentation and decision criteria

Extend existing `StyleEpochStats`, pool counters, and memory snapshots rather
than introduce an independent allocation tracker (**D4.2.5v3**, **VR12**).
Production builds need low-overhead counters, with detailed attribution opt-in.

At parse completion, before/after each cascade, after retirement, and after
layout, report:

| Area | Required counters |
|---|---|
| Work | Visited/restyled elements, rule candidates, full selector attempts, matched rules, conditions evaluated/cache hits, invalidation scope and reason. |
| Eligibility | Shared/owned/pseudo bindings; exclusions by inline/custom/unsupported-value/invalid-declaration reason. |
| Payloads | Unique source/snapshot values, referenced values, declaration records, copied value/text/source bytes. |
| Structure | Winners, fallback references, AVL wrappers, compact blocks, recipes/index bytes, COW bytes. |
| Lifetime | Current live entries, bound/unbound bytes, in-progress/borrower pins, retired referenced bytes, evictions, release delays. |
| Allocation | Requested live bytes, cumulative allocation/free counts, committed and reserved bytes by actual owner; snapshot metadata excluded from the sampled workload. |
| Process | Physical footprint/RSS/peak measured separately from allocator counters. |

Retain cumulative creation statistics but name them explicitly. Add current
gauges; do not interpret cumulative `canonical_entry_count` as the current
live entry count. Attribute the loader/CSS-engine pool as well as the DOM/view
pools. Sum actual backing allocations once and distinguish logical subdomains
from independent physical owners.

A useful accounting model is:

```text
CSS memory = immutable stylesheet/value storage
           + live shared specified blocks and fallback provenance
           + element inline/custom/pseudo state
           + bounded unbound cache
           + retired storage still needed by consumers
           + temporary peak scratch
           + allocator metadata/capacity
```

The priorities are determined by retained bytes and repeated-work counts, not
cache hit rate alone. A high hit rate can coexist with a large cold cache; a
small inline difference can currently force a large owned copy.

### 4.1 Standard-host page-load regression baseline

The probe records one structured `[CSS_CASCADE_MEMORY]` line immediately after
the initial load cascade and one after a clean full recascade. It is disabled
unless `RADIANT_CSS_CASCADE_MEMORY_PROFILE=1`. The companion
`RADIANT_CSS_CASCADE_MEMORY_FORCE_RECASCADE=1` switch takes the existing
clear-and-cascade loader path after scripts even when a fixture made no DOM
mutation. It changes no default page-load ordering and gives every selected
fixture the same second-sample contract.

Each line includes the document-pool live total and per-cascade delta, the
loader/work-pool cascade delta, document-scoped `MEM_ROLE_CSS` live/reserved
totals, current canonical-epoch live/reserved totals, and recipe/binding/lookup
counts. It also includes frozen-payload references, current bound/cold entries,
cold retained bytes, cache evictions, and condition evaluations/cache hits.
The first metric exposes owned specified-style retention under the
document owner; the work-pool metric covers CSS-engine and selector-matcher
allocations that currently use the loader's layout-role pool. The CSS-role and
canonical metrics separately prove whether a full recascade duplicated shared
recipes. This uses the existing pool and `MemContext` accounting path required
by **D4.2.5v3**, and keeps allocator ownership explicit as required by
**D4.1.4v4** and **D4.5.1v3**.

`test/test_css_cascade_memory_gtest.cpp` runs six frozen local pages using the
`lambda.exe` selected by the standard Radiant baseline build, reads the two
lines from the child log, and checks
[`test/css_cascade_memory_baseline.tsv`](../../test/css_cascade_memory_baseline.tsv).
The captured byte baseline is valid for the standard debug or release host;
it allows 15% plus 4 KiB for pool extent rounding, while recipe and binding
counts are exact upper bounds. Run it with:

```sh
make test-css-cascade-memory
```

The selected fixtures are intentionally small enough for a focused gate while
covering distinct cascade shapes:

| Fixture | Source | Initial document / work delta | Recascade document / work delta | Canonical CSS after recascade | Recipes / bindings |
|---|---|---:|---:|---:|---:|
| `jqueryui` | `page` | 67,082 / 415 | 37,770 / 104 | 321,604 | 78 / 327 |
| `linuxmint` | `page` | 502,944 / 754 | 243,360 / 104 | 577,222 | 154 / 506 |
| `netflix` | `page` | 355,136 / 592 | 228,160 / 104 | 224,337 | 57 / 245 |
| `bootstrap-5-kitchen-sink_` | `page` | 3,584,791 / 1,202 | 2,922,267 / 104 | 1,046,285 | 250 / 1,224 |
| `matrix-free-bootstrap-admin-template` | `web-tmpl` | 552,224 / 439 | 362,272 / 104 | 340,279 | 90 / 367 |
| `b-school-free-education-html5-template` | `web-tmpl` | 742,992 / 561 | 520,976 / 104 | 632,405 | 124 / 424 |

#### Before/after implementation capture

The table below compares the pre-implementation page capture with the current
baseline. All values are bytes. Each fixture runs in a separate child process,
so the six-fixture sums compare the same workload set; they are not one
process's simultaneous memory footprint. `Cascade work` is the first-cascade
loader/work-pool delta, `canonical CSS` is the persistent specified-style
store after the initial cascade, and `document live` is the document-pool
total after the forced clean recascade.

| Fixture | Cascade work, before → after | Canonical CSS, before → after | Document live after recascade, before → after |
|---|---:|---:|---:|
| `jqueryui` | 243,235 → 415 | 329,214 → 321,604 | 238,506 → 176,242 |
| `linuxmint` | 5,235,893 → 754 | 701,065 → 577,222 | 1,023,626 → 715,354 |
| `netflix` | 980,448 → 592 | 226,670 → 224,337 | 678,309 → 465,741 |
| `bootstrap-5-kitchen-sink_` | 23,528,905 → 1,202 | 1,472,858 → 1,044,081 | 9,108,918 → 4,553,618 |
| `matrix-free-bootstrap-admin-template` | 1,041,872 → 439 | 383,931 → 340,279 | 1,059,872 → 733,224 |
| `b-school-free-education-html5-template` | 1,920,892 → 561 | 796,941 → 632,405 | 1,520,794 → 1,038,234 |

Across the six separate fixture captures, initial cascade work fell from
0.24–23.53 MB to 0.4–1.2 KiB per page, a 99.8%–99.995% reduction. Canonical
CSS storage fell from 3,910,679 to 3,139,928 bytes (19.7%), and document live
memory after recascade fell from 13,630,025 to 7,682,413 bytes (43.6%). The
work-pool reduction comes from temporary condition parsing and one-pass active
rule construction; the persistent reductions come from compact property
records, shared frozen payloads, and retirement of superseded owned styles.

The captured normal page-load paths have zero cold entries and cold bytes, and
the clean-recascade sample reports zero canonical-live delta. The regression
gate retains these exact profile identities plus the byte limits, so later
changes cannot reintroduce retained conditional scratch or old style trees
without changing the baseline deliberately. The page gate must remain
alongside the focused ownership tests in §5.1.

#### Live GitHub Brotli comparison

On 2026-09-18, the standard debug host loaded
`https://github.com/google/brotli` at 1200 × 800 with the same no-op event
file before and after this implementation. The before revision was
`0a46bd2129c6a4f5bf444bf5deec8a767c250126`; the after revision was
`12b507e510bc4c8f9ed3d5ca321480687a072f14`. Both runs parsed the top-level
document to 26 MB, built the DOM at 28 MB, and reported the same 555-node
script AST census. This confirms a comparable primary document and script
workload, although a live site remains unsuitable for a strict regression
gate.

| Cascade boundary | Before footprint change | After footprint change | Reduction |
|---|---:|---:|---:|
| Initial cascade | 195 MB → 1,831 MB = +1,636 MB | 189 MB → 203 MB = +14 MB | 1,622 MB (99.1%) |
| Post-script full recascade | 6,891 MB → 8,647 MB = +1,756 MB | 5,099 MB → 5,102 MB = +3 MB | 1,753 MB (99.8%) |

These are `MEMSTAGE` process-footprint deltas, so they include every allocator
touched during the cascade and are not an exact pre-change CSS-owner total.
The old revision predates the structured CSS counters. The current run supplies
that ownership attribution: its initial cascade retained 1,650,184 canonical
CSS bytes and 3,738 work-pool bytes; the forced recascade retained 1,652,868
canonical CSS bytes, added zero canonical bytes, and used 104 work-pool bytes.
Both samples had zero cold-cache entries and bytes. The current post-script
footprint was also 1,792 MB lower before recascade, but that interval includes
JavaScript and resource work and is not attributed to CSS here.

#### Residual 5.102 GB diagnosis and correction

The 5.102 GB figure did not represent retained CSS cascade state. The forced
recascade at that point added only 3 MB of process footprint, retained
1,652,868 bytes of canonical CSS, and had a 104-byte cascade work-pool delta.
The remaining process memory had two independent causes:

| Live allocation family in the stopped Brotli capture | Bytes | Share of attributed bytes |
|---|---:|---:|
| MIR compiler/generator structures for document JavaScript | 1,135,941,968 | 54.8% |
| CSS source parsing and tokenizer arrays | 548,500,688 | 26.4% |
| Other JavaScript parsing/binding | 191,830,080 | 9.2% |
| Other allocations | 187,434,512 | 9.0% |
| Pool VM reservations and DOM/font allocations | 10,297,984 | 0.5% |
| **Total stack-attributed live allocations** | **2,074,061,344** | **100.0%** |

macOS allocator diagnostics also reported about 994 MB allocated in the
default malloc zone, about 1.1 GB of allocator fragmentation, and 683 MB of
MIR VM allocations. The stack logger changes allocator behavior, so its total
is attribution evidence rather than a process-footprint replacement. Together
they explain why a 1.65 MB canonical store could coexist with multi-gigabyte
physical footprint.

The CSS tokenizer had reserved one 64-byte `CssToken` slot per source byte in
the loader pool. Its capacity now starts at eight tokens and grows only while
tokens are emitted. The regression test covers a 512 KiB comment, which emits
two content tokens plus EOF and therefore must not reserve proportional token
storage. The standard six-page cascade gate remains within its pre-existing
byte baseline. On the Brotli post-script snapshot, the tracked loader subtree
fell as follows; these pool figures are directly comparable ownership metrics.

| Post-script tracked state | Before capacity growth | After capacity growth | Change |
|---|---:|---:|---:|
| `cmd_layout` live bytes | 253,892,809 | 86,615,497 | -167,277,312 (-65.9%) |
| `cmd_layout` reserved bytes | 536,857,600 | 268,422,144 | -268,435,456 (-50.0%) |
| All tracked physical live bytes | 298,989,366 | 126,090,658 | -172,898,708 (-57.8%) |
| Canonical CSS live bytes | 1,652,868 | 1,650,184 | unchanged in practical terms |

#### Tokenizer scratch lifetime correction

Geometric capacity removed the pathological one-slot-per-source-byte bound,
but it did not close the lifetime. `pool_free()` coalesces a block for reuse;
it does not release the pool's VM extent. A token array allocated in the
document/loader pool could therefore remain reserved and committed after the
stylesheet parser had stopped using it.

`css_enhanced_parse_stylesheet()` now creates a tokenizer-only pool, builds
the complete token array there, parses semantic rules into `engine->pool`, and
destroys the tokenizer pool before returning the stylesheet. This makes the
token records, copied lexemes, numeric conversion strings, and their VM
extents parse-call scratch. It follows **D1.3v3**'s explicit ownership and
cleanup discipline: parser temporary storage must not become a hidden
document-lifetime owner.

The downstream audit found no retained `CssToken*` in stylesheet, rule,
selector, declaration, value, cascade, or epoch records. It did find retained
`CssToken::value` strings. Selector function/pseudo/attribute strings,
declaration property names, `@import` URLs, `@charset` values, generic at-rule
names, and `var()`/`env()`/`attr()` references now duplicate the needed text
into their semantic-owner pool before tokenizer scratch is destroyed. Values
already represented as `CssValue` scalars or owned strings continue to use
their existing copies.

The tokenizer record also no longer carries unused parse-location state.
`line` and `column` were only stamped then copied by the compatibility wrapper;
`is_escaped` and `unicode_codepoint` were neither written nor read. Removing
them and the per-byte `LineCounter` pass reduces `CssToken` from 64 to 48 bytes
on the profiled 64-bit target.

| Regression probe | Input / result | Required result | Observed result |
|---|---|---|---|
| Direct tokenizer release | 65,536 adjacent `/*x*/` comments, 327,680 source bytes, 65,537 emitted tokens | Releasing records and lexemes restores the pool's pre-tokenization live-byte count | Exact equality |
| Enhanced stylesheet lifetime | Same comment-only sheet | Engine pool retains less than 20,480 bytes after parsing | Passed; only stylesheet state remains |
| Semantic ownership after scratch reuse | `@charset`, `@import`, `@keyframes`, attribute/pseudo selector, and declaration property | Values remain valid after a 64 KiB allocation reuses freed tokenizer storage | Passed |

The standard six-page CSS cascade memory baseline also passes. These tests are
part of the normal Radiant test graph and do not select a build mode.

The post-change standard-host GitHub Brotli capture reported 1,650,184 bytes
of canonical CSS, 3,738 bytes of initial cascade work, and 104 bytes of
recascade work: the same persistent CSS accounting as the prior capture. The
structured samples occur after stylesheet parsing, so they deliberately do
not include the transient tokenizer extent. The direct-release and
scratch-reuse tests above provide that lifetime proof; the live capture proves
the release did not disturb persistent cascade state.

The larger defect was a broken execution-tier boundary. `script_runner.cpp`
intentionally selects the AST executor for a browser document so that all
callbacks share one closure ABI. Static modules bypassed the normal script
entrypoint, and `transpile_js_module_to_mir()` ignored that selection. Every
imported asset could therefore retain MIR compiler/generator state despite an
AST document realm. The module path now consults
`js_ast_interpreter_requested()` before lowering and dispatches to its existing
AST module executor. This completes the one-realm ownership rule and follows
**D1.3v3**: a hosted guest shares the host's accounting and lifecycle rather
than creating a parallel compiler-owner lifetime.

| Same URL, viewport, and no-op event file | Before tier propagation | After tier propagation | Reduction |
|---|---:|---:|---:|
| Post-script process footprint | 3,700 MB | 310 MB | 3,390 MB (91.6%) |
| Post-layout process footprint | 3,819 MB | 433 MB | 3,386 MB (88.7%) |
| Post-render process footprint | 3,823 MB | 454 MB | 3,369 MB (88.1%) |
| After cleanup process footprint | 1,016 MB | 87 MB | 929 MB (91.4%) |

Both captures reached the same 550-node layout result and completed the
headless event file. The live asset manifest is not frozen, so this table is
diagnostic evidence rather than a regression threshold. It is corroborated by
the script cache: the prior capture built 75 MIR images, while the corrected
capture built none for the document module graph. The focused
`JsInterpreter.StaticModuleHonorsRequestedAstBackend` test checks the same
dispatch and verifies that its direct static module creates no MIR code-store
entry.

## 5. Suggested implementation order and acceptance gates

| Stage | Deliverable | Gate before proceeding |
|---|---|---|
| 1 | **Done:** phase accounting, condition program reuse, bounded condition scratch | Repeated unchanged evaluation hits the bounded cache; uncached parser scratch is destroyed. |
| 2 | **Done:** explicit owned/pseudo retirement; current custom-property state | Focused repeated-cascade tests plateau after safe consumer release. |
| 3 | **Done:** mutation invalidation planner | Structural/text changes use the parent closure; broad relational dependencies take the correct full fallback. |
| 4 | **Done:** immutable payload/cascade-reference split | Payload count scales with unique source declarations; CSSOM/COW snapshot lifetime tests pass. |
| 5 | **Done:** compact property record, inline reuse, pseudo lifecycle | Removed duplicate AVL metadata, retained exact `InlineProp` reuse, and made pseudo publication/retirement explicit. |
| 6 | **Done:** bounded unbound cache and explicit replace/extend APIs | Unique-style churn respects the cold-cache budget; retained consumers block release correctly. |
| 7 | **Done:** computed-descriptor admission gate | Exact `InlineProp` remains shared; mutable groups remain excluded by the write/ownership audit. |

Some stages can be developed independently, but cache eviction must not precede
the necessary liveness contract, and borrowed payload sharing must not precede
immutable version ownership. The final representation should be chosen from
measured compact-block versus tree costs, not committed in advance of that gate.

### 5.1 Required correctness and memory tests

- Repeated no-change full cascades with ordinary, inline, custom, and pseudo
  declarations; live and committed growth should plateau after warm-up.
- Repeated calls with the same conditions, both true and false, nested
  conditions, viewport changes, color-scheme changes, and capability changes.
- Finite A/B state toggles versus many distinct styles; measure live bindings
  separately from cached history and allocator capacity.
- Different rule identities with the same values; different histories with
  the same current winners; source-order and selector-group specificity ties.
- Inline normal versus stylesheet important declarations, removal, CSSOM
  authored-text serialization, and preservation of inline writes on recascade.
- Shorthand/longhand combinations, pending variable substitution,
  logical/physical properties, `inherit`/`unset`/`initial`/`revert`, and supported
  layer behavior. Invalid-at-computed-value-time substitution follows CSS
  defaulting, not fallback to a previous declaration.
- Custom-property override/removal, case sensitivity, cycles, inherited
  computed environments, and registered properties where supported.
- Retained generated pseudo boxes during recascade, detach/reinsert, COW after
  source mutation, old/new epoch coexistence, and source-owner release.
- Forced hash collisions, allocation failure before publication, and attempted
  eviction while a builder or borrower still uses an entry.
- Attribute, sibling, ancestor, structural, relational, and text-selector
  invalidation; CSSOM reads during load; mutation-record overflow fallback.

Use existing style-epoch and CSS suites as the starting point, with the Radiant
baseline required for engine changes. Lifetime diagnostics may use a separate
sanitizer build; page memory comparisons use the configured standard host
(debug or release), without selecting a build mode in the test. Freeze local
HTML/CSS/script inputs for a future GitHub reproduction
and retain per-owner samples; do not attribute the historical gigabytes from
the synthetic probes alone.

### 5.2 Alternatives not recommended as first changes

- **Disable style epochs:** loses measured reuse and does not address owned
  resets or conditional scratch.
- **Start an epoch for every reflow/cascade:** conflates validity with lifetime,
  recreates snapshots, and still cannot reclaim pinned consumers safely.
- **Cache only duplicate recipes within one batch:** already lost useful reuse
  in the VR13 evaluation; prefer a bounded cross-batch cold cache.
- **Share current mutable declaration pointers or AVL branches:** violates
  source-order/CSSOM or structural mutation ownership.
- **Drop all losing declarations without reconstruction:** loses rollback and
  removal semantics.
- **Intern only final winner values and discard provenance:** not enough for
  future mutation or current shorthand-priority consumers.
- **Change the general allocator first:** the probes show objects that are
  never freed. Pool tuning cannot reclaim unreachable-but-still-allocated
  records on behalf of missing ownership logic.
- **Cross-document sharing initially:** introduces base-URL, stylesheet-version,
  resource, and document-lifetime coupling before local sharing is efficient.

## Appendix A. Profiling method and measured results

### A.1 Build, platform, and artifacts

The working tree was clean before profiling. Production sources were unchanged.
The optimized host was rebuilt with `make build-release-compile`, the incremental
release compilation target used by `make release`; release data/lib archives
were built with `config=release_native`. The standalone probe used generated
release flags, including C++17, `-O3`, `-DNDEBUG`, and thin LTO. No debug executable
was used for these measurements.

- Platform: macOS 26.3, arm64.
- Source revision: `0a46bd2129c6a4f5bf444bf5deec8a767c250126`.
- Host SHA-256:
  `a4da130e3c1b69fdd9d57ed5dd8ca60ed91bb19cc2b2c7a9a9a3b6558d65a71d`.
- Local artifacts: `temp/css_cascade_profile/` contains the probe source,
  generated fixtures, build command/logs, raw logs, and machine-readable data.
- Eight native cases were run twice in fresh processes. All 16 completed with
  identical allocation counters between repetitions.
- Twenty-four local page cases were run twice in fresh processes. All 48
  completed successfully; repeated allocator/epoch results were identical.

The temporary artifacts are local evidence, not a permanent benchmark suite.
The fixtures, procedures, and result tables below preserve the essential
experiment definition if those artifacts are later cleaned.

### A.2 Direct condition evaluation

Each process creates a document and CSS engine, measures the document pool,
then evaluates one fixed true condition 10,000 times. For direct attribution,
the probe gives the engine `doc.document_pool`; normal loaders may use a
different engine pool. No DOM matching, layout, or JS runs in these loops.

| Condition | Additional allocations | Frees during loop | Additional requested live bytes | Bytes/call | Final pool reserved bytes |
|---|---:|---:|---:|---:|---:|
| Media `(min-width: 1px)` | 20,000 | 0 | 340,000 | 34 | 2,081,792 |
| Supports `(display: block)` | 110,000 | 0 | 20,550,000 | 2,055 | 33,539,072 |

Both start at 4,565 live bytes and 17,408 reserved bytes. The slopes are exact
for these fixtures. Reserved bytes include allocator capacity and are not
interchangeable with requested live bytes or physical footprint.

### A.3 Repeated replace-and-recascade

Each process creates a synthetic root with 128 `div` children and parses one
stylesheet. It clears existing cascaded/pseudo styles and applies the same
stylesheet 100 times through the production cascade walker, using one matcher.
There is no layout or JS, and all parsed source allocations precede round 1.

Fixture declarations:

| Case | Stylesheet | Additional element state |
|---|---|---|
| Plain | `div { margin-left: 0px; color: black; }` | None |
| Inline | Same as plain | `style="width: 10px"` on each child |
| Custom | `div { --probe: 1px; margin-left: 0px; color: black; }` | None |
| Pseudo | `div::before { content: ''; color: black; }` | None |

| Case | Document live bytes, round 1 | Round 100 | Additional bytes, rounds 2–100 | Canonical live bytes, round 100 | Bound canonical element refs |
|---|---:|---:|---:|---:|---:|
| Plain | 21,894 | 21,894 | 0 | 2,900 | 129 |
| Inline | 325,638 | 6,002,694 | 5,677,056 | 2,200 | 1 |
| Custom | 105,872 | 7,404,944 | 7,299,072 | 2,200 | 1 |
| Pseudo | 133,784 | 5,810,840 | 5,677,056 | 2,200 | 129 |

The inline and pseudo slopes are **448 requested bytes per child per further
cascade**. The custom slope is **576 bytes** and one extra custom-property
record per child per cascade. The pseudo bindings in the table refer to empty
ordinary styles; their actual pseudo-style trees remain owned storage.

The plain case still performs allocation/free work: its document-pool allocation
count increases from 831 to 51,915 while live and reserved bytes plateau. This
is an opportunity to reduce temporary builders/empty-tree churn after fixing
retention, not evidence that canonical sharing fails.

### A.4 Current-epoch retention and additive recipes

The distinct-rule case parses 100 separate copies of the same two-declaration
rule before sampling. One child then visits those rules in 100 replacement
cascades. Thus source parsing is excluded from the measured cascade growth.

The additive case has one parsed rule and one child, but intentionally does
not clear between 100 applications. It characterizes the extension API only.

| Case | Canonical live bytes, round 1 | Round 100 | Cumulative entries after round 100 | Live bytes after all bindings unbound | Live bytes after new empty epoch |
|---|---:|---:|---:|---:|---:|
| Distinct rule identities, same declaration contents | 2,900 | 72,200 | 101 | 72,200 | 2,200 |
| Repeated additive application | 2,900 | 1,495,096 | 200 | 1,495,096 | 2,200 |

After unbinding, both have zero canonical element refs. Starting the next epoch
releases the previous pool and leaves a new empty canonical style. The
cumulative entry counter does not reset, which is why it must not be used as
a live-entry gauge.

### A.5 End-to-end local page checks

Pages contain 100 or 1,000 empty `div` elements and 32 repeated rules. Plain,
media, and supports versions have the same declarations, with the latter two
wrapped in the conditions above. Inline/custom/pseudo variants follow A.3.
The scripted version appends a script that sets an unreferenced `data-profile`
attribute on `body` twice. It exercises production loading and script-related
cascade checkpoints; it is not a controlled measurement of exactly one extra
cascade.

For 1,000 elements, the existing profiler reports:

| Case | Selected-pool live bytes, static | Selected-pool live bytes, scripted | Canonical live bytes, static/scripted |
|---|---:|---:|---:|
| Plain | 797,349 | 133,678 | 13,408 / 13,408 |
| Media | 797,349 | 133,678 | 13,408 / 13,408 |
| Supports | 797,349 | 133,678 | 13,408 / 13,408 |
| Inline | 11,340,141 | 24,948,470 | 2,200 / 2,200 |
| Custom | 11,850,141 | 33,170,470 | 2,200 / 2,200 |
| Pseudo | 8,771,525 | 22,164,854 | 2,200 / 2,200 |

These are the profiler's selected-pool `physical_total.live_bytes` values,
**not process physical footprint**. The endpoint mixes style and view storage;
the plain scripted value is lower, so it is not a valid isolated cascade delta.
Identical media/supports totals do not refute condition retention: the report
does not isolate their loader/engine pool. The direct probes in A.2/A.3 provide
the controlled attribution.

An additional attempt to collect `/usr/bin/time -l` resource statistics could
not obtain them because the environment denied `sysctl kern.clockrate`. The
later live GitHub capture in §4.1 instead uses the existing `MEMSTAGE`
footprint metric. It records comparable before/after cascade deltas without
claiming an allocator-owner split for the pre-change revision.

### A.6 Reproduction with retained local artifacts

Run from the repository root; keep all scratch under `temp/`:

```sh
mkdir -p temp/css_cascade_profile
TMPDIR="$PWD/temp/css_cascade_profile" make build-release-compile
TMPDIR="$PWD/temp/css_cascade_profile" make -C build/premake config=release_native lambda-data-cpp -j10
python3 temp/css_cascade_profile/build_probe.py
python3 temp/css_cascade_profile/run_probes.py
python3 temp/css_cascade_profile/run_pages.py
make test-css-cascade-memory
```

`build_probe.py` obtains flags from the generated release make configuration;
it does not edit generated Lua/build files. `run_probes.py` checks fresh-process
repeatability and writes `probes.json`; `run_pages.py` writes `pages.json` and
the individual profiles. If rebuilding changes the host hash, preserve the
new source revision, flags, and hash with the new results rather than mixing
them with this baseline.

## Appendix B. Implementation map

Line references identify the reviewed revision; symbols are the durable anchors.

| Source | Relevant symbols and role |
|---|---|
| `radiant/cmd_layout.cpp:1413, 1521, 2012, 2119` | `clear_load_stylesheet_cascade_visitor`, `apply_load_css_cascade`, loader engine pool, unconditional mutation-triggered full recascade. |
| `radiant/css_cascade.cpp:13, 95, 133` | `apply_rule_to_element`, `apply_stylesheet_to_tree`, `radiant_apply_css_stylesheets_to_tree`; conditional evaluation and element/rule traversal. |
| `lambda/input/css/css_engine.cpp:619, 662` | `css_evaluate_supports_span`, `css_evaluate_media_query`; retained parsing/string scratch. |
| `lambda/input/css/css_tokenizer.cpp`, `css_engine.cpp` | `css_tokenize`, `css_tokenizer_tokenize`, `css_token_array_release`, and the stylesheet-local tokenizer pool; records/lexemes are parser scratch while semantic owners receive explicit copies. |
| `lambda/input/css/style_epoch.cpp:85, 145, 236, 276, 292, 356, 432, 503, 618` | Environment key, release gate, eligibility, rule validation, recipe materialization, identity, canonical miss, COW, stats. |
| `lambda/input/css/css_style_node.cpp:187, 502, 548, 684, 814, 829, 946, 1272, 1339` | Shallow cascade record versus owned snapshot, unref, node allocation, clear, insertion/source order, inline filtering, owned clone/destruction. |
| `lib/avl_tree.c:641, 650` | `avl_tree_clear`, `avl_tree_insert`; forgotten root and separately allocated wrapper. |
| `lambda/input/css/dom_element.cpp:573, 614, 1416, 1470, 1522, 1542` | Clear, pseudo borrowing, custom-property insertion, rule application, pseudo clear/application. |
| `radiant/event.cpp:6016, 6214, 6269, 6545` | Existing mutation cascade-root selection, incremental eligibility, subtree recascade, event integration. |
| `radiant/view_memory_profile.cpp:110` | `view_memory_profile_write`; reusable reporting infrastructure and selected-owner limitations. |
| `radiant/view_reuse.cpp:104` | `canonical_inline_find_or_create`; existing exact computed-prop interning and cap. |
| `test/test_view_reuse_gtest.cpp:309` | `StyleEpochTest`; collisions, COW, epoch coexistence, environment changes, and snapshot lifetime. |
| `test/test_css_cascade_memory_gtest.cpp` and `test/css_cascade_memory_baseline.tsv` | Standard page-load and forced-recascade memory baseline; exact recipe/binding ceiling plus byte budgets. |

The first implementation should concentrate on the measured condition and
owned-style retention paths, while preserving the demonstrated plain-recipe
reuse. Larger representation changes then have a controlled baseline and
explicit ownership gates.
