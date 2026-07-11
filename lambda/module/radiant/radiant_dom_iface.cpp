// radiant module — DOM3 declared interface for Range and Selection.
//
// Shape lives in radiant_dom_interface_decl (Lambda type syntax, parsed by the
// registry at registration); behavior is the binding tables below, thin
// adapters onto the receiver-explicit engine entries in the Jube host API.
// The strcmp dispatch chains, is-native-property predicates, method caches,
// and expando side tables these replace are deleted from js_dom_selection.cpp.

#include "../../lambda.hpp"
#include "radiant_host_api.hpp"
#include "../../jube/jube.h"

extern const JubeHostAPI* radiant_host_api;
extern "C" const void* radiant_dom_range_host_type(void);
extern "C" const void* radiant_dom_selection_host_type(void);

extern const char radiant_dom_interface_decl[];
const char radiant_dom_interface_decl[] =
    "type range {\n"
    "    start_container: dom_node,\n"
    "    start_offset: int,\n"
    "    end_container: dom_node,\n"
    "    end_offset: int,\n"
    "    collapsed: bool,\n"
    "    common_ancestor_container: dom_node,\n"
    "    START_TO_START: int = 0,\n"
    "    START_TO_END: int = 1,\n"
    "    END_TO_END: int = 2,\n"
    "    END_TO_START: int = 3,\n"
    "    set_start: fn(node: dom_node, offset: int) null,\n"
    "    set_end: fn(node: dom_node, offset: int) null,\n"
    "    set_start_before: fn(node: dom_node) null,\n"
    "    set_start_after: fn(node: dom_node) null,\n"
    "    set_end_before: fn(node: dom_node) null,\n"
    "    set_end_after: fn(node: dom_node) null,\n"
    "    collapse: fn(to_start: bool) null,\n"
    "    select_node: fn(node: dom_node) null,\n"
    "    select_node_contents: fn(node: dom_node) null,\n"
    "    clone_range: fn() range,\n"
    "    compare_boundary_points: fn(how: int, other: range) int,\n"
    "    compare_point: fn(node: dom_node, offset: int) int,\n"
    "    is_point_in_range: fn(node: dom_node, offset: int) bool,\n"
    "    intersects_node: fn(node: dom_node) bool,\n"
    "    detach: fn() null,\n"
    "    to_string: fn() string,\n"
    "    get_client_rects: fn() any,\n"
    "    get_bounding_client_rect: fn() any,\n"
    "    delete_contents: fn() null,\n"
    "    extract_contents: fn() any,\n"
    "    clone_contents: fn() any,\n"
    "    insert_node: fn(node: dom_node) null,\n"
    "    surround_contents: fn(node: dom_node) null\n"
    "}\n"
    "type selection {\n"
    "    anchor_node: dom_node,\n"
    "    anchor_offset: int,\n"
    "    focus_node: dom_node,\n"
    "    focus_offset: int,\n"
    "    is_collapsed: bool,\n"
    "    range_count: int,\n"
    "    'type': string,\n"
    "    direction: string,\n"
    "    base_node: dom_node,\n"
    "    base_offset: int,\n"
    "    extent_node: dom_node,\n"
    "    extent_offset: int,\n"
    "    get_range_at: fn(index: int) range,\n"
    "    add_range: fn(r: range) null,\n"
    "    remove_range: fn(r: range) null,\n"
    "    remove_all_ranges: fn() null,\n"
    "    empty: fn() null,\n"
    "    collapse: fn(node: dom_node, offset: int) null,\n"
    "    set_position: fn(node: dom_node, offset: int) null,\n"
    "    collapse_to_start: fn() null,\n"
    "    collapse_to_end: fn() null,\n"
    "    extend: fn(node: dom_node, offset: int) null,\n"
    "    set_base_and_extent: fn(an: dom_node, ao: int, fo_node: dom_node, fo: int) null,\n"
    "    select_all_children: fn(node: dom_node) null,\n"
    "    contains_node: fn(node: dom_node, allow_partial: bool) bool,\n"
    "    delete_from_document: fn() null,\n"
    "    to_string: fn() string,\n"
    "    modify: fn(alter: string, direction: string, granularity: string) null,\n"
    "    __force_direction: fn(direction: string) null\n"
    "}\n"
    "type inline_style {\n"
    "    css_text: string,\n"
    "    length: string,\n"
    "    get_property_value: fn(prop: string) string,\n"
    "    set_property: fn(prop: string, value: string, priority: string) null,\n"
    "    remove_property: fn(prop: string) string\n"
    "}\n"
    "type computed_style {\n"
    "    css_text: string,\n"
    "    length: string,\n"
    "    get_property_value: fn(prop: string) string,\n"
    "    set_property: fn(prop: string, value: string, priority: string) null,\n"
    "    remove_property: fn(prop: string) string\n"
    "}\n";

// ---- adapters: JubeMemberBind handler shape -> host API behavior entries ----

static Item radiant_iface_arg(Item* args, int argc, int i) {
    return i < argc ? args[i] : (Item){.item = ITEM_JS_UNDEFINED};
}

#define RADIANT_GETTER(name, entry)                                          \
    static int name(Item receiver, Item* out) {                              \
        *out = radiant_host_api->dom->entry(receiver);                       \
        return 1;                                                            \
    }

#define RADIANT_METHOD_0(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        (void)args; (void)argc;                                              \
        *out = radiant_host_api->dom->entry(receiver);                       \
        return 1;                                                            \
    }

#define RADIANT_METHOD_1(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0));                               \
        return 1;                                                            \
    }

#define RADIANT_METHOD_2(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1)); \
        return 1;                                                            \
    }

#define RADIANT_METHOD_3(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1), \
            radiant_iface_arg(args, argc, 2));                               \
        return 1;                                                            \
    }

#define RADIANT_METHOD_4(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1), \
            radiant_iface_arg(args, argc, 2), radiant_iface_arg(args, argc, 3)); \
        return 1;                                                            \
    }

RADIANT_GETTER(r_start_container, range_get_start_container)
RADIANT_GETTER(r_start_offset, range_get_start_offset)
RADIANT_GETTER(r_end_container, range_get_end_container)
RADIANT_GETTER(r_end_offset, range_get_end_offset)
RADIANT_GETTER(r_collapsed, range_get_collapsed)
RADIANT_GETTER(r_common_ancestor, range_get_common_ancestor)
RADIANT_METHOD_2(r_set_start, range_set_start)
RADIANT_METHOD_2(r_set_end, range_set_end)
RADIANT_METHOD_1(r_set_start_before, range_set_start_before)
RADIANT_METHOD_1(r_set_start_after, range_set_start_after)
RADIANT_METHOD_1(r_set_end_before, range_set_end_before)
RADIANT_METHOD_1(r_set_end_after, range_set_end_after)
RADIANT_METHOD_1(r_collapse, range_collapse)
RADIANT_METHOD_1(r_select_node, range_select_node)
RADIANT_METHOD_1(r_select_node_contents, range_select_node_contents)
RADIANT_METHOD_0(r_clone_range, range_clone_range)
RADIANT_METHOD_2(r_compare_boundary_points, range_compare_boundary_points)
RADIANT_METHOD_2(r_compare_point, range_compare_point)
RADIANT_METHOD_2(r_is_point_in_range, range_is_point_in_range)
RADIANT_METHOD_1(r_intersects_node, range_intersects_node)
RADIANT_METHOD_0(r_detach, range_detach)
RADIANT_METHOD_0(r_to_string, range_to_string)
RADIANT_METHOD_0(r_get_client_rects, range_get_client_rects)
RADIANT_METHOD_0(r_get_bounding_client_rect, range_get_bounding_client_rect)
RADIANT_METHOD_0(r_delete_contents, range_delete_contents)
RADIANT_METHOD_0(r_extract_contents, range_extract_contents)
RADIANT_METHOD_0(r_clone_contents, range_clone_contents)
RADIANT_METHOD_1(r_insert_node, range_insert_node)
RADIANT_METHOD_1(r_surround_contents, range_surround_contents)

RADIANT_GETTER(s_anchor_node, selection_get_anchor_node)
RADIANT_GETTER(s_anchor_offset, selection_get_anchor_offset)
RADIANT_GETTER(s_focus_node, selection_get_focus_node)
RADIANT_GETTER(s_focus_offset, selection_get_focus_offset)
RADIANT_GETTER(s_is_collapsed, selection_get_is_collapsed)
RADIANT_GETTER(s_range_count, selection_get_range_count)
RADIANT_GETTER(s_type, selection_get_type)
RADIANT_GETTER(s_direction, selection_get_direction)
RADIANT_METHOD_1(s_get_range_at, selection_get_range_at)
RADIANT_METHOD_1(s_add_range, selection_add_range)
RADIANT_METHOD_1(s_remove_range, selection_remove_range)
RADIANT_METHOD_0(s_remove_all_ranges, selection_remove_all_ranges)
RADIANT_METHOD_0(s_empty, selection_empty)
RADIANT_METHOD_2(s_collapse, selection_collapse)
RADIANT_METHOD_2(s_set_position, selection_set_position)
RADIANT_METHOD_0(s_collapse_to_start, selection_collapse_to_start)
RADIANT_METHOD_0(s_collapse_to_end, selection_collapse_to_end)
RADIANT_METHOD_2(s_extend, selection_extend)
RADIANT_METHOD_4(s_set_base_and_extent, selection_set_base_and_extent)
RADIANT_METHOD_1(s_select_all_children, selection_select_all_children)
RADIANT_METHOD_2(s_contains_node, selection_contains_node)
RADIANT_METHOD_0(s_delete_from_document, selection_delete_from_document)
RADIANT_METHOD_0(s_to_string, selection_to_string)
RADIANT_METHOD_3(s_modify, selection_modify)
RADIANT_METHOD_1(s_force_direction, selection_force_direction)

// prototype identity comes live from the runtime's global Range/Selection
// constructors (same source the deleted engine dispatch used)
static Item radiant_range_prototype_seed(void) {
    return radiant_host_api->dom->range_get_prototype_value();
}

static Item radiant_selection_prototype_seed(void) {
    return radiant_host_api->dom->selection_get_prototype_value();
}

#define BIND_GET(n, fn)      {n, NULL, NULL, NULL, fn, NULL, NULL, NULL, 0}
#define BIND_GET_HIDDEN(n, fn) \
    {n, NULL, NULL, NULL, fn, NULL, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_CALL(n, fn)     {n, NULL, NULL, NULL, NULL, NULL, fn, NULL, 0}
#define BIND_CALL_JS(n, js, fn) {n, js, NULL, NULL, NULL, NULL, fn, NULL, 0}

static const JubeMemberBind radiant_range_members[] = {
    BIND_GET("start_container", r_start_container),
    BIND_GET("start_offset", r_start_offset),
    BIND_GET("end_container", r_end_container),
    BIND_GET("end_offset", r_end_offset),
    BIND_GET("collapsed", r_collapsed),
    BIND_GET("common_ancestor_container", r_common_ancestor),
    BIND_CALL("set_start", r_set_start),
    BIND_CALL("set_end", r_set_end),
    BIND_CALL("set_start_before", r_set_start_before),
    BIND_CALL("set_start_after", r_set_start_after),
    BIND_CALL("set_end_before", r_set_end_before),
    BIND_CALL("set_end_after", r_set_end_after),
    BIND_CALL("collapse", r_collapse),
    BIND_CALL("select_node", r_select_node),
    BIND_CALL("select_node_contents", r_select_node_contents),
    BIND_CALL("clone_range", r_clone_range),
    BIND_CALL("compare_boundary_points", r_compare_boundary_points),
    BIND_CALL("compare_point", r_compare_point),
    BIND_CALL("is_point_in_range", r_is_point_in_range),
    BIND_CALL("intersects_node", r_intersects_node),
    BIND_CALL("detach", r_detach),
    BIND_CALL("to_string", r_to_string),
    BIND_CALL("get_client_rects", r_get_client_rects),
    BIND_CALL("get_bounding_client_rect", r_get_bounding_client_rect),
    BIND_CALL("delete_contents", r_delete_contents),
    BIND_CALL("extract_contents", r_extract_contents),
    BIND_CALL("clone_contents", r_clone_contents),
    BIND_CALL("insert_node", r_insert_node),
    BIND_CALL("surround_contents", r_surround_contents),
};

static const JubeMemberBind radiant_selection_members[] = {
    BIND_GET("anchor_node", s_anchor_node),
    BIND_GET("anchor_offset", s_anchor_offset),
    BIND_GET("focus_node", s_focus_node),
    BIND_GET("focus_offset", s_focus_offset),
    BIND_GET("is_collapsed", s_is_collapsed),
    BIND_GET("range_count", s_range_count),
    BIND_GET("type", s_type),
    BIND_GET("direction", s_direction),
    // legacy aliases shadow the anchor/focus members and stay out of own-keys
    BIND_GET_HIDDEN("base_node", s_anchor_node),
    BIND_GET_HIDDEN("base_offset", s_anchor_offset),
    BIND_GET_HIDDEN("extent_node", s_focus_node),
    BIND_GET_HIDDEN("extent_offset", s_focus_offset),
    BIND_CALL("get_range_at", s_get_range_at),
    BIND_CALL("add_range", s_add_range),
    BIND_CALL("remove_range", s_remove_range),
    BIND_CALL("remove_all_ranges", s_remove_all_ranges),
    BIND_CALL("empty", s_empty),
    BIND_CALL("collapse", s_collapse),
    BIND_CALL("set_position", s_set_position),
    BIND_CALL("collapse_to_start", s_collapse_to_start),
    BIND_CALL("collapse_to_end", s_collapse_to_end),
    BIND_CALL("extend", s_extend),
    BIND_CALL("set_base_and_extent", s_set_base_and_extent),
    BIND_CALL("select_all_children", s_select_all_children),
    BIND_CALL("contains_node", s_contains_node),
    BIND_CALL("delete_from_document", s_delete_from_document),
    BIND_CALL("to_string", s_to_string),
    BIND_CALL("modify", s_modify),
    // WPT testdriver shim internal; snake->camel derivation cannot produce the
    // double-underscore-preserving spelling, hence the explicit js_name
    BIND_CALL_JS("__force_direction", "__forceDirection", s_force_direction),
};


// ---- style hosts (inline_style / computed_style) ----
// Pinned legacy behaviors preserved by construction: Object.keys = [] (all
// members non-enumerable, expandos impossible), `length` reads as "" (a CSS
// property-table miss, not a count), no prototype object, and non-CSS writes
// are swallowed by the CSS parser rather than stored.

extern "C" Item radiant_dom_wrap_node(void* dom_elem);

static Item radiant_style_key(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

// inline-style wrappers carry the owner DomElement* as host_data; the engine
// style entries take the owner ELEMENT item
static Item radiant_style_owner_item(Item receiver) {
    return radiant_dom_wrap_node(receiver.vmap->host_data);
}

static int st_css_text_get(Item r, Item* out) {
    *out = radiant_host_api->dom->style_get_property(radiant_style_owner_item(r),
                                                     radiant_style_key("cssText"));
    return 1;
}

static int st_css_text_set(Item r, Item v, Item* out) {
    *out = radiant_host_api->dom->style_set_property(radiant_style_owner_item(r),
                                                     radiant_style_key("cssText"), v);
    return 1;
}

static int st_length_get(Item r, Item* out) {
    *out = radiant_host_api->dom->style_get_property(radiant_style_owner_item(r),
                                                     radiant_style_key("length"));
    return 1;
}

static int st_named_get(Item r, Item key, Item* out) {
    *out = radiant_host_api->dom->style_get_property(radiant_style_owner_item(r), key);
    return 1;
}

static int st_named_set(Item r, Item key, Item v, Item* out) {
    *out = radiant_host_api->dom->style_set_property(radiant_style_owner_item(r), key, v);
    return 1;
}

static int st_named_has(Item r, Item key, Item* out) {
    *out = radiant_host_api->dom->style_css_has(r, key);
    return 1;
}

static int st_get_property_value(Item r, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->style_get_property(radiant_style_owner_item(r),
        radiant_iface_arg(args, argc, 0));
    return 1;
}

static int st_set_property(Item r, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->style_set_property_bridge(r.vmap->host_data,
        radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1),
        radiant_iface_arg(args, argc, 2), argc >= 3);
    return 1;
}

static int st_remove_property(Item r, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->style_remove_property_bridge(r.vmap->host_data,
        radiant_iface_arg(args, argc, 0));
    return 1;
}

// computed style: read-only; the resolver entry takes the style item itself
static int cs_get(Item r, Item key, Item* out) {
    *out = radiant_host_api->dom->computed_style_get_property(r, key);
    return 1;
}

static int cs_css_text_get(Item r, Item* out) {
    return cs_get(r, radiant_style_key("cssText"), out);
}

static int cs_length_get(Item r, Item* out) {
    return cs_get(r, radiant_style_key("length"), out);
}

static int cs_get_property_value(Item r, Item* args, int argc, Item* out) {
    return cs_get(r, radiant_iface_arg(args, argc, 0), out);
}

static int cs_noop_method(Item r, Item* args, int argc, Item* out) {
    (void)r; (void)args; (void)argc;
    *out = ItemNull;
    return 1;
}

static int cs_named_set(Item r, Item key, Item v, Item* out) {
    (void)r; (void)key;
    *out = v;
    return 1;
}

// style objects have no prototype (a non-map seed records "none")
static Item radiant_style_no_prototype(void) {
    return ItemNull;
}

static const JubeMemberBind radiant_inline_style_members[] = {
    {"css_text", NULL, NULL, NULL, st_css_text_get, st_css_text_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    BIND_GET_HIDDEN("length", st_length_get),
    BIND_CALL("get_property_value", st_get_property_value),
    BIND_CALL("set_property", st_set_property),
    BIND_CALL("remove_property", st_remove_property),
};

static const JubeMemberBind radiant_computed_style_members[] = {
    BIND_GET_HIDDEN("css_text", cs_css_text_get),
    BIND_GET_HIDDEN("length", cs_length_get),
    BIND_CALL("get_property_value", cs_get_property_value),
    BIND_CALL("set_property", cs_noop_method),
    BIND_CALL("remove_property", cs_noop_method),
};

extern const JubeTypeBinding radiant_dom_type_bindings[];
const JubeTypeBinding radiant_dom_type_bindings[] = {
    {"range", NULL, radiant_range_members,
     (int32_t)(sizeof(radiant_range_members) / sizeof(radiant_range_members[0])),
     NULL, NULL, NULL, NULL, radiant_range_prototype_seed},
    {"selection", NULL, radiant_selection_members,
     (int32_t)(sizeof(radiant_selection_members) / sizeof(radiant_selection_members[0])),
     NULL, NULL, NULL, NULL, radiant_selection_prototype_seed, NULL},
    {"inline_style", NULL, radiant_inline_style_members,
     (int32_t)(sizeof(radiant_inline_style_members) / sizeof(radiant_inline_style_members[0])),
     st_named_get, st_named_set, NULL, NULL, radiant_style_no_prototype, st_named_has},
    {"computed_style", NULL, radiant_computed_style_members,
     (int32_t)(sizeof(radiant_computed_style_members) / sizeof(radiant_computed_style_members[0])),
     cs_get, cs_named_set, NULL, NULL, radiant_style_no_prototype, st_named_has},
};

extern const int32_t radiant_dom_type_binding_count;
const int32_t radiant_dom_type_binding_count =
    (int32_t)(sizeof(radiant_dom_type_bindings) / sizeof(radiant_dom_type_bindings[0]));
