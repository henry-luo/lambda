// Simple standalone test to debug MarkEditor
#include <stdio.h>
#include <assert.h>
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_editor.hpp"
#include "../lib/mempool.h"

int main() {
    printf("Creating pool and input...\n");
    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    
    printf("Building initial map...\n");
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .final();
    
    printf("Doc type: %d (expected %d for MAP)\n", doc._type_id, LMD_TYPE_MAP);
    assert(doc._type_id == LMD_TYPE_MAP);
    
    input->root = doc;
    
    printf("Creating editor...\n");
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    printf("Getting builder from editor...\n");
    MarkBuilder* edit_builder = editor.builder();
    printf("Builder ptr: %p\n", edit_builder);
    assert(edit_builder != nullptr);
    
    printf("Creating value with builder...\n");
    Item new_age = edit_builder->createLong(31);
    printf("New age type: %d (expected %d for INT64)\n", new_age._type_id, LMD_TYPE_INT64);
    assert(new_age._type_id == LMD_TYPE_INT64);
    
    printf("Updating map...\n");
    Item updated = editor.map_update(doc, "age", new_age);
    printf("Updated type: %d (expected %d for MAP)\n", updated._type_id, LMD_TYPE_MAP);
    
    if (updated._type_id != LMD_TYPE_MAP) {
        printf("ERROR: Update failed\n");
        pool_destroy(pool);
        return 1;
    }
    
    printf("✅ Test passed!\n");
    pool_destroy(pool);
    return 0;
}
