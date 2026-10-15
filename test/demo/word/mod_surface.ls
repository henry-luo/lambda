// One source path per applied model node; never parse the painted DOM back into state.
// The shared factory table keeps tag choice separate from projection (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import model: .mod_model

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
    'src','alt','width','height','href','title'],a.name))
    (string(a.name),a.value)])}
fn wrap_marks(marks,i,child) => if (i >= len(marks)) child else {
    let m = marks[i]
    let attrs = if (m.value is array) projected_attrs(m.value) else {}
    wrap_marks(marks,i+1,make(m.name,attrs,[child]))
}
view map {
    if (~.kind == 'text') wrap_marks(~.marks,0,if (~.text == "") "\u200B" else ~.text)
    else if (~.kind == 'node') make(if (~.tag == 'doc') 'div' else ~.tag,
        projected_attrs(~.attrs),[for (child in ~.content) apply(child)])
    else ""
}
pub fn surface(editor,zoom,read_only=false) =>
    <div id:"edit-surface", class:"word-content", contenteditable:if (read_only) "false" else "true", tabindex:"0",
        ["aria-label"]:"Document", ["aria-multiline"]:"true", role:"textbox",
        style:"transform:scale(" ++ string(zoom) ++ "); transform-origin:top left;",
        apply(editor.doc)>

// The same document typography serves the editor, HTML export, and paged PDF.
pub let document_css = "
 .word-content { box-sizing:border-box; background:#fff; color:#24334b;
   font-family:Georgia, 'Times New Roman', serif; font-size:11pt; line-height:1.5;
   padding:64px 72px 72px; width:794px; min-height:1123px; outline:none; }
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
 .word-content hr { border:0; border-top:1px solid #bfced9; margin:22px 0; }
 .word-content a,.word-link { color:#2864ad; text-decoration:underline; }
 .word-content pre,.word-content code { font-family:'Courier New',monospace; }
 .word-content img { max-width:100%; }
 @page { size:A4; margin:17mm 19mm; }
 @media print { .word-content { width:auto; min-height:0; padding:0; }
   h1,h2,h3 { break-after:avoid; } tr { break-inside:avoid; } }
"
