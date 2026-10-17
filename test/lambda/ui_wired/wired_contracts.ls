import ui: lambda.ui.wired

[
    ["unknown prop",((ui.button({unknown:true}) ^ { null }) or null) == null],
    ["map props",((ui.input("bad") ^ { null }) or null) == null],
    ["theme variants",((ui.button({variant:'secondary'}) ^ { null }) or null) == null],
    ["disabled bool",((ui.button({disabled:1}) ^ { null }) or null) == null],
    ["input type",((ui.input({type:'calendar'}) ^ { null }) or null) == null],
    ["button type",((ui.button({type:'menu'}) ^ { null }) or null) == null],
    ["value ownership",((ui.input({value:null,default_value:"x"}) ^ { null }) or null) == null],
    ["checked ownership",((ui.checkbox({checked:false,default_checked:true}) ^ { null }) or null) == null],
    ["rows positive",((ui.text_area({rows:0}) ^ { null }) or null) == null],
    ["maxlength integral",((ui.input({maxlength:1.5}) ^ { null }) or null) == null],
    ["heading range",((ui.title({level:7}) ^ { null }) or null) == null],
    ["progress finite range",all([for (value in [-1,101,nan,inf]) ((ui.progress({percent:value}) ^ { null }) or null) == null])],
    ["gap validated",all([for (value in [-1,"1px;color:red",inf]) ((ui.space({gap:value}) ^ { null }) or null) == null])],
    ["elevation bounds",all([for (value in [0,6,1.5,null]) ((ui.card({elevation:value}) ^ { null }) or null) == null])],
    ["seed bounds",all([for (value in [-1,2147483647,1.5,null]) ((ui.button({seed:value}) ^ { null }) or null) == null])],
    ["ambiguous select values",((ui.select({options:[{value:1,label:"One"},{value:"1",label:"Also one"}]}) ^ { null }) or null) == null],
    ["direct element validation",((ui.render(<wired.button elevation:8>) ^ { null }) or null) == null],
    ["unknown component",((ui.render(<wired.unknown>) ^ { null }) or null) == null],
    ["page direction",((ui.page("x",{direction:'up'}) ^ { null }) or null) == null],
    ["family diagnostic",(ui.button({unknown:true}) ^ {starts_with(^.message,"wired: button:")}) == true]
]
