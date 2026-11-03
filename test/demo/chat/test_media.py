#!/usr/bin/env python3
"""Real windows send emoji, ZIP bytes and compressed native clipboard images."""
import base64
import json
import os
import zipfile

import test_launch as launch
from test_shared_clients import attribute, count, send


def click(selector):
    return {"type":"click", "target":{"selector":selector}}


def value(selector, text):
    return {"type":"assert_value", "target":{"selector":selector}, "equals":text}


def text(selector, text):
    return {"type":"type", "target":{"selector":selector}, "text":text, "clear":True}


def script(code):
    return {"type":"assert_script", "js":code}


def attr(selector, name, expected):
    return {"type":"assert_attribute", "target":{"selector":selector}, "attribute":name,
            "equals":expected, "assert_timeout":15000}


class ChatMediaTests(launch.ChatLaunchTests):
    def write_fixture(self, user, events=None):
        # keep inherited launcher checks while giving media work a larger viewport.
        super().write_fixture(user, events)
        path=self.artifacts / (user+".json")
        data=json.loads(path.read_text())
        data["viewport"]={"width":1200, "height":900}
        path.write_text(json.dumps(data))

    def save_events(self, name, destination):
        return [click(f'.incoming button[aria-label="Save {name}"]'),
                *self.path_events(destination), click("#attachment-confirm"),
                count("#attachment-dialog", 0)]

    def path_events(self, path):
        # paste long absolute paths as one edit, as a user copying a path does;
        # per-character replay would spend the peer's receipt wait typing it.
        return [click("#attachment-path"), {"type":"key_combo", "key":"a", "mods_str":"ctrl"},
                {"type":"paste_text", "target":{"selector":"#attachment-path"}, "text":str(path)}]

    def test_mobile_composer_controls(self):
        self.write_fixture("alex", [attribute("data-online", "true"), click("#emoji-toggle"),
            click('.emoji-choice[aria-label="Wave"]'), value("#draft", "👋"), click("#send"),
            count('.outgoing .bubble[data-status="sent"]', 1),
            {"type":"paste_image", "target":{"selector":"#draft"}, "file":"test/ui/note/fixtures/clipboard.png"},
            attr("#pending-attachment .attachment", "data-mime", "image/webp"),
            script("var b=document.querySelector('#send').getBoundingClientRect(); b.x>=0&&b.right<=390&&b.bottom<=844"),
            click("#remove-attachment"), click("#attach-file"),
            *self.path_events("test/demo/chat/README.md"), click("#attachment-confirm"),
            attr("#pending-attachment .attachment", "data-kind", "file"), click("#remove-attachment")])
        path=self.artifacts / "alex.json"
        data=json.loads(path.read_text())
        data["viewport"]={"width":390,"height":844}
        path.write_text(json.dumps(data))
        with self.running("alex", self.view_options()) as process:
            self.assertEqual(process.wait(timeout=90), 0, str(self.artifacts))
        self.assert_server_closed()

    def test_emoji_file_clipboard_and_restart(self):
        # incompressible bytes exercise the explicit listener limit above 1 MiB.
        payload=os.urandom(1100000)
        source=self.artifacts / "payload.bin"
        source.write_bytes(payload)
        saved_zip=self.artifacts / "received.zip"
        saved_image=self.artifacts / "received.webp"
        image="test/ui/note/fixtures/clipboard.png"
        alex=[attribute("data-online", "true"), text("#draft", "Hello Maya"),
              script("document.querySelector('#draft').setSelectionRange(6,10); true"),
              click("#emoji-toggle"), click('.emoji-choice[aria-label="Wave"]'),
              value("#draft", "Hello 👋"), click("#send"),
              count('.outgoing .bubble[data-status="read"]', 1),
              click("#attach-file"), *self.path_events(source), click("#attachment-confirm"),
              attr("#pending-attachment .attachment", "data-kind", "file"),
              *send("Archive for Maya"), count('.outgoing .bubble[data-status="read"]', 2),
              {"type":"paste_image", "target":{"selector":"#draft"}, "file":image},
              attr("#pending-attachment .attachment", "data-mime", "image/webp"),
              script("var a=document.querySelector('#pending-attachment .attachment'); "
                     "Number(a.getAttribute('data-size')) < Number(a.getAttribute('data-source-size'))"),
              script("document.querySelector('#pending-attachment img').getBoundingClientRect().width > 0"),
              click("#send"), count('.outgoing .bubble[data-status="read"]', 3),
              click("#compress-images"),
              {"type":"paste_image", "target":{"selector":"#draft"}, "file":image},
              attr("#pending-attachment .attachment", "data-mime", "image/png"),
              click("#remove-attachment"), count("#pending-attachment", 0),
              {"type":"paste_text", "target":{"selector":"#draft"}, "text":"Ordinary text paste"},
              value("#draft", "Ordinary text paste"), click("#send"),
              count('.outgoing .bubble[data-status="read"]', 4),
              count('.incoming .bubble[data-status="read"]', 1)]
        maya=[attribute("data-online", "true"), count('.incoming .bubble[data-status="read"]', 4),
              count('.incoming .attachment[data-kind="file"]', 1),
              count('.incoming .attachment[data-kind="image"] img', 1),
              *self.save_events("payload.bin.zip", saved_zip),
              *self.save_events("clipboard.webp", saved_image),
              *send("Received 👍"), count('.outgoing .bubble[data-status="read"]', 1),
              {"type":"capture_surface", "source":"page", "file":str(self.artifacts.relative_to(launch.ROOT) / "media.png")}]
        for user, events in (("alex", alex), ("maya", maya)):
            self.write_fixture(user, events)
        with self.running("maya,alex", self.view_options()) as process:
            self.assertEqual(process.wait(timeout=150), 0, str(self.artifacts))
        for user in ("alex", "maya"):
            report=json.loads((self.artifacts / (user+"-result.json")).read_text())
            self.assertEqual(report["result"], "PASS", report)
        with zipfile.ZipFile(saved_zip) as archive:
            self.assertEqual(archive.namelist(), ["payload.bin"])
            self.assertEqual(archive.read("payload.bin"), payload)
            self.assertEqual(archive.getinfo("payload.bin").compress_type, zipfile.ZIP_DEFLATED)
        history=json.loads((self.artifacts / "history.json").read_text())["messages"]
        self.assertEqual(len(history), 5)
        self.assertTrue(all(message["status"] == "read" for message in history))
        attachments=[message["attachment"] for message in history if "attachment" in message]
        self.assertEqual({item["kind"] for item in attachments}, {"file", "image"})
        image_data=next(item for item in attachments if item["kind"] == "image")
        saved_bytes=saved_image.read_bytes()
        self.assertEqual(base64.b64decode(image_data["data"].split(",", 1)[1]), saved_bytes)
        self.assertEqual((saved_bytes[:4], saved_bytes[8:12]), (b"RIFF", b"WEBP"))
        self.assertLess(saved_image.stat().st_size, (launch.ROOT / image).stat().st_size)
        self.assertFalse(list((self.artifacts / "outgoing").glob("*.zip")), "ZIP scratch file was retained")
        self.assert_server_closed()
        # reuse the same history with a new server and new processes; each client
        # must receive multiple bounded replay frames with the actual attachment bytes.
        for user in ("alex", "maya"):
            self.write_fixture(user, [attribute("data-messages", "5"), count(".attachment", 2),
                                      count('.attachment[data-kind="image"] img', 1)])
        with self.running("maya,alex", self.view_options()) as process:
            self.assertEqual(process.wait(timeout=90), 0, str(self.artifacts))
        self.assert_server_closed()


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
