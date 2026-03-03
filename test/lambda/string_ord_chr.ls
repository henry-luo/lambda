// Test script for ord() and chr() functions

"===== ORD/CHR TEST ====="

// ord - ASCII
{t: "ord A", r: ord("A")}
{t: "ord a", r: ord("a")}
{t: "ord 0", r: ord("0")}
{t: "ord space", r: ord(" ")}

// ord - multi-char (first char)
{t: "ord hello", r: ord("hello")}

// ord - UTF-8 multi-byte
{t: "ord é", r: ord("é")}
{t: "ord emoji", r: ord("😀")}
{t: "ord 中", r: ord("中")}

// ord - symbol input
{t: "ord sym A", r: ord('A')}

// ord - empty
{t: "ord empty", r: ord("")}

// chr - ASCII
{t: "chr 65", r: chr(65)}
{t: "chr 97", r: chr(97)}
{t: "chr 48", r: chr(48)}

// chr - UTF-8 multi-byte
{t: "chr 233", r: chr(233)}
{t: "chr emoji", r: chr(128512)}
{t: "chr 中", r: chr(20013)}

// chr - invalid
{t: "chr -1", r: chr(-1)}
{t: "chr too big", r: chr(1114112)}

// round-trip
{t: "rt chr-ord", r: chr(ord("Z"))}
{t: "rt ord-chr", r: ord(chr(65))}

"ALL TESTS COMPLETE"
