// Exact interval bounds prune offscreen subtrees; positions retain canonical order.
fn tree(rows, begin, end) {
    if (begin>=end) null else if (end-begin<=16) {
        let leaf=slice(rows,begin,end);
        {begin:begin,end:end,low:rows[begin].start,
            high:if (any(leaf |> ~.end==null)) null else max(leaf |> ~.end)}
    } else {
        let middle=begin+(end-begin) div 2;
        let left=tree(rows,begin,middle);
        let right=tree(rows,middle,end);
        {begin:begin,end:end,low:left.low,
            high:if (left.high==null or right.high==null) null else max(left.high,right.high),
            left:left,right:right}
    }
}

pub fn build(spec) {
    let rows=sort([for (position,item in spec.events)
        {position:position,id:item.id,start:item.start.value,end:item.end.value}],{by:(row)=>row.start});
    {events:spec.events,rows:rows,root:tree(rows,0,len(rows)),
        ids:sort([for (position,item in spec.events) {id:item.id,position:position}],{by:(row)=>row.id}),
        groups:sort(spec.groups,{by:(group)=>group.id})}
}

fn lower_bound(rows, id, begin, end) {
    if (begin>=end) begin else {
        let middle=begin+(end-begin) div 2;
        if (rows[middle].id<id) lower_bound(rows,id,middle+1,end)
        else lower_bound(rows,id,begin,middle)
    }
}
pub fn lookup(rows, id) {
    let position=lower_bound(rows,id,0,len(rows));
    if (position<len(rows) and rows[position].id==id) rows[position] else null
}
pub fn event(index, id) {
    let row=lookup(index.ids,id);
    if (row==null) null else index.events[row.position]
}
pub fn group(index, id) => lookup(index.groups,id)

fn outside(node, domain) => node==null or node.low!=null and node.low>domain.to
    or node.high!=null and node.high<domain.from
fn visit(index, node, domain) {
    if (outside(node,domain)) {positions:[],nodes:if (node==null) 0 else 1,examined:0}
    else if (node.left==null) {
        let rows=slice(index.rows,node.begin,node.end);
        {positions:[for (row in rows where (row.start==null or row.start<=domain.to)
            and (row.end==null or row.end>=domain.from)) row.position],nodes:1,examined:len(rows)}
    } else {
        let left=visit(index,node.left,domain);
        let right=visit(index,node.right,domain);
        {positions:[*left.positions,*right.positions],nodes:1+left.nodes+right.nodes,
            examined:left.examined+right.examined}
    }
}
pub fn query(index, domain) {
    let result=visit(index,index.root,domain);
    let positions=sort(result.positions);
    {*:result,positions:positions,events:[for (position in positions) index.events[position]]}
}
