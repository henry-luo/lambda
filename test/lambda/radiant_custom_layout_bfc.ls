import radiant;

let ok = radiant.register_layout("bfc_smoke", (parent, children, ctx) => {
  placements: [for (child in children) {
    child: child,
    x: 0,
    y: 0
  }]
})

let doc = radiant.load("test/lambda/radiant_custom_layout_bfc.html")
let root = radiant.root(doc)
let did_layout = radiant.layout(root)
let graph = root.owner_document.query_selector("#graph")
let child = root.owner_document.query_selector("#child")
let after = root.owner_document.query_selector("#after")
let graph_box = radiant.box(graph)
let child_box = radiant.box(child)
let after_box = radiant.box(after)

{
  registered: ok,
  layout: did_layout,
  graph: [graph_box.x, graph_box.y, graph_box.width, graph_box.height],
  child: [child_box.x, child_box.y, child_box.width, child_box.height],
  after: [after_box.x, after_box.y, after_box.width, after_box.height],
  free: radiant.free(doc)
}
