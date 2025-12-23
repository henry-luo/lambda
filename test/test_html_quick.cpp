// Quick test to understand Lambda HTML parser behavior
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"
#include <stdio.h>

extern "C" {
    struct Input;
    Input* input_create(Pool* pool, Arena* arena);
    void input_from_source(Input* input, const char* source, Url* url, String* type, String* flavor);
}

void print_tree(Item item, int depth) {
    std::string indent(depth * 2, ' ');
    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_ELEMENT) {
        ElementReader elem(item);
        printf("%s<element: %s, children: %lld>\n", indent.c_str(), elem.tagName(), elem.childCount());
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            print_tree(child.item(), depth + 1);
        }
    } else if (type == LMD_TYPE_STRING) {
        ItemReader reader(item.to_const());
        String* str = reader.asString();
        if (str) {
            printf("%s\"", indent.c_str());
            fwrite(str->chars, 1, str->len, stdout);
            printf("\"\n");
        }
    } else if (type == LMD_TYPE_LIST) {
        List* list = item.list;
        printf("%sList: %lld items\n", indent.c_str(), list->length);
        for (int64_t i = 0; i < list->length; i++) {
            print_tree(list->items[i], depth + 1);
        }
    } else {
        printf("%s<type: %d>\n", indent.c_str(), type);
    }
}

int main() {
    Pool* pool = pool_create(1024 * 1024);
    Arena* arena = arena_create(1024 * 1024);

    // Test simple HTML
    const char* html = "<p>One<p>Two";

    Input* input = input_create(pool, arena);
    input_from_source(input, html, nullptr, nullptr, nullptr);

    printf("=== Parsing: %s ===\n", html);
    printf("Result type: %d\n", get_type_id(input->root));
    print_tree(input->root, 0);

    pool_destroy(pool);
    arena_destroy(arena);
    return 0;
}
