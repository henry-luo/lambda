#include <stdio.h>
#include <string.h>
#include "lambda/input/html5_parser.h"
#include "lambda/input/input.h"
#include "lib/pool.h"

int main() {
    // Create pool
    Pool* pool = pool_create();

    // Create input
    Input* input = (Input*)pool_calloc(pool, sizeof(Input));
    input->pool = pool;
    input->type_list = arraylist_new(10);
    input->sb = stringbuf_new(pool);

    // Parse simple HTML
    const char* html = "<html><head></head><body></body></html>";
    Element* doc = html5_parse(input, html, strlen(html), pool);

    printf("doc: %p\n", (void*)doc);
    if (doc) {
        printf("doc->length: %ld\n", doc->length);
        printf("doc->items: %p\n", (void*)doc->items);

        if (doc->length > 0 && doc->items) {
            printf("First child:\n");
            Item item = doc->items[0];
            printf("  item.type_id: 0x%02x\n", item.type_id);
            printf("  item.pointer: 0x%016llx\n", (unsigned long long)item.pointer);

            if (item.type_id == LMD_TYPE_ELEMENT) {
                Element* child = (Element*)item.pointer;
                printf("  child element: %p\n", (void*)child);
                if (child && child->type) {
                    TypeElmt* type = (TypeElmt*)child->type;
                    printf("  tag name: %s\n", type->name.str);
                }
            }
        }
    }

    pool_destroy(pool);
    return 0;
}
