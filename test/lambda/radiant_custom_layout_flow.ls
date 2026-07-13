import radiant;

let ok = radiant.register_layout("flow_smoke", (parent, children, ctx) => {
  baseline: 12,
  placements: [for (child in children) {
    child: child,
    x: radiant.velmt_index(child) * 50 + (if (ctx.child_available_width_definite and ctx.child_available_width_source == "available") -10 else -20),
    y: radiant.velmt_index(child) * 30 + (if (ctx.child_available_height_definite and ctx.child_available_height_source == "available") 0 else 1000),
    z: radiant.velmt_index(child)
  }]
})

let doc = radiant.load("test/lambda/radiant_custom_layout_flow.html")
let root = radiant.root(doc)
let did_layout = radiant.layout(root)
let graph = root.owner_document.query_selector("#graph")
let a = root.owner_document.query_selector("#a")
let b = root.owner_document.query_selector("#b")
let c = root.owner_document.query_selector("#c")
let graph_box = radiant.box(graph)
let a_box = radiant.box(a)
let b_box = radiant.box(b)
let c_box = radiant.box(c)

{
  registered: ok,
  layout: did_layout,
  graph: [graph_box.width, graph_box.height],
  a: [a_box.x, a_box.y, a_box.width, a_box.height],
  b: [b_box.x, b_box.y, b_box.width, b_box.height],
  c: [c_box.x, c_box.y, c_box.width, c_box.height],
  free: radiant.free(doc)
}
