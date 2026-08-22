// Focused template-route probe for the contenteditable transaction gate.
// The handler owns the model update; Radiant must not apply a DOM fallback.

edit <editable_template_probe> state text: "seed", status: "ready" {
  <div id:"surface", contenteditable:"true", tabindex:"0", text>
  <output id:"state"; status ++ ":" ++ text>
}
on beforeinput(evt) {
  if (evt.input_intent != null) {
    text = evt.input_intent.data
    status = evt.input_type
  }
}

<html
  <body
    apply(<editable_template_probe>, {mode: "edit"})
  >
>
