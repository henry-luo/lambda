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

> **Status (alpha, 2026-09-28).** This matrix was compiled from the source (`lambda/input/css/`, `radiant/`, `lib/font/`), the conformance suites, and spot checks run with `lambda layout` and `lambda render` on 2026-09-28. Legend: ✅ supported · ◐ partial, with what is missing stated · ❌ not supported. *Parsed only* means the CSS parser accepts the syntax but nothing downstream applies it. Anything that could not be confirmed is marked *not verified*.

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
| Constraint validation | ◐ | `required`, `type`, `min`, `max`, `step`, `minlength` and `maxlength` drive `:valid`/`:invalid` in `lambda view` (validation lives in the `dom` package, [Lambda_Packages.md §10.3](Lambda_Packages.md#103-dom--browser-behaviour-for-html)). `pattern` is checked only through `validity.patternMismatch` in JavaScript. |
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
| Attribute `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, the `i` flag | ✅ | The `s` flag is accepted and has no effect. |
| Namespaces `ns\|E`, `\|E` | ❌ | The rule is dropped. `*\|E` works. |
| Combinators: descendant, `>`, `+`, `~` | ✅ | |
| Column combinator `\|\|` | ❌ | Not parsed. |
| `:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:nth-last-child()`, `:first-of-type`, `:last-of-type`, `:only-of-type` | ✅ | An+B syntax including `odd`/`even`. |
| `:nth-of-type()`, `:nth-last-of-type()` | ❌ | Evaluated as `:nth-child()` / `:nth-last-child()`, so they count siblings of every type. |
| `:nth-child(An+B of S)` | ❌ | The `of S` form is not understood. The pseudo-class is ignored and the rest of the selector matches more elements than it should. |
| `:empty` | ◐ | An element containing only a comment is not treated as empty. |
| `:is()`, `:where()`, `:not()` with selector lists and complex selectors | ✅ | `:where()` contributes zero specificity. |
| `:has()` | ◐ | Descendant arguments only (`:has(.x)`). Relative arguments such as `:has(> .x)` and `:has(+ .x)` never match. |
| `:link` | ✅ | Any element with an `href`. |
| `:visited` | ❌ | Never matches (there is no history). |
| `:any-link`, `:local-link` | ❌ | Never match. |
| `:hover`, `:active`, `:focus`, `:focus-within`, `:focus-visible`, `:target` | ◐ | Live in `lambda view` (`:focus-visible` follows keyboard focus; `:target` follows fragment navigation). A static `layout` or `render` has no pointer or focus, so they never match there. |
| `:checked`, `:disabled`, `:enabled`, `:required`, `:optional`, `:read-only`, `:read-write`, `:open` | ✅ | From the element's attributes in static output, and from live state in `lambda view`. |
| `:placeholder-shown`, `:valid`, `:invalid` | ◐ | Live in `lambda view` only (validity comes from the `dom` package's validation); never match in static output. |
| `:default`, `:indeterminate`, `:in-range`, `:out-of-range`, `:user-invalid` | ❌ | Parsed, never match. |
| `:lang()`, `:dir()` | ❌ | Parsed, never match. |
| `:scope` | ◐ | Works in DOM query APIs; never matches in a stylesheet. |
| `:defined`, `:modal`, `:popover-open`, `:fullscreen`, `:autofill`, `:playing`, `:paused` | ❌ | Unknown pseudo-classes: they never match, so `:not(:defined)` matches every element. |
| `::before`, `::after`, `::first-line`, `::first-letter`, and the legacy single-colon spellings | ✅ | |
| `::marker` | ◐ | Matched, but not every property reaches the marker: `color` was ignored in a spot check. |
| `::placeholder` (and the `-webkit-`/`-moz-` spellings) | ✅ | |
| `::selection` | *parsed only* | Selection colours are not styleable. |
| `::backdrop` | *parsed only* | Stored; nothing lays it out or paints it. |
| `::file-selector-button` | ❌ | Its declarations are applied to the `<input>` element itself. |
| `::slotted()` | ◐ | Shadow-DOM slots; not verified on real pages. |
| `::part()`, `::cue`, `::highlight()`, `::-webkit-*` | ❌ | Never match. |

Specificity and selector-list handling have known gaps:

- Only `:hover`, `:active`, `:focus`, `:visited`, `:link`, `:first-child`, `:last-child`, `:nth-child()` and `:nth-last-child()` add pseudo-class specificity; every other pseudo-class and every pseudo-element counts as zero. `:is()`, `:not()` and `:has()` correctly take their most specific argument.
- For a selector list such as `div.a, #i.a`, the rule takes the specificity of the first selector in the list that matches, not the most specific one, so it can lose to a rule that should rank below it.
- A list containing an invalid selector, such as `.a, .a:bogus`, is kept rather than dropped.
- Pseudo-class, at-rule and property names are matched case-sensitively: `:FIRST-CHILD`, `@MEDIA` and `WIDTH:` are ignored.

## 4. At-Rules

| At-rule | Status | Notes |
|---|---|---|
| `@media` | ◐ | Supported: the media types `all` and `screen`; `width` and `height` with `min-`/`max-` (px, em, rem, with em fixed at 16 px); `orientation`; `prefers-color-scheme` (always `light`); `prefers-reduced-motion` (always `no-preference`); `and`, `not`, `only` and comma lists. Not supported: range syntax such as `(width >= 600px)`, which **always matches**; boolean features such as `(color)`, which always match; `aspect-ratio`, `resolution`, `hover` and `pointer`, which never match; `or`, which evaluates only its first term. `print` and `speech` never match, even for PDF output, while an unknown type such as `tv` matches. |
| `@import` | ◐ | Local files and http(s), nested up to 5 levels. Media, `supports()` and `layer()` conditions are ignored. Imported rules are ordered *after* the sheet that imports them, so they override it instead of the reverse. |
| `@font-face` | ◐ | `font-family`; `src` with `url()` plus `format()` (woff2, woff, truetype, opentype) and the first `local()`; `font-style` normal, italic or oblique (no angle); `font-weight` as a single value (no ranges); `unicode-range`. `font-display` is parsed and ignored. Top-level rules only. Remote (http/https) font URLs are skipped by the synchronous loader (see [§11](#11-text-and-fonts)). |
| `@keyframes` | ✅ | Top-level rules only; `@-webkit-keyframes` is dropped. |
| `@supports` | ◐ | `not`, `and`, `or` and parentheses work, but a declaration test only checks that the property is known: `(display: bogus)` is true, so `not (display: bogus)` is false. `selector()` is always false. |
| `@layer` | ❌ | Layer blocks apply as ordinary unlayered CSS in source order: no layer ordering, unlayered rules do not beat layered ones, and there is no `!important` inversion. The statement form `@layer a, b;` also swallows the rule that follows it. |
| `@container` | *parsed only* | Rules inside never apply; `container-type` and `container-name` have no effect. |
| CSS nesting (`&`, nested rules) | *parsed only* | Nested rules are parsed but never applied. |
| `@page` | *parsed only* | Visible to CSSOM; margin boxes are not parsed, and PDF output takes neither page size nor margins from it. |
| `@namespace` | ❌ | Ignored; namespaced selectors are dropped. |
| `@property`, `@counter-style`, `@scope`, `@starting-style`, `@font-feature-values`, `@view-transition` | ❌ | Skipped with their block; rules inside `@scope` and `@starting-style` are lost. |
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
| `!important` | ◐ | Beats normal declarations. Stylesheet declarations are internally tagged with the user-agent origin, so a stylesheet `!important` wins over an inline `style="… !important"`, which is the reverse of the standard. |
| Origins | ◐ | Built-in UA styles and author styles only; there are no user stylesheets. |
| Specificity and source order | ◐ | See the specificity gaps in [§3](#3-css-selectors). |
| Inheritance, `inherit` | ✅ | |
| `initial`, `unset` | ◐ | Resolved per property; on `width` and `height` they produce 0 instead of `auto`. |
| `revert` | ◐ | Falls back to the built-in UA default. |
| `revert-layer` | ❌ | Not a recognized keyword. |
| `all` | ◐ | `all: initial` resets only the font properties. |
| Custom properties and `var()` | ◐ | Fallbacks, nested `var()`, `var()` inside shorthands and inside `calc()` all work. Three gaps: a `var()` cycle ignores the fallback and yields the property's initial value; a custom property defined as `var(--other)` is resolved where it is used rather than where it is declared, so an inherited value can change; an unresolvable `var()` in `width` or `height` gives 0. |
| `@property` registration | ❌ | See [§4](#4-at-rules). |
| Cascade layers | ❌ | See [§4](#4-at-rules). |
| HTML presentational attributes | ✅ | `body` `bgcolor`/`marginwidth`/`marginheight`/`leftmargin`/`topmargin`; `table` `bgcolor`/`border`/`align`/`cellpadding`/`cellspacing`/`rules`/`width`/`height`; `tr`/`td`/`th` `bgcolor`/`align`/`valign`/`nowrap`/`width`/`height`; `font` `color`/`size`/`face`; `width`/`height` on `img`, `iframe`, `video`, `canvas`, `embed` and `object`; `align` on `hr`/`div`/`p`; `dir`; `hidden`; `ol` `start`/`reversed` and `li` `value`; `size` on `input`/`select`; `cols`/`rows` on `textarea`. |

In `lambda view`, a change of interaction state (hover, focus, checked, …) re-runs the cascade when a stylesheet uses the affected pseudo-class, then reflows the page. Static `layout` and `render` run the cascade once, before layout.

## 6. Values, Units and Functions

Unsupported functions and units are the most common source of silent differences from a browser. An unknown math function or unit does not invalidate the declaration: it evaluates to 0 or to a raw pixel number and can override a valid earlier value.

### 6.1 Units

| Unit | Status | Notes |
|---|---|---|
| `px`, `cm`, `mm`, `Q`, `in`, `pt`, `pc` | ✅ | |
| `em`, `rem`, `ex`, `ch` | ✅ | `ex` and `ch` come from the font's metrics. |
| `%` | ✅ | Resolved against the right reference per property, including deferred resolution for absolutely positioned boxes. |
| `vw`, `vh`, `vmin`, `vmax` | ✅ | Against the layout viewport ([§4](#4-at-rules)). |
| `lh` | ◐ | Uses only the element's own `line-height`, otherwise `normal`. |
| `rlh`, `cap`, `ic`, `vi`, `vb`, `svw`/`svh`, `lvw`/`lvh`, `dvw`/`dvh` | ❌ | Parsed, then read as `px` (`2cap` is 2 px). |
| Other `sv*`/`lv*`/`dv*` variants, `cqw`, `cqh`, `cqi`, `cqb`, `cqmin`, `cqmax` | ❌ | Read as `px`. |
| `deg`, `rad`, `grad`, `turn` | ◐ | Converted in transforms. Gradient angles, `hue-rotate()` and the `hsl()` hue read the number as degrees, so `0.25turn` is 0.25°. |
| `s`, `ms` | ✅ | |
| `fr` | ✅ | Grid tracks. |
| `dpi`, `dpcm`, `dppx`, `x` | ◐ | Only inside `image-set()` in `content`. |

### 6.2 Functions

| Function | Status | Notes |
|---|---|---|
| `calc()` | ✅ | Mixed units, precedence, nesting, `var()` inside. A percentage inside `calc()` on an absolutely positioned box is resolved against the parent element instead of the containing block. |
| `min()`, `max()`, `clamp()` | ✅ | |
| `round()`, `mod()`, `rem()`, `abs()`, `sign()`, `sin()`/`cos()`/`tan()`/`asin()`/`acos()`/`atan()`/`atan2()`, `pow()`, `sqrt()`, `hypot()`, `log()`, `exp()`, `pi`, `e`, `infinity` | ❌ | Evaluate to 0 (verified: `calc(100px * sin(30deg))` gives width 0). |
| `var()` | ◐ | As a whole value, inside `calc()`/`min()`/`max()`, in colours and in `border`. There is no token-level substitution, so `margin: var(--m)` with `--m: 7px 9px` gives 7 px on every side, and `var()` inside `rgb()` or as a `display` or `grid-template-columns` value fails. See also [§5](#5-cascade-inheritance-and-custom-properties). |
| `env()` | ❌ | Evaluates to 0; the fallback is ignored. |
| `attr()` | ◐ | In `content` only; typed `attr()` elsewhere gives 0. |
| `counter()`, `counters()` | ✅ | With a list-style argument. |
| `url()` | ◐ | `background-image`, `list-style-image`, `content`, `@font-face`, `@import`. Not for `cursor`, `border-image`, `mask-image`, `filter` or `clip-path`. |
| `image-set()` | ◐ | In `content` only; takes the first candidate. |
| `cross-fade()`, `element()` | ❌ | |

### 6.3 Colours

| Syntax | Status | Notes |
|---|---|---|
| Hex: `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa` | ✅ | |
| Named colours, `transparent`, `currentColor` | ✅ | All 148 CSS named colours. |
| `rgb()`, `rgba()` | ◐ | Comma and space/slash syntax. A percentage alpha (`rgb(255 0 0 / 50%)`) paints nothing; a numeric alpha works. |
| `hsl()`, `hsla()` | ✅ | Hue in degrees only (see [§6.1](#61-units)). |
| `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, `color-mix()`, `light-dark()`, relative colours (`rgb(from …)`) | ❌ | Render black or not at all (verified for text and backgrounds). |
| System colours (`Canvas`, `ButtonText`, …) | ❌ | Recognized but have no value; render black. |

Colours are sRGB throughout; there is no wide-gamut output.

### 6.4 Gradients

| Function | Status | Notes |
|---|---|---|
| `linear-gradient()` | ◐ | Angles in `deg`, `to` sides and corners (corners as fixed 45° steps), several stops, two-position stops. Colour hints and `in <colorspace>` are ignored. |
| `repeating-linear-gradient()` | ◐ | Correct in raster output; wrong offsets in SVG output. |
| `radial-gradient()` | ◐ | `circle`/`ellipse` and `at <position>` with keywords. Size keywords (`closest-side`, `farthest-corner`, …) are not parsed. |
| `repeating-radial-gradient()` | ◐ | Paints without repeating. |
| `conic-gradient()` | ◐ | `from <angle>`; `at <position>` is ignored. Raster output only ([§17](#17-output-targets)). |
| `repeating-conic-gradient()` | ◐ | Paints without repeating, and only through the `background` shorthand. |

### 6.5 Transform, filter, easing and shape functions

| Family | Status | Notes |
|---|---|---|
| Transform functions | ◐ | `translate()`/`X`/`Y`/`Z`/`3d`, `rotate()`/`X`/`Y`/`Z`/`3d`, `skew()`/`X`/`Y`, `matrix()`, `matrix3d()`, `perspective()`, all angle units. `scale()` with a percentage is ignored. The individual `translate`, `rotate` and `scale` properties are not supported. |
| Filter functions | ◐ | `blur()`, `brightness()`, `contrast()`, `grayscale()`, `invert()`, `opacity()`, `saturate()` and `sepia()` work. `hue-rotate()` takes degrees only; `drop-shadow()` ignores a named colour and draws black; `url()` filters are not supported. |
| Easing | ◐ | `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier()`, `step-start`, `step-end`. `steps()` supports only start and end jumps (`jump-both`/`jump-none` behave as end); `linear()` with stops falls back to `ease`. |
| Grid functions | ◐ | `repeat()` with `auto-fill`/`auto-fit`, `minmax()`, `fit-content()`, named lines, `grid-template-areas`. Track lengths in units other than `px`, `%` and `fr` are read as pixels, `calc()` tracks are dropped, and `subgrid` and `masonry` are not supported. |
| Basic shapes for `clip-path` | ◐ | `inset()`, `circle()`, `ellipse()` and `polygon()` clip in raster output, with lengths in `px` and `%` only (`circle(2em)` is 2 px). `path()` is parsed but does not clip; `rect()`, `xywh()`, `shape()`, `url(#svg-clip)` and geometry boxes are not supported. |

## 7. CSS Property Coverage

The parser's property table (`lambda/input/css/css_properties.cpp`) knows **342 properties**. Classified by what the resolver (`radiant/resolve_css_style.cpp`) stores and what layout or paint then reads: **244 have an effect** (196 longhands and 48 shorthands), **43 work only for some values**, and **55 are parsed only**, with no effect. A property the table does not know is dropped silently. A vendor-prefixed name such as `-webkit-transform` or `-moz-border-radius` maps to the standard property when that property is known. Shorthands marked `*` are resolved directly rather than expanded into longhands at parse time.

| Area | Applied | Partial (what is missing) | Parsed only (no effect) |
|---|---|---|---|
| Box model | `display`, `box-sizing`, `margin`\*, `padding`\* and their physical and logical (`-block`, `-inline`, `-start`, `-end`) longhands | | `margin-trim` |
| Sizing | `width`, `height`, `min-`/`max-` of both, `block-size`, `inline-size` and their `min-`/`max-` forms | | |
| Positioning | `position`, `top`, `right`, `bottom`, `left`, `inset`\*, `inset-block`\*, `inset-inline`\* and their longhands, `z-index` | | |
| Floats | | `float`, `clear` (no `inline-start`/`inline-end`) | `float-reference`, `float-defer`, `float-offset`, `wrap-flow`, `wrap-through` |
| Overflow and scrolling | `overflow`\*, `overflow-x`, `overflow-y`, `scrollbar-gutter` | | `overflow-block`, `overflow-inline`, `overflow-clip-margin`, `overscroll-behavior`, `scroll-margin`, `scroll-padding`, `scroll-behavior`, `scroll-snap-type`, `scroll-snap-align` |
| Flexbox | `flex`\*, `flex-flow`\*, `flex-direction`, `flex-wrap`, `flex-grow`, `flex-shrink`, `flex-basis`, `order` | | |
| Grid | `grid`\*, `grid-template`\*, `grid-template-areas`, `grid-area`\*, `grid-row`\*, `grid-column`\* and their start/end longhands, `grid-gap`\*, `grid-row-gap`, `grid-column-gap` | `grid-template-columns`, `grid-template-rows`, `grid-auto-rows`, `grid-auto-columns` (non-`px` lengths read as px, no `calc()`), `grid-auto-flow` (no `dense`) | |
| Box alignment | `justify-content`, `justify-items`, `justify-self`, `align-content`, `align-items`, `align-self`, `place-content`\*, `place-items`\*, `place-self`\*, `gap`\*, `row-gap`, `column-gap` | | |
| Tables | `border-collapse`, `border-spacing`, `caption-side`, `empty-cells`, `table-layout` | | |
| Multi-column | `columns`\*, `column-count`, `column-width`, `column-fill`, `column-span`, `column-rule`\*, `column-rule-width`, `column-rule-style`, `column-height`, `column-wrap` | `column-rule-color` (named colours and `rgb()` ignored) | |
| Fragmentation | `box-decoration-break` | `break-before`, `break-after`, `break-inside`, `page-break-*`, `orphans`, `widows` (inside multi-column only; no pagination) | |
| Lists and generated content | `list-style`\*, `list-style-type`, `list-style-position`, `list-style-image`, `counter-reset`, `counter-increment`, `counter-set`, `content`, `quotes` | | `marker-offset` |
| Text | `white-space`, `text-wrap`\*, `text-wrap-mode`, `word-break`, `line-break`, `overflow-wrap`, `word-wrap`, `hyphens`, `hyphenate-character`, `text-align`, `text-align-last`, `letter-spacing`, `word-spacing`, `text-transform`, `text-decoration-style`, `text-decoration-color`, `text-overflow`, `line-height`, `vertical-align`, `text-spacing-trim`, `text-autospace`, `initial-letter`, `text-box`\*, `text-box-trim`, `text-box-edge`, `line-clamp`, `-webkit-line-clamp`, `dominant-baseline`, `baseline-source`, `ruby-position` | `text-decoration`, `text-decoration-line` (one line keyword: `underline overline` paints only the overline), `text-decoration-thickness` (lengths only), `text-shadow` (dropped unless the element also sets a font property), `text-indent` (no `hanging`/`each-line`), `tab-size` (numbers only), `text-wrap-style` (`balance` only), `text-justify` (`none` only), `text-emphasis`, `text-emphasis-style`, `text-emphasis-position` (space reserved, marks not painted) | `text-align-all`, `text-emphasis-color`, `hanging-punctuation`, `alignment-baseline`, `baseline-shift`, `ruby-align` |
| Fonts | `font`\*, `font-family`, `font-size`, `font-weight`, `font-style`, `font-kerning` | `font-variant` (`small-caps` only) | `font-stretch`, `font-size-adjust`, `font-variant-ligatures`, `-caps`, `-numeric`, `-alternates`, `-east-asian`, `font-feature-settings`, `font-variation-settings`, `font-language-override`, `font-optical-sizing`, `font-display` |
| Writing modes | `writing-mode`, `direction`, `unicode-bidi`, `text-combine-upright` | `text-orientation` (no `sideways`) | |
| Colour and background | `color`, `background-color`, `background-position`, `-position-x`, `-position-y`, `background-size`, `background-repeat`, `background-origin`, `background-clip` | `background`\* (an image `url()` is not painted), `background-image` (one image layer; no `image-set()`/`cross-fade()`), `background-blend-mode` (image over the background colour only) | `background-attachment` |
| Borders and outline | `border`\* and every physical and logical side, width, style and colour longhand the table knows, `border-radius`\* and the four corner radii, `outline`\*, `outline-width`, `outline-style`, `outline-color`, `outline-offset` | `border-image-source` (linear gradients only), `border-image-width` (one value) | `border-image`\*, `border-image-slice`, `border-image-outset`, `border-image-repeat` |
| Effects | `box-shadow`, `opacity`, `visibility`, `mix-blend-mode` | `filter`, `backdrop-filter` (see [§6.5](#65-transform-filter-easing-and-shape-functions)), `clip-path` (basic shapes, raster output only), `mask-image` (a radial gradient becomes a hard circular clip; no `url()` or linear masks) | `isolation`, `clip`, `mask-type` |
| Transforms | `transform`, `transform-origin`, `transform-style`, `perspective`, `perspective-origin` | | `backface-visibility` |
| Transitions and animations | `transition`\* and its longhands, `animation-name`, `-duration`, `-delay`, `-iteration-count`, `-direction`, `-fill-mode`, `-play-state`, `-timing-function` | | `animation`\* (the shorthand is not expanded: use the longhands) |
| UI | `cursor`, `pointer-events`, `user-select`, `accent-color`, `caret-shape`, `field-sizing` | `appearance` (`none` and `base-select` only) | `caret-color`, `resize`, `nav-index`, `nav-up`, `nav-right`, `nav-down`, `nav-left` |
| Replaced elements | `object-fit`, `object-position`, `object-view-box`, `image-orientation`, `aspect-ratio` | | `image-rendering` |
| Containment | `contain-intrinsic-size`\* and its longhands | `contain` (size containment only), `container-type`, `content-visibility` (`hidden` only) | `container`\*, `container-name` |
| SVG | `fill`, `stroke`, `stroke-width` (on the root `<svg>` only; see [§15](#15-svg)) | | |
| Other | `zoom` | `all` (`initial` and `unset` only) | |

**Not recognized at all** (dropped): the individual transform properties `translate`, `rotate` and `scale`; `will-change`; `touch-action`; `text-underline-offset`; the `mask` shorthand and the other `mask-*` properties; `shape-outside` and the other `shape-*` properties; `offset-*`; `color-scheme`; `font-synthesis`; `font-palette`; the logical border properties `border-inline-width`/`-style`/`-color` and `border-block-style`; the logical corner radii; anchor positioning; scroll-driven animation timelines; `transition-behavior`; and SVG properties other than `fill`, `stroke` and `stroke-width` (`stroke-dasharray`, `fill-opacity`, … are read only as attributes or from a `<style>` inside the SVG).

## 8. Layout Modes

Every mode below lays out the same tree, and all positions and sizes are fractional CSS pixels. Flex, grid and table layout share a measure-then-place engine for intrinsic (min-content, max-content, fit-content) sizes. The design is in [RAD_03](dev/radiant/RAD_03_Layout_Driver_Block_BFC.md) through [RAD_11](dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md).

| Mode | Status | Notes |
|---|---|---|
| Block flow | ✅ | Margin collapsing (siblings, parent and child, empty blocks), block formatting contexts (`display: flow-root`, `overflow`, floats, inline-blocks), `auto` margins for centering. |
| Inline formatting | ✅ | Line boxes, `vertical-align`, `line-height`, `inline-block`, `inline-flex`, `inline-grid`, replaced elements on the line; text details in [§11](#11-text-and-fonts). |
| `display` values | ✅ | `block`, `inline`, `inline-block`, `flex`, `inline-flex`, `grid`, `inline-grid`, `flow-root`, `list-item`, `contents`, `none`, the `table-*` family and `ruby` (annotation placed above the base) were checked. |
| Flexbox | ◐ | Direction, wrapping, `gap`, `flex-grow`/`-shrink`/`-basis`, `order`, `justify-content` (including `space-evenly`), `align-items`/`-self`/`-content`, auto margins and baseline alignment. `align-items: last baseline` is not fully spec-correct, and flex containers nested more than 16 deep are not laid out further ([RAD_08 §9](dev/radiant/RAD_08_Flexbox_Layout.md)). |
| Grid | ◐ | Track lists with `px`, `%`, `fr`, `auto`, `min-content`, `max-content`, `minmax()` and `fit-content()`; `repeat()` with `auto-fill` and `auto-fit`; named lines and areas; line-number placement and spans; implicit tracks; gaps; box alignment. **Gaps:** track sizes in `em`, `rem` and other non-`px` units are read as pixels (`5em` becomes 5 px, verified); `grid-auto-flow: dense` behaves like sparse placement (verified); a single `minmax()` or `fit-content()` in `grid-auto-rows`/`-columns` is ignored; a grid holds at most 64 tracks per axis, where later columns collapse onto the 64th line, and 256 items, where later items are not placed (both verified). Named-line resolution is simplified ([RAD_09 §8](dev/radiant/RAD_09_Grid_Layout.md)). |
| Tables | ✅ | Automatic and fixed layout (CSS 2.1 §17), `border-collapse` with conflict resolution, `border-spacing`, row and column spans, captions, row groups, anonymous table boxes, `display: table*` on any element. |
| Floats and `clear` | ◐ | `left`, `right`, `none` and `both`, line shortening around floats and clearance work. The logical values `inline-start`/`inline-end` are not mapped, and `shape-outside` is not applied: text wraps around the float's margin box (verified). |
| Positioning | ◐ | `relative`, `absolute` and `sticky` (re-resolved as the page scrolls), `inset` and its longhands, `z-index`. `fixed` boxes use the initial containing block (or a transformed or contained ancestor); RAD_11 lists that they scroll with the page in `lambda view` instead of staying pinned, which was not verified here. |
| Multi-column | ◐ | `column-count`, `column-width`, `columns`, `column-gap`, `column-rule`, `column-fill`, balancing, `column-span: all`, and `break-before`/`-after`/`-inside`, `orphans` and `widows` inside columns. `column-rule-color` accepts only hex colours. Fragmentation is simplified ([RAD_11 §7](dev/radiant/RAD_11_Positioned_Float_Multicol_Lists.md)); the multi-column WPT suite records 144 full and 218 partial passes of 362. |
| Lists and counters | ◐ | Markers inside and outside, `list-style-type` (decimal, roman, alpha and bullet styles checked), `list-style-image`, `<ol start>`, `<ol reversed>`, `<li value>`, `counter-reset`/`-increment`/`-set`, `counter()` and `counters()` with a style argument. `::marker` styling is partial (see [§3](#3-css-selectors)); `@counter-style` is not supported. |
| Writing modes and bidi | ◐ | `writing-mode: vertical-rl` and `vertical-lr` swap the block and inline axes, so box geometry is correct, but glyphs are painted horizontally. `direction` and `unicode-bidi` apply; reordering of mixed-direction text needs the optional FriBidi library. Details in [§11](#11-text-and-fonts). |

## 9. Box Model and Sizing

| Feature | Status | Notes |
|---|---|---|
| `margin`, `padding`, `border-width`, percentages against the containing block's width | ✅ | Checked: `padding-top: 10%` resolves against the width. |
| `box-sizing: content-box \| border-box` | ✅ | |
| `width`, `height`, `min-*`, `max-*` | ◐ | Lengths, percentages, `auto`, `min-content`, `max-content`, `fit-content`, `fit-content()` and `stretch`. `initial`/`unset` give 0 instead of `auto` ([§5](#5-cascade-inheritance-and-custom-properties)). |
| `aspect-ratio` | ✅ | Checked: `160px` wide at `16/9` gives 90 px. |
| Margin collapsing | ✅ | |
| `overflow: visible \| hidden \| clip \| scroll \| auto` | ✅ | Establishes a block formatting context, clips painting (including rounded corners), and creates a scroll container in `lambda view`. |
| Logical properties (`margin-inline`, `inset-block`, …) | ◐ | Margins, paddings, insets, `inline-size`/`block-size` and `border-inline-start`/`-end` map to physical sides by `direction` and `writing-mode` (checked: `padding-inline-end` lands on the left under `rtl`, and `inline-size` becomes the height under `vertical-rl`). The logical border shorthands `border-inline-width`/`-style`/`-color` and `border-block-style` and the logical corner radii are not recognized ([§7](#7-css-property-coverage)). |

## 10. Backgrounds and Borders

Status is for the raster painter (PNG, JPEG and the `lambda view` window); SVG and PDF output differ in places, listed in [§17](#17-output-targets).

| Feature | Status | Notes |
|---|---|---|
| `background-color` | ✅ | |
| `background-image: url()`, one layer | ✅ | Local files, relative paths and `data:` URIs, with `background-repeat`, `background-position`, `background-size` (lengths, `cover`, `contain`) and `background-origin`. |
| Several background layers | ◐ | Gradient layers stack, but an image `url()` inside a multi-layer list is not painted (checked with two images, and with an image over a gradient). |
| `background` shorthand with an image | ❌ | `background: url(x.png) no-repeat` paints no image, with or without other components (checked with relative, absolute and `data:` URLs). Colours and gradients in the shorthand work. Use `background-image` and the other longhands. |
| `linear-gradient()`, `radial-gradient()`, `conic-gradient()`, `repeating-*` | ✅ | SVG and PDF output drop conic gradients. |
| `background-clip`, including `text` | ✅ | SVG output renders `background-clip: text` as invisible text. |
| `background-blend-mode` | ❌ | `multiply` over two gradient layers did not blend in a spot check. |
| `background-attachment` | ❌ | Parsed and stored, but no painter reads it: `fixed` and `local` paint like `scroll`. |
| `border-style` | ✅ | `solid`, `dashed`, `dotted`, `double`, `groove`, `ridge`, `inset`, `outset`, mixed per side. SVG and PDF output draw `dashed` and `dotted` borders as solid. |
| `border-radius` | ✅ | Per-corner and elliptical radii; backgrounds, borders and overflow clipping follow the curve. |
| `border-image` | ◐ | Gradient sources set through the longhands paint. An image `url()` source did not paint, and neither did the `border-image` shorthand: the ordinary border was drawn instead. |
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
| System fonts and fallback | ✅ | Per-character fallback through the platform font service; `serif`, `sans-serif` and `monospace` were checked. The generic families `emoji`, `math` and `fangsong` are not mapped. |
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
| `text-align` (including `justify`), `text-align-last`, `letter-spacing`, `word-spacing`, `text-transform` | ✅ | `text-transform: full-width` is not supported. |
| `text-indent` | ◐ | Lengths, percentages and `calc()`; the `hanging` and `each-line` keywords are rejected. |
| `line-height`, `vertical-align` | ✅ | |
| `text-decoration` | ◐ | Style (including `wavy`), colour and thickness work, but only one line keyword applies at a time: `underline overline` paints only the overline. `text-underline-offset`, `text-underline-position` and `text-decoration-skip-ink` are not recognized; underlines always skip descenders. |
| `text-shadow` | ◐ | Paints correctly, but is dropped unless the same element also sets a font property such as `font-size`. |
| `text-overflow: ellipsis` | ◐ | The text is clipped, but no "…" is painted (verified). |
| `line-clamp`, `-webkit-line-clamp` | ◐ | Lines are clamped, but the "…" is appended without shortening the last line, so it is clipped away. |
| `text-emphasis` | ◐ | Line space is reserved; the marks are not painted. |
| `tab-size` | ◐ | Integer values only. |
| Complex scripts | ❌ | Arabic is drawn as isolated letter forms in logical order, and Indic reordering is not done (WPT css-text shaping: 1 of 29 tests match). |
| Bidirectional text | ◐ | `direction` sets alignment and the inline base direction, and every `unicode-bidi` value resolves. Reordering of mixed-direction runs uses FriBidi when the build found it (an optional dependency); otherwise a simplified fallback is used, under which a right-to-left paragraph keeps its runs in left-to-right order. Mirrored brackets are not substituted. The macOS build checked for this document had no FriBidi. |
| `writing-mode: vertical-rl`, `vertical-lr`, `sideways-*` | ◐ | Box geometry is correct, but glyphs are painted horizontally (verified). `text-orientation: sideways` is treated as `mixed`. |
| `text-combine-upright` | ◐ | Affects layout; not verified in paint. |

## 12. Visual Effects

Status is for the raster painter; SVG and PDF output differ as listed in [§17](#17-output-targets). Paint is covered by a pixel-comparison suite against Chrome ([§1](#1-conformance-summary)); the design is in [RAD_13](dev/radiant/RAD_13_Render_Walk_Painters.md).

| Feature | Status | Notes |
|---|---|---|
| 2D transforms, `transform-origin` | ✅ | |
| 3D transforms, `perspective`, `perspective-origin`, `transform-style` | ◐ | 4×4 matrices are projected with perspective in raster output; there is no depth sorting between intersecting planes. `backface-visibility` is ignored. |
| `opacity` | ✅ | |
| `filter` | ◐ | `blur()`, `brightness()`, `contrast()`, `grayscale()`, `hue-rotate()`, `invert()`, `opacity()`, `saturate()`, `sepia()`, `drop-shadow()`, with the limits in [§6.5](#65-transform-filter-easing-and-shape-functions). The functions run in a fixed order (colour functions, then blur, then drop shadow) rather than in declaration order. `url()` filters are not supported. |
| `backdrop-filter` | ◐ | Same functions as `filter`; implemented for raster output, not spot-checked. SVG and PDF output paint an opaque grey box instead. |
| `mix-blend-mode`, `background-blend-mode` | ◐ | The 11 separable modes (`multiply`, `screen`, `overlay`, `darken`, `lighten`, `color-dodge`, `color-burn`, `hard-light`, `soft-light`, `difference`, `exclusion`). `hue`, `saturation`, `color`, `luminosity` and `plus-lighter` fall back to `normal`. `background-blend-mode` blends a layer with the background colour; two gradient layers did not blend with each other in a spot check. |
| `isolation` | ❌ | Parsed only. |
| `clip-path` | ◐ | Basic shapes in raster output ([§6.5](#65-transform-filter-easing-and-shape-functions)); no `url(#svg-clip)` and no geometry boxes. |
| `clip` (legacy `rect()`) | ❌ | Ignored (verified). |
| `mask-image` | ◐ | Only a `radial-gradient()`, approximated as a hard-edged circle. No `url()` or linear-gradient masks, no `mask` shorthand and no `mask-size` or `mask-position`. |
| `box-shadow` | ✅ | See [§10](#10-backgrounds-and-borders). |
| `z-index`, stacking contexts | ◐ | Positive `z-index` works. A child with a negative `z-index` paints above its parent's background when it should paint below it ([RAD_13](dev/radiant/RAD_13_Render_Walk_Painters.md) lists stacking contexts as simplified). |
| `overflow` clipping | ✅ | Including clipping to rounded corners. SVG output does not clip overflow. |
| `visibility: hidden` | ✅ | SVG output paints hidden content. |
| `outline` | ✅ | |

## 13. Transitions and Animations

Animations run on the frame clock of `lambda view` ([RAD_16](dev/radiant/RAD_16_Animation_Frame_Scheduling.md)). Static output does not animate, and the two static commands disagree: `lambda layout` samples keyframe animations at time 0 (honouring a negative `animation-delay` and the fill modes), while `lambda render` ignores CSS animations entirely and paints the unanimated styles.

| Feature | Status | Notes |
|---|---|---|
| `@keyframes` | ✅ | Up to 64 keyframe selectors per rule and 32 declarations per keyframe; anything beyond is silently dropped. |
| `animation-name`, `-duration`, `-timing-function`, `-delay`, `-iteration-count`, `-direction`, `-fill-mode`, `-play-state` | ✅ | |
| `animation` shorthand | ❌ | Ignored (verified: `animation: grow 10s` has no effect). Use the longhands. |
| Animatable properties | ◐ | 32 properties interpolate: `opacity`; `transform` (translate, scale, rotate, skew); colours and border colours; border widths; widths, heights and their minimums and maximums; the inset properties; margins; paddings; `aspect-ratio`; `display`. `filter`, `box-shadow` and `font-size` do not interpolate. |
| Timing functions | ◐ | See [§6.5](#65-transform-filter-easing-and-shape-functions). |
| Transitions | ◐ | Wired for `opacity`, `color`, `background-color`, `width`, `height` and their `min-`/`max-` forms, and `aspect-ratio`. `transform` does not transition, and at most 8 properties are taken from a `transition-property` list. Of the 10 WPT transition-event tests that are run, 1 passes. |
| Events | ◐ | `animationstart`, `animationiteration`, `animationend`, `transitionend`, `transitioncancel`. No `animationcancel`, `transitionrun` or `transitionstart`. |
| SVG SMIL animation | ❌ | See [§15](#15-svg). |
| Animated GIF, Lottie | ◐ | Play in `lambda view`; static output shows the first frame (GIF) or an empty box (Lottie). |

## 14. Interaction

`lambda view` is a browsing shell around the same engine: platform input goes through one event funnel, interaction state (hover, focus, selection, scroll) lives on the document, and page scripts and the `dom` package respond to events. Everything in this section applies to `lambda view` and `lambda edit` only. Design: [RAD_15 — Events](dev/radiant/RAD_15_Events_Input.md), [RAD_17 — Interaction State](dev/radiant/RAD_17_Interaction_State.md), [RAD_18 — Editing](dev/radiant/RAD_18_Editing_Selection_Ranges.md), [RAD_20 — Application Shell](dev/radiant/RAD_20_Application_Shell_Browsing.md).

| Feature | Status | Notes |
|---|---|---|
| Documents and navigation | ◐ | Local files in every supported format and http(s) URLs; links; back and forward through a history of up to 100 entries; fragment navigation, which drives `:target`. One document per window, with no tabs. Top-level SVG and image documents have no zoom or pan. |
| Mouse, keyboard, wheel, drag and drop, context menu | ✅ | The context menu has five fixed items. |
| Focus | ✅ | Tab order follows `tabindex`, plus `autofocus`; `:focus-visible` follows keyboard focus. Focus behaviour is written in Lambda in the `dom` package ([Lambda_Packages.md §10.3](Lambda_Packages.md#103-dom--browser-behaviour-for-html)). |
| Interaction pseudo-classes | ✅ | `:hover`, `:active`, `:focus`, `:focus-within`, `:focus-visible`, `:target`, `:checked`, `:valid`/`:invalid` and `:placeholder-shown` restyle live; see [§3](#3-css-selectors). |
| Scrolling | ◐ | Mouse wheel and scrollbar dragging, overlay scrollbars (shown on hover or drag for `overflow: auto`, always for `scroll`), `scrollbar-gutter`, and sticky positioning that follows the scroll. `scroll-behavior`, `scroll-snap-*` and `overscroll-behavior` are parsed only. RAD_11 lists `position: fixed` boxes as scrolling with the page instead of staying pinned (not verified here). |
| `contenteditable` (rich and `plaintext-only`) | ✅ | Radiant fires `beforeinput` and the `dom` package performs the edit, per ruling D7.2.5 in [Lambda_Formal_Design.md](Lambda_Formal_Design.md): editing policy belongs to the Lambda DOM behaviour package, not to native code. 50+ `inputType`s; WPT input-events: 20 files pass. |
| Form text controls | ✅ | Native caret, selection, undo and IME; see [§2](#2-html). |
| Text selection, `Selection` and `Range` | ◐ | Implemented ([RAD_18](dev/radiant/RAD_18_Editing_Selection_Ranges.md)); word and sentence movement in `Selection.modify()` is approximate. Not exercised for this document. |
| Clipboard | ◐ | An in-memory clipboard plus plain text through the OS; no rich (HTML) OS clipboard. |
| IME | ◐ | macOS and Windows; no reconversion and no clause underlines. |
| `cursor` | ◐ | Only the text, pointer and default arrow shapes are shown. |
| `lambda edit` | ✅ | Rich-text editing for Markdown and HTML and a drawing editor for SVG, built as Lambda packages on the DOM layer; see [Lambda_Doc_Pipeline.md §3.6](Lambda_Doc_Pipeline.md#36-edit). |

## 15. SVG

Radiant parses and paints SVG itself, whichever way it arrives: inline `<svg>` in HTML, a standalone `.svg` file given to `lambda render` or `lambda view`, `<img src="x.svg">`, CSS `url(x.svg)`, `data:` SVG URIs, SVG `<image href="x.svg">`, and external `<use href="f.svg#id">`. ThorVG is the rasterizer underneath; its own SVG loader is not used. Design: [RAD_14 — SVG, Vector Graphics & Diagram Layout](dev/radiant/RAD_14_SVG_Vector_Graph.md). SVG *output* is covered in [§17](#17-output-targets).

**2026-10-02 update:** radius defaults, default line paint, path error recovery,
basic CSS colors, group opacity and geometry bounds for gradient fills have
fresh Chromium pixel checks. The implementation and validation record is in
[SVG Support](../vibe/impl/Lambda_Impl_SVG_Support.md#74-progress-record).
Other entries retain the 2026-09-28 evidence date.

### 15.1 Elements

| Element | Status | Notes |
|---|---|---|
| `<svg>`, nested `<svg>` | ✅ | `viewBox` and `preserveAspectRatio` (alignment, `meet`, `slice`, `none`). Default size 300 × 150; with only a `viewBox` it fills the container width at the viewBox ratio. Content is always clipped to the box, so `overflow="visible"` is ignored. |
| `<g>` | ✅ | Group opacity composites overlapping shapes once, with or without a root `viewBox`, including nested groups and a nonzero viewBox origin. |
| `<path>` | ✅ | All commands (`M L H V C S Q T A Z`, absolute and relative), arcs included. Invalid data retains complete preceding segments; incomplete parameter sets are not emitted. |
| `<rect>` | ✅ | Either omitted radius uses the other radius; used radii are clamped to half the corresponding dimension. An explicit zero radius gives square corners. |
| `<circle>`, `<ellipse>`, `<polygon>`, `<polyline>` | ✅ | |
| `<line>` | ✅ | The default stroke is `none`; explicit, inherited and inline CSS strokes use normal paint resolution. |
| `<text>`, `<tspan>` | ◐ | Runs, `text-anchor`, font family/size/weight/style, solid fill, `textLength`/`lengthAdjust`, `xml:space`. Only the first `x`/`y`/`dx`/`dy` value is used. Not supported on text: stroke, `opacity`, `fill-opacity`, `letter-spacing`, `word-spacing`, `dominant-baseline`, `text-decoration`, `rotate`, gradient fill. |
| `<textPath>` | ❌ | Its content is skipped. |
| `<a>` | ◐ | Children render; the link's own `transform` and attributes are ignored. |
| `<use>`, `<symbol>`, `<defs>` | ✅ | `href` and `xlink:href`, `x`/`y`, the symbol's `viewBox` and `preserveAspectRatio`, external `file.svg#id`, a cycle guard, nesting up to 16 levels. |
| `<image>` | ◐ | PNG, JPEG and SVG sources; GIF and WebP do not load. A raster image's `x`/`y` are ignored, so it is drawn at the user-space origin; relative `href`s resolve against the process working directory instead of the document; `preserveAspectRatio` alignment and `none` are ignored for file sources. |
| `<switch>` | ❌ | Every child renders. |
| `<foreignObject>` | ❌ | Nothing renders. |
| `<style>` inside SVG | ◐ | Type, `.class`, `#id`, compound and `*` selectors. No selector lists, combinators or `!important`. |
| `<title>`, `<desc>` | ✅ | Not painted, as in browsers; no tooltip. |
| `<linearGradient>`, `<radialGradient>` | ◐ | See the paint-server table below. |
| `<pattern>` | ◐ | `patternUnits`, `x`/`y`/`width`/`height`, `patternTransform`. No `viewBox`, `patternContentUnits` or `href`. |
| `<clipPath>` | ◐ | The union of basic shapes, or only the first `<path>`. Child transforms, `clipPathUnits="objectBoundingBox"` and `style="clip-path:…"` are ignored, and the clip ignores the clipped element's own transform. |
| `<mask>` | ◐ | Luminance of solid-filled shapes. Gradient masks are ignored (the element paints unmasked), and black shapes cannot cut holes. |
| `<marker>` | ◐ | `marker-end` on `<path>` only, with `orient`, `refX`/`refY` and `markerUnits`. No `marker-start` or `marker-mid`, no markers on `<line>`, `<polyline>` or `<polygon>`, and the marker's `viewBox` is ignored (all verified). |
| `<filter>` | ◐ | Only the first `<feGaussianBlur>`, and only on shapes and paths (not on `<g>`, `<text>`, `<image>` or `<use>`). |
| Other filter primitives | ❌ | `feDropShadow`, `feOffset`, `feMerge`, `feColorMatrix` and `feFlood` are ignored (a classic drop-shadow chain blurs the source graphic instead); there is no code for `feBlend`, `feComposite`, `feMorphology`, `feTurbulence`, `feDisplacementMap`, `feImage`, `feTile` or the lighting filters. |
| SMIL animation (`<animate>`, `<animateTransform>`, `<set>`) | ❌ | The static state is drawn. |

### 15.2 Attributes, styling and paint servers

| Feature | Status | Notes |
|---|---|---|
| `transform` attribute | ✅ | All transform functions. The CSS `transform` property on SVG child elements is ignored. |
| Colours | ✅ | Hex, `rgb()`/`rgba()` including percentages, `hsl()`/`hsla()` with number/degree/radian/gradian/turn hues, and CSS named colours including `rebeccapurple`. SVG uses the shared CSS color parser. |
| `currentColor` | ✅ | Including a CSS `color` set on the `<svg>` element. |
| `context-fill`, `context-stroke` | ❌ | Render black. |
| `fill="url(#gradient)"` | ◐ | Explicit linear/radial gradient fills work on paths and basic shapes, including polygons and polylines, with default `objectBoundingBox` units or `userSpaceOnUse`. Cubic paths use curve extrema for geometry bounds. A gradient fill inherited from a `<g>` still renders black. |
| `stroke="url(#gradient)"` | ❌ | Renders solid black. |
| `gradientUnits` | ✅ | |
| `gradientTransform`, `spreadMethod` (other than `pad`), gradient `href` templates, `fx`/`fy`/`fr` | ❌ | A template reached through `href` renders nothing. |
| `stop-color`, `stop-opacity` | ◐ | Work as attributes; `stop-color` given in `style=` renders black. |
| `fill-rule` | ✅ | |
| `clip-rule` | ❌ | |
| `opacity`, `fill-opacity`, `stroke-opacity` | ◐ | Work on shapes; ignored on text. |
| `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `stroke-dashoffset` | ✅ | |
| `stroke-dasharray` | ◐ | Zero-length entries are dropped, which breaks the `0 n` dotted-line idiom; percentages are read as user units. |
| `stroke-miterlimit`, `vector-effect`, `paint-order` | ❌ | |
| `text-anchor` | ✅ | |
| Presentation attributes vs CSS | ✅ | `style` attribute, then `<style>` rules inside the SVG by specificity and order, then presentation attributes, as in browsers. |
| **Page stylesheets reaching SVG content** | ❌ | Rules in the HTML page's own stylesheets do not reach elements inside an inline `<svg>`; only the root `<svg>`'s `color`, `fill`, `stroke` and `stroke-width` are taken from the page. Put SVG styling in a `<style>` inside the `<svg>` or in attributes (verified). |
| Font properties on `<g>` | ◐ | Presentation attributes are inherited; CSS `style` values are not. |
| `display="none"` | ✅ | |
| `visibility` | ❌ | Ignored. |
| Units | ◐ | `px`, `pt`, `pc`, `mm`, `cm`, `in`. `em` and `ex` are fixed at 16 and 8 px; `%` is read as a plain number (`width="10%"` is 10 user units). |

## 16. Image Formats

The image decoders linked into `lambda` are libpng, libjpeg-turbo and giflib (`lib/image.c`); SVG goes through Radiant's own SVG renderer, and Lottie through ThorVG.

| Format | `<img>`, CSS images | Notes |
|---|---|---|
| PNG | ✅ | Including `data:` URIs. |
| JPEG | ✅ | EXIF orientation is ◐: for orientations 5–8 the box is swapped but the pixels are not rotated, so the image is stretched. |
| GIF | ✅ | Animated in `lambda view`; exports show the first frame. |
| SVG | ✅ | See [§15](#15-svg). |
| WebP | ❌ | Shown as a placeholder. |
| AVIF, BMP, TIFF, ICO | ❌ | Render blank. |
| Lottie (`.json`, `.lottie`) | ◐ | Played in `lambda view`; a blank 300 × 300 box in `layout` and `render`. |
| Remote `http(s)` images | *not verified* | A network path exists (`radiant/surface.cpp`). |

| Image feature | Status | Notes |
|---|---|---|
| `width`/`height` attributes, intrinsic size, CSS `aspect-ratio` | ✅ | |
| `object-fit`, `object-position` | ✅ | In raster output; SVG and PDF output ignore them. |
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
| CSS `clip-path` | ✅ | ❌ ignored | ❌ ignored |
| 2D transforms | ✅ | ✅ | ✅ |
| 3D transforms, `perspective` | ✅ | ❌ the perspective part is dropped | ❌ same |
| `<img>` (raster) | ✅ | ◐ linked as `file://` URLs, not embedded; `data:` images dropped; `object-fit` ignored | ✅ embedded; `object-fit` ignored |
| `<img>` (SVG) | ✅ | ◐ linked | ❌ dropped |
| CSS `background-image` | ✅ | ◐ a `<pattern>` referencing the original relative URL; a non-repeating image without `background-size` gets zero size | ❌ dropped |
| Inline `<svg>` | ✅ | ◐ copied through verbatim: page CSS sizing is lost, and an undeclared `xlink:` prefix makes the file invalid XML | ◐ rasterized at 1× on an opaque white box |
| Hyperlinks | n/a | ❌ | ❌ no link annotations |
| Pages | one image | one image | ❌ one page the size of the content (1 CSS px = 1 pt); `@page`, page breaks and print media are ignored |
| Page background | white | white | transparent; the `body` background does not fill the page |
| Size when no `-vw`/`-vh` is given | PNG: laid out at 1200 px wide, canvas fits the content plus 50 px. **JPEG: a fixed 1200 × 800 crop** | laid out at 1200 px, canvas fits the content plus 50 px | laid out at 800 px, page fits the content plus 50 px |
| `-s` / `--pixel-ratio` | ✅ e.g. `-s 2` doubles the pixels | ❌ `-s` enlarges the canvas without scaling the content; `--pixel-ratio` is ignored | ❌ same |
| Colour management | PNG: RGBA without colour-space chunks; JPEG: quality 85, no ICC profile | — | DeviceRGB, no ICC profile |

For print-quality PDF today, render to PNG at a higher density (`-s 2`) or use the SVG output, and keep text to fonts the reader has installed.

## 18. Known Limitations

The gaps most likely to change how a real page looks, with the section that has the detail. Each design document in [dev/radiant/](dev/radiant/RAD_00_Overview.md) ends with a *Known Issues* section for engine-level detail.

**Text**

- No text shaping: no ligatures, and Arabic, Indic and other complex scripts render incorrectly ([§11](#11-text-and-fonts)).
- Bidirectional reordering depends on the optional FriBidi library; without it, right-to-left paragraphs keep their runs in left-to-right order ([§11](#11-text-and-fonts)).
- Vertical writing modes lay out correctly but paint their text horizontally ([§11](#11-text-and-fonts)).
- `text-overflow: ellipsis` draws no ellipsis, and `text-shadow` is dropped unless the element also sets a font property ([§11](#11-text-and-fonts)).

**CSS**

- Cascade layers, `@container`, CSS nesting, `@scope` and `@property` have no effect, and media-query range syntax such as `(width >= 600px)` always matches ([§4](#4-at-rules)).
- A stylesheet `!important` beats an inline `!important`, a selector list takes the specificity of its first matching selector, and `:nth-of-type()` counts siblings of every type ([§3](#3-css-selectors), [§5](#5-cascade-inheritance-and-custom-properties)).
- `lab()`, `lch()`, `oklab()`, `oklch()`, `hwb()`, `color()`, `color-mix()` and `light-dark()` render black; math functions other than `calc()`, `min()`, `max()` and `clamp()` evaluate to 0; newer units such as `dvh`, `cqw` and `cap` are read as pixels ([§6](#6-values-units-and-functions)).
- Some shorthands are not applied: `background` with an image `url()`, `animation`, and `border-image`. Use the longhands ([§7](#7-css-property-coverage)).
- `width: initial` and `width: unset` (and the same on `height`) give 0 instead of `auto` ([§5](#5-cascade-inheritance-and-custom-properties)).
- Grid track sizes in `em`, `rem` and other non-pixel units are read as pixels, and `grid-auto-flow: dense` is not supported ([§8](#8-layout-modes)).

**SVG and images**

- Gradient fills on `<path>`, `<polygon>` and `<polyline>` paint nothing with the default `objectBoundingBox` units, and gradient strokes paint black ([§15](#15-svg)).
- Rules in the page's stylesheets do not reach elements inside inline SVG; style SVG with attributes or a `<style>` inside the `<svg>` ([§15](#15-svg)).
- SVG filters are limited to `feGaussianBlur`, markers to `marker-end` on paths, and masks to solid-filled luminance masks ([§15](#15-svg)).
- WebP, AVIF, BMP, TIFF and ICO images do not load ([§16](#16-image-formats)).

**Output**

- PDF output uses the built-in Helvetica, Times and Courier fonts only and garbles non-ASCII text; it has one page, no links and no CSS background images ([§17](#17-output-targets)).
- SVG and PDF output lose CSS `clip-path`, `dashed`/`dotted` borders and conic gradients; SVG output also ignores overflow clipping and `visibility: hidden`, and links images instead of embedding them ([§17](#17-output-targets)).
- JPEG output without `-vw`/`-vh` is a fixed 1200 × 800 crop rather than sized to the page ([§17](#17-output-targets)).
- `lambda render` ignores CSS animations, while `lambda layout` samples them at time 0 ([§13](#13-transitions-and-animations)).

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
