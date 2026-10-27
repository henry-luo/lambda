// D7.2.5 / S12.1.3: handlers call these effects; rendering only projects state.
import dom
import m: .mod_model
import f: .mod_formula
import files: .mod_io

pub pn focus(owner) { dom.request_frame(dom.get_element_by_id(dom.root_node(owner),"sheet"),"sheet_focus") }
pub fn begin_edit(st,text=null) => {*:st,editing:true,draft:if (text==null) m.active(st).raw or "" else text}
pub pn accept(st,owner) {
    let node=dom.get_element_by_id(dom.root_node(owner),if (st.editing) "cell-editor" else "formula-input")
    return m.set_value(st,dom.get_state(node,"value") or "")
}
pub fn new_book(st,sample=false) => {*:m.initial(if (sample) m.sample() else m.blank()),row:0,col:0,anchor:{row:0,col:0}}
pub pn command(st,id,owner) map^ {
    if (id=="undo" or id=="redo") { return m.history(st,id=="redo") }
    if (id=="new" or id=="sample" or id=="open") {
        if (st.book!=st.saved) { return {*:st,dialog:"replace",pending:id} }
        if (id=="open") { return {*:st,dialog:"open",dialog_path:""} }
        return new_book(st,id=="sample")
    }
    if (id=="dialog-cancel") { return {*:st,dialog:"",pending:null} }
    if (id=="dialog-discard") {
        if (st.pending=="open") { return {*:st,dialog:"open",dialog_path:"",pending:null} }
        return new_book(st,st.pending=="sample")
    }
    if (id=="save" or id=="export" or id=="rename-sheet") {
        return {*:st,dialog:if (id=="rename-sheet") "rename" else id,
            dialog_path:if (id=="export") "workbook.csv" else if (st.path=="") "workbook.sheet.json" else st.path}
    }
    if (id=="dialog-confirm") {
        if (st.dialog=="replace") { return {*:st,dialog:"save",dialog_path:if (st.path=="") "workbook.sheet.json" else st.path} }
        let path=trim(dom.get_state(dom.get_element_by_id(dom.root_node(owner),"file-path"),"value") or "")
        if (path=="") { return {*:st,status:"Enter a name or file path",error:"Enter a name or file path"} }
        if (st.dialog=="rename") {
            if (len(path)>31 or contains(path,":") or contains(path,"/") or contains(path,"\\") or
                any([for (i,s in st.book.sheets where i!=st.sheet) s.title==path])) {
                return {*:st,status:"Use a unique sheet name of 1–31 characters without :, / or \\"}
            }
            let next=m.change(st,{*:st.book,sheets:[for (i,s in st.book.sheets) if (i==st.sheet) {*:s,title:path} else s]},"Sheet renamed")
            return {*:next,dialog:""}
        }
        if (st.dialog=="open") {
            let book=files.read(path)^
            return {*:m.initial(book),path:path,row:0,col:0,anchor:{row:0,col:0},status:"Opened " ++ path}
        }
        if (st.dialog=="export") {
            files.export_csv(m.current(st),path)^
            return {*:st,dialog:"",status:"Exported " ++ path}
        }
        files.write(st.book,path)^
        let saved={*:st,saved:st.book,path:path,dialog:"",status:"Saved " ++ path}
        if (st.pending!=null) { return command({*:saved,pending:null},st.pending,owner)^ }
        return saved
    }
    if (id=="copy" or id=="cut") {
        dom.clipboard_write(m.copy_text(st))
        return if (id=="cut") m.clear(st) else {*:st,status:"Copied selection"}
    }
    if (id=="paste") {
        let text=dom.clipboard_text()
        return if (text==null or text=="") {*:st,status:"Clipboard is empty"} else m.paste(st,text)
    }
    if (id=="clear") { return m.clear(st) }
    if (id in ["bold","italic","underline","wrap","border"]) { return m.format_cells(st,id,m.active(st).style[id]!=true) }
    if (starts_with(id,"align-")) { return m.format_cells(st,"align",slice(id,6)) }
    if (id=="currency" or id=="percent") { return m.format_cells(st,"format",id) }
    if (id=="autosum") { return m.autosum(st) }
    if (id=="sort-asc" or id=="sort-desc") { return m.sort_rows(st,id=="sort-desc") }
    if (id=="add-sheet") { return m.add_sheet(st) }
    if (id=="add-rows") {
        if (m.current(st).rows>=500) { return {*:st,status:"This demo supports up to 500 rows per sheet"} }
        return m.change(st,{*:st.book,sheets:[for (i,s in st.book.sheets) if (i==st.sheet) {*:s,rows:min(s.rows+10,500)} else s]},"Rows added")
    }
    if (id=="gridlines") { return {*:st,grid:not st.grid} }
    if (id=="show-formulas") { return {*:st,formulas:not st.formulas} }
    if (id=="zoom-reset") { return {*:st,zoom:1.0} }
    if (id in ["zoom-in","zoom-out","status-zoom-in","status-zoom-out"]) {
        return {*:st,zoom:max(0.5,min(1.5,st.zoom+(if (ends_with(id,"in")) 0.1 else -0.1)))}
    }
    if (id=="cancel-edit") { return {*:st,editing:false,status:"Edit cancelled"} }
    if (id=="accept-edit") { return accept(st,owner) }
    return st
}

// S7.6.7v4: a procedure handler discards its value; capture success in its value arm.
pub pn dispatch(st,id,owner) {
    var result={*:st,error:null}
    command(result,id,owner) ^ { result={*:result,status:string(^.message),error:string(^.message)} } ~ { result=~ }
    return result
}
