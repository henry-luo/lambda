import ui: lambda.ui.dtna
let models = [
    ui.title({},"Heading")^,ui.text({},"Text")^,ui.paragraph({},"Paragraph")^,ui.link({href:"#"},"Link")^,
    ui.divider()^,ui.flex()^,ui.space()^,ui.space_compact()^,ui.row()^,ui.col()^,
    ui.layout()^,ui.layout_header()^,ui.layout_footer()^,ui.layout_content()^,ui.layout_sider()^,
    ui.avatar({},"A")^,ui.badge({count:0},"Badge")^,ui.card({title:0})^,ui.descriptions({items:[]})^,
    ui.empty()^,ui.statistic({value:0})^,ui.progress({percent:125})^,ui.timeline({items:[]})^,
    ui.steps({items:[]})^,ui.breadcrumb({items:[]})^,ui.skeleton({rows:1})^,ui.spin()^,
    ui.result({status:'success',title:0})^,ui.icon({name:"check"})^,
    ui.alert({message:0})^,ui.tag({},"Tag")^,ui.config_provider()^,
    ui.form()^,ui.form_item()^
]
let empty = ui.render(ui.empty({description:"No records"})^)
let progress = ui.render(ui.progress({percent:125})^);
[[for (model in models) name(ui.render(model))],
    content(content(empty)[1])[0],progress["aria-valuenow"],
    (ui.title({level:6}) or null) == null,
    (ui.col({span:25}) or null) == null,
    (ui.select({options:[{value:1},{value:"1"}]}) or null) == null,
    (ui.page("body",{unknown:true}) or null) == null,
    (ui.text({variant:'primary'},"text") or null) == null]
