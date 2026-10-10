import ui: lambda.ui.dtna
let ordinary = ui.render(ui.button({id:"plain",["aria-label"]:"Save"},"Save")^)
let loading = ui.render(ui.button({loading:true},"Loading")^)
let delayed = ui.render(ui.button({loading:{delay:300}},"Wait")^)
let custom = ui.render(ui.button({loading:{icon:<b "L">}},"Custom")^)
let link = ui.render(ui.button({href:"#target",target:"_blank",rel:"noopener",download:"file"},"Link")^)
let disabled = ui.render(ui.button({href:"#target",disabled:true},"Disabled")^)
let parts = ui.render(ui.button({icon:<i "I">,icon_placement:'end',class_names:{root:"authored",icon:"image",content:"caption"},
    styles:{root:"margin:1px;",content:"color:red;"},style:"margin:2px;"},"Parts")^)
let cn = ui.render(ui.button({},"按钮")^)
let exact_cn = ui.render(ui.button({auto_insert_space:false},"按钮")^);
[
    name(ordinary) == 'button',ordinary.type == "button",ordinary["aria-label"] == "Save",
    content(content(ordinary)[0])[0] == "Save",ordinary["aria-busy"] == "false",
    loading["aria-busy"] == "true",loading["aria-disabled"] == "true",
    delayed["aria-busy"] == "false",name(content(content(custom)[0])[0]) == 'b',
    name(link) == 'a',link.href == "#target",link.target == "_blank",link.rel == "noopener",link.download == "file",
    disabled.href == null,disabled.tabindex == -1,disabled["aria-disabled"] == "true",
    contains(parts.class,"authored"),contains(parts.class,"dtna-button-icon-end"),parts.style == "margin:1px;margin:2px;",
    contains(content(parts)[0].class,"image"),content(parts)[1].style == "color:red;",
    content(content(cn)[0])[0] == "按 钮",content(content(exact_cn)[0])[0] == "按钮",
    (ui.button({loading:{delay:-1}}) or null) == null,(ui.button({loading:{delay:inf}}) or null) == null,
    (ui.button({loading:{delay:"soon"}}) or null) == null,(ui.button({loading:{unknown:true}}) or null) == null,
    (ui.button({icon_placement:'above'}) or null) == null,(ui.button({styles:{caption:"color:red;"}}) or null) == null,
    (ui.button({href:1}) or null) == null,(ui.button({auto_focus:1}) or null) == null,
    contains(map(ui.render(ui.button({auto_focus:true})^)),'autofocus'),
    contains(ui.render(ui.button({shape:'square'})^).class,"dtna-shape-square"),
    ui.render(ui.button({type:'submit'})^).type == "submit",ui.render(ui.input({type:'email'})^).type == "email"
]
