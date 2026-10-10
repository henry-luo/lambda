import ui: lambda.ui.dtna
import t: lambda.ui.dtna.tokens
let decorated = ui.render(ui.text({strong:true,italic:true,underline:true,delete:true,code:true,mark:true,keyboard:true},"Text")^)
let disabled = ui.render(ui.link({disabled:true,href:"#target"},"Disabled")^)
let external = ui.render(ui.link({href:"https://example.test",target:"_blank"},"External")^);
[
    ["heading hierarchy",[for (level in 1 to 5) name(ui.render(ui.title({level:level},"Title")^))] == ['h1','h2','h3','h4','h5']],
    ["heading size",contains(ui.render(ui.title({level:1},"Title")^).style,"font-size:var(--dtna-font-size-heading-1);")],
    ["decorations",name(content(decorated)[0]) == 'kbd' and name(content(content(decorated)[0])[0]) == 'mark'],
    ["status type",contains(ui.render(ui.paragraph({type:'danger'},"Danger")^).class,"dtna-text-danger")],
    ["disabled link",not ('href' at disabled) and disabled.tabindex == "-1" and disabled["aria-disabled"] == "true"],
    ["external link",external.rel == "noopener noreferrer" and external.target == "_blank"],
    ["explicit link policy",ui.render(ui.link({rel:"author",download:"report.txt"},"Download")^).rel == "author"],
    ["invalid flags",(ui.text({strong:1}) or null) == null and (ui.text({mark:"yellow"}) or null) == null],
    ["invalid heading",(ui.title({level:0}) or null) == null and (ui.title({level:6}) or null) == null],
    ["invalid type",(ui.text({type:'primary'}) or null) == null],
    ["default font scale",[for (level in 1 to 5) (ui.tokens()^)["font_size_heading_" ++ string(level)]] == [38,30,24,20,16]],
    ["large font scale",[for (level in 1 to 5) (ui.tokens({font_size:20})^)["font_size_heading_" ++ string(level)]] == [54,44,36,28,24]],
    ["small font scale",[for (level in 1 to 5) (ui.tokens({font_size:12})^)["font_size_heading_" ++ string(level)]] == [32,26,20,16,14]],
    ["derived line boxes",[for (level in 1 to 5) (ui.tokens({font_size:16})^)["font_height_heading_" ++ string(level)]] == [50,42,36,30,26]],
    ["scoped heading variables",contains(t.scoped_variables({font_size:20})^,"--dtna-font-size-heading-1:54px;") and
        contains(t.scoped_variables({font_size:20})^,"--dtna-font-height-heading-5:32px;")],
    ["unrelated scope inheritance",not contains(t.scoped_variables({radius:0})^,"font-size-heading")],
    ["derived seeds immutable",(ui.tokens({font_size_heading_1:40}) or null) == null],
    ["derived overflow rejected",(ui.tokens({font_size:1.0e308}) or null) == null],
    ["body line height derived",(ui.tokens({font_size:20})^).line_height == 1.4],
    ["explicit line height",(ui.tokens({font_size:20,line_height:1.8})^).line_height == 1.8]
]
