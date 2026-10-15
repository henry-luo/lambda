// Presentation MathML layout; Core's policies never read TeX companion data (D7.2.8).
// Algorithms: https://www.w3.org/TR/2025/CR-mathml-core-20250624/ §§3–5.
import mml: .mathml
import operators: .mathml_operators
import alphabet: .alphabet
import font: .font
import bx: .svg_box
import geometry: .geometry
import stretch: .stretch
import util: .util

fn require(ok, message) bool^ => if (ok) true else raise error("mathml: "++message)
fn tag(n) => if (n is element) mml.local_name(n) else ""
fn children(n) => [for (child in content(n) where child is element or
    (child is string and trim(child)!="")) child]
fn boolean(value, fallback = false) => if (lower(string(value))=="true") true
    else if (lower(string(value))=="false") false else fallback
fn scale(c) => if (c.level==0) 1.0 else if (c.level==1)
    c.profile.facts.constants.script_percent_scale_down/100.0
    else c.profile.facts.constants.script_script_percent_scale_down/100.0 * (0.71 ** (c.level-2))
fn em(c) => font.UNITS*scale(c)
fn metric(c,key) => (c.profile.facts.constants[key] or 0.0)*scale(c)
fn script(c, extra = 1) => {*:c,level:c.level+extra,display:false,stretch:null,form:"postfix"}
fn normalized(text) => join([for (part in split(replace(replace(replace(text,"\n"," "),"\r"," "),"\t"," ")," ")
    where part!="") part]," ")

// CSS lengths use CSS points; percentage bases are supplied by the calling algorithm.
fn length(raw,c,relative=null) number^ {
    let text=trim(string(raw))
    let percent=ends_with(text,"%")
    let parsed=util.dimension_from_string(text)
    let explicit=ends_with(text,parsed.unit)
    let amount=float(if (percent) slice(text,0,len(text)-1)
        else if (explicit) slice(text,0,len(text)-len(parsed.unit)) else text)^
    let factor={em:em(c),ex:c.profile.font_metrics.x_height*scale(c),
        px:font.UNITS/c.pixels,pt:font.UNITS/c.pixels*96.0/72.0,
        pc:font.UNITS/c.pixels*16.0,in:font.UNITS/c.pixels*96.0,
        cm:font.UNITS/c.pixels*96.0/2.54,mm:font.UNITS/c.pixels*96.0/25.4}[parsed.unit]
    let checked=require(amount-amount==0 and (percent or factor!=null),"unsupported length "++text)^;
    if (percent) amount*(relative or em(c))/100.0 else amount*factor
}

fn declarations(text) => reduce([{},*[for (part in split(string(text or ""),";") where trim(part)!="")
    (let colon=index_of(part,":"),
    {[lower(trim(slice(part,0,colon or 0)))]:trim(slice(part,(colon or 0)+1,len(part)))})]],(a,b)=>{*:a,*:b})

fn context_for(n,c) map^ {
    let ns=mml.bindings(n,c.ns)
    let parts=split(string(name(n)),":")
    let prefix=if (len(parts)>1) parts[0] else ""
    let valid_namespace=require(ns[prefix]==mml.namespace,"foreign namespace inside math")^
    let direction=require(n.dir==null or lower(string(n.dir))=="ltr","RTL math is not supported")^
    let background=require(n.mathbackground==null,"mathbackground is not supported")^
    let css=declarations(n.style)
    let allowed=["color","font-weight","font-style","font-family","font-size",
        "math-style","math-depth","math-shift","text-align","margin-left","visibility"]
    let checked=require(all([for (key,value in css) contains(allowed,string(key))]),"unsupported inline CSS on "++tag(n))^
    let depth=string(css["math-depth"] or n.scriptlevel or c.level)
    let level=max(0,int(depth)^+(if (starts_with(depth,"+") or starts_with(depth,"-")) c.level else 0))
    let valid_level=require(level<=32,"scriptlevel exceeds the supported limit")^
    let variant=if (css["font-weight"]=="bold" or css["font-weight"]=="700") "bold"
        else if (css["font-style"]=="italic") "italic"
        else if (css["font-family"]=="monospace") "mono"
        else if (css["font-family"]=="sans-serif") "sans" else c.variant
    let valid_font=require(css["font-size"]==null and n.mathsize==null,
        "nested font-size/mathsize is not supported")^;
    {*:c,ns:ns,level:level,variant:variant,
        display:if (css["math-style"]!=null) css["math-style"]=="normal" else boolean(n.displaystyle,c.display),
        cramped:if (css["math-shift"]!=null) css["math-shift"]=="compact" else c.cramped,
        css:css}
}

fn space_like(n) => if (contains(["mspace","mtext"],tag(n))) true
    else if (contains(["math","mrow","mstyle","mphantom","mpadded"],tag(n))) all([for (child in children(n)) space_like(child)])
    else false

// §3.2.4.1: embellished operators retain their core mo through wrappers/scripts.
fn core(n) {
    let items=children(n)
    let significant=[for (child in items where not space_like(child)) child];
    if (tag(n)=="mo") n
    else if (contains(["msub","msup","msubsup","munder","mover","munderover","mfrac"],tag(n)) and len(items)>0) core(items[0])
    else if (contains(["math","mrow","mstyle","mphantom"],tag(n)) and len(significant)==1) core(significant[0])
    else null
}

fn properties(n,c) {
    let spelling=normalized(util.text_of(n))
    let explicit=contains(["prefix","infix","postfix"],lower(string(n.form)))
    let form=if (explicit) lower(string(n.form)) else c.form
    let entry=operators.lookup(spelling,form,explicit)
    let left=(entry.lspace or if (entry.lspace==0) 0 else 5)*em(c)/18.0
    let right=(entry.rspace or if (entry.rspace==0) 0 else 5)*em(c)/18.0;
    {text:spelling,form:form,horizontal:entry.horizontal==true,
        largeop:boolean(n.largeop,entry.largeop==true),
        movablelimits:boolean(n.movablelimits,entry.movablelimits==true),
        stretchy:boolean(n.stretchy,entry.stretchy==true),
        symmetric:boolean(n.symmetric,entry.symmetric==true),
        lspace:if (n.lspace!=null) length(n.lspace,c,left)^ else if (c.level>0) 0.0 else left,
        rspace:if (n.rspace!=null) length(n.rspace,c,right)^ else if (c.level>0) 0.0 else right,
        minsize:n.minsize,maxsize:n.maxsize}
}

fn character(ch,c,variant) map^ {
    let decoded=if (c.profile.facts.has_math) {character:ch,style:null} else alphabet.decode(ch)
    let g=font.character(c.profile,decoded.character,decoded.style or variant,variant=="normal" and decoded.style==null)^;
    bx.glyph(g,scale(c))
}

fn token(n,c) map^ {
    let checked=require(all([for (child in content(n)) not (child is element)]),"HTML inside token elements is not supported")^
    let text=normalized(util.text_of(n))
    let variant=if (tag(n)=="mi" and len(text)==1 and lower(string(n.mathvariant))!="normal")
        (if (c.variant=="bold") "bolditalic" else if (c.variant=="sans") "sansitalic"
         else if (c.variant=="normal") "auto" else c.variant) else c.variant
    let boxes=[for (ch in split(text,"")) character(ch,c,variant)^]
    let result=bx.row(boxes);
    if (len(boxes)==1) {*:boxes[0],slanted:variant=="auto" or variant=="italic"}
    else result
}

fn operator(n,c) map^ {
    let props=properties(n,c)
    let natural=token(n,{*:c,variant:"normal"})^
    let target=if (props.stretchy and props.horizontal) c.stretch.width
        else if (props.stretchy and not props.horizontal and c.stretch.height!=null)
            (if (props.symmetric) 2.0*max(c.stretch.height-metric(c,"axis_height"),c.stretch.depth+metric(c,"axis_height"))
             else c.stretch.height+c.stretch.depth)
        else if (props.largeop and c.display) metric(c,"display_operator_min_height") else null
    let minimum=if (props.minsize!=null) max(0.0,length(props.minsize,c,natural.height+natural.depth)^) else em(c)
    let maximum=if (props.maxsize!=null and lower(string(props.maxsize))!="infinity")
        max(minimum,length(props.maxsize,c,natural.height+natural.depth)^) else inf
    let amount=if (target!=null and props.stretchy) min(maximum,max(minimum,target)) else target
    let sized=if (amount!=null and len(text_or_empty(props.text))==1 and natural.glyph!=null)
        stretch.glyph(natural.glyph,amount,not props.horizontal,scale(c),"mord")^ else natural
    let centered=if ((props.largeop and c.display) or (props.stretchy and not props.horizontal and target!=null))
        bx.shifted(sized,0.0,(sized.height-sized.depth)/2.0-metric(c,"axis_height"),sized.width) else sized;
    {*:centered,op:props,slanted:false,italic:sized.italic,glyph:sized.glyph,glyph_scale:sized.glyph_scale}
}

fn text_or_empty(text) => text or ""

fn row(n,c) map^ {
    let items=if (n is array) n else children(n)
    let indices=[for (i,child in items where not space_like(child)) i]
    let embellished=if (n is array) null else core(n)
    let natural=[for (i,child in items) node(child,{*:c,stretch:null,
        form:if (embellished!=null) c.form else if (len(indices)>1 and i==indices[0]) "prefix"
            else if (len(indices)>1 and i==indices[len(indices)-1]) "postfix" else "infix"})^]
    let fixed=[for (box in natural where box.op==null or not box.op.stretchy or box.op.horizontal) box]
    let basis=if (len(fixed)>0) fixed else natural
    let target={height:max([0.0,*[for (box in basis) box.height]]),depth:max([0.0,*[for (box in basis) box.depth]])}
    let boxes=[for (i,box in natural) if (box.op.stretchy and not box.op.horizontal)
        node(items[i],{*:c,form:box.op.form,stretch:if (embellished!=null and c.stretch!=null) c.stretch else target})^ else box]
    let add_space=embellished==null or tag(n)=="math"
    let gaps=[for (i,box in boxes) {left:(if (add_space) box.op.lspace or 0.0 else 0.0)+
        (if (i>0 and not box.slanted and boxes[i-1].slanted) boxes[i-1].italic else 0.0),
        right:if (add_space) box.op.rspace or 0.0 else 0.0}]
    let entries=[for (i,box in boxes) {box:box,y:0.0,x:gaps[i].left+
        sum([for (j in 0 to (i-1)) boxes[j].width+gaps[j].left+gaps[j].right])}]
    let italic=if (len(boxes)>0 and boxes[len(boxes)-1].slanted) boxes[len(boxes)-1].italic else 0.0
    let result=bx.compose(entries,sum([for (i,box in boxes) box.width+gaps[i].left+gaps[i].right])+italic);
    {*:result,op:if (embellished!=null and len(boxes)>0) [for (box in boxes where box.op!=null) box.op][0] else null,
        italic:italic,slanted:len(boxes)==1 and boxes[0].slanted}
}

fn fraction(items,n,c) map^ {
    let child=if (c.display) {*:c,display:false,stretch:null} else script(c)
    let numer=node(items[0],child)^
    let denom=node(items[1],{*:child,cramped:true})^
    let thickness=if (n.linethickness!=null) max(0.0,length(n.linethickness,c,metric(c,"fraction_rule_thickness"))^)
        else metric(c,"fraction_rule_thickness")
    let axis=metric(c,"axis_height")
    let up0=metric(c,if (thickness>0.0) (if (c.display) "fraction_numerator_display_style_shift_up" else "fraction_numerator_shift_up")
        else if (c.display) "stack_top_display_style_shift_up" else "stack_top_shift_up")
    let down0=metric(c,if (thickness>0.0) (if (c.display) "fraction_denominator_display_style_shift_down" else "fraction_denominator_shift_down")
        else if (c.display) "stack_bottom_display_style_shift_down" else "stack_bottom_shift_down")
    let missing=if (thickness>0.0) 0.0 else max(0.0,metric(c,if (c.display) "stack_display_style_gap_min" else "stack_gap_min") -
        (up0+down0-numer.depth-denom.height))
    let up=if (thickness>0.0) max(up0,axis+thickness/2.0+numer.depth+
        metric(c,if (c.display) "fraction_num_display_style_gap_min" else "fraction_numerator_gap_min")) else up0+missing/2.0
    let down=if (thickness>0.0) max(down0,thickness/2.0+denom.height-axis+
        metric(c,if (c.display) "fraction_denom_display_style_gap_min" else "fraction_denominator_gap_min")) else down0+missing/2.0
    let width=max(numer.width,denom.width)
    let body=geometry.fraction(numer,denom,width,(width-numer.width)/2.0,up,down,axis,thickness)
    // §3.3.2: the UA adds one CSS pixel on each inline side of a fraction.
    let pad=font.UNITS/c.pixels;
    {*:bx.shifted(body,pad,0.0,width+2.0*pad),op:numer.op}
}

fn side_scripts(base,sub,sup,c) {
    let up0=if (sup==null) 0.0 else max([metric(c,if (c.cramped) "superscript_shift_up_cramped" else "superscript_shift_up"),
        metric(c,"superscript_bottom_min")+sup.depth,base.height-metric(c,"superscript_baseline_drop_max")])
    let down0=if (sub==null) 0.0 else max([metric(c,"subscript_shift_down"),sub.height-metric(c,"subscript_top_max"),
        base.depth+metric(c,"subscript_baseline_drop_min")])
    let missing=if (sub!=null and sup!=null) max(0.0,metric(c,"sub_superscript_gap_min")-(up0-sup.depth+down0-sub.height)) else 0.0
    let lift=min(missing,max(0.0,metric(c,"superscript_bottom_max_with_subscript")-(up0-(sup.depth or 0.0))))
    let x_sub=base.width-(if (base.op.largeop) base.italic else 0.0)
    let x_sup=base.width+(if (base.op.largeop) 0.0 else base.italic)
    let width=max([base.width,*if (sub!=null) [x_sub+sub.width] else [],*if (sup!=null) [x_sup+sup.width] else []])+metric(c,"space_after_script")
    let result=geometry.scripts(base,sub,sup,x_sub,x_sup,up0+lift,down0+missing-lift,width);
    {*:result,op:base.op}
}

fn scripted(items,n,c,under_over) map^ {
    let kind=tag(n)
    let base=node(items[0],c)^
    let has_sub=contains(["msub","msubsup","munder","munderover"],kind)
    let has_sup=contains(["msup","msubsup","mover","munderover"],kind)
    let lower_node=if (has_sub) items[1] else null
    let upper_node=if (has_sup) items[if (has_sub) 2 else 1] else null
    let under_accent=under_over and boolean(n.accentunder)
    let over_accent=under_over and boolean(n.accent)
    let sub=if (lower_node!=null) node(lower_node,if (under_accent) {*:c,stretch:null,form:"postfix"} else {*:script(c),cramped:true})^ else null
    let sup=if (upper_node!=null) node(upper_node,if (over_accent) {*:c,stretch:null,form:"postfix"} else script(c))^ else null;
    if (not under_over or (not c.display and base.op.movablelimits)) side_scripts(base,sub,sup,c)
    else {
        let target=max([base.width,sub.width or 0.0,sup.width or 0.0])
        let above=if (sup.op.stretchy and sup.op.horizontal) node(upper_node,{*:if (over_accent) c else script(c),stretch:{width:target},form:"postfix"})^ else sup
        let below=if (sub.op.stretchy and sub.op.horizontal) node(lower_node,{*:if (under_accent) c else script(c),stretch:{width:target},form:"postfix"})^ else sub
        let italic=if (base.op.largeop) base.italic else 0.0
        let left=min([0.0-base.width/2.0,*if (below!=null) [0.0-below.width/2.0-italic/2.0] else [],
            *if (above!=null) [0.0-above.accent+italic/2.0] else []])
        let right=max([base.width/2.0,*if (below!=null) [below.width/2.0-italic/2.0] else [],
            *if (above!=null) [above.width-above.accent+italic/2.0] else []])
        let up=if (above==null) 0.0 else base.height+(if (base.op.largeop)
            max(metric(c,"upper_limit_baseline_rise_min"),metric(c,"upper_limit_gap_min")+above.depth)
            else if (base.op.stretchy and base.op.horizontal) max(metric(c,"stretch_stack_top_shift_up"),metric(c,"stretch_stack_gap_below_min")+above.depth)
            else above.depth+(if (over_accent) max(0.0,metric(c,"accent_base_height")-base.height) else metric(c,"overbar_vertical_gap")))
        let down=if (below==null) 0.0 else base.depth+(if (base.op.largeop)
            max(metric(c,"lower_limit_baseline_drop_min"),metric(c,"lower_limit_gap_min")+below.height)
            else if (base.op.stretchy and base.op.horizontal) max(metric(c,"stretch_stack_bottom_shift_down"),metric(c,"stretch_stack_gap_above_min")+below.height)
            else below.height+(if (under_accent) 0.0 else metric(c,"underbar_vertical_gap")))
        let result=bx.compose([{box:base,x:0.0-left-base.width/2.0,y:0.0},
            *if (above!=null) [{box:above,x:0.0-left-above.accent+italic/2.0,y:0.0-up}] else [],
            *if (below!=null) [{box:below,x:0.0-left-below.width/2.0-italic/2.0,y:down}] else []],right-left);
        {*:result,op:base.op,
            height:result.height+(if (above!=null and not base.op.largeop and not (base.op.stretchy and base.op.horizontal)) metric(c,"overbar_extra_ascender") else 0.0),
            depth:result.depth+(if (below!=null and not base.op.largeop and not (base.op.stretchy and base.op.horizontal)) metric(c,"underbar_extra_descender") else 0.0)}
    }
}

fn radical(items,n,c) map^ {
    let body=if (tag(n)=="mroot") node(items[0],{*:c,cramped:true,stretch:null})^
        else row(n,{*:c,cramped:true,stretch:null})^
    let degree=if (tag(n)=="mroot") node(items[1],script(c,2))^ else null
    let thickness=metric(c,"radical_rule_thickness")
    let gap=metric(c,if (c.display) "radical_display_style_vertical_gap" else "radical_vertical_gap")
    let radical_sign=stretch.glyph(font.glyph(c.profile,ord("√"))^,body.height+body.depth+gap+thickness,true,scale(c),"mord")^
    let top=0.0-body.height-gap-thickness
    let before=if (degree!=null) max(0.0,metric(c,"radical_kern_before_degree")) else 0.0
    let offset=if (degree!=null) before+degree.width+max(0.0-degree.width,metric(c,"radical_kern_after_degree")) else 0.0
    let extra=metric(c,"radical_extra_ascender")
    let height=max(body.height,body.height+gap+thickness+extra)
    let depth=max(body.depth,radical_sign.height+radical_sign.depth+extra-height)
    let degree_y=depth-(height+depth)*c.profile.facts.constants.radical_degree_bottom_raise_percent/100.0-(degree.depth or 0.0)
    let result=geometry.radical(body,radical_sign,degree,top,thickness,offset,before,degree_y,extra);
    {*:result,height:max(height,result.height),depth:max(depth,result.depth)}
}

fn table(n,c) map^ {
    let attributes=require(n.rowspacing==null and n.columnspacing==null and n.columnalign==null and
        n.rowalign==null and n.frame==null and n.rowlines==null and n.columnlines==null,
        "legacy matrix layout attributes are not supported")^
    let rows=children(n)
    let valid=require(all([for (r in rows) tag(r)=="mtr" and all([for (cell in children(r)) tag(cell)=="mtd"])]),"invalid matrix children")^
    let cells=[for (r in rows) (let rc=context_for(r,{*:c,display:false,stretch:null})^,
        [for (cell in children(r)) node(cell,rc)^])]
    let column_count=max([0,*[for (r in cells) len(r)]])
    // Core §3.5 UA stylesheet: cell padding is 0.5ex vertically and 0.4em horizontally.
    let px=0.4*em(c)
    let py=0.5*c.profile.font_metrics.x_height*scale(c)
    let widths=[for (j in 0 to (column_count-1)) max([0.0,*[for (r in cells) r[j].width or 0.0]])+2.0*px]
    let heights=[for (r in cells) max([0.0,*[for (cell in r) cell.height]])+py]
    let depths=[for (r in cells) max([0.0,*[for (cell in r) cell.depth]])+py]
    let total=sum(heights)+sum(depths)
    let ascent=total/2.0+metric(c,"axis_height")
    let entries=[for (i,r in cells,j,cell in r) {box:cell,
        x:sum(slice(widths,0,j))+px+(if (declarations(children(rows[i])[j].style)["text-align"]=="left") 0.0 else (widths[j]-2.0*px-cell.width)/2.0),
        y:sum(slice(heights,0,i))+sum(slice(depths,0,i))+heights[i]-ascent}]
    let result=bx.compose(entries,sum(widths));
    {*:result,height:max(0.0,ascent),depth:max(0.0,total-ascent)}
}

fn node(n,inherited) map^ {
    if (n is string) token(<mtext n>,inherited)^
    else {
    let c=context_for(n,inherited)^
    let kind=tag(n)
    let items=children(n)
    let expected={mfrac:2,mroot:2,msub:2,msup:2,msubsup:3,munder:2,mover:2,munderover:3}[kind]
    let result=if (expected!=null and len(items)!=expected) row(n,c)^
    else match kind {
        case "math": row(n,c)^
        case "mrow": row(n,c)^
        case "mstyle": row(n,c)^
        case "mi": token(n,c)^
        case "mn": token(n,c)^
        case "mtext": token(n,c)^
        case "ms": token(n,c)^
        case "mo": operator(n,c)^
        case "mfrac": fraction(items,n,c)^
        case "msqrt": radical(items,n,c)^
        case "mroot": radical(items,n,c)^
        case "msub": scripted(items,n,c,false)^
        case "msup": scripted(items,n,c,false)^
        case "msubsup": scripted(items,n,c,false)^
        case "munder": scripted(items,n,c,true)^
        case "mover": scripted(items,n,c,true)^
        case "munderover": scripted(items,n,c,true)^
        case "mtable": table(n,c)^
        case "mtd": row(n,c)^
        case "mphantom": (let body=row(n,c)^, {*:body,body:<g>})
        case "mspace": bx.make(<g>,if (n.width!=null) length(n.width,c)^ else 0.0,
            if (n.height!=null) max(0.0,length(n.height,c)^) else 0.0,
            if (n.depth!=null) max(0.0,length(n.depth,c)^) else 0.0)
        case "semantics": if (len(items)>0 and not contains(["annotation","annotation-xml"],tag(items[0]))) node(items[0],c)^ else bx.empty()
        case "annotation": bx.empty()
        case "annotation-xml": bx.empty()
        default: raise error("mathml: unsupported layout element "++kind)
    }
    let margin=if (c.css["margin-left"]!=null) length(c.css["margin-left"],c)^ else 0.0
    let shifted=if (margin!=0.0) bx.shifted(result,margin,0.0,result.width+margin) else result;
    if (c.css.visibility=="hidden") {*:shifted,body:<g>}
    else if (c.css.color!=null or n.mathcolor!=null) {*:shifted,body:<g fill:c.css.color or n.mathcolor,shifted.body>}
    else shifted
    }
}

fn presentation_text(n) {
    if (n is string) n
    else if (contains(["annotation","annotation-xml"],tag(n))) ""
    else if (tag(n)=="semantics") presentation_text(children(n)[0])
    else join([for (child in content(n)) presentation_text(child)],"")
}

pub fn render(ast,options=null) map^ {
    let checked=require(mml.is_mathml(ast),"expected a MathML namespace math root")^
    let pixels=options.font_size or 16.0
    let valid_size=require(pixels is number and pixels>0 and pixels-pixels==0,"font_size must be finite and positive")^
    let profile=font.prepare(ast,{*:(options or {}),notation:"mathml"})^
    let c={profile:profile,display:if (options.display!=null) options.display else lower(string(ast.display))=="block",
        level:0,cramped:false,variant:"normal",pixels:pixels,ns:{},form:"infix",stretch:null}
    let result=node(ast,c)^
    let output_el=bx.emit(result,font.UNITS,options.color,options.font_size,ast.alttext or normalized(presentation_text(ast)),
        font.stylesheet(profile,result.body));
    {element:output_el,width:result.width/font.UNITS,height:result.height/font.UNITS,
        depth:result.depth/font.UNITS,type:result.type,italic:result.italic/font.UNITS,
        skew:0.0,max_font_size:1.0,font_family:profile.family}
}

// The generic retained layout callback supplies the live source and computed size.
pub fn layout(parent,children,ctx) map^ {
    let source=ctx.element
    // Radiant already applies the root's CSS box, color and font size. Only
    // math-content style declarations need to enter the package's SVG geometry.
    let css=declarations(source.style)
    let style=join([for (key,value in css where contains(["font-weight","font-style",
        "math-style","math-depth","math-shift"],string(key))) string(key)++":"++string(value)],";")
    let tree=<math *:source,xmlns:mml.namespace,style:style,for (child in content(source)) child>
    let measured=render(tree,{font_size:ctx.font_size})^
    let pixels=ctx.font_size;
    {width:measured.width*pixels,height:(measured.height+measured.depth)*pixels,
        baseline:measured.height*pixels,
        placements:[for (child in children) {index:child.index,x:0.0,y:0.0}],
        paint_layers:[{z:0,content:measured.element}]}
}
