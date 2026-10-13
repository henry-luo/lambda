// shared Ant-inspired contracts for source skins; static tag factories stay with each skin (D7.2.4).
import base: lambda.ui.dtna
import c: .component

let entries = {
    ["button"]:{build:base.button,children:true},
    ["button-group"]:{build:base.button_group,children:true},
    ["input"]:{build:base.input,children:false},
    ["text-area"]:{build:base.text_area,children:false},
    ["select"]:{build:base.select,children:false},
    ["checkbox"]:{build:base.checkbox,children:true},
    ["radio"]:{build:base.radio,children:true},
    ["switch"]:{build:base.switch,children:false},
    ["icon"]:{build:base.icon,children:false},
    ["title"]:{build:base.title,children:true},
    ["text"]:{build:base.text,children:true},
    ["paragraph"]:{build:base.paragraph,children:true},
    ["link"]:{build:base.link,children:true},
    ["space"]:{build:base.space,children:true},
    ["flex"]:{build:base.flex,children:true},
    ["space-compact"]:{build:base.space_compact,children:true},
    ["divider"]:{build:base.divider,children:true},
    ["row"]:{build:base.row,children:true},
    ["col"]:{build:base.col,children:true},
    ["layout"]:{build:base.layout,children:true},
    ["layout-header"]:{build:base.layout_header,children:true},
    ["layout-footer"]:{build:base.layout_footer,children:true},
    ["layout-content"]:{build:base.layout_content,children:true},
    ["layout-sider"]:{build:base.layout_sider,children:true},
    ["avatar"]:{build:base.avatar,children:true},
    ["badge"]:{build:base.badge,children:true},
    ["badge-ribbon"]:{build:base.badge_ribbon,children:true},
    ["card"]:{build:base.card,children:true},
    ["card-grid"]:{build:base.card_grid,children:true},
    ["card-meta"]:{build:base.card_meta,children:true},
    ["empty"]:{build:base.empty,children:true},
    ["statistic"]:{build:base.statistic,children:false},
    ["progress"]:{build:base.progress,children:false},
    ["timeline"]:{build:base.timeline,children:false},
    ["steps"]:{build:base.steps,children:false},
    ["breadcrumb"]:{build:base.breadcrumb,children:false},
    ["descriptions"]:{build:base.descriptions,children:false},
    ["skeleton"]:{build:base.skeleton,children:true},
    ["skeleton-avatar"]:{build:base.skeleton_avatar,children:false},
    ["skeleton-button"]:{build:base.skeleton_button,children:false},
    ["skeleton-input"]:{build:base.skeleton_input,children:false},
    ["skeleton-image"]:{build:base.skeleton_image,children:false},
    ["skeleton-node"]:{build:base.skeleton_node,children:true},
    ["spin"]:{build:base.spin,children:true},
    ["result"]:{build:base.result,children:true},
    ["alert"]:{build:base.alert,children:true},
    ["tag"]:{build:base.tag,children:true},
    ["checkable-tag"]:{build:base.checkable_tag,children:true},
    ["rate"]:{build:base.rate,children:false},
    ["tabs"]:{build:base.tabs,children:false},
    ["segmented"]:{build:base.segmented,children:false},
    ["menu"]:{build:base.menu,children:false},
    ["pagination"]:{build:base.pagination,children:false},
    ["form"]:{build:base.form,children:true},
    ["form-item"]:{build:base.form_item,children:true},
    ["collapse"]:{build:base.collapse,children:false},
    ["tree"]:{build:base.tree,children:false},
    ["table"]:{build:base.table,children:false}
}
pub fn kinds() array => [for (key,value in entries) c.text(key)]
pub fn native(kind, props, children = [], family = "dtna") element^ {
    let entry = entries[c.text(kind)];
    if (entry == null) raise c.fail(kind,"unknown component",family)
    else if (not (props is map)) raise c.fail(kind,"props must be a map",family)
    else if (not entry.children and len(children) > 0) raise c.fail(kind,"component does not accept children",family)
    else (if (entry.children) entry.build(props,children) else entry.build(props)) ^ {
        raise error(replace(^.message,"dtna:",family ++ ":"))
    }
}
pub fn descriptor(tags, kind, props, child = null, family = "dtna") element^ {
    let source = native(kind,props,c.children(child),family)^;
    tags[c.text(c.kind(source))](c.props(source),content(source))
}
// lower map/array slots as well as children; providers and skin widgets remain view boundaries.
pub fn lower_slots(value, family, boundaries = ["page","config-provider"], custom = null) any^ {
    if (value is array) [for (child in value) lower_slots(child,family,boundaries,custom)^]
    else if (value is element and starts_with(c.text(name(value)),family ++ ".") and
        not contains(boundaries,c.text(c.kind(value)))) (
        let kind = c.text(c.kind(value)),
        let props = lower_slots(c.props(value),family,boundaries,custom)^,
        let children = [for (child in content(value)) lower_slots(child,family,boundaries,custom)^],
        if (custom != null and contains(custom.kinds,kind)) custom.build(kind,props,children)^
        else native(kind,props,children,family)^
    )
    else if (value is map) map([for (key,child in value) (key,lower_slots(child,family,boundaries,custom)^)])
    else value
}
pub fn render(root, family, known) any^ {
    if (root is array) [for (child in root) render(child,family,known)^] else (
        let valid = if (root is element and starts_with(c.text(name(root)),family ++ ".") and
            not contains(known,c.text(c.kind(root)))) raise c.fail(c.kind(root),"unknown component",family) else true,
        c.render(root)^
    )
}
