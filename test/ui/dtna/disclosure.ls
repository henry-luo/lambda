import ui: lambda.ui.dtna
let nested = <dtna.collapse id:"nested",items:[{key:'inner',label:"Inner",children:"Nested body"}]>
let disclosure = <dtna.collapse id:"disclosure",default_active_keys:['a'],items:[
    {key:'a',label:"Account",children:<dtna.input id:"retained-name",default_value:"Ada">},
    {key:'blocked',label:"Disabled",disabled:true,children:"Unavailable"},
    {key:'c',label:"Details",children:nested}
]>
let accordion = <dtna.collapse id:"accordion",accordion:true,items:[{key:1,label:"One",children:"First"},{key:2,label:"Two",children:"Second"}]>
let tabs = <dtna.tabs id:"retained-tabs",keep_mounted:true,items:[
    {key:'first',label:"First",children:<dtna.input id:"tab-input",default_value:"X">},
    {key:'second',label:"Second",children:"Other"}]>
view dtna_disclosure_test: <disclosure_test> state last_action:"" {
    <div *[apply(disclosure),apply(accordion),apply(tabs),<span id:"last",last_action>]>
}
on ui_change(action) { last_action = action.id ++ ":" ++ string(action.action) }
apply(<dtna.page <disclosure_test>>)
