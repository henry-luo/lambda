import tikz: lambda.doc.tikz.tikz

// the named-node renderer supplies generated SVG paint through the automatic layout bridge.
let picture = tikz.render("\\begin{tikzpicture}" ++
  "\\node[fill=blue,minimum width=6cm,minimum height=6cm] (a) at (0,0) {};" ++
  "\\end{tikzpicture}")^;

<html
  <head
    <style "body { margin: 0; }
      .header { height: 80px; background: #ff0000; }
      .viewport { height: 100px; overflow: hidden; background: #ffffff; }
      .offset { margin-top: -60px; }">
  >
  <body
    <div class:"header">
    <div class:"viewport"
    , <div class:"offset"
      , <div style:"display:block;", picture>
      >
    >
  >
>
