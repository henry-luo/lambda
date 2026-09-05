// S16.10.1v2: words that can BEGIN a construct are barred as binding names,
// rejected at the declaration site. Keep the fixture syntactically valid so
// D8.2.5's separate build/bind pass reports the S16.10.1v2 admission error.
let type = 1
let int = 2
let if = 3
let state = 4
