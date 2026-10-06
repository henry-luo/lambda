// VT byte strings are produced from a cell plan in Lambda. The host only
// writes these bytes and reports whether the full write completed (D7.1.6).

fn csi(amount, command) =>
  if (amount <= 0) "" else chr(27) ++ "[" ++ string(amount) ++ command

pub fn enable_paste() => chr(27) ++ "[?2004h"
pub fn disable_paste() => chr(27) ++ "[?2004l"

fn row_move(from_row, to_row) =>
  "\r" ++ (if (to_row < from_row) csi(from_row - to_row, "A")
            else csi(to_row - from_row, "B"))

fn paint_operation(cursor, operation) {
  if (operation.kind == "row") {
    {row: operation.index,
     bytes: cursor.bytes ++ row_move(cursor.row, operation.index) ++
            chr(27) ++ "[2K" ++ operation.text}
  }
  else {
    {row: operation.row,
     bytes: cursor.bytes ++ row_move(cursor.row, operation.row) ++
            csi(operation.col, "C")}
  }
}

pub fn encode(previous, planned) {
  if (len(planned.operations) == 0) ""
  else {
    let old_count = if (previous == null) 1 else len(previous.rows)
    let previous_row = if (previous == null) 0 else previous.caret.row
    let added = max(0, len(planned.screen.rows) - old_count)
    let reserve = if (added == 0) ""
                  else row_move(previous_row, old_count - 1) ++
                       join([for (i in 1 to added) "\r\n"], "")
    let start_row = if (added == 0) previous_row else old_count + added - 1
    let initial_cursor = {row: start_row, bytes: chr(27) ++ "[?7l" ++ reserve}
    let painted = reduce([initial_cursor, *planned.operations], paint_operation)
    painted.bytes ++ chr(27) ++ "[?7h"
  }
}

pub fn finish_frame(screen) =>
  row_move(screen.caret.row, len(screen.rows) - 1) ++ "\n"
