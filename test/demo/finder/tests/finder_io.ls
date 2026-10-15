import f: ~~.mod_io
import m: ~~.mod_model

let entries = f.read_folder("test/demo/finder/files")^
let welcome = [for (e in entries where e.name == "Welcome.md") e][0]
let assets = [for (e in entries where e.name == "Assets") e][0]
let image = f.read_folder(assets.path)^[0]
let missing = f.read_folder("test/demo/finder/files/does-not-exist") ^ { "missing" }
let data = f.preview(welcome)^;
[
    len(entries) == 5,
    welcome.kind == "Text document" and welcome.size > 0 and len(welcome.modified)>0,
    ends_with(welcome.path,"/test/demo/finder/files/Welcome.md"),
    assets.dir and assets.kind == "Folder",
    data.kind == "text" and contains(data.text,"A place for everything."),
    not data.truncated,
    f.preview(assets)^.kind == "folder",
    f.preview(image)^.kind == "image",
    f.preview(null)^.kind == "empty",
    f.preview({kind:"Document",size:0})^.kind == "generic",
    f.preview({kind:"Text document",size:131073})^.kind == "generic",
    missing == "missing",
    len(m.visible(m.listing(m.initial("/"),"/",entries))) == 4
]
