import model: ~~.mod_model
import home: ~~.mod_home
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

// Change Case maps each selected run and keeps the selection on the same characters.
let runs = model.open_editor(node('doc',[node('p',[text("the open "),text_marked("STUDIO. every",[mark('strong',true)]),text(" week")])]))
let all = edit_set_selection(runs,text_selection(pos([0,0],0),pos([0,2],5)))
doc_text(model.execute(all,"format.case",{mode:"sentence"})^.doc) == "The open studio. Every week"
doc_text(model.execute(all,"format.case",{mode:"title"})^.doc) == "The Open Studio. Every Week"
doc_text(model.execute(all,"format.case",{mode:"upper"})^.doc) == "THE OPEN STUDIO. EVERY WEEK"
doc_text(model.execute(all,"format.case",{mode:"toggle"})^.doc) == "THE OPEN studio. EVERY WEEK"
let lowered = model.execute(all,"format.case",{mode:"lower"})^
doc_text(lowered.doc) == "the open studio. every week"
lowered.doc.content[0].content[1].marks == [mark('strong',true)]
lowered.selection == all.selection
model.execute(lowered,"edit.undo")^.doc == runs.doc
let partial = edit_set_selection(runs,text_selection(pos([0,0],4),pos([0,0],8)))
doc_text(model.execute(partial,"format.case",{mode:"upper"})^.doc) == "the OPEN STUDIO. every week"
model.execute(runs,"format.case",{mode:"upper"}) ^ { true } ~ { false }
model.execute(all,"format.case",{mode:"shout"}) ^ { true } ~ { false }

// Format Painter copies character formatting and keeps the target's link.
let styled = [mark('em',true),mark('span',[{name:'style',value:"color: #2864ad; font-size: 14pt"}])]
let linked = [mark('a',[{name:'href',value:"https://example.com"}]),mark('strong',true)]
let painted = home.painted(linked,styled)
mark_value(painted,'a') != null and mark_value(painted,'em')==true and mark_value(painted,'strong')==null
attrs_get(mark_value(painted,'span'),'style') == "color: #2864ad; font-size: 14pt"
let target = edit_set_selection(runs,text_selection(pos([0,2],1),pos([0,2],5)))
let brushed = model.execute(target,"format.painter",{marks:styled})^
model.char_value(brushed,"font-size","") == "14pt"

// Grow/Shrink Font step through the size list.
home.font_step(11,true) == 12 and home.font_step(11,false) == 10 and home.font_step(72,true) == 72

// Character shading, border, colors, and paragraph shading are span or block CSS.
let word = edit_set_selection(runs,text_selection(pos([0,0],4),pos([0,0],8)))
let shaded = model.execute(word,"format.shading",{color:home.shading_gray})^
model.char_value(shaded,"background-color","") == home.shading_gray
model.char_value(model.execute(shaded,"format.shading",{color:"none"})^,"background-color","none") == "none"
let boxed = model.execute(word,"format.border",{value:home.border_line})^
model.char_value(boxed,"border","") == home.border_line
model.execute(word,"format.border",{value:"9pt dotted red"}) ^ { true } ~ { false }
model.char_value(model.execute(word,"format.color",{color:"#7030a0"})^,"color","") == "#7030a0"
model.execute(word,"format.color",{color:"url(x)"}) ^ { true } ~ { false }
let block_css = (ed) => attrs_get(ed.doc.content[0].attrs,'style')
contains(block_css(model.execute(word,"para.shading",{color:"#fff2cc"})^),"background-color: #fff2cc")

// Line spacing follows Word's menu, including 2.5 and 3.0.
contains(block_css(model.execute(word,"para.lineSpacing",{value:2.5})^),"line-height: 2.5")
contains(block_css(model.execute(word,"para.spaceBefore",{value:12})^),"margin-top: 12pt")

// Paragraph borders toggle a side; Outside boxes a run of paragraphs.
let three = model.open_editor(node('doc',[node('p',[text("One")]),node('p',[text("Two")]),node('p',[text("Three")])]))
let span3 = edit_set_selection(three,text_selection(pos([0,0],0),pos([2,0],5)))
let css_of = (ed,i) => attrs_get(ed.doc.content[i].attrs,'style') or ""
let bottom = model.execute(edit_set_selection(three,text_selection(pos([0,0],1),pos([0,0],1))),"para.borders",{preset:"bottom"})^
contains(css_of(bottom,0),"border-bottom: " ++ home.border_line) and css_of(bottom,1) == ""
not contains(css_of(model.execute(bottom,"para.borders",{preset:"bottom"})^,0),"border-bottom")
let outside = model.execute(span3,"para.borders",{preset:"outside"})^
contains(css_of(outside,0),"border-top") and not contains(css_of(outside,0),"border-bottom")
not contains(css_of(outside,1),"border-top") and contains(css_of(outside,1),"border-left")
contains(css_of(outside,2),"border-bottom")
not contains(css_of(model.execute(outside,"para.borders",{preset:"none"})^,1),"border")

// Increase Indent steps half an inch outside lists; Decrease returns to the margin.
let caret = edit_set_selection(three,text_selection(pos([1,0],1),pos([1,0],1)))
let indented = model.execute(caret,"para.indent")^
contains(css_of(indented,1),"margin-left: 36pt")
contains(css_of(model.execute(indented,"para.indent")^,1),"margin-left: 72pt")
contains(css_of(model.execute(indented,"para.outdent")^,1),"margin-left: 0pt")

// Sort reorders the selected paragraphs in one step.
let sorted = model.execute(span3,"para.sort")^
;[for (b in sorted.doc.content) doc_text(b)] == ["One","Three","Two"]
;[for (b in model.execute(span3,"para.sort",{descending:true})^.doc.content) doc_text(b)] == ["Two","Three","One"]
model.execute(sorted,"edit.undo")^.doc == three.doc
model.execute(caret,"para.sort") ^ { true } ~ { false }

// List styles wrap plain text first and set the marker type in the same undo step.
let roman = model.execute(caret,"para.listStyle",{kind:"ordered",style:"upper-roman"})^
roman.doc.content[1].tag == 'ol'
contains(attrs_get(roman.doc.content[1].attrs,'style'),"list-style-type: upper-roman")
model.execute(roman,"edit.undo")^.doc == three.doc
let squares = model.execute(edit_set_selection(roman,text_selection(pos([1,0,0,0],1),pos([1,0,0,0],1))),"para.listStyle",{kind:"bullet",style:"square"})^
squares.doc.content[1].tag == 'ul'
contains(attrs_get(squares.doc.content[1].attrs,'style'),"list-style-type: square")
model.execute(caret,"para.listStyle",{kind:"bullet",style:"star"}) ^ { true } ~ { false }

// Paste Text Only inserts plain text at the caret.
doc_text(model.execute(edit_set_selection(three,text_selection(pos([0,0],3),pos([0,0],3))),"edit.pasteText",{text:"!"})^.doc.content[0]) == "One!"
