import ui: lambda.ui.dtna
import colors: lambda.ui.dtna.tokens
let plain = ui.render(ui.tag({},"Plain")^)
let blue = ui.render(ui.tag({color:'blue',variant:'outlined'},"Blue")^)
let custom = ui.render(ui.tag({color:"#108ee9"},"Custom")^)
let link = ui.render(ui.tag({href:"#target",target:"_blank"},"Link")^)
let checked = ui.render(ui.checkable_tag({checked:true},"Selected")^)
let disabled = ui.render(ui.tag({href:"#target",disabled:true,color:'red',closable:true},"Disabled")^);
[
    ["plain inline tag",name(plain) == 'span' and content(content(plain)[0])[0] == "Plain"],
    ["default filled",contains(plain.class,"dtna-tag-filled")],
    ["preset palette",contains(blue.style,"color:#0958d9;") and contains(blue.style,"background-color:#e6f4ff;") and contains(blue.style,"border-color:#91caff;")],
    ["custom light background",contains(custom.style,"color:#108ee9;") and contains(custom.style,"background-color:#e7f4fd;")],
    ["semantic colors",contains((ui.render(ui.tag({color:'success'},"OK")^)).style,"var(--dtna-success-background)")],
    ["legacy status",contains((ui.render(ui.tag({status:'error'},"Error")^)).style,"var(--dtna-error)")],
    ["solid color",contains((ui.render(ui.tag({color:'red',variant:'solid'},"Red")^)).style,"background-color:#f5222d;")],
    ["outlined compatibility",contains((ui.render(ui.tag({bordered:true})^)).class,"dtna-tag-outlined")],
    ["native link",name(link) == 'a' and link.href == "#target" and link.target == "_blank"],
    ["disabled link",disabled.href == null and disabled["aria-disabled"] == "true" and disabled.style == ""],
    ["native close button",name(content(disabled)[1]) == 'button' and content(disabled)[1].disabled == ""],
    ["checkable semantics",name(checked) == 'button' and checked.role == "checkbox" and checked["aria-checked"] == "true"],
    ["uncontrolled selection",(ui.render(ui.checkable_tag({default_checked:true})^))["aria-checked"] == "true"],
    ["controlled hidden",(ui.render(ui.tag({visible:false},"Hidden")^)).hidden == ""],
    ["initial hidden",(ui.render(ui.tag({default_visible:false},"Hidden")^)).hidden == ""],
    ["invalid controlled pairs",(ui.checkable_tag({checked:false,default_checked:true}) or null) == null and (ui.tag({visible:true,default_visible:true}) or null) == null],
    ["invalid flags",(ui.tag({visible:null}) or null) == null and (ui.checkable_tag({checked:1}) or null) == null],
    ["unknown props",(ui.tag({unknown:true}) or null) == null and (ui.checkable_tag({closable:true}) or null) == null],
    ["color diagnostic",(ui.tag({color:"red;opacity:0"}) or null) == null and (ui.tag({color:"rebeccapurple"}) or null) == null],
    ["solid CSS color",contains((ui.render(ui.tag({color:"rebeccapurple",variant:'solid'})^)).style,"background-color:rebeccapurple;")],
    ["invalid variant",(ui.tag({variant:'primary'}) or null) == null],
    ["grayscale lightness",colors.color_lightness("#000000",0.95)^ == "#f2f2f2" and colors.color_lightness("#ffffff",0.95)^ == "#f2f2f2"]
]
