// S7.2.2/S9.1.2: multi-axis writes share one last scope and snapshot their
// owner; projected coordinates run once through the checked COW spine.
type Board { rows: int[][] }
pn once(var counter) int { counter = counter + 1; return 1 }
pn main() {
    var grid = [[1, 2, 3], [4, 5, 6]]
    let saved = grid
    grid[last, 0] = 40
    print([saved, grid]); print("\n")
    var box = {rows: [[1, 2, 3], [4, 5, 6]]}
    let old_rows = box.rows
    var count = 0
    box.rows[once(count), 2] = 60
    print([count, old_rows, box.rows]); print("\n")
    var board: Board = <Board rows: [[1, 2], [3, 4]]>
    let old_board = board
    board.rows[0, 1] = 20
    print([old_board.rows, board.rows]); print("\n")
}
