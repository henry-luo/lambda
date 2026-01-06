/**
 * Lambda FontConfig Replacement
 *
 * Cross-platform font discovery and matching system for Lambda Script.
 * Replaces FontConfig dependency with lightweight, custom implementation.
 *
 * Features:
 * - Zero external dependencies (beyond system APIs)
 * - Consistent cross-platform behavior
 * - Integration with Lambda's memory management
 * - Persistent caching for fast startup
 * - Unicode coverage detection
 * - Advanced font matching algorithm
 *
 * Copyright (c) 2025 Lambda Script Project
 */

/* Define POSIX feature test macro for strcasecmp() */
#define _POSIX_C_SOURCE 200809L

#include "font_config.h"
#include "mempool.h"
#include "arena.h"
#include "arraylist.h"
#include "hashmap.h"
#include "strbuf.h"
#include "file.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

// Platform-specific includes
#ifdef __APPLE__
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#include <strings.h>  // for strcasecmp on macOS
#elif defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#include <strings.h>  // for strcasecmp on Linux
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
// Additional Windows constants if not defined
#ifndef CSIDL_FONTS
#define CSIDL_FONTS 0x0014
#endif
#ifndef CSIDL_LOCAL_APPDATA
#define CSIDL_LOCAL_APPDATA 0x001c
#endif
#ifndef SHGFP_TYPE_CURRENT
#define SHGFP_TYPE_CURRENT 0
#endif
#endif

// POSIX includes for non-Windows
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#endif

// ============================================================================
// Constants and Configuration
// ============================================================================

#define FONT_CACHE_MAGIC 0x4C464E54  // 'LFNT'
#define FONT_CACHE_VERSION 1
#define MAX_FONT_FAMILY_NAME 256
#define MAX_FONT_FILE_PATH 1024
#define FONT_MATCH_SCORE_THRESHOLD 0.1f
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// TTF/OTF table tags (big-endian)
#define TTF_TAG_NAME 0x6E616D65  // 'name'
#define TTF_TAG_CMAP 0x636D6170  // 'cmap'
#define TTF_TAG_OS2  0x4F532F32  // 'OS/2'
#define TTF_TAG_HEAD 0x68656164  // 'head'
#define TTF_TAG_HHEA 0x68686561  // 'hhea'

// Name ID constants for name table
#define NAME_ID_FAMILY_NAME 1
#define NAME_ID_SUBFAMILY_NAME 2
#define NAME_ID_POSTSCRIPT_NAME 6

// OS/2 table constants
#define OS2_WEIGHT_CLASS_OFFSET 4
#define OS2_SELECTION_OFFSET 62
#define OS2_SELECTION_ITALIC 0x0001

// Global singleton font database for performance
static FontDatabase* g_global_font_db = NULL;
static bool g_font_db_initialized = false;

// Forward declarations
static bool parse_font_metadata(const char *file_path, FontEntry *entry, Arena *arena);

// Platform-specific font directories
static const char* macos_font_dirs[] = {
    "/System/Library/Fonts",
    "/System/Library/Fonts/Supplemental",
    "/Library/Fonts",
    NULL  // User fonts added dynamically
};

static const char* linux_font_dirs[] = {
    "/usr/share/fonts",
    "/usr/local/share/fonts",
    "/usr/X11R6/lib/X11/fonts",
    NULL  // User fonts added dynamically
};

static const char* windows_font_dirs[] = {
    NULL  // Populated from system calls
};

// Generic font family mappings
// High-priority web fonts that should be loaded immediately
static const char* priority_font_families[] = {
    // CSS web-safe fonts - most commonly used
    "Arial",
    "Helvetica",
    "Times",
    "Times New Roman",
    "Courier",
    "Courier New",
    "Verdana",
    "Georgia",
    "Trebuchet MS",
    "Comic Sans MS",
    "Impact",

    // System fonts commonly used in web design
    "Helvetica Neue",
    "Monaco",
    "Menlo",
    "San Francisco",
    "SF Pro Display",
    "SF Pro Text",

    // Common fallback fonts
    "DejaVu Sans",
    "DejaVu Serif",
    "Liberation Sans",
    "Liberation Serif",
    NULL
};

static const struct {
    const char* generic;
    const char* preferred[8];
} generic_families[] = {
    {"serif", {"Times New Roman", "Times", "Georgia", "DejaVu Serif", NULL}},
    {"sans-serif", {"Arial", "Helvetica", "DejaVu Sans", "Liberation Sans", NULL}},
    {"monospace", {"Courier New", "Courier", "Monaco", "DejaVu Sans Mono", NULL}},
    {"cursive", {"Comic Sans MS", "Apple Chancery", "Bradley Hand", NULL}},
    {"fantasy", {"Impact", "Papyrus", "Herculanum", NULL}},
    {NULL, {NULL}}
};

// Unicode blocks for language detection
static const struct UnicodeBlock {
    uint32_t start;
    uint32_t end;
    const char* name;
    const char* languages[8];
} unicode_blocks[] = {
    {0x0000, 0x007F, "Basic Latin", {"en", "es", "fr", "de", "pt", "it", "nl", NULL}},
    {0x0080, 0x00FF, "Latin-1 Supplement", {"fr", "de", "es", "pt", "it", "da", "sv", NULL}},
    {0x0100, 0x017F, "Latin Extended-A", {"cs", "pl", "hu", "sk", "sl", "hr", NULL}},
    {0x0180, 0x024F, "Latin Extended-B", {"ro", "hr", "sk", "sl", NULL}},
    {0x0370, 0x03FF, "Greek and Coptic", {"el", NULL}},
    {0x0400, 0x04FF, "Cyrillic", {"ru", "uk", "bg", "sr", "mk", "be", NULL}},
    {0x0590, 0x05FF, "Hebrew", {"he", "yi", NULL}},
    {0x0600, 0x06FF, "Arabic", {"ar", "fa", "ur", "ps", NULL}},
    {0x0900, 0x097F, "Devanagari", {"hi", "ne", "mr", "sa", NULL}},
    {0x4E00, 0x9FFF, "CJK Unified Ideographs", {"zh", "ja", NULL}},
    {0xAC00, 0xD7AF, "Hangul Syllables", {"ko", NULL}},
    {0, 0, NULL, {NULL}}
};

// ============================================================================
// Internal Data Structures
// ============================================================================

typedef struct TTF_Header {
    uint32_t scaler_type;
    uint16_t num_tables;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;
} TTF_Header;

typedef struct TTF_Table_Directory {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} TTF_Table_Directory;

typedef struct FontCacheHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t num_fonts;
    uint32_t num_families;
    time_t creation_time;
    uint32_t platform_id;
    uint32_t string_table_size;
    uint32_t checksum;
} FontCacheHeader;

typedef struct FontCacheEntry {
    uint32_t family_name_offset;
    uint32_t subfamily_name_offset;
    uint32_t postscript_name_offset;
    uint32_t file_path_offset;
    int weight;
    FontStyle style;
    bool is_monospace;
    FontFormat format;
    time_t file_mtime;
    size_t file_size;
    uint32_t unicode_coverage_hash;
    int collection_index;
    bool is_collection;
} FontCacheEntry;

// ============================================================================
// Utility Functions
// ============================================================================

// Convert big-endian 32-bit value to host byte order
static uint32_t be32toh_local(uint32_t big_endian_32bits) {
    return ((big_endian_32bits & 0xFF000000) >> 24) |
           ((big_endian_32bits & 0x00FF0000) >> 8)  |
           ((big_endian_32bits & 0x0000FF00) << 8)  |
           ((big_endian_32bits & 0x000000FF) << 24);
}

// Convert big-endian 16-bit value to host byte order
static uint16_t be16toh_local(uint16_t big_endian_16bits) {
    return ((big_endian_16bits & 0xFF00) >> 8) |
           ((big_endian_16bits & 0x00FF) << 8);
}

// Hash function for font entries (for hashmap)
static uint64_t font_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontEntry *font = (const FontEntry*)item;
    if (!font || !font->file_path) return 0;
    return hashmap_xxhash3(font->file_path, strlen(font->file_path), seed0, seed1);
}

// Comparison function for font entries (for hashmap)
static int font_entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;  // Suppress unused parameter warning
    const FontEntry *fa = (const FontEntry*)a;
    const FontEntry *fb = (const FontEntry*)b;

    // Handle NULL pointers
    if (!fa || !fb) return 0;
    if (!fa->file_path && !fb->file_path) return 0;
    if (!fa->file_path) return -1;
    if (!fb->file_path) return 1;

    return strcmp(fa->file_path, fb->file_path);
}

// Hash function for font entry pointers (for hashmaps storing FontEntry*)
static uint64_t font_entry_ptr_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontEntry **font_ptr = (const FontEntry**)item;
    if (!font_ptr || !*font_ptr || !(*font_ptr)->file_path) return 0;
    return hashmap_xxhash3((*font_ptr)->file_path, strlen((*font_ptr)->file_path), seed0, seed1);
}

// Comparison function for font entry pointers (for hashmaps storing FontEntry*)
static int font_entry_ptr_compare(const void *a, const void *b, void *udata) {
    (void)udata;  // Suppress unused parameter warning
    const FontEntry **fa_ptr = (const FontEntry**)a;
    const FontEntry **fb_ptr = (const FontEntry**)b;

    if (!fa_ptr || !fb_ptr || !*fa_ptr || !*fb_ptr) return 0;

    const FontEntry *fa = *fa_ptr;
    const FontEntry *fb = *fb_ptr;

    if (!fa->file_path && !fb->file_path) return 0;
    if (!fa->file_path) return -1;
    if (!fb->file_path) return 1;

    return strcmp(fa->file_path, fb->file_path);
}

// Hash function for font families (for hashmap)
static uint64_t font_family_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const FontFamily *family = (const FontFamily*)item;
    if (!family || !family->family_name) return 0;
    
    // Convert to lowercase for case-insensitive hashing
    // This must match the case-insensitive compare function
    size_t len = strlen(family->family_name);
    char lower_name[256];
    size_t copy_len = len < sizeof(lower_name) - 1 ? len : sizeof(lower_name) - 1;
    for (size_t i = 0; i < copy_len; i++) {
        lower_name[i] = tolower((unsigned char)family->family_name[i]);
    }
    lower_name[copy_len] = '\0';
    
    return hashmap_xxhash3(lower_name, copy_len, seed0, seed1);
}

// Comparison function for font families (for hashmap)
static int font_family_compare(const void *a, const void *b, void *udata) {
    (void)udata;  // Suppress unused parameter warning
    const FontFamily *fa = (const FontFamily*)a;
    const FontFamily *fb = (const FontFamily*)b;

    if (!fa || !fb) return 0;
    if (!fa->family_name && !fb->family_name) return 0;
    if (!fa->family_name) return -1;
    if (!fb->family_name) return 1;

    return strcasecmp(fa->family_name, fb->family_name);
}

// Case-insensitive string comparison for font matching
static bool string_match_ignore_case(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcasecmp(a, b) == 0;
}

// Calculate simple hash for Unicode coverage (for quick comparison)
static uint32_t calculate_unicode_coverage_hash(FontUnicodeRange *ranges) {
    uint32_t hash = 0;
    FontUnicodeRange *current = ranges;

    while (current) {
        hash ^= current->start_codepoint;
        hash ^= (current->end_codepoint << 16);
        hash = (hash << 1) | (hash >> 31);  // Rotate left
        current = current->next;
    }

    return hash;
}

// ============================================================================
// Platform-Specific Functions
// ============================================================================

#ifdef __APPLE__
static void add_macos_user_fonts(FontDatabase *db) {
    const char *home = getenv("HOME");
    if (home) {
        StrBuf *user_fonts = strbuf_create(home);
        strbuf_append_str(user_fonts, "/Library/Fonts");
        font_add_scan_directory(db, user_fonts->str);
        strbuf_free(user_fonts);
    }
}

static bool get_font_metadata_with_core_text(const char *file_path, FontEntry *entry) {
    // Use Core Text APIs for enhanced metadata extraction on macOS
    CFStringRef path_str = CFStringCreateWithCString(NULL, file_path, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path_str, kCFURLPOSIXPathStyle, false);

    if (!url) {
        CFRelease(path_str);
        return false;
    }

    CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
    CGFontRef cg_font = CGFontCreateWithDataProvider(provider);

    bool success = false;
    if (cg_font) {
        CTFontRef font = CTFontCreateWithGraphicsFont(cg_font, 12.0, NULL, NULL);
        CFStringRef family_name = CTFontCopyFamilyName(font);
        if (family_name) {
            char family_buffer[MAX_FONT_FAMILY_NAME];
            if (CFStringGetCString(family_name, family_buffer, sizeof(family_buffer), kCFStringEncodingUTF8)) {
                // Update entry with Core Text metadata
                // This is more accurate than TTF parsing for macOS system fonts
                log_debug("Core Text family name: %s", family_buffer);
                success = true;
            }
            CFRelease(family_name);
        }
        CFRelease(font);
        CGFontRelease(cg_font);
    }

    CGDataProviderRelease(provider);
    CFRelease(url);
    CFRelease(path_str);

    return success;
}
#endif

#ifdef __linux__
static void add_linux_user_fonts(FontDatabase *db) {
    const char *home = getenv("HOME");
    if (home) {
        // ~/.fonts (traditional)
        StrBuf *user_fonts_old = strbuf_create(home);
        strbuf_append_str(user_fonts_old, "/.fonts");
        font_add_scan_directory(db, user_fonts_old->str);
        strbuf_free(user_fonts_old);

        // ~/.local/share/fonts (XDG standard)
        StrBuf *user_fonts_xdg = strbuf_create(home);
        strbuf_append_str(user_fonts_xdg, "/.local/share/fonts");
        font_add_scan_directory(db, user_fonts_xdg->str);
        strbuf_free(user_fonts_xdg);
    }

    // Check XDG_DATA_HOME
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        StrBuf *xdg_fonts = strbuf_create(xdg_data_home);
        strbuf_append_str(xdg_fonts, "/fonts");
        font_add_scan_directory(db, xdg_fonts->str);
        strbuf_free(xdg_fonts);
    }
}
#endif

#ifdef _WIN32
// Windows-specific font discovery functions
static void add_windows_font_directories(ArrayList *directories) {
    char windows_fonts[260];  // MAX_PATH equivalent
    char user_fonts[260];

    // System fonts directory
    if (SHGetFolderPathA(NULL, 0x0014, NULL, 0, windows_fonts) == 0) {  // CSIDL_FONTS, SHGFP_TYPE_CURRENT, S_OK
        arraylist_append(directories, strdup(windows_fonts));
    }

    // User fonts directory
    if (SHGetFolderPathA(NULL, 0x001c, NULL, 0, user_fonts) == 0) {  // CSIDL_LOCAL_APPDATA, SHGFP_TYPE_CURRENT, S_OK
        strcat(user_fonts, "\\Microsoft\\Windows\\Fonts");
        arraylist_append(directories, strdup(user_fonts));
    }
}

static void scan_windows_registry_fonts(FontDatabase *db) {
    HKEY hkey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        0, KEY_READ, &hkey);

    if (result != ERROR_SUCCESS) {
        log_warn("Failed to open Windows font registry key");
        return;
    }

    DWORD index = 0;
    char font_name[256];
    char font_file[260];  // MAX_PATH equivalent
    DWORD name_size, file_size;

    while (true) {
        name_size = sizeof(font_name);
        file_size = sizeof(font_file);

        result = RegEnumValueA(hkey, index++, font_name, &name_size,
            NULL, NULL, (LPBYTE)font_file, &file_size);

        if (result != ERROR_SUCCESS) {
            break;
        }

        // Process registry font entry
        log_debug("Registry font: %s -> %s", font_name, font_file);

        // Convert relative path to absolute if needed
        char full_path[260];  // MAX_PATH equivalent
        if (font_file[0] != '\\' && font_file[1] != ':') {
            // Relative path, prepend Windows fonts directory
            char windows_dir[260];
            if (SHGetFolderPathA(NULL, 0x0014, NULL, 0, windows_dir) == 0) {  // CSIDL_FONTS, SHGFP_TYPE_CURRENT, S_OK
                snprintf(full_path, sizeof(full_path), "%s\\%s", windows_dir, font_file);
            } else {
                continue;
            }
        } else {
            // Absolute path - copy safely with bounds checking
            strncpy(full_path, font_file, sizeof(full_path) - 1);
            full_path[sizeof(full_path) - 1] = '\0';  // Ensure null termination
        }

        // Check if file exists and add to database
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            // Parse font file and add to database
            FontEntry* entry = pool_calloc(db->font_pool, sizeof(FontEntry));
            if (entry && parse_font_metadata(full_path, entry, db->string_arena)) {
                arraylist_append(db->all_fonts, entry);
                log_debug("Added registry font: %s", entry->family_name);
            }
        }
    }

    RegCloseKey(hkey);
}
#else
// Stub functions for non-Windows platforms
static void add_windows_font_directories(ArrayList *directories) {
    // No-op on non-Windows platforms
    (void)directories;
}

static void scan_windows_registry_fonts(FontDatabase *db) {
    // No-op on non-Windows platforms
    (void)db;
}
#endif

// ============================================================================
// Font File Parsing
// ============================================================================

static FontFormat detect_font_format(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        return FONT_FORMAT_UNKNOWN;
    }

    uint32_t signature;
    if (fread(&signature, sizeof(signature), 1, file) != 1) {
        fclose(file);
        return FONT_FORMAT_UNKNOWN;
    }
    fclose(file);

    signature = be32toh_local(signature);

    switch (signature) {
        case 0x00010000:  // TTF
        case 0x74727565:  // 'true' (Mac TTF)
            return FONT_FORMAT_TTF;
        case 0x4F54544F:  // 'OTTO' (OTF)
            return FONT_FORMAT_OTF;
        case 0x74746366:  // 'ttcf' (TTC)
            return FONT_FORMAT_TTC;
        case 0x774F4646:  // 'wOFF'
            return FONT_FORMAT_WOFF;
        case 0x774F4632:  // 'wOF2'
            return FONT_FORMAT_WOFF2;
        default:
            return FONT_FORMAT_UNKNOWN;
    }
}

static bool read_ttf_table_directory(FILE *file, TTF_Header *header, TTF_Table_Directory **tables) {
    *tables = calloc(header->num_tables, sizeof(TTF_Table_Directory));
    if (!*tables) {
        return false;
    }

    for (int i = 0; i < header->num_tables; i++) {
        if (fread(&(*tables)[i], sizeof(TTF_Table_Directory), 1, file) != 1) {
            free(*tables);
            *tables = NULL;
            return false;
        }

        // Convert from big-endian
        (*tables)[i].tag = be32toh_local((*tables)[i].tag);
        (*tables)[i].offset = be32toh_local((*tables)[i].offset);
        (*tables)[i].length = be32toh_local((*tables)[i].length);
    }

    return true;
}

static TTF_Table_Directory* find_ttf_table(TTF_Table_Directory *tables, int num_tables, uint32_t tag) {
    for (int i = 0; i < num_tables; i++) {
        if (tables[i].tag == tag) {
            return &tables[i];
        }
    }
    return NULL;
}

static bool parse_name_table(FILE *file, TTF_Table_Directory *name_table, FontEntry *entry, Arena *arena) {
    if (fseek(file, name_table->offset, SEEK_SET) != 0) {
        return false;
    }

    uint16_t format, count, string_offset;
    if (fread(&format, 2, 1, file) != 1 ||
        fread(&count, 2, 1, file) != 1 ||
        fread(&string_offset, 2, 1, file) != 1) {
        return false;
    }

    format = be16toh_local(format);
    count = be16toh_local(count);
    string_offset = be16toh_local(string_offset);

    // Read name records
    for (int i = 0; i < count; i++) {
        // Early exit if we already have family name (performance optimization)
        if (entry->family_name) {
            break;
        }
        uint16_t platform_id, encoding_id, language_id, name_id, length, offset;

        if (fread(&platform_id, 2, 1, file) != 1 ||
            fread(&encoding_id, 2, 1, file) != 1 ||
            fread(&language_id, 2, 1, file) != 1 ||
            fread(&name_id, 2, 1, file) != 1 ||
            fread(&length, 2, 1, file) != 1 ||
            fread(&offset, 2, 1, file) != 1) {
            return false;
        }

        platform_id = be16toh_local(platform_id);
        encoding_id = be16toh_local(encoding_id);
        language_id = be16toh_local(language_id);
        name_id = be16toh_local(name_id);
        length = be16toh_local(length);
        offset = be16toh_local(offset);

        // Debug: show the first few name records (only in verbose mode)
        #ifdef FONT_DEBUG_VERBOSE
        if (i < 5) {
            log_debug("name record %d: platform=%d, encoding=%d, language=%d, name_id=%d, length=%d",
                      i, platform_id, encoding_id, language_id, name_id, length);
        }
        #endif

        // We're interested in family names (name_id == 1) from various platforms:
        // Platform 1 (Mac): language 0, Platform 3 (Microsoft): language 0x0409/1033
        bool is_family_name = (name_id == 1);
        bool is_supported_platform =
            (platform_id == 1 && language_id == 0) ||  // Mac platform
            (platform_id == 3 && (language_id == 0x0409 || language_id == 1033 || language_id == 0));  // Microsoft platform

        if (is_family_name && is_supported_platform) {
            long current_pos = ftell(file);
            long string_pos = name_table->offset + string_offset + offset;

            if (fseek(file, string_pos, SEEK_SET) == 0) {
                char *name_buffer = arena_alloc(arena, length + 1);
                if (name_buffer && fread(name_buffer, 1, length, file) == length) {
                    name_buffer[length] = '\0';

                    // Convert to ASCII based on platform encoding
                    char *ascii_name = NULL;

                    // Debug: show raw bytes for family name records (only in debug builds)
                    #ifdef FONT_DEBUG_VERBOSE
                    if (name_id == 1 && length <= 20) {
                        char hex_buffer[256] = {0};
                        char *p = hex_buffer;
                        p += snprintf(p, sizeof(hex_buffer), "Raw bytes for family name (platform=%d, length=%d): ", platform_id, length);
                        for (int j = 0; j < length && (p - hex_buffer + 4) < sizeof(hex_buffer); j++) {
                            p += snprintf(p, sizeof(hex_buffer) - (p - hex_buffer), "%02x ", (uint8_t)name_buffer[j]);
                        }
                        log_debug("%s", hex_buffer);
                    }
                    #endif

                    if (platform_id == 1) {
                        // Platform 1 (Mac): usually MacRoman encoding (single byte)
                        ascii_name = arena_alloc(arena, length + 1);
                        if (ascii_name) {
                            int ascii_len = 0;
                            for (int j = 0; j < length; j++) {
                                uint8_t byte = (uint8_t)name_buffer[j];
                                if (byte >= 32 && byte < 127) {  // ASCII range
                                    ascii_name[ascii_len++] = byte;
                                } else if (byte != 0) {  // Include non-null non-ASCII for debugging
                                    ascii_name[ascii_len++] = '?';
                                }
                            }
                            ascii_name[ascii_len] = '\0';

                            // Debug output for platform 1
                            #ifdef FONT_DEBUG_VERBOSE
                            if (name_id == 1) {
                                log_debug("Platform 1 decoded name: '%s'", ascii_name);
                            }
                            #endif
                        }
                    } else if (platform_id == 3) {
                        // Platform 3 (Microsoft): UTF-16 BE
                        ascii_name = arena_alloc(arena, (length / 2) + 1);
                        if (ascii_name) {
                            int ascii_len = 0;
                            for (int j = 0; j < length - 1; j += 2) {
                                uint8_t high_byte = (uint8_t)name_buffer[j];
                                uint8_t low_byte = (uint8_t)name_buffer[j + 1];

                                // For ASCII characters, high byte should be 0
                                if (high_byte == 0 && low_byte >= 32 && low_byte < 127) {
                                    ascii_name[ascii_len++] = low_byte;
                                } else if (high_byte != 0 || low_byte != 0) {  // Include non-null for debugging
                                    ascii_name[ascii_len++] = '?';
                                }
                            }
                            ascii_name[ascii_len] = '\0';

                            // Debug output for platform 3
                            #ifdef FONT_DEBUG_VERBOSE
                            if (name_id == 1) {
                                log_debug("Platform 3 decoded name: '%s'", ascii_name);
                            }
                            #endif
                        }
                    }

                        if (ascii_name && strlen(ascii_name) > 0) {
                            switch (name_id) {
                                case NAME_ID_FAMILY_NAME:
                                    if (!entry->family_name) {
                                        entry->family_name = ascii_name;
                                    }
                                    break;
                                case NAME_ID_SUBFAMILY_NAME:
                                    if (!entry->subfamily_name) {
                                        entry->subfamily_name = ascii_name;
                                    }
                                break;
                            case NAME_ID_POSTSCRIPT_NAME:
                                if (!entry->postscript_name) {
                                    entry->postscript_name = ascii_name;
                                }
                                break;
                        }
                    }
                }
            }

            fseek(file, current_pos, SEEK_SET);
        }
    }

    return entry->family_name != NULL;
}

static bool parse_os2_table(FILE *file, TTF_Table_Directory *os2_table, FontEntry *entry) {
    if (fseek(file, os2_table->offset, SEEK_SET) != 0) {
        return false;
    }

    uint16_t version;
    if (fread(&version, 2, 1, file) != 1) {
        return false;
    }
    version = be16toh_local(version);

    // Skip to weight class (offset 4 from start of table)
    if (fseek(file, os2_table->offset + OS2_WEIGHT_CLASS_OFFSET, SEEK_SET) != 0) {
        return false;
    }

    uint16_t weight_class;
    if (fread(&weight_class, 2, 1, file) != 1) {
        return false;
    }
    weight_class = be16toh_local(weight_class);
    entry->weight = weight_class;

    // Check selection flags for italic
    if (fseek(file, os2_table->offset + OS2_SELECTION_OFFSET, SEEK_SET) == 0) {
        uint16_t selection;
        if (fread(&selection, 2, 1, file) == 1) {
            selection = be16toh_local(selection);
            if (selection & OS2_SELECTION_ITALIC) {
                entry->style = FONT_STYLE_ITALIC;
            }
        }
    }

    return true;
}

static bool parse_cmap_table(FILE *file, TTF_Table_Directory *cmap_table, FontEntry *entry, Arena *arena) {
    if (fseek(file, cmap_table->offset, SEEK_SET) != 0) {
        return false;
    }

    uint16_t version, num_tables;
    if (fread(&version, 2, 1, file) != 1 ||
        fread(&num_tables, 2, 1, file) != 1) {
        return false;
    }

    version = be16toh_local(version);
    num_tables = be16toh_local(num_tables);

    // For now, just mark that we found a cmap table
    // Full Unicode range parsing would be more complex
    FontUnicodeRange *basic_range = arena_alloc(arena, sizeof(FontUnicodeRange));
    if (basic_range) {
        basic_range->start_codepoint = 0x0020;  // Space
        basic_range->end_codepoint = 0x007E;    // Tilde (basic ASCII)
        basic_range->next = NULL;
        entry->unicode_ranges = basic_range;
        entry->unicode_coverage_hash = calculate_unicode_coverage_hash(basic_range);
    }

    return true;
}

// TTC (TrueType Collection) header structure
typedef struct TTC_Header {
    uint32_t signature;    // 'ttcf'
    uint32_t version;      // 0x00010000 or 0x00020000
    uint32_t num_fonts;    // Number of fonts in collection
    // Followed by array of offsets to font directories
} TTC_Header;

static bool parse_ttc_font_metadata(const char *file_path, FontDatabase *db, Arena *arena) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        log_warn("Failed to open TTC file: %s", file_path);
        return false;
    }

    TTC_Header ttc_header;
    if (fread(&ttc_header, sizeof(ttc_header), 1, file) != 1) {
        log_warn("Failed to read TTC header: %s", file_path);
        fclose(file);
        return false;
    }

    ttc_header.num_fonts = be32toh_local(ttc_header.num_fonts);
    #ifdef FONT_DEBUG_VERBOSE
    log_debug("TTC file %s contains %u fonts", file_path, ttc_header.num_fonts);
    #endif

    // Read offsets to individual fonts
    uint32_t *font_offsets = calloc(ttc_header.num_fonts, sizeof(uint32_t));
    if (!font_offsets) {
        fclose(file);
        return false;
    }

    if (fread(font_offsets, sizeof(uint32_t), ttc_header.num_fonts, file) != ttc_header.num_fonts) {
        log_warn("Failed to read TTC font offsets: %s", file_path);
        free(font_offsets);
        fclose(file);
        return false;
    }

    // Convert offsets from big-endian
    for (uint32_t i = 0; i < ttc_header.num_fonts; i++) {
        font_offsets[i] = be32toh_local(font_offsets[i]);
    }

    // Parse each font in the collection (limit to first 8 for performance)
    uint32_t max_fonts_to_process = ttc_header.num_fonts > 8 ? 8 : ttc_header.num_fonts;
    bool success = false;
    for (uint32_t i = 0; i < max_fonts_to_process; i++) {
        if (fseek(file, font_offsets[i], SEEK_SET) != 0) {
            log_warn("Failed to seek to font %u in TTC: %s", i, file_path);
            continue;
        }

        // Create a new font entry for this font in the collection
        FontEntry* entry = pool_calloc(db->font_pool, sizeof(FontEntry));
        if (!entry) continue;

        // Initialize entry for this collection font
        entry->file_path = arena_strdup(arena, file_path);
        entry->format = FONT_FORMAT_TTC;
        entry->weight = 400;
        entry->style = FONT_STYLE_NORMAL;
        entry->is_monospace = false;
        entry->collection_index = i;
        entry->is_collection = true;

        // Get file metadata
        struct stat file_stat;
        if (stat(file_path, &file_stat) == 0) {
            entry->file_mtime = file_stat.st_mtime;
            entry->file_size = file_stat.st_size;
        }

        // Read TTF header at this offset
        TTF_Header header;
        if (fread(&header, sizeof(header), 1, file) != 1) {
            log_debug("Failed to read TTF header for font %u in TTC: %s", i, file_path);
            continue;
        }

        header.num_tables = be16toh_local(header.num_tables);

        // Read table directory
        TTF_Table_Directory *tables;
        if (!read_ttf_table_directory(file, &header, &tables)) {
            log_debug("Failed to read TTF table directory for font %u in TTC: %s", i, file_path);
            continue;
        }

        // Parse essential tables
        bool font_success = true;

        TTF_Table_Directory *name_table = find_ttf_table(tables, header.num_tables, TTF_TAG_NAME);
        if (name_table) {
            // TTC table offsets are already absolute from file start (per OpenType spec)
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("TTC font %u: parsing name table at offset %u", i, name_table->offset);
            #endif
            font_success &= parse_name_table(file, name_table, entry, arena);
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("TTC font %u: name table parsed, family_name=%s", i, entry->family_name ? entry->family_name : "NULL");
            #endif
        } else {
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("TTC font %u: no name table found", i);
            #endif
            font_success = false;
        }

        TTF_Table_Directory *os2_table = find_ttf_table(tables, header.num_tables, TTF_TAG_OS2);
        if (os2_table) {
            // TTC table offsets are already absolute from file start
            parse_os2_table(file, os2_table, entry);
        }

        TTF_Table_Directory *cmap_table = find_ttf_table(tables, header.num_tables, TTF_TAG_CMAP);
        if (cmap_table) {
            // TTC table offsets are already absolute from file start
            parse_cmap_table(file, cmap_table, entry, arena);
        }

        free(tables);

        // Set fallback names if needed
        if (!entry->family_name) {
            char fallback_name[256];
            snprintf(fallback_name, sizeof(fallback_name), "TTC Font %u", i);
            entry->family_name = arena_strdup(arena, fallback_name);
        }

        if (!entry->subfamily_name) {
            if (entry->style == FONT_STYLE_ITALIC && entry->weight > 600) {
                entry->subfamily_name = arena_strdup(arena, "Bold Italic");
            } else if (entry->style == FONT_STYLE_ITALIC) {
                entry->subfamily_name = arena_strdup(arena, "Italic");
            } else if (entry->weight > 600) {
                entry->subfamily_name = arena_strdup(arena, "Bold");
            } else {
                entry->subfamily_name = arena_strdup(arena, "Regular");
            }
        }

        if (font_success && entry->family_name) {
            arraylist_append(db->all_fonts, entry);
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("Successfully parsed TTC font %u: %s (%s)", i, entry->family_name, entry->subfamily_name);
            #endif
            success = true;
        } else {
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("Failed to parse TTC font %u (font_success=%d, family_name=%s)", i, font_success, entry->family_name ? entry->family_name : "NULL");
            #endif
        }
    }

    free(font_offsets);
    fclose(file);
    return success;
}

static bool parse_font_metadata(const char *file_path, FontEntry *entry, Arena *arena) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        log_warn("Failed to open font file: %s", file_path);
        return false;
    }

    // Initialize entry
    entry->file_path = arena_strdup(arena, file_path);
    if (!entry->file_path) {
        log_error("Failed to allocate file_path for: %s", file_path);
        fclose(file);
        return false;
    }
    entry->format = detect_font_format(file_path);
    entry->weight = 400;  // Default normal weight
    entry->style = FONT_STYLE_NORMAL;
    entry->is_monospace = false;
    entry->collection_index = 0;
    entry->is_collection = (entry->format == FONT_FORMAT_TTC);

    // Clear placeholder family_name so parse_name_table() reads actual font metadata
    // Placeholders may have guessed family names from filename which can be incorrect
    // (e.g., "Arial Narrow" files guessed as "Arial")
    entry->family_name = NULL;
    entry->subfamily_name = NULL;
    entry->postscript_name = NULL;

    // Get file metadata
    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0) {
        entry->file_mtime = file_stat.st_mtime;
        entry->file_size = file_stat.st_size;
    }

    if (entry->format == FONT_FORMAT_UNKNOWN) {
        log_debug("Unknown font format: %s", file_path);
        fclose(file);
        return false;
    }

    // Handle TTC files differently - they need special processing
    if (entry->format == FONT_FORMAT_TTC) {
        fclose(file);
        log_debug("TTC file detected, but parse_font_metadata called for single entry: %s", file_path);
        return false;  // TTC files should be handled by parse_ttc_font_metadata
    }

    // Read TTF/OTF header
    TTF_Header header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        log_warn("Failed to read TTF header: %s", file_path);
        fclose(file);
        return false;
    }

    header.num_tables = be16toh_local(header.num_tables);

    // Read table directory
    TTF_Table_Directory *tables;
    if (!read_ttf_table_directory(file, &header, &tables)) {
        log_warn("Failed to read TTF table directory: %s", file_path);
        fclose(file);
        return false;
    }

    // Parse essential tables
    bool success = true;

    TTF_Table_Directory *name_table = find_ttf_table(tables, header.num_tables, TTF_TAG_NAME);
    if (name_table) {
        success &= parse_name_table(file, name_table, entry, arena);
    }

    TTF_Table_Directory *os2_table = find_ttf_table(tables, header.num_tables, TTF_TAG_OS2);
    if (os2_table) {
        parse_os2_table(file, os2_table, entry);  // Non-critical
    }

    TTF_Table_Directory *cmap_table = find_ttf_table(tables, header.num_tables, TTF_TAG_CMAP);
    if (cmap_table) {
        parse_cmap_table(file, cmap_table, entry, arena);  // Non-critical
    }

    free(tables);
    fclose(file);

    // Fallback to filename if parsing failed
    if (!entry->family_name) {
        const char *filename = strrchr(file_path, '/');
        if (!filename) filename = strrchr(file_path, '\\');
        filename = filename ? filename + 1 : file_path;

        // Strip extension
        char *family_fallback = arena_strdup(arena, filename);
        char *ext = strrchr(family_fallback, '.');
        if (ext) *ext = '\0';

        entry->family_name = family_fallback;
        log_debug("Using filename as family name: %s", entry->family_name);
    }

    // Set subfamily if not parsed
    if (!entry->subfamily_name) {
        if (entry->style == FONT_STYLE_ITALIC && entry->weight > 600) {
            entry->subfamily_name = arena_strdup(arena, "Bold Italic");
        } else if (entry->style == FONT_STYLE_ITALIC) {
            entry->subfamily_name = arena_strdup(arena, "Italic");
        } else if (entry->weight > 600) {
            entry->subfamily_name = arena_strdup(arena, "Bold");
        } else {
            entry->subfamily_name = arena_strdup(arena, "Regular");
        }
    }

    log_debug("Successfully parsed font: %s (%s %s)",
        entry->family_name, entry->subfamily_name, entry->file_path);

    return success;
}

// ============================================================================
// Font Database Implementation
// ============================================================================

FontDatabase* font_database_create(struct Pool* pool, struct Arena* arena) {
    if (!pool || !arena) {
        log_error("Font database requires valid memory pool and arena");
        return NULL;
    }

    FontDatabase* db = pool_calloc(pool, sizeof(FontDatabase));
    if (!db) {
        log_error("Failed to allocate font database");
        return NULL;
    }

    // Initialize hashmaps
    db->families = hashmap_new(sizeof(FontFamily), 0, 0, 0,
        font_family_hash, font_family_compare, NULL, NULL);
    db->postscript_names = hashmap_new(sizeof(FontEntry*), 0, 0, 0,
        font_entry_ptr_hash, font_entry_ptr_compare, NULL, NULL);
    db->file_paths = hashmap_new(sizeof(FontEntry*), 0, 0, 0,
        font_entry_ptr_hash, font_entry_ptr_compare, NULL, NULL);

    if (!db->families || !db->postscript_names || !db->file_paths) {
        log_error("Failed to create font database hashmaps");
        return NULL;
    }

    // Initialize arrays
    db->all_fonts = arraylist_new(0);
    db->scan_directories = arraylist_new(0);
    db->font_files = arraylist_new(0);

    if (!db->all_fonts || !db->scan_directories || !db->font_files) {
        log_error("Failed to create font database arrays");
        if (db->families) hashmap_free(db->families);
        if (db->postscript_names) hashmap_free(db->postscript_names);
        if (db->file_paths) hashmap_free(db->file_paths);
        if (db->all_fonts) arraylist_free(db->all_fonts);
        if (db->scan_directories) arraylist_free(db->scan_directories);
        if (db->font_files) arraylist_free(db->font_files);
        return NULL;
    }

    // Store memory management references
    db->font_pool = pool;
    db->string_arena = arena;

    log_debug("Created font database");
    return db;
}

void font_database_destroy(FontDatabase* db) {
    if (!db) return;

    log_debug("Destroying font database");

    if (db->families) hashmap_free(db->families);
    if (db->postscript_names) hashmap_free(db->postscript_names);
    if (db->file_paths) hashmap_free(db->file_paths);
    if (db->all_fonts) arraylist_free(db->all_fonts);
    if (db->scan_directories) {
        // Note: Directory strings are arena-allocated, don't free individually
        arraylist_free(db->scan_directories);
    }
    if (db->font_files) {
        // Note: Font file strings are arena-allocated, don't free individually
        arraylist_free(db->font_files);
    }

    // Note: Don't destroy pool/arena - they're managed by caller
}

static void add_platform_font_directories(FontDatabase* db) {
    const char **dirs = NULL;

#ifdef __APPLE__
    // Add user fonts first - they're most likely what the user wants
    add_macos_user_fonts(db);
    dirs = macos_font_dirs;
    while (*dirs) {
        font_add_scan_directory(db, *dirs);
        dirs++;
    }
#elif defined(__linux__)
    // Add user fonts first - they're most likely what the user wants
    add_linux_user_fonts(db);
    dirs = linux_font_dirs;
    while (*dirs) {
        font_add_scan_directory(db, *dirs);
        dirs++;
    }
#elif defined(_WIN32)
    add_windows_font_directories(db->scan_directories);
#endif
}

void font_add_scan_directory(FontDatabase* db, const char* directory) {
    if (!db || !directory) return;

    // Check if directory already exists
    for (size_t i = 0; i < db->scan_directories->length; i++) {
        const char* existing = (const char*)db->scan_directories->data[i];
        if (strcmp(existing, directory) == 0) {
            return;  // Already added
        }
    }

    char* dir_copy = arena_strdup(db->string_arena, directory);
    arraylist_append(db->scan_directories, dir_copy);
    log_debug("Added font scan directory: %s", directory);
}

// Fast font file extension checking using suffix matching
static bool is_font_file(const char* filename) {
    if (!filename) return false;

    size_t len = strlen(filename);
    if (len < 5) return false; // Minimum: "a.ttf"

    // Check for common extensions using fast suffix matching
    const char* end = filename + len;

    // Check .ttf (most common)
    if (len >= 4 &&
        (end[-4] == '.' || end[-4] == '.') &&
        (end[-3] == 't' || end[-3] == 'T') &&
        (end[-2] == 't' || end[-2] == 'T') &&
        (end[-1] == 'f' || end[-1] == 'F')) {
        return true;
    }

    // Check .otf
    if (len >= 4 &&
        (end[-4] == '.' || end[-4] == '.') &&
        (end[-3] == 'o' || end[-3] == 'O') &&
        (end[-2] == 't' || end[-2] == 'T') &&
        (end[-1] == 'f' || end[-1] == 'F')) {
        return true;
    }

    // Check .ttc
    if (len >= 4 &&
        (end[-4] == '.' || end[-4] == '.') &&
        (end[-3] == 't' || end[-3] == 'T') &&
        (end[-2] == 't' || end[-2] == 'T') &&
        (end[-1] == 'c' || end[-1] == 'C')) {
        return true;
    }

    return false; // Skip woff/woff2 for now - less common and slower to parse
}

#define MAX_TTC_FONTS 4  // Limit TTC fonts for performance - reduced from 8

static bool is_valid_font_file_size(off_t file_size) {
    // Skip files that are too small (< 1KB) or too large (> 50MB)
    // This helps avoid processing invalid or corrupted files
    return file_size >= 1024 && file_size <= (50 * 1024 * 1024);
}

// Skip known non-font directories for performance
static bool should_skip_directory(const char* dirname) {
    if (!dirname) return true;

    // Skip common non-font directories
    const char* skip_dirs[] = {
        "Cache", "Caches", "cache", "caches",
        "Temp", "temp", "tmp", "TMP",
        "Logs", "logs", "Log", "log",
        "Backup", "backup", "Backups", "backups",
        "Archive", "archive", "Archives", "archives",
        "Documentation", "Docs", "docs",
        "Preferences", "Settings", "Config", "config",
        NULL
    };

    for (const char** skip = skip_dirs; *skip; skip++) {
        if (strcmp(dirname, *skip) == 0) {
            return true;
        }
    }
    return false;
}

// Check if a font family is a high-priority web font
static bool is_priority_font_family(const char* family_name) {
    if (!family_name) return false;

    for (int i = 0; priority_font_families[i]; i++) {
        if (strcasecmp(family_name, priority_font_families[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Create placeholder font entry without parsing - for lazy loading
static FontEntry* create_font_placeholder(const char* file_path, Arena* arena) {
    if (!file_path || !arena) return NULL;

    FontEntry* font = (FontEntry*)arena_alloc(arena, sizeof(FontEntry));
    if (!font) return NULL;

    // Initialize with minimal data
    memset(font, 0, sizeof(FontEntry));
    font->file_path = arena_strdup(arena, file_path);
    font->is_placeholder = true;  // Mark for lazy parsing
    font->weight = 400;  // Default weight
    font->style = FONT_STYLE_NORMAL;  // Default style

    // Try to guess family from filename for priority checking
    const char* filename = strrchr(file_path, '/');
    filename = filename ? filename + 1 : file_path;

    // Simple heuristics for common font families based on filename
    if (strstr(filename, "Arial") || strstr(filename, "arial")) {
        font->family_name = arena_strdup(arena, "Arial");
    } else if (strstr(filename, "Verdana") || strstr(filename, "verdana")) {
        font->family_name = arena_strdup(arena, "Verdana");
    } else if (strstr(filename, "DejaVuSans") || strstr(filename, "DejaVu Sans") ||
               strstr(filename, "dejavu-sans") || strstr(filename, "DejaVu-Sans")) {
        font->family_name = arena_strdup(arena, "DejaVu Sans");
    } else if (strstr(filename, "DejaVuSerif") || strstr(filename, "DejaVu Serif") ||
               strstr(filename, "dejavu-serif") || strstr(filename, "DejaVu-Serif")) {
        font->family_name = arena_strdup(arena, "DejaVu Serif");
    } else if (strstr(filename, "DejaVuSansMono") || strstr(filename, "DejaVu Sans Mono") ||
               strstr(filename, "dejavu-sans-mono") || strstr(filename, "DejaVu-Sans-Mono")) {
        font->family_name = arena_strdup(arena, "DejaVu Sans Mono");
    } else if (strstr(filename, "Liberation") || strstr(filename, "liberation")) {
        // Liberation fonts (common on Linux)
        if (strstr(filename, "Sans")) {
            font->family_name = arena_strdup(arena, "Liberation Sans");
        } else if (strstr(filename, "Serif")) {
            font->family_name = arena_strdup(arena, "Liberation Serif");
        } else if (strstr(filename, "Mono")) {
            font->family_name = arena_strdup(arena, "Liberation Mono");
        }
    } else if (strstr(filename, "Times New Roman") || strstr(filename, "times new roman")) {
        // Must check "Times New Roman" before "Times" since strstr("Times New Roman", "Times") == true
        font->family_name = arena_strdup(arena, "Times New Roman");
    } else if (strstr(filename, "Times") || strstr(filename, "times")) {
        font->family_name = arena_strdup(arena, "Times");
    } else if (strstr(filename, "HelveticaNeue") || strstr(filename, "Helvetica Neue") ||
               strstr(filename, "helveticaneue") || strstr(filename, "helvetica neue")) {
        // Must check "Helvetica Neue" before "Helvetica"
        font->family_name = arena_strdup(arena, "Helvetica Neue");
    } else if (strstr(filename, "Helvetica") || strstr(filename, "helvetica")) {
        font->family_name = arena_strdup(arena, "Helvetica");
    } else if (strstr(filename, "Courier New") || strstr(filename, "courier new")) {
        // Must check "Courier New" before "Courier"
        font->family_name = arena_strdup(arena, "Courier New");
    } else if (strstr(filename, "Courier") || strstr(filename, "courier")) {
        font->family_name = arena_strdup(arena, "Courier");
    } else if (strstr(filename, "Menlo") || strstr(filename, "menlo")) {
        font->family_name = arena_strdup(arena, "Menlo");
    } else if (strstr(filename, "Monaco") || strstr(filename, "monaco")) {
        font->family_name = arena_strdup(arena, "Monaco");
    } else if (strstr(filename, "Georgia") || strstr(filename, "georgia")) {
        font->family_name = arena_strdup(arena, "Georgia");
    } else if (strstr(filename, "Trebuchet") || strstr(filename, "trebuchet")) {
        font->family_name = arena_strdup(arena, "Trebuchet MS");
    } else if (strstr(filename, "Comic Sans") || strstr(filename, "comic sans")) {
        font->family_name = arena_strdup(arena, "Comic Sans MS");
    } else if (strstr(filename, "Impact") || strstr(filename, "impact")) {
        font->family_name = arena_strdup(arena, "Impact");
    } else if (strstr(filename, "Apple Color Emoji") || strstr(filename, "AppleColorEmoji")) {
        font->family_name = arena_strdup(arena, "Apple Color Emoji");
    } else if (strstr(filename, "PingFang") || strstr(filename, "pingfang")) {
        font->family_name = arena_strdup(arena, "PingFang SC");
    } else if (strstr(filename, "STHeiti") || strstr(filename, "stheiti")) {
        font->family_name = arena_strdup(arena, "STHeiti");
    } else if (strstr(filename, "Heiti") || strstr(filename, "heiti")) {
        font->family_name = arena_strdup(arena, "Heiti SC");
    } else if (strstr(filename, "Songti") || strstr(filename, "songti")) {
        font->family_name = arena_strdup(arena, "Songti SC");
    } else {
        // Unknown family - use filename without extension as family name
        // This enables lazy loading for user fonts like Ahem.ttf, lato.ttf, etc.
        size_t name_len = strlen(filename);
        const char* ext = strrchr(filename, '.');
        if (ext) {
            name_len = ext - filename;
        }
        if (name_len > 0 && name_len < 256) {
            char family_buf[256];
            memcpy(family_buf, filename, name_len);
            family_buf[name_len] = '\0';
            font->family_name = arena_strdup(arena, family_buf);
        }
    }

    return font;
}

static void scan_directory_recursive(FontDatabase* db, const char* directory, int max_depth) {
    if (max_depth <= 0) return;

    log_debug("Scanning directory: %s (depth: %d)", directory, max_depth);

#ifdef _WIN32
    // Simplified Windows directory scanning - will implement properly later
    log_debug("Windows directory scanning not fully implemented yet: %s", directory);
    (void)db; // Suppress unused parameter warning
#else
    DIR* dir = opendir(directory);
    if (!dir) {
        log_debug("Failed to open directory: %s", directory);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. directories
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files and system temporary files for performance
        if (entry->d_name[0] == '.' || strstr(entry->d_name, "~$") ||
            strstr(entry->d_name, ".tmp") || strstr(entry->d_name, ".cache")) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);

        // Fast path: check if it's a font file BEFORE expensive stat() call
        bool is_potential_font = is_font_file(entry->d_name);

        struct stat stat_buf;
        if (stat(full_path, &stat_buf) != 0) {
            continue;
        }

        if (S_ISDIR(stat_buf.st_mode)) {
            // Skip known non-font directories for performance
            if (!should_skip_directory(entry->d_name)) {
                scan_directory_recursive(db, full_path, max_depth - 1);
            }
        } else if (S_ISREG(stat_buf.st_mode) && is_potential_font && is_valid_font_file_size(stat_buf.st_size)) {
            // Create placeholder for lazy loading instead of parsing immediately
            FontEntry* placeholder = create_font_placeholder(full_path, db->string_arena);
            if (placeholder) {
                arraylist_append(db->all_fonts, placeholder);
                hashmap_set(db->file_paths, &placeholder);

                #ifdef FONT_DEBUG_VERBOSE
                log_debug("Created placeholder for font: %s (family: %s)",
                          full_path, placeholder->family_name ? placeholder->family_name : "unknown");
                #endif
            }
        }
    }

    closedir(dir);
#endif
}

// Parse a placeholder font in-place (convert placeholder to full font entry)
static bool parse_placeholder_font(FontEntry* placeholder, Arena* arena) {
    if (!placeholder || !placeholder->is_placeholder || !placeholder->file_path) {
        return false;
    }

    #ifdef FONT_DEBUG_VERBOSE
    log_debug("Parsing placeholder font: %s", placeholder->file_path);
    #endif

    // Check if this is a TTC file - TTC files cannot be parsed in-place
    // because they contain multiple fonts
    FontFormat format = detect_font_format(placeholder->file_path);
    if (format == FONT_FORMAT_TTC) {
        #ifdef FONT_DEBUG_VERBOSE
        log_debug("Placeholder is TTC file, requires special handling: %s", placeholder->file_path);
        #endif
        // TTC files need to be parsed by parse_ttc_font_metadata which adds entries to the database
        // We can't parse in-place, so just mark this placeholder as parsed but empty
        placeholder->is_placeholder = false;
        placeholder->family_name = arena_strdup(arena, "TTC-Placeholder");  // Dummy name
        return false;  // Signal that we couldn't parse in-place
    }

    // Parse the font metadata in-place for single-font files
    bool success = parse_font_metadata(placeholder->file_path, placeholder, arena);
    if (success) {
        placeholder->is_placeholder = false;  // No longer a placeholder
        return true;
    }

    return false;
}

// Lazy loading: parse a single font file on demand
static FontEntry* lazy_load_font(FontDatabase* db, const char* file_path) {
    if (!db || !file_path) return NULL;

    // Check if already loaded
    FontEntry search_key = {.file_path = (char*)file_path};
    FontEntry** existing = (FontEntry**)hashmap_get(db->file_paths, &search_key);
    if (existing && *existing) {
        #ifdef FONT_DEBUG_VERBOSE
        log_debug("Font already loaded: %s", file_path);
        #endif
        return *existing;
    }

    // Parse the font now
    FontEntry* font_entry = pool_calloc(db->font_pool, sizeof(FontEntry));
    if (!font_entry) return NULL;

    FontFormat format = detect_font_format(file_path);
    bool parsed = false;

    if (format == FONT_FORMAT_TTC) {
        #ifdef FONT_DEBUG_VERBOSE
        log_debug("Lazy loading TTC file: %s", file_path);
        #endif
        // For TTC files, we'll parse the first font for now
        // (In a full implementation, you'd need to handle multiple fonts per file)
        parsed = parse_ttc_font_metadata(file_path, db, db->string_arena);
        if (parsed && db->all_fonts->length > 0) {
            // Return the last added font (most recently parsed)
            font_entry = (FontEntry*)db->all_fonts->data[db->all_fonts->length - 1];
        }
    } else {
        #ifdef FONT_DEBUG_VERBOSE
        log_debug("Lazy loading font file: %s", file_path);
        #endif
        parsed = parse_font_metadata(file_path, font_entry, db->string_arena);
        if (parsed) {
            arraylist_append(db->all_fonts, font_entry);
            hashmap_set(db->file_paths, &font_entry);
        }
    }

    if (!parsed) {
        log_debug("Failed to lazy load font: %s", file_path);
        return NULL;
    }

    return font_entry;
}

static void organize_fonts_into_families(FontDatabase* db) {
    for (size_t i = 0; i < db->all_fonts->length; i++) {
        FontEntry* font = (FontEntry*)db->all_fonts->data[i];
        if (!font || !font->family_name) continue;

        // Find or create font family
        FontFamily search_key = {.family_name = font->family_name};
        FontFamily* family = (FontFamily*)hashmap_get(db->families, &search_key);

        if (!family) {
            // Create new family
            family = pool_calloc(db->font_pool, sizeof(FontFamily));
            if (!family) continue;

            family->family_name = font->family_name;  // Already arena-allocated
            family->fonts = arraylist_new(0);
            family->aliases = arraylist_new(0);
            family->is_system_family = true;  // Assume system fonts for now

            if (!family->fonts || !family->aliases) continue;

            hashmap_set(db->families, family);
            log_debug("Created font family: %s", family->family_name);
        }

        arraylist_append(family->fonts, font);

        // Add PostScript name mapping if available
        if (font->postscript_name) {
            hashmap_set(db->postscript_names, &font);
        }

        // Add file path mapping
        hashmap_set(db->file_paths, &font);
    }
}

bool font_database_scan(FontDatabase* db) {
    if (!db) return false;

    log_info("Starting font database scan with priority loading");

    // Add platform-specific directories
    add_platform_font_directories(db);

    // PHASE 1: Quick scan to identify all font files (no parsing yet)
    log_debug("Phase 1: Building font file inventory");
    for (size_t i = 0; i < db->scan_directories->length; i++) {
        const char* directory = (const char*)db->scan_directories->data[i];

        // Use shallow scan for most directories, deeper for system font dirs
        int scan_depth = 1;
        if (strstr(directory, "/System/Library/Fonts") ||
            strstr(directory, "/Library/Fonts") ||
            strstr(directory, "/usr/share/fonts") ||  // Linux system fonts need depth 3
            strstr(directory, "supplemental") || strstr(directory, "Supplemental")) {
            scan_depth = 3;  // Need 3 levels: /usr/share/fonts → truetype → dejavu
        }

        scan_directory_recursive(db, directory, scan_depth);

        // Limit total font files to prevent excessive scanning
        if (db->all_fonts->length > 300) {
            log_debug("Font file limit reached: found %d files", db->all_fonts->length);
            break;
        }
    }

    // PHASE 2: Parse priority fonts immediately
    log_debug("Phase 2: Parsing priority fonts (%d total files found)", db->all_fonts->length);
    int priority_fonts_parsed = 0;
    for (size_t i = 0; i < db->all_fonts->length; i++) {
        FontEntry* font = (FontEntry*)db->all_fonts->data[i];
        if (font && font->is_placeholder && font->family_name &&
            is_priority_font_family(font->family_name)) {

            // Check if TTC file - needs special handling
            FontFormat format = detect_font_format(font->file_path);
            if (format == FONT_FORMAT_TTC) {
                // Parse TTC file and add all fonts to database
                log_debug("Parsing priority TTC font: %s (family: %s)", font->file_path, font->family_name);
                if (parse_ttc_font_metadata(font->file_path, db, db->string_arena)) {
                    priority_fonts_parsed++;
                    // Mark the placeholder as processed
                    font->is_placeholder = false;
                    font->family_name = arena_strdup(db->string_arena, "TTC-Parsed");
                }
            } else {
                // Parse single-font file fully
                if (parse_placeholder_font(font, db->string_arena)) {
                    priority_fonts_parsed++;

                    #ifdef FONT_DEBUG_VERBOSE
                    log_debug("Parsed priority font: %s (%s)",
                              font->family_name, font->file_path);
                    #endif
                }
            }

            if (priority_fonts_parsed >= 20) {  // Reasonable limit for priority fonts
                log_debug("Priority font limit reached: parsed %d priority fonts", priority_fonts_parsed);
                break;
            }
        }
    }

#ifdef _WIN32
    // Also scan Windows registry
    scan_windows_registry_fonts(db);
#endif

    // PHASE 3: Organize parsed priority fonts into families
    if (priority_fonts_parsed > 0) {
        log_debug("Phase 3: Organizing %d priority fonts into families", priority_fonts_parsed);
        organize_fonts_into_families(db);
    }

    db->last_scan = time(NULL);
    db->cache_dirty = true;

    log_info("Font scan completed: found %zu font files (%d priority fonts parsed)",
        db->all_fonts->length, priority_fonts_parsed);

    return true;
}

static float calculate_match_score(FontEntry* font, FontDatabaseCriteria* criteria) {
    float score = 0.0f;

    // Family name match (highest priority - 40 points max)
    if (string_match_ignore_case(font->family_name, criteria->family_name)) {
        score += 40.0f;  // Exact match
    } else {
        // Check for partial matches or generic families
        for (int i = 0; generic_families[i].generic; i++) {
            if (string_match_ignore_case(criteria->family_name, generic_families[i].generic)) {
                for (int j = 0; generic_families[i].preferred[j]; j++) {
                    if (string_match_ignore_case(font->family_name, generic_families[i].preferred[j])) {
                        score += 25.0f - (j * 2.0f);  // Prefer earlier matches
                        break;
                    }
                }
                break;
            }
        }
    }

    // Weight matching (20 points max)
    if (criteria->weight > 0) {
        int weight_diff = abs(font->weight - criteria->weight);
        if (weight_diff == 0) {
            score += 20.0f;
        } else if (weight_diff <= 100) {
            score += 20.0f - (weight_diff / 100.0f * 10.0f);
        } else if (weight_diff <= 200) {
            score += 10.0f - ((weight_diff - 100) / 100.0f * 5.0f);
        }
    } else {
        score += 20.0f;  // No preference
    }

    // Style matching (15 points max)
    if (font->style == criteria->style) {
        score += 15.0f;
    } else if (criteria->style == FONT_STYLE_NORMAL && font->style != FONT_STYLE_NORMAL) {
        // Prefer normal style when requested but allow others
        score += 5.0f;
    }

    // Monospace preference (10 points max)
    if (criteria->prefer_monospace) {
        if (font->is_monospace) {
            score += 10.0f;
        } else {
            score -= 5.0f;  // Penalty for non-monospace when monospace preferred
        }
    } else if (font->is_monospace) {
        score -= 2.0f;  // Slight penalty for monospace when not preferred
    }

    // Unicode support (15 points max, or disqualifying)
    if (criteria->required_codepoint != 0) {
        if (font_entry_supports_codepoint(font, criteria->required_codepoint)) {
            score += 15.0f;
        } else {
            return 0.0f;  // Hard requirement - disqualify
        }
    }

    // Language support bonus (5 points max)
    if (criteria->language && font_supports_language(font, criteria->language)) {
        score += 5.0f;
    }

    // Standard font preference (10 points max)
    // Prefer standard variants over Unicode/specialty variants for better browser compatibility
    if (font->file_path) {
        const char* filename = strrchr(font->file_path, '/');
        filename = filename ? filename + 1 : font->file_path;

        // Penalty for Unicode variants when standard font requested
        if (strstr(filename, "Unicode") && !strstr(criteria->family_name, "Unicode")) {
            score -= 8.0f;  // Significant penalty for Unicode variants
        }

        // Penalty for oversized font files (likely comprehensive Unicode fonts)
        if (font->file_size > 5 * 1024 * 1024) {  // > 5MB
            score -= 5.0f;  // Penalty for very large fonts
        }

        // Bonus for exact filename matches (e.g., "Arial.ttf" for "Arial")
        char expected_filename[256];
        snprintf(expected_filename, sizeof(expected_filename), "%s.ttf", criteria->family_name);
        if (strcasecmp(filename, expected_filename) == 0) {
            score += 10.0f;  // Bonus for exact filename match
        }
    }

    return score;
}

FontDatabaseResult font_database_find_best_match(FontDatabase* db, FontDatabaseCriteria* criteria) {
    FontDatabaseResult result = {0};

    if (!db || !criteria || !criteria->family_name) {
        return result;
    }

    float best_score = 0.0f;
    FontEntry* best_font = NULL;
    bool exact_family = false;

    // First, check if we have the family loaded already
    FontFamily search_key = {.family_name = criteria->family_name};
    FontFamily* family = (FontFamily*)hashmap_get(db->families, &search_key);

    // If family not found, try lazy loading ALL placeholder fonts that match
    if (!family && db->all_fonts->length > 0) {
        #ifdef FONT_DEBUG_VERBOSE
        printf("DEBUG: Family '%s' not found, attempting lazy loading\n", criteria->family_name);
        #endif

        // Parse ALL placeholder fonts matching the requested family to get all variants
        int parsed_count = 0;
        bool found_any = false;
        for (size_t i = 0; i < db->all_fonts->length; i++) {
            FontEntry* placeholder = (FontEntry*)db->all_fonts->data[i];
            if (placeholder && placeholder->is_placeholder) {
                // Check if placeholder family name matches what we're looking for
                bool potential_match = false;
                if (placeholder->family_name) {
                    potential_match = string_match_ignore_case(placeholder->family_name, criteria->family_name);
                }

                if (!potential_match) continue;

                // For TTC files, trigger full lazy loading which parses all fonts in the collection
                FontFormat format = detect_font_format(placeholder->file_path);
                if (format == FONT_FORMAT_TTC) {
                    #ifdef FONT_DEBUG_VERBOSE
                    log_debug("Lazy loading TTC placeholder: %s", placeholder->file_path);
                    #endif
                    FontEntry* loaded = lazy_load_font(db, placeholder->file_path);
                    if (loaded) {
                        found_any = true;
                        parsed_count += 5;  // Count as multiple fonts
                    }
                } else {
                    // For single-font files, parse in-place
                    if (parse_placeholder_font(placeholder, db->string_arena)) {
                        parsed_count++;
                        found_any = true;
                    }
                }
            }
        }

        // Organize all newly parsed fonts into families
        if (found_any) {
            organize_fonts_into_families(db);
            #ifdef FONT_DEBUG_VERBOSE
            log_debug("Lazy loaded %d fonts for family '%s'", parsed_count, criteria->family_name);
            #endif
        }
    }

    // First pass: Search through all fonts to find best match
    for (size_t i = 0; i < db->all_fonts->length; i++) {
        FontEntry* font = (FontEntry*)db->all_fonts->data[i];
        if (!font) continue;

        float score = calculate_match_score(font, criteria);
        if (score > best_score && score >= FONT_MATCH_SCORE_THRESHOLD) {
            best_score = score;
            best_font = font;
            exact_family = string_match_ignore_case(font->family_name, criteria->family_name);
        }
    }

    // If we found a family match but style/weight is wrong, try lazy loading more variants
    // This handles cases where only some variants were loaded during priority scan
    const float STYLE_WEIGHT_PENALTY = 20.0f;  // penalty from calculate_match_score
    if (best_font && exact_family && best_score < (100.0f - STYLE_WEIGHT_PENALTY)) {
        // Poor style/weight match - try lazy loading more variants
        bool loaded_more = false;
        for (size_t i = 0; i < db->all_fonts->length; i++) {
            FontEntry* placeholder = (FontEntry*)db->all_fonts->data[i];
            if (placeholder && placeholder->is_placeholder && placeholder->family_name &&
                string_match_ignore_case(placeholder->family_name, criteria->family_name)) {

                // For TTC files, use lazy_load_font which handles TTC properly
                FontFormat format = detect_font_format(placeholder->file_path);
                if (format == FONT_FORMAT_TTC) {
                    if (lazy_load_font(db, placeholder->file_path)) {
                        loaded_more = true;
                    }
                } else {
                    // For single-font files, parse in-place
                    if (parse_placeholder_font(placeholder, db->string_arena)) {
                        loaded_more = true;
                    }
                }
            }
        }

        if (loaded_more) {
            organize_fonts_into_families(db);

            // Re-search for better match
            best_score = 0.0f;
            best_font = NULL;
            for (size_t i = 0; i < db->all_fonts->length; i++) {
                FontEntry* font = (FontEntry*)db->all_fonts->data[i];
                if (!font) continue;

                float score = calculate_match_score(font, criteria);
                if (score > best_score && score >= FONT_MATCH_SCORE_THRESHOLD) {
                    best_score = score;
                    best_font = font;
                }
            }
        }
    }

    result.font = best_font;
    result.match_score = best_score / 100.0f;  // Normalize to 0-1 range
    result.exact_family_match = exact_family;
    result.requires_synthesis = false;  // TODO: Implement synthesis detection
    result.synthetic_style = NULL;

    // Commented out verbose font matching logs - called very frequently during layout
    // if (best_font) {
    //     log_debug("Best font match for '%s': %s (score: %.2f)",
    //         criteria->family_name, best_font->family_name, result.match_score);
    // } else {
    //     log_debug("No font match found for: %s", criteria->family_name);
    // }

    return result;
}

ArrayList* font_database_find_all_matches(FontDatabase* db, const char* family_name) {
    if (!db || !family_name) return NULL;

    ArrayList* matches = arraylist_new(0);
    if (!matches) return NULL;

    FontFamily search_key = {.family_name = (char*)family_name};
    FontFamily* family = (FontFamily*)hashmap_get(db->families, &search_key);

    if (family && family->fonts) {
        for (size_t i = 0; i < family->fonts->length; i++) {
            FontEntry* font = (FontEntry*)family->fonts->data[i];
            if (font) {
                arraylist_append(matches, font);
            }
        }
    }

    return matches;
}

FontEntry* font_database_get_by_postscript_name(FontDatabase* db, const char* ps_name) {
    if (!db || !ps_name) return NULL;

    FontEntry search_key = {.postscript_name = (char*)ps_name};
    FontEntry** entry_ptr = (FontEntry**)hashmap_get(db->postscript_names, &search_key);
    return entry_ptr ? *entry_ptr : NULL;
}

FontEntry* font_database_get_by_file_path(FontDatabase* db, const char* file_path) {
    if (!db || !file_path) return NULL;

    FontEntry search_key = {.file_path = (char*)file_path};
    FontEntry** entry_ptr = (FontEntry**)hashmap_get(db->file_paths, &search_key);
    return entry_ptr ? *entry_ptr : NULL;
}

bool font_entry_supports_codepoint(FontEntry* font, uint32_t codepoint) {
    if (!font || !font->unicode_ranges) {
        // If no Unicode info, assume basic ASCII support
        return (codepoint >= 0x0020 && codepoint <= 0x007E);
    }

    FontUnicodeRange* range = font->unicode_ranges;
    while (range) {
        if (codepoint >= range->start_codepoint && codepoint <= range->end_codepoint) {
            return true;
        }
        range = range->next;
    }

    return false;
}

bool font_supports_language(FontEntry* font, const char* language) {
    if (!font || !language || !font->unicode_ranges) {
        return false;
    }

    // Check Unicode blocks for language support
    for (int i = 0; unicode_blocks[i].name; i++) {
        for (int j = 0; unicode_blocks[i].languages[j]; j++) {
            if (strcmp(language, unicode_blocks[i].languages[j]) == 0) {
                // Check if font supports this Unicode block
                if (font_entry_supports_codepoint(font, unicode_blocks[i].start)) {
                    return true;
                }
            }
        }
    }

    return false;
}

ArrayList* font_get_available_families(FontDatabase* db) {
    if (!db || !db->families) return NULL;

    ArrayList* families = arraylist_new(0);
    if (!families) return NULL;

    size_t iter = 0;
    void* item;
    while (hashmap_iter(db->families, &iter, &item)) {
        FontFamily* family = (FontFamily*)item;
        arraylist_append(families, family->family_name);
    }

    return families;
}

bool font_is_file_changed(FontEntry* font) {
    if (!font || !font->file_path) return false;

    struct stat file_stat;
    if (stat(font->file_path, &file_stat) != 0) {
        return true;  // File doesn't exist anymore
    }

    return (file_stat.st_mtime != font->file_mtime ||
            file_stat.st_size != font->file_size);
}

void font_database_refresh_changed_files(FontDatabase* db) {
    if (!db) return;

    // TODO: Implement incremental refresh
    log_debug("Font database refresh not yet implemented");
}

// Cache implementation
bool font_database_load_cache(FontDatabase* db) {
    // For now, keep this simple to avoid cache complexity issues
    // The main performance win is the global database singleton
    log_debug("Font cache loading not yet implemented - using singleton instead");
    return false;
}

bool font_database_save_cache(FontDatabase* db) {
    // For now, keep this simple - the main win is avoiding rescanning
    log_debug("Font cache saving not yet implemented - relying on singleton");
    return false;
}

// Utility functions
const char* font_format_to_string(FontFormat format) {
    switch (format) {
        case FONT_FORMAT_TTF: return "TTF";
        case FONT_FORMAT_OTF: return "OTF";
        case FONT_FORMAT_TTC: return "TTC";
        case FONT_FORMAT_WOFF: return "WOFF";
        case FONT_FORMAT_WOFF2: return "WOFF2";
        default: return "Unknown";
    }
}

const char* font_style_to_string(FontStyle style) {
    switch (style) {
        case FONT_STYLE_NORMAL: return "Normal";
        case FONT_STYLE_ITALIC: return "Italic";
        case FONT_STYLE_OBLIQUE: return "Oblique";
        default: return "Unknown";
    }
}

FontStyle font_style_from_string(const char* style_str) {
    if (!style_str) return FONT_STYLE_NORMAL;

    if (strcasecmp(style_str, "italic") == 0) return FONT_STYLE_ITALIC;
    if (strcasecmp(style_str, "oblique") == 0) return FONT_STYLE_OBLIQUE;
    return FONT_STYLE_NORMAL;
}

void font_database_set_cache_path(FontDatabase* db, const char* cache_path) {
    if (!db || !cache_path) return;

    db->cache_file_path = arena_strdup(db->string_arena, cache_path);
}

bool font_database_cache_is_valid(FontDatabase* db) {
    // TODO: Implement cache validation
    return false;
}

void font_database_invalidate_cache(FontDatabase* db) {
    if (!db) return;
    db->cache_dirty = true;
}

size_t font_database_get_font_count(FontDatabase* db) {
    return db ? db->all_fonts->length : 0;
}

size_t font_database_get_family_count(FontDatabase* db) {
    return db ? hashmap_count(db->families) : 0;
}

void font_database_print_statistics(FontDatabase* db) {
    if (!db) return;

    log_info("Font Database Statistics:");
    log_info("  Total fonts: %zu", font_database_get_font_count(db));
    log_info("  Font families: %zu", font_database_get_family_count(db));
    log_info("  Scan directories: %zu", db->scan_directories->length);
    log_info("  Last scan: %s", db->last_scan ? ctime(&db->last_scan) : "Never");
    log_info("  Cache dirty: %s", db->cache_dirty ? "Yes" : "No");
}

// Global font database singleton for performance
FontDatabase* font_database_get_global() {
    if (!g_font_db_initialized) {
        // Create global pools for font database
        Pool* global_pool = pool_create();
        Arena* global_font_arena = arena_create(global_pool, 64 * 1024, 1024 * 1024);  // 64KB->1MB chunks

        if (global_pool && global_font_arena) {
            g_global_font_db = font_database_create(global_pool, global_font_arena);
            if (g_global_font_db) {
                // Set cache path for persistence
                char cache_path[512];
                snprintf(cache_path, sizeof(cache_path), "%s/.lambda_font_cache", getenv("HOME") ?: "/tmp");
                font_database_set_cache_path(g_global_font_db, cache_path);

                // Try to load from cache first
                if (!font_database_load_cache(g_global_font_db)) {
                    // If cache load fails, scan fonts
                    log_info("Font cache miss, scanning system fonts...");
                    font_database_scan(g_global_font_db);
                    // Save cache for next time
                    font_database_save_cache(g_global_font_db);
                } else {
                    log_info("Font database loaded from cache");
                }
            }
        }

        g_font_db_initialized = true;
    }

    return g_global_font_db;
}

void font_database_cleanup_global() {
    if (g_global_font_db) {
        // Save cache before cleanup
        font_database_save_cache(g_global_font_db);

        // Note: We don't actually free the database here since it uses pools
        // The pools will be cleaned up when the program exits
        g_global_font_db = NULL;
        g_font_db_initialized = false;
    }
}
