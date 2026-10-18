import ui: lambda.ui.clay

[
    ["unknown prop",((ui.button({unknown:true}) ^ { null }) or null) == null],
    ["map props",((ui.input("bad") ^ { null }) or null) == null],
    ["button variant",((ui.button({variant:'unknown'}) ^ { null }) or null) == null],
    ["boolean flags",((ui.button({disabled:1}) ^ { null }) or null) == null],
    ["explicit null ownership",((ui.input({value:null,default_value:"x"}) ^ { null }) or null) == null],
    ["checked ownership",((ui.checkbox({checked:false,default_checked:true}) ^ { null }) or null) == null],
    ["select typed uniqueness",((ui.select({options:[{value:1,label:"One"},{value:"1",label:"Also one"}]}) ^ { null }) or null) == null],
    ["tabs identity",((ui.tabs({items:[{key:1,label:"One"}]}) ^ { null }) or null) == null],
    ["tabs key uniqueness",((ui.tabs({id:"tabs",items:[{key:1},{key:1}]}) ^ { null }) or null) == null],
    ["tree key ownership",((ui.tree({id:"tree",items:[{key:1}],checked_keys:[2]}) ^ { null }) or null) == null],
    ["table page size",((ui.table({id:"table",rows:[],columns:[{key:'name'}],page_size:0}) ^ { null }) or null) == null],
    ["collapse controlled/default",((ui.collapse({id:"collapse",items:[],active_keys:[],default_active_keys:[]}) ^ { null }) or null) == null],
    ["rating range",((ui.rate({value:6}) ^ { null }) or null) == null],
    ["page unknown",((ui.page("x",{unknown:1}) ^ { null }) or null) == null],
    ["page direction",((ui.page("x",{direction:'up'}) ^ { null }) or null) == null],
    ["provider bool",((ui.config_provider({disabled:1}) ^ { null }) or null) == null],
    ["provider direction",((ui.config_provider({direction:'vertical'}) ^ { null }) or null) == null],
    ["provider unknown tokens",((ui.config_provider({tokens:{missing:2}}) ^ { null }) or null) == null],
    ["direct elements validate",((ui.render(<clay.button unknown:true>) ^ { null }) or null) == null],
    ["array errors propagate",((ui.render([<clay.button unknown:true>]) ^ { null }) or null) == null],
    ["page child errors propagate",((ui.page(<clay.button unknown:true>) ^ { null }) or null) == null],
    ["direct nested slots validate",((ui.render(<clay.tabs id:"tabs",items:[{key:1,label:"One",children:<clay.input type:'bad'>}]>) ^ { null }) or null) == null],
    ["leaf content rejected",((ui.render(<clay.input "unexpected">) ^ { null }) or null) == null],
    ["family diagnostic",(ui.button({unknown:true}) ^ {starts_with(^.message,"clay: button:")}) == true]
]
