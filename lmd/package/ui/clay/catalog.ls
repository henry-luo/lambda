// clay keeps authored identity while sharing the Ant-inspired behavior layer (S2.4.3v3; S12.1.3).
import base: lambda.ui.dtna
import c: lambda.ui.core.component

let entries = {
    ["button"]:{build:base.button,children:true,tag:(p,k) => <clay.button *:p,*k>},
    ["button-group"]:{build:base.button_group,children:true,tag:(p,k) => <clay.button_group *:p,*k>},
    ["input"]:{build:base.input,children:false,tag:(p,k) => <clay.input *:p,*k>},
    ["text-area"]:{build:base.text_area,children:false,tag:(p,k) => <clay.text_area *:p,*k>},
    ["select"]:{build:base.select,children:false,tag:(p,k) => <clay.select *:p,*k>},
    ["checkbox"]:{build:base.checkbox,children:true,tag:(p,k) => <clay.checkbox *:p,*k>},
    ["radio"]:{build:base.radio,children:true,tag:(p,k) => <clay.radio *:p,*k>},
    ["switch"]:{build:base.switch,children:false,tag:(p,k) => <clay.switch *:p,*k>},
    ["icon"]:{build:base.icon,children:false,tag:(p,k) => <clay.icon *:p,*k>},
    ["title"]:{build:base.title,children:true,tag:(p,k) => <clay.title *:p,*k>},
    ["text"]:{build:base.text,children:true,tag:(p,k) => <clay.text *:p,*k>},
    ["paragraph"]:{build:base.paragraph,children:true,tag:(p,k) => <clay.paragraph *:p,*k>},
    ["link"]:{build:base.link,children:true,tag:(p,k) => <clay.link *:p,*k>},
    ["space"]:{build:base.space,children:true,tag:(p,k) => <clay.space *:p,*k>},
    ["flex"]:{build:base.flex,children:true,tag:(p,k) => <clay.flex *:p,*k>},
    ["space-compact"]:{build:base.space_compact,children:true,tag:(p,k) => <clay.space_compact *:p,*k>},
    ["divider"]:{build:base.divider,children:true,tag:(p,k) => <clay.divider *:p,*k>},
    ["row"]:{build:base.row,children:true,tag:(p,k) => <clay.row *:p,*k>},
    ["col"]:{build:base.col,children:true,tag:(p,k) => <clay.col *:p,*k>},
    ["layout"]:{build:base.layout,children:true,tag:(p,k) => <clay.layout *:p,*k>},
    ["layout-header"]:{build:base.layout_header,children:true,tag:(p,k) => <clay.layout_header *:p,*k>},
    ["layout-footer"]:{build:base.layout_footer,children:true,tag:(p,k) => <clay.layout_footer *:p,*k>},
    ["layout-content"]:{build:base.layout_content,children:true,tag:(p,k) => <clay.layout_content *:p,*k>},
    ["layout-sider"]:{build:base.layout_sider,children:true,tag:(p,k) => <clay.layout_sider *:p,*k>},
    ["avatar"]:{build:base.avatar,children:true,tag:(p,k) => <clay.avatar *:p,*k>},
    ["badge"]:{build:base.badge,children:true,tag:(p,k) => <clay.badge *:p,*k>},
    ["badge-ribbon"]:{build:base.badge_ribbon,children:true,tag:(p,k) => <clay.badge_ribbon *:p,*k>},
    ["card"]:{build:base.card,children:true,tag:(p,k) => <clay.card *:p,*k>},
    ["card-grid"]:{build:base.card_grid,children:true,tag:(p,k) => <clay.card_grid *:p,*k>},
    ["card-meta"]:{build:base.card_meta,children:true,tag:(p,k) => <clay.card_meta *:p,*k>},
    ["empty"]:{build:base.empty,children:true,tag:(p,k) => <clay.empty *:p,*k>},
    ["statistic"]:{build:base.statistic,children:false,tag:(p,k) => <clay.statistic *:p,*k>},
    ["progress"]:{build:base.progress,children:false,tag:(p,k) => <clay.progress *:p,*k>},
    ["timeline"]:{build:base.timeline,children:false,tag:(p,k) => <clay.timeline *:p,*k>},
    ["steps"]:{build:base.steps,children:false,tag:(p,k) => <clay.steps *:p,*k>},
    ["breadcrumb"]:{build:base.breadcrumb,children:false,tag:(p,k) => <clay.breadcrumb *:p,*k>},
    ["descriptions"]:{build:base.descriptions,children:false,tag:(p,k) => <clay.descriptions *:p,*k>},
    ["skeleton"]:{build:base.skeleton,children:true,tag:(p,k) => <clay.skeleton *:p,*k>},
    ["skeleton-avatar"]:{build:base.skeleton_avatar,children:false,tag:(p,k) => <clay.skeleton_avatar *:p,*k>},
    ["skeleton-button"]:{build:base.skeleton_button,children:false,tag:(p,k) => <clay.skeleton_button *:p,*k>},
    ["skeleton-input"]:{build:base.skeleton_input,children:false,tag:(p,k) => <clay.skeleton_input *:p,*k>},
    ["skeleton-image"]:{build:base.skeleton_image,children:false,tag:(p,k) => <clay.skeleton_image *:p,*k>},
    ["skeleton-node"]:{build:base.skeleton_node,children:true,tag:(p,k) => <clay.skeleton_node *:p,*k>},
    ["spin"]:{build:base.spin,children:true,tag:(p,k) => <clay.spin *:p,*k>},
    ["result"]:{build:base.result,children:true,tag:(p,k) => <clay.result *:p,*k>},
    ["alert"]:{build:base.alert,children:true,tag:(p,k) => <clay.alert *:p,*k>},
    ["tag"]:{build:base.tag,children:true,tag:(p,k) => <clay.tag *:p,*k>},
    ["checkable-tag"]:{build:base.checkable_tag,children:true,tag:(p,k) => <clay.checkable_tag *:p,*k>},
    ["rate"]:{build:base.rate,children:false,tag:(p,k) => <clay.rate *:p,*k>},
    ["tabs"]:{build:base.tabs,children:false,tag:(p,k) => <clay.tabs *:p,*k>},
    ["segmented"]:{build:base.segmented,children:false,tag:(p,k) => <clay.segmented *:p,*k>},
    ["menu"]:{build:base.menu,children:false,tag:(p,k) => <clay.menu *:p,*k>},
    ["pagination"]:{build:base.pagination,children:false,tag:(p,k) => <clay.pagination *:p,*k>},
    ["form"]:{build:base.form,children:true,tag:(p,k) => <clay.form *:p,*k>},
    ["form-item"]:{build:base.form_item,children:true,tag:(p,k) => <clay.form_item *:p,*k>},
    ["collapse"]:{build:base.collapse,children:false,tag:(p,k) => <clay.collapse *:p,*k>},
    ["tree"]:{build:base.tree,children:false,tag:(p,k) => <clay.tree *:p,*k>},
    ["table"]:{build:base.table,children:false,tag:(p,k) => <clay.table *:p,*k>}
}

pub fn kinds() array => [for (key,value in entries) c.text(key)]
pub fn native(kind, props, children = []) element^ {
    let entry = entries[c.text(kind)];
    if (entry == null) raise c.fail(kind,"unknown component","clay")
    else if (not (props is map)) raise c.fail(kind,"props must be a map","clay")
    else if (not entry.children and len(children) > 0) raise c.fail(kind,"component does not accept children","clay")
    else (if (entry.children) entry.build(props,children) else entry.build(props)) ^ {
        raise error(replace(^.message,"dtna:","clay:"))
    }
}

pub fn descriptor(kind, props, child = null) element^ {
    let source = native(kind,props,c.children(child))^;
    // aliases such as password retain the native constructor's normalized kind.
    entries[c.text(c.kind(source))].tag(c.props(source),content(source))
}

// lower slots as well as children, so card tabs, table cells and layout sider detection share the same engine.
pub fn lower_slots(value) any^ {
    if (value is array) [for (child in value) lower_slots(child)^]
    else if (value is element and starts_with(c.text(name(value)),"clay.") and
        not contains(['page','config-provider'],c.kind(value)))
        native(c.kind(value),lower_slots(c.props(value))^,[for (child in content(value)) lower_slots(child)^])^
    else if (value is map) map([for (key,child in value) (key,lower_slots(child)^)])
    else value
}
