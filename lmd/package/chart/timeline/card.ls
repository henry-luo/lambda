// one card tree serves live layout, intrinsic measurement, and static snapshots.
import c: .common
import theme: .theme
import media: .media
import search: .search
import html: lambda.ui.core.presentation
import radiant
import util: lambda.chart.util
import editing: .edit
import bookmark: .bookmark

pub fn callback(value, item, spec) => if (value is fn) value(item,{chart_id:spec.id,locale:spec.locale}) else c.render(value)

pub fn body(item, spec, snapshot = null) {
    let passive=snapshot.passive==true;
    let expanded=passive or contains(c.items(snapshot.expanded_events),item.id);
    let collapsible=spec.cards.read_more==true or spec.cards.variant=="compact";
    let query=c.text(c.option(c.attrs(snapshot),"query",c.option(spec.search,"query","")));
    let title=search.highlight(callback(item.title,item,spec),query,spec.search);
    let heading=if (item.link != null) <a href:item.link,target:item.target,title> else title;
    let panels={text:c.items(search.highlight(callback(item.content,item,spec),query,spec.search)),
        media:[for (index,record in item.media) media.render(record,spec,{chart_id:spec.id,event_id:item.id,
            media_id:c.text(c.option(record,"id",string(index))),index:index,locale:spec.locale,passive:passive,snapshot:snapshot})],
        children:[for (child in c.items(item.children)) c.render(child)]};
    let panel_order=if (spec.view=="story") c.option(spec.story,"panel_order",["text","media","children"]) else ["text","media","children"];
    let details=[for (part in panel_order) for (value in panels[c.text(part)]) value] |: ~!=null;
    [if (item.title != null) theme.render(spec,"card_title",item,
        html.heading(theme.part(spec,"card_title",{class:"lambda-timeline-card-title",style:theme.card_style(spec,item,"title")}),c.items(heading),c.option(spec.cards,"heading_level",3))),
     if (item.subtitle != null) theme.render(spec,"card_subtitle",item,
        <div *:theme.part(spec,"card_subtitle",{class:"lambda-timeline-card-subtitle"}),search.highlight(callback(item.subtitle,item,spec),query,spec.search)>),
     // keep dtna's slot structure unless expansion or a recipe needs a wrapper.
     *(if (collapsible or spec.parts.card_body!=null) [theme.render(spec,"card_body",item,
        <div *:theme.part(spec,"card_body",{class:"lambda-timeline-card-body",
        style:if (collapsible and not expanded) "max-height:80px;overflow:hidden;" else ""}),*details>)] else details),
     if (collapsible and not passive) <button type:"button",'data-timeline-command':if (expanded) "collapse" else "expand",
        'data-timeline-action':format({type:if (expanded) "collapse" else "expand",id:item.id},'json'),'aria-expanded':c.text(expanded),
        c.message(spec,if (expanded) "read_less" else "read_more",if (expanded) "Read less" else "Read more")>,
     *(if (passive) [] else [*editing.controls(spec,item),bookmark.render(spec,item)])] |: ~ != null
}

pub fn date_label(item, spec) => theme.render(spec,"date",item,<div *:theme.part(spec,"date",{class:"lambda-timeline-card-date",style:theme.card_style(spec,item,"date")}),callback(item.display_date,item,spec)>)
pub fn slots(item, spec, snapshot = null) => [if (item.display_date!=null) date_label(item,spec),*body(item,spec,snapshot)] |: ~!=null

pub fn overlay_style(spec, item, width, height = null) =>
    "box-sizing:border-box;width:" ++ c.text(width) ++ "px;" ++
    (if (height!=null) "height:" ++ c.text(height) ++ "px;" else "") ++
    "overflow:auto;background:var(--" ++ theme.prefix(spec) ++ "-card);border:1px solid var(--" ++ theme.prefix(spec) ++ "-border);padding:8px;" ++ theme.card_style(spec,item,"body",true)

pub fn measure(items, spec, widths, viewport, snapshot = null) {
    if (len(items)==0) [] else {
        let source=<html <head <meta charset:"utf-8"> <style theme.cards_css>>
            <body style:"margin:0;" ++ theme.variables(spec,snapshot),dir:spec.direction,
                *[for (index,item in items) <article *:theme.part(spec,"card",{
                    class:"lambda-timeline-overlay-card" ++ (if (spec.cards.text_overlay==true) " lambda-timeline-text-overlay" else ""),
                    style:overlay_style(spec,item,widths[index],spec.cards.height)}),
                    theme.render(spec,"card",item,slots(item,spec,snapshot))>]
            >>;
        let metrics=radiant.measure_html(format(source,'html'),int(ceil(viewport.width)),int(ceil(viewport.height)));
        if (metrics==null or len(metrics)!=len(items) or any([for (metric in c.items(metrics)) metric==null or not c.positive(metric.width) or not c.positive(metric.height)]))
            c.fail("cards.measure","content measurement failed") else metrics
    }
}
