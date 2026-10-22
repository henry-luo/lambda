import c: lambda.ui.core.component
import icons: .icons
import timeline: lambda.chart.timeline
import live: lambda.chart.timeline.events
import core: lambda.chart.timeline.common
import styles: .timeline_style

fn placement(value) => if (c.text(value) == "right") "end" else if (c.text(value) == "left") "start" else c.text(value)
pub fn descriptor(props) element^ {
    let flags = c.boolean_props(props,["reverse"],"timeline")^
    let items = c.option(props,"items",[]);
    if (not (items is array)) raise c.fail("timeline","items must be an array")
    else if (not c.enum_valid(props.mode,["start","end","left","right","alternate"])^) raise c.fail("timeline","invalid mode")
    else if (not c.enum_valid(props.orientation,["horizontal","vertical"])^) raise c.fail("timeline","invalid orientation")
    else (
        let valid = [for (item in items) validate_item(item)^],
        let core=timeline.validate(specification(props)),
        if (core is error) raise core else
        c.node('timeline',props,null,["items","mode","orientation","reverse","pending","pending_icon","timeline_options"])^
    )
}
fn validate_item(item) bool^ {
    let names = c.properties(item,["key","title","content","label","children","color","icon","dot","placement","position","loading","class","style"],"timeline.item")^
    let flags = c.boolean_props(item,["loading"],"timeline.item")^
    let color = c.validate_colors(item,["color"],"timeline.item")^;
    if (not c.enum_valid(item.placement,["start","end"])^ or not c.enum_valid(item.position,["start","end","left","right"])^)
        raise c.fail("timeline","invalid item placement") else true
}
fn item_color(item) {
    let color = c.option(item,"color","blue");
    if (color == "blue") "var(--dtna-primary)" else if (color == "red") "var(--dtna-error)"
    else if (color == "green") "var(--dtna-success)" else if (color == "gray") "#bfbfbf" else color
}
// dtna translates props and supplies its skin; the chart package owns structure.
fn marker_content(item) {
    let custom = c.option(item,"icon",item.dot);
    if (custom != null) c.render(custom) else if (item.loading) icons.render({name:"loading"})^ else null
}
pub fn specification(props) {
    let mode = placement(c.option(props,"mode","start"));
    let options=core.attrs(props.timeline_options);
    let defaults={id:props.id,view:"cards",spacing:"sequence",order:"source",placement:mode,
        orientation:c.option(props,"orientation","vertical"),
        cards:{reverse:props.reverse == true},
        theme:{class_prefix:"dtna-timeline",background:"var(--dtna-background)",text:"var(--dtna-text)",
            border:"var(--dtna-border)",primary:"var(--dtna-primary)"},
        parts:{root:{class:props.class,style:"font:inherit;" ++ c.text(props.style),attributes:c.attrs(props)}},
        events:[for (index,item in c.option(props,"items",[])) {
            id:if (item.key != null) c.text(item.key) else "event-" ++ string(index),
            display_date:c.option(item,"title",item.label),content:c.option(item,"content",item.children),
            color:item_color(item),placement:placement(c.option(item,"placement",c.option(item,"position",mode))),
            marker:marker_content(item),
            loading:item.loading == true,class:item.class,style:item.style}],
        pending:if (props.pending == null or props.pending == false) null else
            {content:if (props.pending == true) null else props.pending,
             marker:if (props.pending_icon != null) c.render(props.pending_icon) else icons.render({name:"loading"})^}};
    if (props.timeline_options!=null and not (props.timeline_options is map)) core.fail("timeline_options","expected a timeline specification map") else
    {*:defaults,*:options,theme:{*:defaults.theme,*:core.attrs(options.theme),class_prefix:"dtna-timeline",
        preset:if (options.theme is string or options.theme is symbol) c.text(options.theme) else options.theme.preset},
        cards:{*:defaults.cards,*:core.attrs(options.cards)},parts:{*:defaults.parts,*:core.attrs(options.parts)},
        interaction:if (props.timeline_options==null) {enabled:false} else core.attrs(options.interaction)}
}
fn source(node) => {spec:specification(c.props(node)),legacy_list:c.props(node).timeline_options==null}
// the dtna source owns its instance; all state, effects and presentation delegate to core.
view dtna_timeline: <dtna.timeline> state live_state:{interaction:null,token:0} { live.presentation(source(~),live_state) }
on render(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on timeline_frame(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on click(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on dblclick(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on keydown(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on input(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on change(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on wheel(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on pointerdown(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on pointermove(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on pointerup(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on pointercancel(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on lostpointercapture(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on timeline_action(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on hashchange(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }
on popstate(evt) { live_state=live.handle(source(~),live_state,evt); live.deliver(live_state,evt) }

// share core geometry; dtna supplies CSS variables and its existing class namespace.
pub let css = styles.css
