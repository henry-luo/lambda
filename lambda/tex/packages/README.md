# LaTeX Package Bindings

This directory contains JSON-based package definitions for Lambda's LaTeX pipeline.

## Package JSON Schema

Each package is defined as a `.pkg.json` file with the following structure:

```json
{
  "name": "package_name",
  "version": "1.0",
  "requires": ["dependency1", "dependency2"],
  "commands": {
    "command_name": {
      "type": "macro|primitive|constructor|environment|math",
      "params": "[]{}", 
      "replacement": "...",
      "pattern": "<element>#1</element>",
      "callback": "callback_name",
      "properties": {}
    }
  }
}
```

## Command Types

| Type | Description |
|------|-------------|
| `macro` | Simple text expansion (like `\def`) |
| `primitive` | Side effect execution (like `\relax`) |
| `constructor` | Produces whatsit for output (like `\section`) |
| `environment` | Begin/end pair (like `\begin{itemize}`) |
| `math` | Math-mode command (like `\sin`) |

## Parameter Specification

- `{}` - Required argument
- `[]` - Optional argument with empty default
- `[Default]` - Optional argument with default value

## Pattern Syntax

- `#1`, `#2`, ... - Argument substitution
- `?#1(yes)(no)` - Conditional (if #1 non-empty)
- `^<elem>` - Float up to parent (e.g., footnotes)

## Files

| File | Description |
|------|-------------|
| `tex_base.pkg.json` | TeX primitives |
| `latex_base.pkg.json` | LaTeX core commands |
| `amsmath.pkg.json` | AMS math package |
| `amssymb.pkg.json` | AMS symbols |
| `graphicx.pkg.json` | Graphics inclusion |
| `hyperref.pkg.json` | Hyperlinks |
