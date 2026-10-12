// quantitative overlays reuse chart preparation, scales, marks and path warping.
import c: .common
import dates: .date
import time_map: .time_map
import coordinate: .coordinate
import theme: .theme
import parse: lambda.chart.parse
import renderer: lambda.chart.mark_renderer
import spatial: lambda.chart.coordinate
import scales: lambda.chart.scale
import transform: lambda.chart.transform
import cfg: lambda.chart.config
import paint: lambda.chart.paint
import legend: lambda.chart.legend
import svg: lambda.chart.svg
import util: lambda.chart.util
import axis: lambda.chart.axis

fn mark_options(value) {
    let raw=if (value is string or value is symbol) {kind:c.text(value)} else parse.parse_mark(value);
    let kind=if (raw.kind!=null) c.text(raw.kind) else "line";
    {*:raw,kind:if (kind=="step") "line" else if (kind=="column") "bar" else if (kind=="range") "rect" else kind,
        *:(if (kind=="step") {interpolate:"step"} else {})}
}

fn clock(value, row, fallback, scale) => dates.normalize(parse.channel_value(parse.channel_definition(value,row),row,fallback),scale)

pub fn prepare(spec, plan, item, index = 0) {
    let raw=if (item.data is array) item.data else c.items(item.data.values);
    let data=if (item.transform!=null) transform.apply_transforms(raw,item.transform) else raw;
    let encoding=parse.prepare_channels(c.attrs(item.encoding));
    let options=mark_options(item.mark);
    let records=if (data is error or encoding is error) [] else [for (row in data,
        let start=clock(encoding.x,row,row.at,spec.scale),
        let end=if (encoding.x2!=null) clock(encoding.x2,row,row.to,spec.scale) else start)
        if (start is error) start else if (end is error) end else {*:row,_timeline_at:start.value,_timeline_to:end.value,
            _timeline_x:time_map.fraction(plan.time_map,start.value),_timeline_x2:time_map.fraction(plan.time_map,end.value)}];
    let enc={*:encoding,x:{field:"_timeline_x",dtype:"quantitative"},
        x2:if (encoding.x2!=null) {field:"_timeline_x2",dtype:"quantitative"} else null};
    let prepared=renderer.prepare_mark_data({data:records,encoding:enc,mark:options});
    let named=(spec.scales |: ~.id==c.text(item.value_scale))[0];
    let measure_options=if (named!=null) {*:named,type:named.kind} else c.attrs(encoding.y.scale);
    let values=[for (row in prepared.data) for (channel in [encoding.y,encoding.y2] where channel!=null)
        parse.channel_value(parse.channel_definition(channel,row),row)];
    let bounds=if (prepared.stack_mode!=null) [for (row in prepared.data) for (value in [row._y0,row._y1]) value] else values;
    let mapping=scales.configured_scale(bounds,plan.height,0.0,"linear",measure_options,contains(["bar","area"],options.kind));
    let skin=theme.tokens(spec);
    let styling=cfg.resolve_theme({font:skin.font_family,background:skin.background,*:c.attrs(item.config)});
    let paints=paint.plan([],"timeline-series-" ++ util.binding_key([spec.id,c.option(item,"id",string(index))]));
    let context={*:renderer.mark_context(prepared.data,enc,scales.linear_scale(0,1,0,plan.width),mapping,
        {plot_w:plan.width,plot_h:plan.height},prepared.stack_mode),_theme:styling,_paints:paints,
        _behaviors:[],_parameters:[],_parameter_values:{},_view_path:"timeline-series-" ++ string(index)};
    let breadth=c.option(spec.coordinate,"lane_width",min(plan.box.width,plan.box.height)*0.65);
    let projection=spatial.configure({type:"custom",precision:plan.path.precision,
        project:(point)=>coordinate.project(plan.path,point[0],(point[1]-0.5)*breadth).point,
        invert:(point)=>(let hit=coordinate.invert(plan.path,point),let base=coordinate.project(plan.path,hit.fraction),
            [hit.fraction,0.5+((point[0]-base.point[0])*base.normal[0]+(point[1]-base.point[1])*base.normal[1])/breadth])},plan.width,plan.height);
    let legends=if (item.legend==false) [] else legend.plans(enc,context,styling);
    let failure=util.first_error([data,encoding,prepared,mapping,projection,paints._error,*records,for (entry in legends) entry._error,
        for (key in ["color","stroke","shape","size","opacity"]) context[key ++ "_scale"],
        if (not contains(["point","line","area","bar","rect","rule","tick","text","errorbar","errorband"],options.kind)) c.fail("series.mark","unsupported timeline overlay mark"),
        if (encoding.y==null) c.fail("series.encoding.y","quantitative overlay requires a value channel"),
        for (value in values) if (not util.finite_number(value)) c.fail("series.encoding.y","expected finite quantitative values")]);
    if (failure is error) failure else {id:c.option(item,"id","series-" ++ string(index)),spec:item,data:prepared.data,
        context:context,mark:cfg.mark_config(styling,options),projection:projection,mapping:mapping,legends:legends,values:bounds}
}

pub fn prepare_all(spec, plan) {
    let prepared=[for (index,item in spec.series where item.visible!=false) prepare(spec,plan,item,index)];
    let failure=util.first_error(prepared);
    if (failure is error) failure else {
        let shared=[for (mapping in spec.scales where mapping.id!=spec.time_scale,
            let values=[for (model in prepared where c.text(model.spec.value_scale)==mapping.id) for (value in model.values) value])
            if (mapping.domain!=null or len(values)==0) mapping else {*:mapping,domain:[min(values),max(values)]}];
        let combined={*:spec,scales:[spec.scale,*shared]};
        let models=[for (index,model in prepared) if (model.spec.value_scale==null) model else prepare(combined,plan,model.spec,index)];
        let problem=util.first_error(models);
        if (problem is error) problem else models
    }
}

pub fn axes(spec, models, plan) {
    let images=[for (guide in spec.axes where guide.scale!=null and c.text(guide.scale)!=spec.time_scale and guide.position!="none",
        let model=(models |: c.text(~.spec.value_scale)==c.text(guide.scale))[0] where model!=null)
        for (side in (if (guide.position=="both") ["left","right"] else [if (guide.position=="start") "left" else "right"]),
            let prepared=axis.prepare(model.mapping,{*:c.attrs(guide),orient:side},guide.title),
            let image=axis.y_axis(model.mapping,plan.width,plan.height,prepared,guide.title))
            if (image is error) image else <g class:"lambda-timeline-value-axis",'data-timeline-scale':c.text(guide.scale),
                spatial.warp(image,model.projection)>];
    let failure=util.first_error(images);
    if (failure is error) failure else images
}

pub fn render(model) {
    let image=renderer.render(model.mark.kind,model.data,model.context,{*:model.mark,clip:false});
    let projected=if (image is error) image else spatial.warp(image,model.projection);
    let clipped=if (projected is error) projected else spatial.clip(projected,model.projection,"timeline-series-" ++ util.binding_key([model.id]));
    if (clipped is error) clipped else <g class:"lambda-timeline-series",'data-timeline-series':c.text(model.id),clipped>
}

pub fn render_legends(models, plan) => <g class:"lambda-timeline-series-legends",
    *[for (index,entry in [for (model in models) for (entry in model.legends) entry])
        <g transform:svg.translate(plan.box.x+index*160,plan.height-36),legend.render_plan(entry)>]>
