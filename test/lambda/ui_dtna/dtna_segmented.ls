import ui: lambda.ui.dtna
let items = [{key:1,label:"One",icon:<svg width:14,height:14>},{key:2,label:"Two",disabled:true},{key:3,label:"Three"}]
let rendered = ui.render(ui.segmented({id:"choices",items:items,orientation:'vertical',size:'large',block:true,shape:'round',name:"choice"})^)
let choice_list = content(rendered)[0]
let buttons = [for (child in content(choice_list) where child is element and name(child) == 'button') child]
let hidden = [for (child in content(rendered) where child is element and name(child) == 'input') child];
[
    ["group semantics",rendered.role == "radiogroup" and rendered["aria-orientation"] == "vertical"],
    ["size and shape",contains(rendered.class,"dtna-size-large") and contains(rendered.class,"dtna-segmented-round")],
    ["block",contains(rendered.class,"dtna-segmented-block")],
    ["one selected",buttons[0]["aria-checked"] == "true" and buttons[2]["aria-checked"] == "false"],
    ["roving focus",buttons[0].tabindex == "0" and buttons[2].tabindex == "-1"],
    ["disabled option",'disabled' at buttons[1] and not ('disabled' at buttons[0])],
    ["icon slot",name(content(content(buttons[0])[0])[0]) == 'svg'],
    ["native form entry",len(hidden) == 1 and hidden[0].name == "choice" and hidden[0].value == "1"],
    ["disabled form omission",'disabled' at [for (child in content(ui.render(ui.segmented({id:"off",items:items,name:"ignored",disabled:true})^)) where child is element and name(child) == 'input') child][0]],
    ["typed controlled value",(ui.segmented({id:"typed",items:items,value:3})^).value is int],
    ["invalid size",(ui.segmented({id:"bad",items:items,size:'huge'}) or null) == null],
    ["invalid shape",(ui.segmented({id:"bad",items:items,shape:'square'}) or null) == null],
    ["invalid block",(ui.segmented({id:"bad",items:items,block:1}) or null) == null],
    ["invalid name",(ui.segmented({id:"bad",items:items,name:1}) or null) == null],
    ["invalid orientation",(ui.segmented({id:"bad",items:items,orientation:'diagonal'}) or null) == null],
    ["controlled/default conflict",(ui.segmented({id:"bad",items:items,value:1,default_value:3}) or null) == null]
]
