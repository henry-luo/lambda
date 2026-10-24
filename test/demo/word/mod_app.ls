// One controller serves child-widget events and root keyboard handlers.
// emit() targets ancestors, so root handlers call this controller directly.
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_editor
import session: lambda.edit.session
import fields: lambda.edit.files
import document: .mod_document
import model: .mod_model
import storage: .mod_io
import page: .mod_page
import print: .mod_print
import columns: .mod_columns
import insert: .mod_insert
import stories: .mod_stories
import graphics: .mod_graphics
import changes: .mod_changes
import transform: .mod_transform
import controller: lambda.edit.controller
import tables: .mod_table
import table_design: .mod_table_design
import table_layout: .mod_table_layout
import mini: .mod_selection_toolbar
import views: .mod_view
import navigation: .mod_navigation
import ruler: .mod_ruler
import references: .mod_references
import object_layout: .mod_object_layout
import image: lambda.editor.mod_image
import groups: .mod_group
import shape_format: .mod_shape_format
import equation: .mod_equation
import equation_edit: .mod_equation_edit
import equation_catalog: .mod_equation_catalog
import home: .mod_home
import math_edit: .mod_math_edit

pub let default_settings = {show_ribbon_group_labels:false}
pub fn initial(settings=default_settings) => {editor:model.initial(),file_state:storage.new_session(document.sample(),"The Open Studio Handbook"),
    show_ribbon_group_labels:settings.show_ribbon_group_labels==true,
    fonts:sort(unique([for (face in (dom.query_local_fonts(null) ^ {[]})) face.family]),(family)=>lower(family)),
    tab:"Home",math:null,home:{*:home.defaults,painter:null,marks:false},equation_edit:null,shape_pane:false,shape_draft:null,selection_pane:false,object_picks:[],table_pen:table_design.pen,table_gridlines:false,dialog:null,context:null,mini_hidden:false,pending:"",status:"Ready to write",query:"",replace_text:"",search_mode:null,match_case:false,match_index:-1,
    zoom:1.0,nav:true,nav_tab:"headings",nav_target:null,ruler_focus:null,layout:"print",read_only:false,focused:false,review:false,show_resolved:false,revisions:false,show_changes:true}
pub let mounted = controller.mount
pub pn field(node,id) { fields.field_value(node,id) }
pn page_values(node) { return {paper:field(node,"page-paper"),orientation:field(node,"page-orientation"),top:field(node,"page-top"),
    right:field(node,"page-right"),bottom:field(node,"page-bottom"),left:field(node,"page-left"),mirrored:field(node,"page-mirror")=="mirrored"} }
pn shape_values(node,kind=null) { return {kind:kind or field(node,"shape-kind"),width:field(node,"shape-width"),height:field(node,"shape-height"),
    fill:field(node,"shape-fill"),stroke:field(node,"shape-stroke"),weight:field(node,"shape-weight"),rotation:field(node,"shape-rotation"),
    text:field(node,"shape-text"),text_color:field(node,"shape-text-color"),size:field(node,"shape-size")} }
pn art_values(node,style=null) { return {text:field(node,"art-text"),style:style or field(node,"art-style"),transform:field(node,"art-transform"),
    width:field(node,"art-width"),height:field(node,"art-height"),size:field(node,"art-size"),font:field(node,"art-font"),
    fill:field(node,"art-fill"),outline:field(node,"art-outline"),rotation:field(node,"art-rotation")} }
pn source_values(node,d) {
    return {*:d.values,*:map([for (f in references.visible_fields(d.values.kind,d.all_fields==true))
        for (part in [f.key,field(node,"source-" ++ f.key)]) part]),id:d.source_id or ""}
}
pn illustration_values(node,kind,current) {
    return map([for (key,value in graphics.kind(kind).defaults) for (part in [string(key),
        if (string(key)==graphics.choice_field(kind)) current[key] else field(node,kind ++ "-" ++ string(key))]) part])
}
pub let focus_document = controller.focus
pub let is_control = controller.is_control
pub fn primary(evt) => evt.metaKey == true or evt.ctrlKey == true
pub fn hide_toolbar(st) => {*:st,mini_hidden:true}
pub pn refresh_toolbar(st,node) {
    ruler.refresh(node)
    if (mini.visible(st)) mini.refresh(node)
}
pub pn selected_all(st,node) {
    if (st.editor.selection.kind!='all') { return false }
    let root = dom.query_selector(controller.surface(node),".word-pages")
    let bounds = dom.selection_boundaries(dom.document_selection(node))
    // the full-document range spans presentation wrappers; retain its source kind only while that range survives.
    return root!=null and bounds.anchor_node==root and bounds.focus_node==root and
        min(bounds.anchor_offset,bounds.focus_offset)==0 and
        max(bounds.anchor_offset,bounds.focus_offset)==len(dom.child_nodes(root))
}
pub pn adopt(st,evt,changes_only=false) {
    // a structural selection's native caret is only a projection; toolbar/key snapshots retain the cells.
    if (tables.structural(st.editor) and not changes_only) { return st }
    if (selected_all(st,evt.target)) { return st }
    let accepted = controller.adopt(st.editor,evt)
    // focus/selection echoes name the projected first-cell caret, not the logical cell range.
    if (changes_only and tables.projected_selection(st.editor,accepted.editor.selection)) {
        return {*:st,editor:{*:st.editor,native_selection_revision:accepted.editor.native_selection_revision}}
    }
    if (changes_only and not accepted.changed) { return st }
    return {*:st,editor:accepted.editor}
}
pub pn selection_changed(st,evt) {
    // focusing ruler chrome must not replace the saved document range or retire pointer capture.
    if (st.read_only or st.context!=null or ruler.dragging(evt.target)) { return st }
    let adopted = adopt(st,evt,true)
    if (adopted.editor==st.editor) { return st }
    let next = {*:adopted,editor:model.clean(tables.promote(adopted.editor)),
        mini_hidden:if (adopted.editor.selection!=st.editor.selection) false else st.mini_hidden}
    controller.restore(tables.native_editor(next.editor))
    refresh_toolbar(next,evt.target)
    return next
}
let replaced_editor = controller.replace
pub pn finish(st,node,restore_selection=true) {
    // Bind only once an editing action owns a selection. View-only changes
    // before the first caret placement have no model selection to publish.
    controller.restore(tables.native_editor(st.editor),restore_selection and not st.read_only and st.dialog==null)
    refresh_toolbar(st,node)
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    dom.set_close_guard(node,dirty)
    dom.set_window_title(node,(if (dirty) "● " else "") ++ st.file_state.envelope.title ++ " — Word")
}
pn continued(st,id,node) {
    if (id == "file.open") { return {*:st,pending:"",dialog:{kind:"open",path:""}} }
    if (id == "file.close") {
        dom.set_close_guard(node,false)
        dom.request_window_close(node)
        return {*:st,pending:"",dialog:null}
    }
    let doc = if (id == "file.new") document.blank() else document.sample()
    return {*:st,editor:replaced_editor(st.editor,model.open_editor(doc)),object_picks:[],pending:"",dialog:null,query:"",match_index:-1,
        status:"Ready to write",file_state:storage.new_session(doc,
            if (id == "file.new") "Untitled document" else "The Open Studio Handbook")}
}
pn saved(st,path,node,overwrite=false) {
    if (starts_with(path,"temp/word/")) { io.mkdir("temp/word") ^ { null } }
    let result = session.save(st.file_state,st.editor.doc,path,overwrite)
    let next = {*:st,file_state:result.session,status:result.status,
        dialog:if (result.ok) null else if (result.conflict != null)
            {kind:string(result.conflict),path:path,action:"save"} else st.dialog}
    if (result.ok and st.pending != "") continued(next,st.pending,node) else next
}
pn model_command(st,id,params,node,retain_focus=false) {
    let editor = mounted(st.editor,node)
    let next = model.execute(editor,id,params) ^ { return {*:st,editor:editor,status:^.message,
        dialog:if (st.dialog==null) null else {*:st.dialog,error:^.message}} }
    if (not retain_focus) focus_document(node)
    let revision = contains(["review.trackChanges","review.accept","review.reject","review.acceptAll","review.rejectAll"],id)
    return {*:st,editor:model.clean(next),object_picks:[],dialog:null,status:model.spec(id).label,
        review:not revision and (st.review or starts_with(id,"review.")),revisions:st.revisions or revision}
}
pn dialog_model(st,id,values,params,node) {
    return model_command({*:st,dialog:{*:st.dialog,values:values}},id,params,node)
}
// an in-place equation edit commits the new source; the open LaTeX pane follows the same revision.
pn math_commit(st,change,node) {
    let next = model_command(st,"equation.format",{source:math_edit.source(change.row)},node)
    if (next.editor.model_revision==st.editor.model_revision) { return {*:next,math:st.math} }
    let pane = if (equation_edit.active(st)) {*:st.equation_edit,revision:next.editor.model_revision,
        values:equation.settings(image.selected(next.editor).node)} else st.equation_edit
    return {*:next,equation_edit:pane,math:math_edit.start(next.editor,null,change.caret),status:"Editing equation"}
}
pub pn math_key(st,evt,node) {
    let k = math_edit.key(st,evt)
    if (k==null) { return null }
    if (k.kind=="move") { return {*:st,math:{*:st.math,caret:k.caret}} }
    if (k.kind=="exit") { return {*:st,math:null,status:"Equation selected"} }
    if (k.kind=="leave") {
        focus_document(node)
        return {*:st,math:null,editor:edit_set_selection(mounted(st.editor,node),math_edit.beside(st.editor,k.after))}
    }
    if (k.row==st.math.row) { return {*:st,math:{*:st.math,caret:k.caret}} }
    return math_commit(st,k,node)
}
pn equation_apply(st,node,values,close=false,selection=null) {
    let draft = equation_edit.draft(st)
    let next = model_command(st,"equation.format",equation_edit.parameters(values),node,true)
    if (next.editor.model_revision==st.editor.model_revision and next.status!="Equation formatting") {
        return {*:next,equation_edit:{*:draft,values:values,error:next.status}}
    }
    if (close) { focus_document(node); return {*:next,equation_edit:null,tab:"Home"} }
    equation_edit.request(node)
    return {*:next,equation_edit:{path:draft.path,revision:next.editor.model_revision,
        values:equation.settings(image.selected(next.editor).node),error:null,
        selection:selection or equation_edit.source_range(node,values.source),focus:"equation-editor-source"}}
}
pn dialog_command(st,action,node) {
    if (action == "cancel") { focus_document(node); return {*:st,dialog:null,pending:""} }
    if (action == "discard") { return continued(st,st.pending,node) }
    if (action=="object_layout") {
        let values = {wrap:field(node,"object-wrap"),side:field(node,"object-side"),distance:field(node,"object-distance"),
            x:field(node,"object-x"),y:field(node,"object-y")}
        return dialog_model(st,"object.layout",values,{values:{*:values,distance:float(values.distance),x:float(values.x),y:float(values.y)}},node)
    }
    if (action=="source") {
        let values = source_values(node,st.dialog)
        return dialog_model(st,"references.saveSource",values,{*:values,insert:st.dialog.insert==true,locator:st.dialog.locator or ""},node)
    }
    if (action=="citation") {
        return dialog_model(st,"references.editCitation",{locator:field(node,"citation-locator")},{locator:field(node,"citation-locator")},node)
    }
    if (starts_with(action,"table_")) {
        if (action=="table_properties") {
            let p = {table_width:float(field(node,"table-preferred-width")),alignment:field(node,"table-position"),indent:float(field(node,"table-indent"))}
            return dialog_model(st,"table.properties",p,p,node)
        }
        if (action=="table_split") {
            let p = {columns:float(field(node,"split-columns")),rows:float(field(node,"split-rows"))}
            return dialog_model(st,"table.splitCells",p,p,node)
        }
        if (action=="table_margins") {
            let p = map([for (side in ["top","right","bottom","left"]) for (part in [side,float(field(node,"cell-margin-" ++ side))]) part])
            return dialog_model(st,"table.cellMargins",p,p,node)
        }
        if (action=="table_sort") {
            let p = {column:float(field(node,"table-sort-column")),type:field(node,"table-sort-type"),
                descending:field(node,"table-sort-order")=="true",header:field(node,"table-sort-header")=="true"}
            return dialog_model(st,"table.sort",p,p,node)
        }
        if (action=="table_text") {
            let p = {separator:field(node,"table-text-separator")}
            return dialog_model(st,"table.convertText",p,p,node)
        }
        if (action=="table_formula") {
            let decimals = field(node,"table-formula-decimals")
            let p = {expression:field(node,"table-formula-expression"),decimals:if (decimals=="general") decimals else float(decimals)}
            return dialog_model(st,"table.formula",p,p,node)
        }
    }
    if (action=="table_shading") {
        let values = {color:field(node,"table-shading-color")}
        return dialog_model(st,"table.shading",values,values,node)
    }
    if (action=="illustration") {
        let entry = graphics.kind(st.dialog.kind)
        let values = illustration_values(node,entry.kind,st.dialog.values)
        return dialog_model(st,if (st.dialog.editing) entry.update else entry.insert,values,graphics.parameters(entry.kind,values),node)
    }
    if (action=="emoji") {
        let values = {char:field(node,"emoji-text")}
        return dialog_model(st,"insert.emoji",values,values,node)
    }
    if (action=="cell_borders") {
        let values = {preset:field(node,"border-preset"),style:field(node,"border-style"),
            width:field(node,"border-width"),color:field(node,"border-color")}
        return dialog_model(st,"table.borders",values,{*:values,width:float(values.width)},node)
    }
    if (action == "save") { return command({*:st,dialog:null},"file.save",{},node) }
    if (action == "insert_table") {
        let values = {rows:field(node,"table-rows"),cols:field(node,"table-columns"),header:field(node,"table-header")}
        return dialog_model(st,"insert.table",values,{rows:float(values.rows),cols:float(values.cols),header:values.header=="true"},node)
    }
    if (action == "page_setup") {
        let values = page_values(node)
        return dialog_model(st,"layout.pageSetup",values,{*:values,top:float(values.top),right:float(values.right),
            bottom:float(values.bottom),left:float(values.left)},node)
    }
    if (action == "columns") {
        let values = {count:field(node,"columns-count"),gap:field(node,"columns-gap"),line:field(node,"columns-line")}
        return dialog_model(st,"layout.columns",values,{count:float(values.count),gap:float(values.gap),line:values.line=="true"},node)
    }
    if (action == "comment") {
        let values = {author:field(node,"comment-author"),text:field(node,"comment-text")}
        return dialog_model(st,"review.addComment",values,values,node)
    }
    if (action == "cover") {
        let values = {title:field(node,"cover-title"),subtitle:field(node,"cover-subtitle"),author:field(node,"cover-author")}
        return dialog_model(st,"insert.coverPage",values,values,node)
    }
    if (action == "bookmark") {
        let values = {name:field(node,"bookmark-name")}
        return dialog_model(st,"insert.bookmark",values,values,node)
    }
    if (action == "symbol") {
        let values = {char:field(node,"symbol-text")}
        return dialog_model(st,"insert.symbol",values,values,node)
    }
    if (action == "shape") {
        let values = shape_values(node)
        return dialog_model(st,if (st.dialog.editing) "shape.set" else "insert.shape",values,
            {*:values,width:float(values.width),height:float(values.height),weight:float(values.weight),rotation:float(values.rotation),size:float(values.size)},node)
    }
    if (action == "stories") {
        let values = {header:field(node,"stories-header"),footer:field(node,"stories-footer"),header_align:field(node,"stories-header-align"),
            footer_align:field(node,"stories-footer-align"),size:field(node,"stories-size"),distance:field(node,"stories-distance"),different_first:field(node,"stories-first")}
        return dialog_model(st,"layout.headerFooter",values,{*:values,size:float(values.size),distance:float(values.distance),different_first:values.different_first=="true"},node)
    }
    if (action == "equation") {
        let values = {source:field(node,"equation-source"),size:field(node,"equation-size"),color:field(node,"equation-color"),display:field(node,"equation-display")}
        return dialog_model(st,if (st.dialog.editing) "equation.set" else "insert.equation",values,
            {*:values,size:float(values.size),display:values.display=="true"},node)
    }
    if (action == "wordart") {
        let values = art_values(node)
        return dialog_model(st,if (st.dialog.editing) "wordArt.set" else "insert.wordArt",values,
            {*:values,width:float(values.width),height:float(values.height),size:float(values.size),rotation:float(values.rotation)},node)
    }
    if (action=="tracking") {
        let values = {enabled:field(node,"tracking-enabled"),author:field(node,"tracking-author")}
        return dialog_model(st,"review.trackChanges",values,{*:values,enabled:values.enabled=="true"},node)
    }
    if (action == "internal_link") {
        let name = field(node,"link-bookmark")
        let values = {href:insert.bookmark_href(name),label:field(node,"link-label")}
        let target = [for (b in insert.bookmarks(st.editor.doc) where b.name==name) b][0]
        if (target==null) { return {*:st,dialog:{*:st.dialog,values:values,error:"Choose an existing bookmark."}} }
        return dialog_model(st,"insert.link",values,values,node)
    }
    if (action == "paragraph") {
        let values = {align:field(node,"paragraph-align"),lines:field(node,"paragraph-lines"),before:field(node,"paragraph-before"),
            after:field(node,"paragraph-after"),indent:field(node,"paragraph-indent"),first:field(node,"paragraph-first"),right:field(node,"paragraph-right")}
        return dialog_model(st,"para.set",values,{align:values.align,lines:float(values.lines),before:float(values.before),
            after:float(values.after),indent:float(values.indent),first:float(values.first),right:float(values.right)},node)
    }
    if (action == "picture") {
        let values = {path:field(node,"picture-path"),alt:field(node,"picture-alt"),width:field(node,"picture-width"),height:field(node,"picture-height"),rotation:field(node,"picture-rotation")}
        let editing = st.dialog.kind=="picture_settings"
        let source = if (editing) "" else storage.picture_source(trim(values.path)) ^ {
            return {*:st,dialog:{*:st.dialog,values:values,error:^.message},status:^.message}
        }
        return dialog_model(st,if (editing) "picture.size" else "insert.picture",values,
            {src:source,alt:values.alt,width:float(values.width),height:if (trim(values.height)=="") 0 else float(values.height),rotation:float(values.rotation)},node)
    }
    if (action == "link") {
        let href = trim(field(node,"link-address"))
        let label = field(node,"link-label")
        if (not (starts_with(href,"https://") or starts_with(href,"http://") or starts_with(href,"mailto:") or starts_with(href,"#"))) {
            return {*:st,dialog:{*:st.dialog,values:{href:href,label:label},error:"Enter an http, https, mailto, or fragment address."}}
        }
        return dialog_model(st,"insert.link",{href:href,label:label},{href:href,title:"",label:label},node)
    }
    let path = if (action == "overwrite") st.dialog.path else trim(field(node,"word-file-path"))
    let kind = if (action == "overwrite") st.dialog.action else st.dialog.kind
    if (kind == "open") {
        let opened = storage.read_document(path) ^ { return {*:st,status:^.message} }
        focus_document(node)
        return {*:st,editor:replaced_editor(st.editor,opened.editor),object_picks:[],file_state:opened.session,status:opened.status,dialog:null,query:"",match_index:-1}
    }
    if (kind == "save") { return saved(st,path,node,action == "overwrite") }
    if (kind == "print") {
        let target = if (action == "overwrite") st.dialog.path else trim(field(node,"print-path"))
        let values = {*:st.dialog.values,path:target}
        let failed = (message) => {*:st,dialog:{kind:"print",values:values,starts:st.dialog.starts,error:message}}
        if (lower(split(target,".")[last]) != "pdf") { return failed("Word: save the printout as a .pdf file") }
        let printed = print.document(st.editor.doc,values) ^ { return failed(^.message) }
        // composed printouts are built from the measured preview pages, so they need a finished measurement.
        let starts = st.dialog.starts
        if (print.composed(values) and starts==null) { return failed("Word: the preview is still updating; try again") }
        if (exists(target) and action != "overwrite") { return {*:st,dialog:{kind:"exists",path:target,action:"print",values:values,starts:starts}} }
        if (starts_with(target,"temp/word/")) { io.mkdir("temp/word") ^ { null } }
        let exported: any | error = if (print.composed(values)) storage.export_sheets(printed,st.file_state.envelope.title,values,starts,target)
            else storage.export_document(printed,st.file_state.envelope.title,target,not values.backgrounds)
        if (exported is error) { return failed(exported.message) }
        focus_document(node)
        return {*:st,status:"Saved " ++ target ++ " for printing",dialog:null}
    }
    if (kind == "export") {
        if (exists(path) and action != "overwrite") { return {*:st,dialog:{kind:"exists",path:path,action:"export"}} }
        if (starts_with(path,"temp/word/")) { io.mkdir("temp/word") ^ { null } }
        let exported: any | error = storage.export_document(st.editor.doc,st.file_state.envelope.title,path)
        if (exported is error) { return {*:st,status:exported.message} }
        focus_document(node)
        return {*:st,status:"Exported " ++ path,dialog:null}
    }
    return st
}
pub pn command(st,id,params,node,retain_focus=false) {
    if (starts_with(id,"dialog.")) { return dialog_command(st,slice(id,7),node) }
    // auto-opened comments follow Review instead of staying latched across ribbon changes.
    if (id == "ui.tab") { return {*:st,tab:params.tab,review:params.tab=="Review" and not st.revisions} }
    if (starts_with(id,"ui.equation") and not contains(["ui.equation","ui.equationSettings","ui.equationPreset"],id)) {
        if (id=="ui.equationCancel" or (st.read_only and id=="ui.equationClose")) {
            if (not st.read_only) focus_document(node)
            return {*:st,equation_edit:null,tab:if (id=="ui.equationClose") "Home" else st.tab}
        }
        if (st.read_only or equation.settings(image.selected(st.editor).node)==null) { return st }
        let draft = equation_edit.draft(st)
        if (id=="ui.equationEdit") {
            equation_edit.request(node)
            return {*:st,tab:"Equation",equation_edit:{*:draft,focus:params.focus or "equation-editor-source",
                selection:{start:len(draft.values.source),end:len(draft.values.source)}}}
        }
        if (id=="ui.equationDraft") {
            return {*:st,equation_edit:{*:draft,values:{*:draft.values,[params.property]:params.value},error:null}}
        }
        if (id=="ui.equationInsert") {
            let entry = equation_edit.fragment(params)
            if (entry==null) { return {*:st,status:"Choose an equation symbol or structure."} }
            // with the caret inside the equation, structures insert there instead of into the LaTeX pane.
            if (math_edit.active(st)) {
                let change = math_edit.insert(st,entry) ^ { return {*:st,status:^.message} }
                return math_commit(st,change,node)
            }
            let bounds = equation_edit.source_range(node,draft.values.source)
            let inserted = equation_catalog.insert(draft.values.source,bounds.start,bounds.end,entry)
            return equation_apply(st,node,{*:draft.values,source:inserted.source},false,{start:inserted.start,end:inserted.end})
        }
        if (id=="ui.equationUse") {
            let preset = [for (e in equation_catalog.examples where e.id==params.id) e][0]
            if (preset==null) { return st }
            return equation_apply(st,node,{*:draft.values,source:preset.source},false,{start:0,end:len(preset.source)})
        }
        if (id=="ui.equationFormat") {
            let values = {*:draft.values,*:params}
            if (equation_edit.active(st)) { return equation_apply(st,node,values) }
            return model_command(st,"equation.format",values,node)
        }
        if (contains(["ui.equationApply","ui.equationClose"],id)) {
            if (not equation_edit.active(st)) { focus_document(node); return {*:st,tab:"Home"} }
            return equation_apply(st,node,draft.values,id=="ui.equationClose")
        }
    }
    if (id=="ui.selectionPane") { return {*:st,selection_pane:not st.selection_pane} }
    if (id=="ui.shapePane") { return {*:st,shape_pane:if (params.open==true) true else not st.shape_pane} }
    if (id=="ui.shapeField") {
        if (st.read_only) { return st }
        let item = shape_format.describe(image.selected(st.editor).node)
        if (item==null) { return st }
        let value = if (item.values[symbol(params.property)] is number) (float(params.value) ^ {null}) else params.value
        let next = model_command(st,"shape.format",{values:{[params.property]:value}},node,true)
        // controlled inputs retain incomplete hex/number drafts until they become valid source values.
        return {*:next,shape_draft:{path:image.selected(next.editor).path,revision:next.editor.model_revision,
            values:{*:shape_format.draft_values(st.editor,st.shape_draft),[params.property]:params.value}}}
    }
    if (id=="ui.objectPick") {
        let picked=contains(st.object_picks,params.path)
        return {*:st,object_picks:if (picked) [for (path in st.object_picks where path!=params.path) path] else [*st.object_picks,params.path],
            editor:image.select_image(mounted(st.editor,node),params.path),tab:"Object Format"}
    }
    if (id=="ui.objectSelect") {
        focus_document(node)
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_navigation_ready")
        return {*:st,editor:image.select_image(mounted(st.editor,node),params.path),object_picks:[],tab:"Object Format",nav_target:{kind:"object"}}
    }
    if (id=="ui.objectVisibility") {
        if (st.read_only) { return st }
        let editor = image.select_image(mounted(st.editor,node),params.path)
        return model_command({*:st,editor:editor},"object.layout",{values:{hidden:not object_layout.settings(image.selected(editor).node).hidden}},node)
    }
    if (id=="ui.objectLayout") {
        let selected = image.selected(st.editor)
        if (st.read_only or selected==null) { return st }
        let p = object_layout.settings(selected.node)
        let frame = object_layout.measure(st.editor,node,st.zoom)
        return {*:st,dialog:{kind:"object_layout",values:{*:p,x:round(frame.x*100)/100,y:round(frame.y*100)/100}}}
    }
    if (id=="ui.citation" or id=="ui.sources" or id=="ui.editCitation") {
        let active = references.current(st.editor)
        if (id!="ui.sources" and st.read_only) { return st }
        if (id=="ui.editCitation" and active==null) { return {*:st,status:"Select a citation first."} }
        return {*:st,dialog:if (id!="ui.sources" and active!=null) {kind:"citation",values:active.value}
            else {kind:if (id=="ui.sources") "sources" else "citations",values:{locator:""}}}
    }
    if (id=="ui.source") {
        if (st.read_only) { return st }
        let active = references.current(st.editor)
        let s = if (params.new==true) null else references.source(st.editor.doc,params.id or active.value.source.id)
        return {*:st,dialog:{kind:"source",source_id:s.id,values:s or references.defaults,all_fields:false,
            insert:params.insert==true,locator:if (st.dialog.kind=="citations") field(node,"citation-locator") else ""}}
    }
    if (contains(["ui.sourceKind","ui.sourceCorporate","ui.sourceFields"],id) and st.dialog.kind=="source") {
        let values = source_values(node,st.dialog)
        return {*:st,dialog:{*:st.dialog,values:{*:values,
            kind:if (id=="ui.sourceKind") params.value else values.kind,
            corporate:if (id=="ui.sourceCorporate") params.value=="true" else values.corporate},
            all_fields:if (id=="ui.sourceFields") params.value==true else st.dialog.all_fields,error:null}}
    }
    if (id=="ui.insertCitation") {
        if (st.read_only) { return st }
        return dialog_model(st,"references.insertCitation",{locator:field(node,"citation-locator")},
            {id:params.id,locator:field(node,"citation-locator")},node)
    }
    if (id=="references.deleteSource" and contains(["sources","citations"],st.dialog.kind)) {
        if (st.read_only) { return st }
        let next = model_command(st,id,params,node)
        return {*:next,dialog:next.dialog or st.dialog}
    }
    if (id=="ui.tableGridlines") { return {*:st,table_gridlines:not st.table_gridlines} }
    if (id=="ui.tableSize") {
        if (st.read_only) { return st }
        // size fields commit each edit while retaining their own text caret.
        return model_command(st,"table.cellSize",{axis:params.axis,value:if (trim(string(params.value))=="") null else float(params.value)},node,true)
    }
    if (id=="ui.tableLayoutDialog") {
        if (st.read_only or tables.context(st.editor)==null) { return st }
        let p = table_layout.settings(st.editor)
        let values = if (params.kind=="table_split") {rows:p.rows,columns:if (p.rows==1 and p.columns==1) 2 else p.columns}
            else if (params.kind=="table_sort") {column:1,type:"text",descending:false,header:true}
            else if (params.kind=="table_text") {separator:"tabs"}
            else if (params.kind=="table_formula") {expression:attrs_get(tables.selected(st.editor)[0].node.attrs,'data-word-formula') or "=SUM(ABOVE)",decimals:"general"}
            else p
        return {*:st,dialog:{kind:params.kind,values:values}}
    }
    if (id=="ui.tablePen") {
        if (st.read_only) { return st }
        return {*:st,table_pen:{*:st.table_pen,[params.property]:if (params.property=="width") float(params.value) else params.value}}
    }
    if (id=="ui.tableShading") {
        if (st.read_only or model.table_context(st.editor)==null) { return st }
        return {*:st,dialog:{kind:"table_shading",values:{color:table_design.shading_value(tables.selected(st.editor)[0].node)}}}
    }
    if (id=="ui.cellBorders") {
        if (st.read_only or model.table_context(st.editor)==null) { return st }
        return {*:st,dialog:{kind:"cell_borders",values:tables.border_settings(st.editor)}}
    }
    if (contains(["ui.headerFooter","ui.pageNumber"],id)) {
        if (st.read_only) { return st }
        let p = stories.settings(st.editor.doc)
        return {*:st,dialog:{kind:"stories",values:if (id=="ui.pageNumber" and p.footer=="") {*:p,footer:"Page {PAGE} of {NUMPAGES}"} else p}}
    }
    let selected_graphic = graphics.descriptor(model.current_node(st.editor,'img').node)
    let graphic_command = graphics.command(id)
    if ((graphic_command!=null and contains([graphic_command.open,graphic_command.edit],id)) or
        (contains(["ui.objectSettings","ui.pictureSettings"],id) and selected_graphic!=null)) {
        if (st.read_only) { return st }
        let entry = graphic_command or selected_graphic
        if (entry.kind=="group") { return command(st,"ui.objectLayout",{},node) }
        let editing = id!=entry.open
        let values = entry.settings(model.current_node(st.editor,'img').node)
        if (editing and values==null) { return {*:st,status:"Select a " ++ entry.label ++ " first."} }
        return {*:st,dialog:{kind:entry.kind,editing:editing,values:if (editing) values else entry.defaults}}
    }
    if (id=="ui.emoji") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"emoji",values:{char:"😊"}}}
    }
    if (id=="ui.chooseEmoji" and st.dialog.kind=="emoji") {
        return {*:st,dialog:{*:st.dialog,values:{char:params.char},error:null}}
    }
    if (contains(["ui.chooseGraphic","ui.previewGraphic"],id) and contains(graphics.illustration_kinds,st.dialog.kind)) {
        let kind = st.dialog.kind
        let values = {*:illustration_values(node,kind,st.dialog.values),*:params}
        let preview = graphics.kind(kind).graphic(graphics.parameters(kind,values)) ^ {
            return {*:st,dialog:{*:st.dialog,values:values,error:^.message}}
        }
        return {*:st,status:"Preview updated",dialog:{*:st.dialog,values:values,error:null}}
    }
    if (id=="ui.equationPreset" and st.dialog.kind=="equation") {
        return {*:st,dialog:{*:st.dialog,error:null,values:{source:params.source,size:field(node,"equation-size"),
            color:field(node,"equation-color"),display:field(node,"equation-display")}}}
    }
    if (id=="ui.chooseArt" and st.dialog.kind=="wordart") {
        let values = art_values(node,params.style)
        return {*:st,dialog:{*:st.dialog,error:null,values:{*:st.dialog.values,*:values}}}
    }
    if (id == "ui.chooseShape") {
        if (st.dialog==null or st.dialog.kind!="shape") { return st }
        let values = shape_values(node,params.kind)
        return {*:st,dialog:{*:st.dialog,error:null,values:{*:st.dialog.values,*:values}}}
    }
    if (id == "ui.cover") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"cover",values:{title:st.file_state.envelope.title,subtitle:"Document subtitle",author:"Me"}}}
    }
    if (id == "ui.bookmarks") {
        return {*:st,dialog:{kind:"bookmarks",values:{name:insert.bookmark_name(model.current_block(st.editor).node) or ""}}}
    }
    if (id == "ui.symbol") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"symbol",values:{char:"©"}}}
    }
    if (id == "ui.chooseSymbol") {
        if (st.dialog==null or st.dialog.kind!="symbol") { return st }
        return {*:st,dialog:{*:st.dialog,values:{char:params.char},error:null}}
    }
    if (id == "ui.bookmarkText" or id == "ui.followLink") {
        let href = attrs_get(model.current_node(st.editor,'a').node.attrs,'href') or ""
        let name = if (id=="ui.bookmarkText") params.name else insert.link_bookmark(href)
        let selection = insert.bookmark_selection(st.editor.doc,name)
        if (selection==null) { return {*:st,status:"This bookmark is no longer in the document."} }
        focus_document(node)
        let target = dom.get_element_by_id(dom.root_node(node),insert.bookmark_id(name))
        if (target!=null) dom.scroll_into_view(target)
        return {*:st,dialog:null,editor:edit_set_selection(mounted(st.editor,node),selection),status:"Bookmark: " ++ name}
    }
    if (id == "ui.comments") { return {*:st,review:not st.review,revisions:false} }
    if (id=="ui.revisions") { return {*:st,revisions:not st.revisions,review:false} }
    if (id=="ui.showChanges") { return {*:st,show_changes:not st.show_changes} }
    if (id=="ui.tracking") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"tracking",values:changes.settings(st.editor.doc)}}
    }
    if (id=="ui.revisionText") {
        let selected = changes.selection(st.editor.doc,params.id)
        if (selected==null) { return {*:st,status:"This revision no longer exists."} }
        focus_document(node)
        let target = dom.query_selector(dom.root_node(node),"#edit-surface [data-word-change=\"" ++ params.id ++ "\"]")
        if (target!=null) dom.scroll_into_view(target)
        return {*:st,revisions:true,review:false,show_changes:true,editor:edit_set_selection(mounted(st.editor,node),selected)}
    }
    if (id == "ui.resolvedComments") { return {*:st,show_resolved:not st.show_resolved} }
    if (id == "ui.comment") {
        if (st.read_only) { return st }
        if (not model.can_comment(st.editor)) { return {*:st,status:"Select the text you want to comment on."} }
        return {*:st,dialog:{kind:"comment",values:{author:"Me",text:""}}}
    }
    if (id == "ui.commentText" or id == "ui.nextComment" or id == "ui.previousComment") {
        let target_id = if (id=="ui.commentText") params.id else model.next_comment_id(st.editor,st.show_resolved,id=="ui.nextComment")
        let selection = model.comment_selection(st.editor,target_id)
        if (selection==null) { return {*:st,review:true,status:"The commented text is no longer in the document."} }
        focus_document(node)
        let anchor = dom.query_selector(dom.root_node(node),"#edit-surface [data-word-comment~=\"" ++ target_id ++ "\"]")
        if (anchor!=null) dom.scroll_into_view(anchor)
        return {*:st,review:true,editor:edit_set_selection(mounted(st.editor,node),selection)}
    }
    if (id == "ui.matchCase") { return {*:st,match_case:not st.match_case,match_index:-1} }
    if (id == "ui.clearFind") { return {*:st,query:"",match_index:-1} }
    if (id == "ui.escape") {
        if (st.dialog!=null) { return dialog_command(st,"cancel",node) }
        if (st.home.painter!=null) { return {*:st,home:{*:st.home,painter:null},status:"Ready to write"} }
        return {*:st,focused:false}
    }
    if (id=="ui.editingMenu") {
        if (st.context.menu==params.menu) { focus_document(node); return {*:st,context:null} }
        let trigger = dom.closest(node,"button")
        let box = dom.bounding_box(trigger)
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_context_ready")
        return {*:st,context:{menu:params.menu,x:box.x,y:box.y+box.height,
            item_selection:st.editor.selection}}
    }
    if (id == "ui.find" or id=="ui.replace") {
        if (id=="ui.replace" and st.read_only) { return st }
        // defer focus until the navigation pane is visible and its fields have rendered.
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_search_ready")
        return {*:st,nav:true,nav_tab:"results",focused:false,search_mode:if (id=="ui.replace") "replace" else "find"}
    }
    if (id=="ui.selectParagraph") {
        if (st.read_only) { return st }
        focus_document(node)
        return {*:st,editor:model.select_paragraph(mounted(st.editor,node)),status:"Paragraph selected"}
    }
    // Format Painter keeps the caret's character formatting until the next drag selection.
    if (id=="ui.formatPainter") {
        if (st.home.painter!=null or st.read_only) { return {*:st,home:{*:st.home,painter:null},status:"Ready to write"} }
        return {*:st,home:{*:st.home,painter:model.current_marks(st.editor)},status:"Select text to apply the copied formatting"}
    }
    if (id=="ui.showMarks") { return {*:st,home:{*:st.home,marks:not st.home.marks}} }
    if (id == "view.navigationPane") { return {*:st,nav:not st.nav} }
    if (id == "view.ribbonGroupLabels") { return {*:st,show_ribbon_group_labels:not st.show_ribbon_group_labels} }
    if (id=="ui.closeNavigation") { focus_document(node); return {*:st,nav:false} }
    if (id=="ui.navigationTab") {
        if (not any([for (tab in navigation.tabs) tab.id==params.tab])) { return st }
        if (params.focus) dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_navigation_tab_ready")
        return {*:st,nav_tab:params.tab}
    }
    if (views.mode(id)!=null) {
        let next = views.activate(st,id,params.enabled)
        return if (next.read_only) {*:next,editor:tables.native_editor(st.editor)} else next
    }
    if (id == "view.focusMode") { return {*:st,focused:not st.focused} }
    if (starts_with(id,"view.zoom")) { return {*:st,zoom:if (id == "view.zoomReset") 1.0
        else min(1.5,max(0.5,st.zoom + if (id == "view.zoomIn") 0.1 else -0.1))} }
    if (id == "ui.heading") {
        let index = [for (i,heading in model.headings(st.editor.doc) where heading.path==params.path) i][0]
        focus_document(node)
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_navigation_ready")
        return {*:st,editor:model.select_heading(mounted(st.editor,node),params.path),nav_target:{kind:"heading",index:index}}
    }
    if (id=="ui.page") {
        let point = navigation.page_position(st.editor.doc,params.page)
        if (point==null) { return st }
        focus_document(node)
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_navigation_ready")
        return {*:st,editor:edit_set_selection(mounted(st.editor,node),text_selection(point,point)),
            nav_target:{kind:"page",page:params.page},status:"Page " ++ string(params.page)}
    }
    if (id=="ui.marginMode" and st.dialog.kind=="page_setup") {
        return {*:st,dialog:{*:st.dialog,error:null,values:{*:page_values(node),mirrored:params.value=="mirrored"}}}
    }
    if (contains(["ui.link","ui.table","ui.picture","ui.pictureSettings","ui.paragraph","ui.pageSetup","ui.columns","ui.margins"],id)) {
        if (st.read_only) { return st }
        if (id == "ui.link") {
            let link = model.current_node(st.editor,'a')
            return {*:st,dialog:{kind:"link",editing:link!=null,values:{href:attrs_get(link.node.attrs,'href') or "",label:doc_text(link.node) or ""}}}
        }
        if (id == "ui.table") { return {*:st,dialog:{kind:"table",values:{rows:3,cols:3,header:"true"}}} }
        if (id == "ui.paragraph") { return {*:st,dialog:{kind:"paragraph",values:model.paragraph_settings(st.editor)}} }
        if (id == "ui.pageSetup" or id=="ui.margins") { return {*:st,dialog:{kind:"page_setup",margins:id=="ui.margins",values:page.settings(st.editor.doc)}} }
        if (id == "ui.columns") { return {*:st,context:null,dialog:{kind:"columns",values:columns.settings(st.editor.doc)}} }
        let image = model.current_node(st.editor,'img')
        return {*:st,dialog:{kind:if (id == "ui.pictureSettings") "picture_settings" else "picture",
            values:{path:"",alt:attrs_get(image.node.attrs,'alt') or "",width:attrs_get(image.node.attrs,'width') or 320,
                height:attrs_get(image.node.attrs,'height') or "",rotation:transform.rotation(image.node)}}}
    }
    if (id == "file.save" or id == "file.saveAs") {
        if (st.file_state.path=="" or id == "file.saveAs") { return {*:st,dialog:{kind:"save",
            path:if (st.file_state.path=="") "temp/word/document.word.json" else st.file_state.path}} }
        return saved(st,st.file_state.path,node)
    }
    if (id == "file.export") { return {*:st,dialog:{kind:"export",path:"temp/word/document.pdf"}} }
    // print overrides live on the dialog; each change re-measures the preview's page breaks after layout.
    if (id == "file.print" or (id == "ui.printSetting" and st.dialog.kind == "print")) {
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_print_measure")
        if (id == "file.print") { return {*:st,context:null,dialog:{kind:"print",values:print.defaults(st.editor.doc),starts:null}} }
        return {*:st,dialog:{*:st.dialog,error:null,starts:null,values:{*:st.dialog.values,path:field(node,"print-path"),[params.key]:params.value}}}
    }
    if (contains(["file.new","file.sample","file.open","file.close"],id)) {
        if (session.is_dirty(st.file_state,st.editor.doc)) { return {*:st,pending:id,dialog:{kind:"unsaved"}} }
        return continued(st,id,node)
    }
    if (contains(["edit.findNext","edit.findPrevious","ui.findResult"],id)) {
        let hits = model.search(st.editor.doc,st.query,st.match_case)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let index = if (id=="ui.findResult") min(len(hits)-1,max(0,params.index))
            else if (st.match_index<0) (if (id=="edit.findNext") 0 else len(hits)-1)
            else (st.match_index + (if (id == "edit.findNext") 1 else -1) + len(hits)) % len(hits)
        focus_document(node)
        dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_navigation_ready")
        return {*:st,editor:edit_set_selection(mounted(st.editor,node),hits[index]),match_index:index,nav_target:{kind:"selection"},
            status:"Match " ++ string(index+1) ++ " of " ++ string(len(hits))}
    }
    if (st.read_only) { return st }
    if (id == "edit.replace") {
        let hits = model.search(st.editor.doc,st.query,st.match_case)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let selected = [for (hit in hits where hit == st.editor.selection) hit][0]
        return model_command({*:st,editor:edit_set_selection(st.editor,selected or hits[0])},"edit.replace",{replace:st.replace_text},node)
    }
    let payload = if (id=="object.group") {paths:st.object_picks,boxes:groups.measure(st.editor,node,st.object_picks,st.zoom)}
        else if (contains(["object.position","object.align","object.ungroup"],id)) {*:params,frame:object_layout.measure(st.editor,node,st.zoom)}
        else if (id=="object.layout" and contains(["front","behind"],params.values.wrap) and not object_layout.floating(image.selected(st.editor).node))
            {*:params,values:{*:params.values,x:object_layout.measure(st.editor,node,st.zoom).x,y:object_layout.measure(st.editor,node,st.zoom).y}}
        else if (id == "edit.replaceAll") {find:st.query,replace:st.replace_text,match_case:st.match_case}
        else if (contains(["table.distributeRows","table.distributeColumns"],id) or (id=="table.autoFit" and params.mode=="fixed"))
            {*:params,*:table_layout.measure(st.editor,node,st.zoom)} else params
    let next = model_command(st,id,payload,node,retain_focus)
    // split color buttons repeat the last color applied from their menus.
    let remembered = if (id=="format.color") "color" else if (id=="format.highlight") "highlight" else if (id=="para.shading") "shading" else null
    return if (remembered==null or not (params.color is string) or params.color=="none" or next.status!=model.spec(id).label) next
        else {*:next,home:{*:next.home,*:map([remembered,params.color])}}
}
