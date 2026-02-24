// trim() unit tests — covers null normalization for empty results

// basic trim
"1. trim both sides:"
trim("  hello  ")
"2. trim no whitespace:"
trim("hello")
"3. trim left only:"
trim("  hello")
"4. trim right only:"
trim("hello  ")

// null normalization: all-whitespace → null
"5. trim spaces → null:"
if (trim("   ") == null) "null" else "NOT null"
"6. trim tab → null:"
if (trim("\t") == null) "null" else "NOT null"
"7. trim newline → null:"
if (trim("\n") == null) "null" else "NOT null"
"8. trim mixed ws → null:"
if (trim(" \t\n ") == null) "null" else "NOT null"
"9. type of trimmed ws:"
type(trim("  "))

// null input → error (not a string)
"10. trim null → error:"
type(trim(null))

// trim_start
"11. trim_start:"
trim_start("  hello  ")
"12. trim_start all ws → null:"
if (trim_start("   ") == null) "null" else "NOT null"
"13. type trim_start ws:"
type(trim_start("\t\n"))

// trim_end
"14. trim_end:"
trim_end("  hello  ")
"15. trim_end all ws → null:"
if (trim_end("   ") == null) "null" else "NOT null"
"16. type trim_end ws:"
type(trim_end("\t\n"))

// edge cases
"17. trim single space → null:"
if (trim(" ") == null) "null" else "NOT null"
"18. trim single char:"
trim("a")
"19. inner ws preserved:"
trim("  hello world  ")

// symbol trim
"20. trim symbol:"
trim('hello')
"21. trim symbol type:"
type(trim('hello'))
