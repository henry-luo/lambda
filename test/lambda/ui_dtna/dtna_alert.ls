import ui: lambda.ui.dtna
let plain = ui.render(ui.alert({message:"Info"})^)
let banner = ui.render(ui.alert({banner:true,message:"Banner"})^)
let detail = ui.render(ui.alert({title:0,description:"Detail",status:'success',show_icon:true,closable:true,action:"Retry"})^)
let parts = ui.render(ui.alert({message:"Styled",class:"authored",style:"opacity:0.8;",class_names:{root:"root-extra",title:"title-extra",close:"close-extra"},
    styles:{root:"color:red;",title:"font-weight:700;"},close_icon:"Done",close_label:"Dismiss notice"})^);
[
    ["live alert",name(plain) == 'div' and plain.role == "alert" and plain["data-show"] == "true"],
    ["default info outlined",contains(plain.class,"dtna-status-info") and contains(plain.class,"dtna-alert-outlined")],
    ["message compatibility",content(content(content(plain)[0])[0])[0] == "Info"],
    ["no icon by default",len(content(plain)) == 1],
    ["banner warning",contains(banner.class,"dtna-alert-banner") and contains(banner.class,"dtna-status-warning")],
    ["banner default icon",content(banner)[0].class == "dtna-alert-icon" and content(banner)[0]["aria-hidden"] == "true"],
    ["banner icon opt out",len(content(ui.render(ui.alert({banner:true,show_icon:false})^))) == 1],
    ["status override",contains((ui.render(ui.alert({banner:true,status:'error'})^)).class,"dtna-status-error")],
    ["description variant",contains(detail.class,"dtna-alert-with-description")],
    ["numeric title",content(content(content(detail)[1])[0])[0] == "0"],
    ["description content",content(content(content(detail)[1])[1])[0] == "Detail"],
    ["action slot",content(content(detail)[2])[0] == "Retry"],
    ["native close",name(content(detail)[3]) == 'button' and content(detail)[3].type == "button" and content(detail)[3]["aria-label"] == "Close"],
    ["filled variant",contains((ui.render(ui.alert({variant:'filled'})^)).class,"dtna-alert-filled")],
    ["custom icon",content(content(ui.render(ui.alert({show_icon:true,icon:"!"})^))[0])[0] == "!"],
    ["custom close implies closable",content(content(parts)[1])[0] == "Done" and content(parts)[1]["aria-label"] == "Dismiss notice"],
    ["close opt out",len(content(ui.render(ui.alert({close_icon:"Done",closable:false})^))) == 1],
    ["root semantic merge",contains(parts.class,"authored") and contains(parts.class,"root-extra") and parts.style == "color:red;opacity:0.8;"],
    ["title semantic part",content(content(parts)[0])[0].class == "dtna-alert-title title-extra" and content(content(parts)[0])[0].style == "font-weight:700;"],
    ["close semantic part",contains(content(parts)[1].class,"close-extra")],
    ["controlled hidden",(ui.render(ui.alert({visible:false})^)).hidden == ""],
    ["initial hidden",(ui.render(ui.alert({default_visible:false})^)).hidden == ""],
    ["explicit null title",len(content(content(ui.render(ui.alert({title:null,message:"Not chosen"})^))[0])) == 0],
    ["invalid visibility pair",(ui.alert({visible:true,default_visible:true}) or null) == null],
    ["invalid flags",(ui.alert({show_icon:null}) or null) == null and (ui.alert({banner:1}) or null) == null and (ui.alert({closable:1}) or null) == null],
    ["invalid status or variant",(ui.alert({status:'processing'}) or null) == null and (ui.alert({variant:'solid'}) or null) == null and
        (ui.alert({status:null}) or null) == null and (ui.alert({variant:null}) or null) == null],
    ["invalid semantic slots",(ui.alert({styles:{typo:"color:red;"}}) or null) == null and (ui.alert({class_names:{root:4}}) or null) == null],
    ["invalid close label",(ui.alert({close_label:4}) or null) == null],
    ["unknown prop",(ui.alert({unknown:true}) or null) == null]
]
