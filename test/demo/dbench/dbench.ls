// S12.1.3v2/S13.3.1v2: views are pure; a document-owned task holds the session.
import dom
import resources: ~~.mod_resources
import grid: lambda.ui.core.virtual_grid
import grid_view: lambda.ui.core.virtual_grid_view
import gesture: lambda.ui.core.virtual_grid_gesture
import capture: ~~.mod_splitter
import model: .mod_model
import tabs_model: .mod_tabs
import session: .mod_session
import changes: .mod_edit
import exports: .mod_export
import profiles: .mod_profiles

let BASE=url_resolve(resources.entry_uri()^,".")
let SAMPLE=slice(url_resolve(BASE,"sample.sqlite"),7)^
let CSS=input(url_resolve(BASE,"dbench.css"),'text')^
let INITIAL=model.initial("")
let INITIAL_TABS=[{id:1,state:INITIAL,worker:null,uri:SAMPLE,driver:"sqlite",sqlite_path:null,tls:"verify-full"}]

fn node(owner,id) => dom.get_element_by_id(dom.root_node(owner),id)
pn clear_profile_password(owner) any^ {
    let field=node(owner,"profile-password")^
    if (field!=null) dom.set_state(field,"value","")
    return null
}
fn text(value,limit=200) => if (value==null) "NULL" else if (value is binary) "[binary: " ++ (string(len(value)) ^ {"?"}) ++ " bytes]"
    else if (value is string and len(value)>limit) (slice(value,0,limit) ^ {"[text unavailable]"}) ++ "…" else (string(value) ^ {"[value unavailable]"})
fn value_kind(value) => if (value==null) "null" else if (value is binary) "binary" else if (value is string) "text"
    else if (value is bool) "boolean" else if (value is number) "number" else "other"
fn review_value(value) => if (value is binary) (changes.binary_preview(value) ^ {"[binary unavailable]"})
    else if (value is string and value=="") "(empty text)" else text(value,80)
fn cell(st,bounds,r,c) {
    let values=model.row_value(st,r) ^ { null }
    let cached=values!=null
    let value=if (cached) values[c] else null
    let selected=grid.contains(bounds,r,c);
    <td id:"result-" ++ string(r) ++ "-" ++ string(c),'data-row':string(r),'data-col':string(c),
        role:"gridcell",'aria-colindex':string(c+1),'aria-selected':string(selected),
        class:trim((if (selected) "selected " else "") ++ (if (r==st.row and c==st.col) "active " else "") ++
            (if (cached and value==null) "null" else "")),if (cached) text(value) else "Loading…">
}
fn row(st,r,cells) => <tr 'aria-rowindex':string(st.offset+r+1),
    <th class:"row-number",role:"rowheader",'data-row-select':string(r),string(st.offset+r+1)>; *cells>
fn column_width(st,index) => st.column_widths[string(index)] or 160
fn column_key(st) => {count:len(st.columns),sizes:st.column_widths}
fn column_axis(st) any^ => if (st.column_axis!=null and st.column_key==column_key(st)) st.column_axis else
    grid.axis(len(st.columns)^,160,[for (key,value in st.column_widths) {index:int(string(key)),size:value}])^

fn result_grid(st) any^ {
    let row_axis=grid.axis(model.row_count(st)^,27)
    let col_axis=column_axis(st)^
    let rows=grid.window(row_axis,st.viewport.y,max(0,st.viewport.height-27),2)^
    let cols=grid.window(col_axis,st.viewport.x,max(0,st.viewport.width-46),1)^
    let bounds=model.bounds(st)
    let left=cols.before>0
    let right=cols.after>0
    let body=grid_view.body(rows,cols,(r,cells) => row(st,r,cells),(r,c) => cell(st,bounds,r,c))^;
    <div id:"result-scroll",class:"grid-scroll",
        <table id:"result-grid",class:"grid",role:"grid",tabindex:"0",'aria-label':"Query results",'aria-rowcount':"-1",
            'aria-colcount':string(len(st.columns)),'data-first-row':string(rows.first),'data-last-row':string(rows.last),
            'data-first-column':string(cols.first),'data-last-column':string(cols.last),
            *:if (st.row>=rows.first and st.row<rows.last and st.col>=cols.first and st.col<cols.last)
                {'aria-activedescendant':"result-" ++ string(st.row) ++ "-" ++ string(st.col)} else {},
            style:"width:" ++ string(46+grid.extent(col_axis)^) ++ "px;",
            <colgroup <col style:"width:46px">;
                if (left) <col style:"width:" ++ string(cols.before) ++ "px"> else null;
                if (cols.first<cols.last) for (c in cols.first to (cols.last-1)) <col style:"width:" ++ string(column_width(st,c)) ++ "px"> else null;
                if (right) <col style:"width:" ++ string(cols.after) ++ "px"> else null>
            <thead <tr <th class:"corner",scope:"col","#">;
                if (left) <th class:"grid-gap",'aria-hidden':"true"> else null;
                if (cols.first<cols.last) for (c in cols.first to (cols.last-1))
                    <th role:"columnheader",'aria-colindex':string(c+1),
                        <button type:"button",'data-sort':st.columns[c][0],title:"Sort table by " ++ st.columns[c][0],
                            st.columns[c][0]>
                        <span id:"result-col-resize-" ++ string(c),class:"column-divider",role:"separator",tabindex:"0",
                            'data-column-resize':string(c),'aria-label':"Resize " ++ st.columns[c][0],
                            'aria-orientation':"vertical",'aria-valuemin':"50",'aria-valuemax':"800",
                            'aria-valuenow':string(column_width(st,c))>> else null;
                if (right) <th class:"grid-gap",'aria-hidden':"true"> else null>>
            body>>
}

view <dbench_grid> { ~.rendered }
on scroll(evt) {
    let owner=node(evt.target,"dbench")
    // Unchanged measurements do not rerender, and no visual flag needs updating.
    dom.request_frame_once(owner,"dbench_measure")
    return 'handled'
}

fn profile_panel(st) {
    let p=st.profile_form;
    <div class:"profile-backdrop",<section class:"profile-panel",role:"dialog",'aria-label':"Connection profile",
        <h2 "Connection profile">
        <label for:"profile-file","Profile file"> <input id:"profile-file",value:st.profile_path,placeholder:"Path to a JSON profile">
        <div class:"profile-actions",<button id:"profile-load",type:"button","Load">
            <button id:"profile-save",type:"button","Save as new">>
        <label for:"profile-name","Profile name"> <input id:"profile-name",value:p.name>
        <label for:"profile-engine","Database engine">
        <select id:"profile-engine",for (engine in ["sqlite","mysql","postgresql"]) <option value:engine,
            *:if (p.engine==engine) {selected:""} else {},engine>>
        if (p.engine=="sqlite") <label for:"profile-path","SQLite file"> else <label for:"profile-host","Host">;
        if (p.engine=="sqlite") <input id:"profile-path",value:p.path> else <input id:"profile-host",value:p.host>;
        if (p.engine!="sqlite") <div class:"profile-network",
            <label for:"profile-port","Port"> <input id:"profile-port",value:p.port>
            <label for:"profile-database","Database"> <input id:"profile-database",value:p.database>
            <label for:"profile-user","User"> <input id:"profile-user",value:p.user>
            <label for:"profile-tls","TLS security"> <select id:"profile-tls",
                for (mode in ["verify-full","require","disable"]) <option value:mode,*:if (p.tls==mode) {selected:""} else {},mode>>
            <label for:"profile-ca","CA certificate (optional)"> <input id:"profile-ca",value:p.ca>
            <label for:"profile-password","Password (not saved)"> <input id:"profile-password",type:"password",autocomplete:"off">> else null;
        <span class:"hint","Profiles contain connection details but never passwords. Saving creates a new file.">
        if (st.error!=null) <span class:"error",st.error> else null;
        <div class:"profile-actions",<button id:"profile-connect",type:"button","Connect">
            <button id:"profile-close",type:"button","Close">>>>
}

fn workspace(st,tabs,active_tab) any^ {
    let names=[for (name in model.objects(st.schema)^ where contains(lower(name),lower(trim(st.object_filter)))^) name]
    let fields=if (st.table==null) [] else model.table_columns(st.schema,st.table);
    <body id:"dbench",'data-connected':string(st.connected),'data-mode':st.mode,'data-table':st.table or "",
        'data-rows':string(model.row_count(st)^),'data-offset':string(st.offset),'data-generation':string(st.generation),'data-loading':string(st.loading),
        'data-token':string(st.token),'data-window-token':string(st.window_token),
        'data-editable-sqlite':string(st.sqlite_path!=null),'data-active-tab':string(active_tab),
        <header class:"top",<span class:"brand","DBench"> <span class:"top-note","Database workbench · Use read-only database credentials">;
            <button id:"sample",type:"button","Open sample">>
        <div class:"shell",
            <aside id:"sidebar",class:"sidebar",style:"width:" ++ string(st.sidebar_width) ++ "px;",
                <div class:"connection",
                    <label for:"driver","Database engine">;
                    <select id:"driver",'aria-label':"Database engine",*:if (st.applying or st.write_preparing) {disabled:""} else {},
                        for (engine in ["sqlite","mysql","postgresql"]) <option value:engine,
                            *:if (engine==st.engine) {selected:""} else {},engine>>
                    <label for:"target",if (st.engine=="sqlite") "SQLite file" else "Connection URI">;
                    <input id:"target",type:if (st.engine=="sqlite") "text" else "password",value:st.target,
                        autocomplete:"off",*:if (st.applying or st.write_preparing) {disabled:""} else {},
                        'aria-label':if (st.engine=="sqlite") "SQLite file" else "Connection URI">
                    if (st.engine!="sqlite") <label for:"tls","TLS security"> else null;
                    if (st.engine!="sqlite") <select id:"tls",'aria-label':"TLS security",*:if (st.applying or st.write_preparing) {disabled:""} else {},
                        for (mode in ["verify-full","require","disable"]) <option value:mode,
                            *:if (mode==st.tls) {selected:""} else {},mode>> else null;
                    <span class:"hint",if (st.engine=="sqlite") "Open an existing file or use the bundled sample." else
                        "Paste a connection URI, including TLS options. Credentials are not saved.">
                    <div class:"connection-actions",
                        <button id:"connect",type:"button",*:if (st.connecting) {disabled:""} else {},"Connect">
                        <button id:"disconnect",type:"button",*:if (not st.connected and not st.connecting) {disabled:""} else {},"Disconnect">
                        <button id:"profile-open",type:"button","Profiles">>>
                <nav class:"navigator",'aria-label':"Database objects",<h2 "Tables and views">;
                    <input id:"object-filter",value:st.object_filter,placeholder:"Filter objects…",'aria-label':"Filter database objects">
                    if (len(names)==0) <span class:"hint",if (st.connected) "No matching objects." else "Connect to browse objects."> else
                        for (name in names) <button type:"button",class:if (name==st.table) "object current" else "object",'data-table':name,
                            <span name> <span class:"object-kind",model.table_kind(st.schema,name)>>>>
            <div id:"sidebar-divider",class:"pane-divider vertical",role:"separator",tabindex:"0",
                'data-pane-resize':"sidebar",'aria-label':"Resize navigator",'aria-orientation':"vertical",
                'aria-valuemin':"180",'aria-valuemax':"500",'aria-valuenow':string(st.sidebar_width)>
            <main class:"main",
                <div class:"tabs",role:"tablist",'aria-label':"SQL workspaces",
                    for (tab in tabs) <span class:if (tab.id==active_tab) "tab active" else "tab",
                        <button id:"tab-" ++ string(tab.id),type:"button",role:"tab",'data-tab':string(tab.id),
                            tabindex:if (tab.id==active_tab) 0 else -1,
                            'aria-selected':string(tab.id==active_tab),"SQL " ++ string(tab.id)>
                        if (len(tabs)>1) <button type:"button",class:"tab-close",'data-tab-close':string(tab.id),
                            'aria-label':"Close SQL " ++ string(tab.id),"×"> else null>;
                    <button id:"tab-new",type:"button",*:if (len(tabs)>=tabs_model.LIMIT) {disabled:""} else {},"+ New SQL">>
                <div class:"heading",<h1 if (st.mode=="query") "SQL result" else st.table or "Choose a table">;
                    <button id:"details",type:"button",*:if (st.table==null or st.mode!="table" or not st.connected) {disabled:""} else {},
                        if (st.show_details) "Hide details" else "Object details">
                    <button id:"refresh",type:"button",*:if (not st.connected) {disabled:""} else {},"Refresh schema">>
                if (st.show_details and st.mode=="table" and st.table!=null) <section class:"details",'aria-label':"Object details",
                    <h2 "Columns">;
                    for (field in fields) <div class:"detail-line",<strong field.name>;
                        " " ++ (field.declared or "") ++ (if (field.primary>0) " · primary key " ++ string(field.primary) else "") ++
                        (if (field.nullable) " · nullable" else " · not null")>;
                    <h2 "Indexes and foreign keys">;
                    for (detail in st.details where detail[0]==st.table)
                        if (detail[1]=="index") <div class:"detail-line","Index " ++ detail[2] ++ " · " ++ (detail[3] or "expression") ++
                            (if (detail[4]) " · unique" else "")>
                        else <div class:"detail-line","Foreign key " ++ detail[2] ++ " → ";
                            <button type:"button",'data-table':detail[3],detail[3] ++ "." ++ (detail[4] or "")>>> else null;
                <div class:"editor",<textarea id:"sql",'aria-label':"SQL editor",style:"height:" ++ string(st.editor_height) ++ "px;",
                    *:if (st.applying or st.write_preparing) {disabled:""} else {},st.query>;
                    <div class:"actions",
                        <div class:"action-row",<button id:"run",type:"button",*:if (not st.connected) {disabled:""} else {},"Run SQL">
                            <button id:"cancel",type:"button",*:if (not st.connected and not st.connecting) {disabled:""} else {},"Cancel">>
                        <div class:"action-row",<button id:"history",type:"button","History">
                            <button id:"save-sql",type:"button","Save SQL">>>>
                <div id:"editor-divider",class:"pane-divider horizontal",role:"separator",tabindex:"0",
                    'data-pane-resize':"editor",'aria-label':"Resize SQL editor",'aria-orientation':"horizontal",
                    'aria-valuemin':"62",'aria-valuemax':"340",'aria-valuenow':string(st.editor_height)>
                <div class:"parameters",<label for:"sql-params","Parameters (JSON array)">;
                    <input id:"sql-params",value:st.params,'aria-label':"SQL parameters",placeholder:"[]",
                        *:if (st.applying or st.write_preparing) {disabled:""} else {}>>
                if (st.show_history) <section class:"history",'aria-label':"Query history",
                    <div class:"hint","This session keeps SQL text, including any literals; parameter values are not recorded.">
                    for (i,query in st.history) <button type:"button",'data-history':string(i),text(query,120)>;
                    <button id:"history-clear",type:"button",*:if (len(st.history)==0) {disabled:""} else {},"Clear history">> else null;
                <div class:"filter",<span "Filter">;
                    <select id:"filter-column",'aria-label':"Filter column",
                        for (i,field in fields) <option value:field.name,
                            *:if (field.name==st.filter_input_column or (st.filter_input_column==null and i==0)) {selected:""} else {},field.name>>
                    <input id:"filter-value",value:st.filter_input_value,'aria-label':"Filter value",placeholder:"Equals…">
                    <button id:"apply-filter",type:"button",*:if (st.table==null or not st.connected) {disabled:""} else {},"Apply">
                    <button id:"clear-filter",type:"button",*:if (st.table==null or not st.connected) {disabled:""} else {},"Clear">>
                <div class:"edit-actions",
                    <button id:"edit-cell",type:"button",*:if (not st.connected or st.applying or st.sqlite_path==null or
                        st.mode!="table" or st.table==null or len(st.rows)==0 or model.table_kind(st.schema,st.table)!="table") {disabled:""} else {},
                        "Stage value edit">
                    <button id:"delete-row",type:"button",*:if (not st.connected or st.applying or st.sqlite_path==null or
                        st.mode!="table" or st.table==null or len(st.rows)==0 or model.table_kind(st.schema,st.table)!="table") {disabled:""} else {},
                        "Stage row deletion">
                    <button id:"insert-row",type:"button",*:if (not st.connected or st.applying or st.sqlite_path==null or
                        st.mode!="table" or st.table==null or model.table_kind(st.schema,st.table)!="table") {disabled:""} else {},
                        "Stage insert">
                    <button id:"write-review",type:"button",*:if (not st.connected or st.applying or st.sqlite_path==null or
                        len(st.pending)>0 or st.write_preparing) {disabled:""} else {},"Review SQL write">
                    <button id:"apply-changes",type:"button",*:if (len(st.pending)==0 or st.applying or not st.connected or
                        st.mode!="table" or st.loading) {disabled:""} else {},
                        "Apply " ++ string(len(st.pending)) ++ (if (len(st.pending)==1) " change" else " changes")>
                    <button id:"revert-changes",type:"button",*:if (len(st.pending)==0 or st.applying) {disabled:""} else {},"Revert">
                    if (len(st.pending)>0) <span class:"hint","Target: " ++ st.sqlite_path> else null>
                if (st.show_editor) <div class:"edit-panel",
                    <label for:"edit-value","New value for " ++ st.edit_base.table ++ "." ++ st.edit_base.fields[st.edit_base.column].name>;
                    <select id:"edit-mode",'aria-label':"Value format",
                        for (mode in ["text","json","hex"]) <option value:mode,*:if (mode==st.edit_mode) {selected:""} else {},
                            if (mode=="text") "Text" else if (mode=="json") "JSON scalar" else "Hex bytes">>
                    <input id:"edit-value",value:st.edit_value,'aria-label':"New value">
                    <span class:"hint","SQLite applies column affinity.">
                    <button id:"stage-value",type:"button","Stage"> <button id:"cancel-edit",type:"button","Cancel">> else null;
                if (st.show_insert) <div class:"edit-panel",
                    <label for:"insert-values","New " ++ st.insert_base.table ++ " row (JSON object)">;
                    <input id:"insert-values",value:st.insert_text,'aria-label':"Insert row values">
                    <button id:"stage-insert",type:"button","Stage"> <button id:"cancel-insert",type:"button","Cancel">> else null;
                if (st.show_write) <section class:"write-review",'aria-label':"Review SQL write",
                    <strong "Write target: " ++ st.sqlite_path>
                    <div "Statement"> <pre id:"write-statement",st.write_plan.sql>
                    <div "Bound parameters"> <pre id:"write-parameters",st.write_plan.params_text>
                    <button id:"write-confirm",type:"button",*:if (st.write_preparing) {disabled:""} else {},"Execute write">
                    <button id:"write-cancel",type:"button",*:if (st.write_preparing) {disabled:""} else {},"Cancel review">> else null;
                if (len(st.pending)>0) <div class:"pending",'aria-label':"Staged changes",
                    if (any([for (change in st.pending) change.action=="delete"]))
                        <div class:"hint","Deletes may cascade or fire database triggers."> else null;
                    for (change in st.pending) <div <strong if (change.action=="delete") "Delete from " ++ change.table else
                            if (change.action=="insert") "Insert into " ++ change.table else "Update " ++ change.table>;
                        if (change.action=="delete") " · key " ++ join([for (item in change.key) text(item,80)],", ")
                            else if (change.action=="insert") " · values " ++ text(format(change.statement_params,'json') ^ {"[unavailable]"},160)
                            else " · " ++ join([for (entry in change.edits) entry.column ++ ": " ++ review_value(entry.old) ++ " → " ++ review_value(entry.value)],"; ");
                        <code change.statement_sql>>> else null;
                if (st.show_export) <div class:"export-panel",
                    <label for:"export-path","Create new file">;
                    <input id:"export-path",value:st.export_path,'aria-label':"Export file path",placeholder:"Path to new file">
                    if (st.export_kind=="sql") <span "SQL script"> else <select id:"export-kind",'aria-label':"Export format",
                        for (kind in ["csv","json"]) <option value:kind,*:if (kind==st.export_kind) {selected:""} else {},kind>>;
                    if (st.export_kind!="sql") <select id:"export-scope",'aria-label':"Export scope",
                        for (scope in (if (st.stream_available and st.mode=="query") ["loaded","selection","complete"]
                                else if (st.mode=="table" and st.table!=null and st.connected) ["loaded","selection","table"]
                                else ["loaded","selection"]))
                            <option value:scope,*:if (scope==st.export_scope) {selected:""} else {},
                                if (scope=="table") "whole table (saved values)" else scope>>
                    else null;
                    <button id:"export-save",type:"button","Create export"> <button id:"export-cancel",type:"button","Cancel">> else null;
                apply(<dbench_grid rendered:result_grid(st)^>);
                if (st.show_value and model.row_cached(st,st.row)^ and len(st.columns)>0) <section id:"value-inspector",class:"value-inspector",'aria-label':"Selected value",
                    <div class:"value-heading",<strong "Row " ++ string(st.offset+st.row+1) ++ " · " ++ st.columns[st.col][0]>;
                        <span class:"hint","Declared " ++ st.columns[st.col][1] ++ " · value " ++ value_kind(model.row_value(st,st.row)^[st.col])>
                        <span class:"grow">
                        <button id:"value-prev",type:"button",*:if (st.col==0) {disabled:""} else {},"Previous column">
                        <button id:"value-next",type:"button",*:if (st.col>=len(st.columns)-1) {disabled:""} else {},"Next column">>
                    <pre id:"value-content",if (model.row_value(st,st.row)^[st.col]=="") "(empty text)" else text(model.row_value(st,st.row)^[st.col],4096)>> else null;
                <div class:"paging",<button id:"previous",type:"button",*:if (st.mode!="table" or st.offset==0 or not st.connected or st.loading) {disabled:""} else {},"Previous">
                    <button id:"next",type:"button",*:if (st.mode!="table" or st.done or not st.connected or st.loading) {disabled:""} else {},"Next">
                    <button id:"more",type:"button",*:if (st.mode!="query" or st.done or not st.connected or st.loading) {disabled:""} else {},"Fetch more">
                    <button id:"copy",type:"button",*:if (len(st.rows)==0) {disabled:""} else {},"Copy selection">
                    <button id:"inspect-value",type:"button",*:if (len(st.rows)==0) {disabled:""} else {},if (st.show_value) "Hide value" else "Inspect value">
                    <button id:"export",type:"button",*:if ((len(st.rows)==0 and not st.stream_available and
                        (st.mode!="table" or st.table==null or not st.connected)) or st.exporting) {disabled:""} else {},"Export results">
                    <span class:"grow"> <span id:"row-count",string(model.row_count(st)^) ++ " rows loaded">>>>
        <footer class:"status",<span id:"status",role:"status",st.status>;
            if (st.error!=null) <span class:"error"," · " ++ st.error> else null>;
        if (st.show_profile) profile_panel(st) else null>
}

pn report(root,message) {
    let owner=dom.get_element_by_id(root,"dbench")
    if (owner!=null) dom.dispatch(owner,{type:"dbench_result",bubbles:false,detail:message})
}
pn connect(root,uri,driver,generation,tab) {
    return start(session.run,[uri,driver,generation,pn (message) => { report(root,{*:message,tab:tab}) }],{owner:'document'})
}
pn apply_changes(root,path,plans,generation,tab) {
    let outcome: any^=changes.apply_staged(path,plans)
    if (outcome is error) report(root,{type:"apply_failed",generation:generation,tab:tab,message:string(outcome.message)})
    else report(root,{type:"applied",generation:generation,tab:tab})
}
pn apply_sql(root,path,statement,params,generation,tab) {
    let outcome: any^=changes.execute_sql(path,statement,params)
    if (outcome is error) report(root,{type:"sql_write_failed",generation:generation,tab:tab,message:string(outcome.message)})
    else report(root,{type:"sql_written",generation:generation,tab:tab})
}
pn write_export(path,snapshot,scope,kind) any^ {
    let content=if (kind=="sql") {
        if (4*len(snapshot.query)^>1024*1024) raise error("DBench: SQL script exceeds the size limit")
        else snapshot.query
    } else {
        let selected=exports.data(snapshot,scope)^;
        exports.encode(selected.columns,selected.rows,kind)^
    }
    io.create_file(path,content)^
    return null
}
pn export_file(root,path,snapshot,scope,kind,generation,tab) {
    let outcome: any^=write_export(path,snapshot,scope,kind)
    if (outcome is error) report(root,{type:"export_failed",generation:generation,tab:tab,message:string(outcome.message)})
    else report(root,{type:"exported",generation:generation,tab:tab,path:path})
}

pn profile_operation(operation,path,form) any^ {
    if (operation=="save") {
        let encoded=profiles.encode(profiles.from_form(form)^)^
        io.create_file(path,encoded)^
        return null
    } else {
        let content=io.read(path)^
        if (len(content)>65536) raise error("DBench: profile is too large")
        return profiles.decode(content)^
    }
}
pn profile_io(root,operation,path,form,generation,tab) {
    let outcome: any^=profile_operation(operation,path,form)
    if (outcome is error) report(root,{type:"profile_failed",generation:generation,tab:tab,message:string(outcome.message)})
    else report(root,{type:if (operation=="save") "profile_saved" else "profile_loaded",
        generation:generation,tab:tab,path:path,profile:outcome})
}
pn query_input(owner,current,use_saved=false) map^ {
    let field=node(owner,"sql")^
    let script=dom.get_state(field,"value") or ""
    let selection=if (use_saved and current.sql_range!=null and current.sql_range.script==script) current.sql_range else
        {start:dom.tc_selection_start(field),end:dom.tc_selection_end(field)}
    let from=selection.start
    let to=selection.end
    let query=if (to>from) slice(script,from,to)^ else script
    let text_params=dom.get_state(node(owner,"sql-params"),"value") or "[]"
    if (len(text_params)^>65536) { raise error("SQL parameters exceed the size limit.") }
    let params: any^=parse(text_params,'json')
    if (params is error or not (params is array)) { raise error("Enter SQL parameters as a JSON array.") }
    if (4*len(query)^>1024*1024) { raise error("SQL statement exceeds the size limit.") }
    return {script:script,sql:query,params:params,params_text:text_params}
}
pn run_query(owner,current,worker,use_saved=false) map^ {
    let input: any^=query_input(owner,current,use_saved)
    if (input is error) { return {*:current,error:string(input.message)} }
    let token=current.token+1
    send(worker,{op:"query",token:token,sql:input.sql,params:input.params,limit:256})^
    return {*:current,query:input.script,sql_range:null,history:model.remember(current.history,input.sql),mode:"query",token:token,rows:[],columns:[],column_widths:{},bytes:0,offset:0,
        spooled:false,total_rows:0,row_base:0,window_token:0,window_pending:null,copy_ready:null,
        row:0,col:0,anchor:{row:0,col:0},done:true,loading:true,stream_available:false,status:"Running SQL…",error:null}
}
pn show_table(st,worker,table,offset,cursor=null) map^ {
    let request=model.table_request(st,table,offset,cursor)^
    let token=st.token+1
    send(worker,{op:"page",token:token,sql:request.sql,params:request.params,limit:256,offset:offset})^
    return {*:st,table:table,mode:"table",token:token,offset:offset,key_ordered:request.key_ordered,keyset:request.keyset,
        page_cursors:if (offset==0) map([]) else st.page_cursors,rows:[],columns:[],spooled:false,total_rows:0,row_base:0,window_pending:null,
        column_widths:if (st.table==table and st.mode=="table") st.column_widths else map([]),bytes:0,done:true,loading:true,stream_available:false,row:0,col:0,anchor:{row:0,col:0},
        viewport:{*:st.viewport,x:0.0,y:0.0},status:"Loading table…",error:null}
}

// A closed session loses its spool; retain only the cached rows with local indices.
fn detach_spool(st) map^ => if (not st.spooled) st else {
    let end_index=max(0,len(st.rows)-1)
    let selected=max(0,min(end_index,st.row-st.row_base))
    let anchor=max(0,min(end_index,st.anchor.row-st.row_base));
    {*:st,spooled:false,total_rows:0,offset:st.row_base,row_base:0,row:selected,
        anchor:{row:anchor,col:st.anchor.col},window_pending:null,copy_ready:null,show_value:false,
        viewport:{*:st.viewport,y:0.0}}
}

// The same session transitions apply to active and background tabs; only the
// active tab performs DOM updates after accepting a reply.
pn session_result(current,task,message) map^ {
    if (message.generation!=current.generation) { return current }
    if (message.type=="connected") {
        let next={*:current,connected:true,connecting:false,schema:message.schema,status:"Connected · read-only requested",error:null}
        let names=model.objects(message.schema)^
        return if (len(names)>0) show_table(next,task,names[0],0) ^ {{*:next,error:string(^.message)}} else next
    }
    if (message.type=="schema" and message.token==current.token) {
        let names=model.objects(message.schema)^
        let selected=if (current.table in names) current.table else if (len(names)>0) names[0] else null
        let next={*:current,schema:message.schema,loading:false,error:null}
        if (selected==null) { return {*:next,table:null,rows:[],columns:[],done:true,status:"Schema refreshed"} }
        let base=if (selected==current.table) next else {*:next,sort_column:null,filter_column:null,filter_value:"",
            filter_input_column:null,filter_input_value:""}
        return show_table(base,task,selected,0) ^ {{*:base,error:string(^.message)}}
    }
    if (message.type=="batch" and message.token==current.token) {
        let accepted=model.accept_batch(current,message)^
        let next={*:accepted,stream_available:if (message.op=="query" or message.op=="more") accepted.error==null else false,
            status:if (message.op=="query" and message.spool_error!=null) "Spool unavailable · result cache is limited" else accepted.status}
        if (next.error!=null) send(task,{op:"finish"}) ^ {null}
        return if (message.op=="more") next else {*:next,viewport:{*:next.viewport,x:0.0,y:0.0}}
    }
    if (message.type=="window" and message.token==current.token and message.window_token==current.window_token) {
        return {*:current,rows:message.rows,row_base:message.first,bytes:model.result_size(message.rows),window_pending:null}
    }
    if (message.type=="window_failed" and message.token==current.token and message.window_token==current.window_token) {
        return {*:current,status:"Could not load result rows",error:message.message}
    }
    if (message.type=="copy_ready" and message.token==current.token and message.copy_token==current.copy_token) {
        return {*:current,copy_ready:message.text,status:"Selection ready to copy",error:null}
    }
    if (message.type=="copy_failed" and message.token==current.token and message.copy_token==current.copy_token) {
        return {*:current,status:"Could not copy selection",error:message.message}
    }
    if (message.type=="details") { return {*:current,details:message.details,status:"Object details loaded",error:null} }
    if (message.type=="details_failed") { return {*:current,show_details:false,status:"Could not load object details",error:message.message} }
    if (message.type=="failed" and message.token==current.token) {
        return {*:current,status:"Database request failed",error:message.message,done:true,loading:false,stream_available:false}
    }
    if (message.type=="disconnected") {
        return {*:detach_spool(current)^,connected:false,connecting:false,status:"Connection closed",error:message.message,done:true,loading:false,
            stream_available:false,show_write:false,write_preparing:false,write_plan:null}
    }
    return current
}

fn session_message(message) bool^ => message.type in ["connected","schema","batch","window","window_failed","copy_ready","copy_failed","details","details_failed","failed","disconnected"]

pn reveal(owner,st) map^ {
    let port=node(owner,"result-scroll")^
    let metrics=dom.scroll_metrics(port)
    let scroll=dom.scroll_state(port)
    let rows=grid.axis(model.row_count(st)^,27)
    let columns=column_axis(st)^
    let x=grid.reveal(columns,st.col,float(scroll.x),max(0,float(metrics.client_width)-46))^
    let y=grid.reveal(rows,st.row,float(scroll.y),max(0,float(metrics.client_height)-27))^
    dom.set_scroll_state(port,x,y)
    return {*:st,viewport:{x:x,y:y,width:float(metrics.client_width),height:float(metrics.client_height)}}
}

pn copy_selection(st,worker) map^ {
    if (st.mode=="query" and st.spooled) {
        let b=model.bounds(st)
        if (((b.bottom-b.top+1)*(b.right-b.left+1)>100000)^) { return {*:st,error:"Selection is too large to copy"} }
        let token=st.copy_token+1
        send(worker,{op:"copy",token:st.token,copy_token:token,top:b.top,bottom:b.bottom,left:b.left,right:b.right})^
        return {*:st,copy_token:token,copy_ready:null,status:"Copying selection…",error:null}
    }
    let copied: any^=model.copy_text(st)
    if (copied is error) { return {*:st,error:string(copied.message)} }
    dom.clipboard_write(copied)
    return {*:st,status:"Selection copied",error:null}
}
fn stage_change(current,proposed,bytes) map^ {
    let matches=[for (i,pending in current.pending where pending.table==proposed.table and pending.key==proposed.key) i]
    if (len(matches)>0) {
        let index=matches[0]
        let merged: any^=changes.merge_updates(current.pending[index],proposed)
        if (merged is error) {*:current,error:string(merged.message)}
        else {
            let added=model.result_size([[proposed.edits[0].value]])
            if (current.pending_bytes+added>16*1024*1024) {*:current,error:"Staged changes exceed the size limit."}
            else {*:current,pending:[for (i,pending in current.pending) if (i==index) merged else pending],
                pending_bytes:current.pending_bytes+added,show_editor:false,show_insert:false,status:"Change staged for review",error:null}
        }
    } else if (len(current.pending)>=32 or bytes>16*1024*1024 or current.pending_bytes+bytes>16*1024*1024)
        {*:current,error:"Staged changes exceed the size limit."}
    else {*:current,pending:[*current.pending,proposed],pending_bytes:current.pending_bytes+bytes,
        show_editor:false,show_insert:false,status:"Change staged for review",error:null}
}
fn resized(current,axis,value,index=null) map^ => if (axis=="sidebar") {*:current,sidebar_width:value}
    else if (axis=="column") {*:current,column_widths:{*:current.column_widths,*:map([string(index),value])}} else {*:current,editor_height:value}
fn resize_limits(axis) => if (axis=="sidebar") {min:180,max:500} else if (axis=="column") {min:50,max:800} else {min:62,max:340}
pn resize_begin(current,divider,evt,axis,index=null) map^ {
    let size=if (axis=="sidebar") current.sidebar_width else if (axis=="column") column_width(current,index) else current.editor_height
    let position=if (axis=="editor") evt.clientY else evt.clientX
    let target=if (axis=="column") node(evt.target,"result-scroll")^ else divider
    let focus=if (axis=="column") node(evt.target,"result-grid")^ else divider
    let captured=capture.capture(target,evt,focus)
    return {*:current,selection_click:axis=="column",resize_drag:{*:captured,axis:axis,index:index,x:position,width:size}}
}
fn resize_move(current,x,y) any^ {
    let active=current.resize_drag
    let point=if (active.axis=="editor") y else x
    let limits=resize_limits(active.axis);
    resized(current,active.axis,max(limits.min,min(limits.max,capture.pointer_width(active,point))),active.index)^
}
pn resize_finish(owner,current,cancelled=false) map^ {
    let active=current.resize_drag
    let divider=node(owner,if (active.axis=="sidebar") "sidebar-divider" else if (active.axis=="column") "result-scroll" else "editor-divider")
    capture.release(divider,active)
    return {*:(if (cancelled) resized(current,active.axis,active.width,active.index)^ else current),resize_drag:null}
}

pn drag_move(owner,current,x,y,final=false,tick=false) any^ {
    let root=node(owner,"dbench")^
    let axes=if (tick) {rows:grid.axis(model.row_count(current)^,27),columns:column_axis(current)^} else null
    let update=gesture.move(root,node(owner,"result-scroll")^,current.selection_drag,x,y,final,tick,axes,
        46,27,"#result-grid td[data-row][data-col]","dbench_drag_scroll")^
    let next={*:current,selection_drag:update.gesture}
    return if (update.hit==null) next else model.select(next,update.hit.row,update.hit.col,true)^
}
pn drag_finish(owner,current,cancelled=false) any^ {
    let active=current.selection_drag
    capture.release(node(owner,"result-scroll")^,active)
    let next={*:current,selection_drag:null}
    return if (cancelled) {*:next,row:active.previous.row,col:active.previous.col,anchor:active.previous.anchor} else next
}

edit <dbench_app> () any^ state st:INITIAL, worker:null, writer:null, tabs:INITIAL_TABS, active_tab:1, next_tab:2 { workspace(st,tabs,active_tab)^ }
on load(evt) {
    if (st.generation!=0) { return 'pass' }
    let generation=st.generation+1
    worker=connect(dom.root_node(evt.target),SAMPLE,"sqlite",generation,active_tab)
    st={*:st,generation:generation,connecting:true,status:"Opening bundled SQLite sample…"}
    tabs=tabs_model.update(tabs,active_tab,{worker:worker,state:st})^
    return 'handled'
}
on render(evt) {
    if (evt.event_phase==2) {
        let owner=node(evt.target,"dbench")
        dom.watch_window(owner,"resize",true)
        dom.request_frame_once(owner,"dbench_measure")
    }
    return 'pass'
}
on resize(evt) { dom.request_frame_once(node(evt.target,"dbench"),"dbench_measure"); return 'pass' }
on dbench_measure(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    let owner=evt.target
    let port=node(owner,"result-scroll")^
    let metrics=dom.scroll_metrics(port)
    let position=dom.scroll_state(port)
    let next={x:float(position.x),y:float(position.y),width:float(metrics.client_width),height:float(metrics.client_height)}
    let key=column_key(st)
    let columns=column_axis(st)^
    let rows=grid.axis(model.row_count(st)^,27)
    let before={rows:grid.window(rows,st.viewport.y,max(0,st.viewport.height-27),2)^,
        columns:grid.window(columns,st.viewport.x,max(0,st.viewport.width-46),1)^}
    let visible={rows:grid.window(rows,next.y,max(0,next.height-27))^,
        columns:grid.window(columns,next.x,max(0,next.width-46))^}
    if ((not grid.covers(before.rows,visible.rows) or not grid.covers(before.columns,visible.columns) or
            next.width!=st.viewport.width or next.height!=st.viewport.height or
            st.column_key!=key or st.column_axis==null)^) {
        st={*:st,viewport:next,column_key:key,column_axis:columns}
    }
    if (st.spooled and st.mode=="query" and not st.loading and st.total_rows>0) {
        let visible=grid.window(grid.axis(st.total_rows,27),next.y,max(0,next.height-27),2)^
        let covered=visible.last<=visible.first or
            (model.row_cached(st,visible.first)^ and model.row_cached(st,visible.last-1)^)
        // Returning to a cached viewport invalidates any older in-flight window.
        if (covered and st.window_pending!=null) {
            st={*:st,window_token:st.window_token+1,window_pending:null}
        } else if (not covered) {
            let count=min(st.total_rows,min(model.MAX_ROWS,max(256,visible.last-visible.first+128)))
            let first=min(max(0,visible.first-64),st.total_rows-count)
            let wanted={first:first,count:count}
            if (wanted!=st.window_pending) {
                let token=st.window_token+1
                send(worker,{op:"window",token:st.token,window_token:token,first:first,count:count})^
                st={*:st,window_token:token,window_pending:wanted}
            }
        }
    }
    return 'handled'
}
on dbench_tab_restore(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    dom.set_state(node(evt.target,"sql")^,"value",st.query)
    dom.set_state(node(evt.target,"sql-params")^,"value",st.params)
    dom.set_state(node(evt.target,"target")^,"value",st.target)
    dom.set_state(node(evt.target,"object-filter")^,"value",st.object_filter)
    dom.set_state(node(evt.target,"filter-value")^,"value",st.filter_input_value)
    let fields=if (st.table==null) [] else model.table_columns(st.schema,st.table)
    let selected=[for (i,field in fields where field.name==st.filter_input_column) i]
    if ((len(fields)>0)^) dom.set_selected_index(node(evt.target,"filter-column")^,if (len(selected)>0) selected[0] else 0)
    dom.set_selected_index(node(evt.target,"driver")^,if (st.engine=="sqlite") 0 else if (st.engine=="mysql") 1 else 2)
    if (st.engine!="sqlite") dom.set_selected_index(node(evt.target,"tls")^,if (st.tls=="verify-full") 0 else if (st.tls=="require") 1 else 2)
    dom.set_scroll_state(node(evt.target,"result-scroll")^,st.viewport.x,st.viewport.y)
    if (st.copy_ready!=null) {
        dom.clipboard_write(st.copy_ready)
        st={*:st,copy_ready:null,status:"Selection copied"}
    }
    dom.request_frame_once(evt.target,"dbench_measure")
    return 'handled'
}
on dbench_edit_ready(evt) {
    if (evt.event_phase!=2 or not st.show_editor) { return 'pass' }
    let control=node(evt.target,"edit-mode")^
    let index=if (st.edit_mode=="text") 0 else if (st.edit_mode=="json") 1 else 2
    if (dom.selected_index(control)!=index) dom.set_selected_index(control,index)
    return 'handled'
}
on dbench_profile_ready(evt) {
    if (evt.event_phase!=2 or not st.show_profile) { return 'pass' }
    let form=st.profile_form
    dom.set_selected_index(node(evt.target,"profile-engine")^,
        if (form.engine=="sqlite") 0 else if (form.engine=="mysql") 1 else 2)
    if (form.engine!="sqlite") {
        dom.set_selected_index(node(evt.target,"profile-tls")^,
            if (form.tls=="verify-full") 0 else if (form.tls=="require") 1 else 2)
    }
    dom.focus_set(node(evt.target,"profile-file")^,false)
    return 'handled'
}
on dbench_result(evt) {
    let message=evt.detail
    if (message.tab!=active_tab) {
        let tab=tabs_model.get(tabs,message.tab)^
        if ((tabs_model.accepts(tab,message)^ and session_message(message)^)^) {
            let changed=session_result(tab.state,tab.worker,message)^
            tabs=tabs_model.update(tabs,tab.id,{state:changed,worker:if (message.type=="disconnected") null else tab.worker,
                uri:if (message.type=="disconnected") null else tab.uri})^
        }
        return 'handled'
    }
    if (message.generation!=st.generation) { return 'handled' }
    if (session_message(message)^) {
        let accepted=message.type!="batch" or message.token==st.token
        st=session_result(st,worker,message)^
        if (message.type=="copy_ready" and st.copy_ready!=null) {
            dom.clipboard_write(st.copy_ready)
            st={*:st,copy_ready:null,status:"Selection copied"}
        }
        if (message.type=="disconnected") {
            worker=null
            tabs=tabs_model.update(tabs,active_tab,{uri:null,worker:null})^
        }
        if (message.type!="batch" or not accepted) { return 'handled' }
        if (message.op=="page" and st.filter_input_column==null and st.table!=null) {
            let fields=model.table_columns(st.schema,st.table)
            let control=node(evt.target,"filter-column")^
            if ((len(fields)>0 and dom.selected_index(control)<0)^) dom.set_selected_index(control,0)
        }
        if (message.op!="more") {
            dom.set_scroll_state(node(evt.target,"result-scroll")^,0,0)
        }
        return 'handled'
    } else if (message.type=="profile_saved") {
        st={*:st,profile_busy:false,status:"Saved profile " ++ message.path,error:null}
    } else if (message.type=="profile_loaded") {
        // Clear the previous secret before publishing a different profile.
        clear_profile_password(evt.target)^
        st={*:st,profile_busy:false,profile_form:profiles.to_form(message.profile)^,
            status:"Loaded profile " ++ message.path,error:null}
        dom.request_frame(evt.target,"dbench_profile_ready")
    } else if (message.type=="profile_failed") {
        st={*:st,profile_busy:false,status:"Profile operation failed",error:message.message}
    } else if (message.type=="write_ready" and message.token==st.token and st.write_preparing) {
        writer=start(apply_sql,[dom.root_node(evt.target),st.sqlite_path,st.write_plan.sql,st.write_plan.params,st.generation,active_tab],{owner:'document'})^
        st={*:st,write_preparing:false,applying:true,status:"Executing reviewed SQL…",error:null}
    } else if (message.type=="write_prepare_failed" and message.token==st.token and st.write_preparing) {
        st={*:st,write_preparing:false,status:"Could not prepare SQL write",error:message.message}
    } else if (message.type=="sql_written") {
        writer=null
        let token=st.token+1
        let sent: any^=send(worker,{op:"schema",token:token})
        if (sent is error) { st={*:st,applying:false,show_write:false,write_plan:null,status:"SQL executed; schema refresh failed",error:string(sent.message)} }
        else { st={*:st,token:token,applying:false,show_write:false,write_plan:null,loading:true,status:"SQL executed; refreshing schema…",error:null} }
    } else if (message.type=="sql_write_failed") {
        writer=null
        st={*:st,applying:false,status:"SQL write failed; inspect the target before retrying",error:message.message}
    } else if (message.type=="applied") {
        writer=null
        st={*:st,applying:false,pending:[],pending_bytes:0,status:"Changes applied",error:null}
        if (st.connected and st.mode=="table" and st.table!=null) {
            st=show_table(st,worker,st.table,st.offset,st.page_cursors[string(st.offset)]) ^ {{*:st,error:string(^.message)}}
        }
    } else if (message.type=="apply_failed") {
        writer=null
        st={*:st,applying:false,status:"Changes were not applied",error:message.message}
    } else if (message.type=="exported") {
        st={*:st,exporting:false,show_export:false,stream_available:if (st.export_scope=="complete") false else st.stream_available,
            done:if (st.export_scope=="complete") true else st.done,status:"Created " ++ message.path,error:null}
    } else if (message.type=="export_progress" and message.token==st.token) {
        st={*:st,status:"Exported " ++ string(message.rows)^ ++ " rows…"}
    } else if (message.type=="export_failed") {
        st={*:st,exporting:false,stream_available:if (st.export_scope=="complete") false else st.stream_available,
            status:"Export failed",error:message.message}
    }
    return 'handled'
}
on change(evt) {
    if (st.applying or st.write_preparing or st.exporting or st.profile_busy) { return 'prevent-default' }
    if (dom.get_attribute(evt.target,"id")=="profile-engine") {
        let engine=dom.get_state(evt.target,"value")
        let old=st.profile_form.port
        st={*:st,profile_form:{*:st.profile_form,engine:engine,
            port:if (old in ["3306","5432"]) (if (engine=="mysql") "3306" else "5432") else old}}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="profile-tls") {
        st={*:st,profile_form:{*:st.profile_form,tls:dom.get_state(evt.target,"value") or "verify-full"}}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="edit-mode") {
        st={*:st,edit_mode:dom.get_state(evt.target,"value") or "text"}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="export-kind") {
        st={*:st,export_kind:dom.get_state(evt.target,"value") or "csv"}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="export-scope") {
        st={*:st,export_scope:dom.get_state(evt.target,"value") or "loaded"}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="filter-column") {
        st={*:st,filter_input_column:dom.get_state(evt.target,"value") or null}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="driver") {
        let engine=dom.get_state(evt.target,"value")
        st={*:st,engine:engine,target:""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="tls") {
        st={*:st,tls:dom.get_state(evt.target,"value") or "verify-full"}
        return 'handled'
    }
    return 'pass'
}
on input(evt) {
    if (st.applying or st.write_preparing or st.exporting or st.profile_busy) { return 'prevent-default' }
    let input_id=dom.get_attribute(evt.target,"id") or ""
    if (input_id=="profile-file") {
        st={*:st,profile_path:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (input_id in ["profile-name","profile-path","profile-host","profile-port","profile-database","profile-user","profile-ca"]) {
        let field=slice(input_id,8)^
        st={*:st,profile_form:{*:st.profile_form,*:map([field,dom.get_state(evt.target,"value") or ""])}}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="object-filter") {
        st={*:st,object_filter:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="sql") {
        st={*:st,query:dom.get_state(evt.target,"value") or "",sql_range:null}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="sql-params") {
        st={*:st,params:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="filter-value") {
        st={*:st,filter_input_value:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="edit-value") {
        st={*:st,edit_value:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="insert-values") {
        st={*:st,insert_text:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    if (dom.get_attribute(evt.target,"id")=="export-path") {
        st={*:st,export_path:dom.get_state(evt.target,"value") or ""}
        return 'handled'
    }
    return 'pass'
}
on pointerdown(evt) {
    if ((evt.button!=0 or st.selection_drag!=null or st.resize_drag!=null)^) { return 'pass' }
    if (dom.closest(evt.target,"#run, #write-review")!=null) {
        // Capture the native text selection before focus moves to the button.
        let field=node(evt.target,"sql")^
        st={*:st,sql_range:{start:dom.tc_selection_start(field),end:dom.tc_selection_end(field),
            script:dom.get_state(field,"value") or ""}}
        return 'pass'
    }
    let column_divider=dom.closest(evt.target,"[data-column-resize]")
    if (column_divider!=null) {
        st=resize_begin(st,column_divider,evt,"column",int(dom.get_attribute(column_divider,"data-column-resize")))^
        return 'prevent-default'
    }
    let divider=dom.closest(evt.target,"[data-pane-resize]")
    if (divider!=null) {
        st=resize_begin(st,divider,evt,dom.get_attribute(divider,"data-pane-resize"))^
        return 'prevent-default'
    }
    let cell=dom.closest(evt.target,"#result-grid td[data-row][data-col]")
    if (cell==null) { return 'pass' }
    let previous={row:st.row,col:st.col,anchor:st.anchor}
    let active=capture.capture(node(evt.target,"result-scroll")^,evt,node(evt.target,"result-grid")^)
    st={*:model.select(st,int(dom.get_attribute(cell,"data-row")),int(dom.get_attribute(cell,"data-col")),evt.shiftKey==true)^,
        selection_click:true,selection_drag:{*:active,previous:previous}}
    return 'prevent-default'
}
on pointermove(evt) {
    if (st.resize_drag!=null) {
        if ((evt.pointerId!=st.resize_drag.pointer)^) { return 'pass' }
        st=resize_move(st,evt.clientX,evt.clientY)^
        return 'prevent-default'
    }
    if ((st.selection_drag==null or evt.pointerId!=st.selection_drag.pointer)^) { return 'pass' }
    st=drag_move(evt.target,st,evt.clientX,evt.clientY)^
    return 'prevent-default'
}
on dbench_drag_scroll(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    if ((st.selection_drag!=null and st.selection_drag.x!=null)^) {
        st=drag_move(evt.target,st,st.selection_drag.x,st.selection_drag.y,false,true)^
    }
    return 'handled'
}
on pointerup(evt) {
    if (st.resize_drag!=null) {
        if ((evt.pointerId!=st.resize_drag.pointer)^) { return 'pass' }
        st=resize_finish(evt.target,resize_move(st,evt.clientX,evt.clientY)^)^
        return 'prevent-default'
    }
    if ((st.selection_drag==null or evt.pointerId!=st.selection_drag.pointer)^) { return 'pass' }
    st=drag_finish(evt.target,drag_move(evt.target,st,evt.clientX,evt.clientY,true)^)^
    return 'prevent-default'
}
on pointercancel(evt) {
    if (st.resize_drag!=null) {
        if ((evt.pointerId!=st.resize_drag.pointer)^) { return 'pass' }
        st=resize_finish(evt.target,st,true)^
        return 'handled'
    }
    if ((st.selection_drag==null or evt.pointerId!=st.selection_drag.pointer)^) { return 'pass' }
    st=drag_finish(evt.target,st,true)^
    return 'handled'
}
on lostpointercapture(evt) {
    if ((st.resize_drag!=null and evt.pointerId==st.resize_drag.pointer)^) { st=resize_finish(evt.target,st,true)^ }
    if ((st.selection_drag!=null and evt.pointerId==st.selection_drag.pointer)^) { st=drag_finish(evt.target,st)^ }
    return 'pass'
}
on click(evt) {
    let target=dom.closest(evt.target,"button,td[data-row][data-col],th[data-row-select]")
    if (target==null) { return 'pass' }
    let id=dom.get_attribute(target,"id")
    if (st.profile_busy) { return 'prevent-default' }
    if (st.show_profile and dom.closest(evt.target,".profile-panel")==null) { return 'prevent-default' }
    if (st.applying) {
        if (id=="cancel" and writer!=null) {
            let requested: any^=cancel(writer)
            if (requested is error) { st={*:st,error:string(requested.message)} }
            else { st={*:st,status:"Cancelling write…",error:null} }
            return 'handled'
        }
        return 'prevent-default'
    }
    if (st.exporting) {
        if (id=="cancel" and (st.export_scope in ["complete","table"] or (st.spooled and st.export_kind!="sql"))) {
            if ((worker!=null)^) cancel(worker)
            worker=null
            tabs=tabs_model.update(tabs,active_tab,{uri:null,worker:null})^
            st={*:detach_spool(st)^,generation:st.generation+1,connected:false,connecting:false,loading:false,done:true,
                exporting:false,show_export:false,stream_available:false,
                status:"Cancellation requested; check the destination",error:null}
            return 'handled'
        }
        return 'prevent-default'
    }
    if (id=="profile-open") {
        let current=if (st.profile_form.name=="") profiles.form(st.engine,st.tls,
            if (st.engine=="sqlite") st.target else "") else st.profile_form
        st={*:st,show_profile:true,profile_form:current,error:null}
        dom.request_frame(node(evt.target,"dbench")^,"dbench_profile_ready")
        return 'handled'
    }
    if (id=="profile-close") {
        clear_profile_password(evt.target)^
        st={*:st,show_profile:false,error:null}
        return 'handled'
    }
    if (id in ["profile-save","profile-load"]) {
        let path=trim(st.profile_path)
        if (path=="") { st={*:st,error:"Enter a profile file path."}; return 'handled' }
        if (id=="profile-save") {
            let checked: any^=profiles.from_form(st.profile_form)
            if (checked is error) { st={*:st,error:string(checked.message)}; return 'handled' }
        }
        start(profile_io,[dom.root_node(evt.target),if (id=="profile-save") "save" else "load",
            path,st.profile_form,st.generation,active_tab],{owner:'document'})^
        st={*:st,profile_busy:true,status:if (id=="profile-save") "Saving profile…" else "Loading profile…",error:null}
        return 'handled'
    }
    if (st.selection_click) {
        st={*:st,selection_click:false}
        if (dom.closest(evt.target,"#result-scroll")!=null) { return 'prevent-default' }
    }
    let table=dom.get_attribute(target,"data-table")
    let selected_tab=dom.get_attribute(target,"data-tab")
    let close_tab=dom.get_attribute(target,"data-tab-close")
    let sort=dom.get_attribute(target,"data-sort")
    let history_index=dom.get_attribute(target,"data-history")
    let row=dom.get_attribute(target,"data-row")
    let row_select=dom.get_attribute(target,"data-row-select")
    if (st.write_preparing and id!="cancel") { return 'prevent-default' }
    // A page boundary is valid only after the current result has arrived.
    if (st.loading and (id in ["next","previous","more"])) { return 'prevent-default' }
    if (id=="tab-new" or selected_tab!=null or close_tab!=null) {
        if (st.resize_drag!=null or st.selection_drag!=null) { return 'prevent-default' }
        let port=node(evt.target,"result-scroll")^
        let position=dom.scroll_state(port)
        let metrics=dom.scroll_metrics(port)
        let snapshot={*:st,target:dom.get_state(node(evt.target,"target")^,"value") or "",
            viewport:{x:float(position.x),y:float(position.y),width:float(metrics.client_width),height:float(metrics.client_height)}}
        tabs=tabs_model.update(tabs,active_tab,{state:snapshot,worker:worker})^
        if (id=="tab-new") {
            if (len(tabs)>=tabs_model.LIMIT) { return 'handled' }
            let source=tabs_model.get(tabs,active_tab)^
            let tab_id=next_tab
            let connected_target=source.uri!=null
            let driver=if (connected_target) source.driver else st.engine
            let tls=if (connected_target) source.tls else st.tls
            let sqlite_path=if (connected_target) source.sqlite_path else null
            let initial={*:model.initial(sqlite_path or ""),engine:driver,tls:tls,sqlite_path:sqlite_path}
            var new_worker=null
            var new_state=initial
            if (connected_target) {
                new_worker=connect(dom.root_node(evt.target),source.uri,driver,1,tab_id)
                new_state={*:initial,generation:1,connecting:true,status:"Connecting…"}
            }
            tabs=[*tabs,{id:tab_id,state:new_state,worker:new_worker,uri:source.uri,driver:driver,
                sqlite_path:sqlite_path,tls:tls}]
            next_tab=tab_id+1
            active_tab=tab_id
            st=new_state
            worker=new_worker
        } else if (close_tab!=null) {
            let tab_id=int(close_tab)^
            let closing=tabs_model.get(tabs,tab_id)^
            if (closing==null or len(tabs)==1) { return 'handled' }
            if ((len(closing.state.pending)>0)^) { st={*:st,error:"Apply or revert that tab's staged changes before closing it."}; return 'handled' }
            if (closing.worker!=null) cancel(closing.worker) ^ {null}
            tabs=tabs_model.remove(tabs,tab_id)^
            if (tab_id==active_tab) {
                let replacement=tabs[0]
                active_tab=replacement.id
                st=replacement.state
                worker=replacement.worker
            }
        } else {
            let destination=tabs_model.get(tabs,int(selected_tab)^)^
            if (destination==null or destination.id==active_tab) { return 'handled' }
            active_tab=destination.id
            st=destination.state
            worker=destination.worker
        }
        dom.request_frame(node(evt.target,"dbench")^,"dbench_tab_restore")
        return 'handled'
    }
    if (row!=null) {
        st=model.select(st,int(row),int(dom.get_attribute(target,"data-col")),evt.shiftKey==true)^
        dom.focus_set(node(evt.target,"result-grid")^,false)
        return 'handled'
    }
    if ((row_select!=null and len(st.columns)>0)^) {
        let r=int(row_select)
        st=model.select(model.select(st,r,0)^,r,len(st.columns)-1,true)^
        dom.focus_set(node(evt.target,"result-grid")^,false)
        return 'handled'
    }
    if (id in ["sample","connect","profile-connect"]) {
        if ((len(st.pending)>0)^) { st={*:st,error:"Apply or revert staged changes before switching connections."}; return 'handled' }
        let profile: any^=if (id=="profile-connect") profiles.from_form(st.profile_form) else null
        if (profile is error) { st={*:st,error:string(profile.message)}; return 'handled' }
        let driver=if (id=="sample") "sqlite" else if (id=="profile-connect") profile.engine
            else dom.get_state(node(evt.target,"driver"),"value")
        let password=if (id=="profile-connect" and driver!="sqlite")
            dom.get_state(node(evt.target,"profile-password"),"value") or "" else ""
        let entered_result: any^=if (id=="sample") SAMPLE else if (id=="profile-connect") profiles.target(profile,password)
            else trim(dom.get_state(node(evt.target,"target"),"value") or "")
        if (entered_result is error) { st={*:st,error:string(entered_result.message)}; return 'handled' }
        let entered=entered_result^
        let tls=if (driver=="sqlite") "verify-full" else if (id=="profile-connect") profile.tls
            else dom.get_state(node(evt.target,"tls"),"value") or "verify-full"
        if (entered=="") { st={*:st,error:"Enter a database file or connection URI."}; return 'handled' }
        if ((driver!="sqlite" and (not contains(entered,"://") or contains(entered,"#") or not (tls in ["verify-full","require","disable"])))^) {
            st={*:st,error:"Enter a database URI and select a TLS mode."}; return 'handled'
        }
        let uri=if (driver=="sqlite") entered else entered ++ (if (contains(entered,"?")^) "&" else "?") ++
            (if (driver=="mysql") "ssl-mode=" else "sslmode=") ++ tls
        if ((worker!=null)^) cancel(worker)
        let generation=st.generation+1
        worker=connect(dom.root_node(evt.target),uri,driver,generation,active_tab)
        tabs=tabs_model.update(tabs,active_tab,{uri:uri,driver:driver,tls:tls,
            sqlite_path:if (driver=="sqlite" and id!="sample") uri else null,worker:worker})^
        st={*:model.initial(if (driver=="sqlite" and id!="sample") uri else ""),engine:driver,tls:tls,generation:generation,
            sqlite_path:if (driver=="sqlite" and id!="sample") uri else null,connecting:true,status:"Connecting…",
            profile_path:st.profile_path,profile_form:st.profile_form}
    } else if (id=="disconnect" or id=="cancel") {
        if ((len(st.pending)>0)^) { st={*:st,error:"Apply or revert staged changes before disconnecting."}; return 'handled' }
        if ((worker!=null)^) cancel(worker)
        worker=null
        tabs=tabs_model.update(tabs,active_tab,{uri:null,worker:null})^
        st={*:detach_spool(st)^,generation:st.generation+1,connected:false,connecting:false,done:true,loading:false,stream_available:false,
            show_write:false,write_preparing:false,write_plan:null,status:"Disconnected",error:null}
    } else if (id=="refresh" and st.connected) {
        let token=st.token+1
        let sent: any^=send(worker,{op:"schema",token:token})
        if (sent is error) { st={*:st,error:string(sent.message)} }
        else { st={*:st,token:token,details:[],show_details:false,loading:true,status:"Refreshing schema…",error:null} }
    } else if (id=="history") {
        st={*:st,show_history:not st.show_history}
    } else if (id=="history-clear") {
        st={*:st,history:[],status:"Query history cleared"}
    } else if (history_index!=null) {
        let selected=st.history[int(history_index)]
        dom.set_state(node(evt.target,"sql")^,"value",selected)
        dom.set_state(node(evt.target,"sql-params")^,"value","[]")
        st={*:st,query:selected,params:"[]",sql_range:null,show_history:false,status:"Query restored; set parameters before running"}
    } else if (id=="details" and st.connected and st.mode=="table" and st.table!=null) {
        if (st.show_details) { st={*:st,show_details:false} }
        else {
            send(worker,{op:"details"}) ^ {null}
            st={*:st,show_details:true,status:"Loading object details…",error:null}
        }
    } else if ((id=="write-review" and st.connected and st.sqlite_path!=null and len(st.pending)==0)^) {
        let input: any^=query_input(evt.target,st,true)
        if (input is error) { st={*:st,error:string(input.message)} }
        else { st={*:st,show_write:true,write_plan:input,sql_range:null,error:null,status:"Review the target, SQL and parameters"} }
    } else if (id=="write-cancel") {
        st={*:st,show_write:false,write_plan:null,status:"SQL write cancelled",error:null}
    } else if (id=="write-confirm" and st.show_write and st.write_plan!=null and st.connected) {
        let token=st.token+1
        send(worker,{op:"finish_for_write",token:token})^
        st={*:st,token:token,write_preparing:true,loading:false,status:"Finishing the current result…",error:null}
    } else if ((id=="edit-cell" and st.connected and st.sqlite_path!=null and st.mode=="table" and
                len(st.rows)>0 and model.table_kind(st.schema,st.table)=="table")^) {
        let value=st.rows[st.row][st.col]
        let prepared: any^=changes.editor_value(value)
        if (prepared is error) { st={*:st,error:string(prepared.message)} }
        else {
            st={*:st,show_editor:true,show_insert:false,edit_mode:prepared.mode,edit_value:prepared.text,error:null,
                edit_base:{table:st.table,fields:model.table_columns(st.schema,st.table),row:st.rows[st.row],column:st.col}}
            dom.request_frame(node(evt.target,"dbench")^,"dbench_edit_ready")
        }
    } else if (id=="cancel-edit") {
        st={*:st,show_editor:false}
    } else if (id=="stage-value" and st.show_editor) {
        let fields=st.edit_base.fields
        let row=st.edit_base.row
        let value: any^=changes.parse_value(st.edit_mode,st.edit_value)
        if (value is error) { st={*:st,error:string(value.message)} }
        else {
            let bytes=2*model.result_size([row])+model.result_size([[value]])
            let proposed: any^=changes.plan(st.edit_base.table,fields,row,st.edit_base.column,value)
            if (proposed is error) { st={*:st,error:string(proposed.message)} }
            else { st=stage_change(st,proposed,bytes)^ }
        }
    } else if ((id=="delete-row" and st.connected and st.sqlite_path!=null and st.mode=="table" and
                len(st.rows)>0 and model.table_kind(st.schema,st.table)=="table")^) {
        let row=st.rows[st.row]
        let proposed: any^=changes.delete_plan(st.table,model.table_columns(st.schema,st.table),row)
        if (proposed is error) { st={*:st,error:string(proposed.message)} }
        else { st=stage_change(st,proposed,2*model.result_size([row]))^ }
    } else if ((id=="insert-row" and st.connected and st.sqlite_path!=null and st.mode=="table" and
                st.table!=null and model.table_kind(st.schema,st.table)=="table")^) {
        st={*:st,show_insert:true,show_editor:false,insert_text:"{}",
            insert_base:{table:st.table,fields:model.table_columns(st.schema,st.table)},error:null}
    } else if (id=="cancel-insert") {
        st={*:st,show_insert:false}
    } else if (id=="stage-insert" and st.show_insert) {
        if ((len(st.insert_text)>65536)^) { st={*:st,error:"Insert values exceed the size limit."}; return 'handled' }
        let values: any^=parse(st.insert_text,'json')
        if (values is error or not (values is map)) { st={*:st,error:"Enter insert values as a JSON object."}; return 'handled' }
        let proposed: any^=changes.insert_plan(st.insert_base.table,st.insert_base.fields,values)
        if (proposed is error) { st={*:st,error:string(proposed.message)} }
        else { st=stage_change(st,proposed,model.result_size([proposed.statement_params]))^ }
    } else if (id=="revert-changes") {
        st={*:st,pending:[],pending_bytes:0,show_editor:false,show_insert:false,status:"Staged changes reverted",error:null}
    } else if ((id=="apply-changes" and len(st.pending)>0 and st.sqlite_path!=null and st.connected and
                st.mode=="table" and not st.loading)^) {
        writer=start(apply_changes,[dom.root_node(evt.target),st.sqlite_path,st.pending,st.generation,active_tab],{owner:'document'})^
        st={*:st,applying:true,show_editor:false,show_insert:false,status:"Applying staged changes…",error:null}
    } else if ((id=="export" and (len(st.rows)>0 or st.stream_available or
            (st.mode=="table" and st.table!=null and st.connected)))^) {
        st={*:st,show_export:true,export_kind:"csv",export_scope:if (len(st.rows)>0) "loaded"
            else if (st.mode=="table") "table" else "complete",error:null}
    } else if (id=="save-sql") {
        st={*:st,show_export:true,export_kind:"sql",export_scope:"loaded",error:null}
    } else if (id=="export-cancel") {
        st={*:st,show_export:false}
    } else if (id=="export-save" and st.show_export) {
        let path=trim(st.export_path)
        if (path=="") { st={*:st,error:"Enter a path for the new export file."}; return 'handled' }
        if (st.export_scope=="complete" and (not st.stream_available or st.mode!="query" or st.loading)) {
            st={*:st,error:"Run a query and wait for its result before exporting it."}; return 'handled'
        }
        if (st.export_scope=="table" and (st.mode!="table" or st.table==null or not st.connected or st.loading)) {
            st={*:st,error:"Wait for the table page before exporting it."}; return 'handled'
        }
        let bounded_spool=st.spooled and st.mode=="query" and st.export_kind!="sql" and st.export_scope!="complete"
        if (bounded_spool and st.loading) { st={*:st,error:"Wait for the current fetch before exporting."}; return 'handled' }
        if (bounded_spool) {
            let b=if (st.export_scope=="selection") model.bounds(st) else
                {top:0,bottom:st.total_rows-1,left:0,right:len(st.columns)-1}
            if (((b.bottom-b.top+1)*(b.right-b.left+1)>exports.MAX_CELLS)^) {
                st={*:st,error:"Export exceeds the cell limit."}; return 'handled'
            }
            let token=st.token+1
            send(worker,{op:"stream_export",token:token,path:path,kind:st.export_kind,
                columns:[for (c in b.left to b.right) st.columns[c]],spooled:true,bounded:true,
                first:b.top,last:b.bottom,left:b.left,right:b.right,done:true})^
            st={*:st,token:token,window_pending:null}
        } else if (st.export_scope=="table") {
            let query=model.table_request(st,st.table,0,null,true)^
            let token=st.token+1
            send(worker,{op:"table_export",token:token,path:path,kind:st.export_kind,columns:st.columns,
                sql:query.sql,params:query.params,rows:[],done:false,spooled:false,total_rows:0})^
            st={*:st,token:token}
        } else if (st.export_scope=="complete") {
            let token=st.token+1
            send(worker,{op:"stream_export",token:token,path:path,kind:st.export_kind,columns:st.columns,rows:st.rows,done:st.done,
                spooled:st.spooled,total_rows:st.total_rows})^
            st={*:st,token:token,window_pending:null}
        }
        if (not bounded_spool and not (st.export_scope in ["complete","table"]))
            start(export_file,[dom.root_node(evt.target),path,st,st.export_scope,st.export_kind,st.generation,active_tab],{owner:'document'})^
        st={*:st,exporting:true,status:"Creating export…",error:null}
    } else if ((id=="inspect-value" and len(st.rows)>0)^) {
        st={*:st,show_value:not st.show_value}
    } else if ((id=="value-prev" or id=="value-next") and st.show_value) {
        let col=st.col+(if (id=="value-next") 1 else -1)
        st=reveal(evt.target,model.select(st,st.row,col)^)^
    } else if (table!=null and st.connected) {
        let reset={*:st,sort_column:null,sort_direction:"ASC",filter_column:null,filter_value:"",
            filter_input_column:null,filter_input_value:""}
        st=show_table(reset,worker,table,0) ^ {{*:st,error:string(^.message)}}
    } else if (sort!=null and st.connected and st.mode=="table") {
        let direction=if (st.sort_column==sort and st.sort_direction=="ASC") "DESC" else "ASC"
        st=show_table({*:st,sort_column:sort,sort_direction:direction},worker,st.table,0) ^ {{*:st,error:string(^.message)}}
    } else if ((id=="apply-filter" or id=="clear-filter") and st.connected and st.table!=null) {
        let column=dom.get_state(node(evt.target,"filter-column"),"value")
        let value=if (id=="clear-filter") "" else dom.get_state(node(evt.target,"filter-value"),"value") or ""
        st=show_table({*:st,filter_column:column,filter_value:value,filter_input_column:column,filter_input_value:value},worker,st.table,0) ^ {{*:st,error:string(^.message)}}
    } else if ((id=="next" or id=="previous") and st.connected and st.mode=="table") {
        let offset=if (id=="next") st.offset+256 else max(0,st.offset-256)
        let cursor=if (id=="next") model.next_cursor(st)^ else st.page_cursors[string(offset)]
        let next=if (id=="next") {*:st,page_cursors:model.remember_cursor(st,offset,cursor)^} else st
        st=show_table(next,worker,st.table,offset,cursor) ^ {{*:st,error:string(^.message)}}
    } else if (id=="run" and st.connected) {
        st=run_query(evt.target,st,worker,true)^
    } else if (id=="more" and st.connected and st.mode=="query" and not st.done) {
        if ((not st.spooled and (len(st.rows)>=model.MAX_ROWS or st.bytes>=model.MAX_BYTES))^) {
            send(worker,{op:"finish"}) ^ {null}
            st={*:st,done:true,stream_available:false,status:"Result cache limit reached"}
        } else {
            let token=st.token+1
            send(worker,{op:"more",token:token,limit:if (st.spooled) 256 else min(256,model.MAX_ROWS-len(st.rows)),spooled:st.spooled}) ^ {null}
            st={*:st,token:token,loading:true,window_pending:null,status:"Fetching rows…"}
        }
    } else if ((id=="copy" and len(st.rows)>0)^) {
        st=copy_selection(st,worker)^
    } else { return 'pass' }
    return 'handled'
}
on keydown(evt) {
    if (st.profile_busy) { return 'prevent-default' }
    if (st.show_profile) {
        if (evt.key=="Escape") {
            clear_profile_password(evt.target)^
            st={*:st,show_profile:false,error:null}
            return 'prevent-default'
        }
        if (dom.closest(evt.target,".profile-panel")==null) {
            dom.focus_set(node(evt.target,"profile-file")^,false)
            return 'prevent-default'
        }
        return 'pass'
    }
    let tab_button=dom.closest(evt.target,"[data-tab]")
    if (tab_button!=null and (evt.key=="ArrowLeft" or evt.key=="ArrowRight")) {
        let id=int(dom.get_attribute(tab_button,"data-tab"))^
        let neighbor=tabs_model.neighbor(tabs,id,if (evt.key=="ArrowRight") 1 else -1)^
        if (neighbor!=null) dom.focus_set(node(evt.target,"tab-" ++ string(neighbor)^)^,false)
        return 'prevent-default'
    }
    if ((dom.closest(evt.target,"#sql")!=null and (evt.ctrlKey==true or evt.metaKey==true) and evt.key=="Enter" and
            st.connected and not st.applying and not st.write_preparing and not st.exporting)^) {
        st=run_query(evt.target,st,worker)^
        return 'prevent-default'
    }
    let column_divider=dom.closest(evt.target,"[data-column-resize]")
    if (column_divider!=null and (evt.key in ["ArrowLeft","ArrowRight","Home","End"])) {
        let index=int(dom.get_attribute(column_divider,"data-column-resize"))
        let width=capture.key_width(column_width(st,index),evt.key,resize_limits("column"))
        st=resized(st,"column",width,index)^
        return 'prevent-default'
    }
    let divider=dom.closest(evt.target,"[data-pane-resize]")
    if (divider!=null) {
        let axis=dom.get_attribute(divider,"data-pane-resize")
        let key=evt.key or ""
        let supported=if (axis=="sidebar") ["ArrowLeft","ArrowRight","Home","End"] else ["ArrowUp","ArrowDown","Home","End"]
        if (key in supported) {
            let direction=if (key=="ArrowUp") "ArrowLeft" else if (key=="ArrowDown") "ArrowRight" else key
            let width=if (axis=="sidebar") st.sidebar_width else st.editor_height
            st=resized(st,axis,capture.key_width(width,direction,resize_limits(axis)))^
            return 'prevent-default'
        }
    }
    if ((dom.closest(evt.target,"#result-grid")==null or len(st.rows)==0)^) { return 'pass' }
    let key=evt.key or ""
    let primary=evt.ctrlKey==true or evt.metaKey==true
    if (primary and lower(key)=="c") { st=copy_selection(st,worker)^; return 'prevent-default' }
    if (not (key in ["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End","PageUp","PageDown"])) { return 'pass' }
    let page=max(1,int(max(0,st.viewport.height-27)/27))
    let dr=if (key=="ArrowDown") 1 else if (key=="ArrowUp") -1 else if (key=="PageDown") page else if (key=="PageUp") -page else 0
    let dc=if (key=="ArrowRight") 1 else if (key=="ArrowLeft") -1 else 0
    let r=if (primary and key=="Home") 0 else if (primary and key=="End") model.row_count(st)^-1 else st.row+dr
    let c=if (key=="Home") 0 else if (key=="End") len(st.columns)-1 else st.col+dc
    st=reveal(evt.target,model.select(st,r,c,evt.shiftKey==true)^)^
    return 'prevent-default'
}
<html lang:"en",<head <meta charset:"utf-8"> <title "DBench — Lambda database workbench"> <style CSS>> apply(<dbench_app>,{mode:"edit"})>
