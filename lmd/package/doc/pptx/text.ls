// DrawingML runs remain editable HTML; inherited properties merge before direct formatting.
import c: .common
import opc: lambda.doc.opc
import theme: lambda.doc.docx.theme

fn property_slot(node) => if (node.tag in ["buNone","buChar","buAutoNum","buBlip"]) "bullet"
    else if (node.tag in ["solidFill","noFill","gradFill","pattFill","blipFill","grpFill"]) "fill" else node.tag
pub fn merge(nodes) {
    let present = [for (node in nodes where node!=null) node];
    {attrs:map([for (node in present,key,value in node.attrs) (key,value)]),
        children:[for (i,node in present,child in c.children(node,null,null) where
            not any([for (j,later in present,other in c.children(later,null,child.uri) where j>i)
                property_slot(other)==property_slot(child)])) child]}
}
fn font(properties,colors) {
    let latin = c.attr(c.child(properties,"latin",c.A),"typeface")
    let ea = c.attr(c.child(properties,"ea",c.A),"typeface");
    if (starts_with(latin,"+mj")) colors.fonts.major
    else if (starts_with(latin,"+mn")) colors.fonts.minor
    else if (latin!=null and latin!="") latin
    else if (ea!=null and ea!="" and not starts_with(ea,"+")) ea else null
}
fn run_style(properties,ctx) {
    let family = font(properties,ctx.theme)
    let baseline = c.num(c.attr(properties,"baseline"))
    let size = c.num(c.attr(properties,"sz"),0.0)*96.0/7200.0*c.get(ctx,"text_scale",1.0)
    let underline = c.attr(properties,"u")
    let strike = c.attr(properties,"strike");
    c.css({["font-family"]:if (family==null) null else format(family,'json')^,
        ["font-size"]:if (size<=0) null else c.px(size),
        ["font-weight"]:if (c.attr(properties,"b")==null) null else if (c.truth(c.attr(properties,"b"))) "700" else "400",
        ["font-style"]:if (c.attr(properties,"i")==null) null else if (c.truth(c.attr(properties,"i"))) "italic" else "normal",
        ["text-decoration"]:if (underline==null and strike==null) null else
            if ((underline==null or underline=="none") and (strike==null or strike=="noStrike")) "none" else
                (if (underline!=null and underline!="none") "underline " else "") ++
                (if (strike!=null and strike!="noStrike") "line-through" else ""),
        ["text-decoration-style"]:if (underline in ["wavy","wavyHeavy","wavyDbl"]) "wavy" else if (underline in ["dbl","wavyDbl"]) "double" else null,
        ["letter-spacing"]:if (c.attr(properties,"spc")==null) null else c.px(c.num(c.attr(properties,"spc"))*96.0/7200.0),
        color:theme.drawing_color(c.child(properties,"solidFill",c.A),ctx.theme,null,true),
        ["vertical-align"]:if (baseline>0) "super" else if (baseline<0) "sub" else null})
}
fn linked(run,properties,ctx) {
    let link = c.child(properties,"hlinkClick",c.A)
    let rel = opc.by_id(ctx.relationships,c.attr(link,"id",c.R))
    let href = if (rel.external and rel.type==c.R ++ "/hyperlink" and c.safe_link(rel.target)) rel.target else null;
    if (href==null) run else <a href:href,run>
}
fn run(node,defaults,ctx) {
    let properties = merge([defaults,c.child(node,"rPr",c.A)])
    let value = c.text(c.child(node,"t",c.A));
    if (node.tag=="br") <br>
    else if (node.tag=="tab") "\t"
    else if (node.tag in ["r","fld"]) linked(<span style:run_style(properties,ctx),
        *:if (c.attr(properties,"lang")==null) {} else {lang:c.attr(properties,"lang")},value>,properties,ctx)
    else null
}
fn level_properties(styles,level) => [for (style in styles,
    node in [c.child(style,"defPPr",c.A),c.child(style,"lvl" ++ string(level+1) ++ "pPr",c.A)] where node!=null) node]
fn spacing(node) {
    let points = c.attr(c.child(node,"spcPts",c.A),"val")
    let percent = c.attr(c.child(node,"spcPct",c.A),"val");
    if (points!=null) c.px(c.num(points)*96.0/7200.0) else if (percent!=null) string(c.num(percent)/1000.0) ++ "%" else null
}
fn paragraph_info(node,styles) {
    let direct = c.child(node,"pPr",c.A)
    let level = int(c.num(c.attr(direct,"lvl")))
    let properties = merge([*level_properties(styles,level),direct])
    let defaults = merge([for (item in [*level_properties(styles,level),direct]) c.child(item,"defRPr",c.A)])
    let auto = c.child(properties,"buAutoNum",c.A);
    {node:node,properties:properties,defaults:defaults,level:level,auto:auto,
        restart:c.attr(c.child(direct,"buAutoNum",c.A),"startAt")}
}
fn numbered_paragraphs(nodes,styles) => reduce([{counters:{},output:[]},*[for (node in nodes) paragraph_info(node,styles)]],(acc,info) => {
    let key = string(info.level)
    let kind = c.attr(info.auto,"type")
    let previous = acc.counters[key]
    let initial = int(c.num(c.attr(info.auto,"startAt"),1.0))
    let count = if (info.restart!=null) int(c.num(info.restart,1.0))
        else if (kind!=null and previous.kind==kind) previous.count+1 else initial
    // Leaving a list resets that level; descending to a nested level preserves its parent count.
    let counters = map([for (level,value in acc.counters where int(level)<info.level) (level,value)]);
    {counters:{*:counters,*:if (kind==null) {} else {[key]:{kind:kind,count:count}}},
        output:[*acc.output,{*:info,count:count}]}
}).output
fn paragraph(info,ctx) {
    let node = info.node
    let properties = info.properties
    let defaults = info.defaults
    let level = info.level
    let align = {l:"left",ctr:"center",r:"right",just:"justify",dist:"justify"}[c.attr(properties,"algn")]
    let char = c.attr(c.child(properties,"buChar",c.A),"char")
    let auto = info.auto
    let numbered = auto!=null and c.child(properties,"buNone",c.A)==null
    let kind = c.attr(auto,"type")
    let numeric = string(info.count)
    let marker = if (c.child(properties,"buNone",c.A)!=null) "" else if (char!=null) char ++ " "
        else if (numbered) (if (kind=="arabicPlain") numeric else if (kind=="arabicParenBoth") "(" ++ numeric ++ ")"
            else numeric ++ (if (kind=="arabicParenR") ")" else ".")) ++ " " else "";
    <p style:c.css({margin:"0",["text-align"]:align,["line-height"]:spacing(c.child(properties,"lnSpc",c.A)),
        ["margin-top"]:spacing(c.child(properties,"spcBef",c.A)),["margin-bottom"]:spacing(c.child(properties,"spcAft",c.A)),
        ["margin-left"]:if (c.attr(properties,"marL")!=null) c.px(c.pixels(c.attr(properties,"marL"))) else if (marker!="") c.px(level*24.0) else null,
        ["text-indent"]:if (c.attr(properties,"indent")==null) null else c.px(c.pixels(c.attr(properties,"indent")))}) ++ run_style(defaults,ctx),
        *[marker,*[for (child in c.children(node,null,c.A) where child.tag in ["r","br","tab","fld"]) run(child,defaults,ctx)],
            if (len(c.children(node,"r",c.A))==0 and len(c.children(node,"fld",c.A))==0) <br> else null]>
}
pub fn body(bodies,styles,ctx,fill=true) {
    let own = bodies[last]
    let properties = merge([for (body in bodies) c.child(body,"bodyPr",c.A)])
    let lists = [*styles,*[for (body in bodies) c.child(body,"lstStyle",c.A)]]
    let anchor = c.attr(properties,"anchor")
    let vertical = c.attr(properties,"vert")
    let inset = {lIns:9.6,rIns:9.6,tIns:4.8,bIns:4.8};
    // Cell content sizes its row intrinsically; shape text fills an explicitly positioned box.
    <div style:"box-sizing:border-box;width:100%;" ++ (if (fill) "height:100%;" else "") ++
        c.css({padding:join([for (key in ["tIns","rIns","bIns","lIns"]) c.px(c.pixels(c.attr(properties,key),inset[key]))]," "),
            display:if (anchor in ["ctr","b"]) "flex" else null,
            ["flex-direction"]:if (anchor in ["ctr","b"]) "column" else null,
            ["justify-content"]:if (anchor=="ctr") "center" else if (anchor=="b") "flex-end" else null,
            ["writing-mode"]:if (vertical in ["vert","eaVert"]) "vertical-rl" else null}),
        *[for (info in numbered_paragraphs(c.children(own,"p",c.A),lists)) paragraph(info,ctx)]>
}
pub fn diagnostics(node,ctx) => [*[for (link in c.descendants(node,"hlinkClick",c.A),
    let rel=opc.by_id(ctx.relationships,c.attr(link,"id",c.R)) where
        rel==null or not rel.external or rel.type!=c.R ++ "/hyperlink" or not c.safe_link(rel.target))
    c.issue("unsupported-link",ctx.main,"A missing, unsafe or internal hyperlink was kept as text")],
    *[for (auto in c.descendants(node,"buAutoNum",c.A) where not (c.attr(auto,"type") in
        ["arabicPeriod","arabicPlain","arabicParenR","arabicParenBoth"]))
        c.issue("unsupported-numbering",ctx.main,"A list numbering format uses decimal markers")],
    *[for (bullet in c.descendants(node,"buBlip",c.A)) c.issue("unsupported-bullet",ctx.main,"An image bullet was omitted")]]
pub fn plain(node) => if (node is string) node else if (node is element) {
    if (name(node)=='table') join([for (body in content(node),row in content(body) where name(body)=='tbody')
        join([for (cell in content(row)) trim(join([for (child in content(cell)) plain(child)],""))],"\t")],"\n")
    else join([for (child in content(node)) plain(child)],"") ++ (if (name(node) in ['p','li','br']) "\n" else "")
} else ""
