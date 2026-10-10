import ui: lambda.ui.dtna
let plain = ui.render(ui.rate({})^)
let half = ui.render(ui.rate({value:2.5,allow_half:true,name:"rating"})^)
let locked = ui.render(ui.rate({value:3,disabled:true})^)
let readonly = ui.render(ui.rate({value:2,readonly:true})^)
let custom = ui.render(ui.rate({count:3,character:(star) => string(star.index + 1),tooltips:["Poor","Fair","Good"]})^);
[
    ["group semantics",plain.role == "radiogroup" and plain["data-value"] == "0"],
    ["default count",len(content(plain)) == 5],
    ["default empty",all([for (star in content(plain)) contains(star.class,"dtna-rate-zero") and star["aria-checked"] == "false"])],
    ["native activation",all([for (star in content(plain)) name(star) == 'button' and star.type == "button"])],
    ["positional labels",content(plain)[4]["aria-posinset"] == "5" and content(plain)[4]["aria-setsize"] == "5"],
    ["full stars",contains(content(half)[0].class,"dtna-rate-full") and contains(content(half)[1].class,"dtna-rate-full")],
    ["half star",contains(content(half)[2].class,"dtna-rate-half") and content(half)[2]["aria-checked"] == "true"],
    ["empty remainder",contains(content(half)[3].class,"dtna-rate-zero")],
    ["exact form text",content(half)[5].name == "rating" and content(half)[5].value == "2.5"],
    ["disabled",locked["aria-disabled"] == "true" and all([for (star in content(locked)) star.disabled == "" and star.tabindex == -1])],
    ["readonly",readonly["aria-readonly"] == "true" and all([for (star in content(readonly)) star.disabled == null and star.tabindex == -1])],
    ["initial value",(ui.render(ui.rate({default_value:4})^))["data-value"] == "4"],
    ["custom count",len(content(custom)) == 3],
    ["custom symbol function",content(content(content(custom)[2])[1])[0] == "3"],
    ["tooltips and labels",content(custom)[0].title == "Poor" and content(custom)[0]["aria-label"] == "Poor"],
    ["invalid count",(ui.rate({count:0}) or null) == null and (ui.rate({count:2.5}) or null) == null],
    ["invalid values",(ui.rate({value:-1}) or null) == null and (ui.rate({value:6}) or null) == null and (ui.rate({value:nan}) or null) == null],
    ["controlled pair",(ui.rate({value:1,default_value:2}) or null) == null],
    ["invalid flags",(ui.rate({allow_half:null}) or null) == null and (ui.rate({readonly:1}) or null) == null],
    ["invalid tooltips",(ui.rate({tooltips:[1]}) or null) == null and (ui.rate({tooltips:"bad"}) or null) == null],
    ["unknown prop",(ui.rate({unknown:true}) or null) == null],
    ["size",contains((ui.render(ui.rate({size:'small'})^)).class,"dtna-size-small") and (ui.rate({size:'huge'}) or null) == null]
]
