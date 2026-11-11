import c: lambda.sync.core

let s = c.empty("test",'mark','map',null)
let a = c.set_field(s,"alice",c.root,"title",c.scalar("Alice"))^
let b = c.set_field(s,"bob",c.root,"title",c.scalar("Bob"))^
let merged = c.merge(a,b)^
let resolved = c.set_field(merged,"alice",c.root,"title",c.scalar("Both"))^;
[len(c.field(merged,c.root,"title")) == 2,
 c.merge(a,b)^ == c.merge(b,a)^,
 c.merge(merged,merged)^ == merged,
 len(c.field(c.merge(c.compact(resolved),a)^,c.root,"title")) == 1,
 c.field(resolved,c.root,"title")[0].value.value == "Both"]
