import ui: lambda.ui.dtna
let basic = ui.render(ui.skeleton()^)
let detailed = ui.render(ui.skeleton({avatar:{size:48,shape:'square'},title:{width:120},paragraph:{rows:2,width:[100,"75%"]},round:true,active:true})^)
let empty = ui.render(ui.skeleton({title:false,paragraph:{rows:0}})^)
let ready = ui.render(ui.skeleton({loading:false},"Ready")^)
let avatar = ui.render(ui.skeleton_avatar({dimension:48,shape:'circle'})^)
let button = ui.render(ui.skeleton_button({block:true,height:40})^)
let image = ui.render(ui.skeleton_image()^)
let node = ui.render(ui.skeleton_node({},"Chart")^)
let section = content(detailed)[1];
[
    ["default loading",basic["aria-busy"] == "true" and len(content(content(basic)[0])) == 2],
    ["default paragraph",len(content(content(content(basic)[0])[1])) == 3],
    ["avatar dimensions",contains(content(content(detailed)[0])[0].style,"width:48px;height:48px;border-radius:4px;")],
    ["title width",contains(content(section)[0].style,"width:120px;")],
    ["row widths",contains(content(content(section)[1])[0].style,"width:100px;") and contains(content(content(section)[1])[1].style,"width:75%;")],
    ["zero rows",len(content(content(content(empty)[0])[0])) == 0],
    ["loading transition",content(ready)[0] == "Ready" and ready["aria-busy"] == null],
    ["standalone avatar",contains(content(avatar)[0].style,"height:48px;border-radius:50%;")],
    ["standalone button",contains(content(button)[0].style,"width:100%;height:40px;")],
    ["standalone image",name(content(content(image)[0])[0]) == 'svg'],
    ["custom node",content(content(node)[0])[0] == "Chart"],
    ["invalid options",(ui.skeleton({paragraph:{rows:-1}}) or null) == null and (ui.skeleton({avatar:{bad:1}}) or null) == null and (ui.skeleton({title:1}) or null) == null],
    ["invalid dimensions",(ui.skeleton_input({width:-1}) or null) == null and (ui.skeleton_avatar({dimension:0}) or null) == null],
    ["invalid flags",(ui.skeleton({active:1}) or null) == null and (ui.skeleton_button({block:1}) or null) == null]
]
