// XLSX is an OPC workbook with namespace-qualified worksheet parts.
import opc: lambda.doc.opc
import xml: lambda.doc.xml

let X = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
let R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
let TYPES = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"
let LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

fn fail(code,message,part=null) error => error({code:7800,message:"xlsx: " ++ message,file:part})
fn canonical(uri) any^ => if (uri=="http://purl.oclc.org/ooxml/spreadsheetml/main") X
    else if (starts_with(uri,"http://purl.oclc.org/ooxml/officeDocument/relationships"))
        replace(uri,"http://purl.oclc.org/ooxml/officeDocument/relationships",R)
    else uri
fn child(node,tag) => xml.child(node,tag,X)
fn children(node,tag) => xml.children(node,tag,X)
fn attr(node,key) => xml.attr(node,key,"")
fn as_int(value,fallback=0) => if (value==null) fallback else int(value) ^ {fallback}
fn as_real(value,fallback=0.0) => if (value==null) fallback else float(value) ^ {fallback}
fn issue(code,part,message) => {severity:"warning",code:code,part:part,message:message}
fn root(pkg,kind) any^ => opc.related_xml(pkg,kind,fail,canonical)^
fn letters_end(text,at=0) any^ => if (at<len(text) and contains(LETTERS,slice(text,at,at+1))) letters_end(text,at+1)^ else at
fn column(text,i=0,n=0) any^ => if (i==len(text)) n-1 else column(text,i+1,n*26+index_of(LETTERS,slice(text,i,i+1))+1)^
fn position(text) map^ {
    let a=upper(text or "");
    // Reject oversized addresses before walking their characters recursively.
    if (len(a)>10) raise fail("invalid-package","Invalid cell address " ++ a)
    else {
        let at=letters_end(a)^
        let tail=slice(a,at)
        let row=as_int(tail,0)-1
        let col=column(slice(a,0,at))^;
        if (at==0 or tail=="" or not all([for (ch in ((split(tail,"")) ^ { [] })) contains("0123456789",ch)]) or
            row<0 or row>=1048576 or col<0 or col>=16384) raise fail("invalid-package","Invalid cell address " ++ a)
        else {row:row,col:col}
    }
}
// Concatenate rich text runs; phonetic annotations are not displayed text.
fn text(node) any^ => join([for (part in children(node,null))
    if (part.tag=="t") xml.tree_text(part) else if (part.tag=="r") xml.tree_text(child(part,"t")) else ""],"")
fn strings(pkg) array^ {
    let node=root(pkg,"sharedStrings")^;
    if (node==null) [] else if (node.uri!=X or node.tag!="sst") raise fail("invalid-package","Invalid shared strings")
    else [for (item in children(node,"si")) text(item)^]
}
fn color(node) any^ {
    let argb=attr(node,"rgb");
    if (argb!=null and (len(argb)==6 or len(argb)==8) and
        all([for (ch in ((split(lower(argb),"")) ^ { [] })) contains("0123456789abcdef",ch)]))
        "#" ++ lower(slice(argb,len(argb)-6)) else null
}
fn on(node) any^ => node!=null and not contains(["0","false","off"],lower(attr(node,"val") or "1"))
fn number_format(id,custom,date1904) any^ {
    let code=lower(custom or "");
    if (id>=14 and id<=17) (if (date1904) "date1904" else "date")
    else if (id==9 or id==10 or contains(code,"%")) "percent"
    else if (id>=5 and id<=8 or contains(code,"$") or contains(code,"€") or contains(code,"£")) "currency"
    else if (id>=1 and id<=4) "number" else "general"
}
fn style_context(pkg,date1904) map^ {
    let root_node=root(pkg,"styles")^;
    if (root_node==null) {fonts:[],fills:[],xfs:[],formats:{},date1904:date1904}
    else if (root_node.uri!=X or root_node.tag!="styleSheet") raise fail("invalid-package","Invalid styles")
    else {date1904:date1904,fonts:children(child(root_node,"fonts"),"font"),fills:children(child(root_node,"fills"),"fill"),
        xfs:children(child(root_node,"cellXfs"),"xf"),
        formats:map([for (n in ((children(child(root_node,"numFmts"),"numFmt")) ^ { [] })) (attr(n,"numFmtId"),attr(n,"formatCode"))])}
}
fn style(ctx,id) any^ {
    let xf=ctx.xfs[as_int(id,0)]
    let font=ctx.fonts[as_int(attr(xf,"fontId"),0)]
    let fill=ctx.fills[as_int(attr(xf,"fillId"),0)]
    let align=child(xf,"alignment")
    let family=attr(child(font,"name"),"val")
    let size=int(round(as_real(attr(child(font,"sz"),"val"),0)))
    let fill_color=color(child(child(fill,"patternFill"),"fgColor"))
    let font_color=color(child(font,"color"))
    let num_id=as_int(attr(xf,"numFmtId"),0)
    let format=number_format(num_id,ctx.formats[string(num_id)],ctx.date1904)
    let horizontal=attr(align,"horizontal");
    {*:if (on(child(font,"b"))) {bold:true} else {},
     *:if (on(child(font,"i"))) {italic:true} else {},
     *:if (on(child(font,"u"))) {underline:true} else {},
     *:if (contains(["Arial","Helvetica Neue","Georgia","Courier New"],family)) {font:family} else {},
     *:if (size>=1 and size<=72) {size:size} else {},
     *:if (font_color!=null) {color:font_color} else {},
     *:if (fill_color!=null and attr(child(fill,"patternFill"),"patternType")=="solid") {fill:fill_color} else {},
     *:if (contains(["left","center","right"],horizontal)) {align:horizontal} else {},
     *:if (on(child(xf,"alignment")) and contains(["1","true"],lower(attr(align,"wrapText") or "0"))) {wrap:true} else {},
     *:if (format!="general") {format:format} else {}}
}
fn cell(node,shared,styles,part) map^ {
    let pos=position(attr(node,"r"))^
    let typ=attr(node,"t") or "n"
    let value=xml.tree_text(child(node,"v"))
    let formula=child(node,"f")
    let index=if (typ=="s") as_int(value,-1) else -1
    let result=if (typ=="s") (if (index<0 or index>=len(shared)) raise fail("invalid-package","Invalid shared string index",part) else shared[index])
        else if (typ=="inlineStr") text(child(node,"is"))^
        else if (typ=="b") (if (value=="1") true else if (value=="0") false else raise fail("invalid-package","Invalid boolean cell",part))
        else if (typ=="n" and value!="") (float(value) ^ {raise fail("invalid-package","Invalid numeric cell",part)})
        else value;
    {*:pos,type:typ,value:result,raw_value:value,has_value:child(node,"v")!=null,formula:if (formula==null) null else xml.tree_text(formula),
        formula_type:attr(formula,"t"),style:style(styles,attr(node,"s"))}
}
fn worksheet(pkg,entry,shared,styles) map^ {
    let rel=opc.by_id(pkg.relationships,xml.attr(entry,"id",R))
    let name=attr(entry,"name");
    if (rel==null or rel.external or rel.type!=R ++ "/worksheet") raise fail("invalid-package","Missing worksheet relationship",pkg.main)
    else {
        let root_node=opc.xml(pkg.parts,rel.target,true,fail,canonical)^
        if (root_node.tag!="worksheet" or root_node.uri!=X) raise fail("invalid-package","Invalid worksheet",rel.target)
        else {
            let data=child(root_node,"sheetData")
            let rows=children(data,"row")
            let cells=[for (row in rows,node in ((children(row,"c")) ^ { [] })) cell(node,shared,styles,rel.target)^]
            let warnings=[
                if (child(root_node,"mergeCells")!=null) issue("merged-cells",rel.target,"Merged cells are not preserved"),
                if (child(root_node,"drawing")!=null) issue("drawing",rel.target,"Charts and drawings are not imported"),
                if (any([for (c in cells) c.formula!=null])) issue("formula-recalculation",rel.target,"Formula evaluation depends on the consuming application"),
                if (any([for (c in cells) c.formula_type!=null and c.formula_type!="normal"])) issue("formula-type",rel.target,"Shared and array formulas are not expanded")];
            {title:name,part:rel.target,cells:cells,
             column_widths:[for (col in ((children(child(root_node,"cols"),"col")) ^ { [] }))
                {min:as_int(attr(col,"min"),1)-1,max:as_int(attr(col,"max"),1)-1,width:as_real(attr(col,"width"),0.0)}],
             row_heights:[for (row in rows where ((attr(row,"ht")!=null) ^ { false }))
                {row:as_int(attr(row,"r"),1)-1,height:as_real(attr(row,"ht"),0.0)}],
             diagnostics:[for (warning in warnings where warning!=null) warning]}
        }
    }
}
pub fn read(source) map^ {
    let pkg=opc.capture(source,fail,canonical,"XLSX")^
    let book=pkg.root;
    if (opc.content_type(pkg,pkg.main)!=TYPES or book.uri!=X or book.tag!="workbook")
        raise fail("invalid-package","Expected an XLSX workbook",pkg.main)
    else {
        let shared=strings(pkg)^
        let date1904=contains(["1","true"],lower(attr(child(book,"workbookPr"),"date1904") or "0"))
        let styles=style_context(pkg,date1904)^
        let entries=children(child(book,"sheets"),"sheet")
        let sheets=[for (entry in entries) worksheet(pkg,entry,shared,styles)^];
        if (len(sheets)==0) raise fail("invalid-package","Workbook contains no worksheets",pkg.main)
        else {title:pkg.metadata.title or "Imported workbook",date1904:date1904,sheets:sheets,metadata:pkg.metadata,
            diagnostics:[for (sheet in sheets,warning in sheet.diagnostics) warning]}
    }
}
