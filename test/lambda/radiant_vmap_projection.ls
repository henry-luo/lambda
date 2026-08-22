import radiant

fn vmap_projection(parent, children, ctx) {
  let child = children[0];
  let attrs = child.attrs;
  {
    width: child.width,
    height: child.height,
    placements: [for (value in children) {
      child_index: value.index, x: value.width, y: value.height
    }],
    paint_layers: [{z: 0, content: <g 'data-projection-tag': child.tag,
      'data-projection-role': attrs["data-graph-role"],
      'data-projection-id': attrs["data-node-id"]>}]
  }
}

let installed = radiant.register_layout("vmap-projection-smoke", vmap_projection)

let svg = radiant.render_svg(
  "<html><body><div style=\"display:block\" data-radiant-layout=\"vmap-projection-smoke\"><div style=\"display:block\" data-graph-role=\"node\" data-node-id=\"n0\">N</div></div></body></html>",
  80, 40)

{
  installed: installed,
  tag: contains(svg, "data-projection-tag=\"div\""),
  role: contains(svg, "data-projection-role=\"node\""),
  id: contains(svg, "data-projection-id=\"n0\""),
  float_string: float("60")
}
