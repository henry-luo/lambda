# JS MVP integration implementation

**Date:** 2026-10-21  
**Authority:** **D8.1.3v27**, **D5.3–D5.4**, **D8.4.1v2–D8.4.3v2**  
**Design:** [JS MVP integration](../jube/JS_MVP_Integration.md)  
**Status:** implementation and qualification in progress; integration is not closed.

## Changes

### Common entry and ownership

- `lambda/main.cpp` normalizes `--runtime=mvp-lmd` into the ordinary JS
  command. File/eval/print, module selection, arguments and explicit AST/AUTO
  policy use the same parser and execution path.
- `-p` prints the actual Script completion after execution. It no longer
  changes the source grammar by wrapping the program in `console.log(...)`.
  Both selectors use the former explicit MVP value formatter, including
  quoted strings and structured array output.
- A CLI Script with no document or subsequent interactive turn supplies a
  terminal-entry lifetime fact. The compiler consumes that fact once.
  Source dependencies can still require a shared, mutable realm.
- `MvpLmdExecution` borrows the caller's canonical context. Standalone API
  clients allocate an ordinary `Runtime` and use `js_prepare_eval_context`;
  the embedded `EvalContext` and private heap/context teardown are removed.
  JS capsules remain lazy for programs that do not need them.
- `runtime_cleanup` retires jobs and heap resources before MVP compiler
  metadata is released. The existing Script retention path owns public
  executions and escaping callbacks.

### Canonical globals and intrinsic identity

- The first MVP unit adopts a prepared global object directly and publishes a
  new global only when the realm has none. The descriptor-copying
  `mvp_lmd_import_host_globals` bridge and Window retargeting are removed.
  Existing host descriptors, aliases and object identity survive entry.
- AUTO satellites use the same adoption path. The duplicated
  post-instantiation global swap is removed. V32 also removes late prototype
  replacement and adopts ordinary, collection and iterator families before
  construction. V33 passes the full 213-case Script/AUTO suite.
- Native constructor and prototype lookups borrow initialized MVP intrinsic
  slots. Guest replacement of `Object` or another global does not alter
  intrinsic identity. Prototype lookup checks callable ABI before accessing a
  `JsFunction` tail, as required by **D6.2.2v2**.
- Native Math, JSON, Reflect, Atomics and console getters consult those same
  slots. A prepared native realm retains its existing console and performance
  objects and methods. This consolidates lookup; remaining construction and
  host-service adapters are tracked below.
- The v36 console audit confirms one observable namespace in either entry
  order. An MVP-created console still lacks extended native methods such as
  `assert` and `Console`; the archived Result52 normal and explicit entries
  both return `"undefined:undefined"` for their types. This is a retained
  host-surface gap, not a second namespace. Broader method coverage follows
  performance qualification under the design's delivery order.
- Lazy global publication uses the shared storage writer for either carrier.
  The old MVP-only writer rejected a prepared native global.
- The common Promise implementation's private resolving, capability, await
  and combinator records have null prototypes. They must not inherit setters
  from the canonical guest `Object.prototype`. Public result records retain
  their ordinary prototypes and own-data property semantics.
- Lazy FileReader registration also uses null-prototype internal descriptors.
  A standalone Promise's first native callback initialized host globals after
  guest code had installed an inherited `value` setter. The three FileReader
  constants incorrectly invoked it with `0`, `1`, and `2`; the descriptor
  construction now bypasses that guest prototype chain.

### Canonical binding index

- `JsGlobalEnvironment` now indexes top-level lexicals from compiled units,
  interpreter module slabs and hosted Script environments. Entries reference
  their existing owned slots or capture cells; values are not copied between
  environments. Native host lexical APIs use the same storage readers and
  writers, including scalar retention across later operands.
- Dynamic lookup no longer scans every compiled unit or interpreter scope
  chain. The interpreter chain remains solely as the precise lifetime owner
  of its cells. Child realms use the same registry implementation with their
  own records. Retiring an MVP realm removes borrowed storage references
  before releasing interpreter environment roots (**D5.3**, **D5.4**).
- Compiled local and resolved-cell accesses are unchanged. Registry creation
  and publication occur at entry for programs requiring dynamic source.
  V22 passes all 815 MVP/public execution cases, including cross-entry,
  TDZ, const and realm coverage. V21 exposed a borrowed scalar home during
  reentry; the common reader now retains scalars from every storage kind.
- The initial registry still searched its rows linearly. V55's `jq_records`
  MIR emits 767 dynamic-global calls, versus none in the fast control;
  its checked instrumented execution remains slower
  (`jq-records-mir-v55/results.json`). V56 retains live object-property reads
  for declared Script functions: their non-configurable properties prevent
  later lexical shadowing, while reassignment still changes the callable.
  This removes 460 dynamic-global calls. Satellite wrappers and unresolved
  names retain their existing handling. Three focused cases, all **823 MVP
  cases**, and all **111 optimization contracts** pass
  (`global-function-focused-v56.json`, `mvp-v56.json`, `opt-v56.json`).
- V56's remaining global misses still scan the canonical registry. A release
  profile observes `js_global_environment_find` among the sampled helpers;
  it also includes lazy compilation and generator activation costs, so it
  does not establish that lookup explains the entire timing gap
  (`jq-records-profile-v56/`). V57 adds a hash index of binding kind, spelling
  hash and row number to that same registry. Stored index entries contain
  no borrowed `Item` key or copied value; comparisons resolve keys through
  the existing precise roots. Growth preserves row numbers, removal drops
  the derived index, and heap replacement clears it. Allocation failure
  retains complete linear lookup over the authoritative records.
- V57's five focused cases pass, including equal pooled/unpooled keys,
  deliberate hash collisions, upsert identity, binding kinds, borrowed
  storage, row compaction, and heap replacement
  (`binding-index-focused-v57.json`). This preserves **D5.3/D5.4** ownership
  and the semantic-miss contract of **D8.4.1v2**. The complete MVP suite
  passes **824/824**, optimization contracts pass **111/111**, and both CLI
  spellings pass **136/136 pairs** (`mvp-v57.json`, `opt-v57.json`,
  `entry-v57/results.json`). Fresh aggregate gates and release performance
  qualification remain required.
- The V57 eight-row screen passes every output check. `jq_records` measures
  **0.980×** the archived fast control, but later pairs overlap V58 builds and
  tests. Binary and private-module hashes remain stable; the source/build
  check correctly fails. This is preliminary recovery evidence, not acceptance
  (`remaining-diagnostic-v57.json`, `remaining-diagnostic-v57-context.json`).

### Suspended function intrinsics

- A native-first probe on v22 found four identity mismatches: generator
  functions, generator instances, async functions and async-generator
  functions. The native and MVP initializers each constructed these families.
- V23 changes suspended-family initialization to borrow the existing native
  realm's constructor and prototype records. Native factories likewise
  consult an initialized MVP realm. Intrinsic constructors have precise
  slots independent of guest changes to a prototype's `constructor`
  property (**D5.3**, **D5.4**).
- Native generator methods dispatch to the receiver's existing activation
  implementation; the extracted MVP resume helper keeps its original
  completion and scalar ownership behavior. V24 passes 815/815, including
  identity and `next`/`return`/`throw` across both entry orders. A native class
  metadata check initially rejected MVP receivers; it now recognizes the
  common generator carrier.
- The expanded native-first probe also found that async results used a second
  Promise constructor. V25 borrows the existing Promise constructor and
  prototype before initialization, avoiding construction of the duplicate
  family. V26 passes all 815 unit cases and all 12 native-first diagnostic
  checks, including async and async-generator Promise identity.
- The async check exposed an unregistered DOM cache publication: the host
  document setter could store `defaultView` before lazy root registration,
  which then cleared it. V25 also fails when the test uses that public setter.
  V26 registers the shared cache before publishing and limits its root span
  to the four actual `Item` fields (**D5.3–D5.4**).
- A separate native-first probe found two strict-arguments thrower identities.
  V27 adopts the canonical Function constructor, prototype and immutable
  method/thrower bindings before initialization. It also borrows the prepared
  Math, JSON, Reflect and Atomics namespaces without constructing replacement
  methods. V27 passes 815/815 and the strict-arguments identity reproducer.

### Native constructors and value carriers

- V28 adopts buffer, typed-array and weak-collection families through one
  constructor/prototype helper. The existing native intrinsic-prototype
  reader is shared rather than copied. Promise adoption uses the same helper.
- Expanded native-first coverage exposed an existing failure in
  `class B extends Uint8Array`: class initialization required an MVP callable
  metadata record even when the base constructor already had a supported
  native ABI. It fails on both v27 and v28. V28 therefore passes 814/815;
  the new subclass assertion remains failing rather than being excluded.
- V29 lets native constructors retain their allocation and `newTarget`
  handling through the common ABI, with explicit live ancestry links.
  This follows the callable-layout distinction in **D6.2.2v2**.
- A separate v27 probe found that native RegExp methods/getters rejected
  compiled literals, and native Number methods rejected MVP's boxed `this`.
  V29 adopts the existing RegExp family and iterator prototype, routes
  compiled regex receivers to their existing matcher, and shares the realm's
  legacy match record. Native-first primitive boxing now uses the shared
  boxed-value constructor. Borrowing an initialized iterator prototype also
  preserves guest changes to its parent.
- V29 passes all eight native storage probes and all nine RegExp/boxed-value
  probes. The full suite passes 813/815: canonical RegExp legacy properties
  are absent from descriptor lookup, and an invalid regex receiver crosses
  to the native error path. These failures remain recorded.
- V30 publishes native RegExp legacy accessors as real descriptors and keeps
  invalid receivers in the caller's error path. A v29 reverse-entry test also
  reproduces distinct strict-arguments throwers and separate regex capture
  records. V30 reads canonical Function slots in either entry order and uses
  the context's one top-level match record; child realms retain separate
  records (**D5.4**, **D6.2.2v2**). V30 passes 815/815 and all 17 expanded
  native-first diagnostic checks.
- V31 adopts primitive and error constructors before installing methods,
  removes their late prototype replacement, and dispatches caught-error
  construction through the declared callable ABI. It passes 815/815,
  including native-first Number/Error subclasses, native exceptions,
  compiled conversion failures and precise GC.
- V32 adopts Object, Array, Date, Symbol, Map, Set, sequence iterator families,
  Proxy and the prepared test host before creating replacement objects.
  The obsolete `mvp_lmd_borrow_native_prototypes` pass is removed. Existing
  native namespaces and guest mutations remain owned by the prepared realm.
  It passes 814/815 MVP cases and 80/80 CLI entry pairs. Its first prepared-host
  failure also causes later script-suite context failures after an assertion
  skips cleanup. The isolated AUTO realm/promotion test passes.
- The v32 release trace reaches the packed-only writer while publishing
  `$262.global` on the adopted native host object. V33 makes named
  initialization writes dispatch by the object's existing storage owner;
  MVP-owned objects retain the packed write. V33 passes 815/815 MVP cases,
  213/213 Script/AUTO cases and 80/80 CLI entry pairs. An additional reverse
  RegExp carrier check also passes. The test harness and its cleanup policy
  are unchanged.
- V34's new global-factory identity check fails on v33 for the published
  parser, URI, timer and other host functions. The native factory creates
  alternatives to MVP intrinsics, and the import path wraps native callables
  despite their supported ABI. V34 shares immutable global-function records,
  preserves native callable identity and borrows prepared timer functions
  before construction (**D6.2.2v2**, **D8.1.3v27**). Its aggregate run stops
  after 32 MVP passes and at the first Script case; entry checks pass 78/80.
  The invocation dispatcher still expects import to return a wrapper and
  recursively invokes the unchanged native callable. The release debugger
  records repeated recursive frames in `script-v34-crash-bt.log`.
- V35 dispatches the imported callable by its published ABI, removing that
  recursion. The performance host family also completes the existing MVP
  namespace instead of allocating a second object; `now` and `timeOrigin`
  retain their original identity and clock origin. It passes 213/213
  Script/AUTO cases and 80/80 entry pairs; MVP qualification is 812/815.
- The remaining v35 failures expose native function enumeration entering
  MVP-only metadata preparation, a Promise child-realm result with the wrong
  prototype, and an import assertion requiring the retired extra wrapper.
  V36 leaves native descriptors with their existing kernel and restores the
  callee's home realm at native invocation. The import check now requires
  canonical callable identity and a successful call after GC, alongside its
  existing early-error and namespace assertions (**D5.4**, **D6.2.2v2**).
  V36 passes 815/815 MVP cases, 213/213 Script/AUTO cases and 80/80 CLI
  entry pairs. Its first MVP run passes 814/815: the new post-GC assertion
  assumes a field that the existing Lambda fixture does not expose on either
  v33 or v36. The final test calls that fixture's existing wide-integer export
  after GC and checks its exact value. Both attempts and the control
  comparison are retained; the runtime is unchanged between them.
- V37 removes the native RegExp operation dispatcher introduced while
  unifying receiver carriers. Each published callable now enters its
  semantic kernel directly; getter entries share the carrier/brand check,
  and protocol callbacks use the catalog's executable getter (**D6.2.2v2**).
  It passes 815/815 MVP cases, 213/213 Script/AUTO cases and 80/80 CLI entry
  pairs. The callable census falls from 66 to 38 semantic cases, matching
  HEAD's remaining cases; its zero-case ratchet still fails. The census
  and its threshold are unchanged.

### Local numeric-region admission

- The existing numeric-region planner rejected every function when the unit
  enabled property callbacks. V26 removes that unit-level veto and explicitly
  rejects functions needing an eval environment. The existing per-function
  effect proof and own-data descriptor/type guards still decide admission
  (**D8.4.1v2**, **D8.4.2v2**).
- This change preserves the generic body on a guard miss and does not replay
  source. It does not yet broaden the planner to loops or indexed arrays.
  Qualification includes dynamic source beside a numeric kernel, accessor
  misses, null receivers, changed value types and an unchanged release
  control for MIR/timing diagnostics.
- V26 passes 815/815. The dynamic-source probe produces the same checked
  result and calls the fallback accessor exactly once. Its MIR includes the
  guarded numeric body alongside the generic body. Seven alternating release
  pairs have medians of 94 ms on v25 and 82 ms on v26; these measurements ran
  under concurrent load and are diagnostic only. The samples, MIR and binary
  hashes are retained in `dynamic-region-comparison-v26.json`.
- V42 narrows the dense-loop and ordered Number-presence vetoes to functions
  needing an eval environment. Existing binding, effect and storage proofs
  still exclude captured/external bindings and observable operations. Dense
  misses restore only unobservable local writes before entering the complete
  generic continuation; ordered facts are invalidated by writes
  (**D8.4.1v2**, **D8.4.2v2**).
- New emission/behavior fixtures retain holes, throwing inherited getters,
  NaN, infinity, negative zero, conditional writes and a function with its
  own direct eval. Node, V39 and V42 agree on both outputs. The dense fixture
  now contains the guarded and generic bodies; the ordered fixture reduces
  generic property-key calls from three to one. These are MIR/semantic
  observations, not performance acceptance. `loop-locality-v42-contract/`
  retains the diagnostic artifacts. All **109/109 optimization contracts**
  pass in `opt-v42.json`; final aggregate gates remain open.
- V55 removes the remaining unit-wide dynamic-source veto from the
  integer-write inventory. The proof now rejects a binding's own eval
  environment, captured/external cells and incomplete or fractional writes.
  In an unrelated function it eliminates one integer-to-double round trip,
  equality test and branch per indexed iteration. Bounds and semantic
  property misses remain. The new `mvp_local_integral_index` contract checks
  inherited and throwing getters, effectful loop-bound conversion, NaN,
  negative zero and fractional writes; both V54 and V55 produce the same
  checked output (`integral-index-v55-comparison.json`). The complete MVP
  suite passes **822/822** (`mvp-v55.json`). This implements the local-proof
  requirement in **D8.1.3v27** and **D8.4.1v2**; release performance
  qualification remains outstanding.
- V58 removes a redundant object-to-primitive stage from relational
  comparisons with one proven Number operand and no admitted BigInt domain.
  Such comparisons cannot select string ordering. The existing ToNumber
  miss still owns the other operand's conversion and exception; neither
  operand is reevaluated (**D8.4.1v2**, **D8.4.2v2**).
- The new fixture checks all four operators in both directions, numeric
  strings, holes, NaN, infinities, negative zero, conversion counts, Symbol
  errors, thrown-value identity and operand evaluation order. Node, V57 and
  V58 agree. Its eight comparisons retain all eight complete numeric
  conversions while removing eight separate primitive conversions and 150
  MIR instructions (`numeric-comparison-v58-review.json`).
- V58 passes **824/824 MVP cases**. Its first optimization run passes
  100/112: twelve existing fixtures require fourteen lower primitive-call
  counts. All thirteen affected/new sources match Node and V57; their helper
  census changes only the removed primitive calls and renamed indirect
  registers. V59 records those reviewed counts and passes **112/112
  optimization contracts** and **136/136 CLI pairs**. V58 and V59 release
  binaries have identical hashes; V59 changes only the emission contracts.
  The audit initially assumed every fixture printed a single `OPT_OK` line;
  two fixtures print additional matching values. The generalized audit
  checks exact three-lane output equality and the final success marker.
  Both attempts remain in `numeric-comparison-v58-audit/` and
  `numeric-comparison-v58-audit-b/`; runtime assertions are unchanged.
- Seven fresh release pairs on the canonical sources measure **0.952×
  pnpoly**, **1.026× Havlak**, and **1.051× prettier_ast** against archived
  fast MVP. All outputs and source/build/binary/module checks pass
  (`remaining-short-diagnostic-v59.json`). These three rows do not replace
  the full protected population or establish final acceptance. The remaining
  `prettier_ast` gap still needs investigation.
- V67 identifies a separate open-Script limitation: the numeric-region
  proof rejects every loop, including a small scalar loop whose only writes
  are unescaped locals. `scalar-loop-v66-before/` reproduces its per-iteration
  generic conversion, with unchanged output in Node and V66.
- V68 admits small scalar loops through that existing proof and guards
  Number arguments once at the callable entry. A miss selects the original
  generic body before any iteration. Eval environments, captured locals,
  parameter writes, property paths, calls, allocations and receiver
  observations remain outside this loop proof (**D8.1.3v27**,
  **D8.4.1v2**, **D8.4.2v2**).
- The initial V67 diagnostic matches Node and V66, including conversion counts
  and thrown identity, but its new region also enabled repeated call-site
  expansion. V68 keeps this new guard at the callable entry while preserving
  previously eligible ordinary inlining. It adds for/while/do/nested-loop,
  shared-binding replacement, retained-function GC, receiver-coercion and
  BigInt-miss coverage.
- V69 passes **828/828 MVP**, **116/116 optimization** and **136/136 CLI
  pairs** (the CLI capture uses the identical V68 release). Its Node/V66/V68
  audit changes only the loop function; the caller MIR is unchanged.
  `scalar-loop-v68-audit/`, `mvp-v69.json`, `opt-v69.json` and
  `entry-v68/results.json` retain the evidence.
- The JS MIR suite passes **35/36** in V69: `nested_var_accumulator` catches
  duplicate arithmetic in a parameterless loop that was already native.
  V70 requires the argument guard to add a reachable Number fact before
  emitting a second scalar loop body. Parameterless and already-numeric
  functions keep their original body; the existing emission contract is
  unchanged (**D8.6.1**).
- V70 passes **828/828 MVP**, **116/116 optimization**, **36/36 JS MIR**
  and **136/136 CLI pairs**. `scalar-loop-v70-audit/` confirms identical
  Node/control/candidate output, unchanged caller MIR for the new fixture,
  and entirely unchanged MIR for `nested_var_accumulator`.
- A separate synthetic million-iteration loop beside dynamic source has
  release execution medians **2.117 ms V66** and **0.689 ms V70** across
  three alternating pairs after warm-up. All outputs match Node, and source,
  build, binary and native-module checks pass
  (`scalar-loop-diagnostic/v66-v70.json`). This diagnoses the local guard's
  benefit; it is not a canonical benchmark acceptance result.
- V70 source and release are frozen for the next aggregate correctness run:
  `lambda-unified-v70.exe`, SHA-256
  `85b09052851882623acaf98da1f8ea0c989c1911ec3994b3f2da2aed158e12a6`,
  with 3,268 source files in `source-v70.json`.

### Native array and exception boundaries

- A V42 AUTO probe creates an indexed accessor in the native interpreter,
  then promotes a dense comparison loop. The first four calls return `22`;
  promoted calls incorrectly return `12` and omit the getter. Native
  descriptor installation leaves the former dense element alongside the
  companion accessor. `native-array-loop-v42.json` records the mismatch
  against Node, AST and whole-Script MIR.
- V43 extends the existing descriptor-finalization helper to remove obsolete
  dense and sparse element storage when the companion accessor owns the
  index. The existing hole miss reaches the complete property operation;
  there is no new hot-loop guard or global optimization veto
  (**D8.4.1v2**, **D8.4.2v2**).
- Expanded AUTO coverage finds that a native Error thrown by the getter loses
  identity when MVP catch materialization constructs a replacement. V44
  recognizes the shared Error carrier's existing object representation and
  preserves it across the propagation boundary (**D8.4.3v2**). The regression
  retains getter counts, exact thrown identity, GC, accessor-to-data updates,
  undefined getters and sparse indices. V43's failed run remains recorded in
  `descriptor-focused-v43.json` and `native-array-v43.json`.
- V44 passes both focused descriptor regressions and all **816/816 MVP**
  cases (`descriptor-focused-v44.json`, `mvp-v44.json`). The completed Lambda
  aggregate passes **7,898/7,898**, including **215/215 Script/AUTO** cases;
  other qualification gates remain open. The native getter method currently stays interpreted after
  satellite source parsing rejects its method syntax; the regression checks
  that the consumer function actually promotes to MVP.

### Host dependencies

- `require` delegates to the shared loader and namespace cache.
- `process` uses the shared profiled object and argument service. It is
  materialized when requested, preserving lazy host initialization.
- Foreign native objects use a receiver guard at property access. A foreign
  receiver enters the complete existing property operation; an MVP receiver
  keeps the existing fixed operation. The call carries the shared emitter's
  GC/reentry metadata.
- Unknown host globals, external source loaders and native promise
  continuations select the complete realm before execution. There is no
  whole-script retry after side effects.
- Built-in status alone is insufficient proof that a module cannot introduce
  source. The Jube catalog carries an optional `script_source: "closed"`
  contract, attested by the size-gated `JubeModuleDef.script_source_closed`
  tail before activation. Missing contracts are conservative. Node FS
  attests that its operations perform native I/O and invoke supplied
  callbacks without evaluating or loading new JavaScript. This contract
  does not claim purity, absence of callbacks, or absence of GC.

### Closed-script native call admission

- V44's release entry matrix passes 80/80 pairs, but its canonical diagnostic
  workloads fail at `process.stdout.write` in both default and explicit modes.
  V33 succeeds with both the current root modules and a fresh private release
  bundle; V37–V44 fail with either bundle. `stdout-boundary-v44.json` and
  `mir-v44/` preserve these results. No performance conclusion is taken from
  the failed capture.
- Canonical host functions retain their native ABI. The closed-script call
  emitter still rejects that ABI unless the unit enables property callbacks.
  V45 sends this ABI miss to the existing complete invocation helper, with
  the original receiver and already evaluated arguments (**D6.2.2v2**,
  **D8.4.3v2**). Its cold entry fills the same precise argument-root span;
  the successful MVP guard and call sequence remain intact (**D5.3**).
- Argument-span emission is shared with sequence methods. New native-host
  coverage uses the default output sink, a captured writer and the closed FS
  module. The previous host-output fixture installed an MVP writer for its
  custom sink, which did not exercise the canonical native callable. The CLI
  entry matrix now also includes `process.stdout.write`.
- V45 passes **817/817 MVP cases**, **109/109 optimization contracts** and
  **84/84 release entry pairs**. The first optimization attempt used the root
  release executable without profiling hooks; its failed capture is retained
  in `opt-v45.json`. The supported `LAMBDA_JS_OPT_EXE` selector points the
  passing rerun at the private V45 debug executable (`opt-v45-debug.json`).
- V46's unchanged Lambda aggregate completes **7,896/7,899** with three
  emission-size failures; source and binary inputs remained stable
  (`qualification-v46-lambda.json`). All runtime behavior cases pass.
  The additional native-call fallback grew generator-basic by 77 MIR
  instructions, dynamic-catch by 2 and exact-collection by 7. V47 admits
  that branch only when the existing native-host dependency fact permits
  native callees; receiver ABI alone is insufficient. The size budgets
  remain unchanged; all **20/20** pass on V47 (`ratchet-v47.json`).
  V47 also passes **818/818 MVP** and **109/109 optimization** cases
  (`mvp-v47.json`, `opt-v47.json`). Its unchanged Lambda aggregate passes
  **7,900/7,900**, with stable source, executable and module hashes
  (`qualification-v47-lambda.json`).
- V60 routes a captured non-MVP callable ABI directly to the existing complete
  invocation helper. That helper already validates native JS and unsupported
  ABIs before accessing MVP storage, so a second emitted ABI comparison and
  capability failure are redundant (**D8.4.1v2–D8.4.3v2**). The native miss
  continues to fill the original argument-root span without re-evaluation.
  The new `NativeCallGuard` contract checks native and guest callees, invalid
  calls, argument effects and thrown-value identity against Node and V59.
  Its MIR removes one failure call and one net branch.
  V60 passes **824/824 MVP cases** and **113/113 optimization contracts**.
  `native-call-guard-v60-audit/`, `mvp-v60.json` and `opt-v60.json` retain the
  evidence.
- The separate V60 release CLI size check passes **19/20**; the default
  `lambda_cow_nested_store` budget reports 169 instructions against 166.
  V59 and V60 emit identical opcode/helper counts and output for that Lambda
  fixture (`ratchet-cow-v60-review/`). This check used the release/default
  profile; the aggregate's normal Darwin debug profile remains to be run.
  The failure is retained in `ratchet-v60.json`; no budget was changed.
- V61 excludes proven primitive receivers from native-object property
  dispatch, while unknown receivers and callable results retain their ABI
  guards. Its new fixture exposes an existing capability error for named
  primitive properties: V60 and V61 fail where Node returns `undefined`.
  V62 admits the required primitive prototype families for proven primitive
  receivers outside the direct method/index catalog. The closed entry still
  fails because inlining a string receiver discards the named key. V63
  retains that key on the semantic miss. The final fixture matches Node,
  while its V60 control remains failed (`primitive-receiver-v63-audit/`).
  These repairs reuse the full realm prototype lookup and preserve direct
  string indices and method calls (**D8.1.3v27**, **D8.4.1v2**).
  Fresh embedded and closed-entry cases cover strings, numbers, booleans,
  BigInts, Symbols, method values and mixed primitive parameters.
  V63 passes **826/826 MVP cases** and **114/114 optimization contracts**
  (`mvp-v63.json`, `opt-v63.json`). Its formatter MIR matches V60 exactly
  (`primitive-host-mir-v63/`), so this repair does not explain the formatter
  performance gap.

### Direct ownership guards for shared field reads

- V60's amplified formatter profile identifies general Item ownership
  classification among the sampled runtime work (`prettier-profile-v60/`).
  Its link map is applied only after proving identical release text bytes and
  addresses. The amplified output matches the canonical oracle; these
  instrumented timings are not acceptance evidence.
- The own-field emitter has already checked the Map carrier and loaded its
  shape before calling `mvp_lmd_owns_properties`, which repeats that work.
  V64 reads the same nominal extension from the proven shape and branches
  to the existing semantic miss for foreign ownership. It reuses this guard
  with ordinary-function instance checks. Descriptor, accessor, scalar-home
  and GC ownership rules remain unchanged (**D8.4.1v2**, **D3.4.6**, **D5.3**).
- Five affected sources match Node and V63 exactly. Helper-census differences
  contain only the removed ownership calls; the primary contract now checks
  the nominal load and extension branches before shared shape lookup.
  `ownership-guard-v64-audit/` retains each source, MIR and comparison.
- V64 passes **826/826 MVP**, **114/114 optimization** and **36/36 JS MIR**
  cases. The checked canonical MIR removes **521** ownership-call sites from
  Havlak and **161** from `prettier_ast`, adding two MIR instructions per site
  (`ownership-mir-v64/`). Timing and final aggregate acceptance remain open.
- V64's seven-pair canonical screen measures Havlak **1.01160×** (upper
  **1.02987×**), pnpoly **0.94591×** (upper **0.95699×**) and formatter
  **1.02648×** (upper **1.03421×**). All outputs and source, executable,
  build and native-module checks pass. Two formatter extensions retain all
  **49** pairs: **1.02745×**, upper **1.03161×**. The formatter row remains
  outside the unchanged 1.03× upper-bound limit. The three-row geometric
  mean **0.99435×** (upper **1.00227×**) does not replace full-population
  acceptance (`ownership-short-extended49-v64.json`,
  `ownership-short-report49-v64.json`).

### Shared string equality kernel

- The formatter profile also identifies string comparison work. MVP equality
  computes lexical ordering and then compares that result with zero. V65
  extracts the existing shared strict-equality string branch into
  `js_string_equal`, used by both the semantic runtime and the MIR string
  equality branch. Pointer and identical-byte matches remain direct; an
  ASCII byte mismatch returns false without computing lexical order.
- Non-ASCII mismatches retain the shared UTF-16 decoder: canonical UTF-8 and
  concatenated WTF-8 surrogate pairs can represent equal JS strings despite
  different byte lengths (**S1.11**). The helper allocates nothing, invokes no
  guest code and returns the scalar MIR ABI. Relational ordering still uses
  its original comparator (**D8.4.1v2**, **D5.3**).
- New coverage checks both physical encodings across a forced collection,
  embedded NULs, unequal prefixes, lone surrogates, equality operators,
  switch cases, operand evaluation and UTF-16 relational ordering. The MIR
  contract distinguishes equality calls from ordering calls.
- V65 passes **827/827 MVP**, **36/36 JS MIR** and **136/136 CLI pairs**.
  Its first optimization run passes **114/115**: the new contract initially
  counted only four operand comparisons, omitting four comparisons with its
  boxed expected parameter. The reviewed Node/V64/V65 fixture has identical
  output; each of its eight former ordering calls becomes equality, removing
  eight MIR equality-with-zero instructions. V66 records that exact count;
  the earlier failed contract remains in `opt-v65.json`. Performance and
  final aggregate qualification remain pending.
- The initial V66 optimization rerun overlapped the release relink. Its MIR
  harness still invokes root `lambda.exe`, even when profile capture selects
  the private debug CLI; 106 cases failed to launch that executable.
  `opt-v66.json` retains this scheduling failure. Subsequent optimization
  runs wait for the release build to finish. V65 and V66 release hashes are
  identical; only the reviewed sidecar changes between their source pins.
- The serialized V66 run passes **115/115 optimization contracts**
  (`opt-v66b.json`). Its candidate release is
  `lambda-unified-v66.exe`, SHA-256
  `aefec92f794905269a9378cd13fba4ff7b7b530e4eb452ba50b4dc53b39a573a`.
- Seven canonical pairs in `string-equality-short-v66.json` pass all three
  diagnostic row limits with unchanged outputs and stable provenance:
  Havlak **1.01338×** (upper **1.02447×**), pnpoly **0.93784×** (upper
  **0.95098×**) and formatter **0.97829×** (upper **0.98698×**).
  The three-row geometric mean is **0.97602×**, upper **0.98390×**.
  This is a fresh archived-fast/V66 comparison, not a direct V64/V66 A/B.
  Full-population qualification remains deferred until the implementation
  changes are complete, following the user's timing guidance.

### Optional process extension

- V46's same-binary diagnostic under full and minimal host profiles isolates
  approximately 2.5 ms of optional Node-core activation on the three
  diagnostic workloads (`process-startup-v46/results.json`). These measurements
  overlapped correctness qualification and are diagnostic only.
- V47 uses the existing canonical base process object for closed direct
  `process.stdout.write(value)` / `process.stderr.write(value)` calls.
  It defers the optional extension only after proving every process use has
  this shape. Prepared/shared realms, custom output sinks, process or writer
  escapes, extra arguments, module dependencies, coercion callbacks,
  reflection, dynamic source, constructor access and unknown property keys
  retain the complete profile. Numeric indexing remains admissible.
  This changes initialization dependencies, not namespace ownership
  (**D5.4**, **D8.1.3v27**). The three focused host cases pass, including
  ten admission/refusal probes that inspect actual Node-core attachment
  (`process-focused-v47.json`). Release qualification is pending.
- The source review also exposes failures for direct and computed
  access to an ordinary function's `constructor` followed by dynamic
  construction (`process-admission-v46/results.json`). Both fail on V46
  before this optimization. Archived normal JS passes both, while archived
  fast MVP fails both (`process-admission-v46/archived-reference.json`).
  These are integration regressions when fast MVP becomes the default.
  V48 detects constructor reads on solved function receivers after planning
  and rebuilds the uninstantiated plan with complete shared realm bindings.
  Named `constructor` reads also admit possible function receivers; computed
  keys require a solved function receiver and a key that can name the
  constructor. Numeric indexing and constructor assignments alone do not
  trigger this admission. The selected backend remains MIR, with no guest
  execution or side-effect replay before replanning (**D8.1.3v27**).
  V48 passes **819/819 MVP cases**, including ten constructor probes
  covering aliases, closed-call parameters, returned functions, class
  constructors, lexical updates and exactly-once effects. Its release CLI
  matrix passes **92/92 pairs**, including both original regressions under
  unset, MIR, AST and AUTO policies (`mvp-v48.json`,
  `constructor-focused-v48.json`, `entry-v48/results.json`).
  Aggregate and complete performance qualification remain open.
- Follow-up V48 release probes reproduce three more regressions against
  archived normal JS: named and computed destructuring of a function's
  constructor, and computed constructor access through an object method.
  `constructor-coverage-v48.json` preserves both lanes. The V48 aggregate
  preparation was stopped before any aggregate gate began; it provides no
  acceptance evidence (`regressions-v48-interruption.json`).
  V49 includes property-pattern reads and follows function definitions through
  generic method projections and aliases before selecting the complete realm.
  Numeric indexing retains its existing plan. Four focused test cases pass,
  including the additional declaration, assignment, parameter and class-method
  forms (`constructor-focused-v49.json`). The expanded suite passes **819/819**
  (`mvp-v49.json`); the V49 release entry matrix passes **104/104 pairs**.

### Closed global alias stores

- V49's 68-row implementation screen passes every output check, but exposes
  large preliminary slowdowns in `text/fast_diff` (2.806×) and
  `text/microdiff` (1.182×) against archived fast MVP. These are one-pair
  diagnostics, not qualification: indexing was active, and V50 compilation
  overlapped the tail. The binary and private native-module hashes remained
  stable; the build-manifest check correctly reports the source/build change.
  `standard-diagnostic-v49.json` retains all 68 rows. The final protection
  population remains all 71 rows, including every long jq workload.
- `text-mir-v49/` shows the compiler cause for `fast_diff`: the archived fast
  lane has 177,098 MIR instructions and no dynamic-global calls; V49 has
  222,280 instructions and 200 such calls. Both sources install unused
  self-aliases on the global object. Temporary source variants that remove
  those stores, or replace their values with primitives, preserve checksums
  and restore the fast plan (`global-alias-cause-v49/`). These modified-source
  timings are causal diagnostics only.
- V50 defers admission for a top-level named self-alias store. After inference,
  it requires the alias to remain unobserved, refuses receiver-dependent
  `this` and property callbacks, and reuses `plan_fixed_global_bindings` to
  prove that computed keys cannot reach native names or realm bindings.
  A failed proof selects the complete realm before lowering and execution.
  This follows **D8.1.3v27** and **D8.4.1v2** without replay or a second
  environment.
- V50 passes **820/820 MVP cases**, including semantic and MIR checks for the
  admitted store and refusal cases involving observed aliases, descriptors,
  setters and function-local installation (`alias-focused-v50.json`,
  `mvp-v50.json`). Three paired release samples recover `fast_diff` to
  **0.989×** archived fast MVP; `microdiff` remains **1.155×** and Havlak
  **1.047×** (`text-havlak-diagnostic-v50.json`). These are implementation
  diagnostics; the complete protection population remains outstanding.
- V50's release stack and MIR captures isolate two further costs.
  `microdiff` emits 81 dynamic-global operations, versus none in archived
  fast MVP. Removing only the aliases leaves constructor admission active;
  replacing only the constructor-name expression leaves alias admission
  active. Removing both restores comparable execution in the diagnostic
  source variant (`microdiff-admission-v50b/`). The replacement uses
  `Object.prototype.toString` and preserves the checksum; the earlier
  constant-name variant fails the checksum and supplies no performance
  evidence. Canonical benchmark sources remain unchanged.
- V51 reuses the existing checked global read to combine own-descriptor
  lookup with value retrieval. Inherited bindings retain ordered
  `HasProperty`/`Get`, and lexical lookup still precedes object lookup.
  A missing lexical no longer enters the hosted interpreter profile.
  Four focused cases pass, including accessor counts, proxy installation of
  an own property during `has`, missing names, later lexical declarations
  and TDZ. Three release pairs reduce `microdiff` to **1.096×**
  (`dynamic-read-focused-v51.json`, `microdiff-diagnostic-v51.json`).
- V52 narrows admission for constructor-name reads and initial unused
  global aliases. Alias stores preceding every executable effect cannot
  encounter a guest-installed setter in a fresh closed realm. The proof
  still refuses observed aliases, implicit `this`, unknown host names,
  preexisting globals and dynamic-source escapes; later stores retain the
  earlier mutation proof. Constructor values passed, called, stored or
  mutated still select the complete realm. Four focused cases pass,
  including metadata getters and reflective alias recovery
  (`metadata-focused-v52.json`). The first complete MVP run finds a missing
  Function-prototype admission requirement (820/821). The compiler now
  requests that metadata independently of dynamic source; the reproducing
  test order passes and the complete suite passes **821/821**
  (`metadata-order-v52b.json`, `mvp-v52b.json`). Three canonical release
  pairs measure **0.991× microdiff** and **0.968× fast_diff**, with all
  output, binary, build and private-module checks passing
  (`text-diagnostic-v52.json`). Complete performance qualification remains
  open.
  These are refinements of **D8.1.3v27** and **D8.4.1v2**, with no effect
  replay or additional runtime environment.
- Havlak's MIR capture shows 851 ordinary class-property calls selecting
  the host-capable helper because the source writes to `process.stdout`.
  The release profile concentrates in map lookup and allocation
  (`havlak-mir-v50/`, `havlak-profile-v50/`). V53 reuses the closed stream
  proof before lowering and marks only functions containing the process
  reads. Unrelated functions retain their fixed property and callable
  paths; inlined bodies use their own function's flag. Escaped writers,
  preexisting globals and open host entries retain complete handling.
  Six focused cases pass, including the function-local MIR check
  (`host-scope-focused-v53.json`). V53 still emits 851 host-property calls in
  Havlak because the proof also requires every computed key to be closed.
  V54 separates availability of the complete process API from proof of which
  functions can receive its values. It emits **831 fixed and 20 host** calls;
  129 of 131 functions have the control's argument/local counts, opcode
  counts and helper-call counts. The two remaining functions contain output.
  Three release pairs still measure **1.047×** for Havlak, so this compiler
  repair does not explain or close the remaining execution regression
  (`text-havlak-diagnostic-v54.json`, `havlak-candidate-v54.mir`).
- A further eight-case release probe compares archived normal JS with V51.
  Six regressions involve constructors recovered from array-stored functions,
  native methods, bound functions, reflective method lookup and the Function
  prototype. V53 follows array initializer values, includes native method
  origins, and initially treats erased call results as possibly callable before
  unknown-key access. The six additions pass the focused constructor case,
  and the complete V53 suite passes **822/822** (`mvp-v53.json`).
  `constructor-origins-v51b.json` preserves the valid `-e` comparison;
  the preceding `-p` experiment is invalid for the archived statement syntax
  and supplies no compatibility evidence.
- V53's broad erased-call rule regresses `fast_diff` to **2.678×** and
  `microdiff` to **1.103×**. V54 limits the added callable origins to
  `bind`, reflective lookup and callable prototype retrieval. Six focused
  cases pass (`admission-focused-v54b.json`), and three unchanged-source
  release pairs recover **0.993× fast_diff** and **0.987× microdiff**.
  The first V54 focused run contains a new MIR assertion whose arbitrary
  string key correctly requires callback handling; the corrected fast-path
  assertion uses a boolean key, while a separate arbitrary-string case
  retains its semantic check. Both artifacts are retained. V53's performance
  regression also remains recorded; neither run is final qualification.
- V54 passes **822/822 MVP cases** and **136/136 default/explicit CLI
  pairs** under unset, MIR, AST and AUTO policies (`mvp-v54.json`,
  `entry-v54/results.json`). A diagnostic Havlak source variant replacing
  only output stream writes with `print` still measures **1.034×** in three
  release pairs (`havlak-print-diagnostic-v54.json`). Its checked output
  matches, but transformed-source timings do not qualify the canonical row.
  The remaining execution gap is not attributable solely to host-property
  helper selection.
- V57's eight-row implementation screen keeps the canonical sources and
  pinned release/module inputs. Its first six rows identify remaining gaps
  in Havlak, `pnpoly`, and `prettier_ast`; this is not the complete protected
  population. A concurrent MIR audit after those six rows confirms matching
  output on both releases (`remaining-mir-v57/`). `pnpoly` acquires separate
  object-to-primitive guards when host support activates the library, even
  for comparisons with one known Number operand. `prettier_ast` retains
  host-capable property/call handling throughout its functions because it
  imports a native module. These are lowering differences to investigate,
  not proof that either accounts for the entire timing difference.
  `remaining-diagnostic-v57-context.json` records the overlap with the long
  jq diagnostic; final performance acceptance requires a separate campaign.
- V59's separate `prettier-no-fs-v59` diagnostic replaces only the startup
  file read with the identical JSON text, retaining `JSON.parse`, the timed
  formatting loop and checksum. Three alternating release pairs measure
  **0.989×**, versus **1.051×** for the canonical source in the preceding
  seven-pair screen. All outputs match. This implicates the native-import
  compiler plan, but does not isolate a particular guard or qualify the
  canonical row. The source variant, input hashes and complete samples remain
  under `temp/js_mvp_integration/`; the benchmark source is unchanged.

### Measurement infrastructure

The paired runner now uses the canonical LambdaJS JetStream sources and
expands the benchmark include directives used by the standard runner.
Source provenance is captured from the original source tree before wrapper
generation, avoiding accidental hashing of the entire temporary directory.
Result52 artifacts are unchanged.

Final qualification preparation freezes the same 71 workloads in both Lambda
source variants as `protected-lambda-peers.json` (142 rows). Result52 records
failures for untyped `awfy/queens` and typed `text/jq_bf`; both remain in the
manifest. Checked-in output goldens are retained where available, with source
self-checks and full paired output identity for the JetStream rows.

The separate phase campaign records MVP's parse/bind/validation/planning,
emission and finalization timings. The remaining wall time includes setup,
teardown, I/O and instrumentation overhead; it is not reported as a pure
startup measurement. Uninstrumented paired runs determine execution acceptance.
The report validates the complete fixed population, unique row identities,
matching binary revisions and unchanged source trees before evaluating the
per-row and aggregate uncertainty gates.
Both JS and Lambda paired campaigns now record the production source/build
snapshot, runner files, module search paths, manifests and native-library
hashes, and check them again at completion. The report's optional frozen
protection manifest retains coverage-only rows and their observed failures;
a missing protected row or a new protected failure still prevents acceptance.

## Qualification record

Control: `test/benchmark/exe/lambda-v52-222fb3a494`, SHA-256
`62bb7194afcd5cc9506c4424e37181b559db5c9d33fd5fed277e1218cf6244f8`.
The control lane uses the explicit fast selector. Candidate lanes use the
ordinary default command. All performance comparisons require release builds.

Work artifacts are under `temp/js_mvp_integration/`. They include intermediate
experiments, so individual filenames and revisions must accompany conclusions.
Per the user's implementation guidance, work continues while other checkouts
use the machine. Interim timings are diagnostic; the quiet-machine performance
campaign and final aggregate verification follow the completed changes.

| Evidence | Status |
|---|---|
| Archived control: 17 MVP microbenchmarks | 17/17 correct in `micro_control/results.json`; one measured sample, population screen only |
| Canonical 71-workload control population | 71/71 outputs match Node: 68 in `node_population.json`, three long jq workloads in `long_control/results.json`. Frozen in `protected-standard.json`; concurrent screening timings are not acceptance measurements |
| MVP/public execution unit suite | V70 passes 828/828 MVP cases (`mvp-v70.json`), repeated in the full Lambda aggregate alongside 215/215 Script/AUTO and 510/510 public JS cases |
| Optimization / MIR contracts | V70 passes 116/116 optimization contracts, 36/36 JS MIR, 251/251 Lambda MIR and 20/20 unchanged debug size-ratchet cases in the aggregate. V60's release/default-profile Lambda failure remains recorded above |
| Default vs explicit entry equivalence | V70 passes 136/136 pairs (272 invocations) under unset, MIR, AST and AUTO policies; `entry-v70/results.json` |
| Performance protection gates | V70's 17 micro rows all have correct output; after retained-sample extensions, the geometric mean is 0.99665× with a 0.99982× one-sided 95% upper bound. Sixteen row bounds meet 1.03×; `js_micro/lit` remains inconclusive at 1.05512× despite equal 0.127 ms medians. The complete 71-row standard and 142-row Lambda peer campaign is running against the pinned V70 release; incomplete and interrupted captures do not qualify |
| Lambda baseline | V70 completes **7,917/7,917**: 5,805 runtime and 2,112 input tests, with zero failures, partial results or skips. This includes 319/319 forced-GC cases and all 20 unchanged debug MIR size limits. Source and all 42 executable/native-module inputs remain unchanged; `qualification-v70-lambda.json` and `lambda-baseline-v70/`. Earlier failed attempts remain archived |
| Radiant baseline | V70 completes with required native access: **5,997 passed, zero failed, 146 partial and 2 skipped** out of 6,145 reported cases, matching V44's counts. This includes 32/32 input-event WPT, 646/646 UI automation and 196/196 vector cases. Source and all 28 executable/native-module inputs remain unchanged. `qualification-v70-radiant.json` and `radiant-baseline-v70/` retain the evidence; earlier attempts remain archived |
| Test262 baseline | V70: **40,261/40,261 fully passed**, zero retries, partial results, failures or lost batches; the same 2,652 exclusions and backend routing remain. Source, all 16 executable/native-module inputs and configuration hashes are stable. The corpus is clean at `673e9bacbe28590f501e2dcd817aadcc31899191`. `qualification-v70-test262.json`, `test262-v70/` and `test262-v70-corpus-status.json` retain the result; earlier attempts remain archived |

The V47 archive also copied an unchanged `_t262_batch_kills.txt` from
21:51:49, before that gate began at 00:01:37. Its original timestamp and
hash are recorded in `test262-v47-archive-review.json`; it is not V47 retry
evidence. V47's own log has no retry phase. Subsequent archives include
only diagnostics written during their gate and list excluded stale files.

`regressions-v70.json` completes all three aggregate gates with unchanged
source. The root `lambda.exe` is restored to the pinned V70 release hash.
This closes current-source aggregate correctness qualification; the complete
release performance population and the remaining design work below remain open.

V70's initial micro capture uses seven alternating pairs on all 17 rows.
`micro-v70/extended49.json` retains those samples and extends four uncertain
rows to 49 pairs; `micro-v70/report49.json` retains the resulting gate.
Literal allocation timings cluster around 0.123–0.127 ms and 0.134–0.138 ms
in both lanes. Equal medians do not close its uncertainty gate.
`micro-v70/literal-uncertainty-review.json` records byte-identical control
and candidate MIR and a finite extension to 196 total pairs, retaining all
49 existing pairs.
`full-performance-v70.json` records the subsequent full standard/Lambda
campaign, with the same release/native-module hashes and frozen source
inventory. Its population includes the long jq rows and both historical
Lambda coverage failures.
`performance-tail-v70.json` queues the literal extension, separate phase
measurements and the three original normal-entry recovery comparisons after
that campaign finishes. It checks source/binary provenance before starting
and does not run these captures concurrently with the full population.
Queued or completed captures alone do not constitute performance acceptance.

The first V55 optimization invocation selected the release CLI and failed
107 trace-dependent cases because release does not emit the required profile.
`opt-v55.json` retains that invalid setup. `opt-v55b.json` selects the separately
built debug CLI through `LAMBDA_JS_OPT_EXE` and passes all 110 cases; release
performance measurements continue to use the pinned release executable.

Earlier focused runs exposed two eager-capsule regressions and four eager
process-initialization regressions; those causes were repaired without changing
test expectations. An intermediate 815-case run had 15 standalone Promise/async failures:
publishing the full realm bridge for a minimal standalone profile selected a
global constructor lookup that its profile did not provide. The complete
815-case rerun passes after the correction. Intermediate timings and passing
subsets do not establish performance acceptance.

The subsequent canonical-intrinsic changes pass all 113 public execution
cases in the v18 and v19 focused runs. Two standalone checks failed:
`PromiseRecordsCreateOwnDataProperties` and
`AsyncIteratorResultUsesOwnDataProperties`. Null-prototype private Promise
records remove the initial capability exception. A private standalone probe
then traced the remaining calls to FileReader registration
(`records-values-v19.json`); the CLI's earlier host initialization concealed
that defect. V20 passes the complete 815-case suite and the standalone
reproducer. Native-first identity checks also pass for globals, prototypes,
namespaces and callables. A separate native-first lexical probe still reports
ReferenceError on v20; that reproducer motivates the v21 binding-index changes.
V21 fixes that access but fails `DynamicInterpretedGlobalScalarReentry`
(814/815): a noncaptured unit slot still lent its mutable number home to the
reader. V22 fixes the common storage reader and passes 815/815. The separate
ten-check native-family diagnostic has six passes and the four suspended
prototype mismatches above; its results are recorded without treating the
unit-suite pass as complete integration acceptance.

### Correctness and emission review

The default now exercises fast lowering that the public MIR contracts previously
did not cover. All 111 source fixtures in `emission-audit/results.json` matched
Node before the existing emission contracts were updated. The 91 changed
sidecars retain semantic output assertions and pin native arguments, direct
calls, fixed property operations and explicit error completion where applicable.
No Test262 harness or baseline population has changed. The three reviewed
MIR budget changes are detailed below.

The v36 aggregate finds one further emission-contract change:
`ConstructorShapeDeclinesReceiverEscapingRhs`. The v26 local-region proof
admits the later numeric read behind three own-data/numeric guards while
retaining the constructor's escaping writes and the complete callable miss.
Node, pinned v13 and pinned v36 all produce `OPT_OK`. The reviewed MIR removes
two primitive conversions and one concatenation from the guarded inline path;
the separate `mvp_lmd_f3` fallback retains them. The revised sidecar requires
the three ownership/shape guards and six invocation misses as well as the
unchanged generic fallback counts. Its focused rerun passes.
`constructor-region-v37-review/` retains the commands, outputs and MIR hashes.

The earlier v13 Lambda baseline failures were 75 optimization cases, 15 MIR
contracts, one Lambda/JS realm-slot case, three MIR size budgets, one PDF Python
environment failure and one Mermaid rendering assertion. The optimization,
emission and realm-slot cases pass focused reruns. The PDF checks pass 14/14
with the installed Python 3.13/Pillow environment; the default Python 3.14 lacks
Pillow. At that stage the Mermaid case asserted in `radiant/view_prop_ensure.cpp` when a
scratch-lived anonymous text item requested a view-pool property. That failure
was not reproduced with a pristine debug binary and is not classified
as a proven preexisting failure.

The v36 debugger reproduces that assertion through
`calculate_max_content_height` → `dom_node_resolve_style` → `ensure_font`.
`layout_create_anonymous_text_item` borrows a text or container font, but both
carriers can legitimately omit an inherited font group. V38 also borrows the
active layout context's resolved font in that case. It retains the existing
layout-lifetime borrow and avoids requesting persistent pool storage for the
scratch item (**D4.5.1v4**). A focused native test covers context, parent and
text font precedence. The scope is this ownership defect blocking the core
gate; no geometry algorithm or rendering golden is changed.
The Mermaid golden rerun passes 1/1; the new font test and existing anonymous
grid retained-layout test pass 2/2. The Radiant dimension lint passes.
`qualification-v38-focused.json` records this evidence. The full V38 Lambda
aggregate passes 7,893/7,893, including the unchanged Mermaid golden.

The initial Radiant aggregate cannot qualify the implementation: its WPT
server receives `PermissionError` while binding loopback, and two Scene3D
workers report denied macOS XPC access before their idle timeouts. The owned
process tree was stopped and verified exited; its partial results and unchanged
source/binary hashes are retained in `radiant-baseline-v38-restricted/` and
`qualification-v38-radiant-restricted.json`. The same Scene3D control passes
in 152 ms with access. All four isolated view controls also pass, including
the HTTP/font checks and the two large native-layout/timer probes.

`PaintOwnership.SvgPhasesKeepForeignHtmlInsideItsSubscene` fails with access.
The debugger reproduces it with zero hits on the
anonymous-text helper, both changed DOM initialization paths and the JS
compilation entry (`svg-phase-trace-v38.log`). Source review identifies a test
boundary mismatch: commit `5572c8c7e` makes an outer SVG atomic to its parent
stack, as required by [SVG 2 §3.4.1](https://www.w3.org/TR/SVG2/render.html#RenderingOrder),
while the later producer-phase test enters through the parent traversal.
V39 retains the original internal-phase pixel expectations at the producer
entry and adds both parent-stack phases. Production painting and pixel
tolerances are unchanged; the ownership boundary follows **D4.5.1v4**.

The full access run also exposes four input-event WPT failures. Isolated
replay passes the two cases killed by signal 9 during the aggregate, but
cut/paste and target ranges still exit without a summary. The archived
Result52 binary passes all four exact scripts; both binaries use approximately
3.2–3.4 GB peak RSS. Logs and commands are retained in
`input-events-isolated-v38-collected/` and `input-events-control-v52/`.
The two reproducible failures reduce to `window.event`: native dispatch writes
the canonical host slot, while MVP's native-object read adapter reads the
ordinary placeholder. V39 routes native global reads through the existing
complete property kernel, also preserving live viewport fields. Ordinary MVP
object reads keep their existing path (**D8.1.3v27**, **D6.2.2v2**).
Focused tests cover both initialization orders, nested dispatch, restoration,
and GC inside listeners: 2/2 pass. All four original WPT scripts pass unchanged,
with stable binary hashes, and the complete vector suite passes 196/196.
No WPT source, harness, timeout or population changes.
The complete V39 Radiant rerun then passes all 32 input-event WPT cases and
all 196 vector cases. The aggregate has zero failures, with its 146 partial
and two skipped cases reported separately. Source and binary hashes are stable.

The V39 Test262 gate exposes the shared native descriptor writer's treatment
of a `writable`-only update as a generic descriptor. Converting RegExp's `$1`
accessor therefore leaves an accessor descriptor while reporting success.
An isolated comparison also reproduces this defect on ordinary objects,
functions, arrays and symbol keys in the archived AST backend; native
function `name` and `length` updates additionally invoke the accessor.
MVP's own descriptor path and Node pass the same probe. Canonical RegExp
accessors exposed the shared-kernel defect in the configured baseline.

V40 supplies the default `undefined` value when a data descriptor converts
an accessor, reusing the existing complete storage path. Lazy function-field
materialization is now limited to data properties, so generic and data updates
do not call an existing getter (**D8.1.3v27**, **D6.2.2v2**).
The new cross-backend regression covers both descriptor categories, sealing,
symbol keys, native callable fields, RegExp and GC between Script entries.
The expanded regression also exposes Error reflection rebuilding every
side-map descriptor as data, even when storage correctly retains an accessor.
V41 uses the shared descriptor conversion helper for that carrier. The
cross-backend regression and all **214/214 Script/AUTO cases** pass, including
collection between entries. `descriptor-focused-v41.json` and
`script-v41.json` retain the results; final aggregate qualification remains
open. `descriptor-conversion-v39.json` retains the Node, archived-AST and
candidate AST/MIR comparison.

The three MIR budget differences were reviewed against both archived entries,
including the actual emitted functions. V13 matches the archived fast
instruction structure: catch and exact-collection differ only in relocated
operands; generator additionally shifts two slot positions. Raw MIR and
normalized diffs are in `ratchet-review-v13/`; outputs match the archived
control, whose outputs were already compared with Node.

The new default uses inline fixed-property alternatives that can be larger
than the former public generic helper calls. The generator and collection
fixtures also exposed an unnecessary per-call realm guard; this was removed
before recording exact new limits under **D8.6.1**. The three budget entries
now pin both growth and shrinkage:

| Fixture | Reviewed changes from former public budget |
|---|---|
| Generator | Module instructions 11,855 → 14,804 |
| Dynamic catch | Module 739 → 464; function 354 → 261; root stores 9 → 7; scalar homes 2 → 1; safepoints 8 → 9; error-tag limit unchanged |
| Exact collection | Module 15,762 → 14,585; entry 8,154 → 8,494; leaf 33 → 29; entry root stores 503 → 537, homes 10 → 17, safepoints 892 → 913 |

These limits retain zero slack. The v13 aggregate passes the revised ratchet
20/20; the former-public and archived-fast comparisons remain available.

The same run's GC stress suite completed 304 cases before its forced-GC
`regression_music_layout` case exceeded the runner's 360-second idle limit.
The harness killed its GTest parent; the verified orphaned Lambda child was
then stopped. Builds and tests were active on the same machine, including a
separate `Lambda-opus` checkout. This is a recorded timeout, without a claimed
root cause or a replacement pass. The subsequent DocBook reference case also
reported a timeout, and the enclosing Lambda runtime suite later exceeded its
600-second idle limit. The full Test262 run was started before the aggregate's
final preflight, causing that preflight to reject the overlapping runner.
This scheduling error is recorded separately from runtime failures; the
runner and its exclusion policy are unchanged. V36 subsequently passes the
entire forced-GC suite, DocBook, the PDF checks and the final Test262 preflight.
The earlier interrupted runs remain failed attempts; V36's two remaining
failures and the V38 repairs are recorded above.

### Performance repairs in progress

- Restricted Function `caller`/`arguments` access originally activated general
  property handling for any object field with that name. Receiver-kind admission
  now enables Function metadata only when a function receiver is possible.
  In v8, `js_micro/args_fp` is 1.0041× the archived fast control with a 1.0135×
  paired 95% upper bound, versus approximately 1.15× before the repair.
  All 17 microbenchmarks have correct outputs; noisy rows still leave the
  complete performance gate open.
- V9 standard sampling found Permute at 1.0383× and Bounce at 1.0882×, both
  slower in every measured pair. The shared `process` namespace had caused
  foreign-value handling to reach unrelated class operations. The realm guard
  now depends on admission of additional source. The property helper reuses
  the existing nominal lookup, with native Map fallback after the source-class
  operation. Foreign callable tails and virtual carriers are checked before
  accessing their implementation-specific storage.
- V12's 21 alternating pairs pass both repaired row gates: Permute is
  **1.01137×**, with a **1.01944×** one-sided 95% upper bound; Bounce is
  **1.02137×**, with a **1.02943×** upper bound. All outputs match the frozen
  oracle. `awfy-regressions-v12.json` retains the samples. This closes those
  two row regressions, not the full population or geometric-mean gate.
- The paired runner checkpoints complete rows atomically. An interrupted
  campaign retains raw output, timings and provenance with
  `capture_complete: false`; incomplete campaigns never imply acceptance.
- V33's quiet seven-pair micro run has correct output on all 17 rows and a
  0.99875× geometric-mean ratio. Three per-row upper bounds remain above
  1.03×: calls (1.03004×), object deletion (1.03238×), and escaped field
  retyping (1.04435×). Their MIR operation streams match the control; the
  object cases differ in relocated addresses. This does not close the
  performance gate. Samples and MIR are in `micro-v33-quiet/`.
- V45's initial seven-pair micro capture passes all output checks. Numeric,
  deletion and escaped-retyping uncertainty requires 14 additional pairs
  each; `micro-v45/results-extended21.json` retains all initial observations
  and provenance, while the original capture remains unchanged. All 17 row
  upper bounds then meet 1.03×. The fixed-population geometric mean is
  **0.99682×**, with a **1.00177×** one-sided 95% upper bound. This closes the
  micro execution gate only; the 71-row standard and Lambda peer gates remain
  required.
- Escaped retyping retains a small measured slowdown within the limit. Its
  V45/control MIR has the same 717-instruction body and helper calls; the
  textual differences are relocated addresses. `retype-mir-v45-enabled/`
  records the review. No added general dispatch is present in this kernel;
  the remaining timing difference is not attributed to a proven cause.
- The three diagnostic workloads pass in all four lanes. Their numeric and
  array kernels retain their MIR structure; changes are confined to the
  enclosing host-output callers. Candidate default and explicit MIR match.
  Against archived normal Result52, seven-pair execution medians are
  **0.875 vs 42.839 ms** for fib, **5.292 vs 5,155.427 ms** for matmul, and
  **0.277 vs 282.093 ms** for array1 (`recovery-v45.json`). These recovery
  comparisons do not replace the archived-fast protection gate.
- Separate phase capture passes all 21 rows in
  `phases-v45-zero-resolution/`. The initial capture rejected zero-duration
  executions of the minimal startup expression. The diagnostic helper now
  accepts a single well-formed zero record for that startup probe only;
  positive execution timing remains required by performance gates. Minimal
  startup medians are **8.123 vs 8.196 ms** end to end (control/candidate).
  Scripts requesting `process` show roughly **4 ms** added residual time.
  The canonical process lookup activates the configured Node core namespace;
  its startup cost remains a separate review item, not an execution speedup.
- V45's standard campaign stops after 21 completed rows for diagnosis:
  Bounce loses all seven pairs, with **1.03121×** median and **1.05310×**
  upper bound. The original incomplete capture and the separately finalized
  interruption record remain in `protected-standard-v45*.json`. Fourteen
  further Permute/Bounce pairs retain every earlier observation but do not
  close both uncertainty gates (`awfy-uncertainty-v45-combined.json`).
- An amplified diagnostic reproduces about 2% Bounce overhead. GC mark time
  is only 1.5/3.2 ms over 14-second control/candidate executions. Sampling
  identifies the shared property's general-operation setter as a hot path.
  A private link map is applied only after checking that its text addresses
  and bytes match the pinned V45 binary; `bounce-profile-v45/` retains the
  samples, map and symbolization evidence.
- V46 specializes `Set` through the existing property implementation,
  preserving the strict flag and complete foreign/property fallback
  (**D8.4.1v2**, **D8.4.2v2**). This removes unrelated operation branches
  from writes just as the existing read specialization does. Seven paired
  diagnostic runs with 1,000 checked iterations compare V46 directly with
  V45: Bounce is **0.98108×** (upper **0.99362×**), and Permute is
  **0.99214×** (upper **1.00179×**). These amplified sources are retained
  separately and never replace canonical benchmark rows.
  The canonical V46 21-pair screen passes Bounce's row limit but leaves
  Permute's uncertainty open; aggregate correctness and the complete
  performance population remain required.
- V47 passes the 17-row micro protection gate with geometric mean
  **1.00099×**, upper **1.00209×**. Initial seven-pair observations remain
  in `micro-v47/results.json`; integer-dense and strings extend to 21 pairs,
  and escaped retyping extends through 21 to 49 pairs. Every observation
  remains in `results-extended49.json`; `report-extended49.json` records
  the unchanged limits. Escaped retyping is still **1.02546×**, upper
  **1.02972×**; this retained slowdown remains visible in the report.
  Amplified sampling finds shared shape lookups, retyping and field storage
  dominate (`retype-profile-v47/`). The candidate symbol map is validated
  against identical text bytes and addresses. The prior identical-MIR
  comparison and this profile do not establish a cause for the difference.
- V47's separate phase capture passes 21/21 output checks. The diagnostic
  residual cost (wall minus execution and recorded compilation) exceeds the
  fast control by **1.35 ms fib**, **1.17 ms matmul**, and **1.22 ms array1**,
  compared with approximately 4 ms before deferring Node-core. These are
  separate paired campaigns, not a fresh V45/V47 A/B. The canonical base
  process still initializes its complete base fields and streams. Minimal
  entry is **8.276/8.348 ms** control/candidate. Startup and complete
  standard/Lambda performance acceptance remain open.

### Entry equivalence

The v12 check covers both CLI spellings under unset, MIR, AST and AUTO policies,
including file arguments, eval, print completion, modules, timing and errors.
All 44 paired cases have equal behavior between spellings. Two cases expose
the same AST/AUTO failure: a bare string directive at EOF is concatenated with
injected CommonJS declarations without a statement boundary. The source join
now inserts a line terminator when needed, also preserving a trailing line
comment's original EOF boundary. V13 passes all 56 paired cases, including
additional directive and trailing-comment cases.

V15 passes 80 paired cases (160 invocations), adding help, an empty invocation,
explicit Module strictness, Module top-level `this`, and top-level `await`.
Two entry defects were repaired:

- Leading compatibility selectors are skipped when finding help or an empty
  invocation, so the selector cannot become a filename.
- The explicit Module goal reaches the common source entry and the execution
  worker even when the source contains no `import` or `export`. The shared
  runtime header owns the entry declaration.

The v15 release and private release Node core/FS modules are frozen in
`candidate-v15.json`, including hashes and the `JUBE_MODULE_PATH` used by the
entry check. The executable SHA-256 is
`e0ee5de14ccdad5b31d2d3a25be1555beabddc7514fae58ff6dcf23dc80f9c2e`.
The v15 unit executable uses the shared Node core image against which it was
linked. Substituting the private release DSO failed at process launch because
that image omits an internal initialization export required by the unit
executable; no tests ran in that attempt. The subsequent 815/815 run records
the actual linked host hash and supplies correctness evidence only.
The later property-dispatch refinement still needs fresh performance evidence:
foreign receiver checks now sit inside the existing array/function arms or
after a failed nominal lookup, preserving the original strict/initialization
operation when forwarding.

## Remaining design work

The terminal CLI proof recovers the existing closed-unit planning opportunity.
Open embedding, modules and preambles still select broad shared-realm handling.
Function-local numeric property regions, scalar loops, dense-loop admission,
integer-index proofs and ordered Number facts now remain available beside
dynamic source. Further function-effect refinement and guarded public calls
remain required; these local improvements do not complete that analysis.

The global-copy bridge is removed, and the binding index references the
existing storage owners. V70's complete aggregate gates qualify the current
intrinsic adoption, host integration and lifetime implementation. Remaining
work includes host-surface coverage and release performance qualification.
Legitimate child realms retain separate globals while sharing their context
and event loop.

### Consolidation boundaries and open checks

The following boundaries track implemented consolidation and open work for
milestones 2–3 under **D5.4** and **D8.1.3v27**. V70 completes the current
correctness gates above; performance acceptance remains pending.

| Current boundary | Open work |
|---|---|
| V32 removes late prototype replacement and borrows existing ordinary, primitive, error, buffer, weak, collection and iterator families before constructing alternatives; V70 passes aggregate qualification | Complete release performance qualification while retaining callable ABI checks and exact ownership |
| `mvp_lmd_compile_function_satellite` uses the same native-first initialization; V70 passes all 215 Script/AUTO cases and aggregate lifetime checks | Complete release performance qualification |
| The shared binding index references MVP slots, interpreter cells and module slabs; V70 passes the aggregate, including 319 forced-GC cases | Complete release performance qualification |
| Suspended families and Promise borrow existing native intrinsic records; both entry orders, shared methods and retained constructor identities are covered by the V70 aggregate | Complete release performance qualification |
| V36 shares immutable global functions, prepared timers, console and performance namespaces; process and require use shared services; V70 passes aggregate qualification | Broaden the retained console method gap after preserving performance; keep clock origin, output sinks, callable home realms and existing native-call specialization |
| Shared-realm admission still widens unit-level flags | Refine effects at functions and observing operations, preserving unrelated numeric kernels and complete misses without replay |

No milestone is accepted until its correctness and performance exit gates in
the design document pass. In particular, the full protected population includes
the long jq workloads; a timeout cannot remove them from the final report.
