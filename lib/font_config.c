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
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

// Platform-specific includes
#ifdef __APPLE__
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__)
#include <dirent.h>
#include <unistd.h>
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

// Platform-specific font directories
static const char* macos_font_dirs[] = {
    "/System/Library/Fonts",
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
    return hashmap_xxhash3(family->family_name, strlen(family->family_name), seed0, seed1);
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
static void add_macos_user_fonts(ArrayList *directories) {
    const char *home = getenv("HOME");
    if (home) {
        StrBuf *user_fonts = strbuf_create(home);
        strbuf_append_str(user_fonts, "/Library/Fonts");
        arraylist_append(directories, user_fonts->str);
        // Note: strbuf_cstr returns internal pointer, safe until strbuf is freed
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
static void add_linux_user_fonts(ArrayList *directories) {
    const char *home = getenv("HOME");
    if (home) {
        // ~/.fonts (traditional)
        StrBuf *user_fonts_old = strbuf_create(home);
        strbuf_append_str(user_fonts_old, "/.fonts");
        arraylist_append(directories, user_fonts_old->str);
        
        // ~/.local/share/fonts (XDG standard)
        StrBuf *user_fonts_xdg = strbuf_create(home);
        strbuf_append_str(user_fonts_xdg, "/.local/share/fonts");
        arraylist_append(directories, user_fonts_xdg->str);
    }
    
    // Check XDG_DATA_HOME
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        StrBuf *xdg_fonts = strbuf_create(xdg_data_home);
        strbuf_append_str(xdg_fonts, "/fonts");
        arraylist_append(directories, xdg_fonts->str);
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
        arraylist_add(directories, strdup(windows_fonts));
    }
    
    // User fonts directory  
    if (SHGetFolderPathA(NULL, 0x001c, NULL, 0, user_fonts) == 0) {  // CSIDL_LOCAL_APPDATA, SHGFP_TYPE_CURRENT, S_OK
        strcat(user_fonts, "\\Microsoft\\Windows\\Fonts");
        arraylist_add(directories, strdup(user_fonts));
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
            strcpy(full_path, font_file);
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
        name_id = be16toh_local(name_id);
        length = be16toh_local(length);
        offset = be16toh_local(offset);
        
        // We're interested in English names (platform 3, language 0x0409 or 0)
        if (platform_id == 3 && (language_id == 0x0409 || language_id == 0)) {
            long current_pos = ftell(file);
            long string_pos = name_table->offset + string_offset + offset;
            
            if (fseek(file, string_pos, SEEK_SET) == 0) {
                char *name_buffer = arena_alloc(arena, length + 1);
                if (name_buffer && fread(name_buffer, 1, length, file) == length) {
                    name_buffer[length] = '\0';
                    
                    // Convert UTF-16 to UTF-8 if needed (simplified)
                    // For now, assume ASCII subset
                    char *ascii_name = arena_alloc(arena, (length / 2) + 1);
                    if (ascii_name) {
                        int ascii_len = 0;
                        for (int j = 1; j < length; j += 2) {  // Skip high bytes
                            if (name_buffer[j] >= 32 && name_buffer[j] < 127) {
                                ascii_name[ascii_len++] = name_buffer[j];
                            }
                        }
                        ascii_name[ascii_len] = '\0';
                        
                        switch (name_id) {
                            case NAME_ID_FAMILY_NAME:
                                if (!entry->family_name && ascii_len > 0) {
                                    entry->family_name = ascii_name;
                                    log_debug("Parsed family name: %s", entry->family_name);
                                }
                                break;
                            case NAME_ID_SUBFAMILY_NAME:
                                if (!entry->subfamily_name && ascii_len > 0) {
                                    entry->subfamily_name = ascii_name;
                                    log_debug("Parsed subfamily name: %s", entry->subfamily_name);
                                }
                                break;
                            case NAME_ID_POSTSCRIPT_NAME:
                                if (!entry->postscript_name && ascii_len > 0) {
                                    entry->postscript_name = ascii_name;
                                    log_debug("Parsed PostScript name: %s", entry->postscript_name);
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
    
    log_debug("Parsed OS/2 table: weight=%d, italic=%s", 
        entry->weight, entry->style == FONT_STYLE_ITALIC ? "yes" : "no");
    
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
    
    log_debug("Parsed cmap table with %d subtables", num_tables);
    return true;
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
    
    if (!db->all_fonts || !db->scan_directories) {
        log_error("Failed to create font database arrays");
        if (db->families) hashmap_free(db->families);
        if (db->postscript_names) hashmap_free(db->postscript_names);
        if (db->file_paths) hashmap_free(db->file_paths);
        if (db->all_fonts) arraylist_free(db->all_fonts);
        if (db->scan_directories) arraylist_free(db->scan_directories);
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
    
    // Note: Don't destroy pool/arena - they're managed by caller
}

static void add_platform_font_directories(FontDatabase* db) {
    const char **dirs = NULL;
    
#ifdef __APPLE__
    dirs = macos_font_dirs;
    while (*dirs) {
        font_add_scan_directory(db, *dirs);
        dirs++;
    }
    add_macos_user_fonts(db->scan_directories);
#elif defined(__linux__)
    dirs = linux_font_dirs;
    while (*dirs) {
        font_add_scan_directory(db, *dirs);
        dirs++;
    }
    add_linux_user_fonts(db->scan_directories);
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

static bool is_font_file(const char* filename) {
    if (!filename) return false;
    
    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    
    return (strcasecmp(ext, ".ttf") == 0 ||
            strcasecmp(ext, ".otf") == 0 ||
            strcasecmp(ext, ".ttc") == 0 ||
            strcasecmp(ext, ".woff") == 0 ||
            strcasecmp(ext, ".woff2") == 0);
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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);
        
        struct stat stat_buf;
        if (stat(full_path, &stat_buf) != 0) {
            continue;
        }
        
        if (S_ISDIR(stat_buf.st_mode)) {
            scan_directory_recursive(db, full_path, max_depth - 1);
        } else if (S_ISREG(stat_buf.st_mode) && is_font_file(entry->d_name)) {
            log_debug("Processing font file: %s", full_path);
            FontEntry* font_entry = pool_calloc(db->font_pool, sizeof(FontEntry));
            if (font_entry) {
                if (parse_font_metadata(full_path, font_entry, db->string_arena)) {
                    arraylist_append(db->all_fonts, font_entry);
                    log_debug("Successfully added font: %s (family: %s)", full_path, font_entry->family_name);
                } else {
                    log_debug("Failed to parse font metadata: %s", full_path);
                }
            } else {
                log_debug("Failed to allocate FontEntry for: %s", full_path);
            }
        }
    }
    
    closedir(dir);
#endif
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
    
    log_info("Starting font database scan");
    
    // Add platform-specific directories
    add_platform_font_directories(db);
    
    // Scan all directories
    for (size_t i = 0; i < db->scan_directories->length; i++) {
        const char* directory = (const char*)db->scan_directories->data[i];
        log_debug("Scanning font directory: %s", directory);
        scan_directory_recursive(db, directory, 3);  // Max 3 levels deep
    }
    
#ifdef _WIN32
    // Also scan Windows registry
    scan_windows_registry_fonts(db);
#endif
    
    // Organize fonts into families
    organize_fonts_into_families(db);
    
    db->last_scan = time(NULL);
    db->cache_dirty = true;
    
    log_info("Font scan completed: %zu fonts in %zu families", 
        db->all_fonts->length, hashmap_count(db->families));
    
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
    
    // Search through all fonts
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
    
    result.font = best_font;
    result.match_score = best_score / 100.0f;  // Normalize to 0-1 range
    result.exact_family_match = exact_family;
    result.requires_synthesis = false;  // TODO: Implement synthesis detection
    result.synthetic_style = NULL;
    
    if (best_font) {
        log_debug("Best font match for '%s': %s (score: %.2f)", 
            criteria->family_name, best_font->family_name, result.match_score);
    } else {
        log_debug("No font match found for: %s", criteria->family_name);
    }
    
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
    // TODO: Implement cache loading
    log_debug("Font cache loading not yet implemented");
    return false;
}

bool font_database_save_cache(FontDatabase* db) {
    // TODO: Implement cache saving  
    log_debug("Font cache saving not yet implemented");
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