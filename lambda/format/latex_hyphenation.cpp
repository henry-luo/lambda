// latex_hyphenation.cpp - Text hyphenation for LaTeX to HTML conversion
// Implements soft hyphen insertion using simplified TeX hyphenation patterns

#include "latex_hyphenation.hpp"
#include <cctype>
#include <algorithm>

namespace lambda {

Hyphenator& Hyphenator::instance() {
    static Hyphenator instance;
    return instance;
}

Hyphenator::Hyphenator() {
    initPatterns();
}

// Common English hyphenation patterns (simplified subset of TeX patterns)
// Format: pattern string -> array of break priorities (odd = break ok, even = no break)
// These are applied to words with '.' at start and end: ".word."
void Hyphenator::initPatterns() {
    // Common suffix patterns
    patterns_["tion"] = {0, 0, 1, 0, 0};  // -tion
    patterns_["sion"] = {0, 0, 1, 0, 0};  // -sion
    patterns_["ing"] = {0, 1, 0, 0};      // -ing
    patterns_["ment"] = {0, 1, 0, 0, 0};  // -ment
    patterns_["ness"] = {0, 1, 0, 0, 0};  // -ness
    patterns_["able"] = {0, 1, 0, 0, 0};  // -able
    patterns_["ible"] = {0, 1, 0, 0, 0};  // -ible
    patterns_["ful"] = {0, 1, 0, 0};      // -ful
    patterns_["less"] = {0, 1, 0, 0, 0};  // -less
    patterns_["ous"] = {0, 1, 0, 0};      // -ous
    patterns_["ive"] = {0, 1, 0, 0};      // -ive
    patterns_["ure"] = {0, 1, 0, 0};      // -ure
    patterns_["ize"] = {0, 1, 0, 0};      // -ize
    patterns_["ise"] = {0, 1, 0, 0};      // -ise
    patterns_["ly"] = {0, 1, 0};          // -ly
    patterns_["er"] = {0, 1, 0};          // -er
    patterns_["ed"] = {0, 1, 0};          // -ed
    patterns_["es"] = {0, 1, 0};          // -es
    
    // Common prefix patterns
    patterns_["pre"] = {0, 0, 0, 1};      // pre-
    patterns_["pro"] = {0, 0, 0, 1};      // pro-
    patterns_["con"] = {0, 0, 0, 1};      // con-
    patterns_["com"] = {0, 0, 0, 1};      // com-
    patterns_["dis"] = {0, 0, 0, 1};      // dis-
    patterns_["mis"] = {0, 0, 0, 1};      // mis-
    patterns_["sub"] = {0, 0, 0, 1};      // sub-
    patterns_["super"] = {0, 0, 0, 0, 0, 1}; // super-
    patterns_["inter"] = {0, 0, 0, 0, 0, 1}; // inter-
    patterns_["under"] = {0, 0, 0, 0, 0, 1}; // under-
    patterns_["over"] = {0, 0, 0, 0, 1};  // over-
    patterns_["anti"] = {0, 0, 0, 0, 1};  // anti-
    patterns_["auto"] = {0, 0, 0, 0, 1};  // auto-
    patterns_["semi"] = {0, 0, 0, 0, 1};  // semi-
    
    // Vowel-consonant patterns (VC-CV rule)
    patterns_["ble"] = {0, 1, 0, 0};      // consonant + le
    patterns_["cle"] = {0, 1, 0, 0};
    patterns_["dle"] = {0, 1, 0, 0};
    patterns_["fle"] = {0, 1, 0, 0};
    patterns_["gle"] = {0, 1, 0, 0};
    patterns_["kle"] = {0, 1, 0, 0};
    patterns_["ple"] = {0, 1, 0, 0};
    patterns_["tle"] = {0, 1, 0, 0};
    patterns_["zle"] = {0, 1, 0, 0};
    
    // Double consonant patterns (break between)
    patterns_["bb"] = {0, 1, 0};
    patterns_["cc"] = {0, 1, 0};
    patterns_["dd"] = {0, 1, 0};
    patterns_["ff"] = {0, 1, 0};
    patterns_["gg"] = {0, 1, 0};
    patterns_["ll"] = {0, 1, 0};
    patterns_["mm"] = {0, 1, 0};
    patterns_["nn"] = {0, 1, 0};
    patterns_["pp"] = {0, 1, 0};
    patterns_["rr"] = {0, 1, 0};
    patterns_["ss"] = {0, 1, 0};
    patterns_["tt"] = {0, 1, 0};
    patterns_["zz"] = {0, 1, 0};
    
    // Common word breaks
    patterns_["ation"] = {0, 0, 1, 0, 0, 0};   // a-tion
    patterns_["ition"] = {0, 0, 1, 0, 0, 0};   // i-tion
    patterns_["ution"] = {0, 0, 1, 0, 0, 0};   // u-tion
    patterns_["ction"] = {0, 0, 1, 0, 0, 0};   // c-tion -> break before tion
    patterns_["acter"] = {0, 0, 1, 0, 0, 0};   // char-ac-ter
    patterns_["ument"] = {0, 0, 1, 0, 0, 0};   // doc-u-ment
    patterns_["ement"] = {0, 0, 1, 0, 0, 0};   // -ement
    patterns_["iment"] = {0, 0, 1, 0, 0, 0};   // -iment
}

std::vector<int> Hyphenator::getPattern(const std::string& key) const {
    auto it = patterns_.find(key);
    if (it != patterns_.end()) {
        return it->second;
    }
    return {};
}

std::string Hyphenator::hyphenateWord(const std::string& word) const {
    // Don't hyphenate short words
    if (word.length() < (size_t)min_word_length_) {
        return word;
    }
    
    // Convert to lowercase for pattern matching
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Initialize break points array (one more than word length)
    std::vector<int> points(word.length() + 1, 0);
    
    // Apply patterns at each position
    for (size_t i = 0; i < lower.length(); i++) {
        // Try patterns of various lengths starting at position i
        for (size_t len = 2; len <= 6 && i + len <= lower.length(); len++) {
            std::string sub = lower.substr(i, len);
            auto pattern = getPattern(sub);
            if (!pattern.empty()) {
                // Apply pattern - take max of current and pattern values
                for (size_t j = 0; j < pattern.size() && i + j <= word.length(); j++) {
                    if (pattern[j] > points[i + j]) {
                        points[i + j] = pattern[j];
                    }
                }
            }
        }
    }
    
    // Build result with soft hyphens at odd-numbered break points
    std::string result;
    for (size_t i = 0; i < word.length(); i++) {
        result += word[i];
        
        // Check if we can insert a hyphen after this character
        // Must respect left_min_ and right_min_ margins
        if (i >= (size_t)(left_min_ - 1) && 
            i < word.length() - (size_t)right_min_ &&
            points[i + 1] % 2 == 1) {
            result += SOFT_HYPHEN;
        }
    }
    
    return result;
}

std::string Hyphenator::hyphenateText(const std::string& text) const {
    if (!enabled_) {
        return text;
    }
    
    std::string result;
    std::string word;
    
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = text[i];
        
        // Check for multi-byte UTF-8 sequences
        int bytes = 1;
        if ((c & 0x80) == 0) {
            bytes = 1;  // ASCII
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
        }
        
        // Check if this is an alphabetic character (ASCII only for now)
        bool is_alpha = (bytes == 1 && std::isalpha(c));
        
        if (is_alpha) {
            // Accumulate word characters
            word += c;
            i++;
        } else {
            // Non-alphabetic: flush word and add character
            if (!word.empty()) {
                result += hyphenateWord(word);
                word.clear();
            }
            // Add the non-alphabetic character(s)
            for (int j = 0; j < bytes && i + j < text.length(); j++) {
                result += text[i + j];
            }
            i += bytes;
        }
    }
    
    // Don't forget final word
    if (!word.empty()) {
        result += hyphenateWord(word);
    }
    
    return result;
}

} // namespace lambda
