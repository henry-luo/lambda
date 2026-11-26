# Lambda Script TeX and LaTeX engine implementation plan

**Status:** In progress; production TeX and LaTeX input routes through Script,
and the native TeX engine source and direct tests have been retired. The records
below include historical observations and test results as of their dates; they
do not imply full TeX compatibility.

**Scope and authority:** Port TeX expansion, LaTeX digestion and the selected
expl3 APIs to `lambda.doc.latex` in Lambda Script. TeX source remains data
(**S1.8**); the input boundary is governed by **D10.2.2v2** and the
input layer must not depend on the Lambda runtime (**D9.1.3v2**). The requested
production migration requires the coordinated formal and native-client work
described in [the input migration record](../input/Input_Latex.md).
The existing native engine design and historical Phase IV work are in
[TeX engine design](../latex/Lambda_Design_TeX_Engine.md) and
[LaTeX Phase IV implementation](Lambda_Impl_Latex_Phase4.md).

## Implementation priorities and acceptance

1. Complete core tokenization, expansion, scoped state, constructors, resources,
   diagnostics and TeX/expl3 operations, preserving native or separately ruled
   behavior. Record partial support and unsupported cases in the findings below.
2. Complete expanded-output reconstruction, source mapping and LaTeX digestion;
   compare text, diagnostics, ASTs and rendered documents against the existing
   native and TeX reference fixtures. Preserve existing golden results and fix
   root causes rather than hiding failing cases.
3. Qualify interpreter and JIT, representative forced-GC cases, the existing
   package/corpus tests and release-profile performance. Retest after each change
   that affects their results.
4. Specify and implement the production entry-point and native-client migration,
   including resource authority, lifetime and formal-design changes, before
   retiring the C++ path. The input-owned defects remain in the
   [Lib and Input issue ledger](../Lib_Input_Issue_Ledger.md).

## Current checkpoint

- **Focused test timeouts, 2026-11-26:** The digest and expl3 integer-compare
  fixtures rebuilt the same resource-free kernel for every independent case.
  They now reuse the supported immutable preload (D10.2.2v2) while retaining
  fresh-load coverage for the first document and the included-file case. Both
  unchanged goldens pass the original GTest timeout in an isolated release
  home: digest 3.287 seconds and integer comparison 11.908 seconds. Before the
  fixture change, both exceeded 60 seconds when run alone. No expectation,
  case, or harness timeout was removed or changed. Both fixtures also match
  under forced GC with freed-memory poisoning in interpreter and JIT.

- **Math SVG corpus recovery, 2026-11-26.** The shared math digester preserves
  raw arguments on generic extension commands and restores recovery for braced
  left delimiters, invalid middle delimiters and targetless negation
  (D10.2.2v2). All 13 affected public parse trees match the retired native
  parser. The SVG corpus passes 921/921 and now reports parser and renderer
  error messages (D10.2.3, S7.6.1v9). All 74 focused goldens pass. The
  recovery golden also passes in the interpreter and JIT with forced GC.
  The broader Lambda baseline passes 5,203/5,220, with failures in
  networking and JavaScript media playback. Evidence is in
  `temp/math-svg-corpus/` and `temp/math_corpus_report.json`.

- **Prefixed macro source mapping, 2026-11-26:** The definition continuation
  and reconstructed-segment argument handling now cover the source-group
  joining that originally required the global prefixed-macro guard
  (D10.2.2v2). The four original cases, 48 additional assignment/group cases,
  and 192 placement, comment, conditional, expansion and prefix cases match
  complete retired-engine document trees; another 16 cases remain explicitly
  blocked by the independent accent mapping guard. A 20-case regression checks
  complete native trees through direct digestion and public `parse`. The old
  four guard expectations now assert those independently captured trees. Three
  focused GTests and both forced-GC execution tiers pass in the isolated home.
  The other CSV operation and provenance guards remain active. Verification
  uses an isolated Script home while another change edits the shared math
  digester.

- **Table alignment categories, 2026-11-26:** The document digester now treats
  an ampersand as a cell separator only when its TeX category is alignment
  (D10.2.2v2). Four complete document trees, including category-11,
  category-12, alignment and escaped ampersands, match the retired engine
  through both direct digestion and public `parse`. Five table GTests pass;
  the new fixture also passes forced GC in interpreter and JIT. A diagnostic
  run that forcibly disables the unqualified CSV source-mapping guards now
  produces the expected six HTML checks, including `Tea &amp; cake`; this is
  evidence for further provenance work, not a production guard removal.
  The normal phase-five table GTest still fails its six CSV assertions because
  those guards remain active. Prefixed macro provenance is covered by the
  later checkpoint; the expl3 and input-stream guards remain to qualify.

- **Expl3 token trimming, 2026-11-26:** Script preloads TeX's original
  delimiter-driven `tl_trim_spaces:n`, its value/local/global variants and
  auxiliaries (D10.2.2v2). The macro loader preserves explicit space tokens in
  parameter delimiters. Thirteen TeX Live comparisons cover grouped and
  protected tokens, category-12 spaces, scope, and macro meanings. Five
  focused GTests pass; the new fixture also matches in interpreter and JIT
  with forced GC. CSV expansion now resolves all named columns and retains
  the expected filtered rows. Trimming and other complex output remain
  guarded at the document source-mapping boundary.

- **Expl3 Boolean assignments and predicate tokens, 2026-11-26:** Script now
  evaluates `bool_set:Nn` and `bool_gset:Nn` with TeX's f-expansion and ordinary
  scoped character definitions, and implements `int_compare_p:nNn` using its
  native definition (D10.2.2v2). The shared primitive-predicate result now
  returns `c_true_bool` or `c_false_bool` tokens, as TeX does; returning digit
  characters previously broke Boolean expressions. TeX Live checks cover
  assignments, predicates, token meanings, global overrides and
  after-assignment. Seven focused GTests pass, and the new fixture matches in
  interpreter and JIT under forced GC. The full CSV expansion now completes
  without diagnostics and filters the intended rows, but its named columns
  were subsequently resolved by the token-trimming checkpoint; its document
  tree remains guarded and the six phase-five table assertions still fail.

- **Expl3 string comparison variants, 2026-11-26:** The native `VnTF`, `VnT`
  and `VnF` value variants reuse the existing register-expansion and string
  comparator; `eeTF`, `eeF` and `p:ee` use the native expanded-comparison
  definitions (D10.2.2v2). TeX Live checks cover token-list and integer
  values, retained control tokens, branch selection, expanded operands,
  predicates and macro meanings. Four focused GTests pass; both new fixtures
  match their goldens under forced GC in interpreter and JIT. Value-argument
  and expanded-comparison
  document source mappings remain guarded. The complete phase-five CSV
  expansion then reached `bool_set:Nn`; the subsequent Boolean checkpoint
  records its current status.

- **Expl3 regex matching, 2026-11-26:** The Script engine implements the
  public protected `regex_match:nnTF`, `nnT` and `nnF` macros with a bounded
  matcher for ASCII character and control-sequence tokens (D10.2.2v2). It
  supports unanchored search, anchors, literals, dot, digit/word/whitespace
  classes, bracket ranges, negation and `*`, `+`, `?`; other patterns,
  non-ASCII and active character subjects, and unqualified assignment scopes
  remain explicit unsupported results. The internal matcher is currently a
  Script primitive rather than the native introspectable macro. A 180-case
  generated matrix and targeted character-category, branching and meaning
  checks agree with TeX Live 2025. Interpreter and JIT forced-GC checks pass.
  The four-case focused GTest passed three cases; the integer-compare case
  exceeded its unchanged 60-second limit both alone and with pre-regex Script
  sources, so that performance failure predates the matcher.
  Regex document source mapping is guarded pending native tree qualification.
  The later string-comparison checkpoint records the next CSV blocker; no CSV
  rendering parity is claimed.

- **Expl3 `exp_args:NnV`, 2026-11-26:** Script preloads the native expandable
  helper, preserving the first argument and expanding the second variable's
  value (D10.2.2v2). Five TeX Live comparisons cover unexpanded control tokens,
  grouping, integer registers, empty values and aliases. Three focused GTests
  and interpreter/JIT forced-GC runs pass. Its value-argument document mapping
  remains guarded. The subsequent regex checkpoint records the next CSV blocker.

- **Headless LaTeX performance, 2026-11-26:** The reader splits physical lines
  once, retaining each terminator and the byte and code-point cursors. TeX
  still applies the current category codes during token reads (S1.6, S2.5.8,
  D10.2.2v2). The showcase's line scan dropped from 1.61s to 0.013s. Output
  reconstruction accumulates fragments and span records through the shared
  source builder and materializes them once. The control-word suffix state
  carries escape parity across fragments. The isolated release showcase fell
  from 12.87s to 6.73s and the math-intensive view from 8.97s to 6.53s.
  Mixed line endings, UTF-8 raw spans and reconstructed segment boundaries
  have focused goldens. The original iframe replay with the full showcase
  passes. The normal release baseline takes 15.74s and 14.84s for the two
  LaTeX views with seven View workers. With `LAMBDA_RADIANT_VIEW_TEST_JOBS=3`,
  those views take 8.37s and 7.89s, and all seven targeted cases finish under
  ten seconds. Evidence is in `temp/radiant-under-10/`.

- **Iframe load and kernel resource retries, 2026-11-26:** Profiling the
  showcase found 15 attempts that repeatedly expanded the same kernel as new
  package resources arrived. A preload is now reusable only when it completes
  with resource requests enabled and no captures supplied. The host keeps one
  precisely rooted snapshot per profile, and each attempt binds its current
  authorized captures (D6.3.3, D6.4.2, D10.2.2v2, D11.1.2). The full
  showcase exceeded the navigation replay's ten-second assertion budget
  before the subsequent reader and reconstruction changes.
  The parent-click replay uses a focused scrollable document that loads two
  packages and checks both iframe commits. The full showcase has its own View
  smoke case. Both View cases pass in the Radiant baseline. Six resource
  authority cases and the capture-reuse golden pass in interpreter and JIT.
  The kernel-message golden's first group reports the same pre-existing
  mismatch with both release builds. Evidence is in
  `temp/radiant-three-failures/`.

- **Counter diagnostics and generated commands, 2026-11-26:** Counter errors
  and exhausted register allocation now retain the native messages and source
  locations, rather than only their codes (S1.8, D10.2.2v2). Generated counter
  commands retain their invocation origin, and the digester admits their
  source mapping only after checking the reconstructed spelling. Fourteen
  diagnostic cases, eight complete diagnostic trees, one included-file tree,
  and sixteen generated-command text/tree cases match the retired engines.
  The previously failing counter GTest and eight focused reconstruction,
  spacing, rule, table and new parity tests pass. Both new fixtures also pass
  interpreter and JIT under forced GC and freed-memory poisoning. The counter
  golden's two diagnostic AST expectations were updated after independent
  full-tree comparison.

- **Scoped expl3 input streams, 2026-11-26:** The input-stream free list and
  allocated stream variable now follow TeX assignment scope under negative
  `globaldefs`; physical open and close effects remain visible across group
  boundaries (S1.8, D10.2.2v2). Eight TeX Live comparisons cover root and
  nested groups, reuse, counter restoration and a later global override. The
  new fixture passes interpreter and JIT under forced GC; six focused stream
  and group GTests and the interpreter/JIT file-authority checks pass.
  The broader counter fixture also failed against the pre-change Script
  snapshot; its subsequent diagnostic and reconstruction fixes are recorded
  above. Stream missing-file diagnostics remain open.

- **Included environment definitions, 2026-11-26:** Twelve complete trees from
  the retired native engines match Script for included multiline environment
  definitions, blank lines, comments, CRLF and adjacent definitions
  (S1.8, D10.2.2v2). The new golden fixture and existing foreign-definition
  fixture pass; the new fixture also passes interpreter and JIT with forced GC
  and freed-memory poisoning. The older report's original temporary fixture is
  unavailable, so these comparisons qualify the tested cases without closing
  that exact historical report.

- **Unicode reader scaling, 2026-11-26:** A release-only reader probe took
  0.16 seconds for 4,001 characters and 3.95 seconds for 32,001 characters
  when the source contained one non-ASCII scalar; an otherwise comparable
  32,001-character ASCII source took 0.67 seconds. The reader repeatedly
  slices the complete source at character offsets, and the runtime's Unicode
  substring path recounts and rescans it (S1.8, D10.2.2v2). A temporary
  character-array cache increased both 32,001-character cases to about
  4.65 seconds, so it was not applied. A second prototype reduced the isolated
  Unicode probe to 0.87 seconds and preserved one complete algebra tree in
  36.21 seconds versus 40.52 seconds for its control, but differed on malformed
  UTF-8. Full validation at each line regressed the ASCII probe, and inconsistent
  invalid-text length and slice boundaries are recorded as LI-124. No reader
  optimization has been applied. The probes and uncommitted experiments are
  under `temp/latex-retirement/`.

- **External shape fingerprint cost, 2026-11-26:** Input now combines pointer
  identities as words instead of hashing each pointer byte; collision matches
  still check the complete shape (D3.4.3, D9.1.3v2). The 96-case MarkBuilder
  suite passes against the candidate. Two release bundled-fixture comparisons
  took 53.43/49.82 and 52.24/48.17 seconds for control/candidate, with exact
  golden output. The previously timed-out bundled GTest now passes with the
  optimized release executable and unchanged 60-second limit. These are
  individual measurements, not a statistical benchmark. The corpus timeout
  remains open; see LI-123 in the input ledger.

- **Stream whitespace in math, 2026-11-26:** A broader 48-case retired-engine
  comparison found that `readline` can emit a category-12 carriage return
  inside math. The native math scanner ignores the reconstructed whitespace;
  Script formerly emitted punctuation. The digester now applies the native
  whitespace rule to both math atoms and argument scanning (D10.2.2v2). The
  comparison now matches, including six endline characters; the regression
  fixture contains the independently captured native trees. Six focused math
  and stream GTests pass; the stream fixture also passes interpreter and JIT
  with forced GC and freed-memory poisoning.

- **Stream document mapping, 2026-11-26:** Constructor argument mode preserves
  stream commands inside arguments; the older eager mode does not. The document
  digester now admits stream and EOF operations only in the former mode
  (S1.8, D10.2.2v2). Twenty-three complete ASTs captured from the retired native
  engines cover file reads, closed streams, EOF, math, definitions, formatting
  arguments and aliases. The old mode still rejects its confirmed nested-command
  mismatch. The focused stream and EOF GTests pass, as does the new comparison
  in interpreter and JIT with forced GC and freed-memory poisoning. Other
  operation-specific source-mapping guards remain in place.

- **Current suite and performance investigation, 2026-11-26:** The broader
  document run passed 83 of 86 cases; the corpus and bundled-package cases hit
  their 60-second limits, and the table case still reports six CSV failures
  behind the expl3 source-mapping guards. One optimized release measurement
  of the bundled fixture took 53.26 seconds on the current Script versus
  51.27 seconds at `64ecd0563`, with both outputs matching the golden. Sampling
  identifies repeated external map-shape fingerprinting during TeX expansion;
  its safe optimization and the corpus timeout remain open. The generated-table
  and stream fixes below do not qualify removal of the expl3 guards.
- **Generated table headers, 2026-11-26:** The digester now accepts generated
  character-only table width and column groups when each reconstructed byte
  and adjacency is verified (D10.2.2v2). Five complete trees, including
  generated `tabularx` width and a nonadjacent group, match the retired native
  engines exactly. Generated headers containing control sequences remain
  unsupported. In a diagnostic probe only, bypassing existing operation
  guards now yields a CSV table tree; the guards have not been removed or
  qualified for general production use.

- **Expl3 replication, 2026-11-26:** Script preloads native expandable
  `prg_replicate:nn` and its decimal helpers (D10.2.2v2). TeX Live
  comparisons cover zero, multi-digit counts, arithmetic, nested replication
  and unexpanded tokens. Negative-count diagnostics and replication source
  mapping remain explicitly unsupported. Csvsimple's simple expansion now
  reconstructs a concrete `|l|l|` table with all three data rows. A diagnostic
  probe that bypasses only the existing guards still finds generated table
  header source mapping unsupported. The bounded header fix and remaining
  operation guards are recorded in the current checkpoint above.

- **Expl3 token-list mapping, 2026-11-26:** Script preloads the native
  inline and expandable function maps with their eight-item scanner, break
  points and global nesting counter (D10.2.2v2). TeX Live comparisons cover
  groups, ignored spaces, the scanner boundary, nested maps and breaks.
  A later replication operation and the remaining document source-mapping
  blocker are recorded in the current checkpoint above. Constructor argument
  handling and token-map source mapping remain unsupported.

- **Expl3 Roman integer conversion, 2026-11-26:** Script preloads the native
  lower- and upper-case conversion macros and their letter emitters
  (D10.2.2v2). TeX Live comparisons cover arithmetic, zero and negative
  inputs, output category codes and generated control-sequence names. This
  fixes csvsimple's missing `endcsname`; the later expansion outcome is in the
  token-map checkpoint above. Integer source mapping remains unsupported.

- **Expl3 dynamic token-list assignment, 2026-11-26:** Script preloads the
  standard local/global `:cn` and `:cx` setters (D10.2.2v2). TeX Live
  comparisons cover expanded names, unexpanded and expanded values, aliases
  and scope. Constructor argument handling and dynamic-assignment source
  mapping remain unsupported. Csvsimple's subsequent generated-name failure
  and fix are recorded in the Roman conversion checkpoint above.

- **Expl3 sequence mapping, 2026-11-26:** Script preloads the native inline,
  indexed-inline and expandable indexed-function sequence maps, including
  their scoped item definitions and break cleanup (D10.2.2v2). TeX Live
  comparisons cover nested maps, indices, grouped items, early breaks and
  counter restoration. Constructor argument handling and sequence-map source
  mapping remain unsupported. Csvsimple's later blocker is recorded in the
  dynamic token-list checkpoint above.

- **Expl3 string cases, 2026-11-26:** Script preloads the native expandable
  `str_case:nn` conditional family and its delimiter-scanned helpers
  (D10.2.2v2). TeX Live comparisons cover first-match behavior, all branch
  forms and unexpanded control-sequence keys. This fixes csvsimple's grouped
  split delimiter: the missing case macro had remained in its separator token
  list. Csvsimple's later blocker is recorded in the sequence-map checkpoint
  above; string-case source mapping remains explicitly unsupported.

- **Expl3 token removal, 2026-11-26:** Script preloads the four native local
  and global, once and all removal wrappers over the shared token-replacement
  implementation (D10.2.2v2). TeX Live comparisons cover grouped tokens,
  aliasing and scope. Empty search patterns, constructor argument handling
  and replacement source mapping remain explicitly unsupported. Csvsimple
  reached a grouped sequence-split delimiter, traced and fixed in the current
  string-case checkpoint above.

- **Expl3 boolean expressions and loops, 2026-11-26:** Script preloads the
  native expandable boolean parser, alignment-safe helpers and four expression
  loop forms (D10.2.2v2). The macro-source reader now preserves TeX's category
  for `&`; otherwise native `&&` helpers reject their delimiter. TeX Live
  comparisons cover precedence, negation, grouping, loop entry conditions and
  an input stream through EOF. Boolean-expression source mapping remains
  explicitly unsupported. Csvsimple's later blocker is recorded in the token
  removal checkpoint above.

- **Expl3 value insertion, 2026-11-26:** Script preloads the native
  `exp_not:V`/`:v` and local/global left/right value-insertion macro bodies
  (D10.2.2v2). TeX Live comparisons cover unexpanded token values, dynamic
  names, aliases, group scope and integer-register values. Constructor argument
  handling and value-source mapping remain explicitly unsupported. Csvsimple
  reaches the later blocker recorded in the boolean-expression checkpoint.

- **Expl3 inline comma-list mapping, 2026-11-26:** Script implements literal
  and variable inline maps with normalized list storage, scoped protected
  callbacks, nested map counters and the standard break macros (D10.2.2v2).
  TeX Live comparisons cover grouping, empty items, nesting, break results and
  counter restoration. The document digester still rejects source mapping
  for this operation. Csvsimple's later blocker is recorded in the value
  insertion checkpoint above.

- **Expl3 choice and boolean keys, 2026-11-26:** Script now implements
  `.choices:nn` and local/global boolean and inverse-boolean key properties
  (D10.2.2v2). Choice dispatch uses scoped key macros, and its index is a TeX
  count register whose value is restored across nested key calls. TeX Live
  comparisons cover choice values and indices, defaults, inverse and global
  booleans, state restoration and a user-defined unknown-choice handler.
  Unknown-choice diagnostics and document source mapping remain unsupported.
  Csvsimple's later blocker is recorded in the inline-map checkpoint above.

- **Expl3 key definitions and execution, 2026-11-26:** Script now handles
  scoped `.code:n`, `.default:n`, `.initial:n`, `.tl_set:N`, `.tl_gset:N`
  and `.meta:n` properties through `keys_define:nn` and `keys_set:nn`
  (D10.2.2v2). TeX Live comparisons cover initial ordering, defaults, nested
  calls and key context, local redefinition, global token-list assignment and
  variable creation. The document digester still rejects key source mapping;
  other properties, diagnostic recovery, complex name tokens and assignment
  prefixes remain explicitly unsupported. Csvsimple's later blocker is
  recorded in the current choice-key checkpoint above.

- **Adapter and constructor regression migration, 2026-11-26:** Both
  previously failing fixtures now pass without changing their expected
  assertions (D10.2.2v2). The no-adapter cases compare exact output captured
  from the retired C engine with matching resource settings; configured
  adapter cases still compare the production entry point. The constructor
  fixture avoids calling that entry point after the Script profile has already
  reported an unsupported operation, and shares immutable kernel preloads.

- **Expl3 key-value parsing, 2026-11-26:** Script implements the expandable
  `keyval_parse:NNn` and `keyval_parse:nnn` token parsers, including active
  separators, nested groups, omitted values and empty values (D10.2.2v2).
  Four expandable results match TeX Live; malformed key diagnostics and
  document source mapping remain explicitly unsupported. This supplies the
  common parser used by the bounded key-definition and execution support
  recorded above.

- **Expl3 input-stream lifecycle, 2026-11-26:** Script now opens, reads,
  checks EOF and closes expl3 input streams through the existing captured
  resource broker, reusing TeX stream slots (S1.8, D10.2.2v2, D11.1.2).
  TeX Live comparisons cover raw/tokenized lines, EOF and no-value behavior,
  slot reuse, reopening, dynamic names, filename spacing and scoped/global
  reads. An authorized file and denied symlink pass the security runtime check
  in interpreter and JIT. Missing-file diagnostics remain explicitly
  unsupported. Later negative-`globaldefs` scoping, document mapping and the
  csvsimple blocker are recorded in the current checkpoint above.

- **Expl3 stream declarations and integer registers, 2026-11-26:** Script now
  creates closed input-stream variables with TeX's terminal-stream meaning and
  exposes integer creation, use, zeroing, setting, increment and decrement via
  the existing TeX register operations, including global and dynamic-name
  forms (D10.2.2v2). TeX Live comparisons cover values, arithmetic, scope,
  dynamic names and duplicate stream declarations. Focused GTests and both
  execution tiers with forced GC pass. This ports stream declaration only;
  opening and reading expl3 streams remain open. Csvsimple now reaches the
  unported `keys_define:nn` instead of stopping on stream or integer creation.

- **Expl3 generated variants, 2026-11-26:** Script now builds ordinary TeX
  macros for `cs_generate_variant:Nn` over `N`/`n` base signatures with
  `N`, `n`, `c`, `V`, `v`, `o` and `e` argument forms (D10.2.2v2). The
  generated expansion helpers preserve the existing source-mapping guards.
  TeX Live comparisons cover expansion order, protected meanings, aliases,
  global definitions, later base redefinition, conditional variants and the
  sequence and token-list operations used by csvsimple. The fixture passes
  interpreter and JIT under forced GC, and four focused GTests pass. Other
  forms, including `x` and `f`, remain explicitly unsupported. The phase-five
  tables test still fails: csvsimple now reaches the unported `ior_new:N`
  instead of stopping at variant generation.

- **Expl3 token-list replacement, 2026-11-26:** Local and global `tl_replace`
  operations now replace the first or all top-level matches without expanding
  search or replacement tokens (D10.2.2v2). Ten cases match TeX Live,
  including grouped text, control tokens, scope and empty replacements; empty
  or grouped search patterns remain explicitly unsupported. The new fixture
  and sequence regression pass with forced GC in interpreter and JIT. Four
  focused GTests pass; an additional token-list equality matrix initially
  timed out, then passed unchanged after sharing the existing immutable expl3
  test preload. Generic variant generation and csvsimple remain open.

- **Document suite, 2026-11-26:** The current isolated 86-case LaTeX GTest
  run passed 84. The phase-three corpus exceeded the 60-second per-script
  timeout but matches the full golden in the separate release run below; the
  phase-five tables golden differs in csvsimple cases, whose expl3 variant
  generator and other binding operations are still incomplete. See
  `temp/latex-retirement/document-suite-current.log` (D10.2.2v2).

- **Token and source reconstruction cost, 2026-11-26:** A symbolized optimized
  profile showed repeated nested contract admission while the output token
  array grew. Token emission and reconstructed segments now use concatenation
  that retains the existing array certificate; reconstruction also caches
  immutable source/output bytes, derives prefix lengths from its last segment,
  and searches ordered source segments by offset (D10.2.2v2, D4.3.3). Exact
  source-mapping and document fixtures pass, including both tiers with forced
  GC and poisoning. A reconstruction matrix that timed out now passes in
  2.4 seconds after sharing its unchanged kernel preload. With the same release
  host, single samples for the algebra parse changed from 78.94 to 41.97
  seconds, and the complete 31-document corpus changed from 226.31 to 186.60
  seconds while matching the same golden byte for byte. These are single
  samples, not a statistical benchmark; the 60-second corpus timeout remains
  open. Logs and profiles are under `temp/latex-retirement/`.

- **Full phase-three corpus, 2026-11-26:** The complete 31-document release
  run now matches the existing golden byte for byte (SHA-256
  `dd575e39107edf5a83033846084f77a62a2d481b9ea9c370e70b855920eedd76`).
  It took 226.31 seconds, exceeding the unchanged 60-second GTest limit. The
  process was observed using about 3.2 GB RSS near completion; the sandbox
  denied `time -l`'s final sysctl, so that value is an observation rather than
  a reported peak. `temp/latex-retirement/corpus-release-after.out` and
  `corpus-release-after.err` preserve the run (D10.2.2v2). A separate release
  measurement of the 35 KB algebra document took 78.94 seconds to parse and
  82.26 seconds to parse and render. These are single samples; they show the
  document exceeds the limit even without the rest of the corpus. An optimized
  profiling build is in progress to locate the parsing cost.

- **Expl3 sequence splitting, 2026-11-26:** Script now implements local and
  global `seq_set_split` and `seq_set_split_keep_spaces` with top-level token
  delimiters, empty fields and empty-delimiter token mapping (D10.2.2v2).
  Ten exact cases match TeX Live, including grouping, control and space
  delimiters and scope; the new test also passes interpreter and JIT with
  forced GC and poisoning. The shared token-list comparison and comma-list
  splitter are reused. Four existing sequence matrices now share an immutable
  kernel through a test helper; the three initially timed-out matrices pass
  their unchanged goldens in 13–16 seconds, and the reverse matrix passes too.
  Group tokens in a delimiter remain explicitly unsupported, and generic
  `cs_generate_variant:Nn` and the csvsimple fixture are still open.

- **Math definition echoes, 2026-11-26:** Math digestion now reads the
  reconstructed bytes of definition source spans and permits verified comment
  gaps (D10.2.2v2). Exact trees for a definition with a comment and one with
  math shifts match the isolated retired parser. The semantic-inference sample
  now matches all six expected fields, as do the other three corpus records
  previously mismatching in the isolated run. The new tree fixture passes
  interpreter and JIT with forced GC and poisoning. A broader math sweep passed
  16/17; the one stale `\\,` expectation was checked against the retired
  parser, corrected, and passed its focused rerun. The complete corpus has not
  yet been rerun, and its aggregate
  timeout remains open.

- **Nested math alignment, 2026-11-26:** The amsmath adapter now tracks nested
  environments when it splits rows and cells and when it extracts outer-row
  annotations (D10.2.2v2). An aligned or split environment is therefore passed
  intact to the existing math parser and typesetter. The scientific-writing
  sample now matches all six corpus fields; the new fixture checks nested
  separators, annotation ownership, numbering and rendered cells. It passes
  focused GTest and forced-GC interpreter/JIT runs; the existing amsmath and
  phase-five math tests also pass. Semantic-inference and the corpus timeout
  remain open.

- **Raw adapter arguments, 2026-11-26:** The Script engine now installs the
  host's raw-constructor registry after kernel preload and copies balanced
  arguments and math spans without expanding them. Constructor aliases emit
  the copied meaning's name. Unfinished math text groups retain their emitted
  source, matching the retired parser (S1.8, D10.2.2v2). Captured native trees
  cover raw and ordinary aliases, argument forms, math and cache isolation;
  the sequent sample now matches all six expected corpus fields. The two new
  fixtures pass focused GTest and forced-GC interpreter and JIT runs. The
  `adapter_state` and `constructor_mode` fixture migrations are recorded in
  the current checkpoint above.
  The semantic-inference and scientific-writing corpus records and aggregate
  timeout remain open.

- **Lua source in definitions, 2026-11-26:** The 31-document corpus completed
  in an isolated release run with four records different from the golden; it
  still exceeds the GTest timeout. Script now captures `\directlua` and
  `\luaexec` groups inside re-read definition bodies and parses their emitted
  arguments in math without executing Lua (S1.8, D10.2.2v2). The arithmetic
  sample now matches all six expected record fields. Definition, nested-command
  and math trees match the isolated retired parser, five neighboring GTests
  pass, and the new exact-tree fixture passes interpreter and JIT with forced
  GC and poisoning. The remaining three corpus records and aggregate timeout
  are open; see `temp/latex-retirement/corpus-release-before.out`.

- **Optional heading titles, 2026-11-26:** The document parser now permits a
  sectioning command without a required title group and leaves following text
  and whitespace intact. This restores `\part` inside exam questions while
  retaining optional and spaced title groups (D10.2.2v2). The formerly failing
  phase-five class golden and three neighboring GTests pass; the exact heading
  tree matches the isolated retired parser and passes interpreter and JIT
  with forced GC and poisoning.

- **Tabbing and alignment trees, 2026-11-26:** Script now parses `tabbing`
  using tab-stop meanings for top-level `\=`, `\'` and `\`` while keeping
  accents inside nested groups. Both captured trees match the isolated retired
  parser, and the phase-five kernel golden now passes (D10.2.2v2). Nested
  `split` and equation-tag alignment trees also match the retired parser; the
  old matrix golden now records their supported results and retains a separate
  generated-name source-mapping guard. Six focused GTests pass, and the two
  new tree fixtures pass interpreter and JIT with forced GC and poisoning.

- **Raw-environment matrix, 2026-11-26:** The timed-out matrix repeatedly
  initialized the same kernel for each input. It now constructs one immutable
  preload for each of its two exact configurations and retains every source,
  comparison and expected result. The unchanged golden passes the original
  GTest limit and passes interpreter and JIT with forced GC and poisoning
  (D6.4.2, D10.2.2v2). As with other migrated tests, comparisons through
  `parse` now exercise Script on both sides; they are not independent native
  parity evidence.

- **Raw title and inline code, 2026-11-26:** Script now preserves the six
  titling hook arguments as raw code, including environment boundaries that
  close in a later hook. Inline raw commands containing `%` are accepted when
  the rest of that source line cannot conceal an environment boundary; the
  unsafe case remains guarded (D10.2.2v2). The supported trees in both new
  fixtures match the isolated retired document parser, and the fixtures pass interpreter
  and JIT with forced GC and poisoning. The formerly failing phase-five front
  and text-package goldens now pass. A neighboring seven-case raw suite passed
  five: the raw-environment matrix timed out at the existing 60-second limit,
  and the raw-inline matrix still expects native parsing in five guarded
  cases now routed through Script. Neither failure is waived or counted as a
  pass; see `temp/latex-retirement/gtest-raw-arguments.log`.

- **Filename parsing and indirect expansion, 2026-11-26:** Script now executes
  `\filename@parse` as scoped TeX definitions, preserving the active letter and
  space categories. Nine text and diagnostic cases match the isolated retired
  engine, including empty names, multiple dots, expansion, scope and `\ifx`.
  The shared text-macro constructor now also uses the native short-definition
  and category rules. The bundled `ifthen` package exposed an independent
  `\expandafter` bug: token-list protection was applied to an indirectly
  expanded `\the` or `\unexpanded`. Only direct definition scans retain that
  protection now; four retired-engine comparisons and both actual `ifthen`
  equality branches match. The full bundled-package fixture and the three
  focused goldens pass the existing GTest limit (S1.8, D10.2.2v2). The
  bundled fixture finished close to its limit. Seven adjacent definition,
  kernel-reuse and generated-environment fixtures pass, and the filename,
  `ifthen` and indirect-token-list goldens pass interpreter and JIT with
  forced GC and poisoning; see `temp/latex-retirement/`.

- **Format-date checks, 2026-11-26:** The kernel implements
  `\IfFormatAtLeastTF` through LaTeX's version comparison macros and the
  active `\fmtversion`, replacing the retired engine's unconditional true
  branch. Past, equal, future, slash-form and changed-version cases agree with
  the installed TeX Live reference; the exact Script golden and neighboring
  loop, definition and kernel-reuse tests pass interpreter and JIT with forced
  GC and poisoning (S1.8, D10.2.2v2). This deliberately corrects the old C
  approximation for future dates. The bundled-package fixture now reaches
  the unported `\filename@parse`; it is not yet passing.

- **Package argument definitions, 2026-11-26:** Script now implements
  `\@yargdef` using the existing control-target, numeric scanner, argument
  converter and scoped-definition machinery. Six independent retired-engine
  comparisons cover ordinary and bracket-delimited arguments, zero and invalid
  counts, protected definitions and illegal parameter recovery. The exact
  golden passes interpreter and JIT with forced GC and poisoning (S1.8,
  D10.2.2v2). The bundled-package fixture progresses to the still-unported
  `\IfFormatAtLeastTF`; it is not yet passing.

- **Generated document environments, 2026-11-26:** Script digestion accepts
  generated environment boundaries after checking their reconstructed opening
  and closing bytes, maps their offsets back to the source, and reads
  `tcolorbox` options with the existing environment-option scanner (D10.2.2v2).
  The previously failing nested-macro fixture now matches its golden. Seven of
  eight adjacent environment fixtures pass; the eighth has two older guarded
  cases whose output is unchanged against the preceding Script version, but
  whose historical native-parsed assertions remain unmet. A focused generated
  environment tree also passes interpreter and JIT with forced GC and
  poisoning. Evidence is in `temp/latex-retirement/`.

- **Package loops and source mapping, 2026-11-26:** Script now executes the
  retired engine's `\@for` and `\@tfor` token loops, including grouped and
  empty items, one leading macro expansion, nested loops and malformed-syntax
  recovery. Nine exact text/diagnostic cases were captured independently from
  an isolated retired engine built under `temp/latex-retirement/`. Generated
  escaped text now blocks document digestion only when its tokens reach output;
  balanced verbatim spans can be checked across their enclosing source group
  while unmatched spans retain the guard (S1.8, D10.2.2v2). The previously
  failing showcase now matches its complete golden. The loop, generated-text
  and balanced/unbalanced verbatim regressions pass interpreter and JIT with
  forced GC and poisoning. A follow-up of eleven tests passed four and still
  found seven mismatches or failures; the 31-document corpus timeout remains
  open. See `temp/latex-retirement/gtest-after-loops.log`.

- **Document environment headers, 2026-11-25:** The 70-case LaTeX document
  sweep initially passed 59 cases; ten mismatched and the 31-document corpus
  exceeded its existing 60-second limit. Script digestion now preserves the
  native argument groups for `subfigure` and `subtable`, the column and width
  headers for `longtable`, `tabularx` and `xltabular`, and the separate
  `multicols` preface (D10.2.2v2). The previously failing caption and document
  boundary goldens now match. Three independent exact-tree fixtures pass in
  interpreter and JIT with forced collection and poisoning. Other mismatches
  remain under investigation; the showcase also reaches unported `\@for` in
  the bundled `calc` package. The original sweep and focused evidence are in
  `temp/latex-retirement/`; no full passing sweep is claimed.

- **Resource restarts and expl3 preload, 2026-11-25:** Repeated parses no
  longer evict their initial kernel snapshot when the host captures a package
  resource, and the separate expl3 snapshot is prepared only when requested.
  Both snapshots remain keyed by all options and the host still authorizes each
  kernel and resource read (D6.4.2, D10.2.2v2, D11.1.2). A resource-plus-expl3
  regression passes in interpreter and JIT with forced GC and poisoning; eight
  captured-member and revoked-authority checks pass across both tiers. The
  bibliography phase-two golden matched in isolated release runs before and
  after the change, taking 66.98 and 10.21 seconds respectively. These are
  single samples, not a statistical benchmark. Release binaries and output are
  retained under `temp/latex-retirement/`. The phase-two bibliography and
  phase-five math fixtures also pass the existing 60-second GTest limit in an
  isolated debug-host run.

- **Unicode math arguments, 2026-11-25:** Script now consumes one complete
  Unicode code point in an unbraced math superscript or subscript, preserving
  subsequent characters and generated-token source mapping (S2.5.8,
  D10.2.2v2). Independent AST, MathML and document checks cover BMP and
  supplementary characters and pass in auto, interpreter and JIT with forced
  GC and freed-memory poisoning. This returns valid Unicode instead of the
  retired native engine's invalid-byte behavior recorded in LI-118.

- **Production retirement and remaining parity, 2026-11-25:** The Input archive
  excludes the C TeX engine and the executable contains no native TeX engine
  symbols. Runtime and native CLI document inputs use the Script facade;
  standalone Input reports that it needs a runtime host. Standalone TeX math
  and Radiant's embedded Markdown math now use Script; native AsciiMath remains
  available (D10.2.2v2, D9.1.3v2, S1.8). The C TeX engine source and its
  direct test executable have been removed. Six native document tests were transferred to
  `test_latex_retired_native_documents.ls`, which passes all eight checks.
  Encoding dispatch, macro, environment and expl3 regressions were also
  transferred; the expl3 cases pass after implementing undefined-control case
  conversion, token expandability and unexpanded string equality. Existing
  comparisons that call `parse` now compare Script with itself, so they cannot
  establish native parity. Preserve independent expected results when migrating
  them. `test_math_extended_constructs` still reports two equation-reference
  failures caused by unsupported `\\tag` and `\\label` now pass. Text groups,
  embedded math, verbatim and raised boxes also support the rich-text golden,
  which passes in auto and interpreter. A hot typed error handler exposed
  [LR09-37](<../Lambda_Issue_Ledger (fixed).md#lr09-37>), fixed by applying
  the existing constrained-predicate satellite admission rule (S11.4.11,
  D8.3.1). The migrated custom-format test now exercises automatic restart,
  caller-captured text and binary `expl3.ltx` files, missing files, isolation,
  and bundled authority for nested inputs. Automatic host capture of the
  native format remains open; the Script facade currently loads the format
  only when its authorized snapshot is supplied (D10.2.2v2, D11.1.2).
  Automatic and lazy archive member reads now select Script using the member's
  actual filename and retain no external grants; [LI-122](<../Lib_Input_Issue_Ledger (fixed).md#li-122>)
  records the detection fix. Full corpus parity and native-client qualification
  remain open.
  The resource broker grants shipped metadata and reads without enumeration;
  the resource root now uses metadata, while every resource read still requires
  authorization (D11.1.2). The eight scoped local-package, include, captured
  member and revoked-authority cases pass across both tiers; all five HTTP
  handoff cases and all five standalone Input math/scanner cases also pass.
  The earlier Lambda baseline did not complete because
  an aborting test child hung in `pthread_kill`; it is not a passing baseline.

- **Math source forms, 2026-11-25:** The 35-case captured native source-format
  golden now passes. Script digestion preserves binomial, general and infix
  fractions, sized delimiters, arrows, colors, images and unknown generic
  commands; matrix rows retain their optional gaps and each cell owns its math
  list. Focused fraction, arrow, AMS primitive and rich-text regressions pass
  (D10.2.2v2). A settled math sweep passed 48/54: the failures were feature
  commands, HTML snapshot, phase-five packages, text escapes, MathML export,
  and a 60-second metrics timeout. Follow-up fixes now pass the feature,
  phase-five, text-escape and MathML export goldens. Text and chemistry
  arguments retain their raw contents during standalone math expansion, so an
  unmatched dollar no longer corrupts the surrounding group. The HTML document
  now parses after preserving normalized whitespace in source-map validation;
  its full rendering matched the captured snapshot in a direct run but exceeded
  the 60-second test limit. A resource-free kernel preload is now retained in
  the evaluator's precisely rooted realm capsule and reused only for equal
  options; each parse still authorizes and reads the kernel. The metrics and
  HTML tests now pass within the existing limit, and the complete 56-case math
  suite passes (57/57 after the Unicode argument regression). Definition isolation and changing captured resources pass in
  both tiers and under forced GC with freed-memory poisoning. The eight scoped
  authorization regressions pass, including a cached math parse before kernel
  read authority is revoked (S1.8, D10.2.2v2, D6.4.2, D11.1.2).

- **TeX error recovery, 2026-11-25:** Incomplete expl3 declarations previously
  caused `parse(..., 'tex')` to raise `tex-runaway-argument` because their
  implementation scanned every argument even after the first failure. They now
  use the shared long-macro scanner and recovery, retaining partial output and
  native-shaped diagnostics. Independent missing, unclosed and extra-brace
  checks pass in both tiers, and all seven migrated native-engine regressions
  pass. An interrupted 420-case LaTeX sweep exposed constructor-mode failures,
  an environment-option mapping failure, and a phase-five math timeout; it is
  not a passing suite. Extending the resource-free kernel cache to TeX and
  LaTeX made the phase-five fixture pass in isolation, but it timed out again
  during a concurrent document sweep. Document definition/resource isolation
  passes in both tiers; the earlier cache passed forced GC with freed-memory
  poisoning, while the final expl3-bootstrap GC run was interrupted. TeX and
  LaTeX now default to constructor-aware
  argument scanning, retaining the native behavior for assignments inside
  formatting commands; an explicit false option retains legacy behavior.
  Separate per-format and expl3 restart preloads prevent alternating inputs
  from evicting one another. The 29-case expl3 constructor matrix and the
  token-list and environment-option boundary goldens now pass. The 70-case
  document subset was interrupted when the goal was cleared. The timeout and
  broader qualification remain open (S1.8, D10.2.2v2, D6.4.2, D11.1.2).

- **Earlier HTTP Path checkpoint:** The baseline did not finish:
  `ValueRepresentationTest.UnsupportedDirectTransitionFailsClosed` timed out
  at 180 seconds and also hung in an isolated rerun. Both aborting children
  remained in macOS uninterruptible exit state, leaving the runner's pipe open;
  the runner and isolated parent were stopped. Sampling shows the child in
  `abort` → `pthread_kill`; the reason the OS call does not complete is not
  yet confirmed.
  See `temp/latex-retirement/path-baseline.log` and
  `test_output/test_lambda_errors_gtest.log`. The Script handoff now reserves
  all eight root slots, including its captured kernel; the local Path include
  regression passes in both tiers with forced GC and freed-memory poisoning
  (D5.2.1). At that checkpoint standalone math still used Input's native
  direct parser; `digest.ls` already owns the corresponding Script math atom
  parser. The subsequent work exposed that parser for a runtime math entry point
  and migrated embedded markup math (D10.2.2v2,
  D9.1.3v2). A scratch probe using the shared reader and atom parser matched
  the native AST for nine of eleven simple formulas, including scripts,
  fractions, radicals, Unicode, and delimiters. It reported unsupported for
  spacing `\\,` and `\\text{...}`. This is evidence for reuse, not an admission
  policy or production fallback; definitions, recovery, source mapping and
  the full math fixtures remain to qualify. See
  `temp/latex-retirement/math-probe/out.txt` (S1.8).

- **HTTP Path and document base, 2026-11-25:** HTTP Path input now shares the
  URL renderer used by other target consumers, encoding each NameKey and
  rejecting ambiguous or unrepresentable resource names. The underlying Path
  preserves exact name lengths through both execution tiers, while OS and HTTP
  targets reject embedded NUL bytes (S2.4.2v6, S8.2.1v5, D11.1.2). Local Path
  TeX inputs now retain their resolved base for relative includes (D10.2.2v2).
  Native Path tests pass 28/28; NameKey and local include regressions pass in
  interpreter and JIT, and three HTTP loopback tests pass. HTTP IntKeys and
  literal dot-navigation keys are refused at the URL boundary. Standalone HTTP
  and native document/math clients still require migration; the native engines
  remain in the build. See [LI-120](<../Lib_Input_Issue_Ledger (fixed).md#li-120>).

- **HTTP handoff, 2026-11-25:** The downloader now passes the existing deferred
  source handoff through URL and HTTP Path targets, preserving the original
  standalone HTTP entry point and its format detection. Runtime `input()` sends
  detected and explicitly typed TeX/LaTeX URL inputs to Script without an
  Input-to-runtime callback (S1.8, D9.1.3v2, D10.2.2v2). A local HTTP regression demonstrates
  that the previous native route disagreed with direct Script parsing and the
  new route agrees. The Input HTTP test covers fresh and cached bytes; a runtime
  loopback test covers both routes and confirms a remote document does not fetch
  a relative TeX input (D11.1.2). The full loopback test runs as part of
  `make test-lambda-full`. At that checkpoint HTTP Path targets could not reach
  the handoff until LI-120 fixed their URL serialization; standalone HTTP
  callers still use the native path (S2.4.2v6, S2.4.3v4).

- **Captured archive members, 2026-11-25:** Explicit `input(member, 'tex' | 'latex')`
  and `parse(member, ...)` now hand the captured source to the Script engine.
  Input caches the captured source, not a runtime-owned result; repeated Script
  parses currently re-execute, so parsed-result caching remains open. A member carries
  no external resource grant even if its options name a base or supply resource
  snapshots (S12.4.1v2, D11.1.2, D10.2.2v2). Standalone lazy TeX reads now
  report that a runtime host is required instead of using the native engine;
  the later LI-122 change connects lazy virtual content when a runtime host is
  active. Two captured
  member regressions pass in both tiers under forced GC; the security isolation
  tests pass in both tiers under forced GC, and five native ZIP cache/member
  tests pass. At that checkpoint the HTTP, standalone document and math paths
  still needed migration.

- **Automatic detection handoff, 2026-11-25:** Local `input()` and `parse()`
  can ask Input to return the acquired TeX/LaTeX source and detected format to
  the runtime. Input keeps its format detection and never imports or calls the
  runtime; the runtime copies the source into its heap and invokes Script
  (D9.1.3v2, D10.2.2v2, S1.8). The original native target-loader signature
  and `InputParseOptions` layout remain available. A regression that failed on
  the prior mixed auto/native route now passes, along with a native-backed
  automatic document comparison. The final build and eight focused script
  tests pass; both auto tests pass in both tiers under forced GC, and scoped
  include and denied-symlink tests pass in both tiers under forced GC. At that
  checkpoint captured archive members, HTTP acquisition, standalone Input
  clients, and math still needed migration before C retirement.

- **Resource requests, 2026-11-25:** The Script expander requests missing local
  or bundled resources by scope and name; the runtime captures authorized bytes
  through `SecurityIOClient`, preserves a document base and restarts expansion.
  The shared input boundary supplies the native path containment rules. Shipped
  kernel bytes are also captured explicitly, so a restricted scope does not need
  `sys.lambda.home`. The broker now distinguishes authorized absence from a
  denied path. TeX and LaTeX include fixtures, no-base and missing-file
  diagnostics, job names and automatic expl3 bootstrap have native-backed
  regressions. The 10 focused script tests and all 27 file-broker tests pass;
  four focused security runtime cases pass in both tiers, also under forced GC,
  and the broker's absence, denial and symlink regression passes. The old native engine
  remains for automatic dispatch, standalone Input, math and embedded math;
  resource behavior for those paths and the full corpus remains to migrate and
  qualify (D10.2.2v2, D9.1.2v2, D11.1.2, S1.8).

- **Retirement ordered before parity, 2026-11-25:** The user explicitly
  directed retirement first and repair of failing Script tests afterwards.
  Runtime `parse` for explicit `tex`, `latex` and `latex-ts` and `input` with
  an explicit type now invoke the Script facade after authorized acquisition.
  At this earlier checkpoint the archived native cases matched through `parse`, but automatic type detection, captured
  archive members, native Input clients, math and embedded math still use the
  native provider, so it has not yet been removed. Existing tests that compare
  `parse` against Script now compare the same implementation; preserve their
  native expected results separately before relying on them. D10.2.2v2 assigns
  the final parser to Script in the runtime, retaining D9.1.3v2 and S1.8.

- **Earlier retirement checkpoint, 2026-11-25:** The user requested retirement of
  both native engines. At this checkpoint `tex`, `latex` and `latex-ts` still dispatched
  to `lambda-input`'s TeX expander and direct digester. The existing
  `InputScriptParser` host boundary, used by graph and TikZ, is a possible
  integration point, but the earlier direction to avoid a native-to-Script
  call boundary and the needs of standalone Input clients remain unresolved
  (D9.1.3v2).
  First build and qualify a script-owned parse facade that preserves explicit
  unsupported outcomes and caller-provided resources, then migrate every
  production and native-client entry point and qualify the full corpus before
  removing the native provider. TeX remains data (S1.8); resource acquisition
  and capabilities remain governed by D9.1.2v2 and D11.1.2. This migration
  was in progress and neither production dispatch nor the native engine was retired.
  A new Script-owned facade returns TeX output or LaTeX trees with explicit
  unsupported results and accepts captured resources. Initial text, tree,
  diagnostic and included-file comparisons match native. Shared expanded-text
  recovery now matches 48 native comparisons including EOF and missing braces
  in `\\message` and `\\errmessage`, in both tiers under forced GC. A first
  13-case archive preserves native text and trees independently of the native
  provider; it is only a starting point for corpus-wide qualification (S1.8,
  D10.2.2).

- **Property lookup, 2026-11-25:** Native-shaped `\\prop_get:NnN` and
  `\\prop_get:cnN`, including T/F/TF variants, now use the existing property
  splitter and native conditional flow. Twenty native text, message and
  diagnostic comparisons cover found/missing keys, assignments, aliases,
  protected expansion and macro meanings; two undeclared-target cases retain
  the explicit `__prop_split_wrong:Nw` boundary. The 22-case candidate
  regression passed interpreter and JIT with forced GC and poisoning, including
  after installation, and eight tree comparisons matched or reached independent
  guards. The regression also passes GTest. Expanded-key variants for N/c
  property names, V/v/o/e/x keys and c targets now use native-shaped argument
  helpers. The candidate matches 28 ordinary comparisons, ten macro-meaning,
  alias and protected-expansion checks, and eight independently defined,
  `\\ifx`-equivalent substitution probes. Sixteen document-tree probes match or
  reach independent guards. Invalid key-variable probes match native; undeclared
  properties retain the explicit splitter boundary. The three variant fixtures
  and existing lookup fixture pass candidate GTest; the four permanent variant
  regressions pass both tiers under forced GC. The installed package also passes
  all four regressions in JIT under forced GC; linked-property and non-expandable
  error recovery remain open (S1.8,
  D10.2.2).

- **Sequence function mapping, 2026-11-25:** Native-shaped
  `\\seq_map_function:NN`/`:cN` and map-break macros now execute through the
  general TeX macro engine. The isolated candidate matched 18 initial text,
  message and diagnostic comparisons, plus eight nested-break and expanded
  definition probes. Ten tree comparisons matched or reached independent
  digest guards. A 20-case regression passed both tiers with forced GC and
  poisoning. After adding a nested-break case, the installed 22-case regression
  passes both tiers with forced GC and poisoning; the existing sequence-membership
  and property regressions also pass in JIT under forced GC; the new regression
  also passes GTest. Inline mapping and the broader non-expandable message layer remain
  open (S1.8, D10.2.2).

- **Expanded-space skipping, 2026-11-25:** `\\ignorespaces` now uses the
  shared expanded-token scanner and returns the first non-space token to the
  input, as native does. Twenty-six direct, macro-generated, conditional, alias,
  grouping and EOF comparisons match native text, messages and diagnostics in
  both constructor modes and in both execution tiers under forced GC and
  poisoning. Ten document-tree probes match and two reach an existing inline
  definition guard; a permanent tree regression covers both outcomes. A
  subsequent `\\noexpand` probe exposed a one-use protection mismatch: the
  scanner returned the protection flag after it had already consumed it.
  The operation now shares numeric scanning's protection-reset helper; three
  additional native comparisons pass both tiers under forced GC and poisoning;
  the full regression passes GTest, and the shared expression-recovery golden
  matches under forced GC in JIT. The math-environment golden now records its
  newly native-equal `\\ignorespaces` tree and matches the live JIT result
  (S1.8, D10.2.2).

- **Property-list dependency investigation, 2026-11-25:** Native expandable
  messages depend on generic expl3 property lists. An isolated candidate in
  `temp/latex-parity/prop-candidate/` matches native on 18 ordinary and 13
  alias/conditional cases and reproduces the public macro meanings. It is not
  installed: `\\escapechar=-1\\prop_put:Nnn` executes the prototype's whole
  assignment during number scanning, whereas native first completes the
  numeric assignment; inserting `\\scan_stop:` makes the results match. Its
  dispatch metadata may also differ from an independently defined, `\\ifx`-equal
  macro, and duplicate declarations need native message recovery. The
  underlying helpers need a substitutable, correctly staged implementation
  rather than a per-name shortcut (S1.8, D10.2.2).

  A second isolated candidate uses the shipped upstream macro shapes for
  property assignment and flat-property lookup, including their expansion
  helpers, rather than dispatch metadata. Eight key, category-code and
  assignment-order probes match native text, diagnostics and messages in the
  interpreter. Alias, macro-meaning, conditional and duplicate-declaration
  checks match another 31 native comparisons, including an independently
  defined equivalent macro. The upstream-shaped flat-property operations are
  now installed; duplicate declarations explicitly stop at the unported native
  message machinery, and sampled document trees stop at the existing
  control-sequence existence source-mapping guard. The permanent regression
  passes 21 native text, diagnostic and message comparisons in interpreter and
  JIT, including forced collection and freed-memory poisoning; both were
  rerun against the private stable executable after workspace binary drift.
  Bootstrap now also preserves a `\\c_empty_prop` definition supplied by the
  caller's kernel; the regression passes both tiers under forced GC and
  poisoning.
  The constructor
  sweep's frozen expansion module predates this installation and needs
  requalification (S1.8, D10.2.2).

  Missing property assignment targets currently reach the explicit
  `__prop_split_wrong:Nw` unsupported boundary; six other empty, missing-read,
  expansion and replacement probes match native. A recovery regression was
  added after the stable sweep manifest and passes both tiers with forced GC
  and poisoning; it must be included in the sweep's final drift assessment.
  Removing the existence, token-list conditional and
  token-list assignment digest guards in isolation yielded 32 native tree
  matches and one independent prefixed-macro guard; 34 further comparisons
  match in both constructor modes. Historical token-list insertion examples
  still differ in legacy nested formatting, but match in constructor mode.
  A 30-case matrix found the same two legacy mismatches and no constructor
  mismatches. However, the insertion guard is shared with sequence helpers:
  an eight-case sequence comparison exposed a constructor-mode mismatch
  inside `\\textbf`, where unimplemented expandable-message tokens leaked into
  the output and diagnostics differed before message support. The shared
  insertion guard was restored pending a fresh tree audit. After expandable
  messages were installed, the previously failing nested sequence case and
  a further 30 token-list and sequence tree comparisons match native in
  constructor mode. Right-pop checks either match or reach the independent
  constructor-argument assignment guard. The token-list assignment guard is
  now limited to legacy mode, where the nested mismatch persists. Its 16-case
  regression passes interpreter and JIT with forced GC and poisoning. The
  token-list conditional and existence guards were subsequently removed after
  the audits below (S1.8, D10.2.2).

  A fresh isolated control-sequence existence guard audit now exercises
  branches that define commands: 16 body-definition comparisons and all eight
  mode variants of the original preamble-definition cases match native trees;
  a further 32-case alias, nested and dynamic-name audit has no silent
  mismatch. All 28 original guarded examples also match native trees. Their
  regressions now compare trees, text, diagnostics and messages and pass with
  forced GC in JIT; a new 16-case definition-branch regression passes in both
  tiers and in the GTest harness after installation. The existence guard and
  its unused tracking flag are removed; property examples initially reached
  the independent token-list conditional guard addressed below (S1.8, D10.2.2).
  The four larger strengthened existence fixtures match in direct JIT runs
  with forced GC. A concurrent GTest run hit the 60-second fixture limit for
  all four. Their native tree comparisons were moved to smaller dedicated
  fixtures (the original four cases already have two-mode coverage above),
  preserving all cases. A fresh GTest run is qualifying the split fixtures
  without changing the timeout; its first two expansion-only fixtures still
  time out under the concurrent sweep. A further split of all 130 original
  expansion cases was applied using `temp/latex-parity/prepare-existence-split.py`.
  All eleven resulting expansion and tree fixtures pass GTest with the original
  timeout, including under the concurrent sweep. The shared comparison helper
  retains the original expected fields and all original golden records.

  Property pop/remove now reuses the native property splitter and generated
  local/global conditional shapes. Twenty-four initial text, diagnostic and
  message comparisons match native, and eight document-tree comparisons match
  or reach independent guards. The candidate's permanent pop and removal
  fixtures pass both tiers under forced GC, including after installation, and
  both pass GTest. Malformed or linked-property paths and other
  generated variants remain explicit unsupported boundaries (S1.8, D10.2.2).

  An isolated follow-up removed that conditional guard. Twenty initial native
  comparisons, including the earlier definition-offset cases, have no silent
  mismatch; property examples in legacy mode reach the separate token-list
  assignment guard, and both examples match native trees in constructor mode.
  An additional 36 alias, nested-command and math comparisons and 30 token
  count, stringification and sequence-emptiness comparisons have no silent
  mismatch. Eighteen existing tree checks pass against the candidate; a new
  16-case native tree regression passes both tiers with forced GC, including
  after installation. The guard and unused tracking flag are now removed, and
  the expanded property-tree regression checks native equality in constructor
  mode and the separate assignment guard in legacy mode; it also passes both
  tiers under forced GC after installation. Both regressions pass GTest
  (S1.8, D10.2.2).

- **Expandable message investigation, 2026-11-25:** An isolated candidate
  under `temp/latex-parity/msg-candidate/` implements upstream-shaped message
  expansion, its argument helpers and the delimiter mismatch used for native
  diagnostics. Five comparisons with a user-defined message match native,
  including execution, `\\edef` and public macro meanings. The candidate does
  not yet initialize the built-in message catalog or module registrations and
  is not installed. Sequence-message comparisons using explicitly initialized
  data match all three constructor-mode probes, including the nested sequence
  case. Five registration, replacement and meaning comparisons and seven
  additional alias, unknown-message, scope and Unicode probes also match;
  duplicate declarations explicitly reach the unported `msg_error:nnnn` path.
  An experimental catalog extracted from the bundled expl3 source matches
  native presence for all 201 statically named declarations; two are inactive
  under native's engine conditions. Three catalog meanings differ because their
  declarations occur within `\\use:e`; applying the engine's general TeX
  expansion during catalog initialization makes the full batch of 201
  macro-meaning comparisons match. Module-name and module-type property
  meanings and the unseeded sequence-message probes also match native. The
  two engine-dependent declarations are temporarily excluded in this isolated
  prototype; their native conditions and preservation of preloaded definitions
  need to be implemented and tested before installation. The full message
  layer and malformed cases remain open (S1.8, D10.2.2).

  A generator prototype now extracts 202 static declarations, obtains the 200
  active names from the native format, and derives both module-registration
  lists from bundled source. Compiling ordinary message bodies with the Script
  engine and installing them as immutable values avoids reparsing hundreds of
  bodies on each expansion; the three declarations under `\\use:e` still use
  actual runtime expansion. The compiled prototype matches native module
  property meanings and preserves a custom prefix and handler under forced GC.
  All 404 text/help meaning comparisons match native across eight shards; the
  builtin sequence-message probes pass under forced GC in JIT. The generator
  derives the two engine-dependent decisions from a private native-format
  query; a pooled representation shares 142 immutable token values. Its helper
  meanings, sequence probes and two custom-prefix preload cases pass under JIT
  forced GC; all 404 pooled text/help comparisons match native across eight
  shards. Both-tier message and neighboring tests passed against the candidate.

  Source review then found a dynamically named message declaration inside a
  macro template: native has `str/unescape-name` and `str/unescape-url`, but
  the static candidate reported both absent. The generator now replays the
  enclosing source template with the Script engine, compares its generated
  text and help meanings against native, and includes both entries. It uses
  the shared Script reader for source scanning and audits unhandled message
  declarations. The resulting 202-message catalog, expandable-message macros
  and sequence-pop family are installed. The message regression, both
  sequence-pop regressions and property recovery pass in interpreter and JIT
  under forced collection and freed-memory poisoning; generator `--check`
  matches bundled source and native format. Non-expandable `\\msg_error`
  recovery and the full message layer remain open (S1.8, D10.2.2).

  Native non-expandable error probes confirm that `\\msg_error` performs
  module lookup, redirection, line wrapping and `\\errmessage` recovery;
  duplicate property declarations use that same path. Script currently stops
  explicitly at the unported message or redirect command. Porting the shared
  error and I/O machinery remains necessary; emitting a special-case diagnostic
  would not cover the native behavior (S1.8, D10.2.2).

- **Large-angle trigonometric quadrant, 2026-11-25:** Tangent qualification
  exposed an installed sine/cosine defect above the finite `int` range: the
  quadrant index saturated under **S4.1.1** and **S4.1.6**, so modulo and branch
  selection were wrong. The evaluator now reduces the index modulo four as a
  decimal before converting it. Fourteen large positive/negative native
  comparisons and the existing trig golden pass in both tiers under forced GC
  and poisoning. Tangent now propagates the sine/cosine uncertainty through
  division; 38 initial and 80 pole/random native comparisons matched. The old
  fixed large-angle cutoff is replaced by a reduction-boundary and rounding
  uncertainty check. Another 78 wide-angle comparisons match native and six
  ambiguous cases stay guarded. The updated trig and tangent regressions pass
  in both tiers under forced GC and poisoning. The constructor sweep snapshot
  predates these FP changes and must be requalified against the corrected
  module (S1.8, D10.2.2).

- **Constructor final-snapshot sweep, 2026-11-25:** A second candidate under
  `temp/latex-parity/constructor-final/lmd` uses constructor mode by default
  with all current engine fixes. Its JIT sweep starts from the current test
  set, records hashes for the binary, copied modules, test sources, goldens
  and test resources, and checks source/golden stability per fixture. The
  final audit reports changed or added inputs and module/binary drift. Run
  `temp/latex-parity/constructor-final-sweep.py`; progress and outputs are in
  `temp/latex-parity/constructor-final-sweep/`. A separate tail shard runs with
  `TEX_SWEEP_SHARD=tail` and keeps its own outputs and progress. The earlier
  sweep remains useful for classifying known improvements but predates fixes
  (S1.8, D10.2.2).

  The final-snapshot sweep is not valid as a frozen-binary qualification: its
  tail audit reports that `lambda.exe` changed during the run, and a later
  attempted shard independently detected another binary hash mismatch. The
  running main shard was interrupted. Keep its comparisons as investigation
  evidence only; rerun against a stable binary and the final installed modules
  before changing the default. Property and FP changes also postdate this
  frozen expansion snapshot (S1.8, D10.2.2).

  A replacement snapshot in `temp/latex-parity/constructor-stable/` copies the
  current executable and package, enables the candidate default, and hashes
  the copied binary, modules and fixture inputs. Its smoke test matches the
  property-tree golden. A private executable isolates it from workspace
  rebuilds, while the manifest detects test-resource changes. The combined
  main and tail shards cover all 285 snapshot fixtures: 277 match their
  goldens and eight are the previously native-verified tree improvements.
  The combined audit records no binary, snapshot-module or original-input
  drift. Property recovery and message regressions were added after the
  manifest; the former passes both tiers under forced GC, and the latter
  passes against the isolated message candidate. The live property preload
  fix also postdates the snapshot. The final package still requires its own
  qualification before changing the default (S1.8, D10.2.2).

  A new `temp/latex-parity/constructor-current/` snapshot includes the current
  property, message, sequence, `\\ignorespaces` and digest changes, copies the
  frozen executable, and enables the candidate default in its private profile.
  Its hash-audited JIT fixture sweep was stopped at 278 of 293 fixtures when the
  goal was cleared; no final audit was produced. The live default remains
  unchanged pending classification and final input/module drift checks. The
  existence regression sources and shared test helper were changed after its
  manifest, and the existence/conditional guard removals, sequence mapping and
  property operations postdate the module snapshot; the final audit must account
  for this drift and the added regressions.
  The sweep's `expl3_bool` and `expl3_bool_inverse` changes are successful
  native-equal constructor trees exposed by the narrowed assignment guard;
  their live legacy-mode goldens still match. The general expl3 fixture used
  left-pop as an unsupported-operation sentinel; it now uses the still
  unsupported `\\halign`, and the updated live fixture matches its golden.
  The four `expl3_put_cn` guard assertions now check native equality when a
  tree is returned and retain the legacy assignment guard; the full fixture
  matches its golden with both legacy and constructor defaults. Eight analogous
  `expl3_tl_set_eq` and `_c` trees also match native in constructor mode; those
  tests now share the same native-tree-or-legacy-guard check. The three fixtures
  were split into nine smaller expansion/tree tests, preserving 97 original
  comparisons and their goldens; all nine pass GTest under the original timeout.
  The sweep also exposed extra `unsupported` fields in the legacy
  `missing_relation` and `number_overflow` outputs after a test-helper change.
  The original helper result shape has been restored, with a separate detail
  helper for the new boundary assertions; `number_overflow` and
  `missing_relation` now match their unchanged live JIT goldens.

  `constructor-final-v2/lmd` is a copy of the earlier final snapshot with only the current
  FP module changed; `module-diff.json` verifies the sole module delta. The
  `constructor-fp-recheck.py` replayed all 16 fixtures whose source references
  FP or the bundled expl3 fixture; all match. The shared FP helper, binary and
  candidate FP module also retain their recorded hashes. Selection is
  source-based, so the broader sweep and final drift audit remain necessary.

- **Discard-boundary `\\unskip`, 2026-11-25:** Native input fixed its stale
  output boundaries under LI-019 on 2026-11-14. Script now adjusts each open
  box and discard boundary when removing an outer space, matching the current
  native behavior instead of reporting unsupported. Thirteen expansion cases
  in both constructor modes match native text, messages and diagnostics;
  22 of 26 corresponding trees match and four remain explicitly guarded. The
  original LI-019 nested-discard and global-definition cases and an included
  source pass the regression in both tiers under forced GC and poisoning.
  Insert passes in both tiers; box-register passes in JIT and a direct
  interpreter run matches its golden after the harness timed out. The frozen
  constructor sweep predates this expansion change (S1.8, D10.2.2).

- **Token-list head trees, 2026-11-25:** A 50-case native comparison across
  constructor modes produced 47 tree matches and three cases stopped by
  independent command/environment guards. Twenty-four `N`, `V` and `v`
  probes all reached existing argument-mapping guards. The redundant global
  `\\tl_head:n` digest guard was removed; a 24-case text/diagnostic/tree
  regression passes in both tiers under forced GC and poisoning. All four
  existing head-variant goldens match in direct forced-GC runs in both tiers.
  The frozen constructor sweep predates this guard removal (S1.8, D10.2.2).

- **Unicode math atoms, 2026-11-25:** Native groups contiguous non-ASCII
  bytes into `unicode_text`, while Script previously rejected them as math
  atoms. The math scanner now shares source-adjacency handling with raw math
  text and constructs native-shaped Unicode atoms. The included Unicode
  `\\vcenter` probe matches native text, diagnostics and tree in both
  constructor modes. Twenty-six direct and ten macro-generated comparisons,
  an included-source regression and the `\\vcenter` neighbor pass in both
  tiers under forced GC and poisoning. The raw-math harness timed out in both
  tiers under concurrent load; direct forced-GC runs matched the full golden.
  Unbraced Unicode script arguments remain guarded, with the guard tested in
  both tiers, because native consumes them bytewise and produces invalid
  UTF-8; recorded as [LI-118](../Lib_Input_Issue_Ledger.md#li-118) (S1.8,
  D10.2.2).

- **Math `\\vcenter`, 2026-11-25:** Native math parsing represents the
  expanded wrapper as a `box_transform` with a script-argument body. The
  Script math digester now constructs that node and lets other source-mapping
  checks handle non-math and nested-box cases. Four existing math examples
  match native trees; all other examples in the 26-case corpus stop at more
  specific mapping guards. A 20-case text/diagnostic/message/tree regression
  across both constructor modes and the existing `\\vcenter` golden pass in
  interpreter and JIT under forced GC and poisoning. An included ASCII source
  matches text, diagnostics and tree in both modes in the same regression;
  included Unicode previously reached a separate math-atom guard, which is
  being investigated.
  The frozen constructor sweep predates this digest change (S1.8, D10.2.2).

- **Font-token source mapping, 2026-11-25:** The constructor-aware path
  retains font commands within formatting arguments, resolving the older
  mismatch where legacy argument expansion consumed them. The font-token
  digest guard is now limited to legacy mode; the constructor-aware native
  comparisons cover plain and math text, nested formatting, defined fonts,
  aliases and `\\edef`. Nine text/diagnostic/message/tree regressions and the
  existing font-token golden pass in interpreter and JIT under forced GC and
  poisoning. Other font-family and generated-text
  guards remain in force. The frozen constructor sweep predates this guard change (S1.8,
  D10.2.2).

- **Floating-point extrema and directed rounding, 2026-11-25:** `min` and
  `max` now accept multiple operands and retain the first equal operand,
  including its zero sign; `sign`, `trunc`, `floor` and `ceil` follow native
  finite-number behavior. Nineteen extrema cases, two trees and 24 directed
  rounding cases pass in interpreter and JIT under forced GC and poisoning;
  another 30 deterministic and 12 extreme-scale directed-rounding cases match
  native text, messages and diagnostics. Four extreme-scale cases are in the
  permanent regression. The existing FP,
  round and trig goldens pass in both tiers under the same GC settings.
  Fractional or out-of-range precision
  remains guarded (S1.8, D10.2.2).

- **Definition-target recovery, 2026-11-25:** The `\\newcommand` family and
  document-command definitions now share native-style control-sequence target
  scanning, including missing targets, nested braces and unbraced input.
  Document signatures and bodies also use shared malformed-argument recovery.
  Fifteen native text/diagnostic cases and an included-file origin case pass
  in interpreter and JIT with forced GC and poisoning. The frozen constructor
  sweep predates this scanner change and the new regression, so its results
  require requalification against the final package (S1.8, D10.2.2).

- **Constructor sweep recovery, 2026-11-25:** The first sweep stopped after
  67 recorded fixtures when debug workers filled the volume with a 13 GB
  `log.txt`; its final 2,000 lines are saved in the sweep directory as
  `disk-full-tail.log`. The generated log was cleared and the disk recovered.
  The sweep resumed with the CLI's existing `--no-log` option, frozen module
  and binary checks, and per-fixture source/golden hash checks. It completed
  269 fixtures: 257 match, ten changes are native-verified improvements, the
  grouped-source-gap fixture expected an obsolete guard for four comment cases
  that now match native trees in both modes. Its assertion now requires native
  trees; direct forced-GC runs match the golden in both tiers. The improvements
  include paragraph assignments, a parenthesized command, a foreign-package
  tree and trailing source. Their successful trees were checked against native.
  `\\unexpanded` similarly matches native in constructor mode while legacy
  mode retains its guard. Its strengthened test postdates the final sweep's
  manifest; current interpreter/JIT tests and a forced-GC JIT run against
  the final candidate match their golden. The FP fixture changed during the
  earlier sweep and must be rerun against the final snapshot
  (S1.8, D10.2.2).

- **Floating-point rounding ties, 2026-11-25:** `round` now accepts the
  third argument and follows native tie direction: positive chooses toward
  positive infinity, negative toward negative infinity and zero chooses even.
  Seventeen text/diagnostic comparisons and two document trees match native
  in both tiers under forced GC and poisoning; fractional precision still
  stops at the explicit `fp round` guard because native's message expansion
  recovery is not ported. The existing FP golden passed in interpreter, and
  the sine/cosine neighbor passes in both tiers (S1.8, D10.2.2).

- **Floating-point sine and cosine, 2026-11-25:** Decimal quadrant reduction
  now splits pi/2 to retain precision near roots and checks an uncertainty
  interval before accepting the 16-digit result. Signed zero is preserved;
  ambiguous rounding and quadrant indices outside signed 64-bit range remain
  explicitly unsupported. The isolated candidate matched 158 ordinary and 80
  near-root native results, plus large-angle probes through `10^18`. The
  installed 14-case expansion and two-tree regression and the existing FP
  golden pass in both tiers with forced GC and poisoning. The
  269-fixture constructor sweep predates this FP change; its FP source and
  golden also changed during the run, so that fixture must be requalified
  against the final package (S1.8, D10.2.2).

- **Unicode text-case investigation, 2026-11-25:** An isolated candidate
  using Lambda's Unicode case mappings differs from native: `Straße` yields
  `STRASSE` versus native `STRAße`; `café` exhausts native's 20-million
  expansion budget with diagnostics; Greek lowercase emits native macro
  mismatch diagnostics. The existing Unicode guard remains in place and the
  combined long-running probe was stopped. Evidence is under
  `temp/latex-parity/case-candidate/` (S1.8, D10.2.2).

- **Trigonometry investigation, 2026-11-25:** An isolated decimal Taylor
  candidate for `sin` and `cos` matched 157 of 158 initial native values; the
  sole signed-zero difference was corrected in a follow-up probe. A targeted
  set of 40 values near multiples of pi exposed 22 native last-digit
  differences, so the candidate was not installed. Native's own range
  reduction and rounding need to be reproduced; evidence is under
  `temp/latex-parity/fp-candidate/` (S1.8, D10.2.2).

- **Constructor default qualification, 2026-11-25:** A fresh isolated
  package snapshot enables constructor argument mode by default while
  preserving explicit false. All 24 character-definition cases match native
  text and trees in that snapshot, versus 24 default-mode text mismatches in
  the installed profile. A 269-fixture JIT golden sweep is running against
  the frozen candidate (`temp/latex-parity/constructor-sweep/`, binary and
  source/module hashes in `manifest.json`). Changed goldens require independent
  native comparison; the installed default remains unchanged pending that
  qualification (S1.8, D10.2.2).

- **Document signature diagnostics, 2026-11-25:** Argument processors,
  unknown types, excess argument counts and incomplete delimiter fields now
  follow native definition recovery and diagnostic ordering. Unknown Unicode
  types retain the exact single byte produced by native's C formatting, even
  when it is not UTF-8. Twenty-three text/diagnostic cases match in both tiers
  with forced GC and poisoning; modifier, embellishment and environment-tree
  neighbors pass in both tiers. Native does not apply argument processors or
  embellishments, so the matching diagnostic does not provide that feature
  (S1.8, D10.2.2).

- **Document signature modifiers and embellishments, 2026-11-25:** The `+`
  and `!` modifiers now follow native signature scanning. The `e` and `E`
  forms reproduce native's unsupported-embellishment diagnostic and empty
  argument recovery. Ten cases for each path match native text and diagnostics
  in both tiers with forced GC and poisoning. Argument processors and malformed
  signature recovery remain open (S1.8, D10.2.2).

- **Document environment bodies, 2026-11-25:** The `b` signature form
  collects tokens through the matching environment end, preserves nested
  same-name environments and leaves the closing command for normal handling.
  Ten text/diagnostic cases, a child-to-parent file boundary and four document
  trees match native in both tiers under forced GC and poisoning. Other
  xparse forms and malformed-input recovery remain open (S1.8, D10.2.2).

- **Document environments, 2026-11-25:** Document environment definitions
  share the ordered xparse signature scanner and construct the native-shaped
  begin/end macros. Duplicate definitions and renewals of undefined commands
  or environments now recover with native diagnostics. Fourteen text and
  diagnostic cases and four document trees match native in both tiers under
  forced GC and poisoning. The `b` body argument and other unsupported
  xparse forms remain open (S1.8, D10.2.2).

- **Verbatim document arguments, 2026-11-25:** The `v` signature form uses
  the native sequence of argument scanning, token printing and conversion to
  category-other text tokens. Twelve expansion cases and four document
  trees match native in both tiers with forced GC and poisoning. The scanner
  now shares one implementation of the no-value default across optional
  forms. Environment-body and embellishment arguments and malformed-input
  recovery remain open (S1.8, D10.2.2).

- **Delimited document arguments, 2026-11-25:** The `d`, `D`, `r` and `R`
  signature forms now scan nested delimiters while preserving braces, support
  equal opening and closing tokens, and use native absent/default behavior.
  Missing required delimiters retain their input locations and emit native
  diagnostics. The shared default scanner also accepts a single-token default.
  Eighteen text/diagnostic cases, an included-file diagnostic and four document
  trees match native in both tiers with forced GC and poisoning; optional
  neighbors pass in both tiers. Other xparse variants and malformed signature
  recovery remain open (S1.8, D10.2.2).

- **Optional document arguments, 2026-11-25:** The ordered signature scanner
  now supports `o`, `g` and `G` alongside the existing defaulted bracket
  argument. `\\IfNoValue...` and `\\IfValue...` use the same unexpanded
  argument and branch scanner as boolean tests. Thirteen expansion cases and
  four document-tree cases match native text, diagnostics and trees where
  applicable; both regressions pass in JIT and interpreter with forced GC and
  poisoning. The 18 boolean cases also pass both tiers after the shared
  conditional change. Other xparse argument forms and malformed-input recovery
  remain open (S1.8, D10.2.2).

- **Boolean document arguments, 2026-11-25:** Document command signatures now
  store ordered `m`, `O`, `s` and `t` argument specifications separately
  from TeX parameter text, and `\\IfBooleanTF`, `\\IfBooleanT` and
  `\\IfBooleanF` select branches from unexpanded boolean tokens. Eighteen
  expansion, alias, macro-meaning and protected-definition cases match native
  text and diagnostics, and four document cases also match native trees; both
  regressions pass in JIT and interpreter with forced GC and poisoning. The
  existing expl3 golden passes in JIT after updating its former star-argument
  guard; its interpreter GTest hit the 60-second limit, while a direct
  interpreter replay matched the golden. Other xparse argument types and
  malformed-input recovery remain open (S1.8, D10.2.2).

- **Token-head source-mapping investigation, 2026-11-25:** With the existing
  global `\\tl_head:n` tree guard temporarily removed, eight prior guarded
  fixtures matched native. A further 12 control-sequence probes either
  matched native or stopped at independent command/environment/math guards.
  This does not prove source offsets for all expansion paths; the global
  guard was restored unchanged. Probe outputs are in
  `temp/latex-parity/tl-head-{candidate,controls-v2}.out` (S1.8, D10.2.2).

- **Character definitions in constructor trees, 2026-11-25:** Character
  emissions retain their source control token, and inline digestion reuses it
  when its main-file origin is verified. Generic argument descriptors restore
  verified control spellings and their source gaps; inline groups replay
  adjacent controls together and preserve trailing whitespace. Twenty cases
  match native trees and diagnostics in both tiers with forced GC and
  poisoning; three nonidentical paragraph spans and one generated origin remain
  guarded. The default non-constructor mode still reconstructs different TeX
  text in all 24 regression cases;
  constructor mode matches the investigated direct text case, so this is not
  full expansion parity. The focused test and character golden pass both
  tiers under forced GC and poisoning. The large integer-predicate fixture was
  run directly in interpreter: its sole golden change is the newly matching
  character-definition tree; the 60-second GTest harness was not used for
  that long fixture. Further constructor-mode and source-mapping work remains
  open (S1.8, D10.2.2).

- **Included punctuation definitions, 2026-11-25:** The earlier native
  control-word punctuation fix also resolves the included `\\catcode`
  definition fixture. Its golden now checks the complete native tree and
  passes in both tiers with forced GC and freed-memory poisoning. An included
  UTF-8 control name still fails the independent native-token comparison and
  remains guarded (S1.8, D10.2.2).

- **Environment-argument recovery, 2026-11-25:** `\\begin` and `\\end` now
  use the shared macro-argument recovery and continue with the partial name,
  as native does. Fifteen missing, unclosed and extra-brace cases, including
  an unfinished conditional, match native text and diagnostics in both tiers
  with forced GC and freed-memory poisoning. The expansion golden was updated
  for the recovered partial output; included environment diagnostics and
  environment-option neighbors pass in both tiers. This covers TeX expansion;
  native parser error-interface parity remains open (S1.8, D10.2.2).

- **Incomplete conditionals and runaway offsets, 2026-11-25:** Conditional
  stack entries now retain their origin and EOF recovery emits native-shaped
  diagnostics for active conditions and skipped branches. Runaway macro
  arguments retain the last fresh-token offset instead of unconditionally using
  the file end. Thirty-seven conditional cases, six runaway-offset cases and an
  included-file origin comparison match native text and diagnostics in both
  tiers under forced GC and freed-memory poisoning. Five neighboring JIT
  conditional goldens pass; four macro-argument goldens pass in both tiers and
  the expansion golden was updated for recovery. Recovery also distinguishes
  tests opened during operand expansion from conditionals skipped as source.
  Malformed LaTeX `\\end` recovery, wider conditional variants and native
  message-expansion behavior remain open (S1.8, D10.2.2).

- **Sequence-pop recovery investigation, 2026-11-25:** Reproduced the
  unsupported `\\seq_pop_left:NN`. An upstream-shaped macro candidate matches
  native populated, empty and global-pop examples, but an undeclared sequence
  exposes a broader incomplete-conditional defect: native reports both a
  runaway-argument diagnostic and an incomplete conditional at offset 40,
  while Script previously omitted the latter record and located the first at
  offset 50. The later conditional and runaway fixes above resolve that probe.
  An additional protected-expansion probe still differs because Script lacks
  native `\\msg_expandable_error:nn` behavior. The sequence-pop candidate was
  removed pending that separate parity work. Reproductions are in
  `temp/latex-parity/seq-pop.ls` and `temp/latex-parity/incomplete-if.ls`
  (S1.8, D10.2.2). A fresh upstream-shaped candidate with the property work
  matches ordinary, empty, scoped and malformed-input cases, but in an `\\edef`
  it leaves the unimplemented expandable-message tokens in the result instead
  of producing native's macro-mismatch diagnostic. It remains isolated under
  `temp/latex-parity/prop-upstream-candidate/`; installing it before fixing
  generic expandable-message recovery would introduce incorrect output. With
  the isolated message candidate, the upstream-shaped sequence-pop path now
  matches native text and diagnostics in all 12 original cases, including
  the protected-expansion case. The local/global left-pop operations, named
  variants, conditionals and `\\seq_pop` aliases are now installed as
  upstream-shaped macros. Two 16-case regressions compare text, diagnostics
  and messages in both constructor modes; both pass interpreter and JIT with
  forced collection and freed-memory poisoning. Local/global right-pop and
  their named and conditional variants now use the same upstream-shaped macro
  generator. Twenty initial and alias, assignment-order, malformed-input
  and macro-meaning probes matched native; the permanent 20-case regression
  and both left-pop regressions pass interpreter and JIT under forced GC and
  poisoning. The earlier message and left-pop tests also pass the actual
  GTest harness using a private frozen executable (S1.8, D10.2.2).

- **Definition paragraph and comment gaps, 2026-11-25:** Echoed definition
  spans now return the later paragraph tokens to the enclosing block parser;
  comment-joined argument groups are retained through definition continuation.
  Sixteen definition and line-ending cases and an included-file comparison match
  native text, diagnostics and ASTs in both tiers under forced GC and
  freed-memory poisoning. Five neighboring JIT goldens pass; the gap and
  continuation goldens also pass in the interpreter under forced GC. Inline
  continuation now leaves separated arguments to the enclosing parser and
  retains consumed command whitespace; 21 nested formatting and generic-command
  cases match native text, diagnostics and ASTs, and the former nested-definition
  guard is resolved. The nested regression and golden pass in both tiers under
  forced GC.
  A concurrent workspace build temporarily removed `lambda.exe`, so one replay
  could not start; the final regression was rerun successfully in both tiers
  after the executable reappeared (S1.8, D10.2.2).

- **Post-document comments, 2026-11-25:** Plain trailing source now uses the
  shared paragraph/comment scanner, including empty comment paragraphs, CRLF,
  blank lines and syntax characters inside comments. Sixteen text, diagnostic
  and AST comparisons match native in both tiers with forced GC and
  freed-memory poisoning. The tail-origin golden passes with one guard removed;
  live trailing markup and the two other guarded document-end cases remain
  open (S1.8, D10.2.2).

- **Environment-option token boundary, 2026-11-25:** A verified emitted
  control word is split when its spelling contains the closing option bracket;
  the resulting token stream is retained for the list body. The formerly
  guarded case now matches the native tree. Eighteen comparisons across three
  list environments, three suffixes and spacing variants match native text,
  diagnostics and trees in both tiers under forced GC and freed-memory
  poisoning; the existing environment-option golden also passes in both tiers.
  Boundaries in unverified emissions or skipped source gaps remain guarded
  (S1.8, D10.2.2).

- **Definition-option comments, 2026-11-25:** Removed the obsolete percent
  guard after verifying that inline paragraph reconstruction retains raw
  option bytes and matches native comment and escaped-percent trees. The
  existing 28-case option golden now includes three formerly guarded matches;
  nine additional nested, CRLF, blank-line and commented-bracket cases match
  native text, diagnostics and ASTs. Both tests pass in both tiers with forced
  GC and freed-memory poisoning (S1.8, D10.2.2).

- **Control-word punctuation, 2026-11-25:** Extended native lexical splitting
  to ordinary ASCII punctuation that TeX can assign letter category. Forty
  native comparisons cover 21 characters in definition and formatting
  contexts; text, diagnostics and ASTs match in both tiers under forced GC and
  freed-memory poisoning. An unmatched opening parenthesis is treated as text
  only after the complete reconstructed output proves it has no matching close;
  both neighboring parenthesis goldens pass in both tiers. Other uncertain
  parenthesized source mappings remain guarded (S1.8, D10.2.2).

- **Unclosed-group error shape, 2026-11-25:** Group-scan failures now include
  the cursor required when inline parsing propagates them. Five malformed
  expanded and direct group cases previously failed with E201; they now return
  `latex-digest-unclosed-group`, with text and TeX diagnostics matching native.
  The regression passes in both tiers with forced GC and freed-memory poisoning;
  the neighboring expanded and general-text goldens pass in JIT. Mapping the
  digester error to the public native parser's error interface remains open
  (S1.8, D10.2.2).

- **Formatting-command control-word gaps, 2026-11-25:** The digester now
  splits bracket-bearing TeX control words inside constructor arguments and
  preserves following whitespace, comments and paragraph boundaries in the
  reconstructed output. A 24-case regression matches native text, diagnostics
  and trees in both tiers, including forced GC and freed-memory poisoning;
  neighboring definition and environment goldens pass. The larger digest
  GTest timed out at its unchanged 60-second limit twice, including an isolated
  run; a direct JIT execution completed and exactly matched its golden. Further
  punctuation and included-source coverage remains open (S1.8, D10.2.2).

- **Raw general-text recovery, 2026-11-25:** Installed a shared scanner for
  `\\expanded`, `\\unexpanded`, `\\detokenize` and `\\scantokens`. It inserts a
  missing opening brace, retains partial text at EOF, and records native
  diagnostics. Twelve direct cases match text and diagnostics; eight ASTs
  match and four incomplete documents remain guarded as unclosed environments.
  Three child-to-parent group boundaries match text, diagnostics and ASTs. The
  regression passes in both tiers with forced GC and freed-memory poisoning;
  seven neighboring goldens pass in both tiers. A 28-case opening-brace matrix
  across all four primitives, active and `\\noexpand` aliases matches native
  text, diagnostics and ASTs under forced GC in both tiers. Wider raw-text
  matrices, release performance and error parity for incomplete document
  trees remain open (S1.8, D10.2.2).

- **`\\expanded` replay and recovery, 2026-11-25:** Installed a narrow
  `\\expanded` path that replays stored tokens without direct-source identity,
  recovers partial text at EOF, and follows native opening-brace scanning,
  including aliases and temporary `\\noexpand` relax meanings. The 18-case
  text/diagnostic regression and included-file location and AST comparison pass
  in both tiers with forced GC and freed-memory poisoning. Four incomplete
  document trees remain explicitly guarded. Seven expanded-argument,
  parameter, `\\unexpanded`, `\\detokenize` and `\\noexpand` goldens pass in
  both tiers. The broader staged raw-general-text work described below remains
  open and the previous scratch snapshots are absent (S1.8, D10.2.2).

- **Definition control-word source gaps, 2026-11-25:** The installed digester
  splits `]` from a TeX control word when native source scanning ends before
  it, and preserves the source gap consumed by the TeX reader. Restored gaps
  retain comment spelling through paragraph reconstruction, including CRLF and
  blank lines. A 24-case regression matches native text, diagnostics and ASTs
  in both tiers, normally and with forced GC and freed-memory poisoning. The
  environment-option, definition-gap, definition-join and generated-definition
  parity goldens also pass in both tiers. Other punctuation and inline-format
  source mappings still need qualification (S1.8, D10.2.2).

- **Macro-prefix recovery, 2026-11-25:** Installed recovery for a mismatched
  leading delimiter: the call is discarded, the unmatched token is returned,
  and the native diagnostic is recorded; end of input does not diagnose a
  mismatch. Eleven focused cases compare text and diagnostic records with native
  TeX in both tiers, including aliases, matched calls and true EOF. They pass normally and
  with forced GC and freed-memory poisoning; the expansion golden also passes
  in both tiers after updating its former fatal-mismatch expectation. An
  included-file case also matches native text, diagnostic location and AST in
  both tiers under forced GC. Broader source-mapping qualification remains open
  (S1.8, D10.2.2).

- **Empty captured resources, 2026-11-25:** Installed the resource-boundary
  fix: preserve zero-byte captured resources as empty text because conversion
  to binary produces null (**S2.2.2v2**), then decode through the shared UTF-8
  decoder. The previously failing profile, read and readline GTests pass with
  unchanged goldens in both JIT and interpreter. The expansion golden now also
  checks the MD5 of an existing empty file and passes in both tiers. The other
  staged changes described below remain uninstalled and require fresh
  reproductions: the prior scratch snapshots are absent from this checkout
  (S1.8, D10.2.2, D11.1.2).

- **Definition source-mapping regression, 2026-11-24:** The installed JIT
  corpus reports `definition command source mapping` instead of the existing
  `environment option token boundary` guard. The pre-session digest matches
  the unchanged golden; the installed and staged packages do not. The added
  native-name normalization rejects a `]` embedded in a TeX control word.
  A broad bracket split restored the golden but lost consumed whitespace and
  produced incorrect ASTs, so it was rejected. An isolated narrower snapshot
  restores the consumed source gap and preserves the golden
  (`native-brackets-v2-test_tex_script_environment_options.out`); it remains
  uninstalled. In a 60-case punctuation and whitespace matrix, text and
  diagnostics match native in each tier; 48 ASTs match and twelve comment-gap
  cases remain explicitly guarded (`definition-gap-{ast,text}-{jit,interp}.out`).
  Separate probes also found pre-existing incorrect ASTs for bracket-bearing
  control words inside formatting commands; they remain open. A 12-case focused
  regression and the 21-case macro-prefix regression pass on the combined
  snapshot in both tiers, normally and with forced GC and freed-memory
  poisoning (`{definition-gap,macro-prefix}-regression-{normal,gc}-{jit,interp}.out`).
  Seven neighboring JIT GTests now pass their unchanged goldens on the combined
  snapshot, as do the corresponding interpreter GTests
  (`combined-neighbor-gtest-{jit,interp}.log`). The generic expansion script's
  only JIT golden difference is the intended native-compatible macro-prefix
  recovery. Four combined raw-text regressions pass in both tiers
  (`combined-raw-regressions.log`). Three interleaved release pairs had mean
  baseline/candidate CPU seconds of 6.435/6.381 for general text and
  6.349/6.376 for the control; macro-prefix pairs comparing the earlier
  raw-text snapshot with the combined candidate averaged 6.376/6.392. All
  outputs matched native text, diagnostics and ASTs
  (`bench-combined-{general-text,general-control,macro-prefix}.log`). The
  combined candidate is still uninstalled (S1.8, D10.2.2).

- **Empty captured resources, 2026-11-24:** The in-progress JIT corpus's
  profile, read and readline scripts returned E201 at `decode_source`: an empty
  captured source reaches a binary-only parameter as null. `binary("")` is null under
  **S2.2.2v2**; captured resources currently convert their validated data to
  binary without preserving the empty case. An untested `resource-empty-lmd`
  snapshot retains empty text for zero bytes and lets the shared decoder
  accept that representation; it also retains the staged prefix changes. Eight
  JIT cases match native text and diagnostics across package, class, input,
  stream and PDF file consumers in each tier; five trees match and the three
  stream cases retain explicit digester guards
  (`empty-resource-matrix-{jit,interp}.out`). The three failing profile, read
  and readline GTests now pass with their unchanged goldens in both tiers on
  the snapshot (`empty-resource-focused-{jit,interp}.log`). An eight-case native
  comparison also passes in both tiers with and without forced GC and memory
  poisoning (`empty-resource-regression-{normal,gc}-{jit,interp}.out`). The
  change remains uninstalled; combined qualification remains pending
  (D10.2.2, D11.1.2).

- **Macro-prefix recovery, 2026-11-24:** A 21-case comparison against the
  native TeX engine reproduces fatal `tex-macro-mismatch` where native backs up
  the unmatched token, records a diagnostic and continues. End of input while
  matching a prefix instead returns without that diagnostic. An isolated
  `macro-prefix-lmd` snapshot based on the staged raw-text scanner matches all
  21 texts and diagnostic sets in each tier (`macro-prefix-candidate-{jit,interp}.out`).
  Eighteen ASTs match and three cases have explicit existing digester guards;
  an included-file case matches native text, tree and diagnostic location in
  both tiers (`macro-prefix-{ast,foreign}-{jit,interp}.out`). Its focused
  regression passes normally and under forced GC in both tiers on the combined
  snapshot. Neighboring goldens and release qualification remain pending;
  the change is not installed (S1.8, D10.2.2).

- **TeX Script JIT corpus, 2026-11-24:** The installed package's GTest run
  passed 220 of 237 tests (`tex-corpus-jit.log`). Thirteen exceeded the
  harness's unchanged 60-second limit; three failed at the empty-resource
  decoder and one reported an environment-option guard mismatch: expected
  `environment option token boundary`, got `definition command source mapping`.
  The 2,046 package, test and executable hashes and seven separate reference
  fixture hashes remained unchanged. Timed-out cases require isolated replays,
  without changing this aggregate result. All thirteen also timed out in the
  earlier two-worker sweep.
  The prefix probes overlapped the corpus, and lowering their scheduling
  priority was denied; this contention may have contributed to the tenth
  timeout. Further qualification probes are deferred until the corpus ends.
  A subsequent isolated release replay of `declaration_definition_gap`
  completed with the exact golden (`profile-timeout-sample.log`); macOS
  sampling could not inspect that process in the sandbox
  (`profile-timeout-sample.err`). The thirteen-case GTest and release replays
  remain pending.

- **Combined-source baseline, 2026-11-24:** `make test-lambda-baseline` now
  builds successfully with the paired Radiant revision, then reports 6,111 of
  6,128 tests passing (`lambda-baseline-paired.log`). The JavaScript suite
  reports 576/577: the
  `dom_media_playback` JS case stops after ten `true` lines before its media
  playback continuation; the media fixture exists. The serve suite also has
  three failures, all reporting that loopback listener binding was not
  permitted by the sandbox. The exact media GTest and all three serve GTests
  pass when rerun outside the sandbox (`media-gtest-unsandboxed.log`,
  `serve-gtest-unsandboxed.log`); the aggregate still records its four failures.
  Individual baseline results and logs are saved as
  `lambda-baseline-{js,serve}-{results.json,log}`. Radiant lint passes
  (D9.1.3v2). The 13 remaining network, discovery and filesystem-watch
  scripts also pass outside the sandbox (`lambda-baseline-13-unsandboxed.log`).
  The sandboxed aggregate remains failed. A full unrestricted rerun with its
  own results directory passed all 6,128 tests
  (`lambda-baseline-unsandboxed.log`). The Test262 baseline passed all 40,261
  baseline entries, with 2,652 skips and no regressions (`test262-baseline.log`).
  The unrestricted Radiant baseline completed with no failed layout cases,
  but its DevTools and render suites could not start because the render corpus
  lacks `pngjs` (`radiant-baseline.log`). The layout report contains 292
  partial and 12 skipped cases; one Radiant View speech smoke is skipped. The
  lockfile-pinned `pngjs` 7.0.0 was recovered from the local npm cache, verified
  against its SHA-512 integrity, and placed under `temp/` without changing the
  corpus. An unrestricted aggregate rerun passed all 25 DevTools protocol
  cases, but render startup then exposed the additional missing ESM dependency
  `pixelmatch` (`radiant-baseline-with-deps.log`). The lockfile-pinned version
  6.0.0 was also verified from the local cache; a temporary ESM resolver keeps
  both dependencies outside the corpus. The focused render baseline passed:
  207 passes, four expected failures, one skip and no regressions
  (`radiant-render-deps.log`). The complete aggregate is being rerun with both
  dependencies passed: 8,179 passes, 296 partial, 20 skipped and no failures
  (`radiant-baseline-complete.log`). The linked `lambda-test` corpus remains
  clean.

- **Production boundary inventory, 2026-11-24:** `fn_parse2` dispatches through
  `input_from_source_n_with_name_parent`, and the CLI conversion path calls
  `input_from_url`; Input's format table still routes `latex`, `latex-ts` and
  `tex` to native parsers. The document-transform registry already invokes
  Lambda package exports, but the LaTeX package's `parse_source` still calls
  native `parse`. `interp_call_module_export` provides an existing runtime-side
  call path, not a drop-in Input dependency. Any migration must preserve
  resource authorization and native-client ownership, root values over nested
  evaluation, and retain equivalent diagnostics and ASTs (S1.8, D9.1.3v2,
  D10.2.2, D11.1.2). Public dispatch remains native until those gates pass.

- **Expanded-definition recovery:** Before this change the scanner checked
  completed replacement lists, while native checks `#` during expansion
  and can diagnose it before later tokens expand. The prior exploratory
  candidate is not present in the current scratch tree. A fresh reproduction
  matrix is `temp/latex-port-resume/expanded-param-reprobe.ls`. Both tiers
  reproduce fatal `tex-illegal-param` on changed token streams where native
  continues, including the missing-number diagnostic order. A separate
  `\detokenize` case emits the correct expansion but drops the echoed
  definition while native retains it (`expanded-param-reprobe-{jit,interp}.out`).
  An exploratory `expanded-param-lmd` snapshot scans expanded-definition
  parameter markers while tokens are read, preserving the distinction between
  ordinary expansion lookahead and direct token-list insertion. The ongoing
  baseline used the installed source. The snapshot fixes the reproduced
  expanded-definition text and diagnostic-order cases in JIT; an ordinary
  macro-generated `\def` remains fatal and the separate detokenize echo loss
  remains (`expanded-param-candidate-jit.out`). The 31-case expanded-parameter
  matrix gives identical JIT and interpreter results: 27 text and 29 diagnostic
  matches. The four text mismatches comprise the separate detokenize echo gap,
  two ordinary macro-generated definitions and a runaway definition
  (`expanded-param-matrix-{jit,interp}.out`). All six neighboring
  JIT goldens pass (`expanded-param-neighbors.log`). A combined snapshot
  (`combined-param-lmd`) passes 28 text and 29 diagnostic checks out of 31 in
  both tiers; only the two ordinary-definition and runaway-definition text
  gaps remain. Included-file text, diagnostics and tree match in both tiers,
  and `test_tex_script_expanded_param_order` passes in both tiers, including
  forced GC and freed-memory poisoning (`expanded-param-gc-{jit,interp}.out`).
  The combined JIT and interpreter runs match native text and diagnostics
  across all 20 extended detokenize cases, with one explicitly guarded tree
  (`combined-detokenize-extended-{jit,interp}.out`). The echo loss appears to come from
  generated tokens marking the definition trace as foreign: unlike numeric
  `\the` tokens, they lack `trace_internal`. An independent
  `detokenize-trace-lmd` snapshot reuses the existing generated-text helper;
  its first 18 cases match native text, diagnostics and ASTs in both tiers
  (`detokenize-trace-candidate-{jit,interp}.out`). An included-source probe
  also matches native text, tree and diagnostic file/line in both tiers
  (`detokenize-foreign-{jit,interp}.out`). The combined regression matches in
  both tiers and failed against the pre-change parser; all six neighboring JIT
  goldens pass. A preliminary empty-output observation was a polling error:
  the output file was read before the process finished. The 20-case extended
  matrix matches text and diagnostics in 19 cases in both tiers; the remaining
  `\noexpand` parameter-recovery failure also occurred with the pre-change
  parser; the independent expanded-parameter candidate matches native text and
  diagnostics for three `\noexpand\detokenize` variants in JIT
  (`expanded-param-noexpand-jit.out`). One `\meaning` case retains the
  generated-text source-mapping guard.
  The combined regression also matches with `LAMBDA_GC_FORCE_EVERY=1000` and
  freed-memory poisoning in both tiers (`detokenize-gc-{jit,interp}.out`).
  Three interleaved release pairs on an expanded-definition detokenize workload
  had baseline CPU seconds 5.294/5.275/5.244 and candidate
  5.276/5.309/5.267, with identical native-checked output hashes
  (`bench-detokenize-inner-pairs.json`). Parameter-scanning baseline CPU seconds
  were 6.216/6.242/6.242 and combined-candidate seconds 6.259/6.229/6.238,
  with identical native-checked text, diagnostic and AST output hashes
  (`bench-parameter-pairs.json`). Both changes are installed. Six focused
  repository tests pass against the installed package in each tier
  (`installed-combined-focused-{jit,interp}.log`); the session-end aggregate
  remains pending (S1.8, D10.2.2). The pre-existing illegal-parameter recovery
  test now checks native text and diagnostics for the formerly guarded case;
  its updated golden matches both tiers
  (`illegal-parameter-installed-{jit,interp}.out`).

- **Ordinary macro-generated definitions:** A 26-case reproduction has 15
  text and diagnostic mismatches in the installed engine
  (`raw-param-baseline-jit.out`). An isolated `raw-param-lmd` snapshot scans
  ordinary definition bodies with the same parameter logic at token-read time;
  its matrix matches 24 cases in both tiers. The remaining cases expose a separate macro
  argument mismatch and runaway-definition recovery. A foreign-file probe
  matches native text, diagnostic locations and tree in both tiers
  (`raw-param-foreign-{jit,interp}.out`). A separate 16-case mixed-source
  probe revealed five incorrect diagnostic offsets for buffered arguments.
  A revised snapshot uses the reader position when tokens are buffered; both
  tiers now match 15 cases, with the existing macro mismatch remaining
  (`raw-param-cross-v2-{jit,interp}.out`). Its 23-case regression and foreign
  source probe pass in both tiers. The earlier 31-case expanded-definition
  matrix now matches 30 texts and all 31 diagnostic sets in both tiers; the
  only text mismatch is the pre-existing runaway-definition case
  (`raw-param-v2-expanded-{jit,interp}.out`). The 23-case regression passes
  with forced GC and freed-memory poisoning in both tiers
  (`raw-param-v2-gc-{jit,interp}.out`). The extended 25-case regression,
  including CRLF and Unicode, also passes with and without forced GC in both
  tiers (`raw-param-v2-regression-*`). Sixteen neighboring goldens and the
  `\scantokens` source test pass on the frozen source; the 32 source and
  golden files stayed unchanged (`raw-param-v2-neighbors.json`,
  `raw-param-v2-test-manifest.json`, `raw-param-v2-sha256.txt`). Three
  interleaved release pairs on ordinary definitions produced baseline CPU
  seconds 6.224/6.218/6.212 and candidate 6.230/6.257/6.239, with identical
  native-checked text, diagnostics and AST hashes
  (`bench-raw-parameter-pairs.json`). The recovery change and its 25-case
  regression are installed; four installed-package focused tests pass in both
  tiers (`installed-raw-param-focused-{jit,interp}.log`). The unrestricted
  Lambda aggregate passed 6,128/6,128 (`lambda-baseline-final.log`), but its
  selected scripts do not include these LaTeX fixtures; the focused results
  above remain a separate gate (S1.8, D10.2.2).

- **Incomplete definitions:** The installed scanner stops with an issue at
  end of input, while native retains the partial definition and, for an open
  body, records `tex-runaway`. On a 20-case JIT reproduction, all texts and
  17 diagnostic sets differ (`runaway-baseline-jit.out`). An isolated
  `runaway-lmd` snapshot retains the scanned body or signature and records
  the native EOF diagnostic. It matches native text and diagnostics for all
  20 body probes and all 21 signature probes in JIT
  (`runaway-v2-jit.out`, `signature-eof-v2-jit.out`). An expanded-text EOF
  audit found different partial-source behavior for the shared `\expanded`
  scanner (`expanded-eof-v2-jit.out`); the revised candidate limits recovery
  to definition bodies and leaves incomplete general expanded text guarded.
  JIT output for 20 incomplete expanded-text cases is byte-identical to the
  installed engine (`expanded-eof-v3-{baseline,candidate}-jit.out`). An AST
  audit exposed an existing missing `index` when an incomplete echoed
  definition's error propagates through the digester; the scratch change adds
  that index, returning a guarded unclosed-group or unsupported issue rather
  than E201 (`runaway-ast-v4-jit.out`). A definition opened in an included
  file and closed in its parent matches native text, diagnostics and AST; an
  unclosed case matches text and diagnostics with an unclosed-environment guard
  (`runaway-{closed-,}foreign-v3-jit.out`). The frozen two-file candidate
  matches JIT and interpreter outputs for the body, signature, AST and
  included-file matrices, and preserves the expanded-text cases unchanged
  (`runaway-v4-sha256.txt`, `*-v4-{jit,interp}.out`). Its 14-case regression
  with both foreign-file controls passes in both tiers and with forced GC and
  freed-memory poisoning (`runaway-regression-v4-{jit,interp}.out`,
  `runaway-v4-gc-{jit,interp}.out`). All 25 neighboring goldens pass, with 50
  source and golden hashes unchanged (`runaway-v4-{neighbors,extra}.json` and
  their manifests). Three interleaved release pairs had baseline CPU seconds
  5.826/6.132/6.122 versus 6.161/6.274/6.331 for the candidate. A reversed
  order repeat had baseline 6.089/6.133/6.170 versus 6.164/6.305/6.314;
  all samples retained the same native-checked output hash
  (`bench-runaway-control{,-repeat}-pairs.json`). Removing a repeated lookup
  did not show a clear improvement: the original candidate measured
  6.486/6.371/6.330 CPU seconds and the alternative 6.451/6.315/6.358
  (`bench-runaway-opt-pairs.json`). The fully qualified original candidate,
  including its digester error-shape repair and regression fixtures, is now
  installed. Three installed-package focused tests pass in both tiers
  (`installed-runaway-focused-{jit,interp}.log`). The complete 237-script TeX
  JIT GTest corpus is running in session 92518 with two workers and the
  unchanged 60-second per-script timeout (`tex-corpus-jit.log`). The package,
  fixture and executable snapshot contains 2,046 hashes
  (`tex-corpus-manifest.json`); a later fixture-path audit added the seven
  TeX reference files, all older than the corpus start
  (`tex-corpus-extra-manifest.json`). Verify both after the run. At 135/237, the
  unchanged timeout has been reached by `declaration_definition_gap`,
  `expl3_tl_tail`, `expl3_seq_in`, `raw_environment_digest` and
  `numeric_conditional`, `digest`, `engine_identity`, `expl3_seq_item` and
  `expl3_seq_left_macro`.
  The harness is replaying missing batch records;
  replay the timeouts in isolation after the aggregate. The separate native
  six-fixture TeX conformance test passes in both tiers
  (`native-tex-conformance-{jit,interp}.log`); the script-port reference test
  is part of the ongoing corpus. This filter covers
  `latex_test_tex_script_*`, not all `latex_*` scripts. The interpreter corpus
  and the broader `make test-lambda-full` gate required by `test/README.md`
  remain pending (S1.8, D10.2.2).

- **Expanded token source and EOF:** Native `back_list_copy` clears direct-source
  flags on `\expanded` results. The installed script path retained them: a
  22-case complete-input audit has nine text and eight AST mismatches
  (`expanded-valid-baseline-jit.out`). The isolated `expanded-eof-v6-lmd`
  snapshot clears those flags and limits partial-EOF recovery to `\expanded`;
  it matches all 22 texts and diagnostics, 21 ASTs and one source-mapping guard
  in both tiers (`expanded-valid-v6-jit.out`,
  `expanded-valid-matrix-v6-interp.out`). Its EOF matrix matches 16/20 texts
  and 15/20 diagnostic sets; the other cases concern missing opening braces
  or separate `\unexpanded`/`\detokenize` recovery
  (`expanded-eof-v6-jit.out`). An AST probe exposed a second digester error
  whose group-scan result lacked an inline token index. The isolated v7
  snapshot adds that index: its EOF AST audit has 13 native matches and seven
  explicit guards, with no E201 (`expanded-eof-ast-v7-jit.out`). A separate
  v9 snapshot also implements native missing-brace scanning, skips temporary
  relax meanings and handles active and `\noexpand` aliases. It matches all
  27 opening-brace probes in JIT, including native ASTs
  (`expanded-opening-regression-v9-jit.out`). Three staged source, EOF and
  opening regressions pass in both tiers, including included-file controls;
  all 10 neighboring JIT goldens pass with unchanged source/golden hashes
  (`expanded-v9-neighbors.json`, `expanded-v9-test-manifest.json`). All three
  regressions also pass forced GC and freed-memory poisoning in both tiers
  (`expanded-*-regression-v9-gc-{jit,interp}.out`). The 27 opening and alias
  cases also match in constructor mode in both tiers
  (`expanded-opening-constructor-v9-{jit,interp}.out`). The source snapshot is
  frozen in `expanded-eof-v9-sha256.txt`. It remains uninstalled; release
  performance qualification is pending (S1.8, D10.2.2).

- **Raw general-text recovery:** Native `\unexpanded`, `\detokenize` and
  `\scantokens` use the same brace and EOF scanner. A 25-case JIT probe has
  17 text and diagnostic mismatches on v9 (`raw-text-v9-jit.out`). An
  isolated v10 snapshot factors that common scan and matches all 25 texts and
  diagnostic sets (`raw-text-v10-jit.out`). Its JIT AST audit has 19 native
  matches and six explicit unclosed-structure guards, with no E201
  (`raw-text-ast-v10-jit.out`). A complete included-file example and three
  child-to-parent group boundaries match native text, diagnostics and AST
  (`raw-text-source-v10-jit.out`, `raw-text-boundary-v10-jit.out`). Its two
  changed source hashes are recorded in `raw-text-v10-sha256.txt`. Its 25-case
  regression and the three earlier expanded-token regressions pass in both
  tiers (`*-regression*-v10-{jit,interp}.out`). All four regressions also pass
  forced GC and freed-memory poisoning in both tiers
  (`*-regression*-v10-gc-{jit,interp}.out`); the final interpreter case was
  repeated with a captured exit status (`expanded-eof-regression-v10-gc-confirm-interp.out`).
  All 14 neighboring JIT goldens pass with unchanged test hashes
  (`raw-text-v10-neighbors.json`, `raw-text-v10-test-manifest.json`). The
  25-case raw-text and cross-file regression passes in constructor mode in
  both tiers (`raw-text-constructor-v10-{jit,interp}.out`). An additional 48
  complete-input combinations of the three primitives match native text and
  diagnostics in both tiers (`raw-text-fuzz-v10-{jit,interp}.out`); all 48
  ASTs also match in both tiers (`raw-text-fuzz-ast-v10-{jit,interp}.out`).
  The existing six-fixture script reference test matches its full golden,
  including all 151 reference messages, in both tiers
  (`tex-reference-v10-{jit,interp}.out`).
  It is not installed; uncontended release performance
  checks remain open (S1.8, D10.2.2).

- **Constructor sweep result, 2026-11-24:** The frozen 231-test JIT harness
  completed with 186 passes, 36 timeouts and nine output mismatches
  (`default-sweep/jit.log`). All 1,745 executable, package and fixture hashes
  in the manifest remain unchanged. The timeout list is in
  `default-sweep/timed-out.txt`. The single-worker GTest rerun passed 25 and
  still timed out on 11, with no other output mismatch
  (`default-sweep/isolated-jit.log`). All 72 source and golden files for the
  36 selected cases still match the original manifest despite later test
  additions. `default-persistent-release.py` compared the 11 persistent
  cases against the original default on the release host without altering
  the harness or its deadline.
  The first case, declaration-definition-gap, passes its golden on both
  release snapshots, with identical output hashes. Original-default runtime
  was 70.51 s (68.96 CPU s) and candidate runtime 69.08 s (68.67 CPU s), both
  beyond the harness limit (`default-sweep/persistent-release.log`).
  The second case, digest, also passes with identical output hashes in both
  release snapshots, taking 62.63 s and 63.39 s respectively.
  The third case, `expl3_cs_exist_c`, passes with identical output, but one
  release pair took 50.73 s on the original default and 67.42 s on the candidate
  (49.72 vs 64.02 CPU s). That difference did not reproduce: three interleaved
  baseline CPU samples were 47.848/47.234/47.230 s and candidate samples were
  47.230/47.097/47.356 s, with the same output hash (`bench-exist-c-pairs.json`).
  All 11 release comparisons pass their goldens with identical output hashes
  in both versions (`default-sweep/persistent-release-results.json`). GTest
  still records the 11 fixed-deadline timeouts. The original-default
  integer-helper run took 173.10 s wall and 102.02 s CPU, showing substantial
  off-CPU time.
  The explicitly mode-selected paragraph-gap fixture took 111.38 s vs 146.92 s
  wall (110.24 vs 137.52 CPU s) despite identical output; avoid causal
  performance conclusions from individual pairs.
  The declaration-definition-gap fixture explicitly runs both constructor
  modes for all 108 combinations, and length-digestion likewise passes an
  explicit mode for each expansion; the candidate's absent-option default
  cannot change those configured modes. Compare their harness outcomes with the
  installed snapshot and use the release host for timing. Classify other
  persistent timeouts individually.
  Compare each mismatch with the installed default and native before considering a default
  change. The user-visible default remains unchanged (S1.8, D10.2.2).
  Direct follow-ups for foreign generic commands, unexpanded unknown commands
  and generated parenthesized arguments match native text, diagnostics and
  trees in both tiers (`default-mismatch-detail-{jit,interp}.out`). An initial
  counters comparison used mismatched adapter configurations and is not a
  parity result; the earlier aligned adapter comparison remains the evidence
  for that fixture.
  The sweep output itself checks native trees for the expl3 predicate and
  two font cases; the paragraph case was checked separately with aligned
  options. The parenthesized fixture's newly supported case has equal trees.
  The environment-option case remains explicitly unsupported, but reports a
  different source-mapping guard. These observations do not qualify the 36
  timed-out cases or establish corpus-wide parity.

- **Raw Lua document trees, installed 2026-11-24:** Native reparses a direct `\\directlua`
  or `\\luaexec` group into a command element with a `source` attribute. The
  installed script expansion preserves its bytes but the digester rejects the
  tree. An exploratory `raw-lua-lmd` snapshot tags captured Lua groups and
  parses reconstructed spans, reusing the existing raw-group and environment
  safety scanners. The first 20-case native comparison exposed a missing
  reconstruction trigger; the revised 20-case JIT comparison has 13 native
  text, diagnostics and tree matches and seven explicit guards in both tiers,
  with no silent tree mismatch (`raw-lua-tree-v2-{jit,interp}.out`). A JIT
  included-file comparison also matches all three and the mapped offsets
  (`raw-lua-foreign-v2-jit.out`). A 40-case audit has 30 native matches,
  ten guards and no mismatches in both tiers (`raw-lua-audit-v2-{jit,interp}.out`);
  the included-file case also matches under poisoned GC in interpreter
  (`raw-lua-foreign-v2-gc-interp.out`). That initial version was not installed.
  The existing explicitly guarded no-group and ambiguous
  environment cases must remain guarded until proven safe (S1.8, D10.2.2).
  The neighboring raw-inline capture golden matches. Raw-inline digestion has
  one changed golden row: a direct Lua command followed by `\\lstinline`
  improves from a guard to native text, diagnostics and AST equality
  (`raw-lua-neighbor-test_tex_script_raw_inline_digest.out`). The other four
  neighboring goldens match (`raw-lua-neighbors.log`). A separate v3 prototype checks
  reconstructed lookahead and retains whitespace when a Lua command has no
  group. Its 24-case JIT matrix has 20 native matches, four explicit guards,
  and no silent mismatch in either tier (`raw-lua-no-group-v3-{jit,interp}.out`).
  V3 retains the 40-case JIT audit results and passes the included-file
  interpreter poisoned-GC check. A subsequent lexical-boundary audit found
  four silent AST whitespace mismatches in both tiers
  (`raw-lua-lexical-v3-{jit,interp}.out`): reconstructed separators and
  comment-following whitespace after the captured raw span were dropped. A
  v4 prototype reuses reconstructed gap parsing. Its 25-case JIT lexical
  audit now has 20 native matches and five guards, with no mismatches in both
  tiers (`raw-lua-lexical-v4-{jit,interp}.out`). Its 16-case paragraph-gap
  audit has 15 native matches and one guard in both tiers
  (`raw-lua-gaps-v4-{jit,interp}.out`); the earlier 40-case JIT audit remains
  30 matches and ten guards. The 24-case unbraced-command audit remains 20
  matches and four guards in both tiers; the included-file case matches with
  poisoned GC in both tiers. Of three v4 neighbor checks, verbatim and
  definition-joining goldens match; the mixed Lua/listings row changes to the
  previously verified native match (`raw-lua-v4-neighbors.log`). The Lua gap
  audit also reproduces the debug results on the fresh release host
  (`raw-lua-gaps-v4-release.out`). A 15-case focused regression passes in both
  tiers (`raw-lua-digest-regression-{jit,interp}.out`). Three interleaved
  baseline CPU samples were 6.146/6.167/6.138 s and candidate samples were
  6.147/6.160/6.187 s; all native-checked output hashes match
  (`bench-raw-isolated-pairs.json`). V4 is installed in `tex_engine/digest.ls` and its
  capture tag is merged into `tex_engine/expansion.ls` beside the integer fix.
  The paired `test_tex_script_raw_lua_digest.{ls,txt}` includes direct,
  guarded and included-file cases. Existing guards are updated only where the
  resulting tree was checked against native. The combined installed source
  passes all four focused GTests in both tiers (`installed-port-focused-{jit,interp}.log`);
  both new regressions also match their goldens under poisoned GC in both tiers
  (`installed-{expression_recovery,raw_lua_digest}-gc-{jit,interp}.out`). A
  mixed Lua/expl3 source probe matches native text, diagnostics and the
  integer source-mapping guard in both tiers (`combined-port-{jit,interp}.out`).
  Three interleaved
  release pairs on the frozen combined package retain native-checked output
  hashes: integer baseline CPU 9.930/10.081/10.073 s, combined
  10.394/10.177/10.162 s; raw baseline 6.186/6.177/6.182 s, combined
  6.188/6.201/6.202 s (`bench-{integer,raw}-combined-pairs.json`).

- **expl3 integer terminators, installed 2026-11-24:** Reproduced four explicit
  `\relax` differences on installed modules (`expl3-relax-probe.out`). A
  separate `expl3-v2-lmd` snapshot implements the bundled expl3 macro flow
  for absolute value, minimum, maximum and rounded division, and changes the
  shared expression scanner to diagnose an inserted closing parenthesis while
  retaining its value and triggering token. All four original text and full
  diagnostic comparisons match native on that snapshot
  (`expl3-macro-v2-recovery.out`). The candidate is installed in
  `tex_engine/expansion.ls`, with paired regression
  `test_tex_script_expression_recovery.{ls,txt}` (S1.8, D10.2.2). The 13-case
  integer/dimension/glue and helper matrix plus 12 EOF, nested, macro,
  overflow and division edge cases match native text and diagnostic records
  in JIT (`expr-recovery-{v2,edges}-jit.out`). The earlier absolute-value-only
  snapshot matched the integer-helper golden in both tiers. The full v2
  integer-helper golden also matches in both JIT and interpreter; the 13-case
  interpreter matrix matched JIT exactly. `\meaning` for
  all four staged helpers also matches native text and diagnostics
  (`expl3-meaning-v2.out`). The v2
  expansion hash is
  `dec439474573ccd5225ab75ed842b1001105fe2d29d56c30f29b1adeab17f48f`.
  The existing division golden matches against v2 in JIT. The 12 EOF and
  malformed-expression edge cases also match the JIT output in interpreter.
  Five direct and aliased helper probes retain the existing integer
  source-mapping guard and match native text and diagnostics
  (`expl3-v2-guard.out`). The division golden also matches in interpreter.
  Eight random-state, alias and expanded-definition comparisons match native
  text and diagnostic records in JIT and interpreter
  (`expl3-v2-sideeffects-{jit,interp}.out`). An
  included-file case matches native text and both diagnostic records in JIT
  and in interpreter with scheduled poisoned GC
  (`expr-recovery-foreign-{jit,gc-interp}.out`). Six
  neighboring numeric, dimension and expl3 fixtures were checked;
  missing-number recovery, numeric conditionals, dimension-unit recovery and
  expl3 integer comparison, comparison-expression and integer overflow have
  all matched (`expl3-v2-neighbors.log`). The 13-case integer/dimension/glue matrix also
  matches native under forced poisoned GC in JIT (`expr-recovery-v2-gc-jit.out`).
  A separate-output release build advanced past the DOM error
  after Radiant was moved to `b0a225b`, but found the document-preview adapter
  requires Radiant `ad7fdc8` (`release-build.log`). That existing descendant is
  paired with Lambda commit `68c875a10`; the clean Radiant checkout was moved
  there, leaving the gitlink unstaged. The single-job split release build then
  passed with zero errors and 110 executable-link warnings
  (`release-build-paired.log`). Its separate executable has SHA-256
  `3adb338414df9582368d9dc5d98b132b62b72e1845eb8ec77927819706ecd252` and did
  not replace the sweep's `lambda.exe`. It includes concurrent input-owner
  edits, whose hashes and source revisions are in `release-current-provenance.json`.
  The Radiant gitlink also changes owner layout sources; its integer-cast lint
  passes (`radiant-lint.log`) and its baseline runs in session 88575.
  The combined 25-case scanner regression matches native text and diagnostics
  on this release host (`expr-recovery-v2-release.out`). Three interleaved
  baseline CPU samples were 10.084/10.030/10.063 s and candidate samples were
  10.112/10.108/10.125 s, with identical native-checked output hashes
  (`bench-integer-isolated-pairs.json`). The combined installed-source harness passes
  all four focused tests and the new regression under poisoned GC in both tiers.
  The integer
  and existing-raw workloads have identical baseline/candidate output with
  successful native checks (`bench-{integer,raw}-smoke-{baseline,candidate}.out`).
  The frozen package trees differ only in the tested engine files.
  A previously pinned 2026-11-19 release host (`c43bc5f0...`, provenance in
  `temp/lambda_frontend_profile/provenance.json`) can run the four ordinary
  helper probes and matches the current debug-host output. It is older than
  the current runtime and cannot establish current-release qualification.

- **Concurrent input-owner work:** New edits appeared in
  `lambda-input/lambda/format/format-utils.cpp`, its markup formatter test, and
  `vibe/Lib_Input_Issue_Ledger (fixed).md` while this sweep ran. They are outside
  this port and must be preserved. Account for them when qualifying a fresh
  build; the frozen script sweep still uses its pinned executable and package.

- **Constructor-mode default qualification, 2026-11-24:** In the included
  nested-definition reproduction, the installed default executes and removes
  the definition inside a command argument; native preserves it. Explicit
  constructor mode matches native text and tree (`constructor-modes.out`). A
  frozen `default-lmd` snapshot changes only the LaTeX profile's absent-option
  default to true and retains an explicit false option. The default and true
  probes match native; false retains the existing difference
  (`default-options{,-interp}.out`). Five explicit-false scripts produce
  byte-identical output, diagnostics, issues and ASTs on the installed and
  candidate packages in JIT and interpreter; all four outputs hash to
  `ae304bb2547cba92aec9d3ceb0093bea8d65ee90b4a8c7a657ec3d4fbe74ec88`.
  The real GTest harness is sweeping 231 discovered
  script-engine fixtures on JIT with two workers in session 6972; source,
  host, harness and package hashes are in `default-sweep/manifest.json` and
  the full log is `default-sweep/jit.log`. Poll the existing run, classify
  every changed result against native with aligned resource and adapter
  options, and qualify interpreter and explicit-false behavior before any
  default change. Several rows have exceeded the unchanged 60-second deadline
  under concurrent load so far; rerun them independently after the sweep.
  (S1.8, D10.2.2).

- **Generated length and definition source mapping, 2026-11-24:** Installed
  the staged parser after the nine definition goldens matched the same
  candidate snapshot (`definition-goldens-final.log`). It aligns verified
  serialized token spans, preserves mapped group-edge whitespace and parses
  echoed definition command names using native lexical boundaries. TeX
  expansion remains governed by its original tokens (S1.8, D10.2.2). The
  nine-case native-tree parity fixture and the earlier guarded reproduction
  match their goldens against installed modules in JIT and interpreter. The
  installed digest SHA-256 is
  `8b9c81f49e7c84699ec714d3a219f19d9346721375e7f756bdcec6d7204f99ce`;
  it differs from the qualified snapshot only in a marker name and comment.
  The
  milestone `make test-lambda-baseline` stopped before tests: the split-runtime
  build reports that `lambda/dom/dom.cpp:735` passes seven arguments to the
  selected Radiant declaration of `radiant_dom_mutation_begin`, which accepts
  six. The full compiler diagnostic is in `temp/build_split-runtime.log` and
  the gate log is `temp/latex-port-resume/lambda-baseline.log`. The wider TeX
  corpus and production migration remain open. The DOM call was paired with
  a Radiant revision whose seven-argument overload uses the old value for
  relational-style invalidation; see [LR12-72](../Lambda_Issue_Ledger.md#lr12-72).
  The Test262 baseline was not run; its target also builds the split runtime.

- **Nested definition mapping, 2026-11-24:** After definition tokens were
  normalized for native document parsing, the nested parser still compared
  them to unnormalized TeX-reader tokens. A staged change in
  the script digester normalizes the reread with the same
  scanner before comparison, preserving the independent native-token check.
  Six constructor-mode cases match native expansion, diagnostics and trees
  in JIT and interpreter; their false-mode counterparts remain guarded. The
  existing nested-definition and foreign-definition goldens match both tiers,
  and scheduled poisoned GC matches the matrix in both tiers. An included
  nested-source probe still stops at the explicit source-mapping guard.
  The 12-case installed regression matches its golden in both tiers.
  The pinned host used for this investigation has SHA-256
  `26e6c872e5cab1e50d482fb95e096c3928b21447804d5c4754a4dd399242b97d`.
  Further included-source qualification remains (S1.8, D10.2.2).

- **Included nested definitions, 2026-11-24:** The included-source guard
  compared a raw token slice with a normalized reread. The installed change
  normalizes both comparison inputs
  and retains the independent native-token and emitted-byte checks. An
  eight-file constructor-mode matrix then exposed an adjacent optional group
  that native attaches to a nested definition; the parser now attaches
  those groups using the generic argument scanner and a shared node merger.
  All eight trees, expansion texts and diagnostics matched on the staged
  implementation and after the merger refactor, in both JIT and interpreter.
  Eight following required, optional, parenthesized and whitespace/comment
  forms also match in constructor mode in both tiers (`following-matrix-*`).
  The prior engine guarded six of these eight; all eight non-constructor-mode
  expansion differences reproduce on that engine (`nested-foreign-installed.out`)
  and remain open. The adjacent nested, foreign-definition, definition-join,
  definition-continuation and nested-native-name goldens match. A combined
  included-source probe matches under poisoned GC in interpreter, and the
  following-group probe matches under poisoned GC in JIT. The new installed
  `test_tex_script_nested_native_foreign` matches in both tiers. Installed
  digest SHA-256 is
  `0421d4411e10a3cdab745e439464828fe248dc5c7b564fd25d7a843e224d6da2`
  (S1.8, D10.2.2).

- **Correction, 2026-11-24:** The exact-span installation described below
  was reverted after an independent echoed definition containing `\a@b`
  produced a silently different native tree alongside a simple generated
  `addtolength`. The restored engine reports `command source mapping` on
  that reproducer (`temp/latex-port-resume/unrelated-definition-reverted.out`).
  The experimental fix and its regression are retained under
  `temp/latex-port-resume/`, not installed. The staged parser joins and splits
  source-backed definition-body names at native lexical boundaries and
  restores mapped group-edge spaces. It matches the unrelated-definition
  reproduction, 40 lexical comparisons, 16 generated-command comparisons,
  and six changed-category comparisons in JIT. Of four leading-punctuation
  comparisons, escaped underscore now matches; the other three stop at
  an existing lexical guard. Foreign-command, generated-name and direct-name
  adjacent goldens match on the candidate in JIT. Source-backed and
  reconstructed word joins now share one scanner. The 24-case definition
  matrix matches 11 native trees in constructor mode, explicitly guards one
  nested definition, and guards all 12 cases with constructor mode off;
  all expansion texts and diagnostics match. Six changed-category cases also
  match in interpreter. Three additional leading-punctuation definition
  cases match in constructor mode. The installed guard has a permanent
  regression in `test_tex_script_generated_definition_guard`. Finish wider
  lexical, definition and both-tier qualification before installing
  (S1.8, D10.2.2).
  The current candidate additionally rejects unmappable group edges. Its
  prior snapshot matched 48 generated-value and following-argument cases in
  JIT, and eight in interpreter, with an unrelated `\a@b` definition in
  each source; the original unrelated-definition comparison also matches
  under scheduled poisoned GC in JIT. After the edge check, candidate length,
  and direct-name goldens completed and matched, as did the generated-name
  golden and a 16-value interpreter regression. The candidate is still under
  `temp/`.

- **Definition targets, 2026-11-24:** Further qualification found five
  successful but differing candidate trees when native command boundaries
  changed the definition target (`targets-matrix.out`). The installed engine
  remains guarded. The staged parser now normalizes command tokens across
  the echoed definition before parsing its target and body, using the same
  native name scanner; TeX expansion still uses the original tokens. All
  five target trees match in constructor mode in JIT and interpreter, and the
  other mode remains guarded (`targets-native{,-interp}.out`). The 24-case
  definition matrix retains its 11 matches and explicit guards, and all 48
  generated-value comparisons match (`definitions-native.out` and
  `value-native.out`). The staged length, direct-name and generated-name
  goldens matched before this latest change. The definition-option and
  direct-name goldens were rerun and both match. Four superscript-source cases also
  match native in constructor mode, with explicit guards in the other mode.
  Changed-escape probes retain guards where serialization cannot be mapped;
  two native inputs also fail their own LaTeX parse (`escape-matrix2.out`).
  Qualify other source-boundary cases before installation (S1.8, D10.2.2).

- **Active letters in definition names, 2026-11-24:** A follow-up found
  that `@` or an ASCII letter with TeX category 13 could be split out of a
  source-backed command name, yielding a different native tree. The staged
  shared join scanner now includes verified active source characters; three
  target/body probes match in constructor mode in both tiers, and the other
  mode remains guarded (`targets-active2.out`, `targets-active-interp.out`).
  Candidate digest SHA-256 is
  `35d8cd513bb19ea05a2d2763d854615d74b2f620f33a78b25eb948059d3599fa`.
  A nine-fixture JIT definition sweep is running in session 22442. Its first
  fixture failed to import a helper before that helper was copied into the
  staged tree; its retry matches the golden. The candidate changed during
  the sweep; final-snapshot nested-definition and continuation retries run in
  sessions 37159 and 50319. The 16-value
  regression now matches native trees, text and diagnostics in both JIT and
  interpreter (`v2-port-{jit,interp}.out`).
  Qualification remains required before installation (S1.8, D10.2.2).

- **Definition regression sweep, 2026-11-24:** The candidate definition
  sweep matched seven fixtures after retrying the initially missing local
  helper, but active-definition and group-source-gap goldens differed. The
  active failure is the blank-line case: a synthetic `par` token was joined
  with the following `Y` as if it had a literal control-word source. The
  staged scanner now verifies the source spelling before joining or splitting;
  the isolated native tree matches (`active-tree-fixed.out`). The full active
  fixture also matches its golden. Group-gap isolation identified four blank-
  line cases in definition bodies and defaults; all four match after the same
  fix (`group-short.out`). The full definition-join golden and 16-value
  interpreter comparison match on the corrected snapshot; the full group-gap
  golden also matches. The original incorrect-tree reproduction matches in
  both tiers with scheduled poisoned GC (`v3-gc-{jit,interp}.out`). A
  24-case interpreter definition matrix retains 11 matches and the expected
  guards, with matching expansion and diagnostics. A focused nine-case parity
  fixture also matches in both tiers (`parity-{jit,interp}.out`). The
  nine-fixture definition sweep against the same snapshot runs in session
  35539; poll it before installation.
  The staged digest now hashes to
  `286cdfbac0c57bf5b5b5cd63d1816743bd58f9f52809e9ab885613aff38c7f3e`;
  earlier candidate receipts do not qualify this changed snapshot
  (S1.8, D10.2.2).

- **Resume check, 2026-11-24:** The staged `temp/tex-addlength-digest/`
  snapshot and its recorded probe artifacts are absent from this checkout, so
  their results cannot be used to install the change. A fresh reproducer at
  `temp/latex-port-resume/probe.ls` expands a macro-generated `addtolength`;
  Script and native expansion text agree, but digestion stops at `command
  source mapping` while native returns a tree. Reconstruct and qualify the
  emitted-token mapping and native lexical boundaries before installing a fix
  (S1.8, D10.2.2). The existing `setlength` mapping is a starting point, but
  `addtolength` produces a generic command node with argument-group offsets.
  A follow-up marks the interior of coalesced synthetic output only when
  every token's spelling exactly matches the serialized span. Those marked
  control tokens can use their emission origin in constructor mode; other
  generated spans remain guarded. A 16-case JIT probe matches native text,
  diagnostics and full trees for 12 cases; four `dimexpr` cases still stop
  at the guard because serialization inserts control-word spaces. The new
  12-case `test_tex_script_addlength_digest` matches its golden in direct JIT
  and interpreter runs. A combined length
  fixture exceeded the 60-second harness deadline under concurrent load;
  the new checks now have a separate fixture. The original combined direct
  interpreter run completed and matched all 112 original, 12 generated and
  two foreign checks plus the 12 new checks. A subsequent alignment prototype
  admitted one serialized space after a control token. Its four `dimexpr`
  cases produced different complete trees despite matching expansion text and
  diagnostics, so it was removed. A later staged candidate restores the
  mapped group boundary as described below. Reparse the emitted
  bytes with native lexical rules, including definition boundaries, rather
  than inferring token positions from spelling alone. The adjacent direct-name,
  generated-name and foreign-command harness fixtures pass in JIT (3/3;
  `temp/latex-port-resume/neighbors-jit.log`). Further source-mapping and
  corpus qualification remains before considering this change complete.

- **Serialized group boundaries, 2026-11-24:** A candidate under
  `temp/latex-port-resume/engine/` aligns serialized tokens and restores the
  output gap before a closing group delimiter. The original 16 generated
  comparisons now match native trees, expansion text and diagnostics,
  including all four `dimexpr` cases (`matrix-candidate2.out`). A further
  40-case lexical probe exposed four silent tree differences for `\a@b`
  in an echoed definition (`lex-matrix.out`, `compare-at.out`): the TeX
  reader splits that name at `@`, while the native document scanner keeps it
  in one control word. The installed engine guards this sample. The candidate
  must reparse definition-body names using native lexical boundaries without
  changing TeX definition target and parameter scanning, and qualify the
  full lexical matrix before installation (S1.8, D10.2.2).

- **Generated `addtolength` digestion, 2026-11-18:** generated length
  expansion matches native text, but its document node previously stopped at
  `command source mapping` (**S1.8**, **D10.2.2**). The staged digester in
  `temp/tex-addlength-digest/` reparses coalesced output with native lexical
  rules, maps node offsets to the original emission, and includes following
  argument groups. Its 80-case generated matrix and 30 following-group cases
  match native. The probes also exposed definition control words whose TeX
  category codes differ from native document scanning. The staged scanner
  joins and splits source-backed fragments while retaining TeX definition
  boundaries; the 32-case category-code matrix and 34 escaped-punctuation
  comparisons match native. The 128-case consolidated regression and final
  neighboring-golden checks remain to run before installation. The failed
  all-native-catcode prototype is isolated in `temp/tex-native-definition/`.

- **`setlength` digestion investigation, 2026-11-18:** native document
  nodes read the raw emitted target and value (**S1.8**, **D10.2.2**). The
  installed digester recognizes echoed
  spans and individually reconstructed direct constructor tokens, checking
  source boundaries and spelling before building the node. The staged direct
  matrix matched 80 native trees; 24 nested comparisons and 36 symbol-target
  comparisons also match; emissions without a verified mapping remain guarded.
  Three previously guarded sample documents now reach subsequent parsing:
  `timeline` matches its complete native tree; `foray_into_latex` and
  `nursing_care_plan` reach existing math and environment guards. The new
  `test_tex_script_length_digest` passed on the staged snapshot in both
  tiers; the installed snapshot passes in interpreter and JIT with scheduled
  GC poisoning, including the two included-source cases. The 31-file candidate
  corpus in `temp/tex-length-digest-final/corpus-bundled/` finished with 18
  matching trees, 13 explicit stops, and no differing completed expansion.
  Generated-command qualification in `temp/tex-length-digest-generated/`
  found native adds control-word spaces when serializing generated values.
  Expansion now uses TeX token printing after assignment, and digestion maps
  the emitted span. The 80-case generated matrix matches native text,
  diagnostics and trees, as do active target and `escapechar` probes. The
  112 direct and 12 generated permanent comparisons passed on the staged
  implementation in both tiers. Installed expansion and digestion regressions
  pass with scheduled GC poisoning in both JIT and interpreter.
  The 31-file constructor candidate (`temp/tex-length-generated-candidate/`)
  has 18 matching trees, 13 explicit stops and 30 matching completed expansions.
  The installed-default snapshot (`temp/tex-length-generated-final/`) has
  pre-existing mismatches in `booktabs`, `nursing_care_plan` and `slashbox`;
  each was reproduced on the installed baseline. In `booktabs`, the default
  constructor mode drops `\relax` from `\date{\relax}`. The candidate
  constructor mode preserves it, but remains disabled pending full qualification.
  A 24-case declaration and formatting probe confirms the candidate matches
  native text, diagnostics and trees, while explicit false mode differs in seven
  cases (`temp/tex-constructor-qualification/date_probe.out`). All 13 focused
  package goldens match the candidate (`temp/tex-constructor-qualification/`).
  A broader 140-fixture JIT qualification against the frozen candidate was
  stopped after 62 fixtures finished when the goal was cleared; two further
  subprocesses were interrupted. Results are in
  `temp/tex-constructor-qualification-full/`. The installed constructor
  default is unchanged until qualification is completed and reviewed.
  The changed `counters_fixture` golden was checked directly with aligned
  `adapters: []`: candidate text, diagnostics and full native tree match.

- **Length commands, 2026-11-18:** `newlength`, `setlength` and
  `addtolength` now use the native LaTeX target scanning, skip allocation and
  TeX assignment paths (**S1.8**, **D10.2.2**). The staged 54-case matrix
  matches native expansion text, diagnostic records and messages in both
  constructor modes; counter, profile and constructor recovery goldens also
  pass. `newlength` document digestion matches on direct probes; `setlength`
  digestion now handles the mapped direct forms described above. The installed
  `test_tex_script_length` adds the matrix, document and included-file checks;
  JIT and interpreter runs pass, including scheduled GC with poisoning in
  both tiers. A 31-sample candidate corpus with native-aligned bundled
  resources and adapters finished under `temp/tex-length-corpus/corpus-bundled/`:
  17 trees match, 13 stop at digestion guards and one at an unsupported
  primitive. All 30 completed expansions match native text and diagnostics.
  Its constructor default is only a frozen candidate and has not been enabled
  in the installed engine.

- **Sample corpus and paragraph boundaries, 2026-11-18:** the 31 files in
  `test/latex/samples/` were run through the frozen constructor-default
  candidate (`temp/tex-constructor-final/corpus/`, source/module manifest
  included). Twenty-five runs returned and six aborted because a normalized
  whitespace token has no source end; `paragraph_collect` now validates that
  field before passing it to the shared reconstruction helper. Seven runs
  returned different trees and three have text or diagnostic differences, so
  this corpus does not qualify the default. The probe supplied an empty bundled
  resource map while native can load bundled files: `math_proof` loads native
  `ifthen.sty` while Script passes its adapter through. With all bundled
  resources captured and `adapters: []` to match native's default options,
  `math_proof` expansion text and diagnostics match. Two 31-file passes
  with those resources and adapter settings completed in sessions 93733
  (installed paragraph implementation snapshot) and 98005 (staged comment
  fix), under `temp/tex-constructor-corpus{2,3}/corpus-bundled/`. Both snapshots
  contain the adapter-marker regression described below; repeat qualification
  after restoring the correct marker behavior. In particular,
  `booktabs` and `complex_numbers` exposed double-counted control-word gaps
  before blank lines. The installed digester now reconstructs consecutive
  direct paragraph tokens as a single source span, checks mapped output when
  present, and avoids adding the control gap twice. The staged
  80-case whitespace matrix and both complete sample trees match native;
  five neighboring paragraph, declaration and environment goldens pass in
  JIT. The new 74-case native text/diagnostic/tree regression passes on the
  installed modules in both JIT and interpreter.
  The subsequent staged comment fix in `temp/tex-paragraph-follow/` also
  matches `greek_russian` and `diagonal_table_cells`: direct source gaps must
  retain comments, including comments that create an empty paragraph. Its
  80 whitespace, 66 inline-comment and 90 comment-before-block comparisons,
  plus five neighboring goldens, match native. The change is installed; the
  146-case aggregate passes on the installed tree in both tiers. A corrected,
  bundled-resource 31-file pass with restored adapter markers completed
  (`temp/tex-constructor-corpus4/corpus-bundled/`): seven complete trees
  matched, one differed and 23 were unsupported. The remaining successful
  mismatch (`thai_polyglossia`) loses whitespace before a source-span
  definition; `temp/tex-paragraph-span/` stages a fix that matches the sample.
  Its 144-case definition-gap matrix matched native; the fix and its 108-case
  regression pass in both tiers. `pulley_systems` and `spacetime_diagrams`
  exposed raw TikZ bodies being expanded instead of preserved. The installed
  raw-environment port matches native text and diagnostics on both samples
  and direct, indirect, missing-end and included-source probes; raw document
  digestion remains explicitly unsupported. Its 32-case name matrix, four
  neighboring environment goldens, and staged GC probes in both tiers pass.
  The 60-case installed regression and scheduled-GC probes with poisoning pass
  in both tiers.
  The refactored begin path preserves the original document hook list. A new
  bundled-resource corpus snapshot completed
  (`temp/tex-constructor-corpus5/corpus-bundled/`): eight trees matched, 23
  were explicitly unsupported and none returned a differing successful tree.
  `vietnamese_document` initially had expansion and diagnostic differences;
  the probe also omitted `tex/binding/`, which native searches after the root.
  Capturing both directories with native precedence makes its text and
  diagnostics match (`vietnamese-bindings.out`). Of the other samples, 25
  matched native text and diagnostics and five stopped before comparison.
  `temp/tex-group-gaps/` explores digestion of retained raw spans using the
  native header rules and reconstruction offsets. Its 128 direct raw cases
  and malformed/generated-name guards match native where supported. A shared
  group parser now restores direct source gaps
  around group contents, including comments; this fixes tabs dropped in
  unrelated groups and makes the full `pulley_systems` tree match. The group
  change is installed after a 70-case comparison in both tiers and five
  neighboring goldens. Synthetic parser events also no longer read
  nonexistent source offsets. The 31-file bundled-resource candidate pass
  completed with 17 matching trees and 14 unsupported
  (`temp/tex-constructor-corpus6/corpus-bundled/`); its apparent Vietnamese
  mismatch was the missing binding resource noted above. Generated probes found three
  false-mode mismatches that also occur on the pre-group baseline;
  constructor-mode results match native. Raw digestion is now installed:
  its 78-case regression and revised expansion fixture pass in both tiers,
  as do staged GC edge probes. Installed GC checks run in session 71951; a
  follow-up iterative whitespace-scan check runs in sessions 2227 and 75972.
  The corpus snapshot with both root and `binding/` resources and matching
  adapters completed (`temp/tex-constructor-corpus7/`): 17 matching trees,
  14 unsupported, and 26 matching native expansion texts and diagnostics;
  five stopped before expansion comparison. Runtime, source, module and
  captured-resource hashes still match its manifest. The
  included-source raw tree matches
  in constructor mode and remains guarded without it; malformed and ambiguous
  raw spans retain a source-mapping guard. Recheck installed tests and corpus
  before considering the qualification complete.
  Other unsupported samples still need investigation. This pass does not
  qualify the production migration (S1.8, D10.2.2).

- **Constructor default qualification, 2026-11-18:** `temp/tex-constructor-default/lmd/`
  is a frozen copy of the package tree with only the absent-option default
  changed: LaTeX profiles use constructor-argument mode, and explicit `false`
  retains the installed behavior. A direct probe reproduces the native state
  difference and confirms all three option choices. The three-worker JIT sweep
  of 155 default-mode engine fixtures completed: 144 matched their goldens,
  ten differed and one aborted on the pre-fix positional result error;
  `temp/tex-constructor-default/sweep/{manifest,progress}.json` records frozen
  module, runtime and fixture hashes and each result. Module, runtime and
  source hashes remain unchanged; the profile and definition-option goldens
  were intentionally updated after the snapshot. The differing font, paragraph,
  parenthesis, foreign and unexpanded-source cases have been checked against
  complete native trees; the expansion and expl3 differences already occur
  with the installed default. A fresh snapshot with the subsequent fixes and
  default candidate is running the 11 changed fixtures and two new regressions
  in session 63185 (`temp/tex-constructor-final/sweep/`). Recheck the live
  session before restarting and classify any remaining differences. The
  separate `temp/tex-constructor-seq/` candidate tests native-shaped sequence
  insertion inside constructor arguments. Simply expanding the existing
  sequence wrapper is insufficient: seven of eight native comparisons emit
  an unexpanded `\\msg_expandable_error:nn` instead of the native macro
  mismatch diagnostic. See `temp/tex-constructor-seq/probe.out`; do not enable
  this candidate without implementing the underlying error expansion. The
  bundled expl3 source defines the message chain around lines 11440–11469,
  including a deliberately delimited `\\???` macro; the Script profile has
  not yet ported that chain.
  The sweep also exposed a pre-existing abort in `definition_option`: the
  generic delimiter scan omitted `index` on an unclosed argument, violating
  the positional parser result contract. The scan now carries that position;
  the existing 28-case fixture matches its golden in JIT and interpreter.
  The frozen sweep predates this fix and its corresponding failure is expected.
  Echoed definitions can also end inside a balanced native argument: a staged
  continuation now replays only adjacent direct tokens that both scanners
  reproduce and stops at the exact parsed token boundary. The installed change
  matches native text, diagnostics and trees for 22 cases in both tiers; the
  prior 28-case definition-option fixture and adjacent definition suites pass.
  Cases whose continuation contains consumed or differently tokenized source
  remain guarded. A separate conditional probe found native recovers from an
  unmatched `\\fi` while Script stops; `temp/tex-conditional/` stages recovery,
  but it is not installed because document-end incomplete-condition diagnostics
  also need correct state and source locations.
  The counters package fixture demonstrates why a successful digest alone is
  not sufficient: with the candidate default the script retains
  `\\textcolor{red}{highlight}` while native expansion emits
  `highlight`, producing a different tree without an unsupported result.
  `temp/tex-constructor-default/counters-detail.out` has both complete trees
  and reconstructed texts. The reduced `loaded-probe.out` traces the branch
  to package state: the initial probe compared Script's default adapter
  registry against native `parse` with no adapters, incorrectly concluding
  that adapter-only entries lack loaded markers. Native `record_package`
  confirms configured adapters receive markers; the erroneous marker change
  has been reverted. `test_tex_script_adapter_state` now passes the same
  `packages` list to native and Script and passes in both tiers, including the
  complete local-package text and tree; a separate bundled-adapter fixture
  also passes in both tiers. The frozen candidate sweeps
  include the erroneous marker change and must not be used to qualify the
  final default; the profile golden is restored. Recheck changed cases and
  package behavior with matching options before promoting the default.
  The expansion and expl3 fixture
  differences observed so far are identical with the installed default and
  reflect earlier recovery changes, rather than the candidate option.
  S1.8 and D10.2.2 govern both investigations; production remains native.

- **Definition-following groups, 2026-11-18:** the original consumed-
  assignment offset reproductions now match native in both constructor modes;
  the later definition-boundary fix already addressed those cases. The installed
  digester now attaches following
  groups using reconstructed segment offsets and the shared generic argument
  scanner. Macro bodies retain parsed groups; environment-definition bodies
  retain native raw groups; primitive delimited definitions leave later groups
  in the inline stream. A 64-case direct matrix and 32 generated-token cases
  matched native in both tiers on the earlier candidate, and an included-file
  probe matched in both modes. Candidate v5 also matches 24 comment and
  whitespace cases in JIT. Its staged 32-case/two-foreign-case fixture matches
  interpreter and JIT, including forced collection with poisoning in both tiers.
  Seven adjacent goldens match in both tiers. The installed join, boundary and
  active-definition fixtures match in both tiers; the installed join fixture
  also matches with scheduled collection and poisoning in each tier. A release host comparison on
  30 unchanged definitions produced median process CPU of 9.704 s baseline
  and 9.722 s candidate with matching output and native ASTs; timings include
  startup and JIT and do not qualify overall port performance. The release host
  and source hashes are in `bench-release-hashes.txt`; samples are in
  `bench-release.json`. `test_tex_script_definition_boundary` has one expected
  unsupported result now asserts native equality; `test_tex_script_active_definition`
  likewise checks the recovered AST instead of the former guard. Candidate and
  comparison evidence are in `temp/tex-definition-candidate/` (S1.8, D10.2.2).
  Production migration remains open.

- **Box dimensions, 2026-11-18:** the installed implementation models `\wd`,
  `\ht` and `\dp` as
  dimensions of an identity-shared box, independent of register-group
  snapshots. It handles void boxes, assignments, prefixed assignments, scalar
  and dimension reads, and the native one-time approximate-metrics diagnostic.
  The staged 40-case expansion matrix matches native; its first 20 cases pass
  interpreter and JIT, and 80 AST combinations match in each constructor
  mode in JIT. An included-file text and diagnostic probe also matches. The
  40-case/eight-AST shared-helper fixture passes interpreter and JIT, including
  scheduled collection and poisoning in both tiers. The installed fixture adds
  an included-source diagnostic comparison and passes both tiers with and
  without scheduled collection and poisoning; the neighboring box-register,
  box-digestion and box-movement goldens also match in both tiers. Evidence:
  `temp/tex-box-candidate/` (S1.8, D10.2.2). The long-running expl3 probe had
  already loaded the prior engine; its source hashes and expansion snapshot are
  `temp/tex-real-expl3-700k-source-hashes.txt` and
  `temp/tex-box-candidate/probe-baseline-expansion.ls`.

- **Expression arithmetic recovery, 2026-11-18:** TeX integer, dimension,
  glue and muglue expression arithmetic now retains an overflow flag until
  the expression terminator, reports `tex-arith-overflow` once and returns
  the appropriate zero. Nested explicit expressions save and restore their
  caller's flag. Fifteen native comparisons cover overflow, zero division,
  continued operands, nested expressions and conditionals. The expanded
  numeric fixture matches in interpreter and JIT, including scheduled GC with
  poisoning. The adjacent dimension fixture passes both tiers and expl3
  integer-math passes JIT; its concurrent interpreter harness run timed out
  at 60 seconds but a direct interpreter replay matches the golden. Evidence:
  `temp/tex-expr-{interp,jit}.log`, `temp/tex-expr-gc-{interp,jit}.out` and
  `temp/tex-expr-int-math-interp.out` (S1.8, D10.2.2). The three recovery
  fixtures now share `_tex_script_recovery.ls`; all three match in interpreter
  and JIT after extraction, and the numeric fixture also matches with forced
  collection and poisoning (`temp/tex-helper-*`, `temp/tex-expr-helper-gc-*`).
- **Bundled expl3 probe, 2026-11-18:** with the actual bundled TeX and Unicode
  resources and the Script engine, a bounded 120,000-expansion JIT run reaches
  `expl3-code.tex` line 8620 without diagnostics and stops at its budget;
  `temp/tex-real-expl3-nolog.out` records its state. This debug-host correctness
  probe does not complete the format or establish performance. The first probe
  was interrupted after debug logging grew `log.txt` to 9.3 GB; subsequent probes
  use the existing `--no-log` option without changing `log.conf`. A longer run
  reached 700,000 expansions with no diagnostics, with `expl3-code.tex` at line
  34066 and `UnicodeData.txt` open, then stopped at its explicit budget. Its
  captured source hashes and expansion snapshot predate the box-dimension
  installation above; see `temp/tex-real-expl3-700k.out` and the source-hash
  manifest. This does not establish completion of the Unicode or format load.

- **Dimension magnitude recovery, 2026-11-18:** oversized dimensions now
  report `tex-dimension-too-large`, clamp to TeX's maximum scaled-point
  magnitude and continue. Dimension digits reuse the integer scanner, including
  its once-only oversized-number diagnostic and token retention. Magnification
  and unit conversion check TeX's intermediate limits before scaling, avoiding
  a Script integer overflow and runaway allocation reproduced by
  `\mag=1\dimen0=2147483647truein`. The unit regression now includes 22
  magnitude cases, with decimals, `sp`, converted units, negative register
  values, glue, macros and extreme magnification. The 45-case combined fixture
  and the numeric-overflow fixture pass interpreter and JIT; the combined
  fixture also passes both tiers with scheduled collection and poisoning. The
  existing numeric-math golden matches in a direct JIT run. Evidence:
  `temp/tex-dimension-large-{final,gc}-{interp,jit}.log` and
  `temp/tex-dimension-numeric-jit.out` (S1.8, D10.2.2). Broader TeX and document
  parity and production migration remain open.

- **Dimension-unit recovery, 2026-11-18:** absent or invalid units now report
  `tex-illegal-unit`, insert `pt` or `mu`, and retain the tokens following a
  failed keyword scan. Infinite glue consumes every excess `l` after `filll`,
  diagnosing each while keeping order three; the scan is iterative. The
  23-case regression compares native text, messages and full diagnostic records,
  including partial units, macro expansion, math glue and 120 excess letters.
  It and the numeric overflow regression pass in interpreter and JIT; the new
  regression also passes both tiers with collection every 100,000 allocations
  and freed-memory poisoning. Evidence: `temp/tex-unit-{final,gc}-{interp,jit}.log`
  (S1.8, D10.2.2). The later checkpoint covers dimension magnitude recovery;
  broader malformed-input recovery remains open.

- **Register arithmetic recovery, 2026-11-18:** failed register
  multiplication and division now record `tex-arith-overflow` and retain the
  previous count, dimension, glue or muglue value. The native comparison adds
  12 cases for zero divisors, multiplication limits, scope, and chained errors
  to `test_tex_script_number_overflow`. That regression and the neighboring
  missing-relation golden pass in interpreter and JIT. The combined regression
  also passes both tiers with collection every 100,000 allocations and freed
  memory poisoning; evidence is in `temp/tex-arithmetic-{final,gc}-{interp,jit}.log`
  (S1.8, D10.2.2). Expression and advance overflow recovery remain separate work.

- **Oversized integer literals, 2026-11-18:** decimal, octal and hexadecimal
  integer scans now report `tex-number-too-big` once, clamp the magnitude to
  2147483647, and consume the remaining digits, as native does. The new
  regression compares text, messages and full diagnostics for 14 native cases,
  including signs, exact limits, long literals, macros, conditionals and count
  assignment. It and the missing-number and missing-relation goldens pass in
  interpreter and JIT; logs are `temp/tex-overflow-{interp,jit}.log` (S1.8,
  D10.2.2). Dimension scanning and arithmetic overflow remain separate work.

- **Missing numeric relation, 2026-11-18:** numeric conditionals now record
  the native `tex-missing-relation` diagnostic, insert `=`, and replay the
  triggering token for the right operand instead of stopping. Existing expansion
  errors and end-of-input retain their separate paths. The new regression
  compares text, messages, and complete diagnostics with native for ten
  `ifnum`/`ifdim` cases, including nested and negated conditions, expanded
  operands and missing numbers (S1.8, D10.2.2). It and the missing-number
  golden pass in interpreter and JIT. The larger numeric-conditional golden
  timed out at 60 seconds in concurrent harness runs in both tiers; a serial
  direct JIT run matches its golden. Logs are `temp/tex-relation-*.log` and
  `temp/tex-relation-numeric-jit.out`. The broader port, malformed end-of-input
  recovery, and production migration remain open.

- **Missing-number recovery, 2026-11-17:** the shared integer scanner now
  records TeX's `tex-missing-number` diagnostic, returns zero and replays the
  triggering token instead of stopping expansion. The installed regression
  compares text and complete diagnostics with native for 21 ordinary cases,
  four expl3 expressions and an included file. A separate 31-case caller audit
  finds no silent mismatch; `\\ignorespaces` was unsupported at that point and
  is covered by the later expanded-space checkpoint above.
  A further 29-case JIT audit matches 24 cases and explicitly stops on five
  cases requiring unit, brace, relation or macro-parameter recovery. The
  subsequent checkpoint addresses a missing `\ifnum` relation; numeric
  overflow/division recovery remains open. The new regression and the expansion, numeric-conditional,
  expl3-integer-sign and stream goldens pass in both interpreter and JIT;
  the new regression also passes scheduled collection every 100,000 allocations
  with freed-memory poisoning in both tiers (12 checks). These runs used the
  previously pinned `temp/tex-structured/lambda-release-final` host, not a
  fresh full baseline. Evidence is in `temp/tex-port-continuation/`, including
  `checks-final.log`, `caller-jit.out` and `extra-jit.out` (S1.8, D10.2.2).

- **Capture follow-up, 2026-11-17:** `next` returns a nullable complete
  `ReadStep`; capture functions return complete `SourceSpan` values or an
  error. Private delimiter scans retain presence, endpoint and closure status;
  public capture functions choose the prior recovery behavior. Expansion
  handles capture failures at its input boundary. The E312 constrained-error
  issue is fixed in the shared core and compiler; the reader now declares
  its capture error constraint. S11.4.6v4 enforces its base at admission;
  predicate filtering remains explicit. Capture handlers filter this type and
  propagate unrelated errors. Normal and nullable success conversion, nested
  aliases, filtered propagation and JIT return-effect planning are covered in
  `test/lambda/constrained_error_return.ls`; all three tiers pass with forced
  and poisoned GC on every allocation. The input baseline passes 5,312 tests
  with three skips; Test262 passes 40,261/40,261. The final Lambda aggregate
  reports 7,236/7,239, retaining the PDF suite abort, editor assertion and
  unavailable WebRTC described in `temp/capture-error/lambda-summary-final-source.json`.
  Both tiers match 352 token/state and 39 raw capture cases
  from the preceding reader; 23 focused cases, two adjacent JIT goldens and
  285 LaTeX compile checks pass. Test262 passed 40,261/40,261. The Lambda
  baseline reported 7,235/7,238,
  including the previously observed PDF native-test abort, editor assertion
  and unavailable WebRTC dependency. Its script suite ran while the final
  caller EOF checks and verbatim scanner were changed, so the aggregate is
  mixed-source evidence; the focused cases were rerun after those edits.
  Evidence is in temp/reader-refactor/{reader-v2-identity.json,
  focused-v2-complete.json, adjacent-v2-final-jit.json, compile-v2-complete.json,
  lambda-summary-v2.json, test262-v2.log} and the _compare{,_raw}_v2-final2
  outputs. A same-host release comparison of 8,000 ordinary tokens gave
  median CPU 0.3236s before and 0.3262s after this follow-up (1.008x),
  including startup and JIT compilation; all outputs matched. Samples are in
  release-token-bench-v2.json. The previous qualification below describes
  the preceding revision, and the earlier reader performance issue remains open.

- **Companion module contracts, 2026-11-17:** structured stream/trace/argument,
  numeric scanner, reconstruction and expansion-state contracts are installed
  across the engine. The migration exposed and fixed open-record extra-field
  loss in runtime admission (D3.4.6/D4.2.5, S11.4.6v4). Remaining macro and
  digestion helpers are tracked explicitly; see the
  [structured-contract checkpoint](Lambda_Impl_Tex_Structured_Contracts.md).
- **Reader refactor completed, 2026-11-17:** reader.ls uses typed scanner
  records and explicit absence/capture outcomes, with no blanket any^ results.
  Preserve reader snapshots and all current TeX/native boundaries (S1.6,
  S1.8, S7.10.7, S12.1.2v3). Confined pn helpers cannot be invoked by the
  fn API (S12.1.1v3), so an escaping mutable closure is not the interface.
- **Reader worktree:** master at 1323612342784e3d8240a2d8342df89e271fecff,
  plus the query/map changes qualified in the convention plan. Preserve
  unrelated changes, including the review-document move. The original
  reader and staged typed draft are in temp/reader-refactor/{baseline,candidate}.
- **Reader installed:** all functions have concrete or nullable result types;
  no function returns any^. Source slices coalesce at text_span, malformed
  category overrides recover at catcode, and EOF/capture failures are structured
  outcomes. Raw scanners and control-word/^^ decoding use local loops.
- **Compiler repair:** S7.7.2v2 / S11.4.1v5 return proofs inspect final field
  expressions and ordered spreads rather than conservative storage recipes.
  Annotated spread literals no longer report supplied fields missing. Unknown
  keys and optional overwrites still validate dynamically; storage compatibility
  is unchanged. A final guard retains error completions from failed spreads.
- **Caller boundaries:** UTF-8 source decoding in expansion.ls now supplies
  explicit valid/text fields. digest.special_command has a string parameter.
  Newly error-free cache/record constructors no longer carry redundant carets.
- **Qualification:** the final release host passes all 782 previous/current
  token/state and raw-capture comparisons across interpreter and JIT; all 285
  LaTeX entry scripts compile; 23 focused reader/query/record checks pass,
  including scheduled GC and poisoning. Direct/nested failed-spread negatives
  pass on interpreter, JIT and AUTO. Test262 is 40,261/40,261 fully passing.
  The Lambda baseline's exposed caller contracts have focused successful
  rechecks; the aggregate remains non-green, including a native PDF allocation
  abort. Full results and host/source identities are recorded below.
- **Reader follow-up:** the full JIT sweep plus serial rechecks matches all
  285 goldens; host provenance is retained below. The final interleaved release
  measurement preserves output but costs 1.59x process CPU, including startup
  and JIT compilation. Typing friction and this unisolated cost remain open
  in the core ledger. The older port-wide checkpoint below is historical
  context, not qualification of the remaining production migration.

- **Goal:** complete the Script TeX expansion and LaTeX digestion port, qualify
  it against the native path, and coordinate production migration. S1.8,
  D10.2.2, D9.1.3v2 and D11.2.1 govern the current boundary.
- **Worktree:** `/Users/henryluo/Projects/Lambda`, `master` at `ffd7d5031`;
  updated 2026-11-16. The working tree contains the Script changes, tests and
  notes described below; sibling Input and Radiant trees have independent dirty
  changes. Production still uses the native engine.
- **Expanded generic arguments:** the digester now reconstructs the raw text
  and byte offsets of expanded generic-command arguments in both constructor
  modes, using the same provenance check as declarations. Staged comparisons
  covered 70 argument cases and an included source. The installed regression
  covers 24 native text, document-message, diagnostic and AST comparisons plus
  an included source in both modes; it and five neighboring GTests pass in the
  interpreter and JIT. The regression also passed scheduled collection every
  100,000 allocations with freed-memory poisoning in both tiers. The original
  message check excluded the native kernel preload; it now checks the full stream
  after the separate kernel-message fix, pending installed GTests.
  Three paired release samples preserved native AST and output
  (`bench-generic-arguments.log`);
  this is not overall performance qualification. The expl3 sequence and token-list
  guards remain: unguarded staging exposed separate list-item spacing, echoed
  definition control-word lexing and mixed expl3 expansion mismatches. See
  `expl3-seq-body-structures-jit.out` (S1.8, D10.2.2).
- **Kernel messages, installed:** native keeps kernel allocation messages for an
  ordinary run but discards them when it restarts from the expl3 format. The
  candidate in `temp/tex-script-f/kernel_message_candidate/` tracks executed
  format requests and retains the appropriate messages. In an initial JIT
  matrix all 36 full message comparisons and an included-source comparison
  match native. A second 44-case matrix has two message mismatches from the
  separately recorded expl3 register-allocation state (`count25` versus native
  `count125`); the results agree between interpreter and JIT. Explicit expl3
  package requests still differ in reconstructed text. Malformed package
  declarations preserve the native preload choice, while options remain
  unsupported. The generalized candidate and audited test updates are now
  installed. Eight selected GTests pass in both interpreter and JIT; broader
  corpus qualification remains open
  (S1.8, D10.2.2).
- **Format-request generalization:** `kernel_message_candidate_v2/` moves the
  package-list check to the same point as native, including explicit requests
  when `script_expl3` is false. Ten JIT cases match native messages and
  diagnostics; the separately recorded package-text mismatch remains. The
  57-case audit below used the earlier candidate. The generalized candidate
  passed focused JIT and interpreter comparisons and scheduled collection
  with freed-memory poisoning in both tiers. The eight installed GTests pass in
  both tiers. Three paired release samples retained identical output and native
  ASTs: baseline CPU 10.822/10.795/10.798 seconds and candidate
  10.801/10.647/10.656 seconds (`bench-kernel-message.log`). This does not
  qualify total port performance.
- **Kernel-message audit:** `sweep-kernel-updated.py` examined 57
  existing native-message assertions on the pinned `0d58ab8b...` release host.
  Twenty-nine original snapshots stayed equal; all 28 staged direct-native
  assertion updates preserved their other expected results. Source and golden hashes are recorded in
  `kernel-updated-manifest.json`. The expanded manifest also pins 1,888 package,
  test, fixture, candidate and host paths in `kernel-updated-full-manifest.json`.
  Staged test files and logs are under
  `temp/tex-script-f/`; the 28 audited updates are installed.
  Both manifests matched on completion. Original and replacement outputs are
  saved as `kernel-original-*` and `kernel-updated-*`; cached initial
  results use `kernel-message-sweep-*`.
  The focused candidate regression already matches native for 24 direct cases,
  four expl3 package message cases, four option-off requests, four malformed
  requests and both included-file modes in JIT and interpreter. Five tests that
  print document-message contents also retain their prior output and now compare
  the full native stream; their audited updates are installed.
- **Expl3 package setup:** the pass-through candidate in
  `expl3_package_candidate/` was rejected: all 24 comparisons differed from
  native text and AST. Native executes the bundled `expl3.sty` backend setup;
  forwarding the original declaration is incorrect. See
  `expl3-package-probe-jit.out`. With the bundled loader and 37 captured TeX
  resources, `expl3-native-preload-probe.ls` reaches `expl3-code.tex` line
  3160, then reports `tex-extra-fi` after 10,036 expansions. The native format
  completes this preload. Conditional tracing found the outer `\if` is not
  registered while its operands expand nested conditionals. A staged shared
  scanning fix matches 88 native text, message and diagnostic comparisons in
  both tiers, plus 48 focused expansion cases, eight AST cases and an included
  source. The focused cases pass scheduled GC with poisoning in both tiers. It
  advances the preload to line 7821 and 105,185 expansions, where
  `tex-missing-number` remains.
  The next failure is numeric scanning of the aliased `\pdftexversion`
  primitive, which is registered but has no Script numeric value; native
  treats it as a read-only integer. See
  `expl3-native-preload-number-trace-jit.out`.
  See `conditional_operand_candidate/`, `expl3_condition_trace/`, and
  `expl3-native-preload-conditional-jit.out`. Seven staged neighboring goldens
  passed. Three release pairs retained identical output and native ASTs:
  baseline CPU 10.672/10.821/10.787 seconds and candidate
  10.867/10.831/10.724 seconds (`bench-conditional-operand.log`). The 1,909
  pinned paths matched before installation. The fix and paired regression are
  installed; six focused GTests pass in both interpreter and JIT. Broader corpus
  qualification remains open.
- **Engine identity, installed:** native treats `\pdftexversion` as a read-only
  numeric engine identity. `engine_identity_candidate/` centralizes the native
  pdfTeX and e-TeX version and revision metadata and uses it for numeric scans,
  `\the`, and existing revision primitives. Eleven initial text, message and
  diagnostic comparisons matched native. A 74-case matrix matched in both tiers;
  the focused regression covers 64 expansion cases, eight AST cases and two
  included files, including foreign-file diagnostic locations. It passed
  scheduled GC with poisoning in both tiers, and six staged neighboring goldens
  passed. Three release pairs preserved identical output and native ASTs:
  baseline CPU 10.789/10.672/10.849 seconds and candidate
  10.760/10.860/10.777 seconds (`bench-engine-identity.log`). The change advances the upstream
  expl3 preload to line 8020 and 105,847 expansions, where `tex-illegal-param`
  remains (`expl3-native-preload-identity-jit.out`). The source and paired
  regression are installed; six focused GTests pass in both interpreter and JIT.
  Broader corpus qualification remains open (S1.8, D10.2.2).
- **Expanded-definition token lists, installed:** native appends `\unexpanded` and
  token-register results directly while scanning an expanded definition. The
  Script scanner previously lost that provenance and parsed literal parameter
  characters as the outer macro's parameters. The candidate in
  `expanded_parameter_candidate/` matches 10 of 11 focused native cases; a
  later malformed generated definition still stops with `tex-illegal-param`.
  An initial candidate leaked that provenance through the shared scanner for
  `\expanded`, breaking generated expl3 conditionals. The revised candidate
  confines it to actual definition scans: the upstream bootstrap prefix now
  defines `\cs_if_free:NF` identically to native and takes the same branch.
  The full preload then reports a Lambda stack overflow. With the normal stack,
  bounded runs reach line 8433 and 113,975 expansions without diagnostics, then
  overflow before 113,980 while generating a `\clist_get:NN` conditional
  variant. A diagnostic run with a 16 MiB process stack reaches line 9112 and
  130,000 expansions; this is evidence of excessive nested depth, not a fix.
  The unfinished 200,000-expansion diagnostic was stopped when the goal was
  cleared. See `expl3-native-preload-stack-trace-113975-jit.out` and
  `expl3-native-preload-budget-130000-stack16-jit.out`. A standalone map-carrying
  tail recursion reaches 150,000 iterations but overflows at 200,000 in JIT;
  the interpreter completes 300,000 and a 16 MiB diagnostic stack also lets
  JIT complete 300,000. A temporary instrumented engine counted 72,586 driver
  steps at the last safe preload point. An 80,000-line `\relax` input fails
  with E308 in the recursive Script driver in JIT but completes in the
  interpreter; an iterative local-state driver completes it in JIT and reaches
  expl3 line 8448 at 115,000 expansions on the normal stack. The local-loop
  driver and paired regression are installed. Seven staged neighboring goldens
  match; the 80,000-line case and native text match in both tiers. Budget-error
  and terminal-message behavior matches the prior driver in both tiers; a
  shorter version passes collection every 10,000 allocations with poisoning in
  both tiers. Three paired release samples retain identical output and native
  ASTs: baseline CPU 31.650/10.980/11.199 seconds, candidate
  20.393/12.542/11.247 seconds (`bench-iterative-driver.log`); the first pair
  ran under variable host load. The 992 pinned paths matched before install.
  Four installed-source GTests passed in JIT and interpreter. The interpreter
  batch worker was killed after the first two tests; the harness retried the
  two remaining tests in fresh workers, where both passed. The 80,000-line
  regression also passed alone in the interpreter
  (`iterative-driver-installed-interp-solo.log`). The full expl3 load and
  aggregate corpus remain unqualified (S1.4v2, S12.1.2v3).
  See `tail-state-probe-*.{out,err}` and
  `expl3-native-preload-run-count-jit.out`. A focused matrix covers 34 direct cases,
  12 nested-expansion cases, six AST comparisons and an included source in both
  constructor modes; all match in the interpreter and under scheduled GC with
  poisoning in both tiers. Six staged neighboring goldens pass in JIT. Three
  paired release samples preserve native AST and identical output: baseline CPU
  10.816/10.808/10.792 seconds, candidate 10.823/10.747/10.880 seconds
  (`bench-expanded-parameter.log`). The 991 pinned paths matched before
  installation. Four installed-source GTests pass in both interpreter and JIT
  (`expanded-parameter-installed-{interp,jit}.log`). Nested source echo and
  malformed generated-definition recovery have separate remaining gaps; this
  does not qualify the full corpus or overall performance (S1.8, D10.2.2).
- **Long source lines, installed:** recursive line-boundary, trailing-space and
  byte-count scans overflowed JIT on a long input line even after the driver
  was made iterative. Local loops now preserve those scans without consuming
  the native stack (S1.4v2, S12.1.2v3). A 560,000-character TeX line completes
  in JIT; the paired regression compares native text for 10,000 commands on one
  line and verifies 80,000 trailing spaces plus mixed Unicode and CR/LF byte
  offsets in both tiers. Short cases match the old reader in both tiers and
  pass collection every 10,000 allocations with poisoning. Seven staged
  neighboring goldens pass in JIT. Three release pairs preserve identical
  output and native ASTs: baseline CPU 11.015/10.977/10.968 seconds and
  candidate 10.975/10.994/10.929 seconds (`bench-iterative-reader.log`). The
  995 pinned paths matched before install; three installed-source GTests pass
  in both tiers (`iterative-reader-installed-{jit,interp}.log`). Full corpus
  qualification and other potentially deep recursive scanners remain open. The
  real bundled expl3 preload with both iterative fixes reaches line 33284 at
  472,271 expansions on the normal stack, then reports unsupported `setbox`;
  it is not a completed format (`expl3-native-preload-installed-500000-jit.out`).
- **Box registers, installed:** `temp/tex-script-f/box_register_candidate/`
  captures assigned horizontal and vertical boxes with hooks and scoped state,
  and implements register copy, consume, unboxing, split and `lastbox` behavior.
  Box consumption tracks locally saved slots: comparing box identity alone
  incorrectly emptied an outer slot when a local copy was consumed. The
  revised 62-case expansion comparison matches native in both tiers; the paired
  tree regression has 36 matching cases and two guarded replayed-rule source
  mappings. Fourteen focused cases pass forced GC with poisoning in both tiers,
  and seven neighboring staged goldens pass in JIT. Three release pairs retain
  identical output and native ASTs: baseline CPU 10.828/10.839/10.844 seconds,
  candidate 10.893/10.896/10.843 seconds (`bench-box-register.log`). The 1,006
  pinned paths matched before installation. The old unsupported-primitive
  placeholders now use still-unsupported `halign`; their golden changes affect
  only that primitive name. A staged preload reaches line 34057 and 477,338
  expansions, then stops at an unsupported `the` operand. A trace identifies
  `\g__codepoint_data_ior`, a `chardef` with value 0; native treats character
  definitions as integer quantities. The installed change reuses the integer
  `\the` path for character and math-character definitions. Thirty-six native
  text, message and diagnostic cases, eight AST cases and included files match
  in both tiers, including scheduled GC with poisoning. Six neighboring staged
  goldens pass in JIT. Three release pairs retain identical output and native
  ASTs: baseline CPU 11.033/11.158/10.985 seconds and candidate
  10.921/11.009/11.137 seconds (`bench-the-character.log`). The 1,005 pinned
  paths matched before installation. Four installed-source GTests pass in both
  tiers (`the-character-installed-{jit,interp}.log`). The
  550,000-expansion preload probe was stopped after several minutes without a
  result; later Unicode-data processing remains unqualified. Five installed-source
  GTests pass in both tiers (`box-register-installed-{jit,interp}.log`). Box
  dimensions, movement/shipout retrieval, replayed-rule source mapping and
  aggregate qualification remain open (S1.8, D10.2.2).
- **Captured stream decoding, installed:** each `\read` and `\readline`
  decoded the entire captured file even after a reader retained the decoded
  source and current position. The reader now decodes on the first read and
  reuses that source; reopening constructs a fresh reader. Mixed reads,
  reopening, native text/messages/diagnostics and lazy rejection of invalid
  UTF-8 match the old behavior in both tiers, including scheduled GC with
  poisoning. Seven staged neighboring goldens pass in JIT. Three alternating
  release pairs reading twelve lines from the actual 2,175,362-byte Unicode
  data file retain identical output and native equality: baseline CPU
  21.994/12.461/12.082 seconds, candidate 10.105/10.012/9.718 seconds
  (`bench-read-stream-unicode.log`; the first baseline ran under variable host
  load). The 1,027 pinned paths matched before installation; five installed-
  source GTests pass in both tiers (`read-stream-installed-{jit,interp}.log`).
  With the installed cache and `\the` fix, the pinned real expl3 preload reaches
  620,000 expansions in Unicode-data processing without diagnostics, then stops
  on the explicit budget. The captured stream is at line 219 of 40,116; this is
  forward progress, not format completion. See
  `expl3-native-preload-installed-{550000,620000}-jit.out` (the 550,000 run
  retained 167 unchanged pinned paths). S1.8, D10.2.2 and D11.2.1 apply.
- **Ignored input, installed:** `reader.next` recursively skipped comments and
  ignored tokens, causing JIT stack overflow on 20,000 consecutive comment
  lines. A local loop now reads the same input without growing the call stack
  (S1.4v2, S12.1.2v3). The long-comment regression matches native text, messages
  and diagnostics in both tiers; 72 token/state comparisons match the previous
  reader in both tiers, and a shorter case passes scheduled GC with poisoning.
  Reader, stream and reference goldens pass in staged JIT checks. One paired
  release sample preserved output and native AST, with baseline/candidate CPU
  10.940/10.865 seconds (`bench-token-reader-focused.log`). The 1,012 pinned
  paths matched before installation; three installed-source GTests pass in both
  tiers (`token-reader-installed-{jit,interp}.log`).
- **Inline stars:** 31 direct and generated cases plus included sources match
  native. Five selected GTests and sampled GC/poison pass in both tiers. In one
  JIT run a worker received signal 9 and its retry passed; the affected case
  subsequently passed alone. Two earlier timeouts overlapped GC. Three pinned
  release pairs preserved output and native AST (`bench-star-parity.log`).
- **Active definition targets:** 30 direct and nested cases and an included
  source match native; the following-group case remains guarded.
  Five GTests and sampled GC/poison pass in both tiers. Three pinned release
  pairs preserved output and native AST (`bench-active-target.log`).
- **Repeated math scripts:** 23 direct, generated and display cases and an
  included source match native; `x__2` is now covered by the punctuation
  increment below. Six GTests and sampled GC/poison pass in both tiers. Three pinned release pairs preserved
  output and native AST (`bench-repeated-math.log`).
- **Reader-normalized math:** direct math and echoed definitions now use the
  appropriate reconstructed or raw spelling. Twenty-four cases and an included
  source match native; the previously guarded echoed math is now covered by the
  punctuation increment below. Eight GTests and sampled GC/poison pass in both
  tiers. Three pinned full-reader release pairs
  preserved output and native AST (`bench-math-source-full.log`). These bounded
  timing samples do not qualify overall port performance. All four increments
  used `temp/tex-script-f/lambda-release`, SHA-256
  `bbc1e4d24f1d8d48228967d5eb5b2580f44707dadafb6fde4605d2c15acb0684`, for
  direct and GC checks. Logs use the `inline-star-*`, `active-definition-*`,
  `repeated-math-*` and `math-source-*` prefixes in `temp/tex-script-f/`.
- **Constructor sweep:** the frozen 141-script sweep finished with 133 matching
  recorded goldens, seven candidate changes and one shared stale golden. The
  parenthesis and package-provenance changes match native AST; the stale
  paragraph-quantity golden was corrected and passes both tier harnesses.
  Other changes, the expl3 assignment guard and corpus qualification remain
  open. Keep the default unchanged; see `constructor-default-v2-{sweep,resume}.log`.
- **Math punctuation:** the installed shared punctuation parser handles native
  unbraced script arguments and ordinary punctuation atoms, including paired
  relations. Forty-two direct, generated and operator cases, seven additional
  source-boundary cases, and an included file match native in both tiers on the
  pinned host. The prior `x__2` and
  echoed `$x^^2$` guards were replaced with native-equality cases. Three staged
  release pairs retained matching hashes and native ASTs; baseline CPU seconds
  were 10.960/11.054/11.314 and candidate 10.966/11.291/10.995
  (`bench-math-punctuation.log`). This is not overall performance qualification.
  Ten selected GTests pass in both tiers without retries; the expanded
  49-case regression also passed a separate JIT harness recheck and scheduled
  GC every 100,000 allocations with freed-memory poisoning in both tiers; see
  `temp/tex-script-f/math-punctuation-*` (S1.8, D10.2.2).
- **Definition paragraphs:** the installed partial-definition guard now scans
  outer breaks and lets the nested inline parser handle paragraphs inside groups.
  Eighteen direct cases, seven related forms and an included file matched native
  in both tiers on the pinned host; a following-group case remains guarded.
  Three release pairs on ordinary active definitions preserved output and native
  AST (`bench-definition-paragraph.log`). Eight GTests pass without retries in
  both tiers, and the new regression passes scheduled GC every 100,000
  allocations with freed-memory poisoning in both tiers (S1.8, D10.2.2).
- **Unexpanded source identity:** native `back_list_copy` stores scanned
  `\unexpanded` tokens and clears their direct-source flag. Script now reuses
  `stored_tokens` for replay outside a token-text scan. Fifteen direct cases
  and an included source match native expansion text, diagnostics and AST;
  one generated generic-command case remains guarded. Six neighboring GTests
  pass in both tiers and the new comparison passes scheduled GC every 100,000
  allocations with poisoning in both tiers. Three interleaved release pairs
  preserved output and native AST; CPU seconds were 10.723/10.954/11.426
  before and 11.093/10.967/12.042 after (`temp/tex-script-f/bench-unexpanded.log`).
  These are bounded samples, not overall performance qualification (S1.8,
  D10.2.2). The aggregate runs recorded below predate this increment.
- **Constructor scanner and token-list arguments:** in explicit constructor
  mode, the installed digester joins and splits native ASCII command words at
  reconstructed byte boundaries, handles the qualified literal escape forms,
  and maps expanded or normalized generic arguments from reconstructed bytes.
  The native-shaped `tl_put_right:Nn` and `tl_gput_right:Nn` wrappers now expand
  in argument mode without executing the assignment. Three installed goldens
  cover 36 direct names, 30 generated names, 12 token-list cases and an included
  source; all compare native text, diagnostics and AST in interpreter and JIT.
  Unicode control names remain guarded because native can split UTF-8 (Input
  LI-064); unqualified assignments and lexical cases remain guarded. The 64-case
  direct-name staging matrix matched native in both tiers and the generated
  matrix matched 30 ASCII cases with four Unicode guards. Ten selected existing
  JIT goldens passed against the staged candidate after correcting a stale
  relative test-helper import. The direct-name and token-list tests passed
  scheduled GC every 100,000 allocations with poisoning in both tiers. Three
  interleaved release samples on ordinary formatting preserved output and native
  AST; CPU seconds were 10.876/10.756/10.675 before and 10.888/11.088/10.817
  after (`bench-constructor-relex.log`), a bounded sample only. Logs
  use `constructor-relex-*` and `direct-word-*` under `temp/tex-script-f/`
  (S1.8, D10.2.2). These checks do not qualify the full corpus.
- **Following-group investigation:** a staged candidate matches direct
  active-target cases but exposes an expansion difference inside formatting
  arguments when constructor mode is off; the installed guard remains. Do not
  enable constructor mode by default until its source mapping and corpus gates
  pass.
- **Sequence wrapper probe:** a staged attempt to expand the native-shaped
  `seq_put_right:Nn` wrapper inside constructor arguments did not match native
  text or diagnostics in seven cases (`constructor-seq-matrix-jit.out`). It
  remains uninstalled and the existing explicit unsupported guard remains.
- **Expl3 Nx variants:** the six supported `Nx` token-list variants are now
  generated as the protected native-shaped `exp_args:NNx` macros, sharing the
  existing variant-name table. The `use:x` helper, its scratch token list and
  `cs_set_nopar:Npx` alias use the bundled kernel definitions. This supports
  constructor arguments and preserves native macro meaning, `\ifx`, aliases,
  scope and normal assignments. The two new goldens cover 29 text/diagnostic/
  AST cases, an included file, 24 scope/alias cases and eight `\show` results;
  four selected installed GTests pass in both tiers. Eight earlier neighboring
  goldens matched the staged implementation in JIT. The older constructor
  golden was updated only where the former guard now matches native. Both new
  tests pass scheduled GC every 100,000 allocations with freed-memory
  poisoning in both tiers. Three interleaved release pairs for the final
  representation retained native AST and identical output: baseline CPU
  seconds were 15.064/15.269/15.195 and candidate 15.043/15.225/15.139
  (`bench-constructor-nx-full.log`), a bounded sample. S1.8 and D10.2.2 apply.
- **Expl3 scratch sequences:** initialize the four kernel-defined local and
  global scratch sequences as empty sequence values, using the shared scratch
  initialization helper. Twenty-one expansion cases and six document-tree
  cases match native, as does an included-file case; duplicate declaration
  still stops with `expl3-already-defined` where native emits and recovers from
  a diagnostic, and four nonempty-sequence AST cases retain their mapping
  guard. Three installed GTests pass in both tiers; the new test passes
  scheduled GC every 100,000 allocations with poisoning in both tiers. Eight
  neighboring JIT goldens match the staged candidate. Three interleaved release
  pairs retained identical output and native AST; baseline CPU seconds were
  15.128/15.083/15.004 and candidate 15.008/15.133/14.997
  (`bench-expl3-scratch.log`), a bounded sample (S1.8, D10.2.2).
- **LaTeX aggregate interrupted:** the fresh 269-script JIT selection used two
  workers and the standard batch timeout, with no concurrent test suite. Its
  source and host manifest is `temp/tex-script-f/latex-suite-current/source-manifest.json`
  and log is `temp/tex-script-f/latex-suite-current/jit.log`. At 115 completed
  records, it had reported 60-second timeouts in `test_tex_script_digest`,
  `test_tex_script_expl3_bool` and `test_tex_script_expl3_tl_string`, and two
  batch-worker crashes (bus error and segmentation fault). The already-failing
  run was stopped to investigate timeout recovery; there is no aggregate result
  and missing records were not retried. Preserve the original events and rerun
  the gate after addressing the timeout and worker failures.
- **Timeout recovery:** the next script after a one-second timeout hung on the
  MIR code-generation mutex; sampling the debug host showed that the signal
  jump bypassed the lock destructor. `test-batch` now reports 124 using
  signal-safe writes and exits the interrupted worker; the existing harness
  retries unstarted records. The runtime regression and an end-to-end 60-second
  harness probe pass, keeping the intentional timeout as a failure. The three
  LaTeX timeout cases then passed individually in one-worker JIT on the debug
  host in 44.7, 32.0 and 30.5 seconds; this does not erase the aggregate
  failures. The broader runtime issue remains in
  [LR01-24](../Lambda_Issue_Ledger.md#lr01-24) (D6.3.6, D6.4.3).
- **Prior LaTeX failures:** all ten remaining cases from the earlier 12-case
  failure list pass a one-worker JIT harness replay without retries; the other
  two (digester and inline formatting) passed separately. This is a focused
  replay, not a fresh aggregate or corpus gate
  (`temp/tex-script-f/prior-latex-failures-replay-jit.log`).
- **State:** production still uses the native engine. Script `f` expansion and
  the numeric noexpand scan fix are installed locally; their 11-case golden
  passed interpreter and JIT. The adjacent noexpand JIT test passed; numeric
  conditional timed out in the harness but matched directly in JIT. See the
  `expl3 token-list f-expanded head and tail` record for details. After the
  recovery change, the `f` and noexpand goldens both pass the JIT harness
  (`temp/tex-script-f/followup-regression-jit.log`).
- **Open gates:** reconstructed-output and source provenance, diagnostics and
  recovery, TeX/expl3 and document parity, corpus and forced-GC qualification,
  release performance, and coordinated formal and native-client migration.
- **Post-timeout-fix runtime gates:** the verified-snapshot Lambda baseline
  finished 7,532/7,549, with the same 17 failed cases as the prior run and the
  new timeout regression passing. The server failures report sandbox-denied
  localhost binds; the media-playback and Scene3D failures remain unresolved.
  Test262 then passed all 40,261 selected cases in the initial batches, with
  zero reported regressions. The timeout and ordinary failed-script regressions
  pass on both debug and release hosts. The baseline debug host SHA-256 was
  `5da3be17c2aa6ab36917c16e1dfcdc01d25507a34793c8932b8249abb3a6cfdf`;
  the Test262 release host SHA-256 was
  `0d58ab8b973c3c662980d8b14a4199203253ab4664a7d498931597371645a8ea`.
  Logs, changed-file manifest and matching sibling snapshot checks are under
  `temp/tex-port-baseline-20261116/` with the `timeout-fix` suffix. Generated
  build configurations were restored afterward. These runtime gates preceded
  the expl3 Nx increment above. The 269-script LaTeX run
  remains interrupted and must be repeated.
- **Latest aggregate validation:** with checksum-verified snapshots of the
  current dirty Input and Radiant source trees, `make test-lambda-baseline`
  finished 7,531/7,548. Three server tests reported sandbox-denied localhost
  binds; 12 sync/WebSocket cases, `dom_media_playback` and `scene3d_midtown`
  also failed. The media test stopped early and Scene3D differed from its
  golden; their causes were not established. The separate Test262 baseline
  reported zero regressions: 40,257/40,261 passed in batches and four regexp
  cases passed on isolated retry. No staged tests overlapped these runs.
  The debug host SHA-256 was
  `824ea083209ddcd1c54b5022046b7eace56832c49886f126734508fec74c2337`;
  Test262 rebuilt the release host, SHA-256
  `8eeff64dda3918d6b86d67087050b293cbbde2a929cfda9bcf68376b77abad8d`.
  Source hashes and sibling snapshots matched before and after the runs;
  logs and manifest are under `temp/tex-port-baseline-20261116/`. The
  generated configurations were restored to the normal sibling roots. The
  full LaTeX corpus gate remains open.
- **Earlier aggregate validation:** `make test-lambda-baseline` ran against
  checksum-verified snapshots of the current dirty Input and Radiant
  source checkouts under `temp/tex-script-f/baseline-src/`. Their HEADs at copy
  time were `2672c1ada7cabbd4144f21f97f15ee96a62b1b6f` and
  `9a21ad9ed4a43a7fb013e819fdef11826f031535`. The first fresh-cache attempt
  stopped before compilation because GitHub was unreachable; a retry uses the
  existing local pinned dependency cache. The retry finished 7,518/7,534,
  failing three server tests on sandbox-denied localhost binds, one JavaScript
  media-playback output comparison, and 12 sync/WebSocket script cases. Eight
  script cases report listener bind errors; the other four report unavailable
  WebRTC, failed Bonjour registration, a disk-monitor error, and a task-wait
  error. The media test stops before its completion marker; its cause remains
  unconfirmed. The complete report is
  `temp/tex-script-f/test-lambda-baseline-offline.log` and case details are in
  `test_output/`. The subsequent `make test262-baseline` run used the same
  snapshots and exited successfully with zero reported regressions: 40,257 of
  40,261 cases passed fully and four regexp cases passed only on retry. The
  staged LaTeX checks overlapped part of the run, so its load differs from an
  isolated run. The report is `temp/tex-script-f/test262-baseline.log`. The
  full LaTeX corpus gate remains open. Generated Premake and static-link
  configurations were restored to the normal sibling roots after the builds;
  `lambda.exe` is the snapshot-qualified release host from the Test262 gate.
- **Inline separated environments:** the installed digest handles whitespace
  between `\begin` and
  its name in constructor arguments. A synthetic whitespace token must retain
  the preceding token's sequence so a following paragraph newline is not
  duplicated. The staged 14-case edge matrix matches native in both tiers for
  12 cases and retains guards for two comment gaps; the included-environment
  matrix matches except the separately guarded paragraph-provenance case.
  Nine neighboring goldens passed in both tiers; the other two differed only
  in intentional native-matching cases. The previously installed
  environment-options increment had left a stale digester expectation, which
  is now updated after direct installed-source verification. The expanded
  inline regression agrees across tiers. Installed GTest checks passed 2/2 in
  each tier (`temp/tex-script-f/inlinegap-installed-{interp,jit}.log`), and
  the extended included-file fixture passed both tiers against native. Three
  interleaved release pairs produced identical hashes and native ASTs; CPU
  seconds were 10.582/10.635/10.524 before and 10.427/10.460/10.480 after
  (`temp/tex-script-f/bench-inlinegap-constructor.log`). These samples use the
  pinned release host and do not imply a causal speedup.
- **Broader LaTeX golden suite:** the JIT run of
  `AutoDiscovered/*latex_*` finished 230/242, with log
  `temp/tex-script-f/latex-golden-suite-jit.log`. Five cases hit the
  60-second per-case timeout under parallel load: `expl3_cs_exist_c`,
  `inline_without_group`, `random`, `digest`, and `numeric_math`. All five
  passed a subsequent one-worker JIT run with the same 60-second limit
  (`latex-timeout-isolated-jit.log`); the broad retry process was still active,
  so this was lower concurrency, not an idle machine. Three batch workers also crashed
  (one bus error and two segmentation faults); the existing harness retries
  missing records individually. Six other cases reported output mismatches and
  `pdfstrings` was charged for an abnormal worker exit after its last record;
  individual replays completed; `pdfstrings` passed alone. Subsequent focused
  native comparisons led to fixes and corrected stale goldens, but do not
  establish that the aggregate run passes.
  All three crashed workers' last
  reported results were timeouts; a short-timeout reproduction using the
  pinned release host completed without crashing. Six further 1- and 5-second
  replays of the affected pairs on the actual suite host also completed without
  crashing; they do not reproduce the 60-second execution point. The root
  cause remains unproven (`batch-timeout-repro.*`, `batch-timeout-digest.*`,
  `repro-batch-timeouts.log`). Staged candidate
  checks overlapped the run, and the sandbox denied their requested lower process
  priority; account for that load when interpreting the timeouts.
  This exercises native and Script tests and is not by itself a complete
  parity or corpus gate.
- **Included paragraphs:** the installed digest accepts reconstructed
  blank-line `par` tokens whose spelling is a line ending, and distinguishes
  an expanded explicit `\par` using its emitted spelling when the original
  source span is indirect. A second source-boundary correction avoids replaying
  blanks before a comment after explicit `\par`. All 23 included-file cases
  match native in both tiers after the final change. Six direct-source comment
  cases and a combined included-file fixture also match in both tiers
  (`inlineparagraph-main-comment-{interp,jit}.out` and
  `inlineparagraph-foreign-{interp,jit}.out`). Eleven neighboring goldens
  passed in both tiers (`inlineparagraph-goldens-{interp,jit}.log`). The
  included-file fixture also passed in both tiers with collection scheduled
  every 100,000 allocations and freed-memory poisoning enabled
  (`inlineparagraph-gc-{interp,jit}.out`). The permanent included-file fixture
  and additional direct comment cases passed the installed checks in both
  tiers (`combined-installed-{interp,jit}.log`).
- **Native command-byte investigation:** the native LaTeX scanner consumes a
  single byte after a backslash when the next byte is not an ASCII name
  character. For the included `\def\é{Q}` fixture, both its target and
  argument descriptor contain bytes `5C C3`, leaving the `A9` continuation in
  the remaining text (`native-unicode-bytes*.out`). Ordinary `utf8.decode`
  rejects that truncated sequence, while the existing data parser
  `parse(bytes, {type: "text"})` preserves the exact bytes in a string
  (`native-invalid-string-parse.out`). S2.5.8 defines sequence operations on
  text as code points, so byte positions must remain explicit here. This is
  not a decision to alter the Input parser's behavior. A staged scanner at
  `temp/tex-script-f/native_latex_scanner.ls` matches the actual Input scanner
  in 564 deterministic C-locale cases in interpreter and JIT, including all
  first-byte values, NULs, long names, offsets and output-buffer truncation
  (`native-scanner-matrix-{interp,jit}.out`). The independent native oracle
  is compiled from the checksum-verified Input snapshot; its generator is
  `generate-native-scanner-matrix.py`. The primitive is not yet integrated
  into digestion. The native scanner's locale dependence and partial UTF-8
  output are recorded with the Input owner as
  [LI-064](../Lib_Input_Issue_Ledger.md#li-064).
  The Input owner scanner now uses its existing ASCII-letter helper, preserving
  C-locale results and making classification independent of the host locale.
  Its new native GTest fails on the original scanner and passes on the fix;
  all 14 TeX engine tests pass against the candidate archive built from the
  recorded Input snapshot, and installed source and test match that candidate.
  The 564-case, three-locale comparison also passes. This is a focused
  owner-side check, not the full Input baseline. Partial UTF-8 output remains
  open in LI-064; the completed LaTeX suite used the earlier snapshot-built
  executable. A fresh checksum-verified copy of the current
  Input source, including the locale fix and its dirty owner changes, ran
  `make test-input-baseline`. It failed on sandbox-denied localhost servers
  and the existing [LI-002](../Lib_Input_Issue_Ledger.md#li-002)
  external-parent admission test. The TeX engine portion passed 12 and skipped
  two expl3 cases; the separate resource-enabled run passed all 14. The full
  output is `temp/tex-script-f/input-locale-baseline.log`.
- **Generated document-end tail:** the broad suite exposed a real AST mismatch
  in `test_tex_script_definition_end`. Native `finish_document` appends the
  root tail only when its main reader is the active input frame; the Script
  engine previously ignored exhausted token frames and end-hook frames. The
  installed combined change records that condition when scheduling the
  document close. Seven supported
  direct cases and four included-file cases match native text and AST in both
  tiers; the expanded direct matrix has 19 text matches, 16 AST matches and
  three cases still unsupported in digestion. The unchanged definition-end and
  trailing-source goldens pass
  in both tiers (`definition-end-combined-*`, `trailing-source-combined-*`,
  `tail-origin-{candidate,foreign}-*.out`). The new document-tail and
  group-recovery goldens, existing related goldens, and corrected stale
  native-equality expectations passed 10/10 in both tiers
  (`combined-installed-{interp,jit}.log`).
  Three release pairs on the pinned host matched output hashes and native ASTs;
  baseline CPU seconds were 10.402/10.232/10.238 and candidate seconds were
  10.278/10.486/10.208 (`bench-combined.log`).
- **Generated declarations:** D10.2.2 requires the same source and resources
  to produce one document tree. The installed digester maps proven raw echo
  and synthesized declaration spans through reconstructed output, including
  a macro that vanishes between argument braces. A 75-case matrix has no text
  or AST mismatches in either tier; nine raw-option cases remain explicitly
  guarded (`declaration-cartesian-empty-{interp,jit}.out`). The new
  `test_tex_script_generated_declarations` compares direct, macro-generated,
  empty and local class/package cases with native; its one ambiguous raw-option
  case retains the guard. The counters fixture advances from declaration
  mapping to `math source mapping`. Its golden and the blank-line declaration
  golden record their current guard reasons. Earlier raw-stage neighboring
  goldens passed except those two changed reasons; the final empty-macro stage
  is being checked in `declaration-empty-goldens-{interp,jit}.log`.
- **Generated math and paragraphs:** the installed Script digester maps
  byte-faithful math tokens through reconstructed output and checks native
  lexical categories. It classifies expanded `\par` by emitted spelling,
  skips whitespace consumed by native after it, and retains trailing whitespace
  in a raw declaration echo. The counters fixture matches native expanded
  text and the entire AST in both tiers with no adapters and also with the
  production adapter registry and constructor-argument mode
  (`counters-both-modes-{interp,jit}.out`). The profile's default-adapter run
  without constructor mode still guards `command source mapping` at generated
  `\textcolor` and is not equivalent to native input without adapter options. The 12-case expanded
  paragraph matrix matches both tiers, and 28 generated math boundary and
  whitespace cases plus three cross-file cases matched in the earlier staged
  matrix. Altered `^`/`_` category codes remain explicitly guarded. An illegal
  macro parameter exposes a separate native recovery difference; `$$` from an
  empty expansion is rejected by native as an unclosed display. The focused
  regression tests are `test_tex_script_generated_math`,
  `test_tex_script_generated_paragraph`, and
  `test_tex_script_counters_native_fixture`, all with paired goldens. The
  installed-source checks passed for six related scripts in both tiers; the
  counters regression was subsequently strengthened to test both adapter modes
  and passed separately (`counters-both-modes-{interp,jit}.out`). The staged
  neighboring checks passed 12/12 in both tiers
  (`scan-neighbors-{interp,jit}.log`). The existing math-raw golden now records
  native equality for its formerly guarded case. The three new math,
  paragraph and counters tests passed in both tiers with collection scheduled
  every 100,000 allocations and freed-memory poisoning enabled
  (`gc-installed-*-{interp,jit}.out`). Four focused GTests passed in each
  tier on the snapshot-qualified `lambda.exe`
  (`installed-front-end-gtest-{interp,jit}.log`). Three interleaved release
  pairs on 30 direct-math paragraphs preserved exact output and native AST
  equality: baseline CPU seconds 10.541/10.478/10.360, installed candidate
  10.603/10.655/10.530 (`bench-math-installed.log`). The candidate's mean
  is approximately 1.3% higher in this workload; this does not qualify overall
  port performance. The pinned release host SHA-256 is recorded in that log.
- **Illegal macro parameters:** the installed Script helper now recovers
  literal invalid parameters for direct-reader primitive definitions and
  LaTeX command definitions, preserving their distinct native diagnostic
  positions and definition-order rules. The eight-case native comparison,
  21 of 23 extended cases, all 11 command-order cases, six document AST cases
  and an included-file comparison matched text and exact diagnostics in both
  tiers. An 18-case provenance probe had no silent mismatches and four still-
  fatal token-stream or expanded definitions; a separate parameter-order error
  also remains fatal. The paired `test_tex_script_illegal_parameter_recovery`
  golden matches the installed candidate in both tiers. Eight neighboring
  goldens passed both tiers on the preceding candidate. Three interleaved
  definition-workload release pairs preserved output and native AST equality:
  baseline CPU seconds 10.343/10.498/10.366 and candidate
  10.363/10.343/10.344 (`bench-illegal-param.log`); this does not qualify
  overall port performance. Five installed-source goldens passed in both tiers;
  the recovery regression also passed GTest and sampled forced-GC in both
  tiers (`illegal-param-installed-gtest-{interp,jit}.log`,
  `illegal-param-installed-gc-{interp,jit}.out`). The subsequent installed
  increment recovers `\edef` / `\xdef` only when the complete reader
  sequence proves no token expansion occurred. Its 12-case matrix has eight
  native matches and four explicit failures; six AST cases and included-file
  text, diagnostics and ASTs match in both tiers. A 10-case diagnostic-order
  probe has no silent mismatches and four existing failures. The expanded
  permanent fixture records a changed-token-stream failure explicitly. Four
  neighboring checks passed both tiers (`expanded-param-goldens-{interp,jit}.log`).
  Three interleaved release `\edef` pairs preserved native text and AST
  equality: baseline CPU seconds 10.936/12.626/12.060 and candidate
  11.072/11.205/12.337 (`bench-edef-expanded.log`); the spread prevents a clear
  performance conclusion. Three installed-source goldens and the recovery
  forced-GC check passed in both tiers (`expanded-param-installed-*`).
- **Parameter-order recovery:** the installed raw signature scanner records
  an incorrect parameter number, pushes its lookahead back as a delimiter,
  and continues. After nine parameters it diagnoses and consumes the extra
  token. Captured stream states preserve each diagnostic location. Seventeen
  of 19 staged edge cases matched native before zero-parameter brace support;
  the remaining runaway-argument recovery is still open. The brace form now
  matches ten text/diagnostic cases in both tiers, and two valid document AST
  cases also match; another is rejected by both document parsers as an
  unclosed group. Nine nested or included-source provenance cases match native.
  The paired `test_tex_script_parameter_order_recovery` covers text,
  diagnostics, document AST and an included file. Nine neighboring goldens
  passed both tiers (`parameter-brace-goldens-{interp,jit}.log`). Three
  interleaved release pairs preserved text and native AST equality: baseline
  CPU seconds 11.116/12.188/12.288 and candidate 11.071/12.617/12.209
  (`bench-parameter-order.log`); the spread prevents a clear performance
  conclusion. Installed-source and sampled forced-GC outputs match the goldens
  in both tiers (`parameter-order-installed-*`); the focused GTest passed in
  both tiers (`parameter-order-installed-gtest-{interp,jit}.log`).
- **Runaway macro arguments:** the `span_macro_runaway_customenv` changes are
  installed in the package. Five focused GTests pass in each tier on the
  snapshot host (`macro-customenv-installed-gtest-{interp,jit}.log`). Both new
  regression scripts also match their goldens in each tier under forced GC
  and freed-memory poisoning (`test_tex_script_*_recovery-installed-gc-*`).
  Native treats a missing undelimited argument at EOF quietly, diagnoses EOF
  inside a group or delimiter, and diagnoses paragraph or extra-brace errors
  before returning the triggering token to the input. The candidate carries
  these causes through the stream scanner, uses the actual call name, and
  continues after failure. The first ten cases and 22 of 23 initial extended
  cases matched text and diagnostic records; the remaining long-name case now
  matches after native's 126-byte name truncation, including a split UTF-8
  character. Nine ASCII lengths and three Unicode boundary cases match even
  as binary message bytes (`macro-runaway-long-bytes-{interp,jit}.out`). An included-file
  comparison matches. The constructor-mode expl3 path also passes the actual
  alias token to error reporting; four focused comparisons match in both tiers
  (`macro-runaway-expl-alias-{interp,jit}.out`). Recovery exposed an older missing `latex-env-mismatch`
  diagnostic record and an ordinary-mode difference for environment arguments.
  The candidate records that diagnostic and processes passthrough arguments as
  native does. The 40-case argument matrix matches text and diagnostics in both
  modes and tiers; eight of ten document AST cases match, one remains guarded,
  and native rejects the other as an unterminated group
  (`environment-arguments-ast-audit.out`).
  Processing these arguments also exposed a custom-environment regression;
  the staged implementation expands their begin and end macros in an explicit
  environment scope. Twenty-four nested, optional-argument and scope cases match
  native text and diagnostics, and ten document AST comparisons match, in both
  tiers (`environment-custom-matrix-*`, `environment-custom-ast-both-*`).
  The split macro and environment regression scripts match native in both
  tiers, including included-file locations and aliased expl3 setters
  (`test_tex_script_*_recovery-candidate-{interp,jit}.out`). The expansion
  golden has three native-verified recovery changes; the inline-environment and
  constructor-mode goldens each gain a supported native match. Seven other
  neighboring goldens pass in both tiers (`runaway-customenv-goldens-*.log`).
  A further malformed custom-environment probe has eight native matches and
  four explicit stops across both modes, with no silent mismatch
  (`customenv-recovery-edges-interp.out`); the later constructor and optional
  argument fixes resolve these stops (`optional-recovery-edges-*.out`).
  Three interleaved pinned-release pairs all produced the same output hash and
  native text/AST match. Baseline CPU seconds were 11.006, 11.032 and 10.982;
  candidate seconds were 11.123, 10.910 and 10.962 for this one workload
  (`bench-envargs-customenv.log`).
- **Truncated constructor arguments:** the changes from
  `span_constructor_truncated` are installed. Native returns accumulated constructor-argument
  tokens at EOF and terminates a malformed bracket argument after an unmatched
  closing brace. The candidate does the same; it resolves the two EOF stops in
  `constructor-truncated-edges-{interp,jit}.out`. Two malformed optional
  argument cases initially stopped earlier in macro scanning and were fixed in
  the subsequent optional-argument increment. A 42-case
  direct-command and environment matrix has three diagnostic mismatches in
  ordinary mode for unmatched braces in command brackets; constructor mode
  matches (`constructor-truncated-matrix-{interp,jit}.out`). The installed
  baseline has exactly those same three mismatches; the candidate improves 19
  cases without introducing a new matrix difference
  (`constructor-baseline-matrix-interp.out`). The paired focused regression
  matches in both tiers, and five neighboring goldens pass in both tiers
  (`test_tex_script_*-truncated-neighbor-*.out`).
- **Optional macro arguments:** the changes from `span_optional_recovery` are
  installed. Native scans these arguments using a synthetic `\relax` call;
  its EOF or extra-brace error discards the macro call and returns lookahead.
  Twelve custom-environment edge comparisons and 32 standalone, alias,
  constructor and environment comparisons match native text and complete
  diagnostic records in both tiers (`optional-recovery-{edges,matrix}-*.out`).
  An incomplete included-file argument also matches both the output and the
  native diagnostic offset (`optional-recovery-foreign-*.out`). The paired
  regression matches native in both tiers, including full diagnostic records
  (`test_tex_script_optional_argument_recovery-candidate2-*.out`), and the
  truncated-argument regression also passes on the combined candidate. Five
  neighboring goldens pass in each tier (`test_tex_script_*-optional-neighbor-*.out`).
  Three pinned-release pairs have identical output hashes and native text/AST
  checks. Baseline CPU seconds were 10.949, 14.139 and 12.120; candidate
  seconds were 10.880, 10.905 and 12.388. The spread prevents a performance
  conclusion (`bench-optional-recovery.log`). Three installed-source GTests
  pass in each tier (`optional-truncated-installed-gtest-{interp,jit}.log`).
  Both new scripts also match their goldens in each tier under forced GC and
  freed-memory poisoning (`test_tex_script_*_recovery-installed-gc-*` and
  `test_tex_script_constructor_truncated-installed-gc-*`).
- **Constructor mode default:** `span_constructor_default` is an exploratory
  candidate only. It enables constructor argument scanning when the option is
  absent and retains explicit `false`. Eighteen of 19 direct native text and
  diagnostic comparisons match in both tiers; the raw `\verb` argument stops
  explicitly as unsupported (`constructor-default-matrix-{interp,jit}.out`).
  Four explicit-`false` probes preserve installed text and diagnostics in both
  tiers (`constructor-default-legacy-{interp,jit}.out`).
  Seven of eight sampled existing goldens remain unchanged in the interpreter;
  the counters fixture improves from `command source mapping` to a successful
  digest (`constructor-default-goldens-interp.log`; its native comparison with
  production adapters and constructor mode was checked separately above).
  A wider JIT sweep of 15 selected features has 14 unchanged goldens and one
  changed paragraph-assignment result (`constructor-default-sweep.log`). The
  changed case matches native text, diagnostics and AST in both tiers
  (`constructor-default-paragraph-ast-{interp,jit}.out`).
  After the subsequent raw-inline, tree and source-boundary work, that snapshot
  is stale. `span_constructor_default_v2` is based on the current installed
  engine and matches all 19 direct text/diagnostic comparisons in both tiers,
  including the previously guarded `\verb` case
  (`constructor-default-v2-matrix-*.out`). Four explicit-legacy probes preserve
  installed behavior in both tiers (`constructor-default-v2-legacy-*.out`). A
  141-script JIT sweep of default-mode scripts was interrupted after 105 rows
  when the goal was cleared. It used frozen baseline and candidate modules and a source/golden manifest
  (`constructor-default-v2-sweep/manifest.json`). The two snapshots differ
  only in the default option. Investigate all changed or failed rows and
  qualify broader corpus coverage before any default change.
  The sweep has identified the previously checked counters improvement, two
  font-assignment cases whose candidate ASTs match native, and an expl3
  token-list assignment that stops explicitly inside a constructor. A staged
  native-shaped expansion of that token-list wrapper matches TeX text but
  produces a different LaTeX tree: native reparses the emitted control name
  while the Script digest retains its original token identity. The prototype
  remains uninstalled (`constructor-tl-put-detail-{interp,jit}.out`); its
  explicit guard must remain until the reconstructed lexical boundary is fixed.
  The font-character sweep also exposed a stale baseline golden for a math
  quantity: the installed engine matches native text, diagnostics and AST in
  both tiers (`font-characters-stale-golden-check-*.out`); its golden is updated.
  Do not promote until all changed behavior, package/corpus coverage and the
  explicit legacy option are qualified.
- **Verbatim inside constructor arguments:** the isolated
  `span_constructor_verb` change is installed. It reuses the existing raw verbatim reader within the
  constructor loop. Nine direct text/diagnostic cases match native in both
  tiers, including brace delimiters; three generated or malformed cases stop
  explicitly (`constructor-verb-matrix-{interp,jit}.out`). The document-tree
  probe still stops at the existing raw-inline source-mapping guard for all
  three cases; native rejects one of those trees
  (`constructor-verb-ast-{interp,jit}.out`). An included-file expansion matches
  native in both tiers (`constructor-verb-foreign-*.out`); a paired regression
  matches, and four of five neighboring goldens are unchanged in both tiers;
  the fifth now has a native-matching expansion
  (`constructor-verb-goldens-{interp,jit}.log`). Three pinned-release pairs
  have identical output hashes and native text/AST checks; baseline CPU seconds
  were 11.066, 11.091, 10.975 and candidate seconds were 11.065, 10.980,
  11.021 (`bench-constructor-verb.log`). Two installed-source GTests pass in
  each tier (`constructor-verb-installed-gtest-{interp,jit}.log`), and the new
  test matches its golden with forced GC and freed-memory poisoning in both
  tiers (`test_tex_script_constructor_verb-installed-gc-*`). Removing the digest guard in a
  diagnostic-only copy reaches an explicit unsupported `verb` branch: native
  creates a `verb` element with `raw_source`, optional `starred` and mapped
  `source_offset` (`raw-inline-ast-detail-interp.out`). The following increment
  implements this node for the qualified cases; ambiguous raw spans and other
  raw commands remain guarded.
- **Verbatim document trees:** the follow-up `span_raw_verb_relex_final` changes
  are installed. Two installed-source GTests pass in both tiers
  (`raw-verb-relex-installed-gtest-{interp,jit}.log`); both new scripts also
  match their goldens under forced GC and freed-memory poisoning in both tiers
  (`test_tex_script_{verb_digest,escaped_text_digest}-installed-gc-*`).
  The initial `span_raw_verb_tree` prototype matched two native trees but
  silently accepted a group that native rejects. The current experiment reuses
  a generalized byte group scanner to detect raw spans that could change an
  enclosing native group; it now explicitly guards that case and still matches
  the two valid trees (`raw-verb-tree-ast-safe-*.out`). A wider 18-case matrix
  has fourteen native AST matches and four explicit guards, three of which
  native itself rejects (`raw-verb-tree-matrix-{interp,jit}.out`). An included
  Unicode verbatim span also matches native text, diagnostics and mapped AST
  offsets (`raw-verb-tree-foreign-{interp,jit}.out`). A further deterministic
  54-case audit of raw bodies, nesting, Unicode and generated prefixes has 42
  native matches and 12 explicit guards (six native parses reject), with no
  silent mismatch in either tier (`raw-verb-tree-audit-{interp,jit}.out`). A
  paired regression and six neighboring goldens pass in both tiers
  (`raw-tree-neighbors-*.log`). A later audit found that an alias whose name
  begins with `verb` must be distinguished from the actual command; the staged
  code now checks the token name. A subsequent generated-character boundary
  audit found three silent AST mismatches (`raw-verb-boundary-audit-*.out`), so
  this version must not be promoted. Reconstruction emits `\textbackslash{}`
  for `\char92`; the direct-token digester treated it as literal text. The
  follow-up `span_raw_verb_relex` prototype maps canonical text-backslash,
  text-ascii-circumflex and text-ascii-tilde commands back to native nodes and
  source offsets. Eighteen generated-character cases match native text,
  diagnostics and AST in both tiers, and an included fixture matches mapped
  offsets (`escaped-text-{matrix,foreign}-*.out`). Nine
  of ten verbatim boundary cases now match; one changed-catcode form remains
  explicitly guarded (`raw-verb-relex-boundary-v2-*.out`). The 54-case verbatim
  audit again has 42 native matches, twelve guards and no silent mismatch in
  either tier (`raw-verb-relex-audit-*.out`). Two draft regressions and eight
  neighboring goldens pass in each tier (`relex-neighbors-{interp,jit}.log`).
  Explicit `constructor_argument_mode: false` still executes `\char` in
  formatting arguments; the installed and
  candidate expansion outputs agree with each other and both differ from
  native for the three escaped characters (`escaped-legacy-baseline-*.out`).
  The draft regression records that legacy difference separately.
  Both paired regressions match their expected results in both tiers, with the
  guarded cases and the legacy-mode difference explicit. Three interleaved
  pinned-release list-scanner pairs have identical output hashes and native
  text/AST checks; baseline CPU seconds were 11.081, 10.991, 12.630 and
  candidate seconds were 11.032, 11.012, 12.135
  (`bench-raw-verb-relex-final.log`).
- **Top-level verbatim group context:** the `span_raw_verb_group_context`
  changes are installed. Native scans verbatim as raw text at paragraph level;
  nested groups first scan their contents as group bytes. The candidate only
  applies the group-byte guard in nested contexts. A 54-case audit has 48
  native AST matches and six explicit guards, all six rejected by native. A
  separate 15-case environment, heading and table matrix has nine matches and
  six such guards; neither matrix has silent mismatches in either tier
  (`raw-verb-context-{audit,matrix-v2}-*.out`). The revised regression and five
  neighboring goldens pass in both tiers, and generated-character boundary
  checks remain unchanged (`context-neighbors-*.log`,
  `raw-verb-context-boundary-*.out`). Three interleaved pinned-release pairs
  on a verbatim workload have identical output hashes and native text/AST
  checks. Baseline CPU seconds were 10.838, 11.052, 13.259; candidate seconds
  were 11.001, 11.051, 11.962 (`bench-verb-context-final.log`), with too much
  spread for a performance conclusion. Two installed-source GTests pass in
  both tiers (`verb-context-installed-gtest-{interp,jit}.log`); the revised
  regression also matches in both tiers with forced GC and freed-memory
  poisoning (`test_tex_script_verb_digest-context-gc-*`).
- **Listings and minted inline capture:** the `span_raw_inline_capture` changes
  are installed. They extend the shared raw group reader with the
  native inline option, language, braced and delimiter rules, and reuses that
  capture from both ordinary and constructor expansion. A 42-case native
  expansion and diagnostic matrix has 40 native matches and two explicit
  unsupported Unicode-delimiter cases in both tiers
  (`raw-inline-capture-matrix-{interp,jit}.out`). A further 48 edge comparisons
  and an included-file case match native in both tiers
  (`raw-inline-capture-{edges,foreign}-*.out`). Document tree handling remains
  guarded. A native byte probe confirms that a non-ASCII delimiter can split a
  UTF-8 sequence and emit noncanonical bytes (`native-raw-unicode-delimiter.out`);
  the Script reader cannot represent that position and continues to guard it.
  The paired draft regression and seven other neighboring goldens pass
  in both tiers; `test_tex_script_verb` gains the native-verified `\lstinline`
  expansion (`raw-capture-neighbors-*.log`,
  `test_tex_script_verb-raw-capture-current-*.out`). Three interleaved pinned
  release pairs on the existing raw-reader path have identical text and
  diagnostic results and hashes. Baseline CPU seconds were 9.753, 9.775,
  9.875; candidate seconds were 9.772, 9.811, 11.471. The spread prevents a
  performance conclusion (`bench-raw-inline-final.log`). Three installed-source
  GTests pass in both tiers (`raw-inline-installed-gtest-{interp,jit}.log`);
  the new regression matches its golden under forced GC and freed-memory
  poisoning in both tiers (`test_tex_script_raw_inline_capture-installed-gc-*`).
- **Listings and minted document trees:** the `span_raw_inline_foreign_final`
  changes are installed. They build native-shaped code nodes from reconstructed raw
  spans, including stars, options, language and mapped offsets. An initial
  36-case audit exposed a silent acceptance of a percent-containing span that
  native rejects: native's environment scan skips `\verb`, but not these
  commands. The candidate now guards raw spans that can affect enclosing
  groups, comments or environment boundaries. A broader 48-case comparison is
  clean of silent mismatches: 28 native trees match and 20 cases stop
  explicitly (`raw-inline-tree-matrix-v2-{interp,jit}.out`). The follow-up
  `span_raw_inline_tree_context` applies group scanning according to context,
  while separately protecting native environment scans. Its 48-case matrix has
  30 native matches and 18 guards in both tiers. A 32-case boundary matrix and
  12 mixed-command cases have no silent mismatch; the first included-file
  probe then found duplicate text after a raw span. The separately installed
  source-boundary fix repairs that case. A subsequent 80-case audit has 60
  native matches and 20 explicit guards (all 20 native parses reject), with no
  silent mismatches in either tier (`raw-inline-tree-audit-*.out`). A further
  nine included-file cases have five native matches and four explicit guards,
  with no silent mismatch (`raw-inline-tree-fixtures-*.out`). The paired draft
  regression and eight neighboring tests pass in both tiers
  (`inline-tree-neighbors-*.log`). Three pinned-release pairs on the common
  verbatim workload have identical native-checked output. Baseline CPU seconds
  were 11.037, 12.196, 12.113; candidate seconds were 10.959, 14.218, 11.348.
  The spread prevents a performance conclusion (`bench-inline-tree-final.log`).
  Three installed-source GTests pass in both tiers
  (`inline-tree-installed-gtest-{interp,jit}.log`); the new regression also
  matches its golden under forced GC and freed-memory poisoning in both tiers
  (`test_tex_script_raw_inline_digest-installed-gc-*`).
- **Included raw-span paragraph boundary:** native and Script expansion agree
  for an included `\verb|猫|` followed by a newline, but the previous
  digester duplicated the raw body and returned a wrong AST
  (`verb-tail-baseline-{interp,jit}.out`). Paragraph gap reconstruction used
  the command token's end instead of the raw echo's end. The staged
  installed `span_raw_boundary_fix` changes promote reconstruction's existing echo-to-source
  conversion to a shared helper and uses the same effective boundary in
  paragraph scanning; the combined raw-inline candidate now matches the
  included native tree (`raw-inline-foreign-v2-*.out`). The isolated fix also
  matches the original `\verb` case and eight included whitespace, comment,
  blank-line and definition-echo fixtures in both tiers
  (`verb-tail-boundary-fix-*.out`, `raw-boundary-fixtures-*.out`). The paired
  regression and eight neighboring goldens pass both tiers
  (`boundary-neighbors-*.log`). Three pinned-release pairs have identical
  native-checked output; baseline CPU seconds were 11.094, 12.082, 12.182 and
  candidate seconds were 10.919, 12.276, 12.076
  (`bench-raw-boundary-final.log`). Two installed-source GTests pass in both
  tiers (`raw-boundary-installed-gtest-{interp,jit}.log`); the expanded
  regression matches its golden under forced GC and freed-memory poisoning in
  both tiers (`test_tex_script_verb_digest-boundary-gc-*`).
- **Expanded-definition parameter scanning:** `span_edef_stream_params` is an
  exploratory candidate only. Native checks macro parameters as it expands
  `\edef`/`\xdef` replacement tokens, rather than checking a completed token
  list. The staged scanner follows that ordering and preserves direct token
  insertions; the initial 12 previously expanded-stream edge cases now match
  native, and the diagnostic-order probe has eight matches and two remaining
  explicit missing-number stops in the interpreter. A broader parameter,
  protection and token-list matrix exposed two silent mismatches involving
  `\unexpanded` and `\detokenize` (`edef-stream-param-matrix-*.out`); that
  version must not be installed. Native stores `\unexpanded` tokens without
  direct-source identity when expanded by `get_x_token`, while `\detokenize`
  tokens produced by the primary expanded-body scanner enter the body directly.
  The follow-up `span_edef_stream_params_v2` implements those distinctions:
  26 of 28 parameter cases match native in both tiers; the other two stop on
  existing missing-number and runaway-definition paths
  (`edef-stream-param-matrix-v2-*.out`). The two previously silent cases now
  match text and full diagnostics (`edef-stream-param-detail-v2-*.out`). Wider
  Thirty-six additional expanded-definition/token-list cases, 13 ordinary
  `\unexpanded`/`\detokenize` cases, and an included diagnostic-file case
  match in both tiers (`edef-stream-param-{extra,foreign}-*.out`,
  `token-primitive-ordinary-v2-*.out`). Seven of eight neighboring goldens
  pass in both tiers; the eighth gains a native-verified recovery
  (`edef-v2-neighbors-*.log`). The subsequent numeric candidate and its
  regressions remain before promotion.
- **Missing numeric operands:** `span_edef_number_recovery` is a staged
  follow-up, not installed as a whole. Its shared numeric recovery was
  independently applied to the current structured scanner on 2026-11-17;
  the expanded-definition changes remain separate. Native diagnoses a missing number, uses zero, and
  returns the triggering token to the stream. The candidate matches native
  text and complete diagnostics in 22 of 24 primary cases and 25 of 29 broader
  contexts in both tiers; the other cases stop explicitly on separate
  relation, unit, brace or arithmetic recovery paths. Included-file diagnostics
  also match (`missing-number-recovery-{matrix,extra,foreign}-*.out`). The
  parameter and number regression drafts matched in both tiers before the
  latest edge cases were added; the expanded draft rerun was interrupted when
  the goal was cleared. All ten expanded-parameter diagnostic-order cases now match
  (`illegal-param-expanded-diagnostic-order-number-*.out`). Eleven truncated
  parameter cases show no silent mismatch; nine retain the existing explicit
  runaway-definition stop (`edef-truncated-hash-number-*.out`). Another 31
  numeric-caller boundary cases match in both tiers except one explicit
  unsupported primitive (`missing-number-caller-boundary-*.out`). The eight
  neighboring tests pass in both tiers except the already native-verified
  illegal-parameter recovery improvement (`number-neighbors-*.log`). Compare
  release performance and qualify installed tests before promotion.
- **Existing verbatim regression:** after installing the verbatim tree support,
  `test_tex_script_verb` still expected the former raw-inline digest guard. Its
  assertion now compares the full native AST, and both tiers produce the
  updated golden (`test_tex_script_verb-verified-*.out`).
- **Extra-group recovery:** the installed engine now records the native
  diagnostic for an extra source `}` or `\endgroup`,
  preserves the active scope, and emits an extra `}` while leaving internal
  group closures on the existing path. Thirty-four direct cases, including
  scope and `\aftergroup`, match native reconstructed text and diagnostic
  records in both tiers, as does an included file with both errors
  (`group-recovery-{compare,scope,foreign}-{interp,jit}.out`). Nine neighboring
  goldens pass in both tiers. The expansion golden differed
  in the newly recovered group case and a stale expectation for the already
  installed `\unless` recovery. Its installed replacement checks the
  corresponding diagnostic codes explicitly and also corrects the end-hook
  tail expectation (`combined-expansion-regression-{interp,jit}.out`).
  The included-file case also passes with collection scheduled every 100,000
  allocations and freed-memory poisoning. The combined change matches both
  native AST and diagnostic records on an included document in both tiers
  (`combined-recovery-foreign-{interp,jit}.out`).
- **Current increment:** invalid `\unless` recovery now records its native
  diagnostic, returns the following token to the stream and accepts end of
  input. Interpreter and JIT match 14 expansion cases, two AST cases and an
  included-file diagnostic; the focused harness passes both new goldens in
  interpreter and JIT (`temp/tex-script-f/unless-gtest-{interp,jit}.log`).
- **Current increment:** direct paragraph digestion now reconstructs whitespace
  runs before applying native comment and adjacent-LF rules. The source
  `A\n%comment\n\nText` previously split on a TeX `par` token where native
  retains one paragraph (`temp/tex-script-f/paragraph-before.out`). The
  installed candidate matched a 72-case matrix, 25 edge cases and six explicit
  `\par` cases in the staged JIT runs; six existing goldens passed in both
  tiers before an equivalent helper cleanup. The installed 30-case golden
  passes the harness in both tiers after that cleanup, as does an 18-case
  whitespace-length golden. Six selected cases pass
  sampled forced GC with poisoning in JIT, and one explicit `\par`/CRLF case
  passes the same sampled GC profile in the interpreter. The earlier `f` and
  two `unless` goldens also pass the harness in both tiers after installation.
  Full Lambda and Test262 baselines have not been run for this increment.
  An isolated release build passed host export
  checks and used input and Radiant snapshots under `temp/tex-script-f/build-src`.
  As of the latest check, Input's ICS, TOML, VCF and YAML parsers and Radiant's
  `dom_range.cpp` and `state_store.cpp` differ from those snapshots; TeX and
  LaTeX input sources match. On the pinned host SHA-256 `bbc1e4d24f1d8d48228967d5eb5b2580f44707dadafb6fde4605d2c15acb0684`,
  three interleaved digest pairs for the installed code matched native ASTs and
  exact output hashes: baseline 10.292/10.766/10.852 s, candidate
  10.160/10.832/10.834 s (`temp/tex-script-f/bench-digest-final.log`). The reconstruction output also had
  identical hashes; baseline 12.776/10.892/10.947 s, candidate
  10.913/10.912/11.011 s (`temp/tex-script-f/bench-reconstruct.log`). These
  measurements cover one sampled document; the reconstruction measurements
  predate a digest-only helper cleanup.
  Build-file generation invalidated shared debug objects when owner paths
  changed. Normal generated build files were restored; the removed debug
  objects require rebuilding when needed.
- **Source mapping:** optional token spans now identify byte ranges in
  reconstructed output and report coalesced emissions as a single range.
  `test_tex_script_reconstructed_token_spans` passes interpreter and JIT for
  generated and mixed coordinates, UTF-8, coalesced accents and inserted
  separators. The default reconstruction output remains unchanged. On the
  same pinned release host, default reconstruction's three baseline samples
  were 12.085/10.457/10.278 s and span-capable samples were
  10.409/17.266/10.473 s with identical output hashes
  (`temp/tex-script-f/bench-spans.log`); the one slow sample leaves the timing
  comparison noisy. The opt-in tracking path passes sampled forced GC with
  poisoning in both tiers (`temp/tex-script-f/span-gc-{interp,jit}.out`). The
  existing reconstruction golden also matches in both tiers when run directly.
- **Source-mapping increment:** the expanded-argument digestion candidate from
  `temp/tex-script-f/span_fast/` is installed. It matched 18 of 20 mixed, generated and
  Unicode coordinate cases; an unclosed group and a generated command name
  retain existing guards. Sixteen additional cases, including stars, preceding
  arguments and comment gaps, matched in both tiers. An earlier snapshot passed
  six existing goldens in both tiers, and the unchanged digester golden differs
  only in three newly native-matching cases. The final short-circuiting scan
  passes a 20-case focused comparison in both tiers. Its three ordinary release
  pairs have identical output hashes (`bench-paren-fast.log`); timings are noisy.
  Three unmatched-parentheses release pairs also matched exactly; baseline CPU
  was 14.913/11.965/12.230 s and candidate 12.987/12.441/12.069 s
  (`bench-paren-fast-unmatched.log`). Standalone expanded parentheses also
  matched in three release pairs, with baseline CPU 10.207/10.150/10.075 s and
  candidate 10.386/10.284/10.407 s (`bench-paren-fast-plain.log`). Thirty
  repeated expanded argument groups matched native in a release run taking
  10.296 s (`paren-fallback-release.out`). Sampled GC with poisoning passes in
  both tiers, as does the long unmatched interpreter workload. Six related
  goldens pass both tiers; the complete digester comparison matched between
  tiers and changed only the three newly native-matching cases. The installed
  20-case and token-span harness tests pass both tiers, and the complete
  digester golden matches directly in both tiers. The full corpus and
  production migration gates remain open.
- **Included-file arguments:** the source-identity fix is installed. Five
  individual fixtures and a combined UTF-8 document match native ASTs in both
  tiers, and the installed three-test source-mapping harness passes in both
  tiers (`foreign-installed-harness-{jit,interp}.log`). An included whole group
  and a mixed group pass sampled GC with poisoning in both tiers. Six
  neighboring goldens also match in both tiers on the final candidate
  (`paren-foreign-final-goldens-{jit,interp}.log`). Three
  interleaved ordinary-document release pairs on the pinned host match native
  and have identical output hashes: baseline CPU 11.120/10.338/10.289 s and
  candidate 10.646/10.314/10.580 s (`bench-paren-foreign.log`). These timings
  qualify one ordinary workload, not the cost of the new mapped path. The
  installed included-file fixture also matches its golden on that release host
  (`paren-foreign-installed-release.out`).
- **Included and generated commands:** source mapping is installed for the
  qualified constructor-argument path. A combined fixture and the existing
  parenthesis goldens pass the installed harness in both tiers
  (`commands-installed-harness-{jit,interp}.log`); the fixture also matches on
  the pinned release host. The installed combined case passes sampled GC with
  poisoning in both tiers (`commands-installed-gc-{jit,interp}.out`). A 21-case mixed-source matrix and 28-case
  control/whitespace matrix match native with constructor mode on. Seven
  neighboring goldens and the complete digester golden match in both tiers on
  the equivalent candidate; the last classification cleanup was followed by
  focused installed tests. Without constructor mode, expansion can discard
  commands native retains in arguments, so the command guard remains. Some nested
  environments and definitions are guarded, and other source-provenance and
  document parity work remains open. The release host and snapshot limitations
  are as recorded above. Three interleaved ordinary-document CPU pairs were
  baseline 10.352/10.189/10.350 s and candidate 10.331/10.352/10.299 s;
  repeated expanded-formatting pairs were baseline 10.270/10.189/10.308 s and
  candidate 10.307/10.299/10.353 s. All had matching hashes and native ASTs
  (`bench-command-final-{ordinary,expanded}.log`). They cover those workloads,
  not the cost of the newly mapped included-command path.
- **Included nested environments:** the installed source-origin fix applies
  the existing emission helper after the whole raw environment name is read,
  when native emits those tokens. An earlier per-read origin candidate failed
  a name split across files; the native emission point is documented by
  `latex_arg_mode_env` and `emit_token` in Input. The qualified candidate
  matches native on 18 of 21 environment cases and all three native-parseable
  split-source cases in both tiers. Three other environment forms remain
  guarded; four split names are rejected by native. See
  `foreign-command-environment-{matrix,split}-v2-{jit,interp}.out`. Eight
  constructor, environment, math and provenance goldens and the staged fixture
  pass both tiers; sampled GC with poisoning passes both tiers. Three interleaved
  nested-environment release pairs on the pinned host matched native and exact
  output hashes: baseline CPU 10.303/10.312/10.248 s and candidate
  10.261/10.362/10.141 s (`bench-environment-final.log`). This workload uses
  main-file environments; the installed included fixture also matches on that
  release host. The installed three-test harness and sampled GC with poisoning
  pass in both tiers (`environment-installed-harness-{jit,interp}.log` and
  `environment-installed-gc-{jit,interp}.out`).
- **Nested definitions:** the installed constructor-mode increment records
  definition category codes, reuses the existing definition parser and maps
  constructed node offsets. It matches native in both tiers on 23 of 24
  included-file cases and the same 23 main-file cases; a macro-expanded body
  remains guarded. See
  `{foreign,main}-definition-matrix-{jit,interp}.out`. Six tested mixed-source
  definitions remain guarded. A category-code probe found that native's LaTeX
  scanner splits punctuation and UTF-8 control symbols differently from the
  TeX reader; the implementation checks source-lexer compatibility and guards these
  two cases rather than returning a mismatched AST. Evidence:
  `foreign-definition-{split,catcodes-v2,unicode-v3}-{jit,interp}.out`.
  The permanent combined fixture matches native in both tiers and checks both
  scanner guards; the candidate also passes sampled GC with poisoning in both
  tiers. Ten neighboring goldens passed on successive candidate snapshots and
  four definition/constructor goldens passed after the final shared token
  comparison refactor (`definition-final-v4-goldens-{jit,interp}.log`). The
  final fixture, matrix and sampled-GC checks pass in both tiers. Three
  interleaved release pairs on the pinned host matched native and exact output
  hashes: baseline CPU 10.222/10.328/10.300 s and candidate
  10.139/10.227/10.115 s (`bench-definitions-v4.log`). The workload exercises
  existing main-file nested definitions; the installed fixture also matches
  its golden on that release host. The installed harness and sampled GC with
  poisoning pass in both tiers (`definitions-installed-harness-{jit,interp}.log`
  and `definitions-installed-gc-{jit,interp}.out`).
- **Environment options:** the installed change distinguishes ordinary
  environment body brackets from list `options_raw`, including reconstructed
  macro and included-file options. Its 14-name matrix, 23 of 24 whitespace and
  syntax cases, six mixed-source cases and the permanent fixture match native
  in both tiers; the remaining mixed bracket-and-brace case stays guarded. Sampled GC
  with poisoning passes both tiers. Eight neighboring goldens match; an existing
  inline-environment expectation was strengthened from unsupported to native
  equality in both tiers. Follow-up category-code, whitespace and active-percent
  probes exposed silent mismatches; the staged scanner now follows emitted
  bytes, native whitespace, escape and comment behavior, and guards ambiguous
  token interiors. The revised 23-case fixture, six whitespace probes and
  active-percent comparison match in both tiers. Nine neighboring goldens and
  sampled GC with poisoning pass both tiers on the byte-scanner candidate.
  Three-pair release comparisons of that snapshot matched native ASTs and output
  hashes (`bench-envoptions-byte-{ordinary,brackets}.log`). The bracket-heavy
  timings showed a small cost, so the installed per-environment scan was
  compared against that candidate on the same host. Prior CPU times were
  10.839/10.782/10.696 s and new times were 10.833/10.694/10.740 s, with exact
  output and native AST equality (`bench-bracket-scan.log`). A separate release
  run with 25 list options matched native in 10.44 s
  (`envoptions-release-workload.out`); this is an absolute time, not a paired
  comparison. The installed three-test harness and sampled GC with poisoning
  pass in both tiers, and the fixture matches on the pinned release host
  (`envoptions-installed-harness-{jit,interp}.log`,
  `envoptions-installed-gc-{jit,interp}.out`, `envoptions-installed-release.out`).
- **Next action:** rerun the interrupted staged regressions and frozen
  141-script default-constructor sweep,
  compare changed rows with the installed baseline and native behavior, and
  qualify the corpus before changing the default. Continue the remaining raw
  inline source guards, byte-level non-ASCII delimiters, illegal-parameter
  recovery after token-stream changes, declaration raw-option guards, and
  mixed-source provenance. Investigate the broad suite's batch exits and finish
  the specified qualification and coordinated production migration.

General Lambda language, compiler and runtime defects found during this work
remain in the [Lambda issue ledger](../Lambda_Issue_Ledger.md). The package
findings below were moved from that ledger on 2026-11-12; their dated statuses,
reproductions, evidence and test history are retained.

## Lambda Script expl3 subset needs a TeX expansion boundary — 2026-11-11

**Status:** OPEN — feature integration boundary. **Dependent task blocked:** yes,
the requested expl3 subset in `lambda.doc.latex`. Authority: D9.1.3v2,
D10.2.2, D8.3.1 and D11.2.1.

`lmd/package/doc/latex/latex.ls` calls `parse` before its adapters run. Input's
`run_tex_engine` performs TeX expansion during that parse, and its current
`EngineOptions` and `InputParseOptions` carry adapter names and raw commands,
not a way to call Lambda Script during expansion. Consequently a script module
used by the existing post-expansion adapters cannot implement expandable expl3
operations, scoped TeX assignments or macro definitions at the point TeX needs
them. Reproduce by inspecting those entry points and the expl3 fixture at
`test/lambda/latex/fixtures/local_packages/lmdexpl.sty`.

The requested implementation needs Lambda-owned package/runtime work and an
agreed input/host boundary; the shape of that boundary is not yet decided.
Input must not import the runtime, values crossing the boundary require precise
rooting, and resource authority must be preserved. Input-side design and the
candidate first subset are recorded in `../input/Input_Latex.md`.

### Superseding request: full TeX and LaTeX script port — 2026-11-11

The user now requests porting the TeX engine to Lambda Script and moving the
entire LaTeX handling path to script, superseding the partial expl3 bridge
request above. **Dependent task blocked:** yes, pending Lambda-owned package,
runtime, formal-design and test work and a coordinated input migration.

Current evidence: `lambda.doc.latex.latex.parse_source` calls the built-in
`parse`; input's `parse_latex_direct` runs `tex::Engine` and then the C++
`DirectLatexParser`, while `parse_tex_expansion` exposes the engine directly.
Inspect `lmd/package/doc/latex/latex.ls`, input's `lambda/input/input-latex-c.cpp`
and `lambda/input/input.cpp`. A package-only engine leaves those public and
native entry points unchanged. D10.2.2 currently places every LaTeX parse at
the input boundary, so the requested migration requires coordinated revision
of that ruling. Preserve S1.8 (TeX remains data), D9.1.3v2 (no input runtime
imports), D11.2.1 authority and existing conformance and document fixtures.

At the time of this 2026-11-11 observation, no script engine had been shipped.
Input-side scope and candidate compatibility gates are documented in
`../input/Input_Latex.md`.

2026-11-11 follow-up: the user authorized coordinated edits to the Lambda and
input checkouts. `lmd/package/doc/latex/tex_engine/reader.ls` starts the script
port with incremental token reading, and `test/lambda/latex/test_tex_script_reader`
has a golden result passing in interpreter and JIT modes. The production parse
path is still the C++ implementation. Expansion, primitives, AST construction,
public-entry migration, formal ruling updates and full parity remain open;
the earlier authorization blocker is removed.


## Peer-sync qualification: TeX/expl3 package-echo golden mismatch — 2026-11-11

**Status:** FIXED for the script-handled package echo. The earlier aggregate
Lambda baseline was **7,475/7,476**, with the sole failure in
`test/lambda/latex/test_tex_script_expl3.ls`. Its `script expl3
package` golden expects `A` but receives `usepackage{expl3}A`; the duplicate
case expects `X ` but receives `usepackage{expl3}usepackage{expl3}X `. The
isolated unchanged test reproduces the same mismatch. These cases inspect the
script expansion's direct output-token values; this report does not decide
whether that projection, its golden or the expansion needs correction.
Evidence in the Lambda checkout: `temp/sync-peer/baseline.log`,
`expl3-recheck.log` and `expl3-recheck.json`.

The peer task changed neither the TeX modules nor their goldens; the mismatch
is outside its sync-module paths and does not block the focused peer acceptance
gates. This report does not assess the separately recorded package-echo selection
fix. S1.8 and D10.2.2 remain the applicable input boundary.


## Expanded parenthesized command argument source mapping — 2026-11-11

**Status:** PARTIALLY FIXED for the qualified expanded parenthesized arguments.
**Full script TeX-port blocker:** yes, for the remaining source-provenance and
argument forms. The production native parser is unaffected.

Expand `\def\arg{(1,2)}\begin{document}\mystery\arg\end{document}` with
`profile.expand_with_kernel` and `echo_definitions: true`, then compare
`digest.digest(expanded).ast` to `parse(source, {type: "latex"})`. The native
AST puts `(1,2)` in a `parenthesized` argument of `mystery`, mapped to the
invocation; the expansion tokens point at the macro definition and have
`direct: false`. A second reproduction is `\def\arg{1,2}\begin{document}
\mystery(\arg)\end{document}`: only the contents are expanded, but native still
recognizes a coordinate argument and records its expanded raw value. Using only
original source slices misclassifies both examples as ordinary text and cannot
produce the native argument descriptor. The script digester must retain enough
invocation provenance and expanded text to reproduce native raw values and
source offsets without violating S1.8 or precise source ownership. The staged
port previously returned `latex-digest-unsupported` for these cases; its
regression tests retain these reproductions.

2026-11-15 follow-up: `temp/tex-script-f/paren-origin.out` rechecks both
reproductions against the current engine. Reconstruction yields the expected
expanded text and segment maps. In the fully generated example the opening
byte maps to invocation offset 39, but the group end maps to offset 43 at the
*next* segment boundary; mapping the last generated token's origin instead
would incorrectly return 39. In the mixed example the opening and closing
delimiters are direct while the contents map to the invocation. A general
solution needs emitted-token positions in the reconstructed byte stream,
expanded raw argument text, and boundary mapping through the existing segment
map; it must preserve the current source and constructor guards. No guard is
relaxed by that observation.

The Script digester now obtains optional per-emission byte ranges from the
shared reconstruction path when an expanded parenthesized argument may be
present. It maps both argument boundaries through the segment map and reads
the expanded raw argument from the reconstructed text. Character-only groups,
including generated delimiters, mixed direct and expanded contents, starred
commands and preceding arguments, match native in the qualified fixtures.
Ambiguous coalesced tokens, non-character contents, unclosed groups and
generated generic command names retain their respective guards. The new
`test_tex_script_paren_reconstruction` fixture compares 20 cases, and the
previous three digester expectations now require native AST equality.
Detailed test and release-performance evidence is in the current checkpoint;
full source mapping and production migration remain open.

### Included-file argument source identity — 2026-11-15

**Status:** PARTIALLY FIXED for the tested included character-only parenthesis
groups. S1.8, D10.2.2 and D11.2.1 apply.
For `\mystery(\input{paren-foreign-child.tex})` where the child contains
`1,2`, the installed digester emits an argument-free command and plain text;
native emits a parenthesized argument with the included text and main-source
offsets (`temp/tex-script-f/paren-foreign-probe.out`). The included tokens are
direct in their own file, so checking only `direct` missed their synthetic
relationship to the main source. The installed digester also compares file
identity and uses reconstruction segments for those boundaries.
Five separate fixtures and one combined UTF-8 document match native ASTs in
both tiers (`paren-foreign-matrix-{jit,interp}.out` and
`paren-foreign-combined-{jit,interp}.out`). The permanent combined fixture and
the existing reconstruction and macro-coordinate tests pass the installed
harness in both tiers. Mixed and whole included groups pass sampled GC with
poisoning in both tiers; release comparison and its limits are recorded in the
checkpoint. Foreign generic command names remain a separate issue.


## Script spacing reconstruction lost separators and comment-adjacent whitespace — 2026-11-11

**Status:** FIXED for the tested spacing and control-space forms in the
experimental script digester. **Full TeX-port blocker:** no for these cases;
full source-provenance and LaTeX parity remain open.

With `\def\spaceout{\quad}\begin{document}A\spaceout B\end{document}`
and `echo_definitions: true`, native parsing yields `"A"`, `quad`, `" B"`;
the script digester previously yielded `"A"`, `quad`, `"B"`. The expanded
`quad` token is not source-adjacent to the following letter: native TeX
reconstruction inserts a separator to keep a control word and following ASCII
letter distinct. This is not simply copied invocation whitespace: two spaces
at the macro call still produce one separator, while following punctuation
needs none. The digester now follows that reconstruction rule.

The same investigation found that direct spacing commands and control spaces
followed by a comment could lose whitespace around the comment. The shared
source-gap scanner now retains that whitespace while matching native comment
line-ending handling, including CRLF. The full script digester goldens pass
interpreter and JIT, with native-AST comparisons for macro expansion, following
letters, punctuation and groups, and LF/CRLF comments. The separate expanded
parenthesized-argument source-mapping issue remains open.

## Script TeX control-space primitive was unsupported — 2026-11-11

**Status:** FIXED in the experimental script engine. **Full TeX-port blocker:**
no for the tested control-space forms; full TeX and LaTeX parity remains open.

Expanding `A\ B` with the script profile previously stopped with
`tex-unsupported-primitive` for control space, although native TeX keeps the
primitive in the expanded stream and native LaTeX parses it as a literal space.
The primitive was recognized in the registry but lacked an execution branch.
The script expansion step now preserves the control token, including `\let`
aliases; the digester consumes a literal control space and recovers direct
following whitespace from source. New TeX expansion token tests and
`test/lambda/latex/test_tex_script_control_space.ls` cover direct, repeated,
macro-expanded, and formatted cases. The staged expansion and digester
goldens pass interpreter and JIT; the control-space AST cases match native in
both backends.


## Macro-generated inline formatting uses definition offset — 2026-11-11

**Status:** FIXED for the tested inline-formatting cases. The stream records
fresh main-file reads and synthetic formatting tokens carry the current read
offset; source reconstruction for generic commands remains open separately.
The production native parser is unaffected.

Expand `\def\u{\textbf{Hi}}\begin{document}\u\end{document}` using
`profile.expand_with_kernel` with `echo_definitions: true`, then compare
`digest.digest(expanded).ast` against `parse(source, {type: "latex"})`. The
script digester previously returned a formatting element whose source offset
points at the command in the macro definition, whereas native reconstruction
maps the expanded command to its invocation. The affected synthetic token has
`direct: false` and retains its definition offset. The stream now records the
last fresh main-file read without resetting it when tokens are pushed back or
foreign files are read; the digester uses that position for synthetic inline
formatting. `test_tex_script_provenance` compares nested and repeated macros,
arguments, lookahead, case changes and non-ASCII text against native ASTs;
`test_tex_script_provenance_package` compares offsets from a local package.
The installed tests and existing TeX script suites pass interpreter and JIT,
including focused forced-GC checks. Expanded parenthesized and foreign generic
command source mappings remain open.


## Script TeX underline and overline primitives were unsupported — 2026-11-11

**Status:** FIXED for direct and tested macro-generated text underline, and
tested math underline/overline forms. **Full TeX-port blocker:** no for the
covered forms; generic source reconstruction remains open.

The experimental profile stopped with `tex-unsupported-primitive` for
`\underline{Hello}` and `$\overline{x}$`, although native TeX retains those
primitives for the LaTeX digester. They were recognized in the primitive
registry but lacked execution branches. The script engine now preserves the
primitive tokens and aliases; the digester builds native-compatible text
underline and math accent nodes. `test/lambda/latex/test_tex_script_underline`
covers nesting, empty and spaced groups, math expressions, primitive aliases,
and macro-generated formatting cases. Expansion and digester goldens pass
interpreter and JIT, and supported ASTs
match native parsing on both backends.


## Empty TeX macro argument inserts a null token — 2026-11-11

**Status:** FIXED for tested empty required and optional macro arguments.
**Full script TeX-port blocker:** no for those forms. Native TeX is unaffected.

Before the fix, expanding `\def\a#1{[#1]}\a{}` in the script engine failed with
E201, `map literal spread requires a map-like value or element`. The macro
substitution comprehension places a conditional around `*args[t.value]`; an
empty argument becomes a null item, which `stored_tokens` then tries to spread
as a map. A standalone reproduction is `let a = [[]]; [for (t in [0]) if
(true) *a[t] else t]`, which produces `[null]` on the observed interpreter.
The fix spreads the selected token array outside the conditional so that the
empty argument contributes zero tokens. The expression's behavior is consistent
with collapsed list values under S2.5.5v2; no runtime defect is asserted. Both
TeX and expl3 macro substitutions use this shared helper. The installed
`test_tex_script_provenance` covers empty, repeated and mixed arguments in both
backends; existing TeX and expl3 goldens also pass.

## TeX case shifting retains direct source flags — 2026-11-11

**Status:** FIXED for the tested case-shift source mappings. **Full script
TeX-port blocker:** no for those forms. Native TeX is unaffected.

For `\begin{document}\uppercase{\textbf{Hi}}\end{document}`, the native
AST reports the `textbf` source offset at the last main-file token read by
the case operation, while the script previously used the command's original
position. Native `case_shift` backs up a stored copy of the scanned token list,
and `copy_stored` clears direct-source flags; the script shifted the list and
pushed it without clearing them. The fix stores the resulting tokens before
pushing them, preserving TeX token values and category codes while marking their
source as synthetic. The stream also tracks the main-file read position used
for synthetic formatting tokens.
`test_tex_script_provenance` compares upper and lower case, nested formatting
and zero mappings to native in both backends.


## Script LaTeX profile cannot load lmdnotes counter declaration — 2026-11-11

**Status:** FIXED for counter expansion in the local-package fixture. The
fixture's synthesized class declaration still blocks script AST digestion in
the separate source-mapping issue below; the native production path remains
available.

Run `profile.expand_with_kernel` with the source from
`test/lambda/latex/fixtures/local_packages/document.tex`, authorized local
snapshots of `lmdreport.cls` and `lmdnotes.sty`, and an empty bundled snapshot.
The script result previously stopped at `newcounter` in `lmdnotes.sty`; see
`../lambda-input/temp/expl3-script/provenance-existing-fixture.out`. The script
engine now implements register-backed declarations, global set/add/step,
child resets, `value` and the tested counter formats. The installed
`test_tex_script_counters` passes interpreter and JIT and compares with native
and pdfLaTeX, keeping input LI-014's two intentional native divergences visible.
`test_tex_script_counters_fixture` confirms expansion reaches both exercise
numbers; AST digestion still reports the class source-mapping issue.
`refstepcounter` currently implements numeric step/reset only; reference-label
state remains unfinished.


## Foreign generic command offsets use package positions — 2026-11-11

**Status:** PARTIALLY FIXED for the tested generic commands and arguments in
constructor-argument mode. S1.8, D10.2.2 and D11.2.1 apply. Full Script-port
parity remains open for unqualified source spans, nested special structures,
and ordinary expansion. Native production parsing is unaffected.

A local package containing `\mystery{Package}` and loaded by
`\usepackage{foreign-output}\begin{document}Hi\end{document}` produces
a native `<mystery>` whose `source_offset` and required-argument boundaries
map to main-file offset 27. The staged script expansion/digestion instead
returned the package-file command and group offsets 54, 62 and 71. Reproduce
with `../lambda-input/temp/expl3-script/provenance-foreign.ls` and its captured
local files under `../lambda-input/temp/expl3-provenance/`; evidence is in
`provenance-foreign-generic.out` there. The command tokens are direct relative
to their package file but their emitted bytes are synthetic relative to the
main document. The digester now uses reconstructed token boundaries and source
segments for qualified included and generated commands in constructor mode,
including mixed-file arguments, stars, comments, nested generic commands, math
and item offsets. Coalesced emissions expose only their outer boundaries.
`test_tex_script_foreign_commands` compares the combined fixture and a generated
command with native and checks the ordinary-mode guard. Validation and release
timings are in the current checkpoint. This is related to the expanded
parenthesized-argument issue.

### Included environments within generic arguments — 2026-11-15

**Status:** PARTIALLY FIXED for the tested constructor-mode environments.
S1.8, D10.2.2 and D11.2.1 apply. Environment-name tokens captured from an
included file previously lacked a main-source emission origin, so reconstruction
stopped before their opening brace. The engine now attaches the reader position
when it emits the complete passthrough command and raw name, matching native's
emission point. The digester maps the resulting environment-node offset through
the reconstruction segments. This timing also handles a command in a child file
whose environment name is read from the main file; attaching origins earlier at
each individual read produced a different native offset. The combined permanent
fixture covers nested environments, math and that split. The checkpoint records
the matrix, guards, regression and release evidence. Nested definitions and
other environment syntax still require work.

### List options and emitted delimiters — 2026-11-16

**Status:** PARTIALLY FIXED for the tested generic environments and list
options. S1.8, D10.2.2 and D11.2.1 apply. Ordinary environments keep a
following bracket in their body, while native `itemize`, `enumerate` and
`description` environments consume a raw option after source whitespace. The
Script digester now distinguishes those cases and maps expanded or included
option text through reconstructed output. Its bracket scanner follows emitted
bytes, including native comment and escape rules: the original token's value
alone is insufficient when category codes change, and an active percent can
start a source comment. Token interiors that cannot be mapped remain guarded.
`test_tex_script_environment_options` compares 23 direct cases and an included
case with native, and records two guards. The current checkpoint records the
focused regressions and release qualification.


## Script TeX writes bypass terminal line buffering — 2026-11-11

**Status:** FIXED for terminal writes and `\showtokens` in the script engine.
Output-file streams remain separately OPEN below.

Previously `\immediate\write16{}` recorded an empty message, while native
TeX flushes no line. With `\newlinechar=64\immediate\write16{A@B}`, native
records `A` and `B` separately while the script recorded `A@B`. The cause was
that `write_message` directly appended `token_text` instead of honoring
`newlinechar` and native flush rules. The script now buffers terminal output,
only splitting newly printed text using the current newline character; writes
and `\showtokens` flush around their output. The installed
`test_tex_script_message_pending` compares empty writes, line breaks, changes to
`newlinechar`, and interleaved message/write/show operations against native in
interpreter and JIT. The earlier reproduction is in
`../lambda-input/temp/expl3-script/message-old-interp.out`.


## Script TeX message primitive is unsupported — 2026-11-11

**Status:** FIXED for terminal output and string diagnostic codes; structured
native diagnostic metadata remains separately OPEN. **Full script TeX-port
blocker:** this primitive no longer blocks the tested message cases.

Previously `\message{hello}` returned `tex-unsupported-primitive`, while native
`parse("\\message{hello}", {type:"tex"})` appended `hello` to its messages.
The registered `\errmessage` primitive also had no execution branch. Both now
expand their arguments and use the shared terminal buffer, including native
spacing and flush behavior; `\errmessage` adds `tex-errmessage` to script
diagnostic codes. Installed native comparisons cover multiple messages, empty
messages, macro and counter expansion, newline changes, Unicode, and error
lines; they pass in interpreter and JIT. Forced-GC tests pass both tiers.
Structured diagnostics and output-stream state remain separately OPEN.


## Echoed TeX source spans lose token sequence boundaries — 2026-11-11

**Status:** FIXED for sequence-boundary retention. The general reconstruction
and source map remain OPEN; native production parsing is unaffected.

`\def\x{A}% comment\nB` emits an echoed `source-span` followed by `B`.
The original reader tokens have consecutive sequence numbers, but the span
only retains byte offsets and drops its first and last sequence. Native output
reconstruction uses those sequence boundaries to preserve intervening source
bytes; without them, the script cannot correctly tell whether the comment and
newline belong in the reconstructed output. `stream.end_trace` computed the
boundaries but did not return them. It now returns the first and last sequence,
and echoed definition spans retain them. The installed stream and trace-span
tests cover adjacent comments, lookahead, and macro output in interpreter and
JIT; forced-GC tests also pass. Evidence is in
`../lambda-input/temp/expl3-script/trace-span-interp.out`.


## Script TeX output does not retain emission origins for all tokens — 2026-11-11

**Status:** OPEN — the general emitted-output provenance contract is incomplete.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed
source offsets. Native production parsing is unaffected.

For `\def\m{\quad}\begin{document}A\m B\end{document}`, native
reconstruction inserts a space between the expanded control word and `B`. Its
separator is mapped to the main-file read position recorded at emission. The
script's direct `B` output token has no emission-position field; synthetic
class declaration tokens also have no recorded main-file origin. Existing
`read_offset` covers selected synthetic tokens processed by `step`, not all
emission paths. A temporary reconstruction prototype therefore reports
`tex-reconstruct-missing-origin` at this separator rather than guessing.
Evidence: `../lambda-input/temp/expl3-script/reconstruct-tests-interp.out` and
`counter-fixture-prefix.out` in the same directory. Native `emit_token` and
`emit_text` record `last_main_offset` per output item; the script needs the
same contract, including output emitted after lookahead or by package helpers.


## Foreign RequirePackage declaration is echoed instead of synthesized — 2026-11-11

**Status:** FIXED for selecting the echoed or synthesized declaration.
Reconstructed source gaps and full offset mapping remain separately OPEN.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, the captured
`lmdnotes.sty` contains `\RequirePackage{xcolor}`. Native input emits the
synthesized declaration `\usepackage{xcolor}`, while the script emits the
foreign direct `RequirePackage` token and its package-file arguments. Previously the
script's `latex_usepackage` emitted original tokens whenever the plan had no
local loads, without checking whether a main-file source echo was valid. Native
`do_usepackage` uses `echo_finish`, which rejects foreign source reads, and
otherwise emits a synthesized declaration. The script now traces and validates
the echo, synthesizing the declaration when tracing fails. Installed native
comparisons cover direct and macro-generated commands and names, options,
duplicates, empty names, and the local-package fixture; they pass interpreter
and JIT, as do focused forced-GC checks. The original reproduction is in
`../lambda-input/temp/expl3-script/reconstruct-fixture-interp.out`.

## Echoed package declarations lose their consumed source gap — 2026-11-11

**Status:** OPEN — the script emits the declaration's explicit tokens but
omits whitespace consumed by its optional-version lookahead. **Full script
TeX-port blocker:** yes, for native reconstructed text and offset parity.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, native output
preserves the newline after the direct main-file `\usepackage{amsmath}` before
a subsequently synthesized declaration. The script emits the command and its
argument tokens, while `optional_argument` consumes the following newline;
the resulting output items no longer contain that source gap. Native
`echo_finish` emits one source span extending through the consumed bytes.
A temporary reconstruction with matching empty adapter configuration shows the
missing newline in `../lambda-input/temp/expl3-script/reconstruct-fixture-aligned.out`.
The same prototype also lacks the raw trailing source after `\end{document}`;
that trailing-source behavior needs to be carried into reconstruction.


## Script scantokens pseudo-source is unavailable to reconstruction — 2026-11-11

**Status:** FIXED for pseudo-source retention. General reconstructed output
and source mapping remain OPEN. Native production parsing is unaffected.

For `\begin{document}\scantokens{\textbf{Hi}}\end{document}`, native
expanded text contains `\textbf {Hi}` while a temporary script reconstruction
emits `\textbf{Hi}`. Native converts the scanned token list to a pseudo-file,
registers it in the engine's source list, and preserves the skipped space between
adjacent direct pseudo-file tokens. The script previously created its reader with file id -1 and did not retain the
source, preventing reconstruction from reading the gap. It now registers a
unique source for each `\scantokens` reader. Installed tests cover multiple and
nested calls and Unicode byte offsets; both backends and forced-GC checks pass.
The experimental reconstruction now matches native for this case and all 29
provenance text cases in interpreter and JIT. Evidence:
`../lambda-input/temp/expl3-script/reconstruct-scan-probe.out` and
`reconstruct-provenance-fixed-interp.out`.


## Script TeX inputlineno quantity is unsupported — 2026-11-11

**Status:** FIXED for the tested physical, pseudo-file, and local-package lines.
Structured diagnostic line metadata remains separately OPEN. Native production
parsing is unaffected.

Previously `Line 1\n\the\inputlineno/\number\inputlineno` stopped with
`tex-unsupported-primitive`, while native emitted `2/2`. The reader now tracks
physical lines per reader, and both `\the` and numeric scans read the active
reader's line number. Installed native comparisons cover CRLF, blank/comment
lines, signed scans, macro calls, multiline expansion, nested `\scantokens`,
and a local package returning to the main file. They pass interpreter and JIT,
as do affected regression and forced-GC checks. Original evidence:
`../lambda-input/temp/expl3-script/inputlineno-probe.out`.


## Script TeX character primitive is unsupported — 2026-11-11

**Status:** PARTIALLY FIXED — scalar values and invalid-range diagnostics are
implemented; surrogate code points remain explicitly unsupported. **Full script
TeX-port blocker:** yes, for surrogate values. Native parsing is unaffected.

Previously `\char65\char36` returned `tex-unsupported-primitive` for `char`,
while native reconstructed `A\$`. The script now scans and emits character
tokens with native categories, keeps the main-read position, and reports
`tex-bad-char` for numbers outside the native range. Installed tests cover
ASCII, Unicode scalars, zero, invalid ranges, registers, aliases, numeric forms,
macros, and special characters in interpreter and JIT; forced-GC checks pass.
Surrogate code points are explicitly rejected with `char surrogate`: native
retains such tokens while Lambda strings cannot encode them. The regression
test keeps this limit visible. Evidence:
`../lambda-input/temp/expl3-script/char-complete-interp.out`.


## Script TeX stops after an extra closing brace — 2026-11-11

**Status:** OPEN — script error recovery differs from native. **Full script
TeX-port blocker:** yes, for malformed documents where native continues after a
diagnostic. Native production parsing is unaffected.

Expand `A}B`: the script stops with `tex-extra-brace` after emitting `A`.
Native reports the same diagnostic and continues, producing `A}B ` (including
the reader's final space). The script treats this diagnostic as a terminal
`issue`; native `handle_right_brace` emits the extra token after reporting it.
Evidence: `../lambda-input/temp/expl3-script/brace-recovery-probe.out`.
The same difference occurs for an extra `\endgroup` and mismatched group
types. A staged fix matches native output and diagnostic codes on seven cases,
but the existing `test_tex_script_expansion.txt` explicitly expects a terminal
`tex-extra-endgroup` for `{\endgroup}`. The supplied checkout instructions
require preserving reference results; the staged change is not installed and
that failing comparison remains visible in
`../lambda-input/temp/expl3-script/group-recovery-existing-test.out`. Structured
diagnostic metadata remains a separate OPEN issue.


## Script digester rejects active-character definition targets — 2026-11-11

**Status:** PARTIALLY FIXED for the qualified direct, nested and included
active-character definitions. S1.8 and D10.2.2 govern the input boundary.

For `\def~{XYZ}\begin{document}A~B\end{document}`, expansion succeeded but
Script digestion rejected the echoed definition as `definition target`. Native
LaTeX emits an empty `def` node followed by the target and its group, and the
Script digester now follows that structure using its existing partial-definition
handling. Nested definitions resume at the active target so the enclosing
parser retains the rest of the source. `test_tex_script_active_definition`
compares 30 direct and nested cases and an included file against native text,
diagnostics and AST in both tiers. A following source group retains its
existing explicit mapping guard; nested blank lines are handled as described below. The former guarded
case in `test_tex_script_active_digest` now asserts native equality; current
harness, GC and performance qualification is in the checkpoint.


## Paragraphs inside partially digested definitions — 2026-11-16

**Status:** PARTIALLY FIXED for the qualified grouped cases. S1.8 and D10.2.2
govern the input boundary.

The partial-definition safety check previously rejected a paragraph token at
any nesting depth, including one inside a brace group that the existing inline
parser can represent. It now skips balanced groups while checking for outer
paragraph breaks, and retains the guard for a break at the outer level or an
unbalanced group. `test_tex_script_definition_paragraph` compares 25 direct,
nested and related definition cases and an included file with native text,
diagnostics and AST. A following-group mapping case remains explicitly guarded;
the current checkpoint records harness, GC and release qualification.


## Script LaTeX digester rejects emitted active characters — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary emitted active text is supported;
active characters that reconstruct to LaTeX syntax remain explicitly unsupported.
**Full script TeX-port blocker:** yes for those syntax characters. Native parsing
is unaffected.

Previously `\begin{document}A~B\end{document}` produced
`latex-digest-unsupported` for an active token while native produced paragraph
text `A~B`. The script now accepts emitted active text and accounts for active
letters after spacing commands. Installed native-AST tests cover tildes,
macros, custom active punctuation and letters, and spacing in interpreter and
JIT; forced-GC checks pass. Active `%`, `$`, and other syntax characters are
explicitly rejected because treating them as plain text produces an incorrect
AST; `active-syntax-probe.out` in `../lambda-input/temp/expl3-script/` shows
the native error. Active definition targets remain separately OPEN.


## Script math digester lacks common style commands — 2026-11-11

**Status:** FIXED for the tested style-command arguments and `\in` symbol.
Broader math support and general reconstruction remain OPEN. Native parsing is
unaffected.

Previously the local-package fixture reached `latex-digest-unsupported` for
`math command mathbb`. The script now emits native-compatible `style_command`
nodes for 12 common style commands and handles `\in`. Sixteen installed
native-AST comparisons cover braced, unbraced, nested, and scripted arguments
in interpreter and JIT; forced-GC checks pass. An isolated reconstruction and
reparse of the fixture now matches native AST content and mapped offsets in
both tiers, while root diagnostics/package metadata remain separately OPEN.
Evidence: `../lambda-input/temp/expl3-script/reparse-fixture-remapped.out`.


## Script math digester rejects repeated scripts — 2026-11-11

**Status:** FIXED for the tested repeated subscript and superscript forms;
S1.8 and D10.2.2 govern the input boundary.

The script previously rejected `x^2^3` as `repeated math script`. Native
combines successive superscripts into nested groups and replaces an earlier
subscript with the last; the Script math digester now does the same. The new
`test_tex_script_repeated_math` compares 23 direct, macro-generated, inline and
display expressions and an included-file case with native text, diagnostics
and AST in both tiers. The former pending `test_tex_script_digest` case now
asserts native equality. Unbraced punctuation such as `x__2` is handled by the
shared math punctuation parser. The `^^` source-field repair is recorded below. Six
focused GTests and scheduled GC/poison checks pass in each tier; release timing
is recorded in the current checkpoint.


## Unbraced math punctuation — 2026-11-16

**Status:** IMPLEMENTED for the qualified ASCII punctuation cases. S1.8 and
D10.2.2 govern the input boundary.

Native math parsing accepts punctuation and operators as unbraced script
arguments; the Script digester previously rejected inputs such as `x__2` and
`x_<=2`. A shared punctuation parser now serves both script arguments and
ordinary atoms, including adjacent `<=`, `>=`, `!=`, `->` and `;;` forms.
The earlier guarded `x__2` and echoed `$x^^2$` cases now assert native equality.
`test_tex_script_math_punctuation` covers 49 direct, generated and
comment-boundary cases and an included file against native text, diagnostics
and AST. The current checkpoint records harness, GC and release qualification;
full Unicode, error-recovery and native-lexer parity remain open.


## Reader-normalized math and echoed definition source — 2026-11-16

**Status:** PARTIALLY FIXED for the qualified direct, generated and included
cases. S1.8 and D10.2.2 govern the input boundary.

For `$x^^2$`, the reader emits `r` from the direct `^^2` source span. The
Script math AST matched native, but its `source` attribute incorrectly retained
`x^^2`; native records the reconstructed `xr`. Math mapping now compares a
direct token's source spelling with the existing reconstruction helper before
assuming that its source span is faithful. The digester also reparses echoed
definitions without the reader's `^^` conversion: native preserves raw
spelling there, even when the macro's expanded output is normalized. This mode
is confined to the digester's definition reader; normal TeX reading retains
its conversion.

`test_tex_script_math_source_normalized` compares 24 direct, generated,
definition and delimiter cases plus an included file against native text,
diagnostics and AST. The later math punctuation increment also handles the
previously guarded echoed `$x^^2$` expression; broader malformed-input and
native-lexer parity remain open. The current checkpoint records tier, GC and
release qualification.


## Script digester rejects bracket text after unknown environment start — 2026-11-11

**Status:** FIXED for the tested ordinary-environment body brackets. The
earlier guard rejected `\begin{strange}[x]Hello`; the installed parser treats
the bracket as body text, matching native. The existing document-bracket and
digest fixtures both assert native AST equality. A fresh direct comparison of
text, diagnostics and tree matches in JIT and interpreter
(`temp/latex-port-resume/generic-bracket{,-interp}.out`). The current checkpoint
records the broader environment-option limits (S1.8, D10.2.2).


## Script digester rejects leading bracket text in document — 2026-11-11

**Status:** FIXED for the tested document environment and ordinary environment
brackets. Native parsing is unaffected (S1.8, D10.2.2).

Previously `\begin{document}[A]\end{document}` returned
`latex-digest-unsupported` for `environment options`, while native treated
`[A]` as body text. The script now applies the option guard only to other
environments. Installed native comparisons cover direct, repeated, nested,
followed-by-group and macro-generated brackets in interpreter and JIT. The
full existing digest golden passes both tiers, and the reconstructed-path corpus
now matches 101 native ASTs with two explicitly unsupported cases. Original
evidence: `../lambda-input/temp/expl3-script/document-bracket-probe.out`.


## Experimental reconstructed-text digestion drops NUL characters — 2026-11-11

**Status:** FIXED in the isolated reconstruction prototype using the VMap
embedded-NUL key fix; general reconstruction remains uninstalled and OPEN.
**Full script TeX-port blocker:** yes, before that path can produce native ASTs
for documents containing character code zero. Production parsing is unaffected.

The prototype reconstructs `\begin{document}\char-1\end{document}` and
retokenizes the resulting text with the TeX reader. Native LaTeX digestion
retains the emitted NUL character, while the TeX reader's default category
for NUL ignores it; both `tex-bad-char` metadata and a successful prototype
AST are then present, but AST equality is false. The installed direct-token
script digester matches native on this example. Evidence:
`../lambda-input/temp/expl3-script/reparse-diagnostics-interp.out`. The
prototype must use LaTeX digester lexical semantics or explicitly reject cases
it cannot preserve; it must not be connected to production in its current form.


The tokenizer's explicit NUL category now survives the corrected map spread,
and the prototype matches native ASTs for invalid `\char` and mixed supported
diagnostics in interpreter and JIT with the isolated fixed executable. Evidence:
`../lambda-input/temp/expl3-script/nul-map-fix/reparse-diagnostics-interp.out`.


## Synthesized class declaration has no source span — 2026-11-11

**Status:** PARTIALLY FIXED for qualified generated declarations. The local-class
fixture now advances to the separate math source-mapping guard. Full Script
TeX-port qualification and the native production migration remain open.

With the staged counter implementation, `profile.expand_with_kernel`
completes expansion of `test/lambda/latex/fixtures/local_packages/document.tex`
with authorized `lmdreport.cls` and `lmdnotes.sty` snapshots. Digestion stops
with `latex-digest-unsupported`, `declaration source mapping`. The class
loader emits a synthesized `\documentclass{article}` whose tokens have no
source file, span, or main-file read offset; `group_raw` therefore cannot
construct the native declaration's argument descriptor. Reproduce with
`../lambda-input/temp/expl3-script/counter-fixture-prefix.ls`; the observed
tokens and issue are in `counter-fixture-prefix.out`. The general output
reconstruction and source-segment mapping must cover generated declarations
as well as foreign generic commands and expanded parenthesized arguments.

2026-11-16: The Script digester now maps validated interior token positions in
raw echoes and synthesized declarations. It also detects an expanded-away
argument by the consumed token sequence, so an empty macro does not reuse the
original argument text. Direct and local-resource regression cases are in
`test_tex_script_generated_declarations.ls/.txt`; raw options whose lexical
identity cannot be proven remain explicitly unsupported. The native path is
unchanged. See the current checkpoint for parity matrices and remaining gates.


## Generic argument end offsets ignore reconstructed segment boundaries — 2026-11-11

**Status:** FIXED for the tested direct-argument boundaries. The native
production path is unaffected; general expanded-output reconstruction remains
open.

In `\begin{document}\newcounter{p}\newcounter{c}[p]\stepcounter{p}\arabic{c}\end{document}`,
the direct `stepcounter` argument ends at source offset 62, but native maps
its reconstructed-output boundary to offset 71, the next synthesized token's
main-file read position. The script previously returned 62. An empty macro
between a direct command argument and the next direct token produces the same
class of mismatch: reconstruction maps the boundary to the next nonadjacent
source segment. Evidence and native AST comparisons are in
`../lambda-input/temp/expl3-script/counter-ast-detail.out` and
`counter-boundary-probe.out`. The digester maps a boundary to the next
synthetic token's read offset, or to the next nonadjacent direct token's
position, and keeps the original end for adjacent direct segments. The installed
`test_tex_script_counters` covers direct, empty-expansion, synthetic,
parenthesized and optional boundaries in both backends. More general
expanded-output source reconstruction remains open.


## Script digester omits native TeX diagnostic metadata — 2026-11-11

**Status:** OPEN — metadata representation and propagation are incomplete.
**Full script TeX-port blocker:** yes, for AST parity when TeX reports
diagnostics. Native production parsing is unaffected.

For `\begin{document}\newcounter{n}\setcounter{n}{1\relax A}\arabic{n}\end{document}`,
native LaTeX parsing attaches `tex_diagnostics` containing a
`latex-counter-value` diagnostic and a `tex_packages` attribute to the root
element. The staged script expansion records the diagnostic code in its
state, but `digest.digest` returns an AST without those attributes; full AST
equality fails even when the document children agree. Reproduce using
`../lambda-input/temp/expl3-script/counter-relax-detail.ls`; the output is
recorded in `counter-relax-detail.out`. The script engine currently keeps only
string diagnostic codes in several paths, so reproducing native structured
messages and source positions requires a general diagnostic representation
and mapping rather than adding empty attributes.

For clean documents that loaded local packages, the script digester now attaches
`tex_diagnostics: []` and the ordered local `tex_packages` list. The installed
`test_tex_script_package_metadata` compares a real package fixture and a document
without packages against native in interpreter and JIT, and focused forced-GC
checks pass. The experimental reconstructed path now achieves full native AST
equality on the real local-class fixture when diagnostics are empty. Structured
diagnostics and their file/line/offset metadata remain OPEN for other codes.

The script now also records complete native-compatible `tex-errmessage` entries
for messages up to 511 UTF-8 bytes without NUL, including main-file,
`\scantokens`, and local-package source locations. The digester attaches them
only when every diagnostic has a complete record. Installed tests compare
messages, offsets, filenames, lines, and ASTs in interpreter and JIT, with
forced-GC checks. Mixed diagnostic codes and oversized messages remain visibly
incomplete in `test_tex_script_errmessage_diagnostics`; see
`../lambda-input/temp/expl3-script/errmessage-structured-qualified-interp.out`.

Out-of-range `\char` now also records the native `tex-bad-char` message and
location. Installed comparisons cover negative and oversized values, multiple
errors, and mixed `tex-bad-char`/`tex-errmessage` ASTs; both backends and
forced-GC checks pass. Other diagnostic types remain OPEN.


## Script TeX output streams are unsupported — 2026-11-11

**Status:** FIXED for the existing native input engine's modeled stream behavior.
Both engines discard written file contents; actual filesystem output is not
implemented by this compatibility model.

Previously `\immediate\openout0=unused.txt \immediate\write0{secret}`
stopped with `tex-unsupported-primitive` for `openout`. The script now scans
filenames, tracks the 16 output stream slots, implements `openout` and
`closeout` (including their immediate and aliased forms), and suppresses
terminal output for writes to open streams. The installed
`test_tex_script_output_stream` compares 12 stream, grouping, number-wrapping,
filename, and terminal-buffer cases with native in interpreter and JIT. Focused
forced-GC tests pass in both backends. The original reproduction is in
`../lambda-input/temp/expl3-script/stream-output-probe.out`.

## Script class and counter echoes omit consumed source bytes — 2026-11-11

**Status:** OPEN — the installed script emits the explicit tokens of direct
main-file class and counter declarations, but does not retain native echo spans.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed ASTs
and source offsets. Native production parsing is unaffected. S1.8 and D10.2.2
cover the data and input boundary.

In an isolated reconstruction prototype, `\documentclass{article}` followed
by a newline, comment or spaces produces a different AST from native because
optional-version lookahead consumed the gap. `\newcounter{n}` followed by a
newline similarly loses bytes consumed by optional-parent lookahead. The native
`echo_finish` emits the traced source span, including these consumed bytes.
The prototype also explicitly reports missing origins for macro-generated
`\stepcounter`, `\setcounter`, and `\chardef` output. Evidence, including
interpreter/JIT parity: `../lambda-input/temp/expl3-script/reparse-origin-interp.out`
and `reparse-origin-jit.out` in the same directory. These findings extend the
separately recorded package-gap and emission-origin issues; the prototype is
not installed as a production path.

## Reconstructed LaTeX comments can start an empty paragraph — 2026-11-11

**Status:** OPEN — the isolated script reconstruction and reparse prototype
cannot yet represent every comment preserved in emitted TeX source. **Full
script TeX-port blocker:** yes, for native-compatible ASTs. Native production
parsing is unaffected. S1.8 and D10.2.2 apply to the data and input boundary.

For `\documentclass{article}%x` followed by a newline and
`\begin{document}A\end{document}`, the native AST contains an empty paragraph
between the class declaration and document. The experimental reparse feeds
reconstructed bytes to the TeX reader, which discards `%x` and its line ending
and therefore silently omitted that paragraph. The prototype now explicitly
reports `latex-digest-unsupported` / `preserved comment` when skipped reader
gaps contain `%`; this conservative guard can reject cases whose AST would
otherwise match. Native examples in document content and command arguments are
recorded in `../lambda-input/temp/expl3-script/native-comment-tests.out`; the
original mismatch and guarded outcomes are in `reparse-origin-v2-interp.out`
and `reparse-origin-tests-guard-v2-interp.out` in that directory. Interpreter
and JIT agree on the focused outcome. A LaTeX-source-aware comment representation
is needed before this guard can be removed.

## Script LaTeX declarations omit trailing optional groups — 2026-11-11

**Status:** FIXED for adjacent brace and bracket groups on declarations.
Native production parsing is unaffected. S1.8 and D10.2.2 apply to the input
boundary; the broader script TeX migration remains incomplete.

For `\documentclass[11pt]{article}[2020/01/01]` followed by a document, native
places the trailing optional group in `documentclass.argument_groups` and a
`brack_group` child; the isolated reconstruction and script digest instead
emit it as paragraph text. Reproduced in both execution backends by
`../lambda-input/temp/expl3-script/reparse-origin-more.ls`, with results in
`reparse-origin-more-interp.out` and `reparse-origin-more-jit.out` beside it.
The declaration parsing branch in `lmd/package/doc/latex/tex_engine/digest.ls`
reads only the initial optional and required groups; the trailing group needs
native-compatible general argument handling.

The script digester now extends declarations through adjacent required and
optional groups, retaining group descriptors and native argument packing;
whitespace and parentheses end the declaration's group sequence. New
`test/lambda/latex/test_tex_script_declaration_groups.ls/.txt` compares ten
cases with the native AST. The new test and existing digest and provenance
goldens pass interpreter and JIT; the declaration suite also passes against
the installed package in both tiers. The separate reconstructed-comment
limitation and general emission provenance remain OPEN.

## Script LaTeX environment recognition ignores command-to-brace spaces — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary whitespace between environment commands
and braces now follows native parsing; comment gaps and other malformed or
unmatched cases can still be explicitly unsupported. **Full script TeX-port
blocker:** yes, for remaining AST parity. Native production parsing is
unaffected. S1.8 and D10.2.2 govern the data and input boundary.

Reproduction `\begin {document}\begin {quote}A\end {quote}\end {document}`
produces nested `document` and `quote` nodes in the experimental reconstructed
script AST, but four paragraph nodes in native parsing. The script digester's
`blocks` branch recognizes a control token followed by a group without checking
the raw gap. Both backends reproduce the difference in
`../lambda-input/temp/expl3-script/reparse-environment-origin-interp.out` and
`reparse-environment-origin-jit.out`. The same audit found that macro-generated
`\end{quote}` and `\end{document}` are rejected by the prototype as missing
emission origins, consistent with the separately recorded general provenance
issue. Macro-generated starts and an unmatched math definition in the test
produce native parse errors, so those cases are not evidence of script parity.



The script digester now preserves whitespace before a spaced environment
argument as paragraph content, rather than treating the command as a boundary.
`test/lambda/latex/test_tex_script_environment_gaps.ls/.txt` covers spaces,
tabs, newlines, nesting, ordinary adjacent environments, and an unclosed
environment. The installed suite and existing digest, provenance and declaration
goldens pass interpreter and JIT. Unsupported comment gaps and unmatched
endings remain visible, and the separate general provenance issue remains OPEN.

## Script TeX digestion and reconstruction follow-up — 2026-11-12

The script inline digester now consumes the `\end` command itself while
retaining its following group and whitespace, matching the native direct
parser's `parse_command` handling. The installed
`test/lambda/latex/test_tex_script_definition_end.ls/.txt` compares seven
macro-definition examples, including nested and spaced forms, with native.
The new test passes installed in interpreter and JIT; existing digest,
provenance, declaration and environment goldens also pass both backends.

The separately recorded document-tail fix cannot yet be installed without changing the
existing `test_tex_script_expansion.txt` expectation: its `latex document hooks`
case asserts the old `TAIL` value. The checkout instructions require preserving
reference results; the native-matching change stays in the temporary prototype.

The isolated reconstruction prototype now represents preserved top-level
comments immediately before block commands, including empty paragraphs and
whitespace before a comment. Eight focused examples match native in interpreter
and JIT (`../lambda-input/temp/expl3-script/reparse-comment-tests-v2-interp.out`
and `reparse-comment-tests-v2-jit.out`). It still rejects comments in groups,
math, and argument gaps, and has not been installed. This narrows but does not
close the earlier reconstructed-comment issue.

## Script TeX passes unexpanded environment names through output — 2026-11-12

**Status:** FIXED for the tested computed environment names and emitted
closings. The full script TeX port remains incomplete; native production
parsing is unaffected.
S1.8 and D10.2.2 govern the data and input boundary.

For `\def\ename{quote}\begin{document}\begin{\ename}A\end{\ename}\end{document}`,
the script computes `quote` for its own environment state but emits the raw
`\ename` argument. Native expansion instead emits `\begin{quote}` and
`\end{quote}` when direct source echo cannot represent the expansion.
Macro-generated `\end` likewise requires synthetic output at its emission
position. Native emission uses source spans only for valid direct echoes.
The staged script records those spans and emits computed forms with the
current main-source offset. Evidence: six native text comparisons in
`../lambda-input/temp/expl3-script/envstage-env-output-interp.out` and its JIT
counterpart; existing expansion, profile and provenance goldens pass in both
backends. The separate main-file tail issue remains OPEN and its existing
golden is preserved.

The script now echoes an unchanged package declaration only when every named
package remains in the passthrough set. Script-handled `expl3` is consumed,
while mixed foreign, adapter and local packages retain their proper output.
`test/lambda/latex/test_tex_script_expl3_package_echo.ls/.txt` covers eight
cases. The previously failing `test_tex_script_expl3` golden and the new test
pass against the installed package in interpreter and JIT.

Environment echo spans and synthetic expanded names are now in the installed
script engine. Six native expanded-text comparisons are covered by
`test/lambda/latex/test_tex_script_environment_output.ls/.txt` and pass in both
backends. Existing expansion, profile, provenance, counters, package metadata
and related digest goldens pass in both backends. Small focused tests for both
changes pass with every-allocation GC and freed-memory poisoning in interpreter
and JIT (`../lambda-input/temp/expl3-script/env-package-gc-{interp,jit}.out`).
The independent trailing-source and full source-mapping issues remain OPEN.

## Script LaTeX digester rejects standalone math groups — 2026-11-12

**Status:** FIXED for supported standalone math groups. The full script TeX
port and other math constructs remain incomplete. Native production parsing is
unaffected. S1.8 and D10.2.2 apply to the document and input boundary.

For `\begin{document}${a+b}$\end{document}`, native creates a `group` inside
the `inline_math` AST, while the script digester returns
`latex-digest-unsupported` / `math atom`. Empty, nested, scripted and display
math groups fail similarly. `math_atoms` handles groups as arguments to
commands and scripts but has no branch for a group used as an ordinary atom.
Eight reproductions and native ASTs are in
`../lambda-input/temp/expl3-script/math-group-before-interp.out` and the JIT
counterpart. The independently tracked repeated-script issue is unaffected.

The script `math_atoms` now uses the existing group parser and applies any
following scripts to the resulting group. The installed
`test/lambda/latex/test_tex_script_math_groups.ls/.txt` compares eight empty,
nested, scripted, adjacent, radical and display groups with native; it passes
interpreter and JIT. Existing digest, underline, active-text and definition
goldens pass both tiers. The reconstruction prototype also matches four native
ASTs for comments inside those groups; it remains uninstalled.

The reconstructed-comment prototype now retains supported comment whitespace,
handles comments in tested math formulas, and explicitly rejects unqualified
argument/definition gaps. After these changes its prior 103-case corpus matches
100 native ASTs, with repeated scripts, unknown environment options and a
multiple-comment argument gap explicitly unsupported; see
`../lambda-input/temp/expl3-script/reparse-digest-tests-mathcomments-interp.out`
and its JIT counterpart. Further malformed-input, provenance and performance
qualification is required before production use.

## Script TeX rejects native pass-through and penalty primitives — 2026-11-12

**Status:** FIXED for the listed pass-through, penalty and unskip primitives.
Other TeX operations and downstream math digestion remain incomplete. Native
production is unaffected. S1.8 and D10.2.2 govern the document and input
boundary.

`\begin{document}\noindent!\end{document}` terminates script expansion with
`tex-unsupported-primitive` for `noindent`, while native emits the command
and continues. The same happens for `indent`, fill/filneg variants, alignment
markers, discretionary hyphen and italic correction; `penalty`, `unpenalty` and
`unkern` are also unsupported. Native `do_typesetting` passes the first group
as tokens, scans and discards a penalty's integer, and ignores the last two.
Eight reproductions in both backends are recorded in
`../lambda-input/temp/expl3-script/typesetting-primitives-before-interp.out`
and its JIT counterpart. This is about preserving native expansion behavior;
layout remains Radiant's responsibility under D10.2.2.

The same primitive audit found `\mathord`, `\displaystyle`, `\limits`,
`\left`, `\over`, and their native `P_MATH_EMIT` siblings are also rejected
by the installed script engine. Native expansion emits these commands as tokens;
it does not consume their math structure at this stage. The reproductions and
native expanded text are in `../lambda-input/temp/expl3-script/typeset-all-before-interp.out`
and the JIT counterpart. A staged implementation follows the shared native
pass-through branch, with downstream math AST support tracked separately.

The script now emits the same pass-through token families as native, scans and
discards penalties, and implements `\unpenalty`, `\unkern` and `\unskip`
with native output-token behavior. The new `test_tex_script_typesetting_tokens`
`.ls/.txt` compares 18 expansion cases with native, including
math commands, aliases, numeric expressions and whitespace removal. The new
suite passes installed in interpreter and JIT; existing expansion, profile,
expl3 and environment-output goldens pass both tiers. This does not claim
downstream LaTeX AST support for every emitted math command.

## Script math digester rejects style declarations and operatorname — 2026-11-12

**Status:** FIXED for the four style declarations and supported `operatorname`
forms. Other math syntax and source-provenance cases remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After native-compatible math token emission, `\begin{document}$\displaystyle x+1$\end{document}`
reaches script digestion but returns `latex-digest-unsupported` /
`math command displaystyle`. Native emits a `style_command` node without an
argument. The other three math style declarations and `\operatorname` are
also unsupported, including the starred operator form. Six comparisons and
native ASTs are in `../lambda-input/temp/expl3-script/math-declarations-before-interp.out`
and its JIT counterpart. This is a digester issue independent of the newly
ported TeX token pass-through.

## Script math digester rejects ordinary punctuation atoms — 2026-11-12

**Status:** PARTIALLY FIXED for parentheses, brackets, comma, colon and vertical
bars. Other punctuation and multi-character relations remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After staging support for `\operatorname`, the formula
`\begin{document}$\operatorname{foo}(x)$\end{document}` still returns
`latex-digest-unsupported` / `math atom` when the script digester encounters
`(`. Native creates `punctuation` nodes for the parentheses. The script's
`math_atoms` handles letters, numbers, a few operators and relations, but no
ordinary punctuation branch. Both backends reproduce this in
`../lambda-input/temp/expl3-script/math-declarations-after-interp.out` and
its JIT counterpart. Other punctuation and multi-character relation semantics
need individual validation rather than assuming all characters are alike.

## Script math digester rejects atom classes and mathchoice — 2026-11-12

**Status:** FIXED for supported atom-class bodies and four-branch mathchoice.
Malformed arguments and operator modifiers remain incomplete. Native production
is unaffected; S1.8 and D10.2.2 apply.

After the TeX token pass-through port, `\begin{document}$\mathord{x}$\end{document}`
still returns `latex-digest-unsupported` / `math command mathord`. Native
produces a `math_atom` with a `mord` class and grouped body. `\mathop` and
`\mathchoice` are similarly unsupported; native retains the operator body and
all four choice branches. Five examples and native ASTs are recorded in
`../lambda-input/temp/expl3-script/math-atom-commands-before-interp.out` and
the JIT counterpart. Additional malformed and modifier cases need separate
qualification.

The script math digester now emits native-style declaration nodes and
`operatorname` nodes, including direct adjacent starred forms; uncertain
star provenance remains explicitly unsupported. Parentheses, brackets, comma,
colon and vertical bars are parsed as punctuation atoms and can take scripts.
The installed `test_tex_script_math_declarations.ls/.txt` compares 13 cases
with native and passes interpreter and JIT. Existing digest, math-group,
math-style and underline goldens also pass both backends. Other punctuation,
relations and the independently recorded atom classes remain open.

The script math parser now retains the native atom class and body for
`\mathord`, `\mathbin`, `\mathrel`, `\mathopen`, `\mathclose`,
`\mathpunct`, and `\mathinner`, represents `\mathop`, and preserves all four
`\mathchoice` branches. A shared argument scanner also serves fractions.
The installed `test_tex_script_math_atom_classes.ls/.txt` compares nine native
ASTs and passes interpreter and JIT. Existing digest, math-group, declaration,
style and underline goldens pass both tiers. A focused native AST comparison
passes with forced GC and freed-memory poisoning in both backends; evidence is
`../lambda-input/temp/expl3-script/math-atoms-gc-{interp,jit}.out`.

## Script math digester rejects left/right delimiter groups — 2026-11-12

**Status:** PARTIALLY FIXED for supported scalable delimiter groups, including
nested and middle delimiters. Invalid middle delimiters and other malformed
forms remain explicitly unsupported. Native production parsing is unaffected;
S1.8 and D10.2.2 apply.

`\begin{document}$\left(x+1\right)$\end{document}` now passes script TeX
expansion but digestion returns `latex-digest-unsupported` / `math command left`.
Native builds a `delimiter_group` with its left/right spellings and parsed
children. Seven native AST comparisons, including nested, named, invisible,
scripted, middle and unclosed forms, are recorded in
`../lambda-input/temp/expl3-script/math-delimiters-before-interp.out` and its
JIT counterpart. Malformed and raw delimiter behavior requires qualification
before enabling a broad implementation.

The script now matches nested `\left`/`\right` pairs, parses supported
`\middle` delimiters, retains delimiter spellings and children, applies
following scripts, and covers tested native recovery and braced-right forms.
The installed `test_tex_script_math_delimiters.ls/.txt` compares 20 native ASTs
and asserts explicit rejection for an invalid middle delimiter. It passes both
interpreter and JIT; existing digest and math goldens pass both tiers. A focused
nested delimiter AST comparison also passes every-allocation GC and poisoning
in both backends (`../lambda-input/temp/expl3-script/math-delimiters-gc-*.out`).

## Script TeX rejects native mark and special handling — 2026-11-12

**Status:** FIXED for the tested native mark and special expansion behavior.
Malformed-input recovery and the broader script TeX port remain incomplete.
Native production is unaffected; S1.8 and D10.2.2 apply.

`\begin{document}A\special{color push}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `special`, while native expands and discards
the special's token list and continues with `AB`. `\mark`, indexed `\marks`,
and all five ordinary and indexed mark queries are likewise unsupported.
Native discards expanded mark contents, returns empty mark queries, and scans
an integer for indexed forms; it does not perform page building. Nine examples,
including nested expansion and aliases, are in
`../lambda-input/temp/expl3-script/mark-special-before-interp.out` and its JIT
counterpart. This concerns native expansion compatibility, not page layout.

The script now scans expanded mark/special text without executing its
unexpandable tokens, consumes the index for `\marks` and indexed queries,
and returns the native empty mark result. The installed
`test_tex_script_marks_special.ls/.txt` compares 12 native expanded outputs and
checks that discarded messages are not executed. It passes interpreter and JIT;
existing expansion, profile, expl3, token, environment and digest goldens pass
both tiers. A focused every-allocation-GC/poison check also passes both backends
(`../lambda-input/temp/expl3-script/marks-special-gc-{interp,jit}.out`).

## Script TeX rejects explicit glue and kern commands — 2026-11-12

**Status:** FIXED for the tested forms. Full script-port qualification remains
open; S1.8 and D10.2.2 apply.

`\begin{document}A\hskip1pt!B\end{document}` stops script expansion with
`tex-unsupported-primitive` / `hskip`. Native scans the glue and emits
`\hspace{1.0pt}` in this case. `\vskip`, `\kern`, `\mkern` and `\mskip`
are also unsupported; native uses math-sensitive command names, formatted
scanned values, and direct source echo when its trace is valid. Nine native
examples, including flexible glue, aliases, registers and expressions, are in
`../lambda-input/temp/expl3-script/spacing-command-before-interp.out`.
Successful implementation must preserve direct-echo spans and synthetic output
origins rather than replacing every case with a fixed spelling.


The script now scans and emits these glue and kern commands, retains captured
direct tokens and source spans when native echo is valid, and synthesizes
math-sensitive output otherwise. The installed
`test_tex_script_spacing_commands.ls/.txt` compares 23 native cases, including
comments, UTF-8 offsets, aliases, expressions, and a dimension longer than 64
tokens. It passes interpreter and JIT; focused forced-GC and freed-memory-poison
checks pass both. Existing expansion, stream, trace, profile, provenance, digest
and package tests pass with the staged candidate in both backends. Malformed
input recovery and release-profile performance qualification remain open.

## Script TeX dimension scanner lacks font-relative and pixel units — 2026-11-12

**Status:** FIXED for the tested units and supported font parameters. Full
script-port qualification remains open; S1.8 and D10.2.2 apply.

The script dimension scanner rejects `em`, `ex` and `px` as
`tex-illegal-unit`. For example, `\pdfpxdimen=2pt \dimen0=1.5px
\the\dimen0` yields `3.0pt` from the native TeX parser but script expansion
stops. A selected font's metrics likewise determine `em` and `ex`. The
reproduction and native comparisons are in
`../lambda-input/temp/expl3-script/font-unit-before-interp.out`. Native also
tries these units before physical units; partially matched keyword tokens are
restored as non-direct tokens, which can invalidate a source-echo trace. The
script currently omits those probes and preserves the direct flags on matched
prefixes. This additionally affects the spacing-command work recorded above.

The script scanner now reads the selected font's em/ex parameters and the
`pdfpxdimen` value, and restores partially matched keyword prefixes with the
native non-direct provenance. The installed `test_tex_script_font_units.ls/.txt`
compares ten native cases, including assigned and negative font metrics, pixel
values, case variants and math units. It passes interpreter and JIT, including
focused forced-GC and freed-memory-poison runs. The existing expansion golden also passes with the candidate in both backends.

## Script TeX keyword scanner rejects character category codes accepted by native — 2026-11-12

**Status:** FIXED for tested keyword characters. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

``\catcode`p=7 \dimen0=1pt \the\dimen0`` yields `1.0pt` from the native parser without a diagnostic, while the
script scanner returns `tex-illegal-unit`. The executable reproduction and output
are `../lambda-input/temp/expl3-script/unit-catcode-check.ls` and
`unit-catcode-check.out`. Native `scan_keyword` matches character values without
requiring letter/other categories; script `scan_word` currently requires those
categories. This also affects other keyword-using primitives, so changes need
regression checks beyond dimension scanning.


The script keyword scanner now matches character values without restricting
their category, in line with native `scan_keyword`. The installed
`test_tex_script_keyword_catcodes.ls/.txt` compares six native dimension, glue
and arithmetic cases and passes interpreter and JIT. Focused forced-GC and
freed-memory-poison tests, spacing comparisons, and existing expansion, stream,
trace, profile, digest, provenance and expl3 goldens pass in both backends.

## Script TeX rejects discretionary replacement text — 2026-11-12

**Status:** FIXED for tested well-formed groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\discretionary{-}{z}{bc}D\end{document}` stops script
expansion at `tex-unsupported-primitive` / `discretionary`. Native expansion
consumes the first two unexpanded token groups and processes the third group,
producing `\begin{document}AbcD\end{document}`. A macro-containing case and
native output are in `../lambda-input/temp/expl3-script/discretionary-before.ls`
and `discretionary-before-interp.out`. The script implementation needs to
preserve token-list expansion and source provenance, and report unsupported
recovery where malformed input is not yet handled.


The script now scans three unexpanded token groups, discards the first two, and
executes stored replacement tokens with non-direct provenance. The installed
`test_tex_script_discretionary.ls/.txt` compares 13 native cases including
macros, nested groups, aliases, Unicode, assignments and messages. It passes
interpreter and JIT, as do focused forced-GC/poison and existing expansion,
stream, trace, profile, digest, provenance, expl3 and spacing checks. Missing
opening braces still return an explicit issue rather than native recovery.

## Script TeX rejects numeric accent primitive — 2026-11-12

**Status:** PARTIALLY FIXED for tested expansion and digest forms. Full script
TeX-port qualification remains blocked on unsupported accent arguments and
source reconstruction. Native production parsing is unaffected; S1.8 and
D10.2.2 apply.

`\begin{document}\accent19 e\end{document}` stops script expansion with
`tex-unsupported-primitive` / `accent`, while native emits `\'{e}`. The native
engine maps selected accent numbers to LaTeX commands, scans an optional
character or character definition, and emits the character unaccented for an
unknown accent number. Reproduction and examples are in
`../lambda-input/temp/expl3-script/accent-before.ls` and
`accent-before-interp.out`. Successful support needs native-compatible
character-number scanning, lookahead, and synthetic-output provenance.


The script now scans numeric accent and character arguments and emits the native
command mapping with synthetic origins. Installed `test_tex_script_accent.ls/
.txt` compares 26 native outputs and diagnostic records in interpreter and JIT;
focused forced-GC/poison checks and existing character, diagnostic, expansion,
profile, digest, provenance, stream and expl3 goldens pass both. The script digester now builds supported text accent nodes, including the
first example, and the installed `test_tex_script_accent_digest.ls/.txt`
compares 28 native ASTs and renders `é à č` to HTML. It passes interpreter and
JIT, as do focused forced-GC/poison and math, underline, digest, provenance and
package regressions. Comment-adjacent arguments, control-sequence arguments and
certain emitted syntax characters return explicit unsupported results; native
behavior for comment gaps is documented in
`../lambda-input/temp/expl3-script/accent-digest-probe-v3-interp.out`. General
emitted-source reconstruction and full accent coverage remain open.


A staged control-argument candidate matches native for `\'\i!`, `\'\%`,
and nested accents (`../lambda-input/temp/expl3-script/accent-args-probe-interp.out`).
The installed `test_tex_script_accent_digest` golden explicitly asserts that
the first two forms are unsupported, so the checkout rule preserving existing
assertions and reference results prevents installing that behavior without an
explicit user ruling. The candidate remains in
`../lambda-input/temp/expl3-script/accent-control-candidate-home`; nested accents
are handled independently.



Nested accents are now supported by a shared accent-node parser. The installed
`test_tex_script_nested_accents.ls/.txt` compares six native ASTs and checks the
remaining comment-gap rejection; it passes interpreter and JIT, as do the
existing accent golden, math, digest and package regressions and focused
forced-GC/poison checks. The separate control-argument limitation remains.

## Script TeX rejects insert and vertical adjustment groups — 2026-11-12

**Status:** PARTIALLY FIXED for tested groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\insert0{hidden}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `insert`; `\vadjust` similarly stops. Native
runs the group with ordinary scoping and then discards its output, while global
assignments still take effect. Three native comparisons are in
`../lambda-input/temp/expl3-script/insert-before.ls` and
`insert-before-interp.out`. A correct port must execute the body, manage group
scope and aftergroup actions, and remove only the produced output; simply
skipping group tokens would lose side effects.


The script now executes these groups with normal scoping, preserves global and
aftergroup effects, and discards their emitted output at group close. Installed
`test_tex_script_insert.ls/.txt` compares 24 native outputs including nested
groups, macros, math, messages, and an early document end. It passes interpreter
and JIT, as do focused forced-GC/poison and existing expansion, discretionary,
spacing, digest, provenance, math and package regressions. Cross-boundary
`\unskip` returns an explicit unsupported issue, and malformed-opening-brace
recovery is incomplete (`../lambda-input/temp/expl3-script/insert-rejection-probe.out`).

## Script TeX rejects rule primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested rule forms. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hrule width1pt height2pt depth3pt!B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `hrule`; native emits
`\rule{1.0pt}{5.0pt}`. A macro-generated `\vrule` emits the native default
`\rule{0.4pt}{1em}`. Native scans optional dimension keywords in any order,
uses source echo when its trace is valid, and otherwise synthesizes a rule.
Reproductions are in `../lambda-input/temp/expl3-script/rule-before.ls` and
`rule-before-interp.out`. The script port needs correct scanning, echo spans,
and synthetic source origins.


The script now scans and emits rules with native source echo or synthesized
output. Installed `test_tex_script_rules.ls/.txt` compares 19 native expanded
outputs, and `test_tex_script_rule_digest.ls/.txt` compares 17 native ASTs and
a rendered HTML rule. Both pass interpreter and JIT, as do focused forced-GC/
poison checks and existing insert, spacing, digest, math and package regressions.
An echoed rule containing a comment can lose whitespace in direct digestion;
the script explicitly reports `rule comment source gap` instead. The mismatch
and rejection are reproduced in
`../lambda-input/temp/expl3-script/rule-digest-stage-check-interp.out` and
`../lambda-input/temp/expl3-script/rule-comment-gap-probe.out`. General
comment reconstruction and malformed dimension recovery remain open.

## Script TeX rejects leader primitives — 2026-11-12

**Status:** PARTIALLY FIXED for rule and inline-box leaders. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\leaders\hrule\hskip2cm!B\end{document}` stops script
expansion with `tex-unsupported-primitive` / `leaders`. Native consumes the
rule specification and then processes the following glue, emitting
`\hskip2cm`. A second case using `\cleaders\vrule` is in
`../lambda-input/temp/expl3-script/leaders-before.ls` and
`leaders-before-interp.out`. Native also permits box leaders, executing and
discarding their box contents before processing the following glue. The script
port needs both rule and box behavior, with scoped execution and source fidelity.


The script now consumes rule leaders and executes/discards `hbox`, `vbox` and
`vtop` leader bodies, including their hooks, scoping and global effects. The
installed `test_tex_script_leaders.ls/.txt` compares 24 native outputs and
passes interpreter and JIT; focused forced-GC/poison and existing rule, insert,
digest, math and package regressions pass both. Leader boxes obtained from
registers, copies or splits remain explicitly unsupported, as does malformed
box recovery. Box-leader side effects were reproduced in
`../lambda-input/temp/expl3-script/leader-box-before-interp.out`.

## Script TeX rejects inline box primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested inline boxes. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hbox{Hi}B\end{document}` stops script expansion at
`tex-unsupported-primitive` / `hbox`; native emits `\mbox{Hi}`. A vertical box
emits a grouped body. Reproductions are in
`../lambda-input/temp/expl3-script/inline-box-before.ls` and
`inline-box-before-interp.out`. Correct support must scan box specifications,
execute scoped bodies and box hooks, and preserve synthetic-output origins.
Box registers, copying, splitting and movement require additional work.


A staged inline-box expansion matches native output in the initial cases. Its
direct vertical-box digester currently loses a space after a skipped comment:
`\vbox{a%note\n b}` yields `ab` where native yields `a b` (reproduction in
`../lambda-input/temp/expl3-script/inline-box-digest-probe.out`). Synthetic
horizontal boxes also lack command source mapping. Both need explicit handling
or rejection before installing the expansion.


A category-code probe found an additional horizontal-box argument mismatch:
when `$` has category 12, native records escaped `\$` in the argument's raw
source but a first candidate recorded `$`. The staged digester now rejects
this source shape until token serialization is available. Before and after
evidence is `../lambda-input/temp/expl3-script/inline-box-catcode-probe.out`
and `inline-box-catcode-probe-v2.out`.


The script now expands inline boxes with scoped bodies and hooks, and digests
supported simple horizontal and vertical boxes. Installed
`test_tex_script_inline_box.ls/.txt` compares 17 native expanded outputs;
`test_tex_script_inline_box_digest.ls/.txt` compares 13 native ASTs and renders
HTML. Both pass interpreter and JIT, as do focused forced-GC/poison and existing
leader, rule, insert, math and package regressions. Comment-sensitive vertical
boxes, macro-expanded horizontal bodies and special category-code characters
return explicit unsupported results pending source reconstruction
(`../lambda-input/temp/expl3-script/inline-box-unsupported-probe.out`,
`inline-box-catcode-probe-v2.out`).

## Script TeX rejects box registers and retrieval — 2026-11-12

**Status:** PARTIALLY FIXED. `\setbox`, `\box`, `\copy`, unboxing, splitting
and `\lastbox` are installed for the qualified cases described in the current
checkpoint. Box dimensions, some box consumers and source mapping remain open.
Native production parsing is unaffected; S1.8 and D10.2.2 apply.

`\setbox0=\hbox{Hi}\begin{document}A\copy0!\box0!\copy0 B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `setbox`. Native stores
the box body, copies it once, consumes it once, and leaves the final copy empty.
A related `\ifvoid` example and output are in
`../lambda-input/temp/expl3-script/box-register-before.ls` and
`box-register-before-interp.out`. The port needs group-scoped box assignment,
copy versus consume semantics, box conditionals, hooks and source provenance;
splitting, unboxing and movement also remain to be qualified.


An initial register candidate matched the tested output and scope cases, but
replaying `\setbox0=\hbox{a%note\n b}` into the script digester produced
`ab` where the native AST contains `a b`. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe.out`. Box replay
needs to preserve or explicitly reject unqualified source gaps before the
register implementation can be installed.


Further staged AST comparisons found a definition argument ending at the wrong
source offset when a box assignment separates it from the next emitted input,
and a synthesized rule replay whose final argument end should map to the next
emitted source segment. The output text matches native; ASTs differ. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe-v3.out`. General
definition reconstruction is already incomplete; the candidate must reject
unqualified definition/replay combinations and fix or reject the rule mapping.


The definition-offset mismatch also occurs with an empty assigned box and no
replay: `../lambda-input/temp/expl3-script/box-assignment-definition-probe.out`
shows native mapping the definition argument end to the next emitted document
segment, while the candidate used the original closing brace. The staged
digester must account for assignments, not just replay markers, when it reports
this unqualified reconstruction.


A staged register candidate now matches 26 native expanded-output examples and
eight native AST examples in interpreter and JIT, with a focused forced-GC/
poison check. Existing `test_tex_script_expansion.ls:134` and
`test_tex_script_profile.ls:13` explicitly use `\setbox0=\hbox{}` as an
unsupported primitive and failed preload. Their unchanged `.txt` references
therefore fail in both backends when the candidate correctly accepts it. The
then-current checkout rule prevented installation. The later parity continuation
replaced those unsupported placeholders with `halign`, preserving the tests'
unsupported-primitive purpose, and installed the revised, independently checked
box implementation. The original temporary candidate is no longer present.
Replayed synthetic rules remain explicitly guarded for their unresolved source
offset; broader source reconstruction remains open.

## Script TeX rejects box dimension access — 2026-11-12

**Status:** PARTIALLY FIXED for the expansion, state and document cases in the
current checkpoint. Full corpus, malformed-input and production qualification
remain open. Native production parsing is unaffected; S1.8 and D10.2.2 apply.

With the staged box-register implementation,
`\setbox0=\hbox{Hi}\setbox1=\copy0\wd1=2pt` stops at
`tex-unsupported-primitive` / `wd`. Native reports both `\wd0` and `\wd1` as
`2.0pt`, because the copied registers refer to the same box object. Evidence is
`../lambda-input/temp/expl3-script/box-dimensions-before.ls` and
`box-dimensions-before-interp.out`. Correct support needs native-compatible
shared-box mutation, scanning, scoped references, and dimension diagnostics.

The Script engine now keeps dimensions by box identity so copied registers and
saved scopes observe the same object mutation. Reads and assignments scan the
same numeric and dimension operands as native, including the one-time
approximate-metrics diagnostic. The current checkpoint records the differential
fixtures, document-tree matrix and remaining qualification.


## Script TeX rejects paragraph pattern and shape assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed expansion. AST source mapping,
some quantity-read ASTs and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

The script profile stops with `tex-unsupported-primitive` for `\patterns`,
`\hyphenation`, `\parshape`, and `\interlinepenalties`. For example,
`\begin{document}A\patterns{a1b}B\end{document}` expands natively to
`\begin{document}AB\end{document}`. Reproduction and native outputs are in
`../lambda-input/temp/expl3-script/paragraph-assign-before.ls` and
`paragraph-assign-before.out`. The native engine consumes the pattern text
without expansion and scans shape dimensions or penalty integers without storing
layout state; parity requires preserving scanning and assignment side effects.

An experimental implementation matches 25 native expansion cases, but its
initial AST comparisons expose three source-mapping failures: a prior echoed
definition gets the wrong end offset, a nested formatting argument loses a
`\hyphenation` node, and math source includes a consumed penalty assignment.
See `../lambda-input/temp/expl3-script/paragraph-digest-probe-interp.out` for
actual and native trees. These AST mismatches block claiming complete support;
the digester must reject affected inputs until source reconstruction is fixed.
Malformed numeric and brace recovery remains separate from the well-formed
assignment subset.

The script implementation scans pattern and hyphenation text without executing
it and consumes paragraph-shape dimensions and the four penalty lists, including
prefixes and `\afterassignment`. The installed
`test_tex_script_paragraph_assignments.ls/.txt` compares 25 text and diagnostic
cases with native behavior and checks that pattern contents do not execute. Both
backends pass, as do 23 focused regressions (the fixture-dependent test was
rerun after correcting its isolated working directory) and sampled forced GC
with freed-memory poisoning. To avoid emitting incorrect ASTs, digestion now
explicitly rejects documents that execute these assignments until the mapping
gaps above are repaired. `\the\parshape` and `\the\clubpenalties1` were unsupported while native
returns zero; the original reproduction is
`../lambda-input/temp/expl3-script/paragraph-query-probe.ls` and `.out`.

The Script engine now reads those quantities in `\the`, numeric expressions,
conditionals and dimension factors, scanning the required penalty index. The
new `test_tex_script_paragraph_quantities.ls/.txt` compares 20 expansion and
diagnostic cases, and three supported ASTs, against native in interpreter
and JIT. Both backends also pass 24 focused regressions and sampled forced GC
with freed-memory poisoning. Math with a generated quantity and combinations
with echoed definitions report unsupported where source mapping remains
unqualified; assignment-containing documents retain the broader guard above.


## Script LaTeX definition offsets omit following consumed assignments — 2026-11-12

**Status:** PARTIALLY FIXED for the cases covered by the later definition-
boundary and following-group checkpoints. Broader source mapping and corpus
qualification remain open; S1.8 and D10.2.2 apply.

With `\def\x{Hi}\count0=0\begin{document}A\x B\end{document}`, Script
digestion reports the definition body's argument end as byte 10, while native
reports byte 19, the start of the next emitted segment. `\dimen0=1pt` similarly
reports 10 versus 21. The Script result is marked successful despite the AST
mismatch. Reproduction and both trees are in
`../lambda-input/temp/expl3-script/definition-assignment-mapping-probe.ls` and
`.out`. A candidate for paragraph quantity reads also exposes this existing
problem. The later definition-boundary work maps the following reconstructed
segment, and the current checkpoint records native equality for the original
reproductions and the subsequent following-group work.


## Script TeX rejects font hyphen and skew characters — 2026-11-12

**Status:** PARTIALLY FIXED for direct font identifiers. Family identifiers,
AST source mapping and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

`\the\hyphenchar\nullfont` and `\the\skewchar\nullfont` stop Script
expansion at `tex-unsupported-primitive`; native emits `45` and `-1`. Font
property assignments such as `\hyphenchar\f=63` likewise stop, whereas
native updates the selected font and reads back `63`. Reproduction is
`../lambda-input/temp/expl3-script/font-character-before.ls` and `.out`.
The implementation needs native font identity, defaults, property mutation,
scoping and quantity scanning rather than treating the properties as text.

A staged candidate matches 20 native expansion and diagnostic examples,
including shared font identity, defaults and group behavior. Its initial AST
probe produces incorrect successful trees for an assignment inside a formatting
argument and for an assignment following an echoed definition; see
`../lambda-input/temp/expl3-script/font-character-digest-probe.out`. The latter
also occurs for ordinary register assignments (the separate definition-offset
issue above). The candidate must report unsupported for unqualified AST source
mappings; full source reconstruction remains open.


Script now tracks these properties on shared font identities, preserves native
defaults for new fonts and nullfont, and supports assignment and reads through
`\the`, numeric expressions, conditions and dimension factors. The new
`test_tex_script_font_characters.ls/.txt` compares 20 expansion/diagnostic
cases and two supported ASTs to native in interpreter and JIT. Both backends
pass 26 focused regressions and sampled forced GC with poisoning. Digestion
explicitly rejects property assignments or unqualified definition combinations,
and math source mapping remains unsupported. Font-family identifiers are
tracked separately below.

## Script TeX rejects math font family assignments and identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for family assignments and font-identifier scans.
Direct font-token `\the`, AST source mapping and malformed-input recovery
remain open; S1.8 and D10.2.2 apply.

`\textfont0=\f` and `\scriptfont2=\f` stop Script expansion at
`tex-unsupported-primitive`; native accepts them. With the staged font-property
work, `\the\hyphenchar\textfont0` reports `tex-missing-font`, while native
reads the default null font and emits `45`. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-family-before.ls` and `.out`. Correct
support needs family-slot assignment, index scanning and font identifiers in
numeric and `\the` contexts, including font identity and source mapping.

A staged family-slot candidate matches 16 native expansion and diagnostic
examples, including index wrapping, aliases, shared font identity and global
effects. Removing its provisional AST guard reproduces incorrect successful
trees when a family assignment occurs inside formatting or after a definition;
see `../lambda-input/temp/expl3-script/font-family-digest-probe.out`. These
source-mapping cases must remain explicitly unsupported until reconstruction
is qualified. Direct `\the\textfont` font-token production is not yet ported.


Script now scans and assigns all three math font families, including wrapped
indices, aliases, shared font identity and native group behavior. It resolves
family identifiers for font properties and font dimensions. The new
`test_tex_script_font_families.ls/.txt` compares 16 expansion/diagnostic cases
and two supported ASTs with native in interpreter and JIT; 27 focused
regressions and sampled forced GC with poisoning pass both. Family assignments
explicitly reject AST digestion while source mapping remains unqualified.
Direct font-token production through `\the` remains the separate issue below.

## Script TeX cannot expand font identifiers through the — 2026-11-12

**Status:** PARTIALLY FIXED for expansion and experimental text reconstruction.
Font-token AST source mapping and production integration remain open; S1.8
and D10.2.2 apply.

`\the\font`, `\the\nullfont` and `\the\f` stop Script expansion as
unsupported, while native produces the font's original control-sequence token.
For example, after `\font\f=cmr10 at 13pt`, `\edef\x{\the\f}` stores
`\f`, not the literal font name. Redefining that control sequence affects
subsequent use of the returned token. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-the-before.ls`, `font-the-before.out`,
and `font-family-the-probe.out`. Correct support needs original font identifier
identity and `\the` token-list expansion semantics, including `\edef`.

The staged candidate in `../lambda-input/temp/expl3-script/font-the-stage-home`
returns the original font token for direct use, but `\edef` cases expose two
additional mismatches: the echoed definition is lost when tracing sees the
generated font token, and in-document `\meaning` emits a raw backslash where
native serializes a text-backslash command. Sixteen comparisons are in
`test_tex_script_font_tokens.ls`, with actual/native output in
`font-token-mismatch-probe.out`. That earlier candidate remained uninstalled while the general source-trace and
text serialization causes were investigated.


The Script engine now preserves original font control-sequence identity for
`\the` of named, current, null and math-family fonts, including `\edef` and
later redefinitions. Generated tokens are distinguished in source tracing.
`test_tex_script_font_tokens.ls/.txt` compares 17 native text/diagnostic cases
and verifies safe AST handling in interpreter and JIT. The experimental
`reconstruct.ls` is used for those text comparisons; the existing digester
returns unsupported for font-token ASTs until mapping is qualified.

## Script LaTeX meaning output does not serialize backslash characters — 2026-11-12

**Status:** PARTIALLY FIXED in the experimental reconstructor. Production
integration and complete source-map qualification remain open; S1.8 and
D10.2.2 apply.

For `\def\x{\y}\begin{document}\meaning\x\end{document}`, Script
expansion emits a raw backslash before `y`, while native serializes it as
`\textbackslash{}y`. The Script result is marked successful. A macro meaning
with only ordinary characters matches. Reproduce with
`../lambda-input/temp/expl3-script/meaning-serialization-probe.ls` and `.out`.
The evidence points to generated character serialization in the LaTeX profile;
the exact general cause and all affected character categories are unconfirmed.


The experimental Script `reconstruct.ls` canonicalizes generated characters
and matches native output for the reproduction above. It is a separate API,
not yet the production LaTeX path; direct expanded tokens remain tokens.
The new `test_tex_script_reconstruction.ls/.txt` checks 91 native text cases
in both backends, including this one, comments, boxes, rules, accents and
assignments. Offset-map and full-corpus qualification remain open.

## Script TeX reconstruction loses numeric accent text segments — 2026-11-12

**Status:** FIXED for tested numeric accent reconstruction in the experimental
Script reconstructor. Full reconstruction qualification remains open; S1.8
and D10.2.2 apply.

Native `\accent19 \char123` and `\accent19 \char92` emit a single raw
text segment, including the character. The staged Script reconstructor treats
the characters as individual synthetic tokens and escapes them, producing
`\'{\{}` and `\'{\textbackslash{}}` rather than the native output.
Reproduction, all 51 comparisons and actual/native text are in
`../lambda-input/temp/expl3-script/reconstruct-conformance-b.ls` and
`reconstruct-conformance-b-interp.out`. The native `do_accent` calls
`emit_text`; the script must retain that segment boundary and origin.


The Script expansion output now retains native accent text-segment boundaries
and the experimental reconstructor emits them as one segment. Both examples
are included in the 91-case native comparison test.

## Script LaTeX font-token expansion loses nested and math source mapping — 2026-11-12

**Status:** OPEN in the staged font-token implementation. Blocks AST parity
for documents using font-valued `\the`; S1.8 and D10.2.2 apply.

The staged engine correctly reconstructs expansion text for direct
`\the\nullfont`, but its AST silently differs from native when that command
appears in math or a formatting argument. See
`../lambda-input/temp/expl3-script/font-token-digest-probe.ls` and `.out` for
actual and native trees. Source mapping for font-token expansion must report
unsupported until the native digestion behavior is ported or reconstructed.


## Script TeX executes assignments inside constructor arguments — 2026-11-12

**Status:** OPEN. Blocks script-port output, state and AST parity for affected
documents; S1.8 and D10.2.2 apply. Production native parsing is unaffected.

For `\count0=0\begin{document}\textbf{\global\count0=7 X}\the\count0\end{document}`,
the Script engine executes the assignment, removes it from the output and
emits `7`; native leaves the assignment in the constructor argument and emits
`0` afterwards. Optional constructor arguments have the same defect. The
`\hyphenation` nested formatting discrepancy and several font-related AST
mismatches arise from this difference. Actual/native text and controls are in
`../lambda-input/temp/expl3-script/constructor-arg-mode-before.ls` and `.out`.
Native `arg_mode` in `input-tex-main.cpp` expands macros in constructor
arguments while passing unexpandable primitives and assignments through, with
special handling for nested constructors, environments and raw arguments.
Correct support must preserve those semantics and source provenance.

A staged constructor-argument implementation in
`../lambda-input/temp/expl3-script/constructor-arg-stage-home` matches the four
reproductions above, including state and optional arguments. Its unchanged
regressions for `test_tex_script_paragraph_assignments`,
`test_tex_script_font_characters`, and `test_tex_script_font_families` now
fail because those existing references explicitly expect unsupported ASTs for
these nested cases. The corrected candidate returns native-matching ASTs for
all three; the paragraph AST was compared directly in interpreter and JIT in
`../lambda-input/temp/expl3-script/constructor-paragraph-ast.ls` and `.out`. Failing comparisons are
`constructor-arg-regression-{paragraph_assignments,font_characters,font_families}-interp.out`;
the paragraph failure also reproduces in JIT. Checkout rules require preserving
test assertions and reference results, so this blocks installation without a
user ruling. The candidate also still needs complete environment/raw-command
handling and qualification; no golden was changed.

The same state error also occurs in arguments of unknown passthrough commands:
`\unknown{B\global\count0=7 C}` executes the assignment in Script, while
native preserves it and leaves the register unchanged. Evidence is
`../lambda-input/temp/expl3-script/unknown-arg-before.ls/.out`. Native main
control applies argument mode to both undefined controls and constructors. The
existing staged candidate matches this example in the interpreter; see
`../lambda-input/temp/expl3-script/unknown-arg-staged.out`. It still needs
rebasing, JIT qualification, and environment and raw-argument handling.

The rebased prototype at `../lambda-input/temp/expl3-script/constructor-current-stage-home`
also handles tested passthrough environment options and unknown commands in
inline math; see `constructor-current-probe2.out`. It retains passthrough
nested environment names and explicitly rejects unqualified raw commands and
custom environments inside an argument. The latter is blocked by input issue
[LI-017](../Lib_Input_Issue_Ledger.md#li-017), which causes
native to emit its internal environment-end marker. Test evidence is
`constructor-env-probe.ls`, `.out`, and `constructor-env-probe2.out` in the
same temporary directory. Interpreter and JIT now agree on 17 of 20 combined
LaTeX probes; the other three explicitly reject raw commands or LI-017's custom
environment. Four plain-TeX cases also match in both backends; see
`constructor-pure-probe2.out` and `constructor-pure-probe2-jit.out`. The
candidate remains uninstalled, and the reference result conflict described
above remains unresolved.

The rebased stage initially lost main-file origin on a synthetic opening brace
read as a constructor argument from a `\read` stream, causing
`tex-reconstruct-missing-origin` in `script_read_scope`; evidence is
`constructor-read-detail2.out` in the same temporary directory. A shared
emission-origin helper now annotates such delimiters, and the existing nine-case
`test_tex_script_read` matches its preserved reference in both backends
(`constructor-read-fixed2-interp.out` and `constructor-read-fixed2-jit.out`).

A further rebase at `../lambda-input/temp/expl3-script/constructor-verb-stage-home`
uses the installed direct-source `\verb` reader inside constructor arguments.
Nine native comparisons cover braces, comments, optional arguments, Unicode,
starred and aliased forms in interpreter and JIT; evidence is
`constructor-verb-probe.ls` and `constructor-verb-probe-{interp,jit}.out`.
Nine focused existing references, including `\read` and `\verb`, pass in both
backends; the three paragraph/font references still fail only because they
expect unsupported results. Logs and preserved actual outputs are under
`../lambda-input/temp/expl3-script/constructor-verb-regressions-{interp,jit}.log`
and `constructor-verb-regression-{interp,jit}/`. The candidate still
explicitly rejects custom environments affected by LI-017 and remains
uninstalled because of the preserved-reference conflict.

The latest rebase at `../lambda-input/temp/expl3-script/constructor-random-stage-home`
retains the installed identity and deterministic-random features. Nine further
native comparisons cover random expansion and seed assignments, `\jobname`,
`\fontname`, `\the` and inline `\verb` inside arguments; all match in both
backends. Evidence is `constructor-random-probe.ls` and
`constructor-random-probe-random-{interp,jit}.out` in the same directory.
The prior command and inline-verb probes also continue to match.

The current rebase at `../lambda-input/temp/expl3-script/constructor-shipout-stage-home`
also matches six native cases in both backends where movement or shipout appears
inside arguments, including nested assignments and verbatim content. Evidence is
`constructor-shipout-probe.ls` and `constructor-shipout-probe-{interp,jit}.out`.
The staged fix remains subject to the preserved-reference and LI-017 limits.


## Script TeX rejects letterspacefont alias assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments outside constructor
arguments. AST source mapping and malformed-input recovery remain open; S1.8
and D10.2.2 apply.

`\font\f=cmr10 at 13pt\letterspacefont\g\f 100` stops the Script
profile at `tex-unsupported-primitive`, while native defines `\g` as the
same font identity as `\f`. Native reads its sixth font dimension as
`13.0pt` and `\ifx\f\g` succeeds; nullfont aliases also work. Reproduction
and native outputs are in `../lambda-input/temp/expl3-script/letterspacefont-before.ls`
and `.out`. Native scans and discards the spacing integer, preserving font
identity; Script support needs that scanning, scope and source handling.


Script now scans the font and spacing expression and assigns a scoped alias to
the existing font identity. The new `test_tex_script_letterspacefont.ls/.txt`
compares 18 native text and diagnostic cases and checks safe AST outcomes in
both backends, including global definitions and `\afterassignment`. Thirteen
focused regressions and sampled forced GC with poisoning pass both. AST
digestion explicitly reports unsupported for assignments, because unguarded
nested and definition cases differ from native; see
`../lambda-input/temp/expl3-script/letterspacefont-digest-probe.out`. The
separate constructor-argument defect affects assignments inside arguments.

## Script TeX rejects interaction and page-state primitives — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments and queries outside
constructor arguments. AST source mapping and malformed-input recovery remain
open; S1.8 and D10.2.2 apply.

`\the\interactionmode`, `\interactionmode=3`, `\batchmode`,
`\the\pagegoal`, and `\pagegoal=1pt` stop Script expansion at
`tex-unsupported-primitive`. Native currently reports interaction mode `1` and
page dimensions `0.0pt` even after assignment, because its input engine does
not model interaction state or a page builder; it still scans assignment
values. Evidence is `../lambda-input/temp/expl3-script/page-state-before.ls`
and `.out`. Script parity must scan values and preserve assignment side effects
and diagnostics rather than omit the commands or their inputs.


Script now models the native engine's fixed interaction/page results and scans
assignment values, including prefixes, aliases and `\afterassignment`. The
new `test_tex_script_page_state.ls/.txt` compares 23 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. Unguarded nested
and definition assignments produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/page-state-unguarded-probe.out`. The
digester reports unsupported for those unqualified assignment mappings, and
the separate constructor-argument defect affects nested assignments.

## Script expl3 rejects token-list append and prepend APIs — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed `Nn` and `Nx` variants outside
constructor arguments. AST source mapping and invalid-target recovery remain
open; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_put_right:Nn`, `\tl_put_left:Nn` and
`\tl_put_right:Nx` stop at `tex-unsupported-primitive`; native produces
`AB`, `BA`, and an expanded token-list result respectively. Reproduction
and native text are in `../lambda-input/temp/expl3-script/expl3-tl-put-before.ls`
and `.out`. Support needs token-level concatenation, local/global scope,
`n` versus `x` expansion and source/diagnostic behavior.


A staged implementation matches 20 native text and diagnostic cases in both
backends. Unguarded digestion still returns incorrect successful ASTs for
nested constructor arguments and assignments following definitions; see
`../lambda-input/temp/expl3-script/expl3-tl-put-digest-probe.out`. The staged
digester must report unsupported for these unqualified source mappings.

Script now supports local/global append and prepend, preserving token bodies
and `n` versus `x` expansion. `test_tex_script_expl3_tl_put.ls/.txt` compares
20 native text/diagnostic cases and checks safe AST handling in interpreter and
JIT. Thirteen focused regressions and sampled forced GC with poisoning pass
both. The digester explicitly rejects token-list mutations until source mapping
is qualified; constructor arguments remain a separate issue.

## Script expl3 omits standard scratch token lists — 2026-11-12

**Status:** FIXED for the four standard local/global scratch token lists.
Other expl3 initialization remains incomplete; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_use:N\l_tmpa_tl` and
`\tl_use:N\l_tmpb_tl` fail with `expl3-undefined-variable`; native expl3
provides these as initially empty token lists. Reproduction and native text
are in `../lambda-input/temp/expl3-script/expl3-scratch-before.ls` and `.out`.
Correct initialization must preserve standard scope and duplicate-declaration
behavior rather than treating every missing token list as empty.


Script expl3 initialization now provides `\l_tmpa_tl`, `\l_tmpb_tl`,
`\g_tmpa_tl` and `\g_tmpb_tl` as empty token lists, without treating other
undefined names as empty. The new token-list test covers all four and scoped
mutation; existing expl3 tests continue to pass in both backends.

## Script expl3 rejects token-list emptiness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for tested `N` and `n` branch variants. AST
source mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_empty:NTF` and `\tl_if_empty:nTF` stop Script expansion at
`tex-unsupported-primitive`, while native selects the true branch for an empty
list and the false branch for a nonempty one. Reproduction and native output
are in `../lambda-input/temp/expl3-script/expl3-tl-empty-before.ls` and `.out`.
The implementation must scan both branches, expand only the selected branch,
and preserve token-list and grouping semantics.

The staged conditional initially chose correct branches but echoed an `\edef`
that native suppresses after expanding expl3's internal macro tokens. Both `N`
and `n` cases reproduce this in
`../lambda-input/temp/expl3-script/expl3-tl-empty-trace-probe.out`. Source
tracing must model that internal expansion origin; the uncorrected staged
result is not qualified.

A second staged probe found that checking only the token-list body is
insufficient for `N` conditionals: native compares the complete TeX meaning
to the empty token-list meaning. Empty long/protected macros, macros with
parameter prefixes, `\relax` and undefined controls select false. Evidence
is `../lambda-input/temp/expl3-script/expl3-tl-empty-meaning-probe.out` and
`expl3-tl-empty-meaning-native.out`. The candidate must compare meanings.

An unguarded staged AST comparison also returns an incorrect successful
definition-offset tree after a token-list conditional; see
`../lambda-input/temp/expl3-script/expl3-tl-empty-digest-probe.out`. The
conditional must remain guarded in the current digester until source mapping
is qualified.


Script now provides `NTF`/`NT`/`NF` and `nTF`/`nT`/`nF` variants, comparing
complete TeX meanings for `N` and unexpanded tokens for `n`. Internal
macro-expansion provenance is retained for definition tracing. The new
`test_tex_script_expl3_tl_empty.ls/.txt` compares 31 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. AST digestion
explicitly reports unsupported until conditional source mapping is qualified.

## Script expl3 rejects token-list blankness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `n` and `V` branch variants. AST source
mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_blank:nTF` stops Script expansion at `tex-unsupported-primitive`.
Native selects true for a token list consisting only of spaces, false for a
letter or for an unexpanded control sequence even if that macro expands to
empty. Evidence is `../lambda-input/temp/expl3-script/expl3-tl-blank-before.ls`
and `.out`. Correct support requires token-category-aware whitespace tests and
normal conditional branch and source-trace behavior.

The staged `nTF`/`nT`/`nF` implementation matches 17 native text and
diagnostic examples. Without the existing conditional source-map guard, an
conditional following an echoed definition yields an incorrect successful AST;
see `../lambda-input/temp/expl3-script/expl3-tl-blank-digest-probe.out`. The
staged digester retains the explicit guard until mapping is qualified.

The common `VTF` variable variant is also unsupported in the staged `n`
implementation. Native checks the variable's stored tokens without expanding
control sequences inside it; evidence is
`../lambda-input/temp/expl3-script/expl3-tl-blank-v-before.ls` and `.out`.

Script now implements the `n` variants with category-aware space checks,
unexpanded token semantics and macro-origin tracing. The new
`test_tex_script_expl3_tl_blank.ls/.txt` compares 17 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fifteen focused
regressions and sampled forced GC with poisoning pass both. The digester
continues to reject unqualified conditional source mappings.


The Script engine now also implements `VTF`/`VT`/`VF`, checking the stored
tokens without expanding embedded controls. `test_tex_script_expl3_tl_blank_v.ls/.txt`
compares 19 native text/diagnostic cases and checks safe AST outcomes in both
backends. Sixteen focused regressions and sampled forced GC with poisoning pass
both; unsupported variable types and conditional source mapping remain bounded.

## Script TeX rejects input-stream EOF conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for opened and closed captured-resource streams.
Stream AST source mapping remains open; S1.8 and D10.2.2 apply.

`\ifeof0 Y\else N\fi` stops Script expansion at
`tex-unsupported-primitive`, while native selects `Y` for an initially closed
stream. Nested and `\unless` examples show the same gap; evidence is
`../lambda-input/temp/expl3-script/ifeof-before.ls` and `.out`. Native scans a
stream number, diagnoses out-of-range values and tests the stream's actual
open/EOF state; a partial port must not assume open streams are closed.

The staged closed-stream candidate matches 15 native text/diagnostic cases,
but unguarded AST comparisons still differ for math and a preceding echoed
definition. Evidence is `../lambda-input/temp/expl3-script/ifeof-digest-probe.out`.
The current digester must explicitly reject those unqualified conditional source
mappings until the native mapping is reproduced.


Script now tracks input-stream state and implements `\ifeof`, including
stream-number diagnostics, nested conditions and `\unless`. Captured-resource
opening, closing, `\readline` and `\read` now update this state.
`test_tex_script_ifeof.ls/.txt` compares 15 native text and diagnostic cases and checks safe AST outcomes in both backends. Seventeen
focused regressions and sampled forced GC with poisoning pass both. The
digester explicitly rejects EOF conditional source mapping for now.

## Script TeX rejects input stream opening and closing — 2026-11-12

**Status:** PARTIALLY FIXED for captured-resource opening and closing. Stream
reading is supported for tested valid UTF-8 resources; AST source mapping
remains open. S1.8 and D10.2.2 apply.

`\openin0=stream_fixture` stops the Script engine at
`tex-unsupported-primitive`. Native opening a fixture relative to the input
file changes the following `\ifeof0` result to false; a missing file leaves
the stream closed. The isolated reproduction uses
`../lambda-input/temp/expl3-script/openin-main.tex`, `stream_fixture.tex`,
`openin-file-native.ls/.out`, and `openin-before.ls/.out`. Native source
`do_open_stream` scans the stream number and file name, tries the exact name
and `.tex`, and keeps state until closed or read past EOF. Script must use the
authorized captured-resource provider rather than assuming file availability;
the initial candidate did not yet port `\read` and `\readline`.

A staged captured-resource implementation matches a file-backed native fixture
covering existing and missing files, `.tex` fallback, empty files, wrapped
stream numbers, grouping, closing and scanner lookahead. Without a digestion
guard, opening after a definition and closing inside a formatting argument
produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/openin-digest-probe.out`. The separate
constructor-argument defect contributes to the nested case.


Script now opens and closes streams through the existing captured-resource
provider, trying exact and `.tex` names, tracking empty files and wrapped stream
numbers, and preserving native scan order. The new file-backed
`test_tex_script_openin.ls/.txt` compares native text/diagnostics across
opening, missing files, grouping, empty files and closing. Both backends pass
18 focused regressions and sampled forced GC with poisoning. The digester
reports unsupported for unqualified stream command mappings; `\read` remains
separate work.

## Script TeX rejects readline from input streams — 2026-11-12

**Status:** PARTIALLY FIXED for valid UTF-8 captured resources. Malformed
inputs, unsupported endline characters and AST mapping remain open; S1.8 and
D10.2.2 apply.

With the staged `\openin` implementation, `\readline0 to\first` stops
Script expansion at `tex-unsupported-primitive`. Native reads and trims a
line, stores other-category character tokens in a control sequence, and closes
the stream only after a subsequent read passes EOF. The fixture with two lines
and an EOF check is `../lambda-input/temp/expl3-script/openin-test-cwd/test/`
`lambda/latex/fixtures/local_packages/script_readline.tex` and
`script_stream_data.dat`; reproduction and native output are
`../lambda-input/temp/expl3-script/readline-before.ls` and `.out`. Support
must preserve input bytes, endline handling, stream position, scope,
`\afterassignment` and diagnostics. Ordinary `\read` remains separate.


Script now reads physical lines, trims trailing spaces, emits other-category
character tokens, tracks stream position and EOF, honors endline settings,
scopes assignments and preserves `\afterassignment`. Six file-backed native
fixtures compare text and diagnostics in interpreter and JIT, covering CRLF,
UTF-8, special characters, category behavior, scope and terminal reads.
Nineteen focused regressions and sampled forced GC with poisoning pass both.
Invalid UTF-8, surrogate endline characters, malformed headers and stream AST
mapping remain explicitly unsupported; tokenized `\read` is tracked below.

## Script TeX rejects tokenized input-stream reads — 2026-11-12

**Status:** PARTIALLY FIXED for tested valid UTF-8 captured resources. Invalid
UTF-8, surrogate endline characters, malformed headers and stream AST source
mapping remain unsupported; S1.8 and D10.2.2 apply.

With staged stream opening and `\readline`, `\read0 to\first` stops Script
expansion at `tex-unsupported-primitive`, while native reads a line, tokenizes
it using current category codes and stores it in a control sequence.
Reproduction is `../lambda-input/temp/expl3-script/read-before.ls/.out` with
`openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read.tex`
and `script_stream_data.dat`. Native continues across lines while braces remain
unbalanced, so a correct port must retain tokenization, balance, EOF, scope,
`\afterassignment`, source provenance and diagnostics.

Script now tokenizes lines with the current category codes and continues while
braces remain unbalanced. It preserves stream state, scope, `\afterassignment`,
reused synthetic source provenance and the tested native diagnostics. Nine
file-backed fixtures compare native text and diagnostics in interpreter and JIT,
including multiline and mixed `\read`/`\readline` input. Twenty focused
regressions and sampled forced GC with poisoning pass both backends. The
digester continues to reject unqualified stream AST mappings.


## Script TeX reconstruction errors on stale partial UTF-8 spans — 2026-11-12

**Status:** FIXED for tested stale partial UTF-8 spans. Full reconstruction
mapping remains experimental; S1.8 and D10.2.2 apply.

The staged `\read` implementation reproduces native reuse of one synthetic
read buffer. In `script_read_mixed.tex`, an earlier token span later points into
a changed UTF-8 line and slices through a multibyte character. The installed
Script `reconstruct.ls` propagates `E318` from UTF-8 decoding instead of
falling back to the token's canonical serialization as native does. The
file-backed fixture and failure are
`../lambda-input/temp/expl3-script/openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read_mixed.tex`
and `../lambda-input/temp/expl3-script/read-final4-interp.log`. The Script
reconstructor must treat an undecodable candidate span as non-faithful; a
required undecodable source gap still needs an explicit issue.

The reconstructor now falls back to canonical token serialization when a
candidate source span cannot be decoded; an undecodable required gap remains an
explicit `tex-reconstruct-missing-origin` issue. Two focused cases and all 91
existing native text comparisons pass in interpreter and JIT, including
sampled forced GC with poisoning for the focused cases.


## Script TeX rejects expl3 token-list equality conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `NN` and `nn` variants. Other variants and
conditional AST source mapping remain open; S1.8 and D10.2.2 apply.

`\tl_if_eq:NNTF\l_tmpa_tl\l_tmpb_tl{T}{F}` and
`\tl_if_eq:nnTF{A}{A}{T}{F}` stop Script expansion with
`tex-unsupported-primitive`, while native selects the true branch. Native
also distinguishes a braced group from its unbraced contents. The comparisons
and diagnostic evidence are `../lambda-input/temp/expl3-script/tl-eq-before.ls`
and `.out`. The implementation must preserve TeX token identity, category
codes, full meanings for N arguments, conditional branch selection and source
mapping; AST qualification remains separate.

Script now implements `\tl_if_eq:NNTF/NNT/NNF` and `nnTF/nnT/nnF`.
The N variants compare complete TeX meanings and the n variants compare token
identity without expansion. Thirty-seven native text and diagnostic cases and
three safe AST checks cover branch selection, scope, category codes and `\edef`
in interpreter and JIT, including forced GC with poisoning. Conditional AST
mapping remains explicitly guarded.


## Script TeX rejects expl3 token-list stringification — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `V` inputs.
Unsupported variable types and stringification AST mapping remain open; S1.8
and D10.2.2 apply.

In Script expl3 mode both variants stop with `tex-unsupported-primitive`, while
native produces string tokens. Native output also serializes control sequences,
groups and spaces before emitting TeX-safe text. Evidence:
`../lambda-input/temp/expl3-script/tl-to-str-before.ls/.out`. A port needs
native token/category and serialization behavior, including variable lookup and
source mapping, not only ordinary text examples.

The Script port serializes tokens into other-category characters and spaces,
with variable lookup and the tested `\edef` source tracing. Forty-one native
text and diagnostic comparisons and three guarded AST checks pass interpreter
and JIT, as do 19 focused regressions and 13 sampled forced-GC/poison cases.
Tests include category-sensitive
characters, control sequences, scoping, escape characters, empty values and
nested definitions. The temporary trace exemption is cleared when a generated
token is stored in a definition, so its later macro expansion retains its
correct foreign origin.


## Script TeX rejects expl3 token-list counting — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `N` inputs. Other
variable types and AST source mapping remain open; S1.8 and D10.2.2 apply.

Both variants stop Script expansion with `tex-unsupported-primitive`. Native
counts `ABC` as three items, ignores a top-level space, and counts a braced
group as one item. Reproduction and native results are
`../lambda-input/temp/expl3-script/tl-count-before.ls/.out`. A correct port
must preserve unexpanded TeX token and group semantics, variable lookup, and
source tracing; AST mapping needs independent qualification.

The Script port counts unexpanded items, skipping top-level space tokens and
treating balanced groups as one item. Thirty-seven native text/diagnostic
comparisons and three guarded AST checks pass in interpreter and JIT, including
nested groups, scope, macro meanings, `\edef` and a 256-token input. Thirteen
sampled GC/poison cases and ten focused regressions pass both backends.


## Script TeX rejects raw inline verbatim commands — 2026-11-12

**Status:** PARTIALLY FIXED for direct-source, terminated `\verb` with an
ASCII delimiter. Unterminated input, non-ASCII delimiters, macro-sourced
commands, `\lstinline` and other raw forms, and AST mapping remain open;
S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\verb|a_% b|`,
`\verb*+x y+` and `\lstinline|a_% b|`; native retains the raw bytes and
continues after the closing delimiter. Evidence is
`../lambda-input/temp/expl3-script/verb-before.ls/.out`. A correct port must
read from the active physical source without interpreting TeX category codes,
respect direct-source eligibility, delimiters, reader position, source spans,
EOF diagnostics and constructor argument mode. Raw environments and command
variants need separate qualification.

The staged direct-source implementation found a reconstruction defect for raw
spans emitted from an included file: converting an echoed span to a temporary
source span dropped its main-file emission origin, so reconstruction returned
`tex-reconstruct-missing-origin`. The failing comparison is
`../lambda-input/temp/expl3-script/verb-final2-interp.out`. The current staged
reconstructor now retains the source token's emission origin. All 91 existing
native reconstruction comparisons pass in interpreter and JIT. Raw spans also
remain distinct from echoed definition spans.

The Script reader now captures the tested `\verb` spans from direct main and
included sources without tokenizing their contents, and advances the active
physical reader. Sixteen inline native text/diagnostic cases and one file-backed
case pass in interpreter and JIT, along with eight focused regressions and
sampled forced-GC/poison checks.
Tests cover Unicode content, special TeX characters, aliases, line endings,
source-span classification, and explicitly unsupported boundaries. The
digester rejects raw inline source mapping until it is qualified.

Native LaTeX AST comparison for valid verbatim containing `%` or an apparent
`\end{document}` is additionally blocked by input issue
[LI-018](../Lib_Input_Issue_Ledger.md#li-018): native TeX
preserves those spans, but its LaTeX environment scan interprets their contents.


## Script TeX rejects expandable engine identity primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested identity expansion. Font-name AST
source mapping and downstream malformed-input recovery remain open; S1.8 and
D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\jobname`,
`\fontname\nullfont` and `\pdftexrevision`, while native expands them to
`texput`, `nullfont` and `27` respectively for string input. Reproduction and
native diagnostics are `../lambda-input/temp/expl3-script/fontname-before.ls`
and `.out`. A port must preserve source-dependent job names, font identity and
size formatting, expanded-token behavior and diagnostics. File-backed cases
and AST source mapping need separate qualification.

Script now expands `\jobname` from the explicit `base_path` option (or
`texput` when absent), `\fontname` from the selected font identity and size,
and `\pdftexrevision` to the native revision. Thirty-three string-input and
four file-backed native text/diagnostic comparisons pass in interpreter and JIT,
including included-file job names. Font-name AST source mapping remains guarded;
other tested AST outcomes match native. Sixteen focused regressions and sampled
forced-GC/poison checks pass both backends.


## Script TeX stops on missing font identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for font-identifier scanning and tested
`\fontname` recovery. Downstream numeric and dimension recovery remains open;
S1.8 and D10.2.2 apply.

`\fontdimen2\bad` and `\hyphenchar\bad=3` stop Script expansion at
`tex-missing-font`, while native reports the diagnostic, pushes the offending
token back, uses `nullfont`, and continues with further diagnostics and
output. Native `scan_font_ident` in `lambda/input/input-tex-scan.cpp` implements
that recovery. `../lambda-input/temp/expl3-script/font-recovery-before.ls/.out`
contains the native and Script evidence, including a `\fontname` lookahead case
in the staged identity implementation. Correct support must preserve input
position, diagnostic metadata, downstream scans and group behavior.

The shared font scanner now records `tex-missing-font`, restores the offending
token and returns `nullfont`. Native comparisons cover undefined and character
identifiers, EOF, definition lookahead and included-file transitions in both
backends. Later numeric and dimension scans can still stop on their own errors;
those diagnostic recovery paths remain open.


## Script TeX retains a stale source line after main input EOF — 2026-11-12

**Status:** FIXED for tested main-input EOF diagnostics and active included
readers. Broader malformed-input recovery remains open; S1.8 and D10.2.2 apply.

With staged `\fontname` and missing-font recovery, the source `\fontname`
produces the correct `tex-missing-font` text and offset, but Script attaches
`line: 1` while native has no line attribute. Evidence is
`../lambda-input/temp/expl3-script/fontname-eof-detail.ls/.out` and
`fontname-eof-state.ls/.out`. `stream.next` retains the previous root reader
when `reader.next` returns null, and `record_diagnostic` consequently mistakes
that stale reader for an active source. Correct recovery must distinguish an
exhausted main reader from a still-active included reader and preserve native
metadata without suppressing the diagnostic.

The stream now records root EOF separately while retaining its source data;
active included readers remain visible. Diagnostic recording omits the stale
line at exhausted root EOF and preserves a line when a scan resumes in an
included or parent file. The identity tests cover these cases and raw-reader
eligibility in interpreter and JIT, including sampled forced GC with poisoning.


## Script TeX rejects deterministic pdfTeX random primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested deterministic random and seed behavior.
Malformed numeric recovery and AST source mapping remain open; S1.8, S12.1.1v2
and D10.2.2 apply.

The Script engine reports `tex-unsupported-primitive`, while native returns 27
for the default-seeded `\pdfuniformdeviate 100`, 95 after seed 1, zero for
`\pdfnormaldeviate`, and 12345 for the initial `\pdfrandomseed`. Evidence:
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native uses a
32-bit deterministic linear congruential sequence, an explicitly settable
seed, and a fixed zero normal deviate. Correct support must preserve state,
scanning, grouping, arithmetic, expansion and diagnostic behavior.

Script now keeps native's 32-bit state, using exact-range intermediate products,
including seed assignment, zero and negative bounds, grouped and nested scans,
and the native fixed-zero normal deviate. Sixty-one string-input comparisons,
one included-file comparison, five safe AST checks, 18 focused regressions and
sampled forced-GC/poison tests pass in interpreter and JIT. The digester
explicitly rejects unqualified random and timer source mappings.

## Script TeX rejects pdfTeX timer primitives — 2026-11-12

**Status:** FIXED for the native input engine's zero-valued timer model.
AST source mapping remains guarded; S1.8 and D10.2.2 apply.

`\pdfresettimer\the\pdfelapsedtime` stops Script at
`tex-unsupported-primitive`, while native emits zero. Reproduction is
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native's timer model
must be verified before porting its observable behavior.

The native input engine returns zero for elapsed time and ignores reset; Script
now reproduces that deterministic model. Numeric and `\the` uses are included
in the native comparison suite.


## Script TeX loses definition echo for the-expansion — 2026-11-12

**Status:** FIXED for tested numeric, dimension, glue and token-register
`\the` values in echoed definitions. Broader reconstruction remains open;
S1.8 and D10.2.2 apply.

For `\edef\x{\the\count0}\x`, Script reconstructs `0` while native
reconstructs the original definition followed by `0`. The same discrepancy
occurs for `\tolerance`, `\inputlineno`, `\eTeXversion` and `\dimen0`;
`\number\count0` correctly suppresses that echo. Evidence is
`../lambda-input/temp/expl3-script/the-edef-before.ls/.out`. Numeric and
dimension tokens produced by `\the` lack the internal-origin trace exemption
already used by font-token `\the`; a fix must cover relevant quantities and
clear the exemption when tokens enter a stored definition.

The same defect affects `\the\toks0`: characters, control sequences and
nested groups from a token register suppress an echo that native retains. See
`../lambda-input/temp/expl3-script/the-toks-detail.ls/.out`. The staged fix
also marks these directly inserted tokens as internal to the current trace;
`the-toks-detail2.out` matches native for four cases in the interpreter.

Numeric, dimension and glue text and token-register contents now retain their
internal trace origin only while scanning the current definition. Tests cover
nested definitions, `\number`, macros in register indices, and global, long and
protected prefixes in both backends. They are part of the same native
comparison and focused-regression runs above.


## Script TeX rejects box movement primitives — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes and AST source mapping remain open; S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for these commands.
Native turns `\raise1pt\hbox{B}` into `\raisebox{1.0pt}{B}`, negates the
dimension for `\lower`, and emits the moved inline box's contents for
`\moveleft` and `\moveright`. Evidence is
`../lambda-input/temp/expl3-script/move-before.ls/.out`. A correct port must
scan the dimension and box, preserve grouping, hooks and source origins, and
handle registered boxes without assuming they are empty; AST mapping requires
separate qualification.

Script now scans movement dimensions and executes inline box bodies with the
native wrapper or discarded shift, preserving group scope, hooks and source
origins. Thirty-five string-input and one included-file native comparisons,
three safe AST checks, 16 focused regressions and 16 sampled forced-GC/poison
cases pass in interpreter and JIT. Registered-box movement remains explicitly
unsupported; the digester guards unqualified movement source mapping.


## Script TeX rejects shipout of inline boxes — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes, cross-boundary `\unskip` and AST source mapping remain open;
S1.8 and D10.2.2 apply.

`\shipout\hbox{B}` stops Script with `tex-unsupported-primitive`, while the
native input engine discards the box's emitted contents and continues; global
assignments within the box still take effect. Evidence is
`../lambda-input/temp/expl3-script/shipout-before.ls/.out`. A correct port
must preserve box grouping, hooks, assignment effects, diagnostics, and source
mapping; registered boxes require the separate box-register implementation.

Script now executes inline boxes and discards their emitted material, retaining
global assignments, grouping, hooks, terminal messages and diagnostics. Twenty-one
string-input native comparisons, four message/diagnostic comparisons, one
included-file comparison, three safe AST checks, 16 focused regressions and
sampled forced-GC/poison checks pass in interpreter and JIT. Cross-boundary
`\unskip` remains explicitly unsupported as tracked below and in LI-019.


## Script TeX rejects cross-boundary unskip in discarded material — 2026-11-12

**Status:** OPEN. Blocks output parity when `\unskip` inside discarded insert,
vadjust, leader or shipout material removes preceding output; S1.8 and D10.2.2
apply.

For `\begin{document}A \insert0{\unskip}B\end{document}`, Script explicitly
reports `tex-unsupported-primitive` / `insert cross-boundary unskip`, while
native emits `AB`. The same occurs for `\vadjust` and discarded leader boxes;
the inline `\shipout` slice has the same limit. Evidence is
`../lambda-input/temp/expl3-script/discard-unskip-before.ls/.out` and
`shipout-edge-probe.ls/.out`. The discard scope stores an output-length
boundary and cannot currently represent removal of pre-existing output without
corrupting its later truncation. A fix needs to preserve native mutation and
nested discard behavior rather than dropping the error.

A staged boundary-adjustment candidate matches 16 of 18 native text cases but
differs on two nested/echo cases; it also exposes unqualified AST mappings.
The native differences are caused by input issue
[LI-019](../Lib_Input_Issue_Ledger.md#li-019): native leaves
stale discard boundaries after the removal and can leak discarded output.
Evidence is `../lambda-input/temp/expl3-script/discard-unskip-detail.ls/.out`,
`discard-unskip-detail.log` and `discard-unskip-ast-probe.out`. The candidate
is uninstalled; the existing explicit unsupported result remains in place.


## Script TeX rejects vcenter boxes — 2026-11-12

**Status:** PARTIALLY FIXED for tested `\vcenter` expansion and grouping.
Math/document AST source mapping and broader group semantics remain open; S1.8
and D10.2.2 apply.

Script expansion stops with `tex-unsupported-primitive`, while native emits a
`\vcenter{...}` wrapper and processes its body; an optional `to` dimension is
scanned rather than emitted. Evidence is
`../lambda-input/temp/expl3-script/vcenter-before.ls/.out`. A correct port must
preserve box specification scanning, grouping, body effects, hooks and source
mapping. Math and document AST digestion require separate qualification.

An isolated digester experiment can recognize the native math `box_transform`
shape, but six probes remain explicitly unsupported because the synthetic
wrapper lacks qualified math or command source mapping. Evidence is
`../lambda-input/temp/expl3-script/vcenter-ast-probe.ls/.out`. The engine slice
keeps an explicit AST guard until reconstruction is qualified.

Script now scans optional size specifications and emits the native wrapper
while preserving tested scope and body effects without injecting `\everyvbox`.
Twenty-six string-input and one included-file native comparisons, three safe AST
checks, 17 focused regressions and sampled forced-GC/poison tests pass both
backends. The digester explicitly guards unqualified `\vcenter` source mapping.


## Script TeX stops on native-diagnosed numeric math commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script now scans tested numeric operands for
`\mathchar`, `\delimiter`, `\radical` and `\mathaccent`, reports the native
diagnostic and continues. S1.8 and D10.2.2 apply. Numeric-math AST source mapping
and malformed operand recovery remain unsupported; the native input engine also
does not render these numeric math characters.

Previously Script reported `tex-unsupported-primitive` and stopped. Native scans the numeric
operand, reports `tex-unsupported-mathchar` with source metadata and continues
with subsequent output. Reproduction and diagnostic records are
`../lambda-input/temp/expl3-script/math-number-before.ls/.out`. A compatible
port must preserve operand expansion and scanning, diagnostics, source
locations and continued processing; native does not render these numeric math
characters, and broader math support remains separate. The port matches 32 string
cases and one included-file case, with three guarded AST checks, 17 focused
regressions and sampled forced-GC checks in both execution backends. See
`test/lambda/latex/test_tex_script_numeric_math.ls`.


## Script TeX rejects native inspection commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script handles tested `\show`, `\showbox`,
`\showlists`, `\showgroups`, and `\showifs` cases; S1.8 and D10.2.2 apply.
Inspection AST source mapping, registered boxes, and malformed operand recovery
remain unsupported. The separate `\showthe` issue records its tested support.

Previously Script reported `tex-unsupported-primitive`. Native reports a token's meaning
for `\show`, prints void-box information for `\showbox`, and treats the three
list/group/conditional inspection commands as no-ops in this input model.
Reproduction, native messages, text and diagnostics are
`../lambda-input/temp/expl3-script/show-before.ls/.out`. A correct port needs
token identity, register scanning, terminal buffering and box-state behavior;
`\showthe` remains a separate inspection operation. Fifty native string
comparisons, one included-file comparison, three guarded AST checks, 21 focused
regressions and sampled forced-GC tests pass both execution backends; see
`test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports dimension and glue register meanings — 2026-11-12

**Status:** FIXED for tested dimension and glue register aliases in `\meaning`
and `\show`; S1.8 and D10.2.2 apply.

For `\dimendef\d=3 \meaning\d`, Script previously output `undefined`, while the
native engine prints the meaning `\dimen3`. Similarly `\skipdef` and
`\muskipdef` aliases incorrectly reported `undefined` instead of `\skip5`
and `\muskip7`. Reproduction and output are
`../lambda-input/temp/expl3-script/meaning-probe.ls/.out`. The shared Script
`print_meaning` previously omitted dimension and glue register kinds; native
`print_meaning` handles all register kinds. The shared implementation now
handles them, covered by `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports LaTeX constructor meanings — 2026-11-12

**Status:** FIXED for tested LaTeX constructor meanings in `\meaning` and
`\show`; S1.8 and D10.2.2 apply.

For `\show\section`, Script previously printed `> \section=\section.` while native
prints `> \section=\protected macro:->\section .`. The same missing
protected-macro representation affects constructor symbols such as `\%`.
Evidence: `../lambda-input/temp/expl3-script/show-extra-probe.ls/.out`. The
Script `print_meaning` constructor branch previously omitted the native protected
macro wrapper and control-sequence spacing; it now reproduces both. Tests are
in `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX rejects showthe — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\showthe` quantities and token lists are
supported; inspection AST source mapping and malformed operand recovery remain
unsupported. S1.8 and D10.2.2 apply.

Previously Script stopped with `tex-unsupported-primitive`. Native prints the
scanned internal quantity or token-list value and continues: `\count0=12
\showthe\count0` prints `> 12.`, while `\toks0={abc} \showthe\toks0`
prints `> abc.`. Evidence: `../lambda-input/temp/expl3-script/showthe-before.ls/.out`.
The shared Script scanner now captures the result without expanding token-list
contents, including empty lists and nested `\the` expressions. Forty-one string
cases, one included file, two guarded AST checks, 29 existing regressions and
sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_showthe.ls`.


## Script LaTeX digester misinterprets generated meaning text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\meaning` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\meaning\section\end{document}`, Script expansion
matches native text, but digestion returns successful paragraph text containing
literal backslashes. Native produces `textbackslash` nodes with source offsets.
The same mismatch occurs for dimension and glue register aliases and constructor
symbols. Four native comparisons are recorded in
`../lambda-input/temp/expl3-script/meaning-ast-probe.ls/.out`. The Script
digester consumes generated character tokens directly and does not reparse
native escaped output; the required source mapping remains unimplemented. The
guard is reset after kernel preload and preserves ordinary text digestion. See
`test/lambda/latex/test_tex_script_generated_text_digest.ls`.


## Script LaTeX digester misinterprets generated string text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\string` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\string\foo\end{document}`, Script returns a successful
AST with a literal `\foo` paragraph string, while native creates a
`textbackslash` node and text `foo`. `\string\%` has the same mismatch; a
plain `\string A` matches native. Evidence:
`../lambda-input/temp/expl3-script/string-ast-probe.ls/.out`. Generated
character tokens bypass the native text escaping and source-offset mapping.
The guard and ordinary text cases are covered by
`test/lambda/latex/test_tex_script_generated_text_digest.ls`; 12 focused
regressions and sampled forced-GC checks pass in both backends.


## Script expl3 integer variables and comparisons are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Script now supports tested `\int_abs:n`,
`\int_min:nn`, `\int_max:nn`, `\int_compare:nNnTF/T/F` with direct
relations or zero-argument macros expanding to one relation, and chained
`\int_compare:nTF/T/F` expressions. Integer variables, predicate variants,
more complex `nNn` relations, delimiter-consuming operands, overflow recovery
and integer AST source mapping remain open; S1.8 and D10.2.2 apply.

With `script_expl3: true`, Script previously stopped with `tex-unsupported-primitive` for
`\int_new:N`, `\int_set:Nn`, `\int_zero:N`, `\int_incr:N`,
`\int_compare:nNnTF`, `\int_compare:nTF`, `\int_abs:n`, and
`\int_max:nn`. Native produces the expected values in seven comparisons,
including local scratch variables. Evidence:
`../lambda-input/temp/expl3-script/int-api-before.ls/.out`. A port needs
register identity and scoping, expression parsing and expansion behavior,
conditional branch semantics, and guarded AST source mapping. The installed
numeric helpers share the integer-expression scanner with `\int_eval:n` and
match 51 string cases, one included file, two guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both backends. See
`test/lambda/latex/test_tex_script_expl3_int_math.ls`. The direct-relation
comparison forms match 38 string cases, one included file, two guarded AST
checks, 13 focused regressions and sampled forced-GC checks in both backends;
see `test/lambda/latex/test_tex_script_expl3_int_compare.ls`. Zero-argument
relation macros that expand to a single `<`, `=` or `>` token are now supported:
34 string cases, one included file, three bounded unsupported cases, three safe
AST checks, 19 focused regressions and sampled forced-GC checks pass both
backends; see `test/lambda/latex/test_tex_script_expl3_int_relation.ls`.
Some conditional and parameter-taking relation expansions remain unsupported; the
original mismatch and examples are in
`../lambda-input/temp/expl3-script/int-compare-relation-before.ls/.out` and
`int-relation-debug.ls/.out`.
The single-expression forms support `=`, `==`, `!=`, `<`, `>`, `<=`, `>=` and
short-circuited chains; 44 string cases, one included file, three guarded AST
checks, 18 focused regressions and sampled forced-GC checks pass both backends.
See `test/lambda/latex/test_tex_script_expl3_int_compare_expr.ls`. Operand
macros that consume internal comparison delimiters still report an issue where
native continues; evidence: `../lambda-input/temp/expl3-script/int-marker-malformed-probe.ls/.out`.


## Script expl3 preload lacks native count register allocations — 2026-11-12

**Status:** OPEN. Blocks observable count allocation and register-identity parity
for the Script expl3 profile; S1.8 and D10.2.2 apply.

With `script_expl3: true`, `\ExplSyntaxOn\number\count10\ExplSyntaxOff`
succeeds but prints `24`; native prints `124`. Native `\meaning\l_tmpa_int`
is `\count26`, while Script reports `undefined`, and the next native
`\int_new:N` allocation is `\count125`. The bundled expl3 source defines
`\int_new:N` through `\newcount` and allocates numerous internal and scratch
registers. Evidence: `../lambda-input/temp/expl3-script/int-allocation-before.ls/.out`
and `lmd/package/doc/latex/tex/expl3-code.tex` near `\int_new:N`. A port must
preserve the shared allocator, identity and scope; setting observed counts or
register numbers to constants would hide the missing initialization.


## Script expl3 preload omits standard integer constants — 2026-11-12

**Status:** PARTIALLY FIXED. The non-allocating `\c_zero_int` and
`\c_one_int` now retain their native TeX meanings; register-backed constants
such as `\c_max_int` and the broader allocation state remain open. S1.8 and
D10.2.2 apply.

Native `\meaning\c_zero_int` is `\char"0` and `\meaning\c_one_int` is
`\mathchar"1`; Script previously reported both as undefined. Native `\int_eval:n` can
also read `\c_max_int`, while Script reports `tex-missing-number`. Evidence:
`../lambda-input/temp/expl3-script/int-constants-before.ls/.out`; the bundled
expl3 source defines these values and their TeX meanings in
`lmd/package/doc/latex/tex/expl3-code.tex`. Some integer constants allocate
registers, so their initialization is coupled to the separately recorded
count-allocation mismatch. `\c_two_int` and `\c_minus_one_int` were also
tested and are undefined in the native profile; do not invent them. The port
matches 32 string cases, one included file and five safe AST cases; see
`test/lambda/latex/test_tex_script_expl3_int_constants.ls`.


## Script LaTeX digestion misparses emitted expl3 math character names — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\mathchardef` control names that the
native LaTeX scanner would split are explicitly guarded; native AST source
mapping remains open. S1.8 and D10.2.2 apply.

With the Script expl3 profile, directly emitting `\c_one_int` inside a document
returns a successful generic `c_one_int` node, while native reconstruction
reparses its emitted command and produces an accent node followed by text. A
separate `\mathchardef\foo_bar` reproduction yields a generic `foo_bar` node
in Script and a `foo` node plus `_bar` text natively. Ordinary `\foo` and
`\alpha` math-character names match. Evidence:
`../lambda-input/temp/expl3-script/int-one-ast-probe.ls/.out`,
`mathchardef-expl-native-only.ls/.out`, and
`mathchardef-expl-script-only.ls/.out`. The direct-token digester does not
reparse those emitted names with the native LaTeX scanner's catcodes. The
guard preserves successful native-equal ASTs for ordinary control names;
coverage is in `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script TeX profile exceeds budget time on expl3 math-character input — 2026-11-12

**Status:** PARTIALLY FIXED. The tested exact self-recursive package bootstrap
now returns an explicit unsupported issue promptly after the package is attempted.
Other recursive macro shapes remain subject to the expansion budget; S1.8 and
D10.2.2 apply.

With `script_expl3` omitted, expanding the source in
`../lambda-input/temp/expl3-script/mathchardef-expl-disabled-small-budget.ls`
did not finish within a 10-second process timeout even with expansion budget
5000; the subprocess was terminated by the test. The same source with
`script_expl3: true` finishes in under a second in this local run, and native
LaTeX parsing finishes. See `mathchardef-expl-disabled-small-budget.out`,
`mathchardef-expl-script-only.ls/.out`, and
`mathchardef-expl-native-only.ls/.out` in that directory. At initial isolation it was unconfirmed
whether the slowdown was a non-progressing loop, excessive work outside the
expansion counter, or another cause. Further isolation shows `\ExplSyntaxOn`
alone reproduces the issue when expl3 resources and adapters are unavailable.
Budgets 500, 1000 and 2000 terminate with `tex-budget`; 5000 exceeded five
seconds. At budget 1000 the engine had accumulated 8000 output items and one
unknown `expl3` package. The kernel stub recursively requires `expl3` and
re-enters `\ExplSyntaxOn`; an unknown passthrough package does not replace the
stub. Evidence: `explsyntax-budget-results.out`, `explsyntax-cycle-detail.ls/.out`
and `mathchardef-timeout-isolates.out` in the same directory. The repeated
bootstrap is confirmed; quadratic cost from growing output is a plausible but
unconfirmed contributor. A general check now detects zero-argument macros whose
body requires an already attempted package and then re-enters the same meaning.
It preserves direct unknown-package passthrough, redefined `\RequirePackage`,
and packages that replace the bootstrap. Eleven behavioral cases, 18 focused
regressions and forced-GC checks pass both execution backends; see
`test/lambda/latex/test_tex_script_bootstrap_cycle.ls`.


## Script TeX reconstruction cannot emit an expl3 zero character — 2026-11-12

**Status:** FIXED for tested direct and macro-sourced `\c_zero_int` output;
numeric use is also supported. S1.8 and D10.2.2 apply.

With the staged non-allocating expl3 integer constants, expanding
`\ExplSyntaxOn\c_zero_int\ExplSyntaxOff` succeeded, but reconstruction
previously returned `tex-reconstruct-missing-origin` and no bytes. Native TeX output has
one NUL byte. Numeric byte counts and equality, which avoid the separately
recorded CLI NUL truncation, are in
`../lambda-input/temp/expl3-script/int-zero-nul-probe.ls/.out`. The shared `\chardef` emission branch omitted the reconstruction origin; see
`int-zero-origin.ls/.out` and the separate chardef provenance issue. The fixed
port reconstructs the same one byte as native; numeric evidence is in
`int-zero-nul-after.out`, and coverage is in
`test/lambda/latex/test_tex_script_chardef_origin.ls`. The direct-token LaTeX
AST also matched native in `int-constants-ast-probe.ls/.out`.


## Script TeX chardef emission loses source origin — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\chardef` characters now carry a
main-source origin for tested reconstruction cases; constructor-argument parity
and broader source mapping remain separate. S1.8 and D10.2.2 apply.

For `\chardef\c=65 \c`, Script expansion succeeded but reconstruction
previously returned `tex-reconstruct-missing-origin`; native emits `A`. The same occurred for
escaped punctuation, Unicode and macro-sourced uses. Evidence:
`../lambda-input/temp/expl3-script/chardef-origin-before.ls/.out`. The shared
`step` branch creates a character token with the control token's start/end
but omits file, read-offset and emission-origin fields. A staged
`\c_zero_int` token shows these fields are null in
`int-zero-origin.ls/.out`; the missing reconstruction origin is confirmed.
Constructor-argument behavior is separately blocked by the existing
constructor expansion issue and must not be inferred from the last probe. The
port matches 25 string cases, one included file, three safe AST checks, two
ordinary math-character AST cases, 24 focused regressions and sampled forced-GC
checks in both backends; see `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script expl3 integer zero and parity conditionals are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\int_if_zero:nTF/T/F`,
`\int_if_odd:nTF/T/F` and `\int_if_even:nTF/T/F` are supported. Predicate
variants, conditional evaluation order for nested diagnostics, malformed or
overflow recovery and integer AST source mapping remain open; S1.8 and D10.2.2
apply.

Previously Script stopped with `tex-unsupported-primitive` for these operations; native
selects the expected branches for zero, negative odd and positive even
expressions. Five native comparisons are in
`../lambda-input/temp/expl3-script/int-tests-before.ls/.out`. A port must
preserve expansion, signed parity, branch laziness, group behavior and guarded
AST source mapping; predicate variants remain separate. The port matches 44
string cases, one included file, three guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both execution backends; see
`test/lambda/latex/test_tex_script_expl3_int_tests.ls`. Branch evaluation order
for nested diagnostic-producing expressions remains an open issue recorded in
`Script expl3 integer conditionals read branches before evaluation`.


## Script expl3 integer division and modulo are unsupported — 2026-11-12

**Status:** OPEN. Blocks `\int_div_truncate:nn`, `\int_div_round:nn` and
`\int_mod:nn` in the Script expl3 profile; S1.8 and D10.2.2 apply.

Script stops with `tex-unsupported-primitive`; native distinguishes truncation
toward zero from nearest rounding and preserves the dividend's sign in the
remainder for the tested cases. Six native comparisons are in
`../lambda-input/temp/expl3-script/int-div-before.ls/.out`. Division-by-zero,
overflow recovery, expansion order and AST source mapping must be qualified.
The staged implementation explicitly stops with `tex-arith-overflow` for a
zero divisor, while native emits `0` and continues with a diagnostic; evidence:
`../lambda-input/temp/expl3-script/int-div-zero-probe.ls/.out`.


## Script expl3 integer conditionals read branches before evaluation — 2026-11-12

**Status:** OPEN. Blocks native diagnostic source metadata and potentially
branch-tokenization parity for nested integer operations; S1.8 and D10.2.2
apply. The tested ordering cause is confirmed for Script; native's exact
internal sequencing is inferred from source offsets and remains to be fully
qualified.

In the staged division port, `\int_compare:nTF{\int_mod:nn{1}{0}=0}{Y}{N}`
emits the correct `YX` text and arithmetic-overflow diagnostic but reports
offset 55, while native reports offset 49. The analogous `\int_if_zero:nTF`
case reports 59 instead of 53. The Script comparison and zero-test functions
read both branch arguments before evaluating the expression, advancing the
main-source cursor; native offsets indicate evaluation before those branch
tokens are consumed. Evidence:
`../lambda-input/temp/expl3-script/int-div-zero-offset-mismatch.ls/.out`. The
port must preserve branch laziness, source trace, grouping and diagnostics.
Conversely, the Script binary integer helpers evaluate their first operand
before collecting the second. Nested division diagnostics report offset 48
instead of native 51 for `\int_max:nn`, and 51 instead of 54 for
`\int_div_truncate:nn`; `nNn` comparison also reads branches too early.
Evidence: `../lambda-input/temp/expl3-script/int-order-binary-before.ls/.out`.
The first staged ordering revision regressed existing `nNn` comparisons when
an operand ended in `\relax`: its synthetic expression terminator was read as
a branch. Both backends produced two failed text comparisons in
`int-order-regression-{interp,jit}/expl3_int_relation.out` under that directory.
The installed implementation is unaffected; a corrected revision is being
qualified before installation.


## Concurrent integer-port preflight processes terminated with status 137 — 2026-11-12

**Status:** OPEN, cause unconfirmed. This limits highly concurrent qualification;
it does not currently block smaller test runs.

Ten concurrent interpreter/JIT preflight processes against the staged integer
ordering change exited with status 137 before producing result output. Sampled
stderr contained no fatal runtime diagnostic. A subsequent isolated interpreter
run of the unchanged integer-helper golden passed in 31.33 seconds. Evidence:
`../lambda-input/temp/expl3-script/int-order-concurrent-termination.out` and
`int-order-isolated-math.out/.log` in that directory. Resource pressure, external
termination and a runtime defect have not been distinguished; no cause is claimed.


## Script expl3 integer helpers mishandle explicit relax — 2026-11-12

**Status:** OPEN. Blocks native parity for some explicit expression terminators
and blocks qualification of the staged integer-division port; S1.8 and
D10.2.2 apply.

With `script_expl3: true`, `\int_abs:n{-1\relax}` emits `1`, while native emits
`1 `; `\int_min:nn{1\relax}{2}` stops with `tex-missing-number`, while native
recovers with a diagnostic and emits `0 `. The installed profile also stops on a
nested `\int_max:nn{\int_abs:n{-1\relax}\relax}{2}` where native continues with
two diagnostics. Evidence: `../lambda-input/temp/expl3-script/int-order-relax-detail.ls`
and `int-order-relax-live.out` in that directory. The staged ordering change
separates synthetic terminators and fixes the existing `nNn` branch regression,
but still reports a successful, different result for some helper arguments; for
example staged `\int_div_round:nn{7}{2\relax}` emits `4` without a diagnostic,
while native emits `4)` with `tex-missing-paren`. Evidence: `int-order-relax-detail.out`
and `int-div-fixed-interp.out` / `int-div-fixed-jit.out` in that directory. The candidate has not
been installed. Correct behavior requires modeling each native macro's expression
and terminator flow, not discarding the terminator or changing existing goldens.


## Script expl3 sequence queries and reversal are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. `\seq_if_empty:NTF/NT/NF`, the standard
`\c_empty_seq` meaning, `\seq_reverse:N` / `\seq_greverse:N` and tested
`\seq_item:Nn` indexing are implemented in the experimental Script profile.
The predicate and other sequence operations remain open; S1.8 and D10.2.2 apply.

Before this change, `\seq_if_empty:NTF`, `\seq_item:Nn`, `\seq_reverse:N`
and `\seq_if_in:NnTF` stopped with `tex-unsupported-primitive`. The tested
emptiness conditionals compare TeX meanings against the current `\c_empty_seq`,
including aliases, redefinitions and grouping. Forty native text and diagnostic
comparisons, one included file, two guarded AST cases, 14 focused regressions
and sampled forced-GC checks pass in both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_empty.ls`. The `p`/`c` variants,
malformed-input recovery and full AST source mapping remain open. Native returns
`b` for index 2
and `c` for index -1, reverses `a,b,c` to `c,b,a`, and finds `b` in the
sequence. Reproduction and results:
`../lambda-input/temp/expl3-script/seq-api-before.ls/.out`. The remaining port
must retain TeX meaning comparison, expandable/protected behavior, group scope,
token identity and source mapping.

`\seq_item:Nn` supports tested positive and negative integer positions,
out-of-range empty results, nested expressions, and unexpanded item tokens.
Forty-two native text and diagnostic comparisons, two guarded residual-token
cases, three safe or guarded AST cases, one included file, eleven focused
regressions and sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_item.ls`. Alternate argument
variants, malformed-input recovery, expressions with remaining tokens after an
early `\relax`, and full AST mapping remain open.

The reversal port matches 34 native text and diagnostic comparisons, one
included file, three safe AST cases, nine focused regressions and sampled
forced-GC checks in both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_reverse.ls`. It preserves local and
global assignment, `\globaldefs`, aliases and token order. It stops with
`expl3-invalid-sequence` for `\edef\rev{\seq_reverse:N\l_s_seq}\rev`:
the protected command is retained while the following sequence token expands,
so the executed target is not a sequence variable. Native diagnoses the resulting
malformed target and continues with the original sequence. Evidence:
`../lambda-input/temp/expl3-script/seq-reverse-probe.ls/.out`. Recovery is still
open; the explicit invalid-target error remains visible. Native AST comparison
when expl3 is first invoked inside a constructor argument is separately blocked
by [LI-020](../Lib_Input_Issue_Ledger.md#li-020).


## Staged sequence indexing leaks an expression terminator — 2026-11-12

**Status:** PARTIALLY FIXED. The installed `\seq_item:Nn` bounds integer
index evaluation and consumes its synthetic terminator. Residual tokens after
an early `\relax` are explicitly unsupported; complete native recovery and
expression behavior remain open. S1.8 and D10.2.2 apply.

For a sequence containing `10,20,30`, the staged expression
`\int_eval:n{\seq_item:Nn\l_s_seq{2\relax}+1}` returned `20+1` with no
issue while native returned `21`. Nested `\int_max:nn` also stopped with
`tex-missing-number` where native returned `20`. Evidence:
`../lambda-input/temp/expl3-script/seq-item-nested-probe.ls/.out`. The staged
indexer used the shared integer scanner, whose synthetic `\relax` remains in
the input after an explicit terminating `\relax`; the enclosing expression
then consumes it. The installed indexer delimits its token scope and matches the
tested nested cases. It reports `tex-unsupported-primitive` when additional
user tokens remain after an early `\relax`, instead of discarding them;
native can evaluate those tokens as part of its macro expression. Evidence:
`../lambda-input/temp/expl3-script/seq-item-residual-probe.ls/.out`.


## Script sequence operations reject valid macro bodies without cached items — 2026-11-12

**Status:** PARTIALLY FIXED. Tested sequence operations can decode ordinary
TeX macro bodies without a private cache. Protected macro mutation, broader
macro forms and full AST source mapping remain open; S1.8 and D10.2.2 apply.

For `\def\s{\s__seq\__seq_item:n{a}\__seq_item:n{b}}`, native
`\seq_use:Nn\s{|}` emits `a|b`; Script stops with
`expl3-invalid-sequence`. Native counting, indexing, appending and reversal
likewise work on the same macro body; Script currently requires a `seq_items`
array attached by its own sequence assignment functions. Evidence:
`../lambda-input/temp/expl3-script/seq-body-before.ls/.out`. Sequence identity
in TeX is the macro's token body, not a hidden allocation or a precomputed
cache. Decode well-formed sequence bodies while preserving token categories,
macro grouping, expansion behavior and bounded handling of malformed bodies.
The first staged decoder also rejects native-supported unbraced items and
protected sequence macros; its two initial macro-created-sequence AST examples
returned incorrect successful ASTs. Evidence:
`../lambda-input/temp/expl3-script/seq-body-probe.ls/.out`. The unbraced and
protected cases need qualification, and source mapping must remain guarded
until it can reproduce the native AST. An extended staged probe also found
that appending or prepending to a sequence with unbraced items reconstructs
those existing items with braces, changing `\meaning` from native. Reversal
and sorting do normalize those tested items natively. Evidence:
`../lambda-input/temp/expl3-script/seq-body-meaning-probe.ls/.out`. Updates must
preserve the existing macro body where native does, not only the item values.
The installed decoder handles tested braced and unbraced item bodies, and
append/prepend preserves the original body tokens. Forty native text and
diagnostic cases, two guarded protected mutations, three guarded AST cases,
one included file, thirteen focused regressions and sampled forced-GC checks
pass both execution backends; see
`test/lambda/latex/test_tex_script_expl3_seq_body.ls`. The tested protected
sequence macros can be read, but mutation remains explicitly unsupported until
its full native expansion behavior is modeled.


## Script-backed protected expl3 operations skip macro lookahead — 2026-11-12

**Status:** PARTIALLY FIXED for the installed local/global sequence and token-list
left- and right-insertion operations and sequence membership conditionals.
Other protected operations remain open; S1.8 and D10.2.2 apply.

With expl3 syntax active, store a letter-category `x` in a sequence and use
`\catcode`x=12 \seq_if_in:NnTF\l_s_seq{x}{Y}{N}`. Native emits `Y` and the
staged Script emits `N`. If a `\seq_put_right:Nn` immediately follows the
assignment before the membership test, native emits `N` and Script emits `Y`.
Both engines agree when the `\catcode` assignment is explicitly terminated by
`\relax`. Evidence: `../lambda-input/temp/expl3-script/seq-in-cats-probe.ls/.out`
and `seq-in-cat-detail.ls/.out`. A separate TeX Live 2025 pdfLaTeX
comparison agrees with the native `Y` and `N` results; see
`seq-in-cat-pdftex.tex/.out` in that directory.

Native `get_x_token` expands protected macros outside protected-expansion
contexts, so scanning the assignment's number starts expansion of the following
expl3 macro and captures its argument tokens before the category-code change.
Script-backed protected operations are modeled as unexpandable primitives; the
Script scanner does not start them until after the assignment, changing the
category codes of their arguments. The exact full macro-expansion sequence for
all affected operations remains to be qualified. This also affected
previously installed protected operations such as `\seq_put_right:Nn`; do not
alter existing goldens or substitute expected tokens. Other operations still need
protected-macro expansion that preserves argument timing and `\edef` protection.

An isolated prototype represents `\seq_put_right:Nn`, its global variant and
underlying token-list insertion as actual protected TeX macros, using the native
`\edef`/`\xdef`, `\unexpanded` and `\expandafter` aliases. It matches nine
initial native cases, including the category-code lookahead case, both macro
meanings, protected `\edef`, aliases and `\afterassignment`; evidence:
`../lambda-input/temp/expl3-script/seq-macro-right-probe.ls` and
`seq-macro-full-probe.out`. The implementation depends on sequence-body decoding. It covers only
right insertion and does not close the general protected-macro issue. The first
prototype also bypassed the installed token-list assignment source-mapping
guard: two existing `test_tex_script_expl3_tl_put` AST checks returned false.
Evidence: `../lambda-input/temp/expl3-script/seq-macro-tl-put-interp.out`.
The macro-backed path must preserve that guard until its reconstructed AST is
qualified; the existing golden remains unchanged. The installed implementation
attaches the guard to the underlying `\edef`/`\xdef` aliases, including aliases and
wrappers. Thirty-six native text, message and diagnostic comparisons, three
AST checks, an included file, fifteen existing regression suites and sampled
forced-GC checks pass on interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_seq_macro.ls`.


## Staged sequence membership omits internal assignments and source mapping — 2026-11-12

**Status:** PARTIALLY FIXED. The installed `\seq_if_in:NnTF/T/F` uses
protected TeX macros for tested membership operations. Expanded-output source
mapping remains guarded; S1.8 and D10.2.2 apply.

A pending `\afterassignment X` produces `XY` or `XN` natively, while the
staged membership function emits `YX` or `NX`: native membership executes an
internal token-list assignment before choosing the branch. With `\globaldefs=1`,
its internal scratch token-list changes persist; one test yields `Y/b/b`
natively but `Y/BEFORE/OLD` in the staged version. Evidence:
`../lambda-input/temp/expl3-script/seq-in-probe.ls/.out` and
`seq-in-sideeffects.ls/.out`. The staged direct comparison skips those internal
assignments and therefore cannot currently reproduce the side effects. Two
other native AST comparisons produce incorrect successful Script ASTs; see the
final array in `seq-in-probe.out`. Correct support requires modeling the
internal assignment/group behavior and expanded-output source mapping, not
merely moving an output token or synthesizing the observed scratch values.


The installed implementation executes protected TeX macros and supporting
group, assignment and conditional macros. Forty-seven comparisons and an
included file match native text, messages and diagnostics on interpreter and
JIT, including category lookahead, `\afterassignment`, and local and global
scratch assignments; three AST examples are conservatively guarded. Sixteen
existing regression suites and sampled forced-GC tests pass both backends.
A changed-kernel-category initialization case is also covered. See
`test/lambda/latex/test_tex_script_expl3_seq_in.ls`. General malformed-input
recovery and source mapping remain open.


## Script expl3 operations expose primitive meanings — 2026-11-12

**Status:** PARTIALLY FIXED for the installed local/global sequence and token-list
left- and right-insertion macros and sequence membership conditionals.
Introspection parity remains open for other hand-ported expl3 operations; S1.8 and D10.2.2 apply.

With `script_expl3: true`, `\meaning\seq_if_empty:NTF` and
`\meaning\int_abs:n` return escaped primitive names with no issue, while native
returns their long macro meanings and bodies. The staged reversal operations
similarly report primitive names where native reports protected long macros.
The Script `meaning` and `print_meaning` paths classify script-backed operations
as primitives, so the TeX macro identity, prefixes and bodies are absent.
Evidence: `../lambda-input/temp/expl3-script/expl3-meaning-probe.ls`,
`expl3-meaning-live.out` and `expl3-meaning-probe.out` in that directory.
The staged `\ifx` checks for reversal versus global reversal and for a `\let`
alias happen to match native; this does not qualify general `\ifx`, `\show`,
or other reflection. Preserve real TeX meaning semantics and aliases when
implementing this; do not substitute observed strings for macro definitions.

The installed right-insertion operations use real protected macro bodies; their
`\meaning`, `\show`, tested `\ifx` and aliases match native. The installed
sequence membership conditionals also use real protected macro bodies and
match tested `\meaning` and `\show` results. Other expl3 operations remain
subject to the primitive-identity limitation above.


## Staged membership definitions inherit kernel category codes — 2026-11-12

**Status:** FIXED in the installed membership implementation; S1.8 and
D10.2.2 apply.

A supplied kernel ending with `\catcode35=12` caused the prototype to tokenize
its internal macro definitions with a non-parameter `#`, producing
`tex-macro-mismatch` even when the document restored the category before use.
Evidence: `../lambda-input/temp/expl3-script/seq-in-initial-catcode.ls` and
`seq-in-initial-catcode.out`. The definition reader now uses fixed TeX
group and parameter categories and expl3 syntax categories; the user document
keeps its own category state. The same reproducer now emits `Y`; see
`seq-in-initial-catcode-fixed.out`. Its assertion is included in
`test/lambda/latex/test_tex_script_expl3_seq_in.ls`.


## Token-list left insertion uses protected TeX macros — 2026-11-12

**Status:** IMPLEMENTED for `\tl_put_left:Nn` and `\tl_gput_left:Nn` in the
experimental Script profile. S1.8 and D10.2.2 apply.

The operations now execute their protected TeX definitions through the existing
`\edef`/`\xdef`, `\unexpanded` and `\expandafter` aliases. Their tested
argument timing, `\meaning`, `\show`, aliases and local/global assignment
behavior match native, while the existing token-list assignment source-mapping
guard remains in place. Twenty-eight native text, diagnostic and message
comparisons, two guarded AST cases, an included file, ten focused regressions
and sampled forced-GC checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_tl_left_macro.ls`. Other token-list
variants and general expanded-output source mapping remain open.


## Macro-backed token-list assignment needs precise source mapping — 2026-11-12

**Status:** OPEN; `\tl_set:Nn` and `\tl_gset:Nn` remain on the installed
primitive implementation. S1.8 and D10.2.2 apply.

An isolated version using their real protected TeX macro bodies matches 19
native text, diagnostic and message comparisons. However, their shared
`\__kernel_tl_set:Nx` and `\__kernel_tl_gset:Nx` aliases carry the existing
token-list insertion source-mapping guard. For a direct assignment before a
document, the installed implementation produces an AST that matches native;
the staged version rejects it as `latex-digest-unsupported`. Reproduction:
`../lambda-input/temp/expl3-script/tl-set-ast-probe.ls`,
`tl-set-ast-live.out` and `tl-set-ast-stage.out`. Do not install the change by
regressing that supported document or weaken the guard globally: unguarded
insertion is already known to admit incorrect ASTs. Preserve expansion
provenance accurately enough to support direct assignments and safely handle
insertions and wrappers.

A further wrapper comparison currently differs between Script and native even
before the staged change. Native splits expl3 control names in the echoed
definition, and the two parsers also disagree on its source span. Native
scanner evidence and the still-unconfirmed offset cause are recorded as
[LI-021](../Lib_Input_Issue_Ledger.md#li-021); do not
copy the native scanner limitation into Script. Evidence:
`../lambda-input/temp/expl3-script/tl-set-ast-cases.ls`,
`tl-set-ast-cases-live.out`, `tl-set-ast-cases-unguarded.out` and
`tl-set-ast-detail.out`.


## Script expl3 profile lacks `\scan_stop:` — 2026-11-12

**Status:** FIXED in the installed Script profile; `\scan_stop:` is a real
TeX `\relax` alias. S1.8 and D10.2.2 apply.

`\ExplSyntaxOn\meaning\scan_stop:\ExplSyntaxOff` reports `undefined`
in Script while native reports `\relax`. This also affects meanings copied
by a staged `\tl_set_eq:NN`. The profile has not installed the expl3 alias
for TeX `\relax`; it must preserve primitive and `\ifx` behavior, rather
than substituting a printed value. Evidence:
`../lambda-input/temp/expl3-script/tl-set-eq-detail.ls` and `.out`.


## Token-list and sequence meaning-copy operations — 2026-11-12

**Status:** IMPLEMENTED for local and global `\tl_set_eq:NN` and
`\seq_set_eq:NN` in the experimental profile. S1.8 and D10.2.2 apply.

These operations execute protected TeX macros backed by `\let` and the
normal scoped assignment mechanism; aliases retain TeX meaning and assignments
respect `\globaldefs` and `\afterassignment`. The `\scan_stop:`,
`\tex_let:D` and `\tex_global:D` primitive aliases are installed. A
source-mapping guard follows aliased `\let` operations through direct and
prefixed assignments. Twenty-five native text, message and diagnostic
comparisons, four guarded AST cases, an included file, twelve focused
regressions and sampled forced-GC checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_tl_set_eq.ls`. Other argument
variants and general source mapping remain open.


## Numeric scans ignore control sequences with space meaning — 2026-11-12

**Status:** FIXED for tested expanded leading-space and numeric-terminator
scans in the installed Script engine. S1.8 and D10.2.2 apply.

In the staged implementation of the native expl3 `f`-expansion helpers,
`\exp_stop_f:` has the meaning of a space token. Native consumes it both
before a number and as a terminator after ordinary or character numbers. The
Script scanner checks only the input token's literal category and therefore
leaves an extra space, or reports `tex-missing-number`. This also leaves
`\exp_stop_f:` in the resulting sequence macro body and causes a later
`expl3-invalid-sequence`. Evidence:
`../lambda-input/temp/expl3-script/seq-left-number-probe.ls/.out`,
`seq-left-detail.ls/.out`, and `seq-left-probe.ls/.out`.

The installed fix recognizes control sequences whose TeX meaning is a space in
expanded leading-space scans and numeric terminators; the three native numeric
cases and the initial sequence cases match after the change. Evidence:
`seq-left-number-fixed.out` and `seq-left-probe-fixed.out` in the same
directory. The tested numeric and aliased-space cases, nineteen focused regressions and
sampled GC pass both backends. General source mapping remains guarded. Native
recovery after a missing number is a separate issue; the
first two cases in the numeric probe demonstrate that it is not fixed here.


## Sequence left insertion uses protected TeX macros — 2026-11-12

**Status:** IMPLEMENTED for `\seq_put_left:Nn` and `\seq_gput_left:Nn` in
the experimental Script profile. S1.8 and D10.2.2 apply.

The operations execute the protected TeX definitions and their tested `f`
expansion helpers, preserving token bodies, argument timing, aliases, scope
and introspection. The existing token-list assignment source-mapping guard
remains in effect. Forty-six native text, diagnostic and message comparisons,
two guarded AST cases, an included file, nineteen focused regressions and
sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_left_macro.ls`. Other variants,
general `f` expansion and source mapping still need qualification.


## Control-sequence existence conditionals need expanded-output mapping — 2026-11-12

**Status:** PARTIALLY IMPLEMENTED. The installed Script profile uses the real
expandable TeX conditional macros with an explicit source-mapping guard;
full AST support remains open. S1.8 and D10.2.2 apply.

Fifteen initial native text, message and diagnostic comparisons match, but
three AST examples whose branches define commands return successful Script
ASTs that differ from native. One direct document-body example matches and
one math example is already unsupported. Evidence:
`../lambda-input/temp/expl3-script/cs-exist-probe.ls/.out` and
`cs-exist-ast.ls/.out`. The exact contribution of native definition scanning
and source-span behavior (see [LI-021](../Lib_Input_Issue_Ledger.md#li-021))
versus Script mapping needs isolation. A staged guard marks actual conditional
macro expansions, including aliases, and returns `latex-digest-unsupported`;
see `cs-exist-ast-guarded.out`. Keep the mismatching cases visible and do not
claim tree support until the provenance is fixed and compared independently.


The installed `\cs_if_exist:NTF`, `:NT`, `:NF` and `\cs_if_exist_p:N`
macros preserve the tested distinction between undefined and `\relax`
meanings, expansion, aliases and nesting. Thirty native text, message and
diagnostic comparisons, four guarded AST cases, an included file, twenty-one
focused regressions and sampled forced-GC checks pass on interpreter and JIT;
see `test/lambda/latex/test_tex_script_expl3_cs_exist.ls`. Other variants and
expanded-output source mapping remain open.


The `N`-argument existence conditionals and predicates for token lists,
sequences, integers, booleans, comma lists, property lists, strings and
dimensions are real aliases of the corresponding control-sequence checks.
Their copied TeX meanings and the existing source-mapping guard follow aliases
and survive later redefinition of the original conditional. Thirty-two native
comparisons covering all 32 aliases, ten guarded AST cases, an included file,
twelve focused regression suites and sampled forced-GC checks pass on both
backends; see `test/lambda/latex/test_tex_script_expl3_exist_alias.ls`. Other
argument variants and expanded-output source mapping remain open (S1.8,
D10.2.2).


The `c`-argument control-sequence existence conditionals and predicates, plus
the corresponding token-list, sequence, integer, boolean, comma-list,
property-list, string and dimension aliases, now execute their native TeX
macro meanings. Dynamic names expand with `\ifcsname` semantics, including
the tested absence of control-sequence creation for missing names; the
existing AST source-mapping guard follows the conditional and its aliases.
Thirty-six native text, diagnostic and message comparisons, ten guarded AST
cases, an included file, fourteen focused regressions and sampled forced-GC
checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_cs_exist_c.ls`. Other variants and
expanded-output source mapping remain open (S1.8, D10.2.2).


The `N`- and `c`-argument `\cs_if_free` conditionals and predicates now
execute their native TeX macro meanings, including the tested treatment of
undefined and `\relax` meanings and dynamic names. Their expanded output
uses the same explicit source-mapping guard. Thirty-two native text, message
and diagnostic comparisons, four guarded AST cases, an included file, fifteen
focused regressions and sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_cs_free.ls`. Other variants and
expanded-output source mapping remain open (S1.8, D10.2.2).


## Control-sequence meaning comparison — 2026-11-12

**Status:** IMPLEMENTED for `\cs_if_eq:NNTF`, `:NNT`, `:NNF` and
`\cs_if_eq_p:NN` in the experimental Script profile. S1.8 and D10.2.2 apply.

These expandable TeX macros compare actual meanings, including tested
undefined commands, aliases, macro prefixes, token category codes and register
identities. Expanded-output source mapping has a dedicated explicit guard.
Thirty native text, message and diagnostic comparisons, four guarded AST
cases, an included file, sixteen focused regression suites and sampled
forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_cs_eq.ls`. Other argument variants
and expanded-output source mapping remain open.


The `cN`, `Nc` and `cc` meaning-comparison conditionals and predicates now
execute their native TeX wrapper and argument-expansion macro meanings. The
tested `\csname` behavior creates missing names as in TeX, and the existing
comparison source-mapping guard follows wrappers and aliases. Twenty-nine
native text, message and diagnostic comparisons, four guarded AST cases, an
included file, seventeen focused regressions and sampled forced-GC checks pass
both backends; see `test/lambda/latex/test_tex_script_expl3_cs_eq_c.ls`.
General expansion variants and expanded-output source mapping remain open
(S1.8, D10.2.2).


The local and global token-list and sequence meaning-copy operations also
support the `cN`, `Nc` and `cc` variants using native protected TeX wrapper
meanings and the shared name-expansion helpers. The assignment source-mapping
guard remains in effect. Thirty-six native text, message and diagnostic
comparisons, four guarded AST cases, an included file, eighteen focused
regressions and sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_tl_set_eq_c.ls`. Other argument
variants and expanded-output source mapping remain open (S1.8, D10.2.2).


The local and global token-list and sequence left/right insertion operations
also support the `cn` variants using the native protected TeX wrapper meanings
and shared name-expansion helper. The assignment source-mapping guard remains
in effect. Thirty-six native text, message and diagnostic comparisons, four
guarded AST cases, an included file, nineteen focused regressions and sampled
forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_put_cn.ls`. Other argument variants
and expanded-output source mapping remain open (S1.8, D10.2.2).


## Duplicate expl3 declarations do not reproduce native diagnostics — 2026-11-12

**Status:** OPEN in the installed Script profile; the dynamic-name declaration
wrappers inherit this limitation. S1.8 and D10.2.2 apply.

Declaring the same token list or sequence twice through `\tl_new:c` or
`\seq_new:c` stops the Script prototype with `expl3-already-defined` and no
diagnostic records. Native TeX instead records a `tex-errmessage` with the
control sequence name and continues recovery. The root is the installed
`expl3_tl_assign`/`expl3_seq_assign` fresh-name check, which calls `fail`
instead of executing the native declaration error machinery. Evidence:
`../lambda-input/temp/expl3-script/decl-c-invalid.ls/.out`. This also limits
the existing `N` forms. Preserve the failing evidence and implement the
diagnostic and recovery path before claiming full declaration compatibility;
valid declarations can be qualified independently.


## Declaration-stage regression process terminated — 2026-11-12

**Status:** UNCONFIRMED cause; qualification continues serially. In the
staged dynamic declaration run, `test_tex_script_expl3_cs_free.ls` exited
with signal 9 and no stdout; see
`../lambda-input/temp/expl3-script/decl-c-regressions-jit.log`. The identical
staged suite subsequently passed against its unchanged golden when run alone,
with peak child RSS 853,114,880 bytes. The installed profile also passed the
same isolated suite with peak child RSS 940,867,584 bytes. These individual
debug-run measurements do not establish a performance comparison or a cause
for the termination. Scripts and captured outputs are `decl-c-one.py`,
`decl-c-live-one.py`, `decl-c-one-jit-expl3_cs_free.out` and
`decl-c-live-one-jit-expl3_cs_free.out` in that directory. Continue serial
qualification, recording exit codes; do not treat the empty output as a TeX
semantic mismatch without further evidence (S1.8, D10.2.2).


## Dynamic-name token-list and sequence declarations — 2026-11-12

**Status:** IMPLEMENTED for valid `\tl_new:c`, `\seq_new:c`, and local and
global `c` clearing forms in the experimental Script profile. S1.8 and
D10.2.2 apply.

The operations use protected TeX wrappers and the shared dynamic-name
expansion helper. Tested scope, `\globaldefs`, aliases and assignment ordering
match native. Thirty valid native text, message and diagnostic comparisons,
four guarded AST cases, an included file, twenty focused regressions and
sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_decl_c.ls`. Duplicate declarations
retain the diagnostic/recovery limitation above, and source mapping remains
guarded.


## Dynamic-name assignment wrappers still lose protected lookahead — 2026-11-12

**Status:** OPEN; the `cn` token-list assignment and sequence-from-clist
wrappers remain uninstalled. S1.8 and D10.2.2 apply.

An isolated port using the native `\exp_args:Nc` wrappers matches ordinary
assignment, scope, alias and included-file tests. However, if a `\catcode`
number scan is immediately followed by `\tl_set:cn` or
`\seq_set_from_clist:cn`, a saved `x` has the wrong category: native emits
`Y` and Script emits `N` in both tests. Evidence:
`../lambda-input/temp/expl3-script/assign-cn-detail.ls/.out` and the failing
comparisons in `test_tex_script_expl3_assign_cn.ls` /
`assign-cn-final-interp.out` in that directory.

The wrapper expands while the number is being scanned, but its underlying
`\tl_set:Nn` and `\seq_set_from_clist:Nn` are Script primitives instead of
the native protected macros, so their argument tokens are captured after the
category change. The earlier staged macro-backed token-list assignment is
also blocked by its source-mapping issue above. Preserve these failing tests,
implement the underlying protected expansion and source provenance, and
qualify both wrappers before installation; do not substitute expected tokens.


## Token-list use accepts a relax meaning without native diagnostics — 2026-11-12

**Status:** PARTIALLY FIXED: the installed precondition rejects the invalid
variable explicitly. Native diagnostic and recovery parity remain open;
S1.8 and D10.2.2 apply.

For `\ExplSyntaxOn\tl_use:c{missing}/\ifcsname missing\endcsname Y\else N\fi\ExplSyntaxOff`,
the staged `c` wrapper initially emitted `/Y` with no issue or diagnostics,
while native emitted `/Y` and recorded `tex-macro-mismatch`. Evidence:
`../lambda-input/temp/expl3-script/use-c-missing-detail.ls/.out`. The existing
`\tl_use:N` primitive rejects only an undefined meaning, but `\csname`
creates a `\relax` meaning and native `\tl_use:N` also treats that as a
missing variable. The installed fix applies the existing undefined-variable issue
to both meanings; see `use-c-missing-fixed.out`. It prevents silent success
but does not reproduce native diagnostics or recovery. Preserve the original
failing comparison, qualify valid reads independently, and implement the
expl3 message/recovery path before claiming full invalid-input compatibility.


The same staged dynamic-read probe found further missing-name limits:
`\tl_count:c{missing}` returns `tex-unsupported-primitive` in Script while
native returns `1`; `\seq_count:c{missing}` returns `expl3-invalid-sequence`
in Script while native returns `0`. Missing-name sequence use and indexing
also stop explicitly instead of reproducing native diagnostics and recovery.
Evidence: `../lambda-input/temp/expl3-script/use-c-invalids.ls/.out`. These
unsupported cases must remain visible and are outside the qualified valid-read
subset until the underlying token-list and sequence meaning semantics and
error recovery are implemented.


## Dynamic-name token-list and sequence reads — 2026-11-12

**Status:** IMPLEMENTED for valid `\tl_use:c`, `\tl_count:c`,
`\seq_use:cn`, `\seq_item:cn` and `\seq_count:c` in the experimental
Script profile. S1.8 and D10.2.2 apply.

The operations execute native expandable TeX wrappers and the shared
name-expansion helper. Thirty valid native text, message and diagnostic
comparisons, five guarded AST cases, four explicitly rejected invalid cases,
an included file, twenty-two focused regressions and sampled forced-GC tests
pass both backends; see `test/lambda/latex/test_tex_script_expl3_use_c.ls`.
Missing-name diagnostics, recovery and count semantics remain open as
described above; expanded-output source mapping remains guarded.


## Dynamic-name token-list and sequence emptiness — 2026-11-13

**Status:** IMPLEMENTED for `\tl_if_empty:cTF`/`:cT`/`:cF` and
`\seq_if_empty:cTF`/`:cT`/`:cF` in the experimental Script profile.
S1.8 and D10.2.2 apply.

These forms use expandable TeX wrappers and the shared name-expansion helper.
Thirty-three native text, message and diagnostic comparisons, seven guarded
AST cases, an included file, seven focused regressions and sampled
forced-GC checks pass both execution backends; see
`test/lambda/latex/test_tex_script_expl3_empty_c.ls`. Expanded-output source
mapping remains guarded.


## Inline formatting commands without a brace group — 2026-11-13

**Status:** PARTIALLY FIXED for the tested script digester cases. S1.8 and
D10.2.2 govern the document and input boundary.

For `\def\x{\textbf}\begin{document}A\end{document}`, the native parser
creates an empty `textbf` node in the definition body. The script digester
instead returned `latex-digest-expected-group`: it required a brace group for
every formatting command. The native parser consumes an adjacent star, skips
ordinary space only when a group follows, and leaves a comment before a group
outside the formatting command. The script digester now follows these rules,
retains trailing group whitespace, and restores separators on synthetic output.

`test/lambda/latex/test_tex_script_inline_without_group.ls/.txt` compares 47
native ASTs for the supported formatting commands, definitions, comments,
whitespace, stars, Unicode and generated text. Interpreter and JIT pass with
the explicit generated-star guard, affected digest and provenance regressions,
and sampled forced GC. Earlier five-suite digest regressions passed in both
tiers. Full reconstructed-output digestion and malformed-input parity remain
unqualified.

An expanded command followed by a star can have different token source files
or offsets; for example `\def\x{\textbf}\begin{document}\x*{B}\end{document}`.
The installed digester now uses reconstructed byte adjacency when the command
or star has generated or consumed provenance. It checks the emitted whitespace
before a following group independently of TeX category codes and retains a
guard when a span cannot prove the gap. The 31-case native comparison covers
empty macros, comments, changed category codes and included sources; see the
current checkpoint for interpreter, JIT, GC and remaining performance evidence.
General reconstructed-output source mapping remains open.


## Experimental reconstruction drops source-mapping guards — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction/reparse prototype; full
script-port blocker for affected documents. S1.8 and D10.2.2 apply. Production
continues to use the native parser.

For `\begin{document}A\textbf{B\hyphenation{words}C}D\end{document}`,
the experimental expander sets `paragraph_data_seen`; direct script digestion
correctly reports `latex-digest-unsupported` / `paragraph assignment source
mapping`. The reconstruction prototype instead returns an AST with no issue,
but that AST differs from native because the assignment was executed inside a
constructor argument. The prototype builds a new input map containing only
the reconstructed tokens and package/diagnostic metadata; it drops the source
flags before calling `digest.digest`. Reproduce with
`../lambda-input/temp/expl3-script/reparse-guard-probe.ls` and
`reparse-guard-before.out`. Do not integrate this path until it preserves the
relevant guard decisions, with any relaxed guards justified by direct native
comparisons and the constructor-argument behavior fixed separately.


## Ordinary output omits its emission origin — 2026-11-13

**Status:** PARTIALLY FIXED for ordinary emitted tokens; other emission paths
still need auditing for reconstructed output. S1.8 and D10.2.2 apply.
Production continues to use the native engine.

For `\def\spaceout{\quad}\begin{document}A\spaceout B\end{document}`,
`reconstruct.reconstruct` returns `tex-reconstruct-missing-origin`. The `B`
is a direct main-file token and needs a synthesized separator after `\quad`,
but its output item does not carry the current main-reader offset. Native
`emit_token` records that offset even on direct tokens, and reconstruction
uses it for separator source mapping. Evidence is
`../lambda-input/temp/expl3-script/reparse-spacing-origin-detail.out`.

The ordinary output path now records the reader position at emission,
including when lookahead has advanced it. Thirty-two native text, document-message
and diagnostic cases, four direct AST comparisons, an included-file fixture,
six focused regressions and sampled forced GC pass both execution backends;
see `test/lambda/latex/test_tex_script_macro_argument_origin.ls`. Other
emission paths require separate audit.


## Macro substitution loses argument source identity — 2026-11-13

**Status:** PARTIALLY FIXED for tested macro argument substitution; other
source-map and constructor-argument gaps remain open. S1.8 and D10.2.2 apply. Production still
uses native parsing.

For `\def\q#1{\quad#1}\begin{document}\q{\textbf{B}}C\end{document}`,
the native AST maps `textbf` to byte 36, its main-file source location. The
reconstruction prototype maps it to byte 46 instead. The script's
`expand_macro` applies `stored_tokens` to the entire substituted list,
clearing `direct` on freshly scanned arguments. Native macro substitution
copies argument tokens with their existing flags while its stored replacement
body is non-direct. Evidence:
`../lambda-input/temp/expl3-script/reparse-macro-argument-detail.out`.

The expander now clears the replacement body's direct flags separately,
keeps scanned argument flags and clears stored optional defaults. Thirty-two
native text, document-message and diagnostic cases, four direct AST cases, an
included-file fixture, six focused regressions and sampled forced GC pass both
backends; see `test/lambda/latex/test_tex_script_macro_argument_origin.ls`.
The separate reconstruction prototype matches 16 text/AST cases in both
tiers and 29 provenance cases in the interpreter; it is still uninstalled.


## Kernel preload messages are omitted from script results — 2026-11-13

**Status:** PARTIALLY FIXED for ordinary profile output and executed expl3-format
requests. Expl3 register allocation messages still differ because the script
format state is incomplete; installed corpus qualification remains open. S1.8
and D10.2.2 apply. The production native path is unchanged.

Before the fix, with `X` as input, native `parse(..., {type: "tex"})` included the kernel's
allocation messages, whereas `profile.expand_with_kernel` returned no
messages. Running `expansion.expand` on the script kernel directly produces
the same preload messages as native; `expansion.resume` explicitly resets
`messages` to `[]`. Native `run_preload` clears the emitted tokens but leaves
the messages. Reproduce with
`../lambda-input/temp/expl3-script/kernel-preload-message-detail.ls/.out`;
the pre-change and staged macro profiles behave identically. Existing
inspection goldens deliberately compared document messages after subtracting
the native empty-document baseline. The current checkpoint records their audited
updates and the separately qualified expl3-format request handling. The earlier
failing full-message comparisons remain in `macro-origin-native-interp.out`.


## Reconstructed leading blank lines lose a paragraph — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction/reparse prototype; blocks
AST parity for affected documents. S1.8 and D10.2.2 apply. Production still
uses native parsing.

The local-file fixture `../lambda-input/temp/expl3-script/macro-origin-fixture-cwd/`
loads a file defining two macros before `\begin{document}`. Script and native
reconstructed text match, starting with three newline bytes, but the prototype
omits the leading `<paragraph "\n">` present in the native AST. Its TeX
reader converts those newlines to three `par` tokens, and `blocks` drops them
at the top level. Evidence: `../lambda-input/temp/expl3-script/
macro-origin-fixture-interp.out`, produced by `macro-origin-fixture.ls`.
The precise blank-line and comment rules must be preserved when reparsing; do
not integrate this prototype while it silently accepts the mismatched case.


## Formatted optional defaults are not digested — 2026-11-13

**Status:** PARTIALLY FIXED for tested formatted defaults; comment source
mapping and some nested-bracket scanning remain open. S1.8 and D10.2.2 apply.
Production remains native. Definition options now reuse balanced delimited
reading and inline digestion, preserving the raw source descriptor while
building the same formatted children as native. The new
`test/lambda/latex/test_tex_script_definition_option.ls` checks 24 matching
ASTs for commands and environments, formatting, nested groups, math, Unicode,
and whitespace on both backends. Three percent-containing cases explicitly
report `definition option comment source mapping`, because a source comment
can alter native whitespace. A nested unbraced bracket case remains explicitly
unclosed in the existing expansion path. Sampled forced GC also passes both
backends. Evidence: `../lambda-input/temp/expl3-script/formatted-option-v3-interp.out`.


2026-11-13 prototype follow-up: reconstructed blank-line normalization
matched 12 focused cases and the included-file fixture in the interpreter;
its 103-case corpus matched 100 ASTs and explicitly rejected three. The
prototype and its shared source guards remain uninstalled pending broader
qualification and do not close the production migration.


## Constructor execution also bypasses reconstruction guards — 2026-11-13

**Status:** PARTIALLY FIXED in opt-in script expansion; the reconstructed
pipeline and complete TeX/LaTeX parity remain open. S1.8 and D10.2.2 apply.
Production still uses native parsing.

For `\count0=0\begin{document}\textbf{\global\count0=7 X}\the\count0\end{document}`,
the default script expander executes the assignment inside the constructor
argument and emits `7`; native passes the assignment through and emits `0`.
The same mismatch occurs for unknown commands and optional arguments; evidence
is `../lambda-input/temp/expl3-script/reparse-constructor-safety-interp.out`.

The opt-in `constructor_argument_mode: true` now expands argument tokens
without executing assignments, handles passthrough environment headers, and
retains emission-time source identity. `test/lambda/latex/
test_tex_script_constructor_mode.ls` compares 41 cases with native text and
records five explicit unsupported forms in both execution backends. Nine
existing default-mode and expl3 goldens and sampled forced GC also pass both
backends. Raw inline commands, custom environments and unqualified expl3
assignments remain guarded; custom environments also depend on LI-017. The
experimental reconstruction pipeline remains uninstalled and incomplete.


## Declaration source tracing and synthesized origins — 2026-11-13

**Status:** PARTIALLY FIXED for the tested class and package declarations;
reconstructed-output and full script-parser parity remain open. S1.8 and
D10.2.2 govern source handling and the production boundary.

For `\documentclass{article}\n\usepackage{xcolor}\n\begin{document}A\end{document}`,
script reconstruction dropped the first newline and produced a different AST.
The class and package handlers emitted individual raw tokens without the
source trace needed to retain skipped bytes. With a local class that invokes
`\LoadClass`, reconstruction could instead fail with
`tex-reconstruct-missing-origin`, because a synthesized declaration carried
no main-reader emission position. Native traces direct declarations and
assigns an emission origin to synthesized output.

The script now retains the traced span for direct class/package declarations,
synthesizes a declaration when the trace contains foreign expansion, and
records the emission position of synthesized declaration tokens. Fifteen
direct, macro-generated, whitespace, comment and duplicate declaration cases
and the local-class/package fixture compare reconstructed text and diagnostics
with native in `test/lambda/latex/test_tex_script_declaration_origin.ls`.
The separate experimental pipeline also matches the local fixture's AST and
HTML; it remains uninstalled and incomplete. Production remains native.


## Declaration argument gaps in the script digester — 2026-11-13

**Status:** PARTIALLY FIXED for the tested declaration arguments; blank-line
and general source-mapping forms remain open. S1.8 and D10.2.2 apply.
Production remains native.

For `\usepackage {xcolor} [2020/01/01]\begin{document}A\end{document}`,
native creates a `usepackage` node with no arguments and keeps the space and
group in the following paragraph. The script digester incorrectly attached the
group to `usepackage`. Declaration scanning also consumes whitespace between
some raw tokens, which caused later gaps and argument end offsets to be lost.

The digester now restores whitespace consumed within a traced declaration,
uses generic adjacent-group parsing with literal group endpoints, and handles
an adjacent star. `test/lambda/latex/test_tex_script_declaration_digest.ls`
compares 32 spacing, comment, option, star, Unicode and group cases with the
native AST in both backends. Six existing digest and provenance goldens and
sampled forced GC also pass both backends.

The same test retains one explicit unsupported case:
`\usepackage[foo]\n\n{xcolor}\begin{document}A\end{document}` returns
`latex-digest-unsupported` / `command argument source gap`. Literal paragraph
breaks inside scanned declarations and generated-star source mapping remain
unqualified and block parity for affected documents.


## Redefined length and counter commands in constructor arguments — 2026-11-13

**Status:** PARTIALLY FIXED in opt-in constructor argument mode; raw AST
mapping and full script-parser parity remain open. S1.8 and D10.2.2 apply.
Production remains native.

For `\def\setlength#1#2{BAD}\begin{document}\textbf{\setlength{\foo}{X}}\end{document}`,
native argument mode passes the direct, redefined `\setlength` call through,
while the initial script candidate expanded it to `BAD`. The same mismatch
occurred for `\addtolength`, `\setcounter`, and `\stepcounter`; evidence is
`../lambda-input/temp/expl3-script/constructor-redefined-before-interp.out`.

The opt-in argument scanner now checks the native direct-main-source rule
before expansion, including the separate math profile condition. The focused
cases match native text in `test/lambda/latex/test_tex_script_constructor_mode.ls`.
The script digester still explicitly rejects the affected `setlength` AST
forms; the reconstructed pipeline remains uninstalled.


## Redefined length and counter commands at document level — 2026-11-13

**Status:** PARTIALLY FIXED for the tested document-level calls; constructor
argument handling and other source mappings remain open. S1.8 and D10.2.2
apply. Production remains native.

For `\def\setcounter#1#2{Q}\begin{document}\setcounter{foo}{7}\end{document}`,
native emits the original `\setcounter{foo}{7}` call and then executes the
redefined macro, yielding `Q`. The script expander previously emitted only
`Q`, so the experimental pipeline returned a different AST without an issue.
Native checks for direct, main-source redefined commands before expansion and
replays arguments as non-direct tokens after echoing the source.

The script now traces and echoes the original call and replays its macro and
arguments once, including for a command defined in a local package.
`test/lambda/latex/test_tex_script_redefined_document.ls` compares 15 calls
and a local-package fixture with native reconstructed text and diagnostics in
both backends, including state changes, comments, spacing, macro-generated
calls and math in a document. Existing expansion, profile, counter, expl3,
package and provenance regressions and sampled forced GC also pass both
backends. Opt-in constructor argument mode handles the tested redefined calls separately.


## Raw Lua groups are expanded at document level — 2026-11-13

**Status:** PARTIALLY FIXED for the tested raw Lua groups; raw-source AST
mapping and inline-code commands remain open. S1.8 and D10.2.2 apply.
Production remains native.

For `\def\foo{EXPANDED}\begin{document}\directlua{\foo}\end{document}`,
native keeps the raw group as `\directlua{\foo}`, while the script expander
previously emitted `\directlua{EXPANDED}` without an issue. The same occurred
for `\luaexec`. Native scans the current source reader for balanced raw groups
at document level, including escaped delimiters and comments.

The script reader now scans and seeks over these raw groups without expanding
their contents. `test/lambda/latex/test_tex_script_raw_lua.ls` compares 20
direct cases and a local package with native text and diagnostic records,
including multiline, Unicode, malformed and redefined-command cases. The
existing reader, stream, verb, expansion, profile, diagnostics, package and
reconstruction checks and sampled forced GC pass in the qualified execution
backends. The digester explicitly reports `raw inline source mapping` until it
can construct the correct AST for these spans.


## Raw inline-command expansion conflicts with a preserved golden — 2026-11-13

**Status:** OPEN in the installed script expander; blocks the affected raw
inline commands. S1.8 and D10.2.2 apply. Production remains native.

The isolated raw-source scanner supports `\lstinline`, `\Verb`, `\mint` and
`\mintinline`, and matches native reconstructed text and diagnostics for
braced and delimited examples in
`../lambda-input/temp/expl3-script/test_tex_script_raw_commands.ls`. However,
the existing `test/lambda/latex/test_tex_script_verb.txt` explicitly expects
`tex-unsupported-primitive` / `lstinline`. Enabling the scanner changes that
assertion to a successful expansion; the exact failing comparison is in
`../lambda-input/temp/expl3-script/raw-regression-interp-verb.out`.
The checkout instructions require preserving existing assertions and reference
results, so the raw inline-command integration remains uninstalled. The raw
Lua-group implementation can be qualified independently. This conflict must
be resolved before the inline support can replace the old expected failure;
raw-inline AST source mapping is separately unsupported.


## expl3 assignments inside constructor arguments — 2026-11-13

**Status:** PARTIALLY FIXED in opt-in constructor argument mode; other expl3
assignments remain guarded. S1.8 and D10.2.2 apply. Production remains native.

For a `\tl_set:Nn` inside `\textbf`, native argument mode expands the expl3
assignment macro to `\__kernel_tl_set:Nx` without executing the assignment.
The initial script candidate emitted `\tl_set:Nn` instead; evidence is
`../lambda-input/temp/expl3-script/constructor-expl3-interp.out`.

Argument mode now hand-ports the `tl_set:Nn` and `tl_gset:Nn` macro bodies
from `expl3-code.tex`, including unexpanded values, and preserves `\noexpand`.
The native text comparisons in `test/lambda/latex/
test_tex_script_constructor_mode.ls` cover local and global setters, aliases,
empty and nested values, overrides and expandable expl3 reads. Other recognized
expl3 assignments, including built-in sequence put wrappers and their aliases,
explicitly report `tex-unsupported-primitive` / `constructor argument expl3
assignment` until their argument-mode expansions are qualified. The wider
expl3 API and reconstructed pipeline remain incomplete.


## Environments inside inline arguments become `begin` nodes — 2026-11-13

**Status:** PARTIALLY FIXED for tested passthrough environments; special,
custom and option-bearing environments remain open. S1.8 and D10.2.2 apply.
Production remains native.

For `\begin{document}\textbf{A\begin{quote}B\end{quote}C}D\end{document}`
with opt-in constructor argument mode, native creates a nested `quote`
environment. The script digester previously emitted a generic `begin`
command, text, and a stray curly group without an issue; evidence is
`../lambda-input/temp/expl3-script/inline-environment-before-interp.out`.

The digester now shares environment parsing between block and inline contexts.
`test/lambda/latex/test_tex_script_inline_environment.ls` compares eight
supported AST cases with native, including nested environments, formatting,
declarations and ordinary groups, and retains six explicit guarded forms.
Existing digest, definition, formatting, declaration, provenance and
constructor-mode goldens and sampled forced GC pass both backends. Special or
custom environments and environment options still report unsupported where
their source mapping or expansion has not been qualified.


## Nested environment whitespace restoration shifts the parent cursor — 2026-11-13

**Status:** PARTIALLY FIXED for the tested nested arguments and environments;
the reconstructed pipeline remains incomplete. S1.8 and D10.2.2 apply.
Production remains native.

For `\begin{document}\begin{quote}\maketitle\nA\end{quote}TAIL\end{document}`,
native retains `TAIL`, while the script digester previously returned `AIL`
without an issue. Restoring source-gap tokens created a new token array inside
the environment, but its cursor was returned to a parent still using the old
array. A real fixture, `test/input/test_input.tex`, also lost its trailing
newline paragraph in the uninstalled reconstructed pipeline; evidence is
`../lambda-input/temp/expl3-script/nested-gap-state-before.out` and
`pipeline-test-input-debug-v4.out`.

Nested block and inline parsing now return the updated token stream with its
cursor, including when an environment appears inside a command argument.
`test/lambda/latex/test_tex_script_nested_gap_state.ls` compares 15 native AST
cases covering multiple gaps, nested environments, arguments, comments and
Unicode. Existing digest and provenance goldens and sampled forced GC pass
both backends. The separate reconstructed pipeline now matches the fixture's
native AST and HTML in focused testing, but remains uninstalled and incomplete.


## Common math operators and delimiters are unsupported — 2026-11-13

**Status:** PARTIALLY FIXED for the tested math operators, functions and
delimiter commands; broader math parsing remains open. S1.8 and D10.2.2 apply.
Production remains native.

The experimental script pipeline previously rejected `\downarrow` in
`test/input/sample.tex` and `\vert` in
`test/latex/samples/complex_numbers.tex`. Native math digestion treats these
standalone delimiters as generic commands and recognizes a broader table of
operators and functions.

The script digester now ports the native operator and function tables and
handles optional and braced children on standalone delimiter commands.
`test/lambda/latex/test_tex_script_math_symbols.ls` compares 36 grouped cases
with native ASTs, including scripts, nested arguments and delimiters. Existing
math and digest goldens and sampled forced GC pass both execution backends.
The sample document proceeds through this math case, but the reconstructed
pipeline remains experimental and other math atoms remain unsupported.

An unmatched optional bracket after a generic math command causes native
LaTeX parsing to stall. Input-owned **LI-028** records the bounded
reproduction and root cause. Malformed-input parity for that case remains
blocked until the input parser is fixed.


## Reconstructed whitespace-only lines create extra paragraphs — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction/reparse prototype; blocks
safe script-only integration for affected documents. S1.8 and D10.2.2 apply.
Production remains native.

After adding common math command support, `test/input/sample.tex` produces
matching reconstructed text but a different AST and HTML. The candidate creates
paragraph breaks around whitespace-only lines that native leaves within a
paragraph, and loses some indentation after comments. Native's direct LaTeX
parser treats consecutive literal LF bytes as a paragraph break; the TeX
reader can convert a line containing spaces into `\par`, losing the
distinction when the prototype retokenizes reconstructed output. Evidence:
`../lambda-input/temp/expl3-script/pipeline-sample-text.out`,
`pipeline-sample-actual.txt`, and `pipeline-sample-native.txt`.
Reproduce with `pipeline-real-documents.ls` against the staged math candidate.
Preserve whitespace and comment text while normalizing reconstructed tokens,
and keep this pipeline uninstalled until the mismatch is fixed or guarded.

## Theorem declarations and adjacent stars — 2026-11-13

The script digester now handles `\newtheorem` through the same generic
argument parser as native LaTeX. A directly adjacent star produces the native
`newtheorem*` tag and `starred` attribute. A generic command consumes a star
only when the source positions are adjacent; previously `\mystery *{X}` was
silently parsed as a starred command. When the star is emitted without direct
source mapping, the digester reports `command star source mapping` rather than
guessing. S1.8 and D10.2.2 apply; this does not change the production boundary.

`test/lambda/latex/test_tex_script_theorem.ls` compares 18 declaration and
generic-command cases with native ASTs and checks the generated-star guard.
The focused cases, four existing digester goldens and sampled forced-GC checks
pass in interpreter and JIT. In the experimental reconstructed pipeline,
`test/input/test_latex_m7.tex` now proceeds beyond theorem declarations and
explicitly reports an unsupported environment; the document is not yet fully
supported.

## Math comparison operators — 2026-11-13

The script digester now parses `<`, `>`, `<=`, and `>=` as math relations.
An equals sign joins the preceding comparison only when their source positions
are adjacent; relation atoms can carry scripts. S1.8 and D10.2.2 apply, and
the production LaTeX input boundary remains unchanged.

`test/lambda/latex/test_tex_script_math_relations.ls` compares 23 direct,
spaced, commented, nested and scripted cases against native ASTs. Focused
comparisons, three existing math/digest goldens and sampled forced-GC checks
pass in interpreter and JIT. In the experimental reconstructed pipeline,
`test/latex/samples/complex_numbers.tex` now matches the native AST and HTML
in both backends, though the pipeline remains incomplete.

The separately staged whitespace normalization passes a 126-case interpreter
matrix, a 90-case JIT matrix plus 36 added cases, focused reparse and pipeline
checks in both backends, and a 103-case corpus with 100 matches and three
explicit unsupported results in each backend. Four other real documents
match native AST and HTML. This evidence does not establish complete parity;
the whitespace normalization and reconstructed pipeline remain uninstalled.

## Table and tabular environments — 2026-11-13

The script digester now preserves `table` placement and `tabular` column
headers when their source mapping is available. In tabular bodies it emits
alignment separators at the current environment level, while nested groups
and environments retain their own parsing context. S1.8 and D10.2.2 apply;
production continues to use the native input boundary.

`test/lambda/latex/test_tex_script_tables.ls` exercises 22 native AST matches
and six explicit guards, including nested tables, spacing, formatting, math,
comments, and generated content. Placement containing comments or generated
tokens, and headers or columns with unqualified generated source mapping,
remain unsupported. Four existing digest/environment goldens and sampled
forced-GC checks pass in interpreter and JIT. With this candidate, the
experimental reconstructed pipeline matches the native AST and HTML for
`test/input/test_latex_m7.tex` in both backends; full parser parity and
production migration remain open.

## Generic math symbols and periods — 2026-11-13

Common set relations and other symbols that the native parser represents as
generic commands now use that same shape in the script digester. Math periods
also distinguish punctuation, decimal prefixes and three-dot text, including
attached scripts. S1.8 and D10.2.2 apply; production remains at the native
input boundary.

`test/lambda/latex/test_tex_script_math_generic_symbols.ls` and
`test/lambda/latex/test_tex_script_math_dots.ls` compare 15 grouped command
cases and 20 punctuation/number cases against native ASTs. Three existing
math goldens and sampled forced-GC checks pass both execution backends. The
scientific-writing sample now progresses to a displayed math environment in
the experimental pipeline; displayed math remains under qualification.

## Displayed math environments — 2026-11-13

**Status:** PARTIALLY FIXED for `equation`, `equation*`, `align`, and
`align*`; other displayed environments and several source forms remain open.
S1.8 and D10.2.2 apply; production remains at the native input boundary.

The script digester builds native-shaped math ASTs and uses shared TeX token
reconstruction for the body source. This fixes the staged mismatch where a
consumed `\relax` or assignment remained in the source attribute even though
native emitted only the surrounding text. Comments, generated body tokens,
nested math environments and equation tags remain explicitly unsupported
when their source mapping or parser is not qualified.

Local qualification compared 19 native ASTs, recorded seven explicit
unsupported cases, and checked a delimiter guard. Repository coverage for
those cases remains pending. The existing inline-environment golden was updated for its newly supported
equation with the user's explicit approval. Focused existing math and digest
goldens, interpreter and JIT comparisons, and sampled forced-GC checks pass.
The experimental pipeline still reports itself incomplete.

## Math accents and inequality — 2026-11-13

The script digester now ports the native math accent command table, including
wide accents, braces and arrows, and emits the native negation node for `\ne`
and `\neq`. S1.8 and D10.2.2 apply; production remains native.

`test/lambda/latex/test_tex_script_math_accents.ls` compares 19 grouped cases
against native ASTs, including missing and unbraced arguments, scripts, and
nested fractions. Three existing math/underline goldens and sampled forced-GC
checks pass in interpreter and JIT. Unsupported text-mode and displayed-math
forms remain separate work.

## Malformed expl3 integer expressions abort instead of recovering — 2026-11-13

**Status:** PARTIALLY FIXED for missing operands; arithmetic and other malformed
input recovery remain open. S1.8 and D10.2.2 apply. Previously,
`\ExplSyntaxOn\int_sign:n{}\ExplSyntaxOff` stopped with `tex-missing-number`,
returned no text, and had no diagnostic record. Native TeX reports the same
error code with a diagnostic and recovers to output `0`. Division by zero,
undefined numeric control sequences, and out-of-range integers also diverged
in recovery and diagnostics. The already-installed `\int_abs:n` showed the
same four failures, so this was not specific to the sign operation. Historical
reproducers were
`../lambda-input/temp/expl3-script/expl3-sign-errors.ls` and
`expl3-abs-errors.ls`; `expl3-sign-error-detail.out` includes the direct
missing-number comparison. The shared integer scanner now recovers a missing
operand with zero and a diagnostic, including empty `int_sign:n` and
`int_abs:n`; the overflow, division and undefined numeric-control cases have
not been established as fixed by this change.

## expl3 integer sign — 2026-11-13

The script expl3 profile implements `\int_sign:n` using the existing TeX
integer-expression scanner and returns -1, 0 or 1 after one evaluation. S1.8
and D10.2.2 apply. `test/lambda/latex/test_tex_script_expl3_int_sign.ls`
compares 17 valid text, message and diagnostic cases with native TeX, including
nested expressions, aliases, registers and random-state effects. It also
checks two document-digestion cases. Three existing integer goldens and sampled
forced-GC checks pass interpreter and JIT.

The malformed-integer recovery issue above applies to this operation as well.
Its `\meaning` also exposes the script primitive rather than native's long
macro definition, like the existing `\int_abs:n`; evidence is
`../lambda-input/temp/expl3-script/expl3-sign-meaning.out`. Introspection
parity remains open under the earlier primitive-meaning record.

## expl3 integer division and remainder — 2026-11-13

The script expl3 profile now implements `\int_div_truncate:nn`,
`\int_div_round:nn`, and `\int_mod:nn`. It scans each expression once,
uses truncation toward zero for the truncated quotient and remainder, and
shares the TeX rounded-division helper for rounded quotients. S1.8 and
D10.2.2 apply.

`test/lambda/latex/test_tex_script_expl3_int_div.ls` compares 23 grouped cases
with native text, messages and diagnostics, covering signs, ties, integer
limits, nested expressions, aliases, registers and random-state effects. Two
further cases check safe document digestion. Three existing integer goldens
and sampled forced-GC checks pass interpreter and JIT. The malformed-number
recovery and primitive-meaning limitations described above also apply here.

## Assignment prefixes do not expand a following macro — 2026-11-13

**Status:** PARTIALLY FIXED for expansion after a prefix; AST source mapping
remains guarded. S1.8 and D10.2.2 apply. Before the fix, in the boolean prototype,
`\ExplSyntaxOn{\bool_gset_true:N\g_tmpa_bool}\ExplSyntaxOff` stops with
`tex-bad-prefix`, while native TeX completes the global assignment. The real
macro expands to `\tex_global:D\cs_set_eq:NN...`; the Script `prefixed`
scanner reads its next token without expansion and rejects the macro before
it reaches the underlying `\let`. The same root affects ordinary TeX macros
after an assignment prefix. Evidence is in
`../lambda-input/temp/expl3-script/expl3-bool-interp.out` and the staged
`expl3-bool-stage-home` candidate. Source-echo and scoped-assignment checks
are covered below.

The prefix candidate expands macros before locating the assignment and matches
13 focused native text/diagnostic cases in both backends. Its direct digester
initially produced incorrect ASTs when an echoed definition was followed by a
source group emptied by the assignment: native attaches that group to the
preceding definition, while the Script digester emitted it separately. The
candidate reports `prefixed macro source mapping` for macro-expanded prefixes
combined with echoed definitions until that reconstructed group provenance is
implemented. Evidence: `../lambda-input/temp/expl3-script/prefix-v2-interp.out`
and `prefix-v3-interp.out`. This is a guard, not a resolution of AST parity.

The prefix expansion fix is now installed. `test/lambda/latex/test_tex_script_prefix_expansion.ls`
compares 14 expansion/diagnostic cases and four source-mapping guards. Five
existing TeX and expl3 goldens and sampled forced-GC checks pass interpreter
and JIT. The prefix source-mapping guard remains until reconstructed definition
arguments are handled correctly.


## Source after the document end is absent from direct digestion — 2026-11-13

**Status:** PARTIALLY FIXED for plain trailing text; structured tails remain
guarded and block full AST parity. S1.8 and D10.2.2
apply. With script expl3 enabled,
`\ExplSyntaxOn\begin{document}\textbf{F}\end{document}\ExplSyntaxOff`
produces a script AST ending at the document element, while native LaTeX also
contains a following paragraph with an `ExplSyntaxOff` node. The expansion
result records the remaining text as `trailing_source`, and shared TeX
reconstruction includes it, but the direct digester reads only `output`.
The same mismatch occurs without any boolean call, so it predates the boolean
prototype. Evidence: `../lambda-input/temp/expl3-script/expl3-bool-ast-end.out`
and `expl3-bool-tail-state.out`. Additional trailing forms and the remaining guard are described below.

The direct digester now appends plain trailing text as paragraphs using the
native parser's adjacent-LF rule, preserving spaces, line endings and Unicode.
Tails containing commands, groups, math or comments explicitly report
`post-document source mapping` until source-aware tail digestion is ported.
`test/lambda/latex/test_tex_script_trailing_source.ls` covers 25 matching cases
and two guarded cases. Four existing goldens and sampled forced-GC checks pass
in interpreter and JIT.

## expl3 boolean declarations and conditionals — 2026-11-13

The script expl3 profile now provides the standard scratch booleans,
`\bool_new:N`/`:c`, local and global true/false setters, equality aliases,
and `N`/`c` conditionals and predicates. The boolean wrappers use TeX macro
bodies and the existing conditional and `\let` machinery; the fresh-name
copy operation `\cs_new_eq:NN` is script-backed. S1.8 and D10.2.2 apply.

`test/lambda/latex/test_tex_script_expl3_bool.ls` contains 31 matching native
text/message/diagnostic comparisons, source-mapping checks, and a constructor
declaration guard. It covers scope, `\globaldefs`, dynamic names, aliases,
reflection of the boolean wrappers, and constructor arguments. Existing
existence and equality goldens and sampled forced-GC checks pass interpreter
and JIT.

Boolean declarations and setters still encounter the existing direct-digester
assignment source-mapping guard. Duplicate `\bool_new:N` aborts with
`expl3-already-defined` without native's diagnostic and recovery; this is the
same open duplicate-declaration issue noted above. Evidence is
`../lambda-input/temp/expl3-script/expl3-bool-duplicate.out`. Introspection of
script-backed `\cs_new_eq:NN` and of existing primitive-backed expl3 setup
commands remains subject to the primitive-meaning limitation. Boolean
expression setters and other APIs remain unimplemented; inversion is covered
below.

## Equation tags and nested math environments — 2026-11-13

**Status:** staged, not installed. S1.8 and D10.2.2 apply; production remains
native. The reconstructed pipeline stopped on `\tag{$\dagger$}` in
`test/latex/samples/scientific_writing.tex`, then on a control-space and nested
`split` environment. The staged candidate adds tag text and embedded-math
parsing, math control spaces, and common nested matrix-style environments.
It matches the sample's native AST and HTML in interpreter and JIT; evidence
is `../lambda-input/temp/expl3-script/scientific-nested-v2-interp.out` and
`scientific-nested-v2-jit.out`. The staged pipeline still reports incomplete.

An initial nested-environment implementation emitted `\\[2pt]` as a row
separator followed by ordinary math tokens, while native attaches `2pt` as the
row gap. Evidence is `math-nested-env-interp.out` in the same temporary
folder. The candidate now preserves that row-gap attribute and guards
unqualified options. Existing golden cases that explicitly expect `tag` and
`split` to be unsupported will need an authorized reference update before the
candidate can be installed.

The staged tag and nested-environment candidate now has 38 matching native
AST cases for tag text, 16 for nested environments, and explicit guards for
unsupported commands and source mappings. Nested row gaps, macro-expanded
source guards, existing math goldens, and sampled forced GC pass in both
backends. The math-environment golden would change only the two cases noted
above; it remains unchanged pending authorization. Evidence is in
`../lambda-input/temp/expl3-script/math-tag-v5-interp.out`,
`math-nested-env-probe-v2-interp.out`, and `math-tag-source-interp.out`.

## expl3 boolean inversion — 2026-11-13

The Script expl3 profile now implements local and global boolean inversion,
with `N` and dynamically constructed `c` arguments. S1.8 and D10.2.2 apply.
`test/lambda/latex/test_tex_script_expl3_bool_inverse.ls` compares text,
messages, and diagnostics with native TeX across scope, `globaldefs`,
introspection, and constructor arguments. Direct AST digestion reports the
existing assignment source-mapping guard. Focused existing expl3 goldens and
sampled forced-GC checks pass interpreter and JIT.

Without constructor argument mode, inversion inside a constructor has the
existing reconstructed-source difference; with the mode enabled it matches
native. Evidence: `../lambda-input/temp/expl3-script/expl3-bool-inverse-constructor.out`.
Boolean expression setters and the assignment source mapping remain open.

## expl3 boolean string conversion — 2026-11-13

The script profile implements `\bool_to_str:N` and `\bool_to_str:c` with
other-category letters, preserving the native token identity under `\ifx`.
S1.8 and D10.2.2 apply. `test/lambda/latex/test_tex_script_expl3_bool_string.ls`
compares native output, diagnostics, introspection, token identity, three
LaTeX ASTs and a constructor argument. Existing boolean-inversion and token
string goldens and sampled forced-GC checks pass in interpreter and JIT.
Boolean expressions remain unimplemented.


## Whitespace between echoed definition arguments — 2026-11-13

**Status:** PARTIALLY FIXED for single-line and newline whitespace; paragraph
breaks and structured tails remain guarded. S1.8 and D10.2.2 apply. A line break between the
name and the first body of `\newenvironment` produces
`latex-digest-expected-group` in the Script digester, though native LaTeX
parses it. The definition-span parser assumes consecutive group tokens, while
TeX's argument scanner skips spaces and the native AST instead ends the
command's argument list at source whitespace. A space immediately after
`\newenvironment` also causes a silent AST mismatch: native emits an empty
command followed by the separate groups, while the Script digester attaches
all groups. Evidence: `../lambda-input/temp/expl3-script/definition-gap-before.out`,
`definition-gap-native.out`, and `math-proof-debug-v3.out`. The command-source
adjacency rule and remaining source spans need to be preserved when digesting
echoed definitions; simply skipping spaces would still produce the wrong AST.


The installed definition-gap parser now preserves native command argument
boundaries and returns the remaining source as ordinary inline nodes. The
new `test/lambda/latex/test_tex_script_definition_gap.ls` has 28 matching
native AST cases and a paragraph-break guard, covering options, stars,
comments, newlines, and local definitions. Existing digest, theorem,
prefix and inline-environment goldens and sampled forced GC pass both
backends. The experimental `math_proof.tex` pipeline advances to an
explicit guard for a nested `renewcommand`; it is not yet complete.

## Undefined environment renewal omits native diagnostic — 2026-11-13

**Status:** PARTIALLY FIXED for tested source locations. S1.8 and D10.2.2
apply. `\renewenvironment{x}{A}{B}X` previously appended only a diagnostic
code, leaving its text and location unavailable to the direct AST. The
Script expander now uses the shared source-aware diagnostic recorder.
`test/lambda/latex/test_tex_script_environment_diagnostic.ls` compares 12
native ASTs and diagnostic metadata for a dynamically expanded name. The
existing expansion and definition-gap goldens and sampled forced GC pass
interpreter and JIT. `definition-renew-diagnostic.out` captures the original
mismatch. `test/lambda/latex/test_tex_script_environment_diagnostic_foreign.ls`
checks a diagnostic emitted by an included file, including its file and source
location. Source-offset parity with an expanded environment name is a separate
open issue below; broader foreign-source cases require further qualification.

## Expanded environment names shift an echoed definition boundary — 2026-11-13

**Status:** PARTIALLY FIXED for qualified definition boundaries and following
groups. S1.8 and D10.2.2 apply. Native maps the end of an echoed definition's final argument through
the next reconstructed segment when intervening TeX commands emit no text.
The Script digester now applies that mapping for known emitted origins, while
preserving source gaps. This fixes the expanded environment-name example and
similar assignments, conditionals, generated text and following block commands.
`test/lambda/latex/test_tex_script_definition_boundary.ls` compares the original
cases with native on interpreter and JIT. Following groups now use the shared
argument scanner and reconstructed source map; the current checkpoint records
the added coverage and remaining qualification. Some unqualified definition
shapes still report `definition following group source mapping`. Focused
existing goldens and sampled forced GC pass both backends. Evidence:
`../lambda-input/temp/expl3-script/definition-boundary-v4-interp.out`.


## Duplicate environment declaration omits native diagnostic — 2026-11-13

**Status:** PARTIALLY FIXED for tested duplicate environments.
S1.8 and D10.2.2 apply. With two `\newenvironment{x}` declarations, native
adds a `latex-already-defined` record with its message and location, while the
Script AST omits the record because the expander stores only the code.
Evidence: `../lambda-input/temp/expl3-script/environment-duplicate-diagnostic.out`.
This is the same missing-record mechanism as the renewal diagnostic fixed
above; the existing definition-recovery behavior requires separate checking.

The Script expander now uses the shared diagnostic recorder for duplicate
environments. `test/lambda/latex/test_tex_script_environment_duplicate.ls`
compares ten native ASTs and two recovery cases. Existing expansion and
renewal-diagnostic goldens and sampled forced GC pass interpreter and JIT.
Broader definition recovery and source locations remain to be qualified.


## Command definition errors omit native diagnostic records — 2026-11-13

**Status:** PARTIALLY FIXED for tested command definitions.
S1.8 and D10.2.2 apply. Both `\renewcommand{\x}{A}` when `\x` is undefined
and a duplicate `\newcommand{\x}` produce native diagnostic records, while
the Script expander appends only their codes and the direct AST omits them.
Evidence: `../lambda-input/temp/expl3-script/command-definition-diagnostic.out`.
The missing-record mechanism is the same as the environment cases above;
command spelling, source locations and recovery need qualification.

The Script expander now records the native messages and locations for these
command-definition errors. `test/lambda/latex/test_tex_script_command_diagnostic.ls`
compares 15 native ASTs and three recovery cases, including changed escape
characters. An included-file diagnostic also matches native in the temporary
`command-diagnostic-foreign.out` probe. Existing expansion and duplicate
environment goldens and sampled forced GC pass interpreter and JIT. Broader
other error recovery remains open; invalid argument counts are covered below.


## Invalid command argument counts lack native diagnostics — 2026-11-13

**Status:** PARTIALLY FIXED for representable counts in `\newcommand`
and `\renewcommand` argument counts. S1.8 and D10.2.2 apply. Native reports
`latex-bad-arg-count` immediately after the optional count, including its
actual numeric value and location; the Script expander records only the code
after consuming the command body. In an undefined renewal both diagnostics
are needed in order. Evidence:
`../lambda-input/temp/expl3-script/command-arity-before.out`. The Script
count reader currently saturates values at 10, so complete error-message
parity also requires preserving the original parsed numeric value.


The Script parser now records the diagnostic at the count argument and uses
the full representable numeric value. `test/lambda/latex/test_tex_script_command_arity.ls`
compares 15 native ASTs, checks two explicit overflow guards, and verifies the
long-name diagnostic. The included-file count diagnostic also matches native
in `command-arity-foreign.out`. Existing expansion, command-diagnostic and
definition-gap goldens and sampled forced GC pass interpreter and JIT.

## Long command names differ in definition diagnostics — 2026-11-13

**Status:** PARTIALLY FIXED for names whose 100-byte prefix is valid UTF-8. S1.8 and D10.2.2
apply. The newly recorded `\renewcommand` diagnostic contains the entire
110-byte name, while native truncates it to 100 bytes when constructing the
message. Evidence: `../lambda-input/temp/expl3-script/long-command-name.out`;
`lambda/input/input-tex-latex.cpp` constructs the message with a 100-byte name
limit. UTF-8 truncation at a code-point boundary requires separate handling.

The diagnostic now uses the native 100-byte prefix for valid UTF-8. A prefix
that cuts a multibyte character explicitly reports an unsupported encoding
until byte-preserving diagnostic strings are qualified.


## Nested definitions in echoed source groups — 2026-11-13

**Status:** PARTIALLY FIXED for tested nested definitions. S1.8 and D10.2.2 apply. After the
source-whitespace fix, `math_proof.tex` reached an explicitly unsupported
`renewcommand` inside the separated body of a `newenvironment`. A prototype
retains the enclosing echoed span's TeX category codes and reuses the existing
definition digester for nested definitions. Initial cases match native ASTs;
whitespace within a nested definition remains guarded. The experimental
pipeline then advances to a `math atom` guard. Evidence:
`../lambda-input/temp/expl3-script/nested-definition-v2.out` and
`math-proof-nested-interp.out`. The installed implementation rereads with the
original category codes and verifies token kinds, values, categories and source
offsets before accepting the result. `test/lambda/latex/test_tex_script_nested_definition.ls`
contains 22 native AST matches, one source-gap guard and two invalid-parameter
cases. Five existing goldens and sampled forced GC pass in both backends; the
current executable also matches the reference. Included-file AST whitespace
remains a separate open issue below.

Constructor-mode definitions inside ordinary command arguments now also retain
their category-code snapshot and reuse this parser. Included-file node and
argument offsets are mapped through reconstructed output segments. The new
`test_tex_script_foreign_definitions` compares main and included forms with
native and retains explicit guards where the native source scanner tokenizes a
control name differently. Mixed-file spans and macro-expanded bodies remain
guarded; see the current checkpoint for qualification.


## Included definition whitespace creates an extra paragraph — 2026-11-13

**Status:** OPEN; blocks direct AST parity for the reproduced included source.
S1.8 and D10.2.2 apply. A main file that inputs a child containing a multiline
`\newenvironment` declaration produces an extra paragraph containing two
newlines in the Script AST; native parsing omits it. The mismatch reproduces
with the installed digester before the nested-definition candidate. Evidence:
`../lambda-input/temp/expl3-script/nested-definition-foreign-before.out` and
fixture files in `environment-diagnostic-fixture/`. The exact source-origin
and whitespace cause is unconfirmed. The reconstructed Script TeX text does
match native for this fixture, so the observed difference is in AST digestion
or source-origin interpretation; see `nested-definition-foreign-expansion.out`.
The nested-definition candidate does not fix included-file AST parity.


## Raw math text for parameter markers and exclamation marks — 2026-11-13

**Status:** PARTIALLY FIXED for tested raw-math runs; work remains in the experimental
`math_proof.tex` pipeline. S1.8 and D10.2.2 apply. A macro body containing
`$#1$` reports `math atom` in the Script digester, while native produces a
`raw_math_text` node whose value is `#1`. Native's math parser groups runs
starting with `#` or `!` and continuing with those characters or decimal
digits. Evidence: `../lambda-input/temp/expl3-script/math-atom-debug.out`,
`math-parameter-native.out`, and `lambda/input/input-latex-c.cpp`. Source
adjacency and attached scripts must be preserved when porting this rule.


## Escaped punctuation in math — 2026-11-13

**Status:** PARTIALLY FIXED for tested escaped punctuation; work remains in the experimental
`math_proof.tex` pipeline. S1.8 and D10.2.2 apply. After the staged raw-math
and `\Box` support, the sample stops at `math command {` for `\{`.
Native emits an `escaped_symbol` and combines adjacent compatible escapes;
`\_` is a separate atom. Evidence: `../lambda-input/temp/expl3-script/math-proof-box-interp.out`,
`math-brace-native.out`, and the math command parser in
`lambda/input/input-latex-c.cpp`. Source adjacency and scripts must be kept.


## Overset and underset math commands — 2026-11-13

**Status:** PARTIALLY FIXED for tested annotation commands; work remains in the experimental
`math_proof.tex` pipeline. S1.8 and D10.2.2 apply. After staged escaped
punctuation support, the sample stops at `math command underset`. Native
uses `overunder_command` with parsed annotation and base, and maps
`\stackrel` to `\overset`. Evidence:
`../lambda-input/temp/expl3-script/math-proof-escaped-interp.out`,
`math-under-over-native.out`, and `lambda/input/input-latex-c.cpp`.


## Math-proof sample AST differences — 2026-11-13

**Status:** OPEN in the experimental reconstructed pipeline; blocks AST
parity for this sample. S1.8 and D10.2.2 apply. With staged raw math,
escaped punctuation, `\Box` and over/under commands, `math_proof.tex` now
parses and renders HTML matching native, but its AST differs. The Script AST
retains an `ifthen` package node absent from the native result and differs in
several whitespace or empty-paragraph nodes. Evidence:
`../lambda-input/temp/expl3-script/math-proof-overunder-interp.out` and the
`math-proof-native-ast.out` / `math-proof-script-ast.out` diff in that folder.
The package difference came from the experiment's empty bundled-resource
snapshot: supplying the shipped `ifthen.sty` removes that difference, while
three whitespace or empty-paragraph differences remain. Evidence:
`math-proof-with-ifthen-interp.out` and the diff against
`math-proof-ifthen-ast.out` in the same temporary folder. The whitespace cause
is unconfirmed; matching HTML does not establish AST parity or completeness.

## Qualified raw and annotated math atoms — 2026-11-13

The Script digester now handles raw runs beginning with `#` or `!`, escaped
punctuation runs, `\Box`, and `\overset` / `\underset` / `\stackrel`.
S1.8 and D10.2.2 apply. `test/lambda/latex/test_tex_script_math_raw.ls` compares
54 native ASTs and records three existing source or command guards. Five
related math and definition goldens and sampled forced GC pass in interpreter
and JIT. The separately staged reconstructed pipeline renders the math-proof
sample with matching HTML but still has the AST differences recorded above.
Native dollar-delimited escaped-dollar comparison is blocked by input issue
LI-030; the named-delimiter equivalent is covered.


## Direct digestion loses comment-adjacent paragraph whitespace — 2026-11-13

**Status:** PARTIALLY FIXED for the qualified direct paragraph cases.
S1.8 and D10.2.2 apply. With `A\n%comment\n\nText` inside a document, the
earlier Script digester emitted separate `A` and `Text` paragraphs, while
native places `A\n\nText` in one paragraph. A comment gap after an echoed
`\newcommand` can also lose a newline. Evidence:
`../lambda-input/temp/expl3-script/paragraph-direct-before.out`. The direct
digester consumes TeX `par` tokens whose source spans can include skipped
comments, while native paragraph splitting is based on adjacent source line
feeds. Source-aware comment and line-boundary normalization needs to be
shared or ported before claiming parity; simply keeping a space is not enough
for all reproduced cases.


The isolated `paragraph-direct-stage-home` candidate normalizes direct literal
newline runs and preserves comment-gap text. Its first broad pass had 41 native
AST matches and one existing math-source guard; a wider matrix then exposed
two generated-macro cases. Reconstruction evidence shows that a non-adjacent
TeX `par` token after generated text emits a full pair of newlines, not its
single source byte (`../lambda-input/temp/expl3-script/macro-comment-reconstruction.out`).
The revised candidate matches 41 focused cases and 64 combinations of source
whitespace, comments, formatting, headings and generated macros in both
backends. A bounded-chunk version has identical results and matches 18 cases
around chunk boundaries in both backends. Five existing goldens and sampled forced GC also pass in both backends. It
remained staged pending a release-profile performance assessment; unrelated
concurrent Lambda build changes made a shared rebuild inappropriate at the time.

2026-11-15: A new implementation shares emitted source spelling with
`reconstruct`, interprets paragraph boundaries and comments in that source,
and retains the source's CRLF behavior and generated-token adjacency. A
focused 30-case test compares native ASTs for comments, formatting, generated
macros, headings and explicit `\par`; it passes the installed harness in both
tiers. Six selected cases also pass sampled forced GC with poisoning in JIT.
An explicit `\par`/CRLF case passes the same profile in the interpreter.
`test_tex_script_paragraph_lengths` covers 18 whitespace lengths around 64,
128 and 256 characters with and without CRLF comments in both harness tiers.
The 72-case matrix and 25 edge cases passed staged JIT checks, and six existing
goldens passed both tiers before a final equivalent helper cleanup. Release
measurements and remaining qualification are recorded in the checkpoint.
General source-origin mapping and reconstructed-pipeline parity remain open.

## Reconstructed paragraph comment gaps — 2026-11-13

**Status:** STAGED; the reconstructed pipeline remains uninstalled. S1.8 and
D10.2.2 apply. A comment between literal paragraph breaks can create an empty
native paragraph, and a preceding newline must survive when comments separate
it from the following break. The isolated candidate preserves both boundaries.
It matches 27 focused comment/whitespace cases, the six-document AST/HTML
corpus and the scientific-writing sample in interpreter and JIT. With the
shipped `ifthen.sty` supplied, `math_proof.tex` now matches both native AST and
HTML in both backends. Sampled forced GC also passes. Evidence:
`../lambda-input/temp/expl3-script/paragraph-comment-v4-interp.out`,
`paragraph-corpus-interp.out`, `paragraph-scientific-interp.out`, and
`math-proof-comment-interp.out`. The pipeline still reports `complete: false`;
the direct-digester comment gap and the separately staged math-golden update
remain open. Do not infer full compatibility from this corpus.


## expl3 integer predicate forms — 2026-11-13

**Status:** PARTIALLY FIXED for the qualified predicate and constructor cases;
direct digestion of character definitions in arguments remains guarded. S1.8
and D10.2.2 apply. `\int_if_zero_p:n`, `\int_if_odd_p:n`, and
`\int_if_even_p:n` now use the native-shaped expandable macros and conditional
primitive aliases. Numeric expressions, negative and computed values, aliases,
macro meanings, conditionals, constructor expansion, diagnostics, and tested
document-level ASTs match native. The new
`test/lambda/latex/test_tex_script_expl3_int_predicate.ls` covers these cases in
both execution backends. Four existing integer and boolean goldens and sampled
forced GC also pass in both backends.

Direct digestion executes a character definition before parsing a constructor
argument, while native argument processing retains it. The token now records
its origin and the digester reports `character definition argument source
mapping` for this unqualified path; direct top-level use remains supported.
Evidence: `../lambda-input/temp/expl3-script/int-predicate-v6-interp.out` and
`chardef-guard.out`. Full source-aware argument digestion remains open.


## Numeric conditional delimiters encountered while scanning operands — 2026-11-13

**Status:** PARTIALLY FIXED for qualified numeric and case conditionals;
full malformed-input recovery remains open. S1.8 and D10.2.2 apply. Production remains native.
Before the fix, `\ifnum1=1\else Z\fi X` produced `X ` without diagnostics natively, while
the Script engine stopped with `tex-extra-fi`; a space between `1` and `\else`
avoids the mismatch. The false comparison `\ifnum1=2\else Z\fi X` also
failed, while an ordinary `\iftrue` empty branch succeeded. Evidence:
`../lambda-input/temp/expl3-script/empty-if-probe.ls` and `.out`.
The Script numeric scanner expands the next token before `begin_conditional`
adds the conditional to its stack, so it executes the delimiter against the
wrong condition depth. This is independent of the definition source-offset work.


The Script engine now records a pending conditional while scanning, inserts a
relax before an early delimiter, and preserves nested conditional state when
the operand ends. A token returned by integer digit scanning also loses a
consumed `\noexpand` marker, allowing its normal subsequent expansion. The
new `test/lambda/latex/test_tex_script_numeric_conditional.ls` checks 73 native
text/message/diagnostic cases and 11 AST cases, including true and false
branches, aliases, nested conditions, `\ifcase`, `\expandafter`, and
`\noexpand`. Both execution backends and sampled forced GC pass. Focused
existing expansion, expl3 integer and random goldens pass both backends.
Evidence: `../lambda-input/temp/expl3-script/numeric-case-golden-interp.out`.


## expl3 token-list head — 2026-11-13

**Status:** PARTIALLY FIXED for expansion; direct AST source mapping remains guarded.
S1.8 and D10.2.2 apply. Before the fix, `\tl_head:n{abc}` stopped with
`tex-unsupported-primitive` while native emits `a`, and `\meaning\tl_head:n`
was undefined in Script. Evidence: `../lambda-input/temp/expl3-script/
tl-head-before.ls` and `.out`. The native macro uses expandable primitives
and a helper to preserve token and group semantics; expansion, empty inputs,
category codes, macro meaning, source mapping and constructor arguments need
qualification. Production remains native.


The Script profile now installs the native-shaped expandable `\tl_head:n`
and helper macros, backed by the existing `\expanded` and `\unexpanded`
primitives. The new `test/lambda/latex/test_tex_script_expl3_tl_head.ls`
compares 35 text, message and diagnostic cases, including macro meanings,
token categories, grouping, aliases, and constructor arguments. Eight direct
AST cases report `expl3 token head source mapping`: a control sequence
returned as the head can have a native emission offset different from its
original token location. The flag follows meaning aliases and resets on
resume. Both backends, focused token-list goldens and sampled forced GC pass.
Evidence: `../lambda-input/temp/expl3-script/tl-head-final-interp.out` and
`tl-head-cases-interp.out`. Full direct source mapping remains open.


## expl3 token-list tail — 2026-11-13

**Status:** PARTIALLY FIXED for expansion; direct AST source mapping remains guarded.
S1.8 and D10.2.2 apply. Before the fix, `\tl_tail:n{abc}` stopped with
`tex-unsupported-primitive` while native emits `bc`, and `\meaning\tl_tail:n`
was undefined in Script. Evidence: `../lambda-input/temp/expl3-script/
tl-tail-before.ls` and `.out`. Expansion, blank and grouped inputs, category
codes, macro meaning, source mapping and constructor arguments need
qualification. Production remains native.


The Script profile now installs the native-shaped expandable `\tl_tail:n`
macro with its native primitive aliases. The new
`test/lambda/latex/test_tex_script_expl3_tl_tail.ls` compares 35 text, message
and diagnostic cases, including macro meanings, token categories, grouping,
aliases, and constructor arguments. Twelve direct AST cases report
`expl3 token tail source mapping`: returned control sequences can have native
emission offsets different from their original token positions. The flag
follows meaning aliases and resets on resume. Both backends, focused token-list
goldens and sampled forced GC pass. Evidence:
`../lambda-input/temp/expl3-script/tl-tail-final-interp.out` and
`tl-tail-v2-interp.out`. Full direct source mapping remains open.


## expl3 token-list head and tail N variants — 2026-11-13

**Status:** PARTIALLY FIXED for `:N`; distinct `:V` variants remain open.
S1.8 and D10.2.2 apply. `\tl_head:N` and `\tl_tail:N` were absent from the
Script profile, as was their expandable `\exp_args:No` helper. With a variable
containing `abc`, native emits `a/bc`, while Script stopped at `\tl_head:N`.
Evidence: `../lambda-input/temp/expl3-script/tl-n-before.ls` and `.out`, plus
`tl-variants-native.out` for native meanings. The helper must perform exactly
one expansion and retain token, scope, alias and source-mapping semantics;
the distinct `:V` variants need separate support. Production remains native.


The profile now installs the native-shaped expandable `\exp_args:No`,
`\tl_head:N` and `\tl_tail:N` macros. The new
`test/lambda/latex/test_tex_script_expl3_tl_head_tail_n.ls` compares 24 native
text, message and diagnostic cases, including one-step expansion, scope,
aliases, meanings and constructor arguments. Existing head/tail guards apply
to their `:N` forms. The generic helper separately reports `expl3 one-step
argument source mapping` in direct digestion because an expanded heading can
carry a different native emission offset. Its guard follows aliases and resets
on resume. Both backends, focused token-list goldens and sampled forced GC
pass. Evidence: `../lambda-input/temp/expl3-script/tl-n-final-v2-interp.out`
and `tl-n-ast-detail.out`. Full direct source mapping remains open.


## expl3 token-list head and tail V variants — 2026-11-13

**Status:** PARTIALLY FIXED for qualified values; invalid-variable diagnostics remain open.
S1.8 and D10.2.2 apply. `\tl_head:V`, `\tl_tail:V`, and their expandable
`\exp_args:NV` helper were undefined in the Script profile. With a variable
containing `abc`, native emits `a/bc` while Script stopped at `\tl_head:V`.
Evidence: `../lambda-input/temp/expl3-script/tl-v-before.ls` and `.out`.
Unlike `:N`, `:V` uses variable evaluation, including register and invalid
variable handling; macro meanings, tokens, source mapping and diagnostics
need qualification. Production remains native.


The Script profile now installs native-shaped `\exp_args:NV`, variable
resolution helpers and `\tl_head:V` / `\tl_tail:V`. The new
`test/lambda/latex/test_tex_script_expl3_tl_head_tail_v.ls` compares 28 native
text, message and diagnostic cases, including token lists, explicitly defined
count/dimension/token registers, aliases, macro meanings and constructor
arguments. Seven direct AST cases report the existing head/tail or new value
argument source-mapping guards; the new guard follows aliases and resets on
resume. Both backends, focused token-list goldens and sampled forced GC pass.
Evidence: `../lambda-input/temp/expl3-script/tl-v-final-v3-interp.out`.

Invalid variable recovery remains open: `\tl_tail:V\scan_stop:` stops with
`tex-missing-number` in Script while native reports `tex-macro-mismatch`;
see `../lambda-input/temp/expl3-script/tl-v-register-final.out`. Named
registers allocated with `\newcount` and related commands produce different
allocation messages because the separately recorded expl3 preload allocation
state is incomplete, although their tested values match.


## Noexpand token meaning in ifx — 2026-11-13

**Status:** PARTIALLY FIXED for tested comparisons; full variable evaluation
and malformed-input recovery remain open. S1.8 and D10.2.2 apply.
For `\def\x{A}\expandafter\ifx\noexpand\x\x T\else F\fi`,
native emitted `F`, while Script emitted `T`; comparing the protected token with
`\relax` had the opposite discrepancy. Script `ifx` read the original
meaning despite its one-use `noexpand` marker. Evidence:
`../lambda-input/temp/expl3-script/noexpand-ifx.ls` and `.out`. The correction
must preserve aliases, active tokens, undefined controls, nested conditionals
and expansion semantics. Production remains native.


The Script `\ifx` comparison now treats a one-use `\noexpand` marker as
`\relax`, while leaving the stored control-sequence meaning unchanged. The
new `test/lambda/latex/test_tex_script_noexpand_ifx.ls` compares 26 native
text, message and diagnostic cases and four AST cases, including undefined
controls, aliases, active tokens, register kinds and nested conditionals. Both execution
backends, focused conditional/expl3 goldens and sampled forced GC pass.
Evidence: `../lambda-input/temp/expl3-script/noexpand-ifx-final-interp.out`.


## Invalid unless recovery — 2026-11-13

**Status:** FIXED for the tested malformed prefix cases. S1.8 and D10.2.2 apply. For
`\def\x{A}\unless\expandafter\ifx\noexpand\x\x T\else F\fi`,
native records `tex-bad-unless` with a location and continues to emit `F`,
while Script previously stopped with the issue and no diagnostic record. Evidence:
`../lambda-input/temp/expl3-script/bad-unless-native.out` and
`noexpand-ifx-cases-interp.out`. The Script engine now records the native
diagnostic and replays the token, and it finishes quietly at end of input as
native does. The new `test_tex_script_unless_recovery` golden compares 14
expansion cases and two ASTs, including aliases, nested conditionals, invalid
`ifcase`, and source lines. The companion foreign-source golden compares the
included-file diagnostic and expanded text. Both goldens match in interpreter
and JIT; the focused harness passes in both tiers. Production remains native.


## Noexpand marks unexpandable registers — 2026-11-13

**Status:** PARTIALLY FIXED for tested unexpandable register kinds; broader
variable evaluation and diagnostics remain open. S1.8 and
D10.2.2 apply. For `\newcount\n\expandafter\ifx\noexpand\n\n T\else F\fi`,
native emitted `T`, while the earlier staged correction emitted `F`. The Script
`noexpand` routine marked register controls even though they are unexpandable;
the staged `ifx` code then interpreted the marker as relax. Dimension and token
registers reproduce the same issue. Ordinary `\the` register reads match;
`\exp_args:NV` over these registers fails. Evidence:
`../lambda-input/temp/expl3-script/noexpand-register-before.out` and
`tl-v-register-probe.out`. Expansion classification, aliases and register
kinds need qualification. Production remains native.


The corrected classification leaves unexpandable registers and character
definitions unmarked. Count, dimension, token and glue registers, character
definitions and ordinary numeric reads match native in both backends; see
`test/lambda/latex/test_tex_script_noexpand_ifx.ls`.


## expl3 token-list dynamic-name head and tail — 2026-11-13

**Status:** PARTIALLY FIXED for qualified dynamic-name values; invalid-name diagnostics remain open.
S1.8 and D10.2.2 apply. For a token-list variable containing `abc`, native
emits `a/bc` from its dynamic name, whereas Script stopped at `\tl_head:v`;
the three macro meanings were undefined. Evidence:
`../lambda-input/temp/expl3-script/tl-v-lower-before.ls` and `.out`.
Dynamic control-sequence construction, value evaluation, scope, aliases,
source mapping and invalid-name diagnostics need qualification. Production
remains native.


The Script profile now installs native-shaped `\exp_args:Nv`, the dynamic
variable-resolution helper, and `\tl_head:v` / `\tl_tail:v`. The new
`test/lambda/latex/test_tex_script_expl3_tl_head_tail_v_lower.ls` compares 26
native text, message and diagnostic cases for expanded names, scoped token
lists, explicitly defined count/dimension/token registers, aliases, meanings
and constructor arguments. Seven direct AST cases use the existing head,
tail or value-argument source-mapping guards, including aliases and resume.
Both backends, focused existing goldens and sampled forced GC pass. Evidence:
`../lambda-input/temp/expl3-script/tl-v-lower-v2-interp.out`.

Invalid-name diagnostics share the open variable-evaluation recovery gap.
Dynamic `\tl_set:cn` assignment remains separately unsupported; it was
observed while testing this feature (`tl-v-lower-cases-interp.out`).


## Protected token-list setter provenance prototype — 2026-11-13

**Status:** STAGED; the macro-backed setter and `\tl_set:cn` wrappers are not
installed. S1.8 and D10.2.2 apply. A provenance tag on the native-shaped
`\tl_set:Nn` and `\tl_gset:Nn` macro bodies distinguishes their internal
unexpanded assignment from token-list insertion. The prototype preserves the
previously supported direct setter AST and matches the `\tl_set:cn` catcode
lookahead reproducer, while retaining the insertion guard. Evidence:
`../lambda-input/temp/expl3-script/tl-set-provenance-probe.out`,
`tl-set-provenance-cn.out`, and `tl-set-guard-cases-interp.out`. The analogous
sequence-from-clist wrapper remains unsupported.

This is not ready to install: a setter inside a constructor or generic
command argument can be executed by direct expansion while native argument
processing preserves its expansion. The prototype silently differs for
`\textbf{\tl_set:Nn\l_tmpa_tl{A}}`, optional and required heading
arguments, and a generic `\foo` argument. A plain standalone group and a
setter preceding a formatting command match native. Evidence:
`../lambda-input/temp/expl3-script/tl-set-constructor-ast.ls` and `.out`.
The underlying constructor-argument source context must be tracked or parsed
faithfully; a broad guard would reject previously supported cases, while an
incomplete guard would hide mismatches. A nested macro-definition case is
explicitly guarded in the prototype and remains subject to the separately
recorded native source-boundary issue. Production remains native.


## expl3 token-list f-expanded head and tail — 2026-11-13

**Status:** PARTIALLY FIXED for the qualified expansion cases; direct AST
source mapping remains guarded.
S1.8 and D10.2.2 apply. For a two-step macro expanding to `abc`, native
emits `a/bc` from the head and tail, while Script stops at `\tl_head:f`;
the three macro meanings are undefined. Evidence:
`../lambda-input/temp/expl3-script/tl-f-before.ls` and `.out`. The `f`
expansion boundary, spaces, protected controls, token identity, source mapping
and diagnostics need qualification. Production remains native.

2026-11-15: The Script profile now installs the native-shaped `\exp_args:Nf`,
`\tl_head:f`, and `\tl_tail:f` macros. The existing `\exp:w` and
`\exp_end_continue_f:w` implement the expansion boundary. While comparing
against native, a `\noexpand` token following an alphabetic numeric constant
retained its one-use marker after the numeric scan; the character-number
scanner now consumes that marker just as the digit scanner does. The focused
golden compares eleven text, meaning, message and diagnostic cases against native,
including nested macros, spaces, groups, protected tokens, aliases and the
numeric scan. It passes interpreter and JIT. The digester guards generic `f`
argument provenance, and the existing head/tail guards remain in force; the
test also checks that the new guard resets on resume. Expanded-output source
mapping, broader malformed-input recovery, and production migration remain open.
The adjacent `noexpand_ifx` JIT golden passed. The numeric conditional golden
exceeded the harness's 60-second deadline, then matched when run directly in
JIT mode without that deadline; this does not establish an aggregate gate.

## Reader records and error boundaries — 2026-11-17

The reader retains its snapshot API (S1.6). Stream switching, category-code
changes and source reconstruction already depend on copied reader values.
An escaping closure would not supply shared mutable state, and the current fn
API cannot call nested pn scanners (S9.1.4v2, S12.1.1v3, D7.2.3). Local loops
inside fn are permitted by S12.1.2v3 and keep the existing public boundary.

`Reader`, `Token`, `ReadStep`, `SourceSpan` and `Capture` make those boundaries
explicit. Internal `TextScan` and `LineStep` keep character/line scanning
structured: EOF has a reader/token absence result, raw capture failures have
an issue, and an empty character is the private end-of-text sentinel. Queries
return absence under S7.10.7; text_span supplies the empty text default once.
catcode validates optional overrides and supplies the standard category for
invalid values. Its int conversion is the reader's only error handler.
UTF-8 resource loading now produces valid/text at one decode_source boundary;
invalid data still generates the existing TeX diagnostic. No downstream
scanner accepts a general error result from these input helpers.

The typed refactor exposed compiler admission defects. Early literal storage
shapes deliberately remain conservative for forward calls (D7.1.3), but they
are insufficient evidence of a final record return contract. Return admission
now examines final field expressions and ordered spreads (S7.7.2v2,
S11.4.1v5). Known literal union arms can establish their own membership
(S11.2.1). Computed keys, unknown spread fields and optional overwrites retain
dynamic admission. A failed spread remains an error even if later fields
would satisfy the successful record contract. Physical storage compatibility
and conversion retain their separate checks (D3.4.6); the existing MIR sidecars
were retained without weakening their assertions.

Explicit any fields still accept contained error data (S11.4.3). The return
proof preserves this top-type membership instead of imposing the nested
record's shape on any. Interpreter admission had a separate disagreement:
after admitting every field, a redundant whole-record validator rejected an
error stored in any. Plain record admission now retains the completed field
proof. Nominal, binary, namespace and JavaScript-specific contracts retain
their additional validation; field conversion and layout reification remain
unchanged. The IME cleared/cancel, paint mark and sketch map_objects wrappers
now require map sources, so their clean map returns do not conceal a possible
invalid spread.

`record_return_boundary` tests accepted state updates, snapshot preservation,
branch returns and rejection of malformed dynamic data. The negative
record_return_spread_error and record_return_nested_spread_error fixtures
check that failed direct/nested spreads cannot cross a return contract
excluding error. The positive fixture also checks nested data and contained
errors in explicit any fields across interpreter, JIT and AUTO.
`test_tex_script_reader_contracts` checks
clean caller contracts, EOF, UTF-8 byte offsets, dynamic categories, malformed
overrides, raw-capture issues, superscript decoding and long raw/control-word
scans. The existing iterative-reader test drops a redundant caret on its
statically admitted literal source. Three unrelated typed record/cache callers
also drop carets where their constructor outcomes are now proven error-free;
their data and goldens are unchanged.

Two existing LaTeX assertions were stale before this refactor. The guarded
environment-option case already succeeds with the previous reader; both
readers match the native AST. It now asserts that equality explicitly. The
marks/special case compared message count with zero despite the native
kernel-message result; it now compares the complete native message list and
text. The previous and installed readers both satisfy the corrected
assertions. These corrections do not waive an engine/native difference.

The remaining language friction is recorded in the central
[issue ledger](../Lambda_Issue_Ledger.md#reader-record-contracts): spread bindings
need explicit state annotations, mixed branch shapes benefit from one typed
TextScan constructor, the staged `is int` narrowing probe still needs explicit
admission, and imported type names need a local binding before annotation.
These are bounded use cases for future work, not grounds to widen the reader
back to any^.

Qualification evidence is under temp/reader-refactor/. The baseline reader
SHA256 is 1e39c1ecc2375d906b1d9e016e6cd79ea1c550dcf62c4aaa3230bdfc19daa6f2;
the installed reader is bf6b0d547f020d4e7bf6142f3252bd0f4f681f8b73225b2b53147cdbf856158e.
The first release host is pinned as lambda-release, SHA256
98606dcc79c1d176b592a0a4ca0d85e086386020c4013f041cd5e862ed42a6a0.
The final release host, including the plain-record admission repair, is
lambda-release-final, SHA256
7a943527779be0a108e277f5455e37acd1752d2e407af9380cc79ae41d5801d2.
final-host.json also records the compiler/runtime source hashes. The reader
hash remained unchanged throughout the release qualification.

- Final-host differential: 352 token/state and 39 raw-capture cases agree with
  the previous reader in each of interpreter and JIT (782 comparisons).
  contracts-complete.json also records six passing failed-spread negatives
  across interpreter/JIT/AUTO, and an AUTO positive record check with GC.
- Final-host focused runs: 23/23 exact goldens, including reader, streams,
  iterative scanning, verbatim/raw capture, read streams, source provenance,
  query totals and record returns. Scheduled GC uses
  LAMBDA_GC_FORCE_EVERY=1000 plus LAMBDA_GC_POISON_FREED=1 in both tiers;
  see focused-complete.json. All 285 LaTeX entries also compile on this host
  (compile-tex-complete.json).
- The full JIT corpus ran on the first pinned release host: 279/285 matched
  immediately; two stale assertions were corrected against native as described
  above, and four cases timed out at 120 seconds under eight-worker contention.
  The final host's serial follow-up passes all six exact goldens: environment
  options 19.390s, expl3 integer math 40.906s, inline-without-group 26.694s,
  inspection 28.432s, kernel messages 30.900s and marks/special 13.106s, all
  within the original 120-second limit. This closes all six corpus rows,
  giving 285/285 across the full sweep plus follow-up. The final-host follow-up
  is recorded separately in release-followup-goldens.json; it does not claim
  a second complete sweep after the final runtime admission change.
- Final isolated make test262-baseline: 40,261/40,261 fully passing, no
  regressions or non-fully-passing cases (test262-complete.log).
- make test-lambda-baseline is not green: its raw result is 7,202/7,238,
  including a native suite abort represented as one failed suite instead of
  its full case count. After the map-parameter repairs, 33 failed individual
  cases pass focused rechecks: 25 script cases in the 55-case spread-caller
  selection, proc_native_var_proc, four DOM cases and three editing GC cases. See
  baseline-followup.json and its four logs. MIR emission is 595/595,
  MIR GC stress 368/368, and emission/ownership units 66/66 in the aggregate.
- The remaining observed aggregate failures are proc_sync_rtc (unavailable
  WebRTC dependency), EditBridgeSessionTest.ExecAppliesRichTextBlockCommands,
  and PdfFileBuilder.RejectsDictionaryMetadataAllocationFailure. The PDF
  allocation test aborts with an invalid free, both after 336 preceding native
  successes and when selected alone; no full runtime-suite pass is claimed.
  Logs are lambda-baseline-complete.log, runtime-errors-complete.log,
  pdf-allocation-isolated.log and edit-bridge-complete.log. These are retained
  qualification limits, not changes to the fixtures or harness.

The final release measurement uses the same 8,000-token workload and host for
both reader versions, alternating order over three pairs. All six outputs
are [8000,8000], SHA256
f7073a87a059c1d62b6730d9a57d49732ac0351aa220ac990878033686af6e9a.
Previous-reader CPU samples are 0.2066, 0.1929 and 0.1999 seconds; typed-reader
samples are 0.3163, 0.3184 and 0.3243 seconds. Medians are 0.1999 versus
0.3184 seconds (1.59x). This includes process startup and JIT compilation;
it does not establish hot-loop cost or isolate the cause. The earlier host
showed 2.13x; that cross-host change is not causal evidence for a particular
fix. Timings, wall time, per-sample hashes and scripts are retained in
release-token-bench-final.json, run_token_bench_final.py and
_tokens_bench_{baseline,candidate}.ls under temp/reader-refactor/.
