// Structural equation editing over LaTeX source: a row/item tree, caret slots, and
// geometry read from one render with layout-neutral \lambdamark slots (Lambda_Pkg_Math §3.3).
// Items: char, cmd (atom), pending (a \name being typed), group, script (sub/sup after its base),
// frac, sqrt, arg (one-argument command), delim (\left…\right), raw (verbatim environment).
import mathdoc: lambda.doc.math.math

let letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
fn is_letter(c) => c is string and len(c)==1 and (contains(letters,c) ^ { false })
let fracs = ["frac","dfrac","tfrac","cfrac","binom","dbinom","tbinom"]
let text_args = ["text","textrm","textbf","textit","textsf","texttt","textnormal","mbox"]
let math_args = ["mathrm","mathbf","mathit","mathsf","mathtt","mathcal","mathbb","mathfrak","mathscr","boldsymbol",
    "operatorname","hat","bar","vec","dot","ddot","tilde","widehat","widetilde","overline","underline",
    "overrightarrow","overleftarrow","boxed","phantom","overbrace","underbrace","cancel","bm"]
let caret_token = "lambdacaret"
pub let caret_mark = "\\" ++ caret_token ++ " "
let operators = "+-=<>,;:!|"

// --- source → tokens --------------------------------------------------------
// tokens keep character spans so environments can be copied verbatim.
fn scan_step(s,x) {
    if (s.name!=null and is_letter(x.c)) {*:s,name:s.name ++ x.c}
    else if (s.name=="") {*:s,name:null,word:false,out:[*s.out,{kind:"sym",v:"\\" ++ x.c,s:s.at,e:x.j+1}]}
    else {
        let word = s.word or s.name!=null
        let out = if (s.name==null) s.out else [*s.out,{kind:"cmd",v:s.name,s:s.at,e:x.j}];
        if (x.c=="\\") {*:s,out:out,name:"",at:x.j,word:false}
        // spaces after a control word end it; other spaces matter only in text arguments.
        else if (contains(" \t\r\n",x.c)) {*:s,out:if (word) out else [*out,{kind:"space",v:" ",s:x.j,e:x.j+1}],name:null,word:word}
        else {*:s,out:[*out,{kind:"char",v:x.c,s:x.j,e:x.j+1}],name:null,word:false}
    }
}
fn scan(chars) {
    let s = reduce([{out:[],name:null,at:0,word:false},*[for (j,c in chars) {c:c,j:j}]],scan_step);
    if (s.name==null) s.out else if (s.name=="") [*s.out,{kind:"char",v:"\\",s:s.at,e:s.at+1}]
        else [*s.out,{kind:"cmd",v:s.name,s:s.at,e:len(chars)}]
}

// --- tokens → tree ----------------------------------------------------------
fn is_tok(t,kind,v) => t!=null and t.kind==kind and t.v==v
fn skip_spaces(tok,i) => if (i<len(tok) and tok[i].kind=="space") skip_spaces(tok,i+1) else i
fn env_end(tok,i,depth) => if (i>=len(tok)) null
    else if (is_tok(tok[i],"cmd","begin")) env_end(tok,i+1,depth+1)
    else if (is_tok(tok[i],"cmd","end")) (if (depth==1) close_brace(tok,i+1) else env_end(tok,i+1,depth-1))
    else env_end(tok,i+1,depth)
fn close_brace(tok,i) => if (i>=len(tok)) null else if (is_tok(tok[i],"char","}")) i else close_brace(tok,i+1)
fn delimiter_text(t) => if (t.kind=="cmd") "\\" ++ t.v else t.v

// a mandatory argument: a braced row or one item.
fn parse_arg(ctx,i,path,text) map^ {
    let at = if (text) i else skip_spaces(ctx.tok,i)
    let t = ctx.tok[at]
    if (t==null) raise error("math field: missing argument")
    else if (is_tok(t,"char","{")) parse_row(ctx,at+1,path,text,"}",[],null)^
    else {
        let one = parse_item(ctx,at,path,[],text)^;
        {row:one.row,i:one.i,caret:one.caret}
    }
}
fn args(ctx,i,path,index,count,text,r=0,rows=[],caret=null) map^ {
    if (r>=count) {rows:rows,i:i,caret:caret} else {
        let a = parse_arg(ctx,i,[*path,index,r],text)^;
        args(ctx,a.i,path,index,count,text,r+1,[*rows,a.row],caret or a.caret)^
    }
}
// parse the next item and append it to row (a script merges into a preceding script).
fn parse_item(ctx,i,path,row,text) map^ {
    let t = ctx.tok[i]
    let index = len(row)
    let push = (item,next,caret) => {row:[*row,item],i:next,caret:caret};
    if (t.kind=="space") (if (text) push({k:"char",t:" "},i+1,null) else {row:row,i:i+1,caret:null})
    else if (is_tok(t,"cmd",caret_token)) {row:row,i:i+1,caret:{path:path,index:index}}
    else if (is_tok(t,"char","{")) {
        let g = parse_row(ctx,i+1,[*path,index,0],text,"}",[],null)^;
        push({k:"group",rows:[g.row]},g.i,g.caret)
    }
    else if (not text and (is_tok(t,"char","^") or is_tok(t,"char","_"))) {
        let slot = if (t.v=="^") "sup" else "sub"
        let prev = row[index-1]
        let merge = prev!=null and prev.k=="script" and not contains(prev.slots,slot)
        let at = if (merge) index-1 else index
        let a = parse_arg(ctx,i+1,[*path,at,if (merge) len(prev.rows) else 0],text)^;
        if (merge) {row:[*slice(row,0,at),{*:prev,slots:[*prev.slots,slot],rows:[*prev.rows,a.row]}],i:a.i,caret:a.caret}
        else push({k:"script",slots:[slot],rows:[a.row]},a.i,a.caret)
    }
    else if (t.kind=="cmd" and contains(fracs,t.v)) {
        let a = args(ctx,i+1,path,index,2,false)^;
        push({k:"frac",t:"\\" ++ t.v,rows:a.rows},a.i,a.caret)
    }
    else if (is_tok(t,"cmd","sqrt")) {
        let at = skip_spaces(ctx.tok,i+1)
        let indexed = is_tok(ctx.tok[at],"char","[")
        let idx = if (indexed) parse_row(ctx,at+1,[*path,index,0],false,"]",[],null)^ else null
        let body = parse_arg(ctx,if (indexed) idx.i else i+1,[*path,index,if (indexed) 1 else 0],false)^;
        push({k:"sqrt",slots:if (indexed) ["index","body"] else ["body"],rows:if (indexed) [idx.row,body.row] else [body.row]},
            body.i,if (indexed) idx.caret or body.caret else body.caret)
    }
    else if (t.kind=="cmd" and (contains(text_args,t.v) or contains(math_args,t.v))) {
        let a = args(ctx,i+1,path,index,1,contains(text_args,t.v))^;
        push({k:"arg",t:"\\" ++ t.v,text:contains(text_args,t.v),rows:a.rows},a.i,a.caret)
    }
    else if (is_tok(t,"cmd","left")) {
        let l = skip_spaces(ctx.tok,i+1)
        let body = parse_row(ctx,l+1,[*path,index,0],false,"right",[],null)^
        let r = skip_spaces(ctx.tok,body.i+1);
        if (ctx.tok[l]==null or ctx.tok[r]==null) raise error("math field: unmatched \\left")
        else push({k:"delim",l:delimiter_text(ctx.tok[l]),r:delimiter_text(ctx.tok[r]),rows:[body.row]},r+1,body.caret)
    }
    else if (is_tok(t,"cmd","begin")) {
        let close = env_end(ctx.tok,i,0)
        let raw = if (close==null) "" else join(slice(ctx.chars,t.s,ctx.tok[close].e),"")
        // an environment is one atom; a caret token inside it moves after the atom.
        if (close==null) raise error("math field: unmatched \\begin")
        else push({k:"raw",t:replace(replace(raw,caret_mark,""),"\\" ++ caret_token,"")},close+1,
            if (contains(raw,"\\" ++ caret_token)) {path:path,index:index+1} else null)
    }
    else if (t.kind=="cmd") push({k:"cmd",t:"\\" ++ t.v},i+1,null)
    else if (t.kind=="sym") push({k:"cmd",t:t.v},i+1,null)
    else if (is_tok(t,"char","}")) raise error("math field: unbalanced }")
    else push({k:"char",t:t.v},i+1,null)
}
fn parse_row(ctx,i,path,text,stop,row,caret) map^ {
    let t = ctx.tok[i]
    if (t==null) (if (stop==null) {row:row,i:i,caret:caret} else raise error("math field: missing " ++ stop))
    else if ((stop=="}" and is_tok(t,"char","}")) or (stop=="]" and is_tok(t,"char","]"))) {row:row,i:i+1,caret:caret}
    else if (stop=="right" and is_tok(t,"cmd","right")) {row:row,i:i,caret:caret}
    else {
        let next = parse_item(ctx,i,path,row,text)^;
        parse_row(ctx,next.i,path,text,stop,next.row,caret or next.caret)^
    }
}
// source (with an optional \lambdacaret token) → {row, caret}
pub fn parse_field(source) map^ {
    let chars = split(source,"")
    let parsed = parse_row({chars:chars,tok:scan(chars)},0,[],false,null,[],null)^;
    {row:parsed.row,caret:settle(parsed.row,parsed.caret or {path:[],index:len(parsed.row)})}
}

// --- tree → source ----------------------------------------------------------
// a mark directly before a script would become its base, so that slot does not exist.
fn valid(row,i) => i==0 or i>=len(row) or row[i].k!="script"
fn letter_command(t) => len(t)>1 and (starts_with(t,"\\") ^ { false }) and is_letter(slice(t,1,2) ^ { "" })
fn rows_of(item) => item.rows or []
fn key(path,index) => join([for (p in path) string(p)],",") ++ ":" ++ string(index)
fn marker(path,i,opts) =>
    if (opts.caret!=null and opts.caret.index==i and opts.caret.path==path) caret_mark
    else if (opts.marks!=null and valid(opts.row_of(path),i)) "\\lambdamark{" ++ string(opts.marks[key(path,i)]) ++ "}"
    else ""
fn ser_item(item,row,i,path,opts) {
    let r = (k) => ser_row(item.rows[k],[*path,i,k],opts);
    if (item.k=="char") item.t
    else if (item.k=="cmd") item.t ++ (if (letter_command(item.t)) " " else "")
    else if (item.k=="pending") "\\mathrm{\\backslash " ++ item.t ++ "}"
    else if (item.k=="group") "{" ++ r(0) ++ "}"
    else if (item.k=="script") (if (i==0) "{}" else "") ++
        join([for (k,slot in item.slots) (if (slot=="sub") "_{" else "^{") ++ r(k) ++ "}"],"")
    else if (item.k=="frac") item.t ++ "{" ++ r(0) ++ "}{" ++ r(1) ++ "}"
    else if (item.k=="sqrt") "\\sqrt" ++ (if (len(item.rows)==2) "[" ++ r(0) ++ "]{" ++ r(1) ++ "}" else "{" ++ r(0) ++ "}")
    else if (item.k=="arg") item.t ++ "{" ++ r(0) ++ "}"
    else if (item.k=="delim") "\\left" ++ item.l ++ (if (letter_command(item.l)) " " else "") ++ r(0) ++
        "\\right" ++ item.r ++ (if (letter_command(item.r)) " " else "")
    else item.t
}
fn ser_row(row,path,opts) => join([for (i in 0 to len(row)) marker(path,i,opts) ++
    (if (i<len(row)) ser_item(row[i],row,i,path,opts) else "")],"")
pub fn source(root) => ser_row(root,[],{caret:null,marks:null})
pub fn source_with_caret(root,caret) => ser_row(root,[],{caret:caret,marks:null})

// --- caret slots --------------------------------------------------------------
pub fn row_at(root,path) => if (len(path)==0) root else row_at(root[path[0]].rows[path[1]],slice(path,2,len(path)))
// slots in reading order: before item i, inside its rows, then after it.
pub fn positions(row,path=[]) => [for (i in 0 to len(row)) for (p in [
    *(if (valid(row,i)) [{path:path,index:i}] else []),
    *(if (i<len(row)) [for (k,sub in rows_of(row[i])) for (q in positions(sub,[*path,i,k])) q] else [])]) p]
fn same(a,b) => a.index==b.index and a.path==b.path
fn rank(ps,caret) => [for (k,p in ps where same(p,caret)) k][0]
// a caret produced by an edit may sit before a script; move it to the next real slot.
pub fn settle(root,caret) {
    let ps = positions(root)
    let exact = rank(ps,caret);
    if (exact!=null) caret else [for (p in ps where p.path==caret.path and p.index>caret.index) p][0] or ps[last]
}
// left/right/home/end; null means the caret leaves the equation.
pub fn move(root,caret,dir) {
    let ps = positions(root)
    let k = rank(ps,caret);
    if (dir=="left") (if (k==0) null else ps[k-1])
    else if (dir=="right") (if (k==len(ps)-1) null else ps[k+1])
    // Home/End go to the ends of the whole equation, like Word's line ends.
    else if (dir=="home") ps[0]
    else ps[last]
}

// --- geometry -------------------------------------------------------------------
fn numbers(text) => [for (part in split(replace(replace(text,","," "),")"," ")," ") where trim(part)!="") float(part) ^ { 0 }]
fn transform_of(text,f) {
    let t = trim(text or "");
    if (starts_with(t,"translate(")) (let v=numbers(slice(t,10,len(t))), {*:f,x:f.x+f.sx*v[0],y:f.y+f.sy*(v[1] or 0)})
    else if (starts_with(t,"scale(")) (let v=numbers(slice(t,6,len(t))), {*:f,sx:f.sx*v[0],sy:f.sy*(v[1] or v[0])})
    else f
}
fn marks_in(el,f) => if (not (el is element)) []
    else if (name(el)=='line' and el.class=="math-edit-mark")
        [{id:int(el["data-mark"]),x:f.x,top:f.y+f.sy*float(el.y1),bottom:f.y+f.sy*float(el.y2)}]
    else (let g=transform_of(el.transform,f), [for (c in el) for (m in marks_in(c,g)) m])
// every slot's caret line, in CSS px from the formula's top-left corner.
pub fn geometry(root,options) array^ {
    let ps = positions(root)
    let ids = map([for (k,p in ps) for (part in [key(p.path,p.index),k]) part])
    let src = ser_row(root,[],{caret:null,marks:ids,row_of:(path)=>row_at(root,path)})
    let measured = mathdoc.render_box(parse(src,{type:"math",flavor:"latex"})^,options)^
    let px = options.font_size/1000.0
    let found = marks_in(measured.element,{x:0,y:0,sx:1,sy:1});
    [for (m in found) {caret:ps[m.id],x:m.x*px,top:m.top*px+measured.height*options.font_size,
        bottom:m.bottom*px+measured.height*options.font_size}]
}
pub fn caret_box(geometry,caret) => [for (g in geometry where same(g.caret,caret)) g][0]
fn outside(v,lo,hi) => if (v<lo) lo-v else if (v>hi) v-hi else 0
pub fn nearest(geometry,x,y) => sort(geometry,(g)=>abs(g.x-x)+2*outside(y,g.top,g.bottom))[0].caret
// the row an enclosing structure stacks above or below this one: numerator/denominator,
// superscript/subscript, root index/body.
fn stacked_row(root,path,down) {
    if (len(path)==0) null else {
        let owner = slice(path,0,len(path)-2)
        let i = path[len(path)-2]
        let k = path[len(path)-1]
        let item = row_at(root,owner)[i]
        let want = if (item.k=="script") [for (j,s in item.slots where s==(if (down) "sub" else "sup")) j][0]
            else if (item.k=="frac" or (item.k=="sqrt" and len(item.rows)==2)) (if (down) 1 else 0) else null;
        if (want!=null and want!=k) [*owner,i,want] else stacked_row(root,owner,down)
    }
}
// up/down follow the structure first, then the nearest slot wholly above or below.
pub fn vertical(root,geometry,caret,down) {
    let at = caret_box(geometry,caret)
    let target = stacked_row(root,caret.path,down)
    let pool = if (at==null) [] else if (target!=null) [for (g in geometry where g.caret.path==target) g]
        else [for (g in geometry where if (down) g.top>at.top+1 and g.bottom>at.bottom+1 else g.top<at.top-1 and g.bottom<at.bottom-1) g];
    if (len(pool)==0) null else sort(pool,(g)=>abs(g.x-at.x)*4+abs(g.top-at.top))[0].caret
}

// --- edits ----------------------------------------------------------------------
fn splice(row,at,remove,items) => [*slice(row,0,at),*items,*slice(row,at+remove,len(row))]
// every edit replaces a span of one row.
fn edit_row(root,path,at,remove,items) => if (len(path)==0) splice(root,at,remove,items) else
    [for (i,item in root) if (i!=path[0]) item else {*:item,rows:[for (k,r in item.rows)
        if (k==path[1]) edit_row(r,slice(path,2,len(path)),at,remove,items) else r]}]
fn text_mode(root,path) => len(path)>0 and (let item=row_at(root,slice(path,0,len(path)-2))[path[len(path)-2]], item.k=="arg" and item.text)
fn escape(ch,text) =>
    if (contains("{}#$%&",ch)) "\\" ++ ch
    else if (ch=="~") (if (text) "\\textasciitilde " else "\\sim ")
    else if (text and (ch=="^" or ch=="_")) "\\" ++ ch ++ "{}"
    else if (ch=="\\") "\\backslash "
    else null
fn atom(ch,text) => (let e=escape(ch,text), if (e==null) {k:"char",t:ch} else {k:"cmd",t:trim(e)})
fn insert(root,caret,items,inside=null) {
    let next = edit_row(root,caret.path,caret.index,0,items);
    {row:next,caret:if (inside!=null) inside else {path:caret.path,index:caret.index+len(items)}}
}
fn operand(item) => item!=null and not (item.k=="char" and contains(operators,item.t)) and
    not (item.k=="cmd" and contains(["\\pm","\\mp","\\times","\\cdot","\\div","\\le","\\ge","\\leq","\\geq","\\ne","\\neq","\\to","\\approx","\\equiv"],item.t))
// a finished \name becomes its structure: \frac, \sqrt and one-argument commands take the caret inside.
fn resolve(root,caret) {
    let row = row_at(root,caret.path)
    let at = caret.index-1
    let name = row[at].t
    let item = if (contains(fracs,name)) {k:"frac",t:"\\" ++ name,rows:[[],[]]}
        else if (name=="sqrt") {k:"sqrt",slots:["body"],rows:[[]]}
        else if (contains(text_args,name) or contains(math_args,name)) {k:"arg",t:"\\" ++ name,text:contains(text_args,name),rows:[[]]}
        else {k:"cmd",t:"\\" ++ name}
    let next = edit_row(root,caret.path,at,1,[item]);
    {row:next,caret:if (item.rows==null) {path:caret.path,index:at+1} else {path:[*caret.path,at,0],index:0}}
}
fn script(root,caret,slot) {
    let row = row_at(root,caret.path)
    let prev = row[caret.index-1]
    let depth = len(caret.path)
    let owner = if (depth==0) null else row_at(root,slice(caret.path,0,depth-2))[caret.path[depth-2]];
    // x_1^2: at the end of a filled script slot, the other script belongs to the same base.
    if (owner!=null and owner.k=="script" and len(row)>0 and caret.index==len(row) and not contains(owner.slots,slot))
        script(root,{path:slice(caret.path,0,depth-2),index:caret.path[depth-2]+1},slot)
    else if (prev!=null and prev.k=="script") {
        let k = [for (j,s in prev.slots where s==slot) j][0]
        if (k!=null) {row:root,caret:{path:[*caret.path,caret.index-1,k],index:len(prev.rows[k])}}
        else {
            // TeX order keeps a subscript before a superscript.
            let slots = if (slot=="sub") ["sub",*prev.slots] else [*prev.slots,"sup"]
            let rows = if (slot=="sub") [[],*prev.rows] else [*prev.rows,[]]
            let at = if (slot=="sub") 0 else len(rows)-1;
            {row:edit_row(root,caret.path,caret.index-1,1,[{*:prev,slots:slots,rows:rows}]),
                caret:{path:[*caret.path,caret.index-1,at],index:0}}
        }
    }
    else insert(root,caret,[{k:"script",slots:[slot],rows:[[]]}],{path:[*caret.path,caret.index,0],index:0})
}
// "/" lifts the preceding operand (with its scripts) into a numerator, as Word's math autocorrect does.
fn fraction(root,caret) {
    let row = row_at(root,caret.path)
    let i = caret.index
    let take = if (i>0 and row[i-1].k=="script" and i>1 and operand(row[i-2])) 2 else if (operand(row[i-1])) 1 else 0
    let item = {k:"frac",t:"\\frac",rows:[slice(row,i-take,i),[]]};
    {row:edit_row(root,caret.path,i-take,take,[item]),
        caret:{path:[*caret.path,i-take,if (take>0) 1 else 0],index:0}}
}
// typing: letters extend a pending \name; any other key first resolves it.
pub fn type_text(root,caret,ch) {
    let row = row_at(root,caret.path)
    let prev = row[caret.index-1]
    let text = text_mode(root,caret.path);
    if (prev!=null and prev.k=="pending") {
        if (is_letter(ch)) {row:edit_row(root,caret.path,caret.index-1,1,[{*:prev,t:prev.t ++ ch}]),caret:caret}
        else if (prev.t=="") {row:edit_row(root,caret.path,caret.index-1,1,[{k:"cmd",t:"\\" ++ ch}]),caret:caret}
        else (let done=resolve(root,caret), if (ch==" ") done else type_text(done.row,done.caret,ch))
    }
    else if (text) insert(root,caret,[atom(ch,true)])
    else if (ch==" ") {row:root,caret:caret}
    else if (ch=="\\") insert(root,caret,[{k:"pending",t:""}])
    else if (ch=="^" or ch=="_") script(root,caret,if (ch=="^") "sup" else "sub")
    else if (ch=="/") fraction(root,caret)
    else insert(root,caret,[atom(ch,false)])
}
fn empty(item) => all([for (r in rows_of(item)) len(r)==0])
fn remove_item(root,path,at) => {row:edit_row(root,path,at,1,[]),caret:{path:path,index:at}}
// Backspace/Delete remove atoms; a structure is entered first and removed once it is empty.
pub fn erase(root,caret,forward) {
    let row = row_at(root,caret.path)
    let target = if (forward) caret.index else caret.index-1
    let item = row[target]
    let parent = if (len(caret.path)==0) null else {path:slice(caret.path,0,len(caret.path)-2),index:caret.path[len(caret.path)-2]}
    let raw = if (item==null) null else if (len(rows_of(item))==0 or empty(item)) remove_item(root,caret.path,target)
        else {row:root,caret:if (forward) {path:[*caret.path,target,0],index:0}
            else (let k=len(item.rows)-1, {path:[*caret.path,target,k],index:len(item.rows[k])})}
    let out = if (raw!=null) raw
        else if (parent==null) {row:root,caret:caret}
        else if (empty(row_at(root,parent.path)[parent.index])) remove_item(root,parent.path,parent.index)
        else {row:root,caret:move(root,caret,if (forward) "right" else "left") or caret};
    {row:out.row,caret:settle(out.row,out.caret)}
}
// a ribbon template is LaTeX with caret_mark where typing continues.
pub fn insert_source(root,caret,fragment) map^ {
    let src = source_with_caret(root,caret)
    let at = index_of(src,caret_mark)
    parse_field(slice(src,0,at) ++ fragment ++ slice(src,at+len(caret_mark),len(src)))^
}
