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
    "}\n"
    "type stylesheet {\n"
    "    css_rules: any,\n"
    "    rules: any,\n"
    "    length: int,\n"
    "    disabled: bool,\n"
    "    'type': string,\n"
    "    href: string,\n"
    "    title: string,\n"
    "    owner_node: any,\n"
    "    insert_rule: fn(text: string, index: int) int,\n"
    "    delete_rule: fn(index: int) null\n"
    "}\n"
    "type css_rule {\n"
    "    selector_text: string,\n"
    "    style: any,\n"
    "    css_rules: any,\n"
    "    rules: any,\n"
    "    css_text: string,\n"
    "    'type': int,\n"
    "    parent_rule: any,\n"
    "    parent_style_sheet: any\n"
    "}\n"
    "type dom_node {\n"
    "    tag_name: string,\n"
    "    node_name: string,\n"
    "    local_name: string,\n"
    "    namespace_uri: string,\n"
    "    prefix: any,\n"
    "    id: string,\n"
    "    class_name: string,\n"
    "    node_type: int,\n"
    "    parent_node: any,\n"
    "    parent_element: any,\n"
    "    is_connected: bool,\n"
    "    child_element_count: int,\n"
    "    children: any,\n"
    "    attributes: any,\n"
    "    owner_document: any,\n"
    "    first_child: any,\n"
    "    last_child: any,\n"
    "    next_sibling: any,\n"
    "    previous_sibling: any,\n"
    "    first_element_child: any,\n"
    "    last_element_child: any,\n"
    "    next_element_sibling: any,\n"
    "    previous_element_sibling: any,\n"
    "    child_nodes: any,\n"
    "    disabled: bool,\n"
    "    required: bool,\n"
    "    multiple: bool,\n"
    "    read_only: bool,\n"
    "    readonly: bool,\n"
    "    no_validate: bool,\n"
    "    form_no_validate: bool,\n"
    "    open: bool,\n"
    "    default_checked: bool,\n"
    "    default_selected: bool,\n"
    "    autofocus: bool,\n"
    "    max_length: int,\n"
    "    min_length: int,\n"
    "    size: int,\n"
    "    width: int,\n"
    "    height: int,\n"
    "    rows: int,\n"
    "    cols: int,\n"
    "    src: string,\n"
    "    href: string,\n"
    "    alt: string,\n"
    "    name: string,\n"
    "    placeholder: string,\n"
    "    autocomplete: string,\n"
    "    pattern: string,\n"
    "    min: string,\n"
    "    max: string,\n"
    "    step: string,\n"
    "    accept: string,\n"
    "    html_for: string,\n"
    "    target: string,\n"
    "    accept_charset: string,\n"
    "    form_target: string,\n"
    "    wrap: string,\n"
    "    input_mode: string,\n"
    "    enter_key_hint: string,\n"
    "    content_editable: string,\n"
    "    checked: bool,\n"
    "    value: string,\n"
    "    selected_index: int,\n"
    "    length: int,\n"
    "    selected: bool,\n"
    "    text: string,\n"
    "    selection_start: string,\n"
    "    selection_end: string,\n"
    "    selection_direction: string,\n"
    "    default_value: string,\n"
    "    options: any,\n"
    "    selected_options: any,\n"
    "    'type': string,\n"
    "    index: int,\n"
    "    label: string,\n"
    "    form: any,\n"
    "    is_content_editable: bool,\n"
    "    named_item: fn(a0: any) any,\n"
    "    add: fn(a0: any, a1: any) any,\n"
    "    remove: fn(a0: any) any,\n"
    "    contains: fn(a0: any) any,\n"
    "    compare_document_position: fn(a0: any) any,\n"
    "    replace_with: fn(a0: any) any,\n"
    "    has_child_nodes: fn() any,\n"
    "    clone_node: fn(a0: any) any,\n"
    "    replace_data: fn(a0: any, a1: any, a2: any) any,\n"
    "    insert_data: fn(a0: any, a1: any) any,\n"
    "    append_data: fn(a0: any) any,\n"
    "    delete_data: fn(a0: any, a1: any) any,\n"
    "    substring_data: fn(a0: any, a1: any) any,\n"
    "    get_attribute: fn(a0: any) any,\n"
    "    set_attribute: fn(a0: any, a1: any) any,\n"
    "    remove_attribute: fn(a0: any) any,\n"
    "    toggle_attribute: fn(a0: any, a1: any) any,\n"
    "    has_attribute: fn(a0: any) any,\n"
    "    get_attribute_names: fn() any,\n"
    "    matches: fn(a0: any) any,\n"
    "    query_selector: fn(a0: any) any,\n"
    "    query_selector_all: fn(a0: any) any,\n"
    "    closest: fn(a0: any) any,\n"
    "    get_elements_by_tag_name: fn(a0: any) any,\n"
    "    get_elements_by_class_name: fn(a0: any) any,\n"
    "    get_element_by_id: fn(a0: any) any,\n"
    "    add_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    remove_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    dispatch_event: fn(a0: any) any,\n"
    "    append_child: fn(a0: any) any,\n"
    "    remove_child: fn(a0: any) any,\n"
    "    insert_before: fn(a0: any, a1: any) any,\n"
    "    replace_child: fn(a0: any, a1: any) any,\n"
    "    normalize: fn() any,\n"
    "    append: fn(a0: any) any,\n"
    "    prepend: fn(a0: any) any,\n"
    "    insert_adjacent_element: fn(a0: any, a1: any) any,\n"
    "    insert_adjacent_html: fn(a0: any, a1: any) any,\n"
    "    get_bounding_client_rect: fn() any,\n"
    "    get_client_rects: fn() any,\n"
    "    scroll_into_view: fn(a0: any) any,\n"
    "    scroll: fn(a0: any, a1: any) any,\n"
    "    scroll_to: fn(a0: any, a1: any) any,\n"
    "    scroll_by: fn(a0: any, a1: any) any,\n"
    "    focus: fn() any,\n"
    "    blur: fn() any,\n"
    "    click: fn() any,\n"
    "    reset: fn() any,\n"
    "    submit: fn() any,\n"
    "    request_submit: fn(a0: any) any,\n"
    "    check_validity: fn() any,\n"
    "    report_validity: fn() any,\n"
    "    set_custom_validity: fn(a0: any) any,\n"
    "    set_selection_range: fn(a0: any, a1: any, a2: any) any,\n"
    "    set_range_text: fn(a0: any, a1: any, a2: any, a3: any) any,\n"
    "    select: fn() any,\n"
    "    item: fn(a0: any) any,\n"
    "    toggle: fn(a0: any, a1: any) any,\n"
    "    replace: fn(a0: any, a1: any) any,\n"
    "    attach_shadow: fn(a0: any) any,\n"
    "    to_string: fn() any,\n"
    "    __lambda_boundary_from_point: fn(a0: any, a1: any, a2: any) any,\n"
    "    __lambda_text_control_boundary_from_point: fn(a0: any, a1: any) any,\n"
    "    __lambda_text_control_caret_bounds: fn() any\n"
    "}\n"
    "type rule_style_decl {\n"
    "    length: int,\n"
    "    css_text: string,\n"
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


// ---- CSSOM (stylesheet / css_rule / rule_style_decl) ----
// Pinned behaviors preserved: keys = [], descriptors undefined for open names,
// no prototypes, unknown-name writes swallowed (stylesheet/rule) or parsed as
// CSS declarations (rule_style_decl). The drifted has-chain is gone: `in` now
// derives from the same declarations `get` serves, plus null-valued members
// (owner_node, parent_style_sheet) that the legacy chain reported present.

#define RADIANT_GETTER_D(name, entry)                                        \
    static int name(Item receiver, Item* out) {                              \
        *out = radiant_host_api->dom->entry(receiver);                       \
        return 1;                                                            \
    }

RADIANT_GETTER_D(sh_css_rules, stylesheet_get_css_rules)
RADIANT_GETTER_D(sh_length, stylesheet_get_length)
RADIANT_GETTER_D(sh_disabled, stylesheet_get_disabled)
RADIANT_GETTER_D(sh_type, stylesheet_get_type)
RADIANT_GETTER_D(sh_href, stylesheet_get_href)
RADIANT_GETTER_D(sh_title, stylesheet_get_title)

static int cssom_null_get(Item receiver, Item* out) {
    // has-only legacy members (ownerNode, parentStyleSheet): the old chain
    // reported them present while reads returned null — declaring them with a
    // null getter preserves both halves
    (void)receiver;
    *out = ItemNull;
    return 1;
}

static int cssom_swallow_set(Item receiver, Item key, Item value, Item* out) {
    (void)receiver; (void)key;
    *out = value;
    return 1;
}

static int sh_indexed_get(Item receiver, int64_t index, Item* out) {
    *out = radiant_host_api->dom->stylesheet_index(receiver, index);
    return 1;
}

static int sh_insert_rule(Item receiver, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->stylesheet_insert_rule(receiver,
        radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1));
    return 1;
}

static int sh_delete_rule(Item receiver, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->stylesheet_delete_rule(receiver,
        radiant_iface_arg(args, argc, 0));
    return 1;
}

RADIANT_GETTER_D(cr_selector_text, rule_get_selector_text)
RADIANT_GETTER_D(cr_style, rule_get_style)
RADIANT_GETTER_D(cr_css_rules, rule_get_css_rules)
RADIANT_GETTER_D(cr_css_text, rule_get_css_text)
RADIANT_GETTER_D(cr_type, rule_get_type)
RADIANT_GETTER_D(cr_parent_rule, rule_get_parent_rule)

static int cr_selector_text_set(Item receiver, Item value, Item* out) {
    *out = radiant_host_api->dom->rule_set_selector_text(receiver, value);
    return 1;
}

// rule declarations: CSS property names are the open-name surface
static int rd_named_get(Item receiver, Item key, Item* out) {
    *out = radiant_host_api->dom->cssom_rule_decl_get_property(receiver, key);
    return 1;
}

static int rd_named_set(Item receiver, Item key, Item value, Item* out) {
    *out = radiant_host_api->dom->cssom_rule_decl_set_property(receiver, key, value);
    return 1;
}

static int rd_named_has(Item receiver, Item key, Item* out) {
    *out = radiant_host_api->dom->rule_decl_css_has(receiver, key);
    return 1;
}

static int rd_length_get(Item receiver, Item* out) {
    return rd_named_get(receiver, radiant_style_key("length"), out);
}

static int rd_css_text_get(Item receiver, Item* out) {
    return rd_named_get(receiver, radiant_style_key("cssText"), out);
}

static int rd_get_property_value(Item receiver, Item* args, int argc, Item* out) {
    return rd_named_get(receiver, radiant_iface_arg(args, argc, 0), out);
}

static int rd_set_property(Item receiver, Item* args, int argc, Item* out) {
    // the legacy method dispatcher dropped the priority argument; preserved
    return rd_named_set(receiver, radiant_iface_arg(args, argc, 0),
                        radiant_iface_arg(args, argc, 1), out);
}

static int rd_remove_property(Item receiver, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom->rule_decl_remove_property(receiver,
        radiant_iface_arg(args, argc, 0));
    return 1;
}

static const JubeMemberBind radiant_stylesheet_members[] = {
    BIND_GET_HIDDEN("css_rules", sh_css_rules),
    BIND_GET_HIDDEN("rules", sh_css_rules),
    BIND_GET_HIDDEN("length", sh_length),
    BIND_GET_HIDDEN("disabled", sh_disabled),
    BIND_GET_HIDDEN("type", sh_type),
    BIND_GET_HIDDEN("href", sh_href),
    BIND_GET_HIDDEN("title", sh_title),
    BIND_GET_HIDDEN("owner_node", cssom_null_get),
    BIND_CALL("insert_rule", sh_insert_rule),
    BIND_CALL("delete_rule", sh_delete_rule),
};

static const JubeMemberBind radiant_css_rule_members[] = {
    {"selector_text", NULL, NULL, NULL, cr_selector_text, cr_selector_text_set,
     NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE},
    BIND_GET_HIDDEN("style", cr_style),
    BIND_GET_HIDDEN("css_rules", cr_css_rules),
    BIND_GET_HIDDEN("rules", cr_css_rules),
    BIND_GET_HIDDEN("css_text", cr_css_text),
    BIND_GET_HIDDEN("type", cr_type),
    BIND_GET_HIDDEN("parent_rule", cr_parent_rule),
    BIND_GET_HIDDEN("parent_style_sheet", cssom_null_get),
};

static const JubeMemberBind radiant_rule_decl_members[] = {
    BIND_GET_HIDDEN("length", rd_length_get),
    BIND_GET_HIDDEN("css_text", rd_css_text_get),
    BIND_CALL("get_property_value", rd_get_property_value),
    BIND_CALL("set_property", rd_set_property),
    BIND_CALL("remove_property", rd_remove_property),
};


// ---- dom_node Phase 4a: identity/navigation cluster ----
// Transitional: legacy_ops carries everything unconverted (text/comment
// kinds, reflected attrs, tag-gated members, methods, side-table expandos,
// per-kind prototypes, own-keys). Members guard element-only so text/comment
// reads fall through to the legacy chains unchanged.

extern const JubeHostObjectOps radiant_dom_node_host_ops;
extern "C" int radiant_dom_member_is_element(Item receiver);
extern "C" int radiant_dom_member_tag_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_local_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_namespace_uri(Item receiver, Item* out);
extern "C" int radiant_dom_member_prefix(Item receiver, Item* out);
extern "C" int radiant_dom_member_id(Item receiver, Item* out);
extern "C" int radiant_dom_member_class_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_node_type(Item receiver, Item* out);
extern "C" int radiant_dom_member_parent_node(Item receiver, Item* out);
extern "C" int radiant_dom_member_is_connected(Item receiver, Item* out);
extern "C" int radiant_dom_member_child_element_count(Item receiver, Item* out);
extern "C" int radiant_dom_member_children(Item receiver, Item* out);
extern "C" int radiant_dom_member_attributes(Item receiver, Item* out);
extern "C" int radiant_dom_member_owner_document(Item receiver, Item* out);
extern "C" int radiant_dom_member_first_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_last_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_next_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_previous_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_first_element_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_last_element_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_next_element_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_previous_element_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_child_nodes(Item receiver, Item* out);
extern "C" int radiant_dom_guard_dis(Item receiver);
extern "C" int radiant_dom_guard_ist(Item receiver);
extern "C" int radiant_dom_guard_it(Item receiver);
extern "C" int radiant_dom_guard_ib(Item receiver);
extern "C" int radiant_dom_guard_fist(Item receiver);
extern "C" int radiant_dom_guard_input(Item receiver);
extern "C" int radiant_dom_guard_select(Item receiver);
extern "C" int radiant_dom_guard_textarea(Item receiver);
extern "C" int radiant_dom_guard_form(Item receiver);
extern "C" int radiant_dom_guard_details(Item receiver);
extern "C" int radiant_dom_guard_option(Item receiver);
extern "C" int radiant_dom_guard_img(Item receiver);
extern "C" int radiant_dom_guard_srct(Item receiver);
extern "C" int radiant_dom_guard_hreft(Item receiver);
extern "C" int radiant_dom_guard_namet(Item receiver);
extern "C" int radiant_dom_guard_lblout(Item receiver);
extern "C" int radiant_dom_m4b_disabled_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_disabled_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_required_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_required_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_multiple_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_multiple_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_multiple2_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_multiple2_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_read_only_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_read_only_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_readonly_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_readonly_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_no_validate_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_no_validate_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_form_no_validate_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_form_no_validate_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_open_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_open_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_default_checked_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_default_checked_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_default_selected_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_default_selected_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_autofocus_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_autofocus_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_max_length_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_max_length_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_min_length_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_min_length_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_size_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_size_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_size2_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_size2_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_width_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_width_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_height_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_height_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_rows_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_rows_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_cols_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_cols_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_src_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_src_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_href_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_href_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_alt_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_alt_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_name_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_name_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_placeholder_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_placeholder_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_autocomplete_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_autocomplete_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_pattern_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_pattern_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_min_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_min_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_max_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_max_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_step_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_step_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_accept_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_accept_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_html_for_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_html_for_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_target_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_target_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_accept_charset_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_accept_charset_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_form_target_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_form_target_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_wrap_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_wrap_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_input_mode_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_input_mode_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_enter_key_hint_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_enter_key_hint_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_content_editable_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_content_editable_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_is_content_editable_get(Item r, Item* out);
extern "C" int radiant_dom_guard_tc(Item receiver);
extern "C" int radiant_dom_guard_input_nontc(Item receiver);
extern "C" int radiant_dom_guard_node(Item receiver);
extern "C" int radiant_dom_guard_text(Item receiver);
extern "C" Item radiant_dom_element_method(Item elem_item, Item method_name, Item* args, int argc);
extern "C" int radiant_dom_m4d_named_item(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_add(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_remove(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_contains(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_compare_document_position(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_remove2(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_replace_with(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_has_child_nodes(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_clone_node(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_replace_data(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_insert_data(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_append_data(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_delete_data(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_substring_data(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_attribute(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_set_attribute(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_remove_attribute(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_toggle_attribute(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_has_attribute(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_attribute_names(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_matches(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_query_selector(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_query_selector_all(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_closest(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_elements_by_tag_name(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_elements_by_class_name(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_element_by_id(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_add_event_listener(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_remove_event_listener(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_dispatch_event(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_append_child(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_remove_child(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_insert_before(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_replace_child(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_normalize(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_append(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_prepend(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_insert_adjacent_element(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_insert_adjacent_html(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_bounding_client_rect(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_get_client_rects(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_scroll_into_view(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_scroll(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_scroll_to(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_scroll_by(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_focus(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_blur(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_click(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_reset(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_submit(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_request_submit(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_check_validity(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_report_validity(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_set_custom_validity(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_set_selection_range(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_set_range_text(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_select(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_item(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_toggle(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_replace(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_attach_shadow(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d_to_string(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d___lambda_boundary_from_point(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d___lambda_text_control_boundary_from_point(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4d___lambda_text_control_caret_bounds(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4c_get_checked(Item r, Item* out);
extern "C" int radiant_dom_m4c_checked_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_value(Item r, Item* out);
extern "C" int radiant_dom_m4c_value_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value2_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value3_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value4_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_selectedIndex(Item r, Item* out);
extern "C" int radiant_dom_m4c_selected_index_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_length(Item r, Item* out);
extern "C" int radiant_dom_m4c_length_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_selected(Item r, Item* out);
extern "C" int radiant_dom_m4c_selected_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_text(Item r, Item* out);
extern "C" int radiant_dom_m4c_text_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_selectionStart(Item r, Item* out);
extern "C" int radiant_dom_m4c_selection_start_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_selectionEnd(Item r, Item* out);
extern "C" int radiant_dom_m4c_selection_end_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_selectionDirection(Item r, Item* out);
extern "C" int radiant_dom_m4c_selection_direction_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_defaultValue(Item r, Item* out);
extern "C" int radiant_dom_m4c_default_value_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_options(Item r, Item* out);
extern "C" int radiant_dom_m4c_get_selectedOptions(Item r, Item* out);
extern "C" int radiant_dom_m4c_get_type(Item r, Item* out);
extern "C" int radiant_dom_m4c_get_index(Item r, Item* out);
extern "C" int radiant_dom_m4c_get_label(Item r, Item* out);
extern "C" int radiant_dom_m4c_get_form(Item r, Item* out);

#define BIND_NODE(n, fn) \
    {n, NULL, NULL, radiant_dom_member_is_element, fn, NULL, NULL, NULL, \
     JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_NODE_JS(n, js, fn) \
    {n, js, NULL, radiant_dom_member_is_element, fn, NULL, NULL, NULL, \
     JUBE_MEMBER_NON_ENUMERABLE}

static const JubeMemberBind radiant_dom_node_members[] = {
    BIND_NODE("tag_name", radiant_dom_member_tag_name),
    BIND_NODE("node_name", radiant_dom_member_tag_name),
    BIND_NODE("local_name", radiant_dom_member_local_name),
    BIND_NODE_JS("namespace_uri", "namespaceURI", radiant_dom_member_namespace_uri),
    BIND_NODE("prefix", radiant_dom_member_prefix),
    BIND_NODE("id", radiant_dom_member_id),
    BIND_NODE("class_name", radiant_dom_member_class_name),
    BIND_NODE("node_type", radiant_dom_member_node_type),
    BIND_NODE("parent_node", radiant_dom_member_parent_node),
    BIND_NODE("parent_element", radiant_dom_member_parent_node),
    BIND_NODE("is_connected", radiant_dom_member_is_connected),
    BIND_NODE("child_element_count", radiant_dom_member_child_element_count),
    BIND_NODE("children", radiant_dom_member_children),
    BIND_NODE("attributes", radiant_dom_member_attributes),
    BIND_NODE("owner_document", radiant_dom_member_owner_document),
    BIND_NODE("first_child", radiant_dom_member_first_child),
    BIND_NODE("last_child", radiant_dom_member_last_child),
    BIND_NODE("next_sibling", radiant_dom_member_next_sibling),
    BIND_NODE("previous_sibling", radiant_dom_member_previous_sibling),
    BIND_NODE("first_element_child", radiant_dom_member_first_element_child),
    BIND_NODE("last_element_child", radiant_dom_member_last_element_child),
    BIND_NODE("next_element_sibling", radiant_dom_member_next_element_sibling),
    BIND_NODE("previous_element_sibling", radiant_dom_member_previous_element_sibling),
    BIND_NODE("child_nodes", radiant_dom_member_child_nodes),
    {"disabled", NULL, NULL, radiant_dom_guard_dis, radiant_dom_m4b_disabled_get, radiant_dom_m4b_disabled_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"required", NULL, NULL, radiant_dom_guard_ist, radiant_dom_m4b_required_get, radiant_dom_m4b_required_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"multiple", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_multiple_get, radiant_dom_m4b_multiple_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"multiple", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4b_multiple2_get, radiant_dom_m4b_multiple2_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"read_only", "readOnly", NULL, radiant_dom_guard_it, radiant_dom_m4b_read_only_get, radiant_dom_m4b_read_only_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"readonly", NULL, NULL, radiant_dom_guard_it, radiant_dom_m4b_readonly_get, radiant_dom_m4b_readonly_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"no_validate", "noValidate", NULL, radiant_dom_guard_form, radiant_dom_m4b_no_validate_get, radiant_dom_m4b_no_validate_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"form_no_validate", "formNoValidate", NULL, radiant_dom_guard_ib, radiant_dom_m4b_form_no_validate_get, radiant_dom_m4b_form_no_validate_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"open", NULL, NULL, radiant_dom_guard_details, radiant_dom_m4b_open_get, radiant_dom_m4b_open_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"default_checked", "defaultChecked", NULL, radiant_dom_guard_input, radiant_dom_m4b_default_checked_get, radiant_dom_m4b_default_checked_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"default_selected", "defaultSelected", NULL, radiant_dom_guard_option, radiant_dom_m4b_default_selected_get, radiant_dom_m4b_default_selected_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"autofocus", NULL, NULL, radiant_dom_member_is_element, radiant_dom_m4b_autofocus_get, radiant_dom_m4b_autofocus_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"max_length", "maxLength", NULL, radiant_dom_guard_it, radiant_dom_m4b_max_length_get, radiant_dom_m4b_max_length_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"min_length", "minLength", NULL, radiant_dom_guard_it, radiant_dom_m4b_min_length_get, radiant_dom_m4b_min_length_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"size", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_size_get, radiant_dom_m4b_size_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"size", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4b_size2_get, radiant_dom_m4b_size2_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"width", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_width_get, radiant_dom_m4b_width_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"height", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_height_get, radiant_dom_m4b_height_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"rows", NULL, NULL, radiant_dom_guard_textarea, radiant_dom_m4b_rows_get, radiant_dom_m4b_rows_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"cols", NULL, NULL, radiant_dom_guard_textarea, radiant_dom_m4b_cols_get, radiant_dom_m4b_cols_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"src", NULL, NULL, radiant_dom_guard_srct, radiant_dom_m4b_src_get, radiant_dom_m4b_src_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"href", NULL, NULL, radiant_dom_guard_hreft, radiant_dom_m4b_href_get, radiant_dom_m4b_href_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"alt", NULL, NULL, radiant_dom_guard_img, radiant_dom_m4b_alt_get, radiant_dom_m4b_alt_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"name", NULL, NULL, radiant_dom_guard_namet, radiant_dom_m4b_name_get, radiant_dom_m4b_name_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"placeholder", NULL, NULL, radiant_dom_guard_it, radiant_dom_m4b_placeholder_get, radiant_dom_m4b_placeholder_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"autocomplete", NULL, NULL, radiant_dom_guard_fist, radiant_dom_m4b_autocomplete_get, radiant_dom_m4b_autocomplete_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"pattern", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_pattern_get, radiant_dom_m4b_pattern_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"min", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_min_get, radiant_dom_m4b_min_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"max", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_max_get, radiant_dom_m4b_max_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"step", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_step_get, radiant_dom_m4b_step_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"accept", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4b_accept_get, radiant_dom_m4b_accept_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"html_for", "htmlFor", NULL, radiant_dom_guard_lblout, radiant_dom_m4b_html_for_get, radiant_dom_m4b_html_for_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"target", NULL, NULL, radiant_dom_guard_form, radiant_dom_m4b_target_get, radiant_dom_m4b_target_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"accept_charset", "acceptCharset", NULL, radiant_dom_guard_form, radiant_dom_m4b_accept_charset_get, radiant_dom_m4b_accept_charset_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"form_target", "formTarget", NULL, radiant_dom_guard_ib, radiant_dom_m4b_form_target_get, radiant_dom_m4b_form_target_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"wrap", NULL, NULL, radiant_dom_guard_textarea, radiant_dom_m4b_wrap_get, radiant_dom_m4b_wrap_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"input_mode", "inputMode", NULL, radiant_dom_member_is_element, radiant_dom_m4b_input_mode_get, radiant_dom_m4b_input_mode_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"enter_key_hint", "enterKeyHint", NULL, radiant_dom_member_is_element, radiant_dom_m4b_enter_key_hint_get, radiant_dom_m4b_enter_key_hint_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"content_editable", "contentEditable", NULL, radiant_dom_member_is_element, radiant_dom_m4b_content_editable_get, radiant_dom_m4b_content_editable_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"is_content_editable", "isContentEditable", NULL, radiant_dom_member_is_element, radiant_dom_m4b_is_content_editable_get, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"checked", NULL, NULL, radiant_dom_guard_input, radiant_dom_m4c_get_checked, radiant_dom_m4c_checked_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"value", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4c_get_value, radiant_dom_m4c_value_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"value", NULL, NULL, radiant_dom_guard_tc, radiant_dom_m4c_get_value, radiant_dom_m4c_value2_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"value", NULL, NULL, radiant_dom_guard_input_nontc, radiant_dom_m4c_get_value, radiant_dom_m4c_value3_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"value", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_value, radiant_dom_m4c_value4_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selected_index", "selectedIndex", NULL, radiant_dom_guard_select, radiant_dom_m4c_get_selectedIndex, radiant_dom_m4c_selected_index_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"length", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4c_get_length, radiant_dom_m4c_length_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selected", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_selected, radiant_dom_m4c_selected_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"text", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_text, radiant_dom_m4c_text_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selection_start", "selectionStart", NULL, radiant_dom_guard_tc, radiant_dom_m4c_get_selectionStart, radiant_dom_m4c_selection_start_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selection_end", "selectionEnd", NULL, radiant_dom_guard_tc, radiant_dom_m4c_get_selectionEnd, radiant_dom_m4c_selection_end_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selection_direction", "selectionDirection", NULL, radiant_dom_guard_tc, radiant_dom_m4c_get_selectionDirection, radiant_dom_m4c_selection_direction_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"default_value", "defaultValue", NULL, radiant_dom_guard_tc, radiant_dom_m4c_get_defaultValue, radiant_dom_m4c_default_value_set, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"options", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4c_get_options, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"selected_options", "selectedOptions", NULL, radiant_dom_guard_select, radiant_dom_m4c_get_selectedOptions, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"type", NULL, NULL, radiant_dom_guard_select, radiant_dom_m4c_get_type, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"index", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_index, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"label", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_label, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"form", NULL, NULL, radiant_dom_guard_option, radiant_dom_m4c_get_form, NULL, NULL, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"named_item", "namedItem", NULL, radiant_dom_guard_select, NULL, NULL, radiant_dom_m4d_named_item, NULL, 0},
    {"add", NULL, NULL, radiant_dom_guard_select, NULL, NULL, radiant_dom_m4d_add, NULL, 0},
    {"remove", NULL, NULL, radiant_dom_guard_select, NULL, NULL, radiant_dom_m4d_remove, NULL, 0},
    {"contains", NULL, NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_contains, NULL, 0},
    {"compare_document_position", "compareDocumentPosition", NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_compare_document_position, NULL, 0},
    {"remove", NULL, NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_remove2, NULL, 0},
    {"replace_with", "replaceWith", NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_replace_with, NULL, 0},
    {"has_child_nodes", "hasChildNodes", NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_has_child_nodes, NULL, 0},
    {"clone_node", "cloneNode", NULL, radiant_dom_guard_node, NULL, NULL, radiant_dom_m4d_clone_node, NULL, 0},
    {"replace_data", "replaceData", NULL, radiant_dom_guard_text, NULL, NULL, radiant_dom_m4d_replace_data, NULL, 0},
    {"insert_data", "insertData", NULL, radiant_dom_guard_text, NULL, NULL, radiant_dom_m4d_insert_data, NULL, 0},
    {"append_data", "appendData", NULL, radiant_dom_guard_text, NULL, NULL, radiant_dom_m4d_append_data, NULL, 0},
    {"delete_data", "deleteData", NULL, radiant_dom_guard_text, NULL, NULL, radiant_dom_m4d_delete_data, NULL, 0},
    {"substring_data", "substringData", NULL, radiant_dom_guard_text, NULL, NULL, radiant_dom_m4d_substring_data, NULL, 0},
    {"get_attribute", "getAttribute", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_attribute, NULL, 0},
    {"set_attribute", "setAttribute", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_set_attribute, NULL, 0},
    {"remove_attribute", "removeAttribute", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_remove_attribute, NULL, 0},
    {"toggle_attribute", "toggleAttribute", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_toggle_attribute, NULL, 0},
    {"has_attribute", "hasAttribute", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_has_attribute, NULL, 0},
    {"get_attribute_names", "getAttributeNames", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_attribute_names, NULL, 0},
    {"matches", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_matches, NULL, 0},
    {"query_selector", "querySelector", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_query_selector, NULL, 0},
    {"query_selector_all", "querySelectorAll", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_query_selector_all, NULL, 0},
    {"closest", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_closest, NULL, 0},
    {"get_elements_by_tag_name", "getElementsByTagName", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_elements_by_tag_name, NULL, 0},
    {"get_elements_by_class_name", "getElementsByClassName", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_elements_by_class_name, NULL, 0},
    {"get_element_by_id", "getElementById", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_element_by_id, NULL, 0},
    {"add_event_listener", "addEventListener", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_add_event_listener, NULL, 0},
    {"remove_event_listener", "removeEventListener", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_remove_event_listener, NULL, 0},
    {"dispatch_event", "dispatchEvent", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_dispatch_event, NULL, 0},
    {"append_child", "appendChild", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_append_child, NULL, 0},
    {"remove_child", "removeChild", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_remove_child, NULL, 0},
    {"insert_before", "insertBefore", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_insert_before, NULL, 0},
    {"replace_child", "replaceChild", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_replace_child, NULL, 0},
    {"normalize", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_normalize, NULL, 0},
    {"append", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_append, NULL, 0},
    {"prepend", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_prepend, NULL, 0},
    {"insert_adjacent_element", "insertAdjacentElement", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_insert_adjacent_element, NULL, 0},
    {"insert_adjacent_html", "insertAdjacentHTML", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_insert_adjacent_html, NULL, 0},
    {"get_bounding_client_rect", "getBoundingClientRect", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_bounding_client_rect, NULL, 0},
    {"get_client_rects", "getClientRects", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_get_client_rects, NULL, 0},
    {"scroll_into_view", "scrollIntoView", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_scroll_into_view, NULL, 0},
    {"scroll", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_scroll, NULL, 0},
    {"scroll_to", "scrollTo", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_scroll_to, NULL, 0},
    {"scroll_by", "scrollBy", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_scroll_by, NULL, 0},
    {"focus", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_focus, NULL, 0},
    {"blur", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_blur, NULL, 0},
    {"click", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_click, NULL, 0},
    {"reset", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_reset, NULL, 0},
    {"submit", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_submit, NULL, 0},
    {"request_submit", "requestSubmit", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_request_submit, NULL, 0},
    {"check_validity", "checkValidity", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_check_validity, NULL, 0},
    {"report_validity", "reportValidity", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_report_validity, NULL, 0},
    {"set_custom_validity", "setCustomValidity", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_set_custom_validity, NULL, 0},
    {"set_selection_range", "setSelectionRange", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_set_selection_range, NULL, 0},
    {"set_range_text", "setRangeText", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_set_range_text, NULL, 0},
    {"select", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_select, NULL, 0},
    {"item", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_item, NULL, 0},
    {"toggle", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_toggle, NULL, 0},
    {"replace", NULL, NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_replace, NULL, 0},
    {"attach_shadow", "attachShadow", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_attach_shadow, NULL, 0},
    {"to_string", "toString", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d_to_string, NULL, 0},
    {"__lambda_boundary_from_point", "__lambdaBoundaryFromPoint", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d___lambda_boundary_from_point, NULL, 0},
    {"__lambda_text_control_boundary_from_point", "__lambdaTextControlBoundaryFromPoint", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d___lambda_text_control_boundary_from_point, NULL, 0},
    {"__lambda_text_control_caret_bounds", "__lambdaTextControlCaretBounds", NULL, radiant_dom_member_is_element, NULL, NULL, radiant_dom_m4d___lambda_text_control_caret_bounds, NULL, 0},
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
    {"stylesheet", NULL, radiant_stylesheet_members,
     (int32_t)(sizeof(radiant_stylesheet_members) / sizeof(radiant_stylesheet_members[0])),
     NULL, cssom_swallow_set, sh_indexed_get, NULL, radiant_style_no_prototype, NULL},
    {"css_rule", NULL, radiant_css_rule_members,
     (int32_t)(sizeof(radiant_css_rule_members) / sizeof(radiant_css_rule_members[0])),
     NULL, cssom_swallow_set, NULL, NULL, radiant_style_no_prototype, NULL},
    {"rule_style_decl", NULL, radiant_rule_decl_members,
     (int32_t)(sizeof(radiant_rule_decl_members) / sizeof(radiant_rule_decl_members[0])),
     rd_named_get, rd_named_set, NULL, NULL, radiant_style_no_prototype, rd_named_has},
    {"dom_node", NULL, radiant_dom_node_members,
     (int32_t)(sizeof(radiant_dom_node_members) / sizeof(radiant_dom_node_members[0])),
     NULL, NULL, NULL, NULL, NULL, NULL, &radiant_dom_node_host_ops},
};

extern const int32_t radiant_dom_type_binding_count;
const int32_t radiant_dom_type_binding_count =
    (int32_t)(sizeof(radiant_dom_type_bindings) / sizeof(radiant_dom_type_bindings[0]));
