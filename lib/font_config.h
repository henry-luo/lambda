/**
 * Lambda FontConfig Replacement - Header
 * 
 * Cross-platform font discovery and matching system for Lambda Script.
 * Replaces FontConfig dependency with lightweight, custom implementation.
 * 
 * Copyright (c) 2025 Lambda Script Project
 */

#ifndef LAMBDA_FONT_CONFIG_H
#define LAMBDA_FONT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Include Lambda data structures
#include "arraylist.h"
#include "hashmap.h"

// Forward declarations (avoid redefinition warnings)
struct Pool;
struct Arena;

// ============================================================================
// Public Type Definitions
// ============================================================================

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
    struct Pool* font_pool;     // Pool for FontEntry allocation
    struct Arena* string_arena; // Arena for string storage
} FontDatabase;

typedef struct FontDatabaseCriteria {
    char family_name[256];      // Requested family
    int weight;                 // 100-900, -1 for any
    FontStyle style;            // normal/italic/oblique
    bool prefer_monospace;      // Prefer fixed-width fonts
    uint32_t required_codepoint; // Must support this codepoint (0 = any)
    char language[8];           // Language hint (ISO 639-1)
} FontDatabaseCriteria;

typedef struct FontDatabaseResult {
    FontEntry* font;            // Best matching font
    float match_score;          // 0.0-1.0 quality score
    bool exact_family_match;    // Family name matched exactly
    bool requires_synthesis;    // Needs bold/italic synthesis
    char* synthetic_style;      // "synthetic-bold", "synthetic-italic", NULL
} FontDatabaseResult;

// ============================================================================
// Public API
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Database lifecycle
FontDatabase* font_database_create(struct Pool* pool, struct Arena* arena);
void font_database_destroy(FontDatabase* db);
bool font_database_scan(FontDatabase* db);
bool font_database_load_cache(FontDatabase* db);
bool font_database_save_cache(FontDatabase* db);

// Font discovery
FontDatabaseResult font_database_find_best_match(FontDatabase* db, FontDatabaseCriteria* criteria);
ArrayList* font_database_find_all_matches(FontDatabase* db, const char* family_name);
FontEntry* font_database_get_by_postscript_name(FontDatabase* db, const char* ps_name);
FontEntry* font_database_get_by_file_path(FontDatabase* db, const char* file_path);

// Font metadata
bool font_entry_supports_codepoint(FontEntry* font, uint32_t codepoint);
bool font_supports_language(FontEntry* font, const char* language);
ArrayList* font_get_available_families(FontDatabase* db);

// System integration
void font_add_scan_directory(FontDatabase* db, const char* directory);
bool font_is_file_changed(FontEntry* font);
void font_database_refresh_changed_files(FontDatabase* db);

// Utility functions
const char* font_format_to_string(FontFormat format);
const char* font_style_to_string(FontStyle style);
FontStyle font_style_from_string(const char* style_str);

// Cache management
void font_database_set_cache_path(FontDatabase* db, const char* cache_path);
bool font_database_cache_is_valid(FontDatabase* db);
void font_database_invalidate_cache(FontDatabase* db);

// Statistics and debugging
size_t font_database_get_font_count(FontDatabase* db);
size_t font_database_get_family_count(FontDatabase* db);
void font_database_print_statistics(FontDatabase* db);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_CONFIG_H