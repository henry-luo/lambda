// Shipped terminal facade and session template (S12.1.3, D7.2.4).
import model: .terminal.model
import .terminal.frame
import layout: .terminal.layout
import display: .terminal.display
import vt: .terminal.vt
import protocol: .terminal.protocol

pn deliver_decoded(events) {
  for (event in events) {
    if (event.kind == "text") {
      emit("readline_frame_event", {name: "text_input", event: event})
    }
    else if (event.kind == "key" and event.key != "PasteStart" and
             event.key != "PasteEnd") {
      emit("readline_frame_event", {name: "keydown", event: event})
    }
    else { emit("readline_unknown", event) }
  }
}

// The source holds one stable frame model. Replacing that value creates the
// next frame lifetime while the terminal template state remains session-owned.
view <readline_terminal> state history: [], active_frame: null,
                               history_limit: null, history_ignore_dot: null,
                               history_unique: false,
                               acknowledged: null, acknowledged_frame_id: null,
                               acknowledged_prompt: null,
                               prompt_override: null,
                               question_active: false,
                               question_old_prompt: null,
                               paused: false,
                               columns: 80, tty: true,
                               decoder: {pending: [], utf_expected: 0, saw_cr: false},
                               kill_ring: [] {
  let current = if (active_frame == null) ~.frame else active_frame;
  let rendered = apply(current, {mode: "edit"});
  let screen = layout.layout_frame(
    {prompt: rendered.prompt, text: rendered.text},
    rendered.caret.offset, columns, 8);
  let planned = display.plan(acknowledged, screen);
  <terminal id: ~.id, active_frame_id: current.id, paused: paused,
            display_prompt: rendered.prompt, screen: screen,
            bytes: if (tty)
                     (if (acknowledged_frame_id != current.id)
                        vt.enable_paste() else "") ++
                     vt.encode(acknowledged, planned)
                   else if (acknowledged_frame_id != current.id or
                            acknowledged_prompt != rendered.prompt) rendered.prompt
                   else "",
            wait_ms: protocol.pending_timeout(decoder),
            finish_bytes: if (tty)
                            vt.disable_paste() ++ vt.finish_frame(screen)
                          else "",
            interrupt_bytes: if (tty)
                               vt.disable_paste() ++ vt.finish_frame(screen)
                             else "",
            close_bytes: if (tty)
                           vt.disable_paste() ++ vt.finish_frame(screen)
                         else "",
            release_bytes: if (tty) vt.disable_paste() else "", rendered>
}
on readline_bytes(bytes) {
  let decoded = protocol.feed(decoder, bytes)
  decoder = decoded.decoder
  deliver_decoded(decoded.events)
}
on readline_key(evt) {
  let named = if (evt.name == "left") "ArrowLeft"
              else if (evt.name == "right") "ArrowRight"
              else if (evt.name == "up") "ArrowUp"
              else if (evt.name == "down") "ArrowDown"
              else if (evt.name == "return" or evt.name == "enter") "Enter"
              else if (evt.name == "backspace") "Backspace"
              else if (evt.name == "delete") "Delete"
              else if (evt.name == "tab") "Tab"
              else if (evt.name == "home") "Home"
              else if (evt.name == "end") "End"
              else evt.name
  let command = if (evt.sequence == chr(31)) "Control-Z"
                else if (evt.sequence == chr(30)) "Control-Shift-Z"
                else if (evt.ctrl and len(named) == 1)
                  "Control-" ++ upper(named)
                else named
  emit("readline_frame_event",
       {name: "keydown", event: {key: command, ctrl: evt.ctrl,
                                  alt: evt.meta, shift: evt.shift}})
}
on readline_flush() {
  let decoded = protocol.flush_pending(decoder)
  decoder = decoded.decoder
  deliver_decoded(decoded.events)
}
on readline_end() {
  let completed = protocol.end_input(decoder)
  decoder = completed.decoder
  deliver_decoded(completed.events)
  let current = if (active_frame == null) ~.frame else active_frame
  let rendered = apply(current, {mode: "edit"})
  if (rendered.text != "") {
    emit("readline_submit", {frame_id: current.id, text: rendered.text})
  }
  emit("readline_close", {terminal_id: ~.id})
}
on readline_submit(evt) {
  let updated = model.add_history(
    {history: history,
     max_entries: if (history_limit == null) ~.max_entries else history_limit,
     ignore_dot: if (history_ignore_dot == null) ~.ignore_dot
                 else history_ignore_dot,
     remove_duplicates: history_unique}, evt.text)
  history = updated.history
  let current = if (active_frame == null) ~.frame else active_frame
  let was_question = question_active
  if (was_question) {
    question_active = false
    prompt_override = question_old_prompt
    emit("readline_answer", {terminal_id: ~.id, frame_id: evt.frame_id,
                              text: evt.text})
  }
  if (not was_question) {
    emit("readline_line", {terminal_id: ~.id, frame_id: evt.frame_id,
                            text: evt.text})
  }
  // A transport chunk can contain several lines; each accepted line needs a
  // fresh edit instance before the decoder delivers the next event.
  active_frame = <readline_frame id: evt.frame_id + 1,
                                 prompt: if (prompt_override == null)
                                           current.prompt else prompt_override,
                                 history: history,
                                 has_completer: current.has_completer>
}
on readline_question(query) {
  if (not question_active) {
    let current = if (active_frame == null) ~.frame else active_frame
    question_old_prompt = if (prompt_override == null)
                            current.prompt else prompt_override
    question_active = true
    prompt_override = query
    emit("readline_frame_event", {name: "set_prompt", event: query})
  }
}
on readline_prompt(value) {
  prompt_override = value
  emit("readline_frame_event", {name: "set_prompt", event: value})
}
on readline_history_size(value) { history_limit = max(0, value) }
on readline_history_ignore_dot(value) { history_ignore_dot = value }
on readline_history_unique(value) { history_unique = value }
on readline_pause() { paused = true }
on readline_resume() { paused = false }
on readline_kill(evt) {
  if (evt.text != "") {
    let appended = [*kill_ring, evt.text]
    kill_ring = slice(appended, max(0, len(appended) - 10), len(appended))
  }
}
on readline_yank() {
  if (len(kill_ring) > 0) {
    emit("readline_frame_event",
         {name: "text_input", event: {text: kill_ring[len(kill_ring) - 1]}})
  }
}
on readline_eof() { emit("readline_close", {terminal_id: ~.id}) }
on readline_interrupt() {
  emit("readline_cancel", {terminal_id: ~.id})
}
on readline_next(evt) {
  prompt_override = null
  active_frame = <readline_frame id: evt.frame_id, prompt: evt.prompt,
                                 history: history,
                                 has_completer: evt.completer == true>
}
on readline_presented(presented) {
  acknowledged = presented.screen
  acknowledged_frame_id = presented.active_frame_id
  acknowledged_prompt = presented.display_prompt
}
on readline_resize(width) { columns = max(1, width) }
on readline_device(is_tty) { tty = is_tty }

pub fn terminal(session_id, frame_id, prompt) =>
  <readline_terminal id: session_id, max_entries: 500, ignore_dot: true,
    frame: <readline_frame id: frame_id, prompt: prompt, history: [],
                           has_completer: false>>

pub fn new_frame(prompt) => model.new_frame(prompt)
pub fn insert_text(frame, offset, value) => model.insert_text(frame, offset, value)
pub fn backspace(frame, offset) => model.backspace(frame, offset)
pub fn delete_forward(frame, offset) => model.delete_forward(frame, offset)
pub fn move_caret(frame, offset) => model.move_caret(frame, offset)
pub pn word_forward(value, offset) { model.word_forward(value, offset) }
pub pn word_backward(value, offset) { model.word_backward(value, offset) }
pub fn delete_range(frame, first_offset, end_offset) =>
  model.delete_range(frame, first_offset, end_offset)
pub fn kill_to_end(frame, offset) => model.kill_to_end(frame, offset)
pub fn kill_all(frame) => model.kill_all(frame)
pub pn kill_word_forward(frame, offset) { model.kill_word_forward(frame, offset) }
pub pn kill_word_backward(frame, offset) { model.kill_word_backward(frame, offset) }
pub fn transpose_chars(frame, offset) => model.transpose_chars(frame, offset)
pub pn edit_command(frame, offset, command) {
  model.edit_command(frame, offset, command)
}
pub fn completion_prefix(matches) => model.completion_prefix(matches)
pub fn new_session(max_entries, ignore_dot) => model.new_session(max_entries, ignore_dot)
pub fn add_history(session, text) => model.add_history(session, text)
pub fn history_navigation() => model.history_navigation()
pub fn history_previous(session, navigation, current_text) =>
  model.history_previous(session, navigation, current_text)
pub fn history_next(session, navigation, current_text) =>
  model.history_next(session, navigation, current_text)
pub fn new_undo() => model.new_undo()
pub fn remember(undo_stack, frame, caret) => model.remember(undo_stack, frame, caret)
pub fn undo_edit(undo_stack, frame, caret) => model.undo_edit(undo_stack, frame, caret)
pub fn redo_edit(undo_stack, frame, caret) => model.redo_edit(undo_stack, frame, caret)
