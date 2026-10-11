// Resolve bundled assets from the entry script without changing cwd (D7.1.2v2).
pub fn entry_uri() string^ {
    let paths = [for (argument in sys.proc.self.argv# where ends_with(argument, ".ls")) argument]
    if (len(paths) == 0) raise error("Demo: no Lambda entry path in command line")
    else url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", paths[0])
}
