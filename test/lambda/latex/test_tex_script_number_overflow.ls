import recovery: ._tex_script_recovery

[for (source in [
    "\\number2147483647 X", "\\number2147483648 X", "\\number-2147483648 X",
    "\\number9999999999999999999999999X", "\\number+0002147483648 X",
    "\\number'17777777777 X", "\\number'20000000000 X",
    "\\number\"7FFFFFFF X", "\\number\"800000000FFFFX",
    "\\count0=999999999999 X\\the\\count0",
    "\\ifnum9999999999=2147483647 T\\else F\\fi X",
    "\\def\\n{9999999999}\\number\\n X",
    "\\number9999999999\\relax X", "\\number9999999999%comment\nX"
]) recovery.check(source)^];

[for (source in [
    "\\count0=42\\divide\\count0 by 0 X\\the\\count0",
    "\\count0=-42\\divide\\count0 by 0 X\\the\\count0",
    "\\count0=1073741824\\multiply\\count0 by 2 X\\the\\count0",
    "\\count0=-1073741824\\multiply\\count0 by 2 X\\the\\count0",
    "\\count0=42\\divide\\count0 by 2 X\\the\\count0",
    "\\dimen0=2pt\\divide\\dimen0 by 0 X\\the\\dimen0",
    "\\dimen0=10000pt\\multiply\\dimen0 by 2 X\\the\\dimen0",
    "\\skip0=2pt plus 1pt\\divide\\skip0 by 0 X\\the\\skip0",
    "\\skip0=10000pt plus 1pt\\multiply\\skip0 by 2 X\\the\\skip0",
    "\\muskip0=2mu plus 1mu\\divide\\muskip0 by 0 X\\the\\muskip0",
    "\\count0=10{\\count0=21\\global\\divide\\count0 by 0 X}\\the\\count0",
    "\\count0=10\\divide\\count0 by 0\\multiply\\count0 by 9999999999 X\\the\\count0"
]) recovery.check(source)^];

[for (source in [
    "\\number\\numexpr2147483647+1\\relax X",
    "\\number\\numexpr-2147483647-1+5\\relax X",
    "\\number\\numexpr1/0+7\\relax X", "\\number\\numexpr1/0+2/0\\relax X",
    "\\number\\numexpr2147483647*2/3\\relax X",
    "\\number\\numexpr(2147483647+1)*2+3\\relax X",
    "\\number\\numexpr10+\\numexpr1/0\\relax+2\\relax X",
    "\\number\\numexpr2147483647+1+\\numexpr1/0\\relax\\relax X",
    "\\number\\numexpr2+3*4\\relax X", "\\the\\dimexpr16000pt+1000pt\\relax X",
    "\\the\\dimexpr1pt/0+2pt\\relax X", "\\the\\glueexpr1pt plus 1fil /0 +2pt\\relax X",
    "\\the\\muexpr1mu plus 1fil /0 +2mu\\relax X",
    "\\ifnum\\numexpr1/0\\relax=0 T\\else F\\fi X",
    "\\count0=\\numexpr1/0+\\number3\\relax X\\the\\count0"
]) recovery.check(source)^];

// recovery keeps the native diagnostic while continuing through each operation.
[for (source in [
    "\\count0=3 \\divide\\count0 by 0 \\the\\count0",
    "\\dimen0=16384pt \\the\\dimen0", "\\dimen0=1 .5pt",
    "\\skip0=1pt \\divide\\skip0 0", "\\skip0=16383pt \\multiply\\skip0 2",
    "\\the\\dimexpr 1pt/0\\relax", "\\the\\dimexpr 16383pt*2\\relax"
]) recovery.check(source)^]
