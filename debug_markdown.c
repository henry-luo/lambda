#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lambda/lambda.h"
#include "../lib/strbuf.h"

// Forward declarations
extern __thread Context* context;
void heap_init();
void frame_start();
void frame_end();
void heap_destroy();

typedef struct Input {
    void* url;
    void* path;
    VariableMemPool* pool;
    ArrayList* type_list;
    Item root;
    StrBuf* sb;
} Input;

Input* input_from_source(char* source, lxb_url_t* abs_url, String* type, String* flavor);
String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
lxb_url_t* get_current_dir();
lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);

String* create_lambda_string(const char* text) {
    if (!text) return NULL;
    
    size_t len = strlen(text);
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;
    
    result->len = len;
    result->ref_cnt = 1;
    strcpy(result->chars, text);
    
    return result;
}

void init_test_context() {
    heap_init();
    frame_start();
}

void cleanup_test_context() {
    frame_end();
    heap_destroy();
}

int main() {
    init_test_context();
    
    const char* complex_md = "# Main Header\n\n"
        "This is a **bold** paragraph with *italic* text and `code snippets`.\n\n"
        "## Subheader\n\n"
        "Here's a list:\n"
        "- First item\n"
        "- Second item with **emphasis**\n"
        "- Third item\n\n"
        "### Code Example\n\n"
        "```javascript\n"
        "function hello() {\n"
        "    console.log('Hello, World!');\n"
        "}\n"
        "```\n\n"
        "And a [link](http://example.com) for good measure.\n\n"
        "> This is a blockquote with some **bold** text.";
    
    printf("=== Debug Markdown Roundtrip ===\n");
    printf("Original markdown:\n%s\n\n", complex_md);
    
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;
    
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.md");
    
    char* md_copy = strdup(complex_md);
    
    Input* input = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    if (!input) {
        printf("ERROR: Failed to parse markdown input\n");
        cleanup_test_context();
        return 1;
    }
    
    printf("Markdown parsing successful, root item: %p\n", (void*)input->root.pointer);
    
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    if (!formatted) {
        printf("ERROR: Failed to format markdown data\n");
        cleanup_test_context();
        return 1;
    }
    
    printf("Formatted markdown:\n%s\n", formatted->chars);
    printf("Formatted length: %d\n", formatted->len);
    
    // Compare character by character
    printf("\n=== Character-by-character comparison ===\n");
    size_t orig_len = strlen(complex_md);
    size_t fmt_len = formatted->len;
    size_t max_len = (orig_len > fmt_len) ? orig_len : fmt_len;
    
    for (size_t i = 0; i < max_len; i++) {
        char orig_c = (i < orig_len) ? complex_md[i] : '\0';
        char fmt_c = (i < fmt_len) ? formatted->chars[i] : '\0';
        
        if (orig_c != fmt_c) {
            printf("Difference at position %zu: orig='%c'(%d) fmt='%c'(%d)\n", 
                   i, orig_c, (int)orig_c, fmt_c, (int)fmt_c);
            
            // Show context around the difference
            printf("Original context: \"");
            for (int j = -5; j <= 5; j++) {
                int pos = (int)i + j;
                if (pos >= 0 && pos < (int)orig_len) {
                    char c = complex_md[pos];
                    if (c == '\n') printf("\\n");
                    else if (c == '\t') printf("\\t");
                    else if (c >= 32 && c < 127) printf("%c", c);
                    else printf("\\x%02x", (unsigned char)c);
                }
            }
            printf("\"\n");
            
            printf("Formatted context: \"");
            for (int j = -5; j <= 5; j++) {
                int pos = (int)i + j;
                if (pos >= 0 && pos < (int)fmt_len) {
                    char c = formatted->chars[pos];
                    if (c == '\n') printf("\\n");
                    else if (c == '\t') printf("\\t");
                    else if (c >= 32 && c < 127) printf("%c", c);
                    else printf("\\x%02x", (unsigned char)c);
                }
            }
            printf("\"\n");
            break;
        }
    }
    
    if (orig_len != fmt_len) {
        printf("Length difference: original=%zu, formatted=%zu\n", orig_len, fmt_len);
    } else {
        printf("Same length, content matches\n");
    }
    
    cleanup_test_context();
    return 0;
}
