import dom
import c: lambda.ui.core.component
import collection: lambda.ui.core.collection
import interaction: lambda.ui.core.interaction
import navigation: .navigation

pub fn rows(props) array^ {
    if (not (props.rows is array)) raise c.fail("table","rows must be an array")
    else (
        let keyed = [for (index,row in props.rows) {key:row[c.option(props,"row_key",'key')],disabled:row.disabled,index:index,data:row}],
        let valid = collection.validate(keyed,"table")^,
        if (not all([for (row in props.rows) row is map])) raise c.fail("table","rows must contain maps") else keyed
    )
}
fn validate_sort(columns, sorting) bool^ {
    if (sorting == null) true
    else if (not (sorting is map) or not contains(['asc','desc'],sorting.direction) or
        not any([for (column in columns) column.key == sorting.key and column.sortable])) raise c.fail("table","sort needs a sortable column key and asc/desc direction")
    else true
}
fn field(column) => c.option(column,"data_index",column.key)
fn validate_filters(columns, filters) bool^ {
    if (not (filters is map)) raise c.fail("table","filters must be a map")
    else (
        let invalid = [for (key,value in filters where not any([for (column in columns)
            string(column.key) == string(key) and (value == null or any([for (option in column.filters) option.value == value]))])) key],
        if (len(invalid) > 0) raise c.fail("table","unknown filter column or value") else true
    )
}
pub fn table(props) element^ {
    let node = c.node('table',props,null,["rows","columns","row_key","sort","default_sort","filters","default_filters","current","default_current",
        "page_size","pagination","selection","selected_keys","default_selected_keys","expanded_keys","default_expanded_keys","caption","summary","empty_text","bordered","size","loading","disabled"])^
    let identity = collection.require_id(props,"table")^
    let flags = c.boolean_props(props,["pagination","bordered"],"table")^
    let valid_columns = collection.validate(props.columns,"table columns")^
    let columns = [for (column in props.columns) (
        let names = c.properties(column,["key","title","data_index","sortable","filters","render","width","align"],"table column")^,
        let booleans = c.boolean_props(column,["sortable"],"table column")^,
        let options = collection.validate([for (option in c.option(column,"filters",[])) {key:option.value,disabled:option.disabled}],"table filters")^,
        if (not (field(column) is string or field(column) is symbol)) raise c.fail("table","data_index must be a string or symbol")
        else if (column.filters != null and not (column.filters is array)) raise c.fail("table","column filters must be arrays")
        else if (column.render != null and not (column.render is fn)) raise c.fail("table","cell render must be a pure function")
        else if (column.width != null and (not c.finite(column.width) or column.width <= 0)) raise c.fail("table","column width must be positive")
        else if (not c.enum_valid(column.align,["left","center","right"])^) raise c.fail("table","invalid column alignment") else true
    )]
    let data = rows(props)^
    let selections = [for (name in ["selected","expanded"]) collection.key_props(data,props,name ++ "_keys","default_" ++ name ++ "_keys","table")^]
    let sorting = [validate_sort(props.columns,props.sort)^,validate_sort(props.columns,props.default_sort)^]
    let filtering = [validate_filters(props.columns,c.option(props,"filters",{}))^,validate_filters(props.columns,c.option(props,"default_filters",{}))^]
    let size = c.option(props,"page_size",10)
    let current = c.option(props,"current",c.option(props,"default_current",1));
    if (len(props.columns) == 0) raise c.fail("table","at least one column is required")
    else if (props.row_key != null and not (props.row_key is string or props.row_key is symbol)) raise c.fail("table","row_key must be a string or symbol")
    else if (not (size is int) or size < 1 or not (current is int) or current < 1) raise c.fail("table","page_size and current must be positive ints")
    else if (not c.enum_valid(props.selection,["none","single","multiple"])^) raise c.fail("table","invalid selection mode")
    else if (c.text(props.selection) == "single" and (len(c.option(props,"selected_keys",[])) > 1 or len(c.option(props,"default_selected_keys",[])) > 1)) raise c.fail("table","single selection allows at most one key")
    else if (any([for (name in ["sort","filters","current"])
        c.has(props,name) and c.has(props,"default_" ++ name)])) raise c.fail("table","controlled and default props are mutually exclusive")
    else node
}
pub fn model(props, sorting = null, filters = {}, current = 1) {
    let filtered = [for (entry in rows(props)^ where all([for (column in props.columns)
        filters[column.key] == null or entry.data[field(column)] == filters[column.key]])) entry]
    let column = [for (entry in props.columns where entry.key == sorting.key) entry][0]
    let ordered = if (sorting == null) filtered else sort(filtered,{dir:sorting.direction,by:(entry) => entry.data[field(column)]})
    let size = c.option(props,"page_size",10)
    let pages = if (props.pagination == false) 1 else collection.page_count(len(ordered),size)
    let page = min(max(1,current),pages);
    {rows:if (props.pagination == false) ordered else collection.page_rows(ordered,page,size)^,total:len(ordered),pages:pages,current:page,page_size:size}
}
fn current_value(node, name, current) => c.option(c.props(node),name,current)
fn selection_mode(node) string^ => c.text(c.option(c.props(node),"selection",'none'))^
fn col_style(column) => (if (column.width == null) "" else "width:" ++ c.px(column.width) ++ ";") ++
    (if (column.align == null) "" else "text-align:" ++ c.text(column.align) ++ ";")
fn cell(column, entry) => c.render(if (column.render == null) entry.data[field(column)] else column.render(entry.data[field(column)],entry.data,entry.index))
fn check_button(node, entry, selected) => <button type:"button",class:"dtna-check",["data-dtna-table-select"]:string(entry.index),
    role:if (selection_mode(node)^ == "single") "radio" else "checkbox",["aria-checked"]:c.aria(contains(selected,entry.key)),
    ["aria-label"]:"Select row " ++ c.text(entry.key),*:c.boolean_attr("disabled",c.props(node).disabled or entry.disabled),if (contains(selected,entry.key)) "✓" else "">
view dtna_table: <dtna.table> state sorting:c.option(c.props(~),"default_sort",null),filters:c.option(c.props(~),"default_filters",{}),
    current:c.option(c.props(~),"default_current",1),selected:c.option(c.props(~),"default_selected_keys",[]),expanded:c.option(c.props(~),"default_expanded_keys",[]) {
    let order = current_value(~,"sort",sorting)
    let filtering = current_value(~,"filters",filters)
    let data = model(c.props(~),order,filtering,current_value(~,"current",current))
    let chosen = current_value(~,"selected_keys",selected)
    let opened = current_value(~,"expanded_keys",expanded)
    let candidates = [for (entry in collection.enabled(data.rows)) entry.key]
    let checked = len(candidates) > 0 and all([for (key in candidates) contains(chosen,key)])
    let some = any([for (key in candidates) contains(chosen,key)])
    let expandable = any([for (row in c.props(~).rows) row.detail != null])
    let span = len(c.props(~).columns) + (if (selection_mode(~)^ == "none") 0 else 1) + (if (expandable) 1 else 0);
    <div *:c.attrs(c.props(~)),class:c.classes('table',c.props(~)) ++ (if (c.props(~).bordered) " dtna-table-bordered" else ""),style:c.props(~).style,
        ["aria-busy"]:c.aria(c.props(~).loading),*[
        <div class:"dtna-table-scroll",<table *[
            if (c.props(~).caption != null) <caption c.render(c.props(~).caption)> else null,
            <thead <tr *[
                if (expandable) <th scope:"col",["aria-label"]:"Expand row"> else null,
                if (selection_mode(~)^ != "none") <th scope:"col",if (selection_mode(~)^ == "multiple") <button type:"button",class:"dtna-check",role:"checkbox",
                    ["data-dtna-table-all"]:"",["aria-label"]:"Select page",["aria-checked"]:if (checked) "true" else if (some) "mixed" else "false",
                    *:c.boolean_attr("disabled",c.props(~).disabled or len(candidates) == 0),if (checked) "✓" else if (some) "−" else ""> else null> else null,
                *[for (index,column in c.props(~).columns) <th scope:"col",style:col_style(column),
                    *:(if (column.sortable) {'aria-sort':if (order.key != column.key) "none" else if (order.direction == 'asc') "ascending" else "descending"} else {}),*[
                    if (column.sortable) <button type:"button",class:"dtna-table-sort",["data-dtna-table-sort"]:string(index),
                        *:c.boolean_attr("disabled",c.props(~).disabled),*[c.render(column.title),<span ["aria-hidden"]:"true",if (order.key != column.key) " ↕" else if (order.direction == 'asc') " ↑" else " ↓">]>
                    else c.render(column.title),
                    if (column.filters != null) <div class:"dtna-table-filters",["aria-label"]:"Filter " ++ c.text(column.title),
                        *[for (option_index,option in [ {value:null,label:"All"},*column.filters]) <button type:"button",
                            ["data-dtna-table-filter"]:string(index),["data-dtna-filter-option"]:string(option_index),
                            ["aria-pressed"]:c.aria(filtering[column.key] == option.value),*:c.boolean_attr("disabled",c.props(~).disabled or option.disabled),c.render(option.label)>]> else null]>]]>>,
            <tbody *[if (len(data.rows) == 0) <tr <td colspan:string(span),class:"dtna-empty",c.render(c.option(c.props(~),"empty_text","No data"))>> else null,
                *[for (entry in data.rows) for (row in [
                    <tr class:if (contains(chosen,entry.key)) "dtna-table-selected" else null,["data-dtna-row"]:string(entry.index),*[
                        if (expandable) <td if (entry.data.detail != null) <button type:"button",class:"dtna-table-expand",["data-dtna-table-expand"]:string(entry.index),
                            ["aria-label"]:"Expand row " ++ c.text(entry.key),["aria-expanded"]:c.aria(contains(opened,entry.key)),
                            *:c.boolean_attr("disabled",c.props(~).disabled or entry.disabled),if (contains(opened,entry.key)) "−" else "+"> else null> else null,
                        if (selection_mode(~)^ != "none") <td check_button(~,entry,chosen)> else null,
                        *[for (column in c.props(~).columns) <td style:col_style(column),cell(column,entry)>]]>,
                    if (entry.data.detail != null and contains(opened,entry.key)) <tr class:"dtna-table-detail",<td colspan:string(span),c.render(entry.data.detail)>> else null]) row]]>,
            if (c.props(~).summary != null) <tfoot <tr <td colspan:string(span),c.render(c.props(~).summary)>>> else null]>>,
        if (c.props(~).loading) <div role:"status",class:"dtna-table-loading","Loading…"> else null,
        if (c.props(~).pagination != false) <nav class:"dtna-pagination",["aria-label"]:"Table pagination",*navigation.page_controls(data.current,data.pages,c.props(~).disabled == true)> else null]>
}
on click(evt) {
    if (c.props(~).disabled or interaction.root(~,evt) == null) { return 'pass' }
    let target = interaction.target(~,evt,"[data-dtna-table-sort],[data-dtna-table-filter],[data-dtna-page],[data-dtna-table-select],[data-dtna-table-all],[data-dtna-table-expand]")
    if (target == null or dom.get_state(target,"disabled")) { return 'pass' }
    let data = model(c.props(~),current_value(~,"sort",sorting),current_value(~,"filters",filters),current_value(~,"current",current))
    let sort_index = dom.get_attribute(target,"data-dtna-table-sort")
    let filter_index = dom.get_attribute(target,"data-dtna-table-filter")
    let page = dom.get_attribute(target,"data-dtna-page")
    let row_index = dom.get_attribute(target,"data-dtna-table-select")
    let expand_index = dom.get_attribute(target,"data-dtna-table-expand")
    let action = if (sort_index != null) 'sort' else if (filter_index != null) 'filter' else if (page != null) 'page' else if (expand_index != null) 'expand' else 'select'
    if (action == 'sort') {
        let key = c.props(~).columns[int(sort_index) or 0].key
        let old = current_value(~,"sort",sorting)
        sorting = if (old.key == key and old.direction == 'desc') null else {key:key,direction:if (old.key == key and old.direction == 'asc') 'desc' else 'asc'}
        current = 1
    } else if (action == 'filter') {
        let column = c.props(~).columns[int(filter_index) or 0]
        let option = int(dom.get_attribute(target,"data-dtna-filter-option")) or 0
        filters = {*:current_value(~,"filters",filters),[column.key]:if (option == 0) null else column.filters[option-1].value}
        current = 1
    } else if (action == 'page') { current = min(data.pages,max(1,int(page) or 1)) }
    else if (action == 'expand') {
        let entry = rows(c.props(~))^[int(expand_index) or 0]
        expanded = collection.toggle(current_value(~,"expanded_keys",expanded),entry.key)
    } else {
        let chosen = current_value(~,"selected_keys",selected)
        let candidates = [for (entry in collection.enabled(data.rows)) entry.key]
        let entry = if (row_index == null) null else rows(c.props(~))^[int(row_index) or 0]
        if (entry != null and entry.disabled) { return 'pass' }
        selected = if (row_index == null) collection.set_keys(chosen,candidates,not all([for (key in candidates) contains(chosen,key)]))
            else collection.toggle(chosen,entry.key,selection_mode(~)^ == "multiple")
    }
    emit("ui_change",c.action(~,action,
        if (action == 'select') {selected_keys:selected}
        else if (action == 'expand') {expanded_keys:expanded}
        else {sort:if (action == 'sort') sorting else current_value(~,"sort",sorting),
            filters:if (action == 'filter') filters else current_value(~,"filters",filters),
            current:current,page_size:data.page_size}))
    'pass'
}
