#!/usr/bin/env python3
"""Compare complete DocBook HTML bodies with pinned official xslTNG fixtures."""
import argparse
import difflib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "utils"))
import docbook_reference as oracle

FIXTURES = ROOT / "test/input/docbook"
CASES = ["article", "book", "tables", "notes", "lists", "book_notes", "inlines", "verbatim"]
VOIDS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.root = ["body", [], []]
        self.stack = [self.root]
        self.body = None
        self.feed(text)
        self.close()

    def handle_starttag(self, tag, attrs):
        # Namespace declarations differ in placement, not HTML namespace identity.
        attrs = sorted((key, value) for key, value in attrs
                       if key != "xmlns" and not key.startswith("xmlns:"))
        node = [tag, attrs, []]
        self.stack[-1][2].append(node)
        if tag == "body":
            self.body = node
        if tag not in VOIDS:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOIDS:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        if tag in VOIDS:
            return
        if len(self.stack) < 2 or self.stack[-1][0] != tag:
            raise ValueError("Mismatched HTML end tag " + tag)
        self.stack.pop()

    def handle_data(self, text):
        children = self.stack[-1][2]
        if children and isinstance(children[-1], str):
            children[-1] += text
        else:
            children.append(text)

    def handle_comment(self, text):
        self.stack[-1][2].append(["!comment", [], [text]])

    def canonical(self):
        if len(self.stack) != 1:
            raise ValueError("Unclosed HTML element " + self.stack[-1][0])
        return self.body if self.body is not None else self.root


def export(name, chunked=False):
    case = name + ("-chunked" if chunked else "")
    folder = ROOT / "temp/docbook-parity" / case
    # A stale page must not conceal a missing export or a removed chunk.
    if folder.exists():
        shutil.rmtree(folder)
    script = ROOT / "temp/docbook-parity" / ("_" + case + ".ls")
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text(
        "import d: lambda.doc.docbook\npn main() {\n"
        "    let source = d.read(" + json.dumps(str(FIXTURES / (name + ".xml"))) + ")^\n"
        "    let result = d.convert(source, {unsupported:\"error\"" +
        (",output:\"chunked\"" if chunked else "") + "})^\n"
        "    d.write(result," + json.dumps(str(folder)) + ")^\n}\n")
    run = subprocess.run([str(ROOT / "lambda.exe"), "run", str(script)], cwd=ROOT,
                         capture_output=True, text=True, timeout=60)
    if run.returncode:
        raise RuntimeError(run.stdout + run.stderr)
    return folder


def generated_ids(tree, seen=None):
    """Map Saxon's ephemeral db-id metadata to the source occurrence's HTML ID."""
    seen = {} if seen is None else seen
    if isinstance(tree, str):
        return tree
    tag, pairs, children = tree
    attrs = dict(pairs)
    if "db-id" in attrs:
        if "id" not in attrs or (attrs["db-id"] in seen and seen[attrs["db-id"]] != attrs["id"]):
            raise ValueError("Non-bijective upstream occurrence identity")
        seen[attrs["db-id"]] = attrs["id"]
        attrs["db-id"] = attrs["id"]
    return [tag, sorted(attrs.items()), [generated_ids(child, seen) for child in children]]


def nodes(tree):
    if not isinstance(tree, str):
        yield tree
        for child in tree[2]:
            yield from nodes(child)


def publication_integrity(folder):
    documents = {}
    for page in folder.glob("*.html"):
        parsed = Document(page.read_text())
        parsed.canonical()
        documents[page.resolve()] = parsed.root
    identities = {}
    for page, document in documents.items():
        ids = [dict(node[1])["id"] for node in nodes(document) if "id" in dict(node[1])]
        if len(ids) != len(set(ids)):
            raise ValueError("Duplicate HTML ID in " + page.name)
        identities[page] = set(ids)
    for page, document in documents.items():
        for node in nodes(document):
            for key, value in node[1]:
                if key not in {"src", "href"} or value is None:
                    continue
                uri = urlsplit(value)
                if uri.scheme or uri.netloc:
                    continue
                target = (page.parent / unquote(uri.path)).resolve() if uri.path else page
                if not target.is_file():
                    raise ValueError("Missing publication target: " + value)
                if uri.fragment and target in identities and unquote(uri.fragment) not in identities[target]:
                    raise ValueError("Missing publication fragment: " + value)
    for stylesheet in folder.rglob("*.css"):
        for value in re.findall(r'url\(\s*[\"\']?([^\"\')]+)', stylesheet.read_text()):
            uri = urlsplit(value)
            if uri.scheme or uri.netloc or not uri.path:
                continue
            if not (stylesheet.parent / unquote(uri.path)).is_file():
                raise ValueError("Missing CSS dependency: " + value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh", action="store_true", help="regenerate official expected bodies")
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--chunked", action="store_true")
    args = parser.parse_args()
    expected_dir = FIXTURES / "expected"
    expected_dir.mkdir(exist_ok=True)
    failures = 0
    for name in args.case or CASES:
        if args.chunked:
            expected = expected_dir / (name + "-chunked")
            if args.refresh:
                temporary = ROOT / "temp/docbook-reference" / (name + "-chunked")
                if temporary.exists():
                    shutil.rmtree(temporary)
                temporary.mkdir(parents=True, exist_ok=True)
                oracle.transform(oracle.distribution(), FIXTURES / (name + ".xml"), temporary / "index.html",
                                 ["chunk=index.html", "chunk-output-base-uri=" + temporary.as_uri() + "/"])
                expected.mkdir(exist_ok=True)
                for old_page in expected.glob("*.html"):
                    old_page.unlink()
                for page in temporary.glob("*.html"):
                    body = re.search(r"<body\b.*?</body>", page.read_text(), re.S)
                    if not body:
                        raise ValueError("Missing upstream body: " + page.name)
                    (expected / page.name).write_text(body.group(0))
            references = list(expected.glob("*.html"))
            if not references:
                raise SystemExit("Missing chunk references; use --refresh")
        else:
            expected = expected_dir / (name + ".html")
            if args.refresh:
                temporary = ROOT / "temp/docbook-reference" / (name + "-single.html")
                oracle.transform(oracle.distribution(), FIXTURES / (name + ".xml"), temporary, [])
                body = re.search(r"<body\b.*?</body>", temporary.read_text(), re.S)
                if not body:
                    raise ValueError("Missing upstream body: " + name)
                expected.write_text(body.group(0))
            if not expected.exists():
                raise SystemExit("Missing reference " + str(expected) + "; use --refresh")
            references = [expected]
        actual = export(name, args.chunked)
        publication_integrity(actual)
        if args.chunked and {p.name for p in references} != {p.name for p in actual.glob("*.html")}:
            failures += 1
            print(name + ": FAIL (chunk filename set)")
            continue
        for page in sorted(references):
            output = actual / (page.name if args.chunked else "index.html")
            reference = Document(page.read_text()).canonical()
            if args.chunked:
                reference = generated_ids(reference)
            produced = Document(output.read_text()).canonical()
            if produced != reference:
                failures += 1
                before = json.dumps(reference, ensure_ascii=False, indent=2).splitlines()
                after = json.dumps(produced, ensure_ascii=False, indent=2).splitlines()
                delta = list(difflib.unified_diff(before, after, fromfile="xslTNG", tofile="Lambda"))
                print(name + "/" + page.name + ": FAIL\n" + "\n".join(delta[:120]))
            else:
                print(name + "/" + page.name + ": PASS (complete body, attributes, classes and text)")
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
