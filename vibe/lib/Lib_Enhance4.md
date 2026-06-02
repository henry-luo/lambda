# `./lib` Enhancement Proposal 4 — New Primitive Modules (endian / uuid / color)

Fourth-round scan of `lambda/` and `radiant/` for code that can be centralized
under `./lib`, building on rounds 1–3 (`Lib_Enhance.md`, `Lib_Enhance2.md`,
`Lib_Enhance3.md`). Rounds 1–3 shipped: lru_cache, binsearch, thread_pool,
hashmap_helpers, hash, math_utils, sort, hex, time_util, atomic, escape, digest,
plus `lib/utf`, `lib/url`, `lib/base64`.

This round shipped **three new header-only primitive modules** and a handful of
adoption-only findings. All three are pure, dependency-light, and locked down
with focused gtests — the same low-risk profile as the hex/base64 work.

**Status legend:** ✅ done · ⏳ partial · ❌ proposed (not yet done) · ⛔ deferred

| Module | Status | Kills |
|--------|--------|-------|
| `lib/endian.h` | ✅ done | 5 duplicate font `rd16/rd32` helper sets + 2 `be*toh_local` copies |
| `lib/uuid.h` | ✅ done | 2 independent UUID-v4 generators |
| `lib/color.h` | ✅ done | 4 hex-color parsers + 2 formatters (round-3's deferred §7) |

Verification: `make build` clean; **lambda baseline 835/837** (2 pre-existing PDF
failures, unrelated); **radiant baseline byte-identical** to pre-change run
(layout baseline 3860/0 — proves the font endian migration is correct); 18 new
lib gtests pass.

---

## 1. `lib/endian.h` — portable byte-order reads/writes + byteswap ✅

**The duplication.** Every OpenType/font table parser hand-rolled its own
big-endian reader set (`rd16`/`rd16s`/`rd32`/`rd32s`), and two font files
hand-rolled `be32toh_local`/`be16toh_local`:

| Site | Had |
|------|-----|
| [lib/font/font_tables.c](../../lib/font/font_tables.c) | rd16, rd16s, rd32, rd32s |
| [lib/font/font_glyf.c](../../lib/font/font_glyf.c) | rd16, rd16s, rd32 |
| [lib/font/font_cbdt.c](../../lib/font/font_cbdt.c) | rd16, rd16s, rd32 |
| [lib/font/font_colr.c](../../lib/font/font_colr.c) | rd16, rd32 |
| [lib/font/font_gpos.c](../../lib/font/font_gpos.c) | rd16, rd16s, rd32 |
| [lib/font/font_database.c](../../lib/font/font_database.c) | be32toh_local, be16toh_local |
| [lib/font/font_config.c](../../lib/font/font_config.c) | be32toh_local, be16toh_local |

### Shipped API (`lib/endian.h`, header-only)

```c
// big-endian reads (network / OpenType order)
uint16_t read_be16(const uint8_t* p);   int16_t read_be16s(const uint8_t* p);
uint32_t read_be24(const uint8_t* p);
uint32_t read_be32(const uint8_t* p);   int32_t read_be32s(const uint8_t* p);
uint64_t read_be64(const uint8_t* p);
// little-endian reads (le16/le16s/le32/le32s/le64)
// big/little-endian writes (write_be16/be32, write_le16/le32)
// portable byteswap (bswap16/bswap32/bswap64)
```

### Migration

Each font file dropped its local definitions and aliased the short names to lib
via token-based macros (`#define rd16 read_be16`, etc.) — **zero call-site
churn** across all ~261 usages, logic centralized. The `be*toh_local` swappers
became `#define be32toh_local bswap32` (behaviour-identical on little-endian
hosts, which is every target platform).

**Risk:** low — pure transform, and the layout baseline (3860/0) exercises the
font reads end-to-end. gtests: `test/lib/test_endian_gtest.cpp` (BE/LE reads,
signed, write round-trips, byteswap, BE↔LE-via-swap).

---

## 2. `lib/uuid.h` — RFC 4122 v4 formatting ✅

**The duplication.** Two independent v4 generators with identical
version/variant bit-twiddling and 8-4-4-4-12 formatting, differing only in
entropy source:

- [lambda/js/js_crypto.cpp](../../lambda/js/js_crypto.cpp) `js_crypto_randomUUID` — OS crypto entropy
- [radiant/webdriver/webdriver_session.cpp](../../radiant/webdriver/webdriver_session.cpp) `generate_uuid` — `rand()`

### Shipped API (`lib/uuid.h`, header-only)

```c
#define UUID_STR_LEN 37  // 36 hex/hyphen chars + NUL
// set version(4)+variant bits on bytes in place, format canonical string into out
void uuid_v4_format(uint8_t bytes[16], char out[UUID_STR_LEN]);
```

The caller supplies the 16 random bytes, so the **entropy tier stays the
caller's choice** (crypto-strength for security IDs, cheap PRNG otherwise) — the
helper only owns the formatting that was duplicated. Both call sites migrated.

**Risk:** low. gtests: `test/lib/test_uuid_gtest.cpp` (shape, version/variant
bits, all-zero known vector → `00000000-0000-4000-8000-000000000000`).

---

## 3. `lib/color.h` — CSS hex color parse/format ✅ (was round-3 §7, deferred)

Round 3 deferred this "until a 4th caller appears or the two color structs are
unified." **The 4th caller appeared** ([radiant/css_animation.cpp](../../radiant/css_animation.cpp)
`parse_color_value`), so it graduated to actionable. The struct-divergence
concern is sidestepped by keeping the lib API **byte-level** (`r,g,b,a` in/out)
and letting each caller adapt into its own color struct.

Four parsers + two formatters consolidated:

- [lambda/input/css/css_properties.cpp](../../lambda/input/css/css_properties.cpp) `css_parse_hex_to_rgba` (3/4/6/8-digit, alpha)
- [radiant/graph_theme.cpp](../../radiant/graph_theme.cpp) `parse_hex_color` (6-digit) + `format_hex_color` (inverse)
- [radiant/resolve_htm_style.cpp](../../radiant/resolve_htm_style.cpp) `parse_html_color` (HTML `bgcolor`, 3/6-digit)
- [radiant/css_animation.cpp](../../radiant/css_animation.cpp) `parse_color_value` (hex branch; rgb()/named kept local)

### Shipped API (`lib/color.h`, header-only)

```c
// optional leading '#'; 3 (#rgb), 4 (#rgba), 6 (#rrggbb), 8 (#rrggbbaa) hex
// digits; short forms nibble-doubled; alpha defaults 255. false on bad input.
bool color_parse_hex(const char* str, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);
// format "#rrggbb" (lowercase) into out (>= 8 bytes)
void color_format_hex(uint8_t r, uint8_t g, uint8_t b, char* out);
```

The lib parser is the **superset** of all four callers' digit-count support and
is stricter on malformed input (rejects non-hex / wrong-length, where the old
`sscanf`/`strtol` paths silently accepted garbage). `css_animation` keeps its
hex-digit run-count so trailing content is still tolerated.

**Risk:** medium (CSS color parsing is hot) — mitigated by input parser baseline
(2105/2105) + radiant baseline (unchanged). gtests:
`test/lib/test_color_gtest.cpp` (all digit counts, no-hash, case, reject paths,
parse↔format round-trip).

---

## 4. Adoption-only findings — lib helper exists, just under-used ⏳

These need **no new logic** — `lib/file.h` and `lib/str.h` already provide the
helper, but ~20+ inline copies still hand-roll it. Pure migration, best done
opportunistically; not yet swept.

- `file_path_ext` / `file_path_dirname` / `file_path_basename` / `file_path_join`
  / `file_temp_path` / `file_is_file` / `file_is_dir` / `read_binary_file` — all
  exist in [lib/file.h](../../lib/file.h); inline `strrchr`/`snprintf("%s/%s")`/
  `fopen+fseek+fread` copies remain in runner.cpp, py_stdlib.cpp, surface.cpp,
  main.cpp, path.c, css_font_face.cpp, etc.
- `str_trim` / `str_span_whitespace` / `str_to_lower` — exist in
  [lib/str.h](../../lib/str.h); line-oriented parsers (input-pdf/csv/kv) still
  re-roll them inline.

**Two genuine small gaps** worth adding when the sweep happens:

```c
// lib/file.h — '/'-prefix + Windows /C:/ drive detection (open-coded in
//   target.cpp, input.cpp, input-dir.cpp)
bool file_path_is_absolute(const char* path);

// lib/str.h — recurs in input-pdf/csv/kv parsers; no lib equivalent today
void str_skip_to_line_end(const char** s, size_t* len);
```

---

## 5. Carried-over / deferred ⛔

- **`lib/escape.h` escape scanners + `lib/numparse.h`** (round-3 §6/§8) — the
  `\uXXXX`/`\UHHHHHHHH`/`\xHH` multi-digit scanners and `hex_scan`/`octal_scan`
  /`try_parse_int64`. 4+ copies (input-toml, build_py_ast, js, csv). **Medium
  risk** — per-language overflow/early-stop semantics differ; only migrate
  exact-match sites. Best done as its own focused round.
- **qsort comparators** (8+ type-specific one-liners) — centralizing adds
  indirection without real savings.
- **`%02x` hex logging** — cosmetic; `lib/hex.h` already covers the algorithmic
  need.
- **weak `rand()` Math.random** ([js_runtime.cpp](../../lambda/js/js_runtime.cpp),
  [rb_runtime.cpp](../../lambda/rb/rb_runtime.cpp)) — intentionally a separate
  quality tier from `crypto_random_bytes`; do not conflate.

---

## Priority summary

| # | Item | New code? | Risk | Status |
|---|------|-----------|------|--------|
| 1 | `lib/endian.h` (font rd16/rd32 + bswap) | yes | low | ✅ done |
| 2 | `lib/uuid.h` (`uuid_v4_format`) | yes | low | ✅ done |
| 3 | `lib/color.h` (hex parse/format) | yes | med | ✅ done |
| 4 | `file_path_is_absolute` + `str_skip_to_line_end` | small | low | ❌ proposed |
| 5 | Adopt existing `file_path_*`/`str_*` at inline sites | no | low | ❌ proposed (sweep) |
| 6 | `lib/escape.h` scanners + `lib/numparse.h` | yes | med | ⛔ deferred (own round) |
| 7 | qsort comparators / `%02x` logging / weak rand | — | — | ⛔ deferred |

### Shipped this round
New modules: `lib/endian.h`, `lib/uuid.h`, `lib/color.h`.
New gtests: `test/lib/test_endian_gtest.cpp` (6), `test_uuid_gtest.cpp` (3),
`test_color_gtest.cpp` (9), registered in `build_lambda_config.json`.
Migrated: 7 font files, js_crypto, webdriver_session, css_properties,
graph_theme, resolve_htm_style, css_animation.
