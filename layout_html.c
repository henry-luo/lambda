#include "layout.h"

void print_view_tree(ViewGroup* view_block);
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);

void layout_init(LayoutContext* lycon, Document* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;
    // most browsers use a generic sans-serif font as the default
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 px for most browsers
    setup_font(uicon, &lycon->font, "Hei", &default_font_prop);
}

void layout_cleanup(LayoutContext* lycon) {
}

void layout_html_doc(UiContext* uicon, Document *doc, bool is_reflow) {
    LayoutContext lycon;
    if (!doc) return;
    if (is_reflow) {
        // free existing view tree
        if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = calloc(1, sizeof(ViewTree));
    }
    view_pool_init(doc->view_tree);

    lxb_dom_element_t *body = (lxb_dom_element_t*)lxb_html_document_body_element(doc->dom_tree);
    if (body) {
        printf("start to layout DOM tree\n");
        
        layout_init(&lycon, doc, uicon);
        doc->view_tree->root = alloc_view(&lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)body);
        
        lycon.parent = (ViewGroup*)doc->view_tree->root;
        lycon.block.width = uicon->window_width;  
        lycon.block.height = uicon->window_height;
        lycon.block.advance_y = 0;  lycon.block.max_width = 800;
        lycon.block.line_height = round(1.2 * 16 * uicon->pixel_ratio);  
        lycon.block.text_align = LXB_CSS_VALUE_LEFT;
        lycon.line.is_line_start = true;
        printf("start to layout body\n");
        layout_block(&lycon, (lxb_html_element_t*)body, LXB_CSS_VALUE_BLOCK);
        printf("end layout\n");

        layout_cleanup(&lycon);
        
        print_view_tree((ViewGroup*)doc->view_tree->root);
    }
}

