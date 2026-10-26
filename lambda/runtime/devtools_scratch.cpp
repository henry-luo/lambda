#include "../devtools/scratch.hpp"
#include "../devtools/protocol.hpp"
#include "interp.hpp"
#include "transpiler.hpp"
#include "root_vector.h"
#include "lambda-root-frame.hpp"
#include "heap_api.h"
#include "../../lib/mem.h"
#include "../../lib/uv_loop.h"
#include "../../lib/str.h"
#include <string.h>

static constexpr int64_t SCRATCH_PAGE = 256;
static constexpr int64_t SCRATCH_TEXT = 4096;
static constexpr int64_t SCRATCH_HANDLES = 4096;

struct ScratchValue {
    uint64_t scalar_home;
};

struct ScratchInspector {
    InterpReplSession session;
    DevtoolsConnection* connection;
    RootVector values;
    ArrayList* homes;
    int64_t live_handles;
};

static const char* scratch_text(MapReader request, const char* key) {
    Item value = request.get(key).item();
    return get_type_id(value) == LMD_TYPE_STRING &&
        !memchr(value.get_chars(), 0, value.get_len()) ? value.get_chars() : nullptr;
}

static bool scratch_page(DevtoolsExchange& exchange, MapReader request,
        int64_t* offset, int64_t* limit, int64_t maximum = SCRATCH_PAGE) {
    if ((request.has("offset") && !request.get("offset").isInt()) ||
            (request.has("limit") && !request.get("limit").isInt())) {
        exchange.fail("invalid", "offset and limit must be integers");
        return false;
    }
    *offset = request.has("offset") ? request.get("offset").asInt() : 0;
    *limit = request.has("limit") ? request.get("limit").asInt() : maximum;
    if (*offset < 0 || *limit < 1 || *limit > maximum) {
        exchange.fail("invalid", "offset or limit is outside the page bounds");
        return false;
    }
    return true;
}

static int64_t scratch_retain(ScratchInspector* inspector, Item value) {
    for (int64_t i = 0; i < inspector->homes->length; i++) {
        if (inspector->homes->data[i] && root_vector_at(&inspector->values, i)->item == value.item)
            return i + 1;
    }
    if (inspector->live_handles >= SCRATCH_HANDLES || inspector->homes->length >= 65536) return 0;
    ScratchValue* home = (ScratchValue*)mem_calloc(1, sizeof(ScratchValue), MEM_CAT_TEMP);
    if (!home) return 0;
    // D5.2/D5.3.3: number homes and precise Item slots both outlive the request.
    value = lambda_item_adopt_scalar_home(value, &home->scalar_home);
    if (!root_vector_push(&inspector->values, value)) { mem_free(home); return 0; }
    if (!arraylist_append(inspector->homes, home)) {
        root_vector_pop(&inspector->values);
        mem_free(home);
        return 0;
    }
    inspector->live_handles++;
    return inspector->homes->length;
}

static int64_t scratch_count(Item value) {
    switch (get_type_id(value)) {
    case LMD_TYPE_MAP: return MapReader::fromItem(value).size();
    case LMD_TYPE_ELEMENT: {
        ElementReader element(value);
        return element.attrCount() + element.childCount();
    }
    case LMD_TYPE_ARRAY: return value.array->length;
    case LMD_TYPE_ARRAY_NUM: return value.array_num->length;
    case LMD_TYPE_RANGE: return value.range->length;
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: return value.get_len();
    case LMD_TYPE_BINARY: return binary_length(value.get_binary());
    default: return 0;
    }
}

static Item scratch_preview(DevtoolsExchange& exchange, ScratchInspector* inspector, Item value) {
    MarkBuilder builder(exchange.input);
    TypeId type = get_type_id(value);
    auto record = builder.map();
    // integer shares the decimal carrier; sized numerics also need their native subtype.
    const char* name = type == LMD_TYPE_DECIMAL && value.get_decimal()->storage_kind == DECIMAL_BIGINT
        ? "integer" : type == LMD_TYPE_NUM_SIZED
        ? get_num_sized_type_name((NumSizedType)NUM_SIZED_SUBTYPE(value.item)) : get_type_name(type);
    record.put("type", builder.createSymbolItem(name));
    if (type == LMD_TYPE_DECIMAL) {
        const mpd_t* decimal = decimal_mpd(value.get_decimal());
        // bound scalar formatting too: a computed BigInt can exceed the entry-size limit.
        if (decimal && decimal->digits > SCRATCH_TEXT - 32)
            return record.put("opaque", true).put("digits", (int64_t)decimal->digits).final();
    }
    int64_t count = scratch_count(value);
    bool expandable = type == LMD_TYPE_MAP || type == LMD_TYPE_ELEMENT ||
        type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM || type == LMD_TYPE_RANGE;
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL || type == LMD_TYPE_BINARY)
        expandable = count > SCRATCH_TEXT;
    if (expandable) {
        int64_t ref = scratch_retain(inspector, value);
        if (!ref) return exchange.fail("limit", "release retained values before inspecting more results");
        record.put("ref", ref).put("count", count);
        if (type == LMD_TYPE_ELEMENT)
            record.put("tag", ElementReader(value).tagName());
        if (type == LMD_TYPE_ARRAY_NUM) {
            auto shape = builder.array();
            ArrayNumShape* dimensions = value.array_num->is_ndim ? (ArrayNumShape*)(uintptr_t)value.array_num->extra : nullptr;
            if (dimensions) {
                for (uint8_t i = 0; i < dimensions->ndim; i++) shape.append(array_num_shape_dims(dimensions)[i]);
            } else shape.append(count);
            record.put("shape", shape.final());
        }
        return record.final();
    }
    switch (type) {
    case LMD_TYPE_NULL: case LMD_TYPE_BOOL: case LMD_TYPE_INT: case LMD_TYPE_FLOAT:
    case LMD_TYPE_DECIMAL: case LMD_TYPE_DTIME: case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY:
        return record.put("value", value).final();
    case LMD_TYPE_INT64: case LMD_TYPE_UINT64: case LMD_TYPE_NUM_SIZED: case LMD_TYPE_COMPLEX: {
        // unqualified scalar spellings stay exact text with their native type.
        StrBuf* text = strbuf_new();
        print_item(text, value);
        Item result = record.put("text", text->str).final();
        strbuf_free(text);
        return result;
    }
    default:
        return record.put("opaque", true).final();
    }
}

static Item scratch_eval(DevtoolsExchange& exchange, MapReader request, void* owner) {
    ScratchInspector* inspector = (ScratchInspector*)owner;
    const char* source = scratch_text(request, "source");
    if (!source || strlen(source) > 65536) return exchange.fail("invalid", "expected source string up to 65536 bytes");
    if (inspector->live_handles >= SCRATCH_HANDLES || inspector->homes->length >= 65536)
        return exchange.fail("limit", "release retained values before submitting another entry");
    RootFrame roots(1);
    Rooted<Item> result(roots, ItemNull);
    Item output = ItemNull;
    ReplEntryStatus status = interp_repl_session_eval(&inspector->session, source, &output);
    result.set(output);
    if (status != REPL_ENTRY_OK) return exchange.fail(
        status == REPL_ENTRY_INCOMPLETE ? "incomplete" : status == REPL_ENTRY_REJECTED ? "rejected" : "evaluation",
        "Lambda entry did not complete; see the scratch process diagnostics");
    MarkBuilder builder(exchange.input);
    return builder.map().put("result", scratch_preview(exchange, inspector, result.get()))
        .put("declarations", inspector->session.declarations_only).final();
}

struct ScratchBindings {
    DevtoolsExchange* exchange;
    ScratchInspector* inspector;
    ArrayBuilder* rows;
    int64_t offset, limit, count;
};

static void scratch_binding(void* opaque, const char* name, size_t length, Item value, bool imported) {
    ScratchBindings* page = (ScratchBindings*)opaque;
    int64_t index = page->count++;
    if (index < page->offset || index - page->offset >= page->limit || page->exchange->error_code) return;
    MarkBuilder builder(page->exchange->input);
    page->rows->append(builder.map().put("name", builder.createStringItem(name, length))
        .put("imported", imported).put("value", scratch_preview(*page->exchange, page->inspector, value)).final());
}

static Item scratch_bindings(DevtoolsExchange& exchange, MapReader request, void* owner) {
    int64_t offset, limit;
    if (!scratch_page(exchange, request, &offset, &limit)) return ItemNull;
    MarkBuilder builder(exchange.input);
    auto rows = builder.array();
    ScratchBindings page = {&exchange, (ScratchInspector*)owner, &rows, offset, limit, 0};
    interp_repl_session_each_binding(&page.inspector->session, scratch_binding, &page);
    return builder.map().put("entries", rows.final()).put("count", page.count).final();
}

static Item scratch_bytes(DevtoolsExchange& exchange, MapReader request,
        const uint8_t* data, int64_t count, bool binary, const char* key, const char* kind = nullptr) {
    int64_t offset, limit;
    if (!scratch_page(exchange, request, &offset, &limit, SCRATCH_TEXT)) return ItemNull;
    if (offset > count) offset = count;
    int64_t end = offset + (limit < count - offset ? limit : count - offset);
    MarkBuilder builder(exchange.input);
    auto result = builder.map();
    if (binary) result.put(key, Item{.item = x2it(builder.createBinary(data + offset, end - offset))});
    else {
        if (offset < count && (data[offset] & 0xc0) == 0x80)
            return exchange.fail("invalid", "offset must begin at a UTF-8 boundary");
        while (end < count && (data[end] & 0xc0) == 0x80) end--;
        if (end == offset && end < count) return exchange.fail("invalid", "limit splits a UTF-8 character");
        result.put(key, builder.createStringItem((const char*)data + offset, end - offset));
    }
    if (kind) result.put("kind", builder.createSymbolItem(kind));
    return result.put("count", count).put("next", end).final();
}

static Item scratch_inspect(DevtoolsExchange& exchange, MapReader request, void* owner) {
    ScratchInspector* inspector = (ScratchInspector*)owner;
    int64_t ref = request.get("ref").isInt() ? request.get("ref").asInt() : 0;
    if (ref < 1 || ref > inspector->homes->length || !inspector->homes->data[ref - 1])
        return exchange.fail("stale", "value handle is no longer retained");
    Item value = *root_vector_at(&inspector->values, ref - 1);
    TypeId type = get_type_id(value);
    int64_t offset, limit, count = scratch_count(value);
    bool bytes = type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL || type == LMD_TYPE_BINARY;
    if (bytes) return scratch_bytes(exchange, request, type == LMD_TYPE_BINARY ? binary_data(value.get_binary()) :
        (const uint8_t*)value.get_chars(), count, type == LMD_TYPE_BINARY, "value");
    if (!scratch_page(exchange, request, &offset, &limit)) return ItemNull;
    MarkBuilder builder(exchange.input);
    auto result = builder.map();
    result.put("count", count);
    auto entries = builder.array();
    int64_t index = 0;
    auto append = [&](const char* name, int64_t key, Item child, const char* kind) {
        if (index >= offset && index - offset < limit && !exchange.error_code)
            entries.append(builder.map().put("key", name ? builder.createStringItem(name) : builder.createInt(key)).put("kind", builder.createSymbolItem(kind))
                .put("value", scratch_preview(exchange, inspector, child)).final());
        index++;
    };
    if (type == LMD_TYPE_MAP) {
        auto map = MapReader::fromItem(value);
        auto fields = map.entries();
        const char* name; ItemReader child;
        while (index < offset + limit && fields.next(&name, &child)) append(name, 0, child.item(), "field");
    } else if (type == LMD_TYPE_ELEMENT) {
        ElementReader element(value);
        auto attributes = element.attrs();
        const char* name; ItemReader child;
        while (index < offset + limit && attributes.next(&name, &child)) append(name, 0, child.item(), "attribute");
        index = element.attrCount();
        for (int64_t i = offset > index ? offset - index : 0; i < element.childCount() && element.attrCount() + i < offset + limit; i++) {
            index = element.attrCount() + i;
            append(nullptr, i, element.childAt(i).item(), "child");
        }
    } else {
        const int64_t range_start = type == LMD_TYPE_RANGE ? value.range->start : 0;
        const bool char_range = type == LMD_TYPE_RANGE && value.range->is_char;
        for (int64_t i = offset; i < count && i - offset < limit; i++) {
            Item child = ItemNull;
            if (type == LMD_TYPE_ARRAY) child = array_item_read(value.array, i);
            else if (type == LMD_TYPE_ARRAY_NUM) child = array_num_read_borrowed_item(value.array_num,
                array_num_element_offset(value.array_num, i));
            else if (type == LMD_TYPE_RANGE) {
                if (char_range) {
                    // keep synthesized page strings Input-owned; a GC during traversal
                    // must never invalidate an earlier scalar borrowed by the reply.
                    char text[4];
                    size_t length = str_utf8_encode((uint32_t)(range_start + i), text, sizeof(text));
                    child = builder.createStringItem(text, length);
                } else child = Item{.item = i2it(range_start + i)};
            }
            index = i;
            append(nullptr, i, child, "index");
        }
    }
    return result.put("entries", entries.final()).put("next", offset >= count ? count :
        offset + (limit < count - offset ? limit : count - offset)).final();
}

static Item scratch_release(DevtoolsExchange& exchange, MapReader request, void* owner) {
    ScratchInspector* inspector = (ScratchInspector*)owner;
    bool all = request.get("all").isBool() && request.get("all").asBool();
    if (request.has("all") && (!all || request.has("refs")))
        return exchange.fail("invalid", "use either all: true or a refs array");
    ItemReader refs = request.get("refs");
    if (!all && (!refs.isArray() || refs.asArray().length() > SCRATCH_HANDLES))
        return exchange.fail("invalid", "release requires a bounded refs array");
    ArrayReader values = refs.asArray();
    for (int64_t i = 0; !all && i < values.length(); i++) {
        ItemReader ref = values.get(i);
        if (!ref.isInt() || ref.asInt() < 1 || ref.asInt() > inspector->homes->length)
            return exchange.fail("invalid", "invalid handle in refs");
    }
    int64_t length = all ? inspector->homes->length : values.length();
    for (int64_t i = 0; i < length; i++) {
        int64_t index = all ? i : values.get(i).asInt() - 1;
        if (!inspector->homes->data[index]) continue;
        *root_vector_at(&inspector->values, index) = ItemNull;
        mem_free(inspector->homes->data[index]);
        inspector->homes->data[index] = nullptr;
        inspector->live_handles--;
    }
    return ItemNull;
}

static Item scratch_gc(DevtoolsExchange& exchange, MapReader, void* owner) {
    ScratchInspector* inspector = (ScratchInspector*)owner;
    heap_gc_collect();
    MarkBuilder builder(exchange.input);
    return builder.map().put("retained", inspector->live_handles).final();
}

static Item scratch_source(DevtoolsExchange& exchange, MapReader request, void* owner) {
    const char* source = interp_repl_session_source(&((ScratchInspector*)owner)->session);
    return scratch_bytes(exchange, request, (const uint8_t*)source, strlen(source), false, "text", "scratch_session");
}

int devtools_run_scratch(int fd) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ScratchInspector inspector = {};
    inspector.homes = arraylist_new(16);
    if (!inspector.homes || !interp_repl_session_init(&inspector.session, &runtime, false)) {
        if (inspector.homes) arraylist_free(inspector.homes);
        runtime_cleanup(&runtime);
        return 1;
    }
    root_vector_init(&inspector.values, (Context*)inspector.session.runner.context, "devtools.values");
    static const DevtoolsCommand commands[] = {
        {"eval", scratch_eval}, {"bindings", scratch_bindings}, {"inspect", scratch_inspect},
        {"release", scratch_release}, {"gc", scratch_gc}, {"source", scratch_source},
    };
    inspector.connection = devtools_open(fd, commands, sizeof(commands) / sizeof(commands[0]), &inspector);
    int status = inspector.connection ? 0 : 1;
    while (devtools_connected(inspector.connection)) {
        uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
        devtools_dispatch_pending(inspector.connection);
        uv_sleep(8);
    }
    devtools_destroy(inspector.connection);
    root_vector_destroy(&inspector.values);
    for (int i = 0; i < inspector.homes->length; i++) mem_free(inspector.homes->data[i]);
    arraylist_free(inspector.homes);
    interp_repl_session_destroy(&inspector.session);
    runtime_cleanup(&runtime);
    return status;
}
