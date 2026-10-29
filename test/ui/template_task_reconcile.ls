// S12.1.1v3 / D5.1.1v3: task notifications write state; the host projects the pure view.
import dom
import t: lambda.scene3d.transform

fn layers(depth,day) element => if (depth==0)
 <mesh geometry:"blade",material:"leaf",instances:[for (i in 0 to (if (day==0) 0 else 399))
  t.matrix([float(i%20)*0.13-1.25,0.0,float(int(i/20))*0.13-1.25],
   [0.0,float(day)*0.15,0.0],[1.0,1.0+float(day)*0.1,1.0])^]>
 else <group layers(depth-1,day)>

// rebuild recipe layers without increasing the native scene hierarchy.
fn generated(depth,day) element {
 let part=if (depth==0) layers(40,day) else generated(depth-1,day);
 <group 'data-recipe-layer':depth,part[0]>
}

fn scene(day) element => <scene3d id:"task-scene",width:240,height:180,camera:"camera",background:"#18242c",
 <resources
  <geometry id:"blade",type:"buffer",positions:[-0.035,0.0,0.0,0.035,0.0,0.0,0.0,0.6,0.0],normals:[0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0]>;
  <material id:"leaf",type:"lambert",color:"#74b84b",side:"double">>;
 <light type:"ambient",intensity:0.5>;<light type:"directional",position:[2.0,4.0,3.0],intensity:1.0>;
 <camera id:"camera",type:"perspective",position:[3.3,2.6,4.3],target:[0.0,0.25,0.0],fov:40.0>;
 (if (day==0) layers(0,day) else generated(600,day))>

pn advance(root) {
 let progress=dom.get_element_by_id(root,"progress")
 if (progress!=null) dom.dispatch(progress,{type:"complete",bubbles:false})
}

view <progress> state busy:false {
 <span id:"progress",'data-busy':string(busy),if (busy) "working" else "idle">
}
on begin(evt) {
 if (evt.event_phase!=2) { return 'pass' }
 busy=true
 start(advance,[dom.root_node(evt.target)],{owner:'document'})
 'handled'
}
on complete(evt) {
 if (evt.event_phase!=2) { return 'pass' }
 let app=dom.get_element_by_id(dom.root_node(evt.target),"app")
 dom.dispatch(app,{type:"advanced",bubbles:false})
 busy=false
 'handled'
}

view <task_garden> state day:0,running:false,locale:"en" {
 <body <main id:"app",'data-running':string(running),'data-locale':locale,
  <h1 id:"date",if (day==0) "2026-01-01" else if (day==1) "2026-01-31" else "2026-03-02">;
  <button id:"month","Advance 30 days">;<button id:"run",if (running) "Pause" else "Run">;
  <button id:"locale",locale>;
  apply(<progress>);
  scene(day)>>
}
on advanced(evt) {
 if (evt.event_phase!=2) { return 'pass' }
 day=day+1
 'handled'
}
on click(evt) {
 let id=dom.get_attribute(evt.target,"id")
 if (id=="month") {
  dom.dispatch(dom.get_element_by_id(dom.root_node(evt.target),"progress"),{type:"begin",bubbles:false})
 } else if (id=="run") { running=not running }
 else if (id=="locale") { locale=if (locale=="en") "zh-CN" else "en" }
 'handled'
}

<html <head <title "Task-owned garden projection">>;apply(<task_garden>)>
