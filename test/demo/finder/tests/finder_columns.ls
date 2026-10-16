import m: ~~.mod_model

let rows = [{name:"Assets",path:"/demo/Assets",dir:true,size:0,kind:"Folder"},
    {name:"Projects",path:"/demo/Projects",dir:true,size:0,kind:"Folder"},
    {name:"Notes.txt",path:"/demo/Notes.txt",dir:false,size:10,kind:"Text document"}]
let assets = [{name:"Nested",path:"/demo/Assets/Nested",dir:true,size:0,kind:"Folder"},
    {name:"Palette.svg",path:"/demo/Assets/Palette.svg",dir:false,size:20,kind:"Image"}]
let root = {*:m.listing(m.initial("/demo"),"/demo",rows),mode:"columns"}
let child = m.listing(root,"/demo/Assets",assets)
let deep = m.listing(child,"/demo/Assets/Nested",[])
let previous = m.previous_column(deep)
let branch = m.listing(m.activate_column(deep,0),"/demo/Projects",[])
let filtered = m.listing({*:root,query:"assets"},"/demo/Assets",assets)
let refreshed = m.listing(child,child.path,[])
let back = m.listing(child,"/demo",rows,0)
let forward = m.listing(back,"/demo/Assets",assets,1)
fn paths(st) => [for (c in st.columns) c.path];
[
    paths(root) == ["/demo"],
    paths(child) == ["/demo","/demo/Assets"],
    paths(deep) == ["/demo","/demo/Assets","/demo/Assets/Nested"],
    m.column(child,0).selected == ["/demo/Assets"] and m.column(child,0).focus == "",
    m.column(child,1).path == child.path and m.column(child,1).entries == assets,
    m.column(deep,1).selected == ["/demo/Assets/Nested"],
    paths(previous) == ["/demo","/demo/Assets"] and previous.focus == "/demo/Assets/Nested",
    m.previous_column(root) == root,
    m.activate_column(child,-1) == child and m.activate_column(child,9) == child,
    m.activate_column(child,1) == child,
    paths(branch) == ["/demo","/demo/Projects"] and branch.entries == [],
    filtered.query == "" and m.column(filtered,0).query == "assets",
    [for (e in m.visible(m.column(filtered,0))) e.name] == ["Assets"],
    m.activate_column(filtered,0).query == "assets",
    paths(refreshed) == paths(child) and m.column(refreshed,1).entries == [],
    back.cursor == 0 and paths(back) == ["/demo"],
    forward.cursor == 1 and paths(forward) == paths(child),
    paths(m.listing(deep,"/other",[])) == ["/other"],
    paths(m.listing(deep,"/demo/Projects",[])) == ["/demo","/demo/Projects"],
    m.choose(m.activate_column(child,0),"/demo/Notes.txt").focus == "/demo/Notes.txt"
]
