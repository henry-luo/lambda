// known points and colors exercise inherited state independently of the renderer (PG12.3–4).
import a: lambda.procart.adjust
import t: lambda.scene3d.transform
let root = a.identity()
let small = a.compose(root,{scale:[0.9,0.1]})^
let rotate_move = a.compose(a.compose(root,{rotate:90})^,{position:[1.0,0.0,0.0]})^
let move_rotate = a.compose(a.compose(root,{position:[1.0,0.0,0.0]})^,{rotate:90})^
let p = t.point(rotate_move.matrix,[0.0,0.0,0.0])^
let q = t.point(move_rotate.matrix,[0.0,0.0,0.0])^;
[small.matrix[0] == 0.9,small.matrix[5] == 0.1,small.matrix[10] == 1.0,abs(p[0]) < 1e-12,abs(p[1]-1.0) < 1e-12,q == [1.0,0.0,0.0],
 a.matrix({position:[2.0,1.0,0.0],scale:2.0})^ == a.matrix({scale:2.0,position:[2.0,1.0,0.0]})^];
let parent = a.compose(root,{color:"#ff0000",hue:20,opacity:0.5})^
let child = a.compose(parent,{hue:40,opacity:0.4})^
let yellow = a.paint(child.colors)^
let reset = a.paint(a.compose(parent,{color:"#0000ff"})^.colors)^;
[yellow.color == "#ffff00",abs(yellow.opacity-0.2)<1e-12,reset.color == "#0000ff",reset.opacity == 0.5,
 a.paint(parent.colors)^.color != yellow.color,a.paint(root.colors)^.color == "#ffffff",
 a.paint(a.compose(a.compose(root,{saturation:2})^,{saturation:0.5})^.colors,"#ff0000")^.color == "#ff8080"];
[(a.matrix({scale:0}) ^ {null}) == null,(a.matrix({matrix:root.matrix,position:[1,0,0]}) ^ {null}) == null,
 (a.matrix({rotate:10,rotation:[0,0,0]}) ^ {null}) == null,(a.color_op({opacity:2}) ^ {null}) == null]
