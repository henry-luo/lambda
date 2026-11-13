// Script implementations of selected expl3 control-sequence and token-list APIs.
// Tokens keep their category codes and source identity; the TeX engine owns scope.
import reader: .reader
import stream: .stream
let CS_NEW = ["cs_new:Npn", "cs_new_protected:Npn"]
let CS_LOCAL = ["cs_set:Npn", "cs_set_protected:Npn"]
let CS_GLOBAL = ["cs_gset:Npn", "cs_gset_protected:Npn"]
let TL_NEW = ["tl_new:N", "tl_const:Nn"]
let TL_SET = ["tl_set:Nn", "tl_set:Nx", "tl_gset:Nn", "tl_gset:Nx", "tl_clear:N", "tl_gclear:N"]
let TL_PUT = ["tl_put_left:Nn", "tl_put_right:Nn", "tl_gput_left:Nn", "tl_gput_right:Nn",
    "tl_put_left:Nx", "tl_put_right:Nx", "tl_gput_left:Nx", "tl_gput_right:Nx"]
let TL_EMPTY = ["tl_if_empty:NTF", "tl_if_empty:NT", "tl_if_empty:NF",
    "tl_if_empty:nTF", "tl_if_empty:nT", "tl_if_empty:nF"]
let TL_COUNT = ["tl_count:n", "tl_count:N"]
let TL_STRING = ["tl_to_str:n", "tl_to_str:V"]
let TL_EQUAL = ["tl_if_eq:NNTF", "tl_if_eq:NNT", "tl_if_eq:NNF",
    "tl_if_eq:nnTF", "tl_if_eq:nnT", "tl_if_eq:nnF"]
let TL_BLANK = ["tl_if_blank:nTF", "tl_if_blank:nT", "tl_if_blank:nF",
    "tl_if_blank:VTF", "tl_if_blank:VT", "tl_if_blank:VF"]
let SEQ_EMPTY = ["seq_if_empty:NTF", "seq_if_empty:NT", "seq_if_empty:NF"]
let SEQ_NEW = ["seq_new:N"]
let SEQ_SET = ["seq_set_from_clist:Nn", "seq_gset_from_clist:Nn", "seq_clear:N", "seq_gclear:N",
    "seq_reverse:N", "seq_greverse:N",
    "seq_put_left:Nn", "seq_put_right:Nn", "seq_gput_left:Nn", "seq_gput_right:Nn"]
let SEQ_EXPAND = ["seq_use:Nn", "seq_count:N", "seq_item:Nn"]
let SEQ_SORT = ["seq_sort:Nn", "seq_gsort:Nn"]
let SORT_RETURN = ["sort_return_same:", "sort_return_swapped:"]
let STR_COMPARE = ["str_compare:nNnTF", "str_compare:nNnT", "str_compare:nNnF"]
let TEXT_CASE = ["text_uppercase:n", "text_lowercase:n"]
let INT_MATH = ["int_abs:n", "int_sign:n", "int_max:nn", "int_min:nn",
    "int_div_truncate:nn", "int_div_round:nn", "int_mod:nn"]
let INT_COMPARE = ["int_compare:nNnTF", "int_compare:nNnT", "int_compare:nNnF"]
let INT_COMPARE_EXPR = ["int_compare:nTF", "int_compare:nT", "int_compare:nF"]
let INT_TEST = ["int_if_zero:nTF", "int_if_zero:nT", "int_if_zero:nF",
    "int_if_odd:nTF", "int_if_odd:nT", "int_if_odd:nF",
    "int_if_even:nTF", "int_if_even:nT", "int_if_even:nF"]

pub fn exists(name) => contains(SEQ_EMPTY, name) or contains(INT_TEST, name) or contains(INT_COMPARE_EXPR, name) or contains(INT_COMPARE, name) or contains(INT_MATH, name) or contains(CS_NEW, name) or contains(CS_LOCAL, name) or
    contains(CS_GLOBAL, name) or contains(TL_NEW, name) or contains(TL_SET, name) or contains(TL_PUT, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name) or contains(TL_EQUAL, name) or contains(TL_STRING, name) or contains(TL_COUNT, name) or
    contains(SEQ_NEW, name) or contains(SEQ_SET, name) or contains(SEQ_EXPAND, name) or
    contains(SEQ_SORT, name) or contains(SORT_RETURN, name) or contains(STR_COMPARE, name) or contains(TEXT_CASE, name) or
    contains(["ExplSyntaxOn", "ExplSyntaxOff", "ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile", "tl_use:N", "int_eval:n", "fp_eval:n", "cs_new_eq:NN"], name)
pub fn expandable(name) => contains(SEQ_EMPTY, name) or contains(INT_TEST, name) or contains(INT_COMPARE_EXPR, name) or contains(INT_COMPARE, name) or contains(INT_MATH, name) or contains(["tl_use:N", "int_eval:n", "fp_eval:n"], name) or contains(SEQ_EXPAND, name) or contains(STR_COMPARE, name) or contains(TEXT_CASE, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name) or contains(TL_EQUAL, name) or contains(TL_STRING, name) or contains(TL_COUNT, name)
pub fn cs_definition(name) => contains(CS_NEW, name) or contains(CS_LOCAL, name) or contains(CS_GLOBAL, name)
pub fn fresh(name) => contains(CS_NEW, name) or contains(TL_NEW, name) or contains(SEQ_NEW, name)
pub fn global(name) => contains(CS_NEW, name) or contains(CS_GLOBAL, name) or contains(TL_NEW, name) or
    contains(SEQ_NEW, name) or contains(["tl_gset:Nn", "tl_gset:Nx", "tl_gclear:N", "tl_gput_left:Nn", "tl_gput_right:Nn",
        "tl_gput_left:Nx", "tl_gput_right:Nx",
        "seq_gset_from_clist:Nn", "seq_gclear:N", "seq_gput_left:Nn", "seq_gput_right:Nn", "seq_gsort:Nn", "seq_greverse:N"], name)
pub fn protected(name) => contains(["cs_new_protected:Npn", "cs_set_protected:Npn", "cs_gset_protected:Npn"], name)
pub fn token_assignment(name) => contains(TL_NEW, name) or contains(TL_SET, name) or contains(TL_PUT, name)
pub fn token_put(name) => contains(TL_PUT, name)
pub fn token_empty(name) => contains(SEQ_EMPTY, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name)
pub fn int_test(name) => contains(INT_TEST, name)
pub fn int_compare_expr(name) => contains(INT_COMPARE_EXPR, name)
pub fn int_compare(name) => contains(INT_COMPARE, name)
pub fn int_math(name) => contains(INT_MATH, name)
pub fn token_count(name) => contains(TL_COUNT, name)
pub fn token_string(name) => contains(TL_STRING, name)
pub fn token_equal(name) => contains(TL_EQUAL, name)
pub fn token_blank(name) => contains(TL_BLANK, name)
pub fn token_put_left(name) => contains(["tl_put_left:Nn", "tl_gput_left:Nn", "tl_put_left:Nx", "tl_gput_left:Nx"], name)
pub fn sequence_sort(name) => contains(SEQ_SORT, name)
pub fn sort_return(name) => contains(SORT_RETURN, name)
pub fn string_compare(name) => contains(STR_COMPARE, name)
pub fn text_case(name) => contains(TEXT_CASE, name)
pub fn case_opaque(name) => contains(["ref", "pageref", "label", "cite", "nocite"], name)
pub fn sequence_assignment(name) => contains(SEQ_NEW, name) or contains(SEQ_SET, name)
pub fn sequence_reverse(name) => contains(["seq_reverse:N", "seq_greverse:N"], name)
pub fn sequence_empty_assignment(name) => contains(["seq_new:N", "seq_clear:N", "seq_gclear:N"], name)
pub fn sequence_from_clist(name) => contains(["seq_set_from_clist:Nn", "seq_gset_from_clist:Nn"], name)
pub fn sequence_put_left(name) => contains(["seq_put_left:Nn", "seq_gput_left:Nn"], name)
pub fn no_argument(name) => contains(["tl_new:N", "tl_clear:N", "tl_gclear:N"], name)
pub fn expanded_argument(name) => contains(["tl_set:Nx", "tl_gset:Nx", "tl_put_left:Nx", "tl_put_right:Nx",
    "tl_gput_left:Nx", "tl_gput_right:Nx"], name)
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
