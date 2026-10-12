// adjacent runs cluster along arc distance; canonical events remain in the source.
import c: .common
import util: lambda.chart.util
import coordinate: .coordinate
import collision: lambda.chart.collision

fn key(item, options) {
    let by=options.by;
    let category=if (by is fn) by(item.event) else if (by is array) [for (field in by) item.event[field]]
        else if (by!=null) item.event[by] else null;
    util.binding_key([item.lane,category])
}
fn aggregate(items, path, options, snapshot) {
    let ids=items |> ~.id;
    let id=util.binding_key(["cluster",ids]);
    if (len(items)<c.option(options,"min_count",3) or contains(c.items(snapshot.expanded_clusters),id)) items
    else {
        let first=items[0];
        let fraction=avg(items |> ~.fraction);
        let projected=coordinate.project(path,fraction,first.offset);
        let dx=projected.point[0]-first.point[0];
        let dy=projected.point[1]-first.point[1];
        let bounds=if (first.bounds!=null) collision.translate(first.bounds,dx,dy) else null;
        let title=if (options.title is fn) options.title(items |> ~.event,{count:len(items),ids:ids})
            else if (options.title!=null) c.message({messages:{cluster_events:options.title},locale:options.locale},"cluster_events","",{count:len(items)})
            else c.message(options,"cluster_events","{count} events",{count:len(items)});
        [{*:first,*:projected,id:id,bounds:bounds,card:if (first.card!=null) [first.card[0]+dx,first.card[1]+dy] else null,cluster:true,ids:ids,count:len(items),layout_index:min(items |> ~.layout_index),
            event:{id:id,title:title,kind:"point",display_date:null,media:[],selectable:false,
                index:min(items |> ~.event.index),priority:max(items |> c.option(~.event,"priority",0)),
                start:{value:min(items |> ~.event.start.value)},end:{value:max(items |> ~.event.end.value)}}}]
    }
}

pub fn layout(items, path, options, snapshot = null) {
    if (options.enabled!=true or len(items)==0) {events:items,clusters:[]} else {
        let distance=c.option(options,"distance",30);
        let keyed=[for (item in items) {*:item,_cluster_key:key(item,options)}];
        // A single stable sort avoids copying a growing partition for every member.
        let ordered=sort(keyed,{by:(item)=>[item._cluster_key,item.fraction]});
        let breaks=[0,for (index in 1 to (len(ordered)-1) where ordered[index]._cluster_key!=ordered[index-1]._cluster_key or
            abs(ordered[index].fraction-ordered[index-1].fraction)*path.length*(path.end-path.start)>distance) index,len(ordered)];
        let projected=[for (index in 0 to (len(breaks)-2))
            for (item in aggregate(slice(ordered,breaks[index],breaks[index+1]),path,options,snapshot)) item];
        {events:sort(projected,{by:(item)=>item.layout_index}),clusters:projected |: ~.cluster==true}
    }
}
