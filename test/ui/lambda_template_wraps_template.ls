// A template whose body applies another template directly returns that
// template's element. Both templates handle its events, innermost first, as
// nested elements would: the inner's own handlers run, an event the inner
// emits reaches the outer (which swaps the inner for another template), and
// a click inside the new inner reaches the outer's own click handler.
import dom

view <wt_button> {
  <button id: ~.id, ~.label>
}
on click(evt) {
  emit("wt_cmd", {cmd: ~.id})
}

edit <wt_child> state count: ~.start {
  <div id: "child",
    <span id: "count", string(count)>
    apply(<wt_button id: "inc", label: "inc">)
    apply(<wt_button id: "switch", label: "switch">)
  >
}
on wt_cmd(req) {
  if (req.cmd == "switch") {
    emit("wt_switch", {count: count})
    return
  }
  count = count + 1
}

view <wt_other> {
  <div id: "other",
    <span id: "got", string(~.got)>
    <span id: "outer-clicks", string(~.clicks)>
    <button id: "poke", "poke">
  >
}

edit <wt_root> state mode: 'child', carried: 0, clicks: 0 {
  if (mode == 'child') apply(<wt_child start: 5>, {mode: "edit"})
  else apply(<wt_other got: carried, clicks: clicks>)
}
// a re-render of this template re-applies the inner one afresh, so only the
// stateless inner counts here. The switch click counts too: the inner's
// emit is handled before this outer click handler runs.
on click(evt) {
  if (mode == 'other') { clicks = clicks + 1 }
}
on wt_switch(req) {
  carried = req.count
  mode = 'other'
}

<html <body apply(<wt_root>, {mode: "edit"})>>
