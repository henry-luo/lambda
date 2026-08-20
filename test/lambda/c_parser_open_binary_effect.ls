fn box(x, y) => {x: x, y: y}

fn spread_box(a, b, clearance) => {
  *:box(min([a.x, b.x]) - clearance, min([a.y, b.y]) - clearance)
}

spread_box({x: 1.0, y: 2.0}, {x: 3.0, y: 4.0}, 0.0)
