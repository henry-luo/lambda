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
#include <sys/stat.h>
#endif

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
#else
    // Linux/Windows: scan database directories for a matching file
    // (handled by font_database_find_best_match_internal instead)
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
