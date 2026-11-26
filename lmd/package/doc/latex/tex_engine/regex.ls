// Bounded expl3 regex matching over ASCII TeX character tokens.
fn character(token) => token != null and token.kind == "char" and (len(token.value) ^ {0}) == 1 and (ord(token.value) ^ {128}) < 128
fn pattern_space(token) => character(token) and token.cat == 10
fn atom(kind, value = null) => {kind:kind,value:value,minimum:1,repeat:false}

// Regex escapes name character classes or quote one ASCII punctuation character.
fn escaped(token) any^ {
    if (token.kind != "control") null
    else if (contains(["d","D","w","W","s","S"],token.value)) atom(token.value)
    else if ((len(token.value) ^ {0}) == 1 and (ord(token.value) ^ {128}) < 128 and
        not ((token.value >= "a" and token.value <= "z") or (token.value >= "A" and token.value <= "Z")))
        atom("literal",token.value)
    else null
}
fn class_item(token) any^ => if (token.kind == "control") escaped(token)
    else if (character(token) and not contains(["[", "]"], token.value)) atom("literal",token.value)
    else null

// TeX control words in patterns are parsed as a backslash escape followed by their remaining characters.
fn pattern_tokens(token) any^ {
    let value = token.value;
    if (token.kind != "control" or len(value)^ <= 1 or
            not contains(["A","Z","d","D","w","W","s","S"],slice(value,0,1))) [token]
    else [{*:token,value:slice(value,0,1)},
        *[for (i in 1 to len(value)^ - 1) {kind:"char",value:slice(value,i,i + 1),cat:12}]]
}

// Ranges and escaped classes share the same character predicate as ordinary atoms.
fn class_items(tokens, index, negate = false, first = true, entries = []) any^ {
    if (index >= len(tokens)) {issue:"pattern"}
    else {
        let token = tokens[index];
        if (pattern_space(token)) class_items(tokens,index + 1,negate,first,entries)
        else if (first and character(token) and token.value == "^") class_items(tokens,index + 1,true,false,entries)
        else if (character(token) and token.value == "]")
            if (len(entries) == 0) {issue:"pattern"} else {issue:null,index:index + 1,atom:{*:atom("class"),entries:entries,negate:negate}}
        else {
            let item = class_item(token)^;
            let dash = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let end_token = if (index + 2 < len(tokens)) tokens[index + 2] else null;
            let is_range = item != null and item.kind == "literal" and character(dash) and dash.value == "-" and
                end_token != null and not (character(end_token) and end_token.value == "]");
            let end = if (is_range) class_item(end_token)^ else null;
            if (item == null or (is_range and (end == null or end.kind != "literal" or ord(item.value) > ord(end.value))))
                {issue:"pattern"}
            else class_items(tokens,index + if (is_range) 3 else 1,negate,false,
                [*entries,if (is_range) {kind:"range",low:ord(item.value),high:ord(end.value)} else item])
        }
    }
}

fn compile(tokens, index = 0, atoms = []) any^ {
    if (index >= len(tokens)) {issue:null,atoms:atoms}
    else {
        let t = tokens[index];
        if (pattern_space(t)) compile(tokens,index + 1,atoms)
        else if (character(t) and contains(["*","+","?"],t.value)) {
            let prior = if (len(atoms) == 0) null else atoms[len(atoms) - 1];
            if (prior == null or prior.kind == "start" or prior.kind == "end" or prior.quantified == true) {issue:"pattern"}
            else compile(tokens,index + 1,[*slice(atoms,0,len(atoms)-1),
                {*:prior,minimum:if (t.value == "+") 1 else 0,repeat:t.value != "?",quantified:true}])
        }
        else if (character(t) and t.value == "[") {
            let parsed = class_items(tokens,index + 1)^;
            if (parsed.issue != null) parsed else compile(tokens,parsed.index,[*atoms,parsed.atom])
        }
        else {
            let parsed = if (t.kind == "control" and t.value == "A") atom("start")
                else if (t.kind == "control" and t.value == "Z") atom("end")
                else if (t.kind == "control") escaped(t)^
                else if (character(t) and t.value == "^") atom("start")
                else if (character(t) and t.value == "$") atom("end")
                else if (character(t) and t.value == ".") atom("dot")
                else if (character(t) and not contains(["(",")","|","{","}","]"],t.value)) atom("literal",t.value)
                else null;
            if (parsed == null) {issue:"pattern"} else compile(tokens,index + 1,[*atoms,parsed])
        }
    }
}

fn word(code) => (code >= 48 and code <= 57) or (code >= 65 and code <= 90) or
    (code >= 97 and code <= 122) or code == 95
fn blank(code) => code == 32 or code == 9 or code == 10 or code == 12 or code == 13
fn accepts(a, token) any^ {
    let code = if (token.kind == "control") -1 else ord(token.value);
    if (a.kind == "literal") code >= 0 and token.value == a.value
    else if (a.kind == "range") code >= a.low and code <= a.high
    else if (a.kind == "dot") true
    else if (a.kind == "d" or a.kind == "D") (code >= 48 and code <= 57) == (a.kind == "d")
    else if (a.kind == "w" or a.kind == "W") word(code) == (a.kind == "w")
    else if (a.kind == "s" or a.kind == "S") blank(code) == (a.kind == "s")
    else if (a.kind == "class") any([for (entry in a.entries) accepts(entry,token)^]) != a.negate
    else false
}

// Epsilon closure only moves forward, so empty matches cannot loop.
fn closure(atoms, pending, at, size, seen = []) any^ {
    if (len(pending) == 0) seen
    else {
        let index = pending[0];
        let rest = slice(pending,1,len(pending));
        if (contains(seen,index)) closure(atoms,rest,at,size,seen)
        else if (index == len(atoms)) closure(atoms,rest,at,size,[*seen,index])
        else {
            let a = atoms[index];
            let skip = if (a.kind == "start") at == 0 else if (a.kind == "end") at == size else a.minimum == 0;
            closure(atoms,[*rest,*(if (skip) [index + 1] else [])],at,size,[*seen,index])
        }
    }
}
fn run(atoms, subject, at = 0, states = []) any^ {
    let ready = closure(atoms,[*states,0],at,len(subject))^;
    if (contains(ready,len(atoms))) true
    else if (at >= len(subject)) false
    else {
        let next = [for (index in ready where index < len(atoms) and accepts(atoms[index],subject[at])^)
            *(if (atoms[index].repeat) [index,index + 1] else [index + 1])];
        run(atoms,subject,at + 1,next)
    }
}

pub fn evaluate(pattern: map[], subject: map[], budget: int) any^ {
    if ((len(pattern) + 1) * (len(subject) + 1) > budget) {issue:"budget"}
    else if (any([for (t in subject) not ((character(t) and t.cat != 13) or t.kind == "control")])) {issue:"subject"}
    else {
        let normalized = [for (token in pattern where not pattern_space(token)^) *pattern_tokens(token)^];
        let compiled = compile(normalized)^;
        if (compiled.issue != null) compiled else {issue:null,matched:run(compiled.atoms,subject)^}
    }
}
