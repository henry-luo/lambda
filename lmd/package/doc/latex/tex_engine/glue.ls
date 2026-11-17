import dimension: .dimension
pub type Glue = {width: int, stretch: int, stretch_order: int, shrink: int, shrink_order: int}
type OrderedPart = {value: int, order: int}

pub fn zero() Glue => {width: 0, stretch: 0, stretch_order: 0, shrink: 0, shrink_order: 0}
fn unit(order: int, mu: bool) string => if (order == 1) "fil" else if (order == 2) "fill" else if (order == 3) "filll"
    else if (mu) "mu" else "pt"
pub fn format(g: Glue, mu: bool) string^ => dimension.format(g.width, if (mu) "mu" else "pt") ++
    (if (g.stretch != 0) " plus " ++ dimension.format(g.stretch, unit(g.stretch_order, mu)) else "") ++
    (if (g.shrink != 0) " minus " ++ dimension.format(g.shrink, unit(g.shrink_order, mu)) else "")

fn add_part(a: int, a_order: int, b: int, b_order: int) OrderedPart =>
    if (a == 0) {value: b, order: if (b == 0) 0 else b_order}
    else if (b == 0) {value: a, order: a_order}
    else if (a_order == b_order) {value: a + b, order: a_order}
    else if (a_order > b_order) {value: a, order: a_order}
    else {value: b, order: b_order}

pub fn add(a: Glue, b: Glue) Glue^ {
    let stretch = add_part(a.stretch, a.stretch_order, b.stretch, b.stretch_order);
    let shrink = add_part(a.shrink, a.shrink_order, b.shrink, b.shrink_order);
    {width: a.width + b.width, stretch: stretch.value, stretch_order: stretch.order,
        shrink: shrink.value, shrink_order: shrink.order}
}

pub fn negate(g: Glue) Glue => {*:g, width: -g.width, stretch: -g.stretch, shrink: -g.shrink}

fn quotient(a: int, b: int) int => if ((a < 0) != (b < 0)) -(abs(a) div abs(b)) else abs(a) div abs(b)

pub fn scale(g: Glue, factor: int, divide: bool) Glue^ {
    let width = if (divide) quotient(g.width, factor) else g.width * factor;
    let stretch = if (divide) quotient(g.stretch, factor) else g.stretch * factor;
    let shrink = if (divide) quotient(g.shrink, factor) else g.shrink * factor;
    {width: width, stretch: stretch, stretch_order: if (stretch == 0) 0 else g.stretch_order,
        shrink: shrink, shrink_order: if (shrink == 0) 0 else g.shrink_order}
}

pub fn valid(g: Glue) bool => abs(g.width) < 1073741824 and abs(g.stretch) < 1073741824 and abs(g.shrink) < 1073741824
