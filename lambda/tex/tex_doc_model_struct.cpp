// tex_doc_model_struct.cpp - Structural element builders for LaTeX document model
//
// This file contains builders for structural elements:
// - Section builders (section, subsection, chapter, etc.)
// - List builders (itemize, enumerate, description)
// - Table builders (tabular, table)
// - Alignment environment builders (center, flushleft, flushright, quote, etc.)
// - Code block builders (verbatim, lstlisting)

#include "tex_document_model.hpp"
#include "tex_doc_model_internal.hpp"

namespace tex {

#ifndef DOC_MODEL_MINIMAL

// ============================================================================
// Section Builders
// ============================================================================

// Map section command name to heading level for document model
// LaTeX levels: part=0, chapter=1, section=2, subsection=3, subsubsection=4,
//               paragraph=5, subparagraph=6
static int get_doc_section_level(const char* cmd_name) {
    if (!cmd_name) return 2;  // default to section
    if (tag_eq(cmd_name, "part")) return 0;
    if (tag_eq(cmd_name, "chapter")) return 1;
    if (tag_eq(cmd_name, "section")) return 2;
    if (tag_eq(cmd_name, "subsection")) return 3;
    if (tag_eq(cmd_name, "subsubsection")) return 4;
    if (tag_eq(cmd_name, "paragraph")) return 5;
    if (tag_eq(cmd_name, "subparagraph")) return 6;
    return 2;  // default
}

// Build section command (\section, \subsection, etc.)
DocElement* build_section_command(const char* cmd_name, const ElementReader& elem,
                                   Arena* arena, TexDocumentModel* doc) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    heading->heading.level = get_doc_section_level(cmd_name);
    heading->heading.title = nullptr;
    heading->heading.number = nullptr;
    heading->heading.label = nullptr;
    
    // Check for starred variant (unnumbered)
    bool is_starred = false;
    
    // Process children to find title
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag) {
                // Optional argument (short title for TOC)
                if (tag_eq(child_tag, "brack_group") || tag_eq(child_tag, "optional")) {
                    // Short title for TOC - we ignore this for now
                    continue;
                }
                // Required argument (title)
                if (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg") ||
                    tag_eq(child_tag, "title")) {
                    // Render title content to HTML
                    heading->heading.title = render_brack_group_to_html(child, arena, doc);
                }
                // Check for star
                if (tag_eq(child_tag, "star")) {
                    is_starred = true;
                }
            }
        } else if (child.isString()) {
            const char* text = child.cstring();
            // Check for star in the command name
            if (text && strchr(text, '*')) {
                is_starred = true;
            }
        }
    }
    
    // Assign section number if not starred
    if (!is_starred && doc) {
        static int section_counters[7] = {0, 0, 0, 0, 0, 0, 0};
        int level = heading->heading.level;
        
        // Increment counter at this level, reset deeper levels
        section_counters[level]++;
        for (int i = level + 1; i < 7; i++) {
            section_counters[i] = 0;
        }
        
        // Build number string
        StrBuf* num_buf = strbuf_new_cap(32);
        for (int i = 0; i <= level; i++) {
            if (i > 0) strbuf_append_char(num_buf, '.');
            char digit[16];
            snprintf(digit, sizeof(digit), "%d", section_counters[i]);
            strbuf_append_str(num_buf, digit);
        }
        
        // Copy to arena
        char* num_str = (char*)arena_alloc(arena, num_buf->length + 1);
        memcpy(num_str, num_buf->str, num_buf->length + 1);
        heading->heading.number = num_str;
        strbuf_free(num_buf);
        
        // Create label for cross-references
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "sec-%s", num_str);
        char* label_str = (char*)arena_alloc(arena, strlen(label_buf) + 1);
        strcpy(label_str, label_buf);
        heading->heading.label = label_str;
        
        // Register with document for cross-referencing
        doc->current_ref_id = heading->heading.label;
        doc->current_ref_text = heading->heading.number;
    }
    
    return heading;
}

// ============================================================================
// List Builders
// ============================================================================

// Helper to process list content and build list items
static void process_list_content(DocElement* list, const ElementReader& elem,
                                  Arena* arena, TexDocumentModel* doc, ListType list_type) {
    DocElement* current_item = nullptr;
    int item_number = 0;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag) {
                // \item command starts a new list item
                if (tag_eq(child_tag, "item") || tag_eq(child_tag, "item_command")) {
                    // Finalize previous item
                    if (current_item && current_item->first_child) {
                        trim_paragraph_whitespace(current_item, arena);
                        doc_append_child(list, current_item);
                    }
                    
                    // Create new item
                    current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                    current_item->list_item.label = nullptr;
                    current_item->list_item.html_label = nullptr;
                    current_item->list_item.item_number = 0;
                    current_item->list_item.has_custom_label = false;
                    
                    if (list_type == ListType::ENUMERATE) {
                        item_number++;
                        current_item->list_item.item_number = item_number;
                    }
                    
                    // Check for optional label [...]
                    auto item_iter = child_elem.children();
                    ItemReader item_child;
                    while (item_iter.next(&item_child)) {
                        if (item_child.isElement()) {
                            ElementReader ic_elem = item_child.asElement();
                            const char* ic_tag = ic_elem.tagName();
                            if (ic_tag && (tag_eq(ic_tag, "brack_group") || 
                                          tag_eq(ic_tag, "optional"))) {
                                current_item->list_item.has_custom_label = true;
                                // Render label to HTML for styled labels
                                current_item->list_item.html_label = render_brack_group_to_html(item_child, arena, doc);
                                // Also extract plain text for description lists
                                current_item->list_item.label = extract_text_content(item_child, arena);
                            }
                        }
                    }
                    continue;
                }
                
                // Nested list
                if (tag_eq(child_tag, "itemize") || tag_eq(child_tag, "enumerate") ||
                    tag_eq(child_tag, "description")) {
                    DocElement* nested = build_list_environment(child_tag, child_elem, arena, doc);
                    if (nested) {
                        if (current_item) {
                            doc_append_child(current_item, nested);
                        } else {
                            // List started without \item - create one
                            current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                            current_item->list_item.label = nullptr;
                            current_item->list_item.html_label = nullptr;
                            current_item->list_item.item_number = 0;
                            current_item->list_item.has_custom_label = false;
                            doc_append_child(current_item, nested);
                        }
                    }
                    continue;
                }
                
                // Handle paragraph container - items may be inside
                // The AST often wraps list content in a paragraph element
                if (tag_eq(child_tag, "paragraph") || tag_eq(child_tag, "par")) {
                    // Process content inside paragraph, looking for items
                    auto para_iter = child_elem.children();
                    ItemReader para_child;
                    while (para_iter.next(&para_child)) {
                        if (para_child.isElement()) {
                            ElementReader pc_elem = para_child.asElement();
                            const char* pc_tag = pc_elem.tagName();
                            
                            // Check for \item inside paragraph
                            if (pc_tag && (tag_eq(pc_tag, "item") || tag_eq(pc_tag, "item_command"))) {
                                // Finalize previous item
                                if (current_item && current_item->first_child) {
                                    trim_paragraph_whitespace(current_item, arena);
                                    doc_append_child(list, current_item);
                                }
                                
                                // Create new item
                                current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                                current_item->list_item.label = nullptr;
                                current_item->list_item.html_label = nullptr;
                                current_item->list_item.item_number = 0;
                                current_item->list_item.has_custom_label = false;
                                
                                if (list_type == ListType::ENUMERATE) {
                                    item_number++;
                                    current_item->list_item.item_number = item_number;
                                }
                                
                                // Check for optional label [...]
                                auto item_iter = pc_elem.children();
                                ItemReader item_child;
                                while (item_iter.next(&item_child)) {
                                    if (item_child.isElement()) {
                                        ElementReader ic_elem = item_child.asElement();
                                        const char* ic_tag = ic_elem.tagName();
                                        if (ic_tag && (tag_eq(ic_tag, "brack_group") || 
                                                      tag_eq(ic_tag, "optional"))) {
                                            current_item->list_item.has_custom_label = true;
                                            current_item->list_item.html_label = render_brack_group_to_html(item_child, arena, doc);
                                            current_item->list_item.label = extract_text_content(item_child, arena);
                                        }
                                    }
                                }
                                continue;
                            }
                            
                            // Check for nested list inside paragraph
                            if (pc_tag && (tag_eq(pc_tag, "itemize") || tag_eq(pc_tag, "enumerate") ||
                                          tag_eq(pc_tag, "description"))) {
                                DocElement* nested = build_list_environment(pc_tag, pc_elem, arena, doc);
                                if (nested) {
                                    if (current_item) {
                                        doc_append_child(current_item, nested);
                                    } else {
                                        current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                                        current_item->list_item.label = nullptr;
                                        current_item->list_item.html_label = nullptr;
                                        current_item->list_item.item_number = 0;
                                        current_item->list_item.has_custom_label = false;
                                        doc_append_child(current_item, nested);
                                    }
                                }
                                continue;
                            }
                        }
                        
                        // Other content in paragraph goes to current item
                        if (current_item) {
                            DocElement* content = build_doc_element(para_child, arena, doc);
                            if (content && content != PARBREAK_MARKER) {
                                doc_append_child(current_item, content);
                            }
                        }
                    }
                    continue;
                }
                
                // Other content goes into current item
                if (current_item) {
                    DocElement* content = build_doc_element(child, arena, doc);
                    if (content && content != PARBREAK_MARKER) {
                        doc_append_child(current_item, content);
                    }
                }
            }
        } else if (child.isString()) {
            // Text content
            const char* text = child.cstring();
            if (text && strlen(text) > 0 && current_item) {
                // Skip pure whitespace
                bool has_content = false;
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        has_content = true;
                        break;
                    }
                }
                if (has_content) {
                    DocElement* text_elem = doc_create_text_cstr(arena, text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_item, text_elem);
                    }
                }
            }
        }
    }
    
    // Finalize last item
    if (current_item && current_item->first_child) {
        trim_paragraph_whitespace(current_item, arena);
        doc_append_child(list, current_item);
    }
}

// Build list environment (itemize, enumerate, description)
DocElement* build_list_environment(const char* env_name, const ElementReader& elem,
                                    Arena* arena, TexDocumentModel* doc) {
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    
    // Determine list type
    ListType list_type = ListType::ITEMIZE;
    if (tag_eq(env_name, "enumerate")) {
        list_type = ListType::ENUMERATE;
    } else if (tag_eq(env_name, "description")) {
        list_type = ListType::DESCRIPTION;
    }
    
    list->list.list_type = list_type;
    list->list.start_num = 1;
    list->list.nesting_level = 0;
    
    // Process list content
    process_list_content(list, elem, arena, doc, list_type);
    
    return list;
}

// ============================================================================
// Table Builders
// ============================================================================

// Helper to get column alignment character from spec character
static char get_column_alignment(char c) {
    switch (c) {
        case 'l': return 'l';
        case 'r': return 'r';
        case 'c': return 'c';
        case 'p': return 'l';   // paragraph column - left aligned
        case 'm': return 'c';   // middle vertical alignment
        case 'b': return 'l';   // bottom vertical alignment
        default:  return 'l';
    }
}

// Helper to count columns from column spec
static int count_columns_from_spec(const char* spec) {
    if (!spec) return 0;
    int count = 0;
    bool in_brace = false;
    for (const char* p = spec; *p; p++) {
        if (*p == '{') in_brace = true;
        else if (*p == '}') in_brace = false;
        else if (!in_brace && (*p == 'l' || *p == 'r' || *p == 'c' || 
                               *p == 'p' || *p == 'm' || *p == 'b')) {
            count++;
        }
    }
    return count;
}

// Build table environment (tabular, array)
DocElement* build_table_environment(const char* env_name, const ElementReader& elem,
                                     Arena* arena, TexDocumentModel* doc) {
    (void)env_name;
    DocElement* table = doc_alloc_element(arena, DocElemType::TABLE);
    table->table.column_spec = nullptr;
    table->table.num_columns = 0;
    table->table.num_rows = 0;
    
    // First pass: find column specification
    auto spec_iter = elem.children();
    ItemReader spec_child;
    while (spec_iter.next(&spec_child)) {
        if (spec_child.isElement()) {
            ElementReader sc_elem = spec_child.asElement();
            const char* sc_tag = sc_elem.tagName();
            if (sc_tag && (tag_eq(sc_tag, "curly_group") || tag_eq(sc_tag, "column_spec"))) {
                table->table.column_spec = extract_text_content(spec_child, arena);
                table->table.num_columns = count_columns_from_spec(table->table.column_spec);
                break;
            }
        }
    }
    
    // Parse column spec for alignments
    char* col_aligns = nullptr;
    if (table->table.num_columns > 0 && table->table.column_spec) {
        col_aligns = (char*)arena_alloc(arena, table->table.num_columns * sizeof(char));
        int col_idx = 0;
        bool in_brace = false;
        for (const char* p = table->table.column_spec; *p && col_idx < table->table.num_columns; p++) {
            if (*p == '{') in_brace = true;
            else if (*p == '}') in_brace = false;
            else if (!in_brace && (*p == 'l' || *p == 'r' || *p == 'c' || 
                                   *p == 'p' || *p == 'm' || *p == 'b')) {
                col_aligns[col_idx++] = get_column_alignment(*p);
            }
        }
    }
    
    // Second pass: process rows
    DocElement* current_row = nullptr;
    DocElement* current_cell = nullptr;
    int row_count = 0;
    int col_idx = 0;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag) {
                // Skip column spec
                if (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "column_spec")) {
                    continue;
                }
                
                // Row element
                if (tag_eq(child_tag, "row") || tag_eq(child_tag, "table_row")) {
                    // Finalize previous row
                    if (current_row && current_row->first_child) {
                        doc_append_child(table, current_row);
                        row_count++;
                    }
                    
                    // Create new row
                    current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
                    current_cell = nullptr;
                    col_idx = 0;
                    
                    // Process row content
                    auto row_iter = child_elem.children();
                    ItemReader row_child;
                    while (row_iter.next(&row_child)) {
                        if (row_child.isElement()) {
                            ElementReader rc_elem = row_child.asElement();
                            const char* rc_tag = rc_elem.tagName();
                            
                            if (rc_tag) {
                                // Cell element
                                if (tag_eq(rc_tag, "cell") || tag_eq(rc_tag, "table_cell")) {
                                    current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
                                    
                                    current_cell->cell.colspan = 1;
                                    current_cell->cell.rowspan = 1;
                                    current_cell->cell.alignment = (col_aligns && col_idx < table->table.num_columns) 
                                                                ? col_aligns[col_idx] : 'l';
                                    
                                    // Build cell content
                                    auto cell_iter = rc_elem.children();
                                    ItemReader cell_child;
                                    while (cell_iter.next(&cell_child)) {
                                        DocElement* content = build_doc_element(cell_child, arena, doc);
                                        if (content && content != PARBREAK_MARKER) {
                                            doc_append_child(current_cell, content);
                                        }
                                    }
                                    
                                    doc_append_child(current_row, current_cell);
                                    col_idx++;
                                }
                                // Ampersand (column separator)
                                else if (tag_eq(rc_tag, "ampersand") || tag_eq(rc_tag, "&")) {
                                    if (!current_cell) {
                                        // Create empty cell
                                        current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
                                        
                                        current_cell->cell.colspan = 1;
                                        current_cell->cell.rowspan = 1;
                                        current_cell->cell.alignment = (col_aligns && col_idx < table->table.num_columns)
                                                                    ? col_aligns[col_idx] : 'l';
                                        doc_append_child(current_row, current_cell);
                                    }
                                    col_idx++;
                                    current_cell = nullptr;
                                }
                                // Other content in row goes into current cell
                                else {
                                    if (!current_cell) {
                                        current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
                                        
                                        current_cell->cell.colspan = 1;
                                        current_cell->cell.rowspan = 1;
                                        current_cell->cell.alignment = (col_aligns && col_idx < table->table.num_columns)
                                                                    ? col_aligns[col_idx] : 'l';
                                    }
                                    DocElement* content = build_doc_element(row_child, arena, doc);
                                    if (content && content != PARBREAK_MARKER) {
                                        doc_append_child(current_cell, content);
                                    }
                                }
                            }
                        } else if (row_child.isString()) {
                            const char* text = row_child.cstring();
                            if (text && strlen(text) > 0) {
                                if (!current_cell) {
                                    current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
                                    
                                    current_cell->cell.colspan = 1;
                                    current_cell->cell.rowspan = 1;
                                    current_cell->cell.alignment = (col_aligns && col_idx < table->table.num_columns)
                                                                ? col_aligns[col_idx] : 'l';
                                }
                                DocElement* text_elem = doc_create_text_cstr(arena, text, DocTextStyle::plain());
                                if (text_elem) {
                                    doc_append_child(current_cell, text_elem);
                                }
                            }
                        }
                    }
                    
                    // Finalize last cell in row
                    if (current_cell && current_cell->first_child && !current_cell->parent) {
                        doc_append_child(current_row, current_cell);
                    }
                    
                    continue;
                }
                
                // Line break (\\) creates new row
                if (tag_eq(child_tag, "linebreak") || tag_eq(child_tag, "\\\\")) {
                    // Finalize current row
                    if (current_row && current_row->first_child) {
                        doc_append_child(table, current_row);
                        row_count++;
                    }
                    current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
                    current_cell = nullptr;
                    col_idx = 0;
                    continue;
                }
                
                // \hline - just skip
                if (tag_eq(child_tag, "hline") || tag_eq(child_tag, "cline")) {
                    continue;
                }
            }
        }
    }
    
    // Finalize last row
    if (current_row && current_row->first_child) {
        doc_append_child(table, current_row);
        row_count++;
    }
    
    table->table.num_rows = row_count;
    return table;
}

// ============================================================================
// Blockquote Builders
// ============================================================================

// Build blockquote environment (quote, quotation)
DocElement* build_blockquote_environment(const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc) {
    DocElement* quote = doc_alloc_element(arena, DocElemType::BLOCKQUOTE);
    
    // Process content
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        DocElement* content = build_doc_element(child, arena, doc);
        if (content && !is_special_marker(content)) {
            doc_append_child(quote, content);
        }
    }
    
    return quote->first_child ? quote : nullptr;
}

// ============================================================================
// Alignment Environment Builders
// ============================================================================

// Build alignment environment (center, flushleft, flushright, quote, quotation, verse)
DocElement* build_alignment_environment(const char* env_name, const ElementReader& elem,
                                         Arena* arena, TexDocumentModel* doc) {
    // Determine element type and alignment
    DocElemType elem_type = DocElemType::PARAGRAPH;
    bool is_quote = false;
    
    if (tag_eq(env_name, "quote") || tag_eq(env_name, "quotation") || tag_eq(env_name, "verse")) {
        elem_type = DocElemType::BLOCKQUOTE;
        is_quote = true;
    }
    
    DocElement* container = doc_alloc_element(arena, elem_type);
    
    // Set alignment for paragraph-based environments using flags
    if (!is_quote) {
        if (tag_eq(env_name, "center")) {
            container->flags |= DocElement::FLAG_CENTERED;
        } else if (tag_eq(env_name, "flushright")) {
            container->flags |= DocElement::FLAG_FLUSH_RIGHT;
        } else if (tag_eq(env_name, "flushleft")) {
            container->flags |= DocElement::FLAG_FLUSH_LEFT;
        }
    }
    
    // Process content
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        DocElement* content = build_doc_element(child, arena, doc);
        if (content && content != PARBREAK_MARKER) {
            doc_append_child(container, content);
        }
    }
    
    return container;
}

// ============================================================================
// Code Block Builders
// ============================================================================

// Helper to recursively collect all text from an item
static void collect_text_recursive(const ItemReader& item, StrBuf* out) {
    if (item.isString()) {
        const char* text = item.cstring();
        if (text) {
            strbuf_append_str(out, text);
        }
        return;
    }
    
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            collect_text_recursive(child, out);
        }
    }
}

// Build code block environment (verbatim, lstlisting, listing)
DocElement* build_code_block_environment(const char* env_name, const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc) {
    (void)doc;
    (void)env_name;
    DocElement* code = doc_alloc_element(arena, DocElemType::CODE_BLOCK);
    code->text.text = nullptr;
    code->text.text_len = 0;
    code->text.style = DocTextStyle::plain();
    
    // Collect all text content
    StrBuf* text_buf = strbuf_new_cap(1024);
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        // Skip option brackets
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            if (child_tag && (tag_eq(child_tag, "brack_group") || tag_eq(child_tag, "optional"))) {
                continue;
            }
        }
        collect_text_recursive(child, text_buf);
    }
    
    // Copy to arena
    if (text_buf->length > 0) {
        char* code_text = (char*)arena_alloc(arena, text_buf->length + 1);
        memcpy(code_text, text_buf->str, text_buf->length + 1);
        code->text.text = code_text;
        code->text.text_len = text_buf->length;
    }
    
    strbuf_free(text_buf);
    
    return code;
}

#endif // DOC_MODEL_MINIMAL

} // namespace tex
