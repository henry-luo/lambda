import ui: lambda.ui.dtna
import icons: lambda.ui.dtna.icons
let outlined = ui.render(ui.icon({name:"check-circle",label:"Complete",rotate:90,width:24,height:18})^)
let filled = ui.render(ui.icon({name:"check-circle",theme:'filled'})^)
let two_tone = ui.render(ui.icon({name:"check-circle",theme:'two-tone',primary_color:"#722ed1",secondary_color:"#f9f0ff"})^)
let group = ui.render(ui.icon({name:"dot-net"})^)
let variants = icons.variants()
let rendered = [for (variant in variants) ui.render(ui.icon(variant)^)];
[
    ["pinned names",len(ui.icon_names()) == 498 and contains(ui.icon_names(),"account-book") and contains(ui.icon_names(),"dot-net")],
    ["all variants render",len(rendered) == 848 and all([for (node in rendered) node is element and name(node) == 'svg' and len(content(node)) > 0])],
    ["named icon",outlined.role == "img" and outlined["aria-label"] == "Complete" and outlined["aria-hidden"] == null],
    ["decorative icon",filled.role == null and filled["aria-hidden"] == "true"],
    ["size and rotation",contains(outlined.style,"width:24px;") and contains(outlined.style,"height:18px;") and contains(outlined.style,"transform:rotate(90deg);")],
    ["filled geometry",content(filled)[0].d != content(outlined)[0].d],
    ["two-tone colors",content(two_tone)[0].fill == "#722ed1" and content(two_tone)[1].fill == "#f9f0ff"],
    ["automatic secondary color",content(ui.render(ui.icon({name:"check-circle",theme:'two-tone',primary_color:"#722ed1"})^))[1].fill == "#f9f0ff"],
    ["explicit named colors",content(ui.render(ui.icon({name:"check-circle",theme:'two-tone',primary_color:"purple",secondary_color:"white"})^))[0].fill == "purple"],
    ["group attributes",name(content(group)[0]) == 'g' and content(group)[0]["fill-opacity"] == ".88"],
    ["legacy alias",ui.render(ui.icon({name:"chevron-down"})^).viewBox == ui.render(ui.icon({name:"down"})^).viewBox],
    ["spinner",contains(ui.render(ui.icon({name:"loading"})^).class,"dtna-icon-spin") and contains(ui.render(ui.icon({name:"sync",spin:true})^).class,"dtna-icon-spin")],
    ["invalid catalog choices",(ui.icon({name:"missing"}) or null) == null and (ui.icon({theme:'missing'}) or null) == null and (ui.icon({name:"check",theme:'two-tone'}) or null) == null],
    ["invalid icon props",(ui.icon({rotate:inf}) or null) == null and (ui.icon({spin:1}) or null) == null and (ui.icon({width:0}) or null) == null and (ui.icon({primary_color:"red;fill:none"}) or null) == null]
]
