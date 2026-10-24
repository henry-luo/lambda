// One object catalog drives commands, contextual editing, clipboard attributes, and transforms.
import shapes: .mod_shapes
import equation: .mod_equation
import art: .mod_wordart
import icons: .mod_icon
import smartart: .mod_smartart
import chart: .mod_chart
import group: .mod_group_data

pub let catalog = [
    {kind:"shape",label:"Shape",key:'data-word-shape',insert:"insert.shape",update:"shape.set",open:"ui.shape",edit:"ui.shapeSettings",icon:"shapes",defaults:shapes.defaults,settings:shapes.settings,transaction:shapes.transaction,graphic:shapes.graphic},
    {kind:"equation",label:"Equation",key:'data-word-equation',insert:"insert.equation",update:"equation.set",open:"ui.equation",edit:"ui.equationSettings",icon:"equation",defaults:equation.defaults,settings:equation.settings,transaction:equation.transaction},
    {kind:"wordart",label:"Word Art",key:'data-word-wordart',insert:"insert.wordArt",update:"wordArt.set",open:"ui.wordArt",edit:"ui.wordArtSettings",icon:"wordart",defaults:art.defaults,settings:art.settings,transaction:art.transaction,graphic:art.graphic},
    {kind:"icon",label:"Icon",key:'data-word-icon',insert:"insert.icon",update:"icon.set",open:"ui.icon",edit:"ui.iconSettings",icon:"star",defaults:icons.defaults,settings:icons.settings,transaction:icons.transaction,graphic:icons.graphic},
    {kind:"smartart",label:"Diagram",key:'data-word-smartart',insert:"insert.smartArt",update:"smartArt.set",open:"ui.smartArt",edit:"ui.smartArtSettings",icon:"smartart",defaults:smartart.defaults,settings:smartart.settings,transaction:smartart.transaction,graphic:smartart.graphic},
    {kind:"chart",label:"Chart",key:'data-word-chart',insert:"insert.chart",update:"chart.set",open:"ui.chart",edit:"ui.chartSettings",icon:"chart",defaults:chart.defaults,settings:chart.settings,transaction:chart.transaction,graphic:chart.graphic}]
pub fn descriptor(n) => if (group.settings(n)!=null)
    {kind:"group",label:"Group",key:group.key,edit:"ui.objectLayout",icon:"object_group",settings:group.settings}
    else [for (entry in catalog where entry.settings(n)!=null) entry][0]
pub fn command(id) => [for (entry in catalog where contains([entry.insert,entry.update,entry.open,entry.edit],id)) entry][0]
pub fn kind(id) => [for (entry in catalog where entry.kind==id) entry][0]
pub let illustration_kinds = ["icon","smartart","chart"]
pub fn choice_field(id) => {icon:"name",smartart:"layout",chart:"type"}[id]
pub fn parameters(id,values) => map([for (key,value in values) for (part in [string(key),
    if (kind(id).defaults[key] is number and value is string) float(value) else value]) part])
