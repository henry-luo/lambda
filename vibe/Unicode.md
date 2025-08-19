# Lambda Unicode Migration: ICU → utf8proc

## Migration Overview

**Current State**: Lambda uses ICU (International Components for Unicode) for Unicode support with a 4-tier system ranging from ASCII-only to full ICU (8-12MB overhead).

**Target State**: Replace ICU with utf8proc for significantly reduced size (~350KB vs 8MB) while maintaining core Unicode functionality.

## Why utf8proc?

### Size Comparison
- **Current ICU Compact**: ~8MB (stripped down from 25-30MB)
- **utf8proc**: ~350KB static library (~400KB dynamic)
- **Size Reduction**: ~96% smaller (23x reduction)

### Feature Comparison

| Feature | ICU (Current) | utf8proc | Migration Impact |
|---------|---------------|----------|------------------|
| **String Normalization** | ✅ NFC/NFD/NFKC/NFKD | ✅ NFC/NFD/NFKC/NFKD | ✅ **Equivalent** |
| **Case Folding** | ✅ Locale-aware | ✅ Unicode standard | ⚠️ **Simplified** (no locale-specific) |
| **String Collation** | ✅ Full locale collation | ❌ **Not provided** | ⚠️ **Requires implementation** |
| **Character Classification** | ✅ Full categories | ✅ Basic categories | ✅ **Sufficient** |
| **Grapheme Boundaries** | ✅ Via break iteration | ✅ Native support | ✅ **Better API** |
| **Memory Footprint** | 8MB | 350KB | ✅ **Massive improvement** |
| **Cross-platform** | ✅ Good | ✅ Excellent | ✅ **No change** |
| **License** | ICU License | MIT | ✅ **More permissive** |

### Key Differences

**Lost Features:**
1. **Locale-specific collation** - ICU provides locale-aware string comparison
2. **Locale-specific case mapping** - ICU handles Turkish İ/ı correctly per locale
3. **Break iteration** - ICU's word/sentence boundary detection (utf8proc only does graphemes)

**Gained Benefits:**
1. **Size reduction**: 96% smaller memory footprint
2. **Simpler API**: More straightforward Unicode operations
3. **Better maintenance**: Active development, Julia backing, regular Unicode updates
4. **No complex build**: Single library, no data filtering needed

## Current ICU Usage Analysis

### Core ICU Functions Used
```cpp
// From lambda/unicode_string.cpp
ucol_open()                    // String collation
ucol_strcollUTF8()            // UTF-8 string comparison
ucol_setAttribute()           // Collation configuration
unorm2_getNFCInstance()       // Normalization
u_init() / u_cleanup()        // ICU initialization
```

### Usage Patterns
1. **String Equality**: `equal_comp_unicode()` - ICU collation comparison
2. **String Ordering**: `string_compare_unicode()` - ICU collation for `<`, `>`, `<=`, `>=`
3. **Normalization**: Currently NFC normalization only
4. **Initialization**: Global ICU collator instances
5. **ASCII Fast Path**: Performance optimization for ASCII-only strings

## Migration Strategy

### Phase 1: Infrastructure Setup (Week 1)
**Goal**: Prepare build system and basic utf8proc integration

1. **Add utf8proc Dependency**
   - Update `build_lambda_config.json` with utf8proc library
   - Add platform-specific utf8proc paths (macOS/Linux/Windows)
   - Create utf8proc build scripts for cross-compilation

2. **Configuration Updates**
   - Extend `unicode_config.h` with utf8proc support levels
   - Add `LAMBDA_UTF8PROC_SUPPORT` flag
   - Keep ICU flags for parallel development

3. **Initial Integration**
   - Create `lambda/utf8proc_string.h` and `.cpp`
   - Implement basic utf8proc initialization/cleanup
   - Add utf8proc to test builds

### Phase 2: Core String Operations (Week 2)
**Goal**: Implement equivalent utf8proc-based string operations

1. **Normalization Functions**
   ```cpp
   // New utf8proc-based functions
   utf8proc_uint8_t* normalize_utf8proc_nfc(const char* str, int len);
   utf8proc_uint8_t* normalize_utf8proc_nfd(const char* str, int len);
   ```

2. **Case-Insensitive Comparison**
   ```cpp
   // Using utf8proc casefold + comparison
   int compare_utf8proc_casefold(const char* str1, int len1, 
                                const char* str2, int len2);
   ```

3. **Basic String Comparison**
   ```cpp
   // Replacement for ICU collation (simplified)
   UnicodeCompareResult string_compare_utf8proc(const char* str1, int len1,
                                               const char* str2, int len2);
   ```

### Phase 3: Collation Implementation (Week 3)
**Goal**: Replace ICU collation with custom implementation

**Challenge**: utf8proc doesn't provide collation, need custom solution.

**Approach**: Implement simplified Unicode collation
1. **Normalization-based comparison**: NFC normalize both strings, then compare
2. **Case-folding option**: Use utf8proc casefold for case-insensitive comparison
3. **ASCII fast path**: Keep existing optimization
4. **Fallback strategy**: Byte comparison for complex cases

```cpp
// New collation implementation
typedef enum {
    UTF8PROC_COLLATE_BINARY,     // Byte comparison (fastest)
    UTF8PROC_COLLATE_NORMALIZED, // NFC normalization + compare
    UTF8PROC_COLLATE_CASEFOLD,   // Case-insensitive comparison
} Utf8procCollateMode;

UnicodeCompareResult collate_utf8proc(const char* str1, int len1,
                                     const char* str2, int len2,
                                     Utf8procCollateMode mode);
```

### Phase 4: Function Migration (Week 4)
**Goal**: Replace ICU functions with utf8proc equivalents

1. **Update Comparison Functions**
   - Modify `equal_comp_unicode()` to use utf8proc
   - Update `fn_eq_unicode()`, `fn_ne_unicode()`, etc.
   - Implement `fn_lt_unicode()`, `fn_gt_unicode()` with new collation

2. **Configuration Integration**
   - Add utf8proc support to Unicode level system
   - New level: `LAMBDA_UNICODE_UTF8PROC = 2`
   - Update build configuration

### Phase 5: Testing & Validation (Week 5)
**Goal**: Ensure equivalent behavior and performance

1. **Functionality Tests**
   - Unicode normalization equivalence tests
   - String comparison behavior verification
   - Edge case handling (malformed UTF-8, etc.)

2. **Performance Testing**
   - Compare utf8proc vs ICU performance
   - Measure memory usage reduction
   - ASCII fast path optimization verification

3. **Cross-platform Testing**
   - macOS, Linux, Windows builds
   - Static vs dynamic linking tests

### Phase 6: Migration & Cleanup (Week 6)
**Goal**: Complete migration and remove ICU dependency

1. **Default Configuration Change**
   - Change default from ICU to utf8proc
   - Update documentation
   - Migration guide for users

2. **ICU Removal**
   - Remove ICU build scripts and dependencies
   - Clean up ICU-specific code paths
   - Update build configurations

3. **Documentation Updates**
   - Update Unicode support documentation
   - New size/performance metrics
   - Migration notes for breaking changes

## Technical Implementation Details

### New Unicode Configuration Levels
```cpp
// Updated lambda/unicode_config.h
#define LAMBDA_UNICODE_NONE        0  // ASCII-only (~0KB)
#define LAMBDA_UNICODE_MINIMAL     1  // Basic UTF-8 support (~100KB)
#define LAMBDA_UNICODE_UTF8PROC    2  // utf8proc support (~350KB)
#define LAMBDA_UNICODE_ICU_COMPAT  3  // ICU compatibility (deprecated)
```

### Build Configuration Changes
```json
// build_lambda_config.json
{
    "name": "utf8proc",
    "include": "/opt/homebrew/include",
    "lib": "/opt/homebrew/lib/libutf8proc.a",
    "link": "static",
    "version": "2.11.0",
    "platforms": {
        "macos": "/opt/homebrew/lib/libutf8proc.a",
        "linux": "/usr/local/lib/libutf8proc.a", 
        "windows": "windows-deps/lib/libutf8proc.a"
    }
}
```

### API Translation Map

| ICU Function | utf8proc Equivalent | Notes |
|--------------|-------------------|-------|
| `ucol_strcollUTF8()` | `custom_collate_utf8proc()` | Custom implementation |
| `unorm2_normalize()` | `utf8proc_NFC()` / `utf8proc_NFD()` | Direct equivalent |
| `ucol_open()` | `utf8proc_version()` | Initialization check |
| `u_init()` | *None required* | utf8proc is stateless |
| `ucol_getAttribute()` | *Configuration flags* | Compile-time options |

### Memory Management
- **ICU**: Complex initialization, global collators, cleanup required
- **utf8proc**: Stateless, no initialization needed, simple memory management
- **Migration**: Remove global state, simplify init/cleanup

## Breaking Changes & Mitigation

### 1. Collation Behavior Changes
**Issue**: utf8proc doesn't provide locale-specific collation
**Impact**: String ordering may differ for non-ASCII text
**Mitigation**: 
- Document behavior changes
- Provide compatibility mode with ICU-like behavior
- Add warning for locale-sensitive applications

### 2. Case-Folding Differences  
**Issue**: ICU locale-aware vs utf8proc Unicode standard
**Impact**: Turkish İ/ı handling differences
**Mitigation**:
- Document specific differences
- Add locale-agnostic mode as default
- Provide opt-in locale-specific handling

### 3. Performance Characteristics
**Issue**: Different performance profiles
**Impact**: Some operations faster, some slower
**Mitigation**:
- Comprehensive benchmarking
- Optimize critical paths
- Maintain ASCII fast path

## Success Metrics

1. **Size Reduction**: Target >90% reduction in Unicode library overhead
2. **Performance**: Maintain or improve performance for common operations
3. **Compatibility**: 95%+ test case compatibility with existing behavior
4. **Build Simplicity**: Eliminate complex ICU build process
5. **Cross-platform**: Support all current platforms (macOS, Linux, Windows)

## Risk Assessment

### High Risk
- **Collation behavior changes** - May break string ordering in applications
- **Locale-specific operations** - Applications relying on locale behavior

### Medium Risk  
- **Performance regressions** - Different optimization profiles
- **Cross-platform builds** - Need utf8proc on all platforms

### Low Risk
- **Basic Unicode operations** - utf8proc equivalents well-tested
- **Library size** - Guaranteed significant improvement

## Timeline Summary

| Phase | Duration | Milestone |
|-------|----------|-----------|
| 1. Infrastructure | Week 1 | utf8proc builds successfully |
| 2. Core Operations | Week 2 | Basic string functions working |
| 3. Collation | Week 3 | String comparison implemented |
| 4. Function Migration | Week 4 | All Unicode functions migrated |
| 5. Testing | Week 5 | Full test suite passes |
| 6. Migration Complete | Week 6 | ICU dependency removed |

**Total Duration**: 6 weeks
**Critical Path**: Collation implementation (Phase 3)
**Rollback Point**: End of Phase 4 (before ICU removal)

## Conclusion

The migration from ICU to utf8proc represents a strategic improvement in Lambda's Unicode support:

- **96% size reduction** (8MB → 350KB)
- **Simpler maintenance** and build process
- **Maintained functionality** for core Unicode operations
- **Some trade-offs** in locale-specific features

This migration aligns with Lambda's goals of being a lightweight, cross-platform scripting language while maintaining robust Unicode support for international text processing.
