import physics: lambda.physics

<html
    <head <title "Lambda physics"><style "body { margin: 24px; background: #171c25; color: #edf2f8; font-family: sans-serif; } scene3d { display: block; }">>
    <body
        <h1 "Native rigid-body physics">
        <p "Three boxes fall onto the floor. Physics advances at 120 ticks per second.">
        <p <button id:"play", "Play"> " " <button id:"pause", "Pause"> " "
            <button id:"step", "Step"> " " <button id:"reset", "Reset">>
        <p id:"status", "Playing">
        <scene3d id:"world",width:720,height:440,camera:"main",background:"#202936",
            *[physics.world({state:'playing'}),
              <camera id:"main",type:'perspective',position:[6.0,4.0,8.0],target:[0.0,1.0,0.0]>,
              <light type:'ambient',intensity:0.4>,
              <light type:'directional',position:[3.0,5.0,4.0],intensity:0.8>,
              physics.body("floor",[physics.collider("floor-shape",'box',{size:[10.0,0.2,10.0],friction:0.7}),
                <mesh <geometry type:'box',size:[10.0,0.2,10.0]><material type:'lambert',color:"#64748b">>],
                {type:'fixed',position:[0.0,-0.1,0.0]}),
              for (i in 0 to 2) physics.body("box-"++string(i),[
                physics.collider("shape-"++string(i),'box',{size:[1.0,1.0,1.0],friction:0.6,restitution:0.15}),
                <mesh <geometry type:'box',size:[1.0,1.0,1.0]>
                    <material type:'lambert',color:["#60a5fa","#34d399","#fbbf24"][i]>>],
                {position:[float(i)-1.0,3.0+float(i),0.0]})]
        >
        <script type:"module", "
import { operate } from './_control.ls';
globalThis.physicsControl = (request) => {
    globalThis.physicsState = null;
    return operate(document.getElementById('world'), request).then(value => {
        globalThis.physicsState = value;
        document.getElementById('status').textContent = request.operation + (value.tick === undefined ? '' : ' — tick ' + value.tick);
    }).catch(error => {
        document.getElementById('status').textContent = error.message;
        throw error;
    });
};
for (const operation of ['play','pause','reset']) {
    document.getElementById(operation).addEventListener('click', () => physicsControl({operation}));
}
document.getElementById('step').addEventListener('click', () => physicsControl({operation:'step',ticks:1}));
        ">
    >
>
