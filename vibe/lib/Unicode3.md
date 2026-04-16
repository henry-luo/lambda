# Unicode Centralization Proposal — `lib/utf.c`

## Problem

Unicode codepoint handling is duplicated across **20+ files** in the Lambda/Radiant codebase. The same UTF-8 encode/decode logic is copy-pasted in at least 12 places, surrogate pair handling in 8+, and character classification functions are duplicated between layout and intrinsic sizing. This creates:

- **Bug risk**: Subtle differences between copies (e.g., `< 0x110000` vs `<= 0x10FFFF`, null-termination vs not)
- **Maintenance burden**: Fixing a bug requires patching every copy
- **Binary bloat**: Identical code inlined across translation units
- **Discoverability**: New code authors don't know which UTF-8 function to call

## Inventory of Scattered Unicode Code

### 1. UTF-8 Encode (codepoint → bytes) — **12 copies**

| File | Function/Pattern | Notes |
|------|-----------------|-------|
| `lib/str.c:1148` | `str_utf8_encode()` | Canonical. Returns byte count, capacity-checked, rejects surrogates |
| `lambda/input/input-utils.cpp:17` | `codepoint_to_utf8()` | Null-terminates output. Does NOT reject surrogates |
| `radiant/event.cpp:321` | `static codepoint_to_utf8()` | Local static. Null-terminates. No surrogate check |
| `radiant/pdf/fonts.cpp:1259` | `static encode_utf8()` | Local static. Returns byte count. No surrogate check |
| `lambda/bash/bash_builtins.cpp:59` | `static utf8_encode()` | Local static. Returns byte count. Uses `< 0x110000` |
| `lambda/js/js_runtime.cpp:13781` | `static js_cp_to_utf8()` | Local static. Returns byte count |
| `test/test_html_roundtrip_gtest.cpp:174` | `static encode_utf8()` | Test-only copy |
| `lambda/js/build_js_ast.cpp:240,397,821,860` | Inline bit-shifting | 4 separate inline encode blocks |
| `lambda/js/js_early_errors.cpp:137` | Inline bit-shifting | Full inline encode |
| `radiant/render_pdf.cpp:188` | Inline bit-shifting | In text transform loop |
| `radiant/render_svg.cpp:127` | Inline bit-shifting | In text transform loop |
| `lib/font/font_tables.c:834` | Inline bit-shifting | In cmap name decode |

**Differences between copies:**
- `str_utf8_encode()` rejects surrogates (0xD800–0xDFFF) → correct
- `codepoint_to_utf8()` and most others do NOT reject surrogates → potential WTF-8 leaks
- Some null-terminate, others return byte count only
- Range checks vary: `< 0x110000` vs `<= 0x10FFFF` (equivalent but inconsistent)

### 2. UTF-8 Decode (bytes → codepoint) — **5 implementations**

| File | Function | Notes |
|------|----------|-------|
| `lib/str.c:1103` | `str_utf8_decode()` | Canonical. Full validation, overlong/surrogate rejection |
| `lambda/js/js_runtime.cpp:9267` | `js_decode_utf8()` | Local JS-specific decoder |
| `lambda/input/css/css_tokenizer.cpp:121` | `css_parse_unicode_char()` | Manual 2/3/4-byte decode |
| `lambda/input/html5/html5_tokenizer.cpp:88` | Hoehrmann DFA `utf8_decode()` | DFA-based decoder for HTML5 spec compliance |
| `lambda/input/html_entities.cpp:79` | `utf8_first_codepoint()` | Decode first codepoint only |

### 3. UTF-8 Validation — **2 implementations**

| File | Function | Notes |
|------|----------|-------|
| `lib/str.c:1061` | `str_utf8_valid()` | Manual validation with SWAR fast-path |
| `lambda/utf_string.cpp:28` | `is_valid_utf8()` | Wraps `utf8proc_iterate` loop |

### 4. UTF-16 Surrogate Pair Decode — **8 sites**

| File | Location | Notes |
|------|----------|-------|
| `lambda/input/input-utils.cpp:52` | `decode_surrogate_pair()` | Canonical. Clean API |
| `lambda/build_ast.cpp:2057` | Inline | Lambda string literal parsing |
| `lambda/input/input-prop.cpp:87` | Inline | Properties file parser |
| `lambda/input/input-toml.cpp:65` | Inline | TOML parser |
| `lambda/input/input-utils.hpp:99` | Inline | In `parse_escape_char()` |
| `lambda/js/js_globals.cpp:1407,1453` | Inline | `encodeURI`/`decodeURI` helpers |
| `lambda/js/js_runtime.cpp:13879` | Inline | JS `String.fromCodePoint` |
| `lib/url.c:887,942` | Inline | URI percent-decode |

### 5. Codepoint → UTF-16 Surrogate Pair Encode — **7 sites**

All in Apple CoreText font code, identical pattern:

| File | Lines | Sites |
|------|-------|-------|
| `lib/font/font_platform.c` | 505, 602, 824, 859, 871 | 5 |
| `lib/font/font_rasterize_ct.c` | 92, 145 | 2 |

Pattern repeated identically each time:
```c
if (codepoint <= 0xFFFF) {
    utf16[0] = (UniChar)codepoint;
    utf16_len = 1;
} else {
    uint32_t cp = codepoint - 0x10000;
    utf16[0] = (UniChar)(0xD800 + (cp >> 10));
    utf16[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
    utf16_len = 2;
}
```

### 6. Unicode Character Classification — duplicated between files

| Function | `layout_text.cpp` | `intrinsic_sizing.cpp` | Notes |
|----------|--------------------|------------------------|-------|
| `is_emoji_for_zwj()` | line 844 | line 76 | **Exact duplicate** |
| `is_zwj_composition_base()` | line 860 | line 88 | **Exact duplicate** |
| `is_cjk_character()` | line 446 | — | Only in layout_text |
| `has_id_line_break_class()` | line 466 | — | 120-line UAX#14 table, called from intrinsic_sizing |

### 7. Other Unicode Facilities (not duplicated, but scattered)

| File | Functions | Category |
|------|-----------|----------|
| `lambda/utf_string.cpp` | NFC/NFD/NFKC/NFKD normalization, casefold | Normalization |
| `radiant/layout_text.cpp` | `is_east_asian_fw()`, `is_hangul()`, `is_other_space_separator()`, `is_typographic_letter_unit()`, `get_unicode_space_width_em()` | Classification |
| `radiant/layout_text.cpp` | `FullCaseMapping` table, `apply_text_transform_full()` | Case mapping |
| `radiant/layout_text.cpp` | `is_line_break_op()`, `is_line_break_cl()`, `is_line_break_cj()`, `is_line_break_ns()`, `classify_break()` | Line breaking (UAX#14) |
| `radiant/resolve_htm_style.cpp:113` | `bidi_strong_class()` | BiDi detection |
| `radiant/layout_block.cpp:526` | `is_first_letter_punctuation()` | Punctuation class |
| `lib/cmdedit_utf8.c` | Display width, cursor movement, char/byte offset conversion | REPL/terminal |
| `lambda/input/css/css_tokenizer.cpp` | `css_is_name_start_char_unicode()`, `css_parse_unicode_char()` | CSS spec |
| `lambda/input/html5/html5_tokenizer.cpp` | Hoehrmann DFA, `html5_is_invalid_codepoint()` | HTML5 spec |
| `radiant/layout_counters.cpp` | Georgian/Greek counter encoding to UTF-8 | CSS counters |

---

## Proposal: Centralize into `lib/utf.c` + `lib/utf.h`

### Phase 1 — Core UTF Codec (Low Risk, High Impact)

**New file**: `lib/utf.c` + rewrite `lib/utf.h` (currently just an `#error` stub)

Provide a single, correct, well-tested set of UTF codec functions:

```c
// lib/utf.h — Core Unicode codec and classification utilities

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── UTF-8 Codec ────────────────────────────────────────────────────

// Encode a Unicode codepoint as UTF-8.
// Returns bytes written (1–4), or 0 on invalid codepoint (surrogates, > 0x10FFFF).
// Does NOT null-terminate. Caller needs buf[4].
size_t utf8_encode(uint32_t codepoint, char buf[4]);

// Encode a Unicode codepoint as null-terminated UTF-8.
// Returns bytes written (1–4), or 0 on invalid codepoint.
// Caller needs buf[5].
size_t utf8_encode_z(uint32_t codepoint, char buf[5]);

// Decode one UTF-8 codepoint from s[0..len-1].
// Returns bytes consumed (1–4), writes codepoint to *out.
// Returns -1 on invalid/truncated/overlong sequence or surrogate.
int utf8_decode(const char* s, size_t len, uint32_t* out);

// Number of bytes for a UTF-8 lead byte (1–4), or 0 if invalid.
size_t utf8_char_len(unsigned char lead);

// Count Unicode codepoints (not bytes) in s[0..len-1].
size_t utf8_count(const char* s, size_t len);

// Validate that s[0..len-1] is well-formed UTF-8.
bool utf8_valid(const char* s, size_t len);

// Convert char index → byte offset. Returns (size_t)-1 if out of range.
size_t utf8_char_to_byte(const char* s, size_t len, size_t char_index);

// Convert byte offset → char index.
size_t utf8_byte_to_char(const char* s, size_t len, size_t byte_offset);

// ── UTF-16 Surrogate Pairs ────────────────────────────────────────

// Decode a UTF-16 surrogate pair to a codepoint.
// Returns codepoint >= 0x10000, or 0 on invalid pair.
uint32_t utf16_decode_pair(uint16_t high, uint16_t low);

// Encode a codepoint as UTF-16 into utf16[2].
// Returns number of UTF-16 code units written (1 for BMP, 2 for supplementary).
// Returns 0 on invalid codepoint.
int utf16_encode(uint32_t codepoint, uint16_t utf16[2]);

// ── Codepoint Classification ──────────────────────────────────────

bool utf_is_surrogate(uint32_t cp);       // 0xD800–0xDFFF
bool utf_is_valid_codepoint(uint32_t cp); // 0–0x10FFFF, not surrogate

// CJK / East Asian
bool utf_is_cjk(uint32_t cp);            // Han, Kana, Hangul
bool utf_is_hangul(uint32_t cp);          // Hangul Jamo/Syllables/Extensions
bool utf_is_east_asian_wide(uint32_t cp); // East Asian Width F/W (wraps utf8proc)

// Emoji
bool utf_is_emoji_for_zwj(uint32_t cp);
bool utf_is_zwj_composition_base(uint32_t cp);

// Unicode spaces and separators
bool utf_is_other_space_separator(uint32_t cp); // Zs category minus U+0020/U+00A0
float utf_space_width_em(uint32_t cp);          // Unicode-specified space widths

#ifdef __cplusplus
}
#endif
```

### Phase 2 — Migrate Callers

**Step 2a**: Replace all local UTF-8 encode functions with `utf8_encode()` / `utf8_encode_z()`:

| Callsite | Change |
|----------|--------|
| `lambda/input/input-utils.cpp` `codepoint_to_utf8()` | Thin wrapper calling `utf8_encode_z()` (keep API for compatibility) |
| `radiant/event.cpp` `codepoint_to_utf8()` | Replace local static → `#include "utf.h"` + `utf8_encode_z()` |
| `radiant/pdf/fonts.cpp` `encode_utf8()` | Replace local static → `utf8_encode()` |
| `lambda/bash/bash_builtins.cpp` `utf8_encode()` | Replace local static → `utf8_encode()` |
| `lambda/js/js_runtime.cpp` `js_cp_to_utf8()` | Replace local static → `utf8_encode()` |
| `test/test_html_roundtrip_gtest.cpp` `encode_utf8()` | Replace local static → `utf8_encode_z()` |
| `lambda/js/build_js_ast.cpp` (4 inline sites) | Replace inline bit-shifting → `utf8_encode()` |
| `lambda/js/js_early_errors.cpp` | Replace inline bit-shifting → `utf8_encode()` |
| `radiant/render_pdf.cpp:188` | Replace inline bit-shifting → `utf8_encode()` |
| `radiant/render_svg.cpp:127` | Replace inline bit-shifting → `utf8_encode()` |
| `lib/font/font_tables.c:834` | Replace inline bit-shifting → `utf8_encode()` |

**Step 2b**: Replace all local surrogate pair code with `utf16_decode_pair()` / `utf16_encode()`:

| Callsite | Change |
|----------|--------|
| `lambda/build_ast.cpp:2057` | Replace inline → `utf16_decode_pair()` |
| `lambda/input/input-prop.cpp:87` | Replace inline → `utf16_decode_pair()` |
| `lambda/input/input-toml.cpp:65` | Replace inline → `utf16_decode_pair()` |
| `lambda/js/js_globals.cpp:1407,1453` | Replace inline → `utf16_decode_pair()` |
| `lambda/js/js_runtime.cpp:13879,9955` | Replace inline → `utf16_decode_pair()` / `utf16_encode()` |
| `lib/url.c:887,942` | Replace inline → `utf16_decode_pair()` |
| `lib/font/font_platform.c` (5 sites) | Replace inline → `utf16_encode()` |
| `lib/font/font_rasterize_ct.c` (2 sites) | Replace inline → `utf16_encode()` |

**Step 2c**: Deduplicate classification functions:

| Callsite | Change |
|----------|--------|
| `radiant/intrinsic_sizing.cpp:76` `is_emoji_for_zwj()` | Delete duplicate, call `utf_is_emoji_for_zwj()` |
| `radiant/intrinsic_sizing.cpp:88` `is_zwj_composition_base()` | Delete duplicate, call `utf_is_zwj_composition_base()` |

### Phase 3 — Migrate `str_utf8_*` from `lib/str.c`

The `str_utf8_*` functions in `lib/str.c` (lines 1011–1203) are the current canonical implementations. Options:

**Option A (recommended)**: Move the implementations into `lib/utf.c`, keep `str_utf8_*` as thin inline wrappers in `str.h` for backward compatibility:
```c
// str.h
static inline int str_utf8_decode(const char* s, size_t len, uint32_t* cp) {
    return utf8_decode(s, len, cp);
}
```

**Option B**: Keep `str_utf8_*` in `str.c`, have `lib/utf.c` call them. This avoids touching `str.c` but keeps two naming conventions.

### Phase 4 — Future Consolidation (Out of Scope)

These are **not** proposed for immediate migration but noted for completeness:

| Component | Reason to Keep Separate |
|-----------|------------------------|
| `lambda/utf_string.cpp` (NFC/NFD/casefold) | Lambda-specific, depends on Lambda types |
| `radiant/layout_text.cpp` (UAX#14 line break tables) | Large, CSS-layout-specific |
| `radiant/layout_text.cpp` (FullCaseMapping tables) | CSS text-transform specific |
| `radiant/resolve_htm_style.cpp` (BiDi classes) | HTML5 spec-specific |
| `lambda/input/css/css_tokenizer.cpp` (CSS ident chars) | CSS Syntax spec-specific |
| `lambda/input/html5/html5_tokenizer.cpp` (Hoehrmann DFA) | HTML5 spec-mandated behavior |
| `lib/cmdedit_utf8.c` | REPL-specific, depends on utf8proc display width |
| `radiant/layout_counters.cpp` | CSS counter styles, script-specific encoding |

---

## Relationship to Existing Files

| File | Status After Migration |
|------|----------------------|
| `lib/utf.h` | **Rewritten** — from `#error` stub to full header |
| `lib/utf.c` | **New** — core implementations |
| `lib/str.c` (§13) | Lines 1011–1203 moved to `utf.c`, thin wrappers remain |
| `lib/str.h` | Inline wrappers → `utf.h` functions |
| `lambda/input/input-utils.h` | `codepoint_to_utf8()` becomes wrapper → `utf8_encode_z()` |
| `lambda/input/input-utils.cpp` | `codepoint_to_utf8()` / `decode_surrogate_pair()` become wrappers |
| `lib/cmdedit_utf8.c` | No change (utf8proc-specific REPL code) |
| `lambda/utf_string.cpp` | No change (normalization stays Lambda-specific) |

## Build Integration

Add `lib/utf.c` to the build in `build_lambda_config.json`. No new dependencies — pure C, no utf8proc required for the core codec. Classification functions that wrap utf8proc (`utf_is_east_asian_wide`) would be conditionally compiled.

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Behavior changes in edge cases | Existing `test_str_gtest.cpp` UTF-8 tests cover encode/decode/valid. Add targeted tests for surrogate rejection in encode paths that previously allowed them |
| `codepoint_to_utf8()` callers expect null-termination | `utf8_encode_z()` variant preserves this; wrapper in input-utils.h keeps old API |
| Inline encode in JS AST is performance-sensitive | `utf8_encode()` is trivially inlineable by LTO or can be marked `inline` in header |
| CoreText surrogate encode is Apple-only | `utf16_encode()` uses `uint16_t`, not `UniChar` — simple cast at callsite |

## Estimated Scope

- **New code**: ~200 lines (`lib/utf.c` + `lib/utf.h`)
- **Deleted duplicate code**: ~400+ lines of local statics and inline encode blocks
- **Modified files**: ~20 files (mostly mechanical: delete local function, add `#include`, rename call)
- **Net reduction**: ~200+ lines
