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

#include <stdio.h>
#include <string.h>

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
        return arena_strdup(NULL, path); // caller must handle memory
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
