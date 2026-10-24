// Lambda-built SVG gives <pattern> tile geometry as number items as well as
// text; both must tile the fill (fixture for svg_pattern_numeric_attrs).
<html <head <style "body{margin:0;background:#fff} svg{display:block}">>
    <body <svg id:"stage", width:200, height:100, viewBox:"0 0 200 100",
        <defs
            <pattern id:"text-tile", x:"0", y:"0", width:"20", height:"20", patternUnits:"userSpaceOnUse",
                <rect width:"10", height:"10", fill:"red">>
            <pattern id:"number-tile", x:0, y:0, width:20, height:20, patternUnits:"userSpaceOnUse",
                <rect width:10, height:10, fill:"red">>>
        <rect width:200, height:100, fill:"#fff">
        <rect id:"text-geometry", x:0, y:0, width:80, height:80, fill:"url(#text-tile)">
        <rect id:"number-geometry", x:100, y:0, width:80, height:80, fill:"url(#number-tile)">>>>
