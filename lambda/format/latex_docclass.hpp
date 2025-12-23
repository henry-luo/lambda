// latex_docclass.hpp - Document class system for LaTeX to HTML conversion
// Implements article, book, report document classes

#ifndef LATEX_DOCCLASS_HPP
#define LATEX_DOCCLASS_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

namespace lambda {

// Forward declaration
class LatexProcessor;

/**
 * Paper size definitions (in points).
 */
struct PaperSize {
    double width;   // Width in points
    double height;  // Height in points
    
    static PaperSize A4()        { return {595.28, 841.89}; }   // 210mm x 297mm
    static PaperSize A5()        { return {419.53, 595.28}; }   // 148mm x 210mm
    static PaperSize B5()        { return {498.90, 708.66}; }   // 176mm x 250mm
    static PaperSize Letter()    { return {612.0, 792.0}; }     // 8.5in x 11in
    static PaperSize Legal()     { return {612.0, 1008.0}; }    // 8.5in x 14in
    static PaperSize Executive() { return {522.0, 756.0}; }     // 7.25in x 10.5in
};

/**
 * Length value with unit for CSS output (document class specific).
 * Note: This is a simpler version than the Length in latex_generator.hpp.
 */
struct DocLength {
    double value;
    std::string unit;
    
    DocLength() : value(0), unit("pt") {}
    DocLength(double v, const char* u) : value(v), unit(u) {}
    
    std::string toCss() const;
    DocLength add(const DocLength& other) const;
    DocLength sub(const DocLength& other) const;
    DocLength mul(double factor) const;
    double toPoints() const;
    
    static DocLength fromPt(double pt) { return DocLength(pt, "pt"); }
    static DocLength fromIn(double in) { return DocLength(in, "in"); }
    static DocLength fromMm(double mm) { return DocLength(mm, "mm"); }
    static DocLength fromEm(double em) { return DocLength(em, "em"); }
    static DocLength fromPx(double px) { return DocLength(px, "px"); }
};

/**
 * Document class options parsed from \documentclass[options]{class}.
 */
struct DocClassOptions {
    // Paper size
    std::string paper_size = "letterpaper";
    PaperSize paper = PaperSize::Letter();
    bool landscape = false;
    
    // Font size
    double base_font_size = 10.0;  // In points
    
    // Layout
    bool two_side = false;
    bool two_column = false;
    bool title_page = false;
    
    // Math
    bool fleqn = false;   // Flush left equations
    bool leqno = false;   // Left equation numbers
    
    // Parse options from string vector
    void parseOptions(const std::vector<std::string>& options);
};

/**
 * Counter definition for LaTeX counters (document class specific).
 */
struct DocCounter {
    std::string name;
    int value = 0;
    std::string parent;              // Parent counter (reset when parent increments)
    std::vector<std::string> resets; // Counters to reset when this increments
};

/**
 * Base document class - defines common behavior for all classes.
 */
class DocumentClass {
public:
    virtual ~DocumentClass() = default;
    
    /**
     * Get the name of this document class.
     */
    virtual const char* name() const = 0;
    
    /**
     * Get the CSS file path for this class.
     */
    virtual const char* cssFile() const = 0;
    
    /**
     * Initialize counters for this document class.
     */
    virtual void initCounters(std::map<std::string, DocCounter>& counters) const;
    
    /**
     * Initialize lengths for this document class.
     */
    virtual void initLengths(std::map<std::string, DocLength>& lengths,
                            const DocClassOptions& options) const;
    
    /**
     * Process class-specific options.
     */
    virtual void processOptions(DocClassOptions& options) const {}
    
    /**
     * Format a counter value (e.g., arabic, roman, alph).
     */
    virtual std::string formatCounter(const char* counter_name, int value) const;
    
    /**
     * Get the full formatted counter string (e.g., "1.2.3" for subsubsection).
     */
    virtual std::string theCounter(const char* counter_name,
                                   const std::map<std::string, DocCounter>& counters) const;
    
    /**
     * Get section numbering depth.
     */
    virtual int secnumdepth() const { return 3; }
    
    /**
     * Get table of contents depth.
     */
    virtual int tocdepth() const { return 3; }
    
    /**
     * Localized names.
     */
    virtual const char* contentsName() const { return "Contents"; }
    virtual const char* listFigureName() const { return "List of Figures"; }
    virtual const char* listTableName() const { return "List of Tables"; }
    virtual const char* refName() const { return "References"; }
    virtual const char* bibName() const { return "Bibliography"; }
    virtual const char* indexName() const { return "Index"; }
    virtual const char* figureName() const { return "Figure"; }
    virtual const char* tableName() const { return "Table"; }
    virtual const char* partName() const { return "Part"; }
    virtual const char* chapterName() const { return "Chapter"; }
    virtual const char* appendixName() const { return "Appendix"; }
    virtual const char* abstractName() const { return "Abstract"; }
    
    /**
     * Does this class support chapters?
     */
    virtual bool hasChapters() const { return false; }
    
protected:
    // Helper to format numbers
    static std::string formatArabic(int n);
    static std::string formatRoman(int n, bool uppercase = false);
    static std::string formatAlph(int n, bool uppercase = false);
};

/**
 * Article document class.
 */
class ArticleClass : public DocumentClass {
public:
    const char* name() const override { return "article"; }
    const char* cssFile() const override { return "css/article.css"; }
    
    void initCounters(std::map<std::string, DocCounter>& counters) const override;
    int secnumdepth() const override { return 3; }
    int tocdepth() const override { return 3; }
};

/**
 * Report document class.
 */
class ReportClass : public DocumentClass {
public:
    const char* name() const override { return "report"; }
    const char* cssFile() const override { return "css/book.css"; }
    
    void initCounters(std::map<std::string, DocCounter>& counters) const override;
    int secnumdepth() const override { return 2; }
    int tocdepth() const override { return 2; }
    bool hasChapters() const override { return true; }
    
    std::string theCounter(const char* counter_name,
                          const std::map<std::string, DocCounter>& counters) const override;
};

/**
 * Book document class.
 */
class BookClass : public ReportClass {
public:
    const char* name() const override { return "book"; }
    const char* cssFile() const override { return "css/book.css"; }
    
    void initCounters(std::map<std::string, DocCounter>& counters) const override;
};

/**
 * Factory function to create document class by name.
 */
std::unique_ptr<DocumentClass> createDocumentClass(const char* name);

/**
 * Parse document class options from a comma-separated string.
 */
std::vector<std::string> parseDocClassOptions(const char* options);

} // namespace lambda

#endif // LATEX_DOCCLASS_HPP
