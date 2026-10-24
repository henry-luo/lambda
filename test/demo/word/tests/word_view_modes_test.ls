import views: ~~.mod_view
import model: ~~.mod_model
import page: ~~.mod_page
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.initial()
let selected = edit_set_selection(base,text_selection(pos([0,0],1),pos([0,0],4)))
let st = {editor:selected,layout:"print",read_only:false,status:"Ready",zoom:1.2,focused:true}
let web = views.activate(st,"view.webLayout")
views.active(web.layout,web.read_only)=="web"
web.editor==selected and web.zoom==1.2 and web.focused
page.settings(web.editor.doc)==page.settings(base.doc)
let reading = views.activate(web,"view.readMode")
views.active(reading.layout,reading.read_only)=="read" and reading.layout=="web"
views.activate(reading,"view.readMode",true)==reading
let resumed = views.activate(reading,"view.readMode")
views.active(resumed.layout,resumed.read_only)=="web" and resumed.editor==selected
let printing = views.activate(reading,"view.printLayout")
views.active(printing.layout,printing.read_only)=="print" and not printing.read_only
printing.editor==selected and printing.zoom==1.2 and printing.focused
views.activate(st,"unrelated")==st
