import graph: lambda.package.graph.layout

fn placement(result, id) => [for (item in result.placements where item.id == id) item][0]

let model = {
  nodes: [
    {id: "a", width: 40, height: 20},
    {id: "b", width: 40, height: 20}
  ],
  edges: [{from: "a", to: "b"}],
  rank_sep: 70
}
let tb = graph.compute(model, {direction: "TB"})
let bt = graph.compute(model, {direction: "BT"})
let lr = graph.compute(model, {direction: "LR"})
let rl = graph.compute(model, {direction: "RL"})

{
  TB: [tb.width, tb.height, placement(tb, "a").x, placement(tb, "a").y,
       placement(tb, "b").x, placement(tb, "b").y],
  BT: [bt.width, bt.height, placement(bt, "a").x, placement(bt, "a").y,
       placement(bt, "b").x, placement(bt, "b").y],
  LR: [lr.width, lr.height, placement(lr, "a").x, placement(lr, "a").y,
       placement(lr, "b").x, placement(lr, "b").y],
  RL: [rl.width, rl.height, placement(rl, "a").x, placement(rl, "a").y,
       placement(rl, "b").x, placement(rl, "b").y]
}
