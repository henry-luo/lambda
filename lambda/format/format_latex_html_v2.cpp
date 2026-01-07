// format_latex_html_v2.cpp - Main entry point for LaTeX to HTML conversion
// Processes Lambda Element tree from input-latex-ts.cpp parser

#include "format.h"
#include "html_writer.hpp"
#include "html_generator.hpp"
#include "latex_packages.hpp"
#include "latex_docclass.hpp"
#include "latex_assets.hpp"
#include "latex_picture.hpp"
#include "latex_hyphenation.hpp"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../input/input.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/stringbuf.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <strings.h>
#include <map>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// external function for evaluating LaTeX numeric expressions
extern "C" int latex_eval_num_expr(const char* expr);

namespace lambda {

// =============================================================================
// Space-Absorbing Commands - Commands that consume following whitespace
// =============================================================================

// LaTeX commands that absorb following whitespace per LaTeX semantics
// After these commands, we need ZWS markers to preserve word boundaries in HTML
static const std::unordered_set<std::string> SPACE_ABSORBING_COMMANDS = {
    // Logo commands (no arguments)
    "LaTeX", "TeX", "LaTeXe",
    // Font size commands (no arguments when used as declarations)
    "tiny", "scriptsize", "footnotesize", "small", "normalsize",
    "large", "Large", "LARGE", "huge", "Huge",
    // Special commands
    "empty",  // From whitespace_tex_15
    // Note: text styling commands like \textbf, \emph, etc. are NOT included
    // because they take arguments and the argument handling prevents space absorption
    // Add more as needed based on test failures
};

// Check if a command absorbs following whitespace
static bool commandAbsorbsSpace(const char* cmd_name) {
    if (!cmd_name) return false;
    return SPACE_ABSORBING_COMMANDS.count(cmd_name) > 0;
}

// =============================================================================
// Diacritic Support - Maps LaTeX diacritic commands + base char to Unicode
// =============================================================================

// Map from (diacritic_command, base_char) -> precomposed Unicode character
// This uses Unicode combining characters as fallback when precomposed not available
struct DiacriticKey {
    char cmd;
    uint32_t base_char;  // UTF-32 codepoint

    bool operator==(const DiacriticKey& other) const {
        return cmd == other.cmd && base_char == other.base_char;
    }
};

struct DiacriticKeyHash {
    size_t operator()(const DiacriticKey& k) const {
        return std::hash<char>()(k.cmd) ^ (std::hash<uint32_t>()(k.base_char) << 8);
    }
};

// Diacritic command -> Unicode combining character mapping
static const std::unordered_map<char, uint32_t> diacritic_combining_map = {
    {'\'', 0x0301},  // combining acute accent
    {'`', 0x0300},   // combining grave accent
    {'^', 0x0302},   // combining circumflex
    {'"', 0x0308},   // combining diaeresis (umlaut)
    {'~', 0x0303},   // combining tilde
    {'=', 0x0304},   // combining macron
    {'.', 0x0307},   // combining dot above
    {'u', 0x0306},   // combining breve
    {'v', 0x030C},   // combining caron (háček)
    {'H', 0x030B},   // combining double acute
    {'c', 0x0327},   // combining cedilla
    {'d', 0x0323},   // combining dot below
    {'b', 0x0331},   // combining macron below
    {'r', 0x030A},   // combining ring above
    {'k', 0x0328},   // combining ogonek
};

// Common precomposed diacritic characters for better rendering
static const std::unordered_map<DiacriticKey, const char*, DiacriticKeyHash> diacritic_precomposed = {
    // Acute accent (')
    {{'\'',' '}, "'"}, // handle space as combining char only
    {{'\'','a'}, "á"}, {{'\'','e'}, "é"}, {{'\'','i'}, "í"}, {{'\'','o'}, "ó"}, {{'\'','u'}, "ú"},
    {{'\'','A'}, "Á"}, {{'\'','E'}, "É"}, {{'\'','I'}, "Í"}, {{'\'','O'}, "Ó"}, {{'\'','U'}, "Ú"},
    {{'\'','y'}, "ý"}, {{'\'','Y'}, "Ý"}, {{'\'','c'}, "ć"}, {{'\'','C'}, "Ć"},
    {{'\'','n'}, "ń"}, {{'\'','N'}, "Ń"}, {{'\'','s'}, "ś"}, {{'\'','S'}, "Ś"},
    {{'\'','z'}, "ź"}, {{'\'','Z'}, "Ź"}, {{'\'','l'}, "ĺ"}, {{'\'','L'}, "Ĺ"},
    {{'\'','r'}, "ŕ"}, {{'\'','R'}, "Ŕ"},

    // Grave accent (`)
    {{'`','a'}, "à"}, {{'`','e'}, "è"}, {{'`','i'}, "ì"}, {{'`','o'}, "ò"}, {{'`','u'}, "ù"},
    {{'`','A'}, "À"}, {{'`','E'}, "È"}, {{'`','I'}, "Ì"}, {{'`','O'}, "Ò"}, {{'`','U'}, "Ù"},

    // Circumflex (^)
    {{'^','a'}, "â"}, {{'^','e'}, "ê"}, {{'^','i'}, "î"}, {{'^','o'}, "ô"}, {{'^','u'}, "û"},
    {{'^','A'}, "Â"}, {{'^','E'}, "Ê"}, {{'^','I'}, "Î"}, {{'^','O'}, "Ô"}, {{'^','U'}, "Û"},
    {{'^','c'}, "ĉ"}, {{'^','C'}, "Ĉ"}, {{'^','g'}, "ĝ"}, {{'^','G'}, "Ĝ"},
    {{'^','h'}, "ĥ"}, {{'^','H'}, "Ĥ"}, {{'^','j'}, "ĵ"}, {{'^','J'}, "Ĵ"},
    {{'^','s'}, "ŝ"}, {{'^','S'}, "Ŝ"}, {{'^','w'}, "ŵ"}, {{'^','W'}, "Ŵ"},
    {{'^','y'}, "ŷ"}, {{'^','Y'}, "Ŷ"},

    // Diaeresis/umlaut (")
    {{'"','a'}, "ä"}, {{'"','e'}, "ë"}, {{'"','i'}, "ï"}, {{'"','o'}, "ö"}, {{'"','u'}, "ü"},
    {{'"','A'}, "Ä"}, {{'"','E'}, "Ë"}, {{'"','I'}, "Ï"}, {{'"','O'}, "Ö"}, {{'"','U'}, "Ü"},
    {{'"','y'}, "ÿ"}, {{'"','Y'}, "Ÿ"},

    // Tilde (~)
    {{'~','a'}, "ã"}, {{'~','o'}, "õ"}, {{'~','n'}, "ñ"},
    {{'~','A'}, "Ã"}, {{'~','O'}, "Õ"}, {{'~','N'}, "Ñ"},
    {{'~','i'}, "ĩ"}, {{'~','I'}, "Ĩ"}, {{'~','u'}, "ũ"}, {{'~','U'}, "Ũ"},

    // Macron (=)
    {{'=','a'}, "ā"}, {{'=','e'}, "ē"}, {{'=','i'}, "ī"}, {{'=','o'}, "ō"}, {{'=','u'}, "ū"},
    {{'=','A'}, "Ā"}, {{'=','E'}, "Ē"}, {{'=','I'}, "Ī"}, {{'=','O'}, "Ō"}, {{'=','U'}, "Ū"},

    // Dot above (.)
    {{'.','c'}, "ċ"}, {{'.','C'}, "Ċ"}, {{'.','e'}, "ė"}, {{'.','E'}, "Ė"},
    {{'.','g'}, "ġ"}, {{'.','G'}, "Ġ"}, {{'.','z'}, "ż"}, {{'.','Z'}, "Ż"},
    {{'.','I'}, "İ"},

    // Breve (u)
    {{'u','a'}, "ă"}, {{'u','A'}, "Ă"}, {{'u','e'}, "ĕ"}, {{'u','E'}, "Ĕ"},
    {{'u','g'}, "ğ"}, {{'u','G'}, "Ğ"}, {{'u','i'}, "ĭ"}, {{'u','I'}, "Ĭ"},
    {{'u','o'}, "ŏ"}, {{'u','O'}, "Ŏ"}, {{'u','u'}, "ŭ"}, {{'u','U'}, "Ŭ"},

    // Caron/háček (v)
    {{'v','c'}, "č"}, {{'v','C'}, "Č"}, {{'v','d'}, "ď"}, {{'v','D'}, "Ď"},
    {{'v','e'}, "ě"}, {{'v','E'}, "Ě"}, {{'v','n'}, "ň"}, {{'v','N'}, "Ň"},
    {{'v','r'}, "ř"}, {{'v','R'}, "Ř"}, {{'v','s'}, "š"}, {{'v','S'}, "Š"},
    {{'v','t'}, "ť"}, {{'v','T'}, "Ť"}, {{'v','z'}, "ž"}, {{'v','Z'}, "Ž"},

    // Cedilla (c)
    {{'c','c'}, "ç"}, {{'c','C'}, "Ç"}, {{'c','s'}, "ş"}, {{'c','S'}, "Ş"},
    {{'c','t'}, "ţ"}, {{'c','T'}, "Ţ"},

    // Ring above (r)
    {{'r','a'}, "å"}, {{'r','A'}, "Å"}, {{'r','u'}, "ů"}, {{'r','U'}, "Ů"},

    // Ogonek (k)
    {{'k','a'}, "ą"}, {{'k','A'}, "Ą"}, {{'k','e'}, "ę"}, {{'k','E'}, "Ę"},
    {{'k','i'}, "į"}, {{'k','I'}, "Į"}, {{'k','o'}, "ǫ"}, {{'k','O'}, "Ǫ"},
    {{'k','u'}, "ų"}, {{'k','U'}, "Ų"},
};

// Check if a command name is a diacritic command
static bool isDiacriticCommand(const char* cmd_name) {
    if (!cmd_name || strlen(cmd_name) != 1) return false;
    char c = cmd_name[0];
    return diacritic_combining_map.find(c) != diacritic_combining_map.end();
}

// Apply diacritic to a single UTF-8 character, returning the result
// Always uses combining characters (NFD form) to match latex.js output
static std::string applyDiacritic(char diacritic_cmd, const char* base_char) {
    if (!base_char || base_char[0] == '\0') {
        return "";
    }

    // Decode UTF-8 to get the character length
    const unsigned char* p = (const unsigned char*)base_char;
    int char_len = 1;

    if ((p[0] & 0x80) == 0) {
        // ASCII
        char_len = 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        // 2-byte UTF-8
        char_len = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        // 3-byte UTF-8
        char_len = 3;
    } else if ((p[0] & 0xF8) == 0xF0) {
        // 4-byte UTF-8
        char_len = 4;
    }

    // Look up the combining character for this diacritic
    auto comb_it = diacritic_combining_map.find(diacritic_cmd);
    if (comb_it != diacritic_combining_map.end()) {
        std::string result(base_char, char_len);  // Base character
        uint32_t combining = comb_it->second;

        // Encode combining character to UTF-8
        if (combining < 0x80) {
            result += (char)combining;
        } else if (combining < 0x800) {
            result += (char)(0xC0 | (combining >> 6));
            result += (char)(0x80 | (combining & 0x3F));
        } else if (combining < 0x10000) {
            result += (char)(0xE0 | (combining >> 12));
            result += (char)(0x80 | ((combining >> 6) & 0x3F));
            result += (char)(0x80 | (combining & 0x3F));
        }
        return result;
    }

    // Fallback: just return the base character
    return std::string(base_char, char_len);
}

// Get UTF-8 character length from first byte
static int getUtf8CharLen(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;
    if ((first_byte & 0xE0) == 0xC0) return 2;
    if ((first_byte & 0xF0) == 0xE0) return 3;
    if ((first_byte & 0xF8) == 0xF0) return 4;
    return 1;  // Invalid, treat as single byte
}

// Helper function to convert hex character to value
static int hex_to_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;  // Not a hex digit
}

// Helper function to encode Unicode codepoint to UTF-8
static std::string utf8_encode(uint32_t codepoint) {
    std::string result;
    if (codepoint <= 0x7F) {
        // 1-byte sequence
        result += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        // 2-byte sequence
        result += static_cast<char>(0xC0 | (codepoint >> 6));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        // 3-byte sequence
        result += static_cast<char>(0xE0 | (codepoint >> 12));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        // 4-byte sequence
        result += static_cast<char>(0xF0 | (codepoint >> 18));
        result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return result;
}

// Process LaTeX ^^ notation for special characters
// ^^HH     = hex HH (2 digits)
// ^^^^HHHH = hex HHHH (4 digits)
// ^^c      = if charcode(c) < 64 then charcode(c)+64 else charcode(c)-64
static std::string processHatNotation(const char* text) {
    std::string result;
    result.reserve(strlen(text));

    for (const char* p = text; *p; p++) {
        if (*p == '^' && *(p+1) == '^') {
            // Found ^^
            p += 2;  // Skip ^^

            // Check for ^^^^ (4 hats total)
            if (*p == '^' && *(p+1) == '^') {
                p += 2;  // Skip the second ^^

                // Parse 4 hex digits
                int h1 = hex_to_value(*p);
                int h2 = hex_to_value(*(p+1));
                int h3 = hex_to_value(*(p+2));
                int h4 = hex_to_value(*(p+3));

                if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                    uint32_t codepoint = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                    result += utf8_encode(codepoint);
                    p += 3;  // Skip 4 hex digits (p will be incremented by loop)
                    continue;
                } else {
                    // Invalid hex sequence, output as-is
                    result += "^^^^";
                    p--;  // Back up one char (loop will increment)
                    continue;
                }
            }

            // Check for ^^HH (2 hex digits)
            int h1 = hex_to_value(*p);
            int h2 = hex_to_value(*(p+1));

            if (h1 >= 0 && h2 >= 0) {
                // Valid 2-digit hex
                uint32_t codepoint = (h1 << 4) | h2;
                result += utf8_encode(codepoint);
                p += 1;  // Skip 2 hex digits (p will be incremented by loop)
                continue;
            }

            // ^^c (single character transform)
            unsigned char c = static_cast<unsigned char>(*p);
            uint32_t transformed;
            if (c < 64) {
                transformed = c + 64;
            } else {
                transformed = c - 64;
            }
            result += utf8_encode(transformed);
            // p will be incremented by loop to move past c
        } else {
            result += *p;
        }
    }

    return result;
}

// Convert ASCII apostrophe (') to right single quotation mark (')
// Also handles dash ligatures: -- → en-dash, --- → em-dash
// And single hyphen → Unicode hyphen (U+2010) when not in monospace mode
// If in_monospace is true, skip all dash/ligature conversions (keep literal characters)
// Returns a new string with conversions applied
static std::string convertApostrophes(const char* text, bool in_monospace = false) {
    std::string result;
    result.reserve(strlen(text) * 3);  // Reserve space for potential UTF-8 expansion
    for (const char* p = text; *p; p++) {
        if (*p == '\'') {
            // Check for '' (two apostrophes) → " (closing double quote)
            if (*(p+1) == '\'') {
                result += "\xE2\x80\x9D";  // " (U+201D = E2 80 9D)
                p++;  // Skip second apostrophe
            } else {
                // Single apostrophe → ' (U+2019 = E2 80 99 in UTF-8)
                result += "\xE2\x80\x99";
            }
        } else if (*p == '`') {
            // Check for `` (two backticks) → " (opening double quote)
            if (*(p+1) == '`') {
                result += "\xE2\x80\x9C";  // " (U+201C = E2 80 9C)
                p++;  // Skip second backtick
            } else {
                // Single backtick → ' (U+2018 = E2 80 98 in UTF-8)
                result += "\xE2\x80\x98";
            }
        } else if (*p == '-') {
            if (in_monospace) {
                // In monospace mode, keep all dashes as literal ASCII
                result += '-';
            } else {
                // Check for --- (em-dash) or -- (en-dash)
                if (*(p+1) == '-' && *(p+2) == '-') {
                    result += "\xE2\x80\x94";  // — (U+2014 = em-dash)
                    p += 2;  // Skip two more hyphens
                } else if (*(p+1) == '-') {
                    result += "\xE2\x80\x93";  // – (U+2013 = en-dash)
                    p++;  // Skip second hyphen
                } else {
                    // Single hyphen → Unicode hyphen (U+2010)
                    // LaTeX.js converts single hyphens to typographic hyphens
                    result += "\xE2\x80\x90";  // ‐ (U+2010 = hyphen)
                }
            }
        } else if (*p == '!' && (unsigned char)*(p+1) == 0xC2 && (unsigned char)*(p+2) == 0xB4) {
            // !´ (exclamation + acute accent U+00B4) → ¡ (inverted exclamation U+00A1)
            result += "\xC2\xA1";  // ¡ (U+00A1 = C2 A1)
            p += 2;  // Skip the ´ (2 bytes)
        } else if (*p == '?' && (unsigned char)*(p+1) == 0xC2 && (unsigned char)*(p+2) == 0xB4) {
            // ?´ (question + acute accent U+00B4) → ¿ (inverted question U+00BF)
            result += "\xC2\xBF";  // ¿ (U+00BF = C2 BF)
            p += 2;  // Skip the ´ (2 bytes)
        } else {
            result += *p;
        }
    }
    return result;
}

// Maximum macro expansion depth to prevent infinite recursion
// Real LaTeX documents rarely nest beyond 10 levels, but 100 allows complex templates
const int MAX_MACRO_DEPTH = 100;

// Forward declarations for command processors
class LatexProcessor;

// Command processor function type
typedef void (*CommandFunc)(LatexProcessor* proc, Item elem);

// Forward declarations for helper functions
static Element* cloneElement(Element* src, Input* input, Pool* pool);
static std::vector<Item> substituteParamsInString(const char* text, size_t len,
                                                   const std::vector<Element*>& args,
                                                   Pool* pool);
static void substituteParamsRecursive(Element* elem, const std::vector<Element*>& args, Pool* pool, int depth);

// =============================================================================
// LatexProcessor - Processes LaTeX Element tree and generates HTML
// =============================================================================

class LatexProcessor {
public:
    // Macro definition structure (defined here so it can be used in public methods)
    struct MacroDefinition {
        std::string name;
        int num_params;
        Element* definition;
        Element* default_value;  // optional default value for first parameter (LaTeX [default] syntax)
    };

public:
    LatexProcessor(HtmlGenerator* gen, Pool* pool, Input* input)
        : gen_(gen), pool_(pool), input_(input), in_paragraph_(false), inline_depth_(0),
          next_paragraph_is_continue_(false), next_paragraph_is_noindent_(false),
          next_paragraph_alignment_(nullptr),
          strip_next_leading_space_(false), styled_span_depth_(0), italic_styled_span_depth_(0),
          recursion_depth_(0), depth_exceeded_(false), restricted_h_mode_(false),
          restricted_h_mode_first_text_(false), next_box_frame_(false),
          pending_zws_output_(false), pending_zws_had_trailing_space_(false),
          group_suppresses_zws_(false), monospace_depth_(0), margin_par_counter_(0),
          stored_title_({0}), stored_author_({0}), stored_date_({0}),
          has_title_(false), has_author_(false), has_date_(false) {}

    // Process a LaTeX element tree
    void process(Item root);

    // Process a single node (element, string, or symbol)
    void processNode(Item node);

    // Process element children
    void processChildren(Item elem);

    // Process spacing command
    void processSpacingCommand(Item elem);

    // Process text content
    void processText(const char* text);
    void outputTextWithSpecialChars(const char* text);

    // Get generator
    HtmlGenerator* generator() { return gen_; }

    // Get pool
    Pool* pool() { return pool_; }

    // Get input
    Input* input() { return input_; }

    // Font declaration tracking - call after a font declaration command
    // to indicate that the next text should strip its leading space
    void setStripNextLeadingSpace(bool strip) { strip_next_leading_space_ = strip; }

    // Styled span depth management - used to prevent double-wrapping in text-styling commands
    void enterStyledSpan() { styled_span_depth_++; }
    void exitStyledSpan() { if (styled_span_depth_ > 0) styled_span_depth_--; }
    bool inStyledSpan() const { return styled_span_depth_ > 0; }

    // Italic span tracking - to know if we're inside an italic styled span
    void enterItalicStyledSpan() { italic_styled_span_depth_++; }
    void exitItalicStyledSpan() { if (italic_styled_span_depth_ > 0) italic_styled_span_depth_--; }
    bool inItalicStyledSpan() const { return italic_styled_span_depth_ > 0; }

    // Monospace mode tracking - inside \texttt or similar, suppress dash ligatures
    void enterMonospaceMode() { monospace_depth_++; }
    void exitMonospaceMode() { if (monospace_depth_ > 0) monospace_depth_--; }
    bool inMonospaceMode() const { return monospace_depth_ > 0; }

    // Font environment class tracking - for \begin{small}, \begin{bfseries}, etc.
    // Only the innermost environment class is used for text wrapping
    void pushFontEnvClass(const std::string& font_class) { font_env_class_stack_.push_back(font_class); }
    void popFontEnvClass() { if (!font_env_class_stack_.empty()) font_env_class_stack_.pop_back(); }
    bool inFontEnv() const { return !font_env_class_stack_.empty(); }
    const std::string& currentFontEnvClass() const {
        static const std::string empty;
        return font_env_class_stack_.empty() ? empty : font_env_class_stack_.back();
    }

    // Restricted horizontal mode (inside \mbox, \fbox, etc.)
    // In this mode: \\ and \newline are ignored, \par becomes a space
    void enterRestrictedHMode() {
        restricted_h_mode_ = true;
        restricted_h_mode_first_text_ = true;  // Track first text for ZWS handling
    }
    void exitRestrictedHMode() {
        restricted_h_mode_ = false;
        restricted_h_mode_first_text_ = false;
    }
    bool inRestrictedHMode() const { return restricted_h_mode_; }

    // Frame class flag - when true, next box command should add "frame" class
    void set_next_box_frame(bool frame) { next_box_frame_ = frame; }
    bool get_next_box_frame() const { return next_box_frame_; }

    // Group ZWS suppression - when true, the containing group won't output ZWS
    void setSuppressGroupZWS(bool suppress) { group_suppresses_zws_ = suppress; }
    bool getSuppressGroupZWS() const { return group_suppresses_zws_; }

    // Pending ZWS output - set by space-absorbing commands like \LaTeX
    void setPendingZWSOutput(bool pending) { pending_zws_output_ = pending; }

    // Paragraph management - public so command handlers can access
    void endParagraph();  // Close current paragraph if open
    void closeParagraphIfOpen();  // Alias for endParagraph
    void setNextParagraphIsContinue() { next_paragraph_is_continue_ = true; }
    void setNextParagraphIsNoindent() { next_paragraph_is_noindent_ = true; }
    void setNextParagraphAlignment(const char* alignment) { next_paragraph_alignment_ = alignment; }
    const char* getCurrentAlignment() const { return next_paragraph_alignment_; }
    void pushAlignmentScope() { alignment_stack_.push_back(next_paragraph_alignment_); }
    void popAlignmentScope() {
        if (!alignment_stack_.empty()) {
            next_paragraph_alignment_ = alignment_stack_.back();
            alignment_stack_.pop_back();
        }
    }
    void ensureParagraph();  // Start a paragraph if not already in one
    bool inParagraph() const { return in_paragraph_; }  // Check if currently in a paragraph
    void setInParagraph(bool value) { in_paragraph_ = value; }  // Set paragraph state (for list items)

    // Inline mode management (to suppress paragraph creation)
    void enterInlineMode() { inline_depth_++; }
    void exitInlineMode() { inline_depth_--; }

    // Macro system functions (public so command handlers can access)
    void registerMacro(const std::string& name, int num_params, Element* definition, Element* default_value = nullptr);
    bool isMacro(const std::string& name);
    MacroDefinition* getMacro(const std::string& name);
    Element* expandMacro(const std::string& name, const std::vector<Element*>& args);

    // Document metadata storage for \title, \author, \date, \maketitle
    void storeTitle(Item elem) { stored_title_ = elem; has_title_ = true; }
    void storeAuthor(Item elem) { stored_author_ = elem; has_author_ = true; }
    void storeDate(Item elem) { stored_date_ = elem; has_date_ = true; }
    bool hasTitle() const { return has_title_; }
    bool hasAuthor() const { return has_author_; }
    bool hasDate() const { return has_date_; }
    Item getStoredTitle() const { return stored_title_; }
    Item getStoredAuthor() const { return stored_author_; }
    Item getStoredDate() const { return stored_date_; }

    // Margin paragraph functions
    int addMarginParagraph(const std::string& content);
    bool hasMarginParagraphs() const;
    void writeMarginParagraphs(HtmlWriter* writer);

    // Helper to get next sibling (skipping whitespace) for command argument collection
    // Returns true if a sibling was found, sets out_item and out_type
    bool getNextSiblingArg(int64_t offset, Item* out_item, const char** out_type);

    // Helper to consume sibling arguments for echo commands
    // Returns number of siblings consumed
    int consumeSiblingArgs(std::vector<Item>& brack_args, std::vector<Item>& curly_args);

    // Helper to output the content of a group with parbreak -> <br> conversion
    void outputGroupContent(Item group_item);

    // Sibling context accessors for \begin{...} handling
    ElementReader* getSiblingParent() { return sibling_ctx_.parent_reader; }
    int64_t getSiblingCurrentIndex() { return sibling_ctx_.current_index; }
    void setSiblingConsumed(int64_t count) {
        if (sibling_ctx_.consumed_count) *sibling_ctx_.consumed_count = count;
    }

private:
    HtmlGenerator* gen_;
    Pool* pool_;
    Input* input_;

    // Command dispatch table (will be populated)
    std::map<std::string, CommandFunc> command_table_;

    // Macro storage
    std::map<std::string, MacroDefinition> macro_table_;

    // Paragraph tracking for auto-wrapping text
    bool in_paragraph_;
    int inline_depth_;  // Track nesting depth of inline elements

    // When true, the next paragraph should have class="continue"
    // Set when a block environment ends (itemize, enumerate, center, etc.)
    bool next_paragraph_is_continue_;

    // When true, the next paragraph should have class="noindent"
    // Set by \noindent command
    bool next_paragraph_is_noindent_;

    // Alignment for next paragraph (centering, raggedright, raggedleft)
    // Set by alignment declaration commands (\centering, \raggedright, \raggedleft)
    const char* next_paragraph_alignment_;

    // Stack for tracking alignment in nested groups (for proper scope restoration)
    std::vector<const char*> alignment_stack_;

    // Font declaration tracking - when true, the next text should strip leading space
    // This is set by font declaration commands like \bfseries, \em, etc.
    bool strip_next_leading_space_;

    // Styled span depth - when > 0, we're inside a text-styling command like \textbf{}
    // processText should not add font spans when inside a styled span
    int styled_span_depth_;

    // Italic styled span depth - when > 0, we're inside an italic styled span (\textit{}, \emph{})
    // Used by \emph to decide whether to add outer <span class="it">
    int italic_styled_span_depth_;

    // Recursion depth tracking for macro expansion (prevent infinite loops)
    int recursion_depth_;
    bool depth_exceeded_;  // Flag to halt processing when depth limit is exceeded

    // Restricted horizontal mode flag - set when inside \mbox, \fbox, etc.

    // Frame class flag - when true, next box command should add "frame" class
    // Set by fbox when it contains a single parbox/minipage/makebox
    bool next_box_frame_;
    // In this mode, linebreaks (\\, \newline) are ignored and \par becomes a space
    bool restricted_h_mode_;
    // Flag to track if we should add ZWS before first newline-sourced whitespace
    // Set when entering restricted h-mode, cleared after first text is processed
    bool restricted_h_mode_first_text_;

    // Pending ZWS output - when true, a ZWS should be output if there's more content
    // Set by curly_group handler when a group at document level closes
    bool pending_zws_output_;
    // If the curly group that set pending_zws_output_ had trailing whitespace
    bool pending_zws_had_trailing_space_;
    // If set, the current group contains only whitespace-controlling commands
    // and should not trigger ZWS output
    bool group_suppresses_zws_;

    // Monospace mode tracking - when > 0, we're inside \texttt or similar
    // In monospace mode, dash ligatures (-- → en-dash, --- → em-dash) are suppressed
    // and single hyphens are not converted to Unicode hyphen
    int monospace_depth_;

    // Font environment class stack - tracks the class to use for current font environment
    // When inside \begin{small}...\end{small}, this is "small"
    // When inside \begin{bfseries}...\end{bfseries}, this is "bf"
    // The innermost (top) class is used for text wrapping, not the accumulated font state
    std::vector<std::string> font_env_class_stack_;

    // Margin paragraph tracking
    struct MarginParagraph {
        int id;              // Unique ID (1-based)
        std::string content; // Rendered HTML content of the marginpar
    };
    std::vector<MarginParagraph> margin_paragraphs_;
    int margin_par_counter_;  // Counter for generating unique IDs

    // Document metadata storage for \maketitle
    // Stores element references so content can be rendered later
    Item stored_title_;       // Content from \title{}
    Item stored_author_;      // Content from \author{}
    Item stored_date_;        // Content from \date{}
    bool has_title_;
    bool has_author_;
    bool has_date_;

    // Helper methods for paragraph management
    bool isBlockCommand(const char* cmd_name);
    bool isInlineCommand(const char* cmd_name);

    // Process specific command
    void processCommand(const char* cmd_name, Item elem);

    // Sibling context for lookahead in command handlers
    // Set by processChildren before calling processNode/processCommand
    struct SiblingContext {
        ElementReader* parent_reader;  // Parent element containing siblings
        int64_t current_index;         // Current child index being processed
        int64_t* consumed_count;       // Output: how many extra siblings were consumed
    };
    SiblingContext sibling_ctx_;

    // Initialize command table
    void initCommandTable();

    // Recursion depth guard (RAII pattern)
    class DepthGuard {
    public:
        DepthGuard(LatexProcessor* proc) : proc_(proc) {
            proc_->recursion_depth_++;
        }
        ~DepthGuard() {
            proc_->recursion_depth_--;
        }
        bool exceeded() const {
            return proc_->recursion_depth_ > MAX_MACRO_DEPTH;
        }
    private:
        LatexProcessor* proc_;
    };
    friend class DepthGuard;
};

// =============================================================================
// Macro System - Member Function Implementations
// =============================================================================

void LatexProcessor::registerMacro(const std::string& name, int num_params, Element* definition, Element* default_value) {

    MacroDefinition macro;
    macro.name = name;
    macro.num_params = num_params;
    macro.definition = definition;
    macro.default_value = default_value;
    macro_table_[name] = macro;
}

bool LatexProcessor::isMacro(const std::string& name) {
    return macro_table_.find(name) != macro_table_.end();
}

LatexProcessor::MacroDefinition* LatexProcessor::getMacro(const std::string& name) {
    auto it = macro_table_.find(name);
    if (it != macro_table_.end()) {
        return &it->second;
    }
    return nullptr;
}

Element* LatexProcessor::expandMacro(const std::string& name, const std::vector<Element*>& args) {
    // Check if depth was already exceeded
    if (depth_exceeded_) {
        return nullptr;
    }

    DepthGuard guard(this);
    if (guard.exceeded()) {
        log_error("Macro expansion depth exceeded maximum %d for macro '%s'", MAX_MACRO_DEPTH, name.c_str());
        depth_exceeded_ = true;  // Set flag to halt all further processing
        return nullptr;
    }

    MacroDefinition* macro = getMacro(name);
    if (!macro || !macro->definition) {
        log_debug("expandMacro: macro '%s' not found or no definition", name.c_str());
        return nullptr;
    }

    log_debug("expandMacro: '%s' with %zu args, num_params=%d, depth=%d", name.c_str(), args.size(), macro->num_params, recursion_depth_);

    // Clone the definition using MarkBuilder to preserve TypeElmt metadata
    Element* expanded = cloneElement(macro->definition, input_, pool_);

    // Substitute parameters with actual arguments if needed
    if (expanded && args.size() > 0 && macro->num_params > 0) {
        log_debug("expandMacro: substituting parameters in macro '%s'", name.c_str());
        substituteParamsRecursive(expanded, args, pool_, 0);
    }

    return expanded;
}

// =============================================================================
// Margin Paragraph - Member Function Implementations
// =============================================================================

int LatexProcessor::addMarginParagraph(const std::string& content) {
    margin_par_counter_++;
    MarginParagraph mp;
    mp.id = margin_par_counter_;
    mp.content = content;
    margin_paragraphs_.push_back(mp);
    return margin_par_counter_;
}

bool LatexProcessor::hasMarginParagraphs() const {
    return !margin_paragraphs_.empty();
}

void LatexProcessor::writeMarginParagraphs(HtmlWriter* writer) {
    if (margin_paragraphs_.empty()) return;

    // Output: <div class="margin-right"><div class="marginpar">...content...</div></div>
    writer->openTag("div", "margin-right");
    writer->openTag("div", "marginpar");

    for (const auto& mp : margin_paragraphs_) {
        // Each marginpar gets: <div id="N"><span class="mpbaseline"></span>content</div>
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "%d", mp.id);
        writer->writeRawHtml("<div id=\"");
        writer->writeRawHtml(id_str);
        writer->writeRawHtml("\">");
        writer->writeRawHtml("<span class=\"mpbaseline\"></span>");
        writer->writeRawHtml(mp.content.c_str());
        writer->writeRawHtml("</div>");
    }

    writer->closeTag("div");  // marginpar
    writer->closeTag("div");  // margin-right
}

// =============================================================================
// Macro System - Helper Functions
// =============================================================================

// Clone an Element tree (deep copy for macro expansion)
// Uses MarkBuilder to properly reconstruct Elements with TypeElmt metadata
static Element* cloneElement(Element* src, Input* input, Pool* pool) {
    if (!src) return nullptr;

    Item src_item;
    src_item.element = src;
    ElementReader reader(src_item);
    const char* tag = reader.tagName();
    if (!tag) {
        log_error("cloneElement: source element has no tag name");
        return nullptr;
    }

    // Create builder using input's arena
    MarkBuilder builder(input);
    auto elem_builder = builder.element(tag);

    // Clone all child items
    for (int64_t i = 0; i < reader.childCount(); i++) {
        ItemReader child_reader = reader.childAt(i);
        Item child = child_reader.item();
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_ELEMENT) {
            // Recursively clone child elements
            Element* child_clone = cloneElement(child.element, input, pool);
            if (child_clone) {
                Item child_item;
                child_item.element = child_clone;
                elem_builder.child(child_item);
            }
        } else if (type == LMD_TYPE_STRING) {
            // Copy string
            String* str = (String*)child.string_ptr;
            String* str_copy = builder.createString(str->chars, str->len);
            Item str_item;
            str_item.string_ptr = (uint64_t)str_copy;
            str_item._8_s = LMD_TYPE_STRING;
            elem_builder.child(str_item);
        } else {
            // Copy other types as-is (symbols, numbers, etc.)
            elem_builder.child(child);
        }
    }

    Item clone_item = elem_builder.final();
    return clone_item.element;
}

// Substitute #1, #2, etc. in a string with actual argument values
static std::vector<Item> substituteParamsInString(const char* text, size_t len,
                                                   const std::vector<Element*>& args,
                                                   Pool* pool) {
    std::vector<Item> result;
    size_t i = 0;
    size_t segment_start = 0;

    while (i < len) {
        if (text[i] == '#' && i + 1 < len && text[i + 1] >= '1' && text[i + 1] <= '9') {
            // Found parameter reference
            int param_num = text[i + 1] - '0';

            // Add text segment before the parameter
            if (i > segment_start) {
                size_t seg_len = i - segment_start;
                String* seg_str = (String*)pool_calloc(pool, sizeof(String) + seg_len + 1);
                seg_str->len = seg_len;
                memcpy(seg_str->chars, text + segment_start, seg_len);
                seg_str->chars[seg_len] = '\0';

                Item str_item;
                str_item.string_ptr = (uint64_t)seg_str;
                str_item._8_s = LMD_TYPE_STRING;
                result.push_back(str_item);
            }

            // Add the argument element (if it exists)
            if (param_num > 0 && param_num <= (int)args.size() && args[param_num - 1]) {
                Item arg_item;
                arg_item.item = (uint64_t)args[param_num - 1];
                result.push_back(arg_item);
            }

            i += 2;  // Skip #N
            segment_start = i;
        } else {
            i++;
        }
    }

    // Add remaining text
    if (segment_start < len) {
        size_t seg_len = len - segment_start;
        String* seg_str = (String*)pool_calloc(pool, sizeof(String) + seg_len + 1);
        seg_str->len = seg_len;
        memcpy(seg_str->chars, text + segment_start, seg_len);
        seg_str->chars[seg_len] = '\0';

        Item str_item;
        str_item.string_ptr = (uint64_t)seg_str;
        str_item._8_s = LMD_TYPE_STRING;
        result.push_back(str_item);
    }

    return result;
}

// Recursively substitute parameters in an Element tree
static void substituteParamsRecursive(Element* elem, const std::vector<Element*>& args, Pool* pool, int depth) {
    // Check depth limit to prevent infinite recursion in substitution
    if (depth > MAX_MACRO_DEPTH) {
        log_error("Parameter substitution depth exceeded maximum %d", MAX_MACRO_DEPTH);
        return;
    }

    if (!elem) return;

    List* elem_list = (List*)elem;
    if (!elem_list->items) return;

    std::vector<Item> new_items;

    for (int64_t i = 0; i < elem_list->length; i++) {
        Item item = elem_list->items[i];
        TypeId type = get_type_id(item);

        if (type == LMD_TYPE_STRING) {
            String* str = (String*)item.string_ptr;

            // Check if string contains parameter references
            bool has_param = false;
            for (size_t j = 0; j < str->len; j++) {
                if (str->chars[j] == '#' && j + 1 < str->len &&
                    str->chars[j + 1] >= '1' && str->chars[j + 1] <= '9') {
                    has_param = true;
                    break;
                }
            }

            if (has_param) {
                // Substitute parameters in this string
                std::vector<Item> substituted = substituteParamsInString(str->chars, str->len, args, pool);
                new_items.insert(new_items.end(), substituted.begin(), substituted.end());
            } else {
                new_items.push_back(item);
            }
        } else if (type == LMD_TYPE_SYMBOL) {
            // Check if symbol is a parameter reference like "#1"
            String* sym = (String*)item.string_ptr;

            if (sym->len >= 2 && sym->chars[0] == '#' &&
                sym->chars[1] >= '1' && sym->chars[1] <= '9') {
                // This is a parameter reference
                int param_num = sym->chars[1] - '0';

                if (param_num > 0 && param_num <= (int)args.size() && args[param_num - 1]) {
                    // Substitute with the argument element
                    Item arg_item;
                    arg_item.item = (uint64_t)args[param_num - 1];
                    new_items.push_back(arg_item);
                } else {
                    log_warn("Parameter #%d out of range (have %zu args)", param_num, args.size());
                    new_items.push_back(item);
                }
            } else {
                new_items.push_back(item);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            // Recursively process child elements
            substituteParamsRecursive(item.element, args, pool, depth + 1);
            new_items.push_back(item);
        } else if (type == LMD_TYPE_LIST) {
            // Recursively process list items
            substituteParamsRecursive((Element*)item.list, args, pool, depth + 1);
            new_items.push_back(item);
        } else {
            new_items.push_back(item);
        }
    }

    // Replace element's items with substituted version (always update, even if size is same)
    elem_list->items = (Item*)pool_calloc(pool, sizeof(Item) * new_items.size());
    elem_list->length = new_items.size();
    elem_list->capacity = new_items.size();
    for (size_t i = 0; i < new_items.size(); i++) {
        elem_list->items[i] = new_items[i];
    }
}

// =============================================================================
// Command Implementations
// =============================================================================

// Helper to check if a command element has an empty curly_group child (terminator like \ss{})
// If so, the {} consumes the command, so we shouldn't strip trailing space
static bool hasEmptyCurlyGroupChild(Item elem) {
    ElementReader reader(elem);
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* child_tag = child_elem.tagName();
            if (child_tag && strcmp(child_tag, "curly_group") == 0) {
                // Check if empty (no children or only whitespace)
                // A curly_group with 0 children is definitely empty
                if (child_elem.childCount() == 0) {
                    return true;
                }
                // Has children but check if they're all empty/whitespace
                // For simplicity, if it has any children, treat as non-empty
                // The terminator {} should have exactly 0 children
                return false;
            }
        }
    }
    return false;
}

// =============================================================================
// Diacritic Commands - Handle accent marks like \^{o}, \'{e}, etc.
// =============================================================================

// Generic diacritic handler - extracts base character from children and applies diacritic
static void processDiacritic(LatexProcessor* proc, Item elem, char diacritic_cmd) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    // Look for the base character in children
    std::string base_char;
    bool found_base = false;

    // Check all children
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* child_tag = child_elem.tagName();

            if (child_tag && strcmp(child_tag, "curly_group") == 0) {
                // Extract text from curly_group
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                if (str && str->len > 0) {
                    base_char = std::string(str->chars, str->len);
                    found_base = true;
                }
                break;
            }
        } else if (child.isString()) {
            // Direct string child (like \^o where 'o' is direct child)
            const char* text = child.asString()->chars;
            if (text && text[0] != '\0') {
                // Take only the first character (UTF-8 aware)
                int char_len = getUtf8CharLen((unsigned char)text[0]);
                base_char = std::string(text, char_len);
                found_base = true;

                // If there's more text after the first char, we need to output it too
                if (strlen(text) > (size_t)char_len) {
                    std::string result = applyDiacritic(diacritic_cmd, base_char.c_str());
                    proc->ensureParagraph();
                    gen->text(result.c_str());
                    gen->text(text + char_len);
                    return;
                }
            }
            break;
        }
    }

    if (found_base && !base_char.empty()) {
        std::string result = applyDiacritic(diacritic_cmd, base_char.c_str());
        proc->ensureParagraph();
        gen->text(result.c_str());
    } else {
        // No base character - output the diacritic mark itself
        proc->ensureParagraph();
        char diacritic_str[2] = {diacritic_cmd, '\0'};
        gen->text(diacritic_str);
        // Output ZWS if empty curly group (e.g., \^{} produces ^​)
        if (hasEmptyCurlyGroupChild(elem)) {
            gen->text("\xe2\x80\x8b");  // U+200B zero-width space
        }
    }
}

// Individual diacritic command handlers
static void cmd_acute(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '\''); }
static void cmd_grave(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '`'); }
static void cmd_circumflex(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '^'); }
static void cmd_tilde_accent(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '~'); }
static void cmd_diaeresis(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '"'); }
static void cmd_macron(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '='); }
static void cmd_dot_above(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, '.'); }
static void cmd_breve(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'u'); }
static void cmd_caron(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'v'); }
static void cmd_double_acute(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'H'); }
static void cmd_cedilla(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'c'); }
static void cmd_dot_below(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'd'); }
static void cmd_macron_below(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'b'); }
static void cmd_ring_above(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'r'); }
static void cmd_ogonek(LatexProcessor* proc, Item elem) { processDiacritic(proc, elem, 'k'); }

// =============================================================================
// Special Character Commands - Non-combining special letters
// =============================================================================

static void cmd_i(LatexProcessor* proc, Item elem) {
    // \i - Dotless i (ı) for use with diacritics
    proc->ensureParagraph();
    proc->generator()->text("ı");
    // Only strip trailing space if no terminator {} present
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_j(LatexProcessor* proc, Item elem) {
    // \j - Dotless j (ȷ) for use with diacritics
    proc->ensureParagraph();
    proc->generator()->text("ȷ");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_l(LatexProcessor* proc, Item elem) {
    // \l - Polish L-with-stroke (ł)
    proc->ensureParagraph();
    proc->generator()->text("ł");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_L(LatexProcessor* proc, Item elem) {
    // \L - Polish L-with-stroke uppercase (Ł)
    proc->ensureParagraph();
    proc->generator()->text("Ł");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_o_special(LatexProcessor* proc, Item elem) {
    // \o - Scandinavian slashed o (ø)
    proc->ensureParagraph();
    proc->generator()->text("ø");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_O_special(LatexProcessor* proc, Item elem) {
    // \O - Scandinavian slashed O uppercase (Ø)
    proc->ensureParagraph();
    proc->generator()->text("Ø");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_ss(LatexProcessor* proc, Item elem) {
    // \ss - German sharp s (ß) - eszett
    proc->ensureParagraph();
    proc->generator()->text("ß");
    if (hasEmptyCurlyGroupChild(elem)) {
        // Terminator {} produces ZWS for visual separation
        proc->generator()->text("\xe2\x80\x8b");  // U+200B
    } else {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_ae(LatexProcessor* proc, Item elem) {
    // \ae - Latin small letter ae (æ)
    proc->ensureParagraph();
    proc->generator()->text("æ");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_AE(LatexProcessor* proc, Item elem) {
    // \AE - Latin capital letter AE (Æ)
    proc->ensureParagraph();
    proc->generator()->text("Æ");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_oe(LatexProcessor* proc, Item elem) {
    // \oe - Latin small ligature oe (œ)
    proc->ensureParagraph();
    proc->generator()->text("œ");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_OE(LatexProcessor* proc, Item elem) {
    // \OE - Latin capital ligature OE (Œ)
    proc->ensureParagraph();
    proc->generator()->text("Œ");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

static void cmd_aa(LatexProcessor* proc, Item elem) {
    // \aa - Latin small letter a with ring above (å)
    proc->ensureParagraph();
    proc->generator()->text("å");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}
static void cmd_AA(LatexProcessor* proc, Item elem) {
    // \AA - Latin capital letter A with ring above (Å)
    proc->ensureParagraph();
    proc->generator()->text("Å");
    if (!hasEmptyCurlyGroupChild(elem)) {
        proc->setStripNextLeadingSpace(true);
    }
}

// =============================================================================
// Text formatting commands
// Note: These commands create spans directly and do NOT modify font state,
// so processText won't double-wrap the content in another span.
// Font state is only modified by declaration commands (\bfseries, \em, etc.)

static void cmd_textbf(LatexProcessor* proc, Item elem) {
    // \textbf{text} - bold text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("bf");  // Just the class name
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textit(LatexProcessor* proc, Item elem) {
    // \textit{text} - italic text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    proc->enterItalicStyledSpan();  // Mark we're inside an italic styled span
    gen->currentFont().shape = FontShape::Italic;  // Set italic state for nested \emph
    gen->span("it");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitItalicStyledSpan();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_emph(LatexProcessor* proc, Item elem) {
    // \emph{text} - emphasized text (toggles italic)
    // When already in italic and inside an italic styled span, just output <span class="up">
    // When already in italic but NOT inside an italic styled span (e.g., \em declaration),
    //   output <span class="it"><span class="up"> to show current state + toggle
    // When not italic, just output <span class="it">
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText

    // Toggle italic state
    bool was_italic = (gen->currentFont().shape == FontShape::Italic);
    bool in_italic_span = proc->inItalicStyledSpan();

    if (was_italic) {
        if (in_italic_span) {
            // Already inside an italic styled span (e.g., nested \emph or inside \textit)
            // Just output the upright span
            gen->currentFont().shape = FontShape::Upright;
            gen->span("up");
            proc->processChildren(elem);
            gen->closeElement();  // Close "up"
        } else {
            // Italic from declaration (e.g., \em) - need outer span to show current state
            gen->span("it");  // Outer span reflects current state
            gen->currentFont().shape = FontShape::Upright;
            gen->span("up");  // Inner span for toggled state
            proc->enterItalicStyledSpan();  // Mark that we're now inside an italic span
            proc->processChildren(elem);
            proc->exitItalicStyledSpan();
            gen->closeElement();  // Close "up"
            gen->closeElement();  // Close "it"
        }
    } else {
        // Not italic, just add italic span
        gen->currentFont().shape = FontShape::Italic;
        gen->span("it");
        proc->enterItalicStyledSpan();  // Mark that we're now inside an italic span
        proc->processChildren(elem);
        proc->exitItalicStyledSpan();
        gen->closeElement();  // Close "it"
    }

    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_texttt(LatexProcessor* proc, Item elem) {
    // \texttt{text} - typewriter/monospace text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    proc->enterMonospaceMode();  // Suppress dash ligatures and hyphen conversion
    gen->currentFont().family = FontFamily::Typewriter;
    gen->span("tt");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitMonospaceMode();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textsf(LatexProcessor* proc, Item elem) {
    // \textsf{text} - sans-serif text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().family = FontFamily::SansSerif;
    gen->span("textsf");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textrm(LatexProcessor* proc, Item elem) {
    // \textrm{text} - roman (serif) text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().family = FontFamily::Roman;
    gen->span("textrm");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textsc(LatexProcessor* proc, Item elem) {
    // \textsc{text} - small caps text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().shape = FontShape::SmallCaps;
    gen->span("textsc");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_underline(LatexProcessor* proc, Item elem) {
    // \underline{text} - underlined text
    HtmlGenerator* gen = proc->generator();

    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("underline");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
}

static void cmd_sout(LatexProcessor* proc, Item elem) {
    // \sout{text} - strikethrough text
    HtmlGenerator* gen = proc->generator();

    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("sout");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
}

// =============================================================================
// Additional Font Commands (textmd, textup, textsl, textnormal)
// =============================================================================

static void cmd_textmd(LatexProcessor* proc, Item elem) {
    // \textmd{text} - medium weight text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().series = FontSeries::Normal;
    gen->span("textmd");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textup(LatexProcessor* proc, Item elem) {
    // \textup{text} - upright shape
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().shape = FontShape::Upright;
    gen->span("up");  // latex-js uses 'up' not 'textup'
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textsl(LatexProcessor* proc, Item elem) {
    // \textsl{text} - slanted text
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->currentFont().shape = FontShape::Slanted;
    gen->span("textsl");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_textnormal(LatexProcessor* proc, Item elem) {
    // \textnormal{text} - normal font
    HtmlGenerator* gen = proc->generator();

    gen->enterGroup();
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    // Reset to defaults
    gen->currentFont().series = FontSeries::Normal;
    gen->currentFont().shape = FontShape::Upright;
    gen->currentFont().family = FontFamily::Roman;
    gen->currentFont().size = FontSize::NormalSize;
    gen->span("textnormal");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

// =============================================================================
// Font Declaration Commands (bfseries, mdseries, rmfamily, etc.)
// These can operate in two modes:
// 1. Declaration-style (no children): \bfseries sets font for subsequent text in current scope
// 2. Command-style (\small{text}): wraps content in a single span
// 3. Environment-style (\begin{small}...\end{small}):
//    - Outputs ZWS spans at boundaries for visual separation
//    - Text inside gets per-chunk wrapping via processText() based on font state
// Distinction: Environment-style has paragraph elements as children (from \begin{...})
// while command-style has direct text/inline content as children.
// =============================================================================

// Helper: Check if element has a paragraph child (indicates environment syntax)
static bool hasEnvironmentSyntax(Item elem) {
    ElementReader reader(elem);
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "paragraph") == 0) {
                return true;
            }
        }
    }
    return false;
}

// Helper: Output a ZWS marker span with specified font class
// Used at font environment boundaries to mark the transition
// Uses explicit class to avoid inheriting parent font state
static void outputFontBoundaryZWSWithClass(LatexProcessor* proc, const char* font_class) {
    HtmlGenerator* gen = proc->generator();
    if (font_class && font_class[0] != '\0') {
        gen->span(font_class);
        gen->text("\xe2\x80\x8b ");  // ZWS + space
        gen->closeElement();
    }
}

// Helper: Output a ZWS marker span with current font class (full state)
// Used for generic contexts, but NOT for font environments
static void outputFontBoundaryZWS(LatexProcessor* proc) {
    HtmlGenerator* gen = proc->generator();
    std::string font_class = gen->getFontClass(gen->currentFont());
    if (!font_class.empty()) {
        gen->span(font_class.c_str());
        gen->text("\xe2\x80\x8b ");  // ZWS + space
        gen->closeElement();
    }
}

static void cmd_bfseries(LatexProcessor* proc, Item elem) {
    // \bfseries - switch to bold
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        // Declaration-style: just set font state
        gen->currentFont().series = FontSeries::Bold;
        proc->setStripNextLeadingSpace(true);
    } else if (hasEnvironmentSyntax(elem)) {
        // Environment-style (\begin{bfseries}...): ZWS at boundaries, per-chunk wrapping
        // Use explicit "bf" class, not inherited font state
        gen->enterGroup();
        gen->currentFont().series = FontSeries::Bold;
        proc->pushFontEnvClass("bf");  // Push font env class for text wrapping
        outputFontBoundaryZWSWithClass(proc, "bf");  // ZWS span at start
        proc->processChildren(elem);
        outputFontBoundaryZWSWithClass(proc, "bf");  // ZWS span at end
        proc->popFontEnvClass();
        gen->exitGroup();
    } else {
        // Command-style (\bfseries{text}): single span wrapper
        gen->enterGroup();
        gen->currentFont().series = FontSeries::Bold;
        proc->enterStyledSpan();
        gen->span("bf");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_mdseries(LatexProcessor* proc, Item elem) {
    // \mdseries - switch to medium weight (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().series = FontSeries::Normal;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_rmfamily(LatexProcessor* proc, Item elem) {
    // \rmfamily - switch to roman family (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().family = FontFamily::Roman;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_sffamily(LatexProcessor* proc, Item elem) {
    // \sffamily - switch to sans-serif family (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().family = FontFamily::SansSerif;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_ttfamily(LatexProcessor* proc, Item elem) {
    // \ttfamily - switch to typewriter family (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().family = FontFamily::Typewriter;
    proc->enterMonospaceMode();  // Suppress dash ligatures and hyphen conversion
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
    proc->exitMonospaceMode();
}

static void cmd_itshape(LatexProcessor* proc, Item elem) {
    // \itshape - switch to italic shape (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().shape = FontShape::Italic;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_em(LatexProcessor* proc, Item elem) {
    // \em - toggle italic/upright shape (like \emph but as declaration)
    HtmlGenerator* gen = proc->generator();

    // Toggle italic state
    FontShape current = gen->currentFont().shape;
    if (current == FontShape::Italic) {
        // Toggle from italic to explicit upright (will produce <span class="up">)
        gen->currentFont().shape = FontShape::ExplicitUpright;
    } else if (current == FontShape::ExplicitUpright) {
        // Toggle from explicit upright back to italic
        gen->currentFont().shape = FontShape::Italic;
    } else {
        // Toggle from default upright to italic
        gen->currentFont().shape = FontShape::Italic;
    }

    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_slshape(LatexProcessor* proc, Item elem) {
    // \slshape - switch to slanted shape (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().shape = FontShape::Slanted;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_scshape(LatexProcessor* proc, Item elem) {
    // \scshape - switch to small caps shape (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().shape = FontShape::SmallCaps;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_upshape(LatexProcessor* proc, Item elem) {
    // \upshape - switch to upright shape (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().shape = FontShape::Upright;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

static void cmd_normalfont(LatexProcessor* proc, Item elem) {
    // \normalfont - reset to normal font (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().series = FontSeries::Normal;
    gen->currentFont().shape = FontShape::Upright;
    gen->currentFont().family = FontFamily::Roman;
    gen->currentFont().size = FontSize::NormalSize;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
}

// Macro definition commands

static void cmd_newcommand(LatexProcessor* proc, Item elem) {
    // \newcommand{\name}[num]{definition}
    // Defines a new macro (error if already exists)

    ElementReader reader(elem);

    // DEBUG: Check textContent of entire element
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    reader.textContent(sb);
    String* all_text = stringbuf_to_string(sb);




    std::string macro_name;
    int num_params = 0;
    Element* definition = nullptr;
    Element* default_value = nullptr;  // optional default value for first parameter
    bool have_num_params = false;  // track if we've seen [num] bracket group

    // Parse arguments: first is command name, second (optional) is num params, third is definition
    auto iter = reader.children();
    ItemReader child;
    int arg_index = 0;

    while (iter.next(&child)) {
        TypeId child_type = child.getType();


        if (child.isString()) {
            String* str = child.asString();

            // If we find a string starting with \, that might be the command name
            if (macro_name.empty() && str->chars[0] == '\\') {
                macro_name = str->chars;
            }
        } else if (child.isSymbol()) {
            String* sym = child.asSymbol();

            // Check if this symbol is the command name (not a marker)
            if (macro_name.empty() && sym->chars[0] == '\\') {
                macro_name = sym->chars;
            }
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();



            // Check for brack_group FIRST before other processing
            if (strcmp(tag, "brack_group") == 0 || strcmp(tag, "brack_group_argc") == 0) {
                fprintf(stderr, "DEBUG: Found bracket group '%s', have_num_params=%d\n", tag, have_num_params);

                // First brack_group is [num] - number of parameters
                // Second brack_group is [default] - default value for first parameter
                if (!have_num_params) {
                    // [num] parameter count - extract number from bracket group
                    Element* brack_elem = const_cast<Element*>(child_elem.element());
                    List* brack_list = (List*)brack_elem;

                    fprintf(stderr, "DEBUG: Bracket group has %lld items\n", brack_list->length);

                    // Look through items to find the number
                    fprintf(stderr, "DEBUG: Bracket group has extra=%lld items beyond length\n", brack_list->extra);

                    // Dump ALL items for debugging
                    for (int64_t j = 0; j < brack_list->length + brack_list->extra; j++) {
                        Item item = brack_list->items[j];
                        TypeId item_type = get_type_id(item);
                        fprintf(stderr, "DEBUG:   Item %lld: type=%d", j, item_type);
                        if (item_type == LMD_TYPE_STRING) {
                            String* str = (String*)item.string_ptr;
                            fprintf(stderr, " STRING='%s'", str->chars);
                        } else if (item_type == LMD_TYPE_INT) {
                            int val = (int)(item.item >> 32);
                            fprintf(stderr, " INT=%d", val);
                        } else if (item_type == LMD_TYPE_SYMBOL) {
                            String* sym = (String*)item.string_ptr;
                            fprintf(stderr, " SYMBOL='%s'", sym->chars);
                        }
                        fprintf(stderr, "\n");
                    }

                    for (int64_t j = 0; j < brack_list->length; j++) {
                        Item item = brack_list->items[j];
                        TypeId item_type = get_type_id(item);

                        fprintf(stderr, "DEBUG:   Processing item %lld: type=%d\n", j, item_type);

                        if (item_type == LMD_TYPE_STRING) {
                            String* str = (String*)item.string_ptr;
                            fprintf(stderr, "DEBUG:   Item %lld: STRING '%s'\n", j, str->chars);

                            // Try to parse as number
                            if (str->len > 0 && str->chars[0] >= '0' && str->chars[0] <= '9') {
                                num_params = atoi(str->chars);
                                fprintf(stderr, "DEBUG:   Parsed num_params=%d from string\n", num_params);
                                have_num_params = true;
                                break;
                            }
                        } else if (item_type == LMD_TYPE_INT) {
                            // Direct int value
                            int64_t val = item.item >> 32;
                            fprintf(stderr, "DEBUG:   Item %lld: INT %lld\n", j, val);
                            num_params = (int)val;
                            fprintf(stderr, "DEBUG:   Parsed num_params=%d from int\n", num_params);
                            have_num_params = true;
                            break;
                        } else if (item_type == LMD_TYPE_ELEMENT) {
                            Item elem_item;
                            elem_item.item = (uint64_t)item.element;
                            ElementReader elem_reader(elem_item);
                            const char* elem_tag = elem_reader.tagName();

                            if (strcmp(elem_tag, "argc") == 0) {
                                Pool* pool = proc->pool();
                                StringBuf* sb = stringbuf_new(pool);
                                elem_reader.textContent(sb);
                                String* argc_str = stringbuf_to_string(sb);

                                if (argc_str->len > 0) {
                                    num_params = atoi(argc_str->chars);
                                    fprintf(stderr, "DEBUG:   Parsed num_params=%d from argc textContent\n", num_params);
                                    have_num_params = true;
                                }
                                break;
                            }
                        }
                    }

                    // If we found brack_group but num_params is still 0, default to 1
                    if (num_params == 0) {
                        num_params = 1;
                        have_num_params = true;
                    }
                } else {
                    // Second brack_group: [default] - default value for first parameter
                    fprintf(stderr, "DEBUG: Found second brack_group - this is default value\n");
                    // Store the entire brack_group element as the default value
                    // (we'll wrap its content when needed)
                    MarkBuilder builder(proc->input());
                    auto default_elem = builder.element("arg");

                    // Copy children from brack_group to default_elem
                    auto brack_iter = child_elem.children();
                    ItemReader brack_child;
                    while (brack_iter.next(&brack_child)) {
                        default_elem.child(brack_child.item());
                    }

                    Item default_item = default_elem.final();
                    default_value = (Element*)default_item.item;
                    fprintf(stderr, "DEBUG: Stored default_value=%p\n", default_value);
                }


                continue;  // Don't process as regular arg
            }

            // If element tag starts with \, it might be the command itself
            if (macro_name.empty() && tag[0] == '\\' && strcmp(tag, "\\newcommand") != 0) {
                macro_name = tag;
            }

            // Special case: check if this is the \newcommand token itself
            if (strcmp(tag, "\\newcommand") == 0) {

                // Check its raw items
                Element* token_elem = const_cast<Element*>(child_elem.element());
                List* token_list = (List*)token_elem;


                for (int64_t k = 0; k < token_list->length; k++) {
                    Item token_item = token_list->items[k];
                    TypeId token_type = get_type_id(token_item);


                    if (token_type == LMD_TYPE_STRING) {
                        String* str = (String*)token_item.string_ptr;

                        if (macro_name.empty() && str->chars[0] == '\\') {
                            macro_name = str->chars;
                        }
                    } else if (token_type == LMD_TYPE_SYMBOL) {
                        String* sym = (String*)token_item.string_ptr;

                        if (macro_name.empty() && sym->chars[0] == '\\') {
                            macro_name = sym->chars;
                        }
                    } else if (token_type == LMD_TYPE_ELEMENT) {
                        Item elem_item;
                        elem_item.item = (uint64_t)token_item.element;
                        ElementReader elem_reader(elem_item);

                        if (macro_name.empty() && elem_reader.tagName()[0] == '\\') {
                            macro_name = elem_reader.tagName();
                        }
                    } else {

                    }
                }
            }

            // FALLBACK: Check if we still haven't found num_params and this looks like a number
            if (num_params == 0 && strcmp(tag, "curly_group") != 0 && strcmp(tag, "curly_group_command_name") != 0) {
                // Try extracting text content to see if it's a number
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* text_str = stringbuf_to_string(sb);
                if (text_str->len > 0 && text_str->chars[0] >= '0' && text_str->chars[0] <= '9') {
                    num_params = atoi(text_str->chars);

                }
            }

            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {

                // NEW: If macro_name is already set, treat this as the definition
                if (!macro_name.empty()) {
                    definition = const_cast<Element*>(child_elem.element());
                    arg_index++;
                    continue;
                }

                if (arg_index == 0) {
                    // First arg: command name (like {\greet})
                    // The command is stored as a symbol (if no children) or element (if has children)
                    Element* curly_elem = const_cast<Element*>(child_elem.element());
                    List* curly_list = (List*)curly_elem;

                    // Extract command name from curly_group_command_name
                    // After fix: command_name tokens are now strings (e.g., "greet"), not symbols
                    for (int64_t j = 0; j < curly_list->length; j++) {
                        Item item = curly_list->items[j];
                        TypeId item_type = get_type_id(item);

                        if (item_type == LMD_TYPE_STRING) {
                            String* str = (String*)item.string_ptr;
                            if (str->len > 0 && macro_name.empty()) {
                                macro_name = str->chars;
                                break;
                            }
                        }
                    }

                    // Remove leading backslash if present
                    if (!macro_name.empty() && macro_name[0] == '\\') {
                        macro_name = macro_name.substr(1);
                    }

                } else if (arg_index == 1) {
                    // Could be [num] or {definition}
                    // Check if it looks like a number
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* content_str = stringbuf_to_string(sb);
                    std::string content = content_str->chars;

                    if (!content.empty() && content[0] >= '0' && content[0] <= '9') {
                        num_params = atoi(content.c_str());
                    } else {
                        // It's the definition
                        definition = const_cast<Element*>(child_elem.element());
                    }
                } else if (arg_index == 2) {
                    // Third arg: definitely the definition
                    definition = const_cast<Element*>(child_elem.element());
                }
                arg_index++;
            }
        }
    }

    // Remove leading backslash from macro_name if present
    if (!macro_name.empty() && macro_name[0] == '\\') {
        macro_name = macro_name.substr(1);
    }

    fprintf(stderr, "DEBUG: newcommand parsed: name='%s', num_params=%d, definition=%p, default_value=%p\n", macro_name.c_str(), num_params, definition, default_value);

    if (!macro_name.empty() && definition) {
        // Check if macro already exists
        if (proc->isMacro(macro_name)) {
            log_error("Macro \\%s already defined (use \\renewcommand to redefine)", macro_name.c_str());
        } else {
            proc->registerMacro(macro_name, num_params, definition, default_value);
        }
    } else {

    }
}

static void cmd_renewcommand(LatexProcessor* proc, Item elem) {
    // \renewcommand{\name}[num]{definition}
    // Redefines an existing macro (warning if doesn't exist)
    ElementReader reader(elem);

    std::string macro_name;
    int num_params = 0;
    Element* definition = nullptr;

    // Same parsing as newcommand
    auto iter = reader.children();
    ItemReader child;
    int arg_index = 0;

    while (iter.next(&child)) {
        // NEW: Check for string child (macro name from hybrid grammar)
        if (child.isString()) {
            String* str = child.asString();
            if (macro_name.empty() && str->chars[0] == '\\') {
                macro_name = str->chars;
            }
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {
                // NEW: If macro_name is already set, treat this as the definition
                if (!macro_name.empty()) {
                    definition = const_cast<Element*>(child_elem.element());
                    arg_index++;
                    continue;
                }

                if (arg_index == 0) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* name_str = stringbuf_to_string(sb);
                    macro_name = name_str->chars;
                    if (!macro_name.empty() && macro_name[0] == '\\') {
                        macro_name = macro_name.substr(1);
                    }
                } else if (arg_index == 1) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* content_str = stringbuf_to_string(sb);
                    std::string content = content_str->chars;

                    if (!content.empty() && content[0] >= '0' && content[0] <= '9') {
                        num_params = atoi(content.c_str());
                    } else {
                        definition = const_cast<Element*>(child_elem.element());
                    }
                } else if (arg_index == 2) {
                    definition = const_cast<Element*>(child_elem.element());
                }
                arg_index++;
            } else if (strcmp(tag, "brack_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* num_str = stringbuf_to_string(sb);
                num_params = atoi(num_str->chars);
            }
        }
    }

    // Remove leading backslash from macro_name if present
    if (!macro_name.empty() && macro_name[0] == '\\') {
        macro_name = macro_name.substr(1);
    }

    if (!macro_name.empty() && definition) {
        // Warn if doesn't exist but register anyway
        if (!proc->isMacro(macro_name)) {
            log_info("Macro \\%s not previously defined (\\renewcommand used anyway)", macro_name.c_str());
        }
        fprintf(stderr, "DEBUG: renewcommand parsed: name='%s', num_params=%d, definition=%p\n", macro_name.c_str(), num_params, definition);
        proc->registerMacro(macro_name, num_params, definition);
    }
}

static void cmd_providecommand(LatexProcessor* proc, Item elem) {
    // \providecommand{\name}[num]{definition}
    // Defines a macro only if it doesn't already exist
    ElementReader reader(elem);

    std::string macro_name;
    int num_params = 0;
    Element* definition = nullptr;

    // Same parsing as newcommand
    auto iter = reader.children();
    ItemReader child;
    int arg_index = 0;

    while (iter.next(&child)) {
        // NEW: Check for string child (macro name from hybrid grammar)
        if (child.isString()) {
            String* str = child.asString();
            if (macro_name.empty() && str->chars[0] == '\\') {
                macro_name = str->chars;
            }
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {
                // NEW: If macro_name is already set, treat this as the definition
                if (!macro_name.empty()) {
                    definition = const_cast<Element*>(child_elem.element());
                    arg_index++;
                    continue;
                }

                if (arg_index == 0) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* name_str = stringbuf_to_string(sb);
                    macro_name = name_str->chars;
                    if (!macro_name.empty() && macro_name[0] == '\\') {
                        macro_name = macro_name.substr(1);
                    }
                } else if (arg_index == 1) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* content_str = stringbuf_to_string(sb);
                    std::string content = content_str->chars;

                    if (!content.empty() && content[0] >= '0' && content[0] <= '9') {
                        num_params = atoi(content.c_str());
                    } else {
                        definition = const_cast<Element*>(child_elem.element());
                    }
                } else if (arg_index == 2) {
                    definition = const_cast<Element*>(child_elem.element());
                }
                arg_index++;
            } else if (strcmp(tag, "brack_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* num_str = stringbuf_to_string(sb);
                num_params = atoi(num_str->chars);
            }
        }
    }

    // Remove leading backslash from macro_name if present
    if (!macro_name.empty() && macro_name[0] == '\\') {
        macro_name = macro_name.substr(1);
    }

    if (!macro_name.empty() && definition) {
        // Only register if doesn't exist
        if (!proc->isMacro(macro_name)) {
            proc->registerMacro(macro_name, num_params, definition);
        }
    }
}

static void cmd_def(LatexProcessor* proc, Item elem) {
    // \def\name{definition} - TeX primitive macro definition
    // Note: \def doesn't use the [n] syntax for parameters, but we'll support it for compatibility
    ElementReader reader(elem);

    std::string macro_name;
    Element* definition = nullptr;

    // Parse: first child is command name, second is definition
    auto iter = reader.children();
    ItemReader child;
    int arg_index = 0;

    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0 ||
                strcmp(tag, "generic_command") == 0) {
                if (arg_index == 0) {
                    // First: command name
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* name_str = stringbuf_to_string(sb);
                    macro_name = name_str->chars;
                    if (!macro_name.empty() && macro_name[0] == '\\') {
                        macro_name = macro_name.substr(1);
                    }
                } else if (arg_index == 1) {
                    // Second: definition
                    definition = const_cast<Element*>(child_elem.element());
                }
                arg_index++;
            }
        }
    }

    if (!macro_name.empty() && definition) {
        // Count #1, #2, etc. in definition to determine num_params
        int num_params = 0;
        Pool* pool = proc->pool();
        StringBuf* sb = stringbuf_new(pool);
        Item def_item;
        def_item.item = (uint64_t)definition;
        ElementReader def_reader(def_item);
        def_reader.textContent(sb);
        String* def_text = stringbuf_to_string(sb);

        for (size_t i = 0; i < def_text->len; i++) {
            if (def_text->chars[i] == '#' && i + 1 < def_text->len &&
                def_text->chars[i + 1] >= '1' && def_text->chars[i + 1] <= '9') {
                int param_num = def_text->chars[i + 1] - '0';
                if (param_num > num_params) {
                    num_params = param_num;
                }
            }
        }

        proc->registerMacro(macro_name, num_params, definition);
    }
}

// Font size commands
// These commands can operate in two modes:
// 1. Declaration-style (no children): \small sets font for subsequent text in current scope
// 2. Argument-style (with children): {\small text} wraps children in a span
// Note: In LaTeX, \small by itself is a declaration that affects all following text
// until the end of the current group/scope. The span wrapping happens in processText()
// based on the current font state.

static void cmd_tiny(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    // Check if this is declaration-style (no children) or argument-style (with children)
    if (reader.isEmpty()) {
        // Declaration-style: just set font state, no span here
        // The font state persists until group exit, and processText() will wrap text in spans
        gen->currentFont().size = FontSize::Tiny;
    } else {
        // Argument-style: wrap children in a span
        gen->enterGroup();
        gen->currentFont().size = FontSize::Tiny;
        proc->enterStyledSpan();
        gen->span("tiny");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_scriptsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::ScriptSize;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::ScriptSize;
        proc->enterStyledSpan();
        gen->span("scriptsize");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_footnotesize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::FootnoteSize;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::FootnoteSize;
        proc->enterStyledSpan();
        gen->span("footnotesize");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_small(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Small;
    } else if (hasEnvironmentSyntax(elem)) {
        // Environment-style (\begin{small}...): ZWS at boundaries, per-chunk wrapping
        // Use explicit "small" class, not inherited font state
        gen->enterGroup();
        gen->currentFont().size = FontSize::Small;
        proc->pushFontEnvClass("small");  // Push font env class for text wrapping
        outputFontBoundaryZWSWithClass(proc, "small");  // ZWS span at start
        proc->processChildren(elem);
        outputFontBoundaryZWSWithClass(proc, "small");  // ZWS span at end
        proc->popFontEnvClass();
        gen->exitGroup();
    } else {
        // Command-style (\small{text}): single span wrapper
        gen->enterGroup();
        gen->currentFont().size = FontSize::Small;
        proc->enterStyledSpan();
        gen->span("small");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_normalsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::NormalSize;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::NormalSize;
        proc->enterStyledSpan();
        gen->span("normalsize");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Large;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::Large;
        proc->enterStyledSpan();
        gen->span("large");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_Large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Large2;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::Large2;
        proc->enterStyledSpan();
        gen->span("Large");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_LARGE(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Large3;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::Large3;
        proc->enterStyledSpan();
        gen->span("LARGE");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Huge;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::Huge;
        proc->enterStyledSpan();
        gen->span("huge");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

static void cmd_Huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    if (reader.isEmpty()) {
        gen->currentFont().size = FontSize::Huge2;
    } else {
        gen->enterGroup();
        gen->currentFont().size = FontSize::Huge2;
        proc->enterStyledSpan();
        gen->span("Huge");
        proc->processChildren(elem);
        gen->closeElement();
        proc->exitStyledSpan();
        gen->exitGroup();
    }
}

// =============================================================================
// Special LaTeX Commands (\TeX, \LaTeX, \today, etc.)
// =============================================================================

static void cmd_TeX(LatexProcessor* proc, Item elem) {
    // \TeX - TeX logo
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();

    gen->span("tex");
    gen->text("T");
    gen->span("e");
    gen->text("e");
    gen->closeElement();  // close inner span
    gen->text("X");
    gen->closeElement();  // close outer span

    // \TeX is a space-absorbing command - set flag for ZWS output
    proc->setPendingZWSOutput(true);
}

static void cmd_LaTeX(LatexProcessor* proc, Item elem) {
    // \LaTeX - LaTeX logo
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();

    gen->span("latex");
    gen->text("L");
    gen->span("a");
    gen->text("a");
    gen->closeElement();  // close inner span
    gen->text("T");
    gen->span("e");
    gen->text("e");
    gen->closeElement();  // close inner span
    gen->text("X");
    gen->closeElement();  // close outer span

    // \LaTeX is a space-absorbing command - set flag for ZWS output
    proc->setPendingZWSOutput(true);
}

static void cmd_today(LatexProcessor* proc, Item elem) {
    // \today - Current date
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();

    // Format: December 12, 2025 (example)
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%B %d, %Y", tm_info);

    gen->text(buffer);
}

static void cmd_empty(LatexProcessor* proc, Item elem) {
    // Three cases for \empty:
    // 1. \empty (no braces) - produces nothing (null command)
    // 2. \empty{} (empty braces) - output ZWS
    // 3. \begin{empty}...\end{empty} (environment) - process content + ZWS at boundaries

    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    // Check what kind of children we have
    bool has_empty_curly_group = false;
    bool has_other_content = false;

    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                // Check if the curly_group is empty
                auto group_iter = child_elem.children();
                ItemReader group_child;
                bool group_has_content = false;
                while (group_iter.next(&group_child)) {
                    if (group_child.isElement()) {
                        group_has_content = true;
                        break;
                    } else if (group_child.isString()) {
                        const char* str = group_child.cstring();
                        if (str && str[0] != '\0') {
                            // Check if it's not just whitespace
                            for (const char* p = str; *p; p++) {
                                if (!isspace(*p)) {
                                    group_has_content = true;
                                    break;
                                }
                            }
                            if (group_has_content) break;
                        }
                    }
                }
                if (!group_has_content) {
                    has_empty_curly_group = true;
                } else {
                    has_other_content = true;
                }
            } else {
                // Has other content (e.g., paragraph from environment)
                has_other_content = true;
            }
        } else if (child.isString()) {
            has_other_content = true;
        }
    }

    // Case 3: Environment with content - ZWS at start (if leading whitespace) and end
    // ZWS at end prevents space after environment content from collapsing
    // with following content. ZWS at start (before leading whitespace) preserves
    // the boundary when there's whitespace before the environment content.
    if (has_other_content) {
        // Check if content starts with whitespace
        // The empty element may contain a 'paragraph' wrapper, so we need to look inside
        auto first_iter = reader.children();
        ItemReader first_child;
        bool has_leading_whitespace = false;
        while (first_iter.next(&first_child)) {
            if (first_child.isString()) {
                const char* str = first_child.cstring();
                if (str && str[0] != '\0' && isspace(str[0])) {
                    has_leading_whitespace = true;
                }
                break;
            } else if (first_child.isElement()) {
                // If first child is a paragraph element, look inside it
                ElementReader child_elem(first_child.item());
                const char* tag = child_elem.tagName();
                if (tag && strcmp(tag, "paragraph") == 0) {
                    // Look at the paragraph's first child
                    auto para_iter = child_elem.children();
                    ItemReader para_child;
                    while (para_iter.next(&para_child)) {
                        if (para_child.isString()) {
                            const char* str = para_child.cstring();
                            if (str && str[0] != '\0' && isspace(str[0])) {
                                has_leading_whitespace = true;
                            }
                            break;
                        } else if (para_child.isElement()) {
                            break;  // Non-string first child means no leading whitespace
                        }
                    }
                }
                break;  // First child is element, done checking
            }
        }

        // Output ZWS at start if there's leading whitespace in content
        if (has_leading_whitespace) {
            proc->ensureParagraph();
            gen->text("\xE2\x80\x8B");  // ZWS at start
        }

        proc->processChildren(elem);
        gen->text("\xE2\x80\x8B");  // ZWS at end
        return;
    }

    // Case 2: Empty braces - output ZWS
    if (has_empty_curly_group) {
        proc->ensureParagraph();
        gen->text("\xE2\x80\x8B");  // UTF-8 encoding of U+200B
        return;
    }

    // Case 1: No braces - output nothing (null command)
}

// Helper function to convert LaTeX lengths to pixels
// LaTeX.js conversion: 1pt = 1.333px (based on 72pt/inch, 96px/inch)
static double convertLatexLengthToPixels(const char* length_str) {
    if (!length_str || !*length_str) {
        return 0.0;
    }

    // Parse numeric value
    char* end_ptr;
    double value = strtod(length_str, &end_ptr);

    // Skip whitespace after number
    while (*end_ptr == ' ' || *end_ptr == '\t') {
        end_ptr++;
    }

    // Check unit (case-insensitive comparison)
    const char* unit = end_ptr;
    if (strncasecmp(unit, "pt", 2) == 0) {
        return value * 1.333;  // 1pt = 1.333px
    } else if (strncasecmp(unit, "mm", 2) == 0) {
        return value * 3.7795;  // 1mm = 3.7795px
    } else if (strncasecmp(unit, "cm", 2) == 0) {
        return value * 37.795;  // 1cm = 37.795px (10mm)
    } else if (strncasecmp(unit, "in", 2) == 0) {
        return value * 96.0;    // 1in = 96px
    } else if (strncasecmp(unit, "em", 2) == 0) {
        return value * 16.0;    // 1em ≈ 16px (default font size)
    } else if (strncasecmp(unit, "ex", 2) == 0) {
        return value * 8.0;     // 1ex ≈ 8px (approximately 0.5em)
    } else if (strncasecmp(unit, "pc", 2) == 0) {
        return value * 16.0;    // 1pc = 12pt = 16px
    } else if (strncasecmp(unit, "bp", 2) == 0) {
        return value * 1.333;   // 1bp = 1pt (big point = PostScript point)
    } else if (strncasecmp(unit, "dd", 2) == 0) {
        return value * 1.494;   // 1dd = 1.07pt (Didot point)
    } else if (strncasecmp(unit, "cc", 2) == 0) {
        return value * 17.9;    // 1cc = 12dd (Cicero)
    } else if (strncasecmp(unit, "sp", 2) == 0) {
        return value * 0.000020;  // 1sp = 1/65536 pt (scaled point, smallest TeX unit)
    }

    // Default: assume pixels if no recognized unit
    return value;
}

static void cmd_unskip(LatexProcessor* proc, Item elem) {
    // \unskip - removes preceding whitespace from output
    // Unlike most LaTeX commands, this "breaks out" of groups - it affects output
    // even when inside {...} groups
    (void)elem;
    HtmlGenerator* gen = proc->generator();
    gen->trimTrailingWhitespace();

    // Mark that the containing group should not output ZWS
    LatexProcessor* mutable_proc = const_cast<LatexProcessor*>(proc);
    mutable_proc->setSuppressGroupZWS(true);
}

static void cmd_ignorespaces(LatexProcessor* proc, Item elem) {
    // \ignorespaces - skips following whitespace
    // Unlike \unskip, this does NOT break out of groups - it only affects whitespace
    // that follows it within the current group
    (void)elem;
    LatexProcessor* mutable_proc = const_cast<LatexProcessor*>(proc);
    mutable_proc->setStripNextLeadingSpace(true);

    // Mark that the containing group should not output ZWS
    mutable_proc->setSuppressGroupZWS(true);
}

static void cmd_ligature_break(LatexProcessor* proc, Item elem) {
    // \/ - ligature break (zero-width non-joiner)
    // Prevents ligatures from forming, e.g., shelf\/ful prevents "shelfful" from becoming "shelﬀul"
    // Inserts U+200C zero-width non-joiner
    (void)elem;
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\xE2\x80\x8C");  // U+200C zero-width non-joiner
}

static void cmd_textbackslash(LatexProcessor* proc, Item elem) {
    // \textbackslash - Outputs a backslash character
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\\");
    // If followed by empty curly group, add ZWS
    if (hasEmptyCurlyGroupChild(elem)) {
        gen->text("\xe2\x80\x8b");  // ZWS
    }
}

static void cmd_textellipsis(LatexProcessor* proc, Item elem) {
    // \textellipsis - Outputs an ellipsis character (…)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("…");
}

static void cmd_textendash(LatexProcessor* proc, Item elem) {
    // \textendash - Outputs an en-dash character (–)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("–");  // U+2013 EN DASH
}

static void cmd_textemdash(LatexProcessor* proc, Item elem) {
    // \textemdash - Outputs an em-dash character (—)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("—");  // U+2014 EM DASH
}

static void cmd_ldots(LatexProcessor* proc, Item elem) {
    // \ldots - Outputs an ellipsis character (…) - alias for \textellipsis
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("…");
}

static void cmd_dots(LatexProcessor* proc, Item elem) {
    // \dots - Outputs an ellipsis character (…)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("…");
}

// Helper to convert char code to UTF-8 string
static std::string codepoint_to_utf8(uint32_t codepoint) {
    std::string result;
    if (codepoint < 0x80) {
        result += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        result += static_cast<char>(0xC0 | (codepoint >> 6));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        result += static_cast<char>(0xE0 | (codepoint >> 12));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x110000) {
        result += static_cast<char>(0xF0 | (codepoint >> 18));
        result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return result;
}

static void cmd_char(LatexProcessor* proc, Item elem) {
    // \char<number> or \char"<hex> - output character by code
    // NOTE: \char is a TeX primitive that consumes the following number token.
    // The parser creates <char/> followed by "98" as siblings, not parent-child.
    // This command currently doesn't work correctly due to this parser limitation.
    // TODO: Fix parser to consume following number, or implement lookahead in formatter.
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();

    // Get the argument text using ElementReader
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* arg_str = stringbuf_to_string(sb);

    if (!arg_str || arg_str->len == 0) {
        return;
    }

    // Skip leading whitespace
    const char* arg = arg_str->chars;
    while (*arg && (*arg == ' ' || *arg == '\t' || *arg == '\n' || *arg == '\r')) {
        arg++;
    }

    if (!*arg) {
        return;
    }

    uint32_t charcode = 0;
    // Check if hex notation (starts with " or 0x)
    if (arg[0] == '"') {
        charcode = std::strtoul(arg + 1, nullptr, 16);
    } else if (strlen(arg) > 2 && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
        charcode = std::strtoul(arg + 2, nullptr, 16);
    } else {
        charcode = std::strtoul(arg, nullptr, 10);
    }

    if (charcode > 0) {
        // Special case: 0xA0 is non-breaking space, output as HTML entity
        if (charcode == 0xA0) {
            gen->writer()->writeRawHtml("&nbsp;");
        } else {
            std::string utf8 = codepoint_to_utf8(charcode);
            gen->text(utf8.c_str());
        }
    }
}

static void cmd_symbol(LatexProcessor* proc, Item elem) {
    // \symbol{number} or \symbol{"hex} - output character by code
    // Same as \char but with braced argument
    cmd_char(proc, elem);
}

static void cmd_makeatletter(LatexProcessor* proc, Item elem) {
    // \makeatletter - Make @ a letter (category code change)
    // In HTML output, this doesn't affect anything
    // Just process children
    proc->processChildren(elem);
}

static void cmd_makeatother(LatexProcessor* proc, Item elem) {
    // \makeatother - Make @ other (restore category code)
    // In HTML output, this doesn't affect anything
    // Just process children
    proc->processChildren(elem);
}

// =============================================================================
// Sectioning commands
// =============================================================================

static void cmd_section(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();

    // Only end paragraph if we're NOT inside a styled span (inline context)
    // When section appears inside \emph{} or similar, keep inline flow
    // and let CSS handle the visual appearance (per LaTeX.js behavior)
    if (!proc->inStyledSpan()) {
        proc->endParagraph();
    }

    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();

    // Find the title argument (from "title" field or children/textContent)
    std::string title;

    // First try to get title from "title" field (new grammar structure)
    if (elem_reader.has_attr("title")) {
        ItemReader title_reader = elem_reader.get_attr("title");
        if (title_reader.isElement()) {
            ElementReader title_elem(title_reader.item());
            StringBuf* sb = stringbuf_new(pool);
            title_elem.textContent(sb);
            String* title_str = stringbuf_to_string(sb);
            title = title_str->chars;
        }
    }

    // Fallback: collect text content from children (old parser structure)
    if (title.empty()) {
        StringBuf* title_sb = stringbuf_new(pool);
        auto iter = elem_reader.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isString()) {
                stringbuf_append_str(title_sb, child.cstring());
            } else if (child.isElement()) {
                ElementReader child_elem(child.item());
                // Skip label elements from title
                if (strcmp(child_elem.tagName(), "label") != 0) {
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* text = stringbuf_to_string(sb);
                    stringbuf_append_str(title_sb, text->chars);
                }
            }
        }
        String* title_str = stringbuf_to_string(title_sb);
        title = title_str->chars;
    }

    gen->startSection("section", false, title, title);

    // Now register any labels as children of section
    auto label_iter = elem_reader.children();
    ItemReader child;
    while (label_iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "label") == 0) {
                // Found a \label - register it with section's anchor
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* label_str = stringbuf_to_string(sb);
                gen->setLabel(label_str->chars);
            }
        }
    }
    // NOTE: Do NOT call processChildren - section heading is complete
}

static void cmd_subsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();

    // End any open paragraph before section heading
    proc->endParagraph();

    ElementReader elem_reader(elem);

    // Find the title argument (from "title" field or first curly_group child)
    std::string title;

    // First try to get title from "title" field (new grammar structure)
    if (elem_reader.has_attr("title")) {
        ItemReader title_reader = elem_reader.get_attr("title");
        if (title_reader.isElement()) {
            ElementReader title_elem(title_reader.item());
            Pool* pool = proc->pool();
            StringBuf* sb = stringbuf_new(pool);
            title_elem.textContent(sb);
            String* title_str = stringbuf_to_string(sb);
            title = title_str->chars;
        }
    }

    // Fallback: try first curly_group child or string child (command parsing structure)
    if (title.empty()) {
        auto iter = elem_reader.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem(child.item());
                if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* title_str = stringbuf_to_string(sb);
                    title = title_str->chars;
                    break;
                }
            } else if (child.isString()) {
                String* str = child.asString();
                title = str->chars;
                break;
            }
        }
    }

    gen->startSection("subsection", false, title, title);
    // NOTE: Do NOT call processChildren - section heading is complete
    //       Sections are flat in new grammar, not containers
}

static void cmd_subsubsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();

    // End any open paragraph before section heading
    proc->endParagraph();

    ElementReader elem_reader(elem);

    // Find the title argument (from "title" field or first curly_group/string child)
    std::string title;

    // First try to get title from "title" field (new grammar structure)
    if (elem_reader.has_attr("title")) {
        ItemReader title_reader = elem_reader.get_attr("title");
        if (title_reader.isElement()) {
            ElementReader title_elem(title_reader.item());
            Pool* pool = proc->pool();
            StringBuf* sb = stringbuf_new(pool);
            title_elem.textContent(sb);
            String* title_str = stringbuf_to_string(sb);
            title = title_str->chars;
        }
    }

    // Fallback: try first curly_group child or string child (command parsing structure)
    if (title.empty()) {
        auto iter = elem_reader.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem(child.item());
                if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* title_str = stringbuf_to_string(sb);
                    title = title_str->chars;
                    break;
                }
            } else if (child.isString()) {
                String* str = child.asString();
                title = str->chars;
                break;
            }
        }
    }

    gen->startSection("subsubsection", false, title, title);
    // NOTE: Do NOT call processChildren - section heading is complete
}

static void cmd_chapter(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();

    // End any open paragraph before section heading
    proc->endParagraph();

    ElementReader elem_reader(elem);

    // Find the title argument (from "title" field or textContent)
    std::string title;

    if (elem_reader.has_attr("title")) {
        ItemReader title_reader = elem_reader.get_attr("title");
        if (title_reader.isElement()) {
            ElementReader title_elem(title_reader.item());
            Pool* pool = proc->pool();
            StringBuf* sb = stringbuf_new(pool);
            title_elem.textContent(sb);
            String* title_str = stringbuf_to_string(sb);
            title = title_str->chars;
        }
    }

    if (title.empty()) {
        Pool* pool = proc->pool();
        StringBuf* sb = stringbuf_new(pool);
        elem_reader.textContent(sb);
        String* title_str = stringbuf_to_string(sb);
        title = title_str->chars;
    }

    gen->startSection("chapter", false, title, title);
}

static void cmd_part(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();

    // End any open paragraph before section heading
    proc->endParagraph();

    ElementReader elem_reader(elem);

    // Find the title argument (from "title" field or textContent)
    std::string title;

    if (elem_reader.has_attr("title")) {
        ItemReader title_reader = elem_reader.get_attr("title");
        if (title_reader.isElement()) {
            ElementReader title_elem(title_reader.item());
            Pool* pool = proc->pool();
            StringBuf* sb = stringbuf_new(pool);
            title_elem.textContent(sb);
            String* title_str = stringbuf_to_string(sb);
            title = title_str->chars;
        }
    }

    if (title.empty()) {
        Pool* pool = proc->pool();
        StringBuf* sb = stringbuf_new(pool);
        elem_reader.textContent(sb);
        String* title_str = stringbuf_to_string(sb);
        title = title_str->chars;
    }

    gen->startSection("part", false, title, title);
}

// Helper to extract label string from brack_group children
// For formatted labels, pass the brack_group Item to createItem
static std::string extractLabelFromBrackGroup(ElementReader& brack_elem) {
    std::string label_buf;

    for (size_t k = 0; k < brack_elem.childCount(); k++) {
        ItemReader brack_child = brack_elem.childAt(k);
        if (brack_child.isString()) {
            label_buf += brack_child.cstring();
        } else if (brack_child.isElement()) {
            ElementReader child_elem = brack_child.asElement();
            const char* child_tag = child_elem.tagName();
            if (child_tag) {
                // Convert common symbol commands to their unicode equivalents
                if (strcmp(child_tag, "textendash") == 0) {
                    label_buf += "–";  // U+2013 EN DASH
                } else if (strcmp(child_tag, "textemdash") == 0) {
                    label_buf += "—";  // U+2014 EM DASH
                } else if (strcmp(child_tag, "textbullet") == 0) {
                    label_buf += "•";  // U+2022 BULLET
                } else if (strcmp(child_tag, "textperiodcentered") == 0) {
                    label_buf += "·";  // U+00B7 MIDDLE DOT
                } else if (strcmp(child_tag, "textasteriskcentered") == 0) {
                    label_buf += "*";
                } else {
                    // For other elements, try to extract text content
                    for (size_t m = 0; m < child_elem.childCount(); m++) {
                        ItemReader inner = child_elem.childAt(m);
                        if (inner.isString()) {
                            label_buf += inner.cstring();
                        }
                    }
                }
            }
        }
    }

    return label_buf;
}

// Helper to process list items - handles the tree structure where item and its content are siblings
static void processListItems(LatexProcessor* proc, Item elem, const char* list_type) {
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    bool in_item = false;
    bool at_item_start = false;  // Track if we're at the very start of an item (for whitespace trimming)
    bool item_paragraph_open = false;  // Track if we have an open <p> inside item
    bool next_paragraph_noindent = false;  // Track if next paragraph should have noindent class

    for (size_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (!tag) continue;

            // Handle paragraph wrapper
            if (strcmp(tag, "paragraph") == 0) {
                // Process paragraph contents for items
                for (size_t j = 0; j < child_elem.childCount(); j++) {
                    ItemReader para_child = child_elem.childAt(j);

                    if (para_child.isElement()) {
                        ElementReader para_child_elem = para_child.asElement();
                        const char* para_tag = para_child_elem.tagName();
                        if (!para_tag) continue;

                        if (strcmp(para_tag, "item") == 0 || strcmp(para_tag, "enum_item") == 0) {
                            // Close previous item if open
                            if (in_item) {
                                proc->setInParagraph(false);  // Reset paragraph state before closing item
                                item_paragraph_open = false;  // Reset paragraph tracking
                                gen->endItem();  // Close li/dd with proper structure
                            }

                            // Get optional label from item
                            // For description/itemize/enumerate lists, label is in brack_group child: \item[label]
                            bool has_brack_group = false;  // Track if brack_group exists (for empty label)
                            std::string html_label;  // Rendered HTML label content

                            if (para_child_elem.childCount() > 0) {
                                ItemReader first = para_child_elem.childAt(0);
                                if (first.isElement()) {
                                    ElementReader first_elem = first.asElement();
                                    const char* first_tag = first_elem.tagName();
                                    if (first_tag && strcmp(first_tag, "brack_group") == 0) {
                                        has_brack_group = true;
                                        // Render the brack_group contents to HTML using capture mode
                                        // Enter group to isolate font changes from affecting later content
                                        gen->enterGroup();
                                        // Enter inline mode to suppress paragraph creation
                                        proc->enterInlineMode();
                                        gen->startCapture();
                                        for (size_t k = 0; k < first_elem.childCount(); k++) {
                                            ItemReader brack_child = first_elem.childAt(k);
                                            proc->processNode(brack_child.item());
                                        }
                                        html_label = gen->endCapture();
                                        proc->exitInlineMode();
                                        gen->exitGroup();
                                    }
                                }
                            }

                            if (has_brack_group) {
                                gen->createItemWithHtmlLabel(html_label.c_str());
                            } else {
                                gen->createItem(nullptr);
                            }
                            proc->setInParagraph(true);  // Mark that we're now inside the item's <p>
                            item_paragraph_open = true;  // <p> is now open
                            in_item = true;
                            at_item_start = true;  // Next text should be trimmed
                            next_paragraph_noindent = false;  // Reset noindent flag for new item
                        } else {
                            // Other element within paragraph
                            if (in_item) {
                                const char* elem_tag = para_child_elem.tagName();

                                // Check if this is the noindent command
                                if (elem_tag && strcmp(elem_tag, "noindent") == 0) {
                                    // \noindent - close current paragraph and set flag for next
                                    if (item_paragraph_open) {
                                        gen->trimTrailingWhitespace();
                                        gen->closeElement();  // Close <p>
                                        proc->setInParagraph(false);
                                        item_paragraph_open = false;
                                    }
                                    next_paragraph_noindent = true;
                                    at_item_start = true;  // Trim leading whitespace
                                    continue;
                                }

                                // Check if this is a block element (list, etc.)
                                bool is_block = elem_tag && (
                                    strcmp(elem_tag, "itemize") == 0 ||
                                    strcmp(elem_tag, "enumerate") == 0 ||
                                    strcmp(elem_tag, "description") == 0 ||
                                    strcmp(elem_tag, "center") == 0 ||
                                    strcmp(elem_tag, "quote") == 0 ||
                                    strcmp(elem_tag, "quotation") == 0 ||
                                    strcmp(elem_tag, "verse") == 0 ||
                                    strcmp(elem_tag, "flushleft") == 0 ||
                                    strcmp(elem_tag, "flushright") == 0
                                );

                                if (is_block) {
                                    // Close <p> before block element
                                    if (item_paragraph_open) {
                                        gen->trimTrailingWhitespace();
                                        gen->closeElement();  // Close <p>
                                        proc->setInParagraph(false);  // <p> is now closed
                                        item_paragraph_open = false;
                                    }
                                    proc->processNode(para_child.item());
                                    // DON'T open new <p> here - let endItem handle it
                                    // The <p> will be opened lazily when text content is encountered
                                    at_item_start = true;  // Trim leading whitespace
                                } else {
                                    proc->processNode(para_child.item());
                                    at_item_start = false;
                                }
                            }
                        }
                    } else if (para_child.isSymbol()) {
                        // Symbol - check for parbreak
                        String* sym = para_child.asSymbol();
                        if (sym && strcmp(sym->chars, "parbreak") == 0) {
                            // Paragraph break within list item - close </p> only
                            if (in_item && item_paragraph_open) {
                                gen->itemParagraphBreak();
                                item_paragraph_open = false;
                                proc->setInParagraph(false);
                                at_item_start = true;  // Trim leading whitespace in new paragraph
                            }
                        }
                    } else if (para_child.isString()) {
                        // Text content
                        const char* text = para_child.cstring();
                        log_debug("processListItems: text child '%s', at_item_start=%d, item_paragraph_open=%d",
                                  text, at_item_start ? 1 : 0, item_paragraph_open ? 1 : 0);
                        if (in_item && text && text[0] != '\0') {
                            // Trim leading whitespace at start of item or after paragraph break
                            if (at_item_start) {
                                while (*text && isspace((unsigned char)*text)) {
                                    text++;
                                }
                                // Only reset at_item_start when we have actual content
                                // This way multiple whitespace-only text nodes are all skipped
                            }

                            // Skip if now empty after trimming
                            if (text[0] != '\0') {
                                // Reset at_item_start now that we have actual content
                                at_item_start = false;

                                // Open paragraph if needed (lazy paragraph opening)
                                if (!item_paragraph_open) {
                                    log_debug("processListItems: lazy opening p for text '%s'", text);
                                    const char* p_class = next_paragraph_noindent ? "noindent" : nullptr;
                                    gen->writer()->openTag("p", p_class);
                                    item_paragraph_open = true;
                                    proc->setInParagraph(true);
                                    next_paragraph_noindent = false;  // Reset flag after use
                                }
                                // Convert apostrophes and output text
                                std::string converted = convertApostrophes(text);
                                gen->text(converted.c_str());
                            }
                        }
                    }
                }
                continue;
            }

            // Direct item (not in paragraph wrapper)
            if (strcmp(tag, "item") == 0 || strcmp(tag, "enum_item") == 0) {
                if (in_item) {
                    proc->setInParagraph(false);  // Reset paragraph state before closing item
                    gen->endItem();  // Close previous item with proper structure
                }

                // Get optional label from item
                // For description/itemize/enumerate lists, label is in brack_group child: \item[label]
                const char* label = nullptr;
                std::string label_buf2;  // Buffer to hold extracted label
                bool has_brack_group2 = false;  // Track if brack_group exists

                if (child_elem.childCount() > 0) {
                    ItemReader first = child_elem.childAt(0);
                    if (first.isElement()) {
                        ElementReader first_elem = first.asElement();
                        const char* first_tag = first_elem.tagName();
                        if (first_tag && strcmp(first_tag, "brack_group") == 0) {
                            has_brack_group2 = true;
                            // Extract label from brack_group using helper
                            label_buf2 = extractLabelFromBrackGroup(first_elem);
                            label = label_buf2.c_str();
                        }
                    } else if (first.isString()) {
                        label = first.cstring();
                    }
                }

                gen->createItem(has_brack_group2 ? label : nullptr);
                proc->setInParagraph(true);  // Mark that we're now inside the item's <p>
                in_item = true;
                at_item_start = true;  // Next text should be trimmed
            } else {
                // Other element
                if (in_item) {
                    proc->processNode(child.item());
                    at_item_start = false;  // Content processed
                }
            }
        } else if (child.isString()) {
            const char* text = child.cstring();
            if (in_item && text && text[0] != '\0') {
                // Trim leading whitespace at start of item
                if (at_item_start) {
                    while (*text && isspace((unsigned char)*text)) {
                        text++;
                    }
                    at_item_start = false;
                }

                // Skip if now empty after trimming
                if (text[0] != '\0') {
                    // Convert apostrophes and output text
                    std::string converted = convertApostrophes(text);
                    gen->text(converted.c_str());
                }
            }
        }
    }

    // Close last item
    if (in_item) {
        proc->setInParagraph(false);  // Reset paragraph state before closing item
        gen->endItem();  // Close last item with proper structure
    }
}

// Helper to scan for alignment declarations at the start of list content
static const char* scanForListAlignment(Item elem) {
    ElementReader elem_reader(elem);

    // Scan element children for alignment declarations

    // Look for centering/raggedright/raggedleft commands in first paragraph
    for (size_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (!tag) continue;

            // Check paragraph wrapper
            if (strcmp(tag, "paragraph") == 0) {
                // Scan paragraph content for alignment commands before first item
                for (size_t j = 0; j < child_elem.childCount(); j++) {
                    ItemReader para_child = child_elem.childAt(j);

                    if (para_child.isElement()) {
                        ElementReader para_child_elem = para_child.asElement();
                        const char* para_tag = para_child_elem.tagName();
                        if (!para_tag) continue;

                        // Stop at first item - alignment must come before items
                        if (strcmp(para_tag, "item") == 0 || strcmp(para_tag, "enum_item") == 0) {
                            return nullptr;
                        }

                        // Check for alignment commands
                        if (strcmp(para_tag, "centering") == 0) {
                            return "centering";
                        } else if (strcmp(para_tag, "raggedright") == 0) {
                            return "raggedright";
                        } else if (strcmp(para_tag, "raggedleft") == 0) {
                            return "raggedleft";
                        }
                    }
                }
            }
        }
    }

    return nullptr;
}

// List environment commands

static void cmd_itemize(LatexProcessor* proc, Item elem) {
    // \begin{itemize} ... \end{itemize}
    HtmlGenerator* gen = proc->generator();

    // Save paragraph state - nested lists shouldn't corrupt parent's paragraph tracking
    bool saved_in_paragraph = proc->inParagraph();

    // Scan for alignment declarations in list content
    const char* list_alignment = scanForListAlignment(elem);
    if (list_alignment) {
        // Set alignment in processor so items can use it
        proc->setNextParagraphAlignment(list_alignment);
    }

    // Pass alignment to list
    gen->startItemize(list_alignment ? list_alignment : proc->getCurrentAlignment());
    processListItems(proc, elem, "itemize");
    gen->endItemize();

    // Clear alignment if it was set by the list
    if (list_alignment) {
        proc->setNextParagraphAlignment(nullptr);
    }

    // Restore paragraph state
    proc->setInParagraph(saved_in_paragraph);

    // Next paragraph should have class="continue"
    proc->setNextParagraphIsContinue();
}

static void cmd_enumerate(LatexProcessor* proc, Item elem) {
    // \begin{enumerate} ... \end{enumerate}
    HtmlGenerator* gen = proc->generator();

    // Save paragraph state - nested lists shouldn't corrupt parent's paragraph tracking
    bool saved_in_paragraph = proc->inParagraph();

    // Scan for alignment declarations in list content
    const char* list_alignment = scanForListAlignment(elem);
    if (list_alignment) {
        // Set alignment in processor so items can use it
        proc->setNextParagraphAlignment(list_alignment);
    }

    // Pass alignment to list
    gen->startEnumerate(list_alignment ? list_alignment : proc->getCurrentAlignment());
    processListItems(proc, elem, "enumerate");
    gen->endEnumerate();

    // Clear alignment if it was set by the list
    if (list_alignment) {
        proc->setNextParagraphAlignment(nullptr);
    }

    // Restore paragraph state
    proc->setInParagraph(saved_in_paragraph);

    // Next paragraph should have class="continue"
    proc->setNextParagraphIsContinue();
}

static void cmd_description(LatexProcessor* proc, Item elem) {
    // \begin{description} ... \end{description}
    HtmlGenerator* gen = proc->generator();

    // Save paragraph state - nested lists shouldn't corrupt parent's paragraph tracking
    bool saved_in_paragraph = proc->inParagraph();

    gen->startDescription();
    processListItems(proc, elem, "description");
    gen->endDescription();

    // Restore paragraph state
    proc->setInParagraph(saved_in_paragraph);

    // Next paragraph should have class="continue"
    proc->setNextParagraphIsContinue();
}

static void cmd_item(LatexProcessor* proc, Item elem) {
    // \item or \item[label]
    HtmlGenerator* gen = proc->generator();

    // Check if there's an optional label argument
    ElementReader elem_reader(elem);
    const char* label = nullptr;

    // Try to get first child as label (if it exists)
    if (elem_reader.childCount() > 0) {
        ItemReader first_child = elem_reader.childAt(0);
        if (first_child.isString()) {
            label = first_child.cstring();
        }
    }

    gen->createItem(label);
    proc->setInParagraph(true);  // Mark that we're now inside the item's <p>
    proc->processChildren(elem);
    proc->setInParagraph(false);  // Reset before closing
    gen->closeElement();  // Close li/dd
}

// Basic environment commands

static void cmd_quote(LatexProcessor* proc, Item elem) {
    // \begin{quote} ... \end{quote}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startQuote();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endQuote();
    proc->setNextParagraphIsContinue();
}

static void cmd_quotation(LatexProcessor* proc, Item elem) {
    // \begin{quotation} ... \end{quotation}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startQuotation();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endQuotation();
    proc->setNextParagraphIsContinue();
}

static void cmd_verse(LatexProcessor* proc, Item elem) {
    // \begin{verse} ... \end{verse}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startVerse();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endVerse();
    proc->setNextParagraphIsContinue();
}

static void cmd_center(LatexProcessor* proc, Item elem) {
    // \begin{center} ... \end{center}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startCenter();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endCenter();
    proc->setNextParagraphIsContinue();
}

static void cmd_flushleft(LatexProcessor* proc, Item elem) {
    // \begin{flushleft} ... \end{flushleft}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startFlushLeft();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endFlushLeft();
    proc->setNextParagraphIsContinue();
}

static void cmd_flushright(LatexProcessor* proc, Item elem) {
    // \begin{flushright} ... \end{flushright}
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();
    gen->startFlushRight();
    proc->processChildren(elem);
    proc->closeParagraphIfOpen();
    gen->endFlushRight();
    proc->setNextParagraphIsContinue();
}

static void cmd_comment(LatexProcessor* proc, Item elem) {
    // \begin{comment} ... \end{comment}
    // Comment environment - skip all content (do nothing)
    (void)proc;
    (void)elem;
}

static void cmd_multicols(LatexProcessor* proc, Item elem) {
    // \begin{multicols}{n}[pretext] ... \end{multicols}
    // Multi-column layout using CSS columns
    // n = number of columns
    // [pretext] = optional text to span all columns before the multi-column content
    HtmlGenerator* gen = proc->generator();

    proc->closeParagraphIfOpen();

    ElementReader reader(elem);
    int num_cols = 2;  // Default to 2 columns
    int first_content_idx = 0;  // Index of first content child (after column count)
    Item pretext_item = ItemNull;  // Optional pretext before multicol div

    // First child should be the number of columns (wrapped in curly_group)
    if (reader.childCount() > 0) {
        ItemReader first_child = reader.childAt(0);
        // Check if it's a curly_group containing the column count
        if (first_child.isElement()) {
            ElementReader elem_reader = first_child.asElement();
            const char* tag = elem_reader.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                // Extract number from inside curly_group
                if (elem_reader.childCount() > 0) {
                    ItemReader num_child = elem_reader.childAt(0);
                    if (num_child.isString()) {
                        const char* num_str = num_child.cstring();
                        if (num_str) {
                            num_cols = atoi(num_str);
                            if (num_cols < 1) num_cols = 1;
                            if (num_cols > 10) num_cols = 10;
                        }
                    }
                }
                first_content_idx = 1;  // Skip the curly_group
            }
        } else if (first_child.isString()) {
            // Direct string (backward compatibility)
            const char* num_str = first_child.cstring();
            if (num_str) {
                num_cols = atoi(num_str);
                if (num_cols < 1) num_cols = 1;
                if (num_cols > 10) num_cols = 10;
            }
            first_content_idx = 1;
        }
    }

    // Check for optional pretext (second argument in brackets)
    if (first_content_idx < reader.childCount()) {
        ItemReader second_child = reader.childAt(first_content_idx);
        if (second_child.isElement()) {
            ElementReader second_elem = second_child.asElement();
            const char* tag = second_elem.tagName();
            if (tag && strcmp(tag, "brack_group") == 0) {
                pretext_item = second_child.item();
                first_content_idx++;  // Skip the brack_group too
            }
        }
    }

    // Process pretext before the multicols div (spans all columns)
    if (pretext_item.map != nullptr) {
        ElementReader pretext_reader(pretext_item);
        for (int64_t i = 0; i < pretext_reader.childCount(); i++) {
            proc->processNode(pretext_reader.childAt(i).item());
        }
    }

    proc->closeParagraphIfOpen();

    // Build attributes string including class and style
    char attrs[256];
    snprintf(attrs, sizeof(attrs), "class=\"multicols\" style=\"column-count:%d\"", num_cols);

    // Open multicols container with all attributes
    gen->writer()->openTagRaw("div", attrs);

    // Process content (skip first child which is the column count)
    for (int64_t i = first_content_idx; i < reader.childCount(); i++) {
        proc->processNode(reader.childAt(i).item());
    }

    proc->closeParagraphIfOpen();
    gen->writer()->closeTag("div");

    proc->setNextParagraphIsContinue();
}

// Forward declaration
static void cmd_verb_command(LatexProcessor* proc, Item elem);

static void cmd_verb(LatexProcessor* proc, Item elem) {
    // \verb|text| when parsed as a regular command (scanner fallback)
    // In this case, the children contain the verbatim content
    // This happens when the scanner doesn't recognize the \verb pattern
    HtmlGenerator* gen = proc->generator();

    proc->ensureParagraph();

    ElementReader reader(elem);
    int64_t child_count = reader.childCount();

    if (child_count == 0) {
        // No content - just output empty code block
        gen->writer()->openTagRaw("code", "class=\"tt\"");
        gen->writer()->closeTag("code");
        return;
    }

    // Check if first child is a string that looks like the full \verb token
    // (scanner format: "\verb|content|")
    ItemReader first_child = reader.childAt(0);
    if (first_child.isString()) {
        const char* text = first_child.cstring();
        if (text && strncmp(text, "\\verb", 5) == 0) {
            // This is actually a verb_command format - delegate to that handler
            cmd_verb_command(proc, elem);
            return;
        }
    }

    // Regular command form - extract content from children
    gen->writer()->openTagRaw("code", "class=\"tt\"");

    // Collect all text content from children
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child = reader.childAt(i);
        if (child.isString()) {
            String* str = child.asString();
            if (str && str->chars && str->len > 0) {
                // Output verbatim - don't process special characters
                std::string content(str->chars, str->len);
                gen->writer()->writeText(content.c_str());
            }
        } else if (child.isElement()) {
            // Process child elements (might be nested commands)
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            // For curly groups, extract their text content
            if (tag && (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_text") == 0)) {
                StringBuf* sb = stringbuf_new(proc->pool());
                child_elem.textContent(sb);
                if (sb->str && sb->length > 0) {
                    std::string content(sb->str->chars, sb->length);
                    gen->writer()->writeText(content.c_str());
                }
                stringbuf_free(sb);
            }
        }
    }

    gen->writer()->closeTag("code");
}

static void cmd_verb_command(LatexProcessor* proc, Item elem) {
    // \verb|text| inline verbatim with delimiter
    // The external scanner matched the full \verb<delim>text<delim> pattern
    // The AST builder stored it as a string child
    HtmlGenerator* gen = proc->generator();

    log_debug("cmd_verb_command: CALLED");

    // Ensure we're in a paragraph (verb is inline content)
    proc->ensureParagraph();

    ElementReader elem_reader(elem);

    // Get the token string from the first child
    if (elem_reader.childCount() < 1) {
        log_warn("verb_command: no children found");
        return;
    }

    ItemReader first_child = elem_reader.childAt(0);
    if (!first_child.isString()) {
        log_warn("verb_command: first child is not a string");
        return;
    }

    const char* text = first_child.cstring();
    if (!text) {
        log_warn("verb_command: first child string is null");
        return;
    }

    log_debug("verb_command: processing text='%s'", text);

    // Parse: "\verb<delim>content<delim>" or "\verb*<delim>content<delim>"
    // Skip "\verb" (5 chars)
    if (strlen(text) < 7) {  // Minimum: \verb||
        log_warn("verb_command: token too short: %s", text);
        return;
    }

    const char* ptr = text + 5;  // After "\verb"

    // Check for starred variant (\verb*)
    bool starred = false;
    if (*ptr == '*') {
        starred = true;
        ptr++;
        log_debug("verb_command: starred variant detected");
    }

    // Next character is the delimiter
    char delim = *ptr;
    if (delim == '\0') {
        log_warn("verb_command: no delimiter after \\verb%s", starred ? "*" : "");
        return;
    }

    log_debug("verb_command: delimiter='%c'", delim);

    // Find content between delimiters
    const char* content_start = ptr + 1;
    const char* content_end = strchr(content_start, delim);

    if (!content_end) {
        log_warn("verb_command: missing closing delimiter '%c' in: %s", delim, text);
        return;
    }

    size_t content_len = content_end - content_start;

    log_debug("verb_command: content='%.*s' (len=%zu)", (int)content_len, content_start, content_len);

    // Open <code class="tt"> tag (matches LaTeX.js)
    gen->writer()->openTagRaw("code", "class=\"tt\"");

    // Output verbatim content (extract substring)
    std::string content(content_start, content_len);

    // For \verb*, replace spaces with visible space character (U+2423 OPEN BOX)
    if (starred) {
        for (size_t i = 0; i < content.length(); i++) {
            if (content[i] == ' ') {
                content.replace(i, 1, "␣");  // U+2423 OPEN BOX
            }
        }
    }

    gen->writer()->writeText(content.c_str());

    gen->writer()->closeTag("code");

    log_debug("verb_command: DONE");
}

static void cmd_verbatim(LatexProcessor* proc, Item elem) {
    // \begin{verbatim} ... \end{verbatim}
    HtmlGenerator* gen = proc->generator();

    gen->startVerbatim();

    // In verbatim mode, output text as-is without processing commands
    ElementReader elem_reader(elem);
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            gen->verbatimText(child.cstring());
        }
    }

    gen->endVerbatim();
}

// Math environment commands

static void cmd_math(LatexProcessor* proc, Item elem) {
    // Inline math: $...$  or \(...\)
    HtmlGenerator* gen = proc->generator();

    // Get LaTeX source from 'source' attribute (set during parsing)
    ElementReader reader(elem.element);
    ItemReader source_attr = reader.get_attr("source");
    const char* latex_source = nullptr;

    if (source_attr.isString()) {
        latex_source = source_attr.cstring();
    }

    if (latex_source && *latex_source) {
        gen->startInlineMathWithSource(latex_source);
        // Don't process children - the data-latex attribute has the source
        // and Radiant will render the math using the math engine
    } else {
        gen->startInlineMath();
        // Fallback: no source available, process children for HTML display
        proc->processChildren(elem);
    }
    gen->endInlineMath();
}

// Alias for inline_math (tree-sitter grammar node type)
static void cmd_inline_math(LatexProcessor* proc, Item elem) {
    cmd_math(proc, elem);
}

static void cmd_displaymath(LatexProcessor* proc, Item elem) {
    // Display math: \[...\] or $$...$$
    HtmlGenerator* gen = proc->generator();

    // Get LaTeX source from 'source' attribute (set during parsing)
    ElementReader reader(elem.element);
    ItemReader source_attr = reader.get_attr("source");
    const char* latex_source = nullptr;

    if (source_attr.isString()) {
        latex_source = source_attr.cstring();
    }

    if (latex_source && *latex_source) {
        gen->startDisplayMathWithSource(latex_source);
        // Don't process children - the data-latex attribute has the source
        // and Radiant will render the math using the math engine
    } else {
        gen->startDisplayMath();
        // Fallback: no source available, process children for HTML display
        proc->processChildren(elem);
    }
    gen->endDisplayMath();
}

// Alias for display_math (tree-sitter grammar node type)
static void cmd_display_math(LatexProcessor* proc, Item elem) {
    cmd_displaymath(proc, elem);
}

// Handler for $$ delimiter token
static void cmd_dollar_dollar(LatexProcessor* proc, Item elem) {
    // This is a standalone $$ token - typically start/end of display math
    // In tree-sitter parsing, display math should be wrapped in display_math element
    // If we get a bare $$, treat it as displaymath content marker
    (void)proc;
    (void)elem;
    // Nothing to output - $$ is just a delimiter
}

static void cmd_math_environment(LatexProcessor* proc, Item elem) {
    // Tree-sitter math_environment node for \[...\] display math
    cmd_displaymath(proc, elem);
}

static void cmd_equation(LatexProcessor* proc, Item elem) {
    // \begin{equation} ... \end{equation}
    HtmlGenerator* gen = proc->generator();

    gen->startEquation(false);  // numbered
    proc->processChildren(elem);
    gen->endEquation(false);
}

static void cmd_equation_star(LatexProcessor* proc, Item elem) {
    // \begin{equation*} ... \end{equation*}
    HtmlGenerator* gen = proc->generator();

    gen->startEquation(true);  // unnumbered
    proc->processChildren(elem);
    gen->endEquation(true);
}

// Math-mode text command
static void cmd_text(LatexProcessor* proc, Item elem) {
    // \text{...} - text inside math mode (or standalone)
    // Output as regular text (not math font)
    HtmlGenerator* gen = proc->generator();

    gen->span("text");  // Use text class for Roman font in math
    proc->processChildren(elem);
    gen->closeElement();
}

// Math-mode symbols (Greek letters, operators, etc.)
static void cmd_xi(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    proc->generator()->text("ξ");
    (void)elem;
}

static void cmd_pi(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    proc->generator()->text("π");
    (void)elem;
}

static void cmd_infty(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    proc->generator()->text("∞");
    (void)elem;
}

static void cmd_int_sym(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    proc->generator()->text("∫");
    (void)elem;
}

static void cmd_frac(LatexProcessor* proc, Item elem) {
    // \frac{numerator}{denominator}
    // Output as inline fraction: (num)/(denom) or use MathML-like structure
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    proc->ensureParagraph();

    // Get numerator and denominator from children
    int64_t count = reader.childCount();
    if (count >= 2) {
        // Open fraction container
        gen->writer()->openTagRaw("span", "class=\"frac\"");

        // Numerator
        gen->writer()->openTagRaw("span", "class=\"numer\"");
        ItemReader num = reader.childAt(0);
        proc->processNode(num.item());
        gen->writer()->closeTag("span");

        // Fraction bar is CSS
        gen->writer()->openTagRaw("span", "class=\"frac-line\"");
        gen->writer()->closeTag("span");

        // Denominator
        gen->writer()->openTagRaw("span", "class=\"denom\"");
        ItemReader denom = reader.childAt(1);
        proc->processNode(denom.item());
        gen->writer()->closeTag("span");

        gen->writer()->closeTag("span");
    } else if (count >= 1) {
        // Only numerator
        proc->processChildren(elem);
    }
}

static void cmd_superscript(LatexProcessor* proc, Item elem) {
    // Superscript: x^{y}
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->writer()->openTagRaw("sup", nullptr);
    proc->processChildren(elem);
    gen->writer()->closeTag("sup");
}

static void cmd_subscript(LatexProcessor* proc, Item elem) {
    // Subscript: x_{y}
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->writer()->openTagRaw("sub", nullptr);
    proc->processChildren(elem);
    gen->writer()->closeTag("sub");
}

static void cmd_hat(LatexProcessor* proc, Item elem) {
    // \hat{x} - hat accent (caret)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    proc->processChildren(elem);
    gen->text("̂");  // U+0302 combining circumflex accent
}

// Line break commands

static void cmd_newline(LatexProcessor* proc, Item elem) {
    // \\ or \newline - create a line break
    // In restricted horizontal mode (inside \mbox, etc.), line breaks are ignored.
    // Both trailing and leading whitespace are stripped (unskip + skip).
    if (proc->inRestrictedHMode()) {
        HtmlGenerator* gen = proc->generator();
        gen->trimTrailingWhitespace();  // unskip
        proc->setStripNextLeadingSpace(true);  // skip after
        return;
    }
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(false);
}

static void cmd_linebreak(LatexProcessor* proc, Item elem) {
    // \linebreak - optional line break hint
    // In restricted horizontal mode (inside \mbox, etc.), line breaks are ignored.
    // Both trailing and leading whitespace are stripped (unskip + skip).
    if (proc->inRestrictedHMode()) {
        HtmlGenerator* gen = proc->generator();
        gen->trimTrailingWhitespace();  // unskip
        proc->setStripNextLeadingSpace(true);  // skip after
        return;
    }
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(false);
}

static void cmd_par(LatexProcessor* proc, Item elem) {
    // \par - end current paragraph and start a new one
    // In restricted horizontal mode (inside \mbox, etc.), \par is ignored with unskip
    log_debug("cmd_par: inRestrictedHMode=%d", proc->inRestrictedHMode());
    if (proc->inRestrictedHMode()) {
        HtmlGenerator* gen = proc->generator();
        // Remove trailing whitespace (unskip) and skip following whitespace
        gen->trimTrailingWhitespace();
        proc->setStripNextLeadingSpace(true);
        // \par takes no arguments in LaTeX, but parser may attach following curly groups
        // Process any children as regular content
        proc->processChildren(elem);
        return;
    }
    // Simply close the paragraph if open - the next text will start a new one
    proc->endParagraph();
    // \par takes no arguments in LaTeX, but parser may attach following curly groups
    // Process any children as regular content
    proc->processChildren(elem);
}

static void cmd_noindent(LatexProcessor* proc, Item elem) {
    // \noindent - the next paragraph should not be indented
    // Close current paragraph if open, and set flag for next paragraph
    proc->endParagraph();
    proc->setNextParagraphIsNoindent();
    (void)elem;  // unused
}

static void cmd_gobbleO(LatexProcessor* proc, Item elem) {
    // \gobbleO - gobble whitespace and optional argument (from echo package)
    // latex.js: args.gobbleO = <[ H o? ]>, \gobbleO : -> []
    // 'H' means: add brsp (ZWS + space) after IF optional arg was consumed
    // Note: Don't unskip - the preceding space should be preserved
    // Returns empty array (no output), but adds brsp if optional arg consumed

    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);
    bool has_optional_arg = false;

    // Check if optional argument was consumed (brack_group or curly_group child)
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (strcmp(tag, "brack_group") == 0 || strcmp(tag, "curly_group") == 0) {
                has_optional_arg = true;
                break;
            }
        }
    }

    // Output ZWS + space (brsp) if optional argument was consumed
    // LaTeX.js: brsp = '\u200B ' (breakable but non-collapsible space)
    if (has_optional_arg) {
        gen->text("\u200B ");  // Zero-width space + space
    }
    // Otherwise output nothing (just gobble the space)
}

static void cmd_echoO(LatexProcessor* proc, Item elem) {
    // \echoO[optional] - from echo package for testing
    // latex.js: args.echoO = <[ H o? ]>, \echoO : (o) -> [ "-", o, "-" ]
    // Outputs "-optional-" if optional arg present, otherwise just "-"

    HtmlGenerator* gen = proc->generator();
    Pool* pool = proc->pool();
    ElementReader reader(elem);

    gen->text("-");

    // Find and output optional argument content
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (strcmp(tag, "brack_group") == 0) {
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                gen->text(str->chars);
                break;
            }
        }
    }

    gen->text("-");
}

static void cmd_echoOGO(LatexProcessor* proc, Item elem) {
    // \echoOGO[o1]{g}[o2] - from echo package for testing
    // latex.js: args.echoOGO = <[ H o? g o? ]>
    // Pattern: -optional- for optional args, +mandatory+ for mandatory args
    // For \echoOGO[o1]{g}[o2] -> outputs: -o1-+g+-o2-
    //
    // Parser behavior:
    // - First curly_group {g} is UNWRAPPED: its content becomes direct children of the command
    // - If [o1] is directly adjacent (no space), it becomes a child
    // - If [o2] is separated by space, it becomes a SIBLING

    HtmlGenerator* gen = proc->generator();
    Pool* pool = proc->pool();
    ElementReader reader(elem);

    std::vector<Item> brack_args_children;  // optional args from children (before mandatory)
    std::vector<Item> brack_args_siblings;  // optional args from siblings (after mandatory)
    std::vector<Item> curly_args;           // extra mandatory args from siblings (shouldn't normally happen)

    // Mandatory arg: the unwrapped text content of the command element
    StringBuf* mandatory_sb = stringbuf_new(pool);

    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group") == 0) {
                // Optional arg as child (before mandatory, i.e. [o1])
                brack_args_children.push_back(child.item());
            } else if (strcmp(tag, "group") == 0 || strcmp(tag, "curly_group") == 0) {
                // Should not happen for echoOGO since only curly is mandatory and gets unwrapped
                child_elem.textContent(mandatory_sb);
            } else {
                // Other element content - add to mandatory
                child_elem.textContent(mandatory_sb);
            }
        } else if (child.isString()) {
            // Text content (unwrapped from {g})
            stringbuf_append_str(mandatory_sb, child.cstring());
        }
    }

    // Then, consume sibling arguments (pattern: o? after mandatory)
    proc->consumeSiblingArgs(brack_args_siblings, curly_args);

    // Ensure we're in a paragraph before outputting content
    proc->ensureParagraph();

    // Output: -o1- +g+ -o2-

    // First optional(s) from children (use outputGroupContent to handle parbreak -> <br>)
    for (size_t i = 0; i < brack_args_children.size(); i++) {
        gen->text("-");
        proc->outputGroupContent(brack_args_children[i]);
        gen->text("-");
    }

    // Mandatory (unwrapped text content)
    String* mandatory = stringbuf_to_string(mandatory_sb);
    if (mandatory && mandatory->len > 0) {
        gen->text("+");
        gen->text(mandatory->chars);
        gen->text("+");
    }

    // Second optional(s) from siblings (use outputGroupContent to handle parbreak -> <br>)
    for (size_t i = 0; i < brack_args_siblings.size(); i++) {
        gen->text("-");
        proc->outputGroupContent(brack_args_siblings[i]);
        gen->text("-");
    }
}

static void cmd_echoGOG(LatexProcessor* proc, Item elem) {
    // \echoGOG{g1}[o]{g2} - from echo package for testing
    // latex.js: args.echoGOG = <[ H g o? g ]>
    // Pattern: +g1+-o-+g2+ where optional is only if present
    //
    // Parser behavior:
    // - First curly_group {g1} is UNWRAPPED: its content becomes direct children of the command
    // - If [o] and {g2} are directly adjacent (no space), they become children
    // - If [o] and {g2} are separated by space, they become SIBLINGS

    HtmlGenerator* gen = proc->generator();
    Pool* pool = proc->pool();
    ElementReader reader(elem);

    std::vector<Item> brack_args;  // optional args (from children and siblings)
    std::vector<Item> curly_args;  // mandatory args (from siblings only - first is unwrapped)

    // First mandatory arg: the unwrapped text content of the command element
    // Collect all non-brack_group, non-curly_group children as the first mandatory
    StringBuf* first_mandatory_sb = stringbuf_new(pool);

    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group") == 0) {
                // Optional arg as child (no space between command and [])
                brack_args.push_back(child.item());
            } else if (strcmp(tag, "group") == 0 || strcmp(tag, "curly_group") == 0) {
                // Additional mandatory arg as child (shouldn't happen with unwrapping, but handle it)
                curly_args.push_back(child.item());
            } else {
                // Other element content - add to first mandatory
                child_elem.textContent(first_mandatory_sb);
            }
        } else if (child.isString()) {
            // Text content (unwrapped from first curly_group)
            stringbuf_append_str(first_mandatory_sb, child.cstring());
        }
    }

    // Then, consume sibling arguments (optional and second mandatory)
    proc->consumeSiblingArgs(brack_args, curly_args);

    // Ensure we're in a paragraph before outputting content
    proc->ensureParagraph();

    // Pattern for echoGOG: g o? g
    // Output: +g1+ -o- +g2+ (or +g1+ +g2+ if no optional)

    // First mandatory (unwrapped text content)
    String* first_mandatory = stringbuf_to_string(first_mandatory_sb);
    if (first_mandatory && first_mandatory->len > 0) {
        gen->text("+");
        gen->text(first_mandatory->chars);
        gen->text("+");
    }

    // Optional (if present) - use outputGroupContent to handle parbreak -> <br>
    for (size_t i = 0; i < brack_args.size(); i++) {
        gen->text("-");
        proc->outputGroupContent(brack_args[i]);
        gen->text("-");
    }

    // Second mandatory (from siblings) - use outputGroupContent for consistency
    for (size_t i = 0; i < curly_args.size(); i++) {
        gen->text("+");
        proc->outputGroupContent(curly_args[i]);
        gen->text("+");
    }
}

static void cmd_newpage(LatexProcessor* proc, Item elem) {
    // \newpage
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(true);  // page break
}

// =============================================================================
// Spacing Commands
// =============================================================================

// Convert LaTeX length to pixels
// Returns pixels for valid lengths, or -1 for invalid/unsupported
static double convert_length_to_px(const char* length) {
    double value;
    char unit[16] = {0};

    if (sscanf(length, "%lf%15s", &value, unit) < 1) {
        return -1;
    }

    // Convert to pixels based on unit
    // Standard conversions: 1in = 96px, 1cm = 37.795px, 1mm = 3.7795px
    // 1pt = 1.333px, 1pc = 16px, 1em = 16px (assuming 16px base)
    if (strlen(unit) == 0 || strcmp(unit, "px") == 0) {
        return value;
    } else if (strcmp(unit, "cm") == 0) {
        return value * 37.795275591;  // 96 / 2.54
    } else if (strcmp(unit, "mm") == 0) {
        return value * 3.7795275591;
    } else if (strcmp(unit, "in") == 0) {
        return value * 96.0;
    } else if (strcmp(unit, "pt") == 0) {
        return value * 1.333333333;  // 96 / 72
    } else if (strcmp(unit, "pc") == 0) {
        return value * 16.0;  // 12pt * 1.333
    } else if (strcmp(unit, "em") == 0) {
        return value * 16.0;  // assuming 16px base font
    } else if (strcmp(unit, "ex") == 0) {
        return value * 8.0;  // roughly half of em
    }

    return -1;  // unknown unit
}

static void cmd_hspace(LatexProcessor* proc, Item elem) {
    // \hspace{length} - horizontal space
    HtmlGenerator* gen = proc->generator();

    // Extract length from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* length_str = stringbuf_to_string(sb);

    // Convert length to pixels
    double px = convert_length_to_px(length_str->chars);

    // Create inline spacer with margin-right (matches LaTeX.js behavior)
    char style[256];
    if (px >= 0) {
        // Use pixel value with margin-right
        // Use 3 decimal places to match expected output
        snprintf(style, sizeof(style), "margin-right:%.3fpx", px);
    } else {
        // Fallback to original unit if conversion failed
        snprintf(style, sizeof(style), "margin-right:%s", length_str->chars);
    }
    gen->spanWithStyle(style);
    gen->closeElement();
}

static void cmd_vspace(LatexProcessor* proc, Item elem) {
    // \vspace{length} - vertical space
    // In LaTeX, \vspace behavior depends on mode:
    // - Horizontal mode (inline): stays inline with text → <span class="vspace-inline" style="margin-bottom:...">
    // - Vertical mode (block): creates space between paragraphs → <span class="vspace" style="margin-bottom:...">
    HtmlGenerator* gen = proc->generator();

    // Extract length from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* length_str = stringbuf_to_string(sb);

    // Convert LaTeX length to pixels (approximate conversion)
    // LaTeX units: pt, cm, mm, in, em, ex
    // For now, use browser's ability to handle these units directly
    double px_value = convertLatexLengthToPixels(length_str->chars);

    // Create style with margin-bottom (not height, as vertical space affects flow)
    char style[256];
    snprintf(style, sizeof(style), "margin-bottom:%.3fpx", px_value);

    // Context-dependent output
    if (proc->inParagraph()) {
        // Inline context: output inline span within paragraph
        gen->spanWithClassAndStyle("vspace-inline", style);
        gen->closeElement();
    } else {
        // Block context: output span between paragraphs
        gen->spanWithClassAndStyle("vspace", style);
        gen->closeElement();
    }
}

static void cmd_addvspace(LatexProcessor* proc, Item elem) {
    // \addvspace{length} - add vertical space (only if needed)
    // For HTML, just treat as regular vspace
    cmd_vspace(proc, elem);
}

static void cmd_smallbreak(LatexProcessor* proc, Item elem) {
    // \smallbreak - small vertical break with paragraph break
    // Ends current paragraph and outputs span between paragraphs
    proc->endParagraph();
    HtmlGenerator* gen = proc->generator();
    gen->span("vspace smallskip");
    gen->closeElement();
}

static void cmd_medbreak(LatexProcessor* proc, Item elem) {
    // \medbreak - medium vertical break with paragraph break
    proc->endParagraph();
    HtmlGenerator* gen = proc->generator();
    gen->span("vspace medskip");
    gen->closeElement();
}

static void cmd_bigbreak(LatexProcessor* proc, Item elem) {
    // \bigbreak - big vertical break with paragraph break
    proc->endParagraph();
    HtmlGenerator* gen = proc->generator();
    gen->span("vspace bigskip");
    gen->closeElement();
}

static void cmd_marginpar(LatexProcessor* proc, Item elem) {
    // \marginpar{text} - margin note
    // 1. Output inline placeholder: <span class="mpbaseline" id="marginref-N"></span>
    // 2. Capture marginpar content and store for later output after body div

    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);

    // Enter a new group to isolate font changes from the main document
    gen->enterGroup();

    // Enter inline mode to prevent paragraph creation inside marginpar
    proc->enterInlineMode();

    // Get the marginpar content by capturing output
    gen->startCapture();
    proc->processChildren(elem);
    std::string content = gen->endCapture();

    // Exit inline mode
    proc->exitInlineMode();

    // Exit group to restore font state
    gen->exitGroup();

    // Add to margin paragraphs list and get unique ID
    int id = proc->addMarginParagraph(content);

    // Ensure we're in a paragraph before outputting the placeholder
    // This ensures the span appears inside <p> not before it
    proc->ensureParagraph();

    // Output inline placeholder at current position
    char id_attr[64];
    snprintf(id_attr, sizeof(id_attr), "mpbaseline\" id=\"marginref-%d", id);
    gen->span(id_attr);
    gen->closeElement();
}

static void cmd_index(LatexProcessor* proc, Item elem) {
    // \index{entry} - index entry
    // In HTML text mode, this is a no-op (consume argument but don't output)
    (void)proc;
    (void)elem;
}

static void cmd_glossary(LatexProcessor* proc, Item elem) {
    // \glossary{entry} - glossary entry
    // In HTML text mode, this is a no-op (consume argument but don't output)
    (void)proc;
    (void)elem;
}

static void cmd_smallskip(LatexProcessor* proc, Item elem) {
    // \smallskip - small vertical space
    // In LaTeX, skip commands have different behavior based on mode:
    // - Horizontal mode (inline): stays inline with text → <span class="vspace-inline smallskip"></span>
    // - Vertical mode (block): creates space between paragraphs → <span class="vspace smallskip"></span>
    HtmlGenerator* gen = proc->generator();

    if (proc->inParagraph()) {
        // Inline context: output inline span within paragraph
        gen->span("vspace-inline smallskip");
        gen->closeElement();
    } else {
        // Block context: output span between paragraphs
        gen->span("vspace smallskip");
        gen->closeElement();
    }
}

static void cmd_medskip(LatexProcessor* proc, Item elem) {
    // \medskip - medium vertical space
    HtmlGenerator* gen = proc->generator();

    if (proc->inParagraph()) {
        gen->span("vspace-inline medskip");
        gen->closeElement();
    } else {
        gen->span("vspace medskip");
        gen->closeElement();
    }
}

static void cmd_bigskip(LatexProcessor* proc, Item elem) {
    // \bigskip - large vertical space
    HtmlGenerator* gen = proc->generator();

    if (proc->inParagraph()) {
        gen->span("vspace-inline bigskip");
        gen->closeElement();
    } else {
        gen->span("vspace bigskip");
        gen->closeElement();
    }
}

static void cmd_vfill(LatexProcessor* proc, Item elem) {
    // \vfill - vertical fill (flexible space)
    HtmlGenerator* gen = proc->generator();
    gen->divWithClassAndStyle("vfill", "flex-grow:1");
    gen->closeElement();
}

static void cmd_hfill(LatexProcessor* proc, Item elem) {
    // \hfill - horizontal fill (flexible space)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("hfill", "flex-grow:1");
    gen->closeElement();
}

static void cmd_nolinebreak(LatexProcessor* proc, Item elem) {
    // \nolinebreak[priority] - discourage line break
    // In HTML, use non-breaking space or CSS hint
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithStyle("white-space:nowrap");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_nopagebreak(LatexProcessor* proc, Item elem) {
    // \nopagebreak[priority] - discourage page break
    // Complete no-op: no page concept in HTML text mode
    (void)proc;
    (void)elem;
}

static void cmd_pagebreak(LatexProcessor* proc, Item elem) {
    // \pagebreak[priority] - encourage page break
    // Complete no-op: no page concept in HTML text mode
    (void)proc;
    (void)elem;
}

static void cmd_clearpage(LatexProcessor* proc, Item elem) {
    // \clearpage - end page and flush floats
    HtmlGenerator* gen = proc->generator();
    gen->divWithClassAndStyle("clearpage", "clear:both;page-break-after:always");
    gen->closeElement();
}

static void cmd_cleardoublepage(LatexProcessor* proc, Item elem) {
    // \cleardoublepage - clear to next odd page
    HtmlGenerator* gen = proc->generator();
    gen->divWithClassAndStyle("cleardoublepage", "clear:both;page-break-after:always");
    gen->closeElement();
}

static void cmd_enlargethispage(LatexProcessor* proc, Item elem) {
    // \enlargethispage{length} - enlarge current page
    // In HTML, this is a no-op (no page concept)
    (void)proc;
    (void)elem;
}

static void cmd_negthinspace(LatexProcessor* proc, Item elem) {
    // \negthinspace or \! - negative thin space
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("negthinspace");
    gen->closeElement();
}

static void cmd_thinspace(LatexProcessor* proc, Item elem) {
    // \thinspace or \, - thin space (1/6 em)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\u2009");  // U+2009 thin space
}

static void cmd_enspace(LatexProcessor* proc, Item elem) {
    // \enspace - en space (1/2 em)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\u2002");  // U+2002 en space
}

static void cmd_quad(LatexProcessor* proc, Item elem) {
    // \quad - 1 em space
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\u2003");  // U+2003 em space
}

static void cmd_qquad(LatexProcessor* proc, Item elem) {
    // \qquad - 2 em space
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\u2003\u2003");  // two em spaces
}

// =============================================================================
// Box Commands
// =============================================================================

// Helper function for box commands - matches latex.js _box pattern
// Creates structure: <span class="classes"><span>content</span></span>
// Content is processed in restricted horizontal mode where:
//   - \\ and \newline are ignored (no linebreak output)
//   - \par and paragraph breaks become spaces
static void _box(LatexProcessor* proc, Item elem, const char* classes,
                 const char* width = nullptr, const char* pos = nullptr) {
    HtmlGenerator* gen = proc->generator();

    // Build the class string based on position parameter
    std::string box_classes = classes ? classes : "hbox";

    if (width && pos) {
        // \makebox[width][pos]{text} handling
        switch (pos[0]) {
        case 's': box_classes += " stretch"; break;
        case 'c': box_classes += " clap"; break;
        case 'l': box_classes += " rlap"; break;
        case 'r': box_classes += " llap"; break;
        }
    }

    // Outer span with hbox class
    gen->span(box_classes.c_str());
    // Inner span for content (pass nullptr to get <span> without class)
    gen->span(nullptr);

    // Process content in restricted horizontal mode AND inline mode
    // Inline mode prevents paragraph creation inside the box
    proc->enterRestrictedHMode();
    proc->enterInlineMode();
    proc->processChildren(elem);
    proc->exitInlineMode();
    proc->exitRestrictedHMode();

    gen->closeElement(); // close inner span
    gen->closeElement(); // close outer span
}

static void cmd_mbox(LatexProcessor* proc, Item elem) {
    // \mbox{text} - make box (prevent line breaking)
    // Matches: \mbox : (txt) -> @makebox undefined, undefined, undefined, txt
    proc->ensureParagraph();
    _box(proc, elem, "hbox");
}

static void cmd_fbox(LatexProcessor* proc, Item elem) {
    // \fbox{text} - framed box
    // Special handling: if content is a single parbox/minipage/makebox, add frame class to it
    // Otherwise, wrap in hbox frame

    ElementReader reader(elem);

    // Check if content is a single box command (parbox, minipage, makebox)
    std::vector<Item> children;
    for (int64_t i = 0; i < reader.childCount(); i++) {
        ItemReader child = reader.childAt(i);
        TypeId type = get_type_id(child.item());
        if (type == LMD_TYPE_STRING) {
            String* str = child.asString();
            // Skip whitespace-only strings
            bool all_whitespace = true;
            for (size_t j = 0; j < str->len; j++) {
                if (!isspace((unsigned char)str->chars[j])) {
                    all_whitespace = false;
                    break;
                }
            }
            if (!all_whitespace) {
                children.push_back(child.item());
            }
        } else {
            children.push_back(child.item());
        }
    }

    // If exactly one child and it's a box element (parbox, minipage, makebox), add frame class to it
    if (children.size() == 1 && get_type_id(children[0]) == LMD_TYPE_ELEMENT) {
        ElementReader child_elem(children[0]);
        const char* elem_name = child_elem.tagName();

        if (strcmp(elem_name, "parbox") == 0 || strcmp(elem_name, "minipage") == 0 ||
            strcmp(elem_name, "makebox") == 0) {
            // Set flag in processor that next box should have frame class
            proc->set_next_box_frame(true);
            proc->processNode(children[0]);
            proc->set_next_box_frame(false);
            return;
        }
    }

    // Default: wrap in hbox frame
    _box(proc, elem, "hbox frame");
}

static void cmd_framebox(LatexProcessor* proc, Item elem) {
    // \framebox[width][pos]{text} - framed box with options
    // Same as makebox but with frame class
    // latex.js: @_box width, pos, txt, "hbox frame"
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();

    // Default values
    std::string width = "";
    std::string pos = "";

    // Collect bracket groups and content children
    std::vector<std::string> brack_params;
    std::vector<Item> content_items;

    for (int64_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group") == 0) {
                // Optional parameter
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                brack_params.push_back(std::string(str->chars, str->len));
            } else {
                // Content elements (curly_group, etc.)
                content_items.push_back(child.item());
            }
        } else if (child.isString()) {
            // Text content
            content_items.push_back(child.item());
        }
    }

    // Parse bracket parameters
    if (brack_params.size() >= 1) {
        width = brack_params[0];  // Width
    }
    if (brack_params.size() >= 2) {
        pos = brack_params[1];  // Position: l, c, r, s
    }

    // Build CSS classes - include frame
    std::string classes = "hbox frame";

    // Position classes
    if (!pos.empty() && !width.empty()) {
        switch (pos[0]) {
        case 's': classes += " stretch"; break;
        case 'c': classes += " clap"; break;
        case 'l': classes += " rlap"; break;
        case 'r': classes += " llap"; break;
        }
    }

    proc->ensureParagraph();

    // Build style attribute
    std::stringstream style;
    style << std::fixed << std::setprecision(3);
    if (!width.empty()) {
        double width_px = convert_length_to_px(width.c_str());
        if (width_px >= 0) {
            style << "width:" << width_px << "px";
        }
    }

    // Generate HTML
    std::stringstream attrs;
    attrs << "class=\"" << classes << "\"";
    if (!style.str().empty()) {
        attrs << " style=\"" << style.str() << "\"";
    }

    gen->writer()->openTagRaw("span", attrs.str().c_str());
    gen->span(nullptr);  // inner span for content

    // Process content in restricted horizontal and inline mode
    proc->enterRestrictedHMode();
    proc->enterInlineMode();
    for (const Item& item : content_items) {
        proc->processNode(item);
    }
    proc->exitInlineMode();
    proc->exitRestrictedHMode();

    gen->closeElement();  // close inner span
    gen->closeElement();  // close outer span
}

static void cmd_frame(LatexProcessor* proc, Item elem) {
    // \frame{text} - simple frame
    _box(proc, elem, "hbox frame");
}

static void cmd_parbox(LatexProcessor* proc, Item elem) {
    // \parbox[pos][height][inner-pos]{width}{text} - paragraph box
    // LaTeX.js: args.\parbox = <[ H i? l? i? l g ]>
    // pos: c,t,b (default: c)
    // height: optional length
    // inner-pos: t,c,b,s (default: same as pos)
    //
    // Parser output: <parbox [brack_group]* string string [element*]>
    // First string is width, second string/elements are content
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();

    // Default values
    std::string pos = "c";
    std::string inner_pos = "";
    std::string width = "";
    std::string height = "";

    // Collect bracket groups and text children
    std::vector<std::string> brack_params;
    std::vector<Item> content_items;
    bool found_width = false;

    for (int64_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group") == 0) {
                // Optional parameter
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                brack_params.push_back(std::string(str->chars, str->len));
            } else {
                // Content elements
                if (found_width) {
                    content_items.push_back(child.item());
                }
            }
        } else if (child.isString()) {
            String* str = child.asString();
            if (!found_width && str && str->len > 0) {
                // First string is width
                width = std::string(str->chars, str->len);
                found_width = true;
            } else if (found_width) {
                // Subsequent strings are content
                content_items.push_back(child.item());
            }
        }
    }

    // Parse bracket parameters
    if (brack_params.size() >= 1) {
        pos = brack_params[0];  // Position: t, c, b
    }
    if (brack_params.size() >= 2) {
        height = brack_params[1];  // Height
    }
    if (brack_params.size() >= 3) {
        inner_pos = brack_params[2];  // Inner position: t, c, b, s
    }

    // Default inner_pos to pos if not specified
    if (inner_pos.empty()) {
        inner_pos = pos;
    }

    // Build CSS classes
    std::string classes = "parbox";

    if (!height.empty()) {
        classes += " pbh";
    }

    // Position classes
    if (pos == "c") classes += " p-c";
    else if (pos == "t") classes += " p-t";
    else if (pos == "b") classes += " p-b";

    // Inner position classes
    if (inner_pos == "s") classes += " stretch";
    else if (inner_pos == "c") classes += " p-cc";
    else if (inner_pos == "t") classes += " p-ct";
    else if (inner_pos == "b") classes += " p-cb";

    // Add frame class if requested by fbox
    if (proc->get_next_box_frame()) {
        classes += " frame";
    }

    // Build style attribute
    std::stringstream style;
    style << std::fixed << std::setprecision(3);  // 3 decimal places for pixel values
    if (!width.empty()) {
        // Convert LaTeX length to pixels
        double width_px = convert_length_to_px(width.c_str());
        if (width_px >= 0) {
            style << "width:" << width_px << "px;";
        } else {
            style << "width:" << width << ";";  // fallback to original value
        }
    }
    if (!height.empty()) {
        // Convert LaTeX length to pixels
        double height_px = convert_length_to_px(height.c_str());
        if (height_px >= 0) {
            style << "height:" << height_px << "px;";
        } else {
            style << "height:" << height << ";";  // fallback to original value
        }
    }

    // Generate HTML
    proc->ensureParagraph();
    std::stringstream attrs;
    attrs << "class=\"" << classes << "\"";
    if (!style.str().empty()) {
        attrs << " style=\"" << style.str() << "\"";
    }

    gen->writer()->openTagRaw("span", attrs.str().c_str());
    gen->writer()->openTag("span");

    // Process content items
    for (const Item& item : content_items) {
        proc->processNode(item);
    }

    gen->writer()->closeTag("span");
    gen->writer()->closeTag("span");
}

static void cmd_makebox(LatexProcessor* proc, Item elem) {
    // \makebox[width][pos]{text} - make box with size
    // latex.js: @_box width, pos, txt, "hbox"
    // width: optional length
    // pos: l (left/flush-right), c (center), r (right/flush-left), s (spread)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();

    // Default values
    std::string width = "";
    std::string pos = "";

    // Collect bracket groups and content children
    std::vector<std::string> brack_params;
    std::vector<Item> content_items;

    for (int64_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group") == 0) {
                // Optional parameter
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                brack_params.push_back(std::string(str->chars, str->len));
            } else {
                // Content elements (curly_group, etc.)
                content_items.push_back(child.item());
            }
        } else if (child.isString()) {
            // Text content
            content_items.push_back(child.item());
        }
    }

    // Parse bracket parameters
    if (brack_params.size() >= 1) {
        width = brack_params[0];  // Width
    }
    if (brack_params.size() >= 2) {
        pos = brack_params[1];  // Position: l, c, r, s
    }

    // Build CSS classes
    std::string classes = "hbox";

    // Position classes
    // l = left (content appears at left, box extends right = rlap)
    // c = center (content centered, box extends both ways = clap)
    // r = right (content at right, box extends left = llap)
    if (!pos.empty() && !width.empty()) {
        switch (pos[0]) {
        case 's': classes += " stretch"; break;
        case 'c': classes += " clap"; break;
        case 'l': classes += " rlap"; break;
        case 'r': classes += " llap"; break;
        }
    }

    // Add frame class if requested by \framebox
    if (proc->get_next_box_frame()) {
        classes += " frame";
    }

    proc->ensureParagraph();

    // Build style attribute
    std::stringstream style;
    style << std::fixed << std::setprecision(3);  // 3 decimal places
    if (!width.empty()) {
        double width_px = convert_length_to_px(width.c_str());
        if (width_px >= 0) {
            style << "width:" << width_px << "px";
        }
    }

    // Generate HTML: <span class="classes" style="width:..."><span>content</span></span>
    std::stringstream attrs;
    attrs << "class=\"" << classes << "\"";
    if (!style.str().empty()) {
        attrs << " style=\"" << style.str() << "\"";
    }

    gen->writer()->openTagRaw("span", attrs.str().c_str());
    gen->span(nullptr);  // inner span for content

    // Process content in restricted horizontal and inline mode
    proc->enterRestrictedHMode();
    proc->enterInlineMode();
    for (const Item& item : content_items) {
        proc->processNode(item);
    }
    proc->exitInlineMode();
    proc->exitRestrictedHMode();

    gen->closeElement();  // close inner span
    gen->closeElement();  // close outer span
}

static void cmd_phantom(LatexProcessor* proc, Item elem) {
    // \phantom{text} - invisible box
    // latex.js: [ @g.create @g.inline, txt, "phantom hbox" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("phantom hbox");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_hphantom(LatexProcessor* proc, Item elem) {
    // \hphantom{text} - horizontal phantom (width only)
    // latex.js: [ @g.create @g.inline, txt, "phantom hbox smash" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("phantom hbox smash");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_vphantom(LatexProcessor* proc, Item elem) {
    // \vphantom{text} - vertical phantom (height/depth only)
    // latex.js: [ @g.create @g.inline, txt, "phantom hbox rlap" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("phantom hbox rlap");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_smash(LatexProcessor* proc, Item elem) {
    // \smash[tb]{text} - smash height/depth
    // latex.js: [ @g.create @g.inline, txt, "hbox smash" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("hbox smash");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_clap(LatexProcessor* proc, Item elem) {
    // \clap{text} - centered lap (zero width, centered)
    // latex.js: [ @g.create @g.inline, txt, "hbox clap" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("hbox clap");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_llap(LatexProcessor* proc, Item elem) {
    // \llap{text} - left lap (zero width, right-aligned)
    // latex.js: [ @g.create @g.inline, txt, "hbox llap" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("hbox llap");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_rlap(LatexProcessor* proc, Item elem) {
    // \rlap{text} - right lap (zero width, left-aligned)
    // latex.js: [ @g.create @g.inline, txt, "hbox rlap" ]
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("hbox rlap");
    proc->processChildren(elem);
    gen->closeElement();
}

// =============================================================================
// Alignment Declaration Commands
// =============================================================================

static void cmd_centering(LatexProcessor* proc, Item elem) {
    // \centering - center alignment (declaration)
    // This is a paragraph alignment declaration that affects the current paragraph
    // (if open) or following paragraphs in the current group scope.
    // Per LaTeX semantics, alignment is applied at paragraph END, not start.
    // We set the alignment but do NOT end the paragraph.
    (void)elem;
    proc->setNextParagraphAlignment("centering");
}

static void cmd_raggedright(LatexProcessor* proc, Item elem) {
    // \raggedright - ragged right (left-aligned, declaration)
    // This is a paragraph alignment declaration
    (void)elem;
    proc->setNextParagraphAlignment("raggedright");
}

static void cmd_raggedleft(LatexProcessor* proc, Item elem) {
    // \raggedleft - ragged left (right-aligned, declaration)
    // This is a paragraph alignment declaration
    (void)elem;
    proc->setNextParagraphAlignment("raggedleft");
}

// =============================================================================
// Document Metadata Commands
// =============================================================================

static void cmd_author(LatexProcessor* proc, Item elem) {
    // \author{name} - store author content for \maketitle
    // LaTeX.js stores these and renders them in \maketitle
    proc->storeAuthor(elem);
    // Don't output anything - \maketitle will render
}

static void cmd_title(LatexProcessor* proc, Item elem) {
    // \title{text} - store title content for \maketitle
    // LaTeX.js stores these and renders them in \maketitle
    proc->storeTitle(elem);
    // Don't output anything - \maketitle will render
}

static void cmd_date(LatexProcessor* proc, Item elem) {
    // \date{text} - store date content for \maketitle
    // LaTeX.js stores these and renders them in \maketitle
    proc->storeDate(elem);
    // Don't output anything - \maketitle will render
}

static void cmd_thanks(LatexProcessor* proc, Item elem) {
    // \thanks{text} - thanks footnote in title
    HtmlGenerator* gen = proc->generator();
    gen->spanWithClassAndStyle("thanks", "vertical-align:super;font-size:smaller");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_maketitle(LatexProcessor* proc, Item elem) {
    // \maketitle - generate title block matching LaTeX.js structure
    // Structure: div.list.center containing:
    //   - vspace 2em
    //   - div.title (inline content only, no <p> tags)
    //   - vspace 1.5em
    //   - div.author (inline content only)
    //   - vspace 1em
    //   - div.date (inline content only)
    //   - vspace 1.5em
    (void)elem;
    HtmlGenerator* gen = proc->generator();

    // Close any open paragraph before title block
    proc->closeParagraphIfOpen();

    // Start the centered title block container
    // Use raw HTML since we need class="list center" (two classes)
    gen->writer()->writeRawHtml("<div class=\"list center\">");
    gen->enterGroup();  // Track element depth

    // Vertical space before title (2em)
    gen->spanWithClassAndStyle("vspace", "margin-bottom:2em");
    gen->closeElement();

    // Title - enter inline mode to suppress <p> creation
    if (proc->hasTitle()) {
        gen->div("title");
        proc->enterInlineMode();
        proc->processChildren(proc->getStoredTitle());
        proc->exitInlineMode();
        gen->closeElement();
    }

    // Vertical space after title (1.5em)
    gen->spanWithClassAndStyle("vspace", "margin-bottom:1.5em");
    gen->closeElement();

    // Author - enter inline mode to suppress <p> creation
    if (proc->hasAuthor()) {
        gen->div("author");
        proc->enterInlineMode();
        proc->processChildren(proc->getStoredAuthor());
        proc->exitInlineMode();
        gen->closeElement();
    }

    // Vertical space after author (1em)
    gen->spanWithClassAndStyle("vspace", "margin-bottom:1em");
    gen->closeElement();

    // Date (or today's date if not set) - enter inline mode
    gen->div("date");
    proc->enterInlineMode();
    if (proc->hasDate()) {
        proc->processChildren(proc->getStoredDate());
    } else {
        // Default to current date like LaTeX.js \today
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char date_buf[64];
        strftime(date_buf, sizeof(date_buf), "%B %d, %Y", tm_info);
        gen->text(date_buf);
    }
    proc->exitInlineMode();
    gen->closeElement();

    // Vertical space after date (1.5em)
    gen->spanWithClassAndStyle("vspace", "margin-bottom:1.5em");
    gen->closeElement();

    // Close the container
    gen->writer()->writeRawHtml("</div>");
    gen->exitGroup();
}

// =============================================================================
// Label and reference commands
// =============================================================================

static void cmd_label(LatexProcessor* proc, Item elem) {
    // \label{name} - register a label with current counter context
    // latex.js: label associates the label name with the current counter context
    // The anchor ID is already output by the section/counter command
    HtmlGenerator* gen = proc->generator();

    // Extract label name from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* label_str = stringbuf_to_string(sb);

    // Register the label with current context
    gen->setLabel(label_str->chars);

    // NOTE: Do NOT output an anchor here - the anchor is already in the
    // section heading or counter element. The label just associates the
    // label name with that anchor.
}

static void cmd_ref(LatexProcessor* proc, Item elem) {
    // \ref{name}
    HtmlGenerator* gen = proc->generator();

    // Extract reference name
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* ref_str = stringbuf_to_string(sb);

    // Create reference link
    gen->ref(ref_str->chars);
}

static void cmd_pageref(LatexProcessor* proc, Item elem) {
    // \pageref{name}
    HtmlGenerator* gen = proc->generator();

    // Extract reference name
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* ref_str = stringbuf_to_string(sb);

    // Create page reference
    gen->pageref(ref_str->chars);
}

// Hyperlink commands

static void cmd_url(LatexProcessor* proc, Item elem) {
    // \url{http://...}
    // Note: Tree-sitter doesn't extract URL text properly yet
    // For now, just skip processing (URL text is lost in parse tree)
    HtmlGenerator* gen = proc->generator();

    // TODO: Need to fix Tree-sitter LaTeX parser to capture URL text content
    // For now, output a placeholder
    gen->text("[URL]");
}

static void cmd_href(LatexProcessor* proc, Item elem) {
    // \href{url}{text}
    HtmlGenerator* gen = proc->generator();

    // Extract URL and text (two children)
    ElementReader elem_reader(elem);

    if (elem_reader.childCount() >= 2) {
        Pool* pool = proc->pool();

        // First child: URL
        ItemReader url_child = elem_reader.childAt(0);
        StringBuf* url_sb = stringbuf_new(pool);
        if (url_child.isString()) {
            stringbuf_append_str(url_sb, url_child.cstring());
        } else if (url_child.isElement()) {
            ElementReader(url_child.item()).textContent(url_sb);
        }
        String* url_str = stringbuf_to_string(url_sb);

        // Second child: text
        ItemReader text_child = elem_reader.childAt(1);
        StringBuf* text_sb = stringbuf_new(pool);
        if (text_child.isString()) {
            stringbuf_append_str(text_sb, text_child.cstring());
        } else if (text_child.isElement()) {
            ElementReader(text_child.item()).textContent(text_sb);
        }
        String* text_str = stringbuf_to_string(text_sb);

        gen->hyperlink(url_str->chars, text_str->chars);
    }
}

// Footnote command

static void cmd_footnote(LatexProcessor* proc, Item elem) {
    // \footnote{text}
    HtmlGenerator* gen = proc->generator();

    // Extract footnote text
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* text_str = stringbuf_to_string(sb);

    // Create footnote marker
    gen->footnote(text_str->chars);
}

// =============================================================================
// Table Commands
// =============================================================================

static void cmd_tabular(LatexProcessor* proc, Item elem) {
    // \begin{tabular}{column_spec}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Find column specification (first curly_group child)
    std::string column_spec;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* spec_str = stringbuf_to_string(sb);
                column_spec = spec_str->chars;
                break;
            }
        }
    }

    // Start table
    gen->startTabular(column_spec.c_str());

    // Process table content (rows and cells)
    proc->processChildren(elem);

    // End table
    gen->endTabular();
}

static void cmd_hline(LatexProcessor* proc, Item elem) {
    // \hline - horizontal line in table
    HtmlGenerator* gen = proc->generator();

    // Insert a special row with hline class
    gen->startRow();
    gen->startCell();
    gen->writer()->writeAttribute("class", "hline");
    gen->writer()->writeAttribute("colspan", "100");
    gen->endCell();
    gen->endRow();
}

static void cmd_multicolumn(LatexProcessor* proc, Item elem) {
    // \multicolumn{n}{align}{content}
    // Parser gives us: {"$":"multicolumn", "_":["3", "c", "Title"]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract arguments - they come as direct text children
    std::vector<std::string> args;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            // Direct string argument
            String* str = (String*)child.item().string_ptr;
            // Trim whitespace
            std::string arg(str->chars);
            // Remove leading/trailing whitespace
            size_t start = arg.find_first_not_of(" \t\n\r");
            size_t end = arg.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                args.push_back(arg.substr(start, end - start + 1));
            }
        }
    }

    if (args.size() < 3) {
        log_error("\\multicolumn requires 3 arguments, got %zu", args.size());
        return;
    }

    // Parse arguments
    int colspan = atoi(args[0].c_str());
    const char* align = args[1].c_str();

    // Start cell with colspan
    gen->startCell(align);
    gen->writer()->writeAttribute("colspan", args[0].c_str());

    // Output content (third argument)
    gen->text(args[2].c_str());

    gen->endCell();
}

static void cmd_figure(LatexProcessor* proc, Item elem) {
    // \begin{figure}[position]
    // Parser gives: {"$":"figure", "_":[...children...]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract position from first bracket_group if present
    const char* position = nullptr;
    auto iter = elem_reader.children();
    ItemReader child;
    if (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "bracket_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* pos_str = stringbuf_to_string(sb);
                position = pos_str->chars;
            }
        }
    }

    gen->startFigure(position);
    proc->processChildren(elem);
    gen->endFigure();
}

static void cmd_table_float(LatexProcessor* proc, Item elem) {
    // \begin{table}[position]
    // Note: This is the float environment, not the tabular environment
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract position from first bracket_group if present
    const char* position = nullptr;
    auto iter = elem_reader.children();
    ItemReader child;
    if (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "bracket_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* pos_str = stringbuf_to_string(sb);
                position = pos_str->chars;
            }
        }
    }

    // Use startTable/endTable for table float environment
    gen->startTable(position);
    proc->processChildren(elem);
    gen->endTable();
}

static void cmd_caption(LatexProcessor* proc, Item elem) {
    // \caption{text}
    // Parser produces: {"$":"caption", "_":["caption text"]} (simplified)
    // Or: {"$":"caption", "_":[<curly_group>, ...]} (old format)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    gen->startCaption();

    // Extract caption text from children (string or curly_group)
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            // Direct string child - this is the caption text
            String* str = (String*)child.item().string_ptr;
            gen->text(str->chars);
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* text_str = stringbuf_to_string(sb);
                gen->text(text_str->chars);
            } else {
                // Process other element children
                proc->processNode(child.item());
            }
        }
    }

    gen->endCaption();
}

static void cmd_includegraphics(LatexProcessor* proc, Item elem) {
    // \includegraphics[options]{filename}
    // Parser produces: {"$":"includegraphics", "_":["filename"]} (simplified)
    // Or with options: {"$":"includegraphics", "_":[<brack_group>, "filename"]}
    // brack_group may contain direct string child like "width=5cm" or structured key_value_pairs
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    const char* filename = nullptr;
    Pool* pool = proc->pool();
    StringBuf* options_sb = stringbuf_new(pool);

    // Extract filename and options
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            // Direct string child - this is the filename
            String* str = (String*)child.item().string_ptr;
            filename = str->chars;
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group_path") == 0) {
                // curly_group_path contains a STRING child with the filename
                auto path_iter = child_elem.children();
                ItemReader path_child;
                while (path_iter.next(&path_child)) {
                    if (path_child.getType() == LMD_TYPE_STRING) {
                        String* str = (String*)path_child.item().string_ptr;
                        filename = str->chars;
                        break;
                    }
                }
            } else if (strcmp(tag, "brack_group") == 0 ||
                       strcmp(tag, "brack_group_key_value") == 0 ||
                       strcmp(tag, "bracket_group") == 0) {
                // Options bracket - could be direct string or structured key-value pairs
                auto kv_iter = child_elem.children();
                ItemReader kv_child;
                bool first = true;
                while (kv_iter.next(&kv_child)) {
                    if (kv_child.getType() == LMD_TYPE_STRING) {
                        // Direct string like "width=5cm" or "width=5cm,height=3cm"
                        String* str = (String*)kv_child.item().string_ptr;
                        if (!first) stringbuf_append_char(options_sb, ',');
                        stringbuf_append_str(options_sb, str->chars);
                        first = false;
                    } else if (kv_child.getType() == LMD_TYPE_ELEMENT) {
                        ElementReader kv_elem(kv_child.item());
                        if (strcmp(kv_elem.tagName(), "key_value_pair") == 0) {
                            // Extract key and value from structured pair
                            std::string key, value;
                            auto pair_iter = kv_elem.children();
                            ItemReader pair_child;
                            while (pair_iter.next(&pair_child)) {
                                if (pair_child.getType() == LMD_TYPE_STRING) {
                                    if (key.empty()) {
                                        String* str = (String*)pair_child.item().string_ptr;
                                        key = str->chars;
                                    }
                                } else if (pair_child.getType() == LMD_TYPE_ELEMENT) {
                                    ElementReader value_elem(pair_child.item());
                                    if (strcmp(value_elem.tagName(), "value") == 0) {
                                        StringBuf* val_sb = stringbuf_new(pool);
                                        value_elem.textContent(val_sb);
                                        String* val_str = stringbuf_to_string(val_sb);
                                        value = val_str->chars;
                                    }
                                }
                            }

                            // Add to options string
                            if (!key.empty() && !value.empty()) {
                                if (!first) stringbuf_append_str(options_sb, ",");
                                stringbuf_append_str(options_sb, key.c_str());
                                stringbuf_append_str(options_sb, "=");
                                stringbuf_append_str(options_sb, value.c_str());
                                first = false;
                            }
                        }
                    }
                }
            }
        }
    }

    const char* options = nullptr;
    if (options_sb->length > 0) {
        String* options_str = stringbuf_to_string(options_sb);
        options = options_str->chars;
    }

    if (filename) {
        gen->includegraphics(filename, options);
    }
}

// =============================================================================
// Picture Environment - SVG graphics rendering
// =============================================================================

// Picture environment state - per-processor instance
static thread_local PictureContext g_picture_ctx;
static thread_local PictureRenderer* g_picture_renderer = nullptr;

// Global unitlength in pixels (default: 1pt = 1.333px)
// This is set by \setlength{\unitlength}{...} and used when starting picture environments
static thread_local double g_unitlength_px = 1.333;  // 1pt default

// Helper: Parse coordinate pair from string like "(60,50)" or "(60, 50)"
// Returns pointer to character after closing paren, or nullptr on failure
static const char* parsePicCoordAdvance(const char* str, double* x, double* y) {
    if (!str || !x || !y) return nullptr;

    // skip leading whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n')) str++;
    if (*str != '(') return nullptr;
    str++;

    char* end;
    *x = strtod(str, &end);
    if (end == str) return nullptr;
    str = end;

    // skip comma and whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == ',')) str++;

    *y = strtod(str, &end);
    if (end == str) return nullptr;
    str = end;

    // skip to closing paren
    while (*str && *str != ')') str++;
    if (*str == ')') str++;

    return str;
}

static bool parsePicCoord(const char* str, double* x, double* y) {
    return parsePicCoordAdvance(str, x, y) != nullptr;
}

// Forward declaration for cmd_picture (used by cmd_begin)
static void cmd_picture(LatexProcessor* proc, Item elem);

// Structure to hold parsed picture items for sequential processing
struct PictureItem {
    enum Type { TEXT, PUT, LINE, VECTOR, CIRCLE, CIRCLE_FILLED, OVAL, QBEZIER,
                MULTIPUT, THICKLINES, THINLINES, LINETHICKNESS, CURLY_GROUP, BRACK_GROUP, UNKNOWN };
    Type type;
    std::string text;
    Item elem;

    PictureItem() : type(UNKNOWN), elem(ItemNull) {}
    PictureItem(Type t, const std::string& txt = "") : type(t), text(txt), elem(ItemNull) {}
    PictureItem(Type t, Item e) : type(t), elem(e) {}
};

// Flatten picture children into a sequential list
static void flattenPictureChildren(Item elem, std::vector<PictureItem>& items, Pool* pool) {
    ElementReader elem_reader(elem);

    auto iter = elem_reader.children();
    ItemReader child;

    while (iter.next(&child)) {
        TypeId type = child.getType();

        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child.item().string_ptr;
            items.push_back(PictureItem(PictureItem::TEXT, str->chars));
            continue;
        }

        if (type != LMD_TYPE_ELEMENT) continue;

        ElementReader child_elem(child.item());
        const char* tag = child_elem.tagName();

        if (!tag) continue;

        if (strcmp(tag, "paragraph") == 0) {
            // Recurse into paragraphs
            flattenPictureChildren(child.item(), items, pool);
        }
        else if (strcmp(tag, "put") == 0) {
            items.push_back(PictureItem(PictureItem::PUT, child.item()));
        }
        else if (strcmp(tag, "line") == 0) {
            items.push_back(PictureItem(PictureItem::LINE, child.item()));
        }
        else if (strcmp(tag, "vector") == 0) {
            items.push_back(PictureItem(PictureItem::VECTOR, child.item()));
        }
        else if (strcmp(tag, "circle") == 0) {
            items.push_back(PictureItem(PictureItem::CIRCLE, child.item()));
        }
        else if (strcmp(tag, "circle*") == 0) {
            items.push_back(PictureItem(PictureItem::CIRCLE_FILLED, child.item()));
        }
        else if (strcmp(tag, "oval") == 0) {
            items.push_back(PictureItem(PictureItem::OVAL, child.item()));
        }
        else if (strcmp(tag, "qbezier") == 0) {
            items.push_back(PictureItem(PictureItem::QBEZIER, child.item()));
        }
        else if (strcmp(tag, "multiput") == 0) {
            items.push_back(PictureItem(PictureItem::MULTIPUT, child.item()));
        }
        else if (strcmp(tag, "thicklines") == 0) {
            items.push_back(PictureItem(PictureItem::THICKLINES));
        }
        else if (strcmp(tag, "thinlines") == 0) {
            items.push_back(PictureItem(PictureItem::THINLINES));
        }
        else if (strcmp(tag, "linethickness") == 0) {
            items.push_back(PictureItem(PictureItem::LINETHICKNESS, child.item()));
        }
        else if (strcmp(tag, "curly_group") == 0) {
            items.push_back(PictureItem(PictureItem::CURLY_GROUP, child.item()));
        }
        else if (strcmp(tag, "brack_group") == 0 || strcmp(tag, "bracket_group") == 0) {
            items.push_back(PictureItem(PictureItem::BRACK_GROUP, child.item()));
        }
        else {
            log_debug("picture flatten: unknown '%s'", tag);
            items.push_back(PictureItem(PictureItem::UNKNOWN, child.item()));
        }
    }
}

// Process flattened picture items
static void processPictureItems(LatexProcessor* proc, std::vector<PictureItem>& items) {
    Pool* pool = proc->pool();
    size_t i = 0;

    while (i < items.size()) {
        PictureItem& item = items[i];

        switch (item.type) {
            case PictureItem::THICKLINES:
                if (g_picture_renderer) g_picture_renderer->thicklines();
                i++;
                break;

            case PictureItem::THINLINES:
                if (g_picture_renderer) g_picture_renderer->thinlines();
                i++;
                break;

            case PictureItem::LINETHICKNESS: {
                // \linethickness{value}
                // The value is in the element's children or next curly_group
                double thickness = 0.4;  // default in pt

                if (get_type_id(item.elem) == LMD_TYPE_ELEMENT) {
                    ElementReader lt_elem(item.elem);
                    StringBuf* sb = stringbuf_new(pool);
                    lt_elem.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    if (str && strlen(str->chars) > 0) {
                        char* end;
                        thickness = strtod(str->chars, &end);
                    }
                }

                // Also check for next curly_group
                if (thickness <= 0 && i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    if (str && strlen(str->chars) > 0) {
                        char* end;
                        thickness = strtod(str->chars, &end);
                    }
                    i++;
                }

                if (g_picture_renderer && thickness > 0) {
                    g_picture_renderer->linethickness(thickness);
                }
                log_debug("linethickness: %.2fpt", thickness);
                i++;
                break;
            }

            case PictureItem::PUT: {
                // \put(x,y){content}
                // Next items should be: TEXT with "(x,y)", then CURLY_GROUP with content
                double x = 0, y = 0;

                // Look for coordinates in next TEXT item
                if (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                    if (parsePicCoord(items[i+1].text.c_str(), &x, &y)) {
                        i++;  // consume the coord text
                    }
                }

                // Set the current position for nested commands
                if (g_picture_renderer) {
                    g_picture_renderer->setPosition(x, y);
                }

                // Look for content in next CURLY_GROUP
                if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    // Check if curly_group contains picture commands
                    ElementReader grp(items[i+1].elem);
                    std::vector<PictureItem> nested_items;
                    flattenPictureChildren(items[i+1].elem, nested_items, pool);

                    bool has_nested_commands = false;
                    for (const auto& ni : nested_items) {
                        if (ni.type == PictureItem::LINE || ni.type == PictureItem::VECTOR ||
                            ni.type == PictureItem::CIRCLE || ni.type == PictureItem::CIRCLE_FILLED ||
                            ni.type == PictureItem::OVAL || ni.type == PictureItem::QBEZIER) {
                            has_nested_commands = true;
                            break;
                        }
                    }

                    if (has_nested_commands) {
                        // Process nested picture commands at position (x, y)
                        log_debug("put: (%.2f,%.2f) processing nested commands", x, y);
                        processPictureItems(proc, nested_items);
                    } else {
                        // Just text content
                        StringBuf* sb = stringbuf_new(pool);
                        grp.textContent(sb);
                        String* str = stringbuf_to_string(sb);
                        std::string content = str->chars;
                        if (g_picture_renderer && !content.empty()) {
                            g_picture_renderer->put(x, y, content);
                        }
                        log_debug("put: (%.2f,%.2f) text='%s'", x, y, content.c_str());
                    }
                    i++;  // consume the curly group
                }

                i++;
                break;
            }

            case PictureItem::LINE: {
                // \line(slope_x,slope_y){length}
                double sx = 0, sy = 0, len = 0;

                if (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                    if (parsePicCoord(items[i+1].text.c_str(), &sx, &sy)) {
                        i++;
                    }
                }

                if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    char* end;
                    len = strtod(str->chars, &end);
                    i++;
                }

                if (g_picture_renderer) {
                    g_picture_renderer->line(sx, sy, len);
                }
                log_debug("line: slope=(%.2f,%.2f) len=%.2f", sx, sy, len);
                i++;
                break;
            }

            case PictureItem::VECTOR: {
                // \vector(slope_x,slope_y){length}
                double sx = 0, sy = 0, len = 0;

                if (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                    if (parsePicCoord(items[i+1].text.c_str(), &sx, &sy)) {
                        i++;
                    }
                }

                if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    char* end;
                    len = strtod(str->chars, &end);
                    i++;
                }

                if (g_picture_renderer) {
                    g_picture_renderer->vector(sx, sy, len);
                }
                log_debug("vector: slope=(%.2f,%.2f) len=%.2f", sx, sy, len);
                i++;
                break;
            }

            case PictureItem::CIRCLE: {
                // \circle{diameter} or \circle*{diameter}
                // The diameter might be:
                // 1. A child of the circle element itself (when parsed as {$: "circle", _: ["10"]})
                // 2. A sibling curly_group (when parsed separately)
                double diameter = 0;
                bool filled = false;

                // First, check if the circle element has children with the diameter
                if (get_type_id(item.elem) == LMD_TYPE_ELEMENT) {
                    ElementReader circle_elem(item.elem);
                    StringBuf* sb = stringbuf_new(pool);
                    circle_elem.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    if (str && strlen(str->chars) > 0) {
                        const char* txt = str->chars;
                        // Check for * prefix
                        while (*txt == ' ' || *txt == '\t' || *txt == '\n') txt++;
                        if (*txt == '*') {
                            filled = true;
                            txt++;
                        }
                        char* end;
                        diameter = strtod(txt, &end);
                    }
                }

                // If diameter still 0, check for sibling items
                if (diameter == 0) {
                    // Check for * in next text
                    if (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                        const char* txt = items[i+1].text.c_str();
                        while (*txt == ' ' || *txt == '\t' || *txt == '\n') txt++;
                        if (*txt == '*') {
                            filled = true;
                            i++;  // consume the *
                        }
                    }

                    if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                        ElementReader grp(items[i+1].elem);
                        StringBuf* sb = stringbuf_new(pool);
                        grp.textContent(sb);
                        String* str = stringbuf_to_string(sb);
                        char* end;
                        diameter = strtod(str->chars, &end);
                        i++;
                    }
                }

                if (g_picture_renderer && diameter > 0) {
                    g_picture_renderer->circle(diameter, filled);
                }
                log_debug("circle: diameter=%.2f filled=%d", diameter, filled);
                i++;
                break;
            }

            case PictureItem::CIRCLE_FILLED: {
                // \circle*{diameter} - filled circle (parsed as separate command)
                double diameter = 0;

                // Check if the circle* element has children with the diameter
                if (get_type_id(item.elem) == LMD_TYPE_ELEMENT) {
                    ElementReader circle_elem(item.elem);
                    StringBuf* sb = stringbuf_new(pool);
                    circle_elem.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    if (str && strlen(str->chars) > 0) {
                        char* end;
                        diameter = strtod(str->chars, &end);
                    }
                }

                // If diameter still 0, check for sibling curly_group
                if (diameter == 0 && i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    char* end;
                    diameter = strtod(str->chars, &end);
                    i++;
                }

                if (g_picture_renderer && diameter > 0) {
                    g_picture_renderer->circle(diameter, true);  // always filled
                }
                log_debug("circle* (filled): diameter=%.2f", diameter);
                i++;
                break;
            }

            case PictureItem::OVAL: {
                // \oval(width,height)[portion]
                double w = 0, h = 0;
                std::string portion;

                if (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                    if (parsePicCoord(items[i+1].text.c_str(), &w, &h)) {
                        i++;
                    }
                }

                if (i + 1 < items.size() && items[i+1].type == PictureItem::BRACK_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    portion = str->chars;
                    i++;
                }

                if (g_picture_renderer && (w > 0 || h > 0)) {
                    g_picture_renderer->oval(w, h, portion);
                }
                log_debug("oval: (%.2f,%.2f) portion='%s'", w, h, portion.c_str());
                i++;
                break;
            }

            case PictureItem::QBEZIER: {
                // \qbezier(x1,y1)(cx,cy)(x2,y2)
                double x1 = 0, y1 = 0, cx = 0, cy = 0, x2 = 0, y2 = 0;
                int coords = 0;

                // Look for three coordinate pairs in following TEXT items
                while (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT && coords < 3) {
                    double tx, ty;
                    if (parsePicCoord(items[i+1].text.c_str(), &tx, &ty)) {
                        if (coords == 0) { x1 = tx; y1 = ty; }
                        else if (coords == 1) { cx = tx; cy = ty; }
                        else { x2 = tx; y2 = ty; }
                        coords++;
                        i++;
                    } else {
                        break;
                    }
                }

                if (g_picture_renderer && coords >= 3) {
                    g_picture_renderer->qbezier(x1, y1, cx, cy, x2, y2);
                }
                log_debug("qbezier: (%f,%f)-(%f,%f)-(%f,%f)", x1, y1, cx, cy, x2, y2);
                i++;
                break;
            }

            case PictureItem::MULTIPUT: {
                // \multiput(x,y)(dx,dy){n}{object}
                double x = 0, y = 0, dx = 0, dy = 0;
                int n = 0;

                // Parse coordinates from following TEXT items
                // The text might contain both coordinates like "(0,0)(1,0)"
                while (i + 1 < items.size() && items[i+1].type == PictureItem::TEXT) {
                    const char* text = items[i+1].text.c_str();

                    // Try to parse first coordinate pair
                    const char* after_first = parsePicCoordAdvance(text, &x, &y);
                    if (after_first) {
                        // Try to parse second coordinate pair from remainder
                        const char* after_second = parsePicCoordAdvance(after_first, &dx, &dy);
                        if (after_second) {
                            // Successfully parsed both coordinates
                            i++;
                            break;
                        }
                    }

                    // If we can't parse anything, stop
                    if (after_first == nullptr) break;
                    i++;
                }

                // Get n from first curly group
                if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    ElementReader grp(items[i+1].elem);
                    StringBuf* sb = stringbuf_new(pool);
                    grp.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    n = atoi(str->chars);
                    i++;
                }

                // Get object from second curly group - process nested picture commands
                std::vector<PictureItem> nested_items;
                if (i + 1 < items.size() && items[i+1].type == PictureItem::CURLY_GROUP) {
                    flattenPictureChildren(items[i+1].elem, nested_items, pool);
                    i++;
                }

                log_debug("multiput: start=(%.2f,%.2f) delta=(%.2f,%.2f) n=%d nested=%zu",
                         x, y, dx, dy, n, nested_items.size());

                // Process n copies of the nested content with position offset
                if (g_picture_renderer && n > 0) {
                    for (int copy = 0; copy < n; copy++) {
                        double pos_x = x + copy * dx;
                        double pos_y = y + copy * dy;
                        g_picture_renderer->setPosition(pos_x, pos_y);

                        // Process nested items (similar to main loop)
                        for (size_t ni = 0; ni < nested_items.size(); ni++) {
                            const auto& item = nested_items[ni];

                            if (item.type == PictureItem::LINE) {
                                // Process nested line: LINE, TEXT(slope), CURLY_GROUP(length)
                                double sx = 0, sy = 0, len = 0;

                                // Check next item for slope text
                                if (ni + 1 < nested_items.size() && nested_items[ni+1].type == PictureItem::TEXT) {
                                    parsePicCoord(nested_items[ni+1].text.c_str(), &sx, &sy);
                                    ni++;
                                }

                                // Check next item for length curly_group
                                if (ni + 1 < nested_items.size() && nested_items[ni+1].type == PictureItem::CURLY_GROUP) {
                                    ElementReader grp(nested_items[ni+1].elem);
                                    StringBuf* sb = stringbuf_new(pool);
                                    grp.textContent(sb);
                                    String* str = stringbuf_to_string(sb);
                                    char* end;
                                    len = strtod(str->chars, &end);
                                    ni++;
                                }

                                if (len > 0) {
                                    g_picture_renderer->line(sx, sy, len);
                                }
                            } else if (item.type == PictureItem::CIRCLE || item.type == PictureItem::CIRCLE_FILLED) {
                                // Process nested circle
                                double diameter = 0;

                                // Diameter might be in element content or following curly_group
                                ElementReader circ_elem(item.elem);
                                StringBuf* sb = stringbuf_new(pool);
                                circ_elem.textContent(sb);
                                String* str = stringbuf_to_string(sb);
                                if (str && strlen(str->chars) > 0) {
                                    char* end;
                                    diameter = strtod(str->chars, &end);
                                }

                                if (diameter == 0 && ni + 1 < nested_items.size() &&
                                    nested_items[ni+1].type == PictureItem::CURLY_GROUP) {
                                    ElementReader grp(nested_items[ni+1].elem);
                                    StringBuf* sb2 = stringbuf_new(pool);
                                    grp.textContent(sb2);
                                    String* str2 = stringbuf_to_string(sb2);
                                    char* end;
                                    diameter = strtod(str2->chars, &end);
                                    ni++;
                                }

                                if (diameter > 0) {
                                    g_picture_renderer->circle(diameter, item.type == PictureItem::CIRCLE_FILLED);
                                }
                            }
                        }
                    }
                }

                i++;
                break;
            }

            case PictureItem::TEXT:
            case PictureItem::CURLY_GROUP:
            case PictureItem::BRACK_GROUP:
            case PictureItem::UNKNOWN:
            default:
                // Skip text and orphaned groups
                i++;
                break;
        }
    }
}

// Handler for \begin{...} when parsed as a standalone command (inside curly groups)
// This collects siblings until matching \end{...} and dispatches to appropriate handler
static void cmd_begin(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    Pool* pool = proc->pool();

    // Extract environment name from \begin element content
    ElementReader elem_reader(elem);
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* str = stringbuf_to_string(sb);
    const char* env_name = str ? str->chars : "";

    log_debug("cmd_begin: environment='%s'", env_name);

    // Only handle picture environment specially
    if (strcmp(env_name, "picture") != 0) {
        // For other environments, just process children
        proc->processChildren(elem);
        return;
    }

    // For picture environment, we need to collect siblings until \end{picture}
    // Use sibling context to consume following elements

    // Create a synthetic element to hold the picture content
    MarkBuilder builder(proc->input());
    ElementBuilder pic_elem = builder.element("picture");

    // Consume following siblings until \end{picture}
    int64_t consumed = 0;
    bool found_end = false;

    // Access sibling context through proc's internal method
    ElementReader* parent = proc->getSiblingParent();
    int64_t current_idx = proc->getSiblingCurrentIndex();

    if (parent) {
        int64_t count = parent->childCount();
        for (int64_t i = current_idx + 1; i < count; i++) {
            ItemReader sibling = parent->childAt(i);
            consumed++;

            // Check if this is \end{picture}
            if (sibling.isElement()) {
                ElementReader sib_elem = sibling.asElement();
                const char* tag = sib_elem.tagName();
                if (tag && strcmp(tag, "end") == 0) {
                    // Check if it's \end{picture}
                    StringBuf* end_sb = stringbuf_new(pool);
                    sib_elem.textContent(end_sb);
                    String* end_str = stringbuf_to_string(end_sb);
                    if (end_str && strcmp(end_str->chars, "picture") == 0) {
                        found_end = true;
                        break;
                    }
                }
            }

            // Add this sibling to the picture element
            pic_elem.child(sibling.item());
        }
    }

    if (!found_end) {
        log_warn("cmd_begin: no matching \\end{picture} found");
    }

    // Tell processChildren how many siblings we consumed
    proc->setSiblingConsumed(consumed);

    // Create the picture element and process it
    Item picture_item = pic_elem.final();
    cmd_picture(proc, picture_item);
}

static void cmd_picture(LatexProcessor* proc, Item elem) {
    // \begin{picture}(width,height)(x_offset,y_offset)
    // Parser gives: {"$":"picture", "_":[paragraph with coords and commands]}
    HtmlGenerator* gen = proc->generator();
    Pool* pool = proc->pool();

    // Initialize picture context with current unitlength
    g_picture_ctx = PictureContext();
    g_picture_ctx.unitlength_px = g_unitlength_px;
    g_picture_renderer = new PictureRenderer(g_picture_ctx);

    // Flatten picture children into sequential list
    std::vector<PictureItem> items;
    flattenPictureChildren(elem, items, pool);

    // Parse picture dimensions from first text content
    double width = 100, height = 100;  // default
    double x_off = 0, y_off = 0;

    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].type == PictureItem::TEXT) {
            const char* text = items[i].text.c_str();
            const char* next = parsePicCoordAdvance(text, &width, &height);
            if (next) {
                // Check for offset
                parsePicCoord(next, &x_off, &y_off);
                break;
            }
        }
    }

    log_debug("cmd_picture: size=(%.2f,%.2f) offset=(%.2f,%.2f) unitlength=%.3fpx",
              width, height, x_off, y_off, g_unitlength_px);

    // Begin picture with parsed dimensions
    g_picture_renderer->beginPicture(width, height, x_off, y_off);

    // Process picture items
    processPictureItems(proc, items);

    // End picture and get HTML output
    std::string html = g_picture_renderer->endPicture();

    // Output directly as raw HTML
    gen->rawHtml(html.c_str());

    // Cleanup
    delete g_picture_renderer;
    g_picture_renderer = nullptr;
}

static void cmd_thicklines(LatexProcessor* proc, Item elem) {
    // \thicklines - set thick line mode for picture
    if (g_picture_renderer) {
        g_picture_renderer->thicklines();
    }
}

static void cmd_thinlines(LatexProcessor* proc, Item elem) {
    // \thinlines - set thin line mode for picture
    if (g_picture_renderer) {
        g_picture_renderer->thinlines();
    }
}

// =============================================================================
// Color Commands
// =============================================================================

// Helper: Convert color specification to CSS color string
static std::string colorToCss(const char* model, const char* spec) {
    if (!model || !spec) return "black";

    if (strcmp(model, "rgb") == 0) {
        // rgb{r,g,b} with values 0-1
        float r, g, b;
        if (sscanf(spec, "%f,%f,%f", &r, &g, &b) == 3) {
            int ir = (int)(r * 255);
            int ig = (int)(g * 255);
            int ib = (int)(b * 255);
            char buf[32];
            snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", ir, ig, ib);
            return std::string(buf);
        }
    } else if (strcmp(model, "RGB") == 0) {
        // RGB{R,G,B} with values 0-255
        int r, g, b;
        if (sscanf(spec, "%d,%d,%d", &r, &g, &b) == 3) {
            char buf[32];
            snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", r, g, b);
            return std::string(buf);
        }
    } else if (strcmp(model, "HTML") == 0) {
        // HTML{RRGGBB} hex color
        char buf[16];
        snprintf(buf, sizeof(buf), "#%s", spec);
        return std::string(buf);
    } else if (strcmp(model, "gray") == 0) {
        // gray{value} with value 0-1
        float gray;
        if (sscanf(spec, "%f", &gray) == 1) {
            int ig = (int)(gray * 255);
            char buf[32];
            snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", ig, ig, ig);
            return std::string(buf);
        }
    }

    return "black";
}

// Helper: Get named color CSS value
static std::string namedColorToCss(const char* name) {
    // Return standard CSS named colors
    return std::string(name);
}

// Handler for color_reference node (tree-sitter specific)
static void cmd_color_reference(LatexProcessor* proc, Item elem) {
    // Tree-sitter parses \textcolor and \colorbox as <color_reference>
    // Structure: <color_reference> <\textcolor|colorbox> <curly_group_text "color"> <curly_group "content">
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string command_name;
    std::string color_name;
    Item content_group = ItemNull;

    log_debug("cmd_color_reference called");  // DEBUG

    // Extract command name, color, and content
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_SYMBOL) {
            // Command name like "\textcolor" or "\colorbox"
            String* str = (String*)child.item().string_ptr;
            command_name = str->chars;
            log_debug("Found command: %s", command_name.c_str());  // DEBUG
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group_text") == 0) {
                // Color name
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);
                color_name = content->chars;
            } else if (strcmp(tag, "curly_group") == 0) {
                // Text content
                content_group = child.item();
            }
        }
    }

    // Generate output based on command type
    if (command_name.find("textcolor") != std::string::npos) {
        // \textcolor - colored text
        std::string style_value = "color: " + namedColorToCss(color_name.c_str());
        gen->spanWithStyle(style_value.c_str());
        if (get_type_id(content_group) != LMD_TYPE_NULL) {
            proc->processChildren(content_group);
        }
        gen->closeElement();
    } else if (command_name.find("colorbox") != std::string::npos) {
        // \colorbox - colored background
        std::string style_value = "background-color: " + namedColorToCss(color_name.c_str());
        gen->spanWithStyle(style_value.c_str());
        if (get_type_id(content_group) != LMD_TYPE_NULL) {
            proc->processChildren(content_group);
        }
        gen->closeElement();
    }
}

static void cmd_textcolor(LatexProcessor* proc, Item elem) {
    // \textcolor{color}{text} or \textcolor[model]{spec}{text}
    // Parser produces: {"$":"textcolor", "_":["color_name", "text_content"]}
    // Or with model: {"$":"textcolor", "_":[<bracket_group>, "spec", "text_content"]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string color_model;
    std::string color_spec;
    std::string color_name;
    bool has_model = false;
    std::vector<Item> text_items;  // Collect text content items

    // Extract color specification and text
    auto iter = elem_reader.children();
    ItemReader child;
    int child_index = 0;

    while (iter.next(&child)) {
        TypeId child_type = child.getType();

        if (child_type == LMD_TYPE_STRING) {
            // Direct string child
            String* str = (String*)child.item().string_ptr;
            const char* content = str->chars;

            if (color_name.empty() && !has_model) {
                // First string is the color name (named color like "red", "blue")
                color_name = content;
            } else if (has_model && color_spec.empty()) {
                // After model bracket, first string is color spec
                color_spec = content;
            } else {
                // Remaining strings are text content
                text_items.push_back(child.item());
            }
        } else if (child_type == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group_text") == 0 || strcmp(tag, "bracket_group") == 0) {
                // Color model like [rgb] or [HTML]
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* model_str = stringbuf_to_string(sb);
                color_model = model_str->chars;
                has_model = true;
            } else if (strcmp(tag, "curly_group_text") == 0 || strcmp(tag, "curly_group") == 0) {
                // Could be color spec or text content
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);

                if (color_name.empty() && !has_model) {
                    color_name = content->chars;
                } else if (has_model && color_spec.empty()) {
                    color_spec = content->chars;
                } else {
                    // Text content element - save for processing
                    text_items.push_back(child.item());
                }
            } else {
                // Other elements are text content
                text_items.push_back(child.item());
            }
        }
        child_index++;
    }

    // Generate colored span if we have text content
    if (!text_items.empty()) {
        std::string style_value = "color: " +
             (has_model ? colorToCss(color_model.c_str(), color_spec.c_str())
                        : namedColorToCss(color_name.c_str()));
        gen->spanWithStyle(style_value.c_str());

        // Process all text content items
        for (Item text_item : text_items) {
            if (get_type_id(text_item) == LMD_TYPE_STRING) {
                String* str = (String*)text_item.string_ptr;
                gen->text(str->chars);
            } else {
                proc->processNode(text_item);
            }
        }
        gen->closeElement();
    }
}

static void cmd_color(LatexProcessor* proc, Item elem) {
    // \color{name} or \color[model]{spec} - changes current color
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string color_model;
    std::string color_spec;
    std::string color_name;
    bool has_model = false;

    // Extract color specification
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "bracket_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* model_str = stringbuf_to_string(sb);
                color_model = model_str->chars;
                has_model = true;
            } else if (strcmp(tag, "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);

                if (has_model) {
                    color_spec = content->chars;
                } else {
                    color_name = content->chars;
                }
            }
        }
    }

    // Open a span with the color style (contents will be added by parent)
    std::string style_value = "color: " +
         (has_model ? colorToCss(color_model.c_str(), color_spec.c_str())
                    : namedColorToCss(color_name.c_str()));
    gen->spanWithStyle(style_value.c_str());
}

static void cmd_colorbox(LatexProcessor* proc, Item elem) {
    // \colorbox{color}{text} or \colorbox[model]{spec}{text}
    // Parser produces: {"$":"colorbox", "_":["color_name", "text_content"]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string color_model;
    std::string color_spec;
    std::string color_name;
    bool has_model = false;
    std::vector<Item> text_items;  // Collect text content items

    // Extract color specification and text
    auto iter = elem_reader.children();
    ItemReader child;

    while (iter.next(&child)) {
        TypeId child_type = child.getType();

        if (child_type == LMD_TYPE_STRING) {
            // Direct string child
            String* str = (String*)child.item().string_ptr;
            const char* content = str->chars;

            if (color_name.empty() && !has_model) {
                // First string is the color name
                color_name = content;
            } else if (has_model && color_spec.empty()) {
                // After model bracket, first string is color spec
                color_spec = content;
            } else {
                // Remaining strings are text content
                text_items.push_back(child.item());
            }
        } else if (child_type == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "brack_group_text") == 0 || strcmp(tag, "bracket_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* model_str = stringbuf_to_string(sb);
                color_model = model_str->chars;
                has_model = true;
            } else if (strcmp(tag, "curly_group_text") == 0 || strcmp(tag, "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);

                if (color_name.empty() && !has_model) {
                    color_name = content->chars;
                } else if (has_model && color_spec.empty()) {
                    color_spec = content->chars;
                } else {
                    // Text content element
                    text_items.push_back(child.item());
                }
            } else {
                // Other elements are text content
                text_items.push_back(child.item());
            }
        }
    }

    // Generate colored box if we have text content
    if (!text_items.empty()) {
        std::string style_value = "background-color: " +
             (has_model ? colorToCss(color_model.c_str(), color_spec.c_str())
                        : namedColorToCss(color_name.c_str()));
        gen->spanWithStyle(style_value.c_str());

        // Process all text content items
        for (Item text_item : text_items) {
            if (get_type_id(text_item) == LMD_TYPE_STRING) {
                String* str = (String*)text_item.string_ptr;
                gen->text(str->chars);
            } else {
                proc->processNode(text_item);
            }
        }
        gen->closeElement();
    }
}

static void cmd_fcolorbox(LatexProcessor* proc, Item elem) {
    // \fcolorbox{framecolor}{bgcolor}{text}
    // Tree-sitter parses this as <fcolorbox> with 3 direct STRING children (not curly_group!)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string frame_color;
    std::string bg_color;
    std::string text_content;
    int string_count = 0;

    // Extract the 3 string children
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            String* str = (String*)child.item().string_ptr;
            if (string_count == 0) {
                frame_color = str->chars;
            } else if (string_count == 1) {
                bg_color = str->chars;
            } else if (string_count == 2) {
                text_content = str->chars;
            }
            string_count++;
        }
    }

    // Generate output
    if (string_count >= 3) {
        std::string style_value = "background-color: " + namedColorToCss(bg_color.c_str()) +
                           "; border: 1px solid " + namedColorToCss(frame_color.c_str());
        gen->spanWithStyle(style_value.c_str());
        gen->text(text_content.c_str());
        gen->closeElement();
    }
}

static void cmd_definecolor(LatexProcessor* proc, Item elem) {
    // \definecolor{name}{model}{spec}
    // Parser produces: {"$":"definecolor", "_":["name", "model", "spec"]}
    // For now, just output a comment - in full implementation would store in color registry
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string color_name;
    std::string color_model;
    std::string color_spec;
    int string_index = 0;

    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            String* str = (String*)child.item().string_ptr;
            if (string_index == 0) {
                color_name = str->chars;
            } else if (string_index == 1) {
                color_model = str->chars;
            } else if (string_index == 2) {
                color_spec = str->chars;
            }
            string_index++;
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            // Handle curly_group children (old format)
            ElementReader child_elem(child.item());
            Pool* pool = proc->pool();
            StringBuf* sb = stringbuf_new(pool);
            child_elem.textContent(sb);
            String* content = stringbuf_to_string(sb);

            if (string_index == 0) {
                color_name = content->chars;
            } else if (string_index == 1) {
                color_model = content->chars;
            } else if (string_index == 2) {
                color_spec = content->chars;
            }
            string_index++;
        }
    }

    // Output as an HTML comment for now (helps with debugging/testing)
    if (!color_name.empty() && !color_model.empty() && !color_spec.empty()) {
        // Convert to CSS to include in output for testing
        std::string css_color = colorToCss(color_model.c_str(), color_spec.c_str());
        std::stringstream comment;
        comment << "<!-- definecolor: " << color_name << " = " << color_model << "{" << color_spec << "} → " << css_color << " -->";
        gen->text(comment.str().c_str());
    }
}

// =============================================================================
// Bibliography & Citation Commands
// =============================================================================

static void cmd_cite(LatexProcessor* proc, Item elem) {
    // \cite[optional]{key} or \cite{key1,key2}
    // Parser produces: {"$":"cite", "_":["key1,key2"]} or {"$":"cite", "_":[<bracket_group>, "key"]}
    // Generate citation reference like [key]
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract citation keys
    std::vector<std::string> keys;
    std::string optional_text;

    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            // Direct string child - citation key(s)
            String* str = (String*)child.item().string_ptr;
            const char* content = str->chars;

            // Split by comma
            std::string current_key;
            for (size_t i = 0; i <= strlen(content); i++) {
                if (content[i] == ',' || content[i] == '\0') {
                    if (!current_key.empty()) {
                        // Trim whitespace
                        size_t start = current_key.find_first_not_of(" \t\n");
                        size_t end = current_key.find_last_not_of(" \t\n");
                        if (start != std::string::npos) {
                            keys.push_back(current_key.substr(start, end - start + 1));
                        }
                        current_key.clear();
                    }
                } else {
                    current_key += content[i];
                }
            }
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "bracket_group") == 0) {
                // Optional text like "p. 42"
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* text = stringbuf_to_string(sb);
                optional_text = text->chars;
            } else if (strcmp(tag, "curly_group") == 0) {
                // Extract keys (may be comma-separated)
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* keys_str = stringbuf_to_string(sb);

                // Split by comma
                const char* str = keys_str->chars;
                std::string current_key;
                for (size_t i = 0; i <= strlen(str); i++) {
                    if (str[i] == ',' || str[i] == '\0') {
                        if (!current_key.empty()) {
                            // Trim whitespace
                            size_t start = current_key.find_first_not_of(" \t\n");
                            size_t end = current_key.find_last_not_of(" \t\n");
                            if (start != std::string::npos) {
                                keys.push_back(current_key.substr(start, end - start + 1));
                            }
                            current_key.clear();
                        }
                    } else {
                        current_key += str[i];
                    }
                }
            }
        }
    }

    // Generate citation
    gen->span("cite");
    gen->text("[");

    for (size_t i = 0; i < keys.size(); i++) {
        if (i > 0) gen->text(",");
        // For now, just output the key - in full implementation would look up number
        gen->text(keys[i].c_str());
    }

    if (!optional_text.empty()) {
        gen->text(", ");
        gen->text(optional_text.c_str());
    }

    gen->text("]");
    gen->closeElement();  // close span
}

static void cmd_citeauthor(LatexProcessor* proc, Item elem) {
    // \citeauthor{key} - output author name
    // Parser produces: {"$":"citeauthor", "_":["key"]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract key
    std::string key;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            // Direct string child - this is the key
            String* str = (String*)child.item().string_ptr;
            key = str->chars;
            break;
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* key_str = stringbuf_to_string(sb);
                key = key_str->chars;
                break;
            }
        }
    }

    // For now, just output the key - in full implementation would look up author
    gen->span("cite-author");
    gen->text(key.c_str());
    gen->closeElement();
}

static void cmd_citeyear(LatexProcessor* proc, Item elem) {
    // \citeyear{key} - output year
    // Parser produces: {"$":"citeyear", "_":["key"]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    // Extract key
    std::string key;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_STRING) {
            // Direct string child - this is the key
            String* str = (String*)child.item().string_ptr;
            key = str->chars;
            break;
        } else if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* key_str = stringbuf_to_string(sb);
                key = key_str->chars;
                break;
            }
        }
    }

    // For now, just output the key - in full implementation would extract year
    gen->span("cite-year");
    gen->text(key.c_str());
    gen->closeElement();
}

static void cmd_nocite(LatexProcessor* proc, Item elem) {
    // \nocite{key} - add to bibliography without displaying citation
    // This is metadata only, produces no visible output
    (void)proc;
    (void)elem;
}

static void cmd_bibliographystyle(LatexProcessor* proc, Item elem) {
    // \bibliographystyle{style} - set citation style
    // This is typically just metadata, doesn't produce output
    // We could store the style in the processor for later use
    // For now, just skip it
}

static void cmd_bibliography(LatexProcessor* proc, Item elem) {
    // \bibliography{file} - include bibliography
    // This would normally read a .bib file and generate the bibliography
    // For now, just output a placeholder section
    HtmlGenerator* gen = proc->generator();

    gen->startSection("section", false, "References", "references");

    // Process children (if any - though \bibliography usually has no content)
    proc->processChildren(elem);
}

static void cmd_bibitem(LatexProcessor* proc, Item elem) {
    // \bibitem[label]{key} Entry text...
    // Part of thebibliography environment
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);

    std::string label;
    std::string key;

    // Extract optional label and key
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "bracket_group") == 0) {
                // Optional custom label
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* label_str = stringbuf_to_string(sb);
                label = label_str->chars;
            } else if (strcmp(tag, "curly_group") == 0) {
                // Citation key
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* key_str = stringbuf_to_string(sb);
                key = key_str->chars;
            }
        }
    }

    // Start bibliography item
    gen->div("bibitem");

    // Output label/number
    gen->span("bibitem-label");
    if (!label.empty()) {
        gen->text("[");
        gen->text(label.c_str());
        gen->text("]");
    } else {
        // Use key as fallback
        gen->text("[");
        gen->text(key.c_str());
        gen->text("]");
    }
    gen->closeElement();  // close span

    gen->text(" ");

    // The entry text will follow as siblings
    proc->processChildren(elem);

    gen->closeElement();  // close div
}

// =============================================================================
// Document Structure Commands
// =============================================================================

static void cmd_documentclass(LatexProcessor* proc, Item elem) {
    // \documentclass[options]{class}
    // Configure generator based on document class
    HtmlGenerator* gen = proc->generator();

    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* class_str = stringbuf_to_string(sb);
    std::string doc_class = class_str->chars;

    // Configure counters based on document class
    if (doc_class == "book" || doc_class == "report") {
        // Book/Report class: sections have chapter as parent
        gen->newCounter("section", "chapter");
        gen->newCounter("subsection", "section");
        gen->newCounter("subsubsection", "subsection");
        gen->newCounter("figure", "chapter");
        gen->newCounter("table", "chapter");
        gen->newCounter("footnote", "chapter");
        gen->newCounter("equation", "chapter");
    }
    // article uses default initialization (no chapter parent)
}

static void cmd_usepackage(LatexProcessor* proc, Item elem) {
    // \usepackage[options]{package1,package2,...}
    // Parse options and package names, then load them via PackageRegistry
    //
    // AST structure: <usepackage "package1,package2,...">
    // - The curly group content is unwrapped to a direct string child
    // - Optional bracket_group for options

    ElementReader reader(elem);
    std::vector<std::string> options;
    std::vector<std::string> package_names;
    Pool* pool = proc->pool();

    int64_t child_count = reader.childCount();
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child = reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            if (tag && (strcmp(tag, "bracket_group") == 0 || strcmp(tag, "brack_group") == 0)) {
                // Parse options (comma-separated in brackets)
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                std::string opt_text(sb->str ? sb->str->chars : "", sb->length);
                stringbuf_free(sb);

                // Split by comma
                std::istringstream iss(opt_text);
                std::string opt;
                while (std::getline(iss, opt, ',')) {
                    size_t start = opt.find_first_not_of(" \t\n\r");
                    size_t end = opt.find_last_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        options.push_back(opt.substr(start, end - start + 1));
                    }
                }
            }
        } else if (child.isString()) {
            // Package names are stored as direct string children (curly group is unwrapped)
            String* str = child.asString();
            if (str && str->chars) {
                std::string pkg_text(str->chars, str->len);

                // Split by comma
                std::istringstream iss(pkg_text);
                std::string pkg;
                while (std::getline(iss, pkg, ',')) {
                    size_t start = pkg.find_first_not_of(" \t\n\r");
                    size_t end = pkg.find_last_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        package_names.push_back(pkg.substr(start, end - start + 1));
                    }
                }
            }
        }
    }

    // Load each package via the registry
    PackageRegistry& registry = PackageRegistry::instance();
    for (const auto& pkg_name : package_names) {
        if (!pkg_name.empty()) {
            registry.loadPackage(pkg_name.c_str(), options);
            log_debug("usepackage: loaded package '%s'", pkg_name.c_str());
        }
    }
}

static void cmd_include(LatexProcessor* proc, Item elem) {
    // \include{filename}
    // File inclusion - in HTML output, we would need to actually read and process the file
    // For now, just skip (no output)
    // TODO: Implement actual file inclusion
}

static void cmd_input(LatexProcessor* proc, Item elem) {
    // \input{filename}
    // Similar to \include but more flexible (can be used anywhere)
    // For now, just skip (no output)
    // TODO: Implement actual file inclusion
}

static void cmd_abstract(LatexProcessor* proc, Item elem) {
    // \begin{abstract}...\end{abstract}
    // Expected format:
    // <div class="list center"><span class="bf small">Abstract</span></div>
    // <div class="list quotation"><p><span class="... small">content</span></p></div>
    HtmlGenerator* gen = proc->generator();

    // Title div
    gen->div("list center");
    gen->span("bf small");
    gen->text("Abstract");
    gen->closeElement();  // close span
    gen->closeElement();  // close title div

    // Content div with quotation styling and small font
    gen->div("list quotation");
    gen->enterGroup();
    gen->currentFont().size = FontSize::Small;  // set small font for content
    proc->processChildren(elem);
    gen->exitGroup();
    proc->closeParagraphIfOpen();  // Ensure paragraph is closed before closing div
    gen->closeElement();  // close content div
}

static void cmd_tableofcontents(LatexProcessor* proc, Item elem) {
    // \tableofcontents
    // Generate table of contents from section headings
    // For now, just output a placeholder
    HtmlGenerator* gen = proc->generator();
    gen->div("toc");
    gen->h(2, nullptr);
    gen->text("Contents");
    gen->closeElement();
    // TODO: Generate actual TOC from collected section headings
    gen->closeElement();
}

static void cmd_document(LatexProcessor* proc, Item elem) {
    // \begin{document}...\end{document}
    // The main document environment - just process children without wrapper
    proc->processChildren(elem);
}

static void cmd_appendix(LatexProcessor* proc, Item elem) {
    // \appendix
    // Changes section numbering to letters (A, B, C...)
    // In HTML, just affects subsequent sections - no direct output
}

static void cmd_mainmatter(LatexProcessor* proc, Item elem) {
    // \mainmatter (for book class)
    // Resets page numbering and starts arabic numerals
    // In HTML, affects page numbering - no direct output
}

static void cmd_frontmatter(LatexProcessor* proc, Item elem) {
    // \frontmatter (for book class)
    // Roman numerals for pages
    // In HTML, affects page numbering - no direct output
}

static void cmd_backmatter(LatexProcessor* proc, Item elem) {
    // \backmatter (for book class)
    // Unnumbered chapters
    // In HTML, affects chapter numbering - no direct output
}

static void cmd_tableofcontents_star(LatexProcessor* proc, Item elem) {
    // \tableofcontents* (starred version - no TOC entry for itself)
    cmd_tableofcontents(proc, elem);
}

// =============================================================================
// Counter & Length System Commands
// =============================================================================

static void cmd_newcounter(LatexProcessor* proc, Item elem) {
    // \newcounter{counter}[parent]
    // Defines a new counter with optional parent for automatic reset
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();

    // Find counter name (first curly_group or direct text)
    std::string counter_name;
    std::string parent_name;

    for (int64_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isString()) {
            String* str = child.asString();
            if (str && str->len > 0 && counter_name.empty()) {
                counter_name = std::string(str->chars, str->len);
            }
        } else if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            if (strcmp(tag, "curly_group") == 0) {
                if (counter_name.empty()) {
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* str = stringbuf_to_string(sb);
                    counter_name = std::string(str->chars, str->len);
                }
            } else if (strcmp(tag, "brack_group") == 0) {
                // Optional parent parameter
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* str = stringbuf_to_string(sb);
                parent_name = std::string(str->chars, str->len);
            }
        }
    }

    // Create the counter with optional parent
    if (!counter_name.empty()) {
        gen->newCounter(counter_name, parent_name);
    }
}

// Helper function to evaluate numeric expressions with embedded LaTeX commands
// Handles the fact that parser outputs \real{1.6} as siblings: [" text\real", {curly_group: ["1.6"]}]
static std::string evaluate_numeric_expression_recursive(LatexProcessor* proc, ElementReader& elem_reader, int64_t& index);

static std::string evaluate_numeric_expression(LatexProcessor* proc, Item expr_item) {
    std::ostringstream result;
    HtmlGenerator* gen = proc->generator();

    ItemReader reader(expr_item.to_const());
    TypeId type = reader.getType();

    if (type == LMD_TYPE_ELEMENT) {
        ElementReader elem_reader(expr_item);

        // Process all children with lookahead for \real and \value
        int64_t count = elem_reader.childCount();
        int64_t i = 0;
        while (i < count) {
            result << evaluate_numeric_expression_recursive(proc, elem_reader, i);
            i++;
        }
    } else if (type == LMD_TYPE_STRING) {
        String* str = reader.asString();
        if (str && str->len > 0) {
            result << std::string(str->chars, str->len);
        }
    }

    return result.str();
}

// Helper that processes one child with lookahead
static std::string evaluate_numeric_expression_recursive(LatexProcessor* proc, ElementReader& elem_reader, int64_t& index) {
    std::ostringstream result;
    HtmlGenerator* gen = proc->generator();

    ItemReader child = elem_reader.childAt(index);
    TypeId type = child.getType();

    if (type == LMD_TYPE_STRING) {
        String* str = child.asString();
        if (str && str->len > 0) {
            std::string text(str->chars, str->len);

            // Check if text ends with \real or \value command
            if (text.length() >= 5 && text.substr(text.length() - 5) == "\\real") {
                // Output text before \real
                result << text.substr(0, text.length() - 5);

                // Look ahead to next sibling for curly_group with value
                if (index + 1 < elem_reader.childCount()) {
                    ItemReader next = elem_reader.childAt(index + 1);
                    if (next.isElement()) {
                        ElementReader next_elem(next.item());
                        if (strcmp(next_elem.tagName(), "curly_group") == 0 && next_elem.childCount() > 0) {
                            // Extract float value from curly_group
                            ItemReader value_child = next_elem.childAt(0);
                            if (value_child.isString()) {
                                const char* num_str = value_child.cstring();
                                double value = atof(num_str);
                                result << (int)value;  // Truncate to int like LaTeX.js
                                index++;  // Skip the curly_group we just consumed
                            }
                        }
                    }
                }
            } else if (text.length() >= 6 && text.substr(text.length() - 6) == "\\value") {
                // Output text before \value
                result << text.substr(0, text.length() - 6);

                // Look ahead to next sibling for curly_group with counter name
                if (index + 1 < elem_reader.childCount()) {
                    ItemReader next = elem_reader.childAt(index + 1);
                    if (next.isElement()) {
                        ElementReader next_elem(next.item());
                        if (strcmp(next_elem.tagName(), "curly_group") == 0 && next_elem.childCount() > 0) {
                            // Extract counter name from curly_group
                            ItemReader name_child = next_elem.childAt(0);
                            if (name_child.isString()) {
                                const char* counter_name = name_child.cstring();
                                int value = gen->getCounter(counter_name);
                                result << value;
                                index++;  // Skip the curly_group we just consumed
                            }
                        }
                    }
                }
            } else {
                // Plain text
                result << text;
            }
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        // For elements like curly_group, recursively process
        ElementReader child_elem(child.item());
        if (strcmp(child_elem.tagName(), "curly_group") == 0) {
            // Process children
            int64_t child_count = child_elem.childCount();
            for (int64_t j = 0; j < child_count; j++) {
                result << evaluate_numeric_expression_recursive(proc, child_elem, j);
            }
        }
    }

    return result.str();
}

static void cmd_setcounter(LatexProcessor* proc, Item elem) {
    // \setcounter{counter}{value}
    // Sets counter to a specific value

    FILE* debugf = fopen("/tmp/latex_debug.txt", "a");
    if (debugf) {
        fprintf(debugf, "=== cmd_setcounter CALLED ===\n");
        fclose(debugf);
    }

    HtmlGenerator* gen = proc->generator();

    // Extract counter name and value from children
    ElementReader elem_reader(elem);
    int child_count = elem_reader.childCount();

    if (child_count >= 2) {
        Pool* pool = proc->pool();

        // First child: counter name
        ItemReader first = elem_reader.childAt(0);
        StringBuf* sb1 = stringbuf_new(pool);
        if (first.isElement()) {
            first.asElement().textContent(sb1);
        } else if (first.isString()) {
            stringbuf_append_str(sb1, first.cstring());
        }
        String* counter_str = stringbuf_to_string(sb1);

        // Remaining children (from index 1 onwards): value expression parts
        // Parser creates Element nodes for \real{} and \value{} commands
        // Example: [" 3*", {real: ["1.6"]}, " * ", {real: ["1.7"]}, " + -- 2"]

        std::ostringstream expr_builder;
        for (int i = 1; i < child_count; i++) {
            ItemReader child = elem_reader.childAt(i);
            TypeId child_type = child.getType();

            if (child_type == LMD_TYPE_STRING) {
                // Plain text - append as-is
                String* str = child.asString();
                if (str && str->len > 0) {
                    expr_builder << std::string(str->chars, str->len);
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                ElementReader child_elem(child.item());
                const char* tag = child_elem.tagName();

                if (strcmp(tag, "real") == 0) {
                    // \real{x} - extract float value (keep as string for expression eval)
                    if (child_elem.childCount() > 0) {
                        ItemReader value_child = child_elem.childAt(0);
                        if (value_child.isString()) {
                            // Append the numeric string directly - evaluator will parse as float
                            expr_builder << value_child.cstring();
                        }
                    }
                } else if (strcmp(tag, "value") == 0) {
                    // \value{counter} - look up counter value
                    if (child_elem.childCount() > 0) {
                        ItemReader name_child = child_elem.childAt(0);
                        if (name_child.isString()) {
                            const char* counter_name = name_child.cstring();
                            int value = gen->getCounter(counter_name);
                            expr_builder << value;
                        }
                    }
                } else {
                    // Other elements (curly_group, etc.) - extract text content
                    Pool* pool = proc->pool();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    String* text_str = stringbuf_to_string(sb);
                    if (text_str && text_str->len > 0) {
                        expr_builder << std::string(text_str->chars, text_str->len);
                    }
                }
            }
        }

        std::string expr_str = expr_builder.str();

        // debug: write to file to see what's happening
        debugf = fopen("/tmp/latex_debug.txt", "a");
        if (debugf) {
            fprintf(debugf, "cmd_setcounter: counter='%s', expr_str='%s'\n", counter_str->chars, expr_str.c_str());
            fclose(debugf);
        }

        // evaluate numeric expression (supports +, -, *, /, parentheses)
        int value = latex_eval_num_expr(expr_str.c_str());

        debugf = fopen("/tmp/latex_debug.txt", "a");
        if (debugf) {
            fprintf(debugf, "cmd_setcounter: result=%d\n", value);
            fclose(debugf);
        }

        gen->setCounter(counter_str->chars, value);
    }
}

static void cmd_addtocounter(LatexProcessor* proc, Item elem) {
    // \addtocounter{counter}{value}
    // Adds to counter value
    HtmlGenerator* gen = proc->generator();

    ElementReader elem_reader(elem);
    int child_count = elem_reader.childCount();

    if (child_count >= 2) {
        Pool* pool = proc->pool();

        // First child: counter name
        ItemReader first = elem_reader.childAt(0);
        StringBuf* sb1 = stringbuf_new(pool);
        if (first.isElement()) {
            first.asElement().textContent(sb1);
        } else if (first.isString()) {
            stringbuf_append_str(sb1, first.cstring());
        }
        String* counter_str = stringbuf_to_string(sb1);

        // Second child: value expression to add (may contain \real{}, \value{}, etc.)
        ItemReader second = elem_reader.childAt(1);

        // Recursively evaluate the expression, resolving any embedded commands
        std::string expr_str = evaluate_numeric_expression(proc, second.item());

        // debug: write to file to see what's happening
        FILE* debugf = fopen("/tmp/latex_debug.txt", "a");
        if (debugf) {
            fprintf(debugf, "cmd_addtocounter: counter='%s', expr_str='%s'\n", counter_str->chars, expr_str.c_str());
            fclose(debugf);
        }

        // evaluate numeric expression (supports +, -, *, /, parentheses)
        int value = latex_eval_num_expr(expr_str.c_str());

        debugf = fopen("/tmp/latex_debug.txt", "a");
        if (debugf) {
            fprintf(debugf, "cmd_addtocounter: result=%d\n", value);
            fclose(debugf);
        }

        gen->addToCounter(counter_str->chars, value);
    }
}

static void cmd_stepcounter(LatexProcessor* proc, Item elem) {
    // \stepcounter{counter}
    // Increments counter by 1
    HtmlGenerator* gen = proc->generator();

    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    gen->stepCounter(counter_str->chars);
}

static void cmd_refstepcounter(LatexProcessor* proc, Item elem) {
    // \refstepcounter{counter}
    // Steps counter, makes it referenceable, and outputs an anchor
    HtmlGenerator* gen = proc->generator();

    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    // Step the counter
    gen->stepCounter(counter_str->chars);

    // Get the new counter value
    int value = gen->getCounter(counter_str->chars);

    // Generate anchor ID and set as current label
    std::stringstream anchor;
    anchor << counter_str->chars << "-" << value;

    std::string text_value = std::to_string(value);
    gen->setCurrentLabel(anchor.str(), text_value);

    // Output an anchor element
    std::stringstream attrs;
    attrs << "id=\"" << anchor.str() << "\"";
    gen->writer()->openTagRaw("a", attrs.str().c_str());
    gen->writer()->closeTag("a");
}

static void cmd_the(LatexProcessor* proc, Item elem) {
    // \the command - expands following counter/length reference
    // In LaTeX, \the\value{c} expands to the value of counter c
    // The tree-sitter parser treats "\the\value{c}" as text "\the\u000balue" + {c}
    // So we need to process children to find the actual counter reference
    proc->ensureParagraph();
    proc->processChildren(elem);
}

static void cmd_value(LatexProcessor* proc, Item elem) {
    // \value{counter}
    // Returns the value of a counter (for use in calculations)
    // Note: In LaTeX, \value returns a numeric value that can be used in expressions
    // For now, we output the counter value as text
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = std::to_string(value);
        gen->text(output.c_str());
    } else {
        gen->text("0");
    }
}

static void cmd_arabic(LatexProcessor* proc, Item elem) {
    // \arabic{counter} - format counter as arabic numerals (1, 2, 3, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatArabic(value);
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        // Counter doesn't exist - output counter name as fallback
        gen->text(counter_str->chars);
    }
}

static void cmd_roman(LatexProcessor* proc, Item elem) {
    // \roman{counter} - format counter as lowercase roman numerals (i, ii, iii, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatRoman(value, false);  // lowercase
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        gen->text(counter_str->chars);
    }
}

static void cmd_Roman(LatexProcessor* proc, Item elem) {
    // \Roman{counter} - format counter as uppercase roman numerals (I, II, III, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatRoman(value, true);  // uppercase
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        gen->text(counter_str->chars);
    }
}

static void cmd_alph(LatexProcessor* proc, Item elem) {
    // \alph{counter} - format counter as lowercase letters (a, b, c, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatAlph(value, false);  // lowercase
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        gen->text(counter_str->chars);
    }
}

static void cmd_Alph(LatexProcessor* proc, Item elem) {
    // \Alph{counter} - format counter as uppercase letters (A, B, C, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatAlph(value, true);  // uppercase
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        gen->text(counter_str->chars);
    }
}

static void cmd_fnsymbol(LatexProcessor* proc, Item elem) {
    // \fnsymbol{counter} - format counter as footnote symbols (*, †, ‡, ...)
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);

    if (gen->hasCounter(counter_str->chars)) {
        int value = gen->getCounter(counter_str->chars);
        std::string output = gen->formatFnSymbol(value);
        proc->ensureParagraph();
        gen->text(output.c_str());
    } else {
        gen->text(counter_str->chars);
    }
}

static void cmd_newlength(LatexProcessor* proc, Item elem) {
    // \newlength{\lengthcmd}
    // Defines a new length variable
    // In HTML, no output (length management)
    // TODO: Length variable tracking
}

static void cmd_setlength(LatexProcessor* proc, Item elem) {
    // \setlength{\lengthcmd}{value}
    // Sets a length to a specific value
    // Currently only unitlength is tracked (for picture environment)
    ElementReader elem_reader(elem);

    std::string length_name;
    std::string length_value;

    // Parse children to get length name and value
    for (int64_t i = 0; i < elem_reader.childCount(); i++) {
        ItemReader child = elem_reader.childAt(i);

        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            // First element child is the length name (e.g., "unitlength")
            // The tag name IS the length name
            if (length_name.empty()) {
                if (tag) {
                    length_name = tag;
                }
            }
        } else if (child.isString()) {
            String* str = child.asString();
            if (str && str->len > 0) {
                // This is the length value
                length_value = std::string(str->chars, str->len);
            }
        }
    }

    // Handle unitlength specially for picture environment
    if (length_name == "unitlength" && !length_value.empty()) {
        double px = convert_length_to_px(length_value.c_str());
        if (px > 0) {
            g_unitlength_px = px;
        }
    }

    // No HTML output
}

// =============================================================================
// LatexProcessor Implementation
// =============================================================================

void LatexProcessor::initCommandTable() {
    // Macro definitions
    command_table_["newcommand"] = cmd_newcommand;
    command_table_["renewcommand"] = cmd_renewcommand;
    command_table_["providecommand"] = cmd_providecommand;
    command_table_["def"] = cmd_def;

    // Diacritic commands (accent marks)
    command_table_["'"] = cmd_acute;       // \'e -> é
    command_table_["`"] = cmd_grave;       // \`e -> è
    command_table_["^"] = cmd_circumflex;  // \^o -> ô
    command_table_["~"] = cmd_tilde_accent; // \~n -> ñ
    command_table_["\""] = cmd_diaeresis;  // \"a -> ä
    command_table_["="] = cmd_macron;      // \=a -> ā
    command_table_["."] = cmd_dot_above;   // \.z -> ż
    command_table_["u"] = cmd_breve;       // \u{a} -> ă  (NOTE: conflicts with letter u)
    command_table_["v"] = cmd_caron;       // \v{c} -> č  (NOTE: conflicts with letter v)
    command_table_["H"] = cmd_double_acute; // \H{o} -> ő
    command_table_["c"] = cmd_cedilla;     // \c{c} -> ç  (NOTE: conflicts with letter c)
    command_table_["d"] = cmd_dot_below;   // \d{a} -> ạ  (NOTE: conflicts with letter d)
    command_table_["b"] = cmd_macron_below; // \b{a} -> a̲ (NOTE: conflicts with letter b)
    command_table_["r"] = cmd_ring_above;  // \r{a} -> å  (NOTE: conflicts with letter r)
    command_table_["k"] = cmd_ogonek;      // \k{a} -> ą  (NOTE: conflicts with letter k)

    // Special character commands (non-combining letters)
    command_table_["i"] = cmd_i;           // \i -> ı (dotless i)
    command_table_["j"] = cmd_j;           // \j -> ȷ (dotless j)
    command_table_["l"] = cmd_l;           // \l -> ł (Polish l-stroke)
    command_table_["L"] = cmd_L;           // \L -> Ł (Polish L-stroke)
    command_table_["o"] = cmd_o_special;   // \o -> ø (Scandinavian slashed o)
    command_table_["O"] = cmd_O_special;   // \O -> Ø (Scandinavian slashed O)
    command_table_["ss"] = cmd_ss;         // \ss -> ß (German eszett)
    command_table_["ae"] = cmd_ae;         // \ae -> æ
    command_table_["AE"] = cmd_AE;         // \AE -> Æ
    command_table_["oe"] = cmd_oe;         // \oe -> œ
    command_table_["OE"] = cmd_OE;         // \OE -> Œ
    command_table_["aa"] = cmd_aa;         // \aa -> å
    command_table_["AA"] = cmd_AA;         // \AA -> Å

    // Text formatting
    command_table_["textbf"] = cmd_textbf;
    command_table_["textit"] = cmd_textit;
    command_table_["emph"] = cmd_emph;
    command_table_["texttt"] = cmd_texttt;
    command_table_["textsf"] = cmd_textsf;
    command_table_["textrm"] = cmd_textrm;
    command_table_["textsc"] = cmd_textsc;
    command_table_["underline"] = cmd_underline;
    command_table_["sout"] = cmd_sout;
    command_table_["textmd"] = cmd_textmd;
    command_table_["textup"] = cmd_textup;
    command_table_["textsl"] = cmd_textsl;
    command_table_["textnormal"] = cmd_textnormal;

    // Font declarations
    command_table_["bfseries"] = cmd_bfseries;
    command_table_["mdseries"] = cmd_mdseries;
    command_table_["rmfamily"] = cmd_rmfamily;
    command_table_["sffamily"] = cmd_sffamily;
    command_table_["ttfamily"] = cmd_ttfamily;
    command_table_["itshape"] = cmd_itshape;
    command_table_["em"] = cmd_em;  // \em is same as \itshape
    command_table_["slshape"] = cmd_slshape;
    command_table_["scshape"] = cmd_scshape;
    command_table_["upshape"] = cmd_upshape;
    command_table_["normalfont"] = cmd_normalfont;

    // Font sizes
    command_table_["tiny"] = cmd_tiny;
    command_table_["scriptsize"] = cmd_scriptsize;
    command_table_["footnotesize"] = cmd_footnotesize;
    command_table_["small"] = cmd_small;
    command_table_["normalsize"] = cmd_normalsize;
    command_table_["large"] = cmd_large;
    command_table_["Large"] = cmd_Large;
    command_table_["LARGE"] = cmd_LARGE;
    command_table_["huge"] = cmd_huge;
    command_table_["Huge"] = cmd_Huge;

    // Sectioning
    command_table_["part"] = cmd_part;
    command_table_["chapter"] = cmd_chapter;
    command_table_["section"] = cmd_section;
    command_table_["subsection"] = cmd_subsection;
    command_table_["subsubsection"] = cmd_subsubsection;

    // List environments
    command_table_["itemize"] = cmd_itemize;
    command_table_["enumerate"] = cmd_enumerate;
    command_table_["description"] = cmd_description;
    command_table_["item"] = cmd_item;
    command_table_["enum_item"] = cmd_item;  // Tree-sitter node type for \item

    // Basic environments
    command_table_["quote"] = cmd_quote;
    command_table_["quotation"] = cmd_quotation;
    command_table_["verse"] = cmd_verse;
    command_table_["center"] = cmd_center;
    command_table_["flushleft"] = cmd_flushleft;
    command_table_["flushright"] = cmd_flushright;
    command_table_["comment"] = cmd_comment;
    command_table_["multicols"] = cmd_multicols;  // Multi-column layout
    command_table_["verbatim"] = cmd_verbatim;
    command_table_["verb_command"] = cmd_verb_command;  // \verb|text| inline verbatim (scanner)
    command_table_["verb"] = cmd_verb;  // \verb fallback when scanner doesn't recognize

    // Math environments
    command_table_["math"] = cmd_math;
    command_table_["inline_math"] = cmd_inline_math;  // Tree-sitter node for $...$
    command_table_["displaymath"] = cmd_displaymath;
    command_table_["display_math"] = cmd_display_math;  // Tree-sitter node for $$...$$
    command_table_["$$"] = cmd_dollar_dollar;  // $$ delimiter token
    command_table_["math_environment"] = cmd_math_environment;  // Tree-sitter node for \[...\]
    command_table_["displayed_equation"] = cmd_displaymath;  // Tree-sitter node for \[...\]
    command_table_["equation"] = cmd_equation;
    command_table_["equation*"] = cmd_equation_star;

    // Math-mode commands
    command_table_["text"] = cmd_text;  // \text{...} inside math
    command_table_["xi"] = cmd_xi;
    command_table_["pi"] = cmd_pi;
    command_table_["infty"] = cmd_infty;
    command_table_["int"] = cmd_int_sym;
    command_table_["frac"] = cmd_frac;
    command_table_["superscript"] = cmd_superscript;
    command_table_["subscript"] = cmd_subscript;
    command_table_["hat"] = cmd_hat;

    // Line breaks
    command_table_["\\"] = cmd_newline;
    command_table_["newline"] = cmd_newline;
    command_table_["linebreak"] = cmd_linebreak;
    command_table_["newpage"] = cmd_newpage;
    command_table_["par"] = cmd_par;
    command_table_["noindent"] = cmd_noindent;
    command_table_["gobbleO"] = cmd_gobbleO;
    command_table_["echoO"] = cmd_echoO;
    command_table_["echoOGO"] = cmd_echoOGO;
    command_table_["echoGOG"] = cmd_echoGOG;

    // Special LaTeX commands
    command_table_["TeX"] = cmd_TeX;
    command_table_["LaTeX"] = cmd_LaTeX;
    command_table_["today"] = cmd_today;
    command_table_["empty"] = cmd_empty;
    command_table_["unskip"] = cmd_unskip;
    command_table_["ignorespaces"] = cmd_ignorespaces;
    command_table_["/"] = cmd_ligature_break;  // \/ ligature break
    command_table_["textbackslash"] = cmd_textbackslash;
    command_table_["textellipsis"] = cmd_textellipsis;
    command_table_["textendash"] = cmd_textendash;
    command_table_["textemdash"] = cmd_textemdash;
    command_table_["ldots"] = cmd_ldots;
    command_table_["dots"] = cmd_dots;
    command_table_["char"] = cmd_char;
    command_table_["symbol"] = cmd_symbol;
    command_table_["makeatletter"] = cmd_makeatletter;
    command_table_["makeatother"] = cmd_makeatother;

    // Spacing commands
    command_table_["hspace"] = cmd_hspace;
    command_table_["vspace"] = cmd_vspace;
    command_table_["addvspace"] = cmd_addvspace;
    command_table_["smallskip"] = cmd_smallskip;
    command_table_["medskip"] = cmd_medskip;
    command_table_["bigskip"] = cmd_bigskip;
    command_table_["smallbreak"] = cmd_smallbreak;
    command_table_["medbreak"] = cmd_medbreak;
    command_table_["bigbreak"] = cmd_bigbreak;
    command_table_["vfill"] = cmd_vfill;
    command_table_["hfill"] = cmd_hfill;
    command_table_["nolinebreak"] = cmd_nolinebreak;
    command_table_["nopagebreak"] = cmd_nopagebreak;
    command_table_["pagebreak"] = cmd_pagebreak;
    command_table_["clearpage"] = cmd_clearpage;
    command_table_["marginpar"] = cmd_marginpar;
    command_table_["index"] = cmd_index;
    command_table_["glossary"] = cmd_glossary;
    command_table_["cleardoublepage"] = cmd_cleardoublepage;
    command_table_["enlargethispage"] = cmd_enlargethispage;
    command_table_["negthinspace"] = cmd_negthinspace;
    command_table_["!"] = cmd_negthinspace;  // \! is an alias for \negthinspace
    command_table_["thinspace"] = cmd_thinspace;
    command_table_[","] = cmd_thinspace;  // \, is an alias for \thinspace
    command_table_["enspace"] = cmd_enspace;
    command_table_["quad"] = cmd_quad;
    command_table_["qquad"] = cmd_qquad;

    // Box commands
    command_table_["mbox"] = cmd_mbox;
    command_table_["fbox"] = cmd_fbox;
    command_table_["framebox"] = cmd_framebox;
    command_table_["frame"] = cmd_frame;
    command_table_["parbox"] = cmd_parbox;
    command_table_["makebox"] = cmd_makebox;
    command_table_["phantom"] = cmd_phantom;
    command_table_["hphantom"] = cmd_hphantom;
    command_table_["vphantom"] = cmd_vphantom;
    command_table_["smash"] = cmd_smash;
    command_table_["clap"] = cmd_clap;
    command_table_["llap"] = cmd_llap;
    command_table_["rlap"] = cmd_rlap;

    // Alignment declarations
    command_table_["centering"] = cmd_centering;
    command_table_["raggedright"] = cmd_raggedright;
    command_table_["raggedleft"] = cmd_raggedleft;

    // Document metadata
    command_table_["author"] = cmd_author;
    command_table_["title"] = cmd_title;
    command_table_["date"] = cmd_date;
    command_table_["thanks"] = cmd_thanks;
    command_table_["maketitle"] = cmd_maketitle;

    // Labels and references
    command_table_["label"] = cmd_label;
    command_table_["ref"] = cmd_ref;
    command_table_["pageref"] = cmd_pageref;

    // Hyperlinks
    command_table_["url"] = cmd_url;
    command_table_["hyperlink"] = cmd_href;  // Tree-sitter node type for \href
    command_table_["curly_group_uri"] = cmd_url;  // Tree-sitter uri group
    command_table_["href"] = cmd_href;

    // Footnotes
    command_table_["footnote"] = cmd_footnote;

    // Tables
    command_table_["tabular"] = cmd_tabular;
    command_table_["hline"] = cmd_hline;
    command_table_["multicolumn"] = cmd_multicolumn;

    // Float environments
    command_table_["figure"] = cmd_figure;
    command_table_["table"] = cmd_table_float;
    command_table_["caption"] = cmd_caption;

    // Graphics
    command_table_["graphics_include"] = cmd_includegraphics;
    command_table_["includegraphics"] = cmd_includegraphics;

    // Picture environment (LaTeX picture graphics)
    command_table_["picture"] = cmd_picture;
    command_table_["begin"] = cmd_begin;  // Handle \begin{picture} inside curly groups
    command_table_["end"] = [](LatexProcessor* proc, Item elem) {
        // \end{...} is consumed by cmd_begin, skip orphaned ones
        (void)proc; (void)elem;
    };
    command_table_["thicklines"] = cmd_thicklines;
    command_table_["thinlines"] = cmd_thinlines;

    // Color commands
    command_table_["color_reference"] = cmd_color_reference;  // Tree-sitter node for \textcolor and \colorbox
    command_table_["textcolor"] = cmd_textcolor;
    command_table_["color"] = cmd_color;
    command_table_["colorbox"] = cmd_colorbox;
    command_table_["fcolorbox"] = cmd_fcolorbox;
    command_table_["definecolor"] = cmd_definecolor;

    // Bibliography & Citations
    command_table_["cite"] = cmd_cite;
    command_table_["citeauthor"] = cmd_citeauthor;
    command_table_["citeyear"] = cmd_citeyear;
    command_table_["nocite"] = cmd_nocite;
    command_table_["bibliographystyle"] = cmd_bibliographystyle;
    command_table_["bibliography"] = cmd_bibliography;
    command_table_["bibitem"] = cmd_bibitem;

    // Document structure (additional commands)
    command_table_["documentclass"] = cmd_documentclass;
    command_table_["usepackage"] = cmd_usepackage;
    command_table_["include"] = cmd_include;
    command_table_["input"] = cmd_input;
    command_table_["document"] = cmd_document;
    command_table_["abstract"] = cmd_abstract;
    command_table_["tableofcontents"] = cmd_tableofcontents;
    command_table_["tableofcontents*"] = cmd_tableofcontents_star;
    command_table_["appendix"] = cmd_appendix;
    command_table_["mainmatter"] = cmd_mainmatter;
    command_table_["frontmatter"] = cmd_frontmatter;
    command_table_["backmatter"] = cmd_backmatter;

    // Counter and length system
    command_table_["newcounter"] = cmd_newcounter;
    command_table_["setcounter"] = cmd_setcounter;
    command_table_["addtocounter"] = cmd_addtocounter;
    command_table_["stepcounter"] = cmd_stepcounter;
    command_table_["refstepcounter"] = cmd_refstepcounter;
    command_table_["value"] = cmd_value;
    command_table_["the"] = cmd_the;
    // Counter display commands (now implemented)
    command_table_["arabic"] = cmd_arabic;
    command_table_["roman"] = cmd_roman;
    command_table_["Roman"] = cmd_Roman;
    command_table_["alph"] = cmd_alph;
    command_table_["Alph"] = cmd_Alph;
    command_table_["fnsymbol"] = cmd_fnsymbol;
    // \the<counter> commands are handled dynamically in processCommand()
    command_table_["newlength"] = cmd_newlength;
    command_table_["setlength"] = cmd_setlength;
}

// =============================================================================
// Paragraph Management
// =============================================================================

bool LatexProcessor::isBlockCommand(const char* cmd_name) {
    // Block-level commands that should not be wrapped in paragraphs
    return (strcmp(cmd_name, "chapter") == 0 ||
            strcmp(cmd_name, "section") == 0 ||
            strcmp(cmd_name, "subsection") == 0 ||
            strcmp(cmd_name, "subsubsection") == 0 ||
            strcmp(cmd_name, "paragraph") == 0 ||
            strcmp(cmd_name, "subparagraph") == 0 ||
            strcmp(cmd_name, "part") == 0 ||
            strcmp(cmd_name, "itemize") == 0 ||
            strcmp(cmd_name, "enumerate") == 0 ||
            strcmp(cmd_name, "description") == 0 ||
            strcmp(cmd_name, "quote") == 0 ||
            strcmp(cmd_name, "quotation") == 0 ||
            strcmp(cmd_name, "verse") == 0 ||
            strcmp(cmd_name, "verbatim") == 0 ||
            strcmp(cmd_name, "center") == 0 ||
            strcmp(cmd_name, "flushleft") == 0 ||
            strcmp(cmd_name, "flushright") == 0 ||
            strcmp(cmd_name, "figure") == 0 ||
            strcmp(cmd_name, "table") == 0 ||
            strcmp(cmd_name, "tabular") == 0 ||
            strcmp(cmd_name, "equation") == 0 ||
            strcmp(cmd_name, "displaymath") == 0 ||
            strcmp(cmd_name, "picture") == 0 ||
            strcmp(cmd_name, "par") == 0 ||
            strcmp(cmd_name, "newpage") == 0 ||
            strcmp(cmd_name, "maketitle") == 0 ||
            strcmp(cmd_name, "title") == 0 ||
            strcmp(cmd_name, "author") == 0 ||
            strcmp(cmd_name, "date") == 0 ||
            strcmp(cmd_name, "environment") == 0);  // Generic environment wrapper
}

bool LatexProcessor::isInlineCommand(const char* cmd_name) {
    // Inline formatting commands that should be wrapped in paragraphs
    return (strcmp(cmd_name, "textbf") == 0 ||
            strcmp(cmd_name, "textit") == 0 ||
            strcmp(cmd_name, "emph") == 0 ||
            strcmp(cmd_name, "texttt") == 0 ||
            strcmp(cmd_name, "textsf") == 0 ||
            strcmp(cmd_name, "textrm") == 0 ||
            strcmp(cmd_name, "textsc") == 0 ||
            strcmp(cmd_name, "underline") == 0 ||
            strcmp(cmd_name, "sout") == 0 ||
            strcmp(cmd_name, "textcolor") == 0 ||
            strcmp(cmd_name, "colorbox") == 0 ||
            strcmp(cmd_name, "fcolorbox") == 0 ||
            strcmp(cmd_name, "tiny") == 0 ||
            strcmp(cmd_name, "scriptsize") == 0 ||
            strcmp(cmd_name, "footnotesize") == 0 ||
            strcmp(cmd_name, "small") == 0 ||
            strcmp(cmd_name, "normalsize") == 0 ||
            strcmp(cmd_name, "large") == 0 ||
            strcmp(cmd_name, "Large") == 0 ||
            strcmp(cmd_name, "LARGE") == 0 ||
            strcmp(cmd_name, "huge") == 0 ||
            strcmp(cmd_name, "Huge") == 0 ||
            strcmp(cmd_name, "cite") == 0 ||
            strcmp(cmd_name, "citeauthor") == 0 ||
            strcmp(cmd_name, "citeyear") == 0 ||
            strcmp(cmd_name, "url") == 0 ||
            strcmp(cmd_name, "href") == 0 ||
            strcmp(cmd_name, "ref") == 0 ||
            strcmp(cmd_name, "pageref") == 0 ||
            strcmp(cmd_name, "footnote") == 0);
}

void LatexProcessor::ensureParagraph() {
    // Only open paragraph if we're not inside an inline element
    if (!in_paragraph_ && inline_depth_ == 0) {
        log_debug("ensureParagraph: starting paragraph buffering, restricted=%d", restricted_h_mode_);
        // Start capturing paragraph content - we'll wrap it with <p> when we end the paragraph
        // This allows alignment commands to affect the paragraph retroactively
        gen_->startCapture();
        in_paragraph_ = true;
    }
}

void LatexProcessor::closeParagraphIfOpen() {
    if (in_paragraph_) {
        log_debug("closeParagraphIfOpen: closing paragraph with alignment=%s, restricted=%d",
                  next_paragraph_alignment_ ? next_paragraph_alignment_ : "none", restricted_h_mode_);

        // Get the captured paragraph content
        std::string para_content = gen_->endCapture();

        // Trim trailing whitespace from captured content
        while (!para_content.empty() &&
               (para_content.back() == ' ' || para_content.back() == '\t' ||
                para_content.back() == '\n' || para_content.back() == '\r')) {
            para_content.pop_back();
        }

        // Only output paragraph if it has content
        if (!para_content.empty()) {
            // Determine paragraph class
            const char* para_class = nullptr;
            if (next_paragraph_alignment_) {
                para_class = next_paragraph_alignment_;
                // Keep alignment for subsequent paragraphs (until explicitly changed)
            } else if (next_paragraph_is_noindent_) {
                para_class = "noindent";
                next_paragraph_is_noindent_ = false;
            } else if (next_paragraph_is_continue_) {
                para_class = "continue";
                next_paragraph_is_continue_ = false;
            }

            // Write the paragraph with proper class
            gen_->p(para_class);
            gen_->writer()->writeRawHtml(para_content.c_str());
            gen_->closeElement();
        }

        in_paragraph_ = false;
    }
}

void LatexProcessor::endParagraph() {
    closeParagraphIfOpen();
}

// Helper: Get next sibling argument, skipping whitespace/comments
bool LatexProcessor::getNextSiblingArg(int64_t offset, Item* out_item, const char** out_type) {
    if (!sibling_ctx_.parent_reader) return false;

    int64_t count = sibling_ctx_.parent_reader->childCount();
    int64_t idx = sibling_ctx_.current_index + offset;

    while (idx < count) {
        ItemReader reader = sibling_ctx_.parent_reader->childAt(idx);

        // Skip whitespace strings
        if (reader.isString()) {
            const char* text = reader.cstring();
            if (text) {
                bool is_whitespace = true;
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        is_whitespace = false;
                        break;
                    }
                }
                if (is_whitespace) {
                    idx++;
                    continue;
                }
            }
        }

        // Skip space elements
        if (reader.isElement()) {
            ElementReader elem(reader.item());
            const char* tag = elem.tagName();
            if (tag && strcmp(tag, "space") == 0) {
                idx++;
                continue;
            }
            // Found a non-whitespace element
            *out_item = reader.item();
            *out_type = tag;
            return true;
        }

        // Any other content - not a valid argument
        break;
    }

    return false;
}

// Helper: Output the content of a group (brack_group, curly_group, etc.) with parbreak handling
// This processes children and converts parbreak symbols to <br> tags
void LatexProcessor::outputGroupContent(Item group_item) {
    HtmlGenerator* gen = generator();
    ElementReader reader(group_item);

    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && *text) {
                gen->text(text);
            }
        } else if (child.isSymbol()) {
            const char* sym = child.cstring();
            if (sym && strcmp(sym, "parbreak") == 0) {
                // Convert paragraph break to <br>
                gen->lineBreak(false);
            } else if (sym) {
                // Other symbols - output as-is
                gen->text(sym);
            }
        } else if (child.isElement()) {
            ElementReader elem(child.item());
            const char* tag = elem.tagName();

            if (tag && strcmp(tag, "line_comment") == 0) {
                // Skip line comments in group content
                continue;
            }

            // For other elements, extract text content recursively
            Pool* pool = this->pool();
            StringBuf* sb = stringbuf_new(pool);
            elem.textContent(sb);
            String* str = stringbuf_to_string(sb);
            if (str && str->len > 0) {
                gen->text(str->chars);
            }
        }
    }
}

// Helper: Consume sibling brack_group and curly_group arguments
int LatexProcessor::consumeSiblingArgs(std::vector<Item>& brack_args, std::vector<Item>& curly_args) {
    // Debug logging
    FILE* debug_f = fopen("/tmp/sibling_debug.txt", "a");
    if (debug_f) {
        fprintf(debug_f, "consumeSiblingArgs: parent_reader=%p, current_index=%lld\n",
                (void*)sibling_ctx_.parent_reader, (long long)sibling_ctx_.current_index);
    }

    if (!sibling_ctx_.parent_reader) {
        if (debug_f) { fprintf(debug_f, "  -> no parent_reader, returning 0\n"); fclose(debug_f); }
        return 0;
    }

    int consumed = 0;
    int64_t count = sibling_ctx_.parent_reader->childCount();
    int64_t idx = sibling_ctx_.current_index + 1;

    if (debug_f) {
        fprintf(debug_f, "  -> count=%lld, starting at idx=%lld\n", (long long)count, (long long)idx);
    }

    while (idx < count) {
        ItemReader reader = sibling_ctx_.parent_reader->childAt(idx);

        // Skip whitespace strings
        if (reader.isString()) {
            const char* text = reader.cstring();
            if (text) {
                bool is_whitespace = true;
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        is_whitespace = false;
                        break;
                    }
                }
                if (is_whitespace) {
                    consumed++;
                    idx++;
                    continue;
                }
            }
            // Non-whitespace text - stop consuming
            break;
        }

        // Check for brack_group or curly_group elements
        if (reader.isElement()) {
            ElementReader elem(reader.item());
            const char* tag = elem.tagName();

            if (tag && strcmp(tag, "space") == 0) {
                consumed++;
                idx++;
                continue;
            }

            if (tag && strcmp(tag, "brack_group") == 0) {
                brack_args.push_back(reader.item());
                consumed++;
                idx++;
                continue;
            }

            if (tag && strcmp(tag, "curly_group") == 0) {
                curly_args.push_back(reader.item());
                consumed++;
                idx++;
                continue;
            }

            // Any other element - stop consuming
            break;
        }

        // Any other node type - stop consuming
        break;
    }

    // Update the consumed count so processChildren can skip these siblings
    if (sibling_ctx_.consumed_count) {
        *sibling_ctx_.consumed_count = consumed;
    }

    if (debug_f) {
        fprintf(debug_f, "  -> returning consumed=%d, brack_args.size=%zu, curly_args.size=%zu\n",
                consumed, brack_args.size(), curly_args.size());
        fclose(debug_f);
    }

    return consumed;
}

void LatexProcessor::process(Item root) {
    initCommandTable();
    in_paragraph_ = false;  // Reset paragraph state
    depth_exceeded_ = false;  // Reset depth error flag for this processing session
    sibling_ctx_ = {};  // Reset sibling context
    processNode(root);
    closeParagraphIfOpen();  // Close any open paragraph at the end
}

void LatexProcessor::processNode(Item node) {
    // Check if depth was already exceeded (early exit to stop cascading calls)
    if (depth_exceeded_) {
        return;
    }

    DepthGuard guard(this);
    if (guard.exceeded()) {
        log_error("Processing depth exceeded maximum %d", MAX_MACRO_DEPTH);
        gen_->text("[MAX DEPTH EXCEEDED]");
        depth_exceeded_ = true;  // Set flag to halt all further processing
        return;
    }

    ItemReader reader(node.to_const());
    TypeId type = reader.getType();

    if (type == LMD_TYPE_STRING) {
        // Text content
        String* str = reader.asString();
        if (str && str->len > 0) {
            const char* text = str->chars;

            // Skip EMPTY_STRING sentinel (which has content "lambda.nil")
            if (str == &EMPTY_STRING ||
                (str->len == 10 && strncmp(text, "lambda.nil", 10) == 0)) {
                return;  // skip the empty string sentinel
            }

            // Debug: log "document" string to track where it's coming from
            if (strcmp(text, "document") == 0) {
                log_debug("processNode: found 'document' string - context unknown");
            }

            // Find the first backslash to check for embedded command
            const char* backslash = strchr(text, '\\');

            if (backslash && backslash[1] != '\0') {
                // There's a backslash with content after it
                // Process text before backslash (if any)
                if (backslash > text) {
                    // Create temp buffer for the prefix text
                    size_t prefix_len = backslash - text;
                    char* prefix = (char*)alloca(prefix_len + 1);
                    memcpy(prefix, text, prefix_len);
                    prefix[prefix_len] = '\0';
                    processText(prefix);
                }

                // Extract command name (everything after backslash until non-alpha or end)
                const char* cmd_start = backslash + 1;
                size_t cmd_len = 0;
                while (cmd_start[cmd_len] && isalpha((unsigned char)cmd_start[cmd_len])) {
                    cmd_len++;
                }

                if (cmd_len > 0) {
                    // It's an alphabetic command like \textbackslash
                    char* cmd_name = (char*)alloca(cmd_len + 1);
                    memcpy(cmd_name, cmd_start, cmd_len);
                    cmd_name[cmd_len] = '\0';
                    processCommand(cmd_name, node);

                    // Process any remaining text after command
                    const char* remainder = cmd_start + cmd_len;
                    if (*remainder) {
                        processText(remainder);
                    }
                } else {
                    // Not an alpha command - just process as text
                    processText(text);
                }
            } else {
                // No backslash or backslash at end - normal text
                processText(text);
            }
        }
        return;
    }

    if (type == LMD_TYPE_SYMBOL) {
        // Symbol (spacing, paragraph break, special characters, etc.)
        String* str = reader.asSymbol();
        if (str) {
            const char* sym_name = str->chars;

            if (strcmp(sym_name, "parbreak") == 0) {
                // In restricted horizontal mode (inside \mbox), paragraph breaks:
                // 1. Trim trailing whitespace (unskip)
                // 2. Output exactly one space (to preserve word separation)
                // 3. Skip next leading whitespace
                // Note: If \par immediately precedes, the \par already did unskip,
                // and here we output a space. But if \par's skip consumed the parbreak's
                // effective content, the result is still correct.
                if (restricted_h_mode_) {
                    bool had_trailing = gen_->hasTrailingWhitespace();
                    gen_->trimTrailingWhitespace();
                    // Only output space if we had actual trailing content (not just ws)
                    // This handles "xx\par\n\n{..." where \par already did unskip
                    // and the parbreak shouldn't add more space
                    if (!strip_next_leading_space_) {
                        gen_->text(" ");
                    }
                    strip_next_leading_space_ = true;
                    return;
                }
                // Paragraph break: close current paragraph and prepare for next
                closeParagraphIfOpen();
                // Clear continue and noindent flags - parbreak resets paragraph styling
                next_paragraph_is_continue_ = false;
                next_paragraph_is_noindent_ = false;
                // Don't open new paragraph yet - ensureParagraph() will handle it when next content arrives
            } else if (strcmp(sym_name, "TeX") == 0) {
                // TeX logo
                ensureParagraph();
                gen_->span("tex");
                gen_->text("T");
                gen_->span("e");
                gen_->text("e");
                gen_->closeElement();  // close inner span
                gen_->text("X");
                gen_->closeElement();  // close outer span
                // Set pending ZWS flag - will be checked in processChildren
                pending_zws_output_ = true;
            } else if (strcmp(sym_name, "LaTeX") == 0) {
                // LaTeX logo
                ensureParagraph();
                gen_->span("latex");
                gen_->text("L");
                gen_->span("a");
                gen_->text("a");
                gen_->closeElement();  // close inner span
                gen_->text("T");
                gen_->span("e");
                gen_->text("e");
                gen_->closeElement();  // close inner span
                gen_->text("X");
                gen_->closeElement();  // close outer span
                // Set pending ZWS flag - will be checked in processChildren
                pending_zws_output_ = true;
            } else if (strlen(sym_name) == 1) {
                // Single-character symbols are escaped special characters
                // Output them as literal text
                processText(sym_name);
            } else {
                // Skip other symbols (like 'uri', 'path', etc. - they are markers, not content)
                log_debug("processNode: skipping symbol '%s'", sym_name);
            }
        }
        return;
    }

    if (type == LMD_TYPE_LIST) {
        // List (array of items) - process each item
        List* list = node.list;
        if (list && list->items) {
            for (int64_t i = 0; i < list->length; i++) {
                processNode(list->items[i]);
            }
        }
        return;
    }

    if (type == LMD_TYPE_ELEMENT) {
        // Command or environment - use ElementReader
        ElementReader elem_reader(node);
        const char* tag = elem_reader.tagName();

        log_debug("processNode element tag='%s'", tag);

        // Special handling for root element
        if (strcmp(tag, "latex_document") == 0) {
            // Just process children
            processChildren(node);
            return;
        }

        // Skip "end" elements (malformed \end{...} that parser incorrectly included)
        // These occur when the parser fails to match environment closing tags correctly
        if (strcmp(tag, "end") == 0) {
            // Skip - this is a parsing artifact
            log_debug("processNode: skipping malformed 'end' element");
            return;
        }

        // Special handling for linebreak_command (\\)
        if (strcmp(tag, "linebreak_command") == 0) {
            // In restricted horizontal mode (inside \mbox, etc.), line breaks are ignored.
            // Both trailing and leading whitespace are stripped (unskip + skip).
            if (inRestrictedHMode()) {
                gen_->trimTrailingWhitespace();  // unskip
                setStripNextLeadingSpace(true);  // skip after
                return;
            }
            ensureParagraph();

            // Check for optional length attribute
            if (elem_reader.has_attr("length")) {
                String* length_str = elem_reader.get_string_attr("length");
                if (length_str && length_str->len > 0) {
                    const char* dim_text = length_str->chars;
                    size_t dim_len = length_str->len;

                    // Check if it's a relative unit (em, ex) - preserve as-is
                    bool is_relative = false;
                    if (dim_len >= 2) {
                        const char* suffix = dim_text + dim_len - 2;
                        if (strcmp(suffix, "em") == 0 || strcmp(suffix, "ex") == 0) {
                            is_relative = true;
                        }
                    }

                    // Build the style string
                    char style[128];
                    if (is_relative) {
                        snprintf(style, sizeof(style), "margin-bottom:%s", dim_text);
                    } else {
                        double pixels = convertLatexLengthToPixels(dim_text);
                        if (pixels == 0.0) {
                            gen_->lineBreak(false);
                            return;
                        }
                        snprintf(style, sizeof(style), "margin-bottom:%.3fpx", pixels);
                    }

                    // Check if font styling is active - wrap breakspace in font class for em-relative sizing
                    std::string font_class = gen_->getFontClass(gen_->currentFont());
                    if (!font_class.empty()) {
                        gen_->span(font_class.c_str());
                        gen_->spanWithClassAndStyle("breakspace", style);
                        gen_->closeElement();  // close breakspace
                        gen_->closeElement();  // close font class wrapper
                    } else {
                        gen_->spanWithClassAndStyle("breakspace", style);
                        gen_->closeElement();
                    }
                } else {
                    // No valid length string, just output <br>
                    gen_->lineBreak(false);
                }
            } else {
                // No length specified, just output <br>
                gen_->lineBreak(false);
            }
            return;
        }

        // Special handling for spacing_command
        if (strcmp(tag, "spacing_command") == 0) {
            processSpacingCommand(node);
            return;
        }

        // Special handling for space_cmd (\, \! \; \: \/ \@ \ )
        if (strcmp(tag, "space_cmd") == 0) {
            processSpacingCommand(node);
            return;
        }

        // Special handling for nbsp (~) - non-breaking space
        if (strcmp(tag, "nbsp") == 0) {
            ensureParagraph();
            gen_->writer()->writeRawHtml("&nbsp;");
            return;
        }

        // Special handling for space (horizontal whitespace or single newline)
        // In LaTeX, single newlines are treated as spaces
        // The space element contains the actual whitespace text as children
        if (strcmp(tag, "space") == 0) {
            // Get the actual space text from children and normalize to single space
            // In LaTeX, any amount of horizontal whitespace or single newline = one space
            processText(" ");
            return;
        }

        // Handle _seq elements (from error recovery in parser)
        // These are transparent containers - just process children
        if (strcmp(tag, "_seq") == 0) {
            processChildren(node);
            return;
        }

        // Process command
        processCommand(tag, node);
        return;
    }

    // Unknown type - skip
    log_warn("processNode: unknown type %d", type);
}

void LatexProcessor::processChildren(Item elem) {
    ElementReader elem_reader(elem);

    if (elem_reader.childCount() == 0) {
        return;
    }

    // Use index-based iteration for lookahead capability
    int64_t count = elem_reader.childCount();

    // Debug: log first few children to understand structure
    FILE* debug_file = fopen("/tmp/zws_debug_direct.txt", "a");
    if (debug_file) {
        fprintf(debug_file, "[STRUCTURE] processChildren called, %lld children\n", count);
        for (int64_t dbg_i = 0; dbg_i < std::min(count, (int64_t)5); dbg_i++) {
            ItemReader dbg_reader = elem_reader.childAt(dbg_i);
            TypeId dbg_type = dbg_reader.getType();
            if (dbg_type == LMD_TYPE_STRING) {
                const char* text = dbg_reader.cstring();
                fprintf(debug_file, "[STRUCTURE]   child[%lld]: STRING \"%s\"\n", dbg_i, text ? text : "NULL");
            } else if (dbg_type == LMD_TYPE_SYMBOL) {
                const char* sym = dbg_reader.asSymbol()->chars;
                fprintf(debug_file, "[STRUCTURE]   child[%lld]: SYMBOL '%s'\n", dbg_i, sym ? sym : "NULL");
            } else if (dbg_type == LMD_TYPE_ELEMENT) {
                ElementReader dbg_elem(dbg_reader.item());
                const char* tag = dbg_elem.tagName();
                fprintf(debug_file, "[STRUCTURE]   child[%lld]: ELEMENT tag='%s'\n", dbg_i, tag ? tag : "NULL");
            } else {
                fprintf(debug_file, "[STRUCTURE]   child[%lld]: type=%d\n", dbg_i, dbg_type);
            }
        }
        fclose(debug_file);
    }
    for (int64_t i = 0; i < count; i++) {
        ItemReader child_reader = elem_reader.childAt(i);

        // Check for \char command that needs lookahead for its numeric argument
        if (child_reader.isElement()) {
            ElementReader child_elem(child_reader.item());
            const char* tag = child_elem.tagName();

            if (tag && strcmp(tag, "char") == 0 && child_elem.childCount() == 0) {
                // \char command with no children - parser limitation
                // Look ahead to next sibling for the numeric argument
                if (i + 1 < count) {
                    ItemReader next_reader = elem_reader.childAt(i + 1);
                    if (next_reader.isString()) {
                        const char* text = next_reader.cstring();
                        if (text && (isdigit((unsigned char)text[0]) || text[0] == '"' || text[0] == '\'')) {
                            // Found the number argument - parse and consume just the numeric part
                            ensureParagraph();
                            uint32_t charcode = 0;
                            char* endptr = nullptr;

                            if (text[0] == '"') {
                                // Hex: \char"A0 - consume quote + hex digits
                                charcode = std::strtoul(text + 1, &endptr, 16);
                            } else if (text[0] == '\'') {
                                // Octal: \char'77 - consume quote + octal digits
                                charcode = std::strtoul(text + 1, &endptr, 8);
                            } else {
                                // Decimal: \char98 - consume decimal digits
                                charcode = std::strtoul(text, &endptr, 10);
                            }

                            // Output the character
                            if (charcode > 0) {
                                if (charcode == 0xA0) {
                                    gen_->writer()->writeRawHtml("&nbsp;");
                                } else {
                                    std::string utf8 = codepoint_to_utf8(charcode);
                                    gen_->text(utf8.c_str());
                                }
                            }

                            // Output remaining text after the number (e.g., " test" from "98 test")
                            if (endptr && *endptr) {
                                processText(endptr);
                            }

                            // Skip the next string element since we consumed it
                            i++;
                            continue;
                        }
                    }
                }

                // No valid number found - just skip the \char command
                continue;
            }
        }

        // Check if this is a linebreak element
        if (child_reader.isElement()) {
            ElementReader child_elem(child_reader.item());
            const char* tag = child_elem.tagName();

            if (tag && (strcmp(tag, "linebreak") == 0 || strcmp(tag, "linebreak_command") == 0 ||
                        strcmp(tag, "newline") == 0)) {
                bool has_dimension = false;
                bool preserve_unit = false;
                double dimension_px = 0;
                const char* dimension_text = nullptr;

                // For linebreak_command (new grammar), check for length attribute
                if (strcmp(tag, "linebreak_command") == 0 && child_elem.has_attr("length")) {
                    String* length_str = child_elem.get_string_attr("length");
                    if (length_str && length_str->len > 0) {
                        const char* dim_text = length_str->chars;
                        size_t dim_len = length_str->len;

                        // Check if it's a relative unit (em, ex) - preserve as-is
                        bool is_relative = false;
                        if (dim_len >= 2) {
                            const char* suffix = dim_text + dim_len - 2;
                            if (strcmp(suffix, "em") == 0 || strcmp(suffix, "ex") == 0) {
                                is_relative = true;
                            }
                        }

                        if (is_relative) {
                            has_dimension = true;
                            preserve_unit = true;
                            dimension_text = dim_text;
                        } else {
                            dimension_px = convertLatexLengthToPixels(dim_text);
                            if (dimension_px > 0) {
                                has_dimension = true;
                                preserve_unit = false;
                            }
                        }
                    }
                }
                // For old linebreak/newline (old grammar), check if next sibling is a brack_group
                else if (i + 1 < count) {
                    ItemReader next_reader = elem_reader.childAt(i + 1);

                    if (next_reader.isElement()) {
                        ElementReader next_elem(next_reader.item());
                        const char* next_tag = next_elem.tagName();

                        if (next_tag && strcmp(next_tag, "brack_group") == 0) {
                            // Extract dimension text from brack_group
                            Pool* pool = pool_;
                            StringBuf* sb = stringbuf_new(pool);
                            next_elem.textContent(sb);
                            String* dim_str = stringbuf_to_string(sb);

                            if (dim_str && dim_str->len > 0) {
                                // Check if it's a relative unit (em, ex) - preserve as-is
                                const char* dim_text = dim_str->chars;
                                size_t dim_len = dim_str->len;
                                bool is_relative = false;
                                if (dim_len >= 2) {
                                    const char* suffix = dim_text + dim_len - 2;
                                    if (strcmp(suffix, "em") == 0 || strcmp(suffix, "ex") == 0) {
                                        is_relative = true;
                                    }
                                }

                                if (is_relative) {
                                    // Keep relative units as-is
                                    has_dimension = true;
                                    preserve_unit = true;
                                    dimension_text = dim_text;
                                    i++;
                                } else {
                                    // Convert to pixels
                                    dimension_px = convert_length_to_px(dim_text);
                                    if (dimension_px > 0) {
                                        has_dimension = true;
                                        preserve_unit = false;
                                        i++;
                                    }
                                }
                            }
                        }
                    }
                }

                // In restricted horizontal mode, linebreaks collapse surrounding whitespace.
                //
                // LaTeX behavior in restricted horizontal mode:
                // - \\ and \newline are essentially no-ops (can't break lines in a box)
                // - But they still do \unskip (remove preceding space)
                // - Following spaces are also absorbed
                //
                // Special case for \\[dim]:
                // - If \\[dim] has a dimension argument AND next text has leading space,
                //   preserve exactly one space (the dimension indicates intentional spacing)
                // - Otherwise, collapse all whitespace
                //
                // Summary:
                // - \linebreak: outputs exactly one space
                // - \\[dim] with next leading space: preserve one space
                // - \\ without dim, or next has no leading space: collapse all
                // - \newline: collapse all
                if (restricted_h_mode_) {
                    // \linebreak command (not \\) outputs exactly one space
                    // Note: "linebreak_command" is \\ in tree-sitter, "linebreak" is \linebreak
                    bool is_linebreak_cmd = (strcmp(tag, "linebreak") == 0);
                    bool had_trailing_ws = gen_->hasTrailingWhitespace();
                    gen_->trimTrailingWhitespace();

                    if (is_linebreak_cmd) {
                        // \linebreak: always output exactly one space
                        gen_->text(" ");
                        strip_next_leading_space_ = true;
                        continue;
                    }

                    // Check for next text having leading whitespace
                    bool next_has_leading_ws = false;
                    for (int64_t j = i + 1; j < count; j++) {
                        ItemReader lookahead = elem_reader.childAt(j);
                        if (lookahead.isElement()) {
                            ElementReader la_elem(lookahead.item());
                            const char* la_tag = la_elem.tagName();
                            if (la_tag && strcmp(la_tag, "brack_group") == 0) {
                                continue; // Keep looking for next text
                            }
                            break; // Stop at any other element
                        } else if (lookahead.isString()) {
                            String* la_str = lookahead.asString();
                            if (la_str && la_str->len > 0) {
                                char first = la_str->chars[0];
                                next_has_leading_ws = (first == ' ' || first == '\t' || first == '\n' || first == '\r');
                            }
                            break;
                        }
                    }

                    // For \\[dim] with leading space after: preserve one space
                    // This handles cases like "one \\[4cm] space" where the dimension
                    // indicates intentional vertical space, and word separation should be preserved
                    // Note: has_dimension is set earlier when brack_group is consumed and i is incremented
                    // "linebreak" is old grammar, "linebreak_command" is new tree-sitter grammar for backslash-backslash
                    if ((strcmp(tag, "linebreak") == 0 || strcmp(tag, "linebreak_command") == 0) &&
                        has_dimension && had_trailing_ws && next_has_leading_ws) {
                        gen_->text(" ");
                    }
                    strip_next_leading_space_ = true;
                    continue;
                }

                // Output the linebreak
                ensureParagraph();;
                if (has_dimension) {
                    // Output span with class and style: <span class="breakspace" style="margin-bottom:X"></span>
                    // If font styling is active, wrap in font class span for proper em-unit sizing
                    char style[256];
                    if (preserve_unit) {
                        snprintf(style, sizeof(style), "margin-bottom:%s", dimension_text);
                    } else {
                        snprintf(style, sizeof(style), "margin-bottom:%.3fpx", dimension_px);
                    }

                    // Check if font styling is active - wrap breakspace in font class for em-relative sizing
                    std::string font_class = gen_->getFontClass(gen_->currentFont());
                    if (!font_class.empty()) {
                        gen_->span(font_class.c_str());
                        gen_->spanWithClassAndStyle("breakspace", style);
                        gen_->closeElement();  // close breakspace
                        gen_->closeElement();  // close font class wrapper
                    } else {
                        gen_->spanWithClassAndStyle("breakspace", style);
                        gen_->closeElement();
                    }
                } else {
                    gen_->lineBreak(false);
                }
                continue;
            }

            // Check if this is a diacritic command (^, ', `, ", ~, =, ., u, v, H, c, d, b, r, k)
            if (tag && isDiacriticCommand(tag)) {
                char diacritic_cmd = tag[0];

                // First check if the diacritic element has a curly_group child
                bool has_child_arg = false;
                std::string base_char;

                if (child_elem.childCount() > 0) {
                    // Diacritic has children - process first curly_group
                    auto dia_iter = child_elem.children();
                    ItemReader dia_child;
                    while (dia_iter.next(&dia_child)) {
                        if (dia_child.isElement()) {
                            ElementReader dia_child_elem(dia_child.item());
                            const char* dia_child_tag = dia_child_elem.tagName();
                            if (dia_child_tag && strcmp(dia_child_tag, "curly_group") == 0) {
                                // Extract text from curly_group
                                Pool* pool = pool_;
                                StringBuf* sb = stringbuf_new(pool);
                                dia_child_elem.textContent(sb);
                                String* str = stringbuf_to_string(sb);
                                if (str && str->len > 0) {
                                    base_char = std::string(str->chars, str->len);
                                    has_child_arg = true;
                                }
                                break;
                            }
                        } else if (dia_child.isString()) {
                            // Direct string child (like \^o where 'o' is direct child)
                            const char* text = dia_child.asString()->chars;
                            if (text && text[0] != '\0') {
                                int char_len = getUtf8CharLen((unsigned char)text[0]);
                                base_char = std::string(text, char_len);
                                has_child_arg = true;
                            }
                            break;
                        }
                    }
                }

                if (has_child_arg) {
                    // Apply diacritic to the extracted base character
                    ensureParagraph();
                    std::string result = applyDiacritic(diacritic_cmd, base_char.c_str());
                    gen_->text(result.c_str());
                    continue;
                }

                // Check next sibling for the base character
                if (i + 1 < count) {
                    ItemReader next_reader = elem_reader.childAt(i + 1);

                    if (next_reader.isElement()) {
                        ElementReader next_elem(next_reader.item());
                        const char* next_tag = next_elem.tagName();

                        if (next_tag && strcmp(next_tag, "curly_group") == 0) {
                            // Extract content from curly_group
                            Pool* pool = pool_;
                            StringBuf* sb = stringbuf_new(pool);
                            next_elem.textContent(sb);
                            String* str = stringbuf_to_string(sb);

                            if (str && str->len > 0) {
                                ensureParagraph();
                                std::string result = applyDiacritic(diacritic_cmd, str->chars);
                                gen_->text(result.c_str());
                            } else {
                                // Empty curly_group (like \^{}) - output diacritic char + ZWS
                                // ZWS after empty curly groups is part of LaTeX formatting
                                ensureParagraph();
                                gen_->text(tag);
                                gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
                            }
                            i++;  // Skip the curly_group (even if empty)
                            continue;
                        }

                        // Check if next element is a command that resolves to a special character
                        // This handles cases like \"\i (umlaut on dotless-i), \'\i, \^\j, etc.
                        // The parser converts \i to an element with tag "i", not "command"
                        if (next_tag) {
                            // Check if it's a special character command element
                            const char* base_char = nullptr;
                            if (strcmp(next_tag, "i") == 0) {
                                base_char = "ı";  // dotless-i
                            } else if (strcmp(next_tag, "j") == 0) {
                                base_char = "ȷ";  // dotless-j
                            } else if (strcmp(next_tag, "l") == 0) {
                                base_char = "ł";  // Polish L
                            } else if (strcmp(next_tag, "L") == 0) {
                                base_char = "Ł";  // Polish L uppercase
                            } else if (strcmp(next_tag, "o") == 0) {
                                base_char = "ø";  // Scandinavian o
                            } else if (strcmp(next_tag, "O") == 0) {
                                base_char = "Ø";  // Scandinavian O uppercase
                            } else if (strcmp(next_tag, "ae") == 0) {
                                base_char = "æ";  // ae ligature
                            } else if (strcmp(next_tag, "AE") == 0) {
                                base_char = "Æ";  // AE ligature
                            } else if (strcmp(next_tag, "oe") == 0) {
                                base_char = "œ";  // oe ligature
                            } else if (strcmp(next_tag, "OE") == 0) {
                                base_char = "Œ";  // OE ligature
                            } else if (strcmp(next_tag, "command") == 0) {
                                // Also check if it's a command node with name extraction
                                Pool* pool = pool_;
                                StringBuf* sb = stringbuf_new(pool);
                                next_elem.textContent(sb);
                                String* str = stringbuf_to_string(sb);
                                if (str && str->len > 0) {
                                    const char* cmd = str->chars;
                                    if (strcmp(cmd, "i") == 0) {
                                        base_char = "ı";
                                    } else if (strcmp(cmd, "j") == 0) {
                                        base_char = "ȷ";
                                    } else if (strcmp(cmd, "l") == 0) {
                                        base_char = "ł";
                                    } else if (strcmp(cmd, "L") == 0) {
                                        base_char = "Ł";
                                    } else if (strcmp(cmd, "o") == 0) {
                                        base_char = "ø";
                                    } else if (strcmp(cmd, "O") == 0) {
                                        base_char = "Ø";
                                    }
                                }
                            }

                            if (base_char) {
                                ensureParagraph();
                                std::string result = applyDiacritic(diacritic_cmd, base_char);
                                gen_->text(result.c_str());
                                // Strip trailing space (LaTeX command consumes following space)
                                setStripNextLeadingSpace(true);
                                i++;  // Skip the command
                                continue;
                            }
                        }
                    } else if (next_reader.isString()) {
                        // Next sibling is text - consume first character
                        const char* text = next_reader.asString()->chars;
                        if (text && text[0] != '\0') {
                            ensureParagraph();
                            int char_len = getUtf8CharLen((unsigned char)text[0]);
                            std::string first_char(text, char_len);
                            std::string result = applyDiacritic(diacritic_cmd, first_char.c_str());
                            gen_->text(result.c_str());

                            // Output remaining text
                            if (strlen(text) > (size_t)char_len) {
                                gen_->text(text + char_len);
                            }
                            i++;  // Skip the text node (we already processed it)
                            continue;
                        }
                    }
                }

                // No base character found - just output the diacritic as-is (e.g., \^{})
                ensureParagraph();
                gen_->text(tag);
                continue;
            }

        }

        // Check if this is a text node containing an embedded command followed by curly_group
        // This handles cases like \emph{text} where parser produces: ["\x1bmph", curly_group{text}]
        // The parser encodes \cmd as ESC (0x1B) + cmdname (e.g., \emph → 0x1B + "mph")
        // We need to combine them so the command handler can process the argument
        if (child_reader.isString()) {
            String* str = child_reader.asString();
            if (str && str->len > 0) {
                const char* text = str->chars;
                // Check if text contains an ESC byte (0x1B) indicating embedded command
                // The parser uses ESC to mark LaTeX commands
                const char* esc = strchr(text, '\x1b');
                if (esc && esc[1] != '\0') {
                    const char* cmd_start = esc + 1;
                    size_t cmd_len = 0;
                    while (cmd_start[cmd_len] && isalpha((unsigned char)cmd_start[cmd_len])) {
                        cmd_len++;
                    }

                    // Check if command ends at end of string (no trailing text after command name)
                    if (cmd_len > 0 && cmd_start[cmd_len] == '\0') {
                        // Text ends with a command - check if next sibling is curly_group
                        if (i + 1 < count) {
                            ItemReader next_reader = elem_reader.childAt(i + 1);
                            if (next_reader.isElement()) {
                                ElementReader next_elem(next_reader.item());
                                const char* next_tag = next_elem.tagName();

                                if (next_tag && strcmp(next_tag, "curly_group") == 0) {
                                    // Found command + curly_group pattern
                                    // Process text before command (if any)
                                    if (esc > text) {
                                        size_t prefix_len = esc - text;
                                        char* prefix = (char*)alloca(prefix_len + 1);
                                        memcpy(prefix, text, prefix_len);
                                        prefix[prefix_len] = '\0';
                                        processText(prefix);
                                    }

                                    // The command name after ESC might be missing the first letter
                                    // For \emph, we get ESC + "mph", but need to look up "emph"
                                    // Prepend 'e' to handle common \e... commands (\e → ESC)
                                    char* cmd_name_with_e = (char*)alloca(cmd_len + 2);
                                    cmd_name_with_e[0] = 'e';
                                    memcpy(cmd_name_with_e + 1, cmd_start, cmd_len);
                                    cmd_name_with_e[cmd_len + 1] = '\0';

                                    // Pass the curly_group as the command's element
                                    // This allows cmd_emph etc to call processChildren on it
                                    processCommand(cmd_name_with_e, next_reader.item());

                                    // Skip the curly_group sibling
                                    i++;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Normal processing for other nodes
        // Set up sibling context so command handlers can access following arguments
        int64_t consumed_count = 0;
        sibling_ctx_.parent_reader = &elem_reader;
        sibling_ctx_.current_index = i;
        sibling_ctx_.consumed_count = &consumed_count;

        processNode(child_reader.item());

        // Skip any siblings that were consumed by command handlers
        if (consumed_count > 0) {
            i += consumed_count;
        }

        // Clear sibling context
        sibling_ctx_ = {};

        // Check if previous node set pending ZWS output flag (curly_group at document level)
        if (pending_zws_output_) {
            bool had_trailing_space = pending_zws_had_trailing_space_;
            pending_zws_output_ = false;  // Clear flags
            pending_zws_had_trailing_space_ = false;

            // Only output ZWS if there's more contentful siblings after this
            // BUT not if there's a paragraph break (double newline) before the next content
            // ALSO not if group had trailing space AND next non-whitespace starts immediately
            bool has_following_content = false;
            bool next_is_plain_text = false;
            bool found_first_content = false;
            int consecutive_newlines = 0;  // Track newlines across siblings

            for (int64_t j = i + 1; j < count; j++) {
                ItemReader next_reader = elem_reader.childAt(j);

                // Check for paragraph break symbol
                if (next_reader.isSymbol()) {
                    const char* sym = next_reader.asSymbol()->chars;
                    if (strcmp(sym, "parbreak") == 0) {
                        // Paragraph break symbol - suppress ZWS
                        has_following_content = false;
                        goto done_checking_siblings;
                    }
                }

                if (next_reader.isString() || next_reader.isSymbol()) {
                    // Check strings and symbols (whitespace may be stored as symbols)
                    const char* next_text = next_reader.isString() ? next_reader.cstring() : (next_reader.isSymbol() ? next_reader.asSymbol()->chars : nullptr);
                    if (next_text && next_text[0] != '\0') {
                        // First check if this is a space-absorbing symbol (like TeX, LaTeX)
                        if (next_reader.isSymbol()) {
                            bool absorbs = commandAbsorbsSpace(next_text);
                            printf("[DEBUG] Symbol '%s', absorbs=%d\n", next_text, absorbs ? 1 : 0);
                            if (absorbs) {
                                // Next sibling is a space-absorbing command - suppress ZWS
                                printf("[DEBUG] Suppressing ZWS for symbol '%s'\n", next_text);
                                has_following_content = false;
                                goto done_checking_siblings;
                            }
                        }

                        // Count consecutive newlines to detect paragraph break
                        for (const char* p = next_text; *p; p++) {
                            if (*p == '\n') {
                                consecutive_newlines++;
                                if (consecutive_newlines >= 2) {
                                    // Found paragraph break - stop here, no ZWS needed
                                    has_following_content = false;
                                    goto done_checking_siblings;
                                }
                            } else if (*p != ' ' && *p != '\t' && *p != '\r') {
                                // Non-whitespace content found in string
                                has_following_content = true;
                                // Check if this is the first non-whitespace AND starts immediately
                                if (!found_first_content) {
                                    found_first_content = true;
                                    // Only "plain text" if it starts at string beginning (no leading space)
                                    if (p == next_text) {
                                        next_is_plain_text = true;
                                    }
                                }
                                goto done_checking_siblings;
                            } else {
                                // Space/tab/CR resets newline count (but not to zero if we had 1)
                                if (*p != '\r') {
                                    consecutive_newlines = 0;
                                }
                            }
                        }
                        // Continue to next sibling to keep checking
                    }
                } else if (next_reader.isElement()) {
                    // Element found - check if it's a space-absorbing command
                    // Don't output ZWS between two space-absorbing commands (e.g., \TeX \LaTeX)
                    ElementReader next_elem(next_reader.item());
                    const char* next_tag = next_elem.tagName();
                    bool is_space_absorbing_cmd = false;

                    // Check if the element tag itself is a space-absorbing command
                    // (e.g., tag="LaTeX" from symbol processing)
                    if (next_tag && commandAbsorbsSpace(next_tag)) {
                        is_space_absorbing_cmd = true;
                    }
                    // Otherwise, check if it's a "command" element containing a space-absorbing command
                    else if (next_tag && strcmp(next_tag, "command") == 0) {
                        // Check if this command absorbs space
                        auto cmd_iter = next_elem.children();
                        ItemReader cmd_child;
                        while (cmd_iter.next(&cmd_child)) {
                            if (cmd_child.isElement()) {
                                ElementReader cmd_child_elem(cmd_child.item());
                                const char* child_tag = cmd_child_elem.tagName();
                                if (child_tag && strcmp(child_tag, "command_name") == 0) {
                                    String* name_str = cmd_child_elem.get_string_attr("name");
                                    if (name_str && commandAbsorbsSpace(name_str->chars)) {
                                        is_space_absorbing_cmd = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    // Only output ZWS if no paragraph break and not a space-absorbing command
                    if (consecutive_newlines < 2 && !is_space_absorbing_cmd) {
                        has_following_content = true;
                        next_is_plain_text = false;
                    }
                    break;
                }
            }
            done_checking_siblings:

            // Suppress ZWS if: group had trailing space AND next is plain text
            // (the trailing space already provides word separation)
            if (had_trailing_space && next_is_plain_text) {
                has_following_content = false;
            }

            if (has_following_content) {
                ensureParagraph();
                // Output ZWS with current font styling (if any)
                std::string font_class = gen_->getFontClass(gen_->currentFont());
                if (!font_class.empty() && !inStyledSpan()) {
                    gen_->span(font_class.c_str());
                    gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
                    gen_->closeElement();
                } else {
                    gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
                }
            }
        }

        // =============================================================================
        // Zero-Width Space (ZWS) Marker Logic
        // =============================================================================
        // LaTeX commands consume following whitespace, but HTML needs explicit markers
        // to preserve word boundaries. Insert <span class="zws"> </span> after:
        // 1. Commands that absorb space (\LaTeX, \textbf, etc.)
        // 2. Empty curly groups {}
        // 3. Commands with empty arguments

        if (child_reader.isElement()) {
            ElementReader cmd_elem(child_reader.item());
            const char* cmd_tag = cmd_elem.tagName();

            bool needs_zws = false;

            // Check if this is a curly_group with no content
            if (cmd_tag && strcmp(cmd_tag, "curly_group") == 0) {
                auto group_iter = cmd_elem.children();
                ItemReader group_child;
                bool has_content = false;
                while (group_iter.next(&group_child)) {
                    if (group_child.isElement()) {
                        has_content = true;
                        break;
                    } else if (group_child.isString()) {
                        const char* str = group_child.cstring();
                        if (str && str[0] != '\0') {
                            // Check if it's not just whitespace
                            for (const char* p = str; *p; p++) {
                                if (!isspace(*p)) {
                                    has_content = true;
                                    break;
                                }
                            }
                            if (has_content) break;
                        }
                    }
                }
                if (!has_content) {
                    needs_zws = true;
                }
            }

            // Check if this is a command that absorbs space
            if (cmd_tag && strcmp(cmd_tag, "command") == 0) {
                // Get command name
                const char* cmd_name = nullptr;
                auto cmd_iter = cmd_elem.children();
                ItemReader cmd_child;
                while (cmd_iter.next(&cmd_child)) {
                    if (cmd_child.isElement()) {
                        ElementReader cmd_child_elem(cmd_child.item());
                        const char* child_tag = cmd_child_elem.tagName();
                        if (child_tag && strcmp(child_tag, "command_name") == 0) {
                            String* name_str = cmd_child_elem.get_string_attr("name");
                            if (name_str) {
                                cmd_name = name_str->chars;
                            }
                            break;
                        }
                    }
                }

                if (cmd_name && commandAbsorbsSpace(cmd_name)) {
                    // Check if next sibling is NOT a curly_group/brack_group (command arguments)
                    // If it has arguments, they'll handle their own spacing
                    bool has_following_arg = false;
                    if (i + 1 < count) {
                        ItemReader next_reader = elem_reader.childAt(i + 1);
                        if (next_reader.isElement()) {
                            ElementReader next_elem(next_reader.item());
                            const char* next_tag = next_elem.tagName();
                            if (next_tag && (strcmp(next_tag, "curly_group") == 0 ||
                                           strcmp(next_tag, "brack_group") == 0)) {
                                has_following_arg = true;
                            }
                        }
                    }

                    if (!has_following_arg) {
                        needs_zws = true;
                    }
                }
            }

            // Output ZWS marker if needed and next sibling is text or another command
            if (needs_zws && i + 1 < count) {
                // Skip whitespace-only strings to find next real content
                int64_t next_idx = i + 1;
                bool found_next = false;
                bool next_is_space_absorbing = false;

                while (next_idx < count && !found_next) {
                    ItemReader scan_reader = elem_reader.childAt(next_idx);

                    if (scan_reader.isString()) {
                        const char* text = scan_reader.cstring();
                        bool is_whitespace_only = true;
                        if (text) {
                            for (const char* p = text; *p; p++) {
                                if (!isspace(*p)) {
                                    is_whitespace_only = false;
                                    found_next = true;
                                    break;
                                }
                            }
                        }
                        if (!is_whitespace_only) {
                            break;  // Found non-whitespace text
                        }
                        next_idx++;
                    } else if (scan_reader.isElement()) {
                        // Check if it's a space-absorbing command
                        ElementReader scan_elem(scan_reader.item());
                        const char* scan_tag = scan_elem.tagName();
                        if (scan_tag && strcmp(scan_tag, "command") == 0) {
                            // Extract command name to check if space-absorbing
                            auto scan_iter = scan_elem.children();
                            ItemReader scan_child;
                            while (scan_iter.next(&scan_child)) {
                                if (scan_child.isElement()) {
                                    ElementReader scan_child_elem(scan_child.item());
                                    const char* child_tag = scan_child_elem.tagName();
                                    if (child_tag && strcmp(child_tag, "command_name") == 0) {
                                        String* name_str = scan_child_elem.get_string_attr("name");
                                        if (name_str && commandAbsorbsSpace(name_str->chars)) {
                                            next_is_space_absorbing = true;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        found_next = true;
                        break;
                    } else {
                        // Other types - consider found
                        found_next = true;
                        break;
                    }
                }

                // Only output ZWS if we found next content and it's NOT space-absorbing
                if (found_next && !next_is_space_absorbing) {
                    ensureParagraph();
                    // TEMPORARILY DISABLED TO TEST
                    // gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
                }
            }
        }

        // =============================================================================
        // Space Consumption Logic (Original)
        // =============================================================================
        // LaTeX behavior: commands consume following space, but with exceptions:
        // - \macro{} with empty braces FOLLOWING: outputs ZWS, does NOT consume space
        // - \macro (no args): consumes space
        // - \macro{arg} (with args): consumes space
        // - \begin{empty}...\end{empty}: outputs ZWS, does NOT consume space
        //
        // Key distinction: \empty{} has {} as a SIBLING (following element)
        //                  \gobbleO{} has {} as a CHILD (argument to command)
        if (child_reader.isElement()) {
            // Check if this is an "empty" command or "curly_group" - they output ZWS and preserve spaces
            ElementReader cmd_elem(child_reader.item());
            const char* cmd_name = cmd_elem.tagName();

            bool is_empty_cmd = (cmd_name && strcmp(cmd_name, "empty") == 0);
            bool is_curly_group = (cmd_name && strcmp(cmd_name, "curly_group") == 0);

            // Commands with arguments should not consume following space
            // Check if this command has any non-whitespace children
            bool has_content_child = false;
            auto child_iter = cmd_elem.children();
            ItemReader cmd_child;
            while (child_iter.next(&cmd_child)) {
                TypeId child_type = cmd_child.getType();
                if (child_type == LMD_TYPE_STRING) {
                    const char* str = cmd_child.cstring();
                    // Check if non-whitespace string
                    if (str && str[0] != '\0') {
                        bool is_whitespace_only = true;
                        for (const char* p = str; *p; p++) {
                            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                                is_whitespace_only = false;
                                break;
                            }
                        }
                        if (!is_whitespace_only) {
                            has_content_child = true;
                            break;
                        }
                    }
                } else if (child_type == LMD_TYPE_ELEMENT || child_type == LMD_TYPE_LIST) {
                    // Has element or list child - consider it a content child
                    has_content_child = true;
                    break;
                }
            }

            bool skip_space_consumption = is_empty_cmd || is_curly_group || has_content_child;

            // Check if NEXT sibling is an empty curly_group (e.g., "\empty {}")
            bool next_is_empty_curly = false;
            if (i + 1 < count) {
                ItemReader next_reader = elem_reader.childAt(i + 1);
                if (next_reader.isElement()) {
                    ElementReader next_elem(next_reader.item());
                    const char* next_tag = next_elem.tagName();
                    if (next_tag && strcmp(next_tag, "curly_group") == 0) {
                        // Check if the curly_group is empty (no children or only whitespace)
                        auto group_iter = next_elem.children();
                        ItemReader group_child;
                        bool has_content = false;
                        while (group_iter.next(&group_child)) {
                            if (group_child.isElement()) {
                                has_content = true;
                                break;
                            } else if (group_child.isString()) {
                                const char* str = group_child.cstring();
                                if (str && str[0] != '\0') {
                                    // Check if it's not just whitespace
                                    bool has_non_ws = false;
                                    for (const char* p = str; *p; p++) {
                                        if (!isspace(*p)) {
                                            has_non_ws = true;
                                            break;
                                        }
                                    }
                                    if (has_non_ws) {
                                        has_content = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!has_content) {
                            next_is_empty_curly = true;
                        }
                    }
                }
            }

            if (next_is_empty_curly) {
                // Next sibling is empty {}: output ZWS, skip the empty braces, don't consume space
                ensureParagraph();
                gen_->text("\u200B");
                // Skip the next child (the empty curly_group)
                i++;
            } else if (!skip_space_consumption && i + 1 < count) {
                // Standard behavior: consume following space (but NOT for empty command or curly_group)
                ItemReader next_reader = elem_reader.childAt(i + 1);
                if (next_reader.isString()) {
                    const char* next_text = next_reader.cstring();
                    if (next_text && (next_text[0] == ' ' || next_text[0] == '\t')) {
                        // Next sibling starts with whitespace - consume it by processing
                        // the text starting from position 1 instead of 0
                        if (next_text[1] != '\0') {
                            // Process remaining text after first space
                            processText(next_text + 1);
                        }
                        // Skip the next child since we processed it here
                        i++;
                        continue;
                    }
                }
            }
        }
    }
}

void LatexProcessor::processSpacingCommand(Item elem) {
    ElementReader reader(elem);

    // Get the command field which contains the actual spacing command string
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* cmd = child.asString()->chars;
            ensureParagraph();

            if (strcmp(cmd, "\\,") == 0 || strcmp(cmd, "\\thinspace") == 0) {
                // Thin space (1/6 em) - use Unicode thin space U+2009
                gen_->text("\u2009");
            } else if (strcmp(cmd, "\\!") == 0 || strcmp(cmd, "\\negthinspace") == 0) {
                // Negative thin space - use empty span with class
                gen_->span("negthinspace");
                gen_->closeElement();
            } else if (strcmp(cmd, "\\;") == 0 || strcmp(cmd, "\\thickspace") == 0) {
                // Thick space (5/18 em) - use em space U+2003
                gen_->text("\u2003");
            } else if (strcmp(cmd, "\\:") == 0 || strcmp(cmd, "\\medspace") == 0) {
                // Medium space (2/9 em) - use en space U+2002
                gen_->text("\u2002");
            } else if (strcmp(cmd, "\\enspace") == 0) {
                // en-space (0.5 em) - use en space U+2002
                gen_->text("\u2002");
            } else if (strcmp(cmd, "\\quad") == 0) {
                // quad space (1 em) - use em space U+2003
                gen_->text("\u2003");
            } else if (strcmp(cmd, "\\qquad") == 0) {
                // qquad space (2 em) - use two em spaces
                gen_->text("\u2003\u2003");
            } else if (strcmp(cmd, "\\space") == 0) {
                // Normal space
                gen_->text(" ");
            } else if (strcmp(cmd, "\\ ") == 0 || strcmp(cmd, "\\\t") == 0 ||
                       strcmp(cmd, "\\\n") == 0 || strcmp(cmd, "\\\r") == 0) {
                // Backslash-space/tab/newline (control space) - produces zero-width space + regular space
                // The ZWS allows line breaking, the space provides word separation
                gen_->text("\u200B ");  // ZWSP + space (per LaTeX.js behavior)
            } else if (strcmp(cmd, "~") == 0) {
                // Tilde (non-breaking space) - this path shouldn't be reached since
                // ~ is handled as nbsp element, but keep for completeness
                gen_->writer()->writeRawHtml("&nbsp;");
            } else if (strcmp(cmd, "\\/") == 0) {
                // Ligature break - insert zero-width non-joiner U+200C
                // Prevents ligatures from forming, e.g., shelf\/ful prevents "shelfful" from becoming "shelﬀul"
                gen_->text("\xE2\x80\x8C");  // U+200C zero-width non-joiner
            } else if (strcmp(cmd, "\\@") == 0) {
                // Inter-sentence space marker - output nothing (it's just a marker for TeX)
            } else if (strcmp(cmd, "\\-") == 0) {
                // Discretionary hyphen - soft hyphen U+00AD
                gen_->text("\xC2\xAD");  // UTF-8 encoding of U+00AD
            }
            break;
        }
    }
}

// Output text with special handling for non-breaking space (U+00A0)
// which must be output as &nbsp; HTML entity
void LatexProcessor::outputTextWithSpecialChars(const char* text) {
    if (!text || *text == '\0') return;

    const char* p = text;
    const char* segment_start = p;

    while (*p) {
        // Check for UTF-8 encoded U+00A0 (non-breaking space): 0xC2 0xA0
        if ((unsigned char)*p == 0xC2 && (unsigned char)*(p+1) == 0xA0) {
            // Output any text before this nbsp
            if (p > segment_start) {
                std::string segment(segment_start, p - segment_start);
                gen_->text(segment.c_str());
            }
            // Output nbsp as HTML entity
            gen_->writer()->writeRawHtml("&nbsp;");
            p += 2;  // Skip the 2-byte UTF-8 sequence
            segment_start = p;
        } else {
            // Regular character - advance by UTF-8 char length
            unsigned char c = (unsigned char)*p;
            if (c < 0x80) {
                p++;  // 1-byte ASCII
            } else if (c < 0xE0) {
                p += 2;  // 2-byte UTF-8
            } else if (c < 0xF0) {
                p += 3;  // 3-byte UTF-8
            } else {
                p += 4;  // 4-byte UTF-8
            }
        }
    }

    // Output any remaining text
    if (p > segment_start) {
        gen_->text(segment_start);
    }
}

void LatexProcessor::processText(const char* text) {
    if (!text) return;

    // Skip EMPTY_STRING sentinel ("lambda.nil")
    if (strlen(text) == 10 && strncmp(text, "lambda.nil", 10) == 0) {
        return;
    }

    // Debug: log text content to see what's being processed
    log_debug("processText: '%s' (len=%zu, in_paragraph=%d)", text, strlen(text), in_paragraph_ ? 1 : 0);

    // Note: ZWS output is handled by processChildren() which checks for following content
    // Don't output ZWS here - let the sibling loop decide based on context

    // In restricted h-mode, check if first text starts with newline (latex-js adds ZWS + space)
    // This distinguishes "\mbox{\n..." from "\mbox{ ..."
    // Only applies to the very first text in restricted h-mode
    bool add_leading_zws = false;
    if (restricted_h_mode_first_text_) {
        restricted_h_mode_first_text_ = false;  // Clear flag - only check first text
        // Check if original text starts with newline (not space)
        if (text[0] == '\n' || (text[0] == '\r' && text[1] == '\n')) {
            add_leading_zws = true;
        }
    }

    // Normalize whitespace: collapse multiple spaces/newlines/tabs to single space
    // This matches LaTeX behavior where whitespace is collapsed
    std::string normalized;
    bool in_whitespace = false;

    for (const char* p = text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (!in_whitespace) {
                // First whitespace character: keep it as a space
                normalized += ' ';
                in_whitespace = true;
            }
            // Skip additional consecutive whitespace
        } else {
            // Non-whitespace character
            normalized += *p;
            in_whitespace = false;
        }
    }

    // Process LaTeX ^^ notation for special characters
    // This must be done before other text transformations
    normalized = processHatNotation(normalized.c_str());

    // Convert ASCII apostrophe (') to right single quotation mark (')
    // Also handles dash ligatures (-- → en-dash, --- → em-dash)
    // And single hyphen → Unicode hyphen (U+2010)
    // In monospace mode, dash/ligature conversions are skipped
    normalized = convertApostrophes(normalized.c_str(), inMonospaceMode());

    // Note: Automatic hyphenation (soft hyphen insertion) is disabled
    // Tests expect exact text without soft hyphens (U+00AD)
    // Hyphenation can be manually added with \- command if needed

    // Check if result is pure whitespace (multiple spaces/newlines)
    // Don't skip single spaces - they're significant in inline content
    bool all_whitespace = true;
    for (char c : normalized) {
        if (c != ' ') {
            all_whitespace = false;
            break;
        }
    }

    // Skip if only whitespace AND more than one space (e.g., paragraph breaks)
    // Keep single spaces as they're part of inline formatting
    if (all_whitespace && normalized.length() > 1) {
        return;
    }

    // Skip whitespace (even single space) if not inside a paragraph
    // This prevents empty paragraphs when whitespace appears between \par and parbreak
    if (all_whitespace && !in_paragraph_) {
        return;
    }

    // Trim leading whitespace if starting a new paragraph (LaTeX ignores leading space)
    if (!in_paragraph_ && !normalized.empty() && normalized[0] == ' ') {
        normalized = normalized.substr(1);
    }

    ensureParagraph();  // Auto-wrap text in <p> tags

    // Check if we should strip leading space (set by font declaration commands)
    // LaTeX commands consume their trailing space, so text after a declaration
    // or special character command should have its leading space stripped
    bool should_strip_leading = strip_next_leading_space_;
    strip_next_leading_space_ = false;  // Reset flag after checking

    // Also strip leading space if the output already ends with whitespace
    // This prevents consecutive spaces from adjacent text nodes
    if (!should_strip_leading && gen_->hasTrailingWhitespace() &&
        !normalized.empty() && normalized[0] == ' ') {
        should_strip_leading = true;
    }

    // Strip leading space if flagged (applies to all text, not just font-styled)
    if (should_strip_leading && !normalized.empty() && normalized[0] == ' ') {
        size_t start = 0;
        while (start < normalized.length() && normalized[start] == ' ') {
            start++;
        }
        if (start > 0) {
            normalized = normalized.substr(start);
            // Recalculate all_whitespace after trimming
            if (normalized.empty()) {
                return;
            }
            all_whitespace = true;
            for (char c : normalized) {
                if (c != ' ') {
                    all_whitespace = false;
                    break;
                }
            }
        }
    }

    // Check if current font differs from default - if so, wrap text in a span
    // BUT skip this if we're inside a styled span (like \textbf{}) to prevent double-wrapping
    // When inside a font environment, use only the innermost environment's class (not accumulated)
    std::string font_class;
    if (inFontEnv()) {
        // Inside a font environment: use only the innermost environment's class
        font_class = currentFontEnvClass();
    } else {
        // Not in a font environment: use accumulated font class
        font_class = gen_->getFontClass(gen_->currentFont());
    }

    // Output leading ZWS if needed (for restricted h-mode with leading newline)
    // This is output before the text content, creating "ZWS + space + text" pattern
    if (add_leading_zws && !normalized.empty() && normalized[0] == ' ') {
        gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
    }

    if (!font_class.empty() && !inStyledSpan()) {
        // When font styling is active
        if (all_whitespace) {
            // Skip pure whitespace when font styling is active
            return;
        }

        // Wrap content in span with font class
        if (!normalized.empty()) {
            gen_->span(font_class.c_str());
            outputTextWithSpecialChars(normalized.c_str());
            gen_->closeElement();
        }
    } else if (!all_whitespace || normalized.length() == 1) {
        // Normal text or single space - output with special character handling
        outputTextWithSpecialChars(normalized.c_str());
    }
    // Skip pure whitespace (more than one space) - already handled above
}

void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Handle brack_group at top level - this is literal [] text, not an optional argument
    // When [] appears outside a command context, it's just literal text
    if (strcmp(cmd_name, "brack_group") == 0) {
        ensureParagraph();
        gen_->text("[");
        processChildren(elem);
        gen_->text("]");
        return;
    }

    // Handle curly_group (TeX brace groups) - important for font scoping
    // Groups in TeX (e.g., { \bfseries text }) limit the scope of declarations
    // latex.js adds zero-width space (U+200B) at group boundaries for visual separation
    if (strcmp(cmd_name, "curly_group") == 0) {
        gen_->enterGroup();

        // Save alignment state - alignment declarations are scoped to groups
        // When we enter a group, save current alignment so we can restore it on exit
        pushAlignmentScope();

        // Save strip_next_leading_space_ flag - it should not persist beyond group scope
        // Commands like \ignorespaces only affect whitespace in the current group
        bool saved_strip_flag = strip_next_leading_space_;
        strip_next_leading_space_ = false;  // Reset for group content

        // Reset the group ZWS suppression flag for this group
        group_suppresses_zws_ = false;

        // Check if group is empty (has no children or only whitespace)
        ElementReader reader(elem);
        bool is_empty_group = true;
        bool has_leading_space = false;
        bool has_trailing_space = false;

        // Iterate through children to check emptiness and whitespace
        auto iter = reader.children();
        ItemReader child;
        ItemReader last_string_child;
        bool found_last_string = false;
        bool is_first = true;

        while (iter.next(&child)) {
            if (child.isString()) {
                String* str = child.asString();
                if (str && str->len > 0) {
                    // Check for non-whitespace content
                    for (int64_t i = 0; i < str->len; i++) {
                        if (str->chars[i] != ' ' && str->chars[i] != '\t' && str->chars[i] != '\n') {
                            is_empty_group = false;
                            break;
                        }
                    }
                    // Check first child for leading space
                    if (is_first && str->chars[0] == ' ') {
                        has_leading_space = true;
                    }
                    // Track last string for trailing space check
                    last_string_child = child;
                    found_last_string = true;
                }
            } else if (child.isElement()) {
                // Any element makes the group non-empty
                is_empty_group = false;
            }
            is_first = false;
        }

        // Check trailing space
        if (found_last_string) {
            String* str = last_string_child.asString();
            if (str && str->len > 0 && str->chars[str->len - 1] == ' ') {
                has_trailing_space = true;
            }
        }

        // Output ZWS at entry if leading space (and not empty group)
        if (has_leading_space && !is_empty_group) {
            ensureParagraph();
            gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
        }

        // For empty groups with only spaces, output ZWS separated by spaces
        // e.g., {  } → ZWS + space + ZWS, {   } → ZWS + space + ZWS + space + ZWS
        // Pattern: N spaces → N ZWS markers with (N-1) spaces between them
        if (is_empty_group && (has_leading_space || has_trailing_space)) {
            // Count total spaces first
            int space_count = 0;
            auto count_iter = reader.children();
            ItemReader count_child;
            while (count_iter.next(&count_child)) {
                if (count_child.isString()) {
                    String* str = count_child.asString();
                    if (str && str->len > 0) {
                        for (int64_t i = 0; i < str->len; i++) {
                            if (str->chars[i] == ' ' || str->chars[i] == '\t') {
                                space_count++;
                            }
                        }
                    }
                }
            }

            // Output ZWS markers separated by spaces
            if (space_count > 0) {
                ensureParagraph();
                for (int i = 0; i < space_count; i++) {
                    gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
                    if (i < space_count - 1) {
                        gen_->text(" ");  // Space between ZWS markers
                    }
                }
            }
        } else {
            // Normal processing for non-empty groups or truly empty groups
            processChildren(elem);
        }
        gen_->exitGroup();

        // Restore alignment state after exiting group
        popAlignmentScope();

        // Restore strip_next_leading_space_ flag after group
        strip_next_leading_space_ = saved_strip_flag;

        // ZWS at exit for all curly groups
        // LaTeX groups break word concatenation, so output ZWS at boundaries
        // processChildren will check for paragraph breaks and suppress if needed
        int depth_after_exit = gen_->groupDepth();
        bool should_output_zws = false;

        // At any depth, curly groups should output ZWS to preserve word boundaries
        if (is_empty_group && (has_leading_space || has_trailing_space)) {
            // Empty groups with spaces were already handled above
            should_output_zws = false;
        } else if (is_empty_group) {
            // Truly empty groups serve as terminators (e.g., \^{})
            should_output_zws = true;
        } else {
            // Non-empty groups: output ZWS to mark word boundary
            // processChildren will check for paragraph breaks
            should_output_zws = true;
        }

        // Don't output ZWS if we're at paragraph end or if paragraph is about to close
        // Signal to processChildren that ZWS should be output after this node
        // if there's more contentful siblings
        // ALSO don't output ZWS if group contains whitespace-control commands
        if (should_output_zws && !group_suppresses_zws_) {
            pending_zws_output_ = true;
            pending_zws_had_trailing_space_ = has_trailing_space;
        }

        // Reset the flag for next group
        group_suppresses_zws_ = false;

        return;
    }

    // Handle document wrapper (from \begin{document}...\end{document})
    if (strcmp(cmd_name, "document") == 0) {
        // Just process children - document is a transparent container
        processChildren(elem);
        return;
    }

    // Handle paragraph wrapper (transparent container for text content)
    if (strcmp(cmd_name, "paragraph") == 0) {
        processChildren(elem);
        return;
    }

    // Handle Tree-sitter special node types that should be silent
    if (strcmp(cmd_name, "class_include") == 0) {
        cmd_documentclass(this, elem);
        return;
    }
    if (strcmp(cmd_name, "package_include") == 0) {
        cmd_usepackage(this, elem);
        return;
    }
    if (strcmp(cmd_name, "counter_value") == 0) {
        cmd_value(this, elem);
        return;
    }

    // Handle macro definition elements specially (from Tree-sitter)
    if (strcmp(cmd_name, "new_command_definition") == 0 || strcmp(cmd_name, "newcommand") == 0) {
        cmd_newcommand(this, elem);
        return;
    }

    // Handle macro argument wrapper (transparent - just process children)
    if (strcmp(cmd_name, "arg") == 0) {
        processChildren(elem);
        return;
    }
    if (strcmp(cmd_name, "renew_command_definition") == 0 || strcmp(cmd_name, "renewcommand") == 0) {
        cmd_renewcommand(this, elem);
        return;
    }
    if (strcmp(cmd_name, "provide_command_definition") == 0 || strcmp(cmd_name, "providecommand") == 0) {
        cmd_providecommand(this, elem);
        return;
    }
    if (strcmp(cmd_name, "def_definition") == 0) {
        cmd_def(this, elem);
        return;
    }

    // Check if single-character command that's a literal escape sequence
    // Diacritic commands (', `, ^, ~, ", =, ., etc.) are NOT escape sequences
    // Escape sequences are: %, &, $, #, _, {, }, \, @, /, -, etc.
    if (strlen(cmd_name) == 1) {
        char c = cmd_name[0];
        // Diacritics that should be processed as commands
        if (c == '\'' || c == '`' || c == '^' || c == '~' || c == '"' ||
            c == '=' || c == '.' || c == 'u' || c == 'v' || c == 'H' ||
            c == 't' || c == 'c' || c == 'd' || c == 'b' || c == 'r' || c == 'k') {
            // Fall through to command processing
        } else if (c == 'i' || c == 'j' || c == 'l' || c == 'L' ||
                   c == 'o' || c == 'O') {
            // Special characters: dotless i/j, Polish l/L, Scandinavian o/O
            // Fall through to command processing
        } else {
            // Literal escaped character - output as text
            processText(cmd_name);
            return;
        }
    }

    // Check if this is a user-defined macro
    if (isMacro(cmd_name)) {
        log_debug("Processing macro invocation: %s (depth=%d)", cmd_name, recursion_depth_);
        MacroDefinition* macro = getMacro(cmd_name);
        if (macro && macro->definition) {
            // Extract arguments from the command element
            // For user-defined macros like \greet{Alice}, the {Alice} argument
            // is parsed as direct children of the 'greet' element, not as curly_group
            std::vector<Element*> args;
            ElementReader reader(elem);

            // Collect all children as macro arguments (up to num_params)
            // Wrap each sequence of children into a temporary element
            MarkBuilder builder(input_);
            auto iter = reader.children();
            ItemReader child;
            int args_collected = 0;

            // Check if first child is a brack_group (optional arg override)
            // If so, use it for #1 instead of default; otherwise use default if available
            bool first_is_optional = false;

            fprintf(stderr, "DEBUG: Macro %s needs %d params, has %lld children\n", cmd_name, macro->num_params, reader.childCount());

            // Peek at first child to check if it's a brack_group (optional arg)
            ItemReader peek_child;
            auto peek_iter = reader.children();
            if (peek_iter.next(&peek_child) && peek_child.isElement()) {
                ElementReader peek_elem(peek_child.item());
                const char* peek_tag = peek_elem.tagName();
                if (peek_tag && strcmp(peek_tag, "brack_group") == 0) {
                    first_is_optional = true;
                    fprintf(stderr, "DEBUG: Macro %s first arg is optional brack_group\n", cmd_name);
                }
            }

            while (iter.next(&child) && args_collected < macro->num_params) {
                TypeId child_type = child.getType();
                fprintf(stderr, "DEBUG:   Child %d: type=%d\n", args_collected, child_type);

                // Create a wrapper element for this argument
                ElementBuilder arg_elem = builder.element("arg");

                // Check if this is a brack_group (optional arg) - extract content
                if (child.isElement()) {
                    ElementReader child_elem(child.item());
                    const char* child_tag = child_elem.tagName();
                    if (child_tag && strcmp(child_tag, "brack_group") == 0) {
                        // Extract content from brack_group for first optional parameter
                        auto brack_iter = child_elem.children();
                        ItemReader brack_child;
                        while (brack_iter.next(&brack_child)) {
                            arg_elem.child(brack_child.item());
                        }
                        Item arg_item = arg_elem.final();
                        args.push_back((Element*)arg_item.item);
                        args_collected++;
                        continue;
                    }
                }

                // Add the child to the wrapper
                arg_elem.child(child.item());

                // Store the wrapped argument
                Item arg_item = arg_elem.final();
                args.push_back((Element*)arg_item.item);
                args_collected++;
            }

            fprintf(stderr, "DEBUG: Macro %s collected %d/%d args\n", cmd_name, args_collected, macro->num_params);

            // If we have fewer args than num_params and there's a default value,
            // prepend the default value as the first argument
            // This handles \newcommand{\cmd}[2][default]{#1 #2} when called as \cmd{arg}
            if ((int)args.size() < macro->num_params && macro->default_value != nullptr && !first_is_optional) {
                fprintf(stderr, "DEBUG: Macro %s using default value for first param\n", cmd_name);
                // Prepend default value to args
                std::vector<Element*> new_args;
                new_args.push_back(macro->default_value);
                for (Element* arg : args) {
                    new_args.push_back(arg);
                }
                args = new_args;
                fprintf(stderr, "DEBUG: Macro %s now has %zu args after adding default\n", cmd_name, args.size());
            }

            // Expand the macro with arguments
            Element* expanded = expandMacro(cmd_name, args);
            if (expanded) {
                log_debug("Macro %s expanded with %zu args", cmd_name, args.size());
                // Process the expanded content
                Item expanded_item;
                expanded_item.item = (uint64_t)expanded;
                processNode(expanded_item);
                return;
            }
        }
    }

    // Handle block vs inline commands differently
    // In restricted horizontal mode (inside \mbox), block commands should NOT close paragraph
    // Also, when inside a styled span (like \emph{}), block commands should stay inline
    // to allow CSS to handle the visual appearance (per LaTeX.js behavior)
    if (isBlockCommand(cmd_name) && !restricted_h_mode_ && !inStyledSpan()) {
        // Close paragraph before block commands (only when not in inline context)
        closeParagraphIfOpen();
    } else if (isInlineCommand(cmd_name)) {
        // Ensure paragraph is open before inline commands
        ensureParagraph();
        // Track that we're entering an inline element
        inline_depth_++;
    } else if (strcmp(cmd_name, "\\") == 0 ||
               strcmp(cmd_name, "newline") == 0 ||
               strcmp(cmd_name, "linebreak") == 0) {
        // Line breaks: ensure paragraph but don't affect nesting depth
        ensureParagraph();
    }

    // Check for \the<counter> commands (e.g., \thec, \thesection)
    // These are automatically created by \newcounter{name} and format the counter
    if (strncmp(cmd_name, "the", 3) == 0 && strlen(cmd_name) > 3) {
        const char* counter_name = cmd_name + 3;  // Skip "the" prefix
        if (gen_->hasCounter(counter_name)) {
            // Format counter as arabic (default representation)
            int value = gen_->getCounter(counter_name);
            std::string output = gen_->formatArabic(value);
            ensureParagraph();
            gen_->text(output.c_str());
            return;
        }
        // Counter doesn't exist - fall through to unknown command handling
    }

    // Look up command in table
    auto it = command_table_.find(cmd_name);
    if (it != command_table_.end()) {
        // debug: log command dispatch
        FILE* debugf = fopen("/tmp/latex_debug.txt", "a");
        if (debugf) {
            fprintf(debugf, "processCommand: dispatching '%s'\n", cmd_name);
            fclose(debugf);
        }

        // Call command handler
        it->second(this, elem);

        if (isInlineCommand(cmd_name)) {
            inline_depth_--;
        }
        return;
    }

    // Look up symbol in package registry (for symbols from textgreek, textcomp, gensymb, etc.)
    const char* symbol = PackageRegistry::instance().lookupSymbol(cmd_name);
    if (symbol && *symbol) {
        log_debug("Symbol lookup: %s -> %s", cmd_name, symbol);
        ensureParagraph();
        gen_->text(symbol);
        return;
    }

    // Unknown command - just output children
    log_debug("Unknown command: %s - processing children", cmd_name);  // DEBUG
    processChildren(elem);
}

// =============================================================================
// Main Entry Point
// =============================================================================

Item format_latex_html_v2(Input* input, bool text_mode) {
    // debug: confirm this function is being called
    FILE* debugf = fopen("/tmp/latex_debug.txt", "a");
    if (debugf) {
        fprintf(debugf, "format_latex_html_v2: ENTRY text_mode=%d\n", text_mode ? 1 : 0);
        fclose(debugf);
    }

    if (!input || !input->root.item) {
        log_error("format_latex_html_v2: invalid input");
        Item result;
        result.item = ITEM_NULL;
        return result;
    }

    Pool* pool = input->pool;

    // ==========================================================================
    // PASS 1: Label collection (forward reference resolution)
    // Process the document with a null writer to collect all labels
    // ==========================================================================
    NullHtmlWriter null_writer;
    HtmlGenerator label_gen(pool, &null_writer);
    LatexProcessor label_proc(&label_gen, pool, input);

    // Process to collect labels (output is discarded)
    label_proc.process(input->root);

    // ==========================================================================
    // PASS 2: HTML generation with all labels available
    // ==========================================================================

    // Create HTML writer for actual output
    HtmlWriter* writer = nullptr;
    if (text_mode) {
        // Text mode - generate HTML string
        // Disable pretty print to avoid extra newlines in output
        writer = new TextHtmlWriter(pool, false);  // Pretty-printing not needed since tests normalize whitespace
    } else {
        // Node mode - generate Element tree
        writer = new NodeHtmlWriter(input);
    }

    // Create HTML generator and copy labels from first pass
    HtmlGenerator gen(pool, writer);
    gen.copyLabelsFrom(label_gen);

    // Create processor
    LatexProcessor proc(&gen, pool, input);

    // Start HTML document container (using "body" class for LaTeX.js compatibility)
    writer->openTag("div", "body");

    // Process LaTeX tree
    proc.process(input->root);

    // Close any open paragraph at end of document
    proc.closeParagraphIfOpen();

    // Close HTML document container
    writer->closeTag("div");

    // Output margin paragraphs if any were collected
    if (proc.hasMarginParagraphs()) {
        proc.writeMarginParagraphs(writer);
    }

    // Get result
    Item result = writer->getResult();

    // Cleanup
    delete writer;

    return result;
}

// =============================================================================
// Full HTML Document Generation (with CSS and fonts)
// =============================================================================

std::string format_latex_html_v2_document(Input* input, const char* doc_class,
                                           const char* asset_base_url, bool embed_css) {
    // Generate complete HTML document with CSS and fonts
    // asset_base_url: base URL for CSS/font files (e.g., "https://cdn.example.com/latex-assets/")
    // embed_css: if true, embed CSS inline; if false, use <link> tags

    log_info("format_latex_html_v2_document called: doc_class=%s, asset_url=%s, embed=%d",
             doc_class ? doc_class : "(null)", asset_base_url ? asset_base_url : "(null)", embed_css);

    if (!input || !input->root.item) {
        log_error("format_latex_html_v2_document: invalid input");
        return "";
    }

    // Reset package registry for fresh document processing
    PackageRegistry::instance().reset();

    // Get HTML body content
    Item body_content = format_latex_html_v2(input, true);
    TypeId body_type = get_type_id(body_content);
    if (body_type != LMD_TYPE_STRING) {
        log_error("format_latex_html_v2_document: failed to generate body content, got type %d", body_type);
        return "";
    }

    String* body_str = body_content.get_string();
    std::string body_html(body_str->chars, body_str->len);

    // Extract document title from the body content
    // Look for <div class="title">...</div> and extract plain text
    std::string doc_title = "LaTeX Document";  // Default title
    {
        size_t title_start = body_html.find("<div class=\"title\">");
        if (title_start != std::string::npos) {
            title_start += 19;  // Skip past the opening tag
            size_t title_end = body_html.find("</div>", title_start);
            if (title_end != std::string::npos) {
                std::string title_html = body_html.substr(title_start, title_end - title_start);
                // Strip HTML tags to get plain text
                std::string plain_title;
                bool in_tag = false;
                for (char c : title_html) {
                    if (c == '<') {
                        in_tag = true;
                    } else if (c == '>') {
                        in_tag = false;
                    } else if (!in_tag) {
                        plain_title += c;
                    }
                }
                // Trim whitespace
                size_t start = plain_title.find_first_not_of(" \t\n\r");
                size_t end = plain_title.find_last_not_of(" \t\n\r");
                if (start != std::string::npos && end != std::string::npos) {
                    doc_title = plain_title.substr(start, end - start + 1);
                }
            }
        }
    }

    // Configure asset loading
    LatexAssetConfig config;
    if (asset_base_url && *asset_base_url) {
        config.mode = AssetMode::LINK;
        config.base_url = asset_base_url;
    } else if (embed_css) {
        config.mode = AssetMode::EMBED;
    } else {
        config.mode = AssetMode::LINK;
    }
    config.asset_dir = LatexAssets::getDefaultAssetDir();

    // Determine document class
    std::string docclass = doc_class ? doc_class : "article";

    // Get head content
    std::string head_content = LatexAssets::generateHeadContent(docclass.c_str(), config);

    // Build complete HTML document
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n";
    oss << "<html lang=\"en\">\n";
    oss << "<head>\n";
    oss << "  <meta charset=\"UTF-8\">\n";
    oss << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    oss << "  <title>" << doc_title << "</title>\n";
    oss << head_content;
    oss << "</head>\n";
    oss << "<body>\n";
    oss << body_html;
    oss << "</body>\n";
    oss << "</html>\n";

    std::string result = oss.str();
    log_info("format_latex_html_v2_document: generated %zu bytes, starts with: %.80s",
              result.size(), result.c_str());
    return result;
}

} // namespace lambda

// C API for compatibility with existing code

extern "C" {

Item format_latex_html_v2_c(Input* input, int text_mode) {
    log_debug("format_latex_html_v2_c called, text_mode=%d", text_mode);
    return lambda::format_latex_html_v2(input, text_mode != 0);
}

// Generate complete HTML document with CSS - returns allocated C string (caller must free)
const char* format_latex_html_v2_document_c(Input* input, const char* doc_class,
                                             const char* asset_base_url, int embed_css) {
    std::string result = lambda::format_latex_html_v2_document(input, doc_class,
                                                                 asset_base_url, embed_css != 0);
    if (result.empty()) {
        return nullptr;
    }
    // Allocate copy in input's arena for memory management
    char* copy = (char*)arena_alloc(input->arena, result.size() + 1);
    memcpy(copy, result.c_str(), result.size() + 1);
    return copy;
}

} // extern "C"
