// latex_packages.hpp - Package system for LaTeX to HTML conversion
// Implements textgreek, textcomp, gensymb, hyperref, multicol, stix, etc.

#ifndef LATEX_PACKAGES_HPP
#define LATEX_PACKAGES_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <unordered_map>

namespace lambda {

// Forward declarations
class LatexProcessor;

/**
 * Base class for LaTeX packages.
 */
class LatexPackage {
public:
    virtual ~LatexPackage() = default;
    
    /**
     * Get the package name.
     */
    virtual const char* name() const = 0;
    
    /**
     * Get symbols defined by this package.
     * Map from command name (without backslash) to Unicode string.
     */
    virtual const std::map<std::string, std::string>& symbols() const {
        static const std::map<std::string, std::string> empty;
        return empty;
    }
    
    /**
     * Process package options.
     */
    virtual void processOptions(const std::vector<std::string>& options) {}
    
    /**
     * Check if this package provides a specific command.
     */
    virtual bool providesCommand(const char* cmd) const { return false; }
};

/**
 * textgreek package - Greek letters in text mode.
 */
class TextgreekPackage : public LatexPackage {
public:
    const char* name() const override { return "textgreek"; }
    const std::map<std::string, std::string>& symbols() const override;
};

/**
 * textcomp package - Extended text symbols.
 */
class TextcompPackage : public LatexPackage {
public:
    const char* name() const override { return "textcomp"; }
    const std::map<std::string, std::string>& symbols() const override;
};

/**
 * gensymb package - Generic symbols (degree, celsius, ohm, micro).
 */
class GensymbPackage : public LatexPackage {
public:
    const char* name() const override { return "gensymb"; }
    const std::map<std::string, std::string>& symbols() const override;
};

/**
 * stix package - STIX math symbols and extras.
 */
class StixPackage : public LatexPackage {
public:
    const char* name() const override { return "stix"; }
    const std::map<std::string, std::string>& symbols() const override;
};

/**
 * latexsym package - LaTeX symbol font.
 */
class LatexsymPackage : public LatexPackage {
public:
    const char* name() const override { return "latexsym"; }
    const std::map<std::string, std::string>& symbols() const override;
};

/**
 * hyperref package - Hyperlinks.
 */
class HyperrefPackage : public LatexPackage {
public:
    const char* name() const override { return "hyperref"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * multicol package - Multi-column layouts.
 */
class MulticolPackage : public LatexPackage {
public:
    const char* name() const override { return "multicol"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * graphicx package - Graphics inclusion.
 */
class GraphicxPackage : public LatexPackage {
public:
    const char* name() const override { return "graphicx"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * xcolor package - Extended color support.
 */
class XcolorPackage : public LatexPackage {
public:
    const char* name() const override { return "xcolor"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * comment package - Comment environment.
 */
class CommentPackage : public LatexPackage {
public:
    const char* name() const override { return "comment"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * calc package - Arithmetic in length expressions.
 */
class CalcPackage : public LatexPackage {
public:
    const char* name() const override { return "calc"; }
};

/**
 * picture/pict2e package - Picture environment.
 */
class PicturePackage : public LatexPackage {
public:
    const char* name() const override { return "pict2e"; }
    bool providesCommand(const char* cmd) const override;
};

/**
 * Package registry - manages loaded packages and symbol lookup.
 */
class PackageRegistry {
public:
    /**
     * Get the singleton instance.
     */
    static PackageRegistry& instance();
    
    /**
     * Load a package by name.
     * Returns true if the package was loaded successfully.
     */
    bool loadPackage(const char* name, const std::vector<std::string>& options = {});
    
    /**
     * Check if a package is loaded.
     */
    bool isLoaded(const char* name) const;
    
    /**
     * Check if a package name is built-in (provided by the system).
     */
    bool isBuiltIn(const char* name) const;
    
    /**
     * Look up a symbol from all loaded packages.
     * Returns the Unicode string if found, nullptr otherwise.
     */
    const char* lookupSymbol(const char* cmd) const;
    
    /**
     * Get all symbols from all loaded packages.
     */
    const std::unordered_map<std::string, std::string>& allSymbols() const;
    
    /**
     * Reset the registry (unload all packages).
     */
    void reset();
    
    /**
     * Get a loaded package by name.
     */
    LatexPackage* getPackage(const char* name) const;
    
private:
    PackageRegistry();
    
    // Factory functions for each package type
    using PackageFactory = std::function<std::unique_ptr<LatexPackage>()>;
    std::map<std::string, PackageFactory> factories_;
    
    // Loaded packages
    std::map<std::string, std::unique_ptr<LatexPackage>> loaded_;
    
    // Combined symbol table from all loaded packages
    mutable std::unordered_map<std::string, std::string> all_symbols_;
    mutable bool symbols_dirty_ = true;
    
    // Built-in packages (already provided by the system)
    static const std::vector<std::string> BUILTIN_PACKAGES;
};

/**
 * Get all base symbols defined in symbols.ls.
 * These are always available regardless of packages.
 */
const std::map<std::string, std::string>& getBaseSymbols();

/**
 * Get all diacritics defined in symbols.ls.
 * Maps diacritic command (e.g., "'") to [combining_char, standalone_char].
 */
const std::map<std::string, std::pair<std::string, std::string>>& getDiacritics();

/**
 * Get all ligatures defined in symbols.ls.
 */
const std::map<std::string, std::string>& getLigatures();

} // namespace lambda

#endif // LATEX_PACKAGES_HPP
