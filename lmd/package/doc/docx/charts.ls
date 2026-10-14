// DrawingML chart caches are data; the shipped chart package owns SVG layout (D7.2.4, S1.8).
import c: .common
import package: .package
import chart: lambda.chart.chart
import colors: lambda.chart.color
import numbers: lambda.chart.numbers

fn child(node, tag) => c.child(node,tag,c.CH)
fn children(node, tag) => c.children(node,tag,c.CH)
fn val(node, fallback = null) => c.get(node.attrs,"#val",fallback)
fn issue(pkg, message) => {nodes:[],diagnostics:[c.issue("unsupported-chart",pkg.main,message)]}
fn title(node) => join([for (text in c.descendants(child(node,"title"),"t",c.A)) c.text(text)],"")

fn cache(source) {
    let candidates = [child(source,"strLit"),child(source,"numLit"),
        child(child(source,"strRef"),"strCache"),child(child(source,"numRef"),"numCache")]
    let stored = c.first([for (node in candidates where node != null) node]);
    if (stored == null) null else {
        let points = [for (node in children(stored,"pt"))
            {index:c.as_int(c.attr(node,"idx",""),-1),value:c.text(child(node,"v"))}]
        let count = c.as_int(val(child(stored,"ptCount")),-1)
        let invalid = any([for (i,point in points) point.index < 0 or
            (count >= 0 and point.index >= count) or
            any([for (j,prior in points where j < i) prior.index == point.index])]);
        if (invalid) null else {points:sort(points,{by:(point)=>point.index}),
            values:map([for (point in points) (string(point.index),point.value)])}
    }
}
fn series(node, ordinal) {
    let labels = cache(child(node,"tx"))
    let direct_name = c.text(child(child(node,"tx"),"v"))
    let series_name = if (direct_name != "") direct_name else c.get(labels.values,"0","Series " ++ string(ordinal+1))
    let categories = cache(child(node,"cat"))
    let values = cache(child(node,"val"))
    let rgb = c.attr(c.child(c.child(child(node,"spPr"),"solidFill",c.A),"srgbClr",c.A),"val","")
    let color = if (rgb != null and len(rgb) == 6 and
        all([for (ch in split(lower(rgb),"")) contains(split("0123456789abcdef",""),ch)])) "#" ++ rgb
        else colors.category10[ordinal % len(colors.category10)]
    let points = [for (point in values.points)
        {category:categories.values[string(point.index)],series:series_name,
            value:float(point.value) ^ { null }}]
    let category_names = [for (point in categories.points) point.value];
    {name:series_name,color:color,order:c.as_int(val(child(node,"order")),ordinal),points:points,categories:category_names,
        valid:categories != null and values != null and len(points) > 0 and
        len(unique(category_names)) == len(category_names) and
        all([for (point in points) point.category != null and numbers.finite_number(point.value)])}
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
        let plots = [for (node in c.children(plot,null,c.CH) where ends_with(node.tag,"Chart")) node]
        let bars = child(plot,"barChart")
        let data = sort([for (i,node in children(bars,"ser")) series(node,i)],{by:(entry)=>entry.order})
        let group = val(child(bars,"grouping"),"clustered")
        let direction = val(child(bars,"barDir"),"col");
        if (root.tag != "chartSpace" or root.uri != c.CH) issue(pkg,"Invalid DrawingML chart root")
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
