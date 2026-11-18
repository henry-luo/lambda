import recovery: ._tex_script_recovery

[for (source in [
    "\\dimen0=1 X\\the\\dimen0", "\\dimen0=-1.5abc X\\the\\dimen0",
    "\\dimen0=2pX\\the\\dimen0", "\\dimen0=1qq X\\the\\dimen0",
    "\\dimen0=3\\relax X\\the\\dimen0", "\\dimen0=4trueX\\the\\dimen0",
    "\\def\\unit{qq}\\dimen0=2\\unit X\\the\\dimen0",
    "\\ifdim1x=1pt T\\else F\\fi X", "\\dimen0=1%comment\nX\\the\\dimen0",
    "\\skip0=1x plus 2pt X\\the\\skip0",
    "\\muskip0=2x X\\the\\muskip0", "\\muskip0=-1.5pt X\\the\\muskip0",
    "\\muskip0=2mX\\the\\muskip0", "\\muskip0=1mu plus 2x X\\the\\muskip0",
    "\\dimen0=1pt X\\the\\dimen0", "\\muskip0=2mu X\\the\\muskip0",
    "\\skip0=1pt plus 2fil X\\the\\skip0",
    "\\skip0=1pt plus 2fill X\\the\\skip0",
    "\\skip0=1pt plus 2filll X\\the\\skip0",
    "\\skip0=1pt plus 2fillll X\\the\\skip0",
    "\\skip0=1pt minus 2fillllll X\\the\\skip0",
    "\\muskip0=1mu plus 2fillll X\\the\\muskip0",
    "\\skip0=1pt plus 2fil" ++ join([for (i in 1 to 120) "l"], "") ++ " X\\the\\skip0"
]) recovery.check(source)^];

[for (source in [
    "\\dimen0=16383pt X\\the\\dimen0", "\\dimen0=16384pt X\\the\\dimen0",
    "\\dimen0=-16384pt X\\the\\dimen0", "\\dimen0=16383.99999pt X\\the\\dimen0",
    "\\dimen0=999999999999999pt X\\the\\dimen0", "\\dimen0=2147483648.5pt X\\the\\dimen0",
    "\\dimen0=1073741824sp X\\the\\dimen0", "\\dimen0=100000cm X\\the\\dimen0",
    "\\dimen0=16384bogus X\\the\\dimen0", "\\ifdim16384pt>1pt T\\else F\\fi X",
    "\\skip0=16384pt plus 1pt X\\the\\skip0", "\\skip0=1pt plus 16384fil X\\the\\skip0",
    "\\skip0=1pt minus 16384filll X\\the\\skip0", "\\muskip0=16384mu X\\the\\muskip0",
    "\\dimen0=1pt\\dimen1=20000\\dimen0 X\\the\\dimen1",
    "\\pdfpxdimen=1pt\\dimen0=16384px X\\the\\dimen0",
    "\\def\\big{16384}\\dimen0=\\big pt X\\the\\dimen0",
    "\\mag=1\\dimen0=2147483647truein X\\the\\dimen0",
    "\\mag=1\\dimen0=2147483647truesp X\\the\\dimen0",
    "\\dimen0=2147483647in X\\the\\dimen0",
    "\\mag=1\\dimen0=1073742truept X\\the\\dimen0",
    "\\dimen0=-1pt\\dimen1=20000\\dimen0 X\\the\\dimen1"
]) recovery.check(source)^]
