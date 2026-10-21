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
**Extension (2026-10-20):** native incremental computed/used resolution, §3.45;
retained style, sizing and layout architecture, §3.47 (common continuous-document paths qualified 2026-10-21; conservative adapters documented there).
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

### 3.12 Bound substitution work and project complete computed text

The per-value byte/component budgets and scoped dependency cache also bound
named dependency depth (default 128, configurable per document). Include the
remaining depth in each dependency-cache key: a value memoized on a shallow
path must not make a longer path appear within budget. Exceeding the budget
produces an invalid computed custom value; consumer fallbacks and registered
defaults use the ordinary shared resolver. Unused fallbacks stay lazy.
Inheritance follows declaration owners iteratively, so DOM ancestry does not
consume the named-dependency budget. Separately guard recursive value/token
walks against native-stack exhaustion, following the existing
[memory-safety recursion contract](../Memory_Safety_Template.md#53-recursion-depth-librecursion_guardhpp).
The depth policy is a UA resource limit, not a depth mandated by CSS; see
[CSS Variables §3.3](https://www.w3.org/TR/css-variables-1/#long-variables).

Computed accessor callbacks write growable text. Both the full String API and
bounded native API use that computation; a bounded read reports failure and
leaves empty output if the complete value does not fit. Intermediate formatting
and substitutions stay in scratch storage; only the result crosses into the
caller's pool, under **D4.5.1v4 / D4.1.4v5**. No formal ownership ruling changes.

### 3.13 Size-container delivery contract

The P1.1 size-query implementation extends §3.2's existing container-dependent
key contract. Keep parsed conditions in the CSS/input owner; supply layout
measurements through an explicit environment callback, preserving module
boundaries under **D7.3.3–D7.3.4**. The selected view owns query measurements,
dependency state and stabilization work under **D4.5.1v4 / D4.1.4v5**. An
independent print/secondary view never reads browsing geometry to answer a query.

For a size query, select a strict flat-tree ancestor with the requested name
and containment on every axis required by the condition. Use its content-box
dimensions and writing mode. Preserve container blocks in the active rule
program, as with target-dependent scope blocks; do not flatten their children
using a document-global condition result. A cached result includes condition
version, selected container identity, measurement/style generation and the
font/environment inputs it reads. Missing or unavailable query measurements
produce an unknown query result, which does not apply its conditional rules.

Apply the ordered stylesheet program to each ancestor before visiting its
descendants. A stylesheet-major walk exposes only a prefix of the ancestor's
font/custom-property cascade to an early query. The allocation-failure reference
walker must preserve the same order. Compute the complete query value, including
substitution, before checking its numeric dimension; a width declaration's
nonnegative range is not the grammar of a size-query comparison.

Container units perform the corresponding eligible-ancestor search for each
required axis; the nearest containers for the two axes can differ. When no
eligible container exists, use that axis's small-viewport size, as specified
by [CSS Conditional Rules §7](https://www.w3.org/TR/css-conditional-5/#container-lengths).
Names, axis selection and content-box measurements share the same provider
between query matching and length computation, including font-relative math.

Initial layout and subsequent container changes must stabilize query styles
and geometry before publishing a committed view or dispatching motion events.
Size containment and strict ancestor selection give the dependency graph its
direction; process affected descendants in that order. An implementation that
needs staged passes must retain measured inputs across those passes, detect
non-convergence and report failure rather than silently publish an arbitrary
iteration. DOM/style/font/viewport changes invalidate both measurements and
dependent styles. Style and scroll-state queries retain their separate model
prerequisites in P3.4 of the implementation plan.

Secondary editions retain a previous-pass content-size table outside their
replaceable CSS/layout scratch. Entries carry validated `DomNodeRef` identity;
the edition owns both tables, and external reset/release destroys them with that
generation (**D4.5.1v4 / D4.1.4v5**). Internal rejected passes preserve only this
measurement input while rebuilding styles and fragments. Query values use the
selected ancestor's edition-local font and custom-property state.

Capture measurements from accepted compositor state after a complete pass.
Block-frame measurements participate in the existing checkpoint journal, so
speculative page trials cannot leak sizes into the next pass. Definite heights
retain the pre-fragmentation content extent; automatic heights accumulate
accepted content across fragments, excluding cloned decoration. Container
eligibility still restricts which axes can be queried. Rewind page-policy
callbacks before another pass, and commit only after reference bindings and
container measurements both settle. A caller-visible condition-pass budget
reports exhaustion without committing provisional material.

### 3.14 Containment consumers

Intrinsic containment CSSOM reads preserve each axis's `auto` mode and `none`
fallback while computing lengths in the declaration owner's font/unit context.
Physical and logical longhands, the shorthand, and generated pseudo styles use
layout's shared component cascade, including `all` and layer rollback. Explicit
inheritance follows the parent's corresponding logical property; a pseudo
inherits from its originating element. Equal computed axes compress to one
shorthand component. Computed reads never substitute remembered used dimensions
for the authored fallback (**CI-18 / D4.5.1v4 / D4.1.4v5**).

For intrinsic measurement, `auto <length>` uses its explicit length when no
usable remembered size exists; `auto none` retains empty size-containment
contributions. The remembered-size branch keeps precedence. These are existing
[CSS Sizing 4 §5.2](https://drafts.csswg.org/css-sizing-4/#intrinsic-size-override)
consumer requirements, not a new Lambda ruling. Cross-realm/independent CSSOM,
full memory lifecycle, and table/ruby/replaced/fragmentation breadth remain
separate qualification work.

Layout and paint containment establish an independent formatting context.
Paint containment clips descendants at the overflow clip edge, with corner
clipping and `overflow-clip-margin`; computed overflow keywords remain authored.
This follows [CSS Containment 2 §3.4](https://www.w3.org/TR/css-contain-2/#paint-containment).
Use the existing shared block-local overflow geometry for rendering and hit
testing rather than creating a second containment clip. A paint-contained box
does not need a scroll pane to clip, and removing containment must restore its
ordinary used geometry after recascade.

Containment flags are generation-local computed state under **D4.5.1v4 /
D4.1.4v5**. Reset them before applying current winners; retain remembered
intrinsic sizes separately when the visibility implementation needs them.
Store the computed `contain` modes separately from size containment implied by
`container-type`; explicit inheritance copies only the former. Keyword sets
validate before parsing a declaration and again after substitution; computed
reads use canonical keyword case/order while container names remain sensitive.

Layout/paint containment creates a zero-level stacking context even for static
boxes. The shared paint walk defers these boxes with other zero-level contexts;
hit testing reverses the corresponding paint order, including ordinary siblings
that overlap through negative margins. Descendant overflow clips begin after
the box boundary is painted. PDF clip depth survives streamed paint batches,
and SVG/PDF retain the same rounded solid-border path as raster. Elliptical
corners constrain both descendant clips and the border's own hit region, per
[CSS Backgrounds 3 §4.3](https://www.w3.org/TR/css-backgrounds-3/#corner-clipping).

Contained horizontal column flows retain their child margins during ordinary
measurement. Fragmentation recognizes the final child's margin and an empty
block-end gap as break opportunities, truncates margin-only continuations, and
keeps fractional balance minima. Fixed sizes retain their logical extent;
size-contained boxes, scroll containers and `break-inside: avoid` keep their
existing atomic behavior. These controls follow
[CSS Fragmentation 3 §4.1/§5.2](https://www.w3.org/TR/css-break-3/#possible-breaks).

Reverse hit traversal cannot let an editor's margin-caret fallback claim
painted siblings or content outside its parent. SVG text targeting follows
authored style ancestry across anonymous layout wrappers, including wrappers
inserted after the style snapshot was indexed. Source-less layout placeholders
have no Lambda attribute-map identity under **D4.5.1v4**. Shared ancestry
collection uses checked growable storage rather than silently dropping ancestors.

Complete stacking/depth ordering, non-principal boxes,
secondary-view geometry/paint containment and `content-visibility: auto`
still require their own observable controls before complete containment support
can be claimed.

### 3.15 Style containment and generated-content scopes

Style containment creates a counter mutation boundary for the element's
subtree, after its own counter reset/increment/set operations. Reads retain
outer counter instances; an increment/set without a local instance creates
one at the subtree boundary, initially zero. Nested resets keep normal CSS
Lists sibling inheritance within that boundary. No subtree counter state is
published back into its containing element's scope. `container-type: size`
and `inline-size`, and skipped content, imply this same boundary.

Quote depth starts at the enclosing depth, advances in flattened generated
content order, and is restored on leaving a style-contained subtree. Both
browsing and independent compositions use the shared content evaluator and
counter-scope implementation. Scope ownership and generated text follow
**D4.5.1v4 / D4.1.4v5**; pass-local snapshots never become document-owned pointers.
Repeated materialization retains unchanged text rather than allocating a
replacement document string. Counter/quote checkpoints must also bound
measurement trials and early returns.

The browsing owner is `LayoutCounterScope`, shared by block, inline and final
flex/grid item layout. The list item's own implicit counter increment precedes
the style boundary; its generated marker follows it. Final item content uses
a retained flattened source order, independently of visual CSS `order`.
Stretch measurement temporarily clones active counter maps in a checked
`CounterCheckpoint`, restoring maps, active frames and quote depth before
final content. The scratch checkpoint does not retain pointers into a retired
style or view generation. Independent views use the same counter boundary
around their existing source-order composition scopes.

The semantics follow [CSS Containment 2 §3.3](https://www.w3.org/TR/css-contain-2/#style-containment)
and the pinned WPT `css/css-contain/counter-scoping-*` / `quote-scoping-*`
cases. This contract does not claim complete non-principal containment or
`content-visibility: auto` relevance handling.

### 3.16 Content visibility delivery contract (partially implemented)

The computed `visible`, `hidden`, and `auto` states remain distinct from the
used skipped state. Applicability follows size-containment boxes: principal
tables, internal table/ruby boxes and non-atomic inline boxes are excluded;
table captions remain eligible. Hidden/auto imply layout, style and paint
containment; skipped contents additionally imply size containment. A skipped
subtree contributes neither descendant paint/hits nor outer counter effects.
Hidden contents cannot be focused, selected or reached by tab navigation.
Auto contents retain those user-agent routes and become relevant when focused,
selected, in the top layer, or near/intersecting an applicable scroll viewport.

Relevance and remembered inner sizes belong to the selected view. Reuse the
existing last-remembered-size computation and containment measurement helpers,
while removing their assumption that a DOM-wide default-view size is suitable
for every view. State keys include validated DOM identity. Geometry reset
preserves committed history in the same edition. Disconnection immediately resets
proximity/event history, including a synchronous remove-and-reinsert. Remembered
sizes survive that brief disconnection, but a rendering update while disconnected
retires the entry. Adoption and teardown retire stale references under
**D4.5.1v4 / D4.1.4v5**.
Before first geometry exists, auto contents are measured. Subsequent layout
settles relevance and placeholder geometry with a checked pass budget. An
explicit activation margin is a user-agent policy, rather than an authored
length or a hidden test-specific threshold. Scroll, resize, font/content,
focus/selection and CSS mutation changes must schedule recomputation without
requiring an unrelated DOM edit. Printing/paged views make their printable
content relevant independently of the browsing viewport.

Skipping retains reusable layout state, but its stale descendant boxes cannot
participate in paint or hit traversal. Intrinsic `auto` placeholders use the
last committed inner size of that view; ordinary explicit fallback lengths
retain their CSS Sizing behavior. Measurement trials cannot publish remembered
sizes, quote/counter changes or new motion timelines. Skipping and activation
suppress transition creation for the associated style change, while existing
animation phase/lifetime follows the shared motion owner.

Remembered sizing is independent per physical axis: removing that axis's `auto`
clears its memory when an eligible box is updated, including a skipped box.
No box or an unrendered ancestor preserves the old memory. Only final,
uncontained, non-skipped geometry can replace it; explicit size containment
must not turn a fallback into remembered content dimensions. These controls
follow [CSS Sizing 4 §5.2.1](https://drafts.csswg.org/css-sizing-4/#last-remembered)
and pinned WPT `contain-intrinsic-size/auto-006.html` / `auto-009.html`.
The current Chromium reference overwrites memory during ordinary size
containment; retain that difference as qualification evidence against the
draft instead of making a browser comparison a substitute for the contract.

Acceptance must exercise visible clipping/BFC/counters, initially offscreen
placeholders, activation by scroll/focus, remembered-size changes, nested
scrollports, live auto/hidden/visible switches, and independent-view printing.
The rules follow [CSS Containment 2 §4](https://drafts.csswg.org/css-contain-2/#content-visibility)
and [CSS Sizing 4 §5](https://drafts.csswg.org/css-sizing-4/#intrinsic-size-override).
The browsing increment retains auto descendants' geometry for user-agent
queries while suppressing their external sizing, paint and hit effects. The
activation margin is one viewport extent, and the checked convergence budget
is 256 passes per layout. These are explicit user-agent policies. History is
view-owned and survives the loader's geometry allocator replacement; ordinary
view destruction releases it. Query trials and skipped ancestors cannot
refresh committed intrinsic-size memory. Resize observations retain their last
delivered size; intersections remain empty, including within a skipped root.
Rendered text excludes skipped contents while raw textContent stays available.

Intrinsic-size components share strict axis grammar across validation and
consumers. `none` clears the fallback on its axis; `auto none` retains remembered
size behavior with no explicit fallback. Browsing intrinsic measurement and
used sizing share component cascade winners across shorthand and logical axes;
inheritance copies computed owner lengths. Typed math uses the common length
resolver. Final flex content refinement cannot reintroduce a contained height.
Explicit skipped-motion reads refresh existing endpoints from current owner
declarations and fonts without advancing their lifecycle or creating effects.
Skipped effects retain clock service. The current dirty-callback regression
covers opacity with inherited variable endpoints; complete font/leading/SVG
query contexts remain unqualified.

The independent-view increment stores computed visibility and intrinsic
axis fallbacks in `ViewCssStyle`, using that edition's cascade, fonts and length
context. Hidden block contents, including pseudos, produce no children while
their own placeholder and decorations remain. Printable auto content remains
relevant independently of browsing position. Counter boundaries use the same
visibility applicability; no temporary default-view geometry is published
(**D4.5.1v4 / D4.1.4v5**).

Independent remembered sizes use the same weak-identity history owner as
browsing, with each edition retaining its own dimensions. Final model commit
stages a checked copy of valid history, applies completed principal inner-box
measurements, and publishes it only when the complete update succeeds. Failed
composition, reference/container trials and open model checkpoints cannot
publish memory. Hidden placeholder consumers consult committed memory only
for axes retaining `auto`; computed fallback/inheritance remains unchanged.
Retained page leases keep their immutable geometry/fonts while sizing history
follows the stable active edition across arena replacement. Page instances
do not publish a second copy of source measurements. Complete fragmentation
and applicability controls remain separate qualification work.

Committed browsing auto states queue typed `ContentVisibilityAutoStateChangeEvent`
notifications using the existing traced timer owner. The readonly `skipped`
accessor uses a branded native payload; constructor/dictionary conversion,
subclassing and getter exceptions have a JS golden. Initial observation,
scroll changes, inline handlers, removal and reinsertion have a live fixture.
The queued target is validated again and detached targets are discarded;
callbacks run after layout unwinds (**D5.3.3 / D4.3.3**). A connected element
retains proximity history while auto is disabled or its box is absent.
Chromium differs on initial notifications and native event flags; these remain explicit
qualification limits against [CSS Containment 2 §4.4](https://drafts.csswg.org/css-contain-2/#contentvisibilityautostatechange).
`scrollIntoView()` consumes placeholder geometry inside the final convergence
loop, then settles any post-scroll relevance changes before publishing motion,
remembered sizes, geometry and paint. It cannot rely on a subsequent author
mutation to activate contents. A container/descendant scroll fixture checks
used sizes, viewport position and real pixels; browser deactivation hysteresis
remains a documented user-agent policy difference.

Broader independent size-memory lifecycle and paged fragmentation,
top-layer relevance, complete event/task-order interoperability and complete applicability,
forced-geometry, explicit motion-query contexts and lifetime qualification
remain open. The support row stays partial.

### 3.17 CSS image selection and retained paint payloads (partially implemented)

**CI-17** follows [CSS Images 4 §2.4](https://drafts.csswg.org/css-images-4/#image-set-notation):
validate every `image-set()` option before cascade, including optional resolution
and `type()` descriptors, their order and uniqueness, and forbidden direct or
indirect nesting. Omitted resolution is 1x. Unsupported MIME types are removed
before equal-resolution candidates are considered; the first remaining candidate
at each resolution wins. Zero-resolution options are invalid images. Typed
resolution math uses the shared evaluator and its range rules.

Use the selected view's device scale. The current selection policy is the
smallest sufficient density, otherwise the largest available; share that policy
with HTML responsive-image selection. CSS consumers compare canonical densities
at the precision they retain, including equivalent dpi/dpcm/dppx/x forms. Raster
natural dimensions are divided by the selected resolution; vector natural
dimensions keep their CSS Images sizing rules. A resource's own format remains
the authority for that distinction, independently of an author's MIME hint.

A selection result borrows parsed values only during computation. Retain its
resolved URL or resolved gradient in the consuming view's owner, under
**D4.5.1v4 / D4.1.4v5**. Explicit background inheritance clones owned gradient
objects, stop arrays and layer arrays. Retiring either parent's or child's
payload cannot invalidate the other. Image-only inheritance preserves the
child's independent color and background geometry. Replacement releases the
previous payload after the complete replacement is available.

Generated replacement images use the same source admission and resource cache
as HTML images. Revalidate document policy before warm-cache reuse, and retain
the selected source independently of transient declarations. Candidate/URL
changes refresh the borrowed surface and natural dimensions before sizing.
Generated content must not force valid automatic image dimensions to zero.
Resolved URL scratch is released after admission retains the selected source;
reflow must not accumulate a new view-pool copy on each visit.

A single image in `content` replaces an element or pseudo-element, as specified
by [CSS Content 3](https://drafts.csswg.org/css-content-3/#content-property).
The draft records the historical browser treatment of `::before`/`::after` as
inline content and the unresolved [CSSWG issue 2889](https://github.com/w3c/csswg-drafts/issues/2889).
Chromium 152's image pseudo has a two-CSS-pixel inline baseline offset in the
bounded export fixture, while Radiant's replaced pseudo agrees with the explicit
replacement reference. Retain this interoperability difference in qualification;
the image-selection result alone does not establish pseudo-layout parity.

An explicitly requested export device environment reaches SVG/PDF sessions as
well as raster sessions. Export density remains independent of asset selection
([RSC6–RSC7](Radiant_Scale.md#rsc6--paint-semantics-are-logical-raster-lowering-is-physical)).
Decoded raster resources use the common inline image payload when exported to
SVG, including admitted file URLs; relocatable output cannot depend on the source
file remaining beside the exported document. Producers request lazy decoding
before recording pixels and preserve decoded dimensions independently of natural
size. An absent source URL cannot suppress a decoded image.
Fallback glyph advances and bitmap placement convert physical font resources
to CSS coordinates exactly once. The same conversion feeds SVG/PDF text-width
and emphasis calculations; explicit device density cannot enlarge logical text.

Raster, SVG and PDF image backgrounds share positioning/paint boxes and the
tile-size/position/repeat plan. Vector exports record owned clips and the common
image resource/picture paint commands, so deferred effects retain their clip
lifetime and selected image dimensions. This does not establish complete
multilayer ordering, blending, transformed sampling or standalone-resource
qualification. URL list/border images use the same selector and retain raster
density in their used natural size and `border-image-width:auto`. Resolve their
URLs against the declaration's stylesheet base. A valid marker image precedes
the marker string, including `list-style-type:none`, per
[CSS Lists 3 §3.2–3.3](https://drafts.csswg.org/css-lists-3/#content-property).
Explicit image `none` terminates inheritance. Retained markers refresh their
borrowed computed image/cache surface and generated-content suppression after
style changes. An absent marker leaves the rendered tree while retaining its
owner until ordinary generated-node retirement (**D4.5.1v4**).
Retained marker fonts keep their element owner; the optional temporary font
owner starts empty when reuse needs no allocation (**D4.1.4v5**).

Explicit border-image inheritance owns independent URL/gradient/stop copies;
replacement releases the previous owner after constructing the new payload.
Raster, SVG and PDF share nine-slice geometry, region clips and source crops.
Deferred gradient tiles own their paths and stop arrays. The solid boundary
fast path must yield to an image source, including its resource-load fallback.
URL marker export shares its used image rectangle and resource producer.
Native physical marker painting scales logical dimensions once under RSC6.
URL marker painting consumes the active transform. Border slice clips use their
tiles' transform; inherited viewport clips already occupy projected coordinates.
Export extents compose the same transform matrices for boxes and overflow clips
before integral surface allocation. Bounded translation controls cover these
consumers; broader transformed sampling and projective export remain open.

Gradient list images share the used marker rectangle and image producer across
raster, SVG and PDF. Linear/radial commands own their path and copied stops;
conic/repeating-radial commands own generated pixels until all deferred consumers
release them (**D4.5.1v4**). Coincident stops select the following color at their
boundary. Software sampling stores premultiplied pixels; SVG encoding and PDF
soft-mask images receive straight channels. Linear repetitions reuse the checked,
heap-owned CSS stop expansion. A zero period uses equally spaced stops for the
premultiplied average; a linear period smaller than one physical output pixel
uses its distance-weighted average, per
[CSS Images 3 §3.3](https://drafts.csswg.org/css-images-3/#repeating-gradients).
Resolved periods retain their stop sequence; capacity/allocation failure must
not silently substitute an approximate solid color or allocate an attacker-sized
stack array. Software interpolation reuses the premultiplied sRGB color helper.
Subpixel radial/conic repetitions and density decisions under accumulated
ancestor magnification remain unqualified.

Export effect captures lower logical PaintIR and page backdrops at the selected
page density once, then replay through the shared budgeted snapshot path. Capture
bounds are physical pixels; their embedded destination stays logical. Snapshot
replay owns a real scratch arena, so saved backdrops and blur intermediates can
allocate; a null backing must not silently suppress their work. Effect groups
composite premultiplied foregrounds before the snapshot returns straight pixels. Shared
replay preserves gradient spread/ellipse transforms and path clipping in PDF,
including transparent stops, rather than approximating gradients over white.
The bounded controls cover constant, hard-stop repeating and degenerate marker
images, translation, identity brightness, transparent colors and device scales
1x/2x. This does not qualify complete gradient grammar or color interpolation.

Replaced bitmap images and Canvas pixels share fitting, clipping and the
semantic image producer. Decode file sources before the owned PaintIR snapshot;
export effect replay cannot obtain a later borrowed decode. Canvas drawing
surfaces advertise their premultiplied channels, and CPU blits filter those
channels before source-over composition while respecting destination alpha.
Affine and projective vector image replay uses the shared pixel-center sampler.
The replaced content and its decorations occupy one block effect scope, so CSS
group opacity is applied once. Document surfaces remain synchronous borrows;
deferred paint owns snapshots under **D4.5.1v4 / D4.1.4v5**.
Content clipping uses the same containment test as raster painting; coincident
image/content edges do not add a second PDF clip that compounds antialiasing.

Acquiring a WebGL Canvas surface refreshes dirty GPU pixels before the semantic
producer takes its owned snapshot. The document preparation pass calls the same
helper before retained raster ancestors can bypass traversal. Unchanged reads
reuse the publication; replacement retires its generation while outstanding
leases keep the previous pixels alive. Vector output can therefore paint first,
without depending on a prior raster pass. Native graphics still requires the
full GLFW host; headless file controls explicitly enable its existing
`LAMBDA_HEADLESS_GLFW_WINDOW=1` option. Default null-host rendering does not create
a GPU context. Inactive platform backends and broader resampling need separate
qualification.

Bounded 2D Canvas controls cover opaque/translucent pixels, saved drawing state,
translation, identity brightness and group opacity at 1x/2x. PDF comparisons
retain a precision limit: Poppler's translucent soft mask plus constant alpha
can differ by one color level from a precombined mask byte. Opaque controls use
independent baked-alpha PNGs; translucent controls retain the same alpha
representation for exact comparisons. WebGL controls additionally distinguish
premultiplied/straight drawing buffers and preserved/discarded buffers; host
availability and lifetime checks are required alongside matching export pixels.

An image without natural dimensions uses a 1em-square marker default object size,
per [CSS Lists 3 §3.3](https://drafts.csswg.org/css-lists-3/#list-style-image).
The current Chromium 152 reference uses a smaller gradient marker and different
baseline placement. It also returns the final stop for the recorded zero-period
linear/radial/conic controls, differing from the specified average. Retain those
browser differences rather than crediting failed assertions as parity.

Selection occurs when the CSS image recomputes. A pure device-scale repaint may
retain its earlier selected density under CSS Images' UA selection policy;
subsequent declaration changes use the current environment. This preserves
[RSC10](Radiant_Scale.md#rsc10--scale-changes-do-not-automatically-imply-reflow).
Chromium agrees with the 54 causal mutation/density/translation controls. Outside URL-marker
placement remains different in the bounded browser probe, despite matching
selected dimensions; do not credit that probe as placement parity. PDF profiles,
edge rasterization and per-format canvas padding differ. Exact image references
are compared within each output format, with independent selected-color coverage
to exclude matching omissions.

Complete image-function/gradient-body grammar, radial/conic and SVG URL border
sources, multilayer descriptors, generated-gradient
replacement, CSSOM serialization, registered values, transformed sampling and
independent/paged CSS-image producers retain separate acceptance controls.

### 3.18 Ordered background layers (delivery contract; implementation pending)

Resolved image objects and stop arrays follow **D4.5.1v4 / D4.1.4v5**;
deferred PaintIR owns its paths, stops and generated pixels. **CI-18** follows
[CSS Backgrounds 3 §2.1–2.3](https://www.w3.org/TR/css-backgrounds-3/#layering):
retain an authored-order image list, including `none` and unavailable images.
Paint from the last layer to the first, above the single background color.
Each layer keeps its position, size, repeat, origin, clip and attachment;
shorter longhand lists repeat from their first entry, while surplus entries
do not create images. The color uses the last image layer's clip. Shorthand
omissions reset the corresponding components and compete with longhands
through the existing cascade, including importance and rollback.

Use one ordered owned descriptor shape for URL, selected image and gradient
sources. Preserve component-list identity through computation so changing the
image count does not lose repeated longhand values. Explicit image inheritance
clones the source payloads independently and preserves the child's other
background components. Construct a complete replacement before retiring the
previous owner. `none` retires its image payload while preserving its layer
index for component-list matching.

Reuse the shared gradient-image producer and tile plan in raster, SVG and PDF.
No backend may regroup layers by image kind or omit the list in favor of its
scalar projection. Native physical painting converts logical rectangles and
the active transform exactly once; deferred exports retain owned clips.
Keep blend boundaries tied to each authored layer and the background's isolated
backdrop. Wider gradient grammar and unsupported blend operators need their own
acceptance evidence.

The frozen pre-implementation mixed-layer probe fails 14/18 comparisons: linear/radial
ordering fails in both declaration forms, and vector consumers omit all six
mixed lists. Acceptance requires independent color/position references,
URL/gradient mixing, empty layer indexes, repeated component lists, cascade
overrides, live replacement/inheritance, retained release and 1x/2x effects
through all three outputs. The contract does not promote the support matrix
until those controls pass.

Implementation progress (2026-10-12; qualification pending): the ordered owner
and independent computed component periods now feed the common tile/gradient
producer. The current candidate matches all **18/18** original PNG/SVG/PDF
ordering references. Six focused native controls pass, including surplus-value
inheritance when parent/child image counts differ, parent retirement and 256
replacement cycles. Full native vector and view controls pass **127/127** and
**604/604**. Constant gradient images record an owned solid path; varying
gradients retain stop/pixel ownership controls. Background blending uses an
isolated backdrop and shared raster replay for export composition.

The new 1x/2x export group contains **24 pairs / 72 comparisons**. All raster
and SVG comparisons pass; **two 2x PDF blend comparisons still fail at the
reconstructed image boundary**. Transparent-padding and explicit PDF mask
experiments retain their raw failures and are not part of the source fix.
The candidate's Radiant gate passed **4,290 full / 349 partial**; the subsequent
owner-publication ordering cleanup rebuilt and passed **604/604** view controls
again. Its matching 127-input isolated ASan build passes **1,067 native cases**.
The next increment fixes axis-list inheritance by retaining independent X/Y
periods before layer projection. Chromium verifies the two-/three-entry periods
when a child inherits into four images. Parent retirement, restyling, shorthand
competition/importance and variable/defaulting controls pass; four visible-tile
pairs pass **12/12** PNG/SVG/PDF comparisons at 1x/2x. Native suites pass
**606/606** views and **127/127** vector cases. Its matching 127-input ASan build
passes **1,069 native cases**, the Canvas JS golden and all 12 axis pixel
comparisons. The instrumented ordered-background group retains the two PDF
boundary failures without an ASan report; the ordinary runtime stays unchanged.
These results do not replace the earlier aggregate's provenance. Full
positions/math, broader URL/attachment/transformed
sampling, live changes and motion/serialization still need acceptance evidence.
These controls do not qualify the complete family.

The full-position follow-up uses a shared physical-position descriptor for
keyword order, three-/four-value edge offsets and axis longhands. Parsing and
computation share this descriptor. Linear math retains separate percentage/px
terms; nonlinear percentage math owns a computed tree and evaluates the remaining
percentage against each tile's free space (**D4.5.1v4 / D4.1.4v5**, **CI-18**).
The 21/24 failing predecessor probe improves to 36/36 passing comparisons in an
extended 12-case probe. Its native view suite passes 609 cases, including inherited
font-relative lengths and parent retirement. Durable 1x/2x position exports pass
72/72 comparisons; a document-scoped quirks refinement and fresh ASan/aggregate
qualification remain in progress. Broader size/gradient geometry, mutation,
attachment/sampling and motion/serialization acceptance remains open.

The next position revision also confines quirky bare-number lengths to their
document/property context, retaining standards-mode/function rejection. Its
native view suite passes 610 cases and position/axis exports pass 84/84
comparisons. Its Lambda/input aggregate passes 6,647 cases and required Radiant
passes 4,290 full / 349 partial; the ordinary runtime remains unchanged. Its
matching 131-input ASan build passes 1,073 native cases, the Canvas JS golden
and all 84 position/axis export comparisons. The ordered group retains its two
PDF failures without an ASan report. Edge-offset and nonlinear live UI controls
pass 44 exact pixel assertions across normal/ASan runs, including image
clear/restore and a causal 1x-to-2x device-scale change. The nonlinear fixture is
durable; its fresh required Radiant aggregate passes 4,291 full / 349 partial
(4,640 cases). All four strict DSOs pass; the raw boundary gate retains the same
226 deferred / 89 unapproved symbols against the unchanged 165 allowance.
The two
blend PDFs and four direct writer image/rectangle controls match exactly under
Poppler Cairo at 144 dpi, while their existing `pdftoppm` edge failures remain
preserved. This is evidence of a rasterizer difference, not permission to change
the runner or qualify the complete family.


The size-math follow-up shares the computed-expression owner with positions
(**D4.5.1v4 / D4.1.4v5**, **CI-18**). Size percentages use the full positioning
area, while positions use image free space. It validates the per-layer size
shape, retains linear/nonlinear math, clamps used calculations to the nonnegative
size range, suppresses explicit zero sizes and resolves a gradient's `auto` axis
against the positioning area. The 15/18 failing predecessor probe improves to
18/18 matches. Its frozen candidate passes 613 native view cases, 72 size and
84 position/axis export comparisons, and 22 live pixel assertions over 35 events.
Its required Lambda/input aggregate passes 6,647 cases; required Radiant passes
4,291 full / 349 partial (4,640 cases). Matching ASan freezes 131 source inputs and passes 1,076 native cases, the
Canvas JS golden, all 156 size/position/axis export comparisons and 22 live pixel
assertions. The full normal export gate is 225/231: alpha opacity, three SVG
rectangle-output groups, gradient markers and ordered PDF blend edges remain
failed. The five groups beyond ordered PDF fail identically on the predecessor
background candidate and pass on the qualified P0 style-provider runtime.
The shared solid-background/uniform-gradient path substitution is repaired by
the subsequent paint increment below. All strict DSOs pass with the same 226 deferred / 89 unapproved imports;
the unchanged raw boundary gate still fails.
Broader shorthand/gradient grammar and motion/serialization remain open.


The subsequent 23-input paint candidate restores canonical rectangles, preserves
standalone gradient-image sampling and lowers integral rectangular clip
intersections through the existing exact source-over compositor (**CI-18**;
**D4.5.1v4 / D4.1.4v5**). It passes 128 vector / 613 view cases, 22 live pixels and
both required aggregates (6,647 Lambda/input; 4,292 full / 349 partial Radiant).
Full exports improve to 230/231: the five earlier paint regression groups pass,
while exactly two ordered PDF edge assertions remain. Matching 131-input ASan
passes 1,077 native cases, the Canvas JS golden, 156 size/position/axis and 600
marker comparisons, the five repaired groups and 22 live pixels, with no ASan
report and the same two PDF failures. Four strict DSOs pass; the boundary still
has the identical 226 deferred / 89 unapproved imports. Frozen proof is under
`temp/css_support_p1/background_layers/asan_sources_12/`. The next reproduced
shorthand defect now uses one lower layer decoder for validation, computation
and computed-color extraction (**CI-18**). Its frozen 26-input candidate passes
615 native views, 14 literal preservation controls, 29 live assertions and 30
independent PNG/SVG/PDF comparisons at 1×/2×. Matching-source qualification
passes both required aggregates (6,647 Lambda/input; 4,293 full / 349 partial
Radiant), 231/232 full export groups and 1,079 ASan native cases. The Canvas JS
golden, both live fixtures (22 + 29 assertions), 186 background and 600 marker
comparisons pass; two PDF edge assertions remain without an ASan report. Four
strict DSOs pass and the unchanged boundary has 226 deferred / 89 unapproved
imports. Frozen proof is in `ordered_13_sources/`, `asan_sources_13/` and
`grammar13q_qualification_results.json` below the same work directory. No
whole-family promotion follows. The next reproduced defect is longhand
component grammar: 17 invalid replacements and one rejected valid attachment
list, against 18 invalid and six valid Chromium controls (**CI-18**).


The shared component-list validator now preserves all 18 invalid longhand
controls, accepts six valid lists and passes 30 live assertions, against 19
failures on the predecessor (**CI-18**). Repeat parsing/computation uses one
lower descriptor; supported-image recognition is shared with registered values
and border images. Independent inherited periods/storage and 256 recascades
retain stable pool bytes (**D4.5.1v4 / D4.1.4v5**). A mixed URL/local, gradient/
scroll and gradient/fixed page passes 26 live assertions over 1×/2× changes.
The frozen 30-input candidate passes required aggregates (6,647 Lambda/input;
4,295 full / 349 partial Radiant), 231/233 full export groups and 1,081 ASan
native cases. Canvas JS and all three live fixtures (22 + 30 + 26) pass with no
ASan report. Two bitmap/PDF edge assertions and a new clipped-vector-tile PDF
assertion remain. Four strict DSOs pass; the boundary retains 226 deferred / 89
unapproved imports. Frozen proof is in `ordered_14_sources/`, `asan_sources_14/`
and `grammar14q_qualification_results.json` below the same work directory.

The following coverage repair merges touching uniform-gradient tiles and
intersects rectangular clip geometry, retaining real space gaps; a single
`space` tile also uses its authored position (**CI-18**). Predecessor probes fail
21/36 uniform-tile and all nine single-space output comparisons; Chromium
confirms all 30 independent reference pairs at 1×/2×. Full qualification of
this repair and background computed-accessor completion remain pending.

The full coverage candidate passes 232/234 export groups, 1,081 ASan native
cases and 133 live assertions per runtime, retaining the two ordered bitmap
PDF assertions and a negative-position PDF clip-edge assertion. The follow-up
sharp-rectangle intersection repair passes all 234 affected output comparisons;
the experimental zero-radius primitive conversion passed those export controls but failed the combined Lambda/raster gates. The final repair recognizes the original sharp rectangular contours and preserves their stored path representation. Nine
computed background component accessors also share layout's winner/substitution
and shorthand decoding. Owner-font math uses the common SVG/grid length
normalizer; independent inherited periods project only at the receiving image
count, and before/after queries compute in their originating font context
(**CI-18**, **D4.5.1v4 / D4.1.4v5**). The new live fixture passes 20/20 Chromium
references, all 620 view cases pass, and computed reads reach 24 independent
export comparisons. The final 37-input combined candidate (`ordered_17d.exe`, `reads17dq_*`) passes both required aggregates (6,647 Lambda/input; 4,299 full / 349 partial Radiant), 235/236 full export groups, 1,086 matching 131-input ASan native cases, 372 background/overflow/read and 600 marker comparisons, six live fixtures (153 assertions per runtime) and the Canvas JS golden. The two original ordered bitmap PDF assertions and unchanged 226 deferred / 89 unapproved imports remain open. Original contour storage also preserves the fractional-alpha and affine controls. Image/shorthand getters, effect sampling and broader image-family acceptance remain open.


The subsequent explicit radial-radius repair shares one lower prelude decoder
with background/registered image validation and selected `image-set()` candidates.
Shape inference and radius arity follow [CSS Images 3 §3.2.1](https://www.w3.org/TR/css-images-3/#radial-gradients).
Computed descriptors own owner-normalized radius math; geometry supplies each
tile's independent width/height percentage basis. Image inheritance deep-clones
that syntax, and the common release path covers mask replacement (**CI-18**,
**D4.5.1v4 / D4.1.4v5**). Initial controls pass 624 native view cases, 84 durable
export comparisons and 26 live Chromium-matched assertions. The frozen 40-input lowercase-radius checkpoint passes both required gates (6,647 Lambda/input; 4,300 full / 349 partial Radiant), 236/237 full exports, 1,090 matching 131-input ASan native cases, 456 background/radius/overflow/read and 600 marker comparisons, 179 live assertions per runtime and the Canvas JS golden (`radial18q_*`). The two ordered bitmap PDF assertions and unchanged 226 deferred / 89 unapproved imports remain open. A follow-up uppercase prelude probe exposes a validator case regression assigned to the next candidate; full radial/conic centers also fail all 30 off-center output comparisons despite 20 exact Chromium reference pairs. Case grammar, full centers, stop hints/interpolation, computed image/shorthand reads and effect sampling remain open.


The full-center/case increment now shares the generic position decoder and
owned geometry across radial/conic producers. Edge offsets use the whole
gradient box; nonlinear expressions keep their declaration-owner lengths and
receive each tile's percentage basis at paint. Keyword matching reuses the
caseless identifier matcher, and all three gradient kinds share one lower
function/repetition metadata table. The uppercase radius regression is corrected.
Controls pass 627 native views, 96 durable 1×/2× output comparisons and 27 live
Chromium assertions. The frozen 42-input normal / 131-input ASan checkpoint passes fresh 6,647 Lambda/input and 4,301 full / 349 partial Radiant cases, 238/239 exports, 1,093 sanitizer cases, 206 live assertions per runtime, the Canvas JS golden and 552 background/geometry/read plus 600 marker comparisons (`centers19q_*`). Two ordered PDF edge assertions and unchanged 226 deferred / 89 unapproved imports remain open. Wider body/stop/hint grammar and computed image/effect consumers remain open (**CI-18**, **D4.5.1v4 / D4.1.4v5**).

The next applied stop-math increment retains per-stop owned computed syntax,
shares used-line fixup across existing image consumers and validates typed
stop bodies before choosing literal/selected images. Deep clone/retirement
controls follow **D4.5.1v4 / D4.1.4v5**. Focused checks pass 631 view/style cases,
130 vector cases, 126 real-paint all-output comparisons, four raster-only
mask/text comparisons and 34 Chromium-matched live assertions. The frozen 47-input normal /131-input ASan checkpoint passes both required gates (6,647 Lambda/input; 4,302 full /349 partial Radiant), 240/241 exports, 1,097 sanitizer cases, 240 live assertions per runtime, the Canvas JS golden and all 18 focused groups (678 background/geometry/read, four raster-only mask/text and 600 marker comparisons) in `stops20q_*`. The two ordered PDF edge assertions and unchanged 226 deferred /89 unapproved imports remain open. Existing SVG/PDF mask and clipped-text omissions are
explicit P2.2 work, excluded from all-output credit; wider prelude grammar,
angle-percentage math and transition-hint interpolation remain open (**CI-18**).

The existing radial mask now scopes flattened export subtrees through the shared
`PaintEffectGroup` descriptor and raster capture. Sampling retains whole-box
geometry and the inverse of effective density/transforms; opacity and masking
composite once before blend. Borrowed mask display items remain unretainable
while captured pixels own their storage (**D4.5.1v4 /D4.1.4v5**, **CI-18**).
Sixteen native nested-opacity/clip/blend/density controls, 30 real-paint output
comparisons and 30 Chromium-matched live assertions pass. Frozen 51/131-input
`mask21q_*` passes both required gates (6,647 Lambda/input; 4,304 full /349
partial Radiant), 241/242 full exports, 1,098 ASan cases, 270 live assertions
per runtime and all 19 focused export groups. Two prior PDF edge assertions
and unchanged 226 deferred /89 unapproved imports remain. Full mask grammar,
visibility and clipped-gradient-text export paths remain separate work.

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
| `test/test_radiant_core_gtest.cpp:309` | `StyleEpochTest`; collisions, COW, epoch coexistence, environment changes, and snapshot lifetime. |
| `test/test_css_cascade_memory_gtest.cpp` and `test/css_cascade_memory_baseline.tsv` | Standard page-load and forced-recascade memory baseline; exact recipe/binding ceiling plus byte budgets. |

The first implementation should concentrate on the measured condition and
owned-style retention paths, while preserving the demonstrated plain-recipe
reuse. Larger representation changes then have a controlled baseline and
explicit ownership gates.


**CI-18 visibility paint/hit checkpoint (2026-10-12):** A shared computed-self
visibility predicate suppresses own hidden and ordinary collapsed paint while
retaining explicit visible descendants. Raster/export text, images, markers,
outlines, column rules and scrollbars share that predicate; inline-SVG retains
its internal visibility renderer. Hidden-overflow hit coordinates use stored
programmatic offsets independently of scrollbar presence. Sixty PNG/SVG/PDF
comparisons and 72 causal live assertions pass at 1x/2x. Frozen58-input normal
`014f13f760232055dc4768963c01101fff97550b1fa415cc1fc14aa859bf11c2`
and132-input ASan
`2d540b2f9c2dafee507407cc98289cc1b43648f4964d241bb2188422116f4a8b`
pass6647 core/input,4305 full/349 partial Radiant,1098 sanitizer native and342
live assertions per runtime. All20 focused groups and the Canvas golden pass;
full exports242/243 retain only two old ordered 2x PDF edges. Boundary226/89 is
unchanged. Owners retain **D4.5.1v4 /D4.1.4v5**. Table collapse layout, focus,
link annotations and broader paged/effect consumers remain open. Evidence:
`temp/css_support_p1/background_layers/visibility22q_*`,73 protected artifacts.


**CI-18 clip/focus/provider checkpoint (2026-10-12):** Owning-box clip origins
survive scroll adjustment, affine scale/reflection and rounded/nested vector
scopes. Shared SVG path serialization preserves fractional contours. Eighteen
transformed and 36 sharp PNG/SVG/PDF comparisons plus 40 Chromium/native live
pixels pass at 1x/2x. Focus reads the live cascade and owned flat-tree display
ancestry, with native-default fallback for empty pre-render projections.
Committed hiding clears focus through the existing event/identity transition;
104 further Chromium/native focus assertions and 4,096 stable-pool reads pass.
The existing owned-root invariant test is unchanged and passes. Owners follow
**D4.5.1v4 /D4.1.4v5**; pending identity remains **D4.5.2**. Protected
`consumer30q_*` passes6,765 core/input, 5,368 full /353 partial /one skipped
Radiant, 1,106 matching sanitizer cases and 506 live assertions per runtime;
full exports 243/245 preserve19 range comparisons and two ordered PDF edges.
The 133-input sanitizer source snapshot matches the 81 primary plus one
supplemental normal inputs. Seventeen host/collection imports are removed
through lower-owned immutable providers (**D7.1.3 /D7.5.3**); boundary 213/76
remains a failed strict gate with no allowance increase. Hidden PDF links,
clipped gradient text, complete axis/clip-margin/paged/effect consumers and the
broader family limits remain open.


**Follow-up qualification (2026-10-12, CI-18 /D7.1.3 /D7.5.3):** Protected
`consumer31q_*` qualifies flat hidden/collapsed own PDF link suppression and
preserves existing visible-descendant links under **D4.5.1v4 /D4.1.4v5**.
Ten normal/sanitizer annotation controls pass at 1x/2x; Chromium's print behavior
for a visible descendant differs from its browsing activation. Two more Item
host wrappers move behind the established lower-owned provider, preserving
implementations and values. Fresh required gates, 1,106 matching sanitizer cases
and 506 live assertions per runtime pass; exports 244/246 retain nineteen range
comparisons and two ordered PDF boundaries. The first boundary link finds stale
objects containing the moved wrappers. Their provenance and raw failure remain;
rebuilding just those objects measures 211 deferred /74 unapproved with no new
imports or allowance changes. Strict boundary closure remains open under
**D7.3.3–D7.3.4**; no formal ruling changes.


**Event provider qualification (2026-10-12, D7.1.3 /D7.5.3):** Protected
`events32q_*` moves eighteen event host calls behind a lower-owned immutable
provider while preserving native implementation, public module names, traced
payloads and dispatch identity (**D4.5.1v4 /D4.5.2**). Five provider tests cover
all mandatory fields, operation/argument routing, eight creation-flag combinations
and 200,000 concurrent event calls. Fresh required gates pass 6,765 Lambda/input
and 5,368 full /353 partial /one skipped Radiant; 1,107 matching sanitizer cases,
525 live assertions per runtime and the Canvas golden pass. Full exports remain
244/246, preserving nineteen gradient-range comparisons and two ordered PDF
boundaries. Ninety normal and 136 sanitizer source inputs match their protected
binaries. The four strict DSOs build; the raw boundary removes 18 imports from
211/74 to 193 deferred /56 unapproved with no additions and an unchanged 165
allowance. Thirty-seven imports are removed since 230/93; remaining strict
closure follows **D7.3.3–D7.3.4**. The clipped-text draft stays unapplied and
unqualified at the vector edges. No formal ruling changes.


**Event workflow qualification (2026-10-12, D7.1.3 /D7.5.3):** Protected
`workflow33q_*` removes four remaining synthetic/author-template imports through
the existing event provider, preserving implementations and dispatch identity
(**D4.5.1v4 /D4.5.2**). Required gates, 1,107 matching sanitizer cases, 525 live
assertions per runtime and Canvas golden pass; exports 244/246 retain the same
nineteen range comparisons and two ordered PDF edges. Ninety-one normal and 136
sanitizer inputs match protected binaries. The raw boundary improves 193/56 to
189 deferred /52 unapproved with no additions or allowance change; 41 imports
are removed since 230/93. Strict closure remains open (**D7.3.3–D7.3.4**).


**Editing/factory/context ownership qualification (2026-10-12):** Protected
`ownership34q_*` qualifies two editing host callbacks, the lower shared fragment
factory and runtime-owned evaluator transfer (**D7.1.3 / D7.5.3 / D7.2.5 / D5.4.1**).
Required gates pass 6,766 Lambda/input and 5,368 full /353 partial /one skipped
Radiant cases; seven matching sanitizer runners pass 1,144 cases and eighteen
live fixtures pass 534 assertions per runtime. Canvas golden passes; exports
244/246 preserve nineteen range comparisons and two ordered PDF edges. Ninety-nine
normal and 140 sanitizer inputs match protected binaries. The raw boundary is
185 deferred /48 unapproved, four fewer with no additions or allowance change;
45 imports are removed since 230/93. Strict closure remains open
(**D7.3.3–D7.3.4**). No formal ruling or CSS-family promotion changes.


**CI-18 gradient precision and highlight scope increment (2026-10-12):**
Affine linear native fills evaluate the shared premultiplied-sRGB stop sampler
at output pixel centers, using existing vector coverage and clipping. Coverage
captures preserve the caller's page-space tile origin and pixel lattice and are
bounded to the visible path. Ordered stop lookup preserves the following color
at a coincident stop; repeated scanline colors may be shared without sharing
coverage. Stroke/projective/degenerate backend paths retain separate support.

Highlight declarations remain per element under **D4.5.1v4 / D4.1.4v5**.
Replacing a subtree's cascade clears highlight entries for that subtree, leaving
unrelated siblings live. Controls with authored or inherited highlight colors
acquire inline storage through the existing ownership helper before computation.
The corrected selection fixture places pixel bounds at event scope, activating
the original checks. The native/scoped sanitizer and twenty live assertions pass;
`gradient_selection38q_*` records the pending complete merged-build qualification.


**CI-18 native decoration continuity increment (2026-10-13):**
Horizontal native underlines/overlines use the same gap intervals for solid,
dashed, dotted, double and wavy patterns. The complete pattern is clipped to
those intervals so ink skipping does not restart dash/wave phase; line-through
remains continuous. Source bytes bound one scoped gap per decoded codepoint,
removing the former 64-gap loss without retaining font/cache storage
(**D4.5.1v4 / D4.1.4v5**). CSS/font thickness converts once to destination pixels
before snapping; gap clip bounds align vertically to the destination pixel grid
without trimming the pattern's antialias coverage (**RSC7**).

This follows [CSS Text Decoration 4 §2.10.4–2.10.5](https://drafts.csswg.org/css-text-decor-4/#text-decoration-skip-ink-property)
for the bounded horizontal native consumer. Five native and five text-owner
sanitizer groups and twenty-four causal live style/density assertions pass;
complete `decoration39q_*` qualification is running. SVG/PDF probes establish
that this native increment does not yet supply their decoration geometry. Shared
vector producers, decorating-box uniformity, complex/vertical/shaped runs and
broader color/shadow/fragment contexts remain open; serialized SVG CSS alone is
not accepted as portable decoration paint.


### 3.19 Shared decoration paint geometry (bounded implementation)

The vector probes in `temp/css_support_p1/decoration39/vector_probe/` show no
red decoration stroke for authored dashed/dotted/double/wavy cases. SVG delegates
an incomplete decoration style to the receiving text viewer; PDF has no line
producer. This is P2.2 consumer work, not missing CSS registration. The shape
and continuity contract follows [CSS Text Decoration 4 §2](https://drafts.csswg.org/css-text-decor-4/#line-decoration)
and its [painting order](https://drafts.csswg.org/css-text-decor-4/#painting-order).
No formal ruling defines decoration geometry; payload lifetime remains governed
by **D4.5.1v4 / D4.1.4v5**, and coordinate ownership by **RSC1 / RSC7**.

- Extract the existing native metric, pattern and ink-interval calculations into
  one recorder with explicit font, fragment origin/extent, source slice and
  output-density inputs. Native and vector sinks share that calculation; do not
  copy the private native functions into exporters or fabricate a raster surface.
- Font metrics and layout extents stay logical. Bitmap ink measurements convert
  through the selected handle's physical/requested-size ratio; output-grid
  snapping is explicit for native raster recording. Vector geometry is recorded
  in logical user units and inherits the existing transform/clip/effect scope.
- Record solid fills and complete styled paths, with interval clips that preserve
  their phase. Immediate native lowering keeps the existing display-list gateway;
  deferred vector commands own paths/clips before local scratch or fonts retire.
  Use the existing payload-transfer helpers and retained path lowering.
- SVG decorated text consumes selected glyph geometry and preserves semantic
  labels. Its exported appearance must not depend on a viewer implementing CSS
  decoration color, thickness, skip behavior or matching the selected font.
  PDF consumes the same line paths. Avoid duplicate CSS-plus-path decoration.
- Underlines/overlines precede glyph ink; line-through follows it. Keep shadows,
  foreground and the line's actual owner/fragment relationships explicit. A
  bounded horizontal/unshaped delivery cannot claim complete decorating-box
  uniformity, complex-script shaping or vertical/fragmented support.

Acceptance requires actual colored strokes, ink interruptions, inter-glyph phase,
continuous strikes, density/transform/effect consumers and replay after source
font/arena retirement in native, independently rasterized SVG and PDF. Keep
existing representation assertions only where serialization is itself the
contract; replace appearance-only string checks with real pixel controls.


The applied recorder follows the shared geometry/ownership contract for the
bounded horizontal profile. Selected-font decoration pixels, offsets, placement,
paint order, long-run gaps and retirement replay pass focused normal/ASan checks.
SVG uses glyph outlines and semantic labels; PDF records the same lines. Empty
interval sets emit no geometry, and failed compound recording rewinds owned
payloads (**D4.5.1v4 / D4.1.4v5**). Twenty-three exact SVG/PDF pattern-edge
comparisons remain failed: identical path attributes and a clip-removal control
identify independent-reader clip antialiasing. Those raw failures remain visible;
transformed/effect, transparent-color, complete owner/fragment and shaping/vertical
consumers remain unqualified. The fresh repository qualification is protected
under `decoration40q_*`: core 6,797, Radiant 5,410 full /353 partial /one skipped,
1,111 scoped ASan cases, 678 live assertions per runtime and 126 SVG matrix formats
/2,466 assertions. The complete exporter passes 252/257 groups; seventeen prior
PDF comparisons and twenty-three new vector-edge comparisons remain failed.
No full decoration/export claim follows from this slice.


### 3.20 Decoration color state (bounded implementation)

`decoration41/baseline_summary.json` reproduces transparent longhand, shorthand
and zero-alpha RGB values painting an unintended text-colored line in PNG,
independently rasterized SVG and PDF. Resolution drops zero-alpha longhands,
and rendering/copying treats alpha zero as the default `currentColor` marker.
[CSS Text Decoration §2.3](https://drafts.csswg.org/css-text-decor-4/#text-decoration-color-property)
accepts a color and defaults to `currentColor`; [CSS Color §6.3](https://drafts.csswg.org/css-color-4/#transparent-color)
defines transparent as a color with zero alpha. Alpha must remain paint data.
This CSS ruling has no Lambda formal counterpart (**CI-18**); style ownership
continues under **D4.5.1v4 / D4.1.4v5**.

Represent current-color selection separately from RGBA and copy it through the
existing font-value helpers. Reuse the emphasis current-color predicate rather
than introducing a second parser. Shorthand omission/reset, explicit inheritance
and live current-color changes must preserve their state. The shared decoration
recorder consumes that state for all outputs. Qualify transparent/zero-alpha
against an undecorated reference, and current-color/reset/inheritance against
explicit-color references at both densities, including repeated live mutation.
Complete ancestor decorating-box uniformity stays assigned to §3.19; this change
must not claim that propagation model from single-owner color controls.


The applied color-state implementation passes thirty exact reference pairs in
three outputs at both densities, sixteen live assertions and four ownership
groups in normal/scoped ASan. Partial UI/legacy font initialization now shares one
initializer with explicit current-color state. Source lifetime remains governed
by **D4.5.1v4 / D4.1.4v5**. Broad repository qualification is protected under
`decoration_color41q_*`: core 6,797, Radiant 5,411 full /353 partial /one skipped,
1,111 scoped ASan cases, 694 live assertions per runtime, exporter 255/260 and
SVG matrix 126 formats /2,466 assertions. Forty comparisons in the five known
export failure groups remain open. The separate `decoration42` reproducer keeps a later
shorthand/earlier-longhand winner defect open under P0.2; it is not credited as
fixed by the alpha-state delivery.


### 3.21 Decoration shorthand component winners (bounded implementation)

`decoration42/baseline_summary.json` reproduces a later shorthand failing to
cancel an earlier line longhand. Property-ID traversal applies the shorthand
first and then resurrects the weaker longhand. [CSS Text Decoration §2.6](https://drafts.csswg.org/css-text-decor-4/#text-decoration-property)
sets all four components and resets omitted components to their initial values.
Use the existing cascade comparison helper for line/style/color/thickness
application, preserving stronger important/specific/layered longhands. This is
CSS winner computation under **CI-18**; retained declaration ownership remains
under **D4.5.1v4 / D4.1.4v5**.

The isolated gate skips a component declaration when the selected shorthand
outranks it. Fourteen controls at both densities pass eighty-four exact PNG/SVG/PDF
image comparisons, covering source order, all four components, importance,
specificity, normal/reversed-important layer order, invalid declarations and
variable substitution. Before application, add live
restyle and scoped-owner validation; keep complete CSSOM shorthand reflection,
layer/scope breadth and decorating-box propagation explicitly unqualified.


The applied component gate passes both exact reference groups and twelve live
class-restyle assertions in normal/scoped ASan. The protected native-41 runtime
fails forty-eight reference comparisons and six live assertions. Earlier text
controls retain only the known vector-fringe group. Broad qualification is protected
under `decoration_winner42q_*`: core 6,797, Radiant 5,412 full /353 partial /one
skipped, 1,111 scoped ASan cases, 706 live assertions per runtime, complete exporter
257/262, SVG matrix 126 formats /2,466 assertions, and unchanged boundary185/48.
Forty comparisons in five known export groups remain failed. No complete
CSSOM/owner/scope claim follows.


### 3.22 Decoration shadows and complete text paint order (isolated delivery contract)

`decoration43/baseline_summary.json` identifies two producers missing from the
horizontal profile: native underlines are recorded before the shadow prepass,
and native/SVG/PDF shadow layers include glyphs without line geometry. The
native overlap control loses eighty opaque red-band pixels; an offset-shadow
control has no green line pixels between glyphs in any output. [CSS Text
Decoration §4–§5.1](https://drafts.csswg.org/css-text-decor-4/#painting-order)
places outer shadows below lines/text and includes decorations in the shadowed
text. This is **CI-18**; synchronous source/font borrows and retained geometry/
images follow **D4.5.1v4 / D4.1.4v5**, and density follows **RSC1 / RSC7**.

Reuse the shared decoration recorder for the native display-list capture and
vector shadow layers. Translate complete geometry, including ink gaps, and
compute capture extents from glyphs plus decorations before blur padding.
Shadow color overrides foreground decoration color. Merge opaque glyph/line
coverage before applying each shadow layer's alpha once; do not multiply alpha
again at glyph/line overlap. Preserve authored shadow order and place all shadow
layers below foreground underlines, overlines, glyphs, emphasis and strikes.

Acceptance uses translated decorated-text references for styles, sharp/blurred
layers, both densities and independent vector readers; overlap/alpha controls
must reach actual pixels. Live style/color/blur/DPR changes and replay after
font/source/arena retirement qualify lifetime. Preserve existing raw export
failures. Owner/fragment uniformity, complete shaping/color-font and vertical
contexts remain separate requirements; no full shadow/decoration claim follows
from one horizontal fixture.

### 3.23 PDF group opacity (bounded delivery contract)

The isolated §3.22 alpha control exposes a separate PDF producer defect:
opacity-only effects multiply the alpha of each primitive, so opaque glyph/line
overlaps receive alpha repeatedly. Use an isolated transparency Form XObject
to composite each complete group once, preserving vector geometry and nested
groups. PDF group semantics follow ISO 32000 §11.6.6 and §11.4
([PDF Association](https://pdf-issues.pdfa.org/32000-2-2020/clause11.html));
this CSS consumer repair is **CI-18**, with document-owned streams and teardown
under **D4.5.1v4 / D4.1.4v5**.

Qualify exact overlapping interiors against a single union, nested opacity,
primitive alpha, coloured backdrops, transforms, clipping and subsequent
opaque paint at both densities in raster/SVG/PDF. Union perimeter AA is a
separate reader control: blending coincident primitive fringes is not an
interior-alpha oracle. Preserve those raw comparisons. Native writer coverage
must balance groups, reject unbalanced serialization, retain nested resources,
and destroy documents with open groups under ASan. This bounded delivery does
not qualify the broader §3.22 shadow candidate.

**Qualified (2026-10-13):**
`temp/css_support_p1/pdf_opacity43/final_isolated_provenance.json` records
thirty reference pairs in three outputs at both densities, 51 writer cases
and 38 live assertions per normal/ASan mode. Protected runtime 42 fails the
new interior/resource control with 380 assertions. Native PDF groups now own
their streams, reference only direct child forms, restore parent font selection,
and enclose fractional ink boundaries before the Form's clip. Primitive alpha
remains independent of group alpha. The earlier full union control retains two
2× PDF coincident-fringe differences under `decoration43/`; its raw comparisons
remain separate from the interior contract. Protected `pdf_opacity43q_*` records 6,797 core passes, 5,416 full /353 partial
/one skipped Radiant case, 1,162 scoped ASan cases, 744 live assertions per
runtime, Canvas golden and 126 SVG formats /2,466 assertions. Export passes
258/263 groups with the same forty comparisons. Boundary debt remains 185
/48 unapproved with no delta (**D7.1.3 / D7.3.3–D7.3.4**). Normal/ASan snapshots
contain 145/143 inputs with no drift/reports; 147 protected records and 46,600
copied exports seal the delivery. Broader opacity/shadow contexts remain open.

### 3.24 Recorded source ink and filter captures (isolated delivery contract)

The §3.22 wavy blur reference clips a nonzero tail outside its border-derived
capture, while the shadow capture retains the recorded stroke's complete bounds.
Filters consume rendered source ink, including glyph bearings and decoration
overhang, before applying their spatial support (**CI-18 / RSC1 / RSC7**).
Bounds may expand during recording; the final display-list publication remains
immutable under **D4.5.1v4 / D4.1.4v5**.

Derive source bounds from the recorded command range. Reuse that range union
for native and semantic effect groups; update saved-backdrop regions before
replay and include the result in vector raster captures. Preserve explicit
clips, backdrop isolation, effect ordering and density. Qualify overhanging ink,
nested groups, coloured backdrops and both densities with independent references;
retain reader resampling failures and verify replay after source retirement.

The overflow reference also isolates native PDF opacity Forms: their initial
border-derived BBox clips overflowing descendants. Enable writer ink tracking
for these groups, accumulate painted path/stroke/image extents through nested
groups, and round the final BBox outward at output density. Explicit clips remain
separate operations; font outlines keep vector geometry. Writer coverage must
include reset after clipping paths, nested propagation and pen-state restoration.

**Qualified §3.22/§3.24 (2026-10-13):**
Protected `decoration_shadow44t2q_*` qualifies the applied checkpoint 44t under
**CI-18 / RSC1 / RSC7 / D4.5.1v4 / D4.1.4v5**. Complete horizontal glyph/line
silhouettes preserve shadow layer alpha and background/shadow/foreground order.
Translated solid/wavy decorations and sharp/blurred/translucent shadows pass
independent native/SVG/PDF references at both densities. Completed display-list
ink and actual blur support expand captures and saved backdrops; PDF opacity
Forms track painted path/stroke/image bounds and restore font/pen state.
Private zero-alpha margins are trimmed before publication, preserving alpha 1.
Atomic recording preserves queued backgrounds and releases failed resources.

The source/runtime guards cover 153 normal and 145 ASan
inputs without drift. Core passes 6,797; Radiant passes
5,424 full /353 partial /1 skipped. All
1,169 scoped ASan cases, Canvas golden, and
762 live assertions per runtime pass without ASan
reports. SVG passes 126 format checks and
2,466 assertions. The full exporter passes
266/270 groups, preserving 38 raw
assertions: ordered-background PDF edges (2), outside-stop PDF colors (11),
styled SVG/PDF clip fringes (23), and coincident opacity-union PDF fringes (2).
No tolerance or expected output changed. Earlier untrimmed failures remain
preserved under `decoration_shadow44/raw44s/`.

Boundary debt stays 185 deferred /48 unapproved
with no additions or removals and allowance 165; all four strict
DSOs build (**D7.1.3 / D7.5.3 / D7.3.3–D7.3.4**).
`decoration_shadow44t2q_protected_artifacts.json` seals 151 records
and 49,112 copied exports. Support inventory remains 410 names and
twenty bounded partial families. Broader decorating-box/fragment, emphasis,
shaping/vertical, effect-ordering and independent/paged contexts remain open.
No P0–P2 tier is closed; the native state/layout provider is the next slice.

### 3.25 Overflow component computation and open axes (bounded implementation)

Overflow and overscroll resolve each physical axis through one component
cascade over shorthand, physical/logical declarations and `all`; rollback
filters all overlapping sources together. Shared keyword computation performs
substitution, defaulting and physical-axis inheritance. Overflow alone applies
opposite-axis `visible`/`clip` coupling. Computed shorthand/longhand/logical reads
use the same decoder and current ancestor declarations during dirty callbacks,
with iterative owner traversal and scratch-owned formatting (**CI-18 /
D4.5.1v4 / D4.1.4v5**).

An absent authored axis keeps the UA/control fallback supplied by HTML style
setup. Store that fallback separately from author-computed scroll values and
reset both during retained recascade; dirty declaration removal must not expose
the previous author value. Explicit initial/unset use the CSS property's initial
value. Existing dialog/iframe/textarea defaults share the same producer. Broader
control UA CSSOM and browser-specific textarea visible normalization remain open.

A `visible` axis supplies no box edge to descendant paint or pointer rejection.
The shared clipping geometry bounds the open strip using the destination extent
projected into owner coordinates, then retains the authored edge on the clipped
axis. Native, SVG/PDF and export-extent consumers use this contract. A
clip/visible pair has straight edges even with `border-radius`, per
[CSS Overflow 3 §3.1.2](https://www.w3.org/TR/css-overflow-3/#corner-clipping).
Paint containment still constrains both axes. Clip state precedes the owner's
scroll offset; accumulated transforms preserve owner-space edges (**RSC1 /
RSC7 / D4.5.1v4**).

Axis-aligned exact pixels and transformed open-end/interior/clipped-edge points
are separate acceptance controls. Bit-identical transformed antialias fringes,
open strips crossing a projective horizon, broader clip-margin boxes and
independent/paged/effect contexts remain separate qualification work.

### 3.26 DOM native host ownership and borrowed SVG contexts

Runtime DOM calls use the lower-owned immutable host provider for native SVG
timing, property/shape queries, geometry/hit testing, touch input, animation
creation and range/selection lifecycle operations. Radiant publishes native
adapters from its module; pure child-boundary traversal belongs to the DOM node
owner. Shared POD geometry and a typed path-containment callback cross the
boundary without importing native constructor or method symbols (**D7.1.3 /
D7.5.3 / D7.3.3–D7.3.4**).

SVG source scopes borrow document/instance/registry state synchronously. A
scope captures the provider once and restores through that same provider;
absence before registration is harmless. Native and lower scopes call one TLS
entry/restoration producer, retaining nested same-document source inheritance
and cross-document isolation. Scalar geometry outputs copy native values;
private text-hit records remain native. Provider publication has process
lifetime, while borrowed objects retain existing document pins and precise
Item roots (**D4.5.1v4 / D4.1.4v5 / D5.3.3 / D5.4.1**).

Native hosts call `radiant_register_dom_engine_providers()` before using DOM
engine services. `UiContext::init` and Jube static registration share that
idempotent entry point; standalone embedders can install services without
initializing the Jube registry. Initializing only the vector backend does not
install DOM host services. Lower-provider absence remains observable, including
unchanged output records (**D7.1.3 / D7.5.3**).

### 3.27 Pseudo grid computed-value font owners

Grid track computation carries the pseudo selector through declaration
formatting into the shared font/math resolver. A pseudo's relative lengths use
its own computed font, including pending font/custom-property edits; walking an
explicit inherited value changes to the originating declaration owner before
length computation. Formatting clones and normalizes values in temporary
storage without retaining source-pool allocations (**CI-18 / D4.5.1v4 /
D4.1.4v5**).

This qualifies computed track functions and lengths on generated pseudos. Used
track snapshots and pseudo shorthand serialization retain separate limits;
computed lengths reaching a companion's geometry/paint do not qualify every
generated grid layout or independent paged context.

Grid definition and placement shorthands reconstruct their computed members on
pseudos through the same serializer as ordinary elements. Grid component lookup
must select retained shorthand/longhand winners in the pseudo style tree before
projecting a member; a direct property lookup loses declarations supplied by
`grid-template` or `grid`. That component competition is axis-independent, while
the existing font resolver retains the pseudo or explicit inherited declaration
owner. Used pseudo track snapshots and broader shorthand forms remain separate
qualification limits (**CI-18 / D4.5.1v4 / D4.1.4v5**).

### 3.28 Receiving currentColor and pseudo dispatch

Color-typed properties preserve `currentColor` through declaration inheritance
and resolve it against the receiving element or pseudo's color, including the
property's initial value. A literal inherited color still uses the declaration's
value. Retaining the receiver does not change the declaration owner used for
relative-length or variable computation (**D4.5.1v4 / D4.1.4v5; CI-18**).

Computed dispatch selects an accessor's declared pseudo capability before
choosing dirty versus committed execution. Element-only accessors use the
common pseudo declaration path consistently. Accessors that already compute
pseudos, including line-height's font/leading resolver, explicitly declare that
capability; a missing flag must not expose specified tokens in dirty reads.

### 3.29 Failed-image alternative baselines

An image with no admitted image payload and a nonempty alternative uses its
fallback text line's content baseline, including its actual leading. Explicit
width or height changes the replaced box without changing that classification.
The baseline producer and the initial/completed inline-alignment paths share
this state; loaded images, empty alternatives and other replaced types retain
their existing baseline rules (**FT-17 / CI-18**).

### 3.30 Retained fragment copy allocation

An empty display list receiving a known copied range reserves that range's
checked item count before appending. It must not inherit the recording list's
full-frame initial reserve for each tiny retained element. Subsequent growth
remains amortized, and cloning retains its existing independent payload owners
and rollback behavior (**D4.5.1v4 / D4.1.4v5**). Encoding still respects the shared
critical memory budget; reducing retained allocation does not bypass it.

### 3.31 Overflow clip margin computation

The optional visual-box and length components share one validated decomposition
across the parser, layout and computed-value serializer. Computed reads omit the
default padding box and zero offset when a nondefault box alone suffices; lengths
use the declaring element or pseudo's font context, including explicit
inheritance and reads before layout has caught up with a mutation. Temporary
numeric evaluation does not retain source-pool allocations
(**CI-18 / D4.5.1v4 / D4.1.4v5**).

The existing negative-offset geometry follows the working
[CSS Overflow 4 §3.2](https://drafts.csswg.org/css-overflow-4/#overflow-clip-margin)
model, which allows shrinking the clip edge. It is preserved independently of
the positive computed-value/browser controls. New per-edge longhands and broader
replaced/rounded/independent-view applicability are not qualified by this slice.

### 3.32 Overflow and outer-shadow corner outsets

Overflow clip margins and both outer-shadow producers use one outset-adjusted
corner calculation. Positive outsets preserve square corners and reduce the
expansion of small radii according to the original two-dimensional edge
coverage; negative outsets keep the existing inset-and-floor behavior. The
small-radius polynomial is evaluated without subtracting nearly equal floats.
Clipping constrains the resulting used radii and shares that geometry with hit
testing (**CI-18 / RSC1 / RSC7 / D4.5.1v4**).

The rule follows the working
[CSS Backgrounds 3 §4.2](https://drafts.csswg.org/css-backgrounds-3/#corner-shaping)
outset adjustment, including its edge-coverage term; the older spread-only
formula is insufficient for nearly elliptical boxes. Reusing this geometry
does not qualify complete elliptical shadow payloads, projective/fragmented
contexts or arbitrary filter/mask ordering.

### 3.33 Pseudo computed integer counts

Calculated clamp and hyphenation counts retain the pseudo selector through
owner-font numeric evaluation. Dimensional cancellation can turn relative
lengths into integers, so these domains still depend on the correct font.
Explicit inheritance changes to the declaring element before evaluation;
integer rounding and canonical component compression retain their existing
shared helpers (**CI-18 / D4.5.1v4 / D4.1.4v5**). Computed counts reaching a
companion's geometry/paint do not qualify complete pseudo clamping, shaping or
language-dependent hyphenation.

### 3.34 Opacity numeric declaration contexts

Computed opacity reads use the shared owner-font evaluator, retaining the
pseudo selector and fresh declaration state during dirty callbacks. Relative
lengths inside dimensionally cancelling math therefore use the same declaring
font as other numeric domains. Percentages remain coefficients until the
opacity range is applied. Layout, CSSOM and SVG scalar serialization share the
same number/percentage, nonfinite and range policy; this does not require a
layout trial or retain temporary font recipes (**D4.5.1v4 / D4.1.4v5; CI-18**).

Explicit inheritance changes the declaration owner before evaluation, while
invalid substituted dimensions default through the existing cascade path.
The bounded live fixture includes direct generated-box paint and companion
geometry/paint after font, variable and class changes. Percentage quotient or
nonlinear product cancellation still needs a representation beyond the
current affine percentage terms; it is not qualified by this increment.

### 3.35 Integer stack-level computation

`z-index` validates through the existing typed-integer grammar and computes
through the common integer result policy. Clamp counts, hyphenation limits and
stack levels share the CSSOM owner-font adapter, with each consumer's range
kept explicit. Layout evaluates the same numeric recipe, applies integer
rounding/bounds before storage, and copies a parent's computed level for
explicit inheritance. It never reinterprets an inherited recipe against the
child's font (**D4.5.1v4 / D4.1.4v5; CI-18**).

Computed reads retain `auto` separately from integer zero even when the element
is static, and retain pseudo declaration fonts. Dirty font, variable and class
changes reach positive positioned paint order and pointer targets. This
bounded consumer repair does not provide complete negative/zero/auto stacking
context identity, nested depth ordering, flex/grid applicability, pseudo used
stacking or independent/paged fragmentation.

### 3.36 Integer flex/grid item order

`order` uses the same typed-integer grammar and CSSOM owner-font adapter as
stack levels, with its own initial value and no `auto` keyword. Layout computes
integer math before assigning either flex or grid item storage. Explicit
inheritance copies the parent's computed order. The former flex-only accessor
now lives in the layout header and reads the tagged flex/grid item kind;
sorting and inheritance share it instead of duplicating field selection
(**D4.5.1v4 / D4.1.4v5; CI-18**).

Font, variable and class changes update computed values, exact item offsets
and painted positions in both layout systems. Invalid literal fractions or
dimensions preserve an earlier declaration, while invalid substituted values
default to zero. This does not qualify complete ordering/applicability,
reverse/vertical/fragmented axes, animation, focus/accessibility source order,
pseudo used item order or independent/paged contexts.

### 3.37 Numeric flex factors and scalar ranges

Numeric `flex-grow` and `flex-shrink` longhands use typed math and their
nonnegative number range before conversion to finite float item storage.
Their initial values remain distinct during invalid computed-value defaulting;
a valid negative calculated number computes to zero. The existing color clamp
is promoted to the CSS module header with explicit minimum/maximum arguments,
so color, integer, opacity and flex consumers share NaN/range/zero handling.
Owner-font scalar evaluation is shared by formatting and native policy helpers;
committed field access keeps its published numeric snapshot (**D4.5.1v4 /
D4.1.4v5; CI-18**).

The numeric grammar follows [CSS Flexbox §7.2](https://drafts.csswg.org/css-flexbox/#flex-components)
and [CSS Values range checking](https://drafts.csswg.org/css-values-4/#calc-range).
Chromium controls cover fractional, negative, NaN, infinite and overflowed
numbers. Shared clamping canonicalizes opacity's computed signed zero as well.
Shorthand/member competition, absent flex-item storage, alternate view contexts
and complete axis/animation behavior remain separate qualification limits.

### 3.38 Flex factors across item-storage kinds

Computed flex factors use CSS declaration computation across formatting-context
boundaries. A populated flex-item record supplies its published value; a grid
item keeps its grid record while computed reads and explicit inheritance use
the shared owner-aware declaration query. The query returns a copied scalar
within its scratch lifetime. The existing accessor derivation slot supplies a
value when its direct property group is absent; a populated group retains its
published sample (**D4.5.1v4 / D4.1.4v5; CI-18**).

The implementation extends the existing inherited-declaration traversal with a
typed scalar consumer. CSSOM formatting and native inheritance use the same
cascade, substitution, initial-value parsing and owner-font evaluation. Numeric
results are consumed directly, and the grid parent retains its item properties.
This bounded longhand contract still leaves flex shorthand projection and
selected/paged or animated contexts beyond the tested snapshots open.

### 3.39 Flex shorthand component computation (P0.2 / P1.2)

The separate ten-row probe passes 20/20 in Chromium and 5/20 on the
storage-qualified runtime. The legacy `flex` handler consumes literal numbers,
sets all three fields together and has no registered members. CSSOM therefore
cannot project its components, and a later shorthand can overwrite an important
longhand. Numeric math, basis-first forms and invalid substituted components
must follow the same component cascade as longhands (**CI-18**).

Register grow/shrink/basis as the shorthand's members. Parse one shared borrowed
component descriptor in the lower CSS owner; validate the complete grammar
before accepting the declaration. Project only the winning member after
substitution, preserving invalid-at-computed-value defaulting. Share dispatch
for grid/container/flex projections instead of adding a third independent
consumer branch. Layout applies each winning member after the owner's font is
computed; CSSOM/native inheritance use the same projection and owner traversal.
Borrowed immutable grammar defaults avoid per-view declaration allocations.
Complex projected values remain owned by the caller's pool (**D4.5.1v4 / D4.1.4v5**).

This increment targets explicit zero/length/percentage bases, numeric math,
member priority/order, substitution, inheritance and live edits. Omitted basis
is zero length under [Flexbox §7.1](https://www.w3.org/TR/css-flexbox-1/#flex-property);
Chromium's observed `0%` default remains a separately recorded compatibility
difference. Mixed length/percentage used bases, intrinsic basis keywords,
indefinite/reverse/vertical/fragmented contexts, motion and alternate views
remain bounded follow-ups, without a full-flex support claim.

The shorthand fixture now has 55 browser assertions against 19/55 before this
increment. A separate inherited-basis probe from a grid-item parent passes 4/4
in Chromium and 2/4 under the shorthand ASan runtime: CSSOM correctly computes
`60px`, while the used basis falls back to auto without parent flex-item
storage. Its `flex_grid_basis_*` records define the next bounded owner-context
follow-up; the factor-only grid-parent repair does not qualify this basis path.

### 3.40 Inherited flex basis without parent flex storage (P0.2 / P1.2)

The separate `flex_grid_basis_*` probe passes 4/4 in Chromium and 2/4 under the
shorthand runtime. A grid item's `flex-basis:2em` computes to `60px` in CSSOM,
but its child's native `inherit` branch has no parent flex record to copy and
uses auto. This is a used-value storage defect, distinct from the shorthand
winner repair (**CI-18**).

Promote the existing small basis snapshot to the lower CSS header and give
numeric/keyword basis results one typed policy. Layout and declaration-owner
computation supply their existing math contexts to that policy. Extend the
shared inherited-declaration walker with a transient native consumer callback
instead of adding a second ancestry/substitution traversal or formatting then
reparsing computed text. Native inheritance keeps published parent snapshots
when present and requests the computed declaration when that optional storage
is absent. No view tag changes or borrowed value pointers enter the result
(**D4.5.1v4 / D4.1.4v5**).

Qualify relative lengths and preserved percentages, shorthand and longhand
sources, explicit inheritance, causal font/variable changes, initial/invalid
fallbacks and repeated reads. Mixed length/percentage used bases, full intrinsic
basis algorithms, animations, alternate/paged contexts and broader axes remain
separate limits. The earlier zero-length versus Chromium `0%` compatibility
record is unchanged by this storage repair.

### 3.41 Image-rendering reads and raster marker sampling (P0.2 / P1.4)

The `marker_sampling_values_*` probe passes 3/3 in Chromium and 0/3 on the
basis checkpoint: the inherited keyword has a native consumer but no computed
accessor. The same frozen raster shows 32 mixed-color interior pixels in an
integer-enlarged crisp marker, against zero in its ordinary-image control and
Chromium marker. The marker calls the default linear image recorder directly,
bypassing the shared image sampling policy (**CI-18**).

[CSS Images 3 §5.2](https://www.w3.org/TR/css-images-3/#the-image-rendering)
applies the inherited specified keyword to images in all properties.
[CSS Lists 3 §3.2](https://www.w3.org/TR/css-lists-3/#image-markers) takes
`list-style-image` from the originating list item. Add a cascade/pseudo computed
accessor using the existing declaration consumer; retain pending substitution
until the shared validator can check the resulting keyword. Raster URL markers
use their originating item's applied image-rendering value and the existing
resource-owning transformed/scaled painter. No separate sampling algorithm or
borrowed declaration storage is needed (**D4.5.1v4 / D4.1.4v5; RSC1 / RSC7**).

Qualify keyword/default/inheritance/invalid substitution reads, causal edits,
source-pixel preservation and retained transformed markers at both densities.
The exploratory empty-marker baseline offset, transformed pixelated footprint,
and vector image sampling without an explicit PaintIR sampling field remain
separate limits. Chromium 152 does not accept `smooth`; native support for that
existing keyword is not browser-qualified by this increment.

### 3.42 Pixelated sampling in an affine device footprint (P1.4)

The protected marker checkpoint's `pixelated_affine_probe/` reproduces blended
interior colors for integer scaling, anisotropic scaling and quarter turns.
Chromium matches each independent device-sized literal reference exactly;
Radiant differs in 112, 72 and 112 channels respectively. Projection chooses
its virtual nearest-neighbor multiple from the untransformed box, so a 2px
image transformed to 8px is first treated as a 1x image. Projection also uses
ceiling while the ordinary raster blitter already uses the closest positive
integer multiple required by [CSS Images 3 §5.2](https://www.w3.org/TR/css-images-3/#the-image-rendering).

Share the existing closest-multiple policy between both raster paths. For an
affine matrix, derive each axis's device magnification from its matrix column
length, including homogeneous normalization; keep inverse coordinate mapping,
clipping, alpha conversion and resource ownership unchanged. Virtual sample
coordinates use double intermediates so a large composed magnification does
not require an allocated intermediate image or overflow a float pixel index
(**CI-18; RSC1 / RSC7; D4.5.1v4**).

Qualify transformed replaced images, backgrounds and markers at both densities,
including flips, quarter turns, anisotropic scaling and retained effects.
Keep projective footprint selection, skew/noninteger browser filtering and
vector/paged sampling open. The noninteger exploratory browser pairs differ
from literal bilinear references and remain recorded; they are not acceptance
controls for the integer affine repair.

### 3.43 Shared pseudo declaration lookup beyond generated content (P0.2)

The marker-baseline live fixture renders 32px and 48px marker fonts but reads
24px from `getComputedStyle(item, '::marker').fontSize`. The shared declaration
entry point selects pseudo declarations only for before/after; other supported
pseudos silently consult the originating element. The DOM already owns the
canonical pseudo-style mapping, including marker and control parts (**CI-18**).

Route every nonzero pseudo identity through that existing DOM lookup, including
the grid-component winner path. Reuse the existing substitution, font-state,
inheritance/default and serialization consumers. No additional pseudo mapping
or borrowed-style lifetime is introduced (**D4.5.1v4 / D4.1.4v5**). Qualify marker
font variables, inherited/default values, repeated reads and the live used-font
consumer; broader allowed-property and paint support remains separately bounded.

The direct native getter passes after that change, but the live wrapper still
reads the host font: `dom_get_computed_style` independently recognizes only
before/after names. Extract a single immutable pseudo-name/identity registry
from the parser's mapping and the matcher's type switch before adding a third
mapping. Parser, matcher and DOM wrapper reuse its name, selector-type and
legacy-colon policy. Keep every existing numeric identity and alias behavior;
header-owned immutable metadata avoids a new core-to-input module import
(**D4.6.1v4; D7.3.3 / D7.3.4; CI-18**).

### 3.44 Adjoining block-start margins across empty children (P1.2)

The nested-font qualification exposes `input-width` crossing its stored layout
floor. Its body keeps the first empty paragraph's 16px collapsed top margin,
while the following heading's 18.72px margin is consumed only as a sibling
offset. The heading reaches the expected position; the body's border edge and
content height retain the wrong chain origin. A separate three-case probe varies
one/two positive empty-child margins and a negative margin. Current Chromium
matches all three independent references; native matches none. Original pixels
and geometry remain in `temp/css_support_p0_p1_resume/empty_margin_chain_probe/`.

Under **CI-18** and [CSS 2.2 §8.3.1](https://www.w3.org/TR/CSS22/box.html#collapsing-margins), self-collapsing empty children do not end the
parent/first-content margin adjacency. Carry the chain's greatest positive and
most negative components when seeking the first substantive child. Merge the
following child's authored margin with that chain; a sibling-consumed used
margin cannot replace the authored contribution. Reuse the existing chain,
self-collapse, clearance and float-shift helpers. Preserve BFC, border/padding,
clearance, quirky-margin, writing-mode and stretch exclusions. Retained reflow
must reconstruct the chain from resolved/owned state, without retaining new
declaration or view borrows (**D4.5.1v4 / D4.1.4v5**).

Qualify positive/negative/transitive chains, blockers, adjacent floats and causal
margin/font/content edits against browser geometry and native pixels. Keep the
existing failing floors, references and raw results unchanged during diagnosis.
This is a separate used-geometry repair, not a normal-leading adjustment.

The four-row extension includes a first-content descendant whose own child
propagates a larger margin. Preserve the exact amount consumed by the early
sibling-collapse step and restore it to the current margin; restoring only the
initial authored margin loses that descendant contribution. The first draft
passes the original three rows but fails this extension; the revised draft
matches all four browser pairs.

The corrected 54-assertion live fixture passes in Chromium. Native geometry and
pixels pass 48/48, while six computed reads expose consumed margin values.
Vertical physical margin reflection must use the existing owned `flow_margin`
snapshot of resolved values before normal-flow collapse, with the applied
boundary as fallback before a snapshot exists. Keep layout-dependent percentage
resolution and the existing horizontal/auto policy bounded separately; do not
serialize unresolved declaration percentages as used pixels. This follows the
[CSSOM resolved-value contract](https://www.w3.org/TR/cssom-1/#resolved-values)
and adds no new retained owner (**CI-18; D4.5.1v4 / D4.1.4v5**).

The 81-check extension adds owner-relative lengths, percentages and explicit
inheritance. Chromium passes all 81; the snapshot getter passes 80 natively.
Physical/logical spacing inheritance still copies the parent's consumed boundary
and inherits 10px instead of its resolved 8px. The longhand, shorthand, getter
and existing multicol consumers share one immutable snapshot accessor. Preserve
the existing resolved-spacing inheritance profile; percentage inheritance across
different bases, horizontal auto serialization and broader contexts remain
separate acceptance work.

The first broad gate catches three boundary regressions: a root/shadow case,
anonymous table cells and an overflow BFC with empty list children. Skipping
nonzero empty margins before confirming that the parent can adjoin also skips
the ordinary sibling-collapse branch when that parent is blocked. Share the
existing parent boundary test with the first-content search and retain the
sibling path at root/BFC/border/padding/clearance boundaries. The failed gate and
its individual reports remain evidence; no floors or references are changed.

### 3.45 Incremental computed style and geometry resolution (P0)

**Status: native sizing milestone implemented; broader reuse remains partial.**
LLVM LangRef's baseline release profile performs four layouts. The initial
intrinsic counters undercount nested work; see the correction in §3.46. Measurement repeats
`dom_node_resolve_style → resolve_css_styles` even when only the containing size
changes; inheritance, substitution and property dispatch repeat with it.

Keep computed-style validity separate from completed layout. The per-element
`CssResolvedStyle` cache records substituted geometry declarations, folds
font-relative lengths once, and retains percentage dependencies on width,
height or containing inline size. A context mismatch evaluates only affected
bindings through the shared `css_apply_computed_property` dispatcher. Computed
fonts, colors and fixed lengths remain reusable. Full resolution refreshes
inherited fonts before resolving the element's relative lengths.

A typed, clean geometry baseline restores CSS sizes, spacing, insets and flex
fields after speculative sizing or margin collapse. It preserves caller-assigned
auto sizes and excludes resource pointers and flow links. Keys cover specified
style/version, document and parent revisions, view generation/kind/item role,
font resources, raster scale, and root-font/viewport inputs when read. Geometry
bindings track containing dimensions, definiteness and intrinsic policy.
Snapshots and cloned expressions belong to the selected default-view pool:
**D4.5.1v4 / D4.1.4v5**, with no retained GC pointers (**D4.5.2**).

Probe/secondary views, table/replaced/form/SVG roles, explicit geometry
inheritance, unsupported percentage reads, container effects and elements with
motion use full resolution. View resets retire the cache; preserving it across those
resets and reducing repeated intrinsic/document layouts remain separate work.

Acceptance covers A→B→A sizing without full CSS work, definite zero versus
indefinite bases, logical/physical winners, inherited fonts/variables, consumed
margins, provisional view kinds, flex initialization and causal environment edits.
The focused suites pass: view reuse 137, text/layout 166, animation 151. The
Radiant baseline has zero failures with existing partial/skipped cases retained;
all 999 selected CSS/input checks pass. The full input gate has 4502/4526 passes:
24 Wiki/prose/PDF failures reproduce under untouched HEAD and the new host,
including identical PDF pixel differences. No references or exclusions changed.

Three interleaved release trials per build, identical local LLVM assets, against
HEAD `55239a758`: median load **89.869 → 72.102 s** (**19.8% less time**, 1.246×).
Median peak RSS is **705.5 → 709.0 MiB**. Raw trials, binary/source hashes and
validation results: `temp/css-incremental-benchmark/report.json`. Full/measurement
resolutions, incremental reuses, geometry evaluations and fallback reasons are
available with `LAYOUT_PROFILE=1`.

The final diagnostic run records 2,994,711 incremental reuses and 37,538 geometry
evaluations. It still performs 1,179,944 full resolutions, including 828,708
parent-key misses. Distinguishing unchanged inherited computed values from a
parent's conservative restyle revision is deferred. CSS reuse alone does not
remove repeated sizing work. The original report of 247,169 intrinsic width
requests with zero hits was incomplete: nested context restores erased profiler
counters, corrected in §3.46.

### 3.46 Intrinsic sizing profiling and reuse (P0)

**Status: sizing repairs implemented; loader handoff remains deferred.**
**Milestone:** eliminate redundant intrinsic width/height queries. Acceptance
covers containing-size-only changes, percentage descendants, inherited fonts,
root-font/viewport changes, deferred flex heights and nested profiler accounting.
Qualify with focused native tests, the Radiant baseline and three interleaved
release trials per variant against identical local LLVM assets.

The corrected release profile of `aa8ecdad6` records **1,576,692 width requests
(67 hits)** and **2,913,577 recursive height calls** across four document layouts.
Width self time is **36.85 s**, height self time **19.61 s**, together **81% of
69.67 s layout time**; rendering takes **0.23 s**. Native stack samples confirm
the intrinsic sizing paths under nested flex layout. Restoring a saved
`LayoutContext` previously discarded nested profiler work; the shared restore
helper now preserves counters, and nested scopes report inclusive and self
time separately. Width/height self time includes their unscoped CSS/font/text
work; inclusive ancestor timings and CSS/text buckets must not be added to it.

Implemented repairs:

- Width-cache keys treated every containing-size change as significant. Plain
  horizontal-flow subtrees now propagate whether containing dimensions affect
  their intrinsic width. Independent results reuse across those changes; fonts,
  style epochs, generation, root-font size, raster scale and viewport remain
  keyed. Dependent or unsupported descendants retain conservative context keys.
- Flex width requests also recursively estimated heights before their wrapping
  width was assigned. Width-only queries now leave height invalid until needed;
  vertical writing retains the existing paired measurement.

Caches remain owned by the selected view pool (**D4.5.1v4 / D4.1.4v5**), with no
retained GC pointers (**D4.5.2**).

**Remaining structural work:** the loader discards geometry produced by
load-time CSSOM reads and initial presentation lays out the document again.
An experimental handoff removed one pass and reached 33.2 s in a diagnostic
run, but exposed incorrect retained geometry in dynamic anonymous-table/list
fixtures. The extra rebuild currently masks those defects: removing a separator
leaves separate anonymous table runs, and a changed table role retains a stale
list contribution. The handoff change was withdrawn; fix retained mutation
geometry before removing that pass. Evidence: `temp/layout-deep-failure-details.log`
and `temp/layout-deep-layout-handoff-fixed.log`. Complex intrinsic contexts and
recursive height estimation also remain; browser-level load time is not achieved.

Qualification: view-reuse **141/141**, text/layout **166/166**, and the layout
baseline has **zero regressions across 3,294 cases** (existing partial cases
retained). The full Radiant gate reports 5,998 passed, 143 partial, two skipped
and one failure: `PaintOwnership.SvgPhasesKeepForeignHtmlInsideItsSubscene`.
That SVG paint-phase assertion also fails with the sizing optimizations and
added kerning comparison reverted to HEAD behavior; no test expectations were
changed. Lint and `git diff --check` pass. Evidence:
`temp/layout-deep-radiant-final-baseline.log`,
`temp/layout-deep-vector-control.log`, `temp/layout-deep-provenance.json`.

Three interleaved release trials per variant against `aa8ecdad6`, with profiling
disabled and identical local LLVM assets: baseline **73.297 / 72.758 / 73.163 s**;
optimized **47.342 / 47.133 / 47.319 s**. Median **73.163 → 47.319 s**, **35.3%
less time (1.546×)**. Median peak RSS **709.2 → 732.4 MiB**. This result retains
all four document layouts. Trials and binary/input hashes:
`temp/layout-deep-benchmark/report.json`.

Final diagnostic: width requests **1,576,692 → 632,831**, misses
**1,576,625 → 393,451**, and width self time **36.85 → 10.70 s**. Height calls
remain **2,573,534** with **20.11 s** self time; flex content self time is
**12.61 s**, total layout **44.07 s**, paint **0.23 s**. Full CSS resolutions
remain **1,178,513**, including **828,676 parent-key misses**: avoiding width
walks has not repaired conservative inherited-style invalidation. The next
targets are constraint-keyed height reuse and stable inherited computed-style
validity, followed by the retained-geometry defects above. Raw counters:
`temp/layout-deep-qualified-profile-summary.json`.

### 3.47 Resolve and lay out once for unchanged inputs

**Status: adopted 2026-10-20; integrated continuous-document implementation qualified 2026-10-21.**
Continuous-document updates share publication, computed inheritance identity,
demand-driven sizing and independently owned flow results. Current acceptance,
qualification and release measurements are recorded in the
[implementation record](../impl/Radiant_Impl_Retained_Layout.md).

This extends **VR1–VR6 / VR10 / VR13**; ownership remains governed by
**D4.5.1v4 / D4.1.4v5 / D4.5.2**. It does not change a formal ruling.
The execution contract is **one computation per unchanged set of inputs**:
computed CSS per element/style input version, intrinsic sizes per distinct
sizing query, and flow per distinct box constraint. A fixed viewport alone
does not freeze DOM, styles, font/image resources, or container constraints.
Synchronous script reads after geometry-affecting writes may require separate
updates; identical reads and final presentation must reuse current results.

The logical pipeline is:

`style inputs → immutable computed styles → stable boxes → intrinsic queries → retained layout results → paint/geometry reads`

Intrinsic queries are demand-driven. Ordinary block flow establishes widths
downward and returns heights upward during one recursive execution; it does
not first estimate the height of the same subtree.

- **Computed style has semantic identity.** Cascade, substitution and
  inheritance publish a clean snapshot. Deferred percentages retain their
  expressions and dependency masks. Measurement, property allocation,
  provisional box roles and flow-margin consumption cannot change that
  snapshot's identity. Parent dependencies track inherited values, custom
  properties and explicit inheritance, rather than a count of full-resolution
  executions. Equal inputs may preserve identity without resolving again;
  equal outputs use field-aware equality (**VR6**), never hash equality alone.
- **Sizing is a query over stable inputs.** Min/max inline contributions share
  a traversal. Width-dependent block size includes wrapping width, percentage
  bases/definiteness, writing mode, relevant content/style/resource versions,
  and any external formatting inputs it reads. Omit a key component only when
  dependency analysis proves independence. Retain active results through the
  update, including A→B→A requests; cache-slot collisions must not cause
  repeated evaluation. Unsupported contexts keep an explicit conservative path.
- **Measurement can retain completed layout.** A height request at final
  constraints returns a `LayoutResult` with line fragments, child placements,
  baselines, overflow and boundary state. Final flow consumes that result.
  Position-only placement reuses internal layout when its inputs are unchanged.
  Intrinsic and final percentage policies remain distinct queries where CSS
  requires them; flex stretch, cyclic percentages and fragmentation cannot be
  hidden by returning a result computed under different constraints.
- **One scheduler owns updates.** Loader checkpoints, CSSOM reads and
  presentation share the same dirty-dependency processing and retained view.
  Mutations invalidate actual consumers; a document-wide query counter is not
  proof that every computed value changed. Geometry reconstruction retires
  geometry, with style lifetime independent of scratch. Retained anonymous
  table/list normalization must be correct before removing the loader rebuild.

**Integrated retained-result contract (VR15):**

- Row and column preparation request inline contributions first; block contributions
  wait for the assigned wrapping width. Refined auto-line stretch propagates to
  content that reads block size, preserving definite/cyclic percentage policy.
- An ordered source-tree stage resolves counters, quotes, list markers and generated
  before/after content once. Sizing and flow consume that output. Closed-details and
  scripting-enabled noscript contents are omitted. Paged, shadow-projection and
  explicit custom-layout documents retain their existing effect adapter. Content-visibility
  and contain-intrinsic-size producers also keep that adapter until their on-demand effects
  and remembered sizes have an explicit preparation contract. Preparation does not
  start animations or transitions; actual flow binds provisional used geometry.
- Selected-view query entries own text rectangles, fragment unions, child placements,
  baselines/trim state, used boundaries, positioned links, table borders and grid
  track/placement output. Several entries survive A→B→A requests and destructive
  measurement probes; only the live association expires when geometry is rewritten.
  Generation, content/style/environment and constraint keys guard replay and teardown.
- Assigned inline size admits horizontal percentage lengths, row percentage bases,
  HTML image hints, ordinary lists/pseudos, contained positioning, tables and grids.
  Root percentage bases remain keyed. Block-dependent percentages, percentage gaps,
  first-line/letter transforms, fragmentation, SVG/form and other unmigrated consumers
  keep conservative adapters; no result from a different sizing policy is substituted.
- Inherited computed output has its own revision, separate from own input revision.
  Deferred line-height kind, text/list state and transitive custom properties remain
  part of identity. Immutable font selection/spacing inputs are interned by exact
  field equality; mutable metrics, handles and shadow ownership remain element-owned.
  Declaration-driven SVG values and unnormalized expressions retain conservative identity.


The shared continuous-document checkpoint validates a selected-tree publication
against root identity, content/style/subtree revisions, cascade,
viewport/input capabilities, font resources/defaults, raster scale and
motion/media policy (**VR15**). CSSOM and task checkpoints reconcile through the
existing mutation path; presentation consumes the same current result. Loader
root publication retains that tree. Actual full resets restore authored table
runs and first-letter text before resetting props, then detach materialized
pseudos so their retained owners can refresh them in source order. A completed
pass records its inputs before observer sampling; subsequent writes stay dirty.
Paint-only/detached commits can advance their consumed versions only while the
published environment still matches. Paged editions and explicit custom-layout
lifecycle passes retain their own flow entry points.

The host frame walk commits pending iframe mutations through this same checkpoint
before sampling the child. A parent callback can write into a child document without
dirtying the parent's layout. Paint-only commits publish their resolved-style flags
together with presentation values. Computed transforms consequently serialize as
matrices after both initial load and resize, as required by
[CSS Transforms 1 §4.2](https://www.w3.org/TR/css-transforms-1/#serialization-of-transforms).

Ordinary invalidation retains the existing view-pool epoch, while incremental
updates preserve clean sibling results. Flex intrinsic
work flags belong to an item-initialization pass, and unkeyed scalar caches to
a measurement generation; neither can borrow the prop pool's longer lifetime.
Contribution-only queries must restore live boxes as well as leave their
fragments intact (**VR15**). General flex estimator/ancestor-height fidelity
remains a qualification limit recorded with the full LLVM comparison.

The legacy mutable view-prop adapters remain during migration. First remove
execution-count invalidation with stable style input identities and establish
constraint-keyed height reuse; then migrate consumers to immutable computed
records and retained flow results. Input identity is a conservative bridge:
a changed declaration that computes to the same value may still invalidate
until output comparison is complete. Do not claim that bridge implements
semantic equality or the complete once-only pipeline.

**Computed-box migration (milestone 3a):** eligible horizontal selected-view
elements compute box/text properties before provisional view typing. A frozen
typed baseline and normalized percentage expressions survive box allocation;
context-dependent applications live in a separate, lazily allocated used-box
record. Committed reuse validates computed inputs and containing constraints
before keeping algorithm-assigned dimensions. Height queries read clean
preferred sizes, with percentage definiteness tied to the owning flow/flex/page
constraint. Ratio transfer removes border-box padding once; cyclic intrinsic
widths never accept a provisional parent sample as a definite contribution.

Vertical flow, secondary views, table/replaced/form/SVG roles and unsupported
effects retain their existing adapters. In particular, vertical flow still
stores assigned inline constraints in mutable props. This boundary does not
implement immutable font sharing or retain line fragments; stable query inputs
and `LayoutResult` consumption belong to milestone 3b, followed by shared update
publication in milestone 4 (**VR15**, ownership **D4.5.1v4 / D4.1.4v5 / D4.5.2**).

Milestone 3a is qualified: 168 core cases and the full Radiant baseline pass
(6,036 passed, 143 partial, 2 skipped, zero unexpected failures). LLVM full CSS
resolutions fall from 595,226 to 325,474. Three paired release trials give a
40.043→39.663 s median, an effectively flat load time; 1,847,772 height
computations and four document layouts remain. Retained completed flow results
and shared publication were the next milestones at that checkpoint. The later
implementation and evidence are recorded below and in the implementation record.

**Earlier implementation checkpoints (historical):**

**First flow migration (milestone 3b):** row-flex preparation requests width
without estimating height at an unassigned wrapping width. Measurement entries
record height validity explicitly; a later column/height request completes the
same entry. Vertical items and controls keep their paired measurement policy.
At the assigned row width, an ordinary horizontal, auto-height flex item with
independent block/inline descendants can perform real flow and retain its
view-owned fragments, content extent and baselines. Final flow consumes those fragments, preserving
the final stretched box. Line sizing uses the natural content height even when
the container has a definite cross size. Trial layouts invalidate the memo
before overwriting geometry; changed constraints/style/text/font/resource
inputs miss. Each descendant must have current computed inputs and pass the
shared context-independence check. Percentages, ordered pseudo/counter effects,
resources, positioned boxes and other formatting contexts keep the existing
path. This retains one live subtree result; independent fragment storage for
multiple constraints remains pending. This is a bounded
consumer of retained flow, not the general `LayoutResult` migration (**VR15**).

For the first text-only consumer, against the qualified milestone-3a release,
LLVM's complete serialized tree is
identical, with height computations reduced 1,847,772→1,208,921 (**34.6%**).
Three isolated paired release trials give a 39.663→35.744 s median (**9.9%**),
and median peak RSS falls 789.1→767.0 MiB. The four document layouts and general
mixed/block-flow estimation remain; see the implementation record for provenance
and qualification. The final Radiant baseline has zero unexpected failures
(6,037 passed, 143 existing partial outcomes, 2 skipped); 169 focused core
tests, 169 native text/layout tests and the integer-cast lint also pass.

The ordinary block/inline extension is also qualified: 170 core, 170 native
text/layout, 917 CSS cases and the full Radiant baseline pass. Dependency-proven
height queries share an explicit wrapping width across caller space labels;
dependent descendants retain the full policy key. LLVM geometry stays identical
and height computations fall another **10.2%**, but paired release median load
time is effectively flat (**34.575→34.389 s**, overlapping ranges). Four document
layouts remain. Retained anonymous-table/list normalization and shared
publication follow in milestone 4; general query-owned fragments
and generated effects remain pending (**VR15**). Evidence and existing browser
fidelity limitations are in the implementation record.

**Shared update/publication (milestone 4):** continuous CSSOM, task/load and
presentation paths now use the checkpoint above (**VR15**). Native tests verify
unchanged reads preserve box/fragment identity and add no flow; changed inputs
and targeted text mutations invalidate. The release Radiant baseline passes
with 6,044 passed, 143 existing partial outcomes, 2 skipped and zero unexpected
failures; 176 native text/layout cases, 170 core cases and the layout lint pass.
LLVM document layouts fall 4→3 and height computations 1,085,928→817,930
(24.7% fewer). Three isolated paired release trials give a **34.263→25.881 s
median (24.5% faster)**. Its full-tree comparison remains nonidentical, including
five ancestor-height changes; general flex estimator/flow fidelity and query-owned
fragment results remain open. Full classification and provenance are in the
implementation record.

The nested-flex continuation measures eligible auto-height flex subtrees with
real flow at the assigned inline width. An explicit natural-size root constraint
precedes final cross stretch; a provisional height or horizontal flex growth
cannot fix that query's block size. Nested flex results also key the completed
block constraint, since a later stretched height can redistribute descendants.
Ordinary block/inline results retain their proven height independence. Ordered
generated effects, resources and dependent lengths remain on existing paths;
this does not yet provide independent fragments for multiple constraints
(**VR15**, selected-view ownership **D4.5.1v4 / D4.5.2**).

The conservative flex path also distinguishes provisional auto-line heights from
authored constraints. Final stretch keeps its percentage-sizing policy, while
completed child boxes can enlarge an underestimated automatic line and propagate
that extent through nested row containers. A taller sibling's allocated stretch
size remains a floor; definite height and min/max constraints still apply. Both
retained content paths key this provisional policy. Block layout leaves cyclic
resource contribution queries to flex when flex owns the formatting context,
avoiding a subtree estimate before flex has assigned wrapping widths (**VR15**).

Acceptance: nested document/flex fixtures compute unchanged style inputs and
each sizing query once; repeated geometry reads do no additional work; A→B→A
constraints reuse A; inherited font/color/variable edits and changed
percentage bases still update geometry. The local LLVM fixture must retain
correct geometry and script behavior, with duplicate computation counters
separate from query counts. Performance evidence uses comparable release
trials. Milestones and qualification are tracked in
[the implementation record](../impl/Radiant_Impl_Retained_Layout.md).

### Container-query convergence follow-up (2026-10-21)

**VR15**, selected-view ownership **D4.5.1v4 / D4.5.2**: convergence and
condition-result caches now use one `css_container_geometry_key`. It projects
logical query axes through writing mode and includes only eligible physical
dimensions. An inline-size container's changing uncontained block extent cannot
request another whole-document cascade. The provider and convergence collector
also share zoom-normalized content-box metrics. Size containers still observe
both axes; container identity, font and name dependencies remain relevant.

Constant query thresholds first use the existing CSS math evaluator with
absolute units. Only unresolved expressions ask the container's CSSOM numeric
resolver for its font/custom-property context. Relative units retain that
owner's semantics. Native regressions cover axis projection, height-only changes,
aspect ratio, owner-generation changes and resolver call counts.
