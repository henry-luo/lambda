import dom
import c: lambda.ui.core.component
import catalog: .catalog
import theme: .tokens
import shared: lambda.ui.core.theme

pub fn direction(props, owner) bool^ => shared.direction(props,owner,"blade")^
pub fn provider(props, child) element^ {
    let valid = shared.provider(props,"blade",theme.resolve)^;
    <blade.config_provider *:props,*c.children(child)>
}

view blade_provider: <blade.config_provider> state lowered:(catalog.lower_slots(content(~))^) {
    let valid = provider(c.props(~),content(~))^
    let p = c.props(~);
    <fieldset *:c.styled(~,"dtna-config-provider","blade"),dir:p.direction,
        *:c.boolean_attr("disabled",p.disabled),style:theme.variables(c.option(p,"tokens",{}),true)^ ++ c.text(p.style),
        *c.children(c.render(lowered)^)>
}

// keep the nested behavior view alive: its own state and handlers are retained by apply (S12.1.3).
view blade_components: <blade.button> | <blade.button_group> | <blade.input> | <blade.text_area> | <blade.select> | <blade.checkbox> | <blade.radio> | <blade.switch> | <blade.icon> | <blade.title> | <blade.text> | <blade.paragraph> | <blade.link> | <blade.space> | <blade.flex> | <blade.space_compact> | <blade.divider> | <blade.row> | <blade.col> | <blade.layout> | <blade.layout_header> | <blade.layout_footer> | <blade.layout_content> | <blade.layout_sider> | <blade.avatar> | <blade.badge> | <blade.badge_ribbon> | <blade.card> | <blade.card_grid> | <blade.card_meta> | <blade.empty> | <blade.statistic> | <blade.progress> | <blade.timeline> | <blade.steps> | <blade.breadcrumb> | <blade.descriptions> | <blade.skeleton> | <blade.skeleton_avatar> | <blade.skeleton_button> | <blade.skeleton_input> | <blade.skeleton_image> | <blade.skeleton_node> | <blade.spin> | <blade.result> | <blade.alert> | <blade.tag> | <blade.checkable_tag> | <blade.rate> | <blade.tabs> | <blade.segmented> | <blade.menu> | <blade.pagination> | <blade.form> | <blade.form_item> | <blade.collapse> | <blade.tree> | <blade.table> state lowered:(catalog.lower_slots(~)^) {
    c.render(lowered)^
}
