// D6.3.3: a string or symbol equality keeps its left operand rooted while the right one
// allocates; otherwise a forced collection frees the left text and the right reuses its
// memory, so unequal texts compared equal under GC stress.
let names = ['normal', 'multiply', 'screen', 'overlay']
fn texts(name) => [for (b in names) string(b) == string(name)]
fn index(name) => sum([for (i, b in names) if (string(b) == string(name)) i else 0])
let runtime = {blend: symbol(string('normal'))}
let guarded = [for (b in names) (string(b) ^ { "" }) == (string(runtime.blend) ^ { "" })];
[texts('normal'), texts(runtime.blend), index(runtime.blend), guarded]
