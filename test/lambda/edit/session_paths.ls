// lambda.edit session helpers (vibe/radiant/Radiant_Design_Edit_Mode.md §8):
// Save As path resolution, the references a move to another folder would
// break, and content-based dirty state.
import sess: lambda.edit.session
import lambda.edit.model
import lambda.editor.mod_doc

"basename / dirname:";
[sess.basename("/docs/notes/a.md"), sess.dirname("/docs/notes/a.md"), sess.dirname("/a.md"),
 sess.dirname("a.md")]

// a typed name is relative to the document's folder; "." and ".." fold
"resolve_path:";
[sess.resolve_path("/docs/notes", "b.md"),
 sess.resolve_path("/docs/notes", "./sub/../c.md"),
 sess.resolve_path("/docs/notes", "../other/d.md"),
 sess.resolve_path("/docs/notes", "/abs//e.md"),
 sess.resolve_path("/docs", "../../../f.md"),
 sess.resolve_path("./temp/x", "a.md"), sess.resolve_path("../up", "../../b.md"), sess.normal_path("./a/./")]

"is_relative_url:";
[for (u in ["img/a.png", "../b.md", "c.md#part", "https://x.org/a", "mailto:a@b.c", "/root.png",
            "#top", "?q=1", "", "a:b/c", "dir/a:b"]) is_relative_url(u)]

let doc = node('doc', [
  node('p', [text("plain")]),
  node('p', [node_attrs('a', [{name: 'href', value: "https://x.org"}], [text("abs")]),
             node_attrs('img', [{name: 'src', value: "pics/p.png"}, {name: 'alt', value: "p"}], [])])])
let raw = node('doc', [node_attrs('html_block', [{name: 'html', value: "<img SRC=\"x.png\">"}], [])])
let clean = node('doc', [node('p', [node_attrs('a', [{name: 'href', value: "#top"}], [text("up")])])])

"first_relative_reference:";
[first_relative_reference(doc), first_relative_reference(raw), first_relative_reference(clean)]

// dirty compares content with the saved checkpoint, not the edit count
let session = {path: "/docs/a.md", name: "a.md", saved_doc: doc}
"is_dirty / window_title:";
[sess.is_dirty(session, doc), sess.is_dirty(session, clean),
 sess.window_title(session, false), sess.window_title(session, true)]
