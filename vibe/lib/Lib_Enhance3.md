# `./lib` Enhancement Proposal 3 — Encoding / Decoding Centralization

Third-round scan of `lambda/` and `radiant/`, focused on the user's question:
can **hex, base64, and URL (percent) encoding/decoding** be centralized under
`./lib`? Rounds 1–2 (`Lib_Enhance.md`, `Lib_Enhance2.md`) are substantively
complete and shipped 12 lib modules (lru_cache, binsearch, thread_pool,
hashmap_helpers, hash, math_utils, sort, **hex**, time_util, atomic, **escape**,
**digest**) plus `lib/utf`, `lib/url`, `lib/base64`.

This round finds that those three encoding domains are **partially** centralized:

| Domain | lib module today | Gap found |
|--------|------------------|-----------|
| **base64** | `lib/base64.{h,c}` — **decode only** | ~9 hand-rolled **encoders**; no base64url |
| **hex** | `lib/hex.h` — full encode/decode | 4 JS sites still hand-roll nibble tables; 1 decode site |
| **URL** | `lib/url.{h,c}` — full percent + parse | serve/ has 2 duplicate decoders; js_url_module hand-rolls 4; `file://` built ad-hoc in 6 files |

Plus several adjacent utilities worth folding in (verbatim `utf8_encode` copy,
number-literal escape parsing, hex-color parsing).

**Status legend:** ✅ done · ⏳ partial · ❌ proposed (not yet done) · ⛔ deferred

> **Implementation status (shipped).** Items **1–6 are now implemented** and
> verified (lambda baseline 835/837 — the 2 failures are pre-existing PDF tests
> unrelated to this work; input parsers 2105/2105; radiant baseline unchanged).
> Summary of what landed:
> - §1 ✅ `lib/base64` gained `base64_encode`/`base64_encode_alloc`/
>   `base64_encoded_len` + `BASE64_URL` variant + `base64_decode_variant`; all 8
>   hand-rolled encoders and the 2 duplicate serve decoders now call lib.
> - §2 ✅ `lib/url` gained `url_decode_form`, `url_decode_inplace`,
>   `url_encode_with_table`; serve/ + `js_url_module` + `js_querystring` migrated.
> - §3 ✅ `lib/url` gained `url_from_local_path`; all 6 `file://` builders migrated.
> - §4 ✅ residual hex-nibble tables → `hex_encode_nibble_upper`; pdf asciihex
>   digit step → `hex_decode_byte`.
> - §5 ✅ bash `utf8_encode` deleted, now uses `lib/utf.h`.
> - §6 ⏳ the trivially-matching parts done; the `hex_scan`/`octal_scan` helper
>   itself is still open (carried into round 4's deferred list).
> - §7 (hex-color) was deferred here but **shipped in round 4** — see
>   [`Lib_Enhance4.md`](Lib_Enhance4.md) (the 4th caller materialized).
> - §8 (`numparse`), §9 (whitespace) remain deferred.
>
> New gtests: `test/lib/test_base64_gtest.cpp` (9) + URL helper cases added to
> `test_url_extra_gtest.cpp`. Round 4 continues in
> [`Lib_Enhance4.md`](Lib_Enhance4.md).

Everything below was the **original plan** (written before any code changed);
statuses above the line reflect what actually shipped. Items are ordered by impact.

---

## 1. base64 **encode** — the headline gap ❌

`lib/base64.h` exposes only `base64_decode`, `is_data_uri`, `parse_data_uri`.
There is **no encoder in lib**, so every caller that needs base64 *output*
re-implements the 3-byte→4-char loop with its own copy of the alphabet string.
**9 hand-rolled encoders** found:

| Site | Function | Notes |
|------|----------|-------|
| [lambda/serve/ipc_proto.cpp:17](../../lambda/serve/ipc_proto.cpp) | `b64_encode` + `b64_decode` | HTTP body encode for IPC |
| [lambda/serve/asgi_bridge.cpp:19](../../lambda/serve/asgi_bridge.cpp) | `base64_encode` + `base64_decode` | near-identical copy of ipc_proto |
| [lambda/input/input-pdf-postprocess.cpp:1488](../../lambda/input/input-pdf-postprocess.cpp) | `base64_alloc` | builds `data:` URIs for PDF images |
| [lambda/js/js_globals.cpp:13240](../../lambda/js/js_globals.cpp) | `js_btoa` (inline `b64_encode_table`) | JS `btoa()` |
| [lambda/js/js_buffer.cpp:840](../../lambda/js/js_buffer.cpp) | inline encode + decode | `Buffer.toString` — **also base64url** (alphabet at :841) |
| [lambda/js/js_crypto.cpp:312](../../lambda/js/js_crypto.cpp) | `bytes_to_base64_string` | HMAC digest output |
| [radiant/render_svg.cpp:204](../../radiant/render_svg.cpp) | `svg_append_base64` | raster fallback images |
| [radiant/webdriver/webdriver_actions.cpp:386](../../radiant/webdriver/webdriver_actions.cpp) | `base64_encode_data` | screenshot encoding |

Two callers already use lib correctly (decode side):
[radiant/render_svg_inline.cpp:2674](../../radiant/render_svg_inline.cpp),
[radiant/surface.cpp:445](../../radiant/surface.cpp).

The two `serve/` files (`ipc_proto.cpp` + `asgi_bridge.cpp`) are *near-verbatim
duplicates of each other* — including their own private `b64_decode` that
duplicates `lib/base64.c` too.

### Proposed API (`lib/base64.h`)

```c
// alphabet selector
typedef enum { BASE64_STD, BASE64_URL } Base64Variant;

// encoded length (excluding NUL); padded form for STD, unpadded for URL
size_t base64_encoded_len(size_t in_len, Base64Variant variant);

// encode `len` bytes into caller-provided `out` (>= base64_encoded_len()+1).
// returns bytes written (excluding NUL). NUL-terminates.
size_t base64_encode(const void* data, size_t len, char* out, Base64Variant variant);

// convenience: malloc + encode (caller frees), matches existing decode style
char*  base64_encode_alloc(const void* data, size_t len, Base64Variant variant);

// extend existing decoder to accept the URL alphabet (- _) and missing padding:
uint8_t* base64_decode_variant(const char* in, size_t in_len, size_t* out_len,
                               Base64Variant variant);
```

Keep `base64_decode` as a thin alias over `base64_decode_variant(.., BASE64_STD)`.

### Migration

Replace all 9 encoders + the 2 duplicate private decoders with these calls.
`js_buffer.cpp` gets base64url for free via the `BASE64_URL` variant. The
`serve/` duplication collapses to lib calls (the IPC `mem_alloc` category can be
handled by callers using `base64_encode` into a `mem_alloc`'d buffer, or keep
`base64_encode_alloc` for the `malloc` cases).

**Scope:** ~half a day. **Risk:** low — base64 is a fixed transform; round-trip
gtests (empty, 1/2/3-byte tails, padding, URL alphabet) lock it down. This is
the single highest-ROI item in the document: 9 copies → 1.

---

## 2. URL percent-encode/decode — consolidate onto `lib/url` ❌

`lib/url.h` already has the canonical encoders/decoders
(`url_encode_component`, `url_decode_component`, `url_encode_uri`,
`url_decode_uri`, `url_hex_to_int`) and the JS global wrappers in
[js_globals.cpp:13498](../../lambda/js/js_globals.cpp) already use them
correctly. But several subsystems still hand-roll percent-coding:

### 2a. `serve/` — two duplicate in-place decoders

- [lambda/serve/http_request.cpp:49](../../lambda/serve/http_request.cpp) — `static url_decode_inplace()` (used for query params + path)
- [lambda/serve/serve_utils.cpp:111](../../lambda/serve/serve_utils.cpp) — `serve_url_decode()` (used by [body_parser.cpp:117](../../lambda/serve/body_parser.cpp))

These two are duplicates of each other and of `lib/url`. Both add `+`→space
(form decoding), which `url_decode_component` does not. **Proposal:** add a
form-decode flavor to lib so both can migrate:

```c
// like url_decode_component but also maps '+' -> ' ' (application/x-www-form-urlencoded)
char* url_decode_form(const char* str, size_t len, size_t* out_len);
```

Then `serve/` keeps a thin in-place wrapper (it decodes into the same buffer for
zero-alloc request parsing) but the hex/`%XX` core lives in lib.

### 2b. `js_url_module.cpp` — 4 hand-rolled coders

- [js_url_module.cpp:259](../../lambda/js/js_url_module.cpp) `fileURLToPath` — inline `%XX` decode
- [js_url_module.cpp:303](../../lambda/js/js_url_module.cpp) `pathToFileURL` — inline `%20` encode
- [js_url_module.cpp:339](../../lambda/js/js_url_module.cpp) `parse_query_entries` — inline `%XX` decode + `+`→space
- [js_url_module.cpp:570](../../lambda/js/js_url_module.cpp) `js_usp_toString` — inline percent encode (own `"0123456789ABCDEF"` table)

All four can route through `url_encode_component` / `url_decode_component` /
`url_decode_form` once 2a lands.

### 2c. `js_querystring.cpp` — Node `querystring` semantics

[js_querystring.cpp:46](../../lambda/js/js_querystring.cpp) `js_qs_escape` hand-rolls
percent-encoding with explicit UTF-8 surrogate walking and its own hex table; the
*decode* side already calls `url_decode_component`. The encode side encodes a
**different unreserved set** than `encodeURIComponent` (Node's `querystring`
escapes more), so it can't blindly call `url_encode_component`. **Proposal:**
add a parameterized encoder taking an "unreserved" predicate/table:

```c
// encode with a caller-supplied 256-entry "keep as-is" table (1 = literal)
char* url_encode_with_table(const char* str, size_t len, const uint8_t keep[256]);
```

`url_encode_component`/`url_encode_uri` become thin wrappers over this with their
fixed tables; `js_qs_escape` supplies the Node table. This removes the last hand
hex-nibble loop while preserving exact Node semantics.

**Scope (2a–2c):** ~1 day. **Risk:** medium — percent-coding bugs are quiet
output bugs; needs round-trip + reserved-set gtests per caller.

---

## 3. `file://` URL construction — one builder ❌

`file://` URLs are assembled by hand in **6 files**, each with subtly different
rules (leading-slash count, Windows drive handling, backslash conversion):

- [lambda/path.c:873](../../lambda/path.c) — `strbuf_append_str(url_buf, "file://")`
- [lambda/target.cpp:57](../../lambda/target.cpp) `get_cwd_url`, and :220 Windows `file:///%s`
- [lambda/main.cpp:764,767](../../lambda/main.cpp) — `snprintf("file://%s", ...)`
- [lambda/validator/ast_validate.cpp:333,335](../../lambda/validator/ast_validate.cpp) — same pattern
- [lambda/input/input.cpp:881](../../lambda/input/input.cpp) — `strbuf_append_str("file://")`
- [lambda/js/js_url_module.cpp:312](../../lambda/js/js_url_module.cpp) — `memcpy(buf, "file://", 7)`

`lib/url.c` already has the inverse (`url_to_local_path`). **Proposal:** add the
forward direction so all six share one correct, percent-encoded, cross-platform
implementation:

```c
// build a file:// URL from an absolute local path (percent-encodes, handles
// Windows drive letters + backslashes, emits file:/// where required).
char* url_from_local_path(const char* abs_path);
```

**Scope:** ~half a day incl. migration. **Risk:** medium — Windows path edge
cases; the existing 6 copies disagree today, so centralizing *fixes* latent
inconsistencies but must be tested against each caller's expectation.

---

## 4. Residual hand-rolled **hex** nibble loops → `lib/hex.h` ❌

Round 1 shipped `lib/hex.h` and migrated the obvious sites. Remaining hand-rolled
nibble tables, all in JS percent-encoders (overlap with §2):

- [js_querystring.cpp:96](../../lambda/js/js_querystring.cpp) — `hex[byte>>4]`, `hex[byte&0xF]` with local `"0123456789ABCDEF"`
- [js_url_module.cpp:583](../../lambda/js/js_url_module.cpp) — same
- [js_globals.cpp:9729](../../lambda/js/js_globals.cpp) `js_percent_escape_char` + :9817 `js_string_append_percent_escape` — same

These should use `hex_encode_nibble_upper()` from `lib/hex.h` (or disappear
entirely once §2c's `url_encode_with_table` absorbs them).

One decode site is a clean migration:

- [lambda/input/pdf_decompress.cpp:382](../../lambda/input/pdf_decompress.cpp) `asciihex_decode` — full PDF ASCIIHex filter; the per-byte nibble parse → `hex_decode_byte()` (note: PDF ASCIIHex allows embedded whitespace and a `>` terminator, so the *loop* stays; only the digit→value step migrates).

**Scope:** folded into §2 (encode sites) + ~1 hr for pdf_decompress. **Risk:** low.

---

## 5. Verbatim `utf8_encode` copy in bash → `lib/utf.h` ❌

[lambda/bash/bash_expand.cpp:231](../../lambda/bash/bash_expand.cpp) defines a
local `static int utf8_encode(int codepoint, char* buf)` that is a **byte-for-byte
reimplementation** of `lib/utf.h::utf8_encode(uint32_t, char[4])`. Used by the
`$'...'` ANSI-C `\u`/`\U` escape handler (:330, :346).

**Proposal:** delete the local copy, include `lib/utf.h`, call `utf8_encode`. The
signatures differ only in the codepoint type (`int` vs `uint32_t`) and the lib
version already rejects surrogates/over-range, which is what bash wants.

**Scope:** 15 min. **Risk:** low (lib version is already test-covered).

---

## 6. Number-literal escape parsing (`\xHH`, `\NNN`, `\uHHHH`) ⏳ proposed

Several language front-ends hand-roll the same hex/octal escape scanners. The
hex *digit* step is `lib/hex.h::hex_decode_byte`, but the **multi-digit,
bounded-width scan** (`parse N hex/octal digits, stop early, report consumed`) is
duplicated:

- [lambda/bash/bash_expand.cpp:203](../../lambda/bash/bash_expand.cpp) `parse_hex` + :219 `parse_octal`
- [lambda/input/input-pdf-postprocess.cpp:397](../../lambda/input/input-pdf-postprocess.cpp) `parse_hex_token`
- [lambda/js/js_bt_regex.cpp:230](../../lambda/js/js_bt_regex.cpp) `parse_hex_escape`
- plus `strtoul(hex, .., 16)` one-offs in [build_ast.cpp:2058](../../lambda/build_ast.cpp), [js/build_js_ast.cpp:34](../../lambda/js/build_js_ast.cpp), [input/build_py_ast.cpp:133](../../lambda/input/build_py_ast.cpp), and TOML/YAML/RTF `\u` handlers.

**Proposal:** add to `lib/hex.h` (or a new `lib/numparse.h`):

```c
// scan up to max_digits hex digits from s; *consumed = digits read.
// returns accumulated value, or -1 if zero digits. Does not require NUL.
long hex_scan(const char* s, int max_digits, int* consumed);
long octal_scan(const char* s, int max_digits, int* consumed);
```

`bash_expand`'s `parse_hex`/`parse_octal` and the regex/PDF scanners collapse to
these. The `strtoul`-based AST sites are **left as-is** — they parse
NUL-bounded, already-validated substrings where `strtoul` is fine; migrating them
is churn without a bug fix.

**Scope:** ~half a day. **Risk:** low–medium — each language has slightly
different overflow/early-stop rules; only migrate sites whose semantics match
exactly, leave the rest.

---

## 7. Hex-**color** parsing (`#rgb` / `#rrggbb` / `#rrggbbaa`) ⛔ deferred

Three independent parsers exist:

- [lambda/input/css/css_properties.cpp:1231](../../lambda/input/css/css_properties.cpp) `css_parse_hex_to_rgba` — 3/4/6/8-digit, alpha-aware
- [radiant/graph_theme.cpp:28](../../radiant/graph_theme.cpp) `parse_hex_color` (6-digit) + `format_hex_color` (inverse)
- [radiant/resolve_htm_style.cpp:63](../../radiant/resolve_htm_style.cpp) — HTML `bgcolor` via `strtol(.., 16)` + component split

A `lib/color.h` (`color_parse_hex(str, &rgba)` / `color_format_hex`) would unify
these, but the three callers disagree on supported digit counts, alpha handling,
and the in-memory color struct (CSS vs Radiant `Color`). This is a **color
subsystem decision**, not a clean additive lib change — recommend deferring until
a 4th caller appears or the two color structs are unified. (Same posture as
round 1's geometry-struct decision.)

---

## 8. Promote `try_parse_int64` / `try_parse_double` to `lib/` ⛔ low priority

[lambda/input/input-utils.h:52,58](../../lambda/input/input-utils.h) already
provide safe, length-bounded `try_parse_int64` / `try_parse_double` /
`input_strncasecmp`, but they live under `lambda/input/` so non-input callers
(20+ raw `strtoll`/`strtod`/`atoi` sites in main.cpp, transpile-mir.cpp,
build_ast.cpp, lambda-eval-num.cpp) can't see them and use raw libc instead.

**Proposal:** move these three into a `lib/numparse.h` (could co-locate with §6's
`hex_scan`/`octal_scan`) and have `input-utils.h` re-export for source
compatibility. **Deferred** because: (a) the raw libc sites mostly parse
trusted/pre-validated input where error handling doesn't matter, (b) it's a
discoverability nudge, not a dedup of *logic*. Worth doing only alongside §6.

---

## 9. Whitespace-skip loops ⛔ deferred (cosmetic)

15+ inline `while (*p==' '||*p=='\t')` / `while(isspace(*p))` loops across
`radiant/render_svg_inline.cpp`, `radiant/cmd_layout.cpp`, `radiant/render_clip.cpp`,
`lambda/build_ast.cpp`, etc. `input-utils.h:95` has `skip_line_whitespace` but
it's barely adopted. Each loop differs in *which* bytes count as whitespace
(space/tab vs full `isspace` vs space-only), so a single helper won't fit all,
and the win is purely cosmetic. **Defer** (same call as round 1's ArrayList
accessor migration).

---

## Priority summary

| # | Item | Scope | Risk | Do now? |
|---|------|-------|------|---------|
| 1 | **base64 encode** in `lib/base64` (+ base64url) — kill 9 copies | ½ day | low | **Yes — highest ROI** |
| 2 | Consolidate URL percent-coders onto `lib/url` (serve/, js_url_module, qs) | 1 day | med | **Yes** |
| 3 | `url_from_local_path()` — unify 6 `file://` builders | ½ day | med | **Yes** |
| 4 | Residual hex nibble loops → `lib/hex.h` | ~1 hr* | low | With §1/§2 |
| 5 | bash `utf8_encode` → `lib/utf.h` | 15 min | low | **Yes — trivial** |
| 6 | `hex_scan`/`octal_scan` in `lib` for escape parsing | ½ day | low-med | Optional |
| 7 | `lib/color.h` hex-color parse/format | 1 day | med | Defer |
| 8 | Promote `try_parse_*` to `lib/numparse.h` | ½ day | low | With §6 only |
| 9 | Whitespace-skip helper | — | — | Defer (cosmetic) |

*§4 mostly disappears into §2's encoder rewrite.

### Recommended first PR
Items **1, 3, 5** are independent, low-risk, and each removes real duplication
(9 base64 copies, 6 `file://` builders, 1 verbatim UTF-8 copy). Item **2** is the
larger follow-up that also absorbs **4**. Each new/extended lib entry point
should ship with focused gtests (round-trip for base64/url, edge cases for
`url_from_local_path` Windows paths) consistent with rounds 1–2 (which reached
783+ passing lib tests).

### Out of scope / explicitly not changing
- JSON/JS/Python `\uXXXX` AST-builder escapes that already use NUL-bounded
  `strtoul` on validated substrings (§6) — churn without bug fix.
- CSS/HTML/graph color parsing (§7) — needs a color-subsystem decision first.
- `lib/base64`'s `parse_data_uri` / `is_data_uri` — already centralized and used.
