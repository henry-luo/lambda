import radiant

let installed = radiant.register_layout("clip-layer", (parent, children, ctx) => {
  width: 200,
  height: 200,
  placements: [],
  paint_layers: [{z: 0, content:
    <svg xmlns:"http://www.w3.org/2000/svg", width:200, height:200,
      <rect x:0, y:0, width:200, height:200, fill:"#0000ff">
    >}]
});

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
      , <div 'data-radiant-layout':"clip-layer", style:"display:block;">
      >
    >
  >
>
