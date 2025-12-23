// latex_hyphenation.hpp - Text hyphenation for LaTeX to HTML conversion
// Implements soft hyphen insertion using TeX hyphenation patterns

#ifndef LATEX_HYPHENATION_HPP
#define LATEX_HYPHENATION_HPP

#include <string>
#include <vector>
#include <unordered_map>

namespace lambda {

/**
 * Hyphenator - Inserts soft hyphens into text for proper line breaking.
 * 
 * Based on the Liang hyphenation algorithm used in TeX.
 * Uses English (US) hyphenation patterns.
 * 
 * Soft hyphen (U+00AD) is an invisible character that suggests
 * where a word can be broken if necessary for line wrapping.
 */
class Hyphenator {
public:
    /**
     * Get the singleton instance.
     */
    static Hyphenator& instance();
    
    /**
     * Hyphenate a single word.
     * Returns the word with soft hyphens inserted at valid break points.
     * 
     * @param word The word to hyphenate
     * @return Word with soft hyphens (U+00AD) inserted
     */
    std::string hyphenateWord(const std::string& word) const;
    
    /**
     * Hyphenate text, preserving non-alphabetic characters.
     * Only words (sequences of alphabetic characters) are hyphenated.
     * 
     * @param text The text to hyphenate
     * @return Text with soft hyphens inserted in words
     */
    std::string hyphenateText(const std::string& text) const;
    
    /**
     * Set minimum word length for hyphenation.
     * Words shorter than this are not hyphenated.
     * Default: 4 characters
     */
    void setMinWordLength(int len) { min_word_length_ = len; }
    
    /**
     * Set minimum characters before first hyphen.
     * Default: 2
     */
    void setLeftMin(int len) { left_min_ = len; }
    
    /**
     * Set minimum characters after last hyphen.
     * Default: 3
     */
    void setRightMin(int len) { right_min_ = len; }
    
    /**
     * Enable or disable hyphenation.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    Hyphenator();
    void initPatterns();
    
    // Look up hyphenation pattern for a substring
    std::vector<int> getPattern(const std::string& key) const;
    
    // Soft hyphen character (U+00AD)
    static constexpr const char* SOFT_HYPHEN = "\xC2\xAD";
    
    // Hyphenation patterns: key -> break points array
    std::unordered_map<std::string, std::vector<int>> patterns_;
    
    int min_word_length_ = 4;
    int left_min_ = 2;
    int right_min_ = 3;
    bool enabled_ = true;
};

} // namespace lambda

#endif // LATEX_HYPHENATION_HPP
