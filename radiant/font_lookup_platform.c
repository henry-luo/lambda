/**
 * Platform-specific font lookup implementations
 * Provides fallback font discovery when fonts are not in the database
 * 
 * This module handles platform-specific APIs to find system fonts:
 * - macOS: Search standard font directories
 * - Linux: Use FontConfig (optional, for future implementation)
 * - Windows: Use GDI+ or registry (future implementation)
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "../lib/log.h"

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
        {"PingFang SC", "/System/Library/Fonts/STHeiti Medium.ttc"},  // Fallback to STHeiti
        {"Heiti SC", "/System/Library/Fonts/STHeiti Medium.ttc"},
        {"STHeiti", "/System/Library/Fonts/STHeiti Medium.ttc"},
        {"Hiragino Sans", "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"},
        {"Apple Color Emoji", "/System/Library/Fonts/Apple Color Emoji.ttc"},
        {"Helvetica Neue", "/System/Library/Fonts/Helvetica.ttc"},
        {"Times New Roman", "/System/Library/Fonts/Times.ttc"},
        {NULL, NULL}
    };
    
    // Check hardcoded mappings first
    for (int i = 0; mappings[i].family_name != NULL; i++) {
        if (strcasecmp(font_name, mappings[i].family_name) == 0) {
            FILE* test_file = fopen(mappings[i].file_path, "r");
            if (test_file) {
                fclose(test_file);
                char* result = strdup(mappings[i].file_path);
                log_info("Found macOS font '%s' via mapping: %s", font_name, result);
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
                char* result = strdup(font_path);
                log_info("Found macOS font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }
    
    log_debug("Font '%s' not found in standard macOS directories", font_name);
    return NULL;
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
                char* result = strdup(font_path);
                log_info("Found Linux font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }
    
    log_debug("Font '%s' not found in standard Linux directories", font_name);
    return NULL;
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
                char* result = strdup(font_path);
                log_info("Found Windows font '%s' at: %s", font_name, result);
                return result;
            }
        }
    }
    
    log_debug("Font '%s' not found in standard Windows directories", font_name);
    return NULL;
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
