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

// =============================================================================
// LatexProcessor - Processes LaTeX Element tree and generates HTML
// =============================================================================

class LatexProcessor {
public:
    LatexProcessor(HtmlGenerator* gen, Pool* pool) : gen_(gen), pool_(pool) {}
    
    // Process a LaTeX element tree
    void process(Item root);
    
    // Process a single node (element, string, or symbol)
    void processNode(Item node);
    
    // Process element children
    void processChildren(Item elem);
    
    // Process text content
    void processText(const char* text);
    
    // Get generator
    HtmlGenerator* generator() { return gen_; }
    
    // Get pool
    Pool* pool() { return pool_; }
    
private:
    HtmlGenerator* gen_;
    Pool* pool_;
    
    // Command dispatch table (will be populated)
    std::map<std::string, CommandFunc> command_table_;
    
    // Process specific command
    void processCommand(const char* cmd_name, Item elem);
    
    // Initialize command table
    void initCommandTable();
};

// =============================================================================
// Command Implementations
// =============================================================================

// Text formatting commands

static void cmd_textbf(LatexProcessor* proc, Item elem) {
    // \textbf{text} - bold text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().series = FontSeries::Bold;
    gen->span("class=\"textbf\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textit(LatexProcessor* proc, Item elem) {
    // \textit{text} - italic text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().shape = FontShape::Italic;
    gen->span("class=\"textit\"");
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
    gen->span("class=\"emph\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_texttt(LatexProcessor* proc, Item elem) {
    // \texttt{text} - typewriter/monospace text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::Typewriter;
    gen->span("class=\"texttt\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textsf(LatexProcessor* proc, Item elem) {
    // \textsf{text} - sans-serif text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::SansSerif;
    gen->span("class=\"textsf\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textrm(LatexProcessor* proc, Item elem) {
    // \textrm{text} - roman (serif) text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().family = FontFamily::Roman;
    gen->span("class=\"textrm\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_textsc(LatexProcessor* proc, Item elem) {
    // \textsc{text} - small caps text
    HtmlGenerator* gen = proc->generator();
    
    gen->enterGroup();
    gen->currentFont().shape = FontShape::SmallCaps;
    gen->span("class=\"textsc\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_underline(LatexProcessor* proc, Item elem) {
    // \underline{text} - underlined text
    HtmlGenerator* gen = proc->generator();
    
    gen->span("class=\"underline\"");
    proc->processChildren(elem);
    gen->closeElement();
}

// Font size commands

static void cmd_tiny(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Tiny;
    gen->span("class=\"tiny\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_scriptsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::ScriptSize;
    gen->span("class=\"scriptsize\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_footnotesize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::FootnoteSize;
    gen->span("class=\"footnotesize\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_small(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Small;
    gen->span("class=\"small\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_normalsize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::NormalSize;
    proc->processChildren(elem);
    gen->exitGroup();
}

static void cmd_large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large;
    gen->span("class=\"large\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_Large(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large2;
    gen->span("class=\"Large\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_LARGE(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Large3;
    gen->span("class=\"LARGE\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge;
    gen->span("class=\"huge\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

static void cmd_Huge(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->enterGroup();
    gen->currentFont().size = FontSize::Huge2;
    gen->span("class=\"Huge\"");
    proc->processChildren(elem);
    gen->closeElement();
    gen->exitGroup();
}

// Sectioning commands

static void cmd_section(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    // Extract title from children - collect all text
    ElementReader elem_reader(elem);
    
    // Use a StringBuf to collect text content
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* title_str = stringbuf_to_string(sb);
    std::string title = title_str->chars;
    
    gen->startSection("section", false, title, title);
}

static void cmd_subsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* title_str = stringbuf_to_string(sb);
    std::string title = title_str->chars;
    
    gen->startSection("subsection", false, title, title);
}

static void cmd_subsubsection(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* title_str = stringbuf_to_string(sb);
    std::string title = title_str->chars;
    
    gen->startSection("subsubsection", false, title, title);
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
    HtmlGenerator* gen = proc->generator();
    
    // Extract URL
    ElementReader elem_reader(elem);
    Pool* pool = proc->pool();
    StringBuf* sb = stringbuf_new(pool);
    elem_reader.textContent(sb);
    String* url_str = stringbuf_to_string(sb);
    
    // Create hyperlink (URL as both href and text)
    gen->hyperlink(url_str->chars, nullptr);
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
// LatexProcessor Implementation
// =============================================================================

void LatexProcessor::initCommandTable() {
    // Text formatting
    command_table_["textbf"] = cmd_textbf;
    command_table_["textit"] = cmd_textit;
    command_table_["emph"] = cmd_emph;
    command_table_["texttt"] = cmd_texttt;
    command_table_["textsf"] = cmd_textsf;
    command_table_["textrm"] = cmd_textrm;
    command_table_["textsc"] = cmd_textsc;
    command_table_["underline"] = cmd_underline;
    
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
    command_table_["href"] = cmd_href;
    
    // Footnotes
    command_table_["footnote"] = cmd_footnote;
}

void LatexProcessor::process(Item root) {
    initCommandTable();
    processNode(root);
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
        // Symbol (spacing, paragraph break, etc.)
        String* str = reader.asSymbol();
        if (str) {
            const char* sym_name = str->chars;
            
            if (strcmp(sym_name, "parbreak") == 0) {
                gen_->p();
                gen_->closeElement();
            } else {
                // Other symbols - just output as text for now
                gen_->text(sym_name);
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

void LatexProcessor::processText(const char* text) {
    if (!text) return;
    gen_->text(text);
}

void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Look up command in table
    auto it = command_table_.find(cmd_name);
    if (it != command_table_.end()) {
        // Call command handler
        it->second(this, elem);
        return;
    }
    
    // Unknown command - just output children
    log_debug("processCommand: unknown command '%s', outputting children", cmd_name);
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
    LatexProcessor proc(&gen, pool);
    
    // Process LaTeX tree
    proc.process(input->root);
    
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
    return lambda::format_latex_html_v2(input, text_mode != 0);
}

} // extern "C"
