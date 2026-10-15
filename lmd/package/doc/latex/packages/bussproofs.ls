// Bounded bussproofs stack and inference rules. Proof layout remains in Lambda script.
import util: ~~.util
import math: lambda.doc.math.math

fn tree_commands(tree) => [for (paragraph in tree,
    entry in if (paragraph is element and
        string(name(paragraph)) == "paragraph") paragraph else []
    where entry is element) entry]

fn center_definition(raw) string^ {
    if (raw == null or not starts_with(raw, "\\mbox{") or
        not ends_with(raw, "}"))
        raise error("unsupported bussproofs center definition")
    else {
        let first = index_of(raw, "$")
        let remainder = if (first == null) ""
            else slice(raw, first + 1, len(raw))
        let relative_end = index_of(remainder, "$")
        if (first == null or relative_end == null)
            raise error("bussproofs center definition needs one math span")
        else {
            let end_index = first + 1 + relative_end
            let before = trim(replace(slice(raw, len("\\mbox{"), first), "\\ ", ""))
            let after = trim(replace(slice(raw, end_index + 1, len(raw) - 1),
                "\\ ", ""))
            if (before != "" or after != "" or
                index_of(slice(raw, end_index + 1, len(raw)), "$") != null)
                raise error("unsupported text in bussproofs center definition")
            else trim(slice(raw, first + 1, end_index))
        }
    }
}

fn zero_arg_macros(definitions) => [for (definition in definitions
    where definition.params == 0 and definition.body is element and
        len(definition.body) == 1 and definition.body[0] is element)
    {command: "\\" ++ definition.name,
     replacement: "\\" ++ string(name(definition.body[0]))}]

fn expand_formula(source, replacements, center, index) {
    if (index >= len(replacements)) {
        if (center == null) source
        else util.replace_command_token(source, center.command,
            center.replacement)
    } else expand_formula(util.replace_command_token(source,
        replacements[index].command, replacements[index].replacement),
        replacements, center, index + 1)
}

fn formula(raw, replacements, center, force_math = false) any^ {
    if (raw == null) raise error("bussproofs formula is missing")
    else {
        let source = trim(raw)
        let is_math = force_math or (starts_with(source, "$") and ends_with(source, "$"))
        let unwrapped = if (starts_with(source, "$") and ends_with(source, "$"))
            slice(source, 1, len(source) - 1) else source
        let expanded = expand_formula(unwrapped, replacements, center, 0)
        if (center == null and contains(expanded, "\\fCenter"))
            raise error("bussproofs center macro is undefined")
        else if (is_math) math.render_inline(parse(expanded, {type: "math"})^)
        else <span expanded>
    }
}

fn inference(stack, arity, raw, pending_label, force_math) any^ {
    if (len(stack) < arity) raise error("bussproofs inference has too few premises")
    else {
        let count = len(stack) - arity
        let premises = [for (i in count to (len(stack) - 1)) stack[i]]
        let prefix = if (count == 0) []
            else [for (i in 0 to (count - 1)) stack[i]];
        [*prefix,
            {premises: premises, raw: raw, label: pending_label,
             force_math: force_math}]
    }
}

fn parse_tree(commands, index, stack, pending_label, center) any^ {
    if (index >= len(commands)) {
        if (len(stack) != 1 or pending_label != null)
            raise error("bussproofs proof tree is incomplete")
        else {root: stack[0], center: center}
    } else {
        let command = commands[index]
        let tag = string(name(command))
        if (tag == "def") {
            let macro_name = util.raw_argument(command, "required", 0)
            let body = util.raw_argument(command, "required", 1)
            if (macro_name != null and body != null) {
                let replacement = center_definition(body)^
                parse_tree(commands, index + 1, stack, pending_label,
                    {command: macro_name, replacement: replacement})^
            } else if (index + 1 >= len(commands))
                raise error("bussproofs def has no command body")
            else {
                let definition = commands[index + 1]
                let replacement = center_definition(util.raw_argument(definition, "required", 0))^
                parse_tree(commands, index + 2, stack, pending_label,
                    {command: "\\" ++ string(name(definition)),
                     replacement: replacement})^
            }
        } else if (tag == "AxiomC") {
            let raw = util.raw_argument(command, "required", 0)
            if (raw == null) raise error("bussproofs AxiomC needs a formula")
            else parse_tree(commands, index + 1,
                [*stack, {premises: [], raw: raw, label: null,
                    force_math: false}], pending_label, center)^
        } else if (tag == "RightLabel") {
            let raw = util.raw_argument(command, "required", 0)
            if (raw == null or pending_label != null)
                raise error("bussproofs RightLabel needs one pending label")
            else parse_tree(commands, index + 1, stack, raw, center)^
        } else if (tag == "UnaryInfC" or tag == "BinaryInfC" or
                   tag == "BinaryInf") {
            let arity = if (tag == "UnaryInfC") 1 else 2
            let unbraced = tag == "BinaryInf"
            let raw = if (unbraced and index + 1 < len(commands) and
                string(name(commands[index + 1])) == "inline_math")
                commands[index + 1].source
                else util.raw_argument(command, "required", 0)
            if (raw == null) raise error("bussproofs inference needs a formula")
            else {
                let next = inference(stack, arity, raw, pending_label,
                    unbraced)^
                let advanced = if (unbraced) index + 2 else index + 1
                parse_tree(commands, advanced, next, null, center)^
            }
        } else if (tag == "newcommand" or tag == "comment")
            parse_tree(commands, index + 1, stack, pending_label, center)^
        else raise error("unsupported bussproofs command: " ++ tag)
    }
}

fn render_branch(branch, replacements, center) any^ {
    let premises = [for (premise in branch.premises)
        render_branch(premise, replacements, center)^]
    let conclusion = formula(branch.raw, replacements, center,
        branch.force_math)^
    let label = if (branch.label == null) null
        else formula(branch.label, replacements, center)^;
    <div class: "latex-proof-branch",
        if (len(premises) > 0)
            <div class: "latex-proof-premises",
                for (premise in premises) premise>;
        <div class: if (len(premises) == 0)
                "latex-proof-conclusion latex-proof-axiom"
                else "latex-proof-conclusion",
            conclusion
            if (label != null) <span class: "latex-proof-label", label>
        >
    >
}

pub fn render(tree, macro_definitions) any^ {
    let unknown = [for (child in tree where child is element and
        string(name(child)) != "paragraph" and
        string(name(child)) != "comment") child]
    let text = [for (paragraph in tree,
        fragment in if (paragraph is element and
            string(name(paragraph)) == "paragraph") paragraph else []
        where fragment is string and trim(fragment) != "") fragment]
    if (len(unknown) > 0 or len(text) > 0)
        raise error("unsupported content inside bussproofs tree")
    else {
        let commands = tree_commands(tree)
        let parsed = parse_tree(commands, 0, [], null, null)^
        let replacements = zero_arg_macros(macro_definitions)
        let graphic = render_branch(parsed.root, replacements, parsed.center)^;
        <div class: "latex-prooftree", graphic>
    }
}
