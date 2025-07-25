#include "format.h"

// Create a Lambda Item from raw field data with proper type tagging
Item create_item_from_field_data(void* field_data, TypeId type_id) {
    Item item = 0;
    
    switch (type_id) {
        case LMD_TYPE_BOOL:
            item = *(bool*)field_data ? 1 : 0;
            item |= ((uint64_t)LMD_TYPE_BOOL << 56);
            break;
        case LMD_TYPE_INT:
            item = *(int64_t*)field_data;
            item |= ((uint64_t)LMD_TYPE_INT << 56);
            break;
        case LMD_TYPE_FLOAT:
            item = (uint64_t)field_data;
            item |= ((uint64_t)LMD_TYPE_FLOAT << 56);
            break;
        case LMD_TYPE_STRING:
            item = (uint64_t)*(void**)field_data;
            item |= ((uint64_t)LMD_TYPE_STRING << 56);
            break;
        case LMD_TYPE_ARRAY:
            item = (uint64_t)*(void**)field_data;
            item |= ((uint64_t)LMD_TYPE_ARRAY << 56);
            break;
        case LMD_TYPE_MAP:
            item = (uint64_t)*(void**)field_data;
            item |= ((uint64_t)LMD_TYPE_MAP << 56);
            break;
        default:
            // fallback for unknown types
            item = (uint64_t)field_data;
            item |= ((uint64_t)type_id << 56);
            break;
    }
    
    return item;
}

// Common number formatting function
void format_number(StrBuf* sb, Item item) {
    TypeId type = get_type_id((LambdaItem)item);
    
    if (type == LMD_TYPE_INT) {
        // 56-bit signed integer stored directly in the item
        int64_t val = get_int_value(item);
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, val);
        strbuf_append_str(sb, num_buf);
    } else if (type == LMD_TYPE_FLOAT) {
        // Double stored as pointer
        double* dptr = (double*)get_pointer(item);
        if (dptr) {
            char num_buf[32];
            // Check for special values
            if (isnan(*dptr)) {
                strbuf_append_str(sb, "null");
            } else if (isinf(*dptr)) {
                strbuf_append_str(sb, "null");
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.15g", *dptr);
                strbuf_append_str(sb, num_buf);
            }
        } else {
            strbuf_append_str(sb, "null");
        }
    } else if (type == LMD_TYPE_INT64) {
        // 64-bit integer stored as pointer
        long* lptr = (long*)get_pointer(item);
        if (lptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%ld", *lptr);
            strbuf_append_str(sb, num_buf);
        } else {
            strbuf_append_str(sb, "null");
        }
    }
}

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
    else if (strcmp(type->chars, "yaml") == 0) {
        result = format_yaml(ctx->heap->pool, item);
    }
    else if (strcmp(type->chars, "toml") == 0) {
        result = format_toml(ctx->heap->pool, item);
    }
    else {
        printf("Unsupported format type: %s\n", type->chars);
    }
    if (result) {
        arraylist_append(ctx->heap->entries, (void*)s2it(result));
    }
    return result;
}
