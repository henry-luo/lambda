import ui: lambda.ui.blade

let tabs = <blade.panel title:"Retained journal",<blade.tabs id:"tabs",keep_mounted:true,items:[
    {key:'draft',label:"Draft",children:<blade.input id:"draft",label:"Draft",default_value:"">},
    {key:'locked',label:"Locked",disabled:true},
    {key:'items',label:"Items",children:<blade.item_slot id:"nested",icon:'shield',label:"Nested shield">}]>>
let table = <blade.table id:"table",page_size:2,selection:'multiple',columns:[{key:'name',title:"Name",sortable:true}],rows:[{key:1,name:"Cora"},{key:2,name:"Ada"},{key:3,name:"Bea"}]>
view blade_behavior: <blade_behavior> state selected:false, actions:0, action_name:"", accepted:"", requests:"" {
    ui.render(<blade.flex direction:'vertical',align:"stretch",gap:18,
        <blade.space gap:16,wrap:true,
            <blade.item_slot id:"select",icon:'sword',label:"Select sword",selected:selected,quantity:2,rarity:'legendary'>
            <blade.item_slot id:"fixed",icon:'ring',label:"Fixed ring",selected:false>
            <blade.item_slot id:"disabled",icon:'boots',label:"Disabled boots",disabled:true>
            <blade.item_slot id:"empty",label:"Empty slot">
            <blade.ability id:"cast",icon:'fire',label:"Cast fire",hotkey:"Q">
            <blade.ability id:"cooldown",icon:'ice',label:"On cooldown",cooldown:60>>
        <blade.meter id:"health",resource:'health',value:240,max:500>
        <blade.space <blade.input id:"accepted",value:accepted,label:"Accepted field"> <blade.checkbox id:"check","Checkbox">>
        <div ui.render(tabs)^>
        <div ui.render(table)^>
        <blade.form id:"form",<blade.input id:"name",name:"name",default_value:"Blade",label:"Name">
            <blade.button id:"submit",type:'submit',"Submit"> <blade.button id:"reset",type:'reset',"Reset">>
        <blade.config_provider tokens:{primary:"#527a9c",slot_size:96},
            <blade.button id:"blue",variant:'primary',"Blue">
            <blade.config_provider tokens:{radius:10},<blade.button id:"inherited",variant:'primary',"Inherited">>
            <blade.item_slot id:"large-slot",icon:'shield'>>
        <blade.config_provider disabled:true,<blade.item_slot id:"provider-disabled",icon:'sword'> <blade.input id:"provider-input">>
        <output id:"actions",string(actions)> <output id:"last",action_name> <output id:"requests",requests>>)^
}
on ui_action(action) {
    actions = actions + 1
    action_name = action.id ++ ":" ++ string(action.action)
    if (action.id == "select") { selected = not selected }
    if (action.action == 'submit') { requests = action.value[0][0] ++ "=" ++ action.value[0][1] }
}
on ui_change(action) {
    if (action.id == "accepted") { accepted = action.value }
    requests = action.id ++ ":" ++ string(action.value)
}
apply(<blade.page <blade_behavior>>)
