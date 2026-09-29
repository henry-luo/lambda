# 7. Procedures, I/O and Tasks

Everything so far has been pure: expressions and `fn` functions compute values and change nothing. Real scripts also print progress, keep counters, write files and wait on slow work, and in Lambda those actions live in **procedures**, declared with `pn`. This chapter covers procedures and variables, loops, how values are copied and shared, files and folders, and concurrent tasks. Run every script in it with `lambda run`.

## Procedures and `lambda run`

`lambda run script.ls` calls the script's `pn main()`, and `print` writes to the terminal. Save this as `launch.ls`:

```lambda
// launch.ls
pn main() {
    print("T minus", 3, "\n")
    print("lift")
    print("off!\n")
    "launched"
}
```

```bash
lambda run launch.ls
```

```text
T minus 3
liftoff!
"launched"
```

- `print` writes its arguments as plain text, joined by single spaces, and adds no newline, so `"lift"` and `"off!\n"` end up on one line. The value `main` returns is printed in Lambda notation, which is why `"launched"` keeps its quotes.
- Without `run`, `main` is never called: `lambda launch.ls` evaluates only the top level, which holds no expressions, and prints `null`.
- A procedure may call functions and other procedures, but a function may not call a procedure (error E224), so the pure part of a program stays pure.

## Variables with `var`

`let` bindings are final. Inside a procedure, `var` declares a variable you can reassign. Without an annotation it accepts any value, and its type widens to follow. With one, the type is a contract: each assignment is checked before it happens, and a value that does not fit stops the procedure with error E201 instead of changing the variable (S11.4.1v3):

```lambda
// vars.ls
pn main() {
    var x = 42
    x = x / 8
    print(x, type(x), "\n")
    x = "forty-two"
    print(x, type(x), "\n")
    var level: int = 1
    level = level + 1
    level = "high"
}
```

```bash
lambda run vars.ls
```

```text partial
5.25 float
forty-two string
error[E201]: type check at assignment to 'level' failed: expected int, got string 'high'
```

The script prints two lines, then stops at `level = "high"` — an error found while the procedure runs, not before it starts like the one in [Chapter 1](01_Getting_Started.md#when-something-goes-wrong). `var` exists only inside a `pn`, as Lambda has no global variables, and a `let` inside a procedure is still final (reassigning it is error E211).

## Loops

`while (cond) { … }` repeats while the condition is truthy. `break` leaves the loop, `continue` skips to the next round, and `return` leaves the whole procedure at once. Inside a procedure, `for (x in xs) { … }` also works as a plain loop:

```lambda
// loops.ls
pn main() {
    var odds = []
    var i = 0
    while (true) {
        i = i + 1
        if (i > 9) { break }
        if (i % 2 == 0) { continue }
        odds = odds ++ [i]
    }
    print("odds:", odds, "\n")
    for (x in odds) {
        if (x > 4) { return x }
    }
    null
}
```

```bash
lambda run loops.ls
```

```text
odds: [1, 3, 5, 7, 9]
5
```

`return` leaves `main` as soon as the `for` loop meets an odd number over 4; a procedure that runs to its end returns its last expression, as a function does. When you are computing a value rather than performing steps, the pipes and `for` expressions of [Chapter 3](03_Transforming_Data.md) are usually shorter.

## Values Are Copied

If you come from JavaScript or Python, read this section twice. Assigning a map or an array to another variable **copies** it, and so does passing it to a parameter or storing it in another container (S9.1.2). To change a value for its caller, a procedure declares a **`var` parameter**, which writes through to the caller's variable:

```lambda
// copies.ls
pn bump(var xs: int[]) {
    for (i in 0 to len(xs) - 1) { xs[i] = xs[i] + 1 }
}

pn main() {
    var a = {x: 0}
    var b = a
    b.x = 1
    print("a.x =", a.x, "b.x =", b.x, "\n")
    var scores: int[] = [70, 85, 92]
    bump(scores)
    scores
}
```

```bash
lambda run copies.ls
```

```text
a.x = 0 b.x = 1
[71, 86, 93]
```

`b` got its own map, so changing it cannot reach `a`. Copies are cheap — memory is shared until one side changes — and they let you reason about each procedure on its own. The argument for a `var` parameter must itself be a `var` (error E211 otherwise), and a typed `var` parameter is invariant (S9.1.3): an unannotated `var scores = [70, 85, 92]` is a plain `array`, which `var xs: int[]` rejects with E207, so annotate the variable as here or leave the parameter untyped (`var xs`). A plain parameter is the procedure's own copy, which it may change without the caller ever seeing it.

## Sharing a Record

When two places must see the same changing record — a book that is both on your reading list and among the staff picks — give the record one home and let everyone else hold its key:

```lambda
// library.ls
pn check_out(var books, id: int) {
    books[id].out = true
}

pn main() {
    var books = [{title: "SICP", out: false}, {title: "Clean Code", out: false}]
    let reading_list = [1]
    let staff_picks = [0, 1]
    check_out(books, reading_list[0])
    staff_picks |> books[~].title ++ (if (books[~].out) ": out" else ": in")
}
```

```bash
lambda run library.ls
```

```text
["SICP: in", "Clean Code: out"]
```

The books live only in `books`; both lists hold positions in it, and `check_out` changes the one real record through a `var` parameter. Had `reading_list` held the record itself, `check_out` would have changed a copy, and the staff picks would still say `in`. The store stays plain data, so you can print or save it at any point. [Sharing Mutable State](../Lambda_Procedural.md#sharing-mutable-state) works through a larger example.

## Writing and Reading Files

`output(value, file)` writes a value to a file. The format comes from the extension — `.json`, `.yaml`, `.xml`, `.md`, `.html` and more — or from a third argument such as `'json'`, and a string is written as it is. Writing can fail, so the call must engage its error, here with a postfix `^`; leaving it out is error E228. `{mode: "append"}` adds to the end of a file instead of replacing it, and `input` reads a file back:

```lambda
// save.ls
pn main() {
    let books = input("books.json")^
    let classics = books |: ~.year < 2000 |> {title: ~.title, year: ~.year}
    output(classics, "classics.json")^
    output("saved " ++ len(classics) ++ " classics\n", "history.txt")^
    output("done\n", "history.txt", {mode: "append"})^
    print(input("history.txt")^)
    input("classics.json")^ == classics
}
```

```bash
lambda run save.ls
```

```text
saved 3 classics
done
true
```

A `.txt` file reads back as one string, and the classics survive the round trip through JSON unchanged.

## Files and Folders

The `io` module manages files and folders from a procedure, with no import: `io.mkdir` creates a folder and any missing parents, `io.copy` copies a file, and `io.delete` removes a file or a folder, contents and all — so point it only at paths you mean. `exists` checks a path. This script first deletes the archive a previous run left behind, so you can run it again and again:

```lambda
// archive.ls
pn main() {
    if (exists("archive")) { io.delete("archive")^ }
    io.mkdir("archive/2026")^
    io.copy("sales.csv", "archive/2026/sales.csv")^
    output("sales.csv: January orders\n", "archive/2026/README.txt")^
    exists("archive/2026/sales.csv")
}
```

```bash
lambda run archive.ls
```

```text
true
```

```bash
cat archive/2026/README.txt
```

```text
sales.csv: January orders
```

To rehearse a script that changes files, run it with `lambda run --dry-run archive.ls`: `main` runs, but file and network operations are skipped and answer with stand-ins — `exists` says `false` and `input` returns a placeholder string — so nothing on disk changes. `io.move`, `io.touch`, `cmd` and the other procedural I/O functions are listed in [Lambda_Sys_Func.md](../Lambda_Sys_Func.md#procedural-io-functions).

## Worked Example: A Sales Report

Time to put the pieces together. This script reads `sales.csv`, totals the revenue of each region, and writes the totals twice: as JSON for other programs and as a Markdown table for people. The calculation stays in a pure `fn`; the procedure does the reading, looping and writing.

```lambda
// report.ls
fn revenue(row) => int(row.units) * decimal(row.price)

pn main() {
    let rows = input("sales.csv")^
    var totals = {}
    for (row in rows) {
        totals[row.region] = (totals[row.region] or 0) + revenue(row)
    }
    output(totals, "report.json")^
    var md = "# Revenue by Region\n\n| Region | Revenue |\n|---|---:|\n"
    for (region, amount at totals) {
        md = md ++ "| " ++ region ++ " | " ++ amount ++ " |\n"
    }
    output(md, "report.md")^
    print("wrote report.json and report.md:", len(totals), "regions\n")
}
```

CSV fields are strings, so `int` and `decimal` convert them; revenue is money, so it is a decimal. The first time a region appears, `totals[row.region]` is `null` and `or 0` starts it from zero, and the assignment adds or replaces that field. `for (region, amount at totals)` walks a map's fields in order, and `++` turns numbers into text as it joins.

```bash
lambda run report.ls
```

```text
wrote report.json and report.md: 3 regions
```

```bash
cat report.json
```

```text
{
  "EMEA": 186.0,
  "APAC": 186.5,
  "AMER": 382.0
}
```

```bash
cat report.md
```

```text
# Revenue by Region

| Region | Revenue |
|---|---:|
| EMEA | 186.0 |
| APAC | 186.5 |
| AMER | 382.0 |
```

## Running Tasks

Procedures can also run side by side as **tasks**. `start(p, args)` launches procedure `p` with an array of arguments and returns a handle at once, and `wait(h)^` parks until the task finishes and gives its value. `wait(h, timeout: ms)` gives up after `ms` milliseconds with error 310 while the task keeps running; to inspect that failure instead of propagating it, receive it in a binding typed `T | error`, as in [Chapter 6](06_Functions_and_Errors.md). Tasks take turns on one event loop, and a call that parks, such as `sleep(ms)`, looks like any other call — there are no `async` or `await` keywords (S13.1.2v2):

```lambda
// tasks.ls
pn square(n) {
    sleep(20 * n)^
    print("finished " ++ n ++ "\n")
    n * n
}

pn main() {
    let a = start(square, [3])
    let b = start(square, [1])
    let early: int | error = wait(a, timeout: 10)
    if (early is error) { print("still waiting: " ++ early.message ++ "\n") }
    [wait(a)^, wait(b)^]
}
```

```bash
lambda run tasks.ls
```

```text
still waiting: task wait timed out
finished 1
finished 3
[9, 1]
```

The first wait gives up while `a` is still sleeping; the shorter task `b` finishes first, but `wait` hands back results in the order you ask for them. `wait`, `sleep`, `send` and `receive` can all fail, so each call is engaged with `^` or a typed binding. Tasks share no variables — a started procedure may not even capture a `var` (S13.1.4) — so they talk by message: `send(h, value)` puts a value in the task's mailbox, and `receive()` takes the oldest one, parking while the mailbox is empty:

```lambda
// mailbox.ls
pn adder() {
    var total = 0
    while (true) {
        let msg = receive()^
        if (msg == 'stop') { return total }
        total = total + msg
    }
}

pn main() {
    let h = start(adder)
    for (msg in [10, 20, 'stop']) { send(h, msg)^ }
    wait(h)^
}
```

```bash
lambda run mailbox.ls
```

```text
30
```

[Lambda_Concurrency.md](../Lambda_Concurrency.md) covers the rest: `select` for the first of several results, `cancel`, how a block waits for the tasks it started, and errors across tasks.

## What You Learned

- `lambda run` calls `pn main()`; `print` writes plain text with no newline, and `main`'s value is printed in Lambda notation.
- `var` variables can be reassigned — unannotated ones widen, annotated ones reject a value of the wrong type (E201) — and `while`, `for`, `break`, `continue` and `return` steer a procedure.
- Assignment copies containers; a `var` parameter writes through to its caller, and a store plus keys shares a record between places.
- `output(value, file)^` writes in the format of the extension, `{mode: "append"}` appends, `input` reads back, and `io.mkdir`, `io.copy` and `io.delete` manage files and folders.
- `start`, `wait`, `wait(h, timeout: ms)` and `send`/`receive` run and coordinate tasks.

The reference for procedures is [Lambda_Procedural.md](../Lambda_Procedural.md). Next, [Chapter 8](08_Rendering_and_Viewing.md) lays out and renders HTML and CSS.
