// Run: ./lambda.exe view test/demo/scene3d/outdoors.ls
import dom
import resources: ~~.mod_resources
import meadow: ~~.procart.mod_meadow
import viewer: ~~.procart.mod_scene_view
import garden: .mod_outdoors
let SOURCE=input(url_resolve(resources.entry_uri()^,"../procart/meadow.mark"),'mark')^
let MODEL=meadow.generate(SOURCE)^
view <outdoor_study> state mist:true,wind:true,motion:false,cascades:3,lens:garden.home() {
 <body <main <header <div <a href:"index.html","Scene3D /"> <p "OUTDOOR RENDERING STUDY"> <h1 "Light through the garden.">
   <p "Native height fog, leaf backlighting, instanced grass wind, cascaded sun shadows and refractive pond water.">>;
   <nav <button id:"mist",'aria-pressed':string(mist),if (mist) "Clear the mist" else "Bring the mist">;
    <button id:"wind",'aria-pressed':string(wind),if (wind) "Still grass" else "Bend the grass">;
    <button id:"motion",'aria-pressed':string(motion),if (motion) "Pause motion" else "Play wind & water">;
    <button id:"cascades",'aria-pressed':string(cascades==3),if (cascades==3) "Single shadow map" else "Three cascades">>>;
  <section class:"stage",viewer.interactive(garden.viewport(MODEL,{mist:mist,wind:wind,motion:motion,cascades:cascades},lens)^,garden.home(),lens)>;
  <footer <p id:"outdoor-status",(if (mist) "Mist" else "Clear")++" · "++string(cascades)++" shadow maps · "++(if (motion) "Motion playing" else "Motion paused")>;
   <p "Water geometry and finite optical depth are authored here. The renderer animates appearance; ecological and hydrological time do not advance.">>>>
}
on click(evt) {
 let id=dom.get_attribute(evt.target,"id")
 if (not contains(["mist","wind","motion","cascades"],id)) {return 'pass'}
 lens=viewer.snapshot(evt.target,"meadow-camera",garden.home())^
 if (id=="mist") {mist=not mist}
 else if (id=="wind") {wind=not wind}
 else if (id=="motion") {motion=not motion}
 else {cascades=if (cascades==3) 1 else 3}
 'handled'
}
<html lang:"en",<head <meta name:"viewport",content:"width=device-width,initial-scale=1"> <title "Outdoor study · Scene3D">
 <link rel:"stylesheet",href:"outdoors.css">>;apply(<outdoor_study>)>
