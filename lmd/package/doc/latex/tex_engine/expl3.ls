// Script implementations of selected expl3 control-sequence and token-list APIs.
// Tokens keep their category codes and source identity; the TeX engine owns scope.
import reader: .reader
import stream: .stream
let CS_NEW = ["cs_new:Npn", "cs_new_protected:Npn"]
let CS_LOCAL = ["cs_set:Npn", "cs_set_protected:Npn"]
let CS_GLOBAL = ["cs_gset:Npn", "cs_gset_protected:Npn"]
let TL_NEW = ["tl_new:N", "tl_const:Nn"]
let TL_SET = ["tl_set:Nn", "tl_set:Nx", "tl_gset:Nn", "tl_gset:Nx", "tl_clear:N", "tl_gclear:N"]
let SEQ_NEW = ["seq_new:N"]
let SEQ_SET = ["seq_set_from_clist:Nn", "seq_gset_from_clist:Nn", "seq_clear:N", "seq_gclear:N",
    "seq_put_left:Nn", "seq_put_right:Nn", "seq_gput_left:Nn", "seq_gput_right:Nn"]
let SEQ_EXPAND = ["seq_use:Nn", "seq_count:N"]
let SEQ_SORT = ["seq_sort:Nn", "seq_gsort:Nn"]
let SORT_RETURN = ["sort_return_same:", "sort_return_swapped:"]
let STR_COMPARE = ["str_compare:nNnTF", "str_compare:nNnT", "str_compare:nNnF"]
let TEXT_CASE = ["text_uppercase:n", "text_lowercase:n"]

pub fn exists(name) => contains(CS_NEW, name) or contains(CS_LOCAL, name) or
    contains(CS_GLOBAL, name) or contains(TL_NEW, name) or contains(TL_SET, name) or
    contains(SEQ_NEW, name) or contains(SEQ_SET, name) or contains(SEQ_EXPAND, name) or
    contains(SEQ_SORT, name) or contains(SORT_RETURN, name) or contains(STR_COMPARE, name) or contains(TEXT_CASE, name) or
    contains(["ExplSyntaxOn", "ExplSyntaxOff", "ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile", "tl_use:N", "int_eval:n", "fp_eval:n"], name)
pub fn expandable(name) => contains(["tl_use:N", "int_eval:n", "fp_eval:n"], name) or contains(SEQ_EXPAND, name) or contains(STR_COMPARE, name) or contains(TEXT_CASE, name)
pub fn cs_definition(name) => contains(CS_NEW, name) or contains(CS_LOCAL, name) or contains(CS_GLOBAL, name)
pub fn fresh(name) => contains(CS_NEW, name) or contains(TL_NEW, name) or contains(SEQ_NEW, name)
pub fn global(name) => contains(CS_NEW, name) or contains(CS_GLOBAL, name) or contains(TL_NEW, name) or
    contains(SEQ_NEW, name) or contains(["tl_gset:Nn", "tl_gset:Nx", "tl_gclear:N",
        "seq_gset_from_clist:Nn", "seq_gclear:N", "seq_gput_left:Nn", "seq_gput_right:Nn", "seq_gsort:Nn"], name)
pub fn protected(name) => contains(["cs_new_protected:Npn", "cs_set_protected:Npn", "cs_gset_protected:Npn"], name)
pub fn token_assignment(name) => contains(TL_NEW, name) or contains(TL_SET, name)
pub fn sequence_sort(name) => contains(SEQ_SORT, name)
pub fn sort_return(name) => contains(SORT_RETURN, name)
pub fn string_compare(name) => contains(STR_COMPARE, name)
pub fn text_case(name) => contains(TEXT_CASE, name)
pub fn case_opaque(name) => contains(["ref", "pageref", "label", "cite", "nocite"], name)
pub fn sequence_assignment(name) => contains(SEQ_NEW, name) or contains(SEQ_SET, name)
pub fn sequence_empty_assignment(name) => contains(["seq_new:N", "seq_clear:N", "seq_gclear:N"], name)
pub fn sequence_from_clist(name) => contains(["seq_set_from_clist:Nn", "seq_gset_from_clist:Nn"], name)
pub fn sequence_put_left(name) => contains(["seq_put_left:Nn", "seq_gput_left:Nn"], name)
pub fn no_argument(name) => contains(["tl_new:N", "tl_clear:N", "tl_gclear:N"], name)
pub fn expanded_argument(name) => contains(["tl_set:Nx", "tl_gset:Nx"], name)
pub fn token_list(tokens) => {kind: "macro", count: 0, prefix: [], delimiters: [],
    brace_delim: false, long_ok: false, protected_flag: false, body: tokens}
pub fn save_codes(codes) => {[chr(9)]: reader.catcode(chr(9), codes),
    [chr(32)]: reader.catcode(chr(32), codes), [chr(34)]: reader.catcode(chr(34), codes),
    [chr(58)]: reader.catcode(chr(58), codes), [chr(94)]: reader.catcode(chr(94), codes),
    [chr(95)]: reader.catcode(chr(95), codes), [chr(124)]: reader.catcode(chr(124), codes),
    [chr(126)]: reader.catcode(chr(126), codes)}
pub fn syntax_codes(codes) => {*:codes, [chr(9)]: 9, [chr(32)]: 9, [chr(34)]: 12,
    [chr(58)]: 11, [chr(94)]: 7, [chr(95)]: 11, [chr(124)]: 12, [chr(126)]: 10}

fn space_token(token) => token.kind == "char" and token.cat == 10
fn trim_first(tokens, at) any^ => if (at < len(tokens) and space_token(tokens[at])) trim_first(tokens, at + 1) else at
fn trim_last(tokens, at, first) any^ => if (at > first and space_token(tokens[at - 1])) trim_last(tokens, at - 1, first) else at

// Comma-list grouping removes outer braces only after trimming top-level spaces.
fn clist_item(tokens) any^ {
    let first = trim_first(tokens, 0)^;
    let limit = trim_last(tokens, len(tokens), first)^;
    if (first == limit) null
    else stream.strip_outer(slice(tokens, first, limit)^)^
}

fn clist_split(tokens, index = 0, depth = 0, current = [], items = []) any^ {
    if (index >= len(tokens)) {
        let item = clist_item(current)^;
        if (item == null) items else [*items, item]
    }
    else {
        let token = tokens[index];
        if (token.kind == "char" and token.cat == 12 and token.value == "," and depth == 0) {
            let item = clist_item(current)^;
            clist_split(tokens, index + 1, depth, [], if (item == null) items else [*items, item])
        }
        else {
            let next = depth + if (token.kind == "char" and token.cat == 1) 1
                else if (token.kind == "char" and token.cat == 2) -1 else 0;
            clist_split(tokens, index + 1, next, [*current, token], items)
        }
    }
}

pub fn from_clist(tokens) any^ => clist_split(tokens)
