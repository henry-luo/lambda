// One source path per applied model node; never parse the painted DOM back into state.
// The shared factory table keeps tag choice separate from projection (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import page: .mod_page
import columns: .mod_columns
import rendering: lambda.edit.model
import insert: .mod_insert
import stories: .mod_stories
import graphics: .mod_graphics
import table_design: .mod_table_design
import object_layout: .mod_object_layout
import group_data: .mod_group_data

let factories = {
    p:(a,k)=><p *:a, *k>, h1:(a,k)=><h1 *:a, *k>, h2:(a,k)=><h2 *:a, *k>,
    h3:(a,k)=><h3 *:a, *k>, h4:(a,k)=><h4 *:a, *k>, h5:(a,k)=><h5 *:a, *k>, h6:(a,k)=><h6 *:a, *k>,
    ul:(a,k)=><ul *:a, *k>, ol:(a,k)=><ol *:a, *k>, li:(a,k)=><li *:a, *k>,
    table:(a,k)=><table *:a, *k>, thead:(a,k)=><thead *:a, *k>, tbody:(a,k)=><tbody *:a, *k>, tfoot:(a,k)=><tfoot *:a, *k>,
    tr:(a,k)=><tr *:a, *k>, th:(a,k)=><th *:a, *k>, td:(a,k)=><td *:a, *k>,
    blockquote:(a,k)=><blockquote *:a, *k>, pre:(a,k)=><pre *:a, *k>, code:(a,k)=><code *:a, *k>,
    span:(a,k)=><span *:a, *k>, strong:(a,k)=><strong *:a, *k>, b:(a,k)=><b *:a, *k>,
    em:(a,k)=><em *:a, *k>, i:(a,k)=><i *:a, *k>, u:(a,k)=><u *:a, *k>,
    del:(a,k)=><del *:a, *k>, s:(a,k)=><s *:a, *k>, sub:(a,k)=><sub *:a, *k>, sup:(a,k)=><sup *:a, *k>,
    a:(a,k)=><span *:a,class:"word-link", title:a.title or a.href, *k>,
    hr:(a,k)=><hr>, br:(a,k)=><br>, img:(a,k)=><img *:a>,
    div:(a,k)=><div *:a, *k>
}
fn make(tag,attrs,kids) {
    let factory = factories[tag] or factories.div
    factory(attrs,kids)
}
// Image resources remain visible; anchor targets become inert tooltips.
// Imported event handlers never reach the live DOM.
fn projected_attrs(attrs,preview=false) => {*:map([for (a in attrs where contains(['style','align','start','indent','colspan','rowspan','data-word-style',
    'src','alt','width','height','href','title','data-word-citation','data-word-bibliography',*table_design.attributes,'data-word-repeat-header','data-word-formula','data-word-page-break','data-word-comment',
    'data-word-cover','data-word-blank-page',*[for (entry in graphics.catalog) entry.key],'data-word-rotation',object_layout.key,group_data.key,
    'data-word-change','data-word-change-kind','data-word-change-author'],a.name) or
    (not preview and a.name=='id' and (starts_with(string(a.value),insert.bookmark_prefix) or starts_with(string(a.value),"word-source-"))))
    (string(a.name),string(a.value))])}
fn wrap_marks(marks,i,child,preview=false) => if (i >= len(marks)) child else {
    let m = marks[i]
    let projected = if (m.value is array) projected_attrs(m.value,preview) else {}
    let attrs = if (projected["data-word-change-kind"]=="delete") {*:projected,contenteditable:"false"} else projected
    wrap_marks(marks,i+1,make(m.name,attrs,[child]),preview)
}
fn project(n,child,preview=false) {
    // one wrapper per leaf preserves source identity (S2.6.5) and lets read-only navigation resolve text ranges.
    if (n.kind=='text') <span class:"word-text-run",wrap_marks(n.marks,0,if (n.text=="") "\u200B" else n.text,preview)>
    else if (n.kind=='node') make(n.tag,projected_attrs(n.attrs,preview),rendering.surface_children(n,child))
    else ""
}
// Thumbnails share document painting but never apply source nodes or duplicate bookmark IDs.
fn preview_node(n) => project(n,(child)=>preview_node(child),true)
fn column_body(doc,children,preview=false) {
    let render = (child)=>if (preview) preview_node(child) else apply(child)
    if (columns.settings(doc).count==1) [for (child in children) render(child)]
    else [<div class:"word-columns",style:columns.style(doc),*[for (child in children) render(child)]>]
}
pub fn thumbnail(doc,children,width=156,page_no=1) {
    let g = page.geometry(doc)
    let scale = width/g.width;
    <span class:"page-thumbnail",["aria-hidden"]:"true",style:"width:" ++ string(width) ++ "px;height:" ++ string(g.height*scale) ++ "px",
        <span class:"word-content page-thumbnail-content",style:"width:" ++ string(g.width) ++ "px;transform:scale(" ++ string(scale) ++ ")",
            <span class:"word-page",style:page.editor_style(doc,page_no),*column_body(doc,children,true)>>>
}
// A printed page shows one window of its section's flow, between measured page starts (see mod_print.breaks).
// Scaled content flows at width/scale and is scaled back onto the page; the paper and measuring flows are both
// flow-root so a leading margin offsets them identically.
pub fn print_page(doc,entry,page_no,total,width,scale=1) {
    let g = page.geometry(doc,page_no)
    let area = g.height-g.top-g.bottom
    let height = if (entry.bottom==null) area else min((entry.bottom-entry.top)*scale,area);
    <div class:"print-page",["data-page"]:string(page_no),["aria-label"]:"Page " ++ string(page_no),
        style:"width:" ++ string(width) ++ "px;height:" ++ string(g.height*width/g.width) ++ "px",
        <div class:"print-paper",style:"width:" ++ string(g.width) ++ "px;height:" ++ string(g.height) ++ "px;transform:scale(" ++ string(width/g.width) ++ ")",
            stories.story(doc,"header",page_no,total);
            <div class:"print-window",style:"top:" ++ string(g.top) ++ "px;left:" ++ string(g.left) ++ "px;width:" ++
                string(g.width-g.left-g.right) ++ "px;height:" ++ string(height) ++ "px",
                <div class:"word-content print-flow",style:"top:" ++ string(-entry.top*scale) ++ "px;width:" ++
                    string((g.width-g.left-g.right)/scale) ++ "px;transform:scale(" ++ string(scale) ++ ")",*column_body(doc,entry.children,true)>>
            stories.story(doc,"footer",page_no,total)>>
}
pub fn print_measure(doc,scale=1) {
    let g = page.geometry(doc);
    <div class:"print-measure",["aria-hidden"]:"true",*[for (children in page.sheets(doc))
        <div class:"word-content print-measure-sheet",style:"width:" ++ string((g.width-g.left-g.right)/scale) ++ "px",*column_body(doc,children,true)>]>
}
// one sheet of paper: its pages centered in a grid of cells (mod_print.layout); the preview and the PDF share it.
pub fn print_sheet(doc,sheet,layout,total,width,scale=1) {
    let k = width/layout.width
    let g = page.geometry(doc)
    let cell_w = layout.width/layout.cols*k
    let cell_h = layout.height/layout.rows*k
    let page_w = g.width*layout.fit*k
    let page_h = g.height*layout.fit*k;
    <div class:"print-sheet",style:"width:" ++ string(width) ++ "px;height:" ++ string(layout.height*k) ++ "px",
        *[for (i,item in sheet) <div class:"print-cell",style:"left:" ++ string((i%layout.cols)*cell_w+(cell_w-page_w)/2) ++
            "px;top:" ++ string(floor(i/layout.cols)*cell_h+(cell_h-page_h)/2) ++ "px",
            print_page(doc,item.entry,item.number,total,page_w,scale)>]>
}
// print geometry shared by the in-app preview and the composed PDF.
pub let print_css = "
 .print-sheet { position:relative; overflow:hidden; background:#fff; }
 .print-cell { position:absolute; }
 .print-page { position:relative; overflow:hidden; background:#fff; }
 .print-paper { position:absolute; top:0; left:0; transform-origin:top left; background:#fff; }
 .print-window { position:absolute; overflow:hidden; }
 .print-flow { position:absolute; left:0; transform-origin:top left; }
 .print-flow,.print-measure-sheet { display:flow-root; }
 .print-measure { position:absolute; left:-20000px; top:0; visibility:hidden; pointer-events:none; }
 .print-plain .word-content * { background-color:transparent !important; background-image:none !important; }
"
fn render_pages(doc,web=false) => <div class:"word-pages",
    *[for (i,children in page.sheets(doc)) <div class:"word-page",["data-page"]:string(i+1),
        style:if (web) null else page.editor_style(doc,i+1),stories.story(doc,"header",i+1,len(page.sheets(doc)));
        *column_body(doc,children);
        stories.story(doc,"footer",i+1,len(page.sheets(doc)))>]>
// Explicit root-template selection changes presentation while each child still applies its source node.
view word_web_document: map { render_pages(~,true) }
view word_document: map {
    if (~.kind=='node' and ~.tag=='doc') render_pages(~)
    else project(~,(child)=>apply(child))
}
pub fn surface(editor,zoom,read_only=false,layout="print") =>
    <div id:"edit-surface", class:"word-content", contenteditable:if (read_only) "false" else "true", tabindex:"0",
        ["aria-label"]:"Document", ["aria-multiline"]:"true", role:"textbox",
        // reflow before applying the same document scale used by object handles and Print Layout.
        style:"transform:scale(" ++ string(zoom) ++ "); transform-origin:top left; width:" ++
            (if (layout=="web") string(100/zoom) ++ "%" else string(page.geometry(editor.doc).width) ++ "px") ++ ";",
        apply(editor.doc,{template:if (layout=="web") "word_web_document" else "word_document"})>

// The same document typography serves the editor, HTML export, and paged PDF.
pub let document_css = "
 .word-content { box-sizing:border-box; color:#24334b;
   font-family:Georgia, 'Times New Roman', serif; font-size:11pt; line-height:1.5;
   outline:none; }
 .word-page { position:relative; z-index:0; box-sizing:border-box; background:#fff; box-shadow:0 3px 15px rgba(31,54,62,0.10); }
 body.word-content { position:relative; z-index:0; }
 .word-story { position:absolute; font-family:Arial,sans-serif; color:#718397; line-height:1.2; white-space:pre-wrap; cursor:pointer; }
 .word-page + .word-page { margin-top:24px; }
 .word-content p { margin:0 0 11pt; }
 .word-content h1 { font-size:29pt; font-weight:400; letter-spacing:-0.8px;
   line-height:1.16; margin:0 0 12pt; color:#243b57; }
 .word-content h2 { font-family:Arial,sans-serif; font-size:17pt; font-weight:500;
   margin:23pt 0 9pt; color:#294f70; }
 .word-content h3,.word-content h4,.word-content h5,.word-content h6 {
   font-family:Arial,sans-serif; font-size:12pt; margin:18pt 0 7pt; color:#294f70; }
 .word-content [data-word-style=Subtitle] { font-family:Arial,sans-serif; color:#718397;
   font-size:12pt; margin:0 0 24pt; }
 .word-content [data-word-style=Quote],.word-content blockquote { font-style:italic;
   border-left:3px solid #b9cddc; padding:8px 0 8px 18px; margin:18px 0; color:#536b7d; }
 .word-content ul,.word-content ol { padding-left:22px; margin:8px 0 15px; }
 .word-content li { padding-left:4px; margin:5px 0; }
 .word-content li p { margin:0; }
 .word-content table { border-collapse:collapse; width:100%; margin:15px 0 18px; font-size:10pt; }
 .word-content td,.word-content th { border:1px solid #d6e0e7; padding:9px 12px; text-align:left; }
 .word-content th { font-family:Arial,sans-serif; font-weight:600; color:#294f70; background:#edf3f6; }
 .word-content table[data-word-table-style=soft] tr:nth-child(even) td { background:#f0f6fa; }
 .word-content table[data-word-table-style=contrast] th { background:#294f70; color:#fff; border-color:#294f70; }
 .word-content table[data-word-table-style=plain] td,.word-content table[data-word-table-style=plain] th {
   border-left:0; border-right:0; background:transparent; }
 .word-content hr { border:0; border-top:1px solid #bfced9; margin:22px 0; }
 .word-content a,.word-link { color:#2864ad; text-decoration:underline; }
 .word-content [data-word-citation] { color:inherit; text-decoration:none; cursor:pointer; }
 .word-content [data-word-citation]:hover { background:#eee8fb; border-radius:2px; }
 .word-content pre,.word-content code { font-family:'Courier New',monospace; }
 .word-content img { max-width:100%; vertical-align:middle; }
 .word-content [data-word-page-break=true] { break-before:page; }
 .word-content [data-word-blank-page=true] { min-height:1em; }
 .word-content [data-word-cover=title] { margin-top:96pt; padding-bottom:18pt; border-bottom:2px solid #527f78;
   font-size:38pt; color:#284e47; }
 .word-content [data-word-cover=subtitle] { font-family:Arial,sans-serif; font-size:15pt; color:#718397; margin-top:20pt; }
 .word-content [data-word-cover=author] { font-family:Arial,sans-serif; font-size:12pt; color:#527f78; margin-top:100pt; }
 .word-content [data-word-comment] { background-color:#fff3c4; border-bottom:2px solid #d6aa39; }
 @page { size:A4; margin:17mm 19mm; }
 @media print { .word-content { width:auto; min-height:0; padding:0; }
   h1,h2,h3 { break-after:avoid; } tr { break-inside:avoid; } }
" ++ table_design.css ++ join([for (level in 1 to 8)
    // the flat-list model stores levels; both screen and export need their visual offset.
    ".word-content li[indent=\"" ++ string(level) ++ "\"] { margin-inline-start:" ++ string(level*1.75) ++ "em; }"],"\n")
