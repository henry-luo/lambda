let archive = input("test/lambda/latex/fixtures/script_captured_member.zip")^;
let member = [for (entry in content(archive)^ where entry.name == "boundary.tex") entry][0];
let captured = input(member, "latex") ^ { null };
let direct = parse(input(member, "text")^, "latex") ^ { null };
{matches:captured == direct}
