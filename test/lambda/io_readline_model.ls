import rl: lambda.io.terminal

let blank = rl.new_frame("λ> ")
let inserted = rl.insert_text(blank, 0, "aé界")
let erased = rl.backspace(inserted.frame, inserted.caret)
let deleted = rl.delete_forward(erased.frame, 1)
let appended = rl.insert_text(deleted.frame, deleted.caret, "!")

len(blank.text)
inserted.frame.text
0
inserted.caret
erased.frame.text
0
erased.caret
deleted.frame.text
0
appended.frame.text
0
rl.move_caret(appended.frame, 99)

let s0 = rl.new_session(2, true)
let s1 = rl.add_history(s0, "first")
let s2 = rl.add_history(s1, "first")
let s3 = rl.add_history(s2, ".help")
let s4 = rl.add_history(s3, "second")
let s5 = rl.add_history(s4, "third")
len(s2.history)
len(s3.history)
s5.history[0]
0
s5.history[1]

let nav0 = rl.history_navigation()
let nav1 = rl.history_previous(s5, nav0, "draft")
let nav2 = rl.history_previous(s5, nav1.navigation, nav1.text)
let nav3 = rl.history_next(s5, nav2.navigation, nav2.text)
let nav4 = rl.history_next(s5, nav3.navigation, nav3.text)
let undo0 = rl.remember(rl.new_undo(), blank, 0)
let undo1 = rl.undo_edit(undo0, inserted.frame, inserted.caret)
let redo1 = rl.redo_edit(undo1.undo_stack, undo1.frame, undo1.caret);
[
  nav1.text == "third" and nav1.navigation.draft == "draft",
  nav2.text == "second",
  nav3.text == "third",
  nav4.text == "draft" and nav4.navigation.index == null,
  undo1.frame.text == "" and undo1.caret == 0,
  redo1.frame.text == "aé界" and redo1.caret == 3,
  rl.completion_prefix(["hello", "help"]) == "hel"
]
