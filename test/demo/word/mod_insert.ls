// Insert commands keep document structure and history in Lambda (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import inline_format: lambda.editor.mod_inline_format
import symbol_sets: ~~.mod_symbols

pub let symbols = symbol_sets.symbols

pub let bookmark_prefix = "word-bookmark-"
pub fn bookmark_id(name) => bookmark_prefix ++ name
pub fn bookmark_href(name) => "#" ++ bookmark_id(name)
pub fn link_bookmark(href) => if (href is string and starts_with(href,"#" ++ bookmark_prefix))
    slice(href,len(bookmark_prefix)+1) else null
pub fn bookmark_name(n) {
    let id = if (is_node(n)) attrs_get(n.attrs,'id') or "" else ""
    if (id is string and starts_with(id,bookmark_prefix)) slice(id,len(bookmark_prefix)) else null
}
pub fn bookmarks(doc) => [for (entry in descendants(doc),let bookmark = bookmark_name(entry.node) where bookmark!=null)
    {name:bookmark,path:entry.path,label:doc_text(entry.node)}]
pub fn bookmark_selection(doc,name) {
    let entry = [for (b in bookmarks(doc) where b.name==name) b][0]
    let leaf = if (entry==null) null else inline_format.leaves(node_at(doc,entry.path),entry.path)[0]
    let point = if (leaf==null) pos(entry.path,0) else pos(leaf.path,0)
    if (entry==null) null else text_selection(point,point)
}
fn bookmark_attrs(tx,path,name) {
    tx_step(tx,step_set_attr(path,'id',if (name==null) null else bookmark_id(name)))
}
fn remove_bookmarks(tx,entries,i=0) => if (i>=len(entries)) tx else
    remove_bookmarks(bookmark_attrs(tx,entries[i].path,null),entries,i+1)
fn valid_name(name) => name is string and len(name)>0 and len(name)<=40 and
    (contains("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_",slice(name,0,1)) ^ { false }) and
    all([for (c in split(name,"")) contains("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_0123456789",c) ^ { false }])
pub fn bookmark_transaction(editor,id,p,block) map^ {
    let name = if (p.name is string) trim(p.name) else ""
    let existing = [for (b in bookmarks(editor.doc) where b.name==name) b]
    if (id=="insert.deleteBookmark" and len(existing)==0) raise error("Word: this bookmark no longer exists")
    else if (id=="insert.bookmark" and (not valid_name(name) or block==null))
        raise error("Word: bookmark names use 1–40 letters, digits or underscores and cannot start with a digit")
    else if (id=="insert.bookmark" and attrs_get(block.node.attrs,'id')!=null and
        bookmark_name(block.node)==null)
        raise error("Word: this paragraph already has an imported named target")
    else {
        // the shared split policy keeps id on the original paragraph, never its new sibling.
        let tx = remove_bookmarks(tx_begin(editor.doc,editor.selection),existing)
        if (id=="insert.deleteBookmark") tx else bookmark_attrs(tx,block.path,name)
    }
}

pub fn cover_transaction(editor,p) map^ {
    if (not (p.title is string) or trim(p.title)=="" or len(split(p.title,""))>500 or
        not (p.subtitle is string) or len(split(p.subtitle,""))>1000 or not (p.author is string) or len(split(p.author,""))>200)
        raise error("Word: enter a cover title (up to 500 characters), subtitle and author")
    else {
        let roles = [{tag:'h1',role:"title",text:trim(p.title)},{tag:'p',role:"subtitle",text:p.subtitle},{tag:'p',role:"author",text:p.author}]
        let cover = [for (r in roles) node_attrs(r.tag,[{name:'data-word-cover',value:r.role}],[text(r.text)])]
        let tx = tx_step(tx_step(tx_begin(editor.doc,editor.selection),step_set_attr([0],'data-word-page-break',"true")),
            step_replace([],0,0,cover))
        tx_set_selection(tx,text_selection(pos([0,0],0),pos([0,0],len(trim(p.title)))))
    }
}
fn page_split(editor) map^ {
    let split = commands.cmd_insert_paragraph(editor)
    let path = if (split==null) [] else inline_format.selection_path(split.sel_after)
    let index = path[0]
    if (len(path)<1 or len(path)>2 or not contains(['p','h1','h2','h3','h4','h5','h6'],node_at(split.doc_after,[index]).tag))
        raise error("Word: insert a page from a body paragraph or heading")
    else tx_step(split,step_set_attr([index],'data-word-page-break',"true"))
}
pub fn page_break_transaction(editor) map^ => page_split(editor)^
pub fn blank_page_transaction(editor) map^ {
    let split = page_split(editor)^
    let index = inline_format.selection_path(split.sel_after)[0];
    // two boundaries isolate an editable blank page between the split paragraphs.
    {
        let blank = node_attrs('p',[{name:'data-word-page-break',value:"true"},{name:'data-word-blank-page',value:"true"}],[text("")])
        let tx = tx_step(split,step_replace([],index,index,[blank]))
        tx_set_selection(tx,text_selection(pos([index,0],0),pos([index,0],0)))
    }
}
pub fn symbol_transaction(editor,p) map^ {
    let chars = if (p.char is string) split(p.char,"") else []
    if (len(chars)<1 or len(chars)>64 or
        any([for (c in chars,let code=ord(c)) code<32 or (code>=127 and code<=159) or code==8232 or code==8233]))
        raise error("Word: enter 1–64 printable characters")
    else {
        let tx = commands.cmd_insert_text(editor,p.char)
        if (tx==null) raise error("Word: a symbol cannot be inserted at this selection") else tx
    }
}
