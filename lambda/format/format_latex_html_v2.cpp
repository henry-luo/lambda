// format_latex_html_v2.cpp - Main entry point for LaTeX to HTML conversion
// Processes Lambda Element tree from input-latex-ts.cpp parser

#include "format.h"
#include "html_writer.hpp"
#include "html_generator.hpp"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../input/input.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/stringbuf.h"
#include <string>
#include <sstream>
#include <cstring>
#include <map>

namespace lambda {

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
          strip_next_leading_space_(false), styled_span_depth_(0), italic_styled_span_depth_(0),
          recursion_depth_(0), depth_exceeded_(false) {}
    
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
    
    // Paragraph management - public so command handlers can access
    void endParagraph();  // Close current paragraph if open
    void closeParagraphIfOpen();  // Alias for endParagraph
    void setNextParagraphIsContinue() { next_paragraph_is_continue_ = true; }
    void setNextParagraphIsNoindent() { next_paragraph_is_noindent_ = true; }
    void ensureParagraph();  // Start a paragraph if not already in one
    
    // Macro system functions (public so command handlers can access)
    void registerMacro(const std::string& name, int num_params, Element* definition, Element* default_value = nullptr);
    bool isMacro(const std::string& name);
    MacroDefinition* getMacro(const std::string& name);
    Element* expandMacro(const std::string& name, const std::vector<Element*>& args);
    
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
    
    // Helper methods for paragraph management
    bool isBlockCommand(const char* cmd_name);
    bool isInlineCommand(const char* cmd_name);
    
    // Process specific command
    void processCommand(const char* cmd_name, Item elem);
    
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
    gen->currentFont().family = FontFamily::Typewriter;
    gen->span("tt");
    proc->processChildren(elem);
    gen->closeElement();
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
// These set font state and mark that the following text should strip leading space
// (LaTeX commands consume their trailing space)
// =============================================================================

static void cmd_bfseries(LatexProcessor* proc, Item elem) {
    // \bfseries - switch to bold (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->currentFont().series = FontSeries::Bold;
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
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
    proc->setStripNextLeadingSpace(true);
    proc->processChildren(elem);
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

static void cmd_tiny(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Tiny;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("tiny");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_scriptsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::ScriptSize;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("scriptsize");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_footnotesize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::FootnoteSize;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("footnotesize");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_small(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Small;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("small");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_normalsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::NormalSize;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("normalsize");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("large");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_Large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large2;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("Large");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_LARGE(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large3;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("LARGE");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("huge");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
}

static void cmd_Huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge2;
    proc->enterStyledSpan();  // Prevent double-wrapping in processText
    gen->span("Huge");
    proc->processChildren(elem);
    gen->closeElement();
    proc->exitStyledSpan();
    gen->exitGroup();
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
    // 2. \empty{} (empty braces) - produces ZWSP for space preservation
    // 3. \begin{empty}...\end{empty} (environment) - processes content + ZWSP
    
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);
    
    // Check what kind of children we have
    bool has_curly_group = false;
    bool has_content = false;
    
    auto iter = reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                has_curly_group = true;
            } else {
                // Has other content (e.g., paragraph from environment)
                has_content = true;
            }
        } else if (child.isString()) {
            has_content = true;
        }
    }
    
    // Case 3: Environment with content - process children + ZWSP
    if (has_content) {
        proc->processChildren(elem);
        gen->text("\xE2\x80\x8B");  // UTF-8 encoding of U+200B
        return;
    }
    
    // Case 2: Empty braces - output ZWSP only
    if (has_curly_group) {
        gen->text("\xE2\x80\x8B");  // UTF-8 encoding of U+200B
        return;
    }
    
    // Case 1: No braces - output nothing (null command)
}

static void cmd_textbackslash(LatexProcessor* proc, Item elem) {
    // \textbackslash - Outputs a backslash character
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->text("\\");
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
    
    ElementReader elem_reader(elem);
    
    // Find the title - collect all text content from string children
    // Labels are element children and should be registered separately
    std::string title;
    Pool* pool = proc->pool();
    StringBuf* title_sb = stringbuf_new(pool);
    
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            stringbuf_append_str(title_sb, child.cstring());
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            // Skip label elements from title, but remember them for later
            if (strcmp(child_elem.tagName(), "label") != 0) {
                // For other elements (formatting), get text content
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* text = stringbuf_to_string(sb);
                stringbuf_append_str(title_sb, text->chars);
            }
        }
    }
    String* title_str = stringbuf_to_string(title_sb);
    title = title_str->chars;
    
    gen->startSection("section", false, title, title);
    
    // Now register any labels as children of section
    auto label_iter = elem_reader.children();
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
    proc->processChildren(elem);
}

static void cmd_subsubsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
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
    proc->processChildren(elem);
}

static void cmd_chapter(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
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
// Renders elements like \textendash to their text equivalents
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
                                gen->endItem();  // Close li/dd with proper structure
                            }
                            
                            // Get optional label from item
                            // For description/itemize/enumerate lists, label is in brack_group child: \item[label]
                            const char* label = nullptr;
                            std::string label_buf;  // Buffer to hold extracted label
                            bool has_brack_group = false;  // Track if brack_group exists (for empty label)
                            
                            if (para_child_elem.childCount() > 0) {
                                ItemReader first = para_child_elem.childAt(0);
                                if (first.isElement()) {
                                    ElementReader first_elem = first.asElement();
                                    const char* first_tag = first_elem.tagName();
                                    if (first_tag && strcmp(first_tag, "brack_group") == 0) {
                                        has_brack_group = true;
                                        // Extract label from brack_group using helper
                                        label_buf = extractLabelFromBrackGroup(first_elem);
                                        // Use empty string for empty brack_group, or the extracted text
                                        label = label_buf.c_str();
                                    }
                                } else if (first.isString()) {
                                    label = first.cstring();
                                }
                            }
                            
                            gen->createItem(has_brack_group ? label : nullptr);
                            in_item = true;
                            at_item_start = true;  // Next text should be trimmed
                        } else {
                            // Other element within paragraph
                            if (in_item) {
                                const char* elem_tag = para_child_elem.tagName();
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
                                    gen->trimTrailingWhitespace();
                                    gen->closeElement();  // Close <p>
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
                            // Paragraph break within list item - close </p> and open <p>
                            if (in_item) {
                                gen->itemParagraphBreak();
                                at_item_start = true;  // Trim leading whitespace in new paragraph
                            }
                        }
                    } else if (para_child.isString()) {
                        // Text content
                        const char* text = para_child.cstring();
                        if (in_item && text && text[0] != '\0') {
                            // Trim leading whitespace at start of item or after paragraph break
                            if (at_item_start) {
                                while (*text && isspace((unsigned char)*text)) {
                                    text++;
                                }
                                at_item_start = false;
                            }
                            
                            // Skip if now empty after trimming
                            if (text[0] != '\0') {
                                gen->text(text);
                            }
                        }
                    }
                }
                continue;
            }
            
            // Direct item (not in paragraph wrapper)
            if (strcmp(tag, "item") == 0 || strcmp(tag, "enum_item") == 0) {
                if (in_item) {
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
                    gen->text(text);
                }
            }
        }
    }
    
    // Close last item
    if (in_item) {
        gen->endItem();  // Close last item with proper structure
    }
}

// List environment commands

static void cmd_itemize(LatexProcessor* proc, Item elem) {
    // \begin{itemize} ... \end{itemize}
    HtmlGenerator* gen = proc->generator();
    
    gen->startItemize();
    processListItems(proc, elem, "itemize");
    gen->endItemize();
    
    // Next paragraph should have class="continue"
    proc->setNextParagraphIsContinue();
}

static void cmd_enumerate(LatexProcessor* proc, Item elem) {
    // \begin{enumerate} ... \end{enumerate}
    HtmlGenerator* gen = proc->generator();
    
    gen->startEnumerate();
    processListItems(proc, elem, "enumerate");
    gen->endEnumerate();
    
    // Next paragraph should have class="continue"
    proc->setNextParagraphIsContinue();
}

static void cmd_description(LatexProcessor* proc, Item elem) {
    // \begin{description} ... \end{description}
    HtmlGenerator* gen = proc->generator();
    
    gen->startDescription();
    processListItems(proc, elem, "description");
    gen->endDescription();
    
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
    proc->processChildren(elem);
    gen->closeElement();  // Close li/dd
}

// Basic environment commands

static void cmd_quote(LatexProcessor* proc, Item elem) {
    // \begin{quote} ... \end{quote}
    HtmlGenerator* gen = proc->generator();
    
    gen->startQuote();
    proc->processChildren(elem);
    gen->endQuote();
}

static void cmd_quotation(LatexProcessor* proc, Item elem) {
    // \begin{quotation} ... \end{quotation}
    HtmlGenerator* gen = proc->generator();
    
    gen->startQuotation();
    proc->processChildren(elem);
    gen->endQuotation();
}

static void cmd_verse(LatexProcessor* proc, Item elem) {
    // \begin{verse} ... \end{verse}
    HtmlGenerator* gen = proc->generator();
    
    gen->startVerse();
    proc->processChildren(elem);
    gen->endVerse();
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
    
    gen->startInlineMath();
    proc->processChildren(elem);
    gen->endInlineMath();
}

static void cmd_displaymath(LatexProcessor* proc, Item elem) {
    // Display math: \[...\]
    HtmlGenerator* gen = proc->generator();
    
    gen->startDisplayMath();
    proc->processChildren(elem);
    gen->endDisplayMath();
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

// Line break commands

static void cmd_newline(LatexProcessor* proc, Item elem) {
    // \\ or \newline
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(false);
}

static void cmd_linebreak(LatexProcessor* proc, Item elem) {
    // \linebreak
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(false);
}

static void cmd_par(LatexProcessor* proc, Item elem) {
    // \par - end current paragraph and start a new one
    // Simply close the paragraph if open - the next text will start a new one
    proc->endParagraph();
    (void)elem;  // unused
}

static void cmd_noindent(LatexProcessor* proc, Item elem) {
    // \noindent - the next paragraph should not be indented
    // Close current paragraph if open, and set flag for next paragraph
    proc->endParagraph();
    proc->setNextParagraphIsNoindent();
    (void)elem;  // unused
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
    HtmlGenerator* gen = proc->generator();
    
    // Extract length from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* length_str = stringbuf_to_string(sb);
    
    // Create block spacer using divWithClassAndStyle for proper attribute handling
    char style[256];
    snprintf(style, sizeof(style), "display:block;height:%s", length_str->chars);
    gen->divWithClassAndStyle("vspace", style);
    gen->closeElement();
}

static void cmd_addvspace(LatexProcessor* proc, Item elem) {
    // \addvspace{length} - add vertical space (only if needed)
    // For HTML, just treat as regular vspace
    cmd_vspace(proc, elem);
}

static void cmd_smallbreak(LatexProcessor* proc, Item elem) {
    // \smallbreak - small vertical break
    HtmlGenerator* gen = proc->generator();
    gen->div("vspace smallskip");
    gen->closeElement();
}

static void cmd_medbreak(LatexProcessor* proc, Item elem) {
    // \medbreak - medium vertical break
    HtmlGenerator* gen = proc->generator();
    gen->div("vspace medskip");
    gen->closeElement();
}

static void cmd_bigbreak(LatexProcessor* proc, Item elem) {
    // \bigbreak - big vertical break
    HtmlGenerator* gen = proc->generator();
    gen->div("vspace bigskip");
    gen->closeElement();
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
    // In HTML, use CSS page-break hint
    HtmlGenerator* gen = proc->generator();
    gen->spanWithStyle("page-break-inside:avoid");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_pagebreak(LatexProcessor* proc, Item elem) {
    // \pagebreak[priority] - encourage page break
    HtmlGenerator* gen = proc->generator();
    gen->divWithClassAndStyle(nullptr, "page-break-after:always");
    gen->closeElement();
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
    // Just process children
    proc->processChildren(elem);
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

static void cmd_mbox(LatexProcessor* proc, Item elem) {
    // \mbox{text} - make box (prevent line breaking)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithStyle("white-space:nowrap");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_fbox(LatexProcessor* proc, Item elem) {
    // \fbox{text} - framed box
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("fbox", "border:1px solid black;padding:3px");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_framebox(LatexProcessor* proc, Item elem) {
    // \framebox[width][pos]{text} - framed box with options
    // TODO: Parse width and position parameters
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("framebox", "border:1px solid black;padding:3px");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_frame(LatexProcessor* proc, Item elem) {
    // \frame{text} - simple frame
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("frame", "border:1px solid black");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_parbox(LatexProcessor* proc, Item elem) {
    // \parbox[pos][height][inner-pos]{width}{text} - paragraph box
    // TODO: Parse all parameters
    HtmlGenerator* gen = proc->generator();
    gen->divWithClassAndStyle("parbox", "display:inline-block");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_makebox(LatexProcessor* proc, Item elem) {
    // \makebox[width][pos]{text} - make box with size
    // TODO: Parse width and position
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->span("makebox");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_phantom(LatexProcessor* proc, Item elem) {
    // \phantom{text} - invisible box with dimensions
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("phantom", "visibility:hidden");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_hphantom(LatexProcessor* proc, Item elem) {
    // \hphantom{text} - horizontal phantom (width only)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("hphantom", "visibility:hidden");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_vphantom(LatexProcessor* proc, Item elem) {
    // \vphantom{text} - vertical phantom (height/depth only)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("vphantom", "visibility:hidden;width:0");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_smash(LatexProcessor* proc, Item elem) {
    // \smash[tb]{text} - smash height/depth
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("smash", "display:inline-block;height:0");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_clap(LatexProcessor* proc, Item elem) {
    // \clap{text} - centered lap (zero width, centered)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("clap", "display:inline-block;width:0;text-align:center");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_llap(LatexProcessor* proc, Item elem) {
    // \llap{text} - left lap (zero width, right-aligned)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("llap", "display:inline-block;width:0;text-align:right");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_rlap(LatexProcessor* proc, Item elem) {
    // \rlap{text} - right lap (zero width, left-aligned)
    HtmlGenerator* gen = proc->generator();
    proc->ensureParagraph();
    gen->spanWithClassAndStyle("rlap", "display:inline-block;width:0;text-align:left");
    proc->processChildren(elem);
    gen->closeElement();
}

// =============================================================================
// Alignment Declaration Commands
// =============================================================================

static void cmd_centering(LatexProcessor* proc, Item elem) {
    // \centering - center alignment (declaration)
    HtmlGenerator* gen = proc->generator();
    gen->divWithStyle("text-align:center");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_raggedright(LatexProcessor* proc, Item elem) {
    // \raggedright - ragged right (left-aligned, declaration)
    HtmlGenerator* gen = proc->generator();
    gen->divWithStyle("text-align:left");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_raggedleft(LatexProcessor* proc, Item elem) {
    // \raggedleft - ragged left (right-aligned, declaration)
    HtmlGenerator* gen = proc->generator();
    gen->divWithStyle("text-align:right");
    proc->processChildren(elem);
    gen->closeElement();
}

// =============================================================================
// Document Metadata Commands
// =============================================================================

static void cmd_author(LatexProcessor* proc, Item elem) {
    // \author{name} - set document author
    HtmlGenerator* gen = proc->generator();
    // Store author for \maketitle
    // For now, just output inline with span - \maketitle will format properly later
    gen->span("latex-author");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_title(LatexProcessor* proc, Item elem) {
    // \title{text} - set document title
    HtmlGenerator* gen = proc->generator();
    // Store title for \maketitle  
    // For now, just output inline with span - \maketitle will format properly later
    gen->span("latex-title");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_date(LatexProcessor* proc, Item elem) {
    // \date{text} - set document date
    HtmlGenerator* gen = proc->generator();
    // Store date for \maketitle
    // For now, just output inline with span - \maketitle will format properly later
    gen->span("latex-date");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_thanks(LatexProcessor* proc, Item elem) {
    // \thanks{text} - thanks footnote in title
    HtmlGenerator* gen = proc->generator();
    gen->spanWithClassAndStyle("thanks", "vertical-align:super;font-size:smaller");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_maketitle(LatexProcessor* proc, Item elem) {
    // \maketitle - generate title page
    HtmlGenerator* gen = proc->generator();
    gen->div("maketitle");
    // TODO: Combine stored title, author, date
    // For now, just create a placeholder
    gen->closeElement();
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
    
    // Use startFigure with "table" type
    gen->startFigure(position);  // HtmlGenerator uses same method for both
    proc->processChildren(elem);
    gen->endFigure();
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
    // \usepackage[options]{package}
    // Store package name for reference but no HTML output
    // Package-specific commands should already be implemented individually
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
    // Defines a new counter
    HtmlGenerator* gen = proc->generator();
    
    // Extract counter name from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* counter_str = stringbuf_to_string(sb);
    
    // Create the counter (initial value 0)
    gen->newCounter(counter_str->chars);
}

static void cmd_setcounter(LatexProcessor* proc, Item elem) {
    // \setcounter{counter}{value}
    // Sets counter to a specific value
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
        
        // Second child: value
        ItemReader second = elem_reader.childAt(1);
        StringBuf* sb2 = stringbuf_new(pool);
        if (second.isElement()) {
            second.asElement().textContent(sb2);
        } else if (second.isString()) {
            stringbuf_append_str(sb2, second.cstring());
        }
        String* value_str = stringbuf_to_string(sb2);
        
        int value = atoi(value_str->chars);
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
        
        // Second child: value to add
        ItemReader second = elem_reader.childAt(1);
        StringBuf* sb2 = stringbuf_new(pool);
        if (second.isElement()) {
            second.asElement().textContent(sb2);
        } else if (second.isString()) {
            stringbuf_append_str(sb2, second.cstring());
        }
        String* value_str = stringbuf_to_string(sb2);
        
        int value = atoi(value_str->chars);
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

static void cmd_value(LatexProcessor* proc, Item elem) {
    // \value{counter}
    // Returns the value of a counter (for use in calculations)
    // In HTML, output "0" as placeholder
    HtmlGenerator* gen = proc->generator();
    gen->text("0");
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
    // In HTML, no output
    // TODO: Length state tracking
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
    command_table_["verbatim"] = cmd_verbatim;
    
    // Math environments
    command_table_["math"] = cmd_math;
    command_table_["displaymath"] = cmd_displaymath;
    command_table_["math_environment"] = cmd_math_environment;  // Tree-sitter node for \[...\]
    command_table_["displayed_equation"] = cmd_displaymath;  // Tree-sitter node for \[...\]
    command_table_["equation"] = cmd_equation;
    command_table_["equation*"] = cmd_equation_star;
    
    // Line breaks
    command_table_["\\"] = cmd_newline;
    command_table_["newline"] = cmd_newline;
    command_table_["linebreak"] = cmd_linebreak;
    command_table_["newpage"] = cmd_newpage;
    command_table_["par"] = cmd_par;
    command_table_["noindent"] = cmd_noindent;
    
    // Special LaTeX commands
    command_table_["TeX"] = cmd_TeX;
    command_table_["LaTeX"] = cmd_LaTeX;
    command_table_["today"] = cmd_today;
    command_table_["empty"] = cmd_empty;
    command_table_["textbackslash"] = cmd_textbackslash;
    command_table_["makeatletter"] = cmd_makeatletter;
    command_table_["makeatother"] = cmd_makeatother;
    
    // Spacing commands
    command_table_["hspace"] = cmd_hspace;
    command_table_["vspace"] = cmd_vspace;
    command_table_["addvspace"] = cmd_addvspace;
    command_table_["smallbreak"] = cmd_smallbreak;
    command_table_["medbreak"] = cmd_medbreak;
    command_table_["bigbreak"] = cmd_bigbreak;
    command_table_["vfill"] = cmd_vfill;
    command_table_["hfill"] = cmd_hfill;
    command_table_["nolinebreak"] = cmd_nolinebreak;
    command_table_["nopagebreak"] = cmd_nopagebreak;
    command_table_["pagebreak"] = cmd_pagebreak;
    command_table_["clearpage"] = cmd_clearpage;
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
    command_table_["bibliographystyle"] = cmd_bibliographystyle;
    command_table_["bibliography"] = cmd_bibliography;
    command_table_["bibitem"] = cmd_bibitem;
    
    // Document structure (additional commands)
    command_table_["documentclass"] = cmd_documentclass;
    command_table_["usepackage"] = cmd_usepackage;
    command_table_["include"] = cmd_include;
    command_table_["input"] = cmd_input;
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
        if (next_paragraph_is_noindent_) {
            gen_->p("noindent");
            next_paragraph_is_noindent_ = false;  // Reset the flag
        } else if (next_paragraph_is_continue_) {
            gen_->p("continue");
            next_paragraph_is_continue_ = false;  // Reset the flag
        } else {
            gen_->p();
        }
        in_paragraph_ = true;
    }
}

void LatexProcessor::closeParagraphIfOpen() {
    if (in_paragraph_) {
        gen_->trimTrailingWhitespace();  // Trim trailing whitespace before closing paragraph
        gen_->closeElement();
        in_paragraph_ = false;
    }
}

void LatexProcessor::endParagraph() {
    closeParagraphIfOpen();
}

void LatexProcessor::process(Item root) {
    initCommandTable();
    in_paragraph_ = false;  // Reset paragraph state
    depth_exceeded_ = false;  // Reset depth error flag for this processing session
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
        if (str) {
            processText(str->chars);
        }
        return;
    }
    
    if (type == LMD_TYPE_SYMBOL) {
        // Symbol (spacing, paragraph break, special characters, etc.)
        String* str = reader.asSymbol();
        if (str) {
            const char* sym_name = str->chars;
            
            if (strcmp(sym_name, "parbreak") == 0) {
                // Paragraph break: close current paragraph and prepare for next
                closeParagraphIfOpen();
                // Clear continue and noindent flags - parbreak resets paragraph styling
                next_paragraph_is_continue_ = false;
                next_paragraph_is_noindent_ = false;
                // Don't open new paragraph yet - ensureParagraph() will handle it when next content arrives
            } else if (strcmp(sym_name, "TeX") == 0) {
                // TeX logo
                ensureParagraph();
                gen_->span("tex-logo");
                gen_->text("T");
                gen_->text("E");
                gen_->text("X");
                gen_->closeElement();
            } else if (strcmp(sym_name, "LaTeX") == 0) {
                // LaTeX logo
                ensureParagraph();
                gen_->span("latex-logo");
                gen_->text("L");
                gen_->text("A");
                gen_->text("T");
                gen_->text("E");
                gen_->text("X");
                gen_->closeElement();
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
        
        // Special handling for root element
        if (strcmp(tag, "latex_document") == 0) {
            // Just process children
            processChildren(node);
            return;
        }
        
        // Special handling for linebreak_command (\\)
        if (strcmp(tag, "linebreak_command") == 0) {
            ensureParagraph();
            gen_->lineBreak(false);
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
    
    // Iterate through children
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        processNode(child.item());
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
            } else if (strcmp(cmd, "\\ ") == 0) {
                // Backslash-space (control space) - produces zero-width space followed by regular space
                // This allows a line break at this point unlike ~
                gen_->text("\u200B ");  // ZWSP + space
            } else if (strcmp(cmd, "~") == 0) {
                // Tilde (non-breaking space) - this path shouldn't be reached since
                // ~ is handled as nbsp element, but keep for completeness
                gen_->writer()->writeRawHtml("&nbsp;");
            } else if (strcmp(cmd, "\\/") == 0 || strcmp(cmd, "\\@") == 0) {
                // Italic correction or inter-sentence space marker - output nothing
                // These are zero-width commands
            } else if (strcmp(cmd, "\\-") == 0) {
                // Discretionary hyphen - soft hyphen U+00AD
                gen_->text("\xC2\xAD");  // UTF-8 encoding of U+00AD
            }
            break;
        }
    }
}

void LatexProcessor::processText(const char* text) {
    if (!text) return;
    
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
    
    // Note: We do NOT convert ASCII hyphen-minus (U+002D) to Unicode hyphen (U+2010)
    // because standard LaTeX behavior keeps single hyphens as-is in compound words
    // like "daughter-in-law". Only -- (en-dash) and --- (em-dash) are converted,
    // which is handled by the ligature parser in tree-sitter-latex.
    
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
    // should have its leading space stripped
    bool should_strip_leading = strip_next_leading_space_;
    strip_next_leading_space_ = false;  // Reset flag after checking
    
    // Check if current font differs from default - if so, wrap text in a span
    // BUT skip this if we're inside a styled span (like \textbf{}) to prevent double-wrapping
    std::string font_class = gen_->getFontClass(gen_->currentFont());
    if (!font_class.empty() && !inStyledSpan()) {
        // When font styling is active
        if (all_whitespace) {
            // Skip pure whitespace when font styling is active
            return;
        }
        
        std::string content = normalized;
        
        // Strip leading whitespace only if flagged (after a font declaration command)
        if (should_strip_leading) {
            size_t start = 0;
            while (start < content.length() && content[start] == ' ') {
                start++;
            }
            if (start > 0) {
                content = content.substr(start);
            }
        }
        
        // Wrap content in span with font class
        if (!content.empty()) {
            gen_->span(font_class.c_str());
            gen_->text(content.c_str());
            gen_->closeElement();
        }
    } else if (!all_whitespace || normalized.length() == 1) {
        // Normal text or single space - output as-is
        gen_->text(normalized.c_str());
    }
    // Skip pure whitespace (more than one space) - already handled above
}

void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Handle curly_group (TeX brace groups) - important for font scoping
    // Groups in TeX (e.g., { \bfseries text }) limit the scope of declarations
    // latex.js adds zero-width space (U+200B) at group boundaries for visual separation
    if (strcmp(cmd_name, "curly_group") == 0) {
        gen_->enterGroup();
        
        // Check if first child is a string starting with whitespace
        // If so, output ZWS at group entry (latex.js behavior: { → ​ when followed by space)
        ElementReader reader(elem);
        bool has_leading_space = false;
        
        auto iter = reader.children();
        ItemReader first_child;
        if (iter.next(&first_child) && first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->len > 0 && str->chars[0] == ' ') {
                has_leading_space = true;
            }
        }
        
        // Output ZWS at entry if leading space
        if (has_leading_space) {
            ensureParagraph();
            gen_->text("\xe2\x80\x8b");  // U+200B zero-width space
        }
        
        processChildren(elem);
        gen_->exitGroup();
        
        // ZWS at exit for depth <= 2
        if (gen_->groupDepth() <= 2) {
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
    if (strcmp(cmd_name, "new_command_definition") == 0) {
        cmd_newcommand(this, elem);
        return;
    }
    
    // Handle macro argument wrapper (transparent - just process children)
    if (strcmp(cmd_name, "arg") == 0) {
        processChildren(elem);
        return;
    }
    if (strcmp(cmd_name, "renew_command_definition") == 0) {
        cmd_renewcommand(this, elem);
        return;
    }
    if (strcmp(cmd_name, "provide_command_definition") == 0) {
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
    if (isBlockCommand(cmd_name)) {
        // Close paragraph before block commands
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
    
    // Look up command in table
    auto it = command_table_.find(cmd_name);
    if (it != command_table_.end()) {
        // Call command handler
        it->second(this, elem);
        
        // Exit inline element tracking
        if (isInlineCommand(cmd_name)) {
            inline_depth_--;
        }
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
    if (!input || !input->root.item) {
        log_error("format_latex_html_v2: invalid input");
        Item result;
        result.item = ITEM_NULL;
        return result;
    }
    
    Pool* pool = input->pool;
    
    // Create HTML writer
    HtmlWriter* writer = nullptr;
    if (text_mode) {
        // Text mode - generate HTML string
        // Disable pretty print to avoid extra newlines in output
        writer = new TextHtmlWriter(pool, false);
    } else {
        // Node mode - generate Element tree
        writer = new NodeHtmlWriter(input);
    }
    
    // Create HTML generator
    HtmlGenerator gen(pool, writer);
    
    // Create processor
    LatexProcessor proc(&gen, pool, input);
    
    // Start HTML document container (using "body" class for LaTeX.js compatibility)
    writer->openTag("div", "body");
    
    // Process LaTeX tree
    proc.process(input->root);
    
    // Close HTML document container
    writer->closeTag("div");
    
    // Get result
    Item result = writer->getResult();
    
    // Cleanup
    delete writer;
    
    return result;
}

} // namespace lambda

// C API for compatibility with existing code

extern "C" {

Item format_latex_html_v2_c(Input* input, int text_mode) {
    log_debug("format_latex_html_v2_c called, text_mode=%d", text_mode);
    return lambda::format_latex_html_v2(input, text_mode != 0);
}

} // extern "C"
