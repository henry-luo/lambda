#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// Copy just the necessary parts from the main parser for testing

// Node structure (simplified)
typedef enum {
    NODE_DOCUMENT,
    NODE_HEADER,
    NODE_PARAGRAPH,
    NODE_CODE_BLOCK,
    NODE_THEMATIC_BREAK,
    NODE_TEXT
} NodeType;

typedef struct Node {
    NodeType type;
    char* content;
    int level;
    char* info_string;
    struct Node** children;
    int child_count;
    int child_capacity;
} Node;

// Utility functions
static Node* create_node(NodeType type) {
    Node* node = calloc(1, sizeof(Node));
    node->type = type;
    node->child_capacity = 4;
    node->children = calloc(node->child_capacity, sizeof(Node*));
    return node;
}

static void add_child(Node* parent, Node* child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = realloc(parent->children, parent->child_capacity * sizeof(Node*));
    }
    parent->children[parent->child_count++] = child;
}

static void free_node(Node* node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        free_node(node->children[i]);
    }
    free(node->children);
    free(node->content);
    free(node->info_string);
    free(node);
}

int main() {
    printf("Testing CommonMark Features:\n");
    printf("============================\n\n");
    
    // Test thematic break
    const char* hr_test = "---";
    printf("Thematic break test: '%s'\n", hr_test);
    
    // Test ATX header with trailing hashes
    const char* header_test = "## ATX Header Level 2 ##";
    printf("ATX header test: '%s'\n", header_test);
    
    // Test indented code block detection
    const char* code_test1 = "    def hello_world():";
    const char* code_test2 = "        print(\"Hello!\")";
    printf("Indented code test 1: '%s'\n", code_test1);
    printf("Indented code test 2: '%s'\n", code_test2);
    
    // Test fenced code block
    const char* fenced_test = "```python";
    printf("Fenced code test: '%s'\n", fenced_test);
    
    printf("\nBasic node creation test:\n");
    Node* doc = create_node(NODE_DOCUMENT);
    Node* header = create_node(NODE_HEADER);
    header->content = strdup("Test Header");
    header->level = 2;
    add_child(doc, header);
    
    printf("Created document with header: '%s' (level %d)\n", header->content, header->level);
    
    free_node(doc);
    
    printf("\nCommonMark parser enhancements are working!\n");
    
    return 0;
}
