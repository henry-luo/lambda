// CSS lengths use 96 dpi; Word runs use half-points and paragraphs use twips.
import c: .export_common
import inline: lambda.editor.mod_inline_format
import colors: lambda.slide.color
import tokens: lambda.ui.dtna.tokens
import values: lambda.slide.common
pub let css_get = inline.css_get
pub let finite = values.finite
pub fn length(value,em=16.0) {
    if (value==null) null else {
        let raw = lower(trim(string(value)))
        let unit = [for (u in ["px","pt","pc","mm","cm","in","em","%"] where ends_with(raw,u)) u][0]
        let n = if (not contains("0123456789+-.",slice(raw,0,1)) or raw=="") null else
            float(if (unit==null) raw else slice(raw,0,len(raw)-len(unit))) ^ { null }
        let factor = {px:1.0,pt:96.0/72.0,pc:16.0,mm:96.0/25.4,cm:96.0/2.54,in:96.0,em:em,["%"]:em/100.0}[unit];
        if (not finite(n)) null else n*(if (factor==null) 1.0 else factor)
    }
}
let named = {black:"000000",white:"ffffff",red:"ff0000",green:"008000",blue:"0000ff",yellow:"ffff00",
    gray:"808080",grey:"808080",silver:"c0c0c0",maroon:"800000",purple:"800080",lime:"00ff00",
    olive:"808000",navy:"000080",teal:"008080",aqua:"00ffff",fuchsia:"ff00ff",orange:"ffa500"}
pub fn color(value) {
    let raw = if (value is string) lower(trim(value)) else ""
    let rgba = if (not starts_with(raw,"#") and not starts_with(raw,"rgb(") and not starts_with(raw,"rgba(")) null
        else colors.parse(raw,"docx-export.color") ^ { null };
    if (named[raw]!=null) named[raw]
    else if (rgba==null or rgba[3]!=1.0) null
    else join([for (channel in take(rgba,3)) tokens.hex_byte(int(round(channel*255.0)))],"")
}
// Flat selector rules adapt an application's existing typography without copying its defaults.
pub fn rule(stylesheet,selector) => join([for (block in split(stylesheet,"}"),let parts=split(block,"{")
    where len(parts)==2 and any([for (item in split(parts[0],",")) trim(item)==selector])) parts[1]],";")
pub fn declarations(css) => [for (decl in split(css or "",";"),let parts=split(decl,":")
    where len(parts)>1) {key:lower(trim(parts[0])),value:trim(join(drop(parts,1),":"))}]
let known = ["font-family","font-size","font-weight","font-style","text-decoration","color","background-color","background",
    "vertical-align","letter-spacing","text-transform","font-variant","text-align","line-height","margin","margin-top",
    "margin-right","margin-bottom","margin-left","text-indent","white-space","page-break-before","page-break-after",
    "break-before","break-after","page-break-inside","break-inside","width","height","border","border-top","border-right",
    "border-bottom","border-left","border-color","border-width","border-style","padding","padding-top","padding-right",
    "padding-bottom","padding-left","border-collapse","table-layout","max-width","box-sizing","outline","display","list-style-type"]
pub fn diagnostics(node,css) => [for (entry in declarations(css),
    let invalid = not (entry.key in known) or
        (entry.key in ["color","background-color","background"] and not (entry.value in ["transparent","inherit","initial"]) and color(entry.value)==null) or
        (entry.key in ["font-size","letter-spacing","text-indent"] and not (entry.value in ["normal","inherit"]) and length(entry.value)==null) or
        (entry.key=="display" and not (entry.value in ["none","block","inline","inline-block","table","table-row","table-cell"])) or
        (entry.key=="text-transform" and not (entry.value in ["none","uppercase","inherit"]))
    where invalid) c.issue("style","CSS " ++ entry.key ++ ": " ++ entry.value ++ " is outside the DOCX export profile")]
fn unquote(value) => if (len(value)>=2 and ((starts_with(value,"\"") and ends_with(value,"\"")) or
    (starts_with(value,"'") and ends_with(value,"'")))) slice(value,1,len(value)-1) else value
fn choose(value,previous) => if (value==null or value=="inherit") previous else value
pub fn inherit(n,previous,defaults="") {
    let css = defaults ++ ";" ++ (n.style or "")
    let size = length(css_get(css,"font-size"),previous.size or 16.0)
    let weight = css_get(css,"font-weight")
    let emphasis = css_get(css,"font-style")
    let deco = css_get(css,"text-decoration")
    let vertical = css_get(css,"vertical-align")
    let tag = c.tag(n)
    let family = css_get(css,"font-family");
    {*:previous,
        size:if (size!=null and size>0) size else previous.size,
        font:if (family==null) previous.font else unquote(trim(split(family,",")[0])),
        bold:if (weight==null) (if (tag in ["strong","b","th"]) true else previous.bold)
            else weight in ["bold","bolder","600","700","800","900"],
        italic:if (emphasis==null) (if (tag in ["em","i"]) true else previous.italic) else emphasis in ["italic","oblique"],
        underline:if (deco==null) (if (tag=="u") true else previous.underline) else contains(deco,"underline"),
        strike:if (deco==null) (if (tag in ["s","del","strike"]) true else previous.strike) else contains(deco,"line-through"),
        vertical:if (tag=="sup" or vertical=="super") "superscript" else if (tag=="sub" or vertical=="sub") "subscript"
            else if (vertical=="baseline") "baseline" else previous.vertical,
        color:choose(css_get(css,"color"),previous.color),
        background:choose(css_get(css,"background-color") or css_get(css,"background"),previous.background),
        tracking:if (css_get(css,"letter-spacing")==null) previous.tracking else length(css_get(css,"letter-spacing"),previous.size or 16.0),
        preserve:if (css_get(css,"white-space")==null) (tag in ["pre","code"] or previous.preserve==true)
            else css_get(css,"white-space") in ["pre","pre-wrap","break-spaces"],
        hidden:previous.hidden==true or css_get(css,"display")=="none",
        alignment:choose(css_get(css,"text-align"),previous.alignment),leading:choose(css_get(css,"line-height"),previous.leading),
        quote_indent:if (tag=="blockquote") (previous.quote_indent or 0)+48.0 else previous.quote_indent,
        caps:choose(css_get(css,"text-transform"),previous.caps),small_caps:choose(css_get(css,"font-variant"),previous.small_caps)}
}
pub fn run(props) { let raw = <'w:rPr'
    if (props.font!=null) <'w:rFonts' 'w:ascii':props.font,'w:hAnsi':props.font,'w:eastAsia':props.font,'w:cs':props.font>;
    if (props.bold!=null) <'w:b' 'w:val':if (props.bold) "1" else "0">;
    if (props.italic!=null) <'w:i' 'w:val':if (props.italic) "1" else "0">;
    if (props.underline!=null) <'w:u' 'w:val':if (props.underline) "single" else "none">;
    if (props.strike!=null) <'w:strike' 'w:val':if (props.strike) "1" else "0">;
    if (props.vertical!=null) <'w:vertAlign' 'w:val':props.vertical>;
    if (props.size!=null) [<'w:sz' 'w:val':int(round(props.size*1.5))>,<'w:szCs' 'w:val':int(round(props.size*1.5))>];
    if (color(props.color)!=null) <'w:color' 'w:val':color(props.color)>;
    if (color(props.background)!=null) <'w:shd' 'w:val':"clear",'w:fill':color(props.background)>;
    if (props.tracking!=null) <'w:spacing' 'w:val':int(round(props.tracking*15.0))>;
    if (props.caps!=null) <'w:caps' 'w:val':if (props.caps=="uppercase") "1" else "0">;
    if (props.small_caps!=null) <'w:smallCaps' 'w:val':if (props.small_caps=="small-caps") "1" else "0"> >;
    <'w:rPr' *c.ordered(raw,["w:rFonts","w:b","w:i","w:caps","w:smallCaps","w:strike","w:color","w:spacing","w:sz","w:szCs","w:u","w:shd","w:vertAlign"])>
}
pub fn sides(css,prefix,em=16.0) {
    let values = [for (v in split(trim(css_get(css,prefix) or "")," ") where v!="") length(v,em)]
    let keys = ["top","right","bottom","left"];
    map([for (i,key in keys,let direct=length(css_get(css,prefix ++ "-" ++ key),em),
        let fallback=values[if (len(values)==1) 0 else if (len(values)==2) i%2 else if (len(values)==3 and i==3) 1 else i])
        (key,if (direct==null) fallback else direct)])
}
pub fn edge(attrs,side) => {top:(a)=><'w:top' *:a>,right:(a)=><'w:right' *:a>,
    bottom:(a)=><'w:bottom' *:a>,left:(a)=><'w:left' *:a>}[side](attrs)
pub fn border(css,side) {
    let raw = css_get(css,"border-" ++ side) or css_get(css,"border")
    let tokens = split(trim(raw or "")," ")
    let kind = [for (token in tokens where token in ["none","solid","dashed","dotted","double"]) token][0]
    let width = [for (token in tokens,let n=length(token) where n!=null) n][0]
    let color = [for (token in tokens,let value=color(token) where value!=null) value][0];
    if (raw==null or kind==null) null else edge({'w:val':if (kind=="none") "nil" else if (kind=="solid") "single" else kind,
        'w:sz':int(round(max(0.0,width or 1.0)*6.0)),'w:color':color or "000000"},side)
}
pub fn paragraph(node,props,numbering=null,defaults="") {
    let css = defaults ++ ";" ++ (node.style or "")
    let margins = sides(css,"margin",props.size or 16.0)
    let padding = sides(css,"padding",props.size or 16.0)
    let first = length(css_get(css,"text-indent"),props.size or 16.0)
    let line = css_get(css,"line-height") or props.leading
    let ratio = if (line==null or not contains("0123456789.",slice(line,len(line)-1))) null else float(line) ^ { null }
    let leading = if (ratio!=null) ratio*240.0 else length(line,props.size or 16.0)*15.0
    let align = css_get(css,"text-align") or node.align or props.alignment
    let heading = if (c.tag(node) in ["h1","h2","h3","h4","h5","h6"]) int(slice(c.tag(node),1)) else null;
    let raw = <'w:pPr'
        if (heading!=null) <'w:pStyle' 'w:val':"Heading" ++ string(heading)>;
        if (numbering!=null) <'w:numPr' <'w:ilvl' 'w:val':numbering.level><'w:numId' 'w:val':numbering.id>>;
        if (align in ["left","right","center","justify","start","end"]) <'w:jc' 'w:val':if (align=="justify") "both" else align>;
        <'w:spacing' *:map([for (key in ["top","bottom"] where margins[key]!=null)
            ("w:" ++ (if (key=="top") "before" else "after"),int(round(max(0.0,margins[key])*15.0)))]),
            *:if (leading!=null and leading>0) {'w:line':int(round(leading)),'w:lineRule':if (ratio==null) "exact" else "auto"} else {}>;
        <'w:ind' *:map([for (key in ["left","right"],let margin=margins[key] or (if (key=="left") props.quote_indent else null),
            let value=if (margin==null and padding[key]==null) null else (margin or 0)+(padding[key] or 0)
            where value!=null) ("w:" ++ key,int(round(value*15.0)))]),
            *:if (first==null) {} else if (first>=0) {'w:firstLine':int(round(first*15.0))} else {'w:hanging':int(round(0.0-first*15.0))}>;
        <'w:pBdr' *[for (side in ["top","left","bottom","right"]) border(css,side)]>;
        if (color(props.background)!=null) <'w:shd' 'w:val':"clear",'w:fill':color(props.background)>;
        if (css_get(css,"page-break-before")=="always" or css_get(css,"break-before")=="page" or node['data-word-page-break']=="true")
            <'w:pageBreakBefore'>;
        if (css_get(css,"page-break-inside")=="avoid" or css_get(css,"break-inside")=="avoid") <'w:keepLines'>;
        if (heading!=null) <'w:keepNext'> >;
    <'w:pPr' *c.ordered(raw,["w:pStyle","w:keepNext","w:keepLines","w:pageBreakBefore","w:numPr","w:pBdr","w:shd","w:spacing","w:ind","w:jc"])>
}
