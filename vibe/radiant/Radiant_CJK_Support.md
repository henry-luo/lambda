# Radiant CJK Horizontal Layout Support

**Status**: Implemented (Phase 1)  
**Scope**: Full CJK horizontal layout and rendering  
**Out of scope (deferred)**: Vertical text (`writing-mode: vertical-rl/lr`), ruby annotations (`<ruby>`, `<rt>`), `text-combine-upright`

---

## 1. Current Status

### 1.1 What Works

Radiant already has substantial CJK infrastructure in place:

| Feature | Implementation | Location |
|---|---|---|
| **UAX #14 line breaking** | ID-class break-after between ideographs | `layout_text.cpp` `has_id_line_break_class()` |
| **CJK punctuation rules (kinsoku)** | No break before CL/NS, no break after OP | `is_line_break_op()`, `is_line_break_cl()`, `is_line_break_ns()` |
| **Conditional Japanese starters** | CJ class → NS (strict/normal), ID (loose) | `is_line_break_cj()` |
| **`word-break` property** | `break-all`, `keep-all`, `break-word` | `get_word_break()` in `layout_text.cpp` |
| **`line-break` property** | `auto`, `loose`, `normal`, `strict`, `anywhere` | `get_line_break()` in `layout_text.cpp` |
| **`overflow-wrap` property** | `normal`, `break-word`, `anywhere` | `get_overflow_wrap()` in `layout_text.cpp` |
| **Ideographic space** (U+3000) | Hangable break opportunity per CSS Text 3 §4.1.3 | `layout_text.cpp` ~line 2902 |
| **CJK character detection** | `is_cjk_character()` for Han/Kana/Hangul ranges | `layout_text.cpp` ~line 416 |
| **CJK line-height blending** | System CJK font metrics blended for normal line-height | `get_cjk_system_line_height()` in `font_platform.c` |
| **CJK font fallback (macOS)** | CoreText `CTFontCreateForString()` resolves system CJK fonts | `font_platform.c` `font_platform_find_codepoint_font()` |
| **East Asian width helpers** | `is_east_asian_fw()`, `is_hangul()` defined | `layout_text.cpp` ~line 890 |

### 1.2 Gaps

| # | Gap | Impact | Effort | Status |
|---|---|---|---|---|
| **G1** | Segment break transformation Rule 2 not implemented | Spurious spaces between CJK ideographs when source HTML has newlines | Small | ✅ Fixed |
| **G2** | No bundled CJK test font | Cannot run deterministic CJK layout tests; OS font fallback varies across machines | Small | ✅ Fixed |
| **G3** | No CJK pretext corpus tests | CJK line breaking never exercised in regression suite | Small | ✅ Fixed |
| **G4** | CJK line-height blending is macOS-only | `get_cjk_system_line_height()` returns 0 on Linux/Windows — CJK lines may have tight line spacing with non-CJK primary fonts | Small | Deferred |
| **G5** | No `text-spacing` / `autospace` (CJK–Latin spacing) | Missing inter-script spacing between CJK and Latin characters (CSS Text 4 draft) | Medium | Deferred |
| **G6** | `lang` attribute not used for layout decisions | CJ class treated as Japanese-NS for all languages; should be ID for Chinese/Korean in `line-break: normal` | Small | ✅ Fixed |
| **G7** | Default-ignorable characters interfere with segment break Rule 2 | Variation selectors (U+FE00), IVS (U+E0100), SHY, LRM between EA Wide chars prevent Rule 2 from applying | Small | Open |

---

## 2. Gap Details

### G1: Segment Break Transformation Rule 2 (CSS Text 3 §4.1.2) — ✅ FIXED

**Implementation** (`layout_text.cpp` ~line 2838): Wired existing `is_east_asian_fw()` and `is_hangul()` helpers into the segment break logic after Rule 1 (ZWSP adjacency):
```cpp
// Rule 2: East Asian Wide ↔ East Asian Wide (not Hangul)
if (!remove_break && last_processed_cp && next_cp
    && is_east_asian_fw(last_processed_cp) && !is_hangul(last_processed_cp)
    && is_east_asian_fw(next_cp) && !is_hangul(next_cp)) {
    remove_break = true;
}
```

**Result**: Chinese/Japanese text with source newlines no longer shows spurious spaces. Verified with CJK smoke test `cjk_001_segment_break.html`.

**Browser compatibility note**: Chrome 147 does not implement Rule 2 — it still converts all segment breaks to spaces. Radiant follows the CSS Text 3 spec (consistent with Firefox/Mozilla's WPT test expectations). 5 segment-break-transformation tests in the baseline are skipped due to this divergence (see `test/layout/skip_list.txt`).

### G2: Bundled CJK Test Font — ✅ FIXED

**Solution**: Created a broad subset of **Noto Sans SC** (SubsetOTF variant, SIL OFL 1.1) covering:
- CJK Unified Ideographs Basic (U+4E00–9FFF): 20,976 glyphs
- Hiragana + Katakana (U+3040–30FF): 189 glyphs
- CJK punctuation, fullwidth forms, compatibility ideographs
- ASCII (U+0000–007F): 95 glyphs
- **Total**: 29,497 glyphs in 7.7 MB OTF file

**File**: `test/layout/data/font/NotoSansSC-Subset.otf`

**Decision**: Broader subset (full CJK Basic block) rather than corpus-only characters. Single SC variant sufficient — layout metrics are identical across regional variants (SC/TC/JP/KR). Note: Noto Sans SC contains 0 Hangul syllables; Korean tests fall back to system fonts.

### G3: CJK Pretext Corpus Tests — ✅ FIXED

**Implementation**: Added Phase 4 CJK corpora to `test/layout/data/pretext/generate_corpus_tests.js`:
- 6 corpora: `zh-guxiang`, `zh-zhufu`, `ja-kumo-no-ito`, `ja-rashomon`, `ko-sonagi`, `ko-unsu-joh-eun-nal`
- Each generates 61 widths (300–900px, step 10) = **366 HTML test files**
- CJK files use `@font-face` with `NotoSansSC-Subset.otf`
- `<html lang="zh|ja|ko">` attribute set per corpus language
- Currently 78 pretext baseline passed, 366 CJK tests skipped (no reference captures yet)

Additionally, 5 hand-crafted CJK smoke tests in `test/layout/data/cjk/`:
- `cjk_001_segment_break.html` — Segment break Rule 2 (6 test cases)
- `cjk_002_kinsoku.html` — Kinsoku line breaking (6 test cases)
- `cjk_003_korean.html` — Korean Hangul with `word-break` (3 test cases)
- `cjk_004_japanese_cj.html` — CJ class with `line-break` modes (4 test cases)
- `cjk_005_mixed_scripts.html` — Mixed CJK + Latin text (5 test cases)

### G4: CJK Line-Height Blending on Non-macOS

`get_cjk_system_line_height()` in `font_platform.c` uses CoreText to query PingFang SC metrics on macOS. The non-macOS stub returns 0, meaning CJK-heavy lines may render with tighter line spacing when the primary font has small ascent/descent.

**Proposal for bundled font scenario**: When a CJK bundled font (Noto Sans SC) is used as the primary `font-family`, its own metrics provide correct line-height — the system blending path is not needed. This gap only matters when CJK text appears via fallback under a non-CJK primary font (e.g., `font-family: Arial` with CJK content). For pretext testing with CJK-first font-family, this is not a blocker.

**Long-term**: On Linux, read CJK font metrics from the bundled Noto Sans SC TTF via FreeType when `has_cjk_text` is set. Low priority since pretext tests use explicit `font-family`.

### G5: CJK–Latin Inter-Script Spacing (`text-spacing` / `autospace`)

CSS Text 4 `text-autospace` / `text-spacing` property adds small spacing between CJK and non-CJK characters (typically ~1/8 em). This is a typographic nicety, not a correctness issue.

**Status**: Not spec-finalized, partial browser support. Defer to future.

---

## 3. Implementation Plan

| Step | Task                                            | Files                                                             | Status  |
| ---- | ----------------------------------------------- | ----------------------------------------------------------------- | ------- |
| 1    | **Implement segment break Rule 2**              | `radiant/layout_text.cpp`                                         | ✅ Done  |
| 2    | **Add `lang` attribute support + CJ class fix** | `radiant/layout_text.cpp`                                         | ✅ Done  |
| 3    | **Create Noto Sans SC subset font**             | `test/layout/data/font/NotoSansSC-Subset.otf`                     | ✅ Done  |
| 4    | **Create CJK smoke tests**                      | `test/layout/data/cjk/cjk_001–005.html`                           | ✅ Done  |
| 5    | **Add Phase 4 CJK corpora to generator**        | `test/layout/data/pretext/generate_corpus_tests.js`               | ✅ Done  |
| 6    | **Generate CJK test files**                     | 366 `pretext_zh_*.html`, `pretext_ja_*.html`, `pretext_ko_*.html` | ✅ Done  |
| 7    | **Capture pretext CJK browser references**      | `test/layout/reference/pretext/`                                  | Pending |
| 8    | **Add default-ignorable char handling (G7)**    | `radiant/layout_text.cpp`                                         | Open    |

---

## 4. Decisions (from review)

1. **Font subset**: Broader coverage (full CJK Basic block + Kana + punctuation), not just corpus characters. Results in ~7.7 MB, 29,497 glyphs.
2. **Single font**: Noto Sans SC sufficient for all three languages. Layout metrics are identical across regional variants.
3. **Test width range**: Same as existing pretext (300–900px, step 10).
4. **`lang` attribute**: Implemented. `resolve_lang()` walks up the DOM tree to find `lang` attribute. CJ class resolution is now language-aware: NS for Japanese, ID for Chinese/Korean in `line-break: normal`.
5. **Test data**: Both CJK smoke tests (5 files, hand-crafted) and pretext corpus expansion (366 generated files).

---

## 5. Remaining Gaps

### G7: Default-Ignorable Characters in Segment Break Rule 2

CSS Text 3 specifies that default-ignorable characters (variation selectors U+FE00–FE0F, ideographic variation selectors U+E0100–E01EF, soft hyphen U+00AD, LRM/RLM U+200E/200F) should not interfere with segment break transformation. Current implementation checks `last_processed_cp` which may be set to a variation selector, preventing Rule 2 from matching.

WPT test `segment-break-transformation-ignorable-1` validates this behavior. Currently skipped due to Chrome divergence + this gap.

**Fix**: When checking Rule 2, skip backward over default-ignorable codepoints to find the last meaningful character.

---

## 6. Deferred / Future Work

| Feature | Notes |
|---|---|
| Vertical text (`writing-mode: vertical-rl/lr`) | Enum/struct fields defined in `view.hpp`, no layout implementation. Large effort. |
| Ruby annotations (`<ruby>`, `<rt>`, `<rp>`) | HTML tag parsed, `display: ruby` recognized. No layout code. Medium effort. |
| `text-combine-upright` | Only relevant for vertical text. Deferred with vertical layout. |
| `text-spacing` / `text-autospace` | CJK–Latin inter-script spacing. CSS Text 4 draft. Low priority. |
| Complex text shaping (HarfBuzz) | Not needed for CJK/Hangul. Would be needed for Indic, Arabic, complex ligatures. |
