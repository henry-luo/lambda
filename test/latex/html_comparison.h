#ifndef HTML_COMPARISON_H
#define HTML_COMPARISON_H

#include <string>
#include <vector>

struct HtmlDifference {
    enum Type {
        CONTENT_MISMATCH,
        STRUCTURE_MISMATCH,
        ATTRIBUTE_MISMATCH,
        WHITESPACE_DIFFERENCE
    };
    
    Type type;
    std::string expected;
    std::string actual;
    std::string context;
    size_t position;
};

class HtmlComparator {
public:
    HtmlComparator();
    ~HtmlComparator();
    
    // Compare HTML strings with normalization
    bool compare_html(const std::string& expected, const std::string& actual);
    
    // Compare with detailed difference reporting
    bool compare_html_detailed(const std::string& expected, const std::string& actual, 
                              std::vector<HtmlDifference>& differences);
    
    // Configuration options
    void set_ignore_whitespace(bool ignore);
    void set_normalize_attributes(bool normalize);
    void set_case_sensitive(bool sensitive);
    
    // Get last comparison details
    const std::vector<HtmlDifference>& get_last_differences() const;
    std::string get_comparison_report() const;
    
private:
    bool ignore_whitespace_;
    bool normalize_attributes_;
    bool case_sensitive_;
    std::vector<HtmlDifference> last_differences_;
    
    // Normalization functions
    std::string normalize_html(const std::string& html);
    std::string normalize_whitespace(const std::string& html);
    std::string normalize_attributes(const std::string& html);
    std::string remove_comments(const std::string& html);
    
    // Comparison helpers
    bool compare_normalized(const std::string& expected, const std::string& actual);
    void find_differences(const std::string& expected, const std::string& actual);
    std::string extract_context(const std::string& text, size_t position, size_t context_size = 50);
};

#endif // HTML_COMPARISON_H
