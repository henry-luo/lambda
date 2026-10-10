# CSL citations in `lambda.latex`

This experimental CSL 1.0.2 processor is implemented in Lambda script. It interprets style XML and produces rich citation and bibliography elements through the existing LaTeX renderer. JavaScript, Pandoc, BibTeX and Biber are not runtime dependencies.

The package and resource boundaries follow **D7.2.1–D7.2.4**, **D7.1.2v2** and **D7.5.2**. Style and reference markup are inert data (**S1.8**); the processing API receives captured values (**S12.1.1v2**, **S12.4.1v2**). Failures use errors or located diagnostics (**S7.4.1–S7.4.4**).

## Render a LaTeX document

```lambda
import latex: lambda.latex.latex
import html: lambda.latex.to_html

let ast = latex.parse_file("paper.tex")
let result = latex.render_result(ast, {
    base_uri: ".",
    standalone: true,
    citeproc: {style: "apa", locale: "en-US"}
})
html.to_html(result.elements)
```

Use `\addbibresource{references.bib}` or `\addbibresource{references.json}` and the existing citation commands and `\printbibliography`. `filecontents` resources, `\bibliography`, `\nocite`, `refsection`, `refsegment`, repeated prints, headings and the existing bibliography filters share the package's collection and selection code. Citation and bibliography targets remain linked. Note styles use the existing footnote plan for `\footcite` and `\autocite`; a citation inside an ordinary footnote remains inline.

`citeproc` is opt-in. Omit it to use the existing built-in profile. CSL style selection conflicts with explicit BibLaTeX `style`, `citestyle`, `bibstyle`, `sorting`, and `\bibliographystyle`; these receive `conflicting-citation-style`. Resource and print options remain adapter responsibilities. Command-star variants receive diagnostics.

| Option under `citeproc` | Meaning |
|---|---|
| `style` | Bundled shorthand, or local CSL filename relative to `base_uri`; default `"ieee"` |
| `style_xml` | Captured XML string instead of a filename |
| `locale` | Requested language; defaults to document language |
| `locales` | Map of language tags to captured XML strings or local locale filenames |
| `parents` | Map of independent-parent URI identifiers to captured XML strings or local CSL filenames |

Bundled styles are `ieee`, `apa`, `chicago-author-date`, and `chicago-fullnote-bibliography`. Bundled locales are `en-US`, `de-DE`, and `fr-FR`. Custom styles use the same evaluator. Dependent parents must be supplied explicitly; the package does not download them. Local overrides and parent files appear in `result.assets`; `result.metadata.bibliography` records the processor, style ID, locale and CSL version. Resource failures produce diagnostics and visible unsupported output.

## Pure processing API

```lambda
import cp: lambda.latex.citeproc.citeproc
import data: lambda.latex.citeproc.data

// Capture these strings with input() before invoking the processor.
let style_xml = input("styles/example.csl", "text")^
let locale_xml = input("locales/en-US.xml", "text")^
let bibliography_json = input("references.json", "text")^
let references = data.json(bibliography_json)^
let compiled = cp.compile(style_xml, {'en-US': locale_xml}, {locale: "en-US"})^
let result = cp.process(compiled, references.references, [
    {id: "citation-1", note_index: 0, items: [{id: "doe2024", locator: "12", label: "page"}]}
], {nocite: []})^
result.citations
```

`compile` and `process` return `map^`; callers propagate or handle failures with the language's normal error operators. `data.bibtex(text, source)` returns normalized references and parser diagnostics; `data.json(value, source)` accepts captured JSON text or an array and returns `map^`. For diagnostics from an error, use `common.error_issue(error)`; native error codes remain integers.

Each request has a stable `id`, `items`, optional `note_index`, and optional `mode`. Items support `id`, `locator`, `label`, `prefix`, `suffix`, and an optional local link `target`. Modes are `normal`, `textual`, `author-only`, `suppress-author`, `title-only`, `year-only`, and `full`. Pass the complete request sequence: disambiguation can affect earlier citations. `nocite: ["*"]` includes all references without adding citation history.

Results contain `citations`, `bibliography`, resolved `references`, `diagnostics`, `style_id`, and `locale`. Citations and bibliography entries carry both plain `text` and rich `content`. Use `lambda.latex.to_html` to serialize the rich content through the package's escaping boundary.

## Coverage and current limits

The evaluator implements macros, choose conditions, groups with variable suppression, substitution, names and et-al options, localized terms and dates, labels, ordinals, Roman numerals, affixes, quotes, typography, text case, page ranges, stable sorting, numeric/year collapse, name/given-name/year-suffix disambiguation, and note history. BibTeX normalization reuses the existing parser, macros, inheritance and name parser, extends entry-type mappings, and decodes common protected text and accents. CSL JSON markup is sanitized into a small rich-text vocabulary.

This is a bounded implementation, not a claim of complete CSL conformance:

- Sorting uses normalized string keys and Lambda's order, without locale collation tables.
- Year-suffix/ranged year-suffix collapse, several given-name disambiguation rules, and partial subsequent-author substitution are explicitly rejected. No multilingual CSL-M extensions or arbitrary TeX style execution are supported.
- Automatic parsing of implicit particles in CSL JSON names is incomplete; supply explicit `dropping-particle` and `non-dropping-particle` fields. Some particle decoration and conjunction-spacing edge cases remain.
- Large bibliography scaling, incremental updates, full schema validation, and every citation history combination remain follow-up work. XML/macro/parent depth is capped, but there is no general evaluation-work budget.
- The four bundled styles have checked journal examples and package rendering coverage. This does not establish exact output for every entry type and option in those styles.

The [implementation record](../../../../vibe/impl/Lambda_Impl_BibTeX_CSL.md) lists checks, remaining differences and completion gates. The [design proposal](../../../../vibe/latex/Lambda_Design_BibTeX_CSL.md) describes the longer-term scope.

## Validation and resource provenance

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*latex_test_latex_citeproc*'
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*latex*'
python3 utils/citeproc_oracle.py --reference temp/citeproc/upstream/citeproc.js
```

The optional differential tool requires Node and a pinned citeproc-js reference. If `--reference` is omitted, it downloads that development-only reference into `./temp/citeproc/oracle/`. It verifies its hash and all bundled resource hashes, compares every admitted citation and bibliography, and fails on differences. Its JSON report is written to `temp/citeproc/oracle/report.json`. `--upstream-suite PATH` explores a documented input-shape-selected subset of a separately obtained official test-suite checkout, reports every difference, and exits nonzero if any differ.

Unmodified CSL styles/locales retain their upstream authors and rights. See [VENDOR.md](VENDOR.md) and [resources.json](resources.json) for attribution, commits, source URLs and SHA-256 hashes.
