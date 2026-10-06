// One logical input frame; its on handlers own mutable edit state and its body
// is the desired console snapshot (S12.1.3, S9.1.4).
import edits: .model

fn utf16_offset(value, codepoint_offset) =>
  reduce([0, *[for (i in 0 to codepoint_offset - 1)
                 slice(value, i, i + 1)]],
         (units, ch) => units + if (ord(ch) > 65535) 2 else 1)

edit <readline_frame> state buffer: "", caret: 0,
                            completion_generation: 0,
                            undo_stack: {past: [], future: []},
                            navigation: {index: null, draft: ""},
                            display_prompt: null {
  <frame id: ~.id,
         prompt: if (display_prompt == null) ~.prompt else display_prompt,
         text: buffer,
         caret: {region: "input", offset: caret,
                 utf16_offset: utf16_offset(buffer, caret)}>
}
on set_prompt(value) { display_prompt = value }
on text_input(evt) {
  completion_generation = completion_generation + 1
  undo_stack = edits.remember(undo_stack, {prompt: ~.prompt, text: buffer}, caret)
  let changed = edits.insert_text({prompt: ~.prompt, text: buffer}, caret, evt.text)
  buffer = changed.frame.text
  caret = changed.caret
  navigation = edits.history_navigation()
}
on keydown(evt) {
  completion_generation = completion_generation + 1
  let command = if ((evt.alt and evt.key == "d") or
                    (evt.ctrl and evt.key == "Delete")) "Alt-D"
                else if (evt.alt and evt.key == "Backspace") "Control-W"
                else evt.key
  let change = edits.edit_command({prompt: ~.prompt, text: buffer}, caret, command)
  if (change.changed) {
    undo_stack = edits.remember(undo_stack, {prompt: ~.prompt, text: buffer}, caret)
    buffer = change.frame.text
    caret = change.caret
    navigation = edits.history_navigation()
    if (change.killed != "") { emit("readline_kill", {text: change.killed}) }
  }
  else if (evt.key == "ArrowLeft" or evt.key == "Control-B") {
    caret = if (evt.ctrl or evt.alt) edits.word_backward(buffer, caret)
            else edits.move_caret({text: buffer}, caret - 1)
  }
  else if (evt.key == "ArrowRight" or evt.key == "Control-F") {
    caret = if (evt.ctrl or evt.alt) edits.word_forward(buffer, caret)
            else edits.move_caret({text: buffer}, caret + 1)
  }
  else if (evt.alt and evt.key == "b") { caret = edits.word_backward(buffer, caret) }
  else if (evt.alt and evt.key == "f") { caret = edits.word_forward(buffer, caret) }
  else if (evt.key == "Home" or evt.key == "Control-A") { caret = 0 }
  else if (evt.key == "End" or evt.key == "Control-E") { caret = len(buffer) }
  else if (evt.key == "ArrowUp") {
    let selected = edits.history_previous({history: ~.history}, navigation, buffer)
    navigation = selected.navigation
    buffer = selected.text
    caret = len(buffer)
  }
  else if (evt.key == "ArrowDown") {
    let selected = edits.history_next({history: ~.history}, navigation, buffer)
    navigation = selected.navigation
    buffer = selected.text
    caret = len(buffer)
  }
  else if (evt.key == "Control-Z") {
    let restored = edits.undo_edit(undo_stack, {prompt: ~.prompt, text: buffer}, caret)
    undo_stack = restored.undo_stack
    buffer = restored.frame.text
    caret = restored.caret
  }
  else if (evt.key == "Control-Shift-Z") {
    let restored = edits.redo_edit(undo_stack, {prompt: ~.prompt, text: buffer}, caret)
    undo_stack = restored.undo_stack
    buffer = restored.frame.text
    caret = restored.caret
  }
  else if (evt.key == "Control-Y") { emit("readline_yank", {}) }
  else if (evt.key == "Tab") {
    if (~.has_completer) {
      emit("readline_completion_request",
           {frame_id: ~.id, generation: completion_generation,
            line: slice(buffer, 0, caret)})
    }
    else {
      // Without a provider, Tab is one ordinary insertion/undo unit.
      emit("readline_frame_event", {name: "text_input", event: {text: "\t"}})
    }
  }
  else if (evt.key == "Control-C") { emit("readline_interrupt", {}) }
  else if (evt.key == "Control-D" and buffer == "") { emit("readline_eof", {}) }
  else if (evt.key == "Enter") { emit("readline_submit", {frame_id: ~.id, text: buffer}) }
}
on completion_reply(evt) {
  if (evt.frame_id == ~.id and evt.generation == completion_generation and
      len(evt.matches) > 0) {
    let start = caret - len(evt.complete_on)
    if (start >= 0 and slice(buffer, start, caret) == evt.complete_on) {
      let replacement = edits.completion_prefix(evt.matches)
      if (len(evt.matches) == 1 or len(replacement) > len(evt.complete_on)) {
        undo_stack = edits.remember(undo_stack,
                                    {prompt: ~.prompt, text: buffer}, caret)
        buffer = slice(buffer, 0, start) ++ replacement ++
                 slice(buffer, caret, len(buffer))
        caret = start + len(replacement)
        completion_generation = completion_generation + 1
      }
    }
  }
}
