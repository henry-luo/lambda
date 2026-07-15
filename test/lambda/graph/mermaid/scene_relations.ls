import scene: lambda.package.graph.scene

let actual = <'graph-scene' direction: "LR", width: 200, height: 100;
  <cluster id: "c", x: 0, y: 0, width: 200, height: 100,
      fill: "#fff", stroke: "#334455", 'stroke-width': 1>
  <node id: "a", shape: "box", group: "c", x: 10, y: 15, width: 40, height: 30,
      fill: "#abc", stroke: "rgb(51, 68, 85)", 'stroke-width': 2, opacity: 0.8>
  <node id: "b", shape: "box", group: "c", x: 130, y: 15, width: 40, height: 30>
  <edge id: "ab", 'from': "a", 'to': "b", 'from-side': "east", 'to-side': "west",
      'marker-start': "none", 'marker-end': "normal", 'route-kind': "straight",
      stroke: "#abc", 'stroke-width': 2, 'dash-array': "2, 4";
    <route; <point x: 50, y: 30> <point x: 130, y: 30>>
  >
>

let expected = <'graph-scene' direction: "LR";
  <cluster id: "c", x: 0, y: 0, width: 200, height: 100,
      fill: "rgb(255,255,255)", stroke: "rgb(51,68,85)", 'stroke-width': 1>
  <node id: "a", shape: "box", group: "c", x: 10, y: 15, width: 40, height: 30,
      fill: "rgb(170,187,204)", stroke: "#345", 'stroke-width': 2, opacity: 0.8>
  <node id: "b", shape: "box", group: "c", x: 130, y: 15, width: 40, height: 30>
  <edge id: "ab", 'from': "a", 'to': "b", 'from-side': "east", 'to-side': "west",
      'marker-start': "none", 'marker-end': "normal", 'route-kind': "straight",
      stroke: "rgb(170, 187, 204)", 'stroke-width': 2, 'dash-array': "2 4">
>

let good = scene.compare_scenes(actual, expected, {'rank-order': true})

let bad = <'graph-scene' direction: "LR", width: 100, height: 60;
  <cluster id: "c", x: 0, y: 0, width: 50, height: 40>
  <node id: "a", shape: "box", group: "c", x: 30, y: 10, width: 40, height: 30>
  <node id: "b", shape: "box", group: "c", x: 10, y: 10, width: 40, height: 30>
  <edge id: "ab", 'from': "a", 'to': "b", 'marker-start': "none",
      'marker-end': "normal", 'route-kind': "straight";
    <route; <point x: 80, y: 20> <point x: 5, y: 20>>
  >
>
let bad_comparison = scene.compare_scenes(bad, expected,
  {relations: true, 'rank-order': true, 'geometry-tolerance': 0.1,
    'route-tolerance': 0.1})

{
  canonical_paint_and_relations: good["equal"],
  bad_relations: sort([for (issue in bad_comparison.diagnostics
    where starts_with(string(issue.field), "relation.")) string(issue.field)])
}
