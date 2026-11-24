# Lib and Input Fixed Issue Ledger

Archive of resolved and fixed issues owned by `lambda-input` and its shared
`lib` code. Stable issue IDs and evidence are retained here. Open issues
remain in the [active issue ledger](Lib_Input_Issue_Ledger.md).

## Fixed issues

<a id="li-114"></a>

### LI-114 — Native JSX and LaTeX writers silently omitted large strings

**Fixed 2026-11-24.** The [JSX writer](../lambda-input/lambda/format/format-jsx.cpp)
omitted text above 10,000 bytes and emitted empty braces for JavaScript
expressions at or above 10,000 bytes. The
[native LaTeX writer](../lambda-input/lambda/format/format-latex.cpp)
omitted string values at or above 65,536 bytes, leaving empty command arguments.

The native writers removed all three cutoffs under D9.1.1v4. The user directed
that memory pressure and invalid storage be handled through overall memory
management (D5.2.1). [LI-113](#li-113) and
[LR09-31](<Lambda_Issue_Ledger (fixed).md#lr09-31>) record the preceding
HTML/XML repair.

Three [native formatter tests](../lambda-input/test/test_format_markup_gtest.cpp)
reproduce the omissions at the first rejected lengths. All three pass after
the fix and verify the full payload, JSX escaping and closing syntax. The
full markup executable passes 47/47 checks. Evidence:
`temp/li-114/before.log` and `temp/li-114/markup.log`.
The loopback-enabled Input baseline reports 5,363 passed, 3 skipped, and the
same three failures recorded for LI-113: the Bootstrap CSS round trip and
two native DOM checks. See `temp/li-114/input-baseline.log`.

<a id="li-113"></a>

### LI-113 — HTML and XML silently omitted large text and attributes

**Fixed 2026-11-24.** The shared native escape helper (D9.1.1v4) dropped
text above 1 MiB and attribute values above 32 MiB. At 1 MiB plus one byte,
both Lambda tiers returned a successful string with the text missing and
a later paragraph intact. The guard used size as a proxy for invalid
storage. The user directed that memory concerns be handled by overall
memory management (D5.2.1). Both formatter cutoffs were removed.

The owner regression serializes a 1.2 MB text node through both writers
and checks the full payload, escaping, and following paragraph. The markup
suite passed 34/34. The Input baseline and loopback reruns retained a
Bootstrap CSS round-trip failure and two native DOM failures.
See [LI-114](#li-114) for removal of the JSX and native LaTeX cutoffs.
<a id="li-112"></a>

### LI-112 — Explicit epoch-zero cookie expiry is stored as a session cookie

**Fixed 2026-11-24.** Cookiebot probes cookie support by writing a temporary
`-3` cookie and deleting it with `Expires=Thu, 01 Jan 1970 00:00:00 GMT`.
The jar used zero both for that parsed timestamp and for an absent expiry,
then required a positive timestamp before deleting an expired entry. The
probe cookie survived, and the unchanged SDK treated it as an existing
response instead of requesting its banner script. Negative past timestamps
also survived the same check.

The private header parser now reports expiry-attribute presence separately
from its timestamp. The store rejects expired cookies before any session
entry is retained. Valid Max-Age retains precedence, and the last valid
Expires value is selected when Max-Age is absent. Public cookie storage and
profile formats are unchanged. This stays input-owned under
**D9.1.1v4 / D9.1.3v2** and implements the existing
[RFC 6265 §5.2–5.3 contract](https://httpwg.org/specs/rfc6265.html#section-5.2).

All three new C++ regressions fail before repair. Seven focused cookie tests
pass afterward, including four existing hostname, file, HTTP-only and redirect
controls. Six real-HTTP DOM fixtures pass natively and in Chromium. The
unchanged Cookiebot SDK now has `hasResponse=false` and requests its `cc.js`
banner script; its loopback domain is correctly refused by the external
service. This establishes initialization progress, not a full-page clearance.
Typelane currently fails document loading in both renderers. Dayos still
retains its independent loading cover. No site source, profile cookie,
capture clock or deadline is overridden (**D10.1.7**).

Evidence: `temp/online-visual-round201/{cookie-unit-before.log,cookie-unit-after.log,
cookie-dom-after.log,browser-cookie-expiry.log,cookiebot-http-before.json,
cookiebot-http-after.json}`; `test/online/render_cookie_expiry.test.cjs`.

<a id="li-111"></a>

### LI-111 — Text-element fragments tokenize their contents as ordinary HTML

**Fixed 2026-11-24.** Fragment parsing selected a text mode only for enabled
`noscript`. Setting a script's `innerHTML` therefore stripped literal end tags,
created elements from embedded markup and decoded character references inside
JavaScript strings. A reduced script assigning `'</script>'` instead assigned
an empty string. The browser retained the exact source bytes.

Fragment parsing now shares the existing standalone lexer's tag-to-text-mode
helper. RCDATA contexts decode references; raw-text and plaintext contexts keep
them literal. No context start tag has been emitted, so a context end tag stays
text. The existing document parser's raw-text treatment of scripts is retained;
the separately declared, unimplemented escaped script-data states are not claimed
as supported. Input remains runtime-free (**D9.1.1v4 / D9.1.3v2**).

The prior qualified release fails both new script `innerHTML` regressions and
passes the `textContent` control. All **36 focused checks pass** after repair:
11 native HTML tests covering ten text contexts, one lexer/tree agreement test,
and 24 rendered script/Range controls. No baseline repetition or live-page
clearance is attributed to this bounded repair. Lokalise's earlier inline parse
exception is absent in the new capture, but its missing consent panel remains
high severity. Evidence: `temp/online-visual-round194/fragment-before.log`,
`html-focused-final.log`, `sourcepos-focused.log`, `dom-focused.log`,
`script-preparation-focused.log` and `selected-network/`.

<a id="li-110"></a>

### LI-110 — DOM attribute values lose bytes after embedded NUL

**Fixed 2026-11-24.** The DOM attribute cache treated values with the same
prefix before U+0000 as equal. DOM attribute storage, cache keys, and CSS
attribute comparisons now carry the byte length, under S2.5.9v2 and
D9.1.1v4. The core preserves the lengths for synthetic and namespaced
attributes as well. `test/test_nul_text.py` writes two values that differ
only after U+0000 and verifies that each reads and matches independently.

<a id="li-109"></a>

### LI-109 — XML formatter emits an invalid zero character reference

**Fixed 2026-11-24.** `format("a\u0000b", 'xml')` used to emit
`<root>a&#x00;b</root>`. S2.5.9v2 requires a format error. The
XML writer checks the lengths of serialized names and text,
including raw script, comment and processing-instruction text.
Nested output buffers share the failure and no partial result
reaches the caller. The regression checks scalars, attributes,
raw text, map keys and nested maps.

<a id="li-108"></a>

### LI-108 — Text adapters truncate at embedded NUL

**Fixed 2026-11-24.** Under S2.5.9v2 and D9.1.1v4, a zero byte in a
Lambda string must survive length-delimited handling. YAML decoded
`"before\0after"` but passed only the prefix to MarkBuilder. YAML
and JSON key output also used C-string lengths. The parser and
formatters now carry the scalar and key lengths. YAML writes a zero
byte as `\0`. Legacy parsers that consume C strings report a literal
zero byte in the source before parsing. Raw text and binary readers
take the full input length. The host resource resolver rejects NUL
in a textual path or URL before passing it to native code.
The JSX raw expression and MDX passthrough writers also copy full spans.

Coverage lives in `test/lambda/string_embedded_nul.ls` and
`test/test_nul_text.py` in the Lambda checkout.


<a id="li-107"></a>

### LI-107 — Strict XML text validation rescanned the document suffix

**Fixed 2026-11-23.** Input's strict XML path (D9.1.1v4, D10.1.4) searched
from each character-data run to source EOF for the forbidden CDATA terminator.
A large Word body containing many separate text runs paid quadratic scan cost.
The existing bounded string search now examines the current run only. Strict
XML still accepts valid CDATA and reports a literal terminator in character data.

The isolated 2.44 MB XML fixture took about 1.82 s before the fix and 19.0 ms
median after it, including release process startup. The preview workload and
raw evidence are recorded in [the preview benchmark](impl/Lambda_Impl_Preview.md#6-release-benchmark-at-128--128).

<a id="li-106"></a>

### LI-106 — Failed functional selectors retain a broader compound prefix

**Fixed 2026-11-23.** Shared parser ownership follows **D9.1.1v4 /
D9.1.3v2**. A failed functional pseudo could consume its closing parenthesis.
The compound parser then retained its valid prefix and the rule parser saw
an opening brace, accepting the broader selector. For example,
`.page:has(.marker,:unknown-pseudo)` became `.page` and applied display:none.
The same recovery path amplified LI-105: Anam's intended cookie-only
`body > div:has(.--framer-cookie-banner-container)` hid every body div,
including its entire main content.

Every legal compound terminator is checked before parsing a simple selector.
Failure at that point now rejects the compound; forgiving :is()/:where()
argument parsing retains its existing isolated-branch handling. This follows
[Selectors Level 4 §3.9](https://www.w3.org/TR/selectors-4/#invalid) and
[§4.5](https://www.w3.org/TR/selectors-4/#relational); invalid :has() and :not()
arguments cannot silently widen the outer selector.

Three permanent render controls fail before repair and pass afterward,
covering valid double-hyphen membership, insertion/removal and failed
:has()/:not() recovery. All 20 selector-group unit tests pass, including
nine valid identifier shapes and five invalid prefixed functions. All 32
focused render checks pass. At the milestone, dynamic layout passes 630/630;
static layout passes 3851/3852, retaining the previously recorded
`dom_view_role_nested` failure at 44.44% element pass rate. No threshold or
reference is changed. Evidence: `test/online/render_selector_identifiers.test.cjs`,
`lambda-input/test/css/test_selector_groups.cpp` and
`temp/online-visual-round189/`.

Anam About's main gallery and heading return and clear major review. Anam
Home's portrait cards and consent panel return, but its main animated
headline/actions remain absent and it retains major review. Its Chromium
CSSOM still has opacity 0.001 while presentation shows the text; automatic
animation presentation remains unqualified here. No whole-page clearance
is inferred from the lower coarse score alone.

<a id="li-105"></a>

### LI-105 — Double-hyphen class identifiers are rejected by selector parsing

**Fixed 2026-11-23.** Input owns the shared CSS grammar under **D9.1.1v4 /
D9.1.3v2**. The tokenizer's CUSTOM_PROPERTY category remains an identifier
token in selector grammar, but class parsing accepted IDENT only. Valid
`.--marker` therefore failed DOM selector queries; inside Anam's :has()
rule it also exposed LI-106's broader-prefix recovery defect.

A shared identifier predicate now serves class/type selectors, qualified
names, attribute names and descendant-start detection. It preserves the
existing tokenizer and escape decoding rather than adding a class-prefix
exception. [CSS Syntax §4.3.9](https://www.w3.org/TR/css-syntax-3/#would-start-an-ident-sequence)
and [Selectors §6.6](https://www.w3.org/TR/selectors-4/#class-html) define the
relevant identifier/class grammar. No runtime, native, vendor or public
layout data change is required. Qualified input/render evidence and limits
are recorded under LI-106. This no longer blocks the dependent Anam
cascade repair; other animation/media gaps remain independent.

<a id="li-103"></a>

### LI-103 — Legacy gap aliases retain independent property identities

**Fixed 2026-11-22.** Shared CSS parsing and ownership follow **D5.5.1 /
D9.1.1v4 / D9.1.3v2**. Getsafe's navigation declares grid-column-gap:.25rem
inside each inline-block menu item. Final flex layout applies the 4px gaps,
but early intrinsic queries look up column-gap and find no declaration.
The wrapper loses 8px and wraps its second item, adding 89px to the header.

The shared canonical-property table now maps grid-gap, grid-row-gap and
grid-column-gap to their standard identities, as required by
[CSS Box Alignment §8.4](https://www.w3.org/TR/2025/WD-css-align-3-20250311/#gap-legacy).
The existing declaration normalizer handles every mapped alias, including
single declarations, stylesheets and nested declaration runs. Consumers and
CSSOM reuse that identity for intrinsic queries, final layout and priority.
The existing WebKit compatibility grammar is retained. No layout-specific
alias fallback, page selector or public data-layout change was introduced.

The four-case reduction passes standard px/rem/variable controls before the
repair, but the legacy spelling produces 192px instead of 200px and wraps.
All eight permanent native cases now agree with Chromium, including the
shorthand, variables, source order and important priority. All 16 focused
parser tests pass; the normalization table covers the three gap aliases and
existing WebKit cases. The unchanged Getsafe DOM/CSS replay restores its
navigation row and hero position. Its photograph and consent typography
remain unresolved; no whole-page clearance is claimed.

Evidence: `temp/online-visual-round178/gap-probe.log`,
`gap-alias-native.log`, `gap-alias-browser.json`, `input-parser-tests.log`,
`getsafe-after/native.png` and
[comparison round159](../test/online/comparison/repairs.html#round-159).
No baseline was repeated for this focused repair; the user cleared the goal
and requested wrap-up after the root cause was isolated.

<a id="li-102"></a>

### LI-102 — Pending resource polling starves later completions

**Fixed 2026-11-22.** Document resource ownership follows **D5.5.1 /
D9.1.1v4 / D9.1.3v2**. dom_document_has_pending_work returned immediately
when the first registered callback reported pending. These callbacks also
deliver finished native work, so an outstanding lazy image could prevent
a completed eager sibling from publishing its image/load event.

The walk now polls every registered callback and accumulates the pending
result. No resource ownership or public data layout changes. RI-209's
lazy-sibling fixture holds an optional image past the eager completion and
checks that the eager element event precedes window.load while the optional
request remains outstanding. All17 lifecycle,41 discovery and nine late-image
controls pass; the dynamic layout inventory remains630/630. Evidence:
`temp/online-visual-round175/resource-focused-final.log`, `load-browser/results.json`
and `baseline-js.json`. Full pending-callback conformance is not claimed.

<a id="li-101"></a>

### LI-101 — Fragment parsing ignores table context and accepts stray table structure

**Fixed for the qualified HTML profile, 2026-11-22.** Input owns HTML parsing
under **D9.1.1v4 / D9.1.3v2**. Fragment creation put every HTML context in body
mode. Its generic start-tag fallback admitted tr/td and other table structure,
so body fragments incorrectly retained those elements. Simply rejecting them
also broke legitimate table contexts.

HTML fragments now keep only the synthetic html root on the open stack;
reset-insertion-mode substitutes the real context at its bottom. Table,
section, row, caption and colgroup modes reuse existing tree construction.
Closing context tags cannot pop the synthetic root. Body mode rejects the
specified stray table start tags through its existing error path. Ordinary
fragment continuation and html-context head/body creation stay covered.

All 85 HtmlParserTest unit cases pass, including new body-rejection and
seven-context controls. Six AST/MIR contextual-fragment groups, five nearby
DOM goldens and a Lambda-only Range golden pass in the dependent host.
Before repair the Range-only candidate failed all three body-fallback
assertions. Evidence: temp/online-visual-round167/input-html.log,
range-ast-qualified.log, range-ast-final.log, range-mir.log and
nearby-results.json. No claim of full HTML fragment, XML or MathML conformance.

<a id="li-100"></a>

### LI-100 — Native pathname mutation leaves href stale and skips path normalization

**Fixed for pathname writes, 2026-11-22.** URL parsing belongs to Input under
**D9.1.1v4 / D9.1.3v2**. url_set_pathname replaced one raw string without
updating href. It also treated query/fragment delimiters as path text without
encoding and skipped special-scheme slash and encoded-dot handling.

The setter filters URL tabs/newlines, normalizes special backslashes, applies
the path encoding set and reuses url_normalize_path. The normalizer first
canonicalizes only complete literal/percent-encoded dot segments; escaped
filename dots and empty segments remain intact. The setter parses a coherent
replacement and transfers it into the existing carrier atomically, preserving
query and fragment. Opaque paths retain their existing state.

The 13-row focused setter table fails before repair. All 85 native URL unit
cases pass afterward (38 extra/safety and47 core). JS pathname binding and
its AST/MIR/Chromium controls are recorded in JS13-L49. Chromium 148 differs
from native Input and Node for a Windows file-URL drive assignment with a
vertical bar; that control is explicitly unqualified for browser agreement.
Other component setters and absolute-constructor path normalization are not
qualified by this repair. No transfer/source limit or capture clock changes.
Evidence: temp/online-visual-round163/input-pathname-before.log,
input-extra-after.log, input-url-after.log and pathname-browser-final-trace.log.
[Setter](../lambda-input/lib/url.c) · [Shared normalizer](../lambda-input/lib/url_parser.c) ·
[Focused table](../lambda-input/test/lib/test_url_extra_gtest.cpp)

<a id="li-099"></a>

### LI-099 — Raw resource URL strings bypass serialization before curl

**Fixed 2026-11-22.** The network provider belongs to input under
**D9.1.1v4 / D9.1.3v2**. Readymag's absolute CSS font URLs contain literal
spaces. The multi backend passed the raw resource string to CURLOPT_URL,
so curl rejected the request before any transfer. The shared URL parser
already encodes spaces and UTF-8 and preserves existing percent escapes.

configure_transfer now parses the URL and gives curl its serialized href,
then releases the temporary Url after curl copies it. Invalid URLs and option
errors use the existing transfer failure path. Resource identity, admission,
cache keys, timeouts and source limits retain their existing contracts.

The new space/Unicode file-transfer regression fails before repair. All15
network unit cases pass afterward, including plain and already-escaped file
URLs, plus10 existing renderer resource/base-URL controls. Readymag's current
native page has zero malformed-URL errors and uses the downloaded sans font.
It still has major layout/compositing differences. A fresh1000ms Chromium
capture is transitional, so no whole-page clearance or metric improvement is
claimed. Canonical captures and the109-high inventory remain preserved.

Evidence: temp/online-visual-round160/network-test-before.log,
network-test-after.log, resource-focused.log and live/saas_landing_205/after/.
[Implementation](../lambda-input/lambda/network/curl_multi_backend.cpp) ·
[Focused regression](../lambda-input/test/test_network_scheduler_gtest.cpp)

<a id="li-098"></a>

### LI-098 — Bulk weak DOM validation repeats the ownership walk

**Fixed 2026-11-22.** With structural DOM ownership, one node retirement can
expire every outstanding weak lifetime stamp for the document. Pruning many
retained references then walked the owning forest for each cold address.
Wrapping a subtree for custom element reactions could repeat the same work.

Input shares a scratch membership snapshot within each bulk operation
under D5.5.1 and D9.1.3v2. New registrations and lifetime changes expire the
snapshot. A weak reference checks its expected node ID after ownership is
established. The native regression retires nodes while a snapshot is live
and verifies that only the surviving references resolve.
[Implementation](../lambda-input/lambda/input/css/dom_lifecycle.cpp) ·
[Native regression](../lambda-input/test/test_input_dom_native_gtest.cpp)

<a id="li-097"></a>

### LI-097 — Legacy WebKit box declarations need standard equivalents

**Fixed 2026-11-21.** D10.1.9 assigns normalization to CSS input under
D9.1.3v2. The retired display values and clamp declarations caused seven
baseline regressions in `wpt-css-inline` and `wpt-css-text`. The parser now
emits standard flex and clamp declarations. It converts a complete vertical
clamp block to standard flow layout and keeps declaration order and importance.

The original layout fixtures record 169 full matches for `wpt-css-inline` and
896 full matches plus 84 recorded partials for `wpt-css-text`. Both have zero
baseline regressions. Evidence is in `temp/radiant-test-fixes/layout-before.log`
and `layout-normalization-probe.log`.

<a id="li-096"></a>

### LI-096 — A sole double-position color is rejected as one gradient stop

**Fixed 2026-11-21.** Input owns CSS value validation under **D9.1.1v4 /
D9.1.3v2**. `css_gradient_stop_list_valid` counted comma-separated color
groups, rejecting `linear-gradient(#010a14 0% 100%)` even though its color
expands into two stops. Radial component validation also required two color
groups after a geometry prelude. The shared validator now counts expanded
positions and radial geometry accepts one group. Linear/conic leading hints
remain invalid; radial length preludes retain their separate geometry rules.
No color parser or alternate gradient implementation is introduced.

The native parser suite passes all124 tests, including13 valid declarations
and six malformed controls in the new regression. Twelve shared PNG cases
fail before the combined LI-096/RI-186 repair; all12 pass afterward and in
Chromium, with25 existing color controls also passing. The cases cover all
three gradient types, repeats, and forms with/without geometry preludes.
The [CSS Images color-stop rule](https://www.w3.org/TR/css-images-4/#color-stop-syntax)
defines a two-position color as two stops of the same color. A sole color
without two positions remains unsupported. Evidence:
`temp/online-visual-round150/`, `parser-before.log`, `parser-final.log`,
`gradient-before.log`, `gradient-after.log`, `browser-gradient-results.json`.
No remaining blocker for the repaired form; no broad baseline is repeated.

<a id="li-095"></a>

### LI-095 — Default document construction left the tag-name map uninitialized

**Fixed 2026-11-21.** The document owns its lazy tag-name map under **D5.5.1 /
D9.1.3v2**. With Input `5af070c`, a placement-construction probe over nonzero
storage left the `tag_name_overrides` pointer bytes untouched. Document teardown
passed that indeterminate pointer to `hashmap_free`, crashing
`SvgFilterTest.ComponentTransferUsesStraightChannelsAndRemappedAlpha` and
`SecondaryViewTest.PictureSelectionPrecedesFallbackAndPreservesDataUrlCommas`.
The member now starts null on ordinary C++ construction, as it already does on
the factory's zeroed storage. The probe passes with the initializer. All 250
vector and 180 selected paged-media tests pass. Evidence is in Lambda's
`temp/radiant-baseline-build-fix/`, including the probe and crash reports.

<a id="li-093"></a>

### LI-093 — Namespace lookup ignores an element's expanded name

**Fixed 2026-11-21.** Input owns the shared namespace resolver under
**D9.1.1v4 / D9.1.3v2**; DOM consumers share it under **D9.5.6v2**.
The old lookup searches declarations, returning null for an HTML element's
default namespace and for namespaced elements created without declarations.
A namespaced element's own prefix/URI must precede a conflicting declaration.
The separate parser namespace-binding helper remains unchanged.

The existing lookup now checks expanded names and actual XMLNS attributes in
ancestor order, stops at native fragment wrappers, retains reserved prefixes
and empty resets, and ignores literal HTML xmlns aliases. JS13-L37 publishes
Node.lookupNamespaceURI through the shared DOM catalog and dispatches node
kinds. There is no second namespace resolver or page-specific namespace value.

The combined repair passes four native/Chromium fixtures and13 existing
Node/URL/SVG controls; all630 baseline-js cases pass. The fixtures include
HTML/SVG/XML, declaration shadowing/removal, an own-name/declaration conflict,
null-namespace children in fragments, attributes, captured calls and conversion.
The four API fixtures fail before the combined repair; no separate before/after
Input unit count is claimed. Coda's consent-wrapper TypeErrors disappear,
leaving its explicit IndexedDB failure. Evidence: Lambda
`temp/online-visual-round139/`. Full XML and namespace-method conformance
remain outside this qualification. No budget, capture-clock or vendor change.


<a id="li-092"></a>

### LI-092 — Shared shorthand projection omits existing font components

**Fixed 2026-11-21.** Input owns shared CSS syntax/projection under
**D9.1.1v4 / D9.1.3v2**. The generic component projector handles grid,
container, gap and flex, but lacks font despite the existing font parser and
component decoder. Once a consumer selects a font declaration, it cannot
obtain its weight/style/family through the generic API.

The generic projector now delegates font to the existing
css_font_shorthand_longhand helper using property registry metadata.
There is no second grammar or registry change. RI-178 repairs the native
consumer's selection and variable-substitution path. A standalone initialized
API probe projects700, and the combined repair passes15 native/Chromium font
cases plus eight existing font/button controls. Seven original regressions
fail before the combined repair; the later-component guard already passes.
Broader font reset/system-font conformance is outside this qualification.
Evidence: Lambda `temp/online-visual-round137/`, `font-ast.log`,
`font-before.log`, `font-extra-before.log`, `focused-ua-final.log` and
`browser-font.json`.


<a id="li-091"></a>

### LI-091 — Absent shorthand selects unsupported property code zero and overrides valid CSS

**Fixed 2026-11-21.** The shared cascade remains Input-owned under
**D9.1.1v4 / D9.1.3v2**. Dock's product screen is ordinary HTML. Its authored
960px by603.85px grid became a narrow zero-height box. A small component
reproduces without the full page; removing the descendant all reset or applying
a later/inline size makes the original math work.

A native cascade probe isolates the defect. The stored WIDTH declaration has
higher specificity than ALL, and an explicit WIDTH/ALL component query selects
it correctly. The ordinary lookup additionally passes zero from
`css_property_cascade_shorthand` as its absent marker. A retained code-zero
`text-size-adjust:none` declaration then wins that candidate slot by later
priority, overriding valid dimensions with none. Other retained unknown values
can similarly reach unrelated consumers, including length resolution.

The shared candidate lookup now excludes source codes outside the real
property domain before AVL lookup. Unknown authored storage and CSSOM are
retained; the empty shorthand marker never participates in a real cascade.
No page-specific style, alias substitution, forced dimension or vendor patch.

A new native unit fails four selection assertions before repair. All48
style-node units and69 focused native rendering controls pass after it.
Three new paint controls fail before and pass after for text-size-adjust
none/auto/100%. The original component resolves its declared960 by603.85 size
afterward (778.46 by489.66 after its authored scale), instead of185.67 by0.
Whole-page qualification is recorded separately under RI-031. Evidence:
Lambda `temp/online-visual-round130/`. Source and capture budgets are unchanged.

<a id="li-090"></a>

### LI-090 — Absent properties repeatedly resolve the same all-only rollback

**Fixed 2026-11-21.** Shared cascade ownership follows **D9.1.3v2**; canonical
declaration storage retains **D5.2.3 / D5.5.1**. Dock resets descendants through
`all:revert`. A property with no own declaration or implicit shorthand repeatedly
walked the same all-only rollback chain because only present property nodes had
cached winners. A release reduction reproduces that repeated read cost.

When neither an own declaration nor a supported implicit shorthand exists,
ordinary reads without a borrowed fallback now call the existing ALL lookup.
That path initializes and invalidates its cached winner normally, including
cached null results. Authored-only, shorthand, local and borrowed candidates
retain their original selection. No new cache, allocation, cache key, layout
skip or epoch exemption is introduced.

All 47 style-node tests pass, including later global declarations, user-origin
rollback, local/shorthand overrides, removal and clear. The reduced release
workload (2000 losing declarations, an all rollback and 20000 absent-property
reads) takes 153.845ms CPU before and 0.222ms after. This is a reduction result,
not a live-page performance claim. The new unit fixture uses FLEX/FLEX_GROW
because that is an implicit projection supported by this raw tree; margin is
expanded before tree insertion and is not such a projection.
Evidence: Lambda `temp/online-visual-round129/`. Whole-page capture is
qualified separately under RI-031; source and capture budgets remain unchanged.

<a id="li-089"></a>

### LI-089 — Relative Oklab/Oklch backgrounds are rejected despite existing color/channel support

**Fixed 2026-11-21.** Shared CSS computation follows **D9.1.1v4 / D9.1.3v2**;
retained computed expressions remain Input/pool-owned under **D5.5.1**. Dock's
actual parent hero background is `oklch(from var(--color-blue-deep)
calc(l + .02) c h)`. Its rejection leaves white hero text on a white page.
The engine already had relative RGB/HSL channel math and absolute Oklab/Oklch
conversion, but its relative-function guard excluded the perceptual models.

A shared model table now supplies channel names and analogous missing
components. Oklab/Oklch sources preserve their components before channel math,
including values outside sRGB. Polar conversion is extracted from the existing
RGBA consumer and reused. Alpha inheritance, deferred currentColor, explicit
channel math and computed-model serialization share the existing path. No
authored color, page selector, clock or admission limit is substituted.

Two new parser groups and three native paint controls fail before repair.
Afterward **16 focused color/condition/parser tests and 41 native rendering
controls pass**, including RGB/HSL regressions, chained source conversions,
extended components, alpha, invalid channels/units, currentColor, custom
properties and live origin updates. An old negative case used newly supported
valid syntax; it now uses an actually invalid channel name instead.

The processing rules follow [CSS Color 5 sections4.1,4.7 and4.9](https://www.w3.org/TR/css-color-5/#relative-colors).
The current draft preserves extended component ranges and analogous missing
references. Chromium 148 instead clamps one extended L/C sample and resolves
bare missing references to zero; these differences are retained, not forced to
match. Other relative spaces, new gradient interpolation spaces and gamut
mapping remain outside this repair. Evidence: Lambda
`temp/online-visual-round128/`; whole-page outcomes are recorded separately.

<a id="li-088"></a>

### LI-088 — Component cascade selection rescans sorted losing declarations

**Fixed 2026-11-21.** Input owns the shared cascade under **D9.1.3v2**.
A release CPU sample of Dock identifies repeated
`style_tree_best_property_candidate` and cascade comparisons during nested
intrinsic measurement. That selector walked every losing declaration after
finding an eligible winner, although `weak_list_insert` maintains descending
cascade order and `winning_decl` precedes that list.

Each source now stops at its first eligible candidate, after comparing it with
the best result from other sources and any borrowed fallback. Rejected ceiling,
rollback and presentation candidates are still skipped. Cross-source rollback
recursion and mutation/cache ownership are unchanged; no new cache is added.

All46 style-node unit tests pass, including a new chained layer rollback
control across `color`/`all`, authored presentation filtering and borrowed
fallback. All38 metadata/inline-style/SVG native controls also pass. An isolated
release reduction with2000 declarations and20000 component reads takes138.318ms
CPU before and0.056ms after; this measures the reduction, not a live-page
speedup. Dock's fresh capture outcome is recorded separately under RI-031.
Evidence: Lambda `temp/online-visual-round127/`. No source/capture budget,
layout algorithm, vendor code or formal ruling changes.

<a id="li-087"></a>

### LI-087 — Stable inert presence always invalidates native style storage

**Fixed 2026-11-21.** Attribute storage remains Input-owned under
**D9.1.1v4 / D9.1.3v2**. The existing metadata writer accepted only data/ARIA
attributes. Rewriting an already present `inert` attribute therefore discarded
native style state even though its boolean presence stayed true. Shared
`dom_element_can_write_metadata` now includes that stable-presence case; the
host retains responsibility for lexical-value CSS dependencies and attribute
notifications. First enable and removal retain the ordinary path.

Four pre-repair controls fail with eleven layouts rather than one (three inert
entry points and unrelated null-namespace data metadata). The rebuilt release
passes 38 focused metadata, inline-style and SVG controls plus integer lint.
Changed inert selector values, first-enable focus suppression, removal,
namespace collisions, observers and callback-triggered layout remain covered.
No boolean state, admission limit, capture clock or vendor code is changed.
Evidence: Lambda `temp/online-visual-round126/`; host API parity is JS13-L32.

<a id="li-082"></a>

### LI-082 — Merged fragment geometry structure lacks its closing brace

**Fixed 2026-11-21.** The merged `DomFragmentGeometry` declaration omitted
its closing `};` before `DomToggleTaskTracker`. Subsequent declarations were
therefore parsed as nested members, producing template-specialization and
storage-class errors throughout Radiant consumers. Restoring that brace
preserves the intended extracted geometry structure and ownership under
**D9.1.1v4 / D9.1.3v2 / D5.5.1**. The Input, Radiant, runtime and host-adapter
release archives compile afterward, and the rebuilt host passes 35 focused
render/lifecycle controls. No field, layout algorithm or vendor source changes.
Evidence: Lambda `temp/online-visual-round120/`.

<a id="li-081"></a>

### LI-081 — Adopted nodes were walked after their source arena was freed

**Fixed 2026-11-20.** The native CSS/DOM adoption test crashed in
`dom_slot_release_manual_links` during destination teardown. Releasing
the source document resource freed the adopted nodes while the destination
registry still held their addresses. Under D5.5.1, teardown now unlinks
manual slot relationships before releasing document resources. The
destination registry remains live for resource cleanup. The adoption
test verifies the transferred node and its expanded namespace identity,
and all 139 native CSS/DOM integration tests pass.

<a id="li-080"></a>

### LI-080 — Canonical declarations repeat stylesheet source paths

**Fixed 2026-11-18.** The CSS epoch owns the shared path allocation
and includes its bytes in the document's CSS memory accounting
(**D5.2.3 / D5.2.4**).

The jQuery UI cascade retains 78 recipes and 412 immutable declarations in
both load and clean recascade. Resolving the corpus symlink adds seven bytes
to their stylesheet URLs and 2,884 bytes to canonical memory. CSS-role live
bytes reach 374,626 against a 373,940-byte ceiling.

The epoch now retains one exact copy of each source path while any snapshot
uses it. Each declaration keeps its original source URL and line number.
The six-page load and recascade memory gate passes on the optimized build.
The jQuery UI sample retains 329,435 CSS-role bytes after load and recascade.

<a id="li-079"></a>

### LI-079 — Select insertion modes discard customizable select content

**Status:** FIXED for tree construction, 2026-11-18. Input owns HTML parsing
under **D9.1.1v4 / D9.1.3v2**.

`lambda convert` dropped a select's `<button>`, `<selectedcontent>` and rich
option descendants. The old select insertion modes ignored these tokens.
The [HTML in-body rules](https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inbody)
now handle select, option, optgroup and recovery through the normal stack.
Foster parenting moves only a node whose insertion target is a table
structural element, so rich descendants stay inside a fostered select.

The native parser, negative parser and html5lib suites passed 83, 126 and
364 tests respectively. The split Lambda CLI roundtrip preserved the custom
button, selectedcontent and option markup. Selected-content mirroring and
picker presentation belong to the downstream DOM and layout implementations.

<a id="li-078"></a>

### LI-078 — Incomplete Lab computation selects a bright fallback hero

**Status:** FIXED, 2026-11-18. Shared color/value computation belongs to Input
(**D9.1.1v4 / D9.1.3v2**). Computed expression snapshots retain owned values
under **D5.5.1**. This adds a bounded existing color-engine increment, not a
native feature-query exception.

Rogo's saved stylesheet gates its subtle sRGB gradient behind a Lab color-mix
feature query. Ordinary sRGB mixing and gradients work in an isolated control.
The authored fallback uses opaque white and produces the large bright hero
glow. The engine correctly rejected the feature test because only sRGB/Oklab
mixing was implemented; its existing Lab type and library conversion were not
connected to computed parsing, mixing or model-preserving transport.

Absolute Lab channels/percentages and alpha now use the existing computed color
path. Rectangular Lab mixing shares the existing weight, currentColor and
premultiplied-alpha logic. Lab components stay in D50 until used-color conversion.
A shared Bradford adaptation helper serves both conversion directions; the
existing color-name implementation now matches its public declaration and is
reused by computed trees and formatting. No duplicate color table is added.
See [CSS Color 4 §8 and §19](https://www.w3.org/TR/css-color-4/#lab-colors)
and [CSS Color 5 mixing](https://www.w3.org/TR/css-color-5/#color-mix).

LCH/polar interpolation, relative colors, missing-component interpolation,
additional spaces, gamut mapping and Lab interpolation inside gradients remain
outside this increment. A Lab gradient feature query remains unsupported.

Five native paint cases fail before the repair; all five are confirmed by
Chromium. After the repair, all 15 focused color/condition/stylesheet checks
pass. Input's focused color/perceptual parser checks pass 10/10, including
computed transport and serialization; shared color-library checks pass 10/10,
including extended-sRGB conversion round trips. The new Lab transport check
compares lightness to its specified value because converting white can carry
reference-white roundoff above the Lab syntax bound; other components retain
exact values. No existing assertion or baseline tolerance changes.

Rogo's identical saved-source error falls from 13.28% to 1.90%. Its normal live
hero background is repaired (3.97% coarse error), but navigation, the remainder
of its headline, copy and action are still absent. A manual high review keeps
those omissions visible. No live clearance, capture delay, reference update or
full baseline repeat is claimed. The preceding round79 Radiant milestone is
retained. Reproduction, focused logs and PNGs are under the Lambda checkout's
`temp/online-visual-round81/`.

<a id="li-077"></a>

### LI-077 — Duplicate bounded parser after merge

**Status:** FIXED, 2026-11-18. **Authority:** D9.1.1v4. A merge introduced
both implementations of `input_parse_value_n`, causing a C++ redefinition
error and blocking the dependent sync build. One implementation remains; it
preserves bounded JSON/Mark input, embedded-math options, Input ownership and
parse-error tracking. `InputDomNativeTest.ScopedValueParserUsesExplicitLengthAndRestoresItsOwner`
passes after rebuilding. Evidence: Lambda `temp/sync-test-phase2/input-parser-test.log`.

<a id="li-076"></a>

### LI-076 — Constrained error contracts are not admitted by the shared core

**Status:** FIXED, 2026-11-18. **Authority:** S11.4.2v3, S11.4.6v4,
D9.1.1v4. **Lambda blocker:** the two constrained-error test failures resolved.

On Input `6e22903`, `lambda/core/lambda-data.cpp::lambda_type_accepts_error`
followed optional/union operands but skipped constrained bases. Lambda's
unchanged `constrained_error_return` golden reported E312 for `int | First`,
where `First = error that ~.message == "first"`, plus E200 failures. The same
missing case affected `lambda_type_accepts_null`. Both shared walks now recurse
through constrained bases; no runtime dependency or predicate-enforcement change
was added. Existing compiler subtype/normal-admission logic needed no change.

The unchanged golden passes AUTO/interpreter/JIT. Both explicit tiers also pass
with forced collection on every allocation and freed-memory poisoning. Native
`TypeContractMetadataTest.ConstrainedErrorChannelsFollowTheirBase` now covers
nested constraints around optional error bases and explicit internal top
exclusions; the full runtime/error suite passes 340/340. Eleven related
constrained/error/admission goldens pass in each explicit tier.
The full Input baseline passes 5,324 native tests with three skipped cases in
109 runner summaries. The full Lambda baseline passes 7,604/7,604, including
both originally failing tests.

The prior broad Lambda repair remains recorded at
`Lambda_Issue_Ledger (fixed).md#reader-constrained-error-return-fixed`.
This recurrence was discovered during sync qualification and is independent of
sync behavior. Original reproduction evidence remains under Lambda
`temp/sync-test-performance-impl/constrained-{before,after,previous-release}.err`;
repair evidence is under `../temp/constrained-error-fix/`.

<a id="li-074"></a>

### LI-074 — Closing a template in the head swallows following document metadata and body

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for existing HTML
head-template parsing. Input ownership: **D9.1.1v4 / D9.1.3v2**.

The in-head handler inserted templates but had no matching end-template branch.
Following base, linked stylesheet and body nodes remained inside the template.
The existing close-until-template and reset-insertion-mode helpers now close
that scope, and resetting recognizes the containing head. This does not claim
complete HTML template insertion-mode conformance.

`ClosingHeadTemplatesPreservesFollowingMetadataAndBody` covers simple and nested
head templates, then verifies sibling base/link metadata and the real body.
It and the related context-fragment and script-enabled-noscript parser tests
pass (three total). Final host controls also pass: six base/resource fixtures,
inert metadata with active foreign SVG controls, and two persistent HTTP
regressions. Evidence: `../temp/online-visual-round62/`.


<a id="li-073"></a>

### LI-073 — Element discovery loses completion after byte-only prefetch

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for the existing
single-owner resource path. Network ownership remains in Input under
**D9.1.1v4 / D9.1.3v2**; no runtime dependency or capture-clock change.

Initial stylesheet prefetch registers RESOURCE_PREFETCH with no element. The
later typed load returns that same URL resource without adopting its consumer.
Thus both successful and failed initial links lose load/error events; a
print-only sheet never runs its authored media activation handler. A preload
link discovered without the initial stylesheet prefetch is an effective control.

A byte-only prefetch now adopts a discovered typed element consumer. If its
completion was already processed, it reenters the existing main-thread queue;
settled optional failures retain error delivery. Request identity, priority,
statistics and transfer bytes remain shared. Ownerless byte consumers retain
their existing behavior. Radiant reuses the initial parsed sheet (RI-107).

Verification: all **six NetworkResourceManager tests pass**, including queued
and already-processed cached completion, once-only delivery and unchanged
resource counts. The host's four initial-link HTTP regressions failed before
and pass after; the adjacent ten resource-discovery/completion tests also pass.
Chromium headless shell independently confirms load/error ordering, print-sheet
activation and empty-sheet completion. No broad baseline was rerun.

The existing resource API still has one element owner per URL; this repair does
not introduce a multi-consumer subscription model. Failed-sheet CSSOM objects
also differ from Chromium (which retains an empty sheet for this 404 fixture);
that small CSSOM difference is outside the completion-event repair.


<a id="li-072"></a>

### LI-072 — Font-family lists reject pending substitutions and their fallback families

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for the shared
font-family declaration/value path. Input owns token parsing under
**D9.1.1v4 / D9.1.3v2**. Existing document/variable ownership and **D10.1.7**
capture clocks are unchanged; no formal ruling changes.

Limitless supplies its Greycliff families through a custom property and uses
font-family:var(--font-greycliff),system-ui,... . Native CSS.supports returns
false and the page inherits serif/Times New Roman despite the custom property
being present. Literal lists and a whole-value var() already work. The shared
family token validator admits literal strings/identifiers only and runs before
variable substitution; the duplicate property-value parser also treats an
initial function as the entire value, losing later family groups.

Both font-family entry points now share family grouping. Pending substitutions
use the existing declaration component-sequence parser, extracted without a
third implementation; nested fallback commas stay inside their function.
Literal family grammar is checked after substitution, preserving invalid-value
behavior and quoted-name separation. The duplicate family-list parser is
removed. A shared token function-name helper handles decoded names that retain
an opening parenthesis and is also used by ordinary function parsing. No font
name, site condition, glyph metric, font cache or fallback result is hard-coded.
The governing syntax is [CSS Variables section3](https://www.w3.org/TR/css-variables-1/#using-variables)
and [CSS Fonts font-family](https://www.w3.org/TR/css-fonts-4/#font-family-prop).

All205 focused Input tests pass (92 parser,113 engine). The new parser case
checks six pending lists through both entry points. Three native document
controls pass: the new Ahem/list/fallback/invalid-variable/live-update case,
the adjacent monospace-size case and retained-font margin sizing. Before the
repair, the list/fallback/late-variable glyph widths are72.2168px instead of100px.
Chromium independently confirms admission and the real-font/invalid-variable/
live-update expectations. The saved Limitless page now selects its declared
Greycliff family chain and CSS.supports returns true. The first candidate's
new unit exposed the raw FUNCTION token's retained parenthesis; that handling
was corrected before qualification, and the first candidate is not published.

Evidence: sibling Lambda temp/online-visual-round57/ holds source hashes,
before/after parser and font controls, browser results, release pin and selected
live capture/report qualification. Only focused tests are run for this repair;
no new full baseline pass is claimed. Live page clearance requires visual
review and is recorded in the online report, independently of exception or
font-selection repair.

<a id="li-071"></a>

### LI-071 — shape-outside rejects otherwise supported gradient images

**Status:** FIXED, 2026-11-16. **Dependent task blocker:** resolved for
Radiant's CSS Shapes WPT gradient contours. Ownership is D9.1.1v4 /
D9.1.3v2; no formal ruling or dependency direction changed.

`css_shape_outside_components` admitted only URL and image-set functions,
rejecting valid linear/radial/conic gradients before Radiant could construct
contours. It now calls the existing `css_value_is_supported_image` predicate,
sharing the established image grammar and invalid-stop rejection. The existing
Shapes parser test includes valid and malformed generated-image declarations.

The CSS parser suite passes 117/117. The full Input baseline passes 5,312 tests
with three skips across 109 executables. Radiant's final Shapes suite passes
294/294 with the separate approved reference repairs recorded in RI-094.
Evidence: `../Lambda-opus/temp/wpt-css-shapes/input-parser-final.log`,
`input-baseline-continuation.log` and `completed-suite-final.log`.

<a id="li-066"></a>

### LI-066 — Individual translation rejects CSS math arguments

**Status:** FIXED, 2026-11-16. **Lambda blocker:** no; Draftbit's primary
navigation was shifted halfway offscreen. Input ownership D9.1.1v4 /
D9.1.3v2; the native math resolver is tracked separately as RI-094.

`translate:calc(calc(1 / 2 * 100%) * -1) 0` is a valid percentage translation.
`css_value_is_individual_transform` admitted only literal lengths, percentages
and zero, although `css_transform_argument_valid` already validates typed
calculations. Individual translation now shares the `translate3d()` argument
checker, retaining the third-axis percentage rejection and invalid-domain
rejections. `CssEngineTest.SupportsIndividualTranslationMathUsesTransformArgumentTypes`
and the existing supports-validation control pass. Seven native PNG controls
and Chromium positions qualify the integrated repair; evidence is in Lambda
`temp/online-visual-round39/`. No parser tolerance or capture policy changed.

<a id="li-065"></a>

### LI-065 — CSS Shapes properties lack shared property registration

**Status:** FIXED, 2026-11-16. Input registration no longer blocks Radiant's
bounded horizontal CSS Shapes implementation. Authority: D9.1.1v4 /
D9.1.3v2 ownership and D5.6.1 stable name identity. The user explicitly approved
the scoped Input IDs, metadata, validation, names and parser tests.

Before the repair, name lookup returned ID 0 and the native parser probe
rejected supported-declaration admission for `shape-outside:circle(50%)`,
`shape-margin:10px` and `shape-image-threshold:0.5`. The enum/registry had no
entries, and the custom registry accepts only `--` names. The failing probe
is retained at Radiant `temp/css-shapes/registry-probe.log`.

Input now provides distinct standard IDs, non-inherited initial metadata
`none`, `0px`, `0`, core basic-shape/reference-box validation, nonnegative
length-percentage margins, numeric/percentage thresholds and the missing
margin-box keyword. The existing generator emits the property mapping and
CSS declaration attributes. Its source catalog remains in Lambda's
`utils/well_known_names_data.py`; the three identities append to the stable
tail, preserving every pre-existing NameId. No custom-property alias or
second declaration parser bypasses the normal cascade.

`CssParserTest.CssShapesRegistersAndValidatesCoreGrammar` covers valid and
invalid declarations, variables, global values and metadata defaults. The
whole CSS parser suite passes 117/117. `make -j8 test-input-baseline` passes
5,312 tests across 109 executables, with three existing skips. Generated name
and CSS inventory freshness checks pass. Evidence: Radiant
`temp/css-shapes/input-parser-full.log`, `input-baseline.log` and
`vibe/impl/Radiant_Impl_CSS_Shapes.md`. Radiant separately owns actual float
contours; this registration repair does not claim full CSS Shapes conformance.

<a id="li-002"></a>

### LI-002 — External-parent admissibility test disagrees with implementation

**Status:** FIXED 2026-11-16. The failing fixture represented only a
`has_spread` marker. The existing D3.4.3 implementation policy admits that
marker for computed-key literals and rejects actual unnamed spread slots
(Lambda `vibe/impl/Lambda_Impl_DBench.md`). The fixture now contains an unnamed
map-link slot; its null-result assertion is unchanged. The companion control
checks empty-marker admission without mutating the external parent, named-field
admission, and unnamed-slot rejection. This corrects the test setup rather
than changing the admission policy. The full MarkBuilder suite passes 96/96;
the Input baseline passes 5,311 with three skips and no failures.
Evidence: `temp/li002-li003/{admission-before,mark-builder-after,input-final-baseline}.log`.

<details>
<summary>Original report and investigation</summary>

**Original status:** OPEN — expected behavior and root cause need investigation.
**Observed:** 2026-11-09, macOS, `make test-input-baseline` (debug).
**Lambda blocker:** not established. D3.4.3 requires a transition tree not
to write to an external parent; it does not by itself establish the disputed
`has_spread` admission rule.

`TransitionTreeExternalParentTest.InadmissibleParentsAreDeclined` in
`test/test_mark_builder_gtest.cpp` expects `type_tree_add_map_field_chars`
to return null for an external map marked `has_spread`, but it returns a
non-null child. `external_parent_admissible` in `lambda/input/input.cpp`
currently allows named stored fields for runtime-key literals that keep this
flag. Determine the intended admission rule and repair the implementation or
the test at the root cause; do not change the expectation merely to pass.
Reproduce with `./build/test/test_mark_builder_gtest --gtest_filter=TransitionTreeExternalParentTest.InadmissibleParentsAreDeclined`
after building the baseline.

2026-11-14 review: the existing D3.4.3 implementation record in
`Lambda_Impl_DBench.md` explicitly admits `has_spread` when every stored
field is named, and rejects actual unnamed spread slots. The failing test
constructs an empty shape with only the marker, so it conflicts with that
documented implementation policy. A separate native control now checks both
the named-field admission and the unnamed-slot rejection. The original
assertion remains unchanged under this repository's test-preservation rule;
the aggregate failure remains open.

</details>

<a id="li-003"></a>

### LI-003 — Recheck DOM element backing-copy ownership

**Status:** FIXED 2026-11-16 for copied DOM trees. The earlier storage
and source-header repairs remain in place. The remaining source-owned
`ShapeEntry::type` and `LaneStorageDesc` edges now point into a document-owned
snapshot of the structural contract graph (D5.5.3, D3.4.6). A per-copy memo map
retains aliases and cycles. Copied names, nested descriptors, literal payloads
and compiled regex resources have document lifetime; global singleton semantics
and separately rooted nominal/code identity remain intact (S2.1.4, D5.5.2).
Declared nominal types retain their full descriptor layout. Type-valued
attributes use the same copier instead of reducing the type to its TypeId.

The additional controls exposed two related construction defects: MarkBuilder
allocated only a Type header for larger descriptors, and the element writer
silently omitted type/function carriers. The builder now allocates the complete
descriptor, and element writes reuse the existing map storage helper.

All 21 native DOM tests pass. The original two contract tests now destroy the
source pool before reading and editing the copy. New tests cover shared and
recursive contracts, nullable lane pointers, retained nominal identity,
semantic singleton identity, type values, independent regex cleanup and
nominal descriptor layouts. The full Input baseline passes 5,311 with three
skips and no failures. Evidence:
`temp/li002-li003/{contracts-before,dom-final,input-final-baseline}.log`.

**Scope:** directly created borrowed wrappers and executable/nominal host
metadata still require their existing owner/root lifetime under D5.5.2;
these native tests do not qualify host GC or compaction. The independent
shared-allocator capability LI-053 remains open. No allocator-retention or
conservative scanning mechanism was added for this repair.

<details>
<summary>Original report and investigation</summary>

**Original status:** PARTIAL — a source-storage lifetime defect is fixed; broader
non-view ownership remains unverified.
**Ownership checked:** 2026-11-09. **Lambda blocker:** not established.

The historical [RAD01-2](radiant/Radiant_Issue_Ledger.md#rad01-2)
claimed that DOM element backing data was copied and could diverge. The current
input-owned `DomElement::create_in` in `lambda/input/css/dom_element.cpp` still
assigns the supplied `Element` into `element->elmt` and conditionally snapshots
backing data, but the old member names and constructor description are stale.
Review the current ownership and mutation invariants, including D5.5.1 and
D9.1.1v4, before deciding whether any defect remains.

2026-11-14 review: D5.5.3 requires a view occurrence to own its copied
document content. `build_dom_tree_with_parent` now uses `deep_copy_for_dom`
for ordinary view inputs, and the focused
`InputDomNativeTest.ViewOccurrencesOwnBackingAndLeaveSourceUnchanged` passes
for two occurrences and a mutated descendant. The historical `native_element`
member is gone; the registry separately records a render source, while DOM
reads and writes use the embedded backing. A second control verifies that
mapping and live mutation for a non-view wrapper. Non-view construction still
borrows shallow-copied data, and its behavior when the source has a shorter
lifetime than the document has not been qualified, so this issue remains open.
D5.5.2 permits a retained reference only when its GC root lives at least as
long as the document; the non-view control does not establish that host-side
lifetime condition.

**Source storage repair, 2026-11-14:** a non-view wrapper shallow-copies the
source element header. Adding an attribute through `MarkEditor` rebuilt the
wrapper's shape and freed its shared pool buffer, leaving the retained source
pointing at a freed block. If the shared type was a private clone, the same
edit could also change the source's shape. The editor now checks its document's
registered source before reusing a private shape or freeing old data; it
keeps the source-owned type and allocation intact. The test
`InputDomNativeTest.NonViewWrapperShapeChangesRetainSourceStorage` failed for
both shared and private shapes before the repair and passes afterward, along
with the other eight native DOM cases. Evidence: `temp/ledger-dom-regression-before.log`
and `temp/ledger-dom-suite-after.log`. Same-type writes through a directly
created non-view wrapper still share existing data. The shorter-source-lifetime
question remains unqualified; this partial repair does not close LI-003.

**Descendant reproduction:** a non-view tree's parent backing retains its
original child instead of the live DOM wrapper. Consequently
`dom_backed_node_index` returns -1 for the linked child, and a new child
attribute is absent when reading through the parent's Mark backing. Non-view
tree construction now uses the existing D5.5.2/D5.5.3 document-owned deep-copy
path, allocates each copied occurrence as DOM node storage, and retains the
source mapping. Focused tests cover two independent occurrences, descendant
edits and removal, and nested arrays without modifying the source. The native
regression `InputDomNativeTest.NonViewChildBackingTracksTheLiveWrapper` fails
before and passes after the repair. Evidence: `temp/ledger-dom-tree-before.log`
and `temp/ledger-dom-cow-qualified.log`. See the subsequent source-lifetime
control below for its scope; host-managed GC qualification remains open.

**Source-header lifetime repair, 2026-11-14:** even after a tree had been
copied into its document, a later shape edit dereferenced its retained source
header to discover whether its type and data were shared. A guard-page probe
reproduced the access after the source header became inaccessible
(`temp/ledger-dom-source-guard-before.log`). The node registry now records
shared storage while binding the valid source, and the editor consults that
record without rereading the source. Sixteen native DOM controls pass,
including view and non-view copied trees after a separate source pool is
destroyed, a guarded source header, and the existing shared-wrapper cases
(`temp/ledger-dom-source-focused.log`). These tests do not qualify foreign
storage borrowed by a directly created wrapper, render-map identity after
source retirement, or host-managed GC and compaction; LI-003 remains open.

**Retained field contract, 2026-11-14:** the copied element shape still
retains each source `ShapeEntry::type`. With a source-owned field contract,
protecting that contract's page after tree construction causes a subsequent
attribute read to fault (`temp/ledger-dom-contract-before.log`). The native
regression `InputDomNativeTest.CopiedTreeOwnsForeignFieldTypeContract` remains
failing for both view and non-view documents
(`temp/ledger-dom-contract-focused.log`). Shape contracts can contain nested
type graphs and nominal records whose pointer identity is significant under
S2.1.4/D3.1.1; copying only the base header or replacing the contract with a
generic type would not establish the required lifetime or preserve semantics.
The correct ownership of those contracts is still under investigation.
The cached `LaneStorageDesc::semantic_contract` and `base_contract` also
point into the source pool. The additional native nullable-contract control
exposes both the outer contract and its nested base; both controls fail in
view and non-view modes (`temp/ledger-dom-contract-graph-before.log`). The
document constructor receives an `Element*`, not its source `Input` or an
owner handle, and the shared allocator capability is open as [LI-053](#li-053).

</details>

<a id="li-063"></a>

### LI-063 — ZIP output cannot retain timestamps for modified members

**Status:** FIXED 2026-11-16 for modified timestamps and directory mode.
Materialized files and directories carry validated `modified` and `mode`
attributes to the ZIP writer. `explicit_dir` retains the distinction between
real and synthetic directory members. The Lambda package helper forwards those
attributes and the replacement file's compression method (D9.1.1v4, D9.3.4).
The Input ZIP suite passes 37/37 (`Lambda-opus/temp/docx-edit/input-final-suite.log`).
An independent Python ZIP check confirms the DOCX and EPUB replacement times,
explicit directory mode/time, compression, and the DOCX member inventory
(`Lambda-opus/temp/docx-edit/metadata-oracle.log`). The preserving writer also
copies unchanged original local and central records, including extra fields,
member comments and compressed bytes, retaining central order and the archive
comment; edited members are encoded anew. Independent DOCX package comparisons
are in `Lambda-opus/temp/docx-edit/package-oracle.log`.

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and Input `3e14a05`. **Lambda task blocker:** yes for a
metadata-preserving DOCX/EPUB editor. Input ownership is D9.1.1v4 and
D9.1.3v2; document and archive preservation is D9.3.4 / D9.5.5.

From the Lambda checkout run
`./test/test_lambda_gtest.exe --gtest_filter='*proc_editor_package_members*:*proc_editor_epub_chapter*'`.
The package editor reconstructs the changed XML member as a materialized
`<fs kind:'file'>`, preserving its `name`, bytes and `mode`, then writes the
archive through the ZIP writer. Python `zipfile` shows the original DOCX
`word/document.xml` timestamp `(2020, 1, 2, 3, 4, 6)` becomes
`(1980, 1, 1, 0, 0, 0)`; the original EPUB `Books/text/one.xhtml` timestamp
`(2026, 1, 1, 0, 0, 0)` also becomes `(1980, 1, 1, 0, 0, 0)`. Both retain
their `0600` permission bits. Evidence is in
`Lambda-opus/temp/edit-viewer/package-metadata.log`. Other members retain
their logical bytes; the archive is rebuilt and compressed representations and
unknown extra fields are outside the current ZIP writer contract.

`lambda/input/input-zip.cpp` `fs_output_walk` obtains timestamps only from
archive-backed `FsNode` objects. Constructed files have no supported way to
specify a modification timestamp, although their payloads are the supported
way to represent edits; rebuilt directory nodes likewise do not retain their
original directory metadata. Preserve and validate the available original
timestamp and directory mode when writing edited/rebuilt members. Keep the
user-facing package editor disabled until metadata and other package save
requirements have been addressed. No Input implementation was changed.


<a id="li-062"></a>

### LI-062 — Man parser includes .TH argument quotes in the title

**Status:** FIXED 2026-11-16. Quoted and tab-separated `.TH` title
arguments now parse as a single title without literal delimiter quotes; section
headings keep their existing handling. Input owns the parser under D9.1.1v4;
source-preserving document editing follows D9.3.4. The user authorized this
owner repair. `FormatAdapterTest.Man*` passes 3/3, including quoted/unquoted,
space-containing and tab-separated title controls
(`Lambda-opus/temp/edit-man-structured/man-native-final.log`). The Lambda
editor retains the original complete `.TH` header and preamble on edits; its
model and save/undo UI replay pass (`man-test.out`, `man-ui.log`).

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes for reliable
rich editing of man documents that use the Lambda man writer. Input ownership
is D9.1.1v4 and document processing is D9.3.4.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/man-title-parse.ls`.
The input `.TH "TEST" "1"` yields an h1 whose text includes the double
quotes; `.TH TEST 1` yields `TEST` without them. The local `mandoc -Thtml`
independently renders both headers as the same `TEST(1)` title. Evidence:
`Lambda-opus/temp/edit-viewer/man-title-parse.log`,
`man-title-quoted.html`, and `man-title-unquoted.html` in the same directory.
The Lambda man writer emits quoted `.TH` arguments, so its output reparses
to a visibly different title. Writer metadata retention is a separate Lambda
editor concern; correcting the Input parser alone does not complete that work.

The exact Input parser cause is unconfirmed. LI-043 fixed the separate `\&`
zero-width escape issue. The editor retains source mode for man pages. No
Input implementation or reference was changed by this report.


<a id="li-058"></a>

### LI-058 — Calendar and contact parsers accept broken component boundaries

**Status:** FIXED, 2026-11-15. The user explicitly authorized the owner-side
repairs from the Lambda editing task.

**Repair and verification, 2026-11-15:** component state is now validated
before finalization. Unclosed, nested and mismatched card boundaries, unmatched
calendar ends and uncontained properties report parse errors. Orphan folded
lines are no longer silently discarded. Nested calendar components and multiple
vCards remain supported. The existing single-calendar result profile rejects
extra VCALENDAR roots explicitly instead of coalescing their data. Lambda now
provides source-preserving component/property editing and retains source mode
for unsupported syntax (D9.3.4, D10.2.1).

**Continuation, 2026-11-16:** Input now accepts multiple independent `VCALENDAR` roots and returns them in order as an array; a single root retains the existing map result. Unclosed or nested roots still report errors. `InputRoundtripTest.CalendarStreamRetainsIndependentRoots` and the boundary controls pass (`Lambda-opus/temp/edit-man-structured/calendar-native.log`); Lambda editor save and reopen are covered by `calendar_stream.ls` and `edit_calendar_stream.json` (D9.3.4).

Owner controls are in `InputRoundtripTest.CalendarAndContactRejectBrokenComponentBoundaries`.
The seven focused Input tests pass (`Lambda-opus/temp/edit-viewer/validation/focused-final.log`);
RFC property model checks pass 13/13, and both CLI editor UI flows pass save,
undo and redo. The final aggregate Input baseline is qualified separately in
the Lambda implementation checkpoint; unrelated LI-002/LI-003 failures remain.

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes for safely
validating source-backed iCalendar and vCard property edits. Input ownership
is D9.1.1v4; document processing is D9.3.4 and parse-error acknowledgement
is S7.5.1v6.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/contact-calendar-admission.ls`. An unclosed
`BEGIN:VCARD` and a card closed with `END:OTHER` both return a normal contact;
a property outside a vCard returns an empty contact list. For iCalendar an
unclosed `VEVENT`, a mismatched `END:VTODO`, an unclosed calendar, and a
property outside `VCALENDAR` all return success. In the first two cases the
VEVENT and its summary are silently absent from the result. A properly closed
calendar retains the event and acts as a control. The complete output is in
`Lambda-opus/temp/edit-viewer/contact-calendar-admission.log`.

The input parser in `lambda/input/input-vcf.cpp` accepts or finalizes incomplete
cards, while `lambda/input/input-ics.cpp` can leave an active component
unreported at a parent close or end of input; exact parser-state fixes are not
established. LI-040 fixed missing property-name/value separators, but did not
validate nesting or outer boundaries. Return parse errors for malformed
boundaries without silently dropping content. The editor retains source mode
until its provider can validate these documents. No Input implementation or
references are changed by this report.

<a id="li-057"></a>

### LI-057 — YAML parser accepts malformed scalars, aliases and collections

**Status:** FIXED, 2026-11-15. The user explicitly authorized the owner-side
repairs from the Lambda editing task.

**Repair and verification, 2026-11-15:** unresolved aliases, unknown or
malformed quoted escapes, unterminated quotes, block indentation tabs, missing
flow entries/separators and duplicate string mapping keys report errors.
Presence checks detect duplicates even when the old value is null. Source CRLF
and CR normalize before tokenization; a new block-scalar control fails before
this repair and passes afterward. Valid alias shadowing/null aliases, quoted
folding, trailing commas and tabs in scalar content remain supported (D9.3.4,
S7.5.1v6).

The focused owner selection passes seven tests, including malformed/valid
neighbors and CRLF/CR block values. Evidence: `Lambda-opus/temp/edit-viewer/validation/`
`{newline-before,focused-final}.log`. All 276 upstream YAML JSON comparisons
pass in the full owner run. This closes the reported malformed-input cases;
it does not claim complete YAML conformance. Lambda property editing handles
supported block/flow trees and keeps references, tags, directives, document
streams and non-string keys on the source surface. Its model checks pass
32/32 and the CRLF/invalid-draft/save/undo UI flow passes 7/7 assertions;
first-field deletion and empty collection retention add a 6/6 UI replay.

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes for safe YAML
property editing. Input ownership is D9.1.1v4, document processing D9.3.4,
and parse-error acknowledgement S7.5.1v6.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/yaml-admission-probe.ls`.
The parser accepts an unknown double-quoted escape (`a: "bad\q"`), an
unterminated quoted scalar, an undefined alias (`a: *missing`), a tab used
for indentation, a doubled flow-sequence comma (`a: [1,,2]`), and duplicate
mapping keys. The independent local `js-yaml` parser rejects each input.
The Lambda probe output is `Lambda-opus/temp/edit-viewer/yaml-admission-probe.log`.
The precise Input-side causes are unconfirmed. Resolved LI-039 covers missing
flow delimiters, which the current parser does reject; it does not cover these
other malformed cases.

The Lambda editor leaves YAML in source mode because the current parser cannot
reliably validate a complete document or an edited value. No Input implementation
or references are changed by this report.

<a id="li-056"></a>

### LI-056 — TOML parser accepts invalid escapes, numbers and duplicate keys

**Status:** FIXED, 2026-11-15. The user explicitly authorized the owner-side
repairs from the Lambda editing task.

**Repair and verification, 2026-11-15:** TOML now validates numeric digit,
underscore, sign, fraction and exponent grammar before exact conversion. Basic
string escape errors fail the parse, Unicode escapes require scalar values,
continuations require a real line break and unescaped controls are rejected.
Multiline strings normalize source CRLF and retain the legal extra closing
quotes. Per-container declaration state prevents duplicate assignments,
redeclared tables, inline-table extension and ordinary-array/table-array
collisions. Incomplete collections and trailing header content are rejected.
Empty documents retain Input's existing null contract (D9.3.4, S1.5, S7.5.1v6).

The seven focused Input tests pass, including invalid spellings and valid
neighboring literals/table declarations. The extra four-string CLI probe
returned four `false` rejection results before repair; it must return four
`true` results afterward. Evidence: `Lambda-opus/temp/edit-viewer/validation/`
`{toml-string-before.out,focused-final.log}`. Lambda now provides source-backed
TOML property fields under retained table headings, with expandable arrays and
inline tables. Model checks pass 22/22; CRLF no-op/save, invalid drafts and
undo/redo pass 7/7 UI assertions. Full Input baseline residuals are qualified
separately and are not closed by this repair.

**Existing fixture correction:** the former
`TomlTests.TomlUnicodeSurrogatePairs` treated UTF-16 pair escapes as valid TOML.
The [TOML string specification](https://toml.io/en/v1.0.0#string) requires
Unicode scalar escapes. `TomlUnicodeScalarEscapes` retains the original four
UTF-8 value/length checks through raw/scalar spellings and adds four paired/
isolated surrogate rejection checks; the parser is not relaxed (D9.3.4,
S7.5.1v6). All ten Input/TOML focused tests pass. The complete audited Input
baseline retains exactly the three unrelated LI-002/LI-003 failures.
Evidence: `Lambda-opus/temp/edit-viewer/validation/`
`{toml-fixture-before,toml-fixture-after,input-baseline-audited}.log`.

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and input `3e14a05`. **Lambda task blocker:** yes, safe TOML
property editing needs the provider to reject malformed documents and edited
values. Input owns the parser under D9.1.1v4; D9.3.4 covers document processing
and S7.5.1v6 governs parse-error acknowledgement.

From the Lambda checkout, run `./lambda.exe temp/edit-viewer/toml-admission-probe.ls`.
The script-facing TOML parser accepts `name = "bad\q"`, `x = 01`, `x = 1__2`,
`x = 12_`, and repeated `x = 1` / `x = 2` assignments. Python's independent
`tomllib.loads` rejects all five; evidence is in
`Lambda-opus/temp/edit-viewer/toml-admission-probe.log`. The parser reports
unknown basic-string escapes as warnings and continues in
`lambda/input/input-toml.cpp`; the precise numeric and duplicate-key causes
are unconfirmed. The earlier LI-038 fix rejects trailing numeric content but
does not cover these spellings.

The editor currently leaves TOML in source mode. It cannot safely use the
provider to validate a property edit or a candidate document while invalid
values and duplicate assignments are admitted. This report makes no change
to Input implementation or references.


<a id="li-060"></a>

### LI-060 — Length-bounded JSON input reads beyond a binary slice

**Status:** FIXED 2026-11-15. **Lambda task blocker:** cleared for the
`proc_sync_*` selection and bounded JSON parsing. Originally reproduced with
Lambda `e55450ec8` and Input `3e14a05`. Input ownership is D9.1.1v4,
document parsing is D9.3.4; S7.5.1v6 governs acknowledgement of parse errors.

From the Lambda checkout, run
`./lambda.exe temp/proc-sync-investigation/binary-json.ls`. It creates
`slice(binary("1X")^, 0, 1)^`, whose length is one and content is valid JSON
`1`. Parsing the text `"1"` succeeds, but parsing the binary slice produces
E407 for `1X` (unexpected content at column 2); output is `[1, 'error', 1]`.
The parser reads the `X` outside the slice. The focused sync run reports many
E407 errors after apparently complete JSON followed by unrelated bytes;
`proc_sync_local_repository` reproduces independently. Sync repository decode
passes authenticated binary plaintext to `codec.decode`, which calls
`parse(encoded, 'json')`.

`fn_parse2` passes `binary_data` and `binary_length` to
`input_from_source_n_with_name_parent`. Before the fix, the ordinary Input
dispatch forwarded only the source pointer to the NUL-terminated JSON parser.
Unlike a String, a Binary is a length-bounded span and a slice may end before
its backing storage does. The owner fix routes length-bounded JSON dispatch
through `parse_json_n`,
parsing the existing InputContext-owned terminated copy and rejecting NULs
within the supplied span. The IME resource path shares this implementation.
`JsonTests.ParsesOnlyTheBoundedSourceAndRejectsEmbeddedNul` failed before
and passed after the fix; all six `JsonTests` passed. The rebuilt Lambda host
passed all 52 `proc_sync_*` cases outside the sandbox
(`Lambda-opus/temp/proc-sync-investigation/focused-fixed.log`). The Input
baseline completed with 5,290 passed, three skipped, and the existing three
LI-002/LI-003 failures (`temp/li-060-input-baseline.log`).

<a id="li-044"></a>

### LI-044 — AsciiDoc heading levels do not round-trip through the input parser

**Status:** RESOLVED for document titles and body heading levels, 2026-11-14.
**Lambda task blocker:** cleared for the exercised rich-editing path. D9.3.4
governs document processing.

The AsciiDoc reader places a document title in `meta.title` and maps body
section markers to h1–h5. Markdown output and input carry metadata in YAML
front matter, including through the editor's existing front-matter envelope;
strict CommonMark remains unaffected. Generic markup detects the actual input
URL filename, while explicitly requested Markdown takes precedence over it.
Native controls cover title escaping, section levels, metadata round trips,
source positions and explicit format selection. The original wiki assertion
continues to pass (`temp/li044-roundtrip-dispatch.log`).

The original Lambda parse/format/parse probe preserves the title
(`temp/li044-lambda-roundtrip-current.log`), and six editor import, edit, export
and reopen checks pass (`temp/li044-editor-probe-current.log`).
The isolated host links existing Lambda and Radiant debug objects with the
current input archives; hashes are in `temp/li026-comparison-binaries.json`.
This is a focused integration check, not a fresh full Lambda baseline. The
input aggregate completed 109 executables with 5,291 passed, the existing
LI-002 and two LI-003 failures, and one font-dependent skip
(`temp/li044-li026-input-baseline.log`). The release build and debug and
release boundary audits pass (`temp/li044-li026-release-build.log`,
`temp/li044-li026-boundary-debug.log`, `temp/li044-li026-boundary-release.log`).

<details><summary>Original report and investigation</summary>

**Original report:** the issue blocked rich editing of AsciiDoc headings.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/markup-roundtrip-probe.ls`.
The AsciiDoc source `= Title\n\nHello _world_.\n` parses its heading as h1.
Formatting the equivalent Mark Doc with the Lambda AsciiDoc writer produces
`== Title`, which the input parser reads as h2. The writer reserves `= Title`
for document metadata and maps body h1 to `==`, while the input parser maps
`==` to h2. This existing reader/writer mismatch is also documented in
`doc/Script_Formats_Support.md` under the current round-trip results. The editor
falls back to source instead of changing heading levels. Evidence:
`Lambda-opus/temp/edit-viewer/markup-roundtrip-probe.log`. The cross-owner
handoff is recorded in the [Lambda ledger](Lambda_Issue_Ledger.md#input-format-integration-handoffs--2026-11-14).

2026-11-14 ownership review: the input adapter currently maps the number of
leading equals signs directly to a body heading. The Lambda writer's documented
profile instead reserves a single equals sign for `meta.title` and starts body
headings at two. Mapping only `==` to h1 would leave the document title as a
body heading and would not implement that model. Moving the title into `meta`
also affects the existing Markdown conversion path, whose input-owned writer
currently skips metadata, so the reported Markdown-mediated round trip needs
an agreed title-preservation path across the owners. No parser mapping has been
changed as an isolated workaround.


**Earlier source follow-up, 2026-11-14:** two focused native controls built
against the current debug archives reproduce both reader mismatches: `= Title`
is a body h1 with no metadata, and `== Section` is a body h2
(`temp/li044-current-probe.cpp`, `temp/li044-current-probe.log`). The Lambda
editor's `format/markup.ls` formats its parsed document as Markdown and later
reparses Markdown before calling the AsciiDoc writer. Input's Markdown formatter
skips `meta`, so the writer cannot recover a title moved there by the corrected
reader. The Lambda handoff now records this editor integration blocker. A
title-preserving bridge or an established equivalent contract is needed before
the reported editing round trip can be closed; its representation is not yet
established. No heading-only workaround has been applied.

</details>

<a id="li-026"></a>

### LI-026 — Calmind CDN rejects a valid compressed resource request profile

**Status:** RESOLVED for the observed first-party resource rejection,
2026-11-14. **Lambda blocker:** cleared for the tested CSS and image loading.
Network ownership follows **D9.1.1v4 / D9.1.3v2**.

The resource backends omitted the `Accept-Language` already sent on document
navigation. The CDN's response varied on that header when the valid document
Referer was present: 15 paired controls without the language header returned
403, and all 15 with it returned 200 across CSS, image and script requests
(`temp/li026-paired-controls/results.json`). Both resource backends now send
the document's language preference. Referer, codec negotiation and TLS
verification are unchanged. A loopback server that requires both the Referer
and language preference fails both backends before the repair and passes after
(`temp/li026-regression-before.log`, `temp/li026-regression-after.log`).

Two interleaved native renders per version used pinned hosts and fresh caches.
Both before runs saw 24 first-party 403s and all three stylesheets failed;
both after runs saw no first-party HTTP failures or stylesheet failures. All
three decoded cached stylesheets in both after runs match independent identity
responses by SHA-256 (`temp/li026-render-pairs/css-equality.json`). The visible
page restores its styles and images against an independently captured Chromium
view (`temp/li026-render-pairs/`, `temp/li026-browser.png`). Live server
behavior can change; this is not a claim of general CDN compatibility. The
Lambda ledger records separately observed page-script and event-loop failures.
The input aggregate completed 109 executables with 5,291 passed, the existing
LI-002 and two LI-003 failures, and one font-dependent skip
(`temp/li044-li026-input-baseline.log`). The release build and debug and
release boundary audits pass (`temp/li044-li026-release-build.log`,
`temp/li044-li026-boundary-debug.log`, `temp/li044-li026-boundary-release.log`).

<details><summary>Original report and investigation</summary>

**Original report:** the capture completed with missing first-party CSS and
images and a major visual difference. The transport cause was unconfirmed.

The current native capture of https://calmind.com/ receives HTTP403 HTML browser
checks for first-party CSS, scripts and images. Fresh headless Chromium receives
200 CSS for all three stylesheets. The resource manager already forwards the
owning document's Referer through CURLOPT_REFERER; adding the same Referer or
copying the current native User-Agent in system curl still receives 200 CSS.

Independent system-curl controls isolate an origin response variation:
`--compressed` advertises the available `deflate, gzip` codecs and receives
HTTP403, 5768 bytes of an HCDN browser-check page, with Vary:Accept-Encoding.
IPv4 gives the same result. Explicit gzip-only, identity, no encoding header or
Chromium's `gzip, deflate, br, zstd` header receives HTTP200 text/css. The valid
native compressed request is not proven malformed; no cookie, TLS, resource
scheduling or script-startup root cause is established by these controls.

Do not disable compression, spoof unsupported codecs, reorder headers for this
host or substitute its CSS solely to match the reference. Broader CDN/browser
challenge interoperability is outside this existing-bug milestone. Evidence:
Lambda `temp/online-visual-round13/calmind-corrected-controls.json`,
`calmind-encoding-controls.json`, saved response headers/bodies,
`calmind-native-probe.png` and `calmind-browser-current.json/png`. An early
User-Agent extraction concatenated following macros; those native/css-accept
controls are superseded by corrected-controls and are not evidence.

2026-11-14 refresh: the current page points to a first-party autoptimized CSS
asset for which system curl received HTTP 200 with compressed, identity, and
absent Accept-Encoding headers. Its URL and response are saved in
`temp/ledger-network-calmind-css-*.log`. This is a current asset, not the
previously captured asset or a native-renderer comparison, so it does not
establish that the original interoperability issue is fixed.


**Earlier native transport follow-up, 2026-11-14:** the current input-owned curl
archive reports Brotli and zlib support. A fresh host-trust native probe
advertised `deflate, gzip, br` and fetched the later autoptimized stylesheet
with HTTP 200, `text/css`, and 213,349 decoded bytes
(`temp/li026-native-probe.cpp`, `temp/li026-native-probe.xml`, and
`temp/li026-native-probe.log`). A system-curl compressed request for that
same later asset also returned 200 (`temp/li026-current-curl.log`). The
original challenge-producing resource is not available in this checkout, and
this probe does not exercise a complete native render or prove the original
CDN rejection fixed. No input transport defect or general repair has been
established; this issue remains open.

</details>

<a id="li-055"></a>

### LI-055 — Concurrent allocator factories race while installing release hooks

**Status:** RESOLVED, 2026-11-14. **Lambda blocker:** not established. D5.2.1
governs allocator context registration, and D6.4.4 requires safe registry
initialization.

The pool/arena/scratch factory and the NamePool factory each used an
unsynchronized function-local `installed` flag while writing their global
release hooks. Isolated eight-thread first-use probes, with the respective
factory source instrumented by ThreadSanitizer, report races on both flags
(`temp/ledger-mem-factory-tsan-before.log` and
`temp/ledger-name-factory-tsan-before.log`). The C factory now uses the native
platform's one-time initialization primitive, and the C++ factory uses
thread-safe function-local initialization. The same instrumented probes pass
afterward (`temp/ledger-mem-factory-tsan-after.log` and
`temp/ledger-name-factory-tsan-after.log`). This is a focused sanitizer check,
not whole-program ThreadSanitizer qualification. A separate native executable
exercises concurrent first use, and the existing allocator factory and NamePool
suites pass. Shared allocator reference counting remains open as [LI-053](Lib_Input_Issue_Ledger.md#li-053).

**Completion follow-up, 2026-11-14:** instrumenting the factories together
with the pool, arena, scratch-arena and NamePool implementations exposed the
additional shared `next_pool_id` increment in `pool_init`
(`temp/li055-tsan-pool-id-before.log`). The diagnostic ID sequence now uses
a relaxed atomic increment. The native first-use control also exercises the
scratch release hook and checks that concurrently created pools have distinct
IDs. With all six implementation files and the test instrumented, the control
passes in ten fresh processes (`temp/li055-tsan-repeat.json`); exact source
hashes and compile/link commands are in `temp/li055-tsan-metadata.json`.
This remains focused subsystem instrumentation; other linked archive objects
are not instrumented by this check. The Windows one-time initialization path
is present but has not been qualified on a Windows host.
The complete native baseline, release build and boundary audit evidence is
recorded in the allocator-factory qualification above; its remaining failures
belong to LI-002 and LI-003.

<a id="li-054"></a>

### LI-054 — Parallel native tests start duplicate dependency builders

**Status:** RESOLVED, 2026-11-14. **Lambda blocker:** not established. D9.1.5
governs the JSON-driven build pipeline.

`make -j4 test-input-baseline` started the vendor builder directly and through
the recursive quiet `all` target. Concurrent builders reused ThorVG's staging
directory, and one failed because `libthorvg.a` disappeared before installation
(`temp/ledger-input-baseline-contract-graph.log`; inner build evidence
`temp/ledger-build-dag-inner-before.log`). Native test binaries, the aggregate
and the boundary target now depend on the same nonrecursive build graph, so
make schedules each shared dependency builder once. The pre-change dry-run
control exposes both paths (`temp/ledger-build-dag-control-before.log`), and
the new dependency-builder test checks the combined native-test/boundary graph.
The full parallel baseline reaches every executable; its unrelated test failures
remain listed in the qualification above.

<a id="li-052"></a>

### LI-052 — Nested-array DOM edits diverge from the backing tree

**Status:** RESOLVED for ordinary document-owned nested arrays, 2026-11-14.
Input-owned insertion and removal retain DOM node identity and publish copied
array paths, so earlier shallow document snapshots keep their old contents.
Foreign arrays and specialized multidimensional, view, or sparse arrays are
refused before mutation; those profiles are not claimed qualified. Focused
tests cover nested elements, text, comments, sibling order, wide scalars,
snapshot preservation and ownership refusal in view and non-view modes.
Evidence: `temp/ledger-dom-cow-qualified.log`.

**Status at report:** OPEN, reproduced 2026-11-14 on macOS. **Lambda blocker:** not
established. D5.5.2 governs retained DOM ownership and S2.6.4 preserves array
boundaries in element content.

Before the repair, when an element's content contained an array whose child
appeared in the DOM sibling chain, `dom_remove_prepared_backed_child` saw no
direct backing slot and unlinked the wrapper while leaving the array entry.
A later backing-tree consumer could still observe the removed node. The
regression `InputDomNativeTest.NestedArrayEditsKeepOwnedChildOccurrences`
now verifies that removal updates the array. Pre-fix evidence:
`temp/ledger-dom-nested-removal.log`. Inserting before a nested reference also
put the new backing entry after the array even though the DOM link preceded
the reference (`temp/ledger-dom-nested-insertion.log`). The direct-index API remains unchanged. Lambda replacement uses it directly;
the unqualified host path is recorded in the [Lambda ledger](Lambda_Issue_Ledger.md#dom-replacement-of-nested-array-children--2026-11-14).

<a id="li-001"></a>

### LI-001 — Graph parser line-budget test fails

**Status:** RESOLVED, 2026-11-14. Shared directory enumeration and existing
path/Mark helpers remove duplicate logic while preserving local and brokered
include behavior. The resolver is 272 lines, the header 106, and the six-unit
total 4,407; the unchanged budget test and all 22 graph tests pass.

**Status at report:** OPEN — validation failure. **Observed:** 2026-11-09, macOS,
`make test-input-baseline` (debug). **Lambda blocker:** not established.

`GraphParserTest.ParserLocBudget` in `test/test_graph_parser_gtest.cpp`
reports 306 resolver lines against a limit of 273, 108 header lines against
107, and 4,443 combined lines against 4,409. The other 19 graph parser cases
passed in the same run. Reproduce with
`./build/test/test_graph_parser_native --gtest_filter=GraphParserTest.ParserLocBudget`
after building the baseline. The expected result is that the unchanged budget
check passes; a source simplification or other resolution requires investigation.
This observation does not establish a functional parser regression.

2026-11-14 repair: the shared directory adapter already supports an unlimited
ambient listing. It now retains the directory entry symlink classification even
when target metadata is unavailable, so the resolver can use the same
enumeration path while keeping the broker limit, local symlink exclusion,
canonicalization, and ordering. The resolver also reuses the existing Mark
child and path helpers and resolves the root path once. A duplicate header
declaration was removed. The unchanged controls cover local directories with
more than 4,096 entries, live and dangling symlinks, include cycles and network
rejection, as well as brokered filtering and sorted order. The original test
and all source ceilings remain unchanged. Evidence: `temp/ledger-graph-controls-before.log`
and `temp/ledger-graph-final2.log`.

<a id="li-013"></a>

### LI-013 — Native TeX text-case expansion exhausts its budget on reference controls

**Status:** RESOLVED, 2026-11-14. The native reference and undefined-control
case conversions terminate with the kernel encoding dispatch in place. This
does not claim full LaTeX font-selection support. D10.2.2 governs the native
TeX input boundary.

**Status at report:** OPEN — cause unconfirmed. **Observed:** 2026-11-11, macOS debug
native TeX parser. **Script TeX-port blocker:** no; the experimental script
case converter explicitly reports unsupported reference forms it cannot handle.

Running `parse("\\ExplSyntaxOn\\text_uppercase:n{\\ref{aB}/\\pageref{aB}/\\eqref{aB}}\\ExplSyntaxOff", {type: "tex"})`
returns empty text and a `tex-budget` diagnostic after 20,000,000 macro calls,
followed by missing-number and missing-brace diagnostics. Evidence is in
`temp/expl3-script/native-case-refs.out`; the same probe shows `\\label` and
`\\cite` in a different case-conversion argument completing without a budget
diagnostic.

2026-11-14 isolation: `\ref`, `\pageref`, `\relax`, and a defined `\eqref`
complete; undefined `\eqref` and an unrelated undefined command each exhaust
the budget. The bundled expl3 resources were copied into `temp/ledger-expl3/`.
The expl3 predicate correctly reports an undefined token as non-expandable.
Before the repair, the minimal kernel did not define the LaTeX font-encoding controls
`\@current@cmd` and `\@changed@cmd`. In the expl3 text expansion path,
TeX's `\ifx` correctly considers an unrelated undefined control equal to
each undefined encoding control, so expl3 takes its encoding-escape branch
and repeats until the budget is exhausted. A diagnostic preamble defining
both controls prevents this loop, but does not implement their encoding
semantics and is not a fix. The kernel now implements both dispatch controls,
including current-encoding lookup, default-encoding fallback, missing-symbol diagnostics, math warnings,
and protected expansion. Focused controls exercise dispatch, fallback,
missing-symbol reporting, and protection. The opt-in focused regressions
are `TexEngineTest.Expl3ReferenceCaseExpansionTerminates` and
`TexEngineTest.Expl3UndefinedTokenIsNotExpandable`, enabled by
`LAMBDA_INPUT_EXPL3_TEST_DIR`; both pass with the copied resources. Evidence:
`temp/ledger-tex-encoding-suite2.log`, `temp/ledger-expl3-resource-sha256.txt`,
`temp/ledger-expl3-controls.log`,
`temp/ledger-expl3-path2.log`, and `temp/ledger-expl3-kernel-control.log`.

<a id="li-007"></a>

### LI-007 — Shared header retains declarations from the second runtime retirement

**Status:** RESOLVED by the existing header split; revalidated 2026-11-14.


**Status at report:** OPEN — owner header cleanup. **Observed:** 2026-11-10, Lambda
`5f3550a38` plus the second runtime audit. **Lambda blocker:** no; the
36 declarations have no source, generated-import or host-export consumer.
Authority: **D9.1.1v4 / D9.1.3v2** (header ownership), **D9.4.6** (host exports).

Lambda's second audit retires 88 functions and 43 runtime-only JIT import rows,
including old array fills/read adapters, scalar/boolean wrappers, map type-list
adapters and unused COW profiling adapters. Input's `lambda/lambda.h` still
advertises 36 of these removed functions. The expected result is that the
shared ABI header declares only the remaining implementation surface.

Exact declaration inventory and prepared, unapplied patch:
`../lambda/temp/mvp_legacy_reaudit/input-declarations.json` and
`../lambda/temp/mvp_legacy_reaudit/input-header.patch`. Reproduce the inventory
from Lambda with `python3 temp/mvp_legacy_reaudit/post_audit.py`; the original
caller audit is `selected-before.json` in the same directory. This is a stale
header issue, not a reproduced input parser defect. Lambda's runtime builds
without those definitions because no consumer remains. The native math catalog,
Radiant's `fn_call3_into` assembly alias and unary imported-predicate trampoline
are retained. No input/Radiant source or header is edited in this follow-up;
the earlier LI-006 exception was scoped to its region patch only.

Implementation and qualification record:
`../lambda/vibe/impl/Lambda_Impl_MIR_Full_Retirement.md`, second runtime audit.

2026-11-14 review: neither sibling checkout retains the referenced temporary
inventory or patch. The inventory was reconstructed from Lambda retirement
commit `1a038ece67` and the pre-split input header: all 36 matching removed
function definitions had declarations there, and none remains in the current
`lambda/lambda.h`. Input commit `b2a13ec` removed the declarations as part of
the header split. The auditable reconstructed list is
`temp/ledger-retirement-candidates.txt`; no new header change is needed.


<a id="li-036"></a>

### LI-036 — Resource capacity rejects already-admitted consumers

**Status:** RESOLVED, 2026-11-14. **Lambda task blocker:** no after repair.
Input owns the resource manager under D9.1.1v4 / D9.1.3v2.

At 500 retained resources, `resource_manager_load` rejected every request before
checking the URL map. CSS, script and image consumers could no longer reuse
already-cached or pending bytes. Conductor's capture exposed this after unrelated
CSS icon prefetch exhausted the budget (Radiant RI-079).

The existing capacity check now follows existing-resource reuse and still
precedes acquisition of every new identity. Document admission still precedes
both paths; the cap remains 500. No caller reference, retry or ownership rule is
changed. The new offline unit actually stores and admits 500 cached identities,
reuses the first as an image consumer, rejects a new 501st URL and checks the
unchanged total. It and two neighboring cache/lifetime units pass.

Evidence: Lambda's `temp/online-visual-round31/input-focused.log`; test
`NetworkResourceManager.CapacityKeepsExistingConsumersAndRejectsNewResources`
in `test/test_network_scheduler_gtest.cpp`. Full input baseline deferred under
the user's focused-verification instruction.

<a id="li-035"></a>

### LI-035 — Grid auto-flow discards pending custom properties

**Status:** RESOLVED, 2026-11-14. **Lambda task blocker:** no after repair.
Input owns declaration validation under D9.1.1v4 / D9.1.3v2.

Ahrefs' saved tablet promotion used `grid-auto-flow:var(--directionTablet-12pdsr3)`
with `column`, but laid out vertically. The `CSS_PROPERTY_GRID_AUTO_FLOW`
validator rejected pending values before the existing Radiant substitution pass.
`css_properties.cpp` now admits pending substitution, as neighboring keyword
properties already do. Substituted values still pass ordinary grammar validation;
an invalid winning variable resets at computed-value time rather than reviving
an older declaration. No page selectors or replacement values are introduced.

Qualification: the new `SupportsGridAutoFlowRetainsPendingSubstitution` case
passes within 32 selected media/supports units. Five exported PNG controls cover
whole-value/list variables, fallback, dense packing and invalid computed values;
all five fail before and pass after, and independently pass Chromium. Evidence:
Lambda's `temp/online-visual-round30/`; tracked reductions are
`test/online/grid_flow_variable_fixtures.cjs` in Lambda. Full input baseline was
deferred under the user's focused-verification instruction.

<a id="li-034"></a>

### LI-034 — Compact logical conditions lose valid token boundaries

**Status:** RESOLVED, 2026-11-14. **Lambda task blocker:** no after repair.
Input owns CSS condition evaluation under D9.1.1v4 / D9.1.3v2.

Ahrefs' minified tablet queries use `(min-width:768px)and (max-width:1279px)`.
`css_condition_find_operator` incorrectly required whitespace before `and`/`or`,
so both device variants painted and the hero lost its tablet width. A closing
parenthesis now supplies that token boundary. Whitespace after the keyword
remains required; `and(` stays a function token rather than a logical operator,
as specified by [Media Queries 4](https://www.w3.org/TR/mediaqueries-4/#mq-syntax).
The related supports evaluator now unwraps enclosing groups before scanning
operators, fixing nested supported declarations previously sent to the atom parser.

Qualification: two new native cases exercise compact and/or, nesting, case,
comment-separated stylesheet parsing, viewport invalidation and invalid controls.
All 32 selected media/supports cases pass. Six PNG controls improve from 1/6 to
6/6 and independently pass Chromium. The exact saved Ahrefs source removes the
duplicate promotion and restores the hero width. Lambda's
`temp/online-visual-round30/` pins sources/binaries and before/after evidence;
tracked reductions are `test/online/compact_condition_fixtures.cjs` in Lambda.
No admission, URL, stylesheet, viewport or snapshot-time workaround is used.

<a id="li-032"></a>

### LI-032 — Shared signature verifier requires OpenSSL during mbedCrypto-only migration

**Status:** RESOLVED, 2026-11-13. The user explicitly authorized scoped
input-owned source, header, tests and build/setup changes for this migration.
**Lambda task blocker:** no; native provider migration and verification complete.
Ownership: D9.1.1v4 / D9.1.3v2; native crypto boundary: D11.1.2.

The previous `lib/signature.c` used OpenSSL EVP/provider APIs and the `lambda-lib`
target required `openssl-crypto`. Replaced the provider with mbedCrypto RSA-PSS/
SHA-256 and P-256/SHA-256 verification. Ed25519 remains an unsupported reserved
ABI value. Raw SEC1 keys require canonical low-S P1363 signatures; SPKI keys use
standard DER ECDSA. PEM/DER parsing rejects trailing data and extra key blocks.
OpenSSL package/link requirements are removed from input build/setup configuration.

Verification: input signature 4/4, digest 9/9, dependency configuration 11/11;
Lambda baseline 7,473/7,473; 85 sync goldens per AUTO/interpreter/JIT; 42 focused
ordinary/forced-GC crypto, identity, TUF and admission checks. Peer, DataChannel
and LAN demo native process gates pass. Native Lambda host and input archive
symbol/linkage audit finds no OpenSSL dependency. Tests retain the external
OpenSSL CLI only as an independent fixture/certificate oracle. Runtime and setup
need no OpenSSL provider. Evidence: Lambda's `temp/mbedcrypto-only/` and its
`vibe/impl/Lambda_Impl_MbedCrypto_Only (done).md` completion record. The user
subsequently authorized the Lambda-owned optional Node provider removal; that
module is also mbedCrypto-only.

<a id="li-030"></a>

### LI-030 — Animated WebP cannot supply even the initial export frame

**Status:** FIXED for initial-frame image decoding, 2026-11-14. Playback and
later-frame selection remain unsupported. **Lambda blocker:** cleared for
Maple 082's missing title. Native ownership **D9.1.1v4 / D9.1.3v2**;
selected-frame contract **D10.1.7**, allocation ownership **D5.5.1**.

The saved Maple title response is animated WebP despite its `.gif` URL.
The shared decoder explicitly rejected every animated container, including
ordinary image consumers asking only for frame zero. The existing WebP decoder
now decodes the first demuxed subframe directly into a transparent, full-size
canvas, retaining its offset and alpha. ANIM background and later frames are
not composited. The manifest adds the pinned demux source; vendor code is
unchanged. Existing scratch estimates, decoded-pixel and frame limits apply.
Strict thumbnails refuse full-canvas fallback; permitted thumbnails use the
shared reducer. Nonzero frame selection remains explicitly unsupported.

Two new native cases fail before and pass after. They cover full/offset frames,
alpha, transparent canvas, delay/loop metadata, frame/pixel/scratch limits,
truncation, thumbnail policy and unsupported later selection. All five focused
image cases pass, including static scaled WebP, format round trips and GIF.
The unchanged saved Maple page now visibly displays its large Health Yeah title.
Its original Chromium reference shows another animation phase and a cookie
panel; neither page-wide parity nor playback is claimed. Aggregate suites are
deferred under the user's focused-verification instruction.

Evidence: Lambda `temp/online-visual-round26/webp-before.log`, `webp-after.log`,
`maple-before.png` / `maple-after.png`; input `test/test_image_io_gtest.cpp`.
Original source/headers and independent first-frame image remain in Lambda
`temp/online-visual-round24/maple-title*` and `maple-static.html`.


<a id="li-014"></a>

### LI-014 — Native counter value scanning crosses an implicit expression boundary

**Status:** RESOLVED, 2026-11-14. Counter scans mark their private end token and expression scanning leaves that boundary for its caller, preventing expansion of following document tokens. Simple, implicit and nested expression controls pass in the native TeX engine suite.


**Status at report:** OPEN — root cause confirmed. **Observed:** 2026-11-11, macOS debug
native TeX parser; independently compared with pdfLaTeX. **Script TeX-port
blocker:** no for implementing the correct bounded evaluation, but native
parity for this case is blocked until the native path is fixed.

For `\begin{document}\newcounter{n}\setcounter{n}{\numexpr1+2}\arabic{n}\end{document}`,
native `parse(..., {type:"tex"})` emits `0` for `\arabic{n}` and reports
`latex-counter-value`, even though the counter eventually holds 3. The
pdfLaTeX reference emits 3; see `temp/expl3-script/counter-numexpr-ref.log` and
`counter-numexpr-debug.out`. In `input-tex-latex.cpp`, `eval_counter_value`
appends one frozen `\relax`, calls `scan_int`, and then reads another expanded
token to check for trailing material. An implicitly terminated `\numexpr`
consumes the only sentinel; that trailing-token read expands the following
`\arabic{n}` before `counter_command` stores the newly computed value. The
counter-value scan must remain bounded to its argument, including nested
expressions, without evaluating the following document tokens prematurely.

<a id="li-017"></a>

### LI-017 — Native TeX leaks an environment end marker inside constructor arguments

**Status:** RESOLVED, 2026-11-14. Constructor argument mode executes the internal environment-end primitive and closes the group. The private control name is encoded without consuming the first hex-looking letter. Single and consecutive custom-environment controls pass.


**Status at report:** OPEN — cause confirmed for the observed path. **Observed:**
2026-11-12, macOS debug native TeX parser. **Script TeX-port blocker:** yes for
native-compatible handling of custom environments inside constructor arguments.

For `\newenvironment{foo}{B}{E}\begin{document}\textbf{A\begin{foo}X\end{foo}Z}\end{document}`,
native TeX output contains `\textbf{ABXE\.nvendZ}`. The reproduction and
native output are in `temp/expl3-script/constructor-env-probe.ls` and `.out`;
the staged Script result explicitly fails reconstruction for this case. In
`lambda/input/input-tex-latex.cpp`, `latex_arg_mode_env` pushes the end macro
and the internal environment-end primitive for a custom environment.
`arg_mode` in `lambda/input/input-tex-main.cpp` emits that unexpandable
primitive instead of executing it, leaking its internal control sequence into
the output and leaving the environment group unclosed. The internal name uses
`"\x02envend"`, whose C++ hexadecimal escape includes the `e` and produces
the observed leading dot. The native argument-mode handling and group lifetime
need correction before this case can be used as a parity reference.

<a id="li-018"></a>

### LI-018 — Native LaTeX environment scanning interprets raw inline verbatim

**Status:** RESOLVED, 2026-11-14. The document environment scanner shares the existing verbatim delimiter scanner, so percent signs and environment-looking text inside verb payloads remain raw. The original and starred reproductions pass.


**Status at report:** OPEN — cause confirmed for the observed examples. **Observed:**
2026-11-12, macOS debug native LaTeX parser. **Script TeX-port blocker:** yes
for native AST comparison of affected valid verbatim content.

Native TeX preserves both `\begin{document}A\verb|x_%|B\end{document}` and
`\begin{document}A\verb|\end{document}|B\end{document}` without diagnostics,
but native LaTeX parsing reports an error for each; the same parser builds a
`verb` node for `\verb|xyz|`. Reproduction and output are
`temp/expl3-script/verb-native-digest-probe.ls` and `.out`. The document
`find_environment_end` scan in `lambda/input/input-latex-c.cpp` skips the
control word but does not skip its verbatim payload, so it treats `%` inside
that payload as a comment and `\end{document}` inside it as an environment
terminator. The scan needs to recognize raw inline spans before looking for
comments and environment boundaries.

<a id="li-019"></a>

### LI-019 — Native TeX discard boundaries become stale after cross-boundary unskip

**Status:** RESOLVED, 2026-11-14. Removing a preceding space now adjusts all open output-slice boundaries before nested boxes finish. The original discard and global-definition controls pass without leaked content or an out-of-bounds removal.


**Status at report:** OPEN — root cause confirmed. **Observed:** 2026-11-12, macOS debug
native TeX parser. **Script TeX-port blocker:** yes for native-compatible
cross-boundary removal in discarded and nested box material.

For `\begin{document}A \insert0{\vadjust{\unskip}X}B\end{document}`, native
TeX emits `AXB`: the inner `\unskip` removes the preceding space, but `X`
leaks from the outer discarded insert. A similar case leaks an echoed global
macro definition. Reproduction and native output are
`temp/expl3-script/discard-unskip-detail.ls` and `.out`. The native log also
shows `arraylist_remove_oob` with a length of 4294967295; evidence is
`temp/expl3-script/discard-unskip-detail.log`.

`unskip` in `lambda/input/input-tex-main.cpp` removes the last output item
without adjusting open groups' `out_start` values. `finish_box` in
`lambda/input/input-tex-prims.cpp` subtracts those stale unsigned boundaries
from the shorter output length; it can either leave subsequent items outside
the intended discard slice or underflow. The array-list bounds check rejects
the invalid removal. Update the native boundary accounting and qualify nested
discarded groups before using these cases as port parity references.

<a id="li-029"></a>

### LI-029 — Deferred single-block width is omitted from a parsed style rule

**Status:** RESOLVED, 2026-11-14. Width and height validation now defers its single-value grammar when substitution is pending. The original nested stylesheet and standalone declaration pass, while non-variable block and extra-token controls remain rejected.


**Status at report:** OPEN observed discrepancy; source cause **unconfirmed**, 2026-11-13.
**Lambda blocker:** no for LI-028's nested-selector repair. Ownership
**D9.1.1v4 / D9.1.3v2**, parsed declaration ownership **D5.5.1**.

For `.outer{--tokens:{a:hover{width:1px}};width:{var(--size)};a:hover{color:green}.tail{height:2px}}`,
Chromium retains two declarations on the outer style, including the deferred
single-block width. The native engine parser reports only one outer declaration
before LI-028. This is distinct from losing the following pseudo/nested rules.
The declaration parser already has a `css_value_is_single_var_block` path and
focused color-block tests; the stage that drops this width is not yet isolated.
No blanket acceptance or test masking is added. LI-028's custom-block control
uses a supported definite width to test its selected nesting contract.
Evidence: Lambda `temp/online-visual-round18/parser-before.log` and
`browser-controls.json`/`.log`; no current-source resolution claim is made.


<a id="li-005"></a>

### LI-005 — Boundary check looks in the unprofiled object directory

**Status:** RESOLVED, 2026-11-14. The boundary audit now receives the selected build profile and reads its dependency files. Debug and release audits pass; stale unprofiled files no longer affect the result.


**Status at report:** OPEN — build validation failure. **Observed:** 2026-11-10, macOS,
`make -j4 check-boundary` (debug). **Lambda blocker:** not established.
Authority: **D9.1.1v4 / D9.1.3v2** (input ownership boundary).

The input archives build, but `scripts/check_boundary.py` reports missing
dependency files because it checks `build/obj/<group>/<source>.o.d`, while the
Makefile writes them under `build/obj/<group>/<profile>/<source>.o.d`. The
expected result is for `make check-boundary` to inspect the selected profile's
dependency files and succeed when all dependencies stay inside input. An
in-memory audit using the debug-profile paths passed during LI-004 verification.

<a id="li-012"></a>

### LI-012 — Native tar variants test rejects sample.tar.gz

**Status:** RESOLVED, 2026-11-14. The macOS native test link now places zlib before the JPEG provider, whose exported inflate symbols otherwise intercept gzip initialization. The executable binds inflateInit2_ to libz and all 35 archive cases pass, including compressed tar integrity and limits. Other hosts must likewise link the intended zlib provider.


**Status at report:** OPEN — cause unconfirmed. **Observed:** 2026-11-11, macOS,
`make test-input-baseline` (debug), including a rerun with local socket access.
**Lambda blocker:** no known block to the independent TeX script port. Ownership:
**D9.1.1v4 / D9.1.3v2**.

Input base `c5ca83e6d4d9793223117398cea5caea868d1040` plus the two-line HTTP POST
repair fails `ZipTest.NativeTarVariantsCaptureExactBytesAndNames` at
`test/test_zip_gtest.cpp:602`: opening `test/input/archive/sample.tar.gz` returns
null with `tar: invalid or unsupported archive`. The uncompressed tar fixture
succeeds; the gzip variant is expected to preserve the same entries and bytes.
The archive suite reports 34/35 passing, and a focused rerun reproduces the
failure in 2 ms. A separate baseline and rerun with local socket access report
the same failure, with the other 34 cases passing. No archive source, fixture,
expectation, or vendor dependency was edited for these observations.

Reproduce from input:

```sh
./build/test/test_zip_gtest --gtest_filter=ZipTest.NativeTarVariantsCaptureExactBytesAndNames
```

Evidence: `../temp/kianhup-hang/input-baseline.log` and
`tar-control.log`. Existing LI-001 / LI-002 also fail the input aggregate;
all seven local HTTP cases and the Lambda viewer gate pass. No archive repair
or confirmed regression attribution is claimed.

<a id="li-031"></a>

### LI-031 — Unclosed optional math argument stalls native LaTeX parsing

**Status:** RESOLVED, 2026-11-14. The fallback command parser leaves an unmatched bracket for punctuation parsing instead of retrying without advancing. The native unclosed-argument regression terminates successfully.


**Status at report:** OPEN, reproduced 2026-11-13 with the local macOS Lambda snapshot.
**Lambda task blocker:** yes for malformed-input parity and safe full script
port qualification. D10.2.2 governs the current LaTeX input boundary.

Parsing `\begin{document}$\downarrow[unclosed$\end{document}` with
`parse(..., {type: "latex"})` did not return within five seconds; the timed
process was stopped. The fallback loop in `lambda/input/input-latex-c.cpp`
sees `[`, calls `parse_brack_group`, and repeats without advancing when the
bracket is unclosed: `consume_group_span` returns false for that form.
Parsing should terminate with a recoverable result or a diagnostic.

Reproduce with `python3 temp/expl3-script/verify-native-unclosed-math-bracket.py`;
the script and `native-unclosed-math-bracket-result.txt` are in the same
temporary directory. The behavior was discovered while qualifying the Lambda
Script TeX engine. Native input ownership is unchanged, and no input source
or reference result is modified by this report.

<a id="li-033"></a>

### LI-033 — Escaped dollar ends native inline math prematurely

**Status:** RESOLVED, 2026-11-14. Dollar-delimited math skips escaped control symbols when searching for its closing delimiter. The escaped-dollar regression and neighboring native LaTeX cases pass.


**Status at report:** OPEN, reproduced 2026-11-13 with the local macOS Lambda snapshot.
**Lambda task blocker:** yes for native-reference comparison of dollar-delimited
math containing an escaped dollar; other math cases can proceed. D10.2.2
governs the current LaTeX input boundary.

Parsing `\begin{document}$\$\%\#$\end{document}` with
`parse(..., {type: "latex"})` reports a missing dollar delimiter, although
the escaped dollar belongs to the math content. The dollar-math delimiter
scan in `lambda/input/input-latex-c.cpp` stops at the first `$` byte without
checking whether it is escaped. The expected result is one closed math node
containing the escaped symbols.

Reproduction and error evidence are in
`temp/expl3-script/math-raw-probe.ls` and `math-raw-v4-interp.log`. The issue
was found while qualifying the Lambda Script TeX engine. Native input source
and existing reference results have not been modified for this report.

<a id="li-037"></a>

### LI-037 — JSON top-level parser accepts trailing non-whitespace

**Status:** RESOLVED, 2026-11-14. The script-facing JSON parser now rejects missing top-level values and non-whitespace trailing content using the existing parse-error channel. Focused valid and invalid controls pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with Lambda-opus debug
`8b5ebe56e` plus the editor work. **Lambda task blocker:** yes for safely
admitting JSON property editing. Input ownership follows **D9.1.1v4**;
**S7.5.1v6** governs acknowledgement of an ordinary parse error.

From the Lambda checkout, run `./lambda.exe temp/edit-viewer/invalid.ls`
(the script evaluates `parse` on malformed JSON). In particular,
`parse("1abc", 'json')` and `parse("1 2", 'json')` both return `1`
instead of reporting the non-whitespace content after the top-level value;
`parse("", 'json')` returns null. Malformed JSON must report an error rather
than admit a prefix as the complete document. The editor cannot trust the
script-facing result when checking whether a JSON file or edited value is
safe to save.

In `lambda/input/input-json.cpp`, `parse_json` calls `parse_value` once and
returns without checking the remaining cursor; the existing native
`parse_json_to_item_strict` has a trailing-content check. The script-facing
`parse(..., 'json')` dispatch and the intended strictness policy should be
confirmed by the input owner. This report does not change input sources or
reference results.

<a id="li-038"></a>

### LI-038 — TOML parser accepts a numeric prefix followed by invalid text

**Status:** RESOLVED, 2026-11-14. TOML document parsing validates the remainder of each value line before accepting the value. Numeric-prefix and extra-token regressions pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with the Lambda-opus
`8b5ebe56e` debug snapshot plus editor work. **Lambda task blocker:** yes for
safe property editing of the affected format. Input ownership follows
**D9.1.1v4**; **S7.5.1v6** governs acknowledgement of a parse error.

`parse("x = 1junk\n", 'toml')` returns `{x: 1}` instead of rejecting the invalid numeric value. The editor cannot use that result to validate property edits or saved data. Relevant owner code is `lambda/input/input-toml.cpp`; the exact cause has not been confirmed.

Reproduce from the Lambda checkout with `./lambda.exe temp/edit-viewer/other-invalid.ls`.
A malformed document must produce an error rather than a successful partial
value. No input implementation or reference was changed by this report.

<a id="li-039"></a>

### LI-039 — YAML parser accepts an unterminated flow sequence

**Status:** RESOLVED, 2026-11-14. Flow sequences and mappings now require their matching closing delimiter. Focused malformed cases and the existing YAML corpus pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with the Lambda-opus
`8b5ebe56e` debug snapshot plus editor work. **Lambda task blocker:** yes for
safe property editing of the affected format. Input ownership follows
**D9.1.1v4**; **S7.5.1v6** governs acknowledgement of a parse error.

`parse("a: [1,\n", 'yaml')` returns a map containing `a: [1]` rather than an error for the unclosed sequence. The editor cannot rely on this parser to validate the complete YAML document. Relevant owner code is `lambda/input/input-yaml.cpp`; the exact cause has not been confirmed.

Reproduce from the Lambda checkout with `./lambda.exe temp/edit-viewer/other-invalid.ls`.
A malformed document must produce an error rather than a successful partial
value. No input implementation or reference was changed by this report.

<a id="li-040"></a>

### LI-040 — iCalendar and vCard parsers silently drop malformed content lines

**Status:** RESOLVED, 2026-11-14. Calendar and contact readers report content lines missing a property name or value separator instead of silently discarding them. Focused malformed-line controls pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with the Lambda-opus
`8b5ebe56e` debug snapshot plus editor work. **Lambda task blocker:** yes for
safe property editing of the affected format. Input ownership follows
**D9.1.1v4**; **S7.5.1v6** governs acknowledgement of a parse error.

Both `parse("BEGIN:VCALENDAR\nBROKEN\nEND:VCALENDAR\n", 'ics')` and `parse("BEGIN:VCARD\nBROKEN\nEND:VCARD\n", 'vcf')` succeed with empty entries, instead of reporting the line without a colon. The editor cannot rely on these results to validate a complete contact or calendar file. Relevant owner code is `lambda/input/input-ics.cpp` and `lambda/input/input-vcf.cpp`; the exact causes have not been confirmed. This is consistent with the older I-F10 observation in `vibe/impl/Lambda_Impl_Code_Review.md`.

Reproduce from the Lambda checkout with `./lambda.exe temp/edit-viewer/other-invalid.ls`.
A malformed document must produce an error rather than a successful partial
value. No input implementation or reference was changed by this report.

<a id="li-041"></a>

### LI-041 — Markdown footnote definitions lack source ranges

**Status:** RESOLVED, 2026-11-14. With source positions requested, each Markdown footnote retains its authored range, and duplicate definitions remain available as separate source-backed nodes without changing ordinary reference resolution. Multiline, unused, and duplicate-definition controls pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with the Lambda-opus
`8b5ebe56e` debug snapshot plus editor work. **Lambda task blocker:** yes for
showing and editing surrounding Markdown while retaining footnote definitions
as individually rendered view-only blocks. Input ownership follows **D9.1.1v4**;
source-backed document editing is covered by **D9.3.4**.

`parse(source, {type: 'markdown', sourcepos: true})` gives authored paragraph,
HTML and math blocks `sourcepos`, but moves footnote definitions into a generated
`<footnotes>` element whose individual `<footnote>` nodes have no original
source positions. The original source may contain multiple definitions and
unrelated link definitions in the same gap. Consequently the Lambda editor
cannot reliably associate each footnote with its exact authored lines, and
shows the combined gap as raw source instead of rendering the footnote. The
existing `edit_md_view_only` and `edit_md_view_only_keys` UI replays reproduce
this with the original editor as well as the current changes.

Reproduce from the Lambda checkout with
`./lambda.exe temp/edit-viewer/md-footnote-probe.ls` using
`test/ui/edit/kept.md`; its output shows the source-positioned body nodes,
the generated footnote without a `sourcepos`, and the combined source gap.
`lambda/input/markup/block/block_document.cpp` records source positions for
ordinary blocks but `parseFootnoteDefinition` constructs the note separately.
The input owner should retain each definition's original span when source
positions are requested, including multiline, duplicate and unused definitions.
No input implementation or reference was changed by this report.

<a id="li-042"></a>

### LI-042 — CSV separator detection counts tabs inside quoted fields

**Status:** RESOLVED, 2026-11-14. Separator detection scans the first logical record, ignoring separators and line breaks within quoted fields and handling doubled quotes. Focused detection and parsed-row controls pass.


**Status at report:** OPEN, reproduced 2026-11-13 on macOS with the Lambda-opus
`8b5ebe56e` debug snapshot plus editor work. **Lambda task blocker:** yes for
property editing CSV files whose first quoted record contains more tabs than
commas. Input ownership follows **D9.1.1v4**; **D9.3.4** covers document
processing.

`parse("\"a\t\tb\",c\n1,2\n", 'csv')` treats tabs inside the
quoted first field as separators and returns one field named `a\t\tb`
with value `1,2`, instead of the two CSV columns `a\t\tb` and `c`
with values `1` and `2`. The viewer uses this input parser. The editor's
matching delimiter choice cannot safely build the property tree for this
source and currently falls back to source editing.

Reproduce from the Lambda checkout with
`./lambda.exe temp/edit-viewer/csv-separator-probe.ls`. In
`lambda/input/input-csv.cpp`, `detect_csv_separator` counts every tab and
comma on the first physical line without tracking quoted fields. Detection
should ignore delimiters inside quoted fields, including escaped quotes and
multiline quoted records. No input implementation or reference was changed
by this report.

<a id="li-043"></a>

### LI-043 — Man parser renders the zero-width `\&` escape as text

**Status:** RESOLVED, 2026-11-14. The man inline reader consumes the roff zero-width escape without producing visible text. The focused man regression passes.


**Status at report:** Open. **Blocks:** rich editing of man pages in `lambda edit`.

Reproduction from the Lambda checkout: run a Lambda script evaluating
`parse(".SH NAME\n\\&test\n", 'man')` and
`parse(".SH NAME\ntest\n", 'man')`. The first result contains a span with
text `&test`; the second contains text `test`. The roff `\&` escape is
zero-width, so these inputs should have the same visible paragraph text.
The checked output is in `Lambda-opus/temp/edit-viewer/man-input-probe.log`.
The parser's treatment of this escape is unconfirmed; this report identifies
observable behavior rather than an implementation cause. The Lambda man writer
uses `\&` to protect text, so its output cannot currently round-trip through
the input parser. The editor retains source mode until the input owner fixes it.

<a id="li-045"></a>

### LI-045 — Textile formatter and parser disagree on ordinary spans

**Status:** RESOLVED, 2026-11-14. Explicit Textile input now selects the Textile adapter, and its inline parser reads the span delimiters emitted by the existing writer. The writer assertion and parse-format-parse control pass.


**Status at report:** OPEN. **Lambda task blocker:** yes for rich editing of Textile
paragraphs whose parsed model contains a span. D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/markup-roundtrip-probe.ls`.
The Textile input `h1. Title\n\nHello _world_.\n` parses with paragraph text
`Hello `, an emphasized `world`, and `.`. Formatting its Markdown-equivalent
Mark Doc to Textile produces `h1. Title\n\n%Hello _world_.%\n\n`.
Reparsing that output includes the surrounding `%` characters as visible text.
The input-owned Textile formatter's custom `span` handler emits `%` delimiters;
whether the formatter or parser should change is not yet confirmed. The editor
cannot save this round trip without changing visible text and falls back to source.
Evidence: `Lambda-opus/temp/edit-viewer/markup-roundtrip-probe.log`.

<a id="li-046"></a>

### LI-046 — Org table and source block round trips lose structure

**Status:** RESOLVED, 2026-11-14. Org input recognizes plus-delimited table separators; its writer accepts shared table nodes and writes language-free block code as a source block. Focused parsing and format-then-parse controls pass.


**Status at report:** OPEN. **Lambda task blocker:** yes for rich editing of affected
Org tables and source blocks. D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/markup-structure-roundtrip.ls`.
The Org input `| A | B |\n|---+---|\n| one | two |\n` is parsed with the
separator as a data row; after Markdown conversion the input-owned Org writer
returns an empty string. The source block `#+BEGIN_SRC\nhello\n#+END_SRC\n`
parses as block code, but formatting its Markdown-equivalent model produces
`~hello\n~`, which reparses as ordinary paragraph text. The responsible parser
and formatter paths are input-owned; the exact causes remain unconfirmed. The
editor falls back to source instead of losing table or code structure. Evidence:
`Lambda-opus/temp/edit-viewer/markup-structure-roundtrip.log`.

<a id="li-047"></a>

### LI-047 — reStructuredText literal block changes content on round trip

**Status:** RESOLVED, 2026-11-14. The RST literal-block reader saves its introduction before reusing the parser buffer, and the formatter emits block code as raw indented directive content. Literal-block and multiline-code format-then-parse controls pass.


**Status at report:** OPEN. **Lambda task blocker:** yes for rich editing of affected
RST literal blocks. D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/markup-structure-roundtrip.ls`.
For `Example::\n\n  code\n`, the input parser returns a div with a
paragraph `code` and a pre/code block also containing `code`; the `Example:`
introduction is absent. Formatting its Markdown-equivalent Mark Doc back to
RST yields a paragraph and inline-code syntax, which reparses to a different
model. The input parser and formatter are both input-owned; the exact root
causes have not been confirmed. The editor falls back to source to avoid
changing the document. Evidence:
`Lambda-opus/temp/edit-viewer/markup-structure-roundtrip.log`.

<a id="li-048"></a>

### LI-048 — Org writer merges a following paragraph into a list

**Status:** RESOLVED, 2026-11-14. The Org writer emits a blank line after a top-level list, preserving a following paragraph on reparse. The focused structural round trip passes.


**Status at report:** OPEN. **Lambda task blocker:** yes for rich editing of Org files
with a list followed by a paragraph. D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/org-combined-probe.ls`.
The source in `test/ui/edit/sample-org-list-followed.org` contains a two-item list followed
by a separate paragraph with a link. The input-owned Org formatter emits the
list and paragraph without a separating blank line, and reparsing places the
paragraph inside the second list item. The editor falls back to source rather
than changing the document structure. The precise formatter cause is not yet
confirmed. Evidence: `Lambda-opus/temp/edit-viewer/org-combined-probe.log`.

<a id="li-049"></a>

### LI-049 — Org parser treats slash emphasis as plain text

**Status:** RESOLVED, 2026-11-14. The inline prefilter recognizes Org-only emphasis markers before choosing the plain-text fast path. Slash emphasis and neighboring markers pass focused tests.


**Status at report:** OPEN. **Lambda task blocker:** yes for rendering and rich editing
of Org italic text. D9.3.4 governs document processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/org-combine-census.ls`.
Both `Hello /world/.\n` and `/world/\n` parse as plain paragraph strings
containing the slash delimiters instead of an emphasized run. The viewer and
editor both consume this input-owned parse result. The exact cause is not yet
confirmed. Evidence: `Lambda-opus/temp/edit-viewer/org-combine-census.log`.

<a id="li-051"></a>

### LI-051 — Properties parser treats escaped key separators as delimiters

**Status:** RESOLVED, 2026-11-14. The properties reader decodes escaped key separators, whitespace, and continued key lines before splitting the entry. Focused input tests pass. The separately owned Lambda editor source-span scanner still needs its own repair, recorded in the [Lambda ledger](Lambda_Issue_Ledger.md#input-format-integration-handoffs--2026-11-14).

The properties escape adapter also preserves Java's literal `b` for `\b` in
keys and values, instead of applying the shared JSON backspace rule. The
extended key/value regression reproduced the difference before the repair.


**Status at report:** OPEN. **Lambda task blocker:** yes for correct property editing of
Java properties with escaped key separators or spaces. D9.3.4 governs document
processing.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/property-escape-probe.ls`.
For `a\=b=c`, parsing as `properties` currently returns key `a\` and value
`b=c`; Java's `Properties.load(Reader)` rules specify key `a=b` and value `c`.
The editor's current source-span scanner makes the same split and thus presents
this file incorrectly. A separate probe of `key\ space=value` produces a parse
error. The Input implementation cause is unconfirmed. Once the Input provider
is corrected, the Lambda editor's span scanner also needs to recognize escaped
separators and preserve their spelling; do not patch around the Input parser.
Evidence: `Lambda-opus/temp/edit-viewer/property-escape-probe.log`. Reference:
[Java Properties.load documentation](https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/util/Properties.html#load(java.io.Reader)).


<a id="li-050"></a>

### LI-050 — Markdown parser drops trailing spaces inside link labels

**Status:** RESOLVED, 2026-11-14. **Lambda task blocker:** cleared for link-label
whitespace and `editor_markdown_link_roundtrip`. D9.3.4 governs document
processing; D9.1.1v4 / D9.1.3v2 assign the parser repair to Input. The user
explicitly authorized the owner-side repair after the initial boundary report.
Implementation and complete verification:
[Markdown link-label whitespace](<impl/Lib_Input_Impl_Markdown_Link_Whitespace (done).md>).

The parser now applies paragraph-ending space removal only outside the existing
link-label context, in both the plain and mixed-content paths. Nine label cases
reproduce the defect before the fix; all 77 modular markup tests and 1,337
CommonMark examples pass afterward. Lambda's 97 editor tests and full 7,509-test
baseline pass. The Input aggregate retains its separately recorded LI-001,
LI-002 and LI-012 failures (5,235 passed, three failed, one skipped).

The following records the original reproduction and diagnosis before repair.

From the Lambda checkout run `./lambda.exe temp/edit-viewer/md-link-label-probe.ls`.
Parsing `[ leading and trailing ](https://example.org)` yields an `<a>` whose
text is ` leading and trailing`, missing the trailing space; formatting that
model confirms the lost space. The Markdown source spells the trailing space
inside the link and it is visible before adjacent text. The editor's rich
link command can produce this valid source when a selection ends in a space;
its preservation check must refuse the save while reparsing changes the model.
Earlier evidence: `Lambda-opus/temp/edit-viewer/md-link-label-probe.log` and
`Lambda-opus/temp/edit-viewer/link-model-new.log`.

**Confirmed pre-fix cause:** `lambda/input/markup/inline/inline_link.cpp:604` passes the
label to `parse_inline_spans` with `parser->state.link_depth` incremented.
That parser applies paragraph-ending space removal to nested link content:
its plain-string path at `inline/inline_spans.cpp:151` trims the label, and
the mixed-content flush at `inline/inline_spans.cpp:676` does the same.
The missing distinction is paragraph-ending versus nested-inline whitespace;
this belongs in the Input parser, with coverage for both paths.

**Pre-fix reproduction:** Lambda `9bf2203cd2277dac94e5b1c9310bf84f43107eb3`,
existing debug host, AUTO tier, 2026-11-14:
`./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/editor_markdown_link_roundtrip'`
returns `[false, false, true, true]` instead of `[true, true, true, true]`.
Run `./lambda.exe temp/failed-dom-editor-tests/link-probe.ls` from Lambda to
inspect the original and reparsed models: the selection is `short paragraph `,
the exported link is `[short paragraph ](https://example.org)`, and reimport
changes its label to `short paragraph`. The round-trip guard correctly rejects
the changed paragraph. Evidence: Lambda
[`link-repro.log`](../lambda-input/temp/failed-dom-editor-tests/link-repro.log),
[`link-probe.ls`](../lambda-input/temp/failed-dom-editor-tests/link-probe.ls) and
[`link-probe.log`](../lambda-input/temp/failed-dom-editor-tests/link-probe.log).
The initial Lambda task stopped at the ownership boundary. After explicit
authorization, the Input repair cleared the blocker while retaining the golden
and whitespace-preservation guard.


<a id="li-028"></a>

### LI-028 — Type-prefixed nested pseudo selectors consume following rules

**Status:** FIXED, 2026-11-13; fresh online captures are running. **Lambda blocker:** the reduced DigitalOcean
hero omission is repaired. Ownership **D9.1.1v4 / D9.1.3v2**, parsed source and
scratch **D5.5.1**, selected export frame **D10.1.7** unchanged.

DigitalOcean's authored `html[data-pdocs=home]` rule includes `body::before`
before its home hero/media styles. `css_parser.cpp` treated any IDENT followed
by COLON as a declaration in both style and nested conditional blocks. Thus
`a:hover`, `p::before` and functional pseudo selectors consumed their blocks
as malformed declarations, losing later nested rules. The saved script-free
HTML/CSS and small type-pseudo controls reproduce it; explicit `&` and plain
type selector controls work. CSS Syntax3§5.5.5/§5.5.6 requires a failed
declaration to be reconsidered as a qualified rule.

One shared `css_nested_item_starts_rule` helper now disambiguates a top-level
rule block while retaining custom-property component blocks, single-block
values, function/bracket contexts and semicolon recovery. Both parsing
contexts reuse it; no authored CSS or selector is rewritten. The known-property
names `font:hover` and `color:is(.active)` are covered too.

The complete114-case native parser suite passes, including two new cases with
16 selector/context variants and a custom-block control. Twenty real PNG cases
improve2/20 to20/20; independent Chromium agrees on all20. Identical saved
DigitalOcean HTML/CSS with scripts removed now restores the lighthouse image,
paragraph width and hero spacing. Original online references remain fixed;
independent cookie/navigation/background differences are not claimed repaired.
111 online CLI/capture/comparator checks and17 recorded layout suites pass:
630 JS layouts,4979 static passes,295 recorded partials,zero regressions/skips.
Full Input aggregate records5223passes,one skip and the same independent
GraphParserTest.ParserLocBudget, TransitionTreeExternalParentTest.InadmissibleParentsAreDeclined,
and ZipTest.NativeTarVariantsCaptureExactBytesAndNames failures (LI-001 /
LI-002 / LI-012). Those sources/fixtures are unchanged; the complete aggregate
is not called green. Live nested-rule mutation and CSSOM identity add74 UI
assertions with the pinned host.

Evidence: Lambda `temp/online-visual-round18/`: `parser-before.log`,
`parser-after.log`, `png-before.log`, `png-after.log`, `browser-controls.json`,
`digitalocean-{before,after}.png`, frozen HTML/CSS and `layout-results.json`.
Host SHA256 `2119303b51366ffd876710921373c88ea244d4cfe84f159572136e6cd1fe37fc`
uses Lambda0c5c0ba0d / Radiant eb39862 plus RI-062 / Input4d1ad20 plus this
parser repair. An initial native custom-block test also exposed an unrelated
single-block width retention discrepancy; its control uses a definite width
to isolate nesting, and that discrepancy is not claimed fixed.


<a id="li-025"></a>

### LI-025 — Cookie domains include URL ports

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for cookie transport.
Ownership **D9.1.1v4 / D9.1.3v2**. Cookie host extraction reads Url.host, which
contains a nondefault port, instead of the canonical hostname. Cookies then
fail matching on another port of the same host, and curl rejects their imported
port-bearing domain for subresource requests. [RFC6265 §8.5](https://www.rfc-editor.org/info/rfc6265/#section-8.5)
does not isolate cookies by port.

Use url_get_hostname, retaining the established empty-authority behavior for
hostless file documents. CookiesShareAHostnameAcrossPorts fails before and
passes after; a distinct hostname still receives no cookie. The added
FileDocumentCookiesRetainTheirEmptyHostname and existing JS dom_document_cookie
golden protect file compatibility. Initial and script-created image requests
in the real HTTP export tests now carry the shared response/DOM cookies.
The three new input cases are included in the local baseline filter.

Final input aggregate:5224 passes,1 skip, with the same independent graph LOC
budget, transition-tree external-parent and sample.tar.gz failures (LI-001 /
LI-002 / LI-012). No affected source or fixture for those failures changed.
Full AST574 and real HTTP/session6 checks pass with native services available.
Evidence in Lambda temp/online-visual-round8/: http-before.log,
http-hostless.log, input-baseline-final.log, session-qualified.log,
js-baseline-ast-qualified.log. Capture qualification is in Lambda
 test/online/README.md; native RI-045 provides the missing export profile.

<a id="li-024"></a>

### LI-024 — Document response cookies retain HTTP line terminators

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for the reduced
export-session case. Ownership **D9.1.1v4 / D9.1.3v2**. The synchronous document
header callback copies the full curl span into cookie_jar_store. Unlike the
other network frontends, it leaves CRLF intact. A final Path=/ then contains
line endings and fails matching; a terminal HttpOnly/Secure name fails the
attribute comparison. The two-cookie local response exposes the private cookie
to document.cookie while omitting the ordinary visible cookie.
[libcurl HEADERFUNCTION](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html)
delivers bounded, non-NUL-terminated header lines including their terminator.

Use one cookie_jar_store_response_header helper to normalize the bounded span
and store the copied value. The document, synchronous-resource and curl-multi
callbacks reuse it, replacing duplicate normalization code. Allocation failure
aborts the transport. No vendored curl implementation is changed.
DocumentCookiesKeepFinalPathAndHttpOnlyAttributes fails before and passes after;
script-visible cookies exclude HttpOnly while request cookies include it.
All ten local HTTP cases and six real-renderer session tests pass. Evidence:
Lambda temp/online-visual-round8/{http-before.log,http-after.log,http-hostless.log,
session-qualified.log}. The full input gate retains the independent LI-001,
LI-002 and LI-012 failures; expectations and filters were not weakened.

<a id="li-023"></a>

### LI-023 — Missing Oklab/OkLCh components invalidate neutral theme colors

**Status:** FIXED, 2026-11-12. Input color computation ownership follows
**D9.1.1v4 / D9.1.2v2**. **Lambda blocker:** cleared for the reduced theme case.
Nuxt uses `oklch(14.5% 0 none)` in its neutral palette. The shared color helper
already retains a missing-channel bit and a zero used value, but the caller
explicitly disallowed missing components for perceptual functions. This
invalidated the palette fallback, leaving the dark page background transparent
and white headings invisible. CSS Color 4 §4.4 permits `none` in modern color
syntax and treats missing components as zero outside interpolation.

The caller now admits missing channels through the existing helper. Six
regression cases cover lightness/chroma/hue/alpha, stored missing bits,
serialization, round trips and RGB consumers. Two formerly-invalid assertions
were corrected to test actually invalid identifiers instead. The Nuxt hue
case fails before the change; all 112 CSS parser tests pass after. This does
not implement missing-component interpolation in color-mix or new color spaces.
Evidence: Lambda `temp/online-visual-round3/` reduced pages, `color-tests.log`
and Nuxt captures. The report must retain unrelated icon/mask limitations.
Spec: https://www.w3.org/TR/css-color-4/#missing

<a id="li-021"></a>

### LI-021 — All-frame GIF decoding bypasses the process memory budget

**Status:** RESOLVED, 2026-11-11. **Lambda task blocker:** cleared for Studio
PNG capture. Ownership **D9.1.1v4 / D9.1.2v2 / D9.1.3v2**. Sibling updates
were explicitly authorized by the user.

`image_gif_load_from_memory` checked each container against its decoded-byte
limit but allocated all composited frames without process admission. Several
individually valid animations on `https://studio.design/` retained
1,059,694,508 image-category bytes, preventing a viewport PNG allocation.
An allocation stack identifies `image_gif_load_from_memory` during intrinsic
image measurement. The existing preflight peak estimate is now returned and
admitted through `memtrack_allow_allocation` before giflib expansion. Structured
later-frame decoding reports `IMAGE_LIMIT_EXCEEDED` when admission fails.
The process limit is unchanged; independent first-frame decoding remains
available when the complete animation cannot fit.

Full image I/O passes 35/35, including palette/disposal/loop behavior, budget
rejection and still decoding; native Radiant GIF playback passes 23/23.
Studio now exports with 711,756,076 image-category bytes. Its selected initial
frame is mostly blank, so this is load/export qualification, not visual parity.
Evidence: Lambda `temp/online-recovery/studio-image-trace3-output.log`,
`studio-budget-fixed-output.log`, and canonical Studio capture; owner
`temp/online-gif-tests.log`; Radiant `temp/online-gif-player-tests.log`.

Coordinated Lambda **LR03-46** also reuses `map_put_heap` for private JS fields.
The input builder now grows storage before publishing its new descriptor,
preserving **D6.3** at its collecting growth callback. The callback sees the
complete old field list until the added lane and value exist. Lambda's forced-GC private growth regression passes; its full core gate passes
7,415/7,415 including 859 JS, 368 GC stress and 47 representation cases.
Qualification is recorded under Lambda LR03-46 and its final test logs.

<a id="li-020"></a>

### LI-020 — Empty POST reads terminal stdin and blocks page initialization

**Status:** RESOLVED, 2026-11-11.
**Lambda blocker:** removed; the plain terminal launch now renders the login form.
Owner authority: **D9.1.2v2**, **D9.1.1v4 / D9.1.3v2**. The user explicitly
authorized the owner-side fix on 2026-11-11 after the original boundary report.

Confirmed on input `c5ca83e6d4d9793223117398cea5caea868d1040` and Lambda
`456ba137dfe84a19eb59ef8c48f602fac7ffce28` plus its pending cookie-capability
repair. Plain interactive startup hangs before first layout. The public page's
Acro script performs synchronous template POST requests with no body.
`http_fetch2()` sets `CURLOPT_POST=1`, but `http_set_request_body()` returns
without setting a body or length when `FetchConfig.body` is null. Curl then
uses its default stdin reader. The main-thread sample shows
`js_xhr_send -> http_fetch2 -> curl_easy_perform -> Curl_client_read -> fread
-> __read_nocancel`. The process remains idle, waiting for terminal input;
the network timeout and CPU watchdog do not interrupt that blocking read.

Earlier automated runs had EOF on stdin and completed, hiding this separate
failure after Lambda's missing `navigator.cookieEnabled` was fixed. The
[curl POSTFIELDS contract](https://curl.se/libcurl/c/CURLOPT_POSTFIELDS.html)
requires an explicit empty buffer or zero POST length for an empty request.

Evidence: `../temp/kianhup-hang/user-hang.log` and
`user-process.sample.txt`, captured from the user's process PID 53623 without
changing or terminating it. Reproduction: run the exact `view` command in a
terminal with stdin open. A regression must keep stdin open rather than rely
on subprocess EOF. Repair: set POST's initial length to zero, then let the
existing optional-body helper supply an explicit payload and length. No
vendor curl patch or Lambda-side workaround is needed.

**Repair and verification:** `lambda/input/input_http.cpp` `http_fetch2()`
now sets POST's initial length to zero before `http_set_request_body()`
applies any explicit payload. `test/test_http_gtest.cpp`
`EmptyPostDoesNotReadStdin` covers null and empty bodies with a pipe whose
writer remains open, and is selected by the owner offline baseline filter.
All seven local HTTP tests pass. Lambda's rebuilt debug host reaches first
render with live PTY stdin and the exact viewer argv, then a windowed PTY
replay confirms the complete form (`plain-pty.out`, `plain-pty.log`,
`login-window.png`, `login-window.log`). The viewer integration baseline
passes: 8,161 passed, 299 partial, 20 skipped of 8,480, zero failures
(`radiant-baseline.log`). The broader input baseline still fails LI-001,
LI-002 and the separately recorded LI-012; its HTTP cases all pass. Evidence
and the completion checkpoint are under `../temp/kianhup-hang/`.
No credentials were entered or login submitted.

<a id="li-016"></a>

### LI-016 — Combined cloud/peer agent stalls during input/lib teardown

**Status:** RESOLVED — owner fix and combined-agent qualification complete (2026-11-12).
The user explicitly authorized `lambda-input` source changes for this issue.

**Confirmed cause:** InputManager releases retained Inputs in creation order.
Each Input's context was pushed onto a singly linked newest-first sibling list;
`detach_from_parent_locked` scanned that list to remove each oldest context.
The resulting quadratic pointer traversal dominated process exit. The stall was
not evidence of a graph cycle or skipped allocator destruction.

`MemContext` now retains a private previous-sibling link. Creation and removal
maintain both links under the existing registry lock, making arbitrary detach
constant time. Cleanup order and backing dependency teardown remain unchanged
(D5.2.1, D5.2.5). A mixed-order lifecycle test checks survivors, head/middle/tail
removal, reinsertion and final cascade destruction.

Release reproduction (`-O3 -DNDEBUG -std=c17`) compiles the exact before/after
source and exercises the same input-order topology. For 80,000 contexts, full
creation/destruction changed from about 3.03 seconds to a median 0.0097 seconds;
this is an isolated lifecycle measurement, not application-throughput evidence.
Owner focused suites pass **18 memory-context + 7 factory + 62 pool = 87 tests**.
Lambda's rebuilt native host passes **17/17 real-S3 cloud/combined-agent checks
on interpreter, AUTO and JIT**, with the original 180-second deadline unchanged.
The combined case is now mandatory in the default gate. Focused sync GC/poison
checks pass **9/9 per tier**; the ordinary sync sweep passes **51/51**.

Full input baseline: **5,220 passed, 3 failed, 1 skipped** across 106 executables
(5,224 tests). Only the already recorded LI-001 parser budget, LI-002 external-parent
expectation and LI-012 tar fixture fail; the font-dependent emoji test skips.
Full Lambda baseline: **7,493/7,494**, with only the existing TeX/expl3 golden
mismatch. These results do not mark those unrelated issues resolved.

Evidence: input `temp/li016/{before-source.json,after.json,mem_context.log,
mem_factory.log,mempool.log,input-baseline.log}`, Lambda
`temp/li016/{final-matrix.json,final-combined-*.log,final-stress-*.log,
lambda-baseline.log}` and the
[cloud implementation record](<impl/Lambda_Impl_Sync_Cloud (done).md>).
The former Lambda blocker is removed. LI-015 concurrent administration remains
separate and open.

<details><summary>Original observation and investigation — 2026-11-11</summary>

**Status:** OPEN — ownership cause unconfirmed; stack observation is in input/lib
cleanup, not proof of a provider defect. **Observed:** 2026-11-11, copied macOS
native host in Lambda's isolated `temp/sync-cloud/worktree`, interpreter and AUTO.
**Lambda blocker:** yes for the additional combined cloud-poll/peer-listener agent
acceptance case; ordinary one-shot cloud exchange/recovery passes independently.
D9.1.1v4, D6.3.3 and D11.1.2 apply. No sibling source was changed.

Run `test/sync_cloud/test_cloud.py` against the real local S3 test store. The final
combined case starts `cloud_cli.ls` action `run`, with four iterations and a
60-second cloud interval, and completes a peer transfer into that repository.
The peer client exits successfully and file bytes are installed, but the combined
agent does not finish within the existing 180-second test deadline. Both
interpreter and AUTO cases time out; no timeout was raised or harness patched.

The interpreter process reports completion (`executed=1`, about 172M nodes and
2334.50 MiB peak RSS) in its diagnostics before the driver timeout. A one-second
macOS stack sample finds the main thread in `lambda_main_finish` ->
`InputManager::destroy_global` -> `InputManager::reset_inputs` ->
`input_release_document_resources` -> `mem_context_destroy` ->
`destroy_ctx_locked` (`lib/mem_context.c:238`). Process inspection shows a CPU-bound
exit with a large memory footprint. Determine whether this is input/lib graph
teardown, an upstream Lambda ownership error, or pathological accumulated cleanup;
no cycle, allocator corruption or exact shared cause is confirmed.

Evidence relative to the Lambda checkout:
`temp/sync-cloud/worktree/temp/sync-cloud/combined-failure-{interp,auto}.log`,
`combined-evidence/{interp,auto}/31-run.err`, and
`agent-sample.txt`. The full process gate preserves this failure under
`--combined`; its ordinary cloud checks must not be represented as passing the
blocked combined gate. Combined-agent qualification work is stopped. Do not
change sibling sources, suppress teardown, extend timeouts, or add a process-exit
workaround from Lambda. Owner investigation is needed before advertising this
additional combined command as qualified.

</details>

<a id="li-010"></a>

### LI-010 — TeX expl3 format startup exceeds Lambda's five-second debug test target

**Status:** RESOLVED — denied-format recursion stopped and TeX macro expansion
tuned. **Observed:** 2026-11-10,
Lambda HEAD `5c0d7c68c5f5dde248d6d951ec85b6d62f928569`, macOS arm64, debug.
**Lambda blocker:** no; both `AmbientExplThreeFormatIsNotAnAuthorityCache`
cases now meet the five-second target in the measurements below. Authority: **D9.1.1v4 /
D9.1.3v2** (ownership), **D10.2.2** (TeX expansion), **D11.2.1** (authority).

After rebuilding Lambda against the input archive, reproduce from `../lambda` with
`./test/test_security_runtime_gtest.exe --gtest_filter='InterpreterAndJit/SecurityRuntimeTest.AmbientExplThreeFormatIsNotAnAuthorityCache/*'`.
Before the fix, both cases passed but took 13.883 s and 8.482 s consecutively
in one process.
The user's target is less than five seconds per case without weakening the
trusted-format control or the scoped authority denial.

A three-second CPU sample of case `/0` places all 1,559 main-thread samples in
`tex::engine_create -> load_expl3_format -> build_expl3_image -> run_preload`,
including macro expansion, token scanning and temporary token-array allocation.
The test body calls the input-owned engine API directly; neither Lambda tier
execution nor MIR codegen lies on this sampled path. Relevant owner symbols:
`lambda/input/input-tex-main.cpp:927` (`engine_create`) and
`lambda/input/input-tex-format.cpp:535,559` (`build_expl3_image`,
`load_expl3_format`). The original warm `/1` cause was not sampled; expensive
image load/kernel setup was an unconfirmed candidate. A scoped engine must
build privately instead of trusting the ambient format.

Evidence: `../lambda/temp/test-runtime-tuning/security-before.log`,
`security-before.json`, `security-sample.txt`, and `security-profile.log`.

**Resolution:** When a requested expl3 format could not be built or loaded, the
kernel entry-point stubs retried `\RequirePackage{expl3}` until the
20-million-expansion budget expired. The engine now stops at that request
with `tex-expl3-unavailable`, after the scoped client has denied access.
Macro calls also avoid a temporary output list: parameterless bodies are
read from their arena-backed definitions, and substituted bodies are written
directly to the existing pooled input buffer. Expansion limits and private
format builds remain in force.

**Verification:** The Lambda security test was rebuilt from current source
against the modified input archive in this project's `temp/`. Three consecutive
processes passed both cases in 4.036–4.304 s cold and 4 ms warm per process
(`temp/li010-after.log`). The input TeX regression tests cover denied file
classification and reads, authorized private loading, first-request restart,
macro substitution, redefinition, and expansion budgets. All 151 recorded TeX
conformance lines match, and the expl3 package fixture expands identically with
ambient and authorized scoped resource access. The native fixture runners are
`temp/li010-conformance.cpp` and `temp/li010-expl3.cpp`.

<a id="li-009"></a>

### LI-009 — Shared-header migration blocks Lambda release tuning build

**Status:** RESOLVED — normal input and Lambda release builds passed after the owner header repair and Lambda consumer migration.
**Observed:** 2026-11-10, Darwin arm64, input HEAD `0b49aa017d` plus its
uncommitted shared-header migration. **Lambda blocker:** yes, candidate release
build for the Result55 regression-tuning task. Authority: **D9.1.1v4 /
D9.1.3v2** (source ownership), **D6.3** (shared runtime ABI).

From `../Lambda-opus`, reproduce with
`make build-lambda-split SPLIT_PROFILE=release SPLIT_OUTPUT=temp/result55_tuning/candidate1 JOBS=4`.
The input release build fails compiling `lib/datetime.c` with a conflicting
`DateTime` typedef (`lib/datetime.h:35` struct versus `lambda/lambda.h:592`
uint64_t), and compiling `lib/document_lifetime_guard.cpp` with unknown
`MemCategory` at `lib/string.h:42`. The latter include chain is
`mem.h -> memtrack.h -> lambda/lambda.h -> lib/datetime.h -> lib/string.h`,
which reaches the string API before the MemCategory declaration.

Complete compiler evidence:
`../Lambda-opus/temp/result55_tuning/build-candidate1.log` (lines 93–187).
Expected: the normal input release build succeeds with the shared headers
self-consistent in C and C++. These are observed include/type failures; the
intended final header layering is an owner decision. Header contents changed
again during diagnosis (datetime.h now forward-declares StrBuf), so the log
qualifies the failed build's source state, not a subsequent owner fix. No
input source or build rule was edited by the Lambda task. Lambda compiler
experiments remain unqualified until the normal release build succeeds.

**Verification, 2026-11-10:** the owner's subsequent header changes removed
the reported DateTime/MemCategory failures. A retry overlapped an active owner
curl build and failed with missing `conftest` files; process inspection confirmed
the concurrent configure/build. After that job finished, the normal split
release build reached Lambda and exposed stale `lib/lambda_api.h` and
`lib/lambda_alloca.h` consumers. Lambda-owned callers now use a shared
visibility-only wrapper over `lambda/lambda.h` and the existing `lib/alloca.h`.
The complete normal release build exited 0 and produced the unchanged-compiler
control at `../Lambda-opus/temp/result55_tuning/control`. Evidence:
`../Lambda-opus/temp/result55_tuning/build-control-migrated2.log`.
This task changed no input implementation/header/build source; owner edits
remain independently owned. Lambda tuning can resume.

<a id="li-006"></a>

### LI-006 — Region retirement needs the shared map allocator declaration updated

**Status:** RESOLVED — approved shared-header edit applied and verified on
2026-11-10. **Lambda blocker:** no. Authority: **D9.1.1v4 / D9.1.3v2**
(header ownership), **D6.3** (GC ownership).

The user explicitly approved the one-file exception for input-owned
`lambda/lambda.h`. The applied change exactly matches the reviewed patch at
`../lambda/temp/mvp_region_retirement/input-header.patch`: remove the region
opaque type and region-only declarations, and change
`map_alloc_for_type(TypeMap*, LambdaRegion*, int64_t)` to the GC-only
`map_alloc_for_type(TypeMap*, int64_t)`. No other input/Radiant source or header
was edited by Lambda for this retirement. The change remains uncommitted.

Lambda removed the allocator, block/cache helpers, heap cache fields, teardown
and region JIT entries, and migrated all 19 existing native calls plus MVP's
emitted call and import metadata. Input and Radiant have no implementation
callers. The former owner boundary was the only dependency blocking complete
retirement; no compatibility argument or alternate API was introduced.

Verification: input and Radiant archives and the private Lambda release rebuild
successfully. A tracked-source audit across all three projects finds no retired
region names. Nine selected native/profiling GTests, seven focused Python unit
methods, six JS map-growth/GC cases, four typed/untyped tree benchmark goldens
and four diagnostic root/memory parity cases pass. No owner or Lambda/Radiant
baseline suite was run. Receipts and source/binary identities:
`../lambda/temp/mvp_region_retirement/`; implementation record:
`../lambda/vibe/impl/Lambda_Impl_MIR_MVP_Regions_and_Profiling.md`.

The original issue was retained Full MIR declarations in the owner header,
not a reproduced input parser failure. The prior caller inventory/reproduction
is recorded in `../lambda/temp/mvp_region_retirement/source-audit.json`.

<a id="li-008"></a>

### LI-008 — ZIP output cannot store one entry uncompressed

**Status:** RESOLVED — per-file compression override implemented 2026-11-10.
**Observed:** 2026-11-10. **Lambda blocker:** no. Lambda's `.painting` files
are valid ZIPs without it.

ZIP output applied one `compression` option (`'deflate'` or `'stored'`) to every
entry. The method came from `write->method` in `lambda/input/input-zip.cpp` and
`options.method` in `lambda/io/zip_archive.cpp`. Container formats in the ODF,
OpenRaster and Krita `.kra` style require their first entry, `mimetype`, to be
stored uncompressed so file-type sniffers can read it at a fixed offset.
Lambda's painting files (`lambda.paint`, `Lambda_Pkg_Paint.md` PT12) follow that
layout but could only write `mimetype` first and deflated. Entry order is kept
when `deterministic` is false.

The original reproduction wrote
`<fs kind: 'dir', <fs name: "mimetype", kind: 'file', "x">>` with
`output(tree, path, {format: 'zip'})`; reading it with `input(path, 'zip')`
reported `'deflate'`, with no per-file way to select `'stored'`.

The constructed-file `compression` attribute now selects `'stored'` or
`'deflate'` for that file, overriding the archive default (S10.1.4). Absent
attributes inherit the default, while captured archive-node compression remains
source metadata. See [ZIP output](../lambda-input/README.md#zip-output) for an example and
the ordering and ZIP64 constraints on fixed-offset payloads.

Verification: `ZipTest.ConstructedFilesOverrideArchiveCompression` reproduced
both ignored override directions before the fix. Afterward all 35 cases in
`build/test/test_zip_gtest` passed, including fixed-offset payload, invalid
values, archive defaults, captured nodes, and deterministic ZIP64 coverage.

<a id="li-004"></a>

### LI-004 — Shared Lambda header retains retired Full MIR helper declarations

**Status:** RESOLVED — declarations retired on 2026-11-10. **Observed:** 2026-11-10.
**Lambda blocker:** no; Lambda has no remaining source/import/export consumer.
Authority: **D9.1.1v4 / D9.1.3v2** (header ownership), **D9.4.6** (host exports).

Lambda's follow-up to source retirement at `fd07a893f` removes 47 runtime helper
entry points. Its additional compiler/private-analysis cleanup stays Lambda-owned.
The caller audit covers current Lambda/MVP/T0/JS, tests, generated-name patterns,
assembly aliases, Jube export lists, and tracked input/Radiant sources. The
remaining references to removed helpers were 30 declarations in input-owned
`lambda/lambda.h`; Lambda did not edit that header. The input owner removed those
declarations after review.
No source consumer or host-export dependency was found. In particular,
`fn_call3_into` is **retained** because Radiant calls it through an assembly alias;
`fn_call_boxed_1_into` is retained for imported constrained-type predicates.

Reproduce the declaration inventory from the Lambda checkout with
`python3 temp/mvp_helper_retirement/helper_candidates.py` (audit input saved
before deletion), or read
`temp/mvp_helper_retirement/remaining-references.json` and `removed.json`.
Source proof and qualification status are tracked in
`vibe/impl/Lambda_Impl_MIR_Full_Retirement.md` in Lambda. This is coordinated API
cleanup from the retirement change, not a demonstrated input parser defect.

Verification: the debug input archives built successfully; tracked C, C++,
assembly and Python sources in input, Lambda and Radiant have no exact references
to the 30 names. `fn_call3_into` and the runtime unary boxed-call trampoline
remain. All 43 `MarkReaderTest` cases pass. The boundary dependency audit passes
when it reads the debug-profile object directory; the unmodified `make
check-boundary` target fails due to **LI-005**.

Historical owner declaration inventory (line numbers before removal):

- `Item list_fill` (line 1534)
- `void lambda_direct_field_store_image` (line 1550)
- `Item fn_call0_into` (line 1779)
- `Item array_float_literal_with_nulls` (line 2771)
- `Object* object_fill` (line 2833)
- `Item object_fill_checked` (line 2845)
- `uint8_t fn_string_char_eq_ascii` (line 2864)
- `uint8_t fn_string_char_eq` (line 2865)
- `Item fn_virtual_content_check` (line 2964)
- `Bool fn_not` (line 3046)
- `Item lambda_fn_colour_guard_args` (line 3073)
- `Bool lambda_type_value_is_exact` (line 3106)
- `Item lambda_fill_for_contract_int_lane` (line 3123)
- `Item lambda_array_empty_for_contract` (line 3139)
- `Item lambda_array_set_checked` (line 3157)
- `Item lambda_array_set_checked_inplace_item` (line 3167)
- `Item lambda_array_set_checked_lane` (line 3187)
- `Item lambda_array_set_checked_inplace_lane` (line 3190)
- `int64_t fn_len_a` (line 3323)
- `Item fn_join_consume_cert_array` (line 3469)
- `Item fn_join_consume_cert_array_item` (line 3470)
- `Item fn_join_consume_cert_array_pair` (line 3471)
- `Bool fn_starts_with_str` (line 3475)
- `Bool fn_ends_with_str` (line 3477)
- `Type* const_type_with_tl` (line 3541)
- `Item lambda_array_int_push_inferred_cow` (line 3753)
- `int64_t cow_path_set_packed_index` (line 3811)
- `Item cow_path_borrow_home_keys` (line 3819)
- `Item cow_place_leaf_fixed` (line 3830)
- `Item cow_place_leaf_keys` (line 3834)

<a id="li-067"></a>

## LI-067 — Omitted flex basis collapsed content in auto-height columns

**Status:** FIXED, 2026-11-17. Input owns the shared shorthand projection under
D9.1.1v4/D9.1.3v2. No formal ruling changes. In `css_properties.cpp`,
`css_project_shorthand_longhand` gave numeric `flex` shorthands an omitted
basis of `0px`. In an auto-height column with hidden overflow this is definite
zero, so `flex:1` / `flex:1 1` discarded content height. Cleanup.pictures' saved
hydrated source reduced its first app from about920px to240px and exposed the
black section below. The live page also has a separate client initialization
path; a restored saved source alone does not establish a live clearance.

The immutable shorthand default is now `0%`, shared by layout, substitution,
inheritance and computed-style consumers. Explicit `0`, `0px`, `0%`, `auto`,
`none`, and supplied bases retain their own meanings. This deliberately follows
interoperable browser behavior, **not the current Flexbox specification's
omitted-zero wording**. The distinction and collapsed-overflow consequences
are documented in [CSSWG issue5742](https://github.com/w3c/csswg-drafts/issues/5742).
There are no URL, class, content, viewport or capture-specific conditions.

Evidence in Lambda `temp/online-visual-round43`: the8-case PNG test has3 genuine
pre-fix failures, with explicit-zero and auto controls passing; independent
Chromium confirms all8 heights and computed bases. Input's
`FlexShorthandRetainsOmittedPercentageAndExplicitZeroBasis` directly checks8
projection forms. Native
`OmittedFlexBasisRetainsContentAcrossLiveShorthandChanges` checks6 live
shorthand/substitution transitions and their retained percentage modes.
Final qualification and page outcome are recorded in the round43 checkpoint
and comparison report. Capture admission, clocks and tolerances are unchanged.

<a id="li-068"></a>

## LI-068 — Automatic font faces loaded and cached a fixed regular instance

**Status:** FIXED, 2026-11-17. Input owns CSS descriptors and the unified font
registry under D9.1.1v4/D9.1.3v2; Radiant's registration bridge preserves the
new mode. Cache ownership remains D5.5.1. No formal ruling changes.

Affinity's hero requested weight700 from a variable face whose invalid
`font-weight:400 500 700` descriptor must be ignored. CSS Fonts4 §4.4 specifies
an initial automatic descriptor: match as normal, but do not clamp variation
axes. The parser instead initialized normal400. The font registry then loaded
its descriptor weight and cached only by size, losing the caller's requested
variable instance. Raster glyphs were visibly thin despite the existing
CoreText variation backend. The independent local SFNS fixture reproduces
this with omitted, explicit auto and invalid descriptors; all3 fail before
repair, while an explicit400 control passes.

`FONT_WEIGHT_AUTO` is a descriptor-only zero sentinel, without a data-layout
change. Omitted/invalid and explicit auto weight retain that mode; explicit
normal/numeric weights remain fixed. The registry matches auto as400, carries
the requested effective axis weight through file, memory/data and local
sources, and includes that weight in the retained-instance cache. Family and
per-codepoint loading share the same producer. Fixed descriptors retain their
clamp. Source selection and renderer clocks/budgets are unchanged. No page
condition or downloaded proprietary-font fixture is introduced.

This is scoped to automatic weights. General descriptor ranges, automatic
style/width descriptors, extended numeric grammars and text-stroke rendering
are not added here. [CSS Fonts4 §4.4](https://www.w3.org/TR/css-fonts-4/#font-prop-desc)
is the external descriptor reference; browser experiments distinguish
explicit400 from automatic weights rather than forwarding every request.

Lambda `temp/online-visual-round45` records4PNG checks (Mac system SFNS,
non-Mac skip explicit), four independent Chromium equivalences,10focused
font-face parser tests,9font/context tests and4Radiant font tests passing.
The generic registry controls use the existing FreeSans fixture and exercise
cache/glyph selection at multiple weights; Mac controls independently compare
4variable instances ×3glyph advances with their native raster face. Existing
advance handling already passes; no additional metric patch was needed.
A single broad baseline milestone and saved/live page outcomes are recorded
in the round45 checkpoint/report. Focused evidence alone does not establish
a live page clearance or a clean full baseline.

Round45 milestone: Affinity's live mean error improves14.6559%→8.8302%,
with the main variable headline restored. Four large corner images remain
absent, so the page retains high severity by manual review. The broad Radiant
run reports8488checks/8167passed/2failed/299partial/20skipped; neither failure
was in font selection. The retained atomic-role regression and stale omitted
flex-basis expectations were corrected; focused rechecks pass. The full
baseline was not repeated. This does not claim a clean full baseline or page
clearance. Evidence: Lambda round45 checkpoint and comparison repair gallery.

<a id="li-069"></a>

## LI-069 — Borrowed pseudo cascades invalidated authored ancestor inputs

**Status:** FIXED, 2026-11-17. D5.5.1 requires intrinsic queries to preserve
selected fragments; provider ownership is D9.1.1v4/D9.1.3v2. No ruling changes.
Mymind's saved hydrated navigation emitted three selected-flow restoration
errors. A release-visible trace showed list-item input revisions changing9→11
inside a measurement, while generation, style epoch and mutation epoch stayed
unchanged. An automated reduction retained three CSS rules and a small
navigation with an unrelated custom-layout attribute.

`dom_element_borrow_specified_style` reused authored-write invalidation when
refreshing a generated box's derived cascade. It expired ancestor inputs and
made the unchanged selected flow fail its valid stale-result guard. A shared
local refresh now expires the generated box's own style/version/intrinsic
cache and recompute flags. Same and replacement bindings use it; normal
attribute/text/style writes still invalidate ancestor queries. The replay
guard is unchanged. No page condition or capture policy is introduced.

Lambda `temp/online-visual-round46` records same/replacement/rebound pseudo
style controls and a real authored font write; the latter still invalidates
the host. The old executable fails all three new rendering controls. After
this repair and Radiant RI-101, seven PNG tests, four native tests, three
independent Chromium equivalences and integer lint pass. Two existing owned
flow/pseudo-font ownership tests also pass during implementation. The original
saved Mymind page has zero restoration errors instead of three. Its live
capture remains blank, so no high-severity clearance is claimed. Broad
baselines were deferred after the round45 milestone.

<a id="li-070"></a>

## LI-070 — Backed insertion ignored the native slot of DOM-only references

**Status:** FIXED, 2026-11-17. **Lambda blocker:** no.
Owner boundaries are D9.1.1v4 / D9.1.3v2; retained node identity follows
D5.5.1. No design ruling changes.

`dom_insert_detached_backed_element` accepted MarkEditor's relink whenever
the inserted child already had the requested parent. That proves membership,
not position: a DOM-only reference such as a comment has no backing slot.
Inserting a mixed fragment before that anchor reversed its element order,
causing Svelte's later traversal to read a text node instead of its element.
The helper now preserves the completed backing edit and places the live native
node immediately before the actual reference, detaching only its native link
when necessary. No new node, copied subtree or synthetic index is introduced.

The new native test covers both UI and non-UI documents, two consecutive
insertions, sibling identity and Mark ordering. It and the existing nested-array
and mixed-node backing controls pass. Four Lambda PNG cases compare append,
before, after and insertBefore with mixed comments/text/elements; the latter
two fail before and all pass after. Together with RI-102 and Lambda's fragment
and clone repairs, crates.io renders its main page instead of a blank green
canvas. Evidence is in Lambda `temp/online-visual-round47`; the online report
records the remaining visible differences and milestone qualification.
The milestone's borrowed-fragment-text regression was corrected in Lambda;
all nine persistent failed cases and the initial jQuery case pass focused checks.
The full baseline was not repeated after that correction.


<a id="li-075"></a>

## LI-075 — Missing extracted document resource-base resolver

**Status:** FIXED 2026-11-17. **Authority:** D9.1.1v4, D9.1.3v2.
**Dependent blocker:** Lambda full release compilation.

The extracted Input DOM API omitted `dom_document_resolve_url`, while Lambda's
DOM and script host still called it. The standard release build failed with an
undeclared function. Input now exports the owned resolver. It traverses source
identity, ignores inert HTML template content, uses the first HTML base href,
rejects invalid/data/javascript resource bases, and preserves the document URL.
Lambda's remaining stale inert-template helper call now uses Input's existing
source identity and namespace APIs.

Verification: the standard Lambda release build passes; all 24
`test_input_dom_native_gtest` cases pass, including
`ResourceBasePreservesDocumentIdentityAndSkipsTemplates`. No vendor edits or
Input-to-runtime dependency was introduced. Evidence is in the dependent
checkout's `temp/sync-memory-impl/{build-p0.log,input-dom-tests2.log}`.
