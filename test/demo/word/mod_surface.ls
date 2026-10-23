// One source path per applied model node; never parse the painted DOM back into state.
// The shared factory table keeps tag choice separate from projection (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import page: .mod_page
import rendering: lambda.edit.model
import insert: .mod_insert
import stories: .mod_stories

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
    a:(a,k)=><span class:"word-link", title:a.title or a.href, style:a.style, *k>,
    hr:(a,k)=><hr>, br:(a,k)=><br>, img:(a,k)=><img *:a>,
    div:(a,k)=><div *:a, *k>
}
fn make(tag,attrs,kids) {
    let factory = factories[tag] or factories.div
    factory(attrs,kids)
}
// Image resources remain visible; anchor targets become inert tooltips.
// Imported event handlers never reach the live DOM.
fn projected_attrs(attrs) => {*:map([for (a in attrs where contains(['style','align','start','colspan','rowspan','data-word-style',
    'src','alt','width','height','href','title','data-word-table-style','data-word-page-break','data-word-comment',
    'data-word-cover','data-word-blank-page','data-word-shape','data-word-equation','data-word-wordart','data-word-rotation',
    'data-word-change','data-word-change-kind','data-word-change-author'],a.name) or
    (a.name=='id' and starts_with(string(a.value),insert.bookmark_prefix)))
    (string(a.name),string(a.value))])}
fn wrap_marks(marks,i,child) => if (i >= len(marks)) child else {
    let m = marks[i]
    let projected = if (m.value is array) projected_attrs(m.value) else {}
    let attrs = if (projected["data-word-change-kind"]=="delete") {*:projected,contenteditable:"false"} else projected
    wrap_marks(marks,i+1,make(m.name,attrs,[child]))
}
view map {
    if (~.kind == 'text') wrap_marks(~.marks,0,if (~.text == "") "\u200B" else ~.text)
    else if (~.kind == 'node' and ~.tag == 'doc') <div class:"word-pages",
        *[for (i,children in page.sheets(~)) <div class:"word-page",["data-page"]:string(i+1),
            style:page.editor_style(~),stories.story(~,"header",i+1,len(page.sheets(~)));
            *[for (child in children) apply(child)];
            stories.story(~,"footer",i+1,len(page.sheets(~)))>]>
    else if (~.kind == 'node') make(~.tag,
        projected_attrs(~.attrs),rendering.surface_children(~,(child)=>apply(child)))
    else ""
}
pub fn surface(editor,zoom,read_only=false) =>
    <div id:"edit-surface", class:"word-content", contenteditable:if (read_only) "false" else "true", tabindex:"0",
        ["aria-label"]:"Document", ["aria-multiline"]:"true", role:"textbox",
        style:"transform:scale(" ++ string(zoom) ++ "); transform-origin:top left; width:" ++ string(page.geometry(editor.doc).width) ++ "px;",
        apply(editor.doc)>

// The same document typography serves the editor, HTML export, and paged PDF.
pub let document_css = "
 .word-content { box-sizing:border-box; color:#24334b;
   font-family:Georgia, 'Times New Roman', serif; font-size:11pt; line-height:1.5;
   outline:none; }
 .word-page { position:relative; box-sizing:border-box; background:#fff; box-shadow:0 3px 15px rgba(31,54,62,0.10); }
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
"
