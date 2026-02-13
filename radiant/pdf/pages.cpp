// radiant/pdf/pages.cpp
// PDF Page Tree Navigation Implementation

extern "C" {
    #include "../../lambda/lambda.h"
}

#include "pages.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/str.h"
#include <string.h>
#include <stdlib.h>

// Helper to create string key from literal
static String* create_string_key(Pool* pool, const char* str) {
    size_t len = strlen(str);
    String* key = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (!key) return nullptr;

    str_copy(key->chars, len + 1, str, len);
    key->len = len;
    key->ref_cnt = 0;
    return key;
}

// Helper to get map value with string key
static Item map_get_str(Map* map, const char* key_str, Pool* pool) {
    ConstItem result = map->get(key_str);
    return *(Item*)&result;
}

/**
 * Resolve an indirect reference to an actual object
 */
Item pdf_resolve_reference(Map* pdf_data, Item ref_obj, Pool* pool) {
    if (ref_obj.item == ITEM_NULL) return ref_obj;

    // Check if this is actually a map before accessing
    TypeId ref_type = ref_obj.type_id();
    if (ref_type != LMD_TYPE_MAP && ref_type != LMD_TYPE_ELEMENT) {
        // Not a map, return as-is
        return ref_obj;
    }

    // Check if this is an indirect reference map
    Map* ref_map = ref_obj.map;

    // Look for "type" field
    Item type_item = map_get_str(ref_map, "type", pool);
    if (type_item.item == ITEM_NULL) {
        // Not a reference, return as-is
        return ref_obj;
    }

    String* type_str = type_item.get_string();
    if (!type_str || strcmp(type_str->chars, "indirect_ref") != 0) {
        // Not an indirect reference
        return ref_obj;
    }

    // Get object number from reference
    // Try both "obj_num" and "object_num" (PDF parser uses "object_num")
    Item obj_num_item = map_get_str(ref_map, "object_num", pool);
    if (obj_num_item.item == ITEM_NULL) {
        obj_num_item = map_get_str(ref_map, "obj_num", pool);
    }
    if (obj_num_item.item == ITEM_NULL) {
        log_warn("Indirect reference missing obj_num/object_num");
        return {.item = ITEM_NULL};
    }

    int target_obj_num = (int)obj_num_item.get_double();

    log_debug("Resolving indirect reference: %d 0 R", target_obj_num);

    // Search objects array for matching object number
    Item objects_item = map_get_str(pdf_data, "objects", pool);
    if (objects_item.item == ITEM_NULL) {
        log_warn("No objects array in PDF data");
        return {.item = ITEM_NULL};
    }

    Array* objects = objects_item.array;

    // Search for object with matching obj_num
    for (int i = 0; i < objects->length; i++) {
        Item obj_item = objects->items[i];
        if (obj_item.item == ITEM_NULL) continue;

        Map* obj_map = obj_item.map;

        // Check if this is an indirect_object
        Item obj_type_item = map_get_str(obj_map, "type", pool);
        if (obj_type_item.item == ITEM_NULL) continue;

        String* obj_type_str = obj_type_item.get_string();
        if (!obj_type_str || strcmp(obj_type_str->chars, "indirect_object") != 0) continue;

        // Check obj_num matches
        // Try both "obj_num" and "object_num" (PDF parser uses "object_num")
        Item obj_num_field_item = map_get_str(obj_map, "object_num", pool);
        if (obj_num_field_item.item == ITEM_NULL) {
            obj_num_field_item = map_get_str(obj_map, "obj_num", pool);
        }
        if (obj_num_field_item.item == ITEM_NULL) continue;

        int this_obj_num = (int)obj_num_field_item.get_double();

        if (this_obj_num == target_obj_num) {
            // Found matching object - return its content
            Item content_item = map_get_str(obj_map, "content", pool);
            log_debug("Resolved object %d to content", target_obj_num);
            return content_item;
        }
    }

    log_warn("Could not resolve indirect reference %d 0 R", target_obj_num);
    return {.item = ITEM_NULL};
}

/**
 * Extract MediaBox from page dictionary or inherited from parent
 */
bool pdf_extract_media_box(Map* page_dict, Map* pdf_data, double* media_box) {
    if (!page_dict || !media_box) return false;

    // We need a pool for string keys - assume we can create temp strings
    // This is a bit inefficient but necessary without a provided pool
    // In practice, this function is called with a pool from pdf_get_page_info
    Pool* temp_pool = pool_create();
    if (!temp_pool) return false;

    // Look for MediaBox in current dictionary
    Item media_box_item = map_get_str(page_dict, "MediaBox", temp_pool);

    if (media_box_item.item != ITEM_NULL) {
        // Resolve if it's an indirect reference
        media_box_item = pdf_resolve_reference(pdf_data, media_box_item, temp_pool);

        if (media_box_item.item != ITEM_NULL) {
            // Check if it's actually an array
            TypeId box_type = media_box_item.type_id();
            if (box_type != LMD_TYPE_ARRAY && box_type != LMD_TYPE_LIST) {
                pool_destroy(temp_pool);
                return false;
            }

            Array* box_array = media_box_item.array;
            if (box_array && box_array->length >= 4) {
                for (int i = 0; i < 4; i++) {
                    Item val_item = box_array->items[i];
                    if (val_item.item != ITEM_NULL) {
                        TypeId val_type = val_item.type_id();
                        if (val_type == LMD_TYPE_FLOAT || val_type == LMD_TYPE_INT || val_type == LMD_TYPE_INT64) {
                            media_box[i] = val_item.get_double();
                        } else {
                            media_box[i] = 0.0;
                        }
                    } else {
                        media_box[i] = 0.0;
                    }
                }
                log_debug("Extracted MediaBox: [%.2f, %.2f, %.2f, %.2f]",
                         media_box[0], media_box[1], media_box[2], media_box[3]);
                pool_destroy(temp_pool);
                return true;
            }
        }
    }

    // MediaBox not found - try inheriting from Parent
    Item parent_item = map_get_str(page_dict, "Parent", temp_pool);
    if (parent_item.item != ITEM_NULL) {
        // Resolve parent reference
        parent_item = pdf_resolve_reference(pdf_data, parent_item, temp_pool);
        if (parent_item.item != ITEM_NULL) {
            Map* parent_dict = parent_item.map;
            bool result = pdf_extract_media_box(parent_dict, pdf_data, media_box);
            pool_destroy(temp_pool);
            return result;
        }
    }

    // Default to US Letter size
    log_warn("MediaBox not found, using default US Letter size");
    media_box[0] = 0.0;
    media_box[1] = 0.0;
    media_box[2] = 612.0;  // 8.5 inches
    media_box[3] = 792.0;  // 11 inches
    pool_destroy(temp_pool);
    return false;
}/**
 * Recursively traverse Pages tree and collect page dictionaries
 */
static void collect_pages(Map* pdf_data, Item node_item, Array* pages_array, Pool* pool) {
    if (node_item.item == ITEM_NULL) return;

    // Resolve if indirect reference
    node_item = pdf_resolve_reference(pdf_data, node_item, pool);
    if (node_item.item == ITEM_NULL) return;


    // Check if this is actually a map
    TypeId node_type = node_item.type_id();
    if (node_type != LMD_TYPE_MAP && node_type != LMD_TYPE_ELEMENT) {
        log_warn("collect_pages: expected map but got type %d", node_type);
        return;
    }

    Map* node_dict = node_item.map;
    if (!node_dict) {
        log_warn("collect_pages: node_dict is null");
        return;
    }

    // Check Type field
    Item type_item = map_get_str(node_dict, "Type", pool);
    if (type_item.item == ITEM_NULL) {
        // Type field missing - check if this looks like a Page by checking for Contents or MediaBox
        Item contents_item = map_get_str(node_dict, "Contents", pool);
        Item mediabox_item = map_get_str(node_dict, "MediaBox", pool);

        if (contents_item.item != ITEM_NULL || mediabox_item.item != ITEM_NULL) {
            // This looks like a leaf Page node - add to array
            fprintf(stderr, "Found Page node (no Type field but has Contents/MediaBox), adding to collection\n");

            // Use array_append to add the item
            array_append(pages_array, node_item, pool);
            return;
        }

        log_warn("Pages tree node missing Type field and no Contents/MediaBox");
        return;
    }

    String* type_str = type_item.get_string();
    if (!type_str) {
        log_warn("Failed to get type string from Pages tree node");
        return;
    }

    if (strcmp(type_str->chars, "Pages") == 0) {
        // This is an intermediate Pages node - recurse into Kids
        log_debug("Found Pages node, recursing into Kids");

        Item kids_item = map_get_str(node_dict, "Kids", pool);
        if (kids_item.item == ITEM_NULL) {
            log_warn("Pages node missing Kids array");
            return;
        }

        // Resolve Kids if it's an indirect reference
        kids_item = pdf_resolve_reference(pdf_data, kids_item, pool);
        if (kids_item.item == ITEM_NULL) return;

        Array* kids_array = kids_item.array;


        // Recurse into each kid
        for (int i = 0; i < kids_array->length; i++) {
            collect_pages(pdf_data, kids_array->items[i], pages_array, pool);
        }
    } else if (strcmp(type_str->chars, "Page") == 0) {
        // This is a leaf Page node - add to array
        log_debug("Found Page node, adding to collection");
        array_append(pages_array, node_item, pool);
    } else {
        log_warn("Unknown Type in Pages tree: %s", type_str->chars);
    }
}

/**
 * Get the number of pages in a PDF document
 */
int pdf_get_page_count_from_data(Map* pdf_data) {
    if (!pdf_data) return 0;

    log_debug("Getting page count from PDF data");

    Pool* temp_pool = pool_create();
    if (!temp_pool) return 0;    // Get trailer
    Item trailer_item = map_get_str(pdf_data, "trailer", temp_pool);
    if (trailer_item.item == ITEM_NULL) {
        log_warn("No trailer in PDF data");
        pool_destroy(temp_pool);
        return 0;
    }

    Map* trailer_map = trailer_item.map;

    // Get dictionary from trailer
    Item dict_item = map_get_str(trailer_map, "dictionary", temp_pool);
    if (dict_item.item == ITEM_NULL) {
        log_warn("No dictionary in trailer");
        pool_destroy(temp_pool);
        return 0;
    }

    Map* trailer_dict = dict_item.map;

    // Get Root (Catalog)
    Item root_item = map_get_str(trailer_dict, "Root", temp_pool);
    if (root_item.item == ITEM_NULL) {
        log_warn("No Root in trailer dictionary");
        pool_destroy(temp_pool);
        return 0;
    }

    // Resolve Root reference
    root_item = pdf_resolve_reference(pdf_data, root_item, temp_pool);
    if (root_item.item == ITEM_NULL) {
        log_warn("Could not resolve Root reference");
        pool_destroy(temp_pool);
        return 0;
    }

    Map* catalog = root_item.map;

    // Get Pages from catalog
    Item pages_item = map_get_str(catalog, "Pages", temp_pool);
    if (pages_item.item == ITEM_NULL) {
        log_warn("No Pages in catalog");
        pool_destroy(temp_pool);
        return 0;
    }

    // Resolve Pages reference
    pages_item = pdf_resolve_reference(pdf_data, pages_item, temp_pool);
    if (pages_item.item == ITEM_NULL) {
        log_warn("Could not resolve Pages reference");
        pool_destroy(temp_pool);
        return 0;
    }

    Map* pages_dict = pages_item.map;

    // Get Count from Pages dictionary
    Item count_item = map_get_str(pages_dict, "Count", temp_pool);
    if (count_item.item == ITEM_NULL) {
        log_warn("No Count in Pages dictionary, traversing tree");

        // Fall back to counting by traversing tree
        Array* pages_array = array_pooled(temp_pool);
        if (!pages_array) {
            pool_destroy(temp_pool);
            return 0;
        }

        collect_pages(pdf_data, {.item = (uint64_t)pages_dict}, pages_array, temp_pool);
        int count = pages_array->length;
        fprintf(stderr, "Counted %d pages by tree traversal (array length=%d, capacity=%d)\n", count, pages_array->length, pages_array->capacity);
        log_info("Counted %d pages by tree traversal", count);
        pool_destroy(temp_pool);
        return count;
    }

    int count = (int)count_item.get_double();

    log_info("PDF has %d pages", count);
    pool_destroy(temp_pool);
    return count;
}

/**
 * Extract information for a specific page
 */
PDFPageInfo* pdf_get_page_info(Map* pdf_data, int page_index, Pool* pool) {
    if (!pdf_data || page_index < 0) return nullptr;

    log_info("Extracting info for page %d", page_index + 1);

    // Get trailer -> Root -> Pages tree
    Item trailer_item = map_get_str(pdf_data, "trailer", pool);
    if (trailer_item.item == ITEM_NULL) {
        log_error("No trailer in PDF data");
        return nullptr;
    }

    Map* trailer_map = trailer_item.map;

    Item dict_item = map_get_str(trailer_map, "dictionary", pool);
    if (dict_item.item == ITEM_NULL) return nullptr;

    Map* trailer_dict = dict_item.map;

    Item root_item = map_get_str(trailer_dict, "Root", pool);
    if (root_item.item == ITEM_NULL) return nullptr;

    root_item = pdf_resolve_reference(pdf_data, root_item, pool);
    if (root_item.item == ITEM_NULL) return nullptr;

    Map* catalog = root_item.map;

    Item pages_item = map_get_str(catalog, "Pages", pool);
    if (pages_item.item == ITEM_NULL) return nullptr;

    pages_item = pdf_resolve_reference(pdf_data, pages_item, pool);
    if (pages_item.item == ITEM_NULL) return nullptr;

    // Collect all pages
    Array* pages_array = array_pooled(pool);
    if (!pages_array) return nullptr;

    collect_pages(pdf_data, pages_item, pages_array, pool);

    if (page_index >= pages_array->length) {
        log_error("Page index %d out of range (have %d pages)", page_index, pages_array->length);
        return nullptr;
    }


    // Get specific page
    Item page_item = pages_array->items[page_index];
    page_item = pdf_resolve_reference(pdf_data, page_item, pool);
    if (page_item.item == ITEM_NULL) {
        log_error("Could not resolve page %d", page_index);
        return nullptr;
    }

    Map* page_dict = page_item.map;

    // Create page info structure
    PDFPageInfo* page_info = (PDFPageInfo*)pool_calloc(pool, sizeof(PDFPageInfo));
    if (!page_info) return nullptr;

    page_info->page_number = page_index + 1;

    // Extract MediaBox
    pdf_extract_media_box(page_dict, pdf_data, page_info->media_box);

    // Extract CropBox (optional)
    Item crop_box_item = map_get_str(page_dict, "CropBox", pool);
    if (crop_box_item.item != ITEM_NULL && crop_box_item.type_id() != LMD_TYPE_NULL) {
        crop_box_item = pdf_resolve_reference(pdf_data, crop_box_item, pool);
        if (crop_box_item.item != ITEM_NULL && crop_box_item.type_id() != LMD_TYPE_NULL) {
            Array* crop_array = crop_box_item.array;
            if (crop_array && crop_array->length >= 4) {
                for (int i = 0; i < 4; i++) {
                    Item val_item = crop_array->items[i];
                    page_info->crop_box[i] = (val_item.item != ITEM_NULL) ? val_item.get_double() : 0.0;
                }
                page_info->has_crop_box = true;
            }
        }
    }

    // Extract Resources (must be done early, before any returns from Contents processing)
    Item resources_item = map_get_str(page_dict, "Resources", pool);
    log_debug("Looking up Resources for page %d: item=0x%llx, type=%d",
              page_index + 1, (unsigned long long)resources_item.item, resources_item.type_id());
    if (resources_item.item != ITEM_NULL) {
        resources_item = pdf_resolve_reference(pdf_data, resources_item, pool);
        if (resources_item.item != ITEM_NULL && resources_item.type_id() == LMD_TYPE_MAP) {
            page_info->resources = resources_item.map;
            log_debug("Extracted Resources for page %d", page_index + 1);
        }
    }

    // Extract Contents (content streams)
    Item contents_item = map_get_str(page_dict, "Contents", pool);
    if (contents_item.item == ITEM_NULL || contents_item.type_id() == LMD_TYPE_NULL) {
        log_warn("Page %d has no Contents", page_index + 1);
        page_info->content_streams = array_pooled(pool);
        return page_info;
    }

    // Resolve Contents reference
    contents_item = pdf_resolve_reference(pdf_data, contents_item, pool);
    if (contents_item.item == ITEM_NULL || contents_item.type_id() == LMD_TYPE_NULL) {
        log_warn("Could not resolve Contents for page %d", page_index + 1);
        page_info->content_streams = array_pooled(pool);
        return page_info;
    }

    // Contents can be either a single stream or an array of streams
    page_info->content_streams = array_pooled(pool);
    if (!page_info->content_streams) return page_info;

    // Check if contents_item is a stream or array
    TypeId contents_type = contents_item.type_id();

    if (contents_type != LMD_TYPE_MAP && contents_type != LMD_TYPE_ELEMENT) {
        // Might be an array of content streams
        if (contents_type == LMD_TYPE_ARRAY || contents_type == LMD_TYPE_LIST) {
            Array* contents_array = contents_item.array;
            if (contents_array) {
                for (int i = 0; i < contents_array->length; i++) {
                    Item stream_item = contents_array->items[i];
                    stream_item = pdf_resolve_reference(pdf_data, stream_item, pool);
                    if (stream_item.item != ITEM_NULL && stream_item.type_id() != LMD_TYPE_NULL) {
                        array_append(page_info->content_streams, stream_item, pool);
                    }
                }
                log_debug("Page %d has %d content streams", page_index + 1, contents_array->length);
            }
        } else {
        }
        return page_info;
    }

    Map* contents_map = contents_item.map;
    if (!contents_map) {
        return page_info;
    }
    Item type_item = map_get_str(contents_map, "type", pool);

    if (type_item.item != ITEM_NULL && type_item.type_id() != LMD_TYPE_NULL) {
        String* type_str = type_item.get_string();
        if (type_str) {
        }
        if (type_str && strcmp(type_str->chars, "stream") == 0) {
            // Single stream
            array_append(page_info->content_streams, contents_item, pool);
            log_debug("Page %d has 1 content stream", page_index + 1);
            return page_info;
        } else {
            // Might be array - try to cast
            Array* contents_array = contents_item.array;
            if (contents_array) {
                for (int i = 0; i < contents_array->length; i++) {
                    Item stream_item = contents_array->items[i];
                    stream_item = pdf_resolve_reference(pdf_data, stream_item, pool);
                    if (stream_item.item != ITEM_NULL) {
                        array_append(page_info->content_streams, stream_item, pool);
                    }
                }
                log_debug("Page %d has %d content streams", page_index + 1, contents_array->length);
            }
        }
    }

    log_info("Successfully extracted info for page %d", page_index + 1);
    return page_info;
}
