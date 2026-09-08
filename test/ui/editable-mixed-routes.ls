// mixed route probe: the template host owns its source model while the sibling
// standard contenteditable host uses the dom package default.

edit <mixed_template_editor> state text: "template", status: "ready" {
  <div id:"template-host", contenteditable:"true", tabindex:"0", text>
  <output id:"template-state", status ++ ":" ++ text>
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
    <div id:"mixed-root"
, apply(<mixed_template_editor>, {mode: "edit"});
      <div id:"dom-host", contenteditable:"true", tabindex:"0",
        "dom"
        <span id:"dom-false-island", contenteditable:"false", "locked">
      >
    >
  >
>
