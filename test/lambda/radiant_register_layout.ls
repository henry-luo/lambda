import radiant;

let child = {
  index: 0,
  tag: "DIV",
  id: "node-a",
  width: 120.5,
  height: 48,
  box: {x: 0, y: 0, width: 120.5, height: 48},
  children: [{tag: "SPAN"}],
  text: "Alpha",
  style: {display: "block"},
  margin: {left: 1, right: 2, top: 3, bottom: 4},
  border: {left: 5, right: 6, top: 7, bottom: 8},
  padding: {left: 9, right: 10, top: 11, bottom: 12},
  attrs: {role: "node", rank: "2"}
}

let ok = radiant.register_layout("smoke", (parent, children, ctx) => {
  placements: [for (v in children) {
    index: v.index,
    x: radiant.velmt_width(v),
    y: radiant.velmt_height(v)
  }]
})

{
  registered: ok,
  tag: radiant.velmt_tag(child),
  id: radiant.velmt_id(child),
  role: radiant.velmt_attr(child, "role"),
  missing: radiant.velmt_attr(child, "missing"),
  width: radiant.velmt_width(child),
  height: radiant.velmt_height(child),
  box_width: radiant.velmt_box(child).width,
  child_count: len(radiant.velmt_children(child)),
  text: radiant.velmt_text(child),
  display: radiant.velmt_style(child, "display"),
  margin_top: radiant.velmt_margin(child).top,
  border_left: radiant.velmt_border(child).left,
  padding_bottom: radiant.velmt_padding(child).bottom
}
