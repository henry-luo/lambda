edit <body> state loaded: false {
  <body <p id:"child-ready", if (loaded) "Lambda child ready" else "Loading">>
}
on load(evt) { loaded = evt.type == "load" }

<html apply(<body>, {mode:"edit"})>
