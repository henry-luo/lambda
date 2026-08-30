# Lambda Script vs Swift — Procedural Features

*2026-08-30. Scope: Lambda's procedural layer — `pn`, `var`, mutation, errors, resources, concurrency — compared against Swift (through Swift 6.x). Rulings cited by formal-spec ID per `doc/Doc_Convention.md`.*

Lambda and Swift turn out to be close relatives on the mutability axis and near-opposites on everything around it. Swift is an imperative-reference language that *converges on* value semantics by adding checked annotations — exclusivity, `Sendable`, strict concurrency. Lambda is a pure-functional language whose procedural island (`pn`) gets the same theorems *by construction*, by deleting references, globals, and mutable capture outright.

## The deep kinship: mutable value semantics

Swift is the one mainstream language built on *mutable value semantics* — and Lambda's S9 is the same model taken further. A whole layer of Lambda's procedural surface has a nearly one-to-one Swift counterpart:

| Lambda | Swift | Shared idea |
|---|---|---|
| `let` / `var` (S9.1.1, S12.2.1) | `let` / `var` | same keywords, same finality rule |
| containers copy observably, COW underneath (S9.1.2) | `Array`/`Dictionary`/`String` COW structs | value semantics, sharing unobservable |
| `var` params = inout borrow, exclusivity-checked (S9.1.3) | `inout` + Law of Exclusivity | writer-vs-writer exclusivity |
| `var` params invariant (S9.2.1) | `inout` invariant | no covariant-write hole |
| `pn` method needs `var` receiver | `mutating func` needs `var` | no mutation through a `let` |
| error exit cancels-then-joins children (S13.3.1) | `TaskGroup` semantics | structured concurrency |
| cancellation cooperative, observed at park points (S13.3.2) | `Task` cancellation at suspension points | no preemptive kill |
| must engage `T^E` at the call site (S7.6, S11.4.4) | `try` marking is mandatory | compiler-enforced error acknowledgment |

Even the escape valve is the same: `doc/Lambda_Procedural.md` prescribes the handle-store (arena + index) idiom for the two-owner problem, and explicitly notes that's how the same programs are written "in Swift with structs." A `pn` mutating a `var` parameter is essentially a Swift `mutating` function over structs.

## Divergence 1: the purity bit

Lambda's load-bearing wall is S12.1.1 — `fn`/`pn` is a declared, one-bit effect system, and `fn` cannot call `pn`. Swift has nothing like it: its "effect specifiers" are `throws` and `async`, but there is no purity annotation, so no Swift function is ever *known* effect-free. Everything Lambda hangs off that bit has no Swift counterpart:

- `fn` stages auto-parallelize in stream pipelines (S13.4.2);
- builtin reductions are pairwise-by-spec, bit-identical under any parallelism (S13.4.1);
- reactive templates get Elm-architecture guarantees from the type system (S12.1.3).

Even statements are gated by the bit — `var`, assignment, `while`, `break`/`continue`, `return` are `pn`-only (S12.1.2), whereas Swift's imperative control flow is available everywhere.

The cost runs the other way: Swift has error-effect polymorphism (`rethrows`, and generic typed throws where `E = Never` collapses to non-throwing), while Lambda admits effect polymorphism only for `call` (S12.1.4). A higher-order `fn` in Lambda can never accept an effectful callback.

## Divergence 2: references exist in Swift, not in Lambda

Swift is a hybrid: structs are values, but classes, closures, and captured `var`s give you genuine aliasing whenever you want it. Lambda has no reference cells at all (S9.1.5) — cycles are unconstructible, `==` is total, and there is no ARC, `weak`/`unowned`, or retain-cycle debugging because the problem class doesn't exist.

Closures are the sharpest contrast: Swift closures capture mutable locals *by reference* (shared mutable state between closure and scope), while Lambda captures are immutable snapshots and mutation through a capture is a compile error (S9.1.4). Lambda's rule is essentially Swift's `@Sendable`-closure restriction applied unconditionally, everywhere.

Same story for globals: Swift permits global mutable state and Swift 6 retrofits safety onto it via global-actor isolation; Lambda simply has none (S9.1.7) — every mutable root is owned by one `pn` activation, view instance, or object instance.

What Swift buys with references: when a program genuinely wants shared identity (an object graph, an observer list), a class expresses it directly. Lambda makes identity data — the handle store — which costs an indirection and a write-back discipline, and pays out in `==`-comparability, printability, and serializability of all state at any moment.

## Divergence 3: colorless concurrency vs. colored + actors

Swift chose visible suspension: `async` colors signatures virally, `await` marks every potential interleaving point, and actors serialize access to shared mutable state. Lambda ruled out `async`/`await` entirely (S13.1.1v2) — calls are colorless, may-suspend is inferred and never observable (S13.1.2v2), and the only surface is the builtin `start` plus `wait`/`send`/`receive`/`select`/`cancel`.

Both designs are internally coherent, and the difference traces back to divergence 2. Swift *must* mark `await` because interleaving is observable — shared state and actor reentrancy make suspension points semantically meaningful. Lambda may hide suspension because the capture rule (S13.1.4: a started task cannot capture a `var` by reference) plus valueful messaging make thread count semantically unobservable. Swift's entire `Sendable`/region-isolation apparatus exists to police a share-nothing boundary in a reference language; Lambda gets the boundary structurally, since every value is trivially "sendable."

The actor models differ too: a Swift actor is a shared-memory object with an implicit, unbounded queue of awaited method calls; a Lambda task is an Erlang-style explicit actor — one bounded FIFO mailbox, `receive`/`select`, dispatch via `match` (S13.2.1). Lambda additionally makes backpressure a typed value — `send` never blocks and returns `ok^E` with `'mailbox_full'` as an error (S13.2.2) — where Swift actor mailboxes are unbounded with no backpressure story.

Structured concurrency is where the two agree most: Lambda's block-scoped handles (normal exit joins, error exit cancels-then-joins, escape only by typed return — S13.3.1) mirror Swift's `TaskGroup` discipline. The difference is that Swift keeps `Task {}` as an unstructured escape hatch in wide use, while Lambda's only escape is the typed-return one.

## Errors and resources: same goals, different spellings

Error handling is closer than the syntax suggests:

| Lambda | Swift |
|---|---|
| `T^E` declared channel | `throws(E)` typed throws (Swift 6) |
| postfix `e^` propagation | `try` propagation |
| `e ^ { … }` handler | `do { try … } catch { … }` |
| `e or default` (errors falsy) | `try? … ?? default` |
| `T \| error` soft channel (S11.4.2) | `Result<T, E>` |

Lambda goes further in three ways: error unions (`T^E1 | E2`) come free where Swift's typed throws takes exactly one type; the soft channel is built into the type system rather than a library type; and there is deliberately no `try!` — a handler must produce a value or diverge (S7.6), so there is no trap-on-error unwrap.

For cleanup, Swift gives you manual `defer` plus ARC `deinit` (and `~Copyable` RAII); Lambda rejected `defer` outright (S12.4.3) — `open()` resources auto-close at block exit, ownership escapes only via a return visible in the declared return type, and errdefer behavior falls out for free (S12.4.2). Lambda also gives close-errors a channel, which Swift's non-throwing `deinit` structurally cannot.

## Smaller contrasts

- **Mutable plain parameters.** `pn` parameters are reassignable (handy for counters); Swift removed `var` params in Swift 3. Safe in Lambda because plain params snapshot, so mutation stays local (S9.1.3).
- **Runtime type widening.** An unannotated `var` may change runtime type on reassignment (S12.2.1) — scripting heritage with no Swift analog; Swift infers a type once and holds it.
- **No arity overloading.** Two definitions sharing a name is an error (S12.3.6); Swift overloads on arity, types, and labels pervasively.
- **Property machinery.** Swift's computed properties, observers (`willSet`/`didSet`), and property wrappers have no Lambda counterpart: fields are plain data, computed things are `fn` methods.
- **Entry points.** `pn main()` + `lambda run` vs `@main` / top-level code in `main.swift`; both languages also run script-style top level.

## Implementation-status caveat

A few S9 clauses are spec-final but still landing in the implementation — insertion capture (S9.3.1) and plain-param snapshotting (within S9.1.3) are starred rulings tracked under LR12-9 — so today's binary is slightly looser than the spec described above. Write against the spec rules and code keeps working as the checks arrive.

## Summary

Swift trusts the programmer with references and polices them — exclusivity checks, `Sendable`, strict-concurrency mode — while Lambda removes them and gets the same guarantees by construction: `pn` code is *locally imperative but observably functional* (S9.1.1). Lambda's procedural layer is roughly the "structs-only Swift" discipline made mandatory, fused with a purity bit Swift never had, at the price of Swift's escape hatches: classes when you genuinely want shared identity, `rethrows`-style effect polymorphism, and imperative control flow anywhere.
