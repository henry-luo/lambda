#include "graph_theme.hpp"
#include "../lib/str.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ============================================================================
// Default Mix Ratios (matching beautiful-mermaid)
// ============================================================================

const ThemeMixRatios DEFAULT_MIX_RATIOS = {
    .text = 100,           // Primary text: 100% fg
    .text_secondary = 60,  // Secondary text
    .text_muted = 40,      // Muted text
    .line = 30,            // Edge lines
    .arrow = 50,           // Arrow fill
    .node_fill = 3,        // Node background (very subtle tint)
    .node_stroke = 20,     // Node border
    .group_header = 5,     // Subgraph header
    .surface = 8,          // Elevated surface
};

// ============================================================================
// Color Utility Functions
// ============================================================================

bool parse_hex_color(const char* hex, int* r, int* g, int* b) {
    if (!hex || !r || !g || !b) return false;

    // skip leading # if present
    if (hex[0] == '#') hex++;

    // must be 6 hex digits
    if (strlen(hex) != 6) return false;

    unsigned int rgb;
    if (sscanf(hex, "%06x", &rgb) != 1) return false;

    *r = (rgb >> 16) & 0xFF;
    *g = (rgb >> 8) & 0xFF;
    *b = rgb & 0xFF;
    return true;
}

char* format_hex_color(int r, int g, int b) {
    // clamp values
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;

    char* result = (char*)malloc(8);  // "#RRGGBB\0"
    snprintf(result, 8, "#%02x%02x%02x", r, g, b);
    return result;
}

char* mix_colors(const char* fg, const char* bg, int fg_percent) {
    int fg_r, fg_g, fg_b;
    int bg_r, bg_g, bg_b;

    if (!parse_hex_color(fg, &fg_r, &fg_g, &fg_b)) {
        return strdup("#000000");
    }
    if (!parse_hex_color(bg, &bg_r, &bg_g, &bg_b)) {
        return strdup("#ffffff");
    }

    // clamp percentage
    if (fg_percent < 0) fg_percent = 0;
    if (fg_percent > 100) fg_percent = 100;

    float fg_ratio = fg_percent / 100.0f;
    float bg_ratio = 1.0f - fg_ratio;

    int r = (int)(fg_r * fg_ratio + bg_r * bg_ratio);
    int g = (int)(fg_g * fg_ratio + bg_g * bg_ratio);
    int b = (int)(fg_b * fg_ratio + bg_b * bg_ratio);

    return format_hex_color(r, g, b);
}

// ============================================================================
// Theme Creation
// ============================================================================

static char* strdup_safe(const char* s) {
    return s ? strdup(s) : NULL;
}

DiagramTheme* create_theme(const char* name, const char* bg, const char* fg) {
    return create_theme_with_ratios(name, bg, fg, &DEFAULT_MIX_RATIOS);
}

DiagramTheme* create_theme_with_ratios(const char* name, const char* bg,
                                        const char* fg, const ThemeMixRatios* ratios) {
    DiagramTheme* theme = (DiagramTheme*)calloc(1, sizeof(DiagramTheme));

    theme->name = strdup_safe(name);
    theme->bg = strdup_safe(bg);
    theme->fg = strdup_safe(fg);

    // Derive all colors from bg/fg using mix ratios
    theme->text = strdup_safe(fg);  // 100% fg
    theme->text_secondary = mix_colors(fg, bg, ratios->text_secondary);
    theme->text_muted = mix_colors(fg, bg, ratios->text_muted);
    theme->line = mix_colors(fg, bg, ratios->line);
    theme->arrow = mix_colors(fg, bg, ratios->arrow);
    theme->node_fill = mix_colors(fg, bg, ratios->node_fill);
    theme->node_stroke = mix_colors(fg, bg, ratios->node_stroke);
    theme->group_header = mix_colors(fg, bg, ratios->group_header);
    theme->surface = mix_colors(fg, bg, ratios->surface);

    // Default accents (can be overridden)
    theme->accent = NULL;
    theme->error = NULL;
    theme->warning = NULL;
    theme->success = NULL;

    return theme;
}

void free_theme(DiagramTheme* theme) {
    if (!theme) return;

    // Free all allocated strings
    free((void*)theme->name);
    free((void*)theme->bg);
    free((void*)theme->fg);
    free((void*)theme->text);
    free((void*)theme->text_secondary);
    free((void*)theme->text_muted);
    free((void*)theme->line);
    free((void*)theme->arrow);
    free((void*)theme->node_fill);
    free((void*)theme->node_stroke);
    free((void*)theme->group_header);
    free((void*)theme->surface);
    free((void*)theme->accent);
    free((void*)theme->error);
    free((void*)theme->warning);
    free((void*)theme->success);
    free(theme);
}

// ============================================================================
// Predefined Themes
// ============================================================================

// Tokyo Night - Deep blue-purple dark theme (popular VS Code theme)
const DiagramTheme THEME_TOKYO_NIGHT = {
    .name = "tokyo-night",
    .bg = "#1a1b26",
    .fg = "#a9b1d6",
    .text = "#a9b1d6",
    .text_secondary = "#787c99",
    .text_muted = "#565a6e",
    .line = "#3b3d57",
    .arrow = "#5a5d7a",
    .node_fill = "#1d1e2c",
    .node_stroke = "#3b3d57",
    .group_header = "#1e1f2e",
    .surface = "#24253a",
    .accent = "#7aa2f7",
    .error = "#f7768e",
    .warning = "#e0af68",
    .success = "#9ece6a",
};

// Nord - Arctic blue-gray dark theme
const DiagramTheme THEME_NORD = {
    .name = "nord",
    .bg = "#2e3440",
    .fg = "#d8dee9",
    .text = "#d8dee9",
    .text_secondary = "#a3aab8",
    .text_muted = "#7a8294",
    .line = "#4c566a",
    .arrow = "#7d8899",
    .node_fill = "#313845",
    .node_stroke = "#4c566a",
    .group_header = "#333a47",
    .surface = "#3b4252",
    .accent = "#88c0d0",
    .error = "#bf616a",
    .warning = "#ebcb8b",
    .success = "#a3be8c",
};

// Dracula - Purple-tinted dark theme
const DiagramTheme THEME_DRACULA = {
    .name = "dracula",
    .bg = "#282a36",
    .fg = "#f8f8f2",
    .text = "#f8f8f2",
    .text_secondary = "#bdbdb7",
    .text_muted = "#8d8d89",
    .line = "#44475a",
    .arrow = "#9496a1",
    .node_fill = "#2c2e3a",
    .node_stroke = "#44475a",
    .group_header = "#2e303c",
    .surface = "#343746",
    .accent = "#bd93f9",
    .error = "#ff5555",
    .warning = "#ffb86c",
    .success = "#50fa7b",
};

// Catppuccin Mocha - Warm dark theme
const DiagramTheme THEME_CATPPUCCIN_MOCHA = {
    .name = "catppuccin-mocha",
    .bg = "#1e1e2e",
    .fg = "#cdd6f4",
    .text = "#cdd6f4",
    .text_secondary = "#9399b2",
    .text_muted = "#6c708d",
    .line = "#45475a",
    .arrow = "#7d8198",
    .node_fill = "#212132",
    .node_stroke = "#45475a",
    .group_header = "#232334",
    .surface = "#313244",
    .accent = "#cba6f7",
    .error = "#f38ba8",
    .warning = "#fab387",
    .success = "#a6e3a1",
};

// One Dark - Atom-inspired dark theme
const DiagramTheme THEME_ONE_DARK = {
    .name = "one-dark",
    .bg = "#282c34",
    .fg = "#abb2bf",
    .text = "#abb2bf",
    .text_secondary = "#828997",
    .text_muted = "#5c6370",
    .line = "#3e4451",
    .arrow = "#6b7280",
    .node_fill = "#2c3039",
    .node_stroke = "#3e4451",
    .group_header = "#2e333b",
    .surface = "#353b45",
    .accent = "#61afef",
    .error = "#e06c75",
    .warning = "#e5c07b",
    .success = "#98c379",
};

// GitHub Dark - GitHub dark mode colors
const DiagramTheme THEME_GITHUB_DARK = {
    .name = "github-dark",
    .bg = "#0d1117",
    .fg = "#c9d1d9",
    .text = "#c9d1d9",
    .text_secondary = "#8b949e",
    .text_muted = "#6e7681",
    .line = "#30363d",
    .arrow = "#6e7681",
    .node_fill = "#111820",
    .node_stroke = "#30363d",
    .group_header = "#131a21",
    .surface = "#161b22",
    .accent = "#58a6ff",
    .error = "#f85149",
    .warning = "#d29922",
    .success = "#3fb950",
};

// GitHub Light - GitHub light mode colors
const DiagramTheme THEME_GITHUB_LIGHT = {
    .name = "github-light",
    .bg = "#ffffff",
    .fg = "#24292f",
    .text = "#24292f",
    .text_secondary = "#57606a",
    .text_muted = "#8c959f",
    .line = "#d0d7de",
    .arrow = "#8c959f",
    .node_fill = "#f6f8fa",
    .node_stroke = "#d0d7de",
    .group_header = "#f3f5f7",
    .surface = "#f6f8fa",
    .accent = "#0969da",
    .error = "#cf222e",
    .warning = "#9a6700",
    .success = "#1a7f37",
};

// Solarized Light - Classic warm light theme
const DiagramTheme THEME_SOLARIZED_LIGHT = {
    .name = "solarized-light",
    .bg = "#fdf6e3",
    .fg = "#657b83",
    .text = "#657b83",
    .text_secondary = "#839496",
    .text_muted = "#93a1a1",
    .line = "#eee8d5",
    .arrow = "#93a1a1",
    .node_fill = "#faf4e0",
    .node_stroke = "#eee8d5",
    .group_header = "#f9f3de",
    .surface = "#eee8d5",
    .accent = "#268bd2",
    .error = "#dc322f",
    .warning = "#b58900",
    .success = "#859900",
};

// Catppuccin Latte - Warm light theme
const DiagramTheme THEME_CATPPUCCIN_LATTE = {
    .name = "catppuccin-latte",
    .bg = "#eff1f5",
    .fg = "#4c4f69",
    .text = "#4c4f69",
    .text_secondary = "#6c6f85",
    .text_muted = "#8c8fa1",
    .line = "#ccd0da",
    .arrow = "#8c8fa1",
    .node_fill = "#e9ebf0",
    .node_stroke = "#ccd0da",
    .group_header = "#e6e8ee",
    .surface = "#e6e9ef",
    .accent = "#8839ef",
    .error = "#d20f39",
    .warning = "#df8e1d",
    .success = "#40a02b",
};

// Zinc Dark - Neutral gray dark theme
const DiagramTheme THEME_ZINC_DARK = {
    .name = "zinc-dark",
    .bg = "#18181b",
    .fg = "#d4d4d8",
    .text = "#d4d4d8",
    .text_secondary = "#a1a1aa",
    .text_muted = "#71717a",
    .line = "#3f3f46",
    .arrow = "#71717a",
    .node_fill = "#1c1c20",
    .node_stroke = "#3f3f46",
    .group_header = "#1e1e22",
    .surface = "#27272a",
    .accent = "#a1a1aa",
    .error = "#ef4444",
    .warning = "#f59e0b",
    .success = "#22c55e",
};

// Zinc Light - Neutral gray light theme
const DiagramTheme THEME_ZINC_LIGHT = {
    .name = "zinc-light",
    .bg = "#fafafa",
    .fg = "#27272a",
    .text = "#27272a",
    .text_secondary = "#52525b",
    .text_muted = "#71717a",
    .line = "#d4d4d8",
    .arrow = "#71717a",
    .node_fill = "#f4f4f5",
    .node_stroke = "#d4d4d8",
    .group_header = "#f1f1f2",
    .surface = "#f4f4f5",
    .accent = "#52525b",
    .error = "#dc2626",
    .warning = "#d97706",
    .success = "#16a34a",
};

// Default theme alias
const DiagramTheme THEME_DEFAULT = {
    .name = "default",
    .bg = "#18181b",
    .fg = "#d4d4d8",
    .text = "#d4d4d8",
    .text_secondary = "#a1a1aa",
    .text_muted = "#71717a",
    .line = "#3f3f46",
    .arrow = "#71717a",
    .node_fill = "#1c1c20",
    .node_stroke = "#3f3f46",
    .group_header = "#1e1e22",
    .surface = "#27272a",
    .accent = "#a1a1aa",
    .error = "#ef4444",
    .warning = "#f59e0b",
    .success = "#22c55e",
};

// ============================================================================
// Theme Registry
// ============================================================================

static const struct {
    const char* name;
    const DiagramTheme* theme;
} THEME_REGISTRY[] = {
    // Dark themes
    {"tokyo-night", &THEME_TOKYO_NIGHT},
    {"nord", &THEME_NORD},
    {"dracula", &THEME_DRACULA},
    {"catppuccin-mocha", &THEME_CATPPUCCIN_MOCHA},
    {"catppuccin", &THEME_CATPPUCCIN_MOCHA},  // alias
    {"one-dark", &THEME_ONE_DARK},
    {"github-dark", &THEME_GITHUB_DARK},
    {"zinc-dark", &THEME_ZINC_DARK},

    // Light themes
    {"github-light", &THEME_GITHUB_LIGHT},
    {"solarized-light", &THEME_SOLARIZED_LIGHT},
    {"catppuccin-latte", &THEME_CATPPUCCIN_LATTE},
    {"zinc-light", &THEME_ZINC_LIGHT},

    // Aliases
    {"dark", &THEME_ZINC_DARK},
    {"light", &THEME_ZINC_LIGHT},
    {"default", &THEME_DEFAULT},

    {NULL, NULL}  // sentinel
};

const DiagramTheme* get_theme_by_name(const char* name) {
    if (!name || !name[0]) {
        return &THEME_DEFAULT;
    }

    // case-insensitive search
    for (int i = 0; THEME_REGISTRY[i].name != NULL; i++) {
        if (str_ieq(name, strlen(name), THEME_REGISTRY[i].name, strlen(THEME_REGISTRY[i].name))) {
            return THEME_REGISTRY[i].theme;
        }
    }

    return &THEME_DEFAULT;
}

int list_theme_names(const char** out_names, int max_count) {
    if (!out_names || max_count <= 0) {
        // count available themes
        int count = 0;
        for (int i = 0; THEME_REGISTRY[i].name != NULL; i++) {
            count++;
        }
        return count;
    }

    int count = 0;
    for (int i = 0; THEME_REGISTRY[i].name != NULL && count < max_count; i++) {
        out_names[count++] = THEME_REGISTRY[i].name;
    }
    return count;
}
