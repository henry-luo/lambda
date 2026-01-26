/**
 * format_registry.cpp - Format adapter registry implementation
 *
 * Provides factory access to format adapters and automatic format detection.
 */
#include "format_adapter.hpp"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// Forward declarations of adapter classes
class MarkdownAdapter;
class RstAdapter;
class WikiAdapter;
class OrgAdapter;
class ManAdapter;
class AsciidocAdapter;
class TextileAdapter;

// External adapter instances (defined in format/*.cpp)
// Declared as FormatAdapter* to avoid incomplete type issues
extern FormatAdapter* getMarkdownAdapter();
extern FormatAdapter* getRstAdapter();
extern FormatAdapter* getWikiAdapter();
extern FormatAdapter* getOrgAdapter();
extern FormatAdapter* getManAdapter();
extern FormatAdapter* getAsciidocAdapter();
extern FormatAdapter* getTextileAdapter();

// Static adapter table
static FormatAdapter* s_adapters[] = {
    nullptr,  // MARKDOWN
    nullptr,  // RST
    nullptr,  // WIKI
    nullptr,  // TEXTILE
    nullptr,  // ORG
    nullptr,  // ASCIIDOC
    nullptr,  // MAN
    nullptr   // AUTO_DETECT (unused)
};

static bool s_initialized = false;

static void ensureInitialized() {
    if (s_initialized) return;

    // Get adapter addresses via accessor functions
    s_adapters[(int)Format::MARKDOWN] = getMarkdownAdapter();
    s_adapters[(int)Format::RST] = getRstAdapter();
    s_adapters[(int)Format::WIKI] = getWikiAdapter();
    s_adapters[(int)Format::TEXTILE] = getTextileAdapter();
    s_adapters[(int)Format::ORG] = getOrgAdapter();
    s_adapters[(int)Format::ASCIIDOC] = getAsciidocAdapter();
    s_adapters[(int)Format::MAN] = getManAdapter();

    s_initialized = true;
}

FormatAdapter* FormatRegistry::getAdapter(Format format) {
    ensureInitialized();

    if (format == Format::AUTO_DETECT) {
        // Default to Markdown
        return s_adapters[(int)Format::MARKDOWN];
    }

    int index = (int)format;
    if (index >= 0 && index < (int)(sizeof(s_adapters)/sizeof(s_adapters[0]))) {
        return s_adapters[index];
    }

    // Fallback to Markdown
    return s_adapters[(int)Format::MARKDOWN];
}

FormatAdapter* FormatRegistry::detectAdapter(const char* content, const char* filename) {
    ensureInitialized();

    // First try filename extension
    if (filename) {
        Format fmt = detectFromFilename(filename);
        if (fmt != Format::AUTO_DETECT) {
            return getAdapter(fmt);
        }
    }

    // Then try content heuristics
    if (content) {
        Format fmt = detectFromContent(content);
        if (fmt != Format::AUTO_DETECT) {
            return getAdapter(fmt);
        }
    }

    // Default to Markdown
    return getAdapter(Format::MARKDOWN);
}

Format FormatRegistry::detectFromFilename(const char* filename) {
    if (!filename) return Format::AUTO_DETECT;

    // Find the extension
    const char* ext = strrchr(filename, '.');
    if (!ext) return Format::AUTO_DETECT;

    // Convert to lowercase for comparison
    char ext_lower[32];
    size_t i = 0;
    for (; ext[i] && i < sizeof(ext_lower) - 1; i++) {
        ext_lower[i] = tolower((unsigned char)ext[i]);
    }
    ext_lower[i] = '\0';

    // Check each adapter's extensions
    FormatAdapter* adapters[] = {
        s_adapters[(int)Format::MARKDOWN],
        s_adapters[(int)Format::RST],
        s_adapters[(int)Format::WIKI],
        s_adapters[(int)Format::ORG],
        s_adapters[(int)Format::MAN],
        s_adapters[(int)Format::ASCIIDOC],
        s_adapters[(int)Format::TEXTILE]
    };

    for (FormatAdapter* adapter : adapters) {
        if (!adapter) continue;
        const char* const* exts = adapter->extensions();
        if (!exts) continue;

        for (int j = 0; exts[j]; j++) {
            if (strcmp(ext_lower, exts[j]) == 0) {
                return adapter->format();
            }
        }
    }

    return Format::AUTO_DETECT;
}

Format FormatRegistry::detectFromContent(const char* content) {
    if (!content) return Format::AUTO_DETECT;

    // Skip leading whitespace
    while (*content == ' ' || *content == '\t' || *content == '\n' || *content == '\r') {
        content++;
    }

    // Check for Org-mode: starts with #+
    if (strncmp(content, "#+", 2) == 0) {
        return Format::ORG;
    }

    // Check for Org-mode: starts with *
    if (*content == '*' && *(content+1) == ' ') {
        // Could be Org headline - look for more evidence
        const char* p = content;
        while (*p) {
            if (strncmp(p, "#+BEGIN", 7) == 0 || strncmp(p, "#+begin", 7) == 0) {
                return Format::ORG;
            }
            if (*p == '\n') p++;
            else while (*p && *p != '\n') p++;
        }
    }

    // Check for Man page: starts with .TH or .\"
    if (content[0] == '.' && (content[1] == 'T' || content[1] == '\\' || content[1] == '"')) {
        return Format::MAN;
    }

    // Check for YAML frontmatter (Markdown)
    if (strncmp(content, "---", 3) == 0) {
        return Format::MARKDOWN;
    }

    // Check for RST directive
    if (strncmp(content, "..", 2) == 0 && content[2] == ' ') {
        return Format::RST;
    }

    // Check for MediaWiki: starts with [[
    if (content[0] == '[' && content[1] == '[') {
        return Format::WIKI;
    }

    // Check for MediaWiki heading: == or ===
    if (content[0] == '=' && content[1] == '=') {
        return Format::WIKI;
    }

    // Check for Textile: h1. or h2.
    if (content[0] == 'h' && isdigit((unsigned char)content[1]) && content[2] == '.') {
        return Format::TEXTILE;
    }

    // Check for AsciiDoc: = Header (single =) or [source]
    if (content[0] == '=' && content[1] == ' ') {
        return Format::ASCIIDOC;
    }
    if (content[0] == '[' && strncmp(content, "[source", 7) == 0) {
        return Format::ASCIIDOC;
    }

    // Default to Markdown (most common)
    return Format::MARKDOWN;
}

void FormatRegistry::registerAdapter(FormatAdapter* adapter) {
    if (!adapter) return;
    ensureInitialized();

    int index = (int)adapter->format();
    if (index >= 0 && index < (int)(sizeof(s_adapters)/sizeof(s_adapters[0]))) {
        s_adapters[index] = adapter;
    }
}

// ============================================================================
// Utility function implementations
// ============================================================================

const char* find_closing_delimiter(const char* start, const char* delimiter,
                                   bool respect_escapes) {
    size_t delim_len = strlen(delimiter);
    const char* p = start;

    while (*p) {
        // Check for escape
        if (respect_escapes && *p == '\\' && *(p+1)) {
            p += 2;
            continue;
        }

        // Check for delimiter
        if (strncmp(p, delimiter, delim_len) == 0) {
            return p;
        }

        p++;
    }

    return nullptr;
}

} // namespace markup
} // namespace lambda
