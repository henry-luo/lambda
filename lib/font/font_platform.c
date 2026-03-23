/**
 * Lambda Unified Font Module — Platform-Specific Font Discovery
 *
 * Adds default system font directories per platform (macOS, Linux, Windows).
 * Provides platform-specific font path fallback using CoreText (macOS),
 * fontconfig dirs (Linux), or registry (Windows).
 *
 * Adapted from lib/font_config.c (add_platform_font_directories) and
 * radiant/font_lookup_platform.c (find_font_path_fallback).
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../strbuf.h"
#include "../memtrack.h"
#include "../str.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h>
#include <strings.h>
#endif
#include <sys/stat.h>

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

// ============================================================================
// Platform font directories
// ============================================================================

static const char* macos_font_dirs[] = {
    "/System/Library/Fonts",
    "/System/Library/Fonts/Supplemental",
    "/Library/Fonts",
    NULL
};

static const char* linux_font_dirs[] = {
    "/usr/share/fonts",
    "/usr/local/share/fonts",
    "/usr/X11R6/lib/X11/fonts",
    NULL
};

// ============================================================================
// Internal helpers
// ============================================================================

static void add_dir_if_exists(FontDatabase* db, const char* path) {
    if (!db || !path) return;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (!db->scan_directories) {
            db->scan_directories = arraylist_new(0);
        }
        // check for duplicates
        for (int i = 0; i < db->scan_directories->length; i++) {
            if (strcmp((const char*)db->scan_directories->data[i], path) == 0) return;
        }
        char* copy = arena_strdup(db->arena, path);
        arraylist_append(db->scan_directories, copy);
    }
}

// ============================================================================
// macOS
// ============================================================================

#ifdef __APPLE__
static void add_macos_dirs(FontDatabase* db) {
    for (int i = 0; macos_font_dirs[i]; i++) {
        add_dir_if_exists(db, macos_font_dirs[i]);
    }

    // user fonts directory
    const char* home = getenv("HOME");
    if (home) {
        StrBuf* user_fonts = strbuf_create(home);
        strbuf_append_str(user_fonts, "/Library/Fonts");
        add_dir_if_exists(db, user_fonts->str);
        strbuf_free(user_fonts);
    }
}

// CoreText-based font path fallback for macOS
static char* find_font_path_macos(const char* font_name) {
    if (!font_name) return NULL;

    CFStringRef cf_name = CFStringCreateWithCString(NULL, font_name, kCFStringEncodingUTF8);
    if (!cf_name) return NULL;

    CTFontDescriptorRef desc = CTFontDescriptorCreateWithNameAndSize(cf_name, 0.0);
    CFRelease(cf_name);
    if (!desc) return NULL;

    CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
    CFRelease(desc);
    if (!url) return NULL;

    char path[1024];
    bool ok = CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, sizeof(path));
    CFRelease(url);

    if (ok) {
        return mem_strdup(path, MEM_CAT_FONT); // caller frees with mem_free()
    }
    return NULL;
}
#endif

// ============================================================================
// Linux
// ============================================================================

#ifdef __linux__
static void add_linux_dirs(FontDatabase* db) {
    for (int i = 0; linux_font_dirs[i]; i++) {
        add_dir_if_exists(db, linux_font_dirs[i]);
    }

    // user font directories
    const char* home = getenv("HOME");
    if (home) {
        StrBuf* dir1 = strbuf_create(home);
        strbuf_append_str(dir1, "/.fonts");
        add_dir_if_exists(db, dir1->str);
        strbuf_free(dir1);

        StrBuf* dir2 = strbuf_create(home);
        strbuf_append_str(dir2, "/.local/share/fonts");
        add_dir_if_exists(db, dir2->str);
        strbuf_free(dir2);
    }

    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        StrBuf* dir = strbuf_create(xdg);
        strbuf_append_str(dir, "/fonts");
        add_dir_if_exists(db, dir->str);
        strbuf_free(dir);
    }
}
#endif

// ============================================================================
// Windows
// ============================================================================

#ifdef _WIN32
static void add_windows_dirs(FontDatabase* db) {
    char path[260];
    if (SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, path) == S_OK) {
        add_dir_if_exists(db, path);
    }
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path) == S_OK) {
        strncat(path, "\\Microsoft\\Windows\\Fonts", sizeof(path) - strlen(path) - 1);
        add_dir_if_exists(db, path);
    }
}

void scan_windows_registry_fonts(FontDatabase* db) {
    HKEY hkey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        0, KEY_READ, &hkey);

    if (result != ERROR_SUCCESS) {
        log_info("scan_windows_registry_fonts: failed to open registry key");
        return;
    }

    DWORD index = 0;
    char font_name[256];
    char font_file[260];
    DWORD name_size, file_size;
    int added = 0;

    while (1) {
        name_size = sizeof(font_name);
        file_size = sizeof(font_file);

        result = RegEnumValueA(hkey, index++, font_name, &name_size,
            NULL, NULL, (LPBYTE)font_file, &file_size);

        if (result != ERROR_SUCCESS) break;

        // convert relative path to absolute if needed
        char full_path[260];
        if (font_file[0] != '\\' && font_file[1] != ':') {
            char windows_dir[260];
            if (SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, windows_dir) == S_OK) {
                snprintf(full_path, sizeof(full_path), "%s\\%s", windows_dir, font_file);
            } else {
                continue;
            }
        } else {
            strncpy(full_path, font_file, sizeof(full_path) - 1);
            full_path[sizeof(full_path) - 1] = '\0';
        }

        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            // check if this file path is already in the database (from directory scan)
            bool already_exists = false;
            for (int i = 0; i < db->all_fonts->length; i++) {
                FontEntry* existing = (FontEntry*)db->all_fonts->data[i];
                if (existing && existing->file_path &&
                    strcasecmp(existing->file_path, full_path) == 0) {
                    already_exists = true;
                    break;
                }
            }
            if (!already_exists) {
                FontEntry* entry = arena_alloc(db->arena, sizeof(FontEntry));
                if (entry) {
                    memset(entry, 0, sizeof(FontEntry));
                    entry->file_path = arena_strdup(db->arena, full_path);
                    entry->is_placeholder = true;
                    entry->weight = 400;
                    entry->style = FONT_SLANT_NORMAL;
                    entry->format = font_detect_format_ext(full_path);
                    // extract family name from filename for lazy matching
                    const char* base = strrchr(full_path, '\\');
                    if (!base) base = strrchr(full_path, '/');
                    if (base) base++; else base = full_path;
                    char buf[256];
                    strncpy(buf, base, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    char* dot = strrchr(buf, '.');
                    if (dot) *dot = '\0';
                    // strip common suffixes like -Regular, -Bold, etc.
                    char* dash = strrchr(buf, '-');
                    if (dash) {
                        if (strcasecmp(dash, "-Regular") == 0 || strcasecmp(dash, "-Bold") == 0 ||
                            strcasecmp(dash, "-Italic") == 0 || strcasecmp(dash, "-BoldItalic") == 0 ||
                            strcasecmp(dash, "-Light") == 0 || strcasecmp(dash, "-Medium") == 0 ||
                            strcasecmp(dash, "-Semibold") == 0 || strcasecmp(dash, "-Thin") == 0) {
                            *dash = '\0';
                        }
                    }
                    entry->family_name = arena_strdup(db->arena, buf);
                    entry->file_size = (size_t)file_stat.st_size;
                    arraylist_append(db->all_fonts, entry);
                    added++;
                }
            }
        }
    }

    RegCloseKey(hkey);
    log_info("scan_windows_registry_fonts: added %d fonts from registry", added);
}

/**
 * find_font_path_windows - Find a font file path by family name using Windows registry
 *
 * Registry value names are like "Arial (TrueType)", "Segoe UI (TrueType)".
 * We match if the value name starts with the requested family name (case-insensitive).
 * Prefers .ttf over .ttc since ThorVG TTF loader doesn't support TTC.
 */
static char* find_font_path_windows(const char* font_name) {
    HKEY hkey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        0, KEY_READ, &hkey);

    if (result != ERROR_SUCCESS) return NULL;

    char fonts_dir[260];
    if (SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, fonts_dir) != S_OK) {
        RegCloseKey(hkey);
        return NULL;
    }

    size_t name_len = strlen(font_name);
    DWORD index = 0;
    char reg_name[256];
    char reg_file[260];
    char* best_path = NULL;

    while (1) {
        DWORD name_size = sizeof(reg_name);
        DWORD file_size = sizeof(reg_file);

        result = RegEnumValueA(hkey, index++, reg_name, &name_size,
            NULL, NULL, (LPBYTE)reg_file, &file_size);
        if (result != ERROR_SUCCESS) break;

        // check if registry name starts with the requested font name (case-insensitive)
        if (_strnicmp(reg_name, font_name, name_len) != 0) continue;

        // after the font name, expect end, " (" for style suffix like "(TrueType)",
        // but NOT another word (to avoid "Arial" matching "Arial Narrow")
        char after = reg_name[name_len];
        if (after != '\0' && !(after == ' ' && reg_name[name_len + 1] == '(')) continue;

        // skip TTC files
        if (strstr(reg_file, ".ttc") || strstr(reg_file, ".TTC")) continue;

        // build absolute path
        char full_path[520];
        if (reg_file[0] != '\\' && (reg_file[1] != ':')) {
            snprintf(full_path, sizeof(full_path), "%s\\%s", fonts_dir, reg_file);
        } else {
            strncpy(full_path, reg_file, sizeof(full_path) - 1);
            full_path[sizeof(full_path) - 1] = '\0';
        }

        // verify file exists
        struct stat s;
        if (stat(full_path, &s) != 0) continue;

        best_path = mem_strdup(full_path, MEM_CAT_FONT);
        break;
    }

    RegCloseKey(hkey);
    return best_path;
}
#endif

// ============================================================================
// Public API
// ============================================================================

void font_platform_add_default_dirs(FontDatabase* db) {
    if (!db) return;

#ifdef __APPLE__
    add_macos_dirs(db);
#elif defined(__linux__)
    add_linux_dirs(db);
#elif defined(_WIN32)
    add_windows_dirs(db);
#endif

    log_info("font_platform: added %d scan directories",
             db->scan_directories ? db->scan_directories->length : 0);
}

char* font_platform_find_fallback(const char* font_name) {
    if (!font_name) return NULL;

#ifdef __APPLE__
    return find_font_path_macos(font_name);
#elif defined(_WIN32)
    return find_font_path_windows(font_name);
#else
    // Linux: handled by font_database_find_best_match_internal instead
    (void)font_name;
    return NULL;
#endif
}

// ============================================================================
// Platform-specific font metrics
// ============================================================================

#ifdef __APPLE__

/**
 * get_font_metrics_platform - Get font metrics using CoreText (macOS)
 *
 * This matches Chrome's Blink implementation exactly:
 * 1. Get ascent/descent from CTFontGetAscent/CTFontGetDescent (Skia does this)
 * 2. Round each component individually (SkScalarRoundToScalar)
 * 3. Apply 15% adjustment for Times, Helvetica, Courier (crbug.com/445830)
 * 4. LineSpacing = rounded_ascent + rounded_descent + rounded_leading
 *
 * See: third_party/blink/renderer/platform/fonts/font_metrics.cc
 *      third_party/blink/renderer/platform/fonts/simple_font_data.cc
 *      third_party/skia/src/ports/SkScalerContext_mac_ct.cpp
 *
 * @param font_family The font family name
 * @param font_size The font size in CSS pixels
 * @param out_ascent Output: rounded ascent (after macOS hack if applicable)
 * @param out_descent Output: rounded descent
 * @param out_line_height Output: final line height (line spacing)
 * @return 1 if metrics were successfully retrieved, 0 otherwise
 */
int get_font_metrics_platform(const char* font_family, float font_size,
                              float* out_ascent, float* out_descent, float* out_line_height) {
    if (!font_family || font_size <= 0) return 0;

    // create CFString from font family name
    CFStringRef cf_family = CFStringCreateWithCString(NULL, font_family, kCFStringEncodingUTF8);
    if (!cf_family) return 0;

    // create CTFont at the specified size
    CTFontRef ct_font = CTFontCreateWithName(cf_family, (CGFloat)font_size, NULL);
    CFRelease(cf_family);

    if (!ct_font) {
        log_debug("CoreText: could not create font for '%s'", font_family);
        return 0;
    }

    // get metrics from CoreText (this is what Skia does on macOS)
    CGFloat ct_ascent  = CTFontGetAscent(ct_font);
    CGFloat ct_descent = CTFontGetDescent(ct_font);
    CGFloat ct_leading = CTFontGetLeading(ct_font);

    CFRelease(ct_font);

    // round each component individually (matches Chrome's SkScalarRoundToScalar)
    float ascent  = roundf((float)ct_ascent);
    float descent = roundf((float)ct_descent);
    float leading = roundf((float)ct_leading);

    // macOS-specific adjustment for classic Mac fonts
    // Chrome applies a 15% adjustment to ascent ONLY for Apple's classic fonts:
    // "Times", "Helvetica", "Courier" — to match their Microsoft equivalents
    // (the de facto web standard). See: font_metrics.cc lines 129-142, crbug.com/445830
    if (strcmp(font_family, "Times") == 0 ||
        strcmp(font_family, "Helvetica") == 0 ||
        strcmp(font_family, "Courier") == 0) {
        float adjustment = floorf(((ascent + descent) * 0.15f) + 0.5f);
        ascent += adjustment;
        log_debug("CoreText macOS font hack: +%.0f for %s (asc=%.0f, desc=%.0f)",
                  adjustment, font_family, ascent, descent);
    }

    float line_height = ascent + descent + leading;

    if (out_ascent) *out_ascent = ascent;
    if (out_descent) *out_descent = descent;
    if (out_line_height) *out_line_height = line_height;

    log_debug("CoreText metrics for %s@%.1f: ascent=%.0f, descent=%.0f, leading=%.0f, lineHeight=%.0f",
              font_family, font_size, ascent, descent, leading, line_height);

    return 1;
}

#else

/**
 * Non-macOS stub: Font metrics via FreeType only.
 * Returns 0 to indicate platform metrics not available.
 */
int get_font_metrics_platform(const char* font_family, float font_size,
                              float* out_ascent, float* out_descent, float* out_line_height) {
    (void)font_family; (void)font_size;
    (void)out_ascent; (void)out_descent; (void)out_line_height;
    return 0;
}

#endif

// ============================================================================
// Platform codepoint font lookup
// ============================================================================

#ifdef __APPLE__

char* font_platform_find_codepoint_font(uint32_t codepoint, int* out_face_index) {
    if (out_face_index) *out_face_index = 0;

    // encode codepoint as UTF-16 for CFString
    UniChar utf16[2];
    CFIndex utf16_len;
    if (codepoint <= 0xFFFF) {
        utf16[0] = (UniChar)codepoint;
        utf16_len = 1;
    } else if (codepoint <= 0x10FFFF) {
        // surrogate pair
        codepoint -= 0x10000;
        utf16[0] = (UniChar)(0xD800 + (codepoint >> 10));
        utf16[1] = (UniChar)(0xDC00 + (codepoint & 0x3FF));
        utf16_len = 2;
    } else {
        return NULL;
    }

    CFStringRef str = CFStringCreateWithCharacters(NULL, utf16, utf16_len);
    if (!str) return NULL;

    // create a base font (Times New Roman at 12pt) and ask CoreText for a fallback
    CTFontRef base_font = CTFontCreateWithName(CFSTR("Times New Roman"), 12.0, NULL);
    if (!base_font) {
        CFRelease(str);
        return NULL;
    }

    CTFontRef fallback = CTFontCreateForString(base_font, str, CFRangeMake(0, utf16_len));
    CFRelease(str);
    CFRelease(base_font);

    if (!fallback) return NULL;

    // get the font URL from the fallback font
    CTFontDescriptorRef desc = CTFontCopyFontDescriptor(fallback);
    CFRelease(fallback);
    if (!desc) return NULL;

    CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
    CFRelease(desc);
    if (!url) return NULL;

    char path[1024];
    bool ok = CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, sizeof(path));
    CFRelease(url);

    if (ok && path[0]) {
        log_debug("font_platform: codepoint U+%04X → '%s'", codepoint, path);
        return mem_strdup(path, MEM_CAT_FONT);
    }
    return NULL;
}

#else

char* font_platform_find_codepoint_font(uint32_t codepoint, int* out_face_index) {
    (void)codepoint;
    if (out_face_index) *out_face_index = 0;
    return NULL;
}

#endif
