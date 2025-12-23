// latex_assets.hpp - Asset management for LaTeX to HTML conversion
// Handles CSS stylesheets, fonts, and JavaScript for proper HTML rendering

#ifndef LATEX_ASSETS_HPP
#define LATEX_ASSETS_HPP

#include <string>
#include <vector>

namespace lambda {

/**
 * Asset output modes for LaTeX to HTML conversion.
 */
enum class AssetMode {
    LINK,      // Generate <link> and <script> tags pointing to external files
    EMBED,     // Embed CSS/JS directly in <style> and <script> tags
    DATA_URI   // Embed fonts as data: URIs (self-contained HTML)
};

/**
 * Configuration for LaTeX asset handling.
 */
struct LatexAssetConfig {
    AssetMode mode = AssetMode::LINK;
    std::string base_url;           // Base URL for assets (empty = relative paths)
    std::string asset_dir;          // Directory containing assets
    bool include_katex = true;      // Include KaTeX CSS for math
    bool include_fonts = true;      // Include Computer Modern fonts
    bool include_js = true;         // Include base.js for dynamic features
};

/**
 * LatexAssets - Manages CSS, fonts, and JavaScript assets for LaTeX HTML output.
 * 
 * Assets are located in lambda/input/latex/ directory:
 *   css/     - Stylesheets (base.css, article.css, book.css, katex.css)
 *   fonts/   - Computer Modern Unicode fonts (WOFF format)
 *   js/      - JavaScript for dynamic features (marginpar positioning)
 */
class LatexAssets {
public:
    /**
     * Get the default asset directory path.
     * Returns the path to lambda/input/latex/ relative to the executable.
     */
    static std::string getDefaultAssetDir();
    
    /**
     * Generate HTML <head> content with stylesheet links.
     * 
     * @param doc_class Document class name (article, book, report)
     * @param config Asset configuration
     * @return HTML string with <link> and <style> tags
     */
    static std::string generateHeadContent(const char* doc_class,
                                           const LatexAssetConfig& config);
    
    /**
     * Generate stylesheet links for a document class.
     * 
     * @param doc_class Document class name
     * @param base_url Base URL for stylesheet paths (nullptr for relative)
     * @return HTML <link> tags
     */
    static std::string getStylesheetLinks(const char* doc_class,
                                          const char* base_url = nullptr);
    
    /**
     * Get embedded stylesheet content (for EMBED mode).
     * 
     * @param doc_class Document class name
     * @param asset_dir Directory containing CSS files
     * @return CSS content wrapped in <style> tags
     */
    static std::string getEmbeddedStyles(const char* doc_class,
                                         const char* asset_dir);
    
    /**
     * Get JavaScript content for dynamic features.
     * 
     * @param mode Asset mode (determines link vs embed)
     * @param base_url Base URL for script path
     * @return HTML <script> tag or embedded script
     */
    static std::string getScript(AssetMode mode, const char* base_url = nullptr);
    
    /**
     * Read a file's contents as a string.
     * 
     * @param filepath Path to file
     * @return File contents, or empty string on error
     */
    static std::string readFile(const char* filepath);
    
    /**
     * Get the CSS file path for a document class.
     * 
     * @param doc_class Document class name
     * @return Relative path to CSS file (e.g., "css/article.css")
     */
    static const char* getCssPath(const char* doc_class);
    
    /**
     * List of available document classes.
     */
    static const std::vector<std::string>& getDocumentClasses();
};

} // namespace lambda

#endif // LATEX_ASSETS_HPP
