# Lambda Effects Review and Binding Proposal

**Status.** Proposal for review, 2026-11-19. The proposed binders let a
public signature express how a callback's effect bound contributes to the
enclosing call.

**Formal linkage.** S11.1.5v4 defines callable types, S11.4.8v2 defines type
binders, and S12.1.1v5, S12.1.4v7, S12.1.6v3 and D7.1.1v4 define call
admission and effect bounds. S12.1.3v6 and S16.7.7v2 govern render and script
entry. SO44 covers how much of a callable's signature a binder can see. SO52
covers effect mappings, cache policy and future annotations.
[Formal semantics](../doc/Lambda_Formal_Semantics.md) ·
[Formal design](../doc/Lambda_Formal_Design.md)

## 1. Lambda's current model

Lambda checks effects when a callable is invoked. `fn` is the pure form.
A `pn` can publish a concrete upper bound such as `@clock` or
`@[random, clock, print]`. A bare `pn` permits arbitrary effects. The effect
clause follows the complete result and error contract. A narrower bound
fits a wider caller allowance. A callback's bound is also checked when it
crosses a typed callable boundary. Known mismatches are checked statically.
Dynamic admission checks the selected callable before dispatch (S11.1.5v4,
S12.1.1v5, S12.1.6v3, D7.1.1v4).
[Procedural design](Lambda_Design_Procedural.md#11-checked-procedure-effects-and-generative-functions)

`function` uses functional control and result collection. Its call combines
the body's own selected generative effects with the bounds contributed by
its `function`-typed callback slots. For example, `function apply_all(f:
function, xs) => for (x in xs) f(x)` has a pure instantiation with a pure
callback. With a clock-reading callback, the call requires `clock`. The
public signature can express a concrete bound on `f`, and the checker can
propagate the callback's effects. An effect binder would let the signature
express that propagation explicitly while preserving the relationship to
the callback's return type (S12.1.4v7, S12.1.6v3).

A generative `view` or `edit` body runs each reached `random`, `clock` or
`print` operation on each evaluation. State and event handling govern when
the body runs. Those operations schedule no render. The selected template
can also use cached
HTTP(S) `input()` through the render entry, subject to host policy. A
different query value selects a different cache key. Explicit `fetch()`
and database series allocation require procedural context (S7.4.5v4,
S12.1.3v6, S12.1.4v7, S16.7.7v2, D11.1.2).
[Reactive template design](Lambda_Design_Reactive_UI.md#21-functional-presentation)

## 2. What Lambda can learn from other systems

| System | How it describes effects | What Lambda could adopt |
|---|---|---|
| **Effect for TypeScript** | `Effect<A, E, R>` describes a deferred computation with success type `A`, expected error type `E` and required services `R`. `R = never` means execution needs no explicitly supplied services. Operations can still use a default service such as `Clock`. | Scoped substitution for built-in operations. A test can provide `TestClock` and drive time predictably. Lambda could use the same idea for a scripted clock, a random source or a print sink at its ordinary call sites. The public `@clock` bound would still describe the operation the call may perform. [The Effect type](https://effect.website/docs/v4/getting-started/the-effect-type) · [Default services](https://effect.website/docs/v4/requirements-management/default-services) · [TestClock](https://effect.website/docs/v4/testing/testclock) |
| **Koka** | Function types include inferred effect rows. Its `map` uses the same effect variable `e` for the callback and for the call to `map`. A function can also extend an open effect row with its own effects. | A named dependency between the effects of a callback and those of its caller. Lambda can express that relationship using its existing finite effect sets, with binding and union as the initial operations. [Koka language book, polymorphic effects](https://koka-lang.github.io/koka/doc/book.html) |
| **Unison** | An arrow declares the abilities an invocation requires. An ability variable can occur in both a callback type and the enclosing function's requirements. A handler can supply a user-defined ability while forwarding the remaining abilities. | Explicit propagation through a public signature now, with user-defined operation groups and lexical handlers as a later design. A handler that satisfies one ability while forwarding the rest illustrates how a future Lambda handler might affect a bound. [Abilities and handlers](https://www.unison-lang.org/docs/language-reference/abilities-and-ability-handlers/) · [Ability variables in handler signatures](https://www.unison-lang.org/docs/fundamentals/abilities/writing-abilities/) |
| **Haskell** | `IO a` represents an action that produces an `a`. Ordinary code can compose and sequence these actions as values. The plain `IO` type groups many kinds of external interaction into one boundary. | The explicit boundary between constructing an action and performing it could inform a future deferred-work API. For ordinary Lambda calls and templates, the present direct call model gives a straightforward rule for when a sample or print occurs (S12.1.3v6). [Haskell 2010 Report, Basic Input/Output](https://www.haskell.org/onlinereport/haskell2010/haskellch7.html) |

These signatures describe a helper that reads a clock and then runs a
callback. Each assumes a defined clock effect or ability. `e` and `g` are
bound by the callback's contract.

```text
Koka     sample-and-run : (action : () -> e a) -> <clock | e> a
Unison   sampleAndRun   : '{g} a ->{Clock, g} a
```

In Unison, `'{g} a` is the delayed computation passed to the helper.
[Koka language book](https://koka-lang.github.io/koka/doc/book.html) ·
[Unison ability variables](https://www.unison-lang.org/docs/fundamentals/abilities/writing-abilities/)

An effect bound tells the caller which operations may happen. Host policy
decides which concrete resources those operations can access. A future
scoped provider would decide how a permitted operation behaves during a
test or replay. The first two
already follow S12.1.6v3 and D11.1.2. Provider scope and replay semantics need
their own design. An initial provider could substitute a clock or random
source while retaining the caller's existing effect obligation.

## 3. Proposed effect binding

### 3.1 Bind in the callback parameter

Extend the `as` pattern from S11.4.8v2 to capture a callback's success type
and effect bound. The following examples are proposed signature syntax.

```text
function F(callback: function () -> (number as T) @as E) T @E

function sample_and_run(callback: function () -> (number as T) @as E)
    T @[clock, *E]

function run_local(callback: function () -> (number as T) @[clock, print] as E)
    T @E
```

| Form | Proposed meaning |
|---|---|
| `(B as T)` in a callback result | Bind `T` from the callback's success-type contract under bound `B`. For example, `(number as T)` binds `int` for a callback whose success type is `int`. |
| `@as E` in a callback parameter | Bind `E` from the effect bound of calling the supplied callback. |
| `@[clock, print] as E` in a callback parameter | Admit callbacks whose bound fits the listed ceiling, then bind `E` to the supplied callback's bound. A clock-only callback therefore contributes only `clock`. |
| `@E` | Refer to the established effect bound. On the outer signature it publishes that bound for the call. In a later callback contract it can serve as a ceiling. |
| `@[clock, *E]` | Take the union of the named effects and the bound `E`. `*` follows Lambda's spread syntax (S16.8.6v4). Order and duplicates have no significance in an effect set. |

The original `@e as E` suggestion fits the ceiling form when `e` names a
concrete effect or supplies a concrete list. `@as E` is the shorter form
for a binding with no explicit ceiling, mirroring the existing `as T`
parameter shorthand (S11.4.8v2).

Use `function` for both the helper and the callback type when the API should
work with pure and procedural callbacks. Use `pn` for the callback when the
API specifically requires a procedural value. A call through an
effect-polymorphic `function` can still be admitted in `fn` context if the
instantiated call is pure. A plain `fn` keeps its purity guarantee
(S11.1.5v4, S12.1.1v5, S12.1.4v7).
[Lambda spread syntax](../doc/Lambda_Data.md#spread-operator)

### 3.2 Bind and check before execution

1. **Use the callback's contract.** The supplied callable contributes its
   known success type and published effect bound. Bind from static contracts
   when available and from retained callable metadata at a dynamic boundary.
   Deriving `T` from a callback's success contract extends the current
   subvalue-based `as` rule and needs a decision under SO44. Success-type
   admission must respect the callback contract. It cannot obtain a tighter
   `T` by running the callback. Existing `T | Err` and `T^` continue to
   describe errors separately from effects (S7.4.2v4, S11.4.8v2, S12.1.6v3).
2. **Keep three cases distinct.** A pure callback contributes an empty
   bound. A callback with a concrete bound contributes that bound. A bare
   `pn` or a callable whose effects cannot be established contributes an
   unrestricted bound. A concrete ceiling refuses an unrestricted bound.
   Union with an unrestricted bound stays unrestricted. An empty inferred
   `E` needs no explicit empty-list syntax (S12.1.6v3).
3. **Use the bound for the invocation the parameter describes.** Captured
   callbacks and defaults can contribute effects even when the visible
   callable value looks pure. Admission must take those contributions into
   account for the promised invocation. An opaque callback requires the
   conservative rule above. Known and dynamic calls must enforce the same
   bound across closures, module exports and all execution tiers
   (S12.1.4v7, S12.1.6v3, D7.1.1v4).
4. **Check the body for every admissible `E`.** `F` may invoke the supplied
   callback under `@E`. Its body can also pass the callback to a helper
   whose contract preserves that obligation. An independent clock read
   requires `@[clock, *E]` because `E` may be empty. A ceiling containing
   `clock` only limits the possible bindings. It does not grant a clock
   read in all instantiations. The existing generative allowance still
   governs direct operations in the `function` body (S12.1.4v7).

For `F`, a pure `int` callback yields success type `int` and an empty effect
bound. A `float @print` callback yields `float` and the bound `{print}`.
`sample_and_run` with that same callback yields `float` and
`{clock, print}`. A clock-only caller admits `sample_and_run` with the pure
callback and refuses it with the printing callback. A bare procedural
callback fails `run_local`'s ceiling check. These cases exercise the
relationship the signature adds to S12.1.4v7 and S12.1.6v3.

### 3.3 Scope for a first revision

Bind effects at callback parameter sites and refer to them in later
parameter contracts and the enclosing call's effect clause. `@as E` is an
explicit binding site. `@E` always refers to an established name. A first
revision can require one binding site per name and express independent
callbacks with separate names, combined by spread. This keeps the usual
unannotated `function` form available for local combinators and gives public
helpers a way to publish a precise relationship (S11.4.8v2, S12.1.4v7).

Before ratifying the syntax, settle the rules for an erased callback
success type, the scope of binders in nested callable results, and how an
instantiated bound is retained when a closure escapes (SO44, S12.1.4v7).
The first revision can model effects as bounds used by the checker. SO52
tracks additional operation labels and future annotations. First-class
effect values and handler discharge each need a concrete use case and a
separate design. Scoped providers for testing can be designed independently
of effect-binding syntax.
