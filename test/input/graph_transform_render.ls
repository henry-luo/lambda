import transform: lambda.package.graph.transform

let installed = transform.install()
let graph = transform.to_html({
  nodes: [
    {id: "a", content: <div; <strong; "Alpha"> <span; "rich node">>,
      style: "width:120px;height:56px;padding:8px;border:1px solid #27313a;background:#f7fbff;"},
    {id: "b", label: "Beta", shape: "round",
      style: "width:100px;height:48px;padding:8px;border:1px solid #27313a;background:#fff8e8;"}
  ],
  edges: [{id: "ab", from: "a", to: "b", directed: true, z: -1}]
}, {rank_sep: 90})

<html;
  <head; <style; "body{margin:20px;font:16px sans-serif}">>
  <body; graph>
>
