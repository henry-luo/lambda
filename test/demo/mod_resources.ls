// Resolve bundled assets from the entry script without changing cwd (D7.1.2v2).
pub fn entry_uri() string^ {
    let paths = [for (argument in sys.proc.self.argv# where ends_with(argument, ".ls")) argument]
    if (len(paths) == 0) raise error("Demo: no Lambda entry path in command line")
    else url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", paths[0])
}

// Embedded slide hosts need the deck's resource base, independently of the host script.
fn slide_object(node,base) => if (not (node is element)) node
    else if (name(node) == 'image' and node.src is string and node.src != "")
        <image *:{*:map(node),src:url_resolve(base,node.src)},*content(node)>
    else if (name(node) == 'group') <group *:map(node),*[for (child in content(node)) slide_object(child,base)]>
    else node
fn slide_child(node,base) => if (not (node is element)) node
    else if (name(node) == 'slide') <slide *:map(node),*[for (child in content(node)) slide_object(child,base)]>
    else if (name(node) == 'master') <master *:map(node),*[for (child in content(node)) slide_object(child,base)]>
    else node
pub fn slide_sources(deck,base) => <presentation *:map(deck),*[for (child in content(deck)) slide_child(child,base)]>
