#include "input.hpp"
#include "../io/fs_node.hpp"
#include "../core/lambda-error-data.h"
#include "../../lib/memtrack.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include "../../lib/datetime.h"

thread_local const ZipLimits* input_zip_limits = nullptr;
static const char fs_backend_brand = 0;
static const char* const fs_attributes[] = {
    "name", "kind", "is_file", "is_dir", "path", "entry_path", "extension",
    "size", "modified", "mode", "is_link", "format", "compressed_size", "compression", "crc32"
};
static constexpr int FS_ATTR_COUNT = sizeof(fs_attributes) / sizeof(fs_attributes[0]);

struct FsTree;
struct FsParsed {
    const char* type;
    const char* flavor;
    Item value;
    FsParsed* next;
};
struct FsNode {
    FsTree* tree;
    uint32_t index;
    uint32_t child_start;
    Item element;
    Item view;
    Item parsed;
    Array* normalized;
    Item error;
    FsParsed* parses;
    Item attrs[FS_ATTR_COUNT];
    bool prepared;
    int64_t count;
};
struct FsTree {
    Input* input;
    ZipArchive* archive;
    FsNode* nodes;
    uint32_t* children;
    Item tag;
    Item keys[FS_ATTR_COUNT];
};

static Item fs_error(Input* input, const char* member, const char* message) {
    LambdaError* error = (LambdaError*)arena_calloc(input->arena, sizeof(LambdaError));
    if (!error) return ItemError;
    error->type_id = LMD_TYPE_ERROR;
    error->code = ERR_PARSE_ERROR;
    // Pool errors have no owned runtime payload; Input releases their storage.
    error->is_static = true;
    StrBuf* text = strbuf_new();
    if (!text) return ItemError;
    const char* source = input->url && ((Url*)input->url)->href ? ((Url*)input->url)->href->chars : "<memory>";
    strbuf_append_format(text, "%s%s%s: %s", source, member && *member ? "!" : "",
        member ? member : "", message ? message : "ZIP operation failed");
    error->message = (char*)arena_alloc(input->arena, text->length + 1);
    if (error->message) memcpy(error->message, text->str, text->length + 1);
    strbuf_free(text);
    return err2it(error);
}

static FsNode* fs_node_data(Item node) {
    return virtual_host_type(node) == &fs_backend_brand
        ? (FsNode*)virtual_host_data(node) : nullptr;
}
bool fs_node_is(Item item) { return fs_node_data(item) != nullptr; }
ZipArchive* fs_node_archive(Item node) {
    FsNode* fs = fs_node_data(node);
    return fs ? fs->tree->archive : nullptr;
}

static Item fs_parse_uncached(FsNode* node, String* type, String* flavor) {
    FsTree* tree = node->tree;
    ZipEntry* entry = zip_archive_entry(tree->archive, node->index);
    if (entry->directory || node->index == 0) {
        return fs_error(tree->input, entry->path, "input() requires a member file");
    }
    ByteSpan bytes = {};
    ZipError error = {};
    if (!zip_entry_bytes(tree->archive, node->index, &bytes, &error)) {
        return fs_error(tree->input, entry->path, error.message);
    }
    const char* data = (const char*)byte_span_data(&bytes);
    size_t size = bytes.length;
    MarkBuilder builder(tree->input);
    bool automatic = !type || strcmp(type->chars, "auto") == 0;
    if (type && strcmp(type->chars, "zip") == 0) {
        Url* source_url = (Url*)tree->input->url;
        Input* nested = InputManager::create_input(source_url && source_url->href ? url_parse(source_url->href->chars) : nullptr);
        if (!nested) return ItemError;
        // Nested archives share the parent's expansion ledger and nesting depth.
        input_zip(nested, data, size, tree->archive->budget, tree->archive->depth + 1);
        return nested->parse_failed ? fs_error(tree->input, entry->path,
            nested->parse_error_message) : nested->root;
    }
    if ((type && strcmp(type->chars, "binary") == 0) ||
            (automatic && (zip_source_expected(entry->path, data, size) ||
                memchr(data, 0, size) || !str_utf8_valid(data, size)))) {
        if (!size) return ItemNull;
        Binary* binary = builder.createBinary(data, size);
        return binary ? (Item){.item = x2it(binary)} : ItemError;
    }
    if (type && (strcmp(type->chars, "text") == 0 || strcmp(type->chars, "txt") == 0)) {
        return {.item = s2it(builder.createString(data, size))};
    }
    // Give MIME detection the archive-local name, while retaining the outer Path.
    Url* member_url = url_parse(entry->path);
    Input* parsed = input_from_source_n(data, size, member_url, type, flavor);
    if (!parsed) { if (member_url) url_destroy(member_url); return ItemError; }
    if (parsed->parse_failed) return fs_error(tree->input, entry->path,
        parsed->parse_error_message ? parsed->parse_error_message : "member parsing failed");
    return parsed->root;
}

static Item fs_parse(FsNode* node, String* type, String* flavor) {
    const char* type_name = type && strcmp(type->chars, "auto") ? type->chars : "auto";
    const char* flavor_name = flavor ? flavor->chars : "";
    for (FsParsed* parsed = node->parses; parsed; parsed = parsed->next) {
        if (!strcmp(parsed->type, type_name) && !strcmp(parsed->flavor, flavor_name)) return parsed->value;
    }
    Input* input = node->tree->input;
    FsParsed* parsed = (FsParsed*)arena_calloc(input->arena, sizeof(FsParsed));
    if (!parsed) return ItemError;
    parsed->type = (const char*)arena_alloc(input->arena, strlen(type_name) + 1);
    parsed->flavor = (const char*)arena_alloc(input->arena, strlen(flavor_name) + 1);
    if (!parsed->type || !parsed->flavor) return ItemError;
    strcpy((char*)parsed->type, type_name); strcpy((char*)parsed->flavor, flavor_name);
    parsed->value = fs_parse_uncached(node, type, flavor);
    parsed->next = node->parses; node->parses = parsed;
    return parsed->value;
}

Item fs_node_input(Item node, String* type, String* flavor) {
    FsNode* fs = fs_node_data(node);
    return fs ? fs_parse(fs, type, flavor) : ItemError;
}

static VirtualOpStatus fs_prepare(void* data, Item* error) {
    FsNode* node = (FsNode*)data;
    ZipEntry* entry = zip_archive_entry(node->tree->archive, node->index);
    if (node->index == 0 || entry->directory) return VIRTUAL_OP_OK;
    if (!node->prepared) {
        node->prepared = true;
        node->parsed = fs_parse(node, nullptr, nullptr);
        if (get_type_id(node->parsed) == LMD_TYPE_ERROR) node->error = node->parsed;
        else {
            // Reuse the ordinary element-content normalizer (S2.6.2–S2.6.4).
            Input* input = node->tree->input;
            node->normalized = array_pooled(input->pool);
            if (!node->normalized) node->error = ItemError;
            else if (entry->size) {
                list_push_with_owner((List*)node->normalized, node->parsed, input->pool, input->arena, nullptr);
                node->count = node->normalized->length;
            }
        }
    }
    if (node->error.item) { *error = node->error; return VIRTUAL_OP_ERROR; }
    return VIRTUAL_OP_OK;
}
static int64_t fs_count(void* data) { return ((FsNode*)data)->count; }
static Item fs_wrap(FsNode* node);
static VirtualOpStatus fs_get(void* data, int64_t index, Item* out) {
    FsNode* node = (FsNode*)data;
    if (fs_prepare(data, out) == VIRTUAL_OP_ERROR) return VIRTUAL_OP_ERROR;
    if (index < 0 || index >= node->count) return VIRTUAL_OP_MISSING;
    ZipEntry* entry = zip_archive_entry(node->tree->archive, node->index);
    if (node->index == 0 || entry->directory) {
        *out = fs_wrap(&node->tree->nodes[node->tree->children[node->child_start + index]]);
        if (get_type_id(*out) == LMD_TYPE_ERROR) return VIRTUAL_OP_ERROR;
    } else *out = node->normalized->items[index];
    return VIRTUAL_OP_OK;
}
static VirtualOpStatus fs_tag(void* data, Item* out) {
    *out = ((FsNode*)data)->tree->tag; return VIRTUAL_OP_OK;
}
static int64_t fs_attr_count(void*) { return FS_ATTR_COUNT; }
static VirtualOpStatus fs_attr_get(void* data, Item key, Item* out) {
    if (!is_text_type_id(get_type_id(key))) return VIRTUAL_OP_MISSING;
    for (int i = 0; i < FS_ATTR_COUNT; i++) {
        if (key.get_len() == strlen(fs_attributes[i]) &&
                !memcmp(key.get_chars(), fs_attributes[i], key.get_len())) {
            *out = ((FsNode*)data)->attrs[i]; return VIRTUAL_OP_OK;
        }
    }
    return VIRTUAL_OP_MISSING;
}
static VirtualOpStatus fs_attr_key(void* data, int64_t index, Item* out) {
    if (index < 0 || index >= FS_ATTR_COUNT) return VIRTUAL_OP_MISSING;
    *out = ((FsNode*)data)->tree->keys[index]; return VIRTUAL_OP_OK;
}
static VirtualOpStatus fs_attr_value(void* data, int64_t index, Item* out) {
    if (index < 0 || index >= FS_ATTR_COUNT) return VIRTUAL_OP_MISSING;
    *out = ((FsNode*)data)->attrs[index]; return VIRTUAL_OP_OK;
}
static const VArrayVtable fs_children_vtable = {
    {LAMBDA_VIRTUAL_ABI_VERSION, LMD_TYPE_VARRAY, {}, nullptr, nullptr, nullptr},
    {fs_count, fs_get, nullptr, nullptr, fs_prepare}
};
static const VelmtVtable fs_element_vtable = {
    {LAMBDA_VIRTUAL_ABI_VERSION, LMD_TYPE_VELMT, {}, nullptr, nullptr, nullptr},
    {fs_tag, nullptr, {fs_attr_get, nullptr, nullptr, nullptr, fs_attr_count, fs_attr_key, fs_attr_value},
        {fs_count, fs_get, nullptr, nullptr, fs_prepare}}
};
static Item fs_wrap(FsNode* node) {
    if (node->element.item) return node->element;
    FsTree* tree = node->tree;
    Input* input = tree->input;
    ZipArchive* archive = tree->archive;
    uint32_t i = node->index;
    ZipEntry* entry = zip_archive_entry(archive, i);
    MarkBuilder builder(input);
    const char* pathname = input->url && ((Url*)input->url)->pathname ? ((Url*)input->url)->pathname->chars : "";
    Path* path = (Path*)input->path;
    Velmt* element = (Velmt*)arena_calloc(input->arena, sizeof(Velmt));
    VArray* view = (VArray*)arena_calloc(input->arena, sizeof(VArray));
    if (!element || !view) return fs_error(input, entry->path, "ZIP filesystem allocation failed");
    element->type_id = LMD_TYPE_VELMT; element->data = node; element->vtable = &fs_element_vtable;
    element->host_type = &fs_backend_brand; element->host_data = node;
    view->type_id = LMD_TYPE_VARRAY; view->data = node; view->vtable = &fs_children_vtable;
    node->element = {.velmt = element}; node->view = {.varray = view};
    const char* name = strrchr(i ? entry->path : pathname, '/');
    name = name ? name + 1 : i ? entry->path : pathname;
    bool directory = i && entry->directory;
    const char* extension = directory ? nullptr : strrchr(name, '.');
    node->attrs[0] = {.item = s2it(builder.createString(name))};
    node->attrs[1] = builder.createSymbolItem(directory ? "dir" : "file");
    node->attrs[2] = builder.createBool(!directory); node->attrs[3] = builder.createBool(directory);
    node->attrs[4] = {.path = path}; node->attrs[5] = {.item = s2it(builder.createString(entry->path))};
    node->attrs[6] = extension ? (Item){.item = s2it(builder.createString(extension + 1))} : ItemNull;
    node->attrs[7] = directory ? ItemNull : builder.createUInt64(i ? entry->size : archive->snapshot->capacity);
    node->attrs[8] = ItemNull;
    if (i && entry->dos_time) {
        uint32_t date = entry->dos_time >> 16, time = entry->dos_time & 0xffff;
        char stamp[32];
        snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u",
            1980 + (date >> 9), (date >> 5) & 15, date & 31,
            time >> 11, (time >> 5) & 63, (time & 31) * 2);
        DateTime* modified = datetime_parse_iso8601(input->pool, stamp);
        if (modified) node->attrs[8] = builder.createDateTime(*modified);
    }
    node->attrs[9] = i && entry->mode ? builder.createInt(entry->mode & 07777) : ItemNull;
    node->attrs[10] = builder.createBool(false);
    node->attrs[11] = i ? ItemNull : builder.createSymbolItem("zip");
    node->attrs[12] = i && !directory ? builder.createUInt64(entry->compressed_size) : ItemNull;
    node->attrs[13] = i && !directory ? builder.createSymbolItem(entry->method ? "deflate" : "stored") : ItemNull;
    node->attrs[14] = i && !directory ? builder.createUInt64(entry->crc) : ItemNull;
    return node->element;
}

Item fs_node_content(Item node) {
    FsNode* fs = fs_node_data(node);
    Item error = ItemNull;
    if (!fs) return ItemError;
    return fs_prepare(fs, &error) == VIRTUAL_OP_ERROR ? error : fs->view;
}
static void fs_tree_release(void* data) { zip_archive_release((ZipArchive*)data); }

static void fs_input_failure(Input* input, const char* message) {
    input->parse_failed = true;
    input->root = fs_error(input, "", message);
    LambdaError* error = it2err(input->root);
    input->parse_error_message = error ? error->message : nullptr;
}

void input_zip(Input* input, const void* bytes, size_t length,
        ZipBudget* parent_budget, uint32_t depth) {
    ZipError error = {};
    ZipArchive* archive = zip_archive_open(bytes, length, input_zip_limits, parent_budget, depth, &error);
    if (!archive) {
        fs_input_failure(input, error.message);
        return;
    }
    uint64_t metadata_bytes = sizeof(FsTree);
    for (int i = 0; i < archive->entries->length; i++) {
        // Include cached attribute scalars/names, not only virtual headers.
        metadata_bytes += sizeof(FsNode) + sizeof(Velmt) + sizeof(VArray) + sizeof(Item) + 256 +
            3 * strlen(zip_archive_entry(archive, i)->path);
    }
    if (metadata_bytes > archive->budget->limits.index_bytes - archive->index_bytes) {
        zip_archive_release(archive);
        fs_input_failure(input, "ZIP filesystem index limit exceeded");
        return;
    }
    archive->index_bytes += metadata_bytes;
    // D4.1.3: the Input manager owns every node and cached parse until teardown.
    // No cached Item points into a runtime heap; dropping the root cannot free members.
    pool_add_cleanup(input->pool, fs_tree_release, archive);
    FsTree* tree = (FsTree*)arena_calloc(input->arena, sizeof(FsTree));
    if (!tree) { fs_input_failure(input, "ZIP filesystem allocation failed"); return; }
    tree->input = input; tree->archive = archive;
    tree->nodes = (FsNode*)arena_calloc(input->arena, sizeof(FsNode) * archive->entries->length);
    tree->children = (uint32_t*)arena_calloc(input->arena, sizeof(uint32_t) * archive->entries->length);
    if (!tree->nodes || !tree->children) { fs_input_failure(input, "ZIP filesystem allocation failed"); return; }
    MarkBuilder builder(input);
    tree->tag = builder.createSymbolItem("fs");
    for (int i = 0; i < FS_ATTR_COUNT; i++) tree->keys[i] = builder.createSymbolItem(fs_attributes[i]);
    const char* pathname = input->url && ((Url*)input->url)->pathname ? ((Url*)input->url)->pathname->chars : "";
    Path* path = path_new(input->pool, input->url && ((Url*)input->url)->scheme == URL_SCHEME_HTTPS
        ? PATH_SCHEME_HTTPS : input->url && ((Url*)input->url)->scheme == URL_SCHEME_HTTP ? PATH_SCHEME_HTTP : PATH_SCHEME_FILE);
    if (!path) { fs_input_failure(input, "ZIP source path allocation failed"); return; }
    Url* source_url = (Url*)input->url;
    if (source_url && source_url->host && source_url->host->len) {
        path = path_new_authority(input->pool, path->root_scheme, source_url->host->chars);
        if (!path) { fs_input_failure(input, "ZIP source path allocation failed"); return; }
    }
    const char* start = pathname;
    for (const char* at = pathname; ; at++) {
        if (*at && *at != '/') continue;
        if (at != start) path = path_extend_len(input->pool, path, start, at - start);
        if (!path) { fs_input_failure(input, "ZIP source path allocation failed"); return; }
        if (!*at) break;
        start = at + 1;
    }
    input->path = path;
    for (int i = 0; i < archive->entries->length; i++) {
        FsNode* node = &tree->nodes[i];
        ZipEntry* entry = zip_archive_entry(archive, i);
        node->tree = tree; node->index = i;
        node->count = entry->child_count;

    }
    // Prefix spans make indexed access and a full directory walk O(1)/O(n).
    uint32_t next_child = 0;
    for (int i = 0; i < archive->entries->length; i++) {
        tree->nodes[i].child_start = next_child;
        ZipEntry* entry = zip_archive_entry(archive, i);
        for (uint32_t child = entry->first_child; child != UINT32_MAX;
                child = zip_archive_entry(archive, child)->next_sibling) {
            tree->children[next_child++] = child;
        }
    }
    input->root = fs_wrap(&tree->nodes[0]);
    if (get_type_id(input->root) == LMD_TYPE_ERROR) fs_input_failure(input, "ZIP filesystem allocation failed");
}

#include "../format/format.h"

static bool fs_output_error(ZipError* error, const char* message) {
    if (error) snprintf(error->message, sizeof(error->message), "%s", message);
    return false;
}
static Item fs_materialized_attr(Item node, const char* name) {
    return get_type_id(node) == LMD_TYPE_ELEMENT
        ? (Item){.item = node.element->get_attr(name).item} : ItemNull;
}
struct FsOutputBudget { uint64_t expanded; uint64_t index; };
static ZipOutputEntry* fs_output_entry(Pool* pool, const StrBuf* path,
        FsOutputBudget* budget, const ZipLimits& limits, ZipError* error) {
    uint64_t charge = sizeof(ZipOutputEntry) + sizeof(void*) + path->length + 32;
    if (charge > limits.index_bytes - budget->index) {
        fs_output_error(error, "ZIP output index limit exceeded"); return nullptr;
    }
    budget->index += charge;
    ZipOutputEntry* entry = (ZipOutputEntry*)pool_calloc(pool, sizeof(ZipOutputEntry));
    if (!entry) { fs_output_error(error, "ZIP output allocation failed"); return nullptr; }
    entry->path = (const char*)pool_alloc(pool, path->length + 1);
    if (!entry->path) { fs_output_error(error, "ZIP output allocation failed"); return nullptr; }
    memcpy((void*)entry->path, path->str, path->length + 1);
    return entry;
}

static bool fs_output_walk(Item node, const char* parent, bool root,
        uint32_t depth, ArrayList* entries, Pool* pool, const ZipLimits& limits,
        FsOutputBudget* budget, ZipError* error) {
    if (depth > limits.path_depth || (uint64_t)entries->length > limits.entries) {
        return fs_output_error(error, "ZIP output tree exceeds depth/entry limits");
    }
    FsNode* fs = fs_node_data(node);
    bool materialized = get_type_id(node) == LMD_TYPE_ELEMENT;
    if (!fs && !materialized) return fs_output_error(error, "ZIP output requires fs elements");
    if (materialized) {
        TypeElmt* type = (TypeElmt*)node.element->type;
        if (!type || type->name.length != 2 || memcmp(type->name.str, "fs", 2)) {
            return fs_output_error(error, "ZIP output requires the fs tag");
        }
    }
    Item name = fs ? fs->attrs[0] : fs_materialized_attr(node, "name");
    Item kind = fs ? fs->attrs[1] : fs_materialized_attr(node, "kind");
    bool directory = is_text_type_id(get_type_id(kind)) && !strcmp(kind.get_chars(), "dir");
    Item format = materialized ? fs_materialized_attr(node, "format") : ItemNull;
    bool archive_root = fs ? fs->index == 0 : root && is_text_type_id(get_type_id(format)) && !strcmp(format.get_chars(), "zip");
    if (!directory && !archive_root && (!is_text_type_id(get_type_id(kind)) || strcmp(kind.get_chars(), "file"))) {
        return fs_output_error(error, "ZIP fs kind must be file or dir");
    }
    StrBuf* path = strbuf_new();
    if (!path) return fs_output_error(error, "ZIP output path allocation failed");
    bool ok = true;
    if (!(root && (directory || archive_root))) {
        if (!is_text_type_id(get_type_id(name)) || !name.get_len() ||
                memchr(name.get_chars(), 0, name.get_len()) ||
                memchr(name.get_chars(), '/', name.get_len())) {
            strbuf_free(path);
            return fs_output_error(error, "ZIP fs name must be one nonempty path component");
        }
        if (*parent) { strbuf_append_str(path, parent); strbuf_append_char(path, '/'); }
        strbuf_append_str_n(path, name.get_chars(), name.get_len());
    }
    if (path->length > limits.name_bytes) {
        strbuf_free(path); return fs_output_error(error, "ZIP output path length limit exceeded");
    }
    if (directory || archive_root) {
        if (!root) {
            ZipOutputEntry* entry = fs_output_entry(pool, path, budget, limits, error);
            if (!entry) { strbuf_free(path); return false; }
            entry->directory = true;
            if (fs) {
                ZipEntry* original = zip_archive_entry(fs->tree->archive, fs->index);
                entry->mode = original->mode; entry->dos_time = original->dos_time;
            }
            ok = arraylist_append(entries, entry);
        }
        int64_t count = fs ? fs->count : node.element->length;
        for (int64_t i = 0; ok && i < count; i++) {
            Item child = ItemNull;
            if (fs) fs_get(fs, i, &child);
            else child = node.element->items[i];
            ok = fs_output_walk(child, path->str ? path->str : "", false, depth + 1,
                entries, pool, limits, budget, error);
        }
    } else {
        ZipOutputEntry* entry = fs_output_entry(pool, path, budget, limits, error);
        if (!entry) { strbuf_free(path); return false; }
        uint64_t available = limits.expanded_bytes - budget->expanded;
        if (available > limits.member_bytes) available = limits.member_bytes;
        if (fs) {
            ByteSpan bytes = {};
            if (zip_archive_entry(fs->tree->archive, fs->index)->size > available) {
                strbuf_free(path); return fs_output_error(error, "ZIP output expansion limit exceeded");
            }
            ok = zip_entry_bytes(fs->tree->archive, fs->index, &bytes, error);
            entry->bytes = byte_span_data(&bytes); entry->length = bytes.length;
            ZipEntry* original = zip_archive_entry(fs->tree->archive, fs->index);
            entry->mode = original->mode; entry->dos_time = original->dos_time;
        } else {
            Item format = fs_materialized_attr(node, "format");
            StrBuf* payload = strbuf_new();
            if (!payload) { strbuf_free(path); return fs_output_error(error, "ZIP payload allocation failed"); }
            if (get_type_id(format) != LMD_TYPE_NULL && is_text_type_id(get_type_id(format))) {
                String* fmt = (String*)pool_calloc(pool, sizeof(String) + format.get_len() + 1);
                if (!fmt) { strbuf_free(payload); strbuf_free(path); return false; }
                fmt->len = format.get_len(); fmt->is_ascii = 1;
                memcpy(fmt->chars, format.get_chars(), format.get_len());
                Item value = node.element->length == 1 ? node.element->items[0] : ItemNull;
                String* encoded = node.element->length <= 1 ? format_data(value, fmt, nullptr, pool) : nullptr;
                ok = encoded && get_type_id(value) != LMD_TYPE_ERROR;
                if (ok) ok = zip_append_bytes(payload, encoded->chars, encoded->len, available, error);
            } else if (get_type_id(format) != LMD_TYPE_NULL) ok = false;
            else for (int64_t i = 0; ok && i < node.element->length; i++) {
                Item value = node.element->items[i];
                if (is_text_type_id(get_type_id(value))) ok = zip_append_bytes(payload, value.get_chars(), value.get_len(), available, error);
                else if (get_type_id(value) == LMD_TYPE_BINARY) {
                    Binary* binary = value.get_safe_binary();
                    ok = zip_append_bytes(payload, binary_data(binary), binary_length(binary), available, error);
                } else ok = false;
            }
            if (ok) {
                entry->length = payload->length;
                entry->bytes = (const uint8_t*)pool_alloc(pool, payload->length ? payload->length : 1);
                if (!entry->bytes) ok = false;
                else if (payload->length) memcpy((void*)entry->bytes, payload->str, payload->length);
            }
            strbuf_free(payload);
            if (!ok) fs_output_error(error, "ZIP file payload requires text/binary or one value with an explicit format");
            Item mode = fs_materialized_attr(node, "mode");
            if (get_type_id(mode) == LMD_TYPE_INT) entry->mode = 0100000 | ((uint32_t)mode.int_val & 07777);
        }
        if (ok) { budget->expanded += entry->length; ok = arraylist_append(entries, entry); }
    }
    strbuf_free(path);
    return ok;
}

bool fs_zip_encode(Item tree, const ZipWriteOptions* requested,
        StrBuf* bytes, ZipError* error) {
    ZipWriteOptions options = requested ? *requested : zip_default_write_options();
    Pool* pool = pool_create();
    ArrayList* entries = arraylist_new(16);
    bool ok = pool && entries;
    FsOutputBudget budget = {};
    if (ok && get_type_id(tree) == LMD_TYPE_VARRAY) {
        Item prepare_error = virtual_content_error(tree);
        ok = get_type_id(prepare_error) != LMD_TYPE_ERROR;
        for (int64_t i = 0; ok && i < varray_count(tree.varray); i++) {
            ok = fs_output_walk(varray_get(tree.varray, i), "", false, 0, entries, pool, options.limits, &budget, error);
        }
    } else if (ok) ok = fs_output_walk(tree, "", true, 0, entries, pool, options.limits, &budget, error);
    if (ok) ok = zip_write_entries(entries, &options, bytes, error);
    if (entries) arraylist_free(entries);
    if (pool) pool_destroy(pool);
    return ok;
}

bool fs_zip_options(Item options, ZipLimits* limits, ZipWriteOptions* write, ZipError* error) {
    if (get_type_id(options) != LMD_TYPE_MAP) return true;
    Map* map = options.map;
    struct LimitOption { const char* name; uint64_t* large; uint32_t* small; };
    LimitOption fields[] = {
        {"max_archive_bytes", &limits->archive_bytes, nullptr},
        {"max_index_bytes", &limits->index_bytes, nullptr},
        {"max_member_bytes", &limits->member_bytes, nullptr},
        {"max_expanded_bytes", &limits->expanded_bytes, nullptr},
        {"max_entries", nullptr, &limits->entries},
        {"max_name_bytes", nullptr, &limits->name_bytes},
        {"max_path_depth", nullptr, &limits->path_depth},
        {"max_nesting_depth", nullptr, &limits->nesting_depth}
    };
    for (LimitOption& field : fields) {
        bool found = false;
        Item value = {.item = map->get(field.name).item};
        found = map->has_field(field.name);
        if (!found) continue;
        if (!is_integer_type_id(get_type_id(value)) || it2l(value) < 0) {
            return fs_output_error(error, "ZIP limits must be nonnegative integers");
        }
        uint64_t count = (uint64_t)it2l(value);
        if (field.small && count > UINT32_MAX) return fs_output_error(error, "ZIP count limit exceeds uint32");
        if (field.small) *field.small = (uint32_t)count;
        else *field.large = count;
    }
    if (!write) return true;
    write->limits = *limits;
    bool found = false;
    Item compression = {.item = map->get("compression").item};
    found = map->has_field("compression");
    if (found) {
        if (!is_text_type_id(get_type_id(compression))) return fs_output_error(error, "ZIP compression must be stored or deflate");
        if (!strcmp(compression.get_chars(), "stored")) write->method = 0;
        else if (!strcmp(compression.get_chars(), "deflate")) write->method = 8;
        else return fs_output_error(error, "ZIP compression must be stored or deflate");
    }
    Item level = {.item = map->get("compression_level").item};
    found = map->has_field("compression_level");
    if (found) {
        if (!is_integer_type_id(get_type_id(level)) || it2l(level) < -1 || it2l(level) > 9)
            return fs_output_error(error, "ZIP compression_level must be -1 through 9");
        write->compression_level = (int)it2l(level);
    }
    const char* bool_names[] = {"zip64", "deterministic"};
    bool* bool_fields[] = {&write->force_zip64, &write->deterministic};
    for (int i = 0; i < 2; i++) {
        Item value = {.item = map->get(bool_names[i]).item};
    found = map->has_field(bool_names[i]);
        if (!found) continue;
        if (get_type_id(value) != LMD_TYPE_BOOL) return fs_output_error(error, "ZIP zip64/deterministic options must be bool");
        *bool_fields[i] = value.bool_val;
    }
    return true;
}
