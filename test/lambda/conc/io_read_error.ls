pn main() {
    var err = null
    io.read("test/lambda/conc/io_read_missing.txt") ^ { err = ^ }
    print(type(err))
}
