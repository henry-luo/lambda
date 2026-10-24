// Home ribbon formatting that the shared editor commands do not cover (Word Home tab).
// Pure model helpers: mod_model selects the target blocks and applies the transactions.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import format: lambda.editor.mod_inline_format
import colors: lambda.ui.core.component
import review: .mod_review

pub let font_sizes = [8,9,10,11,12,14,16,18,20,24,28,36,48,72]
pub let line_spacings = [1,1.15,1.5,2,2.5,3]
pub let ink = "#24334b"
pub let shading_gray = "#d9d9d9"
pub let border_line = "0.75pt solid currentColor"
pub let font_colors = [{value:"#24334b",label:"Ink"},{value:"#2864ad",label:"Blue"},{value:"#b43d47",label:"Red"},{value:"#28745d",label:"Green"},
    {value:"#c00000",label:"Dark Red"},{value:"#ff0000",label:"Bright Red"},{value:"#ffc000",label:"Orange"},{value:"#ffff00",label:"Yellow"},
    {value:"#92d050",label:"Light Green"},{value:"#00b050",label:"Bright Green"},{value:"#00b0f0",label:"Light Blue"},{value:"#0070c0",label:"Bright Blue"},
    {value:"#002060",label:"Dark Blue"},{value:"#7030a0",label:"Purple"}]
pub let highlight_colors = [{value:"#f9e896",label:"Yellow"},{value:"#c8efc0",label:"Green"},{value:"#c2eef0",label:"Turquoise"},
    {value:"#f8cfe3",label:"Pink"},{value:"#dceafa",label:"Blue"},{value:"#f8c9c4",label:"Red"},{value:"#e9dcf7",label:"Violet"},{value:"#e3e6ea",label:"Gray"}]
pub let shading_colors = [{value:"#f2f2f2",label:"Light Gray"},{value:shading_gray,label:"Gray"},{value:"#dceafa",label:"Blue"},
    {value:"#e2efd9",label:"Green"},{value:"#fff2cc",label:"Gold"},{value:"#fbe4d5",label:"Orange"},{value:"#ede2f6",label:"Purple"},{value:"#deebf6",label:"Sky"}]
pub let defaults = {color:"#b43d47",highlight:"#f9e896",shading:"#f2f2f2"}
// "none" clears the declaration; legacy "transparent" stays valid for saved documents.
pub fn color_value(value) => if (value=="none") "" else if (value=="transparent" or colors.hex_color(value)) value else null

pub fn font_step(size,grow) {
    let choices = [for (s in font_sizes where if (grow) s>size else s<size) s];
    if (grow) choices[0] or font_sizes[last] else choices[last] or font_sizes[0]
}

pub let cases = [{id:"sentence",label:"Sentence case."},{id:"lower",label:"lowercase"},{id:"upper",label:"UPPERCASE"},
    {id:"title",label:"Capitalize Each Word"},{id:"toggle",label:"tOGGLE cASE"}]
fn is_letter(c) => lower(c) != upper(c)
fn case_char(c,mode,word,sentence) =>
    if (mode=="lower") lower(c) else if (mode=="upper") upper(c)
    else if (mode=="toggle") (if (c==upper(c)) lower(c) else upper(c))
    else if ((mode=="title" and word) or (mode=="sentence" and sentence)) upper(c) else lower(c)
// word and sentence starts carry across the runs of one paragraph.
fn case_text(text,mode,start) => reduce([{*:start,text:""},*split(text,"")],(acc,c)=>{
    text:acc.text ++ case_char(c,mode,acc.word,acc.sentence),
    word:not (is_letter(c) or contains("0123456789'’",c)),
    sentence:if (contains(".!?",c)) true else if (is_letter(c)) false else acc.sentence})
fn case_pos(p,items) {
    let item = [for (i in items where path_equal(i.path,p.path)) i][0];
    if (item==null or p.offset < item.to) p else pos(p.path,p.offset+item.end-item.to)
}
// one-for-one leaf replacement keeps paths stable; offsets shift only when case mapping changes length.
pub fn case_transaction(editor,mode) map^ {
    let sel = editor.selection
    let entries = [for (e in format.leaves(editor.doc),let seg=format.leaf_segment(e,sel) where seg!=null and seg.to>seg.from) {*:e,seg:seg}]
    if (not contains([for (c in cases) c.id],mode)) raise error("Word: choose a text case")
    else if (len(entries)==0) raise error("Word: select text to change its case")
    else {
        let items = reduce([{parent:null,state:null,items:[]},*entries],(acc,e)=>{
            let parent = parent_path(e.path)
            let start = if (acc.parent!=null and path_equal(acc.parent,parent)) acc.state else {word:true,sentence:true}
            let out = case_text(slice(e.leaf.text,e.seg.from,e.seg.to),mode,start);
            {parent:parent,state:{word:out.word,sentence:out.sentence},items:[*acc.items,{path:e.path,marks:e.leaf.marks,to:e.seg.to,
                end:e.seg.from+len(out.text),old:e.leaf.text,text:slice(e.leaf.text,0,e.seg.from) ++ out.text ++ slice(e.leaf.text,e.seg.to)}]}
        }).items
        let tx = reduce([tx_begin(editor.doc,sel),*[for (i in items where i.text!=i.old) i]],(tx,i)=>
            tx_step(tx,step_replace(parent_path(i.path),last_index(i.path),last_index(i.path)+1,[text_marked(i.text,i.marks)])));
        if (sel.kind!='text') tx else tx_set_selection(tx,text_selection(case_pos(sel.anchor,items),case_pos(sel.head,items)))
    }
}

// Format Painter carries character formatting; links and review metadata stay with the target text.
let format_marks = ['strong','b','em','i','u','del','s','sub','sup']
pub fn painted(marks,source) {
    let kept = review.clear_format(marks)
    let attrs = if (len(kept)==0) [] else kept[0].value
    let style = format.span_css(source)
    let span = if (style=="") attrs else attrs_set(attrs,'style',style)
    let base = [for (m in marks where m.name=='a') m]
    let formatted = reduce([base,*[for (m in source where contains(format_marks,m.name)) m]],(acc,m)=>with_mark(acc,m.name,m.value));
    if (len(span)==0) formatted else with_mark(formatted,'span',span)
}

pub let bullet_styles = [{id:"disc",label:"Filled Bullet",glyph:"●"},{id:"circle",label:"Hollow Bullet",glyph:"○"},{id:"square",label:"Square Bullet",glyph:"■"}]
pub let number_styles = [{id:"decimal",label:"1. 2. 3.",glyph:"1."},{id:"lower-alpha",label:"a. b. c.",glyph:"a."},
    {id:"upper-alpha",label:"A. B. C.",glyph:"A."},{id:"lower-roman",label:"i. ii. iii.",glyph:"i."},{id:"upper-roman",label:"I. II. III.",glyph:"I."}]
pub fn list_styles(kind) => if (kind=="ordered") number_styles else bullet_styles
pub fn list_style(n) => format.css_get(attrs_get(n.attrs,'style'),"list-style-type")

pub let border_presets = [{id:"bottom",label:"Bottom Border"},{id:"top",label:"Top Border"},{id:"left",label:"Left Border"},
    {id:"right",label:"Right Border"},{id:"none",label:"No Border"},{id:"all",label:"All Borders"},{id:"outside",label:"Outside Borders"}]
let sides = ["top","right","bottom","left"]
fn side_properties(side,on) => [{key:"border-" ++ side,value:if (on) border_line else ""},
    {key:"padding-" ++ side,value:if (not on) "" else if (side=="top" or side=="bottom") "1pt" else "4pt"}]
pub fn has_border(n,side) => format.css_get(attrs_get(n.attrs,'style'),"border-" ++ side)!=null
// a single side toggles from the first paragraph; Outside draws one box around the run of paragraphs.
pub fn border_properties(entries,preset) array^ {
    let blocks = [for (e in entries where not contains(['td','th'],e.node.tag)) e]
    if (not contains([for (p in border_presets) p.id],preset)) raise error("Word: choose a paragraph border")
    else if (len(blocks)==0) raise error("Word: place the caret in a paragraph first")
    else {
        let toggled = contains(sides,preset) and not has_border(blocks[0].node,preset);
        [for (i,e in blocks) {path:e.path,properties:[for (side in sides) for (p in side_properties(side,
            if (preset=="all") true else if (preset=="none") false
            else if (preset=="outside") (side=="left" or side=="right" or (side=="top" and i==0) or (side=="bottom" and i==len(blocks)-1))
            else if (side==preset) toggled else has_border(e.node,side))) p]}]
    }
}

// Sorting reorders whole paragraphs, or whole list items when each holds one paragraph.
fn sort_unit(doc,entry) {
    let parent = parent_path(entry.path)
    let host = node_at(doc,parent);
    if (len(parent)>0 and host.tag=='li' and len(host.content)==1) parent else entry.path
}
pub fn sort_transaction(editor,entries,descending) map^ {
    let doc = editor.doc
    let units = [for (e in entries) sort_unit(doc,e)]
    let parent = parent_path(units[0])
    let first = last_index(units[0])
    if (len(units)<2) raise error("Word: select two or more paragraphs to sort")
    else if (not all([for (i,u in units) path_equal(parent_path(u),parent) and last_index(u)==first+i]))
        raise error("Word: sort paragraphs that share one list or text flow")
    else {
        let nodes = [for (u in units) node_at(doc,u)]
        let ordered = sort(nodes,(n)=>lower(doc_text(n)))
        let tx = tx_step(tx_begin(doc,editor.selection),step_replace(parent,first,first+len(units),if (descending) reverse(ordered) else ordered))
        let leaves = format.leaves(node_at(tx.doc_after,parent),parent)
        let inside = [for (l in leaves,let at=l.path[len(parent)] where at>=first and at<first+len(units)) l];
        if (len(inside)==0) tx else tx_set_selection(tx,text_selection(pos(inside[0].path,0),pos(inside[last].path,len(inside[last].leaf.text))))
    }
}
