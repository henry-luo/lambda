import readline: lambda.io.terminal

let source = readline.terminal("shell", 7, "λ> ")
let first = apply(source)
let second = apply(source);
[
  name(first) == 'terminal',
  first.id == "shell",
  len(content(first)) == 1,
  name(content(first)[0]) == 'frame',
  content(first)[0].id == 7,
  content(first)[0].prompt == "λ> ",
  content(first)[0].text == "",
  content(first)[0].caret == {region: "input", offset: 0, utf16_offset: 0},
  first == second
]
