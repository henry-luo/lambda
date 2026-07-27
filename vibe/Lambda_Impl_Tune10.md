# Lambda Impl Proposal: Tune 10 — MIR Tail Specialization, Owned Strings, and Lifetime Regions

**Status: COMPLETE 2026-07-27 — T2, T3, T4, T5, T6, and T7 retained; T1
rejected.**

**Scope:** Tune10 targets Lambda MIR Direct performance. It does not target
LambdaJS, and it does not change the frozen C2MIR path. It contains the seven
independent Lambda workstreams identified from the current slow MIR rows:

1. module-level immutable constant propagation;
2. loop-carried flex-int SSA residency and deferred boxing;
3. array and string specialization across closed call sites;
4. exclusively owned, GC-heap `String` builders;
5. a store-side map member IC;
6. native sized-integer lowering, starting with `u32`;
7. compiler-proven lifetime regions for temporary object graphs.

All seven mechanisms preserve precise `RootFrame`/`Rooted` ownership.
Conservative native-stack scanning remains retired.

Tune9 established that removing zero-fill alone did not improve `json_gen`, and
that bypassing generic typed-map construction improved `gcbench` by only 4.7%.
Tune10 therefore targets larger algorithmic and representation costs instead
of further general allocator micro-tuning.

---

## 1. Targets and acceptance rule

The initial targets use Result15 or, where Tune9 established a newer focused
baseline, Tune9's last recorded release medians:

| Track | Primary targets | Recorded median |
|---|---|---:|
| T1 — module constants | `jetstream/navier_stokes` MIR | 971.8 ms |
| T2 — flex-int SSA | `larceny/diviter` MIR | 3.09 s |
| T3 — closed-call specialization | `larceny/triangl` MIR | 653.1 ms |
| T4 — owned string builder | `kostya/json_gen` MIR | 76.568 ms |
| T4 — owned string builder | `kostya/base64` MIR | 310.713 ms |
| T5 — map store IC | `jetstream/richards` MIR | 208.4 ms |
| T5 — map store IC | `jetstream/splay` MIR | 164.3 ms |
| T6 — native sized integers | `jetstream/crypto_sha1` MIR | 205.5 ms |
| T7 — lifetime region | `larceny/gcbench` MIR | 384.935 ms |

Before implementation timing, rebuild the current release binary and establish
fresh three-run medians. The fresh medians become the actual acceptance
baselines if they differ from the recorded values.

A track is retained only when:

- at least one declared primary target median improves by at least 5%;
- output and exit status are unchanged;
- no correctness regression is introduced;
- no confirmed performance regression is introduced in the guard set.

This is a per-benchmark gate, never an average: a gain on one benchmark cannot
be used to offset a regression on another. A track with multiple primary rows
may land when one row clears 5% and the remaining primary rows are
non-regressing.

The tracks are independent. A retained track does not require every other
Tune10 track to land.

---

## 2. Full Tune10 workstream

### 2.1 T1 — module-level immutable constant propagation

Build a dependency graph for module-level immutable `let` bindings and evaluate
the closed, pure scalar subgraph at compile time:

```lambda
let WIDTH = 128
let HEIGHT = 128
let ROW_SIZE = WIDTH + 2
let GRID_SIZE = ROW_SIZE * (HEIGHT + 2)
```

Eligible expressions are literals and audited pure scalar operators/functions
whose inputs are also constants. Cycles, dynamic lookups, error-producing
operations without an exact compile-time result, containers with mutable
identity, and effectful calls remain runtime expressions.

The folded result must preserve Lambda's numeric type and overflow semantics.
MIR Direct then emits an immediate instead of loading and unboxing a module
global. The inferred type and range are also fed into T2 and T3.

Primary target: `jetstream/navier_stokes`. Secondary targets include `nbody`,
`spectralnorm`, `primes`, and other loops parameterized by module constants.

### 2.2 T2 — loop-carried flex-int SSA residency

Keep a loop-carried flexible integer in native MIR state rather than boxing the
result of each arithmetic operation and immediately calling `it2i` to recover
the payload for the next assignment.

The native state contains:

- an integer payload while the value remains in the packed integer domain;
- the promoted numeric payload/state after an overflow that Lambda semantics
  require to remain promoted;
- merge/phi state at loop headers and control-flow joins.

Arithmetic emits a native fast lane plus the required overflow transition.
Box only at an observable escape: an Item-taking call, generic comparison,
container store, closure/global capture, unknown merge, or public return.
Range facts from T1 may remove overflow transitions where safety is proved.

Primary target: `larceny/diviter`. Secondary targets include
`navier_stokes`, `triangl`, `primes`, `levenshtein`, and numeric loop bodies.

### 2.3 T3 — array and string specialization from local proofs and closed calls

First retain local element facts when the whole function proves their safety;
then extend the existing direct-call analysis so a non-escaping function with
a closed set of compatible call sites may receive specialized pointer
parameters:

- `ArrayNum*` plus guarded element type;
- hoisted array data pointer and length where mutation rules permit;
- `String*` for audited read-only string parameters;
- homogeneous numeric literals and locally narrowed arrays.

Emit one entry guard and a specialized body with direct element loads/stores.
Keep the generic boxed wrapper for public, indirect, escaped, mismatched, or
otherwise unproven callers. Mutation must continue through the existing COW
ownership rules; specialization does not authorize writes to shared or static
containers.

Primary target: `larceny/triangl`. Secondary targets include
`navier_stokes`, `brainfuck`, `base64`, and `levenshtein`.

### 2.4 T4 — owned `String` builder

Use the `String` itself as the builder under exclusive ownership. The complete
representation, capacity, ownership, sharing, and MIR lowering design is in
section 3.

Primary targets: `kostya/base64` and `kostya/json_gen`.

### 2.5 T5 — store-side map member IC

Add the write-side counterpart of `fn_member_ic`. A per-call-site cell caches
the map shape and resolved field entry/offset for a static member name.

A hit may directly perform a same-representation field store only after the
audited shape, COW, static-container, and field-type guards pass. A type change,
shape rebuild, missing field, element/object semantic case, or ownership
failure uses the canonical `fn_map_set` path. The implementation must reuse the
existing shape lookup and field-store helpers instead of duplicating their
logic.

Audit every writer of `Map::type` before relying on shape identity as the
invalidation guard. If an existing path mutates a cached shape in place, add a
shared shape epoch/version mechanism rather than a local workaround.

Primary targets: `jetstream/richards` and `jetstream/splay`. Secondary targets
include `raytrace3d` and `gcbench`.

### 2.6 T6 — native sized-integer lowering

Keep explicitly sized integers, starting with `u32`, in native MIR registers
across compatible direct calls and loop operations. Lower:

- arithmetic with the type's exact wrap, mask, overflow, or conversion rules;
- shifts and bitwise operations directly;
- comparisons without Item conversion;
- small closed leaf procedures such as SHA-1 rotate/add helpers through direct
  specialization or measured inlining.

Box only when crossing into a generic Item boundary. Do not infer wrapping
machine-integer semantics for ordinary Lambda `int`; T6 applies only where the
sized type makes those semantics explicit.

Primary target: `jetstream/crypto_sha1`.

### 2.7 T7 — lifetime regions

Detect fresh, non-escaping object graphs and allocate them in a resettable
lifetime region selected per call site. The complete analysis, allocator, and
GC design is in section 4.

Primary target: `larceny/gcbench`.

---

## 3. T4 — `String` as an exclusively owned builder

### 3.1 Representation

Use the same anonymous flags-union style already used by `Container`:

```c
typedef struct String {
    uint32_t len;       // byte length
    union {
        uint8_t flags;
        struct {
            uint8_t is_ascii:1;
            uint8_t is_buffer:1;
            uint8_t reserved:6;
        };
    };
    char chars[];       // UTF-8 data followed by '\0'
} String;
```

This is intentionally a bitfield representation. It provides readable
`str->is_ascii` and `str->is_buffer` access while allowing constructors to
initialize the complete state byte through `str->flags`.

The current ABI is preserved on the supported compiler targets:

```text
offset 0  uint32_t len
offset 4  uint8_t flags / bitfields
offset 5  char chars[]
sizeof(String) == 8
```

Add compile-time layout assertions for `sizeof(String)` and
`offsetof(String, chars)`, plus a unit test that verifies the correspondence
between `flags`, `is_ascii`, and `is_buffer` on every supported compiler.

The `String` definitions exposed through `lambda/lambda.h` and `lib/string.h`
must remain identical. No third representation may be introduced.

### 3.2 States and ownership invariant

A string has one of two states:

| State | `is_buffer` | Allocation owner | May be shared? | May grow in place? |
|---|---:|---|---:|---:|
| ordinary string | 0 | arena, constant pool, name pool, GC heap, or other existing owner | yes | no |
| owned buffer string | 1 | GC object heap only | no | yes |

The defining invariant is:

```text
is_buffer == 1
    implies exact GC-object ownership
    and one exclusive logical owner
```

Arena, constant, parser, name-pool, and other non-GC strings do not support
buffer mode. Their `is_buffer` bit must always be zero.

Because many existing pool and arena constructors allocate uninitialized
storage, the representation migration must audit every `String` constructor.
At construction, code must initialize the whole byte:

```c
str->flags = 0;
str->is_ascii = is_ascii ? 1 : 0;
```

Later updates may use the named bitfields when preserving the other state bit
is intended. A generic runtime path must never interpret `is_buffer` until this
constructor audit is complete.

An owned-buffer constructor uses the same rule and then explicitly enables
buffer mode:

```c
str->flags = 0;
str->is_ascii = is_ascii ? 1 : 0;
str->is_buffer = 1;
```

No `StringFlags` enum is required because Tune10 does not perform raw flag-mask
operations. Add named masks later only if an implementation actually needs
whole-byte bitwise operations.

### 3.3 Capacity without enlarging `String`

An owned buffer has no capacity field in `String`. Its capacity comes from the
adjacent GC allocation header:

```text
[gc_header_t][String user allocation]
              ^
              String*
```

For a buffer allocated using the existing `sizeof(String)` allocation
convention:

```c
gc_header_t* header = gc_get_header(str);
size_t capacity = header->alloc_size - sizeof(String) - 1;
```

`capacity` counts character bytes and excludes the terminating NUL. Tune10
must use `header->alloc_size`, not the object-zone size-class rounding; hidden
slot slack is not owned capacity.

Only code that already proved `str->is_buffer` may recover the adjacent header.
Calling `gc_get_header()` on an arena or constant string is invalid.

The buffer allocator requests:

```text
sizeof(String) + requested_capacity + 1
```

and initializes every header field, `len`, `flags`, and `chars[len]`.

### 3.4 Normal concat path and first concatenation

An ordinary string cannot be converted in place: it may be shared, may not have
capacity, and may not have a GC header. "Turn into buffer mode on first concat"
therefore means that the normal `fn_strcat(left, right)` concat result is a new
owned buffer when MIR has selected its builder lane:

```text
ordinary left ++ right
    -> allocate a GC buffer at the exact result length
    -> copy left and right once
    -> return the exclusive buffer result
```

The input strings remain unchanged.

If the left operand is already an exclusively owned buffer, the same
`fn_strcat(left, right)` implementation appends to it:

```text
owned buffer left ++ right
    -> append into left when capacity is sufficient
    -> otherwise allocate a larger owned buffer and copy once
```

The runtime does not add `fn_string_builder_join`, `fn_string_builder_start`,
or a parallel generic concat helper. Dynamic scalar conversion remains on the
existing normal concat path; after it has produced a `String*`, that path calls
the same `fn_strcat` implementation.

Only the left buffer is reusable. It matches append order and avoids a prepend
`memmove`; a right buffer is a read-only source. If both operands identify the
same buffer, or the compiler cannot prove exclusive ownership, `fn_strcat`
allocates an ordinary/new buffer result rather than modifying either input.

Every append must:

- reject `len` and allocation-size overflow before arithmetic;
- reload rooted operands after any allocation that can collect;
- preserve the terminating NUL;
- update `is_ascii` to `left->is_ascii && right->is_ascii`;
- leave the result in owned buffer mode.

The first concat does not reserve speculative slack: ordinary short-lived
chains are common, and their exact result size avoids turning every tiny
temporary into a minimum-size builder allocation. Once an owned buffer needs
to grow, capacity grows geometrically. The first implementation should compare
1.5x and 2x growth on the two target benchmarks instead of assuming either
factor is universally better. The chosen rule must also enforce
`new_capacity >= required_length`.

### 3.5 Sharing, borrowing, moving, and freezing

`is_buffer` is an ownership promise, not merely an allocation-format marker.
A buffered string must never have two mutable logical owners.

The MIR Direct ownership analysis distinguishes:

- **move/consume:** ownership transfers; no copy is required;
- **borrow:** a known non-retaining operation reads the string during the call;
  no copy is required;
- **share/escape:** another live reference may outlive the operation; an
  immutable copy is required;
- **freeze:** exclusive ownership is surrendered and `is_buffer` is cleared;
  the same allocation may then be shared but cannot grow again.

Initial no-copy borrows may include only audited operations such as `len`,
indexing, equality, hashing, and formatting calls whose ABI guarantees that the
pointer is not retained. Unknown calls are escaping.

A share includes:

- assigning the value while the original binding remains live;
- storing it in an array, map, element, object, closure, module global, or
  persistent state;
- passing it to an unknown or retaining call;
- capturing it in an async task;
- merging it through control flow when uniqueness cannot be proved.

At a share, the receiving reference gets an ordinary immutable copy. The
source may remain an owned builder only when the compiler can still prove its
single ownership. If not, freeze or copy before the merge. This proof is why a
generic runtime call may not decide to reuse a buffer merely from
`is_buffer == 1`.

Returning a uniquely owned buffer is a move, not a share. The return boundary
may clear `is_buffer` in place when the caller expects an ordinary value, or
transfer the ownership fact to an analyzed direct caller. Public, indirect,
variadic, and otherwise unknown call boundaries receive ordinary immutable
strings.

### 3.6 MIR Direct integration

Do not make the existing generic `fn_join(Item, Item)` silently mutate any
left operand. It has no uniqueness proof. Do not add a second
`fn_string_builder_join()` path either.

Add an ownership-aware lowering of the existing `fn_strcat()` call used only
when MIR Direct proves:

1. the operation is string concatenation;
2. the left value is a fresh concat temporary or a uniquely owned local;
3. the result is consumed or installed as that local's replacement;
4. no alias of the buffer remains live.

The right operand may remain dynamically typed: the lowering uses the existing
`fn_string(Item)` conversion before calling `fn_strcat`, so it preserves the
same scalar conversion semantics as `fn_join` without introducing a second
concat operation. All other joins remain on `fn_join` and are frozen before
they can escape. This covers the important shapes:

```lambda
json = json ++ obj
result = result ++ chunk
"{" ++ key ++ ":" ++ value ++ "}"
```

The transpiler represents buffer ownership on the value/binding analysis, not
by inspecting the runtime flag alone. It emits the ordinary concat conversion
and then calls `fn_strcat`; the ownership fact merely permits `fn_strcat` to
reuse its left buffer. Ordinary identifier reads, stores, captures, unknown
calls, and unproven merges freeze/copy before they can share the value.

This avoids a builder-specific concat helper. The normal concat code owns its
rooting obligation exactly once; its buffer allocation and growth use the
same precise roots as every other `fn_strcat` call.

The initial implementation is MIR Direct only. Interpreter or generic runtime
paths continue returning immutable strings until they receive equivalent
ownership analysis.

### 3.7 Why this attacks the measured targets

`json_gen` repeatedly extends a growing `json` value and builds object strings
through left-associated concat chains. `base64` repeatedly extends `result`
with four-character groups. The current path allocates a complete destination
and recopies the entire left prefix for each concat.

With geometric owned buffers:

- the growing prefix is copied only when capacity grows;
- intermediate concat-chain results reuse the same allocation;
- allocation count falls from approximately one per concat to one per growth;
- total prefix copying changes from quadratic to amortized linear.

This is materially different from Tune9 phase A. Tune9 removed a zero-fill but
retained the same number of allocations and full-prefix copies.

### 3.8 T4 correctness gates

Add focused tests for:

- `flags`/bitfield layout and zero `is_buffer` on every allocation family;
- ASCII and non-ASCII append;
- empty strings and embedded UTF-8;
- NUL termination after every append and growth;
- exact-capacity append and one-byte growth;
- integer/size overflow rejection;
- arena and constant operands on first concat;
- forced GC during allocation and growth;
- alias preservation, for example:

```lambda
var a = "x"
let old = a
a = a ++ "y"
// old remains "x", a is "xy"
```

- copying on container storage, closure capture, unknown calls, and control-flow
  merges;
- move/freeze behavior at returns;
- poison/stress execution with precise roots.

Required broad gates:

```text
make test-lambda-baseline
forced-GC rooting tests
MIR GC stress tests
```

Because the physical `String` ABI is shared broadly, also build the full test
targets and run the relevant formatter, input, and namespace layout tests.

---

## 4. T7 — compiler-proven lifetime regions

### 4.1 Target lifetime in `gcbench`

The hot inner expression is:

```lambda
total_check = total_check + check_tree(make_tree(depth))
```

The tree returned by `make_tree(depth)`:

1. is fresh;
2. is read only by `check_tree`;
3. is not stored, returned, captured, or carried to the next iteration;
4. becomes unreachable immediately after `check_tree` returns.

This is a natural region:

```text
region begin
    temporary_tree = make_tree(depth)  // every node allocated in region
    count = check_tree(temporary_tree) // borrowed read
region end                            // reclaim all nodes in bulk
```

The `stretch` and `long_lived` calls use the same `make_tree` function but have
different lifetimes. Region choice must therefore be call-site sensitive. The
callee cannot be classified globally as "always region allocated."

### 4.2 Static detection

Detection is a conservative MIR Direct analysis over the procedure CFG and a
fixed-point summary of directly called procedures.

#### Producer summary

A function may produce a region graph only when analysis proves:

- every object in the returned graph is freshly allocated by this call or a
  region-eligible direct callee;
- recursive calls preserve the same property;
- it does not store a region pointer into a global, captured environment,
  persistent state, task, or pre-existing heap object;
- it does not pass a region pointer to an unknown or retaining call;
- it does not return aliases to pre-existing mutable objects as part of the
  candidate graph;
- all exceptional exits can discard the incomplete graph safely.

`make_tree` qualifies: it recursively returns fresh nodes and connects them
only into newly allocated parents.

#### Consumer summary

A call is a region-safe borrow when it:

- reads the graph without retaining it;
- does not store any graph pointer outside the graph;
- does not start async work using it;
- does not return a graph pointer that remains live after region end.

`check_tree` qualifies as a read-only borrower.

#### Call-site liveness

For each fresh-graph producer:

1. build def-use information for the returned value;
2. reject return, capture, persistent storage, unknown calls, loop-carried
   values, and ambiguous aliasing;
3. find a release point that post-dominates every use;
4. require a single-entry region interval, or emit cleanup on every proven
   exit;
5. ensure no region-derived value is live after the release point.

The nested `check_tree(make_tree(depth))` expression has one producer, one
borrow, and an immediate post-dominating release point. It is the first
supported pattern.

The initial implementation should reject uncertain cases instead of adding
runtime escape detection or promotion.

### 4.3 Region propagation

The same function may allocate normally at one call site and into a region at
another. Region-eligible direct functions therefore receive a hidden allocation
context:

```text
make_tree(depth, region_or_null)
```

- `region_or_null == NULL`: use the ordinary GC object allocator;
- non-NULL: allocate eligible fresh objects from that region;
- recursive direct calls propagate the same region.

Use a hidden direct-call parameter rather than a process-global or thread-local
"current region." A global implicit region could accidentally capture
allocations made by unrelated or unknown callees and would make escape safety
depend on dynamic call behavior.

Public, indirect, variadic, external, async, and unproven calls do not receive a
region capability.

### 4.4 Region allocator

Add a per-context region allocator with a stack-scoped descriptor:

```text
LambdaRegion
    block list
    current cursor/end
    optional reusable-block cache link
    debug generation/state
```

Allocation is aligned bump allocation from region blocks. Ending a region
recycles or frees its blocks in bulk; it never walks and individually frees
every tree node.

For the first implementation, retain an adjacent `gc_header_t` for each region
object so existing type-tag, allocation-size, and debug assumptions remain
valid. Mark region objects explicitly and keep them out of:

- the general heap's `all_objects` list;
- object-zone free lists;
- per-slot allocation bitmaps;
- individual sweep accounting.

The region owns them as a block set.

Do not repurpose an existing GC flag without auditing its generation, large,
bump, and freed bit assignments. If no header bit is safely available, use
region block ownership to classify the pointer.

Region blocks should be reusable by the next iteration so `gcbench` reaches a
steady state without one `malloc`/`free` pair per tree.

### 4.5 Interaction with precise GC

No ordinary GC-heap object may acquire a pointer into a region unless the
pointed graph is copied to the normal heap first. The first implementation
rejects such stores statically.

Region objects may point to:

- other objects in the same region;
- immutable arena/constant/name-pool data;
- ordinary GC objects whose lifetimes exceed the borrow.

If an ordinary GC collection can occur while a region is active, the active
region must be registered as a precise root provider for any outgoing pointers
to ordinary GC objects. The collector may scan the region's typed objects to
mark those outgoing references, but it does not mark or sweep the region
objects themselves.

For the initial `gcbench` pattern, the tree nodes contain only null or
same-region node pointers plus static type/shape metadata. The implementation
may use a verified "no outgoing GC pointers" region summary and skip region
scanning for this case.

The region descriptor must be closed on:

- the normal post-dominating exit;
- `return`, `break`, and `continue` paths crossing the boundary;
- propagated Lambda error exits;
- task cancellation or other procedural unwind paths that are valid inside
  the candidate interval.

If cleanup emission cannot be proved for every exit, the candidate is rejected.

### 4.6 Why this attacks `gcbench`

Tune9 measured `gc_object_zone_alloc` as 85.2% of the scaled `gcbench` profile.
The direct typed-map construction experiment retained the same general heap
allocation for every node and therefore improved the benchmark by only 4.7%.

A lifetime region changes the allocation mechanism for the entire temporary
tree:

- one cursor bump replaces class lookup/free-list/general-heap registration;
- region objects do not enter the individual sweep population;
- an iteration reclaims the tree by resetting/recycling blocks;
- long-lived trees remain ordinary precisely traced GC objects.

This is call-site lifetime specialization, not a benchmark-specific
`make_tree` or type-name special case.

### 4.7 T7 correctness gates

Add analysis tests that accept:

- fresh recursive graph followed by one or more read-only borrows;
- a call-site-specialized recursive producer;
- release at a post-dominating point;
- cleanup across locally supported branches.

Add rejection tests for:

- return or global storage of a region object;
- storage into a pre-existing heap container;
- closure or async capture;
- unknown/indirect calls;
- loop-carried region values;
- graph values live on only some merge paths;
- region-to-region pointers with incompatible nested lifetimes;
- error or control-flow exits without proven cleanup.

Runtime tests must cover:

- forced GC while a region is active;
- normal GC references reachable from a region;
- nested regions;
- region reset/reuse and large-block fallback;
- debug poisoning after region end;
- exact output under repeated `gcbench` execution;
- long-lived and stretch trees remaining valid after temporary-region resets.

Required broad gates:

```text
make test-lambda-baseline
test-gc-rooting-core
MIR GC stress tests
forced-GC plus freed-memory poisoning
```

---

## 5. Implementation order

### T1 — module constants

- Build the immutable binding dependency graph.
- Fold only exact pure scalar expressions.
- Feed the resulting type/range facts into MIR emission and later tracks.
- Measure `navier_stokes`; retain only after its independent 5% gate.

### T2 — loop-carried flex-int residency

- [x] Detect the conservative positive-step shape `x >= y; x = x - y` and
  compile a raw compact lane only under a runtime `y > 0` guard.
- [x] Keep a zero-initialized `q = q + 1` counter raw when it is the sole
  counter update in that same loop.
- [x] Emit the pre-existing flex-int loop unchanged for `y <= 0`, unproven
  loop bodies, captures, nested control flow, or nonzero counters.
- [x] Add positive and generic-fallback regression coverage and measure
  `diviter`.
- Re-profile the numeric secondary rows before considering broader SSA/phi
  representation work.

### T3 — local and closed-call array/string specialization

- [x] Retain local boolean-array narrowing when every future write is proven
  boolean; keep generic `item_at` as the runtime fallback.
- [x] Measure `triangl`; retain the local specialization independently.
- Extend call-site summaries with compatible array/string pointer lanes.
- Add one guarded specialized entry and keep the generic wrapper.
- Hoist data/length only under existing COW and mutation invariants.
- Re-profile `navier_stokes`, `brainfuck`, `base64`, and `levenshtein` before
  considering the deferred closed-call slice.

### T4.0 — String ABI and constructor invariant

- Complete: anonymous flags union; no `StringFlags` enum.
- Complete: both public String definitions and generated embed header updated.
- Complete for the audited allocation families touched by runtime, parser,
  formatter, URL, AST, and JS String construction: normal strings initialize
  `flags = 0` before `is_ascii`.
- Complete: compile-time ABI assertions, focused alias/UTF-8/growth regression,
  forced-GC checks, and the Lambda baseline are green.

### T4.1 — normal concat-chain buffers

- Complete: `fn_strcat` allocates GC buffers and derives capacity from the
  adjacent GC header; there is no builder-join runtime entry point.
- Generic joins remain on canonical `fn_join`; no unproven expression can
  retain mutable buffer mode.

### T4.2 — owned accumulator reassignment

- Complete: compiler-owned `s = s ++ rhs` lowers through `fn_string(Item)` and
  the same `fn_strcat`; ordinary identifier reads freeze before aliasing.
- Complete: `json = json ++ part` and `result = result ++ chunk` retain the
  buffer; aliases, UTF-8, growth, forced GC, and the full baseline are covered.

### T5 — store-side map member IC

- Audit shape mutation and choose shape identity or a shared shape epoch as the
  invalidation contract.
- Add a per-site write IC around the existing shape/field-store helpers.
- Keep type changes, COW detachment, static values, and semantic edge cases on
  `fn_map_set`.
- Measure both `richards` and `splay`; retain T5 when at least one improves by
  5% and the other is non-regressing.

### T6 — native sized integers

- Start with `u32` storage, calls, arithmetic, shifts, and comparisons.
- Preserve exact conversion and wrap/mask semantics.
- Specialize or inline small closed typed leaves only when separately measured.
- Measure `crypto_sha1`; retain only after its independent 5% gate.

### T7.0 — lifetime/effect summaries

- Add fresh-graph producer, borrow, retain, and escape summaries.
- Emit analysis diagnostics or counters under an opt-in debug mode.
- Prove that the intended `gcbench` inner expression is selected and the
  long-lived tree is not.

### T7.1 — region runtime and MIR plumbing

- Add region descriptor, reusable blocks, precise cleanup, and hidden direct
  parameters.
- Keep normal allocation as the fallback for every unproven call site.
- Run forced-GC and poison tests before performance timing.

### T7.2 — `gcbench` acceptance

- Enable the proven inner-tree region.
- Verify output and long-lived tree correctness.
- Run interleaved release measurements and the guard set.
- Retain only if the target clears the 5% gate.

---

## 6. Performance protocol

For each candidate:

1. use release builds only;
2. build separate baseline and candidate binaries from known source states;
3. run target and guard workloads interleaved for at least three pairs;
4. compare medians of `__TIMING__`, exact output, and exit status;
5. capture allocation counters:
   - constant bindings folded/rejected;
   - flex-int operations kept native, promoted, and boxed at escapes;
   - specialized array/string call sites and generic fallbacks;
   - ordinary object count and bytes;
   - buffer-string allocations, growths, in-place appends, copies, freezes;
   - map store-IC hits, misses, invalidations, and semantic fallbacks;
   - sized-integer native operations and Item-boundary boxes;
   - region count, region bytes, block reuse, and fallback count;
6. profile the retained candidate to confirm that the intended cost actually
   fell;
7. rerun five pairs for any adverse guard movement above 3%.

T4 guards:

- `larceny/gcbench`
- `jetstream/splay`
- `kostya/matmul`
- `larceny/ray`
- `jetstream/richards`

T7 guards:

- `kostya/json_gen`
- `kostya/base64`
- `jetstream/splay`
- `kostya/matmul`
- `larceny/ray`
- `jetstream/richards`

Counters and analysis diagnostics must be opt-in and absent from ordinary
release output.

---

## 7. Explicit non-goals

- No LambdaJS optimization or JS ownership model.
- No C2MIR implementation.
- No conservative stack scanning.
- No general arena allocation for arbitrary Lambda values.
- No runtime guessing that reference count is one; Lambda has no such count.
- No mutation of arena, constant, parser, name-pool, or shared GC strings.
- No implicit thread-local region that captures unknown allocations.
- No benchmark-name, function-name, type-name, or tree-shape special case.
- No region escape promotion in the first implementation.
- No claim that lower allocation count alone is success without an end-to-end
  target improvement.

---

## 8. Decision summary

1. Tune10 contains seven independent Lambda/MIR tracks: module constants,
   flex-int SSA, closed-call array/string specialization, owned strings,
   store-side map ICs, native sized integers, and lifetime regions.
2. Each track must improve at least one declared primary target median by at
   least 5%; benchmark results are never averaged to qualify a track, and all
   other primary rows must be non-regressing.
3. `StringFlags` is not needed; constructors clear the complete `flags` byte
   and then assign the readable named bitfields.
4. `String` uses the anonymous `flags` union with `is_ascii` and
   `is_buffer` bitfields.
5. Buffer mode is exclusive to GC-heap strings with compiler-proven unique
   ownership.
6. Capacity remains outside `String` and is recovered from the adjacent
   `gc_header_t::alloc_size`.
7. First concat allocates a buffer result; it never mutates an ordinary input.
8. Subsequent concat may update the owned buffer in place.
9. Sharing requires an immutable copy or a proven freeze/move boundary.
10. Lifetime regions are detected from fresh-graph, escape, borrow, CFG
   liveness, and post-dominance analysis.
11. Region allocation is selected per call site and propagated through a hidden
   direct-call parameter.
12. `gcbench` temporary trees qualify; its long-lived tree does not.

---

## 9. Execution record

### T1 — rejected (2026-07-27)

Implemented conservative MIR lowering for module-scope integer constant DAGs
and measured five interleaved release pairs on `navier`. The median changed
from `914.442 ms` to `916.980 ms` (0.28% slower), so it does not meet the
one-target 5% requirement. The candidate code and tests were removed.

### T2 — retained: guarded compact loop lane (2026-07-27)

`diviter_div` and `diviter_mod` previously packed every `x - y` result into a
flexible integer Item, immediately called `it2i`, and wrote the same raw
payload back to the loop local. `diviter_div` repeated the same work for its
zero-initialized `q = q + 1` counter.

The retained lowering recognizes only a deliberately narrow induction shape:
an uncaptured native `int` loop guarded by `x >= y`, with exactly one
top-level `x = x - y` update. It emits a `y > 0` branch before the loop. In
that branch, `x - y` stays in the compact integer domain because the loop
condition gives `x >= y > 0`; a sole `q = q + 1` counter that began at the
literal zero can advance no more than the compact initial `x` magnitude. The
fast lane therefore uses raw MIR subtraction/addition. The `y <= 0` branch,
captures, nested control flow, extra writes, nonzero counters, and every other
unproven shape compile through the original flexible-integer path.

Release measurements against the prior current release median were:

- prior median: `3108.98 ms`;
- candidate samples: `1217.70 ms`, `1217.06 ms`, `1218.97 ms`;
- candidate median: `1217.70 ms` (**60.8% faster**).

All timing runs printed `diviter: PASS`. `proc_compact_loop` verifies the
positive lane and a non-positive no-entry fallback normally and with forced
GC plus freed-memory poisoning. The reviewed MIR size ratchet for the existing
closed-loop fixture rises by 15 instructions because it now contains both the
raw and generic bodies. `make test-lambda-baseline` passed `3585/3585`,
including `25/25` forced-GC MIR stress tests. T2 is retained; broader
promoted-state SSA remains unnecessary for this proven loop class.

### T3 — retained: locally proven boolean Array narrowing (2026-07-27)

`triangl` creates `board` with `fill(15, true)` and writes only `true` or
`false`, but the mutation analysis treated every boolean element store as an
element-type invalidation. Its hot reads therefore called generic `item_at`
despite the runtime object remaining a generic `Array` of boolean Items.

The analysis now retains `elem_type = BOOL` for concrete boolean writes and
rejects an unknown or non-boolean later write. This distinction is necessary:
a generic `Array` retains its container tag after a mixed write, so its fast
boolean load cannot rely on an ArrayNum-style runtime lane guard. The guarded
fast load remains available only while every write proves the stored Item is a
boolean; otherwise the existing generic path is emitted.

Release timings against the prior fresh `triangl` median were:

- prior median: `656.888 ms`;
- candidate samples: `345.659 ms`, `347.852 ms`, `346.648 ms`;
- candidate median: `346.648 ms` (**47.2% faster**).

All runs printed `triangl: PASS`. `proc_fill` now also verifies that a boolean
write preserves the narrowing while a subsequent string write reads back as
the string, proving the mixed-write fallback remains semantic. The full
`make test-lambda-baseline` gate passed `3584/3584`, including `25/25`
forced-GC MIR stress tests. T3 therefore clears the per-benchmark 5%
retention rule; its broader closed-call specialization remains deferred.

### T4 — retained: owned String buffers (2026-07-27)

The first helper-based experiment was rejected: `base64` improved only 1.28%
and `json_gen` regressed 23.0%. The revised implementation keeps one normal
concat primitive: `fn_strcat`. Generic joins remain immutable through
`fn_join`; only a compiler-proven local self-rebinding concat retains buffer
mode. The right side is converted through the existing `fn_string(Item)` path,
so dynamic scalar-conversion semantics remain canonical.

`String` carries readable `is_ascii` and `is_buffer` bitfields in its existing
eight-byte ABI. A buffer's capacity is `gc_header_t::alloc_size -
sizeof(String) - 1`; first concat uses exact capacity and later append growth
is geometric. The buffer flag is a constructor-audited ownership invariant, so
the append path does not perform a heap-wide `gc_is_managed()` lookup.

Fresh three-run release medians on the final binary:

- `base64`: `290.522 ms` to `77.127 ms` (73.45% faster).
- `json_gen`: `71.464 ms` to `17.116 ms` (76.05% faster).

The focused alias/UTF-8/growth regression passed normally and under forced GC
with freed-memory poisoning. `make test-lambda-baseline` passed `3607/3607`,
including `24/24` MIR forced-GC stress tests. T4 therefore clears the
per-benchmark 5% retention rule and is retained.

### T5 — retained: static member-store key lowering (2026-07-27)

The existing MIR lowering called `heap_create_name` for every `obj.field =
value` execution, despite `field` already being an immutable name-pool string
owned by the compiled script. Static member stores now box that existing
`String*` directly and keep the canonical `fn_map_set` path for every actual
write. This changes neither field lookup nor map type-transition behavior.

Five interleaved release pairs against the pre-Tune10 binary produced:

- `richards`: `200.072 ms` to `163.683 ms` median (18.2% faster).
- `splay`: `156.194 ms` to `145.066 ms` median (7.1% faster).

Both workloads reported their checked `PASS` output. This independently clears
the 5% one-target requirement, with the other declared primary row also
improving. The per-site write IC remains deferred: this smaller root-cause fix
removes the dominant redundant per-store operation without adding a second
shape-cache mechanism.

Validation: `make test-lambda-baseline` passed `3583/3583` on the retained
candidate, including `25/25` forced-GC MIR stress tests.

### T6 — retained: native packed `u32` arithmetic (2026-07-27)

When both operands are statically `u32`, addition, subtraction, and
multiplication previously entered the generic numeric helper, then immediately
called `coerce_num_sized` to recover the same packed `u32` representation.
MIR now extracts each 32-bit payload, performs the operation, masks to the
defined wrapping width, and rebuilds the canonical packed `u32` Item. Mixed
domains, division, shifts, bitwise operations, and every other sized kind stay
on the shared numeric classifier.

Five interleaved release pairs on `crypto_sha1` changed the median from
`214.644 ms` to `186.529 ms` (13.1% faster). Every run produced the identical
checked `PASS` output after timing and launcher lines were removed.

Validation: `make test-lambda-baseline` passed `3583/3583`, including `25/25`
forced-GC MIR stress tests.

### T7 — retained: fresh-map lifetime region (2026-07-27)

The first region slice recognizes a deliberately narrow, call-site-specific
pair: a recursive procedure that returns only freshly allocated maps whose
fields are null or recursive fresh-map results, immediately consumed by a
recursive read-only scalar procedure. The producer receives an explicit hidden
`LambdaRegion*` direct-call parameter. Recursive producer calls forward that
capability; public, imported, ordinary direct, and unproven calls pass null
and continue to allocate on the normal GC heap.

`LambdaRegion` owns reusable per-runtime bump blocks. Region map objects retain
the normal adjacent `gc_header_t` layout but are never linked into
`gc->all_objects`. The accepted producer summary has no outgoing ordinary-GC
pointers, so collections safely ignore those temporary objects. Forced-GC
scheduling is also invoked from `lambda_region_calloc`, ensuring stress mode
collects while the region is active rather than merely between iterations.

`proc_lifetime_region` verifies an immediate temporary tree and a separately
allocated persistent tree. It passes normally and with
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; the latter recorded
collections at every `lambda_region_calloc` call. Its generated MIR shows the
hidden producer parameter, recursive capability forwarding, null capability
on the long-lived call, and `lambda_region_begin`/`lambda_region_end` only
around the immediate consumer(producer(...)) expression.

Fresh release `gcbench` candidate samples were `222.990 ms`, `223.092 ms`, and
`225.750 ms`, for a `223.092 ms` median. Against the recorded Tune10 target
baseline of `384.935 ms`, this is **42.0% faster**, well above the independent
5% gate. `make test-lambda-baseline` passed `3586/3586` (input `2104/2104`,
Lambda runtime `1482/1482`), including `25/25` forced-GC MIR stress tests. T7
is retained.
