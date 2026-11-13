// RTF subset parser. Group-local formatting retains the former native behavior (D9.3.4).
import lex: .rtf.lexer
import grammar_lex: .rtf.grammar_lexer

// Both readers feed one semantic lowering; only the grammar reader materializes records.
fn raw_source(source) => if (source is string) source else source.source
fn next_token(source,at) any^ => if (source is string) lex.next(source,at)^ else grammar_lex.next(source,at)^

fn initial_style() => {font:0,size:24,color:0,align:"left",li:0,ri:0,sl:0,uc:1,
    bold:false,italic:false,underline:false,strike:false,caps:false,baseline:0}
fn initial() => {style:initial_style(),default_font:0,fonts:[],colors:[],blocks:[],spans:[],
    paragraph_style:null,text:""}
fn fixed(n) any^ {
    let hundredths = int(round(n * 100.0));
    let whole = int(hundredths / 100);
    let frac = hundredths - whole * 100;
    string(whole) ++ "." ++ (if (frac < 10) "0" else "") ++ string(frac)
}
fn font_name(fonts, font_id) any^ {
    let matches = [for (f in fonts where f.font_id == font_id) f.name];
    if (len(matches) == 0) null else matches[0]
}
fn safe_font(name) any^ => join([for (c in (split(name, "")^) where
    ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or
     (c >= "0" and c <= "9") or c == " " or c == "-")) c], "")
fn span_style(s) any^ {
    let st = s.style;
    let name = font_name(s.fonts,st.font);
    let color = if (st.color >= 0 and st.color < len(s.colors)) s.colors[st.color] else null;
    (if (name == null) "" else "font-family:'" ++ safe_font(name) ++ "';") ++
    (if (st.size <= 0) "" else "font-size:" ++ fixed(st.size * (2.0 / 3.0)) ++ "px;") ++
    (if (st.bold) "font-weight:700;" else "") ++ (if (st.italic) "font-style:italic;" else "") ++
    (if (not st.underline and not st.strike) "" else "text-decoration:" ++
        (if (st.underline) "underline" else "") ++ (if (st.underline and st.strike) " " else "") ++
        (if (st.strike) "line-through" else "") ++ ";") ++
    (if (st.caps) "font-variant:small-caps;" else "") ++
    (if (st.baseline == 0) "" else if (st.baseline > 0) "vertical-align:super;font-size:smaller;"
        else "vertical-align:sub;font-size:smaller;") ++
    (if (color == null or not color.specified) "" else "color:rgb(" ++
        join([for (v in [color.red,color.green,color.blue]) string(max(0,min(255,v)))],",") ++ ");")
}
fn paragraph_style(st) any^ => "text-align:" ++ st.align ++ ";" ++
    (if (st.li > 0) "margin-left:" ++ fixed(st.li / 15.0) ++ "px;" else "") ++
    (if (st.ri > 0) "margin-right:" ++ fixed(st.ri / 15.0) ++ "px;" else "") ++
    (if (st.sl > 0) "line-height:" ++ fixed(st.sl / 15.0) ++ "px;" else "")
fn flush(s) any^ {
    if (s.text == "") s else {
        let css = span_style(s)^;
        let pstyle = if (s.paragraph_style == null) paragraph_style(s.style) else s.paragraph_style;
        {*:s,text:"",paragraph_style:pstyle,spans:[*s.spans,<span style:css,s.text>]}
    }
}
fn finish(s) any^ {
    let result = flush(s)^;
    if (result.paragraph_style == null) result else {*:result,paragraph_style:null,spans:[],
        blocks:[*result.blocks,<p style:result.paragraph_style,*result.spans>]}
}
fn add(s, text) => {*:s,text:s.text ++ text}
fn skip_group(source, at, depth = 0) any^ {
    let t = next_token(source,at)^;
    if (t == null) at else if (t.kind == "open") skip_group(source,t.end,depth+1)
    else if (t.kind == "close") (if (depth == 0) t.end else skip_group(source,t.end,depth-1))
    else skip_group(source,t.end,depth)
}
fn put_font(s, font_id, name) any^ {
    let clean = trim(name)^;
    if (font_id == null or clean == "") s else {*:s,fonts:[*s.fonts,{font_id:font_id,name:clean}]}
}
// Font entries are delimited by semicolons or groups; control words are metadata.
fn font_table(source, at, s, nesting = 0, font_id = null, name = "") any^ {
    let t = next_token(source,at)^;
    if (t == null) {at:at,state:put_font(s,font_id,name)}
    else if (t.kind == "open") font_table(source,t.end,s,nesting+1,font_id,name)
    else if (t.kind == "close") {
        let next = put_font(s,font_id,name)^;
        if (nesting == 0) {at:t.end,state:next} else font_table(source,t.end,next,nesting-1,null,"")
    } else if (t.kind == "control" and t.word == "f" and t.parameter != null)
        font_table(source,t.end,put_font(s,font_id,name),nesting,t.parameter,"")
    else if (t.kind == "text" and font_id != null) {
        let parts = split(replace(replace(t.value,"\r",""),"\n",""),";");
        if (len(parts) > 1) font_table(source,t.end,put_font(s,font_id,name ++ parts[0]),nesting,null,"")
        else font_table(source,t.end,s,nesting,font_id,name ++ t.value)
    } else font_table(source,t.end,s,nesting,font_id,name)
}
fn empty_color() => {red:0,green:0,blue:0,specified:false}
fn color_table(source, at, s, color = empty_color()) any^ {
    let t = next_token(source,at)^;
    if (t == null) {at:at,state:s}
    else if (t.kind == "close") {at:t.end,state:s}
    else if (t.kind == "control" and t.parameter != null and contains(["red","green","blue"],t.word)) {
        let c = if (t.word == "red") {*:color,red:t.parameter,specified:true}
            else if (t.word == "green") {*:color,green:t.parameter,specified:true}
            else {*:color,blue:t.parameter,specified:true};
        color_table(source,t.end,s,c)
    } else if (t.kind == "text" and contains(t.value,";")) {
        let parts = split(t.value,";")^;
        let extras = slice(parts,1,len(parts)-1)^;
        color_table(source,t.end,{*:s,colors:[*s.colors,color,*[for (part in extras) empty_color()]]},empty_color())
    } else color_table(source,t.end,s,color)
}
fn fallback(source, at, count) any^ {
    if (count <= 0 or at >= len(raw_source(source))) { at } else {
        let t = next_token(source,at)^;
        if (t == null or t.kind == "open" or t.kind == "close" or t.kind == "control") at
        else if (t.kind == "text") fallback(source,at+1,count-1)
        else if (t.kind == "hex" or (t.kind == "symbol" and contains(["\\","{","}","~","-","_"],t.value)))
            fallback(source,t.end,count-1)
        else at
    }
}
fn control(s, t) any^ {
    let w = t.word; let v = if (t.parameter == null) 1 else t.parameter; let st = s.style;
    if (w == "par" or w == "row") finish(s)
    else if (w == "page") {let p = finish(s); {*:p,blocks:[*p.blocks,<hr>]}}
    else if (w == "line") add(s,"\n")
    else if (w == "tab" or w == "cell") add(s,"\t")
    else if (w == "bullet") add(s,chr(8226))
    else if (w == "u" and t.parameter != null) add(s,chr((v % 65536 + 65536) % 65536))
    else if (w == "deff" and t.parameter != null) {*:s,default_font:v,style:{*:st,font:v}}
    else if (w == "plain") {*:s,style:{*:st,font:s.default_font,size:24,color:0,bold:false,
        italic:false,underline:false,strike:false,caps:false,baseline:0}}
    else if (w == "pard") {*:s,style:{*:st,align:"left",li:0,ri:0,sl:0}}
    else if (w == "ql" or w == "qc" or w == "qr" or w == "qj")
        {*:s,style:{*:st,align:if (w == "ql") "left" else if (w == "qc") "center" else if (w == "qr") "right" else "justify"}}
    else if (w == "b") {*:s,style:{*:st,bold:v != 0}}
    else if (w == "i") {*:s,style:{*:st,italic:v != 0}}
    else if (w == "ul" or w == "ulnone") {*:s,style:{*:st,underline:w == "ul" and v != 0}}
    else if (w == "strike") {*:s,style:{*:st,strike:v != 0}}
    else if (w == "caps") {*:s,style:{*:st,caps:v != 0}}
    else if (w == "super" or w == "sub" or w == "nosupersub")
        {*:s,style:{*:st,baseline:if (w == "super") 1 else if (w == "sub") -1 else 0}}
    else if (t.parameter == null) s
    else if (w == "f") {*:s,style:{*:st,font:v}}
    else if (w == "fs") {*:s,style:{*:st,size:v}}
    else if (w == "cf") {*:s,style:{*:st,color:v}}
    else if (w == "li") {*:s,style:{*:st,li:v}}
    else if (w == "ri") {*:s,style:{*:st,ri:v}}
    else if (w == "sl") {*:s,style:{*:st,sl:v}}
    else if (w == "uc") {*:s,style:{*:st,uc:max(0,v)}}
    else s
}
fn skip_space(source, at) any^ {
    if (at < len(source) and contains([" ","\t","\r","\n"],slice(source,at,at+1)))
        skip_space(source,at+1) else at
}
fn group(source, at, s, depth) any^ {
    if (depth >= 512) raise error({message:"rtf: maximum nesting depth exceeded"})
    else {
        let begin = skip_space(raw_source(source),at+1);
        let first = next_token(source,begin)^;
        if (first != null and first.kind == "symbol" and first.value == "*")
            {at:skip_group(source,first.end),state:s}
        else if (first != null and first.kind == "control" and first.word == "fonttbl")
            font_table(source,first.end,s)
        else if (first != null and first.kind == "control" and first.word == "colortbl")
            color_table(source,first.end,s)
        else if (first != null and first.kind == "control" and contains(
            ["stylesheet","info","header","footer","fldinst","pict","object","footnote"],first.word))
            {at:skip_group(source,first.end),state:s}
        else walk(source,begin,s,depth)
    }
}
fn walk(source, at, s, depth) any^ {
    let t = next_token(source,at)^;
    if (t == null) {at:at,state:flush(s)}
    else if (t.kind == "close") {at:t.end,state:flush(s)}
    else if (t.kind == "open") {
        let before = flush(s)^;
        let child = group(source,at,before,depth+1)^;
        // Group formatting is local, while output and document tables persist.
        walk(source,child.at,{*:child.state,style:before.style},depth)
    } else if (t.kind == "text")
        walk(source,t.end,add(s,replace(replace(t.value,"\r",""),"\n","")),depth)
    else if (t.kind == "hex") walk(source,t.end,add(s,chr(t.value)),depth)
    else if (t.kind == "symbol") {
        let value = if (contains(["\\","{","}"],t.value)) t.value
            else if (t.value == "~") chr(160) else if (t.value == "-") chr(173) else "";
        walk(source,t.end,add(s,value),depth)
    } else {
        let before = flush(s)^;
        let after = control(before,t)^;
        let end = if (t.word == "u" and t.parameter != null) fallback(source,t.end,before.style.uc)
            else if (t.word == "footnote" and slice(raw_source(source),t.end,t.end+1) == "{") skip_group(source,t.end+1)
            else t.end;
        walk(source,end,after,depth)
    }
}
// Accept RTF source text; callers that own file I/O can read text before parsing.
fn parse_with(source, use_grammar) element^ {
    if (not (source is string)) raise error({message:"rtf: expected source text"})
    else {
        let begin = skip_space(source,0);
        if (slice(source,begin,begin+5) != "{\\rtf")
            raise error({message:"rtf: document must start with '{\\rtf'"})
        else {
            let stream = if (use_grammar) grammar_lex.prepare(source)^ else source;
            let result = group(stream,begin,initial(),0)^;
            let done = finish(result.state)^;
            <doc *done.blocks>
        }
    }
}
pub fn parse(source) element^ => parse_with(source,false)^
// The grammar variant is retained for differential and performance analysis.
pub fn parse_grammar(source) element^ => parse_with(source,true)^
pub fn parse_file(path) element^ => parse(input(path,'text')^)^
