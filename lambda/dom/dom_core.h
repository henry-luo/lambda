#pragma once

/**
 * dom_core.h — the catalog's C surface (ES39/ES40).
 *
 * Every function here has the uniform shape `Item f(Item x argc)` the catalog
 * requires, so one declaration in dom_api.def can be expanded into a host-table
 * slot, a Jube `dom.*` registration, a `radiant.*` alias and a JS binding
 * without per-surface adapters. The `dom_core_*` bodies are the irreducible
 * mechanism (section 4); the `dom_fp_*` bodies are native fast paths for
 * derived operations, each answerable to its derivation string (ES43).
 *
 * Nothing here has a second implementation elsewhere: a body either *is* the
 * mechanism or is a two-line delegation to it.
 */

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flags carried by every catalog row (see dom_api.def header).
enum DomOpFlags {
    DOM_F_NEUTRAL  = 1u << 0,
    DOM_F_MUTATES  = 1u << 1,
    DOM_F_SNAPSHOT = 1u << 2,
    DOM_F_ENGINE   = 1u << 3,
    DOM_F_FASTPATH = 1u << 4,
};
#define DOM_NO_BODY nullptr

// --- core: node reads
Item dom_core_node_type(Item n);
Item dom_core_node_name(Item n);
Item dom_core_node_value(Item n);
Item dom_core_parent_node(Item n);
Item dom_core_first_child(Item n);
Item dom_core_last_child(Item n);
Item dom_core_next_sibling(Item n);
Item dom_core_previous_sibling(Item n);
Item dom_core_owner_document(Item n);
Item dom_core_same_node(Item a, Item b);
// --- core: attributes
Item dom_core_get_attribute(Item n, Item name);
Item dom_core_set_attribute(Item n, Item name, Item value);
Item dom_core_remove_attribute(Item n, Item name);
Item dom_core_attribute_names(Item n);
// --- core: tree mutation
Item dom_core_create_node(Item doc, Item type, Item name, Item data);
Item dom_core_insert_before(Item parent, Item node, Item ref);
Item dom_core_remove_child(Item parent, Item node);
Item dom_core_set_node_value(Item n, Item data);
Item dom_core_set_text_content(Item n, Item text);
Item dom_core_set_inner_html(Item n, Item html);
// --- core: match / parse / serialize
Item dom_core_matches(Item n, Item selector);
Item dom_core_parse_fragment(Item context, Item markup);
Item dom_core_serialize(Item n, Item outer);
// --- core: geometry
Item dom_core_bounding_box(Item n);
Item dom_core_client_rects(Item n);
Item dom_core_scroll_state(Item n);
Item dom_core_set_scroll_state(Item n, Item x, Item y);
Item dom_core_element_from_point(Item doc, Item x, Item y);
Item dom_core_scroll_into_view(Item n);
// --- core: range / selection composite reads
Item dom_core_range_boundaries(Item r);
Item dom_core_selection_boundaries(Item s);
// --- core: style
Item dom_core_computed_style(Item n, Item prop);
// --- core: listeners (uniform 4-arg shape over dom_events)
Item dom_add_event_listener_body(Item n, Item type, Item fn, Item opts);
Item dom_remove_event_listener_body(Item n, Item type, Item fn, Item opts);

// --- fast paths for derived operations (ES43: each equals its derivation)
Item dom_fp_first_element_child(Item n);
Item dom_fp_last_element_child(Item n);
Item dom_fp_next_element_sibling(Item n);
Item dom_fp_previous_element_sibling(Item n);
Item dom_fp_parent_element(Item n);
Item dom_fp_children(Item n);
Item dom_fp_child_nodes(Item n);
Item dom_fp_contains(Item a, Item b);
Item dom_fp_root_node(Item n);
Item dom_fp_document_element(Item n);
Item dom_fp_equal_node(Item a, Item b);
Item dom_fp_append_child(Item parent, Item child);
Item dom_fp_remove(Item n);
Item dom_fp_replace_child(Item parent, Item new_node, Item old_node);
Item dom_fp_create_element(Item doc, Item tag);
Item dom_fp_create_text_node(Item doc, Item data);
Item dom_fp_clone_node(Item n, Item deep);
Item dom_fp_text_content(Item n);
Item dom_fp_query_selector(Item root, Item selector);
Item dom_fp_query_selector_all(Item root, Item selector);
Item dom_fp_closest(Item n, Item selector);
Item dom_fp_get_element_by_id(Item root, Item id);
Item dom_fp_has_attribute(Item n, Item name);
Item dom_fp_inner_html(Item n);
Item dom_fp_outer_html(Item n);

// --- bodies the catalog references that are defined elsewhere in the core
// (range/selection bindings over radiant/dom_range, CSSOM, DOMParser). They
// are declared here, once, so every expansion of dom_api.def sees them.
Item js_range_set_start(Item self_v, Item node_v, Item offset_v);
Item js_range_set_end(Item self_v, Item node_v, Item offset_v);
Item js_range_collapse(Item self_v, Item to_start_v);
Item js_range_get_collapsed(Item self_v);
Item js_range_select_node(Item self_v, Item node_v);
Item js_range_select_node_contents(Item self_v, Item node_v);
Item js_range_to_string(Item self_v);
Item js_range_delete_contents(Item self_v);
Item js_selection_set_base_and_extent(Item self_v, Item anchor_node_v, Item anchor_off_v,
                                      Item focus_node_v, Item focus_off_v);
Item js_selection_modify(Item self_v, Item alter_v, Item dir_v, Item gran_v);
Item js_selection_collapse(Item self_v, Item node_v, Item offset_v);
Item js_selection_extend(Item self_v, Item node_v, Item offset_v);
Item js_selection_select_all_children(Item self_v, Item node_v);
Item dom_global_get_selection(void);
Item dom_create_range(void);
// engine-provided document loader (seam declared in dom.h, ESO80)
Item dom_engine_load_document(Item path);
// engine-provided catalog rows (F32): the host implements these, the core
// declares them, and a runtime without an engine gets the weak default.
Item dom_engine_get_state(Item n, Item name);
Item dom_engine_set_state(Item n, Item name, Item value);
Item dom_engine_request_change(Item n);
Item dom_engine_dispatch(Item n, Item event);
Item dom_engine_focused(Item n);
Item dom_engine_focus_set(Item n, Item from_keyboard);
Item dom_engine_activate_popover(Item a);
Item dom_engine_caret_operation(Item a, Item b, Item c);
Item dom_engine_clear_ime_preedit(Item a);
Item dom_engine_clipboard_text(void);
Item dom_engine_context_menu_target(Item a);
Item dom_engine_edit_insert_at_boundary(Item a, Item b);
Item dom_engine_edit_insert_break(Item a);
Item dom_engine_edit_replace_range(Item a, Item b, Item c, Item d);
Item dom_engine_edit_split_block(Item a);
Item dom_engine_key_intent(Item a, Item b);
Item dom_engine_navigation_destination(Item a, Item b, Item c);
Item dom_engine_open_context_menu(Item a, Item b);
Item dom_engine_request_navigation(Item a);
Item dom_engine_set_caret(Item a, Item b);
Item dom_engine_set_ime_preedit(Item a, Item b, Item c);
Item dom_engine_set_password_reveal(Item a, Item b, Item c);
Item dom_engine_tc_value(Item a);
Item dom_engine_ime_preedit(Item a);
Item dom_engine_tc_selection_start(Item a);
Item dom_engine_tc_selection_end(Item a);
Item dom_engine_edit_node(Item a);
Item dom_engine_edit_start(Item a);
Item dom_engine_edit_end(Item a);
Item dom_parser_parse_from_string(Item markup, Item mime);
Item dom_cssom_stylesheet_get_css_rules(Item sheet_item);
Item dom_cssom_insert_rule(Item sheet_item, Item text_arg, Item index_arg);
Item dom_cssom_delete_rule(Item sheet_item, Item index_arg);

#ifdef __cplusplus
}
#endif
