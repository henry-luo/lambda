// Script implementations of selected expl3 control-sequence and token-list APIs.
// Tokens keep their category codes and source identity; the TeX engine owns scope.
import reader: .reader
import stream: .stream
pub type TokenList = {kind: string, count: int, prefix: map[], delimiters: map[], brace_delim: bool, long_ok: bool, protected_flag: bool, body: map[]}
let CS_NEW = ["cs_new:Npn", "cs_new_protected:Npn"]
let CS_LOCAL = ["cs_set:Npn", "cs_set_protected:Npn"]
let CS_GLOBAL = ["cs_gset:Npn", "cs_gset_protected:Npn"]
let CS_VARIANT = ["cs_generate_variant:Nn"]
let IOR_ACTION = ["ior_open:Nn", "ior_close:N", "ior_get:NN", "ior_str_get:NN"]
let IOR_EOF = ["ior_if_eof:NTF", "ior_if_eof:NT", "ior_if_eof:NF", "ior_if_eof_p:N"]
let TL_NEW = ["tl_new:N", "tl_const:Nn"]
let TL_SET = ["tl_set:Nn", "tl_set:Nx", "tl_gset:Nn", "tl_gset:Nx", "tl_clear:N", "tl_gclear:N"]
let TL_PUT = ["tl_put_left:Nn", "tl_put_right:Nn", "tl_gput_left:Nn", "tl_gput_right:Nn",
    "tl_put_left:Nx", "tl_put_right:Nx", "tl_gput_left:Nx", "tl_gput_right:Nx"]
let TL_REPLACE = ["tl_replace_once:Nnn", "tl_greplace_once:Nnn", "tl_replace_all:Nnn", "tl_greplace_all:Nnn"]
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
    "seq_set_split:Nnn", "seq_gset_split:Nnn", "seq_set_split_keep_spaces:Nnn", "seq_gset_split_keep_spaces:Nnn",
    "seq_reverse:N", "seq_greverse:N",
    "seq_put_left:Nn", "seq_put_right:Nn", "seq_gput_left:Nn", "seq_gput_right:Nn"]
let SEQ_EXPAND = ["seq_use:Nn", "seq_count:N", "seq_item:Nn"]
let SEQ_SORT = ["seq_sort:Nn", "seq_gsort:Nn"]
let SORT_RETURN = ["sort_return_same:", "sort_return_swapped:"]
let STR_COMPARE = ["str_compare:nNnTF", "str_compare:nNnT", "str_compare:nNnF",
    "str_if_eq:nnTF", "str_if_eq:nnT", "str_if_eq:nnF", "str_if_eq_p:nn"]
let TOKEN_EXPANDABLE = ["token_if_expandable:NTF", "token_if_expandable:NT",
    "token_if_expandable:NF", "token_if_expandable_p:N"]
let TEXT_CASE = ["text_uppercase:n", "text_lowercase:n"]
let INT_MATH = ["int_abs:n", "int_sign:n", "int_max:nn", "int_min:nn",
    "int_div_truncate:nn", "int_div_round:nn", "int_mod:nn"]
let INT_COMPARE = ["int_compare:nNnTF", "int_compare:nNnT", "int_compare:nNnF"]
let INT_COMPARE_EXPR = ["int_compare:nTF", "int_compare:nT", "int_compare:nF"]
let INT_TEST = ["int_if_zero:nTF", "int_if_zero:nT", "int_if_zero:nF",
    "int_if_odd:nTF", "int_if_odd:nT", "int_if_odd:nF",
    "int_if_even:nTF", "int_if_even:nT", "int_if_even:nF"]

pub fn exists(name) bool => contains(SEQ_EMPTY, name) or contains(INT_TEST, name) or contains(INT_COMPARE_EXPR, name) or contains(INT_COMPARE, name) or contains(INT_MATH, name) or contains(CS_NEW, name) or contains(CS_LOCAL, name) or
    contains(CS_GLOBAL, name) or contains(CS_VARIANT, name) or contains(IOR_ACTION, name) or contains(IOR_EOF, name) or contains(TL_NEW, name) or contains(TL_SET, name) or contains(TL_PUT, name) or contains(TL_REPLACE, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name) or contains(TL_EQUAL, name) or contains(TL_STRING, name) or contains(TL_COUNT, name) or
    contains(SEQ_NEW, name) or contains(SEQ_SET, name) or contains(SEQ_EXPAND, name) or
    contains(SEQ_SORT, name) or contains(SORT_RETURN, name) or contains(STR_COMPARE, name) or contains(TOKEN_EXPANDABLE, name) or contains(TEXT_CASE, name) or
    contains(["ExplSyntaxOn", "ExplSyntaxOff", "ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile", "tl_use:N", "int_eval:n", "fp_eval:n", "cs_new_eq:NN"], name)
pub fn expandable(name) bool => contains(IOR_EOF, name) or contains(SEQ_EMPTY, name) or contains(INT_TEST, name) or contains(INT_COMPARE_EXPR, name) or contains(INT_COMPARE, name) or contains(INT_MATH, name) or contains(["tl_use:N", "int_eval:n", "fp_eval:n"], name) or contains(SEQ_EXPAND, name) or contains(STR_COMPARE, name) or contains(TOKEN_EXPANDABLE, name) or contains(TEXT_CASE, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name) or contains(TL_EQUAL, name) or contains(TL_STRING, name) or contains(TL_COUNT, name)
pub fn cs_definition(name) bool => contains(CS_NEW, name) or contains(CS_LOCAL, name) or contains(CS_GLOBAL, name)
pub fn cs_variant(name) bool => contains(CS_VARIANT, name)
pub fn ior_action(name) bool => contains(IOR_ACTION, name)
pub fn ior_eof(name) bool => contains(IOR_EOF, name)
pub fn fresh(name) bool => contains(CS_NEW, name) or contains(TL_NEW, name) or contains(SEQ_NEW, name)
pub fn global(name) bool => contains(CS_NEW, name) or contains(CS_GLOBAL, name) or contains(TL_NEW, name) or
    contains(SEQ_NEW, name) or contains(["tl_gset:Nn", "tl_gset:Nx", "tl_gclear:N", "tl_gput_left:Nn", "tl_gput_right:Nn",
        "tl_gput_left:Nx", "tl_gput_right:Nx",
        "tl_greplace_once:Nnn", "tl_greplace_all:Nnn", "seq_gset_from_clist:Nn", "seq_gset_split:Nnn", "seq_gset_split_keep_spaces:Nnn", "seq_gclear:N", "seq_gput_left:Nn", "seq_gput_right:Nn", "seq_gsort:Nn", "seq_greverse:N"], name)
pub fn protected(name) bool => contains(["cs_new_protected:Npn", "cs_set_protected:Npn", "cs_gset_protected:Npn"], name)
pub fn token_assignment(name) bool => contains(TL_NEW, name) or contains(TL_SET, name) or contains(TL_PUT, name)
pub fn token_replacement(name) bool => contains(TL_REPLACE, name)
pub fn token_put(name) bool => contains(TL_PUT, name)
pub fn token_empty(name) bool => contains(SEQ_EMPTY, name) or contains(TL_EMPTY, name) or contains(TL_BLANK, name)
pub fn int_test(name) bool => contains(INT_TEST, name)
pub fn int_compare_expr(name) bool => contains(INT_COMPARE_EXPR, name)
pub fn int_compare(name) bool => contains(INT_COMPARE, name)
pub fn int_math(name) bool => contains(INT_MATH, name)
pub fn token_count(name) bool => contains(TL_COUNT, name)
pub fn token_string(name) bool => contains(TL_STRING, name)
pub fn token_equal(name) bool => contains(TL_EQUAL, name)
pub fn token_blank(name) bool => contains(TL_BLANK, name)
pub fn token_put_left(name) bool => contains(["tl_put_left:Nn", "tl_gput_left:Nn", "tl_put_left:Nx", "tl_gput_left:Nx"], name)
pub fn sequence_sort(name) bool => contains(SEQ_SORT, name)
pub fn sort_return(name) bool => contains(SORT_RETURN, name)
pub fn string_compare(name) bool => contains(STR_COMPARE, name)
pub fn token_expandable(name) bool => contains(TOKEN_EXPANDABLE, name)
pub fn text_case(name) bool => contains(TEXT_CASE, name)
pub fn case_opaque(name) bool => contains(["ref", "pageref", "label", "cite", "nocite"], name)
pub fn sequence_assignment(name) bool => contains(SEQ_NEW, name) or contains(SEQ_SET, name)
pub fn sequence_reverse(name) bool => contains(["seq_reverse:N", "seq_greverse:N"], name)
pub fn sequence_empty_assignment(name) bool => contains(["seq_new:N", "seq_clear:N", "seq_gclear:N"], name)
pub fn sequence_from_clist(name) bool => contains(["seq_set_from_clist:Nn", "seq_gset_from_clist:Nn"], name)
pub fn sequence_split(name) bool => contains(["seq_set_split:Nnn", "seq_gset_split:Nnn",
    "seq_set_split_keep_spaces:Nnn", "seq_gset_split_keep_spaces:Nnn"], name)
pub fn sequence_put_left(name) bool => contains(["seq_put_left:Nn", "seq_gput_left:Nn"], name)
pub fn no_argument(name) bool => contains(["tl_new:N", "tl_clear:N", "tl_gclear:N"], name)
let TL_EXPANDED = ["tl_set:Nx", "tl_gset:Nx", "tl_put_left:Nx", "tl_put_right:Nx",
    "tl_gput_left:Nx", "tl_gput_right:Nx"]
pub fn expanded_argument(name) bool => contains(TL_EXPANDED, name)
pub fn expanded_argument_names() string[] => TL_EXPANDED
pub fn token_list(tokens: map[]) TokenList => {kind: "macro", count: 0, prefix: [], delimiters: [],
    brace_delim: false, long_ok: false, protected_flag: false, body: tokens}
pub fn save_codes(codes: map) map => {[chr(9)]: reader.catcode(chr(9), codes),
    [chr(32)]: reader.catcode(chr(32), codes), [chr(34)]: reader.catcode(chr(34), codes),
    [chr(58)]: reader.catcode(chr(58), codes), [chr(94)]: reader.catcode(chr(94), codes),
    [chr(95)]: reader.catcode(chr(95), codes), [chr(124)]: reader.catcode(chr(124), codes),
    [chr(126)]: reader.catcode(chr(126), codes)}
pub fn syntax_codes(codes: map) map => {*:codes, [chr(9)]: 9, [chr(32)]: 9, [chr(34)]: 12,
    [chr(58)]: 11, [chr(94)]: 7, [chr(95)]: 11, [chr(124)]: 12, [chr(126)]: 10}

fn space_token(token) => token.kind == "char" and token.cat == 10
fn trim_first(tokens: map[], at: int) int => if (at < len(tokens) and space_token(tokens[at])) trim_first(tokens, at + 1) else at
fn trim_last(tokens: map[], at: int, first: int) int => if (at > first and space_token(tokens[at - 1])) trim_last(tokens, at - 1, first) else at

// Both comma lists and split sequences remove one complete outer group.
fn split_item(tokens: map[], trim_spaces: bool, empty: bool, strip_groups: bool) map[] | null | error {
    let first = if (trim_spaces) trim_first(tokens, 0) else 0;
    let limit = if (trim_spaces) trim_last(tokens, len(tokens), first) else len(tokens);
    if (first == limit and not empty) null
    else if (strip_groups) stream.strip_outer(slice(tokens, first, limit))^
    else slice(tokens, first, limit)
}

fn split_tokens(tokens: map[], delimiter: map[], trim_spaces: bool, empty: bool, strip_groups: bool, once: bool,
        index: int = 0, depth: int = 0, current: map[] = [], items: array = []) array^ {
    if (index >= len(tokens)) {
        let item = split_item(current, trim_spaces, empty, strip_groups)^;
        if (item == null) items else [*items, item]
    }
    else {
        let token = tokens[index];
        let matched = depth == 0 and (not once or len(items) == 0) and index + len(delimiter) <= len(tokens) and
            stream.same_tokens(slice(tokens, index, index + len(delimiter)), delimiter)^;
        if (matched) {
            let item = split_item(current, trim_spaces, empty, strip_groups)^;
            split_tokens(tokens, delimiter, trim_spaces, empty, strip_groups, once, index + len(delimiter), depth, [],
                if (item == null) items else [*items, item])
        }
        else {
            let next = depth + if (token.kind == "char" and token.cat == 1) 1
                else if (token.kind == "char" and token.cat == 2) -1 else 0;
            split_tokens(tokens, delimiter, trim_spaces, empty, strip_groups, once, index + 1, next, [*current, token], items)
        }
    }
}

// An empty delimiter maps each top-level token or group, skipping spaces.
fn split_individual(input, items = []) array^ {
    let next = stream.next(input)^;
    if (next.token == null) items
    else if (space_token(next.token)) split_individual(next.stream, items)
    else {
        let argument = stream.argument(stream.push(next.stream, [next.token]), true)^;
        if (argument.error != null) raise error(argument.error)
        else split_individual(argument.stream, [*items, argument.tokens])
    }
}

pub fn from_clist(tokens: map[]) array^ =>
    split_tokens(tokens, [{kind:"char", value:",", cat:12}], true, false, true, false)
pub fn from_split(tokens: map[], delimiter: map[], trim_spaces: bool) array^ =>
    if (len(delimiter) == 0) split_individual(stream.push(stream.from_reader(reader.reader("")), tokens))
    else split_tokens(tokens, delimiter, trim_spaces, true, true, false)
pub fn replace_tokens(tokens: map[], search: map[], replacement: map[], once: bool) array^ =>
    stream.join_tokens(split_tokens(tokens, search, false, true, false, once)^, replacement)

// Build the same expandable argument adapter used by expl3's generated variants.
fn variant_argument(mode, position) any^ {
    let arg = "#" ++ string(position);
    if (mode == "N") "\\exp_not:N" ++ arg
    else if (mode == "n") "{\\exp_not:n{" ++ arg ++ "}}"
    else if (mode == "c") "\\exp_after:wN\\exp_not:N\\cs:w" ++ arg ++ "\\cs_end:"
    else if (mode == "V") "{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\__exp_eval_register:N" ++ arg ++ "}}"
    else if (mode == "v") "{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\__exp_eval_register:c{" ++ arg ++ "}}}"
    else if (mode == "o") "{\\__kernel_exp_not:w\\exp_after:wN{" ++ arg ++ "}}"
    else if (mode == "e") "{\\__kernel_exp_not:w\\tex_expanded:D{{" ++ arg ++ "}}}"
    else null
}

pub fn variant_plan(base, requested) any^ {
    let colon = index_of(base, ":");
    let tail = if (colon == null) "" else slice(base, colon + 1, len(base))^;
    let conditional = if (ends_with(tail, "TF")) "TF"
        else if (ends_with(tail, "T")) "T" else if (ends_with(tail, "F")) "F" else "";
    let signature = split(slice(tail, 0, len(tail) - len(conditional))^, "")^;
    let changes = split(requested, "")^;
    if (colon == null or len(signature) == 0 or len(changes) == 0 or len(changes) > len(signature) or
            len(signature) > 8 or not all([for (mode in signature) contains(["N", "n"], mode)])) null
    else {
        let modes = [for (i, mode in signature) if (i < len(changes)) changes[i] else mode];
        let legal = all([for (i, mode in modes)
            if (signature[i] == "N") contains(["N", "c"], mode)
            else contains(["n", "V", "v", "o", "e"], mode)]);
        let differences = [for (i, mode in modes where mode != signature[i]) i];
        if (not legal) null
        else if (len(differences) == 0) {unchanged:true}
        else {
            let used = slice(modes, 0, differences[len(differences) - 1] + 1)^;
            let helper = "exp_args:N" ++ join(used, "")^;
            let params = join([for (i in 1 to len(used) + 1) "#" ++ string(i)], "")^;
            let body = "\\exp_not:N#1" ++ join([for (i, mode in used) variant_argument(mode, i + 2)^], "")^;
            let value = contains(used, "V") or contains(used, "v");
            let once = contains(used, "o");
            {unchanged:false, name:slice(base, 0, colon + 1)^ ++ join(modes, "")^ ++ conditional,
                helper:helper, source:"\\" ++ helper ++ params ++ "{\\tex_expanded:D{" ++ body ++ "}}",
                guard:if (value and once) "exp_args_value_once" else if (value) "exp_args_value"
                    else if (once) "exp_args_once" else null}
        }
    }
}
