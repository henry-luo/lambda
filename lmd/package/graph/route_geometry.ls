// Shared edge path and sampling geometry for paint and label placement.

fn distance(a, b) => math.sqrt((b.x - a.x) ** 2.0 + (b.y - a.y) ** 2.0)

fn midpoint(a, b) => {x: (a.x + b.x) / 2.0, y: (a.y + b.y) / 2.0}

fn line_path_tail(points, index, result) {
  if (index >= len(points)) result
  else line_path_tail(points, index + 1,
    result ++ " L " ++ string(points[index].x) ++ " " ++ string(points[index].y))
}

fn line_path(points) {
  if (len(points) == 0) ""
  else line_path_tail(points, 1,
    "M " ++ string(points[0].x) ++ " " ++ string(points[0].y))
}

fn curve_segments_at(points, index, start, result) {
  let control = points[index];
  let finish = if (index >= len(points) - 2) points[len(points) - 1]
    else midpoint(control, points[index + 1]);
  let next = [*result, {start: start, control: control, finish: finish}];
  if (index >= len(points) - 2) next
  else curve_segments_at(points, index + 1, finish, next)
}

fn curve_segments(points) =>
  if (len(points) < 3) [] else curve_segments_at(points, 1, points[0], [])

fn curve_path_tail(segments, index, result) {
  if (index >= len(segments)) result
  else {
    let segment = segments[index];
    curve_path_tail(segments, index + 1,
      result ++ " Q " ++ string(segment.control.x) ++ " " ++
        string(segment.control.y) ++ " " ++ string(segment.finish.x) ++
        " " ++ string(segment.finish.y))
  }
}

fn curve_path(points) {
  if (len(points) < 2) line_path(points)
  else if (len(points) == 2) {
    let start = points[0];
    let finish = points[1];
    let dx = finish.x - start.x;
    let dy = finish.y - start.y;
    "M " ++ string(start.x) ++ " " ++ string(start.y) ++
      " C " ++ string(start.x + dx / 3.0) ++ " " ++ string(start.y + dy / 3.0) ++
      " " ++ string(start.x + dx * 2.0 / 3.0) ++ " " ++
        string(start.y + dy * 2.0 / 3.0) ++
      " " ++ string(finish.x) ++ " " ++ string(finish.y)
  }
  else curve_path_tail(curve_segments(points), 0,
    "M " ++ string(points[0].x) ++ " " ++ string(points[0].y))
}

pub fn path_data(edge) =>
  if (edge.route_mode == "curved") curve_path(edge.points)
  else line_path(edge.points)

fn lerp(a, b, t) => {x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t}

fn quadratic(segment, t) {
  let first = lerp(segment.start, segment.control, t);
  let second = lerp(segment.control, segment.finish, t);
  lerp(first, second, t)
}

fn line_samples(a, b) {
  let count = max([1, int(distance(a, b) / 12.0) + 1]);
  [for (i in 1 to count) lerp(a, b, float(i) / float(count))]
}

fn quadratic_samples(segment) {
  let count = max([1, int((distance(segment.start, segment.control) +
    distance(segment.control, segment.finish)) / 12.0) + 1]);
  [for (i in 1 to count) quadratic(segment, float(i) / float(count))]
}

pub fn sample_points(edge) {
  let points = edge.points;
  if (len(points) == 0) []
  else if (len(points) == 1) [points[0]]
  else if (edge.route_mode == "curved" and len(points) > 2)
    [points[0], for (segment in curve_segments(points),
      sample in quadratic_samples(segment)) sample]
  else [points[0], for (i in 1 to (len(points) - 1),
    sample in line_samples(points[i - 1], points[i])) sample]
}
