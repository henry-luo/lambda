import ui: lambda.ui.dtna
import responsive: lambda.ui.core.responsive

let hidden = ui.render(ui.badge({count:0},"Inbox")^)
let zero = ui.render(ui.badge({count:0,show_zero:true})^)
let overflow = ui.render(ui.badge({count:120,overflow_count:99})^)
let status = ui.render(ui.badge({status:'processing',text:"Working"})^)
let ribbon = ui.render(ui.badge_ribbon({placement:'start',text:"New",color:"purple"},"Card")^)
let descriptions = ui.render(ui.descriptions({column:{xs:1,md:3},bordered:true,items:[
    ui.description_item({label:"First"},"One")^,
    ui.description_item({label:"Fill",span:'filled'},"Two")^,
    {label:"Wide",span:2,children:"Three"},{label:"Last",children:"Four"}]})^)
let cells = content(content(descriptions)[0]);
[
    ["zero hidden",content(hidden) == ["Inbox"]],
    ["zero shown",content(content(zero)[0])[0] == "0"],
    ["overflow accessible count",content(content(overflow)[0])[0] == "99+" and content(overflow)[0]["aria-label"] == "120"],
    ["status text",contains(status.class,"dtna-badge-status") and content(content(status)[1])[0] == "Working"],
    ["ribbon logical placement",contains(ribbon.class,"dtna-ribbon-start") and content(content(ribbon)[1])[0] == "New"],
    ["description item",ui.description_item({label:"Value"},false)^.children == false],
    ["filled responsive row",contains(cells[1].style,"--dtna-description-span-md:span 2;") and contains(cells[1].style,"--dtna-description-span-xs:span 1;")],
    ["numeric span clamped",contains(cells[2].style,"--dtna-description-span-xs:span 1;") and contains(cells[2].style,"--dtna-description-span-md:span 2;")],
    ["last cell fills row",contains(cells[3].style,"--dtna-description-span-sm:span 1;") and contains(cells[3].style,"--dtna-description-span-md:span 1;")],
    ["responsive inheritance",responsive.at({sm:2,lg:4},"md",1) == 2 and responsive.at({sm:2},"xs",1) == 1],
    ["badge flags validated",(ui.badge({show_zero:1}) or null) == null and (ui.badge({dot:1}) or null) == null],
    ["badge offset validated",(ui.badge({offset:[1]}) or null) == null and (ui.badge({offset:[1,nan]}) or null) == null],
    ["badge count validated",(ui.badge({count:-1}) or null) == null and (ui.badge({overflow_count:inf}) or null) == null],
    ["ribbon validated",(ui.badge_ribbon({placement:'left'}) or null) == null and (ui.badge_ribbon({color:"red;display:none"}) or null) == null],
    ["description columns validated",(ui.descriptions({column:0,items:[]}) or null) == null and (ui.descriptions({column:{tablet:2},items:[]}) or null) == null and (ui.descriptions({column:{},items:[]}) or null) == null],
    ["description items validated",(ui.descriptions({items:[{span:0}]}) or null) == null and (ui.descriptions({items:[{bogus:1}]}) or null) == null and (ui.description_item({span:0},"Value") or null) == null],
    ["description layout validated",(ui.descriptions({items:[],layout:'grid'}) or null) == null and (ui.descriptions({items:[],bordered:1}) or null) == null]
]
