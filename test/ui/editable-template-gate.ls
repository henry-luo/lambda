// focused ordinary beforeinput probe for a Lambda-owned contenteditable model.
// the handler owns the model update; the dom package must not apply a default.

edit <editable_template_probe> state text: "seed", status: "ready" {
  <div id:"surface", contenteditable:"true", tabindex:"0", text>
  <output id:"state", status ++ ":" ++ text>
}
on beforeinput(evt) {
  if (evt.input_intent != null) {
    text = evt.input_intent.data
    status = evt.input_type
  }
  return 'prevent-default'
}

<html
  <body
    apply(<editable_template_probe>, {mode: "edit"})
  >
>
