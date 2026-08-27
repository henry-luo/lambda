// S16.10.1v2: words that can BEGIN a construct are barred as binding names,
// rejected with E201 at the declaration site — not silently accepted and then
// failing at every use (`let type = 1` used to read the base type instead).
let type = 1
let int = 2
let if = 3
let state = 4
type + int
