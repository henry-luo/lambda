import dom
import c: lambda.ui.core.component
import catalog: .catalog
import theme: .tokens
import shared: lambda.ui.core.theme

pub fn direction(props, owner) bool^ => shared.direction(props,owner,"clay")^
pub fn provider(props, child) element^ {
    let valid = shared.provider(props,"clay",theme.resolve)^;
    <clay.config_provider *:props,*c.children(child)>
}

view clay_provider: <clay.config_provider> state lowered:(catalog.lower_slots(content(~))^) {
    let valid = provider(c.props(~),content(~))^
    let p = c.props(~);
    <fieldset *:c.styled(~,"dtna-config-provider","clay"),dir:p.direction,
        *:c.boolean_attr("disabled",p.disabled),style:theme.variables(c.option(p,"tokens",{}),true)^ ++ c.text(p.style),
        *c.children(c.render(lowered)^)>
}

// keep the nested behavior view alive: its own state and handlers are retained by apply (S12.1.3).
view clay_components: <clay.button> | <clay.button_group> | <clay.input> | <clay.text_area> | <clay.select> | <clay.checkbox> | <clay.radio> | <clay.switch> | <clay.icon> | <clay.title> | <clay.text> | <clay.paragraph> | <clay.link> | <clay.space> | <clay.flex> | <clay.space_compact> | <clay.divider> | <clay.row> | <clay.col> | <clay.layout> | <clay.layout_header> | <clay.layout_footer> | <clay.layout_content> | <clay.layout_sider> | <clay.avatar> | <clay.badge> | <clay.badge_ribbon> | <clay.card> | <clay.card_grid> | <clay.card_meta> | <clay.empty> | <clay.statistic> | <clay.progress> | <clay.timeline> | <clay.steps> | <clay.breadcrumb> | <clay.descriptions> | <clay.skeleton> | <clay.skeleton_avatar> | <clay.skeleton_button> | <clay.skeleton_input> | <clay.skeleton_image> | <clay.skeleton_node> | <clay.spin> | <clay.result> | <clay.alert> | <clay.tag> | <clay.checkable_tag> | <clay.rate> | <clay.tabs> | <clay.segmented> | <clay.menu> | <clay.pagination> | <clay.form> | <clay.form_item> | <clay.collapse> | <clay.tree> | <clay.table> state lowered:(catalog.lower_slots(~)^) {
    c.render(lowered)^
}
