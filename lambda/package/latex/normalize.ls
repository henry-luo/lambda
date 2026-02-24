// latex/normalize.ls — Phase 1: AST cleanup and normalization
// Cleans up the raw tree-sitter AST for easier processing.

import util: .lambda.package.latex.util
import sym: .lambda.package.latex.symbols

// ============================================================
// Main normalization entry point
// ============================================================

// normalize the raw AST from tree-sitter
pub fn normalize(ast) {
    if (ast == null) null
    else if (ast is string) normalize_text(ast)
    else if (ast is element) normalize_element(ast)
    else ast
}

// ============================================================
// Text normalization
// ============================================================

// collapse multiple whitespace, but preserve single newlines within paragraphs
fn normalize_text(text) {
    if (text == "\n") " "
    else text
}

// ============================================================
// Element normalization
// ============================================================

fn normalize_element(el) {
    let tag = name(el)
    match tag {
        case 'ligature':    resolve_ligature(el)
        case 'nbsp':        "\u00A0"
        case 'control_symbol': resolve_control_symbol(el)
        case 'controlspace_command': " "
        default:            normalize_children(el)
    }
}

// resolve a ligature element to its Unicode character
fn resolve_ligature(el) {
    let text = util.text_of(el)
    sym.resolve_ligature(text)
}

// resolve a control symbol (\%, \&, etc.) to its character
fn resolve_control_symbol(el) {
    let text = util.text_of(el)
    // strip leading backslash if present
    let cmd = if (starts_with(text, "\\")) slice(text, 1, len(text)) else text
    let special = sym.resolve_special(cmd)
    if (special != null) { special } else { cmd }
}

// ============================================================
// Children normalization
// ============================================================

// normalize all children of an element, keeping the element tag and attributes
fn normalize_children(el) {
    let n = len(el)
    if (n == 0) {
        el
    } else {
        let kids = (for (i in 0 to (n - 1),
                        let child = el[i],
                        let normed = normalize(child)
                        where normed != null)
                    normed)
        // rebuild element with normalized children
        rebuild_element(el, kids)
    }
}

// rebuild an element preserving its tag and attributes but replacing children
fn rebuild_element(el, new_children) {
    let tag = name(el)
    // for section-like tags, preserve the title attribute
    // for math tags, preserve source and ast attributes
    // for all others, rebuild with just children using match on known tags
    match tag {
        // section-like: preserve title
        case 'section' {
            let t = el.title
            if (t != null) { <section title: t; for c in new_children { c }> } else { <section; for c in new_children { c }> }
        }
        case 'subsection' {
            let t = el.title
            if (t != null) { <subsection title: t; for c in new_children { c }> } else { <subsection; for c in new_children { c }> }
        }
        case 'subsubsection' {
            let t = el.title
            if (t != null) { <subsubsection title: t; for c in new_children { c }> } else { <subsubsection; for c in new_children { c }> }
        }
        case 'paragraph' {
            let t = el.title
            if (t != null) { <paragraph title: t; for c in new_children { c }> } else { <paragraph; for c in new_children { c }> }
        }
        case 'subparagraph' {
            let t = el.title
            if (t != null) { <subparagraph title: t; for c in new_children { c }> } else { <subparagraph; for c in new_children { c }> }
        }
        // math: preserve source and ast
        case 'inline_math' {
            let s = el.source
            let a = el.ast
            if (a != null) { <inline_math source: s, ast: a; for c in new_children { c }> } else if (s != null) { <inline_math source: s; for c in new_children { c }> } else { <inline_math; for c in new_children { c }> }
        }
        case 'display_math' {
            let s = el.source
            let a = el.ast
            if (a != null) { <display_math source: s, ast: a; for c in new_children { c }> } else if (s != null) { <display_math source: s; for c in new_children { c }> } else { <display_math; for c in new_children { c }> }
        }
        // common text-containing elements: rebuild with just children
        case 'textbf':      <textbf; for c in new_children { c }>
        case 'textit':      <textit; for c in new_children { c }>
        case 'emph':        <emph; for c in new_children { c }>
        case 'underline':   <underline; for c in new_children { c }>
        case 'texttt':      <texttt; for c in new_children { c }>
        case 'textsf':      <textsf; for c in new_children { c }>
        case 'textsc':      <textsc; for c in new_children { c }>
        case 'textsl':      <textsl; for c in new_children { c }>
        case 'textrm':      <textrm; for c in new_children { c }>
        case 'curly_group':  <curly_group; for c in new_children { c }>
        case 'brack_group':  <brack_group; for c in new_children { c }>
        case 'group':        <group; for c in new_children { c }>
        case 'text_group':   <text_group; for c in new_children { c }>
        case 'item':         <item; for c in new_children { c }>
        case 'caption':      <caption; for c in new_children { c }>
        case 'footnote':     <footnote; for c in new_children { c }>
        case 'href':         <href; for c in new_children { c }>
        case 'quote':        <quote; for c in new_children { c }>
        case 'quotation':    <quotation; for c in new_children { c }>
        case 'verse':        <verse; for c in new_children { c }>
        case 'center':       <center; for c in new_children { c }>
        case 'abstract':     <abstract; for c in new_children { c }>
        case 'latex_document': <latex_document; for c in new_children { c }>
        case 'document':     <document; for c in new_children { c }>
        case 'itemize':      <itemize; for c in new_children { c }>
        case 'enumerate':    <enumerate; for c in new_children { c }>
        case 'description':  <description; for c in new_children { c }>
        case 'figure':       <figure; for c in new_children { c }>
        case 'table':        <table; for c in new_children { c }>
        case 'tabular':      <tabular; for c in new_children { c }>
        case 'minipage':     <minipage; for c in new_children { c }>
        case 'multicols':    <multicols; for c in new_children { c }>
        case 'flushleft':    <flushleft; for c in new_children { c }>
        case 'flushright':   <flushright; for c in new_children { c }>
        case 'verbatim':     <verbatim; for c in new_children { c }>
        case 'accent':       <accent; for c in new_children { c }>
        // unknown tags: return original element as-is
        default: el
    }
}
