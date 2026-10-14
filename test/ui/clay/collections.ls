import ui: lambda.ui.clay

let collections = <clay.flex direction:'vertical',align:"stretch",gap:18,
        <clay.tabs id:"tabs",keep_mounted:true,items:[
            {key:1,label:"First",children:<clay.input id:"retained",default_value:"",label:"Retained field">},
            {key:2,label:"Unavailable",disabled:true},{key:3,label:"Third",children:<clay.button id:"nested","Nested action">}]>
        <clay.segmented id:"segments",items:[{key:10,label:"Ten"},{key:20,label:"Twenty"}]>
        <clay.tabs id:"fixed-tabs",value:'a',items:[{key:'a',label:"Fixed A",children:"A"},{key:'b',label:"Fixed B",children:"B"}]>
        <clay.pagination id:"pages",total:45,page_size:10>
        <clay.collapse id:"collapse",items:[{key:'one',label:"Open me",children:<clay.button id:"inside","Inside">}]>
        <clay.tree id:"tree",checkable:true,default_expanded_keys:['root'],items:[{key:'root',label:"Studio",children:[{key:1,label:"Design"},{key:2,label:"Product"}]}]>
        <clay.table id:"table",page_size:2,selection:'multiple',columns:[{key:'name',title:"Name",sortable:true}],rows:[
            {key:1,name:"Cora"},{key:2,name:"Ada"},{key:3,name:"Bea"}]>
        <clay.rate id:"rating",default_value:2,label:"Rating">
        <clay.checkable_tag id:"tag","Favorite">
        <clay.alert id:"alert",message:"Ready",closable:true>>;

view clay_collection_example: <clay_collection_example> state last_action:"", requests:0 {
    <div *[ui.render(collections)^,<output id:"last",last_action>,<output id:"requests",string(requests)>]>
}
on ui_change(action) {
    last_action = string(action.id) ++ ":" ++ string(action.action)
    requests = requests + 1
}
on ui_action(action) { last_action = string(action.id) ++ ":" ++ string(action.action) }
apply(<clay.page <clay_collection_example>>)
