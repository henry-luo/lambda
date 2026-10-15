# DocBook xslTNG port provenance and support profile

**Baseline:** DocBook xslTNG 2.8.5, commit
`b26a7c42445b5aae1e3e6cda866513427b2b061c`, from the
[official repository](https://codeberg.org/DocBook/xslTNG).
**Status:** Initial `core-html` profile, 2026-10-15; a partial native port.
**Formal linkage:** D7.2.4 places this source package at `lambda.doc.docbook`;
S12.1.1v3 separates conversion from acquisition/export; S1.8 keeps input and
locale templates as data. No formal ruling changes.

`upstream.json` pins the distribution, source archive, reference processor,
and copied assets by SHA-256. `port-map.json` maps upstream module families
and parameters to Lambda code and tests. `LICENSE.xsltng` is the unmodified
upstream MIT notice. The resources under `resources/xsltng/` are copied
without changes. `resources.ls`, `ids.ls`, and `locale.ls` are generated from
that pinned distribution. Lambda translation code is kept outside the
upstream resource directory. `inlines.ls` translates the source module's
finite `t:inline` wrapper forms; specialized templates stay outside that
generated support map.

## Implemented profile

- DocBook 5.x article/book structures, component and section headings,
  deterministic IDs, numbering, English generated labels and TOCs.
- Mixed prose, common inline content, lists, basic formal objects,
  admonitions, ordinary footnotes, links and forward cross-references.
- HTML-style tables and CALS column names, spanspecs, column spans, row
  spans, implicit empty slots, and row-group occupancy validation.
- Text verbatim blocks with line wrappers, sparse padded numbering and the
  upstream element-specific plain presentation;
  the profile selects `verbatim-syntax-highlighter: "none"`.
- Namespace-preserving XML adaptation, significant text whitespace,
  inherited language/space/base URI, source URI and occurrence paths.
- Single-page and chunked publications, default chunk predicates for the
  covered structures, filenames, navigation and rewritten references.
- Local media capture/export and basic image markup. Media geometry is a
  partial semantic profile, separate from the body parity claim below.
- Pure handler customizations with a continuation to the default renderer.

## Deliberate differences and limits

The default HTML head is static and includes the bundled DocBook CSS. It
omits optional upstream browser scripts and their feature UI. Syntax
highlighting defaults to `none`, rather than upstream's `pygments` default;
other highlighters are rejected. Chunk `db-id` metadata uses deterministic
occurrence IDs, rather than Saxon's process-specific node IDs. Generated
HTML IDs follow upstream's abbreviation/ancestor scheme, with an additional
collision check against explicit source IDs.

The original CSS retains its Google Fonts imports. These six external font
stylesheets are listed in `publication.external_stylesheets`; no font bytes
are fetched by conversion or bundled here. A publication's content, CSS,
draft icons and captured local media export together, while those font
imports remain external. Fully offline font packaging remains future work.

The current locale profile is English, including translated data for its
finite list templates. Other languages produce a diagnostic. Full locale
selection and its XPath-dependent title/reference templates, bibliography,
glossary/index generation, reference-page vocabulary, advanced CALS styling,
table-specific footnotes, inclusion/transclusion/profiling, foreign inline
SVG/MathML trees, full media sizing/selection and optional browser behavior
remain incomplete. Unsupported visible elements and selected unsupported
attributes produce diagnostics; strict mode rejects them. This profile does
not claim schema validation or full xslTNG coverage.
The adapter accepts at most 256 nested element levels and a CALS group at
most 1,024 columns. Verbatim numbering interval/minimum parameters accept
positive integers; element-specific styles retain upstream precedence.

`target: "radiant"` currently returns the same HTML/CSS publication as the
browser target. Radiant layout coverage is a separate renderer capability;
this package does not override the upstream stylesheet to hide layout gaps.
`lambda view` recognizes the covered DocBook XML roots directly. Its source
view adapter resolves copied CSS from the installed package and captured
images as data URIs; it does not write an exported publication. The bundled
file browser uses the same XML route and retains the original source tab.

Automatic tiering/MIR executes the package. Forced interpreter activation
currently rejects the runtime's unsupported map-index assignment node in the
procedural media loader. No package-specific tier override or interpreter
workaround is installed (D8.1.1).

## Reproduce and update

```sh
python3 utils/docbook_reference.py --fetch --resources
python3 test/docbook_reference_check.py
python3 test/docbook_reference_check.py --chunked
```

The ordinary comparisons need Python and Lambda, using checked-in expected
bodies; they do not need Java, Saxon, the downloaded distribution, or network
access. Rebuilding the official expected bodies additionally requires a
compatible Java installation:

```sh
JAVA=/path/to/java python3 test/docbook_reference_check.py --refresh
JAVA=/path/to/java python3 test/docbook_reference_check.py --refresh --chunked
```

The reference runner uses the distribution's verified Saxon-HE 12.10 jar,
with persistent TOC disabled and syntax highlighting set to `none`. All
download caches, generated export scripts, Java temporary files and comparison
outputs stay under the repository's `temp/` directory.

For an upstream update, change the pin and hashes, inspect the module/parameter
diff through `port-map.json`, regenerate the data/assets, and review official
reference changes before accepting new goldens. The HTML comparison retains
all body attributes, classes and text. It normalizes attribute order,
namespace-declaration placement and the declared Saxon `db-id` mapping; it
also verifies local publication links, fragments and resource existence.
