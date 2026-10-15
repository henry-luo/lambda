import c: .common
import epub: .epub
import res: .resources
import xml: lambda.doc.xml
import uri: lambda.doc.uri

let blocked = ["script","iframe","object","embed","base","form","input","button","select","textarea","meta","xmp","plaintext","noembed","noframes","noscript","applet","portal","frame","frameset"]
let voids = ["img","br","hr","link","meta","source","wbr","col"]
let svg = "http://www.w3.org/2000/svg"
let math = "http://www.w3.org/1998/Math/MathML"
fn lower_attrs(book,node,owner,path,chain) map^ {
    let ordinary = map([for (key,value in node.attrs,let pieces = split(string(key),"#"),let local = pieces[len(pieces)-1],
        let ns = join(take(pieces,len(pieces)-1),"#") where (ns == "" or ns == xml.XML) and
            not starts_with(lower(local),"on") and not starts_with(local,"__lambda_") and not (lower(local) in
                ["src","href","srcset","style","action","formaction","poster","background","data","http-equiv","content"])
            and not starts_with(local,"data-ebook-")) (local,value)])
    let style = if (c.attr(node,"style") == null) {text:null,diagnostics:[]}
        else res.css(book,c.attr(node,"style"),owner,chain,svg_resource)^
    let src = c.attr(node,"src")
    let href = c.value(c.attr(node,"href"),c.attr(node,"href","http://www.w3.org/1999/xlink"))
    let resource = if (href != null and node.uri == svg and starts_with(href,"#")) {value:href,diagnostics:[]}
        else if (src != null or (href != null and node.uri == svg))
        res.asset(book,owner,c.value(src,href),svg_resource,chain)^ else {value:null,diagnostics:[]}
    let target = if (href != null and node.tag == "a" and not contains(href,":") and not starts_with(href,"//"))
        c.target(owner,href)^ else null
    let attributes = {*:ordinary,["data-ebook-node"]:path,
        *:if (style.text == null) {} else {style:style.text},
        *:if (resource.value == null or resource.value == "") {} else
            if (src != null) {src:resource.value} else {href:resource.value},
        *:if (target == null) {} else {href:c.url(target),
            ["data-ebook-href"]:target.href,["data-ebook-fragment"]:c.value(target.fragment,"")}};
    {attributes:attributes,diagnostics:style.diagnostics ++ resource.diagnostics}
}
fn lower_node(book,node,owner,path,depth = 0,chain = []) map^ {
    if (depth > 128) raise c.fail("limit","Chapter nesting exceeds 128",owner)
    else if (node is string) {text:res.escape(node),diagnostics:[]}
    else if (node == null) {text:"",diagnostics:[]}
    else if (node.tag in blocked or not (node.uri in ["",c.XHTML,svg,math]))
        {text:"",diagnostics:[c.issue("blocked-content",owner,"Excluded active/unsupported element " ++ node.tag)]}
    else if (node.tag == "link") {
        if (c.attr(node,"rel") != "stylesheet") {text:"",diagnostics:[]} else {
            let href = c.attr(node,"href");
            if (href == null or contains(href,":") or starts_with(href,"//"))
                {text:"",diagnostics:[c.issue("blocked-resource",owner,"External stylesheet blocked")]}
            else {
                let sheet = res.stylesheet(book,c.target(owner,href)^.href,chain,svg_resource)^;
                {text:"<style>" ++ res.escape(sheet.text) ++ "</style>",diagnostics:sheet.diagnostics}
            }
        }
    } else {
        let attrs = lower_attrs(book,node,owner,path,chain)^
        let kids = if (node.tag == "style") {
            let sheet = res.css(book,c.text(node),owner,chain,svg_resource)^;
            [{text:res.escape(sheet.text),diagnostics:sheet.diagnostics}]
        } else [for (i,child in node.children) lower_node(book,child,owner,path ++ "-" ++ string(i),depth+1,chain)^]
        let head = if (node.tag == "html" and c.child(node,"head",c.XHTML) == null) "<head></head>" else ""
        let declaration = if (node.tag in ["html","svg","math"]) " xmlns=\"" ++
            (if (node.tag == "svg") svg else if (node.tag == "math") math else c.XHTML) ++ "\"" else ""
        let attributes = join([for (key,value in attrs.attributes)
            " " ++ string(key) ++ "=\"" ++ res.escape(value) ++ "\""],"");
        {text:"<" ++ node.tag ++ declaration ++ attributes ++ (if (node.tag in voids) " />" else
            ">" ++ head ++ join([for (kid in kids) kid.text],"") ++ "</" ++ node.tag ++ ">"),
            diagnostics:attrs.diagnostics ++ [for (kid in kids,issue in kid.diagnostics) issue]}
    }
}
fn svg_resource(book,path,chain) map^ => lower_node(book,c.tree(epub.source_text(book,path)^,path)^,path,"svg",0,chain)^
// Reader covers use the same resource rewriting and SVG sanitization as chapters.
pub fn resource(book,path) map^ => res.asset(book,"",uri.url(path),svg_resource)^
fn text_nodes(node,path = "n") => if (not (node is map) or node.tag in blocked or node.tag in ["head","style"]) []
    else [for (i,child in node.children,segment in (if (child is string and child != "")
        [{node:path,text:child}] else if (child is map) text_nodes(child,path ++ "-" ++ string(i)) else [])) segment]
fn dimension(value) => float(if (value is string and ends_with(value,"px")) slice(value,0,len(value)-2) else value) ^ { null }
fn viewport(root) {
    if (root.uri == svg) {
        let box = split(trim(c.value(c.attr(root,"viewBox"),""))," ");
        {width:c.value(dimension(c.attr(root,"width")),if (len(box) == 4) dimension(box[2]) else null),
            height:c.value(dimension(c.attr(root,"height")),if (len(box) == 4) dimension(box[3]) else null)}
    } else {
        let node = c.first([for (node in c.descendants(root,"meta") where c.attr(node,"name") == "viewport") node])
        let fields = map([for (field in split(c.value(c.attr(node,"content"),""),","),
            let pair = split(field,"=") where len(pair) == 2) (trim(pair[0]),trim(pair[1]))]);
        {width:dimension(fields.width),height:dimension(fields.height)}
    }
}
pub fn load(book,target) map^ {
    let entry = if (target is int) book.reading_order[target] else c.first([for (entry in book.reading_order
        where (if (target is map) target.href else split(target,"#")[0]) in [entry.href,entry.source_href]) entry])
    let valid = if (entry == null) raise c.fail("invalid-location","Chapter is not in the reading order") else true
    let source = epub.source_text(book,entry.href)^
    let root = if (entry.media_type == "image/svg+xml") c.tree(source,entry.href)^
        else xml.tree(xml.root(parse(source,"xhtml")^))
    let result = lower_node(book,root,entry.href,"n")^
    let page_viewport = if (entry.layout == "fixed") viewport(root) else null
    let viewport_valid = if (entry.layout == "fixed" and (page_viewport.width == null or page_viewport.height == null or
        page_viewport.width <= 0 or page_viewport.height <= 0))
        raise c.fail("invalid-package","Fixed-layout content requires a finite positive source viewport",entry.href) else true
    let html = if (root.uri == svg) "<html xmlns=\"" ++ c.XHTML ++ "\"><head></head><body>" ++ result.text ++ "</body></html>" else result.text
    let document = parse(html,"xhtml")^
    let segments = text_nodes(root)
    let text = join([for (segment in segments) segment.text],"")
    let offsets = math.cumsum([0,*[for (segment in segments) len(segment.text)]])
    let source_map = [for (i,segment in segments) {*:segment,offset:offsets[i]}];
    {href:entry.href,occurrence:entry.occurrence,layout:entry.layout,viewport:page_viewport,source:root,html:format(document,"html")^,
        document:document,text:text,source_map:source_map,diagnostics:result.diagnostics}
}
