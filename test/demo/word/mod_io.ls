// Lossless editable snapshots reuse lambda.edit's atomic/conflict-aware saves.
// Imported office documents are semantic copies; they never overwrite their source.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import html: lambda.edit.html
import markdown: lambda.edit.markdown
import helpers: lambda.edit.model
import session: lambda.edit.session
import docx: lambda.doc.docx
import media: lambda.doc.media
import radiant
import document: .mod_document
import model: .mod_model
import surface: .mod_surface
import page: .mod_page
import columns: .mod_columns
import review: .mod_review
import stories: .mod_stories
import changes: .mod_changes
import print: .mod_print

let tags = ['doc','p','h1','h2','h3','h4','h5','h6','blockquote','ul','ol','li',
    'table','thead','tbody','tfoot','tr','td','th','pre','code','span','strong','em','u','b','i','del','s','sub','sup','a','img','br','hr','div',
    'section','article','main','header','footer','nav','aside','figure','figcaption']
let marks = ['strong','b','em','i','u','del','s','sub','sup','code','span']
fn pack_attrs(attrs) => [for (a in attrs) {name:string(a.name),value:a.value}]
fn packed(n) => if (is_text(n)) {kind:"text",text:n.text,marks:pack_attrs(n.marks)}
    else {kind:"node",tag:string(n.tag),attrs:pack_attrs(n.attrs),content:[for (child in n.content) packed(child)]}
fn valid_attrs(attrs) bool => attrs is array and all([for (a in attrs)
    a is map and (a.name is string or a.name is symbol) and (a.value is string or a.value is number or a.value is bool or a.value == null)])
fn valid_mark(m) bool => m is map and contains(marks,symbol(m.name)) and
    (m.value is bool or (valid_attrs(m.value) ^ { false }))
// restore integer list levels because HTML attributes arrive as strings.
fn unpack_attrs(attrs,tag=null) => [for (a in attrs) {name:symbol(a.name),
    value:if (tag=='li' and symbol(a.name)=='indent' and a.value is string) (int(a.value) ^ { a.value }) else a.value}]
fn unpack_mark(m) => {name:symbol(m.name),value:if (m.value is array) unpack_attrs(m.value) else m.value}
fn restored(n,depth=0) any^ {
    if (depth > 48 or not (n is map)) raise error("Word: invalid or deeply nested document")
    else if (n.kind == "text" or n.kind == 'text') {
        if (not (n.text is string) or not (n.marks is array) or not all([for (m in n.marks) valid_mark(m)]))
            raise error("Word: invalid text or marks")
        else text_marked(n.text,[for (m in n.marks) unpack_mark(m)])
    }
    else if ((n.kind == "node" or n.kind == 'node') and contains(tags,symbol(n.tag)) and
             valid_attrs(n.attrs) and n.content is array) {
        node_attrs(symbol(n.tag),unpack_attrs(n.attrs,symbol(n.tag)),[for (child in n.content) restored(child,depth+1)^])
    }
    else raise error("Word: unsupported document node " ++ string(n.tag))
}
pub fn archive(doc,title) => {format:"lambda-word",version:1,title:title,document:packed(doc)}
pub fn restore(value) map^ {
    if (not (value is map) or value.format != "lambda-word" or value.version != 1 or not (value.title is string))
        raise error("Word: unsupported editable file; expected lambda-word version 1")
    else {
        let doc = restored(value.document)^
        if (doc.tag != 'doc' or len(doc.content)==0 or (len(model.leaves(doc))==0 and len(model.blocks(doc))==0))
            raise error("Word: document must contain editable text")
        else {doc:doc,title:value.title}
    }
}
pub fn import_text(source) map^ {
    let restored_doc = restore(parse(source,'json')^)^;
    {doc:restored_doc.doc,envelope:{title:restored_doc.title}}
}
pub fn export_text(doc,envelope) string^ {
    let source = format(archive(doc,envelope.title),'json')^
    // apply the reader's size limit before saving, including embedded picture data.
    if (len(source)>4000000) raise error("Word: editable files are limited to 4 MB; reduce picture sizes") else source
}
fn check_roundtrip(doc,envelope) {
    let loaded = import_text(export_text(doc,envelope)^) ^ { null }
    if (loaded == null or loaded.doc != doc) "the editable snapshot would change the document" else null
}
pub let descriptor = {name:"Lambda Word",suffixes:[".word.json"],surface:'rich_text',schema:html.schema,
    import_text:import_text,export_text:export_text,check_roundtrip:check_roundtrip}
pub fn new_session(doc,title="Untitled document") =>
    session.new_session("",descriptor,null,{doc:doc,envelope:{title:title}})

// embedded pictures survive moving the editable snapshot or exporting its HTML.
pub fn picture_source(path) string^ {
    let ext = lower(split(path,".")[last])
    let mime = {png:"image/png",jpg:"image/jpeg",jpeg:"image/jpeg",gif:"image/gif",webp:"image/webp",svg:"image/svg+xml"}[ext]
    if (mime == null) raise error("Word: choose a PNG, JPEG, GIF, WebP, or SVG picture")
    else media.data_uri(input(path,'binary')^,mime)^
}

pub fn read_document(path) map^ {
    if (not (path is string) or trim(path)=="") raise error("Word: enter a file path")
    else if (ends_with(lower(path),".word.json")) {
        let source = input(path,'text')^
        if (len(source)>4000000) raise error("Word: editable files are limited to 4 MB") else {
            let loaded = import_text(source)^;
            {editor:model.open_editor(loaded.doc),session:session.new_session(path,descriptor,source,loaded),status:"Opened " ++ session.basename(path)}
        }
    }
    else {
        let extension = lower(split(path,".")[last])
        let source = if (extension == "docx") docx.to_html(path,{unsupported:"error"})^ else input(path,'text')^
        let loaded = if (contains(["html","htm"],extension)) {doc:import_html_document(source)^}
            else if (extension=="docx") html.import_text(source)^
            else if (contains(["md","markdown"],extension)) markdown.import_text(source)^
            else if (extension == "txt") {doc:node('doc',[for (line in split(source,"\n")) document.paragraph(line)])}
            else raise error("Word: open .word.json, .html, .md, .txt, or .docx")
        let imported = if (len(loaded.doc.content)==0) document.blank() else restored(loaded.doc)^
        let doc = imported;
        {editor:model.open_editor(doc),session:new_session(doc,session.basename(path)),
            status:"Imported " ++ session.basename(path) ++ "; save an editable copy."}
    }
}
let metadata_names = ['data-word-page','data-word-columns','data-word-comments','data-word-comment-seq','data-word-stories','data-word-tracking','data-word-change-seq','data-word-references']
pub fn import_html_document(source) map^ {
    let loaded = html.import_text(source)^
    import_metadata(restored(loaded.doc)^,loaded.envelope.head)^
}
fn import_metadata(doc,head) map^ {
    let encoded = [for (m in head where type(m)==element and name(m)=='meta' and m["name"]=="lambda-word-metadata") m["content"]][0]
    if (encoded==null) doc else {
        let attrs = parse(encoded,'json')^
        if (not valid_attrs(attrs) or not all([for (a in attrs) contains(metadata_names,symbol(a.name))]))
            raise error("Word: invalid document metadata")
        else {*:doc,attrs:unpack_attrs(attrs)}
    }
}
fn document_body(doc,paged) {
    let body = html.export_text(doc,{document:false,head:<head>})
    if (paged) columns.pdf_flow(doc,body) else body
}
// plain drops background colors and images, as a browser's print option does.
pub fn html_document(doc,title,paged=false,plain=false) string^ => "<!doctype html><html" ++
    (if (paged) " xmlns=\"http://www.w3.org/1999/xhtml\"" else "") ++ (if (plain) " class=\"print-plain\"" else "") ++ "><head><meta charset=\"utf-8\"><title>" ++
    helpers.escape_text(title) ++ "</title><meta name=\"lambda-word-metadata\" content=\"" ++
    helpers.escape_attr(format(pack_attrs([for (a in doc.attrs where contains(metadata_names,a.name)) a]),'json')^) ++
    "\"><style>" ++ surface.document_css ++ surface.print_css ++ page.export_css(doc) ++ columns.export_css(doc) ++ stories.export_css(doc) ++ review.css(doc) ++ changes.css ++
    "</style></head><body class=\"word-content\">" ++
    document_body(doc,paged) ++ "</body></html>"
pub fn pdf_document(doc,title,plain=false) string^ {
    let source = html_document(doc,title,true,plain)^
    if (columns.settings(doc).count==1) source else {
        // serialize parsed HTML as XML so void elements, text and native namespaces remain well formed.
        let parsed = parse(source,'html')^
        let root = [for (child in content(parsed) where type(child)==element and name(child)=='html') child][0]
        let head = [for (child in content(root) where type(child)==element and name(child)=='head') child][0]
        // the XML formatter retains HTML raw-text tags; escape this generated head for XML parsing.
        let xml_head = <head *[for (child in content(head))
            if (type(child)==element and name(child)=='title') <title helpers.escape_text(join(content(child),""))>
            else if (type(child)==element and name(child)=='style') <style helpers.escape_text(join(content(child),""))>
            else child]>
        format(<html *:map(root),xml_head; *[for (child in content(root) where child!=head) child]>,'xml')^
    }
}
// render to a pending file, then rename, so a failed export never replaces an existing PDF.
pn write_pdf(source,width,height,source_format,path) any^ {
    let pending = path ++ ".word-" ++ string(int(clock()*1000000)) ++ ".pdf"
    let result = radiant.export_document(source,pending,{format:"pdf",width:width,height:height,pagination:true,source_format:source_format})
    if (result != true) {
        io.delete(pending) ^ { null }
        raise error("Word: " ++ string(result))
    }
    io.rename(pending,path) ^ { io.delete(pending) ^ { null }; raise ^ }
    return true
}
pub pn export_document(doc,title,path,plain=false) any^ {
    let extension = lower(split(path,".")[last])
    if (extension == "html") { output(html_document(doc,title)^,path,{format:'text',atomic:true})^ }
    else if (extension == "txt") { output(model.plain_text(doc) ++ "\n",path,{format:'text',atomic:true})^ }
    else if (extension == "pdf") {
        let geometry = page.geometry(doc)
        write_pdf(pdf_document(doc,title,plain)^,geometry.width,geometry.height,
            if (columns.settings(doc).count>1) "xhtml" else "html",path)^
    }
    else { raise error("Word: export as .html, .txt, or .pdf") }
    return true
}
// print the measured preview sheets as fixed pages: scaled, n-up, and page-range printouts (see mod_print.composed).
// Radiant's PDF writer does not yet apply CSS transforms or overflow clipping, so each sheet is rasterized at
// print resolution (where both work, as in the preview) and the PDF carries one full-page image per sheet.
fn print_head(title,lay) => "<!doctype html><html><head><meta charset=\"utf-8\"><title>" ++ helpers.escape_text(title) ++
    "</title><style>" ++ surface.document_css ++ surface.print_css ++ "@page { size:" ++ string(lay.width_mm) ++ "mm " ++
    string(lay.height_mm) ++ "mm; margin:0; } body { margin:0; } .print-sheet + .print-sheet { break-before:page; } " ++
    ".print-image { display:block; }</style></head>"
pn sheet_image(source,lay,path) any^ {
    let result = radiant.export_document(source,path,{format:"png",width:lay.width,height:lay.height,scale:3})
    if (result != true) raise error("Word: " ++ string(result))
    let uri = media.data_uri(input(path,'binary')^,"image/png")^
    io.delete(path) ^ { null }
    return uri
}
pub pn export_sheets(doc,title,values,starts,path) any^ {
    let lay = print.layout(doc,values)
    let sheets = print.sheets(doc,values,starts)^
    if (len(sheets)==0) raise error("Word: the chosen pages are empty")
    let total = len(print.pages(doc,starts))
    let head = print_head(title,lay)
    let images = [for (i,sheet in sheets) sheet_image(head ++ "<body" ++ (if (values.backgrounds) "" else " class=\"print-plain\"") ++ ">" ++
        format(surface.print_sheet(doc,sheet,lay,total,lay.width,print.scale(values)),'html')^ ++ "</body></html>",lay,
        path ++ ".sheet-" ++ string(i+1) ++ ".png")^]
    // exact millimetre sheets: a rounded-up pixel height would spill a sliver onto an extra PDF page.
    let width = lay.width_mm*96/25.4
    let height = lay.height_mm*96/25.4
    let body = join([for (uri in images) "<div class=\"print-sheet\" style=\"width:" ++ string(width) ++ "px;height:" ++ string(height) ++
        "px\"><img class=\"print-image\" alt=\"\" src=\"" ++ uri ++ "\" style=\"width:" ++ string(width) ++ "px;height:" ++ string(height) ++ "px\"></div>"],"")
    return write_pdf(print_head(title,lay) ++ "<body>" ++ body ++ "</body></html>",lay.width,lay.height,"html",path)^
}
