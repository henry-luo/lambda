import ui: lambda.ui.dtna
let form_model = <dtna.form id:"project-form", *[
    <dtna.form_item label:"Name",for:"project-name",required:true, <dtna.input id:"project-name",name:"project",required:true>>,
    <dtna.space *[<dtna.button id:"submit",type:"submit",variant:'primary', "Create">,
        <dtna.button id:"reset",type:"reset", "Reset">]>
]>
view dtna_form_test: <form_test> state submitted:"", submissions:0 {
    <div *[apply(form_model),<span id:"submitted",submitted>,<span id:"submissions",string(submissions)>]>
}
on ui_action(action) {
    if (action.action == 'submit') {
        submitted = action.value[0][1]
        submissions = submissions + 1
    }
}
apply(<dtna.page <form_test>>)
