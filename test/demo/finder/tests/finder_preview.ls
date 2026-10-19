import model: ~~.mod_model
import files: ~~.mod_io

let entries = files.read_folder("test/demo/finder/tests/fixtures")^
let markdown = [for (entry in entries where entry.extension == "md") entry][0]
let html = [for (entry in entries where entry.extension == "html") entry][0]
let source = [for (entry in entries where entry.extension == "js") entry][0]
let preview = files.preview(markdown)^;
[
    preview.kind == "document" and ends_with(preview.src,"/Preview%20%23%20%C3%BC.md"),
    files.preview(html)^.kind == "document",
    files.preview(source)^.kind == "text",
    model.viewable("PDF") and model.viewable("docx") and model.viewable("epub"),
    model.viewable("tex") and model.viewable("ls") and model.viewable("html"),
    not model.viewable("js") and not model.viewable("exe") and not model.viewable(null),
    model.preview_size(300,900) == 300,
    model.preview_size(10,900) == 180,
    model.preview_size(900,900) == 600,
    model.preview_size(600,500) == 334,
    model.preview_size(180,300) == 134,
    model.preview_size(180,100) == 0
]
