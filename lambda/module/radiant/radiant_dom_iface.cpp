// radiant module — DOM3 declared interface for Range and Selection.
//
// Shape lives in radiant_dom_interface_decl (Lambda type syntax, parsed by the
// registry at registration); behavior is the binding tables below, thin
// adapters onto the receiver-explicit engine entries in the Jube host API.
// The strcmp dispatch chains, is-native-property predicates, method caches,
// and expando side tables these replace are deleted from dom_selection.cpp.

#include "../../lambda.hpp"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../jube/jube.h"
#include "../../input/css/css_declaration_attributes.h"
#include "../../../lib/log.h"

extern const JubeHostAPI* radiant_host_api;
extern "C" const void* radiant_dom_range_host_type(void);
extern "C" const void* radiant_dom_selection_host_type(void);
extern "C" Item dom_realm_constructor_prototype(const char* ctor_name);

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
    "type stylesheet {\n"
    "    css_rules: any,\n"
    "    rules: any,\n"
    "    length: int,\n"
    "    disabled: bool,\n"
    "    'type': string,\n"
    "    href: string,\n"
    "    title: string,\n"
    "    owner_node: any,\n"
    "    owner_rule: any,\n"
    "    parent_style_sheet: any,\n"
    "    insert_rule: fn(text: string, index: int) int,\n"
    "    delete_rule: fn(index: int) null\n"
    "}\n"
    "type css_rule { css_text: string, 'type': int, parent_rule: any, parent_style_sheet: any }\n"
    "type css_grouping_rule : css_rule { css_rules: any, insert_rule: fn(text: string, index: int) int, delete_rule: fn(index: int) null }\n"
    "type css_condition_rule : css_grouping_rule { condition_text: string }\n"
#define CSS_RULE_SHAPE_empty ""
#define CSS_RULE_SHAPE_style "selector_text: string, style: any"
#define CSS_RULE_SHAPE_declaration "style: any"
#define CSS_RULE_SHAPE_scope "start: any, end: any"
#define CSS_RULE_SHAPE_property "name: any, syntax: any, inherits: any, initial_value: any"
#define CSS_RULE_SHAPE_namespace "prefix: string, namespace_uri: string"
#define CSS_RULE_INTERFACE(kind, name, base, legacy, host, host_base, shape) \
    "type " #host " : " #host_base " { " CSS_RULE_SHAPE_##shape " }\n"
#define CSS_RULE_INTERFACE_ALIAS(...)
#include "../../input/css/css_rule_interfaces.def"
#undef CSS_RULE_INTERFACE_ALIAS
#undef CSS_RULE_INTERFACE
#undef CSS_RULE_SHAPE_empty
#undef CSS_RULE_SHAPE_style
#undef CSS_RULE_SHAPE_declaration
#undef CSS_RULE_SHAPE_scope
#undef CSS_RULE_SHAPE_property
#undef CSS_RULE_SHAPE_namespace
    "type dom_node {\n"
    "    node_name: string, node_type: int, base_uri: string,\n"
    "    parent_node: dom_node, parent_element: dom_node, is_connected: bool,\n"
    "    owner_document: document, first_child: dom_node, last_child: dom_node,\n"
    "    next_sibling: dom_node, previous_sibling: dom_node, child_nodes: any,\n"
    "    contains: fn(a0: any) any, is_equal_node: fn(a0: any) any,\n"
    "    is_same_node: fn(a0: any) any, compare_document_position: fn(a0: any) any,\n"
    "    get_root_node: fn(a0: any) any, remove: fn() any,\n"
    "    replace_with: fn(a0: any) any, after: fn(a0: any) any, before: fn(a0: any) any,\n"
    "    has_child_nodes: fn() any, clone_node: fn(a0: any) any,\n"
    "    add_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    remove_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    dispatch_event: fn(a0: any) any\n"
    "}\n"
    "type event {\n"
    "    'type': string, target: any, current_target: any, src_element: any,\n"
    "    bubbles: bool, cancelable: bool, composed: bool,\n"
    "    default_prevented: bool, event_phase: int, is_trusted: bool,\n"
    "    time_stamp: float, return_value: bool, cancel_bubble: bool,\n"
    "    prevent_default: fn() null, stop_propagation: fn() null,\n"
    "    stop_immediate_propagation: fn() null, composed_path: fn() any,\n"
    "    init_event: fn(type: string, bubbles: bool, cancelable: bool) null\n"
    "}\n"
    "type html_element : dom_node {\n"
    "    tag_name: string, local_name: string, namespace_uri: string, prefix: any,\n"
    "    id: string, class_name: string, child_element_count: int, children: any,\n"
    "    attributes: any, first_element_child: dom_node, last_element_child: dom_node,\n"
    "    inner_html: string, dataset: any,\n"
    "    next_element_sibling: dom_node, previous_element_sibling: dom_node,\n"
    "    disabled: bool, required: bool,\n"
    "    no_validate: bool, form_no_validate: bool, open: bool, autofocus: bool,\n"
    "    max_length: int, min_length: int, src: string, href: string,\n"
    "    protocol: string, host: string, hostname: string, pathname: string,\n"
    "    search: string, hash: string, origin: string, alt: string, name: string,\n"
    "    placeholder: string, autocomplete: string, html_for: string, target: string,\n"
    "    accept_charset: string, form_target: string, input_mode: string,\n"
    "    enter_key_hint: string, content_editable: string, is_content_editable: bool,\n"
    "    get_attribute: fn(a0: any) any, set_attribute: fn(a0: any, a1: any) any,\n"
    "    get_attribute_node: fn(name: any) any, get_attribute_node_ns: fn(ns: any, name: any) any,\n"
    "    set_attribute_node: fn(attr: any) any, set_attribute_node_ns: fn(attr: any) any,\n"
    "    remove_attribute_node: fn(attr: any) any,\n"
    "    set_attribute_ns: fn(a0: any, a1: any, a2: any) any,\n"
    "    get_attribute_ns: fn(a0: any, a1: any) any,\n"
    "    remove_attribute_ns: fn(a0: any, a1: any) any, remove_attribute: fn(a0: any) any,\n"
    "    toggle_attribute: fn(a0: any, a1: any) any, has_attribute: fn(a0: any) any,\n"
    "    set_pointer_capture: fn(id: any) any, release_pointer_capture: fn(id: any) any, has_pointer_capture: fn(id: any) bool,\n"
    "    get_attribute_names: fn() any, matches: fn(a0: any) any,\n"
    "    webkit_matches_selector: fn(a0: any) any, ms_matches_selector: fn(a0: any) any,\n"
    "    query_selector: fn(a0: any) any, query_selector_all: fn(a0: any) any,\n"
    "    closest: fn(a0: any) any, get_elements_by_tag_name: fn(a0: any) any,\n"
    "    get_elements_by_class_name: fn(a0: any) any, get_element_by_id: fn(a0: any) any,\n"
    "    append_child: fn(a0: any) any, remove_child: fn(a0: any) any,\n"
    "    insert_before: fn(a0: any, a1: any) any, replace_child: fn(a0: any, a1: any) any,\n"
    "    normalize: fn() any, append: fn(a0: any) any, prepend: fn(a0: any) any,\n"
    "    insert_adjacent_element: fn(a0: any, a1: any) any,\n"
    "    insert_adjacent_html: fn(a0: any, a1: any) any,\n"
    "    insert_adjacent_text: fn(a0: any, a1: any) any,\n"
    "    get_bounding_client_rect: fn() any, get_client_rects: fn() any,\n"
    "    scroll_into_view: fn(a0: any) any,\n"
    "    scroll_into_view_if_needed: fn(a0: any) any,\n"
    "    scroll: fn(a0: any, a1: any) any,\n"
    "    scroll_to: fn(a0: any, a1: any) any, scroll_by: fn(a0: any, a1: any) any,\n"
    "    focus: fn() any, blur: fn() any, click: fn() any,\n"
    "    show_popover: fn() any, hide_popover: fn() any, show_modal: fn() any,\n"
    "    reset: fn() any,\n"
    "    submit: fn() any, request_submit: fn(a0: any) any, check_validity: fn() any,\n"
    "    report_validity: fn() any, set_custom_validity: fn(a0: any) any,\n"
    "    set_selection_range: fn(a0: any, a1: any, a2: any) any,\n"
    "    set_range_text: fn(a0: any, a1: any, a2: any, a3: any) any,\n"
    "    select: fn() any, item: fn(a0: any) any, toggle: fn(a0: any, a1: any) any,\n"
    "    replace: fn(a0: any, a1: any) any, attach_shadow: fn(a0: any) any,\n"
    "    to_string: fn() any,\n"
    "    __lambda_boundary_from_point: fn(a0: any, a1: any, a2: any) any,\n"
    "    __lambda_text_control_boundary_from_point: fn(a0: any, a1: any) any,\n"
    "    __lambda_text_control_caret_bounds: fn() any\n"
    "}\n"
    "type character_data : dom_node {\n"
    "    data: string, node_value: string, text_content: string,\n"
    "    replace_data: fn(a0: any, a1: any, a2: any) any,\n"
    "    insert_data: fn(a0: any, a1: any) any, append_data: fn(a0: any) any,\n"
    "    delete_data: fn(a0: any, a1: any) any, substring_data: fn(a0: any, a1: any) any,\n"
    "    split_text: fn(a0: any) any\n"
    "}\n"
    "type attr : dom_node {\n"
    "    name: string, local_name: string, namespace_uri: any, prefix: any,\n"
    "    owner_element: any, specified: bool, value: string, node_value: string, text_content: string\n"
    "}\n"
    "type svg_element : html_element {\n"
    "    dataset: any,\n"
    "    create_svg_point: fn() any, create_svg_matrix: fn() any,\n"
    "    create_svg_transform: fn() any,\n"
    "    create_svg_transform_from_matrix: fn(a0: any) any, get_bbox: fn() any,\n"
    "    get_ctm: fn() any, get_screen_ctm: fn() any,\n"
    "    pause_animations: fn() any, unpause_animations: fn() any,\n"
    "    animations_paused: fn() bool, get_current_time: fn() float,\n"
    "    target_element: any, get_start_time: fn() float, get_simple_duration: fn() float,\n"
    "    set_current_time: fn(a0: any) any,\n"
    "    begin_element: fn() any, begin_element_at: fn(a0: any) any,\n"
    "    end_element: fn() any, end_element_at: fn(a0: any) any\n"
    "}\n"
    "type input_element : html_element {\n"
    "    read_only: bool, readonly: bool,\n"
    "    default_checked: bool, size: int, width: int, height: int,\n"
    "    multiple: bool, checked: bool, 'type': string, value: string,\n"
    "    value_as_number: float, value_as_date: any, files: any,\n"
    "    pattern: string, min: string, max: string, step: string, accept: string,\n"
    "    selection_start: int, selection_end: int, selection_direction: string,\n"
    "    default_value: string,\n"
    "    step_up: fn(a0: any) any, step_down: fn(a0: any) any\n"
    "}\n"
    "type select_element : html_element {\n"
    "    multiple: bool, size: int, value: string, selected_index: int, length: int,\n"
    "    options: any, selected_options: any, 'type': string,\n"
    "    named_item: fn(a0: any) any, add: fn(a0: any, a1: any) any, remove: fn(a0: any) any,\n"
    "    show_picker: fn() any\n"
    "}\n"
    "type textarea_element : html_element {\n"
    "    read_only: bool, readonly: bool,\n"
    "    rows: int, cols: int, wrap: string, value: string, selection_start: string,\n"
    "    selection_end: string, selection_direction: string, default_value: string\n"
    "}\n"
    "type option_element : html_element {\n"
    "    default_selected: bool, value: string, selected: bool, text: string,\n"
    "    index: int, label: string, form: any\n"
    "}\n"
    "type rule_style_decl {\n"
    "    length: int,\n"
    "    css_text: string,\n"
    "    parent_rule: any,\n"
    "    get_property_value: fn(prop: string) string,\n"
    "    get_property_priority: fn(prop: string) string,\n"
    "    item: fn(index: int) string,\n"
    "    set_property: fn(prop: string, value: string, priority: string) null,\n"
    "    remove_property: fn(prop: string) string\n"
    "}\n"
#define CSS_DECLARATION_FIELD(field) "    '" #field "': string,\n"
#define CSS_DECLARATION_INTERFACE(kind, name, host, metadata) \
    "type " #host " : rule_style_decl {\n" metadata##_FIELDS(CSS_DECLARATION_FIELD) "}\n"
#include "../../input/css/css_declaration_interfaces.def"
#undef CSS_DECLARATION_INTERFACE
#undef CSS_DECLARATION_FIELD
    "type inline_style : css_style_properties {}\n"
    "type computed_style : css_style_properties {}\n"
    "type document {\n"
    "    document_element: dom_node,\n"
    "    body: dom_node,\n"
    "    head: dom_node,\n"
    "    title: string,\n"
    "    cookie: string,\n"
    "    url: string,\n"
    "    href: string,\n"
    "    protocol: string,\n"
    "    hostname: string,\n"
    "    port: string,\n"
    "    pathname: string,\n"
    "    search: string,\n"
    "    hash: string,\n"
    "    host: string,\n"
    "    origin: string,\n"
    "    location: any,\n"
    "    document: any,\n"
    "    to_string: fn() string,\n"
    "    ready_state: string,\n"
    "    current_script: any,\n"
    "    fonts: any,\n"
    "    compat_mode: string,\n"
    "    character_set: string,\n"
    "    charset: string,\n"
    "    content_type: string,\n"
    "    node_type: int,\n"
    "    node_name: string,\n"
    "    owner_document: any,\n"
    "    child_nodes: any,\n"
    "    doctype: any,\n"
    "    style_sheets: any,\n"
    "    default_view: any,\n"
    "    implementation: any,\n"
    "    design_mode: string,\n"
    "    active_element: any,\n"
    "    forms: any,\n"
    "    assign: fn(a0: any) any,\n"
    "    replace: fn(a0: any) any,\n"
    "    reload: fn() any,\n"
    "    focus: fn() any,\n"
    "    blur: fn() any,\n"
    "    has_focus: fn() bool,\n"
    "    open: fn() any,\n"
    "    close: fn() any,\n"
    "    write: fn(a0: any) any,\n"
    "    writeln: fn(a0: any) any,\n"
    "    element_from_point: fn(a0: any, a1: any) any,\n"
    "    create_range: fn() range,\n"
    "    get_selection: fn() selection,\n"
    "    get_element_by_id: fn(a0: any) dom_node,\n"
    "    get_elements_by_class_name: fn(a0: any) any,\n"
    "    get_elements_by_tag_name: fn(a0: any) any,\n"
    "    get_elements_by_name: fn(a0: any) any,\n"
    "    query_selector: fn(a0: any) dom_node,\n"
    "    query_selector_all: fn(a0: any) any,\n"
    "    create_element: fn(a0: any) dom_node,\n"
    "    create_element_ns: fn(a0: any, a1: any) any,\n"
    "    create_attribute: fn(name: any) any, create_attribute_ns: fn(ns: any, name: any) any,\n"
    "    create_text_node: fn(a0: any) dom_node,\n"
    "    create_document_fragment: fn() dom_node,\n"
    "    create_comment: fn(a0: any) dom_node,\n"
    "    create_processing_instruction: fn(a0: any, a1: any) any,\n"
    "    import_node: fn(a0: any, a1: any) any,\n"
    "    normalize: fn() any,\n"
    "    adopt_node: fn(a0: any) any,\n"
    "    append_child: fn(a0: any) any,\n"
    "    contains: fn(a0: any) any,\n"
    "    compare_document_position: fn(a0: any) any,\n"
    "    get_root_node: fn(a0: any) any,\n"
    "    clone_node: fn(a0: any) any,\n"
    "    add_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    remove_event_listener: fn(a0: any, a1: any, a2: any) any,\n"
    "    dispatch_event: fn(a0: any) any,\n"
    "    create_tree_walker: fn(a0: any, a1: any) any,\n"
    "    create_event: fn(a0: any) any,\n"
    "    exec_command: fn(a0: any, a1: any, a2: any) bool,\n"
    "    query_command_supported: fn(a0: any) bool,\n"
    "    query_command_enabled: fn(a0: any) bool,\n"
    "    query_command_state: fn(a0: any) bool,\n"
    "    query_command_indeterm: fn(a0: any) bool,\n"
    "    query_command_value: fn(a0: any) string\n"
    "}\n"
    "type foreign_document : document {\n"
    "}\n"
    "type velmt {\n"
    "    index: int,\n"
    "    tag: string,\n"
    "    id: any,\n"
    "    width: float,\n"
    "    height: float,\n"
    "    wd: float,\n"
    "    hg: float,\n"
    "    box: map,\n"
    "    children: array,\n"
    "    text: string,\n"
    "    style: map,\n"
    "    margin: map,\n"
    "    border: map,\n"
    "    padding: map,\n"
    "    attrs: map\n"
    "}\n"
    "type node_list {\n"
    "    length: int,\n"
    "    item: fn(index: int) any\n"
    "}\n"
    // declared bases preserve captured-accessor brands across collection subtypes.
    "type radio_node_list : node_list {}\n"
    "type dom_rect_list {\n"
    "    length: int,\n"
    "    item: fn(index: int) any\n"
    "}\n"
    "type style_sheet_list {\n"
    "    length: int,\n"
    "    item: fn(index: int) any\n"
    "}\n"
    "type css_rule_list {\n"
    "    length: int,\n"
    "    item: fn(index: int) any\n"
    "}\n"
    "type html_collection {\n"
    "    length: int,\n"
    "    item: fn(index: int) any,\n"
    "    named_item: fn(name: string) any\n"
    "}\n"
    "type html_options_collection : html_collection {\n"
    "    length: int,\n"
    "    selected_index: int,\n"
    "    item: fn(index: int) any,\n"
    "    named_item: fn(name: string) any,\n"
    "    add: fn(element: any, before: any) any\n"
    "}\n"
    "type html_form_controls_collection : html_collection {\n"
    "    named_item: fn(name: string) any\n"
    "}\n"
    "type named_node_map {\n"
    "    length: int,\n"
    "    item: fn(index: int) any,\n"
    "    get_named_item: fn(name: string) any, get_named_item_ns: fn(ns: any, name: string) any,\n"
    "    set_named_item: fn(attr: any) any, set_named_item_ns: fn(attr: any) any,\n"
    "    remove_named_item: fn(name: string) any, remove_named_item_ns: fn(ns: any, name: string) any\n"
    "}\n"
    "type dom_token_list {\n"
    "    length: int, value: string,\n"
    "    item: fn(index: int) any, add: fn(token: string) any,\n"
    "    remove: fn(token: string) any, toggle: fn(token: string, force: any) any,\n"
    "    contains: fn(token: string) bool, replace: fn(old: string, next: string) bool,\n"
    "    supports: fn(token: string) bool,\n"
    "    to_string: fn() string\n"
    "}\n";

// ---- adapters: JubeMemberBind handler shape -> host API behavior entries ----

static Item radiant_iface_arg(Item* args, int argc, int i) {
    return i < argc ? args[i] : (Item){.item = ITEM_JS_UNDEFINED};
}

#define RADIANT_GETTER(name, entry)                                          \
    static int name(Item receiver, Item* out) {                              \
        *out = radiant_host_api->dom_catalog->entry(receiver);                       \
        return 1;                                                            \
    }

#define RADIANT_METHOD_0(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        (void)args; (void)argc;                                              \
        *out = radiant_host_api->dom_catalog->entry(receiver);                       \
        return 1;                                                            \
    }

#define RADIANT_METHOD_1(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom_catalog->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0));                               \
        return 1;                                                            \
    }

#define RADIANT_METHOD_2(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom_catalog->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1)); \
        return 1;                                                            \
    }

#define RADIANT_METHOD_3(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom_catalog->entry(receiver,                        \
            radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1), \
            radiant_iface_arg(args, argc, 2));                               \
        return 1;                                                            \
    }

#define RADIANT_METHOD_4(name, entry)                                        \
    static int name(Item receiver, Item* args, int argc, Item* out) {        \
        *out = radiant_host_api->dom_catalog->entry(receiver,                        \
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
    return radiant_host_api->realm->range_get_prototype_value();
}

static Item radiant_selection_prototype_seed(void) {
    return radiant_host_api->realm->selection_get_prototype_value();
}

#define BIND_READ(n, fn, flags) {n, NULL, fn, NULL, NULL, NULL, flags}
#define BIND_GET(n, fn) BIND_READ(n, fn, 0)
#define BIND_GET_HIDDEN(n, fn) \
    BIND_READ(n, fn, JUBE_MEMBER_NON_ENUMERABLE)
// publish attributes on their declaring interface's native prototype.
#define BIND_GET_PROTO(n, fn) BIND_READ(n, fn, JUBE_MEMBER_PROTOTYPE)
#define BIND_CALL(n, fn)     {n, NULL, NULL, NULL, fn, NULL, 0}
#define BIND_CALL_JS(n, js, fn) {n, js, NULL, NULL, fn, NULL, 0}

// ES24/F17: all declared Event fields project one native record. The binding
// owns only spelling adaptation; record semantics stay in the Radiant bridge
// so JS and Lambda reach the same cancellation and propagation state.
#define RADIANT_EVENT_GETTER(fn_name, member_name)                           \
    static int fn_name(Item receiver, Item* out) {                            \
        return radiant_dom_event_member_get(receiver, member_name, out);      \
    }
#define RADIANT_EVENT_SETTER(fn_name, member_name)                           \
    static int fn_name(Item receiver, Item value, Item* out) {                \
        return radiant_dom_event_member_set(receiver, member_name, value, out); \
    }
#define RADIANT_EVENT_CALL(fn_name, member_name)                             \
    static int fn_name(Item receiver, Item* args, int argc, Item* out) {      \
        return radiant_dom_event_call(receiver, member_name, args, argc, out); \
    }

RADIANT_EVENT_GETTER(e_type_get, "type")
RADIANT_EVENT_SETTER(e_type_set, "type")
RADIANT_EVENT_GETTER(e_target_get, "target")
RADIANT_EVENT_SETTER(e_target_set, "target")
RADIANT_EVENT_GETTER(e_current_target_get, "currentTarget")
RADIANT_EVENT_SETTER(e_current_target_set, "currentTarget")
RADIANT_EVENT_GETTER(e_src_element_get, "srcElement")
RADIANT_EVENT_SETTER(e_src_element_set, "srcElement")
RADIANT_EVENT_GETTER(e_bubbles_get, "bubbles")
RADIANT_EVENT_SETTER(e_bubbles_set, "bubbles")
RADIANT_EVENT_GETTER(e_cancelable_get, "cancelable")
RADIANT_EVENT_SETTER(e_cancelable_set, "cancelable")
RADIANT_EVENT_GETTER(e_composed_get, "composed")
RADIANT_EVENT_SETTER(e_composed_set, "composed")
RADIANT_EVENT_GETTER(e_default_prevented_get, "defaultPrevented")
RADIANT_EVENT_SETTER(e_default_prevented_set, "defaultPrevented")
RADIANT_EVENT_GETTER(e_event_phase_get, "eventPhase")
RADIANT_EVENT_SETTER(e_event_phase_set, "eventPhase")
RADIANT_EVENT_GETTER(e_is_trusted_get, "isTrusted")
RADIANT_EVENT_SETTER(e_is_trusted_set, "isTrusted")
RADIANT_EVENT_GETTER(e_time_stamp_get, "timeStamp")
RADIANT_EVENT_SETTER(e_time_stamp_set, "timeStamp")
RADIANT_EVENT_GETTER(e_return_value_get, "returnValue")
RADIANT_EVENT_SETTER(e_return_value_set, "returnValue")
RADIANT_EVENT_GETTER(e_cancel_bubble_get, "cancelBubble")
RADIANT_EVENT_SETTER(e_cancel_bubble_set, "cancelBubble")
RADIANT_EVENT_CALL(e_prevent_default, "preventDefault")
RADIANT_EVENT_CALL(e_stop_propagation, "stopPropagation")
RADIANT_EVENT_CALL(e_stop_immediate_propagation, "stopImmediatePropagation")
RADIANT_EVENT_CALL(e_composed_path, "composedPath")
RADIANT_EVENT_CALL(e_init_event, "initEvent")

static const JubeMemberBind radiant_event_members[] = {
    {"type", NULL, e_type_get, e_type_set, NULL, NULL, 0},
    {"target", NULL, e_target_get, e_target_set, NULL, NULL, 0},
    {"current_target", NULL, e_current_target_get, e_current_target_set, NULL, NULL, 0},
    {"src_element", NULL, e_src_element_get, e_src_element_set, NULL, NULL, 0},
    {"bubbles", NULL, e_bubbles_get, e_bubbles_set, NULL, NULL, 0},
    {"cancelable", NULL, e_cancelable_get, e_cancelable_set, NULL, NULL, 0},
    {"composed", NULL, e_composed_get, e_composed_set, NULL, NULL, 0},
    {"default_prevented", NULL, e_default_prevented_get, e_default_prevented_set, NULL, NULL, 0},
    {"event_phase", NULL, e_event_phase_get, e_event_phase_set, NULL, NULL, 0},
    {"is_trusted", NULL, e_is_trusted_get, e_is_trusted_set, NULL, NULL, 0},
    {"time_stamp", NULL, e_time_stamp_get, e_time_stamp_set, NULL, NULL, 0},
    {"return_value", NULL, e_return_value_get, e_return_value_set, NULL, NULL, 0},
    {"cancel_bubble", NULL, e_cancel_bubble_get, e_cancel_bubble_set, NULL, NULL, 0},
    BIND_CALL("prevent_default", e_prevent_default),
    BIND_CALL("stop_propagation", e_stop_propagation),
    BIND_CALL("stop_immediate_propagation", e_stop_immediate_propagation),
    BIND_CALL("composed_path", e_composed_path),
    BIND_CALL("init_event", e_init_event),
};

static const JubeMemberBind radiant_range_members[] = {
    BIND_GET("start_container", r_start_container),
    BIND_GET("start_offset", r_start_offset),
    BIND_GET("end_container", r_end_container),
    BIND_GET("end_offset", r_end_offset),
    BIND_GET("collapsed", r_collapsed),
    BIND_GET_PROTO("common_ancestor_container", r_common_ancestor),
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
    BIND_GET_PROTO("anchor_node", s_anchor_node),
    BIND_GET_PROTO("anchor_offset", s_anchor_offset),
    BIND_GET_PROTO("focus_node", s_focus_node),
    BIND_GET_PROTO("focus_offset", s_focus_offset),
    BIND_GET_PROTO("is_collapsed", s_is_collapsed),
    BIND_GET_PROTO("range_count", s_range_count),
    BIND_GET_PROTO("type", s_type),
    BIND_GET_PROTO("direction", s_direction),
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


static Item radiant_style_key(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

// ---- CSSOM (stylesheet / css_rule / rule_style_decl) ----
// stylesheet and rule open names use the shared host expando store; native
// members and receiver-specific rule prototypes remain host-API projections.

#define RADIANT_GETTER_D(name, entry)                                        \
    static int name(Item receiver, Item* out) {                              \
        *out = radiant_host_api->dom_catalog->entry(receiver);                       \
        return 1;                                                            \
    }

RADIANT_GETTER_D(sh_css_rules, stylesheet_rules)
RADIANT_GETTER_D(sh_length, stylesheet_get_length)
RADIANT_GETTER_D(sh_disabled, stylesheet_get_disabled)
RADIANT_GETTER_D(sh_owner_node, stylesheet_get_owner_node)
RADIANT_GETTER_D(sh_owner_rule, stylesheet_get_owner_rule)
RADIANT_GETTER_D(sh_parent_style_sheet, stylesheet_get_parent_style_sheet)
RADIANT_GETTER_D(sh_type, stylesheet_get_type)
RADIANT_GETTER_D(sh_href, stylesheet_get_href)
RADIANT_GETTER_D(sh_title, stylesheet_get_title)

static int sh_indexed_get(Item receiver, int64_t index, Item* out) {
    *out = radiant_host_api->dom_catalog->stylesheet_index(receiver, index);
    return 1;
}

static int cssom_insert_rule(Item receiver, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom_catalog->stylesheet_insert_rule(receiver,
        radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1));
    return 1;
}

static int cssom_delete_rule(Item receiver, Item* args, int argc, Item* out) {
    *out = radiant_host_api->dom_catalog->stylesheet_delete_rule(receiver,
        radiant_iface_arg(args, argc, 0));
    return 1;
}

RADIANT_GETTER_D(cr_selector_text, rule_get_selector_text)
RADIANT_GETTER_D(cr_style, rule_get_style)
RADIANT_GETTER_D(cr_css_rules, rule_get_css_rules)
RADIANT_GETTER_D(cr_css_text, rule_get_css_text)
RADIANT_GETTER_D(cr_type, rule_get_type)
RADIANT_GETTER_D(cr_parent_rule, rule_get_parent_rule)
RADIANT_GETTER_D(cr_parent_style_sheet, rule_get_parent_style_sheet)
RADIANT_GETTER_D(cr_scope_start, rule_get_scope_start)
RADIANT_GETTER_D(cr_scope_end, rule_get_scope_end)
RADIANT_GETTER_D(cr_property_name, rule_get_property_name)
RADIANT_GETTER_D(cr_property_syntax, rule_get_property_syntax)
RADIANT_GETTER_D(cr_property_inherits, rule_get_property_inherits)
RADIANT_GETTER_D(cr_property_initial_value, rule_get_property_initial_value)

RADIANT_GETTER_D(cr_condition_text, rule_get_condition_text)
RADIANT_GETTER_D(cr_namespace_prefix, rule_get_namespace_prefix)
RADIANT_GETTER_D(cr_namespace_uri, rule_get_namespace_uri)

#define CSS_RULE_SETTER(name, entry) \
    static int name(Item receiver, Item value, Item* out) { \
        *out = radiant_host_api->dom_catalog->entry(receiver, value); \
        return 1; \
    }
CSS_RULE_SETTER(cr_style_set, rule_set_style)
CSS_RULE_SETTER(cr_css_text_set, rule_set_css_text)
#undef CSS_RULE_SETTER

static int cr_selector_text_set(Item receiver, Item value, Item* out) {
    *out = radiant_host_api->dom_catalog->set_selector_text(receiver, value);
    return 1;
}

static int sh_disabled_set(Item receiver, Item value, Item* out) {
    *out = radiant_host_api->dom_catalog->stylesheet_set_disabled(receiver, value);
    return 1;
}

// rule declarations: CSS property names are the open-name surface
static int rd_named_get(Item receiver, Item key, Item* out) {
    if (!it2b(radiant_host_api->dom_catalog->rule_style_has_property(receiver, key))) return 0;
    *out = radiant_host_api->dom_catalog->rule_style_get_property(receiver, key);
    return 1;
}

static int rd_named_set(Item receiver, Item key, Item value, Item* out) {
    if (!it2b(radiant_host_api->dom_catalog->rule_style_has_property(receiver, key))) return 0;
    *out = radiant_host_api->dom_catalog->rule_style_set_property(receiver, key, value);
    return 1;
}

static int rd_named_has(Item receiver, Item key, Item* out) {
    *out = radiant_host_api->dom_catalog->rule_style_has_property(receiver, key);
    return it2b(*out) ? 1 : 0;
}

static int rd_member_get(Item receiver, const char* name, Item* out) {
    *out = radiant_host_api->dom_catalog->rule_style_get_property(receiver, radiant_style_key(name));
    return 1;
}

static int rd_length_get(Item receiver, Item* out) { return rd_member_get(receiver, "length", out); }
static int rd_css_text_get(Item receiver, Item* out) { return rd_member_get(receiver, "cssText", out); }
static int rd_parent_rule_get(Item receiver, Item* out) { return rd_member_get(receiver, "parentRule", out); }

static int64_t rd_indexed_length(Item receiver) {
    Item length = ItemNull;
    rd_length_get(receiver, &length);
    return fn_int64_index(length);
}

static int rd_indexed_get(Item receiver, int64_t index, Item* out) {
    *out = radiant_host_api->dom_catalog->rule_style_item(receiver, (Item){.item = i2it(index)});
    return 1;
}

static int rd_css_text_set(Item receiver, Item value, Item* out) {
    *out = radiant_host_api->dom_catalog->rule_style_set_property(receiver, radiant_style_key("cssText"), value);
    return 1;
}

RADIANT_METHOD_1(rd_get_property_value, rule_style_get_value)
RADIANT_METHOD_1(rd_get_property_priority, rule_style_get_priority)
RADIANT_METHOD_1(rd_item, rule_style_item)
RADIANT_METHOD_3(rd_set_property, rule_style_set_value)
RADIANT_METHOD_1(rd_remove_property, rule_style_remove_property)

static const JubeMemberBind radiant_stylesheet_members[] = {
    BIND_GET_HIDDEN("css_rules", sh_css_rules),
    BIND_GET_HIDDEN("rules", sh_css_rules),
    BIND_GET_HIDDEN("length", sh_length),
    {"disabled", NULL, sh_disabled, sh_disabled_set, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE},
    BIND_GET_HIDDEN("type", sh_type),
    BIND_GET_HIDDEN("href", sh_href),
    BIND_GET_HIDDEN("title", sh_title),
    BIND_GET_HIDDEN("owner_node", sh_owner_node),
    BIND_GET_HIDDEN("owner_rule", sh_owner_rule),
    BIND_GET_HIDDEN("parent_style_sheet", sh_parent_style_sheet),
    BIND_CALL("insert_rule", cssom_insert_rule),
    BIND_CALL("delete_rule", cssom_delete_rule),
};

#define CSS_RULE_FIELD(name, get, set) {name, NULL, get, set, NULL, NULL, JUBE_MEMBER_PROTOTYPE}
#define CSSOM_METHOD(name, call, required) \
    {name, NULL, NULL, NULL, call, NULL, JUBE_MEMBER_PROTOTYPE | JUBE_MEMBER_REQUIRED_ARGS(required)}
static const JubeMemberBind radiant_css_rule_members[] = {
    CSS_RULE_FIELD("css_text", cr_css_text, cr_css_text_set),
    CSS_RULE_FIELD("type", cr_type, NULL),
    CSS_RULE_FIELD("parent_rule", cr_parent_rule, NULL),
    CSS_RULE_FIELD("parent_style_sheet", cr_parent_style_sheet, NULL),
};
static const JubeMemberBind css_grouping_members[] = {
    CSS_RULE_FIELD("css_rules", cr_css_rules, NULL),
    CSSOM_METHOD("insert_rule", cssom_insert_rule, 1),
    CSSOM_METHOD("delete_rule", cssom_delete_rule, 1),
};
static const JubeMemberBind css_condition_members[] = {
    CSS_RULE_FIELD("condition_text", cr_condition_text, NULL),
};
static const JubeMemberBind css_style_members[] = {
    CSS_RULE_FIELD("selector_text", cr_selector_text, cr_selector_text_set),
    CSS_RULE_FIELD("style", cr_style, cr_style_set),
};
static const JubeMemberBind css_declaration_members[] = {
    CSS_RULE_FIELD("style", cr_style, cr_style_set),
};
static const JubeMemberBind css_scope_members[] = {
    CSS_RULE_FIELD("start", cr_scope_start, NULL),
    CSS_RULE_FIELD("end", cr_scope_end, NULL),
};
static const JubeMemberBind css_property_members[] = {
    CSS_RULE_FIELD("name", cr_property_name, NULL),
    CSS_RULE_FIELD("syntax", cr_property_syntax, NULL),
    CSS_RULE_FIELD("inherits", cr_property_inherits, NULL),
    CSS_RULE_FIELD("initial_value", cr_property_initial_value, NULL),
};
static const JubeMemberBind css_namespace_members[] = {
    CSS_RULE_FIELD("prefix", cr_namespace_prefix, NULL),
    {"namespace_uri", "namespaceURI", cr_namespace_uri, NULL, NULL, NULL, JUBE_MEMBER_PROTOTYPE},
};
static const JubeMemberBind radiant_rule_decl_members[] = {
    // declaration attributes and methods use the same observable WebIDL path as rules.
    CSS_RULE_FIELD("length", rd_length_get, NULL),
    CSS_RULE_FIELD("css_text", rd_css_text_get, rd_css_text_set),
    CSS_RULE_FIELD("parent_rule", rd_parent_rule_get, NULL),
    CSSOM_METHOD("get_property_value", rd_get_property_value, 1),
    CSSOM_METHOD("get_property_priority", rd_get_property_priority, 1),
    CSSOM_METHOD("item", rd_item, 1),
    CSSOM_METHOD("set_property", rd_set_property, 2),
    CSSOM_METHOD("remove_property", rd_remove_property, 1),
};
// fixed property keys share branded named adapters instead of per-property handlers.
#define CSS_DECLARATION_ATTRIBUTE(field, js_name, css_name) \
    {#field, js_name, NULL, NULL, NULL, css_name, \
        JUBE_MEMBER_PROTOTYPE | JUBE_MEMBER_KEYED_ACCESSOR},
#define CSS_DECLARATION_INTERFACE(kind, name, host, metadata) \
    static const JubeMemberBind host##_members[] = { \
        metadata##_ATTRIBUTES(CSS_DECLARATION_ATTRIBUTE) \
    };
#include "../../input/css/css_declaration_interfaces.def"
#undef CSS_DECLARATION_INTERFACE
#undef CSS_DECLARATION_ATTRIBUTE
#undef CSSOM_METHOD
#undef CSS_RULE_FIELD


// ---- dom_node Phase 4a-4e: identity/navigation + named hooks ----
// The residual open-name/property-object semantics are explicit binding hooks;
// dom_node keeps them on its record-owned binding surface.

extern "C" int radiant_dom_member_data(Item receiver, Item* out);
extern "C" int radiant_dom_member_node_value(Item receiver, Item* out);
extern "C" int radiant_dom_member_text_content(Item receiver, Item* out);
extern "C" int radiant_dom_member_tag_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_node_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_base_uri(Item receiver, Item* out);
extern "C" int radiant_dom_member_local_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_namespace_uri(Item receiver, Item* out);
extern "C" int radiant_dom_member_prefix(Item receiver, Item* out);
extern "C" int radiant_dom_member_id(Item receiver, Item* out);
extern "C" int radiant_dom_member_class_name(Item receiver, Item* out);
extern "C" int radiant_dom_member_node_type_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_parent_node_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_parent_element_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_is_connected_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_child_element_count(Item receiver, Item* out);
extern "C" int radiant_dom_member_children(Item receiver, Item* out);
extern "C" int radiant_dom_member_attributes(Item receiver, Item* out);
extern "C" int radiant_dom_member_owner_document_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_first_child_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_last_child_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_next_sibling_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_previous_sibling_any(Item receiver, Item* out);
extern "C" int radiant_dom_member_first_element_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_last_element_child(Item receiver, Item* out);
extern "C" int radiant_dom_member_next_element_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_previous_element_sibling(Item receiver, Item* out);
extern "C" int radiant_dom_member_child_nodes_any(Item receiver, Item* out);
extern "C" int radiant_dom_guard_dis(Item receiver);
extern "C" int radiant_dom_guard_srct(Item receiver);
extern "C" int radiant_dom_guard_hreft(Item receiver);
extern "C" int radiant_dom_guard_anchor(Item receiver);
extern "C" int radiant_dom_m4b_disabled_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_disabled_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_multiple_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_multiple_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_multiple2_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_multiple2_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_default_checked_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_default_checked_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_default_selected_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_default_selected_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4b_autofocus_get(Item r, Item* out);
extern "C" int radiant_dom_m4b_autofocus_set(Item r, Item v, Item* out);
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
extern "C" int radiant_dom_anchor_protocol_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_host_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_hostname_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_pathname_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_search_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_hash_get(Item r, Item* out);
extern "C" int radiant_dom_anchor_origin_get(Item r, Item* out);
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
extern "C" int radiant_dom_guard_input_typed_value(Item receiver);
// Declared from the same table the bridge defines them from, so a binding
// row cannot name an operation thunk the ordinal table does not define.
#define DOM_ELEMENT_OP(NAME, thunk) \
    extern "C" int radiant_dom_m4d_##thunk(Item r, Item* args, int argc, Item* out);
#include "../../dom/dom_element_ops.def"
extern "C" int radiant_dom_m4d_remove2(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_m4c_get_checked(Item r, Item* out);
extern "C" int radiant_dom_m4c_checked_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_get_value(Item r, Item* out);
extern "C" int radiant_dom_m4c_value_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value2_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value3_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_m4c_value4_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_type_get(Item r, Item* out);
extern "C" int radiant_dom_input_type_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_typed_value_get(Item r, Item* out);
extern "C" int radiant_dom_input_typed_value_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_value_as_number_get(Item r, Item* out);
extern "C" int radiant_dom_input_value_as_number_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_value_as_date_get(Item r, Item* out);
extern "C" int radiant_dom_input_value_as_date_set(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_files_get_member(Item r, Item* out);
extern "C" int radiant_dom_input_files_set_member(Item r, Item v, Item* out);
extern "C" int radiant_dom_input_step_up(Item r, Item* args, int argc, Item* out);
extern "C" int radiant_dom_input_step_down(Item r, Item* args, int argc, Item* out);
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

#define BIND_FIELD(n, fn) \
    {n, NULL, fn, NULL, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_FIELD_JS(n, js, fn) \
    {n, js, fn, NULL, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_FIELD_SET(n, get, set) \
    {n, NULL, get, set, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_FIELD_SET_JS(n, js, get, set) \
    {n, js, get, set, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define BIND_CALL(n, fn) \
    {n, NULL, NULL, NULL, fn, NULL, 0}

static int radiant_node_property(Item receiver, const char* name, Item* out) {
    *out = radiant_host_api->dom_catalog->get_property(receiver, (Item){.item = s2it(heap_create_name(name))});
    return 1;
}

static int radiant_node_property_set(Item receiver, const char* name, Item value, Item* out) {
    *out = radiant_host_api->dom_catalog->set_property(receiver, (Item){.item = s2it(heap_create_name(name))}, value);
    return 1;
}

#define NATIVE_PROPERTY_GET(fn, name) \
    static int fn(Item receiver, Item* out) { return radiant_node_property(receiver, name, out); }
#define NATIVE_PROPERTY_SET(fn, name) \
    static int fn(Item receiver, Item value, Item* out) { return radiant_node_property_set(receiver, name, value, out); }
NATIVE_PROPERTY_GET(radiant_element_inner_html, "innerHTML")
NATIVE_PROPERTY_SET(radiant_element_inner_html_set, "innerHTML")
NATIVE_PROPERTY_GET(radiant_element_dataset, "dataset")
// ---------------------------------------------------------------------------
// DS1: member binds are generated from dom_api.def, not written here. A row
// whose `iface` names this table expands to a BIND_ROW; every other row expands
// to nothing. The per-interface empty macros are what make that selection work
// in the preprocessor -- `iface` is a bare token so it can be pasted.
//
// DOM_ROW_MEMBER picks the member spelling: the row name unless the row states
// a different one, and the JS name unless the row states one.
// ---------------------------------------------------------------------------
#define DOM_ROW_JS_NAME_(js)  ((js)[0] ? (js) : NULL)
#define DOM_ROW_SNAKE_(name, member) ((member)[0] ? (member) : #name)
#define DOM_ROW_MEMBER(name, argc, member, js) \
    {DOM_ROW_SNAKE_(name, member), DOM_ROW_JS_NAME_(js), NULL, NULL, NULL, NULL, 0, \
     JUBE_DOM_ROW_##name, (argc)},

// one empty expansion per interface that is not the table being built
#define DOM_ROW_NONE_(name, argc, member, js)

// DS13: the member's slot is the catalog row itself, named by its index so a
// static table can hold it without linking a host symbol. `argc` counts the
// receiver, matching the catalog's arity. The generated `radiant_dom_m4d_*`
// adapter, the module ordinal dispatcher and the executor arm are all skipped.
#define BIND_ROW(n, js, row, argc) \
    {n, js, NULL, NULL, NULL, NULL, 0, JUBE_DOM_ROW_##row, (argc)}
#define BIND_CALL_JS(n, js, fn) \
    {n, js, NULL, NULL, fn, NULL, 0}

// The reflected-attribute accessors are generated in radiant_dom_bridge.cpp
// from lambda/dom/dom_reflect.def; declare them from the same table so the
// binding rows below cannot name one the table does not define.
#define DOM_REFLECT_BOOL(name, attr, tags) \
    extern "C" int radiant_html_##name##_get(Item r, Item* out); \
    extern "C" int radiant_html_##name##_set(Item r, Item v, Item* out);
#define DOM_REFLECT_INT(name, attr, fallback, tags) \
    extern "C" int radiant_html_##name##_get(Item r, Item* out); \
    extern "C" int radiant_html_##name##_set(Item r, Item v, Item* out);
#define DOM_REFLECT_STR(name, attr, fallback, tags) \
    extern "C" int radiant_html_##name##_get(Item r, Item* out); \
    extern "C" int radiant_html_##name##_set(Item r, Item v, Item* out);
#include "../../dom/dom_reflect.def"
#undef DOM_REFLECT_BOOL
#undef DOM_REFLECT_INT
#undef DOM_REFLECT_STR

#define RADIANT_GUARDED_GET(name, guard, getter) \
    static int name(Item receiver, Item* out) { \
        return guard(receiver) ? getter(receiver, out) : 0; \
    }
#define RADIANT_GUARDED_SET(name, guard, setter) \
    static int name(Item receiver, Item value, Item* out) { \
        return guard(receiver) ? setter(receiver, value, out) : 0; \
    }

RADIANT_GUARDED_GET(radiant_html_disabled_get, radiant_dom_guard_dis,
                   radiant_dom_m4b_disabled_get)
RADIANT_GUARDED_SET(radiant_html_disabled_set, radiant_dom_guard_dis,
                   radiant_dom_m4b_disabled_set)
RADIANT_GUARDED_GET(radiant_html_src_get, radiant_dom_guard_srct,
                   radiant_dom_m4b_src_get)
RADIANT_GUARDED_SET(radiant_html_src_set, radiant_dom_guard_srct,
                   radiant_dom_m4b_src_set)
RADIANT_GUARDED_GET(radiant_html_href_get, radiant_dom_guard_hreft,
                   radiant_dom_m4b_href_get)
RADIANT_GUARDED_SET(radiant_html_href_set, radiant_dom_guard_hreft,
                   radiant_dom_m4b_href_set)
RADIANT_GUARDED_GET(radiant_html_protocol_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_protocol_get)
RADIANT_GUARDED_GET(radiant_html_host_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_host_get)
RADIANT_GUARDED_GET(radiant_html_hostname_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_hostname_get)
RADIANT_GUARDED_GET(radiant_html_pathname_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_pathname_get)
RADIANT_GUARDED_GET(radiant_html_search_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_search_get)
RADIANT_GUARDED_GET(radiant_html_hash_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_hash_get)
RADIANT_GUARDED_GET(radiant_html_origin_get, radiant_dom_guard_anchor,
                   radiant_dom_anchor_origin_get)

static int radiant_input_value_get(Item receiver, Item* out) {
    if (radiant_dom_guard_input_typed_value(receiver)) {
        return radiant_dom_input_typed_value_get(receiver, out);
    }
    // input.value uses the text-control state for text-like inputs, but the
    // content attribute for checkbox/radio/button-style inputs.
    return radiant_dom_m4c_get_value(receiver, out);
}

static int radiant_input_value_set(Item receiver, Item value, Item* out) {
    if (radiant_dom_guard_input_typed_value(receiver)) {
        return radiant_dom_input_typed_value_set(receiver, value, out);
    }
    if (radiant_dom_guard_tc(receiver)) {
        return radiant_dom_m4c_value2_set(receiver, value, out);
    }
    return radiant_dom_m4c_value3_set(receiver, value, out);
}

static const JubeMemberBind radiant_dom_node_members[] = {
    // DS1: rows whose iface is `dom_node` land here, generated from dom_api.def.
#define DOM_ROW_BIND_dom_node(name, argc, member, js) DOM_ROW_MEMBER(name, argc, member, js)
#define DOM_ROW_BIND_none(name, argc, member, js)
#define DOM_ROW_BIND_html_element(name, argc, member, js)
#define DOM_ROW_BIND_select_element(name, argc, member, js)
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    DOM_ROW_BIND_##iface(name, argc, member, js_name)
#include "../../dom/dom_api.def"
#undef DOM_OP
#undef DOM_ROW_BIND_none
#undef DOM_ROW_BIND_dom_node
#undef DOM_ROW_BIND_html_element
#undef DOM_ROW_BIND_select_element
    BIND_FIELD_JS("node_name", "nodeName", radiant_dom_member_node_name),
    BIND_FIELD_JS("base_uri", "baseURI", radiant_dom_member_base_uri),
    BIND_FIELD_JS("node_type", "nodeType", radiant_dom_member_node_type_any),
    BIND_FIELD_JS("parent_node", "parentNode", radiant_dom_member_parent_node_any),
    BIND_FIELD_JS("parent_element", "parentElement", radiant_dom_member_parent_element_any),
    BIND_FIELD_JS("is_connected", "isConnected", radiant_dom_member_is_connected_any),
    BIND_FIELD_JS("owner_document", "ownerDocument", radiant_dom_member_owner_document_any),
    BIND_FIELD_JS("first_child", "firstChild", radiant_dom_member_first_child_any),
    BIND_FIELD_JS("last_child", "lastChild", radiant_dom_member_last_child_any),
    BIND_FIELD_JS("next_sibling", "nextSibling", radiant_dom_member_next_sibling_any),
    BIND_FIELD_JS("previous_sibling", "previousSibling", radiant_dom_member_previous_sibling_any),
    BIND_FIELD_JS("child_nodes", "childNodes", radiant_dom_member_child_nodes_any),
    BIND_CALL_JS("get_root_node", "getRootNode", radiant_dom_m4d_get_root_node),
    BIND_CALL("remove", radiant_dom_m4d_remove2),
    BIND_CALL_JS("replace_with", "replaceWith", radiant_dom_m4d_replace_with),
    BIND_CALL("after", radiant_dom_m4d_after),
    BIND_CALL("before", radiant_dom_m4d_before),
    // NOT a BIND_ROW: cloneNode is a Node operation, and this door's dispatcher
    // carries the text/comment branch (the element-only clone bridge returns
    // null for them). Routing it to the row would skip that branch -- the row
    // has to grow a non-element path before the member can hold it.
    BIND_CALL_JS("clone_node", "cloneNode", radiant_dom_m4d_clone_node),
    BIND_CALL_JS("add_event_listener", "addEventListener", radiant_dom_m4d_add_event_listener),
    BIND_CALL_JS("remove_event_listener", "removeEventListener", radiant_dom_m4d_remove_event_listener),
    BIND_CALL_JS("dispatch_event", "dispatchEvent", radiant_dom_m4d_dispatch_event),
};

static const JubeMemberBind radiant_dom_html_element_members[] = {
    // both namespace variants use the same DOM attachment algorithm.
#define ATTR_ROW(name, js, row, count) \
    {name, js, NULL, NULL, NULL, NULL, JUBE_MEMBER_REQUIRED_ARGS(count - 1), JUBE_DOM_ROW_##row, count}
    ATTR_ROW("get_attribute_node", "getAttributeNode", get_attribute_node, 2),
    ATTR_ROW("get_attribute_node_ns", "getAttributeNodeNS", get_attribute_node_ns, 3),
    ATTR_ROW("set_attribute_node", "setAttributeNode", set_attribute_node, 2),
    ATTR_ROW("set_attribute_node_ns", "setAttributeNodeNS", set_attribute_node, 2),
    ATTR_ROW("remove_attribute_node", "removeAttributeNode", remove_attribute_node, 2),
#undef ATTR_ROW
    // DS1: rows whose iface is `html_element` land here, generated from dom_api.def.
#define DOM_ROW_BIND_html_element(name, argc, member, js) DOM_ROW_MEMBER(name, argc, member, js)
#define DOM_ROW_BIND_none(name, argc, member, js)
#define DOM_ROW_BIND_dom_node(name, argc, member, js)
#define DOM_ROW_BIND_select_element(name, argc, member, js)
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    DOM_ROW_BIND_##iface(name, argc, member, js_name)
#include "../../dom/dom_api.def"
#undef DOM_OP
#undef DOM_ROW_BIND_none
#undef DOM_ROW_BIND_dom_node
#undef DOM_ROW_BIND_html_element
#undef DOM_ROW_BIND_select_element
    BIND_FIELD("tag_name", radiant_dom_member_tag_name),
    BIND_FIELD("local_name", radiant_dom_member_local_name),
    BIND_FIELD_JS("namespace_uri", "namespaceURI", radiant_dom_member_namespace_uri),
    BIND_FIELD("prefix", radiant_dom_member_prefix),
    BIND_FIELD("id", radiant_dom_member_id),
    BIND_FIELD("class_name", radiant_dom_member_class_name),
    BIND_FIELD("child_element_count", radiant_dom_member_child_element_count),
    BIND_FIELD("children", radiant_dom_member_children),
    BIND_FIELD("attributes", radiant_dom_member_attributes),
    BIND_FIELD_SET_JS("inner_html", "innerHTML", radiant_element_inner_html, radiant_element_inner_html_set),
    BIND_GET_PROTO("dataset", radiant_element_dataset),
    BIND_FIELD("first_element_child", radiant_dom_member_first_element_child),
    BIND_FIELD("last_element_child", radiant_dom_member_last_element_child),
    BIND_FIELD("next_element_sibling", radiant_dom_member_next_element_sibling),
    BIND_FIELD("previous_element_sibling", radiant_dom_member_previous_element_sibling),
    BIND_FIELD_SET("disabled", radiant_html_disabled_get, radiant_html_disabled_set),
    BIND_FIELD_SET("required", radiant_html_required_get, radiant_html_required_set),
    BIND_FIELD_SET_JS("no_validate", "noValidate", radiant_html_no_validate_get, radiant_html_no_validate_set),
    BIND_FIELD_SET_JS("form_no_validate", "formNoValidate", radiant_html_form_no_validate_get, radiant_html_form_no_validate_set),
    BIND_FIELD_SET("open", radiant_html_open_get, radiant_html_open_set),
    BIND_FIELD_SET("autofocus", radiant_dom_m4b_autofocus_get, radiant_dom_m4b_autofocus_set),
    BIND_FIELD_SET_JS("max_length", "maxLength", radiant_html_max_length_get, radiant_html_max_length_set),
    BIND_FIELD_SET_JS("min_length", "minLength", radiant_html_min_length_get, radiant_html_min_length_set),
    BIND_FIELD_SET("src", radiant_html_src_get, radiant_html_src_set),
    BIND_FIELD_SET("href", radiant_html_href_get, radiant_html_href_set),
    BIND_FIELD("protocol", radiant_html_protocol_get),
    BIND_FIELD("host", radiant_html_host_get),
    BIND_FIELD("hostname", radiant_html_hostname_get),
    BIND_FIELD("pathname", radiant_html_pathname_get),
    BIND_FIELD("search", radiant_html_search_get),
    BIND_FIELD("hash", radiant_html_hash_get),
    BIND_FIELD("origin", radiant_html_origin_get),
    BIND_FIELD_SET("alt", radiant_html_alt_get, radiant_html_alt_set),
    BIND_FIELD_SET("name", radiant_html_name_get, radiant_html_name_set),
    BIND_FIELD_SET("placeholder", radiant_html_placeholder_get, radiant_html_placeholder_set),
    BIND_FIELD_SET("autocomplete", radiant_html_autocomplete_get, radiant_html_autocomplete_set),
    BIND_FIELD_SET_JS("html_for", "htmlFor", radiant_html_html_for_get, radiant_html_html_for_set),
    BIND_FIELD_SET("target", radiant_html_target_get, radiant_html_target_set),
    BIND_FIELD_SET_JS("accept_charset", "acceptCharset", radiant_html_accept_charset_get, radiant_html_accept_charset_set),
    BIND_FIELD_SET_JS("form_target", "formTarget", radiant_html_form_target_get, radiant_html_form_target_set),
    BIND_FIELD_SET("input_mode", radiant_dom_m4b_input_mode_get, radiant_dom_m4b_input_mode_set),
    BIND_FIELD_SET_JS("enter_key_hint", "enterKeyHint", radiant_dom_m4b_enter_key_hint_get, radiant_dom_m4b_enter_key_hint_set),
    BIND_FIELD_SET_JS("content_editable", "contentEditable", radiant_dom_m4b_content_editable_get, radiant_dom_m4b_content_editable_set),
    BIND_FIELD_JS("is_content_editable", "isContentEditable", radiant_dom_m4b_is_content_editable_get),
    BIND_CALL_JS("webkit_matches_selector", "webkitMatchesSelector", radiant_dom_m4d_matches),
    BIND_CALL_JS("ms_matches_selector", "msMatchesSelector", radiant_dom_m4d_matches),
    BIND_CALL("append", radiant_dom_m4d_append),
    BIND_CALL("prepend", radiant_dom_m4d_prepend),
    BIND_CALL_JS("get_bounding_client_rect", "getBoundingClientRect", radiant_dom_m4d_get_bounding_client_rect),
    BIND_CALL_JS("get_client_rects", "getClientRects", radiant_dom_m4d_get_client_rects),
    BIND_CALL("scroll", radiant_dom_m4d_scroll),
    BIND_CALL_JS("scroll_to", "scrollTo", radiant_dom_m4d_scroll_to),
    BIND_CALL_JS("scroll_by", "scrollBy", radiant_dom_m4d_scroll_by),
    BIND_CALL("click", radiant_dom_m4d_click),
    BIND_CALL_JS("set_pointer_capture", "setPointerCapture", radiant_dom_m4d_set_pointer_capture),
    BIND_CALL_JS("release_pointer_capture", "releasePointerCapture", radiant_dom_m4d_release_pointer_capture),
    BIND_CALL_JS("has_pointer_capture", "hasPointerCapture", radiant_dom_m4d_has_pointer_capture),
    BIND_CALL("reset", radiant_dom_m4d_reset),
    BIND_CALL("submit", radiant_dom_m4d_submit),
    BIND_CALL_JS("request_submit", "requestSubmit", radiant_dom_m4d_request_submit),
    BIND_CALL_JS("check_validity", "checkValidity", radiant_dom_m4d_check_validity),
    BIND_CALL("select", radiant_dom_m4d_select),
    BIND_CALL("item", radiant_dom_m4d_item),
    BIND_CALL("toggle", radiant_dom_m4d_toggle),
    BIND_CALL("replace", radiant_dom_m4d_replace),
    BIND_CALL_JS("to_string", "toString", radiant_dom_m4d_to_string),
    BIND_CALL_JS("__lambda_boundary_from_point", "__lambdaBoundaryFromPoint", radiant_dom_m4d___lambda_boundary_from_point),
    BIND_CALL_JS("__lambda_text_control_boundary_from_point", "__lambdaTextControlBoundaryFromPoint", radiant_dom_m4d___lambda_text_control_boundary_from_point),
    BIND_CALL_JS("__lambda_text_control_caret_bounds", "__lambdaTextControlCaretBounds", radiant_dom_m4d___lambda_text_control_caret_bounds),
};

static const JubeMemberBind radiant_dom_character_data_members[] = {
    BIND_FIELD("data", radiant_dom_member_data),
    BIND_FIELD_JS("node_value", "nodeValue", radiant_dom_member_node_value),
    BIND_FIELD_JS("text_content", "textContent", radiant_dom_member_text_content),
    BIND_CALL_JS("replace_data", "replaceData", radiant_dom_m4d_replace_data),
    BIND_CALL_JS("insert_data", "insertData", radiant_dom_m4d_insert_data),
    BIND_CALL_JS("append_data", "appendData", radiant_dom_m4d_append_data),
    BIND_CALL_JS("delete_data", "deleteData", radiant_dom_m4d_delete_data),
    BIND_CALL_JS("substring_data", "substringData", radiant_dom_m4d_substring_data),
    BIND_CALL_JS("split_text", "splitText", radiant_dom_m4d_split_text),
};

static int radiant_dom_svg_target_element_get(Item receiver, Item* out) {
    return radiant_dom_m4d_get_target_element(receiver, nullptr, 0, out);
}

static const JubeMemberBind radiant_dom_svg_element_members[] = {
    BIND_GET_PROTO("dataset", radiant_element_dataset),
    BIND_CALL_JS("create_svg_point", "createSVGPoint", radiant_dom_m4d_create_svg_point),
    BIND_CALL_JS("create_svg_matrix", "createSVGMatrix", radiant_dom_m4d_create_svg_matrix),
    BIND_CALL_JS("create_svg_transform", "createSVGTransform", radiant_dom_m4d_create_svg_transform),
    BIND_CALL_JS("create_svg_transform_from_matrix", "createSVGTransformFromMatrix", radiant_dom_m4d_create_svg_transform_from_matrix),
    BIND_CALL_JS("get_bbox", "getBBox", radiant_dom_m4d_get_bbox),
    BIND_CALL_JS("get_ctm", "getCTM", radiant_dom_m4d_get_ctm),
    BIND_CALL_JS("get_screen_ctm", "getScreenCTM", radiant_dom_m4d_get_screen_ctm),
    BIND_CALL_JS("pause_animations", "pauseAnimations", radiant_dom_m4d_pause_animations),
    BIND_CALL_JS("unpause_animations", "unpauseAnimations", radiant_dom_m4d_unpause_animations),
    BIND_CALL_JS("animations_paused", "animationsPaused", radiant_dom_m4d_animations_paused),
    BIND_CALL_JS("get_current_time", "getCurrentTime", radiant_dom_m4d_get_current_time),
    BIND_FIELD_JS("target_element", "targetElement", radiant_dom_svg_target_element_get),
    BIND_CALL_JS("get_start_time", "getStartTime", radiant_dom_m4d_get_start_time),
    BIND_CALL_JS("get_simple_duration", "getSimpleDuration", radiant_dom_m4d_get_simple_duration),
    BIND_CALL_JS("set_current_time", "setCurrentTime", radiant_dom_m4d_set_current_time),
    BIND_CALL_JS("begin_element", "beginElement", radiant_dom_m4d_begin_element),
    BIND_CALL_JS("begin_element_at", "beginElementAt", radiant_dom_m4d_begin_element_at),
    BIND_CALL_JS("end_element", "endElement", radiant_dom_m4d_end_element),
    BIND_CALL_JS("end_element_at", "endElementAt", radiant_dom_m4d_end_element_at),
};

static const JubeMemberBind radiant_dom_input_element_members[] = {
    // Keep this off HTMLElement so `readOnly in button` follows the IDL surface.
    BIND_FIELD_SET_JS("read_only", "readOnly", radiant_html_read_only_get, radiant_html_read_only_set),
    BIND_FIELD_SET("readonly", radiant_html_readonly_get, radiant_html_readonly_set),
    BIND_FIELD_SET_JS("default_checked", "defaultChecked", radiant_dom_m4b_default_checked_get, radiant_dom_m4b_default_checked_set),
    BIND_FIELD_SET("multiple", radiant_dom_m4b_multiple_get, radiant_dom_m4b_multiple_set),
    BIND_FIELD_SET("size", radiant_dom_m4b_size_get, radiant_dom_m4b_size_set),
    BIND_FIELD_SET("width", radiant_dom_m4b_width_get, radiant_dom_m4b_width_set),
    BIND_FIELD_SET("height", radiant_dom_m4b_height_get, radiant_dom_m4b_height_set),
    BIND_FIELD_SET("checked", radiant_dom_m4c_get_checked, radiant_dom_m4c_checked_set),
    BIND_FIELD_SET("type", radiant_dom_input_type_get, radiant_dom_input_type_set),
    BIND_FIELD_SET("value", radiant_input_value_get, radiant_input_value_set),
    BIND_FIELD_SET_JS("selection_start", "selectionStart", radiant_dom_m4c_get_selectionStart, radiant_dom_m4c_selection_start_set),
    BIND_FIELD_SET_JS("selection_end", "selectionEnd", radiant_dom_m4c_get_selectionEnd, radiant_dom_m4c_selection_end_set),
    BIND_FIELD_SET_JS("selection_direction", "selectionDirection", radiant_dom_m4c_get_selectionDirection, radiant_dom_m4c_selection_direction_set),
    BIND_FIELD_SET_JS("default_value", "defaultValue", radiant_dom_m4c_get_defaultValue, radiant_dom_m4c_default_value_set),
    BIND_FIELD_SET_JS("value_as_number", "valueAsNumber", radiant_dom_input_value_as_number_get, radiant_dom_input_value_as_number_set),
    BIND_FIELD_SET_JS("value_as_date", "valueAsDate", radiant_dom_input_value_as_date_get, radiant_dom_input_value_as_date_set),
    BIND_FIELD_SET("files", radiant_dom_input_files_get_member, radiant_dom_input_files_set_member),
    BIND_FIELD_SET("pattern", radiant_dom_m4b_pattern_get, radiant_dom_m4b_pattern_set),
    BIND_FIELD_SET("min", radiant_dom_m4b_min_get, radiant_dom_m4b_min_set),
    BIND_FIELD_SET("max", radiant_dom_m4b_max_get, radiant_dom_m4b_max_set),
    BIND_FIELD_SET("step", radiant_dom_m4b_step_get, radiant_dom_m4b_step_set),
    BIND_FIELD_SET("accept", radiant_dom_m4b_accept_get, radiant_dom_m4b_accept_set),
    BIND_CALL_JS("step_up", "stepUp", radiant_dom_input_step_up),
    BIND_CALL_JS("step_down", "stepDown", radiant_dom_input_step_down),
};

static const JubeMemberBind radiant_dom_select_element_members[] = {
    // DS1: rows whose iface is `select_element` land here, generated from dom_api.def.
#define DOM_ROW_BIND_select_element(name, argc, member, js) DOM_ROW_MEMBER(name, argc, member, js)
#define DOM_ROW_BIND_none(name, argc, member, js)
#define DOM_ROW_BIND_dom_node(name, argc, member, js)
#define DOM_ROW_BIND_html_element(name, argc, member, js)
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    DOM_ROW_BIND_##iface(name, argc, member, js_name)
#include "../../dom/dom_api.def"
#undef DOM_OP
#undef DOM_ROW_BIND_none
#undef DOM_ROW_BIND_dom_node
#undef DOM_ROW_BIND_html_element
#undef DOM_ROW_BIND_select_element
    BIND_FIELD_SET("multiple", radiant_dom_m4b_multiple2_get, radiant_dom_m4b_multiple2_set),
    BIND_FIELD_SET("size", radiant_dom_m4b_size2_get, radiant_dom_m4b_size2_set),
    BIND_FIELD_SET("value", radiant_dom_m4c_get_value, radiant_dom_m4c_value_set),
    BIND_FIELD_SET_JS("selected_index", "selectedIndex", radiant_dom_m4c_get_selectedIndex, radiant_dom_m4c_selected_index_set),
    BIND_FIELD_SET("length", radiant_dom_m4c_get_length, radiant_dom_m4c_length_set),
    BIND_FIELD("options", radiant_dom_m4c_get_options),
    BIND_FIELD_JS("selected_options", "selectedOptions", radiant_dom_m4c_get_selectedOptions),
    BIND_FIELD("type", radiant_dom_m4c_get_type),
    BIND_CALL("remove", radiant_dom_m4d_remove),
};

static const JubeMemberBind radiant_dom_textarea_element_members[] = {
    BIND_FIELD_SET_JS("read_only", "readOnly", radiant_html_read_only_get, radiant_html_read_only_set),
    BIND_FIELD_SET("readonly", radiant_html_readonly_get, radiant_html_readonly_set),
    BIND_FIELD_SET("rows", radiant_dom_m4b_rows_get, radiant_dom_m4b_rows_set),
    BIND_FIELD_SET("cols", radiant_dom_m4b_cols_get, radiant_dom_m4b_cols_set),
    BIND_FIELD_SET("wrap", radiant_dom_m4b_wrap_get, radiant_dom_m4b_wrap_set),
    BIND_FIELD_SET("value", radiant_dom_m4c_get_value, radiant_dom_m4c_value2_set),
    BIND_FIELD_SET_JS("selection_start", "selectionStart", radiant_dom_m4c_get_selectionStart, radiant_dom_m4c_selection_start_set),
    BIND_FIELD_SET_JS("selection_end", "selectionEnd", radiant_dom_m4c_get_selectionEnd, radiant_dom_m4c_selection_end_set),
    BIND_FIELD_SET_JS("selection_direction", "selectionDirection", radiant_dom_m4c_get_selectionDirection, radiant_dom_m4c_selection_direction_set),
    BIND_FIELD_SET_JS("default_value", "defaultValue", radiant_dom_m4c_get_defaultValue, radiant_dom_m4c_default_value_set),
};

static const JubeMemberBind radiant_dom_option_element_members[] = {
    BIND_FIELD_SET_JS("default_selected", "defaultSelected", radiant_dom_m4b_default_selected_get, radiant_dom_m4b_default_selected_set),
    BIND_FIELD_SET("value", radiant_dom_m4c_get_value, radiant_dom_m4c_value4_set),
    BIND_FIELD_SET("selected", radiant_dom_m4c_get_selected, radiant_dom_m4c_selected_set),
    BIND_FIELD_SET("text", radiant_dom_m4c_get_text, radiant_dom_m4c_text_set),
    BIND_FIELD("index", radiant_dom_m4c_get_index),
    BIND_FIELD("label", radiant_dom_m4c_get_label),
    BIND_FIELD("form", radiant_dom_m4c_get_form),
};

static Item radiant_dom_doc_key(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

#define RADIANT_DOC_GET_FN(fn, js) \
    static int fn(Item receiver, Item* out) { \
        return radiant_dom_document_host_get_property(receiver, radiant_dom_doc_key(js), out); \
    }

#define RADIANT_DOC_SET_FN(fn, js) \
    static int fn(Item receiver, Item value, Item* out) { \
        return radiant_dom_document_host_set_property(receiver, radiant_dom_doc_key(js), value, out); \
    }

#define RADIANT_DOC_CALL_FN(fn, operation) \
    static int fn(Item receiver, Item* args, int argc, Item* out) { \
        return radiant_dom_document_operation(receiver, operation, args, argc, out); \
    }

RADIANT_DOC_GET_FN(radiant_doc_get_document_element, "documentElement")
RADIANT_DOC_GET_FN(radiant_doc_get_body, "body")
RADIANT_DOC_GET_FN(radiant_doc_get_head, "head")
RADIANT_DOC_GET_FN(radiant_doc_get_title, "title")
RADIANT_DOC_GET_FN(radiant_doc_get_cookie, "cookie")
RADIANT_DOC_SET_FN(radiant_doc_set_cookie, "cookie")
RADIANT_DOC_GET_FN(radiant_doc_get_url, "URL")
RADIANT_DOC_GET_FN(radiant_doc_get_current_script, "currentScript")
RADIANT_DOC_GET_FN(radiant_doc_get_href, "href")
RADIANT_DOC_GET_FN(radiant_doc_get_protocol, "protocol")
RADIANT_DOC_GET_FN(radiant_doc_get_hostname, "hostname")
RADIANT_DOC_GET_FN(radiant_doc_get_port, "port")
RADIANT_DOC_GET_FN(radiant_doc_get_pathname, "pathname")
RADIANT_DOC_GET_FN(radiant_doc_get_search, "search")
RADIANT_DOC_GET_FN(radiant_doc_get_hash, "hash")
RADIANT_DOC_GET_FN(radiant_doc_get_host, "host")
RADIANT_DOC_GET_FN(radiant_doc_get_origin, "origin")
RADIANT_DOC_GET_FN(radiant_doc_get_location, "location")
RADIANT_DOC_GET_FN(radiant_doc_get_document, "document")
RADIANT_DOC_GET_FN(radiant_doc_get_ready_state, "readyState")
RADIANT_DOC_GET_FN(radiant_doc_get_fonts, "fonts")
RADIANT_DOC_GET_FN(radiant_doc_get_compat_mode, "compatMode")
RADIANT_DOC_GET_FN(radiant_doc_get_character_set, "characterSet")
RADIANT_DOC_GET_FN(radiant_doc_get_charset, "charset")
RADIANT_DOC_GET_FN(radiant_doc_get_content_type, "contentType")
RADIANT_DOC_GET_FN(radiant_doc_get_node_type, "nodeType")
RADIANT_DOC_GET_FN(radiant_doc_get_node_name, "nodeName")
RADIANT_DOC_GET_FN(radiant_doc_get_owner_document, "ownerDocument")
RADIANT_DOC_GET_FN(radiant_doc_get_child_nodes, "childNodes")
RADIANT_DOC_GET_FN(radiant_doc_get_doctype, "doctype")
RADIANT_DOC_GET_FN(radiant_doc_get_style_sheets, "styleSheets")
RADIANT_DOC_GET_FN(radiant_doc_get_default_view, "defaultView")
RADIANT_DOC_GET_FN(radiant_doc_get_implementation, "implementation")
RADIANT_DOC_GET_FN(radiant_doc_get_design_mode, "designMode")
RADIANT_DOC_GET_FN(radiant_doc_get_active_element, "activeElement")
RADIANT_DOC_GET_FN(radiant_doc_get_forms, "forms")
RADIANT_DOC_SET_FN(radiant_doc_set_design_mode, "designMode")

static int radiant_doc_call_to_string(Item receiver, Item* args, int argc, Item* out) {
    (void)args;
    (void)argc;
    // Location is this document wrapper, so its string form is the URL.
    return radiant_dom_document_host_get_property(receiver, radiant_dom_doc_key("href"), out);
}

RADIANT_DOC_CALL_FN(radiant_doc_call_assign, RADIANT_DOCUMENT_ASSIGN)
RADIANT_DOC_CALL_FN(radiant_doc_call_replace, RADIANT_DOCUMENT_REPLACE)
RADIANT_DOC_CALL_FN(radiant_doc_call_reload, RADIANT_DOCUMENT_RELOAD)
RADIANT_DOC_CALL_FN(radiant_doc_call_focus, RADIANT_DOCUMENT_FOCUS)
RADIANT_DOC_CALL_FN(radiant_doc_call_blur, RADIANT_DOCUMENT_BLUR)
RADIANT_DOC_CALL_FN(radiant_doc_call_has_focus, RADIANT_DOCUMENT_HAS_FOCUS)
RADIANT_DOC_CALL_FN(radiant_doc_call_open, RADIANT_DOCUMENT_OPEN)
RADIANT_DOC_CALL_FN(radiant_doc_call_close, RADIANT_DOCUMENT_CLOSE)
RADIANT_DOC_CALL_FN(radiant_doc_call_write, RADIANT_DOCUMENT_WRITE)
RADIANT_DOC_CALL_FN(radiant_doc_call_writeln, RADIANT_DOCUMENT_WRITELN)
RADIANT_DOC_CALL_FN(radiant_doc_call_element_from_point, RADIANT_DOCUMENT_ELEMENT_FROM_POINT)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_range, RADIANT_DOCUMENT_CREATE_RANGE)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_selection, RADIANT_DOCUMENT_GET_SELECTION)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_element_by_id, RADIANT_DOCUMENT_GET_ELEMENT_BY_ID)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_elements_by_class_name, RADIANT_DOCUMENT_GET_ELEMENTS_BY_CLASS_NAME)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_elements_by_tag_name, RADIANT_DOCUMENT_GET_ELEMENTS_BY_TAG_NAME)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_elements_by_name, RADIANT_DOCUMENT_GET_ELEMENTS_BY_NAME)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_selector, RADIANT_DOCUMENT_QUERY_SELECTOR)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_selector_all, RADIANT_DOCUMENT_QUERY_SELECTOR_ALL)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_element, RADIANT_DOCUMENT_CREATE_ELEMENT)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_element_ns, RADIANT_DOCUMENT_CREATE_ELEMENT_NS)

static int radiant_doc_create_attribute(Item receiver, Item* args, int argc, Item* out, bool namespaced) {
    *out = radiant_host_api->dom_catalog->create_attribute(receiver,
        namespaced ? radiant_iface_arg(args, argc, 0) : ItemNull,
        radiant_iface_arg(args, argc, namespaced ? 1 : 0), (Item){.item = b2it(namespaced)});
    return 1;
}
#define ATTRIBUTE_FACTORY(fn, ns) \
    static int fn(Item receiver, Item* args, int argc, Item* out) { \
        return radiant_doc_create_attribute(receiver, args, argc, out, ns); \
    }
ATTRIBUTE_FACTORY(radiant_doc_call_create_attribute, false)
ATTRIBUTE_FACTORY(radiant_doc_call_create_attribute_ns, true)
#undef ATTRIBUTE_FACTORY
RADIANT_DOC_CALL_FN(radiant_doc_call_create_text_node, RADIANT_DOCUMENT_CREATE_TEXT_NODE)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_document_fragment, RADIANT_DOCUMENT_CREATE_DOCUMENT_FRAGMENT)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_comment, RADIANT_DOCUMENT_CREATE_COMMENT)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_processing_instruction, RADIANT_DOCUMENT_CREATE_PROCESSING_INSTRUCTION)
RADIANT_DOC_CALL_FN(radiant_doc_call_import_node, RADIANT_DOCUMENT_IMPORT_NODE)
RADIANT_DOC_CALL_FN(radiant_doc_call_normalize, RADIANT_DOCUMENT_NORMALIZE)
RADIANT_DOC_CALL_FN(radiant_doc_call_adopt_node, RADIANT_DOCUMENT_ADOPT_NODE)
RADIANT_DOC_CALL_FN(radiant_doc_call_append_child, RADIANT_DOCUMENT_APPEND_CHILD)
RADIANT_DOC_CALL_FN(radiant_doc_call_contains, RADIANT_DOCUMENT_CONTAINS)
RADIANT_DOC_CALL_FN(radiant_doc_call_compare_document_position, RADIANT_DOCUMENT_COMPARE_DOCUMENT_POSITION)
RADIANT_DOC_CALL_FN(radiant_doc_call_get_root_node, RADIANT_DOCUMENT_GET_ROOT_NODE)
RADIANT_DOC_CALL_FN(radiant_doc_call_add_event_listener, RADIANT_DOCUMENT_ADD_EVENT_LISTENER)
RADIANT_DOC_CALL_FN(radiant_doc_call_remove_event_listener, RADIANT_DOCUMENT_REMOVE_EVENT_LISTENER)
RADIANT_DOC_CALL_FN(radiant_doc_call_dispatch_event, RADIANT_DOCUMENT_DISPATCH_EVENT)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_tree_walker, RADIANT_DOCUMENT_CREATE_TREE_WALKER)
RADIANT_DOC_CALL_FN(radiant_doc_call_create_event, RADIANT_DOCUMENT_CREATE_EVENT)
RADIANT_DOC_CALL_FN(radiant_doc_call_exec_command, RADIANT_DOCUMENT_EXEC_COMMAND)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_command_supported, RADIANT_DOCUMENT_QUERY_COMMAND_SUPPORTED)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_command_enabled, RADIANT_DOCUMENT_QUERY_COMMAND_ENABLED)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_command_state, RADIANT_DOCUMENT_QUERY_COMMAND_STATE)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_command_indeterm, RADIANT_DOCUMENT_QUERY_COMMAND_INDETERM)
RADIANT_DOC_CALL_FN(radiant_doc_call_query_command_value, RADIANT_DOCUMENT_QUERY_COMMAND_VALUE)

#define DOC_FIELD(n, js, fn) \
    {n, js, fn, NULL, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define DOC_FIELD_SET(n, js, get_fn, set_fn) \
    {n, js, get_fn, set_fn, NULL, NULL, JUBE_MEMBER_NON_ENUMERABLE}
#define DOC_METHOD(n, js, fn) \
    {n, js, NULL, NULL, fn, NULL, JUBE_MEMBER_NON_ENUMERABLE}

static const JubeMemberBind radiant_document_members[] = {
    DOC_FIELD("document_element", "documentElement", radiant_doc_get_document_element),
    DOC_FIELD("body", NULL, radiant_doc_get_body),
    DOC_FIELD("head", NULL, radiant_doc_get_head),
    DOC_FIELD("title", NULL, radiant_doc_get_title),
    {"cookie", NULL, radiant_doc_get_cookie, radiant_doc_set_cookie, NULL, NULL, 0},
    DOC_FIELD("url", "URL", radiant_doc_get_url),
    DOC_FIELD("href", NULL, radiant_doc_get_href),
    DOC_FIELD("protocol", NULL, radiant_doc_get_protocol),
    DOC_FIELD("hostname", NULL, radiant_doc_get_hostname),
    DOC_FIELD("port", NULL, radiant_doc_get_port),
    DOC_FIELD("pathname", NULL, radiant_doc_get_pathname),
    DOC_FIELD("search", NULL, radiant_doc_get_search),
    DOC_FIELD("hash", NULL, radiant_doc_get_hash),
    DOC_FIELD("host", NULL, radiant_doc_get_host),
    DOC_FIELD("origin", NULL, radiant_doc_get_origin),
    DOC_FIELD("location", NULL, radiant_doc_get_location),
    DOC_FIELD("document", NULL, radiant_doc_get_document),
    DOC_METHOD("to_string", "toString", radiant_doc_call_to_string),
    DOC_FIELD("ready_state", "readyState", radiant_doc_get_ready_state),
    DOC_FIELD("current_script", "currentScript", radiant_doc_get_current_script),
    DOC_FIELD("fonts", NULL, radiant_doc_get_fonts),
    DOC_FIELD("compat_mode", "compatMode", radiant_doc_get_compat_mode),
    DOC_FIELD("character_set", "characterSet", radiant_doc_get_character_set),
    DOC_FIELD("charset", NULL, radiant_doc_get_charset),
    DOC_FIELD("content_type", "contentType", radiant_doc_get_content_type),
    DOC_FIELD("node_type", "nodeType", radiant_doc_get_node_type),
    DOC_FIELD("node_name", "nodeName", radiant_doc_get_node_name),
    DOC_FIELD("owner_document", "ownerDocument", radiant_doc_get_owner_document),
    DOC_FIELD("child_nodes", "childNodes", radiant_doc_get_child_nodes),
    DOC_FIELD("doctype", NULL, radiant_doc_get_doctype),
    DOC_FIELD("style_sheets", "styleSheets", radiant_doc_get_style_sheets),
    DOC_FIELD("default_view", "defaultView", radiant_doc_get_default_view),
    DOC_FIELD("implementation", NULL, radiant_doc_get_implementation),
    DOC_FIELD_SET("design_mode", "designMode", radiant_doc_get_design_mode,
                  radiant_doc_set_design_mode),
    DOC_FIELD("active_element", "activeElement", radiant_doc_get_active_element),
    DOC_FIELD("forms", NULL, radiant_doc_get_forms),
    DOC_METHOD("assign", NULL, radiant_doc_call_assign),
    DOC_METHOD("replace", NULL, radiant_doc_call_replace),
    DOC_METHOD("reload", NULL, radiant_doc_call_reload),
    DOC_METHOD("focus", NULL, radiant_doc_call_focus),
    DOC_METHOD("blur", NULL, radiant_doc_call_blur),
    DOC_METHOD("has_focus", "hasFocus", radiant_doc_call_has_focus),
    DOC_METHOD("open", NULL, radiant_doc_call_open),
    DOC_METHOD("close", NULL, radiant_doc_call_close),
    DOC_METHOD("write", NULL, radiant_doc_call_write),
    DOC_METHOD("writeln", NULL, radiant_doc_call_writeln),
    DOC_METHOD("element_from_point", "elementFromPoint", radiant_doc_call_element_from_point),
    DOC_METHOD("create_range", "createRange", radiant_doc_call_create_range),
    DOC_METHOD("get_selection", "getSelection", radiant_doc_call_get_selection),
    DOC_METHOD("get_element_by_id", "getElementById", radiant_doc_call_get_element_by_id),
    DOC_METHOD("get_elements_by_class_name", "getElementsByClassName", radiant_doc_call_get_elements_by_class_name),
    DOC_METHOD("get_elements_by_tag_name", "getElementsByTagName", radiant_doc_call_get_elements_by_tag_name),
    DOC_METHOD("get_elements_by_name", "getElementsByName", radiant_doc_call_get_elements_by_name),
    DOC_METHOD("query_selector", "querySelector", radiant_doc_call_query_selector),
    DOC_METHOD("query_selector_all", "querySelectorAll", radiant_doc_call_query_selector_all),
    DOC_METHOD("create_element", "createElement", radiant_doc_call_create_element),
    DOC_METHOD("create_element_ns", "createElementNS", radiant_doc_call_create_element_ns),
    {"create_attribute", "createAttribute", NULL, NULL, radiant_doc_call_create_attribute, NULL, JUBE_MEMBER_REQUIRED_ARGS(1)},
    {"create_attribute_ns", "createAttributeNS", NULL, NULL, radiant_doc_call_create_attribute_ns, NULL, JUBE_MEMBER_REQUIRED_ARGS(2)},
    DOC_METHOD("create_text_node", "createTextNode", radiant_doc_call_create_text_node),
    DOC_METHOD("create_document_fragment", "createDocumentFragment", radiant_doc_call_create_document_fragment),
    DOC_METHOD("create_comment", "createComment", radiant_doc_call_create_comment),
    DOC_METHOD("create_processing_instruction", "createProcessingInstruction", radiant_doc_call_create_processing_instruction),
    DOC_METHOD("import_node", "importNode", radiant_doc_call_import_node),
    DOC_METHOD("normalize", NULL, radiant_doc_call_normalize),
    DOC_METHOD("adopt_node", "adoptNode", radiant_doc_call_adopt_node),
    DOC_METHOD("append_child", "appendChild", radiant_doc_call_append_child),
    DOC_METHOD("contains", NULL, radiant_doc_call_contains),
    DOC_METHOD("compare_document_position", "compareDocumentPosition", radiant_doc_call_compare_document_position),
    DOC_METHOD("get_root_node", "getRootNode", radiant_doc_call_get_root_node),
    DOC_METHOD("clone_node", "cloneNode", radiant_dom_document_clone_node),
    DOC_METHOD("add_event_listener", "addEventListener", radiant_doc_call_add_event_listener),
    DOC_METHOD("remove_event_listener", "removeEventListener", radiant_doc_call_remove_event_listener),
    DOC_METHOD("dispatch_event", "dispatchEvent", radiant_doc_call_dispatch_event),
    DOC_METHOD("create_tree_walker", "createTreeWalker", radiant_doc_call_create_tree_walker),
    DOC_METHOD("create_event", "createEvent", radiant_doc_call_create_event),
    DOC_METHOD("exec_command", "execCommand", radiant_doc_call_exec_command),
    DOC_METHOD("query_command_supported", "queryCommandSupported", radiant_doc_call_query_command_supported),
    DOC_METHOD("query_command_enabled", "queryCommandEnabled", radiant_doc_call_query_command_enabled),
    DOC_METHOD("query_command_state", "queryCommandState", radiant_doc_call_query_command_state),
    DOC_METHOD("query_command_indeterm", "queryCommandIndeterm", radiant_doc_call_query_command_indeterm),
    DOC_METHOD("query_command_value", "queryCommandValue", radiant_doc_call_query_command_value),
};

extern "C" int radiant_velmt_host_get_property(Item object, Item key, Item* out);
extern "C" int radiant_velmt_host_set_property(Item object, Item key, Item value, Item* out);
extern "C" int radiant_velmt_host_has_property(Item object, Item key, Item* out);
extern "C" int radiant_velmt_host_delete_property(Item object, Item key, Item* out);
extern "C" int radiant_velmt_host_own_property_descriptor(Item object, Item key, Item* out);
extern "C" int radiant_velmt_host_own_property_names(Item object, Item* out);
static Item radiant_velmt_no_prototype(void) {
    return ItemNull;
}

#define RADIANT_VELMT_GETTER(fn, property)                                  \
    static int fn(Item receiver, Item* out) {                               \
        return radiant_velmt_host_get_property(                             \
            receiver, radiant_dom_doc_key(property), out);                  \
    }

RADIANT_VELMT_GETTER(radiant_velmt_get_index, "index")
RADIANT_VELMT_GETTER(radiant_velmt_get_tag, "tag")
RADIANT_VELMT_GETTER(radiant_velmt_get_id, "id")
RADIANT_VELMT_GETTER(radiant_velmt_get_width, "width")
RADIANT_VELMT_GETTER(radiant_velmt_get_height, "height")
RADIANT_VELMT_GETTER(radiant_velmt_get_wd, "wd")
RADIANT_VELMT_GETTER(radiant_velmt_get_hg, "hg")
RADIANT_VELMT_GETTER(radiant_velmt_get_box, "box")
RADIANT_VELMT_GETTER(radiant_velmt_get_children, "children")
RADIANT_VELMT_GETTER(radiant_velmt_get_text, "text")
RADIANT_VELMT_GETTER(radiant_velmt_get_style, "style")
RADIANT_VELMT_GETTER(radiant_velmt_get_margin, "margin")
RADIANT_VELMT_GETTER(radiant_velmt_get_border, "border")
RADIANT_VELMT_GETTER(radiant_velmt_get_padding, "padding")
RADIANT_VELMT_GETTER(radiant_velmt_get_attrs, "attrs")

static const JubeMemberBind radiant_velmt_members[] = {
    BIND_FIELD("index", radiant_velmt_get_index),
    BIND_FIELD("tag", radiant_velmt_get_tag),
    BIND_FIELD("id", radiant_velmt_get_id),
    BIND_FIELD("width", radiant_velmt_get_width),
    BIND_FIELD("height", radiant_velmt_get_height),
    BIND_FIELD("wd", radiant_velmt_get_wd),
    BIND_FIELD("hg", radiant_velmt_get_hg),
    BIND_FIELD("box", radiant_velmt_get_box),
    BIND_FIELD("children", radiant_velmt_get_children),
    BIND_FIELD("text", radiant_velmt_get_text),
    BIND_FIELD("style", radiant_velmt_get_style),
    BIND_FIELD("margin", radiant_velmt_get_margin),
    BIND_FIELD("border", radiant_velmt_get_border),
    BIND_FIELD("padding", radiant_velmt_get_padding),
    BIND_FIELD("attrs", radiant_velmt_get_attrs),
};

extern "C" int dom_child_collection_named_get(Item receiver, Item key, Item* out);
extern "C" int dom_child_collection_named_has(Item receiver, Item key, Item* out);
extern "C" Item dom_options_collection_selected_index(Item collection);
extern "C" Item dom_options_collection_set_selected_index(
    Item collection, Item value);
extern "C" Item dom_options_collection_add(
    Item collection, Item element, Item before);

static int radiant_collection_length_get(Item receiver, Item* out) {
    if (!out || get_type_id(receiver) != LMD_TYPE_VARRAY) return 0;
    *out = (Item){.item = i2it(fn_len(receiver))};
    return 1;
}

static int radiant_collection_indexed_get(Item receiver, int64_t index, Item* out) {
    if (!out || get_type_id(receiver) != LMD_TYPE_VARRAY) return 0;
    // WebIDL indexed properties are absent outside the current collection
    // bounds. Reporting a successful Lambda null here turns a JS miss into
    // `null`, which breaks array-like consumers that require `undefined`.
    if (index < 0 || index >= varray_count(receiver.varray)) return 0;
    *out = item_at(receiver, index);
    return 1;
}

static int radiant_collection_item(Item receiver, Item* args, int argc, Item* out) {
    if (!out || get_type_id(receiver) != LMD_TYPE_VARRAY) return 0;
    int64_t index = argc > 0 ? fn_int64_index(args[0]) : INT64_MIN;
    *out = index == INT64_MIN ? ItemNull : item_at(receiver, index);
    return 1;
}

static int radiant_collection_named_item(Item receiver, Item* args, int argc, Item* out) {
    if (!out || get_type_id(receiver) != LMD_TYPE_VARRAY) return 0;
    if (argc <= 0 || !dom_child_collection_named_get(receiver, args[0], out)) {
        *out = ItemNull;
    }
    return 1;
}

static Item radiant_node_list_prototype(void) {
    return dom_realm_constructor_prototype("NodeList");
}

static Item radiant_html_collection_prototype(void) {
    return dom_realm_constructor_prototype("HTMLCollection");
}

static Item radiant_html_options_collection_prototype(void) {
    return dom_realm_constructor_prototype("HTMLOptionsCollection");
}

static Item radiant_html_form_controls_collection_prototype(void) {
    return dom_realm_constructor_prototype("HTMLFormControlsCollection");
}

static Item radiant_named_node_map_prototype(void) {
    return dom_realm_constructor_prototype("NamedNodeMap");
}

static Item radiant_dom_token_list_prototype(void) {
    return dom_realm_constructor_prototype("DOMTokenList");
}

static Item radiant_radio_node_list_prototype(void) {
    return dom_realm_constructor_prototype("RadioNodeList");
}

static Item radiant_dom_rect_list_prototype(void) {
    return dom_realm_constructor_prototype("DOMRectList");
}

static Item radiant_style_sheet_list_prototype(void) {
    return dom_realm_constructor_prototype("StyleSheetList");
}

static Item radiant_css_stylesheet_prototype(void) {
    return dom_realm_constructor_prototype("CSSStyleSheet");
}

static Item radiant_rule_declaration_prototype(void) {
    return dom_realm_constructor_prototype("CSSStyleDeclaration");
}

#define CSS_DECLARATION_INTERFACE(kind, name, host, metadata) \
    static Item host##_prototype(void) { \
        return dom_realm_constructor_prototype(#name); \
    }
#include "../../input/css/css_declaration_interfaces.def"
#undef CSS_DECLARATION_INTERFACE

static int radiant_element_style_prototype(Item, Item* out) {
    // native payload subtypes expose their common Web IDL interface prototype.
    *out = css_style_properties_prototype();
    return 1;
}

static Item radiant_css_rule_list_prototype(void) {
    return dom_realm_constructor_prototype("CSSRuleList");
}

#define RADIANT_TOKEN_LIST_METHOD(name, operation)                            \
static int name(Item receiver, Item* args, int argc, Item* out) {             \
    if (!out || !radiant_host_api || !radiant_host_api->realm ||              \
            !radiant_host_api->realm->token_list_operation) return 0;         \
    *out = radiant_host_api->realm->token_list_operation(                      \
        receiver, operation, args, argc);                                     \
    return 1;                                                                 \
}

RADIANT_TOKEN_LIST_METHOD(radiant_token_list_add, JUBE_DOM_TOKEN_LIST_ADD)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_remove, JUBE_DOM_TOKEN_LIST_REMOVE)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_toggle, JUBE_DOM_TOKEN_LIST_TOGGLE)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_contains, JUBE_DOM_TOKEN_LIST_CONTAINS)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_replace, JUBE_DOM_TOKEN_LIST_REPLACE)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_to_string, JUBE_DOM_TOKEN_LIST_TO_STRING)
RADIANT_TOKEN_LIST_METHOD(radiant_token_list_supports, JUBE_DOM_TOKEN_LIST_SUPPORTS)

static int radiant_token_list_value_get(Item receiver, Item* out) {
    return radiant_token_list_to_string(receiver, NULL, 0, out);
}

static int radiant_options_selected_index_get(Item receiver, Item* out) {
    if (!out) return 0;
    *out = dom_options_collection_selected_index(receiver);
    return 1;
}

static int radiant_options_selected_index_set(
        Item receiver, Item value, Item* out) {
    if (!out) return 0;
    *out = dom_options_collection_set_selected_index(receiver, value);
    return 1;
}

static int radiant_options_add(
        Item receiver, Item* args, int argc, Item* out) {
    if (!out) return 0;
    *out = dom_options_collection_add(receiver,
        radiant_iface_arg(args, argc, 0), radiant_iface_arg(args, argc, 1));
    return 1;
}

static const JubeMemberBind radiant_node_list_members[] = {
    BIND_GET_PROTO("length", radiant_collection_length_get),
    {"item", NULL, NULL, NULL, radiant_collection_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
};

static const JubeMemberBind radiant_html_collection_members[] = {
    BIND_GET_PROTO("length", radiant_collection_length_get),
    {"item", NULL, NULL, NULL, radiant_collection_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"named_item", "namedItem", NULL, NULL, radiant_collection_named_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
};

static const JubeMemberBind radiant_html_options_collection_members[] = {
    BIND_FIELD("length", radiant_collection_length_get),
    BIND_FIELD_SET("selected_index", radiant_options_selected_index_get,
                   radiant_options_selected_index_set),
    {"item", NULL, NULL, NULL, radiant_collection_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"named_item", "namedItem", NULL, NULL, radiant_collection_named_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    BIND_CALL("add", radiant_options_add),
};

NATIVE_PROPERTY_GET(radiant_attr_name, "name")
NATIVE_PROPERTY_GET(radiant_attr_local_name, "localName")
NATIVE_PROPERTY_GET(radiant_attr_namespace_uri, "namespaceURI")
NATIVE_PROPERTY_GET(radiant_attr_prefix, "prefix")
NATIVE_PROPERTY_GET(radiant_attr_owner_element, "ownerElement")
NATIVE_PROPERTY_GET(radiant_attr_specified, "specified")
NATIVE_PROPERTY_GET(radiant_attr_value, "value")
NATIVE_PROPERTY_GET(radiant_attr_node_value, "nodeValue")
NATIVE_PROPERTY_GET(radiant_attr_text_content, "textContent")
NATIVE_PROPERTY_SET(radiant_attr_value_set, "value")
NATIVE_PROPERTY_SET(radiant_attr_node_value_set, "nodeValue")
NATIVE_PROPERTY_SET(radiant_attr_text_content_set, "textContent")
#undef NATIVE_PROPERTY_GET
#undef NATIVE_PROPERTY_SET

static const JubeMemberBind radiant_attr_members[] = {
    BIND_FIELD("name", radiant_attr_name),
    BIND_FIELD_JS("local_name", "localName", radiant_attr_local_name),
    BIND_FIELD_JS("namespace_uri", "namespaceURI", radiant_attr_namespace_uri),
    BIND_FIELD("prefix", radiant_attr_prefix),
    BIND_FIELD_JS("owner_element", "ownerElement", radiant_attr_owner_element),
    BIND_FIELD("specified", radiant_attr_specified),
    BIND_FIELD_SET("value", radiant_attr_value, radiant_attr_value_set),
    BIND_FIELD_SET_JS("node_value", "nodeValue", radiant_attr_node_value, radiant_attr_node_value_set),
    BIND_FIELD_SET_JS("text_content", "textContent", radiant_attr_text_content, radiant_attr_text_content_set),
};

enum RadiantAttributeMapOperation { ATTRIBUTE_MAP_GET, ATTRIBUTE_MAP_SET, ATTRIBUTE_MAP_REMOVE };
static int radiant_attribute_map_operation(Item receiver, Item* args, int argc, Item* out,
        RadiantAttributeMapOperation operation, bool namespaced) {
    RootFrame roots(1);
    Rooted<Item> owner(roots, radiant_host_api->dom_catalog->attribute_collection_owner(receiver));
    if (item_is_error(owner.get())) { *out = owner.get(); return 1; }
    Item ns = namespaced ? radiant_iface_arg(args, argc, 0) : ItemNull;
    Item name = radiant_iface_arg(args, argc, namespaced ? 1 : 0);
    if (operation == ATTRIBUTE_MAP_SET) {
        *out = radiant_host_api->dom_catalog->set_attribute_node(owner.get(), radiant_iface_arg(args, argc, 0));
    } else if (operation == ATTRIBUTE_MAP_REMOVE) {
        *out = radiant_host_api->dom_catalog->remove_named_attribute(owner.get(), ns, name, (Item){.item = b2it(namespaced)});
    } else {
        *out = namespaced ? radiant_host_api->dom_catalog->get_attribute_node_ns(owner.get(), ns, name)
            : radiant_host_api->dom_catalog->get_attribute_node(owner.get(), name);
    }
    return 1;
}
#define ATTRIBUTE_MAP_METHOD(fn, operation, ns) \
    static int fn(Item receiver, Item* args, int argc, Item* out) { \
        return radiant_attribute_map_operation(receiver, args, argc, out, operation, ns); \
    }
ATTRIBUTE_MAP_METHOD(radiant_attribute_map_get, ATTRIBUTE_MAP_GET, false)
ATTRIBUTE_MAP_METHOD(radiant_attribute_map_get_ns, ATTRIBUTE_MAP_GET, true)
ATTRIBUTE_MAP_METHOD(radiant_attribute_map_set, ATTRIBUTE_MAP_SET, false)
ATTRIBUTE_MAP_METHOD(radiant_attribute_map_remove, ATTRIBUTE_MAP_REMOVE, false)
ATTRIBUTE_MAP_METHOD(radiant_attribute_map_remove_ns, ATTRIBUTE_MAP_REMOVE, true)
#undef ATTRIBUTE_MAP_METHOD

static const JubeMemberBind radiant_named_node_map_members[] = {
    BIND_FIELD("length", radiant_collection_length_get),
    {"item", NULL, NULL, NULL, radiant_collection_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    {"get_named_item", "getNamedItem", NULL, NULL, radiant_attribute_map_get, NULL, JUBE_MEMBER_REQUIRED_ARGS(1)},
    {"get_named_item_ns", "getNamedItemNS", NULL, NULL, radiant_attribute_map_get_ns, NULL, JUBE_MEMBER_REQUIRED_ARGS(2)},
    {"set_named_item", "setNamedItem", NULL, NULL, radiant_attribute_map_set, NULL, JUBE_MEMBER_REQUIRED_ARGS(1)},
    {"set_named_item_ns", "setNamedItemNS", NULL, NULL, radiant_attribute_map_set, NULL, JUBE_MEMBER_REQUIRED_ARGS(1)},
    {"remove_named_item", "removeNamedItem", NULL, NULL, radiant_attribute_map_remove, NULL, JUBE_MEMBER_REQUIRED_ARGS(1)},
    {"remove_named_item_ns", "removeNamedItemNS", NULL, NULL, radiant_attribute_map_remove_ns, NULL, JUBE_MEMBER_REQUIRED_ARGS(2)},
};

static const JubeMemberBind radiant_dom_token_list_members[] = {
    BIND_FIELD("length", radiant_collection_length_get),
    BIND_FIELD("value", radiant_token_list_value_get),
    {"item", NULL, NULL, NULL, radiant_collection_item, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
    BIND_CALL("add", radiant_token_list_add),
    BIND_CALL("remove", radiant_token_list_remove),
    BIND_CALL("toggle", radiant_token_list_toggle),
    BIND_CALL("contains", radiant_token_list_contains),
    BIND_CALL("replace", radiant_token_list_replace),
    BIND_CALL("supports", radiant_token_list_supports),
    {"to_string", "toString", NULL, NULL, radiant_token_list_to_string, NULL,
     JUBE_MEMBER_NON_ENUMERABLE},
};

static Item css_base_prototype(void) {
    return radiant_host_api->dom_catalog->rule_type_prototype("CSSRule");
}
static Item css_grouping_prototype(void) {
    return radiant_host_api->dom_catalog->rule_type_prototype("CSSGroupingRule");
}
static Item css_condition_prototype(void) {
    return radiant_host_api->dom_catalog->rule_type_prototype("CSSConditionRule");
}
#define CSS_RULE_INTERFACE(kind, name, base, legacy, host, host_base, shape) \
    static Item host##_prototype(void) { \
        return radiant_host_api->dom_catalog->rule_type_prototype(#name); \
    }
#define CSS_RULE_INTERFACE_ALIAS(...)
#include "../../input/css/css_rule_interfaces.def"
#undef CSS_RULE_INTERFACE_ALIAS
#undef CSS_RULE_INTERFACE

extern const JubeTypeBinding radiant_dom_type_bindings[] = {
    {"range", NULL, radiant_range_members,
     (int32_t)(sizeof(radiant_range_members) / sizeof(radiant_range_members[0])),
     NULL, NULL, NULL, NULL, radiant_range_prototype_seed, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"selection", NULL, radiant_selection_members,
     (int32_t)(sizeof(radiant_selection_members) / sizeof(radiant_selection_members[0])),
     NULL, NULL, NULL, NULL, radiant_selection_prototype_seed, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"inline_style", NULL, NULL, 0,
     rd_named_get, rd_named_set, rd_indexed_get, NULL, NULL, rd_named_has,
     rd_indexed_length, NULL, NULL, NULL, NULL, radiant_element_style_prototype},
    {"computed_style", NULL, NULL, 0,
     rd_named_get, rd_named_set, rd_indexed_get, NULL, NULL, rd_named_has,
     rd_indexed_length, NULL, NULL, NULL, NULL, radiant_element_style_prototype},
    {"stylesheet", NULL, radiant_stylesheet_members,
     (int32_t)(sizeof(radiant_stylesheet_members) / sizeof(radiant_stylesheet_members[0])),
     NULL, NULL, sh_indexed_get, NULL, radiant_css_stylesheet_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"css_rule", NULL, radiant_css_rule_members,
     (int32_t)(sizeof(radiant_css_rule_members) / sizeof(radiant_css_rule_members[0])),
     NULL, NULL, NULL, NULL, css_base_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"rule_style_decl", NULL, radiant_rule_decl_members,
     (int32_t)(sizeof(radiant_rule_decl_members) / sizeof(radiant_rule_decl_members[0])),
     rd_named_get, rd_named_set, rd_indexed_get, NULL, radiant_rule_declaration_prototype, rd_named_has,
     rd_indexed_length, NULL, NULL, NULL, NULL, NULL},
#define CSS_DECLARATION_INTERFACE(kind, name, host, metadata) \
    {#host, NULL, host##_members, \
     (int32_t)(sizeof(host##_members) / sizeof(host##_members[0])), \
     rd_named_get, rd_named_set, rd_indexed_get, NULL, host##_prototype, rd_named_has, \
     rd_indexed_length, NULL, NULL, NULL, NULL, NULL},
#include "../../input/css/css_declaration_interfaces.def"
#undef CSS_DECLARATION_INTERFACE
    {"event", NULL, radiant_event_members,
     (int32_t)(sizeof(radiant_event_members) / sizeof(radiant_event_members[0])),
     radiant_dom_event_named_get, radiant_dom_event_named_set, NULL, NULL, NULL,
     radiant_dom_event_named_has, NULL, NULL, NULL, NULL, NULL,
     radiant_dom_event_prototype},
    {"dom_node", NULL, radiant_dom_node_members,
     (int32_t)(sizeof(radiant_dom_node_members) / sizeof(radiant_dom_node_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL,
     NULL, NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"html_element", NULL, radiant_dom_html_element_members,
     (int32_t)(sizeof(radiant_dom_html_element_members) / sizeof(radiant_dom_html_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"attr", NULL, radiant_attr_members,
     (int32_t)(sizeof(radiant_attr_members) / sizeof(radiant_attr_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL,
     NULL, NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"character_data", NULL, radiant_dom_character_data_members,
     (int32_t)(sizeof(radiant_dom_character_data_members) / sizeof(radiant_dom_character_data_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"svg_element", NULL, radiant_dom_svg_element_members,
     (int32_t)(sizeof(radiant_dom_svg_element_members) / sizeof(radiant_dom_svg_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"input_element", NULL, radiant_dom_input_element_members,
     (int32_t)(sizeof(radiant_dom_input_element_members) / sizeof(radiant_dom_input_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"select_element", NULL, radiant_dom_select_element_members,
     (int32_t)(sizeof(radiant_dom_select_element_members) / sizeof(radiant_dom_select_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"textarea_element", NULL, radiant_dom_textarea_element_members,
     (int32_t)(sizeof(radiant_dom_textarea_element_members) / sizeof(radiant_dom_textarea_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"option_element", NULL, radiant_dom_option_element_members,
     (int32_t)(sizeof(radiant_dom_option_element_members) / sizeof(radiant_dom_option_element_members[0])),
     radiant_dom_node_named_get, radiant_dom_node_named_set, NULL, NULL, NULL, NULL,
     NULL, radiant_dom_host_has_property, radiant_dom_host_delete_property,
     radiant_dom_host_own_property_descriptor, radiant_dom_host_own_property_names,
     radiant_dom_node_prototype},
    {"document", NULL, radiant_document_members,
     (int32_t)(sizeof(radiant_document_members) / sizeof(radiant_document_members[0])),
     radiant_dom_document_host_get_property, radiant_dom_document_host_set_property,
     NULL, NULL, NULL, NULL, NULL,
     radiant_dom_document_host_has_property, radiant_dom_document_host_delete_property,
     radiant_dom_document_host_own_property_descriptor, radiant_dom_document_host_own_property_names,
     radiant_dom_document_prototype},
    {"foreign_document", NULL, NULL, 0,
     radiant_dom_document_host_get_property, radiant_dom_document_host_set_property,
     NULL, NULL, NULL, NULL, NULL,
     radiant_dom_document_host_has_property, radiant_dom_document_host_delete_property,
     radiant_dom_document_host_own_property_descriptor, radiant_dom_document_host_own_property_names,
     radiant_dom_document_prototype},
    {"velmt", NULL, radiant_velmt_members,
     (int32_t)(sizeof(radiant_velmt_members) / sizeof(radiant_velmt_members[0])),
     radiant_velmt_host_get_property, radiant_velmt_host_set_property,
     NULL, NULL, radiant_velmt_no_prototype, radiant_velmt_host_has_property,
     NULL, NULL, radiant_velmt_host_delete_property,
     radiant_velmt_host_own_property_descriptor, radiant_velmt_host_own_property_names,
     NULL},
    {"node_list", NULL, radiant_node_list_members,
     (int32_t)(sizeof(radiant_node_list_members) / sizeof(radiant_node_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_node_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"radio_node_list", NULL, radiant_node_list_members,
     (int32_t)(sizeof(radiant_node_list_members) / sizeof(radiant_node_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_radio_node_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"dom_rect_list", NULL, radiant_node_list_members,
     (int32_t)(sizeof(radiant_node_list_members) / sizeof(radiant_node_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_dom_rect_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"style_sheet_list", NULL, radiant_node_list_members,
     (int32_t)(sizeof(radiant_node_list_members) / sizeof(radiant_node_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_style_sheet_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"css_rule_list", NULL, radiant_node_list_members,
     (int32_t)(sizeof(radiant_node_list_members) / sizeof(radiant_node_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_css_rule_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"html_collection", NULL, radiant_html_collection_members,
     (int32_t)(sizeof(radiant_html_collection_members) /
               sizeof(radiant_html_collection_members[0])),
     dom_child_collection_named_get, NULL, radiant_collection_indexed_get, NULL,
     radiant_html_collection_prototype, dom_child_collection_named_has,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"html_options_collection", NULL, radiant_html_options_collection_members,
     (int32_t)(sizeof(radiant_html_options_collection_members) /
               sizeof(radiant_html_options_collection_members[0])),
     dom_child_collection_named_get, NULL, radiant_collection_indexed_get, NULL,
     radiant_html_options_collection_prototype, dom_child_collection_named_has,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"html_form_controls_collection", NULL, radiant_html_collection_members,
     (int32_t)(sizeof(radiant_html_collection_members) /
               sizeof(radiant_html_collection_members[0])),
     dom_child_collection_named_get, NULL, radiant_collection_indexed_get, NULL,
     radiant_html_form_controls_collection_prototype,
     dom_child_collection_named_has,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"named_node_map", NULL, radiant_named_node_map_members,
     (int32_t)(sizeof(radiant_named_node_map_members) /
               sizeof(radiant_named_node_map_members[0])),
     dom_child_collection_named_get, NULL, radiant_collection_indexed_get, NULL,
     radiant_named_node_map_prototype, dom_child_collection_named_has,
     NULL, NULL, NULL, NULL, NULL, NULL},
    {"dom_token_list", NULL, radiant_dom_token_list_members,
     (int32_t)(sizeof(radiant_dom_token_list_members) /
               sizeof(radiant_dom_token_list_members[0])),
     NULL, NULL, radiant_collection_indexed_get, NULL,
     radiant_dom_token_list_prototype, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL},
#define CSS_RULE_MEMBERS_empty NULL, 0
#define CSS_RULE_MEMBERS(shape) css_##shape##_members, \
    (int32_t)(sizeof(css_##shape##_members) / sizeof(css_##shape##_members[0]))
#define CSS_RULE_MEMBERS_style CSS_RULE_MEMBERS(style)
#define CSS_RULE_MEMBERS_declaration CSS_RULE_MEMBERS(declaration)
#define CSS_RULE_MEMBERS_scope CSS_RULE_MEMBERS(scope)
#define CSS_RULE_MEMBERS_property CSS_RULE_MEMBERS(property)
#define CSS_RULE_MEMBERS_namespace CSS_RULE_MEMBERS(namespace)
#define CSS_RULE_BIND(host, members, prototype) \
    {host, NULL, members, NULL, NULL, NULL, NULL, prototype, NULL, \
     NULL, NULL, NULL, NULL, NULL, NULL},
    CSS_RULE_BIND("css_grouping_rule", CSS_RULE_MEMBERS(grouping), css_grouping_prototype)
    CSS_RULE_BIND("css_condition_rule", CSS_RULE_MEMBERS(condition), css_condition_prototype)
#define CSS_RULE_INTERFACE(kind, name, base, legacy, host, host_base, shape) \
    CSS_RULE_BIND(#host, CSS_RULE_MEMBERS_##shape, host##_prototype)
#define CSS_RULE_INTERFACE_ALIAS(...)
#include "../../input/css/css_rule_interfaces.def"
#undef CSS_RULE_INTERFACE_ALIAS
#undef CSS_RULE_INTERFACE
#undef CSS_RULE_BIND
#undef CSS_RULE_MEMBERS_namespace
#undef CSS_RULE_MEMBERS_property
#undef CSS_RULE_MEMBERS_scope
#undef CSS_RULE_MEMBERS_declaration
#undef CSS_RULE_MEMBERS_style
#undef CSS_RULE_MEMBERS_empty
#undef CSS_RULE_MEMBERS

};

extern const int32_t radiant_dom_type_binding_count =
    (int32_t)(sizeof(radiant_dom_type_bindings) / sizeof(radiant_dom_type_bindings[0]));
