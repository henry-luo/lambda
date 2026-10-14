import ui: lambda.ui.wired

[
    ["unknown prop",(ui.button({unknown:true}) or null) == null],
    ["map props",(ui.input("bad") or null) == null],
    ["theme variants",(ui.button({variant:'secondary'}) or null) == null],
    ["disabled bool",(ui.button({disabled:1}) or null) == null],
    ["input type",(ui.input({type:'calendar'}) or null) == null],
    ["button type",(ui.button({type:'menu'}) or null) == null],
    ["value ownership",(ui.input({value:null,default_value:"x"}) or null) == null],
    ["checked ownership",(ui.checkbox({checked:false,default_checked:true}) or null) == null],
    ["rows positive",(ui.text_area({rows:0}) or null) == null],
    ["maxlength integral",(ui.input({maxlength:1.5}) or null) == null],
    ["heading range",(ui.title({level:7}) or null) == null],
    ["progress finite range",all([for (value in [-1,101,nan,inf]) (ui.progress({percent:value}) or null) == null])],
    ["gap validated",all([for (value in [-1,"1px;color:red",inf]) (ui.space({gap:value}) or null) == null])],
    ["elevation bounds",all([for (value in [0,6,1.5,null]) (ui.card({elevation:value}) or null) == null])],
    ["seed bounds",all([for (value in [-1,2147483647,1.5,null]) (ui.button({seed:value}) or null) == null])],
    ["ambiguous select values",(ui.select({options:[{value:1,label:"One"},{value:"1",label:"Also one"}]}) or null) == null],
    ["direct element validation",(ui.render(<wired.button elevation:8>) or null) == null],
    ["unknown component",(ui.render(<wired.unknown>) or null) == null],
    ["page direction",(ui.page("x",{direction:'up'}) or null) == null],
    ["family diagnostic",(ui.button({unknown:true}) ^ {starts_with(^.message,"wired: button:")}) == true]
]
