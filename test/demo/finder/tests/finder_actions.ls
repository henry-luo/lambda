import m: ~~.mod_model
import io: ~~.mod_io

let rows = [{name:"A",path:"/base/A",dir:true,size:0,kind:"Folder"}]
let root = m.listing(m.initial("/base"),"/base",rows)
let child = m.listing(root,"/base/A",[{name:"Nested",path:"/base/A/Nested",dir:true,size:0,kind:"Folder"}])
let deep = m.listing(child,"/base/A/Nested",[])
let renamed = m.relocated(deep,[{from:"/base/A",to:"/base/Renamed"}])
let moved = m.relocated(deep,[{from:"/base/A",to:"/base/B/A"}]);
[
    m.valid_name("Notes.txt") and m.valid_name("space # ü.txt"),
    m.valid_name(".hidden") and m.valid_name(" leading space "),
    not m.valid_name("") and not m.valid_name("  "),
    not m.valid_name(".") and not m.valid_name(".."),
    not m.valid_name("../outside") and not m.valid_name("sub/file"),
    not m.valid_name("sub\\file") and not m.valid_name("bad\u0000name"),
    not m.valid_name(null),
    io.named_path("/base","space # ü.txt")^ == "/base/space # ü.txt",
    io.named_path("/base","~")^ == "/base/~" and io.named_path("/","notes")^ == "/notes",
    (io.named_path("/base","../outside") ^ { "invalid" }) == "invalid",
    m.inside("/base/A","/base/A") and m.inside("/base/A/child","/base/A"),
    not m.inside("/base/Another","/base/A"),
    m.inside("/base/A","/") and not m.inside("relative","/"),
    m.remap("/base/A/child",[{from:"/base/A",to:"/new/A"}]) == "/new/A/child",
    m.remap("/base/Another",[{from:"/base/A",to:"/new/A"}]) == "/base/Another",
    renamed.path == "/base/Renamed/Nested",
    [for (c in renamed.columns) c.path] == ["/base","/base/Renamed","/base/Renamed/Nested"],
    renamed.history == ["/base","/base/Renamed","/base/Renamed/Nested"],
    [for (c in moved.columns) c.path] == ["/base/B/A","/base/B/A/Nested"],
    m.relocated(m.choose(child,"/base/A/Nested"),[{from:"/base/A",to:"/base/Renamed"}]).focus == "/base/Renamed/Nested"
]
