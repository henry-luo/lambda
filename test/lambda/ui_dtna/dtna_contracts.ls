import ui: lambda.ui.dtna
import c: lambda.ui.core.component
let primary = ui.render(ui.button({id:"ok",variant:'primary','aria-label':"Save"}, "Save")^)
let field = ui.render(ui.input({id:"field",value:""})^)
let check = ui.render(ui.checkbox({id:"check",default_checked:false}, "Agree")^)
let checked = ui.render(ui.checkbox({id:"checked",default_checked:true}, "Agree")^);
[
    name(primary), primary.id, primary.class, primary["aria-label"], content(content(primary)[0])[0],
    not contains(map(field), 'disabled'), not contains(map(field), 'readonly'), field.value,
    not contains(map(content(check)[0]), 'checked'), contains(map(content(checked)[0]), 'checked'),
    (ui.button({unknown:true}) or null) == null,
    (ui.button({size:'tiny'}) or null) == null,
    (ui.button({loading:"yes"}) or null) == null,
    (ui.input({value:"x",default_value:"y"}) or null) == null,
    (ui.checkbox({checked:false,default_checked:true}) or null) == null,
    (ui.tokens({unknown:1}) or null) == null,
    (ui.tokens({primary:"red"}) or null) == null,
    (ui.tokens({font_size:0}) or null) == null,
    (ui.text_area({rows:1.5}) or null) == null,
    (ui.input({maxlength:2.5}) or null) == null,
    ui.render(ui.input({maxlength:0})^).maxlength == 0,
    (ui.config_provider({direction:'vertical'}) or null) == null,
    contains(ui.render(ui.space({direction:"vertical"})^).style,"flex-direction:column"),
    contains(ui.render(ui.tabs({id:"vertical",orientation:"vertical",items:[{key:1,label:"One"}]})^).class,"dtna-vertical"),
    c.action(ui.button({id:"a"})^, 'click')
]
