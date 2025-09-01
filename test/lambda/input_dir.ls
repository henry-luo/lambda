let cwd = input('./test/input/dir', null)
for item in cwd {
    { "name": item.name, "size": item.size, "is_link": item.is_link }
}