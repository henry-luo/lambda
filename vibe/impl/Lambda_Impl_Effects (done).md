# Lambda Callable Effects and Generative Templates — Implementation Plan

**Status.** Implemented 2026-11-18. Procedure bounds, generative `function`
values, script entry blocks and template dispatch run on both tiers.

**Design authority.** S1.10v2, S7.4.5v4, S11.1.5v4, S12.1.1v5, S12.1.3v6,
S12.1.4v7, S12.1.6v2, S16.7.7v2, D7.1.1v4–D7.1.3, D9.1.2v2 and D11.1.2.
[Procedural design](../Lambda_Design_Procedural.md#11-checked-procedure-effects-and-generative-functions)
and [reactive template design](../Lambda_Design_Reactive_UI.md#21-functional-presentation)
record the decisions and open questions.

## Completion record

- **Checkout.** `master` at `fb10cbc84a` with the effects changes in the
  working tree on 2026-11-18. Verification used the split `debug_native`
  host, the local `lambda-input` checkout and local loopback servers.
- **Callable contracts.** The parser, compiler and runtime accept `@clock`
  and nonempty `@{random, clock, print}` after the complete result contract,
  which retains `T | E` and `T^`. Omission permits arbitrary effects. Known
  calls use static admission, and unresolved boundaries, captured callbacks
  and omitted defaults use the same effect check in both execution tiers
  (S11.1.5v4, S12.1.4v7, S12.1.6v2).
- **Render context.** The explicit script and template blocks collect results
  as functional blocks. `apply` checks the selected entry. Rendering and
  regeneration execute the selected template's body, and its handlers use
  state and model without evaluating body-local bindings. Opaque legacy
  resource readers need an effect summary to cross a restricted boundary
  (S7.4.5v4, S12.1.3v6, S12.1.6v2, S16.7.7v2).
- **Parser and tiers.** `make test-grammar-s16` passed 475 reference and 488
  production checks. Seven script goldens matched in interpreter, eager
  JIT and AUTO with every-allocation, poisoning GC. Three static refusal
  fixtures matched their diagnostics across all three tier settings.
- **Reactive and HTTP.** `GenerativeTemplateRerendersAndReusesQueryKeys`
  replayed state 0 → 1 → 0 on all three tiers, with three DOM assertions,
  three console emissions and one request per distinct query. A separate
  replay exercised equal output and a skipped branch. The loopback
  error test covered a refused local path, a handled HTTP failure and two
  independent procedural `io.fetch` requests. Four Todo replays passed 39
  assertions. Logs and the supplementary harnesses are in `temp/effects/`.
- **Baselines.** Lambda passed 6,113 tests and the Input native baseline
  passed. The broad Radiant run reported 34 failures, including a missing
  `pngjs` dependency and document UI and layout failures. Four sampled
  document UI failures reproduced with the pre-effects host. The full doc
  checker passed the new example. All 139 failures it reported matched
  diagnostics on the same control host.
- **Follow-up.** SO52 retains extra operation labels and cache policy.
  Effect handlers for test clocks, random sources and replay remain future
  design (S12.1.4v7).

## 1. Delivery boundary

Support the two signature forms from S11.1.5v4 and S12.1.6v2. The effect
clause follows the whole result/error contract. An omitted clause leaves
the procedure unrestricted.

```text
pn name(params) Result @ effects { ... }
type t = pn (params) -> Result @ effects
```

Deliver the same admission and refusal behavior in the AST interpreter,
production MIR Direct and AUTO. Keep error acknowledgement, capture
ownership, suspension analysis and procedural block results under their
existing contracts (S7.4, S9.1, S12.1.2v3, D7.1.3). Effect handlers for
scripted clocks and random sources are future work. This plan delivers
the call contracts they would build on.

## 2. Surface decisions and remaining design questions

| Area | Decision |
|---|---|
| Procedure signatures | S12.1.6v2 adopts `@clock` and nonempty `@{random, clock, print}`. The return uses S7.4.2v4. The new procedure-type arrow is additive (S11.1.5v4). Group a nested callable result to attach a suffix to the outer signature. |
| Function values | S12.1.4v7 gives intrinsically effectful functions and effectful capture instances `pn` colour. Body collection follows the `function` declaration. |
| Operation mappings | `uuid()` supplies random, `now()`, `today()` and `clock()` supply clock, and `print()` supplies print. Seeded `math.random(seed)` remains pure. Additional mappings remain SO52. |
| Template dispatch | S12.1.3v6 and S16.7.7v2 use an explicit generative body and a lexical `apply` allowance. Selected template bodies may read cached HTTP(S) URLs through the existing host policy. Regeneration calls the registered body. |
| Cache policy | S7.4.5v4 uses full URL identity, including the query. The existing input service defines initial cache lifetime and eviction. Further cache policy remains SO52. |

## 3. Implementation phases

### Phase 1. Parse and retain procedure contracts

- Start with `parse_function_declaration` and related type parsing in
  `lambda/runtime/parser/lambda_parser.c`. Follow the reduction through
  AST construction. Update `parse_fn_type` in `parse_type_pattern.cpp`
  and the reference `fn_type` rule in
  `lambda/tree-sitter-lambda/grammar.js`. Use S11.1.5v4 and S16.2.3v3
  to verify nested result types, optional results and line boundaries.
- Trace `Function::fn_type` and `TypeFunc` in
  `lambda-input/lambda/core/data.hpp`. Reuse the semantic signature for
  callable metadata where its ownership permits. Carry bounds through
  declaration and type lowering, signature printing, cloning, imports,
  closures and host signatures (D7.1.1v4, D7.2.1). An omitted bound
  permits arbitrary effects. The set spelling requires at least one name.
- **Exit.** Both parsers agree on the accepted and rejected forms.
  Signature consumers preserve the bound and the result/error contract.
  Existing signatures follow the compatibility rule in S11.1.5v4.

### Phase 2. Check bounds at callable boundaries

- Extend the call-colour walk in `build_ast.cpp` and reuse
  `type_contract.cpp` for callable admission. A narrower procedure bound
  fits a wider receiving bound. An unrestricted callee requires an
  unrestricted procedural context. Resolve known mismatches statically.
  Preserve the procedural declaration boundary even when its effects are unused
  (S12.1.1v5, S12.1.6v2).
- Reuse the runtime check before dispatch in `lambda-eval.cpp`, with
  matching lowering in `interp.cpp` and `mvp/mvp_mir.cpp`. Guard using
  the selected callee and its bound actuals, including defaulted, named
  and rest arguments where supported. Keep effect refusal on the ordinary
  error path and perform it before the rejected call can run. Keep live
  callables and arguments precisely rooted across any allocation.
- Audit direct and dynamic calls, `call()`, pipes, bound methods, hosted
  adapters and system higher-order callbacks. S12.1's implementation
  footnotes already identify gaps in the earlier colour checks for pipes
  and system callbacks. Close any affected route using the same admission
  rule (D7.1.1v4).
- **Exit.** A disallowed effect is rejected through aliases, returned
  callables and imports. Dynamic rejection leaves the callee's effects
  unexecuted on every tier.

### Phase 3. Extend `function` and classify builtins

- Reuse the `is_colour_poly` parameter classification and existing call
  analysis. Combine a body's intrinsic requirements with its polymorphic
  slots under S12.1.4v7. Analyze transitive and recursive calls to a stable
  summary. Preserve dependencies when an escaping closure captures a
  polymorphic callback. Include calls through fixed-colour parameters
  in the body's own requirements.
- Attach the approved operation bounds to `sys_func_registry.c` and
  reuse bound-aware host signature parsing. Ensure a known procedural
  callback cannot hide disallowed effects when wrapped locally. A caller
  in a permitted procedural context can still supply an I/O callback to a
  callback-only combinator.
- Audit constant folding, fusion, parallel evaluation and execution tier
  planning. Use the instantiated purity proof before treating a
  `function` call as pure. Preserve ordering and execution count for
  observable effects (D7.1.2). Keep suspension and defect facts independent
  (D7.1.3).
- **Exit.** A pure callback keeps a callback-only call pure. A clock read
  remains effectful without callbacks. Mixed callbacks and intrinsic effects
  require both permissions, including after closure escape.

### Phase 4. Admit passive effects in templates

- Admit the exact top-level `function() { ... }` block in the production
  parser and reference grammar. Distinguish it from the `function name`
  declaration and the `function` type word. Lower it on both tiers as
  an immediately evaluated functional block with the S16.7.7v2 allowance.
  Feed its result through the existing script content normalization.
- Follow `AstViewNode` into registration in `interp.cpp` and
  `mvp/mvp_driver.cpp`. Publish the template contract through
  `TemplateEntry` and `template_registry_add`. Apply the S16.7.7v2
  dispatch rule to `fn_apply1`, `fn_apply2` and regeneration through
  `render_map_retransform_with_results`. Reuse one admission rule across
  the initial and regeneration entry points.
- Verify that the selected template is admitted before its body runs.
  Preserve pure element attributes and content, pure constraint bodies,
  and handler ownership of persistent writes (S12.1.1v5, S12.1.3v6).
  Carry the selected entry's contract through recursive `apply` and
  template imports. Exercise both view and edit matching.
- **Exit.** A page opened by `lambda view` can render through
  `function() { apply(model) }` with an admitted generative template.
  Direct pure entry still rejects a selected effectful body. Initial
  evaluation and each real re-evaluation execute the reached effects
  with ordinary call order. Equal rendered output can
  suppress presentation updates after evaluation. Randomness, clock reads
  and printing create no subscribers or dirty entries.

### Phase 5. Verify and admit cached URL input

- Trace `fn_input2` through `input_from_target_with_io` to the cache
  lookup in `lambda-input/lambda/input/input_http.cpp`. The HTTP cache
  currently looks up and stores by the URL it receives. Verify that
  target resolution and canonicalization preserve the query on the
  normal template call path. Reuse the existing source cache and host
  policy checks (S7.4.5v4, D9.1.2v2, D11.1.2).
- Exercise dynamic target and option admission. The existing `input`
  accepts filesystem nodes and paths and handles format and schema
  options. Template admission must establish that the call is covered
  by the approved URL read contract, including on hits and redirects.
  Use the existing ordinary error routes for failures. Verify behavior
  on cache misses against the render host's blocking and suspension
  contract (D7.1.3).
- **Exit.** An offline test server sees a request for a cold query key
  and cache reuse on a later render with that valid key. A Refresh
  handler advances `serial_no`, causes one regeneration and reads
  the new query key on a miss. Test a denied request, an acquisition
  error and direct `fetch()` refusal from a template. Verify separately
  that repeated explicit `fetch()` calls in a `pn` run independently.

## 4. Verification and closeout

- At each phase, run the affected S16 or callable fixtures first. Use
  `make test-grammar-s16` for grammar changes. Extend the established
  `proc/fn_pn_function_types` and `proc/function_colour_poly` coverage
  or add focused script pairs following the test map in
  [test/README.md](../../test/README.md). Capture both expected successes
  and refusals. Every new Lambda script fixture needs its expected output.
- Compare interpreter, production JIT and AUTO on dynamic calls, closure
  escape and errors. Use the existing forced-GC gates for changed lifetime
  paths. Verify effect counts through captured console output and request
  counts. Check clock and random results by stable properties and count
  actual invocations at the test boundary where needed.
- Run focused reactive UI automation for initial render, state-driven
  re-render, skipped branches, no-op output and the absence of
  self-scheduled renders. Run `make test-reactive-ui` for existing
  behavior. Use the existing local HTTP and runtime policy harnesses for
  cache and admission coverage. Run the Lambda and Radiant baselines at
  the completed milestone and the input baseline if that subsystem changes.
- Update the reference examples when the new syntax executes. Run
  `make check-doc-code`. Record tested commit or worktree state, tier
  settings, commands, counts and outstanding failures in the checkpoint.
  Update the formal implementation footnotes for qualified slices and
  regenerate the formal index.
