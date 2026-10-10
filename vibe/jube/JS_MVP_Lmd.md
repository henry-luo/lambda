# JS MVP Lmd — JavaScript MIR on the untyped Lambda substrate

**Date:** 2026-10-09

**Status:** scalar/dense-array/function MVP, integer tuning, and the map/plain-object
phase implemented. Ordinary arrays and core strings are implemented (§18);
the latest tuning measurements and validation status are recorded in §28.
The numeric-library phase is implemented in source (§15); its full feature
edge matrix remains pending. §§16–17 record the subsequent tuning.
Basic classes and single inheritance are implemented in source (§21);
class-phase validation and performance evidence are tracked separately there.
Closures, callbacks and array growth are implemented and validated in §22.
Class property access and constructor allocation are tuned in §23.
The next benchmark coverage phase is planned in §29.

**Performance history:** [MVP_Result3–8](../../test/benchmark/js_mvp_lmd/README.md)
retains one representative comparison per major tuning phase.

**Destination:** `lambda/js/mvp-lmd/`

**Scope:** JS execution on Lambda's values, containers, memory ownership, and
MIR infrastructure. This document records design and the latest measurement
round; implementation narratives and earlier measurement rounds are omitted.

## 1. Objective and fixed constraints

Execute the admitted JS subset efficiently on untyped Lambda's substrate.
The four governing goals remain fixed (USER, 2026-10-07):

1. **Keep emitted MIR minimal.** Use retained analysis to avoid unnecessary
   checks, conversions, roots, wrappers, and function bodies.
2. **Keep new helpers and helper code minimal.** Reuse audited Lambda/lib
   primitives; prefer existing Lambda functions first. For a small semantic
   difference, consider an explicit option on the existing function while
   preserving its default Lambda behavior. Count internal utilities, adapters,
   and shared additions too; notify the user before adding any new helper.
3. **Implement exactly the admitted scope.** Section 3 describes the shipped
   subset; §10 records the object/Map extension. Excluded capabilities receive
   diagnostics, without reserved machinery for later features.
4. **Keep the MVP as efficient as possible.** Preserve native computations and
   direct storage/calls where proven; measure release performance with fixed
   sources and correctness oracles.

These goals apply together. Correct JS results, precise ownership, and the
execution boundary take precedence over reducing a single size metric.
Retain added machinery only when its semantic need or measured benefit
justifies its MIR, helper-code, allocation, and execution cost.

Keep the existing JS parser, AST, binding, validation, and applicable analysis.
Use new MVP MIR lowering over Lambda `Item`, scalar lanes, containers, roots,
and the shared emitter. **Call no existing JS runtime helper, directly or
transitively**, including during collection, error handling, and teardown.
Do not fall back into full LambdaJS or the retired private-value MVP.
Present each new helper and its dependency closure for review before coding.

The [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md) govern this design:

| Concern | Authority |
|---|---|
| Hosted JS retains ECMAScript behavior | **S1.11**, **D1.1** |
| Host values and shared physical substrate | **D1.2v2–D1.3v3** |
| Integer subtype and numeric FFI | **D2.2.5**, **S4.9.1** |
| Representation is distinct from conversion | **D2.4.1–D2.4.3** |
| Nominal objects and shared map layouts | **D2.6.6v3**, **D2.6.9v3**, **D3.4.1–D3.4.7** |
| Name identity | **S8.2.2v5**, **D3.4.4v4**, **D4.6.1v4–D4.6.2v2** |
| Returned failures and precise ownership | **D1.4v4**, **D1.5v2**, **D5.2–D5.3**, **D8.4.3v2** |
| Retained analysis and immutable specialization | **D8.2.1–D8.2.6**, **D8.3.1v2**, **D8.4.1v2** |

## 2. Shared substrate and semantic ownership

```mermaid
flowchart TD
    source[JavaScript source] --> front[Existing parser, binding, validation, AST analysis]
    front --> plan[MVP admission and representation planning]
    plan --> mir[New MVP MIR lowering]
    shared[Shared Lambda MIR emitter] --> mir
    mir --> host[Lambda Item, containers, heap, roots]
    mir --> leaves[Reviewed MVP semantic leaves]
    leaves --> host
```

Reuse physical allocation, field storage, shape transitions, scalar ownership,
and control-flow emission. JS owns coercion, equality, reference mutation,
property behavior, and completion semantics (**S1.11**, **D1.3v3**).
Class inheritance must align with Lambda's nominal inheritance model
for deep interoperability; §21 records this requirement.

Unknown or changing values use `Item`; proven scalar regions use native
registers. An inferred kind selects a representation without restricting
later JS assignments. Lambda COW, structural equality, list spreading, and
null-on-missing conventions do not implement the corresponding JS operations.

## 3. Implemented admission boundary

This table summarizes the implemented subset. Section 10 defines the detailed
object/Map boundary and its scope decisions.

| Area | Implemented | Excluded from the implemented phase |
|---|---|---|
| Scalars | Undefined, null, Boolean, Number, String; NaN, infinities, signed zero | BigInt, Symbol, boxed primitive objects |
| Operators | Arithmetic, remainder/power, bitwise/shifts, comparisons, loose/strict equality, `typeof`, `void`, short-circuiting, conditional/comma; object `in`/property `delete`; fixed-class `instanceof` (§21) | Object-to-primitive coercion; general prototype/`instanceof` customization |
| Variables | `var`, `let`, `const`; assignments, logical/compound assignments, updates, changing kinds; simple array destructuring assignment (§18; validation in §19) | Binding/nested/rest/default patterns; sloppy implicit global creation |
| Arrays | Dense mixed/nested literals, indexed reads/writes with gap holes, length reads/shrink/growth; constructors, `fill`/`push`/`pop`/`join`/`slice`/`forEach`, deletion holes and own projections (§18, §22) | Hole literals, other named properties, first-class intrinsic methods, descriptors/prototypes |
| Typed arrays | `Int32Array`, `Uint8Array`, `Float64Array`: length construction, zero initialization, indexed reads/writes, `.length`, `.fill(value, start?, end?)` (§15; validation pending) | Buffer/view constructors, other properties/methods, typed-array iteration, detachment/resizing/shared storage |
| Math | Fixed calls to `sqrt`, `sin`, `floor`, `trunc`, `abs`, `min`, `max`, `ceil`, `cos` (§15; validation pending) | First-class Math object/methods, dynamic method names, mutation, other Math members |
| Objects/Map | Data properties, own projections, `new Map()` and fixed collection operations (§10); class-created inheritance links (§21) | `__proto__`, descriptors/accessors, proxies, arbitrary/mutable prototypes, iterable Map construction |
| Control flow | Blocks, conditionals, while/do/for, direct `for-of` with simple pair binding (§10.5), switch/fallthrough, labels, break/continue, return; uncaught `throw` (§21) | `for-in`, general iterators, generators, async, try/catch/finally |
| Functions/classes | Ordinary declarations/expressions, arrows, simple parameters, recursion, function values, indirect calls, program bindings; shared/escaping local captures and per-iteration bindings (§22); named top-level classes, constructors, instance/static methods, single inheritance, class `this`/`new.target` and `super` (§21), arrow lexical class receivers | Default/rest/spread parameters, observable ordinary-function `this`/`arguments`, global-arrow `this`, arrow lexical `super`, class expressions/nested classes, field initializers, private/accessor members, ordinary-function construction |
| Program | One script with execution-owned bindings and retained early-error/strictness rules | Modules, eval, Function constructor, with, DOM/Node APIs, general global object |

Strings preserve Unicode, including lone surrogates, with UTF-16 length and
indexed code-unit reads. §18 adds `charAt`, `charCodeAt`, `repeat`, and
`String.fromCharCode` (validation in §19); other methods remain excluded.
Primitive template interpolation is admitted with §21.

Unsupported syntax is diagnosed throughout the unit, including unexecuted
functions, before execution. Runtime-dependent unsupported operations return
a capability failure distinct from JS semantic errors. Effects already
performed are neither replayed nor rolled back; failed executions are not
correctness or timing successes.

## 4. Semantic contracts

### 4.1 Values and operators

JS observes one Number type across INT and FLOAT representations (§9).
Primitive conversions distinguish null, undefined, Boolean, Number, and
String. For example, `null + 1` is `1`, `undefined + 1` is NaN, and
`"2" + 1` is `"21"`. Native entry guards test kinds without coercing them.

Use JS truthiness and identity equality for reference values. Logical
operators return the selected operand. Loose equality performs only admitted
primitive conversions; an excluded object-conversion protocol causes a
capability failure. Preserve NaN, signed zero, per-operation binary64 rounding,
ToInt32/ToUint32 wrapping, and JS remainder/power behavior (**S1.11**,
**D2.4.3**).

### 4.2 Bindings and references

- `var` is function/program scoped and instantiated before execution;
  redeclaration without an initializer does not reset it.
- `let`/`const` retain block scope and TDZ; const prevents rebinding without
  freezing the referenced container.
- Program bindings are execution-local and shared by functions in that
  execution. Enclosing function/block captures share lexical bindings;
  `let`/`const` loop iterations retain distinct bindings (§22, **D6.2.3v2**).
- `undefined`, `NaN`, and `Infinity` use resolved intrinsic bindings, with
  proper shadowing, restricted global declarations, and strict/sloppy writes.
  An unresolved read and a TDZ read retain their distinct `typeof` behavior.
- Assignment preserves scalar values and reference aliases. Receiver and key
  are evaluated once before the RHS; compound assignment reads first.
  Retain the owner/key through calls, never a potentially stale data address.
- Preserve prefix/postfix snapshots, logical short-circuiting, switch order,
  and the classic-for update target of `continue`.

These are JS binding/reference contracts under **S1.11**, independent of
Lambda's mutable-value rules in **S9.1**.

### 4.3 Ordinary arrays

Use ordinary host `Array` with mixed Item elements. Literals are dense;
§18 adds constructor/deletion holes; §22 adds growth gaps, distinct from stored undefined.
Aliases and cycles are valid. Foreign containers and mutable prototypes
are outside admission.

| Operation | Contract |
|---|---|
| Literal | Evaluate/store left to right, preserving every value and alias. |
| In-bounds read/write | A hole reads as undefined; writing creates/replaces the own element and returns the original RHS. |
| Read at/above length | Undefined for an admitted canonical index. |
| Write at length | Append one element. |
| Write beyond length | Extend length and leave absent properties in the gap. |
| Length assignment | Shrink/retain/grow a valid length; growth creates holes; invalid lengths cause RangeError. |

Numeric `-0` denotes index zero; string `"-0"` does not. `2^32 - 1` is not an
array index. Key conversion runs once. Length conversion, RHS evaluation,
and truncation preserve ECMAScript order and release removed references.
Wide scalar storage is destination-owned across overwrite and growth
(**D5.2–D5.3**). ArrayNum promotion and a private heap are excluded.

### 4.4 Functions and failures

Each source function has one semantic body and chosen entry. All arguments,
including unused extras, evaluate in order; missing arguments are undefined.
Direct-call specialization requires complete stable-target/domain evidence.
Observed function values and indirect calls retain a checked boxed boundary.
Every evaluated function expression has fresh identity.

Proven native scalar calls use the shared companion return/error convention.
Self-tail reentry must preserve argument snapshots and fresh local
initialization. Other recursion retains the ordinary stack checks. All normal
and error exits restore root/number ownership once (**D5.2.1v3**, **D5.3**).

Semantic and capability failures return explicitly through every frame
(**D1.4v4**, **D8.4.3v2**). Pending-error side channels and old JS dispatchers
are excluded; catch/finally remains outside the admitted language.

## 5. MIR design

Retain one AST and semantic-analysis authority. Separate admission, physical
planning, and lowering through the existing pass infrastructure
(**D8.2.4v2–D8.2.6**). Use native arithmetic, branches, direct loads/stores,
and calls where facts suffice. Box at generic boundaries; a guard miss
performs the pending operation once and rejoins the same body.

Use compile-predicted layout guards and shared physical operations on misses.
No mutable inline caches, feedback vectors, speculative body/loop clones,
deoptimizer, interpreter tier, or generic opcode dispatcher is introduced
(**D8.4.1v2**). Keep analysis only where admission or emission consumes it.

## 6. Ownership and execution isolation

Use the host evaluation context, heap, precise roots, scalar homes, source/code
owner, and MIR lifecycle. Execution starts without initializing full LambdaJS.
The execution owner outlives its values and all host inspection of them.

Generated and native calls follow shared rooting/effect contracts. Allocation
may move backing data, so reload it from a rooted owner. Borrowed scalar
payloads become owned before their source can move, mutate, or die. Mutable
slots reuse homes; no pending scalar escapes into a persistent container.

The dependency boundary covers execution, GC tracing/compaction, finalization,
and teardown as well as generated imports. Reuse of a host allocator or
container is valid only when those lifecycle paths also avoid existing JS
runtime callbacks (**D1.3v3**, **D1.5v2**, **D5.3**).

## 7. Helper review policy

The original MVP has ten semantic/runtime helpers, covering failure
creation; string/Number conversion, concatenation, comparison and indexing;
power; array-key classification; array growth/storage; and function identity.
Integer tuning adds none. The object/Map phase adds ten audited entry points;
their contracts and dependencies are recorded in
[JS MVP objects implementation](../impl/JS_MVP_Lmd_Objects.md).

For every proposed addition, review its operation contract, existing reuse
candidates, complete dependencies, GC/scalar ownership, failure completion,
call frequency, and total code cost. A wrapper needs a real ownership, ABI,
or completion adaptation. Promote shared physical work and update its
clients; do not copy a static helper or rename a forbidden JS dependency.

Infallible raw leaves need a transitive no-GC audit. Fallible operations return
explicit completions; other calls are treated as potentially collecting.
Internal utilities and shared additions count toward the helper-code budget.
Section 10.6 records the object/Map phase's completion criteria.

## 8. Acceptance and evaluation

Define admitted fixtures before measuring. Separate passes, semantic failures,
static rejection, and runtime capability failures. Test bindings, numeric
edges, Unicode, aliases/cycles, evaluation order, recursion/indirect calls,
cleanup, and storage ownership normally and under forced GC with freed-memory
poisoning. Exercise rejection paths and lifecycle isolation too.

Shared changes require the Lambda/input baseline and unchanged full-JS
Test262 baseline. The latter checks full-JS nonregression; it does not establish
MVP conformance. Do not alter harnesses, oracles or timeouts to pass.
Emission-budget changes require review of the exact MIR delta (**D8.6.1**).
Any new Lambda script regression also needs its expected result file.

Compare release builds with fixed sources, inputs, and result checks. Report
workload execution separately from startup/compilation, retain failed and
regressed rows, and disclose Lambda-port differences. Record emitted MIR,
branches/conversions/calls, roots/homes, body count, helper-code cost, and
memory alongside timing. Additional tuning must earn its cost; source-line
reductions never come from removing comments or changing formatting.

## 9. Integer-tuning phase

**Accepted and implemented:** USER, 2026-10-07; **D2.2.5**.
This phase extends the implemented scope's internal representations without
adding JS syntax or restricting later assignments.

### 9.1 Runtime subtype and observable behavior

Lambda `int` is an internal subtype of JS Number. Finite integers in
±(2^53 - 1), excluding negative zero, may use canonical INT Items and proven
native integer lanes. Fractions, negative zero, out-of-band Numbers, NaN,
and infinities use the existing FLOAT representation. INT zero means `+0`.

All numeric consumers accept both representations. Equal INT/FLOAT Numbers
compare equal; NaN remains unequal to itself. A FLOAT tag does not imply a
fraction. Narrowing requires a finite, integral, in-band value with the
correct zero sign. No universal retagging pass is required. The host numeric
FFI still exports JS Number as Lambda float (**S4.9.1**).

### 9.2 Arithmetic and widening

Preserve binary64 rounding at each JS operation. Integer addition/subtraction
may retain an in-band result; otherwise use the original operands for double
arithmetic. Multiplication needs complete overflow/range/zero-sign proofs.
Division remains double unless exactness and zero sign are proven; remainder
must preserve negative zero and zero-divisor NaN. Bitwise/shift operations
retain JS 32-bit conversion rules. Power retains its JS Number contract.

A failed guard executes only that operation, without replaying effects.
Lambda saturation and out-of-band exact intermediates are forbidden
(**S1.11**, **S4.1.2**, **D2.4.3**).

### 9.3 Representation proofs

Facts cover all writes, joins, backedges, call edges, implicit returns, and
possible negative zero. A terminating conservative analysis falls back to
Number/Item when it cannot prove a range. Integer regions require complete
bounds; no workload/input-specific assumptions are allowed. In particular,
Collatz and recursive Fibonacci do not acquire integer workers merely because
one measured input stays in range.

### 9.4 Storage and calls

INT survives program slots, arrays, mixed locals, and generic calls. FLOAT
ownership remains correct through overwrites and growth. Native functions
select a carrier valid for every admitted call and return, retaining one body
and the shared completion ABI (**D5.2–D5.3**, **D8.4.2v2**).

### 9.5 Acceptance

Verify actual Item kinds, widening, intermediate rounding, signed zero,
machine overflow, proof invalidation, and alias/call boundaries. Retain
integer guards only with correctness, ownership, MIR-size, and release
measurement evidence. This phase introduced no new runtime helper.

## 10. Map and plain-object phase

**Status:** IMPLEMENTED; user constraints recorded 2026-10-07. The phase extends
§3 while retaining §1's four goals and the execution-isolation boundary.
Section 10.7 records scope decisions; §17 contains the latest results.

**Fixed direction:** reuse Lambda map/object storage and shape transitions;
align string-key identity through shared canonical UTF-8; include map
iteration along the model of Lambda for-expressions/loops; exclude
`__proto__`, advanced descriptors, accessors, and proxies. **Use no VMap for
either plain objects or ES Map collections, including their backing storage.**
Possible VMap use for exotic features belongs to a separate future phase and
introduces no hook or dependency here.

### 10.1 Admission boundary

| Area | Added in this phase | Excluded |
|---|---|---|
| Plain objects | Empty/nested literals; named, shorthand and computed data properties; values of any admitted kind | Getters/setters, descriptor APIs, spread, classes and user constructors |
| Properties | Dot/computed reads, assignments, compound/logical assignments, updates, addition, deletion, `in`, own-presence checks | Object-to-primitive key conversion, Symbols, private-field syntax, prototype mutation, `instanceof` |
| Object projections | `Object.hasOwn`, `Object.keys`, `Object.values`, `Object.entries` on plain objects and ES Map's attribute face | Projections of arrays/functions/primitives; general reflection and arbitrary host/foreign objects |
| ES Map | `new Map()`, `get`, `set`, `has`, `delete`, `clear`, `size`; admitted values as keys/values | Iterable constructors, subclassing, weak collections, callbacks/`forEach` |
| Iteration | Direct `for-of` over Map entries/keys/values and object projections; simple pair binding as described in §10.5 | General iterator objects/protocols, custom iterables, generators, async iteration, general destructuring, `for-in` |

All `__proto__` **property** forms are excluded: literal definitions, dot
access, and computed access after key conversion. A dynamic spelling must
reach the same capability diagnostic. This phase implements no prototype
setter or literal special case for that name.

Ordinary data properties have the default writable/enumerable/configurable
behavior without materialized descriptor records. Missing own properties
remain distinguishable from present properties holding undefined. Object/Map
values are truthy, use identity equality, and preserve aliases through const
bindings, parameters, arrays, and returns (**S1.11**).

Resolve `Map` and `Object` operations through binding and receiver facts;
shadowed identifiers or overwritten members never receive a builtin fast
path merely because their spelling matches. Unsupported builtin escapes or
prototype observations receive capability diagnostics under §10.7.

### 10.2 Ordinary Lambda storage and shape transitions

Plain objects use the ordinary Lambda `Map` carrier, `TypeMap`/`ShapeEntry`
layout, and a nominal object-family record. ES Map uses the same ordinary
object carrier with a distinct nominal family and internal collection state.
Every transition preserves nominal identity (**D2.6.6v3**, **D2.6.9v3**).
No VMap carrier, backend, or virtual property dispatch participates.

Use the shared transition tree for property addition and field retyping
(**D3.4.3v5–D3.4.6**). Shapes reached by the same ordered fields and storage
contracts share metadata within the same semantic family:

```text
empty -> {x: INT} -> {x: INT, y: STRING}
              \-> {x: STRING}
```

- Compatible overwrites retain the layout. An incompatible write selects a
  new shape and repacks as necessary; equal byte widths alone are insufficient.
- Shared fields remain immutable. Mutating one instance never retags a
  sibling's fields. All shape walks respect the owning shape's field boundary.
- Growth/repacking publishes storage and shape consistently, with explicit
  failure and no partially committed object. Transition-budget exhaustion
  selects the shared substrate's private-shape fallback without narrowing JS.
- Deletion initially rebuilds the live shape; re-adding a string property
  gives it a new creation position. Undefined cannot stand in for deletion.
- Fixed literals may use a predicted final shape while preserving initializer
  order, duplicate-key effects, and last-write values. Dynamic layouts use
  transitions. Never publish uninitialized references to the collector.

Named accesses use proven offsets or immutable predicted-shape guards, with
a small semantic property operation on a miss (**D8.4.1v2**). Stores recheck
the shape after RHS calls that may mutate an alias. Retain owner/key/value
roots and scalar ownership through conversion, growth, and repacking
(**D5.3**).

Reuse requires separating neutral storage work from existing full-JS hooks.
The current general map setter can enter JS shape helpers, and the generic
transition tree declines full-JS metadata layouts. The MVP needs an explicit
data-only nominal family and audited shared storage operations; copying the
old JS property kernel or hiding it behind a wrapper is excluded. Preserve
semantic-family identity when sharing a layout (**D1.3v3**, **D3.4.7**).

### 10.3 Shared canonical UTF-8 key identity

**User direction:** Lambda and MVP string keys share canonical UTF-8 identity.
Reuse one common canonicalization/name mechanism for literals, computed keys,
lookup, hashing, transitions, and enumeration. A separate JS interning scheme
would recreate the same-spelling/different-pool problem.

Under **S8.2.2v5**, **D3.4.4v4**, and **D4.6.1v4**, the key is the resolved
namespace and decoded spelling. Equal NameIds may prove equality; differing
or absent IDs still require kind/namespace/length/content comparison. Hashes
route lookups and never prove identity. Empty and embedded-NUL names are valid.

The encoding contract is canonical UTF-8 for Unicode scalar values:
equivalent literal escapes and computed strings produce the same bytes.
Canonicalization preserves the character sequence; it performs no NFC/NFKC,
case folding, or locale transformation. In particular, composed and decomposed
spellings remain distinct JS keys, as required by **S1.11** and the
[ECMAScript String model](https://tc39.es/ecma262/multipage/ecmascript-data-types-and-values.html#sec-ecmascript-language-types-string-type).

**S8.2.2v5**, **D4.6.1v4:** JS also admits lone UTF-16
surrogates. Use the shared lossless WTF-8 extension for those units, combining
valid surrogate pairs into their canonical scalar encoding. Never replace a lone surrogate with U+FFFD for
key identity. This keeps the existing MVP string domain and gives equal
code-unit sequences one key representation. It does not change UTF-16 JS
length, indexing, or comparison to byte-based operations.

NamePool creation/lookup, shared shape additions, and dynamic Lambda map
reads/writes now canonicalize at their boundaries. Focused coverage checks
computed JS keys against Lambda map lookup, including embedded NUL. Ordinary Map string keys use the same
canonical string-content relation; other Map keys retain §10.4's rules.

### 10.4 ES Map entry storage

Object properties and collection entries are separate domains:
`m.x = 1` does not create the entry addressed by `m.get("x")`. Property
shapes describe the ordinary object's fields; arbitrary collection keys
cannot be encoded as ShapeEntry property names.

Use host-owned ordered Item storage and a `lib` hash index of stable entry
positions inside the ordinary Map's collection state. Keep all key/value
edges visible to precise tracing and own numeric payloads across overwrite,
growth, and deletion. The native index must not hide untraced Item references;
its lifetime follows the collection. No VMap is involved (**D1.3v3**, **D5.3**).

Keys follow SameValueZero: INT/FLOAT forms of one Number share a key, NaNs
match, and both zeros share the canonical zero key. Reference keys use
identity. Updating an entry retains its order; delete/reinsert moves it to
the end. Missing `get` returns undefined, `has` distinguishes presence,
`set` returns the receiver, and `delete` reports whether an entry existed.
These follow the [ECMAScript Map contract](https://tc39.es/ecma262/multipage/keyed-collections.html#sec-map-objects)
under **S1.11**. `size` is the live entry count; supporting this fixed builtin
observation does not admit user-defined accessor machinery.

### 10.5 Iteration through the shared loop model

**User direction:** map iteration should resemble Lambda for-expressions and
loops. Reuse their traversal, key/value binding, control-flow, and rooting
model (**S8.1.2v2**, **S8.2.1v4**, **S8.4.1v2**, **D8.2.6**), while preserving
JS syntax and observable behavior under **S1.11**.

| JS surface | Loop binding |
|---|---|
| `for (const [k, v] of m)` / `for (const [k, v] of m.entries())` | Key and value, analogous to Lambda `for (k, v in m)` |
| `for (const k of m.keys())` | Key only |
| `for (const v of m.values())` | Value only |
| `for (const pair of m)` | A fresh ordinary `[key, value]` array when observable |
| `for (const k of Object.keys(o))` | Own string keys |
| `for (const v of Object.values(o))` / pair binding over `Object.entries(o)` | Own values / key-value pairs |

The pair pattern is restricted to two simple bindings without defaults/rest;
§22 adds captures of the distinct per-iteration lexical bindings.
Collection and projection expressions evaluate once. Proven intrinsic loop
sources lower directly to a cursor/key/value loop; escaping iterator values
and custom iterator protocols remain unsupported. Elide an entry array only
when its identity and contents cannot be observed independently.

Share neutral traversal primitives rather than adopting Lambda's existing
symbol-key list or fixed-length walk unchanged. JS Map loops are live:
skip deleted entries, observe later updates to unvisited entries, and visit
entries appended before exhaustion, including delete/reinsert. Clear and
nested iteration must preserve each active cursor. Do not cache initial size
as the termination bound or compact storage in a way that invalidates cursors.
These requirements follow [CreateMapIterator](https://tc39.es/ecma262/multipage/keyed-collections.html#sec-createmapiterator).

Object projections instead produce snapshots at expression evaluation;
lazy lowering must not change their captured keys/values. Array-index keys
come first in numeric order, followed by other strings in creation order.
Retyping preserves order; deletion/re-addition changes it as above. This is
[ordinary own-property order](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-ordinaryownpropertykeys),
not a change to Lambda iteration semantics. General `for-in` remains excluded.

### 10.6 Completion criteria

Acceptance covers shared-shape siblings, add/retype/delete/re-add, aliases,
cycles, duplicate keys, undefined versus absence, exact evaluation order,
cross-pool and computed key identity, Unicode boundaries, collection key
identity/order, mutation during nested iteration, and rejection of excluded
property forms. It includes scalar relocation, native-index cleanup under
forced GC, and zero existing-JS-runtime callbacks.

The implementation record and concrete helper inventory are in
[JS MVP objects implementation](../impl/JS_MVP_Lmd_Objects.md). Apply §8's
focused and shared baseline gates. Release measurements cover fixed fields,
computed growth, retyping, deletion churn, lookup/update, and key/value loops.

### 10.7 Scope decisions

- Canonical keys use lossless UTF-8/WTF-8 with no Unicode normalization
  (**S8.2.2v5**, **D4.6.1v4**).
- Fixed intrinsic prototype operations beyond this phase's admitted builtins
  produce capability diagnostics. An inherited method is never reported as
  an absent own field. Extracted Map method values and builtin mutation are
  diagnosed; shadowed user bindings and own methods follow ordinary calls.
- Native ordered-entry storage is traced and finalized by the shared GC.
  Active loops preserve slot positions; inactive storage can compact.
  Neither VMap nor existing JS lifecycle hooks participates (**D2.6.9v3**,
  **D5.3**).
- `__proto__`, descriptors, accessors, proxies, user prototype mutation,
  general iterator objects/callbacks, and general destructuring remain outside
  this phase. No further scope decision blocks the admitted subset.

### 10.8 Validation and release evidence

The latest measurements and validation status are in §20. Earlier object/Map
and element-coercion evidence is archived in
[`MVP_Result3.json`](../../test/benchmark/js_mvp_lmd/MVP_Result3.json)
and the [implementation record](../impl/JS_MVP_Lmd_Objects.md#8-generic-element-coercion-and-scalar-ownership).
Exact earlier releases, sources, MIR, runners and gate logs remain in
`temp/mvp_lmd_element_tuning/` (`confirm/` and `recheck/`).

## 11. Object and Map tuning

Implemented as bounded optimizations, in this order:

1. Predict complete literal shapes from existing type/range facts, canonicalize
   static keys during compilation, and initialize known fields directly.
2. Carry bounded shape predictions through aliases and closed calls; retain
   typed field values and pass rooted object arguments individually.
3. Remove redundant boxing and guards where mutation and safepoint analysis
   proves this safe.
4. Reuse payload storage for compatible shape transitions; preserve the shared
   rebuild path for layouts that move fields.
5. Specialize admitted Map method calls and immediate entry updates, retaining
   SameValueZero, identity, insertion order and live iteration.
6. Apply bounded inlining and scalar replacement to local objects whose identity
   and allocation cannot be observed, sharing neutral compiler machinery where
   available.

These changes use immutable shape guards (**D3.4.3v5–D3.4.6**, **D8.4.1v2**),
the direct-call ABI (**D8.4.2v2**), and precise ownership (**D5.3**). Canonical
UTF8/WTF8 identity remains shared with Lambda (**S8.2.2v5**, **D4.6.1v4**);
JS Number behavior remains governed by **D2.2.5** and **S1.11**. VMap,
`__proto__`, descriptors, accessors and proxies remain outside the phase.

The bounds and remaining tuning opportunities are recorded in the
[implementation record](../impl/JS_MVP_Lmd_Objects.md#6-bounded-tuning-implementation).
Section 12 extends these optimizations.

Acceptance requires the existing MVP and forced-GC cases, shared-runtime
regression gates, and output-verified release comparisons against the archived
binary. Pin MIR and compare self-reported workload times; use alternating
control/candidate samples and a control/control noise check. The six newly
enabled workloads and existing object/Map cases form the performance set.

## 12. Further object and Map tuning

Implemented the following bounded extensions:

1. Reuse shared retype plans, including destination offsets and payload reuse
   eligibility, to avoid repeating structural work on established transitions.
2. Preserve native field values through numeric operations and stores, with
   guarded fallback for changing field types. Hoist guards only when effects
   prove the shape stable.
3. Track small immutable shape sets, including recursive child fields, to
   handle common leaf/branch layouts without generic property lookup.
4. Extend bounded inlining to small statement/loop bodies at loop call sites,
   and scalar replacement to local objects with static writes, preserving
   evaluation order and identity.
5. Separate nonallocating Map operations from allocation/error paths and reuse
   lookup information during insertion.

Shared immutable shapes and bounded guards follow **D3.4.3v5–D3.4.6** and
**D8.4.1v2**; native values and calls follow **D2.2.5** and **D8.4.2v2**.
Precise roots and payload reloads across safepoints remain required (**D5.3**).
Canonical UTF8/WTF8 identity, SameValueZero, order and live iteration retain
their existing contracts (**S8.2.2v5**, **S1.11**). The feature exclusions in
§10.7 remain. Apply §11's correctness and paired release measurement gates;
report each implemented bound and any unresolved performance result.

## 13. Numeric loops and array parameters

Implemented two corrections for the `diviter` and `pnpoly` gaps found by
the broader comparison:

1. Preserve the callee's numeric region during inlining so proven integer
   arithmetic does not acquire conversions from its caller.
2. Retain complete binding-kind facts through parameter reads and inline
   snapshots, allowing known arrays to use direct index handling.

Section 14 addresses element coercion and ownership costs. Bounds, changing
element kinds, aliases and precise scalar homes remain valid.

These are retained-analysis and representation corrections (**D8.2.4v2**,
**D8.2.5v3**, **D8.2.6**, **D2.2.5**, **D8.4.1v2**, **D5.3**).
Use the frozen current release as control,
pin native MIR, check outputs, and compare self-reported times with alternating
pairs and an identical-control lane. Include scalar, object, Map and array
controls plus the existing MVP, forced-GC, Lambda and Test262 gates.

## 14. Element coercion and scalar ownership

Implemented; latest evidence is in §17.

1. Route generic numeric coercion through the shared inline-number decoder,
   retaining JS conversion and capability errors for other values.
2. Emit Float snapshot copies directly into their existing destination homes,
   preserving packed values, signed zero and wide payloads. Keep independent
   ownership across writes, calls, growth and GC.
3. Consume simple scalar reads directly when numeric operations finish before
   any mutation or safepoint; retain owned snapshots across effectful operands.
4. Measure the changes against a frozen release on `pnpoly`, `diviter` and
   the full supported workload set, including shared-emitter clients.

This is representation and ownership tuning under **D2.2.5**, **D5.3.1–D5.3.4**
and **D8.2.6**. Numeric-array promotion and new object features remain outside
the phase. Apply §13's paired MIR measurements and correctness gates.

## 15. Numeric libraries and wider benchmark coverage

**Status:** IMPLEMENTED IN SOURCE, acceptance pending; 2026-10-07.
This phase adds fixed-size typed arrays and Math
operations on the shared Lambda substrate (**S1.11**, **D1.3v3**, **D2.2.5**,
**D2.6.1v3**, **D5.3**, **D8.2.6**).

### 15.1 Scope

- `Int32Array`, `Uint8Array`, and `Float64Array`: construction by length,
  zero initialization, indexed reads/writes, `.length`, and `.fill()`.
  Preserve JS length validation, element conversion, out-of-bounds behavior,
  reference aliasing, and precise storage ownership.
- `Math.sqrt`, `sin`, `floor`, and `trunc` first; then `abs`, `min`, `max`,
  `ceil`, and `cos`. Retain JS coercion, argument evaluation, NaN, infinities,
  and signed zero. Intrinsic recognition must respect binding shadowing and
  the admitted receiver/member contract.
- Use owning, fixed-size typed storage. Buffer/view constructors, exposed
  buffers, detachment, resizing, shared buffers, and Atomics are deferred.
  Ordinary mixed arrays retain their existing representation; automatic
  numeric-array promotion is a separate optimization.

The first nine benchmark targets, identified from their current sources, are:

| Suite | Targets |
|---|---|
| R7RS | `nqueens`, `fft` |
| Larceny | `primes`, `quicksort`, `triangl`, `paraffins`, `ray` |
| Kostya | `primes`, `matmul` |

The release comparison in §15.3 now checks these kernels, with the duplicate
`primes` workload measured through its canonical Kostya row. Algorithms,
inputs, iteration counts, and result checks are preserved; only the host
harness is adapted. Node CLI/I/O compatibility is outside this phase.

### 15.2 Reuse and helper disclosure

For every operation, inspect existing Lambda functions, storage primitives,
and shared MIR emitters before adding code. Reuse a matching function directly;
for a small semantic difference, consider a narrowly scoped option that keeps
existing callers' behavior unchanged. Keep JS coercion distinct from physical
conversion and ownership (**S1.11**, **D2.4.3**, **D5.3.4**, **D8.2.6**).

The implementation reuses Lambda's `ArrayNum` allocation, shared MIR storage
operations, existing JS coercion lowering, and Lambda's native Math entry
points. There are **no new native runtime helpers**. Four compiler utilities
were disclosed before coding; the existing string-index helper gained a typed
index option. The audited reuse choices and dependency inventory are in the
[numeric implementation record](../impl/JS_MVP_Lmd_Numeric.md).
Full-JS runtime helpers remain outside §1's execution boundary, including
transitive calls.

Before implementing any further new helper,
including compiler utilities or shared additions, tell the user its proposed
name/location, purpose, why reuse or an option is insufficient, and its
dependencies and GC/ownership effects. Record that inventory with the eventual
implementation evidence; do not add parallel per-element-kind implementations.

### 15.3 Acceptance and following phases

Release compilation and the **25 standard / 17 microbenchmark** comparison
passed. §17 records the latest results, including all 12 newly admitted
kernels. The full constructor/Math/numeric-edge acceptance matrix below
remains pending; §17's targeted semantic/GC checks cover its tuning changes.

Validate numeric edges, constructor lengths, typed stores, bounds, aliases,
shadowing, and allocation lifetimes normally and with forced GC/poisoning.
Require the existing MVP cases, Lambda/input and full-JS Test262 gates, and
matched outputs for each newly admitted benchmark. Compare frozen release
binaries with pinned native MIR and self-reported execution times, alternating
pairs, and an identical-control peer; include existing workloads and affected
shared clients. Report actual coverage, new/extended helpers, and unresolved
results before marking the phase implemented.

The next coverage surfaces are tracked in §18, followed by functions/classes:

| Phase | Surface | Candidate workloads |
|---|---|---|
| Array library | `push`, `pop`, `join`, `slice`, `fill`; then `new Array(n)` with genuine hole semantics | Kostya `json_gen`/`base64`, Larceny `puzzle`, R7RS `mbrot` |
| String library | `charCodeAt`, `charAt`, `repeat`, `slice`, `String.fromCharCode`; simple destructuring assignment | Kostya `brainfuck`/`levenshtein`, Julia string workloads |
| Functions and classes | Captured locals, receiver calls/`this`, constructors, basic classes and inheritance | AWFY and larger JetStream workloads |

Holes must remain distinguishable from present undefined values (**S1.11**).
AWFY also needs its class/receiver behavior; array methods alone do not admit
it. These later surfaces require their own scoped phases. `__proto__`, fancy
descriptors, accessors, and proxies remain excluded; plain objects and Map
continue to use ordinary Lambda storage without VMap (**D2.6.9v3**, §10).

## 16. Numeric facts and element access tuning

**Status:** IMPLEMENTED IN SOURCE, acceptance pending; 2026-10-08.
This phase addresses lost numeric facts around array loads, especially in
`triangl`, `array1`, and `matmul`.

1. Preserve typed-array lane and length facts through aliases, `.fill()`, and
   closed calls. Keep out-of-bounds reads observable as undefined.
2. Carry element results directly into numeric consumers and preserve integer
   store inputs, using existing Lambda storage emitters. Box at observable
   boundaries; retain JS coercion order, wrapping, signed zero, and ownership.
3. Improve counted-loop integer and bounds proofs, including nested loops and
   affine indices. Hoist only facts valid across mutation, calls, and GC.
4. Propagate numeric contents of unchanged ordinary array literals into loads
   and closed calls without changing their boxed storage representation.

Reuse shared Lambda facilities first and disclose further helpers under §15.2.
These are static representation proofs under **D2.2.5**, **D2.4.3**,
**D2.6.1v3**, **D5.3.4**, and **D8.2.6**, without runtime feedback or inline
caches (**D8.4.1v2**). Allocation and recursive-call tuning follow measurement;
Math and fill already use native scalar/lane operations.
Three disclosed compiler utilities were added; no native runtime helper was
added. See the [implementation record](../impl/JS_MVP_Lmd_Numeric_Tuning.md).

Apply §15.3's semantic and GC gates. Compare frozen release binaries across
all supported kernels using pinned MIR, self-reported times, alternating
pairs, and an identical-control peer; retain the prior scalar/object/Map
workloads as regression controls. Source-level coercion probes motivate this
phase but are not compiler speedup evidence.

The [numeric-tuning report](../../test/benchmark/js_mvp_lmd/MVP_Result4.md)
archives this phase's comparison. §17 records the latest performance and
targeted semantic/GC validation; §15.3's full numeric feature matrix remains
pending.

## 17. Slow-kernel tuning

**Status:** implemented; latest paired release benchmarks pass; 2026-10-08.
Prioritize `triangl`, `deriv`, `gcbench`, `pnpoly`, `binarytrees`, and optional
integer conversions in `quicksort`.

- Retain integer elements and optional locals with separate presence flags;
  preserve missing values, NaN, signed zero, and snapshot ownership.
- Lower conditions directly to branches and carry immutable container facts
  through closed calls and unchanged local factories.
- Reuse guarded Lambda shape stores, plain-Item returns for closed nonnumeric
  factories, and precise roots. Evaluate fixed-key literal values in order
  before allocating their unobservable parent; avoid scalar storage for
  nonnumeric locals.

Reuse Lambda storage, range, conversion, map allocation, return ABI and precise
root machinery (**S1.11**, **D2.2.5**, **D2.4.3**, **D3.4.3v5**,
**D5.2.1v3**, **D5.3.4**, **D8.2.6**). The original two disclosed compiler
helpers remain `branch_condition` and `immutable_member_kind`; this follow-up
adds no helper or runtime import.

The [Result5 report](../../test/benchmark/js_mvp_lmd/MVP_Result5.md) and
[implementation record](../impl/JS_MVP_Lmd_Slow_Tuning.md) retain this phase's
measurements, regression limits and validation scope. Current measurements
are in §20. Recursive allocation/collection cost and stronger control-flow
bounds remain tuning opportunities.

## 18. Ordinary arrays and core string methods

**Status:** IMPLEMENTED; initial benchmark output and performance captured in
[MVP_Result6](../../test/benchmark/js_mvp_lmd/MVP_Result6.md); subsequent tuning
and validation recorded in §19; 2026-10-08.
Add `Array(n)`/`new Array(n)`, ordinary-array `fill`, `push`, `pop`, and
`join`; string `charAt`, `charCodeAt`, `repeat`, and `String.fromCharCode`;
and simple array destructuring assignments, including swaps. Preserve holes
as absent properties, mutation/aliases, argument order, binding shadowing,
UTF-16 code units and lone surrogates (**S1.11**). Direct array reads and
iteration expose holes as undefined; own projections omit them. General
sparse writes, iterator destructuring, nested/rest/default patterns, and
object-to-primitive conversion remain outside this phase.
`join` supports primitive elements, null/undefined and holes; nested arrays
and other object elements require the deferred object conversion protocol.

Targets are R7RS `mbrot`, Larceny `puzzle`, and Kostya `base64`, `json_gen`,
`brainfuck`, and `levenshtein`; Result6 covers 31 standard kernels plus 17
micros. The six targets and prior 42 workloads passed the recorded output
checks using pinned MIR. Result6 records self-reported times, exact release
binary identities and per-lane outputs. The six new targets have no
pre-feature MVP control, so their cross-engine ratios are descriptive.

Reuse Lambda array storage, verbatim stores, string joining/repetition and
UTF-16 utilities, with precise roots (**D2.6.1v3**, **D5.3.1–D5.3.4**).
Plain objects retain Map/shape transitions (**D3.4.3v5**); VMap, `__proto__`,
descriptors/accessors and proxies remain excluded. Follow §15.2's helper
disclosure and §15.3's acceptance requirements. Constructors/classes and
receivers, including the error statements in AWFY bundles, are covered in §21;
captured mutable locals are covered in §22.

The [implementation record](../impl/JS_MVP_Lmd_Array_String.md) lists shared
reuse, disclosed helpers and the checks added after the initial Result6 capture.
The targeted semantic/forced-GC checks now pass; §20 records the current
baseline status separately from the historical snapshot.

## 19. Array and string tuning after Result6

**Status:** IMPLEMENTED AND VALIDATED; 2026-10-08.
Prioritize `levenshtein`, `base64`, `brainfuck`, and `json_gen`; retain
`mbrot`, `puzzle`, and the prior workloads as regression controls.

- Preserve builtin return kinds and destructured element/lane facts through
  inference, calls and swaps. Elide unobservable swap arrays using ordered
  snapshots; strengthen bounds proofs where existing facts suffice.
- Reuse Lambda's ASCII character strings, lower proven character-code reads
  directly, and retain numeric interpreter state and JS missing-value behavior.
- Avoid the conversion array for string-only `join`, specialize safe append
  stores, and retain dense-array proofs independently of numeric contents.
- Preserve definite string results and combine eligible concatenations using
  existing Lambda string functions, with ordered conversions and exact roots.

Reuse existing analysis, storage, character and string helpers first; disclose
any new helper before adding it. Preserve **S1.11**, **D2.4.3**, **D5.3.4** and
**D8.2.6**, including UTF-16 behavior, holes, aliases and mutation. Keep §18's
feature exclusions. Diagnostic source variants identify opportunities but do
not count as compiler speedups. Acceptance uses unchanged sources, frozen
release binaries, pinned MIR, paired self-reported timings on all 48 workloads,
and §15.3's semantic, forced-GC, Lambda and Test262 checks.

This phase's measurements, exact provenance, helper inventory and validation
are retained in the
[implementation record](../impl/JS_MVP_Lmd_Array_String.md#result6-tuning-19).
Current measurements follow in §20. Stronger general bounds and per-array
mutation facts remain future work.

## 20. Remaining numeric and indexing costs

**Status:** IMPLEMENTED AND VALIDATED, 2026-10-08.
Target `collatz`, `primes`, `fannkuch`, and `base64` by simplifying remainder
zero tests, extending safe integer loop bounds, retaining integer array indices,
and specializing ASCII indexed reads. Reuse Lambda's AST/emitter, character
table and storage functions; preserve JS rounding, missing values and precise
roots (**S1.11**, **D2.4.3**, **D5.3.4**, **D8.2.6**).
The follow-up adds guarded integer recurrence loops with exact floating-point
fallback, consistent scoped bounds for numeric reads, and cached ASCII Items.
Existing Lambda functions and the shared numeric opcode plan are reused;
no new runtime helper is added. Unicode and unsafe numeric cases retain their
general paths. The disclosed compiler-helper inventory is in the implementation
record.

**Latest measurement:** 15 alternating release pairs against the prior §20
binary, with an identical-control peer and fresh untyped Lambda references
for `collatz` and `base64`. Native MIR is pinned; self-reported execution time
excludes compilation and process startup. All **2,190 measured and 146
discarded output checks** pass on unchanged sources. Median milliseconds:

| Workload | Before → after | Paired gain | Untyped Lambda |
|---|---:|---:|---:|
| collatz | 344.351 → 200.168 | 1.720× | 298.685 |
| base64 | 13.414 → 11.869 | 1.128× | 12.069 |

Across all 48 workloads the geometric gain is **1.032×**. Collatz now takes
**33.0% less time** than untyped Lambda; base64 is approximately level
(1.7% lower median). The same bounds correction improves `array1` **2.14×**.

The previous ~2% Map/JSON regressions come from **native executable layout
sensitivity**: their MIR operations and hot helper bodies are unchanged, and
matching helper placement removes the measurable regressions over 60 pairs.
The final normal build still measures `map_lookup` **1.0% slower** than §19;
JSON's **0.2%** shift is within uncertainty. There are no statistically
significant regressions against the prior §20 build in the 48-workload run.
Separate 60-pair object checks also find no regression in the corrected final
build. The small residual Map cost remains recorded.

Checks pass: **44/44 MVP tests** normally and with forced GC/poisoning,
**6,408/6,408 Lambda/input**, and **40,261/40,261 Test262** with zero retries.
Exact provenance, helper inventory, uncertainty and baseline status:
[implementation record](../impl/JS_MVP_Lmd_Array_String.md#further-collatzbase64-tuning-and-regression-diagnosis).

## 21. Classes and inheritance aligned with Lambda

**Status:** basic class phase IMPLEMENTED IN SOURCE, 2026-10-08.
The interoperability requirement remains governing (USER, 2026-10-08);
script-level cross-language call/subclass adapters remain future work.

JS/MVP class inheritance must share Lambda's foundation so instances and
class relationships can interoperate deeply in both languages. JS may extend
that foundation with additional capabilities, provided the extensions preserve
the shared runtime contracts (**D2.6.6v3**, **D2.6.9v3**).

- **Shared identity and ancestry.** Build on `TypeNominal` and its single-base
  relationship. Class-created prototype and constructor inheritance must agree
  with that ancestry. Preserve nominal identity across shape transitions;
  different instance shapes still belong to the same class (**S2.1.4**,
  **S11.3.1v2**).
- **Shared storage.** Reuse Lambda Map/array storage, canonical name identity,
  inherited field-layout contracts, and shape transitions. JS property changes
  must respect any inherited Lambda field contracts and keep the shape's
  description of stored bytes accurate (**S2.1.3v2**, **D3.4.3v5–D3.4.5**).
  Plain instances continue to use ordinary containers without VMap.
- **Explicit JS extensions.** Prototype properties, receiver calls, constructors,
  `super`, and `newTarget` extend the shared model through language-specific
  metadata and call entries. Preserve Lambda's bound-method behavior and JS's
  explicit receiver behavior at their respective language boundaries. JS
  extensions must never invalidate Lambda's sealed-type/field assumptions or
  reuse their optimizations without valid guards (**S1.11**, **D2.6.7**,
  **D2.6.9v3–D2.6.10**, **D6.2.2v2**).
- **Interop is an acceptance requirement.** Specify and validate instance
  passage in both directions, preserved identity/ancestry, field access,
  inherited/overridden method calls, and precise ownership. Before admitting
  cross-language subclassing, resolve constructor and inherited-field contracts
  in both directions. Unsupported combinations receive explicit diagnostics
  (**D1.2v2**, **D1.5v2**).

Reuse existing Lambda functions first; consider a small explicit option for a
semantic difference and disclose any new helper before implementation (§15.2).
`__proto__`, user prototype mutation, descriptor APIs, accessors, and proxies
remain outside the planned basic class phase. This section records the design
constraint; §3 continues to describe the implemented admission boundary.


### 21.1 Initial class phase and performance boundary

Admit named top-level class declarations, simple constructors, instance/static
methods, single inheritance, default derived constructors, `super()` and
`super.method()`, class `this`/`new.target`, and fixed-chain `instanceof`.
Instance fields use ordinary assignments and retain class identity through
addition, retyping and deletion. Own reflection omits nonenumerable class
methods; inherited lookup follows the shared nominal base chain.

Uncaught `throw`, direct `throw new Error(message)`, and primitive template
interpolation cover the AWFY diagnostic paths. First-class Error objects,
handlers, class expressions/nested classes, field initializers,
ordinary-function construction, Error options, computed `super` keys, and
writes/deletes through `super` remain excluded. Class prototype and constructor
properties are readable; their
mutation remains excluded (**S1.11**, **D2.6.9v3**, **D6.2.2v2**).

Receiver-aware calls and class property dispatch are selected only for units
that admit classes. Existing units retain their prior call ABI and property
helpers. Compare all 48 previous workloads against the frozen release using
pinned native MIR, alternating self-reported times and an identical-control
peer. The six coverage targets are AWFY sieve, permute, queens, towers, list,
and mandelbrot. Shared nominal identity, ancestry, layouts and guest member
resolution establish the runtime interop foundation; cross-language callable
adaptation and subclass construction remain explicit unsupported boundaries.

Latest benchmark evidence is recorded in §21.2. Class semantic/forced-GC and
broad baseline suites have not been run for this phase; §20's totals belong to
its prior binary.

The [class implementation record](../impl/JS_MVP_Lmd_Classes.md) records helper
reuse, benchmark evidence, regression analysis and outstanding validation.

### 21.2 Class workload tuning

Tune numeric indexing without intermediate string keys, fixed-name property
lookup with immutable-shape guards, and ordinary receiver calls through the
existing MIR entry ABI. Retain own-field overrides, shape-change fallbacks,
constructor validation and caller-owned scalar/root lifetimes (**D3.4.3v5–D3.4.5**,
**D5.2.1v3**, **D6.2.2v2**). Reuse shared field readers/writers and shape transitions;
extend existing helpers before introducing new runtime entry points.

Deletion reuses a bounded plan on immutable tree-owned shapes; repeated Map
get/set/has operations reuse a checked entry ordinal. Both retain shared
canonical-key equality and existing mutation fallbacks (**D3.4.4v4**, **D3.4.5**).

Measure sieve, permute, queens, towers and list against frozen releases and
untyped Lambda. Confirm deletion and Map lookup regressions separately, then
check all prior workloads using alternating release runs, pinned native MIR,
self-reported time, output oracles and an identical-control noise lane.

**Phase results:** all **54 workloads** pass output checks. Against the fresh
pre-tuning release, sieve is **6.45× faster**, permute **3.96×**, queens **3.55×**,
towers **2.66×**, and list **2.03×**; list is now **26% faster than untyped Lambda**.
Sieve, permute, queens and towers remain **2.47–3.44× slower** than their Lambda
ports (including the Towers storage/validation differences).

The 48 prior workloads have **3.23% lower geometric-mean time**. Sixty-pair
confirmations resolve the original deletion and Map lookup regressions:
**49.5%** and **36.4% lower time** than the pre-class release; JSON generation
is neutral. Follow-up checks find no confirmed new slowdown, including escaped
retyping, gcbench and binarytrees. Small effects remain limited by measurement
noise. All 54 timings and separate regression confirmations are recorded in
[MVP_Result7](../../test/benchmark/js_mvp_lmd/MVP_Result7.md).
Exact binaries, uncertainty and remaining validation are also documented in the
[implementation record](../impl/JS_MVP_Lmd_Classes.md#class-and-regression-tuning-2026-10-08).

## 22. Closures, callbacks and array growth

**Status:** first step IMPLEMENTED AND VALIDATED, 2026-10-08. The [implementation record](../impl/JS_MVP_Lmd_Closures.md) contains helper disclosures, exact artifacts and timing limits. Static fields and `substring` remain the follow-up.

**Closure alignment (USER, 2026-10-08).** JS/MVP closures must align with and
reuse Lambda's function/environment representation, allocation, owned `Item`
storage, scalar homes and precise GC tracing. Lambda captures are immutable
snapshots; JS captures reference shared lexical bindings that may be mutable
(**D6.2.3v2–D6.2.4**, **D1.3v3**). Enclosing code and sibling closures must
observe the same binding updates, including after the creating call returns.
Preserve JS `const`, temporal-dead-zone and per-iteration `let` semantics;
support nested/escaping captures and arrow lexical `this` (**S1.11**).
The JS extension must preserve Lambda's immutable-capture contract and the
shared runtime's ownership and call boundaries (**D5.2–D5.3**, **D6.2.2v2**).

### 22.1 Scope and coverage targets

| Addition | Target workloads |
|---|---|
| Captured locals/parameters and lexical `this`; array `forEach` | AWFY `nbody`, `richards` |
| Closures in the bundled SOM library; hole-preserving array `slice` and `.length` growth | AWFY `bounce`, `storage`, `cd` |
| Indexed writes beyond length, preserving holes in the gap | JetStream `crypto_sha1` |

The first step enables **six additional workloads (54 → 60)**, including the
complete bundled libraries for Bounce/storage. `slice` must
return an independent shallow copy with holes preserved. `forEach` captures
the initial length, skips absent elements, observes intervening mutations and
propagates callback failures. Reuse existing Lambda array storage and copying
operations with explicit JS bounds/hole policy (**S1.11**, **D1.3v3**).

A follow-up adds public static class fields in declaration order and UTF-16
`String.substring`, targeting AWFY `deltablue` and `json` (**60 → 62**).
Static fields reuse class storage, nominal ancestry and shape transitions
(**D2.6.9v3**, **D3.4.3v5–D3.4.5**). Separately audit adapters for Julia
`parse_integers`, `iteration_pi_sum` and `matrix_statistics`; preserve kernels,
warmup, timing boundaries and oracles. Refresh the manifest's stale 63-row
assumption against the current 71-row standard inventory.

Ordinary-function construction, arbitrary prototype mutation, descriptors,
accessors, proxies, `__proto__`, BigInt, RegExp and Node I/O remain deferred.
Prefer extending existing Lambda helpers or adding a narrow semantic option;
disclose every necessary new helper and its dependency closure before coding.
Retain §1's prohibition on calling full LambdaJS runtime helpers.

### 22.2 Correctness and performance acceptance

Allocate capture environments only where required; retain native locals,
direct calls, class caches and existing in-bounds array fast paths elsewhere.
Implement in small independently measured steps. Validate shared/escaping
captures, loop bindings, lexical `this`, scalar lifetimes and array mutation
under forced GC; complete the outstanding class-phase checks and Lambda/Test262
baseline gates.

**No performance regression is an acceptance requirement.** Compare all 54
existing workloads against the frozen Result7 release using pinned native MIR,
unchanged inputs/oracles, alternating self-reported timings and an identical
control noise lane. Check each workload and shared Lambda clients; aggregate
gains cannot excuse individual regressions. Confirm suspected slowdowns with
longer paired runs, fix or revert confirmed regressions, and report unresolved
noise explicitly. Measure newly admitted workloads against fresh untyped
Lambda and Node references with documented timing and port differences.

**Phase results:** 60 workloads pass their output checks. Thirty balanced
release pairs across the prior 54 workloads show no confirmed regression
against Result7 (geometric mean time ratio 0.9973); 60-pair checks of three
shared Lambda clients also show no confirmed slowdown. MVP semantic checks
pass **52/52**, normally and with forced GC/poisoning; Lambda baseline passes
**6,427/6,427** and Test262 baseline **40,261/40,261** with zero retries.
The six new comparisons use fresh Lambda/Node references; SHA1's canonical
Lambda port is explicitly annotated and is not labeled untyped.

## 23. Class workload tuning

Target Bounce, NBody, Richards and CD while retaining the 60-workload cohort.
Reuse Lambda's immutable shapes and packed lanes (**D3.4.3v5–D3.4.5**), shared
numeric emitters and precise scalar ownership (**D5.2–D5.3**):

- Cache four exact shapes per class property site, including field storage and
  offset. Inline ordinary numeric, Boolean and pointer lanes; retain shared readers for
  other and optional layouts. Numeric consumers and matching native stores
  avoid intermediate boxing.
- Inline small, capture-free, parameterless method expressions only after
  checking the captured callee's entry address. Overrides retain ordinary calls.
- Reuse a completed constructor shape only for straight-line field assignments
  with receiver-free values. Calls, receiver observation/escape, computed keys,
  branches and derived construction retain incremental initialization.
- Hoist the captured iterator's stride and read live Map entries directly;
  retain the existing shared cursor helper for deleted entries.
- Reuse Map's last-entry cache for guarded exact-key `get`/`has` hits, retaining
  the existing lookup helper and scalar adoption on the other paths.

No shared Function/closure representation changes (**D6.2.3v2–D6.2.4**).
The only new helpers are the cache-population helper `mvp_lmd_cache_property`
and compiler proof `constructor_layout_reusable`; both were disclosed before
coding. No new runtime import is needed. Acceptance uses frozen release
binaries, pinned native MIR, output-checked self-reported timings, balanced
pairs with an identical-control peer, all 60 workloads, and forced-GC/baseline
gates. Detailed evidence: [class tuning](../impl/JS_MVP_Lmd_Class_Tuning.md).

**Latest results:** the final release reduces self-reported time by **23.0%**
on Bounce, **52.2%** on NBody, **47.5%** on Richards and **40.2%** on CD.
All **60 workloads** pass output checks, with no statistically confirmed
regression in the paired comparisons. Longer checks measure Map lookup **5.5%**
and Map iteration **7.9%** faster. MVP semantic tests pass **54/54**, normally
and with forced GC/poisoning. Combined Lambda/input baseline passes
**6,459/6,459**; Test262 passes **40,261/40,261** with zero retries.
Exact binaries, fresh Lambda/Node references and uncertainty are recorded in
the linked report.

## 24. Method specialization and stable class fields

Reduce the remaining CD/Richards overhead through bounded, guarded inlining of
parameterized methods, setters and callbacks; stable nullable field storage;
and scalar replacement of provably unobserved temporary class instances.
Reuse the existing inline frame, scalar-field and immutable shape machinery
(**D3.4.3v5–D3.4.5**, **D5.2–D5.3**). Captured function identity, argument order,
mutable closure cells and receiver semantics must remain intact
(**S1.11**, **D1.3v3**, **D6.2.3v2–D6.2.4**). Escaping or observable instances
retain ordinary allocation. Disclose added helpers before implementing them.

Accept each step only with output-checked, balanced release comparisons using
pinned native MIR and self-reported time. Protect all 60 workloads and shared
Lambda clients; run focused ownership tests, forced GC and both baseline gates.
Keep results separate from the published Result series until requested.

**Implemented:** guarded method/callback inlining, bounded scalar class locals,
and nullable links using Lambda's existing Map pointer lane. Release comparisons
show Richards **15.1%**, CD **2.7%**, Permute **23.6%**, Queens **17.7%** and
Towers **26.6%** faster; all **60 workloads** pass output checks. Performance
acceptance remains open: longer checks retain Map lookup **2.14%** and GCbench
**0.17%** slowdowns, and cold compilation/startup costs increase. MVP checks
pass **58/58** normally and with forced GC/poisoning, Lambda/input **6,468/6,468**,
and the final Test262 run **40,261/40,261** with zero retries. Exact evidence,
earlier gate instability and remaining scope are in
[method and field specialization](../impl/JS_MVP_Lmd_Specialization.md).

## 25. Guarded numeric regions

Reduce CD's repeated generic coercions and call/root boundaries by guarding
receiver/argument layouts once around a pure numeric body. Reuse Lambda shape
identity and packed lanes (**D3.4.3v5**), numeric emitters and precise root analysis
(**D5.2**, **D5.3.1–D5.3.2**). Ordinary execution populates shared property caches;
layout or numeric-type mismatches select the original body before any effects.
Preloaded fields remain native within the region. Small scalar methods can inline
transitively beyond loop sites after checking the captured callee and arguments.

Include guarded Boolean lanes, transitive scalar comparators and reusable
constructor inlining. Extend scalar-object analysis across simple direct factory
returns; observable instances retain ordinary allocation and identity. Mutable
closure cells retain Lambda's existing representation (**D6.2.3v2–D6.2.4**).
Infer GC effects from emitted operations; never suppress rooting on the strength
of a method name or benchmark-specific assumption.

The disclosed compiler helpers are `numeric_region_path`, `numeric_region_read`,
`plan_numeric_region`, `guard_property_cache`, `guard_numeric_region` and
`scalar_factory_plan`; no runtime helper or runtime data layout is added. Proven
plain-object mutations call Lambda’s existing shape functions directly. Acceptance
requires unchanged benchmark sources/oracles,
balanced release comparisons with pinned native MIR and self-reported times,
all 60 workloads, cold compilation costs, forced-GC checks and baseline gates.
Measured result: CD **369.3 → 244.0 ms** (33.9% faster, **6.84× Node**),
Richards **86.5 → 80.5 ms** (6.9% faster), with NBody workload and cold time
unchanged. CD frame entries fall 56% and root reloads 35%. All 60 workload
oracles, 64 focused/forced-GC tests, 6,484 Lambda baseline tests and the clean
40,261-test Test262 baseline pass. Longer paired checks still show small
regressions in map iteration (0.81%), nqueens (0.86%) and Storage (1.95%);
performance acceptance remains open for those rows.
Detailed scope and evidence: [guarded numeric regions](../impl/JS_MVP_Lmd_Numeric_Regions.md).

## 26. CD allocation and shape stability

Reduce tree-field cache misses and repeated constructor retyping. Reuse Lambda
shape transitions to retain nullable Map lanes for proven literal-null fields,
and retain completed allocation shapes per inlined construction site. Invalidate
site templates when a class learns another nullable link. Preserve constructor
parameter semantics, object identity, immutable shared shapes and precise roots
(**D3.4.3v5–D3.4.5**, **D5.2–D5.3**, **D6.2.1**). Extend existing numeric-region
inlining for bounded allocation-free direct calls inside functions.

The disclosed internal helper is `mvp_lmd_class_widen_allocation`; it calls
Lambda's existing transition API. Confirm performance with unchanged CD and
control workloads, pinned native MIR and self-reported release times, then run
the focused/forced-GC and baseline gates. Detailed evidence:
[CD allocation and shape stability](../impl/JS_MVP_Lmd_CD_Allocation.md).

**Measured:** CD **243.2 → 195.1 ms** (19.8% faster, **5.47× Node**, 8.4% slower
than typed Lambda), and Richards **80.6 → 75.2 ms** (6.7% faster). Tree-key
helper fallbacks fall 99.5% and root reloads 33.8%. All 60 workload oracles and
65 focused/forced-GC tests pass. Bounce retains a confirmed **0.92%** slowdown;
its cause remains open despite unchanged hot-loop MIR. Lambda/input passes
**6,485/6,485** and Test262 **40,261/40,261** with zero retries. Full measurements
and limitations are in the linked record.

## 27. CD mutation and construction follow-up

Retain bounded construction-site layout variants for recursive records with
alternating parameter types, using the existing shape transitions and allocation
epochs (**D3.4.3v5**, **D3.4.5**). Reuse `scalar_constructor_plan` for concrete
numeric layouts and initialize fresh instances with packed stores. Share receiver
checks within proven pure numeric regions, reuse their guarded method targets,
and keep scalar results native. Restrict small allocating-method inlining to
ordinary function bodies with the cheaper construction path. Reuse `str_cmp` for
ASCII string comparisons.

Retain precise roots and scalar ownership (**D5.2–D5.3**), constructor identity
guards and original argument evaluation order (**D6.2.1**). No new helper function
is added. Validate with paired release MIR measurements, unchanged workloads,
cold process costs and regression controls. The direct null/undefined cache-path
experiment was removed after its short screen failed to show a benefit.
Implementation and evidence:
[CD native construction and calls](../impl/JS_MVP_Lmd_CD_Native_Construction.md).

**Measured:** CD **195.8 → 171.1 ms** (12.6% faster, **4.78× Node**, 6.0% faster
than the typed Lambda port). Helper calls fall 14.6% and frame entries 28.6%.
Richards and Bounce are unchanged in longer paired checks. All 60 workload
oracles, 65 focused/forced-GC tests, 6,485 Lambda/input tests and 40,261 Test262
tests pass; Test262 needs zero retries. At this snapshot, performance acceptance remains open:
`object_delete` is **3.7% slower** in its 60-run confirmation, and CD cold process
time increases **1.1%**. The previous round's Bounce regression remains a
separate unresolved comparison against its older control. See §28 for the follow-up.

## 28. Deletion and compilation regression fixes

Predict bounded deletion/reinsertion layouts from known literal shapes before
execution, then reuse guarded packed-field reads and the shared generic fallback.
Use Lambda's existing shape construction (**D3.4.3v5**, **D3.4.5**); no mutable
per-site cache is added (**D8.4.1v2**). The new compiler helper is
`plan_deleted_shapes`, reusing the existing field planner. Preserve precise
roots, scalar ownership and property order (**D5.2–D5.3**).

Bound allocating-method expansion per caller and inline one allocating target
per site; retain captured-callee identity guards and fallback calls for other
classes (**D6.2.1**). This removes unused polymorphic expansion from CD's
recursive traversal. Validate unchanged workloads with pinned release MIR,
self-reported execution times, cold process measurements, identical-control
peers, forced GC and baseline gates. Evidence and the discarded cache experiment:
[regression fixes](../impl/JS_MVP_Lmd_Regression_Closure.md).

For Bounce, extract Lambda's existing compatible numeric store from `fn_map_set`
as `map_field_store_int_as_float`. Both engines reuse it for resolved double
fields receiving integers (**D3.4.5**); MVP avoids the repeated property lookup
that regressed Richards. Other value types retain their normal transition.

The broad check also confirmed a Map lookup regression with unchanged MIR and
relocated native helpers. Factor canonicalization and allocating Map updates
into the shared `map_call_owned` helper, reusing existing storage and precise
roots (**D5.2–D5.3**), so reads and immediate writes need a smaller native frame.

**Measured follow-up:** deletion is **25.9% faster**, Bounce **25.8% faster**
against its older control, and CD execution **12.8% faster** against the
pre-regression release. Richards improves **2.2%**; Map lookup returns to parity
in its 90-round confirmation. All 60 workload oracles pass with no statistically
confirmed slowdown; three shared Lambda checks also pass. Cold CD process time
is **3.346 → 3.314 s**, with a wide ratio interval (**0.970–1.041**), so a small
startup difference remains unresolved. Host contention and interrupted runs are
recorded in the linked implementation report. Validation passes: **67** MVP
tests normally and under forced GC, **6,487/6,487** Lambda/input tests on the
full rerun after a LaTeX corpus timeout, and **40,261/40,261** Test262 cases
with zero retries. The restored release matches the measured binary.
The 60-workload snapshot and separate confirmations are published as
[MVP_Result8](../../test/benchmark/js_mvp_lmd/MVP_Result8.md).

## 29. Benchmark expansion and script self-timing

**Implemented:** add **16 standard benchmarks**: the original eight proposed
targets plus eight more. Release validation executes **76 workloads**, covering
**59 of 71 standard benchmarks**, plus the existing 17 microbenchmarks. The
aggregate-gate limitation below still prevents full phase acceptance.

| Group | Targets | Main additions |
|---|---|---|
| AWFY | `deltablue`, `json`, `havlak` | Public static fields, UTF-16 substring; Havlak admission/runtime audit |
| Text | `text_search`, `three_way_merge`, `log_pipeline` | Array callbacks, literal splitting, string search/slicing, primitive conversions |
| Julia | `parse_integers`, `iteration_pi_sum`, `matrix_statistics`, `formatted_output` | Correct warmup/timing boundaries; actual synchronous formatted writes |
| BENG | `fasta`, `revcomp`, `knucleotide` | Arguments, UTF-8 file reads/output, reverse, Map-entry spread, comparator sort and formatting |
| JetStream | `cube3d`, `navier_stokes`, `splay` | Ordinary constructors/receivers, prototype data properties, named array properties, numeric builtins |

Expose script `performance.now()` through Lambda's existing clock, with a
per-execution origin and fractional milliseconds. Preserve each benchmark's
setup, warmup, timed work and oracle; accept exactly one workload timing record.
Keep CLI whole-entry and process times separate. Host I/O uses an explicitly
supplied benchmark environment within the module boundary (**D7.3.1–D7.3.4**).

Reuse Lambda functions first, adding a semantic option only where needed.
Keep objects on nominal Lambda Maps and shared shapes (**D2.6.9v3**,
**D3.4.3v5**, **D3.4.5**), and calls/closures on the existing precisely traced
Function machinery (**D5.3**, **D6.2.2v2–D6.2.4**). Ordinary prototype data
writes must remain visible to existing instances. Use compile-predicted guards
and shared fallback dispatch (**D8.4.1v2**). No VMap, `__proto__`, prototype-link
reassignment, accessors, proxies or general Node compatibility is added.

Acceptance requires all 16 correctness/timing contracts and paired release,
pinned-MIR checks of the prior 60 workloads, with no confirmed regression.
Dependencies, proposed helpers, open design checks and validation commands:
[implementation plan](../impl/JS_MVP_Lmd_Benchmark_Expansion.md).


**Latest validation (2026-10-10):** all **76/76** workload oracles pass, including
exact output and script timing for the new 16. Normal and forced-GC focused
checks pass **77/77** each; the shared LambdaJS Test262 baseline passes
**40,261/40,261** with zero retries. The prior-60 release, pinned-MIR comparison
has no statistically confirmed slowdown against Result8; its descriptive
execution-time geometric mean is **1.3% lower**. Small differences remain
possible within the intervals. See the [final evidence](../../temp/mvp_expansion/final-stage25.md).

The Lambda baseline remains **6,534/6,535**: `edit_view_only` also fails on the
unmodified committed build. Windows clock validation remains open. The
implementation plan records these limits and the pre-existing class-cache
discrepancy under **D8.4.1v2**. Results3–8 are preserved; the published population
remains Result8's 60 until aggregate acceptance is resolved.

## 30. Library completion and generator-based benchmarks

**Feature implementation complete; performance gate open (2026-10-10):** adds **11 standard benchmarks** in the following order.
The target is **70 of 71 standard benchmarks**, plus 17 microbenchmarks:
**87 workloads** after acceptance. `jetstream/hashmap` remains excluded, together
with mutable prototype links and accessor execution. All eleven new canonical
oracles pass on the final frozen release; phase acceptance still requires
resolving the remaining `log_pipeline` regression.

| Round | Targets | Required additions |
|---|---|---|
| 1. Core library completion | `jetstream/raytrace3d`, `text/prettier_ast`, `beng/pidigits` | Basic Date and array-to-string coercion; JSON parse/stringify; default/rest parameters; Array flat/reduce/some/includes; BigInt literals, exact arithmetic/comparison and string conversion |
| 2. RegExp and array editing | `beng/regexredux`, `text/fast_diff`, `text/hyphen` | RegExp literals/test/match/replace, including negative lookahead; Array splice/concat/unshift; Function call/bind; for-in, globalThis, basic Object prototype methods and default-locale lowercase |
| 3. Reflection and bounded data descriptors | `text/microdiff` | Optional chaining, Function apply, Array.isArray, numeric classification, prototype reflection and constructor names, Date/RegExp coercion, and data-only Object.defineProperty |
| 4. Generators, iterators and exception handling | `text/jq_mix`, `text/jq_records`, `text/jq_bf`, `text/jq_tree` | Generator functions/methods, yield/yield*, general iterators and Symbol.iterator, iterator closing, try/catch/finally, destructuring and call/object spread; ordinary object methods and named public instance fields; Object.create/assign, Set, and remaining collection/string builtins |

Reuse Lambda implementations first, adding options for small semantic differences
(**D1.3v3**). BigInt should use the existing core `bigint_*` operations. JSON,
arrays, UTF traversal, regex matching and clocks should reuse applicable shared
kernels with explicit JS semantics. Date uses wall-clock milliseconds; existing
`performance.now()` keeps its monotonic execution-relative timing contract.
Disclose any new helper before implementation.

Objects, class inheritance and collection storage retain Lambda Maps and shared
shapes (**D2.6.9v3**, **D3.4.3v5**, **D3.4.5**). `Object.create(p)` establishes a
prototype link at creation; subsequent link reassignment remains excluded.
Generator suspension should build on existing Function/closure machinery with
precisely traced saved state; JS captures remain mutable (**D5.3**,
**D6.2.2v2–D6.2.4**). Exceptions and iterator cleanup use explicit completions
through every frame (**D1.4v4**). Preserve guarded specialization and shared
fallback dispatch (**D8.4.1v2**).

**Scope limits:** no `__proto__`, prototype replacement/reassignment, getter or
setter execution, proxies, VMap for plain objects, general descriptor support, or async
execution. The synchronous hyphen workload does not require Promise scheduling.
The jq tracing getter is admitted with an accessor flag on its ordinary nominal
shape; presence checks can observe the declaration. Reads and writes explicitly
reject accessor execution. Focused checks cover declaration, enumeration and
rejection; all four canonical jq workloads pass. Report any
executed accessor dependency before expanding scope (**D3.4.3v5**, **D8.4.1v2**).

**Acceptance:** preserve canonical sources, inputs, warmup, timed work and exact
oracles for all 11 targets, using existing script self-timing. Freeze the §29
release as control and check all prior **76 workloads** with release, pinned MIR,
paired self-reported times and an identical-control peer; confirm suspected
regressions with longer runs. Require focused normal/forced-GC checks and shared
Lambda/Test262 gates. Keep §29's existing failures and platform limits explicit;
new coverage and performance claims require complete evidence.

Implementation sequence, helper inventory and validation status:
[implementation plan](../impl/JS_MVP_Lmd_Library_Generators.md).

**Phase 30 baseline (round4m):** 100/100 focused tests pass normally and with forced GC;
Test262 passes 40261/40261 without unstable batches. The Lambda aggregate is
6590/6591, retaining only the reproduced §29 `edit_view_only` failure. All eleven
new canonical targets pass their output/backend checks, establishing functional
coverage of **87 workloads**. The seven-repeat prior-76 screen passes every
oracle, with effectively unchanged aggregate timing (geometric mean **0.9992×**
control). Longer confirmation clears the other suspected slowdowns within
measurement uncertainty, but `log_pipeline` remains **1.3% slower** against the
control (also positive against the identical-control peer). Its cause is still
open, so the strict no-regression gate is not passed. Generator stack overhead
also remains a material performance limit for the new jq workloads.

### 30.1 Library workload tuning

**Implemented and measured (2026-10-10):** tune `havlak`, `regexredux`,
`text_search` and the four canonical jq workloads. Preserve numeric element
keys until an actual named-property fallback. Reuse completed class
shapes for constructors with simple parameters whose conditional and nested
initializers cannot expose the receiver (**D3.4.3v5**, **D3.4.5**); expanded
admission adds no new per-site cache cells (**D8.4.1v2**).

RegExp operations reuse one prepared input and the existing matcher scratch
storage. Lambda's shared activation pool batches discard/reclaim operations
and retains one idle stack for immediate reuse (**D1.3v3**, **D5.1.1v3**).
Precise roots, the 16-activation warm window and the 32-stack pool bound stay
in force; retained pages add at most one 3 MiB mapping to resident memory.
Frozen-release, pinned-MIR comparisons show **11.2% less time for Havlak**,
**12.1x faster regexredux**, and **22.2% less time for text_search** over seven
measured pairs. All **87 benchmark output contracts** pass. Longer checks
found no new slowdown against both control copies in the 83 non-jq workloads.
The four jq comparisons observe **17.6–24.8% less time**, but are single
observations; another test briefly overlapped the `jq_mix` candidate.
MVP 100/100 and activation 10/10 pass normally and under forced GC; Test262
passes 40261/40261. Lambda is 6607/6608, retaining the known `edit_view_only`
failure; §30's older `log_pipeline` issue remains
separate. Evidence and limits are
recorded in the [implementation plan](../impl/JS_MVP_Lmd_Library_Generators.md#6-library-workload-tuning).
