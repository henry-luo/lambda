/**
 * Platform-specific font lookup interface
 * 
 * Provides fallback font discovery when fonts are not found in the database.
 * Each platform (macOS, Linux, Windows) implements its own font search strategy.
 */

#ifndef FONT_LOOKUP_PLATFORM_H
#define FONT_LOOKUP_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * find_font_path_fallback
 * 
 * Attempts to locate a font file using platform-specific methods.
 * 
 * This is called as a fallback when font_database_find_all_matches() returns
 * no results. It searches standard system font directories on each platform.
 * 
 * Supported platforms:
 * - macOS: Searches /System/Library/Fonts, /Library/Fonts, etc.
 * - Linux: Searches /usr/share/fonts/truetype, /usr/share/fonts/opentype, etc.
 * - Windows: Searches C:\Windows\Fonts, etc.
 * 
 * @param font_name The font family name to search for (e.g., "PingFang SC")
 * @return Allocated string with absolute path to font file if found,
 *         or NULL if font not found or not supported on this platform.
 *         Caller must free the returned string with free().
 * 
 * Example:
 *   char* path = find_font_path_fallback("Arial");
 *   if (path) {
 *       load_font_from_path(path);
 *       free(path);
 *   }
 */
char* find_font_path_fallback(const char* font_name);

#ifdef __cplusplus
}
#endif

#endif // FONT_LOOKUP_PLATFORM_H
