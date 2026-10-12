// timeline presentation owns structural layout; skins only provide semantic parts.
import c: .common
import theme: .theme
import util: lambda.chart.util
import layout: .layout
import vector: .svg
import media: .media
import card: .card
import sizing: lambda.chart.sizing
import groups: .groups
import search: .search
import filter_controls: .filter_controls
import navigator: .navigator
import primitives: lambda.chart.svg
import editing: .edit
import story: .story

fn marker(item, spec, p) {
    let appearance=c.attrs(item.appearance);
    let area=c.option(item,"size",c.option(spec.markers,"size",80));
    let extent=2.0*math.sqrt(area/math.pi)+4;
    let fallback=if (spec.markers.visible==false or spec.cards.markers==false) null
        else if (item.marker != null) callback(item.marker,item,spec)
        else if (spec.markers.renderer is fn) callback(spec.markers.renderer,item,spec)
        else if (item.size!=null or item.appearance!=null or spec.markers.size!=null) <svg width:extent,height:extent,
            viewBox:"0 0 " ++ c.text(extent) ++ " " ++ c.text(extent),'aria-hidden':"true",
            primitives.symbol_mark(c.text(c.option(appearance,"shape",c.option(spec.markers,"shape","circle"))),extent/2.0,extent/2.0,area,
                {fill:c.option(appearance,"fill",c.option(item,"color",c.option(spec.markers,"color","currentColor"))),
                    stroke:c.option(appearance,"stroke","currentColor"),'stroke-width':c.option(appearance,"stroke_width",1),opacity:c.option(item,"opacity",1)})>
        else <span class:p ++ "-dot">;
    theme.render(spec,"marker",item,fallback)
}
fn callback(value, item, spec) => card.callback(value,item,spec)
pub fn body(item, spec, snapshot = null) => card.body(item,spec,snapshot)

pub fn responsive(spec, viewport = null) {
    let vp=sizing.viewport(viewport);
    let width=if (vp.width!=null) vp.width else if (spec.width is number) spec.width else null;
    let matching=[for (rule in c.items(spec.cards.breakpoints) where width!=null and
        (rule.min_width==null or width>=rule.min_width) and (rule.max_width==null or width<=rule.max_width)) rule];
    let override=matching[len(matching)-1];
    {*:spec,orientation:if (override.orientation!=null) c.text(override.orientation) else spec.orientation,
        placement:if (override.placement!=null) c.text(override.placement) else spec.placement,
        cards:{*:spec.cards,*:c.attrs(override.cards)}}
}

pub fn cards(spec, snapshot = null) {
    let p = theme.prefix(spec);
    let reversed = spec.cards.reverse == true;
    let pending = if (spec.pending != null and spec.pending != false) [{id:"pending",content:if (spec.pending is map) spec.pending.content else if (spec.pending == true) null else spec.pending,
        marker:spec.pending.marker,loading:true,placement:"start",color:spec.pending.color}] else [];
    let shown=layout.visible(spec,snapshot);
    let active=if (snapshot.active_id!=null) snapshot.active_id else spec.events[0].id;
    let original = [*[for (item in shown.events where spec.cards.visibility!="active" or item.id==active) item], *pending];
    let ordered = if (reversed) reverse(original) else original;
    let items = ordered |: ~.visible != false;
    let labels = any([for (item in items) item.display_date != null]);
    let split = labels or spec.placement == "alternate";
    let root = theme.part(spec,"root",{*:c.attributes(spec),class:p ++ " " ++ p ++ "-" ++ spec.orientation ++
        (if (split) " " ++ p ++ "-labelled" else "") ++ (if (spec.placement == "end") " " ++ p ++ "-end" else "") ++
        (if (spec.class != null) " " ++ spec.class else ""),style:theme.variables(spec,snapshot) ++ c.text(spec.style),dir:spec.direction});
    <ol *:root,'data-timeline-host':if (spec.live_host==true) c.text(spec.id) else null,
        *[for (index,item in items,
            let side=if (item.placement == "end") "end" else "start",
            let is_pending=item.loading == true,
            let pending_rail=if (reversed) is_pending else items[index+1].loading == true,
            // data IDs are chart-local; HTML IDs must not collide with other components.
            let attrs=theme.part(spec,"event",{*:c.attributes(item),id:if (spec.id != null) "timeline-" ++ util.binding_key([spec.id,item.id]) else null,
                class:p ++ "-item " ++ p ++ "-item-" ++ side ++
                (if (is_pending) " " ++ p ++ "-pending" else "") ++ (if (pending_rail) " " ++ p ++ "-rail-pending" else "") ++
                (if (item.class != null) " " ++ item.class else ""),style:(if (item.color != null) "--" ++ p ++ "-color:" ++ item.color ++ ";" else "") ++ c.text(item.style),
                'data-timeline-id':item.id,'aria-current':if (snapshot.active_id == item.id) "step" else null,
                tabindex:if (item.id==active) "0" else "-1",
                'aria-selected':if (snapshot.selected_ids != null) c.text(contains(snapshot.selected_ids,item.id)) else null}))
            <li *:attrs,
                *[<div *:theme.part(spec,"rail",{class:p ++ "-rail"}),
                    <span *:theme.part(spec,"marker",{class:p ++ "-marker " ++ p ++ "-marker-" ++ c.text(c.option(c.attrs(item.appearance),"shape",c.option(spec.markers,"shape","circle"))), 'aria-hidden':"true"}),marker(item,spec,p)>>,
                if (split) theme.render(spec,"date",item,<div *:theme.part(spec,"date",{class:p ++ "-title",style:theme.card_style(spec,item,"date")}),callback(item.display_date,item,spec)>) else null,
                theme.render(spec,"card",item,<div *:theme.part(spec,"card",{class:p ++ "-content" ++ (if (spec.cards.text_overlay==true) " lambda-timeline-text-overlay" else ""),
                    style:theme.card_style(spec,item)}),
                    *[if (not split and item.display_date != null) <div class:p ++ "-title",style:theme.card_style(spec,item,"date"),callback(item.display_date,item,spec)> else null,
                    *body(item,spec,snapshot)]>)]>]>
}

pub fn html(raw, viewport = null, snapshot = null) {
    let spec=responsive(raw,viewport);
    let plan=if (spec.view!="cards" or spec.spacing=="time") layout.normalized(spec,viewport,snapshot) else null;
    let active=if (snapshot.active_id!=null) snapshot.active_id else if (spec.story.active_id!=null) c.text(spec.story.active_id)
        else story.items(spec,snapshot)[if (spec.story.start_at_end) len(story.items(spec,snapshot))-1 else 0].id;
    let sampled=story.frame(spec,snapshot);
    if (plan is error) plan else if (sampled is error) sampled else
    <section id:if (spec.view!="cards" or spec.spacing!="sequence") spec.id else null,
        class:"lambda-timeline-chart",role:"region",tabindex:"0",'data-timeline-host':c.text(spec.id),dir:spec.direction,
        'data-timeline-active':snapshot.active_id,'data-timeline-selected':format(snapshot.selected_ids,'json'),
        'data-timeline-playback':snapshot.playback.phase,'data-timeline-elapsed':snapshot.playback.elapsed,
        'data-timeline-window-from':c.text(snapshot.window.from),'data-timeline-window-to':c.text(snapshot.window.to),
        'data-timeline-engaged':c.text(snapshot.engaged),
        'aria-label':c.option(spec,"label",spec.title.content),
        style:theme.variables(spec,snapshot) ++ (if (spec.width is number or spec.min_width!=null or spec.max_width!=null)
            "width:" ++ c.text(layout.dimensions(spec,viewport).width) ++ "px;" else ""),
        <style theme.cards_css>
        if (spec.title != null) <h2 class:"lambda-timeline-heading",c.render(if (spec.title is map) spec.title.content else spec.title)>
        if (spec.description != null) <div class:"lambda-timeline-description",c.render(if (spec.description is map) spec.description.content else spec.description)>
        status(spec,snapshot);
        if (spec.navigator.visible==true and spec.navigator.position=="top") navigator.render(spec,viewport,snapshot);
        if (spec.toolbar.visible!=false and spec.toolbar.enabled!=false and spec.toolbar.position!="bottom") controls(spec,snapshot);
        if (len(spec.groups)>0) group_headers(spec,snapshot);
        editing.pending(spec,snapshot);
        *(if (spec.view=="cards" and spec.spacing=="sequence") [cards(spec,snapshot)]
        else [if (spec.view=="story") story_stage(spec,snapshot,active,sampled),
            <div class:"lambda-timeline-canvas",style:"position:relative;width:" ++ c.text(plan.width) ++ "px;height:" ++ c.text(plan.height) ++ "px;",
                vector.render(plan,snapshot,{unsupported:"omit",cards:spec.view=="story"});
                *[for (item in plan.events where spec.view!="story" and (spec.cards.visibility!="active" or item.id==active),
                    let b=item.bounds)
                    <article *:theme.part(spec,"card",{class:"lambda-timeline-overlay-card" ++ (if (spec.cards.text_overlay==true) " lambda-timeline-text-overlay" else ""),
                        *:(if (item.cluster==true) {'data-timeline-cluster':item.id} else {'data-timeline-id':item.id}),
                        tabindex:if (active==item.id) "0" else "-1",'aria-current':if (active==item.id) "step" else null,
                        style:"box-sizing:border-box;position:absolute;left:" ++ c.text(b.left) ++ "px;top:" ++ c.text(b.top) ++
                            "px;width:" ++ c.text(b.right-b.left) ++ "px;height:" ++ c.text(b.bottom-b.top) ++
                            "px;" ++ card.overlay_style(spec,item.event,b.right-b.left,b.bottom-b.top)}),
                        if (item.cluster==true) <button type:"button",'data-timeline-action':format({type:"cluster_expand",id:item.id,ids:item.ids},'json'),
                            if (spec.cluster.renderer is fn) spec.cluster.renderer(item,{chart_id:spec.id}) else c.text(item.event.title)>
                        else theme.render(spec,"card",item.event,card.slots(item.event,spec,snapshot))>]>,
            <ol class:"lambda-timeline-chronology",style:"position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0,0,0,0);",
                *[for (item in plan.visible_events) <li 'data-timeline-id':item.id,
                    <time c.text(item.display_date)>
                    layout.plain(item.title) ++ " " ++ layout.plain(item.content)>]>,
            if (plan.offscreen_count>0) <p class:"lambda-timeline-window-summary",role:"status",
                c.message(spec,"visible_events","{visible} of {total} events in the visible window. Use navigation or fit to explore the remaining events.",
                    {visible:len(plan.visible_events),total:plan.total_events})>])
        if (spec.navigator.visible==true and spec.navigator.position!="top") navigator.render(spec,viewport,snapshot);
        *story.indicators(spec,snapshot);
        if (spec.toolbar.visible!=false and spec.toolbar.enabled!=false and spec.toolbar.position=="bottom") controls(spec,snapshot)
    >
}

fn story_stage(spec, snapshot, active, sampled) {
    let outgoing=story.item(spec,sampled.transition.outgoing_id);
    <div class:"lambda-timeline-story-stage",style:"position:relative;isolation:isolate;",
        if (outgoing!=null) story_card(spec,snapshot,outgoing,true);
        story_card(spec,snapshot,story.item(spec,active),false)>
}
fn story_card(spec, snapshot, item, outgoing) =>
    <article class:if (outgoing) "lambda-timeline-story-outgoing" else "lambda-timeline-story",
        *:(if (outgoing) {'data-timeline-outgoing':item.id,'aria-hidden':"true",inert:""}
            else {'data-timeline-id':item.id,tabindex:"0",'aria-current':"step"}),
        id:if (spec.id!=null and item.id!=null) "timeline-story-" ++ (if (outgoing) "exit-" else "") ++ util.binding_key([spec.id,item.id]) else null,
        'data-timeline-introduction':if (spec.intro!=null and item.id==spec.intro.id) "true" else null,
        style:(if (outgoing) "position:absolute;inset:0;pointer-events:none;z-index:-1;" else "position:relative;") ++ story.style(spec,snapshot,outgoing),
        *body(item,spec,if (outgoing) {*:c.attrs(snapshot),passive:true} else snapshot)>

pub fn status(spec, snapshot = null) {
    let resource=snapshot.resources.source;
    let problem=if (snapshot.presentation_error!=null) snapshot.presentation_error else resource.error;
    let loading=spec.source.url!=null and (resource==null or resource.status=="pending");
    let empty=len(layout.visible(spec,snapshot).events)==0;
    let kind=if (problem!=null) "error" else if (loading) "loading" else if (empty) "empty" else null;
    let fallback=if (kind=="error") c.message(spec,"source_error","The timeline could not be loaded.")
        else if (kind=="loading") c.message(spec,"loading","Loading timeline…") else c.message(spec,"empty","No events to display.");
    let authored=spec[kind];
    let value=if (authored is map) authored.content else authored;
    if (kind==null or authored==false) null else <div *:theme.part(spec,kind,{class:"lambda-timeline-status lambda-timeline-" ++ kind,
        role:if (kind=="error") "alert" else "status",'aria-live':"polite",'aria-busy':if (loading) "true" else null}),
        if (value is fn) value({kind:kind,error:problem},{chart_id:spec.id,locale:spec.locale}) else c.render(if (value!=null) value else fallback)>
}

pub fn group_headers(spec, snapshot = null) {
    let shown=layout.visible(spec,snapshot);
    let ordered=groups.ordered(shown.groups);
    let collapsed=c.items(c.option(c.attrs(snapshot),"collapsed_groups",[for (group in spec.groups where group.collapsed==true) group.id]));
    <nav class:"lambda-timeline-groups",'aria-label':c.message(spec,"groups","Timeline groups"),
        *[for (group in ordered,
            let closed=contains(collapsed,group.id))
            <button *:theme.part(spec,"group_header",{type:"button",'data-timeline-group':group.id,
                'data-timeline-action':format({type:if (closed) "expand" else "collapse",group:group.id},'json'),
                'aria-expanded':c.text(not closed),style:"margin-inline-start:" ++ c.text(group.depth*16) ++ "px;"}),
                theme.render(spec,"group_header",group,callback(group.title,group,spec))>]>
}
let button_labels={first:"First",previous:"Previous",next:"Next",last:"Last",fit:"Fit",play:"Play",pause:"Pause",resume:"Resume",stop:"Stop",
    zoom_in:"Zoom in",zoom_out:"Zoom out",reset:"Reset",quick_jump:"Jump to event",layout:"Layout",theme:"Theme"}
fn button(spec, kind, title) => <button *:theme.part(spec,"toolbar_button",{type:"button",'data-timeline-command':kind,
    'data-timeline-action':format({type:kind},'json'),title:title,'aria-label':title}),
    *[c.render(spec.toolbar.icons[kind]),title]>
pub fn controls(spec, snapshot = null) {
    let buttons=c.option(spec.toolbar,"buttons",["first","previous","next","last","fit"]);
    let shown=layout.visible(spec,snapshot);
    let count=len(shown.matches);
    let query=c.text(snapshot.query);
    let labels=c.attrs(spec.toolbar.labels);
    let fallback=<nav *:theme.part(spec,"toolbar",{class:"lambda-timeline-toolbar",'aria-label':c.message(spec,"navigation","Timeline navigation"),
        style:if (spec.toolbar.sticky==true) "position:sticky;z-index:2;" ++ (if (spec.toolbar.position=="bottom") "bottom:0;" else "top:0;") else ""}),
        c.render(spec.toolbar.slots.before);
        *[for (kind in buttons,let key=c.text(kind),let label=c.option(labels,key,c.message(spec,key,c.option(button_labels,key,key))))
            if (contains(["quick_jump","layout","theme"],key)) <label *[label,
                <select 'data-timeline-setting':key,'aria-label':label,
                    *[for (entry in (if (key=="quick_jump") [for (item in story.items(spec,snapshot)) {value:item.id,label:c.plain(item.title)}]
                        else if (key=="layout") [for (mode in (if (spec.spacing=="time") ["cards","axis","story"] else ["cards","story"])) {value:mode,label:c.message(spec,mode,mode)}]
                        else [for (preset in ["light","dark","high_contrast"]) {value:preset,label:c.message(spec,preset,preset)}]))
                        <option value:entry.value,*:(if (entry.value==(if (key=="quick_jump") snapshot.active_id else if (key=="layout") snapshot.view else snapshot.theme)) {selected:""} else {}),entry.label>]>]>
            else button(spec,key,label)];
        if (spec.search.enabled!=false and (spec.search.fields!=null or spec.search.mode!=null)) <input type:"search",
            'data-timeline-search':"true",value:c.text(snapshot.query),'aria-label':c.message(spec,"search","Search timeline"),
            style:if (spec.toolbar.search_width!=null) "width:" ++ c.text(spec.toolbar.search_width) ++ "px;" else null>
        if (query!="") <span class:"lambda-timeline-search-status",role:"status",'aria-live':"polite",
            if (count==0) c.option(spec.search,"no_results",c.message(spec,"no_results","No matching events"))
            else if (spec.search.count!=false) c.message(spec,"matches","{count} matching events",{count:count})>
        if (query!="" and count>0) button(spec,"previous_match",c.message(spec,"previous_match","Previous match"));
        if (query!="" and count>0) button(spec,"next_match",c.message(spec,"next_match","Next match"));
        filter_controls.render(spec,snapshot);
        c.render(spec.toolbar.slots.after)
    >;
    theme.render(spec,"toolbar",snapshot,fallback)
}
