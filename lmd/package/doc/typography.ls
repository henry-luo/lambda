// pure text transformations shared by document editors (S12.1.1v3).
pub let font_sizes = [8,9,10,11,12,14,16,18,20,24,28,36,48,72]

pub fn font_step(size,grow) {
    let choices = [for (s in font_sizes where if (grow) s>size else s<size) s];
    if (grow) choices[0] or max(size,font_sizes[last]) else choices[last] or min(size,font_sizes[0])
}


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

