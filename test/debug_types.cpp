// Quick debug test
#include <stdio.h>
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_editor.hpp"
#include "../lib/mempool.h"

int main() {
    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    
    // Create map with int64_t using put
    MarkBuilder builder1(input);
    Item doc1 = builder1.map()
        .put("age", (int64_t)30)
        .final();
    
    printf("Initial map 'age' field:\n");
    ConstItem age1 = doc1.map->get("age");
    printf("  type_id: %d (expected 4 for INT64)\n", age1.type_id());
    
    // Create value with createLong
    MarkBuilder builder2(input);
    Item val = builder2.createLong(31);
    printf("\ncreated value with createLong(31):\n");
    printf("  type_id: %d (expected 4 for INT64)\n", val.type_id());
    
    pool_destroy(pool);
    return 0;
}
