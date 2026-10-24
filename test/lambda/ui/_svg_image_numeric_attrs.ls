// Lambda-built SVG gives <image> geometry as number items as well as text;
// both must place and size the image (fixture for svg_image_numeric_attrs).
let RED = "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='10' height='10' fill='red'/></svg>";
<html <head <style "body{margin:0;background:#fff} svg{display:block}">>
    <body <svg id:"stage", width:200, height:100, viewBox:"0 0 200 100",
        <rect width:200, height:100, fill:"#fff">
        <image id:"text-geometry", href:RED, x:"20", y:"20", width:"40", height:"40">
        <image id:"number-geometry", href:RED, x:120, y:20, width:40, height:40>>>>
