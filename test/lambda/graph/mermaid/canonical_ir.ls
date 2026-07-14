import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn item_kind(value) => if (value is element) string(name(value)) else "text"

fn content_kinds(value) => [for (child in model.content_items(value)) item_kind(child)]

let legacy = <graph direction: "TB", custom: "graph-meta";
  <subgraph id: "group", label: "Group **One**", 'label-format': "markdown",
      custom: "subgraph-meta";
    <node id: "A", label: "The **API**", 'label-format': "markdown", custom: 42>
    <node id: "B", custom: "rich-block";
      <section class: "details"; <strong; "Nested"> " block">
    >
    <edge id: "e0", from: "A", to: "B", label: "<b>calls</b>",
        'label-format': "html", custom: "edge-meta">
  >
>
let once = normalize.normalize(legacy)
let twice = normalize.normalize(once.graph)
let nodes = model.nodes(once.graph)
let edges = model.edges(once.graph)
let groups = model.subgraphs(once.graph)
let html = transform.to_html(once.graph)
let html_nodes = [for (i in 0 to (len(html) - 1), let child = html[i]
  where child is element and string(name(child)) == "node") child]

let authored = <graph direction: "LR";
  <node id: "C", custom: "preserved";
    <label format: "html"; "<b>C</b>">
    <content; <strong; "C">>
  >
>
let authored_result = normalize.normalize(authored)

{
  valid: once.valid,
  rebuilt: once.graph != legacy,
  idempotent: once.graph == twice.graph,
  graph_custom: once.graph.custom,
  subgraph: [groups[0].custom, model.label_source(groups[0]),
    model.label_format(groups[0]), content_kinds(groups[0])],
  nodes: [for (node in nodes) [
    node.id, node.custom, model.label_source(node), model.label_format(node),
    content_kinds(node)
  ]],
  edge: [edges[0].custom, model.label_source(edges[0]),
    model.label_format(edges[0]), content_kinds(edges[0])],
  html_node_content: [for (node in html_nodes) [
    node["data-node-id"],
    [for (i in 0 to (len(node) - 1)) item_kind(node[i])]
  ]],
  authored_unchanged: authored_result.graph == authored,
  authored_custom: model.nodes(authored_result.graph)[0].custom
}
