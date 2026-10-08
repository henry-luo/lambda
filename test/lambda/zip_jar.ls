// S12.4.1v2/S14.3.1v2: JAR uses the captured ZIP tree and lazy member reads.
import .mod_zip_helpers
let archive = input("test/input/zip/sample.jar")^
let manifest = child(child(archive, "META-INF"), "MANIFEST.MF")
let compiled = child(child(archive, "example"), "Hello.class")
let resources = child(archive, "resources")
let opaque = child(resources, "opaque.bin")
let empty = child(resources, "empty.txt")
let class_bytes = input(compiled)^
let manifest_text = input(manifest, 'text')^;
[
    type(archive) == element,
    archive.name == "sample.jar",
    archive.kind == 'file',
    archive.format == 'zip',
    len(content(archive)^) == 4,
    manifest.entry_path == "META-INF/MANIFEST.MF",
    manifest_text == "Manifest-Version: 1.0\r\nMain-Class: example.Hello\r\nCreated-By: Lambda ZIP fixture\r\n\r\n",
    compiled.entry_path == "example/Hello.class",
    type(class_bytes) == binary,
    class_bytes[0 to 3] == b'\xCAFEBABE',
    input(compiled, 'binary')^ == class_bytes,
    content(compiled)^[0] == class_bytes,
    input(child(child(archive, "config"), "settings.json"))^.answer == 42,
    input(child(resources, "message.txt"), 'text')^ == "Hello from JAR!\n",
    input(opaque)^ == b'\x00FF504B00',
    input(opaque, 'binary')^ == b'\x00FF504B00',
    opaque.size == 5,
    empty.size == 0,
    input(empty, 'binary')^ == null,
    len(content(empty)^) == 0,
    input(child(resources, "é space.txt"), 'text')^ == "JAR unicode\n",
    input("test/input/zip/sample.jar", 'zip')^.format == 'zip',
    type(input("test/input/zip/sample.jar", 'binary')^) == binary
]
