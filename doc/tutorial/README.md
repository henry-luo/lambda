# Lambda Tutorial

This tutorial takes you from installing Lambda to transforming data, processing documents, validating them against schemas, rendering pages and building a small interactive app. It has ten chapters of 20–30 minutes each; every chapter builds on the ones before it.

| Chapter | You will learn to |
|---|---|
| [1. Getting Started](01_Getting_Started.md) | Install Lambda, use the REPL, run scripts, and read an error |
| [2. Values and Collections](02_Values_and_Collections.md) | Write numbers, strings, symbols, arrays, maps and elements, and bind them with `let` |
| [3. Transforming Data](03_Transforming_Data.md) | Map, filter, sort, group and join data with pipes and `for` |
| [4. Documents as Data](04_Documents_as_Data.md) | Read JSON, CSV, Markdown and HTML, query the tree, and convert between formats |
| [5. Types and Schemas](05_Types_and_Schemas.md) | Annotate values, declare types, match on them, and validate files |
| [6. Functions and Errors](06_Functions_and_Errors.md) | Write functions and closures, and raise, propagate and handle errors |
| [7. Procedures, I/O and Tasks](07_Procedures_IO_and_Tasks.md) | Use `pn` procedures, `var`, loops, files, and concurrent tasks |
| [8. Rendering and Viewing](08_Rendering_and_Viewing.md) | Lay out HTML with CSS and render it to SVG, PDF and PNG |
| [9. Reactive UI](09_Reactive_UI.md) | Build an interactive page with `view` templates and event handlers |
| [10. Packages and Beyond](10_Packages_and_Beyond.md) | Use the bundled packages, the editor and JavaScript |

## How to Follow Along

Make a working folder and copy the sample files from [`data/`](data/) into it: [`books.json`](data/books.json), [`sales.csv`](data/sales.csv) and [`notes.md`](data/notes.md). Run every command from that folder. The chapters show the contents of any other file they use. Because the working folder is not where Lambda is installed, set `LAMBDA_HOME` as [Chapter 1](01_Getting_Started.md#get-lambda) explains.

Three conventions run through every chapter:

- A code block whose first line is a comment naming a file, such as `// hello.ls`, is a file to save under that name before running the command that follows it.
- Commands are written `lambda …`. In a source checkout the executable is `./lambda.exe` in the repository root; release bundles name it `lambda`.
- A plain block after a command shows exactly what the command prints. These outputs are checked against the current build by `make check-tutorial`, so what you see is what you should get.

## Reference

The tutorial explains each feature once and links to the reference for the details: [Lambda_Reference.md](../Lambda_Reference.md) indexes the full documentation, and [Lambda_Cheatsheet.md](../Lambda_Cheatsheet.md) fits the syntax on a few pages.
