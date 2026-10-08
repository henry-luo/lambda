# Lambda Reactive UI — Design

> **Status:** consolidated working design record; preserves existing decisions and
> proposals without ratifying new language features.
> **Original design records:** 2026-03-27 through 2026-04-12, including later appendices.
> **Consolidated and reviewed:** 2026-10-08, against tree `62f3c21d2` and the
> working changes present at review time.
> **Scope:** the `view`/`edit` template model, state and model ownership,
> pattern dispatch, events, observer-based regeneration, incremental presentation,
> text interaction, drag-and-drop, prior art, alternatives, and unresolved design.
> **Sources consolidated:** `Reactive_UI.md`, `Reactive_UI2.md`,
> `Reactive_UI3.md`, `Reactive_UI4.md`, and `Reactive_UI5.md`.
> Implementation plans, code recipes, debugging records, measurements, file-change
> inventories, test reports, and application examples are excluded.

The formal specifications remain authoritative. This document expands their
reasoning and records the reactive UI decisions not yet distilled into them.
An observed limitation is not a new language rule; unresolved differences are
collected in the final section.

| Subject | Formal linkage | Detailed decision record |
|---|---|---|
| Functional presentation and procedural handlers | **S12.1.1v2–S12.1.3** | This document §§2–3 |
| State ownership and value semantics | **S1.6**, **S9.1.1–S9.1.7**, **S9.2.4v2**, **S9.3.1** | This document §§5–6; [DOM State](Lambda_Design_DOM_State.md), ES4/ES10/ES11 |
| Element mutation and content/fragment behavior | **S12.2.2**, **S2.5**, **S2.6.1v2–S2.6.5** | This document §§4, 6, 9 |
| Event participants and cancellation | **S12.1.3**, **D7.2.5** | [DOM Dispatch](Lambda_Design_DOM_Dispatch.md), ES22–ES29 |
| Script packages and one document's execution ownership | **D7.2.1–D7.2.4**, **D5.3.3** | [DOM State](Lambda_Design_DOM_State.md), ES11/ES12v2 |
| Presentation lifetime and the runtime boundary | **D4.5.1v4**, **D4.5.2** | This document §12 |
| Execution profiles | **D8.1.1v17**, **D7.1.4v2**, **D7.1.7v6** | This document §§2, 12 |
| Bare `apply` and ordinary statement boundaries | **S16.2** | [Syntax](Lambda_Design_Syntax.md#77-apply-fused-token-retired-decided), §7.7 |
| Persistence and stable identity | **S12.1.3**, **S9.1.4**, **S9.1.7** | [State Management](radiant/Radiant_Design_State_Management.md), RS1–RS24 |

## 1. Purpose and design lineage

Reactive UI keeps a transformation from source data to presentation live. A
template declares the shape of data it presents, produces an element tree, and
declares handlers for interactions with that presentation. A handler changes
instance state or an editor-owned model; the system regenerates affected output
and brings the rendered document into agreement with it.

The design serves data processing and document presentation together. Source
data need not already be HTML. An element, map, array, scalar, or named data type
can be presented through an appropriate template. The output uses the same Lambda
element data model and can become HTML, SVG, another document, or a live Radiant
page. Sharing a value model does not collapse the source and result into one
logical document: their responsibilities and mutation authority remain distinct.

The original design combines several influences:

| Concept | Influence | Lambda adaptation |
|---|---|---|
| Data-directed presentation | XSLT template rules | `view`/`edit` patterns and `apply` dispatch |
| Functional component body | React functional components | Functional template body producing element content |
| Component-local state | React state | Named `state` bindings associated with source item and template |
| Event-driven update | React event handling | Procedural `on` blocks following the body |
| Model editing | Two-way binding systems | Explicit `edit` authority over the matched model |
| Immutable history | Persistent structures and Immer | Proposed structurally shared source versions; retained as history and future integration work |
| Functional view and effect boundary | Elm architecture | The `fn`/`pn` boundary enforces the presentation/handler split, **S12.1.3** |

The sequence of the five source records explains the final shape:

- The first record established templates, pattern dispatch, centralized state,
  observer mappings, lifecycle proposals, page access, and persistent-history
  goals. Its early virtual DOM wording was superseded within the same record by
  observer-based regeneration.
- The second established the complete event-to-update contract: matched-model
  context, event payloads, state persistence, child-to-parent notification, model
  mutation, and result ownership across repeated updates. It also exposed the
  identity mismatch between immutable model replacement and live template keys.
- The third unified the context name on `~` and made subtree replacement,
  stylesheet reuse, damage tracking, and safe fallback central to presentation.
- The fourth separated no-op updates, style invalidation, layout invalidation,
  glyph reuse, paint culling, and caret-only damage. These are distinct decisions
  even when they participate in one update.
- The fifth extended the interaction requirements to multi-panel applications,
  file-backed models, inline editing, multiline text, selection, clipboard
  operations, and drag-and-drop. The design obligations below survive independently
  of the particular todo application used to motivate them.

## 2. Principles and boundaries

### 2.1 Functional presentation

A template body is a pure transformation of its model and current instance state
into presentation content. It describes the desired result; application authors
do not manually patch the generated DOM to implement ordinary reactive updates.
The body obeys `fn` rules, and its handlers obey `pn` rules
(**S12.1.1v2–S12.1.3**).

This separation is a language contract. A pure body cannot call a procedural
operation, and procedural-only constructs do not become legal merely because the
body is a UI template. Effects belong in handlers or another authorized procedural
scope. The language's current one-bit effect system remains authoritative; the
additional purity categories explored in the persistence design do not silently
change this contract.

### 2.2 Controlled mutation

Both template kinds may maintain local state. A `view` presents a model without
authority to change it. An `edit` adds model-writing authority in its handlers.
The distinction makes a child's request to alter a containing collection explicit:
a read-only child can notify an editor ancestor instead of acquiring write access
to its parent model.

UI state is owned by template instances, not by mutable function captures or
module globals (**S9.1.4**, **S9.1.7**). State access does not create a general
reference-cell facility or override ordinary value-copy semantics (**S1.6**,
**S9.1.5v2**, **S9.3.1**).

### 2.3 Pattern-directed composition

A parent asks the registry to present a data item. The registry selects the most
specific applicable template. This allows independent templates to specialize
presentation without requiring every parent to name a concrete child component.
Named invocation remains available where an author needs explicit selection.

The tradeoff is less obvious control flow: a reader must understand the applicable
patterns and precedence. Deterministic specificity and tie-breaking are therefore
part of the design, not optional diagnostics.

### 2.4 Observer-directed work

State/model writes identify affected template instances. Regeneration follows
those dependencies; it does not discover all changes by comparing two complete
page trees. Output comparison still has a narrower purpose: an instance can be
dirty yet regenerate equivalent presentation, in which case DOM, layout, and paint
work can be elided.

### 2.5 Presentation and behavior compose

Author templates generate presentation. Behavior templates provide element-kind
defaults on existing elements, including elements generated by author templates.
Both use the same template language and state model. Their attachment mode and
event tier differ; they are not two UI languages. This is **S12.1.3** applied to
the ES11 model in [DOM State](Lambda_Design_DOM_State.md).

### 2.6 Rendering is independent of the execution tier

Template semantics do not depend on whether a body or handler is interpreted or
compiled. **D8.1.1v17** defines Lambda's tiered execution. The design no longer
requires every template to be JIT-compiled before it can participate in a UI.
Likewise, static transformation does not require a window or an event loop.

## 3. Template declaration and context

### 3.1 Declaration parts

`view` and `edit` are template declarations. Their design surface consists of the
following parts, in order:

| Part | Requirement and meaning |
|---|---|
| Template kind | Required; `view` or `edit` determines model-write authority |
| Name followed by `:` | Optional; supports explicit named invocation; otherwise dispatch is pattern-based |
| Model pattern | Required; describes the source item accepted by the template |
| Parameter list | Optional declaration surface; forwarding is discussed in the final section |
| Return contract | Optional; the original declaration design requires the parameter parentheses when a return contract follows |
| `state` clause | Optional, comma-separated named state bindings |
| Body | Required functional body producing presentation content |
| `on` blocks | Zero or more procedural handlers after the body |

The original declaration design defaults the result to an element. The broader
content model also permits useful fragment results; output contracts and fragment
composition must be understood together rather than requiring artificial wrapping
elements (**S2.5**, **S2.6**).

Templates are intended as module-level declarations, alongside other named
definitions. The original proposal excludes declarations nested inside functions
or other template bodies. Nested *applications* remain fundamental to composition.

Event handlers have an event name, parentheses containing zero or one declared
parameter, and a body. Built-in and user-defined event names use the same
declaration form. Their meaning comes from the event source and dispatch contract,
not separate handler grammars.

### 3.2 The matched model is `~`

Within a template body and its handlers, `~` is the matched model item. The event
parameter, when declared, is a different value. State variables are ordinary
named bindings in that template context.

The earlier injected name `it` was removed because two implicit names for the
same model invited confusion. `it` is not a second reserved model accessor.
The resolution of original question **Q1** is retained: body and handler share the
same matched-model meaning. Ordinary nested context-binding expressions continue
to follow their own language scope rules.

### 3.3 Body and handler restrictions

The body follows functional statement semantics. In particular, `var`, assignment,
`while`, `return`, `break`, and `continue` are not admitted by the template body
when they are procedural-only under **S12.1.2**.

Handlers may use procedural local variables, assignment, loops, and early return.
They may perform other authorized effects under **S12.1.1v2**; the initial wording
that restricted all handler effects to state/model mutation was too narrow for
file-backed applications and DOM behavior.

A viewer's handler cannot assign to the matched model or its paths. An editor's
handler can update the model through the document-editing authority. Both can
assign local template state. Passing a view-state binding as a `var` argument is
forbidden by **S9.2.4v2**; procedure calls do not introduce a hidden second writer
to that state.

### 3.4 State declarations

An initialized entry, `state name: value`, denotes template-local state. The
original proposal uses constant/literal defaults and suggests the initialization
hook for model-derived setup. An uninitialized entry, `state name`, binds a known
host-maintained state value under ES4; it is not equivalent to an explicitly
initialized null value.

Bare engine-backed bindings expose canonical state with the host's read/write
rules. Hot interaction state and derived names can be read-only. A template
must not treat a reflected consequence of markup as an independently writable
state variable. The detailed namespace, state ownership, and derived-state rules
remain in [DOM State](Lambda_Design_DOM_State.md), ES4/ES10/ES16.

## 4. Matching and application

### 4.1 Model patterns

The design calls for the existing Lambda pattern/type system to describe template
applicability. The original pattern catalogue includes:

- Element tag patterns.
- Element tags with attribute constraints, including typed fields and pinned
  literal values.
- Structural map patterns with named fields and field types.
- Primitive or collection types and named types/patterns.
- Unions of alternatives.
- A catch-all pattern.

These are design intentions, not a claim that the template matcher already has
the full power of every Lambda type test. Field-name/type checking for maps,
typed attribute predicates, union coverage, and subtype-sensitive ranking need
the conformance work identified in the final section. A field-count approximation
does not redefine structural matching.

### 4.2 Specificity and deterministic selection

Explicit named invocation selects the requested template directly. Among
unnamed patterns, selection follows this precedence:

| Precedence | Pattern class |
|---|---|
| Highest pattern specificity | Element tag with attribute constraints |
| Next | Element tag |
| Next | Structural map |
| Next | Simple type |
| Lowest | Catch-all |

Within a class, pinned attribute values outrank presence/type predicates, then
more constraints outrank fewer. The later behavior-pattern design further calls
for presence predicates to outrank typed predicates (ES3); their complete
distinction remains open. If still tied, the later definition wins. This retains
the original last-match-wins rule and the refinement distinguishing a value
predicate from a non-pinned constraint. Whether merely naming a declaration also
raises its ordinary pattern precedence remains an ambiguity recorded in §18.1.

The first proposal also calls for narrower types to outrank broader types, with
integer/number/any as the motivating relation. That requirement must be reconciled
with complete type-pattern matching; it is not satisfied merely by counting
constraints.

### 4.3 `apply` as explicit dispatch

`apply(target, options?)` presents one source item and returns the selected
template's result. If no applicable template exists, the target passes through
unchanged. The default selects viewers. Edit mode selects an editor when one
matches, and falls back to a viewer when no editor applies; edit mode augments
the presentation vocabulary rather than forcing every data kind to have an editor.

The current public selection options are:

| Option | Meaning |
|---|---|
| `mode` | Selects viewer presentation or editor-enabled presentation; the public spelling uses `"view"`/`"edit"` |
| `template` | Selects a named template directly; the public spelling uses a string name |

The original broader options and parameter-forwarding proposal is preserved in
the final section. Arbitrary named transformation modes such as print/screen are
distinct from the current viewer/editor mode; the two meanings cannot be silently
overloaded.

### 4.4 Recursive descent and child forwarding

Recursion is expressed by applying templates to selected source children or by
the bare `apply` statement, which applies templates to the current model's
children and contributes their results to surrounding content. The child model
becomes the matched context during each child application. Unmatched children
pass through.

The old fused `apply;` spelling is retired. Bare `apply` is a keyword statement;
`apply(...)` is an ordinary call. A semicolon, where needed, is an ordinary
statement separator under **S16.2**, not part of a special token. The rationale is
recorded in [Syntax §7.7](Lambda_Design_Syntax.md#77-apply-fused-token-retired-decided).

The original intended child contract inherits the enclosing application mode,
options, and parameters. The general forwarding contract remains an outstanding
design/conformance item. Authors must not infer a universal recursive pass over
freshly generated output from the historical `recursive: true` proposal.

Child forwarding is also the basic slot/composite-template mechanism: a template
can present its own header or framing and delegate its source children to other
templates. Named slots and a separate slot syntax were not decided.

### 4.5 Fragments and content normalization

Returning multiple root items avoids presentation-only wrapper elements. A
fragment occupies a range in its result parent's content, so replacing it must
replace that entire range, including changes in the number of produced children.

Fragment behavior follows ordinary Lambda content rules: lists spread recursively
into element content, arrays remain single items, null and empty strings disappear,
and adjacent strings merge (**S2.6.2–S2.6.4**). A loop result is not inherently a
broken child that requires a special reactive splat workaround. The current
formal content rules supersede older editing notes that assumed otherwise.

## 5. Instance state and identity

### 5.1 State is separate from presentation

State belongs to a central, session-scoped store rather than to disposable result
nodes. Regenerating or replacing a DOM subtree must not reset its template state.
The conceptual state key is:

**(matched source item, template identity, state name).**

The source component identifies which model item is being presented. The template
component distinguishes alternative presentations of that item. The state name
identifies one declared binding. Named templates use their declaration identity;
anonymous templates need a definition identity. These are conceptual identities,
not a claim that source pointer addresses are suitable serialized identifiers.

### 5.2 Initialization and retention

On first access/application, an existing state entry is restored. If no entry
exists, the declaration supplies its initial value. Subsequent body executions
read current state rather than reapplying defaults.

When an item matches another template, the previous template's state is retained.
If the item later returns to that template, its prior state is available. States
for several templates can coexist for one item. This is the resolution of original
question **Q2**, and it deliberately decouples state lifetime from attachment.

Closing the page/session releases session state. Deleting a source item creates
a separate cleanup question: entries no longer reachable through any live model,
retained version, or intended reattachment may be discarded. The original proposal
suggested lazy orphan cleanup and optional LRU/entry limits; it did not settle
their observable retention policy.

### 5.3 Runtime identity and durable identity

Live source identity supports state lookup and result ownership during a session.
Stable source paths are the identity spine for persistence and reconstruction
([State Management](radiant/Radiant_Design_State_Management.md), **RS7**).
An address that survives one re-render does not survive rebuilding the model or
restoring a page.

Durable template-state addressing therefore identifies a source location and
template definition. Durable interaction state on a generated node additionally
identifies a location within that template's output. Exact encodings, repeated
application of one source item, anonymous-template identity, and collisions in
nested output remain open. This prevents an application-level `id` attribute,
a source path, and a live object identity from being mistaken for interchangeable
keys.

### 5.4 One canonical state model

Author and behavior templates share the state model, declaration syntax, and
state access semantics (ES11). Behavior templates attach to live elements; author
templates attach to model/result relationships. Neither creates a second canonical
focus, selection, checked state, or text value.

State must be distinguished from consequences of state. Durable and transient
values have owners; derived geometry and projections are recomputed by consumers.
Keeping redundant derived copies creates disagreement and extra invariants
([State Management](radiant/Radiant_Design_State_Management.md), RS1/RS2).

## 6. Model mutation, document structure, and history

### 6.1 Editing authority

An editor handler can update attributes/fields, replace or remove fields, and
insert, replace, or delete children and array entries through the document-editing
model. Both the map-like and content-like faces of an element are covered by
**S12.2.2**; using an untyped matched item must not erase this distinction.

These writes identify the source model and affected presentations. A model write
cannot be assumed observable only by comparing the matched item's identity:
the same live source may have changed content. The update contract must record
the mutation and schedule every dependent presentation whose inputs changed.

Editing one instance must not alter the interpretation of unrelated instances
that share its type/shape. Shared field descriptions remain immutable; a
layout-changing edit changes the edited value's description without rewriting
its siblings' contract (**D3.4.3v5**, **D3.4.5**). This is the design invariant
behind the later shared-type correction in the original notes.

### 6.2 Live reactive editing and immutable editor history

The ordinary reactive edit path preserves live source identity. It must not be
documented as automatically creating an immutable whole-document version after
every handler. A separately owned editor session can maintain immutable document
versions and selection snapshots, but that facility is not an implicit guarantee
of every `edit` template.

The original persistent-document proposal remains valuable: construct a new root
for a committed edit, share unchanged subtrees, and retain older roots for undo.
Only the edited node and its ancestor path need new logical versions; unchanged
branches remain shared. This offers work proportional to edit depth rather than
copying the entire document.

The unresolved integration is identity. If replacing a model node changes its
live identity, state keys, source/result ownership, selection anchors, and ancestor
dependencies must follow the new version. Keeping the old mapping is not a valid
way to preserve the state: it would regenerate from stale source data.

### 6.3 History contract

The initial design proposes a sequence of versions, each containing a document
root, a version number, and an optional description. Commit establishes a version;
undo selects a predecessor; redo selects a successor; selecting a version
regenerates affected presentation.

History must preserve relevant selection alongside the document, define redo-tail
behavior after a new edit, and separate view-only state changes from document
mutations. Retention limits govern which old versions remain available. Shared
subtrees must remain valid as long as any retained version uses them.

Text-control history, contenteditable history, and source-document history are
different domains. **D7.2.5** places contenteditable editing history and command
policy in the shipped DOM behavior package. The source-model history proposal
does not supersede that ruling or merge all undo domains automatically.

### 6.4 Parent navigation under structural sharing

A persistent source can share one subtree across several document versions. A
single intrinsic parent link cannot express all those logical parents. Parent
navigation must therefore be relative to a document/version and source location.

The original alternatives are retained:

1. Identify the owning document/version before resolving a parent.
2. Traverse from that version's root to locate the node's path and parent.
3. Cache parent relationships within that document/version and invalidate them
   when the version changes.

The first proposal suggested identifying the root through allocation ownership.
Ownership alone cannot identify a unique logical version when versions share
subtrees, so the version/path context remains essential. Top-down template
application does not require parent navigation. Removal of source parent links
must not be confused with removing the ordinary parent relation of a live DOM.

## 7. Events and template communication

### 7.1 One event pipeline

The current event design is **one propagation engine, two tiers, three addressing
modes, one event record**. [DOM Dispatch](Lambda_Design_DOM_Dispatch.md), ES22–ES29,
owns the detailed decisions where no formal ruling covers them.

The author tier includes JS listeners and author-template handlers. They
participate in the same event path and cancellation state. Once author dispatch
settles, the UA tier may perform an uncanceled default through behavior templates.
Trusted platform input and synthetic DOM dispatch enter the same model; trust
remains explicit where an action requires it.

The three addressing modes remain distinct:

| Participant | Attachment |
|---|---|
| JS listener or event property | A particular event target |
| Author-template handler | A source/template instance that produced result content |
| Behavior-template handler | A selector/pattern matching an existing element for that event |

Combining their propagation does not require flattening them into one registration
model. Doing so would lose either model binding, selector-sensitive defaults, or
the author/UA boundary.

### 7.2 Order and propagation

The propagation path includes the target, its ancestors, the document, and the
window. JS capture listeners run in the capture phase. At the target and in the
bubble phase, the node's JS listeners precede its author-template handlers
(**ES23v2**). Lambda template handlers currently participate at target/bubble;
a template capture declaration is a separate open extension.

An author-template handler's participation does not end the author walk. Every
governed node can participate at its place in the cascade. Propagation stops are
shared across realms; a stop does not itself cancel the default action.

### 7.3 Cancellation and verdicts

Only the event's default-prevention state suppresses the UA tier. Returning
`'prevent-default'` is Lambda's verdict spelling of that operation. Merely
handling the event, returning an ordinary result, or stopping propagation does
not suppress a checkbox toggle, form default, or other UA behavior
(**ES24**, **ES29**).

`'pass'` has different tier consequences. In the author tier it does not control
the ancestor walk. In the UA tier it declines a claim and allows the behavior
search to continue. The UA tier retains first-claimant semantics and its accepted
per-event call budget. Errors and unavailable package behavior follow the
capability/default-action policy in the DOM design; there is no blanket promise
that retired native policy will reappear as fallback.

### 7.4 Event data

An in-flight event is one record visible in both language realms (**ES24**).
It contains event identity, target and current propagation position, phase,
bubbling/cancelability, trust, prevention/stop state, and the payload relevant to
the event. The initial event-map design is superseded by this shared record;
useful Lambda-facing fields are retained as projections rather than independent
copies of cancellation state.

The design vocabulary includes:

| Payload family | Information |
|---|---|
| General | Event type, target, current target, phase, trust and cancellation |
| Target convenience fields | Target tag/class, parent class, and target text where supplied |
| Pointer | Coordinates, buttons, modifiers, and relevant hit-tested target |
| Keyboard | Key name, Shift/Ctrl/Alt/Meta, and the legacy modifier-mask projection where supplied |
| Text | Input character/data, caret position, and current value where the contract supplies it |
| Selection | Selection start/end and direction appropriate to the text domain |
| Clipboard | Pasted or cut text and the affected range |
| Drag | Drag data and current drop-target identity/tag/class |

Availability is event-specific. An event does not promise all fields for all
types. In particular, byte offsets, Lambda code-point indices, and DOM UTF-16
selection indices must never be treated as the same unit; §13 describes the
current text-control contract.

### 7.5 Event families

The first design deliberately adopted HTML/DOM-style names:

| Family | Names and purpose |
|---|---|
| Pointer buttons | `click`, `dblclick`, `mousedown`, `mouseup` |
| Pointer motion | `mousemove`, pointer events where exposed, `mouseenter`, `mouseleave` |
| Scrolling | `scroll`, `wheel`, and supported wheel-name projections |
| Keyboard | `keydown`, `keyup` |
| Editing | `beforeinput`, `input`, committed `change` |
| Focus | `focus`, `blur`, `focusin`, `focusout`, with each event's own propagation contract |
| Forms | `submit`, `reset`, and associated default-action hooks |
| Clipboard | `copy`, `cut`, `paste` |
| Drag-and-drop | `dragstart`, `dragmove`, `dragover`, `dragleave`, `drop`, `dragend` |
| Composition | Composition start/update/end and input intentions governed by the DOM package |
| Application | Any custom name handled through `emit` |

The catalogue preserves the intended vocabulary; it is not a declaration that
every named event is generated at every entry point. The
[default-action record](Lambda_Design_DOM_Default.md) owns the detailed event and
element coverage. Synthetic enter/leave, committed change, and submission must
have explicit generation/ordering semantics rather than being aliases for raw
input callbacks.

### 7.6 Continuous-event economics

Motion, wheel/scroll, and drag-over events can arrive many times per gesture.
Author templates that declare them receive them (**ES23v2**). A page declaring
none should not incur template-dispatch work for each motion event.

A continuous event does not trigger loading a behavior package. Already available
behavior can participate when it declares that event. This preserves both
application control over an owned viewport and the original low-overhead goal.
The old unconditional exclusion of continuous events from all template handlers
is superseded.

### 7.7 `emit` and lifting editing authority

`emit(name, payload)` is a procedural notification from the current template
handler to the nearest enclosing template that declares that name. The receiving
handler's event parameter is the payload, not a newly fabricated pointer event.
Its matched-model context belongs to the receiving template.

The search skips the emitting instance, examines enclosing template ownership,
and stops at the nearest matching handler. This differs from author DOM-event
propagation, which can invoke handlers at multiple nodes. `emit` does not promise
a returned value or a broadcast to all ancestors.

This mechanism was chosen over letting a read-only child directly delete or
rewrite its parent's model. The child communicates intent; the ancestor with
editor authority makes the change. It follows the lifting-state-up rationale
without coupling the child to a specific parent's name or storage path.

The currently established use is inside an active handler with a mounted
presentation context. Arbitrary calls from application code or background work
require a separate destination/lifetime contract.

### 7.8 Several templates may own one result element

A template can directly return another template's result. Both then participate
in producing the same element without creating an extra DOM wrapper. Ownership
forms a chain from the innermost producing template outward.

At that element's author-dispatch position, matching template handlers run in
that order. An emitted notification examines enclosing owners on the same
element before moving to a parent DOM element. Regenerating the inner result
updates the enclosing templates' result association as well. This preserves
composition semantics independently of incidental DOM nesting
([DOM Dispatch](Lambda_Design_DOM_Dispatch.md), **ES27** elaboration, 2026-10-07).

## 8. Initial attachment, lifecycle, and settling

### 8.1 Initial presentation

Initial application selects templates, establishes each instance's state, executes
functional bodies, and records the relationship between source instances and
their output. The complete result then undergoes CSS cascade, layout, and paint.
Event ownership is available on the resulting presentation.

State defaults are established before the body reads them. An initialization
hook, where defined, runs before the first presentation and its state changes
belong to that initial pass rather than scheduling a second redundant pass.

### 8.2 Lifecycle vocabulary

The original lifecycle design is retained in full as a contract proposal:

| Hook | Intended timing and role |
|---|---|
| `init` | First attachment to a source item; model-derived initialization and setup before first render |
| `mount` | Once after the first live result is laid out and presented; measurement or resources needing a live presentation |
| `update` | After a subsequent regeneration caused by changed inputs; follow-up visual/effect work |
| `error` | Error boundary for child rendering/handler failures; can select fallback presentation |
| `unmount` | Before an instance detaches because its model is removed or no longer matches; resource cleanup |
| `suspend` / `resume` | Later persistence design: release and reacquire resources around hibernation while preserving state |

The original React analogies are `componentDidMount`/an empty-dependency effect
for mount, and `componentDidUpdate`/a recurring effect for update. They motivate
the timing distinction; they do not import React's complete effect semantics.

This table does not assert a complete automatic author-template lifecycle. The
behavior system's initialization phase has its own established meaning: controls
receive initial behavior when they become live, even before a user interaction.
That does not by itself implement every author-template hook above. Ordering,
reattachment, and error-boundary details remain in the final section.

### 8.3 The update boundary

An event is delivered to its participants; their procedural mutations identify
dirty instances. Regeneration and DOM reconciliation are settle stages of the
event pipeline, followed by style/layout/paint work that is actually necessary.
Dispatch and regeneration are separate responsibilities (**ES28**).

The initial proposal groups multiple changes into one render opportunity. This
is still the batching goal, not a claim that every possible sequence of events is
already deferred into a single frame. A settled state must not expose a mix of
new template ownership and an unrelated old DOM.

## 9. Observer-based regeneration

### 9.1 Source/result ownership

The conceptual render relation associates **(source item, template identity)**
with produced result content and its location in the containing result. A reverse
relation identifies producing instance owners from a result element for events.

The relation includes the result parent, child position/range, and dirty status.
It supports a single element, fragment content, and nested owners sharing a
result. Source-document paths are distinct from result-tree locations: one says
where the input lives; the other says where its current presentation was placed.

Both directions must agree after regeneration. Event dispatch must always bind
to the current source and template. Replacing an output element cannot silently
disconnect its new presentation from state or handlers.

### 9.2 Marking dependencies dirty

A local-state write affects its owning template instance. An editor write affects
the presentation of the modified source and any enclosing/dependent source views
whose transformation reads the changed data. Deleting, adding, or reordering
source children can also change the containing template's presentation.

Precise dependency information is the opportunity to avoid unnecessary work; it
is not permission to miss an affected reader. The original DAG proposal used
changed-version paths to identify ancestors. A live identity-preserving editor
needs an equally correct invalidation contract even without new source pointers.

### 9.3 Regeneration order

Dirty instances execute their bodies with current model and state. Their new
results replace the old result element or fragment range, and ownership/location
relations are updated before subsequent events rely on them.

The original design chooses top-down dependency order: when a dirty parent
regenerates children, a child's obsolete dirty mark should not cause duplicate
work against output the parent has already replaced. Unchanged descendants are
preserved or skipped when their inputs and ownership remain valid.

This is a required consistency property, not a promise that every update costs
one lookup. A body can itself recursively apply child templates, compute aggregate
data, or produce substantial output. Dirty discovery, ownership maintenance, and
layout may also require broader work.

### 9.4 No-op elision

Dirty means a presentation *may* have changed. After regeneration, equivalent
output permits skipping DOM replacement, cascade, layout, and content paint.
Equality considers element kind/tag, attribute names and values, ordered children,
and nested content, with identity equality as an early success where meaningful.

This comparison is limited to regenerated results. It does not reintroduce a
whole-page virtual DOM diff. Its cost depends on the compared output, and a
semantically unchanged output is not necessarily pointer-identical.

No-op content does not discard unrelated interaction effects. Focus, caret,
selection, scrolling, or default actions may still require their own settling or
overlay damage even when the template result is equal.

### 9.5 Observer and tree-diff tradeoffs

| Concern | Whole-result discovery by diff | Observer-directed regeneration |
|---|---|---|
| Identifying changed inputs | Discover differences after rendering | Writes identify affected instances |
| Unchanged output | Traversal/comparison can be necessary | Unaffected instances need not execute |
| Allocation | Replacement candidate trees for rendered components | New output for dirty instances |
| Identity | Matching/reconciliation rules | Explicit source/template ownership; stable identity still needs a contract |
| No-op output | Commit can be skipped after comparison | Same opportunity after a dirty body's regeneration |
| Complexity risk | Matching heuristics and child identity | Correct invalidation and ownership maintenance |

The original claim of constant-time state-only updates describes the narrow case
of one bounded template body. It is not a general complexity guarantee for the
entire event pipeline or for React's behavior.

## 10. Incremental presentation

### 10.1 Replacing only affected content

Changed result subtrees replace corresponding DOM subtrees. Unchanged DOM,
resolved styles, and layout results remain reusable. The replacement must preserve
the containing order and parent relation and publish the new source/result
association. Fragment ranges may need a broader correctness fallback than a
single-element replacement.

Full rebuild remains the correctness path for initial presentation or when the
change cannot safely be expressed as a local patch. Missing ownership/location
information, replacement of the document root, or unsupported result shapes must
not produce a partial page merely to preserve an incremental path.

### 10.2 Stylesheet reuse and scoped cascade

Parsed stylesheets can be reused while the stylesheet source is unchanged.
Replacing a content subtree does not normally require reparsing every stylesheet.
New nodes need fresh cascade; unchanged nodes retain valid resolved style.

Inherited values come from the already resolved parent context. Class/attribute
changes invalidate the matching subtree. Ancestor and sibling dependencies must
also be honored where selectors or layout context make them relevant. A local
cascade cannot be justified solely because the changed node is small.

The design avoids blanket style invalidation on every reactive event. It also
distinguishes style from layout: an automatic height changing after content
changes is a layout dependency even if the specified style is unchanged.

Both external stylesheets and ordinary embedded/style attributes belong to the
presentation model. A new template-specific CSS language or automatic style
encapsulation is an independent proposal, not a prerequisite to reactive UI.

### 10.3 Layout invalidation

Layout work follows the scope of the dependency:

| Change | Intended minimum safe scope |
|---|---|
| Paint-only state with unchanged geometry | Restyle/paint the affected output as necessary |
| Local content change with stable outer dimensions | Changed subtree and required context |
| Child insertion/deletion or changed outer dimensions | Containing flow and dependent ancestors/siblings |
| Complex formatting dependence or uncertain context | Broader formatting scope or full layout |

Unchanged siblings can retain their measured/layout results, but their position
may shift when an earlier sibling's contribution changes. Preserving a sibling's
size does not permit leaving it at an obsolete position.

The initial isolated-subtree proposal requires stable outer width/height/margins,
a compatible containing block/flex context, and no structural selector effects.
Grid, table, cross-axis sizing, or other complex dependencies require conservative
escalation. Reusing valid layout along dirty paths avoids requiring arbitrary
formatting-context reconstruction for every local update.

### 10.4 Damage regions and repaint

Before replacement, capture the old painted area. After style/layout settling,
include the new area. Both matter: the old location must be cleared and the new
location painted. Changed dimensions or movement can expose or shift neighboring
content, expanding damage beyond the replaced subtree.

Dirty regions are expressed in logical CSS coordinates and transformed to the
output surface's physical coordinates. HiDPI scaling must not mix those units.
Selective surface clearing and selective scene traversal are separate operations:
limiting the clear does not by itself eliminate paint work outside the region.

Cull a subtree only when its paint extent cannot intersect damage. Paint extents
include overflow, positioned descendants, shadows, and other effects; ordinary
layout rectangles are insufficient. When reliable bounds are unavailable, use a
broader traversal/repaint. A fixed guessed overflow margin is not the correctness
contract.

The original heuristic falls back to full repaint when damage covers more than
half the viewport or document-height/scroll changes make local repaint unsafe.
The exact threshold is a tuning choice; correctness always takes precedence over
the selective path.

### 10.5 Glyph reuse

Repeated presentation of unchanged text should reuse valid glyph rendering data
rather than reload the same glyph every frame. Cached rendering depends on the
font instance, code point, and rendering configuration, including scale/DPI and
the intended rendering use.

Invalidation follows font replacement/unload, relevant scale changes, and the
owning presentation lifetime. Fallback-font results must be reused consistently
with the original requested font. The design concerns reusable glyph data; it
does not require one particular vector-shape representation.

### 10.6 Caret and interaction-only updates

Caret movement, blink, or selection/focus changes with no content change should
not regenerate the source-derived presentation merely to draw an overlay. Damage
includes both the old and new caret areas, and restoring the old area must repaint
the underlying content correctly.

This path is conditional. Focus may change selector matching or layout, selection
can require more extensive highlighting, and scrolling moves content. Such
changes are not automatically caret-only. Overlay optimization must preserve the
same visible result as a complete paint.

Interaction decorations are derived presentation. Selection highlights, drag
markers, and similar overlays do not become source-document content or document
history merely because they are visible.

## 11. Behavior templates and DOM defaults

Behavior templates attach by matching existing elements for the event being
handled. They are not candidates for author `apply` presentation. Their provenance
determines the behavior tier; authors do not use a different declaration keyword.

An author template's `~` is its source model item. A behavior template's `~` is
the live DOM element exposed through the shared DOM value vocabulary. Both
can use declared state, but their identity/attachment contracts differ
([DOM State](Lambda_Design_DOM_State.md), ES1/ES2/ES11).

The same behavior package governs parsed HTML and author-template output. This
eliminates the need for a separate handwritten checkbox, caret, paste, or editing
policy for every Lambda app. Native presentation mechanisms remain shared across
language realms.

**D7.2.5** assigns full user-agent editing policy to the shipped Lambda DOM
package: uncanceled contenteditable actions, structural normalization, history,
`designMode`, and the complete `execCommand`/`queryCommand*` compatibility surface.
The engine retains platform transport and generic DOM, event, selection/range,
geometry, clipboard/composition, and mutation mechanisms.

Text-control policy follows ES9/ES17/ES18 in
[DOM State](Lambda_Design_DOM_State.md): edit decisions, range replacement requests,
word/line boundary policy, paste treatment, length constraints, history restore,
change-on-blur decisions, and composition policy belong to behavior. Canonical
storage and geometry remain mechanisms. An application can prevent a default
and perform an authorized replacement; it must not accidentally apply the same
edit twice through both application and default handlers.

## 12. Session ownership, persistence, and static output

### 12.1 A document owns its live execution

Templates, state, handlers, and model/result associations must remain valid for
the page's live session. Evaluation cannot destroy the semantic context needed
by the next event. Loading, event handling, and page teardown have explicit
ownership boundaries.

JS and Lambda handlers for a document share the document's script execution
ownership (**ES12v2**). Stateless document loaders may share window-owned loading
services, but those services are not the handler session. Script pages and editor
applications retain their own session; sharing a loader does not merge the state
of unrelated pages.

The original assertion that retaining a runtime permits arbitrary long-lived
presentation pointers into its heap is superseded by **D4.5.2**. A value retained
across the Lambda/Radiant boundary must have document-owned lifetime or a registered
root lasting as long as the reference. Transient reading is a borrow. This is an
ownership contract, not an instruction to disable collection or infer liveness
from native stack contents.

### 12.2 Persistence by regeneration

For a Lambda page, rendered DOM is derived from model and template state. The
persistence design retains model changes, template state, and durable interaction
state, then regenerates presentation rather than serializing the whole generated
DOM (**RS5**).

State has three durability classes (**RS1/RS2**): durable state survives a snapshot;
transient state is discarded on restore; derived state is recomputed. Snapshots
belong at settled boundaries. Restoring the same model and state must reproduce
the same structure and ordering (**RS3/RS6**), with source-path identity and
template-relative output addresses used for rebinding (**RS7**).

The principle for resources is separate: state survives hibernation, resources
are reacquired (**RS8**). The proposed suspend/resume hooks must not reinitialize
retained local state. Timers pause and resume with relative time unless their
policy opts into dropping; work that must survive page eviction belongs to a
longer-lived owner and communicates results by request identity (**RS9/RS10**).

The later state-management design also records event-sourced replay, view-state
checkpoints, a separate markup-history track, and time-travel tooling
(**RS22–RS24**). Those are extensions of deterministic regeneration, not a claim
that the basic reactive template already supplies a complete persistence or
debugger product. Their full rationale remains in
[State Management](radiant/Radiant_Design_State_Management.md).

### 12.3 Static and server-side use

Because a body produces ordinary element content, `apply` can participate in a
batch transformation and serialization flow without an interactive event loop.
The original SSR suggestion relies on this separation. The same presentation
definitions can support static and live output where their required capabilities
are available; browser hydration and transfer of a live session are additional
contracts, not consequences of serializing HTML.

Build profiles constrain capabilities without changing retained language semantics.
For example, **D7.1.7v6** retains stateful template application in the browser
evaluation profile while excluding `emit` and editor-history operations.
Window/UI commands are separately constrained by **D7.1.4v2**. Headless template
application and opening a live window are distinct capabilities.

## 13. Text interaction, selection, and clipboard

### 13.1 Inline editing

Display and edit presentation can switch using template state. Entering edit mode
replaces a display node with an input or textarea; leaving it commits or cancels
according to application policy. A read-only child sends text-update intent to
an editor ancestor through `emit` rather than writing its model directly.

Inserted controls that request autofocus need focus once they become live.
Regeneration must preserve applicable focus and selection when a control's
presentation is replaced. Automatically focusing any new input was considered
as a shortcut; the meaningful contract is an explicit autofocus/focus request,
not stealing focus on every insertion.

Blur informs an inline editor that focus left, allowing save-and-close policy.
Committed `change` must have defined ordering relative to `blur`; the DOM behavior
design determines whether the value changed before publishing those events.
Escape/click-away behavior and whether to save or cancel remain application policy.

### 13.2 Canonical text value and units

An input/textarea has one canonical live value. Initial markup supplies its
initial/default content; ongoing editing must use current value state. The first
textarea proposal treated a `value` attribute as the text source; it does not
override the current DOM text-control contract.

Text storage, user-visible character navigation, Lambda string operations, and
DOM selection are different coordinate domains. DOM text-control selection
positions use UTF-16 code units, while Lambda string indexing follows its code
point rules (**S2.5.8**, **S8.3.1v3**). Storage byte offsets are an internal
representation. A handler cannot splice a Lambda string using a DOM index without
the required conversion. The old undifferentiated "character index" event wording
is insufficient for Unicode input.

### 13.3 Multiline presentation

Textareas preserve explicit newlines and soft-wrap logical lines to the available
width. Caret hit testing and navigation must map between logical text positions
and visual wrapped lines. Proportional glyph advances matter; the original
monospace-first suggestion was a development simplification, not a permanent
language or UI restriction.

| Action | Single-line input | Multiline textarea |
|---|---|---|
| Enter | Submission/activation policy as appropriate | Insert a line break unless prevented or overridden |
| Left/Right | Adjacent text position | Adjacent text position, crossing line boundaries |
| Up/Down | Applicable control policy | Adjacent visual line, preserving intended horizontal position where possible |
| Home/End | Text boundary as specified | Current visual/logical line boundary according to platform policy |
| Document-boundary shortcut | Start/end of control value | Start/end of whole control value |

Long text requires scrolling. The caret should remain visible during navigation,
and text/selection painting is clipped to the control's viewport. Scroll offsets,
wrapping width, padding, glyph measurements, and caret geometry must describe the
same visual coordinate system.

### 13.4 Selection model

A text-control selection is a range within one control, with anchor/focus and
direction. It does not require a separate competing document selection owner.
The initial proposal for a dedicated textarea selection object was superseded
by reuse of the canonical selection model.

| Gesture | Intended effect |
|---|---|
| Click | Position caret and collapse selection |
| Shift+click | Extend from the existing anchor |
| Press and drag | Establish anchor, extend focus with pointer motion |
| Shift+Left/Right | Extend by one text position |
| Shift+Up/Down | Extend by one visual line |
| Shift+Home/End | Extend to the applicable line boundary |
| Select-all shortcut | Select the whole control value |
| Unmodified navigation with a selection | Collapse or move according to the directional/platform contract |

Selection highlights span each affected visual line and are painted behind text;
the caret is painted afterward. Range extraction, visual highlighting, and edits
must use the same canonical selection, regardless of selection direction.

### 13.5 Clipboard and selection replacement

Copy extracts the selected text without changing the model. Cut copies and removes
that range. Paste inserts clipboard text at the caret or replaces the active
selection. Backspace/Delete remove the selected range when present; otherwise
they perform the appropriate adjacent deletion. Typed text and a multiline Enter
also replace the selection rather than leaving selected text behind.

Caret/selection results follow the edit: deletion collapses to the range start;
replacement collapses after inserted content; selection is cleared or updated
once, by the canonical editing path. Input, paste, cut, and key-intent contracts
must identify the pre-edit range consistently.

The earlier alternative introduced a separate `select_delete` event with start/end
fields. The adopted direction carries selection information on existing editing
events and uses the common `beforeinput` intention/default/`input` contract, rather
than inventing a parallel deletion event protocol. Application handlers can
intercept/cancel default editing, but cancellation and "already applied" must not
cause a second edit or restore an obsolete selection.

History restoration belongs to the same editing authority as ordinary edits.
Undo/redo should restore value and selection without recording the restore as a
new edit; a canceled restoration must not advance history. This follows the
one-writer rationale in ES17 and the package ownership in **D7.2.5** for rich DOM
editing.

## 14. Drag-and-drop

### 14.1 Gesture and ownership

Element dragging is distinct from selecting text. A draggable source becomes a
candidate on press, then an active drag once the five-CSS-pixel movement threshold
is exceeded.
The source records the drag payload and initial/current pointer positions. The
current eligible drop target is separate from the source.

The original interaction sketch also considered a 150 ms hold delay or an immediate
drag handle. These are alternative initiation policies; the movement threshold
is the established simple contract, not an unconditional timed long-press feature.

### 14.2 Event sequence

On activation, `dragstart` reaches the source. While active, motion identifies
the nearest eligible drop zone, sends `dragleave` to the previous zone and
`dragover` to the current zone as appropriate, and sends the source `dragmove`.
Releasing over a valid target sends `drop`, followed by source `dragend` and target
cleanup. A handled drag must not also activate an ordinary click.

The original convention uses `draggable` to identify sources, `dropzone` to mark
targets, and `dragdata` for drag-type data. These are the reactive design's
conventions; they are not a claim of complete browser `DataTransfer` compatibility.
Event payloads identify drag data and the hit-tested drop target.

### 14.3 Feedback and cancellation

The user must see a drag indicator and eligible-target feedback. The initial
proposal suggested a lifted/translated translucent source copy, reduced original
opacity, and a highlighted target. A simpler cursor/insertion marker is an
accepted alternative; a full source ghost is not required by the event model.

Drop outside a valid target cancels the move. Escape cancellation, target removal,
and source/target replacement during regeneration need explicit cleanup. In every
case, transient drag state must stop referring to detached presentation.

### 14.4 Model coordination and frequency

Moving between collections is coordinated by an ancestor with authority over both
models. Source and target templates notify that owner; the owner removes from the
source, inserts into the target, and applies persistence policy. Item identity
must remain unambiguous across the containing document.

Frequent pointer motion should primarily update transient interaction/overlay
state. The first proposal suggests coalescing drag notifications to a presentation
frame (approximately one notification per 16 ms) rather than regenerating a whole
application for every raw motion. Hit
testing and invalidation must remain fast without weakening correctness.

## 15. Application composition and file-backed models

The advanced application record contributes design requirements beyond its
examples. A top-level editor can own the active document, a directory listing,
file-operation mode, and child collection state. A multi-panel layout is ordinary
CSS composition; it does not need a new template layout primitive.

File selection changes the active source model and the highlighted selection.
Creation can use inline name entry, then create a document and refresh the listing.
Rename changes the backing path and updates active-file identity. Delete removes
the actual file and reconciles the selected model/listing. These are procedural
operations under **S12.1.1v2**, not functional body effects.

The two-click confirmation proposal is retained: first expose the destructive
intent visually, then require a second deliberate action. It avoids making a
native modal-dialog facility a prerequisite to safe application-level file
management. Tombstone/empty-file substitution is not the deletion contract.

The earlier record proposed dedicated `delete_file(path)` and
`rename_file(old_path, new_path)` procedures with ordinary error results on failure.
The current I/O vocabulary already provides deletion and rename/move operations;
new duplicate builtins are not required merely to support this application design.
The failed-operation path must leave the active model and file listing consistent.

Within a document, collection editors can add items inline, present a short text
summary, open multiline detail editing, and coordinate cross-collection moves.
Persistent item identifiers are unique within their document; display position
alone is not durable identity. Filenames can supply document labels, while each
file contains its own category/item model and multiline notes.

Saving after every edit, saving on blur/explicit action, and how to handle
unsaved file switches are application policy. The framework must provide clear
model-change notifications and state lifetime so such policy can be expressed;
it must not silently promise an automatic persistence strategy.

## 16. Prior art and design tradeoffs

### 16.1 XSLT

XSLT provides pattern-matched rules, a current source node, recursive template
application, and conflict resolution by precedence/priority. It also separates
named templates and modes. Lambda borrows the data-directed composition, while
using its own values and specificity rules. See the primary
[XSLT template-rule specification](https://www.w3.org/TR/1999/REC-xslt-19991116#section-Defining-Template-Rules).

The original comparison concerns batch transformation: a stylesheet transforms
source into result; it does not define Lambda's live component-state and event
contract. Lambda retains source-to-result ownership and makes handlers trigger
targeted regeneration. The important inheritance is separation of data from
presentation, not XML syntax or a requirement that source and result have the
same tree structure.

| Aspect | XSLT influence | Lambda choice |
|---|---|---|
| Selection | Match rules over source nodes | Existing Lambda data patterns |
| Recursive composition | Apply templates to selected children | Explicit `apply` calls and bare child application |
| Dispatch conflicts | Import precedence and rule priority | Pattern specificity, constraints, later definition |
| Current item | Source-node context | `~` matched model |
| Named presentation | Named templates | Named template selection option |
| Multiple presentations | Modes | Viewer/editor mode today; arbitrary named modes remain proposed |
| State and events | Outside the batch transform contract | Instance state and procedural handlers |
| Result | Constructed document content | Ordinary Lambda elements/fragments, static or live |

### 16.2 React

React supplies the component-state analogy and the separation between calculating
UI and committing changes. Its current documentation describes state-triggered
component rendering, pure calculations, and minimal DOM changes at commit. The
original shorthand that React necessarily diffs an entire page on every update
was too broad. See [Render and Commit](https://react.dev/learn/render-and-commit).

React associates state with component position/type and keys in the render tree.
Lambda's conceptual key instead starts with source item and template identity,
and deliberately retains state across template switches. The difference matters
for repeated application, reordering, and restoration; see
[Preserving and Resetting State](https://react.dev/learn/preserving-and-resetting-state).

| Aspect | React influence | Lambda choice |
|---|---|---|
| Component body | Functional UI calculation | `fn`-context template transformation |
| Local state | Per-component memory | Declared state associated with source/template |
| Events | Handler-triggered state changes | Procedural `on` blocks |
| Child selection | Usually explicit component references | Pattern dispatch, with named selection when needed |
| Child-to-parent request | Callback/lifted-state pattern | Nearest enclosing custom handler via `emit` |
| Update discovery | Render and reconcile the affected component output | Dirty-instance regeneration with direct ownership/location replacement |
| Model discipline | Immutable-update patterns | Explicit `view` versus `edit` authority; ordinary value semantics still apply |
| Memoization | Reuse when inputs permit | Skip unaffected instances; no-op output comparison; further memoization proposed |

Both systems treat presentation as a function of inputs/state and avoid requiring
application-level imperative DOM patching. Lambda's distinguishing choice is
pattern-directed presentation combined with a language-checked functional/effect
boundary and explicit editing authority.

### 16.3 Elm

Elm's architecture separates model, update, and view. Lambda's templates adopt
the same functional-view motivation but use procedural event handlers instead of
requiring every update to be expressed as one pure reducer. **S12.1.3** explicitly
identifies this architecture as enforced by the effect bit. See
[The Elm Architecture](https://guide.elm-lang.org/architecture/).

The analogy does not automatically supply Elm's message types, command model,
subscriptions, or debugger semantics. Lambda's async handler, replay, and resource
lifecycle contracts need their own decisions.

### 16.4 Persistent structures and Immer

The first document's history design borrows structural sharing: an edit changes
the modified branch and retains the rest of the previous value. Immer similarly
uses mutation-like update notation to produce immutable next state with sharing;
see [Introduction to Immer](https://immerjs.github.io/immer/).

That influence explains the original desire for concise editor assignments and
cheap undo roots. It does not make the ordinary live reactive edit path immutable.
The state/result identity consequences of changing a source version are part of
the unresolved integration, not a detail that can be omitted from the contract.

### 16.5 SwiftUI and read-tracked reactivity

The later [feature review](<Lambda_Features (idea).md>) identifies SwiftUI as
another useful comparison: computed view descriptions, framework-owned state,
identity-dependent lifetime, and dependency invalidation. Apple's
[Demystify SwiftUI](https://developer.apple.com/videos/play/wwdc2021/10022/)
describes identity and state lifetime; its
[model-data guide](https://developer.apple.com/documentation/SwiftUI/Managing-model-data-in-your-app)
describes view dependencies formed by reading observable model properties.
Lambda's source/template identity is a different choice whose advantages depend
on solving repeated application and stable source addressing, not a proof that
identity is automatic or universally better.

The review also points to Solid, Vue, and Svelte as references for finer read
dependency tracking. Solid's
[fine-grained reactivity guide](https://docs.solidjs.com/advanced-concepts/fine-grained-reactivity)
explains targeted updates through tracked dependencies. These references motivate
the question of transitive readers: a summary/filter template must be invalidated
when another handler changes the data it reads.

Static analysis of pure template bodies is one proposed Lambda answer. It could
approximate model paths and state read-sets and register dependencies without
requiring a subscription for every runtime read. Dynamic calls and data-dependent
paths require conservative treatment. Runtime read tracking or a hybrid remain
alternatives; purity alone does not establish that write-keyed invalidation is
complete. This is an optimizer/design direction, not an adopted signal primitive.

### 16.6 Binding, observers, and responsibility

Two-way binding motivates writing an edited model field from an interaction,
but Lambda retains explicit authority: a viewer can display and keep local state;
an editor can change the model. The system is not a general signal graph inferred
from every arbitrary read or write. Its primary observation boundary is the
source/template instance, with future finer dependency tracking possible.

The observer choice moves complexity from tree matching into invalidation and
identity maintenance. This is beneficial when writes are already known and
subtrees are independent, but it must cover ancestor readers, multiple presentations,
fragments, and nested owners. Rendering optimizations are valid only after those
semantic obligations are satisfied.

## 17. Superseded design wording and alternatives

The following retains the decisions that changed, so the earlier reasoning is
not lost. Struck wording is historical and is not a current ruling.

| Earlier statement or alternative | Replacement and reason |
|---|---|
| ~~Automatically reconcile by React-style virtual DOM diffing over the result tree.~~ | Observe source/state changes and regenerate affected instances; compare regenerated output only to elide no-ops (§9) |
| ~~Every editor model assignment creates a new immutable document version, and every handler automatically commits it.~~ | Ordinary reactive editing preserves live source identity; explicit history sessions are separate. Persistent integration must migrate identity and invalidation (§6) |
| ~~An unchanged source pointer proves that no model edit happened.~~ | Live model identity can remain stable across a mutation; changes require explicit observation (§6.1) |
| ~~The render relation always has one producing template for one result element.~~ | Nested templates may return the same element and form an ordered owner chain (§7.8; ES27 elaboration) |
| ~~A single root element is always required.~~ | Fragment output is meaningful under the content model; replacement covers its complete parent range (§4.5) |
| ~~The body and handlers implicitly bind both `it` and `~`.~~ | `~` is the matched-model accessor; the extra injected identifier was removed (§3.2) |
| ~~`apply;` is one fused token with its own termination rule.~~ | Bare `apply` is a statement and `;` follows ordinary separator rules (Syntax §7.7; **S16.2**) |
| ~~The `recursive` default automatically re-applies templates throughout freshly produced result trees.~~ | Explicit source-child application is the established recursion mechanism; the broader options remain a proposal (§4.4, §18.2) |
| ~~An author handler claiming an event stops template bubbling and suppresses the native default.~~ | Author handlers participate along the shared path; only default prevention suppresses the UA tier (ES23v2/ES29) |
| ~~Lambda and JS need separately built event maps and mirrored cancellation flags.~~ | One event record supplies both realms and all participants (ES24) |
| ~~Synthetic DOM dispatch has a separate activation implementation.~~ | Trusted and synthetic events use the same author/UA model (ES25/ES26) |
| ~~Continuous pointer/scroll events never reach author templates.~~ | Declaring author templates opt into them; undeclared events avoid template work (ES23v2) |
| ~~All handler side effects are confined to state/model assignments.~~ | Handlers are `pn` context and can perform other authorized procedural effects (**S12.1.1v2**) |
| ~~Retaining a runtime makes arbitrary persistent pointers from presentation into its heap safe; copying is unnecessary.~~ | Borrow to read; document ownership or registered roots to keep (**D4.5.2**) |
| ~~Source parenthood can be identified uniquely from allocation ownership.~~ | Shared source versions require document/version/path context (§6.4) |
| ~~Every reactive event requires full DOM, CSS, layout, and paint reconstruction.~~ | Local replacement, invalidation, no-op elision, and safe escalation are separate stages (§10) |
| ~~Subtree layout must be implemented by detaching and recreating an isolated formatting context.~~ | Dirty-path layout can reuse valid context and unaffected siblings; arbitrary isolation remains conditional (§10.3) |
| ~~A glyph cache must retain vector shape handles.~~ | Reuse the rendering data appropriate to the renderer and configuration (§10.5) |
| ~~Selective clearing alone means only dirty content is rendered.~~ | Clearing, scene traversal, and paint culling are independent; overflow/effects require correct bounds (§10.4) |
| ~~A fixed overflow margin makes paint culling generally safe.~~ | Use reliable paint extents or broaden repaint (§10.4) |
| ~~Textarea selection needs a separate state subsystem.~~ | Reuse one canonical selection and appropriate projections (§13.4) |
| ~~All text offsets are interchangeable character indices.~~ | DOM UTF-16, Lambda code points, and storage bytes have explicit conversion boundaries (§13.2) |
| ~~Add `select_delete` as the required deletion protocol.~~ | Selection-aware existing editing events and the common intention/default/input sequence (§13.5) |
| ~~Focus every newly inserted input as a convenience.~~ | Focus is requested explicitly; insertion alone does not authorize focus theft (§13.1) |
| ~~Rich text/editing policy remains duplicated in native code and application handlers.~~ | Shared behavior policy with generic engine mechanisms; **D7.2.5** governs contenteditable (§11) |
| ~~A full translucent dragged-element copy is required for drag support.~~ | An indicator and target feedback suffice; richer ghost presentation is optional (§14.3) |

## 18. Outstanding issues and proposals

This final section records unresolved contracts and conformance questions, not an
implementation plan. Existing issue IDs are retained where available; no new
ledger series is introduced. Concrete defect tracking remains in the central
[Lambda Issue Ledger](Lambda_Issue_Ledger.md) and the linked area records.

### 18.1 Existing design contracts needing completion or verification

1. **Complete pattern semantics.** Structural map patterns must inspect names
   and admitted types, not just field count. Typed element attributes must be
   distinguished from presence predicates. Union/named-type coverage, non-text
   pinned values, and narrower-type precedence need a complete contract and
   conformance check. Current approximations do not revise the intended pattern
   language (§4.1–§4.2). The first design gives explicit named invocation highest
   priority, while later public descriptions also rank named declarations first;
   whether declaration naming alone changes pattern dispatch needs an explicit
   resolution rather than an undocumented precedence choice.
2. **Source identity across versions and repeated presentation.** Define how
   replacing/reordering source data preserves or resets template state, how one
   source item can be presented more than once, and how owner chains compose with
   fragment results. Stable anonymous-template identity must survive reload and
   restoration. This is also **RS7**, **RSO1/RSO2/RSO10** in the
   [state-management record](radiant/Radiant_Design_State_Management.md).
3. **Persistent source-model editing and undo.** Reconcile live identity-preserving
   edits with structurally shared version roots, state migration, selection
   snapshots, redo-tail semantics, retention limits, and dependent-reader
   invalidation. Automatic per-handler immutable history remains unestablished;
   it cannot be inferred from `edit`, `undo`, or commit vocabulary alone (§6).
4. **Complete invalidation and regeneration order.** Ancestor/aggregate readers,
   several presentations of one model, nested owner chains, and parent/child dirty
   overlap must all settle correctly. Dependency ordering must prevent redundant
   or stale child regeneration. Large update batches must publish every changed
   result rather than silently retaining only a bounded subset (§9). Static
   read-set analysis, runtime read tracking, and hybrid invalidation are the
   alternatives recorded in §16.5.
5. **Patch coverage and selector/layout dependencies.** Root replacement,
   fragment-range changes, cross-subtree selectors, inherited style changes,
   complex formatting contexts, shifted siblings, overflow, and positioned
   descendants need a complete local-patch/escalation contract. Current fast
   paths must preserve the full-rebuild result (§10).
6. **State retention and cleanup.** Define when deleted-source state can be
   collected while retained versions or reversible template switches still exist.
   LRU/max-entry policies must state whether eviction is observable and how it
   interacts with unmount, restore, and model identity (§5.2).
7. **Lifecycle and error boundaries.** Author-template `init`, `mount`, `update`,
   `unmount`, and `error` need ordering, exactly-once/reattachment, nested-failure,
   handler-error, and cleanup contracts. Initialization of DOM behavior is not a
   substitute. Suspend/resume syntax and ordering remain **RSO3** (§8.2).
8. **Event-field units and coverage.** Preserve useful target/key/text/selection
   projections on the shared record and specify their units, availability, and
   before/after-edit meaning. General handler capture remains **ESO64** in
   [DOM Dispatch](Lambda_Design_DOM_Dispatch.md). The
   [default-action record](Lambda_Design_DOM_Default.md) owns event coverage (§7).
9. **Text and drag continuity across regeneration.** Focus, canonical value,
   Unicode selection, wrapping, scroll, composition, clipboard replacement, and
   history must survive presentation replacement without competing writers.
   Drag cancellation by Escape or source/target detachment must leave no stale
   ownership; richer feedback and throttling need explicit policies (§13–§14).
10. **Whitespace fidelity.** The second record reported loss of a single-space
    content string during DOM reconstruction. The design requires Lambda content
    normalization followed by CSS whitespace treatment, not unconditional removal
    of meaningful spaces (**S2.6.2–S2.6.4**). That historical report has not been
   freshly reproduced here and must not be treated as either a verified current
   defect or a permitted workaround.
11. **Template signature and initialization contracts.** Parameter forwarding,
    return-contract checking, the default element contract versus fragment output,
    and the admissibility/timing of nonliteral state initializers need one coherent
    declaration contract. The syntax's expressiveness is not evidence that all of
    those contracts already operate during regeneration (§3.1, §3.4, §4.5).
12. **Scheduler and task integration.** Place regeneration within the page's
    presentation schedule, define dirty-write coalescing per turn/frame, and ensure
    task-resume results use the same owner/invalidation path. Template-local `emit`
    and task/mailbox messages have different destinations and lifetimes; making a
    template instance a general addressable endpoint remains explicitly deferred.

### 18.2 Original application/dispatch extensions retained for decision

The first proposal's broader application contract was
`apply(target, options?, ...params)`. Beyond the established mode and named
selection options, it proposed:

| Proposed option | Original default | Intended purpose and unresolved point |
|---|---|---|
| `recursive` | `true` | Automatic recursive application; settle source-versus-result traversal and interaction with explicit child application |
| `depth` | Unlimited | Bound recursion; define counting and pass-through behavior at the limit |
| `scope` | Empty string | Filter template namespace/scope; define module/import visibility and collision behavior |
| `on_change` | Null | Notify after editor model changes for persistence/synchronization; define batching, error, and reentrancy semantics |
| Additional parameters | None | Forward declaration parameters through named/recursive application and subsequent regeneration |

The original **Q3** communication question is partially settled by `emit`. The
following channels remain proposals:

- **Rendered `page` access.** A live page value could support element lookup by
  unique HTML-style `id`, page queries using Lambda patterns, and targeted handler
  invocation. Proposed read properties are parent-relative `x`/`y`, width/height,
  viewport visibility, scroll offsets, and computed style. CSS already uses ids;
  a `page` keyword and `page.id.handler(...)` API are not implied by that fact.
  Existing DOM APIs must be considered before adding overlapping syntax. The
  original naming alternatives were `view` (already the template keyword),
  `screen` (too platform-specific), `dom` (too HTML-specific), and `ui` (too generic).
  `page` was preferred for the document-oriented rendered screen/page.
- **Shared cross-template state.** Explicit shared keys could supplement ancestor
  notifications, but ownership, write authority, invalidation, and session
  isolation must be defined without introducing module-global mutable state
  (**S9.1.7**).
- **Programmatic dispatch outside a handler.** Define the destination instance,
  mounted-session requirement, cancellation protocol, and lifetime before
  extending `emit` or adding a targeted invocation API.

Further XSLT-style controls remain open:

- **Explicit numeric priority.** Higher priority would override structural
  specificity; decide how it composes with named invocation and tie-breaking.
- **Named transformation modes.** A print/screen or other named mode could restrict
  eligible templates. Decide whether unannotated templates match all modes or
  only an unnamed default, and keep this dimension distinct from view/edit
  authority.

### 18.3 Original language, presentation, and tooling proposals

- **Async handlers (original Q4).** Alternatives were asynchronous `on` syntax,
  separate effect blocks, or callbacks that publish state updates. The unresolved
  contract includes cancellation, owner lifetime, stale responses, update ordering,
  and the existing `fn`/`pn` boundary; timers/tasks do not settle these by themselves.
- **Template-specific CSS (original Q5).** Ordinary external/embedded styles are
  available. A template `style` block, scoped CSS, or CSS-in-Lambda syntax remains
  a separate design choice; selector invalidation must work whichever is chosen.
- **Memoization.** Reuse pure output when model/state dependencies are unchanged.
  Decide cache identity, invalidation, parameter/environment dependencies, and
  retention. This is stronger than skipping an instance that was never dirty.
- **Derived/computed state.** The proposed `computed` clause would cache a value
  derived from model and state and invalidate it automatically. It needs dependency,
  evaluation-order, cycle, and effect rules rather than a second mutable source
  of truth.
- **Guarded handlers.** A condition on an `on` declaration could filter events
  before the procedural body. Specify its purity, context, cancellation, and
  ordering relative to other handlers.
- **Animation/transition integration.** Proposed transition/animate attributes
  interpolate changed CSS properties. Coordinate them with no-op detection,
  geometry/damage invalidation, deletion, and the existing animation design.
- **Keyed lists.** A presentation key could preserve/reorder existing nodes
  independently of position. Define duplicate-key behavior, source identity,
  state retention, and interaction with fragment and owner-chain results before
  treating an arbitrary `key` attribute as framework semantics.
- **Fragments and slots.** Fragments already follow the content model and require
  correct range replacement; named slots or richer child forwarding still need a
  contract. They should not force presentation-only wrappers.
- **Model-change notification.** A callback/event should identify the committed
  model change for save/sync and dependent views. Define whether one event with
  several edits yields one notification and what happens when the callback edits
  again or fails.
- **Hot reload.** Retain state while replacing definitions; specify template
  identity/versioning, pattern rematching, handler replacement, and migration of
  removed/renamed state declarations.
- **Inspection tools.** Template selection/ownership inspection, state viewing,
  and an event log are design goals. Replay/time-travel and hibernation build on
  the separate **RS** decisions and cannot be presented as completed merely
  because the base event loop exists.
- **Static/SSR interoperability.** Static output is a natural use of pure bodies.
  Hydration, transferring state to a live client, and environment-dependent
  regeneration require additional identity/capability decisions (§12.3).
- **Persistence and determinism extensions.** Source-path addressing, serialization
  constraints, state-schema migration, suspend/resume, timer rules, and durable
  event retention remain governed by **RSO1–RSO10**. The proposed extra purity
  lattice must be reconciled explicitly with current **S12.1.1v2–S12.1.3** before
  it changes template or handler semantics.
