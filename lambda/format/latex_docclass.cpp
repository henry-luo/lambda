// latex_docclass.cpp - Document class system implementation
// Implements article, book, report document classes

#include "latex_docclass.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace lambda {

// =============================================================================
// DocLength implementation
// =============================================================================

std::string DocLength::toCss() const {
    std::ostringstream out;
    out << value << unit;
    return out.str();
}

double DocLength::toPoints() const {
    // Convert to points for calculations
    if (unit == "pt") return value;
    if (unit == "in") return value * 72.0;
    if (unit == "mm") return value * 2.83465;
    if (unit == "cm") return value * 28.3465;
    if (unit == "em") return value * 10.0;  // Approximate based on 10pt font
    if (unit == "px") return value * 0.75;
    return value;
}

DocLength DocLength::add(const DocLength& other) const {
    return DocLength(toPoints() + other.toPoints(), "pt");
}

DocLength DocLength::sub(const DocLength& other) const {
    return DocLength(toPoints() - other.toPoints(), "pt");
}

DocLength DocLength::mul(double factor) const {
    return DocLength(value * factor, unit.c_str());
}

// =============================================================================
// DocClassOptions implementation
// =============================================================================

void DocClassOptions::parseOptions(const std::vector<std::string>& options) {
    for (const auto& opt : options) {
        // Trim whitespace
        std::string o = opt;
        o.erase(0, o.find_first_not_of(" \t"));
        o.erase(o.find_last_not_of(" \t") + 1);
        
        if (o.empty()) continue;
        
        // Paper sizes
        if (o == "a4paper") {
            paper_size = o;
            paper = PaperSize::A4();
        } else if (o == "a5paper") {
            paper_size = o;
            paper = PaperSize::A5();
        } else if (o == "b5paper") {
            paper_size = o;
            paper = PaperSize::B5();
        } else if (o == "letterpaper") {
            paper_size = o;
            paper = PaperSize::Letter();
        } else if (o == "legalpaper") {
            paper_size = o;
            paper = PaperSize::Legal();
        } else if (o == "executivepaper") {
            paper_size = o;
            paper = PaperSize::Executive();
        } else if (o == "landscape") {
            landscape = true;
            std::swap(paper.width, paper.height);
        }
        // Layout options
        else if (o == "oneside") {
            two_side = false;
        } else if (o == "twoside") {
            two_side = true;
        } else if (o == "onecolumn") {
            two_column = false;
        } else if (o == "twocolumn") {
            two_column = true;
        } else if (o == "titlepage") {
            title_page = true;
        } else if (o == "notitlepage") {
            title_page = false;
        }
        // Math options
        else if (o == "fleqn") {
            fleqn = true;
        } else if (o == "leqno") {
            leqno = true;
        }
        // Font size (e.g., "10pt", "11pt", "12pt")
        else if (o.size() > 2 && o.substr(o.size() - 2) == "pt") {
            try {
                double size = std::stod(o.substr(0, o.size() - 2));
                if (size >= 8 && size <= 20) {
                    base_font_size = size;
                }
            } catch (...) {
                // Ignore invalid font sizes
            }
        }
    }
}

// =============================================================================
// DocumentClass base implementation
// =============================================================================

void DocumentClass::initCounters(std::map<std::string, DocCounter>& counters) const {
    // Standard counters for all document classes
    counters["part"] = {"part", 0, "", {}};
    counters["section"] = {"section", 0, "", {"subsection"}};
    counters["subsection"] = {"subsection", 0, "section", {"subsubsection"}};
    counters["subsubsection"] = {"subsubsection", 0, "subsection", {"paragraph"}};
    counters["paragraph"] = {"paragraph", 0, "subsubsection", {"subparagraph"}};
    counters["subparagraph"] = {"subparagraph", 0, "paragraph", {}};
    
    counters["figure"] = {"figure", 0, "", {}};
    counters["table"] = {"table", 0, "", {}};
    
    counters["footnote"] = {"footnote", 0, "", {}};
    counters["mpfootnote"] = {"mpfootnote", 0, "", {}};
    
    counters["enumi"] = {"enumi", 0, "", {}};
    counters["enumii"] = {"enumii", 0, "", {}};
    counters["enumiii"] = {"enumiii", 0, "", {}};
    counters["enumiv"] = {"enumiv", 0, "", {}};
    
    counters["equation"] = {"equation", 0, "", {}};
    
    counters["secnumdepth"] = {"secnumdepth", secnumdepth(), "", {}};
    counters["tocdepth"] = {"tocdepth", tocdepth(), "", {}};
}

void DocumentClass::initLengths(std::map<std::string, DocLength>& lengths,
                                const DocClassOptions& options) const {
    // Page dimensions
    lengths["paperwidth"] = DocLength::fromPt(options.paper.width);
    lengths["paperheight"] = DocLength::fromPt(options.paper.height);
    
    // Font size
    lengths["@size"] = DocLength::fromPt(options.base_font_size);
    
    // Text width (345pt max or paperwidth - 2in, whichever is smaller)
    double margin = 72.0;  // 1 inch in points
    double textwidth = std::min(345.0, options.paper.width - 2 * margin);
    lengths["textwidth"] = DocLength::fromPt(textwidth);
    
    // Margins
    double margins = options.paper.width - textwidth;
    double oddsidemargin = margins / 2.0 - 72.0;  // Relative to 1in
    lengths["oddsidemargin"] = DocLength::fromPt(oddsidemargin);
    lengths["evensidemargin"] = DocLength::fromPt(oddsidemargin);
    
    // Margin paragraph
    lengths["marginparsep"] = DocLength::fromPt(11);
    lengths["marginparpush"] = DocLength::fromPt(5);
    double marginparwidth = std::min(144.0, margins / 2.0 - 11 - 57.6);  // Max 2in
    lengths["marginparwidth"] = DocLength::fromPt(std::max(0.0, marginparwidth));
    
    // Indentation
    lengths["parindent"] = DocLength::fromEm(1.5);
    lengths["parskip"] = DocLength::fromPt(0);
    
    // List lengths
    lengths["leftmargini"] = DocLength::fromEm(2.5);
    lengths["leftmarginii"] = DocLength::fromEm(2.2);
    lengths["leftmarginiii"] = DocLength::fromEm(1.87);
    lengths["leftmarginiv"] = DocLength::fromEm(1.7);
    lengths["leftmarginv"] = DocLength::fromEm(1.0);
    lengths["leftmarginvi"] = DocLength::fromEm(1.0);
    lengths["labelsep"] = DocLength::fromEm(0.5);
    
    // Box lengths
    lengths["fboxrule"] = DocLength::fromPt(0.4);
    lengths["fboxsep"] = DocLength::fromPt(3);
    
    // Spacing
    lengths["smallskipamount"] = DocLength::fromEm(0.3);
    lengths["medskipamount"] = DocLength::fromEm(0.6);
    lengths["bigskipamount"] = DocLength::fromEm(1.2);
    
    // Picture
    lengths["unitlength"] = DocLength::fromPt(1);
}

std::string DocumentClass::formatArabic(int n) {
    return std::to_string(n);
}

std::string DocumentClass::formatRoman(int n, bool uppercase) {
    if (n <= 0 || n > 3999) return formatArabic(n);
    
    static const char* ones_lower[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"};
    static const char* tens_lower[] = {"", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"};
    static const char* hundreds_lower[] = {"", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"};
    static const char* thousands_lower[] = {"", "m", "mm", "mmm"};
    
    static const char* ones_upper[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
    static const char* tens_upper[] = {"", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"};
    static const char* hundreds_upper[] = {"", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"};
    static const char* thousands_upper[] = {"", "M", "MM", "MMM"};
    
    std::string result;
    if (uppercase) {
        result = std::string(thousands_upper[n / 1000]) +
                 hundreds_upper[(n % 1000) / 100] +
                 tens_upper[(n % 100) / 10] +
                 ones_upper[n % 10];
    } else {
        result = std::string(thousands_lower[n / 1000]) +
                 hundreds_lower[(n % 1000) / 100] +
                 tens_lower[(n % 100) / 10] +
                 ones_lower[n % 10];
    }
    return result;
}

std::string DocumentClass::formatAlph(int n, bool uppercase) {
    if (n < 1 || n > 26) return formatArabic(n);
    char c = (uppercase ? 'A' : 'a') + (n - 1);
    return std::string(1, c);
}

std::string DocumentClass::formatCounter(const char* counter_name, int value) const {
    std::string name = counter_name;
    
    // Part uses Roman numerals
    if (name == "part") {
        return formatRoman(value, true);
    }
    // Enumerate counters
    if (name == "enumi") {
        return formatArabic(value);
    }
    if (name == "enumii") {
        return "(" + formatAlph(value, false) + ")";
    }
    if (name == "enumiii") {
        return formatRoman(value, false);
    }
    if (name == "enumiv") {
        return formatAlph(value, true);
    }
    
    // Default: arabic
    return formatArabic(value);
}

std::string DocumentClass::theCounter(const char* counter_name,
                                       const std::map<std::string, DocCounter>& counters) const {
    std::string name = counter_name;
    
    auto it = counters.find(name);
    if (it == counters.end()) {
        return "??";
    }
    
    int value = it->second.value;
    
    // Part is just the part number in Roman
    if (name == "part") {
        return formatRoman(value, true);
    }
    
    // Section is just the section number
    if (name == "section") {
        return formatArabic(value);
    }
    
    // Subsection is section.subsection
    if (name == "subsection") {
        auto sec_it = counters.find("section");
        int sec = sec_it != counters.end() ? sec_it->second.value : 0;
        return formatArabic(sec) + "." + formatArabic(value);
    }
    
    // Subsubsection is section.subsection.subsubsection
    if (name == "subsubsection") {
        auto sec_it = counters.find("section");
        auto subsec_it = counters.find("subsection");
        int sec = sec_it != counters.end() ? sec_it->second.value : 0;
        int subsec = subsec_it != counters.end() ? subsec_it->second.value : 0;
        return formatArabic(sec) + "." + formatArabic(subsec) + "." + formatArabic(value);
    }
    
    // Default
    return formatCounter(counter_name, value);
}

// =============================================================================
// ArticleClass implementation
// =============================================================================

void ArticleClass::initCounters(std::map<std::string, DocCounter>& counters) const {
    DocumentClass::initCounters(counters);
    counters["secnumdepth"].value = 3;
    counters["tocdepth"].value = 3;
}

// =============================================================================
// ReportClass implementation
// =============================================================================

void ReportClass::initCounters(std::map<std::string, DocCounter>& counters) const {
    DocumentClass::initCounters(counters);
    
    // Add chapter counter
    counters["chapter"] = {"chapter", 0, "", {"section", "figure", "table", "footnote"}};
    
    // Update section to reset on chapter
    counters["section"].parent = "chapter";
    counters["figure"].parent = "chapter";
    counters["table"].parent = "chapter";
    counters["footnote"].parent = "chapter";
    
    counters["secnumdepth"].value = 2;
    counters["tocdepth"].value = 2;
}

std::string ReportClass::theCounter(const char* counter_name,
                                     const std::map<std::string, DocCounter>& counters) const {
    std::string name = counter_name;
    
    auto it = counters.find(name);
    if (it == counters.end()) {
        return "??";
    }
    
    int value = it->second.value;
    
    // Chapter is just the chapter number
    if (name == "chapter") {
        return formatArabic(value);
    }
    
    // Section is chapter.section
    if (name == "section") {
        auto chap_it = counters.find("chapter");
        int chap = chap_it != counters.end() ? chap_it->second.value : 0;
        return formatArabic(chap) + "." + formatArabic(value);
    }
    
    // Figure/table is chapter.number (if chapter > 0)
    if (name == "figure" || name == "table") {
        auto chap_it = counters.find("chapter");
        int chap = chap_it != counters.end() ? chap_it->second.value : 0;
        if (chap > 0) {
            return formatArabic(chap) + "." + formatArabic(value);
        }
        return formatArabic(value);
    }
    
    // For others, fall back to base
    return DocumentClass::theCounter(counter_name, counters);
}

// =============================================================================
// BookClass implementation
// =============================================================================

void BookClass::initCounters(std::map<std::string, DocCounter>& counters) const {
    ReportClass::initCounters(counters);
    // Book is essentially the same as report for counters
}

// =============================================================================
// Factory function
// =============================================================================

std::unique_ptr<DocumentClass> createDocumentClass(const char* name) {
    if (!name || !*name) {
        return std::make_unique<ArticleClass>();
    }
    
    std::string class_name = name;
    
    // Convert to lowercase for comparison
    std::transform(class_name.begin(), class_name.end(), class_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (class_name == "article") {
        return std::make_unique<ArticleClass>();
    } else if (class_name == "report") {
        return std::make_unique<ReportClass>();
    } else if (class_name == "book") {
        return std::make_unique<BookClass>();
    }
    
    // Default to article
    return std::make_unique<ArticleClass>();
}

std::vector<std::string> parseDocClassOptions(const char* options) {
    std::vector<std::string> result;
    if (!options || !*options) {
        return result;
    }
    
    std::string opt_str = options;
    size_t start = 0;
    size_t end = 0;
    
    while ((end = opt_str.find(',', start)) != std::string::npos) {
        std::string opt = opt_str.substr(start, end - start);
        // Trim
        opt.erase(0, opt.find_first_not_of(" \t"));
        opt.erase(opt.find_last_not_of(" \t") + 1);
        if (!opt.empty()) {
            result.push_back(opt);
        }
        start = end + 1;
    }
    
    // Last option
    std::string opt = opt_str.substr(start);
    opt.erase(0, opt.find_first_not_of(" \t"));
    opt.erase(opt.find_last_not_of(" \t") + 1);
    if (!opt.empty()) {
        result.push_back(opt);
    }
    
    return result;
}

} // namespace lambda
