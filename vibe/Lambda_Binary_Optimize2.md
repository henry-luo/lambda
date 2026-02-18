# Lambda.exe Binary Size Analysis & Optimization Report

**Generated**: February 14, 2026
**Platform**: macOS arm64

| Build | Size | Flags |
|-------|------|-------|
| **Debug** | 12.8 MB (13,383,800 bytes) | `-O0 -g` |
| **Release** | **7.9 MB** (8,242,120 bytes) | `-O2 -flto=thin -ffunction-sections -fdata-sections -fvisibility=hidden -Wl,-dead_strip -Wl,-x` |

**Release achieved 38% reduction** vs debug (12.8 MB → 7.9 MB) through ThinLTO, dead-strip, and symbol stripping.

---

## Release Build Analysis (7.9 MB)

### Segment Breakdown (Release)

| Segment | Size | % of Binary |
|---------|------|-------------|
| `__TEXT` (code + const data) | 7.31 MB | 93.0% |
| `__DATA_CONST` | 0.23 MB | 3.0% |
| `__LINKEDIT` (symbols/relocs) | 0.24 MB | 3.0% |
| `__DATA` | 0.08 MB | 1.0% |

Key differences from debug: `__TEXT` dropped from 10.3 → 7.3 MB (optimized code), `__LINKEDIT` dropped from 2.2 → 0.24 MB (symbols stripped by `-Wl,-x`), `__DATA` dropped from 0.1 → 0.08 MB.

### Release Build Config Details

```
Compiler:  -O2 -flto=thin -ffunction-sections -fdata-sections
           -fvisibility=hidden -fvisibility-inlines-hidden
           -pedantic -fno-omit-frame-pointer
Linker:    -flto=thin -Wl,-dead_strip -Wl,-x
Defines:   -DNDEBUG
```

**Observations on current release config:**
- Uses `-O2` (Premake `optimize "On"`) — the JSON config specifies `-O3` but the Premake generator overrides to `-O2`
- Uses ThinLTO (`-flto=thin`) — faster link than full LTO but less aggressive cross-module optimization
- Already strips local symbols (`-Wl,-x`) — `__LINKEDIT` already minimized to 240 KB
- Already uses dead-strip and function/data sections — linker eliminates unreferenced code
- Has `-fno-omit-frame-pointer` — costs ~1-2% code size for stack frame setup; useful for profiling but unnecessary in pure release
- Additional `strip -x` on the release binary saves only ~100 KB (already well stripped)
- `-Wl,-force_load,libnghttp2.a` forces entire nghttp2 into the binary even if unused symbols exist

### In-Binary Size Contributions (Release, by symbol group)

| Group                  | Size       | %         | Symbols | Notes                                                                      |
| ---------------------- | ---------- | --------- | ------- | -------------------------------------------------------------------------- |
| **Project code**       | **825 KB** | **10.5%** | 187     | Heavily stripped — most project symbols invisible                          |
| **curl + deps**        | **680 KB** | **8.7%**  | 0       | Invisible to `nm` (LTO internalized all symbols); verified by stub-linking |
| **mbedTLS**            | 500 KB     | 6.4%      | 764     | SSL/TLS + crypto + X.509 (partially overlaps with curl)                    |
| **MIR (JIT compiler)** | **396 KB** | **5.1%**  | 103     | c2mir: 180 KB, mir.o+mir-gen.o: 215 KB (verified by full stub-linking)     |
| **tree-sitter**        | 346 KB     | 4.4%      | 63      | All 4 grammars combined                                                    |
| **turbojpeg**          | ~240 KB    | ~2.9%     | 237     | Rebuilt -Os, arith coding removed; `jpeg_aritab` 859 KB was nm error       |
| **woff2**              | 205 KB     | 2.6%      | 13      | Font decompression                                                         |
| **brotli**             | 167 KB     | 2.1%      | 11      | Dictionary data (130 KB)                                                   |
| **mpdec**              | 131 KB     | 1.7%      | 66      | Decimal arithmetic                                                         |
| **freetype**           | 87 KB      | 1.1%      | 56      | Font rendering                                                             |
| **libpng**             | 73 KB      | 0.9%      | 195     | PNG support                                                                |
| **zlib**               | 50 KB      | 0.6%      | 14      | Compression                                                                |
| **GLFW**               | 48 KB      | 0.6%      | 265     | Window management                                                          |
| **nghttp2**            | 47 KB      | 0.6%      | 59      | HTTP/2 (curl dependency)                                                   |
| **libevent**           | 39 KB      | 0.5%      | 128     | Event loop                                                                 |
| **bzip2**              | 19 KB      | 0.2%      | 9       | Compression                                                                |
| **re2**                | 214 KB     | 2.6%      | —       | Regex engine (static, no abseil; LTO internalized all symbols)             |
| **rpmalloc**           | 9 KB       | 0.1%      | 8       | Memory allocator                                                           |
| **utf8proc**           | 4 KB       | 0.1%      | 9       | Unicode (tables in __TEXT const)                                           |

**Note**: "Project code" at 825 KB is an undercount — the release build's `-Wl,-x` stripped most local symbols, so ~410 KB of project code hides inside the `operator delete` → GLFW gap and other stripped regions. True project code contribution is estimated at **~1.5-2 MB**.

**Note on curl**: The 680 KB is curl's **transitive cost** — curl's own code plus the mbedTLS/nghttp2/libevent code that only curl pulls in. When curl is stubbed out, dead-strip removes its dependency code too. The mbedTLS 500 KB figure (from `nm` symbols) partially overlaps — some mbedTLS code exists solely to serve curl (TLS handshake, certificate verification), while `sha256` is used independently by `enhanced_file_cache.cpp`. The sum of the table therefore exceeds 100% in the curl/mbedTLS overlap region.

**⚠️ Correction**: The MIR size of 4.1 MB / 53.2% and turbojpeg size of 1.1 MB / 14.3% reported by initial `nm` analysis were both **nm consecutive-address errors**. Actual MIR contribution is **396 KB (5.1%)** (verified by stub-linking). Actual turbojpeg is **~240 KB** after rebuilding with -Os and no arithmetic coding. The largest *real* single contributor by stub-linking is **curl + deps at 680 KB** (transitive cost including mbedTLS/nghttp2).

### Largest Symbols in Release Binary

| Size | Symbol | Source |
|------|--------|--------|
| ~~859 KB~~ | ~~`jpeg_aritab`~~ | ~~turbojpeg — nm consecutive-address error; actual table is 912 bytes~~ |
| **410 KB** | `operator delete` gap | Stripped project code (layout, rendering, parsers) |
| **254 KB** | `TT_RunIns` | FreeType — TrueType bytecode interpreter |
| **172 KB** | WOFF2 type info | woff2 — RTTI data for WOFF2StringOut |
| **~145 KB** | `c2mir_compile` (code only) | MIR JIT — C-to-MIR compiler (see ⚠️ correction below) |
| **139 KB** | `tree_sitter_latex_math` | tree-sitter — LaTeX math grammar |
| **130 KB** | `kBrotliContextLookupTable` | brotli — decompression context table |
| **130 KB** | `MIR_set_gen_interface` | MIR JIT — code generator interface |
| **119 KB** | `psa_to_ssl_errors` | mbedTLS — error code mapping table |
| **88 KB** | `mpd_free` gap | mpdec + stripped code |
| **55 KB** | `jinit_huff_encoder` | turbojpeg — Huffman encoder init |
| **53 KB** | `MIR_gen` | MIR JIT — machine code generator |
| **48 KB** | `tree_sitter_latex` | tree-sitter — LaTeX grammar |
| **47 KB** | `tree_sitter_javascript` | tree-sitter — JavaScript grammar |
| **45 KB** | `FT_Stream_OpenBzip2` | FreeType — bzip2 font stream |
| **44 KB** | `tree_sitter_lambda` | tree-sitter — Lambda grammar |

### ⚠️ MIR Size Correction: c2mir_compile Is NOT 3.8 MB

The initial symbol analysis reported `c2mir_compile` as 3.8 MB (48% of the binary). **This was wrong** — an artifact of the `nm` consecutive-address differencing method.

**What happened**: `_c2mir_compile` (at `0x1002523b4`) is the **last function symbol** in the `__text` section. The next symbol `nm` sees is `_mbedtls_ssl_tls13_labels` (at `0x1005f4060`) — which is in the `__const` data section. The 3.8 MB gap between them is not code; it contains **`__const`, `__cstring`, and other read-only data from ALL libraries** (brotli tables, mbedTLS tables, tree-sitter data, turbojpeg tables, etc.).

**Actual MIR contribution** (verified by stub-linking ALL of MIR — mir.o + mir-gen.o + c2mir.o):

| Component | Actual Size | Method |
|-----------|------------|--------|
| c2mir (C-to-MIR compiler) | **180 KB** | Stub c2mir.o, link, diff |
| mir.o + mir-gen.o (IR + codegen) | **215 KB** | Stub all 3, link, diff |
| **Total MIR** | **396 KB (5.1%)** | Full stub-linking |

Section-level breakdown (full MIR vs all-MIR-stubbed, unstripped):

| Section | With MIR | Without MIR | MIR contrib |
|---------|----------|-------------|-------------|
| `__text` (executable code) | 5,043,348 | 4,717,400 | 318 KB |
| `__cstring` (string literals) | 434,477 | 395,661 | 38 KB |
| `__const` (TEXT + DATA) | 2,074,542 | 2,054,582 | 19 KB |
| **Total (stripped)** | **7,963,912** | **7,558,792** | **396 KB** |

**Lesson**: The `nm` consecutive-address method is unreliable for the last symbol in a section — it absorbs all subsequent section data. Always verify with stub-linking or section-level comparisons.

### ⚠️ turbojpeg Size Correction: jpeg_aritab Is NOT 859 KB

The 859 KB `jpeg_aritab` figure was another **nm consecutive-address error** (same class of bug as the MIR overcount). The actual `jpeg_aritab` table in `jaricom.c.o` is only **912 bytes**.

**Verified by rebuilding libjpeg-turbo 3.1.2 from source with `-Os` and arithmetic coding disabled** (`-DWITH_ARITH_ENC=OFF -DWITH_ARITH_DEC=OFF`):

| Build | Binary Size | Diff |
|-------|------------|------|
| Custom -Os, with arith | 8,164,232 | baseline |
| Custom -Os, **no arith** | **8,147,672** | **-16 KB** |
| Homebrew -O2, with arith | 8,182,728 | +18 KB vs -Os |

Arithmetic coding's actual cost: **16 KB** (not 859 KB). The `-Os` optimization saves an additional 18 KB vs Homebrew's `-O2` build. Current build uses the no-arith `-Os` variant at `build_temp/libjpeg-turbo/build-noarith/libturbojpeg.a`.

---

## Debug Build Analysis (12.8 MB) — Baseline Reference

### Segment Breakdown (Debug)

| Segment | Size | % of Binary |
|---------|------|-------------|
| `__TEXT` (code) | 10.3 MB | 80.4% |
| `__LINKEDIT` (symbols/relocs) | 2.2 MB | 17.1% |
| `__DATA_CONST` | 0.2 MB | 1.6% |
| `__DATA` | 0.1 MB | 0.9% |

---

## Static Library Contributions (12.6 MB total .a input)

| Library | .a Size | In-binary est. | Purpose |
|---------|---------|----------------|---------|
| **MIR** | **3.3 MB** | ~183 KB code + large data | JIT compiler |
| **tree-sitter-lambda** | **1.6 MB** | ~1.2 MB (parse tables) | Lambda grammar parse tables |
| **ThorVG** | **1.1 MB** | ~300 KB | SVG vector graphics |
| **freetype** | 767 KB | ~121 KB | Font rendering |
| **curl** | 764 KB | ~664 KB | HTTP client (mbedTLS) |
| **turbojpeg** | 746 KB | ~240 KB | JPEG codec (rebuilt -Os, no arithmetic coding) |
| **mbedcrypto** | 715 KB | ~348 KB | Cryptographic primitives |
| **mbedtls** | 433 KB | (included above) | SSL/TLS library |
| **libevent** | 408 KB | ~53 KB | HTTP server events |
| **tree-sitter-javascript** | 391 KB | ~165 KB | JS grammar (parse tables) |
| **utf8proc** | 340 KB | ~335 KB | Unicode text processing |
| **tree-sitter-latex-math** | 324 KB | ~105 KB | LaTeX math grammar |
| **glfw** | 305 KB | ~84 KB | OpenGL window mgmt |
| **png** | 245 KB | ~189 KB | PNG image format |
| **nghttp2** | 242 KB | ~49 KB | HTTP/2 (curl dependency) |
| **tree-sitter** | 231 KB | ~(shared runtime) | Parsing framework |
| **mpdec** | 194 KB | ~56 KB | Decimal arithmetic |
| **brotlicommon** | 130 KB | ~120 KB (dictionary) | Brotli common (WOFF2 dep) |
| **tree-sitter-latex** | 118 KB | ~(parse tables) | LaTeX parser |
| **zlib** | 89 KB | ~24 KB | Compression |
| **mbedx509** | 85 KB | (included in TLS) | X.509 certificate parsing |
| **bzip2** | 63 KB | ~28 KB | Compression |
| **brotlidec** | 49 KB | ~(decompressor) | Brotli decompression |
| **woff2dec** | 42 KB | ~29 KB | WOFF2 font decompression |
| **gif** | 32 KB | ~small | GIF image format |
| **libevent_openssl** | 19 KB | ~small | Event lib OpenSSL support |
| **woff2common** | 9 KB | ~small | WOFF2 common |
| **re2** | 701 KB | ~214 KB | Regex engine (static, built from 2023-03-01 without abseil) |

**Dead code elimination**: The linker stripped very little (~1%) from the debug build because `-O0` prevents function-level stripping.

---

## Dynamic Library Dependencies (15 total)

- CoreFoundation, CoreServices, SystemConfiguration (system)
- Cocoa, AppKit, Carbon, Foundation (UI/app frameworks)
- IOKit, CoreVideo, CoreGraphics, CoreText (hardware/graphics)
- OpenGL (graphics API)
- libc++, libSystem, libobjc (runtime)

**Note**: re2 was previously the only third-party dynamic dependency (`libre2.11.dylib`). It has been switched to static linking (built from re2 2023-03-01 without abseil dependency), making the binary fully self-contained with no third-party `.dylib` dependencies.

---

## Project Code Contributions (5.5 MB in binary, 23.7 MB .o debug)

292 object files compiled into `lambda.exe`, categorized by module:

| Module | .o Size (Debug) | Files | Top Contributors |
|--------|-----------------|-------|-----------------|
| **Input parsers & markup** | 4.3 MB | 60 | block_list (250K), block_quote (236K), block_table (229K) |
| **Radiant: CSS layout** | 3.4 MB | 21 | grid_sizing (464K), layout_grid (354K), layout_table (312K) |
| **TeX/LaTeX engine** | 3.1 MB | 34 | tex_document_model (393K), tex_math_ast_builder (214K) |
| **Misc/lib** | 2.6 MB | 62 | resolve_css_style (424K), resolve_htm_style (129K) |
| **Lambda core runtime** | 1.6 MB | 18 | build_ast (294K), transpile (287K), lambda-eval (168K) |
| **Output formatters** | 1.5 MB | 23 | format-md (198K), format-textile (96K) |
| **Radiant: rendering** | 1.4 MB | 12 | render (221K), render_svg_inline (184K) |
| **CLI/commands** | 767 KB | 6 | cmd_layout (327K), main (191K) |
| **HTML5 parser** | 731 KB | 4 | html5_tokenizer (393K), html5_tree_builder (180K) |
| **CSS engine** | 722 KB | 9 | css_properties (116K), css_parser (114K) |
| **GUI/window/events** | 693 KB | 5 | event (175K), state_store (150K) |
| **Radiant: DOM/view** | 616 KB | 4 | dom_element (214K), view_pool (205K) |
| **Radiant: fonts** | 506 KB | 5 | font_face (199K), font (105K) |
| **WebDriver (testing)** | 459 KB | 5 | webdriver_server (154K), webdriver_actions (105K) |
| **Validator** | 416 KB | 7 | doc_validator (85K), validate (84K) |
| **Data builders** | 346 KB | 6 | mark_editor (105K), mark_builder (103K) |
| **PDF engine** | 264 KB | 3 | pdf_to_view (200K) |
| **Graph utilities** | 170 KB | 4 | graph_to_svg (99K) |
| **Regex (re2)** | 116 KB | 1 | re2_wrapper (116K) |
| **Resource loaders** | 107 KB | 1 | resource_loaders (107K) |
| **HTTP server** | 67 KB | 2 | server (45K) |

---

## Largest Symbols in Debug Binary

| Size | Symbol | Source |
|------|--------|--------|
| 1.1 MB | `ts_parse_table` (Lambda) | tree-sitter-lambda |
| 508 KB | `Curl_parseX509.defaultVersion` | curl/mbedTLS |
| 270 KB | `ts_small_parse_table` (Lambda) | tree-sitter-lambda |
| 195 KB | `utf8proc_properties` | utf8proc |
| 165 KB | `ts_parse_table` (JS) | tree-sitter-javascript |
| 138 KB | `ts_lex` (Lambda) | tree-sitter-lambda |
| 120 KB | `kBrotliDictionaryData` | brotli |
| 106 KB | `small_prime_gaps` | mpdec |
| 91 KB | `utf8proc_stage2table` | utf8proc |
| 78 KB | `png_formatted_warning.valid_parameters` | libpng |
| 76 KB | `resolve_css_property` | project code |
| 69 KB | `ts_parse_actions` (Lambda) | tree-sitter-lambda |
| 65 KB | `GCC_except_table8` | GCC runtime |
| 55 KB | `ft_adobe_glyph_list` | freetype |
| 53 KB | `generate_func_code` | MIR JIT |
| 52 KB | `encode_one_block` | turbojpeg |
| 48 KB | `ts_lex` (JS) | tree-sitter-javascript |
| 37 KB | `g_memtrack` | project code |
| 33 KB | `lambda_lambda_h` | MIR (embedded lambda.h) |
| 33 KB | `named_entities` | HTML entities table |

---

## Optimization Recommendations (from 7.9 MB release baseline)

### 🟢 Phase 1: Build Config Improvements — No Code Changes (~0.5-1.5 MB savings)

#### 1. ~~Switch to release build~~ ✅ Done
- Release build reduced 12.8 MB → 7.9 MB (38% reduction)
- Current flags: `-O2 -flto=thin -ffunction-sections -fdata-sections -fvisibility=hidden -Wl,-dead_strip -Wl,-x`

#### 2. Rebuild libmir.a with `-Oz` (~272 KB savings) ✅ Tested
- MIR contributes ~360 KB total (c2mir: 180 KB, mir-gen: 112 KB, mir-core: 68 KB)
- The pre-built `/usr/local/lib/libmir.a` was compiled with `-O3` — switching to `-Oz` shrinks code
- **Action**: Rebuild MIR from source with `COPTFLAGS="-Oz -DNDEBUG"`
- LTO on MIR is counterproductive (enables cross-TU inlining that *grows* code)
- **Result**: 272 KB savings (3.4%), all 223/223 tests pass

#### 3. Upgrade from ThinLTO to full LTO
- Current: `-flto=thin` (fast link, per-module optimization)
- Full LTO (`-flto`) enables whole-program optimization across all TUs
- Expected: 5-10% additional reduction (~400-800 KB)
- **Trade-off**: Slower link time (~2-3x), acceptable for release-only builds
- **Action**: Change `-flto=thin` to `-flto` in premake5.mac.lua release config

#### 4. Remove `-fno-omit-frame-pointer` in release
- Currently set globally including release builds
- Costs ~1-2% code size (extra push/pop/mov per function prologue)
- Only needed for profiling or crash symbolication
- **Action**: Remove from release config or add `-fomit-frame-pointer` override

#### 5. Remove `-Wl,-force_load,libnghttp2.a`
- Forces the **entire** nghttp2 library into the binary even if only a few symbols are used
- Without force_load, the linker would only pull in referenced object files
- **Action**: Remove the `-Wl,-force_load` prefix; link nghttp2 normally

#### 6. Try `-Oz` instead of `-O2` for cold-path code
- `-Oz` aggressively optimizes for size (no auto-vectorization, minimal inlining, prefer branches over tables)
- Per-file flags via `PERFILE_FLAGS`: use `-O2` for hot paths, `-Oz` for cold code
- Hot paths: `lambda-eval.cpp`, `transpile.cpp`, `transpile-mir.cpp`, `parse.c`, `build_ast.cpp`, `mir.c`
- Cold paths: formatters, validators, input parsers, WebDriver, graph utilities
- Expected: ~10-20% savings on cold code modules

---

### 🟡 Phase 2: Architecture Changes — Moderate Effort (~2-3 MB savings)

#### 7. Make Radiant/GUI a separate binary (~1.5-2.5 MB savings)
- In release, Radiant-only libraries (GLFW 48 KB, ThorVG est. ~200 KB, turbojpeg ~240 KB, libpng 73 KB, freetype 87 KB in binary) total **~650 KB of in-binary contribution**
- Plus project code for layout/rendering/DOM/fonts/GUI estimated at ~1 MB after LTO
- Build two variants:
  - `lambda.exe` — CLI-only (scripting, format conversion, validation)
  - `lambda-view.exe` — Full build with Radiant layout/rendering engine
- **Action**: Use compile-time `#ifdef RADIANT_ENABLED` or split `source_dirs` in build config

#### 8. Shrink tree-sitter grammars (~200-350 KB savings)
- All tree-sitter grammars combined: **~346 KB** in release (down from ~2.4 MB in debug thanks to dead-strip)
- Options:
  - **Lazy-load grammars**: Load JS/LaTeX/LaTeX-math parsers on demand via `dlopen()` (~250 KB savings)
  - **Remove tree-sitter-javascript** (~47 KB in release) if JS parsing isn't essential for CLI
  - **Simplify Lambda grammar** — reduce states/conflicts in `grammar.js`

#### 9. Remove WebDriver from production binary
- WebDriver code (5 files) is solely for automated browser testing
- In release, estimated at ~50-100 KB after LTO
- **Action**: Add to `exclude_source_files` for production builds

---

### 🔵 Phase 3: Library Optimization (~0.5-1.5 MB savings)

#### 10. Replace turbojpeg with lighter JPEG library (~1 MB savings)
- turbojpeg contributes **1.1 MB** in the release binary — second largest after MIR
- `jpeg_aritab` (859 KB lookup table) is always linked even for basic decode-only usage
- Options:
  - **stb_image.h** — single-header, decode-only, ~20 KB compiled. No encode support
  - **libjpeg (not turbo)** — smaller, without arithmetic coding tables
  - **Build turbojpeg minimally** — disable arithmetic coding (`--without-arith-enc --without-arith-dec`) to eliminate the 859 KB table
- If only JPEG *decoding* for image display is needed, stb_image is the smallest option

#### 11. Slim down the HTTP/TLS stack (~300-500 KB savings)
- mbedTLS contributes ~500 KB in release binary
- `psa_to_ssl_errors` table alone is 119 KB, `mbedtls_cipher_base_lookup_table` is 20 KB
- Options:
  - Build mbedTLS with minimal config (TLS 1.2/1.3 + AES-GCM + SHA-256 only)
  - Build curl with `--disable-verbose --disable-manual --disable-ldap --disable-rtsp`
  - Make networking optional — compile curl/mbedTLS/libevent conditionally

#### 12. Trim FreeType TrueType interpreter (~250 KB savings)
- `TT_RunIns` (TrueType bytecode interpreter) is **254 KB** — the largest FreeType symbol
- If only basic glyph rendering is needed (no hinting), disable with `FT_CONFIG_OPTION_NO_TT_INTERPRETER`
- Also `ft_adobe_glyph_list` could be stripped if Adobe glyph name lookups aren't needed

---

### 🟣 Phase 4: Advanced Optimization

#### 13. Per-file `-Oz` for cold code paths
- The build system already supports `PERFILE_FLAGS` — use this to apply:
  - `-O2` for hot paths: `lambda-eval.cpp`, `transpile.cpp`, `transpile-mir.cpp`, `parse.c`, `build_ast.cpp`
  - `-Oz` (minimize size) for cold paths: all formatters, validators, input parsers, WebDriver, graph utilities
- Typically saves 10-20% on cold code with negligible performance impact

#### 14. Compress large read-only data tables
- Several large const arrays could be compressed and decompressed at startup:
  - Brotli context table (130 KB)
  - mbedTLS error/cipher tables (~139 KB)
  - Tree-sitter grammar data (if lazy-loaded)
- **Trade-off**: ~10-50ms startup cost for ~200-400 KB savings

---

## Experimental Results: MIR Rebuild

MIR (c2mir + mir-gen + mir.c) was rebuilt from source with various optimization flags. The original MIR was compiled with `-O3 -DNDEBUG` (default `GNUmakefile` settings), no LTO.

### Test Matrix

| MIR Build Config | lambda.exe (stripped) | `__text` section | Δ vs Original |
|------------------|----------------------|------------------|---------------|
| **Original** (`-O3`, no LTO) | 8,242,120 B (7.86 MB) | 5,282,932 B | — |
| `-Os` | 8,093,832 B (7.72 MB) | 5,174,440 B | **−145 KB (−1.8%)** |
| `-Os -flto=thin` | 8,090,600 B (7.72 MB) | 5,175,928 B | **−148 KB (−1.8%)** |
| `-O2 -flto=thin` | 8,255,240 B (7.87 MB) | 5,334,196 B | +13 KB (+0.2%) ❌ |
| **`-Oz`** ★ | **7,963,912 B (7.59 MB)** | **5,043,348 B** | **−272 KB (−3.4%)** ✅ |

### Key Findings

1. **`-Oz` is the clear winner**: saves 272 KB (3.4%) with zero correctness issues (223/223 baseline tests pass).
2. **LTO on MIR is counterproductive**: Adding `-flto=thin` to MIR enables cross-translation-unit inlining, which *grows* code. `-O2 -flto=thin` is actually *larger* than the original because the linker inlines more MIR internals across `mir.c`, `mir-gen.c`, and `c2mir.c`.
3. **`-Os` vs `-Oz`**: `-Oz` saves 124 KB more than `-Os` by more aggressively avoiding inline expansions and preferring function calls over inline code sequences.
4. **MIR `.a` file size is misleading**: The LTO-enabled `.a` is *larger* (4.1 MB vs 3.5 MB) due to embedded LLVM IR bitcode, but the *final linked* binary can be smaller because the linker can dead-strip across TU boundaries.

### Segment Impact (MIR -Oz vs Original)

| Segment | Original | MIR -Oz | Delta |
|---------|----------|---------|-------|
| `__TEXT` | 7,665,440 | 7,520,256 | −145 KB |
| `__text` (code only) | 5,282,932 | 5,043,348 | −234 KB |
| `__DATA_CONST` | 237,088 | 245,760 | +8 KB |
| `__DATA` | 84,320 | 212,992 | +126 KB |
| `__LINKEDIT` | 255,232 | 245,760 | −9 KB |

The `__DATA` increase (+126 KB) is a characteristic of `-Oz`: the compiler replaces inline code sequences with data table lookups, trading code size for data. Net result is still a 272 KB overall savings.

### How to Apply

```bash
# In setup-mac-deps.sh, change build_mir_for_mac() to:
cd "$MIR_DIR"
make clean
make -j$(sysctl -n hw.ncpu) COPTFLAGS="-Oz -DNDEBUG"
```

---

## Experimental Results: Removing c2mir (Direct MIR Transpilation)

The current JIT pipeline is: **Lambda AST → transpile.cpp (C code) → c2mir → MIR IR → mir-gen → native**.
A direct MIR backend (`transpile-mir.cpp`, 496 lines, already exists) would skip c2mir entirely: **Lambda AST → transpile-mir.cpp → MIR IR → mir-gen → native**.

To measure the size impact, `libmir.a` was rebuilt with c2mir replaced by a no-op stub (mir.o + mir-gen.o + c2mir_stub.o).

### Results

| Build | Binary (stripped) | `__text` | Δ vs c2mir |
|-------|-------------------|----------|------------|
| MIR `-Oz` (with c2mir) | 7,963,912 B (7.59 MB) | 5,043,348 B | — |
| MIR `-Oz` (c2mir stubbed) | 7,779,368 B (7.42 MB) | 4,894,760 B | **−180 KB (−2.3%)** |

### Section-Level Impact

| Section | With c2mir | Without c2mir | Delta |
|---------|-----------|---------------|-------|
| `__text` (executable code) | 5,043,348 | 4,894,760 | −145 KB |
| `__cstring` (string literals) | 434,477 | 419,376 | −15 KB |
| `__const` (const data) | 1,847,574 | 1,845,030 | −2.5 KB |
| `__DATA __const` | 226,968 | 225,528 | −1.4 KB |
| **Total (unstripped)** | **8,183,800** | **7,901,032** | **−276 KB** |

### Key Findings

1. **c2mir contributes only ~180 KB** (2.3% of binary) — not the 3.8 MB that symbol analysis suggested.
2. The 3.8 MB figure was a **measurement artifact**: `_c2mir_compile` was the last code symbol before the `__const` section, so 3.5 MB of global const data from all libraries was misattributed to it.
3. **Total MIR (all 3 objects) is 396 KB (5.1%)** — verified by stubbing out all of MIR and relinking. MIR is **not** 50% of the binary.
4. **Direct MIR transpilation saves ~180 KB** — a modest but real reduction.
5. The bigger benefit of direct MIR transpilation is **architecture** (simpler pipeline, faster JIT startup, no C intermediary) rather than binary size.
6. Combined with `-Oz` MIR: total savings from original is **462 KB (5.6%)**.

---

## Expected Results Summary (from 7.9 MB release baseline)

| Strategy | Estimated Saving | Effort | Performance Impact | Status |
|----------|-----------------|--------|-------------------|--------|
| ~~Release build + strip~~ | ~~4.9 MB~~ | ✅ Done | **Improved** | ✅ Done |
| **Rebuild MIR with `-Oz`** | **272 KB (3.4%)** | Low (rebuild dep) | None (verified) | ✅ **Tested** |
| **Remove c2mir (direct MIR)** | **180 KB (2.3%)** | Medium (finish transpile-mir.cpp) | Faster JIT startup | ✅ **Measured** |
| ~~Rebuild MIR with LTO~~ | ~~0.5-1 MB~~ | — | — | ❌ **Counterproductive** |
| Full LTO (`-flto`) | 0.4-0.8 MB | Low (config) | Slightly improves | ⬜ Not tested |
| Split Radiant/turbojpeg out | 1.5-2.5 MB | Medium (restructure) | None (CLI unaffected) | ⬜ |
| Replace/trim turbojpeg | ~1 MB | Medium (lib swap) | None | ⬜ |
| Slim mbedTLS | ~0.3-0.5 MB | Medium (rebuild) | None | ⬜ |
| Trim FreeType hinting | ~0.25 MB | Low (rebuild flag) | Minor (no TT hinting) | ⬜ |
| Remove WebDriver | ~0.05-0.1 MB | Low | None | ⬜ |
| Remove `-fno-omit-frame-pointer` | ~0.1 MB | Trivial | None | ⬜ |
| Remove `-Wl,-force_load` nghttp2 | ~0.02 MB | Trivial | None | ⬜ |
| Per-file `-Oz` cold code | 0.2-0.5 MB | Low (config) | Negligible | ⬜ |

### Projected Binary Sizes (revised with experimental data)

| Target | Estimated Size | Reduction from 7.9 MB |
|--------|---------------|----------------------|
| **Current release** | 7.86 MB | — |
| **+ MIR -Oz** (tested) | **7.59 MB** | **3.4%** |
| **+ flags cleanup** | ~7.3-7.5 MB | ~5-7% |
| **CLI-only (no Radiant/GUI)** | ~5.0-5.5 MB | ~30-37% |
| **CLI-only + trimmed libs** | ~4.0-4.5 MB | ~43-49% |
| **Aggressive (all phases)** | ~3.5-4.0 MB | ~49-56% |

---

## Analysis Methodology

- `size` command for Mach-O segment analysis
- `otool -L` for dynamic library dependencies
- `otool -l` for segment/section detail
- `nm -n` for symbol analysis (consecutive address differencing — ⚠️ unreliable for last symbol in a section)
- **Stub-linking** for accurate library contribution measurement (replace library with no-op stubs and compare section sizes)
- Object file measurement via `find build/obj/lambda -name "*.o"`
- Build configuration parsed from `build_lambda_config.json` and `build/premake/lambda.make`
- Static library file sizes measured directly
- Symbol stripping tested via `strip -x` on copy
- Both debug (`-O0 -g`) and release (`-O2 -flto=thin -Wl,-dead_strip`) builds analyzed

**Analysis tools**: `temp/analyze_lambda.py`, `temp/analyze_lambda_release.py`
