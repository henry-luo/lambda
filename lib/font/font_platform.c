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

        // extract family name from registry value name (e.g. "Segoe UI Emoji (TrueType)" → "Segoe UI Emoji")
        char reg_family[256];
        strncpy(reg_family, font_name, sizeof(reg_family) - 1);
        reg_family[sizeof(reg_family) - 1] = '\0';
        // strip " (TrueType)", " (OpenType)", " (All)", etc.
        char* paren = strrchr(reg_family, '(');
        if (paren && paren > reg_family) {
            // trim trailing space before '('
            char* trim = paren - 1;
            while (trim > reg_family && *trim == ' ') trim--;
            *(trim + 1) = '\0';
        }

        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            // check if this file path is already in the database (from directory scan)
            FontEntry* existing_entry = NULL;
            for (int i = 0; i < db->all_fonts->length; i++) {
                FontEntry* existing = (FontEntry*)db->all_fonts->data[i];
                if (existing && existing->file_path &&
                    strcasecmp(existing->file_path, full_path) == 0) {
                    existing_entry = existing;
                    break;
                }
            }
            if (existing_entry) {
                // update family name from registry (more accurate than filename heuristic)
                if (reg_family[0] && existing_entry->is_placeholder) {
                    existing_entry->family_name = arena_strdup(db->arena, reg_family);
                }
            } else {
                FontEntry* entry = arena_alloc(db->arena, sizeof(FontEntry));
                if (entry) {
                    memset(entry, 0, sizeof(FontEntry));
                    entry->file_path = arena_strdup(db->arena, full_path);
                    entry->is_placeholder = true;
                    entry->weight = 400;
                    entry->style = FONT_SLANT_NORMAL;
                    entry->format = font_detect_format_ext(full_path);
                    // use registry family name (authoritative on Windows)
                    entry->family_name = arena_strdup(db->arena, reg_family[0] ? reg_family : "Unknown");
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

float get_cjk_system_line_height(float font_size) {
    if (font_size <= 0) return 0;

    // Chrome blends system CJK font metrics for lines containing CJK characters.
    // On macOS, PingFang SC is the default CJK system font.
    static const char* cjk_fonts[] = { "PingFang SC", "Hiragino Sans GB", NULL };
    for (int i = 0; cjk_fonts[i]; i++) {
        float ascent, descent, lh;
        if (get_font_metrics_platform(cjk_fonts[i], font_size, &ascent, &descent, &lh)) {
            log_debug("CJK system line-height: %s@%.1f → %.0f (asc=%.0f, desc=%.0f)",
                      cjk_fonts[i], font_size, lh, ascent, descent);
            return lh;
        }
    }
    return 0;
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

float get_cjk_system_line_height(float font_size) {
    (void)font_size;
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
        uint32_t cp = codepoint - 0x10000;
        utf16[0] = (UniChar)(0xD800 + (cp >> 10));
        utf16[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
        utf16_len = 2;
    } else {
        return NULL;
    }

    CFStringRef str = CFStringCreateWithCharacters(NULL, utf16, utf16_len);
    if (!str) return NULL;

    // cache the base font for repeated fallback lookups (creating a CTFont
    // per call is expensive — CoreText does font catalog lookups each time)
    static CTFontRef s_base_font = NULL;
    if (!s_base_font) {
        s_base_font = CTFontCreateWithName(CFSTR("Times New Roman"), 12.0, NULL);
        if (!s_base_font) {
            CFRelease(str);
            return NULL;
        }
    }

    CTFontRef fallback = CTFontCreateForString(s_base_font, str, CFRangeMake(0, utf16_len));
    CFRelease(str);

    if (!fallback) return NULL;

    // get the font URL and PostScript name from the fallback font
    CTFontDescriptorRef desc = CTFontCopyFontDescriptor(fallback);
    CFStringRef ps_name = CTFontCopyPostScriptName(fallback);
    CFRelease(fallback);
    if (!desc) {
        if (ps_name) CFRelease(ps_name);
        return NULL;
    }

    CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
    CFRelease(desc);
    if (!url) {
        if (ps_name) CFRelease(ps_name);
        return NULL;
    }

    char path[1024];
    bool ok = CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, sizeof(path));

    // for TTC/OTC collections, determine the correct face index by matching
    // the PostScript name against all faces in the collection file
    if (ok && path[0] && out_face_index && ps_name) {
        size_t path_len = strlen(path);
        bool is_collection = (path_len > 4 &&
            (strcasecmp(path + path_len - 4, ".ttc") == 0 ||
             strcasecmp(path + path_len - 4, ".otc") == 0));
        if (is_collection) {
            CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromURL(url);
            if (descs) {
                CFIndex count = CFArrayGetCount(descs);
                for (CFIndex i = 0; i < count; i++) {
                    CTFontDescriptorRef face_desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, i);
                    CFStringRef face_ps = (CFStringRef)CTFontDescriptorCopyAttribute(face_desc, kCTFontNameAttribute);
                    if (face_ps) {
                        if (CFStringCompare(face_ps, ps_name, 0) == kCFCompareEqualTo) {
                            *out_face_index = (int)i;
                            log_debug("font_platform: TTC face index %d for '%s'",
                                      (int)i, CFStringGetCStringPtr(ps_name, kCFStringEncodingUTF8));
                            CFRelease(face_ps);
                            break;
                        }
                        CFRelease(face_ps);
                    }
                }
                CFRelease(descs);
            }
        }
    }

    CFRelease(url);
    if (ps_name) CFRelease(ps_name);

    if (ok && path[0]) {
        log_debug("font_platform: codepoint U+%04X → '%s' (face %d)",
                  codepoint, path, out_face_index ? *out_face_index : 0);
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

// ============================================================================
// CoreText GPOS kerning (macOS only)
//
// FreeType's FT_Get_Kerning only reads the legacy 'kern' table. Modern fonts
// (especially Apple's System Font / SF Pro) store kerning in the OpenType GPOS
// table. CoreText reads GPOS natively, so we use it as a fallback for pair
// kerning when FreeType reports has_kerning=false.
// ============================================================================

#ifdef __APPLE__

void* font_platform_create_ct_font(const char* postscript_name,
                                    const char* family_name,
                                    float size_px,
                                    int css_weight) {
    CTFontRef ct_font = NULL;

    // SFNS.ttf (System Font / -apple-system) is a variable font whose FreeType
    // PostScript names like "SFNS_17opsz" are NOT registered in the macOS font
    // catalog.  CTFontCreateWithName would silently fall back to Helvetica.
    // Use the system UI font API which gives exactly the font Chrome uses.
    // We pass size_px in CSS points so CoreText selects the same opsz instance.
    bool is_system_ui = (postscript_name && strncmp(postscript_name, "SFNS_", 5) == 0) ||
                        (family_name     && strcmp(family_name, "System Font") == 0);
    if (is_system_ui) {
        // Create at the correct weight so advances match Chrome for bold/semibold too.
        // Map CSS weight to CoreText weight trait (-1..1 normalized scale).
        // kCTFontWeight* constants: Semibold≈0.23, Bold≈0.40, Heavy≈0.56, Black≈0.62
        ct_font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem,
                                                (CGFloat)size_px, NULL);
        if (ct_font && css_weight > 500) {
            // For semibold/bold: create a copy with the requested weight trait
            // so glyph advances reflect the actual weight, not just Regular.
            CGFloat ct_weight;
            if      (css_weight >= 900) ct_weight = 0.62f;
            else if (css_weight >= 800) ct_weight = 0.56f;
            else if (css_weight >= 700) ct_weight = 0.40f;
            else                        ct_weight = 0.23f;  // 600 SemiBold
            CFNumberRef wt_num = CFNumberCreate(NULL, kCFNumberCGFloatType, &ct_weight);
            CFStringRef wt_key = kCTFontWeightTrait;
            CFDictionaryRef traits = CFDictionaryCreate(NULL,
                (const void**)&wt_key, (const void**)&wt_num, 1,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFRelease(wt_num);
            CFStringRef tk = kCTFontTraitsAttribute;
            CFDictionaryRef attrs = CFDictionaryCreate(NULL,
                (const void**)&tk, (const void**)&traits, 1,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFRelease(traits);
            CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
            CFRelease(attrs);
            CTFontRef weighted = CTFontCreateCopyWithAttributes(ct_font, (CGFloat)size_px, NULL, desc);
            CFRelease(desc);
            CFRelease(ct_font);
            ct_font = weighted ? weighted : CTFontCreateUIFontForLanguage(
                                               kCTFontUIFontSystem, (CGFloat)size_px, NULL);
        }
    }

    // For non-system fonts, try PostScript name lookup
    if (!ct_font && postscript_name && postscript_name[0]) {
        CFStringRef ps = CFStringCreateWithCString(NULL, postscript_name, kCFStringEncodingUTF8);
        if (ps) {
            CTFontRef candidate = CTFontCreateWithName(ps, (CGFloat)size_px, NULL);
            CFRelease(ps);
            if (candidate) {
                CFStringRef actual = CTFontCopyPostScriptName(candidate);
                bool is_fallback = false;
                if (actual) {
                    char buf[64] = "";
                    CFStringGetCString(actual, buf, sizeof(buf), kCFStringEncodingUTF8);
                    CFRelease(actual);
                    is_fallback = (strcmp(buf, "Helvetica") == 0 &&
                                   postscript_name && strcmp(postscript_name, "Helvetica") != 0);
                }
                if (is_fallback) { CFRelease(candidate); }
                else             { ct_font = candidate; }
            }
        }
    }

    // fall back to family name
    if (!ct_font && family_name && family_name[0]) {
        CFStringRef fam = CFStringCreateWithCString(NULL, family_name, kCFStringEncodingUTF8);
        if (fam) {
            ct_font = CTFontCreateWithName(fam, (CGFloat)size_px, NULL);
            CFRelease(fam);
        }
    }

    if (ct_font) {
        log_debug("font_platform: created CTFont (ps=%s, size=%.2f, wgt=%d)",
                  postscript_name ? postscript_name : "?", size_px, css_weight);
    }
    return (void*)ct_font;
}

void font_platform_destroy_ct_font(void* ct_font_ref) {
    if (ct_font_ref) {
        CFRelease((CTFontRef)ct_font_ref);
    }
}

/**
 * Get the nominal (un-kerned) advance width of a single glyph using CoreText.
 *
 * CoreText and FreeType can differ for variable fonts like SFNS.ttf due to
 * optical-size axis selection.  CTFont is created at CSS_size so that CoreText
 * selects the same opsz as Chrome; the returned advance is therefore already
 * in CSS pixels and requires no pixel_ratio division.
 *
 * @param ct_font_ref  CTFontRef (opaque void*) created at CSS size
 * @param codepoint    Unicode codepoint
 * @return advance_x in CSS pixels, or -1.0f if the glyph is not available
 */
float font_platform_get_glyph_advance(void* ct_font_ref, uint32_t codepoint) {
    if (!ct_font_ref) return -1.0f;

    CTFontRef font = (CTFontRef)ct_font_ref;

    // encode codepoint as UTF-16
    UniChar utf16[2];
    CFIndex char_count;
    if (codepoint <= 0xFFFF) {
        utf16[0] = (UniChar)codepoint;
        char_count = 1;
    } else if (codepoint <= 0x10FFFF) {
        uint32_t cp = codepoint - 0x10000;
        utf16[0] = (UniChar)(0xD800 + (cp >> 10));
        utf16[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
        char_count = 2;
    } else {
        return -1.0f;
    }

    // get glyph id — for a surrogate pair, CoreText fills glyphs[0] with the
    // real glyph and glyphs[1] with 0; we only need glyphs[0]
    CGGlyph glyphs[2] = {0, 0};
    bool found = CTFontGetGlyphsForCharacters(font, utf16, glyphs, char_count);
    if (!found || glyphs[0] == 0) return -1.0f;

    // get nominal advance (un-kerned) for the single glyph
    CGSize advance = {0, 0};
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal,
                                &glyphs[0], &advance, 1);

    // CTFont is at CSS_size, so advance.width is directly in CSS pixels
    return (float)(advance.width);
}

float font_platform_get_pair_kerning(void* ct_font_ref, uint32_t left_cp, uint32_t right_cp) {
    if (!ct_font_ref) return 0.0f;

    CTFontRef font = (CTFontRef)ct_font_ref;

    // encode both codepoints as UTF-16
    UniChar utf16[4];
    CFIndex len = 0;

    if (left_cp <= 0xFFFF) {
        utf16[len++] = (UniChar)left_cp;
    } else if (left_cp <= 0x10FFFF) {
        uint32_t cp = left_cp - 0x10000;
        utf16[len++] = (UniChar)(0xD800 + (cp >> 10));
        utf16[len++] = (UniChar)(0xDC00 + (cp & 0x3FF));
    } else {
        return 0.0f;
    }

    CFIndex left_len = len;

    if (right_cp <= 0xFFFF) {
        utf16[len++] = (UniChar)right_cp;
    } else if (right_cp <= 0x10FFFF) {
        uint32_t cp = right_cp - 0x10000;
        utf16[len++] = (UniChar)(0xD800 + (cp >> 10));
        utf16[len++] = (UniChar)(0xDC00 + (cp & 0x3FF));
    } else {
        return 0.0f;
    }

    // get nominal advance for left glyph
    CGGlyph left_glyph;
    CTFontGetGlyphsForCharacters(font, utf16, &left_glyph, (CFIndex)left_len);
    if (left_glyph == 0) return 0.0f;

    CGSize nominal_adv;
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &left_glyph, &nominal_adv, 1);

    // create attributed string for the pair and measure via CTLine/CTRun
    CFStringRef str = CFStringCreateWithCharacters(NULL, utf16, len);
    if (!str) return 0.0f;

    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef values[] = { font };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL,
        (const void**)keys, (const void**)values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef astr = CFAttributedStringCreate(NULL, str, attrs);
    CFRelease(str);
    CFRelease(attrs);
    if (!astr) return 0.0f;

    CTLineRef line = CTLineCreateWithAttributedString(astr);
    CFRelease(astr);
    if (!line) return 0.0f;

    // get the first glyph's advance from the run (includes GPOS kerning)
    float kerning = 0.0f;
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    if (CFArrayGetCount(runs) > 0) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
        CFIndex glyph_count = CTRunGetGlyphCount(run);
        if (glyph_count >= 2) {
            CGSize advances[2];
            CTRunGetAdvances(run, CFRangeMake(0, 1), advances);
            kerning = (float)(advances[0].width - nominal_adv.width);
        }
    }

    CFRelease(line);
    return kerning;
}

#endif
