# Proposal: Semantic Break Kinds & Corpus-Sweep Text Tests

**Status:** Implemented (Part 1 complete, Part 2 baseline established)  
**Author:** Lambda Team  
**Date:** April 2026  
**Prerequisite:** Soft hyphen support (completed), Radiant_Font3.md

### Implementation Summary

**Part 1 — BreakKind enum:** Complete. 18-value `BreakKind` enum added to `layout.hpp`, boolean flags replaced with `last_space_kind` across 3 files (10 code sites). `classify_break()` function added. Zero regressions (4017/4017 baseline tests pass). Switch-dispatch refactor deferred per proposal.

**Part 2 — Corpus-sweep tests:** Baseline established. 122 HTML tests generated (2 corpora × 61 widths), Chrome references captured. Results:

| Corpus | Exact match | Failing widths | Mismatches | Dominant category |
|--------|-------------|----------------|------------|-------------------|
| en-gatsby-opening | 10/61 (16%) | 51 | 161 (all over-wrap) | edge-fit |
| mixed-app-text | 27/61 (44%) | 34 | 48 (45 over, 3 under) | boundary-discovery |

See `test/layout/data/pretext/status.json` for per-width failure details.

---

## Overview

Two enhancements inspired by Pretext's text layout engine:

1. **Semantic break kinds** — replace ad-hoc flags with a formal break classification enum (CSS Text 3 + UAX #14 conformant)
2. **Corpus-sweep text tests** — adapt Pretext's fine-grained width-sweep methodology for Radiant

---

## Part 1: Semantic Break Kinds

### Problem

Radiant's inline layout tracks break opportunities through a mix of implicit code branches, boolean flags, and character-class checks scattered across `layout_text.cpp`. There is no unified break classification:

| Current mechanism | Used for |
|---|---|
| `is_space(*str)` byte check | Regular spaces |
| `last_space_is_hyphen` flag | `-` vs space break |
| `last_space_is_soft_hyphen` flag | U+00AD break |
| Inline `codepoint == 0x200B` | ZWSP |
| Inline `codepoint == '\t'` | Tab |
| Inline `codepoint == '\n'` | Hard break |
| `is_cjk_ideograph()` function | CJK break-after |
| `is_line_break_op()` / `is_line_break_cl()` | UAX #14 OP/CL classes |
| `is_line_break_ns()` / `is_line_break_cj()` | UAX #14 NS/CJ classes |
| `is_line_break_ex_is_sy()` | UAX #14 EX/IS/SY classes |

This works but has downsides:
- Adding a new break type requires touching 7+ code sites
- No centralized place to reason about break behavior
- White-space mode interactions are implicit (each branch checks `collapse_spaces` independently)
- Hard to audit correctness against CSS Text 3 §5 and UAX #14

### Proposed Enum

Designed for CSS Text 3 §4–5 and UAX #14 conformance, not limited to Pretext's 8 kinds.

```cpp
// radiant/layout.hpp
typedef enum BreakKind {
    // --- Content (not a break character itself) ---
    BRK_TEXT = 0,           // ordinary word/grapheme content

    // --- Whitespace kinds (CSS Text 3 §4) ---
    BRK_SPACE,              // collapsible space (U+0020 in white-space: normal/nowrap/pre-line)
    BRK_PRESERVED_SPACE,    // non-collapsible space (U+0020 in pre/pre-wrap/break-spaces)
    BRK_TAB,                // tab character (advance to next tab stop; CSS Text 3 §4.2)
    BRK_HARD_BREAK,         // newline (\n in pre/pre-wrap/pre-line; CSS Text 3 §4.1)

    // --- Non-breaking / glue (UAX #14 GL, WJ, ZWJ) ---
    BRK_GLUE,               // visible non-breaking: NBSP U+00A0, NNBSP U+202F
    BRK_GLUE_ZW,            // zero-width non-breaking: WJ U+2060, ZWNBSP U+FEFF
    BRK_ZWJ,                // zero-width joiner U+200D (suppresses break, joins emoji sequences)

    // --- Break opportunities (CSS Text 3 §5) ---
    BRK_ZERO_WIDTH_BREAK,   // ZWSP U+200B (invisible, breakable)
    BRK_SOFT_HYPHEN,        // SHY U+00AD (invisible unless broken, then visible '-')
    BRK_HYPHEN,             // explicit hyphen U+002D, U+2010 (break after, includes width)

    // --- UAX #14 line break classes (CSS Text 3 §5.2) ---
    BRK_CJK,                // CJK ideograph (break after, unless word-break: keep-all)
    BRK_OP,                 // opening punctuation — no break after (UAX #14 LB14)
    BRK_CL,                 // closing punctuation — no break before (UAX #14 LB15/16)
    BRK_NS,                 // non-starter — no break before when after CJK (UAX #14 LB20)
    BRK_EX_IS_SY,           // EX/IS/SY — no break before (UAX #14 LB13)
    BRK_CJ,                 // conditional Japanese starter (resolved to NS or ID per line-break mode)

    // --- Ideographic space ---
    BRK_IDEOGRAPHIC_SPACE,  // U+3000 (full-width space, hangable, break opportunity)
} BreakKind;
```

### Design rationale

**Beyond Pretext's 8 kinds.** Pretext is a JS text-measurement library that doesn't need UAX #14 line-break classes (the browser handles those). Radiant implements the full CSS text layout pipeline, so break kinds must cover:

- **CSS Text 3 §4** whitespace processing: collapse, preserve, tab stops, forced breaks
- **CSS Text 3 §5** line breaking: soft wrap opportunities, word-break, overflow-wrap
- **UAX #14** line break algorithm: OP, CL, NS, CJ, EX/IS/SY classes suppress/allow breaks
- **UAX #29 + UTS #51**: ZWJ joins grapheme clusters and emoji sequences

**Hyphens get their own kind.** Radiant already tracks hyphens distinctly via `last_space_is_hyphen`. A dedicated `BRK_HYPHEN` kind eliminates the boolean flag and makes the break-at-hyphen path (include hyphen width in line) explicit in the enum. This also supports future `-` vs `‐` U+2010 distinction.

**CJK gets its own kind.** CJK ideographs have unique break-after semantics (suppressed by `word-break: keep-all`, interacting with OP/CL/NS). A `BRK_CJK` kind makes the `is_cjk_ideograph()` check part of classification rather than a separate post-check in the text path.

**Glue splits into visible and zero-width.** NBSP (U+00A0) has glyph width; WJ (U+2060) is zero-width. Both suppress breaks but differ in advance computation. Splitting `BRK_GLUE` vs `BRK_GLUE_ZW` avoids a width check inside the glue handler.

**ZWJ is separate from glue.** ZWJ (U+200D) suppresses breaks like glue but also affects emoji sequence joining (UTS #51) and grapheme clustering. Its current handling (`zwj_preceded` flag + emoji width suppression) is complex enough to warrant its own kind.

### Mapping: current code → break kind

| Character(s) | white-space: normal | white-space: pre-wrap | Break kind |
|---|---|---|---|
| U+0020 space | Collapse consecutive → 1 | Preserve all, hang at EOL | `BRK_SPACE` / `BRK_PRESERVED_SPACE` |
| U+0009 tab | Collapse to space | Render at tab-size width | `BRK_TAB` (or `BRK_SPACE` if collapsed) |
| U+000A newline | Collapse to space | Force line break | `BRK_HARD_BREAK` (or `BRK_SPACE` if collapsed) |
| U+00A0 NBSP | Non-breaking, visible | Non-breaking, visible | `BRK_GLUE` |
| U+202F NNBSP | Non-breaking, visible | Non-breaking, visible | `BRK_GLUE` |
| U+2060 WJ | Non-breaking, zero-width | Non-breaking, zero-width | `BRK_GLUE_ZW` |
| U+FEFF ZWNBSP | Non-breaking, zero-width | Non-breaking, zero-width | `BRK_GLUE_ZW` |
| U+200D ZWJ | Zero-width, joins sequences | Same | `BRK_ZWJ` |
| U+200B ZWSP | Break opportunity, zero-width | Break opportunity, zero-width | `BRK_ZERO_WIDTH_BREAK` |
| U+00AD SHY | Break opportunity, zero-width, visible `-` on break | Same | `BRK_SOFT_HYPHEN` |
| U+002D `-` | Break after, includes width | Same | `BRK_HYPHEN` |
| U+2010 `‐` | Break after, includes width | Same | `BRK_HYPHEN` |
| CJK ideographs | Break after (unless keep-all) | Same | `BRK_CJK` |
| U+3000 ideographic space | Break opportunity, full-width, hangable | Same | `BRK_IDEOGRAPHIC_SPACE` |
| `(` `[` `{` 「 etc. | No break after | Same | `BRK_OP` |
| `)` `]` `}` 」 、。 etc. | No break before | Same | `BRK_CL` |
| Thai/Khmer marks, CJK iteration | No break before (after CJK) | Same | `BRK_NS` |
| `!` `?` `,` `.` `:` `;` `/` | No break before (LB13) | Same | `BRK_EX_IS_SY` |
| Small kana ぁ ァ, prolonged ー | NS or ID per line-break mode | Same | `BRK_CJ` |
| All other characters | Content | Same | `BRK_TEXT` |

### Where the enum is used

#### A. Linebox break tracking

Replace the boolean flags in `Linebox`:

```cpp
// BEFORE (layout.hpp)
unsigned char* last_space;
float last_space_pos;
bool last_space_is_hyphen;
bool last_space_is_soft_hyphen;

// AFTER
unsigned char* last_space;
float last_space_pos;
BreakKind last_space_kind;    // replaces both booleans + implicit "it's a regular space"
```

`reset_space()` sets `last_space_kind = BRK_TEXT` (no break recorded).

#### B. Character classification

Add a classifier function called early in the main layout loop. This consolidates the scattered character checks into one place:

```cpp
// radiant/layout_text.cpp
static BreakKind classify_break(uint32_t cp, bool collapse_spaces, bool collapse_newlines) {
    // Whitespace (CSS Text 3 §4)
    if (cp == 0x0020) return collapse_spaces ? BRK_SPACE : BRK_PRESERVED_SPACE;
    if (cp == '\t')   return collapse_spaces ? BRK_SPACE : BRK_TAB;
    if (cp == '\n' || cp == '\r') return collapse_newlines ? BRK_SPACE : BRK_HARD_BREAK;

    // Non-breaking glue
    if (cp == 0x00A0 || cp == 0x202F) return BRK_GLUE;         // visible NBSP/NNBSP
    if (cp == 0x2060 || cp == 0xFEFF) return BRK_GLUE_ZW;      // zero-width WJ/ZWNBSP
    if (cp == 0x200D) return BRK_ZWJ;                          // ZW joiner

    // Break opportunities
    if (cp == 0x200B) return BRK_ZERO_WIDTH_BREAK;             // ZWSP
    if (cp == 0x00AD) return BRK_SOFT_HYPHEN;                  // SHY
    if (cp == 0x002D || cp == 0x2010) return BRK_HYPHEN;       // hyphen / hyphen-minus

    // Ideographic space
    if (cp == 0x3000) return BRK_IDEOGRAPHIC_SPACE;

    // CJK ideographs (break-after unless keep-all)
    if (is_cjk_ideograph(cp)) return BRK_CJK;

    // UAX #14 line break classes
    if (is_line_break_op(cp)) return BRK_OP;
    if (is_line_break_cl(cp)) return BRK_CL;
    if (is_line_break_cj(cp)) return BRK_CJ;
    if (is_line_break_ns(cp)) return BRK_NS;
    if (is_line_break_ex_is_sy(cp)) return BRK_EX_IS_SY;

    return BRK_TEXT;
}
```

**Note:** The existing `is_line_break_*()` functions are called during classification instead of in the main loop. This means each codepoint is classified once, and the main loop dispatches on the enum value.

#### C. Main layout loop refactor

The main layout loop currently has deeply nested if/else branches for each character type. With `BreakKind`, the top-level dispatch becomes a switch:

```cpp
BreakKind brk = classify_break(codepoint, collapse_spaces, collapse_newlines);
switch (brk) {
    case BRK_SPACE:
        // existing collapsible space logic (LB7, trailing trim)
        break;
    case BRK_PRESERVED_SPACE:
        // pre-wrap space logic (hanging, break-spaces)
        break;
    case BRK_TAB:
        // tab-size × space_width advance
        break;
    case BRK_HARD_BREAK:
        // forced line break
        break;
    case BRK_GLUE:
        // advance by glyph width, NO break opportunity
        break;
    case BRK_GLUE_ZW:
        // zero advance, NO break opportunity
        break;
    case BRK_ZWJ:
        // zero advance, suppress break, set zwj_preceded flag for emoji joining
        break;
    case BRK_ZERO_WIDTH_BREAK:
        // record break opportunity, zero advance
        break;
    case BRK_SOFT_HYPHEN:
        // record break opportunity, zero advance, flag for visible '-' on break
        break;
    case BRK_HYPHEN:
        // advance by glyph width, record break-after opportunity (include width in line)
        break;
    case BRK_CJK:
        // advance by glyph width, record break-after (unless word-break: keep-all)
        break;
    case BRK_IDEOGRAPHIC_SPACE:
        // advance by glyph width (hangable), record break opportunity
        break;
    case BRK_OP:
        // advance by glyph width, suppress break-after (LB14)
        break;
    case BRK_CL:
        // advance by glyph width, suppress break-before (LB15/16)
        // special: allow break before when preceded by CJK (CSS Text 3 §5.2)
        break;
    case BRK_NS:
        // advance by glyph width, suppress break-before when after CJK (LB20)
        break;
    case BRK_EX_IS_SY:
        // advance by glyph width, suppress break-before (LB13)
        break;
    case BRK_CJ:
        // resolve: if line-break: loose → treat as BRK_CJK; else treat as BRK_NS
        break;
    case BRK_TEXT:
        // existing glyph layout (word-break: break-all, overflow-wrap: anywhere)
        break;
}
```

#### D. Break execution paths

The 7 break-execution code paths remain structurally the same. The difference is that `last_space_kind` replaces the boolean flag checks:

```cpp
// BEFORE
if (last_space_is_soft_hyphen) {
    text_len -= 2;  // exclude SHY bytes
    ...
}

// AFTER
if (linebox.last_space_kind == BRK_SOFT_HYPHEN) {
    text_len -= 2;
    ...
}
```

### Implementation plan

| Step | Scope | Risk |
|---|---|---|
| 1. Add `BreakKind` enum to `layout.hpp` | Header only | None |
| 2. Replace `last_space_is_hyphen` + `last_space_is_soft_hyphen` with `last_space_kind` | `layout.hpp`, `layout_text.cpp` | Low — mechanical flag replacement |
| 3. Add `classify_break()` function | `layout_text.cpp` | None |
| 4. Refactor main loop to use `classify_break()` switch dispatch | `layout_text.cpp` | Medium — largest change, must preserve all 7 break paths |
| 5. Run baseline tests | Test | Gating — must be 100% pass |

**Estimated scope:** ~300 lines changed in `layout_text.cpp`, ~30 lines in `layout.hpp`, 0 behavioral changes.

### Notes

1. **Phase the refactor.** Step 2 (replace booleans with enum) can ship independently and provides immediate benefit. Steps 3–4 (switch dispatch) are a larger refactor that can follow in a separate pass. This de-risks the change.

2. **BRK_OP/CL/NS/EX_IS_SY in the main loop.** These UAX #14 classes currently use lookahead/lookbehind (`peek_codepoint()`) rather than the break-at-last-space pattern. Moving them into the switch makes the dispatch uniform but requires careful handling of the next-character lookahead. The switch case for `BRK_CL` needs to peek backward (was previous character CJK?), and `BRK_EX_IS_SY` needs cross-node lookahead via `view_peek_next_text_codepoint()`. These interactions should be preserved exactly.

3. **`BRK_CJ` resolution.** CJ characters are resolved at classification time based on `line_break` CSS property: `line-break: loose` → `BRK_CJK`; `line-break: normal/strict` → `BRK_NS`. This means `classify_break()` needs access to the `line_break` property value, or CJ resolution happens as a post-classification step.

4. **Enum stability.** The enum values should be considered internal to the layout engine. They are never serialized to JSON or exposed in the view tree. This allows future additions (e.g., `BRK_EMOJI_BASE` if emoji sequences need special handling) without compatibility concerns.

---

## Part 2: Corpus-Sweep Text Tests

### Problem

Radiant's layout tests compare full page layout trees against Chrome references. This is effective for block/flex/grid layout but **coarse for text line-breaking** — a single full-page comparison can't distinguish a text wrapping bug from a margin rendering difference.

Pretext's approach is targeted: sweep one text block across 61 widths (300–900px, step 10) and compare line count / total height against the browser. This isolates text layout accuracy from everything else.

### What Pretext measures

| Level | Metric | Data |
|---|---|---|
| **Accuracy sweep** | `actual height == predicted height` | 7,680 test points: 4 fonts × 8 sizes × 8 widths × 30 short texts |
| **Corpus sweep** | `actual line count == predicted line count` at each width | 13 corpora × 61 widths = ~800 points per corpus |
| **Per-line diagnostics** | Exact line break positions on mismatch | Automatic diff on failure |

### Adaptation strategy for Radiant

Radiant is a C++ layout engine, not a JS library. We can't call `layout()` directly from a test script. Instead:

#### Test flow

```
[corpus .txt files] → [generate .html test files] → [lambda.exe layout --json] → [compare against Chrome reference]
```

1. **Generator script** (`test/layout/generate_pretext_corpus.js`): For each corpus text file and each width (300–900, step 10), generate an HTML file:
   ```html
   <!DOCTYPE html>
   <html><head>
   <style>
     @font-face { font-family: 'Liberation Sans'; src: url('../../font/LiberationSans-Regular.ttf'); }
     * { margin: 0; padding: 0; }
     body { font-family: 'Liberation Sans'; font-size: 16px; line-height: 1.5; }
     .text-block { width: {WIDTH}px; }
   </style>
   </head><body>
   <div class="text-block">{CORPUS_TEXT}</div>
   </body></html>
   ```

2. **Chrome references**: Capture via existing `extract_browser_references.js` infrastructure. Each generated HTML gets a reference JSON.

3. **Comparison**: Run through existing `test_radiant_layout.js` with tighter text-specific tolerances.

#### Key difference from Pretext's approach

| Aspect | Pretext | Radiant adaptation |
|---|---|---|
| Metric | Total height in px | Full layout tree (existing infra) + text rect count per text node |
| Oracle | Live browser API call | Pre-captured Chrome reference JSON |
| Execution | JS `layout()` call | `lambda.exe layout file.html --json` |
| Font | System font (Helvetica, etc.) | Bundled font (`Liberation Sans`) for determinism |
| Line-by-line diff | Custom extractor | Compare text rect sequence from view tree JSON |

#### Why pre-captured references (not live Chrome)

- **Determinism**: System fonts vary across machines; bundled Liberation fonts don't
- **Speed**: No Puppeteer launch per test run; references captured once
- **Existing infra**: Fits cleanly into the current `reference/` workflow
- **CI-friendly**: No browser dependency in test execution

#### Proposed file structure

```
test/layout/data/pretext/
├── generate_corpus_tests.js          # generator script
├── pretext_en_gatsby_w300.html       # generated: English, 300px
├── pretext_en_gatsby_w310.html       # generated: English, 310px
├── ...
├── pretext_en_gatsby_w900.html       # generated: English, 900px
├── pretext_mixed_app_w300.html       # generated: mixed scripts, 300px
├── ...
├── pretext_mixed_app_w900.html       # generated: mixed scripts, 900px
└── README.md                         # documents methodology

test/layout/reference/
├── pretext_en_gatsby_w300.json       # Chrome reference
├── ...
```

#### Corpus selection

Reuse Pretext's corpus files from `ref/pretext/corpora/`. Start with Latin-script corpora; skip RTL and CJK for now (add later as those layout paths mature).

| Corpus ID | Language | Script | Priority | Phase |
|---|---|---|---|---|
| `en-gatsby-opening` | English | Latin | High — baseline Latin | 1 |
| `mixed-app-text` | Multilingual | Mixed (Latin-dominant) | High — mixed scripts | 1 |
| `th-nithan-vetal-story-1` | Thai | Thai | Medium — no word spaces | 2 |
| `th-nithan-vetal-story-7` | Thai | Thai | Low | 2 |
| `hi-eidgah` | Hindi | Devanagari | Low — complex script | 3 |
| `my-cunning-heron-teacher` | Myanmar | Myanmar | Low — complex script | 3 |
| `my-bad-deeds-return...` | Myanmar | Myanmar | Low | 3 |

*Deferred (RTL):* `ar-al-bukhala`, `ar-risalat-al-ghufran-part-1`, `he-masaot-binyamin-metudela`, `ur-chughd`  
*Deferred (CJK):* `ja-kumo-no-ito`, `ja-rashomon`, `zh-guxiang`, `zh-zhufu`, `ko-sonagi`, `ko-unsu-joh-eun-nal`

**Phase 1 (2 corpora, ~122 HTML files):** `en-gatsby-opening`, `mixed-app-text`  
**Phase 2 (+2 corpora, ~122 more):** Thai corpora  
**Phase 3 (+3 corpora, ~183 more):** Hindi, Myanmar

#### Width sweep parameters

- **Range**: 300–900px, step 10 (61 widths per corpus) — matches Pretext
- **Font**: Liberation Sans 16px / line-height 1.5 (consistent, bundled)
- **Viewport**: 1200×800 (existing default)

#### Comparison metrics

Use existing full layout tree comparison (same as baseline tests). This provides 100% web standards conformance checking — element bounds, text positions, and content all compared against Chrome.

Key metrics per test file:
- **Element match %**: All elements (html, body, div) must match positions/dimensions
- **Text match %**: All text rects must match positions/dimensions
- **Existing tolerance**: 5px default with proportional scaling for large values

#### Failure taxonomy

Adopt Pretext's taxonomy for classifying mismatches:

| Category | Description | Radiant action |
|---|---|---|
| `corpus-dirty` | Source text has scraping artifacts | Clean corpus or reject |
| `normalization` | White-space collapse difference | Fix preprocessing |
| `boundary-discovery` | Wrong break opportunities | Fix UAX #14 / kinsoku tables |
| `glue-policy` | Wrong attachment (punct stays with word) | Fix glue rules |
| `edge-fit` | Off by <1px at line edge | Accept or tune tolerance |
| `shaping-context` | Glyph shaping changes break | Accept (engine limitation) |
| `font-mismatch` | Different font resolution | Fix font stack |
| `diagnostic-sensitivity` | Test probe issue | Fix test, not engine |

Track in a machine-readable `test/layout/data/pretext/status.json`:

```json
{
  "en-gatsby-opening": {
    "widths_tested": 61,
    "exact_match": 58,
    "mismatches": [
      { "width": 430, "radiant_lines": 12, "chrome_lines": 13, "category": "edge-fit" },
      { "width": 710, "radiant_lines": 8, "chrome_lines": 9, "category": "shaping-context" }
    ]
  }
}
```

### Implementation plan

| Step | Scope | Output |
|---|---|---|
| 1. Write generator script | `test/layout/data/pretext/generate_corpus_tests.js` | Generates HTML from corpus .txt |
| 2. Generate Phase 1 HTML files | 2 corpora × 61 widths = 122 files | `test/layout/data/pretext/*.html` |
| 3. Capture Chrome references | `make capture-layout suite=pretext` | `test/layout/reference/pretext_*.json` |
| 4. Run against Radiant | `make layout suite=pretext` | Initial pass/fail scores |
| 5. Classify failures | Manual + status.json | Failure taxonomy |
| 6. Fix break model issues | `layout_text.cpp` | Improved text accuracy |
| 7. Phase 2–3 corpora | Thai, then Hindi/Myanmar | Progressive multilingual coverage |

### Notes

1. **Keep outside baseline.** Corpus sweep tests live in the `pretext` suite (`make layout suite=pretext`), separate from `make test-radiant-baseline`. Promote to baseline once accuracy stabilizes above 95%.

2. **HTML file count.** Phase 1 is 122 files + 122 reference JSONs. Full rollout (7 corpora) is ~427 files. This fits the existing batch-mode infrastructure (batch size 100, 5 concurrent). **Keep generated files in git** for deterministic CI, but add a `make regenerate-pretext-corpus` target for updates.

3. **White-space modes.** Pretext tests `normal` and `pre-wrap` modes. The corpus sweep should also cover `pre-wrap` for at least one corpus (e.g., `en-gatsby-opening`) to validate preserved-space handling. Generate a parallel set of `pretext_en_gatsby_prewrap_w*.html` files.

4. **Break-kind probe texts.** Beyond corpora, add targeted probe texts (not full literary passages) for specific break kinds:
   - Soft hyphen: `"Butter\u00ADfly"` at widths 40–100px (force/avoid break)
   - Tab: `"Col1\tCol2\tCol3"` in `pre-wrap` at widths 100–400px
   - NBSP: `"100\u00A0km"` — must never break
   - ZWSP: `"long\u200Bword"` — break without visible space
   These are ~20 extra HTML files but test break kinds that corpora may not exercise.

5. **Pretext's accuracy sweep (7,680 points).** The accuracy sweep (4 fonts × 8 sizes × 8 widths × 30 short texts) tests single-paragraph height prediction. The **30 short texts** are good micro-tests. Extract them from Pretext's test data and add as a separate small suite (`test/layout/data/pretext-micro/`) with fewer width steps (e.g., 5 widths each = 150 files).

---

## Summary

| Enhancement | Scope | Files changed | Test impact |
|---|---|---|---|
| Semantic break kinds | `layout.hpp`, `layout_text.cpp` | ~300 lines | Zero behavioral change; refactor only |
| Corpus sweep tests | New test infrastructure | Generator script + 122–427 HTML/JSON files | `make layout suite=pretext`, outside baseline |

The two enhancements are independent and can be implemented in any order. The break kind enum is a prerequisite for *reasoning* about break correctness but not for *running* the corpus tests.
