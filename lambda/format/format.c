#include "../transpiler.h"

String* format_json(VariableMemPool* pool, Item root_item);
void format_markdown(StrBuf* sb, Item root_item);
String* format_xml(VariableMemPool* pool, Item root_item);
String* format_html(VariableMemPool* pool, Item root_item);

String* format_data(Context* ctx, Item item, String* type) {
    String* result = NULL;
    if (strcmp(type->chars, "json") == 0) {
        result = format_json(ctx->heap->pool, item);
    }
    else if (strcmp(type->chars, "markdown") == 0) {
        StrBuf* sb = strbuf_new_pooled(ctx->heap->pool);
        format_markdown(sb, item);
        result = strbuf_to_string(sb);
        strbuf_free(sb);
    }
    else if (strcmp(type->chars, "xml") == 0) {
        result = format_xml(ctx->heap->pool, item);
    }
    else if (strcmp(type->chars, "html") == 0) {
        result = format_html(ctx->heap->pool, item);
    }
    else {
        printf("Unsupported format type: %s\n", type->chars);
    }
    if (result) {
        arraylist_append(ctx->heap->entries, (void*)s2it(result));
    }
    return result;
}
