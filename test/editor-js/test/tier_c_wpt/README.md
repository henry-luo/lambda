# Tier C — WPT platform tests (selective)

Source: `ref/wpt/` (already checked out in this repo). Selective adoption, NOT comprehensive — these are tier-1 platform-contract gates per [Radiant_Rich_Text_Editor3.md §2](../../../../vibe/Radiant_Rich_Text_Editor3.md).

| WPT corpus | Local count | Adoption stance |
|---|---|---|
| `selection/`                | 162 HTML | Curated subset (~30) — DOM Range / Selection API conformance |
| `contenteditable/`          | 4 HTML   | All four |
| `input-events/`             | 25 HTML  | Curated subset (~15) — beforeinput / InputEvent contract |
| `clipboard-apis/`           | 56 HTML  | Curated subset (~20) — async-clipboard contract |
| `editing/run/` (execCommand) | 43 HTML + 17 huge JS data tables | **Informational only; never gating** — this corpus codifies legacy browser quirks a PM-class editor deliberately rejects |

These tests run via the existing Lambda WPT harness (`test/wpt/test_wpt_*_gtest.cpp`) against Radiant's DOM Selection / contenteditable / clipboard implementation. They are documented here for cross-reference, but their execution lives outside the JS reference (the JS reference doesn't reimplement the web platform).
