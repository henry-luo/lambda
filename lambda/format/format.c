#include "../transpiler.h"

String* format_json(VariableMemPool* pool, Item root_item);

String* format_data(Context* ctx, Item item, String* type) {
    if (strcmp(type->chars, "json") == 0) {
        return format_json(ctx->heap->pool, item);
    }
    else {
        printf("Unsupported format type: %s\n", type->chars);
    }
    return NULL;
}
