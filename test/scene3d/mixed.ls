import s: lambda.scene3d
let viewport = s.normalize(s.scene([
    s.camera({id: "main", fov: 90.0}),
    s.light('ambient'),
    s.group([s.mesh(s.box([2.0, 2.0, 2.0]), s.material('lambert', {color: "#ff0000"}))])
], {id: "viewport", camera: "main", width: 128, height: 128,
    viewBox: "0 0 128 128", style: "position:absolute;left:16px;top:16px;display:block"}))^;
<html
    <head <style "html,body{margin:0;background:#ffffff}">>
    <body *[viewport,
        <svg width: 32, height: 32, viewBox: "0 0 32 32", style: "position:absolute;left:200px;top:0",
            <rect width: 32, height: 32, fill: "#00ff00">
        >,
        <p style: "position:absolute;top:150px", "Lambda, HTML, SVG and native 3D">
    ]>
>
