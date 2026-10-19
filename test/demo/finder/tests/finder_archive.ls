import model: ~~.mod_model
import files: ~~.mod_io
import ui: ~~.mod_ui

fn named(st,name) => [for (e in st.entries where e.name == name) e][0]
fn browse(st,name) map^ => files.navigate(st,named(st,name).path)^
let base = model.absolute("test/input/zip",sys.proc.self.cwd#)
let disk = files.navigate(model.initial(base),base)^
let zip = named(disk,"sample.zip")
let root = browse(disk,"sample.zip")^
let word = browse(root,"word")^
let rels = browse(word,"_rels")^
let nested = browse(root,"nested.zip")^
let text = named(nested,"inner.txt")
let selected = model.choose(nested,text.path)
let previous = model.previous_column(root)
let outside = files.navigate(rels,base)^
let restored = files.navigate(outside,rels.path,rels.cursor)^
let viewer = browse(disk,"viewer.zip")^
let html = files.preview(named(viewer,"index.html"))^
let markdown = files.preview(named(viewer,"readme.md"))^
let image = files.preview(named(viewer,"drawing.svg"))^
let corrupt = browse(disk,"corrupt.zip")^
let corrupt_word = browse(corrupt,"word")^
let markup = format(ui.files({*:root,mode:"columns"}),'html')^
let image_markup = format(ui.files(viewer),'html')^;
[
    not zip.dir and zip.kind == "Archive" and zip.size>0 and zip.modified != "",
    model.archive(zip) and model.browsable(zip) and model.archive({dir:false,extension:"ZIP"}),
    len(root.entries)==7 and root.path == zip.path,
    root.focus == "" and model.preview_entry(root).name == zip.name,
    root.container.size == zip.size and root.container.modified == zip.modified,
    root.container.kind == "Archive" and not root.container.dir,
    model.read_only(root) and contains(model.status(root),"Archive contents (read-only)"),
    all([for (e in root.entries) e.readonly == true and e.source is element]),
    not model.writable(root,"new-folder") and not model.writable(root,"new-file"),
    model.writable(model.choose(disk,zip.path),"rename"),
    not model.writable(selected,"rename") and not model.writable(selected,"delete") and not model.writable(selected,"move"),
    model.location_readonly(root,zip.path) and not model.location_readonly(root,base),
    len(word.entries)==2 and word.container.dir and word.container.name == "word",
    len(rels.entries)==1 and named(rels,"document.xml.rels").readonly,
    nested.container.name == "nested.zip" and not nested.container.dir and len(nested.entries)==1,
    nested.archive_cache.path == zip.path and nested.archive_cache.root.entry_path == "",
    files.preview(text)^.text == "nested",
    model.preview_entry(selected).name == "inner.txt",
    files.preview(named(root,"data.json"))^.text == "{\"answer\":42}",
    files.preview(named(root,"empty.txt"))^.text == "",
    files.preview(named(root,"opaque.bin"))^.kind == "generic",
    len(browse(root,"empty-dir")^.entries)==0,
    len(browse(disk,"empty.zip")^.entries)==0,
    files.preview(named(browse(disk,"wide.zip")^,"wide.txt"))^.text == "ZIP64",
    previous.path == disk.path and model.focused(previous).name == zip.name,
    not model.read_only(previous) and model.writable(previous,"delete"),
    outside.archive_cache == null and not model.read_only(outside),
    restored.path == rels.path and len(restored.entries)==1 and restored.container.locator == rels.container.locator,
    html.kind == "document" and contains(html.html,"Archive HTML"),
    markdown.kind == "document" and contains(markdown.html,"Archive Markdown</h1>") and contains(markdown.html,"markdown.css"),
    image.kind == "image" and image.inline and starts_with(image.src,"data:image/svg+xml;base64,"),
    not contains(image_markup,"data-thumb-path") and not contains(image_markup,"vector-thumbnail"),
    contains(markup,"folder-chevron") and contains(markup,"draggable=\"false\"") and contains(markup,"data-readonly=\"true\""),
    len(corrupt.entries)==7 and len(corrupt_word.entries)==2,
    (files.preview(named(corrupt_word,"document.xml")) ^ { contains(^.message,"CRC") }) == true,
    (browse(disk,"traversal.zip") ^ { true }) == true,
    (files.plan({action:"new-file",folder:root.path,value:"must-not-create.txt"}) ^ { true }) == true,
    (files.plan({action:"delete",folder:root.path,paths:[named(root,"data.json").path]}) ^ { true }) == true,
    (files.read_destination(zip.path) ^ { true }) == true,
    files.plan({action:"rename",folder:disk.path,paths:[zip.path],value:zip.name})^ == [{action:"rename",from:zip.path,to:zip.path}]
]
