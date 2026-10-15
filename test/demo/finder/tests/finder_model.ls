import m: ~~.mod_model

let rows = [
    {name:"zeta.txt",path:"/demo/zeta.txt",dir:false,size:2048,modified:"2026-03-01",kind:"Text document"},
    {name:"Assets",path:"/demo/Assets",dir:true,size:0,modified:"2026-02-01",kind:"Folder"},
    {name:"alpha.json",path:"/demo/alpha.json",dir:false,size:10,modified:"2026-01-01",kind:"Data file"},
    {name:".secret",path:"/demo/.secret",dir:false,size:1,modified:"2026-04-01",kind:"Document"},
    {name:"Projects",path:"/demo/Projects",dir:true,size:0,modified:"2026-01-01",kind:"Folder"}]
let st = m.listing(m.initial("/demo"),"/demo",rows)
fn names(value) => [for (e in m.visible(value)) e.name]
let first = m.choose(st,"/demo/Assets")
let extended = m.choose(first,"/demo/alpha.json",false,true)
let toggled = m.choose(extended,"/demo/Projects",true)
let child = m.listing(st,"/demo/Assets",[])
let back = m.listing(child,"/demo",rows,0)
let branched = m.listing(back,"/demo/Projects",[]);
[
    m.absolute("../b/./c//","/a/z") == "/a/b/c",
    m.absolute("../../../../","/a/b") == "/",
    m.absolute("~/Documents","/demo","/home/alice") == "/home/alice/Documents",
    m.absolute("~","/demo","/home/alice") == "/home/alice",
    m.absolute("space # and ü.txt","/demo") == "/demo/space # and ü.txt",
    m.parent("/") == "/" and m.parent("/a/b") == "/a",
    m.crumbs("/a/b") == [{name:"Computer",path:"/"},{name:"a",path:"/a"},{name:"b",path:"/a/b"}],
    names(st) == ["Assets","Projects","alpha.json","zeta.txt"],
    names({*:st,descending:true}) == ["Projects","Assets","zeta.txt","alpha.json"],
    names({*:st,sort:"size",descending:true}) == ["Assets","Projects","zeta.txt","alpha.json"],
    names({*:st,sort:"modified"}) == ["Projects","Assets","alpha.json","zeta.txt"],
    names({*:st,query:" ALPHA "}) == ["alpha.json"],
    len(m.visible({*:st,hidden:true})) == 5,
    extended.selected == ["/demo/Assets","/demo/Projects","/demo/alpha.json"],
    toggled.selected == ["/demo/Assets","/demo/alpha.json"],
    m.choose(first,"/demo/Assets",true).selected == [],
    m.move(st,1).focus == "/demo/Assets",
    m.move(first,1,true).selected == ["/demo/Assets","/demo/Projects"],
    m.move(m.initial("/"),1).selected == [],
    m.reconcile({*:extended,query:"alpha"}).selected == ["/demo/alpha.json"],
    child.history == ["/demo","/demo/Assets"] and child.cursor == 1,
    back.history == child.history and back.cursor == 0,
    branched.history == ["/demo","/demo/Projects"] and branched.cursor == 1,
    m.listing(st,"/demo",rows).history == ["/demo"],
    m.kind("svg") == "Image" and m.kind("ls") == "Source code",
    m.size_label(0) == "0 bytes" and m.size_label(2048) == "2 KB",
    m.status(extended) == "4 items · 3 selected"
]
