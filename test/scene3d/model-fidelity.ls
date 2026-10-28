// D7.2.4v4 / S12.1.1v3: imported camera nodes and independent map data stay ordinary values.
import s: lambda.scene3d
let model = s.load("test/scene3d/assets/fidelity/model.gltf", {id: "fidelity"})^;
<html <head <style "html,body{margin:0}">> <body s.scene([
    model,
    s.camera({id: "override", type: 'orthographic', view_height: 2.0}),
    s.light('ambient')
], {id: "fidelity-scene", width: 128, height: 128, background: "#000000", style: "display:block"})>>
