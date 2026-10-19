import draw: lambda.editor.draw
import canvas: lambda.editor.draw.view
let a = draw.vertex("a","diamond",{x:60.0,y:80.0,width:140.0,height:80.0},{label:"Shared id",pattern:"grid"})^
let b = draw.vertex("b",{id:"custom",outline:[[0.2,0],[1,0],[0.8,1],[0,1]],ports:[{id:"in",x:0.0,y:0.5}]},
    {x:300.0,y:80.0,width:140.0,height:80.0},{label:"Custom"})^
let e = draw.edge("e",{kind:'boundary',vertex:"a",side:'east'},{kind:'port',vertex:"b",port:"in"})^
let initial = draw.exec(draw.open_session(draw.new_document({width:520.0,height:300.0}))^,{kind:'insert',objects:[a,b,e]})^;
<html <head <meta charset:"UTF-8"><title "Drawing package instances">>
    <body style:"margin:12px;display:flex;gap:16px",
        (canvas.surface(initial.doc,{id:"left",width:540,height:340})^);
        (canvas.surface(initial.doc,{id:"right",width:540,height:340})^)>>
