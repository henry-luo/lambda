// D7.2.5: both editing backends consume one pure descriptor/request/action
// protocol; representation-specific state is absent from these values.
import registry: lambda.dom.edit_registry
import request: lambda.dom.edit_request
import action: lambda.dom.edit_action
import result: lambda.dom.edit_result
import policy: lambda.dom.edit_text_policy

let paste = registry.descriptor_for_intent("insertFromPaste")
let normalized = request.from_event({origin: "platform", data: "plain", html: "<b>rich</b>",
                                     mime: "text/html", edit_plaintext_only: true}, paste)
let lowered = action.lower_request(normalized, paste)
let model_result = result.model_applied(true, true, true, "typing",
    {kind: 'text', anchor: {path: [0, 0], offset: 2}, head: {path: [0, 0], offset: 2}}, 4)
let collision = {
    name: "extension", aliases: ["bold"], input_type: "extensionEdit",
    family: "extension", target_rule: "selection", dom: null,
    model: {supported: true, command_key: "extension", query_key: "none"}
}
let invalid_target = {
    name: "invalidtarget", aliases: [], input_type: "invalidTargetEdit",
    family: "extension", target_rule: "nearest-looking-node", dom: null,
    model: {supported: true, command_key: "invalid_target", query_key: "none"}
}

{
  registry_valid: registry.validate(registry.registry),
  word_delete: {name: registry.descriptor_for_intent("deleteWordBackward").name,
                target: registry.descriptor_for_intent("deleteWordBackward").target_rule,
                model: registry.descriptor_for_intent("deleteWordBackward").model.command_key},
  soft_line_delete: {target: registry.descriptor_for_intent("deleteSoftLineForward").target_rule,
                     model: registry.descriptor_for_intent("deleteSoftLineForward").model.command_key},
  request: {input_type: normalized.input_type, origin: normalized.origin,
            data: normalized.data, html: normalized.html, mime: normalized.mime,
            target: normalized.target_rule},
  action: {family: lowered.family, origin: lowered.origin,
           history: lowered.history_class, data: lowered.payload.data},
  result: {claimed: model_result.claimed, changed: model_result.changed,
           selection_space: model_result.selection_space,
           revision: model_result.model_revision,
           offset: model_result.selection_after.head.offset},
  collision_rejected: registry.merge_extensions([collision]) == null,
  unknown_target_rejected: registry.merge_extensions([invalid_target]) == null,
  text: {crlf: policy.sanitize("a\r\nb", true),
         word_start: policy.word_start("one...two", 9),
         fitted: policy.fit_insertion("abcd", 4, 1, 5)}
}
