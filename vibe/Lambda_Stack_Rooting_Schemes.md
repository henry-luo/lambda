# Stack and GC-rooting schemes in JavaScript runtimes and CRuby

**Status:** comparative design report

**Reviewed:** 2026-07-16

**Lambda tree reviewed:** current working tree at the time of writing

**Related Lambda runtime review:** [Lambda_Stack_MIR.md](./Lambda_Stack_MIR.md)

**Related LambdaJS MIR review:** [Lambda_Stack_JS_MIR.md](./Lambda_Stack_JS_MIR.md)

**Related precise-only migration plan:** [Lambda_Design_Stack_Frame2.md](./Lambda_Design_Stack_Frame2.md)

This report compares how V8, SpiderMonkey, JavaScriptCore, QuickJS, and CRuby
keep live language values visible to memory management. It then compares those
schemes with Lambda's current mixed precise/conservative implementation.

The central conclusion is:

> Production runtimes normally choose one primary contract for each execution
> tier: exact stack maps, explicit rooted handles, a canonical VM value stack,
> conservative native-stack discovery, or reference-count ownership. They do
> not normally publish every live binding after every instruction while also
> scanning the same native stack conservatively as the intended steady state.

Lambda's current state is useful as a migration bridge, but it pays for both
new precise publication and the retained old conservative scan. The closest
long-term models for a moving or precise Lambda collector are V8's safepoint
maps and SpiderMonkey's exact root discipline. CRuby and JavaScriptCore explain
why the old Lambda design was simple, but they do not provide a route to
removing conservative native-stack scanning. QuickJS is a fundamentally
different ownership design rather than a rooting refinement.

---

## 1. Terminology and correctness invariant

### 1.1 Root

A root is a live reference from outside the traced heap. Typical root sources
include:

- globals and runtime singletons;
- language VM stack slots;
- values in JIT registers or spill slots;
- native helper locals;
- persistent handles owned by an embedder;
- pending jobs, promises, callbacks, and module records.

Once a root object is marked, the collector follows its typed heap fields.
Rooting the owner therefore also protects children already linked into that
owner. A newly allocated child not yet linked into a rooted owner may still
need a temporary root.

### 1.2 Precise or exact root discovery

The runtime knows which locations contain references and their representation.
Examples are:

- a bitmap identifying tagged spill slots at one machine-code PC;
- a linked list of `Rooted<T>` objects;
- a handle stack containing only GC references;
- a VM stack whose active cells are all valid language values.

Precise discovery does not necessarily mean exact liveness. A VM may scan all
active value slots even when some contain dead values. It is still more precise
than interpreting arbitrary machine words as possible pointers.

### 1.3 Conservative root discovery

The runtime scans raw machine words and tests whether each word could point to
a live heap allocation. A false match retains an otherwise dead object. A live
reference can also be missed if its representation is transformed, optimized
away, kept in an uncaptured register, or points into an object in a form the
candidate test does not recognize.

Conservative roots also complicate moving collection. If the collector cannot
prove that a candidate word is a real, writable reference, it normally pins the
object instead of updating the word.

### 1.4 Safepoint and stack map

A safepoint is a machine-code location where collection is allowed and the
runtime can recover a valid root set. A stack map associates that location with
the registers and frame slots containing references.

The essential compiler invariant is:

```text
at every may-GC PC
    every live managed value is in a mapped register/slot
    or is reachable from another explicit root
```

Code between safepoints does not need continuously published root state if GC
cannot begin there.

### 1.5 Write barriers are separate

Root discovery answers whether an object is reachable from outside the heap.
A generational or incremental write barrier records relevant edges created
inside the heap. A correct runtime generally needs both:

```text
root protocol     protects live external references
write barrier     protects old-to-young or incremental heap edges
```

---

## 2. Comparison summary

| Runtime | Primary managed-value root scheme | Optimized/JIT code | Native helper/API scheme | Conservative native scan | Moving/compacting consequence |
|---|---|---|---|---|---|
| V8 | Typed JS frames, handle stacks, global handles | Safepoint tables identify tagged registers and spill slots | `HandleScope`, local handles, persistent handles | Not the primary V8 JS-heap root contract; embedder/cppgc modes are a separate caveat | Exact roots can be updated after movement |
| SpiderMonkey | Exact stack roots and typed heap tracing | JIT frame/safepoint and bailout metadata | `Rooted<T>`, `Handle<T>`, `MutableHandle<T>`, persistent roots | No for the SpiderMonkey GC root contract | Exact roots are updated during compacting GC |
| JavaScriptCore | Typed heap tracing plus conservative machine-stack/register roots | JIT values may remain in native frames because the stack is scanned | Protected/persistent references and runtime-owned structures | **Yes** | Conservative candidates favor a non-moving heap and can retain false positives |
| QuickJS | Reference counts on owned `JSValue`s; cycle removal for unreachable cycles | Interpreter values participate in ownership/refcounts | `JS_DupValue()` / `JS_FreeValue()` ownership rules | No tracing root scan is needed for ordinary lifetime | Objects are not moved; cost shifts to retain/release operations |
| CRuby | Ruby VM value stack and control-frame fields, plus registered globals | JITs reuse CRuby frames; native temporaries remain covered conservatively | `VALUE` locals, `RB_GC_GUARD`, registered addresses | **Yes** | Conservative roots are marked/pinned; false retention is possible |
| Lambda current | Registered roots/ranges plus the side-root stack | MIR-Direct and LambdaJS publish side-root slots | Partial `LambdaRootGuard`; many helpers still rely on native visibility | **Yes, still active** | Non-moving heap avoids relocation, but duplicate work and false retention remain |

The generated-code traffic differs substantially:

| Scheme | Root work during normal execution |
|---|---|
| Stack maps | Record metadata at compile time; little or no root-store traffic at run time beyond necessary spills |
| Rooted handles | Push/pop handles at scoped native API boundaries |
| Canonical VM stack | Stack writes already required by interpreter/call semantics double as root publication |
| Conservative scan | Little publication work; collection scans raw stack/register state |
| Reference counting | Retain/release work at ownership changes |
| Current LambdaJS | Side-root stores around calls and after many register writes, plus the retained conservative scan |

---

## 3. V8: handles plus precise JIT safepoint maps

### 3.1 Root families

V8 exposes managed objects to C++ through handles rather than encouraging raw
heap pointers to remain live across allocation:

- a local handle lives in V8's handle stack;
- a `HandleScope` establishes a scoped watermark and releases its local
  handles on destruction;
- an `EscapableHandleScope` moves one return handle into the parent scope;
- persistent/global handles survive beyond one native call and must be reset or
  made weak according to their ownership policy.

The handle stack is separate from the C++ call stack. The C++ scope object is
stack allocated, but the GC-visible handle cells are in V8-managed storage.
Because handles are exact locations, a moving collector can rewrite them.

V8's embedding documentation describes this contract directly in
[Handles and garbage collection](https://v8.dev/docs/embed#handles-and-garbage-collection).

### 3.2 Generated frames

V8 has several frame shapes and compilation tiers. The root visitor understands
the fixed tagged fields of each frame type. Optimized code additionally emits a
safepoint table keyed by machine-code PC.

Conceptually:

```text
optimized frame at return PC P
    fixed frame fields              described by frame type
    tagged spill slot bitmap        described by safepoint(P)
    tagged saved-register bitmap    described by safepoint(P)
    unboxed integers/doubles         omitted from the root map
```

The current safepoint table builder constructs bit vectors whose set bits mean
tagged stack slots and can also record tagged registers. The frame iterator
uses those bits instead of treating every frame word as a possible object.
See V8's
[safepoint-table definition](https://chromium.googlesource.com/v8/v8.git/+/refs/heads/lkgr/src/codegen/safepoint-table.h)
and [optimized-frame root iteration](https://chromium.googlesource.com/v8/v8/+/7cb6c81/src/frames.cc).

Collection is restricted to states for which the generated metadata is valid.
This lets the compiler keep values in registers between safepoints without
publishing them after every instruction.

### 3.3 Consequences

Advantages:

- no false roots from arbitrary JIT frame words;
- tagged and unboxed values are distinguished;
- compacting GC can update actual references;
- run-time publication traffic is concentrated at real spills, calls, and
  frame transitions;
- deoptimization metadata can share knowledge about value locations.

Costs:

- every may-GC return PC needs correct metadata;
- register allocation, spilling, exception exits, OSR, and deoptimization must
  preserve the safepoint contract;
- native C++ code must obey handle rules;
- verification infrastructure is required because one missing map entry can
  become a use-after-free.

V8 describes its collector as accurate in its
[public documentation](https://v8.dev/docs). Some V8 embedder and `cppgc`
configurations also support conservative embedder-stack treatment; that is not
the primary root contract for optimized JavaScript values described here.

### 3.4 Lesson for Lambda

V8 is evidence that precise JIT rooting does not require a root store after
every instruction. It requires:

```text
may-GC points known to the compiler
    + liveness at those points
    + exact register/spill maps
    + a frame walker able to find the code and safepoint entry
```

This is the highest-performance but most compiler-intensive direction.

---

## 4. SpiderMonkey: explicit exact roots plus rooting hazard analysis

### 4.1 Native/API roots

SpiderMonkey requires C++ code that may cross a GC point to use exact rooting
wrappers. The important roles are:

- `Rooted<T>` owns a stack root location;
- `Handle<T>` is a non-owning parameter view of an already rooted value;
- `MutableHandle<T>` permits a callee to update a rooted location;
- persistent-root forms cover lifetimes that do not match one native scope.

Rooted locations are registered in runtime-maintained root chains. The
collector walks those chains; it does not guess which C++ stack words might be
GC pointers.

SpiderMonkey backs the API contract with a static rooting-hazard analysis that
tracks which calls can GC and reports live unrooted GC pointers. The official
[GC overview](https://firefox-source-docs.mozilla.org/js/gc.html) explicitly
describes the collector as precise and explains that C++ wrapper classes and
static analysis provide stack-root knowledge. The analysis workflow is
documented in
[Running the Rooting Hazard Analysis](https://firefox-source-docs.mozilla.org/js/HazardAnalysis/running.html).

### 4.2 Interpreter and JIT frames

SpiderMonkey's lower tiers have well-defined VM/baseline frame layouts. Its
optimizing tier lowers MIR to LIR, performs register allocation, and generates
native code. JIT metadata describes recoverable values at calls, bailouts, and
other safe exits. A bailout reconstructs the baseline/interpreter-visible
frame state from side metadata.

The same general location knowledge serves three related needs:

```text
GC safepoint       find all live GC references
bailout            reconstruct baseline values
stack walking      identify JIT frame kind and boundaries
```

Mozilla's [SpiderMonkey architecture overview](https://firefox-source-docs.mozilla.org/js/)
documents the interpreter, baseline tiers, MIR/LIR optimizing tier, register
allocation, and bailout reconstruction.

### 4.3 Consequences

Advantages:

- no false retention from arbitrary native words;
- compacting GC can update exact locations;
- native API root ownership is explicit in types;
- the `Can GC` call graph and hazard checker make audits scalable;
- handles avoid copying a rooted value into every callee's own root slot.

Costs:

- native code must use the wrapper types consistently;
- generated JIT metadata and exit reconstruction must be exact;
- integration with unannotated third-party C/C++ code requires explicit
  boundary wrappers;
- static analysis and stress-GC testing become part of the correctness model.

### 4.4 Lesson for Lambda

SpiderMonkey is the closest model for Lambda native helpers. A complete Lambda
equivalent would need:

```text
LambdaRooted<T>          owns an exact root location
LambdaHandle<T>          borrows an already rooted value
LambdaMutableHandle<T>   returns or updates through that location
may-GC annotations       describe helper call behavior
root hazard analysis     rejects unrooted live values across may-GC calls
```

`LambdaRootGuard` is a useful scoped primitive, but current limited adoption is
not yet an API-wide rooted-handle discipline.

---

## 5. JavaScriptCore: precise heap tracing with conservative stack roots

### 5.1 Root discovery

JavaScriptCore precisely understands the layout of GC heap objects, but treats
native stack/register contents conservatively. Its `ConservativeRoots` logic
gathers machine contexts and tests candidate words against live allocations.

This permits interpreter and JIT code to leave possible `JSCell*` values in
ordinary native registers and spill locations without producing exact stack
maps for every collection point.

The WebKit article
[Understanding Garbage Collection in JavaScriptCore From Scratch](https://webkit.org/blog/12967/understanding-gc-in-jsc-from-scratch/)
explicitly describes conservative stack scanning. Current WebKit heap source
still contains a dedicated `Conservative Scan` root phase in
[`Heap.cpp`](https://chromium.googlesource.com/external/github.com/WebKit/webkit/+/d08c15c73b5124b56db6f1123bbb23cb55ad9535/Source/JavaScriptCore/heap/Heap.cpp).

### 5.2 Why it is attractive to a JIT

The compiler can optimize normal machine values without maintaining exact GC
maps for every representation and register allocation decision:

```text
machine word resembles a live JSCell address
    -> retain the cell
otherwise
    -> ignore the word
```

JSC still needs known roots for runtime-owned structures, globals, protected
embedder values, and heap edges. It also needs write barriers for generational
and concurrent/incremental marking. Conservative scanning is only the native
frame/root-discovery part of the design.

WebKit's discussion of
[Speculation in JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/)
states the design tradeoff directly: conservative stack scanning and a
non-moving object model reduce compiler integration complexity.

### 5.3 Consequences

Advantages:

- little generated root-publication traffic;
- simple interaction with aggressively optimized JIT frames;
- native helper locals can remain ordinary C++ pointers;
- no separate per-value root metadata is required for those native frames.

Costs:

- false positives retain dead objects;
- collection must inspect a potentially large native-stack region;
- candidate validation needs efficient heap metadata;
- object movement is difficult because an apparent pointer cannot safely be
  rewritten as though it were definitely a pointer;
- optimizer/register-capture assumptions are part of correctness.

### 5.4 Lesson for Lambda

This is the closest external analogue to Lambda's historical rooting model.
It is a defensible steady-state design only if Lambda accepts:

- a non-moving heap;
- false retention;
- ongoing native-stack scan cost;
- conservative correctness constraints for all MIR and native helpers.

It does not help achieve the goal in `Lambda_Design_Stack_Frame2.md`, because
the conservative scan is the scheme rather than a migration fallback.

---

## 6. QuickJS: reference ownership rather than tracing roots

QuickJS uses reference counting for ordinary lifetime management and a cycle
removal pass for unreachable reference cycles. Its public description lists
"reference counting ... with cycle removal" as a core property:
[QuickJS documentation](https://bellard.org/quickjs/quickjs.html).

The C API therefore uses ownership rules rather than a tracing root protocol:

```text
borrowed JSValue parameter
    callee may inspect it but does not free the caller's ownership

stored or independently retained JSValue
    JS_DupValue(value)

owned value no longer needed
    JS_FreeValue(value)

newly returned JSValue
    caller receives ownership
```

A local variable does not survive because a collector found its bits on the
native stack. It survives because some owner has an outstanding reference
count. Cycles require the separate cycle-removal algorithm because their
counts do not naturally reach zero.

Consequences:

- object destruction is usually deterministic;
- no conservative native-stack root scan or JIT stack map is required for
  ordinary ownership;
- objects do not move;
- retain/release operations add execution traffic at ownership boundaries;
- missing duplication creates premature destruction, while missing release
  creates a leak;
- cycle collection and weak-reference semantics require additional machinery.

QuickJS is not a drop-in answer for Lambda rooting. Adopting it would mean
changing the object lifetime model, container stores, assignments, closures,
calls, returns, exceptions, and async ownership throughout the runtime.

---

## 7. CRuby: canonical VM stack plus conservative machine stack

### 7.1 Ruby VM frames

CRuby maintains a Ruby VM stack separate from the native C stack. The VM stack
contains Ruby `VALUE` cells and `rb_control_frame_t` records. A control frame
tracks state such as:

```text
pc          current Ruby instruction
sp          top of active Ruby VALUE stack
iseq        bytecode/function body
self        receiver
ep          environment/local-variable base
block data  current block or closure state
```

Writes to the Ruby operand/local stack are required for interpreter semantics,
method calls, blocks, exception handling, and deoptimization. Those same
`VALUE` cells form a structured root source; Ruby is not maintaining an
otherwise-useless duplicate root stack for ordinary interpreted values.

Current CRuby's `rb_execution_context_mark()` walks active VM-stack values and
control-frame fields. See the generated source documentation for
[`vm.c`](https://docs.ruby-lang.org/capi/en/master/de/de9/vm_8c_source.html).

### 7.2 Conservative native roots

CRuby also saves machine registers with `setjmp` and scans the native machine
stack. A word that looks like a Ruby heap object is marked. The public
`RB_GC_GUARD` documentation explicitly says that Ruby scans the C-level machine
stack conservatively:
[CRuby memory API](https://docs.ruby-lang.org/capi/en/master/dc/d18/memory_8h.html).

The root equation is approximately:

```text
CRuby roots
    = active Ruby VM VALUE stack and frame fields
    + registered global addresses and VM-owned structures
    + escaped environment/closure objects
    + conservative native stack and saved registers
```

`RB_GC_GUARD(value)` prevents the C compiler from making a logically live
`VALUE` disappear before the last raw-pointer use. Long-lived extension values
can instead be registered explicitly. Heap field updates use CRuby write
barriers independently of this root protocol.

CRuby can compact objects, but conservative locations are problematic for
relocation and are therefore marked/pinned rather than blindly rewritten.

### 7.3 CRuby JITs, including the MIR research prototype

CRuby JITs retain the normal Ruby control-frame and VM-stack contract so that
the interpreter, exceptions, backtraces, blocks, and deoptimization continue
to work. JIT-only temporaries in native registers or spills remain protected
by the conservative machine scan unless the JIT has separately materialized
them in VM-visible slots.

Vladimir Makarov's experimental SIR+MIR CRuby branch followed this design:

```text
YARV bytecode
    -> specialized SIR basic blocks
    -> generated C
    -> C2MIR/MIR native code

logical calls       remain rb_control_frame_t records
Ruby values         remain VALUE stack cells
JIT cache locals    may live in native registers/spills
GC                  scans VM roots plus native machine context
```

It did not introduce a Lambda-like precise side-root frame. Makarov describes
the SIR and MIR prototype in
[How I developed a faster Ruby interpreter](https://developers.redhat.com/articles/2022/11/22/how-i-developed-faster-ruby-interpreter).
This research branch is not the current official Ruby JIT; Ruby 4.0 ships YJIT
and an experimental ZJIT instead.

### 7.4 Consequences and Lambda lesson

CRuby gets low publication overhead because the Ruby VM stack is already
semantically necessary. The native scan fills the remaining gap for C and JIT
temporaries.

Lambda MIR-Direct has no equivalent canonical language operand stack. MIR
virtual registers are the primary execution representation. Adding a second
slot for every register therefore creates new traffic that CRuby does not pay.

Copying CRuby's design would mean retaining Lambda's conservative scan as a
permanent component. Copying only its VM-stack part would require introducing
a canonical Lambda activation/value-stack representation and keeping it valid
at GC points.

---

## 8. Lambda's current mixed scheme

### 8.1 Current collector roots

The current collector root equation is:

```text
Lambda roots
    = registered individual root slots
    + registered root ranges
    + active side-root interval [side_root_base, side_root_top)
    + optional explicit extra roots
    + conservative native-stack scan [SP, stack_base)
```

The live implementation can be followed from:

- [`Context.side_root_*`](../lambda/lambda.h);
- [`heap_gc_collect()`](../lambda/lambda-mem.cpp);
- [`LambdaRootGuard`](../lambda/lambda.hpp);
- core MIR frame emission in [`transpile-mir.cpp`](../lambda/transpile-mir.cpp);
- LambdaJS root binding and frame emission in
  [`js_mir_hashmap_scope_utils.cpp`](../lambda/js/js_mir_hashmap_scope_utils.cpp).

`heap_gc_collect()` still calls `setjmp()` to materialize register state, finds
the native stack bounds, computes the active side-root count, and passes both
root regions into collection.

### 8.2 What has changed and what remains

| Mechanism | Current status |
|---|---|
| Heap-backed linked JIT root frames | Removed from current MIR frame implementation |
| Dense side-root stack | Added and used by MIR-Direct and LambdaJS |
| Scoped side-number watermark | Added for wide scalar payload lifetime |
| Registered runtime slots/ranges | Retained for stable and persistent roots |
| Conservative native-stack scan | Retained and executed on every collection |
| Native-helper exact handles | Partial; `LambdaRootGuard` is not runtime-wide |
| C2MIR precise root contract | Missing |

Thus Lambda currently has both the old and new ordinary-local protection:

```text
generated MIR value
    -> explicitly copied into side-root storage
    -> may also remain in a native register/spill

collection
    -> scans side-root storage
    -> scans the native register/stack representation again
```

Duplicate discovery is safe for marking, but it does not provide the intended
steady-state payoff of precise rooting. The runtime pays generated root-store
cost, root-region scan cost, native-stack scan cost, and false-retention cost.

### 8.3 LambdaJS's especially broad publication

The current LambdaJS implementation does more than reserve a frame and publish
values at actual allocation points. It includes mechanisms for:

- assigning root slots to heap-capable variables;
- republishing root bindings around calls;
- rooting call operands and results;
- scanning emitted instructions for writes to rooted registers;
- storing a modified rooted register back into its slot;
- routing returns through common side-root/number cleanup.

The detailed emitted-MIR counts and examples are in
`Lambda_Stack_JS_MIR.md`. The key comparison with other engines is:

```text
V8/SpiderMonkey     metadata and spills at safepoints
JSC/CRuby           conservative discovery, little explicit publication
QuickJS             ownership changes
LambdaJS current    broad explicit publication plus conservative discovery
```

No surveyed runtime supports eliminating conservative scanning merely by
publishing lexical bindings. Expression temporaries, helper locals, exception
paths, callbacks, async state, C2MIR values, and returned values must all be
covered by one complete contract.

---

## 9. What each design optimizes

| Design | Optimizes for | Accepts as cost |
|---|---|---|
| V8 stack maps | JIT throughput, moving GC, exact retention | compiler and metadata complexity |
| SpiderMonkey exact roots | strong native API correctness and compacting GC | wrapper discipline and hazard analysis |
| JavaScriptCore conservative stack | compiler simplicity and easy native/JIT integration | false retention, scan cost, non-moving constraints |
| QuickJS reference counting | small implementation and deterministic destruction | retain/release traffic and cycle removal |
| CRuby VM stack + conservative C stack | compatibility with a stack VM and C extensions | conservative native dependence and pinning |
| Lambda current hybrid | migration safety while adding precise frames | duplicated execution and collection work |

There is no universally cheapest scheme. The root design has to match the
execution representation:

- stack VMs naturally expose a canonical value stack;
- optimizing register VMs naturally favor safepoint maps;
- C-extension-heavy runtimes often retain handles or conservative scanning;
- reference-counted runtimes move work to every ownership transition.

---

## 10. Recommended direction for Lambda

Proposed by GPT 5.6 Sol.

### 10.1 Do not model rooting as an after-every-instruction mirror

The required correctness boundary is a may-GC safepoint, not every MIR
instruction. Between two points at which collection cannot start, a managed
value can remain only in a MIR register.

The target invariant should be:

```text
for every may-GC instruction or call
    compute values live across the point
    identify those that can reference GC storage
    publish only that set, or describe its exact locations in metadata
```

This directly addresses the root-binding instruction blow-up documented in
`Lambda_Stack_JS_MIR.md`.

### 10.2 Choose among shadow slots, canonical slots, and true stack maps deliberately

Three viable MIR-Direct/LambdaJS targets exist. All three share the same two
prerequisites (register representation classes and a `may_gc`/`no_gc` helper
table, section 10.1); they differ in what happens after those exist.

#### Option A: liveness-pruned shadow root slots

```text
function compile time
    assign reusable root slots from live ranges

before may-GC call
    store only dirty live managed values

after call
    reload only when required by clobber/representation rules
```

This retains the current collector interface **and the current shadow-copy
representation**: registers stay primary, side-root slots mirror them. It is
the smallest delta from today's code, but it must build liveness analysis,
per-slot dirty tracking, and live-range slot-reuse allocation purely to reduce
synchronization traffic that Option B removes by construction. Section 12.7
argues Option B dominates this option.

#### Option B: canonical-slot (Henderson) frames

```text
rooted value's home IS its side-root slot
    write     -> one store to the home slot (register keeps a cached copy)
    read      -> register cache (valid because the heap does not move)
    safepoint -> nothing to emit; homes are current by construction
```

Same collector interface as Option A, but no synchronization machinery exists
at all — there is no second copy to keep in sync. Detailed in section 12.

#### Option C: PC-indexed MIR safepoint maps

```text
generated code object
    records frame layout and root bitmap per safepoint PC

collector
    walks MIR native frames
    locates code object from return PC
    marks mapped registers/spills
```

This removes explicit root-store traffic entirely, but requires MIR backend
support for final physical locations, a reliable frame walker, saved-register
recovery rules, unwind integration, and deoptimization/error-path coverage.
Detailed in section 13.

### 10.3 Use SpiderMonkey-style handles for native helpers

Generated-code precision alone cannot protect native C/C++ locals. Lambda needs
a runtime-wide contract with:

- scoped owned roots;
- borrowed handles that do not allocate a duplicate root for every callee;
- mutable/result handles;
- persistent handles for async and embedder lifetimes;
- `may_gc` and `no_gc` annotations or registries;
- a hazard audit for values live across transitive allocation.

### 10.4 Keep tier-specific compatibility explicit during migration

Until C2MIR and native helpers are migrated, the supported modes are:

```text
compatibility mode
    precise side roots + conservative native scan

precise-only verification mode
    precise roots only; force GC at every legal safepoint
```

The compatibility mode should be treated as transitional and measured. The
precise-only mode should initially be a correctness gate, not the default.

### 10.5 Required completion gates

Before removing the conservative scan globally:

1. classify every allocator and transitively may-GC helper;
2. cover MIR-Direct and LambdaJS safepoints with liveness-pruned roots or stack
   maps;
3. migrate native helpers to exact handles;
4. migrate or retire C2MIR;
5. cover exceptions, longjmp paths, async suspension, callbacks, module state,
   and cross-language calls;
6. add forced-GC and root-poisoning tests;
7. run a precise-only Test262, Node, Lambda, Jube, and Radiant gate;
8. only then remove `setjmp()` register flushing and `gc_scan_stack()` from the
   collector path.

---

## 11. Practical design comparison for the current regression

The current MIR growth is not an unavoidable price of precise GC. It is mainly
a property of the selected implementation strategy:

```text
design requirement
    all live managed values visible at GC points

current implementation approximation
    broadly mirror heap-capable registers and bindings into side slots
    around calls and after many instructions

result
    correctness coverage increases
    MIR instruction count increases sharply
    conservative scan still remains
```

The external runtime comparison suggests this optimization order:

1. stop root-binding publication after instructions that cannot lead to GC;
2. classify calls as `may_gc` versus `no_gc`;
3. publish only values live across each `may_gc` call;
4. coalesce root slots by non-overlapping live ranges — or adopt canonical
   slots (section 12), which removes steps 4 and 5 by construction;
5. avoid republishing unchanged values;
6. let rooted owners replace separately rooted children after ownership
   transfer;
7. introduce native handles and a precise-only stress mode;
8. evaluate true stack maps (section 13) only after a cheaper slot-based
   scheme is measured.

This follows the useful property shared by V8 and SpiderMonkey: precise state
is defined at safepoints. It avoids prematurely taking on their full frame-map
complexity while removing the largest source of redundant LambdaJS MIR.

---

## 12. Canonical-slot (Henderson) frames in detail

### 12.1 Concept

Henderson-style frames (after Fergus Henderson's *Accurate garbage collection
in an uncooperative environment*, ISMM 2002 — the scheme behind LLVM's
"shadow-stack" GC strategy) invert the current LambdaJS representation.
Instead of a rooted value living in a MIR register with a shadow copy in the
side-root frame that must be kept synchronized, **the side-root frame slot is
the value's canonical home**. Registers hold at most a cached copy.

- A write to a rooted variable is a single store to its home slot.
- A safepoint requires **no publication work at all** — homes are current by
  construction, because there is no second copy to fall out of sync.
- The collector is unchanged: it scans
  `[side_root_base, side_root_top)` exactly as it does today; canonical slots
  are simply what those words now *are*.

This is the "canonical VM stack" property that makes CRuby's rooting cheap
(section 7.4) — stack writes required by execution semantics double as root
publication — obtained without introducing a full interpreter-style operand
stack. Only rooted values are homed; everything else stays in plain MIR
registers.

### 12.2 Why it structurally removes the LambdaJS blow-up

The review findings in `Lambda_Stack_JS_MIR.md` map onto this scheme as
follows:

| Review finding | Shadow-copy cause | Under canonical slots |
|---|---|---|
| R3 duplicate stores (live-scope publication + operand rooting) | two independent store passes over the same registers | gone — no publication pass exists |
| R5 full republication before every helper | slots can be stale, so all are rewritten | gone — slots cannot be stale |
| R6 linear binding scans per emitted instruction | must discover which shadow slots a write invalidated | gone — the assignment *is* the slot store; no scan |
| R7 root policy duplicated across two call-emission paths | every call site must perform rooting | gone — call sites emit nothing; the policy lives at assignment lowering only |
| R8 rooting before initialization | slot store emitted separately from the defining write | gone — the defining store (including TDZ initialization) is the slot initialization |
| R1/R2 representation- and safepoint-blindness | — | still require the section 10.1 prerequisites; canonical slots decide *where* values live, not *which* values are rootable |

Options A's remaining machinery (liveness for correctness of slot reuse,
dirty tracking, republication suppression) has no counterpart here: it is
machinery for approximating a property this representation has structurally.

### 12.3 Lowering rules

1. **Homing predicate.** A value gets a home slot iff its representation class
   is `BOXED_ITEM` or `RAW_GC_POINTER` *and* it is (a) a lexical variable, or
   (b) a temporary live across at least one `may_gc` safepoint. Rule (b)
   needs only local lookahead ("is there a `may_gc` call between this
   definition and its last use"), not a whole-function dataflow pass; when in
   doubt, home it — the cost is one store, not a publication set.
2. **Parameters** are homed once in the prologue (one store each).
3. **Assignment** to a homed variable lowers to one store to the home slot,
   plus the ordinary register write (section 12.4). The current
   `jm_emit_root_updates()` post-instruction scan is deleted.
4. **Safepoints emit nothing.** `jm_root_live_scope_vars()`, the operand and
   result rooting in `jm_call_with_args()`, and the `MIR_CALL`/`MIR_JCALL`
   interception rooting in `jm_emit()` are all deleted.
5. **Un-homed values** (`NON_GC_SCALAR`, `RAW_NON_GC_POINTER`, or consumed
   before the next safepoint) stay in plain MIR registers with no GC
   interaction. In the review probe this removes the nine exception-flag
   slots, the args mark/pointer slots, the truthiness slot, and the immortal
   string-literal slot.
6. **Slot lifetime.** Slots are assigned per lexical scope and may be reused
   after scope exit. Within a scope a home is permanent; retention is
   frame-lifetime (section 12.6).
7. **Prologue/epilogue** are unchanged from the current frame design
   (late-sized prologue, unified return, number watermark save/restore,
   scalar re-homing, overflow block), plus the review's R9 frame elision for
   functions with no homed slots and no number-stack use.

### 12.4 Write-through register caching under the non-moving heap

The classic Henderson cost is that every read becomes a memory load. Lambda's
mark-sweep collector does not move objects, which removes most of that cost:

- Because objects never move, a register copy of a homed value **never needs
  reloading after a safepoint**. The cached register stays valid until the
  variable is reassigned.
- The scheme therefore degrades to **write-through**: keep the value in a
  register exactly as the pre-stack code did, and add one store to the home
  slot at each definition. Reads keep using the register.

Under write-through, the marginal mutator cost versus the *pre-stack* baseline
is one store per definition of a homed value, plus the already-existing
prologue/epilogue. That is the entire cost.

If a moving or compacting collector is ever adopted, write-through becomes
unsound (cached registers would hold stale pointers after movement); reads
would then need to reload from the home after each safepoint, or the backend
needs true stack maps (section 13). That adoption decision is the trigger
that re-opens the reads question — it should not be pre-paid now.

### 12.5 Estimated effect on the review probe

For `frameReview(a, b, callback)` from `Lambda_Stack_JS_MIR.md` §3, applying
the section 10.1 classification and the rules above, the homed values are
`a`, `b`, `callback`, `sum`, `holder`, and the `new_object` temporary (live
across the `js_create_data_property` safepoint) — roughly 6–8 slots against
the current 28. Home stores: three parameter stores, two TDZ initializations,
three `sum`/`holder` assignments, and a handful of temporary homes.

| Component | Shadow-copy (current) | Canonical-slot write-through |
|---|---:|---:|
| Root copy/store traffic | 161 pairs = 322 instructions | ~12–15 single stores |
| Prologue + overflow block | ~15 | ~15 (slot count 28 → ~8) |
| Unified return + scalar classifier | ~45 | ~45 (unchanged; separately reducible via return-mode inference, review R10) |
| **Total function size** | **440** | **~130** |

The residual ~2x over the 58-instruction pre-stack body is prologue/epilogue/
classifier, which the review showed is not the regression driver (R10) and
which R9 elision plus return-mode inference reduce further.

### 12.6 Costs, risks, and open questions

- **Frame-lifetime retention.** A home keeps its last value reachable until
  reassignment or frame pop — coarser than liveness-precise rooting, exactly
  as coarse as a C local variable. Acceptable; optionally null out homes at
  scope exit where profiling shows retention in large scopes.
- **Uninitialized-slot hazard (R8's successor).** The collector scans the
  whole reserved slot range, so no slot may contain garbage when a safepoint
  can run. Zero-initialize the reserved range in the prologue (slot counts
  are small under the section 12.3 predicate; a short run of stores or one
  memset call), which makes the invariant local instead of depending on
  "no safepoint before first full publication".
- **Register pressure.** Write-through keeps register usage identical to the
  pre-stack code; the home stores are pure additions, not spills. No MIR
  backend change is required.
- **Suspension paths.** Verify that generator/async resumption is covered by
  `gen_env` tracing rather than by frame homes, or that homes are
  re-established on resume (the `Lambda_Stack_JS_MIR.md` §16 checklist item).
- **Placement.** Compare against the core Lambda MIR-Direct transpiler first:
  it has emitted side-root frames without a comparable blow-up, and the
  explanation (fewer rootable values in pure `fn` bodies, fewer safepoints,
  or a discipline already effectively canonical-slot) determines whether this
  scheme lands as a unification in the shared emitter
  (`lambda/mir_emitter_shared.hpp` / `MirEmitter`) rather than as JS-side
  code.

### 12.7 Relationship to Option A

Option A (liveness-pruned shadow slots) and canonical slots need the same
prerequisites and produce similar steady-state store counts on straight-line
code. The differences are all in Option A's disfavor:

- Option A needs liveness, dirty tracking, and slot-reuse analyses **for
  correctness** (a wrong liveness or reuse decision drops a live root or
  revives a stale one); canonical slots need none of them — correctness is
  the local, structural invariant "the defining write stores to the home".
- Option A retains per-call-site rooting logic in both emission paths (R7);
  canonical slots delete call-site rooting entirely.
- Option A's dirty-flush stores cluster at safepoints and grow with live-set
  size; canonical-slot stores are proportional to definitions, independent of
  how many helpers are called.

Canonical slots therefore dominate Option A: the same result with strictly
less machinery and a smaller correctness surface. If a precise slot-based
scheme is built, build this one.

---

## 13. Safepoint stack maps in detail

### 13.1 Concept

The scheme used by every production precise-GC VM with an optimizing compiler
(HotSpot oop maps, V8 safepoint tables — section 3, .NET GCInfo): the mutator
emits **no rooting code at all**. For every call site that can transitively
reach a collection, the compiler records a stack map: which machine registers
and which frame-slot offsets contain GC references at that exact PC, after
register allocation. At collection time the runtime walks the native stack
frame-by-frame via return addresses, looks up each frame's map, and marks
exactly the recorded locations.

Mutator overhead is zero instructions, zero stores, zero slots — the
theoretical optimum, and the reason this option is recorded in detail even
though it is not currently buildable on the MIR backend.

### 13.2 What the MIR backend would have to provide

The MIR library has no safepoint support (notably, Makarov's own experimental
SIR+MIR CRuby prototype relied on CRuby's conservative machine scan instead of
adding one — section 7.3). A fork would need:

1. **Safepoint annotations that survive the pipeline.** A way to mark
   `MIR_CALL`/`MIR_JCALL` sites as safepoints and attach the set of live
   GC-class virtual registers, preserved through MIR-level optimization —
   inlining, copy propagation, and dead-code elimination must update the
   sets, not drop them.
2. **Post-regalloc location resolution.** After register allocation and frame
   layout, each safepoint's virtual registers must be resolved to
   `{machine register | frame offset}` and emitted as a per-call-site table
   keyed by return PC.
3. **Deterministic frame walking.** Either mandatory frame pointers in
   generated code or emitted unwind information, so the runtime can walk from
   the innermost frame outward and compute each frame's base.
4. **Callee-saved register recovery.** The hard part in every real VM: when
   collection triggers inside a C helper, a JS frame's live reference may sit
   in a callee-saved register that a *younger* frame (possibly a C frame)
   saved to *its* stack. Precise marking requires knowing, for every frame,
   where it saved each callee-saved register, and chaining that recovery
   outward — for MIR-generated frames and, via unwind info or an ABI
   contract, for the C helper frames between them.
5. **Derived-pointer handling** if generated code ever holds interior
   pointers into managed allocations across a safepoint.

Items 1–3 are substantial but bounded engineering. Item 4 is what makes this
a VM-scale project rather than a feature.

### 13.3 Runtime-side changes

- Replace the `setjmp()` + conservative-scan entry sequence — for MIR frames —
  with a stack walker driven by the map tables.
- Map storage must be registered per generated function and interact
  correctly with function lifetime (module unload, cache eviction).
- **Interaction with the MIR code cache** (`Lambda_Design_MIR_Cache.md`): the
  L3 code-image route means stack maps must be serialized and relocated
  alongside code images, and map lookup must behave identically for
  cache-loaded and freshly generated code.

### 13.4 What it buys, and what still blocks the payoff

Stack maps make the *generated-code* side of precise rooting exact and free.
They cover MIR-generated frames only. Retiring the conservative scan
additionally requires everything in the section 10.5 gates — in particular
migrating native helpers to exact handles (section 10.3) and migrating or
retiring C2MIR. Until those land, the end state of stack maps alone is still
dual discovery (precise maps for MIR frames + conservative scan for helper
frames), with the payoff limited to zero mutator cost rather than scan
retirement.

The *full* payoff — no conservative scan anywhere, unlocking a moving or
generational collector — is stack maps **plus** runtime-wide handle
discipline: the "become V8" path.

### 13.5 Adoption preconditions

Do not start item 13.2.1 until all three of the following exist:

1. a decision to adopt a moving or generational collector, or measured GC
   pause/retention costs that conservative scanning demonstrably causes;
2. willingness to fork and maintain the MIR backend;
3. a concrete plan for helper-layer handle discipline (section 10.3).

The section 10.1 representation classes and `may_gc` table are shared with
the slot-based schemes and are worth building regardless; the backend fork,
frame walker, and map infrastructure are useful for nothing else and should
not be built speculatively.

### 13.6 Option comparison

| Property | A: liveness-pruned shadow slots | B: canonical slots (§12) | C: stack maps (§13) |
|---|---|---|---|
| Mutator MIR added | dirty-flush stores per safepoint | ~1 store per rooted definition | none |
| Analyses required for correctness | liveness + dirty + slot reuse | none (homes always current) | map construction through regalloc |
| Call-site rooting logic | retained (R7 risk) | deleted | deleted |
| MIR backend changes | none | none | fork: safepoints, location metadata, unwind |
| Collector changes | none | none | frame walker + map lookup |
| Compatible with moving GC | no (register caches unsound) | only in read-reload mode | yes (for MIR frames) |
| Implementation effort | medium | small–medium | very large |

---

## 14. Proposed Lambda solution: canonical root homes and exact native handles

### 14.1 Decision

Adopt **canonical side-root homes (Option B) for generated code**, add an exact
handle discipline for native helpers, and retain the conservative native-stack
scan only as a migration fallback. Do not build PC-indexed stack maps now.

The generated-code invariant is:

```text
at every operation that can start GC
    each live GC-managed value that is not already reachable through a traced
    owner has one current canonical side-root home

between GC-capable operations
    the value may also remain in a MIR register as a non-authoritative cache
```

"Live" includes more than ordinary live-after-call dataflow. A managed call
argument also needs a home for the duration of a `may_gc` helper if that helper
can allocate before it finishes using the argument. A call result needs no home
during the call that creates it; if it survives to a later safepoint, its
defining write stores it to a home immediately after the call. This is sound
because Lambda GC is cooperative and synchronous: it starts only on known
runtime paths, not asynchronously between MIR instructions.

This is deliberately a two-layer design:

```text
MIR/LambdaJS activation       fixed canonical side-root frame
native C/C++ helper           scoped handles into exact root slots
async or persistent lifetime traced heap field or persistent-root registry
```

The number side stack and scalar-return lane are separate concerns. Preserve
their capture-before-restore and re-home-after-restore rules; a function may
need a root frame, a number frame, both, or neither.

### 14.2 Root classification must be semantic

Do not infer rootability from `MIR_T_I64` or `MIR_T_P`. Add a GC representation
class to compiler values and call signatures, for example:

| GC class | Meaning | Root action |
|---|---|---|
| `NON_ROOT` | integer, flag, context pointer, args mark, native address | none |
| `BOXED_ITEM` | tagged `Item` that may contain a managed reference | home the full item bits |
| `GC_BASE_PTR` | raw pointer to the base of a managed allocation | home a collector-recognized pointer |
| `DERIVED_PTR` | interior pointer into a managed allocation | home its owning base object, not the interior pointer |
| `IMMORTAL_PTR` | name-pool, static, or otherwise non-collected storage | none |

This metadata must travel with a value when it is copied, boxed, unboxed,
merged at control flow, passed to a helper, returned, or stored in a closure
environment. A conservative merge may strengthen `NON_ROOT` to `BOXED_ITEM`,
but it must never weaken a possible managed reference.

Owner reachability should eliminate redundant roots. After a child is stored
in a traced array, object, closure environment, module binding, or other rooted
owner, keep the owner live rather than independently rooting every child. This
optimization is valid only after the ownership store is complete.

### 14.3 Generated MIR contract

The lowering sequence should be:

1. Identify every direct and indirect call that can transitively allocate or
   collect. Unknown calls default to `may_gc`.
2. Give a canonical home to each managed parameter, lexical binding, or
   temporary whose root obligation crosses a `may_gc` operation. For a
   temporary used as a `may_gc` argument, the obligation extends through that
   call even when the temporary is dead afterward.
3. Reuse a slot only after the old value's lexical/root-obligation lifetime
   has ended. Slot coloring is an optimization; correctness must not depend on
   aggressive reuse.
4. Reserve and initialize the final slot range once in the prologue. No
   collector-visible slot may contain uninitialized bits.
5. Make each definition of a homed value write through to its slot. Reads and
   subsequent non-GC operations continue to use the MIR register cache.
6. Restore the incoming root watermark through one normal epilogue. Elide the
   entire root prologue/epilogue when the final root-slot count is zero.

Representative lowering:

```text
prologue
    root_base = context.side_root_top
    ensure/commit N root slots             // runtime operation is no-GC
    initialize slots [0, N) to ItemNull
    context.side_root_top = root_base + N

managed parameter or assignment
    value_reg = ...
    root_base[home(value)] = value_reg      // the defining write

ordinary arithmetic or proven no-GC call
    ...                                     // no rooting MIR

may-GC call
    call helper(...)                        // no publication burst: homes are current

epilogue
    preserve/re-home a wide scalar result if required
    context.side_root_top = root_base
    return
```

For LambdaJS this specifically removes `jm_emit_root_updates()`,
`jm_root_live_scope_vars()`, `jm_root_call_insn_regs()`, and the duplicate
rooting in `jm_call_with_args()`. Their replacement is not another call-site
flush pass: it is a write-through hook at the definition/assignment of a
homed value. The existing dense side-root region, `lambda_side_stack_ensure()`,
collector scan, overflow path, and late-sized frame prologue remain reusable.

### 14.4 Safepoint and helper metadata

Extend the shared import/call descriptor, rather than maintaining a JS-only
name list, with at least:

```text
gc_effect       NO_GC | MAY_GC
argument kinds  GC representation class and ownership/borrowing contract
return kind     GC representation class and ownership contract
```

Rules:

- an unclassified direct helper is `MAY_GC`;
- every indirect call is `MAY_GC` unless its function type proves otherwise;
- `NO_GC` is a reviewed promise covering the full transitive call graph, not
  merely the helper's own source file;
- debug/forced-GC builds should reject allocation or collection while a
  `NO_GC` helper is active;
- the descriptor is shared by MIR-Direct and LambdaJS so the two compilers
  cannot silently assign different effects to the same runtime function.

The effect table is needed even with canonical homes. It prevents unnecessary
homes for values that never encounter a safepoint, enables empty-frame
elision, and defines the legal locations for forced-GC testing.

### 14.5 Native helper rooting

Build the native API on the existing `LambdaRootGuard`, but expose three
distinct lifetime forms:

- a scoped owned root slot for a helper-created value;
- borrowed and mutable handles that refer to an existing exact slot instead of
  pushing a duplicate root at every nested call;
- a persistent root or traced heap field for values retained after the native
  call returns.

A helper may use an unrooted local only when no transitive call before its last
use can allocate. If it derives an interior pointer, it must retain a handle to
the base owner. If it calls generated code re-entrantly, its scoped roots stay
below the callee's frame and therefore remain visible.

`LambdaRootGuard` destruction handles normal C++ exits, but it cannot handle
`longjmp`/`siglongjmp`, which bypass destructors. Every non-local recovery
boundary must snapshot and restore both `side_root_top` and
`side_number_top` (using `LambdaSideStackSnapshot`), plus any other
runtime-owned auxiliary watermark established inside that boundary. The
Test262 batch runner, normal runner, cached-MIR path, timeout path, MIR-error
path, and stack-overflow path must all obey the same rule. The signal handler
must only record the failure and jump; restoration belongs at the safe landing
point.

Generator and async suspension are another non-local lifetime boundary. A
side-root frame belongs to a native activation and must not survive suspension.
Every value needed on resume must first be copied into a traced heap environment
or persistent root; resumption then establishes a fresh activation frame.

### 14.6 Migration sequence

Implement this in stages so correctness evidence exists before either old
rooting path is removed:

1. **Representation and effects:** add GC classes and the shared `NO_GC` /
   `MAY_GC` descriptors; keep current rooting and conservative scanning.
2. **Canonical generated frames:** switch LambdaJS one function category at a
   time to canonical homes, deleting its publication passes for that category.
   Keep the conservative scan as an oracle/fallback. Apply the same contract
   to MIR-Direct rather than leaving two rooting models.
3. **Native handles:** audit allocators and their transitive callers, migrate
   helper locals, callbacks, module state, and embedder-visible retained
   values. Unannotated code remains `MAY_GC`.
4. **Precise-only stress mode:** disable native-stack discovery, poison popped
   slots, and request collection at every legal safepoint. Run this mode under
   sanitizers and with exceptional, recursive, callback, async, and recovery
   paths.
5. **Retire the fallback:** only after MIR-Direct, LambdaJS, native helpers,
   and C2MIR are covered may `setjmp()` register flushing and
   `gc_scan_stack()` be removed from the normal collector.

Stage 2 is the performance fix. Stage 5 is a later collector cleanup; tying the
two together would delay removal of the current MIR-store bottleneck.

### 14.7 Acceptance gates

The design is complete only when all of these are true:

- emitted-MIR inspection shows no per-instruction root-binding scan and no
  before/after-call publication burst;
- root stores are explainable as parameter/definition writes, slot
  initialization, or native handle operations;
- functions with no managed value crossing a `MAY_GC` operation emit no root
  frame;
- forced-GC precise-only tests pass with the conservative native scan disabled;
- stack overflow, timeout, MIR error, thrown exception, callback re-entry, and
  async suspension restore exact watermarks without stale roots or leaks;
- release builds pass Lambda baselines, Test262 with zero unexpected retries,
  Node compatibility tests, Jube, and Radiant;
- release-build MIR counts and Test262 wall time return close to the pre-stack
  baseline, with root-store counts proportional to managed definitions rather
  than total MIR instructions or helper-call count.

This gives Lambda the main benefit of a precise VM frame without requiring a
MIR backend fork: generated activations have exact, always-current homes;
native code has explicit lifetime handles; and conservative discovery can be
removed later as a verified migration outcome rather than as a prerequisite
for fixing today's regression.

---

## 15. References

### V8

- [V8 embedding: handles and garbage collection](https://v8.dev/docs/embed#handles-and-garbage-collection)
- [V8 documentation: accurate collector](https://v8.dev/docs)
- [V8 safepoint table](https://chromium.googlesource.com/v8/v8.git/+/refs/heads/lkgr/src/codegen/safepoint-table.h)
- [V8 optimized-frame root iteration](https://chromium.googlesource.com/v8/v8/+/7cb6c81/src/frames.cc)

### SpiderMonkey

- [SpiderMonkey GC overview](https://firefox-source-docs.mozilla.org/js/gc.html)
- [SpiderMonkey architecture and JIT tiers](https://firefox-source-docs.mozilla.org/js/)
- [SpiderMonkey rooting hazard analysis](https://firefox-source-docs.mozilla.org/js/HazardAnalysis/running.html)

### JavaScriptCore

- [Understanding GC in JavaScriptCore](https://webkit.org/blog/12967/understanding-gc-in-jsc-from-scratch/)
- [Speculation in JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/)
- [JavaScriptCore Heap.cpp](https://chromium.googlesource.com/external/github.com/WebKit/webkit/+/d08c15c73b5124b56db6f1123bbb23cb55ad9535/Source/JavaScriptCore/heap/Heap.cpp)

### QuickJS

- [QuickJS documentation](https://bellard.org/quickjs/quickjs.html)
- [QuickJS C API ownership description](https://bellard.org/quickjs/quickjs.pdf)

### CRuby

- [CRuby execution-context marking](https://docs.ruby-lang.org/capi/en/master/de/de9/vm_8c_source.html)
- [CRuby `RB_GC_GUARD` and conservative-stack contract](https://docs.ruby-lang.org/capi/en/master/dc/d18/memory_8h.html)
- [CRuby internal GC API](https://docs.ruby-lang.org/capi/en/master/db/df8/include_2ruby_2internal_2gc_8h.html)
- [Makarov's experimental SIR+MIR CRuby design](https://developers.redhat.com/articles/2022/11/22/how-i-developed-faster-ruby-interpreter)

### Canonical-slot / shadow-stack scheme

- Fergus Henderson, [Accurate garbage collection in an uncooperative
  environment](https://dl.acm.org/doi/10.1145/512429.512449), ISMM 2002
- [LLVM garbage collection: the shadow-stack strategy](https://llvm.org/docs/GarbageCollection.html#the-shadow-stack-gc)

### Lambda

- [Lambda runtime stack/rooting review](./Lambda_Stack_MIR.md)
- [LambdaJS emitted-MIR review](./Lambda_Stack_JS_MIR.md)
- [Conservative-scan removal plan](./Lambda_Design_Stack_Frame2.md)
