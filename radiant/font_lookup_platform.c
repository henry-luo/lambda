/**
 * Platform-specific font lookup implementations
 * Provides fallback font discovery when fonts are not in the database
 *
 * This module handles platform-specific APIs to find system fonts:
 * - macOS: Search standard font directories, CoreText metrics
 * - Linux: Use FontConfig (optional, for future implementation)
 * - Windows: Use GDI+ or registry (future implementation)
 */

// Include Apple frameworks FIRST to avoid Rect type conflict with view.hpp
#ifdef __APPLE__
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"

/**
 * find_font_path_platform
 *
 * Platform-specific font lookup - called when font is not in database
 * Returns path to font file if found, NULL otherwise
 *
 * @param font_name The font family name to search for (e.g., "PingFang SC")
 * @return Allocated string with font path, or NULL if not found
 *         Caller is responsible for freeing the returned string
 */

#ifdef __APPLE__

/**
 * macOS implementation: Search standard macOS font directories
 *
 * This approach is simple and doesn't require CoreFoundation/CoreText imports
 * which can cause header conflicts with other libraries.
 */
char* find_font_path_platform(const char* font_name) {
    if (!font_name) return NULL;

    // log_debug("Attempting macOS font lookup for: %s", font_name);  // Too verbose

    // Hardcoded mappings for common Chinese fonts on macOS
    // PingFang SC is typically in .ttc files with different naming
    typedef struct {
        const char* family_name;
        const char* file_path;
    } FontMapping;

    const FontMapping mappings[] = {
        // macOS San Francisco system font (used by -apple-system, BlinkMacSystemFont, system-ui)
        {"SF Pro Display", "/System/Library/Fonts/SFNS.ttf"},
        {"SF Pro", "/System/Library/Fonts/SFNS.ttf"},
        {".AppleSystemUIFont", "/System/Library/Fonts/SFNS.ttf"},
        {".SF NS", "/System/Library/Fonts/SFNS.ttf"},
        {"SFNS", "/System/Library/Fonts/SFNS.ttf"},
        // Chinese fonts
        {"PingFang SC", "/System/Library/Fonts/STHeiti Medium.ttc"},  // Fallback to STHeiti
        {"Heiti SC", "/System/Library/Fonts/STHeiti Medium.ttc"},
        {"STHeiti", "/System/Library/Fonts/STHeiti Medium.ttc"},
        {"Hiragino Sans", "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc"},
        {"Arial Unicode MS", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"},
        {"Apple Color Emoji", "/System/Library/Fonts/Apple Color Emoji.ttc"},
        {"Helvetica Neue", "/System/Library/Fonts/Helvetica.ttc"},
        {"Times New Roman", "/System/Library/Fonts/Times.ttc"},
        {NULL, NULL}
    };

    // Check hardcoded mappings first
    for (int i = 0; mappings[i].family_name != NULL; i++) {
        if (str_ieq(font_name, strlen(font_name), mappings[i].family_name, strlen(mappings[i].family_name))) {
            FILE* test_file = fopen(mappings[i].file_path, "r");
            if (test_file) {
                fclose(test_file);
                char* result = mem_strdup(mappings[i].file_path, MEM_CAT_FONT);
                log_debug("Found macOS font '%s' via mapping: %s", font_name, result);
                return result;
            } else {
                log_debug("Mapped path doesn't exist: %s", mappings[i].file_path);
            }
        }
    }

    // Common macOS font directories in search priority order
    const char* search_dirs[] = {
        "/System/Library/Fonts",
        "/System/Library/Fonts/Supplemental",
        "/Library/Fonts",
        "/Network/Library/Fonts",
        NULL  // Sentinel
    };

    // Try to find the font file in system directories
    for (int i = 0; search_dirs[i] != NULL; i++) {
        // Try multiple file extensions
        const char* extensions[] = {".ttf", ".otf", ".ttc", NULL};

        for (int j = 0; extensions[j] != NULL; j++) {
            // Build potential font path: DirectoryName/FontName.ext
            char font_path[PATH_MAX];
            snprintf(font_path, sizeof(font_path), "%s/%s%s",
                     search_dirs[i], font_name, extensions[j]);

            // Check if file exists by attempting to open it
            FILE* test_file = fopen(font_path, "r");
            if (test_file) {
                fclose(test_file);
                char* result = mem_strdup(font_path, MEM_CAT_FONT);
                log_debug("Found macOS font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }

    log_debug("Font '%s' not found in standard macOS directories", font_name);
    return NULL;
}

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

    // Create CFString from font family name
    CFStringRef cf_family = CFStringCreateWithCString(NULL, font_family, kCFStringEncodingUTF8);
    if (!cf_family) return 0;

    // Create CTFont at the specified size
    CTFontRef ct_font = CTFontCreateWithName(cf_family, (CGFloat)font_size, NULL);
    CFRelease(cf_family);

    if (!ct_font) {
        log_debug("CoreText: Could not create font for '%s'", font_family);
        return 0;
    }

    // Get metrics from CoreText (this is what Skia does on macOS)
    // See: SkScalerContext_Mac::generateFontMetrics in SkScalerContext_mac_ct.cpp
    CGFloat ct_ascent = CTFontGetAscent(ct_font);
    CGFloat ct_descent = CTFontGetDescent(ct_font);
    CGFloat ct_leading = CTFontGetLeading(ct_font);

    CFRelease(ct_font);

    // Round each component individually (matches Chrome's SkScalarRoundToScalar)
    // See: font_metrics.cc AscentDescentWithHacks lines 111-112
    float ascent = roundf((float)ct_ascent);
    float descent = roundf((float)ct_descent);
    float leading = roundf((float)ct_leading);

    // macOS-specific adjustment for classic Mac fonts
    // Chrome applies a 15% adjustment to ascent ONLY for Apple's classic fonts:
    // "Times", "Helvetica", "Courier" - to match their Microsoft equivalents
    // (the de facto web standard). See: font_metrics.cc lines 129-142, crbug.com/445830
    //
    // IMPORTANT: This does NOT apply to "Times New Roman", "Helvetica Neue",
    // "Courier New" - those are Microsoft/Adobe fonts that already have the
    // correct metrics. Chrome only applies this hack when the CSS font-family
    // is exactly "Times", "Helvetica", or "Courier".
    //
    // Since we're passing the FreeType family name (font's internal name) here,
    // we should only apply the hack for the exact Apple font names.
    if (strcmp(font_family, "Times") == 0 ||
        strcmp(font_family, "Helvetica") == 0 ||
        strcmp(font_family, "Courier") == 0) {
        // ascent += floorf(((ascent + descent) * 0.15f) + 0.5f)
        float adjustment = floorf(((ascent + descent) * 0.15f) + 0.5f);
        ascent += adjustment;
        log_debug("CoreText macOS font hack: +%.0f for %s (asc=%.0f, desc=%.0f)",
                  adjustment, font_family, ascent, descent);
    }

    // LineSpacing = lroundf(ascent) + lroundf(descent) + lroundf(line_gap)
    // See: simple_font_data.cc line 175
    float line_height = ascent + descent + leading;

    if (out_ascent) *out_ascent = ascent;
    if (out_descent) *out_descent = descent;
    if (out_line_height) *out_line_height = line_height;

    log_debug("CoreText metrics for %s@%.1f: ascent=%.0f, descent=%.0f, leading=%.0f, lineHeight=%.0f",
              font_family, font_size, ascent, descent, leading, line_height);

    return 1;
}

#elif defined(__linux__)

/**
 * Linux implementation: Search common Linux font directories
 *
 * Future enhancement: Could integrate with FontConfig library for
 * more sophisticated font matching, but for now use simple directory search.
 */
char* find_font_path_platform(const char* font_name) {
    if (!font_name) return NULL;

    log_debug("Attempting Linux font lookup for: %s", font_name);

    // Common Linux font directories in search priority order
    const char* search_dirs[] = {
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/opentype",
        "/usr/local/share/fonts/truetype",
        "/usr/local/share/fonts/opentype",
        "~/.fonts",  // User fonts (would need path expansion)
        NULL  // Sentinel
    };

    // Try to find the font file in system directories
    for (int i = 0; search_dirs[i] != NULL; i++) {
        const char* extensions[] = {".ttf", ".otf", ".ttc", NULL};

        for (int j = 0; extensions[j] != NULL; j++) {
            char font_path[PATH_MAX];
            snprintf(font_path, sizeof(font_path), "%s/%s%s",
                     search_dirs[i], font_name, extensions[j]);

            FILE* test_file = fopen(font_path, "r");
            if (test_file) {
                fclose(test_file);
                char* result = mem_strdup(font_path, MEM_CAT_FONT);
                log_info("Found Linux font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }

    log_debug("Font '%s' not found in standard Linux directories", font_name);
    return NULL;
}

/**
 * Linux implementation: Font metrics stub
 * Returns 0 to indicate metrics should be computed via FreeType
 */
int get_font_metrics_platform(const char* font_family, float font_size,
                              float* out_ascent, float* out_descent, float* out_line_height) {
    (void)font_family; (void)font_size;
    (void)out_ascent; (void)out_descent; (void)out_line_height;
    return 0;  // Use FreeType metrics on Linux
}

#elif defined(_WIN32)

/**
 * Windows implementation: Search Windows font directories
 *
 * Future enhancement: Could integrate with GDI+ or Windows Registry
 * for more sophisticated font matching.
 */
char* find_font_path_platform(const char* font_name) {
    if (!font_name) return NULL;

    log_debug("Attempting Windows font lookup for: %s", font_name);

    // Common Windows font directories
    const char* search_dirs[] = {
        "C:\\Windows\\Fonts",
        "C:\\Program Files\\Fonts",
        "C:\\Program Files (x86)\\Fonts",
        NULL  // Sentinel
    };

    for (int i = 0; search_dirs[i] != NULL; i++) {
        const char* extensions[] = {".ttf", ".otf", ".ttc", NULL};

        for (int j = 0; extensions[j] != NULL; j++) {
            char font_path[PATH_MAX];
            snprintf(font_path, sizeof(font_path), "%s\\%s%s",
                     search_dirs[i], font_name, extensions[j]);

            FILE* test_file = fopen(font_path, "r");
            if (test_file) {
                fclose(test_file);
                char* result = mem_strdup(font_path, MEM_CAT_FONT);
                log_info("Found Windows font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }

    log_debug("Font '%s' not found in standard Windows directories", font_name);
    return NULL;
}

/**
 * Windows implementation: Font metrics stub
 * Returns 0 to indicate metrics should be computed via FreeType
 */
int get_font_metrics_platform(const char* font_family, float font_size,
                              float* out_ascent, float* out_descent, float* out_line_height) {
    (void)font_family; (void)font_size;
    (void)out_ascent; (void)out_descent; (void)out_line_height;
    return 0;  // Use FreeType metrics on Windows
}

#else

/**
 * Fallback implementation for unknown platforms
 */
char* find_font_path_platform(const char* font_name) {
    if (!font_name) return NULL;

    log_warn("Platform-specific font lookup not implemented for this OS");
    log_debug("Font '%s' lookup not supported on this platform", font_name);
    return NULL;
}

/**
 * Fallback implementation: Font metrics stub
 * Returns 0 to indicate metrics should be computed via FreeType
 */
int get_font_metrics_platform(const char* font_family, float font_size,
                              float* out_ascent, float* out_descent, float* out_line_height) {
    (void)font_family; (void)font_size;
    (void)out_ascent; (void)out_descent; (void)out_line_height;
    return 0;
}

#endif

/**
 * Public interface - called from font.cpp
 * This wraps the platform-specific implementation
 */
char* find_font_path_fallback(const char* font_name) {
    if (!font_name || strlen(font_name) == 0) {
        return NULL;
    }

    // log_debug("Attempting platform-specific font lookup for: %s", font_name);  // Too verbose
    return find_font_path_platform(font_name);
}
