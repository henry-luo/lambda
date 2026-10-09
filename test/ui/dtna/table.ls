import ui: lambda.ui.dtna
let people = ui.table({id:"people",caption:"People",summary:"Four records",page_size:2,selection:'multiple',bordered:true,
    columns:[{key:'name',title:"Name",render:(value,row,index) => <strong value>},{key:'age',title:"Age",sortable:true},
        {key:'status',title:"Status",filters:[{value:'active',label:"Active"},{value:'paused',label:"Paused"}]}],
    rows:[{key:1,name:"Cora",age:30,status:'active',detail:<p id:"person-detail","Account details">},
        {key:2,name:"Ada",age:20,status:'active',disabled:true},{key:3,name:"Ben",age:25,status:'paused'},
        {key:4,name:"Dana",age:20,status:'active'}]})^
view dtna_table_test: <table_test> state last_action:"",chosen:"",page_number:0 {
    <div *[ui.render(people),<span id:"last",last_action>,<span id:"chosen",chosen>,<span id:"page",string(page_number)>]>
}
on ui_change(action) {
    last_action = string(action.action)
    if (action.action == 'select') { chosen = string(action.value.selected_keys) }
    if (contains(['sort','filter','page'],action.action)) { page_number = action.value.current }
}
ui.page(<table_test>)^
