import ui: lambda.ui.blade
import c: lambda.ui.core.component

fn empty_image(props) => content(content(ui.render(<blade.empty *:props>)^)[0])[0]
let source = <blade.page direction:'rtl',title:"Armory",<blade.panel title:"Inventory",<blade.item_slot icon:'sword',label:"Sword",selected:true>>>
let document = apply(source)
let root = content(content(document)[1])[0]
let panel = content(root)[0]
let slot = content(content(panel)[5])[0]
let health = ui.render(<blade.meter resource:'health',value:240,max:500,label:"Health">)^
let ability = ui.render(<blade.ability icon:'fire',hotkey:"Q",cooldown:40>)^
let provider = ui.render(<blade.config_provider tokens:{radius:8},disabled:true,<blade.input id:"locked">>)^;
[
    ["authored identity retained",name(source) == 'blade.page' and name(content(source)[0]) == 'blade.panel'],
    ["qualified shared constructor",ui.button({id:"save"},"Save")^ == <blade.button id:"save","Save">],
    ["qualified RPG constructor",ui.item_slot({icon:'sword'})^ == <blade.item_slot icon:'sword'>],
    ["document shell",name(document) == 'html' and document.dir == "rtl" and root.class == "dtna-root blade-root"],
    ["explicit null direction preserved",ui.page("",{direction:null})^.dir == ""],
    ["semantic panel",name(panel) == 'section' and contains(panel.class,"blade-material-wood")],
    ["slot is accessible native button",name(slot) == 'button' and slot["aria-label"] == "Sword" and slot["aria-pressed"] == "true"],
    ["meter semantics",health.role == "progressbar" and health["aria-valuenow"] == "240" and health["aria-valuemax"] == "500"],
    ["nullable resource uses default",ui.render(<blade.meter resource:null>)^["aria-label"] == "Health"],
    ["meter fills proportionally",content(content(content(health)[1])[0])[0].style == "width:48%;"],
    ["cooldown disables native activation",name(ability) == 'button' and c.has(map(ability),"disabled") and ability["data-cooldown"] == "40"],
    ["provider uses semantic fieldset",name(provider) == 'fieldset' and contains(provider.style,"--blade-radius:8px;") and c.has(map(provider),"disabled")],
    ["empty slots supported",ui.render(<blade.item_slot>)^["aria-label"] == "Empty equipment slot"],
    ["shared layout detects sider",ui.render(<blade.layout <blade.layout_sider "Side">>)^.class == "dtna-layout dtna-layout-has-sider"],
    ["ordinary native markup preserved",ui.render(<button "Native">)^ == <button "Native">],
    ["foreign names stay distinct",ui.render(<other.button>)^ == <other.button>],
    ["fantasy empty presets",all([for (preset in ['default','simple'])
        name(empty_image({image:preset})) == 'svg' and empty_image({image:preset}).class == "blade-empty-art"])],
    ["empty artwork stays decorative",empty_image({})["aria-hidden"] == "true"],
    ["simple empty height preserved",content(ui.render(<blade.empty image:'simple'>)^)[0].style == "height:40px;"],
    ["empty height override preserved",content(ui.render(<blade.empty image:'simple',image_height:72>)^)[0].style == "height:72px;"],
    ["hidden empty images preserved",all([for (value in [false,null]) len(content(ui.render(<blade.empty image:value,description:false>)^)) == 0])],
    ["custom empty URL preserved",empty_image({image:"chest.svg"}).src == "chest.svg"],
    ["custom empty element preserved",empty_image({image:<span "Custom">}) == <span "Custom">]
]
