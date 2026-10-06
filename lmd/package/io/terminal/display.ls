// Desired-versus-acknowledged cell diff. Callers advance the acknowledged
// value only after the complete write succeeds (S12.1.3).

fn row_at(screen, index) =>
  if (screen == null or index >= len(screen.rows)) null else screen.rows[index]

pub fn plan(previous, desired) {
  if (previous == desired) {operations: [], screen: desired}
  else {
    let old_count = if (previous == null) 0 else len(previous.rows)
    let row_count = max(old_count, len(desired.rows))
    let rows = [for (index in 0 to row_count - 1
                   where old_count != len(desired.rows) or
                         row_at(previous, index) != row_at(desired, index))
                  {kind: "row", index: index,
                   text: if (index < len(desired.rows)) desired.rows[index] else ""}]
    {operations: [*rows, {kind: "caret", row: desired.caret.row,
                          col: desired.caret.col}], screen: desired}
  }
}
