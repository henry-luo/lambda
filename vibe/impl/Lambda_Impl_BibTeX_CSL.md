# Lambda-script BibTeX / CSL implementation

**Date:** 2026-10-10. **Status:** Initial opt-in implementation delivered; full CSL conformance and the later gates below remain open.

**Design:** [BibTeX and CSL citation processing in `lambda.latex`](../latex/Lambda_Design_BibTeX_CSL.md). The user selected Lambda script as the implementation language and `lambda.latex` as the package, then authorized implementation. No formal ruling changes are required: source modules follow **D7.2.1–D7.2.4**, resource capture follows **D7.1.2v2 / D7.5.2**, inert style/reference data follows **S1.8**, pure batch processing follows **S12.1.1v2 / S12.4.1v2**, and errors/diagnostics follow **S7.4.1–S7.4.4**.

## Delivered behavior

`latex.render_result(ast, {base_uri: ".", citeproc: {style: "apa", locale: "en-US"}})` selects a CSL interpreter written entirely in Lambda script. Bibliography references come from the existing local BibTeX/BibLaTeX parser or CSL JSON. Citation requests are collected in source order, processed as batches per reference section, and returned as rich Lambda elements to the existing HTML/Radiant path. Existing rendering without `citeproc` retains its default profile.

The processor reads and evaluates actual independent or supplied dependent CSL styles. The bundled style set is IEEE, APA, Chicago author–date, and Chicago full-note bibliography, with English/German/French locales. The implementation does not select handwritten per-style formatting functions or require an external citeproc process. Custom styles have the same evaluator and validation boundary as the bundled styles.

Resource acquisition is isolated in `resources.ls`. Style/locale strings and references are immutable inputs to the engine. Local override and parent files are assets; unavailable resources remain registered with `available: false`, a diagnostic, and visible unsupported output. No network dependency is introduced into document rendering. The bundled resources are unmodified, pinned and attributed in [VENDOR.md](../../lmd/package/latex/citeproc/VENDOR.md) and [resources.json](../../lmd/package/latex/citeproc/resources.json).

## Code map

All new processor modules live under [`lmd/package/latex/citeproc/`](../../lmd/package/latex/citeproc/README.md).

| Module | Responsibility |
|---|---|
| `common.ls` | XML/value helpers, diagnostic records and integer native-error codes |
| `data.ls`, `markup.ls` | Canonical references, extended entry mappings, structured names/dates, inert BibTeX and CSL field markup |
| `style.ls`, `locale.ls` | Namespace/version checks, macro graph/cycles/depth, supplied dependent parents, locale units and fallback |
| `eval.ls`, `output.ls` | CSL tree evaluation, variable accounting, substitution, conditions, rich formatting and punctuation |
| `names.ls`, `dates.ls`, `numbers.ls`, `ranges.ls` | Name options/initialization, localized dates/ranges, ordinals/Roman numerals and page compression |
| `sort.ls` | Precomputed sort keys and stable merge ordering |
| `citeproc.ls` | Pure `compile`/`process` API, disambiguation, numbering, citation collapse, history and bibliography results |
| `resources.ls` | Local bibliography/style/locale/parent capture and asset records |
| `adapter.ls` | LaTeX command modes, scopes, targets, footnote indices, print filters, bibliography CSS and metadata |

Shared code changes are narrow:

- `packages/bib_data.ls` exposes existing parsing, inheritance and resource helpers; the legacy loader still uses its existing entry-type set.
- `packages/bib_model.ls` exposes collection, selection and print planning independently of style-dependent legacy labels.
- `latex.ls`, `analyze.ls`, `render.ls`, and `packages/biblatex.ls` select the opt-in path, plan note-style citations, and project its results.

The [package README](../../lmd/package/latex/citeproc/README.md) documents resource options, the pure API, supported request modes and limits. [Lambda_Packages.md](../../doc/Lambda_Packages.md#7-latex--latex-to-html) describes public selection. No native runtime, renderer, generated parser, vendor code, or build configuration was changed.

## Phase accounting

These statuses refer to the proposal's complete acceptance gates, not merely the existence of the corresponding files.

| Proposal phase | Implemented in this change | Remaining gate |
|---|---|---|
| P0: inventory / oracle | Resource manifests; original fixtures; pinned citeproc-js wrapper; exploratory official-suite adapter | Complete feature inventory across every bundled style branch; second independent processor comparison |
| P1: canonical data | Shared BibTeX strings/inheritance/name parser; extended types, dates, rich fields; CSL JSON validation and duplicate detection | Complete BibLaTeX/Biber mapping, unusual names/dates and full converter parity |
| P2: evaluator | Macros, terms/locales, names, dates, labels, numbers, groups, substitution, conditions, typography and punctuation | Full schema/semantic validation, all CSL options and a general evaluation-work budget |
| P3: batch rules | Stable sorting/numbering, name and given-name expansion, year suffixes, numeric/year collapse | Locale collation, remaining disambiguation rules and year-suffix collapse, exhaustive order-change coverage |
| P4: notes | First/subsequent/ibid/locator history, near-note, first-reference note number; shared footnote plan | All multi-item/interrupted-note cases and incremental edit behavior |
| P5: integration | Opt-in render API; commands, sections/segments, print selection, destinations, assets, CSS, processor metadata | Viewer invalidation interaction tests, richer hash/coverage metadata and all auxiliary-command combinations |
| P6: acceptance | Package goldens, differential acceptance, exploratory slice, rendered preview and broad baseline checks | Full suite, larger bibliographies/performance, more entry types per style and cross-platform/rendered-export gates |

The initial useful milestone is available. P0–P6 are not all marked complete, and the separate Phase IV M4 TeX-style execution proposal is unchanged.

## Validation evidence

Checks were run in the main macOS checkout. Initial source HEAD was `30bb984ff`. Logs and generated artifacts are under `./temp/citeproc/`; they are local evidence, not redistributed reference implementations. Runtime counts below belong to their stated invocations.

| Check | Result | Evidence |
|---|---|---|
| Legacy BibLaTeX/natbib tests before shared refactor | 5/5 pass | `before-package.log` |
| LaTeX-related discovered tests | 58/58 pass, including six citeproc scripts | `latex-suite-final.log` |
| Final citeproc / legacy bibliography integration replay | 11/11 pass after narrowing opt-in dispatch | `integration-final.log` |
| citeproc-js acceptance comparison | 30/30 pass for citation and bibliography text | `oracle-acceptance.log`, `acceptance-report.json` |
| Official suite exploratory slice | 195/201 match historical expectations; 6 differ | `oracle-upstream.log`, `upstream-report.json` |
| Standalone IEEE page through Radiant | Rendered and visually inspected; aligned entries, links, scoped destinations and mixed footnotes visible | `preview.html`, `preview.png`, `preview.json` |
| `make test-lambda-baseline` | 6612/6614 pass; corpus timeout and editor mismatch | `lambda-baseline.log` |
| Individual baseline replay | Corpus passes; editor mismatch persists | `baseline-replay.log` |
| `make test262-baseline` | 40261/40261 fully pass, zero regressions, zero retry time | `test262-baseline.log` |
| Whitespace and resource integrity | `git diff --check` clean; all bundled SHA-256 hashes checked by oracle | Differential tool/report |

The Lambda baseline ran before the final resource-script addition and the last focused fixes. The final LaTeX run covers those changes. The corpus timed out at 60 seconds under the broad baseline's load and passed alone in about 42 seconds. The remaining editor mismatch is `<button disabled>` versus `<button disabled="">`; its editor/HTML-formatting path is outside the modified sources. Replaying it with `LAMBDA_HOME` pointing at an archived `HEAD` package tree reproduces the same mismatch (`editor-at-head.diff`). The test and its expected output were not changed. Thus the aggregate Lambda baseline is **not green**, even though the affected package checks pass.

The independent comparisons use the same reference data and style/locale bytes. The 30 acceptance cases comprise 20 original evaluator cases, six batch/history cases, and one journal example for each of four bundled styles. This establishes those admitted outputs, not every branch of those styles. `citeproc_oracle.py` verifies the reference source hash and resource hashes, compares citations and bibliography entries, writes every difference and exits nonzero on mismatches.

The exploratory adapter admits 201 of the pinned official suite's 845 machine fixtures. Its selection is based on category and input shape, never current pass/fail results. The other 644 IDs are recorded with reasons: 433 outside the exploratory categories, 168 requiring bibliography/sequence/multi-item/abbreviation adapters, 41 requiring incomplete-item normalization, and two requiring another locale. These are untested fixtures, not passes. The tool verifies the complete machine-fixture content hash before reporting the pinned suite commit.

### Remaining exploratory differences

| Fixture ID | Investigation |
|---|---|
| `name_EditorTranslatorSameWithTerm` | Historical fixture expects `tran.`; current pinned locale and current pinned citeproc-js both produce `trans.`, matching Lambda |
| `number_NewOrdinalsWithGenderChange` | Historical expectation normalizes Unicode superscript ordinal characters; current pinned citeproc-js retains them, matching Lambda |
| `name_HebrewAnd` | Custom conjunction with punctuation-space needs special adjacency handling |
| `name_ParsedCommaDelimitedDroppingParticleSortOrderingWithoutAffixes` | Implicit particle/apostrophe parsing in CSL JSON names is incomplete |
| `name_ParsedDroppingParticleWithApostrophe` | Same implicit name-normalization limitation |
| `name_ParsedNonDroppingParticleWithApostrophe` | Same implicit name-normalization limitation |

All six remain differences in the historical-suite report; none is converted into an expected pass or hidden by an allowlist. Current-reference investigations are in `upstream-current-reference.json`. The implicit-name cases depend on citeproc-js's automatic name parsing; explicit CSL particle fields are supported. The two historical/reference disagreements do not justify modifying bundled locale data or replacing literal Unicode text with an obsolete fixture result.

## Reproduction

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*latex_test_latex_citeproc*'
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*latex*'
python3 utils/citeproc_oracle.py --reference temp/citeproc/upstream/citeproc.js
python3 utils/citeproc_oracle.py --upstream-suite temp/citeproc/upstream/test-suite
make test-lambda-baseline
make test262-baseline
```

The last exploratory command intentionally exits nonzero while differences remain. The reference and suite directories are optional external development inputs. The tool can download its pinned citeproc-js reference when `--reference` is omitted; it does not install an engine into the runtime. Reports are written to `temp/citeproc/oracle/report.json`, so preserve a report before running the next comparison.

## Next completion gates

1. Extend the fixture adapter to bibliography and sequence fixtures while retaining exact input/history semantics and explicit untested counts. Add a second independent processor oracle.
2. Finish name normalization/conjunction edge cases, year-suffix collapse and the rejected disambiguation/substitution policies with spec-led fixtures. Keep unsupported values visible until implemented.
3. Select and validate a locale collation policy, then compare multilingual ordering with a pinned independent reference.
4. Add entry-type matrices for all bundled styles and exported PDF/SVG assertions for rich typography, annotations, destinations and note layout.
5. Exercise viewer updates after `.bib`, style, locale and parent edits; add incremental processing only with changed-prior-citation results.
6. Add general work limits and release-build scaling measurements for larger bibliographies before considering a default migration.

These are outstanding compatibility and production-readiness work. No default migration or complete CSL compliance is claimed by the delivered opt-in implementation.
