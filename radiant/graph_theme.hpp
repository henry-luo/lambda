#ifndef RADIANT_GRAPH_THEME_HPP
#define RADIANT_GRAPH_THEME_HPP

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Graph Theme System
 *
 * Inspired by beautiful-mermaid's two-color foundation approach.
 * Provide just bg/fg colors, and all other colors are derived automatically
 * using color mixing ratios.
 *
 * The mixing ratios create a harmonious palette:
 * - text: 100% fg (primary text)
 * - text_secondary: 60% fg + 40% bg (secondary text)
 * - text_muted: 40% fg + 60% bg (muted text, less emphasis)
 * - line: 30% fg + 70% bg (edge/connector lines)
 * - arrow: 50% fg + 50% bg (arrow head fill)
 * - node_fill: 3% fg + 97% bg (subtle node background tint)
 * - node_stroke: 20% fg + 80% bg (node border)
 * - group_header: 5% fg + 95% bg (subgraph header band)
 * - surface: 8% fg + 92% bg (elevated surface background)
 */

/**
 * Theme structure with derived colors.
 * All color strings are hex codes like "#1a1b26".
 */
typedef struct DiagramTheme {
    const char* name;           // Theme identifier (e.g., "tokyo-night")

    // Foundation colors (required)
    const char* bg;             // Background color
    const char* fg;             // Foreground (primary text) color

    // Derived colors (computed from bg/fg if null)
    const char* text;           // Primary text (100% fg)
    const char* text_secondary; // Secondary text (60% fg)
    const char* text_muted;     // Muted text (40% fg)
    const char* line;           // Edge lines (30% fg)
    const char* arrow;          // Arrow fill (50% fg)
    const char* node_fill;      // Node background (3% fg)
    const char* node_stroke;    // Node border (20% fg)
    const char* group_header;   // Subgraph header (5% fg)
    const char* surface;        // Elevated surface (8% fg)

    // Optional accent colors
    const char* accent;         // Accent/highlight color
    const char* error;          // Error/danger color
    const char* warning;        // Warning color
    const char* success;        // Success color
} DiagramTheme;

/**
 * Color mixing ratios (percentage of foreground color).
 * These match beautiful-mermaid's MIX constants.
 */
typedef struct ThemeMixRatios {
    int text;           // 100 - just use fg directly
    int text_secondary; // 60
    int text_muted;     // 40
    int line;           // 30
    int arrow;          // 50
    int node_fill;      // 3
    int node_stroke;    // 20
    int group_header;   // 5
    int surface;        // 8
} ThemeMixRatios;

// Default mixing ratios
extern const ThemeMixRatios DEFAULT_MIX_RATIOS;

// ============================================================================
// Predefined Themes
// ============================================================================

// Dark themes
extern const DiagramTheme THEME_TOKYO_NIGHT;     // Deep blue-purple dark theme
extern const DiagramTheme THEME_NORD;            // Arctic blue-gray dark theme
extern const DiagramTheme THEME_DRACULA;         // Purple-tinted dark theme
extern const DiagramTheme THEME_CATPPUCCIN_MOCHA;// Warm dark theme
extern const DiagramTheme THEME_ONE_DARK;        // Atom-inspired dark theme
extern const DiagramTheme THEME_GITHUB_DARK;     // GitHub dark mode

// Light themes
extern const DiagramTheme THEME_GITHUB_LIGHT;    // GitHub light mode
extern const DiagramTheme THEME_SOLARIZED_LIGHT; // Warm beige light theme
extern const DiagramTheme THEME_CATPPUCCIN_LATTE;// Warm light theme

// Neutral themes
extern const DiagramTheme THEME_ZINC_DARK;       // Neutral gray dark theme
extern const DiagramTheme THEME_ZINC_LIGHT;      // Neutral gray light theme

// Default theme
extern const DiagramTheme THEME_DEFAULT;         // Alias for THEME_ZINC_DARK

// ============================================================================
// Theme Functions
// ============================================================================

/**
 * Get a theme by name.
 * @param name Theme name (e.g., "tokyo-night", "nord", "github-dark")
 * @return Pointer to theme, or THEME_DEFAULT if not found
 */
const DiagramTheme* get_theme_by_name(const char* name);

/**
 * List all available theme names.
 * @param out_names Output array of theme name strings (caller provides)
 * @param max_count Maximum number of names to return
 * @return Number of themes available
 */
int list_theme_names(const char** out_names, int max_count);

/**
 * Create a custom theme from bg/fg colors.
 * Derives all other colors using default mix ratios.
 * @param name Theme name
 * @param bg Background color (hex, e.g., "#1a1b26")
 * @param fg Foreground color (hex, e.g., "#a9b1d6")
 * @return Newly allocated theme (caller must free with free_theme())
 */
DiagramTheme* create_theme(const char* name, const char* bg, const char* fg);

/**
 * Create a custom theme with custom mix ratios.
 * @param name Theme name
 * @param bg Background color
 * @param fg Foreground color
 * @param ratios Custom mix ratios
 * @return Newly allocated theme (caller must free with free_theme())
 */
DiagramTheme* create_theme_with_ratios(const char* name, const char* bg,
                                        const char* fg, const ThemeMixRatios* ratios);

/**
 * Free a dynamically created theme.
 * Do NOT call on predefined themes (they are static).
 */
void free_theme(DiagramTheme* theme);

/**
 * Mix two colors.
 * @param fg Foreground color (hex string)
 * @param bg Background color (hex string)
 * @param fg_percent Percentage of foreground (0-100)
 * @return Newly allocated hex string (caller must free)
 */
char* mix_colors(const char* fg, const char* bg, int fg_percent);

/**
 * Parse hex color to RGB components.
 * @param hex Color string like "#1a1b26" or "1a1b26"
 * @param r, g, b Output components (0-255)
 * @return true if parsing succeeded
 */
bool parse_hex_color(const char* hex, int* r, int* g, int* b);

/**
 * Format RGB to hex string.
 * @param r, g, b Color components (0-255)
 * @return Newly allocated string like "#1a1b26" (caller must free)
 */
char* format_hex_color(int r, int g, int b);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_GRAPH_THEME_HPP
