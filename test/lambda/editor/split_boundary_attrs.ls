// D7.2.5: hosts declare which boundary attributes stay on the original block.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import commands: lambda.editor.mod_commands
import html: lambda.edit.html

let attrs = [{name:'id',value:"anchor"},{name:'page-start',value:true},{name:'style',value:"text-align:center"}]
let doc = node('doc',[node_attrs('p',attrs,[text("BeforeAfter")])])
let schema = {*:html.schema,p:{*:html.schema.p,attrs:[{name:'page-start',keep_on_split:false}]}}
let st = {doc:doc,schema:schema,selection:text_selection(pos([0,0],6),pos([0,0],6))}
let split = commands.cmd_split_block(st)
split.doc_after.content[0].attrs==attrs
split.doc_after.content[1].attrs==[{name:'style',value:"text-align:center"}]
doc_text(split.doc_after)=="BeforeAfter"
let ranged = commands.cmd_split_block({*:st,selection:text_selection(pos([0,0],4),pos([0,0],8))})
ranged.doc_after.content[1].attrs==[{name:'style',value:"text-align:center"}]
doc_text(ranged.doc_after)=="Befoter"
let empty = commands.cmd_split_block({*:st,doc:node('doc',[node_attrs('p',attrs,[])]),selection:text_selection(pos([0],0),pos([0],0))})
empty.doc_after.content[1].attrs==[{name:'style',value:"text-align:center"}]
let ordinary = commands.cmd_split_block({*:st,schema:html.schema})
attrs_get(ordinary.doc_after.content[1].attrs,'page-start')==true
attrs_get(ordinary.doc_after.content[1].attrs,'id')==null
