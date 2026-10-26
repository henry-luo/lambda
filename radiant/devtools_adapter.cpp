#include "devtools_adapter.hpp"
#include "../lambda/devtools/protocol.hpp"
#include "../lambda/dom/dom_cssom.h"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lib/mem.h"
#include "../lib/file.h"
#include "render.hpp"
#include "event.hpp"
#include "radiant.hpp"

struct DevtoolsDocument : DomDocumentResourceData {
    RadiantDevtools* inspector;
};

struct RadiantDevtools {
    UiContext* ui;
    DevtoolsConnection* connection;
    DomDocument* document;
    DevtoolsDocument* lease;
    ArrayList* handles;
    uint64_t mutation_epoch;
    uint64_t style_epoch;
    uint32_t layout_generation;
    int64_t revision;
    char* source_path;
    char* source;
};

static void inspector_clear_handles(RadiantDevtools* inspector) {
    for (int i = 0; i < inspector->handles->length; i++)
        if (inspector->handles->data[i]) ((DomNodeRef*)inspector->handles->data[i])->expected_id = 0;
}

static void inspector_document_destroyed(DomDocumentResourceData* data) {
    DevtoolsDocument* lease = (DevtoolsDocument*)data;
    if (lease->inspector) {
        lease->inspector->document = nullptr;
        lease->inspector->lease = nullptr;
        inspector_clear_handles(lease->inspector);
    }
    mem_free(lease);
}

static void inspector_sync(RadiantDevtools* inspector) {
    DomDocument* doc = inspector->ui->document;
    bool changed = inspector->document != doc;
    if (changed) {
        if (inspector->lease) inspector->lease->inspector = nullptr;
        inspector->lease = nullptr;
        inspector->document = doc;
        inspector_clear_handles(inspector);
        if (doc) {
            DevtoolsDocument* lease = (DevtoolsDocument*)mem_calloc(1, sizeof(DevtoolsDocument), MEM_CAT_TEMP);
            if (lease && dom_document_add_resource(doc, lease, inspector_document_destroyed)) {
                lease->inspector = inspector;
                inspector->lease = lease;
            } else if (lease) mem_free(lease);
        }
    }
    uint64_t mutation = doc ? doc->mutation_epoch : 0;
    uint64_t style = doc ? doc->style_query_epoch : 0;
    uint32_t layout = doc && doc->view_tree ? doc->view_tree->layout_generation : 0;
    changed = changed || mutation != inspector->mutation_epoch ||
        style != inspector->style_epoch || layout != inspector->layout_generation;
    inspector->mutation_epoch = mutation;
    inspector->style_epoch = style;
    inspector->layout_generation = layout;
    if (!changed) return;
    inspector->revision++;
    if (devtools_ready(inspector->connection)) {
        DevtoolsExchange exchange;
        if (!exchange.input) return;
        MarkBuilder builder(exchange.input);
        devtools_send(inspector->connection, builder.map()
            .put("event", builder.createSymbolItem("changed")).put("rev", inspector->revision).final());
    }
}

static int64_t inspector_handle(RadiantDevtools* inspector, DomNode* node) {
    if (!node) return 0;
    DomNodeRef identity = dom_node_ref(node);
    for (int i = 0; i < inspector->handles->length; i++) {
        DomNodeRef* ref = (DomNodeRef*)inspector->handles->data[i];
        if (ref && ref->address == identity.address && ref->expected_id == identity.expected_id) return i + 1;
    }
    if (inspector->handles->length >= 65536) return 0;
    DomNodeRef* ref = (DomNodeRef*)mem_alloc(sizeof(DomNodeRef), MEM_CAT_TEMP);
    if (!ref) return 0;
    *ref = identity;
    if (!arraylist_append(inspector->handles, ref)) { mem_free(ref); return 0; }
    return inspector->handles->length;
}

static DomNode* inspector_node(RadiantDevtools* inspector, MapReader request, DevtoolsExchange& exchange) {
    if (!inspector->document || !inspector->document->root) {
        exchange.fail("unavailable", "target has no DOM document");
        return nullptr;
    }
    if (!request.has("node")) return inspector->document->root;
    ItemReader id = request.get("node");
    int64_t handle = id.isInt() ? id.asInt() : 0;
    if (handle > 0 && handle <= inspector->handles->length && inspector->handles->data[handle - 1]) {
        DomNode* node = dom_node_ref_validate(inspector->document,
            *(DomNodeRef*)inspector->handles->data[handle - 1]);
        // registry membership alone also admits detached nodes; only inspect the live tree.
        DomElement* element = node && node->is_element() ? node->as_element() :
            node && node->parent ? node->parent->as_element() : nullptr;
        if (element && dom_element_is_connected(element)) return node;
    }
    exchange.fail("stale", "node handle is no longer live");
    return nullptr;
}

static Item inspector_box(MarkBuilder& builder, DomNode* node) {
    float x = 0, y = 0, width = 0, height = 0;
    if (node && node->view_type != RDT_VIEW_NONE)
        view_get_visual_bounds(node, &x, &y, &width, &height);
    return builder.map().put("x", (double)x).put("y", (double)y)
        .put("width", (double)width).put("height", (double)height).final();
}

static Item inspector_node_record(MarkBuilder& builder, RadiantDevtools* inspector, DomNode* node) {
    int64_t handle = inspector_handle(inspector, node);
    if (!handle) return ItemNull;
    Item value = ItemNull;
    int64_t count = 0;
    if (node->is_element()) {
        DomElement* element = node->as_element();
        auto snapshot = builder.element(element->tag_name ? element->tag_name : "element");
        DomAttributeIterator attributes = dom_element_attribute_iterator(element);
        DomAttributeView attribute;
        while (dom_element_next_attribute(&attributes, &attribute))
            snapshot.attr(attribute.qualified_name, attribute.value ? attribute.value : "");
        value = snapshot.final();
        for (DomNode* child = element->first_child; child; child = child->next_sibling) count++;
    } else if (node->is_text()) {
        const char* text = dom_text_get_content(node->as_text());
        value = builder.createStringItem(text ? text : "");
    }
    return builder.map().put("node", handle)
        .put("name", node->node_name()).put("value", value).put("count", count)
        .put("line", node->source_line).put("box", inspector_box(builder, node)).final();
}

static Item inspector_tree(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    DomNode* node = inspector_node(inspector, request, exchange);
    if (!node) return ItemNull;
    if ((request.has("offset") && !request.get("offset").isInt()) ||
            (request.has("limit") && !request.get("limit").isInt()))
        return exchange.fail("invalid", "offset and limit must be integers");
    int64_t offset = request.has("offset") ? request.get("offset").asInt() : 0;
    int64_t limit = request.has("limit") ? request.get("limit").asInt() : 100;
    if (offset < 0 || limit < 1 || limit > 500) return exchange.fail("invalid", "expected offset >= 0 and limit 1..500");
    MarkBuilder builder(exchange.input);
    auto children = builder.array();
    int64_t index = 0;
    for (DomNode* child = node->is_element() ? node->as_element()->first_child.get() : nullptr;
            child; child = child->next_sibling) {
        if (index >= offset && index < offset + limit) {
            Item row = inspector_node_record(builder, inspector, child);
            if (row.item == ITEM_NULL) return exchange.fail("limit", "inspector handle limit reached");
            children.append(row);
        }
        index++;
    }
    exchange.revision = inspector->revision;
    Item row = inspector_node_record(builder, inspector, node);
    if (row.item == ITEM_NULL) return exchange.fail("limit", "inspector handle limit reached");
    return builder.map().put("node", row)
        .put("children", children.final()).put("count", index).final();
}

static Item inspector_style(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    DomNode* node = inspector_node(inspector, request, exchange);
    if (!node) return ItemNull;
    if (!node->is_element()) return exchange.fail("invalid", "style requires an element");
    MarkBuilder builder(exchange.input);
    auto computed = builder.map();
    size_t count = 0;
    const CssPropAccessor* accessors = css_prop_accessors(&count);
    for (size_t i = 0; i < count; i++) {
        const char* name = css_property_spelling_from_code(accessors[i].id);
        String* value = name ? css_prop_serialize_computed_value(exchange.pool,
            node->as_element(), accessors[i].id, 0) : nullptr;
        if (value) computed.put(name, builder.createStringItem(value->chars, value->len));
    }
    const char* authored = dom_element_get_inline_style(node->as_element());
    exchange.revision = inspector->revision;
    return builder.map().put("computed", computed.final()).put("inline", authored ? authored : "")
        .put("box", inspector_box(builder, node)).final();
}

static const char* inspector_text(MapReader request, const char* key) {
    Item value = request.get(key).item();
    return get_type_id(value) == LMD_TYPE_STRING &&
        !memchr(value.get_chars(), 0, value.get_len()) ? value.get_chars() : nullptr;
}

static Item inspector_set_style(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    DomNode* node = inspector_node(inspector, request, exchange);
    if (!node) return ItemNull;
    if (!request.get("rev").isInt() || request.get("rev").asInt() != inspector->revision)
        return exchange.fail("conflict", "refresh the document before applying this edit");
    const char* name = inspector_text(request, "name");
    const char* value = inspector_text(request, "value");
    const char* priority = request.has("priority") ? inspector_text(request, "priority") : "";
    if (!node->is_element() || !name || !*name || !value || !priority)
        return exchange.fail("invalid", "expected an element, name, value and optional priority strings");
    MarkBuilder builder(exchange.input);
    Item previous = builder.map().put("name", name)
        .put("value", dom_cssom_inline_property(node->as_element(), name, exchange.pool, false))
        .put("priority", dom_cssom_inline_property(node->as_element(), name, exchange.pool, true)).final();
    if (!dom_cssom_edit_inline_property(node->as_element(), name, value, priority))
        return exchange.fail("invalid", "invalid CSS property, value or priority");
    radiant_reconcile_dom_mutations(inspector->ui, inspector->document);
    inspector_sync(inspector);
    exchange.revision = inspector->revision;
    return previous;
}

static Item inspector_pick(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    ItemReader x = request.get("x"), y = request.get("y");
    if ((!x.isInt() && !x.isFloat()) || (!y.isInt() && !y.isFloat()) ||
            !isfinite(x.asFloat()) || !isfinite(y.asFloat()))
        return exchange.fail("invalid", "pick requires finite viewport x/y coordinates");
    DomNode* node = (DomNode*)radiant_document_element_from_point(inspector->document,
        (float)x.asFloat(), (float)y.asFloat());
    MarkBuilder builder(exchange.input);
    Item result = node ? inspector_node_record(builder, inspector, node) : ItemNull;
    devtools_send(inspector->connection, builder.map().put("event", builder.createSymbolItem("selected"))
        .put("value", result).put("rev", inspector->revision).final());
    exchange.revision = inspector->revision;
    return result;
}

static Item inspector_highlight(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    DomNode* node = inspector_node(inspector, request, exchange);
    if (!node) return ItemNull;
    MarkBuilder builder(exchange.input);
    exchange.revision = inspector->revision;
    return inspector_box(builder, node);
}

static Item inspector_screenshot(DevtoolsExchange& exchange, MapReader, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    if (!inspector->document || !inspector->document->view_tree)
        return exchange.fail("unavailable", "target has no rendered document");
    render_html_doc(inspector->ui, inspector->document->view_tree, nullptr);
    StrBuf* png = render_encode_surface_png(inspector->ui->surface);
    if (!png) return exchange.fail("unavailable", "could not encode document pixels");
    MarkBuilder builder(exchange.input);
    Item result = builder.map().put("mime", "image/png")
        .put("pixels", (Item){.item = x2it(builder.createBinary(png->str, png->length))})
        .put("width", (double)inspector->ui->viewport_width)
        .put("height", (double)inspector->ui->viewport_height).final();
    strbuf_free(png);
    exchange.revision = inspector->revision;
    return result;
}

static Item inspector_source(DevtoolsExchange& exchange, MapReader, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    if (!inspector->source) return exchange.fail("unavailable", "no local source snapshot was captured");
    MarkBuilder builder(exchange.input);
    return builder.map().put("path", inspector->source_path).put("text", inspector->source)
        .put("kind", builder.createSymbolItem("attach_snapshot")).final();
}

static Item inspector_release(DevtoolsExchange& exchange, MapReader request, void* owner) {
    RadiantDevtools* inspector = (RadiantDevtools*)owner;
    ItemReader refs = request.get("refs");
    if (!refs.isArray() || refs.asArray().length() > 500)
        return exchange.fail("invalid", "release requires a bounded refs array");
    auto values = refs.asArray();
    for (int64_t i = 0; i < values.length(); i++) {
        ItemReader id = values.get(i);
        if (!id.isInt() || id.asInt() < 1 || id.asInt() > inspector->handles->length)
            return exchange.fail("invalid", "invalid handle in refs");
    }
    for (int64_t i = 0; i < values.length(); i++) {
        int64_t index = values.get(i).asInt() - 1;
        mem_free(inspector->handles->data[index]);
        inspector->handles->data[index] = nullptr;
    }
    return ItemNull;
}

static const DevtoolsCommand INSPECTOR_COMMANDS[] = {
    {"tree", inspector_tree}, {"node", inspector_tree}, {"style", inspector_style},
    {"set_style", inspector_set_style}, {"pick", inspector_pick},
    {"highlight", inspector_highlight}, {"screenshot", inspector_screenshot}, {"source", inspector_source},
    {"release", inspector_release},
};

RadiantDevtools* radiant_devtools_open(UiContext* ui, int fd, const char* source_path) {
    RadiantDevtools* inspector = (RadiantDevtools*)mem_calloc(1, sizeof(RadiantDevtools), MEM_CAT_TEMP);
    if (!inspector) return nullptr;
    inspector->ui = ui;
    inspector->handles = arraylist_new(32);
    inspector->source_path = mem_strdup(source_path, MEM_CAT_TEMP);
    inspector->source = read_text_file(source_path);
    if (inspector->source && strlen(inspector->source) > DEVTOOLS_MAX_BYTES / 2) {
        mem_free(inspector->source);
        inspector->source = nullptr;
    }
    inspector->connection = devtools_open(fd, INSPECTOR_COMMANDS,
        sizeof(INSPECTOR_COMMANDS) / sizeof(INSPECTOR_COMMANDS[0]), inspector);
    if (!inspector->handles || !inspector->connection) {
        radiant_devtools_destroy(inspector);
        return nullptr;
    }
    inspector_sync(inspector);
    return inspector;
}

bool radiant_devtools_tick(RadiantDevtools* inspector) {
    if (!inspector || !devtools_connected(inspector->connection)) return false;
    inspector_sync(inspector);
    devtools_dispatch_pending(inspector->connection);
    return devtools_connected(inspector->connection);
}

void radiant_devtools_destroy(RadiantDevtools* inspector) {
    if (!inspector) return;
    if (inspector->lease) inspector->lease->inspector = nullptr;
    devtools_destroy(inspector->connection);
    if (inspector->handles) {
        for (int i = 0; i < inspector->handles->length; i++) mem_free(inspector->handles->data[i]);
        arraylist_free(inspector->handles);
    }
    mem_free(inspector->source_path);
    mem_free(inspector->source);
    mem_free(inspector);
}
