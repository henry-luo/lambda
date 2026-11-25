#!/usr/bin/env python3
"""Generate the script TeX engine's message data from the bundled expl3 format.

The native format decides which conditional declarations are present.  The
generated module contains data only; message expansion remains in the script
engine.  Temporary query files stay under the checkout's temp directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "lmd/package/doc/latex/tex/expl3-code.tex"
DESTINATION = ROOT / "lmd/package/doc/latex/tex_engine/message_catalog.ls"
WORK = ROOT / "temp/latex-message-catalog"
MACRO_DEFINITION = re.compile(
    r"\\cs_(?:new|set|gset)(?:_protected)?:Npn\s*\\(?P<name>[A-Za-z_:]+)"
    r"(?P<params>(?:\s*#[1-9])*)\s*\{"
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def snapshot_engine(engine):
    WORK.mkdir(parents=True, exist_ok=True)
    binary = engine.read_bytes()
    frozen = WORK / "lambda.exe"
    shutil.copy2(engine, frozen)
    if digest(frozen.read_bytes()) != digest(binary) or digest(engine.read_bytes()) != digest(binary):
        raise RuntimeError("the native executable changed while making the query snapshot")
    return digest(binary)


class TeXScanner:
    """Batch the existing Script reader's command and group scans."""

    def __init__(self, source, home):
        self.source = source
        self.home = home
        self.groups = {}
        self.openings = {}
        pattern = re.compile(r"\\(?:__kernel_)?msg_new:nnn(?:n)?|\\use:e|"
                             r"\\clist_map_inline:nn|\\prop_gput:Nnn")
        matches = list(pattern.finditer(source))
        self.commands = self.valid_positions(matches)

    def valid_positions(self, matches):
        jobs = []
        for match in matches:
            start = self.source.rfind("\n", 0, match.start()) + 1
            end = self.source.find("\n", match.start())
            if end < 0:
                end = len(self.source)
            name = re.match(r"\\[A-Za-z_:]+", self.source[match.start():]).group()[1:]
            jobs.append({"line": self.source[start:end],
                         "offset": len(self.source[start:match.start()].encode("utf-8")),
                         "name": name})
        valid = self.scan("commands", jobs)
        return {match.start() for match, accepted in zip(matches, valid) if accepted}

    def scan(self, mode, items):
        if not items:
            return []
        (WORK / "scan.json").write_text(json.dumps({"mode": mode, "items": items}, ensure_ascii=False))
        env = os.environ.copy()
        env["LAMBDA_HOME"] = str(self.home)
        env["LAMBDA_EXEC_BACKEND"] = "jit"
        process = subprocess.run([str(WORK / "lambda.exe"), "--no-log", "utils/latex_message_scan.ls"],
                                 cwd=ROOT, env=env, capture_output=True, text=True)
        if process.returncode:
            raise RuntimeError("Script TeX source scan failed:\n" + process.stderr[-4000:])
        output = process.stdout.strip()
        if not output.startswith('"') or not output.endswith('"'):
            raise ValueError("Script TeX source scan returned an invalid result")
        result = json.loads(output[1:-1])
        if len(result) != len(items):
            raise ValueError("Script TeX source scan returned the wrong item count")
        return result

    def opening(self, offset, required=True):
        start = self.openings[offset]
        if start is not None:
            return start
        if required:
            raise ValueError(f"expected a TeX group at offset {offset}")
        return None

    def prefetch_openings(self, offsets):
        missing = sorted({offset for offset in offsets if offset not in self.openings})
        for offset, start in zip(missing, self.scan("openings", missing)):
            self.openings[offset] = start

    def prefetch(self, starts):
        missing = sorted({start for start in starts if start is not None and start not in self.groups})
        for start, found in zip(missing, self.scan("groups", missing)):
            if not found["closed"]:
                raise ValueError(f"unterminated TeX group at offset {start}")
            self.groups[start] = found["end"]

    def content(self, start):
        return self.source[start + 1:self.groups[start] - 1]


def declarations(source, scanner):
    pattern = re.compile(
        r"\\(?P<kernel>__kernel_)?msg_new:nnn(?P<four>n)?\s*"
        r"\{\s*(?P<module>[^{}#]+?)\s*\}\s*\{\s*(?P<name>[^{}#]+?)\s*\}"
    )
    dynamic = re.compile(r"\\(?P<kernel>__kernel_)?msg_new:nnn(?P<four>n)?\s*"
                         r"\{\s*(?P<module>[^{}#]+?)\s*\}\s*\{(?P<name>[^{}]*#[^{}]*)\}")
    dynamic_matches = [match for match in dynamic.finditer(source) if match.start() in scanner.commands]
    matches = [match for match in pattern.finditer(source) if match.start() in scanner.commands]
    uses = [match for match in re.finditer(r"\\use:e\s*\{", source) if match.start() in scanner.commands]
    scanner.prefetch_openings([match.end() for match in matches])
    body_starts = [scanner.opening(match.end()) for match in matches]
    use_starts = [source.find("{", match.start(), match.end()) for match in uses]
    scanner.prefetch(body_starts + use_starts)
    scanner.prefetch_openings([scanner.groups[start] for match, start in zip(matches, body_starts) if match["four"]])
    extra_starts = [scanner.opening(scanner.groups[start]) if match["four"] else None
                    for match, start in zip(matches, body_starts)]
    scanner.prefetch(extra_starts)
    expanded = [(start, scanner.groups[start]) for start in use_starts]
    records = []
    for match, start, extra_start in zip(matches, body_starts, extra_starts):
        body = scanner.content(start)
        extra = scanner.content(extra_start) if extra_start is not None else ""
        module = "".join(match["module"].split())
        if match["kernel"]:
            module = "LaTeX/" + module
        records.append({"module": module, "name": "".join(match["name"].split()),
                        "body": body, "extra": extra,
                        "preexpand": any(begin < match.start() < end for begin, end in expanded)})
    keys = [(entry["module"], entry["name"]) for entry in records]
    if not records or len(keys) != len(set(keys)):
        raise ValueError("message declarations are empty or have duplicate names")
    audit_message_commands(source, scanner, matches, dynamic_matches)
    return records, dynamic_matches


def containing_macro(source, scanner, definitions, offset):
    before = [match for match in definitions if match.start() < offset]
    while before:
        batch, before = before[-32:], before[:-32]
        valid = scanner.valid_positions(batch)
        candidates = [match for match in batch if match.start() in valid]
        starts = [source.find("{", match.start(), match.end()) for match in candidates]
        scanner.prefetch(starts)
        enclosing = [(match, start) for match, start in zip(candidates, starts)
                     if start < offset < scanner.groups[start]]
        if enclosing:
            return enclosing[-1]
    raise ValueError(f"no containing macro for message at offset {offset}")


def audit_message_commands(source, scanner, static, dynamic):
    recognized = {match.start() for match in static + dynamic}
    commands = [match for match in re.finditer(r"\\(?:__kernel_)?msg_new:nnn(?:n)?", source)
                if match.start() in scanner.commands and match.start() not in recognized]
    definitions = list(MACRO_DEFINITION.finditer(source))
    target = re.compile(r"\\(?:cs_(?:new|set|gset)(?:_protected)?:Npn|cs_generate_variant:Nn)\s*$")
    for command in commands:
        line_start = source.rfind("\n", 0, command.start()) + 1
        if target.search(source[line_start:command.start()]):
            continue
        containing_macro(source, scanner, definitions, command.start())
        offset = command.end()
        arguments = []
        for _ in range(4 if command.group().endswith("nnnn") else 3):
            scanner.prefetch_openings([offset])
            start = scanner.opening(offset, required=False)
            if start is None:
                break
            scanner.prefetch([start])
            arguments.append(scanner.content(start).strip())
            offset = scanner.groups[start]
        # A partially applied wrapper or a pure parameter-forwarding wrapper
        # does not itself register a statically identifiable message.
        partial = len(arguments) < 2 and arguments and "#" in arguments[0]
        forwarding = (len(arguments) >= 3 and
                      all(re.fullmatch(r"#[1-9]", arg) for arg in arguments[:3]) and
                      all(not arg or re.fullmatch(r"#[1-9]", arg) for arg in arguments[3:]))
        if not partial and not forwarding:
            raise ValueError(f"unsupported message declaration syntax at offset {command.start()}")


def properties(source, scanner):
    direct = re.compile(
        r"\\prop_gput:Nnn\s*\\g_msg_module_(name|type)_prop\s*"
        r"\{\s*([^{}#]+?)\s*\}\s*\{\s*([^{}#]*?)\s*\}"
    )
    entries = {"name": [], "type": []}
    for match in direct.finditer(source):
        if match.start() in scanner.commands:
            entries[match[1]].append((match.start(), 0, "".join(match[2].split()), match[3]))
    inner = re.compile(
        r"\\prop_gput:Nnn\s*\\g_msg_module_(name|type)_prop\s*"
        r"\{\s*#1\s*\}\s*\{\s*([^{}#]*?)\s*\}"
    )
    clists = [match for match in re.finditer(r"\\clist_map_inline:nn\s*\{", source)
              if match.start() in scanner.commands]
    starts = [source.find("{", match.start(), match.end()) for match in clists]
    scanner.prefetch(starts)
    scanner.prefetch_openings([scanner.groups[start] for start in starts])
    bodies = [scanner.opening(scanner.groups[start], required=False) for start in starts]
    scanner.prefetch(bodies)
    selected = []
    for match, start, body_start in zip(clists, starts, bodies):
        if body_start is None:
            continue
        body = scanner.content(body_start)
        assignments = [item for item in inner.finditer(body)
                       if body_start + 1 + item.start() in scanner.commands]
        if assignments:
            selected.append((match, start, assignments))
    lists = scanner.scan("lists", [scanner.content(start) for _, start, _ in selected])
    for (match, _, assignments), items in zip(selected, lists):
        for assignment in assignments:
            for index, name in enumerate(items.split(",")):
                if name:
                    entries[assignment[1]].append((match.start(), index, name, assignment[2]))
    result = {}
    for kind, values in entries.items():
        ordered = sorted(values)
        names = [item[2] for item in ordered]
        if not names or len(names) != len(set(names)):
            raise ValueError(f"message {kind} module registrations are empty or repeated")
        result[kind] = [{"name": name, "value": value} for _, _, name, value in ordered]
    return result


def dynamic_program(source, scanner, declarations):
    """Replay direct message templates using their original macro calls."""
    definitions = list(MACRO_DEFINITION.finditer(source))
    constants = list(re.finditer(r"\\str_const:Ne\s*\\(?P<name>[A-Za-z_:]+)\s*\{", source))
    valid_constants = scanner.valid_positions(constants)
    constant_map = {match["name"]: match for match in constants if match.start() in valid_constants}
    programs = []
    for declaration in declarations:
        outer, outer_start = containing_macro(source, scanner, definitions, declaration.start())
        parameters = [int(value) for value in re.findall(r"#([1-9])", outer["params"])]
        if parameters != list(range(1, len(parameters) + 1)):
            raise ValueError(f"unsupported dynamic message macro signature: {outer.group()}")
        depth = scanner.scan("depths", [source[outer_start + 1:declaration.start()]])[0]
        if depth != 0:
            raise ValueError(f"nested dynamic message declaration at {declaration.start()}")
        scanner.prefetch_openings([declaration.end()])
        body_start = scanner.opening(declaration.end())
        scanner.prefetch([body_start])
        declaration_end = scanner.groups[body_start]
        if declaration["four"]:
            scanner.prefetch_openings([declaration_end])
            extra_start = scanner.opening(declaration_end)
            scanner.prefetch([extra_start])
            declaration_end = scanner.groups[extra_start]
        later = [match for match in definitions if match.start() >= scanner.groups[outer_start]
                 and match["name"] == outer["name"]]
        valid_later = scanner.valid_positions(later)
        boundary = next((match.start() for match in later if match.start() in valid_later), len(source))
        call_pattern = re.compile(r"\\" + re.escape(outer["name"]) + r"(?![A-Za-z_:])")
        calls = list(call_pattern.finditer(source, scanner.groups[outer_start], boundary))
        valid_calls = scanner.valid_positions(calls)
        calls = [call for call in calls if call.start() in valid_calls]
        if not calls:
            raise ValueError(f"no source calls for dynamic message macro {outer['name']}")
        prefixes = [source[source.rfind("\n", 0, call.start()) + 1:call.start()] for call in calls]
        if not all(scanner.scan("prefixes", prefixes)):
            raise ValueError(f"unsupported context for dynamic message macro {outer['name']}")
        arguments = [source[call.end():boundary] for call in calls]
        consumed = scanner.scan("arguments", [{"text": text, "count": len(parameters)} for text in arguments])
        invocations = [prefix + call.group() + text[:end]
                       for prefix, call, text, end in zip(prefixes, calls, arguments, consumed)]
        references = {name for text in invocations for name in re.findall(r"\\([A-Za-z_:]+)", text)}
        needed = []
        visited = set()

        def visit(name):
            if name in visited or name not in constant_map:
                return
            visited.add(name)
            match = constant_map[name]
            start = source.find("{", match.start(), match.end())
            scanner.prefetch([start])
            body = scanner.content(start)
            for dependency in re.findall(r"\\([A-Za-z_:]+)", body):
                visit(dependency)
            needed.append((name, body))

        for name in sorted(references):
            visit(name)
        # String constants store their expanded contents as other-category characters.
        initialization = "".join("\\edef\\" + name +
            "{\\expandafter\\detokenize\\expandafter{\\expanded{" + body + "}}}"
            for name, body in needed)
        header = source[outer.start():outer_start + 1]
        programs.append(initialization + header + source[declaration.start():declaration_end] + "}" +
                        "".join(invocations))
    return "".join(programs)


def dynamic_templates(program, home):
    if not program:
        return []
    probe_text = "__lambda_catalog_probe_text>"
    probe_extra = "__lambda_catalog_probe_extra>"
    source = ("\\ExplSyntaxOn\\def\\c__msg_text_prefix_tl{" + probe_text + "}"
              "\\def\\c__msg_more_text_prefix_tl{" + probe_extra + "}" + program + "\\ExplSyntaxOff")
    (WORK / "dynamic.tex").write_text(source)
    script = WORK / "dynamic.ls"
    script.write_text(
        'import profile: lambda.doc.latex.tex_engine.profile\n'
        'import expansion: lambda.doc.latex.tex_engine.expansion\n'
        'import reconstruct: lambda.doc.latex.tex_engine.reconstruct\n'
        'let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;\n'
        'let source=input("temp/latex-message-catalog/dynamic.tex","text")^;\n'
        'let options={script_expl3:true,echo_definitions:false,adapters:[],resources:{local:{},bundled:{}}};\n'
        'let result=profile.expand_with_kernel(source,kernel,options,1000000)^;\n'
        'let prefix="control:__lambda_catalog_probe_text>";\n'
        'let entries=[for(k,v in result.definitions where starts_with(string(k)^,prefix)) '
        '(let name=slice(string(k)^,len(prefix),len(string(k)^))^, '
        '{name:name,text:v,extra:result.definitions["control:__lambda_catalog_probe_extra>" ++ name]})];\n'
        'let script_query="\\\\ExplSyntaxOn" ++ join([for(e in entries) '
        '"\\\\expandafter\\\\meaning\\\\csname __lambda_catalog_probe_text>" ++ e.name ++ '
        '"\\\\endcsname|\\\\expandafter\\\\meaning\\\\csname __lambda_catalog_probe_extra>" ++ e.name ++ "\\\\endcsname|"],"") ++ "\\\\ExplSyntaxOff";\n'
        'let native_query="\\\\ExplSyntaxOn" ++ join([for(e in entries) '
        '"\\\\expandafter\\\\meaning\\\\csname msg~text~>~" ++ e.name ++ '
        '"\\\\endcsname|\\\\expandafter\\\\meaning\\\\csname msg~extra~text~>~" ++ e.name ++ "\\\\endcsname|"],"") ++ "\\\\ExplSyntaxOff";\n'
        'let checked=if(result.issue==null) expansion.resume(result,script_query,"<catalog-probe>",1000000)^ else result;\n'
        'let native=parse(native_query,{type:"tex"})^;\n'
        'let matches=checked.issue==null and reconstruct.reconstruct(checked)^.text==native.text '
        'and checked.diagnostic_records==native.diagnostics and checked.messages==native.messages;\n'
        'format({issue:result.issue,matches:matches,entries:entries},\'json\')^\n'
    )
    env = os.environ.copy()
    env["LAMBDA_HOME"] = str(home)
    env["LAMBDA_EXEC_BACKEND"] = "jit"
    process = subprocess.run([str(WORK / "lambda.exe"), "--no-log", str(script.relative_to(ROOT))],
                             cwd=ROOT, env=env, capture_output=True, text=True)
    if process.returncode:
        raise RuntimeError("dynamic message expansion failed:\n" + process.stderr[-4000:])
    output = process.stdout.strip()
    if not output.startswith('"') or not output.endswith('"'):
        raise ValueError("dynamic message expansion returned an invalid result")
    result = json.loads(output[1:-1])
    if result["issue"] is not None or not result["matches"] or not result["entries"]:
        raise ValueError("dynamic message expansion differs from the native format")
    templates = []
    for entry in result["entries"]:
        if not re.fullmatch(r"[A-Za-z0-9_:/-]+", entry["name"]) or "/" not in entry["name"]:
            raise ValueError(f"unsupported dynamic message name: {entry['name']}")
        module, name = entry["name"].rsplit("/", 1)
        templates.append({"module": module, "name": name, "text": entry["text"], "extra": entry["extra"]})
    return sorted(templates, key=lambda item: (item["module"], item["name"]))


def native_presence(records, engine, binary_digest):
    frozen = WORK / "lambda.exe"
    source = "\\ExplSyntaxOn" + "".join(
        "\\msg_if_exist:nnTF{" + item["module"] + "}{" + item["name"] + "}{Y}{N}"
        for item in records
    ) + "\\ExplSyntaxOff"
    (WORK / "presence.tex").write_text(source)
    query = WORK / "presence.ls"
    query.write_text(
        'let s=input("temp/latex-message-catalog/presence.tex","text")^;\n'
        'let r=parse(s,{type:"tex"})^;\n'
        'if(len(r.messages)^==0 and len(r.diagnostics)^==0) r.text else "native-query-error"\n'
    )
    result = subprocess.run([str(frozen), "--no-log", str(query.relative_to(ROOT))],
                            cwd=ROOT, capture_output=True, text=True, check=True)
    flags = json.loads(result.stdout)
    if len(flags) != len(records) or set(flags) - {"Y", "N"}:
        raise ValueError("native format returned an invalid message-presence result")
    if digest(engine.read_bytes()) != binary_digest:
        raise RuntimeError("the native executable changed during the catalog query")
    return flags


def compile_templates(records, home):
    (WORK / "records.json").write_text(json.dumps(records, ensure_ascii=False))
    script = WORK / "compile.ls"
    script.write_text(
        'import profile: lambda.doc.latex.tex_engine.profile\n'
        'import expansion: lambda.doc.latex.tex_engine.expansion\n'
        'let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;\n'
        'let records=input("temp/latex-message-catalog/records.json","json")^;\n'
        'let preload=profile.expand_with_kernel("",kernel,{script_expl3:true,echo_definitions:true},1000000)^;\n'
        'let result=expansion.compile_message_templates(preload,records)^;\n'
        'if(result.issue==null) format(result.templates,\'json\')^ else "catalog-compile-error"\n'
    )
    env = os.environ.copy()
    env["LAMBDA_HOME"] = str(home)
    env["LAMBDA_EXEC_BACKEND"] = "jit"
    process = subprocess.run([str(WORK / "lambda.exe"), "--no-log", str(script.relative_to(ROOT))],
                             cwd=ROOT, env=env, capture_output=True, text=True)
    if process.returncode:
        raise RuntimeError("Script catalog compiler failed:\n" + process.stderr[-4000:])
    output = process.stdout.strip()
    if not output.startswith('"') or not output.endswith('"'):
        raise ValueError("Script catalog compiler returned an invalid result")
    # The CLI renders a returned string between quotes without escaping its contents.
    templates = json.loads(output[1:-1])
    expected = {(item["module"], item["name"]) for item in records if not item["preexpand"]}
    actual = {(item["module"], item["name"]) for item in templates}
    if actual != expected or len(templates) != len(expected):
        raise ValueError("Script catalog compiler omitted or repeated a message")
    return templates


def token_ids(macro, pool, indexes):
    expected = {"kind": "macro", "count": 4, "prefix": [], "delimiters": [[], [], [], []],
                "brace_delim": False, "long_ok": True, "protected_flag": False}
    if any(macro.get(key) != value for key, value in expected.items()):
        raise ValueError("message macro has an unexpected TeX signature")
    result = []
    for token in macro["body"]:
        kind, value, cat = token["kind"], token["value"], token.get("cat")
        if kind not in {"char", "control", "parameter"}:
            raise ValueError(f"unsupported message token kind: {kind}")
        key = (kind, value, cat)
        if key not in indexes:
            indexes[key] = len(pool)
            pool.append({"kind": kind, "value": value, "cat": cat,
                         "start": -1, "end": -1, "file": -1, "direct": False})
        result.append(indexes[key])
    return result


def expression(value):
    if isinstance(value, dict):
        return "{" + ",".join(key + ":" + expression(item) for key, item in value.items()) + "}"
    if isinstance(value, list):
        return "[" + ",".join(expression(item) for item in value) + "]"
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def render(source_bytes, records, registered, templates):
    pool, indexes = [], {}
    encoded = [{"module": item["module"], "name": item["name"],
                "text": token_ids(item["text"], pool, indexes),
                "extra": token_ids(item["extra"], pool, indexes)} for item in templates]
    dynamic = [item for item in records if item["preexpand"]]
    lines = ["// Generated from bundled expl3-code.tex; do not edit by hand.",
             "// Regenerate: python3 utils/generate_latex_message_catalog.py",
             "// Source SHA-256: " + digest(source_bytes), "import expl3: .expl3",
             "let token_pool = " + expression(pool) + ";",
             'fn macro(ids) any^ => {*:expl3.token_list([for(i in ids) token_pool[i]]),count:4,delimiters:[[],[],[],[]],long_ok:true}',
             "let encoded = ["]
    lines.extend("    " + expression(item) + ("," if index + 1 < len(encoded) else "")
                 for index, item in enumerate(encoded))
    lines.extend(["];", "let cached=[for(e in encoded) {module:e.module,name:e.name,text:macro(e.text)^,extra:macro(e.extra)^}];",
                  "pub fn prepared() => cached", "pub fn dynamic() => " + expression(dynamic)])
    for kind in ("name", "type"):
        lines.append("pub fn " + kind + "_properties() => " + expression(registered[kind]))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the checked-in catalog")
    parser.add_argument("--engine", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--home", type=Path, default=ROOT / "lmd")
    args = parser.parse_args()
    data = SOURCE.read_bytes()
    source = data.decode("utf-8")
    engine = args.engine.resolve()
    binary_digest = snapshot_engine(engine)
    scanner = TeXScanner(source, args.home.resolve())
    records, dynamic = declarations(source, scanner)
    registered = properties(source, scanner)
    flags = native_presence(records, engine, binary_digest)
    active = [item for item, flag in zip(records, flags) if flag == "Y"]
    templates = compile_templates(active, args.home.resolve())
    generated = dynamic_templates(dynamic_program(source, scanner, dynamic), args.home.resolve())
    existing = {(item["module"], item["name"]) for item in active}
    for item in generated:
        name = (item["module"], item["name"])
        if name in existing:
            raise ValueError(f"dynamic message duplicates a static declaration: {name}")
        existing.add(name)
        active.append({"module": item["module"], "name": item["name"],
                       "body": "", "extra": "", "preexpand": False})
        templates.append(item)
    rendered = render(data, active, registered, templates)
    if SOURCE.read_bytes() != data:
        raise RuntimeError("the bundled expl3 source changed during generation")
    if args.check:
        if not DESTINATION.is_file() or DESTINATION.read_text() != rendered:
            raise SystemExit("LaTeX message catalog differs; regenerate it")
        print("LaTeX message catalog matches bundled source and native format")
    else:
        DESTINATION.write_text(rendered)
        print(f"Generated {len(active)} message definitions")


if __name__ == "__main__":
    main()
