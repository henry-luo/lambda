import nav: ~~.mod_navigation
import model: ~~.mod_model
import page: ~~.mod_page
import surface: ~~.mod_surface
import lambda.editor.mod_doc
import lambda.editor.mod_source_pos
import lambda.editor.mod_editor

let doc = node('doc',[node('h1',[text("Opening")]),node('p',[text("First page")]),
    node_attrs('p',[{name:'data-word-page-break',value:"true"}],[]),
    node('p',[text("Second page")]),node_attrs('h2',[{name:'data-word-page-break',value:"true"}],
        [text("Third page")])])
page.starts(doc)==[0,2,4];
[for (entry in nav.pages(doc)) [entry.number,entry.start,len(entry.content)]]==[[1,0,2],[2,2,2],[3,4,1]]
nav.page_position(doc,1)==pos([0,0],0)
nav.page_position(doc,2)==pos([3,0],0)
nav.page_position(node('doc',[node_attrs('p',[{name:'data-word-page-break',value:"true"}],[])]),1)==pos([0],0)
nav.page_position(doc,0)==null and nav.page_position(doc,4)==null
let editor = model.open_editor(doc)
nav.current_page(editor)==1
nav.current_page(edit_set_selection(editor,text_selection(pos([3,0],1),pos([3,0],4))))==2
nav.current_page(edit_set_selection(editor,node_selection([4])))==3
nav.current_heading(edit_set_selection(editor,text_selection(pos([3,0],1),pos([3,0],4))))==[0]
nav.current_heading(edit_set_selection(editor,text_selection(pos([4,0],1),pos([4,0],4))))==[4]
nav.next_tab("headings","ArrowLeft")=="results" and nav.next_tab("results","ArrowRight")=="headings"
nav.next_tab("pages","Home")=="headings" and nav.next_tab("pages","End")=="results"
let formatted = node('doc',[node('p',[text("Before "),text_marked("find",[mark('strong')]),text(" after")])])
nav.excerpt(formatted,model.search(formatted,"find")[0])=={before:"Before ",match:"find",after:" after"}
let bookmarked = node_attrs('p',[{name:'id',value:"word-bookmark-Intro"}],[text("Thumbnail")])
let preview = format(surface.thumbnail(doc,[bookmarked]),'html')^
contains(preview,"Thumbnail") and not contains(preview,"word-bookmark-Intro")
let empty = model.open_editor(node('doc',[node('h2',[])]))
model.select_heading(empty,[0]).selection==text_selection(pos([0],0),pos([0],0))
let nested = node('doc',[node('table',[node('tr',[node('td',[node('h2',[text("Cell heading")])])])])])
model.headings(nested)[0].path==[0,0,0,0]
