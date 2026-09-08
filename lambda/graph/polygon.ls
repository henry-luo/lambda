// Shared normalized polygon geometry for semantic HTML and graph routing.

fn raw_vertices(sides, orientation, skew, distortion) => [
  for (i in 0 to (sides - 1),
    let offset = if (sides % 2 == 0) 180.0 / float(sides) else 0.0,
    let angle = (orientation - 90.0 + offset + float(i) * 360.0 / float(sides)) *
      math.pi / 180.0,
    let y = math.sin(angle),
    let x = math.cos(angle) * (1.0 + distortion * y) + skew * y) {x: x, y: y}
]

fn scaled(point, min_x, max_x, min_y, max_y, cx, cy, width, height) => {
  x: cx - width / 2.0 + (point.x - min_x) * width / (max_x - min_x),
  y: cy - height / 2.0 + (point.y - min_y) * height / (max_y - min_y)
}

pub fn vertices(cx, cy, width, height, sides, orientation = 0.0,
    skew = 0.0, distortion = 0.0) {
  let points = raw_vertices(max([3, int(sides)]), float(orientation),
    float(skew), float(distortion));
  let min_x = min([for (point in points) point.x]);
  let max_x = max([for (point in points) point.x]);
  let min_y = min([for (point in points) point.y]);
  let max_y = max([for (point in points) point.y]);
  [for (point in points)
    scaled(point, min_x, max_x, min_y, max_y, cx, cy, width, height)]
}

pub fn css(sides, orientation = 0.0, skew = 0.0, distortion = 0.0) =>
  "clip-path:polygon(" ++ join([for (point in vertices(50.0, 50.0, 100.0, 100.0,
    sides, orientation, skew, distortion))
      string(point.x) ++ "% " ++ string(point.y) ++ "%"], ",") ++ ");"
