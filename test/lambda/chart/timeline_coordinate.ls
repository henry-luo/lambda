import c: lambda.chart.timeline.coordinate
let box={x:20,y:30,width:600,height:400}
let straight=c.configure({},box)
let shapes=[straight,c.configure({kind:"serpentine",levels:4},box),c.configure({kind:"spiral",turns:3},box),
    c.configure({kind:"curve",path:"M0 0 C300 0 0 400 600 400"},box)];
[
 ["shape construction",all([for (shape in shapes) not (shape is error) and shape.length > 0])],
 ["arc distance roundtrip",all([for (shape in shapes) for (t in [0.0,0.1,0.25,0.5,0.75,0.9,1.0])
    abs(c.invert(shape,c.project(shape,t).point).fraction-t) < 0.0001])],
 ["straight origin",c.project(straight,0).point == [20.0,230.0] and c.project(straight,1).point == [620.0,230.0]],
 ["lane normal",c.project(straight,0.5,25).point == [320.0,255.0]],
 ["reversed path",c.project(c.configure({reverse:true},box),0).point == c.project(straight,1).point],
 ["trimmed path",c.project(c.configure({start_location:0.25,end_location:0.75},box),0).point == c.project(straight,0.25).point],
 ["vertical path",c.project(c.configure({},box,"vertical"),0).point == [320.0,30.0]],
 ["zero length rejected",c.configure({kind:"curve",points:[[0,0],[0,0]]},box) is error],
 ["discontinuous path rejected",c.configure({kind:"curve",path:"M0 0 L1 1 M2 2 L3 3"},box) is error],
 ["invalid levels rejected",c.configure({kind:"serpentine",levels:1},box) is error],
 ["invalid radii rejected",c.configure({kind:"spiral",inner_radius:50,outer_radius:10},box) is error],
 ["missing curve rejected",c.configure({kind:"curve"},box) is error]
]
