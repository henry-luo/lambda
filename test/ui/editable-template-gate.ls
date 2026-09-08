// The public notification stays pure; the snapshotted internal action owns the
// model update after cancellation has closed.

edit <editable_template_probe> state text: "seed", status: "ready" {
  <div id:"surface", contenteditable:"true", tabindex:"0", text>
  <output id:"state", status ++ ":" ++ text>
}
on beforeinput(evt) {
  return 'pass'
}
on editaction(evt) {
  text = evt.input_intent.data
  status = evt.input_type
  return {supported: true, enabled: true, claimed: true, changed: true,
          selection_changed: false, history_recorded: false,
          selection_after: null, selection_space: "none", model_revision: 1}
}

<html
  <body
    apply(<editable_template_probe>, {mode: "edit"})
  >
>
