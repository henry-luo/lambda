# 1. Getting Started

In this chapter you install Lambda, evaluate expressions in the REPL, and run your first scripts. By the end you will know the two ways a script produces output and how to read an error message.

## Get Lambda

**Download a release.** The [Releases](https://github.com/henry-luo/lambda/releases) page has a zip per platform. Unzip it anywhere: the folder holds the `lambda` executable and an `lmd/` directory with the runtime's packages, schemas and fonts. Keep the two together. Lambda looks for `lmd/` in the **current** folder, so if you add the executable to your `PATH` and run it from elsewhere, also set the environment variable `LAMBDA_HOME` to the absolute path of `lmd/` — otherwise commands that need the bundled packages or schemas fail.

## Check the Installation

```bash
lambda --help
```

```text partial
Usage:
  lambda                       - Start a functional REPL session (default)
  lambda <script.ls>           - Run a script file
```

The help lists every command. [Lambda_CLI.md](../Lambda_CLI.md) describes each one in full.

## The REPL

Run `lambda` with no arguments to start the REPL (read–eval–print loop). Type an expression and press Enter to see its value:

```text repl
λ> 1 + 2
3
λ> let x = 5
λ> x * 3
15
λ> [1, 2] ++ [3]
[1, 2, 3]
λ> {name: "Ada", langs: ['en', 'fr']}
{
  name: "Ada",
  langs: ['en', 'fr']
}
```

- A `let` binding produces no value, so the REPL prints nothing for it; the name stays bound for later lines.
- Values print in Lambda's own notation: strings in double quotes, symbols such as `'en'` in single quotes, maps in braces.
- An incomplete line continues on the next one, with the continuation prompt `..`.
- Each line runs once: earlier lines are never re-run. A line that fails is reported and rolled back, so the session keeps its last good state.
- `help` lists the REPL commands, `clear` starts a fresh session, and `quit` exits. The prompt is `λ>` on UTF-8 terminals and `>` elsewhere.

## Your First Script

A script is a file of Lambda code, conventionally named `*.ls`. Save this as `hello.ls`:

```lambda
// hello.ls
let name = "Lambda"
"Hello, " ++ name ++ "!"
```

Run it:

```bash
lambda hello.ls
```

```text
"Hello, Lambda!"
```

`++` joins strings. The script's **result** is the value of its last expression — or rather, of all its top-level expressions: each one is printed on its own line, in the same notation as the REPL, which is why the greeting keeps its quotes.

```lambda
// values.ls
let pi = 3.14159
pi * 2;
[1, 2, 3]
{title: "Notes", pages: 12}
```

```bash
lambda values.ls
```

```text
6.28318
[1, 2, 3]
{
  title: "Notes",
  pages: 12
}
```

Notice the `;` after `pi * 2`. A line break normally ends a statement, but a line that starts with `[`, `(`, `-` or a few other tokens could also continue the previous expression — `pi * 2 [1, 2, 3]` — and Lambda never guesses: it asks you to end the previous line with `;`. [Chapter 2](02_Values_and_Collections.md#one-statement-per-line) explains the rule.

## Scripts That Do Things

Top-level code is **pure**: it computes values but cannot print, write files or change variables. Actions live in **procedures**, declared with `pn` instead of `fn`. `lambda run` runs a script's `main` procedure:

```lambda
// greet.ls
pn greet(name) {
    print("Hello, " ++ name ++ "!\n")
}

pn main() {
    greet("Ada")
    greet("Grace")
    "done"
}
```

```bash
lambda run greet.ls
```

```text
Hello, Ada!
Hello, Grace!
"done"
```

`print` writes text as it is — no quotes — and adds no newline of its own, so the string ends with `"\n"`. After `main` returns, `lambda run` prints its value, here the string `"done"`, in Lambda notation. The split between pure functions and procedures is one of Lambda's central ideas; [Chapter 6](06_Functions_and_Errors.md) and [Chapter 7](07_Procedures_IO_and_Tasks.md) come back to it.

## When Something Goes Wrong

Lambda checks a script before it runs it. Save this as `oops.ls`:

```lambda error=E201
// oops.ls
let total: int = "three"
total + 1
```

```bash
lambda oops.ls
```

```text partial
oops.ls:2:1: error[E201]: cannot initialize 'total' of type int with string
1 error(s) found.
```

A diagnostic names the file, line and column, an error code and a message, and the terminal output also shows the offending source line. Codes in the 100s are syntax errors, the 200s type and compile errors, the 300s run-time errors and the 400s I/O errors; [Lambda_Error_Handling.md](../Lambda_Error_Handling.md#error-code-categories) lists them.

## Commands at a Glance

| Command | What it does | Chapter |
|---|---|---|
| `lambda` | Start the REPL | 1 |
| `lambda script.ls` | Run a script and print its result | 1–6 |
| `lambda run script.ls` | Run a script's `main()` procedure | 7 |
| `lambda convert in -t format -o out` | Convert between formats | 4 |
| `lambda validate file -s schema.ls` | Validate a file against a schema | 5 |
| `lambda layout page.html` | Lay out a page and print its box tree | 8 |
| `lambda render page.html -o page.svg` | Render to SVG, PDF, PNG or JPEG | 8 |
| `lambda view page.html` | Open a document in the viewer window | 8–9 |
| `lambda edit notes.md` | Edit Markdown, HTML or SVG and save it back | 10 |
| `lambda js app.js` | Run JavaScript | 10 |

## What You Learned

- The REPL evaluates one expression at a time and prints its value.
- `lambda script.ls` prints a script's top-level values; `lambda run script.ls` runs its `main()` procedure, which can print and perform other actions.
- Values print in Lambda notation; `print` writes plain text with no newline.
- Diagnostics carry a location, a code and a message; `log.txt` holds the details.

Next, [Chapter 2](02_Values_and_Collections.md) tours the values Lambda works with.
