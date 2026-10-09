import ui: lambda.ui.dtna
let form_model = ui.form({id:"project-form"},[
    ui.form_item({label:"Name",for:"project-name",required:true},
        ui.input({id:"project-name",name:"project",required:true})^)^,
    ui.space({},[ui.button({id:"submit",type:"submit",variant:'primary'},"Create")^,
        ui.button({id:"reset",type:"reset"},"Reset")^])^
])^
view dtna_form_test: <form_test> state submitted:"", submissions:0 {
    <div *[ui.render(form_model),<span id:"submitted",submitted>,<span id:"submissions",string(submissions)>]>
}
on ui_action(action) {
    if (action.action == 'submit') {
        submitted = action.value[0][1]
        submissions = submissions + 1
    }
}
ui.page(<form_test>)^
