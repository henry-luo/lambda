import controls: lambda.ui.controls
import binding: lambda.ui.core.binding
import c: lambda.ui.core.component

let field = apply(controls.input({id:"query",value:"",on_change:"find.query"})^)
let choice = apply(controls.select({id:"size",value:"18",options:[{value:11,label:"Small"},{value:18,label:"Large"}],
    on_change:{action:"format.size",param:"size",params:{source:"ribbon"}}})^)
let checkbox = apply(controls.checkbox({checked:false,on_change:"toggle"},"Toggle")^)
let request = binding.request({action:"format.size",param:"size",params:{source:"ribbon",size:11}},"size",18);
let fractional = apply(controls.select({value:1.5,options:[{value:1.15,label:"Tight"},{value:1.5,label:"Normal"}]})^);
[
    name(field) == 'input', field.id == "query", field.value == "",
    not c.has(map(field),"on_change"), not c.has(map(field),"readonly"),
    name(choice) == 'select', c.has(map(content(choice)[1]),"selected"),
    not c.has(map(content(choice)[0]),"selected"), not c.has(map(content(checkbox)[0]),"checked"),
    request.action == "format.size", request.params.size == 18, request.params.source == "ribbon",
    request.value is int, binding.request("find.query","query","hello").params.value == "hello",
    ((controls.input({value:"x",default_value:"y"}) ^ { null }) or null) == null,
    ((controls.checkbox({checked:false,default_checked:true}) ^ { null }) or null) == null,
    ((controls.input({on_change:42}) ^ { null }) or null) == null,
    ((controls.input({on_change:""}) ^ { null }) or null) == null,
    ((controls.input({on_change:{action:"find",param:42}}) ^ { null }) or null) == null,
    ((controls.input({on_change:{action:"find",params:[]}}) ^ { null }) or null) == null,
    ((controls.input({on_change:{action:"find",typo:true}}) ^ { null }) or null) == null,
    ((controls.select({options:[{value:1},{value:"1"}]}) ^ { null }) or null) == null,
    c.has(map(content(fractional)[1]),"selected"),
    ((controls.select({options:[{value:inf}]}) ^ { null }) or null) == null,
    ((controls.select({options:[{value:nan}]}) ^ { null }) or null) == null
]
