import model: ~~.mod_model
import refs: ~~.mod_references
import files: ~~.mod_io
import context: ~~.mod_context
import commands: lambda.editor.mod_commands
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.open_editor(node('doc',[node('p',[text("Before after")])]))
let book = {*:refs.defaults,author:"Lovelace, Ada",title:"Notes on the analytical engine",year:"1843",publisher:"Taylor",city:"London"}
let saved = model.execute(base,"references.saveSource",book)^
len(refs.sources(saved.doc))==1
model.plain_text(saved.doc)=="Before after"
model.execute(saved,"edit.undo")^.doc==base.doc
let key = refs.sources(saved.doc)[0].id
len(refs.sources(model.execute(saved,"references.saveSource",book)^.doc))==1
let middle = edit_set_selection(saved,text_selection(pos([0,0],7),pos([0,0],7)))
let inserted = model.execute(middle,"references.insertCitation",{id:key,locator:"12"})^
model.plain_text(inserted.doc)=="Before (Lovelace, 1843, p. 12)after"
len(refs.entries(inserted.doc))==1 and refs.uses(inserted.doc,key)==1
model.execute(inserted,"edit.undo")^.doc==middle.doc
let typed = model.execute(inserted,"text.insert",{data:" and "})^
model.plain_text(typed.doc)=="Before (Lovelace, 1843, p. 12) and after"
doc_text(refs.entries(typed.doc)[0].node)=="(Lovelace, 1843, p. 12)"
refs.current(typed)==null
let bib = model.execute(typed,"references.bibliography")^
len(refs.bibliographies(bib.doc))==1
contains(model.plain_text(bib.doc),"Lovelace, A. (1843). Notes on the analytical engine. Taylor.")
contains(files.html_document(bib.doc,"References")^,"font-style: italic") or contains(files.html_document(bib.doc,"References")^,"font-style:italic")
model.execute(bib,"edit.undo")^.doc==typed.doc
len(refs.bibliographies(model.execute(bib,"references.bibliography")^.doc))==1
let revised = model.execute(bib,"references.saveSource",{*:book,id:key,title:"Revised engine notes",year:"1844"})^
contains(model.plain_text(revised.doc),"(Lovelace, 1844, p. 12)") and contains(model.plain_text(revised.doc),"Revised engine notes")
refs.entries(revised.doc)[0].value.source.title=="Revised engine notes" and
    attrs_get(refs.entries(revised.doc)[0].node.attrs,'title')=="Revised engine notes"
model.execute(revised,"edit.undo")^.doc==bib.doc
let ieee = model.execute(bib,"references.style",{style:"ieee"})^
doc_text(refs.entries(ieee.doc)[0].node)=="[1, p. 12]"
contains(doc_text(refs.bibliographies(ieee.doc)[0].node),"[1]")
model.execute(ieee,"edit.undo")^.doc==bib.doc
let chicago = model.execute(bib,"references.style",{style:"chicago-author-date"})^
doc_text(refs.entries(chicago.doc)[0].node)=="(Lovelace 1843, 12)"
refs.settings(chicago.doc).style=="chicago-author-date"
let active = edit_set_selection(bib,text_selection(pos(refs.entries(bib.doc)[0].path,0),pos(refs.entries(bib.doc)[0].path,0)))
let localized = model.execute(active,"references.editCitation",{locator:"24–26"})^
contains(doc_text(refs.entries(localized.doc)[0].node),"24–26")
refs.current(localized)==null
contains([for (a in context.describe(active).actions) a.command],"ui.editCitation")
not contains([for (a in context.describe(active).actions) a.command],"ui.link")
let removed = model.execute(active,"references.removeCitation")^
len(refs.entries(removed.doc))==0 and not contains(doc_text(refs.bibliographies(removed.doc)[0].node),"Lovelace")
model.execute(removed,"edit.undo")^.doc==bib.doc
let deleted = model.execute(removed,"references.deleteSource",{id:key})^
len(refs.sources(deleted.doc))==0
model.execute(bib,"references.deleteSource",{id:key}) ^ { true } ~ { false }
files.restore(files.archive(revised.doc,"Citations"))^.doc==revised.doc
let reopened = files.import_html_document(files.html_document(revised.doc,"Citations")^)^
refs.sources(reopened)==refs.sources(revised.doc)
doc_text(refs.entries(reopened)[0].node)==doc_text(refs.entries(revised.doc)[0].node)
len(refs.bibliographies(reopened))==1
let pasted = edit_apply(base,commands.cmd_paste_fragment(base,[node('p',[refs.entries(revised.doc)[0].node])]))
len(refs.sources(pasted.doc))==1 and refs.sources(pasted.doc)[0].title=="Revised engine notes"
let adopted = model.execute(pasted,"references.refresh")^
refs.settings(adopted.doc).sources==refs.sources(pasted.doc)
let organization = {*:refs.defaults,kind:"webpage",corporate:true,author:"Open Studio",title:"Making together",container:"Open Studio",url:"https://example.org/studio"}
let web = model.execute(base,"references.saveSource",{*:organization,insert:true})^
doc_text(refs.entries(web.doc)[0].node)=="(Open Studio, n.d.)"
model.execute(web,"edit.undo")^.doc==base.doc
let article = {*:refs.defaults,kind:"article-journal",author:"Turing, Alan; Church, Alonzo",title:"Computing & <making>",year:"1936",container:"Journal of Making",volume:"4",issue:"2",pages:"10-25",doi:"10.1000/test"}
let journal = model.execute(base,"references.saveSource",{*:article,insert:true})^
doc_text(refs.entries(journal.doc)[0].node)=="(Turing & Church, 1936)"
contains(refs.rendered(journal.doc)^.bibliography[0].text,"https://doi.org/10.1000/test")
contains(refs.rendered(journal.doc)^.bibliography[0].text,"<making>")
let unsafe = model.execute(base,"references.saveSource",{*:book,title:"<script>alert(1)</script>",insert:true})^
not contains(files.html_document(unsafe.doc,"Escaped")^,"<script>")
contains(files.html_document(model.execute(unsafe,"references.bibliography")^.doc,"Escaped")^,"&lt;script&gt;")
let unicode = model.execute(base,"references.saveSource",{*:book,author:"李, 明",title:"共同創作",insert:true})^
contains(doc_text(refs.entries(unicode.doc)[0].node),"李")
model.execute(base,"references.saveSource",{*:book,title:""}) ^ { true } ~ { false }
model.execute(base,"references.saveSource",{*:book,year:"bad"}) ^ { true } ~ { false }
model.execute(base,"references.saveSource",{*:book,url:"javascript:alert(1)"}) ^ { true } ~ { false }
model.execute(base,"references.saveSource",{*:book,accessed:"2026-13-40"}) ^ { true } ~ { false }
model.execute(base,"references.saveSource",{*:book,accessed:"2026-02-30"}) ^ { true } ~ { false }
model.execute(base,"references.insertCitation",{id:"missing"}) ^ { true } ~ { false }
model.execute(base,"references.style",{style:"missing"}) ^ { true } ~ { false }
model.execute(base,"references.bibliography") ^ { true } ~ { false }
model.execute(base,"references.editCitation",{locator:"12"}) ^ { true } ~ { false }
