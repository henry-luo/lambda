import ui: lambda.ui.dtna
let plain = ui.render(ui.spin()^)
let idle = ui.render(ui.spin({spinning:false},"Content")^)
let delayed = ui.render(ui.spin({delay:500})^)
let nested = ui.render(ui.spin({description:"Loading"},<button "Action">)^)
let progress = ui.render(ui.spin({percent:37})^)
let styled = ui.render(ui.spin({class_names:{root:"root-extra",section:"section-extra",indicator:"indicator-extra"},
    styles:{root:"opacity:0.8;",section:"color:red;",indicator:"color:green;"},style:"margin:2px;"})^)
fn custom(snapshot) => <b string(snapshot.size) ++ ":" ++ string(snapshot.percent) ++ ":" ++ string(snapshot.spinning)>
let authored = ui.render(ui.spin({size:'large',percent:25,indicator:custom})^);
[
    ["live status",name(plain) == 'div' and plain.role == "status" and plain["aria-live"] == "polite" and plain["aria-busy"] == "true"],
    ["four dot indicator",len(content(content(content(plain)[0])[0])) == 4],
    ["decorative indicator",content(plain)[0]["aria-hidden"] == "true"],
    ["idle preserves content",idle["aria-busy"] == "false" and len(content(idle)) == 1 and content(content(idle)[0])[0] == "Content"],
    ["delay initial state",delayed["aria-busy"] == "false" and len(content(delayed)) == 0],
    ["nested indicator and content",len(content(nested)) == 2 and content(nested)[0].class == "dtna-spin-section" and name(content(content(nested)[1])[0]) == 'button'],
    ["description",content(content(content(nested)[0])[1])[0] == "Loading"],
    ["tip alias",content(content(ui.render(ui.spin({tip:"Wait"})^))[1])[0] == "Wait"],
    ["explicit null description",len(content(ui.render(ui.spin({description:null,tip:"Not chosen"})^))) == 1],
    ["manual percent",content(content(progress)[0])[0]["aria-valuenow"] == "37"],
    ["percent clamps",content(content(ui.render(ui.spin({percent:140})^))[0])[0]["aria-valuenow"] == "100"],
    ["negative percent uses dots",len(content(content(content(ui.render(ui.spin({percent:-5})^))[0])[0])) == 4],
    ["auto starts with dots",len(content(content(content(ui.render(ui.spin({percent:'auto'})^))[0])[0])) == 4],
    ["custom indicator function",content(content(content(authored)[0])[0])[0] == "large:25:true"],
    ["custom indicator element",name(content(content(ui.render(ui.spin({indicator:<b "Custom">})^))[0])[0]) == 'b'],
    ["fullscreen section",contains((ui.render(ui.spin({fullscreen:true})^)).class,"dtna-spin-fullscreen") and content(ui.render(ui.spin({fullscreen:true})^))[0].class == "dtna-spin-section"],
    ["semantic root and section",contains(styled.class,"root-extra") and contains(styled.class,"section-extra") and styled.style == "opacity:0.8;color:red;margin:2px;"],
    ["semantic indicator",content(styled)[0].class == "dtna-spin-indicator indicator-extra" and content(styled)[0].style == "color:green;"],
    ["invalid flags",(ui.spin({spinning:null}) or null) == null and (ui.spin({fullscreen:1}) or null) == null],
    ["invalid delay",(ui.spin({delay:-1}) or null) == null and (ui.spin({delay:inf}) or null) == null and (ui.spin({delay:"20"}) or null) == null],
    ["invalid percent",(ui.spin({percent:nan}) or null) == null and (ui.spin({percent:null}) or null) == null and (ui.spin({percent:'automatic'}) or null) == null],
    ["invalid size",(ui.spin({size:'giant'}) or null) == null],
    ["invalid semantic part",(ui.spin({styles:{unknown:"color:red;"}}) or null) == null],
    ["unknown prop",(ui.spin({unknown:true}) or null) == null]
]
