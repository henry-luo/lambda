// D7.2.5: shared typography and palettes, promoted from Word Home.
import colors: lambda.ui.core.component

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
    if (grow) choices[0] or max(size,font_sizes[last]) else choices[last] or min(size,font_sizes[0])
}

pub let cases = [{id:"sentence",label:"Sentence case."},{id:"lower",label:"lowercase"},{id:"upper",label:"UPPERCASE"},
    {id:"title",label:"Capitalize Each Word"},{id:"toggle",label:"tOGGLE cASE"}]
fn is_letter(c) => lower(c) != upper(c)
fn case_char(c,mode,word,sentence) =>
    if (mode=="lower") lower(c) else if (mode=="upper") upper(c)
    else if (mode=="toggle") (if (c==upper(c)) lower(c) else upper(c))
    else if ((mode=="title" and word) or (mode=="sentence" and sentence)) upper(c) else lower(c)
// word and sentence starts carry across the runs of one paragraph.
pub fn case_text(text,mode,start) => reduce([{*:start,text:""},*split(text,"")],(acc,c)=>{
    text:acc.text ++ case_char(c,mode,acc.word,acc.sentence),
    word:not (is_letter(c) or contains("0123456789'’",c)),
    sentence:if (contains(".!?",c)) true else if (is_letter(c)) false else acc.sentence})

// retain an authored family even when the platform does not have it installed.
pub fn font_options(families,current) => [for (family in sort(unique([*(families or []),current]),(name)=>lower(name)))
    {value:family,label:family}]

pub let bullet_styles = [{id:"disc",label:"Filled Bullet",glyph:"●"},{id:"circle",label:"Hollow Bullet",glyph:"○"},{id:"square",label:"Square Bullet",glyph:"■"}]
pub let number_styles = [{id:"decimal",label:"1. 2. 3.",glyph:"1."},{id:"lower-alpha",label:"a. b. c.",glyph:"a."},
    {id:"upper-alpha",label:"A. B. C.",glyph:"A."},{id:"lower-roman",label:"i. ii. iii.",glyph:"i."},{id:"upper-roman",label:"I. II. III.",glyph:"I."}]
pub fn list_styles(kind) => if (kind=="ordered") number_styles else bullet_styles
