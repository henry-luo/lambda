// Pure line and session operations. A terminal frame template will call these
// from its on handlers; text positions are code points (S2.5.8, S12.1.3).
import cell: .layout

fn bounded(text, offset) => max(0, min(len(text), offset))

pub fn new_frame(prompt) => {prompt: prompt, text: ""}

pub fn insert_text(frame, offset, value) {
  let at = bounded(frame.text, offset)
  let text = slice(frame.text, 0, at) ++ value ++ slice(frame.text, at, len(frame.text))
  {frame: {*: frame, text: text}, caret: at + len(value)}
}

pub fn backspace(frame, offset) {
  let at = bounded(frame.text, offset)
  if (at == 0) {frame: frame, caret: at}
  else {
    let text = slice(frame.text, 0, at - 1) ++ slice(frame.text, at, len(frame.text))
    {frame: {*: frame, text: text}, caret: at - 1}
  }
}

pub fn delete_forward(frame, offset) {
  let at = bounded(frame.text, offset)
  if (at == len(frame.text)) {frame: frame, caret: at}
  else {
    let text = slice(frame.text, 0, at) ++ slice(frame.text, at + 1, len(frame.text))
    {frame: {*: frame, text: text}, caret: at}
  }
}

pub fn move_caret(frame, offset) => bounded(frame.text, offset)

// The old editor's word command lands at the next word start. Use Unicode
// categories here so byte offsets cannot split a character.
pub pn word_forward(value, offset) {
  var at = bounded(value, offset)
  while (at < len(value) and cell.is_word_char(slice(value, at, at + 1))) {
    at = at + 1
  }
  while (at < len(value) and not cell.is_word_char(slice(value, at, at + 1))) {
    at = at + 1
  }
  at
}

pub pn word_backward(value, offset) {
  var at = bounded(value, offset)
  while (at > 0 and not cell.is_word_char(slice(value, at - 1, at))) {
    at = at - 1
  }
  while (at > 0 and cell.is_word_char(slice(value, at - 1, at))) {
    at = at - 1
  }
  at
}

pub fn delete_range(frame, first_offset, end_offset) {
  let start_at = bounded(frame.text, min(first_offset, end_offset))
  let finish = bounded(frame.text, max(first_offset, end_offset))
  {frame: {*: frame,
           text: slice(frame.text, 0, start_at) ++
                 slice(frame.text, finish, len(frame.text))},
   caret: start_at, killed: slice(frame.text, start_at, finish)}
}

pub fn kill_to_end(frame, offset) => delete_range(frame, offset, len(frame.text))
pub fn kill_all(frame) => delete_range(frame, 0, len(frame.text))
pub pn kill_word_forward(frame, offset) {
  delete_range(frame, offset, word_forward(frame.text, offset))
}
pub pn kill_word_backward(frame, offset) {
  delete_range(frame, word_backward(frame.text, offset), offset)
}

pub fn transpose_chars(frame, offset) {
  let char_count = len(frame.text)
  if (char_count < 2) {frame: frame, caret: bounded(frame.text, offset)}
  else {
    let at = max(1, min(char_count - 1, offset))
    let first = slice(frame.text, at - 1, at)
    let second = slice(frame.text, at, at + 1)
    {frame: {*: frame,
             text: slice(frame.text, 0, at - 1) ++ second ++ first ++
                   slice(frame.text, at + 1, char_count)},
     caret: min(char_count, offset + 1)}
  }
}

pub pn edit_command(frame, offset, command) {
  let result =
    if (command == "Backspace") backspace(frame, offset)
    else if (command == "Delete" or command == "Control-D")
      delete_forward(frame, offset)
    else if (command == "Control-K") kill_to_end(frame, offset)
    else if (command == "Control-U") kill_all(frame)
    else if (command == "Control-W") kill_word_backward(frame, offset)
    else if (command == "Alt-D") kill_word_forward(frame, offset)
    else if (command == "Control-T") transpose_chars(frame, offset)
    else {frame: frame, caret: offset}
  {frame: result.frame, caret: result.caret,
   killed: if (result.killed == null) "" else result.killed,
   changed: result.frame.text != frame.text}
}

fn shared_prefix(left, right) {
  let limit = min(len(left), len(right))
  let matched = reduce([0, *[for (i in 0 to limit - 1) i]],
                       (count, i) =>
                         if (count == i and
                             slice(left, i, i + 1) == slice(right, i, i + 1))
                           i + 1 else count)
  slice(left, 0, matched)
}

pub fn completion_prefix(matches) =>
  if (len(matches) == 0) ""
  else reduce([matches[0], *slice(matches, 1, len(matches))],
              (prefix, candidate) => shared_prefix(prefix, candidate))

pub fn new_session(max_entries, ignore_dot) =>
  {history: [], max_entries: max_entries, ignore_dot: ignore_dot,
   remove_duplicates: false}

pub fn add_history(session, text) {
  let entries = session.history
  if (text == "" or session.max_entries <= 0 or
      (session.ignore_dot and starts_with(text, ".")) or
      (len(entries) > 0 and entries[len(entries) - 1] == text)) session
  else {
    let distinct = if (session.remove_duplicates)
                     [for (entry in entries) if (entry != text) entry]
                   else entries
    let appended = [*distinct, text]
    let kept = slice(appended, max(0, len(appended) - session.max_entries), len(appended))
    {*: session, history: kept}
  }
}

// Navigation keeps the unsent draft separate from committed session entries.
pub fn history_navigation() => {index: null, draft: ""}

pub fn history_previous(session, navigation, current_text) {
  let entries = session.history
  if (len(entries) == 0) {navigation: navigation, text: current_text}
  else {
    let index = if (navigation.index == null) len(entries) - 1
                else max(0, navigation.index - 1)
    let draft = if (navigation.index == null) current_text else navigation.draft
    {navigation: {index: index, draft: draft}, text: entries[index]}
  }
}

pub fn history_next(session, navigation, current_text) {
  if (navigation.index == null) {navigation: navigation, text: current_text}
  else if (navigation.index + 1 >= len(session.history))
    {navigation: history_navigation(), text: navigation.draft}
  else {
    let index = navigation.index + 1
    {navigation: {*: navigation, index: index}, text: session.history[index]}
  }
}

pub fn new_undo() => {past: [], future: []}

pub fn remember(undo_stack, frame, caret) =>
  {past: [*undo_stack.past, {frame: frame, caret: caret}], future: []}

pub fn undo_edit(undo_stack, frame, caret) {
  if (len(undo_stack.past) == 0)
    {undo_stack: undo_stack, frame: frame, caret: caret}
  else {
    let index = len(undo_stack.past) - 1
    let restored = undo_stack.past[index]
    {undo_stack: {past: slice(undo_stack.past, 0, index),
                  future: [*undo_stack.future, {frame: frame, caret: caret}]},
     frame: restored.frame, caret: restored.caret}
  }
}

pub fn redo_edit(undo_stack, frame, caret) {
  if (len(undo_stack.future) == 0)
    {undo_stack: undo_stack, frame: frame, caret: caret}
  else {
    let index = len(undo_stack.future) - 1
    let restored = undo_stack.future[index]
    {undo_stack: {past: [*undo_stack.past, {frame: frame, caret: caret}],
                  future: slice(undo_stack.future, 0, index)},
     frame: restored.frame, caret: restored.caret}
  }
}
