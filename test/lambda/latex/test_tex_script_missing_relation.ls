import recovery: ._tex_script_recovery

[for (source in [
    "\\ifnum1 1 T\\else F\\fi X", "\\ifnum1 2 T\\else F\\fi X",
    "\\ifnum1 a T\\else F\\fi X", "\\ifnum0 a T\\else F\\fi X",
    "\\unless\\ifnum1 2 T\\else F\\fi X",
    "\\def\\n{1}\\ifnum1 \\n T\\else F\\fi X",
    "\\ifdim1pt 1pt T\\else F\\fi X", "\\ifdim1pt 2pt T\\else F\\fi X",
    "\\iftrue\\ifnum0 a T\\else F\\fi X\\else Z\\fi",
    "\\ifnum1 \\relax T\\else F\\fi X"
]) recovery.check(source)^]
