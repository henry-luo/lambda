# Lambda Binary Size Optimization Proposal

## Implementation Status

### ✅ Implemented Optimizations (Verified Working)

The following optimizations have been implemented and tested:

#### Results

| Build | Size | Reduction |
|-------|------|-----------|
| Debug (before) | 7.9 MB | — |
| **Release (after)** | **5.7 MB** | **28%** |

#### 1. Dead Code Elimination
- **Compiler flags**: `-ffunction-sections`, `-fdata-sections`
- **Linker flags**:
  - macOS: `-Wl,-dead_strip`, `-Wl,-x`
  - Linux: `-Wl,--gc-sections`, `-Wl,--strip-all`
  - Windows: `-Wl,--gc-sections`, `-s`

#### 2. Log Stripping in Release Builds
- `log_debug()`, `log_info()` → compiled to no-ops via `NDEBUG` macro
- `clog_debug()`, `clog_info()` → compiled to no-ops via `NDEBUG` macro
- Variadic versions also stripped: `log_vdebug()`, `log_vinfo()`, etc.
- Implementation uses `LOG_IMPL` guard to protect function definitions in `log.c`

Implementation in `lib/log.h`:
```c
#if defined(NDEBUG) && !defined(LOG_IMPL)
    #define log_info(...) ((void)0)
    #define log_debug(...) ((void)0)
#else
    int log_info(const char *format, ...);
    int log_debug(const char *format, ...);
#endif
```

#### 3. Symbol Visibility Control
- **Compiler flags**: `-fvisibility=hidden`, `-fvisibility-inlines-hidden`
- Only explicitly exported symbols are visible

#### 4. Debug Symbol Stripping
- **Linker flags**: `-s` (strip during link)
- **Post-build**: `strip -x` on macOS, `strip` on Linux/Windows

#### 5. Link-Time Optimization (LTO)
- **Flags**: `-flto` for both compiler and linker

### Usage

```bash
# Build optimized release binary
make build-release

# Output shows applied optimizations
# Final binary: lambda.exe (5.7 MB)
```

---

## Current State

| Metric | Debug Build | Release Build |
|--------|-------------|---------------|
| Binary | `lambda.exe` | `lambda.exe` |
| Size | 7.9 MB | **5.7 MB** |
| Optimization | `-g -O2` | `-O3 -flto` |
| Exported Functions | ~5,945 | ~1,895 |
| Logging | Full | `log_debug`/`log_info` stripped |

## Size Breakdown by Component

### Symbol Distribution

| Component | Symbol Count | Notes |
|-----------|-------------|-------|
| C functions (core runtime) | ~10,755 | Lambda engine, parsers, formatters |
| mbedTLS (crypto/TLS) | ~1,278 | Required for HTTPS in curl |
| curl + nghttp2 | ~1,168 | HTTP client |
| Image libs (jpeg, png, gif, freetype) | ~1,015 | Image/font rendering |
| ThorVG | ~539 | SVG rendering |
| C++ STL instantiations | ~490 | Template bloat |
| MIR JIT | ~179 | JIT compilation |

### Static Libraries Linked

| Library | Size | Purpose |
|---------|------|---------|
| libmir.a | **3.3 MB** | JIT compiler (largest!) |
| libcurl.a | 764 KB | HTTP client |
| libfreetype.a | 767 KB | Font rendering |
| libturbojpeg.a | 747 KB | JPEG codec |
| libmbedcrypto.a | 715 KB | Cryptographic primitives |
| libthorvg.a | 608 KB | SVG rendering |
| libmbedtls.a | 433 KB | TLS protocol |
| libpng.a | 245 KB | PNG codec |
| libnghttp2.a | 242 KB | HTTP/2 support |
| libtree-sitter.a | 231 KB | Parser framework |
| libmpdec.a | 194 KB | Decimal arithmetic |

### Largest Object Files

| Object File | Size | Source |
|-------------|------|--------|
| format_latex_html_v2.o | 3.0 MB | LaTeX→HTML converter |
| latex_packages.o | 1.9 MB | LaTeX package support |
| html_generator.o | 1.0 MB | HTML generation |
| latex_generator.o | 925 KB | LaTeX generation |
| latex_docclass.o | 674 KB | Document classes |
| cmd_layout.o | 670 KB | Layout command |

---

## Optimization Recommendations

### 1. Enable Release Build with LTO

**Estimated Savings: 2-3 MB**

The build config already defines a release profile. Use it:

```bash
make build-release
```

Release config flags:
- `-O3` (aggressive optimization)
- `-flto` (Link-Time Optimization)
- `-s` (strip symbols during linking)

### 2. Add Dead Code Elimination

**Estimated Savings: 0.3-0.5 MB**

Add to `build_lambda_config.json` flags:

```json
{
    "flags": [
        "ffunction-sections",
        "fdata-sections"
    ],
    "linker_flags": [
        "Wl,-dead_strip"
    ]
}
```

For Linux builds, use `-Wl,--gc-sections` instead.

### 3. Add Symbol Visibility Control

**Estimated Savings: 0.2-0.3 MB**

Add compiler flags:
```json
{
    "flags": [
        "fvisibility=hidden",
        "fvisibility-inlines-hidden"
    ]
}
```

Mark public API explicitly:
```cpp
#define LAMBDA_API __attribute__((visibility("default")))
#define LAMBDA_INTERNAL __attribute__((visibility("hidden")))
```

### 4. Strip Debug Symbols in Release

**Estimated Savings: 0.5-0.8 MB**

For production distribution:
```bash
strip lambda.exe          # Aggressive strip
# or
strip -x lambda.exe       # Keep symbol table, remove debug info
```

### 5. Create Build Variants

#### 5.1 Lambda Lite (CLI-only)

**Estimated Savings: 2+ MB**

Exclude Radiant engine dependencies:
- ThorVG (SVG rendering)
- FreeType (font rasterization)
- GLFW (windowing)
- Image codecs (turbojpeg, libpng, libgif)

Use case: Command-line data processing, no GUI/rendering.

#### 5.2 Lambda Core (No JIT)

**Estimated Savings: 1-2 MB**

Make MIR JIT optional via conditional compilation:
```cpp
#ifdef ENABLE_JIT
#include "transpile-mir.cpp"
#endif
```

Use case: Interpreter-only mode for embedded/minimal deployments.

#### 5.3 Lambda Minimal (No Network)

**Estimated Savings: 1.5 MB**

Exclude HTTP/TLS stack:
- libcurl
- mbedTLS (mbedtls, mbedx509, mbedcrypto)
- nghttp2

Use case: Offline processing, no `fetch()` support.

### 6. Externalize Large Data Tables

**Estimated Savings: 1-2 MB**

The LaTeX modules contain large static data:
- `latex_packages.cpp` (634 lines but 1.9 MB object)
- `format_latex_html_v2.cpp` (10,475 lines, 3 MB object)

Options:
1. Load package definitions from external JSON/binary at runtime
2. Generate code with `__attribute__((section))` for lazy loading
3. Use memory-mapped files for large lookup tables

### 7. Reduce C++ Template Bloat

**Estimated Savings: 0.1-0.3 MB**

Current: ~490 `std::__1::` template instantiations

Mitigation strategies:
- Use `extern template` for common instantiations
- Prefer `std::string_view` over `std::string` in APIs
- Use type-erased containers where appropriate
- Consolidate `std::map<std::string, T>` instantiations

### 8. Optional: UPX Compression

**Estimated Savings: 50-70% of final size**

For distribution:
```bash
upx --best lambda.exe
```

Trade-offs:
- Slower startup (decompression)
- May trigger antivirus false positives on Windows
- Not recommended for development builds

---

## Implementation Plan

### Phase 1: Quick Wins ✅ COMPLETED

| Task | Effort | Savings | Status |
|------|--------|---------|--------|
| Enable release build | Low | 2.2 MB | ✅ Done |
| Dead code elimination | Low | included | ✅ Done |
| Symbol visibility | Low | included | ✅ Done |
| Strip debug symbols | Low | included | ✅ Done |
| Strip logging calls | Low | included | ✅ Done |

**Result: 5.7 MB release binary (28% reduction from 7.9 MB)**

### Phase 2: Build Variants (Config Changes)

| Task | Effort | Savings |
|------|--------|---------|
| Create `lambda-lite` target (no Radiant) | Medium | 2+ MB |
| Create `lambda-core` target (no JIT) | Medium | 1-2 MB |
| Conditional compilation guards | Medium | — |

**Expected Result: 2-3 MB lite variant**

### Phase 3: Code Refactoring (Larger Effort)

| Task | Effort | Savings |
|------|--------|---------|
| Externalize LaTeX data tables | High | 1-2 MB |
| Reduce template instantiations | High | 0.1-0.3 MB |
| Modularize input parsers | High | Variable |

---

## Summary: Achievable Targets

| Build Variant | Target Size | Status |
|---------------|-------------|--------|
| Debug | 7.9 MB | Current default |
| **Release (optimized)** | **5.7 MB** | ✅ **Implemented** |
| Lite (no Radiant) | 2-3 MB | Planned |
| Minimal (no JIT, no network) | 1.5-2 MB | Planned |
| UPX compressed | ~2 MB | Optional |

---

## Appendix: Build Configuration Changes

### Recommended `build_lambda_config.json` Updates

```json
{
    "platforms": {
        "release": {
            "flags": [
                "std=c++17",
                "DNDEBUG",
                "O3",
                "flto",
                "ffunction-sections",
                "fdata-sections",
                "fvisibility=hidden",
                "fvisibility-inlines-hidden"
            ],
            "linker_flags": [
                "O3",
                "flto",
                "s",
                "Wl,-dead_strip"
            ]
        }
    }
}
```

### New Makefile Targets

```makefile
# Optimized release build
build-release:
    $(MAKE) -C build/premake config=release_native lambda -j$(JOBS)
    strip lambda_release.exe

# Lite build without Radiant
build-lite:
    $(MAKE) -C build/premake config=release_native lambda-lite -j$(JOBS)

# Size report
size-report:
    @echo "Binary size breakdown:"
    @size lambda.exe
    @nm lambda.exe | c++filt | grep -E " [Tt] " | wc -l
```
