import sheet: ~~.~~.demo.sheet.mod_model
import slides: ~~.~~.demo.slides.mod_model

let s0 = sheet.initial(sheet.blank())
let s1 = sheet.set_value(sheet.select(s0,0,0),"one")
let s2 = sheet.set_value(sheet.select(s1,0,1),"two")
let su1 = sheet.history(s2)
let su2 = sheet.history(su1)
let sr1 = sheet.history(su2,true)
let sr2 = sheet.history(sr1,true)
let sbranch = sheet.set_value(sheet.select(su1,1,0),"other")
let p0 = slides.initial()
let p1 = slides.add_object(p0,"text")^
let p2 = slides.set_text(p1,"Title")^
let pu1 = slides.history(p2)^
let pu2 = slides.history(pu1)^
let pr1 = slides.history(pu2,true)^
let pr2 = slides.history(pr1,true)^
let pbranch = slides.set_text(pu1,"Other")^;
[
    su1.book == s1.book and su2.book == s0.book,
    sr1.book == s1.book and sr2.book == s2.book,
    len(sbranch.redo) == 0 and sheet.history(sbranch,true).book == sbranch.book,
    pu1.doc == p1.doc and pu2.doc == p0.doc,
    pr1.doc == p1.doc and pr2.doc == p2.doc,
    pu1.selected == p1.selected and pr2.selected == p2.selected,
    len(pbranch.redo) == 0 and slides.history(pbranch,true)^.doc == pbranch.doc
]
