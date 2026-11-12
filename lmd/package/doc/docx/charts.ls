// DrawingML chart caches are data; the shipped chart package owns SVG layout (D9.3.4, S1.8).
import c: .common
import package: .package
import chart: lambda.chart.chart
import colors: lambda.chart.color
import numbers: lambda.chart.numbers

fn child(node, tag) any^ => c.child(node,tag,c.CH)
fn children(node, tag) any^ => c.children(node,tag,c.CH)
fn val(node, fallback = null) any^ => c.get(node.attrs,"#val",fallback)
fn issue(pkg, message) => {nodes:[],diagnostics:[c.issue("unsupported-chart",pkg.main,message)]}
fn title(node) => join([for (text in ((c.descendants(child(node,"title"),"t",c.A)) ^ { [] })) c.text(text)],"")

fn cache(source) {
    let candidates = [child(source,"strLit"),child(source,"numLit"),
        child(child(source,"strRef"),"strCache"),child(child(source,"numRef"),"numCache")]
    let stored = c.first([for (node in candidates where node != null) node]);
    if (stored == null) null else {
        let points = [for (node in ((children(stored,"pt")) ^ { [] }))
            {index:c.as_int(c.attr(node,"idx",""),-1),value:c.text(child(node,"v"))}]
        let count = c.as_int(val(child(stored,"ptCount")),-1)
        let invalid = any([for (i,point in points) point.index < 0 or
            (count >= 0 and point.index >= count) or
            any([for (j,prior in points where j < i) prior.index == point.index])]);
        if (invalid) null else {points:sort(points,{by:(point)=>point.index}),
            values:map([for (point in points) (string(point.index),point.value)])}
    }
}
fn identity(node,ordinal) {
    let labels = cache(child(node,"tx"))
    let direct_name = c.text(child(child(node,"tx"),"v"))
    let series_name = if (direct_name != "") direct_name else c.get(labels.values,"0","Series " ++ string(ordinal+1))
    let rgb = c.attr(c.child(c.child(child(node,"spPr"),"solidFill",c.A),"srgbClr",c.A),"val","")
    let color = if (rgb != null and len(rgb) == 6 and
        all([for (ch in ((split(lower(rgb),"")) ^ { [] })) contains(split("0123456789abcdef",""),ch)])) "#" ++ rgb
        else colors.category10[ordinal % len(colors.category10)];
    {name:series_name,color:color,order:c.as_int(val(child(node,"order")),ordinal)}
}
fn series(node, ordinal) {
    let info = identity(node,ordinal)
    let categories = cache(child(node,"cat"))
    let values = cache(child(node,"val"))
    let points = [for (point in ((values.points) ^ { [] }))
        {category:categories.values[string(point.index)],series:info.name,
            value:float(point.value) ^ { null }}]
    let category_names = [for (point in ((categories.points) ^ { [] })) point.value];
    {*:info,points:points,categories:category_names,
        valid:categories != null and values != null and len(points) > 0 and
        len(unique(category_names)) == len(category_names) and
        all([for (point in points) point.category != null and numbers.finite_number(point.value)])}
}

// X/Y caches use point indices, not array order; a missing xVal uses Excel's 1-based sequence.
fn xy_series(node,ordinal,bubble) {
    let info = identity(node,ordinal)
    let x_source = child(node,"xVal")
    let xs = cache(x_source)
    let ys = cache(child(node,"yVal"))
    let sizes = if (bubble) cache(child(node,"bubbleSize")) else null
    let points = [for (point in ((ys.points) ^ { [] })) {
        let key = string(point.index);
        {x:if (x_source==null) point.index+1 else float(xs.values[key]) ^ { null },
            y:float(point.value) ^ { null },size:if (bubble) float(sizes.values[key]) ^ { null } else 1.0,series:info.name}
    }];
    {*:info,points:points,valid:ys!=null and (x_source==null or xs!=null) and (not bubble or sizes!=null) and len(points)>0 and
        all([for (point in points) numbers.finite_number(point.x) and numbers.finite_number(point.y) and
            numbers.finite_number(point.size) and point.size>=0])}
}
fn rendered(pkg,spec,width,height,description) {
    let image = chart.render_spec(spec);
    if (image is error) issue(pkg,"Chart renderer could not display the cached data") else
        {nodes:[{kind:"chart",svg:image,width:width,height:height,text:description}],
            diagnostics:[c.issue("partial-chart",pkg.main,"Chart uses cached data; some PowerPoint styling and labels are not reproduced")]}
}
fn xy_chart(pkg,source,plot,kind,width,height) map^ {
    let bubble = kind=="bubbleChart"
    let entries = sort([for (i,node in ((children(plot,"ser")) ^ { [] })) xy_series(node,i,bubble)],{by:(entry)=>entry.order})
    let names = [for (entry in entries) entry.name];
    if (len(entries)==0 or not all([for (entry in entries) entry.valid]) or len(unique(names))!=len(names))
        issue(pkg,"Scatter or bubble chart has unsupported or invalid numeric caches")
    else {
        let points = [for (entry in entries) *entry.points]
        let legend = child(source,"legend")
        let spec = {width:width,height:height,padding:8,mark:{kind:"point",size:60},data:points,
            title:if (title(source)=="") null else title(source),encoding:{
                x:{field:"x",dtype:"quantitative",title:null},y:{field:"y",dtype:"quantitative",title:null},
                *:if (bubble) {size:{field:"size",dtype:"quantitative",legend:false}} else {},
                color:{field:"series",dtype:"nominal",title:null,scale:{domain:names,range:[for (entry in entries) entry.color]},
                    legend:if (legend==null) false else {title:null}}}};
        rendered(pkg,spec,width,height,join([for (point in points) point.series ++ "\t" ++ string(point.x) ++ "\t" ++ string(point.y)],"\n"))
    }
}
fn pie_chart(pkg,source,plot,width,height) map^ {
    let entries = [for (i,node in ((children(plot,"ser")) ^ { [] })) series(node,i)]
    let entry = entries[0];
    if (len(entries)!=1 or not entry.valid or any([for (point in entry.points) point.value<0]))
        issue(pkg,"Pie chart has unsupported or invalid category/value caches")
    else {
        let legend = child(source,"legend")
        let spec = {width:width,height:height,padding:8,mark:{kind:"arc"},data:entry.points,
            title:if (title(source)=="") null else title(source),encoding:{theta:{field:"value",dtype:"quantitative"},
                color:{field:"category",dtype:"nominal",title:null,legend:if (legend==null) false else {title:null}}}};
        rendered(pkg,spec,width,height,join([for (point in entry.points) point.category ++ "\t" ++ string(point.value)],"\n"))
    }
}

pub fn read(pkg, reference, width, height) map^ {
    let rel = package.by_id(pkg.relationships,c.attr(reference,"id",c.R));
    if (rel == null or rel.type != c.R ++ "/chart") issue(pkg,"Unresolved chart relationship")
    else if (rel.external) issue(pkg,"External chart was not loaded")
    else if (pkg.parts[rel.target] == null) issue(pkg,"Chart part is missing")
    else {
        let root = package.xml(pkg.parts,rel.target,true)^
        let source = child(root,"chart")
        let plot = child(source,"plotArea")
        let plots = [for (node in ((c.children(plot,null,c.CH)) ^ { [] }) where ((ends_with(node.tag,"Chart")) ^ { false })) node]
        let bars = child(plot,"barChart")
        let data = sort([for (i,node in ((children(bars,"ser")) ^ { [] })) series(node,i)],{by:(entry)=>entry.order})
        let group = val(child(bars,"grouping"),"clustered")
        let direction = val(child(bars,"barDir"),"col");
        if (root.tag != "chartSpace" or root.uri != c.CH) issue(pkg,"Invalid DrawingML chart root")
        else if (len(plots)==1 and plots[0].tag in ["scatterChart","bubbleChart"])
            xy_chart(pkg,source,plots[0],plots[0].tag,width,height)^
        else if (len(plots)==1 and plots[0].tag=="pieChart") pie_chart(pkg,source,plots[0],width,height)^
        else if (len(plots) != 1 or bars == null or group != "clustered" or direction != "col")
            issue(pkg,"Only clustered column charts are supported")
        else if (len(data) == 0 or not all([for (entry in data) entry.valid]))
            issue(pkg,"Chart category/value caches are missing or invalid")
        else if (len(unique([for (entry in data) entry.name])) != len(data))
            issue(pkg,"Chart series names are not distinct")
        else {
            let legend = child(source,"legend")
            let legend_position = {r:"right",l:"left",t:"top",b:"bottom",tr:"right"}[val(child(legend,"legendPos"),"r")]
            let names = [for (entry in data) entry.name]
            let rows = [for (entry in data) *entry.points]
            let categories = unique([for (entry in data) *entry.categories])
            let category = {field:"category",dtype:"nominal",title:null,
                axis:{title:if (title(child(plot,"catAx")) == "") null else title(child(plot,"catAx"))},
                scale:{domain:categories}}
            let value = {field:"value",dtype:"quantitative",title:null,stack:false,
                axis:{title:if (title(child(plot,"valAx")) == "") null else title(child(plot,"valAx"))}}
            let spec = {width:if (width > 0) width else 480.0,height:if (height > 0) height else 300.0,
                padding:8,mark:{kind:"bar"},data:rows,title:if (title(source) == "") null else title(source),
                encoding:{x:category,y:value,
                    x_offset:{field:"series",dtype:"nominal"},
                    color:{field:"series",dtype:"nominal",title:null,
                        scale:{domain:names,range:[for (entry in data) entry.color]},
                        legend:if (legend == null) false else {title:null,orient:if (legend_position == null) "right" else legend_position}}}}
            let image = chart.render_spec(spec);
            if (image is error) issue(pkg,"Chart renderer could not display the cached data") else {
                {nodes:[{kind:"chart",svg:image,width:width,height:height,
                    text:join(["Chart\t" ++ join(names,"\t"),
                        *[for (category_name in categories) category_name ++ "\t" ++ join([for (entry in data)
                            (let value = c.first([for (point in entry.points where point.category == category_name) point.value]),
                             if (value == null) "" else string(value))],"\t")]],"\n")}],
                    diagnostics:[]}
            }
        }
    }
}
