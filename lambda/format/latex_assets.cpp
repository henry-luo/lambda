// latex_assets.cpp - Asset management implementation for LaTeX to HTML
// Handles CSS stylesheets, fonts, and JavaScript for proper HTML rendering

#include "latex_assets.hpp"
#include <fstream>
#include <sstream>
#include <cstring>

namespace lambda {

// Static list of document classes
static const std::vector<std::string> DOCUMENT_CLASSES = {
    "article", "book", "report"
};

// Map document class to CSS file
static const char* DOC_CLASS_CSS[] = {
    "article", "css/article.css",
    "book",    "css/book.css",
    "report",  "css/book.css",  // Report uses book.css
    nullptr,   nullptr
};

std::string LatexAssets::getDefaultAssetDir() {
    // Default to relative path from executable
    // In production, this should be configurable or use an environment variable
    return "lambda/input/latex/";
}

const char* LatexAssets::getCssPath(const char* doc_class) {
    if (!doc_class || !*doc_class) {
        return "css/article.css";  // Default
    }

    for (int i = 0; DOC_CLASS_CSS[i] != nullptr; i += 2) {
        if (strcmp(DOC_CLASS_CSS[i], doc_class) == 0) {
            return DOC_CLASS_CSS[i + 1];
        }
    }

    return "css/article.css";  // Default fallback
}

const std::vector<std::string>& LatexAssets::getDocumentClasses() {
    return DOCUMENT_CLASSES;
}

std::string LatexAssets::readFile(const char* filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string LatexAssets::getStylesheetLinks(const char* doc_class,
                                            const char* base_url) {
    std::ostringstream out;
    std::string base = base_url ? base_url : "";

    // Ensure base URL ends with / if not empty
    if (!base.empty() && base.back() != '/') {
        base += '/';
    }

    // KaTeX CSS for math rendering
    out << "    <link rel=\"stylesheet\" type=\"text/css\" href=\""
        << base << "css/katex.css\">\n";

    // Note: base.css not included - it uses CSS variables (var()) which aren't
    // fully supported by the layout engine. article.css has simplified styles.

    // Document class CSS (article.css, book.css, etc.)
    const char* css_path = getCssPath(doc_class);
    out << "    <link rel=\"stylesheet\" type=\"text/css\" href=\""
        << base << css_path << "\">\n";

    return out.str();
}

std::string LatexAssets::getEmbeddedStyles(const char* doc_class,
                                           const char* asset_dir) {
    std::ostringstream out;
    std::string dir = asset_dir ? asset_dir : getDefaultAssetDir();

    // Ensure directory ends with /
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }

    // Read and embed base.css
    std::string base_css = readFile((dir + "css/base.css").c_str());

    // Read and embed document class CSS
    const char* css_path = getCssPath(doc_class);
    std::string class_css = readFile((dir + css_path).c_str());

    // Read KaTeX CSS
    std::string katex_css = readFile((dir + "css/katex.css").c_str());

    out << "<style>\n";

    // Note: When embedding, we need to inline the @import from base.css
    // For simplicity, we embed fonts CSS first, then base, then class-specific
    std::string fonts_css = readFile((dir + "fonts/cmu.css").c_str());
    if (!fonts_css.empty()) {
        out << "/* Computer Modern Fonts */\n" << fonts_css << "\n";
    }

    // Embed base.css without the @import line
    if (!base_css.empty()) {
        // Skip the @import line at the beginning
        size_t import_end = base_css.find('\n');
        if (import_end != std::string::npos &&
            base_css.substr(0, 7) == "@import") {
            out << "/* Base Styles */\n" << base_css.substr(import_end + 1) << "\n";
        } else {
            out << "/* Base Styles */\n" << base_css << "\n";
        }
    }

    // Embed document class CSS without @import
    if (!class_css.empty()) {
        size_t import_end = class_css.find('\n');
        if (import_end != std::string::npos &&
            class_css.substr(0, 7) == "@import") {
            out << "/* Document Class Styles */\n" << class_css.substr(import_end + 1) << "\n";
        } else {
            out << "/* Document Class Styles */\n" << class_css << "\n";
        }
    }

    // Embed KaTeX CSS
    if (!katex_css.empty()) {
        out << "/* KaTeX Math Styles */\n" << katex_css << "\n";
    }

    out << "</style>\n";

    return out.str();
}

std::string LatexAssets::getScript(AssetMode mode, const char* base_url) {
    std::ostringstream out;

    if (mode == AssetMode::LINK) {
        std::string base = base_url ? base_url : "";
        if (!base.empty() && base.back() != '/') {
            base += '/';
        }
        out << "    <script src=\"" << base << "js/base.js\"></script>\n";
    } else {
        // For EMBED mode, inline the script
        std::string js = readFile((getDefaultAssetDir() + "js/base.js").c_str());
        if (!js.empty()) {
            out << "<script>\n" << js << "\n</script>\n";
        }
    }

    return out.str();
}

std::string LatexAssets::generateHeadContent(const char* doc_class,
                                             const LatexAssetConfig& config) {
    std::ostringstream out;

    out << "    <meta charset=\"UTF-8\">\n";
    out << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";

    if (config.mode == AssetMode::LINK) {
        out << getStylesheetLinks(doc_class,
                                  config.base_url.empty() ? nullptr : config.base_url.c_str());
    } else {
        out << getEmbeddedStyles(doc_class,
                                 config.asset_dir.empty() ? nullptr : config.asset_dir.c_str());
    }

    if (config.include_js) {
        out << getScript(config.mode,
                        config.base_url.empty() ? nullptr : config.base_url.c_str());
    }

    return out.str();
}

} // namespace lambda
