import ui: lambda.ui.dtna
import statistic: lambda.ui.dtna.statistic
let timeline = ui.render(ui.timeline({mode:'alternate',reverse:true,pending:"Waiting",items:[
    {title:"One",content:"First",color:"green"},{label:"Two",children:"Second",placement:'end',icon:"!"}]})^)
let horizontal = ui.render(ui.timeline({orientation:'horizontal',items:[{content:"Only"}]})^)
let value = ui.render(ui.statistic({title:0,value:"-12345678901234567890.98765",precision:2,prefix:"$",suffix:"USD"})^)
let loading = ui.render(ui.statistic({loading:true,value:123})^)
let custom = ui.render(ui.statistic({value:42,formatter:(value)=>"#" ++ string(value)})^)
let rows = content(timeline);
[
    ["reverse and pending",len(rows) == 3 and contains(rows[0].class,"dtna-timeline-pending") and content(content(rows[2])[2])[0] == "First"],
    ["alternate placement",contains(rows[1].class,"dtna-timeline-item-end") and contains(rows[2].class,"dtna-timeline-item-start")],
    ["custom marker",content(content(content(rows[1])[0])[0])[0] == "!"],
    ["horizontal layout",contains(horizontal.class,"dtna-timeline-horizontal")],
    ["empty timeline",len(content(ui.render(ui.timeline()^))) == 0],
    ["invalid timeline",(ui.timeline({items:1}) or null) == null and (ui.timeline({items:[{loading:1}]}) or null) == null and (ui.timeline({items:[{placement:'middle'}]}) or null) == null and (ui.timeline({items:[{bad:1}]}) or null) == null],
    ["exact large decimal",statistic.number_parts("-12345678901234567890.98765",{precision:2}) == {whole:"-12,345,678,901,234,567,890",fraction:".98"}],
    ["pad precision",statistic.number_parts(42,{precision:3}) == {whole:"42",fraction:".000"}],
    ["custom separators",statistic.number_parts("123456.789",{precision:2,group_separator:" ",decimal_separator:","}) == {whole:"123 456",fraction:",78"}],
    ["truncate precision",statistic.number_parts("9.999",{precision:0}) == {whole:"9",fraction:""}],
    ["statistic title preserves zero",content(content(value)[0])[0] == "0"],
    ["statistic loading",loading["aria-busy"] == "true" and content(loading)[0].class == "dtna-statistic-placeholder"],
    ["statistic callback",content(content(content(custom)[0])[0])[0] == "#42"],
    ["invalid statistic",(ui.statistic({precision:-1}) or null) == null and (ui.statistic({precision:101}) or null) == null and (ui.statistic({formatter:1}) or null) == null and (ui.statistic({loading:1}) or null) == null and (ui.statistic({group_separator:1}) or null) == null]
]
