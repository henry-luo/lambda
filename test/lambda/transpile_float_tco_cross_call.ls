// Typed float params must stay native double across cross-function calls and
// tail-recursive rewrites.

fn stack_helper(level: int, h: float, d: float, acc: float) float {
    if (level <= 0) {
        acc + h + d
    } else {
        stack_helper(level - 1, h, d, acc + h + d + level)
    }
}

fn make_stacked_delim(level: int, h: float, d: float) float {
    let rht = h + d
    stack_helper(level, h, d, rht)
}

fn render_vertical_mult() float {
    make_stacked_delim(3, 0.606, 0.0 - 0.00599)
}

render_vertical_mult()
