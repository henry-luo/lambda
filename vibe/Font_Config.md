# Lambda FontConfig Replacement: Design & Implementation Plan

## Project Overview

Replace FontConfig dependency with a lightweight, cross-platform font discovery and matching system built specifically for Lambda Script. This eliminates external dependencies while providing consistent font behavior across macOS, Linux, and Windows.

### Goals
- **Zero External Dependencies**: Remove FontConfig, Expat, and related libraries
- **Cross-Platform Consistency**: Identical font matching behavior on all platforms
- **Performance**: Faster startup and font discovery through optimized caching
- **Memory Efficiency**: Integration with Lambda's arena/pool memory management
- **Maintainability**: Simplified build system without complex dependency chains

### Current State Analysis

**Existing Dependencies:**
```json
{
  "fontconfig": "/opt/homebrew/lib/libfontconfig_minimal.a",
  "expat": "/opt/homebrew/opt/expat/lib/libexpat.a",
  "freetype": "/opt/homebrew/opt/freetype/lib/libfreetype.a"
}
```

**Current Usage Points:**
- `radiant/font.cpp`: `load_font_path()` using FontConfig pattern matching
- `radiant/ui_context.cpp`: FontConfig initialization and cleanup
- `build_lambda_config.json`: Static linking of FontConfig libraries

## Architecture Design

### Core Data Structures

```c
// lib/font_config.h

typedef enum {
    FONT_FORMAT_TTF,
    FONT_FORMAT_OTF, 
    FONT_FORMAT_TTC,
    FONT_FORMAT_WOFF,
    FONT_FORMAT_WOFF2,
    FONT_FORMAT_UNKNOWN
} FontFormat;

typedef enum {
    FONT_STYLE_NORMAL,
    FONT_STYLE_ITALIC,
    FONT_STYLE_OBLIQUE
} FontStyle;

typedef struct FontUnicodeRange {
    uint32_t start_codepoint;
    uint32_t end_codepoint;
    struct FontUnicodeRange* next;
} FontUnicodeRange;

typedef struct FontEntry {
    // Basic metadata
    char* family_name;          // "Arial", "Times New Roman"
    char* subfamily_name;       // "Regular", "Bold", "Italic"
    char* postscript_name;      // "Arial-BoldMT"
    char* file_path;            // Full path to font file
    
    // Font properties
    int weight;                 // 100-900 (CSS weight scale)
    FontStyle style;            // normal/italic/oblique
    bool is_monospace;          // Fixed-width font
    FontFormat format;          // TTF, OTF, etc.
    
    // Unicode support
    FontUnicodeRange* unicode_ranges;  // Supported character ranges
    uint32_t unicode_coverage_hash;    // Quick coverage check
    
    // File metadata
    time_t file_mtime;          // For cache invalidation
    size_t file_size;           // File size for verification
    
    // Collection info (for .ttc files)
    int collection_index;       // Font index in collection
    bool is_collection;
    
} FontEntry;

typedef struct FontFamily {
    char* family_name;          // Primary family name
    ArrayList* aliases;         // Alternative family names
    ArrayList* fonts;           // Array of FontEntry*
    bool is_system_family;      // System vs user-installed
} FontFamily;

typedef struct FontDatabase {
    // Core storage
    HashMap* families;          // family_name -> FontFamily*
    HashMap* postscript_names;  // ps_name -> FontEntry*
    HashMap* file_paths;        // file_path -> FontEntry*
    ArrayList* all_fonts;       // All FontEntry* for iteration
    
    // Platform directories
    ArrayList* scan_directories; // Directories to scan
    
    // Cache metadata
    time_t last_scan;           // Last full scan timestamp
    char* cache_file_path;      // Persistent cache location
    bool cache_dirty;           // Needs cache write
    
    // Memory management
    MemPool* font_pool;         // Pool for FontEntry allocation
    Arena* string_arena;        // Arena for string storage
} FontDatabase;

typedef struct FontMatchCriteria {
    const char* family_name;    // Requested family
    int weight;                 // 100-900, -1 for any
    FontStyle style;            // normal/italic/oblique
    bool prefer_monospace;      // Prefer fixed-width fonts
    uint32_t required_codepoint; // Must support this codepoint (0 = any)
    const char* language;       // Language hint (ISO 639-1)
} FontMatchCriteria;

typedef struct FontMatchResult {
    FontEntry* font;            // Best matching font
    float match_score;          // 0.0-1.0 quality score
    bool exact_family_match;    // Family name matched exactly
    bool requires_synthesis;    // Needs bold/italic synthesis
    char* synthetic_style;      // "synthetic-bold", "synthetic-italic", NULL
} FontMatchResult;
```

### Module Interface

```c
// lib/font_config.h - Public API

// Database lifecycle
FontDatabase* font_database_create(MemPool* pool, Arena* arena);
void font_database_destroy(FontDatabase* db);
bool font_database_scan(FontDatabase* db);
bool font_database_load_cache(FontDatabase* db);
bool font_database_save_cache(FontDatabase* db);

// Font discovery
FontMatchResult font_database_find_best_match(FontDatabase* db, FontMatchCriteria* criteria);
ArrayList* font_database_find_all_matches(FontDatabase* db, const char* family_name);
FontEntry* font_database_get_by_postscript_name(FontDatabase* db, const char* ps_name);
FontEntry* font_database_get_by_file_path(FontDatabase* db, const char* file_path);

// Font metadata
bool font_supports_codepoint(FontEntry* font, uint32_t codepoint);
bool font_supports_language(FontEntry* font, const char* language);
ArrayList* font_get_available_families(FontDatabase* db);

// System integration
void font_add_scan_directory(FontDatabase* db, const char* directory);
bool font_is_file_changed(FontEntry* font);
void font_database_refresh_changed_files(FontDatabase* db);
```

## Implementation Phases

### Phase 1: Foundation & Basic Discovery (Week 1-2)

**Deliverables:**
- `lib/font_config.c` with core data structures
- Platform-specific font directory scanning
- Basic TTF/OTF metadata parsing
- Simple font database with in-memory storage

**Key Functions:**
```c
// Platform-specific implementations
static void scan_platform_font_directories(FontDatabase* db);
static bool parse_font_metadata(const char* file_path, FontEntry* entry);
static void add_system_font_paths(ArrayList* directories);

#ifdef __APPLE__
static void scan_macos_fonts(FontDatabase* db);
#elif defined(__linux__)
static void scan_linux_fonts(FontDatabase* db); 
#elif defined(_WIN32)
static void scan_windows_fonts(FontDatabase* db);
#endif
```

**Font Metadata Parser:**
```c
typedef struct TTF_Header {
    uint32_t scaler_type;
    uint16_t num_tables;
    // ... TTF/OTF header fields
} TTF_Header;

typedef struct TTF_Table_Directory {
    uint32_t tag;      // 'name', 'cmap', 'OS/2', etc.
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} TTF_Table_Directory;

// Parse essential tables
static bool parse_name_table(FILE* font_file, uint32_t offset, FontEntry* entry);
static bool parse_cmap_table(FILE* font_file, uint32_t offset, FontEntry* entry);
static bool parse_os2_table(FILE* font_file, uint32_t offset, FontEntry* entry);
```

**Testing:**
- Unit tests for font directory scanning on each platform
- TTF/OTF parsing tests with known font files
- Basic font discovery integration tests

### Phase 2: Font Matching & Unicode Support (Week 3-4)

**Deliverables:**
- Advanced font matching algorithm with scoring
- Unicode coverage detection from cmap tables
- Font fallback chains and family aliases
- Integration with existing Radiant font loading

**Font Matching Algorithm:**
```c
static float calculate_match_score(FontEntry* font, FontMatchCriteria* criteria) {
    float score = 0.0f;
    
    // Family name match (highest priority)
    if (font_family_matches(font, criteria->family_name)) {
        score += 40.0f;  // Exact match
    } else if (font_family_alias_matches(font, criteria->family_name)) {
        score += 30.0f;  // Alias match
    } else if (font_generic_family_matches(font, criteria->family_name)) {
        score += 10.0f;  // Generic family (serif, sans-serif, etc.)
    }
    
    // Weight matching
    int weight_diff = abs(font->weight - criteria->weight);
    if (weight_diff == 0) {
        score += 20.0f;
    } else if (weight_diff <= 100) {
        score += 15.0f - (weight_diff / 100.0f * 5.0f);
    }
    
    // Style matching
    if (font->style == criteria->style) {
        score += 15.0f;
    }
    
    // Monospace preference
    if (criteria->prefer_monospace && font->is_monospace) {
        score += 10.0f;
    }
    
    // Unicode support
    if (criteria->required_codepoint != 0) {
        if (font_supports_codepoint(font, criteria->required_codepoint)) {
            score += 15.0f;
        } else {
            return 0.0f;  // Hard requirement
        }
    }
    
    return score;
}
```

**Unicode Coverage System:**
```c
typedef struct UnicodeBlock {
    uint32_t start;
    uint32_t end;
    const char* name;
    const char* language_hints[8];  // ISO 639-1 codes
} UnicodeBlock;

// Standard Unicode blocks for language detection
static const UnicodeBlock unicode_blocks[] = {
    {0x0000, 0x007F, "Basic Latin", {"en", "es", "fr", "de", NULL}},
    {0x0080, 0x00FF, "Latin-1 Supplement", {"fr", "de", "es", NULL}},
    {0x0100, 0x017F, "Latin Extended-A", {"cs", "pl", "hu", NULL}},
    {0x4E00, 0x9FFF, "CJK Unified Ideographs", {"zh", "ja", NULL}},
    {0x0600, 0x06FF, "Arabic", {"ar", "fa", "ur", NULL}},
    {0x0590, 0x05FF, "Hebrew", {"he", NULL}},
    // ... more blocks
};

static bool detect_font_language_support(FontEntry* font, const char* language);
```

**Testing:**
- Font matching accuracy tests with known font combinations
- Unicode coverage tests with multilingual text
- Performance tests with large font collections

### Phase 3: Caching & Optimization (Week 5-6)

**Deliverables:**
- Persistent font cache system
- Cache invalidation based on file modification times
- Performance optimizations for fast startup
- Memory usage optimization

**Cache File Format:**
```c
// Binary cache format for fast loading
typedef struct FontCacheHeader {
    uint32_t magic;           // 'LFNT' - Lambda Font Cache
    uint32_t version;         // Cache format version
    uint32_t num_fonts;       // Total font entries
    uint32_t num_families;    // Total font families
    time_t creation_time;     // Cache creation timestamp
    uint32_t platform_id;     // Platform identifier
    uint32_t checksum;        // Cache integrity check
} FontCacheHeader;

typedef struct FontCacheEntry {
    uint32_t family_name_offset;    // Offset in string table
    uint32_t file_path_offset;      // Offset in string table
    int weight;
    FontStyle style;
    bool is_monospace;
    time_t file_mtime;
    size_t file_size;
    uint32_t unicode_coverage_hash;
    // ... other metadata
} FontCacheEntry;

static bool font_cache_write(FontDatabase* db, const char* cache_path);
static bool font_cache_read(FontDatabase* db, const char* cache_path);
static bool font_cache_validate_entry(FontCacheEntry* entry, const char* file_path);
```

**Performance Optimizations:**
```c
// Fast family lookup with interned names
static const char* intern_family_name(FontDatabase* db, const char* family);

// Lazy Unicode coverage parsing
static void font_parse_unicode_coverage_lazy(FontEntry* font);

// Parallel directory scanning
static void scan_directory_parallel(FontDatabase* db, const char* directory);
```

**Testing:**
- Cache persistence and loading tests
- Cache invalidation correctness tests
- Performance benchmarks vs FontConfig
- Memory usage profiling

### Phase 4: Integration & Platform Polish (Week 7-8)

**Deliverables:**
- Full integration with Radiant font system
- Platform-specific enhancements and optimizations
- Comprehensive test suite and documentation
- Performance validation and benchmarking

**Integration Points:**

**Replace FontConfig in `radiant/font.cpp`:**
```c
// OLD: FontConfig-based implementation
char* load_font_path(FcConfig *font_config, const char* font_name);

// NEW: Custom font database implementation
char* load_font_path_lambda(FontDatabase* font_db, const char* font_name, FontProp* style) {
    FontMatchCriteria criteria = {
        .family_name = font_name,
        .weight = style ? style->font_weight : 400,
        .style = style && style->italic ? FONT_STYLE_ITALIC : FONT_STYLE_NORMAL,
        .prefer_monospace = false,
        .required_codepoint = 0,
        .language = NULL
    };
    
    FontMatchResult result = font_database_find_best_match(font_db, &criteria);
    if (result.font && result.match_score > 0.3f) {
        return strdup(result.font->file_path);
    }
    
    return NULL;
}
```

**Update `radiant/ui_context.cpp`:**
```c
// Replace FontConfig initialization
void ui_context_init(UiContext* uicon) {
    // ... existing FreeType initialization ...
    
    // NEW: Initialize Lambda font database
    uicon->font_database = font_database_create(uicon->pool, uicon->arena);
    if (!font_database_load_cache(uicon->font_database)) {
        font_database_scan(uicon->font_database);
        font_database_save_cache(uicon->font_database);
    }
    
    // ... rest of initialization ...
}
```

**Platform-Specific Enhancements:**

**macOS Optimizations:**
```c
#ifdef __APPLE__
// Use Core Text for enhanced font metadata
#include <CoreText/CoreText.h>

static bool parse_font_with_core_text(const char* file_path, FontEntry* entry) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, 
        (const UInt8*)file_path, strlen(file_path), false);
    CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
    CGFontRef font = CGFontCreateWithDataProvider(provider);
    
    if (font) {
        CFStringRef family_name = CGFontCopyFamilyName(font);
        // Extract metadata using Core Text APIs
        // ... implementation
        return true;
    }
    return false;
}
#endif
```

**Windows Registry Integration:**
```c
#ifdef _WIN32
#include <windows.h>

static void scan_windows_registry_fonts(FontDatabase* db) {
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        
        DWORD index = 0;
        char font_name[256];
        char font_file[MAX_PATH];
        DWORD name_size, file_size;
        
        while (RegEnumValue(hkey, index++, font_name, &name_size,
            NULL, NULL, (LPBYTE)font_file, &file_size) == ERROR_SUCCESS) {
            // Process registry font entries
            // ... implementation
        }
        
        RegCloseKey(hkey);
    }
}
#endif
```

## Build System Integration

### Update `build_lambda_config.json`

**Remove FontConfig Dependencies:**
```json
{
  "libraries": [
    // REMOVE these entries:
    // {"name": "fontconfig", "lib": "/opt/homebrew/lib/libfontconfig_minimal.a"},
    // {"name": "expat", "lib": "/opt/homebrew/opt/expat/lib/libexpat.a"},
    
    // KEEP FreeType (still needed for rendering):
    {
      "name": "freetype",
      "include": "/opt/homebrew/opt/freetype/include/freetype2",
      "lib": "/opt/homebrew/opt/freetype/lib/libfreetype.a",
      "link": "static"
    }
  ]
}
```

**Add New Source Files:**
```json
{
  "source_files": [
    // Add new font config implementation
    "lib/font_config.c",
    
    // Existing files...
    "lib/strbuf.c",
    "lib/arraylist.c",
    // ...
  ]
}
```

### Platform-Specific Build Configurations

**macOS Build:**
```json
{
  "platforms": {
    "macos": {
      "libraries": [
        {
          "name": "CoreText",
          "lib": "-framework CoreText",
          "link": "dynamic"
        },
        {
          "name": "CoreFoundation", 
          "lib": "-framework CoreFoundation",
          "link": "dynamic"
        }
      ]
    }
  }
}
```

**Windows Build:**
```json
{
  "platforms": {
    "windows": {
      "libraries": [
        {
          "name": "advapi32",
          "lib": "-ladvapi32", 
          "link": "dynamic"
        }
      ]
    }
  }
}
```

## Testing Strategy

### Unit Tests (`test/test_font_config_gtest.cpp`)

```cpp
#include <gtest/gtest.h>
#include "../lib/font_config.h"

class FontConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = mempool_create();
        arena = arena_create(1024 * 1024);
        db = font_database_create(pool, arena);
    }
    
    void TearDown() override {
        font_database_destroy(db);
        arena_destroy(arena);
        mempool_destroy(pool);
    }
    
    MemPool* pool;
    Arena* arena;
    FontDatabase* db;
};

TEST_F(FontConfigTest, BasicFontDiscovery) {
    ASSERT_TRUE(font_database_scan(db));
    ArrayList* families = font_get_available_families(db);
    ASSERT_GT(arraylist_size(families), 0);
    
    // Should find common system fonts
    bool found_arial = false;
    bool found_times = false;
    
    for (size_t i = 0; i < arraylist_size(families); i++) {
        const char* family = (const char*)arraylist_get(families, i);
        if (strcasecmp(family, "arial") == 0) found_arial = true;
        if (strcasecmp(family, "times") == 0) found_times = true;
    }
    
    EXPECT_TRUE(found_arial || found_times); // At least one should exist
}

TEST_F(FontConfigTest, FontMatching) {
    font_database_scan(db);
    
    FontMatchCriteria criteria = {
        .family_name = "Arial",
        .weight = 400,
        .style = FONT_STYLE_NORMAL,
        .prefer_monospace = false,
        .required_codepoint = 0x0041 // 'A'
    };
    
    FontMatchResult result = font_database_find_best_match(db, &criteria);
    ASSERT_NE(result.font, nullptr);
    EXPECT_GT(result.match_score, 0.5f);
    EXPECT_TRUE(font_supports_codepoint(result.font, 0x0041));
}

TEST_F(FontConfigTest, UnicodeSupport) {
    font_database_scan(db);
    
    FontMatchCriteria criteria = {
        .family_name = "Arial",
        .weight = 400,
        .style = FONT_STYLE_NORMAL,
        .prefer_monospace = false,
        .required_codepoint = 0x4E2D // Chinese character
    };
    
    FontMatchResult result = font_database_find_best_match(db, &criteria);
    // Should either find a font with Chinese support or return low score
    if (result.font) {
        EXPECT_TRUE(font_supports_codepoint(result.font, 0x4E2D));
    }
}

TEST_F(FontConfigTest, CachePersistence) {
    font_database_scan(db);
    
    // Save cache
    ASSERT_TRUE(font_database_save_cache(db));
    
    // Create new database and load cache
    FontDatabase* db2 = font_database_create(pool, arena);
    ASSERT_TRUE(font_database_load_cache(db2));
    
    // Should have same number of fonts
    ArrayList* families1 = font_get_available_families(db);
    ArrayList* families2 = font_get_available_families(db2);
    EXPECT_EQ(arraylist_size(families1), arraylist_size(families2));
    
    font_database_destroy(db2);
}
```

### Integration Tests

**Performance Benchmarks:**
```bash
# Compare startup time vs FontConfig
time ./lambda.exe --version  # Before (with FontConfig)
time ./lambda_new.exe --version  # After (custom implementation)

# Font discovery benchmark
./lambda.exe benchmark-fonts --iterations=100
```

**Cross-Platform Testing:**
```bash
# Test on all platforms
make test-font-config-macos
make test-font-config-linux  
make test-font-config-windows
```

## Migration Strategy

### Phase 1: Parallel Implementation
- Implement new system alongside existing FontConfig
- Add compile-time flag `LAMBDA_USE_CUSTOM_FONTCONFIG`
- Validate behavior matches existing system

### Phase 2: Integration Testing  
- Run comprehensive test suite with both implementations
- Performance and memory usage comparison
- Fix any behavioral differences

### Phase 3: Switch & Cleanup
- Switch default to custom implementation
- Remove FontConfig dependencies from build system  
- Remove old code and update documentation

## Risk Assessment & Mitigation

### High Risk: Font Metadata Parsing
**Risk:** TTF/OTF parsing bugs cause crashes or incorrect font selection
**Mitigation:** 
- Extensive testing with diverse font collections
- Defensive parsing with bounds checking
- Fallback to filename-based heuristics when parsing fails

### Medium Risk: Platform Compatibility
**Risk:** Platform-specific font discovery issues
**Mitigation:**
- Platform-specific test suites
- Graceful degradation when system APIs unavailable
- Conservative fallbacks to basic directory scanning

### Medium Risk: Performance Regression  
**Risk:** Custom implementation slower than FontConfig
**Mitigation:**
- Benchmark-driven development
- Optimize hot paths (font matching algorithm)
- Implement lazy loading and smart caching

### Low Risk: Unicode Coverage Detection
**Risk:** Incorrect Unicode support detection
**Mitigation:**
- Test with comprehensive Unicode test suite
- Compare results with FontConfig on same fonts
- Fallback to permissive matching when uncertain

## Success Metrics

### Primary Goals (Must Have)
- **Zero Dependencies**: Complete removal of FontConfig and Expat
- **Functional Parity**: All existing font operations work identically
- **Cross-Platform**: Identical behavior on macOS, Linux, Windows
- **Stability**: No crashes or memory leaks in font operations

### Performance Goals (Should Have)  
- **Startup Time**: ≤ 2x FontConfig initialization time
- **Memory Usage**: ≤ 150% of FontConfig memory footprint
- **Font Discovery**: ≤ 3x FontConfig font enumeration time
- **Font Matching**: ≤ 2x FontConfig matching performance

### Quality Goals (Nice to Have)
- **Cache Hit Rate**: > 95% for subsequent startups
- **Unicode Accuracy**: > 99% correct Unicode coverage detection  
- **Match Quality**: Subjectively equivalent font selection quality
- **Maintainability**: Clear, documented, testable code

## Future Enhancements

### Advanced Features
- **Font Substitution**: Automatic fallback for missing glyphs
- **Font Variants**: Support for OpenType feature selection
- **Dynamic Loading**: Hot-reload fonts without restart
- **Remote Fonts**: Web font download and caching
- **Font Synthesis**: Runtime bold/italic generation

### Integration Opportunities
- **Lambda Validator**: Schema validation for font properties
- **CSS Engine**: Direct integration with CSS font-family parsing
- **PDF Generation**: Enhanced font embedding for PDF output
- **Text Shaping**: Integration with HarfBuzz for complex scripts

## Conclusion

This implementation plan provides a comprehensive roadmap for replacing FontConfig with a custom, lightweight font discovery system. The 8-week timeline is realistic given Lambda's existing infrastructure, and the phased approach minimizes risk while ensuring thorough testing.

The custom implementation will eliminate a major external dependency, improve build simplicity, and provide consistent cross-platform behavior. With careful attention to testing and performance, this change will significantly improve Lambda's portability and maintainability.

**Next Steps:**
1. Create `lib/font_config.c` implementation file
2. Begin Phase 1 development (font directory scanning)
3. Set up cross-platform testing infrastructure
4. Implement basic font metadata parsing

**Estimated Total Effort:** 6-8 weeks for complete implementation and testing
**Risk Level:** Medium (manageable with proper testing and incremental approach)
**Impact:** High (eliminates major dependency, improves portability)