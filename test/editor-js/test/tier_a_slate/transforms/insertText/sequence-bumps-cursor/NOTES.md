# Note: cursor in empty paragraph

`cmdInsertText` currently requires the cursor to sit inside a text leaf. An
empty paragraph has no leaves; the first `insertText` event finds the
cursor at a parent-level path and returns null. This fixture is documented
as a known limitation — see Inline_Formatting design doc for the planned
"insert leaf into empty block" extension.

infrastructure follow-up
