# Radiant DOM and view management

**Status** — Current design as of 2026-11-22. This document consolidates the implemented ownership, layout and editing model.

**Scope** — The DOM-backed HTML path from parsing to first layout, retained reflow, editing and document teardown. The same ownership model also supports presentation of Lambda Mark trees. [View structure and slots](Radiant_Design_View_Struct.md) · [Property groups](Radiant_Design_View_Prop_Groups.md) · [View reuse](Radiant_Design_View_Reuse.md) · [Node lifecycle](Radiant_Design_DOM_Node_Lifecycle.md)

**Formal rulings** — D5.5.1–D5.5.3 cover the shared DOM substrate, retained backing and presentation identity. D5.1.4 covers arena retirement, D5.2.4 covers measurement, D6.3.3 covers precise native roots, D10.2.1 covers editing policy, and S13.3.1v2 covers document-owned task teardown. DV3, DV6, DV9 and VR1–VR15 record the Radiant-specific decisions. [Formal design](../../doc/Lambda_Formal_Design.md) · [Formal semantics](../../doc/Lambda_Formal_Semantics.md)

## 1. The document and its view

Radiant uses one node substrate for the authored DOM and its default continuous view (D5.5.1, DV3). `DomElement` embeds a Mark `Element`, carries the document and tree links, and has slots for resolved properties and layout geometry. `DomText` carries text backing and gains view fragments during layout. `ViewElement`, `ViewBlock` and `ViewText` are typed overlays on the same storage. Size assertions enforce that those overlays add zero bytes. This lets ordinary layout, hit testing and editing share a node identity. Layout can also create synthetic boxes or split text into several fragments. Paged or secondary layout can own separate geometry through a view model. [DOM nodes](../../lambda-input/lambda/input/css/dom_node.hpp) · [DOM elements](../../lambda-input/lambda/input/css/dom_element.hpp) · [View overlays and ViewTree](../../radiant/radiant/view.hpp)

| Construct | First comes into use | Lifetime and contents |
|---|---|---|
| `Input`, its arena and name pool | Parsing | Own parsed Mark items and interned names. In UI mode the parser can construct native element or text storage alongside the Mark value in the Input arena. The document retains the Input for as long as it needs that storage. |
| `DomDocument`, `DomNode`, `DomElement`, `DomText`, `DomComment`, `DomAttr` | Document assembly and later DOM operations | The document publishes an authored root. Child and sibling links own the attached tree. Detached or unpublished roots have explicit document ownership. Attributes exposed as nodes and shadow roots have owning edges. Node storage can live in the retained Input arena or the document node arena. |
| `specified_style`, `StyleTree`, style epoch | Initial cascade and subsequent restyles | The specified style records the winning declarations. An epoch pool owns identical canonical trees, declaration payloads and cascade recipes. Element-specific writes use private storage when needed. An older epoch can survive while a live element still uses it. |
| `DomStyleExt` and `DomStyleRareExt` | When a feature first needs a slot | The document allocates each extension lazily. `ext` groups table, form, transform and pseudo-content slots with optional namespace and pseudo-style data. `rare_ext` holds lower-frequency DOM state and view slots. A document-owned extension can point to a payload owned by the view. |
| `ViewTree`, resolved property groups and `LayoutCache` | First layout or a load-time geometry query | The view owns mutable groups such as `BlockProp`, text rectangles, retained measurement and flow results, and shared canonical properties. A block allocates its list and counter group and its rare style and layout group independently. The view pool survives retained reflow and releases its payloads at node or view teardown. |
| Layout context, scratch arenas and published geometry | Each layout or publication | Trial layouts use scoped scratch. Retained results own any data they need after the pass. Final layout publishes the geometry used by painting, hit testing, selection and geometry queries. Render scratch and display-list storage have their own lifetimes. |

`ViewProp<T, Slot>` and `ViewRef<T, Slot>` are pointer-sized slots. A `ViewProp` marks a payload managed by the view lifecycle. A `ViewRef` lets that slot's policy select private or shared storage. For example, `in_line` can reference a private `InlineProp` or an immutable canonical instance. The template itself stores one pointer. The slot descriptor and sharing flag govern release and mutation. `DomNodeRef` serves a different purpose. It is a weak node identity carrying address, document-local ID and a lifetime stamp. [Pointer types](../../lambda-input/lib/mem_kind.hpp#L116) · [View teardown policy](../../radiant/radiant/view_pool.cpp) · [Weak identity](../../lambda-input/lambda/input/css/dom_lifecycle.hpp)

## 2. Loading and first publication

1. **Parse and retain the Input.** The UI parser builds Mark values in the Input arena. The loader creates a `DomDocument` under the Input's memory context, links the authored DOM and publishes its root. DOM construction preserves the source-to-occurrence relation. Presentation of an ordinary Mark tree gives each constructed occurrence its own parent and sibling links (D5.5.3).
2. **Establish the cascade.** The loader collects UA and author inputs, including linked and inline stylesheets and `style` attributes. It establishes the initial specified styles before scripts can query them. A script-enabled load can request style or layout early and can mutate the tree. The memory census uses a script-disabled load to separate construction from first layout.
3. **Resolve properties and lay out.** Layout creates or reuses a `ViewTree`. It resolves the inherited and computed inputs needed for each formatting context, then resolves used sizes and positions against that context's constraints. Block, inline, flex, grid and table layout can measure before final placement. Layout adds generated content or anonymous boxes where CSS needs them.
4. **Publish final geometry.** Layout settles dependencies that need another pass, such as size container queries and content visibility, then makes the selected geometry available to consumers. Continuous layout publishes an input snapshot so later reads can prove the geometry current. Trial state with pass lifetime is released at the pass boundary. [VR15 in the view reuse design](Radiant_Design_View_Reuse.md#vr15--computation-identity-follows-inputs-and-values-not-execution)

## 3. Retained reflow and invalidation

DOM identity, style validity and layout validity have separate lifetimes (D5.5.3, DV9, VR15). A stylesheet or media change can change the global cascade and start a new style epoch. A local mutation can invalidate only the relevant element or subtree. A change in available space can require new layout while retaining valid specified and computed CSS inputs. Layout and measurement generations, per-node style or subtree revisions, and the cache's constraint and environment keys determine which results can be reused.

At a geometry checkpoint Radiant reconciles pending mutations and checks published inputs such as viewport, fonts, cascade and subtree revisions. A batch proven paint-only can preserve geometry and update its publication. A sizing, text or structural change invalidates affected results and can require a retained reflow. Valid cached results survive where their inputs still match. The retained reset path preserves reusable property blocks, releases external payloads as required, restores source runs behind temporary layout fixups and clears stale geometry and scratch. Full view destruction clears node pointers before releasing their view backing. [Retained layout qualification](../impl/Radiant_Impl_Retained_Layout.md) · [View reuse and generations](Radiant_Design_View_Reuse.md)

## 4. Editing, detached nodes and teardown

- **Mutate the authored tree.** Under D10.2.1 the Lambda DOM behavior package owns editing policy and history. Radiant supplies host detection, Selection and Range, geometry, and checked mutation. Mark-backed edits coordinate the content slots and native links. Range and selection endpoints follow the mutation so that the next layout can compute caret and selection geometry from current text and positions. [Editing design](Radiant_Design_Editable.md)
- **Protect provisional edits.** A native edit transaction retains the identities and inverse data it needs. Provisional removal keeps the backing and view resources available to rollback. Commit releases deferred removals for retirement and publishes queued mutation notifications. Abort restores backed content, links and selection. Retained history deltas can keep nodes alive independently until the delta is released. On detaching text, an owned replacement string stays with the node through reinsertion or retirement. [Editing transaction](../../radiant/radiant/editing_dom_waist.cpp)
- **Retire by ownership.** Attached nodes are reachable from their owners. Detachment makes a subtree a retirement candidate. Native holders such as JS wrappers, live ranges and history entries hold a physical node through its inline external count. A hold on a descendant can keep the detached subtree live. Adoption updates the logical document and node ID, preserves the physical storage owner and expires weak references issued by the source document. A current weak reference can validate in constant time. Stale or unstamped references may require a walk of the live forest after checking the bounded address cache. [VR7–VR9 lifecycle design](Radiant_Design_DOM_Node_Lifecycle.md)
- **Reclaim at a safe point.** Retirement releases subtree resources and schedules physical storage for reuse in its owning arena (D5.1.4). The lifecycle service stores a small root list, detach work and a fixed address cache. Precise GC slots exist only for managed backing that crosses the native ownership boundary (D5.5.2, D6.3.3). Per-reason pin verification is opt-in. Document teardown joins document-owned tasks and releases host references while the DOM is live, then releases view and DOM resources and the retained Input (S13.3.1v2). [Lifecycle implementation](../../lambda-input/lambda/input/css/dom_lifecycle.cpp) · [Document teardown](../../radiant/radiant/ui_context.cpp)

## 5. Release memory census

The census used native arm64 release archives built at Lambda `f31a61fe80`, Input `963a104` and Radiant `01e0111` with the block property split, using Clang `-O3` and full LTO. It covered 53 page fixtures and 171 web templates, with one process per page, a 1200 × 800 viewport, local resources and scripting disabled. The first snapshot follows load and initial cascade. The second follows first layout, before output.

| Main-document allocations across 224 pages | After load | After first layout |
|---|---:|---:|
| DOM document pool | 31.86 MiB | 38.84 MiB |
| DOM node arena | 0.61 MiB | 5.26 MiB |
| Input arena | 65.59 MiB | 65.59 MiB |
| Canonical specified-style epoch pools | 53.46 MiB | 53.46 MiB |
| View property pool | 0.00 MiB | 150.73 MiB |
| Canonical view property arena | 0.00 MiB | 0.37 MiB |
| Other | 0.93 MiB | 0.94 MiB |
| **Total** | **152.43 MiB** | **315.18 MiB** |

First layout adds **162.75 MiB**, including **151.10 MiB** in retained view properties, caches, text rectangles and canonical view storage. The DOM document pool and node arena together account for **44.10 MiB** after layout. Parsed backing and specified styles are already present at the first snapshot.

### Connected element inventory after first layout

The corpus had **72,015 connected elements**, including **61,481 authored** and **10,534 synthetic**. The following counts cover occupied slots on those elements. Count × size gives the base payload only. Optional buffers, child groups and allocator overhead are accounted for by their own allocations.

| Base struct | Owning allocator | Instances | Bytes each | Base payload |
|---|---|---:|---:|---:|
| `LayoutCache` | View property pool | 64,132 | 864 | 52.84 MiB |
| `BlockProp` | View property pool | 64,613 | 160 | 9.86 MiB |
| `BlockListProp` | View property pool | 13,328 | 88 | 1.12 MiB |
| `BlockPropExt` | View property pool | 4,083 | 192 | 0.75 MiB |
| `DomStyleExt` | DOM document pool | 18,628 | 48 | 0.85 MiB |
| `DomStyleRareExt` | DOM document pool | 16,372 | 256 | 4.00 MiB |

The three block property structs use **11.73 MiB** of payload and **14.33 MiB** of pool blocks including headers and alignment. At 800 × 700, retained reflow left **4,175** rare groups and **13,331** list groups allocated. [Block property measurements](Radiant_Design_View_Prop_Groups.md#block-property-split)

`LayoutCache` accounts for about 35% of the view property pool. Its base struct includes the four measurement entries and four intrinsic-width entries. The slot census found **23,221** caches with a valid inline intrinsic-width entry and **171** with a valid measurement entry at this snapshot. Separately allocated computed style, intrinsic-size history and retained flow results can add to the cache footprint. [Cache structure](../../radiant/radiant/layout.hpp#L906) · [Slot census](../../radiant/radiant/view_memory_profile.cpp)

On current arm64, the shells are **96 B** for `DomNode`, **344 B** for `DomElement`, **136 B** for `DomText`, **128 B** for `DomComment` and **176 B** for `DomAttr`. The base size of each derived node already includes its `DomNode` header. `DomDocument` is **3,984 B** and `ViewTree` is **328 B** before their separately allocated backing. At both snapshots every current document used one of its eight inline owner-root slots. Heap-backed root arrays, managed backing root slots and per-node lifecycle records each used **0 B** in this sample.

The measurement sums `direct_bytes` for the main document's tag, deducting child allocator backing from its parent. Input-owned nodes embedded in Mark count under **Input arena**, and separately tagged nested documents and process-wide resources stay outside the main-document total. The small **Other** row includes the style selection pool and `iframe_srcdoc` bytes carrying the main document's tag. Arena counters may include bump-used space, and reserved capacity and process RSS are separate measurements (D5.2.4). [Allocator accounting](../../lambda-input/lib/mem_context.c#L626)

The local per-page release traces are in `temp/block-prop-impl/new.json`.

## 6. JavaScript-enabled release memory census

On 2026-11-22, the hosted arm64 release completed paired JS-off and JS-on runs on the same 224 pages. Both modes used separate processes for each page, a 1200 × 800 viewport, local resources and a 250 ms virtual post-load settling window. The sample follows startup and layout and precedes view serialization and teardown. JS-enabled documents retained their realms through capture. The benchmark exercised startup scripts and locally available dependencies. No input interactions ran.

The build used Lambda `9cc3be8e57`, Radiant `161ad05`, Clang `-O3` and full LTO. Both modes used the same executable with an isolated copy of Input `b35af04`, which supplied the validation-scope API required by this host. The JS-off column is the hosted control for this experiment. Section 5 used the standalone native layout probe.

### Process and document memory

| Process metric across 224 page pairs | JS off | JS on |
|---|---:|---:|
| Median physical footprint | 19.77 MiB | 76.24 MiB |
| p95 physical footprint | 25.92 MiB | 117.66 MiB |
| Median RSS | 36.73 MiB | 101.36 MiB |
| p95 RSS | 44.03 MiB | 142.16 MiB |
| p95 high-water RSS at capture | 44.03 MiB | 143.47 MiB |

With JS enabled, **174 pages** retained a live DOM script realm. Their median paired footprint increase was **63.52 MiB**. The other 50 pages had a median paired change of **−0.02 MiB**. In three pairs on a separate minimal page, enabling one DOM text assignment increased median paired footprint by **29.55 MiB**. Five corpus pages emitted a recognized JavaScript error and still produced a layout. The diagnostic pass recorded 45 script tasks with load failures and 12 skipped by the remote-resource policy.

| Allocator totals across 224 independent page runs | JS off | JS on | Increase |
|---|---:|---:|---:|
| Main-document tagged bytes | 395.81 MiB | 529.73 MiB | 133.92 MiB |
| Root allocator used bytes | 951.00 MiB | 4,473.68 MiB | 3,522.69 MiB |
| Root allocator reserved bytes | 1,992.86 MiB | 10,248.00 MiB | 8,255.15 MiB |

The main-document increase came primarily from the document pool (+49.83 MiB), the view property pool (+31.23 MiB), canonical specified styles (+27.24 MiB), the page Input arena (+18.47 MiB) and the node arena (+4.75 MiB). It includes DOM and style changes from scripts and differences due to HTML parsing with scripting enabled.

### Scripting memory by allocator

| Largest increases in root allocator used bytes across 224 pairs | Increase |
|---|---:|
| Retained JavaScript AST pools (`js.ast`) | 1,219.55 MiB |
| Input arenas outside the main document, including runtime parser backing | 709.43 MiB |
| Garbage-collected heaps (`eval.heap`) | 634.02 MiB |
| Evaluator runtime pools (`eval.runtime.pool`) | 539.11 MiB |
| Lambda direct-parser AST pools activated by the scripting path | 254.00 MiB |

These five categories explain about **95%** of the increase in registered used bytes. Retained JavaScript programs own their AST storage under D8.4.1. The Input arena label covers multiple parser uses and gives no language breakdown. JS-on GC heaps accounted for **638.25 MiB used** and **2,702.00 MiB reserved** across independent runs. Used bytes, reserved capacity and OS residency are separate measurements (D5.2.4). [JavaScript AST pools](../../lambda/js/js_scope.cpp) · [Runtime pool and heap creation](../../lambda/runtime/lambda-mem.cpp)

A separate `MEMTRACK_MODE=STATS` pass recorded **1,566.53 MiB** in additional requested bytes. The largest increases were `system` (+1,154.82 MiB), `js-runtime` (+132.82 MiB), `cache-other` (+114.93 MiB), `container` (+99.62 MiB) and `eval` (+27.30 MiB). `system` groups general runtime allocations, including backing chunks and source copies. `js-runtime` identifies one category of native allocations. Tracked backing allocations can also appear in the allocator registry.

Darwin VM tags attributed **55%** of the added dirtied bytes to `MALLOC_SMALL` (+5,951.16 MiB), **42%** to untagged regions (+4,531.39 MiB) and **2%** to stacks (+244.36 MiB). These regions overlap the allocator accounting. Resident executable mappings grew by **171.72 MiB** in aggregate, with JIT pages grouped among other executable mappings.

The local benchmark report, per-page CSV, and raw allocator and VM snapshots are under `temp/js-memory-benchmark/`.

## Appendix A. Code entry points

| Step | Entry point |
|---|---|
| Read HTML, create the DOM and perform the initial cascade | `load_html_doc` and `load_lambda_html_doc_profiled`. The loader calls `dom_document_create` and `build_dom_tree_from_element`. [Loader](../../radiant/radiant/cmd_layout.cpp#L2305) · [MarkBuilder](../../lambda-input/lambda/io/mark_builder.cpp) |
| Resolve and publish selected layout | `layout_html_doc` runs the layout and settlement passes. `layout_update_document` is the continuous geometry checkpoint and reconciles pending mutations. [Layout](../../radiant/radiant/layout.cpp#L6357) |
| Allocate, reset and destroy the view | `ViewTree::init`, `reset_retained` and `destroy`. `layout_pass_ensure_cache` allocates a cache in the view pool on demand. [View pool](../../radiant/radiant/view_pool.cpp#L1222) · [Measurement cache](../../radiant/radiant/layout_pass.cpp#L680) |
| Apply or undo a checked edit | `dom_edit_invocation_begin`, `dom_edit_invocation_commit_selection` and `dom_edit_invocation_end`. [Native editing](../../radiant/radiant/editing_dom_waist.cpp#L1336) · [MarkEditor](../../lambda-input/lambda/io/mark_editor.cpp) |
| Attribute view allocations | `view_memory_profile_write` inventories view domains, connected-node slots and base struct sizes. `mem_snapshot_capture` gives all allocator domains, including Input. [View profiler](../../radiant/radiant/view_memory_profile.cpp#L225) · [Allocator snapshot](../../lambda-input/lib/mem_context.c#L626) |
