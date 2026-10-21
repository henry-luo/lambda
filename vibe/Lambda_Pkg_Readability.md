# Lambda Readability Package — `lambda.doc.readability`

> **Status:** implemented 2026-10-21 as `lambda.doc.readability`, a port of Mozilla Readability.js 0.6.0 and `Readability-readerable.js`. Conformance: **130/130** of Mozilla's own test pages (content, metadata and `readerable`), plus 8/8 synthetic pages checked against Readability.js output (§7).
> **Supersedes:** `vibe/Readability_Plan.md`, `vibe/Readability_Progress.md` and `vibe/Readability_Tune.md` (2026-02), and the 1,701-line `utils/readability2.ls` they described. Their history is kept in [Appendix A](#appendix-a-history-the-2026-02-port).
> **Spec linkage:** D7.2.4v3/D7.2.6 (the `lambda.doc.*` package path and facade), S16.8.9 (computed keys, and the missing computed tag behind RD2), S11.1.2v4 (string-pattern islands), S17.1.1 (ECMAScript `split`), S17.7.1 (`ignore_case`), S11.4.3/D3.3.2v2 (the implicit `any \ error` return contract, E208), S7.6.7v4 (`pn` handlers are statements; LR10-13).
> **Authority:** package design (RD1–RD8, open items RD-O1–RD-O8). Nothing here revises an S#/D# ruling; RD-O1 asks for one.
> **Reference source:** `ref/readability/source` (git-ignored checkout of `@mozilla/readability` 0.6.0; `Readability.js` 2,812 lines, `Readability-readerable.js` 122 lines, test pages under `test/test-pages/`).

## 1. Purpose and scope

Readability.js is the library behind Firefox Reader View. Given a web page it finds the main article, strips navigation, advertising and other clutter, and reports the article's title, byline, excerpt, site name, language, direction and publication time. `lambda.doc.readability` brings the same extraction to Lambda scripts: data pipelines that read saved or fetched HTML, the document viewer, and the planned web-scrape package ([Lambda_Pkg_WebScrape.md](Lambda_Pkg_WebScrape.md)), which uses it for content extraction.

The goal is behavioral fidelity, measured by Mozilla's own test suite rather than by resemblance to the algorithm's outline. The earlier port matched metadata on 60 of 63 pages but never compared article content (Appendix A); this one compares content node by node, as Mozilla's harness does.

Out of scope: paging (`nextLink`/`prevLink` are dead regexes upstream too), the `debug` logger, and the `serializer` option, which Lambda replaces by returning both an element and its HTML (§2).

## 2. Public surface

```lambda no-run
import rd: lambda.doc.readability

let article = rd.parse(html, {url: "https://example.com/news/story.html"})^   // map or null
article.title        // "Story title"
article.content      // <div id: "readability-page-1", class: "page", ...> element
article.html         // the same content as an HTML string
rd.readerable(html)^ // isProbablyReaderable: a quick check before parsing
```

| Function | Readability.js | Result |
|---|---|---|
| `parse(source, options?)` | `new Readability(doc, options).parse()` | `map?^` — the article, or `null` when the page has no content |
| `readerable(source, options?)` | `isProbablyReaderable(doc, options)` | `bool^` |
| `pn read(path, options?)` | — | `input(path, 'html')` then `parse` |

`source` is an HTML string or a parsed Lambda document (`input(p, 'html')`, `parse(s, 'html')`). A bare element is wrapped as body content.

**Result fields** (snake_case, per Lambda convention):

| Field | Readability.js | Notes |
|---|---|---|
| `title`, `byline`, `dir`, `lang`, `excerpt` | same | `null` where Readability.js gives `null`/`undefined` |
| `site_name`, `published_time` | `siteName`, `publishedTime` | |
| `content` | — | the article as a Lambda element: the `readability-page-1` `<div>` |
| `html` | `content` | `innerHTML` of the article container, as Readability.js serializes it |
| `text`, `length` | `textContent`, `length` | `length` counts UTF-16 code units, as JavaScript does (RD4) |

**Options** (`parse`): `url` (document URL; relative links resolve against it or a `<base href>`, and stay as written without one), `char_threshold` (500), `top_candidates` (`nbTopCandidates`, 5), `max_elems` (`maxElemsToParse`, 0 = unlimited; exceeding it raises), `classes_to_preserve` (`[]`; `page` is always kept), `keep_classes` (`false`), `disable_json_ld` (`false`), `link_density_modifier` (0), `allowed_video` (a string pattern replacing `REGEXPS.videos`, matched case-insensitively). An option that is `null` takes its default; unlike JavaScript's `||`, an explicit `0` is honored. `readerable` takes `min_content_length` (140) and `min_score` (20).

## 3. Design decisions

**RD1 — A pure port over immutable trees.** Readability.js mutates the document in place. The Lambda port never mutates its input: every phase is an `fn` that returns a new tree, so `parse` is usable from functional code and the source document stays intact. Readability's retries, which JavaScript implements by caching and restoring `body.innerHTML`, fall out naturally: each attempt starts from the prepared tree (RD6). The cost is that each pass rebuilds the tree it walks (§6).

**RD2 — Node records instead of Lambda elements.** An element literal's tag must be written in the source (the grammar admits a computed key for attributes, S16.8.9, but not for the tag), and no operation removes an attribute from an element of arbitrary tag: a `pn` copy-on-write assignment of `null` leaves `attr=""`, and `del` applies only to documents (S9.1). Readability renames tags (`div`→`p`, `font`→`span`, `h1`→`h2`, a single table cell → `p`/`div`) and strips attributes from every element, custom elements included, so the port converts the parsed document once into records — a text node is a string, a comment is `{tag: "#comment", data}`, an element is `{tag, attrs: [[name, value]], kids, text}` with `text` caching `textContent` — and serializes the article with the HTML fragment serializing algorithm. `content` is that serialization parsed back. The SVG editor met the same gap and took the same route ([Radiant_Impl_Edit_Mode §3.2](impl/Radiant_Impl_Edit_Mode.md)). A computed tag would remove the round trip (RD-O1). Attribute pairs keep DOM order; a table's data-table verdict travels as a record field rather than a marker attribute, so no internal state can leak into output.

**RD3 — Readability.js semantics, quirks included.** The port reproduces observable behavior, not just intent, because the test pages encode the quirks: `hasContent` is `/\S$/` (a text node "has content" only if its *last* character is not white space); `_clean`'s `tagName === "object"` comparison never matches, so `<object>` inner HTML is never inspected; `jsonLdArticleTypes` anchors only its first (`^Article`) and last (`APIReference$`) alternatives; the negative class regex's `^hid$| hid$| hid |^hid ` alternatives equal a space-delimited `hid`; a non-string JSON-LD `@type` makes `String.match` throw, which skips the whole script; the comma count is `split(commas).length`, one more than the commas; `parseInt(rowspan) || 1` turns `0` and `NaN` into 1; an `<img>` that a `<figure>` gains in `_fixLazyImages` is not revisited; a JSON-LD title is untrimmed only when `name` and `headline` disagree.

**RD4 — JavaScript string semantics.** Lambda's `"\s"` class is ASCII-only and its `trim` strips ASCII white space, while Readability's `\s` and `String.prototype.trim` use ECMAScript white space (NBSP, U+FEFF, U+2000–U+200A, …). `text.ls` defines that class once; `trim_text`, `squash` (`/\s{2,}/g`) and the title/meta/srcset patterns use it. JavaScript lengths are UTF-16 code units, so `js_len` adds one per astral character: thresholds (25, 80, 100, 140, 500 characters) and link densities then agree exactly. `RegExp.test` with `i` maps to `find(…, {ignore_case: true, limit: 1})`; `split(/\s+/)` and `split(/\W+/)` rely on S17.1.1's ECMAScript edge cases; word classes (`\w`, `\W`) are ASCII in both languages. Patterns without anchors express anchors through full matches (`is`) or by testing the match index.

**RD5 — DOM identity without a DOM.** Readability keeps scores on node objects and walks parent pointers. The port recovers both from structure:

- The node-prepping loop (`_grabArticle`'s `while (node)`) visits every surviving element exactly once, in pre-order, and never alters a node it has passed. It is therefore one recursive walk that threads the state the loop keeps across nodes (the byline, `shouldRemoveTitleHeader`, `lang`), and `elementsToScore` is exactly the walked tree's elements with a scored tag, in document order.
- Scores live in a path index of the walked tree (`"0.2.3"` → node, parent path). Ancestors are path prefixes; the candidate list is the paths in first-scored order (`group by` keeps first appearance), each score summed in the same order JavaScript adds it; the top-N insertion loop is a stable descending sort.
- `_removeNodes` runs over a static list from last to first, so a node is judged after its descendants and before its ancestors, whose decisions never depend on siblings: a post-order prune. Static `NodeList`s that a pass extends (`_fixLazyImages`) exclude the new nodes; passes whose decision depends on a node's original subtree (`_cleanMatchedNodes`, `_simplifyNestedElements`, single-cell tables) run in pre-order.
- `_replaceBrs` processes `<br>`s in document order; per sibling list that is a left-to-right scan in which a chain's paragraph absorbs the following phrasing siblings before their own `<br>`s are processed.

**RD6 — Retries are faithful to the cached page.** When an attempt yields fewer than `char_threshold` characters, Readability restores `body.innerHTML` from the HTML cached at the start of `_grabArticle` and drops one flag (strip unlikely candidates, weigh classes, clean conditionally). Three details survive the restore and are ported: the `<head>` and `<html>` keep the previous walk's changes; `_articleByline` persists, so a byline found once is not removed again; and the `<body>` element itself persists with its `readability` score, so once scored it is never re-initialized or pushed as a candidate again (found by the Readability.js oracle on the `unlikely` page). The restore itself is an `innerHTML` round trip: the cached body is serialized and re-parsed, which, for example, merges text nodes that removals had left adjacent.

**RD7 — JSON-LD comes from the parsed document.** Lambda's HTML reader already parses `<script type="application/ld+json">` content into data. The package reads JSON-LD from the parsed Lambda document before conversion; a script whose content stayed text (CDATA-wrapped or malformed) is stripped of CDATA markers and parsed with `parse(…, 'json')`, a failure skipping the script as `JSON.parse` would. Readability's `try`/`catch` is emulated by a `'throw'` sentinel where `String.match` would meet a non-string.

**RD8 — Conformance is the gate.** Fidelity is measured three ways (§7): Mozilla's corpus under Mozilla's harness semantics, synthetic pages whose expectations Readability.js itself produced, and a golden of the public API whose values were checked against Readability.js. A behavior change in the package is judged by these, not by inspection.

## 4. Module map

| Module | Readability.js | Role |
|---|---|---|
| `doc/readability.ls` | `parse`, `isProbablyReaderable` | facade: source handling, `parse`, `readerable`, `read` |
| `readability/article.ls` | `parse()` | the pipeline over one document; settings; result assembly |
| `readability/node.ls` | DOM helpers, `_getInnerText`, `_getClassWeight`, `_isPhrasingContent`, `_hasSingleTagInsideElement`, `_getLinkDensity`, `_isProbablyVisible`, `_textSimilarity` | node records, tree transforms, text helpers, serializer |
| `readability/text.ls` | `\s` | the ECMAScript white-space class |
| `readability/metadata.ls` | `_getArticleTitle`, `_getArticleMetadata`, `_getJSONLD`, `_unescapeHtmlEntities` | title, `<meta>` values, JSON-LD |
| `readability/prep.ls` | `_unwrapNoscriptImages`, `_removeScripts`, `_prepDocument`, `_replaceBrs` | document preparation |
| `readability/grab.ls` | `_grabArticle` | node prepping walk, scoring, top candidate, siblings, retries |
| `readability/clean.ls` | `_prepArticle`, `_postProcessContent` and their helpers | article cleanup, relative URLs, nested-element simplification, classes |
| `readability/readerable.ls` | `isProbablyReaderable` | the quick check |

The package is 1,022 lines of Lambda, against 2,934 for the two JavaScript files.

## 5. Pipeline

1. **Convert** the parsed document to records (`node.of`): doctype and processing nodes drop out; attribute values are strings in source order.
2. **Unwrap noscript images**: placeholder `<img>`s without an image source go; a single-image `<noscript>` replaces the single-image element before it, adopting that image's sources it lacks (`data-old-*` when both have one).
3. **JSON-LD** from the Lambda document (RD7); its title check uses the unwrapped document's title.
4. **Remove scripts** (`script`, `noscript`), then **prepare**: `<style>` goes, `<br>` chains in the body become paragraphs (a `<p>` parent becomes `<div>`), `<font>` becomes `<span>`.
5. **Metadata** from `<meta>` (property names normalized as Readability does, later values winning) with JSON-LD first and the document title last; values are entity-unescaped.
6. **Grab** (RD5, RD6): walk (visibility, `aria-modal` dialogs, byline, title-duplicating header, unlikely candidates and roles, empty blocks, `<div>`→`<p>` with phrasing runs wrapped in paragraphs), score, choose the top candidate (alternative-candidate ancestors, score climbing, single-child climbing; or a created container when there is no candidate or the body wins), join siblings, prepare the article (`_prepArticle`'s passes, in Readability's order), wrap it in the `readability-page-1` page, retry or accept.
7. **Post-process**: absolute links and media sources (`javascript:` links become their text or a `<span>`; `srcset` candidates resolve one by one), simplify nested `<div>`/`<section>`s, clean classes.
8. **Assemble** the result: the excerpt falls back to the article's first paragraph; `html` is serialized, `content` parsed from it.

## 6. Performance

Release build, 2026-10-21, `parse` alone: mozilla-1 (92 KB) 0.14 s, mercurial 0.23 s, lwn-1 0.21 s, dropbox-blog 0.17 s; the slowest page is wikipedia-2 (1 MB, 2.3 s). The whole corpus, including `readerable`, two re-parses and the node-by-node comparison per page, runs in 22 s. The 2026-02 port took 40.8 s, 28.4 s and 24.7 s on the first three and 371 s for 63 pages before caching (203 s after).

Two choices account for most of the difference. Each record caches its `textContent`, computed once from its children's caches when the record is built, so the inner-text, link-density and text-density queries that made the old port quadratic cost one normalization of an existing string. And the pipeline follows Readability's phases instead of re-deriving the article from containers, so no query scans a subtree Readability would not scan. Each pass still rebuilds the tree it visits (RD1); structural sharing of untouched subtrees is the next lever (RD-O7).

## 7. Verification

**Mozilla's corpus** — `test/lambda/readability/_corpus.ls` runs every page of a test-pages directory (default `ref/readability/source/test/test-pages`) the way `test/test-readability.js` does: document URL `http://fakehost/test/page.html`, `classes_to_preserve: ["caption"]`, comment nodes removed before parsing (the jsdom run strips them, leaving neighboring text nodes separate), an in-order comparison that skips blank text nodes and compares tag, `id`, class, attributes and collapsed text, and the metadata fields, with `dir`, `lang` and `publishedTime` only when expected. `expected.html` is pretty-printed, which the JavaScript harness neutralizes by printing both sides; the runner trims text instead. It also checks `readerable` against `expected-metadata.json`.

```bash
./lambda.exe run test/lambda/readability/_corpus.ls
```

`READABILITY_PAGES=<dir>` and `READABILITY_PAGE=<name>` select another corpus or one page. Result on 2026-10-21: **130/130** pages pass every check. The corpus lives in the git-ignored `ref/`, so the runner is a manual gate (`_` prefix); moving the pages into `../lambda-test` would make it automatic (RD-O8).

**Synthetic pages** — `test/lambda/readability/pages/<name>/` holds eight small pages (bylines and title separators, `<br>` chains, JSON-LD, lazy images and media, tables, short pages with retries and RTL, unlikely candidates and roles, nested `<div>`s and sibling joining). Their `expected.html` and `expected-metadata.json` were produced by Readability.js 0.6.0 under the same harness settings, with `node utils/generate_readability_expected.js` (jsdom from the `ref/readability/source` checkout); `pages.ls` checks all eight through the shared comparator (`mod_compare.ls`). A new page needs only its `source.html` before the script runs.

**API** — `api.ls` covers the result fields, `content`/`html`, URL resolution with and without `url`, string and parsed sources, every option, `readerable` thresholds (Mozilla's own unit cases) and the no-article case. Every value was checked against Readability.js on the same input.

Both goldens give identical output on the T0 interpreter and the MIR JIT (`LAMBDA_EXEC_BACKEND=interp|jit`), and the corpus passes 130/130 on each tier (release build: 22 s JIT, 49 s T0).

**Demo** — `./lambda.exe demo readability` ([test/demo/readability](../test/demo/readability/README.md)) shows the package in a small browser window: three cluttered article pages and a front page, with an address-bar icon that toggles each tab between its original page and the extracted article; the icon is disabled where `readerable` fails. For its four pages the article HTML is byte-for-byte identical to Readability.js 0.6.0 under jsdom (checked 2026-10-21). Its model golden (`tests_readability_model`) and native replay (`tests/reader.json`) are listed in the demo README.

## 8. Runtime fixes made for the port (2026-10-21)

The conformance runs found four Lambda runtime defects; each was fixed at its root.

| Defect | Fix | Regression |
|---|---|---|
| A malformed or CDATA-wrapped JSON-LD script failed the whole HTML parse (E407): the embedded JSON parse shared the document's `Input` and set `parse_failed` | the reader parses JSON-LD with the strict parser and restores the document's failure state; script text stays text when it is not JSON (`html5_tree_builder.cpp`) | `pages/json-ld`, corpus |
| `!` in a pattern matched letters under `ignore_case`: the complement was emitted as positive intervals containing U+017F and U+212A, which RE2 folds to `s` and `k` (S17.7.1) | a set reaching the top of its alphabet is emitted as `[^…]` (`re2_wrapper.cpp`) | `test/lambda/string_ignore_case.ls` §4 |
| `url_resolve` diverged from the WHATWG URL parser: schemes and special-scheme hosts kept their case, a `file:` drive letter kept `C|`, opaque paths (`data:`, `mailto:`) percent-encoded spaces and rewrote backslashes | lowercased scheme and special host, `C|`→`C:`, the C0-control percent-encode set for opaque paths (`lib/url_parser.c`) | `test/lambda/url_resolve.ls` |
| URLs longer than fixed buffers were silently truncated (href 4,096 bytes, fragment 4,096, relative path 1,024, trimmed input and authority-relative input 2,048): long `data:` images came back cut | buffers sized from their inputs; `UrlSafetyRegression.HrefReconstructionKeepsVeryLongPath` now asserts the full href instead of the old clamp | `test_url_extra_gtest`, `url_resolve.ls` §5 |

## 9. Open items

- **RD-O1 — computed element tags (needs a ruling).** A computed tag, e.g. `<[expr] *:attrs, *kids>` mirroring S16.8.9's computed attribute keys, would let the package build `content` directly instead of serializing and re-parsing (RD2), and would serve every package that rebuilds arbitrary markup (the SVG editor, `chart/svg.ls`'s tag dispatch). Removing an attribute from an element of arbitrary tag has no spelling either. Both are language questions for the user.
- **RD-O2 — patterns named across modules ([LR07-48](Lambda_Issue_Ledger.md#lr07-48)).** A pattern type exported by one module and used as a value in another (`"x" is ws`, `find(s, other.pat)`) resolves its compiled index against the importer's type list. The package composes the shared `ws` class only inside its own `\(…)` islands, which compile locally.
- **RD-O3 — value handlers over `pn` calls** ([LR10-13](Lambda_Issue_Ledger.md#lr10-13)) bind `null`; the corpus runner's per-page check is therefore an `fn`.
- **RD-O4 — HTML reader deviations seen in passing.** A stray `<td>` outside a table is kept (HTML5 ignores it), and `<iframe>` content parses as elements rather than raw text. Neither affects the corpus.
- **RD-O5 — `img.src` and `srcset`.** Readability reads the IDL properties; jsdom resolves `src` against the base, the in-house JSDOMParser does not. The port reads the attribute, as JSDOMParser does; the corpus passes either way.
- **RD-O6 — comments.** Readability keeps comment nodes in the article; Mozilla's harness removes them from the input first. The package keeps them (it ports the library, not the harness); the runner removes them.
- **RD-O7 — structural sharing.** Passes rebuild whole trees; returning untouched subtrees unchanged would cut the cost of the twenty-odd cleanup passes, which dominate on large pages such as wikipedia-2.
- **RD-O8 — corpus home.** Moving the 28 MB test-pages corpus into `../lambda-test` (linked by `setup-test.sh`) would let the corpus run as an automatic gate.

## Appendix A. History: the 2026-02 port

The first plan (February 2026) judged a port highly feasible, with HTML parsing, element traversal, string functions and pattern matching already available, and asked for `lower`/`upper` and `url_resolve`, which have since been added. It proposed a pure functional design — no input mutation, a new output tree, `fn` for extraction and `pn` only for I/O — which RD1 keeps.

The resulting `utils/readability2.ls` (1,701 lines) re-derived the article rather than following Readability's phases: it scored `<div>`/`<section>`/`<article>`/`<main>` containers directly, took a single `<article>` as a fast path, did not port `_replaceBrs`, `_prepDocument` or URL resolution, and parsed JSON-LD from strings by hand. Against the test pages with `expected-metadata.json` (63 of 130 then usable; files over 100 KB were skipped) it matched all metadata on 60; the 67 HTML-only comparisons were never run. Its three failures were an entity-decoding gap in the HTML reader (since fixed), the missing `<br>` replacement, and a wrong candidate on mozilla-1.

The port surfaced Lambda issues of its own, all since resolved: `for … order by` lost compound items (fixed with `fn_sort_by_keys`); an empty HTML attribute did not equal `""` (then fixed by mapping it to `null`; under today's semantics `""` is a string value and empty attributes read as `""`); the remaining reports were usage errors (`range` needs three arguments; a symbol is not a string).

Profiling (the former Readability_Tune.md) found three functions taking 85% of the time: container scoring, metadata extraction and a two-pass byline search, all dominated by recursive inner-text extraction repeated over nested containers. A VMap meta index, a container-text cache and a combined tag walk cut the suite from 371 s to 203 s; caching text for every element was slower, because a pure bottom-up pass copied descendant pairs at every level. The present design caches text in the records themselves (§6) and removes the reason for the old trade-off.

## Appendix B. Implementation notes

- **Error containment (E208).** An unannotated `fn` must not return an error, and a call in return position whose arguments may be errors (field reads, indexing, results typed `any`) is rejected when the callee's result is provably clean. The node helpers therefore type their text interfaces (`trim_text(s: string) string`, `content_text(n) string`, `attr(n, key) string?`), take raw attribute values as `any` where they test them (`test`, `image_ext`, `set_attr`), and recover `replace` with `^ { s }`.
- **Escapes.** Invisible characters (NBSP, U+FEFF, the space and comma variants) are written as `\uXXXX` escapes in source; a tool round trip once turned them into literal characters.
- **Shadowing.** The facade's `parse` shadows the system function, so modules call `lambda.sys.parse` explicitly.
