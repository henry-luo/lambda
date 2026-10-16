import ui: lambda.ui.bold

[
    ["unknown props",((ui.button({unknown:true}) ^ { null }) or null) == null],
    ["map props",((ui.button("invalid") ^ { null }) or null) == null],
    ["button variants",((ui.button({variant:'primary'}) ^ { null }) or null) == null],
    ["boolean disabled",((ui.button({disabled:1}) ^ { null }) or null) == null],
    ["native button type",((ui.button({type:'menu'}) ^ { null }) or null) == null],
    ["control sizes",((ui.input({size:'giant'}) ^ { null }) or null) == null],
    ["native input type",((ui.input({type:'calendar'}) ^ { null }) or null) == null],
    ["value ownership",((ui.input({value:null,default_value:"x"}) ^ { null }) or null) == null],
    ["checked ownership",((ui.checkbox({checked:false,default_checked:true}) ^ { null }) or null) == null],
    ["positive rows",((ui.text_area({rows:0}) ^ { null }) or null) == null],
    ["integral maxlength",((ui.input({maxlength:1.5}) ^ { null }) or null) == null],
    ["heading range",((ui.title({level:7}) ^ { null }) or null) == null],
    ["finite progress",all([for (value in [-1,101,inf,nan]) ((ui.progress({percent:value}) ^ { null }) or null) == null])],
    ["CSS gap validation",all([for (value in [-1,"1px;color:red","calc(2px)",inf]) ((ui.space({gap:value}) ^ { null }) or null) == null])],
    ["flex alignment",((ui.flex({align:"unrecognized"}) ^ { null }) or null) == null],
    ["page direction",((ui.page("x",{direction:'up'}) ^ { null }) or null) == null],
    ["select option values",((ui.select({options:[{value:1,label:"A"},{value:"1",label:"B"}]}) ^ { null }) or null) == null],
    ["select option flags",((ui.select({options:[{value:'a',disabled:"yes"}]}) ^ { null }) or null) == null],
    ["native attribute types",((ui.button({href:42}) ^ { null }) or null) == null],
    ["direct elements validated",((ui.render(<bold.button variant:'unknown'>) ^ { null }) or null) == null],
    ["unknown component rejected",((ui.render(<bold kind:'unimplemented'>) ^ { null }) or null) == null],
    ["family diagnostic",(ui.button({unknown:true}) ^ {starts_with(^.message,"bold: button:")}) == true]
]
