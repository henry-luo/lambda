import ui: lambda.ui.dtna
let disclosure = <dtna.collapse id:"controlled-collapse",active_keys:[],items:[{key:1,label:"Closed",children:"Body"}]>
let outline = <dtna.tree id:"controlled-tree",checkable:true,expanded_keys:['parent'],checked_keys:[],selected_keys:[],
    items:[{key:'parent',label:"Parent",children:[{key:1,label:"Child"}]}]>
let table = <dtna.table id:"controlled-table",current:2,selected_keys:[],sort:null,filters:{},selection:'single',page_size:1,
    columns:[{key:'name',title:"Name",sortable:true}],rows:[{key:1,name:"Alpha"},{key:2,name:"Beta"}]>
let empty_table = <dtna.table id:"empty-table",pagination:false,rows:[],columns:[{key:'name',title:"Name"}],empty_text:"Nothing here">
view dtna_controlled_collections_test: <controlled_collections_test> state request:"",type_name:"" {
    <div *[apply(disclosure),apply(outline),apply(table),apply(empty_table),<span id:"request",request>,<span id:"type-name",type_name>]>
}
on ui_change(action) {
    request = action.id ++ ":" ++ string(action.action)
    if (action.action == 'page') { type_name = string(action.value.current) }
    else if (action.id == "controlled-tree") { type_name = string(type(action.value.key)) }
    else if (action.id == "controlled-table" and action.action == 'select') { type_name = string(type(action.value.selected_keys[0])) }
}
apply(<dtna.page <controlled_collections_test>>)
