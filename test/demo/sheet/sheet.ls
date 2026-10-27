import dom
import resources: ~~.mod_resources
import m: .mod_model
import f: .mod_formula
import app: .mod_app
import v: .mod_view
import overflow: ~~.mod_ribbon_overflow

let BASE=url_resolve(resources.entry_uri()^,".")
let CSS=input(url_resolve(BASE,"../office.css"),'text')^ ++ input(url_resolve(BASE,"../ribbon.css"),'text')^ ++ input(url_resolve(BASE,"sheet.css"),'text')^
fn node(owner,id) => dom.get_element_by_id(dom.root_node(owner),id)
edit <sheet_app> state st:m.initial() {v.workspace(st)}
on render(evt) {
    if (evt.event_phase==2) {
        let owner=node(evt.target,"sheet")
        dom.watch_window(owner,"resize",true)
        dom.request_frame(owner,"sheet_ribbon_ready")
        if (dom.get_attribute(owner,"data-window-min")!="set" and dom.set_window_min_width(owner,720))
            dom.set_attribute(owner,"data-window-min","set")
    }
    return 'pass'
}
on sheet_ribbon_ready(evt) {
    if (evt.event_phase==2) overflow.update(dom.query_selector(node(evt.target,"sheet"),".editor-ribbon"),st.tab,"true")
    return 'pass'
}
on resize(evt) { dom.request_frame(node(evt.target,"sheet"),"sheet_ribbon_ready"); return 'pass' }
on sheet_focus(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    let id=if (st.dialog!="") (if (st.dialog=="replace") "dialog-cancel" else "file-path") else if (st.editing) "cell-editor" else "sheet-grid"
    dom.focus_set(node(evt.target,id),false)
    if (st.editing and st.dialog=="") dom.tc_set_selection(node(evt.target,id),len(st.draft),len(st.draft),"none")
    if (st.dialog=="" and not st.editing) dom.scroll_into_view_options(node(evt.target,"cell-" ++ f.address(st.row,st.col)),{block:"nearest",inline:"nearest"})
    return 'handled'
}
on click(evt) {
    if (evt.button!=null and evt.button!=0) { return 'pass' }
    let group_trigger=dom.closest(evt.target,".ribbon-group-trigger")
    if (group_trigger!=null and st.dialog=="") { overflow.toggle(group_trigger); return 'prevent-default' }
    overflow.close(dom.query_selector(node(evt.target,"sheet"),".editor-ribbon"))
    let target=dom.closest(evt.target,"button,td,[data-column],[data-rowheader],[data-select]")
    if (target==null) { return 'pass' }
    if (st.dialog!="" and dom.closest(target,"#file-dialog")==null) { return 'prevent-default' }
    let id=dom.get_attribute(target,"data-action")
    let tab=dom.get_attribute(target,"data-tab")
    let sheet=dom.get_attribute(target,"data-sheet")
    let row=dom.get_attribute(target,"data-row")
    let col=dom.get_attribute(target,"data-col")
    if (tab!=null) { st={*:st,tab:tab} }
    else if (sheet!=null) { st=m.select({*:st,sheet:int(sheet)},0,0) }
    else if (row!=null and col!=null) {
        if (dom.closest(evt.target,"#cell-editor")!=null) { return 'pass' }
        st=m.select(st,int(row),int(col),evt.shiftKey==true)
    }
    else if (dom.get_attribute(target,"data-column")!=null) {
        let c=int(dom.get_attribute(target,"data-column"))
        st=m.select(m.select(st,0,c),m.current(st).rows-1,c,true)
    }
    else if (dom.get_attribute(target,"data-rowheader")!=null) {
        let r=int(dom.get_attribute(target,"data-rowheader"))
        st=m.select(m.select(st,r,0),r,m.current(st).cols-1,true)
    }
    else if (dom.get_attribute(target,"data-select")!=null) {
        st=m.select(m.select(st,0,0),m.current(st).rows-1,m.current(st).cols-1,true)
    }
    else if (id!=null) { st=app.dispatch(st,id,evt.target) }
    else { return 'pass' }
    app.focus(evt.target)
    return 'prevent-default'
}
on dblclick(evt) {
    if (st.dialog!="") { return 'pass' }
    let cell=dom.closest(evt.target,"td[data-row]")
    if (cell==null) { return 'pass' }
    st=app.begin_edit(m.select(st,int(dom.get_attribute(cell,"data-row")),int(dom.get_attribute(cell,"data-col"))))
    app.focus(evt.target)
    return 'prevent-default'
}
on input(evt) {
    // mirror the native draft without reconciling the grid or replacing the focused field.
    let id=dom.get_attribute(evt.target,"id")
    if (id=="cell-editor") dom.set_state(node(evt.target,"formula-input"),"value",dom.get_state(evt.target,"value"))
    if (id=="formula-input" and st.editing) dom.set_state(node(evt.target,"cell-editor"),"value",dom.get_state(evt.target,"value"))
    return 'pass'
}
on change(evt) {
    let id=dom.get_attribute(evt.target,"id")
    if (id=="formula-input" or id=="cell-editor") { st=m.set_value(st,dom.get_state(evt.target,"value") or "") }
    else if (id=="workbook-title") {
        let title=trim(dom.get_state(evt.target,"value") or "")
        if (title!="") { st=m.change(st,{*:st.book,title:title}) }
    }
    else if (id in ["font","size","fill","color","number-format"]) {
        let value=dom.get_state(evt.target,"value")
        st=m.format_cells(st,if (id=="number-format") "format" else id,if (id=="size") int(value) else value)
    }
    return 'pass'
}
on keydown(evt) {
    let key=string(evt.key)
    let id=dom.get_attribute(evt.target,"id")
    let primary=evt.ctrlKey==true or evt.metaKey==true
    if (st.dialog!="") {
        if (key=="Tab") {
            let controls=dom.query_selector_all(node(evt.target,"file-dialog"),"input,button")
            let at=[for (i,control in controls where dom.focused(control)) i][0] or 0
            dom.focus_set(controls[(at+(if (evt.shiftKey) len(controls)-1 else 1))%len(controls)],false)
            return 'prevent-default'
        }
        if (key=="Escape") { st={*:st,dialog:"",pending:null} }
        else if (key=="Enter") { st=app.dispatch(st,"dialog-confirm",evt.target) }
        else { return 'pass' }
        app.focus(evt.target)
        return 'prevent-default'
    }
    if (key=="Escape") {
        overflow.close(dom.query_selector(node(evt.target,"sheet"),".editor-ribbon"))
        st={*:st,editing:false,status:"Ready"}
        dom.set_state(node(evt.target,"formula-input"),"value",m.active(st).raw or "")
        app.focus(evt.target)
        return 'prevent-default'
    }
    if (id=="name-box" and key=="Enter") {
        let parts=split(dom.get_state(evt.target,"value"),":")
        let start=f.position(parts[0]) ^ {null}
        let end=if (len(parts)>1) (f.position(parts[1]) ^ {null}) else start
        if (start==null or end==null or max(start.row,end.row)>=m.current(st).rows or max(start.col,end.col)>=m.current(st).cols) {
            st={*:st,status:"Enter an address inside this sheet, e.g. B12 or A1:C5"}
        }
        else { st=m.select(m.select(st,start.row,start.col),end.row,end.col,true) }
        app.focus(evt.target)
        return 'prevent-default'
    }
    if (primary and lower(key)=="s") {
        if (st.editing or id=="formula-input") { st=app.accept(st,evt.target) }
        st=app.dispatch(st,"save",evt.target)
        app.focus(evt.target)
        return 'prevent-default'
    }
    let field=dom.closest(evt.target,"input,select,textarea")!=null
    if (field and id!="cell-editor" and id!="formula-input") { return 'pass' }
    if ((id=="cell-editor" or id=="formula-input") and (key=="Enter" or key=="Tab")) {
        st=app.accept(st,evt.target)
        st=m.select(st,st.row+(if (key=="Enter") (if (evt.shiftKey) -1 else 1) else 0),
            st.col+(if (key=="Tab") (if (evt.shiftKey) -1 else 1) else 0))
    }
    else if (field) { return 'pass' }
    else if (primary and lower(key) in ["z","y","c","x","v","b","i","u"]) {
        let action=if (lower(key)=="z") (if (evt.shiftKey) "redo" else "undo") else {y:"redo",c:"copy",x:"cut",v:"paste",b:"bold",i:"italic",u:"underline"}[lower(key)]
        st=app.dispatch(st,action,evt.target)
    }
    else if (key in ["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Tab","Enter","Home","End"]) {
        let dr=if (key=="ArrowDown" or key=="Enter") 1 else if (key=="ArrowUp") -1 else 0
        let dc=if (key=="ArrowRight" or key=="Tab") (if (key=="Tab" and evt.shiftKey) -1 else 1) else if (key=="ArrowLeft") -1 else 0
        st=m.select(st,if (primary and key=="Home") 0 else st.row+dr,
            if (key=="Home") 0 else if (key=="End") m.current(st).cols-1 else st.col+dc,evt.shiftKey==true and key!="Tab")
    }
    else if (key=="Delete" or key=="Backspace") { st=m.clear(st) }
    else if (key=="F2") { st=app.begin_edit(st) }
    else if (len(key)==1 and not primary and evt.altKey!=true) { st=app.begin_edit(st,key) }
    else { return 'pass' }
    app.focus(evt.target)
    return 'prevent-default'
}
<html lang:"en",<head <meta charset:"utf-8"> <title "Sheet — Lambda workspace"> <style CSS>> apply(<sheet_app>,{mode:"edit"})>
