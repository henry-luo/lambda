import ui: lambda.ui.dtna
let outline = <dtna.tree id:"outline",label:"Project",checkable:true,items:[
    {key:'root',label:"Project",children:[{key:1,label:"Alpha"},{key:2,label:"Beta"},
        {key:'disabled',label:"Disabled",disabled:true,children:[{key:'isolated',label:"Isolated"}]}]},
    {key:'tail',label:"Tail"}]>
let strict = <dtna.tree id:"strict",label:"Independent checks",checkable:true,check_strictly:true,default_expanded_keys:['parent'],
    items:[{key:'parent',label:"Parent",children:[{key:'child',label:"Child"}]}]>
view dtna_tree_test: <tree_test> state last_action:"",value_type:"" {
    <div *[apply(outline),apply(strict),<span id:"last",last_action>,<span id:"value-type",value_type>]>
}
on ui_change(action) {
    last_action = action.id ++ ":" ++ string(action.action)
    value_type = string(type(action.value.key))
}
apply(<dtna.page <tree_test>>)
