# Removing DomNodeRecord from production

**Status** — Implemented 2026-11-21 for VR7–VR9. Production uses structural tree ownership and direct external holds. Node registration allocates zero per-node side records. Precise GC slots exist only for managed backing values, as required by D5.5.2 and D6.3.3. Allocation diagnostics are optional under D5.2.4. [Formal design](../../doc/Lambda_Formal_Design.md) · [View reuse design](Radiant_Design_View_Reuse.md)

## Historical baseline before this migration

`DomDocument::init` initializes the registry with 256 buckets. Factories for elements, text, comments, and attributes register nodes. MarkEditor also registers embedded text when it joins a document. Registration looks up the address, grows the table at 75% occupancy, and normally allocates and links a record in the document pool. Some synthetic layout boxes bypass registration and belong to the view. Release builds link this code unconditionally. The debug assertion on a lifecycle invariant becomes an abort in release. [Build manifest](../../lambda-input/build_targets.json) · [Document initialization and factories](../../lambda-input/lambda/input/css/dom_element.cpp) · [Registry implementation](../../lambda-input/lambda/input/css/dom_lifecycle.cpp)

The original arm64 release census covered 53 page fixtures and 171 web templates with scripting disabled. Each column sums independent page processes. Sizes are logical record and bucket payloads. Pool overhead and process RSS are separate measurements.

| Census | Constructed DOM | After first layout |
|---|---:|---:|
| Registered nodes | 160,452 | 179,027 |
| Record payload | 33.05 MiB | 36.88 MiB |
| Hash buckets | 2.32 MiB | 2.65 MiB |
| **Total** | **35.38 MiB** | **39.53 MiB** |
| Records carrying at least one pin | 0 | 10 |
| Records with backing-source or related metadata | 66,457 | 66,457 |
| Records with an explicit string pool | 0 | 4 |
| GC roots / manual-slot links / detached candidates at snapshot | 0 / 0 / 0 | 0 / 0 / 0 |

Ten pinned nodes tells us little about weak reference use. Layout anchor resolution, paged layout, and retained view state can resolve identities without pinning. The two snapshots also miss transient pins and the costs of an editing or scripted workload. [Retained view state](../../radiant/radiant/view_tree_model.cpp) · [Anchor layout](../../radiant/radiant/layout_anchor.cpp) · [Paged layout](../../radiant/radiant/layout_paged.cpp)

## Implemented ownership changes

| Resource | Production owner and release point |
|---|---|
| Durable node references | A checked 32-bit external hold count lives in the common node header. The last release schedules work in the node’s current document, including for adopted character data. Live ranges acquire replacement endpoints before releasing old ones, including mutation rebases, rollback, and teardown. JS node wrappers hold their target directly. A wrapped attribute also holds its owner. On adoption, each realm’s existing cache subscribes to destination teardown. |
| Mark source and GC backing | An embedded occurrence identifies its own source. An element with a distinct source owns a 24-byte binding for its source and borrowed type/data. A managed backing gets a stable root slot on its original heap. Adoption transfers the slot between documents without moving its address. Input-owned values have no root entry. D5.5.2, D5.5.3 and D6.3.3 apply. |
| Text allocation | The original inline string gives the exact arena retirement size after text replacement. A separately owned replacement string carries its physical pool with that allocation. |
| Detached roots and tree edges | Work and wrapper-stranded bytes belong to the queued root. Attributes and shadow subtrees participate in the ownership closure. DOM moves unlink the old sibling and parent edges before publication. MarkEditor prepares the child sequence while the original links are still intact. |
| Forms, manual slots and anchors | A form prop links into its document at creation and unlinks at release or adoption. This reaches never-attached controls before first layout. Manual assignments index only their endpoints. Implicit CSS anchors use the active layout’s membership map before reading a candidate. Scroll snap matches a saved address and generation against live areas during its existing tree walk. |
| Shared view properties | Canonical inline properties remain borrowed from their view arena with copy-on-write. Retained page generations keep their existing counted leases. These changes require no per-element property retain or release. |

### First migration stage, before record removal

Arm64 release builds ran the same 53 page fixtures and 171 web templates, one document per process, without scripting. Both builds used the same CSS compatibility changes from local Input revision `f320e2d`. The pinned Input revision `291f962` lacks two CSS helpers required by Radiant `3fb782a`. The comparison used isolated build trees and preserved both repository pins.

| Summed live bytes across 224 documents | Before migration | After first stage | Reduction |
|---|---:|---:|---:|
| Document pool + node arena, after construction | 67.69 MiB | 50.55 MiB | 25.3% |
| Document pool + node arena, after first layout | 83.19 MiB | 64.08 MiB | 23.0% |
| All document-tagged allocators, after construction | 183.77 MiB | 166.63 MiB | 9.3% |
| All document-tagged allocators, after first layout | 361.18 MiB | 342.07 MiB | 5.3% |
| Record payload + buckets, after first layout | 39.53 MiB | 20.41 MiB | 48.4% |

The all-allocator rows sum direct bytes attributed to the document, including Input, canonical styles and view storage. They include the 10,088-byte aggregate growth in the view property pool. They exclude untagged runtime allocations and process RSS. The record shrank from 216 B to 104 B. The node header, element, and text node measured 80 B, 328 B, and 120 B in both builds. The static documents had zero distinct source bindings and zero managed root slots at either snapshot. Scripted pages and editing workloads can allocate both.

Five paired first-layout CPU samples per page, after a warm-up, gave a corpus median of 23.921 ms before and 24.038 ms after (+0.49%). The p95 went from 43.414 ms to 42.542 ms (−2.01%). All 224 normalized layout trees matched. These samples cover first layout only.

Release verification passed 57 focused native lifecycle, storage-owner and view tests, 85 live-range tests, 57 MarkEditor tests, 34 hosted document-ownership tests, and 140 dynamic layout tests. The static fixture loader also passed. Coverage includes the 10,000-removal arena plateau, address reuse, repeated adoption, a failed subtree transfer followed by retry, original-heap GC compaction, never-attached form cleanup, and collection pacing for an adopted JS wrapper. The host state-store test executable still has a duplicate-symbol link failure, and the broader core suite has an intrinsic-sizing assertion that also fails on the control build. Scripted mutation and teardown timings remain unmeasured.

## Production ownership after record removal

- The document owns its published tree, plus roots that are still unpublished or detached. Each root has a one-based slot for constant-time removal. Eight slots fit in the document service. A larger array grows only for simultaneous roots and returns to inline storage once they fit. Publishing an authored child, shadow root, attribute, or generated layout child transfers ownership to its parent. [Node factories and shadow ownership](../../lambda-input/lambda/input/css/dom_element.cpp) · [Generated children](../../radiant/radiant/layout_block.cpp)
- A factory records the physical recycling arena on the node. Adoption changes the logical document and issues a new document-local ID. Every external pin owns the physical node until its holder releases it. A queued operation checks the current document if its semantics require the original document. A source-document weak reference expires at adoption. No source-document alias allocation is needed. Each document retains its existing precise-root slots on the original GC heap across transfer. [Lifecycle and adoption](../../lambda-input/lambda/input/css/dom_lifecycle.cpp)
- A weak reference minted from a live node occupies 16 bytes on arm64. Its lifetime stamp uses the four bytes after the ID. The document invalidates the stamp before recycling, transfer, and destruction. Validation of an intact current stamp can read the ID directly. For an expired stamp or a manually constructed address/ID pair, validation compares the address with known live nodes before reading any target memory. A fixed 32-slot cache accelerates repeated address checks. Cache eviction happens before storage reuse. Stamp exhaustion permanently selects structural validation. A cold lookup can take linear time in the owning forest. [Weak references](../../lambda-input/lambda/input/css/dom_lifecycle.hpp) · [Validation](../../lambda-input/lambda/input/css/dom_lifecycle.cpp)
- At a safe point, retirement walks the detached subtree, including its attributes and shadow root. MarkEditor commits reciprocal links while both sides are live. Repeated Mark values receive independent occurrences. After releasing resources, each retired allocation holds its own deferred-recycle link, arena and exact allocation size. The recycler visits zero unrelated nodes. Development fixtures can explicitly enable per-reason pin counts and check child-link invariants. [MarkEditor](../../lambda-input/lambda/io/mark_editor.cpp) · [Retirement tests](../../test/test_radiant_core_gtest.cpp)

The native transfer preflights the entire detached subtree. The host still detaches the root before native preflight. If reservation fails at that point, ownership stays with the source document and the host can retry with the detached subtree. Restoring its pre-removal position and effects on host failure remains open.

### Release memory after record removal

Two native arm64 release builds used the same repository pins, Clang optimization flags, 1200 × 800 viewport and local resources. The control retained the 104-byte record. The candidate used the ownership model above. Each of the 53 page fixtures and 171 web templates ran in its own process with scripting disabled. All 224 normalized first-layout trees matched.

| Sum across 224 documents | With 104-byte records | After removal | Saved |
|---|---:|---:|---:|
| Document pool + node arena after construction | 50.55 MiB | 32.46 MiB | 35.79% |
| Document pool + node arena after first layout | 64.08 MiB | 44.10 MiB | 31.18% |
| All document-tagged allocator bytes after construction | 166.63 MiB | 152.43 MiB | 8.52% |
| All document-tagged allocator bytes after first layout | 342.07 MiB | 326.00 MiB | 4.70% |
| Per-node records + address-table buckets after first layout | 20.41 MiB | 0 | 100% |

The broader allocator total includes the Input arena, which often owns the parser-backed node itself. The measurements already include the 16-byte increase in each common node header. On arm64, `DomNode` measures 96 B, `DomElement` 344 B, and `DomText` 136 B. At both snapshots every page used only the inline root array and had zero managed GC slots. Across both lifecycle stages, all document-tagged bytes after first layout fell from 361.18 MiB to 326.00 MiB, a 9.74% reduction. These totals sum live allocations across separate processes. Process RSS and peak memory during mutation are separate measures. The paired per-page measurements and normalized tree digests are in `temp/dom-lifecycle-removal/{old,new}.json`.

### Release CPU and mutation checks

Each page had one warm-up and five timed executions per build in each of two interleaved runs. Combining all ten per-page samples before ranking pages gave the following process CPU times.

| Phase | Median before | Median after | p95 before | p95 after |
|---|---:|---:|---:|---:|
| Document load | 7.378 ms | 7.441 ms | 15.071 ms | 15.109 ms |
| First layout | 23.961 ms | 23.959 ms | 43.609 ms | 44.147 ms |

First-layout median changed by less than 0.01% overall, and p95 increased 1.23%. The first run alone showed a 1.26% median increase. The repeat showed a 0.01% increase. The paired measurements cover static pages with scripting disabled. In a separate native mutation benchmark, a document with 4,096 surviving text siblings performed 5,000 insert, pin, detach, blocked sweep, unpin and successful sweep cycles. Median CPU over nine timed runs went from 84.824 ms to 0.286 ms. The old recycler inspected the 4,096 survivors on every successful sweep. This benchmark isolates retirement cost. It does not measure end-to-end scripted editing.

Release verification passed 53 focused lifecycle, GC and view tests, 85 live-range tests, 57 MarkEditor tests, 34 hosted document-ownership tests, three hosted mutation and resource tests, 140 dynamic layout tests, the static fixture loader and the scene listener retirement regression. One regression adopts a pinned text node, destroys the issuing document and releases the original pin while the physical arena is still live. A native edit that aborts and then commits also exercises string ownership. Removing a text node invalidates its borrowed Mark identity when required, while the node keeps any owned replacement string until reinsertion or retirement. The standalone state-store test runner remains blocked by duplicate symbols from production code and its test stubs, also present on the control.

The full Radiant baseline passed after the text ownership fix. The runner reported 8,176 passes, 296 partial passes and 20 skips, including 201 of 201 hosted document tests, 657 of 657 scene tests and 329 of 329 UI automation tests.

## Design rationale and original acceptance criteria

### Three ways to refer to a node

| What the caller needs | Proposed lifetime | Examples |
|---|---|---|
| Read a live node during a synchronous operation | Borrow from the owner for the operation. End the borrow before a callback that can mutate the tree, or acquire a strong hold before calling out. | Layout walking children, paint reading the current view, MarkEditor building a replacement chain |
| Use a result while its owning view or document state is current | The owner clears the reference or invalidates its entire cache **before** the target can retire. Check the still-live owner first. Recompute on invalidation. A published page or display list can instead own immutable geometry and source IDs as values. | CSS anchors, scroll targets, hit testing, paged fragments, scene bindings |
| Use the same node even after removal, reparenting, or adoption | The wrapper, range endpoint, subscription, queued item, undo entry, or external API handle takes a strong hold while the node is known live. Release on replacement, delivery, cancellation, disconnect, finalization, or teardown. | JS wrappers, live ranges and collections, observer targets and pending records, event and reconciliation queues, editing history |

A saved address and ID alone carry no lifetime guarantee once storage can be recycled. Migrate every persistent `DomNodeRef` consumer, including manually constructed pairs. A cache that can re-resolve after an unrelated mutation may still use its saved identity as a search key in a live owning tree. A caller that promises continuity through detachment needs the third contract. For generation-guarded caches, invalidate before any destructive mutation, adoption, rollback, or owner destruction that can invalidate their pointers. Keep the owner alive for each read. Clear outstanding entries before a guard value can repeat, and end active borrows before reuse. A `ViewPageGeneration` lease protects the view data it owns. Any path from that snapshot back to mutable DOM needs its own contract. [Retained view sources](../../radiant/radiant/view_tree_model.cpp) · [Paged sources](../../radiant/radiant/page_document.hpp) · [Geometry membership](../../radiant/radiant/view_geometry.cpp)

### One inline count for external DOM holders

Use one checked `uint32_t` in `DomNode` for durable external holds. Tree ownership remains structural. Walking a child, accessing a property, painting, or attaching a sibling performs zero retain/release operations. A detached subtree becomes eligible for recycling when every node in its ownership closure has zero external holds. A zero count on an attached node is normal. The last release requests a sweep at a safe point. This avoids destruction inside a wrapper finalizer, event dispatch, or partially completed mutation. For example, a JS wrapper holding a text node keeps its entire detached paragraph usable. Dropping that wrapper makes the paragraph eligible for the next sweep if the other holds are gone.

The implemented arm64 layout keeps the count after `layout_height_contribution` and fits the current-document pointer in the common header. Acquire and release run on the owning DOM thread and check overflow and underflow. Cross-thread consumers must post to the owner or use an independently retained immutable snapshot. Each holder acquires its replacement before releasing its old target. Every failure or rollback path releases what it acquired. Range endpoints should transfer their holds in the endpoint setters and mutation-rebase operations. [Node layout](../../lambda-input/lambda/input/css/dom_node.hpp) · [Current range refresh](../../radiant/radiant/dom_range.cpp)

A wrapper's existing GC weak-slot callback releases its native hold. Preserve the idle-GC behavior for detached trees held only by wrappers. Once total blocked bytes could reach the collection threshold, the host can enumerate its existing weak wrapper caches across the relevant realms and compare wrapper-held references with the total holds in each blocked subtree. Count multiplicity, including an Attr wrapper's hold on its owner. Keep the byte charge on the pending retirement work and end it on reinsertion or retirement. This keeps the nine per-reason counts in the development verifier. Profile this host callback on wrapper-heavy churn. [Wrapper cache](../../lambda/dom/dom_cssom.cpp) · [Wrapper-only retirement regression](../../test/test_radiant_core_gtest.cpp)

### Ownership at detach, adoption, and teardown

- **Tree edges.** Route MarkEditor relinks and DOM insert, remove, replace, and move operations through one structural commit. Prepare allocations first. While old and new nodes are live, sever obsolete edges in both directions and publish the complete new parent, first/last child, and sibling links. Each presentation occurrence owns its links under D5.5.3. Attr ownership, shadow roots, manual-slot backlinks, and synthetic layout boxes need their corresponding owner hooks. A manual assignment owns the relationship and clears both ends when it changes. The verifier detects deliberately injected stale links. The MarkEditor regression exercises repeated insertion through its normal entry point and checks the links before and after retirement. [MarkEditor](../../lambda-input/lambda/io/mark_editor.cpp) · [Subtree retirement](../../lambda-input/lambda/input/css/dom_lifecycle.cpp)
- **Detached nodes.** The document owns its attached root and any detached roots still awaiting disposal, including never-attached API-created nodes. Keep pending roots in the actual retirement queue. Schedule once, cancel on reinsertion, and merge overlapping work. At a quiescent point inspect only pending subtrees, including attributes and shadow ownership. Break embedded-document host cycles, release view and form resources in owner order, then recycle bottom-up. A hold on a descendant keeps its detached ancestors and siblings usable. Last release after adoption schedules the *current* document. Traverse attached and detached roots at shutdown so detached forms and forms allocated before the first view receive teardown. Repeated churn must return to a steady allocation level. [Retirement and form teardown](../../lambda-input/lambda/input/css/dom_lifecycle.cpp) · [View shutdown](../../radiant/radiant/ui_context.cpp)
- **Physical storage.** Factories know whether they constructed an arena node, an embedded Input occurrence, or a view-owned synthetic box. Carry that provenance with the allocation. Derive fixed sizes by type and preserve the original capacity of an inline text allocation even after its string changes. Use the existing element/Attr storage-owner fields for adoption and keep an owned replacement string's physical pool with its text storage. Recycle through `arena_retire(physical_arena, address, exact_size)` under D5.1.4. A Mark import must establish typed embedded provenance or use the source mapping before reading a prefix. Arena address membership alone cannot establish the object's type. [Factories](../../lambda-input/lambda/input/css/dom_element.cpp) · [Storage owners](../../lambda-input/lambda/input/css/dom_element.hpp)
- **Adoption and external handles.** Carry the logical document with the live node, including detached character data. Retain the physical storage owner across adoption. A strong holder releases against the same live node even if its current document or document-local ID has changed. Preserve script-visible identity via the existing wrapper. Rebind document-specific range, selection, and cache membership at the transfer boundary. Keep the generation check at the host boundary under D5.5.1. A live holder can safely check its identity. Use an immutable origin identity for keys that must survive adoption and keep the destination event-log ID document-local. Release cannot depend on looking up the old ID. Before changing a subtree, preflight all owner transfers including attributes and shadow roots. On failure the source tree, holds, and physical owners must still be usable. The host must keep required storage alive or revoke its outstanding handles before document/realm destruction. Keep allocator retention acyclic on adopt-and-return. [Adoption implementation](../../lambda/dom/dom.cpp) · [GC and adoption regressions](../../test/test_radiant_core_gtest.cpp)
- **Mark backing and precise GC roots.** Embedded elements, text, and comments already expose their backing. For a distinct Mark source, bind the occurrence's source key at construction and maintain the existing source-to-result mapping required by D5.5.3. Record actual borrowed type/data ownership on that binding and end it on copy-on-write. Removing an older occurrence must preserve the mapping to a newer one. Only a managed value retained beyond the call gets a stable registered root slot, its physical heap owner, and any compaction fixups. Register before publication and unregister on replacement, retirement, or host teardown. Input-owned values incur zero GC root entries. Keep this exception in the host under D5.5.2 and D6.3.3. The native archive remains runtime-independent under D9.1.1v4 and D9.1.3v2. [Source mapping and storage release](../../lambda-input/lambda/input/css/dom_element.cpp) · [Host GC bridge](../../lambda/module/radiant/radiant_dom_bridge.cpp)

### Where reference counting pays for views and properties

| Object | Recommended ownership |
|---|---|
| Mutable element-owned `BlockProp`, `BoundaryProp`, or layout cache | One element owns it. Its teardown releases it. Reads and writes remain direct. |
| Canonical immutable `InlineProp` borrowed by many elements in one view tree | One canonical arena owns all instances for the tree lifetime. Borrow freely within that lifetime. The first write uses the existing copy-on-write path. A per-element retain and release would add work without enabling earlier reclamation. |
| Immutable page generation retained by multiple independently released views | Keep the existing intrusive `RefCount` on `ViewPageGeneration`. A consumer takes one lease when it publishes the generation. Last release destroys the retired view shell and its resources. |
| Shared CSS payload or a future immutable resource that can outlive its creating view | Count the independently releasable payload or its owning generation. Current style-epoch payloads already do this. Introduce a new count only for a concrete pair of independent owners. Keep back-pointers borrowed, acquire before publishing, and destroy once on last release. A mutable consumer takes a private copy. |

This preserves VR1–VR6 and VR10. A shared descriptor's count protects the memory and resources covered by that descriptor's ownership. It cannot establish the lifetime of a DOM node mentioned by the descriptor. [Canonical prop ownership](Radiant_Design_View_Reuse.md) · [Page generation leases](../../radiant/radiant/view_tree_model.cpp) · [Style payload release](../../lambda-input/lambda/input/css/style_epoch.cpp) · [Shared count primitive](../../lambda-input/lib/ref_count.h)

## Original cutover and acceptance plan

1. Inventory each persistent pointer and address-only ownership probe by producer, consumer, invalidating operation, and teardown. Give each a borrow, an owned snapshot, or an external hold. Start with one weak path (CSS anchor or scroll snap) and one durable path (range endpoints or queued events). A weak-cache miss must rebuild the correct result. A live range or wrapper must retain its observable node across unrelated edits.
2. Migrate the tree writers, allocation provenance, adoption, and feature-owned teardown. In a development build, run the old registry as an independent observer. Compare actual holds, tree reachability, GC roots, and retirement sizes at mutation boundaries. Have the verifier report dangling edges before the recycler runs. Production correctness must pass with this instrumentation compiled out under D5.2.4.
3. Exercise address reuse across types, partial construction, injected allocation failure during adoption, repeated adopt-and-return, source document teardown, held descendants/attributes, shadow slots, iframe cycles, GC compaction, and wrapper-only collection. Run editing, observer, live-collection, and paged-generation tests, and the 10,000-removal plateau cases on the production path. [Native lifecycle regressions](../../test/test_radiant_core_gtest.cpp) · [Range regressions](../../test/test_dom_range_gtest.cpp)
4. Measure the 224 pages and hosted mutation workloads separately. **39.53 MiB after first layout is the gross removable record-and-bucket payload.** Net savings must include any added node storage, Mark source bindings, GC slots, and peak retirement work. Measure first layout, retained reflow, mutation, idle collection, and teardown with the verifier disabled. Check normalized results and the existing 1% median / 3% p95 timing gates. [Measurement convention](Radiant_Design_View_Prop_Groups.md)
