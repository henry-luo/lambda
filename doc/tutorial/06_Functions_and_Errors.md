# 6. Functions and Errors

You have called built-in functions and written a few one-line `fn`s. This chapter writes functions properly — parameters, anonymous functions, closures and recursion — and then turns to failure. In Lambda an error is a value, and a function that can fail says so in its type, so the compiler can check that every caller deals with it.

## Declaring Functions

`fn` declares a function. After `=>` comes a single expression; braces hold a block instead. Save this as `pricing.ls`:

```lambda
// pricing.ls
fn with_tax(price: float) float => price * 1.2

fn label(book) {
    let year = string(book.year)
    book.title ++ " (" ++ year ++ ")"
}

[with_tax(10.0), with_tax(40.0)];
label({title: "Clean Code", year: 2008})
```

```bash
lambda pricing.ls
```

```text
[12, 48]
"Clean Code (2008)"
```

Parameter and return types are optional, as `label` shows, and checked when present ([Chapter 5](05_Types_and_Schemas.md)). A block's value is *every* expression in it, not just the last one — `{ 1; 2 }` produces both numbers, as the top level of a script does — so write the intermediate steps as `let` bindings and end with a single expression. An `fn` has no `return`.

## Parameters

Save this as `params.ls`:

```lambda
// params.ls
fn price(amount: float, tax: float = 0.2, off: float = 0.0) float =>
    amount * (1 - off) * (1 + tax)

fn shelf(name: string, ...) => name ++ ": " ++ join(varg(), ", ")

shelf("Classics", "SICP", "TAOCP");
[price(40.0), price(40.0, 0.5), price(40.0, off: 0.5), price(off: 0.25, amount: 80.0)]
```

```bash
lambda params.ls
```

```text
"Classics: SICP, TAOCP"
[48, 60, 24, 72]
```

- `tax: float = 0.2` gives a default, used when the argument is left out.
- `name: value` passes an argument by name, in any order after the positional ones, so `price(40.0, off: 0.5)` skips `tax`.
- `...` accepts any number of further arguments, and `varg()` returns them as a list; `varg(0)` is the first.
- Leaving out a required argument is a compile error (E206), and so is an argument of the wrong type (E207).

## Functions as Values

`(params) => body` is an anonymous function, called an **arrow**; the parentheses are required. Functions are values: you can bind them with `let`, pass them to other functions and return them. Save this as `discounts.ls`:

```lambda
// discounts.ls
let books = input("books.json")^

fn discount(pct) => (price) => price * (100 - pct) / 100
fn reprice(rule) => books |> rule(~.price)

let ten_off = discount(10)
ten_off(50.0)
reprice(ten_off)
reprice((p) => p + 5)
sort(books, (b) => b.year) |> ~.year
reduce(books, (a, b) => if (a.price <= b.price) a else b).title
```

```bash
lambda discounts.ls
```

```text
45
[38.25, 49.5, 34.11, 44.1, 171]
[47.5, 60, 42.9, 54, 195]
[1968, 1985, 1999, 2008, 2017]
"Clean Code"
```

- `discount(10)` returns an arrow that still sees `pct`, the argument it was made with. Such a function is a **closure**. What it captures is a snapshot, which it can read but never change.
- `reprice` is a **higher-order** function: it takes the pricing rule as a parameter, so one function applies any rule.
- Built-ins take functions too: `sort` orders by the key an arrow computes, and `reduce` combines a collection pairwise — here, down to the cheapest book.

## Method-Style Calls

A built-in function can also be called on its first argument with a dot, which makes chains read left to right:

```text repl
λ> [3, 1, 2].sort()
[1, 2, 3]
λ> [3, 1, 2].sort().reverse()
[3, 2, 1]
λ> "clean code".upper()
"CLEAN CODE"
λ> [37.9, 42.5].sum()
80.4
```

`xs.sort()` is the same call as `sort(xs)`.

## Recursion

A function can call itself. `total` adds up numbers nested to any depth: the total of an array is the sum of its items' totals, and a number is its own total. Save this as `nested.ls`:

```lambda
// nested.ls
fn total(v) => if (v is array) sum(v |> total(~)) else v
total([1, [2, 3], [[4]]])
```

```bash
lambda nested.ls
```

```text
10
```

## Pure Functions

An `fn` is **pure**: its result depends only on its arguments, and calling it does nothing else. It cannot print, write a file or change a variable, and it cannot call a procedure — a `pn` — that does. Save this as `pure.ls`:

```lambda error=E224
// pure.ls
pn note(msg) {
    print(msg ++ "\n")
}

fn with_tax(price: float) float {
    note("taxing " ++ price)
    price * 1.2
}

with_tax(10.0)
```

```bash
lambda pure.ls
```

```text partial
pure.ls:7:5: error[E224]: 'note' is a procedure (pn) and cannot be called from a function (fn)
```

The rule runs one way: a `pn` may call any `fn`. Keep calculations in functions and actions in the procedures that call them; [Chapter 7](07_Procedures_IO_and_Tasks.md) covers procedures.

## Errors Are Values

Most operations that fail do not stop the program; they produce an **error value**. `error("…")` makes one, and a conversion returns one when it cannot convert:

```text repl
λ> let e = error("no book titled SICP")
λ> e is error
true
λ> e.message
"no book titled SICP"
λ> float("free") is error
true
λ> float("free") or 0.0
0
```

Errors are falsy, so `or` replaces one with a default. An error prints as just `error`; its `message` field says what went wrong.

## Returning Errors

A function can return an error like any other value. The return type `float | error` says so: a price, or an error. Save this as `soft.ls`:

```lambda
// soft.ls
fn parse_price(s: string) float | error {
    let p = float(s)
    if (p is error or p <= 0) error("bad price: " ++ s) else p
}

let prices = ["37.90", "12", "free", "-5"]
prices |> parse_price(~)
prices |> (parse_price(~) or 0.0)
prices |: parse_price(~) is error
```

```bash
lambda soft.ls
```

```text
[37.9, 12, error, error]
[37.9, 12, 0, 0]
["free", "-5"]
```

Such an error is ordinary data: it can sit in an array, and the caller may test it with `is error`, replace it with `or`, or pass it along. That suits bulk data, where a few bad rows should not stop the rest.

## Raising Errors

Some failures must not be overlooked. Mark the return type with `^` — `Book^` reads "a `Book`, or a raised error" — and `raise` the error. This program looks books up by title to total an order; save it as `orders.ls`:

```lambda
// orders.ls
type Book = {title: string, price: float}
let books = input("books.json")^

fn find_book(title: string) Book^ {
    let book = (books |: ~.title == title)[0]
    if (book == null) raise error("no book titled " ++ title)
    else book
}

fn line_total(line) float^ {
    let book = find_book(line.title)^
    book.price * line.qty
}

fn order_total(lines) float^ => sum(lines |> line_total(~)^)

let good = [{title: "Clean Code", qty: 2}, {title: "The Pragmatic Programmer", qty: 1}]
let bad = [{title: "Clean Code", qty: 2}, {title: "SICP", qty: 1}]
order_total(good) ^ { 0.0 }
order_total(bad) ^ { 0.0 }
order_total(bad) ^ { [^.code, ^.message] }
```

```bash
lambda orders.ls
```

```text
118.3
0
[318, "no book titled SICP"]
```

- `raise` ends the function at once with the error. Only a function declared with `^` may raise; anywhere else it is a compile error (E208).
- A postfix `^` **propagates**: `find_book(line.title)^` is the book, or else `line_total` fails on the spot with the same error. `order_total` does the same for each line, so one unknown title fails the whole order.
- `e ^ { … }` **handles** the error where it happens: when `e` fails, the block's value is used instead, and when it succeeds, its value passes through untouched. Inside the block, `^` is the error itself: `^.message` is its message and `^.code` its code — 318 for errors you create; errors from the runtime carry their own codes.

## Unhandled Errors Do Not Compile

A raised error cannot be dropped by accident. Save this as `ignored.ls`: it is `parse_price` again, now raising, with a call that ignores the error:

```lambda error=E228
// ignored.ls
fn parse_price(s: string) float^ {
    let p = float(s)
    if (p is error or p <= 0) raise error("bad price: " ++ s)
    else p
}

parse_price("37.90") * 2
```

```bash
lambda ignored.ls
```

```text partial
ignored.ls:8:1: error[E228]: error from 'parse_price' must be handled: use 'parse_price(...)^' to propagate, handle with 'parse_price(...) ^ { ... }', or recover with 'parse_price(...) or default'
```

The message lists the ways to engage the error: propagate it with `^`, handle it with `^ { … }`, or replace it with `or` (S7.5.1). A typed binding counts too: `let r: float | error = parse_price(s)` accepts the error as data. Built-ins that can fail follow the same rule, which is why reading a file is written `input("books.json")^`.

## Branching and Wrapping

The two-arm form `e ^ { … } ~ { … }` handles both outcomes: the first block runs on an error, with `^` bound to it, and the second on success, with `~` bound to the value. Inside a handler, `error("…", ^)` builds a new error that wraps the current one, adding context while keeping the cause in its `source` field. Save this as `report.ls`:

```lambda
// report.ls
fn parse_price(s: string) float^ {
    let p = float(s)
    if (p is error or p <= 0) raise error("bad price: " ++ s)
    else p
}

fn parse_line(line: string) float^ {
    let parts = split(line, ",")
    parse_price(parts[1]) ^ { raise error("rejected " ++ parts[0], ^) }
}

let lines = ["Clean Code,37.90", "SICP,free"];
[for (line in lines)
    parse_line(line) ^ { ^.message ++ " (" ++ ^.source.message ++ ")" }
                     ~ { "ok: " ++ ~ }]
```

```bash
lambda report.ls
```

```text
["ok: 37.9", "rejected SICP (bad price: free)"]
```

`parse_line` knows which line failed, which `parse_price` does not, so it raises a new error with that context and the original as its source; the report reads both messages. These handlers work on `fn` calls; on a procedure call, a handler is a statement of its own (S7.6.7v3), as [Lambda_Error_Handling.md](../Lambda_Error_Handling.md) describes.

## What You Learned

- `fn name(params) => expr` or `fn name(params) { lets; expr }` declares a function; parameters take types, defaults and names, and `...` with `varg()` takes the rest.
- `(x) => …` is an anonymous function. Functions are values: closures capture snapshots, and higher-order functions take functions as arguments.
- `xs.sort()` calls a built-in method-style, and functions may call themselves.
- An `fn` is pure and cannot call a `pn` (E224).
- Errors are values: `error("…")` makes one, `is error` tests for one, `or` supplies a default, and a `T | error` function returns them as data.
- A `T^` function `raise`s errors its callers must engage: `^` propagates, `^ { … }` handles with `^.message` and `^.code`, `^ { … } ~ { … }` branches, and ignoring the error is compile error E228.
- `error("…", ^)` wraps an error with context.

The references are [Lambda_Func.md](../Lambda_Func.md) and [Lambda_Error_Handling.md](../Lambda_Error_Handling.md). Next, [Chapter 7](07_Procedures_IO_and_Tasks.md) turns to procedures, I/O and tasks.
