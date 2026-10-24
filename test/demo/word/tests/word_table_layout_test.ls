import model: ~~.mod_model
import layout: ~~.mod_table_layout
import tables: ~~.mod_table
import grid: ~~.mod_table_grid
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

fn row(values,header=false) => node('tr',[for (v in values) node(if (header) 'th' else 'td',[text(v)])])
let table = node_attrs('table',[{name:'id',value:"original"}],[
    row(["Name","Value","Total"],true),row(["Zebra","20","3"]),row(["Apple","10","4"]),row(["Pear","30",""])])
let doc = node('doc',[node('p',[text("Before")]),table,table,node('p',[text("After")])])
let base = edit_set_selection(model.open_editor(doc),text_selection(pos([1,1,1,0],0),pos([1,1,1,0],0)))
fn choose(ed,r,c) {
    let e = grid.at(tables.entries(ed),r,c)
    edit_set_selection(ed,node_selection(e.path))
}
fn css(ed,path,key) => model.css_get(attrs_get(node_at(ed.doc,path).attrs,'style'),key)
fn at(ed,r,c) => grid.at(tables.entries(ed),r,c)
fn run(ed,id,p={}) map^ => model.execute(ed,"table." ++ id,p)^
let region = edit_set_selection(base,tables.selection(base,"cell",[1,1,1],[1,2,2]))
let merged = run(region,"mergeCells")^
len(tables.entries(merged))==9 and at(merged,1,1).end_row==3 and at(merged,1,1).end_col==3;
doc_text(at(merged,1,1).node)=="203104" and len([for (n in at(merged,1,1).node.content where n.tag=='br') n])==3;
merged.doc.content[0]==doc.content[0] and merged.doc.content[2]==table and merged.doc.content[3]==doc.content[3];
len(merged.history.undo)==1 and model.execute(merged,"edit.undo")^.doc==doc;
model.execute(model.execute(merged,"edit.undo")^,"edit.redo")^.doc==merged.doc;
len(tables.selected(run(choose(merged,2,0),"selectRow")^))==3;
len(tables.selected(run(choose(merged,1,1),"selectColumn")^))==5;
let unmerged = run(merged,"splitCells",{rows:2,columns:2})^
len(tables.entries(unmerged))==12 and at(unmerged,1,1).end_row==2 and at(unmerged,1,1).end_col==2;
doc_text(at(unmerged,1,1).node)=="203104" and doc_text(at(unmerged,2,2).node)=="";
model.execute(unmerged,"edit.undo")^.doc==merged.doc;
let refined = run(base,"splitCells",{rows:2,columns:3})^
grid.bounds(tables.entries(refined)).bottom==5 and grid.bounds(tables.entries(refined)).right==5;
at(refined,1,0).end_row==3 and at(refined,0,1).end_col==4;
len(tables.entries(refined))==17 and doc_text(at(refined,1,1).node)=="20";
let added_row = run(choose(merged,1,0),"insertRowBelow")^
grid.bounds(tables.entries(added_row)).bottom==5 and at(added_row,1,1).end_row==4;
doc_text(at(added_row,3,0).node)=="Apple" and doc_text(at(added_row,2,0).node)=="";
let added_col = run(choose(merged,0,1),"insertColumnRight")^
grid.bounds(tables.entries(added_col)).right==4 and at(added_col,1,1).end_col==4;
doc_text(at(added_col,0,3).node)=="Total" and doc_text(at(added_col,0,2).node)=="";
let deleted_row = run(choose(merged,1,0),"deleteRow")^
grid.bounds(tables.entries(deleted_row)).bottom==3 and at(deleted_row,1,1).end_row==2;
doc_text(at(deleted_row,1,1).node)=="203104" and doc_text(at(deleted_row,1,0).node)=="Apple";
let deleted_col = run(choose(merged,0,1),"deleteColumn")^
grid.bounds(tables.entries(deleted_col)).right==2 and at(deleted_col,1,1).end_col==2;
doc_text(at(deleted_col,1,1).node)=="203104";
let split = run(base,"splitTable")^
len(split.doc.content)==6 and split.doc.content[1].tag=='table' and split.doc.content[2].tag=='p' and split.doc.content[3].tag=='table';
attrs_get(split.doc.content[3].attrs,'id')==null and doc_text(split.doc.content[4])==doc_text(table);
model.execute(split,"edit.undo")^.doc==doc;
let sized = run(run(base,"cellSize",{axis:"height",value:42})^,"cellSize",{axis:"width",value:95})^
css(sized,[1,1],"height")=="42pt" and css(sized,[1,1,1],"width")=="95pt";
layout.settings(sized).height==42 and layout.settings(sized).width==95;
css(run(sized,"cellSize",{axis:"width",value:null})^,[1,1,1],"width")==null;
let entire = run(base,"selectTable")^
let merged_borders = run(merged,"borders",{preset:"all",style:"solid",width:2,color:"#2563eb"})^
all([for (p in [[1,0,1],[1,0,2]]) css(merged_borders,p,"border-bottom")=="2pt solid #2563eb"]) and
    all([for (p in [[1,1,0],[1,2,0]]) css(merged_borders,p,"border-right")=="2pt solid #2563eb"]);
let fully_merged = run(entire,"mergeCells")^
len(tables.entries(fully_merged))==1 and len(fully_merged.doc.content[1].content)==4 and len(fully_merged.doc.content[1].content[1].content)==0;
model.execute(fully_merged,"edit.undo")^.doc==doc;
let full_import = files.import_html_document(files.html_document(fully_merged.doc,"Full merge")^)^
len(grid.entries(full_import,[1]))==1 and grid.at(grid.entries(full_import,[1]),0,0).end_row==4;
tables.projected_selection(entire,text_selection(pos([1,0,0,0],0),pos([1,0,0,0],0)));
not tables.projected_selection(entire,text_selection(pos([1,0,0,0],1),pos([1,0,0,0],1)));
not tables.projected_selection(base,base.selection);
let columns = run(entire,"distributeColumns",{widths:[60,120,90]})^
all([for (e in tables.entries(columns)) css(columns,e.path,"width")=="90pt"]);
let heights = run(entire,"distributeRows",{heights:[18,24,30,36]})^
all([for (r in 0 to 3) css(heights,[1,r],"height")=="27pt"]);
let fixed = run(columns,"autoFit",{mode:"fixed",widths:[50,70,90]})^
css(fixed,[1],"table-layout")=="fixed" and css(fixed,[1,1,1],"width")=="70pt";
css(fixed,[1],"width")=="210pt";
let fit = run(fixed,"autoFit",{mode:"contents"})^
css(fit,[1],"width")=="auto" and all([for (e in tables.entries(fit)) css(fit,e.path,"width")==null]);
css(run(fit,"autoFit",{mode:"window"})^,[1],"width")=="100%";
let positioned = run(base,"properties",{table_width:80,alignment:"center",indent:0})^
css(positioned,[1],"width")=="80%" and css(positioned,[1],"margin-left")=="auto" and css(positioned,[1],"margin-right")=="auto";
let margin = run(region,"cellMargins",{top:2,right:4,bottom:6,left:8})^
all([for (e in tables.selected(margin)) css(margin,e.path,"padding-left")=="8pt" and css(margin,e.path,"padding-top")=="2pt"]);
css(margin,[1,0,0],"padding-left")==null and layout.settings(margin).bottom==6;
let vertical = run(base,"textDirection")^
css(vertical,[1,1,1],"writing-mode")=="vertical-rl" and css(run(run(vertical,"textDirection")^,"textDirection")^,[1,1,1],"writing-mode")=="horizontal-tb";
let sorted = run(base,"sort",{column:1,type:"text",descending:false,header:true})^
doc_text(at(sorted,0,0).node)=="Name" and doc_text(at(sorted,1,0).node)=="Apple" and doc_text(at(sorted,3,0).node)=="Zebra";
doc_text(at(run(base,"sort",{column:2,type:"number",descending:true,header:true})^,1,0).node)=="Pear";
model.execute(sorted,"edit.undo")^.doc==doc;
let repeated = run(choose(base,0,0),"repeatHeader",{enabled:true})^
repeated.doc.content[1].content[0].tag=='thead' and repeated.doc.content[1].content[1].tag=='tr';
layout.settings(repeated).repeat and len(tables.entries(repeated))==12;
not layout.settings(run(repeated,"repeatHeader",{enabled:false})^).repeat;
let total = run(choose(base,3,2),"formula",{expression:"=SUM(ABOVE)",decimals:"general"})^
doc_text(at(total,3,2).node)=="7" and attrs_get(at(total,3,2).node.attrs,'data-word-formula')=="=SUM(ABOVE)";
doc_text(at(run(choose(base,3,2),"formula",{expression:"AVERAGE(B2:B4)",decimals:2})^,3,2).node)=="20";
doc_text(at(run(choose(base,3,2),"formula",{expression:"COUNT(ABOVE)",decimals:0})^,3,2).node)=="2";
let converted = run(base,"convertText",{separator:"tabs"})^
converted.doc.content[1].tag=='p' and doc_text(converted.doc.content[1])=="Name\tValue\tTotal" and doc_text(converted.doc.content[2])=="Zebra\t20\t3";
model.execute(converted,"edit.undo")^.doc==doc;
len(run(base,"convertText",{separator:"paragraphs"})^.doc.content)==15;
files.restore(files.archive(merged.doc,"Layout"))^.doc==merged.doc;
let exported = files.html_document(merged.doc,"Layout")^
contains(exported,"rowspan=\"2\"") and contains(exported,"colspan=\"2\"");
let imported = files.import_html_document(exported)^
len(grid.entries(imported,[1]))==9 and doc_text(grid.at(grid.entries(imported,[1]),1,1).node)=="203104";
run(base,"mergeCells") ^ { true } ~ { false };
run(base,"splitCells",{rows:0,columns:2}) ^ { true } ~ { false };
run(base,"cellSize",{axis:"width",value: -1}) ^ { true } ~ { false };
run(base,"cellMargins",{top:2,right:4,bottom:6,left:100}) ^ { true } ~ { false };
run(merged,"sort",{column:1,type:"text",descending:false,header:true}) ^ { true } ~ { false };
run(base,"repeatHeader",{enabled:true}) ^ { true } ~ { false };
run(choose(base,3,2),"formula",{expression:"SUM(A1:C4)",decimals:"general"}) ^ { true } ~ { false }

let numeric_header = run(repeated,"sort",{column:2,type:"number",descending:false,header:false})^
doc_text(at(numeric_header,0,0).node)=="Name" and doc_text(at(numeric_header,1,0).node)=="Apple"
