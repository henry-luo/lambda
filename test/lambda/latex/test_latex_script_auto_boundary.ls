// Both forms must select the same engine, including at an unsupported boundary.
let path = "test/lambda/latex/fixtures/script_auto_unsupported.tex";
let automatic = input(path) ^ { null };
let explicit = input(path, "latex") ^ { null };
{matches:automatic == explicit}
