# Tree-sitter LaTeX Grammar Size Optimization Guide

**Goal:** Reduce the final compiled Tree-sitter parser binary (`.so` / `.dll`) size by refactoring the *grammar itself*, primarily by shrinking LR parse tables (number of states and actions).

This document focuses exclusively on **grammar-level refactors**. Compiler and linker optimizations are intentionally excluded.

---

## 1. Why Grammar Structure Dominates Binary Size

In Tree-sitter, binary size is driven mainly by:

- Number of LR states
- Number of parse actions per state
- Degree of ambiguity and conflict resolution

Every additional alternative, optional branch, or recursive content expansion multiplies states. The goal is therefore **structural simplification**, not syntactic completeness.

---

## 2. Collapse Command Families into a Generic `command`

### Problem

Having dozens of specialized command rules (`citation`, `label_command`, `include_command`, etc.) creates large branching factors. Even when consolidated with regexes, each distinct argument shape adds states.

### Recommendation

Parse **all commands** using a single generic rule:

- `command_name`
- followed by `repeat(argument)`

Interpret semantic meaning (cite vs ref vs include) **after parsing**, using Tree-sitter queries or a second-pass AST walk.

### Effect

- Dramatically reduces parse-table size
- Removes most lookahead branching

---

## 3. Replace Rich Inline `text` with `text_chunk`

### Problem

A rule like:

```
repeat(choice(word, space, command, subscript, superscript, ...))
```

creates a combinatorial explosion of states.

### Recommendation

Use a **single regex token** for plain text:

- Stop at structural characters (`\\`, `{}`, `$`, `%`, etc.)
- Parse commands, groups, math, etc. *outside* text

Example concept:

```
text_chunk: /[^\\{}$%\[\]\n]+/
```

### Effect

- Collapses many ambiguous inline transitions
- Eliminates common grammar conflicts

---

## 4. Remove Recursive Use of `_root_content` Inside Math

### Problem

Allowing `repeat(_root_content)` inside math environments means the parser must consider **sections, paragraphs, and environments** inside `$...$`, exploding state space.

### Recommendation

Define a **restricted math item set**:

- commands
- groups
- text chunks
- math delimiters

Avoid section- or paragraph-level constructs in math.

### Effect

- Major reduction in cross-product states
- More realistic LaTeX modeling

---

## 5. Unify Environments Aggressively

### Problem

Each environment variant (minted, asy, sage, math, etc.) duplicates begin/end structure and introduces unique parse paths.

### Recommendation

- Use **one** generic `environment` rule
- Parse `\begin{<name>}` generically
- Delegate verbatim-like bodies to an external scanner based on name

Avoid environment-specific option parsing in the grammar.

### Effect

- Fewer branches
- Fewer environment-specific states

---

## 6. Minimize Typed Group Variants

### Problem

Rules like `curly_group_text`, `curly_group_label`, `curly_group_path`, etc. multiply grammar symbols without materially reducing ambiguity.

### Recommendation

Keep only:

- `curly_group`
- `brack_group`

Use `alias()` at usage sites if a distinct node name is required.

### Effect

- Fewer nonterminals
- Smaller goto tables

---

## 7. Avoid `repeat(choice(...))` for Large Choice Sets

### Problem

Patterns such as:

```
repeat(choice(a, b, c, d, e, f))
```

scale poorly in LR automata.

### Recommendation

- Narrow the choice set aggressively
- Or replace the entire construct with a single external token

Typical candidates:

- Implementation bodies
- Spec blocks
- Free-form option bodies

---

## 8. Eliminate Conflicts by Design

Conflicts increase parse tables because Tree-sitter must encode alternative actions.

Strategies:

- Ensure `text` does not include `command`
- Ensure paragraph breaks are handled at block level, not inline
- Prefer structural separation over precedence hacks

---

## 9. Size-First Grammar Design Principle

> **Parse structure, not semantics.**

The parser should answer:

- Where are commands?
- Where are groups?
- Where are environments?

It should *not* answer:

- What kind of command is this?
- How many arguments does it semantically expect?

Those belong in later passes.

---

## 10. Success Metric

After refactoring, run:

```
tree-sitter generate --stats
```

Track:

- Number of states (primary indicator)
- Number of actions

A meaningful binary size reduction almost always correlates with a significant state count drop.

---

**End of document.**

