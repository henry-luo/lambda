// latex_packages.cpp - Package system implementation
// Implements textgreek, textcomp, gensymb, hyperref, multicol, stix, etc.

#include "latex_packages.hpp"
#include <algorithm>
#include <cstring>

namespace lambda {

// =============================================================================
// Base symbols (from symbols.ls) - always available
// =============================================================================

static const std::map<std::string, std::string> BASE_SYMBOLS = {
    // Spaces
    {"space", " "},
    {"nobreakspace", "\u00A0"},   // ~
    {"thinspace", "\u2009"},
    {"enspace", "\u2002"},
    {"enskip", "\u2002"},
    {"quad", "\u2003"},
    {"qquad", "\u2003\u2003"},
    {"textvisiblespace", "\u2423"},
    {"textcompwordmark", "\u200C"},
    
    // Basic Latin
    {"textdollar", "$"},
    {"$", "$"},
    {"slash", "/"},
    {"textless", "<"},
    {"textgreater", ">"},
    {"textbackslash", "\\"},
    {"textasciicircum", "^"},
    {"textunderscore", "_"},
    {"_", "_"},
    {"lbrack", "["},
    {"rbrack", "]"},
    {"textbraceleft", "{"},
    {"{", "{"},
    {"textbraceright", "}"},
    {"}", "}"},
    {"textasciitilde", "˜"},
    
    // Non-ASCII letters
    {"AA", "Å"},
    {"aa", "å"},
    {"AE", "Æ"},
    {"ae", "æ"},
    {"OE", "Œ"},
    {"oe", "œ"},
    {"DH", "Ð"},
    {"dh", "ð"},
    {"DJ", "Đ"},
    {"dj", "đ"},
    {"NG", "Ŋ"},
    {"ng", "ŋ"},
    {"TH", "Þ"},
    {"th", "þ"},
    {"O", "Ø"},
    {"o", "ø"},
    {"i", "ı"},
    {"j", "ȷ"},
    {"L", "Ł"},
    {"l", "ł"},
    {"IJ", "Ĳ"},
    {"ij", "ĳ"},
    {"SS", "ẞ"},
    {"ss", "ß"},
    
    // Quotes
    {"textquotesingle", "'"},
    {"textquoteleft", "\xe2\x80\x98"},    // '
    {"lq", "\xe2\x80\x98"},               // '
    {"textquoteright", "\xe2\x80\x99"},   // '
    {"rq", "\xe2\x80\x99"},               // '
    {"textquotedbl", "\""},
    {"textquotedblleft", "\xe2\x80\x9c"},  // "
    {"textquotedblright", "\xe2\x80\x9d"}, // "
    {"quotesinglbase", "\xe2\x80\x9a"},    // ‚
    {"quotedblbase", "\xe2\x80\x9e"},      // „
    {"guillemotleft", "\xc2\xab"},         // «
    {"guillemotright", "\xc2\xbb"},        // »
    {"guilsinglleft", "\xe2\x80\xb9"},     // ‹
    {"guilsinglright", "\xe2\x80\xba"},    // ›,
    
    // Diacritics (standalone)
    {"textasciigrave", "`"},
    {"textgravedbl", "˵"},
    {"textasciidieresis", "¨"},
    {"textasciiacute", "´"},
    {"textacutedbl", "˝"},
    {"textasciimacron", "¯"},
    {"textasciicaron", "ˇ"},
    {"textasciibreve", "˘"},
    {"texttildelow", "˷"},
    
    // Punctuation
    {"textendash", "–"},
    {"textemdash", "—"},
    {"textellipsis", "…"},
    {"dots", "…"},
    {"ldots", "…"},
    {"textbullet", "•"},
    {"textopenbullet", "◦"},
    {"textperiodcentered", "·"},
    {"textdagger", "†"},
    {"dag", "†"},
    {"textdaggerdbl", "‡"},
    {"ddag", "‡"},
    {"textexclamdown", "¡"},
    {"textquestiondown", "¿"},
    {"textinterrobang", "‽"},
    {"textinterrobangdown", "⸘"},
    {"textsection", "§"},
    {"S", "§"},
    {"textparagraph", "¶"},
    {"P", "¶"},
    {"textblank", "␢"},
    
    // Delimiters
    {"textlquill", "⁅"},
    {"textrquill", "⁆"},
    {"textlangle", "〈"},
    {"textrangle", "〉"},
    {"textlbrackdbl", "〚"},
    {"textrbrackdbl", "〛"},
    
    // Legal symbols
    {"textcopyright", "©"},
    {"copyright", "©"},
    {"textregistered", "®"},
    {"textcircledP", "℗"},
    {"textservicemark", "℠"},
    {"texttrademark", "™"},
    
    // Genealogical
    {"textmarried", "⚭"},
    {"textdivorced", "⚮"},
    
    // Misc
    {"textordfeminine", "ª"},
    {"textordmasculine", "º"},
    {"textdegree", "°"},
    {"textmu", "µ"},
    {"textbar", "|"},
    {"textbardbl", "‖"},
    {"textbrokenbar", "¦"},
    {"textreferencemark", "※"},
    {"textdiscount", "⁒"},
    {"textcelsius", "℃"},
    {"textnumero", "№"},
    {"textrecipe", "℞"},
    {"textestimated", "℮"},
    {"textbigcircle", "◯"},
    {"textmusicalnote", "♪"},
    {"textohm", "Ω"},
    {"textmho", "℧"},
    
    // Arrows
    {"textleftarrow", "←"},
    {"textuparrow", "↑"},
    {"textrightarrow", "→"},
    {"textdownarrow", "↓"},
    
    // Math symbols
    {"textperthousand", "‰"},
    {"perthousand", "‰"},
    {"textpertenthousand", "‱"},
    {"textonehalf", "½"},
    {"textthreequarters", "¾"},
    {"textonequarter", "¼"},
    {"textfractionsolidus", "⁄"},
    {"textdiv", "÷"},
    {"texttimes", "×"},
    {"textminus", "−"},
    {"textasteriskcentered", "∗"},
    {"textpm", "±"},
    {"textsurd", "√"},
    {"textlnot", "¬"},
    {"textonesuperior", "¹"},
    {"texttwosuperior", "²"},
    {"textthreesuperior", "³"},
    
    // Currencies
    {"texteuro", "€"},
    {"textcent", "¢"},
    {"textsterling", "£"},
    {"pounds", "£"},
    {"textbaht", "฿"},
    {"textcolonmonetary", "₡"},
    {"textcurrency", "¤"},
    {"textdong", "₫"},
    {"textflorin", "ƒ"},
    {"textlira", "₤"},
    {"textnaira", "₦"},
    {"textpeso", "₱"},
    {"textwon", "₩"},
    {"textyen", "¥"},
};

const std::map<std::string, std::string>& getBaseSymbols() {
    return BASE_SYMBOLS;
}

// =============================================================================
// Diacritics
// =============================================================================

static const std::map<std::string, std::pair<std::string, std::string>> DIACRITICS = {
    {"b", {"\u0332", "_"}},       // Combining macron below, standalone underscore
    {"c", {"\u0327", "¸"}},       // Combining cedilla
    {"d", {"\u0323", "\u200B\u0323"}},  // Combining dot below
    {"H", {"\u030B", "˝"}},       // Combining double acute
    {"k", {"\u0328", "˛"}},       // Combining ogonek
    {"r", {"\u030A", "˚"}},       // Combining ring above
    {"t", {"\u0361", "\u200B\u0361"}},  // Combining tie
    {"u", {"\u0306", "˘"}},       // Combining breve
    {"v", {"\u030C", "ˇ"}},       // Combining caron
    {"\"", {"\u0308", "¨"}},      // Combining diaeresis
    {"~", {"\u0303", "~"}},       // Combining tilde
    {"^", {"\u0302", "^"}},       // Combining circumflex
    {"`", {"\u0300", "`"}},       // Combining grave
    {"'", {"\u0301", "´"}},       // Combining acute
    {"=", {"\u0304", "¯"}},       // Combining macron
    {".", {"\u0307", "˙"}},       // Combining dot above
};

const std::map<std::string, std::pair<std::string, std::string>>& getDiacritics() {
    return DIACRITICS;
}

// =============================================================================
// Ligatures
// =============================================================================

static const std::map<std::string, std::string> LIGATURES = {
    {"ff", "ff"},
    {"ffi", "ffi"},
    {"ffl", "ffl"},
    {"fi", "fi"},
    {"fl", "fl"},
    {"``", "\xe2\x80\x9c"},
    {"''", "\xe2\x80\x9d"},
    {"!\xc2\xb4", "\xc2\xa1"},
    {"?\xc2\xb4", "\xc2\xbf"},
    {"--", "\xe2\x80\x93"},
    {"---", "\xe2\x80\x94"},
    {"<<", "\xc2\xab"},
    {">>", "\xc2\xbb"},
    {"\"\x60", "\xe2\x80\x9e"},
    {"\"'", "\xe2\x80\x9d"},
};

const std::map<std::string, std::string>& getLigatures() {
    return LIGATURES;
}

// =============================================================================
// textgreek package
// =============================================================================

static const std::map<std::string, std::string> TEXTGREEK_SYMBOLS = {
    // Lowercase Greek letters
    {"textalpha", "α"},
    {"textbeta", "β"},
    {"textgamma", "γ"},
    {"textdelta", "δ"},
    {"textepsilon", "ε"},
    {"textzeta", "ζ"},
    {"texteta", "η"},
    {"texttheta", "ϑ"},
    {"textiota", "ι"},
    {"textkappa", "κ"},
    {"textlambda", "λ"},
    {"textmu", "μ"},
    {"textmugreek", "μ"},
    {"textnu", "ν"},
    {"textxi", "ξ"},
    {"textomikron", "ο"},
    {"textpi", "π"},
    {"textrho", "ρ"},
    {"textsigma", "σ"},
    {"texttau", "τ"},
    {"textupsilon", "υ"},
    {"textphi", "φ"},
    {"textchi", "χ"},
    {"textpsi", "ψ"},
    {"textomega", "ω"},
    
    // Uppercase Greek letters
    {"textAlpha", "Α"},
    {"textBeta", "Β"},
    {"textGamma", "Γ"},
    {"textDelta", "Δ"},
    {"textEpsilon", "Ε"},
    {"textZeta", "Ζ"},
    {"textEta", "Η"},
    {"textTheta", "Θ"},
    {"textIota", "Ι"},
    {"textKappa", "Κ"},
    {"textLambda", "Λ"},
    {"textMu", "Μ"},
    {"textNu", "Ν"},
    {"textXi", "Ξ"},
    {"textOmikron", "Ο"},
    {"textPi", "Π"},
    {"textRho", "Ρ"},
    {"textSigma", "Σ"},
    {"textTau", "Τ"},
    {"textUpsilon", "Υ"},
    {"textPhi", "Φ"},
    {"textChi", "Χ"},
    {"textPsi", "Ψ"},
    {"textOmega", "Ω"},
    
    // Variants
    {"textvarsigma", "ς"},
    {"straightphi", "ϕ"},
    {"scripttheta", "ϑ"},
    {"straighttheta", "θ"},
    {"straightepsilon", "ϵ"},
};

const std::map<std::string, std::string>& TextgreekPackage::symbols() const {
    return TEXTGREEK_SYMBOLS;
}

// =============================================================================
// textcomp package
// =============================================================================

static const std::map<std::string, std::string> TEXTCOMP_SYMBOLS = {
    // Currencies
    {"textcentoldstyle", ""},      // Private use area
    {"textdollaroldstyle", ""},    // Private use area
    {"textguarani", "₲"},
    
    // Legal symbols
    {"textcopyleft", "🄯"},
    
    // Old style numerals
    {"textzerooldstyle", ""},
    {"textoneoldstyle", ""},
    {"texttwooldstyle", ""},
    {"textthreeoldstyle", ""},
    {"textfouroldstyle", ""},
    {"textfiveoldstyle", ""},
    {"textsixoldstyle", ""},
    {"textsevenoldstyle", ""},
    {"texteightoldstyle", ""},
    {"textnineoldstyle", ""},
    
    // Genealogical
    {"textborn", "⭑"},
    {"textdied", "†"},
    
    // Misc
    {"textpilcrow", "¶"},
    {"textdblhyphen", "⹀"},
};

const std::map<std::string, std::string>& TextcompPackage::symbols() const {
    return TEXTCOMP_SYMBOLS;
}

// =============================================================================
// gensymb package
// =============================================================================

static const std::map<std::string, std::string> GENSYMB_SYMBOLS = {
    {"degree", "°"},
    {"celsius", "℃"},
    {"perthousand", "‰"},
    {"ohm", "Ω"},
    {"micro", "μ"},
};

const std::map<std::string, std::string>& GensymbPackage::symbols() const {
    return GENSYMB_SYMBOLS;
}

// =============================================================================
// stix package
// =============================================================================

static const std::map<std::string, std::string> STIX_SYMBOLS = {
    {"checkmark", "✓"},
    {"varspadesuit", "♤"},
    {"varheartsuit", "♥"},
    {"vardiamondsuit", "♦"},
    {"varclubsuit", "♧"},
};

const std::map<std::string, std::string>& StixPackage::symbols() const {
    return STIX_SYMBOLS;
}

// =============================================================================
// latexsym package
// =============================================================================

static const std::map<std::string, std::string> LATEXSYM_SYMBOLS = {
    {"mho", "℧"},
    {"Join", "⨝"},
    {"Box", "□"},
    {"Diamond", "◇"},
    {"leadsto", "⤳"},
    {"sqsubset", "⊏"},
    {"sqsupset", "⊐"},
    {"lhd", "⊲"},
    {"unlhd", "⊴"},
    {"rhd", "⊳"},
    {"unrhd", "⊵"},
};

const std::map<std::string, std::string>& LatexsymPackage::symbols() const {
    return LATEXSYM_SYMBOLS;
}

// =============================================================================
// hyperref package
// =============================================================================

bool HyperrefPackage::providesCommand(const char* cmd) const {
    static const char* commands[] = {
        "href", "url", "nolinkurl", "hyperref", "hyperbaseurl", nullptr
    };
    for (int i = 0; commands[i]; ++i) {
        if (strcmp(commands[i], cmd) == 0) return true;
    }
    return false;
}

// =============================================================================
// multicol package
// =============================================================================

bool MulticolPackage::providesCommand(const char* cmd) const {
    return strcmp(cmd, "multicols") == 0 ||
           strcmp(cmd, "begin_multicols") == 0 ||
           strcmp(cmd, "end_multicols") == 0;
}

// =============================================================================
// graphicx package
// =============================================================================

bool GraphicxPackage::providesCommand(const char* cmd) const {
    static const char* commands[] = {
        "includegraphics", "graphicspath", "rotatebox", 
        "scalebox", "reflectbox", "resizebox", nullptr
    };
    for (int i = 0; commands[i]; ++i) {
        if (strcmp(commands[i], cmd) == 0) return true;
    }
    return false;
}

// =============================================================================
// xcolor package
// =============================================================================

bool XcolorPackage::providesCommand(const char* cmd) const {
    static const char* commands[] = {
        "color", "textcolor", "colorbox", "fcolorbox",
        "definecolor", "definecolorset", nullptr
    };
    for (int i = 0; commands[i]; ++i) {
        if (strcmp(commands[i], cmd) == 0) return true;
    }
    return false;
}

// =============================================================================
// comment package
// =============================================================================

bool CommentPackage::providesCommand(const char* cmd) const {
    return strcmp(cmd, "comment") == 0 ||
           strcmp(cmd, "begin_comment") == 0 ||
           strcmp(cmd, "end_comment") == 0;
}

// =============================================================================
// picture/pict2e package
// =============================================================================

bool PicturePackage::providesCommand(const char* cmd) const {
    static const char* commands[] = {
        "picture", "put", "line", "vector", "circle", "oval",
        "qbezier", "multiput", "linethickness", "thicklines",
        "thinlines", "frame", nullptr
    };
    for (int i = 0; commands[i]; ++i) {
        if (strcmp(commands[i], cmd) == 0) return true;
    }
    return false;
}

// =============================================================================
// PackageRegistry implementation
// =============================================================================

const std::vector<std::string> PackageRegistry::BUILTIN_PACKAGES = {
    "calc", "keyval", "picture", "pspicture", "pict2e", "comment"
};

PackageRegistry::PackageRegistry() {
    // Register all known packages
    factories_["textgreek"] = []() { return std::make_unique<TextgreekPackage>(); };
    factories_["textcomp"] = []() { return std::make_unique<TextcompPackage>(); };
    factories_["gensymb"] = []() { return std::make_unique<GensymbPackage>(); };
    factories_["stix"] = []() { return std::make_unique<StixPackage>(); };
    factories_["latexsym"] = []() { return std::make_unique<LatexsymPackage>(); };
    factories_["hyperref"] = []() { return std::make_unique<HyperrefPackage>(); };
    factories_["multicol"] = []() { return std::make_unique<MulticolPackage>(); };
    factories_["graphicx"] = []() { return std::make_unique<GraphicxPackage>(); };
    factories_["graphics"] = []() { return std::make_unique<GraphicxPackage>(); };  // Alias
    factories_["xcolor"] = []() { return std::make_unique<XcolorPackage>(); };
    factories_["color"] = []() { return std::make_unique<XcolorPackage>(); };       // Alias
    factories_["comment"] = []() { return std::make_unique<CommentPackage>(); };
    factories_["pict2e"] = []() { return std::make_unique<PicturePackage>(); };
    factories_["picture"] = []() { return std::make_unique<PicturePackage>(); };
    factories_["calc"] = []() { return std::make_unique<CalcPackage>(); };
}

PackageRegistry& PackageRegistry::instance() {
    static PackageRegistry registry;
    return registry;
}

bool PackageRegistry::loadPackage(const char* name, const std::vector<std::string>& options) {
    if (!name || !*name) return false;
    
    std::string pkg_name = name;
    
    // Already loaded?
    if (loaded_.find(pkg_name) != loaded_.end()) {
        return true;
    }
    
    // Built-in (no need to actually load)?
    if (isBuiltIn(name)) {
        return true;
    }
    
    // Find factory
    auto it = factories_.find(pkg_name);
    if (it == factories_.end()) {
        // Unknown package - silently ignore
        return true;  // Return true to not break processing
    }
    
    // Create package
    auto pkg = it->second();
    pkg->processOptions(options);
    
    loaded_[pkg_name] = std::move(pkg);
    symbols_dirty_ = true;
    
    return true;
}

bool PackageRegistry::isLoaded(const char* name) const {
    if (!name) return false;
    return loaded_.find(name) != loaded_.end() || isBuiltIn(name);
}

bool PackageRegistry::isBuiltIn(const char* name) const {
    if (!name) return false;
    for (const auto& pkg : BUILTIN_PACKAGES) {
        if (pkg == name) return true;
    }
    return false;
}

const char* PackageRegistry::lookupSymbol(const char* cmd) const {
    if (!cmd) return nullptr;
    
    // First check base symbols
    auto base_it = BASE_SYMBOLS.find(cmd);
    if (base_it != BASE_SYMBOLS.end()) {
        return base_it->second.c_str();
    }
    
    // Then check loaded packages
    for (const auto& [name, pkg] : loaded_) {
        auto& syms = pkg->symbols();
        auto it = syms.find(cmd);
        if (it != syms.end()) {
            return it->second.c_str();
        }
    }
    
    return nullptr;
}

const std::unordered_map<std::string, std::string>& PackageRegistry::allSymbols() const {
    if (symbols_dirty_) {
        all_symbols_.clear();
        
        // Add base symbols
        for (const auto& [k, v] : BASE_SYMBOLS) {
            all_symbols_[k] = v;
        }
        
        // Add package symbols
        for (const auto& [name, pkg] : loaded_) {
            for (const auto& [k, v] : pkg->symbols()) {
                all_symbols_[k] = v;
            }
        }
        
        symbols_dirty_ = false;
    }
    
    return all_symbols_;
}

void PackageRegistry::reset() {
    loaded_.clear();
    symbols_dirty_ = true;
}

LatexPackage* PackageRegistry::getPackage(const char* name) const {
    if (!name) return nullptr;
    auto it = loaded_.find(name);
    if (it != loaded_.end()) {
        return it->second.get();
    }
    return nullptr;
}

} // namespace lambda
