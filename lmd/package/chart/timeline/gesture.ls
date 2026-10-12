// pointer state contains value geometry only; every window change uses the reducer.
import c: .common
import dates: .date
import coordinate: .coordinate
import time_map: .time_map
import util: lambda.chart.util

fn distance(a,b) => math.sqrt((a[0]-b[0])**2+(a[1]-b[1])**2)
fn center(points) => [(points[0].point[0]+points[1].point[0])/2.0,(points[0].point[1]+points[1].point[1])/2.0]
fn instant(plan, point) => time_map.invert(plan.time_map,coordinate.invert(plan.path,point).fraction)
fn begin(points, plan, window, original, surface = "main") => {points:points,start:points,plan:plan,window:window,original:original,moved:false,surface:surface}

pub fn update(current, action, plan, spec) {
    let gesture=current.gesture;
    let key=c.option(action,"pointer_id",1);
    let point=action.point;
    let valid=point is array and len(point)==2 and all(point |> util.finite_number(~));
    let overview=action.surface=="navigator" or gesture.surface=="navigator";
    if (action.type=="gesture_cancel") {gesture:null,window:gesture.original,moved:gesture.moved}
    else if (action.type=="gesture_start") {
        if (not valid or not overview and spec.interaction.pan!=true and spec.interaction.pinch!=true) {gesture:gesture}
        else if (gesture==null) {gesture:begin([{id:key,point:point}],plan,current.window,current.window,c.option(action,"surface","main"))}
        else if (spec.interaction.pinch==true and len(gesture.points)==1 and gesture.points[0].id!=key)
            {gesture:begin([*gesture.points,{id:key,point:point}],plan,current.window,gesture.original)}
        else {gesture:gesture}
    } else if (gesture==null or not contains(gesture.points |> ~.id,key)) {gesture:gesture}
    else if (action.type=="gesture_end") {
        let remaining=gesture.points |: ~.id!=key;
        {gesture:if (len(remaining)==0) null else begin(remaining,plan,current.window,gesture.original,gesture.surface),moved:gesture.moved}
    } else if (not valid) c.fail("gesture.point","expected finite two-dimensional coordinates")
    else {
        let points=[for (item in gesture.points) if (item.id==key) {id:key,point:point} else item];
        let moved=gesture.moved or any([for (index,item in points) distance(item.point,gesture.start[index].point)>3]);
        let window=if (len(points)>1) {
            let first=distance(gesture.start[0].point,gesture.start[1].point);
            let now=distance(points[0].point,points[1].point);
            let factor=if (now<=0 or first<=0) 1.0 else first/now;
            let anchor=instant(gesture.plan,center(gesture.start));
            let shift=anchor-instant(gesture.plan,center(points));
            {from:dates.offset(anchor,decimal(gesture.window.from-anchor)*decimal(factor)+decimal(shift)),
                to:dates.offset(anchor,decimal(gesture.window.to-anchor)*decimal(factor)+decimal(shift))}
        } else if (spec.interaction.pan==true or overview) {
            let delta=(instant(gesture.plan,gesture.start[0].point)-instant(gesture.plan,points[0].point))*(if (overview) -1 else 1);
            {from:dates.offset(gesture.window.from,delta),to:dates.offset(gesture.window.to,delta)}
        } else null;
        {gesture:{*:gesture,points:points,moved:moved},window:if (moved) window else null,moved:moved}
    }
}
