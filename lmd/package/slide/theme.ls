// Layouts supply bounds only; explicitly authored coordinates always win.
pub let layouts = ['blank', 'title', 'title-body', 'two-column']
fn box(x, y, width, height) => {x: x, y: y, width: width, height: height}

pub fn bounds(layout, role, width, height, index) {
    let margin = width * 0.0625
    let top = height * 0.0833333333
    let inner = width - margin * 2.0
    let heading = height * 0.1666666667
    let body_y = top + heading + height * 0.05
    if (layout == 'blank') box(0.0, 0.0, width, height)
    else if (role == 'title' or (role == null and index == 0)) box(margin, top, inner, heading)
    else if (layout == 'title') box(margin, height * 0.45, inner, height * 0.25)
    else if (layout == 'two-column') {
        let column = (inner - margin) / 2.0
        box(if (role == 'right' or (role == null and index > 1)) margin * 2.0 + column else margin,
            body_y, column, height - body_y - top)
    } else box(margin, body_y, inner, height - body_y - top)
}
