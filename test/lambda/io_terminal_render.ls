import layout: lambda.io.terminal.layout

let wide = layout.layout_frame({prompt: "> ", text: "ab界é"}, 5, 8, 4)
let wrap = layout.layout_frame({prompt: "> ", text: "ab界"}, 3, 5, 4)
let edge = layout.layout_frame({prompt: "> ", text: "123"}, 3, 5, 4)
let narrow = layout.layout_frame({prompt: "> ", text: "123"}, 3, 4, 4)
let tabs = layout.layout_frame({prompt: "> ", text: "\tx"}, 2, 8, 4);
[
  lambda.io.cell_width(ord("界")) == 2,
  lambda.io.cell_width(ord("́")) == 0,
  lambda.io.unicode_category(ord("界")) == "Lo",
  layout.is_word_char("é") and layout.is_word_char("_") and not layout.is_word_char("!"),
  wide.rows == ["> ab界é"] and wide.caret == {row: 0, col: 7},
  wrap.rows == ["> ab", "界"] and wrap.caret == {row: 1, col: 2},
  edge.rows == ["> 123", ""] and edge.caret == {row: 1, col: 0},
  narrow.rows == ["> 12", "3"] and narrow.caret == {row: 1, col: 1},
  tabs.rows == [">   x"] and tabs.caret == {row: 0, col: 5}
]
