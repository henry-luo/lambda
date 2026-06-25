# Note: cursor placement after raw step

A raw `replace_text` step does not move the selection — `txStep` calls
`selMap` which shifts the position past the insert. The cursor sat at
offset 2 before the step; after inserting "Y" at offset 2 with the
post-bias mapping rule, the cursor lands at offset 3. Matches the parsed
expected `heY<cursor></cursor>llo` (cursor at [0,0]/3).
