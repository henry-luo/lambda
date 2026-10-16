// Mark preserves symbol tags and exact whitespace inside the JSON notebook.
// Reads parse data only; the shared rich surface admits no imported handlers.
import lambda.editor.mod_doc
import html: lambda.edit.html
import session: lambda.edit.session
import model: .mod_model

pub fn archive(book) => {format:"lambda-notes",version:1,sequence:book.sequence,folders:book.folders,
    notes:[for (n in book.notes) {*:n,doc:format(n.doc,'mark')^}]}
fn valid_attrs(attrs) => attrs is array and all([for (a in attrs) a is map and a.name is symbol and
    (a.value is string or a.value is number or a.value is bool)])
fn safe_node(n,depth=0) bool => depth<48 and n is map and
    (if (is_text(n)) n.text is string and n.marks is array and all([for (m in n.marks)
        m is map and contains(['strong','b','em','i','u','s','del','sub','sup','code'],m.name) and m.value==true])
    else is_node(n) and contains(['doc','p','h1','h2','h3','h4','h5','h6','blockquote','ul','ol','li','pre','br','hr',
        'table','tr','td','th','thead','tbody'],n.tag) and valid_attrs(n.attrs) and n.content is array and
        all([for (c in n.content) safe_node(c,depth+1)]))
fn decoded_note(n,folders) map^ {
    if (not (n is map and n.id is string and n.folder is string and n.updated is string and
        n.pinned is bool and n.deleted is bool and n.doc is string) or not contains(folders,n.folder))
        raise error("Notes: invalid note metadata.")
    else {
        let doc = parse(n.doc,'mark')^;
        if (not safe_node(doc) or doc.tag!='doc' or len(doc.content)==0)
            raise error("Notes: unsupported or invalid note content.")
        else {*:n,doc:doc}
    }
}
pub fn decode(source) map^ {
    if (len(source)>4000000) raise error("Notes: notebooks are limited to 4 MB.") else {
        let data = parse(source,'json')^;
        if (not (data is map and data.format=="lambda-notes" and data.version==1 and
            data.folders is array and data.notes is array and data.sequence is int and data.sequence>0))
            raise error("Notes: choose a version-1 .note.json notebook.")
        else if (not all([for (f in data.folders) f is map and f.id is string and f.name is string and trim(f.name)!=""]) or
            not contains([for (f in data.folders) f.id],"notes")) raise error("Notes: invalid folders.")
        else {
            let folders = [for (f in data.folders) f.id]
            let notes = [for (n in data.notes) decoded_note(n,folders)^]
            let ids = [*folders,*[for (n in notes) n.id]];
            if (len(unique(ids))!=len(ids) or any([for (id in ids)
                contains(["all","trash","note-" ++ string(data.sequence),"folder-" ++ string(data.sequence)],id)]))
                raise error("Notes: duplicate or reserved identifiers.")
            else {folders:data.folders,notes:notes,sequence:data.sequence}
        }
    }
}
pub fn encode(book) => format(archive(book),'json')^
pub let descriptor = {name:"Notes notebook",suffixes:[".note.json"],
    export_text:(book,envelope)=>encode(book),
    check_roundtrip:(book,envelope)=>if (decode(encode(book))^==book) null else "notebook content changed"}
pub fn file_state(st) map^ => session.new_session(st.path,descriptor,st.disk_text,{doc:st.saved,envelope:{}})^
pub fn read_book(path) map^ {
    if (not ends_with(lower(path),".note.json")) raise error("Notes: open a .note.json notebook.") else {
        let source = input(path,'text')^
        let st = model.initial(decode(source)^);
        {*:st,path:path,disk_text:source,status:"Opened " ++ session.basename(path)}
    }
}
