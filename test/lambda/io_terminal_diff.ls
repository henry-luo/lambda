import layout: lambda.io.terminal.layout
import display: lambda.io.terminal.display

let frame = {prompt: "> ", text: "ab界"}
let first = layout.layout_frame(frame, 3, 5, 4)
let initial = display.plan(null, first)
let same = display.plan(first, first)
let smaller = layout.layout_frame({prompt: "> ", text: "a"}, 1, 5, 4)
let changed = display.plan(first, smaller);
[
  len(initial.operations) == 3,
  initial.operations[0] == {kind: "row", index: 0, text: "> ab"},
  initial.operations[1] == {kind: "row", index: 1, text: "界"},
  initial.operations[2] == {kind: "caret", row: 1, col: 2},
  len(same.operations) == 0,
  changed.operations[0] == {kind: "row", index: 0, text: "> a"},
  changed.operations[1] == {kind: "row", index: 1, text: ""},
  changed.operations[2] == {kind: "caret", row: 0, col: 3}
]
