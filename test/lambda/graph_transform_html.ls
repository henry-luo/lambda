import transform: lambda.package.graph.transform

let rich = <strong; "Rich">
let result = transform.to_html({
  directed: true,
  nodes: [
    {id: "a", label: "Alpha"},
    {id: "b", content: [rich, " content"], shape: "round", z: 2}
  ],
  edges: [
    {id: "ab", from: "a", to: "b"}
  ]
}, {direction: "LR", node_sep: 42, rank_sep: 64})

{
  tag: name(result),
  class: result.class,
  layout: result['data-radiant-layout'],
  direction: result['data-direction'],
  spacing: [result['data-node-sep'], result['data-rank-sep']],
  children: [for (i in 0 to (len(result) - 1), let child = result[i]) {
    tag: name(child),
    id: child['data-node-id'],
    from: child['data-from'],
    to: child['data-to'],
    directed: child['data-directed'],
    shape: child['data-shape'],
    z: child['data-z'],
    content: if (name(child) == 'node' and len(child) > 0)
      [for (j in 0 to (len(child) - 1)) child[j]] else []
  }]
}
