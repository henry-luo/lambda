import s: lambda.scene3d
fn viewport(path, id, label) element^ {
    let asset = s.load(path, {id: id, autoplay: true})^
    let scene = s.normalize(s.scene([s.camera({id: id ++ "-camera", position: [0.0, 0.0, 4.0]}),
        s.light('ambient'), asset], {id: id ++ "-view", width: 360, height: 320,
        camera: id ++ "-camera", background: "#101b30", style: "display:block"}))^;
    <div class: "card", *[scene, <h2 label>]>
}
<html
    <head <style "body{margin:32px;background:#080e1c;color:#e0e8ff;font-family:sans-serif}h1{font-size:26px}.card{display:inline-block;margin:12px}h2{font-size:16px;margin:16px 0}">>
    <body *[<h1 "Imported assets · native OpenGL">,
        <p "OBJ material and texture dependencies, glTF skin + morph + cubic playback, and A3D skeletal actions.">,
        viewport("test/demo/scene3d/assets/loading/concave.obj", "obj", "OBJ · concave textured arrow")^,
        viewport("test/demo/scene3d/assets/loading/articulated.gltf", "gltf", "glTF · articulated pennant")^,
        viewport("test/demo/scene3d/assets/loading/puppet.a3d", "a3d", "A3D · skeletal pennant")^]>
>
