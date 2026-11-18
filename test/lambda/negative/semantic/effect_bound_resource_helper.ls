// S12.1.6v2: a legacy input helper still acquires a resource.
fn load(path) any^ => input(path)^
pn bounded(path) any^ @clock { load(path)^ }
