import m: ~~.~~.demo.slides.mod_model
import app: ~~.~~.demo.slides.mod_app
import geometry: lambda.editor.draw.geometry

let base = {*:m.initial(<presentation <slide
    <group rotation:90.0,scale:2.0,width:400.0,height:300.0,
        <shape x:20.0,y:30.0,width:100.0,height:50.0,rotation:30.0,scale:1.5>>>>),
    index:0,selected:"s0-o0-0",scale:0.5}
let obj = m.chosen(base)
let moving = {*:base,drag:{x:100.0,y:100.0,object:obj,resize:false}}
let moved = app.drag_geometry(moving,100.0,140.0)
let resizing = {*:base,drag:{x:100.0,y:100.0,object:obj,resize:true}}
let local = geometry.rotate_point({x:20.0,y:10.0},{x:0.0,y:0.0},obj.rotation)
let screen = geometry.rotate_point({x:local.x*obj.scale,y:local.y*obj.scale},{x:0.0,y:0.0},90.0)
let resized = app.drag_geometry(resizing,100.0+screen.x,100.0+screen.y)
fn corner(box,rotation,scale) {
    let half = geometry.rotate_point({x:box.width/2.0,y:box.height/2.0},{x:0.0,y:0.0},rotation);
    {x:box.x+box.width/2.0-half.x*scale,y:box.y+box.height/2.0-half.y*scale}
}
let before = corner(obj,obj.rotation,obj.scale)
let after = corner(resized,obj.rotation,obj.scale);
[
    moved.x == 60.0 and moved.y == 30.0,
    resized.width == 120.0 and resized.height == 60.0,
    abs(before.x-after.x)<0.000001 and abs(before.y-after.y)<0.000001,
    app.drag_geometry(moving,100.0,100.0) == {x:20.0,y:30.0}
]
