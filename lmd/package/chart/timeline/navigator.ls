// overview geometry shares exact chronology with the main viewport.
import c: .common
import dates: .date
import layout: .layout
import coordinate: .coordinate
import time_map: .time_map
import theme: .theme
import util: lambda.chart.util
import svg: lambda.chart.svg
import story: .story

pub fn plan(spec, viewport = null, snapshot = null) {
    let vp=layout.dimensions(spec,viewport);
    let width=vp.width;
    let height=c.option(spec.navigator,"height",88);
    let domain=layout.extent(spec);
    let visible=layout.window(spec,snapshot);
    let box={x:16.0,y:16.0,width:max(1,width-32),height:max(1,height-32)};
    let path=coordinate.configure({kind:"straight",reverse:spec.direction=="rtl"},box,"horizontal");
    let temporal=time_map.configure(spec.scale,domain);
    let shown=layout.visible(spec,snapshot);
    let failure=util.first_error([vp,visible,path,temporal,shown]);
    if (failure is error) failure else {
        let events=[for (index,item in shown.events,
            let fraction=if (spec.spacing=="sequence") (if (len(shown.events)<2) 0.5 else float(index)/(len(shown.events)-1))
                else time_map.fraction(temporal,if (item.start!=null) item.start.value else domain.from))
            {id:item.id,event:item,fraction:fraction,*:coordinate.project(path,fraction)}];
        let from=util.clamp_val(time_map.fraction(temporal,visible.from),0.0,1.0);
        let to=util.clamp_val(time_map.fraction(temporal,visible.to),0.0,1.0);
        let a=coordinate.project(path,from).point;
        let b=coordinate.project(path,to).point;
        {width:width,height:height,box:box,path:path,time_map:temporal,domain:domain,window:visible,
            events:events,from:from,to:to,selection:{x:min(a[0],b[0]),width:abs(b[0]-a[0]),y:8.0,height:height-16}}
    }
}

pub fn window_action(spec, snapshot, field, fraction) {
    let overview=plan(spec,null,snapshot);
    if (overview is error) overview else if (not util.finite_number(fraction) or not contains(["from","to","center"],field))
        c.fail("navigator","expected a finite position and from, to, or center handle")
    else {
        let at=time_map.invert(overview.time_map,util.clamp_val(fraction,0.0,1.0));
        let current=overview.window;
        let minimum=c.option(spec.window,"min_span",if (spec.scale.kind=="calendar") 1 else 0.000001);
        let span=current.to-current.from;
        {type:"set_window",window:if (field=="from") {from:min(at,dates.offset(current.to,-minimum)),to:current.to}
            else if (field=="to") {from:current.from,to:max(at,dates.offset(current.from,minimum))}
            else {from:dates.offset(at,-decimal(span)/2m),to:dates.offset(at,decimal(span)/2m)}}
    }
}

fn handle(spec, model, key) => <label class:"lambda-timeline-navigator-handle",
    c.message(spec,"window_" ++ key,if (key=="from") "Window start" else "Window end");
    <input type:"range",min:"0",max:"1000",step:"1",value:c.text(model[key]*1000),
        'data-timeline-window-handle':key,'aria-label':c.message(spec,"window_" ++ key,if (key=="from") "Window start" else "Window end"),
        'aria-valuetext':c.text(dates.from_coordinate(model.window[key],spec.scale).label)>>

pub fn render(spec, viewport = null, snapshot = null) {
    let model=plan(spec,viewport,snapshot);
    let skin=theme.tokens(spec,snapshot);
    if (model is error) model else <nav *:theme.part(spec,"navigator",{class:"lambda-timeline-navigator",
        'aria-label':c.message(spec,"overview",if (spec.spacing=="sequence") "Event overview" else "Timeline overview")}),
        *[for (item in story.items(spec,snapshot) where spec.intro!=null and item.id==spec.intro.id)
            <button type:"button",'data-timeline-id':item.id,'aria-current':if (snapshot.active_id==item.id) "step" else null,
                c.plain(item.title)>];
        <div 'data-timeline-navigator':"true",style:"position:relative;width:" ++ c.text(model.width) ++ "px;height:" ++ c.text(model.height) ++ "px;",
            svg.svg_root(model.width,model.height,[
                <rect x:0,y:0,width:model.width,height:model.height,fill:skin.background>,
                if (spec.spacing=="time") <rect class:"lambda-timeline-navigator-window",*:model.selection,fill:skin.selected,stroke:skin.primary,opacity:0.65>,
                <path d:model.path.path,transform:"translate(" ++ c.text(model.path.origin[0]) ++ " " ++ c.text(model.path.origin[1]) ++ ")",fill:"none",stroke:skin.border>,
                *[for (item in model.events) <g 'data-timeline-id':item.id,role:"button",tabindex:"-1",
                    'aria-label':c.plain(item.event.title),
                    <title c.plain(item.event.title) ++ " " ++ c.text(item.event.display_date)>
                    <circle cx:item.point[0],cy:item.point[1],r:if (snapshot.active_id==item.id) 5 else 3,
                        fill:c.option(item.event,"color",skin.primary)>
                    if (spec.navigator.labels==true) <text x:item.point[0],y:item.point[1]+18,'text-anchor':"middle",fill:skin.muted,
                        c.text(item.event.display_date)>>]
            ]);
            *[for (item in model.events where spec.navigator.thumbnails==true and item.event.media[0].thumbnail!=null)
                <img src:item.event.media[0].thumbnail,alt:c.plain(item.event.title),'data-timeline-id':item.id,
                    style:"position:absolute;width:24px;height:24px;object-fit:cover;left:" ++ c.text(item.point[0]-12) ++ "px;top:0;">]>
        if (spec.spacing=="time" and spec.navigator.handles!=false) <div class:"lambda-timeline-navigator-handles",
            handle(spec,model,"from")
            handle(spec,model,"to")>
    >
}
