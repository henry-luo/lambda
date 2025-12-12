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
#include <cstring>
#include <map>

namespace lambda {

// Forward declarations for command processors
class LatexProcessor;

// Command processor function type
typedef void (*CommandFunc)(LatexProcessor* proc, Item elem);

// Forward declarations for helper functions
static Element* cloneElement(Element* src, Input* input, Pool* pool);
static std::vector<Item> substituteParamsInString(const char* text, size_t len,
                                                   const std::vector<Element*>& args,
                                                   Pool* pool);
static void substituteParamsRecursive(Element* elem, const std::vector<Element*>& args, Pool* pool);

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
    };
    
public:
    LatexProcessor(HtmlGenerator* gen, Pool* pool, Input* input) 
        : gen_(gen), pool_(pool), input_(input), in_paragraph_(false), inline_depth_(0) {}
    
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
    
    // Macro system functions (public so command handlers can access)
    void registerMacro(const std::string& name, int num_params, Element* definition);
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
    
    // Helper methods for paragraph management
    void ensureParagraph();
    void closeParagraphIfOpen();
    bool isBlockCommand(const char* cmd_name);
    bool isInlineCommand(const char* cmd_name);
    
    // Process specific command
    void processCommand(const char* cmd_name, Item elem);
    
    // Initialize command table
    void initCommandTable();
};

// =============================================================================
// Macro System - Member Function Implementations
// =============================================================================

void LatexProcessor::registerMacro(const std::string& name, int num_params, Element* definition) {
    fprintf(stderr, "registerMacro: name='%s', num_params=%d, definition=%p\n", name.c_str(), num_params, (void*)definition);
    MacroDefinition macro;
    macro.name = name;
    macro.num_params = num_params;
    macro.definition = definition;
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
    MacroDefinition* macro = getMacro(name);
    if (!macro || !macro->definition) {
        fprintf(stderr, "expandMacro: macro '%s' not found or no definition\n", name.c_str());
        return nullptr;
    }
    
    fprintf(stderr, "expandMacro: '%s' with %zu args, num_params=%d\n", name.c_str(), args.size(), macro->num_params);
    
    // Clone the definition using MarkBuilder to preserve TypeElmt metadata
    Element* expanded = cloneElement(macro->definition, input_, pool_);
    
    fprintf(stderr, "expandMacro: cloned definition, expanded=%p\n", (void*)expanded);
    
    // Substitute parameters with actual arguments if needed
    if (expanded && args.size() > 0 && macro->num_params > 0) {
        fprintf(stderr, "expandMacro: calling substituteParamsRecursive\n");
        substituteParamsRecursive(expanded, args, pool_);
        fprintf(stderr, "expandMacro: substitution done\n");
    }
    
    return expanded;
}

// =============================================================================
// Macro System - Helper Functions
// =============================================================================

// Clone an Element tree (deep copy for macro expansion)
// Uses MarkBuilder to properly reconstruct Elements with TypeElmt metadata
static Element* cloneElement(Element* src, Input* input, Pool* pool) {
    fprintf(stderr, "cloneElement: START, src=%p\n", (void*)src);
    if (!src) return nullptr;
    
    Item src_item;
    src_item.element = src;
    ElementReader reader(src_item);
    const char* tag = reader.tagName();
    fprintf(stderr, "cloneElement: tag='%s'\n", tag ? tag : "NULL");
    if (!tag) {
        fprintf(stderr, "cloneElement: source element has no tag name\n");
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
    
    // Debug: verify the cloned element has a proper tag
    if (clone_item.element) {
        ElementReader verify_reader(clone_item);
        const char* verify_tag = verify_reader.tagName();
        fprintf(stderr, "cloneElement: cloned element tag='%s' (original='%s')\n", 
                verify_tag ? verify_tag : "NULL", tag);
    }
    
    return clone_item.element;
}

// Substitute #1, #2, etc. in a string with actual argument values
static std::vector<Item> substituteParamsInString(const char* text, size_t len,
                                                   const std::vector<Element*>& args,
                                                   Pool* pool) {
    std::vector<Item> result;
    size_t i = 0;
    size_t segment_start = 0;
    
    fprintf(stderr, "substituteParamsInString: text='%.*s', %zu args\n", (int)len, text, args.size());
    
    while (i < len) {
        if (text[i] == '#' && i + 1 < len && text[i + 1] >= '1' && text[i + 1] <= '9') {
            // Found parameter reference
            int param_num = text[i + 1] - '0';
            fprintf(stderr, "  Found param #%d at position %zu\n", param_num, i);
            
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
static void substituteParamsRecursive(Element* elem, const std::vector<Element*>& args, Pool* pool) {
    if (!elem) return;
    
    List* elem_list = (List*)elem;
    if (!elem_list->items) return;
    
    fprintf(stderr, "substituteParamsRecursive: processing %lld items with %zu args\n", elem_list->length, args.size());
    
    std::vector<Item> new_items;
    
    for (int64_t i = 0; i < elem_list->length; i++) {
        Item item = elem_list->items[i];
        TypeId type = get_type_id(item);
        
        fprintf(stderr, "  Item %lld: type=%d\n", i, type);
        
        if (type == LMD_TYPE_STRING) {
            String* str = (String*)item.string_ptr;
            fprintf(stderr, "  String item: '%.*s'\n", (int)str->len, str->chars);
            
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
        } else if (type == LMD_TYPE_ELEMENT) {
            // Recursively process child elements
            substituteParamsRecursive(item.element, args, pool);
            new_items.push_back(item);
        } else {
            new_items.push_back(item);
        }
    }
    
    // Replace element's items with substituted version
    if (new_items.size() != (size_t)elem_list->length) {
        elem_list->items = (Item*)pool_calloc(pool, sizeof(Item) * new_items.size());
        elem_list->length = new_items.size();
        elem_list->capacity = new_items.size();
        for (size_t i = 0; i < new_items.size(); i++) {
            elem_list->items[i] = new_items[i];
        }
    }
}

// =============================================================================
// Command Implementations
// =============================================================================

// Text formatting commands

static void cmd_textbf(LatexProcessor* proc, Item elem) {
    // \textbf{text} - bold text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().series = FontSeries::Bold;
    gen->span("bf");  // Just the class name
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textit(LatexProcessor* proc, Item elem) {
    // \textit{text} - italic text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().shape = FontShape::Italic;
    gen->span("it");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_emph(LatexProcessor* proc, Item elem) {
    // \emph{text} - emphasized text (toggles italic)
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    // Toggle italic state
    if (gen->currentFont().shape == FontShape::Italic) {
        gen->currentFont().shape = FontShape::Upright;
    } else {
        gen->currentFont().shape = FontShape::Italic;
    }
    gen->span("it");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_texttt(LatexProcessor* proc, Item elem) {
    // \texttt{text} - typewriter/monospace text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::Typewriter;
    gen->span("tt");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textsf(LatexProcessor* proc, Item elem) {
    // \textsf{text} - sans-serif text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::SansSerif;
    gen->span("textsf");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textrm(LatexProcessor* proc, Item elem) {
    // \textrm{text} - roman (serif) text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::Roman;
    gen->span("textrm");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textsc(LatexProcessor* proc, Item elem) {
    // \textsc{text} - small caps text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().shape = FontShape::SmallCaps;
    gen->span("textsc");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_underline(LatexProcessor* proc, Item elem) {
    // \underline{text} - underlined text
    HtmlGenerator* gen = proc->generator();
    
    gen->span("underline");
    proc->processChildren(elem);
    gen->closeElement();
}

static void cmd_sout(LatexProcessor* proc, Item elem) {
    // \sout{text} - strikethrough text
    HtmlGenerator* gen = proc->generator();
    
    gen->span("sout");
    proc->processChildren(elem);
    gen->closeElement();
}

// Macro definition commands

static void cmd_newcommand(LatexProcessor* proc, Item elem) {
    // \newcommand{\name}[num]{definition}
    // Defines a new macro (error if already exists)
    fprintf(stderr, "cmd_newcommand: called!\n");
    ElementReader reader(elem);
    
    // DEBUG: Check textContent of entire element
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    reader.textContent(sb);
    String* all_text = stringbuf_to_string(sb);
    fprintf(stderr, "  new_command_definition textContent: '%s'\n", all_text->chars);
    
    fprintf(stderr, "  Total children: %zu\n", reader.childCount());
    
    std::string macro_name;
    int num_params = 0;
    Element* definition = nullptr;
    
    // Parse arguments: first is command name, second (optional) is num params, third is definition
    auto iter = reader.children();
    ItemReader child;
    int arg_index = 0;
    
    while (iter.next(&child)) {
        TypeId child_type = child.getType();
        fprintf(stderr, "  Child (type=%d): ", child_type);
        
        if (child.isString()) {
            String* str = child.asString();
            fprintf(stderr, "STRING='%s'\n", str->chars);
            // If we find a string starting with \, that might be the command name
            if (macro_name.empty() && str->chars[0] == '\\') {
                macro_name = str->chars;
            }
        } else if (child.isSymbol()) {
            String* sym = child.asSymbol();
            fprintf(stderr, "SYMBOL='%s'\n", sym->chars);
            // Check if this symbol is the command name (not a marker)
            if (macro_name.empty() && sym->chars[0] == '\\') {
                macro_name = sym->chars;
            }
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            
            fprintf(stderr, "ELEMENT tag='%s'\n", tag);
            
            // Check for brack_group FIRST before other processing
            if (strcmp(tag, "brack_group") == 0 || strcmp(tag, "brack_group_argc") == 0) {
                // [num] parameter count - use allText() to recursively extract all text
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.allText(sb);
                String* num_str = stringbuf_to_string(sb);
                fprintf(stderr, "    Found brack_group_argc: allText='%s'\n", num_str->chars);
                
                // Parse the number (might have whitespace)
                num_params = atoi(num_str->chars);
                fprintf(stderr, "    Set num_params=%d\n", num_params);
                continue;  // Don't process as regular arg
            }
            
            // If element tag starts with \, it might be the command itself
            if (macro_name.empty() && tag[0] == '\\' && strcmp(tag, "\\newcommand") != 0) {
                macro_name = tag;
            }
            
            // Special case: check if this is the \newcommand token itself
            if (strcmp(tag, "\\newcommand") == 0) {
                fprintf(stderr, "    Checking \\newcommand token structure\n");
                // Check its raw items
                Element* token_elem = const_cast<Element*>(child_elem.element());
                List* token_list = (List*)token_elem;
                fprintf(stderr, "    \\newcommand has %lld items\n", token_list->length);
                
                for (int64_t k = 0; k < token_list->length; k++) {
                    Item token_item = token_list->items[k];
                    TypeId token_type = get_type_id(token_item);
                    fprintf(stderr, "      Token item %lld: type=%d ", k, token_type);
                    
                    if (token_type == LMD_TYPE_STRING) {
                        String* str = (String*)token_item.string_ptr;
                        fprintf(stderr, "string='%s'\n", str->chars);
                        if (macro_name.empty() && str->chars[0] == '\\') {
                            macro_name = str->chars;
                        }
                    } else if (token_type == LMD_TYPE_SYMBOL) {
                        String* sym = (String*)token_item.string_ptr;
                        fprintf(stderr, "symbol='%s'\n", sym->chars);
                        if (macro_name.empty() && sym->chars[0] == '\\') {
                            macro_name = sym->chars;
                        }
                    } else if (token_type == LMD_TYPE_ELEMENT) {
                        Item elem_item;
                        elem_item.item = (uint64_t)token_item.element;
                        ElementReader elem_reader(elem_item);
                        fprintf(stderr, "element='%s'\n", elem_reader.tagName());
                        if (macro_name.empty() && elem_reader.tagName()[0] == '\\') {
                            macro_name = elem_reader.tagName();
                        }
                    } else {
                        fprintf(stderr, "\n");
                    }
                }
            }
            
            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {
                fprintf(stderr, "    Processing curly_group at arg_index=%d\n", arg_index);
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
                    fprintf(stderr, "    Final macro_name='%s'\n", macro_name.c_str());
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
    
    fprintf(stderr, "cmd_newcommand: macro_name='%s', num_params=%d, definition=%p\n", macro_name.c_str(), num_params, (void*)definition);
    
    if (!macro_name.empty() && definition) {
        // Check if macro already exists
        if (proc->isMacro(macro_name)) {
            log_error("Macro \\%s already defined (use \\renewcommand to redefine)", macro_name.c_str());
        } else {
            proc->registerMacro(macro_name, num_params, definition);
        }
    } else {
        fprintf(stderr, "cmd_newcommand: FAILED - macro_name empty or no definition\n");
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
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            
            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {
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
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            
            if (strcmp(tag, "curly_group") == 0 || strcmp(tag, "curly_group_command_name") == 0) {
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
    gen->span("tiny");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_scriptsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::ScriptSize;
    gen->span("scriptsize");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_footnotesize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::FootnoteSize;
    gen->span("footnotesize");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_small(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Small;
    gen->span("small");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_normalsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::NormalSize;
    gen->span("normalsize");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large;
    gen->span("large");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_Large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large2;
    gen->span("Large");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_LARGE(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large3;
    gen->span("LARGE");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge;
    gen->span("huge");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_Huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge2;
    gen->span("Huge");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

// Sectioning commands

static void cmd_section(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    
    // Find the title argument (first curly_group child)
    std::string title;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                // Extract title text from this group
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* title_str = stringbuf_to_string(sb);
                title = title_str->chars;
                break;
            }
        }
    }
    
    gen->startSection("section", false, title, title);
    
    // Process remaining children (section content: label, text, refs, etc.)
    proc->processChildren(elem);
}

static void cmd_subsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    
    // Find the title argument (first curly_group child)
    std::string title;
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
        }
    }
    
    gen->startSection("subsection", false, title, title);
    proc->processChildren(elem);
}

static void cmd_subsubsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    
    // Find the title argument (first curly_group child)
    std::string title;
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
        }
    }
    
    gen->startSection("subsubsection", false, title, title);
    proc->processChildren(elem);
}

static void cmd_chapter(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* title_str = stringbuf_to_string(sb);
    std::string title = title_str->chars;
    
    gen->startSection("chapter", false, title, title);
}

static void cmd_part(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* title_str = stringbuf_to_string(sb);
    std::string title = title_str->chars;
    
    gen->startSection("part", false, title, title);
}

// List environment commands

static void cmd_itemize(LatexProcessor* proc, Item elem) {
    // \begin{itemize} ... \end{itemize}
    HtmlGenerator* gen = proc->generator();
    
    gen->startItemize();
    proc->processChildren(elem);
    gen->endItemize();
}

static void cmd_enumerate(LatexProcessor* proc, Item elem) {
    // \begin{enumerate} ... \end{enumerate}
    HtmlGenerator* gen = proc->generator();
    
    gen->startEnumerate();
    proc->processChildren(elem);
    gen->endEnumerate();
}

static void cmd_description(LatexProcessor* proc, Item elem) {
    // \begin{description} ... \end{description}
    HtmlGenerator* gen = proc->generator();
    
    gen->startDescription();
    proc->processChildren(elem);
    gen->endDescription();
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
    
    gen->startCenter();
    proc->processChildren(elem);
    gen->endCenter();
}

static void cmd_flushleft(LatexProcessor* proc, Item elem) {
    // \begin{flushleft} ... \end{flushleft}
    HtmlGenerator* gen = proc->generator();
    
    gen->startFlushLeft();
    proc->processChildren(elem);
    gen->endFlushLeft();
}

static void cmd_flushright(LatexProcessor* proc, Item elem) {
    // \begin{flushright} ... \end{flushright}
    HtmlGenerator* gen = proc->generator();
    
    gen->startFlushRight();
    proc->processChildren(elem);
    gen->endFlushRight();
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

static void cmd_newpage(LatexProcessor* proc, Item elem) {
    // \newpage
    HtmlGenerator* gen = proc->generator();
    gen->lineBreak(true);  // page break
}

// Label and reference commands

static void cmd_label(LatexProcessor* proc, Item elem) {
    // \label{name}
    HtmlGenerator* gen = proc->generator();
    
    // Extract label name from children
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* label_str = stringbuf_to_string(sb);
    
    // Set the label with current context
    gen->setLabel(label_str->chars);
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
    // Parser gives: {"$":"caption", "_":[{"$":"\caption"}, {"$":"curly_group", "_":["text"]}]}
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    gen->startCaption();
    
    // Extract caption text from curly_group
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            if (strcmp(child_elem.tagName(), "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* text_str = stringbuf_to_string(sb);
                gen->text(text_str->chars);
            }
        }
    }
    
    gen->endCaption();
}

static void cmd_includegraphics(LatexProcessor* proc, Item elem) {
    // \includegraphics[options]{filename}
    // Tree-sitter structure: <graphics_include> <\includegraphics> <brack_group_key_value> <curly_group_path>
    // brack_group_key_value contains <key_value_pair> elements like: <key_value_pair "width" <=> <value "5cm">>
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    const char* filename = nullptr;
    Pool* pool = proc->pool();
    StringBuf* options_sb = stringbuf_new(pool);
    
    // Extract filename and options
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
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
            } else if (strcmp(tag, "brack_group_key_value") == 0 || strcmp(tag, "bracket_group") == 0) {
                // Parse structured key-value pairs
                auto kv_iter = child_elem.children();
                ItemReader kv_child;
                bool first = true;
                while (kv_iter.next(&kv_child)) {
                    if (kv_child.getType() == LMD_TYPE_ELEMENT) {
                        ElementReader kv_elem(kv_child.item());
                        if (strcmp(kv_elem.tagName(), "key_value_pair") == 0) {
                            // Extract key and value
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
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    std::string color_model;
    std::string color_spec;
    std::string color_name;
    bool has_model = false;
    
    // Extract color specification and text
    Item text_content_item = ItemNull;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
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
            } else if (strcmp(tag, "curly_group_text") == 0) {
                // Color name (named color like "red", "blue")
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);
                color_name = content->chars;
            } else if (strcmp(tag, "curly_group") == 0) {
                // Either color spec (if has_model) or text content
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);
                
                if (has_model && color_spec.empty()) {
                    // First curly group after model is the spec
                    color_spec = content->chars;
                } else {
                    // This is the text content - save it for processing
                    text_content_item = child.item();
                }
            }
        }
    }
    
    // Generate colored span if we have text content
    if (get_type_id(text_content_item) != LMD_TYPE_NULL) {
        std::string style_value = "color: " + 
             (has_model ? colorToCss(color_model.c_str(), color_spec.c_str()) 
                        : namedColorToCss(color_name.c_str()));
        gen->spanWithStyle(style_value.c_str());
        proc->processChildren(text_content_item);
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
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    std::string color_model;
    std::string color_spec;
    std::string color_name;
    bool has_model = false;
    Item text_content_item = ItemNull;
    
    // Extract color specification and text
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            
            if (strcmp(tag, "brack_group_text") == 0 || strcmp(tag, "bracket_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* model_str = stringbuf_to_string(sb);
                color_model = model_str->chars;
                has_model = true;
            } else if (strcmp(tag, "curly_group_text") == 0) {
                // Color name (named color)
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);
                color_name = content->chars;
            } else if (strcmp(tag, "curly_group") == 0) {
                Pool* pool = proc->pool();
                StringBuf* sb = stringbuf_new(pool);
                child_elem.textContent(sb);
                String* content = stringbuf_to_string(sb);
                
                if (has_model && color_spec.empty()) {
                    color_spec = content->chars;
                } else {
                    // This is the text content - save it
                    text_content_item = child.item();
                }
            }
        }
    }
    
    // Generate colored box if we have text content
    if (get_type_id(text_content_item) != LMD_TYPE_NULL) {
        std::string style_value = "background-color: " + 
             (has_model ? colorToCss(color_model.c_str(), color_spec.c_str()) 
                        : namedColorToCss(color_name.c_str()));
        gen->spanWithStyle(style_value.c_str());
        proc->processChildren(text_content_item);
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
    // For now, just skip - in full implementation would store in color registry
    // The color will be used later in \textcolor or \color commands
}

// =============================================================================
// Bibliography & Citation Commands
// =============================================================================

static void cmd_cite(LatexProcessor* proc, Item elem) {
    // \cite[optional]{key} or \cite{key1,key2}
    // Generate citation reference like [1] or [Smith20]
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    // Extract citation keys from curly_group
    std::vector<std::string> keys;
    std::string optional_text;
    
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
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
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    // Extract key
    std::string key;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
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
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    // Extract key
    std::string key;
    auto iter = elem_reader.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.getType() == LMD_TYPE_ELEMENT) {
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
    gen->div("class=\"bibitem\"");
    
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
    command_table_["\\item"] = cmd_item;     // Command form with backslash
    
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
    
    // Labels and references
    command_table_["label"] = cmd_label;
    command_table_["ref"] = cmd_ref;
    command_table_["pageref"] = cmd_pageref;
    
    // Hyperlinks
    command_table_["url"] = cmd_url;
    command_table_["\\url"] = cmd_url;  // Command form with backslash
    command_table_["hyperlink"] = cmd_href;  // Tree-sitter node type for \href
    command_table_["curly_group_uri"] = cmd_url;  // Tree-sitter uri group
    command_table_["href"] = cmd_href;
    command_table_["\\href"] = cmd_href;  // Command form with backslash
    
    // Footnotes
    command_table_["footnote"] = cmd_footnote;
    
    // Tables
    command_table_["tabular"] = cmd_tabular;
    command_table_["hline"] = cmd_hline;
    command_table_["\\hline"] = cmd_hline;
    command_table_["multicolumn"] = cmd_multicolumn;
    
    // Float environments
    command_table_["figure"] = cmd_figure;
    command_table_["table"] = cmd_table_float;
    command_table_["caption"] = cmd_caption;
    
    // Graphics
    command_table_["graphics_include"] = cmd_includegraphics;
    command_table_["includegraphics"] = cmd_includegraphics;
    command_table_["\\includegraphics"] = cmd_includegraphics;
    
    // Color commands
    command_table_["color_reference"] = cmd_color_reference;  // Tree-sitter node for \textcolor and \colorbox
    command_table_["textcolor"] = cmd_textcolor;
    command_table_["\\textcolor"] = cmd_textcolor;
    command_table_["color"] = cmd_color;
    command_table_["\\color"] = cmd_color;
    command_table_["colorbox"] = cmd_colorbox;
    command_table_["\\colorbox"] = cmd_colorbox;
    command_table_["fcolorbox"] = cmd_fcolorbox;
    command_table_["\\fcolorbox"] = cmd_fcolorbox;
    command_table_["definecolor"] = cmd_definecolor;
    command_table_["\\definecolor"] = cmd_definecolor;
    
    // Bibliography & Citations
    command_table_["cite"] = cmd_cite;
    command_table_["\\cite"] = cmd_cite;
    command_table_["citeauthor"] = cmd_citeauthor;
    command_table_["\\citeauthor"] = cmd_citeauthor;
    command_table_["citeyear"] = cmd_citeyear;
    command_table_["\\citeyear"] = cmd_citeyear;
    command_table_["bibliographystyle"] = cmd_bibliographystyle;
    command_table_["\\bibliographystyle"] = cmd_bibliographystyle;
    command_table_["bibliography"] = cmd_bibliography;
    command_table_["\\bibliography"] = cmd_bibliography;
    command_table_["bibitem"] = cmd_bibitem;
    command_table_["\\bibitem"] = cmd_bibitem;
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
            strcmp(cmd_name, "date") == 0);
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
        gen_->p();
        in_paragraph_ = true;
    }
}

void LatexProcessor::closeParagraphIfOpen() {
    if (in_paragraph_) {
        gen_->closeElement();
        in_paragraph_ = false;
    }
}

void LatexProcessor::process(Item root) {
    initCommandTable();
    in_paragraph_ = false;  // Reset paragraph state
    processNode(root);
    closeParagraphIfOpen();  // Close any open paragraph at the end
}

void LatexProcessor::processNode(Item node) {
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
                // Don't open new paragraph yet - ensureParagraph() will handle it when next content arrives
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
        // Process list items (e.g., from math environments or flattened content)
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
            }
            break;
        }
    }
}

void LatexProcessor::processText(const char* text) {
    if (!text) return;
    
    // Skip pure whitespace
    bool all_whitespace = true;
    for (const char* p = text; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            all_whitespace = false;
            break;
        }
    }
    
    if (!all_whitespace) {
        ensureParagraph();  // Auto-wrap text in <p> tags
    }
    
    gen_->text(text);
}

void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Handle macro definition elements specially (from Tree-sitter)
    if (strcmp(cmd_name, "new_command_definition") == 0) {
        cmd_newcommand(this, elem);
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
    fprintf(stderr, "processCommand: checking if '%s' is a macro\n", cmd_name);
    if (isMacro(cmd_name)) {
        fprintf(stderr, "processCommand: '%s' IS a macro!\n", cmd_name);
        MacroDefinition* macro = getMacro(cmd_name);
        if (macro && macro->definition) {
            // Extract arguments from the command element
            std::vector<Element*> args;
            ElementReader reader(elem);
            auto iter = reader.children();
            ItemReader child;
            
            while (iter.next(&child)) {
                if (child.isElement()) {
                    ElementReader child_elem(child.item());
                    const char* tag = child_elem.tagName();
                    
                    // Collect curly_group arguments for the macro
                    if (strcmp(tag, "curly_group") == 0) {
                        args.push_back(const_cast<Element*>(child_elem.element()));
                        if ((int)args.size() >= macro->num_params) {
                            break;  // Got all arguments
                        }
                    }
                }
            }
            
            // Expand the macro with arguments
            Element* expanded = expandMacro(cmd_name, args);
            if (expanded) {
                log_debug("Macro %s expanded with %zu args", cmd_name, args.size());
                // Process the expanded content  
                // The expanded element IS the replacement content, so process it directly
                Item expanded_item;
                expanded_item.item = (uint64_t)expanded;
                processNode(expanded_item);  // Process the expanded macro content itself
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
        writer = new TextHtmlWriter(pool, true);  // pretty print
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
