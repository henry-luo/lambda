# Radiant test repairs — 2026-11-28

## Scope and ownership

Repair the reported Radiant document and paged tests, EPUB state checker and
12 UI replays. Radiant owns its native layout, rendering, event and lifetime
providers under **D9.1.3v2**; document package routing follows **D9.3.4**.
The task checkpoint and local evidence are in `temp/fix-failed-tests/`.

## Native document and layout

- An idle counter checkpoint had indeterminate owner fields. Null initialization
  prevents its destructor from reading an invalid context (**D5.5.1**, RI-272).
- Edit rollback retained node identities and content but omitted host and child
  attributes. It now snapshots the host and uses the shared restore path for
  both host and descendants (**D10.2.1 / D5.5.1**, RI-273). Focus delivery also
  retains its pinned detached target and owner state through blur/focusout
  (**D5.5.1**, RI-019).
- A provisional flex block size no longer establishes a percentage height
  basis. Embedded SVG gets its hidden-overflow default, and a replaced item's
  automatic minimum derives its content suggestion without the preferred main
  size while preserving cross-axis transfers. The auto/definite/auto SVG
  transition and visible/clip/hidden overflow controls match Chromium (RI-274).
- Inline font-box realignment preserves already-applied relative offsets,
  including ruby annotations with atomic children (RI-275). Flex intrinsic
  margins resolve mixed percentage math against a known containing inline size
  (RI-276); table row minima classify sizes after variable substitution
  (RI-277); textarea family changes preserve the user-agent font size (RI-278).
- Updated native fixtures to exercise device-backed output in offline mode,
  supply a painter clip, and query their intended sizing contracts (RI-279).
  The paged table baseline oracle now uses the first line's row-relative baseline
  established by the browser and native controls (RI-052).

## Package and UI replays

Standalone EPUB and diff routes now resolve their registered package transforms
(LR12-74). Standalone SVG uses its own document surface and resource base
(**D10.1.5**, LR12-75); isolated SVG image previews keep their restrictions.
The editor preview exposes Mark metadata titles; invalid navigation delivers
the retained blur sequence. The TOML fixture and CSS-risk selector now match
their source structures. Alert actions avoid an extra line box, the IME
popover anchors its near edge without uncommitted geometry, and grid/workbook
replays advance the frames on which their scheduled state changes occur.

## Verification

The complete Radiant document binary passed 545/545 with macOS window and media
services available. The paged-media binary passed 505/505; the existing image
basis renderer suite passed 8/8. The EPUB state checker and all 12 originally
reported UI replays passed. `no-int-cast-radiant` lint passed. The broader
Radiant baseline passed with 8,190 passed, 298 partial and 20 skipped,
including all 776 CSS sizing cases; its report is in
`temp/fix-failed-tests/radiant-baseline-verified.log`. The Lambda baseline passed
5,223/5,223 after its DOM provider fixture was updated to verify the current
canvas gradient argument (**D9.5.6**). After the Lambda baseline, the IME
replay passed 67/67 assertions, the original standalone SVG replay passed 5/5,
and the isolated resource and both document image-preview controls also passed.
The Radiant baseline was rerun after those final changes.
