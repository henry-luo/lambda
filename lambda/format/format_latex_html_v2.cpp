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
    gen->span("class=\"cite\"");
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
    gen->span("class=\"cite-author\"");
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
    gen->span("class=\"cite-year\"");
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
    gen->span("class=\"bibitem-label\"");
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
        // Symbol (spacing, paragraph break, special characters, etc.)
        String* str = reader.asSymbol();
        if (str) {
            const char* sym_name = str->chars;
            
            if (strcmp(sym_name, "parbreak") == 0) {
                gen_->p();
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
    
    // Look up command in table
    auto it = command_table_.find(cmd_name);
    if (it != command_table_.end()) {
        // Call command handler
        it->second(this, elem);
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
    log_debug("format_latex_html_v2_c called, text_mode=%d", text_mode);
    return lambda::format_latex_html_v2(input, text_mode != 0);
}

} // extern "C"
