import files: ~~.~~.demo.photos.mod_files
import io
pn main() {
    io.mkdir("temp/photos") ^ { null }
    let action = {version:1,target:"active-layer",name:"Mono",steps:[{id:"effect.add",options:{saturation:0.0}}]}
    files.write_action(action,"temp/photos/test-action.laction")^
    let loaded = files.read_action("temp/photos/test-action.laction")^
    output("{version:2}","temp/photos/bad-action.laction")^
    var rejected = false
    files.read_action("temp/photos/bad-action.laction") ^ { rejected = true }
    return [loaded == action,rejected]
}
