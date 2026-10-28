// Editable WordprocessingML packages reuse central ZIP output (D7.5.2, S12.4.1v3).
import c: .export_common
import errors: .common
import style: .export_style
import pictures: .export_media
import tables: .export_table
import paths: lambda.io.path

let blocks_tags = ["p","h1","h2","h3","h4","h5","h6","pre","blockquote","div","section","article","main",
    "header","footer","aside","nav","figure","figcaption","ul","ol","table","hr","html","body","doc","document"]
let list_formats = {decimal:"decimal",["1"]:"decimal",["lower-alpha"]:"lowerLetter",["upper-alpha"]:"upperLetter",
    ["lower-roman"]:"lowerRoman",["upper-roman"]:"upperRoman",a:"lowerLetter",A:"upperLetter",i:"lowerRoman",I:"upperRoman"}
fn options(value) map^ {
    let valid_options = if (value!=null and not (value is map)) raise errors.fail("invalid-options","DOCX export options must be a map")
    let opts = if (value==null) {} else value
    let valid_page = if (opts.page!=null and not (opts.page is map)) raise errors.fail("invalid-options","page must be a map")
    let names = ["title","creator","subject","description","unsupported","base","styles","page"]
    let page = {*:{width:793.7007874,height:1122.519685,top:96.0,right:96.0,bottom:96.0,left:96.0},*:opts.page or {}}
    let invalid = [for (key,item in opts where not (string(key) in names)) string(key)];
    if (not (opts is map) or len(invalid)>0) raise errors.fail("invalid-options","Invalid DOCX export options")
    else if (not ((opts.unsupported or "warn") in ["warn","error"])) raise errors.fail("invalid-options","unsupported must be warn or error")
    else if (not (opts.styles==null or opts.styles is map) or any([for (key,v in opts.styles) not (v is string)]))
        raise errors.fail("invalid-options","styles must map element names to CSS declarations")
    else if (any([for (key in ["title","creator","subject","description","base"]) opts[key]!=null and not (opts[key] is string)]))
        raise errors.fail("invalid-options","Document metadata and base must be strings")
    else if (any([for (key,v in page) not (string(key) in ["width","height","top","right","bottom","left"])]))
        raise errors.fail("invalid-options","Unknown DOCX page option")
    else if (not (opts.page==null or opts.page is map) or any([for (key,v in page) not style.finite(v) or v<0]) or
        page.width<=page.left+page.right or page.height<=page.top+page.bottom or page.width>100000 or page.height>100000)
        raise errors.fail("invalid-options","Page dimensions must leave a positive writing area")
    else {*:opts,unsupported:opts.unsupported or "warn",styles:opts.styles or {},page:page}
}
fn valid_xml(value) => all([for (ch in split(value,""),let n=ord(ch))
    n in [9,10,13] or (n>=32 and n<=1114111 and not (n>=55296 and n<=57343) and n!=65534 and n!=65535)])
fn list_records(nodes) => [for (i,entry in [for (n in nodes where c.tag(n.node) in ["ul","ol"]) n]) {
    let raw = style.css_get(entry.node.style,"list-style-type") or entry.node.type
    let format = if (c.tag(entry.node)=="ul") "bullet" else list_formats[raw] or "decimal"
    let start = int(entry.node.start or 1) ^ { 1 }
    let level = len([for (parent in nodes where c.tag(parent.node) in ["ul","ol"] and len(parent.path)<len(entry.path) and
        take(entry.path,len(parent.path))==parent.path) parent]);
    {id:i+1,path:c.path_key(entry.path),format:format,start:start,level:min(8,level),bullet:if (raw=="square") "▪" else if (raw=="circle") "○" else "•",
        diagnostics:if (level>8 or (raw!=null and list_formats[raw]==null and not (raw in ["disc","circle","square"])))
            [c.issue("numbering","List format or depth was approximated with the supported Word numbering profile")] else []}
}]
fn bookmark(ctx,id) => [for (entry in ctx.bookmarks where entry.source==id) entry][0]
fn boundaries(node,ctx,ending=false) {
    let entry = bookmark(ctx,node.id);
    if (entry==null) [] else if (ending) [<'w:bookmarkEnd' 'w:id':entry.id>]
    else [<'w:bookmarkStart' 'w:id':entry.id,'w:name':entry.name>]
}
fn text_runs(value,props) {
    let text = replace(replace(value,"\r\n","\n"),"\r","\n")
    let lines = split(if (props.preserve==true) text else replace(text,"\n"," "),"\n");
    [for (i,line in lines) *[
        if (i>0) <'w:r' style.run(props); <'w:br'>>,
        *[for (j,part in split(line,"\t")) *[
            if (j>0) <'w:r' style.run(props); <'w:tab'>>,
            if (part!="") <'w:r' style.run(props); <'w:t' 'xml:space':"preserve",part>>]]]]
}
fn inlines(nodes,path,ctx) => c.merge([for (i,n in nodes) inline_node(n,[*path,i],ctx)])
fn inline_node(n,path,ctx) {
    if (n is string or n is symbol) c.result(text_runs(string(n),ctx.props))
    else if (not (n is element)) c.result([],[c.issue("value","Non-text inline value was omitted")])
    else {
        let tag = c.tag(n)
        let defaults = ctx.options.styles[tag] or ""
        let props = style.inherit(n,ctx.props,defaults)
        let nested = {*:ctx,props:props}
        let content = if (props.hidden==true or tag in ["script","style","head"]) c.result()
            else if (tag=="br") c.result([<'w:r' <'w:br' *:if (n['data-word-column-break']=="true") {'w:type':"column"} else {}>>])
            else if (tag=="img") {
                let record = [for (image in ctx.images where image.src==(n.src or "")) image][0]
                let id = [for (i,entry in ctx.nodes where entry.path==path) i+1][0];
                if (record.bytes==null) c.result(text_runs(n.alt or "[Image]",props))
                else c.result([<'w:r' pictures.drawing(n,record,id or 1)>])
            } else if (tag=="a") {
                let body = inlines(c.kids(n),path,nested)
                let local = if (starts_with(n.href or "","#")) bookmark(ctx,slice(n.href,1)) else null
                let link = [for (entry in ctx.links where entry.href==n.href) entry][0];
                if (n.href==null) body
                else if (local!=null or link!=null) c.result([<'w:hyperlink'
                    *:if (local!=null) {'w:anchor':local.name} else {'r:id':link.id},*body.nodes>],body.diagnostics)
                else c.result(body.nodes,[*body.diagnostics,c.issue("link","An unsafe or unresolved hyperlink retained its visible text")])
            } else {
                let body = inlines(c.kids(n),path,nested);
                c.result(body.nodes,body.diagnostics ++ (if (tag in ["span","strong","b","em","i","u","s","del","strike","sub","sup","code","small","mark"]) []
                    else [c.issue("element","Element " ++ tag ++ " retained its text without its original representation")]))
            };
        c.result([*boundaries(n,ctx),*content.nodes,*boundaries(n,ctx,true)],content.diagnostics ++ style.diagnostics(n,defaults ++ ";" ++ (n.style or "")))
    }
}
fn paragraph(n,path,ctx,children=null) {
    let defaults = (ctx.options.styles[c.tag(n)] or "") ++ ";" ++
        (if (ctx.cell==true) ctx.options.styles["td " ++ c.tag(n)] or "" else "")
    let props = style.inherit(n,ctx.props,defaults)
    let inline = if (children==null) inlines(c.kids(n),path,{*:ctx,props:props}) else children
    let css = defaults ++ ";" ++ (n.style or "");
    if (props.hidden==true) c.result() else c.result([<'w:p' style.paragraph(n,props,ctx.numbering,defaults);
        *boundaries(n,ctx); *inline.nodes; *boundaries(n,ctx,true);
        if (style.css_get(css,"page-break-after")=="always" or style.css_get(css,"break-after")=="page") <'w:r' <'w:br' 'w:type':"page">> >],
        inline.diagnostics ++ style.diagnostics(n,css))
}
fn write_list(n,path,ctx) {
    let record = [for (entry in ctx.lists where entry.path==c.path_key(path)) entry][0]
    let props = style.inherit(n,ctx.props,ctx.options.styles[c.tag(n)] or "");
    c.merge([for (i,item in c.kids(n) where c.tag(item)=="li") {
        let nesting = record.level+min(8-record.level,max(0,int(item.indent or 0) ^ { 0 }))
        let body = blocks(c.kids(item),[*path,i],{*:ctx,props:style.inherit(item,props),numbering:{id:record.id,level:nesting}});
        c.result(if (len(body.nodes)>0 and string(name(body.nodes[0]))=="w:p") body.nodes else
            [<'w:p' style.paragraph(item,props,{id:record.id,level:nesting})>,*body.nodes],
            body.diagnostics ++ (if (item.value==null) [] else [c.issue("numbering","Per-item list value resets are outside the export profile")]))
    }])
}
fn block(n,path,ctx) {
    let tag = c.tag(n)
    let defaults = if (tag=="body") "" else ctx.options.styles[tag] or ""
    let props = style.inherit(n,ctx.props,defaults);
    if (props.hidden==true) c.result()
    else if (tag in ["ul","ol"]) write_list(n,path,ctx)
    else if (tag=="table") tables.render(n,path,{*:ctx,props:style.inherit(n,ctx.props,ctx.options.styles.table or "")},blocks)^
    else if (tag in ["p","h1","h2","h3","h4","h5","h6","pre","figcaption"]) paragraph(n,path,ctx)
    else if (tag=="hr") c.result([<'w:p' <'w:pPr' <'w:pBdr' <'w:bottom' 'w:val':"single",'w:sz':"6",'w:color':"808080">>> >])
    else {
        let body = blocks(c.kids(n),path,{*:ctx,props:props});
        c.result(body.nodes,body.diagnostics ++ style.diagnostics(n,defaults ++ ";" ++ (n.style or "")))
    }
}
pub fn blocks(nodes,path,ctx,index=0,out=[],diagnostics=[]) map^ {
    if (index>=len(nodes)) c.result(out,diagnostics) else {
        let n = nodes[index]
        let tag = c.tag(n)
        let is_block = tag in blocks_tags
        let next_block = [for (j in range(index+1,len(nodes),1) where c.tag(nodes[j]) in blocks_tags) j][0]
        let end = if (is_block) index+1 else next_block or len(nodes)
        let converted = if (is_block) block(n,[*path,index],ctx)^
            else if (all([for (value in slice(nodes,index,end)) value is string and trim(value)==""])) c.result()
            else paragraph(<p>,path,ctx,c.merge([for (j in range(index,end,1)) inline_node(nodes[j],[*path,j],ctx)]));
        blocks(nodes,path,{*:ctx,numbering:if (len(converted.nodes)>0) null else ctx.numbering},end,
            [*out,*converted.nodes],[*diagnostics,*converted.diagnostics])^
    }
}
fn numbering(lists) { <'w:numbering' 'xmlns:w':c.W,
    *[for (entry in lists) <'w:abstractNum' 'w:abstractNumId':entry.id,
        <'w:multiLevelType' 'w:val':"multilevel">;
        *[for (level in 0 to 8) <'w:lvl' 'w:ilvl':level,
            <'w:start' 'w:val':entry.start><'w:numFmt' 'w:val':entry.format>;
            <'w:lvlText' 'w:val':if (entry.format=="bullet") entry.bullet else "%" ++ string(level+1) ++ ".">;
            <'w:lvlJc' 'w:val':"left"><'w:pPr' <'w:tabs' <'w:tab' 'w:val':"num",'w:pos':720*(level+1)>>;
                <'w:ind' 'w:left':720*(level+1),'w:hanging':360>> >]>];
    *[for (entry in lists) <'w:num' 'w:numId':entry.id,<'w:abstractNumId' 'w:val':entry.id>>]>
}
fn styles(opts) { <'w:styles' 'xmlns:w':c.W,
    <'w:docDefaults' <'w:rPrDefault' style.run(style.inherit(<body>,{},opts.styles.body or "font-family:Calibri;font-size:11pt;"))>>;
    <'w:style' 'w:type':"paragraph",'w:default':"1",'w:styleId':"Normal",<'w:name' 'w:val':"Normal">>;
    *[for (level in 1 to 6) <'w:style' 'w:type':"paragraph",'w:styleId':"Heading" ++ string(level),
        <'w:name' 'w:val':"heading " ++ string(level)><'w:basedOn' 'w:val':"Normal"><'w:next' 'w:val':"Normal">;
        <'w:pPr' <'w:keepNext'><'w:outlineLvl' 'w:val':level-1>>;
        style.run(style.inherit(<span>,{},opts.styles["h" ++ string(level)] or
            "font-weight:bold;font-size:" ++ string([24,18,14,12,11,11][level-1]) ++ "pt;"))>]>
}
fn section(opts) {
    let p = opts.page;
    <'w:sectPr' <'w:pgSz' 'w:w':int(round(p.width*15.0)),'w:h':int(round(p.height*15.0)),
        *:if (p.width>p.height) {'w:orient':"landscape"} else {}>;
        <'w:pgMar' 'w:top':int(round(p.top*15.0)),'w:right':int(round(p.right*15.0)),
            'w:bottom':int(round(p.bottom*15.0)),'w:left':int(round(p.left*15.0)),'w:header':720,'w:footer':720,'w:gutter':0>>
}
pub fn export_document(value,requested=null) map^ {
    let opts = options(requested)^
    let source = if (value is string) parse(value,'html')^ else value
    let original = if (source is element) c.walk(source)^ else []
    let root = c.root(source)^
    let title = opts.title or [for (entry in original where c.tag(entry.node)=="title") c.text(entry.node)][0] or "Untitled document"
    let nodes = c.walk(root)^
    let strings = [for (entry in nodes) if (entry.node is string) entry.node else *[for (key,v in c.attrs(entry.node) where v is string) v]]
    let checked_xml = if (not all([for (v in [*strings,*[for (key,v in opts.styles) v],title,opts.creator or "",opts.subject or "",opts.description or ""]) valid_xml(v)]))
        raise errors.fail("invalid-source","Document contains characters forbidden by XML 1.0")
    let images = pictures.records(nodes,opts)
    let links = [for (i,href in unique([for (entry in nodes where c.tag(entry.node)=="a" and
        not starts_with(entry.node.href or "","#") and errors.safe_link(entry.node.href)) entry.node.href])) {href:href,id:"rIdLink" ++ string(i+1)}]
    let bookmarks = [for (i,id in unique([for (entry in nodes where entry.node.id is string and entry.node.id!="") entry.node.id]))
        {source:id,id:i+1,name:"_lambdaBookmark" ++ string(i+1)}]
    let lists = list_records(nodes)
    let context = {options:opts,nodes:nodes,images:images,links:links,bookmarks:bookmarks,lists:lists,
        props:style.inherit(<body>,{},opts.styles.body or ""),numbering:null,page_width:(opts.page.width-opts.page.left-opts.page.right)*15.0}
    let body = if (c.tag(root) in blocks_tags) block(root,[],context)^ else paragraph(<p>,[],context,inline_node(root,[],context))
    let stylesheet = any([for (entry in original) c.tag(entry.node)=="style" or
        (c.tag(entry.node)=="link" and entry.node.rel=="stylesheet")]) and len(opts.styles)==0
    let diagnostics = unique([*body.diagnostics,*[for (record in [*images,*lists]) *record.diagnostics],
        *if (stylesheet) [c.issue("stylesheet","Stylesheets are outside the export profile; use inline styles or the styles option")] else []])
    let checked_loss = if (opts.unsupported=="error" and len(diagnostics)>0)
        raise errors.fail("unsupported",diagnostics[0].code ++ ": " ++ diagnostics[0].message)
    let document = <'w:document' 'xmlns:w':c.W,'xmlns:r':c.R,
        'xmlns:wp':"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing",
        'xmlns:a':"http://schemas.openxmlformats.org/drawingml/2006/main",'xmlns:pic':"http://schemas.openxmlformats.org/drawingml/2006/picture",
        <'w:body' *body.nodes; if (len(body.nodes)==0) <'w:p'>; section(opts)>>
    let relations = <Relationships xmlns:errors.P,
        c.relationship("rIdStyles","styles","styles.xml"); c.relationship("rIdNumbering","numbering","numbering.xml");
        *[for (record in images where record.bytes!=null) c.relationship(record.id,"image","media/" ++ record.filename)];
        *[for (link in links) c.relationship(link.id,"hyperlink",link.href,true)]>
    let types = <Types xmlns:errors.CT,
        <Default Extension:"rels",ContentType:"application/vnd.openxmlformats-package.relationships+xml">;
        <Default Extension:"xml",ContentType:"application/xml">;
        *[for (extension in unique([for (image in images where image.bytes!=null) split(image.filename,".")[last]]),
            let image=[for (image in images where ends_with(image.filename or "","." ++ extension)) image][0]) <Default Extension:extension,ContentType:image.mime>];
        *[for (part in [{name:"document",kind:"document.main"},{name:"styles",kind:"styles"},{name:"numbering",kind:"numbering"}])
            <Override PartName:"/word/" ++ part.name ++ ".xml",ContentType:"application/vnd.openxmlformats-officedocument.wordprocessingml." ++ part.kind ++ "+xml">];
        <Override PartName:"/docProps/core.xml",ContentType:"application/vnd.openxmlformats-package.core-properties+xml">>
    let core = <'cp:coreProperties' 'xmlns:cp':"http://schemas.openxmlformats.org/package/2006/metadata/core-properties",'xmlns:dc':"http://purl.org/dc/elements/1.1/",
        <'dc:title' title><'dc:creator' opts.creator or "Lambda">;
        if (opts.subject!=null) <'dc:subject' opts.subject>; if (opts.description!=null) <'dc:description' opts.description>>;
    {archive:<fs kind:'dir',format:'zip',
        c.xml_file("[Content_Types].xml",types);
        <fs name:"_rels",kind:'dir',c.xml_file(".rels",<Relationships xmlns:errors.P,
            c.relationship("rIdDocument","officeDocument","word/document.xml");
            <Relationship Id:"rIdCore",Type:"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties",Target:"docProps/core.xml">>)>;
        <fs name:"docProps",kind:'dir',c.xml_file("core.xml",core)>;
        <fs name:"word",kind:'dir',c.xml_file("document.xml",document); c.xml_file("styles.xml",styles(opts)); c.xml_file("numbering.xml",numbering(lists));
            <fs name:"_rels",kind:'dir',c.xml_file("document.xml.rels",relations)>;
            if (any([for (image in images) image.bytes!=null])) <fs name:"media",kind:'dir',
                *[for (image in images where image.bytes!=null) <fs name:image.filename,kind:'file',image.bytes>]>>>,diagnostics:diagnostics}
}
pub pn save(document,path,opts=null) map^ {
    let result = export_document(document,opts)^;
    output(result.archive,path,{format:'zip',atomic:true,deterministic:true})^
    return {path:path,diagnostics:result.diagnostics}
}
pub fn export_file(path,opts=null) map^ {
    let options = opts or {}
    let kind = options.input_type or "auto"
    let source = input(path,{type:kind,flavor:options.input_flavor or ""})^
    let plain = kind in ["text","txt"] or (kind=="auto" and ends_with(lower(path),".txt"))
    let prepared = if (plain) <article *[for (line in split(string(source),"\n")) <p line>]> else source;
    export_document(prepared,{base:paths.dirname(path),unsupported:if (options.loss=="allow") "warn" else "error"})^
}
