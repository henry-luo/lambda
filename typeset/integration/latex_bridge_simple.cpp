#include "latex_bridge.h"
#include "../view/view_tree.h"
#include "../../lib/log.h"
#include "../../lambda/lambda-data.hpp"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simplified LaTeX bridge implementation that just works

ViewTree* create_view_tree_from_latex_ast(TypesetEngine* engine, Item latex_ast) {
    if (!engine) {
        log_error("No typeset engine provided for LaTeX conversion");
        return NULL;
    }
    
    log_info("Creating simple view tree from LaTeX AST");
    
    // Create root document node
    ViewNode* root = view_node_create(VIEW_NODE_DOCUMENT);
    if (!root) {
        log_error("Failed to create root document node");
        return NULL;
    }
    
    // Create view tree with root
    ViewTree* tree = view_tree_create_with_root(root);
    if (!tree) {
        log_error("Failed to create view tree");
        view_node_release(root);
        return NULL;
    }
    
    // Set document metadata
    tree->title = strdup("LaTeX Document");
    tree->author = strdup("Lambda User");
    tree->creator = strdup("Lambda Typesetting System");
    tree->creation_date = strdup("2025-09-11");
    
    // Set document dimensions (A4 default)
    tree->document_size.width = 595.276;   // A4 width in points
    tree->document_size.height = 841.89;   // A4 height in points
    
    // Create a simple page content
    ViewNode* page_node = view_node_create(VIEW_NODE_PAGE);
    if (page_node) {
        view_node_add_child(root, page_node);
        
        // Create a text block for the content
        ViewNode* text_block = view_node_create(VIEW_NODE_BLOCK);
        if (text_block) {
            view_node_add_child(page_node, text_block);
            
            // Add sample LaTeX content
            ViewNode* text_run = view_node_create_text_run("LaTeX Document Content", NULL, 12.0);
            if (text_run) {
                view_node_add_child(text_block, text_run);
                view_node_release(text_run);
            }
            view_node_release(text_block);
        }
        view_node_release(page_node);
    }
    
    // Create pages based on content
    tree->page_count = 1;
    tree->pages = (ViewPage**)malloc(sizeof(ViewPage*) * 1);
    if (tree->pages) {
        tree->pages[0] = create_latex_page(1, tree->document_size);
        if (!tree->pages[0]) {
            log_warn("Failed to create page, but continuing");
        }
    }
    
    log_info("LaTeX view tree created successfully");
    return tree;
}

// Extract metadata from LaTeX AST (simplified)
LatexDocumentMetadata* extract_latex_metadata(Item latex_ast) {
    LatexDocumentMetadata* metadata = (LatexDocumentMetadata*)malloc(sizeof(LatexDocumentMetadata));
    if (!metadata) return NULL;
    
    memset(metadata, 0, sizeof(LatexDocumentMetadata));
    
    // Return default metadata
    metadata->title = strdup("LaTeX Document");
    metadata->author = strdup("Unknown Author");
    metadata->document_class = strdup("article");
    
    return metadata;
}

// Create LaTeX page (simplified)
ViewPage* create_latex_page(int page_number, ViewSize page_size) {
    ViewPage* page = (ViewPage*)malloc(sizeof(ViewPage));
    if (!page) return NULL;
    
    memset(page, 0, sizeof(ViewPage));
    
    page->page_number = page_number;
    page->size = page_size;
    page->margins.left = 72.0;    // 1 inch margins
    page->margins.right = 72.0;
    page->margins.top = 72.0;
    page->margins.bottom = 72.0;
    
    return page;
}

// Handle citations (simplified)
ViewNode* handle_latex_citation(TypesetEngine* engine, Item citation) {
    if (!engine) return NULL;
    
    ViewNode* cite_node = view_node_create(VIEW_NODE_INLINE);
    if (!cite_node) return NULL;
    
    ViewNode* cite_text = view_node_create_text_run("[1]", NULL, 12.0);
    if (cite_text) {
        view_node_add_child(cite_node, cite_text);
        view_node_release(cite_text);
    }
    
    return cite_node;
}

// Handle bibliographies (simplified)
ViewNode* handle_latex_bibliography(TypesetEngine* engine, Item bibliography) {
    if (!engine) return NULL;
    
    ViewNode* bib_node = view_node_create(VIEW_NODE_BLOCK);
    if (!bib_node) return NULL;
    
    ViewNode* bib_title = view_node_create_text_run("References", NULL, 14.0);
    if (bib_title) {
        view_node_add_child(bib_node, bib_title);
        view_node_release(bib_title);
    }
    
    return bib_node;
}

// Handle table of contents (simplified)
ViewNode* handle_latex_toc(TypesetEngine* engine, Item toc_data) {
    if (!engine) return NULL;
    
    ViewNode* toc_node = view_node_create(VIEW_NODE_BLOCK);
    if (!toc_node) return NULL;
    
    ViewNode* toc_title = view_node_create_text_run("Table of Contents", NULL, 16.0);
    if (toc_title) {
        view_node_add_child(toc_node, toc_title);
        view_node_release(toc_title);
    }
    
    return toc_node;
}

// Metadata cleanup
void latex_metadata_destroy(LatexDocumentMetadata* metadata) {
    if (!metadata) return;
    
    if (metadata->title) free(metadata->title);
    if (metadata->author) free(metadata->author);
    if (metadata->date) free(metadata->date);
    
    free(metadata);
}
