"""Exercise embedded NUL through local I/O, the editor, DOM text, and layout."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = "BEFORE" + chr(0) + "AFTER"


def run(exe, *args, backend="jit"):
    result = subprocess.run(
        [exe, *map(str, args)], cwd=ROOT,
        env=dict(os.environ, LAMBDA_EXEC_BACKEND=backend),
        capture_output=True, timeout=60,
    )
    if result.returncode:
        raise AssertionError((args, result.returncode, result.stdout, result.stderr))
    return result.stdout


def text_runs(tree):
    if tree.get("type") == "text":
        yield tree["content"], tree["layout"].get("width", 0)
    for child in tree.get("children", ()):
        yield from text_runs(child)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    exe = str(parser.parse_args().exe.resolve())
    (ROOT / "temp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / "temp", prefix="nul-text-") as scratch:
        root = Path(scratch)
        raw = root / "payload.txt"
        raw.write_bytes(PAYLOAD.encode())

        source = root / "read.ls"
        source.write_text(
            f"""pn main() {{
                let rejected = input({json.dumps(str(raw) + chr(0) + 'suffix')}, 'text') ^ {{ null }};
                if (rejected != null) {{ print("unexpected path result") }}
                let value = input({json.dumps(str(raw))}, 'text')^;
                print(value);
                print("!")
            }}""",
            encoding="utf-8",
        )
        result = root / "result.ls"
        result.write_text(r'"BEFORE\u0000AFTER"; "tail"', encoding="utf-8")
        golden = ROOT / "test/lambda/string_embedded_nul.txt"
        for backend in ["interp", "jit"]:
            actual = run(exe, "test/lambda/string_embedded_nul.ls", backend=backend)
            assert actual.strip() == golden.read_bytes().strip(), (backend, actual)
            assert run(exe, "run", source, backend=backend).strip() == PAYLOAD.encode() + b"!"
            assert PAYLOAD.encode() + b'tail' in run(exe, result, backend=backend).replace(
                b'"', b""
            ).replace(b"\n", b"")

        source_html = root / "source.html"
        html_bytes = b"<p>BEFORE\x00AFTER</p>\r\n"
        source_html.write_bytes(html_bytes)
        editor_events = root / "editor-events.json"
        editor_report = root / "editor-result.json"
        editor_events.write_text(json.dumps({"events": [
            {"type": "assert_count", "target": {"selector": ".src-text"}, "count": 1},
            {"type": "click", "target": {"text": "AFTER"}},
            {"type": "key_combo", "key": "s", "mods_str": "super"},
            {"type": "assert_text", "target": {"selector": ".edit-status"},
             "contains": "Saved source.html"},
        ]}), encoding="utf-8")
        run(exe, "edit", source_html, "--headless", "--event-file", editor_events,
            "--event-result", editor_report)
        assert json.loads(editor_report.read_text())["assertions"] == {"passed": 2, "failed": 0}
        assert source_html.read_bytes() == html_bytes

        invalid = root / "invalid.txt"
        invalid_bytes = b"before\x00\xffafter"
        invalid.write_bytes(invalid_bytes)
        for options in [[], ["--source"]]:
            rejected = subprocess.run([exe, "edit", invalid, "--headless", *options],
                                      cwd=ROOT, capture_output=True, timeout=60)
            assert rejected.returncode and b"not safely readable as UTF-8 text" in rejected.stderr, rejected
            assert invalid.read_bytes() == invalid_bytes

        reload_probe = root / "reload.ls"
        reload_probe.write_text(
            f'''import session: lambda.editor.session
                import source: lambda.editor.surface.source
                pn main() {{
                    let path = {json.dumps(str(invalid))}
                    let initial = "before\\u0000after"
                    let loaded = source.descriptor.import_text(initial)^
                    let opened = session.new_session(path, source.descriptor, initial, loaded)^
                    let reloaded = session.reload(opened)^
                    print(reloaded.ok == false and contains(reloaded.status, "UTF-8"))
                }}''',
            encoding="utf-8",
        )
        for backend in ["interp", "jit"]:
            assert run(exe, "run", reload_probe, backend=backend).strip() == b"true"
        assert invalid.read_bytes() == invalid_bytes

        page = root / "view.ls"
        page.write_text(
            r'''<html <head <style "body {font: 24px monospace}">> <body
                <p "BEFORE">
                <p "BEFORE\u0000AFTER">
                <p "\u0000start">
                <p "end\u0000">
            >>''',
            encoding="utf-8",
        )
        tree = root / "view.json"
        run(exe, "layout", page, "--view-output", tree)
        runs = dict(text_runs(json.loads(tree.read_text())["layout_tree"]))
        assert runs[PAYLOAD] > runs["BEFORE"], runs
        assert "\0start" in runs and "end\0" in runs, runs

        svg = root / "view.svg"
        run(exe, "render", page, "-o", svg)
        root_svg = ET.parse(svg).getroot()
        assert "\0" not in svg.read_text(), "NUL is invalid in SVG XML"
        assert "BEFORE■AFTER" in "".join(root_svg.itertext()), svg

        events = root / "events.json"
        report = root / "events-result.json"
        events.write_text(json.dumps({"events": [{
            "type": "assert_text", "target": {"selector": "pre"}, "contains": "AFTER",
        }]}), encoding="utf-8")
        run(exe, "view", raw, "--headless", "--event-file", events, "--event-result", report)
        assert json.loads(report.read_text())["assertions"] == {"passed": 1, "failed": 0}

        html = root / "dom.html"
        html.write_text(
            r'''<!doctype html><meta charset="utf-8">
            <p id="host"></p><p id="result"></p><textarea id="control"></textarea><p id="markup"></p>
            <script>
            const s = "BEFORE\u0000AFTER";
            const host = document.getElementById("host");
            const n = document.createTextNode(s);
            host.appendChild(n);
            const created = n.data === s && n.nodeValue === s && n.length === 12;
            const observer = new MutationObserver(() => {});
            observer.observe(n, {characterDataOldValue: true});
            n.data = "\u0000begin";
            const old = observer.takeRecords()[0].oldValue === s;
            const updated = n.data === "\u0000begin";
            n.replaceData(1, 5, "end\u0000");
            const replaced = n.data === "\u0000end\u0000";
            host.textContent = s;
            const assigned = host.textContent === s;
            const control = document.getElementById("control");
            control.value = s;
            const value = control.value === s;
            const markup = document.getElementById("markup");
            markup.innerHTML = "<b>" + s + "</b>";
            const fragment = markup.textContent.startsWith("BEFORE") && markup.textContent.endsWith("AFTER");
            document.getElementById("result").textContent =
                [created, updated, replaced, assigned, value, old, fragment].join("/");
            </script>''',
            encoding="utf-8",
        )
        run(exe, "layout", html, "--view-output", tree)
        runs = dict(text_runs(json.loads(tree.read_text())["layout_tree"]))
        assert "true/true/true/true/true/true/true" in runs, runs
        assert PAYLOAD in runs, runs

        attributes = root / "attributes.html"
        attributes.write_text(
            r'''<!doctype html><meta charset="utf-8">
            <p id="host"></p><p id="result"></p>
            <script>
            const data = "BEFORE\u0000AFTER";
            const next = "BEFORE\u0000LAST";
            const host = document.getElementById("host");
            const comment = document.createComment(data);
            host.appendChild(comment);
            const commentRead = comment.data === data && comment.nodeValue === data &&
                comment.textContent === data && comment.length === data.length;
            const commentCopy = comment.cloneNode().data === data;
            const observer = new MutationObserver(() => {});
            observer.observe(comment, {characterDataOldValue: true});
            comment.nodeValue = "\u0000🙂";
            const commentWrite = comment.data === "\u0000🙂" && comment.length === 3 &&
                observer.takeRecords()[0].oldValue === data;
            host.setAttribute("data-test", data);
            const direct = host.getAttribute("data-test") === data &&
                host.getAttribute("data-test\u0000-other") === null &&
                host.dataset.test === data;
            const attr = host.getAttributeNode("data-test");
            const attrRead = attr.value === data && attr.nodeValue === data &&
                attr.textContent === data && attr.cloneNode().value === data;
            observer.observe(host, {attributeOldValue: true});
            host.dataset.test = next;
            const attrWrite = attr.value === next && observer.takeRecords()[0].oldValue === data;
            attr.value = data;
            const observedAttr = observer.takeRecords()[0].oldValue === next;
            const standalone = document.createAttribute("data-other");
            standalone.textContent = next;
            host.setAttributeNode(standalone);
            const detached = host.getAttribute("data-other") === next;
            host.setAttributeNS("urn:test", "x:value", data);
            const named = host.getAttributeNS("urn:test", "value") === data &&
                host.getAttributeNodeNS("urn:test", "value").value === data;
            const htmlText = host.outerHTML.includes('data-test="' + data + '"') &&
                host.outerHTML.includes("<!--\u0000🙂-->");
            const xmlText = new XMLSerializer().serializeToString(host);
            const xmlString = xmlText.includes('data-test="' + data + '"') &&
                xmlText.includes("<!--\u0000🙂-->");
            const selectors = !host.matches('[data-test="BEFORE"]') &&
                host.matches('[data-test^="BEFORE"][data-test$="AFTER"][data-test*="AFTER"]');
            host.id = "raw\u0000id";
            const id = host.id === "raw\u0000id" && !host.matches("#raw") &&
                document.getElementById("raw\u0000id") === host &&
                document.getElementById("raw") === null;
            host.className = "class\u0000name ordinary";
            const classes = host.className === "class\u0000name ordinary" &&
                host.classList.contains("class\u0000name") &&
                host.classList.item(0) === "class\u0000name" &&
                !host.matches(".class") && host.matches(".ordinary");
            host.classList.replace("class\u0000name", "\u0000new");
            const classWrite = host.getAttribute("class") === "ordinary \u0000new";
            const reflected = document.createElement("input");
            reflected.placeholder = data;
            reflected.name = next;
            const reflectsText = reflected.getAttribute("placeholder") === data &&
                reflected.placeholder === data && reflected.name === next &&
                reflected.getAttribute("name") === next;
            const select = document.createElement("select");
            select.innerHTML = "<option>one</option><option>two</option><option>three</option>";
            select.options[0].value = data;
            select.options[1].value = next;
            select.options[2].text = "\u0000three";
            select.value = next;
            const selected = select.selectedIndex === 1 && select.value === next &&
                select.options[0].value === data && select.options[2].text === "\u0000three";
            select.value = "\u0000three";
            const selectedByText = select.selectedIndex === 2 && select.value === "\u0000three";
            document.getElementById("result").textContent = [
                commentRead, commentCopy, commentWrite, direct, attrRead,
                attrWrite, observedAttr, detached, named, htmlText, xmlString, selectors,
                id, classes, classWrite, reflectsText, selected, selectedByText
            ].map(value => value ? "1" : "0").join("");
            </script>''',
            encoding="utf-8",
        )
        run(exe, "layout", attributes, "--view-output", tree)
        runs = dict(text_runs(json.loads(tree.read_text())["layout_tree"]))
        assert "1" * 18 in runs, runs

        lambda_dom = root / "dom.ls"
        lambda_dom.write_text(
            r'''import dom

            pn main() {
                let doc = dom.load("test/js/dom_identity.html")
                let document_root = dom.document_element(doc)
                let data = "BEFORE\u0000AFTER"
                let changed = "\u0000CHANGED"
                let comment = dom.create_node(document_root, 8, null, data)
                print(dom.node_value(comment) == data)
                dom.set_node_value(comment, changed)
                print(dom.node_value(comment) == changed)
                dom.set_attribute(document_root, "data-test", data)
                print(dom.get_attribute(document_root, "data-test") == data)
            }''',
            encoding="utf-8",
        )
        for backend in ["interp", "jit"]:
            actual = run(exe, "run", lambda_dom, backend=backend)
            assert actual.count(b"true") == 3, (backend, actual)

        generated = root / "generated.html"
        generated.write_text(
            r'''<!doctype html><meta charset="utf-8">
            <style>
              #generated::before { content: attr(data-test) }
              li::marker { content: attr(data-test) }
            </style>
            <p id="generated"></p><ol><li id="item">List content</li></ol>
            <script>
              document.getElementById("generated").setAttribute("data-test", "\u0000PSEUDOEND");
              document.getElementById("item").setAttribute("data-test", "\u0000MARKEREND");
            </script>''',
            encoding="utf-8",
        )
        run(exe, "render", generated, "-o", svg)
        exported = "".join(ET.parse(svg).getroot().itertext())
        assert "■PSEUDOEND" in exported and "■MARKEREND" in exported, exported

        form_page = root / "form.html"
        form_png = root / "form.png"
        for node, attribute in [
            ('<input type="submit" id="target">', "value"),
            ('<input id="target">', "placeholder"),
            ('<textarea id="target"></textarea>', "placeholder"),
            ('<select><option id="target">initial</option></select>', "label"),
        ]:
            images = []
            for label in ["FIRST", r"FIRST\u0000SUFFIX"]:
                form_page.write_text(
                    '<!doctype html><meta charset="utf-8">'
                    '<style>input,textarea,select {font: 24px monospace}</style>' + node +
                    f'<script>document.getElementById("target").setAttribute('
                    f'"{attribute}", "{label}");</script>',
                    encoding="utf-8",
                )
                run(exe, "render", form_page, "-o", form_png)
                images.append(form_png.read_bytes())
            assert images[0] != images[1], (node, attribute)
    print("Embedded NUL checks passed")


if __name__ == "__main__":
    main()
