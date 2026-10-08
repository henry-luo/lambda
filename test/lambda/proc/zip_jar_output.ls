// S10.1.4/S12.1.1v2: package extensions require explicit ZIP output.
import ~~.mod_zip_helpers
pn main() {
    let original = input("test/input/zip/sample.jar")^
    output(original, "temp/zip-copy.jar", {format: 'zip', deterministic: true})^
    let copy = input("temp/zip-copy.jar")^
    let old_class = child(child(original, "example"), "Hello.class")
    let new_class = child(child(copy, "example"), "Hello.class")
    let manifest = child(child(copy, "META-INF"), "MANIFEST.MF")
    let resources = child(copy, "resources");
    [
        copy.format == 'zip',
        len(content(copy)^) == 4,
        input(old_class, 'binary')^ == input(new_class, 'binary')^,
        input(manifest, 'binary')^ == input(child(child(original, "META-INF"), "MANIFEST.MF"), 'binary')^,
        input(child(resources, "opaque.bin"), 'binary')^ == b'\x00FF504B00',
        input(child(resources, "message.txt"), 'text')^ == "Hello from JAR!\n",
        len(content(child(resources, "empty.txt"))^) == 0,
        input(child(child(copy, "config"), "settings.json"))^.answer == 42,
        input(child(resources, "é space.txt"), 'text')^ == "JAR unicode\n"
    ]
}
