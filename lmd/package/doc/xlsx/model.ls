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
// Worksheet ranges are stored as zero-based inclusive rectangles.
fn rectangle(ref) map^ {
    let parts=split(ref or "",":")^
    let a=position(parts[0])^
    let b=position(if (len(parts)==1) parts[0] else parts[1])^;
    if (len(parts)>2 or a.row>b.row or a.col>b.col) raise fail("invalid-package","Invalid worksheet range " ++ (ref or ""))
    else {top:a.row,left:a.col,bottom:b.row,right:b.col}
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
// Ignore quoted literals, escapes and color/locale brackets when classifying formats.
fn format_tokens(code,at=0,quoted=false) any^ {
    if (at>=len(code)) "" else {
        let ch=slice(code,at,at+1);
        if (ch=="\"") format_tokens(code,at+1,not quoted)^
        else if (quoted) format_tokens(code,at+1,true)^
        else if (ch=="\\" or ch=="_" or ch=="*") format_tokens(code,at+2,false)^
        else if (ch=="[") {
            let tail=slice(code,at+1)
            let end=index_of(tail,"]");
            if (end==null) "" else {
                let bracket=slice(tail,0,end)
                let token=if (bracket=="h" or bracket=="hh" or bracket=="m" or bracket=="mm" or bracket=="s" or bracket=="ss") "h" else "";
                token ++ format_tokens(code,at+end+2,false)^
            }
        }
        else (if (contains("ymdhs",ch)) ch else "") ++ format_tokens(code,at+1,false)^
    }
}
fn on_attr(node,key) any^ => contains(["1","true","on"],lower(attr(node,key) or "0"))
fn number_format(id,custom,date1904) any^ {
    let code=lower(custom or "")
    let tokens=if (len(code)>512) "" else format_tokens(code)^
    let has_time=contains(tokens,"h") or contains(tokens,"s")
    let has_date=contains(tokens,"y") or contains(tokens,"d") or (contains(tokens,"m") and not has_time)
    let date_format=if (date1904) "date1904" else "date"
    let datetime_format=if (date1904) "datetime1904" else "datetime";
    if (id==22 or (has_date and has_time)) datetime_format
    else if ((id>=14 and id<=17) or has_date) date_format
    else if ((id>=18 and id<=21) or (id>=45 and id<=47) or has_time) "time"
    else if (id==9 or id==10 or contains(code,"%")) "percent"
    else if (id>=5 and id<=8 or contains(code,"$") or contains(code,"€") or contains(code,"£")) "currency"
    else if (id>=1 and id<=4) "number" else "general"
}

fn style_context(pkg,date1904) map^ {
    let root_node=root(pkg,"styles")^;
    if (root_node==null) {fonts:[],fills:[],xfs:[],dxfs:[],formats:{},date1904:date1904}
    else if (root_node.uri!=X or root_node.tag!="styleSheet") raise fail("invalid-package","Invalid styles")
    else {date1904:date1904,fonts:children(child(root_node,"fonts"),"font"),fills:children(child(root_node,"fills"),"fill"),
        xfs:children(child(root_node,"cellXfs"),"xf"),dxfs:children(child(root_node,"dxfs"),"dxf"),
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
// Differential styles contain only properties overridden by a matching rule.
fn differential(node) any^ {
    let font=child(node,"font")
    let fill=child(child(node,"fill"),"patternFill")
    let foreground=color(child(fill,"fgColor"))
    let font_color=color(child(font,"color"));
    {*:if (child(font,"b")!=null) {bold:on(child(font,"b"))} else {},
     *:if (child(font,"i")!=null) {italic:on(child(font,"i"))} else {},
     *:if (child(font,"u")!=null) {underline:on(child(font,"u"))} else {},
     *:if (font_color!=null) {color:font_color} else {},
     *:if (foreground!=null) {fill:foreground} else {}}
}
fn condition(block,rule,styles) any^ {
    let kind=attr(rule,"type")
    let op=if (kind=="cellIs") attr(rule,"operator") else kind
    let formulas=[for (formula in children(rule,"formula")) xml.tree_text(formula)]
    let values=if (kind=="cellIs") [for (formula in formulas) float(formula) ^ {null}]
        else if (contains(["containsText","notContainsText","beginsWith","endsWith"],kind)) [attr(rule,"text")] else []
    let refs=[for (ref in ((split(attr(block,"sqref") or ""," ")) ^ { [] }) where ref!="") ref]
    let style_index=as_int(attr(rule,"dxfId"),-1)
    let dxf=if (style_index>=0 and style_index<len(styles.dxfs)) styles.dxfs[style_index] else null
    let diff=if (dxf==null) {} else differential(dxf)^
    let font=child(dxf,"font")
    let fill=child(child(dxf,"fill"),"patternFill")
    let partial=any([for (item in ((children(dxf,null)) ^ { [] })) not contains(["font","fill"],item.tag)]) or
        any([for (item in ((children(font,null)) ^ { [] })) not contains(["b","i","u","color"],item.tag)]) or
        (child(font,"color")!=null and color(child(font,"color"))==null) or
        (child(dxf,"fill")!=null and (fill==null or color(child(fill,"fgColor"))==null))
    let numeric=kind=="cellIs" and contains(["equal","notEqual","greaterThan","greaterThanOrEqual",
        "lessThan","lessThanOrEqual","between","notBetween"],op) and
        len(formulas)==(if (op=="between" or op=="notBetween") 2 else 1) and
        all([for (formula in formulas) formula!="" and len(formula)<=512])
    let expression_rule=kind=="expression" and len(formulas)==1 and formulas[0]!="" and len(formulas[0])<=512
    let text_rule=contains(["containsText","notContainsText","beginsWith","endsWith"],kind) and values[0] is string
    let blank_rule=contains(["containsBlanks","notContainsBlanks"],kind)
    let supported=len(refs)>0 and (numeric or expression_rule or text_rule or blank_rule) and
        style_index>=0 and style_index<len(styles.dxfs) and len(diff)>0;
    if (not supported) null else {
        let ranges=[for (ref in refs) rectangle(ref)^]
        let anchor={row:ranges[0].top,col:ranges[0].left};
        {ranges:ranges,anchor:anchor,
        operator:op,values:if (numeric and not all([for (value in values) value is number])) [] else values,
        *:if (expression_rule or (numeric and not all([for (value in values) value is number]))) {formulas:formulas} else {},priority:as_int(attr(rule,"priority"),0),
        stop:on_attr(rule,"stopIfTrue")^,style:diff,partial:partial}}
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
            let cols=children(child(root_node,"cols"),"col")
            let sheet_view=child(child(root_node,"sheetViews"),"sheetView")
            let pane=child(sheet_view,"pane")
            let frozen=if (contains(["frozen","frozenSplit"],attr(pane,"state")))
                {rows:as_int(attr(pane,"ySplit"),0),cols:as_int(attr(pane,"xSplit"),0)} else null
            // Split positions are twentieths of a point; frozen positions count cells.
            let split_info=if (pane!=null and frozen==null and (attr(pane,"state")==null or attr(pane,"state")=="split")) {
                let x=as_real(attr(pane,"xSplit"),0.0)
                let y=as_real(attr(pane,"ySplit"),0.0)
                let top_left=position(attr(pane,"topLeftCell") or "A1")^
                let active=attr(pane,"activePane") or "topLeft"
                let selected=[for (item in ((children(sheet_view,"selection")) ^ { [] }) where
                    (((attr(item,"pane") or "topLeft")==active and attr(item,"activeCell")!=null) ^ {false})) item][0]
                let selection=if (selected!=null) position(attr(selected,"activeCell"))^ else
                    {row:if (y==0 or starts_with(active,"bottom")) top_left.row else 0,
                        col:if (x==0 or ends_with(active,"Right")) top_left.col else 0};
                {x:x,y:y,top_left:top_left,active:active,selection:selection}
            } else null
            let split=if (split_info!=null and (split_info.x!=0 or split_info.y!=0)) split_info else null
            let merges=[for (merge in ((children(child(root_node,"mergeCells"),"mergeCell")) ^ { [] })) rectangle(attr(merge,"ref"))^]
            let blocks=children(root_node,"conditionalFormatting")
            let rules=[for (block in blocks, rule in ((children(block,"cfRule")) ^ { [] })) condition(block,rule,styles)^]
            let warnings=[
                if (child(root_node,"drawing")!=null) issue("drawing",rel.target,"Charts and drawings are not imported"),
                if (child(root_node,"sheetProtection")!=null) issue("sheet-protection",rel.target,"Worksheet protection is not enforced"),
                if (pane!=null and frozen==null and split_info==null) issue("split-panes",rel.target,"Unknown pane state is not preserved"),
                if (any([for (rule in rules) rule==null or rule.partial==true]))
                    issue("conditional-formatting",rel.target,"Some conditional formatting rules are not supported"),
                if (any([for (c in cells) c.formula!=null])) issue("formula-recalculation",rel.target,"Formula evaluation depends on the consuming application"),
                if (any([for (c in cells) c.formula_type!=null and c.formula_type!="normal"])) issue("formula-type",rel.target,"Shared and array formulas are not expanded")];
            {title:name,part:rel.target,cells:cells,merges:merges,frozen:frozen,split:split,
             conditional_rules:sort([for (rule in rules where rule!=null) rule],{by:(rule)=>rule.priority}),
             hidden_rows:[for (row in rows where on_attr(row,"hidden")^) as_int(attr(row,"r"),1)-1],
             hidden_columns:[for (col in cols where on_attr(col,"hidden")^)
                {min:as_int(attr(col,"min"),1)-1,max:as_int(attr(col,"max"),1)-1}],
             column_widths:[for (col in cols)
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
            diagnostics:[for (warning in [if (child(book,"workbookProtection")!=null)
                issue("workbook-protection",pkg.main,"Workbook structure protection is not enforced"),
                *[for (sheet in sheets,warning in sheet.diagnostics) warning]] where warning!=null) warning]}
    }
}
