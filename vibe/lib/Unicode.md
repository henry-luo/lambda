# Unicode in Lambda

Consolidated design record for Unicode text handling, replacing `Unicode1.md` (ICU tiers), `Unicode2.md` (utf8proc migration) and `Unicode3.md` (`lib/utf.c` centralization). Design and decisions only; implementation detail lives in the code (`lib/utf.h`, `lib/utf.c`, `lambda/core/utf_string.*`). Status as of 2026-10-23.

Authority: semantic rulings are in `doc/Lambda_Formal_Semantics.md` and win over this document where they differ (S2.5.8 text as a sequence of code points, S6.2.2v3 total order, S8.2.2v6 key identity, S1.11 hosted-JS surrogates). The user-facing string model is `vibe/Lambda_Type_String.md` (§1.4 "Unicode levels", §8 deferred grapheme functions).

## 1. Design position

1. **UTF-8 is the one storage encoding.** Strings, symbols and names are UTF-8. The user-visible unit is the **code point** (S2.5.8): `len`, indexing, slicing, iteration and every reported position count code points, and byte offsets are internal only.
2. **Lone surrogates exist only for hosted JS.** Lambda text is well-formed UTF-8. LambdaJS needs to carry lone UTF-16 units, which are stored as lossless WTF-8 (S1.11, S8.2.2v6). A valid surrogate pair is always stored as its scalar's 4-byte UTF-8 form, and lone units stay distinct. Canonical encoders therefore reject surrogates, and WTF-8 is a separate, explicitly named entry point.
3. **Nothing normalizes implicitly.** `==`, key identity and the total order work on the bytes as written: no NFC/NFD folding, no case folding, no locale, no replacement characters (S6.2.2v3, S8.2.2v6). `"café"` in NFC and in NFD are different strings. Normalization and folding are explicit tools: `normalize(s, form)` (NFC, NFD, NFKC, NFKD), `ignore_case` options that use Unicode **simple** case folding, `upper`/`lower`.
4. **Order is bytewise UTF-8** (S6.2.2v3), which equals code point order. `"Hello" < "hello"` and `"n" < "ñ"` hold because of byte values, not collation. There is no locale-aware ordering in the core language.
5. **Graphemes are not a unit.** A family emoji or a decomposed accent is several code points. Grapheme and word segmentation functions are deferred (Type_String §8). Layout needs cluster/break behaviour on its own and approximates it (see §4).
6. **Dependency tiering.** The core codec and the small classification predicates are dependency-free C (`lib/utf.c`). Anything needing Unicode property tables (normalization, general category, case mapping, display width) goes through utf8proc. No ICU.

## 2. Decision history

### 2.1 ICU tiers (Aug 2025) — superseded

The first design offered four compile-time levels (ASCII-only, ~200 KB minimal, ~2–4 MB stripped ICU, 8–12 MB full ICU) selected by `UNICODE_LEVEL`, with an ASCII fast path, static linking, and ICU collation behind the string relational operators (`<`, `>`, `<=`, `>=`).

Two parts of that were abandoned:

- **ICU itself.** The size cost was the dominant objection to a general ICU (25–30 MB stock, 2–4 MB after aggressive stripping of layout engines, converters, transliteration, regex, break iteration and most locales) and a custom ICU build was a standing maintenance burden. It was replaced by utf8proc, about 200 KB, which gives normalization, case folding and character properties, which is all the language needed.
- **Collation as the string order.** The semantics later ruled the order bytewise with no locale collation (S6.2.2v3), so the "Unicode collation" goal and the locale-ordered ranking of tier 3 are gone, and with them the motive for tiers. The tier switch (`UNICODE_LEVEL`, `build-ascii`, `LAMBDA_UNICODE_LEVEL`) no longer exists in the build.

What survived: the principle that Unicode cost should be paid only when text is non-ASCII (the bytewise paths are ASCII-safe by construction), and that dependencies are statically linked so the binary stays self-contained.

### 2.2 utf8proc migration (Aug 2025)

ICU was removed and utf8proc adopted (`build_lambda_config.json` library `utf8proc`, static). The `LAMBDA_UTF8PROC_SUPPORT` conditional is gone; utf8proc is unconditional. Capability gained: NFC/NFD/NFKC/NFKD, casefold, category and case-mapping queries, UTF-8 iterate.

### 2.3 Codec centralization (`lib/utf.c`)

Problem: UTF-8 encode/decode, surrogate-pair handling and a few classification predicates were copy-pasted across 20+ files, with silent differences (surrogate acceptance, null-termination, range-check spelling), so a fix needed patching every copy and some copies could leak WTF-8 into well-formed strings.

Decision: one dependency-free C module, `lib/utf.c` + `lib/utf.h`, owning:

- UTF-8: `utf8_encode` (rejects surrogates and > 0x10FFFF, no NUL), `utf8_encode_z` (NUL-terminated), `utf8_encode_wtf8` (admits lone surrogates, for JS), `utf8_decode` (strict: rejects overlong, surrogate and truncated), `utf8_decode_replacement`, `utf8_char_len`, `utf8_count`, `utf8_valid`, char/byte index conversion.
- UTF-16 bridge: surrogate pair decode and encode, UTF-8↔UTF-16 length and offset conversion (the DOM and JS index in UTF-16 units over UTF-8 storage), a UTF-16 iterator and comparison, canonical key and slice helpers for WTF-8 names.
- Classification that is small, table-free or a hand-written range set and was duplicated: surrogate and control tests, CJK, Hangul, cursive and clustered scripts, contextual-shaping need, BiDi strong class, emoji-for-ZWJ, emoji presentation default, ZWJ composition base.

Explicitly **not** moved (spec-specific or large, and coupled to their consumer): UAX#14 line-break tables and CSS full case-mapping tables (Radiant layout), the HTML5 Hoehrmann UTF-8 DFA (spec-mandated behaviour), CSS ident-char rules, REPL display width (`lib/cmdedit_utf8.c`), CSS counter-style encoders, and normalization/casefold (stay behind utf8proc in `lambda/core/utf_string.*`).

`lib/str.c` keeps `str_utf8_*` as thin wrappers over the `utf8_*` functions for source compatibility (Option A of the original proposal; the alternative of `utf.c` calling `str.c` was rejected so that there is one implementation).

## 3. Current status

Landed:

- `lib/utf.c` / `lib/utf.h` exist with the API above, and `str_utf8_*` delegate to it.
- `radiant/intrinsic_sizing.cpp`'s ZWJ predicates now delegate to `utf_is_emoji_for_zwj` / `utf_is_zwj_composition_base` (thin static wrappers remain).
- BiDi strong-class detection, hangul/CJK tests and contextual-shaping checks are consumed from `lib/utf` by Radiant layout, editing, DOM range code and image artwork.
- CoreText font code uses `utf16_encode` (no inline surrogate math left in `lib/font/font_platform.c` / `font_rasterize_ct.c`).
- Old `lambda/utf_string.cpp` `is_valid_utf8()` removed; the module is now `lambda/core/utf_string.*` and holds only utf8proc-backed normalization, casefold and simple-fold helpers.

## 4. Open and outstanding issues

Numbered `UNI-n` for reference. Severity is a judgment: **B** correctness risk, **D** duplication/maintenance, **F** feature gap, **H** hygiene.

### Codec centralization incomplete

- **UNI-1 (B/D) Encoders that bypass `utf8_encode`.** Hand-rolled encoders remain, and any that do not check for surrogates can write WTF-8 into text that must be well-formed. Known sites: `lambda/input/input-utils.cpp` (`codepoint_to_utf8`, kept as a wrapper), `lambda/js/js_runtime.cpp` (`js_cp_to_utf8`), `lambda/js/js_globals.cpp`, `lambda/js/js_regex_wrapper.cpp`, `lambda/module/node_core/node_querystring.cpp`, `node_buffer.cpp`, `lambda/module/node_crypto/node_crypto_module.cpp`, `lambda/input/input-pdf-postprocess.cpp`, `radiant/cmd_layout.cpp`, `radiant/webview_layer_linux.cpp`, `test/test_html_roundtrip_gtest.cpp`. Each needs a decision: strict (`utf8_encode`) or JS-lone-surrogate (`utf8_encode_wtf8`). The 3rd-copy rule in AGENTS.md applies.
- **UNI-2 (D) Inline surrogate-pair decode** remains in `lambda/js/js_runtime.cpp`, `js_bt_regex.cpp`, `mvp-lmd/mvp_lmd_library.cpp`, `node_querystring.cpp`, `input-pdf-postprocess.cpp`, `lib/image_psd.cpp`, plus `decode_surrogate_pair` in `input-utils` (wrapper kept) and a DWrite encode in `lib/font/font_backend_dwrite.cpp`. Replace with `utf16_decode_pair` / `utf16_encode`.
- **UNI-3 (D) Duplicate UTF-8 decoders**: `css_parse_unicode_char` (`css_tokenizer.cpp`), `bt_utf8_decode` (`js_bt_regex.cpp`), `html_entities.cpp`'s first-codepoint helper, and `js_decode_utf8`-style local decoders. The HTML5 Hoehrmann DFA is intentionally excluded (UNI-12). Policies differ (strict vs replacement vs structural), so first decide which of `utf8_decode`, `utf8_decode_replacement` or the structural UTF-16 offset policy each caller wants.
- **UNI-4 (D) Two names for one thing.** `str_utf8_*` and `utf8_*` coexist, with `str_utf8_encode` taking a capacity argument. Decide whether to migrate callers and retire the `str_` prefix, or document `str_utf8_*` as the permanent capacity-checked front.
- **UNI-5 (H) Classification still duplicated or stranded in Radiant.** `is_east_asian_fw`, `is_other_space_separator`, `get_unicode_space_width_em`, `is_typographic_letter_unit`, `is_cjk_character`, the UAX#14 `has_id_line_break_class` table (now in `radiant/layout.hpp`), `FullCaseMapping` tables, `is_first_letter_punctuation` (takes a raw `utf8proc_int32_t`). The original proposal listed several as `utf_*` additions (`utf_is_east_asian_wide`, `utf_is_other_space_separator`, `utf_space_width_em`) that were never added. Decide per function: promote to `lib/utf`, or declare Radiant-private in `RAD_06`.

### utf8proc boundary

- **UNI-6 (H) utf8proc calls are not behind one layer.** About 99 call sites outside `utf_string.cpp` (JS runtime regex/category/case code, `js_lexer.c`, `js_bt_regex.cpp`, `layout_block.cpp`, `markup_parser.cpp`, `lambda-eval.cpp`, `radiant_module.cpp`). Consequences: no single place to pin or upgrade the Unicode version, and `lib/utf.c`'s "no external dependency" property is not mirrored by a `utf_*` property API. Decision needed: wrap category, case and width queries as `utf_*` or accept direct utf8proc use in language-specific code.
- **UNI-7 (H) Dead and legacy surface in `utf_string`.** `string_compare_unicode` / `string_equal_unicode` (casefold+NFC then bytewise) have no callers outside the module, are not Lambda's order (S6.2.2v3) and are not UCA. `init_utf8proc_support` / `cleanup_utf8proc_support` are no-ops still called from `main.cpp`. APIs take `int` lengths and return `raw_free`-owned buffers, and the enum keeps the `UTF8PROC_COMPARE_*` names. Delete, or keep and document as the opt-in folding comparison.
- **UNI-8 (F) Unicode data version is implicit.** It is whatever utf8proc 2.10.0 ships, while the hand-written tables in `lib/utf.c` (emoji, scripts, CJK ranges) and Radiant (UAX#14, widths, full case mapping) are maintained by hand against a version nobody records. No test ties them together and nothing flags drift when utf8proc is bumped.

### Semantics and features

- **UNI-9 (F) No grapheme-cluster or word segmentation** (UAX#29). Deferred by Type_String §8. Radiant approximates cluster boundaries for caret and selection with local heuristics (ZWJ, combining marks, contextual scripts), so editing and layout can disagree with a future language-level `graphemes()`.
- **UNI-10 (F) Line breaking is an approximation, not UAX#14.** A hand-written class subset (ideographic, OP/CL/CJ/NS predicates) in Radiant; full break classes are not implemented.
- **UNI-11 (F) BiDi.** `lib/utf` only classifies strong direction. The reordering algorithm and its conformance level are Radiant's (`layout_bidi.cpp`, see the RAD set); not covered here.
- **UNI-12 (D, decided) HTML5 tokenizer DFA.** Kept separate on purpose because the HTML spec mandates its error behaviour. Recorded so that it is not re-proposed as a duplicate.
- **UNI-13 (open ruling) Locale and `Intl`.** The core language has no locale-aware ordering or formatting. How far hosted JS (`localeCompare`, `Intl`, `String.prototype.normalize`, which currently calls utf8proc NFC/NFD/NFKC/NFKD) should go is not designed here; check the JS docs before adding any locale feature, and ask for a ruling if it would touch S6.2.2v3.
- **UNI-14 (open ruling) Invalid UTF-8 at the boundary.** Strict validation (`utf8_valid`), replacement decode (`utf8_decode_replacement`) and the "structural" policy used by the UTF-16 offset helpers (skip continuation bytes, count the rest) are three different behaviours for malformed input. Which applies to file/network input, to `string(binary)`, and to the DOM text APIs is not pinned by a ruling.
- **UNI-15 (F) Malformed-input and large-document testing.** The old plan called for malformed UTF-8 recovery tests, very large Unicode documents and constrained-memory runs; none is tracked. Windows (cross-compiled utf8proc) and Linux distributions were also unverified.

### Housekeeping

- **UNI-16 (H) Dated plan items from the old Unicode2 follow-up (Oct–Dec 2025 targets) are lapsed**, and its success metrics ("95%+ Unicode suite", "benchmarks meeting targets") were never given a suite or targets. Superseded by the list above. The speculative items (RTF/PDF Unicode improvements, Unicode digits in number formatting, international calendars, Unicode regex classes) are unscheduled and are listed in no tracker; treat them as ideas, not commitments.
- **UNI-17 (H) Surrogate test coverage.** The original risk note asked for tests that surrogates are rejected on each previously permissive encode path. Confirm `test_str_gtest.cpp` and the JS tests cover `utf8_encode` vs `utf8_encode_wtf8` once UNI-1 is done.
- **UNI-18 (H) Cross-references.** No other markdown in the repo links to `Unicode1/2/3.md`; `Lib_Str.md` / `Lib_String.md` are the natural places to link this document.

## 5. Suggested order

1. UNI-1 and UNI-2 (mechanical, correctness-relevant, small blast radius), then UNI-3 after the UNI-14 ruling.
2. UNI-7 (delete dead surface) and UNI-6/UNI-8 as one decision about the utf8proc boundary and version pinning.
3. UNI-5 as a per-function promote-or-privatize pass inside the Radiant docs.
4. UNI-9/10 only if a language-level grapheme or segmentation feature is rulings-approved.
