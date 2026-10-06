// Pure frame-to-cell layout. The runtime supplies utf8proc scalar metadata;
// wrapping, tabs and caret coordinates stay in Lambda (S12.1.3, D7.1.6).

fn codepoint_width(ch) {
  let width = lambda.io.cell_width(ord(ch))
  if (width == null or width < 0) 0 else width
}

pub fn is_word_char(ch) {
  let category = lambda.io.unicode_category(ord(ch))
  category == "Lu" or category == "Ll" or category == "Lt" or
  category == "Lm" or category == "Lo" or category == "Nd" or
  category == "Nl" or category == "No" or category == "Pc"
}

fn finish_row(screen) =>
  {*: screen, rows: [*screen.rows, screen.line], line: "", col: 0,
   row: screen.row + 1}

fn append_cell(screen, ch, width) {
  let ready = if (width > 0 and screen.col + width > screen.columns)
                finish_row(screen) else screen
  let updated = {*: ready, line: ready.line ++ ch, col: ready.col + width}
  if (updated.col == updated.columns) finish_row(updated) else updated
}

fn step(screen, ch, tabstop) {
  if (ch == "\n") finish_row(screen)
  else if (ch == "\t") {
    let spaces_needed = tabstop - (screen.col % tabstop)
    // Treat each tab cell separately so a tab at the edge wraps exactly once.
    reduce([screen, *[for (i in 1 to spaces_needed) " "]],
           (current, cell) => append_cell(current, cell, 1))
  }
  else if (ord(ch) < 32 or ord(ch) == 127) screen
  else append_cell(screen, ch, codepoint_width(ch))
}

fn scan(screen, value, tabstop) =>
  reduce([screen, *[for (i in 0 to len(value) - 1) slice(value, i, i + 1)]],
         (current, ch) => step(current, ch, tabstop))

// offset is a code-point position in editable text, independent of prompt.
pub fn layout_frame(frame, offset, columns, tabstop) {
  let width = max(1, columns)
  let tabs = max(1, tabstop)
  let initial_screen = {rows: [], line: "", row: 0, col: 0, columns: width}
  let prompted = scan(initial_screen, frame.prompt, tabs)
  let caret_screen = scan(prompted, slice(frame.text, 0, offset), tabs)
  let final_screen = scan(caret_screen, slice(frame.text, offset, len(frame.text)), tabs)
  {rows: [*final_screen.rows, final_screen.line],
   caret: {row: caret_screen.row, col: caret_screen.col},
   columns: width}
}
