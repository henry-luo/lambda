# Lambda Alpha Documentation Pass — Record and Structure Review

**Status:** pass 1 done 2026-09-28 (user docs updated to the built features); structure proposals await the user.
**Scope:** `doc/*.md` user-facing documentation and `README.md`. The formal specs, `vibe/` and `doc/dev` were read, not edited.

---

## 1. What was done

### 1.1 Method

1. `make check-doc-code` (Doc_Convention §8.1) before and after: 555 units extracted from `doc/**`; **26 failing before, 0 after** (the `check-doc-code` summary is the evidence; failing units were fixed at the doc, never masked with `no-run` unless the feature is genuinely unbuilt, in which case the block says so).
2. Every table row of the Cheatsheet compiled under the `expr`/`plain`/`type` wrappers (239 rows; the only self-contained row that failed was the multi-binding let-expression, now fixed). Cheatsheet tables carry no `<!-- code-fence -->` annotations, so the gate does not cover them — see §3.7.
3. ~130 live probes against `./lambda.exe` for claims the gate cannot check (results, not just compilation): paths, datetime, `sys.*`, identity, document updates, word comparisons, first-class types, constraints, sys-func arities.
4. Six parallel audits of the area docs against the code: formats, math, CLI, sys-func registry, JS/DOM + Reactive UI, hosted languages.

### 1.2 Per-document changes

| Document | Change |
|---|---|
| `Lambda_Reference.md` | Intro rewritten (GC not refcount, interpreter + JIT tiers, utf8proc not ICU, alpha-status note); documentation guide expanded into a full index of every user doc; two failing blocks fixed; `\.config.json` → `\.'config.json'` |
| `Lambda_Syntax.md` | Reserved keywords and type names regenerated from the lexer table; new **Line Continuation** section (S16.2); the "trailing `;` is an error" claim removed |
| `Lambda_Data.md` | Path table (extensions need a quoted step; `sys.proc.self.env.*`, `sys.os.platform`); identity block now uses a `temp` document; `sys.*` reference pruned to the keys `sysinfo.cpp` implements; datetime constructors reduced to the built forms (`datetime(ms)`, `date(y,m,d)`, `time(h,m,s)`); `'human'` format removed; `array((…))` replaced by `[*(…)]`; `{*:state}` example (`state` is a keyword); `open` example creates its document first; `var` examples wrapped in `pn` |
| `Lambda_Type.md` | First-class type examples restricted to what works; `<:` on named types; `none` block separators; `(-100) to 100`; open-map row replaced by a note (S11.4.6); nested function type needs a parameter name; `as` cast section replaced ("No Casts", SO9/SO33); status callouts for object-type constraints (unenforced) and the array-element binder (unbuilt) and `is` against a parameter type (unbuilt) |
| `Lambda_Expr_Stam.md` | New **Element-wise Comparison** section for `eq ne lt le gt ge` (S10.2.2/S10.2.3, built but undocumented); precedence row; "Set Operators" table replaced by type operators (S10.1.1v3); bogus `[1,2] \| [2,3]` union row removed; `null.reverse()` result |
| `Lambda_Func.md` | `pn fold` example called from a `pn`; `reduce` pointer |
| `Lambda_Error_Handling.md` | `T^` in value position marked not yet implemented (`no-run`); error-code table completed from `lambda-error.h` |
| `Lambda_Procedural.md` | `var` needed for map mutation; resource model marked designed-not-built (S12.4); path fix |
| `Lambda_Cheatsheet.md` | CLI section covers every command; `Circle : Point`; let-expression rows; datetime rows; concurrency block compiles; sys-func lists (no `set`/`number`/`diff`/`map(f,v)`/`filter(f,v)`; added `count`, `name`, `clip`, shape functions); element-wise comparison rows; full input/output format lists |
| `Lambda_Sys_Func.md` | Removed dead `number()`/`set()`/`int64()`; fixed `is -inf`, `shl` overflow (saturates to `inf`, S4.1.3), `cmd(cmd, args)` arity, `math.random` arity, `error`/`symbol`/`normalize`/`date`/`time`/`sum`/`avg`/`io.fetch` arities; added bare `sqrt`…, `lambda.sys.*` spellings, `f16`/`f32`; new **Also Available** section listing 50+ registered-but-undocumented functions (complex, array shape, image processing, editor bridge, `io.http_*`) |
| `Lambda_CLI.md` | Binary name; hosted-language dispatch; global flags (`--tier`, `--static-warning`, `--no-drain`); `run` flags; `validate` single-file + root-type rule + real auto-detect list; `convert`/`layout`/`render`/`view`/`edit` flags from the parsers (`--flavor` removed: it never existed); `js` rewritten; `ts`, `py`, `replay`, `serve`, `math` documented honestly; environment variables table |
| `Lambda_Validator_Guide.md` | Rewritten: schema syntax, root-type rule, CLI usage with real output, semantics, built-in schemas. The script-level API (`load_schema`, `validate_with_*`) never existed and is marked not available |
| `Lambda_Doc_Pipeline.md` | 20 MB not 9 MB; HTML root is `<#document>`; `int()` not `num()`; validation is CLI-only |
| `Markup_Formats_Support.md` | Rewritten with real `convert -t mark` output for every format; type strings corrected (`{type, flavor}`, `graph:mermaid`, no `kv`/`dot`/`input_str`); added Typst, Mark, Structurizr, SQLite; INI/properties typed values; EML/VCF/ICS real key names; LaTeX `latex_document` shape; RTF/CSS/MDX limitations; output-format table; no CSV writer |
| `Doc_Schema.md` | Repaired intro; role and file location; "what the parsers emit today" note; `<del>`, `<footnote>`/`<footnote-ref>`, `<code type>`, `<math type: block>`, `<citation key>`; `<meta>` and the format mapping marked target design; emoji, `<field>`, `<reference>`, `<citations>` removed |
| `Math_Support.md` | `parse()` not `input()` for strings; direct parser not tree-sitter; `lm_*` classes and fonts; environment list trimmed to the 14 parsed; `\mskip`; ASCII flavor described as built; §11 gaps corrected; output formats and stubs |
| `Reactive_UI.md` | Rewritten as a user doc: `{mode: "edit"}`, patterns and specificity, state, full event object and event list, handler return values, `emit`, incremental patching, no internal C symbols; all blocks compile |
| `JS_DOM_Support.md` | Rewritten as a status doc: test262 40,261/42,889, Node 3,450, 9 WPT baselines; feature coverage from the baseline; DOM summary; Node compat; Result49 benchmarks (7.8×/8.66× vs Node, Lambda Script 0.63×) replacing the stale 2.2× table; architecture |
| `Python_Support.md` | External-module status and the import/package crash; stale rows flipped (generators, async, match, lambda, `%`/`format`, stdlib shims, packages, setter, descriptors, slice assignment, global/nonlocal, bigint, set dedup) |
| `Bash_Support.md`, `Lambda_Jube_Runtime.md` | Status banners: Bash and Ruby are compiled out of every build; Jube is a developer/packaging note; SHA-256 claim corrected; Node modules mentioned |
| `README.md` | ~20 MB; `|>`/`|:`; every code block compiles (element comma, separators, `^`, `int[]`, `string[]`, REPL transcript as text); `lambda.exe` vs `lambda`; CLI list complete; documentation table complete; Python-only Jube row |

## 2. Defects found (code side — not doc fixes)

Verified against the debug build of 2026-09-28. Each needs a ledger entry or a decision; the docs now describe the behaviour honestly rather than the intent.

**Language / runtime**
1. `set(vec)` and `number(x)` are registry rows with NULL function pointers: any call fails with "import of undefined item fn_set/fn_number" on both tiers. Remove the rows or build them.
2. `array(x)` is not a function (the name is the type); calling it fails at run time.
3. Object-type `that` constraints (field-level and object-level) are parsed but enforced neither at construction nor by `is` (S11.4.6 is starred; a named `T that …` alias *is* enforced).
4. `T^` in parameter/`let` position is rejected by the parser although S7.4.2 says value positions accept it.
5. A binder inside an array element type, `(int as E)[]`, is rejected at the call (S11.4.8v2 "nested site").
6. `v is t` with `t` a `type`-typed parameter evaluates to `false` for a matching value; only literal types, `type` declarations and `let` aliases work on the right of `is`.
7. `type R = -100 to 100` fails to parse (needs `(-100) to 100`); `fn (fn (int) int) int` fails unless the inner parameter is named.
8. `let (a = 1, b = 2, …)`-style multiple bindings in one `let` expression are not accepted (each binding needs its own `let`).
9. `datetime(y, m, d, …)` and `datetime({map})` constructors, `date(y)`, `date(y, m)`, `time(h, m)`, `time(h, m, s, ms)` return `error`; `datetime(n)` takes milliseconds.
10. `dt.format('human')` is not a named format (it is read as a pattern: "10uman").
11. `sys.time.zone`, `sys.time.offset`, `sys.lambda.build`, `sys.lambda.features`, `sys.locale`, `sys.cpu.model`, `sys.process`, `sys.proc.uptime` resolve to nothing; `sys.time.now` is Unix seconds, not a datetime.
12. `shl(1, 54)` and int overflow saturate to `inf` per S4.1.3 — the old doc claimed a float; confirm the ruling is what is wanted for shifts.
13. `open t = temp.'x' { … }` inside `pn main()` under `lambda run` once produced "interp: scratch overflow depth=6 cap=6"; the same block at script top level works. Not reproduced deterministically.
14. Appendix A of the semantics spec is stale in the other direction: S7.2.2–S7.2.4 (`last`), S6.2.1 (string `sort`), S10.2.2 (`eq ne …`), S16.8.9 (computed keys), S5.1.4v2 (identity, `===`, `&`) and S13.1.3v2 are recorded as unimplemented but work today.

**Formats and tools**
15. INI/properties parse `0` as `false` (`zero=0` → `zero: false`).
16. CSS input is an opaque `CssStylesheet*`; `convert -t mark` prints a garbage float. RTF yields raw group maps with a `raw_pointer` leak.
17. VCF: several cards in one file are flattened into one map with duplicate keys; repeated properties are duplicate keys.
18. `.eml` and `.ini` are not auto-detected by `convert` (fall through to text).
19. `lambda validate` help advertises `[files...]` but multiple files are rejected; `.ls` help says `doc_schema.ls` while the code does AST validation; `convert` help lists input-only formats as outputs; `layout` help advertises a non-existent `--flavor`; `js` help claims built-in tests; the CLI banner says "v1.0" while `sys.lambda.version` is "0.1.0" (`main-repl.cpp:145`, `sysinfo.cpp:826` TODO).
20. `math-typst` and `math-mathml` are accepted `convert` targets but stubs; the ASCII math flavor does not recognize AsciiMath's multi-character tokens.
21. `doc_schema.ls` defines `Document`, `Para`, `Header`, `CodeBlock`, `Link`, `Image`, `Code`, `Strong`, `Emphasis` twice (element forms, then map forms); the validator takes the last `Document`, so the element vocabulary is shadowed.
22. Release binaries are 19–20 MB on macOS arm64 (`release/lambda`, stripped: no change); README, the pipeline article and CLAUDE.md ("~8MB") said 8–9 MB. The docs now say ~20 MB; if a smaller distribution build is intended, the release script needs to produce it.
23. Python module: `test_py_import`, `test_py_packages`, `test_py_pkg_simple` segfault; `test_py_advanced_oop` wrong output.
24. Bash and Ruby front ends are compiled out (`LAMBDA_BASH`/`LAMBDA_RUBY` undefined everywhere); 0/37 and 0/22 tests run.

## 3. Structure review

### 3.1 Inventory after this pass

`doc/` holds 27 Markdown files in five roles that the file names do not distinguish: 8 language-reference docs (`Lambda_Syntax`, `_Data`, `_Type`, `_Expr_Stam`, `_Func`, `_Procedural`, `_Error_Handling`, `_Cheatsheet`), 3 library/tool docs (`_Sys_Func`, `_CLI`, `_Validator_Guide`), 6 document/rendering docs (`_Doc_Pipeline`, `Markup_Formats_Support`, `Doc_Schema`, `Math_Support`, `Reactive_UI`, `JS_DOM_Support`), 3 hosted-language docs (`Python_Support`, `Bash_Support`, `Lambda_Jube_Runtime`), 2 normative specs + `Doc_Convention`, and 1 design essay (`The_Unbundled_Monad`). Beside them sit assets (`demo*.png`, `type_hierarchy.*`, `lambda-radiant-pipeline.svg`), the cheatsheet PDF and its LaTeX tooling (`_template/`), and `dev/`.

`Lambda_Reference.md` is now the index: it links every one of these with a one-line description, grouped by role.

### 3.2 Moves (proposed, not done — each is a rename the user should own)

| Item | Proposal | Why |
|---|---|---|
| `doc/The_Unbundled_Monad.md` | → `vibe/Lambda_Design_Effect_System.md` (or `doc/dev/`) | A design-philosophy note that says of itself "descriptive, not normative"; Doc_Convention §8 reserves `doc/*.md` for user documentation, §4 puts reasoning in `vibe/`. Zero inbound links. |
| `doc/Lambda_Jube_Runtime.md` | → `doc/dev/Jube_Modules.md` | Build targets, ABI validation, ownership boundaries: developer content (now bannered as such) |
| `doc/Bash_Support.md` | → `doc/dev/` until the front end ships, or keep with its banner | Documents code that is not in any build |
| `doc/dev/Radiant_Design.md`, `Lambda_and_JS_Runtime.md` | Fold into `RAD_00`/`LR_00` or mark as summaries | They are 90–110 line summaries parallel to the numbered sets' overviews |
| `doc/dev/Bash_Runtime.md`, `Python_Runtime.md`, `Node_Runtime.md` | Refresh stamps or move `Bash_Runtime.md` (a "Design Proposal", verified 2026-03) to `vibe/` | Their content contradicts the source in the ways the hosted-language audit listed |
| `JS262_Test_Guide.md` (repo root) | Delete; `test/js262/JS262_Test_Guide.md` is the maintained copy (the root one says 35,047 passing, the baseline is 40,261) | A stale duplicate at the top level |
| `doc/demo*.png`, `type_hierarchy.*`, `lambda-radiant-pipeline.svg` | → `doc/img/` | Assets mixed with documents |
| `doc/_template/` + `Lambda_Cheatsheet.pdf` | → `utils/cheatsheet/`; regenerate the PDF (dated 2026-08-24, the Markdown changed 2026-09-28) or stop committing it | Tooling and a stale build artifact inside the doc set |
| `site/` | Generate from `doc/` (ideally with `lambda convert`) instead of hand-written HTML | The site's quick start and tour still use the pre-`\|>` pipe and `that`-as-filter syntax; it is a fork of the docs that has already diverged (Doc_Convention §9) |

### 3.3 Content that should move between documents

- **Modules and imports** live in `Lambda_Reference.md` §Modules, which otherwise is an overview/index. Give them `Lambda_Modules.md` (relative/absolute imports, `pub`, namespaces from `Lambda_Syntax`, built-in modules `math`/`io`, guest modules, `lambda.sys.*`).
- **Concurrency** is spread over `Lambda_Procedural.md` §Concurrency, `Lambda_Sys_Func.md` §Concurrency Functions and the Cheatsheet. One `Lambda_Concurrency.md` (tasks, mailboxes, `select`, cancellation, JS Promise interop, S13) with the others pointing at it.
- **Documents as values** — paths, references, `#`, `&`/`===`, `temp`, `put`/`del`/`commit`/`open` — occupy the last third of `Lambda_Data.md` and a table in the Cheatsheet. They are a distinct topic (S2.4, S9, S10.4–5): `Lambda_Documents.md`.
- **String patterns** are 350 lines inside the 1,440-line `Lambda_Type.md`; `Lambda_String_Pattern.md` would let `Lambda_Type.md` stay a type-system doc. Likewise the query operators (`?`, `.?`, `[T]`) could leave `Lambda_Expr_Stam.md` for `Lambda_Query.md` next to the pipe/filter material.
- `Lambda_Func.md` still carries a "System Functions Reference" stub table that duplicates `Lambda_Sys_Func.md`; drop it.

### 3.4 Documents to add

| Document | Why it is missing today |
|---|---|
| **Getting Started / tutorial series** (§4) | There is no path from "installed" to "productive" in `doc/`; the site's quick start is stale |
| `Lambda_Packages.md` | The pipeline article names eight Lambda packages (`chart`, `graph`, `latex`, `math`, `pdf`, `openapi`, `edit`, `dom`) and only `math` has a user doc; nothing says how to import a package or what `lambda.doc.*` paths exist |
| `Radiant_Support.md` (HTML/CSS support matrix) | Users of `lambda view`/`render` have no statement of which CSS features, HTML elements, fonts and image formats are supported — the equivalent of `Markup_Formats_Support.md` for the engine. The material exists in `doc/dev/radiant/` |
| `Lambda_Known_Limitations.md` (or a section in release notes) | Alpha users need one list of what is ruled but unbuilt: streams/laziness (S8.3.2, S14.3), verbs and DataFrame (S14.2), resources (S12.4), thread/process tasks, `compile()`/`quote` (S15.3), `T^` in value position, object constraints, `as` casts, the items in §2 above. Today this is only discoverable in Appendix A of the semantics spec |
| `Lambda_Concurrency.md`, `Lambda_Modules.md`, `Lambda_Documents.md` | See §3.3 |
| Release notes | A `CHANGELOG.md` or `doc/Release_Notes.md` for the alpha |

### 3.5 Naming

Language docs are `Lambda_<Topic>.md`; area docs are `<Area>_Support.md`; `Doc_Schema.md` and `Reactive_UI.md` follow neither. Suggest `Lambda_Doc_Schema.md` and `Lambda_Reactive_UI.md` when the moves above are done, so a directory listing groups the user docs.

### 3.6 Conventions worth adding to `Doc_Convention.md` (proposed)

1. **Implementation-status callouts in user docs.** This pass used a uniform `> **Not yet implemented.** …` blockquote wherever a user doc describes a ruled-but-unbuilt feature, mirroring the spec's `*` mark, and `no-run` with a `// no-run: not yet implemented (S#)` reason on the block. §8 should name this so it stays consistent, and the alpha's known-limitations doc can be generated from these callouts.
2. **Annotate the Cheatsheet tables** with `<!-- code-fence: lambda expr -->` (and `type`/plain) so the gate covers them; the tables would need re-sorting by kind (statements vs expressions vs types). Today the 239 rows are unchecked by the gate.
3. **Result assertions.** The gate checks compilation only; the datetime, `sys.*`, `array()` and constraint claims were wrong in result, not syntax. A `// => value` convention checked by the gate (run the block, compare the printed result) would have caught them.

### 3.7 Verification left to do

- `doc/Lambda_Formal_Semantics.md` links `../vibe/Lambda_Semantics_Features.md` (header "Basis", and the effect doctrine R1–R5), and `doc/The_Unbundled_Monad.md` cites it too, but no such file exists in `vibe/`; the record it points at needs to be located or the link retargeted (editorial, PATCH).
- `doc/dev` stamps: 40 of 63 docs still carry the "initial stamp from git history" marker; the hosted-language and Node docs are the most out of date.
- README standards table (HTML5 100 %, CSS 2.1 98.2 %, CommonMark 100 %, YAML 100 %) and the Lambda-vs-Node benchmark table (`Overall_Result4.md`) were not re-verified; the latest benchmark report is `Overall_Result49.md`.

## 4. Tutorial series — recommendation

**Yes, add one, in `doc/tutorial/`, as gate-checked Markdown, and generate the site from it.** The reference set is complete but is organized by language feature; nothing walks a newcomer through the pipeline the README sells. The existing `site/docs/quickstart` is the right outline but is hand-written HTML with obsolete syntax.

Proposed chapters (each 150–300 lines, every `lambda` block gated, each ending with a runnable script under `test/lambda/tutorial/` so the examples become regression tests):

1. **Install and run** — dev build vs release bundle, `lambda.exe`, REPL, `lambda script.ls` vs `lambda run`, `LAMBDA_HOME`, where `log.txt` goes.
2. **Values and collections** — literals, arrays/maps/elements, the `;` line-continuation rule (the single most common newcomer error in this pass), `let`.
3. **Transforming data** — pipes `|>`, filter `|:`, `that`, `for` with `where`/`order by`/`group by`/joins, vector arithmetic and masks.
4. **Documents as data** — `input()` any format, `?`/`[T]` queries, building elements, `format()`; convert a Markdown file to HTML with a generated table of contents.
5. **Types and schemas** — annotations, unions, element types, constrained types and string patterns, `lambda validate` against your own schema.
6. **Functions and errors** — `fn`/`pn`, closures, `T^E`, `^`, `^ { }`, why the compiler refuses an unhandled error.
7. **Procedures and I/O** — `var`, value semantics (the store-and-handles pattern), `output`, the `io` module, `main()`, tasks with `start`/`wait`.
8. **Rendering and viewing** — `layout`, `render` to SVG/PDF/PNG, `view`, a `.ls` file that *is* a page.
9. **Reactive UI** — the todo list from `test/lambda/ui/todo.ls`, step by step.
10. **Editing and packages** — `lambda edit`, the `math`/`graph`/`chart` packages, JavaScript on the page.

Chapters 1–7 can be written from the updated reference docs alone; 8–10 need the `Lambda_Packages.md` and Radiant support material of §3.4 first.
