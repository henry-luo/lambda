// Test input("dir", "dir") — directory listing with Path metadata
// Validates: string conversion, name/path/extension/size/is_dir/scheme/depth/modified

pn main() {
    var entries^err = input("test/input/test_dir_listing", "dir")
    if (^err) {
        print("error")
        return null
    }

    // T1: listing returns correct count (3 entries: data.csv, hello.txt, subdir)
    print("T1:" ++ string(len(entries)))

    // find specific entries by name
    var txt_entry = null
    var csv_entry = null
    var dir_entry = null
    var i = 0
    while (i < len(entries)) {
        let e = entries[i]
        if (e.name == "hello.txt") { txt_entry = e }
        else if (e.name == "data.csv") { csv_entry = e }
        else if (e.name == "subdir") { dir_entry = e }
        i = i + 1
    }

    // T2: name property
    print(" T2:" ++ txt_entry.name)

    // T3: extension property
    print(" T3:" ++ string(txt_entry.extension))

    // T4: is_dir for file
    print(" T4:" ++ string(txt_entry.is_dir))

    // T5: is_dir for directory
    print(" T5:" ++ string(dir_entry.is_dir))

    // T6: size of hello.txt (6 bytes: "hello\n")
    print(" T6:" ++ string(txt_entry.size))

    // T7: size of data.csv (5 bytes: "data\n")
    print(" T7:" ++ string(csv_entry.size))

    // T8: csv extension
    print(" T8:" ++ csv_entry.extension)

    // T9: scheme is "file"
    print(" T9:" ++ txt_entry.scheme)

    // T10: path contains the directory name
    print(" T10:" ++ string(contains(txt_entry.path, "test_dir_listing")))

    // T11: modified is a datetime (non-null)
    print(" T11:" ++ string(txt_entry.modified != null))

    // T12: string() conversion works on path
    print(" T12:" ++ string(string(csv_entry) != null))

    "done"
}
