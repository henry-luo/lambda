pub fn child(node, name) => [for (entry in content(node)^ where entry.name == name) entry][0]
