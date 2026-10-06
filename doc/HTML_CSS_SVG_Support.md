# HTML, CSS and SVG Support

Radiant is Lambda's HTML/CSS/SVG layout, rendering and interaction engine. It turns a document into laid-out boxes and paints them, and it is the engine behind four commands:

| Command | What Radiant does |
|---|---|
| `lambda layout page.html` | Resolves CSS, lays the page out and writes the computed view tree (every box with its position, size and resolved style) as JSON. |
| `lambda render page.html -o out.svg` | Lays the page out and paints it to SVG, PDF, PNG or JPEG, chosen by the output extension. |
| `lambda view page.html` | Opens the page in an interactive window. Page scripts run on LambdaJS; links, forms, scrolling, selection and animations are live. |
| `lambda edit doc.md` | The viewer as an editor: rich text for Markdown and HTML, a drawing editor for SVG. |

HTML is the native input. LaTeX, Markdown, XML, Lambda script results and diagram sources reach Radiant as an HTML DOM built by their converters, so everything below applies to them too; see [Lambda_CLI.md](Lambda_CLI.md) for the per-command input formats.

**One tree.** Radiant does not build a separate render tree. The parsed document is the Lambda/Mark element tree: CSS is resolved onto its elements, layout writes geometry onto the same nodes, and page scripts reach the same nodes through the DOM. This is why `lambda layout` can report every element's box, and why a script mutation is a relayout of the tree it already has. See [Lambda_Doc_Pipeline.md §1.5](Lambda_Doc_Pipeline.md#15-one-representation-from-parser-to-pixel) and [§3.4](Lambda_Doc_Pipeline.md#34-lay-out-and-render); the engine design is in [dev/radiant/RAD_00_Overview.md](dev/radiant/RAD_00_Overview.md).

> **Status (alpha, focused updates through 2026-10-06).** The initial matrix was compiled from the source (`lambda/input/css/`, `radiant/`, `lib/font/`), the conformance suites, and spot checks run with `lambda layout` and `lambda render` on 2026-09-28. Legend: ✅ supported · ◐ partial, with what is missing stated · ❌ not supported. *Parsed only* means the CSS parser accepts the syntax but nothing downstream applies it. Anything that could not be confirmed is marked *not verified*.

**Implementation update (2026-10-03).** Selector, cascade, nesting, media, `@supports`, custom-property, marker, caret, text-alignment, text-emphasis, logical-border, grid-track, scrolling and image-rendering rows below include focused tests added during the CSS support work. The aggregate conformance and property counts remain the 2026-09-28 baseline until a full re-audit.

**Custom-property follow-up (2026-10-06).** Focused parser, native substitution, SVG and raster tests verify comma-containing function arguments, fallback comma lists, empty fallbacks, empty custom-property declarations, keyword defaulting, active-cycle invalidation, origin/layer rollback and declaration-owner lookup in SVG. The custom-property row remains partial; aggregate counts are unchanged. [Implementation record](../vibe/impl/Radiant_Impl_CSS_Selector_Property_Support.md).
**Implementation update (2026-10-05).** Individual `translate`, `rotate` and `scale` properties now have literal grammar validation and shared HTML transform consumers, including live raster paint, SVG export, computed style, hit testing and transformed containing blocks. Math expressions, animation/transition sampling, dirty-callback serialization and SVG descendant styling remain incomplete. Their computed state and NameIds follow **D4.5.1v4** and **D4.6.1v3**; the historical totals below have not been recounted. Animation shorthand and transition lists now share grammar, cascade projection and custom-property substitution; focused live checks cover multiple/repeated names, paused and completed effects, fractional iterations, per-property timing lists, and step-easing boundaries. Their document-owned timelines and scratch computed lists follow **D4.5.1v4**. Remaining animation/value gaps are stated in §7.

**Implementation update (2026-10-06).** Nested CSSOM rule lists remain live through mutation with named syntax/index/hierarchy errors and namespace restrictions. Scoped styles apply tested root, limit, proximity and mutation behavior in HTML, SVG and PDF. Backface paint/input and six PDF face samples are verified. Registered RGB/HSL/HWB channel math, color lists, inherited `currentColor` and `color(srgb …)` now share validation, computation and paint; 21 live assertions agree with Chromium, with PNG/PDF/SVG output checks. Remaining color spaces and interpolation are stated below. These changes retain **D4.5.1v4** ownership and **D5.3.3** exact native roots; the historical property totals remain pending re-audit.

To check a feature yourself, lay the page out and read the JSON, or render it and look:

```bash
lambda layout page.html --view-output tree.json   # boxes and resolved styles, per element
lambda render page.html -o page.png               # what the raster painter draws
lambda render page.html -o page.svg -vw 800       # vector output at an 800 px viewport
```

`lambda layout` writes its JSON to the `--view-output` file. Without that option it writes `./temp/view_tree.json` under the current folder, and silently writes nothing when that folder has no `temp` subfolder. Its `-o` flag is accepted but ignored.

**Contents:** [Conformance](#1-conformance-summary) · [HTML](#2-html) · [Selectors](#3-css-selectors) · [At-rules](#4-at-rules) · [Cascade](#5-cascade-inheritance-and-custom-properties) · [Values](#6-values-units-and-functions) · [Properties](#7-css-property-coverage) · [Layout](#8-layout-modes) · [Box model](#9-box-model-and-sizing) · [Backgrounds](#10-backgrounds-and-borders) · [Text and fonts](#11-text-and-fonts) · [Effects](#12-visual-effects) · [Animation](#13-transitions-and-animations) · [Interaction](#14-interaction) · [SVG](#15-svg) · [Images](#16-image-formats) · [Output](#17-output-targets) · [Known limitations](#18-known-limitations) · [Related](#19-related-documentation)

## 1. Conformance Summary

Layout conformance is measured against a real browser. A test page is laid out with `lambda layout`, and every element box, inline span and text run is compared with the same page captured in headless Chrome through Puppeteer. The comparison allows 5 px, or a proportional allowance of 2.5–7 % where text measurement errors accumulate (`test/layout/test_radiant_layout.js`). A test passes when all three metrics match; a *partial* baseline entry records per-metric scores that must not regress. The layout corpora live in the companion `lambda-test` repository, which the main repository links as `test/layout/`.

| Suite | Result | Source | Notes |
|---|---|---|---|
| HTML parsing: html5lib tree construction | **364 / 364** | `test/test_wpt_html_parser_gtest.exe`, re-run 2026-09-28 | The test runs 7 of the 53 html5lib files in `test/html/wpt/` (`tests1`–`tests3`, `blocks`, `comments01`, `entities01`, `entities02`). The directory holds 1,560 cases; the other 1,196 are not run by any test. |
| CSS 2.1 (W3C CSS 2.1 test suite) | **9,234 of 9,857** recorded as passing (93.7 %) | `test/layout/data/css2.1/baseline.txt`, dated 2026-05-10 | The suite ships as `css2.1.zip`; 9,857 of its 9,928 pages are runnable (they have a Chrome reference and are not on the skip list). The unpacked suite and its baseline are local files, outside version control and outside the gated set, so the figure is not re-checked on every change. The README's 98.2 % (1,788 / 1,821) could not be traced to any file. |
| Gated layout baselines (15 suites, below) | **1,874** full passes and **1,403** partial of 3,302 tests | `test/layout/data/<suite>/baseline.txt` | Run by `make test-layout-baseline` and `make test-radiant-baseline`; the suite list is `LAYOUT_BASELINE_SUITES` in the `Makefile`. |
| Other recorded layout baselines | CSS UI 1,354 + 101 partial of 1,459; CSS Box Alignment 312 + 2 of 335; `pretext` 428 of 488; real pages 22 + 25 of 51; website templates 1 + 169 of 170 | `baseline.txt` in `wpt-css-ui`, `wpt-css-align`, `pretext`, `page`, `web-tmpl` | Recorded but not part of the gate. `pretext` is CJK and Latin paragraph wrapping at many widths; `page` includes CSS Zen Garden and other real sites. |
| CSS Syntax (WPT `css/css-syntax`) | **38 / 44** pass, 6 skipped, 0 fail | `test/test_wpt_css_syntax_gtest.exe`, re-run 2026-09-28 | Skipped: four UTF-16 charset pages and two upstream-broken tests. |
| CSSOM View (WPT `css/cssom-view`) | 42 of 221 test files pass | `test/wpt/wpt_cssom_view_baseline.txt` | `getBoundingClientRect`, `getClientRects`, `elementFromPoint`, `offset*`, scroll APIs. |
| CSS Transitions (WPT `css/css-transitions`) | 1 of the 10 event tests the runner selects | `test/wpt/wpt_css_transitions_baseline.txt`, `test/wpt/test_wpt_css_transitions_gtest.cpp` | Only `events-*` and three transition-event files are run; see [§13](#13-transitions-and-animations). |
| HTML forms and `FormData` (WPT) | 115 passing files | `test/wpt/wpt_form_baseline.txt` | |
| DOM, events, ranges, observers | see [JS_DOM_Support.md](JS_DOM_Support.md) | `test/wpt/*_baseline.txt` | |

The gated layout suites:

| Suite | Covers | Tests with a Chrome reference | Full passes | Partial |
|---|---|---:|---:|---:|
| `form` | WPT HTML forms plus form-state pages | 381 | 321 | 57 |
| `wpt-css-text` | CSS Text: breaking, bidi, hyphens, spacing, i18n | 980 | 550 | 430 |
| `wpt-css-inline` | CSS Inline Layout | 169 | 51 | 118 |
| `wpt-css-sizing` | CSS Box Sizing: intrinsic sizes, `aspect-ratio`, `stretch` | 776 | 270 | 506 |
| `wpt-css-images` | CSS Images: gradients, `image-set`, `image-orientation`, `object-view-box` | 56 | 56 | 0 |
| `wpt-css-tables` | CSS Tables | 57 | 57 | 0 |
| `wpt-css-lists` | CSS Lists and Counters | 60 | 42 | 18 |
| `wpt-css-position` | CSS Positioned Layout | 139 | 129 | 10 |
| `wpt-css-multicol` | CSS Multi-column Layout | 362 | 144 | 218 |
| `wpt-css-display` | CSS Display: `contents`, `flow-root`, … | 145 | 145 | 0 |
| `wpt-css-box` | CSS Box: `margin-trim` | 17 | – | – (no baseline recorded) |
| `puppertino` | pages built with the Puppertino CSS framework | 12 | 6 | 1 |
| `markdown` | Markdown documents rendered through HTML | 58 | 13 | 45 |
| `bootstrap` | pages built with Bootstrap | 20 | 20 | 0 |
| `tailwind` | pages built with Tailwind CSS | 70 | 70 | 0 |

## 2. HTML

The HTML parser (`lambda/input/html5/`) is a WHATWG HTML5 tokenizer and tree builder: all insertion modes, the adoption agency algorithm, foster parenting, SVG and MathML foreign content, and quirks mode, parsing with scripting enabled. Its conformance figure is in [§1](#1-conformance-summary). HTML presentational attributes are listed in [§5](#5-cascade-inheritance-and-custom-properties).

Elements with special behaviour:

| Element or feature | Status | Notes |
|---|---|---|
| `<script>` | ✅ | Inline and external (file and http), `defer`, `async` and `type="module"`, with `DOMContentLoaded`, `document.readyState`, `load` and timers. Scripts run under `lambda layout` and `lambda render` as well as `lambda view`, so the output reflects the DOM they build. No Web Workers; `WebSocket` is an inert stub. The JavaScript and DOM surface is in [JS_DOM_Support.md](JS_DOM_Support.md); the integration design is [RAD_21](dev/radiant/RAD_21_JS_Scripting_Integration.md). |
| `<style>`, `<link rel="stylesheet">`, `<base href>` | ✅ | See [§5](#5-cascade-inheritance-and-custom-properties). |
| `<meta name="viewport">` | ◐ | Parsed; its effect was not verified. |
| `<input>`: `text`, `password`, `email`, `url`, `search`, `tel`, `number` | ✅ | In `lambda view`: caret, selection, undo, IME and password masking, all native ([RAD_19](dev/radiant/RAD_19_Form_Controls.md)). |
| `<input>`: `checkbox`, `radio`, `range`, `submit`, `reset`, `button`, `hidden` | ✅ | `accent-color` is honoured. |
| `<input>`: `color`, `date`, `time`, `month`, `week`, `datetime-local`, `file` | ◐ | Laid out at browser sizes but drawn as plain text boxes: `color` shows its value as text, `date` and `file` are empty boxes. No pickers and no *Choose File* button. |
| `<select>`: drop-down, `multiple`/`size` list box, `<optgroup>` | ✅ | The drop-down list opens only in `lambda view`. |
| `<textarea>` | ◐ | Laid out correctly, but `lambda render` does not paint its initial text (verified). |
| `<button>`, `<label>`, `<fieldset>`, `<legend>`, `<output>` | ✅ | |
| `<datalist>` | ◐ | Hidden; no suggestion list. |
| `<progress>`, `<meter>` | ❌ | Sized, but nothing is drawn. |
| Constraint validation | ◐ | `required`, `type`, `min`, `max`, and `step` feed a shared native validity calculation for static CSS, live CSS, and the DOM ValidityState object. `minlength`/`maxlength` and full HTML `pattern` syntax still need broader checks; static pattern matching currently uses the RE2 subset. See [Lambda_Packages.md §10.3](Lambda_Packages.md#103-dom--browser-behaviour-for-html). |
| Tables | ✅ | `border`, `cellpadding`, `cellspacing`, `bgcolor`, `rowspan`/`colspan`, `<col span>`; layout in [§8](#8-layout-modes). |
| `<ol>`, `<ul>`, `<li>` | ◐ | `start`, `reversed` and `<li value>` work; `<ol type>` is ignored (`type="a"` still numbers 1, 2, …); use `list-style-type`. |
| `<img>` | ◐ | `width`/`height` attributes and intrinsic sizes. `srcset` uses only its first candidate, and an `<img>` with `srcset` but no `src` is not painted. A broken image reserves a box for its `alt` text but draws neither the text nor an icon. Formats in [§16](#16-image-formats). |
| `<picture>` | ◐ | Takes the first `<source>` without `media` or `type`, and only when the `<img>` has no `src`. |
| `<video>`, `<audio>` | ◐ | macOS only (AVFoundation), local files only, no `poster`. `lambda render` shows an empty box. See [RAD_22](dev/radiant/RAD_22_Media_Webview.md). |
| `<iframe>` | ✅ | `src` and `srcdoc` load as nested documents laid out by Radiant; nesting depth is capped. |
| `<canvas>` | ◐ | 2D context: rectangles; paths with lines, Bézier curves and arcs; fill, stroke and clip; `save`/`restore`; `translate`/`rotate`/`scale`; solid colours; line width, cap and join; `globalAlpha`; `font`, `fillText`, `measureText`, `textAlign`. Missing: `drawImage`, gradients and patterns, `getImageData`/`putImageData`, `strokeText`, `setTransform`, `ellipse`, `arcTo`, shadows, compositing modes and WebGL. At most 4,096 px per side. |
| Inline `<svg>` | ◐ | See [§15](#15-svg). |
| `<math>` (MathML) | *not verified* | Parsed as foreign content; rendering was not checked. Lambda typesets LaTeX math through its own package ([Math_Support.md](Math_Support.md)). |
| `<details>`, `<summary>` | ✅ | Disclosure marker, `open`, and exclusive groups through `name`. |
| `<dialog>` | ◐ | A closed dialog is hidden and `showModal()` centres it. A non-modal `open` dialog gets no UA border, padding or positioning, `::backdrop` is not painted, and `close()` and `show()` are missing. |
| `popover` attribute | ✅ | |
| `<template>` | ✅ | Inert, not rendered. |
| Shadow DOM, `<slot>` | ◐ | `attachShadow()` and slot assignment render. Declarative shadow roots (`shadowrootmode`) are not supported. |
| `<hr>`, `<br>`, `<wbr>`, `<pre>` | ✅ | |
| `<ruby>`, `<rt>`, `<rp>` | ◐ | One base and annotation pair renders above the base; two pairs in one `<ruby>` overlap. |
| `<bdo>` | ❌ | `<bdo dir="rtl">` does not reverse its text. `<bdi>` was not verified. |
| `<embed>`, `<object>` | ◐ | Image and SVG sources only. |
| `<marquee>` | ❌ | Parsed, not animated. |

## 3. CSS Selectors

Selectors are parsed in `lambda/input/css/css_parser.cpp` and matched in `lambda/input/css/selector_matcher.cpp`; the same matcher serves stylesheets and the DOM query APIs (`querySelector`, `matches`, `closest`).

| Selector | Status | Notes |
|---|---|---|
| Type, universal `*`, class, ID | ✅ | Type names are case-insensitive for HTML; class and ID are case-sensitive, with the quirks-mode exception. |
| Attribute `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, the `i` and `s` flags | ✅ | `s` forces a case-sensitive value comparison even when the matcher is in a case-insensitive mode. |
| Namespaces `ns\|E`, `\|E`, `[ns\|a]`, `[\|a]`, `[*\|a]` | ✅ | `@namespace` resolves named and default type namespaces; attributes without a prefix stay in the null namespace. Qualified, null and wildcard attribute selectors match parsed attributes and live `setAttributeNS` state. DOM queries reject undeclared named prefixes but accept null and wildcard forms. |
| Combinators: descendant, `>`, `+`, `~` | ✅ | |
| Column combinator `\|\|` | ◐ | HTML `<col>`/`<colgroup>` membership selects `<td>`/`<th>` through the table slot model, including column and cell spans; queries and live stylesheet restyling are tested. Other host-language grids remain open ([Selectors 5 §9.1](https://drafts.csswg.org/selectors-5/#the-column-combinator), [HTML table model](https://html.spec.whatwg.org/multipage/tables.html#forming-a-table)). Temporary occupancy follows **D4.5.1v4**. |
| `:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:nth-last-child()`, `:first-of-type`, `:last-of-type`, `:only-of-type` | ✅ | An+B syntax including `odd`/`even`. |
| `:nth-of-type()`, `:nth-last-of-type()` | ✅ | Count same-type element siblings. |
| `:nth-child(An+B of S)`, `:nth-last-child(An+B of S)` | ✅ | Filter element siblings through `S` before applying An+B, including negative steps. |
| `:empty` | ✅ | Comments do not make an element nonempty. |
| `:is()`, `:where()`, `:not()` with selector lists and complex selectors | ✅ | `:where()` contributes zero specificity. |
| `:has()` | ✅ | Descendant, child, adjacent-sibling and following-sibling arguments match. DOM tests cover attach/remove; child insertion and sibling class changes restyle stylesheet matches (`RenderOutputParity.HasSelectorRestylesAfterChildAndSiblingMutation`). |
| `:link` | ✅ | Any element with an `href`. |
| `:visited` | ❌ | Never matches (there is no history). |
| `:any-link`, `:local-link` | ✅ | `:any-link` uses link state. `:local-link` compares a resolved link URL with the document URL, ignoring fragments. |
| `:hover`, `:active`, `:focus`, `:focus-within`, `:focus-visible`, `:target` | ◐ | Live in `lambda view` (`:focus-visible` follows keyboard focus; `:target` follows fragment navigation). A static `layout` or `render` has no pointer or focus, so they never match there. |
| `:checked`, `:disabled`, `:enabled`, `:required`, `:optional`, `:read-only`, `:read-write`, `:open` | ✅ | From the element's attributes in static output, and from live state in `lambda view`. |
| `:placeholder-shown`, `:valid`, `:invalid` | ◐ | Initial stylesheet matching now reads native control values and shared validity, including invalid form/fieldset descendants; static render and live submit/value tests pass (`RenderOutputParity.StaticValidityAndPlaceholderSelectorsPaint`, `RenderOutputParity.ValiditySelectorsUseInitialAndLiveConstraints`). `pattern` and less common controls remain partial. |
| `:default` | ✅ | Uses default checked/selected attributes and the form's first submit button, including a disabled first button (`RenderOutputParity.DefaultSelectorUsesFormDefaults`). |
| `:indeterminate` | ✅ | Uses the checkbox IDL state, unchecked radio groups, and progress without a value; checkbox state changes restyle the document (`RenderOutputParity.IndeterminateSelectorTracksFormState`). |
| `:in-range`, `:out-of-range` | ✅ | Numeric/date/time controls with valid range limits use the shared input value codec. Live value changes invalidate value-sensitive stylesheet selectors (`RenderOutputParity.RangeSelectorsTrackLiveNumericValue`). |
| `:user-invalid`, `:user-valid` | ◐ | User validity starts false, changes on committed control edits or `requestSubmit()`, and clears on reset. The selectors follow subsequent value changes; submit/reset/query/render are covered by `RenderOutputParity.ValiditySelectorsUseInitialAndLiveConstraints`. Other interaction paths need broader checks. |
| `:lang()`, `:dir()` | ◐ | `:lang()` uses inherited language. `:dir()` uses inherited HTML direction and first-strong detection for `dir=auto`; form-control direction and live invalidation need broader checks. |
| `:scope` | ✅ | Uses the query root in DOM APIs and the document root in stylesheets. |
| `:modal`, `:popover-open` | ✅ | Match dialog/popover DOM state. Opening and removal/hiding restyle sibling selectors in live output (`RenderOutputParity.DialogAndPopoverStateSelectorsRestyle`). |
| `:defined` | ✅ | Built-in elements match; autonomous custom elements match after `customElements.define()`. A definition triggers stylesheet recascade, and DOM queries see the new state (`RenderOutputParity.DefinedSelectorRestylesAfterCustomElementRegistration`). |
| `:fullscreen`, `:autofill`, `:playing`, `:paused` | ❌ | No matching state source yet. |
| `::before`, `::after`, `::first-line`, `::first-letter`, and the legacy single-colon spellings | ✅ | |
| `::marker` | ◐ | `color` paints on the marker without recoloring list text; pseudo font size, family, weight and style feed marker measurement and raster/SVG text paint. `RenderOutputParity.MarkerColorPaintsSeparatelyFromListText` and `MarkerPseudoFontSizesMeasuredAndPaintedGlyphs` check color and a live font restyle. Other allowed marker properties need broader checks. |
| `::placeholder` (and the `-webkit-`/`-moz-` spellings) | ✅ | |
| `::selection` | ◐ | `background-color` and the color part of `background` paint in contenteditable text and text controls; `color` paints selected ordinary-text, input, and textarea glyphs. The highlight cascade keeps these declarations off the host and survives recascade (`DomIntegrationTest.SelectionColorsCascadeWithoutHostDeclarations`, `RenderOutputParity.SelectionBackgroundUsesPseudoStyle`). Other allowed highlight properties remain unsupported. |
| `::backdrop` | *parsed only* | Stored; nothing lays it out or paints it. |
| `::file-selector-button` | ◐ | File inputs paint a separate raster button with pseudo font, text color, background color, border shorthand and padding shorthand; its label retains the host color after live restyling (`DomIntegrationTest.FileSelectorButtonKeepsStyleOffHost`, `RenderOutputParity.FileSelectorButtonPaintsSeparateFromHostAfterRestyle`). Other button properties, SVG/PDF output, and styled intrinsic height remain unsupported. |
| `::slotted()` | ◐ | Shadow-DOM slots; not verified on real pages. |
| `::part()`, `::cue`, `::highlight()`, `::-webkit-*` | ❌ | Never match. |

Specificity and selector-list handling now count supported pseudo-classes, pseudo-elements, and `:nth-child(... of S)` arguments. A stylesheet rule uses the greatest specificity among its matching list branches. Invalid ordinary selector lists are rejected; only `:is()` and `:where()` use forgiving-list recovery, including undeclared namespace prefixes. CSS pseudo-class, at-rule and property names are normalized without changing case-sensitive IDs, classes or custom-property names. Logical and physical side declarations now honor layer order, including its reversal for `!important` (`RenderOutputParity.LogicalAndPhysicalBordersRespectLayerOrder`). The column combinator handles HTML table slots; other host-language grids remain open.

## 4. At-Rules

| At-rule | Status | Notes |
|---|---|---|
| `@media` | ◐ | Supported: `all`, `screen`, and `print` when exporting HTML to PDF; `width` and `height` with `min-`/`max-`, exact, and one/two-sided range comparisons (px, em, rem, with em fixed at the 16 px initial font size); `aspect-ratio` with ratio values; `resolution` with dppx/x, dpi and dpcm, using the engine's configured device-pixel ratio (currently 1 for normal page loads); `orientation`; `prefers-color-scheme` (defaults to `light`); `prefers-reduced-motion` (defaults to `no-preference`); `and`, `or`, `not`, `only` and comma lists. Boolean `width`, `height`, `aspect-ratio`, `resolution`, `color`, `monochrome` and `orientation` are evaluated; unknown features and media types do not match. Other features such as `hover` and `pointer` remain unsupported; `speech` never matches. `CssEngineTest.MediaQueryRangeBooleanAndOrUseFeatureValues`, `MediaRatioAndResolutionUseTypedRangeValues`, `MediaQueryPrintContextInvalidatesCachedResults`, `RenderOutputParity.MediaRangeAndOrSelectVisibleRules`, and `PdfPrintMediaSelectsRulesAndLinkedStylesheet` check evaluation and paint. The temporary condition parser follows **D4.5.1v4**. |
| `@import` | ◐ | Local files and http(s), nested up to 5 levels. Imported rules apply at the import rule's source position. `layer()`/anonymous `layer`, `supports()` and media conditions use the corresponding layer/supports/media evaluators; imports after ordinary rules are ignored. Media feature coverage remains partial. |
| `@font-face` | ◐ | `font-family`; `src` with `url()` plus `format()` (woff2, woff, truetype, opentype) and the first `local()`; `font-style` normal, italic or oblique (no angle); `font-weight` as a single value (no ranges); `unicode-range`. `font-display` is parsed and ignored. Top-level rules only. Remote (http/https) font URLs are skipped by the synchronous loader (see [§11](#11-text-and-fonts)). |
| `@keyframes` | ◐ | Top-level rules with validated supported endpoints; important declarations are ignored. Per-stop timing and property-specific intervals are sampled; selector lists and broader value grammar remain partial; see §13. `@-webkit-keyframes` is dropped. |
| `@supports` | ◐ | `not`, `and`, `or`, parentheses, `selector()` and property-value checks work for validated properties. `(display: bogus)` is false. Value validation is still incomplete across the full property registry. |
| `@layer` | ◐ | Named, nested, anonymous and order-statement layers rank declarations across stylesheets and layered imports; `!important` reverses that order, and `revert-layer` rolls back longhands and `all` across layers. |
| `@container` | *parsed only* | Rules inside never apply; `container-type` and `container-name` have no effect. |
| CSS nesting (`&`, nested rules) | ◐ | Nested style rules apply at multiple depths, including implicit descendants, `&` in compounds or elsewhere in the selector, parent selector lists with `:is()` specificity, and declarations interleaved with child rules. Nested `@media`/`@supports` conditions apply relative selectors and direct declarations. Standalone `&` uses `:scope` in stylesheets and DOM selector parsing. CSSOM serializes nested rules and declaration runs in source order, reflects declaration edits, and rebinds descendants when an outer `selectorText` changes (`RenderOutputParity.NestedCssomSerializesAndRebindsAfterSelectorMutation`). Focused parser, query, paint, live restyling and CSS Syntax WPT ambiguity/error-recovery cases pass. Nested `@container` still has no effect. Nested style/group lists now remain live through insertion and deletion; relative rules, declaration runs, ownership, namespace restrictions and named mutation errors pass 38 live assertions (`test/ui/css_nested_rule_mutation.json`). Wrapper identity and broader rule interfaces remain open. The parsed rule source follows **D4.5.1v4** ownership. |
| `@page` | ◐ | Active paper size and margins set the PDF MediaBox and content inset for fixed one-page export. Automatic page breaking is available through the separate explicit paged path; page-margin boxes remain limited. |
| `@namespace` | ✅ | Named declarations bind type, universal and attribute selectors; the default declaration binds only type and universal selectors. Quoted and `url(...)` forms work; declarations after ordinary rules are ignored. DOM queries have no stylesheet namespace context. |
| `@scope` | ◐ | Start/end boundaries, implicit owner scopes, nested roots, relative selectors, direct declarations, specificity/proximity order and live class changes apply. CSSOM exposes boundaries and live scoped insertion/deletion. Parser/cascade checks, 27 browser-confirmed live assertions and raster/SVG/PDF export checks pass (`test/ui/css_scope_rules.json`, `RenderOutputParity.ScopeRootsLimitsAndProximityReachRasterSvgAndPdf`). Broader shadow/import contexts and wrapper interfaces need audit. State follows **D4.5.1v4**. |
| `@property` | ◐ | Valid registrations control defaults, inheritance and computed-value validation. Tested numeric math and absolute/viewport/`em`/`rem`, angle/time/resolution units compute on the declaration owner, including transform lengths and integer lists; mixed percentages remain available to the final consumer. Font-size and direct variable cycles invalidate their dependencies. CSSOM descriptors, computed values and live insertion/deletion pass 35 browser-confirmed assertions; registered math passes 18 more. PNG/SVG/PDF output matches explicit-value references. Other relative units, full image/color/URL computation, nonlinear percentage/nonfinite math edges, broader contexts, interfaces and registered-property interpolation remain open (`test/ui/css_registered_properties.json`, `css_registered_math.json`). Ownership follows **D4.5.1v4**. |
| `@counter-style`, `@starting-style`, `@font-feature-values`, `@view-transition` | ❌ | Skipped with their block; rules inside `@starting-style` are lost. |
| `@charset` | ✅ | Used to decode external stylesheets. |

The viewport that `@media` sees depends on the command; `-vw` and `-vh` override all of them:

| Command | Viewport for `@media` |
|---|---|
| `lambda layout` | 1200 × 800 |
| `lambda render` to SVG, PNG or JPEG | 1200 × 800; the output canvas is then sized to the content |
| `lambda render` to PDF | 800 × 1200 |
| `lambda view` | the window, 1200 × 800 at start |

In `lambda view`, media queries (including `<link media>` and `<style media>`) are evaluated once when the page loads; resizing the window reflows the page but does not re-run the cascade. This was read in the code, not observed live.

## 5. Cascade, Inheritance and Custom Properties

Stylesheets are collected in document order, each rule is matched against every element, and the winning declaration per property is chosen by importance, specificity and source order. The user-agent stylesheet is not a `.css` file: it is built into the engine (`radiant/resolve_htm_style.cpp`) and applied before author CSS. See [RAD_02 — CSS Style Resolution](dev/radiant/RAD_02_CSS_Style_Resolution.md).

| Feature | Status | Notes |
|---|---|---|
| `<style>`, `<link rel="stylesheet">`, the `media` attribute | ✅ | Local files and http(s); alternate stylesheets are ignored; `lambda layout -c file.css` adds an extra sheet. |
| Inline `style` attribute | ✅ | Beats stylesheet rules of normal importance. |
| `!important` | ◐ | Beats normal declarations; an inline author `!important` wins over an author stylesheet `!important`, including layered declarations. User stylesheets remain unavailable. |
| Origins | ◐ | Built-in UA styles and author styles only; there are no user stylesheets. |
| Specificity and source order | ◐ | Supported selectors and matching list branches are ranked correctly; layer order precedes specificity. Some selector and CSS-wide value cases remain partial ([§3](#3-css-selectors)). |
| Inheritance, `inherit` | ✅ | |
| `initial`, `unset` | ◐ | `width` and `height` now resolve to `auto`. Other properties still need a complete computed-value audit. |
| `revert` | ◐ | Falls back to the built-in UA default. |
| `revert-layer` | ◐ | Resolves an ordinary property's or `all` shorthand's earlier layer or origin. Computed-value coverage across all property consumers still needs an audit. |
| `all` | ◐ | `initial`, `unset`, `inherit`, `revert` and `revert-layer` participate in the property cascade beyond fonts. Visual fixtures check inherited display, dimensions, padding, border, background, color and opacity, plus `revert` clearing author background and color; other property consumers need an audit. `direction`, `unicode-bidi` and custom properties remain excluded. |
| Custom properties and `var()` | ◐ | Focused layout checks cover declaration-site inheritance, a cycle with fallback, `var()` in `calc()`, and invalid substitutions falling back from `width` to `auto`. Space-separated tokens expand inside `margin` and `padding` shorthands, with invalid substitution resetting the shorthand at computed-value time. Commas regroup substituted function arguments and surrounding tokens; SVG color/spacing and exact raster gradient comparisons verify comma fallback lists, empty fallbacks and empty custom properties. Paint fixtures verify keyword defaulting, active-cycle invalidation with consumer fallbacks, declaration-owner lookup in SVG, and case-sensitive custom names. Custom-property origin/layer rollback shares the regular cascade rules. Computed CSSOM serialization, rollback obtained through substitution, large-value limits and broader property/shorthand validation remain open. |
| `@property` registration / `CSS.registerProperty()` | ◐ | Typed defaults/inheritance and tested computed values apply, including RGB/HSL/HWB math, registered color lists and inherited `currentColor` (21 live color assertions). The script API overrides stylesheet registrations and passes 66 browser-confirmed conversion, validation, name, ownership and live-style checks. Cross-realm contexts, complete computation, CSSOM interfaces and interpolation remain open; see [§4](#4-at-rules). |
| Cascade layers | ◐ | Named, nested, anonymous, cross-sheet and `!important` ordering are active; see [§4](#4-at-rules) for remaining gaps. |
| HTML presentational attributes | ✅ | `body` `bgcolor`/`marginwidth`/`marginheight`/`leftmargin`/`topmargin`; `table` `bgcolor`/`border`/`align`/`cellpadding`/`cellspacing`/`rules`/`width`/`height`; `tr`/`td`/`th` `bgcolor`/`align`/`valign`/`nowrap`/`width`/`height`; `font` `color`/`size`/`face`; `width`/`height` on `img`, `iframe`, `video`, `canvas`, `embed` and `object`; `align` on `hr`/`div`/`p`; `dir`; `hidden`; `ol` `start`/`reversed` and `li` `value`; `size` on `input`/`select`; `cols`/`rows` on `textarea`. |

In `lambda view`, a change of interaction state (hover, focus, checked, …) re-runs the cascade when a stylesheet uses the affected pseudo-class, then reflows the page. Static `layout` and `render` run the cascade once, before layout.

## 6. Values, Units and Functions

Unsupported functions and units remain a source of differences from a browser. The length parser rejects unknown unit tokens, but registered units and math functions still need a property-by-property computed-value audit; some unsupported consumers produce an unresolved value or zero.

### 6.1 Units

| Unit | Status | Notes |
|---|---|---|
| `px`, `cm`, `mm`, `Q`, `in`, `pt`, `pc` | ✅ | |
| `em`, `rem`, `ex`, `ch` | ✅ | `ex` and `ch` come from the font's metrics. |
| `%` | ✅ | Resolved against the right reference per property, including deferred resolution for absolutely positioned boxes. |
| `vw`, `vh`, `vmin`, `vmax` | ✅ | Against the layout viewport ([§4](#4-at-rules)). |
| `lh` | ◐ | Uses only the element's own `line-height`, otherwise `normal`. |
| `rlh`, `cap`, `ic` | ◐ | Used widths resolve against root line-height, font cap height, and the CJK water ideograph's advance, respectively (`RenderOutputParity.FontAndViewportRelativeUnitsResolveUsedWidths`). Other property consumers and font-affecting self-reference cases need checks. |
| `vi`, `vb`, `sv*`, `lv*`, `dv*` | ◐ | Widths resolve against the viewport's physical or writing-mode logical axes; viewport-relative font sizes also resolve. The headless engine has no retractable viewport UI, so small, large, and dynamic variants share its current viewport. Other property consumers and live UI changes need checks. |
| `cqw`, `cqh`, `cqi`, `cqb`, `cqmin`, `cqmax` | ❌ | Parsed; container-relative resolution is not implemented. |
| `deg`, `rad`, `grad`, `turn` | ✅ | A shared angle converter feeds transforms, linear and conic gradients, `hue-rotate()` and `hsl()` hue (`RenderOutputParity.AngleUnitsAgreeAcrossColorGradientAndFilter`). |
| `s`, `ms` | ✅ | |
| `fr` | ✅ | Grid tracks. |
| `dpi`, `dpcm`, `dppx`, `x` | ◐ | Only inside `image-set()` in `content`. |

### 6.2 Functions

| Function | Status | Notes |
|---|---|---|
| `calc()` | ✅ | Mixed units, precedence, nesting, `var()` inside. A percentage inside `calc()` on an absolutely positioned box is resolved against the parent element instead of the containing block. |
| `min()`, `max()`, `clamp()` | ✅ | |
| `round()`, `mod()`, `rem()`, `abs()`, `sign()`, `sin()`/`cos()`/`tan()`/`asin()`/`acos()`/`atan()`/`atan2()`, `pow()`, `sqrt()`, `hypot()`, `log()`, `exp()`, `pi`, `e`, `infinity` | ◐ | Typed length expressions reject unknown identifiers/functions and incompatible dimensions before cascade. Numeric math now reaches used widths, including `calc(100px * sin(30deg))` at 50px, rounding strategies, modulus, and nested expressions (`RenderOutputParity.MathFunctionsAndInvalidCalcResolveUsedWidths`). Transform scale/angle `calc()` arguments now reach sampled matrices and paint (`AnimationTransformMathSpatialAndMatrixValuesReachPaintAndClientRects`). Broader angle/numeric domains, other non-length consumers, full infinity handling, and computed-style serialization remain open. |
| `var()` | ◐ | As a whole value, inside `calc()`/`min()`/`max()`, in colours and in `border`; a space-separated variable also expands into surrounding `margin`/`padding` shorthand and modern `rgb()` tokens. `display: var(--mode)` controls box generation, and separately substituted outside/inside keywords form a flex display value. A variable track list works as the whole `grid-template-columns` value and inside fixed or `auto-fill` `repeat()`; other grid forms and the wider function inventory need verification. See also [§5](#5-cascade-inheritance-and-custom-properties). |
| `env()` | ❌ | Evaluates to 0; the fallback is ignored. |
| `attr()` | ◐ | In `content` only; typed `attr()` elsewhere gives 0. |
| `counter()`, `counters()` | ✅ | With a list-style argument. |
| `url()` | ◐ | `background-image`, `list-style-image`, `content`, `@font-face`, `@import`, and raster `border-image` sources. Not for `cursor`, `mask-image`, `filter` or `clip-path`; vector `border-image` sources remain unsupported. |
| `image-set()` | ◐ | In `content` only; takes the first candidate. |
| `cross-fade()`, `element()` | ❌ | |

### 6.3 Colours

| Syntax | Status | Notes |
|---|---|---|
| Hex: `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | ✅ | |
| Named colours, `transparent`, `currentColor` | ✅ | All 148 CSS named colours. |
| `rgb()`, `rgba()` | ✅ | Comma and space/slash syntax; number and percentage alpha both paint (`RenderOutputParity.RgbPercentageAlphaPaintsLikeNumericAlpha`). |
| `hsl()`, `hsla()` | ✅ | Hue numbers and all four CSS angle units. |
| `hwb()` | ◐ | Hue numbers, CSS angle units and channel math share computation with RGB/HSL. Whiteness/blackness normalization and missing alpha paint correctly. Registered CSSOM preserves missing components; native round-trip checks follow CSS Color 4's September 2026 serialization rules, which differ from Chromium 154. Interpolation and complete ordinary-property contexts remain open. |
| `color(srgb …)` | ◐ | Registered computation, model-preserving CSSOM and PNG/PDF/SVG paint are verified. Computed components retain extended range and missing channels; paint rounds/clamps to sRGB bytes. Other predefined spaces and animation contexts remain open. |
| `lab()`, `lch()`, `oklab()`, `oklch()`, other `color()` spaces, `color-mix()`, `light-dark()`, relative colours (`rgb(from …)`) | ❌ | Render black or not at all; not included in the focused sRGB update. |
| System colours (`Canvas`, `ButtonText`, …) | ❌ | Recognized but have no value; render black. |

Colours are sRGB throughout; there is no wide-gamut output.

### 6.4 Gradients

| Function | Status | Notes |
|---|---|---|
| `linear-gradient()` | ◐ | All CSS angle units, `to` sides and corners (corner angles use the painted box's aspect ratio), several stops, two-position stops. Colour hints and `in <colorspace>` are ignored. `RenderOutputParity.GradientCornerDirectionUsesPaintedBoxAspectRatio` checks a non-square box. |
| `repeating-linear-gradient()` | ◐ | Correct in raster output; wrong offsets in SVG output. |
| `radial-gradient()` | ◐ | `circle`/`ellipse` and `at <position>` with side/center keywords, percentages and lengths; a positioned first color stop is preserved. Size keywords (`closest-side`, `farthest-corner`, …) are not parsed. `RenderOutputParity.GradientCentersResolveKeywordsPercentagesAndLengths` and `ConicAngleStopsSetRepeatPeriod` check paint. |
| `repeating-radial-gradient()` | ◐ | Circle gradients repeat by the first-to-last stop span in raster output (`RenderOutputParity.RepeatingRadialGradientUsesColorStopPeriod`). Ellipse geometry and vector output remain incomplete. |
| `conic-gradient()` | ◐ | `from <angle>` and `at <position>` with side/center keywords, percentages and lengths. Stops accept angles, percentages and two-position forms, with ordered stop fixup. `RenderOutputParity.GradientCentersResolveKeywordsPercentagesAndLengths` and `ConicAngleStopsSetRepeatPeriod` check raster paint; vector output is still absent ([§17](#17-output-targets)). |
| `repeating-conic-gradient()` | ◐ | Repeats by the span from its first to last color stop in raster output, through both `background` and `background-image` (`RenderOutputParity.ConicAngleStopsSetRepeatPeriod`). Vector output remains absent. |

### 6.5 Transform, filter, easing and shape functions

| Family | Status | Notes |
|---|---|---|
| Transform functions | ◐ | `translate()`/`X`/`Y`/`Z`/`3d`, `rotate()`/`X`/`Y`/`Z`/`3d`, `skew()`/`X`/`Y`, `matrix()`, `matrix3d()`, `perspective()`, all angle units. Percentage `scale()` and axis/3D variants resolve to numeric factors (`RenderOutputParity.PercentageScaleMatchesNumericTransform`). Individual `translate`, `rotate` and `scale` properties compose before the transform list in HTML raster/SVG paint and live geometry; literal 2D/3D components, variables, percentages and resets are checked by `RenderOutputParity.IndividualTransformsComposeBeforeTransformAndSurviveRestyle`, `IndividualThreeDimensionalTransformsReuseFunctionMatrices`, `IndividualTransformsKeepIndependentResetAndInheritance`, and `test/ui/individual_transforms.json`. Math, animation and SVG descendant consumers remain open. |
| Filter functions | ◐ | `blur()`, `brightness()`, `contrast()`, `grayscale()`, `invert()`, `opacity()`, `saturate()` and `sepia()` work. `hue-rotate()` accepts all CSS angle units; `drop-shadow()` paints named, functional and `currentColor` values (`RenderOutputParity.DropShadowUsesNamedAndCurrentColor`). `url()` filters are not supported. |
| Easing | ◐ | `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier()`, `step-start`, `step-end`. `steps()` samples start/end/both/none jumps with the specified input intervals and endpoint jumps (`TimingFunctionTest.StepsEndpointModesKeepInputIntervals`); phase-boundary before flags are also checked in `test/ui/css_animation_shorthand.json`. `linear()` with stops is unsupported. |
| Grid functions | ◐ | `repeat()` with `auto-fill`/`auto-fit`, `minmax()`, `fit-content()`, named lines, `grid-template-areas`. Fixed `repeat()` now expands a space-separated track list supplied literally or through `var()` (`RenderOutputParity.GridRepeatExpandsLiteralAndVariableTrackLists`). Font-relative and other supported length units resolve to CSS pixels, including within fixed `repeat()` and `minmax()`; pure-length `calc()` tracks also reach layout (`RenderOutputParity.GridTracksResolveFontUnitsAndLengthCalc`). Mixed percentage/length `calc()`, `subgrid` and `masonry` are not supported. |
| Basic shapes for `clip-path` | ◐ | `inset()`, `circle()`, `ellipse()`, `polygon()` and the supported `path()` commands clip in raster, SVG and PDF output. Shape lengths support `px` and `%` only (`circle(2em)` is treated as 2 px). `rect()`, `xywh()`, `shape()`, `url(#svg-clip)` and geometry boxes are not supported. |

## 7. CSS Property Coverage

At the 2026-09-28 assessment, the parser's property table (`lambda/input/css/css_properties.cpp`) knew **342 properties**. The historical classification was **244 with an effect** (196 longhands and 48 shorthands), **43 working for some values**, and **55 parsed only**, with no effect. These historical totals have not been replaced by a full recount after the focused fixes through 2026-10-06. A property the table does not know is dropped silently. A vendor-prefixed name such as `-webkit-transform` or `-moz-border-radius` maps to the standard property when that property is known. Shorthands marked `*` are resolved directly rather than expanded into longhands at parse time.

| Area | Applied | Partial (what is missing) | Parsed only (no effect) |
|---|---|---|---|
| Box model | `display`, `box-sizing`, `margin`\*, `padding`\* and their physical and logical (`-block`, `-inline`, `-start`, `-end`) longhands | | `margin-trim` |
| Sizing | `width`, `height`, `min-`/`max-` of both, `block-size`, `inline-size` and their `min-`/`max-` forms | | |
| Positioning | `position`, `top`, `right`, `bottom`, `left`, `inset`\*, `inset-block`\*, `inset-inline`\* and their longhands, `z-index` | | |
| Floats | | `float`, `clear` (physical values and horizontal `inline-start`/`inline-end`; vertical logical float placement remains open) | `float-reference`, `float-defer`, `float-offset`, `wrap-flow`, `wrap-through` |
| Overflow and scrolling | `overflow`\*, `overflow-x`, `overflow-y`, `scrollbar-gutter` | `overflow-block`, `overflow-inline` (logical axis values reach raster clipping and compete with physical properties and the shorthand; computed-style serialization and axis-specific clipping need more work); `overscroll-behavior` and physical `-x`/`-y` (wheel chaining obeys `contain`/`none` and later longhands in nested panes; keyboard/touch paths and CSSOM reads during dirty host callbacks need an audit); `scroll-margin`, `scroll-padding` and their physical/logical longhands (`scrollIntoView()` uses lengths, percentages and mixed math in nested panes and the viewport; CSSOM computed serialization and other scrolling operations need work); `overflow-clip-margin` (raster clip geometry handles visual boxes and signed offsets, with scroll-container clamping; rounded corners, vector/PDF output and CSSOM serialization need verification); `scroll-snap-type`, `scroll-snap-align` (mandatory/proximity positions, paired-axis selection, oversized-area interior scrolling and mixed scroll-padding math apply to live scrolling; paired-axis oversized areas, relayout resnapping and CSSOM reads during dirty host callbacks remain open); `scroll-behavior` (`auto`/`smooth` affects CSSOM element writes and `scrollIntoView()` in panes and the viewport; anchor navigation follows root smooth scrolling; other scroll producers need verification) | |
| Flexbox | `flex`\*, `flex-flow`\*, `flex-direction`, `flex-wrap`, `flex-grow`, `flex-shrink`, `flex-basis`, `order` | | |
| Grid | `grid`\*, `grid-template`\*, `grid-template-areas`, `grid-area`\*, `grid-row`\*, `grid-column`\* and their start/end longhands, `grid-gap`\*, `grid-row-gap`, `grid-column-gap`, `grid-auto-flow` (`row`/`column` and `dense` backfill, including both axes; `RenderOutputParity.GridAutoFlowDenseBackfillsEarlierHole`) | `grid-template-columns`, `grid-template-rows`, `grid-auto-rows`, `grid-auto-columns` (supported non-`px` length units and pure-length `calc()` now resolve; mixed percentage/length `calc()` and other value forms remain partial) | |
| Box alignment | `justify-content`, `justify-items`, `justify-self`, `align-content`, `align-items`, `align-self`, `place-content`\*, `place-items`\*, `place-self`\*, `gap`\*, `row-gap`, `column-gap` | | |
| Tables | `border-collapse`, `border-spacing`, `caption-side`, `empty-cells`, `table-layout` | | |
| Multi-column | `columns`\*, `column-count`, `column-width`, `column-fill`, `column-span`, `column-rule`\*, `column-rule-width`, `column-rule-style`, `column-rule-color`, `column-height`, `column-wrap` | | |
| Fragmentation | `box-decoration-break` | `break-before`, `break-after`, `break-inside`, `page-break-*`, `orphans`, `widows` (inside multi-column only; no pagination) | |
| Lists and generated content | `list-style`\*, `list-style-type`, `list-style-position`, `list-style-image`, `counter-reset`, `counter-increment`, `counter-set`, `content`, `quotes` | | `marker-offset` |
| Text | `white-space`, `text-wrap`\*, `text-wrap-mode`, `word-break`, `line-break`, `overflow-wrap`, `word-wrap`, `hyphens`, `hyphenate-character`, `text-align`, `text-align-all`, `text-align-last`, `letter-spacing`, `word-spacing`, `text-transform`, `text-decoration-style`, `text-decoration-color`, `text-overflow`, `line-height`, `vertical-align`, `text-spacing-trim`, `text-autospace`, `initial-letter`, `text-box`\*, `text-box-trim`, `text-box-edge`, `line-clamp`, `-webkit-line-clamp`, `dominant-baseline`, `baseline-source`, `ruby-position`, `text-shadow`, `tab-size`, `text-indent`, `text-decoration-line`, `text-decoration-thickness` | `text-decoration` (multiple line, color, and thickness components paint; skip controls remain incomplete), `text-underline-offset` (lengths and inherited percentages paint in raster and serialize in SVG; PDF remains unverified), `text-underline-position` (`under` uses font descent in horizontal raster, `auto`/`from-font` use the font metric, and SVG serializes the value; vertical placement remains open), `text-decoration-skip-ink` (`auto` and `all` skip solid decoration strokes at glyph ink in raster; `none` paints continuously; SVG serializes the value), `text-wrap-style` (`balance` only), `text-justify` (`none` prevents paint-time spacing, `inter-word` distributes only at spaces, and raster/SVG `auto` expands CJK gaps; PDF CJK and general `inter-character` remain incomplete), `text-emphasis`, `text-emphasis-style`, `text-emphasis-color`, `text-emphasis-position` (marks paint in horizontal raster/SVG; vertical and PDF remain open) | `hanging-punctuation`, `alignment-baseline`, `baseline-shift`, `ruby-align` |
| Fonts | `font`\*, `font-family`, `font-size`, `font-weight`, `font-style`, `font-kerning` | `font-variant` (`small-caps` only) | `font-stretch`, `font-size-adjust`, `font-variant-ligatures`, `-caps`, `-numeric`, `-alternates`, `-east-asian`, `font-feature-settings`, `font-variation-settings`, `font-language-override`, `font-optical-sizing`, `font-display` |
| Writing modes | `writing-mode`, `direction`, `unicode-bidi`, `text-combine-upright` | `text-orientation` (no `sideways`) | |
| Colour and background | `color`, `background-color`, `background-position`, `-position-x`, `-position-y`, `background-size`, `background-repeat`, `background-origin`, `background-clip` | `background`\* (a single `url()` image now paints in raster and SVG; multilayer images and broader shorthand components remain partial), `background-image` (one image layer; no `image-set()`/`cross-fade()`), `background-blend-mode` (image over the background colour only), `background-attachment` (`fixed` and `local` position single raster image/gradient layers to the viewport and scrollable content respectively; vector and multilayer output remain open) | |
| Borders and outline | `border`\* and physical and logical side/axis width, style and colour properties, including one/two-value `border-inline-width`/`-style`/`-color` and `border-block-style` (`RenderOutputParity.LogicalBorderPairsMapByDirectionAndWritingMode`), `border-radius`\* and physical/logical corner radii (`RenderOutputParity.LogicalCornerRadiiMapAndCompeteWithPhysicalCorners`), `outline`\*, `outline-width`, `outline-style`, `outline-color`, `outline-offset` | `border-image`\* and its source, slice, width, outset and repeat longhands paint linear gradients and raster URL sources through the nine-slice raster path; vector output and other image functions remain partial. Logical corner radii do not yet support `calc()`. | |
| Effects | `box-shadow`, `opacity`, `visibility`, `mix-blend-mode` | `filter`, `backdrop-filter` (see [§6.5](#65-transform-filter-easing-and-shape-functions)), `clip-path` (basic shapes, raster output only), `mask-image` (a radial gradient becomes a hard circular clip; no `url()` or linear masks) | `isolation`, `clip`, `mask-type` |
| Transforms | `transform`, `transform-origin` (including unitless-zero length components), `transform-style`, `perspective`, `perspective-origin` | `translate`, `rotate`, `scale` (literal components affect HTML paint, hit testing, computed style and containing blocks; math, animation and SVG descendant consumers remain open); `backface-visibility` (HTML face culling, raster/SVG output, live hit testing and restyle; complete 3D stacking, grouping and SVG descendant scenes remain open) | |
| Transitions and animations | `transition`\* and its longhands, `animation-name`, `-duration`, `-delay`, `-iteration-count`, `-direction`, `-fill-mode`, `-play-state`, `-timing-function` | `animation`\* (eight CSS Animations 1 components, comma lists, variables and live restyling; calculated times and `linear()` remain unsupported) | |
| UI | `cursor`, `pointer-events`, `user-select`, `accent-color`, `caret-color`, `caret-shape`, `field-sizing` | `appearance` (`none` suppresses native checkbox, radio, range, button, text-control and select chrome in raster; `base-select` has a select consumer; `base` and full compatibility behavior remain open) | `resize`, `nav-index`, `nav-up`, `nav-right`, `nav-down`, `nav-left` |
| Replaced elements | `object-fit`, `object-position`, `object-view-box`, `image-orientation`, `aspect-ratio` | `image-rendering` (`crisp-edges`/`optimizeSpeed` select nearest sampling; `pixelated` scales to the closest integer multiple with nearest sampling, then smooths to the raster target; `<img>` and background images, including inherited values, are tested by `RenderOutputParity.ImageRenderingCrispEdgesUsesSourcePixels` and `ImageRenderingPixelatedBlendsAtNonintegerScale`. Transformed/display-list images and other image consumers remain partial.) | |
| Containment | `contain-intrinsic-size`\* and its longhands | `contain` (size containment only), `container-type`, `content-visibility` (`hidden` only) | `container`\*, `container-name` |
| SVG | `fill`, `stroke`, `stroke-width` (on the root `<svg>` only; see [§15](#15-svg)) | | |
| Other | `zoom` | `all` (tested `initial`, `unset`, `inherit`, `revert` and `revert-layer` behavior; full property coverage remains open) | |

**Not recognized at all** (dropped): `will-change`; `touch-action`; the `mask` shorthand and the other `mask-*` properties; `shape-outside` and the other `shape-*` properties; `offset-*`; `color-scheme`; `font-synthesis`; `font-palette`; anchor positioning; scroll-driven animation timelines; `transition-behavior`; and SVG properties other than `fill`, `stroke` and `stroke-width` (`stroke-dasharray`, `fill-opacity`, … are read only as attributes or from a `<style>` inside the SVG).

## 8. Layout Modes

Every mode below lays out the same tree, and all positions and sizes are fractional CSS pixels. Flex, grid and table layout share a measure-then-place engine for intrinsic (min-content, max-content, fit-content) sizes. The design is in [RAD_03](dev/radiant/RAD_03_Layout_Driver_Block_BFC.md) through [RAD_11](dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md).

| Mode | Status | Notes |
|---|---|---|
| Block flow | ✅ | Margin collapsing (siblings, parent and child, empty blocks), block formatting contexts (`display: flow-root`, `overflow`, floats, inline-blocks), `auto` margins for centering. |
| Inline formatting | ✅ | Line boxes, `vertical-align`, `line-height`, `inline-block`, `inline-flex`, `inline-grid`, replaced elements on the line; text details in [§11](#11-text-and-fonts). |
| `display` values | ✅ | `block`, `inline`, `inline-block`, `flex`, `inline-flex`, `grid`, `inline-grid`, `flow-root`, `list-item`, `contents`, `none`, the `table-*` family and `ruby` (annotation placed above the base) were checked. |
| Flexbox | ◐ | Direction, wrapping, `gap`, `flex-grow`/`-shrink`/`-basis`, `order`, `justify-content` (including `space-evenly`), `align-items`/`-self`/`-content`, auto margins and baseline alignment. `align-items: last baseline` is not fully spec-correct, and flex containers nested more than 16 deep are not laid out further ([RAD_08 §9](dev/radiant/RAD_08_Flexbox_Layout.md)). |
| Grid | ◐ | Track lists with `px`, `%`, `fr`, `auto`, `min-content`, `max-content`, `minmax()` and `fit-content()`; `repeat()` with `auto-fill` and `auto-fit`; named lines and areas; line-number placement and spans; implicit tracks; gaps; box alignment. Font-relative track lengths and pure-length `calc()` now resolve through the common unit resolver, including in fixed `repeat()`, `minmax()` and implicit rows (`RenderOutputParity.GridTracksResolveFontUnitsAndLengthCalc`); `grid-auto-flow: dense` backfills earlier holes (`RenderOutputParity.GridAutoFlowDenseBackfillsEarlierHole`). **Gaps:** mixed percentage/length `calc()` needs the grid track's reference box; a grid holds at most 64 tracks per axis, where later columns collapse onto the 64th line, and 256 items, where later items are not placed (both verified). Named-line resolution is simplified ([RAD_09 §8](dev/radiant/RAD_09_Grid_Layout.md)). |
| Tables | ✅ | Automatic and fixed layout (CSS 2.1 §17), `border-collapse` with conflict resolution, `border-spacing`, row and column spans, captions, row groups, anonymous table boxes, `display: table*` on any element. |
| Floats and `clear` | ◐ | `left`, `right`, `none` and `both`, line shortening around floats and clearance work. Horizontal `inline-start`/`inline-end` use the containing block's direction; vertical logical float placement remains open. `shape-outside` is not applied: text wraps around the float's margin box (verified). |
| Positioning | ◐ | `relative`, `absolute` and `sticky` (re-resolved as the page scrolls), `inset` and its longhands, `z-index`. `fixed` boxes use the initial containing block (or a transformed or contained ancestor); RAD_11 lists that they scroll with the page in `lambda view` instead of staying pinned, which was not verified here. |
| Multi-column | ◐ | `column-count`, `column-width`, `columns`, `column-gap`, `column-rule`, `column-fill`, balancing, `column-span: all`, and `break-before`/`-after`/`-inside`, `orphans` and `widows` inside columns. Named, `rgb()` and default `currentColor` column rules paint in SVG. Fragmentation is simplified ([RAD_11 §7](dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md)); the multi-column WPT suite records 144 full and 218 partial passes of 362. |
| Lists and counters | ◐ | Markers inside and outside, `list-style-type` (decimal, roman, alpha and bullet styles checked), `list-style-image`, `<ol start>`, `<ol reversed>`, `<li value>`, `counter-reset`/`-increment`/`-set`, `counter()` and `counters()` with a style argument. `::marker` styling is partial (see [§3](#3-css-selectors)); `@counter-style` is not supported. |
| Writing modes and bidi | ◐ | `writing-mode: vertical-rl` and `vertical-lr` swap the block and inline axes, so box geometry is correct, but glyphs are painted horizontally. `direction` and `unicode-bidi` apply; reordering of mixed-direction text needs the optional FriBidi library. Details in [§11](#11-text-and-fonts). |

## 9. Box Model and Sizing

| Feature | Status | Notes |
|---|---|---|
| `margin`, `padding`, `border-width`, percentages against the containing block's width | ✅ | Checked: `padding-top: 10%` resolves against the width. Spacing shorthands reject unsupported values before cascade; in quirks-mode HTML, unitless numbers in `margin`/`padding` are parsed as px, while standards-mode declarations reject them. |
| `box-sizing: content-box \| border-box` | ✅ | |
| `width`, `height`, `min-*`, `max-*` | ◐ | Lengths, percentages, `auto`, `min-content`, `max-content`, `fit-content`, `fit-content()` and `stretch`. `initial`/`unset` now give `auto` for width and height; other sizing cases need broader checks ([§5](#5-cascade-inheritance-and-custom-properties)). |
| `aspect-ratio` | ✅ | Checked: `160px` wide at `16/9` gives 90 px. |
| Margin collapsing | ✅ | |
| `overflow: visible \| hidden \| clip \| scroll \| auto` | ✅ | Establishes a block formatting context, clips painting (including rounded corners), and creates a scroll container in `lambda view`. |
| Logical properties (`margin-inline`, `inset-block`, …) | ◐ | Margins, paddings, insets, `inline-size`/`block-size`, logical border sides/axis pairs and the four logical corner radii map by `direction` and `writing-mode` (checked: `padding-inline-end` lands on the left under `rtl`, `inline-size` becomes the height under `vertical-rl`, and logical border colors/widths and radii map to physical edges/corners). Logical corner `calc()` values remain unsupported ([§7](#7-css-property-coverage)). |

## 10. Backgrounds and Borders

Status is for the raster painter (PNG, JPEG and the `lambda view` window); SVG and PDF output differ in places, listed in [§17](#17-output-targets).

| Feature | Status | Notes |
|---|---|---|
| `background-color` | ✅ | |
| `background-image: url()`, one layer | ✅ | Local files, relative paths and `data:` URIs, with `background-repeat`, `background-position`, `background-size` (lengths, `cover`, `contain`) and `background-origin`. |
| Several background layers | ◐ | Gradient layers stack, but an image `url()` inside a multi-layer list is not painted (checked with two images, and with an image over a gradient). |
| `background` shorthand with an image | ◐ | A single relative `url()` with `no-repeat` now paints in raster and SVG (`RenderOutputParity.BackgroundShorthandUrlPaintsImage`). Multilayer URL images and combined size/position grammar still need broader checks. |
| `linear-gradient()`, `radial-gradient()`, `conic-gradient()`, `repeating-*` | ✅ | SVG and PDF output drop conic gradients. |
| `background-clip`, including `text` | ✅ | SVG output renders `background-clip: text` as invisible text. |
| `background-blend-mode` | ❌ | `multiply` over two gradient layers did not blend in a spot check. |
| `background-attachment` | ◐ | `scroll`, `fixed` and `local` validate before cascade. Raster `fixed` uses the viewport positioning area while keeping the element's background clip; `local` sizes the positioning area to scrollable overflow and follows its live scroll position. Both work through the longhand and a single-layer `background` shorthand. `inherit` copies the parent's computed attachment. `RenderOutputParity.FixedBackgroundAttachmentUsesViewportPosition`, `LocalBackgroundAttachmentSizesToScrollableContent` and `LocalBackgroundAttachmentMovesWithScrollContent` compare pixels. Multilayer backgrounds, SVG/PDF and transformed containing blocks remain open. The computed state follows **D4.5.1v4**. |
| `border-style` | ✅ | `solid`, `dashed`, `dotted`, `double`, `groove`, `ridge`, `inset`, `outset`, mixed per side. SVG and PDF output draw `dashed` and `dotted` borders as solid. |
| `border-radius` | ✅ | Per-corner and elliptical radii; backgrounds, borders and overflow clipping follow the curve. |
| `border-image` | ◐ | Linear-gradient and raster `url()` sources paint from longhands or the shorthand with nine-slice source cuts, one-to-four slice/width/outset values, `fill`, and two-axis `stretch`/`repeat`/`round`/`space` tiling. The shorthand competes with explicit longhands by cascade order (`RenderOutputParity.BorderImageShorthandCompetesWithLonghandsInCascade`). The `enhance5_spec_border_image_gradient_01` browser reference matches exactly; focused raster cases check used areas, repeat tiles, URL crops, intrinsic `auto` width and center fill. HTML SVG/PDF output currently drops these border-image slices; SVG URL/vector sources and other image functions are still unsupported. |
| `box-shadow` | ✅ | Outer and `inset`, spread, blur, several shadows, rounded corners. SVG and PDF output embed it as a raster image. |
| `outline`, `outline-offset` | ✅ | SVG output approximates `dotted` with dashes. |

## 11. Text and Fonts

Radiant uses Lambda's own font engine (`lib/font/`) for font loading, glyph metrics, kerning, fallback and rasterization, with the platform font service underneath: CoreText on macOS, fontconfig on Linux, DirectWrite on Windows. **Text shaping is the largest gap:** there is no HarfBuzz or equivalent shaper, so glyphs are placed one per character. Latin, Greek, Cyrillic and CJK text lay out well; Arabic, Indic and other complex scripts do not. See [RAD_06](dev/radiant/RAD_06_Inline_and_Text_Layout.md) and [RAD_07](dev/radiant/RAD_07_Fonts.md).

### 11.1 Fonts

| Feature | Status | Notes |
|---|---|---|
| TrueType, OpenType, collections (`.ttc`) | ✅ | CFF-flavoured OpenType outside macOS was not verified. |
| WOFF, WOFF2 | ✅ | Checked: a WOFF2 face loaded through `@font-face` changes the measured text. |
| `@font-face` | ◐ | `url()`, `local()`, `format()` and `unicode-range` (per glyph). Remote fonts are fetched asynchronously by the network loader, so they may not be ready for a one-shot `layout` or `render` (not verified). `font-display` and weight ranges are ignored; see [§4](#4-at-rules). |
| System fonts and fallback | ✅ | Per-character fallback through the platform font service; `serif`, `sans-serif` and `monospace` were checked. HTML UA `pre` and relative heading code now preserve Chromium's 13/16 fixed-font scaling (`RenderOutputParity.UaMonospaceKeepsRelativeHeadingAndPreSizes`). The generic families `emoji`, `math` and `fangsong` are not mapped. |
| Kerning | ✅ | Pair kerning from `GPOS`/`kern`; `font-kerning` is honoured. |
| Ligatures, OpenType features | ❌ | No `GSUB`: no ligatures, and `font-feature-settings` and `font-variant-*` are parsed only. `font-variant: small-caps` is synthesized. |
| Variable fonts | ◐ | Only the `wght` axis, driven by `font-weight`, on macOS. `font-variation-settings` is parsed only. |
| Synthetic bold and italic | ◐ | Synthetic bold on Linux and Windows, synthetic italic on macOS. The `font-synthesis` property is not recognized. |
| Colour fonts | ◐ | macOS draws colour glyphs through CoreText, so Apple Color Emoji works. Linux and Windows support `COLR` v0 and `CBDT`/`CBLC` only: no `COLR` v1, `sbix` or SVG-in-OpenType. |
| Emoji | ◐ | Colour emoji and the VS16 presentation selector work. ZWJ sequences are drawn as their separate glyphs, and skin-tone modifiers are dropped. |
| `font-stretch`, `font-size-adjust`, `font-optical-sizing` | ❌ | Parsed only. |

### 11.2 Text layout

| Feature | Status | Notes |
|---|---|---|
| `white-space` (all values, including `break-spaces`), `text-wrap` (`wrap`, `nowrap`, `balance`) | ✅ | WPT white-space: 223 of 291 tests match fully. |
| Line breaking | ◐ | A subset of the Unicode line-breaking rules (UAX #14) covering ideographs, small kana, break-after and glue classes, and ZWJ; `word-break`, `overflow-wrap` and `line-break` values. CJK wraps correctly (the `pretext` suite). No dictionary-based breaking for Thai, Lao, Khmer or Myanmar. |
| Hyphenation | ◐ | `hyphens: auto` for English only (`lang="en"`, `en-US`). Soft hyphens and `hyphenate-character` work. |
| `text-align` (including `justify`), `text-align-all`, `text-align-last`, `letter-spacing`, `word-spacing`, `text-transform` | ✅ | `text-align-all` wins against the shorthand by cascade priority; `text-align-last` inherits and can override the shorthand's last-line reset. `text-transform: full-width` is not supported. |
| `text-indent` | ✅ | Lengths, percentages, `calc()`, `hanging` and `each-line`; the modifier WPT browser comparison matches all boxes and text runs. |
| `line-height`, `vertical-align` | ✅ | |
| `text-decoration` | ◐ | Multiple line keywords paint in raster and serialize in SVG; colour, style and length/percentage/`from-font` thickness forms are accepted. `text-underline-offset` moves raster underlines and serializes in SVG. `text-underline-position` accepts `auto`, `from-font`, `under`, `left`, `right` and valid pairs; `under` moves horizontal raster strokes below the font descent while `auto`/`from-font` use the font underline metric, and SVG serializes non-default values. Vertical side placement remains open. `text-decoration-skip-ink` accepts `auto`, `none`, and `all`: raster solid underlines skip glyph ink by default, `none` paints continuously, and `all` also applies gap detection to overlines; SVG serializes authored `none`/`all`. Non-solid skip-ink strokes and PDF still need verification. `RenderOutputParity.TextDecorationListPaintsBothLines`, `TextUnderlineOffsetMovesRasterLineAndSurvivesSvg`, `TextUnderlinePositionUnderMovesStrokeAndInherits`, and `TextDecorationSkipInkControlsRasterGapsAndSvgStyle` cover these paths. The computed properties follow **D4.5.1v4**. |
| `text-shadow` | ✅ | Paints without an explicit font declaration; verified in SVG output. |
| `text-overflow: ellipsis` | ◐ | The text is clipped, but no "…" is painted (verified). |
| `line-clamp`, `-webkit-line-clamp` | ◐ | Lines are clamped, but the "…" is appended without shortening the last line, so it is clipped away. |
| `text-emphasis` | ◐ | Horizontal text paints half-size marks in raster and SVG. The shorthand and its style/color/position longhands validate values and respect cascade order; spaces and punctuation do not receive marks. `RenderOutputParity.TextEmphasisPaintsMarksAndRespectsLonghandOrder` checks colored dot/circle glyphs and a style reset. Vertical placement, ruby collision, PDF output and effect-group fallback remain open. |
| `tab-size` | ✅ | Non-negative numbers, including fractions, and lengths. Lengths inherit as computed pixels; line layout and intrinsic sizing both use the chosen tab period. |
| Complex scripts | ❌ | Arabic is drawn as isolated letter forms in logical order, and Indic reordering is not done (WPT css-text shaping: 1 of 29 tests match). |
| Bidirectional text | ◐ | `direction` sets alignment and the inline base direction, and every `unicode-bidi` value resolves. Reordering of mixed-direction runs uses FriBidi when the build found it (an optional dependency); otherwise a simplified fallback is used, under which a right-to-left paragraph keeps its runs in left-to-right order. Mirrored brackets are not substituted. The macOS build checked for this document had no FriBidi. |
| `writing-mode: vertical-rl`, `vertical-lr`, `sideways-*` | ◐ | Box geometry is correct, but glyphs are painted horizontally (verified). `text-orientation: sideways` is treated as `mixed`. |
| `text-combine-upright` | ◐ | Affects layout; not verified in paint. |

## 12. Visual Effects

Status is for the raster painter; SVG and PDF output differ as listed in [§17](#17-output-targets). Paint is covered by a pixel-comparison suite against Chrome ([§1](#1-conformance-summary)); the design is in [RAD_13](dev/radiant/RAD_13_Render_Walk_Painters.md).

| Feature | Status | Notes |
|---|---|---|
| 2D transforms, `transform-origin` | ✅ | |
| 3D transforms, `perspective`, `perspective-origin`, `transform-style`, `backface-visibility` | ◐ | 4×4 matrices are projected with perspective; intersecting planes have no depth sorting. Hidden backfaces use the accumulated context's inverse-transpose normal in HTML paint and hit testing. Independent preserved child planes can paint while input excludes their hidden ancestor. Existing opacity, overflow, filter, blend, radial-mask and paint-containment state flattens the used context while CSSOM retains the computed keyword. `RenderOutputParity.BackfacesRespectThreeDimensionalContextsInRasterAndSvg` and `test/ui/backface_visibility.json` check raster/SVG, live changes, input and containing blocks. The six PDF face samples match an untransformed PDF reference after rasterization. Unsupported grouping effects, SVG descendant scenes and broader PDF scenes remain open. The draft and Chrome 154 differ on three additional grouping/containing-block checks; see the [implementation record](../vibe/impl/Radiant_Impl_CSS_Selector_Property_Support.md#p4--make-parsed-only-properties-effective-by-subsystem). State follows **D4.5.1v4**. |
| `opacity` | ✅ | |
| `filter` | ◐ | `blur()`, `brightness()`, `contrast()`, `grayscale()`, `hue-rotate()`, `invert()`, `opacity()`, `saturate()`, `sepia()`, `drop-shadow()`, with the limits in [§6.5](#65-transform-filter-easing-and-shape-functions). The functions run in a fixed order (colour functions, then blur, then drop shadow) rather than in declaration order. `url()` filters are not supported. |
| `backdrop-filter` | ◐ | Same functions as `filter`; implemented for raster output, not spot-checked. SVG and PDF output paint an opaque grey box instead. |
| `mix-blend-mode`, `background-blend-mode` | ◐ | The 11 separable modes (`multiply`, `screen`, `overlay`, `darken`, `lighten`, `color-dodge`, `color-burn`, `hard-light`, `soft-light`, `difference`, `exclusion`). `hue`, `saturation`, `color`, `luminosity` and `plus-lighter` fall back to `normal`. `background-blend-mode` blends a layer with the background colour; two gradient layers did not blend with each other in a spot check. |
| `isolation` | ❌ | Parsed only. |
| `clip-path` | ◐ | Basic shapes and supported `path()` commands in raster, SVG and PDF output ([§6.5](#65-transform-filter-easing-and-shape-functions)); no `url(#svg-clip)` and no geometry boxes. |
| `clip` (legacy `rect()`) | ❌ | Ignored (verified). |
| `mask-image` | ◐ | Only a `radial-gradient()`, approximated as a hard-edged circle. No `url()` or linear-gradient masks, no `mask` shorthand and no `mask-size` or `mask-position`. |
| `box-shadow` | ✅ | See [§10](#10-backgrounds-and-borders). |
| `z-index`, stacking contexts | ◐ | Positive `z-index` works. A child with a negative `z-index` paints above its parent's background when it should paint below it ([RAD_13](dev/radiant/RAD_13_Render_Walk_Painters.md) lists stacking contexts as simplified). |
| `overflow` clipping | ✅ | Including clipping to rounded corners. SVG output does not clip overflow. |
| `visibility: hidden` | ✅ | SVG output paints hidden content. |
| `outline` | ✅ | |

## 13. Transitions and Animations

Animations run on the frame clock of `lambda view` ([RAD_16](dev/radiant/RAD_16_Animation_Frame_Scheduling.md)). Size-affecting samples request reflow before native or headless frames paint (`RenderOutputParity.AnimationFrameTicksReflowAnimatedSizes`, `TransitionRetargetRestartAndCancellationReachPaint`). Static `lambda layout` and `lambda render` sample keyframe animations at time 0, including negative delays, paused effects and fill modes (`RenderOutputParity.AnimationShorthandProjectsCascadeAndVariableValues`, `AnimationNamesRetainCaseAndTimingEndpointsPaint`).

| Feature | Status | Notes |
|---|---|---|
| `@keyframes` | ◐ | Last valid supported longhand wins within a stop; invalid or important declarations leave the earlier endpoint eligible, including composition and timing descriptors. Equal offsets cascade in source order, and CSS easing applies within each property interval. Authored math whitespace survives parsing. Up to 64 stops and 32 supported properties per stop; selector lists and broader grammar remain to audit. |
| `animation-name`, `-duration`, `-timing-function`, `-delay`, `-iteration-count`, `-direction`, `-fill-mode`, `-play-state` | ◐ | Comma lists repeat shorter property lists. Fractional/zero iteration counts, paused sampling and live timing updates are tested. Calculated times, general effect composition and display-none cancellation remain open. |
| `animation` shorthand | ◐ | Validates and projects the eight CSS Animations 1 components through cascade and variable substitution. Multiple names, longhand priority, quoted/case-sensitive names and live restyling are tested. Calculated times and `linear()` remain unsupported. |
| Animatable properties | ◐ | Sampling writes all 32 registry properties: `opacity`, `transform`, text/background colors, six sizing longhands, `aspect-ratio`, discrete `display`, and the four physical sides of border colors/widths, insets, margins and padding. Side geometry, flow neighbors and colors have static/live render coverage; HTML default margins resolve before sampling. CSS and explicit Web effects preserve important longhand/shorthand/logical/all winners through live restyling; transitions retain higher priority. Keyframe lengths, deferred opacity/colors and context-dependent transforms use shared typed resolvers with per-element caches refreshed after style/layout changes. Invalid substitution defaults through inheritance or the initial value; numeric/keyword pairs sample discretely. Length math and easing results clamp after interpolation while signed margins/insets remain legal. Opacity percentages and numeric/percentage math use the same resolver as ordinary declarations and CSSOM; opacity endpoints clamp before interpolation. `em`, `rem`, physical units, percentages, mixed `calc()`, variables and Web Animation seeks have paint coverage. Missing property stops are ignored; neutral/default endpoints and interior stops interpolate through captured underlying values. Compatible transform functions pad with identity, including `none`; translations preserve pixel and reference-box percentage components. Scale/angle math, 3D translation/scale, named/arbitrary-axis rotation, common primitive conversion and planar/spatial matrix sampling have paint coverage. Normalized equal rotation axes preserve numeric turns; different axes use quaternion interpolation. Matching matrix/perspective pairs interpolate locally before later functions. Perspective uses reciprocal-distance interpolation, accepts `none`, and clamps subpixel endpoint distances to 1 px. Compatible rotation prefixes retain their turns before matrix fallback, and damage bounds use the shared visual geometry. Translation calculations retain absolute lengths and unresolved percentages until the sized reference box is available, including nonlinear math and percentage-dependent planar/spatial matrix suffixes; resizing and simultaneous width animation are tested. Sampled trees are reclaimed between ticks. Length composition runs before interpolation. Legacy color interpolation premultiplies alpha. Standalone CSSOM opacity/color reads sample effects before a geometry read; transform reads synchronize geometry and serialize the sampled `matrix()`/`matrix3d()` without transform-origin; the transform fixtures also check transformed client rectangles. Variable/modern RGB colors and authored `currentColor` have paint/CSSOM coverage; explicit Web seeks refresh opacity/color after variable changes and transforms after width/font/variable changes. Intrinsic functions, broader color values and complete color grammar, broader transform expression domains, planar shear/reflection interoperability, near-identity 2D/3D serialization, broader matrix conditioning, projective export, live Web underlying refresh, host-handler computed sampling and wider composition remain open. `filter`, `box-shadow` and `font-size` do not interpolate. |
| Web Animations keyframes | ◐ | `Element.animate()` and `KeyframeEffect` accept multi-property sequence frames and property-indexed scalar/iterable lists for the 32 sampling properties. Numeric values convert to CSS strings. Authored/missing offsets, per-frame easing, enumerable getter/iterator order, validation and thrown-value identity have Chromium-backed consumer tests; an 80-frame sequence verifies dynamic growth. `KeyframeEffect` snapshots keyframes and supported duration/easing at construction. Multi-property sizing, translation, opacity pixels and explicit seeks are tested in all entry forms. Shorthand/logical/custom properties, timeline offsets, default composition, complete timing/playback/fill APIs and live underlying refresh remain open. |
| Timing functions | ◐ | See [§6.5](#65-transform-filter-easing-and-shape-functions). |
| Transitions | ◐ | Wired for 30 properties: `opacity`, text/background colors, six sizing longhands, `aspect-ratio`, and all four physical border colors/widths, insets, margins and paddings. Snapshot tracks grow with the property set; 20 concurrent side effects and retargeting after growth are tested. Lists preserve all names, repeat shorter timing lists and use the last matching entry, including `all`. Shorthand/longhand cascade, variables, `none`, computed lists, live geometry/paint and completed-effect release are tested. Transform/discrete targets, calculated times and faster reversal remain open. Of the 10 WPT transition-event tests that are run, 1 passes. |
| Events | ◐ | `animationstart`, `animationiteration`, `animationend`, `animationcancel`, `transitionend`, `transitioncancel`. CSS/SVG timing delivery is queued outside layout through traced timer arguments under **D5.3.3** and **D4.5.1v4**. Delayed/negative-delay/zero-duration starts, iteration/end elapsed time, cancellation and a layout-changing start handler pass 17 live assertions in `test/ui/css_animation_events.json`. Complete phase changes, `transitionrun` and `transitionstart` remain open. |
| SVG SMIL animation | ◐ | Live document-time animation and timeline controls; see [§15](#15-svg) for the supported classes and timing limits. |
| Animated GIF, Lottie | ◐ | Play in `lambda view`; static output shows the first frame (GIF) or an empty box (Lottie). |

Opacity math accepts numbers or percentages; adding them together is invalid under [CSS Values 4 §5.6](https://www.w3.org/TR/css-values-4/#mixed-percentages). Out-of-range opacity keyframes follow computed-value clamping in [CSS Color 4 §3.3](https://www.w3.org/TR/css-color-4/#transparency); Chromium currently clamps those keyframe samples differently ([CSSWG issue 3340](https://github.com/w3c/csswg-drafts/issues/3340)). Chromium also switches the tested percentage-math keyframes discretely while Radiant interpolates their computed numbers. These differences remain recorded separately from the six passing original scalar/color browser comparisons.

## 14. Interaction

`lambda view` is a browsing shell around the same engine: platform input goes through one event funnel, interaction state (hover, focus, selection, scroll) lives on the document, and page scripts and the `dom` package respond to events. Everything in this section applies to `lambda view` and `lambda edit` only. Design: [RAD_15 — Events](dev/radiant/RAD_15_Events_Input.md), [RAD_17 — Interaction State](dev/radiant/RAD_17_Interaction_State.md), [RAD_18 — Editing](dev/radiant/RAD_18_Editing_Selection_Ranges.md), [RAD_20 — Application Shell](dev/radiant/RAD_20_Application_Shell_Browsing.md).

| Feature | Status | Notes |
|---|---|---|
| Documents and navigation | ◐ | Local files in every supported format and http(s) URLs; links; back and forward through a history of up to 100 entries; fragment navigation, which drives `:target`. One document per window, with no tabs. Top-level SVG and image documents have no zoom or pan. |
| Mouse, keyboard, wheel, drag and drop, context menu | ✅ | The context menu has five fixed items. |
| Focus | ✅ | Tab order follows `tabindex`, plus `autofocus`; `:focus-visible` follows keyboard focus. Focus behaviour is written in Lambda in the `dom` package ([Lambda_Packages.md §10.3](Lambda_Packages.md#103-dom--browser-behaviour-for-html)). |
| Interaction pseudo-classes | ✅ | `:hover`, `:active`, `:focus`, `:focus-within`, `:focus-visible`, `:target`, `:checked`, `:valid`/`:invalid` and `:placeholder-shown` restyle live; see [§3](#3-css-selectors). |
| Scrolling | ◐ | Mouse wheel and scrollbar dragging, overlay scrollbars (shown on hover or drag for `overflow: auto`, always for `scroll`), `scrollbar-gutter`, and sticky positioning that follows the scroll. `scroll-snap-type`/`scroll-snap-align` affect wheel and programmatic scroll positions in live view; `overscroll-behavior` controls wheel chaining. `scroll-behavior: smooth` animates CSSOM scroll writes and `scrollIntoView()` in live panes and the viewport; anchor navigation also follows the root smooth scrolling value. RAD_11 lists `position: fixed` boxes as scrolling with the page instead of staying pinned (not verified here). |
| `contenteditable` (rich and `plaintext-only`) | ✅ | Radiant fires `beforeinput` and the `dom` package performs the edit, per ruling D7.2.5 in [Lambda_Formal_Design.md](Lambda_Formal_Design.md): editing policy belongs to the Lambda DOM behaviour package, not to native code. 50+ `inputType`s; WPT input-events: 20 files pass. |
| Form text controls | ✅ | Native caret, selection, undo and IME; see [§2](#2-html). |
| Text selection, `Selection` and `Range` | ◐ | Implemented ([RAD_18](dev/radiant/RAD_18_Editing_Selection_Ranges.md)); word and sentence movement in `Selection.modify()` is approximate. Not exercised for this document. |
| Clipboard | ◐ | An in-memory clipboard plus plain text through the OS; no rich (HTML) OS clipboard. |
| IME | ◐ | macOS and Windows; no reconversion and no clause underlines. |
| `cursor` | ◐ | Text, pointer, column/row resize and default arrow shapes are shown; other shapes and image cursors remain unsupported. |
| `lambda edit` | ✅ | Rich-text editing for Markdown and HTML and a drawing editor for SVG, built as Lambda packages on the DOM layer; see [Lambda_Doc_Pipeline.md §3.6](Lambda_Doc_Pipeline.md#36-edit). |

## 15. SVG

Radiant parses and paints SVG itself, whichever way it arrives: inline `<svg>` in HTML, a standalone `.svg` file given to `lambda render` or `lambda view`, `<img src="x.svg">`, CSS `url(x.svg)`, `data:` SVG URIs, SVG `<image href="x.svg">`, and external `<use href="f.svg#id">`. ThorVG is the rasterizer underneath; its own SVG loader is not used. Design: [RAD_14 — SVG, Vector Graphics & Diagram Layout](dev/radiant/RAD_14_SVG_Vector_Graph.md). SVG *output* is covered in [§17](#17-output-targets).

**2026-10-03 closeout:** the listed geometry, cascade, text, paint-server,
clip/mask, stroke/marker, filter and embedded-HTML work is implemented. P12 also
implements access-key/wallclock timing, private local/external/nested use event
state and animation DOM queries/TimeEvents. The
[closeout record](../vibe/impl/Lambda_Impl_SVG_Support.md#716-p12-remaining-timing-instance-dom-and-final-closeout)
separates the advertised inventory, browser differences and outstanding
Linux/Windows P13 runtime smoke. Retained source/sample ownership follows
**D4.2.6/D4.5.1v4**, “pin, gen-check, copy-as-value.”

### 15.1 Elements

| Element | Status | Notes |
|---|---|---|
| `<svg>`, nested `<svg>` | ✅ | `viewBox` and `preserveAspectRatio` (alignment, `meet`, `slice`, `none`). Default size 300 × 150; with only a `viewBox` it fills the container width at the viewBox ratio. Root and nested overflow follow authored CSS/presentation values; visible ink can extend beyond the viewport, while hidden overflow clips in viewport space. |
| `<g>` | ✅ | Group opacity composites overlapping shapes once, with or without a root `viewBox`, including nested groups and a nonzero viewBox origin. |
| `<path>` | ✅ | All commands (`M L H V C S Q T A Z`, absolute and relative), arcs included. Invalid data retains complete preceding segments; incomplete parameter sets are not emitted. |
| `<rect>` | ✅ | Either omitted radius uses the other radius; used radii are clamped to half the corresponding dimension. An explicit zero radius gives square corners. |
| `<circle>`, `<ellipse>`, `<polygon>`, `<polyline>` | ✅ | |
| `<line>` | ✅ | The default stroke is `none`; explicit, inherited and inline CSS strokes use normal paint resolution. |
| `<text>`, `<tspan>` | ◐ | Addressable-character `x`/`y`/`dx`/`dy` lists, repeated `rotate`, `text-anchor`, inherited font family/size/weight/style, fill/stroke and inherited paint opacity, text/tspan compositing, spacing, baseline alignment/shift, decorations, nested `textLength`/`lengthAdjust`, `xml:space`, and character-cell targeting. Glyph outlines and bitmap coverage share paint geometry. Gradient/pattern text paint and complete clip/mask/filter boundaries are implemented; general shaping retains the font engine limitations. Specification fixtures record Chromium 143 differences in supplementary-character lists, nested length calibration and SVG 2 decoration styles/colors. |
| `<textPath>` | ✅ | Local/external paths and SVG2 basic-shape/inline references, calibrated offsets, anchoring, side, align/stretch, closed contours, nested positioning/textLength, decoration and character-cell hits. Specification references identify Chromium gaps and live reference-mutation differences. |
| `<a>` | ✅ | Uses the shared group transform/paint/font state; transformed children retain link click bubbling. |
| `<use>`, `<symbol>`, `<defs>` | ✅ | `href` and `xlink:href`, `x`/`y`, the symbol's `viewBox` and `preserveAspectRatio`, external `file.svg#id`, a cycle guard, nesting up to 16 levels. |
| `<image>` | ✅ | Shared PNG/JPEG/GIF/WebP/SVG and data-URI resources; document-relative URLs, x/y, all aspect alignments, meet/slice/none, transformed clipping and opacity. GIF frames invalidate recorded paint. Referenced SVG images isolate styles and block external file references; standalone SVG has a live document DOM. |
| `<switch>` | ✅ | First eligible child, conditional language/extension checks, no-match behavior and live mutation. |
| `<foreignObject>` | ✅ | Transformed/clipped HTML layout, paint, input and mutation, including prefixed XHTML. SVG images use isolated styles and secure image restrictions. |
| `<style>` inside SVG | ✅ | Uses the shared CSS parser, selector matcher and cascade, including lists, combinators, specificity and `!important`; external image documents retain isolated styles. |
| `<title>`, `<desc>` | ✅ | Not painted, as in browsers; no tooltip. |
| `<linearGradient>`, `<radialGradient>` | ✅ | See the paint-server table below. |
| `<pattern>` | ✅ | Tile/content units, x/y/width/height, affine transforms, viewBox/PAR and local/external href templates. Tiles are clipped and sampled once for fill/stroke; tiny periods have bounded traversal. |
| `<clipPath>` | ✅ | Child contour unions, transforms, object-bounding-box/user units, CSS references, nonzero/even-odd rules and shared clipped pointer geometry. |
| `<mask>` | ✅ | Ordinary shape/text/image/paint-server mask content, alpha/luminance and color-space conversion; shared source capture and final compositing. Missing/circular masks are transparent. |
| `<marker>` | ✅ | Start/mid/end on paths, lines, polylines and polygons, including closed/degenerate segment tangents; orientation, reference points, units, viewBox/aspect fitting, overflow and context paint. |
| `<filter>` | ◐ | Named multi-primitive graphs on shapes, groups, text, images, use and viewports; standard paint/backdrop inputs, primitive/filter regions, bounding-box/user units, color spaces and bounded execution. The 15 implemented primitives are listed below; component transfer and convolution remain unsupported. |
| `feGaussianBlur`, `feDropShadow`, `feOffset`, `feMerge`, `feColorMatrix`, `feFlood` | ✅ | Anisotropic blur and edge extension; complete chains and named intermediate results. |
| `feBlend`, `feComposite`, `feMorphology` | ✅ | 16 blend modes, seven composite operators, arithmetic coefficients and axis-separated erosion/dilation. |
| `feTurbulence`, `feDisplacementMap`, `feImage`, `feTile` | ✅ | Deterministic stitched noise, selected-channel bilinear displacement, local/external images and fragments, and fractional repeat periods. |
| `feDiffuseLighting`, `feSpecularLighting` | ✅ | Distant/point/spot children, Sobel boundaries, cone edges and premultiplied output. |
| `feComponentTransfer`, `feConvolveMatrix` | ❌ | Outside the proposal's F1–F4 primitive inventory; unavailable primitives reject the graph with a diagnostic. |
| SMIL animation (`<animate>`, `<animateTransform>`, `<set>`) | ◐ | Document clocks, pause/seek, begin/end/restart, repeats, freeze/remove, event/syncbase timing, keyTimes/keySplines, discrete/linear/paced/spline, additive/accumulate. Numeric, integer, length/list, color/paint, matching path/point lists, discrete attributes and translate/scale/rotate/skew transforms are implemented on rendered targets. Resource caches follow samples; external use follows its host clock, SVG images run in isolated mode, and exports sample initial time. Access-key/wallclock timing, isolated local/external/nested use events, ID-qualified broadcasts and animation DOM queries/TimeEvents are implemented. Expired intervals before parent zero are excluded. The row remains partial for broader SMIL/SVG animation: motion/discard and general SVG DOM expansion are outside N1; animation does not add unavailable filter/text capabilities. [Class inventory and closeout](../vibe/impl/Lambda_Impl_SVG_Support.md#716-p12-remaining-timing-instance-dom-and-final-closeout). |

### 15.2 Attributes, styling and paint servers

| Feature | Status | Notes |
|---|---|---|
| `transform` attribute | ✅ | Attribute transforms and authored CSS transforms with CSS precedence, transform origins, and view/fill reference boxes; paint and geometric hit testing share decoding. |
| Colours | ✅ | Hex, `rgb()`/`rgba()` including percentages, `hsl()`/`hsla()` with number/degree/radian/gradian/turn hues, and CSS named colours including `rebeccapurple`. SVG uses the shared CSS color parser. |
| `currentColor` | ✅ | Including a CSS `color` set on the `<svg>` element. |
| `context-fill`, `context-stroke` | ✅ | Use and marker instances retain source paint, geometry bounds, coordinates and document/base. No context produces no paint. |
| `fill="url(#gradient)"` | ✅ | Inherited typed paints apply to basic/curved shapes and text using tight geometry bounds; text glyphs share the complete text paint domain. |
| `stroke="url(#gradient)"` | ✅ | Gradient strokes share caps, joins, dashes and paint opacity with solid strokes. |
| `gradientUnits` | ✅ | |
| `gradientTransform`, `spreadMethod`, gradient `href` templates, `fx`/`fy`/`fr` | ✅ | Affine transforms, pad/repeat/reflect, local/external templates with cycles, styled stops and two-circle radial cones. Lambda-side raster lowering covers ThorVG focal-circle limitations. |
| `stop-color`, `stop-opacity` | ✅ | Presentation attributes and authored CSS, including inherited `currentColor` and percentage stop opacity. |
| `fill-rule` | ✅ | |
| `clip-rule` | ✅ | Nonzero/even-odd child contours inside shared clipping. |
| `opacity`, `fill-opacity`, `stroke-opacity` | ✅ | Shapes, containers, text and referenced content share source capture, filter, clip/mask and final opacity ordering. |
| `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `stroke-dashoffset` | ✅ | |
| `stroke-dasharray` | ✅ | Preserves zero-length round-cap dots, repeats odd lists, treats all-zero/invalid negative lists as solid, and resolves percentages against the viewport diagonal. |
| `stroke-miterlimit`, `paint-order` | ✅ | Acute miter cutoffs and every fill/stroke/marker permutation; paint and pointer geometry share stroke facts. |
| `vector-effect` | ◐ | `none` and affine `non-scaling-stroke`. SVG2's at-risk `non-scaling-size`, `non-rotation`, `fixed-position`, combined effects and explicit `viewport`/`screen` selectors remain unsupported ([§8.13](https://www.w3.org/TR/SVG2/coords.html#VectorEffects)). |
| `text-anchor` | ✅ | |
| Presentation attributes vs CSS | ✅ | Shared author cascade, including inline importance and selector-list specificity; presentation attributes have specificity zero. Invalid paint declarations preserve earlier valid declarations. |
| **Page stylesheets reaching SVG content** | ✅ | Host selectors style inline SVG descendants. Host class/style changes and stylesheet text replacement invalidate retained SVG paint; external image documents remain isolated. |
| Font properties on `<g>` | ✅ | Presentation attributes and shared CSS inherit family, size, weight and slant into descendant text. |
| `display="none"` | ✅ | |
| `visibility` | ✅ | Inherited hidden content retains geometry; visible descendants can paint and receive pointer targets. Hidden text retains advances. |
| Units | ◐ | `px`, `pt`, `pc`, `mm`, `cm`, `in`. Shape geometry, transforms and stroke/dash values resolve `em`/`ex` from computed font metrics and `%` from the viewport axis/diagonal. Positioned text and resource-specific consumers use shared viewport/font bases, including declaration-font filter regions. Newer CSS units and general CSS expression limits remain those in §6. |

## 16. Image Formats

The image decoders linked into `lambda` are libpng, libjpeg-turbo, giflib and libwebp (`lib/image.c`); SVG goes through Radiant's own SVG renderer, and Lottie through ThorVG.

| Format | `<img>`, CSS images | Notes |
|---|---|---|
| PNG | ✅ | Including `data:` URIs. |
| JPEG | ✅ | EXIF orientation is ◐: for orientations 5–8 the box is swapped but the pixels are not rotated, so the image is stretched. |
| GIF | ✅ | Animated in `lambda view`; exports show the first frame. |
| SVG | ✅ | See [§15](#15-svg). |
| WebP | ✅ | Shared static decoder for files and data URIs, including SVG images. Animated WebP remains unsupported. |
| AVIF, BMP, TIFF, ICO | ❌ | Render blank. |
| Lottie (`.json`, `.lottie`) | ◐ | Played in `lambda view`; a blank 300 × 300 box in `layout` and `render`. |
| Remote `http(s)` images | *not verified* | A network path exists (`radiant/surface.cpp`). |

| Image feature | Status | Notes |
|---|---|---|
| `width`/`height` attributes, intrinsic size, CSS `aspect-ratio` | ✅ | |
| `object-fit`, `object-position` | ✅ | Raster output and SVG-image SVG/PDF exports; raster-image vector-export fitting retains the separate §17 limits. |
| `object-view-box`, `image-orientation` | ✅ | Resolved and laid out (WPT CSS Images suite). |
| `image-rendering` | ❌ | Parsed; scaling is always bilinear. |
| `srcset`, `sizes` | ❌ | |
| `<picture>` | ◐ | Uses the first `<source>` without `media` or `type`, and only when the `<img>` has no `src`. |
| Broken image and `alt` text | ◐ | Layout reserves a box sized for the `alt` text, but nothing is painted: no text and no icon. |

## 17. Output Targets

`lambda render` records one paint list per page and replays it to the chosen format. PNG and JPEG come from the same raster painter as the `lambda view` window and are the most complete; SVG and PDF are vector exports that fall back to embedded raster images for some effects and drop others. See [RAD_12](dev/radiant/RAD_12_Paint_IR_Display_List.md) and [RAD_13](dev/radiant/RAD_13_Render_Walk_Painters.md).

| Feature | PNG / JPEG | SVG | PDF |
|---|---|---|---|
| Text | ✅ glyphs from Lambda's font engine | ◐ live `<text>` elements naming the font family; fonts are not embedded, so the viewer substitutes any font it lacks | ❌ built-in Helvetica, Times and Courier only, not embedded: web fonts, bold, italic and underline are lost, and non-ASCII text is garbled |
| Colours, borders, `border-radius` | ✅ | ◐ `dashed` and `dotted` borders drawn solid | ◐ `dashed` and `dotted` borders drawn solid |
| Linear and radial gradients | ✅ | ◐ native gradients, but a CSS radial gradient's radius is always half the smaller side | ◐ rasterized at 1 px per CSS pixel; same radius caveat |
| Conic gradients | ✅ | ❌ dropped | ❌ dropped |
| `box-shadow`, `filter`, blend modes | ✅ | ◐ embedded PNG at 1× | ◐ embedded raster at 1× |
| `backdrop-filter` | ✅ | ❌ an opaque grey box | ❌ an opaque grey box |
| `opacity` | ✅ | ✅ group opacity | ◐ applied per object, so overlapping children show through each other |
| CSS `clip-path` | ◐ supported shapes | ◐ supported shapes | ◐ supported shapes |
| 2D transforms | ✅ | ✅ | ✅ |
| 3D transforms, `perspective` | ✅ | ◐ projected matrices with a constant positive homogeneous divisor retain their scale; coordinate-dependent perspective is dropped | ◐ same; the constant-divisor case has PDF pixel coverage |
| `<img>` (raster) | ✅ | ◐ linked as `file://` URLs, not embedded; `data:` images dropped; `object-fit` ignored | ✅ embedded; `object-fit` ignored |
| `<img>` (SVG) | ✅ | ✅ resolved paint with embedded resources and shared object-fit/object-position placement | ✅ shared SVG paint; native opaque paths or transparent raster fallback at the requested density |
| CSS `background-image` | ✅ | ◐ a `<pattern>` referencing the original relative URL; a non-repeating image without `background-size` gets zero size | ❌ dropped |
| Inline `<svg>` | ✅ | ✅ resolved viewport, CSS, sampled animation and resources; paths/gradients stay vector, unsupported paint embeds transparent PNGs; text outlines retain accessible titles and semantic metadata | ✅ resolved SVG paint, with native opaque paths and transparent density-aware fallback for other operations |
| Hyperlinks | n/a | ❌ | ◐ external and internal link annotations from HTML anchors; no outlines |
| Pages | one image | one image | ◐ fixed one-page export honors active `@page` paper size and margins at 72 pt/in, or fits content when no `@page` is active. HTML PDF export applies print media. Explicit paged export handles page breaks separately. |
| Page background | root/body canvas paint, with white fallback | same canvas resolution | same canvas resolution |
| Size when no `-vw`/`-vh` is given | PNG: laid out at 1200 px wide, canvas fits the content plus 50 px. **JPEG: a fixed 1200 × 800 crop** | laid out at 1200 px, canvas fits the content plus 50 px | laid out at 800 px, page fits the content plus 50 px |
| `-s` / `--pixel-ratio` | ✅ e.g. `-s 2` doubles the pixels | ✅ `-s` scales physical dimensions and content together; `--pixel-ratio` is ignored | ✅ `-s` scales the page, content and SVG capture density together; `--pixel-ratio` is ignored |
| Colour management | PNG: RGBA without colour-space chunks; JPEG: quality 85, no ICC profile | — | DeviceRGB, no ICC profile |

For print-quality PDF today, render to PNG at a higher density (`-s 2`) or use the SVG output, and keep text to fonts the reader has installed.

SVG-content export rows were verified on macOS on 2026-10-03 by
`test/svg/test_svg_export.cjs` (the runner behind `make test-svg-export`):
18 fixtures at 1×/2× cover relocated XML-valid SVG, unique resource IDs, embedded
images/fonts, clips, visible overflow, paint servers, effects, embedded HTML and
PDF pixels. SVG text and local/embedded font fixtures retain vector outlines in
both exports; general HTML text retains the separate limitations above.
Linux/Windows export smoke remains pending. Recording snapshots follow
**D4.5.1v4**'s "pin, gen-check, copy-as-value" seam; progress and evidence are in
[SVG closeout](../vibe/impl/Lambda_Impl_SVG_Support.md#716-p12-remaining-timing-instance-dom-and-final-closeout). Exports sample document time zero; active negative intervals contribute, while intervals already ended before zero do not. No CLI sample-time option is exposed.

## 18. Known Limitations

The gaps most likely to change how a real page looks, with the section that has the detail. Each design document in [dev/radiant/](dev/radiant/RAD_00_Overview.md) ends with a *Known Issues* section for engine-level detail.

**Text**

- No text shaping: no ligatures, and Arabic, Indic and other complex scripts render incorrectly ([§11](#11-text-and-fonts)).
- Bidirectional reordering depends on the optional FriBidi library; without it, right-to-left paragraphs keep their runs in left-to-right order ([§11](#11-text-and-fonts)).
- Vertical writing modes lay out correctly but paint their text horizontally ([§11](#11-text-and-fonts)).
- `text-overflow: ellipsis` draws no ellipsis ([§11](#11-text-and-fonts)).

**CSS**

- `@container` has no effect; `@property` now applies tested registration/default/inheritance/computed-value behavior with remaining computation/API/animation gaps. `@scope` applies tested root/limit/proximity behavior with remaining context/interface gaps; nesting applies style rules and supported nested group conditions but still lacks container-dependent rules and broader live invalidation. Cascade layers remain partial, and many media features are unsupported. Fixed PDF export applies print media and active `@page` geometry; automatic page breaking uses the explicit paged path ([§4](#4-at-rules)).
- Column matching outside HTML tables and some selector-backed live invalidation still have gaps ([§3](#3-css-selectors)).
- `lab()`, `lch()`, `oklab()`, `oklch()`, other predefined `color()` spaces, `color-mix()` and `light-dark()` render black. `color(srgb …)` computation/CSSOM/paint and registered HWB serialization are verified; missing-component interpolation and broader color contexts remain open. Math functions other than `calc()`, `min()`, `max()` and `clamp()` evaluate to 0; newer units such as `dvh`, `cqw` and `cap` are read as pixels ([§6](#6-values-units-and-functions)).
- The `animation` shorthand applies its eight CSS Animations 1 components and lists; calculated times, `linear()` and broader animation composition remain open. The `border-image` shorthand paints linear gradients and raster URL sources in raster output, while HTML SVG/PDF export omits its slices. The `background` shorthand paints one URL image, while its multilayer forms remain partial ([§7](#7-css-property-coverage)).
- `all: inherit` and `all: revert` are verified for common box and text properties, while other consumers still need complete visual resolution; custom-property substitution needs full token-stream and shorthand validation ([§5](#5-cascade-inheritance-and-custom-properties)).
- Grid track lengths in supported CSS units and pure-length `calc()` reach layout; mixed percentage/length `calc()` and grid caps remain open ([§8](#8-layout-modes)).

**SVG and images**

- Inline SVG shares the page cascade; external SVG images retain isolated styles ([§15](#15-svg)).
- SVG filter component transfer/convolution, broader SVG2 vector effects and motion/discard animation remain outside the implemented inventory. Text retains the general font shaping limits. Linux/Windows SVG export runtime smoke is pending ([§15](#15-svg)).
- AVIF, BMP, TIFF and ICO images do not load ([§16](#16-image-formats)).

**Output**

- PDF output uses the built-in Helvetica, Times and Courier fonts only and garbles non-ASCII page text; Info metadata is UTF-16BE capable. Fixed export has one page with link annotations and no CSS background images ([§17](#17-output-targets)).
- SVG and PDF output support basic CSS `clip-path` shapes; they still lose `dashed`/`dotted` borders and conic gradients. SVG output also ignores overflow clipping and `visibility: hidden`, and links images instead of embedding them ([§17](#17-output-targets)).
- JPEG output without `-vw`/`-vh` is a fixed 1200 × 800 crop rather than sized to the page ([§17](#17-output-targets)).
- Static layout and render sample CSS animations at time 0; general animation composition, calculated times and display-none cancellation remain incomplete ([§13](#13-transitions-and-animations)).

**Engine limits and approximations**

- Fixed caps: 64 grid tracks per axis and 256 items per grid; flex containers nested 16 deep; 64 keyframe selectors and 32 declarations per keyframe; a bounded `<iframe>` nesting depth. Content beyond a cap is dropped silently ([§8](#8-layout-modes), [§13](#13-transitions-and-animations)).
- Multi-column fragmentation is simplified, grid named-line resolution is simplified, and intrinsic heights used while measuring flex and grid items are estimates ([RAD_05](dev/radiant/RAD_05_Intrinsic_Sizing.md), [RAD_09](dev/radiant/RAD_09_Grid_Layout.md), [RAD_11](dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md)).
- A negative `z-index` child paints above its parent's background ([§12](#12-visual-effects)).
- In `lambda view`, media queries are evaluated once at load and are not re-run when the window is resized, and `position: fixed` boxes are listed as scrolling with the page ([§4](#4-at-rules), [§14](#14-interaction)).
- Video and audio play on macOS only ([§2](#2-html)).

## 19. Related Documentation

- [Lambda_Doc_Pipeline.md](Lambda_Doc_Pipeline.md): the Mark data model and the convert, validate, render, view and edit workflows.
- [Lambda_CLI.md](Lambda_CLI.md): options for `layout`, `render`, `view` and `edit`.
- [JS_DOM_Support.md](JS_DOM_Support.md): LambdaJS and the DOM, CSSOM, canvas and event APIs that page scripts use.
- [Markup_Formats_Support.md](Markup_Formats_Support.md): the input formats that reach Radiant after conversion.
- [Math_Support.md](Math_Support.md): LaTeX math typesetting.
- [Lambda_Packages.md](Lambda_Packages.md): the `dom`, `edit` and `editor` packages and the packages whose output Radiant draws.
- [dev/radiant/RAD_00_Overview.md](dev/radiant/RAD_00_Overview.md): the Radiant engine design set, RAD_01 to RAD_22.
- [dev/js/JS_13_Web_DOM.md](dev/js/JS_13_Web_DOM.md): the DOM and web-platform design.
