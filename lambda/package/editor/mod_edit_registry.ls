// The editor adds schema commands to the common web editing registry without
// teaching the DOM package about model policy (D7.2.5, D7.5.3).
import common: lambda.dom.edit_registry

pub let extensions = [
  {name: "insertcodeblock", aliases: ["insertcodeblock"], input_type: "insertCodeBlock",
   family: "insert_code_block", payload_kind: "value", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "insert_code_block", query_key: "none"}},
  {name: "formatblockquote", aliases: ["formatblockquote"], input_type: "formatBlockquote",
   family: "blockquote", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "boolean", dom: null,
   model: {supported: true, command_key: "wrap_blockquote", query_key: "boolean"}},
  {name: "formatliftblockquote", aliases: ["formatliftblockquote"], input_type: "formatLiftBlockquote",
   family: "blockquote", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "boolean", dom: null,
   model: {supported: true, command_key: "lift_blockquote", query_key: "boolean"}},
  {name: "inserttable", aliases: ["inserttable"], input_type: "insertTable",
   family: "table", payload_kind: "value", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "insert_table", query_key: "none"}},
  {name: "inserttablerow", aliases: ["inserttablerow"], input_type: "insertTableRow",
   family: "table", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "insert_table_row", query_key: "none"}},
  {name: "deletetablerow", aliases: ["deletetablerow"], input_type: "deleteTableRow",
   family: "table", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "delete_table_row", query_key: "none"}},
  {name: "inserttablecolumn", aliases: ["inserttablecolumn"], input_type: "insertTableColumn",
   family: "table", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "insert_table_column", query_key: "none"}},
  {name: "deletetablecolumn", aliases: ["deletetablecolumn"], input_type: "deleteTableColumn",
   family: "table", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "delete_table_column", query_key: "none"}},
  {name: "modeltogglemark", aliases: ["modeltogglemark"], input_type: "modelToggleMark",
   family: "format", payload_kind: "value", plaintext_rule: "disable",
   history_class: "format", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "boolean", dom: null,
   model: {supported: true, command_key: "toggle_mark", query_key: "boolean"}},
  {name: "modeldeletemultinode", aliases: ["modeldeletemultinode"], input_type: "modelDeleteMultiNode",
   family: "delete", payload_kind: "none", plaintext_rule: "disable",
   history_class: "structural", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "delete_multi_node", query_key: "none"}},
  {name: "modelmovetextselection", aliases: ["modelmovetextselection"], input_type: "modelMoveTextSelection",
   family: "drop", payload_kind: "drag", plaintext_rule: "allow",
   history_class: "clipboard", event_contract: "beforeinput-input",
   target_rule: "selection", state_kind: "none", dom: null,
   model: {supported: true, command_key: "move_text_selection", query_key: "none"}}
]

pub let registry = common.merge_extensions(extensions)

pub fn descriptor(spelling) => common.descriptor_in(registry, spelling)
pub fn descriptor_for_intent(input_type) =>
  common.descriptor_for_intent_in(registry, input_type)
