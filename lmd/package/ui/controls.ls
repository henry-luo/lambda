// style-neutral controls with explicit change actions. Native control state is
// synchronized by the same package policy as the styled families (D7.2.5).
import c: .core.component
import shared: .core.contract
import control: .core.control
import elements: .core.element

let tags = {
    input:(attributes,children) => <control.input *:attributes,*children>,
    'text-area':(attributes,children) => <control.text_area *:attributes,*children>,
    select:(attributes,children) => <control.select *:attributes,*children>,
    checkbox:(attributes,children) => <control.checkbox *:attributes,*children>,
    radio:(attributes,children) => <control.radio *:attributes,*children>,
    switch:(attributes,children) => <control.switch *:attributes,*children>
}

fn component(kind, props, child = null) element^ {
    let valid = shared.validate(kind,props,shared.fields,"control",[],[])^;
    elements.create(tags,kind,props,c.children(child))
}

pub fn input(props = {}) element^ => component('input',props)^
pub fn text_area(props = {}) element^ => component('text-area',props)^
pub fn select(props) element^ => component('select',props)^
pub fn checkbox(props = {}, child = null) element^ => component('checkbox',props,child)^
pub fn radio(props = {}, child = null) element^ => component('radio',props,child)^
pub fn switch(props = {}) element^ => component('switch',props)^

view native_controls: <control.input> | <control.text_area> | <control.select> |
    <control.checkbox> | <control.radio> | <control.switch> {
    control.present(component(c.kind(~),c.props(~),content(~))^,"control")
}
on input(evt) { control.edited(~,evt,"control") }
on change(evt) { control.changed(~,evt,"control") }
on render(evt) { control.rendered(~,evt) }
on control_control_sync(evt) { control.sync(~,evt.target) }
