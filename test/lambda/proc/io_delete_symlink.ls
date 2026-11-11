// deleting a link removes its entry, including when its target is absent.
import disk: lambda.io.disk
pn main() {
    let area = "temp/io-delete-symlink"
    io.delete(area) ^ { null }
    io.mkdir(area ++ "/target")^
    output("keep",area ++ "/target/kept.txt")^
    io.symlink("missing",area ++ "/dangling")^
    io.symlink("target",area ++ "/directory")^
    io.delete(area ++ "/dangling")^
    io.delete(area ++ "/directory")^
    let missing = io.delete(area ++ "/absent") ^ { ^.message };
    [input(area ++ "/target/kept.txt",'text')^ == "keep",
     len(disk.entries(area,"",10)^) == 1,starts_with(missing,"io.delete: failed to delete")]
}
