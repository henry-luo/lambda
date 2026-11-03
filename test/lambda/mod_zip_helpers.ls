pub fn child(node, name) any^ => [for (entry in content(node)^ where entry.name == name) entry][0]
